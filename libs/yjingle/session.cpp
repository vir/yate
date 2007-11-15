/**
 * session.cpp
 * Yet Another Jingle Stack
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

#include <yatejingle.h>

using namespace TelEngine;

static XMPPNamespace s_ns;
static XMPPError s_err;

/**
 * JGAudio
 */
XMLElement* JGAudio::createDescription()
{
    return XMPPUtils::createElement(XMLElement::Description,
	XMPPNamespace::JingleAudio);
}

XMLElement* JGAudio::toXML()
{
    XMLElement* p = new XMLElement(XMLElement::PayloadType);
    p->setAttribute("id",m_id);
    p->setAttributeValid("name",m_name);
    p->setAttributeValid("clockrate",m_clockrate);
    p->setAttributeValid("bitrate",m_bitrate);
    return p;
}

void JGAudio::fromXML(XMLElement* element)
{
    element->getAttribute("id",m_id);
    element->getAttribute("name",m_name);
    element->getAttribute("clockrate",m_clockrate);
    element->getAttribute("bitrate",m_bitrate);
}

void JGAudio::set(const char* id, const char* name, const char* clockrate,
	const char* bitrate)
{
    m_id = id;
    m_name = name;
    m_clockrate = clockrate;
    m_bitrate = bitrate;
}

/**
 * JGTransport
 */
JGTransport::JGTransport(const JGTransport& src)
{
    m_name = src.m_name;
    m_address = src.m_address;
    m_port = src.m_port;
    m_preference = src.m_preference;
    m_username = src.m_username;
    m_protocol = src.m_protocol;
    m_generation = src.m_generation;
    m_password = src.m_password;
    m_type = src.m_type;
    m_network = src.m_network;
}

XMLElement* JGTransport::createTransport()
{
    return XMPPUtils::createElement(XMLElement::Transport,
	XMPPNamespace::JingleTransport);
}

XMLElement* JGTransport::toXML()
{
    XMLElement* p = new XMLElement(XMLElement::Candidate);
    p->setAttribute("name",m_name);
    p->setAttribute("address",m_address);
    p->setAttribute("port",m_port);
    p->setAttributeValid("preference",m_preference);
    p->setAttributeValid("username",m_username);
    p->setAttributeValid("protocol",m_protocol);
    p->setAttributeValid("generation",m_generation);
    p->setAttributeValid("password",m_password);
    p->setAttributeValid("type",m_type);
    p->setAttributeValid("network",m_network);
    return p;
}

void JGTransport::fromXML(XMLElement* element)
{
    element->getAttribute("name",m_name);
    element->getAttribute("address",m_address);
    element->getAttribute("port",m_port);
    element->getAttribute("preference",m_preference);
    element->getAttribute("username",m_username);
    element->getAttribute("protocol",m_protocol);
    element->getAttribute("generation",m_generation);
    element->getAttribute("password",m_password);
    element->getAttribute("type",m_type);
    element->getAttribute("network",m_network);
}

/**
 * JGSession
 */
String JGSession::s_dtmf = "0123456789#*ABCD";

TokenDict JGSession::s_actions[] = {
	{"accept",           ActAccept},
	{"initiate",         ActInitiate},
	{"modify",           ActModify},
	{"redirect",         ActRedirect},
	{"reject",           ActReject},
	{"terminate",        ActTerminate},
	{"candidates",       ActTransportCandidates},
	{"transport-info",   ActTransportInfo},
	{"transport-accept", ActTransportAccept},
	{"content-info",     ActContentInfo},
	{0,0}
	};

JGSession::JGSession(JGEngine* engine, JBComponentStream* stream,
	const String& callerJID, const String& calledJID)
    : Mutex(true),
      m_state(Idle),
      m_transportType(TransportInfo),
      m_engine(engine),
      m_stream(stream),
      m_incoming(false),
      m_lastEvent(0),
      m_terminateEvent(0),
      m_private(0),
      m_stanzaId(1),
      m_timeout(0)
{
    m_engine->createSessionId(m_localSid);
    m_sid = m_localSid;
    m_localJID.set(callerJID);
    m_remoteJID.set(calledJID);
    DDebug(m_engine,DebugAll,"Session. Outgoing. ID: '%s'. [%p]",
	m_sid.c_str(),this);
}

