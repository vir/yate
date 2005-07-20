/**
 * ysipchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Sip Channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

#include <yatephone.h>
#include <yatesip.h>

#include <string.h>


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
    {      0,    0 },
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
    {           0,     0 },
};

static TokenDict dict_errors[] = {
    { "incomplete", 484 },
    { "noroute", 404 },
    { "noconn", 503 },
    { "noauth", 401 },
    { "nomedia", 415 },
    { "busy", 486 },
    { "rejected", 406 },
    { "forbidden", 403 },
    { "congestion", 480 },
    { "failure", 500 },
    { "looping", 483 },
    {  0,   0 },
};

class YateUDPParty : public SIPParty
{
public:
    YateUDPParty(Socket* sock, const SocketAddr& addr, int local);
    ~YateUDPParty();
    virtual void transmit(SIPEvent* event);
    virtual const char* getProtoName() const;
    virtual bool setParty(const URI& uri);
protected:
    Socket* m_sock;
    SocketAddr m_addr;
};

class YateSIPEndPoint;

class YateSIPEngine : public SIPEngine
{
public:
    YateSIPEngine(YateSIPEndPoint* ep);
    virtual bool buildParty(SIPMessage* message);
    virtual bool checkUser(const String& username, const String& realm, const String& nonce,
	const String& method, const String& uri, const String& response, const SIPMessage* message);
    inline bool prack() const
	{ return m_prack; }
private:
    YateSIPEndPoint* m_ep;
    bool m_prack;
};

class YateSIPEndPoint : public Thread
{
public:
    YateSIPEndPoint();
    ~YateSIPEndPoint();
    bool Init(void);
    void run(void);
    bool incoming(SIPEvent* e, SIPTransaction* t);
    void invite(SIPEvent* e, SIPTransaction* t);
    void regreq(SIPEvent* e, SIPTransaction* t);
    bool buildParty(SIPMessage* message, const char* host = 0, int port = 0);
    inline YateSIPEngine* engine() const
	{ return m_engine; }
    inline int port() const
	{ return m_port; }
    inline Socket* socket() const
	{ return m_sock; }
private:
    int m_port;
    Socket* m_sock;
    SocketAddr m_addr;
    YateSIPEngine *m_engine;

};

class YateSIPLine : public String
{
    YCLASS(YateSIPLine,String)
public:
    YateSIPLine(const String& name);
    virtual ~YateSIPLine();
    SIPAuthLine* buildAuth(const SIPMessage* answer, const String& method,
	const String& uri, bool proxy = false) const;
    SIPMessage* buildRegister(int expires, const SIPMessage* msg) const;
    void login(const SIPMessage* msg = 0);
    void logout();
    bool process(SIPEvent* ev);
    void timer(const Time& when);
    bool update(const Message& msg);
    inline const String& domain() const
	{ return m_domain ? m_domain : m_registrar; }
    inline bool valid() const
	{ return m_valid; }
    inline bool marked() const
	{ return m_marked; }
    inline void marked(bool mark)
	{ m_marked = mark; }
private:
    void clearTransaction();
    bool change(String& dest, const String& src);
    String m_registrar;
    String m_username;
    String m_password;
    String m_outbound;
    String m_domain;
    String m_display;
    Time m_resend;
    int m_interval;
    bool m_retry;
    SIPTransaction* m_tr;
    bool m_marked;
    bool m_valid;
};

class YateSIPConnection : public Channel
{
    YCLASS(YateSIPConnection,Channel)
public:
    enum {
	Incoming = 0,
	Outgoing = 1,
	Ringing = 2,
	Established = 3,
	Cleared = 4,
    };
    YateSIPConnection(SIPEvent* ev, SIPTransaction* tr);
    YateSIPConnection(Message& msg, const String& uri, const char* target = 0);
    ~YateSIPConnection();
    virtual void disconnected(bool final, const char *reason);
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    void startRouter();
    bool process(SIPEvent* ev);
    bool checkUser(SIPTransaction* t, bool refuse = true);
    void doBye(SIPTransaction* t);
    void doCancel(SIPTransaction* t);
    void reInvite(SIPTransaction* t);
    void hangup();
    inline const SIPDialog& dialog() const
	{ return m_dialog; }
    inline void setStatus(const char *status, int state = -1)
	{ m_status = status; if (state >= 0) m_state = state; }
    inline void setReason(const char* str = "Request Terminated", int code = 487)
	{ m_reason = str; m_reasonCode = code; }
    inline SIPTransaction* getTransaction() const
	{ return m_tr; }
    inline const String& callid() const
	{ return m_callid; }
    inline const String& user() const
	{ return m_user; }
    inline const String& getHost() const
	{ return m_host; }
    inline int getPort() const
	{ return m_port; }
private:
    void clearTransaction();
    SIPMessage* createDlgMsg(const char* method, const char* uri = 0);
    bool emitPRACK(const SIPMessage* msg);
    SDPBody* createSDP(const char* addr, const char* port, const char* formats, const char* format = 0);
    SDPBody* createProvisionalSDP(Message &msg);
    SDPBody* createPasstroughSDP(Message &msg);
    SDPBody* createRtpSDP(SIPMessage* msg, const char* formats);
    SDPBody* createRtpSDP(bool start = false);
    bool startRtp();
    bool addRtpParams(Message& msg, const String& natAddr = String::empty());

    SIPTransaction* m_tr;
    bool m_hungup;
    bool m_byebye;
    bool m_retry;
    int m_state;
    String m_reason;
    int m_reasonCode;
    String m_callid;
    // SIP dialog of this call, used for re-INVITE or BYE
    SIPDialog m_dialog;
    URI m_uri;
    // if we do RTP forwarding or not
    bool m_rtpForward;
    // id of the local RTP channel
    String m_rtpid;
    // remote RTP address
    String m_rtpAddr;
    // remote RTP port
    String m_rtpPort;
    // format used for sending data
    String m_rtpFormat;
    // local RTP address
    String m_rtpLocalAddr;
    // local RTP port
    String m_rtpLocalPort;
    // unique SDP session number
    int m_sdpSession;
    // SDP version number, incremented each time we generate a new SDP
    int m_sdpVersion;
    String m_formats;
    String m_host;
    String m_user;
    String m_line;
    int m_port;
    Message* m_route;
    ObjList* m_routes;
    bool m_authBye;
};

class UserHandler : public MessageHandler
{
public:
    UserHandler()
	: MessageHandler("user.login",150)
	{ }
    virtual bool received(Message &msg);
};

class SIPDriver : public Driver
{
public:
    SIPDriver();
    ~SIPDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool received(Message &msg, int id);
    inline YateSIPEndPoint* ep() const
	{ return m_endpoint; }
    YateSIPConnection* findCall(const String& callid);
    YateSIPConnection* findDialog(const SIPDialog& dialog);
    YateSIPLine* findLine(const String& line);
    bool validLine(const String& line);
private:
    YateSIPEndPoint *m_endpoint;
};

static SIPDriver plugin;
static ObjList s_lines;
static Configuration s_cfg;
static int s_maxForwards = 20;

static void parseSDP(SDPBody* sdp, String& addr, String& port, String& formats, const char* media = "audio")
{
    const NamedString* c = sdp->getLine("c");
    if (c) {
	String tmp(*c);
	if (tmp.startSkip("IN IP4")) {
	    tmp.trimBlanks();
	    // Handle the case media is muted
	    if (tmp == "0.0.0.0")
		tmp.clear();
	    addr = tmp;
	}
    }
    c = sdp->getLine("m");
    while (c) {
	String tmp(*c);
	if (tmp.startSkip(media)) {
	    int var = 0;
	    tmp >> var >> " RTP/AVP";
	    if (var > 0)
		port = var;
	    String fmt;
	    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
	    while (tmp[0] == ' ') {
		var = -1;
		tmp >> " " >> var;
		const char* payload = lookup(var,dict_payloads);
		if (payload && s_cfg.getBoolValue("codecs",payload,defcodecs && DataTranslator::canConvert(payload))) {
		    if (fmt)
			fmt << ",";
		    fmt << payload;
		}
	    }
	    formats = fmt;
	    return;
	}
	c = sdp->getNextLine(c);
    }
}

static bool isPrivateAddr(const String& host)
{
    if (host.startsWith("192.168.") || host.startsWith("169.254.") || host.startsWith("10."))
	return true;
    String s(host);
    if (!s.startSkip("172.",false))
	return false;
    int i = 0;
    s >> i;
    return (i >= 16) && (i <= 31) && s.startsWith(".");
}

YateUDPParty::YateUDPParty(Socket* sock, const SocketAddr& addr, int local)
    : m_sock(sock), m_addr(addr)
{
    m_local = "localhost";
    m_localPort = local;
    m_party = m_addr.host();
    m_partyPort = m_addr.port();
    Socket s(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (s.valid()) {
	if (s.connect(m_addr)) {
	    SocketAddr laddr;
	    if (s.getSockName(laddr))
		m_local = laddr.host();
	}
    }
    DDebug(&plugin,DebugAll,"YateUDPParty local %s:%d party %s:%d",
	m_local.c_str(),m_localPort,
	m_party.c_str(),m_partyPort);
}

YateUDPParty::~YateUDPParty()
{
    m_sock = 0;
}

void YateUDPParty::transmit(SIPEvent* event)
{
    const SIPMessage* msg = event->getMessage();
    if (!msg)
	return;
    String tmp;
    if (msg->isAnswer())
	tmp << "code " << msg->code;
    else
	tmp << "'" << msg->method << " " << msg->uri << "'";
    Debug(&plugin,DebugInfo,"Sending %s %p to %s:%d",
	tmp.c_str(),msg,m_addr.host().c_str(),m_addr.port());
    m_sock->sendTo(
	msg->getBuffer().data(),
	msg->getBuffer().length(),
	m_addr
    );
}

const char* YateUDPParty::getProtoName() const
{
    return "UDP";
}

bool YateUDPParty::setParty(const URI& uri)
{
    if (m_partyPort && m_party && s_cfg.getBoolValue("general","ignorevia"))
	return true;
    if (uri.getHost().null())
	return false;
    int port = uri.getPort();
    if (port <= 0)
	port = 5060;
    if (!m_addr.host(uri.getHost())) {
	Debug(&plugin,DebugWarn,"Could not resolve UDP party name '%s' [%p]",
	    uri.getHost().safe(),this);
	return false;
    }
    m_addr.port(port);
    m_party = uri.getHost();
    m_partyPort = port;
    DDebug(&plugin,DebugInfo,"New UDP party is %s:%d (%s:%d) [%p]",
	m_party.c_str(),m_partyPort,
	m_addr.host().c_str(),m_addr.port(),
	this);
    return true;
}

YateSIPEngine::YateSIPEngine(YateSIPEndPoint* ep)
    : SIPEngine(s_cfg.getValue("general","useragent")),
      m_ep(ep), m_prack(false)
{
    addAllowed("INVITE");
    addAllowed("BYE");
    addAllowed("CANCEL");
    if (s_cfg.getBoolValue("general","registrar"))
	addAllowed("REGISTER");
    m_prack = s_cfg.getBoolValue("general","prack");
    if (m_prack)
	addAllowed("PRACK");
}

bool YateSIPEngine::buildParty(SIPMessage* message)
{
    return m_ep->buildParty(message);
}

bool YateSIPEngine::checkUser(const String& username, const String& realm, const String& nonce,
    const String& method, const String& uri, const String& response, const SIPMessage* message)
{
    Message m("user.auth");
    m.addParam("username",username);
    m.addParam("realm",realm);
    m.addParam("nonce",nonce);
    m.addParam("method",method);
    m.addParam("uri",uri);
    m.addParam("response",response);
    if (message) {
	m.addParam("ip_addr",message->getParty()->getPartyAddr());
	m.addParam("ip_port",String(message->getParty()->getPartyPort()));
    }
    
    if (!Engine::dispatch(m))
	return false;
    // FIXME: deal with empty passwords or just disallow them
    if (m.retValue().null())
	return true;
    String res;
    buildAuth(username,realm,m.retValue(),nonce,method,uri,res);
    return (res == response);
}

YateSIPEndPoint::YateSIPEndPoint()
    : Thread("YSIP EndPoint"), m_sock(0), m_engine(0)
{
    Debug(&plugin,DebugAll,"YateSIPEndPoint::YateSIPEndPoint() [%p]",this);
}

YateSIPEndPoint::~YateSIPEndPoint()
{
    Debug(&plugin,DebugAll,"YateSIPEndPoint::~YateSIPEndPoint() [%p]",this);
    plugin.channels().clear();
    s_lines.clear();
    if (m_engine) {
	// send any pending events
	while (m_engine->process())
	    ;
	delete m_engine;
	m_engine = 0;
    }
    if (m_sock) {
	delete m_sock;
	m_sock = 0;
    }
}

bool YateSIPEndPoint::buildParty(SIPMessage* message, const char* host, int port)
{
    if (message->isAnswer())
	return false;
    URI uri(message->uri);
    if (!host) {
	host = uri.getHost().safe();
	if (port <= 0)
	    port = uri.getPort();
    }
    if (port <= 0)
	port = 5060;
    SocketAddr addr(AF_INET);
    if (!addr.host(host)) {
	Debug(&plugin,DebugWarn,"Error resolving name '%s'",host);
	return false;
    }
    addr.port(port);
    DDebug(&plugin,DebugAll,"built addr: %s:%d",
	addr.host().c_str(),addr.port());
    message->setParty(new YateUDPParty(m_sock,addr,m_port));
    return true;
}

bool YateSIPEndPoint::Init()
{
    /*
     * This part have been taken from libiax after i have lost my sip driver for bayonne
     */
    if (m_sock) {
	Debug(&plugin,DebugInfo,"Already initialized.");
	return true;
    }

    m_sock = new Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!m_sock->valid()) {
	Debug(DebugGoOn,"Unable to allocate UDP socket");
	return false;
    }
    
    SocketAddr addr(AF_INET);
    addr.port(s_cfg.getIntValue("general","port",5060));
    addr.host(s_cfg.getValue("general","addr","0.0.0.0"));

    if (!m_sock->bind(addr)) {
	Debug(DebugWarn,"Unable to bind to preferred port - using random one instead");
	addr.port(0);
	if (!m_sock->bind(addr)) {
	    Debug(DebugGoOn,"Unable to bind to any port");
	    return false;
	}
    }
    
    if (!m_sock->getSockName(addr)) {
	Debug(DebugGoOn,"Unable to figure out what I'm bound to");
	return false;
    }
    if (!m_sock->setBlocking(false)) {
	Debug(DebugGoOn,"Unable to set non-blocking mode");
	return false;
    }
    Debug(DebugInfo,"SIP Started on %s:%d", addr.host().safe(), addr.port());
    m_port = addr.port();
    m_engine = new YateSIPEngine(this);
    return true;
}

