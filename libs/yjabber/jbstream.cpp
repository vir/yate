/**
 * jbstream.cpp
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
#include <stdlib.h>

using namespace TelEngine;

#ifdef XDEBUG
  #define JBSTREAM_DEBUG_COMPRESS
  #define JBSTREAM_DEBUG_SOCKET
#else
//  #define JBSTREAM_DEBUG_COMPRESS                 // Show (de)compress debug
//  #define JBSTREAM_DEBUG_SOCKET                   // Show socket read/write debug
#endif

static const String s_dbVerify = "verify";
static const String s_dbResult = "result";

static inline bool isDbVerify(XmlElement& xml)
{
    const String* tag = 0;
    const String* ns = 0;
    return xml.getTag(tag,ns) && *tag == s_dbVerify &&
	ns && *ns == XMPPUtils::s_ns[XMPPNamespace::Dialback];
}

static inline bool isDbResult(XmlElement& xml)
{
    const String* tag = 0;
    const String* ns = 0;
    return xml.getTag(tag,ns) && *tag == s_dbResult &&
	ns && *ns == XMPPUtils::s_ns[XMPPNamespace::Dialback];
}

// Decode a Base64 string to a block
static inline bool decodeBase64(DataBlock& buf, const String& str)
{
    Base64 b((void*)str.c_str(),str.length(),false);
    bool ok = b.decode(buf,false);
    b.clear(false);
    return ok;
}

// Decode a Base64 string to another string
// Check if decoded data has valid UTF8 characters
static bool decodeBase64(String& buf, const String& str, JBStream* stream)
{
    DataBlock d;
    if (!decodeBase64(d,str))
	return false;
    buf.assign((const char*)d.data(),d.length());
    if (-1 != buf.lenUtf8())
	return true;
    Debug(stream,DebugNote,"Received Base64 encoded invalid UTF8 characters [%p]",stream);
    return false;
}

#ifdef DEBUG
static bool checkPing(JBStream* stream, const XmlElement* xml, const String& pingId)
{
    if (!(stream && xml && pingId))
	return false;
    if (pingId != xml->getAttribute(YSTRING("id")))
	return false;
    const char* it = xml->attribute(YSTRING("type"));
    XMPPUtils::IqType iqType = XMPPUtils::iqType(it);
    bool ok = (iqType == XMPPUtils::IqResult || iqType == XMPPUtils::IqError);
    if (ok)
	Debug(stream,DebugAll,"Ping with id=%s confirmed by '%s' [%p]",pingId.c_str(),it,stream);
    return ok;
}
#else
static inline bool checkPing(JBStream* stream, const XmlElement* xml, const String& pingId)
{
    return false;
}
#endif

static const TokenDict s_location[] = {
    {"internal",     0},
    {"remote",       1},
    {"local",       -1},
    {0,0},
};

const TokenDict JBStream::s_stateName[] = {
    {"Running",          Running},
    {"Idle",             Idle},
    {"Connecting",       Connecting},
    {"WaitStart",        WaitStart},
    {"Starting",         Starting},
    {"Features",         Features},
    {"WaitTlsRsp",       WaitTlsRsp},
    {"Auth",             Auth},
    {"Challenge",        Challenge},
    {"Securing",         Securing},
    {"Compressing",      Compressing},
    {"Register",         Register},
    {"Destroy",          Destroy},
    {0,0},
};

const TokenDict JBStream::s_flagName[] = {
    {"noautorestart",    NoAutoRestart},
    {"tlsrequired",      TlsRequired},
    {"dialback",         DialbackOnly},
    {"allowplainauth",   AllowPlainAuth},
    {"register",         RegisterUser},
    {"compress",         Compress},
    {"error",            InError},
    // Internal flags
    {"roster_requested", RosterRequested},
    {"online",           AvailableResource},
    {"secured",          StreamTls | StreamSecured},
    {"encrypted",        StreamTls},
    {"authenticated",    StreamAuthenticated},
    {"waitbindrsp",      StreamWaitBindRsp},
    {"waitsessrsp",      StreamWaitSessRsp},
    {"waitchallenge",    StreamWaitChallenge},
    {"waitchallengersp", StreamWaitChgRsp},
    {"version1",         StreamRemoteVer1},
    {"compressed",       StreamCompressed},
    {"cancompress",      StreamCanCompress},
    {0,0}
};

const TokenDict JBStream::s_typeName[] = {
    {"c2s",      c2s},
    {"s2s",      s2s},
    {"comp",     comp},
    {"cluster",  cluster},
    {0,0}
};

// Retrieve the multiplier for non client stream timers
static inline unsigned int timerMultiplier(JBStream* stream)
{
    return stream->type() == JBStream::c2s ? 1 : 2;
}


/*
 * JBStream
 */
// Incoming
JBStream::JBStream(JBEngine* engine, Socket* socket, Type t, bool ssl)
    : Mutex(true,"JBStream"),
    m_sasl(0),
    m_state(Idle), m_flags(0), m_xmlns(XMPPNamespace::Count), m_lastEvent(0),
    m_setupTimeout(0), m_startTimeout(0),
    m_pingTimeout(0), m_pingInterval(0), m_nextPing(0),
    m_idleTimeout(0), m_connectTimeout(0),
    m_restart(0), m_timeToFillRestart(0),
    m_engine(engine), m_type(t),
    m_incoming(true), m_terminateEvent(0), m_ppTerminate(0), m_ppTerminateTimeout(0),
    m_xmlDom(0), m_socket(0), m_socketFlags(0), m_socketMutex(true,"JBStream::Socket"),
    m_connectPort(0), m_compress(0), m_connectStatus(JBConnect::Start),
    m_redirectMax(0), m_redirectCount(0), m_redirectPort(0)
{
    if (ssl)
	setFlags(StreamSecured | StreamTls);
    m_engine->buildStreamName(m_name,this);
    debugName(m_name);
    debugChain(m_engine);
    Debug(this,DebugAll,"JBStream::JBStream(%p,%p,%s,%s) incoming [%p]",
	engine,socket,typeName(),String::boolText(ssl),this);
    setXmlns();
    // Don't restart incoming streams
    setFlags(NoAutoRestart);
    resetConnection(socket);
    changeState(WaitStart);
}

// Outgoing
JBStream::JBStream(JBEngine* engine, Type t, const JabberID& local, const JabberID& remote,
    const char* name, const NamedList* params, const char* serverHost)
    : Mutex(true,"JBStream"),
    m_sasl(0),
    m_state(Idle), m_local(local), m_remote(remote), m_serverHost(serverHost),
    m_flags(0), m_xmlns(XMPPNamespace::Count), m_lastEvent(0), m_stanzaIndex(0),
    m_setupTimeout(0), m_startTimeout(0),
    m_pingTimeout(0), m_nextPing(0),
    m_idleTimeout(0), m_connectTimeout(0),
    m_restart(1), m_timeToFillRestart(0),
    m_engine(engine), m_type(t),
    m_incoming(false), m_name(name),
    m_terminateEvent(0), m_ppTerminate(0), m_ppTerminateTimeout(0),
    m_xmlDom(0), m_socket(0), m_socketFlags(0), m_socketMutex(true,"JBStream::Socket"),
    m_connectPort(0), m_compress(0), m_connectStatus(JBConnect::Start),
    m_redirectMax(engine->redirectMax()), m_redirectCount(0), m_redirectPort(0)
{
    if (!m_name)
	m_engine->buildStreamName(m_name,this);
    debugName(m_name);
    debugChain(m_engine);
    if (params) {
	int flgs = XMPPUtils::decodeFlags(params->getValue("options"),s_flagName);
	setFlags(flgs & StreamFlags);
	m_connectAddr = params->getValue("server",params->getValue("address"));
	m_connectPort = params->getIntValue("port");
	m_localIp = params->getValue("localip");
    }
    else
	updateFromRemoteDef();
    // Compress always defaults to true if not explicitly disabled
    if (!flag(Compress) && !(params && params->getBoolValue("nocompression")))
	setFlags(Compress);
    Debug(this,DebugAll,"JBStream::JBStream(%p,%s,%s,%s,%s) outgoing [%p]",
	engine,typeName(),local.c_str(),remote.c_str(),m_serverHost.safe(),this);
    setXmlns();
    changeState(Idle);
}

// Destructor
JBStream::~JBStream()
{
    DDebug(this,DebugAll,"JBStream::~JBStream() id=%s [%p]",m_name.c_str(),this);
    TelEngine::destruct(m_sasl);
}

// Outgoing stream connect terminated notification.
void JBStream::connectTerminated(Socket*& sock)
{
    Lock lock(this);
    if (m_state == Connecting) {
	if (sock) {
	    resetConnection(sock);
	    sock = 0;
	    changeState(Starting);
	    XmlElement* s = buildStreamStart();
	    sendStreamXml(WaitStart,s);
	}
	else {
	    DDebug(this,DebugNote,"Connect failed [%p]",this);
	    resetConnectStatus();
	    setRedirect();
	    m_redirectCount = 0;
	    terminate(0,false,0,XMPPError::NoRemote);
	}
	return;
    }
    DDebug(this,DebugInfo,"Connect terminated notification in non %s state [%p]",
	lookup(Connecting,s_stateName),this);
    if (sock) {
	delete sock;
	sock = 0;
    }
}

// Connecting notification. Start connect timer for synchronous connect
bool JBStream::connecting(bool sync, int stat, ObjList& srvs)
{
    if (incoming() || !m_engine || state() != Connecting)
	return false;
    Lock lock(this);
    if (state() != Connecting)
	return false;
    m_connectStatus = stat;
    SrvRecord::copy(m_connectSrvs,srvs);
    if (sync) {
	if (stat != JBConnect::Srv)
	    m_connectTimeout = Time::msecNow() + m_engine->m_connectTimeout;
	else
	    m_connectTimeout = Time::msecNow() + m_engine->m_srvTimeout;
    }
    else
	m_connectTimeout = 0;
    DDebug(this,DebugAll,"Connecting sync=%u stat=%s [%p]",
	sync,lookup(m_connectStatus,JBConnect::s_statusName),this);
    return true;
}

// Get an object from this stream
void* JBStream::getObject(const String& name) const
{
    if (name == "Socket*")
	return state() == Securing ? (void*)&m_socket : 0;
    if (name == "Compressor*")
	return (void*)&m_compress;
    if (name == "JBStream")
	return (void*)this;
    return RefObject::getObject(name);
}

// Get the string representation of this stream
const String& JBStream::toString() const
{
    return m_name;
}

// Check if the stream has valid pending data
bool JBStream::haveData()
{
    Lock2 lck(this,&m_socketMutex);
    // Pending data with socket available for writing
    if (m_pending.skipNull() && socketCanWrite())
	return true;
    // Pending events
    if (m_events.skipNull())
	return true;
    // Pending incoming XML
    XmlDocument* doc = m_xmlDom ? m_xmlDom->document() : 0;
    XmlElement* root = doc ? doc->root(false) : 0;
    XmlElement* first = root ? root->findFirstChild() : 0;
    return first && first->completed();
}

// Retrieve connection address(es), port and status
void JBStream::connectAddr(String& addr, int& port, String& localip, int& stat,
    ObjList& srvs, bool* isRedirect) const
{
    if (m_redirectAddr) {
	addr = m_redirectAddr;
	port = m_redirectPort;
    }
    else {
	addr = m_connectAddr;
	port = m_connectPort;
    }
    if (isRedirect)
	*isRedirect = !m_redirectAddr.null();
    localip = m_localIp;
    stat = m_connectStatus;
    SrvRecord::copy(srvs,m_connectSrvs);
}

// Set/reset RosterRequested flag
void JBStream::setRosterRequested(bool ok)
{
    Lock lock(this);
    if (ok == flag(RosterRequested))
	return;
    if (ok)
	setFlags(RosterRequested);
    else
	resetFlags(RosterRequested);
    XDebug(this,DebugAll,"%s roster requested flag [%p]",ok ? "Set" : "Reset",this);
}

// Set/reset AvailableResource/PositivePriority flags
bool JBStream::setAvailableResource(bool ok, bool positive)
{
    Lock lock(this);
    if (ok && positive)
	setFlags(PositivePriority);
    else
	resetFlags(PositivePriority);
    if (ok == flag(AvailableResource))
	return false;
    if (ok)
	setFlags(AvailableResource);
    else
	resetFlags(AvailableResource);
    XDebug(this,DebugAll,"%s available resource flag [%p]",ok ? "Set" : "Reset",this);
    return true;
}

// Read data from socket. Send it to the parser
bool JBStream::readSocket(char* buf, unsigned int len)
{
    if (!(buf && len > 1))
	return false;
    if (!socketCanRead())
	return false;
    Lock2 lock(*this,m_socketMutex);
    if (!socketCanRead() || state() == Destroy || state() == Idle || state() == Connecting)
	return false;
    socketSetReading(true);
    if (state() != WaitTlsRsp)
	len--;
    else
	len = 1;
    lock.drop();
    // Check stream state
    XMPPError::Type error = XMPPError::NoError;
    int read = m_socket->readData(buf,len);
    Lock lck(m_socketMutex);
    // Check if the connection is waiting to be reset
    if (socketWaitReset()) {
	socketSetReading(false);
	return false;
    }
    // Check if something changed
    if (!(m_socket && socketReading())) {
	Debug(this,DebugAll,"Socket deleted while reading [%p]",this);
	return false;
    }
    if (read && read != Socket::socketError()) {
	if (!flag(StreamCompressed)) {
	    buf[read] = 0;
#ifdef JBSTREAM_DEBUG_SOCKET
	    Debug(this,DebugInfo,"Received %s [%p]",buf,this);
#endif
	    if (!m_xmlDom->parse(buf)) {
		if (m_xmlDom->error() != XmlSaxParser::Incomplete)
		    error = XMPPError::Xml;
		else if (m_xmlDom->buffer().length() > m_engine->m_maxIncompleteXml)
		    error = XMPPError::Policy;
	    }
	}
	else if (m_compress) {
#ifdef JBSTREAM_DEBUG_SOCKET
	    Debug(this,DebugInfo,"Received %d compressed bytes [%p]",read,this);
#endif
	    DataBlock d;
	    int res = m_compress->decompress(buf,read,d);
	    if (res == read) {
#ifdef JBSTREAM_DEBUG_COMPRESS
		Debug(this,DebugInfo,"Decompressed %d --> %u [%p]",read,d.length(),this);
#endif
		if (d.length()) {
		    char c = 0;
		    d.append(&c,1);
		    buf = (char*)d.data();
#ifdef JBSTREAM_DEBUG_SOCKET
		    Debug(this,DebugInfo,"Received compressed %s [%p]",buf,this);
#endif
		    if (!m_xmlDom->parse(buf)) {
			if (m_xmlDom->error() != XmlSaxParser::Incomplete)
			    error = XMPPError::Xml;
			else if (m_xmlDom->buffer().length() > m_engine->m_maxIncompleteXml)
			    error = XMPPError::Policy;
		    }
		}
	    }
	    else
		error = XMPPError::UndefinedCondition;
	}
	else
	    error = XMPPError::Internal;
    }
    socketSetReading(false);
    if (read) {
	if (read == Socket::socketError()) {
	    if (m_socket->canRetry()) {
		read = 0;
#ifdef XDEBUG
		String tmp;
		Thread::errorString(tmp,m_socket->error());
		Debug(this,DebugAll,"Socket temporary unavailable for read. %d: '%s' [%p]",
		    m_socket->error(),tmp.c_str(),this);
#endif
	    }
	    else
		error = XMPPError::SocketError;
	}
    }
    else
	error = XMPPError::SocketError;
    if (error == XMPPError::NoError) {
	// Stop reading if waiting for TLS start and received a complete element
	// We'll wait for the stream processor to handle the received element
	if (read && state() == WaitTlsRsp && !m_xmlDom->buffer().length() &&
	    m_xmlDom->unparsed() == XmlSaxParser::None) {
	    XmlDocument* doc = m_xmlDom->document();
	    // If received a complete element, the parser's current element is
	    // the document's root
	    if (doc && m_xmlDom->isCurrent(doc->root())) {
		DDebug(this,DebugAll,"Received complete element in state=%s. Stop reading [%p]",
		    stateName(),this);
		socketSetCanRead(false);
	    }
	}
	return read > 0;
    }
    // Error
    int location = 0;
    String reason;
    if (error != XMPPError::SocketError) {
	if (error == XMPPError::Xml) {
	    reason << "Parser error '" << m_xmlDom->getError() << "'";
	    Debug(this,DebugNote,"%s buffer='%s' [%p]",
		reason.c_str(),m_xmlDom->buffer().c_str(),this);
	}
	else if (error == XMPPError::UndefinedCondition) {
	    reason = "Decompression failure";
	    Debug(this,DebugNote,"Decompressor failure [%p]",this);
	}
	else if (error == XMPPError::Internal) {
	    reason = "Decompression failure";
	    Debug(this,DebugNote,"No decompressor [%p]",this);
	}
	else {
	    reason = "Parser error 'XML element too long'";
	    Debug(this,DebugNote,"Parser overflow len=%u max= %u [%p]",
		m_xmlDom->buffer().length(),m_engine->m_maxIncompleteXml,this);
	}
    }
    else if (read) {
	String tmp;
	Thread::errorString(tmp,m_socket->error());
	reason << "Socket read error: " << tmp << " (" << m_socket->error() << ")";
	Debug(this,DebugWarn,"%s [%p]",reason.c_str(),this);
    }
    else {
	reason = "Stream EOF";
	Debug(this,DebugInfo,"%s [%p]",reason.c_str(),this);
	location = 1;
    }
    socketSetCanRead(false);
    lck.drop();
    postponeTerminate(location,m_incoming,error,reason);
    return read > 0;
}

