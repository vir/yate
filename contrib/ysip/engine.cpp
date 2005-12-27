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
#include "util.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>


using namespace TelEngine;

static TokenDict sip_responses[] = {
    { "Trying", 100 },
    { "Ringing", 180 },
    { "Call Is Being Forwarded", 181 },
    { "Queued", 182 },
    { "Session Progress", 183 },
    { "OK", 200 },
    { "Accepted", 202 },
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
    { "Session Timer Too Small", 422 },
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
    { "Bad Event", 489 },
    { "Request Pending", 491 },
    { "Undecipherable", 493 },
    { "Security Agreement Required", 494 },
    { "Server Internal Error", 500 },
    { "Not Implemented", 501 },
    { "Bad Gateway", 502 },
    { "Service Unavailable", 503 },
    { "Server Time-out", 504 },
    { "Version Not Supported", 505 },
    { "Message Too Large", 513 },
    { "Response Cannot Be Sent Safely", 514 },
    { "Response requires congestion management", 515 },
    { "Proxying of request would induce fragmentation", 516 },
    { "Precondition Failure", 580 },
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
    m_desc = uri.getDescription();
    m_proto = uri.getProtocol();
    m_user = uri.getUser();
    m_host = uri.getHost();
    m_port = uri.getPort();
    m_parsed = true;
}

URI::URI(const char* proto, const char* user, const char* host, int port, const char* desc)
    : m_desc(desc), m_proto(proto), m_user(user), m_host(host), m_port(port)
{
    if (desc)
	*this << "\"" << m_desc << "\" <";
    *this << m_proto << ":";
    if (user)
	*this << m_user << "@";
    *this << m_host;
    if (m_port > 0)
	*this << ":" << m_port;
    if (desc)
	*this << ">";
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
    m_desc.clear();

    // the compiler generates wrong code so use the temporary
    String tmp(*this);
    bool hasDesc = false;
    Regexp r("^[[:space:]]*\"\\([^\"]\\+\\)\"[[:space:]]*\\(.*\\)$");
    if (tmp.matches(r))
	hasDesc = true;
    else {
	r = "^[[:space:]]*\\([^<]\\+\\)[[:space:]]*<\\([^>]\\+\\)";
	hasDesc = tmp.matches(r);
    }
    if (hasDesc) {
	m_desc = tmp.matchString(1);
	tmp = tmp.matchString(2);
	*const_cast<URI*>(this) = tmp;
	DDebug("URI",DebugAll,"new value='%s' [%p]",c_str(),this);
    }

    r = "<\\([^>]\\+\\)>";
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
	DDebug("URI",DebugAll,"desc='%s' proto='%s' user='%s' host='%s' port=%d [%p]",
	    m_desc.c_str(), m_proto.c_str(), m_user.c_str(), m_host.c_str(), m_port, this);
    }
    else {
	m_desc.clear();
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
      m_cseq(0), m_userAgent(userAgent), m_nonce_time(0)
{
    debugName("sipengine");
    DDebug(this,DebugInfo,"SIPEngine::SIPEngine() [%p]",this);
    if (m_userAgent.null())
	m_userAgent << "YATE/" << YATE_VERSION;
    m_allowed = "ACK";
    char tmp[32];
    ::snprintf(tmp,sizeof(tmp),"%08x",::rand() ^ (int)Time::now());
    m_nonce_secret = tmp;
}

SIPEngine::~SIPEngine()
{
    DDebug(this,DebugInfo,"SIPEngine::~SIPEngine() [%p]",this);
}

SIPTransaction* SIPEngine::addMessage(SIPParty* ep, const char* buf, int len)
{
    DDebug(this,DebugInfo,"addMessage(%p,%d) [%p]",buf,len,this);
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
    DDebug(this,DebugInfo,"addMessage(%p) [%p]",message,this);
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
    SIPTransaction* forked = 0;
    ObjList* l = &TransList;
    for (; l; l = l->next()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	if (!t)
	    continue;
	switch (t->processMessage(message,branch)) {
	    case SIPTransaction::Matched:
		return t;
	    case SIPTransaction::NoDialog:
		forked = t;
		break;
	    case SIPTransaction::NoMatch:
	    default:
		break;
	}
    }
    if (forked)
	return forkInvite(message,forked);

    if (message->isAnswer()) {
	Debug(this,DebugInfo,"Message %p was an unhandled answer [%p]",message,this);
	return 0;
    }
    if (message->isACK()) {
	DDebug(this,DebugAll,"Message %p was an unhandled ACK [%p]",message,this);
	return 0;
    }
    message->complete(this);
    return new SIPTransaction(message,this,message->isOutgoing());
}

SIPTransaction* SIPEngine::forkInvite(SIPMessage* answer, const SIPTransaction* trans)
{
    // TODO: build new transaction or CANCEL
    Debug(this,DebugInfo,"Message %p was a forked INVITE answer [%p]",answer,this);
    return 0;
}

bool SIPEngine::process()
{
    SIPEvent* e = getEvent();
    if (!e)
	return false;
    DDebug(this,DebugInfo,"process() got event %p",e);
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
		DDebug(this,DebugInfo,"Got event %p (state %s) from transaction %p [%p]",
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
    DDebug(this,DebugAll,"Processing %s event %p message %p [%p]",
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
		Debug(this,DebugInfo,"Rejecting unhandled request '%s' in event %p [%p]",
		    event->getMessage()->method.c_str(),event,this);
		event->getTransaction()->setResponse(405);
	    }
	}
    }
    delete event;
}

