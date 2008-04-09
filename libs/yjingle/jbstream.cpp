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

#include <yatemime.h>
#include <string.h>
#include <stdio.h>

using namespace TelEngine;

static XMPPNamespace s_ns;               // Just use the operators
static XMPPError s_err;
static String s_qop = "auth";            // Used to build Digest MD5 SASL

static TokenDict s_streamState[] = {
    {"Idle",       JBStream::Idle},
    {"Connecting", JBStream::Connecting},
    {"Started",    JBStream::Started},
    {"Securing",   JBStream::Securing},
    {"Auth",       JBStream::Auth},
    {"Running",    JBStream::Running},
    {"Destroy",    JBStream::Destroy},
    {0,0},
};

TokenDict JBStream::s_flagName[] = {
    {"autorestart",      AutoRestart},
    {"noversion1",       NoVersion1},
    {"noremoteversion1", NoRemoteVersion1},
    {"tls",              UseTls},
    {"sasl",             UseSasl},
    {"secured",          StreamSecured},
    {"authenticated",    StreamAuthenticated},
    {"allowplainauth",   AllowPlainAuth},
    {0,0}
};

static String s_version = "1.0";         // Protocol version
static String s_declaration = "<?xml version='1.0' encoding='UTF-8'?>";

// Utility: append a param to a string
inline void s_appendParam(String& dest, const char* name, const char* value,
	bool quotes, bool first = false)
{
    if (!first)
	dest << ",";
    dest << name << "=";
    if (quotes)
	dest << "\"" << value << "\"";
    else
	dest << value;
}

#define DROP_AND_EXIT { dropXML(xml); return; }
#define INVALIDXML_AND_EXIT(code,reason) { invalidStreamXML(xml,code,reason); return; }
#define ERRORXML_AND_EXIT { errorStreamXML(xml); return; }

/**
 * JBSocket
 */

JBSocket::JBSocket(JBEngine* engine, JBStream* stream,
	const char* address, int port)
    : m_engine(engine), m_stream(stream), m_socket(0), m_remoteDomain(address),
    m_address(PF_INET), m_streamMutex(true), m_receiveMutex(true)
{
    m_address.host(address);
    m_address.port(port);
}

// Connect the socket
bool JBSocket::connect(bool& terminated, const char* newAddr, int newPort)
{
    terminate();
    Lock2 lck1(m_streamMutex,m_receiveMutex);
    m_socket = new Socket(PF_INET,SOCK_STREAM);
    // Set new connection data. Resolve remote domain
    if (newAddr)
	m_remoteDomain = newAddr;
    if (newPort)
	m_address.port(newPort);
    m_address.host(m_remoteDomain);
    lck1.drop();
    terminated = false;
    bool res = m_socket->connect(m_address);
    // Lock again to update data
    Lock2 lck2(m_streamMutex,m_receiveMutex);
    bool ok = false;
    while (true) {
	if (!m_socket) {
	    Debug(m_engine,DebugMild,
		"Stream. Socket deleted while connecting [%p]",m_stream);
	    terminated = true;
	    break;
	}
	// Check connect result
	if (!res) {
	    m_error = ::strerror(m_socket->error());
	    if (m_error.null())
		m_error = "Socket connect failure";
	    Debug(m_engine,DebugWarn,
		"Stream. Failed to connect socket to '%s:%d'. %d: '%s' [%p]",
		m_address.host().c_str(),m_address.port(),
		m_socket->error(),::strerror(m_socket->error()),m_stream);
	    break;
	}
	// Connected
	ok = true;
	m_socket->setBlocking(false);
	DDebug(m_engine,DebugAll,"Stream. Connected to '%s:%d'. [%p]",
	    m_address.host().c_str(),m_address.port(),m_stream);
	break;
    }
    lck2.drop();
    if (!ok)
	terminate();
    return ok;
}

// Close the socket
void JBSocket::terminate(bool shutdown)
{
    Lock2 lck(m_streamMutex,m_receiveMutex);
    if (!m_socket)
	return;
    Socket* tmp = m_socket;
    m_socket = 0;
    Debug(m_engine,DebugInfo,"Stream. Terminating socket shutdown=%s [%p]",
	String::boolText(shutdown),m_stream);
    lck.drop();
    if (shutdown)
	tmp->shutdown(true,true);
    else {
	tmp->setLinger(-1);
	tmp->terminate();
    }
    delete tmp;
}

// Read data from socket
bool JBSocket::recv(char* buffer, unsigned int& len)
{
    if (!valid()) {
	if (m_error.null())
	    m_error = "Socket read failure";
	return false;
    }

    int read = m_socket->readData(buffer,len);
    if (read != Socket::socketError()) {
#ifdef XDEBUG
	if (read) {
	    String s(buffer,read);
	    XDebug(m_engine,DebugAll,"Stream recv [%p]\r\n%s",
		m_stream,s.c_str());
	}
#endif
	len = read;
	return true;
    }

    len = 0;
    if (!m_socket->canRetry()) {
	m_error = ::strerror(m_socket->error());
	if (m_error.null())
	    m_error = "Socket read failure";
	Debug(m_engine,DebugWarn,
	    "Stream. Socket read error: %d: '%s' [%p]",
	    m_socket->error(),::strerror(m_socket->error()),m_stream);
	return false;
    }
    return true;
}

// Write data to socket
bool JBSocket::send(const char* buffer, unsigned int& len)
{
    if (!valid()) {
	if (m_error.null())
	    m_error = "Socket write failure";
	return false;
    }

    XDebug(m_engine,DebugAll,"Stream sending %s [%p]",buffer,m_stream);
    int c = m_socket->writeData(buffer,len);
    if (c != Socket::socketError()) {
	len = c;
	return true;
    }
    if (!m_socket->canRetry()) {
	m_error = ::strerror(m_socket->error());
	if (m_error.null())
	    m_error = "Socket write failure";
	Debug(m_engine,DebugWarn,"Stream. Socket send error: %d: '%s' [%p]",
	    m_socket->error(),::strerror(m_socket->error()),m_stream);
	return false;
    }
    len = 0;
    DDebug(m_engine,DebugMild,
	"Stream. Socket temporary unavailable to send: %d: '%s' [%p]",
	m_socket->error(),::strerror(m_socket->error()),m_stream);
    return true;
}


/**
 * JBStream
 */

// Outgoing
JBStream::JBStream(JBEngine* engine, int type, XMPPServerInfo& info,
	const JabberID& localJid, const JabberID& remoteJid)
    : m_password(info.password()), m_flags(0), m_challengeCount(2),
    m_waitState(WaitIdle), m_authMech(JIDFeatureSasl::MechNone), m_type(type),
    m_state(Idle), m_outgoing(true), m_restart(0), m_restartMax(0),
    m_timeToFillRestart(0), m_fillRestartInterval(0),
    m_local(localJid.node(),localJid.domain(),localJid.resource()),
    m_remote(remoteJid.node(),remoteJid.domain(),remoteJid.resource()),
    m_engine(engine), m_socket(engine,0,info.address(),info.port()),
    m_lastEvent(0), m_terminateEvent(0), m_startEvent(0), m_recvCount(-1),
    m_streamXML(0), m_declarationSent(0), m_nonceCount(0)
{
    m_socket.m_stream = this;

    if (!engine) {
	Debug(DebugNote,"Can't create stream without engine [%p]",this);
	return;
    }

    // Update options from server info
    if (!info.flag(XMPPServerInfo::NoAutoRestart))
	m_flags |= AutoRestart;
    // Force stream encryption if required by config
    if (info.flag(XMPPServerInfo::TlsRequired))
	m_flags |= UseTls;
    // Stream version supported by server. Ignore SASL flag if version 1 is not supported
    if (info.flag(XMPPServerInfo::NoVersion1))
	m_flags |= NoVersion1;
    else  {
	// Use RFC-3920 SASL instead of XEP-0078 authentication
	if (info.flag(XMPPServerInfo::Sasl))
	    m_flags |= UseSasl;
    }
    // Allow plain auth
    if (info.flag(XMPPServerInfo::AllowPlainAuth))
	m_flags |= AllowPlainAuth;

    // Restart counter and update interval
    if (flag(AutoRestart))
	m_restartMax = m_restart = engine->m_restartCount;
    else
	m_restartMax = m_restart = 1;
    m_fillRestartInterval = engine->m_restartUpdateInterval;
    m_timeToFillRestart = Time::msecNow() + m_fillRestartInterval;

    if (m_engine->debugAt(DebugAll)) {
	String f;
	XMPPUtils::buildFlags(f,m_flags,s_flagName);
	Debug(m_engine,DebugAll,
	    "Stream dir=outgoing type=%s local=%s remote=%s options=%s [%p]",
	    JBEngine::lookupProto(m_type),m_local.safe(),m_remote.safe(),
	    f.c_str(),this);
    }
}

JBStream::~JBStream()
{
    XDebug(m_engine,DebugAll,"JBStream::~JBStream() [%p]",this);
}

