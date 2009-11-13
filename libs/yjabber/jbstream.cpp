/**
 * jbstream.cpp
 * Yet Another Jabber Component Protocol Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <yatejabber.h>
#include <stdlib.h>

using namespace TelEngine;

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
    {"error",            InError},
    // Internal flags
    {"roster_requested", RosterRequested},
    {"online",           AvailableResource},
    {"secured",          StreamTls | StreamSecured},
    {"authenticated",    StreamAuthenticated},
    {"waitbindrsp",      StreamWaitBindRsp},
    {"waitsessrsp",      StreamWaitSessRsp},
    {"waitchallenge",    StreamWaitChallenge},
    {"waitchallengersp", StreamWaitChgRsp},
    {0,0}
};

const TokenDict JBStream::s_typeName[] = {
    {"c2s",      c2s},
    {"s2s",      s2s},
    {"comp",     comp},
    {0,0}
};

// Retrieve the multiplier for non client stream timers
static inline unsigned int timerMultiplier(JBStream* stream)
{
    return stream->type() == JBStream::c2s ? 1 : 1;
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
    m_pingTimeout(0), m_nextPing(0),
    m_idleTimeout(0), m_connectTimeout(0),
    m_restart(0), m_timeToFillRestart(0),
    m_engine(engine), m_type(t),
    m_incoming(true), m_terminateEvent(0),
    m_xmlDom(0), m_socket(0), m_socketFlags(0), m_connectPort(0)
{
    if (ssl)
	m_flags |= (StreamSecured | StreamTls);
    m_engine->buildStreamName(m_name);
    debugName(m_name);
    debugChain(m_engine);
    Debug(this,DebugAll,"JBStream::JBStream(%p,%p,%s,%s) incoming [%p]",
	engine,socket,typeName(),String::boolText(ssl),this);
    setXmlns();
    // Don't restart incoming streams
    m_flags |= NoAutoRestart;
    resetConnection(socket);
    changeState(WaitStart);
}

// Outgoing
JBStream::JBStream(JBEngine* engine, Type t, const JabberID& local, const JabberID& remote,
    const char* name, const NamedList* params)
    : Mutex(true,"JBStream"),
    m_sasl(0),
    m_state(Idle), m_local(local), m_remote(remote),
    m_flags(0), m_xmlns(XMPPNamespace::Count), m_lastEvent(0),
    m_setupTimeout(0), m_startTimeout(0),
    m_pingTimeout(0), m_nextPing(0),
    m_idleTimeout(0), m_connectTimeout(0),
    m_restart(1), m_timeToFillRestart(0),
    m_engine(engine), m_type(t),
    m_incoming(false), m_name(name),
    m_terminateEvent(0),
    m_xmlDom(0), m_socket(0), m_socketFlags(0), m_connectPort(0)
{
    if (!m_name)
	m_engine->buildStreamName(m_name);
    debugName(m_name);
    debugChain(m_engine);
    if (params) {
	int flgs = XMPPUtils::decodeFlags(params->getValue("options"),s_flagName);
	m_flags = flgs & StreamFlags;
	m_connectAddr = params->getValue("server",params->getValue("address"));
	m_connectPort = params->getIntValue("port");
    }
    else
	updateFromRemoteDef();
    Debug(this,DebugAll,"JBStream::JBStream(%p,%s,%s,%s) outgoing [%p]",
	engine,typeName(),local.c_str(),remote.c_str(),this);
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
	    start();
	}
	else {
	    DDebug(this,DebugNote,"Connect failed [%p]",this);
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

// Get an object from this stream
void* JBStream::getObject(const String& name) const
{
    if (name == "Socket*")
	return state() == Securing ? (void*)&m_socket : 0;
    if (name == "JBStream")
	return (void*)this;
    return RefObject::getObject(name);
}

// Get the string representation of this stream
const String& JBStream::toString() const
{
    return m_name;
}

// Set/reset RosterRequested flag
void JBStream::setRosterRequested(bool ok)
{
    Lock lock(this);
    if (ok == flag(RosterRequested))
	return;
    if (ok)
	m_flags |= RosterRequested;
    else
	m_flags &= ~RosterRequested;
    XDebug(this,DebugAll,"%s roster requested flag [%p]",ok ? "Set" : "Reset",this);
}

// Set/reset AvailableResource/PositivePriority flags
bool JBStream::setAvailableResource(bool ok, bool positive)
{
    Lock lock(this);
    if (ok && positive)
	m_flags |= PositivePriority;
    else
	m_flags &= ~PositivePriority;
    if (ok == flag(AvailableResource))
	return false;
    if (ok)
	m_flags |= AvailableResource;
    else
	m_flags &= ~AvailableResource;
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
    Lock lock(this);
    if (!socketCanRead() || state() == Destroy || state() == Idle || state() == Connecting)
	return false;
    socketSetReading(true);
    if (state() != WaitTlsRsp)
	len--;
    else
	len = 1;
    lock.drop();
    XMPPError::Type error = XMPPError::NoError;
    int read = m_socket->readData(buf,len);
    Lock lck(this);
    // Check if something changed
    if (!(m_socket && socketReading())) {
	Debug(this,DebugAll,"Socket deleted while reading [%p]",this);
	return false;
    }
    if (read && read != Socket::socketError()) {
	buf[read] = 0;
	XDebug(this,DebugInfo,"Received %s [%p]",buf,this);
	if (!m_xmlDom->parse(buf)) {
	    if (m_xmlDom->error() != XmlSaxParser::Incomplete)
		error = XMPPError::Xml;
	    else if (m_xmlDom->buffer().length() > m_engine->m_maxIncompleteXml)
		error = XMPPError::Policy;
	}
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
    const char* reason = 0;
    if (error != XMPPError::SocketError) {
	String tmp;
	if (error == XMPPError::Xml) {
	    reason = m_xmlDom->getError();
	    tmp = m_xmlDom->buffer();
	}
	else {
	    tmp << "overflow len=" << m_xmlDom->buffer().length() << " max=" <<
		m_engine->m_maxIncompleteXml;
	    reason = "XML element too long";
	}
	Debug(this,DebugNote,"Parser error='%s' buffer='%s' [%p]",
	    reason,tmp.c_str(),this);
    }
    else if (read) {
	String tmp;
	Thread::errorString(tmp,m_socket->error());
	Debug(this,DebugWarn,"Socket read error: %d: '%s' [%p]",m_socket->error(),
	    tmp.c_str(),this);
    }
    else {
	Debug(this,DebugInfo,"Stream EOF [%p]",this);
	location = 1;
    }
    lck.drop();
    terminate(location,m_incoming,0,error,reason);
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

// Send a stanza ('iq', 'message' or 'presence') in Running state.
bool JBStream::sendStanza(XmlElement*& xml)
{
    if (!xml)
	return false;
    DDebug(this,DebugAll,"sendStanza(%p) '%s' [%p]",xml,xml->tag(),this);
    if (!XMPPUtils::isStanza(*xml)) {
	Debug(this,DebugNote,"Request to send non stanza xml='%s' [%p]",xml->tag(),this);
	TelEngine::destruct(xml);
	return false;
    }
    Lock lock(this);
    m_pending.append(new XmlElementOut(xml));
    xml = 0;
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
    // Use a while() to break to the end: safe cleanup
    while (true) {
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
	m_engine->printXml(this,true,frag);
	ok = sendPending(true);
	break;
    }
    TelEngine::destruct(first);
    TelEngine::destruct(second);
    TelEngine::destruct(third);
    if (ok)
	changeState(newState);
    return ok;
}

// Start the stream. This method should be called by the upper layer
//  when processing an incoming stream Start event. For outgoing streams
//  this method is called internally on succesfully connect.
void JBStream::start(XMPPFeatureList* features, XmlElement* caps)
{
    Lock lock(this);
    if (m_state != Starting)
	return;
    if (outgoing()) {
	TelEngine::destruct(caps);
	XmlElement* s = buildStreamStart();
	sendStreamXml(WaitStart,s);
	return;
    }
    m_features.clear();
    if (features)
	m_features.add(*features);
    if (flag(StreamRemoteVer1)) {
	// Set secured flag if we don't advertise TLS
	if (!(flag(StreamSecured) || m_features.get(XMPPNamespace::Tls)))
	    setSecured();
	// Set authenticated flag if we don't advertise authentication mechanisms
	if (flag(StreamSecured)) {
	    if (flag(StreamAuthenticated))
	        m_features.remove(XMPPNamespace::Sasl);
	    else if (!m_features.get(XMPPNamespace::Sasl))
		m_flags |= JBStream::StreamAuthenticated;
	}
    }
    else if (m_type == c2s) {
	// c2s using non-sasl auth
	setSecured();
    }
    // Send start and features
    XmlElement* s = buildStreamStart();
    XmlElement* f = 0;
    if (flag(StreamRemoteVer1))
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
	else if (m_type == s2s) {
	    XmlElement* rsp = XMPPUtils::createDialbackResult(m_local,m_remote,true);
	    ok = sendStreamXml(Running,rsp);
	}
	else if (m_type == comp) {
	    XmlElement* rsp = XMPPUtils::createElement(XmlTag::Handshake);
	    ok = sendStreamXml(Running,rsp);
	}
	if (ok) {
	    m_features.remove(XMPPNamespace::Sasl);
	    m_features.remove(XMPPNamespace::IqAuth);
	    m_flags |= StreamAuthenticated;
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
	else if (m_type == s2s) {
	    XmlElement* rsp = XMPPUtils::createDialbackResult(m_local,m_remote,false);
	    ok = sendStreamXml(state(),rsp);
	    if (ok)
		terminate(0,true,0,XMPPError::NotAuthorized);
	}
	else if (m_type == comp)
	    terminate(0,true,0,XMPPError::NotAuthorized);
    }
    TelEngine::destruct(m_sasl);
    return ok;
}

// Terminate the stream. Send stream end tag or error.
// Reset the stream. Deref stream if destroying.
void JBStream::terminate(int location, bool destroy, XmlElement* xml, int error,
    const char* reason, bool final)
{
    XDebug(this,DebugAll,"terminate(%d,%u,%p,%u,%s,%u) state=%s [%p]",
	location,destroy,xml,error,reason,final,stateName(),this);
    Lock lock(this);
    m_pending.clear();
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
	    m_flags |= InError;
	else
	    m_flags &= ~InError;
    }
    else
	m_flags |= InError;
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
	    XmlElement* e = XMPPUtils::createStreamError(error,reason);
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

    // Always set termination event, except when called from destructor
    if (!(final || m_terminateEvent)) {
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
		Debug(this,DebugAll,"Restart count set to %u max=%u [%p]",
		    m_restart,m_engine->m_restartMax,this);
	    }
	}
	if (state() == Idle) {
	    // Re-connect
	    // Don't connect if we are in error and have nothing to send
	    if (m_restart) {
		if (flag(InError) && !m_pending.skipNull())
		    return false;
		m_flags &= ~InError;
		changeState(Connecting);
		m_restart--;
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
    XmlDocument* doc = m_xmlDom->document();
    if (!doc) {
	Debug(this,DebugGoOn,"The parser is not a document! [%p]",this);
	terminate(0,true,0,XMPPError::Internal);
	return;
    }
    XmlElement* root = doc->root(false);
    while (true) {
	sendPending();
	if (m_terminateEvent || !root)
	    break;
	// Check for stream termination
	if (root->completed()) {
	    bool error = false;
	    // Check if received an error
	    XmlElement* xml = 0;
	    while (0 != (xml = root->pop())) {
		if (streamError(xml)) {
		    error = true;
		    break;
		}
		TelEngine::destruct(xml);
	    }
	    if (error)
		break;
	    Debug(this,DebugAll,"Remote closed the stream in state %s [%p]",
		stateName(),this);
	    terminate(1,false,0);
	    break;
	}

	if (m_state == WaitStart) {
	    // Print the declaration
	    XmlDeclaration* dec = doc->declaration();
	    if (dec)
		m_engine->printXml(this,false,*dec);
	    // Print the root. Make sure we don't print its children
	    if (!root->getChildren().skipNull())
		m_engine->printXml(this,false,*root);
	    else {
		XmlElement tmp(*root);
		tmp.clearChildren();
		m_engine->printXml(this,false,tmp);
	    }
	    // Check if valid
	    if (!XMPPUtils::isTag(*root,XmlTag::Stream,XMPPNamespace::Stream)) {
		String* ns = root->xmlns();
		Debug(this,DebugMild,"Received invalid stream root '%s' namespace='%s' [%p]",
		    root->tag(),TelEngine::c_safe(ns),this);
		terminate(0,true,0);
		break;
	    }
	    // Check 'from' and 'to'
	    JabberID from;
	    JabberID to;
	    if (!getJids(root,from,to))
		break;
	    Debug(this,DebugAll,"Processing (%p,%s) in state %s [%p]",
		root,root->tag(),stateName(),this);
	    processStart(root,from,to);
	    break;
	}

	XmlElement* xml = root->pop();
	if (!xml)
	    break;

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

	XDebug(m_engine,DebugAll,"Processing (%p,%s) in state %s [%p]",
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
    // Running: check ping and idle timers
    if (m_state == Running) {
	if (m_pingTimeout) {
	    if (m_pingTimeout < time)
		terminate(0,false,0,XMPPError::ConnTimeout,"Ping timeout");
	}
	else if (m_nextPing && time >= m_nextPing) {
	    m_pingId = (unsigned int)time;
	    // TODO: Send it
	    Debug(this,DebugStub,"JBStream::checkTimeouts() sendPing() not implemented");
	}
	else if (m_idleTimeout && m_idleTimeout < time)
	    terminate(0,true,0,XMPPError::ConnTimeout,"Stream idle");
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
	// Wait for the socket to become available (not reading or writing)
	Socket* tmp = 0;
	while (true) {
	    Lock lock(this);
	    if (!(m_socket && (socketReading() || socketWriting()))) {
		tmp = m_socket;
		m_socket = 0;
		m_socketFlags = 0;
		if (m_xmlDom) {
		    delete m_xmlDom;
		    m_xmlDom = 0;
		}
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
    if (sock) {
	Lock lock(this);
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
	    Debug(this,DebugAll,"Connection set local=%s:%d remote=%s:%d [%p]",
		l.host().c_str(),l.port(),r.host().c_str(),r.port(),this);
	}
	m_socket->setReuse(true);
	m_socket->setBlocking(false);
	socketSetCanRead(true);
    }
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
	if (m_type != c2s && m_type != s2s && m_type != comp) {
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
	    m_flags |= StreamRemoteVer1;
	else if (remoteVersion < 1) {
	    if (m_type == c2s)
		XDebug(this,DebugAll,"c2s stream start with version < 1 [%p]",this);
	    else if (m_type == s2s) {
		// Accept invalid/unsupported version only if TLS is not required
		if (!flag(TlsRequired)) {
		    // Check dialback
		    if (!xml->hasAttribute("xmlns:db",XMPPUtils::s_ns[XMPPNamespace::Dialback])) {
			Debug(this,DebugNote,"Received non dialback '%s' [%p]",
			    xml->tag(),this);
			error = XMPPError::InvalidNamespace;
		    }
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
	    Debug(this,DebugNote,"Received '%s' with unacceptable version='%s' [%p]",
		xml->tag(),ver.c_str(),this);
	    break;
	}
	// Set stream id: generate one for incoming, get it from xml if outgoing
	if (incoming()) {
	    // Generate a random, variable length stream id
	    MD5 md5(String((int)(int64_t)this));
	    md5 << m_name << String((int)Time::msecNow());
	    m_id = md5.hexDigest();
	    m_id << "_" << String((int)::random());
	}
	else {
	    m_id = xml->getAttribute("id");
	    if (!m_id)
		reason = "Missing stream id";
	    else if (m_engine->checkDupId(this))
		reason = "Duplicate stream id";
	    if (reason) {
		Debug(this,DebugNote,"Received '%s' with invalid stream id='%s' [%p]",
		    xml->tag(),m_id.c_str(),this);
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

// Check if a received element is a stream error one
bool JBStream::streamError(XmlElement* xml)
{
    if (!(xml && XMPPUtils::isTag(*xml,XmlTag::Error,XMPPNamespace::Stream)))
	return false;
    String text;
    String error;
    XMPPUtils::decodeError(xml,false,error,text);
    Debug(this,DebugAll,"Received stream error '%s' text='%s' in state %s [%p]",
	error.c_str(),text.c_str(),stateName(),this);
    int err = XMPPUtils::s_error[error];
    if (err >= XMPPError::Count)
	err = XMPPError::NoError;
    terminate(1,false,xml,err,text);
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
	    if (m_type == s2s && !m_engine->hasDomain(to.domain())) {
		terminate(0,m_incoming,xml,XMPPError::HostUnknown);
		return false;
	    }
	    if (from.domain() != m_remote.domain()) {
		terminate(0,m_incoming,xml,XMPPError::InvalidFrom);
		return false;
	    }
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
    DDebug(this,DebugAll,"Changing state from '%s' to '%s' [%p]",
	stateName(),lookup(newState,s_stateName),this);
    // Set/reset state depending data
    switch (m_state) {
	case WaitStart:
	    m_startTimeout = 0;
	    break;
	case Securing:
	    m_flags |= StreamSecured;
	    socketSetCanRead(true);
	    break;
	case Connecting:
	    m_connectTimeout = 0;
	    break;
	case Register:
	    if (type() == c2s)
		clientStream()->m_registerReq = 0;
	    break;
	default: ;
    }
    switch (newState) {
	case WaitStart:
	    if (m_engine->m_setupTimeout)
		m_setupTimeout = time + timerMultiplier(this) * m_engine->m_setupTimeout;
	    else
		m_setupTimeout = 0;
	    m_startTimeout = time + timerMultiplier(this) * m_engine->m_startTimeout;
	    DDebug(this,DebugAll,"Set timeouts start=" FMT64 " setup=" FMT64 " [%p]",
		m_startTimeout,m_setupTimeout,this);
	    if (m_xmlDom) {
		m_xmlDom->reset();
		DDebug(this,DebugAll,"XML parser reset [%p]",this);
	    }
	    break;
	case Idle:
	    m_events.clear();
	case Destroy:
	    m_id = "";
	    m_setupTimeout = 0;
	    m_startTimeout = 0;
	    // Reset all internal flags
	    m_flags &= ~InternalFlags;
	    if (type() == c2s)
		clientStream()->m_registerReq = 0;
	    break;
	case Running:
	    m_flags |= StreamSecured | StreamAuthenticated;
	    m_flags &= ~InError;
	    m_setupTimeout = 0;
	    m_startTimeout = 0;
	    if (m_state != Running)
		m_events.append(new JBEvent(JBEvent::Running,this,0));
	    break;
	case Connecting:
	    if (m_engine->m_connectTimeout)
		m_connectTimeout = time + m_engine->m_connectTimeout;
	    else
		m_connectTimeout = 0;
	    DDebug(this,DebugAll,"Set connect timeout " FMT64 " [%p]",m_connectTimeout,this);
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
    // Always try to send pending stream XML first
    if (m_outStreamXml) {
	unsigned int len = m_outStreamXml.length();
	if (!writeSocket(m_outStreamXml.c_str(),len)) {
	    terminate(0,m_incoming,0,XMPPError::SocketError);
	    return false;
	}
	bool all = (len == m_outStreamXml.length());
	if (all)
	    m_outStreamXml.clear();
	else
	    m_outStreamXml = m_outStreamXml.substr(len);
	// Start TLS now for incoming streams
	if (m_incoming && m_state == Securing) {
	    if (all) {
		m_engine->encryptStream(this);
		m_flags |= StreamTls;
		socketSetCanRead(true);
	    }
	    return true;
	}
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
    // Print the element only if it's the first time we try to send it
    if (!eout->sent())
	m_engine->printXml(this,true,*xml);
    u_int32_t len;
    const char* data = eout->getData(len);
    if (writeSocket(data,len)) {
	setIdleTimer();
	// Adjust element's buffer. Remove it from list on completion
	eout->dataSent(len);
	unsigned int rest = eout->dataCount();
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
    Debug(this,DebugNote,"Failed to send (%p,%s) in state=%s [%p]",
	xml,xml->tag(),stateName(),this);
    terminate(0,m_incoming,0,XMPPError::SocketError);
    return false;
}

// Write data to socket
bool JBStream::writeSocket(const char* data, unsigned int& len)
{
    if (!(data && m_socket)) {
	len = 0;
	return m_socket != 0;
    }
    Lock lock(this);
    if (!m_socket) {
	len = 0;
	return false;
    }
    socketSetWriting(true);
    lock.drop();
    XDebug(this,DebugInfo,"Sending %s [%p]",data,this);
    int w = m_socket->writeData(data,len);
    if (w != Socket::socketError())
	len = w;
    else
	len = 0;
#ifdef XDEBUG
    String sent(data,len);
    Debug(this,DebugInfo,"Sent %s [%p]",sent.c_str(),this);
#endif
    Lock lck(this);
    // Check if something changed
    if (!(m_socket && socketWriting())) {
	Debug(this,DebugAll,"Socket deleted while writing [%p]",this);
	return true;
    }
    socketSetWriting(false);
    if (w != Socket::socketError() || m_socket->canRetry())
	return true;
    String tmp;
    Thread::errorString(tmp,m_socket->error());
    Debug(this,DebugWarn,"Socket send error: %d: '%s' [%p]",
	m_socket->error(),tmp.c_str(),this);
    lck.drop();
    // Terminate the connection now: avoid loop back
    resetConnection();
    return false;
}

// Update stream flags and remote connection data from engine
void JBStream::updateFromRemoteDef()
 {
    m_engine->lock();
    JBRemoteDomainDef* domain = m_engine->remoteDomainDef(m_remote.domain());
    // Update flags
    m_flags = (domain->m_flags & StreamFlags);
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
    Debug(this,DebugStub,"Dropping xml=(%p,%s) ns=%s in state=%s reason='%s' [%p]",
	xml,xml->tag(),TelEngine::c_safe(xml->xmlns()),stateName(),reason,this);
    TelEngine::destruct(xml);
    return true;
}

// Set the idle timer in Running state
void JBStream::setIdleTimer(u_int64_t msecNow)
{
    // Set only for c2s in Running state
    if (m_type != c2s || m_state != Running || !m_engine->m_idleTimeout)
	return;
    m_idleTimeout = msecNow + m_engine->m_idleTimeout;
    XDebug(this,DebugAll,"Idle timeout set to " FMT64 "ms [%p]",m_idleTimeout,this);
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
	Debug(this,DebugStub,"Unhandled SASL '%s' in %s state [%p]",
	    xml->tag(),stateName(),this);
	TelEngine::destruct(xml);
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
	Debug(this,DebugNote,"Received challenge response error='%s' [%p]",
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
    XMPPFeatureSasl* sasl = static_cast<XMPPFeatureSasl*>(m_features.get(XMPPNamespace::Sasl));
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

    // Check if received unexpected feature
    if (!m_features.get(ns)) {
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
		m_flags |= StreamSecured | StreamAuthenticated;
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
	    if (flag(TlsRequired) && !flag(StreamSecured))
		return destroyDropXml(xml,XMPPError::EncryptionRequired,
		    "required encryption not supported by remote");
	    if (*t != s_dbResult || ns != XMPPNamespace::Dialback)
		return dropXml(xml,"expecting dialback result");
	    // Auth data
	    m_local = to;
	    m_remote = from;
	    if (!(m_local && engine()->hasDomain(m_local)))
		return destroyDropXml(xml,XMPPError::HostUnknown,
		    "dialback result with unknown 'to' domain");
	    if (!m_remote)
		return destroyDropXml(xml,XMPPError::BadAddressing,
		    "dialback result with empty 'from' domain");
	    const char* key = xml->getText();
	    if (TelEngine::null(key))
		return destroyDropXml(xml,XMPPError::NotAcceptable,
		    "dialback result with empty key");
	    m_flags |= StreamSecured;
	    changeState(Auth);
	    JBEvent* ev = new JBEvent(JBEvent::DbResult,this,xml,from,to);
	    ev->m_text = key;
	    m_events.append(ev);
	    return true;
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
	m_flags |= StreamSecured | StreamAuthenticated;
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
	    m_flags &= ~StreamAuthenticated;
	}
	return processSaslAuth(xml,from,to);
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
	    TelEngine::destruct(xml);
	    XmlElement* x = XMPPUtils::createElement(XmlTag::Starttls,
		XMPPNamespace::Tls);
	    return sendStreamXml(WaitTlsRsp,x);
	}
	if (flag(TlsRequired))
	    return destroyDropXml(xml,XMPPError::EncryptionRequired,
		"required encryption not supported by remote");
	m_flags |= StreamSecured;
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
    JBClientStream* client = clientStream();
    if (client) {
	TelEngine::destruct(xml);
	return client->bind();
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
	m_flags |= StreamTls;
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


/*
 * JBClientStream
 */
