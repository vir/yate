/**
 * engine.cpp
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
#include <yateversn.h>

#include <string.h>
#include <stdlib.h>


using namespace TelEngine;

static TokenDict sip_responses[] = {
    { "Trying", 100 },
    { "Ringing", 180 },
    { "Call Is Being Forwarded", 181 },
    { "Queued", 182 },
    { "Session Progress", 183 },
    { "OK", 200 },
    { "Multiple Choices", 300 },
    { "Moved Permanently", 301 },
    { "Moved Temporarily", 302 },
    { "Use Proxy", 305 },
    { "Alternative Service", 380 },
    { "Bad Request", 400 },
    { "Unauthorized", 401 },
    { "Payment Required", 402 },
    { "Forbidden", 403 },
    { "Not Found", 404 },
    { "Method Not Allowed", 405 },
    { "Not Acceptable", 406 },
    { "Proxy Authentication Required", 407 },
    { "Request Timeout", 408 },
    { "Gone", 410 },
    { "Request Entity Too Large", 413 },
    { "Request-URI Too Long", 414 },
    { "Unsupported Media Type", 415 },
    { "Unsupported URI Scheme", 416 },
    { "Bad Extension", 420 },
    { "Extension Required", 421 },
    { "Interval Too Brief", 423 },
    { "Temporarily Unavailable", 480 },
    { "Call/Transaction Does Not Exist", 481 },
    { "Loop Detected", 482 },
    { "Too Many Hops", 483 },
    { "Address Incomplete", 484 },
    { "Ambiguous", 485 },
    { "Busy Here", 486 },
    { "Request Terminated", 487 },
    { "Not Acceptable Here", 488 },
    { "Request Pending", 491 },
    { "Undecipherable", 493 },
    { "Server Internal Error", 500 },
    { "Not Implemented", 501 },
    { "Bad Gateway", 502 },
    { "Service Unavailable", 503 },
    { "Server Time-out", 504 },
    { "Version Not Supported", 505 },
    { "Message Too Large", 513 },
    { "Busy Everywhere", 600 },
    { "Decline", 603 },
    { "Does Not Exist Anywhere", 604 },
    { "Not Acceptable", 606 },
    { 0, 0 },
};

TokenDict* TelEngine::SIPResponses = sip_responses;

URI::URI()
    : m_parsed(false)
{
}

URI::URI(const String& uri)
    : String(uri), m_parsed(false)
{
}

URI::URI(const URI& uri)
    : String(uri), m_parsed(false)
{
    m_proto = uri.getProtocol();
    m_user = uri.getUser();
    m_host = uri.getHost();
    m_port = uri.getPort();
    m_parsed = true;
}

URI::URI(const char* proto, const char* user, const char* host, int port)
    : m_proto(proto), m_user(user), m_host(host), m_port(port)
{
    *this << m_proto << ":";
    if (user)
	*this << m_user << "@";
    *this << m_host;
    if (m_port > 0)
	*this << ":" << m_port;
    m_parsed = true;
}

void URI::changed()
{
    m_parsed = false;
}

void URI::parse() const
{
    if (m_parsed)
	return;
    DDebug("URI",DebugAll,"parsing '%s' [%p]",c_str(),this);
    m_port = 0;

    // the compiler generates wrong code so use the temporary
    String tmp(*this);
    Regexp r("<\\([^>]\\+\\)>");
    if (tmp.matches(r)) {
	tmp = tmp.matchString(1);
	*const_cast<URI*>(this) = tmp;
	DDebug("URI",DebugAll,"new value='%s' [%p]",c_str(),this);
    }

    // [proto:][user[:passwd]@]hostname[:port][/path][?param=value[&param=value...]]
    // [proto:][user@]hostname[:port][/path][;params][?params][&params]
    r = "^\\([[:alpha:]]\\+:\\)\\?\\([^[:space:][:cntrl:]@]\\+@\\)\\?\\([[:alnum:]._-]\\+\\)\\(:[0-9]\\+\\)\\?";
    if (tmp.matches(r)) {
	m_proto = tmp.matchString(1).toLower();
	m_proto = m_proto.substr(0,m_proto.length()-1);
	m_user = tmp.matchString(2);
	m_user = m_user.substr(0,m_user.length()-1);
	m_host = tmp.matchString(3).toLower();
	tmp = tmp.matchString(4);
	tmp >> ":" >> m_port;
	DDebug("URI",DebugAll,"proto='%s' user='%s' host='%s' port=%d [%p]",
	    m_proto.c_str(), m_user.c_str(), m_host.c_str(), m_port, this);
    }
    else {
	m_proto.clear();
	m_user.clear();
	m_host.clear();
    }
    m_parsed = true;
}

SIPParty::SIPParty()
    : m_reliable(false)
{
    DDebug(DebugAll,"SIPParty::SIPParty() [%p]",this);
}

SIPParty::SIPParty(bool reliable)
    : m_reliable(reliable)
{
    DDebug(DebugAll,"SIPParty::SIPParty(%d) [%p]",reliable,this);
}

SIPParty::~SIPParty()
{
    DDebug(DebugAll,"SIPParty::~SIPParty() [%p]",this);
}

SIPEvent::SIPEvent(SIPMessage* message, SIPTransaction* transaction)
    : m_message(message), m_transaction(transaction),
      m_state(SIPTransaction::Invalid)
{
    DDebug(DebugAll,"SIPEvent::SIPEvent(%p,%p) [%p]",message,transaction,this);
    if (m_message)
	m_message->ref();
    if (m_transaction) {
	m_transaction->ref();
	m_state = m_transaction->getState();
    }
}

SIPEvent::~SIPEvent()
{
    DDebug(DebugAll,"SIPEvent::~SIPEvent() [%p]",this);
    if (m_transaction)
	m_transaction->deref();
    if (m_message)
	m_message->deref();
}

SIPEngine::SIPEngine(const char* userAgent)
    : m_mutex(true),
      m_t1(500000), m_t4(5000000), m_maxForwards(70),
      m_cseq(0), m_userAgent(userAgent)
{
    DDebug(DebugInfo,"SIPEngine::SIPEngine() [%p]",this);
    if (m_userAgent.null())
	m_userAgent << "YATE/" << YATE_VERSION;
    m_allowed = "ACK";
}

SIPEngine::~SIPEngine()
{
    DDebug(DebugInfo,"SIPEngine::~SIPEngine() [%p]",this);
}

SIPTransaction* SIPEngine::addMessage(SIPParty* ep, const char* buf, int len)
{
    DDebug("SIPEngine",DebugInfo,"addMessage(%p,%d) [%p]",buf,len,this);
    SIPMessage* msg = SIPMessage::fromParsing(ep,buf,len);
    if (ep)
	ep->deref();
    if (msg) {
	SIPTransaction* tr = addMessage(msg);
	msg->deref();
	return tr;
    }
    return 0;
}

SIPTransaction* SIPEngine::addMessage(SIPMessage* message)
{
    DDebug("SIPEngine",DebugInfo,"addMessage(%p) [%p]",message,this);
    if (!message)
	return 0;
    // make sure outgoing messages are well formed
    if (message->isOutgoing())
	message->complete(this);
    // locate the branch parameter of last Via header - added by the UA
    const SIPHeaderLine* hl = message->getLastHeader("Via");
    const NamedString* br = hl ? hl->getParam("branch") : 0;
    String branch(br ? *br : String::empty());
    if (!branch.startsWith("z9hG4bK"))
	branch.clear();
    Lock lock(m_mutex);
    ObjList* l = &TransList;
    for (; l; l = l->next()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	if (t && t->processMessage(message,branch))
	    return t;
    }
    if (message->isAnswer()) {
	Debug("SIPEngine",DebugInfo,"Message %p was an unhandled answer [%p]",message,this);
	return 0;
    }
    if (message->isACK()) {
	Debug("SIPEngine",DebugAll,"Message %p was an unhandled ACK [%p]",message,this);
	return 0;
    }
    message->complete(this);
    return new SIPTransaction(message,this,message->isOutgoing());
}

bool SIPEngine::process()
{
    SIPEvent* e = getEvent();
    if (!e)
	return false;
    DDebug("SIPEngine",DebugInfo,"process() got event %p",e);
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
	    if (e) {
		DDebug("SIPEngine",DebugInfo,"Got event %p (state %s) from transaction %p [%p]",
		    e,SIPTransaction::stateName(e->getState()),t,this);
		return e;
	    }
	}
    }
    return 0;
}

void SIPEngine::processEvent(SIPEvent *event)
{
    if (!event)
	return;
    Lock lock(m_mutex);
    const char* type = "unknown";
    if (event->isOutgoing())
	type = "outgoing";
    if (event->isIncoming())
	type = "incoming";
    DDebug("SIPEngine",DebugAll,"Processing %s event %p message %p [%p]",
	type,event,event->getMessage(),this);
    if (event->getMessage()) {
	if (event->isOutgoing()) {
	    switch (event->getState()) {
		case SIPTransaction::Invalid:
		    break;
		case SIPTransaction::Cleared:
		    if (!event->getMessage()->isAnswer())
			break;
		default:
		    if (event->getParty())
			event->getParty()->transmit(event);
	    }
	}
	if (event->isIncoming()) {
	    if ((event->getState() == SIPTransaction::Trying) &&
		!event->getMessage()->isAnswer()) {
		Debug("SIPEngine",DebugInfo,"Rejecting unhandled request '%s' in event %p [%p]",
		    event->getMessage()->method.c_str(),event,this);
		event->getTransaction()->setResponse(501);
	    }
	}
    }
    delete event;
}

u_int64_t SIPEngine::getTimer(char which, bool reliable) const
{
    switch (which) {
	case '1':
	    // T1: RTT Estimate 500ms default
	    return m_t1;
	case '2':
	    // T2: Maximum retransmit interval
	    //  for non-INVITE requests and INVITE responses
	    return 4000000;
	case '4':
	    // T4: Maximum duration a message will remain in the network
	    return m_t4;
	case 'A':
	    // A: INVITE request retransmit interval, for UDP only
	    return m_t1;
	case 'B':
	    // B: INVITE transaction timeout timer
	    return 64*m_t1;
	case 'C':
	    // C: proxy INVITE transaction timeout
	    return 180000000;
	case 'D':
	    // D: Wait time for response retransmits
	    return reliable ? 0 : 32000000;
	case 'E':
	    // E: non-INVITE request retransmit interval, UDP only
	    return m_t1;
	case 'F':
	    // F: non-INVITE transaction timeout timer
	    return 64*m_t1;
	case 'G':
	    // G: INVITE response retransmit interval
	    return m_t1;
	case 'H':
	    // H: Wait time for ACK receipt
	    return 64*m_t1;
	case 'I':
	    // I: Wait time for ACK retransmits
	    return reliable ? 0 : m_t4;
	case 'J':
	    // J: Wait time for non-INVITE request retransmits
	    return reliable ? 0 : 64*m_t1;
	case 'K':
	    // K: Wait time for response retransmits
	    return reliable ? 0 : m_t4;
    }
    Debug("SIPEngine",DebugMild,"Requested invalid timer '%c' [%p]",which,this);
    return 0;
}

bool SIPEngine::isAllowed(const char* method) const
{
    int pos = m_allowed.find(method);
    if (pos < 0)
	return false;
    if ((pos > 0) && (m_allowed[pos-1] != ' '))
	return false;
    return true;
}

void SIPEngine::addAllowed(const char* method)
{
    Lock lock(m_mutex);
    if (method && *method && !isAllowed(method))
	m_allowed << ", " << method;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
