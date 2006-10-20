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

static XMPPNamespace s_ns;
static XMPPError s_err;

static const char* s_declaration = "<?xml version='1.0' encoding='UTF-8'?>";

// Just a shorter code
inline XMLElement* errorHostGone()
{
    return XMPPUtils::createStreamError(XMPPError::HostGone);
}

/**
 * JBComponentStream
 */
JBComponentStream::JBComponentStream(JBEngine* engine, const String& remoteName,
	const SocketAddr& remoteAddr)
    : Mutex(true),
      m_state(Terminated),
      m_remoteName(remoteName),
      m_remoteAddr(remoteAddr),
      m_engine(engine),
      m_socket(0),
      m_receiveMutex(true),
      m_lastEvent(0),
      m_terminateEvent(0),
      m_partialRestart(-1),
      m_totalRestart(-1),
      m_waitBeforeConnect(false)
{
    Debug(m_engine,DebugAll,"JBComponentStream. [%p]",this);
    if (!engine)
	return;
    // Init data
    m_partialRestart = m_engine->partialStreamRestartAttempts();
    m_totalRestart = m_engine->totalStreamRestartAttempts();
    m_engine->getServerIdentity(m_localName,remoteName);
    // Start
    connect();
}

JBComponentStream::~JBComponentStream()
{
    Debug(m_engine,DebugAll,"~JBComponentStream. [%p]",this);
    if (m_engine->debugAt(DebugAll)) {
	String buffer, element;
	for (; true; ) {
	    XMLElement* e = m_parser.extract();
	    if (!e)
		break;
	    XMPPUtils::print(element,e);
	    delete e;
	}
	m_parser.getBuffer(buffer);
	Debug(m_engine,DebugAll,
	    "Stream. Incoming data:[%p]\r\nParser buffer: '%s'.\r\nParsed elements: %s",
	    this,buffer.c_str(),element?element.c_str():"None.");
    }
    cleanup(false,0);
    m_engine->removeStream(this,false);
}

void JBComponentStream::connect()
{
    Lock2 lock(*this,m_receiveMutex);
    if (m_state != Terminated)
	return;
    m_state = WaitToConnect;
    // Check restart counters: If both of them are 0 destroy the stream
    Debug(m_engine,DebugAll,
	"Stream::connect. Remaining attempts: Partial: %d. Total: %d. [%p]",
	m_partialRestart,m_totalRestart,this);
    if (!(m_partialRestart && m_totalRestart)) {
	terminate(true,false,0,false);
	return;
    }
    // Update restart counters
    if (m_partialRestart > 0)
	m_partialRestart--;
    m_waitBeforeConnect = (m_partialRestart == 0);
    // Reset data
    m_id = "";
    m_parser.reset();
    // Re-create socket
    m_socket = new Socket(PF_INET,SOCK_STREAM);
    lock.drop();
    // Connect
    bool res = m_socket->connect(m_remoteAddr);
    // Lock again to update stream
    lock.lock(*this,m_receiveMutex);
    if (!res) {
	Debug(m_engine,DebugWarn,
	    "Stream::connect. Failed to connect socket to '%s:%d'. Error: '%s' (%d). [%p]",
	    m_remoteAddr.host().c_str(),m_remoteAddr.port(),
	    ::strerror(m_socket->error()),m_socket->error(),this);
	terminate(false,false,0,false);
	return;
    }
    Debug(m_engine,DebugAll,"Stream::connect. Connected to '%s:%d'. [%p]",
	m_remoteAddr.host().c_str(),m_remoteAddr.port(),this);
    // Update restart data
    if (m_partialRestart != -1)
	m_partialRestart = m_engine->partialStreamRestartAttempts();
    if (m_totalRestart > 0)
	m_totalRestart--;
    m_waitBeforeConnect = false;
    // Connected
    m_socket->setBlocking(false);
    lock.drop();
    // Start
    DDebug(m_engine,DebugAll,"Stream::connect. Starting stream. [%p]",this);
    XMLElement* start = XMPPUtils::createElement(XMLElement::StreamStart,
	XMPPNamespace::ComponentAccept);
    start->setAttribute("xmlns:stream",s_ns[XMPPNamespace::Stream]);
    start->setAttribute("to",m_localName);
    m_state = Started;
    sendStreamXML(start,Started);
}