JBClientStream::JBClientStream(JBEngine* engine, Socket* socket, bool ssl)
    : JBStream(engine,socket,c2s,ssl),
    m_userData(0), m_registerReq(0)
{
}

JBClientStream::JBClientStream(JBEngine* engine, const JabberID& jid, const String& account,
    const NamedList& params)
    : JBStream(engine,c2s,jid,jid.domain(),account,&params),
    m_userData(0), m_registerReq(0)
{
    m_password = params.getValue("password");
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
    if (incoming()) {
	m_events.append(new JBEvent(JBEvent::Start,this,0,from,to));
	return true;
    }
    // Wait features ?
    if (flag(StreamRemoteVer1)) {
	changeState(Features);
	return true;
    }
    Debug(this,DebugStub,"Outgoing client stream: unsupported remote version (expecting 1.x)");
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
		m_flags &= ~StreamWaitChallenge;
		m_flags |= StreamWaitChgRsp;
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
		    m_flags |= StreamRfc3920Chg;
		    TelEngine::destruct(xml);
		    XmlElement* rsp = XMPPUtils::createElement(XmlTag::Response,
			XMPPNamespace::Sasl);
		    return sendStreamXml(state(),rsp);
		}
#endif
		m_flags &= ~(StreamWaitChgRsp | StreamRfc3920Chg);
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
	m_flags |= StreamAuthenticated;
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
	m_flags &= ~StreamWaitBindRsp;
	TelEngine::destruct(xml);
	if (!m_features.get(XMPPNamespace::Session)) {
	    changeState(Running);
	    return true;
	}
	// Send session
	XmlElement* sess = XMPPUtils::createIq(XMPPUtils::IqSet,0,0,"sess_1");
	sess->addChild(XMPPUtils::createElement(XmlTag::Session,XMPPNamespace::Session));
	m_flags |= StreamWaitSessRsp;
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
	m_flags &= ~StreamWaitBindRsp;
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
	m_flags &= ~RegisterUser;
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

    XMPPFeature* f = m_features.get(XMPPNamespace::Sasl);
    XMPPFeatureSasl* sasl = 0;
    if (f)
	sasl = static_cast<XMPPFeatureSasl*>(f->getObject("XMPPFeatureSasl"));
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
	m_flags |= StreamWaitChallenge;
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
    m_flags |= StreamWaitBindRsp;
    return sendStreamXml(Auth,b);
}


