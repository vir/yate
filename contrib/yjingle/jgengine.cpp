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

/**
 * JGEngine
 */
JGEngine::JGEngine(JBEngine* jb, const NamedList& params)
    : JBClient(jb),
      Mutex(true),
      m_sessionIdMutex(true),
      m_sessionId(1)
{
    debugName("jgengine");
    initialize(params);
    XDebug(this,DebugAll,"JGEngine. [%p]",this);
}

JGEngine::~JGEngine()
{
    XDebug(this,DebugAll,"~JGEngine. [%p]",this);
}

void JGEngine::initialize(const NamedList& params)
{
}

JGSession* JGEngine::call(const String& localJID, const String& remoteJID,
	XMLElement* media, XMLElement* transport, const char* message)
{
    DDebug(this,DebugAll,"call. New outgoing call from '%s' to '%s'.",
	localJID.c_str(),remoteJID.c_str());
    JBComponentStream* stream = m_engine->getStream();
    if (stream) {
	// Create outgoing session
	JGSession* session = new JGSession(this,stream,localJID,remoteJID);
	if (session->state() != JGSession::Destroy) {
	    if (message)
		session->sendMessage(message);
	    session->initiate(media,transport);
	    m_sessions.append(session);
	    return (session && session->ref() ? session : 0);
	}
	session->deref();
    }
    DDebug(this,DebugCall,"call. Outgoing call to '%s' failed. No stream.",remoteJID.c_str());
    return 0;
}

bool JGEngine::receive()
{
    Lock lock(this);
    JBEvent* event = m_engine->getEvent(Time::msecNow());
    if (event) {
	// Check for new incoming session
	if (event->type() == JBEvent::IqJingleSet) {
	    const char* type = event->child()->getAttribute("type");
	    JGSession::Action action = JGSession::action(type);
	    if (action == JGSession::ActInitiate) {
		m_sessions.append(new JGSession(this,event));
		return true;
	    }
	}
	// Add event to the appropriate session
	ObjList* obj = m_sessions.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    JGSession* session = static_cast<JGSession*>(obj->get());
	    if (session->receive(event))
		return true;
	}
	m_engine->returnEvent(event);
    }
    return false;
}

void JGEngine::runReceive()
{
    while(1) {
	if (!receive())
	    Thread::msleep(2,true);
    }
}

bool JGEngine::process()
{
    bool ok = false;
    for (;;) {
	JGEvent* event = getEvent(Time::msecNow());
	if (!event)
	    break;
	ok = true;
	if (event->type() == JGEvent::Destroy) {
	    DDebug(this,DebugAll,"Deleting internal event(%p) 'Destroy'.",event);
	    delete event;
	    continue;
	}
	processEvent(event);
    }
    return ok;
}

void JGEngine::runProcess()
{
    while(1) {
	if (!process())
	    Thread::msleep(2,true);
    }
}

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
	    return event;
	lock();
    }
    unlock();
    return 0;
}

void JGEngine::defProcessEvent(JGEvent* event)
{
    if (!event) 
	return;
    DDebug(this,DebugAll,"JGEngine::defprocessEvent. Deleting event(%p). Type %u.",
	event,event->type());
    delete event;
}

void JGEngine::createSessionId(String& id)
{
    Lock lock(m_sessionIdMutex);
    id = "JG";
    id << (unsigned int)m_sessionId << "_" << (int)random();
    m_sessionId++;
}

void JGEngine::processEvent(JGEvent* event)
{
    DDebug(this,DebugAll,"JGEngine::processEvent. Call default.");
    defProcessEvent(event);
}

void JGEngine::removeSession(JGSession* session)
{
    if (!session)
	return;
    Lock lock(this);
    m_sessions.remove(session,false);
}

/**
 * JGEvent
 */
JGEvent::JGEvent(Type type, JGSession* session, XMLElement* element)
    : m_type(type),
      m_session(0),
      m_element(element),
      m_action(JGSession::ActCount)

{
    XDebug(DebugAll,"JGEvent::JGEvent [%p].",this);
    if (session && session->ref())
	m_session = session;
}

JGEvent::~JGEvent()
{
    if (m_session) {
	m_session->eventTerminated(this);
	m_session->deref();
    }
    if (m_element)
	delete m_element;
    XDebug(DebugAll,"JGEvent::~JGEvent [%p].",this);
}

bool JGEvent::final()
{
// Check: Terminated, Destroy
    switch (type()) {
	case Terminated:
	case Destroy:
	    return true;
	default: ;
    }
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
