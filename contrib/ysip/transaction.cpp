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
      m_firstMessage(message), m_lastMessage(0), m_pending(0), m_engine(engine), m_private(0)
{
    Debug(DebugAll,"SIPTransaction::SIPTransaction(%p,%p,%d) [%p]",
	message,engine,outgoing,this);
    if (m_firstMessage) {
	m_firstMessage->ref();

	const NamedString* ns = message->getParam("Via","branch");
	if (ns)
	    m_branch = *ns;
	if (!m_branch.startsWith("z9hG4bK"))
	    m_branch.clear();
	ns = message->getParam("To","tag");
	if (ns)
	    m_tag = *ns;

	const HeaderLine* hl = message->getHeader("Call-ID");
	if (hl)
	    m_callid = *hl;

	if (!m_outgoing && m_firstMessage->getParty()) {
	    // adjust the address where we send the answers
	    hl = message->getHeader("Via");
	    if (hl) {
		URI uri(*hl);
		// skip over protocol/version/transport
		uri >> "/" >> "/" >> " ";
		uri.trimBlanks();
		uri = "sip:" + uri;
		m_firstMessage->getParty()->setParty(uri);
	    }
	}
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
    setPendingEvent();
    if (m_lastMessage)
	m_lastMessage->deref();
    m_lastMessage = 0;
    if (m_firstMessage)
	m_firstMessage->deref();
    m_firstMessage = 0;
}

const char* SIPTransaction::stateName(int state)
{
    switch (state) {
	case Invalid:
	    return "Invalid";
	case Initial:
	    return "Initial";
	case Trying:
	    return "Trying";
	case Process:
	    return "Process";
	case Retrans:
	    return "Retrans";
	case Finish:
	    return "Finish";
	case Cleared:
	    return "Cleared";
	default:
	    return "Undefined";
    }
}

bool SIPTransaction::changeState(int newstate)
{
    if ((newstate < 0) || (newstate == m_state))
	return false;
    if (m_state == Invalid) {
	Debug("SIPTransaction",DebugGoOn,"Transaction is already invalid [%p]",this);
	return false;
    }
    Debug("SIPTransaction",DebugAll,"State changed from %s to %s [%p]",
	stateName(m_state),stateName(newstate),this);
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
	if (message->isAnswer() && (message->code > 100) && m_tag.null())
	    m_tag = (int)::random();
	message->complete(m_engine,0,0,m_tag);
    }
}

void SIPTransaction::setPendingEvent(SIPEvent* event, bool replace)
{
    if (m_pending)
	if (replace) {
	    delete m_pending;
	    m_pending = event;
	}
	else
	    delete event;
    else
	m_pending = event;
}

void SIPTransaction::setTimeout(unsigned long long delay, unsigned int count)
{
    m_timeouts = count;
    m_delay = delay;
    m_timeout = (count && delay) ? Time::now() + delay : 0;
}