u_int64_t SIPEngine::getUserTimeout() const
{
    // by default allow 2 minutes for user interaction
    return 120000000;
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
    Debug(this,DebugMild,"Requested invalid timer '%c' [%p]",which,this);
    return 0;
}

void SIPEngine::nonceGet(String& nonce)
{
    m_nonce_mutex.lock();
    unsigned int t = Time::secNow();
    if (t != m_nonce_time) {
	m_nonce_time = t;
	String tmp(m_nonce_secret);
	tmp << "." << t;
	MD5 md5(tmp);
	m_nonce = md5.hexDigest();
	m_nonce << "." << t;
	XDebug(this,DebugAll,"Generated new nonce '%s' [%p]",
	    m_nonce.c_str(),this);
    }
    nonce = m_nonce;
    m_nonce_mutex.unlock();
}

long SIPEngine::nonceAge(const String& nonce)
{
    if (nonce.null())
	return -1;
    Lock lock(m_nonce_mutex);
    if (nonce == m_nonce)
	return Time::secNow() - m_nonce_time;
    lock.drop();
    int dot = nonce.find('.');
    if (dot < 0)
	return -1;
    String tmp(nonce.substr(dot+1));
    if (tmp.null())
	return -1;
    unsigned int t = 0;
    tmp >> t;
    if (!tmp.null())
	return -1;
    tmp << m_nonce_secret << "." << t;
    MD5 md5(tmp);
    if (nonce.substr(0,dot) != md5.hexDigest())
	return -1;
    return Time::secNow() - t;
}

bool SIPEngine::checkUser(const String& username, const String& realm, const String& nonce,
    const String& method, const String& uri, const String& response, const SIPMessage* message)
{
    return false;
}

// response = md5(md5(username:realm:password):nonce:md5(method:uri))
void SIPEngine::buildAuth(const String& username, const String& realm, const String& passwd,
    const String& nonce, const String& method, const String& uri, String& response)
{
    XDebug(DebugAll,"SIP Building auth: '%s:%s:%s' '%s' '%s:%s'",
	username.c_str(),realm.c_str(),passwd.c_str(),nonce.c_str(),method.c_str(),uri.c_str());
    MD5 m1,m2;
    m1 << username << ":" << realm << ":" << passwd;
    m2 << method << ":" << uri;
    String tmp;
    tmp << m1.hexDigest() << ":" << nonce << ":" << m2.hexDigest();
    m1.clear();
    m1.update(tmp);
    response = m1.hexDigest();
}

// response = md5(hash_a1:nonce:hash_a2)
void SIPEngine::buildAuth(const String& hash_a1, const String& nonce, const String& hash_a2,
    String& response)
{
    MD5 md5;
    md5 << hash_a1 << ":" << nonce << ":" << hash_a2;
    response = md5.hexDigest();
}

int SIPEngine::authUser(const SIPMessage* message, String& user, bool proxy)
{
    if (!message)
	return -1;
    const char* hdr = proxy ? "Proxy-Authorization" : "Authorization";
    const ObjList* l = &message->header;
    for (; l; l = l->next()) {
	const GenObject* o = l->get();
	const SIPHeaderLine* t = o ? static_cast<const SIPHeaderLine*>(o->getObject("SIPHeaderLine")) : 0;
	if (t && (t->name() &= hdr) && (*t &= "Digest")) {
	    String usr(t->getParam("username"));
	    delQuotes(usr);
	    if (usr.null())
		continue;
	    XDebug(this,DebugAll,"authUser found user '%s'",usr.c_str());
	    // if we know the username check if it matches
	    if (user && (usr != user))
		continue;
	    String nonce(t->getParam("nonce"));
	    delQuotes(nonce);
	    // TODO: implement a nonce cache for the stupid clients that don't send it back
	    if (nonce.null())
		continue;
	    // see if the nonce was generated by this engine
	    long age = nonceAge(nonce);
	    if (age < 0)
		continue;
	    XDebug(this,DebugAll,"authUser nonce age is %ld",age);
	    String res(t->getParam("response"));
	    delQuotes(res);
	    if (res.null())
		continue;
	    String uri(t->getParam("uri"));
	    delQuotes(uri);
	    if (uri.null())
		uri = message->uri;
	    String realm(t->getParam("realm"));
	    delQuotes(realm);

	    if (!checkUser(usr,realm,nonce,message->method,uri,res,message))
		continue;

	    if (user.null())
		user = usr;
	    return age;
	}
    }
    return -1;
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