JGSession::JGSession(JGEngine* engine, JBEvent* event)
    : Mutex(true),
      m_state(Idle),
      m_transportType(TransportInfo),
      m_engine(engine),
      m_stream(0),
      m_incoming(true),
      m_lastEvent(0),
      m_terminateEvent(0),
      m_private(0),
      m_stanzaId(1),
      m_timeout(0)
{
    // This should never happen
    if (!(event && event->stream() && event->stream()->ref() &&
	event->element() && event->child())) {
	Debug(m_engine,DebugFail,"Session. Incoming. Invalid event. [%p]",this);
	if (event)
	    event->deref();
	m_state = Destroy;
	return;
    }
    // Keep stream and event
    m_stream = event->stream();
    event->releaseStream();
    m_events.append(event);
    // Get attributes
    event->child()->getAttribute("id",m_sid);
    // Create local sid
    m_engine->createSessionId(m_localSid);
    DDebug(m_engine,DebugAll,"Session. Incoming. ID: '%s'. [%p]",
	m_sid.c_str(),this);
}

JGSession::~JGSession()
{
    // Cancel pending outgoing. Hangup. Cleanup
    if (m_stream) {
	m_stream->cancelPending(false,&m_localSid);
	hangup();
	m_stream->deref();
    }
    lock();
    m_events.clear();
    m_engine->removeSession(this);
    if (m_terminateEvent)
	delete m_terminateEvent;
    unlock();
    DDebug(m_engine,DebugAll,"~Session. [%p]",this);
}

bool JGSession::sendMessage(const char* message)
{
    XMLElement* xml = XMPPUtils::createMessage(XMPPUtils::MsgChat,
	m_localJID,m_remoteJID,0,message);
    return sendXML(xml,false);
}

bool JGSession::hangup(bool reject, const char* message)
{
    if (!(state() == Pending || state() == Active))
	return false;
    Lock lock(this);
    DDebug(m_engine,DebugAll,"Session. %s('%s'). [%p]",
	reject?"Reject":"Hangup",message,this);
    if (message)
	sendMessage(message);
    XMLElement* xml = createJingleSet(reject ? ActReject : ActTerminate);
    // Clear sent stanzas list. We will wait for this element to be confirmed
    m_sentStanza.clear();
    m_state = Ending;
    m_timeout = Time::msecNow() + JGSESSION_ENDTIMEOUT;
    return sendXML(xml);
}

bool JGSession::sendTransport(JGTransport* transport, Action act)
{
    if (act != ActTransport && act != ActTransportAccept)
	return false;
    // Create transport
    // For transport-info: A 'transport' child element
    // For candidates: The 'session' element
    if (act == ActTransportAccept) {
	if (transport)
	    transport->deref();
	// No need to send transport-accept if type is candidates
	if (m_transportType == TransportCandidates)
	    return true;
	XMLElement* child = JGTransport::createTransport();
	return sendXML(createJingleSet(act,0,child));
    }
    if (!transport)
	return false;
    // transport-info: send both transport types
    if (m_transportType == TransportInfo) {
	XMLElement* child = JGTransport::createTransport();
	transport->addTo(child);
	if (!sendXML(createJingleSet(ActTransportInfo,0,child)))
	    return false;
    }
    XMLElement* child = transport->toXML();
    transport->deref();
    return sendXML(createJingleSet(ActTransportCandidates,0,child));
}

bool JGSession::accept(XMLElement* description)
{
    if (state() != Pending)
	return false;
    XMLElement* jingle = createJingleSet(ActAccept,description,
	JGTransport::createTransport());
    if (sendXML(jingle)) {
	m_state = Active;
	return true;
    }
    return false;
}

bool JGSession::sendResult(const char* id)
{
    String tmp = id;
    // Don't send if no id.
    // It's useless: the remote peer can't match a response without id
    if (tmp.null())
	return true;
    XMLElement* result = XMPPUtils::createIq(XMPPUtils::IqResult,
	m_localJID,m_remoteJID,tmp);
    return sendXML(result,false);
}