// Stream state processor
JBEvent* JBStream::getEvent(u_int64_t time)
{
    if (m_lastEvent)
	return 0;
    Lock lock(this);
    if (m_lastEvent)
	return 0;
    XDebug(this,DebugAll,"JBStream::getEvent() [%p]",this);
    checkPendingEvent();
    if (!m_lastEvent) {
	if (canProcess(time)) {
	    process(time);
	    checkPendingEvent();
	    if (!m_lastEvent)
		checkTimeouts(time);
	}
	else
	    checkPendingEvent();
    }
#ifdef XDEBUG
    if (m_lastEvent)
	Debug(this,DebugAll,"Generating event (%p,%s) in state '%s' [%p]",
	    m_lastEvent,m_lastEvent->name(),stateName(),this);
#endif
    return m_lastEvent;
}

// Send a stanza ('iq', 'message' or 'presence') or dialback elements in Running state.
bool JBStream::sendStanza(XmlElement*& xml)
{
    if (!xml)
	return false;
    DDebug(this,DebugAll,"sendStanza(%p) '%s' [%p]",xml,xml->tag(),this);
    if (!(XMPPUtils::isStanza(*xml) ||
	(m_type == s2s && XMPPUtils::hasXmlns(*xml,XMPPNamespace::Dialback)))) {
	Debug(this,DebugNote,"Request to send non stanza xml='%s' [%p]",xml->tag(),this);
	TelEngine::destruct(xml);
	return false;
    }
    XmlElementOut* xo = new XmlElementOut(xml);
    xml = 0;
    xo->prepareToSend();
    Lock lock(this);
    m_pending.append(xo);
    sendPending();
    return true;
}

// Send stream related XML when negotiating the stream
//  or some other stanza in non Running state
bool JBStream::sendStreamXml(State newState, XmlElement* first, XmlElement* second,
    XmlElement* third)
{
    DDebug(this,DebugAll,"sendStreamXml(%s,%p,%p,%p) [%p]",
	stateName(),first,second,third,this);
    Lock lock(this);
    bool ok = false;
    XmlFragment frag;
    // Use a do while() to break to the end: safe cleanup
    do {
	if (m_state == Idle || m_state == Destroy)
	    break;
	// Check if we have unsent stream xml
	if (m_outStreamXml)
	    sendPending(true);
	if (m_outStreamXml)
	    break;
	if (!first)
	    break;
	// Add stream declaration before stream start
	if (first->getTag() == XMPPUtils::s_tag[XmlTag::Stream] &&
	    first->tag()[0] != '/') {
	    XmlDeclaration* decl = new XmlDeclaration;
	    decl->toString(m_outStreamXml,true);
	    frag.addChild(decl);
	}
	first->toString(m_outStreamXml,true,String::empty(),String::empty(),false);
	frag.addChild(first);
	if (second) {
	    second->toString(m_outStreamXml,true,String::empty(),String::empty(),false);
	    frag.addChild(second);
	    if (third) {
		third->toString(m_outStreamXml,true,String::empty(),String::empty(),false);
		frag.addChild(third);
	    }
	}
	first = second = third = 0;
	if (flag(StreamCompressed) && !compress()) {
	    ok = false;
	    break;
	}
	m_engine->printXml(this,true,frag);
	ok = sendPending(true);
    } while (false);
    TelEngine::destruct(first);
    TelEngine::destruct(second);
    TelEngine::destruct(third);
    if (ok)
	changeState(newState);
    return ok;
}

// Start the stream. This method should be called by the upper layer
//  when processing an incoming stream Start event
void JBStream::start(XMPPFeatureList* features, XmlElement* caps, bool useVer1)
{
    Lock lock(this);
    if (m_state != Starting)
	return;
    if (outgoing()) {
	TelEngine::destruct(features);
	TelEngine::destruct(caps);
	if (m_type == c2s) {
	    // c2s: just wait for stream features
	    changeState(Features);
	}
	else if (m_type == s2s) {
	    // Wait features ?
	    if (flag(StreamRemoteVer1)) {
		changeState(Features);
		return;
	    }
	    // Stream not secured
	    if (!flag(StreamSecured)) {
		// Accept dialback auth stream
		// The namspace presence was already checked in checkStreamStart()
		if (flag(TlsRequired)) {
		    terminate(0,false,0,XMPPError::EncryptionRequired);
		    return;
		}
	    }
	    setFlags(StreamSecured);
	    serverStream()->sendDialback();
	}
	else if (m_type == cluster)
	    changeState(Features);
	else if (m_type == comp)
	    serverStream()->startComp();
	else
	    DDebug(this,DebugStub,"JBStream::start() not handled for type=%s",typeName());
	return;
    }
    m_features.clear();
    if (features)
	m_features.add(*features);
    if (useVer1 && flag(StreamRemoteVer1))
	setFlags(StreamLocalVer1);
    if (flag(StreamRemoteVer1) && flag(StreamLocalVer1)) {
	// Set secured flag if we don't advertise TLS
	if (!(flag(StreamSecured) || m_features.get(XMPPNamespace::Tls)))
	    setSecured();
	// Set authenticated flag if we don't advertise authentication mechanisms
	if (flag(StreamSecured)) {
	    if (flag(StreamAuthenticated))
	        m_features.remove(XMPPNamespace::Sasl);
	    else if (!m_features.get(XMPPNamespace::Sasl))
		setFlags(StreamAuthenticated);
	}
    }
    else
	// c2s using non-sasl auth or s2s not using TLS
	setSecured();
    // Send start and features
    XmlElement* s = buildStreamStart();
    XmlElement* f = 0;
    if (flag(StreamRemoteVer1) && flag(StreamLocalVer1))
	f = m_features.buildStreamFeatures();
    if (f && caps)
	f->addChild(caps);
    else
	TelEngine::destruct(caps);
    State newState = Features;
    if (m_type == c2s) {
	// Change stream state to Running if authenticated and there is no required
	// feature to negotiate
	if (flag(StreamAuthenticated) && !firstRequiredFeature())
	    newState = Running;
    }
    else if (m_type == s2s) {
	// Change stream state to Running if authenticated and features list is empty
	if (flag(StreamAuthenticated) && !m_features.skipNull())
	    newState = Running;
    }
    else if (m_type == cluster) {
	// Change stream state to Running if authenticated and features list is empty
	if (flag(StreamAuthenticated) && !m_features.skipNull())
	    newState = Running;
    }
    sendStreamXml(newState,s,f);
}

// Authenticate an incoming stream
bool JBStream::authenticated(bool ok, const String& rsp, XMPPError::Type error,
    const char* username, const char* id, const char* resource)
{
    Lock lock(this);
    if (m_state != Auth || !incoming())
	return false;
    DDebug(this,DebugAll,"authenticated(%s,'%s',%s) local=%s [%p]",
	String::boolText(ok),rsp.safe(),XMPPUtils::s_error[error].c_str(),
	m_local.c_str(),this);
    if (ok) {
	if (m_type == c2s) {
	    if (m_sasl) {
		// Set remote party node if provided
		if (!TelEngine::null(username)) {
		    m_remote.set(username,m_local.domain(),"");
		    Debug(this,DebugAll,"Remote party set to '%s' [%p]",m_remote.c_str(),this);
		}
		String text;
		m_sasl->buildAuthRspReply(text,rsp);
		XmlElement* s = XMPPUtils::createElement(XmlTag::Success,
		    XMPPNamespace::Sasl,text);
		ok = sendStreamXml(WaitStart,s);
	    }
	    else if (m_features.get(XMPPNamespace::IqAuth)) {
		// Set remote party if not provided
		if (!TelEngine::null(username))
		    m_remote.set(username,m_local.domain(),resource);
		else
		    m_remote.resource(resource);
		if (m_remote.isFull()) {
		    Debug(this,DebugAll,"Remote party set to '%s' [%p]",m_remote.c_str(),this);
		    XmlElement* rsp = XMPPUtils::createIqResult(0,0,id,
			XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::IqAuth));
		    ok = sendStreamXml(Running,rsp);
		    if (!ok)
			m_remote.set(m_local.domain());
		}
		else
		    terminate(0,true,0,XMPPError::Internal);
	    }
	    else
		terminate(0,true,0,XMPPError::Internal);
	}
	else if (m_type == s2s)
	    ok = false;
	else if (m_type == comp) {
	    XmlElement* rsp = XMPPUtils::createElement(XmlTag::Handshake);
	    ok = sendStreamXml(Running,rsp);
	}
	if (ok) {
	    m_features.remove(XMPPNamespace::Sasl);
	    m_features.remove(XMPPNamespace::IqAuth);
	    setFlags(StreamAuthenticated);
	}
    }
    else {
	if (m_type == c2s) {
	    XmlElement* rsp = 0;
	    if (m_sasl)
		rsp = XMPPUtils::createFailure(XMPPNamespace::Sasl,error);
	    else {
		rsp = XMPPUtils::createIq(XMPPUtils::IqError,0,0,id);
		if (TelEngine::null(id))
		    rsp->addChild(XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::IqAuth));
		rsp->addChild(XMPPUtils::createError(XMPPError::TypeAuth,error));
	    }
	    ok = sendStreamXml(Features,rsp);
	}
	else if (m_type == s2s)
	    ok = false;
	else if (m_type == comp)
	    terminate(0,true,0,XMPPError::NotAuthorized);
    }
    TelEngine::destruct(m_sasl);
    return ok;
}

// Terminate the stream. Send stream end tag or error.
// Reset the stream. Deref stream if destroying.
void JBStream::terminate(int location, bool destroy, XmlElement* xml, int error,
    const char* reason, bool final, bool genEvent, const char* content)
{
    XDebug(this,DebugAll,"terminate(%d,%u,%p,%u,%s,%u) state=%s [%p]",
	location,destroy,xml,error,reason,final,stateName(),this);
    Lock lock(this);
    m_pending.clear();
    m_outXmlCompress.clear();
    resetPostponedTerminate();
    // Already in destroy
    if (state() == Destroy) {
	TelEngine::destruct(xml);
	return;
    }
    bool sendEndTag = true;
    destroy = destroy || final || flag(NoAutoRestart);
    // Set error flag
    if (state() == Running) {
	if (error != XMPPError::NoError)
	    setFlags(InError);
	else
	    resetFlags(InError);
    }
    else
	setFlags(InError);
    if (flag(InError)) {
	// Reset re-connect counter if not internal policy error
	if (location || error != XMPPError::Policy)
	    m_restart = 0;
    }
    if (error == XMPPError::NoError && m_engine->exiting())
	error = XMPPError::Shutdown;
    // Last check for sendEndTag
    if (sendEndTag) {
	// Prohibitted states or socket read/write error
	if (m_state == Destroy || m_state == Securing || m_state == Connecting)
	    sendEndTag = false;
	else if (error == XMPPError::SocketError) {
	    sendEndTag = false;
	    reason = "I/O error";
	}
    }
    Debug(this,DebugAll,
	"Terminate by '%s' state=%s destroy=%u error=%s reason='%s' final=%u [%p]",
	lookup(location,s_location),stateName(),destroy,
	XMPPUtils::s_error[error].c_str(),reason,final,this);
    if (sendEndTag) {
	XmlElement* start = 0;
	if (m_state == Starting && incoming())
	    start = buildStreamStart();
	XmlElement* end = new XmlElement(String("/stream:stream"),false);
	if (error != XMPPError::NoError && location < 1) {
	    XmlElement* e = XMPPUtils::createStreamError(error,reason,content);
	    if (!start)
		sendStreamXml(m_state,e,end);
	    else
		sendStreamXml(m_state,start,e,end);
	}
	else {
	    if (!start)
		sendStreamXml(m_state,end);
	    else
		sendStreamXml(m_state,start,end);
	}
    }
    resetConnection();
    m_outStreamXml.clear();
    m_outStreamXmlCompress.clear();

    // Always set termination event, except when called from destructor
    if (genEvent && !(final || m_terminateEvent)) {
	// TODO: Cancel all outgoing elements without id
	m_terminateEvent = new JBEvent(destroy ? JBEvent::Destroy : JBEvent::Terminated,
	    this,xml);
	xml = 0;
	if (!m_terminateEvent->m_text) {
	    if (TelEngine::null(reason))
		m_terminateEvent->m_text = XMPPUtils::s_error[error];
	    else
		m_terminateEvent->m_text = reason;
	}
    }
    TelEngine::destruct(xml);

    changeState(destroy ? Destroy : Idle);
}

// Close the stream. Release memory
void JBStream::destroyed()
{
    terminate(0,true,0,XMPPError::NoError,"",true);
    resetConnection();
    if (m_engine)
	m_engine->removeStream(this,false);
    TelEngine::destruct(m_terminateEvent);
    DDebug(this,DebugAll,"Destroyed local=%s remote=%s [%p]",
	m_local.safe(),m_remote.safe(),this);
    RefObject::destroyed();
}

