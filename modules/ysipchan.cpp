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
    { "busy", 486 },
    { "congestion", 480 },
    { "failure", 500 },
    {  0,   0 },
};

static Configuration s_cfg;

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
    YateSIPEngine *m_engine;

};

class YateSIPConnection : public Channel
{
public:
    enum {
	Incoming,
	Outgoing,
	Ringing,
	Established,
	Cleared,
    };
    YateSIPConnection(SIPEvent* ev, SIPTransaction* tr);
    YateSIPConnection(Message& msg, const String& uri, const char* target = 0);
    ~YateSIPConnection();
    virtual void disconnected(bool final, const char *reason);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual void callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callReject(const char* error, const char* reason);
    void startRouter();
    bool process(SIPEvent* ev);
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
    inline const String& getHost() const
	{ return m_host; }
    inline int getPort() const
	{ return m_port; }
private:
    void clearTransaction();
    SDPBody* createSDP(const char* addr, const char* port, const char* formats, const char* format = 0);
    SDPBody* createPasstroughSDP(Message &msg);
    SDPBody* createRtpSDP(SIPMessage* msg, const char* formats);
    SDPBody* createRtpSDP(bool start = false);
    bool startRtp();
    SIPTransaction* m_tr;
    bool m_hungup;
    bool m_byebye;
    int m_state;
    String m_reason;
    int m_reasonCode;
    String m_callid;
    SIPDialog m_dialog;
    URI m_uri;
    String m_rtpid;
    String m_rtpAddr;
    String m_rtpPort;
    String m_rtpFormat;
    String m_rtpLocal;
    int m_rtpSession;
    int m_rtpVersion;
    String m_formats;
    String m_host;
    int m_port;
    Message* m_route;
};

class SIPDriver : public Driver
{
public:
    SIPDriver();
    ~SIPDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    inline YateSIPEndPoint* ep() const
	{ return m_endpoint; }
    YateSIPConnection* findCall(const String& callid);
    YateSIPConnection* findDialog(const SIPDialog& dialog);
private:
    YateSIPEndPoint *m_endpoint;
};

static SIPDriver plugin;