// Close the stream. Release memory
void JBStream::destroyed()
{
    if (m_engine) {
	Lock lock(m_engine);
	m_engine->m_streams.remove(this,false);
    }
    terminate(false,0,XMPPError::NoError,0,false,true);
    // m_terminateEvent shouldn't be valid:
    //  do that to print a DebugFail output for the stream inside the event
    TelEngine::destruct(m_terminateEvent);
    TelEngine::destruct(m_startEvent);
    DDebug(m_engine,DebugAll,"Stream destroyed local=%s remote=%s [%p]",
	m_local.safe(),m_remote.safe(),this);
    RefObject::destroyed();
}

// Check the 'to' attribute of a received element
bool JBStream::checkDestination(XMLElement* xml, bool& respond)
{
    respond = false;
    return true;
}

// Connect the stream
void JBStream::connect()
{
    Lock2 lck(m_socket.m_streamMutex,m_socket.m_receiveMutex);
    if (state() != Idle && state() != Connecting) {
	Debug(m_engine,DebugNote,
	    "Stream. Attempt to connect when not idle [%p]",this);
	return;
    }
    DDebug(m_engine,DebugInfo,
	"Stream. Attempt to connect local=%s remote=%s addr=%s:%d count=%u [%p]",
	m_local.safe(),m_remote.safe(),addr().host().safe(),addr().port(),
	m_restart,this);
    // Check if we can restart. Destroy the stream if not auto restarting
    if (m_restart)
	m_restart--;
    else
	return;
    // Reset data
    m_id = "";
    m_parser.reset();
    lck.drop();
    // Re-connect socket
    bool terminated = false;
    changeState(Connecting);
    // TODO: check with the engine if server info is available
    //       get address and port and pass them to socket
    if (!m_socket.connect(terminated,0,0)) {
	if (!terminated)
	    terminate(false,0,XMPPError::HostGone,m_socket.error(),false);
	return;
    }

    Debug(m_engine,DebugAll,"Stream. local=%s remote=%s connected to %s:%d [%p]",
	m_local.safe(),m_remote.safe(),addr().host().safe(),addr().port(),this);

    // Send stream start
#if 0
    if (type() == JBEngine::Client && flag(NoVersion1)) {
	Lock2 lck2(m_socket.m_streamMutex,m_socket.m_receiveMutex);
	startTls();
    }
    else
#endif
	sendStreamStart();
}

// Read data from socket and pass it to the parser
// Terminate stream on parser or socket error
bool JBStream::receive()
{
    char buf[1024];
    if (!m_recvCount || state() == Securing || state() == Destroy ||
	state() == Idle || state() == Connecting)
	return false;

    XMPPError::Type error = XMPPError::NoError;
    bool send = false;
    // Lock between start read and end consume to serialize input
    m_socket.m_receiveMutex.lock();
    const char* text = 0;
    unsigned int len = (m_recvCount < 0 ? sizeof(buf) : 1);
    if (m_socket.recv(buf,len)) {
	if (len) {
	    XDebug(m_engine,DebugAll,"Stream. Received %u bytes [%p]",len,this);
	    if (!m_parser.consume(buf,len)) {
		error = XMPPError::Xml;
		text = m_parser.ErrorDesc();
		Debug(m_engine,DebugNote,"Stream. Parser error: '%s' [%p]",text,this);
		send = true;
	    }
	    // Check if the parser consumed all it's buffer and the stream
	    //  will start TLS
	    if (!m_parser.bufLen() && m_recvCount > 0)
		setRecvCount(0);
	}
    }
    else {
	error = XMPPError::HostGone;
	text = "remote server not found";
    }
    m_socket.m_receiveMutex.unlock();
    if (error != XMPPError::NoError)
	terminate(false,0,error,text,send);
    return len != 0;
}

// Send a stanza
JBStream::Error JBStream::sendStanza(XMLElement* stanza, const char* senderId)
{
    if (!stanza)
	return ErrorContext;

    Lock lock(m_socket.m_streamMutex);
    if (state() == Destroy) {
	Debug(m_engine,DebugNote,
	    "Stream. Can't send stanza (%p,%s). Stream is destroying [%p]",
	    stanza,stanza->name(),this);
	TelEngine::destruct(stanza);
	return ErrorContext;
    }

    DDebug(m_engine,DebugAll,"Stream. Posting stanza (%p,%s) id='%s' [%p]",
	stanza,stanza->name(),senderId,this);
    XMLElementOut* e = new XMLElementOut(stanza,senderId);
    // List not empty: the return value will be ErrorPending
    // Else: element will be sent
    bool pending = (0 != m_outXML.skipNull());
    m_outXML.append(e);
    // Send first element
    Error result = sendPending();
    return pending ? ErrorPending : result;
}

// Extract an element from parser and construct an event
JBEvent* JBStream::getEvent(u_int64_t time)
{
    Lock lock(m_socket.m_streamMutex);

    // Increase stream restart counter if it's time to and should auto restart
    if (flag(AutoRestart) && m_timeToFillRestart < time) {
	m_timeToFillRestart = time + m_fillRestartInterval;
	if (m_restart < m_restartMax) {
	    m_restart++;
	    Debug(m_engine,DebugAll,"Stream. restart count=%u max=%u [%p]",
		m_restart,m_restartMax,this);
	}
    }

    if (m_lastEvent)
	return 0;

    // Do nothing if destroying or connecting
    // Just check Terminated or Running events
    // Idle: check if we can restart. Destroy the stream if not auto restarting
    if (state() == Idle || state() == Destroy || state() == Connecting) {
	if (state() == Idle) {
	    if (m_restart) {
		lock.drop();
		m_engine->connect(this);
		return 0;
	    }
	    if (!flag(AutoRestart))
		terminate(true,0,XMPPError::NoError,"connection-failed",false);
	}
	if (m_terminateEvent) {
	    m_lastEvent = m_terminateEvent;
	    m_terminateEvent = 0;
	}
	else if (m_startEvent) {
	    m_lastEvent = m_startEvent;
	    m_startEvent = 0;
	}
	return m_lastEvent;
    }

    if (!m_engine) {
	Debug(DebugMild,"Stream. Engine vanished. Can't live as orphan [%p]",this);
	terminate(true,0,XMPPError::Internal,"Engine is missing",false);
	if (m_terminateEvent) {
	    m_lastEvent = m_terminateEvent;
	    m_terminateEvent = 0;
	}
	return m_lastEvent;
    }

    while (true) {
	if (m_terminateEvent)
	    break;

	// Send pending elements and process the received ones
	sendPending();
	if (m_terminateEvent)
	    break;

	// Process the received XML
	XMLElement* xml = m_parser.extract();
	if (!xml)
	    break;

	// Print it
	m_engine->printXml(*xml,this,false);

	// Check destination
	bool respond = false;
	if (!checkDestination(xml,respond)) {
	    String type = xml->getAttribute("type");
	    Debug(m_engine,DebugNote,
		"Stream. Received %s with unacceptable destination to=%s type=%s [%p]",
		xml->name(),xml->getAttribute("to"),type.c_str(),this);
	    if (!respond)
		dropXML(xml);
	    else
		if (state() == Running)
		    switch (xml->type()) {
			case XMLElement::Iq:
			case XMLElement::Presence:
			case XMLElement::Message:
			    if (type != "error" && type != "result") {
				sendStanza(XMPPUtils::createError(xml,XMPPError::TypeModify,
				    XMPPError::HostUnknown,"Unknown destination"));
				break;
			    }
			default:
			    dropXML(xml);
		    }
		else
		    invalidStreamXML(xml,XMPPError::HostUnknown,"Unknown destination");
	    break;
	}

	// Check if stream end was received (end tag or error)
	if (xml->type() == XMLElement::StreamEnd ||
	    xml->type() == XMLElement::StreamError) {
	    Debug(m_engine,DebugAll,"Stream. Remote closed in state %s [%p]",
		lookupState(state()),this);
	    terminate(false,xml,XMPPError::NoError,xml->getText(),false);
	    break;
	}

	XDebug(m_engine,DebugAll,"Stream. Processing (%p,%s) in state %s [%p]",
	    xml,xml->name(),lookupState(state()),this);

	switch (state()) {
	    case Running:
		processRunning(xml);
		break;
	    case Auth:
		processAuth(xml);
		break;
	    case Securing:
		processSecuring(xml);
		break;
	    case Started:
		// Set stream id if not already set
		if (!m_id) {
		    if (xml->type() != XMLElement::StreamStart) {
			dropXML(xml);
			break;
		    }
		    m_id = xml->getAttribute("id");
		    if (!m_id || m_engine->checkDupId(this)) {
			invalidStreamXML(xml,XMPPError::InvalidId,"Duplicate stream id");
			break;
		    }
		    DDebug(m_engine,DebugAll,"Stream. Id set to '%s' [%p]",
			m_id.c_str(),this);
		}
		processStarted(xml);
		break;
	    default: 
		Debug(m_engine,DebugStub,"Unhandled stream state %u '%s' [%p]",
		    state(),lookupState(state()),this);
		TelEngine::destruct(xml);
	}
	break;
    }

    // Return terminate event if set
    // Get events from queue if not set to terminate
    if (m_terminateEvent) {
	m_lastEvent = m_terminateEvent;
	m_terminateEvent = 0;
    }
    else if (m_startEvent) {
	m_lastEvent = m_startEvent;
	m_startEvent = 0;
    }
    else {
	ObjList* obj = m_events.skipNull();
	m_lastEvent = obj ? static_cast<JBEvent*>(obj->get()) : 0;
	if (m_lastEvent)
	    m_events.remove(m_lastEvent,false);
    }

    if (m_lastEvent)
	DDebug(m_engine,DebugAll,"Stream. Raising event (%p,%s) [%p]",
	    m_lastEvent,m_lastEvent->name(),this);
    return m_lastEvent;
}

