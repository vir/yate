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

/* Yate Payloads for the AV profile */
static TokenDict dict_payloads[] = {
    { "mulaw",   0 },
    { "alaw",    8 },
    { "gsm",     3 },
    { "lpc10",   7 },
    { "slin",   11 },
    { "g726",    2 },
    { "g722",    9 },
    { "g723",    4 },
    { "g728",   15 },
    { "g729",   18 },
};

/* SDP Payloads for the AV profile */
static TokenDict dict_rtpmap[] = {
    { "PCMU/8000",     0 },
    { "PCMA/8000",     8 },
    { "GSM/8000",      3 },
    { "LPC/8000",      7 },
    { "L16/8000",     11 },
    { "G726-32/8000",  2 },
    { "G722/8000",     9 },
    { "G723/8000",     4 },
    { "G728/8000",    15 },
    { "G729/8000",    18 },
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
    YateSIPEngine(YateSIPEndPoint* ep);
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
    bool incoming(SIPEvent* e, SIPTransaction* t);
    void invite(SIPEvent* e, SIPTransaction* t);
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
    enum {
	Incoming,
	Outgoing,
	Established,
	Cleared,
    };
    YateSIPConnection(Message& msg, SIPTransaction* tr);
    YateSIPConnection(Message& msg, const String& uri);
    ~YateSIPConnection();
    virtual void disconnected(bool final, const char *reason);
    virtual const String& toString() const
	{ return m_id; }
    bool process(SIPEvent* ev);
    void ringing(Message* msg = 0);
    void answered(Message* msg = 0);
    void doBye(SIPTransaction* t);
    void doCancel(SIPTransaction* t);
    void hangup();
    inline String id() const
        { return "sip/" + m_id; }
    inline const String& status() const
        { return m_status; }
    inline void setStatus(const char *status, int state = -1)
        { m_status = status; if (state >= 0) m_state = state; }
    inline void setTarget(const char *target = 0)
        { m_target = target; }
    inline const String& getTarget() const
        { return m_target; }
    inline SIPTransaction* getTransaction() const
	{ return m_tr; }
    static YateSIPConnection* find(const String& id);
private:
    void clearTransaction();
    SDPBody* createSDP(const char* addr, const char* port, const char* formats, const char* format = 0);
    SDPBody* createPasstroughSDP(Message &msg);
    SDPBody* createRtpSDP(SIPMessage* msg, const char* formats);
    SDPBody* createRtpSDP(bool start = false);
    bool startRtp();
    SIPTransaction* m_tr;
    bool m_hungup;
    int m_state;
    SIPDialog m_id;
    String m_uri;
    String m_target;
    String m_status;
    String m_rtpid;
    String m_rtpAddr;
    String m_rtpPort;
    String m_rtpFormat;
    String m_formats;
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
    Debug(DebugAll,"YateUDPParty local %s:%d party %s:%d",
	m_local.c_str(),m_localPort,
	m_party.c_str(),m_partyPort);
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

YateSIPEngine::YateSIPEngine(YateSIPEndPoint* ep)
    : m_ep(ep)
{
    addAllowed("INVITE");
    addAllowed("BYE");
    addAllowed("CANCEL");
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
	// hack: use a loop so we can use break and continue
	for (; e; m_engine->processEvent(e),e = 0) {
	    if (!e->getTransaction())
		continue;
	    YateSIPConnection* conn = static_cast<YateSIPConnection*>(e->getTransaction()->getUserData());
	    if (conn) {
		if (conn->process(e)) {
		    delete e;
		    break;
		}
		else
		    continue;
	    }
	    if ((e->getState() == SIPTransaction::Trying) &&
		!e->isOutgoing() && incoming(e,e->getTransaction())) {
		delete e;
		break;
	    }
	}
    }
}

bool YateSIPEndPoint::incoming(SIPEvent* e, SIPTransaction* t)
{
    if (e->getTransaction()->isInvite())
	invite(e,t);
    else if (t->getMethod() == "BYE") {
	YateSIPConnection* conn = YateSIPConnection::find(t->getCallID());
	if (conn)
	    conn->doBye(t);
	else
	    t->setResponse(481,"Call/Transaction Does Not Exist");
    }
    else if (t->getMethod() == "CANCEL") {
	YateSIPConnection* conn = YateSIPConnection::find(t->getCallID());
	if (conn)
	    conn->doCancel(t);
	else
	    t->setResponse(481,"Call/Transaction Does Not Exist");
    }
    else
	return false;
    return true;
}

