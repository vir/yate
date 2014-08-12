/**
 * jbengine.cpp
 * Yet Another Jabber Component Protocol Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <yatejabber.h>
#include <stdio.h>
#include <stdlib.h>

using namespace TelEngine;


static unsigned int fixValue(const NamedList& p, const char* param,
    unsigned int defVal, unsigned int min, unsigned int max, bool zero = false)
{
    unsigned int val = p.getIntValue(param,defVal);
    if (!val) {
	if (!zero)
	    val = defVal;
    }
    else if (val < min)
	val = min;
    else if (val > max)
	val = max;
    return val;
}

const TokenDict JBEvent::s_type[] = {
    {"Message",         Message},
    {"Presence",        Presence},
    {"Iq",              Iq},
    {"Terminated",      Terminated},
    {"Destroy",         Destroy},
    {"Start",           Start},
    {"Auth",            Auth},
    {"Bind",            Bind},
    {"Running",         Running},
    {"DbResult",        DbResult},
    {"DbVerify",        DbVerify},
    {"RegisterOk",      RegisterOk},
    {"RegisterFailed",  RegisterFailed},
    {"Unknown",         Unknown},
    {0,0}
};

const TokenDict JBConnect::s_statusName[] =  {
    {"Start",     Start},
    {"Address",   Address},
    {"Srv",       Srv},
    {"Domain",    Domain},
    {0,0}
};

// Entity caps item tag in document
static const String s_entityCapsItem = "item";
// Node values used by entity caps
static const String s_googleTalkNode = "http://www.google.com/xmpp/client/caps";
static const String s_googleMailNode = "http://mail.google.com/xmpp/client/caps";
static const String s_googleAndroidNode = "http://www.android.com/gtalk/client/caps";
static const String s_googleAndroidNode2 = "http://www.android.com/gtalk/client/caps2";

// Stream read buffer
#define JB_STREAMBUF                8192
#define JB_STREAMBUF_MIN            1024
// Stream restart counter
#define JB_RESTART_COUNT               2
#define JB_RESTART_COUNT_MIN           1
#define JB_RESTART_COUNT_MAX          10
#define JB_RESTART_UPDATE          15000
#define JB_RESTART_UPDATE_MIN       5000
#define JB_RESTART_UPDATE_MAX     300000
// Stream setup timer
#define JB_SETUP_INTERVAL         180000
#define JB_SETUP_INTERVAL_MIN      60000
#define JB_SETUP_INTERVAL_MAX     600000
// Wait stream start timer
#define JB_START_INTERVAL          20000
#define JB_START_INTERVAL_MIN      10000
#define JB_START_INTERVAL_MAX      60000
// Stream connect timer
#define JB_CONNECT_INTERVAL        60000
#define JB_CONNECT_INTERVAL_MIN     1000
#define JB_CONNECT_INTERVAL_MAX   120000
// Stream SRV query timer
#define JB_SRV_INTERVAL            30000
#define JB_SRV_INTERVAL_MIN        10000
#define JB_SRV_INTERVAL_MAX       120000
// Ping
#define JB_PING_INTERVAL          600000
#define JB_PING_INTERVAL_MIN       60000
#define JB_PING_INTERVAL_MAX     3600000
#define JB_PING_TIMEOUT            30000
#define JB_PING_TIMEOUT_MIN        10000
#define JB_PING_TIMEOUT_MAX        JB_PING_INTERVAL_MIN
// Idle
#define JB_IDLE_INTERVAL         3600000 // 1h
#define JB_IDLE_INTERVAL_MIN      600000 // 10min
#define JB_IDLE_INTERVAL_MAX    21600000 // 6h
// Redirect
#define JB_REDIRECT_COUNT              0
#define JB_REDIRECT_COUNT_CLIENT       2
#define JB_REDIRECT_MIN	               0
#define JB_REDIRECT_MAX	              10


/*
 * SASL
 */
static inline unsigned int findZero(const char* buf, unsigned int max)
{
    if (!buf)
	return max + 1;
    unsigned int pos = 0;
    while (pos < max && buf[pos])
	pos++;
    return pos;
}

// Parse and decode a buffer containing SASL plain authentication data
// See RFC 4616 Section 2
// Format: [authzid] UTF8NUL username UTF8NUL passwd
// Each token must be up to 255 bytes length
static NamedList* splitPlainSasl(const DataBlock& buf)
{
    const char* d = (const char*)buf.data();
    unsigned int len = buf.length();
    if (!len)
	return 0;
    String user, pwd, authzid;
    // Use a while to break to the end
    bool ok = false;
    while (true) {
	// authzid
	unsigned int ll = findZero(d,len);
	if (ll && (ll > 255 || ll > len))
	    break;
	authzid.assign(d,ll);
	if (-1 == authzid.lenUtf8())
	    break;
	d += ll;
	len -= ll;
	// Username
	if (d[0] || len < 2)
	    break;
	ll = findZero(++d,--len);
	if (!(ll && ll < len && ll < 256))
	    break;
	user.assign(d,ll);
	if (-1 == user.lenUtf8())
	    break;
	d += ll;
	len -= ll;
	// Password
	if (d[0] || len < 2)
	    break;
	ll = findZero(++d,--len);
	if (ll != len || ll > 255)
	    break;
	pwd.assign(d,ll);
	ok = (-1 != pwd.lenUtf8());
	break;
    }
    if (!ok)
	return 0;
    NamedList* result = new NamedList("");
    result->addParam("username",user);
    result->addParam("response",pwd);
    if (authzid)
	result->addParam("authzid",authzid);
    return result;
}

static NamedList* splitDigestSasl(const String& buf)
{
    const char* d = buf.c_str();
    unsigned int len = buf.length();
    NamedList* result = 0;
    while (len) {
	// Find '='
	unsigned int i = 0;
	while (i < len && d[i] != '=')
	    i++;
	if (!i || i >= len) {
	    Debug(DebugNote,"splitDigestSasl() unexpected end of buffer '%s'",d);
	    break;
	}
	// Get param name and skip over '='
	String name(d,i);
	i++;
	d += i;
	len -= i;
	XDebug(DebugAll,"splitDigestSasl() found directive='%s' rest='%s' len=%u",
	    name.c_str(),d,len);
	String value;
	if (len) {
	    // Find ',', handle quoted parameters
	    if (*d == '\"') {
		if (len < 2) {
		    Debug(DebugNote,
			"splitDigestSasl() unexpected end of buffer '%s'",d);
		    break;
		}
		// Find an unescaped "
		for (i = 1; i < len; i++) {
		    if (d[i] == '"' && d[i-1] != '\\')
			break;
		}
		if (i == len) {
		    Debug(DebugNote,"splitDigestSasl() unclosed '\"' found at %u",
			buf.length() - len);
		    break;
		}
		// Unescape the content
		value.assign(d + 1,i - 1);
		int pos = -1;
		unsigned int start = 0;
		bool ok = true;
		while (-1 != (value.find('\\',start))) {
		    if (pos == 0) {
			// No character to escape: error
			if (value.length() == 1) {
			    Debug(DebugNote,"splitDigestSasl() 2");
			    ok = false;
			    break;
			}
			value = value.substr(1);
		    }
		    else if ((unsigned int)pos < value.length() - 1) {
			if (value[pos - 1] != '"') {
			    // Escaped char
			    value = value.substr(0,pos) + value.substr(0,pos + 1);
			    start = pos + 1;
			}
			else if (value[pos + 1] == '"') {
			    // Escaped backslash
			    value = value.substr(0,pos - 1) + "\\" + value.substr(0,pos + 2);
			    start = pos + 1;
			}
			else {
			    // Error
			    Debug(DebugNote,"splitDigestSasl() 3");
			    ok = false;
			    break;
			}
		    }
		    else {
			// No character to escape: error
			Debug(DebugNote,"splitDigestSasl() 4");
			ok = false;
			break;
		    }
		}
		if (!ok)
		    break;
		// Adjust buffer and length
		if (i < len) {
		    if (i == len - 1)
			i++;
		    else if (d[i + 1] == ',')
			i += 2;
		    else {
			Debug(DebugNote,"splitDigestSasl() ',' not found at %u rest=%s",
			    buf.length() - len + i + 1,d);
			break;
		    }
		}
	    }
	    else {
		// Skip until ,
		for (i = 0; i < len && d[i] != ','; i++)
		    ;
		if (i)
		    value.assign(d,i);
		if (i < len)
		    i++;
	    }
	    d += i;
	    len -= i;
	}
	if (!result)
	    result = new NamedList("");
	XDebug(DebugAll,"splitDigestSasl() found '%s'='%s' rest='%s' len=%u",
	    name.c_str(),value.c_str(),d,len);
	result->addParam(name,value);
    }
    if (len)
	TelEngine::destruct(result);
    return result;
}

// Apend a quoted directive to a string
// Escape the value
static inline void appendQDirective(String& buf, const String& name,
    const String& value)
{
    if (-1 == value.find('\"') && -1 == value.find('\\')) {
	buf.append(name + "=\"" + value + "\"",",");
	return;
    }
    // Replace \ with "\" and " with \"
    // See RFC2831 7.2
    String tmp;
    char c = 0;
    char* s = (char*)value.c_str();
    while ((c = *s++)) {
	if (c == '\"')
	    tmp << '\\' << c;
	else if (c == '\\')
	    tmp << "\"\\\"";
	else
	    tmp += c;
    }
    buf.append(name + "=\"" + tmp + "\"",",");
}

// Constructor
SASL::SASL(bool plain, const char* realm)
    : m_plain(plain), m_params(0), m_realm(realm), m_nonceCount(0)
{
}

// Set auth params
void SASL::setAuthParams(const char* user, const char* pwd)
{
    if (TelEngine::null(user) &&  TelEngine::null(pwd))
	return;
    if (!m_params)
	m_params = new NamedList("");
    if (!TelEngine::null(user))
	m_params->setParam("username",user);
    if (!TelEngine::null(pwd))
	m_params->setParam("password",pwd);
}