// Check if stream state processor can continue
// This method is called from getEvent() with the stream locked
bool JBStream::canProcess(u_int64_t time)
{
    if (outgoing()) {
	// Increase stream restart counter if it's time to and should auto restart
	if (!flag(NoAutoRestart) && m_timeToFillRestart < time) {
	    m_timeToFillRestart = time + m_engine->m_restartUpdInterval;
	    if (m_restart < m_engine->m_restartMax) {
		m_restart++;
		DDebug(this,DebugAll,"Restart count set to %u max=%u [%p]",
		    m_restart,m_engine->m_restartMax,this);
	    }
	}
	if (state() == Idle) {
	    // Re-connect
	    bool conn = (m_connectStatus > JBConnect::Start);
	    if (!conn && m_restart) {
		// Don't connect non client/component or cluster if we are in error and
		//  have nothing to send
		if (m_type != c2s && m_type != comp && m_type != cluster &&
		    flag(InError) && !m_pending.skipNull())
		    return false;
		conn = true;
		m_restart--;
	    }
	    if (conn) {
		resetFlags(InError);
		changeState(Connecting);
		m_engine->connectStream(this);
		return false;
	    }
	    // Destroy if not auto-restarting
	    if (flag(NoAutoRestart)) {
		terminate(0,true,0);
		return false;
	    }
	}
    }
    else if (state() == Idle && flag(NoAutoRestart)) {
	terminate(0,true,0);
	return false;
    }
    return true;
}

// Process stream state. Get XML from parser's queue and process it
// This method is called from getEvent() with the stream locked
void JBStream::process(u_int64_t time)
{
    if (!m_xmlDom)
	return;
    XDebug(this,DebugAll,"JBStream::process() [%p]",this);
    while (true) {
	sendPending();
	if (m_terminateEvent)
	    break;

	// Lock the parser to obtain the root and/or child
	// Unlock it before processing received element
	Lock lockDoc(m_socketMutex);
	XmlDocument* doc = m_xmlDom ? m_xmlDom->document() : 0;
	XmlElement* root = doc ? doc->root(false) : 0;
	if (!root)
	    break;

	if (m_state == WaitStart) {
	    // Print the declaration
	    XmlDeclaration* dec = doc->declaration();
	    if (dec)
		m_engine->printXml(this,false,*dec);
	    XmlElement xml(*root);
	    lockDoc.drop();
	    // Print the root. Make sure we don't print its children
	    xml.clearChildren();
	    m_engine->printXml(this,false,xml);
	    // Check if valid
	    if (!XMPPUtils::isTag(xml,XmlTag::Stream,XMPPNamespace::Stream)) {
		String* ns = xml.xmlns();
		Debug(this,DebugMild,"Received invalid stream root '%s' namespace='%s' [%p]",
		    xml.tag(),TelEngine::c_safe(ns),this);
		terminate(0,true,0);
		break;
	    }
	    // Check 'from' and 'to'
	    JabberID from;
	    JabberID to;
	    if (!getJids(&xml,from,to))
		break;
	    DDebug(this,DebugAll,"Processing root '%s' in state %s [%p]",
		xml.tag(),stateName(),this);
	    processStart(&xml,from,to);
	    break;
	}

	XmlElement* xml = root->pop();
	if (!xml) {
	    if (root->completed())
		socketSetCanRead(false);
	    if (m_events.skipNull())
	        break;
	    if (!root->completed()) {
		if (m_ppTerminate && !(m_pending.skipNull() && socketCanWrite())) {
		    lockDoc.drop();
		    postponedTerminate();
		}
		break;
	    }
	    DDebug(this,DebugAll,"Remote closed the stream in state %s [%p]",
		stateName(),this);
	    lockDoc.drop();
	    resetPostponedTerminate();
	    terminate(1,false,0);
	    break;
	}
	lockDoc.drop();

	// Process received element
	// Print it
	m_engine->printXml(this,false,*xml);
	// Check stream termination
	if (streamError(xml))
	    break;
	// Check 'from' and 'to'
	JabberID from;
	JabberID to;
	if (!getJids(xml,from,to))
	    break;
	// Restart the idle timer
	setIdleTimer(time);
	// Check if a received stanza is valid and allowed in current state
	if (!checkStanzaRecv(xml,from,to))
	    break;

	DDebug(this,DebugAll,"Processing (%p,%s) in state %s [%p]",
	    xml,xml->tag(),stateName(),this);

	// Process here dialback verify
	if (m_type == s2s && isDbVerify(*xml)) {
	    switch (state()) {
		case Running:
		case Features:
		case Starting:
		case Challenge:
		case Auth:
		    m_events.append(new JBEvent(JBEvent::DbVerify,this,xml,from,to));
		    break;
		default:
		    dropXml(xml,"dialback verify in unsupported state");
	    }
	    continue;
	}

	switch (m_state) {
	    case Running:
		processRunning(xml,from,to);
		// Reset ping
		setNextPing(true);
		m_pingId = "";
		break;
	    case Features:
		if (m_incoming)
		    processFeaturesIn(xml,from,to);
		else
		    processFeaturesOut(xml,from,to);
		break;
	    case WaitStart:
	    case Starting:
		processStart(xml,from,to);
		TelEngine::destruct(xml);
		break;
	    case Challenge:
		processChallenge(xml,from,to);
		break;
	    case Auth:
		processAuth(xml,from,to);
		break;
	    case WaitTlsRsp:
		processWaitTlsRsp(xml,from,to);
		break;
	    case Register:
		processRegister(xml,from,to);
		break;
	    case Compressing:
		processCompressing(xml,from,to);
		break;
	    default:
		dropXml(xml,"unhandled stream state in process()");
	}
	break;
    }
    XDebug(this,DebugAll,"JBStream::process() exiting [%p]",this);
}

// Process elements in Running state
bool JBStream::processRunning(XmlElement* xml, const JabberID& from, const JabberID& to)
{
    if (!xml)
	return true;
    int t, ns;
    if (!XMPPUtils::getTag(*xml,t,ns))
	return dropXml(xml,"failed to retrieve element tag");
    switch (t) {
	case XmlTag::Message:
	    if (ns != m_xmlns)
		break;
	    m_events.append(new JBEvent(JBEvent::Message,this,xml,from,to));
	    return true;
	case XmlTag::Presence:
	    if (ns != m_xmlns)
		break;
	    m_events.append(new JBEvent(JBEvent::Presence,this,xml,from,to));
	    return true;
	case XmlTag::Iq:
	    if (ns != m_xmlns)
		break;
	    checkPing(this,xml,m_pingId);
	    m_events.append(new JBEvent(JBEvent::Iq,this,xml,from,to,xml->findFirstChild()));
	    return true;
	default:
	    m_events.append(new JBEvent(JBEvent::Unknown,this,xml,from,to));
	    return true;
    }
    // Invalid stanza namespace
    XmlElement* rsp = XMPPUtils::createError(xml,XMPPError::TypeModify,
	XMPPError::InvalidNamespace,"Only stanzas in default namespace are allowed");
    sendStanza(rsp);
    return true;
}

// Check stream timeouts
// This method is called from getEvent() with the stream locked, after
void JBStream::checkTimeouts(u_int64_t time)
{
    if (m_ppTerminateTimeout && m_ppTerminateTimeout <= time) {
	m_ppTerminateTimeout = 0;
	Debug(this,DebugAll,"Postponed termination timed out [%p]",this);
	if (postponedTerminate())
	    return;
    }
    // Running: check ping and idle timers
    if (m_state == Running) {
	const char* reason = 0;
	if (m_pingTimeout) {
	    if (m_pingTimeout < time) {
		Debug(this,DebugNote,"Ping stanza with id '%s' timed out [%p]",m_pingId.c_str(),this);
		reason = "Ping timeout";
	    }
	}
	else if (m_nextPing && time >= m_nextPing) {
	    XmlElement* ping = setNextPing(false);
	    if (ping) {
		DDebug(this,DebugAll,"Sending ping with id=%s [%p]",m_pingId.c_str(),this);
		if (!sendStanza(ping))
		    m_pingId = "";
	    }
	    else {
		resetPing();
		m_pingId = "";
	    }
	}
	if (m_idleTimeout && m_idleTimeout < time && !reason)
	    reason = "Stream idle";
	if (reason)
	    terminate(0,m_incoming,0,XMPPError::ConnTimeout,reason);
	return;
    }
    // Stream setup timer
    if (m_setupTimeout && m_setupTimeout < time) {
	terminate(0,m_incoming,0,XMPPError::Policy,"Stream setup timeout");
	return;
    }
    // Stream start timer
    if (m_startTimeout && m_startTimeout < time) {
	terminate(0,m_incoming,0,XMPPError::Policy,"Stream start timeout");
	return;
    }
    // Stream connect timer
    if (m_connectTimeout && m_connectTimeout < time) {
	DDebug(this,DebugNote,"Connect timed out stat=%s [%p]",
	    lookup(m_connectStatus,JBConnect::s_statusName),this);
	// Don't terminate if there are more connect options
	if (state() == Connecting && m_connectStatus > JBConnect::Start) {
	    m_engine->stopConnect(toString());
	    m_engine->connectStream(this);
	}
	else
	    terminate(0,m_incoming,0,XMPPError::ConnTimeout,"Stream connect timeout");
	return;
    }
}

// Reset the stream's connection. Build a new XML parser if the socket is valid
void JBStream::resetConnection(Socket* sock)
{
    DDebug(this,DebugAll,"JBStream::resetConnection(%p) current=%p [%p]",
	sock,m_socket,this);
    // Release the old one
    if (m_socket) {
	m_socketMutex.lock();
	m_socketFlags |= SocketWaitReset;
	m_socketMutex.unlock();
	// Wait for the socket to become available (not reading or writing)
	Socket* tmp = 0;
	while (true) {
	    Lock lock(m_socketMutex);
	    if (!(m_socket && (socketReading() || socketWriting()))) {
		tmp = m_socket;
		m_socket = 0;
		m_socketFlags = 0;
		if (m_xmlDom) {
		    delete m_xmlDom;
		    m_xmlDom = 0;
		}
		TelEngine::destruct(m_compress);
		break;
	    }
	    lock.drop();
	    Thread::yield(false);
	}
	if (tmp) {
	    tmp->setLinger(-1);
	    tmp->terminate();
	    delete tmp;
	}
    }
    resetPostponedTerminate();
    if (sock) {
	Lock lock(m_socketMutex);
	if (m_socket) {
	    Debug(this,DebugWarn,"Duplicate attempt to set socket! [%p]",this);
	    delete sock;
	    return;
	}
	m_xmlDom = new XmlDomParser(debugName());
	m_xmlDom->debugChain(this);
	m_socket = sock;
	if (debugAt(DebugAll)) {
	    SocketAddr l, r;
	    localAddr(l);
	    remoteAddr(r);
	    Debug(this,DebugAll,"Connection set local=%s:%d remote=%s:%d sock=%p [%p]",
		l.host().c_str(),l.port(),r.host().c_str(),r.port(),m_socket,this);
	}
	m_socket->setReuse(true);
	m_socket->setBlocking(false);
	socketSetCanRead(true);
	socketSetCanWrite(true);
    }
}

// Build a ping iq stanza
XmlElement* JBStream::buildPing(const String& stanzaId)
{
    return 0;
}

// Build a stream start XML element
XmlElement* JBStream::buildStreamStart()
{
    XmlElement* start = new XmlElement(XMPPUtils::s_tag[XmlTag::Stream],false);
    if (incoming())
	start->setAttribute("id",m_id);
    XMPPUtils::setStreamXmlns(*start);
    start->setAttribute(XmlElement::s_ns,XMPPUtils::s_ns[m_xmlns]);
    start->setAttributeValid("from",m_local.bare());
    start->setAttributeValid("to",m_remote.bare());
    if (outgoing() || flag(StreamRemoteVer1))
	start->setAttribute("version","1.0");
    start->setAttribute("xml:lang","en");
    return start;
}

// Process received elements in WaitStart state
// WaitStart: Incoming: waiting for stream start
//            Outgoing: idem (our stream start was already sent)
// Return false if stream termination was initiated
bool JBStream::processStart(const XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    Debug(this,DebugStub,"JBStream::processStart(%s) [%p]",xml->tag(),this);
    return true;
}

// Process elements in Compressing state
// Return false if stream termination was initiated
bool JBStream::processCompressing(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    XDebug(this,DebugAll,"JBStream::processCompressing() [%p]",this);
    int t = XmlTag::Count;
    int n = XMPPNamespace::Count;
    XMPPUtils::getTag(*xml,t,n);
    if (outgoing()) {
	if (n != XMPPNamespace::Compress)
	    return dropXml(xml,"expecting compression namespace");
	// Expecting: compressed/failure
	bool ok = (t == XmlTag::Compressed);
	if (!ok && t != XmlTag::Failure)
	    return dropXml(xml,"expecting compress response (compressed/failure)");
	if (ok) {
	    if (m_compress)
		setFlags(StreamCompressed);
	    else
		return destroyDropXml(xml,XMPPError::Internal,"no compressor");
	}
	else {
	    XmlElement* ch = xml->findFirstChild();
	    Debug(this,DebugInfo,"Compress failed at remote party error=%s [%p]",
		ch ? ch->tag() : "",this);
	    TelEngine::destruct(m_compress);
	}
	TelEngine::destruct(xml);
	// Restart the stream on success
	if (ok) {
	    XmlElement* s = buildStreamStart();
	    return sendStreamXml(WaitStart,s);
	}
	// Compress failed: continue
	JBServerStream* server = serverStream();
	if (server)
	    return server->sendDialback();
	JBClientStream* client = clientStream();
	if (client)
	    return client->bind();
	Debug(this,DebugNote,"Unhandled stream type in %s state [%p]",stateName(),this);
	terminate(0,true,0,XMPPError::Internal);
	return true;
    }
    // Authenticated incoming s2s waiting for compression or any other element
    if (type() == s2s && m_features.get(XMPPNamespace::CompressFeature)) {
	if (t == XmlTag::Compress && n == XMPPNamespace::Compress)
	    return handleCompressReq(xml);
	// Change state to Running
	changeState(Running);
	return processRunning(xml,from,to);
    }

    return dropXml(xml,"not implemented");
}

// Process elements in Register state
bool JBStream::processRegister(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    dropXml(xml,"can't process in this state");
    terminate(0,true,0,XMPPError::Internal);
    return false;
}

// Process elements in Auth state
bool JBStream::processAuth(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    return dropXml(xml,"can't process in this state");
}

