/**
 * String.cpp
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

#include "telengine.h"

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>

namespace TelEngine {

String operator+(const String &s1, const String &s2)
{
    String s(s1.c_str());
    s += s2.c_str();
    return s;
}

String operator+(const String &s1, const char *s2)
{
    String s(s1.c_str());
    s += s2;
    return s;
}

String operator+(const char *s1, const String &s2)
{
    String s(s1);
    s += s2;
    return s;
}

int lookup(const char *str, const TokenDict *tokens, int defvalue, int base)
{
    if (!str)
	return defvalue;
    if (tokens) {
	for (; tokens->token; tokens++)
	    if (!::strcmp(str,tokens->token))
		return tokens->value;
    }
    char *eptr = 0;
    long int val= ::strtol(str,&eptr,base);
    if (!eptr || *eptr)
	return defvalue;
    return val;
}

const char *lookup(int value, const TokenDict *tokens, const char *defvalue)
{
    if (tokens) {
	for (; tokens->token; tokens++)
	    if (value == tokens->value)
		return tokens->token;
    }
    return defvalue;
}

#define MAX_MATCH 9

class StringMatchPrivate
{
public:
    StringMatchPrivate();
    void fixup();
    void clear();
    int count;
    regmatch_t rmatch[MAX_MATCH+1];
};

};

using namespace TelEngine;

static bool isWordBreak(char c, bool nullOk = false)
{
    return (c == ' ' || c == '\t' || c == '\n' || (nullOk && !c));
}

StringMatchPrivate::StringMatchPrivate()
{
    DDebug(DebugAll,"StringMatchPrivate::StringMatchPrivate() [%p]",this);
    clear();
}

void StringMatchPrivate::clear()
{
    count = 0;
    for (int i = 0; i <= MAX_MATCH; i++) {
	rmatch[i].rm_so = -1;
	rmatch[i].rm_eo = 0;
    }
}

void StringMatchPrivate::fixup()
{
    count = 0;
    rmatch[0].rm_so = rmatch[1].rm_so;
    rmatch[0].rm_eo = 0;
    int i, c = 0;
    for (i = 1; i <= MAX_MATCH; i++) {
	if (rmatch[i].rm_so != -1) {
	    rmatch[0].rm_eo = rmatch[i].rm_eo - rmatch[0].rm_so;
	    rmatch[i].rm_eo -= rmatch[i].rm_so;
	    c = i;
	}
    }
    // Cope with the regexp stupidity.
    if (c > 1) {
	for (i = 0; i < c; i++)
	    rmatch[i] = rmatch[i+1];
	rmatch[c].rm_so = -1;
	c--;
    }
    count = c;
}

String::String()
    : m_string(0), m_length(0), m_hash(0), m_matches(0)
{
    DDebug(DebugAll,"String::String() [%p]",this);
}

String::String(const char *value, int len)
    : m_string(0), m_length(0), m_hash(0), m_matches(0)
{
    DDebug(DebugAll,"String::String(\"%s\",%d) [%p]",value,len,this);
    assign(value,len);
}

String::String(const String &value)
    : m_string(0), m_length(0), m_hash(0), m_matches(0)
{
    DDebug(DebugAll,"String::String(%p) [%p]",&value,this);
    if (!value.null()) {
	m_string = ::strdup(value.c_str());
	changed();
    }
}

String::String(char value, unsigned int repeat)
    : m_string(0), m_length(0), m_hash(0), m_matches(0)
{
    DDebug(DebugAll,"String::String('%c',%d) [%p]",value,repeat,this);
    if (value && repeat) {
	m_string = (char *) ::malloc(repeat+1);
	::memset(m_string,value,repeat);
	m_string[repeat] = 0;
	changed();
    }
}

String::String(int value)
    : m_string(0), m_length(0), m_hash(0), m_matches(0)
{
    DDebug(DebugAll,"String::String(%d) [%p]",value,this);
    char buf[64];
    ::sprintf(buf,"%d",value);
    m_string = ::strdup(buf);
    changed();
}

String::String(unsigned int value)
    : m_string(0), m_length(0), m_hash(0), m_matches(0)
{
    DDebug(DebugAll,"String::String(%u) [%p]",value,this);
    char buf[64];
    ::sprintf(buf,"%u",value);
    m_string = ::strdup(buf);
    changed();
}

String::String(bool value)
    : m_string(0), m_length(0), m_hash(0), m_matches(0)
{
    DDebug(DebugAll,"String::String(%u) [%p]",value,this);
    m_string = ::strdup(value ? "true" : "false");
    changed();
}

String::~String()
{
    DDebug(DebugAll,"String::~String() [%p] (\"%s\")",this,m_string);
    if (m_matches) {
	StringMatchPrivate *odata = m_matches;
	m_matches = 0;
	delete odata;
    }
    if (m_string) {
	char *odata = m_string;
	m_length = 0;
	m_string = 0;
	::free(odata);
    }
}

String& String::assign(const char *value, int len)
{
    if (len && value && *value) {
	if (len < 0)
	    len = ::strlen(value);
	if (value != m_string || len != (int)m_length) {
	    char *data = (char *) ::malloc(len+1);
	    ::memcpy(data,value,len);
	    data[len] = 0;
	    char *odata = m_string;
	    m_string = data;
	    changed();
	    if (odata)
		::free(odata);
	}
    }
    else
	clear();
    return *this;
}

void String::changed()
{
    clearMatches();
    m_hash = 0;
    m_length = m_string ? ::strlen(m_string) : 0;
}

void String::clear()
{
    if (m_string) {
	char *odata = m_string;
	m_string = 0;
	changed();
	::free(odata);
    }
}

char String::at(int index) const
{
    if ((index < 0) || ((unsigned)index >= m_length) || !m_string)
	return 0;
    return m_string[index];
}

String String::substr(int offs, int len) const
{
    if (offs < 0) {
	offs += m_length;
	if (offs < 0)
	    offs = 0;
    }
    if ((unsigned int)offs >= m_length)
	return String();
    return String(c_str()+offs,len);
}

int String::toInteger(int defvalue, int base) const
{
    if (!m_string)
	return defvalue;
    char *eptr = 0;
    long int val= ::strtol(m_string,&eptr,base);
    if (!eptr || *eptr)
	return defvalue;
    return val;
}

int String::toInteger(const TokenDict *tokens, int defvalue, int base) const
{
    if (!m_string)
	return defvalue;
    if (tokens) {
	for (; tokens->token; tokens++)
	    if (operator==(tokens->token))
		return tokens->value;
    }
    return toInteger(defvalue,base);
}

static const char *str_false[] = { "false", "no", "off", "disable", 0 };
static const char *str_true[] = { "true", "yes", "on", "enable", 0 };

bool String::toBoolean(bool defvalue) const
{
    if (!m_string)
	return defvalue;
    const char **test;
    for (test=str_false; *test; test++)
	if (!::strcmp(m_string,*test))
	    return false;
    for (test=str_true; *test; test++)
	if (!::strcmp(m_string,*test))
	    return true;
    return defvalue;
}

String& String::trimBlanks()
{
    if (m_string) {
	const char *s = m_string;
	while (*s == ' ' || *s == '\t')
	    s++;
	const char *e = s;
	for (const char *p = e; *p; p++)
	    if (*p != ' ' && *p != '\t')
		e = p+1;
	assign(s,e-s);
    }
    return *this;
}

String& String::operator=(const char *value)
{
    if (value != c_str()) {
	char *tmp = m_string;
	m_string = value ? ::strdup(value) : 0;
	changed();
	if (tmp)
	    ::free(tmp);
    }
    return *this;
}

String& String::operator+=(const char *value)
{
    if (value && *value) {
	if (m_string) {
	    char *tmp1 = m_string;
	    char *tmp2 = (char *) ::malloc(::strlen(value)+length()+1);
	    ::strcpy(tmp2,m_string);
	    ::strcat(tmp2,value);
	    m_string = tmp2;
	    ::free(tmp1);
	}
	else
	    m_string = ::strdup(value);
	changed();
    }
    return *this;
}

String& String::operator=(char value)
{
    char buf[2] = {value,0};
    return operator=(buf);
}

String& String::operator=(int value)
{
    char buf[64];
    ::sprintf(buf,"%d",value);
    return operator=(buf);
}

String& String::operator=(unsigned int value)
{
    char buf[64];
    ::sprintf(buf,"%u",value);
    return operator=(buf);
}

String& String::operator+=(char value)
{
    char buf[2] = {value,0};
    return operator+=(buf);
}

String& String::operator+=(int value)
{
    char buf[64];
    ::sprintf(buf,"%d",value);
    return operator+=(buf);
}

String& String::operator+=(unsigned int value)
{
    char buf[64];
    ::sprintf(buf,"%u",value);
    return operator+=(buf);
}

String& String::operator>>(const char *skip)
{
    if (m_string && skip && *skip) {
	const char *loc = ::strstr(m_string,skip);
	if (loc)
	    assign(loc+::strlen(skip));
    }
    return *this;
}

String& String::operator>>(char &store)
{
    if (m_string) {
	store = m_string[0];
	assign(m_string+1);
    }
    return *this;
}

String& String::operator>>(int &store)
{
    if (m_string) {
	char *end = 0;
	long int l = ::strtol(m_string,&end,0);
	if (end && (m_string != end)) {
	    store = l;
	    assign(end);
	}
    }
    return *this;
}

String& String::operator>>(unsigned int &store)
{
    if (m_string) {
	char *end = 0;
	unsigned long int l = ::strtoul(m_string,&end,0);
	if (end && (m_string != end)) {
	    store = l;
	    assign(end);
	}
    }
    return *this;
}

String& String::operator>>(bool &store)
{
    if (m_string) {
	const char *s = m_string;
	while (*s == ' ' || *s == '\t')
	    s++;
	const char **test;
	for (test=str_false; *test; test++) {
	    int l = ::strlen(*test);
	    if (!::strncmp(s,*test,l) && isWordBreak(s[l],true)) {
		store = false;
		assign(s+l);
		return *this;
	    }
	}
	for (test=str_true; *test; test++) {
	    int l = ::strlen(*test);
	    if (!::strncmp(s,*test,l) && isWordBreak(s[l],true)) {
		store = true;
		assign(s+l);
		return *this;
	    }
	}
    }
    return *this;
}

bool String::operator==(const char *value) const
{
    if (!m_string)
	return !(value && *value);
    return value && !::strcmp(m_string,value);
}

bool String::operator!=(const char *value) const
{
    if (!m_string)
	return value && *value;
    return (!value) || ::strcmp(m_string,value);
}

bool String::operator==(const String &value) const
{
    if (hash() != value.hash())
	return false;
    return operator==(value.c_str());
}

bool String::operator!=(const String &value) const
{
    if (hash() != value.hash())
	return true;
    return operator!=(value.c_str());
}

int String::find(char what, unsigned int offs) const
{
    if (!m_string || (offs > m_length))
	return -1;
    const char *s = ::strchr(m_string+offs,what);
    return s ? s-m_string : -1;
}

int String::find(const char *what, unsigned int offs) const
{
    if (!(m_string && what && *what) || (offs > m_length))
	return -1;
    const char *s = ::strstr(m_string+offs,what);
    return s ? s-m_string : -1;
}

int String::rfind(char what) const
{
    if (!m_string)
	return -1;
    const char *s = ::strrchr(m_string,what);
    return s ? s-m_string : -1;
}

bool String::startsWith(const char *what, bool wordBreak) const
{
    if (!(m_string && what && *what))
	return false;
    unsigned int l = ::strlen(what);
    if (m_length < l)
	return false;
    else if (wordBreak && (m_length > l) && !isWordBreak(m_string[l]))
	return false;
    return (::strncmp(m_string,what,l) == 0);
}

bool String::startSkip(const char *what, bool wordBreak)
{
    if (startsWith(what,wordBreak)) {
	const char *p = m_string + ::strlen(what);
	if (wordBreak)
	    while (isWordBreak(*p))
		p++;
	assign(p);
	return true;
    }
    return false;
}

bool String::endsWith(const char *what, bool wordBreak) const
{
    if (!(m_string && what && *what))
	return false;
    unsigned int l = ::strlen(what);
    if (m_length < l)
	return false;
    else if (wordBreak && (m_length > l) && !isWordBreak(m_string[m_length-l-1]))
	return false;
    return (::strncmp(m_string+m_length-l,what,l) == 0);
}

bool String::matches(Regexp &rexp)
{
    if (m_matches)
	clearMatches();
    else
	m_matches = new StringMatchPrivate;
    if (rexp.matches(c_str(),m_matches)) {
	m_matches->fixup();
	return true;
    }
    return false;
}

int String::matchOffset(int index) const
{
    if ((!m_matches) || (index < 0) || (index > m_matches->count))
	return -1;
    return m_matches->rmatch[index].rm_so;
}

int String::matchLength(int index) const
{
    if ((!m_matches) || (index < 0) || (index > m_matches->count))
	return 0;
    return m_matches->rmatch[index].rm_eo;
}

int String::matchCount() const
{
    if (!m_matches)
	return 0;
    return m_matches->count;
}

String String::replaceMatches(const String &templ) const
{
    String s;
    int pos, ofs = 0;
    for (;;) {
	pos = templ.find('\\',ofs);
	if (pos < 0) {
	    s << templ.substr(ofs);
	    break;
	}
	s << templ.substr(ofs,pos-ofs);
	pos++;
	char c = templ[pos];
	if (c == '\\') {
	    pos++;
	    s << "\\";
	}
	else if ('0' <= c && c <= '9') {
	    pos++;
	    s << matchString(c - '0');
	}
	else {
	    pos++;
	    s << "\\" << c;
	}
	ofs = pos;
    }
    return s;
}

void String::clearMatches()
{
    if (m_matches)
	m_matches->clear();
}

ObjList* String::split(char separator, bool emptyOK) const
{
    ObjList *list = new ObjList;
    int p = 0;
    int s;
    while ((s = find(separator,p)) >= 0) {
	if (emptyOK || (s > p))
	    list->append(new String(m_string+p,s-p));
	p = s + 1;
    }
    if (emptyOK || (m_string && m_string[p]))
	list->append(new String(m_string+p));
    return list;
}

String String::msgEscape(const char *str, char extraEsc)
{
    if (!str)
	str = "";
    String s;
    char c;
    while ((c=*str++)) {
	if (c < ' ' || c == extraEsc) {
	    c += '@';
	    s += '%';
	}
	else if (c == '%')
	    s += c;
	s += c;
    }
    return s;
}

String String::msgUnescape(const char *str, int *errptr, char extraEsc)
{
    if (!str)
	str = "";
    if (extraEsc)
	extraEsc += '@';
    const char *pos = str;
    String s;
    char c;
    while ((c=*pos++)) {
	if (c < ' ') {
	    if (errptr)
		*errptr = (pos-str);
	    return s;
	}
	else if (c == '%') {
	    c=*pos++;
	    if ((c > '@' && c <= '_') || c == extraEsc)
		c -= '@';
	    else if (c != '%') {
		if (errptr)
		    *errptr = (pos-str);
		return s;
	    }
	}
	s += c;
    }
    if (errptr)
	*errptr = -1;
    return s;
}

unsigned int String::hash() const
{
    if (!m_hash)
	m_hash = hash(m_string);
    return m_hash;
}

unsigned int String::hash(const char *value)
{
    if (!value)
	return 0;

    unsigned int h = 0;
    while (unsigned char c = (unsigned char) *value++)
	h = (h << 1) + c;
    return h;
}

Regexp::Regexp()
    : m_regexp(0)
{
    DDebug(DebugAll,"Regexp::Regexp() [%p]",this);
}

Regexp::Regexp(const char *value)
    : String(value), m_regexp(0)
{
    DDebug(DebugAll,"Regexp::Regexp(\"%s\") [%p]",value,this);
}

Regexp::Regexp(const Regexp &value)
    : String(value.c_str()), m_regexp(0)
{
    DDebug(DebugAll,"Regexp::Regexp(%p) [%p]",&value,this);
}

Regexp::~Regexp()
{
    cleanup();
}

bool Regexp::matches(const char *value, StringMatchPrivate *matches)
{
    DDebug(DebugInfo,"Regexp::matches(\"%s\",%p)",value,matches);
    if (!value)
	return false;
    if (!compile())
	return false;
    int mm = matches ? MAX_MATCH : 0;
    regmatch_t *mt = matches ? (matches->rmatch)+1 : 0;
    return !::regexec((regex_t *)m_regexp,value,mm,mt,0);
}

bool Regexp::matches(const char *value)
{
    return matches(value,0);
}

void Regexp::changed()
{
    cleanup();
    String::changed();
}

bool Regexp::compile()
{
    DDebug(DebugInfo,"Regexp::compile()");
    if (c_str() && !m_regexp) {
	regex_t *data = (regex_t *) ::malloc(sizeof(regex_t));
	if (::regcomp(data,c_str(),0)) {
	    Debug(DebugWarn,"Regexp::compile() \"%s\" failed",c_str());
	    ::regfree(data);
	    ::free(data);
	}
	else
	    m_regexp = (void *)data;
    }
    return (m_regexp != 0);
}

void Regexp::cleanup()
{
    DDebug(DebugInfo,"Regexp::cleanup()");
    if (m_regexp) {
	regex_t *data = (regex_t *)m_regexp;
	m_regexp = 0;
	::regfree(data);
	::free(data);
    }
}

NamedString::NamedString(const char *name, const char *value)
    : String(value), m_name(name)
{
    DDebug(DebugAll,"NamedString::NamedString(\"%s\",\"%s\") [%p]",name,value,this);
}
