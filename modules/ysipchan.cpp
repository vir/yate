/**
 * ysipchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Sip Channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <telengine.h>
#include <telephony.h>

#include <unistd.h>
#include <string.h>


#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

/**
 * we include also the sip stack headers
 */

#include <ysip.h>

using namespace TelEngine;

/* Payloads for the AV profile */
static TokenDict dict_payloads[] = {
    { "mulaw",   0 },
    { "gsm",     3 },
    { "lpc10",   7 },
    { "alaw",    8 },
    { "slin",   11 },
    { "g726",    2 },
    { "g722",    9 },
    { "g723",   12 },
};

static Configuration s_cfg;

class SIPHandler : public MessageHandler
{
public:
    SIPHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class SIPConnHandler : public MessageReceiver
{
public:
    enum {
	Ringing,
	Answered,
	Drop,
    };
    virtual bool received(Message &msg, int id);
};

class YateUDPParty : public SIPParty
{
public:
    YateUDPParty(int fd,struct sockaddr_in sin,int local);
    ~YateUDPParty();
    virtual void transmit(SIPEvent* event);
    virtual const char* getProtoName() const;
    virtual bool setParty(const URI& uri);
protected:
    int m_netfd;
    struct sockaddr_in m_sin;
};

class YateSIPEndPoint;

class YateSIPEngine : public SIPEngine
{
public:
    inline YateSIPEngine(YateSIPEndPoint* ep)
	: m_ep(ep)
	{ }
    virtual bool buildParty(SIPMessage* message);
private:
    YateSIPEndPoint* m_ep;
};

class YateSIPEndPoint : public Thread
{
public:
    YateSIPEndPoint();
    ~YateSIPEndPoint();
    bool Init(void);
   // YateSIPConnection *findconn(int did);
  //  void terminateall(void);
    void run(void);
    void incoming(SIPEvent* e);
    void invite(SIPEvent* e);
    bool buildParty(SIPMessage* message);
    inline ObjList &calls()
	{ return m_calls; }
    inline YateSIPEngine* engine() const
	{ return m_engine; }
private:
    ObjList m_calls;
    int m_localport;
    int m_port;
    int m_netfd;
    YateSIPEngine *m_engine;

};

class YateSIPConnection : public DataEndpoint
{
public:
    YateSIPConnection(Message& msg, SIPTransaction* tr);
    YateSIPConnection(Message& msg, const String& uri);
    ~YateSIPConnection();
    virtual void disconnected(bool final, const char *reason);
    virtual const String& toString() const
	{ return m_id; }
    bool process(SIPEvent* ev);
    void ringing(Message* msg = 0);
    void answered(Message* msg = 0);
    inline const String& id() const
        { return m_id; }
    inline const String& status() const
        { return m_status; }
    inline void setStatus(const char *status)
        { m_status = status; }
    inline void setTarget(const char *target = 0)
        { m_target = target; }
    inline const String& getTarget() const
        { return m_target; }
    inline SIPTransaction* getTransaction() const
	{ return m_tr; }
    static YateSIPConnection* find(const String& id);
private:
    SDPBody* createPasstroughSDP(Message &msg);
    SDPBody* createRtpSDP(SIPMessage* msg, const char* formats);
    SIPTransaction* m_tr;
    String m_id;
    String m_target;
    String m_status;
    String m_rtpid;
};

class SipMsgThread : public Thread
{
public:
    SipMsgThread(SIPTransaction* tr, Message* msg)
	: Thread("SipMsgThread"), m_tr(tr), m_msg(msg)
	{ m_tr->ref(); m_id = m_tr->getCallID(); }
    virtual void run();
    virtual void cleanup();
    bool route();
    inline static int count()
        { return s_count; }
    inline static int routed()
        { return s_routed; }
private:
    SIPTransaction* m_tr;
    Message* m_msg;
    String m_id;
    static int s_count;
    static int s_routed;
};

class SIPPlugin : public Plugin
{
public:
    SIPPlugin();
    ~SIPPlugin();
    virtual void initialize();
    inline YateSIPEndPoint* ep() const
	{ return m_endpoint; }
private:
    SIPConnHandler *m_handler;
    YateSIPEndPoint *m_endpoint;
};

static void parseSDP(SDPBody* sdp, String& addr, String& port, String& formats)
{
    const NamedString* c = sdp->getLine("c");
    if (c) {
	String tmp(*c);
	if (tmp.startSkip("IN IP4")) {
	    tmp.trimBlanks();
	    addr = tmp;
	}
    }
    c = sdp->getLine("m");
    if (c) {
	String tmp(*c);
	if (tmp.startSkip("audio")) {
	    int var = 0;
	    tmp >> var >> " RTP/AVP";
	    if (var > 0)
		port = var;
	    String fmt;
	    while (tmp[0] == ' ') {
		var = -1;
		tmp >> " " >> var;
		const char* payload = lookup(var,dict_payloads);
		if (payload) {
		    if (fmt)
			fmt << ",";
		    fmt << payload;
		}
	    }
	    formats = fmt;
	}
    }
}

static SIPPlugin plugin;
static ObjList s_calls;
static Mutex s_mutex;

YateUDPParty::YateUDPParty(int fd,struct sockaddr_in sin, int local)
    : m_netfd(fd), m_sin(sin)
{
    m_local = "localhost";
    m_localPort = local;
    m_party = inet_ntoa(m_sin.sin_addr);
    m_partyPort = ntohs(m_sin.sin_port);
    int s = ::socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (s != -1) {
	if (!::connect(s, (const sockaddr *)&m_sin, sizeof(m_sin))) {
	    struct sockaddr_in raddr;
	    socklen_t len = sizeof(raddr);
	    if (!::getsockname(s, (sockaddr *)&raddr, &len))
		m_local = ::inet_ntoa(raddr.sin_addr);
	}
	::close(s);
    }
}

YateUDPParty::~YateUDPParty()
{
    m_netfd = -1;
}

void YateUDPParty::transmit(SIPEvent* event)
{
    Debug(DebugAll,"Sending to %s:%d",inet_ntoa(m_sin.sin_addr),ntohs(m_sin.sin_port));
    ::sendto(m_netfd,
	event->getMessage()->getBuffer().data(),
	event->getMessage()->getBuffer().length(),
	0,
	(struct sockaddr *) &m_sin,
	sizeof(m_sin)
    );
}

const char* YateUDPParty::getProtoName() const
{
    return "UDP";
}

bool YateUDPParty::setParty(const URI& uri)
{
    int port = uri.getPort();
    if (port <= 0)
	port = 5060;
    struct hostent he, *res = 0;
    int err = 0;
    char buf[1024];
    if (::gethostbyname_r(uri.getHost().safe(),&he,buf,sizeof(buf),&res,&err)) {
	Debug("YateUDPParty",DebugWarn,"Error %d resolving name '%s' [%p]",
	    err,uri.getHost().safe(),this);
	return false;
    }
    m_sin.sin_family = he.h_addrtype;
    m_sin.sin_addr.s_addr = *((u_int32_t*)he.h_addr_list[0]);
    m_sin.sin_port = htons((short)port);
    m_party = uri.getHost();
    m_partyPort = port;
    Debug("YateUDPParty",DebugInfo,"New party is %s:%d (%s:%d) [%p]",
	m_party.c_str(),m_partyPort,
	inet_ntoa(m_sin.sin_addr),ntohs(m_sin.sin_port),
	this);
    return true;
}

bool YateSIPEngine::buildParty(SIPMessage* message)
{
    return m_ep->buildParty(message);
}

YateSIPEndPoint::YateSIPEndPoint()
    : Thread("YSIP EndPoint"), m_netfd(-1), m_engine(0)
{
    m_netfd = -1;
    Debug(DebugAll,"YateSIPEndPoint::YateSIPEndPoint() [%p]",this);
}

YateSIPEndPoint::~YateSIPEndPoint()
{
    Debug(DebugAll,"YateSIPEndPoint::~YateSIPEndPoint() [%p]",this);
    delete m_engine;
}

bool YateSIPEndPoint::buildParty(SIPMessage* message)
{
    if (message->isAnswer())
	return false;
    URI uri(message->uri);
    int port = uri.getPort();
    if (port <= 0)
	port = 5060;
    struct hostent he, *res = 0;
    int err = 0;
    char buf[1024];
    if (::gethostbyname_r(uri.getHost().safe(),&he,buf,sizeof(buf),&res,&err)) {
	Debug(DebugWarn,"Error %d resolving name '%s'",err,uri.getHost().safe());
	return false;
    }
    struct sockaddr_in sin;
    sin.sin_family = he.h_addrtype;
    sin.sin_addr.s_addr = *((u_int32_t*)he.h_addr_list[0]);
    sin.sin_port = htons((short)port);
    Debug(DebugAll,"built addr: %d %s:%d",
	sin.sin_family,inet_ntoa(sin.sin_addr),ntohs(sin.sin_port));
    message->setParty(new YateUDPParty(m_netfd,sin,m_port));
    return true;
}

bool YateSIPEndPoint::Init ()
{
    /*
     * This part have been taking from libiax after i have lost my sip driver for bayonne
     */
    struct sockaddr_in sin;
    int port = s_cfg.getIntValue("general","port",5060);
    m_localport = port;
    int flags;
    if (m_netfd > -1) {
	Debug(DebugInfo,"Already initialized.");
	return 0;
    }
    m_netfd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (m_netfd < 0) {
	Debug(DebugFail,"Unable to allocate UDP socket\n");
	return -1;
    }
    
    int sinlen = sizeof(sin);
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons((short)port);
    if (::bind(m_netfd, (struct sockaddr *) &sin, sinlen) < 0) {
	Debug(DebugFail,"Unable to bind to preferred port.  Using random one instead.");
    }
    
    if (::getsockname(m_netfd, (struct sockaddr *) &sin, (socklen_t *)&sinlen) < 0) {
	::close(m_netfd);
	m_netfd = -1;
	Debug(DebugFail,"Unable to figure out what I'm bound to.");
    }
    if ((flags = ::fcntl(m_netfd, F_GETFL)) < 0) {
	::close(m_netfd);
	m_netfd = -1;
	Debug(DebugFail,"Unable to retrieve socket flags.");
    }
    if (::fcntl(m_netfd, F_SETFL, flags | O_NONBLOCK) < 0) {
	::close(m_netfd);
	m_netfd = -1;
	Debug(DebugFail,"Unable to set non-blocking mode.");
    }
    port = ntohs(sin.sin_port);
    Debug(DebugInfo,"Started on port %d\n", port);
    m_port = port;
    m_engine = new YateSIPEngine(this);
    return true;
}

void YateSIPEndPoint::run ()
{
    fd_set fds;
    struct timeval tv;
    int retval;
    char buf[1500];
    struct sockaddr_in sin;
    /* Watch stdin (fd 0) to see when it has input. */
    for (;;)
    {
	FD_ZERO(&fds);
	FD_SET(m_netfd, &fds);
	/* Wait up to 20000 microseconds. */
        tv.tv_sec = 0;
	tv.tv_usec = 20000;

	retval = select(m_netfd+1, &fds, NULL, NULL, &tv);
	if (retval)
	{
	    // we got the dates
	    int sinlen = sizeof(sin);
	    
	    int res = ::recvfrom(m_netfd, buf, sizeof(buf)-1, 0, (struct sockaddr *) &sin,(socklen_t *) &sinlen);
	    if (res < 0) {
		if (errno != EAGAIN) {
		    Debug(DebugFail,"Error on read: %s\n", strerror(errno));
		}
	    } else {
		// we got already the buffer and here we start to do "good" stuff
		buf[res]=0;
		m_engine->addMessage(new YateUDPParty(m_netfd,sin,m_port),buf,res);
	    //	Output("res %d buf %s",res,buf);
	    }
	}
//	m_engine->process();
	SIPEvent* e = m_engine->getEvent();
	if (e) {
	    YateSIPConnection* conn = static_cast<YateSIPConnection*>(e->getTransaction()->getUserData());
	    if (conn) {
		if (conn->process(e))
		    delete e;
		else
		    m_engine->processEvent(e);
	    }
	    else if ((e->getState() == SIPTransaction::Trying) && !e->isOutgoing()) {
		incoming(e);
		delete e;
	    }
	    else
		m_engine->processEvent(e);
	}
    }
}

void YateSIPEndPoint::incoming(SIPEvent* e)
{
    if (e->getTransaction() && e->getTransaction()->isInvite()) {
	invite(e);
	return;
    }
}

static int s_maxqueue = 5;

void YateSIPEndPoint::invite(SIPEvent* e)
{
    if (Engine::exiting()) {
        Debug(DebugWarn,"Dropping call, engine is exiting");
	e->getTransaction()->setResponse(500, "Server Shutting Down");
        return;
    }
    int cnt = SipMsgThread::count();
    if (cnt > s_maxqueue) {
        Debug(DebugWarn,"Dropping call, there are already %d waiting",cnt);
	e->getTransaction()->setResponse(503, "Service Unavailable");
        return;
    }

    String callid(e->getTransaction()->getCallID());
    URI uri(e->getTransaction()->getURI());
    const HeaderLine* hl = e->getMessage()->getHeader("From");
    URI from(hl ? *hl : "");
    Message *m = new Message("call.preroute");
    m->addParam("driver","sip");
    m->addParam("id","sip/" + callid);
    m->addParam("caller",from.getUser());
    m->addParam("called",uri.getUser());
    m->addParam("sip.callid",callid);
    if (e->getMessage()->body && e->getMessage()->body->isSDP()) {
	String addr,port,formats;
	parseSDP(static_cast<SDPBody*>(e->getMessage()->body),addr,port,formats);
	m->addParam("rtp.addr",addr);
	m->addParam("rtp.port",port);
	m->addParam("formats",formats);
    }
    SipMsgThread *t = new SipMsgThread(e->getTransaction(),m);
    if (!t->startup()) {
        Debug(DebugWarn,"Error starting routing thread! [%p]",this);
        delete t;
	e->getTransaction()->setResponse(500, "Server Internal Error");
    }
}

static Mutex s_route;
int SipMsgThread::s_count = 0;
int SipMsgThread::s_routed = 0;

YateSIPConnection* YateSIPConnection::find(const String& id)
{
    ObjList* l = s_calls.find(id);
    return l ? static_cast<YateSIPConnection*>(l->get()) : 0;
}

// Incoming call constructor
YateSIPConnection::YateSIPConnection(Message& msg, SIPTransaction* tr)
    : m_tr(tr)
{
    Debug(DebugAll,"YateSIPConnection::YateSIPConnection(%p) [%p]",tr,this);
    s_mutex.lock();
    m_tr->ref();
    m_id = m_tr->getCallID();
    m_tr->setUserData(this);
    s_calls.append(this);
    s_mutex.unlock();
    if (msg.getValue("rtp.forward")) {
    }
}

// Outgoing call constructor
YateSIPConnection::YateSIPConnection(Message& msg, const String& uri)
    : m_tr(0)
{
    Debug(DebugAll,"YateSIPConnection::YateSIPConnection(%p,'%s') [%p]",
	&msg,uri.c_str(),this);
    SIPMessage* m = new SIPMessage("INVITE",uri);
    plugin.ep()->buildParty(m);
//    m->complete(plugin.ep()->engine());
    SDPBody* sdp = createPasstroughSDP(msg);
    if (!sdp)
	sdp = createRtpSDP(m,msg.getValue("formats"));
    m->setBody(sdp);
    m_tr = plugin.ep()->engine()->addMessage(m);
    m->deref();
    if (m_tr) {
	m_tr->ref();
	m_id = m_tr->getCallID();
	m_tr->setUserData(this);
    }
    Lock lock(s_mutex);
    s_calls.append(this);
}

YateSIPConnection::~YateSIPConnection()
{
    Debug(DebugAll,"YateSIPConnection::~YateSIPConnection() [%p]",this);
    Lock lock(s_mutex);
    s_calls.remove(this,false);
    if (m_tr) {
	m_tr->setUserData(0);
	m_tr->setResponse(487, "Request Terminated");
	m_tr->deref();
    }
}

SDPBody* YateSIPConnection::createPasstroughSDP(Message &msg)
{
    String tmp = msg.getValue("rtp.forward");
    if (!tmp.toBoolean())
	return 0;
    tmp = msg.getValue("rtp.port");
    int port = tmp.toInteger();
    tmp = msg.getValue("rtp.addr");
    if (port && tmp) {
	tmp = "IN IP4 " + tmp;
	String frm = msg.getValue("formats");
	if (frm.null())
	    frm = "alaw,mulaw";
	ObjList* l = tmp.split(',',false);
	frm = "audio ";
	frm << port << " RTP/AVP";
	ObjList* f = l;
	for (; f; f = f->next()) {
	    String* s = static_cast<String*>(f->get());
	    if (s) {
		int payload = s->toInteger(dict_payloads,-1);
		if (payload >= 0)
		    frm << " " << payload;
	    }
	}
	delete l;
	String owner;
	owner << "- " << port << " 1" << tmp;
	SDPBody* sdp = new SDPBody;
	sdp->addLine("v","0");
	sdp->addLine("o",owner);
	sdp->addLine("s","Call");
	sdp->addLine("t","0 0");
	sdp->addLine("c",tmp);
	sdp->addLine("m",frm);
	return sdp;
    }
    return 0;
}

SDPBody* YateSIPConnection::createRtpSDP(SIPMessage* msg, const char* formats)
{
    Message m("chan.rtp");
    m.addParam("direction","bidir");
    m.addParam("remoteip",msg->getParty()->getPartyAddr());
    m.userData(static_cast<DataEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	String port(m.getValue("localport"));
	String tmp(m.getValue("localip"));
	tmp = "IN IP4 " + tmp;
	String owner;
	owner << "- " << port << " 1" << tmp;
	String frm(formats);
	if (frm.null())
	    frm = "alaw,mulaw";
	ObjList* l = tmp.split(',',false);
	frm = "audio ";
	frm << port << " RTP/AVP";
	ObjList* f = l;
	for (; f; f = f->next()) {
	    String* s = static_cast<String*>(f->get());
	    if (s) {
		int payload = s->toInteger(dict_payloads,-1);
		if (payload >= 0)
		    frm << " " << payload;
	    }
	}
	delete l;
	SDPBody* sdp = new SDPBody;
	sdp->addLine("v","0");
	sdp->addLine("o",owner);
	sdp->addLine("s","Call");
	sdp->addLine("t","0 0");
	sdp->addLine("c",tmp);
	sdp->addLine("m",frm);
	return sdp;
    }
    return 0;
}

void YateSIPConnection::disconnected(bool final, const char *reason)
{
    Debug(DebugAll,"YateSIPConnection::disconnected() '%s'",reason);
    setStatus("disconnected");
    setTarget();
}

bool YateSIPConnection::process(SIPEvent* ev)
{
    Debug(DebugInfo,"YateSIPConnection::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    return false;
}

void YateSIPConnection::ringing(Message* msg)
{
    if (m_tr && (m_tr->getState() == SIPTransaction::Process))
	m_tr->setResponse(180, "Ringing");
    setStatus("ringing");
}

void YateSIPConnection::answered(Message* msg)
{
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
#if 0
	SDPBody* sdp = new SDPBody;
	sdp->addLine("v","0");
	sdp->addLine("o","- 99 1 IN IP4 192.168.168.2");
	sdp->addLine("s","Call");
	sdp->addLine("t","0 0");
	sdp->addLine("c","IN IP4 192.168.168.2");
	sdp->addLine("m","audio 9090 RTP/AVP 0 8");
#endif
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 200, "OK");
	SDPBody* sdp = 0;
	if (m_rtpid) {
	}
	else if (msg)
	    sdp = createPasstroughSDP(*msg);
	m->setBody(sdp);
	m_tr->setResponse(m);
	m->deref();
//	m_tr->setResponse(200, "OK");
	m_tr->deref();
	m_tr = 0;
    }
    setStatus("answered");
}

