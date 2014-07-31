/**
 * analogdetect.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Analog data detector
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <yatephone.h>
#include <yatemodem.h>

using namespace TelEngine;
namespace { // anonymous

class ADConsumer;                        // Base class for all module's consumers (UART)
class ETSIConsumer;                      // Data consumer for an ETSIModem
class ADModule;                          // The module
class ChanAttachHandler;                 // chan.attach handler

// Base class for all module's consumers
class ADConsumer : public DataConsumer
{
    friend class ADModule;
public:
    ADConsumer(const String& id, const char* notify);
    virtual ~ADConsumer()
	{}
    // Process received data
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    // Remove from module's consumer list
    virtual void destroyed();
protected:
    // Create a chan.notify message
    Message* chanNotify(const char* operation, const char* param = 0, const char* value = 0);
    // Consume data. Return false to stop processing
    virtual bool process(const DataBlock& data)
	{ return false; }
    // Get termination reason from descendents
    virtual const char* getTerminateReason()
	{ return 0; }

    String m_id;                         // Consumer's id
    String m_targetid;                   // The target for chan.notify
private:
    bool m_terminated;                   // Stop processing data
};

// Data consumer for call setup info (bit collector)
class ETSIConsumer : public ADConsumer, public ETSIModem
{
public:
    ETSIConsumer(const String& id, const char* notify, const NamedList& params);
    virtual ~ETSIConsumer()
	{}
    // Notification from modem that the FSK start was detected
    // Return false to stop feeding data
    virtual bool fskStarted() {
	    Engine::enqueue(chanNotify("start"));
	    return true;
	}
protected:
    // Consume data. Return false to stop processing
    virtual bool process(const DataBlock& data)
	{ return demodulate(data); }
    // Process a list of received message parameters
    // Return false to stop processing data
    virtual bool recvParams(MsgType msg, const NamedList& params);
    virtual const char* getTerminateReason()
	{ return lookup(UART::error(),UART::s_errors); }
};

// The module
class ADModule : public Module
{
public:
    ADModule();
    ~ADModule();
    inline const String& prefix()
	{ return m_prefix; }
    virtual void initialize();
    // chan.attach handler
    bool chanAttach(Message& msg);
    // Get next consumer's id
    inline unsigned int nextId() {
	    Lock lock(this);
	    return m_id++;
	}
protected:
    virtual void statusParams(String& str);
    // Process a request to attach an ETSI detector (src is a valid pointer) or generator (src is 0)
    bool attachETSI(Message& msg, DataSource* src, String& type, const char* notify);
private:
    unsigned int m_id;                   // Next consumer's id
    bool m_init;                         // Already initialized flag
    String m_prefix;                     // Module's prefix
};

/**
 * Module's data
 */
static ADModule plugin;
static ObjList s_consumers;              // Consumers list
static unsigned int s_count = 0;         // The number of active consumers


// chan.attach handler
class ChanAttachHandler : public MessageHandler
{
public:
    inline ChanAttachHandler()
	: MessageHandler("chan.attach",100,plugin.name())
	{ }
    virtual bool received(Message& msg);
};


/**
 * ADConsumer
 */
ADConsumer::ADConsumer(const String& id, const char* notify)
    : DataConsumer("slin"),
    m_id(id),
    m_targetid(notify),
    m_terminated(false)
{
    DDebug(&plugin,DebugAll,"Created %s targetid=%s [%p]",
	m_id.c_str(),m_targetid.c_str(),this);
    Lock lock(plugin);
    s_consumers.append(this)->setDelete(false);
    s_count++;
}

// Process received data
unsigned long ADConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (m_terminated)
	return 0;
    m_terminated = !process(data);
    if (!m_terminated)
	return invalidStamp();
    DDebug(&plugin,DebugAll,"Terminated %s targetid=%s [%p]",
	m_id.c_str(),m_targetid.c_str(),this);
    Engine::enqueue(chanNotify("terminate","reason",getTerminateReason()));
    return invalidStamp();
}

// Remove from module's consumer list
void ADConsumer::destroyed()
{
    Lock lock(plugin);
    s_consumers.remove(this,false);
    s_count--;
    DDebug(&plugin,DebugAll,"Destroyed %s targetid=%s [%p]",
	m_id.c_str(),m_targetid.c_str(),this);
}

// Create a chan.notify message
Message* ADConsumer::chanNotify(const char* operation, const char* param, const char* value)
{
    Message* m = new Message("chan.notify");
    m->addParam("module",plugin.debugName());
    m->addParam("id",m_id);
    m->addParam("targetid",m_targetid);
    m->addParam("operation",operation);
    if (param)
	m->addParam(param,value);
    m->userData(this);
    return m;
}


/**
 * ETSIConsumer
 */
ETSIConsumer::ETSIConsumer(const String& id, const char* notify, const NamedList& params)
    : ADConsumer(id,notify),
    ETSIModem(params,m_id)
{
    debugChain(&plugin);
}

