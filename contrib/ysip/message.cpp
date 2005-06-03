/**
 * message.cpp
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
#include "util.h"

#include <string.h>
#include <stdlib.h>


using namespace TelEngine;

SIPHeaderLine::SIPHeaderLine(const char* name, const String& value, char sep)
    : NamedString(name), m_separator(sep)
{
    if (value.null())
	return;
    XDebug(DebugAll,"SIPHeaderLine::SIPHeaderLine('%s','%s') [%p]",name,value.c_str(),this);
    int sp = value.find(m_separator);
    // skip past URIs with parameters
    int lim = value.find('<');
    if ((sp >= 0) && (lim >= 0) && (lim < sp)) {
	lim = value.find('>');
	sp = value.find(m_separator,lim);
    }
    if (sp < 0) {
	assign(value);
	return;
    }
    assign(value,sp);
    trimBlanks();
    while (sp < (int)value.length()) {
	int ep = value.find(m_separator,sp+1);
	if (ep <= sp)
	    ep = value.length();
	int eq = value.find('=',sp+1);
	if ((eq > 0) && (eq < ep)) {
	    String pname(value.substr(sp+1,eq-sp-1));
	    String pvalue(value.substr(eq+1,ep-eq-1));
	    pname.trimBlanks();
	    pvalue.trimBlanks();
	    if (!pname.null()) {
		XDebug(DebugAll,"hdr param name='%s' value='%s'",pname.c_str(),pvalue.c_str());
		m_params.append(new NamedString(pname,pvalue));
	    }
	}
	else {
	    String pname(value.substr(sp+1,ep-sp-1));
	    pname.trimBlanks();
	    if (!pname.null()) {
		XDebug(DebugAll,"hdr param name='%s' (no value)",pname.c_str());
		m_params.append(new NamedString(pname));
	    }
	}
	sp = ep;
    }
}

SIPHeaderLine::SIPHeaderLine(const SIPHeaderLine& original)
    : NamedString(original.name(),original), m_separator(original.separator())
{
    XDebug(DebugAll,"SIPHeaderLine::SIPHeaderLine(%p '%s') [%p]",&original,name().c_str(),this);
    const ObjList* l = &original.params();
    for (; l; l = l->next()) {
	const NamedString* t = static_cast<const NamedString*>(l->get());
	if (t)
	    m_params.append(new NamedString(t->name(),*t));
    }
}

SIPHeaderLine::~SIPHeaderLine()
{
    XDebug(DebugAll,"SIPHeaderLine::~SIPHeaderLine() [%p]",this);
}

void* SIPHeaderLine::getObject(const String& name) const
{
    if (name == "SIPHeaderLine")
	return const_cast<SIPHeaderLine*>(this);
    return NamedString::getObject(name);
}

SIPHeaderLine* SIPHeaderLine::clone() const
{
    return new SIPHeaderLine(*this);
}

void SIPHeaderLine::buildLine(String& line) const
{
    line << name() << ": " << *this;
    const ObjList* p = &m_params;
    for (; p; p = p->next()) {
	NamedString* s = static_cast<NamedString*>(p->get());
	if (s) {
	    line << separator() << s->name();
	    if (!s->null())
		line << "=" << *s;
	}
    }
}

const NamedString* SIPHeaderLine::getParam(const char* name) const
{
    if (!(name && *name))
	return 0;
    const ObjList* l = &m_params;
    for (; l; l = l->next()) {
	const NamedString* t = static_cast<const NamedString*>(l->get());
	if (t && (t->name() &= name))
	    return t;
    }
    return 0;
}

void SIPHeaderLine::setParam(const char* name, const char* value)
{
    ObjList* p = m_params.find(name);
    if (p)
	*static_cast<NamedString*>(p->get()) = value;
    else
	m_params.append(new NamedString(name,value));
}

void SIPHeaderLine::delParam(const char* name)
{
    ObjList* p = m_params.find(name);
    if (p)
	p->remove();
}

SIPAuthLine::SIPAuthLine(const char* name, const String& value)
    : SIPHeaderLine(name,String::empty(),',')
{
    XDebug(DebugAll,"SIPAuthLine::SIPAuthLine('%s','%s') [%p]",name,value.c_str(),this);
    if (value.null())
	return;
    int sp = value.find(' ');
    if (sp < 0) {
	assign(value);
	return;
    }
    assign(value,sp);
    trimBlanks();
    while (sp < (int)value.length()) {
	int ep = value.find(m_separator,sp+1);
	int quot = value.find('"',sp+1);
	if ((quot > sp) && (quot < ep)) {
	    quot = value.find('"',quot+1);
	    if (quot > sp)
		ep = value.find(m_separator,quot+1);
	}
	if (ep <= sp)
	    ep = value.length();
	int eq = value.find('=',sp+1);
	if ((eq > 0) && (eq < ep)) {
	    String pname(value.substr(sp+1,eq-sp-1));
	    String pvalue(value.substr(eq+1,ep-eq-1));
	    pname.trimBlanks();
	    pvalue.trimBlanks();
	    if (!pname.null()) {
		XDebug(DebugAll,"auth param name='%s' value='%s'",pname.c_str(),pvalue.c_str());
		m_params.append(new NamedString(pname,pvalue));
	    }
	}
	else {
	    String pname(value.substr(sp+1,ep-sp-1));
	    pname.trimBlanks();
	    if (!pname.null()) {
		XDebug(DebugAll,"auth param name='%s' (no value)",pname.c_str());
		m_params.append(new NamedString(pname));
	    }
	}
	sp = ep;
    }
}

SIPAuthLine::SIPAuthLine(const SIPAuthLine& original)
    : SIPHeaderLine(original)
{
}

void* SIPAuthLine::getObject(const String& name) const
{
    if (name == "SIPAuthLine")
	return const_cast<SIPAuthLine*>(this);
    return SIPHeaderLine::getObject(name);
}

SIPHeaderLine* SIPAuthLine::clone() const
{
    return new SIPAuthLine(*this);
}

void SIPAuthLine::buildLine(String& line) const
{
    line << name() << ": " << *this;
    const ObjList* p = &m_params;
    for (bool first = true; p; p = p->next()) {
	NamedString* s = static_cast<NamedString*>(p->get());
	if (s) {
	    if (first)
		first = false;
	    else
		line << separator();
	    line << " " << s->name();
	    if (!s->null())
		line << "=" << *s;
	}
    }
}

SIPMessage::SIPMessage(const char* _method, const char* _uri, const char* _version)
    : version(_version), method(_method), uri(_uri),
      body(0), m_ep(0), m_valid(true),
      m_answer(false), m_outgoing(true), m_ack(false), m_cseq(-1)
{
    DDebug(DebugAll,"SIPMessage::SIPMessage('%s','%s','%s') [%p]",
	_method,_uri,_version,this);
}

SIPMessage::SIPMessage(SIPParty* ep, const char* buf, int len)
    : body(0), m_ep(ep), m_valid(false), m_answer(false), m_outgoing(false), m_ack(false), m_cseq(-1)
{
    Debug(DebugAll,"SIPMessage::SIPMessage(%p,%d) [%p]\n%s",
	buf,len,this,buf);
    if (m_ep)
	m_ep->ref();
    if (!(buf && *buf)) {
	Debug(DebugWarn,"Empty message text in [%p]",this);
	return;
    }
    if (len < 0)
	len = ::strlen(buf);
    m_valid = parse(buf,len);
}

SIPMessage::SIPMessage(const SIPMessage* message, int _code, const char* _reason)
    : code(_code), body(0),
      m_ep(0), m_valid(false),
      m_answer(true), m_outgoing(true), m_ack(false), m_cseq(-1)
{
    DDebug(DebugAll,"SIPMessage::SIPMessage(%p,%d,'%s') [%p]",
	message,_code,_reason,this);
    if (!_reason)
	_reason = lookup(code,SIPResponses,"Unknown Reason Code");
    reason = _reason;
    if (!(message && message->isValid()))
	return;
    m_ep = message->getParty();
    if (m_ep)
	m_ep->ref();
    version = message->version;
    uri = message->uri;
    method = message->method;
    copyAllHeaders(message,"Via");
    copyHeader(message,"From");
    copyHeader(message,"To");
    copyHeader(message,"Call-ID");
    copyHeader(message,"CSeq");
    m_valid = true;
}

SIPMessage::SIPMessage(const SIPMessage* message, bool newtran)
    : method("ACK"),
      body(0), m_ep(0), m_valid(false),
      m_answer(false), m_outgoing(true), m_ack(true), m_cseq(-1)
{
    DDebug(DebugAll,"SIPMessage::SIPMessage(%p,%d) [%p]",message,newtran,this);
    if (!(message && message->isValid()))
	return;
    m_ep = message->getParty();
    if (m_ep)
	m_ep->ref();
    version = message->version;
    uri = message->uri;
    copyAllHeaders(message,"Via");
    SIPHeaderLine* hl = const_cast<SIPHeaderLine*>(getHeader("Via"));
    if (!hl) {
	String tmp;
	tmp << version << "/" << getParty()->getProtoName();
	tmp << " " << getParty()->getLocalAddr() << ":" << getParty()->getLocalPort();
	hl = new SIPHeaderLine("Via",tmp);
	header.append(hl);
    }
    if (newtran) {
	String tmp("z9hG4bK");
	tmp << (int)::random();
	hl->setParam("branch",tmp);
    }
    copyHeader(message,"From");
    copyHeader(message,"To");
    copyHeader(message,"Call-ID");
    String tmp;
    tmp << message->getCSeq() << " " << method;
    addHeader("CSeq",tmp);
    m_valid = true;
}

SIPMessage::~SIPMessage()
{
    DDebug(DebugAll,"SIPMessage::~SIPMessage() [%p]",this);
    m_valid = false;
    setParty();
    setBody();
}

void SIPMessage::complete(SIPEngine* engine, const char* user, const char* domain, const char* dlgTag)
{
    DDebug("SIPMessage",DebugAll,"complete(%p,'%s','%s','%s')%s%s%s [%p]",
	engine,user,domain,dlgTag,
	isACK() ? " ACK" : "",
	isOutgoing() ? " OUT" : "",
	isAnswer() ? " ANS" : "",
	this);
    if (!engine)
	return;

    if (isOutgoing() && !getParty())
	engine->buildParty(this);

    // don't complete incoming messages
    if (!isOutgoing())
	return;

    // only set the dialog tag on ACK
    if (isACK()) {
	SIPHeaderLine* hl = const_cast<SIPHeaderLine*>(getHeader("To"));
	if (dlgTag && hl && !hl->getParam("tag"))
	    hl->setParam("tag",dlgTag);
	return;
    }

    if (!user)
	user = "anonymous";
    if (!domain)
	domain = getParty()->getLocalAddr();

    SIPHeaderLine* hl = const_cast<SIPHeaderLine*>(getHeader("Via"));
    if (!hl) {
	String tmp;
	tmp << version << "/" << getParty()->getProtoName();
	tmp << " " << getParty()->getLocalAddr() << ":" << getParty()->getLocalPort();
	hl = new SIPHeaderLine("Via",tmp);
	header.append(hl);
    }
    if (!(isAnswer() || hl->getParam("branch"))) {
	String tmp("z9hG4bK");
	tmp << (int)::random();
	hl->setParam("branch",tmp);
    }
    if (isAnswer()) {
	hl->setParam("received",getParty()->getPartyAddr());
	hl->setParam("rport",String(getParty()->getPartyPort()));
    }

    hl = const_cast<SIPHeaderLine*>(getHeader("From"));
    if (!hl) {
	String tmp;
	tmp << "<sip:" << user << "@" << domain << ">";
	hl = new SIPHeaderLine("From",tmp);
	header.append(hl);
    }
    if (!(isAnswer() || hl->getParam("tag")))
	hl->setParam("tag",String((int)::random()));

    hl = const_cast<SIPHeaderLine*>(getHeader("To"));
    if (!hl) {
	String tmp;
	tmp << "<" << uri << ">";
	hl = new SIPHeaderLine("To",tmp);
	header.append(hl);
    }
    if (dlgTag && !hl->getParam("tag"))
	hl->setParam("tag",dlgTag);

    if (!getHeader("Call-ID")) {
	String tmp;
	tmp << (int)::random() << "@" << domain;
	addHeader("Call-ID",tmp);
    }

    if (!getHeader("CSeq")) {
	String tmp;
	m_cseq = engine->getNextCSeq();
	tmp << m_cseq << " " << method;
	addHeader("CSeq",tmp);
    }

    if (!(isAnswer() || getHeader("Max-Forwards"))) {
	String tmp(engine->getMaxForwards());
	addHeader("Max-Forwards",tmp);
    }

    if (!getHeader("Contact")) {
	String tmp;
	if (isAnswer())
	    tmp = *getHeader("To");
	if (tmp.null()) {
	    tmp << "<sip:" << user << "@" << getParty()->getLocalAddr();
	    if (getParty()->getLocalPort() != 5060)
		tmp << ":" << getParty()->getLocalPort();
	    tmp << ">";
	}
	addHeader("Contact",tmp);
    }

    const char* info = isAnswer() ? "Server" : "User-Agent";
    if (!(getHeader(info) || engine->getUserAgent().null()))
	addHeader(info,engine->getUserAgent());

    if (!getHeader("Allow"))
	addHeader("Allow",engine->getAllowed());
}

bool SIPMessage::copyHeader(const SIPMessage* message, const char* name)
{
    const SIPHeaderLine* hl = message ? message->getHeader(name) : 0;
    if (hl) {
	header.append(hl->clone());
	return true;
    }
    return false;
}

int SIPMessage::copyAllHeaders(const SIPMessage* message, const char* name)
{
    if (!(message && name && *name))
	return 0;
    int c = 0;
    const ObjList* l = &message->header;
    for (; l; l = l->next()) {
	const SIPHeaderLine* hl = static_cast<const SIPHeaderLine*>(l->get());
	if (hl && (hl->name() &= name)) {
	    ++c;
	    header.append(hl->clone());
	}
    }
    return c;
}

bool SIPMessage::parseFirst(String& line)
{
    XDebug(DebugAll,"SIPMessage::parse firstline= '%s'",line.c_str());
    if (line.null())
	return false;
    Regexp r("^\\([Ss][Ii][Pp]/[0-9]\\.[0-9]\\+\\)[[:space:]]\\+\\([0-9][0-9][0-9]\\)[[:space:]]\\+\\(.*\\)$");
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
	r = "^\\([[:alpha:]]\\+\\)[[:space:]]\\+\\([^[:space:]]\\+\\)[[:space:]]\\+\\([Ss][Ii][Pp]/[0-9]\\.[0-9]\\+\\)$";
	if (line.matches(r)) {
	    // Request: <method> <uri> <version>
	    m_answer = false;
	    method = line.matchString(1).toUpper();
	    uri = line.matchString(2);
	    version = line.matchString(3).toUpper();
	    DDebug(DebugAll,"got request method='%s' uri='%s' version='%s'",
		method.c_str(),uri.c_str(),version.c_str());
	    if (method == "ACK")
		m_ack = true;
	}
	else {
	    Debug(DebugAll,"Invalid SIP line '%s'",line.c_str());
	    return false;
	}
    }
    return true;
}

bool SIPMessage::parse(const char* buf, int len)
{
    DDebug(DebugAll,"SIPMessage::parse(%p,%d) [%p]",buf,len,this);
    String* line = 0;
    while (len > 0) {
	line = getUnfoldedLine(&buf,&len);
	if (!line->null())
	    break;
	// Skip any initial empty lines
	line->destruct();
	line = 0;
    }
    if (!line)
	return false;
    if (!parseFirst(*line)) {
	line->destruct();
	return false;
    }
    line->destruct();
    String content;
    while (len > 0) {
	line = getUnfoldedLine(&buf,&len);
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
	    header.append(new SIPAuthLine(name,*line));
	else
	    header.append(new SIPHeaderLine(name,*line));

	if (content.null() && (name &= "Content-Type")) {
	    content = *line;
	    content.toLower();
	}
	if ((m_cseq < 0) && (name &= "CSeq")) {
	    String seq = *line;
	    seq >> m_cseq;
	    if (m_answer) {
		seq.trimBlanks().toUpper();
		method = seq;
	    }
	}
	line->destruct();
    }
    body = SIPBody::build(buf,len,content);
    DDebug(DebugAll,"SIPMessage::parse %d header lines, body %p",
	header.count(),body);
    return true;
}

SIPMessage* SIPMessage::fromParsing(SIPParty* ep, const char* buf, int len)
{
    SIPMessage* msg = new SIPMessage(ep,buf,len);
    if (msg->isValid())
	return msg;
    DDebug("SIPMessage",DebugInfo,"Invalid message");
    msg->destruct();
    return 0;
}

const SIPHeaderLine* SIPMessage::getHeader(const char* name) const
{
    if (!(name && *name))
	return 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const SIPHeaderLine* t = static_cast<const SIPHeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    return t;
    }
    return 0;
}

const SIPHeaderLine* SIPMessage::getLastHeader(const char* name) const
{
    if (!(name && *name))
	return 0;
    const SIPHeaderLine* res = 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const SIPHeaderLine* t = static_cast<const SIPHeaderLine*>(l->get());
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
	const SIPHeaderLine* t = static_cast<const SIPHeaderLine*>(l->get());
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
	const SIPHeaderLine* t = static_cast<const SIPHeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    ++res;
    }
    return res;
}

const NamedString* SIPMessage::getParam(const char* name, const char* param) const
{
    const SIPHeaderLine* hl = getHeader(name);
    return hl ? hl->getParam(param) : 0;
}

const String& SIPMessage::getHeaderValue(const char* name) const
{
    const SIPHeaderLine* hl = getHeader(name);
    return hl ? *static_cast<const String*>(hl) : String::empty();
}

const String& SIPMessage::getParamValue(const char* name, const char* param) const
{
    const NamedString* ns = getParam(name,param);
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
	    SIPHeaderLine* t = static_cast<SIPHeaderLine*>(l->get());
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
	    s << "Content-Type: " << body->getType() << "\r\n";
	    s << "Content-Length: " << body->getBody().length() << "\r\n\r\n";
	    m_data += s;
	}
	else
	    m_data += "Content-Length: 0\r\n\r\n";
	if (body)
	    m_data += body->getBody();

	String buf((char*)m_data.data(),m_data.length());
	Debug(DebugAll,"SIPMessage::getBuffer() [%p]\n%s",
	    this,buf.c_str());
    }
    return m_data;
}

void SIPMessage::setBody(SIPBody* newbody)
{
    if (newbody == body)
	return;
    if (body)
	delete body;
    body = newbody;
}

void SIPMessage::setParty(SIPParty* ep)
{
    if (ep == m_ep)
	return;
    if (m_ep)
	m_ep->deref();
    m_ep = ep;
    if (m_ep)
	m_ep->ref();
}

SIPDialog::SIPDialog()
{
}

SIPDialog::SIPDialog(const SIPDialog& original)
    : String(original),
      localURI(original.localURI), localTag(original.localTag),
      remoteURI(original.remoteURI), remoteTag(original.remoteTag)
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
    : String(message.getHeaderValue("Call-ID"))
{
    Regexp r("<\\([^>]\\+\\)>");
    bool local = message.isOutgoing() ^ message.isAnswer();
    const SIPHeaderLine* hl = message.getHeader(local ? "From" : "To");
    localURI = hl;
    if (localURI.matches(r))
        localURI = localURI.matchString(1);
    if (hl)
	localTag = hl->getParam("tag");
    hl = message.getHeader(local ? "To" : "From");
    remoteURI = hl;
    if (remoteURI.matches(r))
        remoteURI = remoteURI.matchString(1);
    if (hl)
	remoteTag = hl->getParam("tag");
    DDebug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
}

SIPDialog& SIPDialog::operator=(const SIPMessage& message)
{
    const char* cid = message.getHeaderValue("Call-ID");
    if (cid)
	String::operator=(cid);
    Regexp r("<\\([^>]\\+\\)>");
    bool local = message.isOutgoing() ^ message.isAnswer();
    const SIPHeaderLine* hl = message.getHeader(local ? "From" : "To");
    localURI = hl;
    if (localURI.matches(r))
        localURI = localURI.matchString(1);
    if (hl)
	localTag = hl->getParam("tag");
    hl = message.getHeader(local ? "To" : "From");
    remoteURI = hl;
    if (remoteURI.matches(r))
        remoteURI = remoteURI.matchString(1);
    if (hl)
	remoteTag = hl->getParam("tag");
    DDebug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
    return *this;
}

bool SIPDialog::operator==(const SIPDialog& other) const
{
    return
	String::operator==(other) &&
	localURI == other.localURI &&
	localTag == other.localTag &&
	remoteURI == other.remoteURI &&
	remoteTag == other.remoteTag;
}

bool SIPDialog::operator!=(const SIPDialog& other) const
{
    return !operator==(other);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
