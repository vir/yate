/**
 * jgengine.cpp
 * Yet Another Jingle Stack
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

#include <yatejingle.h>
#include <stdlib.h>

using namespace TelEngine;

const TokenDict JGEvent::s_typeName[] = {
    {"Jingle",           Jingle},
    {"ResultOk",         ResultOk},
    {"ResultError",      ResultError},
    {"ResultTimeout",    ResultTimeout},
    {"Terminated",       Terminated},
    {"Destroy",          Destroy},
    {0,0}
};


/*
 * JGEngine
 */
// Constructor
JGEngine::JGEngine(const char* name)
    : Mutex(true,"JGEngine"),
    m_sessionId(1), m_stanzaTimeout(20000), m_streamHostTimeout(180000),
    m_pingInterval(300000)
{
    debugName(name);
}

JGEngine::~JGEngine()
{
}

// Create private stream(s) to get events from sessions
void JGEngine::initialize(const NamedList& params)
{
    int lvl = params.getIntValue("debug_level",-1);
    if (lvl != -1)
	debugLevel(lvl);

    m_sessionFlags = 0;
    m_sessionFlags = decodeFlags(params["jingle_flags"],JGSession::s_flagName);
    m_stanzaTimeout = params.getIntValue("stanza_timeout",20000,10000);
    m_streamHostTimeout = params.getIntValue("stanza_timeout",180000,60000);
    int ping = params.getIntValue("ping_interval",(int)m_pingInterval);
    if (ping == 0)
	m_pingInterval = 0;
    else if (ping < 60000)
	m_pingInterval = 60000;
    else
	m_pingInterval = ping;
    // Make sure we don't ping before a ping times out
    if (m_pingInterval && m_stanzaTimeout && m_pingInterval <= m_stanzaTimeout)
	m_pingInterval = m_stanzaTimeout + 100;

    if (debugAt(DebugAll)) {
	String s;
	s << " jingle_flags=" << m_sessionFlags;
	s << " stanza_timeout=" << (unsigned int)m_stanzaTimeout;
	s << " ping_interval=" << (unsigned int)m_pingInterval;
	Debug(this,DebugAll,"Jingle engine initialized:%s [%p]",s.c_str(),this);
    }
}

// Make an outgoing call
JGSession* JGEngine::call(JGSession::Version ver, const JabberID& caller,
    const JabberID& called, const ObjList& contents, XmlElement* extra,
    const char* msg, const char* subject, const char* line, int* flags)
{
    DDebug(this,DebugAll,"call() from '%s' to '%s'",caller.c_str(),called.c_str());
    JGSession* session = 0;
    switch (ver) {
	case JGSession::Version1:
	    session = new JGSession1(this,caller,called);
	    break;
	case JGSession::Version0:
	    session = new JGSession0(this,caller,called);
	    break;
	case JGSession::VersionUnknown:
	    Debug(this,DebugNote,"Outgoing call from '%s' to '%s' failed: unknown version %d",
		caller.c_str(),called.c_str(),ver);
	    return 0;
    }
    if (session) {
	if (flags)
	    session->setFlags(*flags);
	session->line(line);
	if (!TelEngine::null(msg))
	    sendMessage(session,msg);
	if (session->initiate(contents,extra,subject)) {
	    Lock lock(this);
	    m_sessions.append(session);
	    return (session && session->ref()) ? session : 0;
	}
    }
    TelEngine::destruct(session);
    Debug(this,DebugNote,"Outgoing call from '%s' to '%s' failed to initiate",
	caller.c_str(),called.c_str());
    return 0;
}

// Send a session's stanza.
bool JGEngine::sendStanza(JGSession* session, XmlElement*& stanza)
{
    Debug(this,DebugStub,"JGEngine::sendStanza() not implemented!");
    TelEngine::destruct(stanza);
    return false;
}

// Send a chat message on behalf of a session
bool JGEngine::sendMessage(JGSession* session, const char* body)
{
    XmlElement* x = XMPPUtils::createMessage(XMPPUtils::Chat,0,0,0,body);
    return sendStanza(session,x);
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
	if (0 != (event = s->getEvent(time))) {
	    if (event->type() == JGEvent::Destroy) {
		DDebug(this,DebugAll,"Deleting internal event (%p,Destroy)",event);
		delete event;
	    }
	    else
		return event;
	}
	lock();
    }
    unlock();
    return 0;
}