// Terminate stream. Send stream end tag or error. Remove pending stanzas without id
// Deref stream if destroying
void JBStream::terminate(bool destroy, XMLElement* recvStanza, XMPPError::Type error,
	const char* reason, bool send, bool final)
{
    Lock2 lock(m_socket.m_streamMutex,m_socket.m_receiveMutex);
    if (!flag(AutoRestart))
	destroy = true;
    setRecvCount(-1);
    m_nonceCount = 0;
    TelEngine::destruct(m_startEvent);
    if (m_streamXML) {
	if (m_streamXML->dataCount())
	    send = false;
	TelEngine::destruct(m_streamXML);
    }
    if (state() == Destroy) {
	resetStream();
	m_socket.terminate(true);
	TelEngine::destruct(recvStanza);
	return;
    }
    if (error == XMPPError::NoError && m_engine->exiting()) {
	error = XMPPError::Shutdown;
	reason = 0;
    }

    Debug(m_engine,DebugAll,
	"Stream. Terminate state=%s destroy=%u error=%s reason='%s' final=%u [%p]",
	lookupState(state()),destroy,s_err[error],reason,final,this);

    // Send ending stream element
    if (send && state() != Connecting && state() != Idle) {
	XMLElement* e;
	XMLElement* streamEnd = 0;
	if (error == XMPPError::NoError)
	    e = new XMLElement(XMLElement::StreamEnd);
	else {
	    e = XMPPUtils::createStreamError(error,reason);
	    XMLElement* child = recvStanza;
	    // Preserve received element if an event will be generated
	    if (recvStanza)
		if (final || m_terminateEvent)
		    recvStanza = 0;
		else
		    recvStanza = new XMLElement(*child);
	    e->addChild(child);
	    streamEnd = new XMLElement(XMLElement::StreamEnd);
	}
	if (sendStreamXML(e,m_state) && streamEnd)
	    sendStreamXML(streamEnd,m_state);
    }
    m_socket.terminate(state() == Connecting);

    // Done if called from destructor
    if (final) {
	changeState(Destroy);
	resetStream();
	TelEngine::destruct(recvStanza);
	return;
    }

    // Cancel all outgoing elements without id
    removePending(false,0,true);
    // Always set termination event, except when exiting
    if (!(m_terminateEvent || m_engine->exiting())) {
	if (!recvStanza && error != XMPPError::NoError)
	    recvStanza = XMPPUtils::createStreamError(error,reason);
	Debug(m_engine,DebugAll,"Stream. Set terminate error=%s reason=%s [%p]",
	    s_err[error],reason,this);
	m_terminateEvent = new JBEvent(destroy?JBEvent::Destroy:JBEvent::Terminated,
	    this,recvStanza);
	if (m_terminateEvent->m_text.null())
	    m_terminateEvent->m_text = reason;
	recvStanza = 0;
    }
    TelEngine::destruct(recvStanza);

    // Change state
    if (destroy) {
	changeState(Destroy);
	deref();
    }
    else
	changeState(Idle);
    resetStream();
}

// Get an object from this stream
void* JBStream::getObject(const String& name) const
{
    if (name == "Socket*")
	return state() == Securing ? (void*)&m_socket.m_socket : 0;
    if (name == "JBStream")
	return (void*)this;
    return RefObject::getObject(name);
}

// Get the name of a stream state
const char* JBStream::lookupState(int state)
{
    return lookup(state,s_streamState);
}

// Get the starting stream element to be sent after stream connected
XMLElement* JBStream::getStreamStart()
{
    m_remoteFeatures.clear();
    m_parser.reset();
    m_waitState = WaitStart;

    XMLElement* start = XMPPUtils::createElement(XMLElement::StreamStart,
	XMPPNamespace::Client);
    start->setAttribute("xmlns:stream",s_ns[XMPPNamespace::Stream]);
//    start->setAttribute("from",local().bare());
    start->setAttribute("to",remote());
    // Add version to notify the server we support RFC3920 TLS/SASL authentication
    if (!flag(NoVersion1))
	start->setAttribute("version",s_version);
    return start;
}

// Get the authentication element to be sent when authentication starts
XMLElement* JBStream::getAuthStart()
{
    XMLElement* xml = 0;
    // Deprecated XEP-0078 authentication 
    if (!flag(UseSasl)) {
	xml = XMPPUtils::createIq(XMPPUtils::IqGet,0,0,"auth_1");
	xml->addChild(XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::IqAuth));
	m_waitState = WaitChallenge;
	return xml;
    }
    // RFC 3920 SASL
    if (m_authMech != JIDFeatureSasl::MechMD5 &&
	m_authMech != JIDFeatureSasl::MechPlain)
	return 0;
    String rsp;
    if (m_authMech == JIDFeatureSasl::MechPlain)
	buildSaslResponse(rsp);
    xml = XMPPUtils::createElement(XMLElement::Auth,XMPPNamespace::Sasl,rsp);
    xml->setAttribute("mechanism",lookup(m_authMech,JIDFeatureSasl::s_authMech));
    m_waitState = WaitChallenge;
    return xml;
}

// Process received data while running
void JBStream::processRunning(XMLElement* xml)
{
    XDebug(m_engine,DebugAll,"JBStream::processRunning('%s') [%p]",xml->name(),this);

    switch (xml->type()) {
	case XMLElement::Message:
	    m_events.append(new JBEvent(JBEvent::Message,this,xml));
	    return;
	case XMLElement::Presence:
	    m_events.append(new JBEvent(JBEvent::Presence,this,xml));
	    return;
	case XMLElement::Iq:
	    break;
	default:
	    m_events.append(new JBEvent(JBEvent::Unhandled,this,xml));
	    return;
    }

    XMPPError::Type error = XMPPError::NoError;
    int iq = XMPPUtils::iqType(xml->getAttribute("type"));
    JBEvent* ev = getIqEvent(xml,iq,error);
    if (ev) {
	m_events.append(ev);
	return;
    }
    if (error == XMPPError::NoError) {
	m_events.append(new JBEvent(JBEvent::Unhandled,this,xml));
	return;
    }

    // Don't respond to error or result
    if (iq == XMPPUtils::IqError || iq == XMPPUtils::IqResult) {
	dropXML(xml);
	return;
    }

    // Send error
    sendStanza(XMPPUtils::createError(xml,XMPPError::TypeModify,error));
}

// Helper function to make the code simpler
inline bool checkChild(XMLElement* e, XMPPNamespace::Type ns, XMPPError::Type& error)
{
    if (!e) {
	error = XMPPError::SBadRequest;
	return false;
    }
    if (XMPPUtils::hasXmlns(*e,ns))
	return true;
    error = XMPPError::SFeatureNotImpl;
    return false;
}

// Process a received element in Securing state
void JBStream::processSecuring(XMLElement* xml)
{
    Debug(m_engine,DebugInfo,"Stream. Received '%s' while securing the stream [%p]",
	xml->name(),this);
    dropXML(xml);
}

