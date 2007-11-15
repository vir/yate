/**
 * util.cpp
 * Yet Another SIP Stack
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

#include "util.h"

#include <string.h>

namespace TelEngine {

static const char* compactForms[] = {
    "a", "Accept-Contact",
    "u", "Allow-Events",
    "i", "Call-ID",
    "m", "Contact",
    "e", "Content-Encoding",
    "l", "Content-Length",
    "c", "Content-Type",
    "o", "Event",
    "f", "From",
    "y", "Identity",
    "n", "Identity-Info",
    "r", "Refer-To",
    "b", "Referred-By",
    "j", "Reject-Contact",
    "d", "Request-Disposition",
    "x", "Session-Expires",
    "s", "Subject",
    "k", "Supported",
    "t", "To",
    "v", "Via",
    0 };

// Utility function, returns an uncompacted header name
const char* uncompactForm(const char* header)
{
    if (header && header[0] && !header[1]) {
	char c = header[0];
	const char **p = compactForms;
	for (; *p; p += 2)
	    if (**p == c)
		return *++p;
    }
    return header;
}

// Utility function, returns a compacted header name
const char* compactForm(const char* header)
{
    if (header && *header) {
	const char **p = compactForms;
	for (; *p; p += 2)
	    if (!::strcasecmp(p[1],header))
		return p[0];
    }
    return header;
}

// Utility function, puts quotes around a string
void addQuotes(String& str)
{
    str.trimBlanks();
    int l = str.length();
    if ((l < 2) || (str[0] != '"') || (str[l-1] != '"'))
	str = "\"" + str + "\"";
}

// Utility function, removes quotes around a string
void delQuotes(String& str)
{
    str.trimBlanks();
    int l = str.length();
    if ((l >= 2) && (str[0] == '"') && (str[l-1] == '"')) {
	str = str.substr(1,l-2);
	str.trimBlanks();
    }
}

// Utility function, puts quotes around a string
String quote(const String& str)
{
    String tmp(str);
    addQuotes(tmp);
    return tmp;
}

// Utility function to find a separator not in "quotes" or inside <uri>
int findSep(const char* str, char sep, int offs)
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

}

/* vi: set ts=8 sw=4 sts=4 noet: */
