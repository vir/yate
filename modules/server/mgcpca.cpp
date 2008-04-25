/**
 * mgcpca.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Media Gateway Control Protocol - Call Agent - also remote data helper
 *  for other protocols
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
#include <yatemgcp.h>

#include <stdlib.h>

using namespace TelEngine;
namespace { // anonymous

class YMGCPEngine : public MGCPEngine
{
public:
    inline YMGCPEngine(const NamedList* params)
	: MGCPEngine(false,0,params)
	{ }
    virtual ~YMGCPEngine();
    virtual bool processEvent(MGCPTransaction* trans, MGCPMessage* msg, void* data);
};

class MGCPWrapper : public DataEndpoint
{
    YCLASS(MGCPWrapper,DataEndpoint)
public:
    MGCPWrapper(CallEndpoint* conn, const char* media, Message& msg);
    ~MGCPWrapper();
    bool sendDTMF(const String& tones);
    void gotDTMF(char tone);
    inline const String& id() const
	{ return m_id; }
    inline const String& ntfyId() const
	{ return m_notify; }
    inline const String& callId() const
	{ return m_master; }
    inline const String& connEp() const
	{ return m_connEp; }
    inline const String& connId() const
	{ return m_connId; }
    inline bool isAudio() const
	{ return m_audio; }
    static MGCPWrapper* find(const CallEndpoint* conn, const String& media);
    static MGCPWrapper* find(const String& id);
    static MGCPWrapper* findNotify(const String& id);
    bool processEvent(MGCPTransaction* tr, MGCPMessage* mm);
    bool rtpMessage(Message& msg);
    RefPointer<MGCPMessage> sendSync(MGCPMessage* mm, const SocketAddr& address);
    void clearConn();
protected:
    virtual bool nativeConnect(DataEndpoint* peer);
private:
    void addParams(MGCPMessage* mm);
    MGCPTransaction* m_tr;
    RefPointer<MGCPMessage> m_msg;
    String m_connId;
    String m_connEp;
    String m_id;
    String m_notify;
    String m_master;
    bool m_audio;
};

class RtpHandler : public MessageHandler
{
public:
    RtpHandler(unsigned int prio) : MessageHandler("chan.rtp",prio) { }
    virtual bool received(Message &msg);
};

class DTMFHandler : public MessageHandler
{
public:
    DTMFHandler() : MessageHandler("chan.dtmf",150) { }
    virtual bool received(Message &msg);
};

class MGCPPlugin : public Module
{
public:
    MGCPPlugin();
    virtual ~MGCPPlugin();
    virtual void initialize();
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
};

static YMGCPEngine* s_engine = 0;
static MGCPEndpoint* s_endpoint = 0;
static String s_defaultEp;

static MGCPPlugin splugin;
static ObjList s_wrappers;
static Mutex s_mutex;


// copy parameter (if present) with new name
bool copyRename(NamedList& dest, const char* dname, const NamedList& src, const String& sname)
{
    if (!sname)
	return false;
    const NamedString* value = src.getParam(sname);
    if (!value)
	return false;
    dest.addParam(dname,*value);
    return true;
}

YMGCPEngine::~YMGCPEngine()
{
    s_engine = 0;
    s_endpoint = 0;
}

bool YMGCPEngine::processEvent(MGCPTransaction* trans, MGCPMessage* msg, void* data)
{
    MGCPWrapper* wrap = YOBJECT(MGCPWrapper,static_cast<GenObject*>(data));
    Debug(this,DebugAll,"YMGCPEngine::processEvent(%p,%p,%p) wrap=%p [%p]",
	trans,msg,data,wrap,this);
    if (!trans)
	return false;
    if (wrap)
	return wrap->processEvent(trans,msg);
    if (!msg)
	return false;
    if (!data && !trans->outgoing() && msg->isCommand()) {
	if (msg->name() == "NTFY") {
	    Debug(this,DebugStub,"NTFY from '%s'",msg->endpointId().c_str());
	    wrap = MGCPWrapper::findNotify(msg->params.getValue("x"));
	    if (wrap)
		return wrap->processEvent(trans,msg);
	    trans->setResponse(515,"Unknown notification-id");
	    return true;
	}
	if (msg->name() == "RSIP") {
	    Debug(this,DebugStub,"RSIP from '%s'",msg->endpointId().c_str());
	    trans->setResponse(200);
	    return true;
	}
	Debug(this,DebugMild,"Unhandled '%s' from '%s'",
	    msg->name().c_str(),msg->endpointId().c_str());
    }
    return false;
}


MGCPWrapper::MGCPWrapper(CallEndpoint* conn, const char* media, Message& msg)
    : DataEndpoint(conn,media),
      m_tr(0)
{
    Debug(&splugin,DebugAll,"MGCPWrapper::MGCPWrapper(%p,'%s') [%p]",
	conn,media,this);
    m_id = "mgcp/";
    m_id << (unsigned int)::random();
    if (conn)
	m_master = conn->id();
    m_master = msg.getValue("id",(conn ? conn->id().c_str() : (const char*)0));
    m_audio = (name() == "audio");
    m_connEp = msg.getValue("mgcp_endpoint",s_defaultEp);
    s_mutex.lock();
    s_wrappers.append(this);
//    setupRTP(localip,rtcp);
    s_mutex.unlock();
}

MGCPWrapper::~MGCPWrapper()
{
    Debug(&splugin,DebugAll,"MGCPWrapper::~MGCPWrapper() '%s' [%p]",
	name().c_str(),this);
    s_mutex.lock();
    s_wrappers.remove(this,false);
    if (m_tr) {
	m_tr->userData(0);
	m_tr = 0;
    }
    s_mutex.unlock();
    m_msg = 0;
    clearConn();
}

bool MGCPWrapper::processEvent(MGCPTransaction* tr, MGCPMessage* mm)
{
    Debug(&splugin,DebugAll,"MGCPWrapper::processEvent(%p,%p) [%p]",
	tr,mm,this);
    if (tr == m_tr) {
	if (!mm || (tr->msgResponse())) {
	    tr->userData(0);
	    m_msg = mm;
	    m_tr = 0;
	}
    }
    else if (mm) {
	if (mm->name() == "NTFY") {
	    String* event = mm->params.getParam("o");
	    if (event) {
		Debug(&splugin,DebugInfo,"Event '%s' [%p]",event->c_str(),this);
	    }
	    tr->setResponse(200);
	    return true;
	}
    }
    return false;
}

bool MGCPWrapper::rtpMessage(Message& msg)
{
    if (!s_endpoint)
	return false;
    const char* cmd = "MDCX";
    if (m_connId.null())
	cmd = "CRCX";
    if (msg.getBoolValue("terminate")) {
	if (m_connId.null())
	    return true;
	cmd = "DLCX";
    }
    MGCPEpInfo* ep = s_endpoint->find(m_connEp);
    if (!ep)
	return false;
    RefPointer<MGCPMessage> mm = new MGCPMessage(s_engine,cmd,ep->toString());
    addParams(mm);
    if (m_connId.null()) {
	copyRename(mm->params,"x-transport",msg,"transport");
	copyRename(mm->params,"x-mediatype",msg,"media");
    }
    copyRename(mm->params,"x-localip",msg,"localip");
    copyRename(mm->params,"x-localport",msg,"localport");
    copyRename(mm->params,"x-remoteip",msg,"remoteip");
    copyRename(mm->params,"x-remoteport",msg,"remoteport");
    copyRename(mm->params,"x-payload",msg,"payload");
    copyRename(mm->params,"x-evpayload",msg,"evpayload");
    copyRename(mm->params,"x-format",msg,"format");
    copyRename(mm->params,"x-direction",msg,"direction");
    copyRename(mm->params,"x-ssrc",msg,"ssrc");
    copyRename(mm->params,"x-drillhole",msg,"drillhole");
    copyRename(mm->params,"x-autoaddr",msg,"autoaddr");
    copyRename(mm->params,"x-anyssrc",msg,"anyssrc");
    mm = sendSync(mm,ep->address);
    if (!mm)
	return false;
    if (m_connId.null())
	m_connId = mm->params.getParam("i");
    if (m_connId.null())
	return false;
    copyRename(msg,"localip",mm->params,"x-localip");
    copyRename(msg,"localport",mm->params,"x-localport");
    msg.setParam("rtpid",id());
    return true;
}

void MGCPWrapper::clearConn()
{
    if (m_connId.null() || !s_endpoint)
	return;
    MGCPEpInfo* ep = s_endpoint->find(m_connEp);
    if (!ep)
	return;
    MGCPMessage* mm = new MGCPMessage(s_engine,"DLCX",ep->toString());
    addParams(mm);
    s_engine->sendCommand(mm,ep->address);
}

void MGCPWrapper::addParams(MGCPMessage* mm)
{
    if (!mm)
	return;
    if (m_connId)
	mm->params.addParam("I",m_connId);
    if (m_master) {
	String callId;
	callId.hexify((void*)m_master.c_str(),m_master.length(),0,true);
	mm->params.addParam("C",callId);
    }
}

RefPointer<MGCPMessage> MGCPWrapper::sendSync(MGCPMessage* mm, const SocketAddr& address)
{
    while (m_msg) {
	if (Thread::check(false))
	    return 0;
	Thread::msleep(10);
    }
    MGCPTransaction* tr = s_engine->sendCommand(mm,address);
    tr->userData(static_cast<GenObject*>(this));
    m_tr = tr;
    while (m_tr == tr)
	Thread::msleep(10);
    RefPointer<MGCPMessage> tmp = m_msg;
    m_msg = 0;
    Debug(&splugin,DebugStub,"MGCPWrapper::sendSync() returning %p [%p]",(void*)tmp,this);
    return tmp;
}

MGCPWrapper* MGCPWrapper::find(const CallEndpoint* conn, const String& media)
{
    if (media.null() || !conn)
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_wrappers;
    for (; l; l=l->next()) {
	const MGCPWrapper *p = static_cast<const MGCPWrapper *>(l->get());
	if (p && (p->getCall() == conn) && (p->name() == media))
	    return const_cast<MGCPWrapper *>(p);
    }
    return 0;
}

MGCPWrapper* MGCPWrapper::find(const String& id)
{
    if (id.null())
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_wrappers;
    for (; l; l=l->next()) {
	const MGCPWrapper *p = static_cast<const MGCPWrapper *>(l->get());
	if (p && (p->id() == id))
	    return const_cast<MGCPWrapper *>(p);
    }
    return 0;
}

MGCPWrapper* MGCPWrapper::findNotify(const String& id)
{
    if (id.null())
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_wrappers;
    for (; l; l=l->next()) {
	const MGCPWrapper *p = static_cast<const MGCPWrapper *>(l->get());
	if (p && (p->ntfyId() == id))
	    return const_cast<MGCPWrapper *>(p);
    }
    return 0;
}

bool MGCPWrapper::sendDTMF(const String& tones)
{
    Debug(&splugin,DebugStub,"MGCPWrapper::sendDTMF('%s') [%p]",
	tones.c_str(),this);
    MGCPEpInfo* ep = s_endpoint->find(m_connEp);
    if (!ep)
	return false;
    MGCPMessage* mm = new MGCPMessage(s_engine,"NTFY",ep->toString());
    addParams(mm);
    String tmp;
    for (unsigned int i = 0; i < tones.length(); i++) {
	if (tmp)
	    tmp << ",";
	tmp << "D/" << tones.at(i);
    }
    mm->params.setParam("O",tmp);
    return s_engine->sendCommand(mm,ep->address) != 0;
}

void MGCPWrapper::gotDTMF(char tone)
{
    Debug(&splugin,DebugInfo,"MGCPWrapper::gotDTMF('%c') [%p]",tone,this);
    if (m_master.null())
	return;
    char buf[2];
    buf[0] = tone;
    buf[1] = 0;
    Message *m = new Message("chan.masquerade");
    m->addParam("id",m_master);
    m->addParam("message","chan.dtmf");
    m->addParam("text",buf);
    m->addParam("detected","mgcp");
    Engine::enqueue(m);
}

bool MGCPWrapper::nativeConnect(DataEndpoint* peer)
{
    MGCPWrapper* other = YOBJECT(MGCPWrapper,peer);
    if (!other)
	return false;
    // check if the other connection is using same endpoint
    if (other->connEp() != m_connEp)
	return false;
    if (other->connId().null()) {
	Debug(&splugin,DebugWarn,"Not bridging to uninitialized %p [%p]",other,this);
	return false;
    }
    Debug(&splugin,DebugStub,"Native bridging to %p [%p]",other,this);
    MGCPEpInfo* ep = s_endpoint->find(m_connEp);
    if (!ep)
	return false;
    MGCPMessage* mm = new MGCPMessage(s_engine,"MDCX",ep->toString());
    addParams(mm);
    mm->params.setParam("Z2",other->connId());
    return s_engine->sendCommand(mm,ep->address) != 0;
}


bool RtpHandler::received(Message& msg)
{
    // refuse calls from a MGCP-GW
    if (!msg.getBoolValue("mgcp_allowed",true))
	return false;
    String trans = msg.getValue("transport");
    if (trans && !trans.startsWith("RTP/"))
	return false;
    Debug(&splugin,DebugAll,"RTP message received");

    MGCPWrapper* w = 0;
    const char* media = msg.getValue("media","audio");
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	w = MGCPWrapper::find(ch,media);
	if (w)
	    Debug(&splugin,DebugAll,"Wrapper %p found by CallEndpoint",w);
    }
    if (!w) {
	w = MGCPWrapper::find(msg.getValue("rtpid"));
	if (w)
	    Debug(&splugin,DebugAll,"Wrapper %p found by ID",w);
    }
    if (!(ch || w)) {
	Debug(&splugin,DebugWarn,"Neither call channel nor MGCP wrapper found!");
	return false;
    }

    if (w)
	return w->rtpMessage(msg);

    if (ch)
	ch->clearEndpoint(media);
    w = new MGCPWrapper(ch,media,msg);
    if (!w->rtpMessage(msg))
	return false;
    if (ch && ch->getPeer())
	w->connect(ch->getPeer()->getEndpoint(media));

    return true;
}


bool DTMFHandler::received(Message& msg)
{
    String targetid(msg.getValue("targetid"));
    if (targetid.null())
	return false;
    String text(msg.getValue("text"));
    if (text.null())
	return false;
    MGCPWrapper* wrap = MGCPWrapper::find(targetid);
    return wrap && wrap->sendDTMF(text);
}


MGCPPlugin::MGCPPlugin()
    : Module("mgcpca","misc")
{
    Output("Loaded module MGCP-CA");
}

MGCPPlugin::~MGCPPlugin()
{
    Output("Unloading module MGCP-CA");
    s_wrappers.clear();
    delete s_engine;
}

void MGCPPlugin::statusParams(String& str)
{
    s_mutex.lock();
    str.append("chans=",",") << s_wrappers.count();
    s_mutex.unlock();
}

void MGCPPlugin::statusDetail(String& str)
{
    s_mutex.lock();
    ObjList* l = s_wrappers.skipNull();
    for (; l; l=l->skipNext()) {
	MGCPWrapper* w = static_cast<MGCPWrapper*>(l->get());
        str.append(w->id(),",") << "=" << w->callId();
    }
    s_mutex.unlock();
}

void MGCPPlugin::initialize()
{
    Output("Initializing module MGCP Call Agent");
    Configuration cfg(Engine::configFile("mgcpca"));
    setup();
    NamedList* engSect = cfg.getSection("engine");
    if (s_engine && engSect)
	s_engine->initialize(*engSect);
    while (!s_engine) {
	if (!(engSect && engSect->getBoolValue("enabled",true)))
	    break;
	int n = cfg.sections();
	for (int i = 0; i < n; i++) {
	    NamedList* sect = cfg.getSection(i);
	    if (!sect)
		continue;
	    String name(*sect);
	    if (name.startSkip("gw") && name) {
		const char* host = sect->getValue("host");
		if (!host)
		    continue;
		if (!s_engine) {
		    s_engine = new YMGCPEngine(engSect);
		    s_engine->debugChain(this);
		    s_endpoint = new MGCPEndpoint(
			s_engine,
			cfg.getValue("endpoint","user","yate"),
			cfg.getValue("endpoint","host",s_engine->address().host()),
			cfg.getIntValue("endpoint","port")
		    );
		}
		MGCPEpInfo* ep = s_endpoint->append(
		    sect->getValue("user",name),
		    host,
		    sect->getIntValue("port",0)
		);
		if (ep) {
		    if (s_defaultEp.null() || sect->getBoolValue("default"))
			s_defaultEp = ep->toString();
		}
		else
		    Debug(this,DebugWarn,"Could not set endpoint for gateway '%s'",
			name.c_str());
	    }
	}
	if (!s_engine) {
	    Debug(this,DebugAll,"No gateways defined so module not initialized.");
	    break;
	}
	Debug(this,DebugNote,"Default remote endpoint: '%s'",s_defaultEp.c_str());
	Engine::install(new RtpHandler(cfg.getIntValue("general","priority",80)));
	Engine::install(new DTMFHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
