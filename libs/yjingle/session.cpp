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
XMLElement* JGAudio::toXML()
{
    XMLElement* p = new XMLElement(XMLElement::PayloadType);
    p->setAttribute("id",id);
    p->setAttributeValid("name",name);
    p->setAttributeValid("clockrate",clockrate);
    p->setAttributeValid("bitrate",bitrate);
    return p;
}

void JGAudio::fromXML(XMLElement* xml)
{
    if (!xml) {
	set("","","","","");
	return;
    }
    xml->getAttribute("id",id);
    xml->getAttribute("name",name);
    xml->getAttribute("clockrate",clockrate);
    xml->getAttribute("bitrate",bitrate);
}


/**
 * JGAudioList
 */
// Find a data payload by its synonym
JGAudio* JGAudioList::findSynonym(const String& value)
{
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(o->get());
	if (value == a->synonym)
	    return a;
    }
    return 0;
}

// Create a 'description' element and add payload children to it
XMLElement* JGAudioList::toXML(bool telEvent)
{
    XMLElement* desc = XMPPUtils::createElement(XMLElement::Description,
	XMPPNamespace::JingleAudio);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(o->get());
	desc->addChild(a->toXML());
    }
    if (telEvent) {
	JGAudio* te = new JGAudio("106","telephone-event","8000","","");
	desc->addChild(te->toXML());
	TelEngine::destruct(te);
    }
    return desc;
}

// Fill this list from an XML element's children. Clear before attempting to fill
void JGAudioList::fromXML(XMLElement* xml)
{
    clear();
    XMLElement* m = xml ? xml->findFirstChild(XMLElement::PayloadType) : 0;
    for (; m; m = xml->findNextChild(m,XMLElement::PayloadType))
	ObjList::append(new JGAudio(m));
}

// Create a list from data payloads
bool JGAudioList::createList(String& dest, bool synonym, const char* sep)
{
    dest = "";
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(o->get());
	dest.append(synonym?a->synonym:a->name,sep);
    }
    return (0 != dest.length());
}


/**
 * JGTransport
 */
JGTransport::JGTransport(const JGTransport& src)
{
    name = src.name;
    address = src.address;
    port = src.port;
    preference = src.preference;
    username = src.username;
    protocol = src.protocol;
    generation = src.generation;
    password = src.password;
    type = src.type;
    network = src.network;
}

XMLElement* JGTransport::createTransport()
{
    return XMPPUtils::createElement(XMLElement::Transport,
	XMPPNamespace::JingleTransport);
}

XMLElement* JGTransport::toXML()
{
    XMLElement* p = new XMLElement(XMLElement::Candidate);
    p->setAttribute("name",name);
    p->setAttribute("address",address);
    p->setAttribute("port",port);
    p->setAttributeValid("preference",preference);
    p->setAttributeValid("username",username);
    p->setAttributeValid("protocol",protocol);
    p->setAttributeValid("generation",generation);
    p->setAttributeValid("password",password);
    p->setAttributeValid("type",type);
    p->setAttributeValid("network",network);
    return p;
}

void JGTransport::fromXML(XMLElement* element)
{
    element->getAttribute("name",name);
    element->getAttribute("address",address);
    element->getAttribute("port",port);
    element->getAttribute("preference",preference);
    element->getAttribute("username",username);
    element->getAttribute("protocol",protocol);
    element->getAttribute("generation",generation);
    element->getAttribute("password",password);
    element->getAttribute("type",type);
    element->getAttribute("network",network);
}


/**
 * JGSession
 */

TokenDict JGSession::s_states[] = {
    {"Idle",     Idle},
    {"Pending",  Pending},
    {"Active",   Active},
    {"Ending",   Ending},
    {"Destroy",  Destroy},
    {0,0}
};

TokenDict JGSession::s_actions[] = {
    {"accept",           ActAccept},
    {"initiate",         ActInitiate},
    {"reject",           ActReject},
    {"terminate",        ActTerminate},
    {"session-info",     ActInfo},
    {"candidates",       ActTransportCandidates},
    {"transport-info",   ActTransportInfo},
    {"transport-accept", ActTransportAccept},
    {"content-info",     ActContentInfo},
    {"Transport",        ActTransport},
    {"session-transfer", ActTransfer},
    {"DTMF",             ActDtmf},
    {"DTMF method",      ActDtmfMethod},
    {0,0}
};