// Build an auth response
bool SASL::buildAuthRsp(String& buf, const char* digestUri)
{
    if (!m_params)
	return false;

    // Plain. See RFC 4616 Section 2
    // Format: [authzid] UTF8NUL username UTF8NUL passwd
    // Each token must be up to 255 bytes length
    if (m_plain) {
	if (!m_params)
	    return false;
	String* user = m_params->getParam("username");
	String* pwd = m_params->getParam("password");
	if (!user || user->length() > 255 || !pwd || pwd->length() > 255)
	    return false;
	DataBlock data;
	unsigned char nul = 0;
	data.append(&nul,1);
	data += *user;
	data.append(&nul,1);
	data += *pwd;
	Base64 base64((void*)data.data(),data.length());
	base64.encode(buf);
	return true;
    }

    // Digest MD5. See RFC 2831 2.1.2.1
    String* pwd = m_params->getParam("password");
    if (!pwd)
	return false;

#define SASL_ADD_QDIR(n) { \
    NamedString* tmp = m_params->getParam(n); \
    if (tmp) \
	appendQDirective(buf,tmp->name(),*tmp); \
}
    SASL_ADD_QDIR("username")
    SASL_ADD_QDIR("realm")
    SASL_ADD_QDIR("nonce")
    MD5 md5(String((unsigned int)Random::random()));
    m_cnonce = md5.hexDigest();
    m_params->setParam("cnonce",m_cnonce);
    SASL_ADD_QDIR("cnonce")
    m_nonceCount++;
    char tmp[9];
    ::sprintf(tmp,"%08x",m_nonceCount);
    m_params->setParam("nc",tmp);
    SASL_ADD_QDIR("nc")
    m_params->setParam("qop","auth");
    SASL_ADD_QDIR("qop")
    m_params->setParam("digest-uri",digestUri);
    SASL_ADD_QDIR("digest-uri")
    String rsp;
    buildMD5Digest(rsp,*pwd);
    buf << ",response=" << rsp;
    SASL_ADD_QDIR("charset")
    SASL_ADD_QDIR("md5-sess")
    XDebug(DebugAll,"SASL built MD5 response %s [%p]",buf.c_str(),this);
#undef SASL_ADD_QDIR
    Base64 base64((void*)buf.c_str(),buf.length());
    buf.clear();
    base64.encode(buf);
    return true;
}

// Build an MD5 challenge from this object
// See RFC 2831 Section 2.1.1
bool SASL::buildMD5Challenge(String& buf)
{
    String tmp;
    if (m_realm) {
	if (-1 == m_realm.lenUtf8())
	    return false;
	appendQDirective(tmp,"realm",m_realm);
    }
    // Re-build nonce. Increase nonce count
    m_nonce.clear();
    m_nonce << (int)Time::msecNow() << (int)Random::random();
    MD5 md5(m_nonce);
    m_nonce = md5.hexDigest();
    m_nonceCount++;
    tmp.append("nonce=\"" + m_nonce + "\"",",");
    tmp << ",qop=\"auth\"";
    tmp << ",charset=\"utf-8\"";
    tmp << ",algorithm=\"md5-sess\"";
    // RFC 2831 2.1.1: The size of a digest-challenge MUST be less than 2048 bytes
    if (tmp.length() < 2048) {
	buf = tmp;
	return true;
    }
    m_nonceCount--;
    return false;
}

bool SASL::parsePlain(const DataBlock& buf)
{
#ifdef XDEBUG
    String tmp;
    tmp.hexify((void*)buf.data(),buf.length(),' ');
    Debug(DebugAll,"SASL::parsePlain() %s [%p]",tmp.c_str(),this);
#endif
    TelEngine::destruct(m_params);
    m_params = splitPlainSasl(buf);
    return m_params != 0;
}

// Parse and decode a buffer containing a SASL Digest MD5 challenge
bool SASL::parseMD5Challenge(const String& buf)
{
    XDebug(DebugAll,"SASL::parseMD5Challenge() %s [%p]",buf.c_str(),this);
    TelEngine::destruct(m_params);
    // RFC 2831 2.1.1: The size of a digest-response MUST be less than 2048 bytes
    if (buf.length() >= 2048) {
	Debug(DebugNote,"SASL::parseMD5Challenge() invalid length=%u (max=2048) [%p]",
	    buf.length(),this);
	return false;
    }
    m_params = splitDigestSasl(buf);
    if (!m_params) {
	Debug(DebugNote,"SASL::parseMD5Challenge() failed to split params [%p]",
	    this);
	return false;
    }
    return true;
}

// Parse and decode a buffer containing a SASL Digest MD5 response
// See RFC 2831
bool SASL::parseMD5ChallengeRsp(const String& buf)
{
    XDebug(DebugAll,"SASL::parseMD5ChallengeRsp() %s [%p]",buf.c_str(),this);
    TelEngine::destruct(m_params);
    // RFC 2831 2.1.2: The size of a digest-response MUST be less than 4096 bytes
    if (buf.length() >= 4096) {
	Debug(DebugNote,"SASL::parseMD5ChallengeRsp() invalid length=%u (max=4096) [%p]",
	    buf.length(),this);
	return false;
    }
    m_params = splitDigestSasl(buf);
    if (!m_params) {
	Debug(DebugNote,"SASL::parseMD5ChallengeRsp() failed to split params [%p]",
	    this);
	return false;
    }
    bool ok = false;
    // Check realm, nonce, nonce count
    // Use a while to break to the end
    while (true) {
	String* tmp = m_params->getParam("realm");
	if (!tmp || *tmp != m_realm) {
	    Debug(DebugNote,"SASL::parseMD5ChallengeRsp() invalid realm='%s' [%p]",
		TelEngine::c_safe(tmp),this);
	    break;
	}
	tmp = m_params->getParam("nonce");
	if (!tmp || *tmp != m_nonce) {
	    Debug(DebugNote,"SASL::parseMD5ChallengeRsp() invalid nonce='%s' [%p]",
		TelEngine::c_safe(tmp),this);
	    break;
	}
	tmp = m_params->getParam("nc");
	if (!tmp || (unsigned int)tmp->toInteger(0,16) != m_nonceCount) {
	    Debug(DebugNote,"SASL::parseMD5ChallengeRsp() invalid nonce count='%s' [%p]",
		TelEngine::c_safe(tmp),this);
	    break;
	}
	ok = true;
	break;
    }
    if (ok)
	return true;
    TelEngine::destruct(m_params);
    return false;
}

// Build a Digest MD5 SASL to be sent with authentication responses
// See RFC 2831 2.1.2.1
void SASL::buildMD5Digest(String& dest, const NamedList& params,
    const char* password, bool challengeRsp)
{
    const char* nonce = params.getValue("nonce");
    const char* cnonce = params.getValue("cnonce");
    String qop = params.getValue("qop","auth");
    MD5 md5;
    md5 << params.getValue("username") << ":" << params.getValue("realm");
    md5 << ":" << password;
    MD5 md5A1(md5.rawDigest(),16);
    md5A1 << ":" << nonce << ":" << cnonce;
    const char* authzid = params.getValue("authzid");
    if (authzid)
	md5A1 << ":" << authzid;
    MD5 md5A2;
    if (challengeRsp)
	md5A2 << "AUTHENTICATE";
    md5A2 << ":" << params.getValue("digest-uri");
    if (qop != "auth")
	md5A2 << ":" << String('0',32);
    MD5 md5Rsp;
    md5Rsp << md5A1.hexDigest();
    md5Rsp << ":" << nonce << ":" << params.getValue("nc");
    md5Rsp << ":" << cnonce << ":" << qop << ":" << md5A2.hexDigest();
    dest = md5Rsp.hexDigest();
}


/*
 * JBConnect
 */
// Constructor. Add itself to the stream's engine
JBConnect::JBConnect(const JBStream& stream)
    : m_status(Start), m_domain(stream.serverHost()), m_port(0),
    m_engine(stream.engine()), m_stream(stream.toString()),
    m_streamType((JBStream::Type)stream.type())
{
    bool redir = false;
    stream.connectAddr(m_address,m_port,m_localIp,m_status,m_srvs,&redir);
    if (redir && m_address) {
	char c = m_address[0];
	if ((c < '0' || c > '9') && c != '[' && m_address[m_address.length() - 1] != ']') {
	    // Redirect to domain: replace stream domain, clear address
	    m_domain = m_address;
	    m_address.clear();
	}
	else {
	    // Redirect to IP address: clear stream domain
	    m_domain.clear();
	}
    }
    if (m_engine)
	m_engine->connectStatus(this,true);
}

// Remove itself from engine
JBConnect::~JBConnect()
{
    terminated(0,true);
}

// Stop the thread
void JBConnect::stopConnect()
{
    Debug(m_engine,DebugStub,"JBConnect::stopConnect() not implemented!");
}

// Retrieve the stream name
const String& JBConnect::toString() const
{
    return m_stream;
}

// Connect the socket.
void JBConnect::connect()
{
    if (!m_engine)
	return;
    Debug(m_engine,DebugAll,"JBConnect(%s) starting stat=%s [%p]",
	m_stream.c_str(),lookup(m_status,s_statusName),this);
    int port = m_port;
    if (!port) {
	if (m_streamType == JBStream::c2s)
	    port = XMPP_C2S_PORT;
	else if (m_streamType == JBStream::s2s)
	    port = XMPP_S2S_PORT;
	else {
	    Debug(m_engine,DebugNote,"JBConnect(%s) no port for %s stream [%p]",
		m_stream.c_str(),lookup(m_streamType,JBStream::s_typeName),this);
	    return;
	}
    }
    Socket* sock = 0;
    bool stop = false;
    advanceStatus();
    // Try to use ip/port
    if (m_status == Address) {
	if (m_address && port) {
	    sock = connect(m_address,port,stop);
	    if (sock || stop || exiting(sock)) {
		terminated(sock,false);
		return;
	    }
	}
	advanceStatus();
    }
    if (m_status == Srv && m_domain) {
	if (!m_srvs.skipNull()) {
	    // Get SRV records from remote party
	    String query;
	    if (m_streamType == JBStream::c2s)
		query = "_xmpp-client._tcp.";
	    else
		query = "_xmpp-server._tcp.";
	    query << m_domain;
	    String error;
	    // Start connecting timeout
	    if (!notifyConnecting(true,true))
		return;
	    int code = 0;
	    if (Resolver::init())
		code = Resolver::srvQuery(query,m_srvs,&error);
	    // Stop the timeout if not exiting
	    if (exiting(sock) || !notifyConnecting(false,true)) {
		terminated(0,false);
		return;
	    }
	    if (!code)
		DDebug(m_engine,DebugAll,"JBConnect(%s) SRV query for '%s' got %u records [%p]",
		    m_stream.c_str(),query.c_str(),m_srvs.count(),this);
	    else
		Debug(m_engine,DebugNote,"JBConnect(%s) SRV query for '%s' failed: %d '%s' [%p]",
		    m_stream.c_str(),query.c_str(),code,error.c_str(),this);
	}
	else
	    // Remove the first entry: we already used it
	    m_srvs.remove();
	ObjList* o = 0;
	while (0 != (o = m_srvs.skipNull())) {
	    SrvRecord* rec = static_cast<SrvRecord*>(o->get());
	    sock = connect(rec->address(),rec->port(),stop);
	    o->remove();
	    if (sock || stop || exiting(sock)) {
		terminated(sock,false);
		return;
	    }
	}
	advanceStatus();
    }
    else if (m_status == Srv)
	advanceStatus();
    if (m_status == Domain) {
	// Try to resolve the domain
	if (port && m_domain)
	    sock = connect(m_domain,port,stop);
	advanceStatus();
    }
    terminated(sock,false);
}

