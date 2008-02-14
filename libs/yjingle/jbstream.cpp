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

using namespace TelEngine;

static XMPPNamespace s_ns;               // Just use the operators
static XMPPError s_err;

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

static String s_version = "1.0";         // Protocol version


/**
 * JBSocket
 */
// Connect the socket
bool JBSocket::connect(const SocketAddr& addr, bool& terminated)
{
    terminate();
    Lock2 lck1(m_streamMutex,m_receiveMutex);
    m_socket = new Socket(PF_INET,SOCK_STREAM);
    lck1.drop();
    terminated = false;
    bool res = m_socket->connect(addr);
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
	    Debug(m_engine,DebugWarn,
		"Stream. Failed to connect socket to '%s:%d'. %d: '%s' [%p]",
		addr.host().c_str(),addr.port(),
		m_socket->error(),::strerror(m_socket->error()),m_stream);
	    break;
	}
	// Connected
	ok = true;
	m_socket->setBlocking(false);
	DDebug(m_engine,DebugAll,"Stream. Connected to '%s:%d'. [%p]",
	    addr.host().c_str(),addr.port(),m_stream);
	break;
    }
    lck2.drop();
    if (!ok)
	terminate();
    return ok;
}

// Close the socket
void JBSocket::terminate()
{
    Lock2 lck(m_streamMutex,m_receiveMutex);
    Socket* tmp = m_socket;
    m_socket = 0;
    if (tmp) {
	tmp->setLinger(-1);
	tmp->terminate();
	delete tmp;
    }
}

