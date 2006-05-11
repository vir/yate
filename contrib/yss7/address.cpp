/**
 * address.cpp
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2006 Null Team
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

#include "yatess7.h"


using namespace TelEngine;

unsigned int SS7CodePoint::pack(Type type) const
{
    if (!compatible(type))
	return 0;
    switch (type) {
	case ITU:
	    return ((m_network & 7) << 11) | (m_cluster << 3) | (m_member & 7);
	case ANSI:
	    return (m_network << 16) | (m_cluster << 8) | m_member;
	// TODO: handle China and Japan
	default:
	    return 0;
    }
}

bool SS7CodePoint::compatible(Type type) const
{
    switch (type) {
	case ITU:
	    return ((m_network | m_member) & 0xf8) == 0;
	case ANSI:
	    return true;
	// TODO: handle China and Japan
	default:
	    return false;
    }
}

unsigned char SS7CodePoint::size(Type type)
{
    switch (type) {
	case ITU:
	    return 14;
	case ANSI:
	case China:
	    return 24;
	case Japan:
	    return 16;
	default:
	    return 0;
    }
}

String& TelEngine::operator<<(String& str, const SS7CodePoint& cp)
{
    str << (int)cp.network() << "-" << (int)cp.cluster() << "-" << (int)cp.member();
    return str;
}


bool SS7Label::compatible(SS7CodePoint::Type type) const
{
    switch (type) {
	case SS7CodePoint::ITU:
	    if (m_sls & 0xf0)
		return false;
	    break;
	case SS7CodePoint::ANSI:
	    if (m_sls & 0xe0)
		return false;
	    break;
	// TODO: handle China and Japan
	default:
	    return false;
    }
    return m_dpc.compatible(type) && m_spc.compatible(type);
}

unsigned char SS7Label::size(SS7CodePoint::Type type)
{
    switch (type) {
	case SS7CodePoint::ITU:
	    return 32;
	case SS7CodePoint::ANSI:
	    return 53;
	// TODO: handle China and Japan
	default:
	    return 0;
    }
}

unsigned int SS7Label::length(SS7CodePoint::Type type)
{
    switch (type) {
	case SS7CodePoint::ITU:
	    return 4;
	case SS7CodePoint::ANSI:
	    return 7;
	// TODO: handle China and Japan
	default:
	    return 0;
    }
}

String& TelEngine::operator<<(String& str, const SS7Label& label)
{
    str << label.spc() << ":" << label.dpc() << ":" << (int)label.sls();
    return str;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
