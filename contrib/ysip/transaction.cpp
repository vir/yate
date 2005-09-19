/**
 * transaction.cpp
 * Yet Another SIP Stack
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

#include <yatesip.h>

#include <string.h>
#include <stdlib.h>


using namespace TelEngine;

SIPTransaction::SIPTransaction(SIPMessage* message, SIPEngine* engine, bool outgoing)
    : m_outgoing(outgoing), m_invite(false), m_transmit(false), m_state(Invalid), m_response(0), m_timeout(0),
      m_firstMessage(message), m_lastMessage(0), m_pending(0), m_engine(engine), m_private(0)
{
    DDebug(DebugAll,"SIPTransaction::SIPTransaction(%p,%p,%d) [%p]",
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

	const SIPHeaderLine* hl = message->getHeader("Call-ID");
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
#ifdef DEBUG
    Debugger debug(DebugAll,"SIPTransaction::~SIPTransaction()"," [%p]",this);
#endif
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

Mutex* SIPTransaction::mutex()
{
    return m_engine ? m_engine->mutex() : 0;
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
	Debug(DebugGoOn,"SIPTransaction is already invalid [%p]",this);
	return false;
    }
    DDebug(DebugAll,"SIPTransaction state changed from %s to %s [%p]",
	stateName(m_state),stateName(newstate),this);
    m_state = newstate;
    return true;
}

void SIPTransaction::setDialogTag(const char* tag)
{
    if (null(tag)) {
	if (m_tag.null())
	    m_tag = (int)::random();
    }
    else
	m_tag = tag;
}

void SIPTransaction::setLatestMessage(SIPMessage* message)
{
    if (m_lastMessage == message)
	return;
    DDebug(DebugAll,"SIPTransaction latest message changing from %p %d to %p %d [%p]",
	m_lastMessage, m_lastMessage ? m_lastMessage->code : 0,
	message, message ? message->code : 0, this);
    if (m_lastMessage)
	m_lastMessage->deref();
    m_lastMessage = message;
    if (m_lastMessage) {
	m_lastMessage->ref();
	if (message->isAnswer()) {
	    m_response = message->code;
	    if (m_response > 100)
		setDialogTag();
	}
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

void SIPTransaction::setTimeout(u_int64_t delay, unsigned int count)
{
    m_timeouts = count;
    m_delay = delay;
    m_timeout = (count && delay) ? Time::now() + delay : 0;
#ifdef DEBUG
    if (m_timeout)
	Debug(DebugAll,"SIPTransaction new %d timeouts initially " FMT64U " usec apart [%p]",
	    m_timeouts,m_delay,this);
#endif
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
	DDebug(DebugAll,"SIPTransaction fired timer #%d [%p]",timeout,this);
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
	    Debug(DebugFail,"SIPTransaction::getEvent in invalid state [%p]",this);
	    break;
    }
    return e;
}

void SIPTransaction::setResponse(SIPMessage* message)
{
    if (m_outgoing) {
	Debug(DebugWarn,"SIPTransaction::setResponse(%p) in client mode [%p]",message,this);
	return;
    }
    Lock lock(mutex());
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
    // extend timeout for provisional messages, use proxy timeout (maximum)
    else if (message && (message->code > 100))
	setTimeout(m_engine->getTimer('C'));
}

bool SIPTransaction::setResponse(int code, const char* reason)
{
    if (m_outgoing) {
	Debug(DebugWarn,"SIPTransaction::setResponse(%d,'%s') in client mode [%p]",code,reason,this);
	return false;
    }
    switch (m_state) {
	case Invalid:
	case Retrans:
	case Finish:
	case Cleared:
	    DDebug(DebugInfo,"SIPTransaction ignoring setResponse(%d) in state %s [%p]",
		code,stateName(m_state),this);
	    return false;
    }
    if (!reason)
	reason = lookup(code,SIPResponses,"Unknown Reason Code");
    SIPMessage* msg = new SIPMessage(m_firstMessage, code, reason);
    setResponse(msg);
    msg->deref();
    return true;
}

void SIPTransaction::requestAuth(const String& realm, const String& domain, bool stale, bool proxy)
{
    if (m_outgoing) {
	Debug(DebugWarn,"SIPTransaction::requestAuth() in client mode [%p]",this);
	return;
    }
    switch (m_state) {
	case Invalid:
	case Retrans:
	case Finish:
	case Cleared:
	    DDebug(DebugInfo,"SIPTransaction ignoring requestAuth() in state %s [%p]",
		stateName(m_state),this);
	    return;
    }
    int code = proxy ? 407 : 401;
    const char* hdr = proxy ? "Proxy-Authenticate" : "WWW-Authenticate";
    SIPMessage* msg = new SIPMessage(m_firstMessage, code, lookup(code,SIPResponses));
    if (realm) {
	String tmp;
	tmp << "Digest realm=\"" << realm << "\"";
	SIPHeaderLine* line = new SIPHeaderLine(hdr,tmp,',');
	if (domain)
	    line->setParam(" domain","\"" + domain + "\"");
	m_engine->nonceGet(tmp);
	line->setParam(" nonce","\"" + tmp + "\"");
	line->setParam(" stale",stale ? "TRUE" : "FALSE");
	line->setParam(" algorithm","MD5");
	msg->addHeader(line);
    }
    setResponse(msg);
    msg->deref();
}

int SIPTransaction::authUser(String& user, bool proxy)
{
    if (!(m_engine && m_firstMessage))
	return -1;
    return m_engine->authUser(m_firstMessage, user, proxy);
}

bool SIPTransaction::processMessage(SIPMessage* message, const String& branch)
{
    if (!(message && m_firstMessage))
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
	    DDebug(DebugAll,"SIPTransaction found non-branch ACK response to our 2xx");
	}
	else if (getMethod() != message->method) {
	    if (!(isIncoming() && isInvite() && message->isACK()))
		return false;
	}
    }
    else {
	if (getMethod() != message->method) {
	    if (!(isIncoming() && isInvite() && message->isACK()))
		return false;
	}
	if ((m_firstMessage->getCSeq() != message->getCSeq()) ||
	    (getCallID() != message->getHeaderValue("Call-ID")) ||
	    (m_firstMessage->getHeaderValue("From") != message->getHeaderValue("From")) ||
	    (m_firstMessage->getHeaderValue("To") != message->getHeaderValue("To")))
	    return false;
	// allow braindamaged UAs that send answers with no Via line
	if (m_firstMessage->getHeader("Via") && message->getHeader("Via") &&
	    (m_firstMessage->getHeaderValue("Via") != message->getHeaderValue("Via")))
	    return false;
	// extra checks are to be made for ACK only
	if (message->isACK()) {
	    if (getURI() != message->uri)
		return false;
	    if (getDialogTag() != message->getParamValue("To","tag"))
		return false;
	}
    }
    if (isOutgoing() != message->isAnswer()) {
	DDebug(DebugAll,"SIPTransaction ignoring retransmitted %s %p '%s' in [%p]",
	    message->isAnswer() ? "answer" : "request",
	    message,message->method.c_str(),this);
	return false;
    }
    DDebug(DebugAll,"SIPTransaction processing %s %p '%s' in [%p]",
	message->isAnswer() ? "answer" : "request",
	message,message->method.c_str(),this);

    if (m_tag.null() && message->isAnswer()) {
	const NamedString* ns = message->getParam("To","tag");
	if (ns) {
	    m_tag = *ns;
	    DDebug(DebugInfo,"SIPTransaction found dialog tag '%s' [%p]",
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
	    m_response = message->code;
	    if (m_response == 100)
		break;
	    // fall trough for non-100 answers
	case Process:
	    if (message->code <= 100)
		break;
	    if (m_invite && (m_response <= 100))
		// use the human interaction timeout in INVITEs
		setTimeout(m_engine->getUserTimeout());
	    m_response = message->code;
	    setPendingEvent(new SIPEvent(message,this));
	    if (m_response >= 200) {
		setTimeout();
		if (isInvite()) {
		    // build the ACK
		    setLatestMessage(new SIPMessage(m_firstMessage,message));
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
	    else {
		m_response = 408;
		changeState(Cleared);
	    }
	    break;
	case Process:
	    if (timeout == 0) {
		m_response = 408;
		changeState(Cleared);
	    }
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
		setResponse(100);
		changeState(Trying);
	    }
	    else {
		setResponse(501);
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
	    // the absolute maximum timeout as we have to accomodate proxies
	    setTimeout(m_engine->getTimer('C'));
	    break;
	case Process:
	    if (timeout < 0)
		break;
	    if (timeout && m_lastMessage)
		e = new SIPEvent(m_lastMessage,this);
	    if (timeout)
		break;
	    setResponse(408);
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
