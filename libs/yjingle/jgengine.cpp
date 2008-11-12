/**
 * jgengine.cpp
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

static XMPPError s_err;

TokenDict JGEvent::s_typeName[] = {
    {"Jingle",           Jingle},
    {"ResultOk",         ResultOk},
    {"ResultError",      ResultError},
    {"ResultWriteFail",  ResultWriteFail},
    {"ResultTimeout",    ResultTimeout},
    {"Terminated",       Terminated},
    {"Destroy",          Destroy},
    {0,0}
};


/**
 * JGEngine
 */
JGEngine::JGEngine(JBEngine* engine, const NamedList* params, int prio)
    : JBService(engine,"jgengine",params,prio), m_sessionIdMutex(true),
    m_sessionId(1), m_stanzaTimeout(20000), m_useSidAttr(false)
{
    JBThreadList::setOwner(this);
}

JGEngine::~JGEngine()
{
    cancelThreads();
}

// Create private stream(s) to get events from sessions
void JGEngine::initialize(const NamedList& params)
{
    int lvl = params.getIntValue("debug_level",-1);
    if (lvl != -1)
	debugLevel(lvl);

    int timeout = params.getIntValue("stanza_timeout",10);
    if (timeout < 10)
	timeout = 10;
    m_stanzaTimeout = timeout * 1000;
    m_useSidAttr = params.getBoolValue("session_sid",false);

    if (debugAt(DebugInfo)) {
	String s;
	s << " stanza_timeout=" << (unsigned int)m_stanzaTimeout;
	s << " session_sid=" << m_useSidAttr;
	Debug(this,DebugInfo,"Jabber Jingle service initialized:%s [%p]",
	    s.c_str(),this);
    }

    if (!m_initialized) {
	m_initialized = true;
	int c = params.getIntValue("private_process_threads",1);
	for (int i = 0; i < c; i++)
	    JBThread::start(JBThread::Jingle,this,this,2,Thread::Normal);
    }
}

// Make an outgoing call
JGSession* JGEngine::call(const String& localJID, const String& remoteJID,
	XMLElement* media, XMLElement* transport, XMLElement* extra,
	const char* message)
{
    DDebug(this,DebugAll,"New outgoing call from '%s' to '%s'",
	localJID.c_str(),remoteJID.c_str());

    // Get a stream from the engine
    JBStream* stream = 0;
    if (engine()->protocol() == JBEngine::Component)
	stream = engine()->getStream();
    else {
	// Client: the stream must be already created
	JabberID jid(localJID);
	stream = engine()->getStream(&jid,false);
    }

    // Create outgoing session
    if (stream) {
	JGSession* session = new JGSession(this,stream,localJID,remoteJID,
	    media,transport,m_useSidAttr,extra,message);
	if (session->state() != JGSession::Destroy) {
	    m_sessions.append(session);
	    return (session && session->ref() ? session : 0);
	}
	TelEngine::destruct(session);
    }
    else {
	TelEngine::destruct(media);
	TelEngine::destruct(transport);
	TelEngine::destruct(extra);
    }

    Debug(this,DebugNote,"Outgoing call from '%s' to '%s' failed: %s",
	localJID.c_str(),remoteJID.c_str(),
	!stream?"can't create stream":"failed to send data");
    return 0;
}

// Get events from sessions
JGEvent* JGEngine::getEvent(u_int64_t time)
{
    JGEvent* event = 0;
    lock();
    ListIterator iter(m_sessions);
    for (;;) {
	JGSession* session = static_cast<JGSession*>(iter.get());
	// End of iteration?
	if (!session)
	    break;
	RefPointer<JGSession> s = session;
	// Dead pointer?
	if (!s)
	    continue;
	unlock();
	if (0 != (event = s->getEvent(time)))
	    if (event->type() == JGEvent::Destroy) {
		DDebug(this,DebugAll,"Deleting internal event (%p,Destroy)",event);
		delete event;
	    }
	    else
		return event;
	lock();
    }
    unlock();
    return 0;
}

// Default event processor
void JGEngine::defProcessEvent(JGEvent* event)
{
    if (!event) 
	return;
    DDebug(this,DebugAll,"JGEngine::defprocessEvent. Deleting event (%p,%u)",
	event,event->type());
    delete event;
}

// Utility: get the (s)id attribute: the session id
static inline bool getSid(XMLElement* xml, String& sid, bool& useSid)
{
    if (!xml)
	return false;
    sid = xml->getAttribute("id");
    if (!sid) {
	sid = xml->getAttribute("sid");
	useSid = true;
    }
    return !sid.null();
}

