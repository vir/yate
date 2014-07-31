/**
 * mgcpca.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Media Gateway Control Protocol - Call Agent - also remote data helper
 *  for other protocols
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
#include <yatemgcp.h>
#include <yatesig.h>
#include <yatesdp.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace TelEngine;
namespace { // anonymous

class MGCPCircuit;

class YMGCPEngine : public MGCPEngine
{
public:
    inline YMGCPEngine(const NamedList* params)
	: MGCPEngine(false,0,params), m_timedOutTrans(0), m_timedOutDels(0)
	{ }
    virtual ~YMGCPEngine();
    virtual bool processEvent(MGCPTransaction* trans, MGCPMessage* msg);
    virtual void timeout(MGCPTransaction* trans);
    inline unsigned int trTimeouts()
    {
	unsigned int tmp = m_timedOutTrans;
	m_timedOutTrans = 0;
	return tmp;
    }
    inline unsigned int delTimeouts()
    {
	unsigned int tmp = m_timedOutDels;
	m_timedOutDels = 0;
	return tmp;
    }
private:
    unsigned int m_timedOutTrans;
    unsigned int m_timedOutDels;
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
    GenObject* m_this;
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
    enum RqntType {
	RqntNone = 0,
	RqntOnce = 1,
	RqntMore = 2,
    };
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
    inline const String& bearer() const
	{ return m_bearer; }
    inline const String& options() const
	{ return m_options; }
    inline const char* version() const
	{ return m_version.null() ? "MGCP 1.0" : m_version.c_str(); }
    inline bool fxo() const
	{ return m_fxo; }
    inline bool fxs() const
	{ return m_fxs; }
    inline bool rtpForward() const
	{ return m_rtpForward; }
    inline bool sdpForward() const
	{ return m_sdpForward; }
    inline bool rtpForcedFwd() const
	{ return m_rtpForcedFwd; }
    inline bool rqntEmbed() const
	{ return m_rqntEmbed; }
    inline RqntType rqntType() const
	{ return m_rqntType; }
    inline const char* rqntStr() const
	{ return m_rqntStr; }
    inline bool rqntCheck() const
	{ return m_rqntCheck; }
    bool ownsId(const String& rqId, const String& epId) const;
    static SignallingComponent* create(const String& type, const NamedList& name);
    static MGCPSpan* findNotify(const String& id, const String& epId);
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
    bool m_rtpForcedFwd;
    bool m_fxo;
    bool m_fxs;
    bool m_ntfyMatch;
    bool m_rqntEmbed;
    bool m_rqntCheck;
    RqntType m_rqntType;
    String m_rqntStr;
    String m_notify;
    String m_address;
    String m_version;
    String m_bearer;
    String m_options;
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
	    m->addParam("id",m_owner,false);
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
    void mediaChanged(const SDPMedia& media);
private:
    void waitNotChanging(bool clearTrans = false);
    MGCPMessage* message(const char* cmd);
    bool sendAsync(MGCPMessage* mm, bool notify = false);
    RefPointer<MGCPMessage> sendSync(MGCPMessage* mm);
    bool sendRequest(const char* sigReq, const char* reqEvt = 0, const char* digitMap = 0);
    bool sendPending(const char* sigReq = 0);
    bool enqueueEvent(SignallingCircuitEvent::Type type, const char* name, const char* dtmf = 0);
    void cleanupRtp(bool all = true);
    bool createRtp();
    bool setupConn(const char* mode = 0);
    String m_epId;
    Status m_statusReq;
    String m_notify;
    String m_specialMode;
    String m_owner;
    bool m_changing;
    bool m_pending;
    bool m_delayed;
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
    GenObject* m_this;
    MGCPTransaction* m_tr;
    RefPointer<MGCPMessage> m_msg;
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
    virtual void genUpdate(Message& msg);
    virtual void appendNotif(NamedString* notif);
private:
    SDPParser m_parser;
    NamedList m_notifs;
};

YSIGFACTORY2(MGCPSpan);

static YMGCPEngine* s_engine = 0;
static MGCPEndpoint* s_endpoint = 0;
static String s_defaultEp;
static int s_tzOffset = 0;

static MGCPPlugin splugin;
static ObjList s_wrappers;
static ObjList s_spans;
static Mutex s_mutex(false,"MGCP-CA");

// Media gateway bearer information (mapped from SDPParser::s_payloads)
static const TokenDict s_dict_gwbearerinfo[] = {
    { "e:mu",          0 },
    { "e:A",           8 },
    {      0,          0 }
};

static const TokenDict s_dict_rqnt[] = {
    { "none", MGCPSpan::RqntNone },
    { "once", MGCPSpan::RqntOnce },
    { "more", MGCPSpan::RqntMore },
    { "no",   MGCPSpan::RqntNone },
    { "yes",  MGCPSpan::RqntOnce },
    { "off",  MGCPSpan::RqntNone },
    { "on",   MGCPSpan::RqntOnce },
    { 0,      0                  }
};


class RtpHandler : public MessageHandler
{
public:
    RtpHandler(unsigned int prio)
	: MessageHandler("chan.rtp",prio,splugin.name())
	{ }
    virtual bool received(Message &msg);
};

class SdpHandler : public MessageHandler
{
public:
    SdpHandler(unsigned int prio)
	: MessageHandler("chan.sdp",prio,splugin.name())
	{ }
    virtual bool received(Message &msg);
};

class DTMFHandler : public MessageHandler
{
public:
    DTMFHandler()
	: MessageHandler("chan.dtmf",150,splugin.name())
	{ }
    virtual bool received(Message &msg);
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
    static const Regexp r("\\([0-9]\\+\\)@");
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
bool YMGCPEngine::processEvent(MGCPTransaction* trans, MGCPMessage* msg)
{
    s_mutex.lock();
    void* data = trans ? trans->userData() : 0;
    RefPointer<MGCPWrapper> wrap = YOBJECT(MGCPWrapper,static_cast<GenObject*>(data));
    RefPointer<MGCPSpan> span = YOBJECT(MGCPSpan,static_cast<GenObject*>(data));
    RefPointer<MGCPCircuit> circ = YOBJECT(MGCPCircuit,static_cast<GenObject*>(data));
    s_mutex.unlock();
    DDebug(this,DebugAll,"YMGCPEngine::processEvent(%p,%p) wrap=%p span=%p circ=%p [%p]",
	trans,msg,(void*)wrap,(void*)span,(void*)circ,this);
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
	while (msg->name() == YSTRING("NTFY")) {
	    const String* rqId = msg->params.getParam(YSTRING("x"));
	    const String* event = msg->params.getParam(YSTRING("o"));
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
		    span = MGCPSpan::findNotify(*rqId,msg->endpointId());
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
	if (msg->name() == YSTRING("RSIP")) {
	    const String* method = msg->params.getParam(YSTRING("rm"));
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
	else if (msg->name() == YSTRING("DLCX")) {
	    const String* error = msg->params.getParam(YSTRING("e"));
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

void YMGCPEngine::timeout(MGCPTransaction* tr)
{
    DDebug(&splugin,DebugInfo,"Handle timed out transaction [%p]",tr);
    if (!tr)
	return;
    if (tr->timeout()) {
	const MGCPMessage* cmd = tr->initial();
	if (!cmd || !cmd->isCommand())
	    return;
	if (cmd->name() != YSTRING("DLCX"))
	    m_timedOutTrans++;
	else
	    m_timedOutDels++;
	MGCPEndpointId epId(tr->ep());
	splugin.appendNotif(new NamedString("mgcp_gw_down",epId.host()));
	splugin.changed();
    }
}

MGCPWrapper::MGCPWrapper(CallEndpoint* conn, const char* media, Message& msg, const char* epId)
    : DataEndpoint(conn,media),
      m_this(0), m_tr(0), m_connEp(epId)
{
    Debug(&splugin,DebugAll,"MGCPWrapper::MGCPWrapper(%p,'%s','%s') [%p]",
	conn,media,epId,this);
    m_id = "mgcp/";
    m_id << (unsigned int)Random::random();
    if (conn)
	m_master = conn->id();
    m_master = msg.getValue(YSTRING("id"),(conn ? conn->id().c_str() : (const char*)0));
    m_audio = (name() == YSTRING("audio"));
    s_mutex.lock();
    m_this = this;
    s_wrappers.append(this);
//    setupRTP(localip,rtcp);
    s_mutex.unlock();
}

MGCPWrapper::~MGCPWrapper()
{
    Debug(&splugin,DebugAll,"MGCPWrapper::~MGCPWrapper() '%s' [%p]",
	name().c_str(),this);
    s_mutex.lock();
    m_this = 0;
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
    DDebug(&splugin,DebugAll,"MGCPWrapper::processEvent(%p,%p) [%p]",
	tr,mm,this);
    if (tr == m_tr) {
	if (!mm || (tr->msgResponse())) {
	    s_mutex.lock();
	    tr->userData(0);
	    m_msg = mm;
	    m_tr = 0;
	    s_mutex.unlock();
	}
    }
    else if (mm) {
	if (mm->name() == YSTRING("NTFY")) {
	    String* event = mm->params.getParam(YSTRING("o"));
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
    bool fini = msg.getBoolValue(YSTRING("terminate"));
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
    String dir = msg.getValue(YSTRING("direction"),m_connId.null() ? "bidir" : "");
    if (dir == YSTRING("bidir"))
	dir = "sendrecv";
    else if (dir == YSTRING("send"))
	dir = "sendonly";
    else if (dir == YSTRING("receive"))
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
    mm = sendSync(mm,ep->address());
    if (!mm)
	return false;
    if (m_connId.null())
	m_connId = mm->params.getParam(YSTRING("i"));
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
    s_engine->sendCommand(mm,ep->address());
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
    u_int64_t t1 = Time::msecNow();
    while (m_msg) {
	if (Thread::check(false))
	    return 0;
	Thread::idle();
    }
    u_int64_t t2 = Time::msecNow();
    RefPointer<MGCPTransaction> tr = s_engine->sendCommand(mm,address,false);
    s_mutex.lock();
    tr->userData(m_this);
    m_tr = tr;
    s_mutex.unlock();
    while (m_tr == tr) {
	Thread::idle();
	s_engine->processTransaction(tr);
    }
    if (tr)
	tr->setEngineProcess();
    tr = 0;
    RefPointer<MGCPMessage> tmp = m_msg;
    m_msg = 0;
    u_int64_t t3 = Time::msecNow();
    if (!tmp)
	Debug(&splugin,DebugMild,"MGCPWrapper::sendSync() returning NULL in %u+%u ms [%p]",
	    (unsigned int)(t2-t1),(unsigned int)(t3-t2),this);
    else {
	int level = DebugAll;
	if (t3-t1 > 500)
	    level = DebugMild;
	else if (t3-t1 > 350)
	    level = DebugNote;
	else if (t3-t1 > 200)
	    level = DebugInfo;
	Debug(&splugin,level,"MGCPWrapper::sendSync() returning %d '%s' in %u+%u ms [%p]",
	    tmp->code(),tmp->comment().c_str(),
	    (unsigned int)(t2-t1),(unsigned int)(t3-t2),this);
    }
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
    return s_engine->sendCommand(mm,ep->address()) != 0;
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
    return s_engine->sendCommand(mm,ep->address()) != 0;
}


// Called by the factory to create MGCP spans
SignallingComponent* MGCPSpan::create(const String& type, const NamedList& name)
{
    if (type != "SignallingCircuitSpan")
	return 0;
    const String* spanName = name.getParam(YSTRING("voice"));
    if (!spanName)
	spanName = &name;
    if (null(spanName) || !s_endpoint)
	return 0;
    MGCPEpInfo* ep = s_endpoint->findAlias(*spanName);
    if (!ep) {
	DDebug(&splugin,DebugAll,"No endpoint info for span '%s'",spanName->c_str());
	return 0;
    }
    TempObjectCounter cnt(splugin.objectsCounter());
    MGCPSpan* span = new MGCPSpan(name,spanName->safe("MGCPSpan"),*ep);
    if (span->init(name))
	return span;
    TelEngine::destruct(span);
    return 0;
}

// Find a span by its Notify-ID
MGCPSpan* MGCPSpan::findNotify(const String& id, const String& epId)
{
    if (id.null())
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_spans;
    for (; l; l=l->next()) {
	MGCPSpan* s = static_cast<MGCPSpan*>(l->get());
	if (s && s->ownsId(id,epId))
	    return s;
    }
    return 0;
}

MGCPSpan::MGCPSpan(const NamedList& params, const char* name, const MGCPEpInfo& ep)
    : SignallingCircuitSpan(params.getValue("debugname",name),
	static_cast<SignallingCircuitGroup*>(params.getObject(YATOM("SignallingCircuitGroup")))),
      m_circuits(0), m_count(0), m_epId(ep), m_operational(false),
      m_rtpForward(false), m_sdpForward(false), m_rtpForcedFwd(false), m_fxo(false), m_fxs(false),
      m_ntfyMatch(true), m_rqntEmbed(true), m_rqntCheck(true), m_rqntType(RqntOnce)
{
    Debug(&splugin,DebugAll,"MGCPSpan::MGCPSpan(%p,'%s') [%p]",
	&params,name,this);
    u_int32_t ntfy = (u_int32_t)Random::random();
    m_notify.hexify(&ntfy,sizeof(ntfy));
    m_rqntStr = "D/[0-9#*](N)";
    const AnalogLineGroup* analog = YOBJECT(AnalogLineGroup,group());
    if (analog) {
	m_ntfyMatch = false;
	switch (analog->type()) {
	    case AnalogLine::FXO:
		m_fxo = true;
		break;
	    case AnalogLine::FXS:
		m_fxs = true;
		m_rqntStr = "L/hu(N)," + m_rqntStr;
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
    const String* sect = params.getParam(YSTRING("voice"));
    if (!sect)
	sect = params.getParam(YSTRING("basename"));
    if (!sect)
	sect = &params;
    int cicStart = params.getIntValue(YSTRING("start"));
    if ((cicStart < 0) || !sect)
	return false;
    Configuration cfg(Engine::configFile("mgcpca"));
    String sn = "gw " + *sect;
    const NamedList* config = cfg.getSection(sn);
    if (!config) {
	// Try to find a template section for gateway:number
	int sep = sn.rfind('+');
	if (sep > 0)
	    config = cfg.getSection(sn.substr(0,sep));
    }
    if (!config) {
	Debug(m_group,DebugWarn,"MGCPSpan('%s'). Failed to find config section [%p]",
	    id().safe(),this);
	return false;
    }
    SignallingCircuitRange range(config->getValue(YSTRING("voicechans")));
    m_count = 1;
    if (range.count())
	m_count = range[range.count()-1];
    m_count = config->getIntValue(YSTRING("chans"),m_count);
    cicStart += config->getIntValue(("offset_"+*sect),
	config->getIntValue(YSTRING("offset")));
    cicStart = config->getIntValue(("start_"+*sect),
	config->getIntValue(YSTRING("start"),cicStart));

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
    m_increment = config->getIntValue(("increment_"+*sect),
	config->getIntValue(YSTRING("increment"),m_increment));
    bool digital = !(m_fxo || m_fxs);
    m_rtpForcedFwd = digital && ((*config)[YSTRING("forward_rtp")] == YSTRING("always"));
    m_rtpForward = m_rtpForcedFwd || config->getBoolValue(YSTRING("forward_rtp"),digital);
    m_sdpForward = config->getBoolValue(YSTRING("forward_sdp"),false);
    m_bearer = lookup(config->getIntValue(YSTRING("bearer"),SDPParser::s_payloads,-1),s_dict_gwbearerinfo);
    m_ntfyMatch = config->getBoolValue(YSTRING("match_ntfy"),m_ntfyMatch);
    m_rqntEmbed = config->getBoolValue(YSTRING("req_embed"),true);
    m_rqntCheck = config->getBoolValue(YSTRING("req_check"),true);
    m_rqntType = (RqntType)config->getIntValue(YSTRING("req_dtmf"),s_dict_rqnt,RqntOnce);
    bool fax = config->getBoolValue(YSTRING("req_fax"),true);
    bool t38 = config->getBoolValue(YSTRING("req_t38"),fax);
    // build Fax notification request
    if (fax || t38) {
	if (RqntNone == m_rqntType) {
	    m_rqntType = RqntOnce;
	    m_rqntStr.clear();
	}
	if (fax)
	    m_rqntStr.append("G/ft(N)",",");
	if (t38)
	    m_rqntStr.append("fxr/t38",",");
    }
    m_rqntStr = config->getValue(YSTRING("req_evts"),m_rqntStr);
    // build Local Connection Options
    String fmts;
    splugin.parser().getAudioFormats(fmts);
    ObjList* l = fmts.split(',',false);
    fmts.clear();
    for (ObjList* o = l; o; o = o->next()) {
	const String* s = static_cast<const String*>(o->get());
	if (!s)
	    continue;
	String fmt = lookup(lookup(*s,SDPParser::s_payloads),SDPParser::s_rtpmap);
	int slash = fmt.find('/');
	if (slash >= 0)
	    fmt = fmt.substr(0,slash);
	if (fmt.null() || (fmts.find(fmt) >= 0))
	    continue;
	if (fmts)
	    fmts << ";" << fmt;
	else
	    fmts << "a:" << fmt;
    }
    TelEngine::destruct(l);
    m_options = fmts;
    bool clear = config->getBoolValue(YSTRING("clearconn"),false);
    m_circuits = new MGCPCircuit*[m_count];
    unsigned int i;
    for (i = 0; i < m_count; i++)
	m_circuits[i] = 0;
    bool ok = true;
    String first;
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
	if (first.null())
	    first << (cicStart + i) << " '" << name << "'";
	circuit->ref();
	if (clear)
	    circuit->needClear();
    }

    if (ok) {
	Debug(&splugin,DebugNote,"MGCPSpan '%s' first circuit=%s",
	    id().safe(),first.c_str());
	m_version = config->getValue(YSTRING("version"));
	const String* addr = config->getParam(YSTRING("address"));
	if (!TelEngine::null(addr) && addr->toBoolean(true)) {
	    const MGCPEpInfo* ep = s_endpoint->find(epId().id());
	    if (ep) {
		SocketAddr a(ep->address().family());
		if (addr->toBoolean(false))
		    a.host(ep->host());
		else
		    a.host(addr);
		a.port(ep->port());
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
    SignallingCircuitEvent::Type evType = active ? SignallingCircuitEvent::NoAlarm :
	SignallingCircuitEvent::Alarm;
    for (unsigned int i = 0; i < m_count; i++) {
	MGCPCircuit* circuit = m_circuits[i];
	if (circuit) {
	    circuit->status((active ? SignallingCircuit::Idle : SignallingCircuit::Missing),true);
	    circuit->addEvent(new SignallingCircuitEvent(circuit,evType));
	}
    }
}

// Set the operational state and copy GW address
void MGCPSpan::operational(const SocketAddr& address)
{
    if (address.host().null() || (address.host() == YSTRING("0.0.0.0")))
	return;
    m_address = address.host();
    const MGCPEpInfo* ep = s_endpoint->find(epId().id());
    if (ep && !(m_operational && ep->address().valid()))
	const_cast<MGCPEpInfo*>(ep)->address(address);
    operational(true);
}

// Get a configuration or operational boolean parameter by name
bool MGCPSpan::getBoolParam(const String& param, bool defValue) const
{
    if (param == YSTRING("operational"))
	return operational();
    if (param == YSTRING("rtp_forward"))
	return m_rtpForward;
    if (param == YSTRING("sdp_forward"))
	return m_sdpForward;
    return defValue;
}

#ifdef XDEBUG
static bool mismatch(const char* s)
{
    Debug(DebugAll,"MGCP No match: %s",s);
    return false;
}
#else
#define mismatch(s) false
#endif

// Check if this span matches an endpoint ID
bool MGCPSpan::matchEndpoint(const MGCPEndpointId& ep)
{
    XDebug(DebugAll,"MGCP Match: %s@%s:%d vs %s@%s:%d",
	ep.user().c_str(),ep.host().c_str(),ep.port(),
	m_epId.user().c_str(),m_epId.host().c_str(),m_epId.port());
    if (ep.port() && (ep.port() != m_epId.port()))
	return mismatch("Port differs");
    if ((ep.host() |= m_epId.host()))
	return mismatch("Host differs");
    if ((ep.user() &= m_epId.user()))
	return true;
    if (ep.user() == "*")
	return true;
    if (findCircuit(ep.id()))
	return true;
    // check for wildcards like */*/*
    static const Regexp s_termsAll("^\\*[/*]\\+\\*$");
    if (s_termsAll.matches(ep.user()))
	return true;
    String tmp = ep.user();
    // check for prefix*/*
    static const Regexp s_finalAll("^\\([^*]\\+\\)[/*]\\+$");
    if (tmp.matches(s_finalAll) && m_epId.user().startsWith(tmp.matchString(1),false,true))
	return true;
    // check for prefix[min-max] or prefix*/[min-max]
    static const Regexp s_finalRange("^\\(.*\\)\\[\\([0-9]\\+\\)-\\([0-9]\\+\\)\\]$");
    if (!tmp.matches(s_finalRange))
	return mismatch("No range");
    int idx = -1;
    if (tmp.matchString(1).endsWith("*/")) {
	if (!m_epId.user().startsWith(tmp.matchString(1).substr(0,tmp.matchLength(1)-2)))
	    return mismatch("Different wildcard range prefix");
	idx = m_epId.user().rfind('/');
	if (idx < 0)
	    return mismatch("No wildcard range separator");
	idx++;
    }
    else {
	if (!m_epId.user().startsWith(tmp.matchString(1),false,true))
	    return mismatch("Different range prefix");
	idx = tmp.matchLength(1);
    }
    idx = m_epId.user().substr(idx).toInteger(-1,10);
    if (idx < 0)
	return mismatch("User suffix not numeric");
    int rMin = tmp.matchString(2).toInteger(idx+1,10);
    int rMax = tmp.matchString(3).toInteger(-1,10);
    if (((idx + (int)m_count - 1) < rMin) || (idx > rMax))
	return mismatch("Suffix not in range");
    return true;
}

