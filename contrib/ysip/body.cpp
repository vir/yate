/**
 * body.cpp
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

SIPBody::SIPBody(const String& type)
    : m_type(type)
{
    Debug(DebugAll,"SIPBody::SIPBody('%s') [%p]",m_type.c_str(),this);
}

SIPBody::~SIPBody()
{
    Debug(DebugAll,"SIPBody::~SIPBody() '%s' [%p]",m_type.c_str(),this);
}

const DataBlock& SIPBody::getBody() const
{
    if (m_body.null())
	buildBody();
    return m_body;
}

SIPBody* SIPBody::build(const char *buf, int len, const String& type)
{
    Debug(DebugAll,"SIPBody::build(%p,%d,'%s')",buf,len,type.c_str());
    if ((len <= 0) || !buf)
	return 0;
    if (type == "application/sdp")
	return new SDPBody(type,buf,len);
    if (type.startsWith("text/"))
	return new StringBody(type,buf,len);
    return new BinaryBody(type,buf,len);
}

SDPBody::SDPBody(const String& type, const char *buf, int len)
    : SIPBody(type)
{
    while (len > 0)
        m_lines.append(getUnfoldedLine(&buf,&len));
}

SDPBody::SDPBody(const SDPBody& original)
    : SIPBody(original.getType())
{
    const ObjList* l = &original.m_lines;
    for (; l; l = l->next()) {
    	String* t = static_cast<String*>(l->get());
        if (t)
	    m_lines.append(new String(*t));
    }
}

SDPBody::~SDPBody()
{
}

void SDPBody::buildBody() const
{
    Debug(DebugAll,"SDPBody::buildBody() [%p]",this);
    DataBlock EOL((void*)"\r\n",2);
    const ObjList* l = &m_lines;
    for (; l; l = l->next()) {
    	String* t = static_cast<String*>(l->get());
        if (t) {
	    DataBlock line((void*)t->c_str(),t->length());
	    m_body += line;
	    m_body += EOL;
	}
    }
}

SIPBody* SDPBody::clone() const
{
    return new SDPBody(*this);
}

BinaryBody::BinaryBody(const String& type, const char *buf, int len)
    : SIPBody(type)
{
    m_body.assign((void*)buf,len);
}

BinaryBody::BinaryBody(const BinaryBody& original)
    : SIPBody(original.getType())
{
    m_body = original.m_body;
}

BinaryBody::~BinaryBody()
{
}

void BinaryBody::buildBody() const
{
    Debug(DebugAll,"BinaryBody::buildBody() [%p]",this);
    // nothing to do
}

SIPBody* BinaryBody::clone() const
{
    return new BinaryBody(*this);
}

StringBody::StringBody(const String& type, const char *buf, int len)
    : SIPBody(type), m_text(buf,len)
{
}

StringBody::StringBody(const StringBody& original)
    : SIPBody(original.getType()), m_text(original.m_text)
{
}

StringBody::~StringBody()
{
}

void StringBody::buildBody() const
{
    Debug(DebugAll,"StringBody::buildBody() [%p]",this);
    m_body.assign((void*)m_text.c_str(),m_text.length());
}

SIPBody* StringBody::clone() const
{
    return new StringBody(*this);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