// Process a received element in Auth state
void JBStream::processAuth(XMLElement* xml)
{
    XDebug(m_engine,DebugAll,"JBStream::processAuth('%s') [%p]",xml->name(),this);

    // Waiting for abort to be confirmed
    if (m_waitState == WaitAborted) {
	if (xml->type() != XMLElement::Aborted)
	    DROP_AND_EXIT
	if (!XMPPUtils::hasXmlns(*xml,XMPPNamespace::Sasl))
	    INVALIDXML_AND_EXIT(XMPPError::InvalidNamespace,0)
	terminate(false,0,XMPPError::Aborted,"Authentication aborted",false);
	TelEngine::destruct(xml);
	return;
    }

    while (true) {
	// Sanity: check wait state
	if (m_waitState != WaitChallenge && m_waitState != WaitResponse)
	    DROP_AND_EXIT

	// SASL: accept challenge, failure, success
	if (flag(UseSasl)) {
	    if (xml->type() != XMLElement::Success &&
		xml->type() != XMLElement::Challenge &&
		xml->type() != XMLElement::Failure)
		DROP_AND_EXIT
	    if (!XMPPUtils::hasXmlns(*xml,XMPPNamespace::Sasl))
		INVALIDXML_AND_EXIT(XMPPError::InvalidNamespace,0)
	    // Succes
	    if (xml->type() == XMLElement::Success) {
		// SASL Digest MD5: Check server credentials
		if (flag(UseSasl) && m_authMech == JIDFeatureSasl::MechMD5) {
		    String tmp = xml->getText();
		    DataBlock rspauth;
		    Base64 base((void*)tmp.c_str(),tmp.length(),false);
		    bool ok = base.decode(rspauth);
		    base.clear(false);
		    if (!ok)
			INVALIDXML_AND_EXIT(XMPPError::IncorrectEnc,0);
		    tmp.assign((const char*)rspauth.data(),rspauth.length());
		    if (!tmp.startSkip("rspauth=",false))
			INVALIDXML_AND_EXIT(XMPPError::BadFormat,"Invalid challenge");
		    String rspAuth;
		    buildDigestMD5Sasl(rspAuth,false);
		    if (rspAuth != tmp)
			INVALIDXML_AND_EXIT(XMPPError::InvalidAuth,"Invalid challenge auth");
		    DDebug(m_engine,DebugAll,"Stream. Server authenticated [%p]",this);
		}
		TelEngine::destruct(xml);
		break;
	    }
	    // Challenge. Send response or abort if can't retry
	    if (xml->type() == XMLElement::Challenge) {
		if (m_challengeCount) {
		    m_challengeCount--;
		    sendAuthResponse(xml);
		}
		else {
		    // Abort
		    m_waitState = WaitAborted;
		    TelEngine::destruct(xml);
		    xml = XMPPUtils::createElement(XMLElement::Abort,XMPPNamespace::Sasl);
		    sendStreamXML(xml,state());
		}
		return;
	    }
	    // Failure
	    XMLElement* e = xml->findFirstChild();
	    XMPPError::Type err = XMPPError::NoError;
	    String reason = "Authentication failed";
	    if (e) {
		err = (XMPPError::Type)XMPPError::type(e->name());
		if (err == XMPPError::Count)
		    err = XMPPError::NoError;
		reason << " with reason '" << e->name() << "'";
	    }
	    terminate(false,xml,err,reason,false);
	    return;
	}

	// XEP-0078: accept iq result or error
	if (xml->type() != XMLElement::Iq)
	    DROP_AND_EXIT
	// Check if received correct type
	XMPPUtils::IqType t = XMPPUtils::iqType(xml->getAttribute("type"));
	if (t != XMPPUtils::IqResult && t != XMPPUtils::IqError)
	    DROP_AND_EXIT
	// Check if received correct id for the current waiting state
	if (xml->hasAttribute("id","auth_1")) {
	    if (m_waitState != WaitChallenge)
		DROP_AND_EXIT
	}
	else if (xml->hasAttribute("id","auth_2")) {
	    if (m_waitState != WaitResponse)
		DROP_AND_EXIT
	}
	else
	    DROP_AND_EXIT

	// Terminate now on valid error
	if (t == XMPPUtils::IqError)
	    ERRORXML_AND_EXIT

	// Result.
	// WaitResponse: authenticated
	if (m_waitState == WaitResponse) {
	    TelEngine::destruct(xml);
	    break;
	}
	// WaitChallenge: Check child and its namespace. Send response
	XMLElement* child = xml->findFirstChild(XMLElement::Query);
	if (!(child && XMPPUtils::hasXmlns(*child,XMPPNamespace::IqAuth)))
	    INVALIDXML_AND_EXIT(XMPPError::InvalidNamespace,0)
	// XEP-0078: username and resource children must be present
	if (!(child->findFirstChild(XMLElement::Username) &&
	    child->findFirstChild(XMLElement::Resource)))
	    INVALIDXML_AND_EXIT(XMPPError::InvalidXml,"Username or resource child is missing")
	// Get authentication methods
	m_remoteFeatures.clear();
	if (child->findFirstChild(XMLElement::Digest))
	    m_remoteFeatures.add(new JIDFeatureSasl(JIDFeatureSasl::MechSHA1));
	if (child->findFirstChild(XMLElement::Password))
	    m_remoteFeatures.add(new JIDFeatureSasl(JIDFeatureSasl::MechPlain));
	setClientAuthMechanism();
	sendAuthResponse(xml);
	return;
    }

    // Authenticated
    resetStream();
    if (flag(UseSasl))
	sendStreamStart();
    else {
	Debug(m_engine,DebugInfo,"Stream. Authenticated [%p]",this);
	changeState(Running);
    }
}

// Process a received element in Started state
void JBStream::processStarted(XMLElement* xml)
{
    XDebug(m_engine,DebugAll,"JBStream::processStarted('%s') [%p]",xml->name(),this);

    if (m_waitState == WaitStart) {
	if (xml->type() != XMLElement::StreamStart)
	    DROP_AND_EXIT
	// Check attributes: namespaces, from
	if (!(xml->hasAttribute("xmlns:stream",s_ns[XMPPNamespace::Stream]) &&
	    XMPPUtils::hasXmlns(*xml,XMPPNamespace::Client)))
	    INVALIDXML_AND_EXIT(XMPPError::InvalidNamespace,0)
	if (!(remote().domain() &= xml->getAttribute("from")))
	    INVALIDXML_AND_EXIT(XMPPError::HostUnknown,0)

	// Get received version
	String version = xml->getAttribute("version");
	if (version.null())
	    m_flags |= NoRemoteVersion1;
	else {
	    int pos = version.find('.');
	    String majorStr = (pos != -1) ? version.substr(0,pos) : version;
	    int major = majorStr.toInteger(0);
	    if (major == 0)
		m_flags |= NoRemoteVersion1;
	    else
		m_flags &= ~NoRemoteVersion1;
	}

	// Version 1: wait stream features
	// Version 0: XEP-0078: start auth
	setRecvCount(-1);
	if (flag(NoVersion1))
	    startAuth();
	else
	    m_waitState = WaitFeatures;
    }
    else if (m_waitState == WaitFeatures) {
	if (xml->type() != XMLElement::StreamFeatures)
	    DROP_AND_EXIT
	if (!getStreamFeatures(xml))
	    return;
	// Check TLS if not already secured
	if (!flag(StreamSecured)) {
	    // Ignore all other features if TLS is started
	    // If missing: TLS shouldn't be used
	    // If present but not required check the local flag
	    JIDFeature* f = m_remoteFeatures.get(XMPPNamespace::Starttls);
	    if (f && (f->required() || flag(UseTls))) {
		setRecvCount(1);
		TelEngine::destruct(xml);
		xml = XMPPUtils::createElement(XMLElement::Starttls,XMPPNamespace::Starttls);
		sendStreamXML(xml,state());
		m_waitState = WaitTlsRsp;
		return;
	    }
	}
	m_flags |= StreamSecured;
	// Check if already authenticated
	if (!flag(StreamAuthenticated)) {
	    // RFC 3920 6.1: no mechanisms --> SASL not supported
	    XMLElement* e = xml->findFirstChild(XMLElement::Mechanisms);
	    if (!(e && e->findFirstChild()))
		m_flags &= ~UseSasl;
	    startAuth();
	    TelEngine::destruct(xml);
	    return;
	}
	m_flags |= StreamAuthenticated;
	// Bind resource
	XMLElement* bind = XMPPUtils::createElement(XMLElement::Bind,XMPPNamespace::Bind);
	if (!m_local.resource().null())
	    bind->addChild(new XMLElement(XMLElement::Resource,0,m_local.resource()));
	XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,0,0,"bind_1");
	iq->addChild(bind);
	m_waitState = WaitBindRsp;
	sendStreamXML(iq,state());
    }
    else if (m_waitState == WaitTlsRsp) {
	// Accept proceed and failure
	bool ok = (xml->type() == XMLElement::Proceed);
	if (!(ok || xml->type() == XMLElement::Failure) &&
	    !XMPPUtils::hasXmlns(*xml,XMPPNamespace::Starttls))
	    INVALIDXML_AND_EXIT(XMPPError::InvalidNamespace,0)
	if (ok)
	    startTls();
	else
	    terminate(false,0,XMPPError::NoError,"Server can't start TLS",false);
    }
    else if (m_waitState == WaitBindRsp) {
	// Accept iq result or error
	if (xml->type() != XMLElement::Iq)
	    DROP_AND_EXIT
	// Check if received correct type
	XMPPUtils::IqType t = XMPPUtils::iqType(xml->getAttribute("type"));
	if (t != XMPPUtils::IqResult && t != XMPPUtils::IqError)
	    DROP_AND_EXIT
	// Check if received correct id for the current waiting state
	if (!xml->hasAttribute("id","bind_1"))
	    DROP_AND_EXIT

	// Terminate now on valid error
	if (t == XMPPUtils::IqError)
	    ERRORXML_AND_EXIT

	// Result
	XMLElement* child = xml->findFirstChild(XMLElement::Bind);
	if (!child)
	    INVALIDXML_AND_EXIT(XMPPError::InvalidXml,"Bind child is missing")
	if (!XMPPUtils::hasXmlns(*child,XMPPNamespace::Bind))
	    INVALIDXML_AND_EXIT(XMPPError::InvalidNamespace,0)
	child = child->findFirstChild(XMLElement::Jid);
	if (!child)
	    INVALIDXML_AND_EXIT(XMPPError::InvalidXml,"Jid child is misssing")
	JabberID jid(child->getText());
	if (!jid.isFull())
	    INVALIDXML_AND_EXIT(XMPPError::InvalidXml,"Invalid JID")
	m_local.set(jid.node(),jid.domain(),jid.resource());
	changeState(Running);
    }
    else
	DROP_AND_EXIT
    TelEngine::destruct(xml);
}