bool JGSession::sendDtmf(char dtmf, bool buttonUp)
{
    if (!isDtmf(dtmf))
	return false;
    String tmp = dtmf;
    XMLElement* xml = XMPPUtils::createElement(XMLElement::Dtmf,
	XMPPNamespace::Dtmf);
    xml->setAttribute("action",buttonUp?"button-up":"button-down");
    xml->setAttribute("code",tmp);
    return sendXML(createJingleSet(ActContentInfo,xml));
}

bool JGSession::sendDtmfMethod(const char* method)
{
    XMLElement* xml = XMPPUtils::createElement(XMLElement::DtmfMethod,
	XMPPNamespace::Dtmf);
    xml->setAttribute("method",method);
    return sendXML(createJingleSet(ActContentInfo,xml));
}

bool JGSession::denyDtmfMethod(XMLElement* element)
{
    if (!element)
	return false;
    String id = element->getAttribute("id");
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqError,
	m_localJID,m_remoteJID,id);
    iq->addChild(element);
    XMLElement* err = XMPPUtils::createError(XMPPError::TypeCancel,
	XMPPError::SFeatureNotImpl);
    err->addChild(XMPPUtils::createElement(s_err[XMPPError::DtmfNoMethod],
	XMPPNamespace::DtmfError));
    iq->addChild(err);
    return sendXML(iq,false);
}

bool JGSession::sendError(XMLElement* element, XMPPError::Type error,
	XMPPError::ErrorType type, const char* text)
{
    if (!element)
	return false;
    String id = element->getAttribute("id");
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqError,
	m_localJID,m_remoteJID,id);
    XMLElement* err = XMPPUtils::createError(type,error,text);
    iq->addChild(element);
    iq->addChild(err);
    return sendXML(iq,false);
}

bool JGSession::receive(JBEvent* event)
{
    // Check stream
    if (!(event && event->stream() && m_stream == event->stream()))
	return false;
    DDebug(m_engine,DebugAll,
	"Session. Check event ((%p): %u) from Jabber. [%p]",
	event,event->type(),this);
    bool accepted = false;
    bool retVal = false;
    switch (event->type()) {
	case JBEvent::Message:
	    accepted = receiveMessage(event,retVal);
	    break;
	case JBEvent::IqResult:
	case JBEvent::IqError:
	    accepted = receiveResult(event,retVal);
	    break;
	case JBEvent::IqJingleGet:
	case JBEvent::IqJingleSet:
	    accepted = receiveJingle(event,retVal);
	    break;
	// WriteFail is identified by the local SID set when posting elements in stream
	case JBEvent::WriteFail:
	    if (event->id() != m_localSid)
		return false;
	    accepted = true;
	    break;
	case JBEvent::Destroy:
	    accepted = receiveDestroy(event,retVal);
	    break;
	default:
	    return false;
    }
    if (!accepted) {
	if (retVal)
	    event->deref();
	return retVal;
    }
    // This session is the destination !
    // Delete event if in terminating state
    if (state() == Destroy) {
	DDebug(m_engine,DebugAll,
	    "Session. Received event ((%p). %u) from Jabber in terminating state. Deleting. [%p]",
	    event,event->type(),this);
	event->deref();
	return true;
    }
    DDebug(m_engine,DebugAll,
	"Session. Accepted event ((%p): %u) from Jabber. [%p]",
	event,event->type(),this);
    // Unlock stream events
    event->releaseStream();
    // Keep event
    Lock lock(this);
    m_events.append(event);
    return true;
}

JGEvent* JGSession::getEvent(u_int64_t time)
{
    Lock lock(this);
    // Check last event, State 
    if (m_lastEvent)
	return 0;
    if (m_terminateEvent) {
	JGEvent* event = m_terminateEvent;
	m_terminateEvent = 0;
	return raiseEvent(event);
    }
    if (state() == Destroy)
	return 0;
    ListIterator iter(m_events);
    GenObject* obj;
    for (; (obj = iter.get());) {
	// Process the event
	JBEvent* jbev = static_cast<JBEvent*>(obj);
	DDebug(m_engine,DebugAll,
	    "Session. Process Jabber event ((%p): %u). [%p]",
	    jbev,jbev->type(),this);
	JGEvent* event = processEvent(jbev,time);
	// Remove jabber event
	DDebug(m_engine,DebugAll,
	    "Session. Remove Jabber event ((%p): %u) from queue. [%p]",
	    jbev,jbev->type(),this);
	m_events.remove(jbev,true);
	// Raise ?
	if (event)
	    return raiseEvent(event);
	if (state() == Destroy) {
	    m_events.clear();
	    break;
	}
    }
    // No event: check timeout
    if (timeout(time)) {
	JGEvent* event = new JGEvent(JGEvent::Terminated,this);
	event->m_reason = "timeout";
	return raiseEvent(event);
    }
    return 0;
}

