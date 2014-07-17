/**
 * message.cpp
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
#include "util.h"

#include <string.h>
#include <stdlib.h>


using namespace TelEngine;

static Regexp s_angled("<\\([^>]\\+\\)>");

SIPMessage::SIPMessage(const SIPMessage& original)
    : RefObject(),
      version(original.version), method(original.method), uri(original.uri),
      code(original.code), reason(original.reason),
      body(0), m_ep(0),
      m_valid(original.isValid()), m_answer(original.isAnswer()),
      m_outgoing(original.isOutgoing()), m_ack(original.isACK()),
      m_cseq(-1), m_flags(original.getFlags())
{
    DDebug(DebugAll,"SIPMessage::SIPMessage(&%p) [%p]",
	&original,this);
    if (original.body)
	setBody(original.body->clone());
    setParty(original.getParty());
    setSequence(original.getSequence());
    bool via1 = true;
    const ObjList* l = &original.header;
    for (; l; l = l->next()) {
	const MimeHeaderLine* hl = static_cast<MimeHeaderLine*>(l->get());
	if (!hl)
	    continue;
	// CSeq must not be copied, a new one will be built by complete()
	if (hl->name() &= "CSeq")
	    continue;
	MimeHeaderLine* nl = hl->clone();
	// this is a new transaction so let complete() add randomness
	if (via1 && (nl->name() &= "Via")) {
	    via1 = false;
	    nl->delParam("branch");
	}
	addHeader(nl);
    }
}

SIPMessage::SIPMessage(const char* _method, const char* _uri, const char* _version)
    : version(_version), method(_method), uri(_uri), code(0),
      body(0), m_ep(0), m_valid(true),
      m_answer(false), m_outgoing(true), m_ack(false), m_cseq(-1), m_flags(-1)
{
    DDebug(DebugAll,"SIPMessage::SIPMessage('%s','%s','%s') [%p]",
	_method,_uri,_version,this);
}

SIPMessage::SIPMessage(SIPParty* ep, const char* buf, int len, unsigned int* bodyLen)
    : code(0), body(0), m_ep(ep), m_valid(false),
      m_answer(false), m_outgoing(false), m_ack(false), m_cseq(-1), m_flags(-1)
{
    DDebug(DebugInfo,"SIPMessage::SIPMessage(%p,%d) [%p]\r\n------\r\n%s------",
	buf,len,this,buf);
    if (m_ep)
	m_ep->ref();
    if (!(buf && *buf)) {
	Debug(DebugWarn,"Empty message text in [%p]",this);
	return;
    }
    if (len < 0)
	len = ::strlen(buf);
    m_valid = parse(buf,len,bodyLen);
}

SIPMessage::SIPMessage(const SIPMessage* message, int _code, const char* _reason)
    : code(_code), body(0),
      m_ep(0), m_valid(false),
      m_answer(true), m_outgoing(true), m_ack(false), m_cseq(-1), m_flags(-1)
{
    DDebug(DebugAll,"SIPMessage::SIPMessage(%p,%d,'%s') [%p]",
	message,_code,_reason,this);
    if (!_reason)
	_reason = lookup(code,SIPResponses,"Unknown Reason Code");
    reason = _reason;
    if (!(message && message->isValid()))
	return;
    m_flags = message->getFlags();
    m_ep = message->getParty();
    if (m_ep)
	m_ep->ref();
    version = message->version;
    uri = message->uri;
    method = message->method;
    m_cseq = message->getCSeq();
    copyAllHeaders(message,"Via");
    copyAllHeaders(message,"Record-Route");
    copyHeader(message,"From");
    copyHeader(message,"To");
    copyHeader(message,"Call-ID");
    copyHeader(message,"CSeq");
    m_valid = true;
}

SIPMessage::SIPMessage(const SIPMessage* original, const SIPMessage* answer)
    : method("ACK"), code(0),
      body(0), m_ep(0), m_valid(false),
      m_answer(false), m_outgoing(true), m_ack(true), m_cseq(-1), m_flags(-1)
{
    DDebug(DebugAll,"SIPMessage::SIPMessage(%p,%p) [%p]",original,answer,this);
    if (!(original && original->isValid()))
	return;
    m_flags = original->getFlags();
    m_ep = original->getParty();
    if (m_ep)
	m_ep->ref();
    version = original->version;
    uri = original->uri;
    copyAllHeaders(original,"Via");
    MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(getHeader("Via"));
    if (!hl) {
	String tmp;
	tmp << version << "/" << getParty()->getProtoName();
	if (getParty()) {
	    tmp << " ";
	    getParty()->appendAddr(tmp,true);
	}
	hl = new MimeHeaderLine("Via",tmp);
	header.append(hl);
    }
    if (answer && (answer->code == 200) && (original->method &= "INVITE")) {
	String tmp("z9hG4bK");
	tmp << (int)Random::random();
	hl->setParam("branch",tmp);
	const MimeHeaderLine* co = answer->getHeader("Contact");
	if (co) {
	    uri = *co;
	    static Regexp r("^[^<]*<\\([^>]*\\)>.*$");
	    if (uri.matches(r))
		uri = uri.matchString(1);
	}
	// new transaction - get/apply routeset unless INVITE already knew it
	if (!original->getHeader("Route")) {
	    ObjList* routeset = answer->getRoutes();
	    addRoutes(routeset);
	    TelEngine::destruct(routeset);
	}
    }
    m_cseq = original->getCSeq();
    copyAllHeaders(original,"Route");
    copyHeader(original,"From");
    copyHeader(original,"To");
    copyHeader(original,"Call-ID");
    String tmp;
    tmp << m_cseq << " " << method;
    addHeader("CSeq",tmp);
    copyHeader(original,"Max-Forwards");
    copyAllHeaders(original,"Contact");
    copyAllHeaders(original,"Authorization");
    copyAllHeaders(original,"Proxy-Authorization");
    copyHeader(original,"User-Agent");
    m_valid = true;
}

SIPMessage::~SIPMessage()
{
    DDebug(DebugAll,"SIPMessage::~SIPMessage() [%p]",this);
    m_valid = false;
    setParty();
    setBody();
}

void SIPMessage::complete(SIPEngine* engine, const char* user, const char* domain, const char* dlgTag, int flags)
{
    DDebug(engine,DebugAll,"SIPMessage::complete(%p,'%s','%s','%s',%d)%s%s%s [%p]",
	engine,user,domain,dlgTag,flags,
	isACK() ? " ACK" : "",
	isOutgoing() ? " OUT" : "",
	isAnswer() ? " ANS" : "",
	this);
    if (!engine)
	return;
    if (-1 == flags)
	flags = m_flags;
    if (-1 == flags)
	flags = engine->flags();
    m_flags = flags;

    // don't complete incoming messages
    if (!isOutgoing())
	return;

    if (!getParty()) {
	engine->buildParty(this);
	if (!getParty()) {
	    Debug(engine,DebugGoOn,"Could not complete party-less SIP message [%p]",this);
	    return;
	}
    }
    String partyLAddr;
    int partyLPort = 0;
    getParty()->getAddr(partyLAddr,partyLPort,true);

    // only set the dialog tag on ACK
    if (isACK()) {
	MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(getHeader("To"));
	if (dlgTag && hl && !hl->getParam("tag"))
	    hl->setParam("tag",dlgTag);
	return;
    }

    String localDomain;
    if (!domain) {
	if (partyLPort && (partyLPort != 5060))
	    SocketAddr::appendTo(localDomain,partyLAddr,partyLPort);
	else
	    SocketAddr::appendAddr(localDomain,partyLAddr);
	domain = localDomain;
    }

    MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(getHeader("Via"));
    if (!hl) {
	String tmp;
	tmp << version << "/" << getParty()->getProtoName();
	tmp << " ";
	SocketAddr::appendTo(tmp,partyLAddr,partyLPort);
	hl = new MimeHeaderLine("Via",tmp);
	if (isReliable() && 0 == (flags & NoConnReuse))
	    hl->setParam("alias");
	if (!((flags & (NotReqRport|RportAfterBranch)) || isAnswer() || isACK()))
	    hl->setParam("rport");
	header.append(hl);
    }
    if (!(isAnswer() || hl->getParam("branch"))) {
	String tmp("z9hG4bK");
	tmp << (unsigned int)Random::random();
	hl->setParam("branch",tmp);
    }
    if (isAnswer()) {
	if (!(flags & NotSetReceived)) {
	    Lock lock(getParty()->mutex());
	    hl->setParam("received",getParty()->getPartyAddr());
	}
	const String* rport = hl->getParam("rport");
	if (rport && rport->null() && !(flags & NotSetRport))
	    const_cast<String&>(*rport) = getParty()->getPartyPort();
    }
    else if ((flags & RportAfterBranch) && !((flags & NotReqRport) || isACK() || hl->getParam("rport")))
	hl->setParam("rport");

    if (!isAnswer()) {
	hl = const_cast<MimeHeaderLine*>(getHeader("From"));
	if (!hl) {
	    String tmp = "<sip:";
	    if (user)
		tmp << String::uriEscape(user,'@',"+?&") << "@";
	    tmp << domain << ">";
	    hl = new MimeHeaderLine("From",tmp);
	    header.append(hl);
	}
	if (!hl->getParam("tag"))
	    hl->setParam("tag",String((unsigned int)Random::random()));
    }

    hl = const_cast<MimeHeaderLine*>(getHeader("To"));
    if (!(isAnswer() || hl)) {
	String tmp;
	tmp << "<" << uri << ">";
	hl = new MimeHeaderLine("To",tmp);
	header.append(hl);
    }
    if (hl && dlgTag && !hl->getParam("tag"))
	hl->setParam("tag",dlgTag);

    if (!(isAnswer() || getHeader("Call-ID"))) {
	String tmp;
	tmp << (unsigned int)Random::random() << "@" << domain;
	addHeader("Call-ID",tmp);
    }

    if (!isAnswer()) {
	hl = const_cast<MimeHeaderLine*>(getHeader("CSeq"));
	if (hl) {
	    if (m_cseq <= 0) {
		int sep = hl->find(' ');
		if (sep > 0)
		    m_cseq = hl->substr(sep).toInteger(-1,10);
	    }
	}
	else {
	    String tmp;
	    if (m_cseq <= 0) {
		SIPSequence* seq = getSequence();
		if (!seq)
		    seq = engine->getSequence();
#ifdef DEBUG
		else
		    Debug(engine,DebugAll,"Using local sequence %p last=%d [%p]",
			seq,seq->getLastCSeq(),this);
#endif
		m_cseq = seq->getNextCSeq();
	    }
	    tmp << m_cseq << " " << method;
	    addHeader("CSeq",tmp);
	}
    }

    const char* info = isAnswer() ? "Server" : "User-Agent";
    if (!((flags & NotAddAgent) || getHeader(info) || engine->getUserAgent().null()))
	addHeader(info,engine->getUserAgent());

    // keep 100 answers short - they are hop to hop anyway
    if (isAnswer() && (code == 100))
	return;

    if (!(isAnswer() || getHeader("Max-Forwards"))) {
	String tmp(engine->getMaxForwards());
	addHeader("Max-Forwards",tmp);
    }

    if ((method == YSTRING("INVITE")) && !getHeader("Contact")) {
	// automatically add a contact field to (re)INVITE and its answers
	String tmp(user);
	if (!tmp) {
	    tmp = uri;
	    static Regexp r(":\\([^:@]*\\)@");
	    tmp.matches(r);
	    tmp = tmp.matchString(1).uriUnescape();
	}
	if (tmp)
	    tmp = tmp.uriEscape('@',"+?&") + "@";
	tmp = "<sip:" + tmp;
	SocketAddr::appendTo(tmp,partyLAddr,partyLPort) << ">";
	addHeader("Contact",tmp);
    }

    if (!((flags & NotAddAllow) || getHeader("Allow")))
	addHeader("Allow",engine->getAllowed());
}

bool SIPMessage::copyHeader(const SIPMessage* message, const char* name, const char* newName)
{
    const MimeHeaderLine* hl = message ? message->getHeader(name) : 0;
    if (hl) {
	header.append(hl->clone(newName));
	return true;
    }
    return false;
}

int SIPMessage::copyAllHeaders(const SIPMessage* message, const char* name, const char* newName)
{
    if (!(message && name && *name))
	return 0;
    int c = 0;
    const ObjList* l = &message->header;
    for (; l; l = l->next()) {
	const MimeHeaderLine* hl = static_cast<const MimeHeaderLine*>(l->get());
	if (hl && (hl->name() &= name)) {
	    ++c;
	    header.append(hl->clone(newName));
	}
    }
    return c;
}

bool SIPMessage::parseFirst(String& line)
{
    XDebug(DebugAll,"SIPMessage::parse firstline= '%s'",line.c_str());
    if (line.null())
	return false;
    static Regexp r("^\\([Ss][Ii][Pp]/[0-9]\\.[0-9]\\+\\)[[:space:]]\\+\\([0-9][0-9][0-9]\\)[[:space:]]\\+\\(.*\\)$");
    if (line.matches(r)) {
	// Answer: <version> <code> <reason-phrase>
	m_answer = true;
	version = line.matchString(1).toUpper();
	code = line.matchString(2).toInteger();
	reason = line.matchString(3);
	DDebug(DebugAll,"got answer version='%s' code=%d reason='%s'",
	    version.c_str(),code,reason.c_str());
    }
    else {
	static Regexp r2("^\\([[:alpha:]]\\+\\)[[:space:]]\\+\\([^[:space:]]\\+\\)[[:space:]]\\+\\([Ss][Ii][Pp]/[0-9]\\.[0-9]\\+\\)$");
	if (line.matches(r2)) {
	    // Request: <method> <uri> <version>
	    m_answer = false;
	    method = line.matchString(1).toUpper();
	    uri = line.matchString(2);
	    version = line.matchString(3).toUpper();
	    DDebug(DebugAll,"got request method='%s' uri='%s' version='%s'",
		method.c_str(),uri.c_str(),version.c_str());
	    if (method == YSTRING("ACK"))
		m_ack = true;
	}
	else {
	    Debug(DebugAll,"Invalid SIP line '%s'",line.c_str());
	    return false;
	}
    }
    return true;
}

bool SIPMessage::parse(const char* buf, int len, unsigned int* bodyLen)
{
    DDebug(DebugAll,"SIPMessage::parse(%p,%d) [%p]",buf,len,this);
    String* line = 0;
    while (len > 0) {
	line = MimeBody::getUnfoldedLine(buf,len);
	if (!line->null())
	    break;
	// Skip any initial empty lines
	TelEngine::destruct(line);
    }
    if (!line)
	return false;
    if (!parseFirst(*line)) {
	line->destruct();
	return false;
    }
    line->destruct();
    int clen = -1;
    while (len > 0) {
	line = MimeBody::getUnfoldedLine(buf,len);
	if (line->null()) {
	    // Found end of headers
	    line->destruct();
	    break;
	}
	int col = line->find(':');
	if (col <= 0) {
	    line->destruct();
	    return false;
	}
	String name = line->substr(0,col);
	name.trimBlanks();
	if (name.null()) {
	    line->destruct();
	    return false;
	}
	name = uncompactForm(name);
	*line >> ":";
	line->trimBlanks();
	XDebug(DebugAll,"SIPMessage::parse header='%s' value='%s'",name.c_str(),line->c_str());

	if ((name &= "WWW-Authenticate") ||
	    (name &= "Proxy-Authenticate") ||
	    (name &= "Authorization") ||
	    (name &= "Proxy-Authorization"))
	    header.append(new MimeAuthLine(name,*line));
	else
	    header.append(new MimeHeaderLine(name,*line));

	if ((clen < 0) && (name &= "Content-Length"))
	    clen = line->toInteger(-1,10);
	else if ((m_cseq < 0) && (name &= "CSeq")) {
	    int sep = line->find(' ');
	    if (sep > 0) {
		m_cseq = line->substr(0,sep).toInteger(-1,10);
		if (m_answer) {
		    method = line->substr(sep + 1);
		    method.trimBlanks().toUpper();
		}
	    }
	}
	line->destruct();
    }
    if (!bodyLen) {
	if (clen >= 0) {
	    if (clen > len)
		Debug("SIPMessage",DebugMild,"Content length is %d but only %d in buffer",clen,len);
	    else if (clen < len) {
		DDebug("SIPMessage",DebugInfo,"Got %d garbage bytes after content",len - clen);
		len = clen;
	    }
	}
	buildBody(buf,len);
    }
    else
	*bodyLen = (clen >= 0) ? clen : 0;
    DDebug(DebugAll,"SIPMessage::parse %d header lines, body %p",
	header.count(),body);
    return true;
}

SIPMessage* SIPMessage::fromParsing(SIPParty* ep, const char* buf, int len, unsigned int* bodyLen)
{
    SIPMessage* msg = new SIPMessage(ep,buf,len,bodyLen);
    if (msg->isValid())
	return msg;
    DDebug("SIPMessage",DebugInfo,"Invalid message");
    msg->destruct();
    return 0;
}

// Build message's body. Reset it before
void SIPMessage::buildBody(const char* buf, int len)
{
    TelEngine::destruct(body);
    if (!buf)
	return;
    if (len < 0)
	len = ::strlen(buf);
    const MimeHeaderLine* cType = getHeader("Content-Type");
    if (cType)
	body = MimeBody::build(buf,len,*cType);
    // Move extra Content- header lines to body
    if (body) {
	ListIterator iter(header);
	for (GenObject* o = 0; (o = iter.get());) {
	    MimeHeaderLine* line = static_cast<MimeHeaderLine*>(o);
	    if (!line->startsWith("Content-",false,true) || (*line &= "Content-Length"))
		continue;
	    // Delete Content-Type and move all other lines to body
	    bool delobj = (line == cType);
	    header.remove(o,delobj);
	    if (!delobj)
		body->appendHdr(line);
	}
    }
    DDebug(DebugAll,"SIPMessage::buildBody %d header lines, body %p",
	header.count(),body);
}

const MimeHeaderLine* SIPMessage::getHeader(const char* name) const
{
    if (!(name && *name))
	return 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const MimeHeaderLine* t = static_cast<const MimeHeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    return t;
    }
    return 0;
}

const MimeHeaderLine* SIPMessage::getLastHeader(const char* name) const
{
    if (!(name && *name))
	return 0;
    const MimeHeaderLine* res = 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const MimeHeaderLine* t = static_cast<const MimeHeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    res = t;
    }
    return res;
}

void SIPMessage::clearHeaders(const char* name)
{
    if (!(name && *name))
	return;
    ObjList* l = &header;
    while (l) {
	const MimeHeaderLine* t = static_cast<const MimeHeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    l->remove();
	else
	    l = l->next();
    }
}

int SIPMessage::countHeaders(const char* name) const
{
    if (!(name && *name))
	return 0;
    int res = 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const MimeHeaderLine* t = static_cast<const MimeHeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    ++res;
    }
    return res;
}

const NamedString* SIPMessage::getParam(const char* name, const char* param, bool last) const
{
    const MimeHeaderLine* hl = last ? getLastHeader(name) : getHeader(name);
    return hl ? hl->getParam(param) : 0;
}

const String& SIPMessage::getHeaderValue(const char* name, bool last) const
{
    const MimeHeaderLine* hl = last ? getLastHeader(name) : getHeader(name);
    return hl ? *static_cast<const String*>(hl) : String::empty();
}

const String& SIPMessage::getParamValue(const char* name, const char* param, bool last) const
{
    const NamedString* ns = getParam(name,param,last);
    return ns ? *static_cast<const String*>(ns) : String::empty();
}

const String& SIPMessage::getHeaders() const
{
    if (isValid() && m_string.null()) {
	if (isAnswer())
	    m_string << version << " " << code << " " << reason << "\r\n";
	else
	    m_string << method << " " << uri << " " << version << "\r\n";

	const ObjList* l = &header;
	for (; l; l = l->next()) {
	    MimeHeaderLine* t = static_cast<MimeHeaderLine*>(l->get());
	    if (t) {
		t->buildLine(m_string);
		m_string << "\r\n";
	    }
	}
    }
    return m_string;
}

const DataBlock& SIPMessage::getBuffer() const
{
    if (isValid() && m_data.null()) {
	m_data.assign((void*)(getHeaders().c_str()),getHeaders().length());
	if (body) {
	    String s;
	    body->buildHeaders(s);
	    s << "Content-Length: " << body->getBody().length() << "\r\n\r\n";
	    m_data += s;
	}
	else
	    m_data += "Content-Length: 0\r\n\r\n";
	if (body)
	    m_data += body->getBody();
#ifdef DEBUG
	if (debugAt(DebugInfo)) {
	    String buf((char*)m_data.data(),m_data.length());
	    Debug(DebugInfo,"SIPMessage::getBuffer() [%p]\r\n------\r\n%s------",
		this,buf.c_str());
	}
#endif
    }
    return m_data;
}

void SIPMessage::setBody(MimeBody* newbody)
{
    if (newbody == body)
	return;
    TelEngine::destruct(body);
    body = newbody;
}

void SIPMessage::setParty(SIPParty* ep)
{
    if (ep == m_ep)
	return;
    if (ep && !ep->ref())
	ep = 0;
    XDebug(DebugAll,"SIPMessage::setParty(%p) current=%p [%p]",ep,m_ep,this);
    SIPParty* tmp = m_ep;
    m_ep = ep;
    TelEngine::destruct(tmp);
}

MimeAuthLine* SIPMessage::buildAuth(const String& username, const String& password,
    const String& meth, const String& uri, bool proxy, SIPEngine* engine) const
{
    const char* hdr = proxy ? "Proxy-Authenticate" : "WWW-Authenticate";
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const MimeAuthLine* t = YOBJECT(MimeAuthLine,l->get());
	if (t && (t->name() &= hdr) && (*t &= "Digest")) {
	    String nonce(t->getParam("nonce"));
	    MimeHeaderLine::delQuotes(nonce);
	    if (nonce.null())
		continue;
	    String realm(t->getParam("realm"));
	    MimeHeaderLine::delQuotes(realm);
	    int par = uri.find(';');
	    String msguri = uri.substr(0,par);
	    NamedList qop(TelEngine::c_safe(t->getParam("qop")));
	    if (qop) {
		MimeHeaderLine::delQuotes(qop);
		if (qop == YSTRING("auth")) {
		    String nc("00000001");
		    if (engine)
			engine->ncGet(nc);
		    qop.addParam("nc",nc);
		    MD5 md5;
		    md5 << String((unsigned int)Random::random()) << nc << String(Time::secNow());
		    qop.addParam("cnonce",md5.hexDigest());
		}
		else
		    continue;
	    }
	    String response;
	    SIPEngine::buildAuth(username,realm,password,nonce,meth,msguri,response,qop);
	    MimeAuthLine* auth = new MimeAuthLine(proxy ? "Proxy-Authorization" : "Authorization","Digest");
	    auth->setParam("username",MimeHeaderLine::quote(username));
	    auth->setParam("realm",MimeHeaderLine::quote(realm));
	    auth->setParam("nonce",MimeHeaderLine::quote(nonce));
	    auth->setParam("uri",MimeHeaderLine::quote(msguri));
	    auth->setParam("response",MimeHeaderLine::quote(response));
	    auth->setParam("algorithm","MD5");
	    // copy opaque data as-is, only if present
	    const NamedString* opaque = t->getParam(YSTRING("opaque"));
	    if (opaque)
		auth->setParam(opaque->name(),*opaque);
	    if (qop) {
		auth->setParam("qop",qop);
		NamedIterator iter(qop);
		for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
		    if (ns->name() == YSTRING("nc"))
			auth->setParam(ns->name(),*ns);
		    else
			auth->setParam(ns->name(),MimeHeaderLine::quote(*ns));
		}
	    }
	    return auth;
	}
    }
    return 0;
}

MimeAuthLine* SIPMessage::buildAuth(const SIPMessage& original, SIPEngine* engine) const
{
    if (original.getAuthUsername().null())
	return 0;
    return buildAuth(original.getAuthUsername(),original.getAuthPassword(),
	original.method,original.uri,(code == 407),engine);
}

ObjList* SIPMessage::getRoutes() const
{
    ObjList* list = 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const MimeHeaderLine* h = YOBJECT(MimeHeaderLine,l->get());
	if (h && (h->name() &= "Record-Route")) {
	    int p = 0;
	    while (p >= 0) {
		MimeHeaderLine* line = 0;
		int s = MimeHeaderLine::findSep(*h,',',p);
		String tmp;
		if (s < 0) {
		    if (p)
			tmp = h->substr(p);
		    else
			line = new MimeHeaderLine(*h,"Route");
		    p = -1;
		}
		else {
		    if (s > p)
			tmp = h->substr(p,s-p);
		    p = s + 1;
		}
		tmp.trimBlanks();
		if (tmp)
		    line = new MimeHeaderLine("Route",tmp);
		if (!line)
		    continue;
		if (!list)
		    list = new ObjList;
		if (isAnswer())
		    // route set learned from an answer, reverse order
		    list->insert(line);
		else
		    // route set learned from a request, preserve order
		    list->append(line);
	    }
	}
    }
    return list;
}

void SIPMessage::addRoutes(const ObjList* routes)
{
    if (isAnswer() || !routes)
	return;
    MimeHeaderLine* hl = YOBJECT(MimeHeaderLine,routes->get());
    if (hl) {
	// check if first route is to a RFC 2543 proxy
	String tmp = *hl;
	if (tmp.matches(s_angled))
	    tmp = tmp.matchString(1);
	if (tmp.find(";lr") < 0) {
	    // prepare a new final route
	    hl = new MimeHeaderLine("Route","<" + uri + ">");
	    // set the first route as Request-URI and then skip it
	    uri = tmp;
	    routes = routes->next();
	}
	else
	    hl = 0;
    }

    // add (remaining) routes
    for (; routes; routes = routes->next()) {
	const MimeHeaderLine* h = YOBJECT(MimeHeaderLine,routes->get());
	if (h)
	    addHeader(h->clone());
    }

    // if first route was to a RFC 2543 proxy add the old Request-URI
    if (hl)
	addHeader(hl);
}


SIPDialog::SIPDialog(const SIPDialog& original)
    : String(original),
      localURI(original.localURI), localTag(original.localTag),
      remoteURI(original.remoteURI), remoteTag(original.remoteTag),
      remoteCSeq(original.remoteCSeq), m_seq(original.getSequence())
{
    DDebug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
}

SIPDialog& SIPDialog::operator=(const SIPDialog& original)
{
    String::operator=(original);
    localURI = original.localURI;
    localTag = original.localTag;
    remoteURI = original.remoteURI;
    remoteTag = original.remoteTag;
    remoteCSeq = original.remoteCSeq;
    setSequence(original.getSequence());
    DDebug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
    return *this;
}

SIPDialog& SIPDialog::operator=(const String& callid)
{
    String::operator=(callid);
    localURI.clear();
    localTag.clear();
    remoteURI.clear();
    remoteTag.clear();
    DDebug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
    return *this;
}

SIPDialog::SIPDialog(const SIPMessage& message)
    : String(message.getHeaderValue("Call-ID")),
     remoteCSeq(-1)
{
    bool local = message.isOutgoing() ^ message.isAnswer();
    const MimeHeaderLine* hl = message.getHeader(local ? "From" : "To");
    localURI = hl;
    if (localURI.matches(s_angled))
        localURI = localURI.matchString(1);
    if (hl)
	localTag = hl->getParam("tag");
    hl = message.getHeader(local ? "To" : "From");
    remoteURI = hl;
    if (remoteURI.matches(s_angled))
        remoteURI = remoteURI.matchString(1);
    if (hl)
	remoteTag = hl->getParam("tag");
    setSequence(message.getSequence());
    if (!(message.isOutgoing() || message.isAnswer() || message.isACK() || remoteCSeq >= message.getCSeq()))
	remoteCSeq = message.getCSeq();
    DDebug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
}

SIPDialog& SIPDialog::operator=(const SIPMessage& message)
{
    const char* cid = message.getHeaderValue("Call-ID");
    if (cid)
	String::operator=(cid);
    bool local = message.isOutgoing() ^ message.isAnswer();
    const MimeHeaderLine* hl = message.getHeader(local ? "From" : "To");
    localURI = hl;
    if (localURI.matches(s_angled))
        localURI = localURI.matchString(1);
    if (hl)
	localTag = hl->getParam("tag");
    hl = message.getHeader(local ? "To" : "From");
    remoteURI = hl;
    if (remoteURI.matches(s_angled))
        remoteURI = remoteURI.matchString(1);
    if (hl)
	remoteTag = hl->getParam("tag");
    SIPSequence* seq = message.getSequence();
    if (seq)
	setSequence(seq);
    if (!(message.isOutgoing() || message.isAnswer() || message.isACK() || remoteCSeq >= message.getCSeq()))
	remoteCSeq = message.getCSeq();
    DDebug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
    return *this;
}

bool SIPDialog::matches(const SIPDialog& other, bool ignoreURIs) const
{
    return
	String::operator==(other) &&
	localTag == other.localTag &&
	remoteTag == other.remoteTag &&
	(ignoreURIs ||
	    (localURI == other.localURI &&
	    remoteURI == other.remoteURI));
}

void SIPDialog::setCSeq(int32_t cseq)
{
    SIPSequence* seq = new SIPSequence(cseq);
    setSequence(seq);
    TelEngine::destruct(seq);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
