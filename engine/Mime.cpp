/**
 * Mime.cpp
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "yatemime.h"

using namespace TelEngine;

// Utility function, checks if a character is a folded line continuation
static bool isContinuationBlank(char c)
{
    return ((c == ' ') || (c == '\t'));
}

/**
 * MimeHeaderLine
 */
MimeHeaderLine::MimeHeaderLine(const char* name, const String& value, char sep)
    : NamedString(name), m_separator(sep ? sep : ';')
{
    if (value.null())
	return;
    XDebug(DebugAll,"MimeHeaderLine::MimeHeaderLine('%s','%s') [%p]",name,value.c_str(),this);
    int sp = findSep(value,m_separator);
    if (sp < 0) {
	assign(value);
	return;
    }
    assign(value,sp);
    trimBlanks();
    while (sp < (int)value.length()) {
	int ep = findSep(value,m_separator,sp+1);
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

MimeHeaderLine::MimeHeaderLine(const MimeHeaderLine& original, const char* newName)
    : NamedString(newName ? newName : original.name().c_str(),original),
      m_separator(original.separator())
{
    XDebug(DebugAll,"MimeHeaderLine::MimeHeaderLine(%p '%s') [%p]",&original,name().c_str(),this);
    const ObjList* l = &original.params();
    for (; l; l = l->next()) {
	const NamedString* t = static_cast<const NamedString*>(l->get());
	if (t)
	    m_params.append(new NamedString(t->name(),*t));
    }
}

MimeHeaderLine::~MimeHeaderLine()
{
    XDebug(DebugAll,"MimeHeaderLine::~MimeHeaderLine() [%p]",this);
}

void* MimeHeaderLine::getObject(const String& name) const
{
    if (name == "MimeHeaderLine")
	return const_cast<MimeHeaderLine*>(this);
    return NamedString::getObject(name);
}

MimeHeaderLine* MimeHeaderLine::clone(const char* newName) const
{
    return new MimeHeaderLine(*this,newName);
}

void MimeHeaderLine::buildLine(String& line) const
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

const NamedString* MimeHeaderLine::getParam(const char* name) const
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

void MimeHeaderLine::setParam(const char* name, const char* value)
{
    ObjList* p = m_params.find(name);
    if (p)
	*static_cast<NamedString*>(p->get()) = value;
    else
	m_params.append(new NamedString(name,value));
}

void MimeHeaderLine::delParam(const char* name)
{
    ObjList* p = m_params.find(name);
    if (p)
	p->remove();
}

// Utility function, puts quotes around a string
void MimeHeaderLine::addQuotes(String& str)
{
    str.trimBlanks();
    int l = str.length();
    if ((l < 2) || (str[0] != '"') || (str[l-1] != '"'))
	str = "\"" + str + "\"";
}

// Utility function, removes quotes around a string
void MimeHeaderLine::delQuotes(String& str)
{
    str.trimBlanks();
    int l = str.length();
    if ((l >= 2) && (str[0] == '"') && (str[l-1] == '"')) {
	str = str.substr(1,l-2);
	str.trimBlanks();
    }
}

// Utility function, puts quotes around a string
String MimeHeaderLine::quote(const String& str)
{
    String tmp(str);
    addQuotes(tmp);
    return tmp;
}

// Utility function to find a separator not in "quotes" or inside <uri>
int MimeHeaderLine::findSep(const char* str, char sep, int offs)
{
    if (!(str && sep))
	return -1;
    str += offs;
    bool inQ = false;
    bool inU = false;
    char c;
    for (; (c = *str++) ; offs++) {
	if (inQ) {
	    if (c == '"')
		inQ = false;
	    continue;
	}
	if (inU) {
	    if (c == '>')
		inU = false;
	    continue;
	}
	if (c == sep)
	    return offs;
	switch (c) {
	    case '"':
		inQ = true;
		break;
	    case '<':
		inU = true;
		break;
	}
    }
    return -1;
}


/**
 * MimeAuthLine
 */
MimeAuthLine::MimeAuthLine(const char* name, const String& value)
    : MimeHeaderLine(name,String::empty(),',')
{
    XDebug(DebugAll,"MimeAuthLine::MimeAuthLine('%s','%s') [%p]",name,value.c_str(),this);
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

MimeAuthLine::MimeAuthLine(const MimeAuthLine& original, const char* newName)
    : MimeHeaderLine(original,newName)
{
}

void* MimeAuthLine::getObject(const String& name) const
{
    if (name == "MimeAuthLine")
	return const_cast<MimeAuthLine*>(this);
    return MimeHeaderLine::getObject(name);
}

MimeHeaderLine* MimeAuthLine::clone(const char* newName) const
{
    return new MimeAuthLine(*this,newName);
}

void MimeAuthLine::buildLine(String& line) const
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


/**
 * MimeBody
 */
YCLASSIMP(MimeBody,GenObject)

MimeBody::MimeBody(const String& type)
    : m_type("Content-Type",type)
{
    DDebug(DebugAll,"MimeBody::MimeBody('%s') [%p]",m_type.c_str(),this);
}

MimeBody::~MimeBody()
{
    DDebug(DebugAll,"MimeBody::~MimeBody() '%s' [%p]",m_type.c_str(),this);
}

const DataBlock& MimeBody::getBody() const
{
    if (m_body.null())
	buildBody();
    return m_body;
}

MimeBody* MimeBody::build(const char* buf, int len, const String& type)
{
    DDebug(DebugAll,"MimeBody::build(%p,%d,'%s')",buf,len,type.c_str());
    if ((len <= 0) || !buf)
	return 0;
    if (type == "application/sdp")
	return new MimeSdpBody(type,buf,len);
    if (type == "application/dtmf-relay")
	return new MimeLinesBody(type,buf,len);
    if (type.startsWith("text/") || (type == "application/dtmf"))
	return new MimeStringBody(type,buf,len);
    return new MimeBinaryBody(type,buf,len);
}

String* MimeBody::getUnfoldedLine(const char*& buf, int& len)
{
    String* res = new String;
    const char* b = buf;
    const char* s = b;
    int l = len;
    int e = 0;
    for (;(l > 0); ++b, --l) {
	bool goOut = false;
	switch (*b) {
	    case '\r':
		// CR is optional but skip over it if exists
		if ((l > 0) && (b[1] == '\n')) {
		    ++b;
		    --l;
		}
	    case '\n':
		++b;
		--l;
		{
		    String line(s,e);
		    *res << line;
		}
		// Skip over any continuation characters at start of next line
		goOut = true;
		while ((l > 0) && isContinuationBlank(b[0])) {
		    ++b;
		    --l;
		    goOut = false;
		}
		s = b;
		e = 0;
		if (!goOut) {
		    --b;
		    ++l;
		}
		break;
	    case '\0':
		// Should not happen - but let's accept what we got
		Debug(DebugMild,"Unexpected NUL character while unfolding lines");
		*res << s;
		goOut = true;
		// End parsing
		b += l;
		l = 0;
		e = 0;
		break;
	    default:
		// Just count this character - we'll pick it later
		++e;
	}
	// Exit without adjusting p and l
	if (goOut)
	    break;
    }
    buf = b;
    len = l;
    // Collect any leftover characters
    if (e) {
	String line(s,e);
	*res << line;
    }
    res->trimBlanks();
    return res;
}


YCLASSIMP(MimeSdpBody,MimeBody)

MimeSdpBody::MimeSdpBody()
    : MimeBody("application/sdp")
{
}

MimeSdpBody::MimeSdpBody(const String& type, const char* buf, int len)
    : MimeBody(type)
{
    while (len > 0) {
        String* line = getUnfoldedLine(buf,len);
	int eq = line->find('=');
	if (eq > 0)
	    m_lines.append(new NamedString(line->substr(0,eq),line->substr(eq+1)));
	line->destruct();
    }
}

MimeSdpBody::MimeSdpBody(const MimeSdpBody& original)
    : MimeBody(original.getType())
{
    const ObjList* l = &original.m_lines;
    for (; l; l = l->next()) {
    	const NamedString* t = static_cast<NamedString*>(l->get());
        if (t)
	    m_lines.append(new NamedString(t->name(),*t));
    }
}

MimeSdpBody::~MimeSdpBody()
{
}

void MimeSdpBody::buildBody() const
{
    DDebug(DebugAll,"MimeSdpBody::buildBody() [%p]",this);
    const ObjList* l = &m_lines;
    for (; l; l = l->next()) {
    	const NamedString* t = static_cast<NamedString*>(l->get());
        if (t) {
	    String line;
	    line << t->name() << "=" << *t << "\r\n";
	    m_body += line;
	}
    }
}

MimeBody* MimeSdpBody::clone() const
{
    return new MimeSdpBody(*this);
}

const NamedString* MimeSdpBody::getLine(const char* name) const
{
    if (!(name && *name))
	return 0;
    const ObjList* l = &m_lines;
    for (; l; l = l->next()) {
    	const NamedString* t = static_cast<NamedString*>(l->get());
        if (t && (t->name() &= name))
	    return t;
    }
    return 0;
}

const NamedString* MimeSdpBody::getNextLine(const NamedString* line) const
{
    if (!line)
	return 0;
    const ObjList* l = m_lines.find(line);
    if (!l)
	return 0;
    l = l->next();
    for (; l; l = l->next()) {
    	const NamedString* t = static_cast<NamedString*>(l->get());
        if (t && (t->name() &= line->name()))
	    return t;
    }
    return 0;
}


YCLASSIMP(MimeBinaryBody,MimeBody)

MimeBinaryBody::MimeBinaryBody(const String& type, const char* buf, int len)
    : MimeBody(type)
{
    m_body.assign((void*)buf,len);
}

MimeBinaryBody::MimeBinaryBody(const MimeBinaryBody& original)
    : MimeBody(original.getType())
{
    m_body = original.m_body;
}

MimeBinaryBody::~MimeBinaryBody()
{
}

void MimeBinaryBody::buildBody() const
{
    DDebug(DebugAll,"MimeBinaryBody::buildBody() [%p]",this);
    // nothing to do
}

MimeBody* MimeBinaryBody::clone() const
{
    return new MimeBinaryBody(*this);
}


YCLASSIMP(MimeStringBody,MimeBody)

MimeStringBody::MimeStringBody(const String& type, const char* buf, int len)
    : MimeBody(type), m_text(buf,len)
{
}

MimeStringBody::MimeStringBody(const MimeStringBody& original)
    : MimeBody(original.getType()), m_text(original.m_text)
{
}

MimeStringBody::~MimeStringBody()
{
}

void MimeStringBody::buildBody() const
{
    DDebug(DebugAll,"MimeStringBody::buildBody() [%p]",this);
    m_body.assign((void*)m_text.c_str(),m_text.length());
}

MimeBody* MimeStringBody::clone() const
{
    return new MimeStringBody(*this);
}


YCLASSIMP(MimeLinesBody,MimeBody)

MimeLinesBody::MimeLinesBody(const String& type, const char* buf, int len)
    : MimeBody(type)
{
    while (len > 0)
        m_lines.append(getUnfoldedLine(buf,len));
}

MimeLinesBody::MimeLinesBody(const MimeLinesBody& original)
    : MimeBody(original.getType())
{
    const ObjList* l = &original.m_lines;
    for (; l; l = l->next()) {
    	const String* s = static_cast<String*>(l->get());
        if (s)
	    m_lines.append(new String(*s));
    }
}

MimeLinesBody::~MimeLinesBody()
{
}

void MimeLinesBody::buildBody() const
{
    DDebug(DebugAll,"MimeLinesBody::buildBody() [%p]",this);
    const ObjList* l = &m_lines;
    for (; l; l = l->next()) {
    	const String* s = static_cast<String*>(l->get());
        if (s) {
	    String line;
	    line << *s << "\r\n";
	    m_body += line;
	}
    }
}

MimeBody* MimeLinesBody::clone() const
{
    return new MimeLinesBody(*this);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