TokenDict JGSession::s_reasons[] = {
    {"busy",                     ReasonBusy},
    {"decline",                  ReasonDecline},
    {"connectivity-error",       ReasonConn},
    {"media-error",              ReasonMedia},
    {"unsupported-transports",   ReasonTransport},
    {"no-error",                 ReasonNoError},
    {"success",                  ReasonOk},
    {"unsupported-applications", ReasonNoApp},
    {"alternative-session",      ReasonAltSess},
    {"general-error",            ReasonUnknown},
    {"transferred",              ReasonTransfer},
    {0,0}
};

// Create an outgoing session
JGSession::JGSession(JGEngine* engine, JBStream* stream,
	const String& callerJID, const String& calledJID,
	XMLElement* media, XMLElement* transport,
	bool sid, XMLElement* extra, const char* msg)
    : Mutex(true),
    m_state(Idle),
    m_transportType(TransportUnknown),
    m_engine(engine),
    m_stream(stream),
    m_outgoing(true),
    m_localJID(callerJID),
    m_remoteJID(calledJID),
    m_sidAttr(sid?"sid":"id"),
    m_lastEvent(0),
    m_private(0),
    m_stanzaId(1)
{
    m_engine->createSessionId(m_localSid);
    m_sid = m_localSid;
    Debug(m_engine,DebugAll,"Call(%s). Outgoing msg=%s [%p]",m_sid.c_str(),msg,this);
    if (msg)
	sendMessage(msg);
    XMLElement* xml = createJingle(ActInitiate,media,transport,extra);
    if (sendStanza(xml))
	changeState(Pending);
    else
	changeState(Destroy);
}

// Create an incoming session
JGSession::JGSession(JGEngine* engine, JBEvent* event, const String& id, bool sid)
    : Mutex(true),
    m_state(Idle),
    m_transportType(TransportUnknown),
    m_engine(engine),
    m_stream(event->stream()),
    m_outgoing(false),
    m_sid(id),
    m_sidAttr(sid?"sid":"id"),
    m_lastEvent(0),
    m_private(0),
    m_stanzaId(1)
{
    m_events.append(event);
    m_engine->createSessionId(m_localSid);
    Debug(m_engine,DebugAll,"Call(%s). Incoming [%p]",m_sid.c_str(),this);
}

// Destructor: hangup, cleanup, remove from engine's list
JGSession::~JGSession()
{
    XDebug(m_engine,DebugAll,"JGSession::~JGSession() [%p]",this);
}

// Release this session and its memory
void JGSession::destroyed()
{
    lock();
    // Cancel pending outgoing. Hangup. Cleanup
    if (m_stream) {
	m_stream->removePending(m_localSid,false);
	hangup(ReasonUnknown);
	TelEngine::destruct(m_stream);
    }
    m_events.clear();
    unlock();
    // Remove from engine
    Lock lock(m_engine);
    m_engine->m_sessions.remove(this,false);
    lock.drop();
    DDebug(m_engine,DebugInfo,"Call(%s). Destroyed [%p]",m_sid.c_str(),this);
}

// Ask this session to accept an event
bool JGSession::acceptEvent(JBEvent* event, const String& sid)
{
    if (!event)
	return false;

    // Requests must match the session id
    // Responses' id must start with session's local id (this is the way we generate the stanza id)
    if (sid) {
	if (sid != m_sid)
	    return false;
    }
    else if (!event->id().startsWith(m_localSid))
	return false;
    // Check to/from
    if (m_localJID != event->to() || m_remoteJID != event->from())
	return false;

    // Ok: keep a referenced event
    if (event->ref())
	enqueue(event);
    return true;
}

// Accept a Pending incoming session
bool JGSession::accept(XMLElement* description, String* stanzaId)
{
    Lock lock(this);
    if (outgoing() || state() != Pending)
	return false;
    XMLElement* xml = createJingle(ActAccept,description,JGTransport::createTransport());
    if (!sendStanza(xml,stanzaId))
	return false;
    changeState(Active);
    return true;
}

