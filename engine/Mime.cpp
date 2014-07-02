/**
 * Mime.cpp
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

#include <stdlib.h>
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
    if (name == YATOM("MimeHeaderLine"))
	return const_cast<MimeHeaderLine*>(this);
    return NamedString::getObject(name);
}

MimeHeaderLine* MimeHeaderLine::clone(const char* newName) const
{
    return new MimeHeaderLine(*this,newName);
}

void MimeHeaderLine::buildLine(String& line, bool header) const
{
    if (header)
	line << name() << ": ";
    line << *this;
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
void MimeHeaderLine::addQuotes(String& str, bool force)
{
    str.trimBlanks();
    unsigned int l = str.length();
    if (force || (l < 2) || (str[0] != '"') || (str[l-1] != '"')) {
	str = "\"" + str + "\"";
	force = true;
    }
    for (l = 1; l < str.length() - 1; l++) {
	switch (str.at(l)) {
	    case '\\':
		if (!force) {
		    // check only, don't quote again
		    switch (str.at(l+1)) {
			case '\\':
			case '"':
			    // already quoted, skip it
			    l++;
			    continue;
		    }
		}
		// fall through
	    case '"':
		str = str.substr(0,l) + "\\" + str.substr(l);
		l++;
	    default:
		break;
	}
    }
}

// Utility function, removes quotes around a string
void MimeHeaderLine::delQuotes(String& str, bool force)
{
    str.trimBlanks();
    unsigned int l = str.length();
    if ((l >= 2) && (str[0] == '"') && (str[l-1] == '"')) {
	str = str.substr(1,l-2);
	str.trimBlanks();
	force = true;
    }
    if (force) {
	for (l = 0; l < str.length(); l++) {
	    if (str.at(l) == '\\')
		str = str.substr(0,l) + str.substr(l+1);
	    // since the string is shorter the loop will skip past next char
	}
    }
}

// Utility function, puts quotes around a string
String MimeHeaderLine::quote(const String& str, bool force)
{
    String tmp(str);
    addQuotes(tmp,force);
    return tmp;
}

// Utility function, removed quotes around a string
String MimeHeaderLine::unquote(const String& str, bool force)
{
    String tmp(str);
    delQuotes(tmp,force);
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

// Build a string from a list of MIME header lines
void MimeHeaderLine::buildHeaders(String& buf, const ObjList& headers)
{
    for (ObjList* o = headers.skipNull(); o; o = o->skipNext()) {
	MimeHeaderLine* hdr = static_cast<MimeHeaderLine*>(o->get());
	String line;
	hdr->buildLine(line);
	buf << line << "\r\n";
    }
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
    if (name == YATOM("MimeAuthLine"))
	return const_cast<MimeAuthLine*>(this);
    return MimeHeaderLine::getObject(name);
}

MimeHeaderLine* MimeAuthLine::clone(const char* newName) const
{
    return new MimeAuthLine(*this,newName);
}

void MimeAuthLine::buildLine(String& line, bool header) const
{
    if (header)
	line << name() << ": ";
    line << *this;
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
    m_type.toLower();
    DDebug(DebugAll,"MimeBody::MimeBody('%s') [%p]",m_type.c_str(),this);
}

// Build from header line
// Make sure the name of the header line is correct
MimeBody::MimeBody(const MimeHeaderLine& type)
    : m_type(type,"Content-Type")
{
    m_type.toLower();
    DDebug(DebugAll,"MimeBody::MimeBody('%s','%s') [%p]",
	m_type.name().c_str(),m_type.c_str(),this);
}

MimeBody::~MimeBody()
{
    DDebug(DebugAll,"MimeBody::~MimeBody() '%s' [%p]",m_type.c_str(),this);
}

// Find the first (sub)body matching the given type
MimeBody* MimeBody::getFirst(const String& type) const
{
    if (type.null())
	return 0;
    if (getType() == type)
	return const_cast<MimeBody*>(this);
    return isMultipart() ? (static_cast<const MimeMultipartBody*>(this))->findBody(type) : 0;
}

// Find an additional header line by its name
MimeHeaderLine* MimeBody::findHdr(const String& name, const MimeHeaderLine* start) const
{
    ObjList* o = m_headers.skipNull();
    if (!o)
	return 0;
    // Find start point
    if (start)
	for (; o; o = o->skipNext())
	    if (start == o->get()) {
		o = o->skipNext();
		break;
	    }
    // Find the header
    for (; o; o = o->skipNext()) {
	MimeHeaderLine* hdr = static_cast<MimeHeaderLine*>(o->get());
	if (hdr->name() &= name)
	    return hdr;
    }
    return 0;
}

// Replace the value of an existing parameter or add a new one
bool MimeBody::setParam(const char* name, const char* value, const char* header)
{
    MimeHeaderLine* hdr = (!(header && *header)) ? &m_type : findHdr(header);
    if (hdr)
	hdr->setParam(name,value);
    return (hdr != 0);
}

// Remove a header parameter
bool MimeBody::delParam(const char* name, const char* header)
{
    MimeHeaderLine* hdr = (!(header && *header)) ? &m_type : findHdr(header);
    if (hdr)
	hdr->delParam(name);
    return (hdr != 0);
}

// Get a header parameter
const NamedString* MimeBody::getParam(const char* name, const char* header) const
{
    const MimeHeaderLine* hdr = (!(header && *header)) ? &m_type : findHdr(header);
    return hdr ? hdr->getParam(name) : 0;
}

const DataBlock& MimeBody::getBody() const
{
    if (m_body.null())
	buildBody();
    return m_body;
}

// Method to build a MIME body from a type and data buffer
MimeBody* MimeBody::build(const char* buf, int len, const MimeHeaderLine& type)
{
    DDebug(DebugAll,"MimeBody::build(%p,%d,'%s')",buf,len,type.c_str());
    if ((len <= 0) || !buf)
	return 0;
    String what = type;
    what.toLower();
    if (what == YSTRING("application/sdp"))
	return new MimeSdpBody(type,buf,len);
    if ((what == YSTRING("application/dtmf-relay")) || (what == YSTRING("message/sipfrag")))
	return new MimeLinesBody(type,buf,len);
    if (what.startsWith("text/") || (what == YSTRING("application/dtmf")))
	return new MimeStringBody(type,buf,len);
    if (what.startsWith("multipart/"))
	return new MimeMultipartBody(type,buf,len);
    // Remove any spurious leading CRLF
    if (len >= 2 && buf[0] == '\r' && buf[1] == '\n') {
	len -= 2;
	if (!len)
	    return 0;
	buf += 2;
    }
    if ((what.length() >=7) && what.endsWith("+xml"))
	return new MimeStringBody(type,buf,len);
    // Create a default binary body
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
		while ((l > 0) && *res && isContinuationBlank(b[0])) {
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
		*res << s;
		goOut = true;
		if (l <= 16) {
		    // If there are maximum 16 NULs suppress the warning
		    e = l;
		    do {
			++b;
			--l;
		    } while (l && !*b);
		}
		if (l)
		    Debug(DebugMild,"Unexpected NUL character while unfolding lines");
#ifdef DEBUG
		else
		    Debug(DebugInfo,"Dropped %d final NUL characters while unfolding lines",e);
#endif
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


/**
 * MimeMultipartBody
 */