// Create an iq event from a received iq stanza
JBEvent* JBStream::getIqEvent(XMLElement* xml, int iqType, XMPPError::Type& error)
{
    // Filter iq stanzas to generate an appropriate event
    // Get iq type : set/get, error, result
    //   result:  MAY have a first child with a response
    //   set/get: MUST have a first child
    //   error:   MAY have a first child with the sent stanza
    //            MUST have an 'error' child
    // Check type and the first child's namespace
    XMLElement* child = xml->findFirstChild();

    // Fix some element name conflicts
    if (child && child->type() == XMLElement::Session &&
	child->hasAttribute("xmlns",s_ns[XMPPNamespace::Jingle]))
	child->changeType(XMLElement::Jingle);

    // Create event
    if (iqType == XMPPUtils::IqResult || iqType == XMPPUtils::IqSet ||
	iqType == XMPPUtils::IqGet) {
	if (!child) {
	    if (iqType == XMPPUtils::IqResult)
		return new JBEvent(JBEvent::IqResult,this,xml);
	    return new JBEvent(JBEvent::Iq,this,xml);
	}

	switch (child->type()) {
	    case XMLElement::Jingle:
		if (!checkChild(child,XMPPNamespace::Jingle,error))
		    return 0;
		switch (iqType) {
		    case XMPPUtils::IqGet:
			return new JBEvent(JBEvent::IqJingleGet,this,xml,child);
		    case XMPPUtils::IqSet:
			return new JBEvent(JBEvent::IqJingleSet,this,xml,child);
		    case XMPPUtils::IqResult:
			return new JBEvent(JBEvent::IqJingleRes,this,xml,child);
		}
		break;
	    case XMLElement::Query:
		if (checkChild(child,XMPPNamespace::DiscoInfo,error))
		    switch (iqType) {
			case XMPPUtils::IqGet:
			    return new JBEvent(JBEvent::IqDiscoInfoGet,this,xml,child);
			case XMPPUtils::IqSet:
			    return new JBEvent(JBEvent::IqDiscoInfoSet,this,xml,child);
			case XMPPUtils::IqResult:
			    return new JBEvent(JBEvent::IqDiscoInfoRes,this,xml,child);
		    }
		else if (checkChild(child,XMPPNamespace::DiscoItems,error))
		    switch (iqType) {
			case XMPPUtils::IqGet:
			    return new JBEvent(JBEvent::IqDiscoItemsGet,this,xml,child);
			case XMPPUtils::IqSet:
			    return new JBEvent(JBEvent::IqDiscoItemsSet,this,xml,child);
			case XMPPUtils::IqResult:
			    return new JBEvent(JBEvent::IqDiscoItemsRes,this,xml,child);
		    }
		else if (checkChild(child,XMPPNamespace::Roster,error))
		    switch (iqType) {
			case XMPPUtils::IqGet:
			    error = XMPPError::SBadRequest;
			    break;
			case XMPPUtils::IqSet:
			    return new JBEvent(JBEvent::IqRosterSet,this,xml,child);
			case XMPPUtils::IqResult:
			    return new JBEvent(JBEvent::IqRosterRes,this,xml,child);
		    }
		return 0;
	    case XMLElement::Command:
		if (!checkChild(child,XMPPNamespace::Command,error))
		    return 0;
		switch (iqType) {
		    case XMPPUtils::IqGet:
			return new JBEvent(JBEvent::IqCommandGet,this,xml,child);
		    case XMPPUtils::IqSet:
			return new JBEvent(JBEvent::IqCommandSet,this,xml,child);
		    case XMPPUtils::IqResult:
			return new JBEvent(JBEvent::IqCommandRes,this,xml,child);
		}
	    default: ;
	}
	// Unhandled child
	if (iqType != XMPPUtils::IqResult)
	    return new JBEvent(JBEvent::Iq,this,xml,child);
	return new JBEvent(JBEvent::IqResult,this,xml,child);
    }
    else if (iqType == XMPPUtils::IqError) {
	JBEvent::Type evType = JBEvent::IqError;
	// First child may be a sent stanza
	if (child && child->type() != XMLElement::Error) {
	    switch (child->type()) {
		case XMLElement::Jingle:
		    evType = JBEvent::IqJingleErr;
		    break;
		case XMLElement::Query:
		    if (XMPPUtils::hasXmlns(*xml,XMPPNamespace::DiscoInfo))
			evType = JBEvent::IqDiscoInfoErr;
		    else if (XMPPUtils::hasXmlns(*xml,XMPPNamespace::DiscoItems))
			evType = JBEvent::IqDiscoItemsErr;
		    else if (XMPPUtils::hasXmlns(*xml,XMPPNamespace::Roster))
			evType = JBEvent::IqRosterErr;
		    break;
		case XMLElement::Command:
		    evType = JBEvent::IqCommandErr;
		    break;
		default: ;
	    }
	    child = xml->findNextChild(child);
	}
	if (!(child && child->type() == XMLElement::Error))
	    child = 0;
	return new JBEvent(evType,this,xml,child);
    }
    error = XMPPError::SBadRequest;
    return 0;
}

// Send declaration and stream start
bool JBStream::sendStreamStart()
{
    m_id = "";
    m_declarationSent = 0;
    return sendStreamXML(getStreamStart(),Started);
}

// Send stream XML elements through the socket
bool JBStream::sendStreamXML(XMLElement* e, State newState)
{
    Lock lock(m_socket.m_streamMutex);
    Error ret = ErrorContext;
    while (e) {
	if (state() == Idle || state() == Destroy)
	    break;
	if (m_streamXML) {
	    ret = sendPending();
	    if (ret != ErrorNone) {
		TelEngine::destruct(e);
		break;
	    }
	}
	bool unclose = (e->type() == XMLElement::StreamStart ||
	    e->type() == XMLElement::StreamEnd);
	m_streamXML = new XMLElementOut(e,0,unclose);
	ret = sendPending();
	if (ret == ErrorPending)
	    ret = ErrorNone;
	break;
    }
    if (ret == ErrorNone)
	changeState(newState);
    return (ret == ErrorNone);
}

// Terminate stream on receiving invalid elements
void JBStream::invalidStreamXML(XMLElement* xml, XMPPError::Type error, const char* reason)
{
    if (!xml)
	return;
    Debug(m_engine,DebugNote,
	"Stream. Invalid XML (%p,%s) state=%s error='%s' reason='%s' [%p]",
	xml,xml->name(),lookupState(state()),s_err[error],reason,this);
    terminate(false,xml,error,reason,true);
}

// Terminate stream on receiving stanza errors
void JBStream::errorStreamXML(XMLElement* xml)
{
    String error, reason;
    if (xml) {
	XMPPUtils::decodeError(xml->findFirstChild(XMLElement::Error),error,reason);
	TelEngine::destruct(xml);
    }
    Debug(m_engine,DebugNote,"Stream. Received error=%s reason='%s' state=%s [%p]",
	error.c_str(),reason.c_str(),lookupState(state()),this);
    terminate(false,0,XMPPError::NoError,reason?reason:error,true);
}

// Drop an unexpected or unhandled element
void JBStream::dropXML(XMLElement* xml, bool unexpected)
{
    if (!xml)
	return;
    Debug(m_engine,unexpected?DebugNote:DebugInfo,
	"Stream. Dropping %s element (%p,%s) in state %s [%p]",
	unexpected?"unexpected":"unhandled",xml,xml->name(),
	lookupState(state()),this);
    TelEngine::destruct(xml);
}

// Change stream state
void JBStream::changeState(State newState)
{
    if (m_state == newState)
	return;
    Debug(m_engine,DebugInfo,"Stream. Changing state from %s to %s [%p]",
	lookupState(m_state),lookupState(newState),this);
    m_state = newState;
    if (newState == Running) {
	streamRunning();
	if (!m_startEvent)
	    m_startEvent = new JBEvent(JBEvent::Running,this,0);
   }
}