// Close a Pending or Active session
bool JGSession::hangup(int reason, const char* msg)
{
    Lock lock(this);
    if (state() != Pending && state() != Active)
	return false;
    bool reject = (state() == Pending);
    DDebug(m_engine,DebugAll,"Call(%s). %s('%s') [%p]",m_sid.c_str(),
	reject?"Reject":"Hangup",msg,this);
    // Clear sent stanzas list. We will wait for this element to be confirmed
    m_sentStanza.clear();
    const char* tmp = lookupReason(reason);
    XMLElement* res = 0;
    if (tmp || msg) {
	res = new XMLElement(XMLElement::Reason);
	if (tmp)
	    res->addChild(new XMLElement(tmp));
	if (msg)
	    res->addChild(new XMLElement(XMLElement::Text,0,msg));
    }
    XMLElement* xml = createJingle(reject?ActReject:ActTerminate,res);
    bool ok = sendStanza(xml);
    changeState(Ending);
    return ok;
}

// Confirm a received element. If the error is NoError a result stanza will be sent
// Otherwise, an error stanza will be created and sent
bool JGSession::confirm(XMLElement* xml, XMPPError::Type error,
	const char* text, XMPPError::ErrorType type)
{
    if (!xml)
	return false;
    XMLElement* iq = 0;
    if (error == XMPPError::NoError) {
	String id = xml->getAttribute("id");
	iq = XMPPUtils::createIq(XMPPUtils::IqResult,m_localJID,m_remoteJID,id);
	// The receiver will detect which stanza is confirmed by id
	// If missing, make a copy of the received element and attach it to the error
	if (!id) {
	    XMLElement* copy = new XMLElement(*xml);
	    iq->addChild(copy);
	}
    }
    else
	iq = XMPPUtils::createError(xml,type,error,text);
    return sendStanza(iq,0,false);
}

// Send a dtmf string to remote peer
bool JGSession::sendDtmf(const char* dtmf, bool buttonUp, String* stanzaId)
{
    XMLElement* iq = createJingle(ActContentInfo);
    XMLElement* sess = iq->findFirstChild();
    if (!(dtmf && *dtmf && sess)) {
	TelEngine::destruct(sess);
	return sendStanza(iq,stanzaId);
    }
    char s[2] = {0,0};
    const char* action = buttonUp ? "button-up" : "button-down";
    while (*dtmf) {
	s[0] = *dtmf++;
	XMLElement* xml = XMPPUtils::createElement(XMLElement::Dtmf,XMPPNamespace::Dtmf);
	xml->setAttribute("action",action);
	xml->setAttribute("code",s);
	sess->addChild(xml);
    }
    TelEngine::destruct(sess);
    return sendStanza(iq,stanzaId);
}

// Send a dtmf method to remote peer
bool JGSession::sendDtmfMethod(const char* method, String* stanzaId)
{
    XMLElement* xml = XMPPUtils::createElement(XMLElement::DtmfMethod,
	XMPPNamespace::Dtmf);
    xml->setAttribute("method",method);
    return sendStanza(createJingle(ActContentInfo,xml),stanzaId);
}

// Deny a dtmf method request from remote peer
bool JGSession::denyDtmfMethod(XMLElement* element)
{
    if (!element)
	return false;
    String id = element->getAttribute("id");
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqError,m_localJID,m_remoteJID,id);
    iq->addChild(element);
    XMLElement* err = XMPPUtils::createError(XMPPError::TypeCancel,XMPPError::SFeatureNotImpl);
    err->addChild(XMPPUtils::createElement(s_err[XMPPError::DtmfNoMethod],XMPPNamespace::DtmfError));
    iq->addChild(err);
    return sendStanza(iq,0,false);
}

// Send a session info element to the remote peer
bool JGSession::sendInfo(XMLElement* xml, String* stanzaId)
{
    if (!xml)
	return false;
    // Make sure we dont't terminate the session if info fails
    String tmp;
    if (!stanzaId) {
	tmp = "Info" + String(Time::secNow());
	stanzaId = &tmp;
    }
    return sendStanza(createJingle(ActInfo,xml),stanzaId);
}

// Check if the remote party supports a given feature
bool JGSession::hasFeature(XMPPNamespace::Type feature)
{
    if (!m_stream)
	return false;
    JBClientStream* cStream = static_cast<JBClientStream*>(m_stream->getObject("JBClientStream"));
    if (cStream) {
	XMPPUser* user = cStream->getRemote(remote());
	if (!user)
	    return false;
	bool ok = false;
	user->lock();
	JIDResource* res = user->remoteRes().get(remote().resource());
	ok = res && res->features().get(feature);
	user->unlock();
	TelEngine::destruct(user);
	return ok;
    }
    return false;
}