/*
 * JBServerStream
 */
// Build an incoming stream from a socket
JBServerStream::JBServerStream(JBEngine* engine, Socket* socket, bool component)
    : JBStream(engine,socket,component ? comp : s2s),
    m_dbKey(0)
{
}

// Build an outgoing stream
JBServerStream::JBServerStream(JBEngine* engine, const JabberID& local,
    const JabberID& remote, const char* dbId, const char* dbKey, bool dbOnly)
    : JBStream(engine,s2s,local,remote),
    m_dbKey(0)
{
    if (!(TelEngine::null(dbId) || TelEngine::null(dbKey)))
	m_dbKey = new NamedString(dbId,dbKey);
    if (dbOnly)
	m_flags |= DialbackOnly | NoAutoRestart;
}

// Send a dialback key response.
// If the stream is in Dialback state change it's state to Running if valid or
//  terminate it if invalid
bool JBServerStream::sendDbResult(const JabberID& from, const JabberID& to, bool valid)
{
    if (incoming() && state() == Auth && from == m_local && to == m_remote)
	return authenticated(valid);
    XmlElement* rsp = XMPPUtils::createDialbackResult(from,to,valid);
    return sendStanza(rsp);
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
	    engine()->buildDialbackKey(id(),key);
	    result = XMPPUtils::createDialbackKey(m_local,m_remote,key);
	    newState = Auth;
	}
    }
    else if (!m_dbKey) {
	// Dialback only with no key?
	Debug(this,DebugGoOn,"Outgoing dialback stream with no key! [%p]",this);
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
    // Check the tags of known dialback elements:
    //  there are servers who don't stamp them with the namespace
    // Let other elements stamped with dialback namespace go the upper layer
    if (type() != comp && isDbResult(*xml)) {
	if (outgoing())
	    return dropXml(xml,"dialback result on outgoing stream");
	const char* key = xml->getText();
	// Result: accept already authenticated
	if (m_local == to && m_remote == from) {
	    if (TelEngine::null(key))
		return destroyDropXml(xml,XMPPError::BadFormat,
		    "dialback result with empty key");
	    String tmp;
	    engine()->buildDialbackKey(id(),tmp);
	    if (tmp == key) {
		TelEngine::destruct(xml);
		return sendDbResult(m_local,m_remote,true);
	    }
	    return destroyDropXml(xml,XMPPError::NotAuthorized,
		"dialback result with invalid key");
	}
	JBEvent* ev = new JBEvent(JBEvent::DbResult,this,xml,from,to);
	ev->m_text = key;
	m_events.append(ev);
	return true;
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
	    if (!flag(StreamSecured) || flag(TlsRequired)) {
		start->setAttribute("version","1.0");
		start->setAttribute("xml:lang","en");
	    }
	}
    }
    else if (type() == comp) {
	if (incoming())
	    start->setAttributeValid("from",m_remote.domain());
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
	return true;

    if (type() == comp) {
	if (incoming()) {
	    changeState(Starting);
	    m_events.append(new JBEvent(JBEvent::Start,this,0,to,JabberID::empty()));
	    return true;
	}
	Debug(this,DebugStub,"JBComponentStream::processStart() not implemented for outgoing [%p]",this);
	terminate(0,true,0,XMPPError::NoError);
	return false;
    }

    if (outgoing()) {
	// Wait features ?
	if (flag(StreamRemoteVer1)) {
	    changeState(Features);
	    return true;
	}
	// Stream not secured
	if (!flag(StreamSecured)) {
	    // Accept dialback auth stream
	    // The namspace presence was already checked in checkStreamStart()
	    if (flag(TlsRequired)) {
		terminate(0,false,0,XMPPError::EncryptionRequired);
		return false;
	    }
	    m_flags |= StreamSecured;
	}
	m_flags |= StreamSecured;
	return sendDialback();
    }

    // Incoming stream
    m_local = to;
    m_remote = from;
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
	String type(xml->getAttribute("type"));
	if (type != "valid") {
	    terminate(1,false,xml,XMPPError::NoError);
	    return false;
	}
	// Stream authenticated
	TelEngine::destruct(xml);
	m_flags |= StreamAuthenticated;
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
    m_local.set(local);
    m_remote.set(remote);
    setSecured();
    XmlElement* s = buildStreamStart();
    return sendStreamXml(Features,s);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