// Check if a received start start element's namespaces are correct.
bool JBStream::processStreamStart(const XmlElement* xml)
{
    XDebug(this,DebugAll,"JBStream::processStreamStart() [%p]",this);
    if (m_state == Starting)
	return true;
    changeState(Starting);
    if (!XMPPUtils::hasDefaultXmlns(*xml,m_xmlns)) {
	Debug(this,DebugNote,"Received '%s' with invalid xmlns='%s' [%p]",
	    xml->tag(),TelEngine::c_safe(xml->xmlns()),this);
	terminate(0,m_incoming,0,XMPPError::InvalidNamespace);
	return false;
    }
    XMPPError::Type error = XMPPError::NoError;
    const char* reason = 0;
    while (true) {
	if (m_type != c2s && m_type != s2s && m_type != comp && m_type != cluster) {
	    Debug(this,DebugStub,"processStreamStart() type %u not handled!",m_type);
	    error = XMPPError::Internal;
	    break;
	}
	// Check xmlns:stream
	String* nsStr = xml->getAttribute("xmlns:stream");
	if (!nsStr || *nsStr != XMPPUtils::s_ns[XMPPNamespace::Stream]) {
	    Debug(this,DebugNote,"Received '%s' with invalid xmlns:stream='%s' [%p]",
		xml->tag(),TelEngine::c_safe(nsStr),this);
	    error = XMPPError::InvalidNamespace;
	    break;
	}
	// Check version
	String ver(xml->getAttribute("version"));
	int remoteVersion = -1;
	if (ver) {
	    int pos = ver.find('.');
	    if (pos > 0)
		remoteVersion = ver.substr(0,pos).toInteger(-1);
	}
	if (remoteVersion == 1)
	    setFlags(StreamRemoteVer1);
	else if (remoteVersion < 1) {
	    if (m_type == c2s)
		XDebug(this,DebugAll,"c2s stream start with version < 1 [%p]",this);
	    else if (m_type == s2s) {
		// Accept invalid/unsupported version only if TLS is not required
		if (!flag(TlsRequired)) {
		    // Check dialback
		    if (!xml->hasAttribute("xmlns:db",XMPPUtils::s_ns[XMPPNamespace::Dialback]))
			error = XMPPError::InvalidNamespace;
		}
		else
		    error = XMPPError::EncryptionRequired;
	    }
	    else if (m_type != comp)
		error = XMPPError::Internal;
	}
	else if (remoteVersion > 1)
	    error = XMPPError::UnsupportedVersion;
	if (error != XMPPError::NoError) {
	    Debug(this,DebugNote,"Unacceptable '%s' version='%s' error=%s [%p]",
		xml->tag(),ver.c_str(),XMPPUtils::s_error[error].c_str(),this);
	    break;
	}
	// Set stream id: generate one for incoming, get it from xml if outgoing
	if (incoming()) {
	    // Generate a random, variable length stream id
	    MD5 md5(String((int)(int64_t)this));
	    md5 << m_name << String((int)Time::msecNow());
	    m_id = md5.hexDigest();
	    m_id << "_" << String((int)Random::random());
	}
	else {
	    m_id = xml->getAttribute("id");
	    if (!m_id) {
		Debug(this,DebugNote,"Received '%s' with empty stream id [%p]",
		    xml->tag(),this);
		reason = "Missing stream id";
		error = XMPPError::InvalidId;
		break;
	    }
	}
	XDebug(this,DebugAll,"Stream id set to '%s' [%p]",m_id.c_str(),this);
	break;
    }
    if (error == XMPPError::NoError)
	return true;
    terminate(0,m_incoming,0,error,reason);
    return false;
}

// Handle an already checked (tag and namespace) compress request
// Respond to it. Change stream state on success
bool JBStream::handleCompressReq(XmlElement* xml)
{
    XMPPError::Type error = XMPPError::UnsupportedMethod;
    State newState = state();
    XmlElement* rsp = 0;
    XmlElement* m = XMPPUtils::findFirstChild(*xml,XmlTag::Method,
	XMPPNamespace::Compress);
    if (m) {
	// Get and check the method
	const String& method = m->getText();
	XMPPFeatureCompress* c = m_features.getCompress();
	if (method && c && c->hasMethod(method)) {
	    // Build the (de)compressor
	    Lock lock(m_socketMutex);
	    m_engine->compressStream(this,method);
	    if (m_compress) {
		newState = WaitStart;
		setFlags(SetCompressed);
		m_features.remove(XMPPNamespace::CompressFeature);
		rsp = XMPPUtils::createElement(XmlTag::Compressed,XMPPNamespace::Compress);
	    }
	    else
		error = XMPPError::SetupFailed;
	}
    }
    TelEngine::destruct(xml);
    if (!rsp)
	rsp = XMPPUtils::createFailure(XMPPNamespace::Compress,error);
    return sendStreamXml(newState,rsp);
}

// Check if a received element is a stream error one
bool JBStream::streamError(XmlElement* xml)
{
    if (!(xml && XMPPUtils::isTag(*xml,XmlTag::Error,XMPPNamespace::Stream)))
	return false;
    String text;
    String error;
    String content;
    XMPPUtils::decodeError(xml,XMPPNamespace::StreamError,&error,&text,&content);
    Debug(this,DebugAll,"Received stream error '%s' content='%s' text='%s' in state %s [%p]",
	error.c_str(),content.c_str(),text.c_str(),stateName(),this);
    int err = XMPPUtils::s_error[error];
    if (err >= XMPPError::Count)
	err = XMPPError::NoError;
    String rAddr;
    int rPort = 0;
    if (err == XMPPError::SeeOther && content) {
	if (m_redirectCount < m_redirectMax) {
	    int pos = content.rfind(':');
	    if (pos >= 0) {
		rAddr = content.substr(0,pos);
		if (rAddr) {
		    rPort = content.substr(pos + 1).toInteger(0);
		    if (rPort < 0)
			rPort = 0;
		}
	    }
	    else
		rAddr = content;
	    if (rAddr) {
		// Check if the connect destination is different
		SocketAddr remoteIp;
		remoteAddr(remoteIp);
		bool sameDest = (rAddr == serverHost()) || (rAddr == m_connectAddr) || (rAddr == remoteIp.host());
		if (sameDest) {
		    sameDest = ((rPort > 0 ? rPort : XMPP_C2S_PORT) == remoteIp.port());
		    if (sameDest) {
			Debug(this,DebugNote,"Ignoring redirect to same destination [%p]",this);
			rAddr = "";
		    }
		}
	    }
	}
    }
    terminate(1,false,xml,err,text,false,rAddr.null());
    setRedirect(rAddr,rPort);
    if (rAddr) {
	resetFlags(InError);
	resetConnectStatus();
	changeState(Connecting);
	m_engine->connectStream(this);
	setRedirect();
    }
    return true;
}

// Check if a received element has valid 'from' and 'to' jid attributes
bool JBStream::getJids(XmlElement* xml, JabberID& from, JabberID& to)
{
    if (!xml)
	return true;
    from = xml->getAttribute("from");
    to = xml->getAttribute("to");
    XDebug(this,DebugAll,"Got jids xml='%s' from='%s' to='%s' [%p]",
	xml->tag(),from.c_str(),to.c_str(),this);
    if (to.valid() && from.valid())
	return true;
    Debug(this,DebugNote,"Received '%s' with bad from='%s' or to='%s' [%p]",
	xml->tag(),from.c_str(),to.c_str(),this);
    terminate(0,m_incoming,xml,XMPPError::BadAddressing);
    return false;
}

// Check if a received element is a presence, message or iq qualified by the stream
//   namespace and the stream is not authenticated
// Validate 'from' for c2s streams
// Validate s2s 'to' domain and 'from' jid
bool JBStream::checkStanzaRecv(XmlElement* xml, JabberID& from, JabberID& to)
{
    if (!XMPPUtils::isStanza(*xml))
	return true;

    // RFC 3920bis 5.2: Accept stanzas only if the stream was authenticated
    // Accept IQs in jabber:iq:register namespace
    // Accept IQs in jabber:iq:auth namespace
    // They might be received on a non authenticated stream)
    if (!flag(StreamAuthenticated)) {
	bool isIq = XMPPUtils::isTag(*xml,XmlTag::Iq,m_xmlns);
	bool valid = isIq && XMPPUtils::findFirstChild(*xml,XmlTag::Count,
	    XMPPNamespace::IqRegister);
	JBClientStream* c2s = clientStream();
	if (!valid && c2s) {
	    // Outgoing client stream: check register responses
	    // Incoming client stream: check auth stanzas
	    if (outgoing())
		valid = c2s->isRegisterId(*xml);
	    else
		valid = isIq && XMPPUtils::findFirstChild(*xml,XmlTag::Count,
		    XMPPNamespace::IqAuth);
	}
	if (!valid) {
	    terminate(0,false,xml,XMPPError::NotAuthorized,
		"Can't accept stanza on non authorized stream");
	    return false;
	}
    }

    switch (m_type) {
	case c2s:
	    if (m_incoming) {
		// Check for valid from
		if (from && !m_remote.match(from)) {
		    XmlElement* e = XMPPUtils::createError(xml,
			XMPPError::TypeModify,XMPPError::BadAddressing);
		    sendStanza(e);
		    return false;
		}
		// Make sure the upper layer always has the full jid
		if (!from)
		    from = m_remote;
		else if (!from.resource())
		    from.resource(m_remote.resource());
	    }
	    else {
		XDebug(this,DebugStub,
		    "Possible checkStanzaRecv() unhandled outgoing c2s stream [%p]",this);
	    }
	    break;
	case comp:
	case s2s:
	    // RFC 3920bis 9.1.1.2 and 9.1.2.1:
	    // Validate 'to' and 'from'
	    // Accept anything for component streams
	    if (!(to && from)) {
		terminate(0,m_incoming,xml,XMPPError::BadAddressing);
		return false;
	    }
	    // TODO: Find an outgoing stream and send stanza error to the remote server
	    //  instead of terminating the stream
	    if (m_type == s2s) {
		if (incoming()) {
		    // Accept stanzas only for validated domains
		    if (!serverStream()->hasRemoteDomain(from.domain())) {
			terminate(0,m_incoming,xml,XMPPError::BadAddressing);
			return false;
		    }
		}
		else {
		    // We should not receive any stanza on outgoing s2s
		    terminate(0,m_incoming,xml,XMPPError::NotAuthorized);
		    return false;
		}
		if (m_local != to.domain()) {
		    terminate(0,m_incoming,xml,XMPPError::BadAddressing);
		    return false;
		}
	    }
	    else if (from.domain() != m_remote.domain()) {
		terminate(0,m_incoming,xml,XMPPError::InvalidFrom);
		return false;
	    }
	    break;
	case cluster:
	    break;
	default:
	    Debug(this,DebugStub,"checkStanzaRecv() unhandled stream type=%s [%p]",
		typeName(),this);
    }
    return true;
}

// Change stream state. Reset state depending data
void JBStream::changeState(State newState, u_int64_t time)
{
    if (newState == m_state)
	return;
    Debug(this,DebugAll,"Changing state from '%s' to '%s' [%p]",
	stateName(),lookup(newState,s_stateName),this);
    // Set/reset state depending data
    switch (m_state) {
	case Running:
	    resetPing();
	    m_pingId = "";
	    break;
	case WaitStart:
	    // Reset connect status if not timeout
	    if (m_startTimeout && m_startTimeout > time)
		resetConnectStatus();
	    m_startTimeout = 0;
	    break;
	case Securing:
	    setFlags(StreamSecured);
	    socketSetCanRead(true);
	    break;
	case Connecting:
	    m_connectTimeout = 0;
	    m_engine->stopConnect(toString());
	    break;
	case Register:
	    if (type() == c2s)
		clientStream()->m_registerReq = 0;
	    break;
	default: ;
    }
    switch (newState) {
	case WaitStart:
	    if (m_engine->m_setupTimeout && m_type != cluster)
		m_setupTimeout = time + timerMultiplier(this) * m_engine->m_setupTimeout;
	    else
		m_setupTimeout = 0;
	    m_startTimeout = time + timerMultiplier(this) * m_engine->m_startTimeout;
	    DDebug(this,DebugAll,"Set timeouts start=" FMT64 " setup=" FMT64 " [%p]",
		m_startTimeout,m_setupTimeout,this);
	    if (m_xmlDom) {
		Lock lck(m_socketMutex);
		if (m_xmlDom) {
		    m_xmlDom->reset();
		    DDebug(this,DebugAll,"XML parser reset [%p]",this);
		}
	    }
	    break;
	case Idle:
	    m_events.clear();
	case Destroy:
	    m_id = "";
	    m_setupTimeout = 0;
	    m_startTimeout = 0;
	    // Reset all internal flags
	    resetFlags(InternalFlags);
	    if (type() == c2s)
		clientStream()->m_registerReq = 0;
	    break;
	case Running:
	    resetConnectStatus();
	    setRedirect();
	    m_redirectCount = 0;
	    m_pingInterval = m_engine->m_pingInterval;
	    setNextPing(true);
	    setFlags(StreamSecured | StreamAuthenticated);
	    resetFlags(InError);
	    m_setupTimeout = 0;
	    m_startTimeout = 0;
	    if (m_state != Running)
		m_events.append(new JBEvent(JBEvent::Running,this,0));
	    break;
	case Securing:
	    socketSetCanRead(false);
	    break;
	default: ;
    }
    m_state = newState;
    if (m_state == Running)
	setIdleTimer(time);
}

// Check if the stream compress flag is set and compression was offered by remote party
XmlElement* JBStream::checkCompress()
{
    if (flag(StreamCompressed) || !flag(Compress))
	return 0;
    XMPPFeatureCompress* c = m_features.getCompress();
    if (!c)
	return 0;
    if (!(c && c->methods()))
	return 0;
    XmlElement* x = 0;
    Lock lock(m_socketMutex);
    m_engine->compressStream(this,c->methods());
    if (m_compress && m_compress->format()) {
	x = XMPPUtils::createElement(XmlTag::Compress,XMPPNamespace::Compress);
	x->addChild(XMPPUtils::createElement(XmlTag::Method,m_compress->format()));
    }
    else
	TelEngine::destruct(m_compress);
    return x;
}

// Check for pending events. Set the last event
void JBStream::checkPendingEvent()
{
    if (m_lastEvent)
	return;
    if (!m_terminateEvent) {
	GenObject* gen = m_events.remove(false);
	if (gen)
	    m_lastEvent = static_cast<JBEvent*>(gen);
	return;
    }
    // Check for register events and raise them before the terminate event
    for (ObjList* o = m_events.skipNull(); o; o = o->skipNext()) {
	JBEvent* ev = static_cast<JBEvent*>(o->get());
	if (ev->type() == JBEvent::RegisterOk || ev->type() == JBEvent::RegisterFailed) {
	    m_lastEvent = ev;
	    m_events.remove(ev,false);
	    return;
	}
    }
    m_lastEvent = m_terminateEvent;
    m_terminateEvent = 0;
}

// Send pending stream XML or stanzas
bool JBStream::sendPending(bool streamOnly)
{
    if (!m_socket)
	return false;
    XDebug(this,DebugAll,"JBStream::sendPending() [%p]",this);
    bool noComp = !flag(StreamCompressed);
    // Always try to send pending stream XML first
    if (m_outStreamXml) {
	const void* buf = 0;
	unsigned int len = 0;
	if (noComp) {
	    buf = m_outStreamXml.c_str();
	    len = m_outStreamXml.length();
	}
	else {
	    buf = m_outStreamXmlCompress.data();
	    len = m_outStreamXmlCompress.length();
	}
	if (!writeSocket(buf,len))
	    return false;
	bool all = false;
	if (noComp) {
	    all = (len == m_outStreamXml.length());
	    if (all)
		m_outStreamXml.clear();
	    else
		m_outStreamXml = m_outStreamXml.substr(len);
	}
	else {
	    all = (len == m_outStreamXmlCompress.length());
	    if (all) {
		m_outStreamXml.clear();
		m_outStreamXmlCompress.clear();
	    }
	    else
		m_outStreamXmlCompress.cut(-(int)len);
	}
	// Start TLS now for incoming streams
	if (m_incoming && m_state == Securing) {
	    if (all) {
		m_engine->encryptStream(this);
		setFlags(StreamTls);
		socketSetCanRead(true);
	    }
	    return true;
	}
	// Check set StreamCompressed flag if all data sent
	if (all && flag(SetCompressed))
	    setFlags(StreamCompressed);
	if (streamOnly || !all)
	    return true;
    }

    // Send pending stanzas
    if (m_state != Running || streamOnly)
	return true;
    ObjList* obj = m_pending.skipNull();
    if (!obj)
	return true;
    XmlElementOut* eout = static_cast<XmlElementOut*>(obj->get());
    XmlElement* xml = eout->element();
    if (!xml) {
	m_pending.remove(eout,true);
	return true;
    }
    bool sent = eout->sent();
    const void* buf = 0;
    unsigned int len = 0;
    if (noComp)
	buf = (const void*)eout->getData(len);
    else {
	if (!sent) {
	    // Make sure the buffer is prepared for sending
	    eout->getData(len);
	    m_outXmlCompress.clear();
	    if (!compress(eout))
		return false;
	}
	buf = m_outXmlCompress.data();
	len = m_outXmlCompress.length();
    }
    // Print the element only if it's the first time we try to send it
    if (!sent)
	m_engine->printXml(this,true,*xml);
    if (writeSocket(buf,len)) {
	if (!len)
	    return true;
	setIdleTimer();
	// Adjust element's buffer. Remove it from list on completion
	unsigned int rest = 0;
	if (noComp) {
	    eout->dataSent(len);
	    rest = eout->dataCount();
	}
	else {
	    m_outXmlCompress.cut(-(int)len);
	    rest = m_outXmlCompress.length();
	}
	if (!rest) {
	    DDebug(this,DebugAll,"Sent element (%p,%s) [%p]",xml,xml->tag(),this);
	    m_pending.remove(eout,true);
	}
	else
	    DDebug(this,DebugAll,"Partially sent element (%p,%s) sent=%u rest=%u [%p]",
		xml,xml->tag(),len,rest,this);
	return true;
    }
    // Error
    Debug(this,DebugNote,"Failed to send (%p,%s) [%p]",xml,xml->tag(),this);
    return false;
}