// Build a transfer element
XMLElement* JGSession::buildTransfer(const String& transferTo,
    const String& transferFrom, const String& sid)
{
    XMLElement* transfer = XMPPUtils::createElement(XMLElement::Transfer,
	XMPPNamespace::JingleTransfer);
    transfer->setAttributeValid("from",transferFrom);
    transfer->setAttributeValid("to",transferTo);
    transfer->setAttributeValid("sid",sid);
    return transfer;
}

// Enqueue a Jabber engine event
void JGSession::enqueue(JBEvent* event)
{
    Lock lock(this);
    if (event->type() == JBEvent::Terminated || event->type() == JBEvent::Destroy)
	m_events.insert(event);
    else
	m_events.append(event);
    DDebug(m_engine,DebugAll,"Call(%s). Accepted event (%p,%s) [%p]",
	m_sid.c_str(),event,event->name(),this);
}

// Process received events. Generate Jingle events
JGEvent* JGSession::getEvent(u_int64_t time)
{
    Lock lock(this);
    if (m_lastEvent)
	return 0;
    if (state() == Destroy)
	return 0;
    // Deque and process event(s)
    // Loop until a jingle event is generated or no more events in queue
    JBEvent* jbev = 0;
    while (true) {
	TelEngine::destruct(jbev);
	jbev = static_cast<JBEvent*>(m_events.remove(false));
	if (!jbev)
	    break;

	DDebug(m_engine,DebugAll,
	    "Call(%s). Dequeued Jabber event (%p,%s) in state %s [%p]",
	    m_sid.c_str(),jbev,jbev->name(),lookupState(state()),this);

	// Process Jingle 'set' stanzas
	if (jbev->type() == JBEvent::IqJingleSet) {
	    // Filter some conditions in which we can't accept any jingle stanza
	    // Outgoing idle sessions are waiting for the user to initiate them
	    if (state() == Idle && outgoing()) {
		confirm(jbev->releaseXML(),XMPPError::SRequest);
		continue;
	    }

	    m_lastEvent = decodeJingle(jbev);

	    if (!m_lastEvent) {
		// Destroy incoming session if session initiate stanza contains errors
		if (!outgoing() && state() == Idle) {
		    m_lastEvent = new JGEvent(JGEvent::Destroy,this,0,"failure");
		    break;
		}
		continue;
	    }

	    // ActInfo: empty session info
	    if (m_lastEvent->action() == ActInfo) {
	        XDebug(m_engine,DebugAll,"Call(%s). Received empty '%s' (ping) [%p]",
		    m_sid.c_str(),lookup(m_lastEvent->action(),s_actions),this);
		confirm(m_lastEvent->element());
		delete m_lastEvent;
		m_lastEvent = 0;
		continue;
	    }

	    DDebug(m_engine,DebugInfo,
		"Call(%s). Processing action (%u,'%s') state=%s [%p]",
		m_sid.c_str(),m_lastEvent->action(),
		lookup(m_lastEvent->action(),s_actions),lookupState(state()),this);

	    // Check for termination events
	    if (m_lastEvent->final())
		break;

	    bool error = false;
	    bool fatal = false;
	    switch (state()) {
		case Active:
		    error = m_lastEvent->action() == ActAccept ||
			m_lastEvent->action() == ActInitiate ||
			m_lastEvent->action() == ActRinging;
		    break;
		case Pending:
		    // Accept session-accept, transport, ringing stanzas
		    switch (m_lastEvent->action()) {
			case ActAccept:
			    changeState(Active);
			    break;
			case ActTransportAccept:
			case ActTransport:
			case ActTransportInfo:
			case ActTransportCandidates:
			case ActContentInfo:
			case ActRinging:
			    break;
			default:
			    error = true;
		    }
		    break;
		case Idle:
		    // Update data. Terminate if not a session initiating event
		    if (m_lastEvent->action() == ActInitiate) {
			m_localJID.set(jbev->to());
			m_remoteJID.set(jbev->from());
			changeState(Pending);
		    }
		    else
			error = fatal = true;
		    break;
		default:
		    error = true;
	    }

	    if (!error) {
		// Automatically confirm some actions
		// Don't confirm actions that need session user's interaction:
		//  transfer, transport, dtmf method negotiation or other info
		switch (m_lastEvent->action()) {
		    case ActTransport:
		    case ActDtmfMethod:
		    case ActTransfer:
		    case ActRinging:
		    case ActHold:
		    case ActActive:
		    case ActMute:
			break;
		    default:
			confirm(m_lastEvent->element());
		}
	    }
	    else {
		confirm(m_lastEvent->releaseXML(),XMPPError::SRequest);
		delete m_lastEvent;
		m_lastEvent = 0;
		if (fatal)
		    m_lastEvent = new JGEvent(JGEvent::Destroy,this);
		else
		    continue;
	    }
	    break;
	}

	// Check for responses or failures
	bool response = jbev->type() == JBEvent::IqJingleRes ||
			jbev->type() == JBEvent::IqJingleErr ||
			jbev->type() == JBEvent::IqResult ||
			jbev->type() == JBEvent::IqError ||
			jbev->type() == JBEvent::WriteFail;
	while (response) {
	    JGSentStanza* sent = 0;
	    // Find a sent stanza to match the event's id
	    for (ObjList* o = m_sentStanza.skipNull(); o; o = o->skipNext()) {
		sent = static_cast<JGSentStanza*>(o->get());
		if (jbev->id() == *sent)
		    break;
		sent = 0;
	    }
	    if (!sent)
		break;

	    // Check termination conditions
	    // Always terminate when receiving responses in Ending state
	    bool terminateEnding = (state() == Ending);
	    // Terminate pending outgoing if no notification required
	    // (Initial session request is sent without notification required)
	    bool terminatePending = false;
	    if (state() == Pending && outgoing() &&
		(jbev->type() == JBEvent::IqJingleErr || jbev->type() == JBEvent::WriteFail))
		terminatePending = !sent->notify();
	    // Write fail: Terminate if failed stanza is a Jingle one and the sender
	    //  didn't requested notification
	    bool terminateFail = false;
	    if (!(terminateEnding || terminatePending) && jbev->type() == JBEvent::WriteFail)
		terminateFail = !sent->notify();

	    // Generate event
	    if (terminateEnding)
		m_lastEvent = new JGEvent(JGEvent::Destroy,this);
	    else if (terminatePending || terminateFail)
		m_lastEvent = new JGEvent(JGEvent::Terminated,this,
		    jbev->type() != JBEvent::WriteFail ? jbev->releaseXML() : 0,
		    jbev->text() ? jbev->text().c_str() : "failure");
	    else if (sent->notify())
		switch (jbev->type()) {
		    case JBEvent::IqJingleRes:
		    case JBEvent::IqResult:
			m_lastEvent = new JGEvent(JGEvent::ResultOk,this,
			    jbev->releaseXML());
			break;
		    case JBEvent::IqJingleErr:
		    case JBEvent::IqError:
			m_lastEvent = new JGEvent(JGEvent::ResultError,this,
			    jbev->releaseXML(),jbev->text());
			break;
		    case JBEvent::WriteFail:
			m_lastEvent = new JGEvent(JGEvent::ResultWriteFail,this,
			    jbev->releaseXML(),jbev->text());
			break;
		    default:
			DDebug(m_engine,DebugStub,
			    "Call(%s). Unhandled response event (%p,%u,%s) [%p]",
			    m_sid.c_str(),jbev,jbev->type(),jbev->name(),this);
		}
	    if (m_lastEvent && !m_lastEvent->m_id)
		m_lastEvent->m_id = *sent;
	    m_sentStanza.remove(sent,true);

	    String error;
#ifdef DEBUG
	    if (jbev->type() == JBEvent::IqJingleErr && jbev->text())
		error << " (error='" << jbev->text() << "')";
#endif
	    bool terminate = (m_lastEvent && m_lastEvent->final());
	    Debug(m_engine,DebugAll,
		"Call(%s). Sent element with id=%s confirmed by event=%s%s%s [%p]",
		m_sid.c_str(),jbev->id().c_str(),jbev->name(),error.safe(),
		terminate ? ". Terminating": "",this);

	    // Gracefully terminate
	    if (terminate && state() != Ending)
		hangup(ReasonUnknown);

	    break;
	}
	if (response)
	    if (!m_lastEvent)
		continue;
	    else
		break;

	// Silently ignore temporary stream down
	if (jbev->type() == JBEvent::Terminated) {
	    DDebug(m_engine,DebugInfo,
		"Call(%s). Stream disconnected in state %s [%p]",
		m_sid.c_str(),lookupState(state()),this);
	    continue;
	}

	// Terminate on stream destroy
	if (jbev->type() == JBEvent::Destroy) {
	    Debug(m_engine,DebugInfo,
		"Call(%s). Stream destroyed in state %s [%p]",
		m_sid.c_str(),lookupState(state()),this);
	    m_lastEvent = new JGEvent(JGEvent::Terminated,this,0,"noconn");
	    break;
	}

	Debug(m_engine,DebugStub,"Call(%s). Unhandled event type %u '%s' [%p]",
	    m_sid.c_str(),jbev->type(),jbev->name(),this);
	continue;
    }
    TelEngine::destruct(jbev);

    // No event: check first sent stanza's timeout
    if (!m_lastEvent) {
	ObjList* o = m_sentStanza.skipNull();
	JGSentStanza* tmp = o ? static_cast<JGSentStanza*>(o->get()) : 0;
	while (tmp && tmp->timeout(time)) {
	    Debug(m_engine,DebugNote,"Call(%s). Sent stanza ('%s') timed out [%p]",
		m_sid.c_str(),tmp->c_str(),this);
	    // Don't terminate if the sender requested to be notified
	    m_lastEvent = new JGEvent(tmp->notify() ? JGEvent::ResultTimeout : JGEvent::Terminated,
		this,0,"timeout");
	    m_lastEvent->m_id = *tmp;
	    o->remove();
	    if (m_lastEvent->final())
		hangup(false,"Timeout");
	    break;
	}
    }

    if (m_lastEvent) {
	// Deref the session for final events
	if (m_lastEvent->final()) {
	    changeState(Destroy);
	    deref();
	}
	DDebug(m_engine,DebugAll,
	    "Call(%s). Raising event (%p,%u) action=%u final=%s [%p]",
	    m_sid.c_str(),m_lastEvent,m_lastEvent->type(),
	    m_lastEvent->action(),String::boolText(m_lastEvent->final()),this);
	return m_lastEvent;
    }

    return 0;
}