// Create and try to connect a socket. Return it on success
// Set stop on fatal failure and return 0
Socket* JBConnect::connect(const char* addr, int port, bool& stop)
{
    Socket* sock = new Socket(PF_INET,SOCK_STREAM);
    // Bind to local ip
    if (m_localIp) {
	SocketAddr lip(PF_INET);
	lip.host(m_localIp);
	bool ok = false;
	if (lip.host()) {
	    ok = sock->bind(lip);
	    if (!ok) {
		String tmp;
		Thread::errorString(tmp,sock->error());
		Debug(m_engine,DebugNote,
		    "JBConnect(%s) failed to bind to '%s' (%s). %d '%s' [%p]",
		    m_stream.c_str(),lip.host().c_str(),m_localIp.c_str(),
		    sock->error(),tmp.c_str(),this);
	    }
	}
	else
	    Debug(m_engine,DebugNote,"JBConnect(%s) invalid local ip '%s' [%p]",
		m_stream.c_str(),m_localIp.c_str(),this);
	stop = !ok || exiting(sock);
	if (stop) {
	    deleteSocket(sock);
	    return 0;
	}
	DDebug(m_engine,DebugAll,"JBConnect(%s) bound to '%s' (%s) [%p]",
	    m_stream.c_str(),lip.host().c_str(),m_localIp.c_str(),this);
    }
    // Use async connect
    u_int64_t tout = 0;
    if (m_engine)
	tout = m_engine->m_connectTimeout * 1000;
    if (tout && !(sock->canSelect() && sock->setBlocking(false))) {
	tout = 0;
	if (sock->canSelect()) {
	    String tmp;
	    Thread::errorString(tmp,sock->error());
	    Debug(m_engine,DebugInfo,
		"JBConnect(%s) using sync connect (async set failed). %d '%s' [%p]",
		m_stream.c_str(),sock->error(),tmp.c_str(),this);
	}
	else
	    Debug(m_engine,DebugInfo,
		"JBConnect(%s) using sync connect (select() not available) [%p]",
		m_stream.c_str(),this);
    }
    if (!notifyConnecting(tout == 0)) {
	stop = true;
	deleteSocket(sock);
	return 0;
    }
    u_int64_t start = tout ? Time::now() : 0;
    SocketAddr a(PF_INET);
    a.host(addr);
    a.port(port);
    // Check exiting: it may take some time to resolve the domain
    stop = exiting(sock);
    if (stop)
	return 0;
    if (!a.host()) {
	Debug(m_engine,DebugNote,"JBConnect(%s) failed to resolve '%s' [%p]",
	    m_stream.c_str(),addr,this);
	deleteSocket(sock);
	return 0;
    }
    unsigned int intervals = 0;
    if (start) {
	start = Time::now() - start;
	if (tout > start)
	    intervals = (unsigned int)(tout - start) / Thread::idleUsec();
	// Make sure we wait for at least 1 timeout interval
	if (!intervals)
	    intervals = 1;
    }
    String domain;
    if (a.host() != addr)
	domain << " (" << addr << ")";
    Debug(m_engine,DebugAll,"JBConnect(%s) attempt to connect to '%s:%d'%s [%p]",
	m_stream.c_str(),a.host().c_str(),a.port(),domain.safe(),this);
    bool ok = (0 != sock->connect(a));
    bool timeout = false;
    // Async connect in progress
    if (!ok && sock->inProgress()) {
	bool done = false;
	bool event = false;
	while (intervals && !(done || event || stop)) {
	    if (!sock->select(0,&done,&event,Thread::idleUsec()))
	        break;
	    intervals--;
	    stop = exiting(sock);
	}
	timeout = !intervals && !(done || event);
	if (sock && !sock->error() && (done || event) && sock->updateError())
	    ok = !sock->error();
    }
    if (ok) {
	Debug(m_engine,DebugAll,"JBConnect(%s) connected to '%s:%d'%s [%p]",
	    m_stream.c_str(),a.host().c_str(),a.port(),domain.safe(),this);
	return sock;
    }
    if (sock) {
	String reason;
	if (timeout)
	    reason = "Timeout";
	else {
	    String tmp;
	    Thread::errorString(tmp,sock->error());
	    reason << sock->error() << " '" << tmp << "'";
	}
	Debug(m_engine,DebugNote,"JBConnect(%s) failed to connect to '%s:%d'%s. %s [%p]",
	    m_stream.c_str(),a.host().c_str(),a.port(),domain.safe(),reason.safe(),this);
	deleteSocket(sock);
    }
    return 0;
}

// Check if exiting. Release socket
bool JBConnect::exiting(Socket*& sock)
{
    bool done = Thread::check(false) || !m_engine || m_engine->exiting();
    if (done && sock)
	deleteSocket(sock);
    return done;
}

// Notify termination, remove from engine
void JBConnect::terminated(Socket* sock, bool final)
{
    bool done = exiting(sock);
    JBEngine* engine = m_engine;
    m_engine = 0;
    // Remove from engine
    if (engine)
	engine->connectStatus(this,false);
    if (done) {
	if (!final && Thread::check(false))
	    Debug(m_engine,DebugAll,"JBConnect(%s) cancelled [%p]",m_stream.c_str(),this);
	return;
    }
    JBStream* stream = engine->findStream(m_stream,m_streamType);
    if (!final)
	Debug(engine,DebugAll,"JBConnect(%s) terminated [%p]",m_stream.c_str(),this);
    else if (stream)
	Debug(engine,DebugWarn,"JBConnect(%s) abnormally terminated! [%p]",
	    m_stream.c_str(),this);
    // Notify stream
    if (stream) {
	stream->connectTerminated(sock);
	TelEngine::destruct(stream);
    }
    else {
	deleteSocket(sock);
	DDebug(engine,DebugInfo,"JBConnect(%s) stream vanished while connecting [%p]",
	    m_stream.c_str(),this);
    }
}

// Notify connecting to the stream. Return false if stream vanished
bool JBConnect::notifyConnecting(bool sync, bool useCurrentStat)
{
    JBStream* stream = m_engine ? m_engine->findStream(m_stream,m_streamType) : 0;
    if (!stream)
	return false;
    int stat = m_status;
    if (!useCurrentStat) {
	// Advertised state:
	// Srv --> Address: we'll advance the state on retry
	// Domain --> Start to re-start on retry
	if (m_status == Srv)
	    stat = Address;
	else if (m_status == Domain)
	    stat = Start;
    }
    bool ok = stream->connecting(sync,stat,m_srvs);
    TelEngine::destruct(stream);
    return ok;
}

// Delete a socket
void JBConnect::deleteSocket(Socket*& sock)
{
    if (!sock)
	return;
    sock->setReuse();
    sock->setLinger(0);
    delete sock;
    sock = 0;
}

// Advance the status
void JBConnect::advanceStatus()
{
    if (m_status == Start)
	m_status = Address;
    else if (m_status == Address) {
	if (m_domain) {
	    if (!m_port &&
		(m_streamType == JBStream::c2s || m_streamType == JBStream::s2s))
		m_status = Srv;
	    else
		m_status = Domain;
	}
	else
	    m_status = Start;
    }
    else if (m_status == Srv)
	m_status = Domain;
    else if (m_status == Domain)
	m_status = Start;
    else
	m_status = Address;
}


/*
 * JBEngine
 */
JBEngine::JBEngine(const char* name)
    : Mutex(true,"JBEngine"),
    m_exiting(false),
    m_restartMax(JB_RESTART_COUNT), m_restartUpdInterval(JB_RESTART_UPDATE),
    m_setupTimeout(JB_SETUP_INTERVAL), m_startTimeout(JB_START_INTERVAL),
    m_connectTimeout(JB_CONNECT_INTERVAL), m_srvTimeout(JB_SRV_INTERVAL),
    m_pingInterval(JB_PING_INTERVAL), m_pingTimeout(JB_PING_TIMEOUT),
    m_idleTimeout(0), m_pptTimeoutC2s(0), m_pptTimeout(0),
    m_streamReadBuffer(JB_STREAMBUF), m_maxIncompleteXml(XMPP_MAX_INCOMPLETEXML),
    m_redirectMax(JB_REDIRECT_COUNT),
    m_hasClientTls(true), m_printXml(0), m_initialized(false)
{
    debugName(name);
    XDebug(this,DebugAll,"JBEngine [%p]",this);
}

JBEngine::~JBEngine()
{
    XDebug(this,DebugAll,"~JBEngine [%p]",this);
}

// Cleanup streams. Stop all threads owned by this engine. Release memory
void JBEngine::destruct()
{
    cleanup(true,false);
    GenObject::destruct();
}

// Initialize the engine's parameters
void JBEngine::initialize(const NamedList& params)
{
    int lvl = params.getIntValue("debug_level",-1);
    if (lvl != -1)
	debugLevel(lvl);
    JBClientEngine* client = YOBJECT(JBClientEngine,this);
    String tmp = params.getValue("printxml");
    if (!tmp && client)
	tmp = "verbose";
    m_printXml = tmp.toBoolean() ? -1: ((tmp == "verbose") ? 1 : 0);

    m_streamReadBuffer = fixValue(params,"stream_readbuffer",
	JB_STREAMBUF,JB_STREAMBUF_MIN,(unsigned int)-1);
    m_maxIncompleteXml = fixValue(params,"stream_parsermaxbuffer",
	XMPP_MAX_INCOMPLETEXML,1024,(unsigned int)-1);
    m_restartMax = fixValue(params,"stream_restartcount",
	JB_RESTART_COUNT,JB_RESTART_COUNT_MIN,JB_RESTART_COUNT_MAX);
    m_restartUpdInterval = fixValue(params,"stream_restartupdateinterval",
	JB_RESTART_UPDATE,JB_RESTART_UPDATE_MIN,JB_RESTART_UPDATE_MAX);
    m_setupTimeout = fixValue(params,"stream_setuptimeout",
	JB_SETUP_INTERVAL,JB_SETUP_INTERVAL_MIN,JB_SETUP_INTERVAL_MAX);
    m_startTimeout = fixValue(params,"stream_starttimeout",
	JB_START_INTERVAL,JB_START_INTERVAL_MIN,JB_START_INTERVAL_MAX);
    m_connectTimeout = fixValue(params,"stream_connecttimeout",
	JB_CONNECT_INTERVAL,JB_CONNECT_INTERVAL_MIN,JB_CONNECT_INTERVAL_MAX);
    m_srvTimeout = fixValue(params,"stream_srvtimeout",
	JB_SRV_INTERVAL,JB_SRV_INTERVAL_MIN,JB_SRV_INTERVAL_MAX);
    m_pingInterval = fixValue(params,"stream_pinginterval",
	client ? JB_PING_INTERVAL : 0,JB_PING_INTERVAL_MIN,JB_PING_INTERVAL_MAX,true);
    m_pingTimeout = fixValue(params,"stream_pingtimeout",
	client ? JB_PING_TIMEOUT : 0,JB_PING_TIMEOUT_MIN,JB_PING_TIMEOUT_MAX,true);
    if (!(m_pingInterval && m_pingTimeout))
	m_pingInterval = m_pingTimeout = 0;
    m_idleTimeout = fixValue(params,"stream_idletimeout",
	JB_IDLE_INTERVAL,JB_IDLE_INTERVAL_MIN,JB_IDLE_INTERVAL_MAX);
    int defVal = JB_REDIRECT_COUNT;
    if (client)
	defVal = JB_REDIRECT_COUNT_CLIENT;
    m_redirectMax = params.getIntValue("stream_redirectcount",
	defVal,JB_REDIRECT_MIN,JB_REDIRECT_MAX);
    m_pptTimeoutC2s = params.getIntValue("stream_ppttimeout_c2s",10000,0,120000);
    m_pptTimeout = params.getIntValue("stream_ppttimeout",60000,0,180000);
    m_initialized = true;
}