// Write data to socket
bool JBStream::writeSocket(const void* data, unsigned int& len)
{
    if (!(data && len)) {
	len = 0;
	return true;
    }
    Lock lock(m_socketMutex);
    if (!socketCanWrite()) {
	len = 0;
	if (0 != (m_socketFlags & SocketCanWrite)) {
	    socketSetCanWrite(false);
	    postponeTerminate(0,m_incoming,XMPPError::SocketError,"No socket");
	}
	return false;
    }
    socketSetWriting(true);
    lock.drop();
#ifdef JBSTREAM_DEBUG_SOCKET
    if (!flag(StreamCompressed))
	Debug(this,DebugInfo,"Sending %s [%p]",(const char*)data,this);
    else
	Debug(this,DebugInfo,"Sending %u compressed bytes [%p]",len,this);
#endif
    int w = m_socket->writeData(data,len);
    if (w != Socket::socketError())
	len = w;
    else
	len = 0;
#ifdef JBSTREAM_DEBUG_SOCKET
    if (!flag(StreamCompressed)) {
	String sent((const char*)data,len);
	Debug(this,DebugInfo,"Sent %s [%p]",sent.c_str(),this);
    }
    else
	Debug(this,DebugInfo,"Sent %u compressed bytes [%p]",len,this);
#endif
    Lock lck(m_socketMutex);
    // Check if the connection is waiting to be reset
    if (socketWaitReset()) {
	socketSetWriting(false);
	return true;
    }
    // Check if something changed
    if (!(m_socket && socketWriting())) {
	Debug(this,DebugAll,"Socket deleted while writing [%p]",this);
	return true;
    }
    socketSetWriting(false);
    if (w != Socket::socketError() || m_socket->canRetry())
	return true;
    socketSetCanWrite(false);
    String tmp;
    Thread::errorString(tmp,m_socket->error());
    String reason;
    reason << "Socket send error: " << tmp << " (" << m_socket->error() << ")";
    Debug(this,DebugWarn,"%s [%p]",reason.c_str(),this);
    lck.drop();
    postponeTerminate(0,m_incoming,XMPPError::SocketError,reason);
    return false;
}

// Update stream flags and remote connection data from engine
void JBStream::updateFromRemoteDef()
{
    m_engine->lock();
    JBRemoteDomainDef* domain = m_engine->remoteDomainDef(m_remote.domain());
    // Update flags
    setFlags(domain->m_flags & StreamFlags);
    // Update connection data
    if (outgoing() && state() == Idle) {
	m_connectAddr = domain->m_address;
	m_connectPort = domain->m_port;
    }
    m_engine->unlock();
}

// Retrieve the first required feature in the list
XMPPFeature* JBStream::firstRequiredFeature()
{
    for (ObjList* o = m_features.skipNull(); o; o = o->skipNext()) {
	XMPPFeature* f = static_cast<XMPPFeature*>(o->get());
	if (f->required())
	    return f;
    }
    return 0;
}

// Drop (delete) received XML element
bool JBStream::dropXml(XmlElement*& xml, const char* reason)
{
    if (!xml)
	return true;
    Debug(this,DebugNote,"Dropping xml=(%p,%s) ns=%s in state=%s reason='%s' [%p]",
	xml,xml->tag(),TelEngine::c_safe(xml->xmlns()),stateName(),reason,this);
    TelEngine::destruct(xml);
    return true;
}

// Set stream flag mask
void JBStream::setFlags(int mask)
{
#ifdef XDEBUG
    String f;
    XMPPUtils::buildFlags(f,mask,s_flagName);
    Debug(this,DebugAll,"Setting flags 0x%X (%s) current=0x%X [%p]",
	mask,f.c_str(),m_flags,this);
#endif
    m_flags |= mask;
#ifdef DEBUG
    if (0 != (mask & StreamCompressed))
	Debug(this,DebugAll,"Stream is using compression [%p]",this);
#endif
}

// Reset stream flag mask
void JBStream::resetFlags(int mask)
{
#ifdef XDEBUG
    String f;
    XMPPUtils::buildFlags(f,mask,s_flagName);
    Debug(this,DebugAll,"Resetting flags 0x%X (%s) current=0x%X [%p]",
	mask,f.c_str(),m_flags,this);
#endif
    m_flags &= ~mask;
}

// Set the idle timer in Running state
void JBStream::setIdleTimer(u_int64_t msecNow)
{
    // Set only for non c2s in Running state
    if (m_type == c2s || m_type == cluster || m_state != Running ||
	!m_engine->m_idleTimeout)
	return;
    m_idleTimeout = msecNow + m_engine->m_idleTimeout;
    XDebug(this,DebugAll,"Idle timeout set to " FMT64 "ms [%p]",m_idleTimeout,this);
}

// Reset ping data
void JBStream::resetPing()
{
    if (!(m_pingTimeout || m_nextPing))
	return;
    XDebug(this,DebugAll,"Reset ping data [%p]",this);
    m_nextPing = 0;
    m_pingTimeout = 0;
}

// Set the time of the next ping if there is any timeout and we don't have a ping in progress
// @return XmlElement containing the ping to send, 0 if no ping is going to be sent or 'force' is true
XmlElement* JBStream::setNextPing(bool force)
{
    if (!m_pingInterval) {
	resetPing();
	return 0;
    }
    if (m_type != c2s && m_type != comp)
	return 0;
    if (force) {
	m_nextPing = Time::msecNow() + m_pingInterval;
	m_pingTimeout = 0;
	XDebug(this,DebugAll,"Next ping " FMT64U " [%p]",m_nextPing,this);
	return 0;
    }
    XmlElement* ping = 0;
    if (m_nextPing) {
	// Ping still active in engine ?
	Time time;
	if (m_nextPing > time.msec())
	    return 0;
	if (m_engine->m_pingTimeout) {
	    generateIdIndex(m_pingId,"_ping_");
	    ping = buildPing(m_pingId);
	    if (ping)
		m_pingTimeout = time.msec() + m_engine->m_pingTimeout;
	    else
		m_pingTimeout = 0;
	}
	else
	    resetPing();
    }
    if (m_pingInterval)
	m_nextPing = Time::msecNow() + m_pingInterval;
    else
	m_nextPing = 0;
    XDebug(this,DebugAll,"Next ping " FMT64U " ping=%p [%p]",m_nextPing,ping,this);
    return ping;
}

// Process incoming elements in Challenge state
// Return false if stream termination was initiated
bool JBStream::processChallenge(XmlElement* xml, const JabberID& from, const JabberID& to)
{
    int t, n;
    if (!XMPPUtils::getTag(*xml,t,n))
	return dropXml(xml,"failed to retrieve element tag");
    if (n != XMPPNamespace::Sasl)
	return dropXml(xml,"expecting sasl namespace");
    if (t == XmlTag::Abort) {
	TelEngine::destruct(xml);
	TelEngine::destruct(m_sasl);
	XmlElement* rsp = XMPPUtils::createFailure(XMPPNamespace::Sasl,XMPPError::Aborted);
	sendStreamXml(Features,rsp);
	return true;
    }
    if (t != XmlTag::Response) {
	dropXml(xml,"expecting sasl response");
	return true;
    }
    XMPPError::Type error = XMPPError::NoError;
    // Use a while() to set error and break to the end
    while (true) {
	// Decode non empty auth data
	const String& text = xml->getText();
	if (text) {
	    String tmp;
	    if (!decodeBase64(tmp,text,this)) {
		error = XMPPError::IncorrectEnc;
		break;
	    }
	    if (m_sasl && !m_sasl->parseMD5ChallengeRsp(tmp)) {
		error = XMPPError::MalformedRequest;
		break;
	    }
	}
	else if (m_sasl)
	    TelEngine::destruct(m_sasl->m_params);
	break;
    }
    if (error == XMPPError::NoError) {
	changeState(Auth);
	m_events.append(new JBEvent(JBEvent::Auth,this,xml,from,to));
    }
    else {
	Debug(this,DebugNote,"Received bad challenge response error='%s' [%p]",
	    XMPPUtils::s_error[error].c_str(),this);
	XmlElement* failure = XMPPUtils::createFailure(XMPPNamespace::Sasl,error);
	sendStreamXml(Features,failure);
	TelEngine::destruct(xml);
    }
    return true;
}

// Process incoming 'auth' elements qualified by SASL namespace
// Return false if stream termination was initiated
bool JBStream::processSaslAuth(XmlElement* xml, const JabberID& from, const JabberID& to)
{
    if (!xml)
	return true;
    if (!XMPPUtils::isTag(*xml,XmlTag::Auth,XMPPNamespace::Sasl))
	return dropXml(xml,"expecting 'auth' in sasl namespace");
    XMPPFeatureSasl* sasl = m_features.getSasl();
    TelEngine::destruct(m_sasl);
    XMPPError::Type error = XMPPError::NoError;
    const char* mName = xml->attribute("mechanism");
    int mech = XMPPUtils::authMeth(mName);
    // Use a while() to set error and break to the end
    while (true) {
	if (!sasl->mechanism(mech)) {
	    error = XMPPError::InvalidMechanism;
	    break;
	}
	if (mech == XMPPUtils::AuthMD5) {
	    // Ignore auth text: we will challenge the client
	    m_sasl = new SASL(false,m_local.domain());
	    String buf;
	    if (m_sasl->buildMD5Challenge(buf)) {
		XDebug(this,DebugAll,"Sending challenge=%s [%p]",buf.c_str(),this);
		Base64 b((void*)buf.c_str(),buf.length());
		b.encode(buf);
		XmlElement* chg = XMPPUtils::createElement(XmlTag::Challenge,
		    XMPPNamespace::Sasl,buf);
		if (!sendStreamXml(Challenge,chg)) {
		    TelEngine::destruct(xml);
		    return false;
		}
	    }
	    else {
		TelEngine::destruct(m_sasl);
		error = XMPPError::TempAuthFailure;
		break;
	    }
	}
	else if (mech == XMPPUtils::AuthPlain) {
	    // Decode non empty auth data
	    DataBlock d;
	    const String& text = xml->getText();
	    if (text && text != "=" && !decodeBase64(d,text)) {
		error = XMPPError::IncorrectEnc;
		break;
	    }
	    m_sasl = new SASL(true);
	    if (!m_sasl->parsePlain(d)) {
		error = XMPPError::MalformedRequest;
		break;
	    }
	}
	else {
	    // This should never happen: we don't handle a mechanism sent
	    // to the remote party!
	    Debug(this,DebugWarn,"Unhandled advertised auth mechanism='%s' [%p]",
		mName,this);
	    error = XMPPError::TempAuthFailure;
	    break;
	}
	break;
    }
    if (error == XMPPError::NoError) {
	// Challenge state: we've challenged the remote party
	// Otherwise: request auth from upper layer
	if (state() == Challenge)
	    TelEngine::destruct(xml);
	else {
	    changeState(Auth);
	    m_events.append(new JBEvent(JBEvent::Auth,this,xml,from,to));
	}
    }
    else {
	Debug(this,DebugNote,"Received auth request mechanism='%s' error='%s' [%p]",
	    mName,XMPPUtils::s_error[error].c_str(),this);
	XmlElement* failure = XMPPUtils::createFailure(XMPPNamespace::Sasl,error);
	sendStreamXml(m_state,failure);
	TelEngine::destruct(xml);
    }
    return true;
}