// Constructor to build an empty multipart body
MimeMultipartBody::MimeMultipartBody(const char* subtype, const char* boundary)
    : MimeBody((subtype && *subtype) ? (String("multipart/") + subtype) : "multipart/mixed")
{
    String b = boundary;
    b.trimBlanks();
    if (b.null())
	b << (int)Random::random() << "_" << (unsigned int)Time::now();
    if (b.length() > 70)
	b = b.substr(0,70);
    setParam("boundary",b);
}

// Constructor from block of data
MimeMultipartBody::MimeMultipartBody(const String& type, const char* buf, int len)
    : MimeBody(type)
{
    parse(buf,len);
}

// Constructor from block of data
MimeMultipartBody::MimeMultipartBody(const MimeHeaderLine& type, const char* buf, int len)
    : MimeBody(type)
{
    parse(buf,len);
}

MimeMultipartBody::~MimeMultipartBody()
{
}

// Copy constructor
MimeMultipartBody::MimeMultipartBody(const MimeMultipartBody& original)
    : MimeBody(original.getType())
{
    for (ObjList* o = original.m_bodies.skipNull(); o; o = o->skipNext()) {
	MimeBody* body = static_cast<MimeBody*>(o->get());
	m_bodies.append(body->clone());
    }
}

// Find object by class name, descend into parts
void* MimeMultipartBody::getObject(const String& name) const
{
    if (name == YATOM("MimeMultipartBody"))
	return const_cast<MimeMultipartBody*>(this);
    void* res = MimeBody::getObject(name);
    if (res)
	return res;
    for (ObjList* o = m_bodies.skipNull(); o; o = o->skipNext()) {
	const MimeBody* body = static_cast<const MimeBody*>(o->get());
	res = body->getObject(name);
	if (res)
	    return res;
    }
    return 0;
}