// Terminate all streams
void JBEngine::cleanup(bool final, bool waitTerminate)
{
    DDebug(this,DebugAll,"JBEngine::cleanup() final=%s wait=%s",
	String::boolText(final),String::boolText(waitTerminate));
    dropAll(JBStream::TypeCount,JabberID::empty(),JabberID::empty(),
	XMPPError::Shutdown);
    lock();
    ObjList* found = m_connect.skipNull();
    if (found) {
	Debug(this,DebugAll,"Terminating %u stream connect threads",m_connect.count());
	for (ObjList* o = found; o; o = o->skipNext()) {
	    JBConnect* conn = static_cast<JBConnect*>(o->get());
	    XDebug(this,DebugAll,"Terminating connect thread (%p)",conn);
	    conn->stopConnect();
	}
    }
    unlock();
    if (found) {
	XDebug(this,DebugAll,"Waiting for stream connect threads to terminate");
	while (found) {
	    Thread::yield(false);
	    Lock lock(this);
	    found = m_connect.skipNull();
	}
	Debug(this,DebugAll,"Stream connect threads terminated");
    }
    stopStreamSets(waitTerminate);
}

// Accept an incoming stream connection. Build a stream
bool JBEngine::acceptConn(Socket* sock, SocketAddr& remote, JBStream::Type t, bool ssl)
{
    if (!sock)
	return false;
    if (exiting()) {
	Debug(this,DebugNote,
	    "Can't accept connection from '%s:%d' type='%s': engine is exiting",
	    remote.host().c_str(),remote.port(),lookup(t,JBStream::s_typeName));
	return false;
    }
    if (ssl && t != JBStream::c2s) {
	Debug(this,DebugNote,"SSL connection on non c2s stream");
	return false;
    }
    JBStream* s = 0;
    if (t == JBStream::c2s)
	s = new JBClientStream(this,sock,ssl);
    else if (t == JBStream::s2s)
	s = new JBServerStream(this,sock,false);
    else if (t == JBStream::comp)
	s = new JBServerStream(this,sock,true);
    else if (t == JBStream::cluster)
	s = new JBClusterStream(this,sock);
    if (s)
	addStream(s);
    else
	Debug(this,DebugNote,"Can't accept connection from '%s:%d' type='%s'",
	    remote.host().c_str(),remote.port(),lookup(t,JBStream::s_typeName));
    return s != 0;
}

// Find a stream by its name
JBStream* JBEngine::findStream(const String& id, JBStream::Type hint)
{
    if (!id)
	return 0;
    RefPointer<JBStreamSetList> list[JBStream::TypeCount];
    getStreamLists(list,hint);
    for (unsigned int i = 0; i < JBStream::TypeCount; i++) {
	if (!list[i])
	    continue;
	JBStream* stream = JBEngine::findStream(id,list[i]);
	if (stream) {
	    for (; i < JBStream::TypeCount; i++)
		list[i] = 0;
	    return stream;
	}
	list[i] = 0;
    }
    return 0;
}

// Find all c2s streams whose local or remote bare jid matches a given one
ObjList* JBEngine::findClientStreams(bool in, const JabberID& jid, int flags)
{
    if (!jid.node())
	return 0;
    RefPointer<JBStreamSetList> list;
    getStreamList(list,JBStream::c2s);
    if (!list)
	return 0;
    ObjList* result = 0;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    JBClientStream* stream = static_cast<JBClientStream*>(s->get());
	    // Ignore destroying streams
	    if (stream->incoming() != in || stream->state() == JBStream::Destroy)
		continue;
	    Lock lock(stream);
	    const JabberID& sid = in ? stream->remote() : stream->local();
	    if (sid.bare() == jid.bare() && stream->flag(flags) && stream->ref()) {
		if (!result)
		    result = new ObjList;
		result->append(stream);
	    }
	}
    }
    list->unlock();
    list = 0;
    return result;
}

// Find all c2s streams whose local or remote bare jid matches a given one and
//  their resource is found in the given list
ObjList* JBEngine::findClientStreams(bool in, const JabberID& jid, const ObjList& resources,
    int flags)
{
    if (!jid.node())
	return 0;
    RefPointer<JBStreamSetList> list;
    getStreamList(list,JBStream::c2s);
    if (!list)
	return 0;
    ObjList* result = 0;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    JBClientStream* stream = static_cast<JBClientStream*>(s->get());
	    // Ignore destroying streams
	    if (stream->incoming() != in || stream->state() == JBStream::Destroy)
		continue;
	    Lock lock(stream);
	    const JabberID& sid = in ? stream->remote() : stream->local();
	    if (sid.bare() == jid.bare() && resources.find(sid.resource()) &&
		stream->flag(flags) && stream->ref()) {
		if (!result)
		    result = new ObjList;
		result->append(stream);
	    }
	}
    }
    list->unlock();
    list = 0;
    return result;
}

// Find a c2s stream by its local or remote jid
JBClientStream* JBEngine::findClientStream(bool in, const JabberID& jid)
{
    if (!jid.node())
	return 0;
    RefPointer<JBStreamSetList> list;
    getStreamList(list,JBStream::c2s);
    if (!list)
	return 0;
    JBClientStream* found = 0;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    found = static_cast<JBClientStream*>(s->get());
	    // Ignore destroying streams
	    if (found->incoming() != in || found->state() == JBStream::Destroy)
		continue;
	    Lock lock(found);
	    const JabberID& sid = in ? found->remote() : found->local();
	    if (sid == jid && found->ref())
		break;
	    found = 0;
	}
	if (found)
	    break;
    }
    list->unlock();
    list = 0;
    return found;
}

// Terminate all streams matching type and/or local/remote jid
unsigned int JBEngine::dropAll(JBStream::Type type, const JabberID& local,
    const JabberID& remote, XMPPError::Type error, const char* reason)
{
    XDebug(this,DebugInfo,"dropAll(%s,%s,%s,%s,%s)",lookup(type,JBStream::s_typeName),
	local.c_str(),remote.c_str(),XMPPUtils::s_error[error].c_str(),reason);
    RefPointer<JBStreamSetList> list[JBStream::TypeCount];
    getStreamLists(list,type);
    unsigned int n = 0;
    for (unsigned int i = 0; i < JBStream::TypeCount; i++) {
	if (!list[i])
	    continue;
	list[i]->lock();
	for (ObjList* o = list[i]->sets().skipNull(); o; o = o->skipNext()) {
	    JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	    n += set->dropAll(local,remote,error,reason);
	}
	list[i]->unlock();
	list[i] = 0;
    }
    DDebug(this,DebugInfo,
	"Dropped %u streams type=%s local=%s remote=%s error=%s reason=%s",
	n,lookup(type,JBStream::s_typeName),local.c_str(),remote.c_str(),
	XMPPUtils::s_error[error].c_str(),reason);
    return n;
}

// Process an event
void JBEngine::processEvent(JBEvent* ev)
{
    Debug(this,DebugStub,"JBEngine::processEvent() not implemented!");
    returnEvent(ev);
}

// Return an event to this engine
void JBEngine::returnEvent(JBEvent* ev, XMPPError::Type error, const char* reason)
{
    if (!ev)
	return;
    // Send error when supported
    if (error != XMPPError::NoError)
	ev->sendStanzaError(error,reason);
    XDebug(this,DebugAll,"Deleting returned event (%p,%s)",ev,ev->name());
    TelEngine::destruct(ev);
}

// Start stream TLS
void JBEngine::encryptStream(JBStream* stream)
{
    Debug(this,DebugStub,"JBEngine::encryptStream() not implemented!");
}

// Connect an outgoing stream
void JBEngine::connectStream(JBStream* stream)
{
    Debug(this,DebugStub,"JBEngine::connectStream() not implemented!");
}

// Start stream compression
void JBEngine::compressStream(JBStream* stream, const String& formats)
{
    Debug(this,DebugStub,"JBEngine::compressStream() not implemented!");
}

// Build a dialback key
void JBEngine::buildDialbackKey(const String& id, const String& local,
    const String& remote, String& key)
{
    Debug(this,DebugStub,"JBEngine::buildDialbackKey() not implemented!");
}

// Check for duplicate stream id at a remote server
bool JBEngine::checkDupId(JBStream* stream)
{
    if (!stream || stream->incoming())
	return false;
    RefPointer<JBStreamSetList> list;
    getStreamList(list,stream->type());
    if (!list)
	return false;
    stream->lock();
    String domain = stream->remote().domain();
    String id = stream->id();
    stream->unlock();
    list->lock();
    JBStream* found = 0;
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    found = static_cast<JBStream*>(s->get());
	    if (found != stream && found->outgoing()) {
		// Lock the stream: its data might change
		Lock lock(found);
		// Ignore destroying streams
		if (found->remote().domain() == domain &&
		    found->id() == id && found->state() != JBStream::Destroy)
		    break;
	    }
	    found = 0;
	}
	if (found)
	    break;
    }
    list->unlock();
    list = 0;
    return found != 0;
}

// Print XML to output
void JBEngine::printXml(const JBStream* stream, bool send, XmlChild& xml) const
{
    if (!(m_printXml && debugAt(DebugInfo)))
	return;
    String s;
    if (m_printXml > 0)
	s << "\r\n-----";
    XMPPUtils::print(s,xml,m_printXml > 0);
    if (m_printXml > 0)
	s << "\r\n-----";
    const char* dir = send ? "Sending to" : "Receiving from";
    if (m_printXml < 0)
	Debug(stream,DebugInfo,"%s '%s' %s [%p]",dir,stream->remote().c_str(),s.c_str(),stream);
    else
	Debug(stream,DebugInfo,"%s '%s' [%p]%s",dir,stream->remote().c_str(),stream,s.c_str());
}

