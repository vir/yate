/**
 * util.h
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

#include <yateclass.h>

namespace TelEngine {

// Utility function, returns an uncompacted header name
const char* uncompactForm(const char* header);

// Utility function, returns a compacted header name
const char* compactForm(const char* header);

}

/* vi: set ts=8 sw=4 sts=4 noet: */