bool SipMsgThread::route()
{
    Debug(DebugAll,"Routing thread for %s [%p]",m_id.c_str(),this);
    Engine::dispatch(m_msg);
    *m_msg = "call.route";
    m_msg->retValue().clear();
    bool ok = Engine::dispatch(m_msg) && !m_msg->retValue().null();
    if (m_tr->getState() != SIPTransaction::Process) {
	Debug(DebugInfo,"SIP call %s (%p) vanished while routing!",m_id.c_str(),m_tr);
	return false;
    }
    if (ok) {
        *m_msg = "call.execute";
        m_msg->addParam("callto",m_msg->retValue());
        m_msg->retValue().clear();
	YateSIPConnection* conn = new YateSIPConnection(*m_msg,m_tr);
	m_msg->userData(conn);
	if (Engine::dispatch(m_msg)) {
            Debug(DebugInfo,"Routing SIP call %s (%p) to '%s' [%p]",
		m_id.c_str(),m_tr,m_msg->getValue("callto"),this);
	    conn->setStatus("routed");
	    conn->setTarget(m_msg->getValue("targetid"));
	    if (conn->getTarget().null()) {
		Debug(DebugInfo,"Answering now SIP call %s [%p] because we have no targetid",
		    conn->id().c_str(),conn);
		conn->answered();
	    }
	    else
		m_tr->setResponse(183, "Session Progress");
	}
	else {
	    Debug(DebugInfo,"Rejecting unconnected SIP call %s (%p) [%p]",
		m_id.c_str(),m_tr,this);
	    m_tr->setResponse(500, "Server Internal Error");
	    conn->setStatus("rejected");
	    conn->destruct();
	}
    }
    else {
	Debug(DebugInfo,"Rejecting unrouted SIP call %s (%p) [%p]",
	    m_id.c_str(),m_tr,this);
	m_tr->setResponse(404, "Not Found");
    }
    return ok;
}