// Find a body. Enclosed multiparts are also searched for the requested body
MimeBody* MimeMultipartBody::findBody(const String& content, MimeBody** start) const
{
    MimeBody* localStart = start ? *start : 0;
    MimeBody* body = 0;
    XDebug(DebugAll,"MimeMultipartBody::findBody(%s,%p) [%p]",
	content.c_str(),localStart,this);
    for (ObjList* o = m_bodies.skipNull(); !body && o; o = o->skipNext()) {
	body = static_cast<MimeBody*>(o->get());
	if (!localStart) {
	    if (content == body->getType())
		break;
	}
	else if (body == localStart)
	    localStart = 0;
	// Check inside multiparts for starting point or requested body
	if (body->isMultipart())
	    body = (static_cast<MimeMultipartBody*>(body))->findBody(content,&localStart);
	else
	    body = 0;
    }
    XDebug(DebugInfo,"MimeMultipartBody::findBody() body=%p start=%p [%p]",
	body,localStart,this);
    if (start)
	*start = localStart;
    return body;
}

// Duplicate this MIME body
MimeBody* MimeMultipartBody::clone() const
{
    return new MimeMultipartBody(*this);
}

// Method that is called internally to build the binary encoded body
void MimeMultipartBody::buildBody() const
{
    String boundary;
    if (!getBoundary(boundary))
	return;

    String crlf = "\r\n";
    String boundaryLast = boundary + "--" + crlf;
    boundary << crlf;

    // Build this body. Add a boundary before each component
    ObjList* o = m_bodies.skipNull();
    if (o)
	for (; o; o = o->skipNext()) {
	    MimeBody* body = static_cast<MimeBody*>(o->get());
	    String hdr;
	    body->buildHeaders(hdr);
	    m_body += boundary;
	    m_body += hdr;
	    m_body += crlf;
	    m_body += body->getBody();
	}
    else
	m_body += boundary;

    // Add termination boundary
    m_body += boundaryLast;
}

// Skip chars until boundary line ends
// Check for last boundary
static void finalizeBoundary(const char*& buf, int& len, bool& last, const char* boundary)
{
    last = (len >= 2 && buf[0] == '-' && buf[1] == '-');
    if (last) {
	buf += 2;
	len -= 2;
	// Ignore the rest, we don't need the epilogue
	return;
    }
    // RFC 2046 states the boundary line should be padded with spaces only
    // and end with CR/LF. We allow any char until LF
    for (; len && *buf != '\n'; buf++, len--)
	;
    if (len) {
	buf++;
	len--;
    }
    if (!len) {
	Debug(DebugNote,"Unexpected multipart end for boundary '%s'",boundary + 4);
	last = true;
    }
}