// Print an XML fragment to output
void JBEngine::printXml(const JBStream* stream, bool send, XmlFragment& frag) const
{
    if (!(m_printXml && debugAt(DebugInfo)))
	return;
    String s;
    if (m_printXml > 0)
	s << "\r\n-----";
    for (ObjList* o = frag.getChildren().skipNull(); o; o = o->skipNext())
	XMPPUtils::print(s,*static_cast<XmlChild*>(o->get()),m_printXml > 0);
    if (m_printXml > 0)
	s << "\r\n-----";
    const char* dir = send ? "Sending to" : "Receiving from";
    if (m_printXml < 0)
	Debug(stream,DebugInfo,"%s '%s' %s [%p]",dir,stream->remote().c_str(),s.c_str(),stream);
    else
	Debug(stream,DebugInfo,"%s '%s' [%p]%s",dir,stream->remote().c_str(),stream,s.c_str());
}

// Add a stream to one of the stream lists
void JBEngine::addStream(JBStream* stream)
{
    Debug(this,DebugStub,"JBEngine::addStream() not implemented!");
}

// Remove a stream
void JBEngine::removeStream(JBStream* stream, bool delObj)
{
    if (!stream)
	return;
    stopConnect(stream->toString());
}

// Add/remove a connect stream thread when started/stopped
void JBEngine::connectStatus(JBConnect* conn, bool started)
{
    if (!conn)
	return;
    Lock lock(this);
    if (started) {
	// Make sure we remove any existing connect stream with the same name
	stopConnect(conn->toString());
	m_connect.append(conn)->setDelete(false);
	DDebug(this,DebugAll,"Added stream connect thread (%p)",conn);
    }
    else {
	GenObject* o = m_connect.remove(conn,false);
	if (o)
	    DDebug(this,DebugAll,"Removed stream connect thread (%p)",conn);
    }
}

// Stop a connect stream
void JBEngine::stopConnect(const String& name)
{
    Lock lock(this);
    ObjList* o = m_connect.find(name);
    if (!o)
	return;
    JBConnect* conn = static_cast<JBConnect*>(o->get());
    Debug(this,DebugAll,"Stopping stream connect thread (%p,%s)",conn,name.c_str());
    conn->stopConnect();
    o->remove(false);
}

// Find a stream by its name in a given set list
JBStream* JBEngine::findStream(const String& id, JBStreamSetList* list)
{
    if (!list)
	return 0;
    Lock lock(list);
    ObjList* found = 0;
    for (ObjList* o = list->sets().skipNull(); !found && o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	found = set->clients().find(id);
    }
    JBStream* stream = found ? static_cast<JBStream*>(found->get()) : 0;
    if (stream && !stream->ref())
	stream = 0;
    return stream;
}


/*
 * JBServerEngine
 */
JBServerEngine::JBServerEngine(const char* name)
    : JBEngine(name),
    m_streamIndex(0),
    m_c2sReceive(0), m_c2sProcess(0), m_s2sReceive(0), m_s2sProcess(0),
    m_compReceive(0), m_compProcess(0), m_clusterReceive(0), m_clusterProcess(0)
{
}

JBServerEngine::~JBServerEngine()
{
}

// Terminate all streams
void JBServerEngine::cleanup(bool final, bool waitTerminate)
{
    JBEngine::cleanup(final,waitTerminate);
    DDebug(this,DebugAll,"JBServerEngine::cleanup() final=%s wait=%s",
	String::boolText(final),String::boolText(waitTerminate));
    if (!final)
	return;
    Lock lock(this);
    TelEngine::destruct(m_c2sReceive);
    TelEngine::destruct(m_c2sProcess);
    TelEngine::destruct(m_s2sReceive);
    TelEngine::destruct(m_s2sProcess);
    TelEngine::destruct(m_compReceive);
    TelEngine::destruct(m_compProcess);
    TelEngine::destruct(m_clusterReceive);
    TelEngine::destruct(m_clusterProcess);
}

// Stop all stream sets
void JBServerEngine::stopStreamSets(bool waitTerminate)
{
    XDebug(this,DebugAll,"JBServerEngine::stopStreamSets() wait=%s",
	String::boolText(waitTerminate));
    lock();
    RefPointer<JBStreamSetList> sets[] = {m_c2sReceive,m_c2sProcess,
	m_s2sReceive,m_s2sProcess,m_compReceive,m_compProcess,
	m_clusterReceive,m_clusterProcess};
    unlock();
    int n = 2 * JBStream::TypeCount;
    for (int i = 0; i < n; i++)
	if (sets[i])
	    sets[i]->stop(0,waitTerminate);
    for (int j = 0; j < n; j++)
	sets[j] = 0;
}

// Retrieve the list of streams of a given type
void JBServerEngine::getStreamList(RefPointer<JBStreamSetList>& list, int type)
{
    Lock lock(this);
    if (type == JBStream::c2s)
	list = m_c2sReceive;
    else if (type == JBStream::s2s)
	list = m_s2sReceive;
    else if (type == JBStream::comp)
	list = m_compReceive;
    else if (type == JBStream::cluster)
	list = m_clusterReceive;
}

// Retrieve the stream lists of a given type
void JBServerEngine::getStreamListsType(int type, RefPointer<JBStreamSetList>& recv,
    RefPointer<JBStreamSetList>& process)
{
    if (type == JBStream::c2s) {
	recv = m_c2sReceive;
	process = m_c2sProcess;
    }
    else if (type == JBStream::s2s) {
	recv = m_s2sReceive;
	process = m_s2sProcess;
    }
    else if (type == JBStream::comp) {
	recv = m_compReceive;
	process = m_compProcess;
    }
    else if (type == JBStream::cluster) {
	recv = m_clusterReceive;
	process = m_clusterProcess;
    }
}

// Find a server to server or component stream by local/remote domain.
// Skip over outgoing dialback streams
JBServerStream* JBServerEngine::findServerStream(const String& local, const String& remote,
    bool out, bool auth)
{
    if (!(local && remote))
	return 0;
    lock();
    RefPointer<JBStreamSetList> list[2] = {m_s2sReceive,m_compReceive};
    unlock();
    JBServerStream* stream = 0;
    for (int i = 0; i < 2; i++) {
	list[i]->lock();
	for (ObjList* o = list[i]->sets().skipNull(); o; o = o->skipNext()) {
	    JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	    for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
		stream = static_cast<JBServerStream*>(s->get());
		if (stream->type() == JBStream::comp ||
		    (out == stream->outgoing() && !stream->dialback())) {
		    // Lock the stream: remote jid might change
		    Lock lock(stream);
		    if (local != stream->local()) {
			stream = 0;
			continue;
		    }
		    bool checkRemote = out || stream->type() == JBStream::comp;
		    if ((checkRemote && remote == stream->remote()) ||
			(!checkRemote && stream->hasRemoteDomain(remote,auth))) {
			stream->ref();
			break;
		    }
		}
		stream = 0;
	    }
	    if (stream)
		break;
	}
	list[i]->unlock();
	if (stream)
	    break;
    }
    list[0] = list[1] = 0;
    return stream;
}

// Create an outgoing s2s stream.
JBServerStream* JBServerEngine::createServerStream(const String& local,
    const String& remote, const char* dbId, const char* dbKey, bool dbOnly,
    const NamedList* params)
{
    if (exiting()) {
	Debug(this,DebugAll,"Can't create s2s local=%s remote=%s: engine is exiting",
	    local.c_str(),remote.c_str());
	return 0;
    }
    JBServerStream* stream = 0;
    if (!dbOnly)
	stream = findServerStream(local,remote,true);
    if (!stream) {
	stream = new JBServerStream(this,local,remote,dbId,dbKey,dbOnly,params);
	stream->ref();
	addStream(stream);
    }
    else
	TelEngine::destruct(stream);
    return stream;
}

// Create an outgoing comp stream
JBServerStream* JBServerEngine::createCompStream(const String& name, const String& local,
    const String& remote, const NamedList* params)
{
    if (exiting()) {
	Debug(this,DebugAll,"Can't create comp local=%s remote=%s: engine is exiting",
	    local.c_str(),remote.c_str());
	return 0;
    }
    JBServerStream* stream = findServerStream(local,remote,true);
    if (!stream) {
	stream = new JBServerStream(this,local,remote,&name,params);
	stream->ref();
	addStream(stream);
    }
    return stream;
}

// Find a cluster stream by remote domain
JBClusterStream* JBServerEngine::findClusterStream(const String& remote,
    JBClusterStream* skip)
{
    if (!remote)
	return 0;
    lock();
    RefPointer<JBStreamSetList> list = m_clusterReceive;
    unlock();
    JBClusterStream* stream = 0;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    stream = static_cast<JBClusterStream*>(s->get());
	    if (skip != stream) {
		Lock lock(stream);
		if (stream->state() != JBStream::Destroy &&
		    remote == stream->remote()) {
		    stream->ref();
		    break;
		}
	    }
	    stream = 0;
	}
    }
    list->unlock();
    list = 0;
    return stream;
}

// Create an outgoing cluster stream
JBClusterStream* JBServerEngine::createClusterStream(const String& local,
    const String& remote, const NamedList* params)
{
    if (exiting()) {
	Debug(this,DebugAll,"Can't create cluster local=%s remote=%s: engine is exiting",
	    local.c_str(),remote.c_str());
	return 0;
    }
    JBClusterStream* stream = findClusterStream(remote);
    if (!stream) {
	stream = new JBClusterStream(this,local,remote,params);
	stream->ref();
	addStream(stream);
    }
    return stream;
}

// Terminate all incoming c2s streams matching a given JID
unsigned int JBServerEngine::terminateClientStreams(const JabberID& jid,
    XMPPError::Type error, const char* reason)
{
    unsigned int n = 0;
    ObjList* list = findClientStreams(true,jid);
    if (!list)
	return 0;
    n = list->count();
    DDebug(this,DebugInfo,"Terminating %u incoming c2s streams jid=%s error=%s reason=%s",
	n,jid.bare().c_str(),XMPPUtils::s_tag[error].c_str(),reason);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	JBClientStream* stream = static_cast<JBClientStream*>(o->get());
	stream->terminate(-1,true,0,error,reason);
    }
    TelEngine::destruct(list);
    return n;
}

// Add a stream to one of the stream lists
void JBServerEngine::addStream(JBStream* stream)
{
    if (!stream)
	return;
    lock();
    RefPointer<JBStreamSetList> recv;
    RefPointer<JBStreamSetList> process;
    getStreamListsType(stream->type(),recv,process);
    unlock();
    if (recv && process) {
	recv->add(stream);
	process->add(stream);
    }
    else
	DDebug(this,DebugStub,"JBServerEngine::addStream() type='%s' not handled!",
	    stream->typeName());
    recv = 0;
    process = 0;
    TelEngine::destruct(stream);
}

// Remove a stream
void JBServerEngine::removeStream(JBStream* stream, bool delObj)
{
    if (!stream)
	return;
    JBEngine::removeStream(stream,delObj);
    lock();
    DDebug(this,DebugAll,"JBServerEngine::removeStream(%p,%u) id=%s",
	stream,delObj,stream->toString().c_str());
    RefPointer<JBStreamSetList> recv;
    RefPointer<JBStreamSetList> process;
    getStreamListsType(stream->type(),recv,process);
    unlock();
    if (recv)
	recv->remove(stream,delObj);
    if (process)
	process->remove(stream,delObj);
    recv = 0;
    process = 0;
}