void YateSIPEndPoint::run()
{
    struct timeval tv;
    char buf[1500];
    /* Watch stdin (fd 0) to see when it has input. */
    for (;;)
    {
	/* Wait up to 5000 microseconds. */
	tv.tv_sec = 0;
	tv.tv_usec = 5000;
	bool ok = false;
	m_sock->select(&ok,0,0,&tv);
	if (ok)
	{
	    // we can read the data
	    int res = m_sock->recvFrom(buf,sizeof(buf)-1,m_addr);
	    if (res <= 0) {
		if (!m_sock->canRetry()) {
		    Debug(DebugGoOn,"SIP error on read: %d", m_sock->error());
		}
	    } else if (res >= 72) {
		Debug(&plugin,DebugInfo,"Received %d bytes SIP message from %s:%d",
		    res,m_addr.host().c_str(),m_addr.port());
		// we got already the buffer and here we start to do "good" stuff
		buf[res]=0;
		m_engine->addMessage(new YateUDPParty(m_sock,m_addr,m_port),buf,res);
	    }
#ifdef DEBUG
	    else
		Debug(&plugin,DebugInfo,"Received short SIP message of %d bytes",res);
#endif
	}
	else
	    Thread::check();
	SIPEvent* e = m_engine->getEvent();
	// hack: use a loop so we can use break and continue
	for (; e; m_engine->processEvent(e),e = 0) {
	    if (!e->getTransaction())
		continue;
	    plugin.lock();
	    GenObject* obj = static_cast<GenObject*>(e->getTransaction()->getUserData());
	    YateSIPConnection* conn = YOBJECT(YateSIPConnection,obj);
	    YateSIPLine* line = YOBJECT(YateSIPLine,obj);
	    if (conn && (conn->refcount() > 0))
		conn->ref();
	    else
		conn = 0;
	    plugin.unlock();
	    if (conn) {
		if (conn->process(e)) {
		    delete e;
		    conn->deref();
		    break;
		}
		else {
		    conn->deref();
		    continue;
		}
	    }
	    if (line) {
		if (line->process(e)) {
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
    if (t->isInvite())
	invite(e,t);
    else if (t->getMethod() == "BYE") {
	YateSIPConnection* conn = plugin.findCall(t->getCallID());
	if (conn)
	    conn->doBye(t);
	else
	    t->setResponse(481);
    }
    else if (t->getMethod() == "CANCEL") {
	YateSIPConnection* conn = plugin.findCall(t->getCallID());
	if (conn)
	    conn->doCancel(t);
	else
	    t->setResponse(481);
    }
    else if (t->getMethod() == "REGISTER")
	regreq(e,t);
    else
	return false;
    return true;
}

void YateSIPEndPoint::invite(SIPEvent* e, SIPTransaction* t)
{
    if (!plugin.canAccept()) {
	Debug(DebugWarn,"Refusing new SIP call, full or exiting");
	t->setResponse(480);
	return;
    }

    if (e->getMessage()->getParam("To","tag")) {
	SIPDialog dlg(*e->getMessage());
	YateSIPConnection* conn = plugin.findDialog(dlg);
	if (conn)
	    conn->reInvite(t);
	else {
	    Debug(DebugWarn,"Got re-INVITE for missing dialog");
	    t->setResponse(481);
	}
	return;
    }

    YateSIPConnection* conn = new YateSIPConnection(e,t);
    conn->startRouter();

}

void YateSIPEndPoint::regreq(SIPEvent* e, SIPTransaction* t)
{
    if (Engine::exiting()) {
	Debug(&plugin,DebugWarn,"Dropping request, engine is exiting");
	t->setResponse(500, "Server Shutting Down");
	return;
    }
    const SIPHeaderLine* hl = e->getMessage()->getHeader("Contact");
    if (!hl) {
	t->setResponse(400);
	return;
    }

    String user;
    int age = t->authUser(user);
    DDebug(&plugin,DebugAll,"User '%s' age %d",user.c_str(),age);
    if ((age < 0) || (age > 10)) {
	t->requestAuth("realm","domain",age > 0);
	return;
    }

    URI addr(*hl);
    Message *m = new Message("user.register");
    m->addParam("username",addr.getUser());
    m->addParam("driver","sip");
    m->addParam("data","sip/" + addr);
    bool dereg = false;
    hl = e->getMessage()->getHeader("Expires");
    if (hl) {
	m->addParam("expires",*hl);
	if (*hl == "0") {
	    *m = "user.unregister";
	    dereg = true;
	}
    }
    // Always OK deregistration attempts
    if (Engine::dispatch(m) || dereg)
	t->setResponse(200);
    else
	t->setResponse(404);
    m->destruct();
}

// Incoming call constructor - just before starting the routing thread
YateSIPConnection::YateSIPConnection(SIPEvent* ev, SIPTransaction* tr)
    : Channel(plugin,0,false),
      m_tr(tr), m_hungup(false), m_byebye(true), m_retry(false),
      m_state(Incoming), m_rtpForward(false),
      m_sdpSession(0), m_sdpVersion(0), m_port(0), m_route(0), m_routes(0),
      m_authBye(true)
{
    Debug(this,DebugAll,"YateSIPConnection::YateSIPConnection(%p,%p) [%p]",ev,tr,this);
    setReason();
    m_tr->ref();
    m_routes = m_tr->initialMessage()->getRoutes();
    m_callid = m_tr->getCallID();
    m_dialog = *m_tr->initialMessage();
    m_host = m_tr->initialMessage()->getParty()->getPartyAddr();
    m_port = m_tr->initialMessage()->getParty()->getPartyPort();
    m_address << m_host << ":" << m_port;
    m_uri = m_tr->initialMessage()->getHeader("From");
    m_uri.parse();
    m_tr->setUserData(this);

    URI uri(m_tr->getURI());
    Message *m = message("call.route");

    String user;
    int age = tr->authUser(user);
    DDebug(this,DebugAll,"User '%s' age %d",user.c_str(),age);
    if (age >= 0) {
	if (age < 10) {
	    m_user = user;
	    m->addParam("username",m_user);
	}
	else
	    m->addParam("expired_user",user);
	m->addParam("xsip_nonce_age",String(age));
    }

    m->addParam("caller",m_uri.getUser());
    m->addParam("called",uri.getUser());
    String tmp(ev->getMessage()->getHeaderValue("Max-Forwards"));
    int maxf = tmp.toInteger(s_maxForwards);
    if (maxf > s_maxForwards)
	maxf = s_maxForwards;
    tmp = maxf-1;
    m->addParam("antiloop",tmp);
    m->addParam("ip_addr",m_host);
    m->addParam("ip_port",String(m_port));
    m->addParam("sip_uri",uri);
    m->addParam("sip_from",m_uri);
    m->addParam("sip_callid",m_callid);
    m->addParam("sip_contact",ev->getMessage()->getHeaderValue("Contact"));
    m->addParam("sip_user-agent",ev->getMessage()->getHeaderValue("User-Agent"));
    if (ev->getMessage()->body && ev->getMessage()->body->isSDP()) {
	parseSDP(static_cast<SDPBody*>(ev->getMessage()->body),m_rtpAddr,m_rtpPort,m_formats);
	if (m_rtpAddr) {
	    m_rtpForward = true;
	    // guess if the call comes from behind a NAT
	    if (s_cfg.getBoolValue("general","nat",true) && isPrivateAddr(m_rtpAddr) && !isPrivateAddr(m_host)) {
		Debug(this,DebugInfo,"NAT detected: private '%s' public '%s' port %s",
		    m_rtpAddr.c_str(),m_host.c_str(),m_rtpPort.c_str());
		m->addParam("rtp_nat_addr",m_rtpAddr);
		m_rtpAddr = m_host;
	    }
	    m->addParam("rtp_forward","possible");
	    m->addParam("rtp_addr",m_rtpAddr);
	    m->addParam("rtp_port",m_rtpPort);
	    m->addParam("formats",m_formats);
	}
	int q = m_formats.find(',');
	m_rtpFormat = m_formats.substr(0,q);
    }
    DDebug(this,DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
    m_route = m;
    Engine::enqueue(message("chan.startup"));
}

// Outgoing call constructor - in call.execute handler
YateSIPConnection::YateSIPConnection(Message& msg, const String& uri, const char* target)
    : Channel(plugin,0,true),
      m_tr(0), m_hungup(false), m_byebye(true), m_retry(true),
      m_state(Outgoing), m_rtpForward(false),
      m_sdpSession(0), m_sdpVersion(0), m_port(0), m_route(0), m_routes(0),
      m_authBye(false)
{
    Debug(this,DebugAll,"YateSIPConnection::YateSIPConnection(%p,'%s') [%p]",
	&msg,uri.c_str(),this);
    m_targetid = target;
    setReason();
    m_rtpForward = msg.getBoolValue("rtp_forward");
    m_line = msg.getValue("line");
    String tmp;
    if (m_line && (uri.find('@') < 0)) {
	YateSIPLine* line = plugin.findLine(m_line);
	if (line) {
	    if (!uri.startsWith("sip:"))
		tmp = "sip:";
	    tmp << uri << "@" << line->domain();
	}
    }
    if (tmp.null())
	tmp = uri;
    m_uri = tmp;
    m_uri.parse();
    SIPMessage* m = new SIPMessage("INVITE",m_uri);
    plugin.ep()->buildParty(m,msg.getValue("host"),msg.getIntValue("port"));
    if (!m->getParty()) {
	Debug(this,DebugWarn,"Could not create party for '%s' [%p]",m_uri.c_str(),this);
	m->destruct();
	tmp = "Invalid address: ";
	tmp << m_uri;
	msg.setParam("reason",tmp);
	setReason(tmp);
	return;
    }
    int maxf = msg.getIntValue("antiloop",s_maxForwards);
    m->addHeader("Max-Forwards",String(maxf));
    m->complete(plugin.ep()->engine(),msg.getValue("caller"),msg.getValue("domain"));
    if (plugin.ep()->engine()->prack())
	m->addHeader("Supported","100rel");
    m_host = m->getParty()->getPartyAddr();
    m_port = m->getParty()->getPartyPort();
    m_address << m_host << ":" << m_port;
    m_dialog = *m;
    SDPBody* sdp = createPasstroughSDP(msg);
    if (!sdp)
	sdp = createRtpSDP(m,msg.getValue("formats"));
    m->setBody(sdp);
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_callid = m_tr->getCallID();
	m_tr->setUserData(this);
    }
    m->deref();
    Engine::enqueue(message("chan.startup"));
}

YateSIPConnection::~YateSIPConnection()
{
    Debug(this,DebugAll,"YateSIPConnection::~YateSIPConnection() [%p]",this);
    hangup();
    clearTransaction();
    if (m_route) {
	delete m_route;
	m_route = 0;
    }
    if (m_routes) {
	delete m_routes;
	m_routes = 0;
    }
}

void YateSIPConnection::startRouter()
{
    Message* m = m_route;
    m_route = 0;
    Channel::startRouter(m);
}

void YateSIPConnection::clearTransaction()
{
    if (m_tr) {
	if (driver())
	    driver()->lock();
	m_tr->setUserData(0);
	if (m_tr->isIncoming()) {
	    if (m_tr->setResponse(m_reasonCode,m_reason.null() ? "Request Terminated" : m_reason.c_str()))
		m_byebye = false;
	}
	m_tr->deref();
	m_tr = 0;
	if (driver())
	    driver()->unlock();
    }
}

void YateSIPConnection::hangup()
{
    if (m_hungup)
	return;
    m_hungup = true;
    const char* error = lookup(m_reasonCode,dict_errors);
    Debug(this,DebugAll,"YateSIPConnection::hangup() state=%d trans=%p error='%s' code=%d reason='%s' [%p]",
	m_state,m_tr,error,m_reasonCode,m_reason.c_str(),this);
    Message* m = message("chan.hangup");
    if (m_reason)
	m->addParam("reason",m_reason);
    Engine::enqueue(m);
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
	case Ringing:
	    if (m_tr) {
		SIPMessage* m = new SIPMessage("CANCEL",m_uri);
		plugin.ep()->buildParty(m,m_host,m_port);
		if (!m->getParty())
		    Debug(this,DebugWarn,"Could not create party for '%s:%d' [%p]",
			m_host.c_str(),m_port,this);
		else {
		    const SIPMessage* i = m_tr->initialMessage();
		    m->copyHeader(i,"Via");
		    m->copyHeader(i,"From");
		    m->copyHeader(i,"To");
		    m->copyHeader(i,"Call-ID");
		    String tmp;
		    tmp << i->getCSeq() << " CANCEL";
		    m->addHeader("CSeq",tmp);
		    plugin.ep()->engine()->addMessage(m);
		}
		m->deref();
	    }
	    break;
    }
    clearTransaction();
    m_state = Cleared;

    if (m_byebye) {
	m_byebye = false;
	SIPMessage* m = createDlgMsg("BYE");
	if (m) {
	    if (m_reason) {
		// FIXME: add SIP and Q.850 cause codes, set the proper reason
		SIPHeaderLine* hl = new SIPHeaderLine("Reason","SIP");
		hl->setParam("text","\"" + m_reason + "\"");
		m->addHeader(hl);
	    }
	    plugin.ep()->engine()->addMessage(m);
	    m->deref();
	}
    }
    if (!error)
	error = m_reason.c_str();
    disconnect(error);
}

// Creates a new message in an existing dialog
SIPMessage* YateSIPConnection::createDlgMsg(const char* method, const char* uri)
{
    if (!uri)
	uri = m_uri;
    SIPMessage* m = new SIPMessage(method,uri);
    m->addRoutes(m_routes);
    plugin.ep()->buildParty(m,m_host,m_port);
    if (!m->getParty()) {
	Debug(this,DebugWarn,"Could not create party for '%s:%d' [%p]",
	    m_host.c_str(),m_port,this);
	m->destruct();
	return 0;
    }
    m->addHeader("Call-ID",m_callid);
    String tmp;
    tmp << "<" << m_dialog.localURI << ">";
    SIPHeaderLine* hl = new SIPHeaderLine("From",tmp);
    tmp = m_dialog.localTag;
    if (tmp.null() && m_tr)
	tmp = m_tr->getDialogTag();
    if (tmp)
	hl->setParam("tag",tmp);
    m->addHeader(hl);
    tmp.clear();
    tmp << "<" << m_dialog.remoteURI << ">";
    hl = new SIPHeaderLine("To",tmp);
    tmp = m_dialog.remoteTag;
    if (tmp.null() && m_tr)
	tmp = m_tr->getDialogTag();
    if (tmp)
	hl->setParam("tag",tmp);
    m->addHeader(hl);
    if (m_tr && m_tr->initialMessage())
	m->copyHeader(m_tr->initialMessage(),"Contact");
    return m;
}

// Emit a PRovisional ACK if enabled in the engine
bool YateSIPConnection::emitPRACK(const SIPMessage* msg)
{
    if (!plugin.ep()->engine()->prack())
	return false;
    if (!(msg && msg->isAnswer() && (msg->code > 100) && (msg->code < 200)))
	return false;
    const SIPHeaderLine* rs = msg->getHeader("RSeq");
    const SIPHeaderLine* cs = msg->getHeader("CSeq");
    if (!(rs && cs))
	return false;
    String tmp;
    const SIPHeaderLine* co = msg->getHeader("Contact");
    if (co) {
	tmp = *co;
	Regexp r("^[^<]*<\\([^>]*\\)>.*$");
	if (tmp.matches(r))
	    tmp = tmp.matchString(1);
    }
    SIPMessage* m = createDlgMsg("PRACK",tmp);
    if (!m)
	return false;
    tmp = *rs;
    tmp << " " << *cs;
    m->addHeader("RAck",tmp);
    plugin.ep()->engine()->addMessage(m);
    m->deref();
    return true;
}

// Creates a SDP for provisional (1xx) messages
SDPBody* YateSIPConnection::createProvisionalSDP(Message &msg)
{
    if (m_rtpForward)
	return createPasstroughSDP(msg);
    // check if our peer can source data
    if (!(getPeer() && getPeer()->getSource()))
	return 0;
    if (m_rtpAddr.null())
	return 0;
    return createRtpSDP(true);
}

// Creates a SDP from RTP address data present in message
SDPBody* YateSIPConnection::createPasstroughSDP(Message &msg)
{
    String tmp = msg.getValue("rtp_forward");
    msg.clearParam("rtp_forward");
    if (!(m_rtpForward && tmp.toBoolean()))
	return 0;
    SDPBody* sdp = 0;
    tmp = msg.getValue("rtp_port");
    int port = tmp.toInteger();
    String addr(msg.getValue("rtp_addr"));
    if (port && addr) {
	m_rtpLocalAddr = addr;
	m_rtpLocalPort = tmp;
	sdp = createSDP(addr,tmp,msg.getValue("formats"));
    }
    if (sdp)
	msg.setParam("rtp_forward","accepted");
    return sdp;
}

// Creates an unstarted external RTP channel from remote addr and builds SDP from it
SDPBody* YateSIPConnection::createRtpSDP(SIPMessage* msg, const char* formats)
{
    Message m("chan.rtp");
    complete(m,true);
    m.addParam("direction","bidir");
    m.addParam("remoteip",msg->getParty()->getPartyAddr());
    m.userData(static_cast<CallEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpForward = false;
	m_rtpid = m.getValue("rtpid");
	m_rtpLocalAddr = m.getValue("localip",m_rtpLocalAddr);
	m_rtpLocalPort = m.getValue("localport",m_rtpLocalPort);
	return createSDP(m_rtpLocalAddr,m_rtpLocalPort,formats);
    }
    return 0;
}

// Creates a started external RTP channel from remote addr and builds SDP from it
SDPBody* YateSIPConnection::createRtpSDP(bool start)
{
    if (m_rtpAddr.null()) {
	m_rtpid = "-";
	return createSDP(0,m_rtpLocalPort,m_formats);
    }
    Message m("chan.rtp");
    complete(m,true);
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    if (start) {
	m.addParam("remoteport",m_rtpPort);
	m.addParam("format",m_rtpFormat);
    }
    m.userData(static_cast<CallEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpForward = false;
	m_rtpid = m.getValue("rtpid");
	m_rtpLocalAddr = m.getValue("localip",m_rtpLocalAddr);
	m_rtpLocalPort = m.getValue("localport",m_rtpLocalPort);
	if (start)
	    m_rtpFormat = m.getValue("format");
	return createSDP(m_rtpLocalAddr,m_rtpLocalPort,m_formats,m_rtpFormat);
    }
    return 0;
}

// Starts an already created external RTP channel
bool YateSIPConnection::startRtp()
{
    if (m_rtpid.null() || m_rtpid == "-")
	return false;
    DDebug(this,DebugAll,"YateSIPConnection::startRtp() [%p]",this);
    Message m("chan.rtp");
    complete(m,true);
    m.addParam("rtpid",m_rtpid);
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    m.addParam("remoteport",m_rtpPort);
    m.addParam("format",m_rtpFormat);
    m.userData(static_cast<CallEndpoint *>(this));
    return Engine::dispatch(m);
}

// Creates a SDP body from transport address and list of formats
SDPBody* YateSIPConnection::createSDP(const char* addr, const char* port, const char* formats, const char* format)
{
    DDebug(this,DebugAll,"YateSIPConnection::createSDP('%s','%s','%s') [%p]",
	addr,port,formats,this);
    // if we got no port we simply create no SDP
    if (!port)
	return 0;
    if (m_sdpSession)
	++m_sdpVersion;
    else
	m_sdpVersion = m_sdpSession = Time::secNow();
    String owner;
    owner << "yate " << m_sdpSession << " " << m_sdpVersion << " IN IP4 " << addr;
    // no address means on hold or muted
    if (!addr)
	addr = "0.0.0.0";
    String tmp;
    tmp << "IN IP4 " << addr;
    String frm(format ? format : formats);
    if (frm.null())
	frm = "alaw,mulaw";
    ObjList* l = frm.split(',',false);
    frm = "audio ";
    frm << port << " RTP/AVP";
    ObjList rtpmap;
    ObjList* f = l;
    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
    for (; f; f = f->next()) {
	String* s = static_cast<String*>(f->get());
	if (s) {
	    int payload = s->toInteger(dict_payloads,-1);
	    if (payload >= 0) {
		const char* map = lookup(payload,dict_rtpmap);
		if (map && s_cfg.getBoolValue("codecs",*s,defcodecs && DataTranslator::canConvert(*s))) {
		    frm << " " << payload;
		    String* temp = new String("rtpmap:");
		    *temp << payload << " " << map;
		    rtpmap.append(temp);
		}
	    }
	}
    }
    delete l;

    // always claim to support telephone events
    frm << " 101";
    rtpmap.append(new String("rtpmap:101 telephone-event/8000"));

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

// Add RTP forwarding parameters to a message
bool YateSIPConnection::addRtpParams(Message& msg, const String& natAddr)
{
    if (m_rtpPort && m_rtpAddr && !startRtp() && m_rtpForward) {
	if (natAddr)
	    msg.addParam("rtp_nat_addr",natAddr);
	msg.addParam("rtp_forward","yes");
	msg.addParam("rtp_addr",m_rtpAddr);
	msg.addParam("rtp_port",m_rtpPort);
	msg.addParam("formats",m_formats);
	return true;
    }
    return false;
}

// Process SIP events belonging to this connection
bool YateSIPConnection::process(SIPEvent* ev)
{
    DDebug(this,DebugInfo,"YateSIPConnection::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    m_dialog = *ev->getTransaction()->recentMessage();
    const SIPMessage* msg = ev->getMessage();
    if (msg && !msg->isOutgoing() && msg->isAnswer() && (msg->code >= 300)) {
	if (m_retry && m_line
	    && ((msg->code == 401) || (msg->code == 407))
	    && plugin.validLine(m_line)) {
	    // try only once to add credentials
	    m_retry = false;
	    YateSIPLine* line = plugin.findLine(m_line);
	    if (line) {
		SIPMessage* m = new SIPMessage(*m_tr->initialMessage());
		SIPAuthLine* auth = line->buildAuth(msg,m->method,m->uri,(msg->code == 407));
		m->addHeader(auth);

		m_tr->setUserData(0);
		m_tr->deref();
		m_tr = 0;

		m_tr = plugin.ep()->engine()->addMessage(m);
		m->deref();
		if (m_tr) {
		    m_tr->ref();
		    m_callid = m_tr->getCallID();
		    m_tr->setUserData(this);
		}
		else
		    setReason("Internal server failure",500);
		return false;
	    }
	}
	setReason(msg->reason,msg->code);
	hangup();
    }
    if (ev->getState() == SIPTransaction::Cleared) {
	if (m_tr) {
	    DDebug(this,DebugInfo,"YateSIPConnection clearing transaction %p [%p]",
		m_tr,this);
	    m_tr->setUserData(0);
	    m_tr->deref();
	    m_tr = 0;
	}
	if (m_state != Established)
	    hangup();
	return false;
    }
    if (!msg || msg->isOutgoing())
	return false;
    String natAddr;
    if (msg->body && msg->body->isSDP()) {
	DDebug(this,DebugInfo,"YateSIPConnection got SDP [%p]",this);
	parseSDP(static_cast<SDPBody*>(msg->body),
	    m_rtpAddr,m_rtpPort,m_formats);
	// guess if the call comes from behind a NAT
	if (s_cfg.getBoolValue("general","nat",true) && isPrivateAddr(m_rtpAddr) && !isPrivateAddr(m_host)) {
	    Debug(this,DebugInfo,"NAT detected: private '%s' public '%s' port %s",
		m_rtpAddr.c_str(),m_host.c_str(),m_rtpPort.c_str());
	    natAddr = m_rtpAddr;
	    m_rtpAddr = m_host;
	}
	int q = m_formats.find(',');
	m_rtpFormat = m_formats.substr(0,q);
	DDebug(this,DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	    m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
    }
    if ((!m_routes) && msg->isAnswer() && (msg->code > 100) && (msg->code < 300))
	m_routes = msg->getRoutes();
    if (msg->isAnswer() && ((msg->code / 100) == 2)) {
	const SIPMessage* ack = m_tr->latestMessage();
	if (ack && ack->isACK()) {
	    m_uri = ack->uri;
	    m_uri.parse();
	}
	setStatus("answered",Established);
	Message *m = message("call.answered");
	addRtpParams(*m,natAddr);
	Engine::enqueue(m);
    }
    if ((m_state < Ringing) && msg->isAnswer()) {
	if (msg->code == 180) {
	    setStatus("ringing",Ringing);
	    Message *m = message("call.ringing");
	    addRtpParams(*m,natAddr);
	    Engine::enqueue(m);
	}
	if (msg->code == 183) {
	    setStatus("progressing");
	    Message *m = message("call.progress");
	    addRtpParams(*m,natAddr);
	    Engine::enqueue(m);
	}
	if ((msg->code > 100) && (msg->code < 200))
	    emitPRACK(msg);
    }
    if (msg->isACK()) {
	DDebug(this,DebugInfo,"YateSIPConnection got ACK [%p]",this);
	startRtp();
    }
    return false;
}

void YateSIPConnection::reInvite(SIPTransaction* t)
{
    if (!checkUser(t))
	return;
    DDebug(this,DebugAll,"YateSIPConnection::reInvite(%p) [%p]",t,this);
    // hack: use a while instead of if so we can return or break out of it
    while (t->initialMessage()->body && t->initialMessage()->body->isSDP()) {
	// accept re-INVITE only for local RTP, not for pass-trough
	if (m_rtpForward || m_rtpid.null())
	    break;
	String addr,port,formats;
	parseSDP(static_cast<SDPBody*>(t->initialMessage()->body),addr,port,formats);
	int q = formats.find(',');
	String frm = formats.substr(0,q);
	if (port.null() || frm.null())
	    break;
	m_rtpAddr = addr;
	m_rtpPort = port;
	m_rtpFormat = frm;
	m_formats = formats;
	// guess if the call comes from behind a NAT
	if (s_cfg.getBoolValue("general","nat",true) && isPrivateAddr(m_rtpAddr) && !isPrivateAddr(m_host)) {
	    Debug(this,DebugInfo,"NAT detected: private '%s' public '%s' port %s",
		m_rtpAddr.c_str(),m_host.c_str(),m_rtpPort.c_str());
	    m_rtpAddr = m_host;
	}
	Debug(this,DebugAll,"New RTP addr '%s' port %s formats '%s' format '%s'",
	    m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());

	m_rtpid.clear();
	setSource();
	setConsumer();

	SIPMessage* m = new SIPMessage(t->initialMessage(), 200);
	SDPBody* sdp = createRtpSDP(true);
	m->setBody(sdp);
	t->setResponse(m);
	m->deref();
	return;
    }
    t->setResponse(488);
}

bool YateSIPConnection::checkUser(SIPTransaction* t, bool refuse)
{
    if (m_user.null())
	return true;
    int age = t->authUser(m_user);
    if ((age > 0) && (age <= 10))
	return true;
    DDebug(this,DebugAll,"YateSIPConnection::checkUser(%p) failed, age %d [%p]",t,age,this);
    if (refuse)
	t->requestAuth("realm","domain",false);
    return false;
}

void YateSIPConnection::doBye(SIPTransaction* t)
{
    if (m_authBye && !checkUser(t))
	return;
    DDebug(this,DebugAll,"YateSIPConnection::doBye(%p) [%p]",t,this);
    const SIPHeaderLine* hl = t->initialMessage()->getHeader("Reason");
    if (hl) {
	const NamedString* text = hl->getParam("text");
	if (text)
	    m_reason = *text;
	// FIXME: add SIP and Q.850 cause codes
    }
    t->setResponse(200);
    m_byebye = false;
    hangup();
}

void YateSIPConnection::doCancel(SIPTransaction* t)
{
#ifdef DEBUG
    if (!checkUser(t,false))
	Debug(DebugMild,"User authentication failed for user '%s' but CANCELing anyway [%p]",
	    m_user.c_str(),this);
#endif
    DDebug(this,DebugAll,"YateSIPConnection::doCancel(%p) [%p]",t,this);
    if (m_tr) {
	t->setResponse(200);
	m_byebye = false;
	clearTransaction();
	disconnect("Cancelled");
    }
    else
	t->setResponse(481);
}

void YateSIPConnection::disconnected(bool final, const char *reason)
{
    Debug(this,DebugAll,"YateSIPConnection::disconnected() '%s' [%p]",reason,this);
    if (reason) {
	int code = lookup(reason,dict_errors);
	if (code)
	    setReason(lookup(code,SIPResponses,reason),code);
	else
	    setReason(reason);
    }
    Channel::disconnected(final,reason);
}

bool YateSIPConnection::msgProgress(Message& msg)
{
    Channel::msgProgress(msg);
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 183);
	m->setBody(createProvisionalSDP(msg));
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("progressing");
    return true;
}

bool YateSIPConnection::msgRinging(Message& msg)
{
    Channel::msgRinging(msg);
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 180);
	m->setBody(createProvisionalSDP(msg));
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("ringing");
    return true;
}

bool YateSIPConnection::msgAnswered(Message& msg)
{
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 200);
	SDPBody* sdp = createPasstroughSDP(msg);
	if (!sdp) {
	    m_rtpForward = false;
	    sdp = createRtpSDP();
	}
	m->setBody(sdp);
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("answered",Established);
    return true;
}

bool YateSIPConnection::msgTone(Message& msg, const char* tone)
{
    if (m_rtpid && (m_rtpid != "-")) {
	msg.setParam("targetid",m_rtpid);
	return false;
    }
    // FIXME: when muted or doing RTP forwarding we should use INFO messages
    return false;
}

bool YateSIPConnection::msgText(Message& msg, const char* text)
{
    return false;
}

bool YateSIPConnection::callRouted(Message& msg)
{
    Channel::callRouted(msg);
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	String s(msg.retValue());
	if (s.startSkip("sip/",false) && s && msg.getBoolValue("redirect")) {
	    Debug(this,DebugAll,"YateSIPConnection redirecting to '%s' [%p]",s.c_str(),this);
	    SIPMessage* m = new SIPMessage(m_tr->initialMessage(),302);
	    s = "<" + s + ">";
	    m->addHeader("Contact",s);
	    m_tr->setResponse(m);
	    m->deref();
	    m_byebye = false;
	    setReason("Redirected",302);
	    setStatus("redirected");
	    return false;
	}
	m_tr->setResponse(183);
    }
    return true;
}

void YateSIPConnection::callAccept(Message& msg)
{
    Channel::callAccept(msg);
    m_user = msg.getValue("username");
    if (m_authBye)
	m_authBye = msg.getBoolValue("xsip_auth_bye",true);
    if (m_rtpForward) {
	String tmp(msg.getValue("rtp_forward"));
	if (tmp != "accepted")
	    m_rtpForward = false;
    }
}

void YateSIPConnection::callRejected(const char* error, const char* reason, const Message* msg)
{
    Channel::callRejected(error,reason,msg);
    int code = lookup(error,dict_errors,500);
    if (code == 401)
	m_tr->requestAuth("realm","domain",false);
    else
	m_tr->setResponse(code,reason);
    setReason(reason,code);
}

YateSIPLine::YateSIPLine(const String& name)
    : String(name), m_resend((u_int64_t)0), m_interval(0),
      m_retry(false), m_tr(0), m_marked(false), m_valid(false)
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::YateSIPLine('%s') [%p]",c_str(),this);
    s_lines.append(this);
}

YateSIPLine::~YateSIPLine()
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::~YateSIPLine() '%s' [%p]",c_str(),this);
    s_lines.remove(this,false);
    logout();
}