void JBComponentStream::terminate(bool destroy, bool sendEnd,
	XMLElement* error, bool sendError)
{
    Lock2 lock(*this,m_receiveMutex);
    if (m_state == Destroy || m_state == Terminated)
	return;
    // Error is sent only if end stream is sent
    XMLElement* eventError = 0;
    if (sendEnd && sendError) {
	//TODO: Make a copy of error element to be attached to the event
    }
    else {
	eventError = error;
	error = 0;
    }
    cleanup(sendEnd,error);
    // Add event. Change state
    if (destroy) {
	addEvent(JBEvent::Destroy,eventError);
	m_state = Destroy;
	deref();
    }
    else {
	addEvent(JBEvent::Terminated,eventError);
	m_state = Destroy;
    }
    Debug(m_engine,DebugAll,"Stream. %s. [%p]",destroy?"Destroy":"Terminate",this);
}

bool JBComponentStream::receive()
{
    char buf[1024];
    if (m_state == Destroy || m_state == Terminated ||
	m_state == WaitToConnect)
	return false;
    u_int32_t len = sizeof(buf);
    // Lock between start read and end consume to serialize input
    m_receiveMutex.lock();
    bool read = (readSocket(buf,len) && len);
    // Parse if received any data and no error
    if (read && !m_parser.consume(buf,len)) {
	Debug(m_engine,DebugNote,
	    "Stream::receive. Error parsing data: '%s'. [%p]",
	    m_parser.ErrorDesc(),this);
	XDebug(m_engine,DebugAll,"Parser buffer: %s",buf);
	XMLElement* e = XMPPUtils::createStreamError(XMPPError::Xml,m_parser.ErrorDesc());
	terminate(false,true,e,true);
    }
    m_receiveMutex.unlock();
    return len != 0;
}

JBComponentStream::Error JBComponentStream::sendStanza(XMLElement* stanza,
	const char* senderId)
{
    if (!stanza)
	return ErrorContext;
    DDebug(m_engine,DebugAll,"Stream::sendStanza((%p): '%s'). Sender id: '%s'. [%p]",
	stanza,stanza->name(),senderId,this);
    XMLElementOut* e = new XMLElementOut(stanza,senderId);
    return postXML(e);
}

JBEvent* JBComponentStream::getEvent(u_int64_t time)
{
    Lock lock(this);
    for (;;) {
	if (m_lastEvent || m_terminateEvent ||
	    m_state == Destroy || m_state == Terminated) {
	    if (m_lastEvent)
		return 0;
	    if (m_terminateEvent) {
		m_lastEvent = m_terminateEvent;
		m_terminateEvent = 0;
	    }
	    return m_lastEvent;
	}
	// Send pending elements.
	// If not terminated check received elements
	// Again, if not terminated, get event from queue
	sendXML();
	if (m_terminateEvent)
	    continue;
	processIncomingXML();
	if (m_terminateEvent)
	    continue;
	// Get first event from queue
	ObjList* obj = m_events.skipNull();
	if (!obj)
	    break;
	m_lastEvent = static_cast<JBEvent*>(obj->get());
	m_events.remove(m_lastEvent,false);
	DDebug(m_engine,DebugAll,
	    "Stream::getEvent. Raise event (%p): %u. [%p]",
	    m_lastEvent,m_lastEvent->type(),this);
	return m_lastEvent;
    }
    //TODO: Keep alive ?
    return 0;
}

void JBComponentStream::cancelPending(bool raise, const String* id)
{
    Lock lock(this);
    // Cancel elements with id. Raise event if requested
    // Don't cancel the first element if partial data was sent:
    //   The remote parser will fail
    if (id && *id) {
	ListIterator iter(m_outXML);
	GenObject* obj;
	bool first = true;
	for (; (obj = iter.get());) {
	    XMLElementOut* e = static_cast<XMLElementOut*>(obj);
	    if (first) {
		first = false;
		if (e->dataCount())
		    continue;
	    }
	    if (!e->id() || *id != e->id())
		continue;
	    if (raise)
		addEventNotify(JBEvent::WriteFail,e);
	    else
		m_outXML.remove(e,true);
	}
	return;
    }
    // Cancel all pending elements without id
    ListIterator iter(m_outXML);
    GenObject* obj;
    for (; (obj = iter.get());) {
	XMLElementOut* e = static_cast<XMLElementOut*>(obj);
	if (!e->id())
	    m_outXML.remove(e,true);
    }
}