/*
 * JBClientEngine
 */
JBClientEngine::JBClientEngine(const char* name)
    : JBEngine(name),
    m_receive(0), m_process(0)
{
}

JBClientEngine::~JBClientEngine()
{
}

// Terminate all streams
void JBClientEngine::cleanup(bool final, bool waitTerminate)
{
    JBEngine::cleanup(final,waitTerminate);
    DDebug(this,DebugAll,"JBClientEngine::cleanup() final=%s wait=%s",
	String::boolText(final),String::boolText(waitTerminate));
    if (!final)
	return;
    Lock lock(this);
    TelEngine::destruct(m_receive);
    TelEngine::destruct(m_process);
}

// Find a stream by account
JBClientStream* JBClientEngine::findAccount(const String& account)
{
    if (!account)
	return 0;
    RefPointer<JBStreamSetList> list;
    getStreamList(list,JBStream::c2s);
    if (!list)
	return 0;
    JBClientStream* found = 0;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); !found && o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    found = static_cast<JBClientStream*>(s->get());
	    if (account == found->account())
		break;
	    found = 0;
	}
    }
    if (found && !found->ref())
	found = 0;
    list->unlock();
    list = 0;
    return found;
}

// Build an outgoing client stream
JBClientStream* JBClientEngine::create(const String& account, const NamedList& params,
    const String& name)
{
    if (!account)
	return 0;
    String serverHost;
    String username = params.getValue("username");
    String domain = params.getValue("domain");
    int pos = username.find("@");
    if (pos > 0) {
	serverHost = domain;
	domain = username.substr(pos + 1);
	username = username.substr(0,pos);
    }
    if (!domain)
	domain = params.getValue("server",params.getValue("address"));
    JabberID jid(username,domain,params.getValue("resource"));
    if (!jid.bare()) {
	Debug(this,DebugNote,"Can't create client stream: invalid jid=%s",jid.bare().c_str());
	return 0;
    }
    Lock lock(this);
    JBClientStream* stream = static_cast<JBClientStream*>(findAccount(account));
    if (!stream) {
	stream = new JBClientStream(this,jid,account,params,name,serverHost);
	stream->ref();
	addStream(stream);
    }
    else
	TelEngine::destruct(stream);
    return stream;
}

// Add a stream to one of the stream lists
void JBClientEngine::addStream(JBStream* stream)
{
    if (!stream)
	return;
    lock();
    RefPointer<JBStreamSetList> recv = 0;
    RefPointer<JBStreamSetList> process = 0;
    if (stream->type() == JBStream::c2s) {
	recv = m_receive;
	process = m_process;
    }
    unlock();
    if (recv && process) {
	recv->add(stream);
	process->add(stream);
    }
    else
	DDebug(this,DebugStub,"JBClientEngine::addStream() type='%s' not handled!",
	    stream->typeName());
    recv = 0;
    process = 0;
    TelEngine::destruct(stream);
}

// Remove a stream
void JBClientEngine::removeStream(JBStream* stream, bool delObj)
{
    if (!stream)
	return;
    JBEngine::removeStream(stream,delObj);
    lock();
    DDebug(this,DebugAll,"JBClientEngine::removeStream(%p,%u) id=%s",
	stream,delObj,stream->toString().c_str());
    RefPointer<JBStreamSetList> recv;
    RefPointer<JBStreamSetList> process;
    if (stream->type() == JBStream::c2s) {
	recv = m_receive;
	process = m_process;
    }
    unlock();
    if (recv)
	recv->remove(stream,delObj);
    if (process)
	process->remove(stream,delObj);
    recv = 0;
    process = 0;
}

// Stop all stream sets
void JBClientEngine::stopStreamSets(bool waitTerminate)
{
    XDebug(this,DebugAll,"JBClientEngine::stopStreamSets() wait=%s",
	String::boolText(waitTerminate));
    lock();
    RefPointer<JBStreamSetList> receive = m_receive;
    RefPointer<JBStreamSetList> process = m_process;
    unlock();
    if (receive)
	receive->stop(0,waitTerminate);
    if (process)
	process->stop(0,waitTerminate);
    receive = 0;
    process = 0;
}

// Retrieve the list of streams of a given type
void JBClientEngine::getStreamList(RefPointer<JBStreamSetList>& list, int type)
{
    if (type != JBStream::c2s)
	return;
    Lock lock(this);
    list = m_receive;
}


/*
 * JBEvent
 */
JBEvent::~JBEvent()
{
    releaseStream(true);
    releaseXml(true);
    XDebug(DebugAll,"JBEvent::~JBEvent [%p]",this);
}

// Get a client stream from the event's stream
JBClientStream* JBEvent::clientStream()
{
    return m_stream ? m_stream->clientStream() : 0;
}

// Get a server stream from the event's stream
JBServerStream* JBEvent::serverStream()
{
    return m_stream ? m_stream->serverStream() : 0;
}

JBClusterStream* JBEvent::clusterStream()
{
    return m_stream ? m_stream->clusterStream() : 0;
}

// Delete the underlying XmlElement(s). Release the ownership.
XmlElement* JBEvent::releaseXml(bool del)
{
    m_child = 0;
    if (del) {
	TelEngine::destruct(m_element);
	return 0;
    }
    XmlElement* tmp = m_element;
    m_element = 0;
    return tmp;
}

void JBEvent::releaseStream(bool release)
{
    if (m_link && m_stream) {
	m_stream->eventTerminated(this);
	m_link = false;
    }
    if (release)
	TelEngine::destruct(m_stream);
}

// Build an 'iq' result stanza from event data
XmlElement* JBEvent::buildIqResult(bool addTags, XmlElement* child)
{
    XmlElement* xml = 0;
    if (addTags)
	xml = XMPPUtils::createIqResult(m_to,m_from,m_id,child);
    else
	xml = XMPPUtils::createIqResult(0,0,m_id,child);
    return xml;
}

// Build and send a stanza 'result' from enclosed 'iq' element
bool JBEvent::sendIqResult(XmlElement* child)
{
    if (!(m_element && m_stream && XMPPUtils::isUnprefTag(*m_element,XmlTag::Iq))) {
	TelEngine::destruct(child);
	return false;
    }
    if (m_stanzaType == "error" || m_stanzaType == "result") {
	TelEngine::destruct(child);
	return false;
    }
    XmlElement* xml = buildIqResult(true,child);
    bool ok = m_stream->state() == JBStream::Running ?
	m_stream->sendStanza(xml) : m_stream->sendStreamXml(m_stream->state(),xml);
    if (ok) {
	releaseXml(true);
	return true;
    }
    return false;
}

// Build an 'iq' error stanza from event data
XmlElement* JBEvent::buildIqError(bool addTags, XMPPError::Type error, const char* reason,
    XMPPError::ErrorType type)
{
    XmlElement* xml = 0;
    if (addTags)
	xml = XMPPUtils::createIq(XMPPUtils::IqError,m_to,m_from,m_id);
    else
	xml = XMPPUtils::createIq(XMPPUtils::IqError,0,0,m_id);
    if (!m_id)
	xml->addChild(releaseXml());
    xml->addChild(XMPPUtils::createError(type,error,reason));
    return xml;
}

// Build and send a stanza error from enclosed element
bool JBEvent::sendStanzaError(XMPPError::Type error, const char* reason,
    XMPPError::ErrorType type)
{
    if (!(m_element && m_stream && XMPPUtils::isStanza(*m_element)))
	return false;
    if (m_stanzaType == "error" || m_stanzaType == "result")
	return false;
    XmlElement* xml = new XmlElement(m_element->toString());
    xml->setAttributeValid("from",m_to);
    xml->setAttributeValid("to",m_from);
    xml->setAttributeValid("id",m_id);
    xml->setAttribute("type","error");
    xml->addChild(XMPPUtils::createError(type,error,reason));
    bool ok = m_stream->state() == JBStream::Running ?
	m_stream->sendStanza(xml) : m_stream->sendStreamXml(m_stream->state(),xml);
    if (ok) {
	releaseXml(true);
	return true;
    }
    return false;
}

bool JBEvent::init(JBStream* stream, XmlElement* element,
    const JabberID* from, const JabberID* to)
{
    bool bRet = true;
    if (stream && stream->ref())
	m_stream = stream;
    else
	bRet = false;
    m_element = element;
    if (from)
	m_from = *from;
    if (to)
	m_to = *to;
    XDebug(DebugAll,"JBEvent::init type=%s stream=(%p) xml=(%p) [%p]",
	name(),m_stream,m_element,this);
    if (!m_element)
	return bRet;

    // Most elements have these parameters:
    m_stanzaType = m_element->getAttribute("type");
    if (!from)
	m_from = m_element->getAttribute("from");
    if (!to)
	m_to = m_element->getAttribute("to");
    m_id = m_element->getAttribute("id");

    // Decode some data
    int t = XMPPUtils::tag(*m_element);
    switch (t) {
	case XmlTag::Message:
	    if (m_stanzaType != "error")
		m_text = XMPPUtils::body(*m_element);
	    else
		XMPPUtils::decodeError(m_element,m_text,m_text);
	    break;
	case XmlTag::Iq:
	case XmlTag::Presence:
	    if (m_stanzaType != "error")
		break;
	default:
	    XMPPUtils::decodeError(m_element,m_text,m_text);
    }
    return bRet;
}


/*
 * JBStreamSet
 */
// Constructor
JBStreamSet::JBStreamSet(JBStreamSetList* owner)
    : Mutex(true,"JBStreamSet"),
    m_changed(false), m_exiting(false), m_owner(owner)
{
    XDebug(m_owner->engine(),DebugAll,"JBStreamSet::JBStreamSet(%s) [%p]",
	m_owner->toString().c_str(),this);
}

// Remove from owner
JBStreamSet::~JBStreamSet()
{
    if (m_clients.skipNull())
	Debug(m_owner->engine(),DebugGoOn,
	    "JBStreamSet(%s) destroyed while owning %u streams [%p]",
	    m_owner->toString().c_str(),m_clients.count(),this);
    m_owner->remove(this);
    XDebug(m_owner->engine(),DebugAll,"JBStreamSet::~JBStreamSet(%s) [%p]",
	m_owner->toString().c_str(),this);
}

// Add a stream to the set. The stream's reference counter will be increased
bool JBStreamSet::add(JBStream* client)
{
    if (!client)
	return false;
    Lock lock(this);
    if (m_exiting || (m_owner->maxStreams() && m_clients.count() >= m_owner->maxStreams()) ||
	!client->ref())
	return false;
    m_clients.append(client);
    m_changed = true;
    DDebug(m_owner->engine(),DebugAll,"JBStreamSet(%s) added (%p,'%s') type=%s [%p]",
	m_owner->toString().c_str(),client,client->name(),client->typeName(),this);
    return true;
}