SIPAuthLine* YateSIPLine::buildAuth(const SIPMessage* answer, const String& method,
    const String& uri, bool proxy) const
{
    return answer ? answer->buildAuth(m_username,m_password,method,uri,proxy) : 0;
}

SIPMessage* YateSIPLine::buildRegister(int expires, const SIPMessage* msg) const
{
    String exp(expires);
    String tmp;
    tmp << "sip:" << m_registrar;
    SIPMessage* m = new SIPMessage("REGISTER",tmp);
    if (msg)
	m->setParty(msg->getParty());
    else
	plugin.ep()->buildParty(m);
    if (!m->getParty()) {
	Debug(&plugin,DebugWarn,"Could not create party for '%s' [%p]",
	    m_registrar.c_str(),this);
	m->destruct();
	return 0;
    }
    tmp = "\"";
    tmp << (m_display.null() ? m_username : m_display);
    tmp << "\" <sip:";
    tmp << m_username << "@";
    tmp << m->getParty()->getLocalAddr() << ":";
    tmp << m->getParty()->getLocalPort() << ">";
    m->addHeader("Contact",tmp);
    m->addHeader("Expires",exp);
    tmp = "<sip:";
    tmp << m_username << "@" << domain() << ">";
    m->addHeader("To",tmp);
    m->complete(plugin.ep()->engine(),m_username,domain());
    return m;
}