static void parseSDP(SDPBody* sdp, String& addr, String& port, String& formats)
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
    if (c) {
	String tmp(*c);
	if (tmp.startSkip("audio")) {
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
		if (payload && s_cfg.getBoolValue("codecs",payload,defcodecs)) {
		    if (fmt)
			fmt << ",";
		    fmt << payload;
		}
	    }
	    formats = fmt;
	}
    }
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
    Debug(&plugin,DebugAll,"Sending to %s:%d",m_addr.host().c_str(),m_addr.port());
    m_sock->sendTo(
	event->getMessage()->getBuffer().data(),
	event->getMessage()->getBuffer().length(),
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
	Debug(DebugWarn,"Could not resolve UDP party name '%s' [%p]",
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
    : m_ep(ep)
{
    addAllowed("INVITE");
    addAllowed("BYE");
    addAllowed("CANCEL");
    if (s_cfg.getBoolValue("general","registrar"))
	addAllowed("REGISTER");
}

bool YateSIPEngine::buildParty(SIPMessage* message)
{
    return m_ep->buildParty(message);
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
	Debug(DebugWarn,"Error resolving name '%s'",host);
	return false;
    }
    addr.port(port);
    Debug(&plugin,DebugAll,"built addr: %s:%d",
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
	Debug(DebugGoOn,"Unable to allocate UDP socket\n");
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
    Debug(DebugInfo,"SIP Started on %s:%d\n", addr.host().safe(), addr.port());
    m_port = addr.port();
    m_engine = new YateSIPEngine(this);
    return true;
}

void YateSIPEndPoint::run()
{
    struct timeval tv;
    char buf[1500];
    SocketAddr addr;
    /* Watch stdin (fd 0) to see when it has input. */
    for (;;)
    {
	/* Wait up to 20000 microseconds. */
	tv.tv_sec = 0;
	tv.tv_usec = 20000;
	bool ok = false;
	m_sock->select(&ok,0,0,&tv);
	if (ok)
	{
	    // we can read the data
	    int res = m_sock->recvFrom(buf,sizeof(buf)-1,addr);
	    if (res <= 0) {
		if (!m_sock->canRetry()) {
		    Debug(DebugGoOn,"SIP error on read: %d\n", m_sock->error());
		}
	    } else if (res >= 72) {
		// we got already the buffer and here we start to do "good" stuff
		buf[res]=0;
		m_engine->addMessage(new YateUDPParty(m_sock,addr,m_port),buf,res);
	    //	Output("res %d buf %s",res,buf);
	    }
#ifdef DEBUG
	    else
		Debug(DebugInfo,"Received short SIP message of %d bytes",res);
#endif
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
	Debug(DebugWarn,"Dropping call, full or exiting");
	e->getTransaction()->setResponse(480);
	return;
    }

    if (e->getMessage()->getParam("To","tag")) {
	SIPDialog dlg(*e->getMessage());
	YateSIPConnection* conn = plugin.findDialog(dlg);
	if (conn)
	    conn->reInvite(t);
	else {
	    Debug(DebugWarn,"Got re-INVITE for missing dialog");
	    e->getTransaction()->setResponse(481);
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
	e->getTransaction()->setResponse(500, "Server Shutting Down");
	return;
    }
    const SIPHeaderLine* hl = e->getMessage()->getHeader("Contact");
    if (!hl) {
	e->getTransaction()->setResponse(400);
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
	e->getTransaction()->setResponse(200);
    else
	e->getTransaction()->setResponse(404);
    m->destruct();
}

// Incoming call constructor - just before starting the routing thread
YateSIPConnection::YateSIPConnection(SIPEvent* ev, SIPTransaction* tr)
    : Channel(plugin,0,false),
      m_tr(tr), m_hungup(false), m_byebye(true), m_state(Incoming),
      m_rtpSession(0), m_rtpVersion(0), m_port(0), m_route(0)
{
    Debug(this,DebugAll,"YateSIPConnection::YateSIPConnection(%p,%p) [%p]",ev,tr,this);
    setReason();
    m_tr->ref();
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
    m->addParam("caller",m_uri.getUser());
    m->addParam("called",uri.getUser());
    m->addParam("sip_uri",uri);
    m->addParam("sip_from",m_uri);
    m->addParam("sip_callid",m_callid);
    m->addParam("sip_contact",ev->getMessage()->getHeaderValue("Contact"));
    m->addParam("sip_user-agent",ev->getMessage()->getHeaderValue("User-Agent"));
    m->addParam("xsip_received",m_host);
    m->addParam("xsip_rport",String(m_port));
    if (ev->getMessage()->body && ev->getMessage()->body->isSDP()) {
	parseSDP(static_cast<SDPBody*>(ev->getMessage()->body),m_rtpAddr,m_rtpPort,m_formats);
	if (m_rtpAddr) {
	    m->addParam("rtp_forward","possible");
	    m->addParam("rtp_addr",m_rtpAddr);
	    m->addParam("rtp_port",m_rtpPort);
	    m->addParam("formats",m_formats);
	}
	int q = m_formats.find(',');
	m_rtpFormat = m_formats.substr(0,q);
    }
    Debug(this,DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
    m_route = m;
    Engine::enqueue(message("chan.startup"));
}

// Outgoing call constructor - in call.execute handler
YateSIPConnection::YateSIPConnection(Message& msg, const String& uri, const char* target)
    : Channel(plugin,0,true),
      m_tr(0), m_hungup(false), m_byebye(true), m_state(Outgoing), m_uri(uri),
      m_rtpSession(0), m_rtpVersion(0), m_port(0), m_route(0)
{
    Debug(this,DebugAll,"YateSIPConnection::YateSIPConnection(%p,'%s') [%p]",
	&msg,uri.c_str(),this);
    m_targetid = target;
    setReason();
    m_uri.parse();
    SIPMessage* m = new SIPMessage("INVITE",uri);
    plugin.ep()->buildParty(m,msg.getValue("host"),msg.getIntValue("port"));
    m->complete(plugin.ep()->engine(),msg.getValue("caller"),msg.getValue("domain"));
    m_host = m->getParty()->getPartyAddr();
    m_port = m->getParty()->getPartyPort();
    m_address << m_host << ":" << m_port;
    m_dialog = *m;
    SDPBody* sdp = createPasstroughSDP(msg);
    if (!sdp)
	sdp = createRtpSDP(m,msg.getValue("formats"));
    m->setBody(sdp);
    m_tr = plugin.ep()->engine()->addMessage(m);
    m_callid = m_tr->getCallID();
    m->deref();
    if (m_tr) {
	m_tr->ref();
	m_tr->setUserData(this);
    }
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
	m_tr->setUserData(0);
	if (m_tr->isIncoming()) {
	    m_byebye = false;
	    m_tr->setResponse(m_reasonCode,m_reason.null() ? "Request Terminated" : m_reason.c_str());
	}
	m_tr->deref();
	m_tr = 0;
    }
}

void YateSIPConnection::hangup()
{
    if (m_hungup)
	return;
    m_hungup = true;
    Debug(this,DebugAll,"YateSIPConnection::hangup() state=%d trans=%p code=%d reason='%s' [%p]",
	m_state,m_tr,m_reasonCode,m_reason.c_str(),this);
    Engine::enqueue(message("chan.hangup"));
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
		plugin.ep()->buildParty(m,m_host,m_port);
		const SIPMessage* i = m_tr->initialMessage();
		m->copyHeader(i,"Via");
		m->copyHeader(i,"From");
		m->copyHeader(i,"To");
		m->copyHeader(i,"Call-ID");
		String tmp;
		tmp << i->getCSeq() << " CANCEL";
		m->addHeader("CSeq",tmp);
		plugin.ep()->engine()->addMessage(m);
		m->deref();
	    }
	    break;
    }
    clearTransaction();
    m_state = Cleared;

    if (m_byebye) {
	m_byebye = false;
	SIPMessage* m = new SIPMessage("BYE",m_uri);
	plugin.ep()->buildParty(m,m_host,m_port);
	m->addHeader("Call-ID",m_callid);
	String tmp;
	tmp << "<" << m_dialog.localURI << ">";
	SIPHeaderLine* hl = new SIPHeaderLine("From",tmp);
	hl->setParam("tag",m_dialog.localTag);
	m->addHeader(hl);
	tmp.clear();
	tmp << "<" << m_dialog.remoteURI << ">";
	hl = new SIPHeaderLine("To",tmp);
	hl->setParam("tag",m_dialog.remoteTag);
	m->addHeader(hl);
	plugin.ep()->engine()->addMessage(m);
	m->deref();
    }
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
    m.addParam("driver","sip");
    m.addParam("id",id());
    m.addParam("direction","bidir");
    m.addParam("remoteip",msg->getParty()->getPartyAddr());
    m.userData(static_cast<CallEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	m_rtpLocal = m.getValue("localip",m_rtpLocal);
	return createSDP(m_rtpLocal,m.getValue("localport"),formats);
    }
    return 0;
}

// Creates a started external RTP channel from remote addr and builds SDP from it
SDPBody* YateSIPConnection::createRtpSDP(bool start)
{
    if (m_rtpAddr.null()) {
	m_rtpid = "-";
	return createSDP(m_rtpLocal,0,m_formats);
    }
    Message m("chan.rtp");
    m.addParam("driver","sip");
    m.addParam("id",id());
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    if (start) {
	m.addParam("remoteport",m_rtpPort);
	m.addParam("format",m_rtpFormat);
    }
    m.userData(static_cast<CallEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	m_rtpLocal = m.getValue("localip",m_rtpLocal);
	if (start)
	    m_rtpFormat = m.getValue("format");
	return createSDP(m_rtpLocal,m.getValue("localport"),m_formats,m_rtpFormat);
    }
    return 0;
}

// Starts an already created external RTP channel
bool YateSIPConnection::startRtp()
{
    if (m_rtpid.null() || m_rtpid == "-")
	return false;
    Debug(this,DebugAll,"YateSIPConnection::startRtp() [%p]",this);
    Message m("chan.rtp");
    m.addParam("driver","sip");
    m.addParam("id",id());
    m.addParam("rtpid",m_rtpid);
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    m.addParam("remoteport",m_rtpPort);
    m.addParam("format",m_rtpFormat);
    m.userData(static_cast<CallEndpoint *>(this));
    return Engine::dispatch(m);
}

SDPBody* YateSIPConnection::createSDP(const char* addr, const char* port, const char* formats, const char* format)
{
    Debug(this,DebugAll,"YateSIPConnection::createSDP('%s','%s','%s') [%p]",
	addr,port,formats,this);
    if (!addr)
	return 0;
    if (m_rtpSession)
	++m_rtpVersion;
    else
	m_rtpVersion = m_rtpSession = (int)(Time::now() / (u_int32_t)10000000000);
    String owner;
    owner << "yate " << m_rtpSession << " " << m_rtpVersion << " IN IP4 " << addr;
    if (!port) {
	port = "1";
	addr = "0.0.0.0";
    }
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
		if (map && s_cfg.getBoolValue("codecs",*s,defcodecs)) {
		    frm << " " << payload;
		    String* tmp = new String("rtpmap:");
		    *tmp << payload << " " << map;
		    rtpmap.append(tmp);
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

bool YateSIPConnection::process(SIPEvent* ev)
{
    Debug(this,DebugInfo,"YateSIPConnection::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    m_dialog = *ev->getTransaction()->recentMessage();
    if (ev->getState() == SIPTransaction::Cleared) {
	if (m_tr) {
	    Debug(this,DebugInfo,"YateSIPConnection clearing transaction %p [%p]",
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
	Debug(this,DebugInfo,"YateSIPConnection got SDP [%p]",this);
	parseSDP(static_cast<SDPBody*>(ev->getMessage()->body),
	    m_rtpAddr,m_rtpPort,m_formats);
	int q = m_formats.find(',');
	m_rtpFormat = m_formats.substr(0,q);
	Debug(this,DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	    m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
    }
    if (ev->getMessage()->isAnswer() && ((ev->getMessage()->code / 100) == 2)) {
	setStatus("answered",Established);
	Message *m = message("call.answered");
	if (m_rtpPort && m_rtpAddr && !startRtp()) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_rtpAddr);
	    m->addParam("rtp_port",m_rtpPort);
	    m->addParam("formats",m_formats);
	}
	Engine::enqueue(m);
    }
    if ((m_state < Ringing) && ev->getMessage()->isAnswer() && (ev->getMessage()->code == 180)) {
	setStatus("ringing",Ringing);
	Message *m = message("call.ringing");
	if (m_rtpPort && m_rtpAddr && !startRtp()) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_rtpAddr);
	    m->addParam("rtp_port",m_rtpPort);
	    m->addParam("formats",m_formats);
	}
	Engine::enqueue(m);
    }
    if (ev->getMessage()->isACK()) {
	Debug(this,DebugInfo,"YateSIPConnection got ACK [%p]",this);
	startRtp();
    }
    return false;
}

void YateSIPConnection::reInvite(SIPTransaction* t)
{
    Debug(this,DebugAll,"YateSIPConnection::reInvite(%p) [%p]",t,this);
    // hack: use a while instead of if so we can return or break out of it
    while (t->initialMessage()->body && t->initialMessage()->body->isSDP()) {
	// accept re-INVITE only for local RTP, not for pass-trough
	if (m_rtpid.null())
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

void YateSIPConnection::doBye(SIPTransaction* t)
{
    Debug(this,DebugAll,"YateSIPConnection::doBye(%p) [%p]",t,this);
    t->setResponse(200);
    m_byebye = false;
    hangup();
}

void YateSIPConnection::doCancel(SIPTransaction* t)
{
    Debug(this,DebugAll,"YateSIPConnection::doCancel(%p) [%p]",t,this);
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
    if (reason)
	setReason(reason);
    Channel::disconnected(final,reason);
}

bool YateSIPConnection::msgRinging(Message& msg)
{
    Channel::msgRinging(msg);
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 180);
	SDPBody* sdp = createPasstroughSDP(msg);
	m->setBody(sdp);
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
	if (!sdp)
	    sdp = createRtpSDP();
	m->setBody(sdp);
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("answered",Established);
    return true;
}

bool YateSIPConnection::msgTone(Message& msg, const char* tone)
{
    return false;
}

bool YateSIPConnection::msgText(Message& msg, const char* text)
{
    return false;
}

void YateSIPConnection::callRouted(Message& msg)
{
    Channel::callRouted(msg);
    if (m_tr && (m_tr->getState() == SIPTransaction::Process))
	m_tr->setResponse(183);
}

void YateSIPConnection::callAccept(Message& msg)
{
    Channel::callAccept(msg);
}

void YateSIPConnection::callReject(const char* error, const char* reason)
{
    Channel::callReject(error,reason);
    int code = lookup(error,dict_errors,500);
    m_tr->setResponse(code,reason);
}

YateSIPConnection* SIPDriver::findCall(const String& callid)
{
    DDebug(this,DebugAll,"SIPDriver finding call '%s'",callid.c_str());
    Lock mylock(this);
    ObjList* l = &channels();
    for (; l; l = l->next()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	// XXX
	if (c)
	    Debug(DebugAll,"Found '%s' at %p",c->callid().c_str(),c);
	if (c && (c->callid() == callid))
	    return c;
    }
    return 0;
}

YateSIPConnection* SIPDriver::findDialog(const SIPDialog& dialog)
{
    DDebug(this,DebugAll,"SIPDriver finding dialog '%s'",dialog.c_str());
    Lock mylock(this);
    ObjList* l = &channels();
    for (; l; l = l->next()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c && (c->dialog() == dialog))
	    return c;
    }
    return 0;
}

bool SIPDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(DebugWarn,"SIP call found but no data channel!");
	return false;
    }
    YateSIPConnection* conn = new YateSIPConnection(msg,dest,msg.getValue("id"));
    if (conn->getTransaction()) {
	CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
	if (ch && conn->connect(ch)) {
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
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