// Remove a stream from set
bool JBStreamSet::remove(JBStream* client, bool delObj)
{
    if (!client)
	return false;
    Lock lock(this);
    ObjList* o = m_clients.find(client);
    if (!o)
	return false;
    DDebug(m_owner->engine(),DebugAll,"JBStreamSet(%s) removing (%p,'%s') delObj=%u [%p]",
	m_owner->toString().c_str(),client,client->name(),delObj,this);
    o->remove(delObj);
    m_changed = true;
    return true;
}

// Terminate all streams matching and/or local/remote jid
unsigned int JBStreamSet::dropAll(const JabberID& local, const JabberID& remote,
    XMPPError::Type error, const char* reason)
{
    DDebug(m_owner->engine(),DebugAll,"JBStreamSet(%s) dropAll(%s,%s,%s,%s) [%p]",
	m_owner->toString().c_str(),local.c_str(),remote.c_str(),
	XMPPUtils::s_error[error].c_str(),reason,this);
    unsigned int n = 0;
    lock();
    for (ObjList* s = m_clients.skipNull(); s; s = s->skipNext()) {
	JBStream* stream = static_cast<JBStream*>(s->get());
	Lock lck(stream);
	bool terminate = false;
	if (!(local || remote))
	    terminate = true;
	else {
	    if (local)
		terminate = stream->local().match(local);
	    if (remote && !terminate) {
		JBServerStream* s2s = stream->incoming() ? stream->serverStream() : 0;
		if (s2s)
		    terminate = s2s->hasRemoteDomain(remote,false);
		else
		    terminate = stream->remote().match(remote);
	    }
	}
	if (terminate) {
	    if (stream->state() != JBStream::Destroy)
		n++;
	    stream->terminate(-1,true,0,error,reason);
	}
    }
    unlock();
    return n;
}

// Process the list
void JBStreamSet::run()
{
    DDebug(m_owner->engine(),DebugAll,"JBStreamSet(%s) start running [%p]",
	m_owner->toString().c_str(),this);
    ObjList* o = 0;
    while (true) {
	if (Thread::check(false)) {
	    m_exiting = true;
	    break;
	}
	lock();
	if (m_changed) {
	    o = 0;
	    m_changed = false;
	}
	else if (o)
	    o = o->skipNext();
	if (!o)
	    o = m_clients.skipNull();
	bool eof = o && !o->skipNext();
	RefPointer<JBStream> stream = o ? static_cast<JBStream*>(o->get()) : 0;
	unlock();
	if (stream) {
	    process(*stream);
	    stream = 0;
	}
	else {
	    // Lock the owner to prevent adding a new client
	    // Don't exit if a new client was already added
	    Lock lock(m_owner);
	    if (!m_changed) {
		m_exiting = true;
		break;
	    }
	}
	if (eof) {
	    if (m_owner->m_sleepMs)
		Thread::msleep(m_owner->m_sleepMs,false);
	    else
		Thread::idle(false);
	}
    }
    DDebug(m_owner->engine(),DebugAll,"JBStreamSet(%s) stop running [%p]",
	m_owner->toString().c_str(),this);
}

// Start running
bool JBStreamSet::start()
{
    Debug(m_owner->engine(),DebugStub,"JBStreamSet(%s)::start() [%p]",
	m_owner->toString().c_str(),this);
    return false;
}

// Stop running
void JBStreamSet::stop()
{
    Debug(m_owner->engine(),DebugStub,"JBStreamSet(%s)::stop() [%p]",
	m_owner->toString().c_str(),this);
}


/*
 * JBStreamSetProcessor
 */
// Calls stream's getEvent(). Pass a generated event to the engine
bool JBStreamSetProcessor::process(JBStream& stream)
{
    JBEvent* ev = stream.getEvent();
    if (!ev)
	return false;
    bool remove = (ev->type() == JBEvent::Destroy);
    m_owner->engine()->processEvent(ev);
    if (remove) {
	DDebug(m_owner->engine(),DebugAll,
	    "JBStreamSetProcessor(%s) requesting stream (%p,%s) ref %u removal [%p]",
	    m_owner->toString().c_str(),&stream,stream.toString().c_str(),
	    stream.refcount(),this);
	m_owner->engine()->removeStream(&stream,true);
    }
    return true;
}


/*
 * JBStreamSetReceive
 */
JBStreamSetReceive::JBStreamSetReceive(JBStreamSetList* owner)
    : JBStreamSet(owner)
{
    if (owner && owner->engine())
	m_buffer.assign(0,owner->engine()->streamReadBuffer());
}

// Calls stream's readSocket()
bool JBStreamSetReceive::process(JBStream& stream)
{
    return stream.readSocket((char*)m_buffer.data(),m_buffer.length());
}


/*
 * JBStreamSetList
 */
// Constructor
JBStreamSetList::JBStreamSetList(JBEngine* engine, unsigned int max,
    unsigned int sleepMs, const char* name)
    : Mutex(true,"JBStreamSetList"),
    m_engine(engine), m_name(name),
    m_max(max), m_sleepMs(sleepMs), m_streamCount(0)
{
    XDebug(m_engine,DebugAll,"JBStreamSetList::JBStreamSetList(%s) [%p]",
	m_name.c_str(),this);
}

// Destructor
JBStreamSetList::~JBStreamSetList()
{
    XDebug(m_engine,DebugAll,"JBStreamSetList::~JBStreamSetList(%s) [%p]",
	m_name.c_str(),this);
}

// Add a stream to the list. Build a new set if there is no room in existing sets
bool JBStreamSetList::add(JBStream* client)
{
    if (!client || m_engine->exiting())
	return false;
    Lock lock(this);
    for (ObjList* o = m_sets.skipNull(); o; o = o->skipNext()) {
	if ((static_cast<JBStreamSet*>(o->get()))->add(client)) {
	    m_streamCount++;
	    return true;
	}
    }
    // Build a new set
    JBStreamSet* set = build();
    if (!set)
	return false;
    if (!set->add(client)) {
	lock.drop();
	TelEngine::destruct(set);
	return false;
    }
    m_streamCount++;
    m_sets.append(set);
    Debug(m_engine,DebugAll,"JBStreamSetList(%s) added set (%p) count=%u [%p]",
	m_name.c_str(),set,m_sets.count(),this);
    lock.drop();
    if (!set->start())
	TelEngine::destruct(set);
    return true;
}

// Remove a stream from list
void JBStreamSetList::remove(JBStream* client, bool delObj)
{
    if (!client)
	return;
    DDebug(m_engine,DebugAll,"JBStreamSetList(%s) removing (%p,'%s') delObj=%u [%p]",
	m_name.c_str(),client,client->name(),delObj,this);
    Lock lock(this);
    for (ObjList* o = m_sets.skipNull(); o; o = o->skipNext()) {
	if ((static_cast<JBStreamSet*>(o->get()))->remove(client,delObj)) {
	    if (m_streamCount)
		m_streamCount--;
	    return;
	}
    }
}

// Stop one set or all sets
void JBStreamSetList::stop(JBStreamSet* set, bool waitTerminate)
{
    // A set will stop when all its streams will terminate
    // Stop it now if wait is not requested
    Lock lck(this);
    if (set) {
	if (set->m_owner != this)
	    return;
	DDebug(m_engine,DebugAll,"JBStreamSetList(%s) stopping set (%p) [%p]",
	    m_name.c_str(),set,this);
	set->dropAll();
	if (!waitTerminate)
	    set->stop();
	lck.drop();
	while (true) {
	    lock();
	    bool ok = (0 == m_sets.find(set));
	    unlock();
	    if (ok)
		break;
	    Thread::yield(!waitTerminate);
	}
	DDebug(m_engine,DebugAll,"JBStreamSetList(%s) stopped set (%p) [%p]",
	    m_name.c_str(),set,this);
	return;
    }
    ObjList* o = m_sets.skipNull();
    if (!o)
	return;
    DDebug(m_engine,DebugAll,"JBStreamSetList(%s) stopping %u sets [%p]",
	m_name.c_str(),m_sets.count(),this);
    for (; o; o =  o->skipNext()) {
	set = static_cast<JBStreamSet*>(o->get());
	set->dropAll();
	if (!waitTerminate)
	    set->stop();
    }
    lck.drop();
    while (true) {
	lock();
	bool ok = (0 == m_sets.skipNull());
	unlock();
	if (ok)
	    break;
	Thread::yield(!waitTerminate);
    }
    DDebug(m_engine,DebugAll,"JBStreamSetList(%s) stopped all sets [%p]",
	m_name.c_str(),this);
}

// Get the string representation of this list
const String& JBStreamSetList::toString() const
{
    return m_name;
}

// Stop all sets. Release memory
void JBStreamSetList::destroyed()
{
    stop(0,true);
    RefObject::destroyed();
}

//Remove a set from list without deleting it
void JBStreamSetList::remove(JBStreamSet* set)
{
    if (!set)
	return;
    Lock lock(this);
    ObjList* o = m_sets.find(set);
    if (!o)
	return;
    o->remove(false);
    Debug(m_engine,DebugAll,"JBStreamSetList(%s) removed set (%p) count=%u [%p]",
	m_name.c_str(),set,m_sets.count(),this);
}

// Build a specialized stream set. Descendants must override this method
JBStreamSet* JBStreamSetList::build()
{
    Debug(m_engine,DebugStub,"JBStreamSetList(%s) build() not implemented! [%p]",
	m_name.c_str(),this);
    return 0;
}


/*
 * JBEntityCapsList
 */

class EntityCapsRequest : public String
{
public:
    inline EntityCapsRequest(const String& id, JBEntityCaps* caps)
	: String(id), m_caps(caps), m_expire(Time::msecNow() + 30000)
	{}
    inline ~EntityCapsRequest()
	{ TelEngine::destruct(m_caps); }
    JBEntityCaps* m_caps;
    u_int64_t m_expire;
private:
    EntityCapsRequest() {}
};

// Expire pending requests
void JBEntityCapsList::expire(u_int64_t msecNow)
{
    if (!m_enable)
	return;
    Lock lock(this);
    // Stop at the first not expired item: the other items are added after it
    for (ObjList* o = m_requests.skipNull(); o; o = o->skipNull()) {
	EntityCapsRequest* r = static_cast<EntityCapsRequest*>(o->get());
	if (r->m_caps && msecNow < r->m_expire)
	    break;
	DDebug(DebugInfo,"JBEntityCapsList request id=%s timed out [%p]",
	    r->toString().c_str(),this);
	o->remove();
    }
}