void YateSIPLine::login(const SIPMessage* msg)
{
    if (m_registrar.null() || m_username.null()) {
	logout();
	m_retry = false;
	m_valid = true;
	return;
    }
    clearTransaction();
    m_retry = true;

    SIPMessage* m = buildRegister(m_interval,msg);
    if (!m) {
	m_retry = false;
	m_valid = false;
	return;
    }
    if (msg) {
	SIPAuthLine* auth = buildAuth(msg,m->method,m->uri,(msg->code == 407));
	m->addHeader(auth);
	m_retry = false;
    }
    DDebug(&plugin,DebugInfo,"YateSIPLine '%s' emiting %p for answer %p [%p]",
	c_str(),m,msg,this);
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_tr->setUserData(this);
    }
    m->deref();
}

void YateSIPLine::logout()
{
    m_resend = 0;
    bool sendLogout = m_valid && m_registrar && m_username;
    clearTransaction();
    m_retry = false;
    m_valid = false;
    if (sendLogout) {
	SIPMessage* m = buildRegister(0,0);
	if (!m)
	    return;
	plugin.ep()->engine()->addMessage(m);
	m->deref();
    }
}

bool YateSIPLine::process(SIPEvent* ev)
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    if (ev->getTransaction() != m_tr)
	return false;
    if (ev->getState() == SIPTransaction::Cleared) {
	clearTransaction();
	m_retry = false;
	m_valid = false;
	m_resend = m_interval*1000000 + Time::now();
	return false;
    }
    const SIPMessage* msg = ev->getMessage();
    if (!(msg && msg->isAnswer()))
	return false;
    if (ev->getState() != SIPTransaction::Process)
	return false;
    clearTransaction();
    DDebug(&plugin,DebugAll,"YateSIPLine '%s' got answer %d%s [%p]",
	c_str(),msg->code,m_retry ? " (may retry)" : "",this);
    switch (msg->code) {
	case 200:
	    m_retry = false;
	    m_valid = true;
	    m_resend = m_interval*1000000 + Time::now();
	    Debug(&plugin,DebugInfo,"SIP line '%s' logon success",c_str());
	    break;
	case 401:
	case 407:
	    if (m_retry) {
		m_retry = false;
		login(msg);
		break;
	    }
	default:
	    m_retry = false;
	    m_valid = false;
	    Debug(&plugin,DebugInfo,"SIP line '%s' logon failure %d",c_str(),msg->code);
    }
    return false;
}

