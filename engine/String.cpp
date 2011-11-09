/**
 * String.cpp
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

#include "yateclass.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>

namespace TelEngine {

// String to regular integer conversion, takes into account overflows
static int strtoi(const char* nptr, char** endptr, int base)
{
    errno = 0;
    long int val = ::strtol(nptr,endptr,base);
#if INT_MAX != LONG_MAX
    if (val >= INT_MAX) {
	errno = ERANGE;
	val = INT_MAX;
    }
    else if (val <= INT_MIN) {
	errno = ERANGE;
	val = INT_MIN;
    }
#endif
    // on overflow/underflow mark the entire string as unreadable
    if ((errno == ERANGE) && endptr)
	*endptr = (char*) nptr;
    return (int) val;
}

String operator+(const String& s1, const String& s2)
{
    String s(s1.c_str());
    s += s2.c_str();
    return s;
}

String operator+(const String& s1, const char* s2)
{
    String s(s1.c_str());
    s += s2;
    return s;
}

String operator+(const char* s1, const String& s2)
{
    String s(s1);
    s += s2;
    return s;
}

int lookup(const char* str, const TokenDict* tokens, int defvalue, int base)
{
    if (!str)
	return defvalue;
    if (tokens) {
	for (; tokens->token; tokens++)
	    if (!::strcmp(str,tokens->token))
		return tokens->value;
    }
    char *eptr = 0;
    int val = strtoi(str,&eptr,base);
    if (!eptr || *eptr)
	return defvalue;
    return val;
}

const char* lookup(int value, const TokenDict* tokens, const char* defvalue)
{
    if (tokens) {
	for (; tokens->token; tokens++)
	    if (value == tokens->value)
		return tokens->token;
    }
    return defvalue;
}

#define MAX_MATCH 9
#define INIT_HASH ((unsigned)-1)

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
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n' || (nullOk && !c));
}

// Decode a single nibble, return -1 on error
static int hexDecode(char c)
{
    if (('0' <= c) && (c <= '9'))
	return c - '0';
    if (('A' <= c) && (c <= 'F'))
	return c - 'A' + 10;
    if (('a' <= c) && (c <= 'f'))
	return c - 'a' + 10;
    return -1;
}

// Encode a single nibble
static inline char hexEncode(char nib)
{
    static const char hex[] = "0123456789abcdef";
    return hex[nib & 0x0f];
}


StringMatchPrivate::StringMatchPrivate()
{
    XDebug(DebugAll,"StringMatchPrivate::StringMatchPrivate() [%p]",this);
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
	else
	    rmatch[i].rm_eo = 0;
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


static const String s_empty;

const String& String::empty()
{
    return s_empty;
}

String::String()
    : m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String() [%p]",this);
}

String::String(const char* value, int len)
    : m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String(\"%s\",%d) [%p]",value,len,this);
    assign(value,len);
}

String::String(const String& value)
    : GenObject(),
      m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String(%p) [%p]",&value,this);
    if (!value.null()) {
	m_string = ::strdup(value.c_str());
	if (!m_string)
	    Debug("String",DebugFail,"strdup() returned NULL!");
	changed();
    }
}

String::String(char value, unsigned int repeat)
    : m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String('%c',%d) [%p]",value,repeat,this);
    if (value && repeat) {
	m_string = (char *) ::malloc(repeat+1);
	if (m_string) {
	    ::memset(m_string,value,repeat);
	    m_string[repeat] = 0;
	}
	else
	    Debug("String",DebugFail,"malloc(%d) returned NULL!",repeat+1);
	changed();
    }
}

String::String(int value)
    : m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String(%d) [%p]",value,this);
    char buf[64];
    ::sprintf(buf,"%d",value);
    m_string = ::strdup(buf);
    if (!m_string)
	Debug("String",DebugFail,"strdup() returned NULL!");
    changed();
}

String::String(unsigned int value)
    : m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String(%u) [%p]",value,this);
    char buf[64];
    ::sprintf(buf,"%u",value);
    m_string = ::strdup(buf);
    if (!m_string)
	Debug("String",DebugFail,"strdup() returned NULL!");
    changed();
}

String::String(bool value)
    : m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String(%u) [%p]",value,this);
    m_string = ::strdup(boolText(value));
    if (!m_string)
	Debug("String",DebugFail,"strdup() returned NULL!");
    changed();
}

String::String(const String* value)
    : m_string(0), m_length(0), m_hash(INIT_HASH), m_matches(0)
{
    XDebug(DebugAll,"String::String(%p) [%p]",&value,this);
    if (value && !value->null()) {
	m_string = ::strdup(value->c_str());
	if (!m_string)
	    Debug("String",DebugFail,"strdup() returned NULL!");
	changed();
    }
}

String::~String()
{
    XDebug(DebugAll,"String::~String() [%p] (\"%s\")",this,m_string);
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

String& String::assign(const char* value, int len)
{
    if (len && value && *value) {
	if (len < 0)
	    len = ::strlen(value);
	else {
	    int l = 0;
	    for (const char* p = value; l < len; l++)
		if (!*p++)
		    break;
	    len = l;
	}
	if (value != m_string || len != (int)m_length) {
	    char* data = (char*) ::malloc(len+1);
	    if (data) {
		::memcpy(data,value,len);
		data[len] = 0;
		char* odata = m_string;
		m_string = data;
		changed();
		if (odata)
		    ::free(odata);
	    }
	    else
		Debug("String",DebugFail,"malloc(%d) returned NULL!",len+1);
	}
    }
    else
	clear();
    return *this;
}

String& String::assign(char value, unsigned int repeat)
{
    if (repeat && value) {
	char* data = (char*) ::malloc(repeat+1);
	if (data) {
	    ::memset(data,value,repeat);
	    data[repeat] = 0;
	    char* odata = m_string;
	    m_string = data;
	    changed();
	    if (odata)
		::free(odata);
	}
	else
	    Debug("String",DebugFail,"malloc(%d) returned NULL!",repeat+1);
    }
    else
	clear();
    return *this;
}

String& String::hexify(void* data, unsigned int len, char sep, bool upCase)
{
    const char* hex = upCase ? "0123456789ABCDEF" : "0123456789abcdef";
    if (data && len) {
	const unsigned char* s = (const unsigned char*) data;
	unsigned int repeat = sep ? 3*len-1 : 2*len;
	// I know it's ugly to reuse but... copy/paste...
	char* data = (char*) ::malloc(repeat+1);
	if (data) {
	    char* d = data;
	    while (len--) {
		unsigned char c = *s++;
		*d++ = hex[(c >> 4) & 0x0f];
		*d++ = hex[c & 0x0f];
		if (sep)
		    *d++ = sep;
	    }
	    // wrote one too many - go back...
	    if (sep)
		d--;
	    *d = '\0';
	    char* odata = m_string;
	    m_string = data;
	    changed();
	    if (odata)
		::free(odata);
	}
	else
	    Debug("String",DebugFail,"malloc(%d) returned NULL!",repeat+1);
    }
    else
	clear();
    return *this;
}

void String::changed()
{
    clearMatches();
    m_hash = INIT_HASH;
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

int String::toInteger(int defvalue, int base, int minvalue, int maxvalue,
    bool clamp) const
{
    if (!m_string)
	return defvalue;
    char *eptr = 0;
    int val = strtoi(m_string,&eptr,base);
    if (!eptr || *eptr)
	return defvalue;
    if (val >= minvalue && val <= maxvalue)
	return val;
    if (clamp)
	return (val < minvalue) ? minvalue : maxvalue;
    return defvalue;
}

int String::toInteger(const TokenDict* tokens, int defvalue, int base) const
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

double String::toDouble(double defvalue) const
{
    if (!m_string)
	return defvalue;
    char *eptr = 0;
    double val= ::strtod(m_string,&eptr);
    if (!eptr || *eptr)
	return defvalue;
    return val;
}

static const char* str_false[] = { "false", "no", "off", "disable", "f", 0 };
static const char* str_true[] = { "true", "yes", "on", "enable", "t", 0 };

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

bool String::isBoolean() const
{
    if (!m_string)
	return false;
    const char **test;
    for (test=str_false; *test; test++)
	if (!::strcmp(m_string,*test))
	    return true;
    for (test=str_true; *test; test++)
	if (!::strcmp(m_string,*test))
	    return true;
    return false;
}

String& String::toUpper()
{
    if (m_string) {
	char c;
	for (char *s = m_string; (c = *s); s++) {
	    if (('a' <= c) && (c <= 'z'))
		*s = c + 'A' - 'a';
	}
    }
    return *this;
}

String& String::toLower()
{
    if (m_string) {
	char c;
	for (char *s = m_string; (c = *s); s++) {
	    if (('A' <= c) && (c <= 'Z'))
		*s = c + 'a' - 'A';
	}
    }
    return *this;
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

String& String::trimSpaces()
{
    if (m_string) {
	const char *s = m_string;
	while (*s == ' ' || *s == '\t' || *s == '\v' || *s == '\f' || *s == '\r' || *s == '\n')
	    s++;
	const char *e = s;
	for (const char *p = e; *p; p++)
	    if (*p != ' ' && *p != '\t' && *p != '\v' && *p != '\f' && *p != '\r' && *p != '\n')
		e = p+1;
	assign(s,e-s);
    }
    return *this;
}

String& String::operator=(const char* value)
{
    if (value && !*value)
	value = 0;
    if (value != c_str()) {
	char *tmp = m_string;
	m_string = value ? ::strdup(value) : 0;
	if (value && !m_string)
	    Debug("String",DebugFail,"strdup() returned NULL!");
	changed();
	if (tmp)
	    ::free(tmp);
    }
    return *this;
}

String& String::operator+=(const char* value)
{
    if (value && !*value)
	value = 0;
    if (value) {
	if (m_string) {
	    int olen = length();
	    int len = ::strlen(value)+olen;
	    char *tmp1 = m_string;
	    char *tmp2 = (char *) ::malloc(len+1);
	    if (tmp2) {
		::strncpy(tmp2,m_string,olen);
		tmp2[olen] = 0;
		::strncat(tmp2,value,len-olen);
		tmp2[len] = 0;
		m_string = tmp2;
		::free(tmp1);
	    }
	    else
		Debug("String",DebugFail,"malloc(%d) returned NULL!",len+1);
	}
	else {
	    m_string = ::strdup(value);
	    if (!m_string)
		Debug("String",DebugFail,"strdup() returned NULL!");
	}
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

String& String::operator>>(const char* skip)
{
    if (m_string && skip && *skip) {
	const char *loc = ::strstr(m_string,skip);
	if (loc)
	    assign(loc+::strlen(skip));
    }
    return *this;
}

String& String::operator>>(char& store)
{
    if (m_string) {
	store = m_string[0];
	assign(m_string+1);
    }
    return *this;
}

String& String::operator>>(int& store)
{
    if (m_string) {
	char *end = 0;
	int l = strtoi(m_string,&end,0);
	if (end && (m_string != end)) {
	    store = l;
	    assign(end);
	}
    }
    return *this;
}

String& String::operator>>(unsigned int& store)
{
    if (m_string) {
	char *end = 0;
	errno = 0;
	unsigned long int l = ::strtoul(m_string,&end,0);
#if UINT_MAX != ULONG_MAX
	if (l > UINT_MAX) {
	    l = UINT_MAX;
	    errno = ERANGE;
	}
#endif
	if (!errno && end && (m_string != end)) {
	    store = l;
	    assign(end);
	}
    }
    return *this;
}

String& String::operator>>(bool& store)
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

String& String::append(const char* value, const char* separator, bool force)
{
    if (value || force) {
	if (!null())
	    operator+=(separator);
	operator+=(value);
    }
    return *this;
}

String& String::append(const ObjList* list, const char* separator, bool force)
{
    if (!list)
	return *this;
    int olen = length();
    int sepLen = 0;
    if (!TelEngine::null(separator))
	sepLen = ::strlen(separator);
    int len = 0;
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	const String& src = o->get()->toString();
	if (sepLen && (len || olen) && (src.length() || force))
	    len += sepLen;
	len += src.length();
    }
    if (!len)
	return *this;
    char* oldStr = m_string;
    char* newStr = (char*)::malloc(olen + len + 1);
    if (!newStr) {
	Debug("String",DebugFail,"malloc(%d) returned NULL!",olen + len + 1);
	return *this;
    }
    if (m_string)
	::memcpy(newStr,m_string,olen);
    for (list = list->skipNull(); list; list = list->skipNext()) {
	const String& src = list->get()->toString();
	if (sepLen && olen && (src.length() || force)) {
	    ::memcpy(newStr + olen,separator,sepLen);
	    olen += sepLen;
	}
	::memcpy(newStr + olen,src.c_str(),src.length());
	olen += src.length();
    }
    newStr[olen] = 0;
    m_string = newStr;
    ::free(oldStr);
    changed();
    return *this;
}

String& String::append(double value, unsigned int decimals)
{
    if (decimals > 12)
	decimals = 12;
    char buf[80];
    ::sprintf(buf,"%0.*f",decimals,value);
    return operator+=(buf);
}

bool String::operator==(const char* value) const
{
    if (!m_string)
	return !(value && *value);
    return value && !::strcmp(m_string,value);
}

bool String::operator!=(const char* value) const
{
    if (!m_string)
	return value && *value;
    return (!value) || ::strcmp(m_string,value);
}

bool String::operator==(const String& value) const
{
    if (hash() != value.hash())
	return false;
    return operator==(value.c_str());
}

bool String::operator!=(const String& value) const
{
    if (hash() != value.hash())
	return true;
    return operator!=(value.c_str());
}

bool String::operator&=(const char* value) const
{
    if (!m_string)
	return !(value && *value);
    return value && !::strcasecmp(m_string,value);
}

bool String::operator|=(const char* value) const
{
    if (!m_string)
	return value && *value;
    return (!value) || ::strcasecmp(m_string,value);
}

int String::find(char what, unsigned int offs) const
{
    if (!m_string || (offs > m_length))
	return -1;
    const char *s = ::strchr(m_string+offs,what);
    return s ? s-m_string : -1;
}

int String::find(const char* what, unsigned int offs) const
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

bool String::startsWith(const char* what, bool wordBreak, bool caseInsensitive) const
{
    if (!(m_string && what && *what))
	return false;
    unsigned int l = ::strlen(what);
    if (m_length < l)
	return false;
    else if (wordBreak && (m_length > l) && !isWordBreak(m_string[l]))
	return false;

    if (caseInsensitive)
	return (::strncasecmp(m_string,what,l) == 0);
    return (::strncmp(m_string,what,l) == 0);
}

bool String::startSkip(const char* what, bool wordBreak, bool caseInsensitive)
{
    if (startsWith(what,wordBreak,caseInsensitive)) {
	const char *p = m_string + ::strlen(what);
	if (wordBreak)
	    while (isWordBreak(*p))
		p++;
	assign(p);
	return true;
    }
    return false;
}

bool String::endsWith(const char* what, bool wordBreak, bool caseInsensitive) const
{
    if (!(m_string && what && *what))
	return false;
    unsigned int l = ::strlen(what);
    if (m_length < l)
	return false;
    else if (wordBreak && (m_length > l) && !isWordBreak(m_string[m_length-l-1]))
	return false;
    if (caseInsensitive)
	return (::strncasecmp(m_string+m_length-l,what,l) == 0);
    return (::strncmp(m_string+m_length-l,what,l) == 0);
}

String& String::extractTo(const char* sep, String& str)
{
    int pos = find(sep);
    if (pos >= 0) {
	str = substr(0,pos);
	assign(m_string+pos+::strlen(sep));
    }
    else {
	str = *this;
	clear();
    }
    return *this;
}

String& String::extractTo(const char* sep, bool& store)
{
    String str;
    extractTo(sep,str);
    store = str.toBoolean(store);
    return *this;
}

String& String::extractTo(const char* sep, int& store, int base)
{
    String str;
    extractTo(sep,str);
    store = str.toInteger(store,base);
    return *this;
}

String& String::extractTo(const char* sep, int& store, const TokenDict* tokens, int base)
{
    String str;
    extractTo(sep,str);
    store = str.toInteger(tokens,store,base);
    return *this;
}

String& String::extractTo(const char* sep, double& store)
{
    String str;
    extractTo(sep,str);
    store = str.toDouble(store);
    return *this;
}

bool String::matches(const Regexp& rexp)
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

String String::replaceMatches(const String& templ) const
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

String String::msgEscape(const char* str, char extraEsc)
{
    String s;
    if (TelEngine::null(str))
	return s;
    char c;
    while ((c=*str++)) {
	if ((unsigned char)c < ' ' || c == ':' || c == extraEsc) {
	    c += '@';
	    s += '%';
	}
	else if (c == '%')
	    s += c;
	s += c;
    }
    return s;
}

String String::msgUnescape(const char* str, int* errptr, char extraEsc)
{
    String s;
    if (TelEngine::null(str))
	return s;
    if (extraEsc)
	extraEsc += '@';
    const char *pos = str;
    char c;
    while ((c=*pos++)) {
	if ((unsigned char)c < ' ') {
	    if (errptr)
		*errptr = (pos-str) - 1;
	    return s;
	}
	else if (c == '%') {
	    c=*pos++;
	    if ((c > '@' && c <= '_') || c == 'z' || c == extraEsc)
		c -= '@';
	    else if (c != '%') {
		if (errptr)
		    *errptr = (pos-str) - 1;
		return s;
	    }
	}
	s += c;
    }
    if (errptr)
	*errptr = -1;
    return s;
}

String String::sqlEscape(const char* str, char extraEsc)
{
    String s;
    if (TelEngine::null(str))
	return s;
    char c;
    while ((c=*str++)) {
	if (c == '\\' || c == '\'' || c == extraEsc)
	    s += '\\';
	s += c;
    }
    return s;
}

String String::uriEscape(const char* str, char extraEsc, const char* noEsc)
{
    String s;
    if (TelEngine::null(str))
	return s;
    char c;
    while ((c=*str++)) {
	if ((unsigned char)c <= ' ' || c == '%' || c == extraEsc ||
	    ((c == '+' || c == '?' || c == '&') && !(noEsc && ::strchr(noEsc,c))))
	    s << '%' << hexEncode(c >> 4) << hexEncode(c);
	else
	    s += c;
    }
    return s;
}

String String::uriUnescape(const char* str, int* errptr)
{
    String s;
    if (TelEngine::null(str))
	return s;
    const char *pos = str;
    char c;
    while ((c=*pos++)) {
	if ((unsigned char)c < ' ') {
	    if (errptr)
		*errptr = (pos-str) - 1;
	    return s;
	}
	else if (c == '%') {
	    int hiNibble = hexDecode(*pos++);
	    if (hiNibble < 0) {
		if (errptr)
		    *errptr = (pos-str) - 1;
		return s;
	    }
	    int loNibble = hexDecode(*pos++);
	    if (loNibble < 0) {
		if (errptr)
		    *errptr = (pos-str) - 1;
		return s;
	    }
	    c = ((hiNibble << 4) | loNibble) & 0xff;
	}
	s += c;
    }
    if (errptr)
	*errptr = -1;
    return s;
}

unsigned int String::hash() const
{
    if (m_hash == INIT_HASH)
	m_hash = hash(m_string);
    return m_hash;
}

unsigned int String::hash(const char* value)
{
    if (!value)
	return 0;

    unsigned int h = 0;
    // sdbm hash algorithm, hash(i) = hash(i-1) * 65599 + str[i]
    while (unsigned char c = (unsigned char) *value++)
	h = (h << 6) + (h << 16) - h + c;
    return h;
}

int String::lenUtf8(const char* value, unsigned int maxSeq, bool overlong)
{
    if (!value)
	return 0;
    if (maxSeq < 1)
	maxSeq = 4; // RFC 3629 default limit

    int count = 0;
    unsigned int more = 0;
    int32_t min = 0;
    int32_t val = 0;

    while (unsigned char c = (unsigned char) *value++) {
	if (more) {
	    // all continuation bytes are in range [128..191]
	    if ((c & 0xc0) != 0x80)
		return -1;
	    val = (val << 6) | (c & 0x3f);
	    if (!--more) {
		if (overlong)
		    continue;
		// got full value, check for overlongs
		if (val < min)
		    return -1;
	    }
	    continue;
	}
	count++;
	// from 1st byte we find out how many are supposed to follow
	if (c < 128)      // 1 byte, 0...0x7F, ASCII characters, no check
	    ;
	else if (c < 192) // invalid as first UFT-8 byte
	    return -1;
	else if (c < 224) {
	    // 2 bytes, 0x80...0x7FF
	    min = 0x80;
	    val = c & 0x1f;
	    more = 1;
	}
	else if (c < 240) {
	    // 3 bytes, 0x800...0xFFFF, Basic Multilingual Plane
	    min = 0x800;
	    val = c & 0x0f;
	    more = 2;
	}
	else if (c < 248) {
	    // 4 bytes, 0x10000...0x1FFFFF, RFC 3629 limit (10FFFF)
	    min = 0x10000;
	    val = c & 0x07;
	    more = 3;
	}
	else if (c < 252) {
	    // 5 bytes, 0x200000...0x3FFFFFF
	    min = 0x200000;
	    val = c & 0x03;
	    more = 4;
	}
	else if (c < 254) {
	    // 6 bytes, 0x4000000...0x7FFFFFFF
	    min = 0x4000000;
	    val = c & 0x01;
	    more = 5;
	}
	else
	    return -1;
	// check if we accept a character with such sequence length
	if (more >= maxSeq)
	    return -1;
    }
    if (more)
	return -1;
    return count;
}

int String::fixUtf8(const char* replace, unsigned int maxSeq, bool overlong)
{
    if (null())
	return 0;
    if (maxSeq < 1)
	maxSeq = 4; // RFC 3629 default limit
    if (!replace)
	replace = "\xEF\xBF\xBD";

    int count = 0;
    unsigned int more = 0;
    int32_t min = 0;
    int32_t val = 0;
    unsigned int pos = 0;
    bool bad = false;
    String tmp;

    for (unsigned int i = 0; i < m_length; i++) {
	unsigned char c = (unsigned char) at(i);
	if (more) {
	    // remember to reject a character with a too long sequence
	    if (more >= maxSeq)
		bad = true;
	    // all continuation bytes are in range [128..191]
	    if ((c & 0xc0) != 0x80) {
		// truncated sequence, must search for 1st byte again
		more = 0;
		count++;
		tmp += replace;
	    }
	    else {
		val = (val << 6) | (c & 0x3f);
		if (!--more) {
		    // got full value, check for overlongs
		    if ((val < min) && !overlong)
			bad = true;
		    // finished multibyte, add it to temporary
		    if (bad) {
			count++;
			tmp += replace;
		    }
		    else
			tmp += substr(pos,(int)(i+1-pos));
		}
		continue;
	    }
	}
	pos = i;
	bad = false;
	// from 1st byte we find out how many are supposed to follow
	if (c < 128)      // 1 byte, 0...0x7F, ASCII characters, good
	    ;
	else if (c < 192) // invalid as first UFT-8 byte
	    bad = true;
	else if (c < 224) {
	    // 2 bytes, 0x80...0x7FF
	    min = 0x80;
	    val = c & 0x1f;
	    more = 1;
	}
	else if (c < 240) {
	    // 3 bytes, 0x800...0xFFFF, Basic Multilingual Plane
	    min = 0x800;
	    val = c & 0x0f;
	    more = 2;
	}
	else if (c < 248) {
	    // 4 bytes, 0x10000...0x1FFFFF, RFC 3629 limit (10FFFF)
	    min = 0x10000;
	    val = c & 0x07;
	    more = 3;
	}
	else if (c < 252) {
	    // 5 bytes, 0x200000...0x3FFFFFF
	    min = 0x200000;
	    val = c & 0x03;
	    more = 4;
	}
	else if (c < 254) {
	    // 6 bytes, 0x4000000...0x7FFFFFFF
	    min = 0x4000000;
	    val = c & 0x01;
	    more = 5;
	}
	else
	    bad = true;
	if (!more) {
	    if (bad) {
		count++;
		tmp += replace;
	    }
	    else
		tmp += (char)c;
	}
    }
    if (more) {
	// UTF-8 truncated at end of string
	count++;
	tmp += replace;
    }

    if (count)
	operator=(tmp);
    return count;
}

void* String::getObject(const String& name) const
{
    if (name == YSTRING("String"))
	return const_cast<String*>(this);
    return GenObject::getObject(name);
}

const String& String::toString() const
{
    return *this;
}


Regexp::Regexp()
    : m_regexp(0), m_flags(0)
{
    XDebug(DebugAll,"Regexp::Regexp() [%p]",this);
}

Regexp::Regexp(const char* value, bool extended, bool insensitive)
    : String(value), m_regexp(0), m_flags(0)
{
    XDebug(DebugAll,"Regexp::Regexp(\"%s\",%d,%d) [%p]",
	value,extended,insensitive,this);
    setFlags(extended,insensitive);
    compile();
}

Regexp::Regexp(const Regexp& value)
    : String(value.c_str()), m_regexp(0), m_flags(value.m_flags)
{
    XDebug(DebugAll,"Regexp::Regexp(%p) [%p]",&value,this);
}

Regexp::~Regexp()
{
    cleanup();
}

bool Regexp::matches(const char* value, StringMatchPrivate* matchlist) const
{
    XDebug(DebugInfo,"Regexp::matches(\"%s\",%p)",value,matchlist);
    if (!value)
	value = "";
    if (!compile())
	return false;
    int mm = matchlist ? MAX_MATCH : 0;
    regmatch_t *mt = matchlist ? (matchlist->rmatch)+1 : 0;
    return !::regexec((regex_t *)m_regexp,value,mm,mt,0);
}

bool Regexp::matches(const char* value) const
{
    return matches(value,0);
}

void Regexp::changed()
{
    cleanup();
    String::changed();
}

bool Regexp::compile() const
{
    XDebug(DebugInfo,"Regexp::compile()");
    if (c_str() && !m_regexp) {
	regex_t *data = (regex_t *) ::malloc(sizeof(regex_t));
	if (!data) {
	    Debug("Regexp",DebugFail,"malloc(%d) returned NULL!",(int)sizeof(regex_t));
	    return false;
	}
	if (::regcomp(data,c_str(),m_flags)) {
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
    XDebug(DebugInfo,"Regexp::cleanup()");
    if (m_regexp) {
	regex_t *data = (regex_t *)m_regexp;
	m_regexp = 0;
	::regfree(data);
	::free(data);
    }
}

void Regexp::setFlags(bool extended, bool insensitive)
{
    int f = (extended ? REG_EXTENDED : 0) | (insensitive ? REG_ICASE : 0);
    if (m_flags != f) {
	cleanup();
	m_flags = f;
    }
}

bool Regexp::isExtended() const
{
    return (m_flags & REG_EXTENDED) != 0;
}

bool Regexp::isCaseInsensitive() const
{
    return (m_flags & REG_ICASE) != 0;
}


NamedString::NamedString(const char* name, const char* value)
    : String(value), m_name(name)
{
    XDebug(DebugAll,"NamedString::NamedString(\"%s\",\"%s\") [%p]",name,value,this);
}

const String& NamedString::toString() const
{
    return m_name;
}

void* NamedString::getObject(const String& name) const
{
    if (name == YSTRING("NamedString"))
	return (void*)this;
    return String::getObject(name);
}


NamedPointer::NamedPointer(const char* name, GenObject* data, const char* value)
    : NamedString(name,value),
    m_data(0)
{
    userData(data);
}

NamedPointer::~NamedPointer()
{
    userData(0);
}

// Set obscure data carried by this object.
void NamedPointer::userData(GenObject* data)
{
    TelEngine::destruct(m_data);
    m_data = data;
}

// Retrieve the pointer carried by this object and release ownership
GenObject* NamedPointer::takeData()
{
    GenObject* tmp = m_data;
    m_data = 0;
    return tmp;
}

void* NamedPointer::getObject(const String& name) const
{
    if (name == YSTRING("NamedPointer"))
	return (void*)this;
    void* p = NamedString::getObject(name);
    if (p)
	return p;
    if (m_data)
	return m_data->getObject(name);
    return 0;
}

// Called whenever the string value changed. Release the pointer
void NamedPointer::changed()
{
    userData(0);
    NamedString::changed();
}


void* GenObject::getObject(const String& name) const
{
    return 0;
}

const String& GenObject::toString() const
{
    return String::empty();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
