/**
 * ysipchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Sip Channel
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
#include <yatesip.h>
#include <yatesdp.h>

#include <string.h>


using namespace TelEngine;
namespace { // anonymous

#define EXPIRES_MIN 60
#define EXPIRES_DEF 600
#define EXPIRES_MAX 3600

static TokenDict dict_errors[] = {
    { "incomplete", 484 },
    { "noroute", 404 },
    { "noroute", 604 },
    { "noconn", 503 },
    { "noconn", 408 },
    { "noauth", 401 },
    { "nomedia", 415 },
    { "nocall", 481 },
    { "busy", 486 },
    { "busy", 600 },
    { "noanswer", 487 },
    { "rejected", 406 },
    { "rejected", 606 },
    { "forbidden", 403 },
    { "forbidden", 603 },
    { "offline", 404 },
    { "congestion", 480 },
    { "failure", 500 },
    { "pending", 491 },
    { "looping", 483 },
    {  0,   0 },
};

static const char s_dtmfs[] = "0123456789*#ABCDF";

static TokenDict info_signals[] = {
    { "*", 10 },
    { "#", 11 },
    { "A", 12 },
    { "B", 13 },
    { "C", 14 },
    { "D", 15 },
    {  0,   0 },
};

class YateUDPParty : public SIPParty
{
public:
    YateUDPParty(Socket* sock, const SocketAddr& addr, int localPort, const char* localAddr = 0);
    ~YateUDPParty();
    virtual void transmit(SIPEvent* event);
    virtual const char* getProtoName() const;
    virtual bool setParty(const URI& uri);
protected:
    Socket* m_sock;
    SocketAddr m_addr;
};

class YateSIPEndPoint;
class SipHandler;

class YateSIPEngine : public SIPEngine
{
public:
    YateSIPEngine(YateSIPEndPoint* ep);
    virtual bool buildParty(SIPMessage* message);
    virtual bool checkUser(const String& username, const String& realm, const String& nonce,
	const String& method, const String& uri, const String& response,
	const SIPMessage* message, GenObject* userData);
    virtual SIPTransaction* forkInvite(SIPMessage* answer, SIPTransaction* trans);
    inline bool prack() const
	{ return m_prack; }
    inline bool info() const
	{ return m_info; }
private:
    static bool copyAuthParams(NamedList* dest, const NamedList& src);
    YateSIPEndPoint* m_ep;
    bool m_prack;
    bool m_info;
    bool m_fork;
};

class YateSIPLine : public String
{
    YCLASS(YateSIPLine,String)
public:
    YateSIPLine(const String& name);
    virtual ~YateSIPLine();
    void setupAuth(SIPMessage* msg) const;
    SIPMessage* buildRegister(int expires) const;
    void login();
    void logout();
    bool process(SIPEvent* ev);
    void timer(const Time& when);
    bool update(const Message& msg);
    inline const String& getLocalAddr() const
	{ return m_localAddr; }
    inline const String& getPartyAddr() const
	{ return m_proxyAddr ? m_proxyAddr : m_partyAddr; }
    inline int getLocalPort() const
	{ return m_localPort; }
    inline int getPartyPort() const
	{ return m_proxyPort ? m_proxyPort : m_partyPort; }
    inline bool localDetect() const
	{ return m_localDetect; }
    inline const String& getFullName() const
	{ return m_display; }
    inline const String& getUserName() const
	{ return m_username; }
    inline const String& getAuthName() const
	{ return m_authname ? m_authname : m_username; }
    inline const String& domain() const
	{ return m_domain ? m_domain : m_registrar; }
    inline const char* domain(const char* defDomain) const
	{ return m_domain ? m_domain.c_str() :
	    (TelEngine::null(defDomain) ? m_registrar.c_str() : defDomain); }
    inline bool valid() const
	{ return m_valid; }
    inline bool marked() const
	{ return m_marked; }
    inline void marked(bool mark)
	{ m_marked = mark; }
private:
    void clearTransaction();
    void detectLocal(const SIPMessage* msg);
    bool change(String& dest, const String& src);
    bool change(int& dest, int src);
    void keepalive();
    void setValid(bool valid, const char* reason = 0);
    String m_registrar;
    String m_username;
    String m_authname;
    String m_password;
    String m_domain;
    String m_display;
    u_int64_t m_resend;
    u_int64_t m_keepalive;
    int m_interval;
    int m_alive;
    int m_flags;
    SIPTransaction* m_tr;
    bool m_marked;
    bool m_valid;
    String m_callid;
    String m_localAddr;
    String m_proxyAddr;
    String m_partyAddr;
    int m_localPort;
    int m_proxyPort;
    int m_partyPort;
    bool m_localDetect;
};

class YateSIPEndPoint : public Thread
{
public:
    YateSIPEndPoint(Thread::Priority prio = Thread::Normal);
    ~YateSIPEndPoint();
    bool Init(void);
    void run(void);
    bool incoming(SIPEvent* e, SIPTransaction* t);
    void invite(SIPEvent* e, SIPTransaction* t);
    void regReq(SIPEvent* e, SIPTransaction* t);
    void regRun(const SIPMessage* message, SIPTransaction* t);
    void options(SIPEvent* e, SIPTransaction* t);
    bool generic(SIPEvent* e, SIPTransaction* t);
    bool buildParty(SIPMessage* message, const char* host = 0, int port = 0, const YateSIPLine* line = 0);
    inline YateSIPEngine* engine() const
	{ return m_engine; }
    inline int port() const
	{ return m_port; }
    inline Socket* socket() const
	{ return m_sock; }
    inline void incFailedAuths() 
	{ m_failedAuths++; }
    inline unsigned int failedAuths()
    {
	unsigned int tmp = m_failedAuths;
	m_failedAuths = 0;
	return tmp;
    }
    inline unsigned int timedOutTrs()
    {
	unsigned int tmp = m_timedOutTrs;
	m_timedOutTrs = 0;
	return tmp;
    }
    inline unsigned int timedOutByes()
    {
	unsigned int tmp = m_timedOutByes;
	m_timedOutByes = 0;
	return tmp;
    }
private:
    void addMessage(const char* buf, int len, const SocketAddr& addr, int port);
    int m_port;
    String m_local;
    Socket* m_sock;
    SocketAddr m_addr;
    YateSIPEngine *m_engine;
    DataBlock m_buffer;

    unsigned int m_failedAuths;
    unsigned int m_timedOutTrs;
    unsigned int m_timedOutByes;
};

// Handle transfer requests
// Respond to the enclosed transaction
class YateSIPRefer : public Thread
{
public:
    YateSIPRefer(const String& transferorID, const String& transferredID,
	Driver* transferredDrv, Message* msg, SIPMessage* sipNotify,
	SIPTransaction* transaction);
    virtual void run(void);
    virtual void cleanup(void)
	{ release(true); }
private:
    // Respond the transaction and deref() it
    void setTrResponse(int code);
    // Set transaction response. Send the notification message. Notify the
    // connection and release other objects
    void release(bool fromCleanup = false);

    String m_transferorID;           // Transferor channel's id
    String m_transferredID;          // Transferred channel's id
    Driver* m_transferredDrv;        // Transferred driver's pointer
    Message* m_msg;                  // 'call.route' message
    SIPMessage* m_sipNotify;         // NOTIFY message to send the result
    int m_notifyCode;                // The result to send with NOTIFY
    SIPTransaction* m_transaction;   // The transaction to respond to
    int m_rspCode;                   // The transaction response
};

class YateSIPRegister : public Thread
{
public:
    inline YateSIPRegister(YateSIPEndPoint* ep, SIPMessage* message, SIPTransaction* t)
	: Thread("YSIP Register"),
	  m_ep(ep), m_msg(message), m_tr(t)
	{ }
    virtual void run()
	{ m_ep->regRun(m_msg,m_tr); }
private:
    YateSIPEndPoint* m_ep;
    RefPointer<SIPMessage> m_msg;
    RefPointer<SIPTransaction> m_tr;
};

class YateSIPConnection : public Channel, public SDPSession
{
    friend class SipHandler;
    YCLASS(YateSIPConnection,Channel)
public:
    enum {
	Incoming = 0,
	Outgoing = 1,
	Ringing = 2,
	Established = 3,
	Cleared = 4,
    };
    enum {
	ReinviteNone,
	ReinvitePending,
	ReinviteRequest,
	ReinviteReceived,
    };
    YateSIPConnection(SIPEvent* ev, SIPTransaction* tr);
    YateSIPConnection(Message& msg, const String& uri, const char* target = 0);
    ~YateSIPConnection();
    virtual void destroyed();
    virtual void complete(Message& msg, bool minimal=false) const;
    virtual void disconnected(bool final, const char *reason);
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool msgUpdate(Message& msg);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    void startRouter();
    bool process(SIPEvent* ev);
    bool checkUser(SIPTransaction* t, bool refuse = true);
    void doBye(SIPTransaction* t);
    void doCancel(SIPTransaction* t);
    bool doInfo(SIPTransaction* t);
    void doRefer(SIPTransaction* t);
    void reInvite(SIPTransaction* t);
    void hangup();
    inline const SIPDialog& dialog() const
	{ return m_dialog; }
    inline void setStatus(const char *stat, int state = -1)
	{ status(stat); if (state >= 0) m_state = state; }
    inline void setReason(const char* str = "Request Terminated", int code = 487)
	{ m_reason = str; m_reasonCode = code; }
    inline SIPTransaction* getTransaction() const
	{ return m_tr; }
    inline const String& callid() const
	{ return m_callid; }
    inline const String& user() const
	{ return m_user; }
    inline int getPort() const
	{ return m_port; }
    inline const String& getLine() const
	{ return m_line; }
    inline void referTerminated()
	{ m_referring = false; }
    inline bool isDialog(const String& callid, const String& fromTag,
	const String& toTag) const
	{ return callid == m_dialog &&
	    m_dialog.fromTag(isOutgoing()) == fromTag &&
	    m_dialog.toTag(isOutgoing()) == toTag; }
    // Build and add a callid parameter to a list
    static inline void addCallId(NamedList& nl, const String& dialog,
	const String& fromTag, const String& toTag) {
	    String tmp;
	    tmp << "sip/" << dialog << "/" << fromTag << "/" << toTag;
	    nl.addParam("callid",tmp);
	}

protected:
    virtual Message* buildChanRtp(RefObject* context);
    MimeSdpBody* createProvisionalSDP(Message& msg);
    virtual void mediaChanged(const SDPMedia& media);
    virtual void endDisconnect(const Message& msg, bool handled);

private:
    virtual void statusParams(String& str);
    void clearTransaction();
    void detachTransaction2();
    void startPendingUpdate();
    bool processTransaction2(SIPEvent* ev, const SIPMessage* msg, int code);
    SIPMessage* createDlgMsg(const char* method, const char* uri = 0);
    void emitUpdate();
    bool emitPRACK(const SIPMessage* msg);
    bool startClientReInvite(Message& msg);
    // Build the 'call.route' and NOTIFY messages needed by the transfer thread
    bool initTransfer(Message*& msg, SIPMessage*& sipNotify, const SIPMessage* sipRefer,
	const MimeHeaderLine* refHdr, const URI& uri, const MimeHeaderLine* replaces);
    // Decode an application/isup body into 'msg' if configured to do so
    // The message's name and user data are restored before exiting, regardless the result
    // Return true if an ISUP message was succesfully decoded
    bool decodeIsupBody(Message& msg, MimeBody* body);
    // Build the body of a SIP message from an engine message
    // Encode an ISUP message from parameters received in msg if enabled to process them
    // Build a multipart/mixed body if more then one body is going to be sent
    MimeBody* buildSIPBody(Message& msg, MimeSdpBody* sdp = 0);

    SIPTransaction* m_tr;
    SIPTransaction* m_tr2;
    // are we already hung up?
    bool m_hungup;
    // should we send a BYE?
    bool m_byebye;
    // should we CANCEL?
    bool m_cancel;
    int m_state;
    String m_reason;
    int m_reasonCode;
    String m_callid;
    // SIP dialog of this call, used for re-INVITE or BYE
    SIPDialog m_dialog;
    // remote URI as we send in dialog messages
    URI m_uri;
    String m_domain;
    String m_user;
    String m_line;
    int m_port;
    Message* m_route;
    ObjList* m_routes;
    bool m_authBye;
    bool m_inband;
    bool m_info;
    // REFER already running
    bool m_referring;
    // reINVITE requested or in progress
    int m_reInviting;
    // sequence number of last transmitted PRACK
    int m_lastRseq;
};

class YateSIPGenerate : public GenObject
{
    YCLASS(YateSIPGenerate,GenObject)
public:
    YateSIPGenerate(SIPMessage* m);
    virtual ~YateSIPGenerate();
    bool process(SIPEvent* ev);
    bool busy() const
	{ return m_tr != 0; }
    int code() const
	{ return m_code; }
private:
    void clearTransaction();
    SIPTransaction* m_tr;
    int m_code;
};

class UserHandler : public MessageHandler
{
public:
    UserHandler()
	: MessageHandler("user.login",150)
	{ }
    virtual bool received(Message &msg);
};

class SipHandler : public MessageHandler
{
public:
    SipHandler()
	: MessageHandler("xsip.generate",110)
	{ }
    virtual bool received(Message &msg);
};

// Proxy class used to transport a data buffer
// The object doesn't own the data buffer
class DataBlockProxy : public RefObject
{
public:
    inline DataBlockProxy()
	: m_data(0)
	{}
    inline DataBlockProxy(DataBlock* data)
	: m_data(data)
	{}
    virtual void* getObject(const String& name) const {
	    if (name == "DataBlock")
		return m_data;
	    return RefObject::getObject(name);
	}
private:
    DataBlock* m_data;
};

class SIPDriver : public Driver
{
public:
    SIPDriver();
    ~SIPDriver();
    virtual void initialize();
    virtual bool hasLine(const String& line) const;
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool received(Message& msg, int id);
    inline YateSIPEndPoint* ep() const
	{ return m_endpoint; }
    inline SDPParser& parser()
        { return m_parser; }
    YateSIPConnection* findCall(const String& callid, bool incRef = false);
    YateSIPConnection* findDialog(const SIPDialog& dialog, bool incRef = false);
    YateSIPConnection* findDialog(const String& dialog, const String& fromTag,
	const String& toTag, bool incRef = false);
    YateSIPLine* findLine(const String& line) const;
    YateSIPLine* findLine(const String& addr, int port, const String& user = String::empty());
    bool validLine(const String& line);
    bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    void msgStatus(Message& msg);
protected:
    virtual void genUpdate(Message& msg);
private:
    SDPParser m_parser;
    YateSIPEndPoint *m_endpoint;
};

static SIPDriver plugin;
static ObjList s_lines;
static Configuration s_cfg;
static String s_realm = "Yate";
static String s_rtpip;
static int s_floodEvents = 20;
static int s_maxForwards = 20;
static int s_nat_refresh = 25;
static bool s_privacy = false;
static bool s_auto_nat = true;
static bool s_progress = false;
static bool s_inband = false;
static bool s_info = false;
static bool s_start_rtp = false;
static bool s_ack_required = true;
static bool s_1xx_formats = true;
static bool s_auth_register = true;
static bool s_reg_async = true;
static bool s_multi_ringing = false;
static bool s_refresh_nosdp = true;
static bool s_sipt_isup = false;         // Control the application/isup body processing

static int s_expires_min = EXPIRES_MIN;
static int s_expires_def = EXPIRES_DEF;
static int s_expires_max = EXPIRES_MAX;

static String s_statusCmd = "status";

// Check if an IPv4 address belongs to one of the non-routable blocks
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

// Check if there may be a NAT between an address embedded in the protocol
//  and an address obtained from the network layer
static bool isNatBetween(const String& embAddr, const String& netAddr)
{
    return isPrivateAddr(embAddr) && !isPrivateAddr(netAddr);
}

// List of headers we may not want to handle generically
static const char* s_filterHeaders[] = {
    "from",
    "to",
    0
};

// List of critical headers we surely don't want to handle generically
static const char* s_rejectHeaders[] = {
    "via",
    "route",
    "record-route",
    "call-id",
    "cseq",
    "max-forwards",
    "content-length",
    "www-authenticate",
    "proxy-authenticate",
    "authorization",
    "proxy-authorization",
    0
};

// Check if a string matches one member of a static list
static bool matchAny(const String& name, const char** strs)
{
    for (; *strs; strs++)
	if (name == *strs)
	    return true;
    return false;
}

// Copy headers from SIP message to Yate message
static void copySipHeaders(NamedList& msg, const SIPMessage& sip, bool filter = true)
{
    const ObjList* l = sip.header.skipNull();
    for (; l; l = l->skipNext()) {
	const MimeHeaderLine* t = static_cast<const MimeHeaderLine*>(l->get());
	String name(t->name());
	name.toLower();
	if (matchAny(name,s_rejectHeaders))
	    continue;
	if (filter && matchAny(name,s_filterHeaders))
	    continue;
	String tmp(*t);
	const ObjList* p = t->params().skipNull();
	for (; p; p = p->skipNext()) {
	    NamedString* s = static_cast<NamedString*>(p->get());
	    tmp << ";" << s->name();
	    if (!s->null())
		tmp << "=" << *s;
	}
	msg.addParam("sip_"+name,tmp);
    }
}

// Copy headers from Yate message to SIP message
static void copySipHeaders(SIPMessage& sip, const NamedList& msg, const char* prefix = "osip_")
{
    prefix = msg.getValue("osip-prefix",prefix);
    if (!prefix)
	return;
    unsigned int n = msg.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* str = msg.getParam(i);
	if (!str)
	    continue;
	String name(str->name());
	if (!name.startSkip(prefix,false))
	    continue;
	if (name.trimBlanks().null())
	    continue;
	sip.addHeader(name,*str);
    }
}

// Copy privacy related information from SIP message to Yate message
static void copyPrivacy(Message& msg, const SIPMessage& sip)
{
    bool anonip = (sip.getHeaderValue("Anonymity") &= "ipaddr");
    const MimeHeaderLine* hl = sip.getHeader("Remote-Party-ID");
    const MimeHeaderLine* pr = sip.getHeader("Privacy");
    if (!(anonip || hl || pr))
	return;
    const NamedString* p = hl ? hl->getParam("screen") : 0;
    if (p)
	msg.setParam("screened",*p);
    if (pr && (*pr &= "none")) {
	msg.setParam("privacy",String::boolText(false));
	return;
    }
    bool privname = false;
    bool privuri = false;
    String priv;
    if (anonip)
	priv.append("addr",",");
    p = hl ? hl->getParam("privacy") : 0;
    if (p) {
	if ((*p &= "full") || (*p &= "full-network"))
	    privname = privuri = true;
	else if ((*p &= "name") || (*p &= "name-network"))
	    privname = true;
	else if ((*p &= "uri") || (*p &= "uri-network"))
	    privuri = true;
    }
    if (pr) {
	if ((*pr &= "user") || pr->getParam("user"))
	    privname = true;
	if ((*pr &= "header") || pr->getParam("header"))
	    privuri = true;
    }
    if (privname)
	priv.append("name",",");
    if (privuri)
	priv.append("uri",",");
    if (pr) {
	if ((*pr &= "session") || pr->getParam("session"))
	    priv.append("session",",");
	if ((*pr &= "critical") || pr->getParam("critical"))
	    priv.append("critical",",");
    }
    if (priv)
	msg.setParam("privacy",priv);
    if (hl) {
	URI uri(*hl);
	const char* tmp = uri.getDescription();
	if (tmp)
	    msg.setParam("privacy_callername",tmp);
	tmp = uri.getUser();
	if (tmp)
	    msg.setParam("privacy_caller",tmp);
	tmp = uri.getHost();
	if (tmp)
	    msg.setParam("privacy_domain",tmp);
	const String* str = hl->getParam("party");
	if (!TelEngine::null(str))
	    msg.setParam("remote_party",*str);
	str = hl->getParam("id-type");
	if (!TelEngine::null(str))
	    msg.setParam("remote_id_type",*str);
    }
}

// Copy privacy related information from Yate message to SIP message
static void copyPrivacy(SIPMessage& sip, const Message& msg)
{
    String screened(msg.getValue("screened"));
    String privacy(msg.getValue("privacy"));
    if (screened.null() && privacy.null())
	return;
    bool screen = screened.toBoolean();
    bool anonip = (privacy.find("addr") >= 0);
    bool privname = (privacy.find("name") >= 0);
    bool privuri = (privacy.find("uri") >= 0);
    String rfc3323;
    // allow for a simple "privacy=yes" or similar
    if (privacy.toBoolean(false))
	privname = privuri = true;
    // "privacy=no" is translated to RFC 3323 "none"
    else if (!privacy.toBoolean(true))
	rfc3323 = "none";
    if (anonip)
	sip.setHeader("Anonymity","ipaddr");
    if (screen || privname || privuri) {
	const char* caller = msg.getValue("privacy_caller",msg.getValue("caller"));
	if (!caller)
	    caller = "anonymous";
	const char* domain = msg.getValue("privacy_domain",msg.getValue("domain"));
	if (!domain)
	    domain = "domain";
	String tmp = msg.getValue("privacy_callername",msg.getValue("callername",caller));
	if (tmp) {
	    MimeHeaderLine::addQuotes(tmp);
	    tmp += " ";
	}
	tmp << "<sip:" << caller << "@" << domain << ">";
	MimeHeaderLine* hl = new MimeHeaderLine("Remote-Party-ID",tmp);
	if (screen)
	    hl->setParam("screen","yes");
	if (privname && privuri)
	    hl->setParam("privacy","full");
	else if (privname)
	    hl->setParam("privacy","name");
	else if (privuri)
	    hl->setParam("privacy","uri");
	else
	    hl->setParam("privacy","none");
	const char* str = msg.getValue("remote_party");
	if (str)
	    hl->setParam("party",str);
	str = msg.getValue("remote_id_type");
	if (str)
	    hl->setParam("id-type",str);
	sip.addHeader(hl);
    }
    if (rfc3323.null()) {
	if (privname)
	    rfc3323.append("user",";");
	if (privuri)
	    rfc3323.append("header",";");
	if (privacy.find("session") >= 0)
	    rfc3323.append("session",";");
	if (rfc3323 && (privacy.find("critical") >= 0))
	    rfc3323.append("critical",";");
    }
    if (rfc3323)
	sip.addHeader("Privacy",rfc3323);
}

// Check if the given body have the given type
// Find the given type inside multiparts
static inline MimeBody* getOneBody(MimeBody* body, const char* type)
{
    return body ? body->getFirst(type) : 0;
}

// Check if the given body is a SDP one or find an enclosed SDP body
//  if it is a multipart
static inline MimeSdpBody* getSdpBody(MimeBody* body)
{
    if (!body)
	return 0;
    return static_cast<MimeSdpBody*>(body->isSDP() ? body : body->getFirst("application/sdp"));
}

// Add a mime body parameter to a list of parameters
// Remove quotes, trim blanks and convert to lower case before adding
// Return false if the parameter wasn't added
inline bool addBodyParam(NamedList& nl, const char* param, MimeBody* body, const char* bodyParam)
{
    const NamedString* ns = body ? body->getParam(bodyParam) : 0;
    if (!ns)
	return false;
    String p = *ns;
    MimeHeaderLine::delQuotes(p);
    p.trimBlanks();
    if (p.null())
	return false;
    p.toLower();
    nl.addParam(param,p);
    return true;
}

// Find an URI parameter separator. Accept '?' or '&'
static inline int findURIParamSep(const String& str, int start)
{
    if (start < 0)
	return -1;
    for (int i = start; i < (int)str.length(); i++)
	if (str[i] == '?' || str[i] == '&')
	    return i;
    return -1;
}


YateUDPParty::YateUDPParty(Socket* sock, const SocketAddr& addr, int localPort, const char* localAddr)
    : m_sock(sock), m_addr(addr)
{
    DDebug(&plugin,DebugAll,"YateUDPParty::YateUDPParty() %s:%d [%p]",localAddr,localPort,this);
    m_localPort = localPort;
    m_party = m_addr.host();
    m_partyPort = m_addr.port();
    if (localAddr)
	m_local = localAddr;
    else {
	SocketAddr laddr;
	if (laddr.local(addr))
	    m_local = laddr.host();
	else
	    m_local = "localhost";
    }
    DDebug(&plugin,DebugAll,"YateUDPParty local %s:%d party %s:%d",
	m_local.c_str(),m_localPort,
	m_party.c_str(),m_partyPort);
}

YateUDPParty::~YateUDPParty()
{
    DDebug(&plugin,DebugAll,"YateUDPParty::~YateUDPParty() [%p]",this);
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
    if (plugin.debugAt(DebugInfo)) {
	String raddr;
	raddr << m_addr.host() << ":" << m_addr.port();
	if (plugin.filterDebug(raddr)) {
	    String buf((char*)msg->getBuffer().data(),msg->getBuffer().length());
	    Debug(&plugin,DebugInfo,"Sending %s %p to %s\r\n------\r\n%s------",
		tmp.c_str(),msg,raddr.c_str(),buf.c_str());
	}
    }
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
    if (m_partyPort && m_party && s_cfg.getBoolValue("general","ignorevia",true))
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
      m_ep(ep), m_prack(false), m_info(false)
{
    addAllowed("INVITE");
    addAllowed("BYE");
    addAllowed("CANCEL");
    if (s_cfg.getBoolValue("general","registrar",!Engine::clientMode()))
	addAllowed("REGISTER");
    if (s_cfg.getBoolValue("general","transfer",!Engine::clientMode()))
	addAllowed("REFER");
    if (s_cfg.getBoolValue("general","options",true))
	addAllowed("OPTIONS");
    m_prack = s_cfg.getBoolValue("general","prack");
    if (m_prack)
	addAllowed("PRACK");
    m_info = s_cfg.getBoolValue("general","info",true);
    if (m_info)
	addAllowed("INFO");
    lazyTrying(s_cfg.getBoolValue("general","lazy100",false));
    m_fork = s_cfg.getBoolValue("general","fork",true);
    m_flags = s_cfg.getIntValue("general","flags",m_flags);
    NamedList *l = s_cfg.getSection("methods");
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = l->getParam(i);
	    if (!n)
		continue;
	    String meth(n->name());
	    meth.toUpper();
	    addAllowed(meth);
	}
    }
}

SIPTransaction* YateSIPEngine::forkInvite(SIPMessage* answer, SIPTransaction* trans)
{
    if (m_fork && trans->isActive() && (answer->code/100) == 2)
    {
	Debug(this,DebugNote,"Changing early dialog tag because of forked 2xx");
	trans->setDialogTag(answer->getParamValue("To","tag"));
	trans->processMessage(answer);
	return trans;
    }
    return SIPEngine::forkInvite(answer,trans);
}


bool YateSIPEngine::buildParty(SIPMessage* message)
{
    return m_ep->buildParty(message);
}

bool YateSIPEngine::copyAuthParams(NamedList* dest, const NamedList& src)
{
    // we added those and we want to exclude them from copy
    static TokenDict exclude[] = {
	{ "protocol", 1 },
	// purposely copy the username and realm
	{ "nonce", 1 },
	{ "method", 1 },
	{ "uri", 1 },
	{ "response", 1 },
	{ "ip_host", 1 },
	{ "ip_port", 1 },
	{ "address", 1 },
	{  0,   0 },
    };
    if (!dest)
	return true;
    unsigned int n = src.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* s = src.getParam(i);
	if (!s)
	    continue;
	if (s->name().toInteger(exclude,0))
	    continue;
	dest->setParam(s->name(),*s);
    }
    return true;
}

bool YateSIPEngine::checkUser(const String& username, const String& realm, const String& nonce,
    const String& method, const String& uri, const String& response,
    const SIPMessage* message, GenObject* userData)
{
    NamedList* params = YOBJECT(NamedList,userData);

    Message m("user.auth");
    m.addParam("protocol","sip");
    if (username) {
	m.addParam("username",username);
	m.addParam("realm",realm);
	m.addParam("nonce",nonce);
	m.addParam("response",response);
    }
    m.addParam("method",method);
    m.addParam("uri",uri);
    if (message) {
	m.addParam("ip_host",message->getParty()->getPartyAddr());
	m.addParam("ip_port",String(message->getParty()->getPartyPort()));
	String addr = message->getParty()->getPartyAddr();
	if (addr) {
	    addr << ":" << message->getParty()->getPartyPort();
	    m.addParam("address",addr);
	}
	// a dialogless INVITE could create a new call
	m.addParam("newcall",String::boolText((message->method == "INVITE") && !message->getParam("To","tag")));
	const MimeHeaderLine* hl = message->getHeader("From");
	if (hl) {
	    URI from(*hl);
	    m.addParam("domain",from.getHost());
	}
    }

    if (params) {
	const char* str = params->getValue("caller");
	if (str)
	    m.addParam("caller",str);
	str = params->getValue("called");
	if (str)
	    m.addParam("called",str);
    }

    if (!Engine::dispatch(m))
	return false;

    // empty password returned means authentication succeeded
    if (m.retValue().null())
	return copyAuthParams(params,m);
    // check for refusals
    if (m.retValue() == "-") {
	if (params) {
	    const char* err = m.getValue("error");
	    if (err)
		params->setParam("error",err);
	    err = m.getValue("reason");
	    if (err)
		params->setParam("reason",err);
	}
	return false;
    }
    // password works only with username
    if (!username)
	return false;

    String res;
    buildAuth(username,realm,m.retValue(),nonce,method,uri,res);
    if (res == response)
	return copyAuthParams(params,m);
    // if the URI included some parameters retry after stripping them off
    int sc = uri.find(';');
    bool ok = false;
    if (sc >= 0) {
	buildAuth(username,realm,m.retValue(),nonce,method,uri.substr(0,sc),res);
	ok = (res == response) && copyAuthParams(params,m);
    }

    if (!ok && !response.null()) {
	DDebug(&plugin,DebugNote,"Failed authentication for username='%s'",username.c_str());
	m_ep->incFailedAuths();
	plugin.changed();
    }
    return ok;
}

YateSIPEndPoint::YateSIPEndPoint(Thread::Priority prio)
    : Thread("YSIP EndPoint",prio),
      m_sock(0), m_engine(0),
      m_failedAuths(0),m_timedOutTrs(0), m_timedOutByes(0)
{
    Debug(&plugin,DebugAll,"YateSIPEndPoint::YateSIPEndPoint(%s) [%p]",
	Thread::priority(prio),this);
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

bool YateSIPEndPoint::buildParty(SIPMessage* message, const char* host, int port, const YateSIPLine* line)
{
    if (message->isAnswer())
	return false;
    DDebug(&plugin,DebugAll,"YateSIPEndPoint::buildParty(%p,'%s',%d,%p)",
	message,host,port,line);
    URI uri(message->uri);
    if (line) {
	if (!host)
	    host = line->getPartyAddr();
	if (port <= 0)
	    port = line->getPartyPort();
	line->setupAuth(message);
    }
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
    // reuse the variables now we finished with them
    host = line ? line->getLocalAddr().c_str() : (const char*)0;
    port = line ? line->getLocalPort() : 0;
    if (!host)
	host = m_local;
    if (port <= 0)
	port = m_port;
    YateUDPParty* party = new YateUDPParty(m_sock,addr,port,host);
    message->setParty(party);
    party->deref();
    return true;
}

bool YateSIPEndPoint::Init()
{
    if (m_sock) {
	Debug(&plugin,DebugInfo,"Already initialized.");
	return true;
    }

    m_sock = new Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!m_sock->valid()) {
	Debug(&plugin,DebugGoOn,"Unable to allocate UDP socket");
	return false;
    }

#ifdef SO_RCVBUF
    int reqlen = s_cfg.getIntValue("general","buffer");
    if (reqlen > 0) {
	int buflen = reqlen;
	if (buflen < 4096)
	    buflen = 4096;
	if (m_sock->setOption(SOL_SOCKET,SO_RCVBUF,&buflen,sizeof(buflen))) {
	    buflen = 0;
	    socklen_t sz = sizeof(buflen);
	    if (m_sock->getOption(SOL_SOCKET,SO_RCVBUF,&buflen,&sz))
		Debug(&plugin,DebugNote,"UDP buffer size is %d (requested %d)",buflen,reqlen);
	    else
		Debug(&plugin,DebugWarn,"Could not get UDP buffer size (requested %d)",reqlen);
	}
	else
	    Debug(&plugin,DebugWarn,"Could not set UDP buffer size %d",buflen);
    }
#endif
    
    SocketAddr addr(AF_INET);
    addr.port(s_cfg.getIntValue("general","port",5060));
    addr.host(s_cfg.getValue("general","addr","0.0.0.0"));

    if (!m_sock->bind(addr)) {
	Debug(&plugin,DebugWarn,"Unable to bind to preferred port - using random one instead");
	addr.port(0);
	if (!m_sock->bind(addr)) {
	    Debug(&plugin,DebugGoOn,"Unable to bind to any port");
	    return false;
	}
    }

    if (!m_sock->getSockName(addr)) {
	Debug(&plugin,DebugGoOn,"Unable to figure out what I'm bound to");
	return false;
    }
    if (!m_sock->setBlocking(false)) {
	Debug(&plugin,DebugGoOn,"Unable to set non-blocking mode");
	return false;
    }
    int maxpkt = s_cfg.getIntValue("general","maxpkt");
    if (maxpkt <= 0)
	maxpkt = 1500;
    else if (maxpkt > 65528)
	maxpkt = 65528;
    else if (maxpkt < 524)
	maxpkt = 524;
    m_buffer.assign(0,maxpkt);
    Debug(&plugin,DebugCall,"Started on %s:%d, max %d bytes",
	addr.host().safe(),addr.port(),maxpkt);
    if (addr.host() != "0.0.0.0")
	m_local = addr.host();
    m_port = addr.port();
    m_engine = new YateSIPEngine(this);
    m_engine->debugChain(&plugin);
    return true;
}

void YateSIPEndPoint::addMessage(const char* buf, int len, const SocketAddr& addr, int port)
{
    SIPMessage* msg = SIPMessage::fromParsing(0,buf,len);
    if (!msg)
	return;

    if (!msg->isAnswer()) {
	URI uri(msg->uri);
	YateSIPLine* line = plugin.findLine(addr.host(),addr.port(),uri.getUser());
	const char* host = 0;
	if (line && line->getLocalPort()) {
	    host = line->getLocalAddr();
	    port = line->getLocalPort();
	}
	if (!host)
	    host = m_local;
	if (port <= 0)
	    port = m_port;
	YateUDPParty* party = new YateUDPParty(m_sock,addr,port,host);
	msg->setParty(party);
	party->deref();
    }
    m_engine->addMessage(msg);
    msg->deref();
}

void YateSIPEndPoint::run()
{
    int evCount = 0;
    char* buf = (char*)m_buffer.data();

    for (;;)
    {
	bool ok = false;
	if ((s_floodEvents <= 1) || (evCount < s_floodEvents) || Engine::exiting())
	    ok = true;
	else if (evCount == s_floodEvents)
	    Debug(&plugin,DebugMild,"Flood detected: %d handled events",evCount);
	else if ((evCount % s_floodEvents) == 0)
	    Debug(&plugin,DebugWarn,"Severe flood detected: %d events",evCount);
	// in any case, try to read a packet now and then to keep up
	ok = ok || ((evCount & 3) == 0);
	if (ok) {
	    // wait up to the platform idle time if we had no events in last run
	    ok = false;
	    m_sock->select(&ok,0,0,((evCount <= 0) ? Thread::idleUsec() : 0));
	}
	if (ok)
	{
	    // we can read the data
	    int res = m_sock->recvFrom(buf,m_buffer.length()-1,m_addr);
	    if (res <= 0) {
		if (!m_sock->canRetry()) {
		    Debug(&plugin,DebugGoOn,"Error on read: %d", m_sock->error());
		}
	    } else if (res >= 72) {
		buf[res]=0;
		if (plugin.debugAt(DebugInfo)) {
		    String raddr;
		    raddr << m_addr.host() << ":" << m_addr.port();
		    if (plugin.filterDebug(raddr))
			Debug(&plugin,DebugInfo,"Received %d bytes SIP message from %s\r\n------\r\n%s------",
			    res,raddr.c_str(),buf);
		}
		// we got already the buffer and here we start to do "good" stuff
		addMessage(buf,res,m_addr,m_port);
		//m_engine->addMessage(new YateUDPParty(m_sock,m_addr,m_port),buf,res);
	    }
#ifdef DEBUG
	    else
		Debug(&plugin,DebugInfo,"Received short SIP message of %d bytes",res);
#endif
	}
	else
	    Thread::check();
	SIPEvent* e = m_engine->getEvent();
	if (e)
	    evCount++;
	else
	    evCount = 0;
	// hack: use a loop so we can use break and continue
	for (; e; m_engine->processEvent(e),e = 0) {
	    SIPTransaction* t = e->getTransaction();
	    if (!t)
		continue;
	    plugin.lock();

	    if (t->isOutgoing() && t->getResponseCode() == 408) {
	    	if (t->getMethod() == "BYE") {
		    DDebug(&plugin,DebugInfo,"BYE for transaction %p has timed out",t);
		    m_timedOutByes++;
		    plugin.changed();
		}
		if (e->getState() == SIPTransaction::Cleared && e->getUserData()) {
		    DDebug(&plugin,DebugInfo,"Transaction %p has timed out",t);
		    m_timedOutTrs++;
		    plugin.changed();
		}
	    }

	    GenObject* obj = static_cast<GenObject*>(t->getUserData());
	    RefPointer<YateSIPConnection> conn = YOBJECT(YateSIPConnection,obj);
	    YateSIPLine* line = YOBJECT(YateSIPLine,obj);
	    YateSIPGenerate* gen = YOBJECT(YateSIPGenerate,obj);
	    plugin.unlock();
	    if (conn) {
		if (conn->process(e)) {
		    delete e;
		    break;
		}
		else
		    continue;
	    }
	    if (line) {
		if (line->process(e)) {
		    delete e;
		    break;
		}
		else
		    continue;
	    }
	    if (gen) {
		if (gen->process(e)) {
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
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	if (conn) {
	    conn->doBye(t);
	    conn->deref();
	}
	else
	    t->setResponse(481);
    }
    else if (t->getMethod() == "CANCEL") {
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	if (conn) {
	    conn->doCancel(t);
	    conn->deref();
	}
	else
	    t->setResponse(481);
    }
    else if (t->getMethod() == "INFO") {
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	bool done = false;
	if (conn) {
	    done = conn->doInfo(t);
	    conn->deref();
	    if (!done)
		done = generic(e,t);
	}
	else if (t->getDialogTag()) {
	    done = true;
	    t->setResponse(481);
	}
	else
	    done = generic(e,t);
	if (!done)
	    t->setResponse(415);
    }
    else if (t->getMethod() == "REGISTER")
	regReq(e,t);
    else if (t->getMethod() == "OPTIONS")
	options(e,t);
    else if (t->getMethod() == "REFER") {
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	if (conn) {
	    conn->doRefer(t);
	    conn->deref();
	}
	else
	    t->setResponse(481);
    }
    else
	return generic(e,t);
    return true;
}

void YateSIPEndPoint::invite(SIPEvent* e, SIPTransaction* t)
{
    if (!plugin.canAccept()) {
	Debug(&plugin,DebugWarn,"Refusing new SIP call, full or exiting");
	t->setResponse(480);
	return;
    }

    if (e->getMessage()->getParam("To","tag")) {
	SIPDialog dlg(*e->getMessage());
	YateSIPConnection* conn = plugin.findDialog(dlg,true);
	if (conn) {
	    conn->reInvite(t);
	    conn->deref();
	}
	else {
	    Debug(&plugin,DebugWarn,"Got re-INVITE for missing dialog");
	    t->setResponse(481);
	}
	return;
    }

    YateSIPConnection* conn = new YateSIPConnection(e,t);
    conn->initChan();
    conn->startRouter();

}

void YateSIPEndPoint::regReq(SIPEvent* e, SIPTransaction* t)
{
    if (Engine::exiting()) {
	Debug(&plugin,DebugWarn,"Dropping request, engine is exiting");
	t->setResponse(500, "Server Shutting Down");
	return;
    }
    if (s_reg_async) {
	YateSIPRegister* reg = new YateSIPRegister(this,e->getMessage(),t);
	if (reg->startup())
	    return;
	Debug(&plugin,DebugWarn,"Failed to start register thread");
	delete reg;
    }
    regRun(e->getMessage(),t);
}

void YateSIPEndPoint::regRun(const SIPMessage* message, SIPTransaction* t)
{
    const MimeHeaderLine* hl = message->getHeader("Contact");
    if (!hl) {
	t->setResponse(400);
	return;
    }

    Message msg("user.register");
    msg.addParam("sip_uri",t->getURI());
    msg.addParam("sip_callid",t->getCallID());
    String user;
    int age = t->authUser(user,false,&msg);
    DDebug(&plugin,DebugAll,"User '%s' age %d",user.c_str(),age);
    if (((age < 0) || (age > 10)) && s_auth_register) {
	t->requestAuth(s_realm,"",age >= 0);
	return;
    }

    // TODO: track registrations, allow deregistering all
    if (*hl == "*") {
	t->setResponse(200);
	return;
    }

    URI addr(*hl);
    if (user.null())
	user = addr.getUser();
    msg.setParam("username",user);
    msg.setParam("number",addr.getUser());
    msg.setParam("driver","sip");
    String data(addr);
    bool nat = isNatBetween(addr.getHost(),message->getParty()->getPartyAddr());
    if (!nat) {
	int port = addr.getPort();
	if (!port)
	    port = 5060;
	nat = (message->getParty()->getPartyPort() != port) && msg.getBoolValue("nat_port_support",true);
    }
    bool natChanged = false;
    if (msg.getBoolValue("nat_support",s_auto_nat && nat)) {
	Debug(&plugin,DebugInfo,"Registration NAT detected: private '%s:%d' public '%s:%d'",
		    addr.getHost().c_str(),addr.getPort(),
		    message->getParty()->getPartyAddr().c_str(),
		    message->getParty()->getPartyPort());
	String tmp(addr.getHost());
	if (addr.getPort())
	    tmp << ":" << addr.getPort();
	msg.addParam("reg_nat_addr",tmp);
	int pos = data.find(tmp);
	if (pos >= 0) {
	    int len = tmp.length();
	    tmp.clear();
	    tmp << data.substr(0,pos) << message->getParty()->getPartyAddr()
		<< ":" << message->getParty()->getPartyPort() << data.substr(pos + len);
	    data = tmp;
	    natChanged = true;
	}
    }
    msg.setParam("data","sip/" + data);
    msg.setParam("ip_host",message->getParty()->getPartyAddr());
    msg.setParam("ip_port",String(message->getParty()->getPartyPort()));

    bool dereg = false;
    String tmp(message->getHeader("Expires"));
    if (tmp.null())
	tmp = hl->getParam("expires");
    int expires = tmp.toInteger(-1);
    if (expires < 0)
	expires = s_expires_def;
    if (expires > s_expires_max)
	expires = s_expires_max;
    if (expires && (expires < s_expires_min)) {
	tmp = s_expires_min;
	SIPMessage* r = new SIPMessage(t->initialMessage(),423);
	r->addHeader("Min-Expires",tmp);
	t->setResponse(r);
	r->deref();
	return;
    }
    tmp = expires;
    msg.setParam("expires",tmp);
    if (!expires) {
	msg = "user.unregister";
	dereg = true;
    }
    else
	msg.setParam("sip_to",addr);
    hl = message->getHeader("User-Agent");
    if (hl)
	msg.setParam("device",*hl);
    // Always OK deregistration attempts
    if (Engine::dispatch(msg) || dereg) {
	if (dereg) {
	    t->setResponse(200);
	    Debug(&plugin,DebugNote,"Unregistered user '%s'",user.c_str());
	}
	else {
	    tmp = msg.getValue("expires",tmp);
	    if (tmp.null())
		tmp = expires;
	    SIPMessage* r = new SIPMessage(t->initialMessage(),200);
	    r->addHeader("Expires",tmp);
	    MimeHeaderLine* contact = new MimeHeaderLine("Contact","<" + addr + ">");
	    contact->setParam("expires",tmp);
	    r->addHeader(contact);
	    if (natChanged) {
		if (s_nat_refresh > 0)
		    r->addHeader("P-NAT-Refresh",String(s_nat_refresh));
		r->addHeader("X-Real-Contact",data);
	    }
	    t->setResponse(r);
	    r->deref();
	    Debug(&plugin,DebugNote,"Registered user '%s' expires in %s s%s",
		user.c_str(),tmp.c_str(),natChanged ? " (NAT)" : "");
	}
    }
    else
	t->setResponse(404);
}

void YateSIPEndPoint::options(SIPEvent* e, SIPTransaction* t)
{
    const MimeHeaderLine* acpt = e->getMessage()->getHeader("Accept");
    if (acpt) {
	if (*acpt != "application/sdp") {
	    t->setResponse(415);
	    return;
	}
    }
    t->setResponse(200);
}

bool YateSIPEndPoint::generic(SIPEvent* e, SIPTransaction* t)
{
    String meth(t->getMethod());
    meth.toLower();
    String user;
    const String* auth = s_cfg.getKey("methods",meth);
    if (!auth)
	return false;
    Message m("sip." + meth);
    if (auth->toBoolean(true)) {
	int age = t->authUser(user,false,&m);
	DDebug(&plugin,DebugAll,"User '%s' age %d",user.c_str(),age);
	if ((age < 0) || (age > 10)) {
	    t->requestAuth(s_realm,"",age >= 0);
	    return true;
	}
    }

    const SIPMessage* message = e->getMessage();
    if (message->getParam("To","tag")) {
	SIPDialog dlg(*message);
	YateSIPConnection* conn = plugin.findDialog(dlg,true);
	if (conn) {
	    m.userData(conn);
	    conn->complete(m);
	    conn->deref();
	}
    }
    if (user)
	m.addParam("username",user);

    String tmp(message->getHeaderValue("Max-Forwards"));
    int maxf = tmp.toInteger(s_maxForwards);
    if (maxf > s_maxForwards)
	maxf = s_maxForwards;
    tmp = maxf-1;
    m.addParam("antiloop",tmp);

    m.addParam("ip_host",message->getParty()->getPartyAddr());
    m.addParam("ip_port",String(message->getParty()->getPartyPort()));
    m.addParam("sip_uri",t->getURI());
    m.addParam("sip_callid",t->getCallID());
    // establish the dialog here so user code will have the dialog tag handy
    t->setDialogTag();
    m.addParam("xsip_dlgtag",t->getDialogTag());
    copySipHeaders(m,*message,false);

    // add the body if it's a string one
    MimeStringBody* strBody = YOBJECT(MimeStringBody,message->body);
    if (strBody) {
	m.addParam("xsip_type",strBody->getType());
	m.addParam("xsip_body",strBody->text());
    }
    else {
	MimeLinesBody* txtBody = YOBJECT(MimeLinesBody,message->body);
	if (txtBody) {
	    String bodyText((const char*)txtBody->getBody().data(),txtBody->getBody().length());
	    m.addParam("xsip_type",txtBody->getType());
	    m.addParam("xsip_body",bodyText);
	}
    }

    int code = 0;
    if (Engine::dispatch(m)) {
	const String* ret = m.getParam("code");
	if (!ret)
	    ret = &m.retValue();
	code = ret->toInteger(m.getIntValue("reason",dict_errors,200));
    }
    else {
	code = m.getIntValue("code",m.getIntValue("reason",dict_errors,0));
	if (code < 300)
	    code = 0;
    }
    if ((code >= 200) && (code < 700)) {
	SIPMessage* resp = new SIPMessage(message,code);
	copySipHeaders(*resp,m);
	t->setResponse(resp);
	resp->deref();
	return true;
    }
    return false;
}


// Build the transfer thread
// transferorID: Channel id of the sip connection that received the REFER request
// transferredID: Channel id of the transferor's peer
// transferredDrv: Channel driver of the transferor's peer
// msg: already populated 'call.route'
// sipNotify: already populated SIPMessage("NOTIFY")
YateSIPRefer::YateSIPRefer(const String& transferorID, const String& transferredID,
    Driver* transferredDrv, Message* msg, SIPMessage* sipNotify,
    SIPTransaction* transaction)
    : Thread("YSIP Transfer"),
    m_transferorID(transferorID), m_transferredID(transferredID),
    m_transferredDrv(transferredDrv), m_msg(msg), m_sipNotify(sipNotify),
    m_notifyCode(200), m_transaction(0), m_rspCode(500)
{
    if (transaction && transaction->ref())
	m_transaction = transaction;
}

void YateSIPRefer::run()
{
    String* attended = m_msg->getParam("transfer_callid");
#ifdef DEBUG
    if (attended)
	Debug(&plugin,DebugAll,"%s(%s) running callid=%s fromtag=%s totag=%s [%p]",
	    name(),m_transferorID.c_str(),attended->c_str(),
	    m_msg->getValue("transfer_fromtag"),m_msg->getValue("transfer_totag"),this);
    else
	Debug(&plugin,DebugAll,"%s(%s) running [%p]",name(),m_transferorID.c_str(),this);
#endif

    // Use a while() to break to the end
    while (m_transferredDrv && m_msg) {
	// Attended transfer: check if the requested channel is owned by our plugin
	// NOTE: Remove the whole 'if' when a routing module will be able to route
	//  attended transfer requests
	if (attended) {
	    String* from = m_msg->getParam("transfer_fromtag");
	    String* to = m_msg->getParam("transfer_totag");
	    if (null(from) || null(to)) {
		m_rspCode = m_notifyCode = 487;     // Request Terminated
		break;
	    }
	    YateSIPConnection* conn = plugin.findDialog(*attended,*from,*to,true);
	    if (conn) {
		m_transferredDrv->lock();
		RefPointer<Channel> chan = m_transferredDrv->find(m_transferredID);
		m_transferredDrv->unlock();
		if (chan && conn->getPeer() && 
		    chan->connect(conn->getPeer(),m_msg->getValue("reason"))) {
		    m_rspCode = 202;
		    m_notifyCode = 200;
		}
		else
		    m_rspCode = m_notifyCode = 487;     // Request Terminated
		TelEngine::destruct(conn);
		break;
	    }
	    // Not ours
	    m_msg->clearParam("called");
	    YateSIPConnection::addCallId(*m_msg,*attended,*from,*to);
	}

	// Route the call
	bool ok = Engine::dispatch(m_msg);
	m_transferredDrv->lock();
	RefPointer<Channel> chan = m_transferredDrv->find(m_transferredID);
	m_transferredDrv->unlock();
	if (!(ok && chan)) {
#ifdef DEBUG
	    if (ok)
		Debug(&plugin,DebugAll,"%s(%s). Connection vanished while routing! [%p]",
		    name(),m_transferorID.c_str(),this);
	    else
		Debug(&plugin,DebugAll,"%s(%s). 'call.route' failed [%p]",
		    name(),m_transferorID.c_str(),this);
#endif
	    m_rspCode = m_notifyCode = (ok ? 487 : 481);
	    break;
	}
	m_msg->userData(chan);
	if ((m_msg->retValue() == "-") || (m_msg->retValue() == "error"))
	    m_rspCode = m_notifyCode = 603; // Decline
	else if (m_msg->getIntValue("antiloop",1) <= 0)
	    m_rspCode = m_notifyCode = 482; // Loop Detected
	else {
	    DDebug(&plugin,DebugAll,"%s(%s). Call succesfully routed [%p]",
		name(),m_transferorID.c_str(),this);
	    *m_msg = "call.execute";
	    m_msg->setParam("callto",m_msg->retValue());
	    m_msg->clearParam("error");
	    m_msg->retValue().clear();
	    if (Engine::dispatch(m_msg)) {
		DDebug(&plugin,DebugAll,"%s(%s). 'call.execute' succeeded [%p]",
		    name(),m_transferorID.c_str(),this);
		m_rspCode = 202;
		m_notifyCode = 200;
	    }
	    else {
		DDebug(&plugin,DebugAll,"%s(%s). 'call.execute' failed [%p]",
		    name(),m_transferorID.c_str(),this);
		m_rspCode = m_notifyCode = 603; // Decline
	    }
	}
	break;
    }
    release();
}

// Respond the transaction and deref() it
void YateSIPRefer::setTrResponse(int code)
{
    if (!m_transaction)
	return;
    SIPTransaction* t = m_transaction;
    m_transaction = 0;
    m_rspCode = code;
    t->setResponse(m_rspCode);
    TelEngine::destruct(t);
}

// Set transaction response. Send the notification message. Notify the
// connection and release other objects
void YateSIPRefer::release(bool fromCleanup)
{
    setTrResponse(m_rspCode);
    TelEngine::destruct(m_msg);
    // Set NOTIFY response and send it (only if the transaction was accepted)
    if (m_sipNotify) {
	if (m_rspCode < 300 && plugin.ep() && plugin.ep()->engine()) {
	    String s;
	    s << "SIP/2.0 " << m_notifyCode << " " << lookup(m_notifyCode,SIPResponses) << "\r\n";
	    m_sipNotify->setBody(new MimeStringBody("message/sipfrag;version=2.0",s));
	    plugin.ep()->engine()->addMessage(m_sipNotify);
	    m_sipNotify = 0;
	}
	else
	    TelEngine::destruct(m_sipNotify);
	// If we still have a NOTIFY message in cleanup() the thread
	//  was cancelled in the hard way
	if (fromCleanup)
	    Debug(&plugin,DebugWarn,"YateSIPRefer(%s) thread terminated abnormally [%p]",
		m_transferorID.c_str(),this);
    }
    // Notify transferor on termination
    if (m_transferorID) {
	plugin.lock();
	YateSIPConnection* conn = static_cast<YateSIPConnection*>(plugin.find(m_transferorID));
	if (conn)
	    conn->referTerminated();
	plugin.unlock();
	m_transferorID = "";
    }
}


// Incoming call constructor - just before starting the routing thread
YateSIPConnection::YateSIPConnection(SIPEvent* ev, SIPTransaction* tr)
    : Channel(plugin,0,false),
      SDPSession(&plugin.parser()),
      m_tr(tr), m_tr2(0), m_hungup(false), m_byebye(true), m_cancel(false),
      m_state(Incoming), m_port(0), m_route(0), m_routes(0),
      m_authBye(true), m_inband(s_inband), m_info(s_info),
      m_referring(false), m_reInviting(ReinviteNone), m_lastRseq(0)
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
    filterDebug(m_address);
    m_uri = m_tr->initialMessage()->getHeader("From");
    m_uri.parse();
    m_tr->setUserData(this);

    URI uri(m_tr->getURI());
    YateSIPLine* line = plugin.findLine(m_host,m_port,uri.getUser());
    Message *m = message("call.preroute");
    decodeIsupBody(*m,m_tr->initialMessage()->body);
    m->addParam("caller",m_uri.getUser());
    m->addParam("called",uri.getUser());
    if (m_uri.getDescription())
	m->addParam("callername",m_uri.getDescription());
    const MimeHeaderLine* hl = m_tr->initialMessage()->getHeader("Call-Info");
    if (hl) {
	const NamedString* type = hl->getParam("purpose");
	if (!type || *type == "info")
	    m->addParam("caller_info_uri",*hl);
	else if (*type == "icon")
	    m->addParam("caller_icon_uri",*hl);
	else if (*type == "card")
	    m->addParam("caller_card_uri",*hl);
    }

    if (line) {
	// call comes from line we have registered to - trust it...
	m_user = line->getUserName();
	m_externalAddr = line->getLocalAddr();
	m_line = *line;
	m_domain = line->domain();
	m->addParam("username",m_user);
	m->addParam("domain",m_domain);
	m->addParam("in_line",m_line);
    }
    else {
	String user;
	int age = tr->authUser(user,false,m);
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
	m_domain = m->getValue("domain");
    }
    if (s_privacy)
	copyPrivacy(*m,*ev->getMessage());

    String tmp(ev->getMessage()->getHeaderValue("Max-Forwards"));
    int maxf = tmp.toInteger(s_maxForwards);
    if (maxf > s_maxForwards)
	maxf = s_maxForwards;
    tmp = maxf-1;
    m->addParam("antiloop",tmp);
    m->addParam("ip_host",m_host);
    m->addParam("ip_port",String(m_port));
    m->addParam("sip_uri",uri);
    m->addParam("sip_from",m_uri);
    m->addParam("sip_to",ev->getMessage()->getHeaderValue("To"));
    m->addParam("sip_callid",m_callid);
    m->addParam("device",ev->getMessage()->getHeaderValue("User-Agent"));
    copySipHeaders(*m,*ev->getMessage());
    const char* reason = 0;
    hl = m_tr->initialMessage()->getHeader("Referred-By");
    if (hl)
	reason = "transfer";
    else {
	hl = m_tr->initialMessage()->getHeader("Diversion");
	if (hl) {
	    reason = "divert";
	    const String* par = hl->getParam("reason");
	    if (par) {
		tmp = par->c_str();
		MimeHeaderLine::delQuotes(tmp);
		if (tmp.trimBlanks())
		    m->addParam("divert_reason",tmp);
	    }
	    par = hl->getParam("privacy");
	    if (par) {
		tmp = par->c_str();
		MimeHeaderLine::delQuotes(tmp);
		if (tmp.trimBlanks())
		    m->addParam("divert_privacy",tmp);
	    }
	    par = hl->getParam("screen");
	    if (par) {
		tmp = par->c_str();
		MimeHeaderLine::delQuotes(tmp);
		if (tmp.trimBlanks())
		    m->addParam("divert_screen",tmp);
	    }
	}
    }
    if (hl) {
	URI div(*hl);
	m->addParam("diverter",div.getUser());
	if (div.getDescription())
	    m->addParam("divertername",div.getDescription());
	m->addParam("diverteruri",div);
    }
    m_rtpLocalAddr = s_rtpip;
    MimeSdpBody* sdp = getSdpBody(ev->getMessage()->body);
    if (sdp) {
	setMedia(plugin.parser().parse(sdp,m_rtpAddr,m_rtpMedia));
	if (m_rtpMedia) {
	    m_rtpForward = true;
	    // guess if the call comes from behind a NAT
	    bool nat = isNatBetween(m_rtpAddr,m_host);
	    if (m->getBoolValue("nat_support",s_auto_nat && nat)) {
		Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		    m_rtpAddr.c_str(),m_host.c_str());
		m->addParam("rtp_nat_addr",m_rtpAddr);
		m_rtpAddr = m_host;
	    }
	    m->addParam("rtp_addr",m_rtpAddr);
	    putMedia(*m);
	}
	if (plugin.parser().sdpForward()) {
	    const DataBlock& raw = sdp->getBody();
	    String tmp((const char*)raw.data(),raw.length());
	    m->addParam("sdp_raw",tmp);
	    m_rtpForward = true;
	}
	if (m_rtpForward)
	    m->addParam("rtp_forward","possible");
    }
    DDebug(this,DebugAll,"RTP addr '%s' [%p]",m_rtpAddr.c_str(),this);
    if (reason)
	m->addParam("reason",reason);
    m_route = m;
    Message* s = message("chan.startup");
    s->addParam("caller",m_uri.getUser());
    s->addParam("called",uri.getUser());
    if (m_user)
	s->addParam("username",m_user);
    Engine::enqueue(s);
}

// Outgoing call constructor - in call.execute handler
YateSIPConnection::YateSIPConnection(Message& msg, const String& uri, const char* target)
    : Channel(plugin,0,true),
      SDPSession(&plugin.parser()),
      m_tr(0), m_tr2(0), m_hungup(false), m_byebye(true), m_cancel(true),
      m_state(Outgoing), m_port(0), m_route(0), m_routes(0),
      m_authBye(false), m_inband(s_inband), m_info(s_info),
      m_referring(false), m_reInviting(ReinviteNone), m_lastRseq(0)
{
    Debug(this,DebugAll,"YateSIPConnection::YateSIPConnection(%p,'%s') [%p]",
	&msg,uri.c_str(),this);
    m_targetid = target;
    setReason();
    m_inband = msg.getBoolValue("dtmfinband",s_inband);
    m_info = msg.getBoolValue("dtmfinfo",s_info);
    m_secure = msg.getBoolValue("secure",plugin.parser().secure());
    setRfc2833(msg.getParam("rfc2833"));
    m_rtpForward = msg.getBoolValue("rtp_forward");
    m_user = msg.getValue("user");
    m_line = msg.getValue("line");
    String tmp;
    YateSIPLine* line = 0;
    if (m_line) {
	line = plugin.findLine(m_line);
	if (line) {
	    if (uri.find('@') < 0 && !uri.startsWith("tel:")) {
		if (!uri.startsWith("sip:"))
		    tmp = "sip:";
		tmp << uri << "@" << line->domain();
	    }
	    m_externalAddr = line->getLocalAddr();
	}
    }
    if (tmp.null()) {
	if (!(uri.startsWith("tel:") || uri.startsWith("sip:"))) {
	    int sep = uri.find(':');
	    if ((sep < 0) || ((sep > 0) && (uri.substr(sep+1).toInteger(-1) > 0)))
		tmp = "sip:";
	}
	tmp << uri;
    }
    m_uri = tmp;
    m_uri.parse();
    SIPMessage* m = new SIPMessage("INVITE",m_uri);
    plugin.ep()->buildParty(m,msg.getValue("host"),msg.getIntValue("port"),line);
    if (!m->getParty()) {
	Debug(this,DebugWarn,"Could not create party for '%s' [%p]",m_uri.c_str(),this);
	TelEngine::destruct(m);
	tmp = "Invalid address: ";
	tmp << m_uri;
	msg.setParam("reason",tmp);
	setReason(tmp,500);
	return;
    }
    int maxf = msg.getIntValue("antiloop",s_maxForwards);
    m->addHeader("Max-Forwards",String(maxf));
    copySipHeaders(*m,msg);
    m_domain = msg.getValue("domain");
    const String* callerId = msg.getParam("caller");
    String caller;
    if (callerId)
	caller = *callerId;
    else if (line) {
	caller = line->getUserName();
	callerId = &caller;
	m_domain = line->domain(m_domain);
    }
    String display = msg.getValue("callername",(line ? line->getFullName().c_str() : (const char*)0));
    m->complete(plugin.ep()->engine(),
	callerId ? (callerId->null() ? "anonymous" : callerId->c_str()) : (const char*)0,
	m_domain,
	0,
	msg.getIntValue("xsip_flags",-1));
    if (display) {
	MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(m->getHeader("From"));
	if (hl) {
	    MimeHeaderLine::addQuotes(display);
	    *hl = display + " " + *hl;
	}
    }
    if (msg.getParam("calledname")) {
	display = msg.getValue("calledname");
	MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(m->getHeader("To"));
	if (hl) {
	    MimeHeaderLine::addQuotes(display);
	    *hl = display + " " + *hl;
	}
    }
    if (plugin.ep()->engine()->prack())
	m->addHeader("Supported","100rel");
    m_host = m->getParty()->getPartyAddr();
    m_port = m->getParty()->getPartyPort();
    m_address << m_host << ":" << m_port;
    filterDebug(m_address);
    m_dialog = *m;
    if (s_privacy)
	copyPrivacy(*m,msg);

    // Check if this is a transferred call
    String* diverter = msg.getParam("diverter");
    if (!null(diverter)) {
	const MimeHeaderLine* from = m->getHeader("From");
	if (from) {
	    URI fr(*from);
	    URI d(fr.getProtocol(),*diverter,fr.getHost(),fr.getPort(),
		msg.getValue("divertername",""));
	    String* reason = msg.getParam("divert_reason");
	    String* privacy = msg.getParam("divert_privacy");
	    String* screen = msg.getParam("divert_screen");
	    bool divert = !(TelEngine::null(reason) && TelEngine::null(privacy) && TelEngine::null(screen));
	    divert = msg.getBoolValue("diversion",divert);
	    MimeHeaderLine* hl = new MimeHeaderLine(divert ? "Diversion" : "Referred-By",d);
	    if (divert) {
		if (!TelEngine::null(reason))
		    hl->setParam("reason",MimeHeaderLine::quote(*reason));
		if (!TelEngine::null(privacy))
		    hl->setParam("privacy",MimeHeaderLine::quote(*privacy));
		if (!TelEngine::null(screen))
		    hl->setParam("screen",MimeHeaderLine::quote(*screen));
	    }
	    m->addHeader(hl);
	}
    }

    // add some Call-Info headers
    const char* info = msg.getValue("caller_info_uri");
    if (info) {
	MimeHeaderLine* hl = new MimeHeaderLine("Call-Info",info);
	hl->setParam("purpose","info");
	m->addHeader(hl);
    }
    info = msg.getValue("caller_icon_uri");
    if (info) {
	MimeHeaderLine* hl = new MimeHeaderLine("Call-Info",info);
	hl->setParam("purpose","icon");
	m->addHeader(hl);
    }
    info = msg.getValue("caller_card_uri");
    if (info) {
	MimeHeaderLine* hl = new MimeHeaderLine("Call-Info",info);
	hl->setParam("purpose","card");
	m->addHeader(hl);
    }

    m_rtpLocalAddr = msg.getValue("rtp_localip",s_rtpip);
    MimeSdpBody* sdp = createPasstroughSDP(msg);
    if (!sdp)
	sdp = createRtpSDP(m_host,msg);
    m->setBody(buildSIPBody(msg,sdp));
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_callid = m_tr->getCallID();
	m_tr->setUserData(this);
    }
    m->deref();
    setMaxcall(msg);
    Message* s = message("chan.startup",msg);
    s->setParam("caller",caller);
    s->copyParams(msg,"callername,called,billid,callto,username");
    s->setParam("calledfull",m_uri.getUser());
    if (m_callid)
	s->setParam("sip_callid",m_callid);
    Engine::enqueue(s);
}

YateSIPConnection::~YateSIPConnection()
{
    Debug(this,DebugAll,"YateSIPConnection::~YateSIPConnection() [%p]",this);
}

void YateSIPConnection::destroyed()
{
    DDebug(this,DebugAll,"YateSIPConnection::destroyed() [%p]",this);
    hangup();
    clearTransaction();
    TelEngine::destruct(m_route);
    TelEngine::destruct(m_routes);
    Channel::destroyed();
}

void YateSIPConnection::startRouter()
{
    Message* m = m_route;
    m_route = 0;
    Channel::startRouter(m);
}

void YateSIPConnection::clearTransaction()
{
    if (!(m_tr || m_tr2))
	return;
    Lock lock(driver());
    if (m_tr) {
	m_tr->setUserData(0);
	if (m_tr->setResponse()) {
	    SIPMessage* m = new SIPMessage(m_tr->initialMessage(),m_reasonCode,
		m_reason.safe("Request Terminated"));
	    copySipHeaders(*m,parameters(),0);
	    m_tr->setResponse(m);
	    TelEngine::destruct(m);
	    m_byebye = false;
	}
	else if (m_hungup && m_tr->isIncoming() && m_dialog.localTag.null())
	    m_dialog.localTag = m_tr->getDialogTag();
	m_tr->deref();
	m_tr = 0;
    }
    // cancel any pending reINVITE
    if (m_tr2) {
	m_tr2->setUserData(0);
	if (m_tr2->isIncoming())
	    m_tr2->setResponse(487);
	m_tr2->deref();
	m_tr2 = 0;
    }
}

void YateSIPConnection::detachTransaction2()
{
    Lock lock(driver());
    if (m_tr2) {
	m_tr2->setUserData(0);
	m_tr2->deref();
	m_tr2 = 0;
	if (m_reInviting != ReinvitePending)
	    m_reInviting = ReinviteNone;
    }
    startPendingUpdate();
}

void YateSIPConnection::hangup()
{
    if (m_hungup)
	return;
    m_hungup = true;
    const char* error = lookup(m_reasonCode,dict_errors);
    Debug(this,DebugAll,"YateSIPConnection::hangup() state=%d trans=%p error='%s' code=%d reason='%s' [%p]",
	m_state,m_tr,error,m_reasonCode,m_reason.c_str(),this);
    setMedia(0);
    Message* m = message("chan.hangup");
    if (m_reason)
	m->setParam("reason",m_reason);
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
	    if (m_cancel && m_tr) {
		SIPMessage* m = new SIPMessage("CANCEL",m_uri);
		plugin.ep()->buildParty(m,m_host,m_port,plugin.findLine(m_line));
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
		    if (m_reason == "pickup") {
			MimeHeaderLine* hl = new MimeHeaderLine("Reason","SIP");
			hl->setParam("cause","200");
			hl->setParam("text","\"Call completed elsewhere\"");
			m->addHeader(hl);
		    }
		    plugin.ep()->engine()->addMessage(m);
		}
		m->deref();
	    }
	    break;
    }
    clearTransaction();
    m_state = Cleared;

    if (m_byebye && m_dialog.localTag && m_dialog.remoteTag) {
	SIPMessage* m = createDlgMsg("BYE");
	if (m) {
	    if (m_reason) {
		// FIXME: add SIP and Q.850 cause codes, set the proper reason
		MimeHeaderLine* hl = new MimeHeaderLine("Reason","SIP");
		if ((m_reasonCode >= 300) && (m_reasonCode <= 699) && (m_reasonCode != 487))
		    hl->setParam("cause",String(m_reasonCode));
		hl->setParam("text",MimeHeaderLine::quote(m_reason));
		m->addHeader(hl);
	    }
	    const char* stats = parameters().getValue("rtp_stats");
	    if (stats)
		m->addHeader("P-RTP-Stat",stats);
	    copySipHeaders(*m,parameters(),0);
	    plugin.ep()->engine()->addMessage(m);
	    m->deref();
	}
    }
    m_byebye = false;
    if (!error)
	error = m_reason.c_str();
    disconnect(error,parameters());
}

// Creates a new message in an existing dialog
SIPMessage* YateSIPConnection::createDlgMsg(const char* method, const char* uri)
{
    if (!uri)
	uri = m_uri;
    SIPMessage* m = new SIPMessage(method,uri);
    m->addRoutes(m_routes);
    plugin.ep()->buildParty(m,m_host,m_port,plugin.findLine(m_line));
    if (!m->getParty()) {
	Debug(this,DebugWarn,"Could not create party for '%s:%d' [%p]",
	    m_host.c_str(),m_port,this);
	m->destruct();
	return 0;
    }
    m->addHeader("Call-ID",m_callid);
    String tmp;
    tmp << "<" << m_dialog.localURI << ">";
    MimeHeaderLine* hl = new MimeHeaderLine("From",tmp);
    tmp = m_dialog.localTag;
    if (tmp.null() && m_tr)
	tmp = m_tr->getDialogTag();
    if (tmp)
	hl->setParam("tag",tmp);
    m->addHeader(hl);
    tmp.clear();
    tmp << "<" << m_dialog.remoteURI << ">";
    hl = new MimeHeaderLine("To",tmp);
    tmp = m_dialog.remoteTag;
    if (tmp.null() && m_tr)
	tmp = m_tr->getDialogTag();
    if (tmp)
	hl->setParam("tag",tmp);
    m->addHeader(hl);
    return m;
}

// Emit a call.update to notify cdrbuild of callid dialog tags change
void YateSIPConnection::emitUpdate()
{
    Message* m = message("call.update");
    m->addParam("operation","cdrbuild");
    Engine::enqueue(m);
}

// Emit a PRovisional ACK if enabled in the engine, return true to handle them
bool YateSIPConnection::emitPRACK(const SIPMessage* msg)
{
    if (!(msg && msg->isAnswer() && (msg->code > 100) && (msg->code < 200)))
	return false;
    if (!plugin.ep()->engine()->prack())
	return true;
    const MimeHeaderLine* rs = msg->getHeader("RSeq");
    const MimeHeaderLine* cs = msg->getHeader("CSeq");
    if (!(rs && cs))
	return true;
    int seq = rs->toInteger(0,10);
    // return false only if we already seen this provisional response
    if (seq == m_lastRseq)
	return false;
    if (seq < m_lastRseq) {
	Debug(this,DebugMild,"Not sending PRACK for RSeq %d < %d [%p]",
	    seq,m_lastRseq,this);
	return false;
    }
    String tmp;
    const MimeHeaderLine* co = msg->getHeader("Contact");
    if (co) {
	tmp = *co;
	static const Regexp r("^[^<]*<\\([^>]*\\)>.*$");
	if (tmp.matches(r))
	    tmp = tmp.matchString(1);
    }
    SIPMessage* m = createDlgMsg("PRACK",tmp);
    if (!m)
	return true;
    m_lastRseq = seq;
    tmp = *rs;
    tmp << " " << *cs;
    m->addHeader("RAck",tmp);
    plugin.ep()->engine()->addMessage(m);
    m->deref();
    return true;
}

// Creates a SDP for provisional (1xx) messages
MimeSdpBody* YateSIPConnection::createProvisionalSDP(Message& msg)
{
    if (!msg.getBoolValue("earlymedia",true))
	return 0;
    if (m_rtpForward)
	return createPasstroughSDP(msg);
    // check if our peer can source at least audio data
    if (!(getPeer() && getPeer()->getSource()))
	return 0;
    if (m_rtpAddr.null())
	return 0;
    if (s_1xx_formats)
	updateFormats(msg);
    return createRtpSDP(true);
}

// Build and populate a chan.rtp message
Message* YateSIPConnection::buildChanRtp(RefObject* context)
{
    Message* m = new Message("chan.rtp");
    if (context)
	m->userData(context);
    else {
	complete(*m,true);
	m->addParam("call_direction",direction());
	m->addParam("call_address",address());
	m->addParam("call_status",status());
	m->addParam("call_billid",billid());
	m->userData(static_cast<CallEndpoint*>(this));
    }
    return m;
}

// Media changed notification, reimplemented from SDPSession
void YateSIPConnection::mediaChanged(const SDPMedia& media)
{
    SDPSession::mediaChanged(media);
    if (media.id() && media.transport()) {
	Message m("chan.rtp");
	m.addParam("rtpid",media.id());
	m.addParam("media",media);
	m.addParam("transport",media.transport());
	m.addParam("terminate",String::boolText(true));
	m.addParam("call_direction",direction());
	m.addParam("call_address",address());
	m.addParam("call_status",status());
	m.addParam("call_billid",billid());
	Engine::dispatch(m);
	const char* stats = m.getValue("stats");
	if (stats)
	    parameters().setParam("rtp_stats"+media.suffix(),stats);
    }
    // Clear the data endpoint, will be rebuilt later if required
    clearEndpoint(media);
}

// Process SIP events belonging to this connection
bool YateSIPConnection::process(SIPEvent* ev)
{
    const SIPMessage* msg = ev->getMessage();
    int code = ev->getTransaction()->getResponseCode();
    DDebug(this,DebugInfo,"YateSIPConnection::process(%p) %s %s code=%d [%p]",
	ev,ev->isActive() ? "active" : "inactive",
	SIPTransaction::stateName(ev->getState()),code,this);
#ifdef XDEBUG
    if (msg)
	Debug(this,DebugInfo,"Message %p '%s' %s %s code=%d body=%p",
	    msg,msg->method.c_str(),
	    msg->isOutgoing() ? "outgoing" : "incoming",
	    msg->isAnswer() ? "answer" : "request",
	    msg->code,msg->body);
#endif

    Lock mylock(driver());
    if (ev->getTransaction() == m_tr2) {
	mylock.drop();
	return processTransaction2(ev,msg,code);
    }

    bool updateTags = true;
    SIPDialog oldDlg(m_dialog);
    m_dialog = *ev->getTransaction()->recentMessage();
    mylock.drop();

    if (msg && !msg->isOutgoing() && msg->isAnswer() && (code >= 300) && (code <= 699)) {
	updateTags = false;
	m_cancel = false;
	m_byebye = false;
	parameters().clearParams();
	parameters().addParam("cause_sip",String(code));
	parameters().addParam("reason_sip",msg->reason);
	setReason(msg->reason,code);
	if (msg->body) {
	    Message tmp("isup.decode");
	    if (decodeIsupBody(tmp,msg->body))
		parameters().copyParams(tmp);
	}
	copySipHeaders(parameters(),*msg);
	if (code < 400) {
	    // this is a redirect, it should provide a Contact and possibly a Diversion
	    const MimeHeaderLine* hl = msg->getHeader("Contact");
	    if (hl) {
		parameters().addParam("redirect",String::boolText(true));
		URI uri(*hl);
		parameters().addParam("called",uri.getUser());
		if (uri.getDescription())
		    parameters().addParam("calledname",uri.getDescription());
		parameters().addParam("calleduri",uri);
		hl = msg->getHeader("Diversion");
		if (hl) {
		    uri = *hl;
		    parameters().addParam("diverter",uri.getUser());
		    if (uri.getDescription())
			parameters().addParam("divertername",uri.getDescription());
		    parameters().addParam("diverteruri",uri);
		    String tmp = hl->getParam("reason");
		    MimeHeaderLine::delQuotes(tmp);
		    if (tmp.trimBlanks())
			parameters().addParam("divert_reason",tmp);
		    tmp = hl->getParam("privacy");
		    MimeHeaderLine::delQuotes(tmp);
		    if (tmp.trimBlanks())
			parameters().addParam("divert_privacy",tmp);
		    tmp = hl->getParam("screen");
		    MimeHeaderLine::delQuotes(tmp);
		    if (tmp.trimBlanks())
			parameters().addParam("divert_screen",tmp);
		}
	    }
	    else
		Debug(this,DebugMild,"Received %d redirect without Contact [%p]",code,this);
	}
	hangup();
    }
    else if (code == 408) {
	// Proxy timeout does not provide an answer message
	updateTags = false;
	if (m_dialog.remoteTag.null())
	    m_byebye = false;
	parameters().setParam("cause_sip","408");
	setReason("Request Timeout",code);
	hangup();
    }

    // Only update channels' callid if dialog tags change
    if (updateTags) {
	Lock lock(driver());
	updateTags = (oldDlg |= m_dialog);
    }

    if (!ev->isActive()) {
	Lock lock(driver());
	if (m_tr) {
	    DDebug(this,DebugInfo,"YateSIPConnection clearing transaction %p [%p]",
		m_tr,this);
	    m_tr->setUserData(0);
	    m_tr->deref();
	    m_tr = 0;
	}
	if (m_state != Established)
	    hangup();
	else if (s_ack_required && (code == 408)) {
	    // call was established but we didn't got the ACK
	    setReason("Not received ACK",code);
	    hangup();
	}
	else {
	    if (updateTags)
		emitUpdate();
	    startPendingUpdate();
	}
	return false;
    }
    if (!msg || msg->isOutgoing()) {
	if (updateTags)
	    emitUpdate();
	return false;
    }
    String natAddr;
    MimeSdpBody* sdp = getSdpBody(msg->body);
    if (sdp) {
	DDebug(this,DebugInfo,"YateSIPConnection got SDP [%p]",this);
	setMedia(plugin.parser().parse(sdp,m_rtpAddr,m_rtpMedia));
	// guess if the call comes from behind a NAT
	if (s_auto_nat && isNatBetween(m_rtpAddr,m_host)) {
	    Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		m_rtpAddr.c_str(),m_host.c_str());
	    natAddr = m_rtpAddr;
	    m_rtpAddr = m_host;
	}
	DDebug(this,DebugAll,"RTP addr '%s' [%p]",m_rtpAddr.c_str(),this);
    }
    if ((!m_routes) && msg->isAnswer() && (msg->code > 100) && (msg->code < 300))
	m_routes = msg->getRoutes();

    if (msg->isAnswer() && m_externalAddr.null() && m_line) {
	// see if we should detect our external address
	const YateSIPLine* line = plugin.findLine(m_line);
	if (line && line->localDetect()) {
	    const MimeHeaderLine* hl = msg->getHeader("Via");
	    if (hl) {
		const NamedString* par = hl->getParam("received");
		if (par && *par) {
		    m_externalAddr = *par;
		    Debug(this,DebugInfo,"Detected local address '%s' [%p]",
			m_externalAddr.c_str(),this);
		}
	    }
	}
    }

    if (msg->isAnswer() && ((msg->code / 100) == 2)) {
	updateTags = false;
	m_cancel = false;
	Lock lock(driver());
	const SIPMessage* ack = m_tr ? m_tr->latestMessage() : 0;
	if (ack && ack->isACK()) {
	    // accept any URI change caused by a Contact: header in the 2xx
	    m_uri = ack->uri;
	    m_uri.parse();
	    DDebug(this,DebugInfo,"YateSIPConnection clearing answered transaction %p [%p]",
		m_tr,this);
	    m_tr->setUserData(0);
	    m_tr->deref();
	    m_tr = 0;
	}
	lock.drop();
	setReason("",0);
	setStatus("answered",Established);
	Message *m = message("call.answered");
	copySipHeaders(*m,*msg);
	decodeIsupBody(*m,msg->body);
	addRtpParams(*m,natAddr,msg->body);
	Engine::enqueue(m);
	startPendingUpdate();
    }
    if (emitPRACK(msg)) {
	if (s_multi_ringing || (m_state < Ringing)) {
	    const char* name = "call.progress";
	    const char* reason = 0;
	    switch (msg->code) {
		case 180:
		    updateTags = false;
		    name = "call.ringing";
		    setStatus("ringing",Ringing);
		    break;
		case 181:
		    reason = "forwarded";
		    setStatus("progressing");
		    break;
		case 182:
		    reason = "queued";
		    setStatus("progressing");
		    break;
		case 183:
		    setStatus("progressing");
		    break;
		// for all others emit a call.progress but don't change status
	    }
	    if (name) {
		Message* m = message(name);
		copySipHeaders(*m,*msg);
		decodeIsupBody(*m,msg->body);
		if (reason)
		    m->addParam("reason",reason);
		addRtpParams(*m,natAddr,msg->body);
		if (m_rtpAddr.null())
		    m->addParam("earlymedia","false");
		Engine::enqueue(m);
	    }
	}
    }
    if (updateTags)
	emitUpdate();
    if (msg->isACK()) {
	DDebug(this,DebugInfo,"YateSIPConnection got ACK [%p]",this);
	startRtp();
    }
    return false;
}

// Process secondary transaction (reINVITE)  belonging to this connection
bool YateSIPConnection::processTransaction2(SIPEvent* ev, const SIPMessage* msg, int code)
{
    if (ev->getState() == SIPTransaction::Cleared) {
	bool fatal = (m_reInviting == ReinviteRequest);
	detachTransaction2();
	if (fatal) {
	    setReason("Request Timeout",408);
	    hangup();
	}
	else {
	    Message* m = message("call.update");
	    m->addParam("operation","reject");
	    m->addParam("error","timeout");
	    Engine::enqueue(m);
	}
	return false;
    }
    if (!msg || msg->isOutgoing() || !msg->isAnswer())
	return false;
    if (code < 200)
	return false;

    if (m_reInviting == ReinviteRequest) {
	detachTransaction2();
	// we emitted a client reINVITE, now we are forced to deal with it
	if (code < 300) {
	    MimeSdpBody* sdp = getSdpBody(msg->body);
	    while (sdp) {
		String addr;
		ObjList* lst = plugin.parser().parse(sdp,addr);
		if (!lst)
		    break;
		if ((addr == m_rtpAddr) || isNatBetween(addr,m_host)) {
		    ObjList* l = m_rtpMedia;
		    for (; l; l = l->next()) {
			SDPMedia* m = static_cast<SDPMedia*>(l->get());
			if (!m)
			    continue;
			SDPMedia* m2 = static_cast<SDPMedia*>((*lst)[*m]);
			if (!m2)
			    continue;
			// both old and new media exist, compare ports
			if (m->remotePort() != m2->remotePort()) {
			    DDebug(this,DebugWarn,"Port for '%s' changed: '%s' -> '%s' [%p]",
				m->c_str(),m->remotePort().c_str(),
				m2->remotePort().c_str(),this);
			    TelEngine::destruct(lst);
			    break;
			}
		    }
		    if (lst) {
			setMedia(lst);
			return false;
		    }
		}
		TelEngine::destruct(lst);
		setReason("Media information changed during reINVITE",415);
		hangup();
		return false;
	    }
	    setReason("Missing media information",415);
	}
	else
	    setReason(msg->reason,code);
	hangup();
	return false;
    }

    Message* m = message("call.update");
    decodeIsupBody(*m,msg->body);
    if (code < 300) {
	m->addParam("operation","notify");
	String natAddr;
	MimeSdpBody* sdp = getSdpBody(msg->body);
	if (sdp) {
	    DDebug(this,DebugInfo,"YateSIPConnection got reINVITE SDP [%p]",this);
	    setMedia(plugin.parser().parse(sdp,m_rtpAddr,m_rtpMedia));
	    // guess if the call comes from behind a NAT
	    if (s_auto_nat && isNatBetween(m_rtpAddr,m_host)) {
		Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		    m_rtpAddr.c_str(),m_host.c_str());
		natAddr = m_rtpAddr;
		m_rtpAddr = m_host;
	    }
	    DDebug(this,DebugAll,"RTP addr '%s' [%p]",m_rtpAddr.c_str(),this);
	    if (m_rtpForward) {
		// drop any local RTP we might have before
		m_mediaStatus = m_rtpAddr.null() ? MediaMuted : MediaMissing;
		m_rtpLocalAddr.clear();
		clearEndpoint();
	    }
	}
	if (!addRtpParams(*m,natAddr,sdp))
	    addSdpParams(*m,sdp);
    }
    else {
	m->addParam("operation","reject");
	m->addParam("error",lookup(code,dict_errors,"failure"));
	m->addParam("reason",msg->reason);
    }
    detachTransaction2();
    Engine::enqueue(m);
    return false;
}

void YateSIPConnection::reInvite(SIPTransaction* t)
{
    if (!checkUser(t))
	return;
    DDebug(this,DebugAll,"YateSIPConnection::reInvite(%p) [%p]",t,this);
    Lock mylock(driver());
    int invite = m_reInviting;
    if (m_tr || m_tr2 || (invite == ReinviteRequest) || (invite == ReinviteReceived)) {
	// another request pending - refuse this one
	t->setResponse(491);
	return;
    }
    if (m_hungup) {
	t->setResponse(481);
	return;
    }
    m_reInviting = ReinviteReceived;
    mylock.drop();

    // hack: use a while instead of if so we can return or break out of it
    MimeSdpBody* sdp = getSdpBody(t->initialMessage()->body);
    while (sdp) {
	// for pass-trough RTP we need support from our peer
	if (m_rtpForward) {
	    String addr;
	    String natAddr;
	    ObjList* lst = plugin.parser().parse(sdp,addr);
	    if (!lst)
		break;
	    // guess if the call comes from behind a NAT
	    if (s_auto_nat && isNatBetween(addr,m_host)) {
		Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		    addr.c_str(),m_host.c_str());
		natAddr = addr;
		addr = m_host;
	    }
	    Debug(this,DebugAll,"reINVITE RTP addr '%s'",addr.c_str());

	    Message msg("call.update");
	    complete(msg);
	    msg.addParam("operation","request");
	    copySipHeaders(msg,*t->initialMessage());
	    msg.addParam("rtp_forward","yes");
	    msg.addParam("rtp_addr",addr);
	    if (natAddr)
		msg.addParam("rtp_nat_addr",natAddr);
	    putMedia(msg,lst);
	    addSdpParams(msg,sdp);
	    bool ok = Engine::dispatch(msg);
	    Lock mylock2(driver());
	    // if peer doesn't support updates fail the reINVITE
	    if (!ok) {
		t->setResponse(msg.getIntValue("error",dict_errors,488),msg.getValue("reason"));
		m_reInviting = invite;
	    }
	    else if (m_tr2) {
		// ouch! this shouldn't have happened!
		t->setResponse(491);
		// media is uncertain now so drop the call
		setReason("Internal Server Error",500);
		mylock2.drop();
		hangup();
	    }
	    else {
		// we remember the request and leave it pending
		t->ref();
		t->setUserData(this);
		m_tr2 = t;
	    }
	    return;
	}
	// refuse request if we had no media at all before
	if (m_mediaStatus == MediaMissing)
	    break;
	String addr;
	ObjList* lst = plugin.parser().parse(sdp,addr);
	if (!lst)
	    break;
	// guess if the call comes from behind a NAT
	if (s_auto_nat && isNatBetween(addr,m_host)) {
	    Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		addr.c_str(),m_host.c_str());
	    addr = m_host;
	}

	// TODO: check if we should accept the new media
	// many implementation don't handle well failure so we should drop

	if (m_rtpAddr != addr) {
	    m_rtpAddr = addr;
	    Debug(this,DebugAll,"New RTP addr '%s'",m_rtpAddr.c_str());
	    // clear all data endpoints - createRtpSDP will build new ones
	    clearEndpoint();
	}
	setMedia(lst);

	m_mediaStatus = MediaMissing;
	// let RTP guess again the local interface or use the enforced address
	m_rtpLocalAddr = s_rtpip;

	SIPMessage* m = new SIPMessage(t->initialMessage(), 200);
	MimeSdpBody* sdpNew = createRtpSDP(true);
	m->setBody(sdpNew);
	t->setResponse(m);
	m->deref();
	Message* msg = message("call.update");
	msg->addParam("operation","notify");
	msg->addParam("mandatory","false");
	msg->addParam("mute",String::boolText(MediaStarted != m_mediaStatus));
	putMedia(*msg);
	Engine::enqueue(msg);
	m_reInviting = invite;
	return;
    }
    m_reInviting = invite;
    if (s_refresh_nosdp && !sdp) {
	// be permissive, accept session refresh with no SDP
	SIPMessage* m = new SIPMessage(t->initialMessage(),200);
	// if required provide our own media offer
	if (!m_rtpForward)
	    m->setBody(createSDP());
	t->setResponse(m);
	m->deref();
	return;
    }
    t->setResponse(488);
}

bool YateSIPConnection::checkUser(SIPTransaction* t, bool refuse)
{
    // don't try to authenticate requests from server
    if (m_user.null() || m_line)
	return true;
    int age = t->authUser(m_user);
    if ((age >= 0) && (age <= 10))
	return true;
    DDebug(this,DebugAll,"YateSIPConnection::checkUser(%p) failed, age %d [%p]",t,age,this);
    if (refuse)
	t->requestAuth(s_realm,m_domain,age >= 0);
    return false;
}

void YateSIPConnection::doBye(SIPTransaction* t)
{
    if (m_authBye && !checkUser(t))
	return;
    DDebug(this,DebugAll,"YateSIPConnection::doBye(%p) [%p]",t,this);
    const SIPMessage* msg = t->initialMessage();
    if (msg->body) {
	Message tmp("isup.decode");
	if (decodeIsupBody(tmp,msg->body))
	    parameters().copyParams(tmp);
    }
    copySipHeaders(parameters(),*msg);
    const MimeHeaderLine* hl = msg->getHeader("Reason");
    if (hl) {
	const NamedString* text = hl->getParam("text");
	if (text)
	    m_reason = MimeHeaderLine::unquote(*text);
	// FIXME: add SIP and Q.850 cause codes
    }
    setMedia(0);
    SIPMessage* m = new SIPMessage(t->initialMessage(),200);
    const char* stats = parameters().getValue("rtp_stats");
    if (stats)
	m->addHeader("P-RTP-Stat",stats);
    t->setResponse(m);
    m->deref();
    m_byebye = false;
    hangup();
}

void YateSIPConnection::doCancel(SIPTransaction* t)
{
#ifdef DEBUG
    // CANCEL cannot be challenged but it may (should?) be authenticated with
    //  an old nonce from the transaction that is being cancelled
    if (m_user && (t->authUser(m_user) < 0))
	Debug(&plugin,DebugMild,"User authentication failed for user '%s' but CANCELing anyway [%p]",
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

bool YateSIPConnection::doInfo(SIPTransaction* t)
{
    if (m_authBye && !checkUser(t))
	return true;
    DDebug(this,DebugAll,"YateSIPConnection::doInfo(%p) [%p]",t,this);
    if (m_hungup) {
	t->setResponse(481);
	return true;
    }
    int sig = -1;
    const MimeLinesBody* lb = YOBJECT(MimeLinesBody,getOneBody(t->initialMessage()->body,"application/dtmf-relay"));
    const MimeStringBody* sb = YOBJECT(MimeStringBody,getOneBody(t->initialMessage()->body,"application/dtmf"));
    if (lb) {
	const ObjList* l = lb->lines().skipNull();
	for (; l; l = l->skipNext()) {
	    String tmp = static_cast<String*>(l->get());
	    tmp.toUpper();
	    if (tmp.startSkip("SIGNAL=",false)) {
		sig = tmp.trimBlanks().toInteger(info_signals,-1);
		break;
	    }
	}
    }
    else if (sb) {
	String tmp = sb->text();
	tmp.trimSpaces();
	sig = tmp.toInteger(info_signals,-1);
    }
    else
	return false;
    t->setResponse(200);
    if ((sig >= 0) && (sig <= 16)) {
	char tmp[2];
	tmp[0] = s_dtmfs[sig];
	tmp[1] = '\0';
	Message* msg = message("chan.dtmf");
	copySipHeaders(*msg,*t->initialMessage());
	msg->addParam("text",tmp);
	msg->addParam("detected","sip-info");
	dtmfEnqueue(msg);
    }
    return true;
}

void YateSIPConnection::doRefer(SIPTransaction* t)
{
    if (m_authBye && !checkUser(t))
	return;
    DDebug(this,DebugAll,"doRefer(%p) [%p]",t,this);
    if (m_hungup) {
	t->setResponse(481);
	return;
    }
    if (m_referring) {
	DDebug(this,DebugAll,"doRefer(%p). Already referring [%p]",t,this);
	t->setResponse(491);           // Request Pending
	return;
    }
    m_referring = true;
    const MimeHeaderLine* refHdr = t->initialMessage()->getHeader("Refer-To");
    if (!(refHdr && refHdr->length())) {
	DDebug(this,DebugAll,"doRefer(%p). Empty or missing 'Refer-To' header [%p]",t,this);
	t->setResponse(400);           // Bad request
	m_referring = false;
	return;
    }

    // Get 'Refer-To' URI and its parameters
    URI uri(*refHdr);
    ObjList params;
    // Find the first parameter separator. Ignore everything before it
    int start = findURIParamSep(uri.getExtra(),0);
    if (start >= 0)
	start++;
    else
	start = uri.getExtra().length();
    while (start < (int)uri.getExtra().length()) {
	int end = findURIParamSep(uri.getExtra(),start);
	// Check if this is the last parameter or an empty one
	if (end < 0)
	    end = uri.getExtra().length();
	else if (end == start) {
	    start++;
	    continue;
	}
	String param;
	param = uri.getExtra().substr(start,end - start);
	start = end + 1;
	if (!param)
	    continue;
	param = param.uriUnescape();
	int eq = param.find("=");
	if (eq < 0) {
	    DDebug(this,DebugInfo,"doRefer(%p). Skipping 'Refer-To' URI param '%s' [%p]",
		t,param.c_str(),this);
	    continue;
	}
	String name = param.substr(0,eq).trimBlanks();
	String value = param.substr(eq + 1);
	DDebug(this,DebugAll,"doRefer(%p). Found 'Refer-To' URI param %s=%s [%p]",
	    t,name.c_str(),value.c_str(),this);
	if (name)
	    params.append(new MimeHeaderLine(name,value));
    }
    // Check attended transfer request parameters
    ObjList* repl = params.find("Replaces");
    const MimeHeaderLine* replaces = repl ? static_cast<MimeHeaderLine*>(repl->get()) : 0;
    if (replaces) {
	const String* fromTag = replaces->getParam("from-tag");
	const String* toTag = replaces->getParam("to-tag");
	if (null(replaces) || null(fromTag) || null(toTag)) {
	    DDebug(this,DebugAll,
		"doRefer(%p). Invalid 'Replaces' '%s' from-tag=%s to-tag=%s [%p]",
		t,replaces->safe(),c_safe(fromTag),c_safe(toTag),this);
	    t->setResponse(501);           // Not implemented
	    m_referring = false;
	    return;
	}
	// Avoid replacing the same connection
	if (isDialog(*replaces,*fromTag,*toTag)) {
	    DDebug(this,DebugAll,
		"doRefer(%p). Attended transfer request for the same dialog [%p]",
		t,this);
	    t->setResponse(400,"Can't replace the same dialog");           // Bad request
	    m_referring = false;
	    return;
	}
    }

    Message* msg = 0;
    SIPMessage* sipNotify = 0;
    Channel* ch = YOBJECT(Channel,getPeer());
    if (ch && ch->driver() &&
	initTransfer(msg,sipNotify,t->initialMessage(),refHdr,uri,replaces)) {
	(new YateSIPRefer(id(),ch->id(),ch->driver(),msg,sipNotify,t))->startup();
	return;
    }
    DDebug(this,DebugAll,"doRefer(%p). No peer or peer has no driver [%p]",t,this);
    t->setResponse(503);       // Service Unavailable
    m_referring = false;
}

void YateSIPConnection::complete(Message& msg, bool minimal) const
{
    Channel::complete(msg,minimal);
    if (minimal)
	return;
    Lock mylock(driver());
    if (m_domain)
	msg.setParam("domain",m_domain);
    addCallId(msg,m_dialog,m_dialog.fromTag(isOutgoing()),m_dialog.toTag(isOutgoing()));
}

void YateSIPConnection::disconnected(bool final, const char *reason)
{
    Debug(this,DebugAll,"YateSIPConnection::disconnected() '%s' [%p]",reason,this);
    if (reason) {
	int code = lookup(reason,dict_errors);
	if (code >= 300 && code <= 699)
	    setReason(lookup(code,SIPResponses,reason),code);
	else
	    setReason(reason);
    }
    Channel::disconnected(final,reason);
}

bool YateSIPConnection::msgProgress(Message& msg)
{
    Channel::msgProgress(msg);
    int code = 183;
    const NamedString* reason = msg.getParam("reason");
    if (reason) {
	// handle the special progress types that have provisional codes
	if (*reason == "forwarded")
	    code = 181;
	else if (*reason == "queued")
	    code = 182;
    }
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), code);
	copySipHeaders(*m,msg);
	m->setBody(buildSIPBody(msg,createProvisionalSDP(msg)));
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("progressing");
    return true;
}

bool YateSIPConnection::msgRinging(Message& msg)
{
    Channel::msgRinging(msg);
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 180);
	copySipHeaders(*m,msg);
	m->setBody(buildSIPBody(msg,createProvisionalSDP(msg)));
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("ringing");
    return true;
}

bool YateSIPConnection::msgAnswered(Message& msg)
{
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	updateFormats(msg,true);
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 200);
	copySipHeaders(*m,msg);
	MimeSdpBody* sdp = createPasstroughSDP(msg);
	if (!sdp) {
	    m_rtpForward = false;
	    bool startNow = msg.getBoolValue("rtp_start",s_start_rtp);
	    if (startNow && !m_rtpMedia) {
		// early RTP start but media list yet unknown - build best guess
		String fmts;
		plugin.parser().getAudioFormats(fmts);
		ObjList* lst = new ObjList;
		lst->append(new SDPMedia("audio","RTP/AVP",msg.getValue("formats",fmts)));
		setMedia(lst);
		m_rtpAddr = m_host;
	    }
	    // normally don't start RTP yet, only when we get the ACK
	    sdp = createRtpSDP(startNow);
	}
	m->setBody(buildSIPBody(msg,sdp));

	const MimeHeaderLine* co = m_tr->initialMessage()->getHeader("Contact");
	if (co) {
	    // INVITE had a Contact: header - time to change remote URI
	    m_uri = *co;
	    m_uri.parse();
	}

	// and finally send the answer, transaction will finish soon afterwards
	m_tr->setResponse(m);
	m->deref();
    }
    setReason("",0);
    setStatus("answered",Established);
    return true;
}

bool YateSIPConnection::msgTone(Message& msg, const char* tone)
{
    if (m_hungup)
	return false;
    bool info = m_info;
    bool inband = m_inband;
    const String* method = msg.getParam("method");
    if (method) {
	if ((*method == "info") || (*method == "sip-info")) {
	    info = true;
	    inband = false;
	}
	else if (*method == "rfc2833") {
	    info = false;
	    inband = false;
	}
	else if (*method == "inband") {
	    info = false;
	    inband = true;
	}
    }
    // RFC 2833 and inband require that we have an active local RTP stream
    if (m_rtpMedia && (m_mediaStatus == MediaStarted) && !info) {
	ObjList* l = m_rtpMedia->find("audio");
	const SDPMedia* m = static_cast<const SDPMedia*>(l ? l->get() : 0);
	if (m) {
	    if (!(inband || m->rfc2833().toBoolean(true))) {
		Debug(this,DebugNote,"Forcing DTMF '%s' inband, format '%s' [%p]",
		    tone,m->format().c_str(),this);
		inband = true;
	    }
	    if (inband && dtmfInband(tone))
		return true;
	    msg.setParam("targetid",m->id());
	    return false;
	}
    }
    // either INFO was requested or we have no other choice
    for (; tone && *tone; tone++) {
	char c = *tone;
	for (int i = 0; i <= 16; i++) {
	    if (s_dtmfs[i] == c) {
		SIPMessage* m = createDlgMsg("INFO");
		if (m) {
		    copySipHeaders(*m,msg);
		    String tmp;
		    tmp << "Signal=" << i << "\r\n";
		    m->setBody(new MimeStringBody("application/dtmf-relay",tmp));
		    plugin.ep()->engine()->addMessage(m);
		    m->deref();
		}
		break;
	    }
	}
    }
    return true;
}

bool YateSIPConnection::msgText(Message& msg, const char* text)
{
    if (m_hungup || null(text))
	return false;
    SIPMessage* m = createDlgMsg("MESSAGE");
    if (m) {
	copySipHeaders(*m,msg);
	m->setBody(new MimeStringBody("text/plain",text));
	plugin.ep()->engine()->addMessage(m);
	m->deref();
	return true;
    }
    return false;
}

bool YateSIPConnection::msgDrop(Message& msg, const char* reason)
{
    if (!Channel::msgDrop(msg,reason))
	return false;
    int code = lookup(reason,dict_errors);
    if (code >= 300 && code <= 699) {
	m_reasonCode = code;
	m_reason = lookup(code,SIPResponses,reason);
    }
    return true;
}

bool YateSIPConnection::msgUpdate(Message& msg)
{
    String* oper = msg.getParam("operation");
    if (!oper || oper->null())
	return false;
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (*oper == "request") {
	if (m_tr || m_tr2) {
	    DDebug(this,DebugWarn,"Update request rejected, pending:%s%s [%p]",
		m_tr ? " invite" : "",m_tr2 ? " reinvite" : "",this);
	    msg.setParam("error","pending");
	    msg.setParam("reason","Another INVITE Pending");
	    return false;
	}
	return startClientReInvite(msg);
    }
    if (*oper == "initiate") {
	if (m_reInviting != ReinviteNone) {
	    msg.setParam("error","pending");
	    msg.setParam("reason","Another INVITE Pending");
	    return false;
	}
	m_reInviting = ReinvitePending;
	startPendingUpdate();
	return true;
    }
    if (!m_tr2) {
	if ((m_reInviting == ReinviteRequest) && (*oper == "notify")) {
	    if (startClientReInvite(msg))
		return true;
	    Debug(this,DebugMild,"Failed to start reINVITE, %s: %s [%p]",
		msg.getValue("error","unknown"),
		msg.getValue("reason","No reason"),this);
	    return false;
	}
	msg.setParam("error","nocall");
	return false;
    }
    if (!(m_tr2->isIncoming() && (m_tr2->getState() == SIPTransaction::Process))) {
	msg.setParam("error","failure");
	msg.setParam("reason","Incompatible Transaction State");
	return false;
    }
    if (*oper == "notify") {
	bool rtpSave = m_rtpForward;
	m_rtpForward = msg.getBoolValue("rtp_forward",m_rtpForward);
	MimeSdpBody* sdp = createPasstroughSDP(msg);
	if (!sdp) {
	    m_rtpForward = rtpSave;
	    m_tr2->setResponse(500,"Server failed to build the SDP");
	    detachTransaction2();
	    return false;
	}
	if (m_rtpForward != rtpSave)
	    Debug(this,DebugInfo,"RTP forwarding changed: %s -> %s",
		String::boolText(rtpSave),String::boolText(m_rtpForward));
	SIPMessage* m = new SIPMessage(m_tr2->initialMessage(), 200);
	m->setBody(sdp);
	m_tr2->setResponse(m);
	detachTransaction2();
	m->deref();
	return true;
    }
    else if (*oper == "reject") {
	m_tr2->setResponse(msg.getIntValue("error",dict_errors,488),msg.getValue("reason"));
	detachTransaction2();
	return true;
    }
    return false;
}

void YateSIPConnection::endDisconnect(const Message& msg, bool handled)
{
    const String* reason = msg.getParam("reason");
    if (!TelEngine::null(reason)) {
	int code = reason->toInteger(dict_errors);
	if (code >= 300 && code <= 699)
	    setReason(lookup(code,SIPResponses,*reason),code);
	else
	    setReason(*reason,m_reasonCode);
    }
    const char* prefix = msg.getValue("osip-prefix");
    if (TelEngine::null(prefix))
        return;
    parameters().clearParams();
    parameters().setParam("osip-prefix",prefix);
    parameters().copySubParams(msg,prefix,false);
}

void YateSIPConnection::statusParams(String& str)
{
    Channel::statusParams(str);
    if (m_line)
	str << ",line=" << m_line;
    if (m_user)
	str << ",user=" << m_user;
    if (m_rtpForward)
	str << ",forward=" << (m_sdpForward ? "sdp" : "rtp");
    str << ",inviting=" << (m_tr != 0);
}

bool YateSIPConnection::callRouted(Message& msg)
{
    // try to disable RTP forwarding earliest possible
    if (m_rtpForward && !msg.getBoolValue("rtp_forward"))
	m_rtpForward = false;
    setRfc2833(msg.getParam("rfc2833"));
    Channel::callRouted(msg);
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	String s(msg.retValue());
	if (s.startSkip("sip/",false) && s && msg.getBoolValue("redirect")) {
	    Debug(this,DebugAll,"YateSIPConnection redirecting to '%s' [%p]",s.c_str(),this);
	    String tmp(msg.getValue("calledname"));
	    if (tmp) {
		MimeHeaderLine::addQuotes(tmp);
		tmp += " ";
	    }
	    s = tmp + "<" + s + ">";
	    int code = msg.getIntValue("reason",dict_errors,302);
	    if ((code < 300) || (code > 399))
		code = 302;
	    SIPMessage* m = new SIPMessage(m_tr->initialMessage(),code);
	    m->addHeader("Contact",s);
	    tmp = msg.getValue("diversion");
	    if (tmp.trimBlanks() && tmp.toBoolean(true)) {
		// if diversion is a boolean true use the dialog local URI
		if (tmp.toBoolean(false))
		    tmp = m_dialog.localURI;
		if (!(tmp.startsWith("<") && tmp.endsWith(">")))
		    tmp = "<" + tmp + ">";
		MimeHeaderLine* hl = new MimeHeaderLine("Diversion",tmp);
		tmp = msg.getValue("divert_reason");
		if (tmp) {
		    MimeHeaderLine::addQuotes(tmp);
		    hl->setParam("reason",tmp);
		}
		tmp = msg.getValue("divert_privacy");
		if (tmp) {
		    MimeHeaderLine::addQuotes(tmp);
		    hl->setParam("privacy",tmp);
		}
		tmp = msg.getValue("divert_screen");
		if (tmp) {
		    MimeHeaderLine::addQuotes(tmp);
		    hl->setParam("screen",tmp);
		}
		m->addHeader(hl);
	    }
	    copySipHeaders(*m,msg);
	    m_tr->setResponse(m);
	    m->deref();
	    m_byebye = false;
	    setReason("Redirected",302);
	    setStatus("redirected");
	    return false;
	}

	updateFormats(msg);
	if (msg.getBoolValue("progress",s_progress))
	    m_tr->setResponse(183);
    }
    return true;
}

void YateSIPConnection::callAccept(Message& msg)
{
    m_user = msg.getValue("username");
    if (m_authBye)
	m_authBye = msg.getBoolValue("xsip_auth_bye",true);
    if (m_rtpForward) {
	String tmp(msg.getValue("rtp_forward"));
	if (tmp != "accepted")
	    m_rtpForward = false;
    }
    m_secure = m_secure && msg.getBoolValue("secure",true);
    Channel::callAccept(msg);

    if ((m_reInviting == ReinviteNone) && !m_rtpForward && !isAnswered() && 
	msg.getBoolValue("autoreinvite",false)) {
	// remember we want to switch to RTP forwarding when party answers
	m_reInviting = ReinvitePending;
	startPendingUpdate();
    }
}

void YateSIPConnection::callRejected(const char* error, const char* reason, const Message* msg)
{
    Channel::callRejected(error,reason,msg);
    int code = lookup(error,dict_errors,500);
    if (code < 300 || code > 699)
	code = 500;
    Lock lock(driver());
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	if (code == 401)
	    m_tr->requestAuth(s_realm,m_domain,false);
	else
	    m_tr->setResponse(code,reason);
    }
    setReason(reason,code);
}

// Start a client reINVITE transaction
bool YateSIPConnection::startClientReInvite(Message& msg)
{
    bool hadRtp = !m_rtpForward;
    bool rtpFwd = msg.getBoolValue("rtp_forward",m_rtpForward);
    if (!rtpFwd) {
	msg.setParam("error","failure");
	msg.setParam("reason","RTP forwarding is not enabled");
	return false;
    }
    m_rtpForward = true;
    // this is the point of no return
    if (hadRtp)
	clearEndpoint();
    MimeSdpBody* sdp = createPasstroughSDP(msg,false);
    if (!sdp) {
	msg.setParam("error","failure");
	msg.setParam("reason","Could not build the SDP");
	if (hadRtp) {
	    Debug(this,DebugWarn,"Could not build SDP for reINVITE, hanging up [%p]",this);
	    disconnect("nomedia");
	}
	return false;
    }
    Debug(this,DebugNote,"Initiating reINVITE (%s RTP before) [%p]",
	hadRtp ? "had" : "no",this);
    SIPMessage* m = createDlgMsg("INVITE");
    copySipHeaders(*m,msg);
    if (s_privacy)
	copyPrivacy(*m,msg);
    m->setBody(sdp);
    m_tr2 = plugin.ep()->engine()->addMessage(m);
    if (m_tr2) {
	m_tr2->ref();
	m_tr2->setUserData(this);
    }
    m->deref();
    return true;
}

// Emit pending update if possible, method is called with driver mutex hold
void YateSIPConnection::startPendingUpdate()
{
    Lock mylock(driver());
    if (m_hungup || m_tr || m_tr2 || (m_reInviting != ReinvitePending))
	return;
    if (m_rtpAddr.null()) {
	Debug(this,DebugWarn,"Cannot start update, remote RTP address unknown [%p]",this);
	m_reInviting = ReinviteNone;
	return;
    }
    if (!m_rtpMedia) {
	Debug(this,DebugWarn,"Cannot start update, remote media unknown [%p]",this);
	m_reInviting = ReinviteNone;
	return;
    }
    m_reInviting = ReinviteRequest;
    mylock.drop();

    Message msg("call.update");
    complete(msg);
    msg.addParam("operation","request");
    msg.addParam("rtp_forward","yes");
    msg.addParam("rtp_addr",m_rtpAddr);
    putMedia(msg);
    // if peer doesn't support updates fail the reINVITE
    if (!Engine::dispatch(msg)) {
	Debug(this,DebugWarn,"Cannot start update by '%s', %s: %s [%p]",
	    getPeerId().c_str(),
	    msg.getValue("error","not supported"),
	    msg.getValue("reason","No reason provided"),this);
	m_reInviting = ReinviteNone;
    }
}

// Build the 'call.route' and NOTIFY messages needed by the transfer thread
// msg: 'call.route' message to create & fill
// sipNotify: NOTIFY message to create & fill
// sipRefer: received REFER message, refHdr: 'Refer-To' header
// refHdr: The 'Refer-To' header
// uri: The already parsed 'Refer-To' URI
// replaces: An already checked Replaces parameter from 'Refer-To' or
//  0 for unattended transfer
// If return false, msg and sipNotify are 0
bool YateSIPConnection::initTransfer(Message*& msg, SIPMessage*& sipNotify,
    const SIPMessage* sipRefer, const MimeHeaderLine* refHdr,
    const URI& uri, const MimeHeaderLine* replaces)
{
    // call.route
    msg = new Message("call.route");
    msg->addParam("id",getPeer()->id());
    if (m_billid)
	msg->addParam("billid",m_billid);
    if (m_user)
	msg->addParam("username",m_user);

    const MimeHeaderLine* sh = sipRefer->getHeader("To");                   // caller
    if (sh) {
	URI uriCaller(*sh);
	uriCaller.parse();
	msg->addParam("caller",uriCaller.getUser());
	msg->addParam("callername",uriCaller.getDescription());
    }

    if (replaces) {                                                        // called or replace
	const String* fromTag = replaces->getParam("from-tag");
	const String* toTag = replaces->getParam("to-tag");
	msg->addParam("transfer_callid",*replaces);
	msg->addParam("transfer_fromtag",c_safe(fromTag));
	msg->addParam("transfer_totag",c_safe(toTag));
    }
    else {
	msg->addParam("called",uri.getUser());
	msg->addParam("calledname",uri.getDescription());
    }

    sh = sipRefer->getHeader("Referred-By");                               // diverter
    URI referBy;
    if (sh)
	referBy = *sh;
    else
	referBy = m_dialog.remoteURI;
    msg->addParam("diverter",referBy.getUser());
    msg->addParam("divertername",referBy.getDescription());

    msg->addParam("reason","transfer");                                    // reason
    // NOTIFY
    String tmp;
    const MimeHeaderLine* co = sipRefer->getHeader("Contact");
    if (co) {
	tmp = *co;
	static const Regexp r("^[^<]*<\\([^>]*\\)>.*$");
	if (tmp.matches(r))
	    tmp = tmp.matchString(1);
    }
    sipNotify = createDlgMsg("NOTIFY",tmp);
    plugin.ep()->buildParty(sipNotify);
    if (!sipNotify->getParty()) {
	DDebug(this,DebugAll,"initTransfer. Could not create party to send NOTIFY [%p]",this);
	TelEngine::destruct(sipNotify);
	TelEngine::destruct(msg);
	return false;
    }
    copySipHeaders(*msg,*sipRefer);
    sipNotify->complete(plugin.ep()->engine());
    sipNotify->addHeader("Event","refer");
    sipNotify->addHeader("Subscription-State","terminated;reason=noresource");
    sipNotify->addHeader("Contact",sipRefer->uri);
    return true;
}

// Decode an application/isup body into 'msg' if configured to do so
// The message's name and user data are restored before exiting, regardless the result
// Return true if an ISUP message was succesfully decoded
bool YateSIPConnection::decodeIsupBody(Message& msg, MimeBody* body)
{
    if (!s_sipt_isup)
	return false;
    // Get a valid application/isup body
    MimeBinaryBody* isup = static_cast<MimeBinaryBody*>(getOneBody(body,"application/isup"));
    if (!isup)
	return false;
    // Remember the message's name and user data and fill parameters
    String name = msg;
    RefObject* userdata = msg.userData();
    if (userdata)
	userdata->ref();
    msg = "isup.decode";
    msg.addParam("message-prefix","isup.");
    addBodyParam(msg,"isup.protocol-type",isup,"version");
    addBodyParam(msg,"isup.protocol-basetype",isup,"base");
    msg.addParam(new NamedPointer("rawdata",new DataBlock(isup->body())));
    bool ok = Engine::dispatch(msg);
    // Clear added params and restore message
    if (!ok) {
	Debug(this,DebugMild,"%s failed error='%s' [%p]",
	    msg.c_str(),msg.getValue("error"),this);
	msg.clearParam("error");
    }
    msg.clearParam("rawdata");
    msg = name;
    msg.userData(userdata);
    TelEngine::destruct(userdata);
    return ok;
}

// Build the body of a SIP message from an engine message
// Encode an ISUP message from parameters received in msg if enabled to process them
// Build a multipart/mixed body if more then one body is going to be sent
MimeBody* YateSIPConnection::buildSIPBody(Message& msg, MimeSdpBody* sdp)
{
    MimeBinaryBody* isup = 0;

    // Build isup
    while (s_sipt_isup) {
	String prefix = msg.getValue("message-prefix");
	if (!msg.getParam(prefix + "message-type"))
	    break;

	// Remember the message's name and user data
	String name = msg;
	RefObject* userdata = msg.userData();
	if (userdata)
	    userdata->ref();

	DataBlock* data = 0;
	msg = "isup.encode";
	if (Engine::dispatch(msg)) {
	    NamedString* ns = msg.getParam("rawdata");
	    if (ns) {
		NamedPointer* np = static_cast<NamedPointer*>(ns->getObject("NamedPointer"));
		if (np)
		    data = static_cast<DataBlock*>(np->userObject("DataBlock"));
	    }
	}
	if (data && data->length()) {
	    isup = new MimeBinaryBody("application/isup",(const char*)data->data(),data->length());
	    isup->setParam("version",msg.getValue(prefix + "protocol-type"));
	    const char* s = msg.getValue(prefix + "protocol-basetype");
	    if (s)
		isup->setParam("base",s);
	    MimeHeaderLine* line = new MimeHeaderLine("Content-Disposition","signal");
	    line->setParam("handling","optional");
	    isup->appendHdr(line);
	}
	else {
	    Debug(this,DebugMild,"%s failed error='%s' [%p]",
		msg.c_str(),msg.getValue("error"),this);
	    msg.clearParam("error");
	}

	// Restore message
	msg = name;
	msg.userData(userdata);
	TelEngine::destruct(userdata);
	break;
    }

    if (!isup)
	return sdp;
    if (!sdp)
	return isup;
    // Build multipart
    MimeMultipartBody* body = new MimeMultipartBody;
    body->appendBody(sdp);
    body->appendBody(isup);
    return body;
}


YateSIPLine::YateSIPLine(const String& name)
    : String(name), m_resend(0), m_keepalive(0), m_interval(0), m_alive(0),
      m_flags(-1), m_tr(0), m_marked(false), m_valid(false),
      m_localPort(0), m_proxyPort(0), m_partyPort(0), m_localDetect(false)
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

void YateSIPLine::setupAuth(SIPMessage* msg) const
{
    if (msg)
	msg->setAutoAuth(getAuthName(),m_password);
}

void YateSIPLine::setValid(bool valid, const char* reason)
{
    if ((m_valid == valid) && !reason)
	return;
    m_valid = valid;
    if (m_registrar && m_username) {
	Message* m = new Message("user.notify");
	m->addParam("account",*this);
	m->addParam("protocol","sip");
	m->addParam("username",m_username);
	if (m_domain)
	    m->addParam("domain",m_domain);
	m->addParam("registered",String::boolText(valid));
	if (reason)
	    m->addParam("reason",reason);
	Engine::enqueue(m);
    }
}

SIPMessage* YateSIPLine::buildRegister(int expires) const
{
    String exp(expires);
    String tmp;
    tmp << "sip:" << m_registrar;
    SIPMessage* m = new SIPMessage("REGISTER",tmp);
    plugin.ep()->buildParty(m,0,0,this);
    if (!m->getParty()) {
	Debug(&plugin,DebugWarn,"Could not create party for '%s' [%p]",
	    m_registrar.c_str(),this);
	m->destruct();
	return 0;
    }
    tmp.clear();
    if (m_display)
	tmp = MimeHeaderLine::quote(m_display) + " ";
    tmp << "<sip:";
    tmp << m_username << "@";
    tmp << m->getParty()->getLocalAddr() << ":";
    tmp << m->getParty()->getLocalPort() << ">";
    m->addHeader("Contact",tmp);
    m->addHeader("Expires",exp);
    tmp = "<sip:";
    tmp << m_username << "@" << domain() << ">";
    m->addHeader("To",tmp);
    if (m_callid)
	m->addHeader("Call-ID",m_callid);
    m->complete(plugin.ep()->engine(),m_username,domain(),0,m_flags);
    return m;
}

void YateSIPLine::login()
{
    m_keepalive = 0;
    if (m_registrar.null() || m_username.null()) {
	logout();
	setValid(true);
	return;
    }
    DDebug(&plugin,DebugInfo,"YateSIPLine '%s' logging in [%p]",c_str(),this);
    clearTransaction();
    // prepare a sane resend interval, just in case something goes wrong
    int interval = m_interval / 2;
    if (interval) {
	if (interval < 30)
	    interval = 30;
	else if (interval > 600)
	    interval = 600;
	m_resend = interval*(int64_t)1000000 + Time::now();
    }

    SIPMessage* m = buildRegister(m_interval);
    if (!m) {
	setValid(false);
	return;
    }

    if (m_localDetect) {
	if (m_localAddr.null())
	    m_localAddr = m->getParty()->getLocalAddr();
	if (!m_localPort)
	    m_localPort = m->getParty()->getLocalPort();
    }

    DDebug(&plugin,DebugInfo,"YateSIPLine '%s' emiting %p [%p]",
	c_str(),m,this);
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_tr->setUserData(this);
	if (m_callid.null())
	    m_callid = m_tr->getCallID();
    }
    m->deref();
}

void YateSIPLine::logout()
{
    m_resend = 0;
    m_keepalive = 0;
    bool sendLogout = m_valid && m_registrar && m_username;
    clearTransaction();
    setValid(false);
    if (sendLogout) {
	DDebug(&plugin,DebugInfo,"YateSIPLine '%s' logging out [%p]",c_str(),this);
	SIPMessage* m = buildRegister(0);
	m_partyAddr.clear();
	m_partyPort = 0;
	if (!m)
	    return;
	plugin.ep()->engine()->addMessage(m);
	m->deref();
    }
    m_callid.clear();
}

bool YateSIPLine::process(SIPEvent* ev)
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    if (ev->getTransaction() != m_tr)
	return false;
    if (ev->getState() == SIPTransaction::Cleared) {
	clearTransaction();
	setValid(false,"timeout");
	m_keepalive = 0;
	Debug(&plugin,DebugWarn,"SIP line '%s' logon timeout",c_str());
	return false;
    }
    const SIPMessage* msg = ev->getMessage();
    if (!(msg && msg->isAnswer()))
	return false;
    if (ev->getState() != SIPTransaction::Process)
	return false;
    clearTransaction();
    DDebug(&plugin,DebugAll,"YateSIPLine '%s' got answer %d [%p]",
	c_str(),msg->code,this);
    switch (msg->code) {
	case 200:
	    {
		int exp = m_interval;
		const MimeHeaderLine* hl = msg->getHeader("Contact");
		if (hl) {
		    const NamedString* e = hl->getParam("expires");
		    if (e)
			exp = e->toInteger(exp);
		    else
			hl = 0;
		}
		if (!hl) {
		    hl = msg->getHeader("Expires");
		    if (hl)
			exp = hl->toInteger(exp);
		}
		if ((exp != m_interval) && (exp >= 60)) {
		    Debug(&plugin,DebugNote,"SIP line '%s' changed expire interval from %d to %d",
			c_str(),m_interval,exp);
		    m_interval = exp;
		}
	    }
	    // re-register at 3/4 of the expire interval
	    m_resend = m_interval*(int64_t)750000 + Time::now();
	    m_keepalive = m_alive ? m_alive*(int64_t)1000000 + Time::now() : 0;
	    detectLocal(msg);
	    if (msg->getParty()) {
		m_partyAddr = msg->getParty()->getPartyAddr();
		m_partyPort = msg->getParty()->getPartyPort();
	    }
	    setValid(true);
	    Debug(&plugin,DebugCall,"SIP line '%s' logon success to %s:%d",
		c_str(),m_partyAddr.c_str(),m_partyPort);
	    break;
	default:
	    // detect local address even from failed attempts - helps next time
	    detectLocal(msg);
	    setValid(false,msg->reason);
	    Debug(&plugin,DebugWarn,"SIP line '%s' logon failure %d: %s",
		c_str(),msg->code,msg->reason.safe());
    }
    return false;
}

void YateSIPLine::detectLocal(const SIPMessage* msg)
{
    if (!(m_localDetect && msg->getParty()))
	return;
    String laddr = m_localAddr;
    int lport = m_localPort;
    MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(msg->getHeader("Via"));
    if (hl) {
	const NamedString* par = hl->getParam("received");
	if (par && *par)
	    laddr = *par;
	par = hl->getParam("rport");
	if (par) {
	    int port = par->toInteger(0,10);
	    if (port > 0)
		lport = port;
	}
    }
    if (laddr.null())
	laddr = msg->getParty()->getLocalAddr();
    if (!lport)
	lport = msg->getParty()->getLocalPort();
    if ((laddr != m_localAddr) || (lport != m_localPort)) {
	Debug(&plugin,DebugInfo,"Detected local address %s:%d for SIP line '%s'",
	    laddr.c_str(),lport,c_str());
	m_localAddr = laddr;
	m_localPort = lport;
	// since local address changed register again in 2 seconds
	m_resend = 2000000 + Time::now();
    }
}

void YateSIPLine::keepalive()
{
    Socket* sock = plugin.ep() ? plugin.ep()->socket() : 0;
    if (sock && m_partyPort && m_partyAddr) {
	SocketAddr addr(PF_INET);
	if (addr.host(m_partyAddr) && addr.port(m_partyPort) && addr.valid()) {
	    Debug(&plugin,DebugAll,"Sending UDP keepalive to %s:%d for '%s'",
		m_partyAddr.c_str(),m_partyPort,c_str());
	    sock->sendTo("\r\n",2,addr);
	}
    }
    m_keepalive = m_alive ? m_alive*(int64_t)1000000 + Time::now() : 0;
}

void YateSIPLine::timer(const Time& when)
{
    if (!m_resend || (m_resend > when)) {
	if (m_keepalive && (m_keepalive <= when))
	    keepalive();
	return;
    }
    m_resend = 0;
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

bool YateSIPLine::change(int& dest, int src)
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
    String oper(msg.getValue("operation"));
    if (oper == "logout") {
	logout();
	return true;
    }
    bool chg = false;
    chg = change(m_registrar,msg.getValue("registrar",msg.getValue("server"))) || chg;
    chg = change(m_username,msg.getValue("username")) || chg;
    chg = change(m_authname,msg.getValue("authname")) || chg;
    chg = change(m_password,msg.getValue("password")) || chg;
    chg = change(m_domain,msg.getValue("domain")) || chg;
    chg = change(m_flags,msg.getIntValue("xsip_flags",-1)) || chg;
    m_display = msg.getValue("description");
    m_interval = msg.getIntValue("interval",600);
    String tmp(msg.getValue("localaddress",s_auto_nat ? "auto" : ""));
    // "auto", "yes", "enable" or "true" to autodetect local address
    m_localDetect = (tmp == "auto") || tmp.toBoolean(false);
    if (!m_localDetect) {
	// "no", "disable" or "false" to just disable detection
	if (!tmp.toBoolean(true))
	    tmp.clear();
	int port = 0;
	if (tmp) {
	    int sep = tmp.find(':');
	    if (sep > 0) {
		port = tmp.substr(sep+1).toInteger(5060);
		tmp = tmp.substr(0,sep);
	    }
	    else if (sep < 0)
		port = 5060;
	}
	chg = change(m_localAddr,tmp) || chg;
	chg = change(m_localPort,port) || chg;
    }
    tmp = msg.getValue("outbound");
    int port = 0;
    if (tmp) {
	int sep = tmp.find(':');
	if (sep > 0) {
	    port = tmp.substr(sep+1).toInteger(0);
	    tmp = tmp.substr(0,sep);
	}
    }
    chg = change(m_proxyAddr,tmp) || chg;
    chg = change(m_proxyPort,port) || chg;
    m_alive = msg.getIntValue("keepalive",(m_localDetect ? 25 : 0));
    tmp = msg.getValue("operation");
    // if something changed we logged out so try to climb back
    if (chg || (oper == "login"))
	login();
    return chg;
}


YateSIPGenerate::YateSIPGenerate(SIPMessage* m)
    : m_tr(0), m_code(0)
{
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_tr->setUserData(this);
    }
    m->deref();
}

YateSIPGenerate::~YateSIPGenerate()
{
    clearTransaction();
}

bool YateSIPGenerate::process(SIPEvent* ev)
{
    DDebug(&plugin,DebugInfo,"YateSIPGenerate::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    if (ev->getTransaction() != m_tr)
	return false;
    if (ev->getState() == SIPTransaction::Cleared) {
	clearTransaction();
	return false;
    }
    const SIPMessage* msg = ev->getMessage();
    if (!(msg && msg->isAnswer()))
	return false;
    if (ev->getState() != SIPTransaction::Process)
	return false;
    clearTransaction();
    Debug(&plugin,DebugAll,"YateSIPGenerate got answer %d [%p]",
	m_code,this);
    return false;
}

void YateSIPGenerate::clearTransaction()
{
    if (m_tr) {
	DDebug(&plugin,DebugInfo,"YateSIPGenerate clearing transaction %p [%p]",
	    m_tr,this);
	m_code = m_tr->getResponseCode();
	m_tr->setUserData(0);
	m_tr->deref();
	m_tr = 0;
    }
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


bool SipHandler::received(Message &msg)
{
    Debug(&plugin,DebugInfo,"SipHandler::received() [%p]",this);
    RefPointer<YateSIPConnection> conn;
    String uri;
    const char* id = msg.getValue("id");
    if (id) {
	plugin.lock();
	conn = static_cast<YateSIPConnection*>(plugin.find(id));
	plugin.unlock();
	if (!conn) {
	    msg.setParam("error","noconn");
	    return false;
	}
	uri = conn->m_uri;
    }
    const char* method = msg.getValue("method");
    uri = msg.getValue("uri",uri);
    static const Regexp r("<\\([^>]\\+\\)>");
    if (uri.matches(r))
	uri = uri.matchString(1);
    if (!(method && uri))
	return false;

    int maxf = msg.getIntValue("antiloop",s_maxForwards);
    if (maxf <= 0) {
	Debug(&plugin,DebugMild,"Blocking looping request '%s %s' [%p]",
	    method,uri.c_str(),this);
	msg.setParam("error","looping");
	return false;
    }

    SIPMessage* sip = 0;
    YateSIPLine* line = 0;
    const char* domain = msg.getValue("domain");
    if (conn) {
	line = plugin.findLine(conn->getLine());
	sip = conn->createDlgMsg(method,uri);
	conn = 0;
    }
    else {
	line = plugin.findLine(msg.getValue("line"));
	if (line && !line->valid()) {
	    msg.setParam("error","offline");
	    return false;
	}
	sip = new SIPMessage(method,uri);
	plugin.ep()->buildParty(sip,msg.getValue("host"),msg.getIntValue("port"),line);
	if (line)
	    domain = line->domain(domain);
    }
    sip->addHeader("Max-Forwards",String(maxf));
    copySipHeaders(*sip,msg,"sip_");
    const char* type = msg.getValue("xsip_type");
    const char* body = msg.getValue("xsip_body");
    if (type && body)
	sip->setBody(new MimeStringBody(type,body,-1));
    sip->complete(plugin.ep()->engine(),msg.getValue("user"),domain,0,
	msg.getIntValue("xsip_flags",-1));
    if (!msg.getBoolValue("wait")) {
	// no answer requested - start transaction and forget
	plugin.ep()->engine()->addMessage(sip);
	sip->deref();
	return true;
    }
    YateSIPGenerate gen(sip);
    while (gen.busy())
	Thread::idle();
    if (gen.code())
	msg.setParam("code",String(gen.code()));
    else
	msg.clearParam("code");
    return true;
}


YateSIPConnection* SIPDriver::findCall(const String& callid, bool incRef)
{
    XDebug(this,DebugAll,"SIPDriver finding call '%s'",callid.c_str());
    Lock mylock(this);
    ObjList* l = channels().skipNull();
    for (; l; l = l->skipNext()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c->callid() == callid)
	    return (incRef ? c->ref() : c->alive()) ? c : 0;
    }
    return 0;
}

YateSIPConnection* SIPDriver::findDialog(const SIPDialog& dialog, bool incRef)
{
    XDebug(this,DebugAll,"SIPDriver finding dialog '%s'",dialog.c_str());
    Lock mylock(this);
    ObjList* l = channels().skipNull();
    for (; l; l = l->skipNext()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c->dialog() &= dialog)
	    return (incRef ? c->ref() : c->alive()) ? c : 0;
    }
    return 0;
}

YateSIPConnection* SIPDriver::findDialog(const String& dialog, const String& fromTag,
    const String& toTag, bool incRef)
{
    XDebug(this,DebugAll,"SIPDriver finding dialog '%s' fromTag='%s' toTag='%s'",
	dialog.c_str(),fromTag.c_str(),toTag.c_str());
    Lock mylock(this);
    for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(o->get());
	if (c->isDialog(dialog,fromTag,toTag))
	    return (incRef ? c->ref() : c->alive()) ? c : 0;
    }
    return 0;
}

// find line by name
YateSIPLine* SIPDriver::findLine(const String& line) const
{
    if (line.null())
	return 0;
    ObjList* l = s_lines.find(line);
    return l ? static_cast<YateSIPLine*>(l->get()) : 0;
}

// find line by party address and port
YateSIPLine* SIPDriver::findLine(const String& addr, int port, const String& user)
{
    if (!(port && addr))
	return 0;
    Lock mylock(this);
    ObjList* l = s_lines.skipNull();
    for (; l; l = l->skipNext()) {
	YateSIPLine* sl = static_cast<YateSIPLine*>(l->get());
	if (sl->getPartyPort() && (sl->getPartyPort() == port) && (sl->getPartyAddr() == addr)) {
	    if (user && (sl->getUserName() != user))
		continue;
	    return sl;
	}
    }
    return 0;
}

// check if a line is either empty or valid (logged in or no registrar)
bool SIPDriver::validLine(const String& line)
{
    if (line.null())
	return true;
    YateSIPLine* l = findLine(line);
    return l && l->valid();
}

bool SIPDriver::received(Message& msg, int id)
{
    if (id == Timer) {
	ObjList* l = s_lines.skipNull();
	for (; l; l = l->skipNext())
	    static_cast<YateSIPLine*>(l->get())->timer(msg.msgTime());
    }
    else if (id == Halt) {
	dropAll(msg);
	channels().clear();
	s_lines.clear();
    }
    else if (id == Status) {
	String target = msg.getValue("module");
	if (target && target.startsWith(name(),true) && !target.startsWith(prefix())) {
	    msgStatus(msg);
	    return false;
	}
    }
    return Driver::received(msg,id);
}

bool SIPDriver::hasLine(const String& line) const
{
    return line && findLine(line);
}

bool SIPDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugWarn,"SIP call found but no data channel!");
	return false;
    }
    if (!validLine(msg.getValue("line"))) {
	// asked to use a line but it's not registered
	msg.setParam("error","offline");
	return false;
    }
    YateSIPConnection* conn = new YateSIPConnection(msg,dest,msg.getValue("id"));
    conn->initChan();
    if (conn->getTransaction()) {
	CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
	if (ch && conn->connect(ch,msg.getValue("reason"))) {
	    conn->callConnect(msg);
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
    : Driver("sip","varchans"), 
      m_parser("sip","SIP Call"),
      m_endpoint(0)
{
    Output("Loaded module SIP Channel");
    m_parser.debugChain(this);
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
    s_realm = s_cfg.getValue("general","realm","Yate");
    s_maxForwards = s_cfg.getIntValue("general","maxforwards",20);
    s_floodEvents = s_cfg.getIntValue("general","floodevents",20);
    s_privacy = s_cfg.getBoolValue("general","privacy");
    s_auto_nat = s_cfg.getBoolValue("general","nat",true);
    s_progress = s_cfg.getBoolValue("general","progress",false);
    s_inband = s_cfg.getBoolValue("general","dtmfinband",false);
    s_info = s_cfg.getBoolValue("general","dtmfinfo",false);
    s_rtpip = s_cfg.getValue("general","rtp_localip");
    s_start_rtp = s_cfg.getBoolValue("general","rtp_start",false);
    s_multi_ringing = s_cfg.getBoolValue("general","multi_ringing",false);
    s_refresh_nosdp = s_cfg.getBoolValue("general","refresh_nosdp",true);
    s_sipt_isup = s_cfg.getBoolValue("sip-t","isup",false);
    s_expires_min = s_cfg.getIntValue("registrar","expires_min",EXPIRES_MIN);
    s_expires_def = s_cfg.getIntValue("registrar","expires_def",EXPIRES_DEF);
    s_expires_max = s_cfg.getIntValue("registrar","expires_max",EXPIRES_MAX);
    s_auth_register = s_cfg.getBoolValue("registrar","auth_required",true);
    s_nat_refresh = s_cfg.getIntValue("registrar","nat_refresh",25);
    s_reg_async = s_cfg.getBoolValue("registrar","async_process",true);
    s_ack_required = !s_cfg.getBoolValue("hacks","ignore_missing_ack",false);
    s_1xx_formats = s_cfg.getBoolValue("hacks","1xx_change_formats",true);
    m_parser.initialize(s_cfg.getSection("codecs"),s_cfg.getSection("hacks"),s_cfg.getSection("general"));
    if (!m_endpoint) {
	m_endpoint = new YateSIPEndPoint(Thread::priority(s_cfg.getValue("general","thread")));
	if (!(m_endpoint->Init())) {
	    delete m_endpoint;
	    m_endpoint = 0;
	    return;
	}
	m_endpoint->startup();
	setup();
	installRelay(Halt);
	installRelay(Progress);
	installRelay(Update);
	installRelay(Route);
	installRelay(Status);
	Engine::install(new UserHandler);
	if (s_cfg.getBoolValue("general","generate"))
	    Engine::install(new SipHandler);
    }
}

void SIPDriver::genUpdate(Message& msg)
{
    DDebug(this,DebugInfo,"fill module.update message");
    Lock l(this);
    if (m_endpoint) {
	msg.setParam("failed_auths",String(m_endpoint->failedAuths()));
	msg.setParam("transaction_timeouts",String(m_endpoint->timedOutTrs()));
	msg.setParam("bye_timeouts",String(m_endpoint->timedOutByes()));
    }
}

bool SIPDriver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    String cmd = s_statusCmd + " " + name();
    String overviewCmd = s_statusCmd + " overview " + name();
    if (partLine == cmd || partLine == overviewCmd)
	itemComplete(msg.retValue(),"accounts",partWord);
    else 
    	return Driver::commandComplete(msg,partLine,partWord);
    return false;
}

void SIPDriver::msgStatus(Message& msg)
{
    String str = msg.getValue("module");
    while (str.startSkip(name())) {
	str.trimBlanks();
	if (str.null())
	    Module::msgStatus(msg);
	else if (str.startSkip("accounts")) {
	    msg.retValue().clear();
	    msg.retValue() << "module=" << name();
	    msg.retValue() << ",protocol=SIP";
	    msg.retValue() << ",format=Username|Status;";
	    msg.retValue() << "accounts=" << s_lines.count();
	    if (!msg.getBoolValue("details",true)) {
		msg.retValue() << "\r\n";
		return;
	    }
	    String accounts = "";
	    for (ObjList* o = s_lines.skipNull(); o; o = o->skipNext()) {
		YateSIPLine* line = static_cast<YateSIPLine*>(o->get());
		accounts.append(line->c_str(),",") << "=";
		accounts.append(line->getUserName()) << "|";
		accounts << (line->valid() ? "online" : "offline");
	    }
	    msg.retValue().append(accounts,";"); 
	    msg.retValue() << "\r\n";
	    return;
	}
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