void JBComponentStream::eventTerminated(const JBEvent* event)
{
    if (event && event == m_lastEvent) {
	m_lastEvent = 0;
	DDebug(m_engine,DebugAll,
	    "Stream::eventTerminated. Event: (%p): %u. [%p]",
	    event,event->type(),this);
    }
}

void JBComponentStream::cleanup(bool endStream, XMLElement* e)
{
    if (!m_socket) {
	if (e)
	    delete e;
	return;
    }
    bool partialData = false;
    // Remove first element from queue if partial data was sent
    ObjList* obj = m_outXML.skipNull();
    XMLElementOut* first = obj ? static_cast<XMLElementOut*>(obj->get()) : 0;
    if (first && first->dataCount()) {
	addEventNotify(JBEvent::WriteFail,first);
	partialData = true;
    }
    // Send stream terminate
    //   No need to do that if partial data was sent:
    //   the remote XML parser will fail anyway
    if (!partialData && endStream) {
	sendStreamXML(new XMLElement(XMLElement::StreamEnd),m_state,e);
	e = 0;
    }
    if (e)
	delete e;
    // Cancel outgoing elements without id
    cancelPending(false,0);
    // Destroy socket. Close in background
    m_socket->setLinger(-1);
    m_socket->terminate();
    delete m_socket;
    m_socket = 0;
}

JBComponentStream::Error JBComponentStream::postXML(XMLElementOut* element)
{
    Lock lock(this);
    if (!element)
	return ErrorNone;
    if (state() == Destroy) {
	element->deref();
	return ErrorContext;
    }
    DDebug(m_engine,DebugAll,"Stream::postXML((%p): '%s'). [%p]",
	element->element(),element->element()->name(),this);
    // List not empty: the return value will be ErrorPending
    // Else: element will be sent
    bool pending = (0 != m_outXML.skipNull());
    m_outXML.append(element);
    // Send first element
    Error result = sendXML();
    return pending ? ErrorPending : result;
}

JBComponentStream::Error JBComponentStream::sendXML()
{
    // Get the first element from list
    ObjList* obj = m_outXML.skipNull();
    XMLElementOut* e = obj ? static_cast<XMLElementOut*>(obj->get()) : 0;
    if (!e)
	return ErrorNone;
    if (state() != Running)
	return ErrorPending;
    if (m_engine->debugAt(DebugAll)) {
	String eStr;
	XMPPUtils::print(eStr,e->element());
	Debug(m_engine,DebugAll,"Stream::sendXML(%p). [%p]%s",
	    e->element(),this,eStr.c_str());
    }
    else
	Debug(m_engine,DebugAll,"Stream::sendXML((%p): '%s'). [%p]",
	    e->element(),e->element()->name(),this);
    // Prepare & send
    u_int32_t len;
    const char* data = e->getData(len);
    if (!writeSocket(data,len)) {
	// Write failed. Try to raise event. Remove from list
	addEventNotify(JBEvent::WriteFail,e);
	return ErrorNoSocket;
    }
    e->dataSent(len);
    // Partial data sent ?
    if (e->dataCount())
	return ErrorPending;
    // All data was sent. Remove
    m_outXML.remove(e,true);
    return ErrorNone;
}

bool JBComponentStream::sendStreamXML(XMLElement* element, State newState,
	XMLElement* before)
{
    if (!element) {
	if (before)
	    delete before;
	return false;
    }
    if (m_engine->debugAt(DebugAll)) {
	String eStr;
	if (before)
	    XMPPUtils::print(eStr,before);
	XMPPUtils::print(eStr,element);
	Debug(m_engine,DebugAll,"Stream::sendStreamXML. [%p]%s",
	    this,eStr.c_str());
    }
    else
	Debug(m_engine,DebugAll,"Stream::sendStreamXML('%s'). [%p]",
	    element->name(),this);
    String tmp, buff;
    switch (element->type()) {
	case XMLElement::StreamStart:
	    // Send declaration and the start tag
	    element->toString(buff,true);
	    tmp << s_declaration << buff;
	    break;
	case XMLElement::StreamEnd:
	    // Send 'before' and the end tag
	    if (before)
		before->toString(tmp);
	    element->toString(buff,true);
	    tmp += buff;
	    break;
	default:
	    element->toString(tmp,false);
    }
    delete element;
    if (before)
	delete before;
    u_int32_t len = tmp.length();
    bool result = (writeSocket(tmp,len) && len == tmp.length());
    if (result)
	m_state = newState;
    else
	terminate(false);
    return result;
}