JGEvent* JGSession::badRequest(JGEvent* event)
{
    XDebug(m_engine,DebugAll,"Session::badRequest. [%p]",this);
    sendEBadRequest(event->releaseXML());
    delete event;
    return 0;
}

JGEvent* JGSession::processEvent(JBEvent* jbev, u_int64_t time)
{
    JGEvent* event = 0;
    // Process state Ending
    if (state() == Ending) {
	bool response = isResponse(jbev);
	if (response || time > m_timeout) {
	    DDebug(m_engine,DebugAll,
		"Session. Terminated in state Ending. Reason: '%s'. [%p]",
		response ? "confirmation" : "timeout",this);
	    event = new JGEvent(JGEvent::Destroy,this);
	}
    }
    else
	event = createEvent(jbev);
    if (!event)
	return 0;
    if (event->final()) {
	confirmIq(event->element());
	m_state = Destroy;
	return event;
    }
    switch (state()) {
	case Pending:
	    return processStatePending(jbev,event);
	case Active:
	    return processStateActive(jbev,event);
	case Idle:
	    return processStateIdle(jbev,event);
	default: ;
    }
    return 0;
}

JGEvent* JGSession::processStatePending(JBEvent* jbev, JGEvent* event)
{
    XDebug(m_engine,DebugAll,"Session::processStatePending. [%p]",this);
    // Check event type
    if (event->type() != JGEvent::Jingle) {
	confirmIq(event->element());
	return event;
    }
    // Check forbidden Jingle actions in this state
    // Change state
    switch (event->action()) {
	case ActAccept:
	    // Incoming sessions should never receive an accept
	    if (incoming())
		return badRequest(event);
	    // Outgoing session received accept: change state
	    m_state = Active;
	    break;
	case ActInitiate:
	    // Session initiate not allowed
	    return badRequest(event);
	default: ;
    }
    confirmIqSelect(event);
    return event;
}

JGEvent* JGSession::processStateActive(JBEvent* jbev, JGEvent* event)
{
    XDebug(m_engine,DebugAll,"Session::processStateActive. [%p]",this);
    if (event->type() == JGEvent::Terminated)
	m_state = Destroy;
    confirmIqSelect(event);
    return event;
}

JGEvent* JGSession::processStateIdle(JBEvent* jbev, JGEvent* event)
{
    XDebug(m_engine,DebugAll,"Session::processStateIdle. [%p]",this);
    if (!incoming())
	return badRequest(event);
    if (event->action() != ActInitiate) {
	m_state = Destroy;
	return badRequest(event);
    }
    m_localJID.set(jbev->to());
    m_remoteJID.set(jbev->from());
    confirmIq(event->element());
    m_state = Pending;
    return event;
}

bool JGSession::decodeJingle(JGEvent* event)
{
    // Get action
    event->element()->getAttribute("id",event->m_id);
    XMLElement* child = event->element()->findFirstChild();
    event->m_action = action(child->getAttribute("type"));
    if (event->m_action == ActCount) {
	sendEServiceUnavailable(event->releaseXML());
	return false;
    }
    // Check session id
    if (m_sid != child->getAttribute("id")) {
	sendEBadRequest(event->releaseXML());
	return false;
    }
    switch (event->m_action) {
	// Check termination
	case ActTerminate:
	case ActReject:
	    event->m_type = JGEvent::Terminated;
	    if (event->m_action == ActTerminate)
		event->m_reason = "hangup";
	    else
		event->m_reason = "rejected";
	    return true;
	case ActContentInfo:
	    return processContentInfo(event);
	default: ;
    }
    // Update media & transport
    if (!updateMedia(event))
	return false;
    if (!updateTransport(event))
	return false;
    // OK !
    event->m_type = JGEvent::Jingle;
    return true;
}

