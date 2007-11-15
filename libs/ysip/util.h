/**
 * util.h
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

#include <yateclass.h>

namespace TelEngine {

// Utility function, returns an uncompacted header name
const char* uncompactForm(const char* header);

// Utility function, returns a compacted header name
const char* compactForm(const char* header);

// Utility function, puts quotes around a string
void addQuotes(String& str);

// Utility function, removes quotes around a string
void delQuotes(String& str);

// Utility function, puts quotes around a string
String quote(const String& str);

// Utility function to find a separator not in "quotes" or inside <uri>
int findSep(const char* str, char sep, int offs = 0);

}

/* vi: set ts=8 sw=4 sts=4 noet: */
