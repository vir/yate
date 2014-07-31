/**
 * engine.cpp
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
    { "See Other", 303 },
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
    { "Conflict", 409 },
    { "Gone", 410 },
    { "Length Required", 411 },
    { "Conditional Request Failed", 412 },
    { "Request Entity Too Large", 413 },
    { "Request-URI Too Long", 414 },
    { "Unsupported Media Type", 415 },
    { "Unsupported URI Scheme", 416 },
    { "Unknown Resource-Priority", 417 },
    { "Bad Extension", 420 },
    { "Extension Required", 421 },
    { "Session Timer Too Small", 422 },
    { "Interval Too Brief", 423 },
    { "Bad Location Information", 424 },
    { "Use Identity Header", 428 },
    { "Provide Referrer Identity", 429 },
    { "Flow Failed", 430 },                                // RFC5626
    { "Anonymity Disallowed", 433 },
    { "Bad Identity-Info", 436 },
    { "Unsupported Certificate", 437 },
    { "Invalid Identity Header", 438 },
    { "First Hop Lacks Outbound Support", 439 },           // RFC5626
    { "Max-Breadth Exceeded", 440 },
    { "Bad Info Package", 469 },
    { "Consent Needed", 470 },
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

SIPParty::SIPParty(Mutex* mutex)
    : m_mutex(mutex), m_reliable(false), m_localPort(0), m_partyPort(0)
{
    DDebug(DebugAll,"SIPParty::SIPParty() [%p]",this);
}

SIPParty::SIPParty(bool reliable, Mutex* mutex)
    : m_mutex(mutex), m_reliable(reliable), m_localPort(0), m_partyPort(0)
{
    DDebug(DebugAll,"SIPParty::SIPParty(%d) [%p]",reliable,this);
}

SIPParty::~SIPParty()
{
    DDebug(DebugAll,"SIPParty::~SIPParty() [%p]",this);
}

void SIPParty::setAddr(const String& addr, int port, bool local)
{
    Lock lock(m_mutex);
    String& a = local ? m_local : m_party;
    int& p = local ? m_localPort : m_partyPort;
    a = addr;
    p = port;
    DDebug(DebugAll,"SIPParty updated %s address '%s' [%p]",
	local ? "local" : "remote",SocketAddr::appendTo(a,p).c_str(),this);
}

void SIPParty::getAddr(String& addr, int& port, bool local)
{
    Lock lock(m_mutex);
    addr = local ? m_local : m_party;
    port = local ? m_localPort : m_partyPort;
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
    TelEngine::destruct(m_transaction);
    TelEngine::destruct(m_message);
}


SIPEngine::SIPEngine(const char* userAgent)
    : Mutex(true,"SIPEngine"),
      m_t1(500000), m_t4(5000000), m_reqTransCount(5), m_rspTransCount(6),
      m_maxForwards(70),
      m_flags(0), m_lazyTrying(false),
      m_userAgent(userAgent), m_nc(0), m_nonce_time(0),
      m_nonce_mutex(false,"SIPEngine::nonce"),
      m_autoChangeParty(false)
{
    debugName("sipengine");
    DDebug(this,DebugInfo,"SIPEngine::SIPEngine() [%p]",this);
    m_seq = new SIPSequence;
    m_seq->deref();
    if (m_userAgent.null())
	m_userAgent << "YATE/" << YATE_VERSION;
    m_allowed = "ACK";
    char tmp[32];
    ::snprintf(tmp,sizeof(tmp),"%08x",(int)(Random::random() ^ Time::now()));
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
    const MimeHeaderLine* hl = message->getLastHeader("Via");
    if (!hl)
#ifdef SIP_STRICT
	return 0;
#else
	Debug(this,DebugMild,"Received message with no Via header! (sender bug)");
#endif
    const NamedString* br = hl ? hl->getParam("branch") : 0;
    String branch;
    if (br && br->startsWith("z9hG4bK"))
	branch = *br;
    Lock lock(this);
    SIPTransaction* forked = 0;
    ObjList* l = &m_transList;
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

SIPTransaction* SIPEngine::forkInvite(SIPMessage* answer, SIPTransaction* trans)
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
    Lock lock(this);
    ObjList* l = m_transList.skipNull();
    if (!l)
	return 0;
    u_int64_t time = Time::now();
    for (; l; l = l->skipNext()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	SIPEvent* e = t->getEvent(true,time);
	if (e) {
	    DDebug(this,DebugInfo,"Got pending event %p (state %s) from transaction %p [%p]",
		e,SIPTransaction::stateName(e->getState()),t,this);
	    if (t->getState() == SIPTransaction::Invalid)
		m_transList.remove(t);
	    return e;
	}
    }
    time = Time::now();
    for (l = m_transList.skipNull(); l; l = l->skipNext()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	SIPEvent* e = t->getEvent(false,time);
	if (e) {
	    DDebug(this,DebugInfo,"Got event %p (state %s) from transaction %p [%p]",
		e,SIPTransaction::stateName(e->getState()),t,this);
	    if (t->getState() == SIPTransaction::Invalid)
		m_transList.remove(t);
	    return e;
	}
    }
    return 0;
}

void SIPEngine::processEvent(SIPEvent *event)
{
    if (!event)
	return;
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
		    if (event->getParty() && !event->getParty()->transmit(event) &&
			event->getTransaction())
			event->getTransaction()->msgTransmitFailed(event->getMessage());
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
    // by default allow almost 3 minutes (proxy INVITE) for user interaction
    return getTimer('C') - getTimer('2');
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

// Get a nonce count
void SIPEngine::ncGet(String& nc)
{
    m_nonce_mutex.lock();
    if (!(++m_nc))
	++m_nc;
    u_int32_t val = m_nc;
    m_nonce_mutex.unlock();
    char tmp[9];
    ::sprintf(tmp,"%08x",val);
    nc = tmp;
}

bool SIPEngine::checkUser(String& username, const String& realm, const String& nonce,
    const String& method, const String& uri, const String& response,
    const SIPMessage* message, const MimeHeaderLine* authLine, GenObject* userData)
{
    return false;
}

bool SIPEngine::checkAuth(bool noUser, String& username, const SIPMessage* message,
    const MimeHeaderLine* authLine, GenObject* userData)
{
    return message && noUser && checkUser(username,"","",message->method,message->uri,"",message,authLine,userData);
}

// response = md5(md5(username:realm:password):nonce:md5(method:uri))
// qop=auth --> response = md5(md5(username:realm:password):nonce:nc:cnonce:qop:md5(method:uri))
void SIPEngine::buildAuth(const String& username, const String& realm, const String& passwd,
    const String& nonce, const String& method, const String& uri, String& response,
    const NamedList& qop)
{
    XDebug(DebugAll,"SIP Building auth: '%s:%s:%s' '%s' '%s:%s'",
	username.c_str(),realm.c_str(),passwd.c_str(),nonce.c_str(),method.c_str(),uri.c_str());
    MD5 m1,m2;
    m1 << username << ":" << realm << ":" << passwd;
    m2 << method << ":" << uri;
    String tmp;
    tmp << m1.hexDigest() << ":" << nonce << ":";
    if (qop) {
	if (qop == YSTRING("auth"))
	    tmp << qop[YSTRING("nc")] << ":" << qop[YSTRING("cnonce")] << ":" << qop.c_str() << ":";
	else
	    Debug(DebugStub,"SIPEngine::buildAuth() not implemented for qop=%s",
		qop.c_str());
    }
    tmp << m2.hexDigest();
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

int SIPEngine::authUser(const SIPMessage* message, String& user, bool proxy, GenObject* userData)
{
    if (!message)
	return -1;
    const MimeHeaderLine* authLine = 0;
    const MimeHeaderLine* bestLine = 0;
    long bestAge = -1;
    String bestNonce;
    const char* hdr = proxy ? "Proxy-Authorization" : "Authorization";
    const ObjList* l = &message->header;
    for (; l; l = l->next()) {
	const GenObject* o = l->get();
	if (!o)
	    continue;
	const MimeHeaderLine* t = static_cast<const MimeHeaderLine*>(o->getObject(YATOM("MimeHeaderLine")));
	if (!t || (t->name() |= hdr))
	    continue;
	// remember this line for foreign authentication
	if (!authLine)
	    authLine = t;
	if ((*t |= "Digest"))
	    continue;
	String nonce(t->getParam(YSTRING("nonce")));
	MimeHeaderLine::delQuotes(nonce);
	// TODO: implement a nonce cache for the stupid clients that don't send it back
	if (nonce.null())
	    continue;
	// see if the nonce was generated by this engine
	long age = nonceAge(nonce);
	if (age < 0)
	    continue;
	if (bestAge < 0 || bestAge > age) {
	    // nonce is newer - remember this line
	    bestAge = age;
	    bestNonce = nonce;
	    bestLine = t;
	}
	if (authLine == t)
	    authLine = 0;
    }

    if (bestLine) {
	String usr(bestLine->getParam("username"));
	MimeHeaderLine::delQuotes(usr);
	// if we know the username check if it matches
	if (usr && (user.null() || (usr == user))) {
	    XDebug(this,DebugAll,"authUser nonce age is %ld for '%s'",
		bestAge,usr.c_str());
	    String res(bestLine->getParam("response"));
	    MimeHeaderLine::delQuotes(res);
	    if (res) {
		String uri(bestLine->getParam("uri"));
		MimeHeaderLine::delQuotes(uri);
		if (uri.null())
		    uri = message->uri;
		String realm(bestLine->getParam("realm"));
		MimeHeaderLine::delQuotes(realm);

		if (checkUser(usr,realm,bestNonce,message->method,uri,res,message,0,userData)) {
		    if (user.null())
			user = usr;
		    return bestAge;
		}
	    }
	}
	else
	    bestLine = 0;
    }
    // we got no auth headers for nonce - try to authenticate by other means
    return checkAuth(!bestLine,user,message,authLine,userData) ? 0 : -1;
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
    lock();
    if (method && *method && !isAllowed(method))
	m_allowed << ", " << method;
    unlock();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