// Parse receive stream features
bool JBStream::getStreamFeatures(XMLElement* features)
{
#define REQUIRED(xml) (0 != xml->findFirstChild(XMLElement::Required))
#define GET_FEATURE(xmlType,ns) { \
    XMLElement* e = features->findFirstChild(xmlType); \
    if (e) { \
	if (!(XMPPUtils::hasXmlns(*e,ns))) { \
	    invalidStreamXML(features,XMPPError::InvalidNamespace,0); \
	    return false; \
	} \
	m_remoteFeatures.add(ns,REQUIRED(e)); \
    } \
}
    m_remoteFeatures.clear();
    if (!features)
	return true;

    // TLS
    GET_FEATURE(XMLElement::Starttls,XMPPNamespace::Starttls)
    // SASL
    XMLElement* sasl = features->findFirstChild(XMLElement::Mechanisms);
    if (sasl) {
	if (!(XMPPUtils::hasXmlns(*sasl,XMPPNamespace::Sasl))) {
	    invalidStreamXML(features,XMPPError::InvalidNamespace,0);
	    return false;
	}
	int auth = 0;
	XMLElement* m = 0;
	while (0 != (m = sasl->findNextChild(m,XMLElement::Mechanism)))
	    auth |= lookup(m->getText(),JIDFeatureSasl::s_authMech);
	m_remoteFeatures.add(new JIDFeatureSasl(auth,REQUIRED(sasl)));
    }
    setClientAuthMechanism();
    // Old auth (older the version 1.0 SASL)
    GET_FEATURE(XMLElement::Auth,XMPPNamespace::IqAuthFeature)
    // Register new user
    GET_FEATURE(XMLElement::Register,XMPPNamespace::Register)
    // Bind resources
    GET_FEATURE(XMLElement::Bind,XMPPNamespace::Bind)
    // Sessions
    GET_FEATURE(XMLElement::Session,XMPPNamespace::Session)
    return true;
#undef GET_FEATURE
#undef REQUIRED
}

// Start client TLS. Terminate the stream on error
bool JBStream::startTls()
{
    Debug(m_engine,DebugInfo,"Stream. Initiating TLS [%p]",this);
    changeState(Securing);
    if (m_engine->encryptStream(this)) {
	m_flags |= StreamSecured;
	setRecvCount(-1);
	sendStreamStart();
	return true;
    }
    terminate(false,0,XMPPError::NoError,"Failed to start TLS",false);
    return false;
}

// Start client authentication
bool JBStream::startAuth()
{
    XMLElement* xml = getAuthStart();
    if (xml) {
	Debug(m_engine,DebugAll,
	    "Stream. Starting authentication type=%s mechanism=%s [%p]",
	    ((type()==JBEngine::Component)?"handshake":(flag(UseSasl)?"SASL":"IQ")),
	    lookup(m_authMech,JIDFeatureSasl::s_authMech),this);
	return sendStreamXML(xml,Auth);
    }
    Debug(m_engine,DebugNote,"Stream. Failed to build auth start [%p]",this);
    terminate(false,0,XMPPError::InvalidMechanism,"No mechanism available",true);
    return false;
}

// Send auth response to received challenge/ig
bool JBStream::sendAuthResponse(XMLElement* challenge)
{
    XMLElement* xml = 0;
    String response;
    XMPPError::Type code = XMPPError::NoError;
    const char* error = 0;

    if (flag(UseSasl))
#define SET_CODE_AND_BREAK(c,e) { code = c; error = e; break; }
	while (true) {
	    if (m_authMech != JIDFeatureSasl::MechMD5 &&
		m_authMech != JIDFeatureSasl::MechPlain)
		SET_CODE_AND_BREAK(XMPPError::InvalidMechanism,"No mechanism available")
	    // This should never happen
	    if (!(challenge && challenge->type() == XMLElement::Challenge))
		SET_CODE_AND_BREAK(XMPPError::Internal,"Unexpected element while expecting 'challenge'")
	    // TODO: implement challenge when using plain authentication
	    if (m_authMech == JIDFeatureSasl::MechPlain) {
		const char* s = "Challenge not implemented for plain authentication";
		Debug(m_engine,DebugStub,"Stream. %s [%p]",s,this);
		SET_CODE_AND_BREAK(XMPPError::UndefinedCondition,s)
	    }
	    const char* chgText = challenge->getText();
	    if (!chgText)
		SET_CODE_AND_BREAK(XMPPError::BadFormat,"Challenge is empty")
	    Base64 base64((void*)chgText,::strlen(chgText),false);
	    DataBlock chg;
	    bool ok = base64.decode(chg,false);
	    base64.clear(false);
	    if (!ok)
		SET_CODE_AND_BREAK(XMPPError::IncorrectEnc,"Challenge with incorrect encoding")
	    String tmp((const char*)chg.data(),chg.length());
	    if (tmp.null())
		SET_CODE_AND_BREAK(XMPPError::BadFormat,"Challenge is empty")
	    String nonce,realm;
	    ObjList* obj = tmp.split(',',false);
	    for (ObjList* o = obj->skipNull(); o; o = o->skipNext()) {
		String* s = static_cast<String*>(o->get());
		if (s->startsWith("realm="))
		    realm = s->substr(6);
		else if (s->startsWith("nonce="))
		    nonce = s->substr(6);
	    }
	    TelEngine::destruct(obj);
	    MimeHeaderLine::delQuotes(realm);
	    MimeHeaderLine::delQuotes(nonce);
	    if (realm.null() || nonce.null())
		SET_CODE_AND_BREAK(XMPPError::BadFormat,"Challenge is incomplete")
	    buildSaslResponse(response,&realm,&nonce);
	    xml = XMPPUtils::createElement(XMLElement::Response,XMPPNamespace::Sasl,response);
	    break;
	}
#undef SET_CODE_AND_BREAK
    else {
	xml = XMPPUtils::createIq(XMPPUtils::IqSet,0,0,"auth_2");
	XMLElement* q = XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::IqAuth);
	q->addChild(new XMLElement(XMLElement::Username,0,m_local.node()));
	q->addChild(new XMLElement(XMLElement::Resource,0,m_local.resource()));
	if (m_authMech == JIDFeatureSasl::MechSHA1) {
	    SHA1 sha;
	    sha << id() << m_password;
	    q->addChild(new XMLElement(XMLElement::Digest,0,sha.hexDigest()));
	}
	else if (m_authMech == JIDFeatureSasl::MechPlain)
	    q->addChild(new XMLElement(XMLElement::Password,0,m_password));
	else {
	    code = XMPPError::InvalidMechanism;
	    error = "No mechanism available";
	}
	xml->addChild(q);
    }

    if (!error) {
	TelEngine::destruct(challenge);
	m_waitState = WaitResponse;
	return sendStreamXML(xml,state());
    }
    TelEngine::destruct(xml);
    Debug(m_engine,DebugNote,"Stream. Failed to respond error=%s reason='%s'. %s [%p]",
	s_err[code],error,flag(UseSasl)?"Aborting":"Terminating",this);
    if (flag(UseSasl)) {
	TelEngine::destruct(challenge);
	xml = XMPPUtils::createElement(XMLElement::Abort,XMPPNamespace::Sasl);
	return sendStreamXML(xml,state());
    }
    terminate(false,challenge,code,error,true);
    return false;
}

// Build SASL authentication response
// A valid mechanism must be previously set
void JBStream::buildSaslResponse(String& response, String* realm, String* nonce)
{
    // Digest MD5. See RFC 4616 Section 2
    // [authzid] UTF8NUL authcid UTF8NUL passwd
    if (m_authMech == JIDFeatureSasl::MechPlain) {
	DataBlock data;
	unsigned char nul = 0;
	data.append(&nul,1);
	data += m_local.node();
	data.append(&nul,1);
	data += m_password;
	Base64 base64((void*)data.data(),data.length());
	base64.encode(response);
	return;
    }

    // Digest MD5. See RFC 2831 2.1.2.1
    MD5 md5(String((unsigned int)::random()));
    m_cnonce = md5.hexDigest();
    s_appendParam(response,"username",m_local.node(),true,true);
    if (realm) {
	m_realm = *realm;
	s_appendParam(response,"realm",m_realm,true);
	if (nonce) {
	    m_nonce = *nonce;
	    s_appendParam(response,"nonce",m_nonce,true);
	    m_nonceCount++;
	    char tmp[9];
	    ::sprintf(tmp,"%08x",m_nonceCount);
	    m_nc = tmp;
	    s_appendParam(response,"nc",m_nc,false);
	}
    }
    s_appendParam(response,"cnonce",m_cnonce,true);
    s_appendParam(response,"digest-uri",String("xmpp/")+m_local.domain(),true);
    s_appendParam(response,"qop",s_qop,true);
    String rsp;
    buildDigestMD5Sasl(rsp,true);
    s_appendParam(response,"response",rsp,false);
    s_appendParam(response,"charset","utf-8",false);
    s_appendParam(response,"algorithm","md5-sess",false);
    Base64 base64((void*)response.c_str(),response.length());
    base64.encode(response);
}