// Process received elements in Features state (incoming stream)
// Return false if stream termination was initiated
bool JBStream::processFeaturesIn(XmlElement* xml, const JabberID& from, const JabberID& to)
{
    if (!xml)
	return true;
    const String* t = 0;
    const String* nsName = 0;
    if (!xml->getTag(t,nsName))
	return dropXml(xml,"invalid tag namespace prefix");
    int ns = nsName ? XMPPUtils::s_ns[*nsName] : XMPPNamespace::Count;

    // Component: Waiting for handshake in the stream namespace
    if (type() == comp) {
	if (outgoing())
	    return dropXml(xml,"invalid state for incoming stream");
	if (*t != XMPPUtils::s_tag[XmlTag::Handshake] || ns != m_xmlns)
	    return dropXml(xml,"expecting handshake in stream's namespace");
	JBEvent* ev = new JBEvent(JBEvent::Auth,this,xml,from,to);
	ev->m_text = xml->getText();
	m_events.append(ev);
	changeState(Auth);
	return true;
    }

    XMPPFeature* f = 0;
    // Stream compression feature and compression namespace are not the same!
    if (ns != XMPPNamespace::Compress)
	f = m_features.get(ns);
    else
	f = m_features.get(XMPPNamespace::CompressFeature);

    // Check if received unexpected feature
    if (!f) {
	// Check for some features that can be negotiated via 'iq' elements
	if (m_type == c2s && *t == XMPPUtils::s_tag[XmlTag::Iq] && ns == m_xmlns) {
	    int chTag = XmlTag::Count;
	    int chNs = XMPPNamespace::Count;
	    XmlElement* child = xml->findFirstChild();
	    if (child)
		XMPPUtils::getTag(*child,chTag,chNs);
	    // Bind
	    if (chNs == XMPPNamespace::Bind && m_features.get(XMPPNamespace::Bind)) {
		// We've sent bind feature
		// Don't accept it if not authenticated and TLS/SASL must be negotiated
		if (!flag(StreamAuthenticated)) {
		    XMPPFeature* tls = m_features.get(XMPPNamespace::Tls);
		    if (tls && tls->required()) {
			XmlElement* e = XMPPUtils::createError(xml,XMPPError::TypeWait,
			    XMPPError::EncryptionRequired);
			sendStreamXml(m_state,e);
			return true;
		    }
		    XMPPFeature* sasl = m_features.get(XMPPNamespace::Sasl);
		    XMPPFeature* iqAuth = m_features.get(XMPPNamespace::IqAuth);
		    if ((sasl && sasl->required()) || (iqAuth && iqAuth->required())) {
			XmlElement* e = XMPPUtils::createError(xml,XMPPError::TypeAuth,
			    XMPPError::NotAllowed);
			sendStreamXml(m_state,e);
			return true;
		    }
		}
		// Remove TLS/SASL features from list: they can't be negotiated anymore
		setFlags(StreamSecured | StreamAuthenticated);
		m_features.remove(XMPPNamespace::Tls);
		m_features.remove(XMPPNamespace::Sasl);
		m_features.remove(XMPPNamespace::IqAuth);
		changeState(Running);
		return processRunning(xml,from,to);
	    }
	    else if (chNs == XMPPNamespace::IqRegister) {
		// Register
		m_events.append(new JBEvent(JBEvent::Iq,this,xml,xml->findFirstChild()));
		return true;
	    }
	    else if (chNs == XMPPNamespace::IqAuth) {
		XMPPUtils::IqType type = XMPPUtils::iqType(xml->attribute("type"));
		bool req = type == XMPPUtils::IqGet || type == XMPPUtils::IqSet;
		// Stream non SASL auth
		// Check if we support it
		if (!m_features.get(XMPPNamespace::IqAuth)) {
		    if (req) {
			XmlElement* e = XMPPUtils::createError(xml,XMPPError::TypeCancel,
			    XMPPError::NotAllowed);
			return sendStreamXml(m_state,e);
		    }
		    return dropXml(xml,"unexpected jabber:iq:auth element");
		}
		if (flag(StreamRemoteVer1)) {
		    XMPPFeature* tls = m_features.get(XMPPNamespace::Tls);
		    if (tls && tls->required()) {
			XmlElement* e = XMPPUtils::createError(xml,XMPPError::TypeWait,
			    XMPPError::EncryptionRequired);
			sendStreamXml(m_state,e);
			return true;
		    }
		}
		if (chTag != XmlTag::Query) {
		    if (req) {
			XmlElement* e = XMPPUtils::createError(xml,XMPPError::TypeModify,
			    XMPPError::FeatureNotImpl);
			sendStreamXml(m_state,e);
			return true;
		    }
		    return dropXml(xml,"expecting iq auth with 'query' child");
		}
		// Send it to the uppper layer
		if (type == XMPPUtils::IqSet) {
		    m_events.append(new JBEvent(JBEvent::Auth,this,xml,xml->findFirstChild()));
		    changeState(Auth);
		}
		else
		    m_events.append(new JBEvent(JBEvent::Iq,this,xml,xml->findFirstChild()));
		return true;
	    }
	}
	// s2s waiting for dialback
	if (m_type == s2s) {
	    if (isDbResult(*xml))
		return serverStream()->processDbResult(xml,from,to);
	    // Drop the element if not authenticated
	    if (!flag(StreamAuthenticated))
		return dropXml(xml,"expecting dialback result");
	}
	// Check if all remaining features are optional
	XMPPFeature* req = firstRequiredFeature();
	if (req) {
	    Debug(this,DebugInfo,
		"Received '%s' while having '%s' required feature not negotiated [%p]",
		xml->tag(),req->c_str(),this);
	    // TODO: terminate the stream?
	    return dropXml(xml,"required feature negotiation not completed");
	}
	// No more required features: change state to Running
	// Remove TLS/SASL features from list: they can't be negotiated anymore
	setFlags(StreamSecured | StreamAuthenticated);
	m_features.remove(XMPPNamespace::Tls);
	m_features.remove(XMPPNamespace::Sasl);
	changeState(Running);
	return processRunning(xml,from,to);
    }
    // Stream enchryption
    if (ns == XMPPNamespace::Tls) {
	if (*t != XMPPUtils::s_tag[XmlTag::Starttls])
	    return dropXml(xml,"expecting tls 'starttls' element");
	if (!flag(StreamSecured)) {
	    // Change state before trying to send the element
	    // to signal to sendPending() to enchrypt the stream after sending it
	    changeState(Securing);
	    sendStreamXml(WaitStart,
		XMPPUtils::createElement(XmlTag::Proceed,XMPPNamespace::Tls));
	}
	else {
	    Debug(this,DebugNote,"Received '%s' element while already secured [%p]",
		xml->tag(),this);
	    // We shouldn't have Starttls in features list
	    // Something went wrong: terminate the stream
	    terminate(0,true,xml,XMPPError::Internal,"Stream already secured");
	    return false;
	}
	TelEngine::destruct(xml);
	return true;
    }
    // Stream SASL auth
    if (ns == XMPPNamespace::Sasl) {
	if (*t != XMPPUtils::s_tag[XmlTag::Auth])
	    return dropXml(xml,"expecting sasl 'auth' element");
	if (!flag(StreamAuthenticated)) {
	    // Check if we must negotiate TLS before authentication
	    XMPPFeature* tls = m_features.get(XMPPNamespace::Tls);
	    if (tls) {
		if (!flag(StreamSecured) && tls->required()) {
		    TelEngine::destruct(xml);
		    XmlElement* failure = XMPPUtils::createFailure(XMPPNamespace::Sasl,
			XMPPError::EncryptionRequired);
		    sendStreamXml(m_state,failure);
		    return true;
		}
		setSecured();
	    }
	}
	else {
	    // Remote party requested authentication while already done:
	    // Reset our flag and let it authenticate again
	    Debug(this,DebugNote,
		"Received auth request while already authenticated [%p]",
		this);
	    resetFlags(StreamAuthenticated);
	}
	return processSaslAuth(xml,from,to);
    }
    // Stream compression
    if (ns == XMPPNamespace::Compress) {
	if (*t != XMPPUtils::s_tag[XmlTag::Compress])
	    return dropXml(xml,"expecting stream compression 'compress' element");
	return handleCompressReq(xml);
    }
    return dropXml(xml,"unhandled stream feature");
}

// Process received elements in Features state (outgoing stream)
// Return false if stream termination was initiated
bool JBStream::processFeaturesOut(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    if (!xml)
	return true;
    if (!XMPPUtils::isTag(*xml,XmlTag::Features,XMPPNamespace::Stream))
	return dropXml(xml,"expecting stream features");
    m_features.fromStreamFeatures(*xml);
    // Check TLS
    if (!flag(StreamSecured)) {
	XMPPFeature* tls = m_features.get(XMPPNamespace::Tls);
	if (tls) {
	    if (m_engine->hasClientTls()) {
		TelEngine::destruct(xml);
		XmlElement* x = XMPPUtils::createElement(XmlTag::Starttls,
		    XMPPNamespace::Tls);
		return sendStreamXml(WaitTlsRsp,x);
	    }
	    if (tls->required() || flag(TlsRequired))
		return destroyDropXml(xml,XMPPError::Internal,
		    "required encryption not available");
	}
	else if (flag(TlsRequired))
	    return destroyDropXml(xml,XMPPError::EncryptionRequired,
		"required encryption not supported by remote");
	setFlags(StreamSecured);
    }
    // Check auth
    if (!flag(StreamAuthenticated)) {
	JBServerStream* server = serverStream();
	if (server) {
	    TelEngine::destruct(xml);
	    return server->sendDialback();
	}
	JBClientStream* client = clientStream();
	if (client) {
	    // Start auth or request registration data
	    TelEngine::destruct(xml);
	    if (!flag(RegisterUser))
		return client->startAuth();
	    return client->requestRegister(false);
	}
    }
    // Check compression
    XmlElement* x = checkCompress();
    if (x) {
	TelEngine::destruct(xml);
	return sendStreamXml(Compressing,x);
    }
    JBClientStream* client = clientStream();
    if (client) {
	TelEngine::destruct(xml);
	return client->bind();
    }
    JBServerStream* server = serverStream();
    JBClusterStream* cluster = clusterStream();
    if (server || cluster) {
	TelEngine::destruct(xml);
	changeState(Running);
	return true;
    }
    return dropXml(xml,"incomplete features process for outgoing stream");
}

// Process received elements in WaitTlsRsp state (outgoing stream)
// The element will be consumed
// Return false if stream termination was initiated
bool JBStream::processWaitTlsRsp(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    if (!xml)
	return true;
    int t,n;
    const char* reason = 0;
    if (XMPPUtils::getTag(*xml,t,n)) {
	if (n == XMPPNamespace::Tls) {
	    // Accept proceed and failure
	    if (t != XmlTag::Proceed && t != XmlTag::Failure)
		reason = "expecting tls 'proceed' or 'failure'";
	}
	else
	    reason = "expecting tls namespace";
    }
    else
	reason = "failed to retrieve element tag";
    if (reason) {
	// TODO: Unacceptable response to starttls request
	// Restart socket read or terminate the stream ?
	socketSetCanRead(true);
	return dropXml(xml,reason);
    }
    if (t == XmlTag::Proceed) {
	TelEngine::destruct(xml);
	changeState(Securing);
	m_engine->encryptStream(this);
	socketSetCanRead(true);
	setFlags(StreamTls);
	XmlElement* s = buildStreamStart();
	return sendStreamXml(WaitStart,s);
    }
    // TODO: Implement TLS usage reset if the stream is going to re-connect
    terminate(1,false,xml,XMPPError::NoError,"Server can't start TLS");
    return false;
}

// Set stream namespace from type
void JBStream::setXmlns()
{
    switch (m_type) {
	case c2s:
	    m_xmlns = XMPPNamespace::Client;
	    break;
	case s2s:
	    m_xmlns = XMPPNamespace::Server;
	    break;
	case comp:
	    m_xmlns = XMPPNamespace::ComponentAccept;
	    break;
	case cluster:
	    m_xmlns = XMPPNamespace::YateCluster;
	    break;
    }
}

// Event termination notification
void JBStream::eventTerminated(const JBEvent* ev)
{
    if (ev && ev == m_lastEvent) {
	m_lastEvent = 0;
	XDebug(this,DebugAll,"Event (%p,%s) terminated [%p]",ev,ev->name(),this);
    }
}

// Compress data to be sent (the pending stream xml buffer or pending stanza)
// Return false on failure
bool JBStream::compress(XmlElementOut* xml)
{
    DataBlock& buf = xml ? m_outXmlCompress : m_outStreamXmlCompress;
    const String& xmlBuf = xml ? xml->buffer() : m_outStreamXml;
    m_socketMutex.lock();
    int res = m_compress ? m_compress->compress(xmlBuf.c_str(),xmlBuf.length(),buf) : -1000;
    m_socketMutex.unlock();
    const char* s = xml ? "pending" : "stream";
    if (res >= 0) {
	if ((unsigned int)res == xmlBuf.length()) {
#ifdef JBSTREAM_DEBUG_COMPRESS
	    Debug(this,DebugInfo,"Compressed %s xml %u --> %u [%p]",
		s,xmlBuf.length(),buf.length(),this);
#endif
	    return true;
	}
	Debug(this,DebugNote,"Partially compressed %s xml %d/%u [%p]",
	    s,res,xmlBuf.length(),this);
    }
    else
	Debug(this,DebugNote,"Failed to compress %s xml: %d [%p]",s,res,this);
    return false;
}

// Reset connect status data
void JBStream::resetConnectStatus()
{
    DDebug(this,DebugAll,"resetConnectStatus() [%p]",this);
    m_connectStatus = JBConnect::Start;
    m_connectSrvs.clear();
}

// Postpone stream terminate until all parsed elements are processed
// Terminate now if allowed
void JBStream::postponeTerminate(int location, bool destroy, int error, const char* reason)
{
    lock();
    XDebug(this,DebugAll,"postponeTerminate(%d,%u,%s,%s) state=%s [%p]",
	location,destroy,XMPPUtils::s_error[error].c_str(),reason,stateName(),this);
    if (!m_ppTerminate) {
	int interval = 0;
	if (type() == c2s)
	    interval = m_engine->m_pptTimeoutC2s;
	else
	    interval = m_engine->m_pptTimeout;
	if (interval && haveData()) {
	    m_ppTerminate = new NamedList("");
	    m_ppTerminate->addParam("location",String(location));
	    m_ppTerminate->addParam("destroy",String::boolText(destroy));
	    m_ppTerminate->addParam("error",String(error));
	    m_ppTerminate->addParam("reason",reason);
	    m_ppTerminateTimeout = Time::msecNow() + interval;
	    Debug(this,DebugInfo,
		"Postponed termination location=%d destroy=%u error=%s reason=%s interval=%us [%p]",
		location,destroy,XMPPUtils::s_error[error].c_str(),reason,interval,this);
	}
    }
    bool postponed = m_ppTerminate != 0;
    unlock();
    if (!postponed)
	terminate(location,destroy,0,error,reason);
}

// Handle postponed termination. Return true if found
bool JBStream::postponedTerminate()
{
    if (!m_ppTerminate)
	return false;
    int location = m_ppTerminate->getIntValue("location");
    bool destroy = m_ppTerminate->getBoolValue("destroy");
    int error = m_ppTerminate->getIntValue("error");
    String reason = m_ppTerminate->getValue("reason");
    resetPostponedTerminate();
    DDebug(this,DebugAll,"postponedTerminate(%d,%u,%s,%s) state=%s [%p]",
	location,destroy,XMPPUtils::s_error[error].c_str(),reason.c_str(),
	stateName(),this);
    terminate(location,destroy,0,error,reason);
    return true;
}

// Reset redirect data
void JBStream::setRedirect(const String& addr, int port)
{
    if (!addr) {
	if (m_redirectAddr)
	    Debug(this,DebugInfo,"Cleared redirect data [%p]",this);
	m_redirectAddr = "";
	m_redirectPort = 0;
	return;
    }
    if (m_redirectCount >= m_redirectMax) {
	setRedirect();
	return;
    }
    resetConnectStatus();
    m_redirectAddr = addr;
    m_redirectPort = port;
    m_redirectCount++;
    Debug(this,DebugInfo,"Set redirect to '%s:%d' in state %s (counter=%u max=%u) [%p]",
	m_redirectAddr.c_str(),m_redirectPort,stateName(),m_redirectCount,m_redirectMax,this);
}


/*
 * JBClientStream
 */
JBClientStream::JBClientStream(JBEngine* engine, Socket* socket, bool ssl)
    : JBStream(engine,socket,c2s,ssl),
    m_userData(0), m_registerReq(0)
{
}

JBClientStream::JBClientStream(JBEngine* engine, const JabberID& jid, const String& account,
    const NamedList& params, const char* name, const char* serverHost)
    : JBStream(engine,c2s,jid,jid.domain(),TelEngine::null(name) ? account.c_str() : name,
	&params,serverHost),
    m_account(account), m_userData(0), m_registerReq(0)
{
    m_password = params.getValue("password");
}

// Build a ping iq stanza
XmlElement* JBClientStream::buildPing(const String& stanzaId)
{
    return XMPPUtils::createPing(stanzaId);
}

// Bind a resource to an incoming stream
void JBClientStream::bind(const String& resource, const char* id, XMPPError::Type error)
{
    DDebug(this,DebugAll,"bind(%s,'%s') [%p]",resource.c_str(),
	XMPPUtils::s_error[error].c_str(),this);
    Lock lock(this);
    if (!incoming() || m_remote.resource())
	return;
    XmlElement* xml = 0;
    if (resource) {
	m_remote.resource(resource);
	xml = XMPPUtils::createIq(XMPPUtils::IqResult,0,0,id);
	XmlElement* bind = XMPPUtils::createElement(XmlTag::Bind,
	    XMPPNamespace::Bind);
	bind->addChild(XMPPUtils::createElement(XmlTag::Jid,m_remote));
	xml->addChild(bind);
    }
    else {
	if (error == XMPPError::NoError)
	    error = XMPPError::NotAllowed;
	xml = XMPPUtils::createError(XMPPError::TypeModify,error);
    }
    // Remove non-negotiable bind feature on success
    if (sendStanza(xml) && resource)
	m_features.remove(XMPPNamespace::Bind);
}