JBComponentStream::Error JBComponentStream::sendIqError(XMLElement* stanza,
	XMPPError::ErrorType eType, XMPPError::Type eCond, const char* eText)
{
    if (!stanza)
	return ErrorContext;
    String to = stanza->getAttribute("from");
    String from = stanza->getAttribute("to");
    String id = stanza->getAttribute("id");
    // Create 'iq' and add stanza
    XMLElement* xml = XMPPUtils::createIq(XMPPUtils::IqError,from,to,id);
    xml->addChild(stanza);
    // Add 'error'
    xml->addChild(XMPPUtils::createError(eType,eCond,eText));
    return sendStanza(xml);
}

bool JBComponentStream::processIncomingXML()
{
    if (state() == Destroy || state() == Terminated
	|| state() == WaitToConnect)
	return false;
    for (bool noEvent = true; noEvent;) {
	XMLElement* element = m_parser.extract();
	if (!element)
	    return false;
	if (m_engine->debugAt(DebugAll)) {
	    String eStr;
	    XMPPUtils::print(eStr,element);
	    Debug(m_engine,DebugAll,"Stream::processIncomingXML(%p) [%p]. %s",
	        element,this,eStr.c_str());
	}
	else
	    Debug(m_engine,DebugAll,"Stream::processIncomingXML((%p): '%s'). [%p].",
		element,element->name(),this);
	// Check if we received a stream end or stream error
	if (isStreamEnd(element))
	    break;
	// Process received element
	switch (state()) {
	    case Running:
		noEvent = !processStateRunning(element);
		break;
	    case Started:
		noEvent = !processStateStarted(element);
		break;
	    case Auth:
		noEvent = !processStateAuth(element);
		break;
	    default:
		delete element;
	}
    }
    return true;
}

bool JBComponentStream::processStateStarted(XMLElement* e)
{
    XDebug(m_engine,DebugAll,"Stream::processStateStarted(%p) [%p].",e,this);
    // Expect stream start tag
    // Check if received element other then 'stream'
    if (e->type() != XMLElement::StreamStart)
	return unexpectedElement(e);
    // Check attributes: namespaces, from, id
    if (!e->hasAttribute("xmlns:stream",s_ns[XMPPNamespace::Stream]))
	return invalidElement(e,XMPPError::InvalidNamespace);
    if (!e->hasAttribute("xmlns",s_ns[XMPPNamespace::ComponentAccept]))
	return invalidElement(e,XMPPError::InvalidNamespace);
    if (!e->hasAttribute("from",m_localName))
	return invalidElement(e,XMPPError::HostUnknown);
    m_id = e->getAttribute("id");
    if (!m_id.length() || m_engine->remoteIdExists(this))
	return invalidElement(e,XMPPError::InvalidId);
    // Everything is OK: Reply
    delete e;
    // Get password from engine. Destroy if not accepted
    if (!m_engine->acceptOutgoing(m_remoteAddr.host(),m_password)) {
	Debug(m_engine,DebugNote,
	    "Stream::processStateStarted(%p). Not accepted. [%p]",e,this);
	terminate(true,true,XMPPUtils::createStreamError(XMPPError::NotAuth),
	    true);
	return true;
    }
    // Send auth
    Debug(m_engine,DebugAll,
	"Stream::processStateStarted(%p). Accepted. Send auth. [%p]",e,this);
    String handshake;
    m_engine->createSHA1(handshake,m_id,m_password);
    XMLElement* xml = new XMLElement(XMLElement::Handshake,0,handshake);
    if (!sendStreamXML(xml,Auth))
	return true;
    m_state = Auth;
    return false;
}

bool JBComponentStream::processStateAuth(XMLElement* e)
{
    XDebug(m_engine,DebugAll,"Stream::processStateAuth(%p). [%p]",e,this);
    // Expect handshake
    if (e->type() != XMLElement::Handshake)
	return unexpectedElement(e);
    delete e;
    Debug(m_engine,DebugAll,
	"Stream::processStateAuth(%p). Authenticated. [%p]",e,this);
    m_state = Running;
    return false;
}