// Check if a request Id is for this span or one of its circuits
bool MGCPSpan::ownsId(const String& rqId, const String& epId) const
{
    if (ntfyId() == rqId)
	return true;
    for (unsigned int i = 0; i < m_count; i++) {
	MGCPCircuit* circuit = m_circuits[i];
	if (!circuit)
	    continue;
	if (m_ntfyMatch) {
	    if (circuit->ntfyId() == rqId)
		return true;
	}
	else {
	    if (circuit->epId() == epId)
		return true;
	}
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
    if ((id.rfind(':') < 0) && (m_epId.id().find(':') >= 0))
	id << ":" << m_epId.port();
    for (unsigned int i = 0; i < m_count; i++) {
	MGCPCircuit* circuit = m_circuits[i];
	if (!circuit)
	    continue;
	if (localId && m_ntfyMatch) {
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

    if (mm->name() == YSTRING("NTFY")) {
	const String* rqId = mm->params.getParam(YSTRING("x"));
	if (null(rqId))
	    return false;
	const String* event = mm->params.getParam(YSTRING("o"));
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
      m_epId(id), m_statusReq(Missing),
      m_changing(false), m_pending(false), m_delayed(false),
      m_gwFormatChanged(false), m_localRtpChanged(false), m_needClear(false),
      m_this(0), m_tr(0)
{
    DDebug(&splugin,DebugAll,"MGCPCircuit::MGCPCircuit(%u,%p,'%s') [%p]",
	code,span,id,this);
    u_int32_t cic = code;
    m_notify.hexify(&cic,sizeof(cic));
    m_callId.hexify(this,sizeof(this));
    m_callId += m_notify;
    m_notify = span->ntfyId() + m_notify;
    m_gwFormat = span->bearer();
    m_owner << span->id() << "/" << code;
    m_this = this;
}

MGCPCircuit::~MGCPCircuit()
{
    DDebug(&splugin,DebugAll,"MGCPCircuit::~MGCPCircuit() %u [%p]",
	code(),this);
    s_mutex.lock();
    m_this = 0;
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
	if (name == YATOM("DataSource"))
	    return m_source;
	if (name == YATOM("DataConsumer"))
	    return m_consumer;
    }
    if (name == YATOM("MGCPCircuit"))
	return (void*)this;
    return SignallingCircuit::getObject(name);
}

// Media changed notification, reimplemented from SDPSession
void MGCPCircuit::mediaChanged(const SDPMedia& media)
{
    SDPSession::mediaChanged(media);
    if (media.id() && media.transport()) {
	Message m("chan.rtp");
	m.addParam("rtpid",media.id());
	m.addParam("media",media);
	m.addParam("transport",media.transport());
	m.addParam("terminate",String::boolText(true));
	m.addParam("mgcp_allowed",String::boolText(false));
	Engine::dispatch(m);
    }
}

// Clean up any RTP we may still hold
void MGCPCircuit::cleanupRtp(bool all)
{
    setMedia(0);
    resetSdp(all);
    m_rtpForward = mySpan()->rtpForcedFwd();
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
    cleanupRtp(false);
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
	cleanupRtp(false);
    }
    TelEngine::destruct(de);
    return ok;
}

// Create or update remote connection
bool MGCPCircuit::setupConn(const char* mode)
{
    bool create = m_connId.null();
    RefPointer<MGCPMessage> mm = message(create ? "CRCX" : "MDCX");
    mm->params.addParam("C",m_callId);
    if (m_connId) {
	mm->params.addParam("I",m_connId);
	if (m_delayed) {
	    m_delayed = false;
	    mm->params.addParam("X",m_notify);
	    mm->params.addParam("R",mySpan()->rqntStr());
	}
    }
    else if (mySpan()->rqntEmbed() && mySpan()->rqntStr() &&
	(mySpan()->rqntType() != MGCPSpan::RqntNone) && !(fxs() || fxo())) {
	mm->params.addParam("X",m_notify);
	if (mode && !mySpan()->rqntCheck())
	    m_delayed = true;
	else
	    mm->params.addParam("R",mySpan()->rqntStr());
    }
    if (m_gwFormatChanged && m_gwFormat)
	mm->params.addParam("B",m_gwFormat);
    bool rtpChange = false;
    const char* faxOpt = 0;
    if (m_specialMode == "t38")
	faxOpt = "a:image/t38";
    else if (m_specialMode == "fax")
	faxOpt = "a:PCMU;PCMA";
    if (faxOpt) {
	mm->params.addParam("M","sendrecv");
	mm->params.addParam("L",faxOpt);
	m_specialMode.clear();
	mode = 0;
	rtpChange = true;
    }
    else if (mode)
	mm->params.addParam("M",mode);
    else if (m_localRawSdp.trimBlanks()) {
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
	else {
	    mm->params.addParam("M","inactive");
	    if (create)
		mm->params.addParam("L",mySpan()->options(),false);
	    rtpChange = true;
	}
    }
    mm = sendSync(mm);
    if (!mm)
	return false;
    if (mm->code() == 400 && mm->params.count() == 0 &&
	mm->comment().startsWith("Setup failed",true)) {
	// 400 nnnnn Setup failed 0:(11:135):0:63
	Debug(&splugin,DebugWarn,"Cisco DSP failure detected on circuit %u [%p]",code(),this);
	return false;
    }
    m_gwFormatChanged = false;
    if (m_connId.null())
	m_connId = mm->params.getParam(YSTRING("i"));
    if (m_connId.null()) {
	m_needClear = true;
	return false;
    }
    m_localRtpChanged = rtpChange && hasLocalRtp();;
    MimeSdpBody* sdp = static_cast<MimeSdpBody*>(mm->sdp[0]);
    if (sdp) {
	String oldIp = m_rtpAddr;
	bool mediaChanged = setMedia(splugin.parser().parse(*sdp,m_rtpAddr,
	    m_rtpMedia,String::empty(),m_rtpForward && !create));
	const DataBlock& raw = sdp->getBody();
	m_remoteRawSdp.assign((const char*)raw.data(),raw.length());
	// Disconnect if media changed
	if (mediaChanged && oldIp && oldIp != m_rtpAddr)
	    enqueueEvent(SignallingCircuitEvent::Disconnected,"Disconnected");
	return true;
    }
    return (mm->code() >= 200 && mm->code() <= 299);
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
    if (mySpan()->rqntStr() && (mySpan()->rqntType() != MGCPSpan::RqntNone) && !(fxs() || fxo())) {
	if (mySpan()->rqntEmbed()) {
	    mm->params.addParam("X",m_notify);
	    mm->params.addParam("R","");
	}
	else
	    sendRequest(0,"");
    }
    if (mySpan()->bearer() != m_gwFormat) {
	m_gwFormat = mySpan()->bearer();
	m_gwFormatChanged = true;
    }
    m_connId.clear();
    m_specialMode.clear();
    resetSdp(false);
    m_rtpForward = mySpan()->rtpForcedFwd();
    m_remoteRawSdp.clear();
    m_localRtpChanged = false;
    m_delayed = false;
    sendAsync(mm,true);
    sendPending();
}

// Wait for changing flag to be false
// Clear pending transaction if requested and the circuit is changing
void MGCPCircuit::waitNotChanging(bool clearTrans)
{
    while (true) {
	Lock lock(s_mutex);
	if (!m_changing) {
	    m_changing = true;
	    break;
	}
	if (clearTrans && m_tr) {
	    m_tr->userData(0);
	    m_tr = 0;
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
bool MGCPCircuit::sendAsync(MGCPMessage* mm, bool notify)
{
    if (!mm)
	return false;
    MGCPEpInfo* ep = s_endpoint->find(mySpan()->epId().id());
    if (ep) {
	MGCPTransaction* tr = s_engine->sendCommand(mm,ep->address());
	if (tr) {
	    if (notify) {
		s_mutex.lock();
		tr->userData(m_this);
		s_mutex.unlock();
	    }
	    return true;
	}
    }
    else
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
    u_int64_t t1 = Time::msecNow();
    while (m_msg) {
	if (Thread::check(false))
	    return 0;
	Thread::idle();
    }
    u_int64_t t2 = Time::msecNow();
    RefPointer<MGCPTransaction> tr = s_engine->sendCommand(mm,ep->address(),false);
    s_mutex.lock();
    tr->userData(m_this);
    m_tr = tr;
    s_mutex.unlock();
    while (m_tr == tr) {
	Thread::idle();
	s_engine->processTransaction(tr);
    }
    if (tr)
	tr->setEngineProcess();
    tr = 0;
    RefPointer<MGCPMessage> tmp = m_msg;
    m_msg = 0;
    u_int64_t t3 = Time::msecNow();
    if (!tmp)
	Debug(&splugin,DebugMild,"MGCPCircuit::sendSync() returning NULL in %u+%u ms [%p]",
	    (unsigned int)(t2-t1),(unsigned int)(t3-t2),this);
    else {
	int level = DebugAll;
	if (t3-t1 > 500)
	    level = DebugMild;
	else if (t3-t1 > 350)
	    level = DebugNote;
	else if (t3-t1 > 200)
	    level = DebugInfo;
	Debug(&splugin,level,"MGCPCircuit::sendSync() returning %d '%s' in %u+%u ms [%p]",
	    tmp->code(),tmp->comment().c_str(),
	    (unsigned int)(t2-t1),(unsigned int)(t3-t2),this);
    }
    return tmp;
}

// Send asynchronously a notification request
bool MGCPCircuit::sendRequest(const char* sigReq, const char* reqEvt, const char* digitMap)
{
    if (!(sigReq || reqEvt || digitMap))
	return false;
    Debug(&splugin,DebugInfo,"MGCPCircuit%s%s%s%s%s%s %u [%p]",
	(sigReq ? " Signal out: " : ""),c_safe(sigReq),
	(reqEvt ? " Notify on: " : ""),c_safe(reqEvt),
	(digitMap ? " Digit map: " : ""),c_safe(digitMap),
	code(),this);
    MGCPMessage* mm = message("RQNT");
    mm->params.addParam("X",m_notify);
    if (sigReq)
	mm->params.addParam("S",sigReq);
    else
	m_pending = false;
    if (reqEvt) {
	mm->params.addParam("R",reqEvt);
	m_delayed = false;
    }
    if (digitMap)
	mm->params.addParam("D",digitMap);
    return sendAsync(mm);
}

// Send or clear pending (timeout) signal requests
bool MGCPCircuit::sendPending(const char* sigReq)
{
    if (TelEngine::null(sigReq) && !m_pending)
	return true;
    Debug(&splugin,DebugInfo,"MGCPCircuit %s pending%s%s %u [%p]",
	(sigReq ? "Signal" : "Clear all"),
	(sigReq ? ": " : ""),c_safe(sigReq),
	code(),this);
    MGCPMessage* mm = message("RQNT");
    mm->params.addParam("X",m_notify);
    if (sigReq)
	mm->params.addParam("S",sigReq);
    if (!sendAsync(mm))
	return false;
    m_pending = !TelEngine::null(sigReq);
    return true;
}

// Circuit status change request
bool MGCPCircuit::status(Status newStat, bool sync)
{
    Debug(&splugin,DebugInfo,"MGCPCircuit::status(%s,%s) %u [%p]",
	lookupStatus(newStat),String::boolText(sync),code(),this);
    TempObjectCounter cnt(splugin.objectsCounter());
    waitNotChanging();
    // Don't notify local rtp if we already have it (addr/port/sdp) and didn't changed
    // Accept only synchronous connect requests
    bool allowRtpChange = false;
    if (newStat == Connected) {
	if (!sync) {
	    m_changing = false;
	    return false;
	}
	allowRtpChange = SignallingCircuit::status() == Connected && m_localRtpChanged;
	if (SignallingCircuit::status() != Connected) {
	    if (mySpan()->rqntType() != MGCPSpan::RqntNone && !(fxs() || fxo() || mySpan()->rqntEmbed()))
		sendRequest(0,mySpan()->rqntStr());
	    sendPending();
	}
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
	    if ((m_specialMode == YSTRING("loopback") ||
		m_specialMode == YSTRING("conttest")) &&
		setupConn(m_specialMode))
		break;
	    if (m_rtpForward)
		return false;
	    special = true;
	    // fall through, we'll check it later after connecting
	case Connected:
	    // Create local rtp if we don't have one
	    // Start it if we don't forward the rtp
	    if (m_rtpForward || hasLocalRtp() || createRtp() ||
		(m_rtpForward = mySpan()->rtpForward())) {
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
		if (m_rtpForward) {
		    m_source = 0;
		    m_consumer = 0;
		}
		else
		    cleanupRtp(false);
	    }
	    m_statusReq = SignallingCircuit::status();
	    m_changing = false;
	    return false;
	case Reserved:
	    if (SignallingCircuit::status() <= Idle)
		cleanupRtp();
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
	m.addParam("id",m_owner,false);
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
    int fmt = lookup(format,SDPParser::s_payloads,-1);
    const char* gwFmt = lookup(fmt,s_dict_gwbearerinfo);
    if (!gwFmt)
	return false;
    TempObjectCounter cnt(splugin.objectsCounter());
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
    TempObjectCounter cnt(splugin.objectsCounter());
    bool rtpChanged = false;
    bool reConnect = false;
    if (param == YSTRING("sdp_raw")) {
	rtpChanged = m_localRawSdp != value;
	m_localRawSdp = value;
    }
    else if (param == YSTRING("rtp_forward")) {
	bool fwd = value.toBoolean();
	rtpChanged = m_rtpForward != fwd;
	m_rtpForward = fwd;
	reConnect = rtpChanged && !fwd && (SignallingCircuit::status() == Connected);
    }
    else if (param == YSTRING("rtp_rfc2833"))
	setRfc2833(value);
    else if (param == YSTRING("special_mode")) {
	if (value != m_specialMode) {
	    rtpChanged = true;
	    m_specialMode = value;
	}
    }
    else
	return false;
    if (rtpChanged && hasLocalRtp())
	m_localRtpChanged = true;
    lock.drop();
    DDebug(&splugin,DebugAll,"MGCPCircuit::setParam(%s,%s) %u [%p]",
	param.c_str(),value.c_str(),code(),this);
    if (reConnect) {
	m_localRawSdp.clear();
	m_localRtpChanged = true;
	return status(Connected,true);
    }
    return true;
}

bool MGCPCircuit::getParam(const String& param, String& value) const
{
    if (m_changing)
	return false;
    Lock lock(s_mutex);
    if (m_changing)
	return false;
    TempObjectCounter cnt(splugin.objectsCounter());
    if (param == YSTRING("rtp_addr")) {
	value = m_rtpAddr;
	return true;
    }
    else if (param == YSTRING("sdp_raw")) {
	value = m_remoteRawSdp;
	return true;
    }
    else if (param == YSTRING("special_mode")) {
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
    TempObjectCounter cnt(splugin.objectsCounter());
    if (params == "rtp") {
	waitNotChanging();
	DDebug(&splugin,DebugAll,"MGCPCircuit::setParams(rtp) %u [%p]",code(),this);
	String* raw = params.getParam(YSTRING("sdp_raw"));
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
    TempObjectCounter cnt(splugin.objectsCounter());
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
    TempObjectCounter cnt(splugin.objectsCounter());
    switch (type) {
	case SignallingCircuitEvent::Connect:
	    if (params)
		setParams(*params);
	    return status(Connected,!params || params->getBoolValue(YSTRING("sync"),true));
	case SignallingCircuitEvent::RingBegin:
	    if (fxs()) {
		String s("L/rg");
		if (params) {
		    String number = params->getValue(YSTRING("caller"));
		    String name = params->getValue(YSTRING("callername"));
		    if (number || name) {
			MimeHeaderLine::addQuotes(number);
			MimeHeaderLine::addQuotes(name);
			int year;
			unsigned int month,day,hour,minutes,seconds;
			int tzo = params->getIntValue(YSTRING("tzoffset"),s_tzOffset);
			Time::toDateTime(Time::secNow()+tzo,year,month,day,hour,minutes,seconds);
			char buf[16];
			::snprintf(buf,sizeof(buf),"%02u/%02u/%02u/%02u",month,day,hour,minutes);
			buf[sizeof(buf)-1] = '\0';
			s << ",L/ci(" << buf << "," << number << "," << name << ")";
		    }
		}
		return sendPending(s);
	    }
	    return false;
	case SignallingCircuitEvent::RingEnd:
	    return sendPending();
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
		const String* tone = params->getParam(YSTRING("tone"));
		if (!tone)
		    tone = params;
		if (!null(tone))
		    return sendRequest("D/" + *tone);
	    }
	    break;
	case SignallingCircuitEvent::GenericTone:
	    if (params) {
		const String* tone = params->getParam(YSTRING("tone"));
		if (!tone)
		    tone = params;
		if (null(tone))
		    break;
		if (*tone == YSTRING("ringback") || *tone == YSTRING("ring") || *tone == YSTRING("rt"))
		    return sendPending("G/rt");
		if (*tone == YSTRING("congestion") || *tone == YSTRING("cg"))
		    return sendPending("G/cg");
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
    if (tr->state() < MGCPTransaction::Responded && !tr->timeout())
	return false;
    DDebug(&splugin,DebugAll,"MGCPCircuit::processEvent(%p,%p) [%p]",
	tr,mm,this);
    if (tr == m_tr) {
	if (!mm || (tr->msgResponse())) {
	    s_mutex.lock();
	    tr->userData(0);
	    m_msg = mm;
	    m_tr = 0;
	    s_mutex.unlock();
	    if (tr->timeout())
		enqueueEvent(SignallingCircuitEvent::Disconnected,"Timeout");
	}
    }
    else if (tr->initial() && (tr->initial()->name() == YSTRING("DLCX"))) {
	int err = 406;
	if (tr->msgResponse()) {
	    if (tr->state() > MGCPTransaction::Responded)
		return false;
	    err = tr->msgResponse()->code();
	}
	if (err < 300)
	    return false;
	s_mutex.lock();
	tr->userData(0);
	s_mutex.unlock();
	Debug(&splugin,DebugWarn,"Gateway error %d deleting connection on circuit %u [%p]",
	    err,code(),this);
	if (err >= 500) {
	    String error;
	    error << "Error " << err;
	    if (tr->msgResponse())
		error.append(tr->msgResponse()->comment(),": ");
	    switch (err) {
		case 515: // Incorrect connection-id
		case 516: // Unknown or incorrect call-id
		case 520: // Endpoint is restarting
		    break;
		default:
		    // Disable the circuit and signal Alarm condition
		    SignallingCircuit::status(Disabled);
		    enqueueEvent(SignallingCircuitEvent::Alarm,error);
	    }
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
	    if (fxs() && mySpan()->rqntType() != MGCPSpan::RqntNone)
		sendRequest(0,mySpan()->rqntStr());
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
	if (mySpan()->rqntType() == MGCPSpan::RqntMore)
	    sendRequest(0,mySpan()->rqntStr());
	// DTMF events
	if (event.length() == 1)
	    return enqueueEvent(SignallingCircuitEvent::Dtmf,fullName,event);
    }
    else if (package == "G") {
	if (mySpan()->rqntType() == MGCPSpan::RqntMore)
	    sendRequest(0,mySpan()->rqntStr());
	// Generic events
	if (event &= "ft")
	    return enqueueEvent(SignallingCircuitEvent::GenericTone,"fax");
    }
    else if (package == "FXR") {
	if (mySpan()->rqntType() == MGCPSpan::RqntMore)
	    sendRequest(0,mySpan()->rqntStr());
	// Fax Relay events
	if (event &= "t38(start)")
	    return enqueueEvent(SignallingCircuitEvent::GenericTone,"fax");
	else if (event &= "t38(stop)")
	    return enqueueEvent(SignallingCircuitEvent::GenericTone,"audio");
    }
    return false;
}

// We were forcibly disconnected by the gateway
void MGCPCircuit::processDelete(MGCPMessage* mm, const String& error)
{
    waitNotChanging(true);
    if (m_connId)
	Debug(&splugin,DebugWarn,"Gateway deleted connection '%s' on circuit %u [%p]",
	    m_connId.c_str(),code(),this);
    m_connId.clear();
    m_gwFormat = mySpan()->bearer();
    m_gwFormatChanged = false;
    cleanupRtp(false);
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
	    if (SignallingCircuit::status() > Reserved)
		SignallingCircuit::status(Reserved);
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
    if (!msg.getBoolValue(YSTRING("mgcp_allowed"),true))
	return false;
    String trans = msg.getValue(YSTRING("transport"));
    if (trans && !trans.startsWith("RTP/"))
	return false;
    Debug(&splugin,DebugAll,"RTP message received");

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    const char* media = msg.getValue(YSTRING("media"),"audio");
    MGCPWrapper* w = MGCPWrapper::find(ch,media);
    if (w)
	Debug(&splugin,DebugAll,"Wrapper %p found by CallEndpoint",w);
    else {
	w = MGCPWrapper::find(msg.getValue(YSTRING("rtpid")));
	if (w)
	    Debug(&splugin,DebugAll,"Wrapper %p found by ID",w);
    }
    const char* epId = msg.getValue(YSTRING("mgcp_endpoint"),s_defaultEp);
    if (!(ch || w)) {
	if (epId)
	    Debug(&splugin,DebugWarn,"Neither call channel nor MGCP wrapper found!");
	return false;
    }

    if (w)
	return w->rtpMessage(msg);

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
    if (!msg.getBoolValue(YSTRING("mgcp_allowed"),true))
	return false;
    Debug(&splugin,DebugAll,"SDP message received");

    return false;
}


// Handler for chan.dtmf messages, forwards them to the remote endpoint
bool DTMFHandler::received(Message& msg)
{
    String targetid(msg.getValue(YSTRING("targetid")));
    if (targetid.null())
	return false;
    String text(msg.getValue(YSTRING("text")));
    if (text.null())
	return false;
    MGCPWrapper* wrap = MGCPWrapper::find(targetid);
    return wrap && wrap->sendDTMF(text);
}


MGCPPlugin::MGCPPlugin()
    : Module("mgcpca","misc",true),
      m_parser("mgcpca","PSTN Circuit"),
      m_notifs("notifs")
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
    for (l = s_spans.skipNull(); l; l = l->skipNext()) {
	MGCPSpan* s = static_cast<MGCPSpan*>(l->get());
        str.append(s->id(),",") << "=" << s->epId().id();
    }
    s_mutex.unlock();
}

void MGCPPlugin::initialize()
{
    Output("Initializing module MGCP Call Agent");
    Configuration cfg(Engine::configFile("mgcpca"));
    setup();
    s_tzOffset = cfg.getIntValue(YSTRING("general"),YSTRING("tzoffset"),0);
    NamedList* engSect = cfg.getSection(YSTRING("engine"));
    if (s_engine && engSect)
	s_engine->initialize(*engSect);
    while (!s_engine) {
	if (!(engSect && engSect->getBoolValue(YSTRING("enabled"),true)))
	    break;
	int n = cfg.sections();
	for (int i = 0; i < n; i++) {
	    NamedList* sect = cfg.getSection(i);
	    if (!sect)
		continue;
	    String name(*sect);
	    if (name.startSkip("gw") && name) {
		const char* host = sect->getValue(YSTRING("host"));
		if (!host)
		    continue;
		if (!s_engine) {
		    s_engine = new YMGCPEngine(engSect);
		    s_engine->debugChain(this);
		    s_endpoint = new MGCPEndpoint(
			s_engine,
			cfg.getValue(YSTRING("endpoint"),YSTRING("user"),"yate"),
			cfg.getValue(YSTRING("endpoint"),YSTRING("host"),s_engine->address().host()),
			cfg.getIntValue(YSTRING("endpoint"),YSTRING("port"))
		    );
		}
		int port = sect->getIntValue(YSTRING("port"),0);
		String user = sect->getValue(YSTRING("user"),name);
		name = sect->getValue(YSTRING("name"),name);
		SignallingCircuitRange range(sect->getValue(YSTRING("range")));
		if (range.count() && user.find('*') >= 0) {
		    // This section is a template
		    for (unsigned int idx = 0; idx < range.count(); idx++) {
			String num(range[idx]);
			String tmpN = name + "+" + num;
			String tmpU(user);
			// Replace * with number
			int sep;
			while ((sep = tmpU.find('*')) >= 0)
			    tmpU = tmpU.substr(0,sep) + num + tmpU.substr(sep+1);
			MGCPEpInfo* ep = s_endpoint->append(tmpU,host,port);
			if (ep)
			    ep->alias = tmpN;
			else
			    Debug(this,DebugWarn,"Could not set user '%s' for gateway '%s'",
				tmpU.c_str(),tmpN.c_str());
		    }
		    continue;
		}
		MGCPEpInfo* ep = s_endpoint->append(user,host,port);
		if (ep) {
		    ep->alias = name;
		    if (sect->getBoolValue(YSTRING("cluster"),false))
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
	int prio = cfg.getIntValue(YSTRING("general"),YSTRING("priority"),80);
	if (prio > 0) {
	    Engine::install(new RtpHandler(prio));
	    Engine::install(new SdpHandler(prio));
	    Engine::install(new DTMFHandler);
	}
    }
    m_parser.initialize(cfg.getSection("codecs"),cfg.getSection("hacks"),
	cfg.getSection("general"));
}

void MGCPPlugin::genUpdate(Message& msg)
{
    Lock l(this);
    msg.copyParams(m_notifs);
    msg.setParam("tr_timedout",String(s_engine->trTimeouts()));
    msg.setParam("del_timedout",String(s_engine->delTimeouts()));
    m_notifs.clearParams();
}

void MGCPPlugin::appendNotif(NamedString* notif)
{
    Lock l(this);
    m_notifs.addParam(notif);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