void YateSIPLine::timer(const Time& when)
{
    if (!m_resend || (m_resend > when))
	return;
    m_resend = m_interval*1000000 + when;
    login();
}

void YateSIPLine::clearTransaction()
{
    if (m_tr) {
	DDebug(&plugin,DebugInfo,"YateSIPLine clearing transaction %p [%p]",
	    m_tr,this);
	m_tr->setUserData(0);
	m_tr->deref();
	m_tr = 0;
    }
}

bool YateSIPLine::change(String& dest, const String& src)
{
    if (dest == src)
	return false;
    // we need to log out before any parameter changes
    logout();
    dest = src;
    return true;
}

bool YateSIPLine::update(const Message& msg)
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::update() '%s' [%p]",c_str(),this);
    bool chg = false;
    chg = change(m_registrar,msg.getValue("registrar")) || chg;
    chg = change(m_outbound,msg.getValue("outbound")) || chg;
    chg = change(m_username,msg.getValue("username")) || chg;
    chg = change(m_password,msg.getValue("password")) || chg;
    chg = change(m_domain,msg.getValue("domain")) || chg;
    m_display = msg.getValue("description");
    m_interval = msg.getIntValue("interval",600);
    // if something changed we logged out so try to climb back
    if (chg)
	login();
    return chg;
}

