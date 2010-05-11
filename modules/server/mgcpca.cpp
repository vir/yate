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
#include <yatesig.h>
#include <yatesdp.h>

#include <stdlib.h>
#include <string.h>

using namespace TelEngine;
namespace { // anonymous

class MGCPCircuit;

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
    MGCPWrapper(CallEndpoint* conn, const char* media, Message& msg, const char* epId);
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
    bool processNotify(MGCPTransaction* tr, MGCPMessage* mm, const String& event);
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

class MGCPSpan : public SignallingCircuitSpan
{
public:
    MGCPSpan(const NamedList& params, const char* name, const MGCPEpInfo& ep);
    virtual ~MGCPSpan();
    inline const String& ntfyId() const
	{ return m_notify; }
    inline const MGCPEndpointId& epId() const
	{ return m_epId; }
    inline MGCPEndpointId& epId()
	{ return m_epId; }
    inline bool operational() const
	{ return m_operational; }
    inline const String& address() const
	{ return m_address; }
    inline const char* version() const
	{ return m_version.null() ? "MGCP 1.0" : m_version.c_str(); }
    inline bool fxo() const
	{ return m_fxo; }
    inline bool fxs() const
	{ return m_fxs; }
    bool ownsId(const String& rqId) const;
    static SignallingComponent* create(const String& type, const NamedList& name);
    static MGCPSpan* findNotify(const String& id);
    bool getBoolParam(const String& param, bool defValue) const;
    bool matchEndpoint(const MGCPEndpointId& ep);
    bool processEvent(MGCPTransaction* tr, MGCPMessage* mm);
    bool processNotify(MGCPTransaction* tr, MGCPMessage* mm, const String& event, const String& requestId);
    bool processRestart(MGCPTransaction* tr, MGCPMessage* mm, const String& method);
    bool processDelete(MGCPTransaction* tr, MGCPMessage* mm, const String& error);
private:
    bool init(const NamedList& params);
    void clearCircuits();
    MGCPCircuit* findCircuit(const String& epId, const String& rqId = String::empty()) const;
    void operational(bool active);
    void operational(const SocketAddr& address);
    MGCPCircuit** m_circuits;
    unsigned int m_count;
    MGCPEndpointId m_epId;
    bool m_operational;
    bool m_rtpForward;
    bool m_sdpForward;
    bool m_fxo;
    bool m_fxs;
    String m_notify;
    String m_address;
    String m_version;
};

class MGCPCircuit : public SignallingCircuit, public SDPSession
{
public:
    MGCPCircuit(unsigned int code, MGCPSpan* span, const char* id);
    virtual ~MGCPCircuit();
    virtual void* getObject(const String& name) const;
    virtual bool status(Status newStat, bool sync);
    virtual bool updateFormat(const char* format, int direction);
    virtual bool setParam(const String& param, const String& value);
    virtual bool getParam(const String& param, String& value) const;
    virtual bool getBoolParam(const String& param, bool defValue) const;
    virtual bool setParams(const NamedList& params);
    virtual bool getParams(NamedList& params, const String& category = String::empty());
    virtual bool sendEvent(SignallingCircuitEvent::Type type, NamedList* params);
    inline const String& epId() const
	{ return m_epId; }
    inline const String& ntfyId() const
	{ return m_notify; }
    inline const String& connId() const
	{ return m_connId; }
    inline bool hasRtp() const
	{ return m_source || m_consumer; }
    inline bool hasLocalRtp() const
	{ return m_rtpLocalAddr || m_localRawSdp; }
    inline MGCPSpan* mySpan()
	{ return static_cast<MGCPSpan*>(span()); }
    inline const MGCPSpan* mySpan() const
	{ return static_cast<const MGCPSpan*>(span()); }
    inline bool fxo() const
	{ return mySpan()->fxo(); }
    inline bool fxs() const
	{ return mySpan()->fxs(); }
    inline void needClear()
	{ m_needClear = true; }
    bool processEvent(MGCPTransaction* tr, MGCPMessage* mm);
    bool processNotify(const String& package, const String& event, const String& fullName);
    void processDelete(MGCPMessage* mm, const String& error);
    void clearConn(bool force = false);
protected:
    // Create a chan.rtp message pointing to the circuit
    virtual Message* buildChanRtp(RefObject* context)
	{
	    Message* m = new Message("chan.rtp");
	    m->userData(context ? context : this);
	    return m;
	}
    virtual Message* buildChanRtp(SDPMedia* media, const char* addr, bool start, RefObject* context)
	{
	    Message* m = SDPSession::buildChanRtp(media,addr,start,context);
	    if (m)
		m->addParam("mgcp_allowed",String::boolText(false));
	    return m;
	}
private:
    void waitNotChanging();
    MGCPMessage* message(const char* cmd);
    bool sendAsync(MGCPMessage* mm);
    RefPointer<MGCPMessage> sendSync(MGCPMessage* mm);
    bool sendRequest(const char* sigReq, const char* reqEvt = 0, const char* digitMap = 0);
    bool enqueueEvent(SignallingCircuitEvent::Type type, const char* name, const char* dtmf = 0);
    void cleanupRtp();
    bool createRtp();
    bool setupConn(const char* mode = 0);
    String m_epId;
    Status m_statusReq;
    String m_notify;
    String m_specialMode;
    bool m_changing;
    // Gateway endpoint bearer information
    String m_gwFormat;
    bool m_gwFormatChanged;
    // Connection data
    String m_connId;
    String m_callId;
    // Local RTP related data
    RefPointer<DataSource> m_source;
    RefPointer<DataConsumer> m_consumer;
    String m_localRawSdp;
    bool m_localRtpChanged;
    // Remote (MGCP GW side) RTP data
    bool m_needClear;
    String m_remoteRawSdp;
    // Synchronous transaction data
    MGCPTransaction* m_tr;
    RefPointer<MGCPMessage> m_msg;
};

class RtpHandler : public MessageHandler
{
public:
    RtpHandler(unsigned int prio) : MessageHandler("chan.rtp",prio) { }
    virtual bool received(Message &msg);
};

class SdpHandler : public MessageHandler
{
public:
    SdpHandler(unsigned int prio) : MessageHandler("chan.sdp",prio) { }
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
    inline SDPParser& parser()
	{ return m_parser; }
private:
    SDPParser m_parser;
};

YSIGFACTORY2(MGCPSpan);

static YMGCPEngine* s_engine = 0;
static MGCPEndpoint* s_endpoint = 0;
static String s_defaultEp;

static MGCPPlugin splugin;
static ObjList s_wrappers;
static ObjList s_spans;
static Mutex s_mutex(false,"MGCP-CA");

// Yate Payloads for the AV profile
static TokenDict s_dict_payloads[] = {
    { "mulaw",         0 },
    { "alaw",          8 },
    { "gsm",           3 },
    { "lpc10",         7 },
    { "slin",         11 },
    { "g726",          2 },
    { "g722",          9 },
    { "g723",          4 },
    { "g728",         15 },
    { "g729",         18 },
    { "ilbc",         98 },
    { "ilbc20",       98 },
    { "ilbc30",       98 },
    { "amr",          96 },
    { "amr-o",        96 },
    { "amr/16000",    99 },
    { "amr-o/16000",  99 },
    { "speex",       102 },
    { "speex/16000", 103 },
    { "speex/32000", 104 },
    { "h261",         31 },
    { "h263",         34 },
    { "mpv",          32 },
    {      0,          0 },
};

// Media gateway bearer information (mapped from s_dict_payloads)
static TokenDict s_dict_gwbearerinfo[] = {
    { "e:mu",          0 },
    { "e:A",           8 },
    {      0,          0 },
};

// Copy one parameter (if present) with new name
static bool copyRename(NamedList& dest, const char* dname, const NamedList& src, const String& sname)
{
    if (!sname)
	return false;
    const NamedString* value = src.getParam(sname);
    if (!value)
	return false;
    dest.addParam(dname,*value);
    return true;
}

// Increment the number at the end of a name by an offset
static bool tailIncrement(String& name, unsigned int offs)
{
    Regexp r("\\([0-9]\\+\\)@");
    if (name.matches(r)) {
	int pos = name.matchOffset(1);
	unsigned int len = name.matchLength(1);
	String num(offs + name.matchString(1).toInteger(0,10));
	while (num.length() < len)
	    num = "0" + num;
	name = name.substr(0,pos) + num + name.substr(pos + len);
	return true;
    }
    return false;
}


YMGCPEngine::~YMGCPEngine()
{
    s_engine = 0;
    s_endpoint = 0;
}

// Process all events of this engine, forward them to wrappers if found
bool YMGCPEngine::processEvent(MGCPTransaction* trans, MGCPMessage* msg, void* data)
{
    MGCPWrapper* wrap = YOBJECT(MGCPWrapper,static_cast<GenObject*>(data));
    MGCPSpan* span = YOBJECT(MGCPSpan,static_cast<GenObject*>(data));
    MGCPCircuit* circ = YOBJECT(MGCPCircuit,static_cast<GenObject*>(data));
    Debug(this,DebugAll,"YMGCPEngine::processEvent(%p,%p,%p) wrap=%p span=%p circ=%p [%p]",
	trans,msg,data,wrap,span,circ,this);
    if (!trans)
	return false;
    if (wrap)
	return wrap->processEvent(trans,msg);
    if (span)
	return span->processEvent(trans,msg);
    if (circ)
	return circ->processEvent(trans,msg);
    if (!msg)
	return false;
    if (!data && !trans->outgoing() && msg->isCommand()) {
	while (msg->name() == "NTFY") {
	    const String* rqId = msg->params.getParam("x");
	    const String* event = msg->params.getParam("o");
	    if (null(rqId))
		trans->setResponse(538,"Missing request-id");
	    else if (null(event))
		trans->setResponse(538,"Missing observed events");
	    else if (*rqId == "0") {
		// persistent notification
		Debug(this,DebugInfo,"NTFY '%s' from '%s'",
		    c_str(event),
		    msg->endpointId().c_str());
		MGCPEndpointId id(msg->endpointId());
		bool ok = false;
		if (id.valid()) {
		    s_mutex.lock();
		    ListIterator iter(s_spans);
		    while ((span = static_cast<MGCPSpan*>(iter.get()))) {
			if (span->matchEndpoint(id)) {
			    s_mutex.unlock();
			    ok = span->processNotify(trans,msg,*event,*rqId) || ok;
			    s_mutex.lock();
			}
		    }
		    s_mutex.unlock();
		}
		if (ok) {
		    trans->setResponse(200,"OK");
		    return true;
		}
		break;
	    }
	    else {
		bool ok = false;
		wrap = MGCPWrapper::findNotify(*rqId);
		if (wrap)
		    ok = wrap->processNotify(trans,msg,*event);
		else {
		    span = MGCPSpan::findNotify(*rqId);
		    if (span)
			ok = span->processNotify(trans,msg,*event,*rqId);
		    else {
			trans->setResponse(538,"Unknown request-id");
			return true;
		    }
		}
		if (ok)
		    trans->setResponse(200,"OK");
		else
		    trans->setResponse(539,"Unsupported parameter");
	    }
	    return true;
	}
	if (msg->name() == "RSIP") {
	    const String* method = msg->params.getParam("rm");
	    Debug(this,DebugInfo,"RSIP '%s' from '%s'",
		c_str(method),msg->endpointId().c_str());
	    MGCPEndpointId id(msg->endpointId());
	    bool ok = false;
	    if (id.valid()) {
		if (!method)
		    method = &String::empty();
		s_mutex.lock();
		ListIterator iter(s_spans);
		while ((span = static_cast<MGCPSpan*>(iter.get()))) {
		    if (span->matchEndpoint(id)) {
			s_mutex.unlock();
			ok = span->processRestart(trans,msg,*method) || ok;
			s_mutex.lock();
		    }
		}
		s_mutex.unlock();
	    }
	    if (ok) {
		trans->setResponse(200);
		return true;
	    }
	}
	else if (msg->name() == "DLCX") {
	    const String* error = msg->params.getParam("e");
	    Debug(this,DebugInfo,"DLCX '%s' from '%s'",
		c_str(error),msg->endpointId().c_str());
	    MGCPEndpointId id(msg->endpointId());
	    if (id.valid()) {
		if (!error)
		    error = &String::empty();
		s_mutex.lock();
		ListIterator iter(s_spans);
		while ((span = static_cast<MGCPSpan*>(iter.get()))) {
		    if (span->matchEndpoint(id)) {
			s_mutex.unlock();
			if (span->processDelete(trans,msg,*error)) {
			    trans->setResponse(200);
			    return true;
			}
			s_mutex.lock();
		    }
		}
		s_mutex.unlock();
	    }
	}
	Debug(this,DebugMild,"Unhandled '%s' from '%s'",
	    msg->name().c_str(),msg->endpointId().c_str());
    }
    return false;
}


MGCPWrapper::MGCPWrapper(CallEndpoint* conn, const char* media, Message& msg, const char* epId)
    : DataEndpoint(conn,media),
      m_tr(0), m_connEp(epId)
{
    Debug(&splugin,DebugAll,"MGCPWrapper::MGCPWrapper(%p,'%s','%s') [%p]",
	conn,media,epId,this);
    m_id = "mgcp/";
    m_id << (unsigned int)::random();
    if (conn)
	m_master = conn->id();
    m_master = msg.getValue("id",(conn ? conn->id().c_str() : (const char*)0));
    m_audio = (name() == "audio");
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

// Process incoming events for this wrapper
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
	    if (event && processNotify(tr,mm,*event)) {
		tr->setResponse(200);
		return true;
	    }
	}
    }
    return false;
}