// Process a response. This method is thread safe
bool JBEntityCapsList::processRsp(XmlElement* rsp, const String& id, bool ok)
{
    if (!(rsp && id && id.startsWith(m_reqPrefix)))
	return false;
    if (!m_enable)
	return true;
    Lock lock(this);
    GenObject* o = m_requests.remove(id,false);
    if (!o) {
	DDebug(DebugInfo,"JBEntityCapsList::processRsp(%p,%s,%u) id not found [%p]",
	    &rsp,id.c_str(),ok,this);
	return true;
    }
    while (ok) {
	XmlElement* query = XMPPUtils::findFirstChild(*rsp,XmlTag::Query);
	if (!(query && XMPPUtils::hasXmlns(*query,XMPPNamespace::DiscoInfo)))
	    break;
	EntityCapsRequest* r = static_cast<EntityCapsRequest*>(o);
	JBEntityCaps* caps = r->m_caps;
	if (!caps)
	    break;
	// Check node (only for XEP 0115 ver >= 1.4)
	if (caps->m_version == JBEntityCaps::Ver1_4) {
	    String* node = query->getAttribute("node");
	    if (node && *node != (caps->m_node + "#" + caps->m_data)) {
		DDebug(DebugAll,"JBEntityCapsList response with invalid node=%s [%p]",
		    node->c_str(),this);
		break;
	    }
	}
	caps->m_features.fromDiscoInfo(*query);
	// Check hash
	if (caps->m_version == JBEntityCaps::Ver1_4) {
	    caps->m_features.updateEntityCaps();
	    if (caps->m_data != caps->m_features.m_entityCapsHash) {
		DDebug(DebugAll,"JBEntityCapsList response with invalid hash=%s (expected=%s) [%p]",
		    caps->m_features.m_entityCapsHash.c_str(),caps->m_data.c_str(),this);
		break;
	    }
	}
	r->m_caps = 0;
	// OK
	append(caps);
	capsAdded(caps);
	break;
    }
    TelEngine::destruct(o);
    return true;
}

// Request entity capabilities.
void JBEntityCapsList::requestCaps(JBStream* stream, const char* from, const char* to,
    const String& id, char version, const char* node, const char* data)
{
    if (!stream)
	return;
    Lock lock(this);
    // Make sure we don't send another disco info for the same id
    for (ObjList* o = m_requests.skipNull(); o; o = o->skipNext()) {
	EntityCapsRequest* r = static_cast<EntityCapsRequest*>(o->get());
	if (r->m_caps && id == r->m_caps)
	    return;
    }
    String reqId;
    reqId << m_reqPrefix << ++m_reqIndex;
    m_requests.append(new EntityCapsRequest(reqId,new JBEntityCaps(id,version,node,data)));
    lock.drop();
    XmlElement* d = 0;
    if (version == JBEntityCaps::Ver1_4)
	d = XMPPUtils::createIqDisco(true,true,from,to,reqId,node,data);
    else
	d = XMPPUtils::createIqDisco(true,true,from,to,reqId);
    DDebug(DebugAll,"JBEntityCapsList sending request to=%s node=%s id=%s [%p]",
	to,node,reqId.c_str(),this);
    stream->sendStanza(d);
}

// Build a document from this list
XmlDocument* JBEntityCapsList::toDocument(const char* rootName)
{
    Lock lock(this);
    XmlDocument* doc = new XmlDocument;
    XmlDeclaration* decl = new XmlDeclaration;
    XmlSaxParser::Error err = doc->addChild(decl);
    if (err != XmlSaxParser::NoError)
	TelEngine::destruct(decl);
    XmlComment* info = new XmlComment("Generated jabber entity capabilities cache");
    err = doc->addChild(info);
    if (err != XmlSaxParser::NoError)
	TelEngine::destruct(info);
    XmlElement* root = new XmlElement(rootName);
    err = doc->addChild(root);
    if (err != XmlSaxParser::NoError) {
	TelEngine::destruct(root);
	return doc;
    }
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JBEntityCaps* caps = static_cast<JBEntityCaps*>(o->get());
	XmlElement* item = new XmlElement(s_entityCapsItem);
	item->setAttribute("id",caps->c_str());
	item->setAttribute("version",String((int)caps->m_version));
	item->setAttribute("node",caps->m_node);
	item->setAttribute("data",caps->m_data);
	caps->m_features.add(*item);
	doc->addChild(item);
    }
    return doc;
}

// Build this list from an XML document
void JBEntityCapsList::fromDocument(XmlDocument& doc, const char* rootName)
{
    Lock lock(this);
    clear();
    m_requests.clear();
    XmlElement* root = doc.root();
    if (!root || (!TelEngine::null(rootName) && root->toString() != rootName)) {
	DDebug(DebugAll,"JBEntityCapsList invalid document root %p '%s' (expected=%s) [%p]",
	    root,root ? root->tag() : "",rootName,this);
	return;
    }
    XmlElement* item = root->findFirstChild(&s_entityCapsItem);
    for (; item; item = root->findNextChild(item,&s_entityCapsItem)) {
	String* id = item->getAttribute("id");
	if (TelEngine::null(id))
	    continue;
	String* tmp = item->getAttribute("version");
	JBEntityCaps* cap = new JBEntityCaps(*id,tmp ? tmp->toInteger(-1) : -1,
	    item->attribute("node"),item->attribute("data"));
	cap->m_features.fromDiscoInfo(*item);
	append(cap);
    }
    capsAdded(0);
}

// Process an element containing an entity capabily child.
// Request capabilities if not found in the list
bool JBEntityCapsList::processCaps(String& capsId, XmlElement* xml, JBStream* stream,
    const char* from, const char* to)
{
    if (!(m_enable && xml))
	return false;
    char version = 0;
    String* node = 0;
    String* ver = 0;
    String* ext = 0;
    if (!decodeCaps(*xml,version,node,ver,ext))
	return false;
    JBEntityCaps::buildId(capsId,version,*node,*ver,ext);
    Lock lock(this);
    JBEntityCaps* caps = findCaps(capsId);
    if (caps)
	return true;
    // Hack for google (doesn't support disco info, supports only disco info with node)
    if (version == JBEntityCaps::Ver1_3 &&
	(*node == s_googleTalkNode || *node == s_googleMailNode ||
	*node == s_googleAndroidNode || *node == s_googleAndroidNode2)) {
	caps = new JBEntityCaps(capsId,version,*node,*ver);
	if (ext) {
	    ObjList* list = ext->split(' ',false);
	    if (list->find("voice-v1")) {
		caps->m_features.add(XMPPNamespace::JingleSession);
		caps->m_features.add(XMPPNamespace::JingleAudio);
	    }
	    TelEngine::destruct(list);
	}
	append(caps);
	capsAdded(caps);
	return true;
    }
    if (stream)
	requestCaps(stream,from,to,capsId,version,*node,*ver);
    return stream != 0;
}

// Add capabilities to a list.
void JBEntityCapsList::addCaps(NamedList& list, JBEntityCaps& caps)
{
#define CHECK_NS(ns,param) \
    if (caps.hasFeature(ns)) { \
	params->append(param,","); \
	list.addParam(param,String::boolText(true)); \
    }
    int jingleVersion = -1;
    if (caps.m_features.get(XMPPNamespace::Jingle))
	jingleVersion = 1;
    else if (caps.m_features.get(XMPPNamespace::JingleSession) ||
	caps.m_features.get(XMPPNamespace::JingleVoiceV1))
	jingleVersion = 0;
    NamedString* params = new NamedString("caps.params");
    list.addParam("caps.id",caps.toString());
    list.addParam(params);
    if (jingleVersion != -1) {
	params->append("caps.jingle_version");
	list.addParam("caps.jingle_version",String(jingleVersion));
	if (caps.hasAudio()) {
	    params->append("caps.audio",",");
	    list.addParam("caps.audio",String::boolText(true));
	}
	switch (jingleVersion) {
	    case 1:
		CHECK_NS(XMPPNamespace::JingleTransfer,"caps.calltransfer");
		CHECK_NS(XMPPNamespace::JingleAppsFileTransfer,"caps.filetransfer");
		break;
	    case 0:
		break;
	}
	CHECK_NS(XMPPNamespace::FileInfoShare,"caps.fileinfoshare");
	CHECK_NS(XMPPNamespace::ResultSetMngt,"caps.resultsetmngt");
    }
    CHECK_NS(XMPPNamespace::Muc,"caps.muc");
#undef CHECK_NS
}

// Load (reset) this list from an XML document file.
bool JBEntityCapsList::loadXmlDoc(const char* file, DebugEnabler* enabler)
{
    if (!m_enable)
	return false;
    XmlDocument d;
    int io = 0;
    DDebug(enabler,DebugAll,"Loading entity caps from '%s'",file);
    XmlSaxParser::Error err = d.loadFile(file,&io);
    if (err == XmlSaxParser::NoError) {
	fromDocument(d);
	return true;
    }
    String error;
    if (err == XmlSaxParser::IOError) {
	String tmp;
	Thread::errorString(tmp,io);
	error << " " << io << " '" << tmp << "'";
    }
    Debug(enabler,DebugNote,"Failed to load entity caps from '%s': %s%s",
	file,XmlSaxParser::getError(err),error.safe());
    return false;
}

// Save this list to an XML document file.
bool JBEntityCapsList::saveXmlDoc(const char* file, DebugEnabler* enabler)
{
    DDebug(enabler,DebugAll,"Saving entity caps to '%s'",file);
    if (TelEngine::null(file))
	return false;
    XmlDocument* doc = toDocument();
    int res = doc->saveFile(file,true,"  ");
    if (res)
	Debug(enabler,DebugNote,"Failed to save entity caps to '%s'",file);
    delete doc;
    return res == 0;
}

// Check if an XML element has a 'c' entity capability child and process it
bool JBEntityCapsList::decodeCaps(const XmlElement& xml, char& version, String*& node,
    String*& ver, String*& ext)
{
    // Find the first entity caps child with valid node and ext
    XmlElement* c = 0;
    while (true) {
	c = XMPPUtils::findNextChild(xml,c,XmlTag::EntityCapsTag,
	    XMPPNamespace::EntityCaps);
	if (!c)
	    break;
	if (TelEngine::null(c->getAttribute("node")) ||
	    TelEngine::null(c->getAttribute("ver")))
	    continue;
	break;
    }
    if (!c)
	return false;
    // Check for a subsequent child with new entity caps if the first one is an old version
    if (!c->getAttribute("hash")) {
	XmlElement* s = c;
	while (true) {
	    s = XMPPUtils::findNextChild(xml,s,XmlTag::EntityCapsTag,
		XMPPNamespace::EntityCaps);
	    if (!s)
		break;
	    if (!s->getAttribute("hash") ||
		TelEngine::null(s->getAttribute("node")) ||
		TelEngine::null(s->getAttribute("ver")))
		continue;
	    c = s;
	    break;
	}
    }
    node = c->getAttribute("node");
    ver = c->getAttribute("ver");
    String* hash = c->getAttribute("hash");
    if (hash) {
	// Version 1.4 or greater
	if (*hash != "sha-1")
	    return false;
	version = JBEntityCaps::Ver1_4;
	ext = 0;
    }
    else {
	version = JBEntityCaps::Ver1_3;
	ext = c->getAttribute("ext");
    }
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