bool JGSession::processContentInfo(JGEvent* event)
{
    // Check dtmf
    XMLElement* child = event->element()->findFirstChild(XMLElement::Dtmf);
    if (child) {
	event->m_reason = child->getAttribute("action");
	event->m_text = child->getAttribute("code");
	bool checked = true;
	for (int i = 0; event->m_text[i]; i++)
	    if (!isDtmf(event->m_text[i])) {
		checked = false;
		break;
	    }
	if ((event->m_reason != "button-up" && event->m_reason != "button-down") ||
	    event->m_text.null() || !checked) {
	    sendEBadRequest(event->releaseXML());
	    return false;
	}
	event->m_action = ActDtmf;
	return true;
    }
    // Check dtmf method
    child = event->element()->findFirstChild(XMLElement::DtmfMethod);
    if (child) {
	event->m_text = child->getAttribute("method");
	if (event->m_text != "rtp" && event->m_reason != "xmpp") {
	    sendEBadRequest(event->releaseXML());
	    return false;
	}
	event->m_action = ActDtmfMethod;
	return true;
    }
    return true;
}

bool JGSession::updateMedia(JGEvent* event)
{
    XMLElement* child = event->element()->findFirstChild();
    XMLElement* descr = child->findFirstChild(XMLElement::Description);
    if (descr) {
	// Check namespace
	if (!descr->hasAttribute("xmlns",s_ns[XMPPNamespace::JingleAudio])) {
	    sendEServiceUnavailable(event->releaseXML());
	    return false;
	}
	// Get payloads
	XMLElement* p = descr->findFirstChild(XMLElement::PayloadType);
	for (; p; p = descr->findNextChild(p,XMLElement::PayloadType)) {
	    JGAudio* a = new JGAudio(p);
	    event->m_audio.append(a);
	}
    }
    return true;
}

bool JGSession::updateTransport(JGEvent* event)
{
    // Detect transport type
    if (event->m_action == ActTransportCandidates) {
	m_transportType = TransportCandidates;
	event->m_action = ActTransport;
	DDebug(m_engine,DebugAll,
	    "Session. Set transport type: 'candidates'. [%p]",this);
    }
    else if (event->m_action == ActTransportInfo ||
	     event->m_action == ActTransportAccept) {
	m_transportType = TransportInfo;
	// Don't set action for transport-accept
	// Use it only to get transport info if any
	if (event->m_action == ActTransportInfo)
	    event->m_action = ActTransport;
	DDebug(m_engine,DebugAll,
	    "Session. Set transport type: 'transport-info'. [%p]",this);
    }
    else
	return true;

    // Get candidates parent:
    // For transport-info: A 'transport' child element
    // For candidates: The 'session' element
    XMLElement* candidates = event->element()->findFirstChild();
    if (m_transportType == TransportInfo) {
	candidates = candidates->findFirstChild(XMLElement::Transport);
	if (candidates &&
	    !candidates->hasAttribute("xmlns",s_ns[XMPPNamespace::JingleTransport])) {
	    sendEServiceUnavailable(event->releaseXML());
	    return false;
	}
    }
    if (!candidates) {
	sendEBadRequest(event->releaseXML());
	return false;
    }
    // Get transports
    XMLElement* t = candidates->findFirstChild(XMLElement::Candidate);
    for (; t; t = candidates->findNextChild(t,XMLElement::Candidate)) {
	JGTransport* tr = new JGTransport(t);
	event->m_transport.append(tr);
    }
    return true;
}

JGEvent* JGSession::decodeMessage(JGEvent* event)
{
    event->element()->getAttribute("id",event->m_id);
    XMLElement* child = event->element()->findFirstChild(XMLElement::Body);
    if (child)
	event->m_text = child->getText();
    event->m_type = JGEvent::Message;
    return event;
}

bool JGSession::decodeError(JGEvent* event)
{
    event->element()->getAttribute("id",event->m_id);
    event->m_type = JGEvent::Error;
    XMLElement* element = event->element();
    if (!element)
	return (event != 0);
    element = element->findFirstChild("error");
    if (!element)
	return (event != 0);
    XMLElement* tmp = element->findFirstChild();
    if (tmp) {
	event->m_reason = tmp->name();
	tmp = element->findNextChild(tmp);
	if (tmp)
	    event->m_text = tmp->getText();
    }
    return (event != 0);
}

