/**
 * DataBlock.cpp
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

#include <string.h>
#include <stdlib.h>

extern "C" {
#include "all.h"
}

using namespace TelEngine;


static const DataBlock s_empty;

const DataBlock& DataBlock::empty()
{
    return s_empty;
}

DataBlock::DataBlock()
    : m_data(0), m_length(0)
{
}

DataBlock::DataBlock(const DataBlock& value)
    : GenObject(),
      m_data(0), m_length(0)
{
    assign(value.data(),value.length());
}

DataBlock::DataBlock(void* value, unsigned int len, bool copyData)
    : m_data(0), m_length(0)
{
    assign(value,len,copyData);
}

DataBlock::~DataBlock()
{
    clear();
}

void* DataBlock::getObject(const String& name) const
{
    if (name == YSTRING("DataBlock"))
	return const_cast<DataBlock*>(this);
    return GenObject::getObject(name);
}

void DataBlock::clear(bool deleteData)
{
    m_length = 0;
    if (m_data) {
	void *data = m_data;
	m_data = 0;
	if (deleteData)
	    ::free(data);
    }
}

DataBlock& DataBlock::assign(void* value, unsigned int len, bool copyData)
{
    if ((value != m_data) || (len != m_length)) {
	void *odata = m_data;
	m_length = 0;
	m_data = 0;
	if (len) {
	    if (copyData) {
		void *data = ::malloc(len);
		if (data) {
		    if (value)
			::memcpy(data,value,len);
		    else
			::memset(data,0,len);
		    m_data = data;
		}
		else
		    Debug("DataBlock",DebugFail,"malloc(%d) returned NULL!",len);
	    }
	    else
		m_data = value;
	    if (m_data)
		m_length = len;
	}
	if (odata && (odata != m_data))
	    ::free(odata);
    }
    return *this;
}

void DataBlock::truncate(unsigned int len)
{
    if (!len)
	clear();
    else if (len < m_length)
	assign(m_data,len);
}

void DataBlock::cut(int len)
{
    if (!len)
	return;

    int ofs = 0;
    if (len < 0)
	ofs = len = -len;

    if ((unsigned)len >= m_length) {
	clear();
	return;
    }

    assign(ofs+(char *)m_data,m_length - len);
}

DataBlock& DataBlock::operator=(const DataBlock& value)
{
    assign(value.data(),value.length());
    return *this;
}

void DataBlock::append(const DataBlock& value)
{
    if (m_length) {
	if (value.length()) {
	    unsigned int len = m_length+value.length();
	    void *data = ::malloc(len);
	    if (data) {
		::memcpy(data,m_data,m_length);
		::memcpy(m_length+(char*)data,value.data(),value.length());
		assign(data,len,false);
	    }
	    else
		Debug("DataBlock",DebugFail,"malloc(%d) returned NULL!",len);
	}
    }
    else
	assign(value.data(),value.length());
}

void DataBlock::append(const String& value)
{
    if (m_length) {
	if (value.length()) {
	    unsigned int len = m_length+value.length();
	    void *data = ::malloc(len);
	    if (data) {
		::memcpy(data,m_data,m_length);
		::memcpy(m_length+(char*)data,value.safe(),value.length());
		assign(data,len,false);
	    }
	    else
		Debug("DataBlock",DebugFail,"malloc(%d) returned NULL!",len);
	}
    }
    else
	assign((void*)value.c_str(),value.length());
}

void DataBlock::insert(const DataBlock& value)
{
    unsigned int vl = value.length();
    if (m_length) {
	if (vl) {
	    unsigned int len = m_length+vl;
	    void *data = ::malloc(len);
	    if (data) {
		::memcpy(data,value.data(),vl);
		::memcpy(vl+(char*)data,m_data,m_length);
		assign(data,len,false);
	    }
	    else
		Debug("DataBlock",DebugFail,"malloc(%d) returned NULL!",len);
	}
    }
    else
	assign(value.data(),vl);
}

bool DataBlock::convert(const DataBlock& src, const String& sFormat,
    const String& dFormat, unsigned maxlen)
{
    if (sFormat == dFormat) {
	operator=(src);
	return true;
    }
    unsigned sl = 0, dl = 0;
    void *ctable = 0;
    if (sFormat == YSTRING("slin")) {
	sl = 2;
	dl = 1;
	if (dFormat == YSTRING("alaw"))
	    ctable = s2a;
	else if (dFormat == YSTRING("mulaw"))
	    ctable = s2u;
    }
    else if (sFormat == YSTRING("alaw")) {
	sl = 1;
	if (dFormat == YSTRING("mulaw")) {
	    dl = 1;
	    ctable = a2u;
	}
	else if (dFormat == YSTRING("slin")) {
	    dl = 2;
	    ctable = a2s;
	}
    }
    else if (sFormat == YSTRING("mulaw")) {
	sl = 1;
	if (dFormat == YSTRING("alaw")) {
	    dl = 1;
	    ctable = u2a;
	}
	else if (dFormat == YSTRING("slin")) {
	    dl = 2;
	    ctable = u2s;
	}
    }
    if (!ctable) {
	clear();
	return false;
    }
    unsigned len = src.length();
    if (maxlen && (maxlen < len))
	len = maxlen;
    len /= sl;
    if (!len) {
	clear();
	return true;
    }
    resize(len * dl);
    if ((sl == 1) && (dl == 1)) {
	unsigned char *s = (unsigned char *) src.data();
	unsigned char *d = (unsigned char *) data();
	unsigned char *c = (unsigned char *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    else if ((sl == 1) && (dl == 2)) {
	unsigned char *s = (unsigned char *) src.data();
	unsigned short *d = (unsigned short *) data();
	unsigned short *c = (unsigned short *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    else if ((sl == 2) && (dl == 1)) {
	unsigned short *s = (unsigned short *) src.data();
	unsigned char *d = (unsigned char *) data();
	unsigned char *c = (unsigned char *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    return true;
}

// Decode a single nibble, return -1 on error
inline signed char hexDecode(char c)
{
    if (('0' <= c) && (c <= '9'))
	return c - '0';
    if (('A' <= c) && (c <= 'F'))
	return c - 'A' + 10;
    if (('a' <= c) && (c <= 'f'))
	return c - 'a' + 10;
    return -1;
}

// Build this data block from a hexadecimal string representation.
// Each octet must be represented in the input string with 2 hexadecimal characters.
// If a separator is specified, the octets in input string must be separated using
//  exactly 1 separator. Only 1 leading or 1 trailing separators are allowed
bool DataBlock::unHexify(const char* data, unsigned int len, char sep)
{
    clear();
    if (!(data && len))
	return true;

    // Calculate the destination buffer length
    unsigned int n = 0;
    if (!sep) {
	if (0 != (len % 2))
	    return false;
	n = len / 2;
    }
    else {
	// Remove leading and trailing separators
	if (data[0] == sep) {
	    data++;
	    len--;
	}
	if (len && data[len-1] == sep)
	    len--;
	// No more leading and trailing separators allowed
	if (2 != (len % 3))
	    return (bool)(len == 0);
	n = (len + 1) / 3;
    }
    if (!n)
	return true;

    char* buf = (char*)::malloc(n);
    unsigned int iBuf = 0;
    for (unsigned int i = 0; i < len; i += (sep ? 3 : 2)) {
	signed char c1 = hexDecode(data[i]);
	signed char c2 = hexDecode(data[i+1]);
	if (c1 == -1 || c2 == -1 || (sep && (iBuf != n - 1) && (sep != data[i+2])))
	    break;
	buf[iBuf++] = (c1 << 4) | c2;
    }
    if (iBuf >= n)
	assign(buf,n,false);
    else
	::free(buf);
    return (iBuf >= n);
}

String DataBlock::sqlEscape(char extraEsc) const
{
    unsigned int len = m_length;
    unsigned int i;
    for (i = 0; i < m_length; i++) {
	char c = static_cast<char*>(m_data)[i];
	if (c == '\0' || c == '\r' || c == '\n' || c == '\\' || c == '\'' || c == extraEsc)
	    len++;
    }
    String tmp(' ',len);
    char* d = const_cast<char*>(tmp.c_str());
    for (i = 0; i < m_length; i++) {
	char c = static_cast<char*>(m_data)[i];
	if (c == '\0' || c == '\r' || c == '\n' || c == '\\' || c == '\'' || c == extraEsc)
	    *d++ = '\\';
	switch (c) {
	    case '\0':
		c = '0';
		break;
	    case '\r':
		c = 'r';
		break;
	    case '\n':
		c = 'n';
		break;
	}
	*d++ = c;
    }
    return tmp;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
