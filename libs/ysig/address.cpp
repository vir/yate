/**
 * address.cpp
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

bool SS7CodePoint::unpack(Type type, unsigned int packed)
{
    switch (type) {
	case ITU:
	    if (packed & ~0x3fff)
		return false;
	    assign((packed >> 11) & 7,(packed >> 3) & 0xff,packed & 7);
	    return true;
	case ANSI:
	    if (packed & ~0xffffff)
		return false;
	    assign((packed >> 16) & 0xff,(packed >> 8) & 0xff,packed & 0xff);
	    return true;
	// TODO: handle China and Japan
	default:
	    return false;
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


SS7Label::SS7Label()
    : m_type(SS7CodePoint::Other), m_sls(0)
{
}

SS7Label::SS7Label(SS7CodePoint::Type type, const SS7MSU& msu)
    : m_type(SS7CodePoint::Other), m_sls(0)
{
    assign(type,msu);
}

bool SS7Label::assign(SS7CodePoint::Type type, const SS7MSU& msu)
{
    unsigned int llen = length(type);
    if (llen && llen < msu.length()) {
	const unsigned char* s = (const unsigned char*) msu.data();
	switch (type) {
	    case SS7CodePoint::ITU:
		m_type = type;
		// it's easier to pack/unpack than to pick all those bits separately
		m_dpc.unpack(type,s[1] | ((s[2] & 0x3f) << 8));
		m_spc.unpack(type,((s[2] & 0xc0) >> 6) | (s[3] << 2) | ((s[4] & 0x0f) << 10));
		m_sls = (s[4] >> 4) & 0x0f;
		return true;
	    case SS7CodePoint::ANSI:
		m_type = type;
		m_dpc.assign(s[3],s[2],s[1]);
		m_spc.assign(s[6],s[5],s[4]);
		m_sls = s[7] & 0x1f;
		return true;
	    // TODO: handle China and Japan
	    default:
		break;
	}
    }
    return false;
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