// Parse a data buffer and append any valid body to this multipart
// Ignore prolog, epilog and invalid bodies
void MimeMultipartBody::parse(const char* buf, int len)
{
    DDebug(DebugAll,"MimeMultipartBody::parse(%p,%d,'%s') [%p]",
	buf,len,getType().c_str(),this);

    String boundary;
    if (!(buf && len > 0 && getBoundary(boundary)))
	return;

    // Find first boundary: ignore the data before it
    bool endBody = false;
    bool foundFirst = false;
    // RFC 2046: First boundary may be preceded by a preamble
    // If there is a preamble, CR/LF MUST be present in boundary
    // If there is no preamble CR/LF may not be present, the buffer may start with -
    if (buf[0] == '-') {
	const char* tmp = boundary.c_str() + 2;
	int tmpLen = boundary.length() - 2;
	if (tmpLen <= len) {
	    int i = 0;
	    for (; i < tmpLen; i++)
		if (buf[i] != tmp[i])
		    break;
	    if (i == tmpLen) {
		foundFirst = true;
		buf += tmpLen;
		len -= tmpLen;
		finalizeBoundary(buf,len,endBody,boundary);
	    }
	}
    }
    if (!foundFirst)
	findBoundary(buf,len,boundary,boundary.length(),endBody);

    // Parse for bodies
    XDebug(DebugInfo,"Start parsing boundary=%s len=%d [%p]",
	boundary.c_str() + 4,len,this);
    while (len > 0) {
	if (endBody)
	    break;
	// Find next boundary. Get the length of data before it
	// 'start' will point to the beginning of an enclosed body
	const char* start = buf;
	int l = findBoundary(buf,len,boundary.c_str(),boundary.length(),endBody);
	XDebug(DebugInfo,"Found %d length body (remain=%d) [%p]",l,len,this);
	if (l <= 0)
	    continue;
	// Parse 'start' for body headers
	ObjList hdr;
	MimeHeaderLine* cType = 0;
	while (l) {
	    int tmpLen = l;
	    const char* s = start;
	    String* line = MimeBody::getUnfoldedLine(start,l);
	    // Found end of headers
	    if (line->null()) {
		l = tmpLen;
		start = s;
		TelEngine::destruct(line);
		break;
	    }
	    DDebug(DebugAll,"Found line '%s' [%p]",line->c_str(),this);
	    int col = line->find(':');
	    // Check if this is a valid header line
	    if (col <= 0) {
		TelEngine::destruct(line);
		continue;
	    }
	    String name = line->substr(0,col);
	    name.trimBlanks();
	    if (!name) {
		TelEngine::destruct(line);
		continue;
	    }
	    *line >> ":";
	    line->trimBlanks();
	    MimeHeaderLine* ct = new MimeHeaderLine(name,*line);
	    hdr.append(ct);
	    if (name &= "Content-Type")
		cType = ct;
	    TelEngine::destruct(line);
	}
	// 'start' is now pointing to the enclosed body's content
	// Append body to list and move extra headers to it
	MimeBody* body = cType ? MimeBody::build(start,l,*cType) : 0;
	if (!body) {
	    DDebug(DebugNote,
		"Failed to build enclosed body (length=%d)%s [%p]",
		l,cType?"":": Content-Type header is missing",this);
	    continue;
	}
	m_bodies.append(body);
	ListIterator iter(hdr);
	for (GenObject* o = 0; (o = iter.get());) {
	    MimeHeaderLine* line = static_cast<MimeHeaderLine*>(o);
	    if (line == cType)
		continue;
	    hdr.remove(o,false);
	    body->appendHdr(line);
	}
	XDebug(DebugInfo,"Added ('%s',%p) with %u additional headers [%p]",
	    cType->c_str(),body,body->headers().count(),this);
    }
    XDebug(DebugInfo,"End parsing boundary=%s%s [%p]",boundary.c_str() + 4,
	(endBody && len > 0)?" .Found garbage after body":"",this);
}

// Parse input buffer for first body boundary or data end
// Advance buffer pass the boundary line and decrease the buffer length
// Set endData to true if a final boundary was found or the end of the
//  buffer was reached
// Return the length of data before the found boundary
int MimeMultipartBody::findBoundary(const char*& buf, int& len,
	const char* boundary, unsigned int bLen, bool& endBody)
{
    if (len <= 0) {
	endBody = true;
	return 0;
    }

    endBody = false;
    int bodyLen = 0;
    bool found = false;

    while (len) {
	// Skip until the first char of boundary
	for (; len >= (int)bLen && *buf != boundary[0]; len--, buf++)
	    bodyLen++;
	// Current char is the first char of the boundary
	// Check if we have enough data for boundary
	if (len < (int)bLen) {
	    bodyLen += len;
	    buf += len;
	    len = 0;
	    break;
	}
	// Check boundary
	unsigned int n = 0;
	for(; n < bLen && *buf == boundary[n]; n++, buf++, len--)
	    ;
	// Not found
	if (n < bLen) {
	    bodyLen += n;
	    continue;
	}
	found = true;
	finalizeBoundary(buf,len,endBody,boundary);
	break;
    }

    if (!found)
	Debug(DebugNote,"Expected multipart boundary '%s' not found",boundary + 4);
    if (!len)
	endBody = true;
    return found ? bodyLen : 0;
}