// Send a stanza to the remote peer
bool JGSession::sendStanza(XMLElement* stanza, String* stanzaId, bool confirmation)
{
    Lock lock(this);
    if (!(state() != Ending && state() != Destroy && stanza && m_stream)) {
#ifdef DEBUG
	Debug(m_engine,DebugNote,
	    "Call(%s). Can't send stanza (%p,'%s') in state %s [%p]",
	    m_sid.c_str(),stanza,stanza->name(),lookupState(m_state),this);
#endif
	TelEngine::destruct(stanza);
	return false;
    }
    DDebug(m_engine,DebugAll,"Call(%s). Sending stanza (%p,'%s') id=%s [%p]",
	m_sid.c_str(),stanza,stanza->name(),String::boolText(stanzaId != 0),this);
    const char* senderId = m_localSid;
    // Check if the stanza should be added to the list of stanzas requiring confirmation
    if (confirmation && stanza->type() == XMLElement::Iq) {
	String id = m_localSid;
	id << "_" << (unsigned int)m_stanzaId++;
	JGSentStanza* sent = new JGSentStanza(id,
	    m_engine->stanzaTimeout() + Time::msecNow(),stanzaId != 0);
	stanza->setAttribute("id",*sent);
	senderId = *sent;
	if (stanzaId)
	    *stanzaId = *sent;
	m_sentStanza.append(sent);
    }
    // Send. If it fails leave it in the sent items to timeout
    JBStream::Error res = m_stream->sendStanza(stanza,senderId);
    if (res == JBStream::ErrorNoSocket || res == JBStream::ErrorContext)
	return false;
    return true;
}