bool UserHandler::received(Message &msg)
{
    String tmp(msg.getValue("protocol"));
    if (tmp != "sip")
	return false;
    tmp = msg.getValue("account");
    if (tmp.null())
	return false;
    YateSIPLine* line = plugin.findLine(tmp);
    if (!line)
	line = new YateSIPLine(tmp);
    line->update(msg);
    return true;
}

YateSIPConnection* SIPDriver::findCall(const String& callid)
{
    XDebug(this,DebugAll,"SIPDriver finding call '%s'",callid.c_str());
    Lock mylock(this);
    ObjList* l = &channels();
    for (; l; l = l->next()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c && (c->callid() == callid))
	    return c;
    }
    return 0;
}

YateSIPConnection* SIPDriver::findDialog(const SIPDialog& dialog)
{
    XDebug(this,DebugAll,"SIPDriver finding dialog '%s'",dialog.c_str());
    Lock mylock(this);
    ObjList* l = &channels();
    for (; l; l = l->next()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c && (c->dialog() == dialog))
	    return c;
    }
    return 0;
}

YateSIPLine* SIPDriver::findLine(const String& line)
{
    if (line.null())
	return 0;
    ObjList* l = s_lines.find(line);
    return l ? static_cast<YateSIPLine*>(l->get()) : 0;
}