void SipMsgThread::run()
{
    s_route.lock();
    s_count++;
    s_route.unlock();
    Debug(DebugAll,"Started routing thread for %s (%p) [%p]",
	m_id.c_str(),m_tr,this);
    bool ok = route();
    s_route.lock();
    s_count--;
    if (ok)
        s_routed++;
    s_route.unlock();
}

void SipMsgThread::cleanup()
{
    Debug(DebugAll,"Cleaning up routing thread for %s (%p) [%p]",
	m_id.c_str(),m_tr,this);
    delete m_msg;
    m_tr->deref();
}

bool SIPHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (!dest.startSkip("sip/",false))
	return false;
    if (!msg.userData()) {
        Debug(DebugWarn,"SIP call found but no data channel!");
        return false;
    }
    YateSIPConnection* conn = new YateSIPConnection(msg,dest);
    if (conn->getTransaction()) {
	DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
	if (dd && conn->connect(dd)) {
	    msg.addParam("targetid",conn->id());
	    conn->setTarget(msg.getValue("id"));
	    conn->deref();
	    return true;
	}
    }
    conn->destruct();
    return false;
}

bool SIPConnHandler::received(Message &msg, int id)
{
    String callid;
    switch (id) {
	case Answered:
	case Ringing:
	    callid = msg.getParam("targetid");
	    break;
	case Drop:
	    callid = msg.getParam("id");
	    break;
	default:
	    return false;
    }
    if (callid.null()) {
	if (id == Drop) {
	    Debug("SIP",DebugInfo,"Dropping all calls");
	    s_calls.clear();
	}
	return false;
    }
    Lock lock(s_mutex);
    YateSIPConnection* conn = YateSIPConnection::find(callid);
    if (!conn)
	return false;
    switch (id) {
	case Drop:
	    lock.drop();
	    conn->disconnect();
	    break;
	case Ringing:
	    conn->ringing(&msg);
	    break;
	case Answered:
	    conn->answered(&msg);
	    break;
	default:
	    return false;
    }
    return true;
}

SIPPlugin::SIPPlugin()
    : m_handler(0), m_endpoint(0)
{
    Output("Loaded module SIP Channel");
}

SIPPlugin::~SIPPlugin()
{
    Output("Unloading module SIP Channel");
}

void SIPPlugin::initialize()
{
    Output("Initializing module SIP Channel");
    s_cfg = Engine::configFile("sipchan");
    s_cfg.load();
    if (!m_endpoint) {
	m_endpoint = new YateSIPEndPoint();
	if(!(m_endpoint->Init()))
		{
		    delete m_endpoint;
		    m_endpoint = 0;
		    return;
		}
	else 
	    m_endpoint->startup();
    }
    if (!m_handler) {
	m_handler = new SIPConnHandler;
	Engine::install(new MessageRelay("call.ringing",m_handler,SIPConnHandler::Ringing));
	Engine::install(new MessageRelay("call.answered",m_handler,SIPConnHandler::Answered));
	Engine::install(new MessageRelay("call.drop",m_handler,SIPConnHandler::Drop));
	Engine::install(new SIPHandler("call.execute"));
//	Engine::install(new StatusHandler("engine.status"));
    }
}


/* vi: set ts=8 sw=4 sts=4 noet: */
