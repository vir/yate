/**
 * address.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#include "yatesig.h"


using namespace TelEngine;

#define MAKE_NAME(x) { #x, x }
const TokenDict SS7PointCode::s_names[] = {
	MAKE_NAME(ITU),
	MAKE_NAME(ANSI),
	MAKE_NAME(ANSI8),
	MAKE_NAME(China),
	MAKE_NAME(Japan),
	MAKE_NAME(Japan5),
	{ 0, 0 }
	};
#undef MAKE_NAME

// Assign data members from a given string of form 'network-cluster-member'
// Return false if the string has incorrect format or individual elements are not in the range 0..255
bool SS7PointCode::assign(const String& src, Type type)
{
    if (src.null())
	return false;
    if (type != Other) {
	unsigned int packed = src.toInteger();
	if (packed)
	    return unpack(type,packed);
    }
    unsigned char params[3];
    unsigned int len = 0;
    ObjList* list = src.split('-',false);
    if (list->count() == sizeof(params)) {
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    int tmp = (static_cast<String*>(o->get()))->toInteger(-1);
	    if (tmp >= 0 && tmp <= 255)
		params[len++] = (unsigned char)tmp;
	    else
		break;
	}
    }
    TelEngine::destruct(list);
    if (len == sizeof(params)) {
	assign(params[0],params[1],params[2]);
	return true;
    }
    return false;
}

unsigned int SS7PointCode::pack(Type type) const
{
    if (!compatible(type))
	return 0;
    switch (type) {
	case ITU:
	    return ((m_network & 7) << 11) | (m_cluster << 3) | (m_member & 7);
	case ANSI:
	case ANSI8:
	case China:
	    return (m_network << 16) | (m_cluster << 8) | m_member;
	case Japan:
	case Japan5:
	    return ((m_network & 0x7f) << 9) | ((m_cluster & 0x0f) << 5) | (m_member & 0x1f);
	default:
	    return 0;
    }
}

bool SS7PointCode::unpack(Type type, unsigned int packed)
{
    switch (type) {
	case ITU:
	    if (packed & ~0x3fff)
		return false;
	    assign((packed >> 11) & 7,(packed >> 3) & 0xff,packed & 7);
	    return true;
	case ANSI:
	case ANSI8:
	case China:
	    if (packed & ~0xffffff)
		return false;
	    assign((packed >> 16) & 0xff,(packed >> 8) & 0xff,packed & 0xff);
	    return true;
	case Japan:
	case Japan5:
	    assign((packed >> 9) & 0x7f,(packed >> 5) & 0x0f,packed & 0x1f);
	default:
	    return false;
    }
}

bool SS7PointCode::compatible(Type type) const
{
    switch (type) {
	case ITU:
	    return ((m_network | m_member) & 0xf8) == 0;
	case ANSI:
	case ANSI8:
	case China:
	    return true;
	case Japan:
	case Japan5:
	    return ((m_network & 0x80) | (m_cluster & 0xf0) | (m_member & 0xe0)) == 0;
	default:
	    return false;
    }
}

unsigned char SS7PointCode::size(Type type)
{
    switch (type) {
	case ITU:
	    return 14;
	case ANSI:
	case ANSI8:
	case China:
	    return 24;
	case Japan:
	case Japan5:
	    return 16;
	default:
	    return 0;
    }
}

unsigned char SS7PointCode::length(Type type)
{
    switch (type) {
	case ITU:
	case Japan:
	case Japan5:
	    return 2;
	case ANSI:
	case ANSI8:
	case China:
	    return 3;
	default:
	    return 0;
    }
}

bool SS7PointCode::assign(Type type, const unsigned char* src, int len, unsigned char* spare)
{
    if (!src)
	return false;
    unsigned int llen = length(type);
    if (!llen)
	return false;
    if ((len >= 0) && ((unsigned int)len < llen))
	return false;

    unsigned int tmp = 0;
    unsigned char sbits = 0;
    for (unsigned int i = 0; i < llen; i++) {
	unsigned char c = *src++;
	if (i == (llen - 1)) {
	    // last octet may hold spare bits
	    unsigned int sshift = size(type) & 7;
	    if (sshift) {
		sbits = c >> sshift;
		c &= (0xff >> (8 - sshift));
	    }
	}
	tmp |= ((unsigned int)c << (i * 8));
    }
    if (unpack(type,tmp)) {
	if (spare)
	    *spare = sbits;
	return true;
    }
    return false;
}

bool SS7PointCode::store(Type type, unsigned char* dest, unsigned char spare) const
{
    if (!dest)
	return false;
    unsigned int len = length(type);
    if (!len)
	return false;
    unsigned int tmp = pack(type);
    unsigned int sshift = size(type);
    if (len*8 > sshift)
	tmp |= ((unsigned int)spare) << sshift;
    while (len--) {
	*dest++ = tmp & 0xff;
	tmp >>= 8;
    }
    return true;
}

String& TelEngine::operator<<(String& str, const SS7PointCode& cp)
{
    str << (int)cp.network() << "-" << (int)cp.cluster() << "-" << (int)cp.member();
    return str;
}


SS7Label::SS7Label()
    : m_type(SS7PointCode::Other), m_sls(0), m_spare(0)
{
}

SS7Label::SS7Label(const SS7Label& original)
    : m_type(SS7PointCode::Other), m_sls(0), m_spare(0)
{
    assign(original.type(),original.dpc(),original.opc(),original.sls(),original.spare());
}

SS7Label::SS7Label(const SS7Label& original, unsigned char sls, unsigned char spare)
    : m_type(SS7PointCode::Other), m_sls(0), m_spare(0)
{
    assign(original.type(),original.opc(),original.dpc(),sls,spare);
}

SS7Label::SS7Label(SS7PointCode::Type type, const SS7PointCode& dpc,
    const SS7PointCode& opc, unsigned char sls, unsigned char spare)
    : m_type(SS7PointCode::Other), m_sls(0), m_spare(0)
{
    assign(type,dpc,opc,sls,spare);
}

SS7Label::SS7Label(SS7PointCode::Type type, unsigned int dpc,
    unsigned int opc, unsigned char sls, unsigned char spare)
    : m_type(SS7PointCode::Other), m_sls(0), m_spare(0)
{
    assign(type,dpc,opc,sls,spare);
}

SS7Label::SS7Label(SS7PointCode::Type type, const SS7MSU& msu)
    : m_type(SS7PointCode::Other), m_sls(0), m_spare(0)
{
    assign(type,msu);
}

void SS7Label::assign(SS7PointCode::Type type, const SS7PointCode& dpc,
    const SS7PointCode& opc, unsigned char sls, unsigned char spare)
{
    m_type = type;
    m_dpc = dpc;
    m_opc = opc;
    m_sls = sls;
    m_spare = spare;
}

void SS7Label::assign(SS7PointCode::Type type, unsigned int dpc,
    unsigned int opc, unsigned char sls, unsigned char spare)
{
    m_type = type;
    m_dpc.unpack(type,dpc);
    m_opc.unpack(type,opc);
    m_sls = sls;
    m_spare = spare;
}

bool SS7Label::assign(SS7PointCode::Type type, const SS7MSU& msu)
{
    unsigned int llen = length(type);
    if (!llen)
	return false;
    return assign(type,(const unsigned char*)msu.getData(1,llen),llen);
}

bool SS7Label::assign(SS7PointCode::Type type, const unsigned char* src, int len)
{
    unsigned int llen = length(type);
    if (!llen)
	return false;
    if ((len >= 0) && ((unsigned int)len < llen))
	return false;

    switch (type) {
	case SS7PointCode::ITU:
	    m_type = type;
	    // it's easier to pack/unpack than to pick all those bits separately
	    m_dpc.unpack(type,src[0] | ((src[1] & 0x3f) << 8));
	    m_opc.unpack(type,((src[1] & 0xc0) >> 6) | (src[2] << 2) | ((src[3] & 0x0f) << 10));
	    m_sls = (src[3] >> 4) & 0x0f;
	    m_spare = 0;
	    return true;
	case SS7PointCode::ANSI:
	    m_type = type;
	    m_dpc.assign(src[2],src[1],src[0]);
	    m_opc.assign(src[5],src[4],src[3]);
	    m_sls = src[6] & 0x1f;
	    m_spare = src[6] >> 5;
	    return true;
	case SS7PointCode::ANSI8:
	    m_type = type;
	    m_dpc.assign(src[2],src[1],src[0]);
	    m_opc.assign(src[5],src[4],src[3]);
	    m_sls = src[6];
	    m_spare = 0;
	    return true;
	case SS7PointCode::China:
	    m_type = type;
	    m_dpc.assign(src[2],src[1],src[0]);
	    m_opc.assign(src[5],src[4],src[3]);
	    m_sls = src[6] & 0x0f;
	    m_spare = src[6] >> 4;
	    return true;
	case SS7PointCode::Japan:
	    m_type = type;
	    m_dpc.unpack(type,src[0] | (src[1] << 8));
	    m_opc.unpack(type,src[2] | (src[3] << 8));
	    m_sls = src[4] & 0x0f;
	    m_spare = src[4] >> 4;
	    return true;
	case SS7PointCode::Japan5:
	    m_type = type;
	    m_dpc.unpack(type,src[0] | (src[1] << 8));
	    m_opc.unpack(type,src[2] | (src[3] << 8));
	    m_sls = src[4] & 0x1f;
	    m_spare = src[4] >> 5;
	    return true;
	default:
	    break;
    }
    return false;
}

bool SS7Label::store(unsigned char* dest) const
{
    if (!dest)
	return false;
    unsigned int tmp = 0;
    switch (m_type) {
	case SS7PointCode::ITU:
	    tmp = m_dpc.pack(m_type) | (m_opc.pack(m_type) << 14) | ((unsigned int)m_sls << 28);
	    *dest++ = (unsigned char)(tmp & 0xff);
	    *dest++ = (unsigned char)((tmp >> 8) & 0xff);
	    *dest++ = (unsigned char)((tmp >> 16) & 0xff);
	    *dest++ = (unsigned char)((tmp >> 24) & 0xff);
	    break;
	case SS7PointCode::ANSI:
	    *dest++ = m_dpc.member();
	    *dest++ = m_dpc.cluster();
	    *dest++ = m_dpc.network();
	    *dest++ = m_opc.member();
	    *dest++ = m_opc.cluster();
	    *dest++ = m_opc.network();
	    *dest++ = (m_sls & 0x1f) | (m_spare << 5);
	    break;
	case SS7PointCode::ANSI8:
	    *dest++ = m_dpc.member();
	    *dest++ = m_dpc.cluster();
	    *dest++ = m_dpc.network();
	    *dest++ = m_opc.member();
	    *dest++ = m_opc.cluster();
	    *dest++ = m_opc.network();
	    *dest++ = m_sls;
	    break;
	case SS7PointCode::China:
	    *dest++ = m_dpc.member();
	    *dest++ = m_dpc.cluster();
	    *dest++ = m_dpc.network();
	    *dest++ = m_opc.member();
	    *dest++ = m_opc.cluster();
	    *dest++ = m_opc.network();
	    *dest++ = (m_sls & 0x0f) | (m_spare << 4);
	    break;
	case SS7PointCode::Japan:
	    tmp = m_dpc.pack(m_type) | (m_opc.pack(m_type) << 16);
	    *dest++ = (unsigned char)(tmp & 0xff);
	    *dest++ = (unsigned char)((tmp >> 8) & 0xff);
	    *dest++ = (unsigned char)((tmp >> 16) & 0xff);
	    *dest++ = (unsigned char)((tmp >> 24) & 0xff);
	    *dest++ = (m_sls & 0x0f) | (m_spare << 4);
	    break;
	case SS7PointCode::Japan5:
	    tmp = m_dpc.pack(m_type) | (m_opc.pack(m_type) << 16);
	    *dest++ = (unsigned char)(tmp & 0xff);
	    *dest++ = (unsigned char)((tmp >> 8) & 0xff);
	    *dest++ = (unsigned char)((tmp >> 16) & 0xff);
	    *dest++ = (unsigned char)((tmp >> 24) & 0xff);
	    *dest++ = (m_sls & 0x1f) | (m_spare << 5);
	default:
	    return false;
    }
    return true;
}

bool SS7Label::compatible(SS7PointCode::Type type) const
{
    switch (type) {
	case SS7PointCode::ITU:
	case SS7PointCode::China:
	case SS7PointCode::Japan:
	    if (m_sls & 0xf0)
		return false;
	    if (m_spare & 0xf0)
		return false;
	    break;
	case SS7PointCode::ANSI:
	case SS7PointCode::Japan5:
	    if (m_sls & 0xe0)
		return false;
	    if (m_spare & 0xf8)
		return false;
	    break;
	case SS7PointCode::ANSI8:
	    if (m_spare)
		return false;
	    break;
	default:
	    return false;
    }
    return m_dpc.compatible(type) && m_opc.compatible(type);
}

unsigned char SS7Label::size(SS7PointCode::Type type)
{
    switch (type) {
	case SS7PointCode::ITU:
	    return 32;
	case SS7PointCode::ANSI:
	    return 53;
	case SS7PointCode::ANSI8:
	    return 56;
	case SS7PointCode::China:
	    return 52;
	case SS7PointCode::Japan:
	    return 36;
	case SS7PointCode::Japan5:
	    return 37;
	default:
	    return 0;
    }
}

unsigned int SS7Label::length(SS7PointCode::Type type)
{
    switch (type) {
	case SS7PointCode::ITU:
	    return 4;
	case SS7PointCode::ANSI:
	case SS7PointCode::ANSI8:
	case SS7PointCode::China:
	    return 7;
	case SS7PointCode::Japan:
	case SS7PointCode::Japan5:
	    return 5;
	default:
	    return 0;
    }
}


String& TelEngine::operator<<(String& str, const SS7Label& label)
{
    str << label.opc() << ":" << label.dpc() << ":" << (int)label.sls();
    return str;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
