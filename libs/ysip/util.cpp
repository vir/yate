/**
 * util.cpp
 * Yet Another SIP Stack
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