// Read data from socket
bool JBSocket::recv(char* buffer, unsigned int& len)
{
    if (!valid())
	return false;

    int read = m_socket->recv(buffer,len);
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
    if (!valid())
	return false;

    XDebug(m_engine,DebugAll,"Stream sending [%p]\r\n%s",m_stream,buffer);
    int c = m_socket->send(buffer,len);
    if (c != Socket::socketError()) {
	len = c;
	return true;
    }
    if (!m_socket->canRetry()) {
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
JBStream::JBStream(JBEngine* engine,
	const JabberID& localJid, const JabberID& remoteJid,
	const String& password, const SocketAddr& address,
	bool autoRestart, unsigned int maxRestart,
	u_int64_t incRestartInterval, bool outgoing, int type)
    : m_password(password),
    m_type(type),
    m_state(Idle),
    m_outgoing(outgoing),
    m_autoRestart(autoRestart),
    m_restart(maxRestart),
    m_restartMax(maxRestart),
    m_timeToFillRestart(Time::msecNow() + incRestartInterval),
    m_fillRestartInterval(incRestartInterval),
    m_local(localJid.node(),localJid.domain(),localJid.resource()),
    m_remote(remoteJid.node(),remoteJid.domain(),remoteJid.resource()),
    m_engine(engine),
    m_socket(engine,this),
    m_address(address),
    m_lastEvent(0),
    m_terminateEvent(0),
    m_startEvent(0)
{
    if (m_type == -1 && engine)
	m_type = engine->protocol();
    DDebug(m_engine,DebugAll,"Stream created type=%s local=%s remote=%s [%p]",
	JBEngine::lookupProto(m_type),m_local.safe(),m_remote.safe(),this);
}

JBStream::~JBStream()
{
#ifdef XDEBUG
    if (m_engine && m_engine->printXml() && m_engine->debugAt(DebugAll)) {
	String buffer, element;
	while (true) {
	    XMLElement* e = m_parser.extract();
	    if (!e)
		break;
	    XMPPUtils::print(element,e);
	    delete e;
	}
	m_parser.getBuffer(buffer);
	if (buffer || element)
	    Debug(m_engine,DebugAll,
		"~Stream [%p]\r\nUnparsed data: '%s'.\r\nParsed elements: %s",
		this,buffer.c_str(),element?element.c_str():"None.");
    }
#endif
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

// Connect the stream
void JBStream::connect()
{
    Lock2 lck(m_socket.m_streamMutex,m_socket.m_receiveMutex);
    if (state() != Idle) {
	Debug(m_engine,DebugNote,
	    "Stream. Attempt to connect when not idle [%p]",this);
	return;
    }
    DDebug(m_engine,DebugInfo,
	"Stream. Attempt to connect local=%s remote=%s addr=%s:%d count=%u [%p]",
	m_local.safe(),m_remote.safe(),m_address.host().safe(),m_address.port(),
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
    if (!m_socket.connect(m_address,terminated)) {
	if (!terminated)
	    terminate(false,0,XMPPError::NoError,"connection-failed",false);
	return;
    }

    Debug(m_engine,DebugAll,"Stream. local=%s remote=%s connected to %s:%d [%p]",
	m_local.safe(),m_remote.safe(),m_address.host().safe(),m_address.port(),this);

    // Send declaration
    String declaration;
    declaration << "<?xml version='" << s_version << "' encoding='UTF-8'?>";
    if (m_engine->printXml() && m_engine->debugAt(DebugInfo))
	Debug(m_engine,DebugInfo,"Stream. Sending XML declaration %s [%p]",
	    declaration.c_str(),this);
    unsigned int len = declaration.length();
    if (!m_socket.send(declaration.c_str(),len) || len != declaration.length()) {
	terminate(false,0,XMPPError::Internal,"send declaration failed",false);
	return;
    }

    sendStreamXML(getStreamStart(),Started,false);
}

// Read data from socket and pass it to the parser
// Terminate stream on parser or socket error
bool JBStream::receive()
{
    static char buf[1024];
    if (state() == Destroy || state() == Idle || state() == Connecting)
	return false;

    XMPPError::Type error = XMPPError::NoError;
    bool send = false;
    // Lock between start read and end consume to serialize input
    m_socket.m_receiveMutex.lock();
    const char* text = 0;
    unsigned int len = sizeof(buf);
    if (m_socket.recv(buf,len)) {
	if (len && !m_parser.consume(buf,len)) {
	    error = XMPPError::Xml;
	    text = m_parser.ErrorDesc();
	    Debug(m_engine,DebugNote,"Stream. Parser error: '%s' [%p]",text,this);
	    send = true;
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
	delete stanza;
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

    // Increase stream restart counter if it's time to
    if (m_timeToFillRestart < time) {
	if (m_restart < m_restartMax) {
	    m_restart++;
	    Debug(m_engine,DebugAll,"Stream. restart count=%u max=%u [%p]",
		m_restart,m_restartMax,this);
	}
	m_timeToFillRestart = time + m_fillRestartInterval;
    }

    if (m_lastEvent)
	return 0;

    // Do nothing if destroying or connecting
    // Just check Terminated or Running events
    // Idle: check if we can restart. Destroy the stream if not auto restarting
    if (state() == Idle || state() == Destroy || state() == Connecting) {
	if (state() == Idle) {
	    if (m_restart && m_autoRestart) {
		lock.drop();
		m_engine->connect(this);
		return 0;
	    }
	    if (!m_autoRestart)
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
	if (m_engine->printXml() && m_engine->debugAt(DebugInfo)) {
	    String s;
	    XMPPUtils::print(s,xml);
	    Debug(m_engine,DebugInfo,"Stream. Received [%p]%s",this,s.c_str());
	}
	else
	    DDebug(m_engine,DebugInfo,"Stream. Received (%p,%s) [%p]",xml,xml->name(),this);

	// Check destination
	if (!checkDestination(xml)) {
	    // TODO Respond if state is Started ?
	    if (state() == Started)
		dropXML(xml);
	    else
		invalidStreamXML(xml,XMPPError::BadAddressing,"unknown destination");
	    break;
	}

	// Check if stream end was received (end tag or error)
	if (xml->type() == XMLElement::StreamEnd ||
	    xml->type() == XMLElement::StreamError) {
	    Debug(m_engine,DebugAll,"Stream. Remote closed in state %s [%p]",
		lookupState(state()),this);
	    terminate(false,xml,XMPPError::NoError,xml->getText(),true);
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
			invalidStreamXML(xml,XMPPError::InvalidId,"invalid stream id");
			break;
		    }
		}
		processStarted(xml);
		break;
	    default: 
		Debug(m_engine,DebugStub,"Unhandled stream state %u '%s' [%p]",
		    state(),lookupState(state()),this);
		delete xml;
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
    TelEngine::destruct(m_startEvent);
    if (state() == Destroy) {
	if (recvStanza)
	    delete recvStanza;
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
	}
	sendStreamXML(e,m_state,true);
    }
    m_socket.terminate();

    // Done if called from destructor
    if (final) {
	changeState(Destroy);
	if (recvStanza)
	    delete recvStanza;
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
	if (!recvStanza)
	    m_terminateEvent->m_text = reason;
	recvStanza = 0;
    }
    if (recvStanza)
	delete recvStanza;

    // Change state
    if (destroy) {
	changeState(Destroy);
	deref();
    }
    else
	changeState(Idle);
}

// Get the name of a stream state
const char* JBStream::lookupState(int state)
{
    return lookup(state,s_streamState);
}

// Process received data while running
void JBStream::processRunning(XMLElement* xml)
{
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
    String to = xml->getAttribute("from");
    String from = xml->getAttribute("to");
    String id = xml->getAttribute("id");
    XMLElement* err = XMPPUtils::createIq(XMPPUtils::IqError,from,to,id);
    err->addChild(xml);
    err->addChild(XMPPUtils::createError(XMPPError::TypeModify,error));
    sendStanza(err);
}

// Helper function to make the code simpler
inline bool checkChild(XMLElement* e, XMPPNamespace::Type ns, XMPPError::Type& error)
{
    if (!e) {
	error = XMPPError::SBadRequest;
	return false;
    }
    if (e->hasAttribute("xmlns",s_ns[ns]))
	return true;
    error = XMPPError::SFeatureNotImpl;
    return false;
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
		    if (xml->hasAttribute("xmlns",s_ns[XMPPNamespace::DiscoInfo]))
			evType = JBEvent::IqDiscoInfoErr;
		    else if (xml->hasAttribute("xmlns",s_ns[XMPPNamespace::DiscoItems]))
			evType = JBEvent::IqDiscoItemsErr;
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

// Send stream XML elements through the socket
bool JBStream::sendStreamXML(XMLElement* e, State newState, bool streamEnd)
{
    bool result = false;
    Lock lock(m_socket.m_streamMutex);
    while (e) {
	if (state() == Idle || state() == Destroy)
	    break;
	String tmp;
	e->toString(tmp,true);
	unsigned int len = tmp.length();
	result = (ErrorNone == sendXML(e,tmp.c_str(),len,true,streamEnd));
	break;
    }
    if (!result)
	Debug(m_engine,DebugNote,
	    "Stream. Failed to send stream XML (%p,%s) in state=%s [%p]",
	    e,e?e->name():"",lookupState(state()),this);
    if (e)
	delete e;
    if (result)
	changeState(newState);
    return result;
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

// Drop an unexpected or unhandled element
void JBStream::dropXML(XMLElement* xml, bool unexpected)
{
    if (!xml)
	return;
    Debug(m_engine,unexpected?DebugNote:DebugInfo,
	"Stream. Dropping %s element (%p,%s) in state %s [%p]",
	unexpected?"unexpected":"unhandled",xml,xml->name(),
	lookupState(state()),this);
    delete xml;
}

// Change stream state
void JBStream::changeState(State newState)
{
    if (m_state == newState)
	return;
    Debug(m_engine,DebugInfo,"Stream. Changing state from %s to %s [%p]",
	lookupState(m_state),lookupState(newState),this);
    m_state = newState;
    if (newState == Running && !m_startEvent)
	m_startEvent = new JBEvent(JBEvent::Running,this,0);
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

// Actually send XML elements through the socket
JBStream::Error JBStream::sendXML(XMLElement* e, const char* buffer, unsigned int& len,
	bool stream, bool streamEnd)
{
    if (!(e && buffer && len))
	return ErrorNone;

    if (!stream && state() != Running)
	return ErrorPending;

    Error ret = ErrorNone;
    XMPPError::Type error = XMPPError::NoError;

    while (true) {
	// Try to send any partial data remaining from the last sent stanza
        // Don't send stream element on failure: remote XML parser will fail anyway
	if (stream) {
	    ObjList* obj = m_outXML.skipNull();
	    XMLElementOut* eout = obj ? static_cast<XMLElementOut*>(obj->get()) : 0;
	    if (eout && eout->dataCount())
		ret = sendPending();
	    if (ret != ErrorNone) {
		if (!streamEnd)
		    error = XMPPError::UndefinedCondition;
		// Ignore partial data sent error: terminate stream
		ret = ErrorNoSocket;
		break;
	    }
	}

	if (m_engine->printXml() && m_engine->debugAt(DebugInfo)) {
	    String s;
	    XMPPUtils::print(s,e);
	    Debug(m_engine,DebugInfo,"Stream. Sending [%p]%s",this,s.c_str());
	}
	else
	    DDebug(m_engine,DebugInfo,"Stream. Sending (%p,%s) [%p]",e,e->name(),this);

	unsigned int tmp = len;
	if (m_socket.send(buffer,len)) {
	    if (len != tmp) {
		ret = !stream ? ErrorPending : ErrorNoSocket;
		error = XMPPError::Internal;
	    }
	    break;
	}
	ret = ErrorNoSocket;
	error = XMPPError::HostGone;
	break;
    }

    if (stream && ret != ErrorNone && !streamEnd)
	terminate(false,0,error,0,false);
    return ret;
}

// Try to send the first element in pending outgoing stanzas list
// Terminate stream on socket error
JBStream::Error JBStream::sendPending()
{
    ObjList* obj = m_outXML.skipNull();
    XMLElementOut* eout = obj ? static_cast<XMLElementOut*>(obj->get()) : 0;

    if (!eout)
	return ErrorNone;
    if (!eout->element()) {
	m_outXML.remove(eout,true);
	return ErrorNone;
    }
    if (state() != Running)
	return ErrorPending;

    u_int32_t len;
    const char* data = eout->getData(len);
    Error ret = sendXML(eout->element(),data,len);
    bool notify = false;
    switch (ret) {
	case ErrorNoSocket:
	    notify = true;
	case ErrorNone:
	    break;
	case ErrorPending:
	    eout->dataSent(len);
	    return ErrorPending;
	default: ;
	    ret = ErrorContext;
    }

#ifdef DEBUG
    if (eout->element())
	DDebug(m_engine,notify?DebugNote:DebugAll,
	    "Stream. Remove pending stanza (%p,%s) with id='%s'%s [%p]",
	    eout->element(),eout->element()->name(),eout->id().c_str(),
	    notify?". Failed to send":"",this);
#endif

    if (notify) {
	if (eout->id()) {
	    JBEvent* ev = new JBEvent(JBEvent::WriteFail,this,
		eout->release(),&(eout->id()));
	    m_events.append(ev);
	}
	terminate(false,0,XMPPError::HostGone,0,false);
    }
    m_outXML.remove(eout,true);
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


/**
 * JBComponentStream
 */
JBComponentStream::JBComponentStream(JBEngine* engine,
	const JabberID& localJid, const JabberID& remoteJid,
	const String& password, const SocketAddr& address,
	bool autoRestart, unsigned int maxRestart,
	u_int64_t incRestartInterval, bool outgoing)
    : JBStream(engine,localJid,remoteJid,password,address,
	autoRestart,maxRestart,incRestartInterval,outgoing,JBEngine::Component)
{
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

// Process a received element in Started state
void JBComponentStream::processStarted(XMLElement* xml)
{
    // Expect stream start tag
    // Check if received element other then 'stream'
    if (xml->type() != XMLElement::StreamStart) {
	dropXML(xml);
	return;
    }
    // Check attributes: namespaces, from
    if (!(xml->hasAttribute("xmlns:stream",s_ns[XMPPNamespace::Stream]) &&
	xml->hasAttribute("xmlns",s_ns[XMPPNamespace::ComponentAccept]))) {
	invalidStreamXML(xml,XMPPError::InvalidNamespace,0);
	return;
    }
    // Check the from attribute
    if (!engine()->checkComponentFrom(this,xml->getAttribute("from"))) {
	invalidStreamXML(xml,XMPPError::HostUnknown,0);
	return;
    }
    delete xml;

    // Send auth
    String handshake;
    JIDFeatureSasl* sasl = static_cast<JIDFeatureSasl*>(
	m_remoteFeatures.get(XMPPNamespace::Sasl));
    if (sasl)
	if (sasl->mechanism(JIDFeatureSasl::MechSHA1)) {
	    SHA1 auth;
	    auth << id() << m_password;
	    handshake = auth.hexDigest();
	}
	else if (sasl->mechanism(JIDFeatureSasl::MechMD5)) {
	    MD5 auth;
	    auth << id() << m_password;
	    handshake = auth.hexDigest();
	}
    xml = new XMLElement(XMLElement::Handshake,0,handshake);
    sendStreamXML(xml,Auth,false);
}

// Process a received element in Auth state
void JBComponentStream::processAuth(XMLElement* xml)
{
    // Expect handshake
    if (xml->type() != XMLElement::Handshake) {
	dropXML(xml);
	return;
    }
    delete xml;
    changeState(Running);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
