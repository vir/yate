/**
 * transaction.cpp
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

SIPTransaction::SIPTransaction(SIPMessage* message, SIPEngine* engine, bool outgoing)
    : m_outgoing(outgoing), m_invite(false), m_transmit(false), m_state(Invalid), m_timeout(0),
      m_firstMessage(message), m_lastMessage(0), m_engine(engine)
{
    Debug(DebugAll,"SIPTransaction::SIPTransaction(%p,%p) [%p]",message,engine,this);
    if (m_firstMessage) {
	m_firstMessage->ref();
	const NamedString* br = message->getParam("Via","branch");
	if (br)
	    m_branch = *br;
	if (!m_branch.startsWith("z9hG4bK"))
	    m_branch.clear();
    }
    m_invite = (getMethod() == "INVITE");
    m_engine->TransList.append(this);
    m_state = Initial;
}

SIPTransaction::~SIPTransaction()
{
    Debugger debug(DebugAll,"SIPTransaction::~SIPTransaction()"," [%p]",this);
    m_state = Invalid;
    m_engine->TransList.remove(this,false);
    if (m_lastMessage)
	m_lastMessage->deref();
    m_lastMessage = 0;
    if (m_firstMessage)
	m_firstMessage->deref();
    m_firstMessage = 0;
}

bool SIPTransaction::changeState(int newstate)
{
    if ((newstate < 0) || (newstate == m_state))
	return false;
    if (m_state == Invalid) {
	Debug("SIPTransaction",DebugGoOn,"Transaction is already invalid [%p]",this);
	return false;
    }
    Debug("SIPTransaction",DebugAll,"State changed from %d to %d [%p]",m_state,newstate,this);
    m_state = newstate;
    return true;
}

void SIPTransaction::setLatestMessage(SIPMessage* message)
{
    if (m_lastMessage == message)
	return;
    if (m_lastMessage)
	m_lastMessage->deref();
    m_lastMessage = message;
    if (m_lastMessage) {
	m_lastMessage->ref();
	m_lastMessage->complete(m_engine);
    }
}

void SIPTransaction::setTimeout(unsigned long long delay, unsigned int count)
{
    m_timeouts = count;
    m_delay = delay;
    m_timeout = (count && delay) ? Time::now() + delay : 0;
}

SIPEvent* SIPTransaction::getEvent()
{
    if (m_transmit) {
	m_transmit = false;
	if (m_lastMessage)
	    return new SIPEvent(m_lastMessage,this);
    }
    int timeout = -1;
    if (m_timeout && Time::now() >= m_timeout) {
	timeout = --m_timeouts;
	m_timeout = (m_timeouts) ? Time::now() + m_delay : 0;
	Debug("SIPTransaction",DebugAll,"Fired timer #%d [%p]",timeout,this);
    }
    SIPEvent *e = getEvent(m_state,timeout);
    if (e)
	return e;
    switch (m_state) {
	case Initial:
	    setLatestMessage(new SIPMessage(m_firstMessage, 501, "Not implemented"));
	    m_lastMessage->deref();
	    e = new SIPEvent(m_lastMessage,this);
	    changeState(Cleared);
	    break;
	case Trying:
	    e = new SIPEvent(m_firstMessage,this);
	    changeState(Process);
	    setTimeout(m_engine->getTimer('B'));
	    break;
	case Process:
	    if (timeout < 0)
		break;
	    if (timeout && m_lastMessage)
		e = new SIPEvent(m_lastMessage,this);
	    else if (!timeout)
		changeState(isOutgoing() ? Cleared : Timeout);
	    break;
	case Retrans:
	    if (timeout < 0)
		break;
	    if (timeout && m_lastMessage)
		e = new SIPEvent(m_lastMessage,this);
	    else if (!timeout)
		changeState(Cleared);
	    break;
	case Timeout:
	    setTimeout(m_engine->getTimer('G'),3);
	    setLatestMessage(new SIPMessage(m_firstMessage, 408, "Request Timeout"));
	    m_lastMessage->deref();
	case Finish:
	    e = new SIPEvent(m_lastMessage,this);
	    changeState(Retrans);
	    break;
	case Cleared:
	    setTimeout();
	    e = new SIPEvent(m_firstMessage,this);
	    // make sure we don't get trough this one again
	    changeState(Invalid);
	    // remove from list and dereference
	    m_engine->TransList.remove(this);
	    return e;
	case Invalid:
	    Debug("SIPTransaction",DebugFail,"getEvent in invalid state [%p]",this);
	    break;
    }
    return e;
}

void SIPTransaction::setResponse(SIPMessage* message)
{
    setLatestMessage(message);
    m_lastMessage->deref();
    changeState(Finish);
}

void SIPTransaction::setResponse(int code, const char* reason)
{
    setResponse(new SIPMessage(m_firstMessage, code, reason));
}

bool SIPTransaction::processMessage(SIPMessage* message, const String& branch)
{
    Debug("SIPTransaction",DebugAll,"processMessage(%p,'%s') [%p]",
	message,branch.c_str(),this);
    if (branch) {
	if (branch != m_branch)
	    return false;
	if (getMethod() != message->method) {
	    if (!isInvite() || (message->method != "ACK"))
		return false;
	}
    }
    else {
	Debug("SIPTransaction",DebugWarn,"Non-branch matching not implemented!");
	return false;
    }
    if (message->method == "ACK") {
	if (changeState(Retrans))
	    setTimeout(m_engine->getTimer('I'));
	return true;
    }
    setTransmit();
    return true;
}

SIPEvent* SIPTransaction::getEvent(int state, int timeout)
{
    SIPEvent *e = 0;
    switch (state) {
	case Initial:
	    if (getMethod() == "INVITE") {
		setLatestMessage(new SIPMessage(m_firstMessage, 100, "Trying"));
		m_lastMessage->deref();
		e = new SIPEvent(m_lastMessage,this);
		changeState(Trying);
	    }
	    break;
    }
    return e;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