JGEvent* JGSession::createEvent(JBEvent* jbev)
{
    JGSentStanza* sent;
    JGEvent* event = new JGEvent(JGEvent::Unexpected,this,jbev->releaseXML());
    if (!event->element())
	return 0;
    // Decode the event
    switch (jbev->type()) {
	case JBEvent::IqResult:
	    DDebug(m_engine,DebugAll,
		"Session. Received confirmation. ID: '%s'. [%p]",
		jbev->id().c_str(),this);
	    sent = isResponse(jbev);
	    if (sent)
		m_sentStanza.remove(sent,true);
	    break;
	case JBEvent::IqJingleGet:
	case JBEvent::IqJingleSet:
	    if (decodeJingle(event))
		return event;
	    break;
	case JBEvent::IqError:
	    DDebug(m_engine,DebugAll,
		"Session. Received error. ID: '%s'. [%p]",
		jbev->id().c_str(),this);
	    sent = isResponse(jbev);
	    if (sent)
		m_sentStanza.remove(sent,true);
	    if (decodeError(event))
		return event;
	    break;
	case JBEvent::Message:
	    return decodeMessage(event);
	case JBEvent::WriteFail:
	    sent = isResponse(jbev);
	    if (sent)
		m_sentStanza.remove(sent,true);
	    event->m_reason = "noconn";
	    event->m_type = JGEvent::Terminated;
	    return event;
	default: ;
    }
    delete event;
    return 0;
}

JGEvent* JGSession::raiseEvent(JGEvent* event)
{
    if (m_lastEvent)
	Debug(m_engine,DebugGoOn,"Session::raiseEvent. Last event already set to %p. [%p]",
	    m_lastEvent,this);
    m_lastEvent = event;
    // Do specific actions: change state, deref() ...
    switch (event->type()) {
	case JGEvent::Terminated:
	    m_state = Destroy;
	    deref();
	    break;
	case JGEvent::Destroy:
	    deref();
	    break;
	default: ;
    }
    DDebug(m_engine,DebugAll,"Session. Raising event((%p): %u). Action: %u. [%p]",
	event,event->type(),event->action(),this);
    return event;
}

bool JGSession::initiate(XMLElement* media, XMLElement* transport)
{
    if (incoming() || state() != Idle)
	return false;
    DDebug(m_engine,DebugAll,"Session. Initiate from '%s' to '%s'. [%p]",
	m_localJID.c_str(),m_remoteJID.c_str(),this);
    XMLElement* xml = createJingleSet(ActInitiate,media,transport);
    if (sendXML(xml))
	m_state = Pending;
    return (m_state == Pending);
}

bool JGSession::sendXML(XMLElement* e, bool addId)
{
    if (!e)
	return false;
    Lock lock(this);
    DDebug(m_engine,DebugAll,"Session::sendXML((%p): '%s'). [%p]",e,e->name(),this);
    if (addId) {
	// Create id
	String id = m_localSid;
	id << "_" << (unsigned int)m_stanzaId;
	m_stanzaId++;
	e->setAttribute("id",id);
	appendSent(e);
    }
    // Send. If it fails leave it in the sent items to timeout
    JBComponentStream::Error res = m_stream->sendStanza(e,m_localSid);
    if (res == JBComponentStream::ErrorNoSocket ||
	res == JBComponentStream::ErrorContext)
	return false;
    return true;
}

XMLElement* JGSession::createJingleSet(Action action,
	XMLElement* element1, XMLElement* element2)
{
    // Create 'iq' and 'jingle'
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,
	m_localJID,m_remoteJID,0);
    XMLElement* jingle = XMPPUtils::createElement(XMLElement::Jingle,
	XMPPNamespace::Jingle);
    if (action < ActCount)
	jingle->setAttribute("type",actionText(action));
    jingle->setAttribute("initiator",initiator());