static int s_maxqueue = 5;

void YateSIPEndPoint::invite(SIPEvent* e, SIPTransaction* t)
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

    String callid(t->getCallID());
    URI uri(t->getURI());
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
	m->addParam("rtp_forward","possible");
	m->addParam("rtp_addr",addr);
	m->addParam("rtp_port",port);
	m->addParam("formats",formats);
    }
    SipMsgThread *thr = new SipMsgThread(t,m);
    if (!thr->startup()) {
        Debug(DebugWarn,"Error starting routing thread %p ! [%p]",thr,this);
        delete thr;
	t->setResponse(500, "Server Internal Error");
    }
}

static Mutex s_route;
int SipMsgThread::s_count = 0;
int SipMsgThread::s_routed = 0;

YateSIPConnection* YateSIPConnection::find(const String& id)
{
    Debug("YateSIPConnection",DebugAll,"finding '%s'",id.c_str());
    ObjList* l = s_calls.find(id);
    return l ? static_cast<YateSIPConnection*>(l->get()) : 0;
}

// Incoming call constructor - after call.route but before call.execute
YateSIPConnection::YateSIPConnection(Message& msg, SIPTransaction* tr)
    : m_tr(tr), m_hungup(false), m_state(Incoming)
{
    Debug(DebugAll,"YateSIPConnection::YateSIPConnection(%p) [%p]",tr,this);
    s_mutex.lock();
    m_tr->ref();
    m_id = *m_tr->initialMessage();
    m_uri = m_tr->initialMessage()->getHeader("From");
    m_tr->setUserData(this);
    s_calls.append(this);
    s_mutex.unlock();
    m_rtpAddr = msg.getValue("rtp_addr");
    m_rtpPort = msg.getValue("rtp_port");
    m_formats = msg.getValue("formats");
    int q = m_formats.find(',');
    m_rtpFormat = m_formats.substr(0,q);
    Debug(DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
}

// Outgoing call constructor - in call.execute handler
YateSIPConnection::YateSIPConnection(Message& msg, const String& uri)
    : m_tr(0), m_hungup(false), m_state(Outgoing), m_uri(uri)
{
    Debug(DebugAll,"YateSIPConnection::YateSIPConnection(%p,'%s') [%p]",
	&msg,uri.c_str(),this);
    SIPMessage* m = new SIPMessage("INVITE",uri);
    plugin.ep()->buildParty(m);
    SDPBody* sdp = createPasstroughSDP(msg);
    if (!sdp)
	sdp = createRtpSDP(m,msg.getValue("formats"));
    m->setBody(sdp);
    m_tr = plugin.ep()->engine()->addMessage(m);
    m->deref();
    if (m_tr) {
	m_tr->ref();
	m_id = *m_tr->initialMessage();
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
    hangup();
    clearTransaction();
}

void YateSIPConnection::clearTransaction()
{
    if (m_tr) {
	m_tr->setUserData(0);
	if (m_tr->isIncoming())
	    m_tr->setResponse(487,"Request Terminated");
	m_tr->deref();
	m_tr = 0;
    }
}

void YateSIPConnection::hangup()
{
    if (m_hungup)
	return;
    m_hungup = true;
    Message *msg = new Message("call.hangup");
    msg->addParam("driver","sip");
    msg->addParam("id",id());
    if (m_target)
        msg->addParam("targetid",m_target);
    Engine::enqueue(msg);
    msg = 0;
    switch (m_state) {
	case Cleared:
	    clearTransaction();
	    return;
	case Incoming:
	    if (m_tr) {
		clearTransaction();
		return;
	    }
	    break;
	case Outgoing:
	    if (m_tr) {
		SIPMessage* m = new SIPMessage("CANCEL",m_uri);
		const SIPMessage* i = m_tr->initialMessage();
		m->copyHeader(i,"Via");
		m->copyHeader(i,"From");
		m->copyHeader(i,"To");
		m->copyHeader(i,"Call-ID");
		String tmp;
		tmp << i->getCSeq() << " CANCEL";
		m->addHeader("CSeq",tmp);
		plugin.ep()->buildParty(m);
		plugin.ep()->engine()->addMessage(m);
		m->deref();
	    }
	    break;
    }
    clearTransaction();
    m_state = Cleared;

    SIPMessage* m = new SIPMessage("BYE",m_uri);
    m->addHeader("Call-ID",m_id);
    String tmp;
    tmp << "<" << m_id.localURI << ">";
    HeaderLine* hl = new HeaderLine("From",tmp);
    hl->setParam("tag",m_id.localTag);
    m->addHeader(hl);
    tmp.clear();
    tmp << "<" << m_id.remoteURI << ">";
    hl = new HeaderLine("To",tmp);
    hl->setParam("tag",m_id.remoteTag);
    m->addHeader(hl);
    plugin.ep()->buildParty(m);
    plugin.ep()->engine()->addMessage(m);
    m->deref();
    disconnect();
}

// Creates a SDP from RTP address data present in message
SDPBody* YateSIPConnection::createPasstroughSDP(Message &msg)
{
    String tmp = msg.getValue("rtp_forward");
    msg.clearParam("rtp_forward");
    if (!tmp.toBoolean())
	return 0;
    tmp = msg.getValue("rtp_port");
    int port = tmp.toInteger();
    String addr(msg.getValue("rtp_addr"));
    if (port && addr) {
	SDPBody* sdp = createSDP(addr,tmp,msg.getValue("formats"));
	if (sdp)
	    msg.setParam("rtp_forward","accepted");
	return sdp;
    }
    return 0;
}

// Creates an unstarted external RTP channel from remote addr and builds SDP from it
SDPBody* YateSIPConnection::createRtpSDP(SIPMessage* msg, const char* formats)
{
    Message m("chan.rtp");
    m.addParam("direction","bidir");
    m.addParam("remoteip",msg->getParty()->getPartyAddr());
    m.userData(static_cast<DataEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	return createSDP(m.getValue("localip"),m.getValue("localport"),formats);
    }
    return 0;
}

// Creates a started external RTP channel from remote addr and builds SDP from it
SDPBody* YateSIPConnection::createRtpSDP(bool start)
{
    Message m("chan.rtp");
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    if (start) {
	m.addParam("remoteport",m_rtpPort);
	m.addParam("format",m_rtpFormat);
    }
    m.userData(static_cast<DataEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	if (start)
	    m_rtpFormat = m.getValue("format");
	return createSDP(m.getValue("localip"),m.getValue("localport"),m_formats,m_rtpFormat);
    }
    return 0;
}

// Starts an already created external RTP channel
bool YateSIPConnection::startRtp()
{
    if (m_rtpid.null())
	return false;
    Debug(DebugAll,"YateSIPConnection::startSDP() [%p]",this);
    Message m("chan.rtp");
    m.addParam("rtpid",m_rtpid);
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    m.addParam("remoteport",m_rtpPort);
    m.addParam("format",m_rtpFormat);
    m.userData(static_cast<DataEndpoint *>(this));
    return Engine::dispatch(m);
}

SDPBody* YateSIPConnection::createSDP(const char* addr, const char* port, const char* formats, const char* format)
{
    Debug(DebugAll,"YateSIPConnection::createSDP('%s','%s','%s') [%p]",
	addr,port,formats,this);
//    return 0;
    int t = Time::now() / 10000000000ULL;
    String tmp;
    tmp << "IN IP4 " << addr;
    String owner;
    owner << "1001 " << t << " " << t << " " << tmp;
    String frm(format ? format : formats);
    if (frm.null())
	frm = "alaw,mulaw";
    ObjList* l = frm.split(',',false);
    frm = "audio ";
    frm << port << " RTP/AVP";
    ObjList rtpmap;
    ObjList* f = l;
    for (; f; f = f->next()) {
	String* s = static_cast<String*>(f->get());
	if (s) {
	    int payload = s->toInteger(dict_payloads,-1);
	    if (payload >= 0) {
		frm << " " << payload;
		const char* map = lookup(payload,dict_rtpmap);
		if (map) {
		    String* tmp = new String("rtpmap:");
		    *tmp << payload << " " << map;
		    rtpmap.append(tmp);
		}
	    }
	}
    }
    delete l;

//    frm << " 101";
//    rtpmap.append(new String("rtpmap:101 telephone-event/8000"));

    SDPBody* sdp = new SDPBody;
    sdp->addLine("v","0");
    sdp->addLine("o",owner);
    sdp->addLine("s","Session");
    sdp->addLine("c",tmp);
    sdp->addLine("t","0 0");
    sdp->addLine("m",frm);
    for (f = &rtpmap; f; f = f->next()) {
	String* s = static_cast<String*>(f->get());
	if (s)
	    sdp->addLine("a",*s);
    }
    rtpmap.clear();
    return sdp;
}

void YateSIPConnection::disconnected(bool final, const char *reason)
{
    Debug(DebugAll,"YateSIPConnection::disconnected() '%s' [%p]",reason,this);
    setStatus("disconnected");
    setTarget();
}

bool YateSIPConnection::process(SIPEvent* ev)
{
    Debug(DebugInfo,"YateSIPConnection::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    m_id = *ev->getTransaction()->recentMessage();
    if (ev->getState() == SIPTransaction::Cleared) {
	if (m_tr) {
	    Lock lock(s_mutex);
	    Debug(DebugInfo,"YateSIPConnection clearing transaction %p [%p]",
		m_tr,this);
	    m_tr->setUserData(0);
	    m_tr->deref();
	    m_tr = 0;
	}
	if (m_state != Established)
	    hangup();
	return false;
    }
    if (!ev->getMessage() || ev->getMessage()->isOutgoing())
	return false;
    if (ev->getMessage()->body && ev->getMessage()->body->isSDP()) {
	Debug(DebugInfo,"YateSIPConnection got SDP [%p]",this);
	parseSDP(static_cast<SDPBody*>(ev->getMessage()->body),
	    m_rtpAddr,m_rtpPort,m_formats);
	int q = m_formats.find(',');
	m_rtpFormat = m_formats.substr(0,q);
	Debug(DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	    m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
    }
    if (ev->getMessage()->isAnswer() && ((ev->getMessage()->code / 100) == 2)) {
	setStatus("answered",Established);
	Message *m = new Message("call.answered");
	m->addParam("driver","sip");
	m->addParam("id",id());
	if (m_target)
	    m->addParam("targetid",m_target);
	m->addParam("status","answered");
	if (m_rtpPort) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_rtpAddr);
	    m->addParam("rtp_port",m_rtpPort);
	    m->addParam("formats",m_formats);
	}
	Engine::enqueue(m);
    }
    if (ev->getMessage()->isACK()) {
	Debug(DebugInfo,"YateSIPConnection got ACK [%p]",this);
	startRtp();
    }
    return false;
}

void YateSIPConnection::doBye(SIPTransaction* t)
{
    Debug(DebugAll,"YateSIPConnection::doBye(%p) [%p]",t,this);
    t->setResponse(200,"OK");
    hangup();
}

void YateSIPConnection::doCancel(SIPTransaction* t)
{
    Debug(DebugAll,"YateSIPConnection::doCancel(%p) [%p]",t,this);
    if (m_tr) {
	t->setResponse(200,"OK");
	m_tr->setResponse(487,"Request Terminated");
	disconnect("Cancelled");
    }
    else
	t->setResponse(481,"Call/Transaction Does Not Exist");
}

void YateSIPConnection::ringing(Message* msg)
{
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	m_tr->setResponse(180, "Ringing");
//	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 180, "Ringing");
//	SDPBody* sdp = startRTP(msg,false);
//	m->setBody(sdp);
//	m_tr->setResponse(m);
//	m->deref();
    }
    setStatus("ringing");
}

void YateSIPConnection::answered(Message* msg)
{
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 200, "OK");
	SDPBody* sdp = msg ? createPasstroughSDP(*msg) : 0;
	if (!sdp)
	    sdp = createRtpSDP();
	m->setBody(sdp);
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("answered",Established);
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
	    conn->deref();
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
    m_tr->ref();
    bool ok = route();
    m_tr->deref();
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
	    callid = msg.getValue("targetid");
	    break;
	case Drop:
	    callid = msg.getValue("id");
	    break;
	default:
	    return false;
    }
    if (!callid.startSkip("sip/",false) || callid.null()) {
	if (id == Drop) {
	    Debug("SIP",DebugInfo,"Dropping all calls");
	    s_calls.clear();
	}
	return false;
    }
    Lock lock(s_mutex);
    YateSIPConnection* conn = YateSIPConnection::find(callid);
    Debug("SIP",DebugInfo,"Connhandler lookup '%s' returned %p",
	callid.c_str(),conn);
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