// Process incoming notify events for this wrapper
bool MGCPWrapper::processNotify(MGCPTransaction* tr, MGCPMessage* mm, const String& event)
{
    if (event.null())
	return false;
    else if (event.find(',') >= 0) {
	// Multiple events
	ObjList* l = event.split(',',false);
	bool ok = false;
	for (ObjList* p = l->skipNull(); p; p = p->skipNext())
	    ok = processNotify(tr,mm,p->get()->toString()) || ok;
	delete l;
	return ok;
    }
    else {
	Debug(&splugin,DebugStub,"MGCPWrapper::processNotify(%p,%p,'%s') [%p]",
	    tr,mm,event.c_str(),this);
	return false;
    }
    return true;
}

// Process local chan.rtp messages for this wrapper
bool MGCPWrapper::rtpMessage(Message& msg)
{
    if (!s_endpoint)
	return false;
    const char* cmd = "MDCX";
    bool extend = false;
    bool fini = msg.getBoolValue("terminate");
    if (fini) {
	if (m_connId.null())
	    return true;
	cmd = "DLCX";
    }
    else if (m_connId.null())
	cmd = "CRCX";
    MGCPEpInfo* ep = s_endpoint->find(m_connEp);
    if (!ep)
	return false;
    RefPointer<MGCPMessage> mm = new MGCPMessage(s_engine,cmd,ep->toString());
    addParams(mm);
    String dir = msg.getValue("direction",m_connId.null() ? "bidir" : "");
    if (dir == "bidir")
	dir = "sendrecv";
    else if (dir == "send")
	dir = "sendonly";
    else if (dir == "receive")
	dir = "recvonly";
    else
	dir.clear();
    if (dir)
	mm->params.addParam("M",dir);
    if (extend && !fini) {
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
    }
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

// Delete remote connection if any
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

// Populate a MGCP message with basic identification parameters
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

// Send a MGCP message, wait for an answer and return it
RefPointer<MGCPMessage> MGCPWrapper::sendSync(MGCPMessage* mm, const SocketAddr& address)
{
    while (m_msg) {
	if (Thread::check(false))
	    return 0;
	Thread::idle();
    }
    MGCPTransaction* tr = s_engine->sendCommand(mm,address);
    tr->userData(static_cast<GenObject*>(this));
    m_tr = tr;
    while (m_tr == tr)
	Thread::idle();
    RefPointer<MGCPMessage> tmp = m_msg;
    m_msg = 0;
    if (tmp)
	Debug(&splugin,DebugNote,"MGCPWrapper::sendSync() returning %d '%s' [%p]",
	    tmp->code(),tmp->comment().c_str(),this);
    else
	Debug(&splugin,DebugMild,"MGCPWrapper::sendSync() returning NULL [%p]",this);
    return tmp;
}

// Find a wrapper by Call Endpoint and media type
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

// Find a wrapper by its local ID
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

// Find a wrapper by its Notify-ID
MGCPWrapper* MGCPWrapper::findNotify(const String& id)
{
    if (id.null())
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_wrappers;
    for (; l; l=l->next()) {
	MGCPWrapper* w = static_cast<MGCPWrapper *>(l->get());
	if (w && (w->ntfyId() == id))
	    return w;
    }
    return 0;
}

// Send a DTMF as a sequence of package D events
bool MGCPWrapper::sendDTMF(const String& tones)
{
    DDebug(&splugin,DebugInfo,"MGCPWrapper::sendDTMF('%s') [%p]",
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
    DDebug(&splugin,DebugInfo,"MGCPWrapper::gotDTMF('%c') [%p]",tone,this);
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

// Perform remote bridging if two MGCP endpoints are connected locally
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
    Debug(&splugin,DebugNote,"Native bridging to %p [%p]",other,this);
    MGCPEpInfo* ep = s_endpoint->find(m_connEp);
    if (!ep)
	return false;
    MGCPMessage* mm = new MGCPMessage(s_engine,"MDCX",ep->toString());
    addParams(mm);
    mm->params.setParam("Z2",other->connId());
    return s_engine->sendCommand(mm,ep->address) != 0;
}


// Called by the factory to create MGCP spans
SignallingComponent* MGCPSpan::create(const String& type, const NamedList& name)
{
    if (type != "SignallingCircuitSpan")
	return 0;
    const String* spanName = name.getParam("voice");
    if (!spanName)
	spanName = &name;
    if (null(spanName) || !s_endpoint)
	return 0;
    MGCPEpInfo* ep = s_endpoint->findAlias(*spanName);
    if (!ep) {
	DDebug(&splugin,DebugAll,"No endpoint info for span '%s'",spanName->c_str());
	return 0;
    }
    MGCPSpan* span = new MGCPSpan(name,spanName->safe("MGCPSpan"),*ep);
    if (span->init(name))
	return span;
    TelEngine::destruct(span);
    return 0;
}

// Find a span by its Notify-ID
MGCPSpan* MGCPSpan::findNotify(const String& id)
{
    if (id.null())
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_spans;
    for (; l; l=l->next()) {
	MGCPSpan* s = static_cast<MGCPSpan*>(l->get());
	if (s && s->ownsId(id))
	    return s;
    }
    return 0;
}

MGCPSpan::MGCPSpan(const NamedList& params, const char* name, const MGCPEpInfo& ep)
    : SignallingCircuitSpan(params.getValue("debugname",name),
       static_cast<SignallingCircuitGroup*>(params.getObject("SignallingCircuitGroup"))),
      m_circuits(0), m_count(0), m_epId(ep), m_operational(false),
      m_rtpForward(false), m_sdpForward(false), m_fxo(false), m_fxs(false)
{
    Debug(&splugin,DebugAll,"MGCPSpan::MGCPSpan(%p,'%s') [%p]",
	&params,name,this);
    u_int32_t ntfy = (u_int32_t)::random();
    m_notify.hexify(&ntfy,sizeof(ntfy));
    const AnalogLineGroup* analog = YOBJECT(AnalogLineGroup,group());
    if (analog) {
	switch (analog->type()) {
	    case AnalogLine::FXO:
		m_fxo = true;
		break;
	    case AnalogLine::FXS:
		m_fxs = true;
		break;
	    default:
		break;
	}
    }
    s_mutex.lock();
    s_spans.append(this);
    s_mutex.unlock();
}

MGCPSpan::~MGCPSpan()
{
    Debug(&splugin,DebugAll,"MGCPSpan::~MGCPSpan() '%s' [%p]",
	id().c_str(),this);
    s_mutex.lock();
    s_spans.remove(this,false);
    s_mutex.unlock();
    clearCircuits();
}

// Clear all the circuits in the span
void MGCPSpan::clearCircuits()
{
    MGCPCircuit** circuits = m_circuits;
    m_circuits = 0;
    if (!circuits)
	return;
    for (unsigned int i = 0; i < m_count; i++)
	TelEngine::destruct(circuits[i]);
    delete[] circuits;
}

// Initialize the circuits span
bool MGCPSpan::init(const NamedList& params)
{
    clearCircuits();
    const String* sect = params.getParam("voice");
    if (!sect)
	sect = params.getParam("basename");
    if (!sect)
	sect = &params;
    int cicStart = params.getIntValue("start");
    if ((cicStart < 0) || !sect)
	return false;
    Configuration cfg(Engine::configFile("mgcpca"));
    const NamedList* config = cfg.getSection("gw " + *sect);
    if (!config) {
	Debug(m_group,DebugWarn,"MGCPSpan('%s'). Failed to find config section [%p]",
	    id().safe(),this);
	return false;
    }
    SignallingCircuitRange range(config->getValue("voicechans"));
    m_count = 1;
    if (range.count())
	m_count = range[range.count()-1];
    m_count = config->getIntValue("chans",m_count);
    cicStart += config->getIntValue("offset");

    if (!m_count)
	return false;
    m_increment = m_count;
    // assume 23 circuits means T1, 30 or 31 circuits means E1
    switch (m_increment) {
	case 23:
	    m_increment = 24;
	    break;
	case 30:
	case 31:
	    m_increment = 32;
	    break;
    }
    m_increment = config->getIntValue("increment",m_increment);
    m_rtpForward = config->getBoolValue("forward_rtp",!(m_fxo || m_fxs));
    m_sdpForward = config->getBoolValue("forward_sdp",false);
    bool clear = config->getBoolValue("clearconn",false);
    m_circuits = new MGCPCircuit*[m_count];
    unsigned int i;
    for (i = 0; i < m_count; i++)
	m_circuits[i] = 0;
    bool ok = true;
    for (i = 0; i < m_count; i++) {
	if (range.count() && !range.find(i+1))
	    continue;
	String name = epId().id();
	if (!tailIncrement(name,i)) {
	    Debug(m_group,DebugWarn,"MGCPSpan('%s'). Failed to increment name by %u. Rollback [%p]",
		id().safe(),i,this);
	    clearCircuits();
	    ok = false;
	    break;
	}
	MGCPCircuit* circuit = new MGCPCircuit(cicStart + i,this,name);
	m_circuits[i] = circuit;
	if (!m_group->insert(circuit)) {
	    Debug(m_group,DebugWarn,"MGCPSpan('%s'). Failed to create/insert circuit %u. Rollback [%p]",
		id().safe(),cicStart + i,this);
	    clearCircuits();
	    ok = false;
	    break;
	}
	circuit->ref();
	if (clear)
	    circuit->needClear();
    }

    if (ok) {
	m_version = config->getValue("version");
	const char* addr = config->getValue("address");
	if (addr) {
	    MGCPEpInfo* ep = s_endpoint->find(epId().id());
	    if (ep) {
		SocketAddr a(ep->address);
		a.host(addr);
		operational(a);
	    }
	}
    }

    return ok;
}

// Set the operational state
void MGCPSpan::operational(bool active)
{
    if (active == m_operational)
	return;
    Debug(&splugin,DebugCall,"MGCPSpan '%s' is%s operational [%p]",
	id().c_str(),(active ? "" : " not"),this);
    m_operational = active;
    if (!m_circuits)
	return;
    for (unsigned int i = 0; i < m_count; i++) {
	MGCPCircuit* circuit = m_circuits[i];
	if (circuit)
	    circuit->status((active ? SignallingCircuit::Idle : SignallingCircuit::Missing),true);
    }
}

// Set the operational state and copy GW address
void MGCPSpan::operational(const SocketAddr& address)
{
    if (address.valid() && address.host())
	m_address = address.host();
    MGCPEpInfo* ep = s_endpoint->find(epId().id());
    if (ep && !(m_operational && ep->address.valid()))
	ep->address = address;
    operational(true);
}

// Get a configuration or operational boolean parameter by name
bool MGCPSpan::getBoolParam(const String& param, bool defValue) const
{
    if (param == "operational")
	return operational();
    if (param == "rtp_forward")
	return m_rtpForward;
    if (param == "sdp_forward")
	return m_sdpForward;
    return defValue;
}

// Check if this span matches an endpoint ID
bool MGCPSpan::matchEndpoint(const MGCPEndpointId& ep)
{
    if (ep.port() && (ep.port() != m_epId.port()))
	return false;
    if (ep.host() |= m_epId.host())
	return false;
    if (ep.user() &= m_epId.user())
	return true;
    if (ep.user() == "*")
	return true;
    if (findCircuit(ep.id()))
	return true;
    // check for wildcards like */*/*
    static Regexp s_termsAll("^\\*[/*]\\+\\*$");
    if (s_termsAll.matches(ep.user()))
	return true;
    String tmp = ep.user();
    // check for prefix/*/*
    static Regexp s_finalAll("^\\([^*]\\+/\\)[/*]\\+$");
    if (tmp.matches(s_finalAll) && m_epId.user().startsWith(tmp.matchString(1),false,true))
	return true;
    // check for prefix[min-max]
    static Regexp s_finalRange("^\\(.*\\)\\[\\([0-9]\\+\\)-\\([0-9]\\+\\)\\]$");
    if (!(tmp.matches(s_finalRange) && m_epId.user().startsWith(tmp.matchString(1),false,true)))
	return false;
    int idx = m_epId.user().substr(tmp.matchLength(1)).toInteger(-1,10);
    if (idx < 0)
	return false;
    return (tmp.matchString(2).toInteger(idx+1,10) <= idx) && (idx <= tmp.matchString(3).toInteger(-1,10));
}

// Check if a request Id is for this span or one of its circuits
bool MGCPSpan::ownsId(const String& rqId) const
{
    if (ntfyId() == rqId)
	return true;
    for (unsigned int i = 0; i < m_count; i++) {
	MGCPCircuit* circuit = m_circuits[i];
	if (circuit && (circuit->ntfyId() == rqId))
	    return true;
    }
    return false;
}

// Get the circuit associated to a specific endpoint and request Id
MGCPCircuit* MGCPSpan::findCircuit(const String& epId, const String& rqId) const
{
    if (!(m_count && m_circuits))
	return 0;
    if (epId.find('@') <= 0)
	return 0;
    bool localId = (rqId != "0") && !rqId.null();
    String id = epId;
    if (id.rfind(':') < 0)
	id << ":" << m_epId.port();
    for (unsigned int i = 0; i < m_count; i++) {
	MGCPCircuit* circuit = m_circuits[i];
	if (!circuit)
	    continue;
	if (localId) {
	    if (circuit->ntfyId() != rqId)
		continue;
	}
	else {
	    if (circuit->epId() != id)
		continue;
	}
	return circuit;
    }
    return 0;
}

// Process incoming events for this span
bool MGCPSpan::processEvent(MGCPTransaction* tr, MGCPMessage* mm)
{
    DDebug(&splugin,DebugInfo,"MGCPSpan::processEvent(%p,%p) '%s' [%p]",
	tr,mm,mm->name().c_str(),this);

    if (mm->name() == "NTFY") {
	const String* rqId = mm->params.getParam("x");
	if (null(rqId))
	    return false;
	const String* event = mm->params.getParam("o");
	if (c_str(event) && processNotify(tr,mm,*event,*rqId)) {
	    tr->setResponse(200);
	    return true;
	}
    }

    return false;
}

// Process incoming notify events for this span
bool MGCPSpan::processNotify(MGCPTransaction* tr, MGCPMessage* mm, const String& event, const String& requestId)
{
    DDebug(&splugin,DebugInfo,"MGCPSpan::processNotify(%p,%p,'%s','%s') [%p]",
	tr,mm,event.c_str(),requestId.c_str(),this);

    if (event.null())
	return false;
    else if (event.find(',') >= 0) {
	// Multiple events
	ObjList* l = event.split(',',false);
	bool ok = false;
	for (ObjList* p = l->skipNull(); p; p = p->skipNext())
	    ok = processNotify(tr,mm,p->get()->toString(),requestId) || ok;
	delete l;
	return ok;
    }
    else {
	MGCPCircuit* circuit = findCircuit(mm->endpointId(),requestId);
	if (!circuit)
	    return false;
	int pos = event.find('/');
	if (pos <= 0)
	    return false;
	return circuit->processNotify(event.substr(0,pos).trimBlanks().toUpper(),
	    event.substr(pos+1).trimBlanks(),event);
    }
}

// Process gateway restart events for this span
bool MGCPSpan::processRestart(MGCPTransaction* tr, MGCPMessage* mm, const String& method)
{
    DDebug(&splugin,DebugInfo,"MGCPSpan::processRestart(%p,%p,'%s') [%p]",
	tr,mm,method.c_str(),this);
    if ((method &= "X-KeepAlive") || (method &= "disconnected") || (method &= "restart")) {
	if (m_version.null()) {
	    m_version = mm->version();
	    Debug(&splugin,DebugNote,"MGCPSpan '%s' using version '%s' [%p]",
		id().c_str(),m_version.c_str(),this);
	}
	operational(tr->addr());
    }
    else if (method &= "graceful")
	operational(false);
    else if (method &= "cancel-graceful")
	operational(true);
    else {
	operational(false);
    }
    return true;
}

// Process gateway-initiated connection deletion
bool MGCPSpan::processDelete(MGCPTransaction* tr, MGCPMessage* mm, const String& error)
{
    DDebug(&splugin,DebugInfo,"MGCPSpan::processDelete(%p,%p,'%s') [%p]",
	tr,mm,error.c_str(),this);
    MGCPCircuit* circuit = findCircuit(mm->endpointId());
    if (!circuit)
	return false;
    circuit->processDelete(mm,error);
    return true;
}


MGCPCircuit::MGCPCircuit(unsigned int code, MGCPSpan* span, const char* id)
    : SignallingCircuit(RTP,code,Missing,span->group(),span),
      SDPSession(&splugin.parser()),
      m_epId(id), m_statusReq(Missing), m_changing(false), m_gwFormatChanged(false),
      m_localRtpChanged(false), m_needClear(false), m_tr(0)
{
    Debug(&splugin,DebugAll,"MGCPCircuit::MGCPCircuit(%u,%p,'%s') [%p]",
	code,span,id,this);
    u_int32_t cic = code;
    m_notify.hexify(&cic,sizeof(cic));
    m_callId.hexify(this,sizeof(this));
    m_callId += m_notify;
    m_notify = span->ntfyId() + m_notify;
}

MGCPCircuit::~MGCPCircuit()
{
    Debug(&splugin,DebugAll,"MGCPCircuit::~MGCPCircuit() %u [%p]",
	code(),this);
    s_mutex.lock();
    if (m_tr) {
	m_tr->userData(0);
	m_tr = 0;
    }
    s_mutex.unlock();
    m_msg = 0;
    cleanupRtp();
    clearConn();
}

void* MGCPCircuit::getObject(const String& name) const
{
    if (connected()) {
	if (name == "DataSource")
	    return m_source;
	if (name == "DataConsumer")
	    return m_consumer;
    }
    if (name == "MGCPCircuit")
	return (void*)this;
    return SignallingCircuit::getObject(name);
}

// Clean up any RTP we may still hold
void MGCPCircuit::cleanupRtp()
{
    resetSdp();
    m_localRawSdp.clear();
    m_localRtpChanged = false;
    m_remoteRawSdp.clear();
    m_source = 0;
    m_consumer = 0;
}

// Create a local RTP instance
bool MGCPCircuit::createRtp()
{
    if (hasRtp())
	return true;
    cleanupRtp();
    resetSdp();
    updateSDP(NamedList::empty());
    RefPointer<DataEndpoint> de = new DataEndpoint;
    bool ok = dispatchRtp(mySpan()->address(),false,de);
    if (ok) {
	m_source = de->getSource();
	m_consumer = de->getConsumer();
	DDebug(&splugin,DebugAll,"MGCPCircuit::createRtp() src=%p cons=%p [%p]",
	    (void*)m_source,(void*)m_consumer,this);
    }
    else {
	Debug(&splugin,DebugWarn,"MGCPCircuit::createRtp() failed [%p]",this);
	cleanupRtp();
    }
    TelEngine::destruct(de);
    return ok;
}

// Create or update remote connection
bool MGCPCircuit::setupConn(const char* mode)
{
    RefPointer<MGCPMessage> mm = message(m_connId.null() ? "CRCX" : "MDCX");
    mm->params.addParam("C",m_callId);
    if (m_connId)
	mm->params.addParam("I",m_connId);
    if (m_gwFormatChanged && m_gwFormat)
	mm->params.addParam("B",m_gwFormat);
    if (mode)
	mm->params.addParam("M",mode);
    else if (m_localRawSdp) {
	mm->params.addParam("M","sendrecv");
	mm->sdp.append(new MimeSdpBody("application/sdp",
	    m_localRawSdp.safe(),m_localRawSdp.length()));
    }
    else {
	MimeSdpBody* sdp = createSDP(getRtpAddr());
	if (sdp) {
	    mm->params.addParam("M","sendrecv");
	    mm->sdp.append(sdp);
	}
    }
    mm = sendSync(mm);
    if (!mm)
	return false;
    m_gwFormatChanged = false;
    if (m_connId.null())
	m_connId = mm->params.getParam("i");
    if (m_connId.null()) {
	m_needClear = true;
	return false;
    }
    m_localRtpChanged = false;
    MimeSdpBody* sdp = static_cast<MimeSdpBody*>(mm->sdp[0]);
    if (sdp) {
	String oldIp = m_rtpAddr;
	bool mediaChanged = setMedia(splugin.parser().parse(*sdp,m_rtpAddr,m_rtpMedia));
	const DataBlock& raw = sdp->getBody();
	m_remoteRawSdp.assign((const char*)raw.data(),raw.length());
	// Disconnect if media changed
	if (mediaChanged && oldIp && oldIp != m_rtpAddr)
	    enqueueEvent(SignallingCircuitEvent::Disconnected,"Disconnected");
	return true;
    }
    return true;
}

// Delete remote connection if any
void MGCPCircuit::clearConn(bool force)
{
    if (m_connId.null() && !force)
	return;
    MGCPMessage* mm = message("DLCX");
    if (m_connId) {
	force = false;
	mm->params.addParam("I",m_connId);
    }
    if (!force)
	mm->params.addParam("C",m_callId);
    else {
	m_gwFormat.clear();
	m_gwFormatChanged = false;
    }
    m_connId.clear();
    m_specialMode.clear();
    resetSdp();
    m_remoteRawSdp.clear();
    m_localRtpChanged = false;
    sendAsync(mm);
}

// Wait for changing flag to be false
void MGCPCircuit::waitNotChanging()
{
    while (true) {
	Lock lock(s_mutex);
	if (!m_changing) {
	    m_changing = true;
	    break;
	}
	lock.drop();
	Thread::yield(true);
    }
}

// Build a MGCP message
MGCPMessage* MGCPCircuit::message(const char* cmd)
{
    return new MGCPMessage(s_engine,cmd,epId(),mySpan()->version());
}

// Send a MGCP message asynchronously
bool MGCPCircuit::sendAsync(MGCPMessage* mm)
{
    if (!mm)
	return false;
    MGCPEpInfo* ep = s_endpoint->find(mySpan()->epId().id());
    if (ep && s_engine->sendCommand(mm,ep->address))
	return true;
    TelEngine::destruct(mm);
    return false;
}

// Send a MGCP message, wait for an answer and return it
RefPointer<MGCPMessage> MGCPCircuit::sendSync(MGCPMessage* mm)
{
    if (!mm)
	return 0;
    MGCPEpInfo* ep = s_endpoint->find(mySpan()->epId().id());
    if (!ep) {
	TelEngine::destruct(mm);
	return 0;
    }
    while (m_msg) {
	if (Thread::check(false))
	    return 0;
	Thread::idle();
    }
    MGCPTransaction* tr = s_engine->sendCommand(mm,ep->address);
    tr->userData(static_cast<GenObject*>(this));
    m_tr = tr;
    while (m_tr == tr)
	Thread::idle();
    RefPointer<MGCPMessage> tmp = m_msg;
    m_msg = 0;
    if (tmp)
	Debug(&splugin,DebugNote,"MGCPCircuit::sendSync() returning %d '%s' [%p]",
	    tmp->code(),tmp->comment().c_str(),this);
    else
	Debug(&splugin,DebugMild,"MGCPCircuit::sendSync() returning NULL [%p]",this);
    return tmp;
}

// Send asynchronously a notification request
bool MGCPCircuit::sendRequest(const char* sigReq, const char* reqEvt, const char* digitMap)
{
    MGCPMessage* mm = message("RQNT");
    mm->params.addParam("X",m_notify);
    if (sigReq)
	mm->params.addParam("S",sigReq);
    if (reqEvt)
	mm->params.addParam("R",reqEvt);
    if (digitMap)
	mm->params.addParam("D",digitMap);
    return sendAsync(mm);
}

// Circuit status change request
bool MGCPCircuit::status(Status newStat, bool sync)
{
    Debug(&splugin,DebugInfo,"MGCPCircuit::status(%s,%s) %u [%p]",
	lookupStatus(newStat),String::boolText(sync),code(),this);
    waitNotChanging();
    // Don't notify local rtp if we already have it (addr/port/sdp) and didn't changed
    // Accept only synchronous connect requests
    bool allowRtpChange = false;
    if (newStat == Connected) {
	if (!sync) {
	    m_changing = false;
	    return false;
	}
	allowRtpChange = SignallingCircuit::status() == Connected &&
	    hasLocalRtp() && m_localRtpChanged;
	if (SignallingCircuit::status() != Connected && !(fxs() || fxo()))
	    sendRequest(0,"D/[0-9#*](N)");
    }
    if (!allowRtpChange && (newStat == m_statusReq) &&
	((SignallingCircuit::status() == newStat) || !sync)) {
	m_changing = false;
	return true;
    }
    if (!mySpan()->operational()) {
	if (newStat >= Idle) {
	    m_changing = false;
	    return false;
	}
    }
    bool special = false;
    m_statusReq = newStat;
    switch (newStat) {
	case Special:
	    if (m_specialMode.null())
		return false;
	    if ((m_specialMode == "loopback" || m_specialMode == "conttest") &&
		setupConn(m_specialMode))
		break;
	    if (m_rtpForward)
		return false;
	    special = true;
	    // fall through, we'll check it later after connecting
	case Connected:
	    // Create local rtp if we don't have one
	    // Start it if we don't forward the rtp
	    if (m_rtpForward || hasLocalRtp() || createRtp()) {
		if (setupConn()) {
		    if (m_rtpForward) {
			m_source = 0;
			m_consumer = 0;
			break;
		    }
		    if (startRtp())
			break;
		    clearConn();
		}
		cleanupRtp();
	    }
	    m_statusReq = SignallingCircuit::status();
	    m_changing = false;
	    return false;
	case Reserved:
	    break;
	case Idle:
	    if (m_needClear) {
		m_needClear = false;
		clearConn(true);
	    }
	default:
	    cleanupRtp();
	    clearConn();
    }
    DDebug(&splugin,DebugInfo,"MGCPCircuit new status '%s' on %u [%p]",
	lookupStatus(newStat),code(),this);
    bool ok = SignallingCircuit::status(newStat,sync);
    m_changing = false;
    if (ok && special) {
	Message m("circuit.special");
	m.userData(this);
	if (group())
	    m.addParam("group",group()->toString());
	if (span())
	    m.addParam("span",span()->toString());
	m.addParam("mode",m_specialMode);
	ok = Engine::dispatch(m);
	if (!ok)
	    status(Idle,false);
    }
    return ok;
}

// Change the format of this circuit
bool MGCPCircuit::updateFormat(const char* format, int direction)
{
    if (!format)
	return false;
    Debug(&splugin,DebugInfo,"MGCPCircuit::updateFormat('%s',%d) %u [%p]",
	format,direction,code(),this);
    int fmt = lookup(format,s_dict_payloads,-1);
    const char* gwFmt = lookup(fmt,s_dict_gwbearerinfo);
    if (!gwFmt)
	return false;
    waitNotChanging();
    if (m_gwFormat != gwFmt) {
	m_gwFormat = gwFmt;
	m_gwFormatChanged = true;
    }
    m_changing = false;
    return true;
}

bool MGCPCircuit::setParam(const String& param, const String& value)
{
    if (m_changing)
	return false;
    Lock lock(s_mutex);
    if (m_changing)
	return false;
    bool rtpChanged = false;
    if (param == "sdp_raw") {
	rtpChanged = m_localRawSdp != value;
	m_localRawSdp = value;
    }
    else if (param == "rtp_forward") {
	bool fwd = value.toBoolean();
	rtpChanged = m_rtpForward != fwd;
	m_rtpForward = fwd;
    }
    else if (param == "special_mode")
	m_specialMode = value;
    else
	return false;
    m_localRtpChanged = m_localRtpChanged || rtpChanged;
    lock.drop();
    DDebug(&splugin,DebugAll,"MGCPCircuit::setParam(%s,%s) %u [%p]",
	param.c_str(),value.c_str(),code(),this);
    return true;
}

bool MGCPCircuit::getParam(const String& param, String& value) const
{
    if (m_changing)
	return false;
    Lock lock(s_mutex);
    if (m_changing)
	return false;
    if (param == "rtp_addr") {
	value = m_rtpAddr;
	return true;
    }
    else if (param == "sdp_raw") {
	value = m_remoteRawSdp;
	return true;
    }
    else if (param == "special_mode") {
	value = m_specialMode;
	return true;
    }
    return false;
}

bool MGCPCircuit::getBoolParam(const String& param, bool defValue) const
{
    return mySpan()->getBoolParam(param,defValue);
}

// Set circuit data from a list of parameters
bool MGCPCircuit::setParams(const NamedList& params)
{
    if (params == "rtp") {
	waitNotChanging();
	DDebug(&splugin,DebugAll,"MGCPCircuit::setParams(rtp) %u [%p]",code(),this);
	String* raw = params.getParam("sdp_raw");
	if (raw && m_localRawSdp != *raw) {
	    m_localRawSdp = *raw;
	    m_localRtpChanged = true;
	    m_rtpForward = true;
	}
	if (!m_localRawSdp) {
	    m_localRtpChanged = updateRtpSDP(params) || localRtpChanged() || m_localRtpChanged;
	    setLocalRtpChanged();
	    if (m_localRtpChanged)
		m_rtpForward = true;
	}
	m_changing = false;
	return true;
    }
    return SignallingCircuit::setParams(params);
}

bool MGCPCircuit::getParams(NamedList& params, const String& category)
{
    if (category != "rtp")
	return false;
    waitNotChanging();
    addRtpParams(params,String::empty(),0,true);
    m_changing = false;
    return true;
}

// Send out an event on this circuit
bool MGCPCircuit::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    DDebug(&splugin,DebugAll,"MGCPCircuit::sendEvent(%u,%p) %u [%p]",
	type,params,code(),this);
    switch (type) {
	case SignallingCircuitEvent::Connect:
	    if (params)
		setParams(*params);
	    return status(Connected,!params || params->getBoolValue("sync",true));
	case SignallingCircuitEvent::RingBegin:
	    return fxs() && sendRequest("L/rg");
//	case SignallingCircuitEvent::RingEnd:
//	    return fxs() && sendRequest("L/rg(-)");
	case SignallingCircuitEvent::Polarity:
	    return fxs() && sendRequest("L/lsa");
	case SignallingCircuitEvent::OffHook:
	    return fxo() && sendRequest("L/hd","L/lsa(N)");
	case SignallingCircuitEvent::OnHook:
	    return fxo() && sendRequest("L/hu");
	case SignallingCircuitEvent::Flash:
	    return fxo() && sendRequest("L/hf");
	case SignallingCircuitEvent::Dtmf:
	    if (params) {
		const String* tone = params->getParam("tone");
		if (!tone)
		    tone = params;
		if (!null(tone))
		    return sendRequest("D/" + *tone);
	    }
	    break;
	default:
	    ;
    }
    return SignallingCircuit::sendEvent(type,params);
}

// Process incoming events for this circuit
bool MGCPCircuit::processEvent(MGCPTransaction* tr, MGCPMessage* mm)
{
    Debug(&splugin,DebugAll,"MGCPCircuit::processEvent(%p,%p) [%p]",
	tr,mm,this);
    if (tr == m_tr) {
	if (!mm || (tr->msgResponse())) {
	    tr->userData(0);
	    m_msg = mm;
	    m_tr = 0;
	}
    }
    return false;
}

// Process notifications for this circuit
bool MGCPCircuit::processNotify(const String& package, const String& event, const String& fullName)
{
    DDebug(&splugin,DebugAll,"MGCPCircuit::processNotify('%s','%s') %u [%p]",
	package.c_str(),event.c_str(),code(),this);
    if (package.null() || event.null())
	return false;
    if ((package == "L") || (package == "H")) {
	// Line or Handset events
	if (event &= "hd") {
	    if (!mySpan()->operational()) {
		Debug(&splugin,DebugMild,"Got Off-Hook on non-operational span '%s' [%p]",
		    mySpan()->id().c_str(),this);
		return false;
	    }
	    if (fxs())
		sendRequest(0,"L/hu(N),D/[0-9#*](N)");
	    return enqueueEvent(SignallingCircuitEvent::OffHook,fullName);
	}
	else if (event &= "hu") {
	    if (SignallingCircuit::status() == Connected)
		status(Idle,false);
	    if (m_needClear) {
		m_needClear = false;
		clearConn(true);
	    }
	    return enqueueEvent(SignallingCircuitEvent::OnHook,fullName);
	}
	else if (event &= "hf")
	    return enqueueEvent(SignallingCircuitEvent::Flash,fullName);
	else if (event &= "lsa")
	    return enqueueEvent(SignallingCircuitEvent::Polarity,fullName);
    }
    else if (package == "D") {
#if 0
	// TEST
	Debug(&splugin,DebugStub,"MGCPCircuit::processNotify() sending DTMF req [%p]",this);
        sendRequest(0,"D/[0-9#*](N)");
#endif
	// DTMF events
	if (event.length() == 1)
	    return enqueueEvent(SignallingCircuitEvent::Dtmf,fullName,event);
    }
    return false;
}

// We were forcibly disconnected by the gateway
void MGCPCircuit::processDelete(MGCPMessage* mm, const String& error)
{
    waitNotChanging();
    if (m_connId)
	Debug(&splugin,DebugWarn,"Gateway deleted connection '%s' on circuit %u [%p]",
	    m_connId.c_str(),code(),this);
    m_connId.clear();
    m_gwFormat.clear();
    m_gwFormatChanged = false;
    cleanupRtp();
    m_changing = false;
    unsigned int code = 0;
    String tmp(error);
    tmp >> code;
    switch (code) {
	case 501: // Endpoint is not ready or is out of service
	case 901: // Endpoint taken out of service
	case 904: // Manual intervention
	    // Disable the circuit and signal Alarm condition
	    SignallingCircuit::status(Disabled);
	    enqueueEvent(SignallingCircuitEvent::Alarm,error);
	    return;
	case 403: // Insufficient resources at this time
	case 502: // Insufficient resources (permanent)
	    // Delete all connections on the endpoint before going idle again
	    m_needClear = true;
	    // fall-through
	default:
	    SignallingCircuit::status(
		SignallingCircuit::status() >= Reserved ? Reserved : Idle);
    }
    // Signal a transient media failure condition
    enqueueEvent(SignallingCircuitEvent::Disconnected,error);
}

// Enqueue an event detected by this circuit
bool MGCPCircuit::enqueueEvent(SignallingCircuitEvent::Type type, const char* name,
    const char* dtmf)
{
    DDebug(&splugin,DebugAll,"Enqueueing event %u '%s' '%s' on %u [%p]",
	type,name,dtmf,code(),this);
    SignallingCircuitEvent* ev = new SignallingCircuitEvent(this,type,name);
    if (dtmf)
	ev->addParam("tone",dtmf);
    addEvent(ev);
    return true;
}


// Handler for chan.rtp messages - one per media type
bool RtpHandler::received(Message& msg)
{
    // refuse calls from a MGCP-GW
    if (!msg.getBoolValue("mgcp_allowed",true))
	return false;
    String trans = msg.getValue("transport");
    if (trans && !trans.startsWith("RTP/"))
	return false;
    Debug(&splugin,DebugAll,"RTP message received");

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    const char* media = msg.getValue("media","audio");
    MGCPWrapper* w = MGCPWrapper::find(ch,media);
    if (w)
	Debug(&splugin,DebugAll,"Wrapper %p found by CallEndpoint",w);
    else {
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

    const char* epId = msg.getValue("mgcp_endpoint",s_defaultEp);
    if (!epId)
	return false;

    if (ch)
	ch->clearEndpoint(media);
    w = new MGCPWrapper(ch,media,msg,epId);
    if (!w->rtpMessage(msg))
	return false;
    if (ch && ch->getPeer())
	w->connect(ch->getPeer()->getEndpoint(media));

    return true;
}


// Handler for chan.sdp messages - one message for all media at once
bool SdpHandler::received(Message& msg)
{
    // refuse calls from a MGCP-GW
    if (!msg.getBoolValue("mgcp_allowed",true))
	return false;
    Debug(&splugin,DebugAll,"SDP message received");

    return false;
}


// Handler for chan.dtmf messages, forwards them to the remote endpoint
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
    : Module("mgcpca","misc",true),
      m_parser("mgcpca","PSTN Circuit")
{
    Output("Loaded module MGCP-CA");
    m_parser.debugChain(this);
}

MGCPPlugin::~MGCPPlugin()
{
    Output("Unloading module MGCP-CA");
    s_wrappers.clear();
    s_spans.clear();
    delete s_engine;
}

void MGCPPlugin::statusParams(String& str)
{
    s_mutex.lock();
    str.append("spans=",",") << s_spans.count();
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
		    ep->alias = sect->getValue("name",name);
		    if (sect->getBoolValue("default",s_defaultEp.null()))
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
	if (s_defaultEp)
	    Debug(this,DebugCall,"Default remote endpoint: '%s'",s_defaultEp.c_str());
	int prio = cfg.getIntValue("general","priority",80);
	if (prio > 0) {
	    Engine::install(new RtpHandler(prio));
	    Engine::install(new SdpHandler(prio));
	    Engine::install(new DTMFHandler);
	}
    }
    m_parser.initialize(cfg.getSection("codecs"),cfg.getSection("hacks"));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
