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

}

/* vi: set ts=8 sw=4 sts=4 noet: */