bool JBComponentStream::processStateRunning(XMLElement* e)
{
    XDebug(m_engine,DebugAll,"Stream::processStateRunning(%p) [%p].",e,this);
    switch (e->type()) {
	case XMLElement::Iq:
	    return processIncomingIq(e);
	case XMLElement::Presence:
	case XMLElement::Message:
	    {
		JBEvent::Type evType;
		if (e->type() == XMLElement::Presence)
		    evType = JBEvent::Presence;
		else
		    evType = JBEvent::Message;
		// Create event
		JBEvent* event = addEvent(evType,e);
		event->m_stanzaType = e->getAttribute("type");
		event->m_from = e->getAttribute("from");
		event->m_to = e->getAttribute("to");
		event->m_id = e->getAttribute("id");
	    }
	    return true;
	default: ;
    }
    addEvent(JBEvent::Unhandled,e);
    return true;
}

bool JBComponentStream::processIncomingIq(XMLElement* e)
{
    // Get iq type : set/get, error, result
    //   result:  MAY have a first child with a response
    //   set/get: MUST have a first child
    //   error:   MAY have a first child with the sent stanza
    //            MUST have an 'error' child
    // Check type and the first child's namespace
    DDebug(m_engine,DebugAll,"Stream::processIncomingIq(%p). [%p]",e,this);
    XMPPUtils::IqType iq = XMPPUtils::iqType(e->getAttribute("type"));
    JBEvent* event = 0;
    // Get first child
    XMLElement* child = e->findFirstChild();
    // Create event
    switch (iq) {
	case XMPPUtils::IqResult:
	    // No child: This is a confirmation to a sent stanza
	    if (!child) {
		event = addEvent(JBEvent::IqResult,e);
		break;
	    }
	    // Child non 0: Fall through to check the child
	case XMPPUtils::IqSet:
	case XMPPUtils::IqGet:
	    // Jingle ?
	    if (child->type() == XMLElement::Jingle) {
		// Jingle stanza's type is never 'result'
		if (iq == XMPPUtils::IqResult) {
		    sendIqError(e,XMPPError::TypeModify,XMPPError::SBadRequest);
		    return false;
		}
		// Check namespace
		if (!child->hasAttribute("xmlns",s_ns[XMPPNamespace::Jingle])) {
		    sendIqError(e,XMPPError::TypeModify,XMPPError::SFeatureNotImpl);
		    return false;
		}
		// Add event
		if (iq == XMPPUtils::IqSet)
		    event = addEvent(JBEvent::IqJingleSet,e,child);
		else
		    event = addEvent(JBEvent::IqJingleGet,e,child);
		break;	
	    }
	    // Query ?
	    if (child->type() == XMLElement::Query) {
		// Check namespace
		if (!(child->hasAttribute("xmlns",s_ns[XMPPNamespace::DiscoInfo]) || 
		      child->hasAttribute("xmlns",s_ns[XMPPNamespace::DiscoItems]))) {
		    // Send error
		    sendIqError(e,XMPPError::TypeModify,XMPPError::SFeatureNotImpl);
		    return false;
		}
		// Add event
		switch (iq) {
		     case XMPPUtils::IqGet:
			event = addEvent(JBEvent::IqDiscoGet,e,child);
			break;
		     case XMPPUtils::IqSet:
			event = addEvent(JBEvent::IqDiscoSet,e,child);
			break;
		     case XMPPUtils::IqResult:
			event = addEvent(JBEvent::IqDiscoRes,e,child);
			break;
		     default: ;
		}	
		break;
	    }
	    // Unknown child
	    event = addEvent(JBEvent::Iq,e,child);
	    break;
	case XMPPUtils::IqError:
	    // First child may be a sent stanza
	    if (child && child->type() != XMLElement::Error)
		child = e->findNextChild(child);
	    // Check child type
	    if (!(child && child->type() == XMLElement::Error))
		child = 0;
	    event = addEvent(JBEvent::IqError,e,child);
	    break;
	default:
	    event = addEvent(JBEvent::Iq,e,child);
    }
    // Set event data from type, from, to and id attributes
    event->m_stanzaType = e->getAttribute("type");
    event->m_from = e->getAttribute("from");
    event->m_to = e->getAttribute("to");
    event->m_id = e->getAttribute("id");
    return true;
}