// Parse remote's features and pick an authentication mechanism
//  to be used when requesting authentication
void JBStream::setClientAuthMechanism()
{
    JIDFeature* f = m_remoteFeatures.get(XMPPNamespace::Sasl);
    JIDFeatureSasl* sasl = static_cast<JIDFeatureSasl*>(f);
    m_authMech = JIDFeatureSasl::MechNone;
    if (!sasl)
	return;
    // Component or not using SASL: accept SHA1 and plain
    if (type() == JBEngine::Component || !flag(UseSasl)) {
	if (sasl->mechanism(JIDFeatureSasl::MechSHA1))
	    m_authMech = JIDFeatureSasl::MechSHA1;
	else if (sasl->mechanism(JIDFeatureSasl::MechPlain) && flag(AllowPlainAuth))
	    m_authMech = JIDFeatureSasl::MechPlain;
	return;
    }
    // SASL: accept Digest MD5
    if (sasl->mechanism(JIDFeatureSasl::MechMD5))
	m_authMech = JIDFeatureSasl::MechMD5;
    else if (sasl->mechanism(JIDFeatureSasl::MechPlain) && flag(AllowPlainAuth))
	m_authMech = JIDFeatureSasl::MechPlain;
}

// Build a Digest MD5 SASL to be sent with authentication responses
// See RFC 2831 2.1.2.1
// A1 = H(username:realm:passwd):nonce:cnonce:authzid
// A2 ="AUTHENTICATE:uri
// rsp = HEX(HEX(A1):nonce:nc:cnonce:qop:HEX(A2))
void JBStream::buildDigestMD5Sasl(String& dest, bool authenticate)
{
    MD5 md5;
    md5 << m_local.node() << ":" << m_realm << ":" << m_password;
    MD5 md5A1(md5.rawDigest(),16);
    if (m_nonce)
	md5A1 << ":" << m_nonce;
    md5A1 << ":" << m_cnonce;
    MD5 md5A2;
    if (authenticate)
	md5A2 << "AUTHENTICATE";
    md5A2 << ":xmpp/" << m_local.domain();
    MD5 md5Rsp;
    md5Rsp << md5A1.hexDigest();
    if (m_nonce)
	md5Rsp << ":" << m_nonce << ":" << m_nc;
    md5Rsp << ":" << m_cnonce << ":" << s_qop << ":" << md5A2.hexDigest();
    dest = md5Rsp.hexDigest();
}

// Event termination notification
void JBStream::eventTerminated(const JBEvent* event)
{
    if (event && event == m_lastEvent) {
	m_lastEvent = 0;
	DDebug(m_engine,DebugAll,
	    "Stream. Event (%p,%s) terminated [%p]",event,event->name(),this);
    }
}

// Try to send the first element in pending outgoing stanzas list
// Terminate stream on socket error
JBStream::Error JBStream::sendPending()
{
    XMLElementOut* eout = 0;

    if (state() == Destroy)
	return ErrorContext;

    if (m_streamXML) {
	// Check if declaration was sent
	if (m_declarationSent < s_declaration.length()) {
	    const char* data = s_declaration.c_str() + m_declarationSent;
	    unsigned int len = s_declaration.length() - m_declarationSent;
	    if (!m_socket.send(data,len)) {
		Debug(m_engine,DebugNote,"Stream. Failed to send declaration [%p]",this);
		terminate(false,0,XMPPError::HostGone,"Failed to send data",false);
		return ErrorNoSocket;
	    }
	    m_declarationSent += len;
	    if (m_declarationSent < s_declaration.length())
		return ErrorPending;
	    DDebug(m_engine,DebugAll,"Stream. Sent declaration %s [%p]",
		s_declaration.c_str(),this);
	}
	eout = m_streamXML;
    }
    else {
	ObjList* obj = m_outXML.skipNull();
	if (!obj)
	    return ErrorNone;
	if (state() != Running)
	    return ErrorPending;
	eout = obj ? static_cast<XMLElementOut*>(obj->get()) : 0;
    }
    XMLElement* xml = eout->element();
    if (!xml) {
	if (eout != m_streamXML)
	    m_outXML.remove(eout,true);
	else
	    TelEngine::destruct(m_streamXML);
	return ErrorNone;
    }

    // Print the element only if it's the first time
    if (!eout->sent())
	m_engine->printXml(*xml,this,true);

    Error ret = ErrorNone;
    u_int32_t len;
    const char* data = eout->getData(len);
    unsigned int tmp = len;
    if (m_socket.send(data,len)) {
	if (len != tmp)
	    ret = ErrorPending;
	eout->dataSent(len);
    }	
    else
	ret = ErrorNoSocket;

    if (ret == ErrorPending)
	return ret;

    if (ret == ErrorNone)
	DDebug(m_engine,DebugAll,"Stream. Sent element (%p,%s) id='%s [%p]",
	    xml,xml->name(),eout->id().c_str(),this);
    else {
	// Don't terminate if the element is stream error or stream end:
	//  stream is already terminating
	bool bye = xml->type() != XMLElement::StreamError &&
	    xml->type() != XMLElement::StreamEnd;
	Debug(m_engine,DebugNote,"Stream. Failed to send (%p,%s) in state=%s [%p]",
	    xml,xml->name(),lookupState(state()),this);
	if (eout->id()) {
	    JBEvent* ev = new JBEvent(JBEvent::WriteFail,this,
		eout->release(),&(eout->id()));
	    m_events.append(ev);
	}
	if (bye)
	    terminate(false,0,XMPPError::HostGone,"Failed to send data",false);
    }
    if (eout != m_streamXML)
	m_outXML.remove(eout,true);
    else
	TelEngine::destruct(m_streamXML);
    return ret;
}

// Remove:
//   Pending elements with id if id is not 0
//   All elements without id if id is 0
void JBStream::removePending(bool notify, const String* id, bool force)
{
    ListIterator iter(m_outXML);
    bool first = true;
    for (GenObject* o = 0; (o = iter.get());) {
	XMLElementOut* eout = static_cast<XMLElementOut*>(o);
	// Check if the first element will be removed if partially sent
	if (first) {
	    first = false;
	    if (eout->dataCount() && !force)
		continue;
	}
	if (id) {
	    if (*id != eout->id())
		continue;
	}
	else if (eout->id())
	    continue;
	if (notify)
	    m_events.append(new JBEvent(JBEvent::WriteFail,this,eout->release(),id));
	m_outXML.remove(eout,true);
    }
}

// Called when a setup state was completed
// Set/reset some stream flags and data
void JBStream::resetStream()
{
    // TLS: RFC 3920
    // SASL: RFC 3920 Section 7 page 38
    switch (state()) {
	case Securing:
	    m_flags |= StreamSecured;
	    m_id = "";
	    break;
	case Auth:
	    m_flags |= StreamAuthenticated;
	    if (flag(UseSasl))
		m_id = "";
	    break;
	case Destroy:
	case Idle:
	    m_flags &= ~(StreamAuthenticated | StreamSecured);
	    m_challengeCount = 2;
	    m_id = "";
	    break;
	default:
	    break;
    }
    m_flags &= ~NoRemoteVersion1;
    m_nonce = "";
    m_cnonce = "";
    m_realm = "";
}

// Set receive count
void JBStream::setRecvCount(int value)
{
    Lock lock(m_socket.m_receiveMutex);
    if (m_recvCount == value)
	return;
    DDebug(m_engine,DebugInfo,"Stream. recvCount changed from %d to %d [%p]",
	m_recvCount,value,this);
    m_recvCount = value;
}


/**
 * JBComponentStream
 */

JBComponentStream::JBComponentStream(JBEngine* engine, XMPPServerInfo& info,
	const JabberID& localJid, const JabberID& remoteJid)
    : JBStream(engine,JBEngine::Component,info,localJid,remoteJid)
{
    // Doesn't use SASL auth: just using this structure to set auth mechanism
    JIDFeatureSasl* sasl = new JIDFeatureSasl(JIDFeatureSasl::MechMD5 |
	JIDFeatureSasl::MechSHA1);
    m_remoteFeatures.add(sasl);
}

// Create stream start element
XMLElement* JBComponentStream::getStreamStart()
{
    XMLElement* start = XMPPUtils::createElement(XMLElement::StreamStart,
	XMPPNamespace::ComponentAccept);
    start->setAttribute("xmlns:stream",s_ns[XMPPNamespace::Stream]);
    start->setAttribute("to",local());
    return start;
}

// Get the authentication element to be sent when authentication starts
XMLElement* JBComponentStream::getAuthStart()
{
    setClientAuthMechanism();
    if (m_authMech == JIDFeatureSasl::MechSHA1) {
	SHA1 auth;
	auth << id() << m_password;
	return new XMLElement(XMLElement::Handshake,0,auth.hexDigest());
    }
    else if (m_authMech == JIDFeatureSasl::MechPlain)
	return new XMLElement(XMLElement::Handshake,0,m_password);
    return 0;
}