bool SIPDriver::validLine(const String& line)
{
    if (line.null())
	return true;
    YateSIPLine* l = findLine(line);
    return l && l->valid();
}

bool SIPDriver::received(Message &msg, int id)
{
    if (id == Timer) {
	ObjList* l = &s_lines;
	for (; l; l = l->next()) {
	    YateSIPLine* line = static_cast<YateSIPLine*>(l->get());
	    if (line)
		line->timer(msg.msgTime());
	}
    }
    else if (id == Halt)
	s_lines.clear();
    return Driver::received(msg,id);
}

bool SIPDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugWarn,"SIP call found but no data channel!");
	return false;
    }
    if (!validLine(msg.getValue("line")))
	return false;
    YateSIPConnection* conn = new YateSIPConnection(msg,dest,msg.getValue("id"));
    if (conn->getTransaction()) {
	CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
	if (ch && conn->connect(ch)) {
	    msg.setParam("peerid",conn->id());
	    msg.setParam("targetid",conn->id());
	    conn->deref();
	    return true;
	}
    }
    conn->destruct();
    return false;
}

SIPDriver::SIPDriver()
    : Driver("sip","varchans"), m_endpoint(0)
{
    Output("Loaded module SIP Channel");
}

SIPDriver::~SIPDriver()
{
    Output("Unloading module SIP Channel");
}

void SIPDriver::initialize()
{
    Output("Initializing module SIP Channel");
    s_cfg = Engine::configFile("ysipchan");
    s_cfg.load();
    s_maxForwards = s_cfg.getIntValue("general","maxforwards",20);
    if (!m_endpoint) {
	m_endpoint = new YateSIPEndPoint();
	if (!(m_endpoint->Init())) {
	    delete m_endpoint;
	    m_endpoint = 0;
	    return;
	}
	m_endpoint->startup();
	setup();
	installRelay(Halt);
	installRelay(Progress);
	Engine::install(new UserHandler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