// Decode a jingle stanza
JGEvent* JGSession::decodeJingle(JBEvent* jbev)
{
    XMLElement* jingle = jbev->child();
    if (!jingle) {
	confirm(jbev->releaseXML(),XMPPError::SBadRequest);
	return 0;
    }

    Action act = (Action)lookup(jingle->getAttribute("type"),s_actions,ActCount);
    if (act == ActCount) {
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable,
	    "Unknown session action");
	return 0;
    }

    // *** ActTerminate or ActReject
    if (act == ActTerminate || act == ActReject) {
	confirm(jbev->element());
	const char* reason = 0;
	const char* text = 0;
	XMLElement* res = jingle->findFirstChild(XMLElement::Reason);
	if (res) {
	    XMLElement* tmp = res->findFirstChild();
	    if (tmp && tmp->type() != XMLElement::Text)
		reason = tmp->name();
	    TelEngine::destruct(tmp);
	    tmp = res->findFirstChild(XMLElement::Text);
	    if (tmp)
		text = tmp->getText();
	    TelEngine::destruct(tmp);
	    TelEngine::destruct(res);
	}
	if (!reason)
	    reason = act==ActTerminate ? "hangup" : "rejected";
	return new JGEvent(JGEvent::Terminated,this,jbev->releaseXML(),reason,text);
    }

    // *** ActContentInfo: ActDtmf or ActDtmfMethod
    if (act == ActContentInfo) {
	// Check dtmf
	XMLElement* tmp = jingle->findFirstChild(XMLElement::Dtmf);
	if (tmp) {
	    String reason = tmp->getAttribute("action");
	    // Expect more then 1 'dtmf' child
	    String text;
	    for (; tmp; tmp = jingle->findNextChild(tmp,XMLElement::Dtmf))
		text << tmp->getAttribute("code");
	    if (!text || (reason != "button-up" && reason != "button-down")) {
		confirm(jbev->releaseXML(),XMPPError::SBadRequest,"Unknown action");
		return 0;
	    }
	    return new JGEvent(ActDtmf,this,jbev->releaseXML(),reason,text);
	}
	// Check dtmf method
	tmp = jingle->findFirstChild(XMLElement::DtmfMethod);
	if (tmp) {
	    String text = tmp->getAttribute("method");
	    TelEngine::destruct(tmp);
	    if (text != "rtp" && text != "xmpp") {
		confirm(jbev->releaseXML(),XMPPError::SBadRequest,"Unknown method");
		return 0;
	    }
	    return new JGEvent(ActDtmfMethod,this,jbev->releaseXML(),0,text);
	}
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable);
	return 0;
    }

    // *** ActInfo
    if (act == ActInfo) {
        // Check info element
	// Return ActInfo event to signal ping (XEP-0166 6.8)
	XMLElement* child = jingle->findFirstChild();
	if (!child)
	    return new JGEvent(ActInfo,this,jbev->releaseXML());

	JGEvent* event = 0;
	Action a = ActCount;
	XMPPNamespace::Type ns = XMPPNamespace::Count;
	// Check namespace and build event
	switch (child->type()) {
	    case XMLElement::Transfer:
		a = ActTransfer;
		ns = XMPPNamespace::JingleTransfer;
		break;
	    case XMLElement::Hold:
		a = ActHold;
		ns = XMPPNamespace::JingleRtpInfo;
		break;
	    case XMLElement::Active:
		a = ActActive;
		ns = XMPPNamespace::JingleRtpInfo;
		break;
	    case XMLElement::Ringing:
		a = ActRinging;
		ns = XMPPNamespace::JingleRtpInfo;
		break;
	    case XMLElement::Mute:
		a = ActMute;
		ns = XMPPNamespace::JingleRtpInfo;
		break;
	    default: ;
	}
	if (a != ActCount && XMPPUtils::hasXmlns(*child,ns))
	    event = new JGEvent(a,this,jbev->releaseXML());
	else
	    confirm(jbev->releaseXML(),XMPPError::SFeatureNotImpl);
        TelEngine::destruct(child);
	return event;
    }

    // *** ActAccept ActInitiate ActModify
    // *** ActTransport: ActTransportInfo/ActTransportCandidates
    // *** ActTransportAccept

    // Detect transport type
    if (act == ActTransportCandidates) {
	m_transportType = TransportCandidates;
	act = ActTransport;
	DDebug(m_engine,DebugInfo,"Call(%s). Set transport='candidates' [%p]",
	    m_sid.c_str(),this);
    }
    else if (act == ActTransportInfo || act == ActTransportAccept) {
	m_transportType = TransportInfo;
	// Don't set action for transport-accept. Use it only to get transport info if any
	if (act == ActTransportInfo)
	    act = ActTransport;
	DDebug(m_engine,DebugInfo,"Call(%s). Set transport='transport-info' [%p]",
	    m_sid.c_str(),this);
    }

    // Get transport candidates parent:
    //     transport-info: A 'transport' child element
    //     candidates: The 'session' element
    // Get media description
    // Create event, update transport and media
    XMLElement* trans = jingle;
    XMLElement* media = 0;
    JGEvent* event = 0;
    while (true) {
	if (m_transportType == TransportInfo) {
	    trans = trans->findFirstChild(XMLElement::Transport);
	    if (trans && !trans->hasAttribute("xmlns",s_ns[XMPPNamespace::JingleTransport]))
		break;
	}
	media = jingle->findFirstChild(XMLElement::Description);
	if (media && !media->hasAttribute("xmlns",s_ns[XMPPNamespace::JingleAudio]))
	    break;
	// Don't set the event's element yet: this would invalidate the 'jingle' variable
	event = new JGEvent(act,this,0);
	XMLElement* t = trans ? trans->findFirstChild(XMLElement::Candidate) : 0;
	for (; t; t = trans->findNextChild(t,XMLElement::Candidate))
	    event->m_transport.append(new JGTransport(t));
	event->m_audio.fromXML(media);
	event->m_id = jbev->id();
	event->m_element = jbev->releaseXML();
	break;
    }
    if (trans != jingle)
	TelEngine::destruct(trans);
    TelEngine::destruct(media);
    if (!event)
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable);
    return event;
}

