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

SIPMessage::SIPMessage()
    : body(0), m_ep(0), m_valid(false), m_answer(false), m_outgoing(true)
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
    const ObjList* l = &message->header;
    for (; l; l = l->next()) {
	NamedString* t = static_cast<NamedString*>(l->get());
	if (t)
	    header.append(new NamedString(t->name(),*t));
    }
    body = message->body ? message->body->clone() : 0;
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

bool SIPMessage::parse(const char *buf, int len)
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
	header.append(new NamedString(name.c_str(),*line));
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

const String& SIPMessage::getHeaders() const
{
    if (isValid() && m_string.null()) {
	if (isAnswer())
	    m_string << version << " " << code << " " << reason << "\r\n";
	else
	    m_string << method << " " << uri << " " << version << "\r\n";

	const ObjList* l = &header;
	for (; l; l = l->next()) {
	    NamedString* t = static_cast<NamedString*>(l->get());
	    if (t)
		m_string << t->name() << ": " << t->c_str() << "\r\n";
	}

	m_string << "\r\n";
    }
    return m_string;
}

const DataBlock& SIPMessage::getBuffer() const
{
    if (isValid() && m_data.null()) {
	m_data.assign((void*)(getHeaders().c_str()),getHeaders().length());
	if (body)
	    m_data += body->getBody();
    }
    return m_data;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