//    jingle->setAttribute("responder",incoming()?m_localJID:m_remoteJID);
    jingle->setAttribute("id",m_sid);
    jingle->addChild(element1);
    jingle->addChild(element2);
    iq->addChild(jingle);
    return iq;
}

void JGSession::confirmIq(XMLElement* element)
{
    if (!(element && element->type() == XMLElement::Iq))
	return;
    XMPPUtils::IqType type = XMPPUtils::iqType(element->getAttribute("type"));
    if (type == XMPPUtils::IqResult || type == XMPPUtils::IqError)
	return;
    String id = element->getAttribute("id");
    sendResult(id);
}

void JGSession::confirmIqSelect(JGEvent* event)
{
    if (!event)
	return;
    // Skip transport
    if (event->type() == JGEvent::Jingle)
	switch (event->action()) {
	    case ActTransport:
	    case ActTransportInfo:
	    case ActTransportCandidates:
	    case ActDtmfMethod:
		return;
	    default: ;
	}
    confirmIq(event->element());
}

void JGSession::eventTerminated(JGEvent* event)
{
    lock();
    if (event == m_lastEvent) {
	DDebug(m_engine,DebugAll,
	    "Session. Event((%p): %u) terminated. [%p]",event,event->type(),this);
	m_lastEvent = 0;
    }
    else if (m_lastEvent)
	Debug(m_engine,DebugNote,"Event((%p): %u) replaced while processed. [%p]",
	    event,event->type(),this);
    unlock();
}

JGSentStanza* JGSession::isResponse(const JBEvent* jbev)
{
    Lock lock(this);
    ObjList* obj = m_sentStanza.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JGSentStanza* tmp = static_cast<JGSentStanza*>(obj->get());
	if (tmp->isResponse(jbev)) {
	    DDebug(m_engine,DebugAll,
		"Session. Sent element with id '%s' confirmed. [%p]",
		tmp->m_id.c_str(),this);
	    return tmp;
	}
    }
    return 0;
}

bool JGSession::timeout(u_int64_t time)
{
    Lock lock(this);
    ObjList* obj = m_sentStanza.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JGSentStanza* tmp = static_cast<JGSentStanza*>(obj->get());
	if (tmp->timeout(time)) {
	    DDebug(m_engine,DebugAll,
		"Session. Sent element with id '%s' timed out. [%p]",
		tmp->m_id.c_str(),this);
	    return true;
	}
    }
    return false;
}

void JGSession::appendSent(XMLElement* element)
{
    if (!(element && element->type() == XMLElement::Iq))
	return;
    String id = element->getAttribute("id");
    if (id)
	m_sentStanza.append(new JGSentStanza(id));
}

bool JGSession::receiveMessage(const JBEvent* event, bool& retValue)
{
    if (!(event->to() == local() && event->from() == remote()))
	return false;
    //TODO: Make a copy of message: return false
    retValue = true;
    return true;
}

bool JGSession::receiveResult(const JBEvent* event, bool& retValue)
{
    if (!(event->to() == local() && event->from() == remote()))
	return false;
    Lock lock(this);
    JGSentStanza* sent = isResponse(event);
    if (!sent)
	return false;
    // Keep the event if:  state is Ending: Will raise a Terminated event
    //                     event is IqError: Will be sent to the upper layer
    if (state() == Ending || event->type() == JBEvent::IqError)
	return true;
    // Event is a result. Consume it
    m_sentStanza.remove(sent,true);
    retValue = true;
    return false;
}

bool JGSession::receiveJingle(const JBEvent* event, bool& retValue)
{
    // Jingle stanzas must match source, destination and session id
    if (!(event->to() == local() && event->from() == remote() &&
	event->child() && event->child()->hasAttribute("id",m_sid)))
	return false;
    return true;
}

bool JGSession::receiveDestroy(const JBEvent* event, bool& retValue)
{
    Lock lock(this);
    // Ignore if session is already ending or destroying
    if (state() != Ending && state() != Destroy && !m_terminateEvent) {
	DDebug(m_engine,DebugAll,
	    "Session. Terminate on stream destroy. [%p]",this);
	m_state = Destroy;
	m_terminateEvent = new JGEvent(JGEvent::Terminated,this);
	m_terminateEvent->m_reason = "noconn";
    }
    retValue = false;
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