// Process a received element in Started state
void JBComponentStream::processStarted(XMLElement* xml)
{
    // Expect stream start tag
    setRecvCount(-1);
    if (xml->type() != XMLElement::StreamStart)
	DROP_AND_EXIT
    // Check namespaces
    if (!(xml->hasAttribute("xmlns:stream",s_ns[XMPPNamespace::Stream]) &&
	XMPPUtils::hasXmlns(*xml,XMPPNamespace::ComponentAccept)))
	INVALIDXML_AND_EXIT(XMPPError::InvalidNamespace,0);
    // Check the from attribute
    if (!engine()->checkComponentFrom(this,xml->getAttribute("from")))
	INVALIDXML_AND_EXIT(XMPPError::HostUnknown,0);
    TelEngine::destruct(xml);
    startAuth();
}

// Process a received element in Auth state
void JBComponentStream::processAuth(XMLElement* xml)
{
    setRecvCount(-1);
    if (xml->type() != XMLElement::Handshake)
	DROP_AND_EXIT
    TelEngine::destruct(xml);
    changeState(Running);
}


/**
 * JBClientStream
 */

// Outgoing
JBClientStream::JBClientStream(JBEngine* engine, XMPPServerInfo& info,
	const JabberID& localJid, const NamedList& params)
    : JBStream(engine,JBEngine::Client,info,localJid,JabberID(0,localJid.domain(),0))
{
    m_roster = new XMPPUserRoster(0,localJid.node(),localJid.domain());
    m_resource = new JIDResource(local().resource(),JIDResource::Available,
	JIDResource::CapChat|JIDResource::CapAudio);
}

// Destructor
JBClientStream::~JBClientStream()
{
    TelEngine::destruct(m_roster);
    TelEngine::destruct(m_resource);
}

// Get a remote user from roster
XMPPUser* JBClientStream::getRemote(const JabberID& jid)
{
    return m_roster->getUser(jid,false);
}

// Send a stanza
JBStream::Error JBClientStream::sendStanza(XMLElement* stanza, const char* senderId)
{
    if (!stanza)
	return ErrorContext;

    Lock lock(streamMutex());

    // Destroy: call parent's method to put the debug message
    if (state() == Destroy)
	return JBStream::sendStanza(stanza,senderId);

    // Check 'from' attribute
    const char* from = stanza->getAttribute("from");
    if (from && *from) {
	JabberID jid(from);
	if (!local().match(jid)) {
	    Debug(engine(),DebugNote,
		"Stream. Can't send stanza (%p,%s) with invalid from=%s [%p]",
		stanza,stanza->name(),from,this);
	    TelEngine::destruct(stanza);
	    return ErrorContext;
	}
    }

#if 0
    // TODO: Uncomment and implement. We'll need only 'subscribed' and 'unsubscribed'
    //       elements
    if (stanza->type() == XMLElement::Presence) {
	// Ignore if the presence is not sent to an actual user (node)
	JabberID to(stanza->getAttribute("to"));
	if (to.node()) {
	    Lock lock(m_roster);
	    // TODO: Update subscriptions for users in roster
	}
    }
#endif

    return JBStream::sendStanza(stanza,senderId);
}

// Stream is running: get roster from server
void JBClientStream::streamRunning()
{
    XDebug(engine(),DebugAll,"JBClientStream::streamRunning() [%p]",this);
    if (!m_rosterReqId.null())
	return;
    m_roster->cleanup();
    m_rosterReqId = "roster-query";
    XMLElement* xml = XMPPUtils::createIq(XMPPUtils::IqGet,0,0,m_rosterReqId);
    xml->addChild(XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::Roster));
    sendStanza(xml);
}

// Process received data while running
void JBClientStream::processRunning(XMLElement* xml)
{
    XDebug(engine(),DebugAll,"JBClientStream::processRunning('%s') [%p]",xml->name(),this);

    JBStream::processRunning(xml);

    // Check last event for post processing
    JBEvent* event = lastEvent();
    if (!event)
	return;
    bool sendPres = true;
    switch (event->type()) {
	case JBEvent::Presence:
	    break;
	case JBEvent::IqRosterSet:
	    // Send response and fall through to process it
	    sendStanza(XMPPUtils::createIq(XMPPUtils::IqResult,event->to(),
		event->from(),event->id()));
	case JBEvent::IqRosterRes:
	case JBEvent::IqRosterErr:
	    if (m_rosterReqId == event->id()) {
		// Cleanup roster only if received result or error
		m_rosterReqId = "";
		if (event->type() == JBEvent::IqRosterSet) {
		    sendPres = false;
		    break;
		}
		m_roster->cleanup();
		if (event->type() == JBEvent::IqRosterRes)
		    break;
		// Error
		Debug(engine(),DebugNote,"Stream. Received error '%s' on roster request [%p]",
		    event->text().c_str(),this);
		String err, txt;
		XMPPUtils::decodeError(event->element(),err,txt);
		m_events.remove(event,true);
		String tmp;
		tmp << "Unable to get roster from server";
		if (err)
		    tmp << " error=" << err;
		if (txt)
		    tmp << " reason=" << txt;
		TelEngine::destruct(event);
		terminate(false,0,XMPPError::NoError,tmp,false);
	    }
	    return;
	case JBEvent::IqDiscoInfoGet:
	    sendStanza(m_roster->createDiscoInfoResult(event->to(),event->from(),event->id()));
	    m_events.remove(event,true);
	    return;
	case JBEvent::IqDiscoItemsGet:
	case JBEvent::IqDiscoInfoSet:
	case JBEvent::IqDiscoItemsSet:
	    sendStanza(event->createError(XMPPError::TypeCancel,XMPPError::SFeatureNotImpl));
	    m_events.remove(event,true);
	    return;
	case JBEvent::IqDiscoInfoRes:
	case JBEvent::IqDiscoInfoErr:
	case JBEvent::IqDiscoItemsRes:
	case JBEvent::IqDiscoItemsErr:
	    dropXML(event->releaseXML());
	    m_events.remove(event,true);
	    return;
	default:
	    return;
    }

    // Presence: update roster and let the event to be processed by a service
    // TODO: Presence None and Unavailable: check if we already know it
    //       If so, remove event to avoid sending too many massages
    //       or
    //       Don't do that: someone might rely on those presences (for timeout purposes?)
    if (event->type() == JBEvent::Presence) {
	JBPresence::Presence pres = JBPresence::presenceType(event->stanzaType());
	XMPPUser* user = getRemote(event->from());
	bool error = false;
	switch (pres) {
	    case JBPresence::None:
	    case JBPresence::Unavailable:
		if (user)
		    user->processPresence(event,pres == JBPresence::None);
		else
		    error = true;
		break;
	    case JBPresence::Subscribed:
	    case JBPresence::Unsubscribed:
		if (user)
		    user->processSubscribe(event,pres);
		else
		    error = true;
	    	break;
	    case JBPresence::Subscribe:
	    case JBPresence::Unsubscribe:
	    case JBPresence::Error:
		break;
	    case JBPresence::Probe:
		dropXML(event->releaseXML());
		m_events.remove(event,true);
		break;
	}
	TelEngine::destruct(user);

#ifdef DEBUG
	// Don't show message if it's the same jid: it came from another resource
	if (error && !(event->to().bare() &= event->from().bare()))
	    DDebug(engine(),DebugNote,
		"Stream. Received presence=%s from=%s. User not in roster [%p]",
		event->stanzaType().c_str(),event->from().c_str(),this);
#endif
	return;
    }

    // Roster event: update and change event type
    event->m_type = JBEvent::IqClientRosterUpdate;

    // Add new resource if not added. Send initial presence
    if (sendPres) {
	XMLElement* pres = new XMLElement(XMLElement::Presence);
	m_resource->setName(local().resource());
	m_resource->addTo(pres);
	sendStanza(pres);
    }

    // Process received roster update
    XMLElement* item = event->child() ? event->child()->findFirstChild(XMLElement::Item) : 0;
    for (; item; item = event->child()->findNextChild(item,XMLElement::Item)) {
	JabberID jid = item->getAttribute("jid");
	const char* sub = item->getAttribute("subscription");
	XMPPUser::Subscription subType = (XMPPUser::Subscription)XMPPUser::subscribeType(sub);
	XMPPUser* user = m_roster->getUser(jid,false);
	bool newUser = true;
	if (!user)
	    user = new XMPPUser(m_roster,jid.node(),jid.domain(),subType,false,false);
	else {
	    newUser = false;
	    user->setSubscription(subType);
	}
	if (!user->local()) {
	    Debug(engine(),DebugStub,"Stream. Failed to update roster for jid=%s [%p]",
		jid.c_str(),this);
	    TelEngine::destruct(user);
	    continue;
	}
	Debug(engine(),DebugAll,"Stream. Updated roster jid=%s subscription=%s [%p]",
	    jid.c_str(),sub,this);
	if (!newUser)
	    TelEngine::destruct(user);
    }
}

// Check the 'to' attribute of a received element
// Accept empty or bare/full jid match. Set 'to' if empty
bool JBClientStream::checkDestination(XMLElement* xml, bool& respond)
{
    respond = false;
    if (!xml)
	return false;
    const char* to = xml->getAttribute("to");
    if (to && *to) {
	JabberID jid(to);
	return local().match(to);
    }
    xml->setAttribute("to",local());
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
