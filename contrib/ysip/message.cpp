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
    Debug(DebugAll,"HeaderLine::HeaderLine('%s','%s') [%p]",name,value.c_str(),this);
    if (value.null())
	return;
    int sp = value.find(';');
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
		Debug(DebugAll,"param name='%s' value='%s'",pname.c_str(),pvalue.c_str());
		m_params.append(new NamedString(pname,pvalue));
	    }
	}
	else {
	    String pname(value.substr(sp+1,ep-sp-1));
	    pname.trimBlanks();
	    if (!pname.null()) {
		Debug(DebugAll,"param name='%s' (no value)",pname.c_str());
		m_params.append(new NamedString(pname));
	    }
	}
	sp = ep+1;
    }
}

HeaderLine::HeaderLine(const HeaderLine& original)
    : NamedString(original.name(),original)
{
    Debug(DebugAll,"HeaderLine::HeaderLine(%p '%s') [%p]",&original,name().c_str(),this);
    const ObjList* l = &original.params();
    for (; l; l = l->next()) {
	const NamedString* t = static_cast<const NamedString*>(l->get());
	if (t)
	    m_params.append(new NamedString(t->name(),*t));
    }
}

HeaderLine::~HeaderLine()
{
    Debug(DebugAll,"HeaderLine::~HeaderLine() [%p]",this);
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

SIPMessage::SIPMessage(const char* _method, const char* _uri, const char* _version)
    : version(_version), method(_method), uri(_uri),
      body(0), m_ep(0), m_valid(true), m_answer(false), m_outgoing(true)
{
    Debug(DebugAll,"SIPMessage::SIPMessage() [%p]",this);
}

SIPMessage::SIPMessage(SIPParty* ep, const char *buf, int len)
    : body(0), m_ep(ep), m_valid(false), m_answer(false), m_outgoing(false)
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
      m_ep(0), m_valid(false), m_answer(true), m_outgoing(true)
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
    copyAllHeaders(message,"via");
    copyHeader(message,"to");
    copyHeader(message,"from");
    copyHeader(message,"cseq");
    copyHeader(message,"call-id");
#if 0
    body = message->body ? message->body->clone() : 0;
#endif
    m_valid = true;
}

SIPMessage::~SIPMessage()
{
    Debug(DebugAll,"SIPMessage::~SIPMessage() [%p]",this);
    m_valid = false;
    if (m_ep)
	m_ep->deref();
    m_ep = 0;
    if (body)
	delete body;
    body = 0;
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
	header.append(new HeaderLine(name.c_str(),*line));
	if (content.null() && (name &= "content-type")) {
	    content = *line;
	    content.toLower();
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

const NamedString* SIPMessage::getParam(const char* name, const char* param) const
{
    const HeaderLine* hl = getHeader(name);
    return hl ? hl->getParam(param) : 0;
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
		    if (s)
			m_string << ";" << s->name() << "=" << *s;
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
    }
    return m_data;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