// Ask this engine to accept an incoming xml 'iq' element
bool JGEngine::acceptIq(XMPPUtils::IqType type, const JabberID& from, const JabberID& to,
    const String& id, XmlElement* xml, const char* line, XMPPError::Type& error,
    String& text)
{
    error = XMPPError::NoError;
    if (!xml)
	return false;
    if (type == XMPPUtils::IqResult || type == XMPPUtils::IqError) {
	Lock lock(this);
	for (ObjList* o = m_sessions.skipNull(); o; o = o->skipNext()) {
	    JGSession* session = static_cast<JGSession*>(o->get());
	    if (session->acceptIq(type,from,to,id,xml))
		return true;
	}
	return false;
    }
    if (type != XMPPUtils::IqGet && type != XMPPUtils::IqSet)
	return false;
    // Handle set/get iq
    XmlElement* child = xml->findFirstChild();
    if (!child)
	return false;
    String sid;
    JGSession::Version ver = JGSession::VersionUnknown;
    int ns = XMPPUtils::xmlns(*child);
    bool fileTransfer = false;
    // Jingle or file transfer stanzas (jingle stanzas can only have type='set')
    // Set version and session id
    if (ns == XMPPNamespace::Jingle) {
	if (type == XMPPUtils::IqSet) {
	    ver = JGSession::Version1;
	    sid = child->getAttribute("sid");
	}
    }
    else if (ns == XMPPNamespace::JingleSession) {
	if (type == XMPPUtils::IqSet) {
	    ver = JGSession::Version0;
	    sid = child->getAttribute("id");
	}
    }
    else if (ns == XMPPNamespace::ByteStreams && XMPPUtils::isUnprefTag(*child,XmlTag::Query)) {
	fileTransfer = true;
	sid = child->getAttribute("sid");
    }
    else
	return false;
    if (!sid) {
	if (!fileTransfer) {
	    error = XMPPError::BadRequest;
	    if (type == XMPPUtils::IqSet)
		text = "Missing session id attribute";
	}
	return false;
    }
    Lock lock(this);
    DDebug(this,DebugAll,"Accepting xml child=%s sid=%s version=%d filetransfer=%u",
	child->tag(),sid.c_str(),ver,fileTransfer);
    // Check for an existing session destination
    for (ObjList* o = m_sessions.skipNull(); o; o = o->skipNext()) {
	JGSession* session = static_cast<JGSession*>(o->get());
	if (session->acceptIq(type,from,to,sid,xml))
	    return true;
    }
    // Check if this an incoming session request
    JGSession* session = 0;
    if (ver != JGSession::VersionUnknown) {
	JGSession::Action action = JGSession::lookupAction(child->attribute("action"),ver);
	if (action == JGSession::ActCount)
	    action = JGSession::lookupAction(child->attribute("type"),ver);
	if (action == JGSession::ActInitiate) {
	    switch (ver) {
		case JGSession::Version1:
		    session = new JGSession1(this,to,from,xml,sid);
		    break;
		case JGSession::Version0:
		    session = new JGSession0(this,to,from,xml,sid);
		    break;
		default:
		    error = XMPPError::ServiceUnavailable;
		    Debug(this,DebugNote,
			"Can't accept xml child=%s sid=%s with unhandled version %d",
			child->tag(),sid.c_str(),ver);
	    }
	}
	else {
	    error = XMPPError::Request;
	    text = "Unknown session";
	}
	if (session) {
	    session->line(line);
	    m_sessions.append(session);
	}
	return error == XMPPError::NoError;
    }
    Debug(this,DebugNote,"Can't accept xml child=%s sid=%s with unknown version %d",
	child->tag(),sid.c_str(),ver);
    return false;
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

// Process generated events
void JGEngine::processEvent(JGEvent* event)
{
    Debug(this,DebugStub,"JGEngine::processEvent. Calling default processor");
    defProcessEvent(event);
}

// Decode a comma separated list of flags
int JGEngine::decodeFlags(const String& list, const TokenDict* dict)
{
    if (!(list && dict))
	return 0;
    int ret = 0;
    ObjList* l = list.split(',',false);
    for (; dict->token; dict++)
	if (l->find(dict->token))
	    ret += dict->value;
    TelEngine::destruct(l);
    return ret;
}

// Encode flags to a comma separated list
void JGEngine::encodeFlags(String& buf, int flags, const TokenDict* dict)
{
    if (!(flags && dict))
	return;
    for (; dict->token; dict++)
	if (0 != (flags & dict->value))
	    buf.append(dict->token,",");
}

// Create a local session id
void JGEngine::createSessionId(String& id)
{
    Lock lock(this);
    id = "JG";
    id << (unsigned int)m_sessionId << "_" << (int)Random::random();
    m_sessionId++;
}


/**
 * JGEvent
 */
JGEvent::~JGEvent()
{
    if (m_session) {
	if (!m_confirmed)
	    confirmElement(XMPPError::UndefinedCondition,"Unhandled");
	m_session->eventTerminated(this);
	TelEngine::destruct(m_session);
    }
    TelEngine::destruct(releaseXml());
    XDebug(DebugAll,"JGEvent::~JGEvent [%p]",this);
}

void JGEvent::init(JGSession* session)
{
    XDebug(DebugAll,"JGEvent::JGEvent [%p]",this);
    if (session && session->ref())
	m_session = session;
    if (m_element) {
	m_id = m_element->getAttribute("id");
	if (m_session)
	    switch (m_session->version()) {
		case JGSession::Version1:
		    m_jingle = XMPPUtils::findFirstChild(*m_element,XmlTag::Jingle);
		    break;
		case JGSession::Version0:
		    m_jingle = XMPPUtils::findFirstChild(*m_element,XmlTag::Session);
		    break;
		case JGSession::VersionUnknown:
		    ;
	    }
    }
}

// Set the jingle action as enumeration. Set confirmation flag if
//   the element don't require it
void JGEvent::setAction(JGSession::Action act)
{
    m_action = act;
    m_confirmed = !(m_element && act != JGSession::ActCount);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
