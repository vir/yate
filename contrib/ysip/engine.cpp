/**
 * engine.cpp
 * Yet Another SIP Stack
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <telengine.h>

#include <string.h>
#include <stdlib.h>

#include <ysip.h>

using namespace TelEngine;

SIPParty::SIPParty()
{
    Debug(DebugAll,"SIPParty::SIPParty() [%p]",this);
}

SIPParty::~SIPParty()
{
    Debug(DebugAll,"SIPParty::~SIPParty() [%p]",this);
}

SIPEvent::SIPEvent(SIPMessage* message, SIPTransaction* transaction)
    : m_message(message), m_transaction(transaction),
      m_state(SIPTransaction::Invalid)
{
    Debug(DebugAll,"SIPEvent::SIPEvent(%p,%p) [%p]",message,transaction,this);
    if (m_message)
	m_message->ref();
    if (m_transaction) {
	m_transaction->ref();
	m_state = m_transaction->getState();
    }
}

SIPEvent::~SIPEvent()
{
    Debugger debug(DebugAll,"SIPEvent::~SIPEvent"," [%p]",this);
    if (m_transaction)
	m_transaction->deref();
    if (m_message)
	m_message->deref();
}

SIPEngine::SIPEngine()
{
    Debug(DebugInfo,"SIPEngine::SIPEngine() [%p]",this);
}

SIPEngine::~SIPEngine()
{
    Debug(DebugInfo,"SIPEngine::~SIPEngine() [%p]",this);
}

bool SIPEngine::addMessage(SIPParty* ep, const char *buf, int len)
{
    Debug("SIPEngine",DebugInfo,"addMessage(%p,%d) [%p]",buf,len,this);
    SIPMessage* msg = SIPMessage::fromParsing(ep,buf,len);
    if (ep)
	ep->deref();
    if (msg) {
	bool ok = addMessage(msg);
	msg->deref();
	return ok;
    }
    return false;
}

bool SIPEngine::addMessage(SIPMessage* message)
{
    Debug("SIPEngine",DebugInfo,"addMessage(%p) [%p]",message,this);
    Lock lock(m_mutex);
    ObjList* l = &TransList;
    for (; l; l = l->next()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	if (t && t->processMessage(message))
	    return true;
    }
    if (message->isAnswer()) {
	Debug("SIPEngine",DebugInfo,"Message %p was an unhandled answer [%p]",message,this);
	return false;
    }
    new SIPTransaction(message,this,false);
    return true;
}

bool SIPEngine::process()
{
    SIPEvent* e = getEvent();
    if (!e)
	return false;
    Debug("SIPEngine",DebugInfo,"process() got event %p",e);
    processEvent(e);
    return true;
}

SIPEvent* SIPEngine::getEvent()
{
    Lock lock(m_mutex);
    ObjList* l = &TransList;
    for (; l; l = l->next()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	if (t) {
	    SIPEvent* e = t->getEvent();
	    if (e)
		return e;
	}
    }
    return 0;
}

void SIPEngine::processEvent(SIPEvent *event)
{
    Lock lock(m_mutex);
    if (event) {
	if (event->isOutgoing() && event->getParty())
	    event->getParty()->transmit(event);
	delete event;
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