// Accept an event from the Jabber engine
bool JGEngine::accept(JBEvent* event, bool& processed, bool& insert)
{
    if (!(event && event->stream()))
	return false;
    XMLElement* child = event->child();
    XMPPError::Type error = XMPPError::NoError;
    const char* errorText = 0;
    bool respond = true;
    String sid;
    bool useSid = false;
    Lock lock(this);
    switch (event->type()) {
	case JBEvent::IqJingleGet:
	    // Jingle stanzas should never have type='get'
	    Debug(this,DebugNote,"Received iq jingle stanza with type='get'");
	    error = XMPPError::SServiceUnavailable;
	    break;
	case JBEvent::IqJingleSet:
	    if (!(event->element() && child)) {
		Debug(this,DebugNote,"Received jingle event %s with no element or child",event->name());
		return false;
	    }
	    // Jingle clients may send the session id as 'id' or 'sid'
	    getSid(child,sid,useSid);
	    DDebug(this,DebugAll,"Accepting event=%s child=%s id=%s useSid=%s",
		event->name(),child->name(),sid.c_str(),String::boolText(useSid));
	    if (sid.null()) {
		error = XMPPError::SBadRequest;
		errorText = "Missing or empty session id";
		break;
	    }
	    // Check for a destination
	    for (ObjList* o = m_sessions.skipNull(); o; o = o->skipNext()) {
		JGSession* session = static_cast<JGSession*>(o->get());
		if (session->acceptEvent(event,sid)) {
		    processed = true;
		    return true;
		}
	    }
	    // Check if this an incoming session request
	    if (event->type() == JBEvent::IqJingleSet) {
		const char* type = event->child()->getAttribute("type");
		int action = lookup(type,JGSession::s_actions,JGSession::ActCount);
		if (action == JGSession::ActInitiate) {
		    if (!event->stream()->ref()) {
			error = XMPPError::SInternal;
			errorText = "Stream ref() failed";
			break;
		    }
		    DDebug(this,DebugAll,"New incoming call from=%s to=%s sid=%s",
			event->from().c_str(),event->to().c_str(),sid.c_str());
		    if (event->ref())
			m_sessions.append(new JGSession(this,event,sid,useSid));
		    processed = true;
		    return true;
		}
	    }
	    error = XMPPError::SRequest;
	    errorText = "Unknown session";
	    break;
	case JBEvent::IqJingleRes:
	case JBEvent::IqJingleErr:
	case JBEvent::IqResult:
	case JBEvent::IqError:
	case JBEvent::WriteFail:
	    respond = false;
	    for (ObjList* o = m_sessions.skipNull(); o; o = o->skipNext()) {
		JGSession* session = static_cast<JGSession*>(o->get());
		if (session->acceptEvent(event)) {
		    processed = true;
		    return true;
		}
	    }
	    break;
	case JBEvent::Terminated:
	case JBEvent::Destroy:
	    for (ObjList* o = m_sessions.skipNull(); o; o = o->skipNext()) {
		JGSession* session = static_cast<JGSession*>(o->get());
		if (event->stream() == session->stream())
		    session->enqueue(new JBEvent((JBEvent::Type)event->type(),
			event->stream(),0));
	    }
	    break;
	default:
	    return false;
    }
    if (error == XMPPError::NoError)
	return false;

    Debug(this,DebugNote,"Accepted event=%s child=%s. Invalid: error=%s text=%s",
	event->name(),child?child->name():"",s_err[error],errorText);

    // Send error
    if (respond) {
	XMLElement* iq = XMPPUtils::createError(event->releaseXML(),XMPPError::TypeModify,
	    error,errorText);
	event->stream()->sendStanza(iq);
    }
    processed = true;
    return true;
}

// Process generated events
void JGEngine::processEvent(JGEvent* event)
{
    Debug(this,DebugStub,"JGEngine::processEvent. Calling default processor");
    defProcessEvent(event);
}

// Create a local session id
void JGEngine::createSessionId(String& id)
{
    Lock lock(m_sessionIdMutex);
    id = "JG";
    id << (unsigned int)m_sessionId << "_" << (int)random();
    m_sessionId++;
}


/**
 * JGEvent
 */
JGEvent::~JGEvent()
{
    if (m_session) {
	m_session->eventTerminated(this);
	TelEngine::destruct(m_session);
    }
    TelEngine::destruct(releaseXML());
    XDebug(DebugAll,"JGEvent::~JGEvent [%p]",this);
}

void JGEvent::init(JGSession* session)
{
    XDebug(DebugAll,"JGEvent::JGEvent [%p]",this);
    if (session && session->ref())
	m_session = session;
    if (m_element) {
	m_id = m_element->getAttribute("id");
	m_jingle = m_element->findFirstChild(XMLElement::Jingle);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