JBEvent* JBComponentStream::addEvent(JBEvent::Type type,
	XMLElement* element, XMLElement* child)
{
    Lock2 lock(*this,m_receiveMutex);
    JBEvent* ev = new JBEvent(type,this,element,child);
    Debug(m_engine,DebugAll,"Stream::addEvent((%p): %u). [%p]",ev,ev->type(),this);
    // Append event
    // If we already have a terminated event, ignore the new one
    if (type == JBEvent::Destroy || type == JBEvent::Terminated) {
	if (m_terminateEvent) {
	    Debug(m_engine,DebugAll,
		"Stream::addEvent. Ignoring terminating event ((%p): %u). Already set. [%p]",
		ev,ev->type(),this);
	    delete ev;
	}
	else
	    m_terminateEvent = ev;
	return 0;
    }
    m_events.append(ev);
    return ev;
}

bool JBComponentStream::addEventNotify(JBEvent::Type type,
	XMLElementOut* element)
{
    Lock lock(this);
    XMLElement* e = 0;
    bool raise = (element->id());
    if (raise) {
	e = element->release();
	JBEvent* ev = new JBEvent(type,this,e,&(element->id()));
	Debug(m_engine,DebugAll,
	    "Stream::addEventNotify((%p): %u). [%p]",ev,ev->type(),this);
	m_events.append(ev);
    }
    else
	e = element->element();
    // Remove element
    DDebug(m_engine,DebugAll,
	"Stream::addEventNotify. Remove (%p): '%s' from outgoing queue. [%p]",
	e,e ? e->name() : "",this);
    m_outXML.remove(element,true);
    return raise;
}

bool JBComponentStream::invalidElement(XMLElement* e, XMPPError::Type type,
	const char* text)
{
    Debug(m_engine,DebugAll,
	"Stream. Received invalid element ((%p): '%s') in state %u. Error: '%s'. [%p]",
	e,e->name(),state(),s_err[type],this);
    delete e;
    terminate(false,true,XMPPUtils::createStreamError(type,text),true);
    return true;
}

bool JBComponentStream::unexpectedElement(XMLElement* e)
{
    Debug(m_engine,DebugInfo,
	"Stream. Ignoring unexpected element ((%p): '%s') in state %u. [%p]",
	e,e->name(),state(),this);
    delete e;
    return false;
}

bool JBComponentStream::isStreamEnd(XMLElement* e)
{
    if (!e)
	return false;
    bool end = (e->type() == XMLElement::StreamEnd);
    bool error = (e->type() == XMLElement::StreamError);
    if (end || error) {
	Debug(m_engine,DebugAll,"Stream. Received stream  %s in state %u. [%p]",
	    end?"end":"error",state(),this);
	terminate(false,true,e,false);
	return true;
    }
    return false;
}

bool JBComponentStream::readSocket(char* data, u_int32_t& len)
{
    if (state() == Destroy)
	return false;
    // Check socket
    if (!(m_socket && m_socket->valid())) {
	terminate(false,false,errorHostGone(),false);
	return false;
    }
    // Read socket
    int read = m_socket->recv(data,len);
    if (read == Socket::socketError()) {
	len = 0;
	if (!m_socket->canRetry()) {
	    Debug(m_engine,DebugWarn,
		"Stream::readSocket. Socket error: %d: '%s'. [%p]",
		m_socket->error(),::strerror(m_socket->error()),this);
	    terminate(false,false,errorHostGone(),false);
	    return false;
	}
    }
    else
	len = read;
#ifdef XDEBUG
    if (len) {
	String s(data,len);
	XDebug(m_engine,DebugAll,"Stream::readSocket [%p]\r\nData: %s",s.c_str(),this);
    }
#endif //XDEBUG
    return true;
}

bool JBComponentStream::writeSocket(const char* data, u_int32_t& len)
{
    if (state() == Destroy)
	return false;
    // Check socket
    if (!(m_socket && m_socket->valid())) {
	terminate(false,false,errorHostGone(),false);
	return false;
    }
    // Write data
    XDebug(m_engine,DebugAll,"Stream::writeSocket. [%p]\r\nData: %s",this,data);
    int c = m_socket->send(data,len);
    if (c == Socket::socketError()) {
	c = 0;
	if (!m_socket->canRetry()) {
	    Debug(m_engine,DebugWarn,
		"Stream::writeSocket. Socket error: %d: '%s'. [%p]",
		m_socket->error(),::strerror(m_socket->error()),this);
	    terminate(false,false,errorHostGone(),false);
	    return false;
	}
	DDebug(m_engine,DebugMild,
	    "Stream::writeSocket. Socket temporary unavailable: %d: '%s'. [%p]",
	    m_socket->error(),::strerror(m_socket->error()),this);
    }
    len = (u_int32_t)c;
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
