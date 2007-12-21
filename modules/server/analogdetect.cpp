/**
 * analogdetect.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Analog data detector
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatephone.h>
#include <yatemodem.h>

using namespace TelEngine;
namespace { // anonymous

class ADConsumer;                        // Base class for all module's consumers (UART)
class ETSIConsumer;                      // Data consumer for an ETSIModem
class ADModule;                          // The module
class ADProxy;                           // Proxy class used to forward modulated data to other module
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
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
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
    UART* m_uart;                        // UART descendant class
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
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    // Process a request to attach an ETSI detector (src is a valid pointer) or generator (src is 0)
    bool attachETSI(Message& msg, DataSource* src, String& type, const char* notify);
private:
    unsigned int m_id;                   // Next consumer's id
    bool m_init;                         // Already initialized flag
    String m_prefix;                     // Module's prefix
};

// Proxy class used to forward modulated data to other module
class ADProxy : public RefObject
{
public:
    ADProxy(RefObject* ep, DataBlock* data);
    virtual ~ADProxy();
    // Get endpoint and data
    virtual void* getObject(const String& name) const;
protected:
    // Deref endpoint. Remove data if still owned
    virtual void destroyed();
private:
    RefObject* m_ep;                     // The endpoint used to send modulated data
    mutable DataBlock* m_data;           // The modulated data
};

// chan.attach handler
class ChanAttachHandler : public MessageHandler
{
public:
    inline ChanAttachHandler()
	: MessageHandler("chan.attach",100)
	{}
    virtual bool received(Message& msg);
};

/**
 * Module's data
 */
static ADModule plugin;
static ObjList s_consumers;              // Consumers list
static unsigned int s_count = 0;         // The number of active consumers


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
    s_consumers.append(this);
    s_count++;
}

// Process received data
void ADConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (m_terminated)
	return;
    m_terminated = !process(data);
    if (!m_terminated)
	return;
    DDebug(&plugin,DebugAll,"Terminated %s targetid=%s [%p]",
	m_id.c_str(),m_targetid.c_str(),this);
    Engine::enqueue(chanNotify("terminate","reason",getTerminateReason()));
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
    : Module("analogdetect","misc"),
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

// Used to test the library
// Undef to test
//#define TEST_MODEM_LIBRARY

#if defined(TEST_MODEM_LIBRARY)
void testModemLibrary(Configuration& cfg);
#endif

void ADModule::initialize()
{
    Output("Initializing module Analog Detector");

    Configuration cfg(Engine::configFile("analogdetect"));
    cfg.load();

    if (!m_init) {
	setup();
	installRelay(Command);
	installRelay(Halt);
	Engine::install(new ChanAttachHandler);
    }
    m_init = true;

#if defined(TEST_MODEM_LIBRARY)
    testModemLibrary(cfg);
#endif
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
	    src = static_cast<DataSource*>(sender->getObject("DataSource"));
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

bool ADModule::received(Message& msg, int id)
{
    if (id == Halt) {
	lock();
	s_consumers.clear();
	unlock();
	return Module::received(msg,id);
    }
    return Module::received(msg,id);
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
	bool ok = cons->getConnSource();
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
    ADProxy* proxy = new ADProxy(msg.userData(),buffer);
    send.userData(proxy);
    TelEngine::destruct(proxy);
    send.addParam("override","tone/rawdata");
    send.addParam("single",String::boolText(true));
    return Engine::dispatch(send);
}


/**
 * ADProxy
 */
ADProxy::ADProxy(RefObject* ep, DataBlock* data)
    : m_ep(0),
    m_data(data)
{
    if (ep && ep->ref())
	m_ep = ep;
}

ADProxy::~ADProxy()
{
}

void* ADProxy::getObject(const String& name) const
{
    if (name == "rawdata") {
	DataBlock* tmp = m_data;
	m_data = 0;
	return tmp;
    }
    return m_ep ? m_ep->getObject(name) : 0;
}

// Deref endpoint. Remove data if still owned
void ADProxy::destroyed()
{
    TelEngine::destruct(m_ep);
    TelEngine::destruct(m_data);
}


/**
 * ChanAttachHandler
 */
bool ChanAttachHandler::received(Message& msg)
{
    return plugin.chanAttach(msg);
}


#if defined(TEST_MODEM_LIBRARY)

#include <fcntl.h>

void testModemLibrary(Configuration& cfg)
{
    NamedList dummy("");
    NamedList* test = cfg.getSection("test");
    if (!test)
	test = &dummy;

    const char* caller = test->getValue("caller","caller");
    const char* callername = test->getValue("callername","callername");
    Output("Testing libyatemodem caller=%s callername=%s",caller,callername);

    NamedList modemParams("");
    modemParams.addParam("bufferbits","true");
    ETSIModem* modem = new ETSIModem(modemParams,"TEST");
    modem->debugLevel(DebugAll);

    DataBlock buffer;

    NamedList params(lookup(ETSIModem::MsgCallSetup,ETSIModem::s_msg));
    params.addParam("caller",caller);
    params.addParam("callername",callername);
    if (modem->modulate(buffer,params))
	modem->demodulate(buffer);

    delete modem;
    modem = new ETSIModem(modemParams,"TEST");
    modem->debugLevel(DebugAll);

    const char* filename = test->getValue("filename");
    unsigned int len = (unsigned int)test->getIntValue("length");
    int chunk = test->getIntValue("chunk",0);
    if (chunk == 0)
	chunk = len ? len : 1;
    else if (chunk < 0)
	chunk = 160;
    unsigned char buf[len];
    Output("Testing libyatemodem filename=%s len=%u chunk=%d",filename,len,chunk);
    if (filename) {
	int handler = open(filename,O_RDONLY);
	if (handler != -1)
	    read(handler,buf,len);
	else
	    Debug(modem,DebugWarn,"Error opening %s",filename);
	close(handler);
    }
    unsigned int n = len / chunk;
    for (unsigned char* p = buf; n; n--, p += chunk) {
	DataBlock tmp(p,chunk,false);
	modem->demodulate(tmp);
	tmp.clear(false);
    }

    delete modem;

    Output("libyatemodem test terminated");
}
#endif // TEST_MODEM_LIBRARY

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
