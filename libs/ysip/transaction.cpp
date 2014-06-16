/**
 * transaction.cpp
 * Yet Another SIP Stack
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

#include <yatesip.h>

#include <string.h>
#include <stdlib.h>


using namespace TelEngine;

// Constructor from new message
SIPTransaction::SIPTransaction(SIPMessage* message, SIPEngine* engine, bool outgoing)
    : m_outgoing(outgoing), m_invite(false), m_transmit(false), m_state(Invalid),
      m_response(0), m_timeouts(0), m_timeout(0),
      m_firstMessage(message), m_lastMessage(0), m_pending(0), m_engine(engine), m_private(0)
{
    DDebug(getEngine(),DebugAll,"SIPTransaction::SIPTransaction(%p,%p,%d) [%p]",
	message,engine,outgoing,this);
    if (m_firstMessage) {
	m_firstMessage->ref();

	const NamedString* ns = message->getParam("Via","branch",true);
	if (ns)
	    m_branch = *ns;
	if (!m_branch.startsWith("z9hG4bK"))
	    m_branch.clear();
	ns = message->getParam("To","tag");
	if (ns)
	    m_tag = *ns;

	const MimeHeaderLine* hl = message->getHeader("Call-ID");
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
    m_invite = (getMethod() == YSTRING("INVITE"));
    m_state = Initial;
    m_transCount = outgoing ? m_engine->getReqTransCount() : m_engine->getRspTransCount();
    m_engine->append(this);
}

// Constructor from original and authentication requesting answer
SIPTransaction::SIPTransaction(SIPTransaction& original, SIPMessage* answer)
    : m_outgoing(true), m_invite(original.m_invite), m_transmit(false), m_state(Process),
      m_response(original.m_response), m_transCount(original.m_transCount),
      m_timeouts(0), m_timeout(0),
      m_firstMessage(original.m_firstMessage), m_lastMessage(original.m_lastMessage),
      m_pending(0), m_engine(original.m_engine),
      m_branch(original.m_branch), m_callid(original.m_callid), m_tag(original.m_tag),
      m_private(0)
{
    DDebug(getEngine(),DebugAll,"SIPTransaction::SIPTransaction(&%p,%p) [%p]",
	&original,answer,this);

    SIPMessage* msg = new SIPMessage(*original.m_firstMessage);
    MimeAuthLine* auth = answer->buildAuth(*original.m_firstMessage,m_engine);
    m_firstMessage->setAutoAuth();
    msg->complete(m_engine);
    msg->addHeader(auth);
    const NamedString* ns = msg->getParam("Via","branch",true);
    if (ns)
	original.m_branch = *ns;
    else
	original.m_branch.clear();
    ns = msg->getParam("To","tag");
    if (ns)
	original.m_tag = *ns;
    else
	original.m_tag.clear();
    original.m_firstMessage = msg;
    original.m_lastMessage = 0;

#ifdef SIP_ACK_AFTER_NEW_INVITE
    // if this transaction is an INVITE and we append it to the list its
    //  ACK will be sent after the new INVITE which is legal but "unnatural"
    // some SIP endpoints seem to assume things about transactions
    m_engine->append(this);
#else
    // insert this transaction rather than appending it
    // this way we get a chance to send one ACK before a new INVITE
    // note that there is no guarantee because of the possibility of the
    //  packets getting lost and retransmitted or to use a different route
    m_engine->insert(this);
#endif
}

// Constructor from original and forked dialog tag
SIPTransaction::SIPTransaction(const SIPTransaction& original, const String& tag)
    : m_outgoing(true), m_invite(original.m_invite), m_transmit(false), m_state(Process),
      m_response(original.m_response), m_transCount(original.m_transCount),
      m_timeouts(0), m_timeout(0),
      m_firstMessage(original.m_firstMessage), m_lastMessage(0),
      m_pending(0), m_engine(original.m_engine),
      m_branch(original.m_branch), m_callid(original.m_callid), m_tag(tag),
      m_private(0)
{
    if (m_firstMessage)
	m_firstMessage->ref();

#ifdef SIP_PRESERVE_TRANSACTION_ORDER
    // new transactions at the end, preserve "natural" order
    m_engine->append(this);
#else
    // put new transactions first - faster to match new messages
    m_engine->insert(this);
#endif
}

SIPTransaction::~SIPTransaction()
{
#ifdef DEBUG
    Debugger debug(DebugAll,"SIPTransaction::~SIPTransaction()"," [%p]",this);
#endif
    setPendingEvent(0,true);
    TelEngine::destruct(m_lastMessage);
    TelEngine::destruct(m_firstMessage);
}

void SIPTransaction::destroyed()
{
    DDebug(getEngine(),DebugAll,"SIPTransaction::destroyed() [%p]",this);
    m_state = Invalid;
    m_engine->remove(this);
    setPendingEvent(0,true);
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
	Debug(getEngine(),DebugGoOn,"SIPTransaction is already invalid [%p]",this);
	return false;
    }
    DDebug(getEngine(),DebugAll,"SIPTransaction state changed from %s to %s [%p]",
	stateName(m_state),stateName(newstate),this);
    m_state = newstate;
    return true;
}

void SIPTransaction::setDialogTag(const char* tag)
{
    if (null(tag)) {
	if (m_tag.null())
	    m_tag = (int)Random::random();
    }
    else
	m_tag = tag;
}

void SIPTransaction::setLatestMessage(SIPMessage* message)
{
    if (m_lastMessage == message)
	return;
    DDebug(getEngine(),DebugAll,"SIPTransaction latest message changing from %p %d to %p %d [%p]",
	m_lastMessage, m_lastMessage ? m_lastMessage->code : 0,
	message, message ? message->code : 0, this);
    if (m_lastMessage)
	m_lastMessage->deref();
    m_lastMessage = message;
    if (m_lastMessage) {
	m_lastMessage->ref();
	if (message->isAnswer()) {
	    m_response = message->code;
	    if ((m_response > 100) && (m_response < 300))
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

void SIPTransaction::setTransCount(int count)
{
    if (count < 0)
	return;
    else if (count < 2)
	m_transCount = 2;
    else if (count > 10)
	m_transCount = 10;
    else
	m_transCount = count;
}

void SIPTransaction::setTimeout(u_int64_t delay, unsigned int count)
{
    m_timeouts = count;
    m_delay = delay;
    m_timeout = (count && delay) ? Time::now() + delay : 0;
#ifdef DEBUG
    if (m_timeout)
	Debug(getEngine(),DebugAll,"SIPTransaction new %d timeouts initially " FMT64U " usec apart [%p]",
	    m_timeouts,m_delay,this);
#endif
}

SIPEvent* SIPTransaction::getEvent(bool pendingOnly, u_int64_t time)
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

    if (pendingOnly)
	return 0;

    int timeout = -1;
    if (m_timeout) {
	if (!time)
	    time = Time::now();
	if (time >= m_timeout) {
	    timeout = --m_timeouts;
	    m_delay *= 2; // exponential back-off
	    m_timeout = (m_timeouts) ? time + m_delay : 0;
	    DDebug(getEngine(),DebugAll,"SIPTransaction fired timer #%d [%p]",timeout,this);
	}
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
	    // fall through because we recheck the timeout
	case Finish:
	    if (timeout)
		break;
	    changeState(Cleared);
	    // fall through so we don't wait another turn for processing
	case Cleared:
	    setTimeout();
	    e = new SIPEvent(m_firstMessage,this);
	    // make sure we don't get trough this one again
	    changeState(Invalid);
	    return e;
	case Invalid:
	    Debug(getEngine(),DebugFail,"SIPTransaction::getEvent in invalid state [%p]",this);
	    break;
    }
    return e;
}

void SIPTransaction::setResponse(SIPMessage* message)
{
    if (m_outgoing) {
	Debug(getEngine(),DebugWarn,"SIPTransaction::setResponse(%p) in client mode [%p]",message,this);
	return;
    }
    Lock lock(m_engine);
    setLatestMessage(message);
    setTransmit();
    if (message && (message->code >= 200)) {
	if (isInvite()) {
	    // we need to actively retransmit this message
	    // RFC3261 17.2.1: non 2xx are not retransmitted on reliable transports
	    if (changeState(Retrans)) {
		bool reliable = message->getParty() && message->getParty()->isReliable();
		bool retrans = !reliable || message->code < 300;
		setTimeout(m_engine->getTimer(retrans ? 'G' : 'H',reliable),
		    retrans ? getTransCount() : 1);
	    }
	}
	else {
	    // just wait and reply to retransmits
	    if (changeState(Finish))
		setTimeout(m_engine->getTimer('J'));
	}
    }
    // extend timeout for provisional messages, use proxy timeout (maximum)
    else if (message && (message->code > 100))
	setTimeout(m_engine->getTimer('C'));
}

bool SIPTransaction::setResponse() const
{
    if (m_outgoing)
	return false;
    switch (m_state) {
	case Initial:
	case Trying:
	case Process:
	    return true;
    }
    return false;
}

bool SIPTransaction::setResponse(int code, const char* reason)
{
    if (m_outgoing) {
	Debug(getEngine(),DebugWarn,"SIPTransaction::setResponse(%d,'%s') in client mode [%p]",code,reason,this);
	return false;
    }
    if (!setResponse()) {
	DDebug(getEngine(),DebugInfo,"SIPTransaction ignoring setResponse(%d) in state %s [%p]",
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
	Debug(getEngine(),DebugWarn,"SIPTransaction::requestAuth() in client mode [%p]",this);
	return;
    }
    switch (m_state) {
	case Invalid:
	case Retrans:
	case Finish:
	case Cleared:
	    DDebug(getEngine(),DebugInfo,"SIPTransaction ignoring requestAuth() in state %s [%p]",
		stateName(m_state),this);
	    return;
    }
    int code = proxy ? 407 : 401;
    const char* hdr = proxy ? "Proxy-Authenticate" : "WWW-Authenticate";
    SIPMessage* msg = new SIPMessage(m_firstMessage, code, lookup(code,SIPResponses));
    if (realm) {
	String tmp;
	tmp << "Digest realm=" << MimeHeaderLine::quote(realm);
	MimeHeaderLine* line = new MimeHeaderLine(hdr,tmp,',');
	if (domain)
	    line->setParam(" domain",MimeHeaderLine::quote(domain));
	m_engine->nonceGet(tmp);
	line->setParam(" nonce",MimeHeaderLine::quote(tmp));
	line->setParam(" stale",stale ? "TRUE" : "FALSE");
	line->setParam(" algorithm","MD5");
	msg->addHeader(line);
    }
    setResponse(msg);
    msg->deref();
}

int SIPTransaction::authUser(String& user, bool proxy, GenObject* userData)
{
    if (!(m_engine && m_firstMessage))
	return -1;
    return m_engine->authUser(m_firstMessage, user, proxy, userData);
}

SIPTransaction::Processed SIPTransaction::processMessage(SIPMessage* message, const String& branch)
{
    if (!(message && m_firstMessage))
	return NoMatch;
    XDebug(getEngine(),DebugAll,"SIPTransaction::processMessage(%p,'%s') [%p]",
	message,branch.c_str(),this);
    if (branch) {
	if (branch != m_branch) {
	    // different branch is allowed only for ACK in incoming INVITE...
	    if (!(isInvite() && isIncoming() && message->isACK()))
		return NoMatch;
	    // ...and if also matches the CSeq, Call-ID and To: tag
	    if ((m_firstMessage->getCSeq() != message->getCSeq()) ||
		(getCallID() != message->getHeaderValue("Call-ID")) ||
		(getDialogTag() != message->getParamValue("To","tag")))
		return NoMatch;
	    // ...and only if we sent a 200 response...
	    if (!m_lastMessage || ((m_lastMessage->code / 100) != 2))
#ifdef SIP_STRICT
		return NoMatch;
#else
		Debug(getEngine(),DebugNote,"Received non-branch ACK to non-2xx response! (sender bug)");
#endif
	    DDebug(getEngine(),DebugAll,"SIPTransaction found non-branch ACK response to our 2xx");
	}
	else if (getMethod() != message->method) {
	    if (!(isIncoming() && isInvite() && message->isACK()))
		return NoMatch;
	    if (!m_lastMessage || ((m_lastMessage->code / 100) == 2))
#ifdef SIP_STRICT
		return NoMatch;
#else
		Debug(getEngine(),DebugNote,"Received branch ACK to 2xx response! (sender bug)");
#endif
	}
    }
    else {
	if (getMethod() != message->method) {
	    if (!(isIncoming() && isInvite() && message->isACK()))
		return NoMatch;
	}
	if ((m_firstMessage->getCSeq() != message->getCSeq()) ||
	    (getCallID() != message->getHeaderValue("Call-ID")) ||
	    (m_firstMessage->getHeaderValue("From") != message->getHeaderValue("From")) ||
	    (m_firstMessage->getHeaderValue("To") != message->getHeaderValue("To")))
	    return NoMatch;
	// allow braindamaged UAs that send answers with no Via line
	if (m_firstMessage->getHeader("Via") && message->getHeader("Via") &&
	    (m_firstMessage->getHeaderValue("Via",true) != message->getHeaderValue("Via",true)))
	    return NoMatch;
	// extra checks are to be made for ACK only
	if (message->isACK()) {
	    if (getDialogTag() != message->getParamValue("To","tag"))
		return NoMatch;
	    // use a while so we can either break or return
	    while (getURI() != message->uri) {
#ifndef SIP_STRICT
		// hack to match URIs with lost tags. Cisco sucks. Period.
		String tmp = getURI();
		int sc = tmp.find(';');
		if (sc > 0) {
		    tmp.assign(tmp,sc);
		    if (tmp == message->uri) {
			Debug(getEngine(),DebugMild,"Received no-branch ACK with lost URI tags! (sender bug)");
			break;
		    }
		}
		// now try to match only the user part - Cisco strikes again...
		sc = tmp.find('@');
		if (sc > 0) {
		    tmp.assign(tmp,sc);
		    sc = message->uri.find('@');
		    if ((sc > 0) && (tmp == message->uri.substr(0,sc))) {
			Debug(getEngine(),DebugMild,"Received no-branch ACK with only user matching! (sender bug)");
			break;
		    }
		}
#endif
		return NoMatch;
	    }
	}
    }
    if (!message->getParty())
	message->setParty(m_firstMessage->getParty());
    if (isOutgoing() != message->isAnswer()) {
	DDebug(getEngine(),DebugAll,"SIPTransaction ignoring retransmitted %s %p '%s' in [%p]",
	    message->isAnswer() ? "answer" : "request",
	    message,message->method.c_str(),this);
	return NoMatch;
    }
    DDebug(getEngine(),DebugAll,"SIPTransaction processing %s %p '%s' %d in [%p]",
	message->isAnswer() ? "answer" : "request",
	message,message->method.c_str(),message->code,this);

    if (message->isAnswer()) {
	const NamedString* ns = message->getParam("To","tag");
	if (m_tag.null()) {
	    if (ns) {
		if (message->code > 100) {
		    // establish the dialog
		    m_tag = *ns;
		    DDebug(getEngine(),DebugInfo,"SIPTransaction found dialog tag '%s' [%p]",
			m_tag.c_str(),this);
		}
		else
		    Debug(getEngine(),DebugMild,"Received To tag in 100 answer! (sender bug)");
	    }
	}
	else if (!ns) {
	    // we have a dialog and the message has not - ignore it
	    // as we would be unable to CANCEL it anyway
	    return NoMatch;
	}
	else if (m_tag != *ns) {
	    // we have a dialog established and this message is out of it
	    // discriminate forked answers to INVITEs for later processing
	    return isInvite() ? NoDialog : NoMatch;
	}
    }

    processMessage(message);
    return Matched;
}

void SIPTransaction::processMessage(SIPMessage* message)
{
    if (isOutgoing())
	processClientMessage(message,m_state);
    else
	processServerMessage(message,m_state);
}

void SIPTransaction::processClientMessage(SIPMessage* message, int state)
{
    bool final = message->code >= 200;
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
	    setLatestMessage(message);
	    if (tryAutoAuth(message))
		break;
	    if (m_invite && !final)
		// use the human interaction timeout in INVITEs
		setTimeout(m_engine->getUserTimeout());
	    m_response = message->code;
	    setPendingEvent(new SIPEvent(message,this),final);
	    if (final) {
		setTimeout();
		if (isInvite()) {
		    // build the ACK
		    SIPMessage* m = new SIPMessage(m_firstMessage,message);
		    if (m_engine->autoChangeParty() && message->getParty())
			m->setParty(message->getParty());
		    setLatestMessage(m);
		    m_lastMessage->deref();
		    setTransmit();
		    if (changeState(Finish))
			setTimeout(m_engine->getTimer('H'));
		}
		else
		    changeState(Cleared);
	    }
	    break;
	case Finish:
	    if (m_lastMessage && m_lastMessage->isACK() && final)
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
	    if (changeState(Trying)) {
		bool reliable = e->getParty() && e->getParty()->isReliable();
		if (!reliable)
		    setTimeout(m_engine->getTimer(isInvite() ? 'A' : 'E'),getTransCount());
		else
		    setTimeout(m_engine->getTimer(isInvite() ? 'B' : 'F',true),1);
	    }
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
	    if (!( (m_firstMessage->getCSeq() >= 0) &&
		m_firstMessage->getHeader("Call-ID") &&
		m_firstMessage->getHeader("From") &&
		m_firstMessage->getHeader("To") ))
		setResponse(400);
	    else if (!m_engine->isAllowed(m_firstMessage->method))
		setResponse(501);
	    else {
		setResponse(100);
		// if engine is set up lazy skip first 100 transmission
		if (!isInvite() && m_engine && m_engine->lazyTrying())
		    m_transmit = false;
		changeState(Trying);
		break;
	    }
	    e = new SIPEvent(m_lastMessage,this);
	    m_transmit = false;
	    changeState(Invalid);
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
	    e = new SIPEvent(m_lastMessage,this);
	    break;
	case Retrans:
	    if (isInvite() && (timeout == 0)) {
		// we didn't got an ACK so declare timeout
		m_response = 408;
		changeState(Cleared);
	    }
	    break;
    }
    return e;
}

// Event transmission failed notification
void SIPTransaction::msgTransmitFailed(SIPMessage* msg)
{
    if (!msg)
	return;
    Lock lock(getEngine());
    DDebug(getEngine(),DebugNote,
	"SIPTransaction send failed state=%s msg=%p first=%p last=%p [%p]",
	stateName(m_state),msg,m_firstMessage,m_lastMessage,this);
    // Do nothing in termination states
    if (m_state == Invalid || m_state == Finish || m_state == Cleared)
	return;
    if (isOutgoing()) {
	if (m_state == Trying) {
	    if (msg != m_firstMessage)
		return;
	    // Reliable transport: terminate now
	    // Non reliable: terminate if this is the last attempt
	    if ((msg->getParty() && msg->getParty()->isReliable()) ||
		    m_timeouts >= getTransCount()) {
		Debug(getEngine(),DebugInfo,
		    "SIPTransaction send failed state=%s: clearing [%p]",
		    stateName(m_state),this);
		m_response = 500;
		changeState(Cleared);
		return;
	    }
	}
	else if (m_state == Initial || msg != m_lastMessage)
	    return;
    }
    else {
	// Incoming
	if (msg != m_lastMessage)
	    return;
    }
    // Avoid party retry
    Debug(getEngine(),DebugAll,
	"SIPTransaction send failed state=%s resetting msg %p party [%p]",
	stateName(m_state),msg,this);
    msg->setParty();
}

bool SIPTransaction::tryAutoAuth(SIPMessage* answer)
{
    if ((answer->code != 401) && (answer->code != 407))
	return false;
    if (m_firstMessage->getAuthUsername().null())
	return false;
    setTimeout();
    SIPTransaction* tr = new SIPTransaction(*this,answer);
    changeState(Initial);
    tr->processClientMessage(answer,Process);
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