// Build a boundary string to be used when parsing or building body
// Remove quotes if present. Trim blanks
// Insert CRLF and boundary marks ('--') before parameter
bool MimeMultipartBody::getBoundary(String& boundary) const
{
    boundary = "";
    const NamedString* b = getParam("boundary");
    if (b) {
	String tmp = *b;
	MimeHeaderLine::delQuotes(tmp);
	// RFC 2046 pg. 22: Trailing blanks are not part of the boundary
	// Trim leading blanks also
	tmp.trimBlanks();
	if (!tmp.null()) {
	    boundary = "\r\n--";
	    boundary << tmp;
	}
    }
    if (boundary.null())
	Debug(DebugMild,"MimeMultipartBody::getBoundary() Parameter is %s [%p]",
	    b?"empty":"missing",this);
    return !boundary.null();
}


/**
 * MimeSdpBody
 */
YCLASSIMP(MimeSdpBody,MimeBody)

MimeSdpBody::MimeSdpBody(bool hashing)
    : MimeBody("application/sdp"),
      m_lineAppend(&m_lines), m_hash(0), m_hashing(hashing)
{
}

MimeSdpBody::MimeSdpBody(const String& type, const char* buf, int len)
    : MimeBody(type),
      m_lineAppend(&m_lines), m_hash(0), m_hashing(false)
{
    buildLines(buf,len);
}

MimeSdpBody::MimeSdpBody(const MimeHeaderLine& type, const char* buf, int len)
    : MimeBody(type),
      m_lineAppend(&m_lines), m_hash(0), m_hashing(false)
{
    buildLines(buf,len);
}

MimeSdpBody::MimeSdpBody(const MimeSdpBody& original)
    : MimeBody(original.getType()),
      m_lineAppend(&m_lines), m_hash(original.m_hash), m_hashing(false)
{
    const ObjList* l = &original.m_lines;
    for (; l; l = l->next()) {
    	const NamedString* t = static_cast<NamedString*>(l->get());
        if (t)
	    addLine(t->name(),*t);
    }
    m_hashing = original.m_hashing;
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

NamedString* MimeSdpBody::addLine(const char* name, const char* value)
{
    if (m_hashing)
	m_hash = String::hash(value,String::hash(name,m_hash));
    NamedString* line = new NamedString(name,value);
    m_lineAppend = m_lineAppend->append(line);
    return line;
}

// Build the lines from a data buffer
void MimeSdpBody::buildLines(const char* buf, int len)
{
    while (len > 0) {
        String* line = getUnfoldedLine(buf,len);
	int eq = line->find('=');
	if (eq > 0)
	    addLine(line->substr(0,eq),line->substr(eq+1));
	line->destruct();
    }
}


/**
 * MimeBinaryBody
 */
YCLASSIMP(MimeBinaryBody,MimeBody)

MimeBinaryBody::MimeBinaryBody(const String& type, const char* buf, int len)
    : MimeBody(type)
{
    m_body.assign((void*)buf,len);
}

MimeBinaryBody::MimeBinaryBody(const MimeHeaderLine& type, const char* buf, int len)
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


/**
 * MimeStringBody
 */
YCLASSIMP(MimeStringBody,MimeBody)

MimeStringBody::MimeStringBody(const String& type, const char* buf, int len)
    : MimeBody(type), m_text(buf,len)
{
}

MimeStringBody::MimeStringBody(const MimeHeaderLine& type, const char* buf, int len)
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


/**
 * MimeLinesBody
 */
YCLASSIMP(MimeLinesBody,MimeBody)

MimeLinesBody::MimeLinesBody(const String& type, const char* buf, int len)
    : MimeBody(type)
{
    while (len > 0)
        m_lines.append(getUnfoldedLine(buf,len));
}

MimeLinesBody::MimeLinesBody(const MimeHeaderLine& type, const char* buf, int len)
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
