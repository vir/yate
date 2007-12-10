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
    // Process a request to attach a data consumer
    bool attachConsumer(Message& msg, DataSource* src, String& type, const char* notify);
    // Process a request to attach a data source
    bool attachSource(Message& msg, DataConsumer* cons, String& type, const char* notify);
protected:
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
private:
    unsigned int m_id;                   // Next consumer's id
    bool m_init;                         // Already initialized flag
    String m_prefix;                     // Module's prefix
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
static Mutex s_mutex(true);              // Lock module


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
    Lock lock(s_mutex);
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
    Lock lock(s_mutex);
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

//TODO: remove after test
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

    if (!cfg.getBoolValue("general","testing",false))
	return;

    String filename = "//home/marian/callerid.raw.";
    String type[3] = {"etsi","bell","etsi-ets"};
    unsigned int len[3] = {27200,22400,49280};

    debugLevel(10);
    unsigned int test = cfg.getIntValue("general","test",0);
    if (test > 2)
	test = 2;
    unsigned int chunk = cfg.getIntValue("general","chunk",0);
    if (!chunk)
	chunk = 160;

    filename << type[test];
    int handler = open(filename,O_RDONLY);
    if (handler == -1) {
	Debug(this,DebugWarn,"Error opening %s",filename.c_str());
	return;
    }

    NamedList p("");
    String id;
    id << "test/" << type[test];
    ETSIConsumer* cons = new ETSIConsumer(id,"TEST",p);

    unsigned char buf[len[test]];
    read(handler,buf,len[test]);
    unsigned int n = len[test]/chunk;
    for (unsigned char* p = buf; n; n--, p += chunk) {
	DataBlock tmp(p,chunk,false);
	cons->demodulate(tmp);
	tmp.clear(false);
    }
    cons->deref();
}

bool ADModule::received(Message& msg, int id)
{
    if (id == Halt) {
	s_mutex.lock();
	s_consumers.clear();
	s_mutex.unlock();
	return Module::received(msg,id);
    }
    return Module::received(msg,id);
}

// Process a request to attach data consumer
bool ADModule::attachConsumer(Message& msg, DataSource* src, String& type, const char* notify)
{
    XDebug(this,DebugAll,"Attaching consumer '%s' for '%s'",type.c_str(),notify);

    String id;
    DataConsumer* cons = 0;
    if (type == "callsetup") {
	s_mutex.lock();
	id << m_prefix << m_id++;
	s_mutex.unlock();
	cons = new ETSIConsumer(id,notify,msg);
    }
    else {
	msg.setParam("reason","unknown-detector-type");
	return false;
    }
    DataTranslator::attachChain(src,cons);
    bool ok = cons->getConnSource();
    if (ok)
	msg.userData(cons);
    else
	msg.setParam("reason","attach-failure");
    TelEngine::destruct(cons);
    return ok;
}

// Process a request to attach data source
bool ADModule::attachSource(Message& msg, DataConsumer* cons, String& type, const char* notify)
{
    // TODO: remove it after testing
    msg.setParam("reason","not-implemented");
    return false;

    DDebug(this,DebugAll,"Attaching source '%s' for '%s'",type.c_str(),notify);

    if (type != "callsetup") {
	msg.setParam("reason","unknown-message-type");
	return false;
    }

    String id = prefix();
    id << "callsetup/" << notify;
    ETSIModem* modem = new ETSIModem(msg,id);
    modem->debugChain(this);

    NamedList params(lookup(ETSIModem::MsgCallSetup,ETSIModem::s_msg));
    params.copyParams(msg,"datetime,caller,callername");

    DataBlock buffer;
    bool ok = modem->modulate(buffer,params);
    delete modem;
    if (!ok) {
	msg.setParam("reason",params.getValue("error","invalid-message"));
	return false;
    }

    DDebug(this,DebugAll,"Sending %u buffer for %s",buffer.length(),notify);
    DataSource* src = new DataSource("slin");
    ok = src->attach(cons);
    if (ok)
	src->Forward(buffer);
    else
	msg.setParam("reason","attach-failure");
    TelEngine::destruct(src);
    return ok;
}

void ADModule::statusParams(String& str)
{
    Module::statusParams(str);
    Lock lock(s_mutex);
    str << "count=" << s_count;
    for (ObjList* o = s_consumers.skipNull(); o; o = o->skipNext()) {
	ADConsumer* c = static_cast<ADConsumer*>(o->get());
	str << "," << c->m_id << "=" << c->m_targetid;
    }
}


/**
 * ChanAttachHandler
 */
bool ChanAttachHandler::received(Message& msg)
{
    DataSource* src = 0;
    DataConsumer* cons = 0;

    String type = msg.getValue("consumer");
    if (!type.startSkip(plugin.prefix(),false)) {
	type = msg.getValue("source");
	if (!type.startSkip(plugin.prefix(),false))
	    return false;
	if (msg.userData())
	    cons = static_cast<DataConsumer*>(msg.userData()->getObject("DataConsumer"));
    }
    else if (msg.userData())
	src = static_cast<DataSource*>(msg.userData()->getObject("DataSource"));

    if (!(src || cons)) {
	msg.setParam("reason","nodata");
	return false;
    }

    const char* modemtype = msg.getValue("modemtype");
    if (modemtype && *modemtype) {
	int mtype = lookup(modemtype,FSKModem::s_typeName);
	switch (mtype) {
	    case FSKModem::ETSI:
		break;
	    default:
		msg.setParam("reason","unknown-modem-type");
		return false;
	}
    }

    const char* notify = msg.getValue("notify");
    if (src)
	return plugin.attachConsumer(msg,src,type,notify);
    return plugin.attachSource(msg,cons,type,notify);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
