/**
 * mrcpspeech.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Detector and synthesizer for voice and tones using a MRCP v2 server
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

using namespace TelEngine;

namespace { // anonymous

class MrcpConnection : public CallEndpoint
{
public:
    inline MrcpConnection(const char* id, const char* original)
	: CallEndpoint(id),
	  m_original(original), m_socket(0)
	{ }
    virtual ~MrcpConnection();
    bool init(Message& msg, const char* target);
    bool answered(Message& msg);
private:
    String m_original;
    Socket* m_socket;
};

class MrcpConsumer : public DataConsumer
{
    YCLASS(MrcpConsumer,DataConsumer)
public:
    MrcpConsumer(const String& id, const char* target, const char* format = 0);
    virtual ~MrcpConsumer();
    virtual bool setFormat(const DataFormat& format);
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    bool init(Message& msg);
private:
    void cleanup();
    DataSource* m_source;
    MrcpConnection* m_chan;
    String m_id;
    String m_target;
};

class MrcpModule : public Module
{
public:
    MrcpModule();
    virtual ~MrcpModule();
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
private:
    bool m_first;
};

static Mutex s_mutex(false,"MrcpSpeech");
static ObjList s_conns;
static int s_count = 0;
static int s_total = 0;

static MrcpModule plugin;


class AttachHandler : public MessageHandler
{
public:
    AttachHandler()
	: MessageHandler("chan.attach",100,plugin.name())
	{ }
    virtual bool received(Message& msg);
};

class RecordHandler : public MessageHandler
{
public:
    RecordHandler()
	: MessageHandler("chan.record",100,plugin.name())
	{ }
    virtual bool received(Message& msg);
};

class MrcpRtpHandler : public MessageHandler
{
public:
    MrcpRtpHandler()
	: MessageHandler("chan.rtp",150,plugin.name())
	{ }
    virtual bool received(Message& msg);
};


MrcpConsumer::MrcpConsumer(const String& id, const char* target, const char* format)
    : DataConsumer(format),
      m_source(0), m_chan(0), m_id(id)
{
    s_mutex.lock();
    s_count++;
    s_mutex.unlock();
    if (target) {
	m_target = target;
	m_target >> "mrcp/";
	m_target = "sip/" + m_target;
    }
    Debug(&plugin,DebugAll,"MrcpConsumer::MrcpConsumer('%s','%s','%s') [%p]",
	id.c_str(),target,format,this);
}

MrcpConsumer::~MrcpConsumer()
{
    Debug(&plugin,DebugAll,"MrcpConsumer::~MrcpConsumer '%s' [%p]",
	m_id.c_str(),this);
    s_mutex.lock();
    s_count--;
    s_mutex.unlock();
    cleanup();
}

bool MrcpConsumer::init(Message& msg)
{
    if (m_chan)
	return false;
    String id("mrcp/");
    s_mutex.lock();
    s_total++;
    id << s_total;
    s_mutex.unlock();
    m_source = new DataSource(m_format);
    m_chan = new MrcpConnection(id,m_id);
    m_chan->setSource(m_source);
    if (m_chan->init(msg,m_target))
	return true;
    Debug(&plugin,DebugWarn,"Failed to start connection '%s' for '%s' [%p]",
	id.c_str(),m_id.c_str(),this);
    cleanup();
    return false;
}

void MrcpConsumer::cleanup()
{
    Debug(&plugin,DebugAll,"MrcpConsumer::cleanup() '%s' s=%p c=%p [%p]",
	m_id.c_str(),m_source,m_chan,this);
    if (m_source) {
	m_source->deref();
	m_source = 0;
    }
    if (m_chan) {
	m_chan->disconnect();
	m_chan->deref();
	m_chan = 0;
    }
}

bool MrcpConsumer::setFormat(const DataFormat& format)
{
    Debug(&plugin,DebugAll,"MrcpConsumer::setFormat('%s') '%s' s=%p c=%p [%p]",
	format.c_str(),m_id.c_str(),m_source,m_chan,this);
    return m_source && m_source->setFormat(format);
}

unsigned long MrcpConsumer::Consume(const DataBlock& data, unsigned long timeDelta, unsigned long flags)
{
    return m_source ? m_source->Forward(data,timeDelta,flags) : 0;
}


MrcpConnection::~MrcpConnection()
{
    s_mutex.lock();
    s_conns.remove(this,false);
    s_mutex.unlock();
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
	m_socket = 0;
    }
}

bool MrcpConnection::init(Message& msg, const char* target)
{
    if (!target)
	return false;
    s_mutex.lock();
    if (!s_conns.find(this))
	s_conns.append(this);
    s_mutex.unlock();
    Message m("call.execute");
    m.addParam("id",id());
    m.addParam("callto",target);
    m.copyParam(msg,"caller");
    m.copyParam(msg,"called");
    m.addParam("media",String::boolText(true));
    m.addParam("media_application",String::boolText(true));
    m.addParam("transport_application","TCP/MRCPv2");
    m.addParam("formats_application","1"); // defined by the standard
    m.userData(this);
    return Engine::dispatch(m);
}

bool MrcpConnection::answered(Message& msg)
{
    Debug(&plugin,DebugAll,"MrcpConnection::answered() '%s' [%p]",
	id().c_str(),this);
    int port = msg.getIntValue("rtp_port_application");
    if (port > 0) {
	return true;
    }
    return false;
}


// Attach a tone detector on "chan.attach" as consumer or sniffer
bool AttachHandler::received(Message& msg)
{
    String cons(msg.getValue("consumer"));
    if (!cons.startsWith("mrcp/"))
	cons.clear();
    String snif(msg.getValue("sniffer"));
    if (!snif.startsWith("mrcp/"))
	snif.clear();
    if (cons.null() && snif.null())
	return false;
    CallEndpoint* ch = static_cast<CallEndpoint *>(msg.userObject(YATOM("CallEndpoint")));
    if (ch) {
	if (cons) {
	    MrcpConsumer* c = new MrcpConsumer(ch->id(),cons,msg.getValue("format","slin"));
	    if (c->init(msg))
		ch->setConsumer(c);
	    c->deref();
	}
	if (snif) {
	    RefPointer<DataEndpoint> de = ch->setEndpoint();
	    // try to reinit sniffer if one already exists
	    MrcpConsumer* c = static_cast<MrcpConsumer*>(de->getSniffer(snif));
	    if (c)
		c->init(msg);
	    else {
		c = new MrcpConsumer(ch->id(),snif,msg.getValue("format","slin"));
		if (c->init(msg))
		    de->addSniffer(c);
		c->deref();
	    }
	}
	return msg.getBoolValue("single");
    }
    else
	Debug(&plugin,DebugWarn,"Attach request with no call endpoint!");
    return false;
}


// Attach a tone detector on "chan.record" - needs just a CallEndpoint
bool RecordHandler::received(Message& msg)
{
    String src(msg.getValue("call"));
    String id(msg.getValue("id"));
    if (!src.startsWith("mrcp/"))
	return false;
    CallEndpoint* ch = static_cast<CallEndpoint *>(msg.userObject(YATOM("CallEndpoint")));
    RefPointer<DataEndpoint> de = static_cast<DataEndpoint *>(msg.userObject(YATOM("DataEndpoint")));
    if (ch) {
	id = ch->id();
	if (!de)
	    de = ch->setEndpoint();
    }
    if (de) {
	MrcpConsumer* c = new MrcpConsumer(id,src,msg.getValue("format","slin"));
	if (c->init(msg))
	    de->setCallRecord(c);
	c->deref();
	return true;
    }
    else
	Debug(&plugin,DebugWarn,"Record request with no call endpoint!");
    return false;
}


bool MrcpRtpHandler::received(Message& msg)
{
    String trans = msg.getValue("transport");
    trans.toUpper();
    bool tls = false;
    if (trans == "TCP/TLS/MRCPV2")
	tls = true;
    else if (trans != "TCP/MRCPV2")
	return false;
    Debug(&plugin,DebugAll,"RTP message received, TLS: %s",String::boolText(tls));
    return true;
}


MrcpModule::MrcpModule()
    : Module("mrcp","misc"), m_first(true)
{
    Output("Loaded module MRCP");
}

MrcpModule::~MrcpModule()
{
    Output("Unloading module MRCP");
}

bool MrcpModule::received(Message& msg, int id)
{
    if (id == Answered) {
	const String* cid = msg.getParam("targetid");
	if (!cid)
	    cid = msg.getParam("peerid");
	if (!(cid && cid->startsWith("mrcp/")))
	    return false;
	s_mutex.lock();
	RefPointer<MrcpConnection> conn = static_cast<MrcpConnection*>(s_conns[*cid]);
	s_mutex.unlock();
	return conn && conn->answered(msg);
    }
    return Module::received(msg,id);
}

void MrcpModule::statusParams(String& str)
{
    str.append("count=",",") << s_count;
    str.append("total=",",") << s_total;
}

void MrcpModule::initialize()
{
    Output("Initializing module MrcpSpeech");
    setup();
    if (m_first) {
	m_first = false;
	Engine::install(new AttachHandler);
	Engine::install(new RecordHandler);
	Engine::install(new MrcpRtpHandler);
	installRelay(Answered);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