// Request account setup (or info) on outgoing stream
bool JBClientStream::requestRegister(bool data, bool set, const String& newPass)
{
    if (incoming())
	return true;

    Lock lock(this);
    DDebug(this,DebugAll,"requestRegister(%u,%u) [%p]",data,set,this);
    XmlElement* req = 0;
    if (data) {
	// Register new user, change the account or remove it
	if (set) {
	    // TODO: Allow user account register/change through unsecured streams ?
	    String* pass = 0;
	    if (!flag(StreamAuthenticated))
		pass = &m_password;
	    else if (newPass) {
		m_newPassword = newPass;
		pass = &m_newPassword;
	    }
	    if (!pass)
		return false;
	    m_registerReq = '2';
	    req = XMPPUtils::createRegisterQuery(0,0,String(m_registerReq),
		m_local.node(),*pass);
	}
	else if (flag(StreamAuthenticated)) {
	    m_registerReq = '3';
	    req = XMPPUtils::createRegisterQuery(XMPPUtils::IqSet,0,0,
		String(m_registerReq),XMPPUtils::createElement(XmlTag::Remove));
	}
	else
	    return false;
    }
    else {
	// Request register info
	m_registerReq = '1';
	req = XMPPUtils::createRegisterQuery(XMPPUtils::IqGet,0,0,String(m_registerReq));
    }
    if (!flag(StreamAuthenticated) || state() != Running)
	return sendStreamXml(Register,req);
    return sendStanza(req);
}

// Process elements in Running state
bool JBClientStream::processRunning(XmlElement* xml, const JabberID& from, const JabberID& to)
{
    if (!xml)
	return true;
    // Check if a resource was bound to an incoming stream
    // Accept only 'iq' with bind namespace only if we've sent 'bind' feature
    if (incoming()) {
	if (!m_remote.resource()) {
	    if (XMPPUtils::isTag(*xml,XmlTag::Iq,m_xmlns)) {
		XmlElement* child = XMPPUtils::findFirstChild(*xml,XmlTag::Bind,XMPPNamespace::Bind);
		if (child && m_features.get(XMPPNamespace::Bind)) {
		    m_events.append(new JBEvent(JBEvent::Bind,this,xml,from,to,child));
		    return true;
		}
	    }
	    XmlElement* e = XMPPUtils::createError(xml,XMPPError::TypeCancel,
		XMPPError::NotAllowed,"No resource bound to the stream");
	    sendStanza(e);
	    return true;
	}
    }
    else if (m_registerReq && XMPPUtils::isTag(*xml,XmlTag::Iq,m_xmlns) &&
	isRegisterId(*xml) && XMPPUtils::isResponse(*xml))
	return processRegister(xml,from,to);
    return JBStream::processRunning(xml,from,to);
}

// Process received elements in WaitStart state
// WaitStart: Incoming: waiting for stream start
//            Outgoing: idem (our stream start was already sent)
// Return false if stream termination was initiated
bool JBClientStream::processStart(const XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    XDebug(this,DebugAll,"JBClientStream::processStart(%s) [%p]",xml->tag(),this);

    // Check element
    if (!processStreamStart(xml))
	return false;

    // RFC3920 5.3.1:
    // The 'from' attribute must be set for response stream start
    if (outgoing()) {
	if (from.null()) {
	    Debug(this,DebugNote,"Received '%s' with empty 'from' [%p]",xml->tag(),this);
	    terminate(0,false,0,XMPPError::BadAddressing,"Missing 'from' attribute");
	    return false;
	}
    }
    else {
	if (!flag(StreamAuthenticated)) {
	    m_remote.set(from);
	    m_local.set(to);
        }
    }
    m_remote.resource("");
    // RFC3920 5.3.1: The 'to' attribute must always be set
    // RFC3920: The 'to' attribute is optional
    bool validTo = !to.null();
    if (validTo) {
	if (outgoing())
	    validTo = (m_local.bare() == to);
	else
	    validTo = engine()->hasDomain(to.domain());
    }
#ifdef RFC3920
    else
	validTo = outgoing();
#endif
    if (!validTo) {
	Debug(this,DebugNote,"Received '%s' with invalid to='%s' [%p]",
	    xml->tag(),to.c_str(),this);
	terminate(0,false,0,
	    to.null() ? XMPPError::BadAddressing : XMPPError::HostUnknown,
	    "Invalid 'to' attribute");
	return false;
    }
    if (incoming() || flag(StreamRemoteVer1)) {
	m_events.append(new JBEvent(JBEvent::Start,this,0,from,to));
	return true;
    }
    Debug(this,DebugNote,"Outgoing client stream: unsupported remote version (expecting 1.x)");
    terminate(0,true,0,XMPPError::Internal,"Unsupported version");
    return false;
}

// Process elements in Auth state
bool JBClientStream::processAuth(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    if (!xml)
	return true;
    if (incoming())
	return destroyDropXml(xml,XMPPError::Internal,"invalid state for incoming stream");
    int t,n;
    if (!XMPPUtils::getTag(*xml,t,n))
	return destroyDropXml(xml,XMPPError::Internal,"failed to retrieve element tag");

    // Authenticating
    if (!flag(StreamAuthenticated)) {
	// TODO: The server might challenge us again
	//       Implement support for multiple challenge/response steps
	if (n != XMPPNamespace::Sasl)
	    return destroyDropXml(xml,XMPPError::InvalidNamespace,
		"element with non SASL namespace");
	if (!m_sasl)
	    return destroyDropXml(xml,XMPPError::Internal,"no SASL data");
	if (t == XmlTag::Failure) {
	    terminate(0,true,xml);
	    return false;
	}
	if (!m_sasl->m_plain) {
	    // Digest MD5
	    if (flag(StreamWaitChallenge)) {
		if (t != XmlTag::Challenge)
		    return destroyDropXml(xml,XMPPError::BadRequest,"expecting challenge");
		String tmp;
		if (!decodeBase64(tmp,xml->getText(),this))
		    return destroyDropXml(xml,XMPPError::IncorrectEnc,
			"challenge with incorrect encoding");
		if (!m_sasl->parseMD5Challenge(tmp))
		    return destroyDropXml(xml,XMPPError::MalformedRequest,
			"invalid challenge format");
		TelEngine::destruct(xml);
		m_sasl->setAuthParams(m_local.node(),m_password);
		tmp.clear();
		m_sasl->buildAuthRsp(tmp,"xmpp/" + m_local.domain());
		resetFlags(StreamWaitChallenge);
		setFlags(StreamWaitChgRsp);
		XmlElement* rsp = XMPPUtils::createElement(XmlTag::Response,XMPPNamespace::Sasl,tmp);
		return sendStreamXml(state(),rsp);
	    }
	    // Digest MD5 response reply
	    if (flag(StreamWaitChgRsp)) {
#ifdef RFC3920
		// Expect success or challenge
		// challenge is accepted if not already received one
		if (t != XmlTag::Success && (t != XmlTag::Challenge || flag(StreamRfc3920Chg)))
#else
		// Expect success
		if (t != XmlTag::Success)
#endif
		    return dropXml(xml,"unexpected element");
		if (!flag(StreamRfc3920Chg)) {
		    String rspAuth;
		    if (!decodeBase64(rspAuth,xml->getText(),this))
			return destroyDropXml(xml,XMPPError::IncorrectEnc,
			    "challenge response reply with incorrect encoding");
		    if (!rspAuth.startSkip("rspauth=",false))
			return destroyDropXml(xml,XMPPError::BadFormat,
			    "invalid challenge response reply");
		    if (!m_sasl->validAuthReply(rspAuth))
			return destroyDropXml(xml,XMPPError::InvalidAuth,
			    "incorrect challenge response reply auth");
		}
#ifdef RFC3920
		// Send empty response to challenge
		if (t == XmlTag::Challenge) {
		    setFlags(StreamRfc3920Chg);
		    TelEngine::destruct(xml);
		    XmlElement* rsp = XMPPUtils::createElement(XmlTag::Response,
			XMPPNamespace::Sasl);
		    return sendStreamXml(state(),rsp);
		}
#endif
		resetFlags(StreamWaitChgRsp | StreamRfc3920Chg);
	    }
	    else
		return dropXml(xml,"unhandled sasl digest md5 state");
	}
	else {
	    // Plain
	    if (t != XmlTag::Success)
		return dropXml(xml,"unexpected element");
	}
	// Authenticated. Bind a resource
	Debug(this,DebugAll,"Authenticated [%p]",this);
	TelEngine::destruct(xml);
	TelEngine::destruct(m_sasl);
	setFlags(StreamAuthenticated);
	XmlElement* start = buildStreamStart();
	return sendStreamXml(WaitStart,start);
    }

    XMPPUtils::IqType iq = XMPPUtils::iqType(xml->attribute("type"));
    String* id = xml->getAttribute("id");

    // Waiting for bind response
    if (flag(StreamWaitBindRsp)) {
	// Expecting 'iq' result or error
	if (t != XmlTag::Iq ||
	    (iq != XMPPUtils::IqResult && iq != XMPPUtils::IqError) ||
	    !id || *id != "bind_1")
	    return dropXml(xml,"unexpected element");
	if (iq == XMPPUtils::IqError) {
	    Debug(this,DebugNote,"Resource binding failed [%p]",this);
	    terminate(0,true,xml);
	    return false;
	}
	// Check it
	bool ok = false;
	while (true) {
	    XmlElement* bind = XMPPUtils::findFirstChild(*xml,XmlTag::Bind,XMPPNamespace::Bind);
	    if (!bind)
		break;
	    XmlElement* tmp = bind->findFirstChild(&XMPPUtils::s_tag[XmlTag::Jid]);
	    if (!tmp)
		break;
	    JabberID jid(tmp->getText());
	    if (jid.bare() != m_local.bare())
		break;
	    ok = true;
	    if (m_local.resource() != jid.resource()) {
		m_local.resource(jid.resource());
		Debug(this,DebugAll,"Resource set to '%s' [%p]",
		    local().resource().c_str(),this);
	    }
	    break;
	}
	if (!ok)
	    return destroyDropXml(xml,XMPPError::UndefinedCondition,
		"unacceptable bind response");
	resetFlags(StreamWaitBindRsp);
	TelEngine::destruct(xml);
	if (!m_features.get(XMPPNamespace::Session)) {
	    changeState(Running);
	    return true;
	}
	// Send session
	XmlElement* sess = XMPPUtils::createIq(XMPPUtils::IqSet,0,0,"sess_1");
	sess->addChild(XMPPUtils::createElement(XmlTag::Session,XMPPNamespace::Session));
	setFlags(StreamWaitSessRsp);
	return sendStreamXml(state(),sess);
    }

    // Waiting for session response
    if (flag(StreamWaitSessRsp)) {
	// Expecting 'iq' result or error
	if (t != XmlTag::Iq ||
	    (iq != XMPPUtils::IqResult && iq != XMPPUtils::IqError) ||
	    !id || *id != "sess_1")
	    return dropXml(xml,"unexpected element");
	if (iq == XMPPUtils::IqError) {
	    Debug(this,DebugNote,"Session failed [%p]",this);
	    terminate(0,true,xml);
	    return false;
	}
	TelEngine::destruct(xml);
	resetFlags(StreamWaitSessRsp);
	changeState(Running);
	return true;
    }

    return dropXml(xml,"unhandled");
}

// Process elements in Register state
bool JBClientStream::processRegister(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    if (!xml)
	return true;
    int t, ns;
    if (!XMPPUtils::getTag(*xml,t,ns))
	return dropXml(xml,"failed to retrieve element tag");
    if (t != XmlTag::Iq)
	return dropXml(xml,"expecting 'iq'");
    XMPPUtils::IqType iq = XMPPUtils::iqType(xml->attribute("type"));
    if (iq != XMPPUtils::IqResult && iq != XMPPUtils::IqError)
	return dropXml(xml,"expecting 'iq' response");
    if (!isRegisterId(*xml))
	return dropXml(xml,"unexpected response id");
    if (iq == XMPPUtils::IqError) {
	m_events.append(new JBEvent(JBEvent::RegisterFailed,this,xml,from,to));
	// Don't terminate if the user requested account change after authentication
	if (!flag(StreamAuthenticated))
	    terminate(0,true,0,XMPPError::NoError);
	return flag(StreamAuthenticated);
    }
    // Requested registration data
    if (m_registerReq == '1') {
	// XEP-0077: check for username and password children or
	//  instructions
	XmlElement* query = XMPPUtils::findFirstChild(*xml,XmlTag::Query,
	    XMPPNamespace::IqRegister);
	if (query && XMPPUtils::findFirstChild(*query,XmlTag::Username) &&
	    XMPPUtils::findFirstChild(*query,XmlTag::Password)) {
	    TelEngine::destruct(xml);
	    return requestRegister(true);
	}
	m_events.append(new JBEvent(JBEvent::RegisterFailed,this,xml,from,to));
	// Don't terminate if the user requested account change after authentication
	if (!flag(StreamAuthenticated))
	    terminate(0,true,0,XMPPError::NoError);
	return flag(StreamAuthenticated);
    }
    // Requested registration/change
    if (m_registerReq == '2') {
	m_events.append(new JBEvent(JBEvent::RegisterOk,this,xml,from,to));
	// Reset register user flag
	resetFlags(RegisterUser);
	// Done if account changed after authentication
	if (flag(StreamAuthenticated)) {
	    m_password = m_newPassword;
	    return true;
	}
	// Start auth
	changeState(Features);
	return startAuth();
    }
    // Requested account removal
    if (m_registerReq == '3') {
	terminate(0,true,xml,XMPPError::Reg,"Account removed");
	return false;
    }
    return destroyDropXml(xml,XMPPError::Internal,"unhandled state");
}

// Release memory
void JBClientStream::destroyed()
{
    userData(0);
    JBStream::destroyed();
}

// Start outgoing stream authentication
bool JBClientStream::startAuth()
{
    if (incoming() || state() != Features)
	return false;

    TelEngine::destruct(m_sasl);

    XMPPFeatureSasl* sasl = m_features.getSasl();
    if (!sasl) {
	terminate(0,true,0,XMPPError::NoError,"Missing authentication data");
	return false;
    }

    // RFC 3920 SASL auth
    int mech = XMPPUtils::AuthNone;
    if (sasl->mechanism(XMPPUtils::AuthMD5))
	mech = XMPPUtils::AuthMD5;
    else if (sasl->mechanism(XMPPUtils::AuthPlain) && flag(AllowPlainAuth))
	mech = XMPPUtils::AuthPlain;
    else {
	terminate(0,true,0,XMPPError::NoError,"Unsupported authentication mechanism");
	return false;
    }

    m_sasl = new SASL(mech == XMPPUtils::AuthPlain);
    String rsp;
    if (m_sasl->m_plain) {
	m_sasl->setAuthParams(m_local.node(),m_password);
	if (!m_sasl->buildAuthRsp(rsp)) {
	    terminate(0,true,0,XMPPError::NoError,"Invalid auth data length for plain auth");
	    return false;
	}
    }
    else
	setFlags(StreamWaitChallenge);
    // MD5: send auth element, wait challenge
    // Plain auth: send auth element with credentials and wait response (success/failure)
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Auth,XMPPNamespace::Sasl,rsp);
    xml->setAttribute("mechanism",lookup(mech,XMPPUtils::s_authMeth));
    return sendStreamXml(Auth,xml);
}

