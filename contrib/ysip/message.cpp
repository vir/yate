/**
 * message.cpp
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
#include <util.h>

using namespace TelEngine;

HeaderLine::HeaderLine(const char *name, const String& value)
    : NamedString(name)
{
    DDebug(DebugAll,"HeaderLine::HeaderLine('%s','%s') [%p]",name,value.c_str(),this);
    if (value.null())
	return;
    int sp = value.find(';');
    // skip past URIs with parameters
    int lim = value.find('<');
    if ((sp >= 0) && (lim >= 0) && (lim < sp)) {
	lim = value.find('>');
	sp = value.find(';',lim);
    }
    if (sp < 0) {
	assign(value);
	return;
    }
    assign(value,sp);
    trimBlanks();
    while (sp < (int)value.length()) {
	int ep = value.find(';',sp+1);
	if (ep <= sp)
	    ep = value.length();
	int eq = value.find('=',sp+1);
	if ((eq > 0) && (eq < ep)) {
	    String pname(value.substr(sp+1,eq-sp-1));
	    String pvalue(value.substr(eq+1,ep-eq-1));
	    pname.trimBlanks();
	    pvalue.trimBlanks();
	    if (!pname.null()) {
		DDebug(DebugAll,"param name='%s' value='%s'",pname.c_str(),pvalue.c_str());
		m_params.append(new NamedString(pname,pvalue));
	    }
	}
	else {
	    String pname(value.substr(sp+1,ep-sp-1));
	    pname.trimBlanks();
	    if (!pname.null()) {
		DDebug(DebugAll,"param name='%s' (no value)",pname.c_str());
		m_params.append(new NamedString(pname));
	    }
	}
	sp = ep;
    }
}

HeaderLine::HeaderLine(const HeaderLine& original)
    : NamedString(original.name(),original)
{
    DDebug(DebugAll,"HeaderLine::HeaderLine(%p '%s') [%p]",&original,name().c_str(),this);
    const ObjList* l = &original.params();
    for (; l; l = l->next()) {
	const NamedString* t = static_cast<const NamedString*>(l->get());
	if (t)
	    m_params.append(new NamedString(t->name(),*t));
    }
}

HeaderLine::~HeaderLine()
{
    DDebug(DebugAll,"HeaderLine::~HeaderLine() [%p]",this);
}

const NamedString* HeaderLine::getParam(const char *name) const
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

void HeaderLine::setParam(const char *name, const char *value)
{
    ObjList* p = m_params.find(name);
    if (p)
	*static_cast<NamedString*>(p->get()) = value;
    else
	m_params.append(new NamedString(name,value));
}

void HeaderLine::delParam(const char *name)
{
    ObjList* p = m_params.find(name);
    if (p)
	p->remove();
}

SIPMessage::SIPMessage(const char* _method, const char* _uri, const char* _version)
    : version(_version), method(_method), uri(_uri),
      body(0), m_ep(0), m_valid(true),
      m_answer(false), m_outgoing(true), m_ack(false), m_cseq(-1)
{
    Debug(DebugAll,"SIPMessage::SIPMessage('%s','%s','%s') [%p]",
	_method,_uri,_version,this);
}

SIPMessage::SIPMessage(SIPParty* ep, const char *buf, int len)
    : body(0), m_ep(ep), m_valid(false), m_answer(false), m_outgoing(false), m_ack(false), m_cseq(-1)
{
    Debugger debug(DebugAll,"SIPMessage::SIPMessage","(%p,%d) [%p]\n%s",
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
    : code(_code), reason(_reason), body(0),
      m_ep(0), m_valid(false),
      m_answer(true), m_outgoing(true), m_ack(false), m_cseq(-1)
{
    Debug(DebugAll,"SIPMessage::SIPMessage(%p,%d,'%s') [%p]",
	message,_code,_reason,this);
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
      body(), m_ep(0), m_valid(false),
      m_answer(false), m_outgoing(true), m_ack(true), m_cseq(-1)
{
    Debug(DebugAll,"SIPMessage::SIPMessage(%p,%d) [%p]",message,newtran,this);
    if (!(message && message->isValid()))
	return;
    m_ep = message->getParty();
    if (m_ep)
	m_ep->ref();
    version = message->version;
    uri = message->uri;
    copyAllHeaders(message,"Via");
    HeaderLine* hl = const_cast<HeaderLine*>(getLastHeader("Via"));
    if (!hl) {
	String tmp;
	tmp << version << "/" << getParty()->getProtoName();
	tmp << " " << getParty()->getLocalAddr() << ":" << getParty()->getLocalPort();
	hl = new HeaderLine("Via",tmp);
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
    Debug(DebugAll,"SIPMessage::~SIPMessage() [%p]",this);
    m_valid = false;
    setParty();
    setBody();
}

void SIPMessage::complete(SIPEngine* engine, const char* user, const char* domain, const char* dlgTag)
{
    Debug("SIPMessage",DebugAll,"complete(%p,'%s','%s','%s')%s%s%s [%p]",
	engine,user,domain,dlgTag,
	isACK() ? " ACK" : "",
	isOutgoing() ? " OUT" : "",
	isAnswer() ? " ANS" : "",
	this);
    if (!engine)
	return;

    if (isOutgoing() && !getParty())
	engine->buildParty(this);

    // don't complete ACK or incoming messages
    if (isACK() || !isOutgoing())
	return;

    if (!user)
	user = "anonymous";
    if (!domain)
	domain = getParty()->getLocalAddr();

    HeaderLine* hl = const_cast<HeaderLine*>(getLastHeader("Via"));
    if (!hl) {
	String tmp;
	tmp << version << "/" << getParty()->getProtoName();
	tmp << " " << getParty()->getLocalAddr() << ":" << getParty()->getLocalPort();
	hl = new HeaderLine("Via",tmp);
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

    hl = const_cast<HeaderLine*>(getHeader("From"));
    if (!hl) {
	String tmp;
	tmp << "<sip:" << user << "@" << domain << ">";
	hl = new HeaderLine("From",tmp);
	header.append(hl);
    }
    if (!(isAnswer() || hl->getParam("tag")))
	hl->setParam("tag",String((int)::random()));

    hl = const_cast<HeaderLine*>(getHeader("To"));
    if (!hl) {
	String tmp;
	tmp << "<" << uri << ">";
	hl = new HeaderLine("To",tmp);
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

    if (!(getHeader("User-Agent") || engine->getUserAgent().null()))
	addHeader("User-Agent",engine->getUserAgent());

    if (!getHeader("Allow"))
	addHeader("Allow",engine->getAllowed());
}

bool SIPMessage::copyHeader(const SIPMessage* message, const char* name)
{
    const HeaderLine* hl = message ? message->getHeader(name) : 0;
    if (hl) {
	header.append(new HeaderLine(*hl));
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
	const HeaderLine* hl = static_cast<const HeaderLine*>(l->get());
	if (hl && (hl->name() &= name)) {
	    ++c;
	    header.append(new HeaderLine(*hl));
	}
    }
    return c;
}

bool SIPMessage::parseFirst(String& line)
{
    DDebug("SIPMessage::parse",DebugAll,"firstline= '%s'",line.c_str());
    if (line.null())
	return false;
    Regexp r("^\\([Ss][Ii][Pp]/[0-9]\\.[0-9]\\+\\)[[:space:]]\\+\\([0-9][0-9][0-9]\\)[[:space:]]\\+\\(.*\\)$");
    if (line.matches(r)) {
	// Answer: <version> <code> <reason-phrase>
	m_answer = true;
	version = line.matchString(1).toUpper();
	code = line.matchString(2).toInteger();
	reason = line.matchString(3);
	Debug(DebugAll,"got answer version='%s' code=%d reason='%s'",
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
	    Debug(DebugAll,"got request method='%s' uri='%s' version='%s'",
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
    Debug(DebugAll,"SIPMessage::parse(%p,%d) [%p]",buf,len,this);
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
	*line >> ":";
	line->trimBlanks();
	DDebug("SIPMessage::parse",DebugAll,"header='%s' value='%s'",name.c_str(),line->c_str());
	header.append(new HeaderLine(uncompactForm(name.c_str()),*line));
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
    Debug("SIPMessage::parse",DebugAll,"%d header lines, body %p",
	header.count(),body);
    return true;
}

SIPMessage* SIPMessage::fromParsing(SIPParty* ep, const char *buf, int len)
{
    SIPMessage* msg = new SIPMessage(ep,buf,len);
    if (msg->isValid())
	return msg;
    Debug("SIPMessage",DebugWarn,"Invalid message");
    msg->destruct();
    return 0;
}

const HeaderLine* SIPMessage::getHeader(const char* name) const
{
    if (!(name && *name))
	return 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const HeaderLine* t = static_cast<const HeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    return t;
    }
    return 0;
}

const HeaderLine* SIPMessage::getLastHeader(const char* name) const
{
    if (!(name && *name))
	return 0;
    const HeaderLine* res = 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const HeaderLine* t = static_cast<const HeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    res = t;
    }
    return res;
}

int SIPMessage::countHeaders(const char* name) const
{
    if (!(name && *name))
	return 0;
    int res = 0;
    const ObjList* l = &header;
    for (; l; l = l->next()) {
	const HeaderLine* t = static_cast<const HeaderLine*>(l->get());
	if (t && (t->name() &= name))
	    ++res;
    }
    return res;
}

const NamedString* SIPMessage::getParam(const char* name, const char* param) const
{
    const HeaderLine* hl = getHeader(name);
    return hl ? hl->getParam(param) : 0;
}

const String& SIPMessage::getHeaderValue(const char* name) const
{
    const HeaderLine* hl = getHeader(name);
    return hl ? *hl : String::empty();
}

const String& SIPMessage::getParamValue(const char* name, const char* param) const
{
    const NamedString* ns = getParam(name,param);
    return ns ? *ns : String::empty();
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
	    HeaderLine* t = static_cast<HeaderLine*>(l->get());
	    if (t) {
		m_string << t->name() << ": " << t->c_str();
		const ObjList* p = &(t->params());
		for (; p; p = p->next()) {
		    NamedString* s = static_cast<NamedString*>(p->get());
		    if (s) {
			m_string << ";" << s->name();
			if (!s->null())
			    m_string << "=" << *s;
		    }
		}
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
    Debug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
}

SIPDialog& SIPDialog::operator=(const SIPDialog& original)
{
    String::operator=(original);
    localURI = original.localURI;
    localTag = original.localTag;
    remoteURI = original.remoteURI;
    remoteTag = original.remoteTag;
    Debug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
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
    Debug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
    return *this;
}

SIPDialog::SIPDialog(const SIPMessage& message)
    : String(message.getHeaderValue("Call-ID"))
{
    Regexp r("<\\([^>]\\+\\)>");
    bool local = message.isOutgoing() ^ message.isAnswer();
    const HeaderLine* hl = message.getHeader(local ? "From" : "To");
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
    Debug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
	c_str(),localURI.c_str(),localTag.c_str(),remoteURI.c_str(),remoteTag.c_str(),this);
}

SIPDialog& SIPDialog::operator=(const SIPMessage& message)
{
    String::operator=(message.getHeaderValue("Call-ID"));
    Regexp r("<\\([^>]\\+\\)>");
    bool local = message.isOutgoing() ^ message.isAnswer();
    const HeaderLine* hl = message.getHeader(local ? "From" : "To");
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
    Debug("SIPDialog",DebugAll,"callid '%s' local '%s;tag=%s' remote '%s;tag=%s' [%p]",
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