// Create an 'iq' stanza with a 'jingle' child
XMLElement* JGSession::createJingle(Action action, XMLElement* element1,
    XMLElement* element2, XMLElement* element3)
{
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,m_localJID,m_remoteJID,0);
    XMLElement* jingle = XMPPUtils::createElement(XMLElement::Jingle,
	XMPPNamespace::Jingle);
    if (action < ActCount)
	jingle->setAttribute("type",lookup(action,s_actions));
    jingle->setAttribute("initiator",outgoing()?m_localJID:m_remoteJID);
//    jingle->setAttribute("responder",outgoing()?m_remoteJID:m_localJID);
    jingle->setAttribute(m_sidAttr,m_sid);
    jingle->addChild(element1);
    jingle->addChild(element2);
    jingle->addChild(element3);
    iq->addChild(jingle);
    return iq;
}

// Send a transport related element to the remote peer
bool JGSession::sendTransport(JGTransport* transport, Action act, String* stanzaId)
{
    if (act != ActTransport && act != ActTransportAccept)
	return false;
    // Accept received transport
    if (act == ActTransportAccept) {
	TelEngine::destruct(transport);
	// Clients negotiating transport as 'candidates' don't expect transport-accept
	if (m_transportType == TransportCandidates)
	    return true;
	XMLElement* child = JGTransport::createTransport();
	return sendStanza(createJingle(ActTransportAccept,0,child),stanzaId);
    }
    // Sent transport
    if (!transport)
	return false;
    // TransportUnknown: send both transport types
    // TransportInfo: A 'transport' child element of the session element
    // TransportCandidates: Transport candidates are direct children of the 'session' element
    XMLElement* child = 0;
    bool ok = false;
    switch (m_transportType) {
	case TransportUnknown:
	    // Fallthrough to send both transport types
	case TransportInfo:
	    child = JGTransport::createTransport();
	    transport->addTo(child);
	    ok = sendStanza(createJingle(ActTransportInfo,0,child),stanzaId);
	    if (!ok || m_transportType == TransportInfo)
		break;
	    // Fallthrough to send candidates if unknown and succedded
	case TransportCandidates:
	    child = transport->toXML();
	    ok = sendStanza(createJingle(ActTransportCandidates,0,child),stanzaId);
    }
    TelEngine::destruct(transport);
    return ok;
}

// Event termination notification
void JGSession::eventTerminated(JGEvent* event)
{
    lock();
    if (event == m_lastEvent) {
	DDebug(m_engine,DebugAll,"Call(%s). Event (%p,%u) terminated [%p]",
	    m_sid.c_str(),event,event->type(),this);
	m_lastEvent = 0;
    }
    else if (m_lastEvent)
	Debug(m_engine,DebugNote,
	    "Call(%s). Event (%p,%u) replaced while processed [%p]",
	    m_sid.c_str(),event,event->type(),this);
    unlock();
}

// Change session state
void JGSession::changeState(State newState)
{
    if (m_state == newState)
	return;
    Debug(m_engine,DebugInfo,"Call(%s). Changing state from %s to %s [%p]",
	m_sid.c_str(),lookup(m_state,s_states),lookup(newState,s_states),this);
    m_state = newState;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