// Start resource binding on outgoing stream
bool JBClientStream::bind()
{
    Debug(this,DebugAll,"Binding resource [%p]",this);
    XmlElement* bind = XMPPUtils::createElement(XmlTag::Bind,XMPPNamespace::Bind);
    if (m_local.resource())
	bind->addChild(XMPPUtils::createElement(XmlTag::Resource,m_local.resource()));
    XmlElement* b = XMPPUtils::createIq(XMPPUtils::IqSet,0,0,"bind_1");
    b->addChild(bind);
    setFlags(StreamWaitBindRsp);
    return sendStreamXml(Auth,b);
}


/*
 * JBServerStream
 */
// Build an incoming stream from a socket
JBServerStream::JBServerStream(JBEngine* engine, Socket* socket, bool component)
    : JBStream(engine,socket,component ? comp : s2s),
    m_remoteDomains(""), m_dbKey(0)
{
}

// Build an outgoing stream
JBServerStream::JBServerStream(JBEngine* engine, const JabberID& local,
    const JabberID& remote, const char* dbId, const char* dbKey, bool dbOnly,
    const NamedList* params)
    : JBStream(engine,s2s,local,remote,0,params),
    m_remoteDomains(""), m_dbKey(0)
{
    if (!(TelEngine::null(dbId) || TelEngine::null(dbKey)))
	m_dbKey = new NamedString(dbId,dbKey);
    if (dbOnly)
	setFlags(DialbackOnly | NoAutoRestart);
}

// Constructor. Build an outgoing component stream
JBServerStream::JBServerStream(JBEngine* engine, const JabberID& local, const JabberID& remote,
    const String* name, const NamedList* params)
    : JBStream(engine,comp,local,remote,name ? name->c_str() : 0,params),
    m_remoteDomains(""), m_dbKey(0)
{
    if (params)
	m_password = params->getValue("password");
}

// Send a dialback verify response
bool JBServerStream::sendDbVerify(const char* from, const char* to, const char* id,
    XMPPError::Type rsp)
{
    adjustDbRsp(rsp);
    XmlElement* result = XMPPUtils::createDialbackVerifyRsp(from,to,id,rsp);
    DDebug(this,DebugAll,"Sending '%s' db:verify response from %s to %s [%p]",
	result->attribute("type"),from,to,this);
    return state() < Running ? sendStreamXml(state(),result) : sendStanza(result);
}

// Send a dialback key response. Update the remote domains list
// Terminate the stream if there are no more remote domains
bool JBServerStream::sendDbResult(const JabberID& from, const JabberID& to, XMPPError::Type rsp)
{
    Lock lock(this);
    // Check local domain
    if (m_local != from)
	return false;
    // Respond only to received requests
    NamedString* p = m_remoteDomains.getParam(to);
    if (!p)
	return false;
    bool valid = rsp == XMPPError::NoError;
    // Don't deny already authenticated requests
    if (p->null() && !valid)
	return false;
    // Set request state or remove it if not accepted
    if (valid)
	p->clear();
    else
	m_remoteDomains.clearParam(to);
    bool ok = false;
    adjustDbRsp(rsp);
    XmlElement* result = XMPPUtils::createDialbackResult(from,to,rsp);
    DDebug(this,DebugAll,"Sending '%s' db:result response from %s to %s [%p]",
	result->attribute("type"),from.c_str(),to.c_str(),this);
    if (m_state < Running) {
	ok = sendStreamXml(Running,result);
	// Remove features and set the authenticated flag
	if (ok && valid) {
	    m_features.remove(XMPPNamespace::Sasl);
	    m_features.remove(XMPPNamespace::IqAuth);
	    setFlags(StreamAuthenticated);
	    // Compression can still be set
	    if (!flag(StreamCompressed) && m_features.get(XMPPNamespace::CompressFeature))
		setFlags(StreamCanCompress);
	    else
		resetFlags(StreamCanCompress);
	}
    }
    else if (m_state == Running)
	ok = sendStanza(result);
    else
	TelEngine::destruct(result);
    // Terminate the stream if there are no more remote domains
    if (!m_remoteDomains.count())
	terminate(-1,true,0,rsp);
    return ok;
}

// Send dialback data (key/verify)
bool JBServerStream::sendDialback()
{
    State newState = Running;
    XmlElement* result = 0;
    if (!flag(DialbackOnly)) {
	if (flag(StreamAuthenticated))
	    newState = Running;
	else {
	    String key;
	    engine()->buildDialbackKey(id(),m_local,m_remote,key);
	    result = XMPPUtils::createDialbackKey(m_local,m_remote,key);
	    newState = Auth;
	}
    }
    else if (!m_dbKey) {
	// Dialback only with no key?
	Debug(this,DebugNote,"Outgoing dialback stream with no key! [%p]",this);
	terminate(0,true,0,XMPPError::Internal);
	return false;
    }
    if (m_dbKey) {
	XmlElement* db = XMPPUtils::createDialbackVerify(m_local,m_remote,
	    m_dbKey->name(),*m_dbKey);
	if (result)
	    return sendStreamXml(newState,result,db);
	return sendStreamXml(newState,db);
    }
    if (result)
	return sendStreamXml(newState,result);
    changeState(newState);
    return true;
}

// Release memory
void JBServerStream::destroyed()
{
    TelEngine::destruct(m_dbKey);
    JBStream::destroyed();
}

// Process elements in Running state
bool JBServerStream::processRunning(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    if (!xml)
	return true;
    // Incoming, authenticated stream which might still request compression
    // Any other element will reset compression offer
    if (flag(StreamCanCompress)) {
	if (incoming() && !flag(StreamCompressed) &&
	    m_features.get(XMPPNamespace::CompressFeature)) {
	    int t = XmlTag::Count;
	    int n = XMPPNamespace::Count;
	    XMPPUtils::getTag(*xml,t,n);
	    if (t == XmlTag::Compress && n == XMPPNamespace::Compress)
		return handleCompressReq(xml);
	}
	resetFlags(StreamCanCompress);
	m_features.remove(XMPPNamespace::CompressFeature);
    }
    // Check the tags of known dialback elements:
    //  there are servers who don't stamp them with the namespace
    // Let other elements stamped with dialback namespace go the upper layer
    if (type() != comp && isDbResult(*xml)) {
	if (outgoing())
	    return dropXml(xml,"dialback result on outgoing stream");
	return processDbResult(xml,from,to);
    }
    // Call default handler
    return JBStream::processRunning(xml,from,to);
}

// Build a stream start XML element
XmlElement* JBServerStream::buildStreamStart()
{
    XmlElement* start = new XmlElement(XMPPUtils::s_tag[XmlTag::Stream],false);
    if (incoming())
	start->setAttribute("id",m_id);
    XMPPUtils::setStreamXmlns(*start);
    start->setAttribute(XmlElement::s_ns,XMPPUtils::s_ns[m_xmlns]);
    if (type() == s2s) {
	start->setAttribute(XmlElement::s_nsPrefix + "db",XMPPUtils::s_ns[XMPPNamespace::Dialback]);
	if (!dialback()) {
	    start->setAttributeValid("from",m_local.bare());
	    start->setAttributeValid("to",m_remote.bare());
	    if (outgoing() || flag(StreamLocalVer1))
		start->setAttribute("version","1.0");
	    start->setAttribute("xml:lang","en");
	}
    }
    else if (type() == comp) {
	if (incoming())
	    start->setAttributeValid("from",m_remote.domain());
	else
	    start->setAttributeValid("to",m_local.domain());
    }
    return start;
}

// Process received elements in WaitStart state
// WaitStart: Incoming: waiting for stream start
//            Outgoing: idem (our stream start was already sent)
// Return false if stream termination was initiated
bool JBServerStream::processStart(const XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    XDebug(this,DebugAll,"JBServerStream::processStart() [%p]",this);

    if (!processStreamStart(xml))
	return false;

    if (type() == comp) {
	String from = xml->attribute("from");
	if (m_local == from) {
	    changeState(Starting);
	    m_events.append(new JBEvent(JBEvent::Start,this,0,to,JabberID::empty()));
	}
	else
	    terminate(0,false,0,XMPPError::InvalidFrom);
	return false;
    }

    if (outgoing()) {
	m_events.append(new JBEvent(JBEvent::Start,this,0,from,to));
	return true;
    }

    // Incoming stream
    m_local = to;
    if (m_local && !engine()->hasDomain(m_local)) {
	terminate(0,true,0,XMPPError::HostUnknown);
	return false;
    }
    updateFromRemoteDef();
    m_events.append(new JBEvent(JBEvent::Start,this,0,from,to));
    return true;
}

// Process elements in Auth state
bool JBServerStream::processAuth(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    if (incoming())
	return dropXml(xml,"invalid state for incoming stream");
    // Component
    if (type() == comp) {
	int t,n;
	if (!XMPPUtils::getTag(*xml,t,n))
	    return destroyDropXml(xml,XMPPError::Internal,"failed to retrieve element tag");
	if (t != XmlTag::Handshake || n != m_xmlns)
	    return dropXml(xml,"expecting handshake in stream's namespace");
	// Stream authenticated
	TelEngine::destruct(xml);
	setFlags(StreamAuthenticated);
	changeState(Running);
	Debug(this,DebugAll,"Authenticated [%p]",this);
	return true;
    }
    // Waiting for db:result
    if (!isDbResult(*xml))
	return dropXml(xml,"expecting dialback result");
    // Result
    // Outgoing stream waiting for dialback key response
    if (outgoing()) {
	if (m_remote != from || m_local != to)
	    return destroyDropXml(xml,XMPPError::BadAddressing,
		"dialback response with invalid 'from'");
	// Expect dialback key response
	int rsp = XMPPUtils::decodeDbRsp(xml);
	if (rsp != XMPPError::NoError) {
	    terminate(1,false,xml,rsp);
	    return false;
	}
	// Stream authenticated
	TelEngine::destruct(xml);
	setFlags(StreamAuthenticated);
	// Check compression
	XmlElement* x = checkCompress();
	if (x)
	    return sendStreamXml(Compressing,x);
	changeState(Running);
	return true;
    }
    return dropXml(xml,"incomplete state process");
}

// Start the stream (reply to received stream start)
bool JBServerStream::startComp(const String& local, const String& remote)
{
    if (state() != Starting || type() != comp)
	return false;
    Lock lock(this);
    XmlElement* s = 0;
    if (incoming()) {
	m_local.set(local);
	m_remote.set(remote);
	s = buildStreamStart();
    }
    else {
	String digest;
	buildSha1Digest(digest,m_password);
	s = XMPPUtils::createElement(XmlTag::Handshake,digest);
    }
    setSecured();
    return sendStreamXml(incoming() ? Features : Auth,s);
}

// Process dialback key (db:result) requests
bool JBServerStream::processDbResult(XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    // Check TLS when stream:features were sent
    if (m_state == Features) {
	if (flag(TlsRequired) && !flag(StreamSecured))
	    return destroyDropXml(xml,XMPPError::EncryptionRequired,
		"required encryption not supported by remote");
	// TLS can't be negotiated anymore
	setFlags(StreamSecured);
    }
    // Check remote domain
    if (!from)
	return destroyDropXml(xml,XMPPError::BadAddressing,
	    "dialback result with empty 'from' domain");
    // Accept non empty key only
    const char* key = xml->getText();
    if (TelEngine::null(key))
	return destroyDropXml(xml,XMPPError::NotAcceptable,
	    "dialback result with empty key");
    // Check local domain
    if (!(to && engine()->hasDomain(to))) {
	const char* reason = "dialback result with unknown 'to' domain";
	dropXml(xml,reason);
	XmlElement* rsp = XMPPUtils::createDialbackResult(to,from,XMPPError::ItemNotFound);
	if (m_state < Running)
	    sendStreamXml(state(),rsp);
	else
	    sendStanza(rsp);
	return false;
    }
    if (!m_local)
	m_local = to;
    else if (m_local != to)
	return destroyDropXml(xml,XMPPError::NotAcceptable,
	    "dialback result with incorrect 'to' domain");
    // Ignore duplicate requests
    if (m_remoteDomains.getParam(from)) {
	dropXml(xml,"duplicate dialback key request");
	return false;
    }
    m_remoteDomains.addParam(from,key);
    DDebug(this,DebugAll,"Added db:result request from %s [%p]",from.c_str(),this);
    // Notify the upper layer of incoming request
    JBEvent* ev = new JBEvent(JBEvent::DbResult,this,xml,from,to);
    ev->m_text = key;
    m_events.append(ev);
    return true;
}


/*
 * JBClusterStream
 */
// Build an incoming stream from a socket
JBClusterStream::JBClusterStream(JBEngine* engine, Socket* socket)
    : JBStream(engine,socket,cluster)
{
}

// Build an outgoing stream
JBClusterStream::JBClusterStream(JBEngine* engine, const JabberID& local,
    const JabberID& remote, const NamedList* params)
    : JBStream(engine,cluster,local,remote,0,params)
{
}

// Build a stream start XML element
XmlElement* JBClusterStream::buildStreamStart()
{
    XmlElement* start = new XmlElement(XMPPUtils::s_tag[XmlTag::Stream],false);
    if (incoming())
	start->setAttribute("id",m_id);
    XMPPUtils::setStreamXmlns(*start);
    start->setAttribute(XmlElement::s_ns,XMPPUtils::s_ns[m_xmlns]);
    start->setAttributeValid("from",m_local);
    start->setAttributeValid("to",m_remote);
    start->setAttribute("version","1.0");
    start->setAttribute("xml:lang","en");
    return start;
}

// Process received elements in WaitStart state
// WaitStart: Incoming: waiting for stream start
//            Outgoing: idem (our stream start was already sent)
// Return false if stream termination was initiated
bool JBClusterStream::processStart(const XmlElement* xml, const JabberID& from,
    const JabberID& to)
{
    XDebug(this,DebugAll,"JBClusterStream::processStart() [%p]",this);
    if (!processStreamStart(xml))
	return false;
    // Check from/to
    bool ok = true;
    if (outgoing())
	ok = (m_local == to) && (m_remote == from);
    else {
	if (!m_remote) {
	    m_local = to;
	    m_remote = from;
	    ok = from && to;
	}
	else
	    ok = (m_local == to) && (m_remote == from);
    }
    if (!ok) {
	Debug(this,DebugNote,"Got invalid from='%s' or to='%s' in stream start [%p]",
	    from.c_str(),to.c_str(),this);
	terminate(0,true,0,XMPPError::BadAddressing);
	return false;
    }
    m_events.append(new JBEvent(JBEvent::Start,this,0,m_remote,m_local));
    return true;
}

// Process elements in Running state
bool JBClusterStream::processRunning(XmlElement* xml, const JabberID& from, const JabberID& to)
{
    if (!xml)
	return true;
    int t, ns;
    if (!XMPPUtils::getTag(*xml,t,ns))
	return dropXml(xml,"failed to retrieve element tag");
    JBEvent::Type evType = JBEvent::Unknown;
    XmlElement* child = 0;
    switch (t) {
	case XmlTag::Iq:
	    checkPing(this,xml,m_pingId);
	    evType = JBEvent::Iq;
	    child = xml->findFirstChild();
	    break;
	case XmlTag::Message:
	    evType = JBEvent::Message;
	    break;
	case XmlTag::Presence:
	    evType = JBEvent::Presence;
	    break;
	default: ;
    }
    m_events.append(new JBEvent(evType,this,xml,m_remote,m_local,child));
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