// Process a list of received message parameters
// Return false to stop processing data
bool ETSIConsumer::recvParams(MsgType msg, const NamedList& params)
{
    Message* m = 0;
    switch (msg) {
	case ETSIModem::MsgCallSetup:
	    m = chanNotify("setup");
	    break;
	case ETSIModem::MsgMWI:
	    m = chanNotify("message-summary");
	    break;
	case ETSIModem::MsgCharge:
	    m = chanNotify("charge");
	    break;
	case ETSIModem::MsgSMS:
	    m = chanNotify("sms");
	    break;
	default:
	    Debug(this,DebugStub,"Can't process message %s [%p]",
		lookup(msg,ETSIModem::s_msg),this);
	    return false;
    }

    DDebug(this,DebugAll,"recvParams(%s) operation=%s [%p]",
	lookup(msg,ETSIModem::s_msg),m->getValue("operation"),this);

    unsigned int count = params.count();
    for (unsigned int i = 0; i < count; i++) {
	NamedString* param = params.getParam(i);
	if (param)
	    m->addParam(param->name(),*param);
    }
    Engine::dispatch(*m);
    TelEngine::destruct(m);
    return false;
}


/**
 * ADModule
 */
ADModule::ADModule()
    : Module("analogdetect","misc",true),
    m_id(1),
    m_init(false)
{
    Output("Loaded module Analog Detector");
    m_prefix << debugName() << "/";
}

ADModule::~ADModule()
{
    Output("Unloading module Analog Detector");
}

void ADModule::initialize()
{
    Output("Initializing module Analog Detector");

    if (!m_init) {
	setup();
	installRelay(Command);
	installRelay(Halt);
	Engine::install(new ChanAttachHandler);
    }
    m_init = true;
}

// chan.attach handler
bool ADModule::chanAttach(Message& msg)
{
    int detect = -1;
    DataSource* src= 0;
    RefObject* sender = msg.userData();

    // Check if requested a detector or a generator
    String type = msg.getValue("consumer");
    if (type.startSkip(plugin.prefix(),false)) {
	if (sender)
	    src = static_cast<DataSource*>(sender->getObject(YATOM("DataSource")));
	if (src)
	    detect = 1;
	else
	    msg.setParam("reason","nodata");
    }
    else {
	type = msg.getValue("source");
	if (type.startSkip(plugin.prefix(),false))
	    detect = 0;
    }
    if (detect == -1)
	return false;

    const char* notify = msg.getValue("notify");
    const char* defModem = lookup(FSKModem::ETSI,FSKModem::s_typeName);
    const char* modemType = msg.getValue("modemtype",defModem);
    int mType = lookup(modemType,FSKModem::s_typeName);
    XDebug(this,DebugAll,"Request to create '%s' %s for '%s' modemtype=%s",
	type.c_str(),detect?"detector":"generator",notify,modemType);

    if (mType == FSKModem::ETSI)
	return attachETSI(msg,src,type,notify);

    msg.setParam("reason","unknown-modem-type");
    return false;
}

void ADModule::statusParams(String& str)
{
    Module::statusParams(str);
    Lock lock(this);
    str << "count=" << s_count;
    for (ObjList* o = s_consumers.skipNull(); o; o = o->skipNext()) {
	ADConsumer* c = static_cast<ADConsumer*>(o->get());
	str << "," << c->m_id << "=" << c->m_targetid;
    }
}

// Process a request to attach an ETSI detector (src is a valid pointer) or generator (src is 0)
bool ADModule::attachETSI(Message& msg, DataSource* src, String& type, const char* notify)
{
    int t = 0xffffffff;

    // Check type
    if (type == "callsetup")
	t = ETSIModem::MsgCallSetup;

    if (t == (int)0xffffffff) {
	if (src)
	    msg.setParam("reason","unknown-detector-type");
	else
	    msg.setParam("reason","unknown-generator-type");
	return false;
    }

    String id = prefix();

    // Detector
    if (src) {
	id << nextId();
	DataConsumer* cons = new ETSIConsumer(id,notify,msg);;
	DataTranslator::attachChain(src,cons);
	bool ok = (cons->getConnSource() != 0);
	if (ok)
	    msg.userData(cons);
	else
	    msg.setParam("reason","attach-failure");
	TelEngine::destruct(cons);
	return ok;
    }

    // Generator
    id << "callsetup/" << notify;
    ETSIModem modem(msg,id);
    modem.debugChain(this);
    NamedList params(lookup(t,ETSIModem::s_msg));
    for (int i = 0; ETSIModem::s_msgParams[i].token; i++) {
	NamedString* p = msg.getParam(ETSIModem::s_msgParams[i].token);
	if (p)
	    params.addParam(p->name(),*p);
    }

    DataBlock* buffer = new DataBlock;
    if (!modem.modulate(*buffer,params)) {
	TelEngine::destruct(buffer);
	msg.setParam("reason",params.getValue("error","invalid-message"));
	return false;
    }

    Message send("chan.attach");
    send.userData(msg.userData());
    send.addParam("override","tone/rawdata");
    send.addParam("single",String::boolText(true));
    send.addParam(new NamedPointer("rawdata",buffer));
    return Engine::dispatch(send);
}


/**
 * ChanAttachHandler
 */
bool ChanAttachHandler::received(Message& msg)
{
    return plugin.chanAttach(msg);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