SIPEvent* SIPTransaction::getEvent()
{
    SIPEvent *e = 0;

    if (m_pending) {
	e = m_pending;
	m_pending = 0;
	return e;
    }

    if (m_transmit) {
	m_transmit = false;
	return new SIPEvent(m_lastMessage ? m_lastMessage : m_firstMessage,this);
    }

    int timeout = -1;
    if (m_timeout && (Time::now() >= m_timeout)) {
	timeout = --m_timeouts;
	m_timeout = (m_timeouts) ? Time::now() + m_delay : 0;
	m_delay *= 2; // exponential back-off
	Debug("SIPTransaction",DebugAll,"Fired timer #%d [%p]",timeout,this);
    }

    e = isOutgoing() ? getClientEvent(m_state,timeout) : getServerEvent(m_state,timeout);
    if (e)
	return e;

    // do some common default processing
    switch (m_state) {
	case Retrans:
	    if (timeout < 0)
		break;
	    if (timeout && m_lastMessage)
		e = new SIPEvent(m_lastMessage,this);
	    if (timeout)
		break;
	    changeState(Cleared);
	    // fall trough so we don't wait another turn for processing
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
    if (m_outgoing) {
	Debug(DebugWarn,"setResponse(%p) in client transaction [%p]",message,this);
	return;
    }
    setLatestMessage(message);
    setTransmit();
    if (message && (message->code >= 200)) {
	if (isInvite()) {
	    if (changeState(Finish))
		setTimeout();
	}
	else {
	    setTimeout();
	    changeState(Cleared);
	}
    }
}

void SIPTransaction::setResponse(int code, const char* reason)
{
    if (m_outgoing) {
	Debug(DebugWarn,"setResponse(%d,'%s') in client transaction [%p]",code,reason,this);
	return;
    }
    switch (m_state) {
	case Invalid:
	case Retrans:
	case Finish:
	case Cleared:
	    Debug("SIPTransaction",DebugInfo,"Ignoring setResponse(%d) in state %s [%p]",
		code,stateName(m_state),this);
	    return;
    }
    SIPMessage* msg = new SIPMessage(m_firstMessage, code, reason);
    setResponse(msg);
    msg->deref();
}

bool SIPTransaction::processMessage(SIPMessage* message, const String& branch)
{
    if (!message)
	return false;
    if (isOutgoing() != message->isAnswer())
	return false;
    DDebug("SIPTransaction",DebugAll,"processMessage(%p,'%s') [%p]",
	message,branch.c_str(),this);
    if (branch) {
	if (branch != m_branch) {
	    // different branch is allowed only for ACK in incoming INVITE...
	    if (!(isInvite() && isIncoming() && message->isACK()))
		return false;
	    // ...and only if we sent a 200 response...
	    if (!m_lastMessage || ((m_lastMessage->code / 100) != 2))
		return false;
	    // ...and if also matches the CSeq, Call-ID and To: tag
	    if ((m_firstMessage->getCSeq() != message->getCSeq()) ||
		(getCallID() != message->getHeaderValue("Call-ID")) ||
		(getDialogTag() != message->getParamValue("To","tag")))
		return false;
	    Debug("SIPTransaction",DebugInfo,"Found non-branch ACK response to our 2xx");
	}
	else if (getMethod() != message->method) {
	    if (!(isIncoming() && isInvite() && message->isACK()))
		return false;
	}
    }
    else {
	Debug("SIPTransaction",DebugWarn,"Non-branch matching not implemented!");
	return false;
    }
    Debug("SIPTransaction",DebugAll,"Processing %s %p '%s' in [%p]",
	message->isAnswer() ? "answer" : "request",
	message,message->method.c_str(),this);

    if (m_tag.null() && message->isAnswer()) {
	const NamedString* ns = message->getParam("To","tag");
	if (ns) {
	    m_tag = *ns;
	    Debug("SIPTransaction",DebugInfo,"Found dialog tag '%s' [%p]",
		m_tag.c_str(),this);
	}
    }

    if (isOutgoing())
	processClientMessage(message,m_state);
    else
	processServerMessage(message,m_state);
    return true;
}

void SIPTransaction::processClientMessage(SIPMessage* message, int state)
{
    switch (state) {
	case Trying:
	    setTimeout(m_engine->getTimer(isInvite() ? 'B' : 'F'));
	    changeState(Process);
	    if (message->code == 100)
		break;
	    // fall trough for non-100 answers
	case Process:
	    if (message->code > 100)
		setPendingEvent(new SIPEvent(message,this));
	    if (message->code >= 200) {
		setTimeout();
		if (isInvite()) {
		    setLatestMessage(new SIPMessage(m_firstMessage,(message->code == 200)));
		    m_lastMessage->deref();
		    setTransmit();
		    if (changeState(Retrans))
			setTimeout(m_engine->getTimer('I'));
		}
		else
		    changeState(Cleared);
	    }
	    break;
	case Retrans:
	    if (m_lastMessage && m_lastMessage->isACK())
		setTransmit();
	    break;
    }
}

SIPEvent* SIPTransaction::getClientEvent(int state, int timeout)
{
    SIPEvent *e = 0;
    switch (state) {
	case Initial:
	    e = new SIPEvent(m_firstMessage,this);
	    if (changeState(Trying))
		setTimeout(m_engine->getTimer(isInvite() ? 'A' : 'E'),5);
	    break;
	case Trying:
	    if (timeout < 0)
		break;
	    if (timeout)
		setTransmit();
	    else
		changeState(Cleared);
	    break;
	case Process:
	    if (timeout == 0)
		changeState(Cleared);
	    break;
	case Finish:
	    setTimeout();
	    changeState(Cleared);
	    break;
    }
    return e;
}

void SIPTransaction::processServerMessage(SIPMessage* message, int state)
{
    switch (state) {
	case Trying:
	case Process:
	    setTransmit();
	    break;
	case Finish:
	case Retrans:
	    if (message->isACK()) {
		setTimeout();
		setPendingEvent(new SIPEvent(message,this));
		changeState(Cleared);
	    }
	    else
		setTransmit();
	    break;
    }
}

SIPEvent* SIPTransaction::getServerEvent(int state, int timeout)
{
    SIPEvent *e = 0;
    switch (state) {
	case Initial:
	    if (m_engine->isAllowed(m_firstMessage->method)) {
		setResponse(100, "Trying");
		changeState(Trying);
	    }
	    else {
		setResponse(405, "Method Not Allowed");
		e = new SIPEvent(m_lastMessage,this);
		m_transmit = false;
		changeState(Invalid);
		// remove from list and dereference
		m_engine->TransList.remove(this);
	    }
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
	    if (timeout)
		break;
	    setResponse(408, "Request Timeout");
	    break;
	case Finish:
	    e = new SIPEvent(m_lastMessage,this);
	    setTimeout(m_engine->getTimer('G'),5);
	    changeState(Retrans);
	    break;
    }
    return e;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
