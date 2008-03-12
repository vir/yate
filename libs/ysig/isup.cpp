/**
 * isup.cpp
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

#include "yatesig.h"
#include "string.h"

using namespace TelEngine;

// Maximum number of mandatory parameters including two terminators
#define MAX_MANDATORY_PARAMS 16

// Description of each ISUP parameter
struct IsupParam {
    // numeric type of the parameter
    SS7MsgISUP::Parameters type;
    // size in octets, zero for variable
    unsigned char size;
    // SS7 name of the parameter
    const char* name;
    // decoder callback function
    bool (*decoder)(const SS7ISUP*,NamedList&,const IsupParam*,
	const unsigned char*,unsigned int,const String&);
    // encoder callback function
    unsigned char (*encoder)(const SS7ISUP*,SS7MSU&,unsigned char*,
	const IsupParam*,const NamedString*,const NamedList*,const String&);
    // table data to be used by the callback
    const void* data;
};

// This structure describes parameters of each ISUP message for each dialect
struct MsgParams {
    // type of the message described
    SS7MsgISUP::Type type;
    // does the message support optional part?
    bool optional;
    // parameters, fixed then variable, separated/terminated by EndOfParameters
    //  using an array is a (moderate) waste of space
    const SS7MsgISUP::Parameters params[MAX_MANDATORY_PARAMS];
};


// Nature of Address Indicator
static TokenDict s_dict_nai[] = {
    { "subcriber",     1 },
    { "unknown",       2 },
    { "national",      3 },
    { "international", 4 },
    { 0, 0 }
};

// Numbering Plan Indicator
static TokenDict s_dict_numPlan[] = {
    { "unknown",  0 },
    { "isdn",     1 },
    { "data",     3 },
    { "telex",    4 },
    { "private",  5 },
    { "national", 6 },
    { 0, 0 }
};

// Address Presentation
static TokenDict s_dict_presentation[] = {
    { "allowed",     0 },
    { "restricted",  1 },
    { "unavailable", 2 },
    // aliases for restrict=...
    { "no",    0 },
    { "false", 0 },
    { "yes",   1 },
    { "true",  1 },
    { 0, 0 }
};

// Screening Indicator
static TokenDict s_dict_screening[] = {
    { "user-provided",        0 },
    { "user-provided-passed", 1 },
    { "user-provided-failed", 2 },
    { "network-provided",     3 },
    // aliases for screened=...
    { "no",    0 },
    { "false", 0 },
    { "yes",   1 },
    { "true",  1 },
    { 0, 0 }
};

// Message Compatibility Information (Q.763 3.33)
static const SignallingFlags s_flags_msgcompat[] = {
    { 0x01, 0x01, "end-node" },          // End node / transit exchange
    { 0x02, 0x02, "release" },           // Release call indicator
    { 0x04, 0x04, "cnf" },               // Pass on set but not possible: Send CNF / RLC
    { 0x08, 0x08, "discard-msg" },       // Discard / pass on message
    { 0x10, 0x10, "discard" },           // Pass on set but not possible: Discard information / Release call
    { 0, 0, 0 }
};

// Parameter Compatibility Information (Q.763 3.41)
static const SignallingFlags s_flags_paramcompat[] = {
    { 0x01, 0x01, "end-node" },          // End node / transit exchange
    { 0x02, 0x02, "release" },           // Release call indicator
    { 0x04, 0x04, "cnf" },               // Parameter pass on set but not possible: Send CNF / RLC
    { 0x08, 0x08, "discard-msg" },       // Discard / pass on message
    { 0x10, 0x10, "discard-param" },     // Discard / pass on parameter
    { 0x60, 0x40, "discard" },           // Parameter pass on set but not possible: Discard parameter / Release call
    { 0, 0, 0 }
};

// Default decoder, dumps raw octets
static bool decodeRaw(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String raw;
    raw.hexify((void*)buf,len,' ');
    DDebug(isup,DebugInfo,"decodeRaw decoded %s=%s",param->name,raw.c_str());
    list.addParam(prefix+param->name,raw);
    return true;
}

// Integer decoder, interprets data as little endian integer
static bool decodeInt(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    unsigned int val = 0;
    int shift = 0;
    while (len--) {
	val |= ((unsigned int)*buf++) << shift;
	shift += 8;
    }
    DDebug(isup,DebugAll,"decodeInt decoded %s=%s (%u)",param->name,lookup(val,(const TokenDict*)param->data),val);
    SignallingUtils::addKeyword(list,prefix+param->name,(const TokenDict*)param->data,val);
    return true;
}

// Decoder for ISUP indicators (flags)
static bool decodeFlags(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    const SignallingFlags* flags = (const SignallingFlags*)param->data;
    if (!flags)
	return false;
    return SignallingUtils::decodeFlags(isup,list,prefix+param->name,flags,buf,len);
}

// Utility function - extract just ISUP digits from a parameter
static void getDigits(String& num, unsigned char oddNum, const unsigned char* buf, unsigned int len)
{
    bool odd = (oddNum & 0x80) != 0;
    static const char digits[] = "0123456789\0BC\0\0.";
    for (unsigned int i = 0; i < len; i++) {
	num += digits[buf[i] & 0x0f];
	if (odd && ((i+1) == len))
	    break;
	num += digits[buf[i] >> 4];
    }
}

const char* getIsupParamName(unsigned char type);

// Decoder for message or parameter compatibility
// Q.763 3.33/3.41
static bool decodeCompat(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (!len)
	return false;
    switch (param->type) {
	case SS7MsgISUP::MessageCompatInformation:
	    SignallingUtils::decodeFlags(isup,list,prefix+param->name,s_flags_msgcompat,buf,1);
	    if (buf[0] & 0x80) {
		if (len == 1)
		    return true;
		DDebug(isup,DebugMild,
		    "decodeCompat invalid len=%u for %s with first byte having ext bit set",len,param->name);
		break;
	    }
	    return 0 != SignallingUtils::dumpDataExt(isup,list,prefix+param->name+".more",buf+1,len-1);
	case SS7MsgISUP::ParameterCompatInformation:
	    for (unsigned int i = 0; i < len;) {
		unsigned char val = buf[i++];
		if (i == len) {
		    Debug(isup,DebugMild,"decodeCompat unexpected end of data (len=%u) for %s",len,param->name);
		    return false;
		}
		const char* paramName = getIsupParamName(val);
		String name = prefix + param->name;
		if (paramName)
		    name << "." << paramName;
		else {
		    Debug(isup,DebugMild,"decodeCompat found unknown parameter %u for %s",val,param->name);
		    name << "." << (unsigned int)val;
		}
		SignallingUtils::decodeFlags(isup,list,name,s_flags_paramcompat,buf+i,1);
		if (buf[i++] & 0x80)
		    continue;
		unsigned int count = SignallingUtils::dumpDataExt(isup,list,name+".more",buf+i,len-i);
		if (!count)
		    return false;
		i += count;
	    }
	    return true;
	default:
	    Debug(isup,DebugStub,"decodeCompat not implemented for %s",param->name);
    }
    return false;
}

// Decoder for various ISUP digit sequences (phone numbers)
static bool decodeDigits(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 2)
	return false;
    unsigned char nai = buf[0] & 0x7f;
    unsigned char plan = (buf[1] >> 4) & 7;
    unsigned char pres = (buf[1] >> 2) & 3;
    unsigned char scrn = buf[1] & 3;
    String tmp;
    getDigits(tmp,buf[0],buf+2,len-2);
    DDebug(isup,DebugAll,"decodeDigits decoded %s='%s' inn/ni=%u nai=%u plan=%u pres=%u scrn=%u",
	param->name,tmp.c_str(),buf[1] >> 7,nai,plan,pres,scrn);
    String preName(prefix + param->name);
    list.addParam(preName,tmp);
    SignallingUtils::addKeyword(list,preName+".nature",s_dict_nai,nai);
    SignallingUtils::addKeyword(list,preName+".plan",s_dict_numPlan,plan);
    switch (param->type) {
	case SS7MsgISUP::CalledPartyNumber:
	case SS7MsgISUP::RedirectionNumber:
	case SS7MsgISUP::LocationNumber:
	    tmp = ((buf[1] & 0x80) == 0);
	    list.addParam(preName+".inn",tmp);
	    break;
	case SS7MsgISUP::CallingPartyNumber:
	    tmp = ((buf[1] & 0x80) == 0);
	    list.addParam(preName+".complete",tmp);
	    break;
	default:
	    break;
    }
    switch (param->type) {
	case SS7MsgISUP::CallingPartyNumber:
	case SS7MsgISUP::RedirectingNumber:
	case SS7MsgISUP::OriginalCalledNumber:
	case SS7MsgISUP::LocationNumber:
	case SS7MsgISUP::ConnectedNumber:
	    SignallingUtils::addKeyword(list,preName+".restrict",s_dict_presentation,pres);
	default:
	    break;
    }
    switch (param->type) {
	case SS7MsgISUP::CallingPartyNumber:
	case SS7MsgISUP::LocationNumber:
	case SS7MsgISUP::ConnectedNumber:
	    SignallingUtils::addKeyword(list,preName+".screened",s_dict_screening,scrn);
	default:
	    break;
    }
    return true;
}

// Special decoder for subsequent number
static bool decodeSubseq(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String tmp;
    getDigits(tmp,buf[0],buf+1,len-1);
    DDebug(isup,DebugAll,"decodeSubseq decoded %s='%s'",param->name,tmp.c_str());
    list.addParam(prefix+param->name,tmp);
    return true;
}

// Decoder for circuit group range and status (Q.763 3.43)
static bool decodeRangeSt(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String preName(prefix + param->name);
    // 1st octet is the range code (range - 1)
    len--;
    unsigned int range = 1 + *buf++;
    unsigned int octets = (range + 7) / 8;
    if (octets > len) {
	if (len)
	    Debug(isup,DebugMild,"decodeRangeSt truncating range of %u bits to %u octets!",range,len);
	octets = len;
    }
    list.addParam(preName,String(range));

    String map;
    if (len) {
	unsigned char mask = 1;
	while (range--) {
	    map += (buf[0] & mask) ? "1" : "0";
	    mask <<= 1;
	    if (!mask) {
		++buf;
		if (!--octets)
		    break;
		mask = 1;
	    }
	}
	list.addParam(preName+".map",map);
    }

    DDebug(isup,DebugAll,"decodeRangeSt decoded %s=%u '%s'",param->name,range,map.c_str());
    return true;
}

// Decoder for generic notification indicators (Q.763 3.25)
static bool decodeNotif(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String flg;
    for (; len; len--) {
	unsigned char val = *buf++;
	if (val & 0x80)
	    break;
	const char* keyword = lookup(val & 0x7f,(const TokenDict*)param->data);
	if (keyword)
	    flg.append(keyword,",");
	else {
	    String tmp(0x7f & (int)val);
	    flg.append(tmp,",");
	}
    }
    DDebug(isup,DebugAll,"decodeNotif decoded %s='%s'",param->name,flg.c_str());
    list.addParam(prefix+param->name,flg);
    return true;
}

// Decoder for User Service Information
static bool decodeUSI(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    return SignallingUtils::decodeCaps(isup,list,buf,len,prefix+param->name,true);
}

// Decoder for cause indicators
static bool decodeCause(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    return SignallingUtils::decodeCause(isup,list,buf,len,prefix+param->name,true);
}

// Default encoder, get hexified octets
static unsigned char encodeRaw(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList*, const String&)
{
    if (!(param && val))
	return 0;
    DDebug(isup,DebugInfo,"encodeRaw encoding %s=%s",param->name,val->c_str());
    DataBlock raw;
    if (!raw.unHexify(val->c_str(),val->length(),' ')) {
	DDebug(isup,DebugMild,"encodeRaw failed: invalid string");
	return 0;
    }
    if (!raw.length() || raw.length() > 254 ||
	(param->size && param->size != raw.length())) {
	DDebug(isup,DebugMild,"encodeRaw failed: param size=%u data length=%u",
	    param->size,raw.length());
	return 0;
    }
    if (buf) {
	::memcpy(buf,raw.data(),raw.length());
	return raw.length();
    }
    unsigned char size = param->size ? (unsigned char)param->size : (unsigned char)raw.length();
    msu.append(&size,1);
    msu += raw;
    return raw.length() + size;
}

// Encoder for fixed length ISUP indicators (flags)
static unsigned char encodeFlags(const SS7ISUP* isup, SS7MSU& msu, unsigned char* buf,
    const IsupParam* param, const NamedString* val, const NamedList*, const String&)
{
    if (!param)
	return 0;
    unsigned int n = param->size;
    if (!(n && param->data))
	return 0;
    unsigned int v = 0;
    if (val) {
	ObjList* lst = val->split(',',false);
	ObjList* p = lst->skipNull();
	for (; p; p = p->skipNext()) {
	    const String* s = static_cast<const String*>(p->get());
	    const SignallingFlags* flags = (const SignallingFlags*)param->data;
	    for (; flags->mask; flags++) {
		if (*s == flags->name) {
		    if (v & flags->mask) {
			Debug(isup,DebugMild,"Flag %s.%s overwriting bits 0x%x",
			    param->name,flags->name,v & flags->mask);
			v &= flags->mask;
		    }
		    v |= flags->value;
		}
	    }
	}
	TelEngine::destruct(lst);
    }
    else {
	// locate the defaults
	const SignallingFlags* flags = (const SignallingFlags*)param->data;
	while (flags->mask)
	    flags++;
	v = flags->value;
    }
    DDebug(isup,DebugAll,"encodeFlags encoding %s=0x%x on %u octets",param->name,v,n);
    if (!buf) {
	unsigned int l = msu.length();
	DataBlock dummy(0,n+1);
	msu += dummy;
	buf = (unsigned char*)msu.getData(l,n+1);
	*buf++ = n & 0xff;
    }
    while (n--) {
	*buf++ = v & 0xff;
	v >>= 8;
    }
    return param->size;
}

// Encoder for fixed length little-endian integer values
static unsigned char encodeInt(const SS7ISUP* isup, SS7MSU& msu, unsigned char* buf,
    const IsupParam* param, const NamedString* val, const NamedList*, const String&)
{
    if (!param)
	return 0;
    unsigned int n = param->size;
    if (!n)
	return 0;
    unsigned int v = 0;
    if (val)
	v = val->toInteger((const TokenDict*)param->data);
    DDebug(isup,DebugAll,"encodeInt encoding %s=%u on %u octets",param->name,v,n);
    if (!buf) {
	unsigned int l = msu.length();
	DataBlock dummy(0,n+1);
	msu += dummy;
	buf = (unsigned char*)msu.getData(l,n+1);
	*buf++ = n & 0xff;
    }
    while (n--) {
	*buf++ = v & 0xff;
	v >>= 8;
    }
    return param->size;
}

// Utility function - write digit sequences
static unsigned char setDigits(SS7MSU& msu, const char* val, unsigned char nai, int b2 = -1, int b3 = -1)
{
    unsigned char buf[32];
    buf[1] = nai & 0x7f;
    unsigned int len = 2;
    if (b2 >= 0) {
	buf[len++] = b2 & 0xff;
	if (b3 >= 0)
	    buf[len++] = b3 & 0xff;
    }
    bool odd = false;
    while (val && (len < sizeof(buf))) {
	char c = *val++;
	if (!c)
	    break;
	unsigned char n = 0;
	if (('0' <= c) && (c <= '9'))
	    n = c - '0';
	else if ('.' == c)
	    n = 15;
	else if ('B' == c)
	    n = 11;
	else if ('C' == c)
	    n = 12;
	else
	    continue;
	odd = !odd;
	if (odd)
	    buf[len] = n;
	else
	    buf[len++] |= (n << 4);
    }
    if (odd) {
	buf[1] |= 0x80;
	len++;
    }
    buf[0] = (len-1) & 0xff;
    DDebug(DebugAll,"setDigits encoding %u octets (%s)",len,odd ? "odd" : "even");
    DataBlock tmp(buf,len,false);
    msu += tmp;
    tmp.clear(false);
    return buf[0];
}

// Encoder for variable length digit sequences
static unsigned char encodeDigits(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!param || buf || param->size)
	return 0;
    unsigned char nai = 2;
    unsigned char plan = 1;
    String preName(prefix + param->name);
    if (val && extra) {
	nai = extra->getIntValue(preName+".nature",s_dict_nai,nai);
	plan = extra->getIntValue(preName+".plan",s_dict_numPlan,plan);
    }
    unsigned char b2 = (plan & 7) << 4;
    switch (param->type) {
	case SS7MsgISUP::CalledPartyNumber:
	case SS7MsgISUP::RedirectionNumber:
	case SS7MsgISUP::LocationNumber:
	    if (val && extra && !extra->getBoolValue(preName+".inn",true))
		b2 |= 0x80;
	    break;
	case SS7MsgISUP::CallingPartyNumber:
	    if (val && extra && !extra->getBoolValue(preName+".complete",true))
		b2 |= 0x80;
	    break;
	default:
	    break;
    }
    switch (param->type) {
	case SS7MsgISUP::CallingPartyNumber:
	case SS7MsgISUP::RedirectingNumber:
	case SS7MsgISUP::OriginalCalledNumber:
	case SS7MsgISUP::LocationNumber:
	case SS7MsgISUP::ConnectedNumber:
	    if (val && extra)
		b2 |= (extra->getIntValue(preName+".restrict",s_dict_presentation) & 3) << 2;
	default:
	    break;
    }
    switch (param->type) {
	case SS7MsgISUP::CallingPartyNumber:
	case SS7MsgISUP::LocationNumber:
	case SS7MsgISUP::ConnectedNumber:
	    if (val && extra)
		b2 |= extra->getIntValue(preName+".screened",s_dict_screening) & 3;
	default:
	    break;
    }
    return setDigits(msu,val ? val->c_str() : 0,nai,b2);
}

// Encoder for circuit group range and status (Q.763 3.43)
static unsigned char encodeRangeSt(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!(param && val))
	return 0;
    unsigned char data[34] = {1};
    // 1st octet is the range code (range - 1)
    unsigned int range = val->toInteger(0);
    if (range < 1 || range > 256) {
	Debug(isup,DebugNote,"encodeRangeSt invalid range %s=%s",val->name().c_str(),val->safe());
	return 0;
    }
    data[1] = range - 1;
    // Next octets: status bits for the circuits given by range
    NamedString* map = extra->getParam(prefix+param->name+".map");
    if (map && *map) {
	// Max status bits is 256. Relevant status bits: range
	if (range < map->length()) {
	    Debug(isup,DebugNote,"encodeRangeSt truncating status bits %u to %u",map->length(),range);
	    return 0;
	}
	unsigned char* src = (unsigned char*)map->c_str();
	unsigned char* dest = data + 1;
	for (unsigned char mask = 0; range && *src; range--, src++) {
	    if (mask == 1) {
		data[0]++;
		*++dest = 0;
	    }
	    if (*src != '0')
		*dest |= mask;
	    mask = (mask == 128 ? 1 : mask << 1);
	}
    }
    // Copy to msu
    DDebug(isup,DebugAll,"encodeRangeSt encoding %s on %u octets",param->name,data[0]);
    DataBlock tmp(data,data[0] + 1,false);
    msu += tmp;
    tmp.clear(false);
    return data[0];
}

// Encoder for generic notification indicators (Q.763 3.25)
static unsigned char encodeNotif(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!(param && val) || buf || param->size)
	return 0;
    unsigned char notif[32];
    unsigned int len = 0;
    ObjList* lst = val->split(',',false);
    ObjList* p = lst->skipNull();
    for (; p; p = p->skipNext()) {
	const String* s = static_cast<const String*>(p->get());
	int v = s->toInteger((const TokenDict*)param->data,-1);
	if (v < 0)
	    continue;
	notif[++len] = v & 0x7f;
	if (len >= sizeof(notif)-1)
	    break;
    }
    TelEngine::destruct(lst);
    DDebug(isup,DebugAll,"encodeNotif encoding %s on %u octets",param->name,len);
    if (!len)
	return 0;
    notif[len] |= 0x80;
    notif[0] = len & 0xff;
    DataBlock tmp(notif,len+1,false);
    msu += tmp;
    tmp.clear(false);
    return notif[0];
}

// Encoder for User Service Information (Q.763 3.57, Q.931)
static unsigned char encodeUSI(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!param)
	return 0;
    DataBlock tmp;
    SignallingUtils::encodeCaps(isup,tmp,*extra,prefix+param->name,true);
    DDebug(isup,DebugAll,"encodeUSI encoding %s on %u octets",param->name,tmp.length());
    if (tmp.length() < 1)
	return 0;
    msu += tmp;
    return tmp.length() - 1;
}

// Encoder for cause indicators
static unsigned char encodeCause(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!param)
	return 0;
    DataBlock tmp;
    SignallingUtils::encodeCause(isup,tmp,*extra,prefix+param->name,true);
    DDebug(isup,DebugAll,"encodeCause encoding %s on %u octets",param->name,tmp.length());
    if (tmp.length() < 1)
	return 0;
    msu += tmp;
    return tmp.length() - 1;
}

// Nature of Connection Indicators (Q.763 3.35)
static const SignallingFlags s_flags_naci[] = {
    // TODO: add more flags
    { 0x03, 0x00, "0sat" },
    { 0x03, 0x01, "1sat" },
    { 0x03, 0x02, "2sat" },
    { 0xc0, 0x00, "cont-check-none" },      // Continuity check not required
    { 0xc0, 0x40, "cont-check-this" },      // Continuity check required on this circuit
    { 0xc0, 0x80, "cont-check-prev" },      // Continuity check performed on a previous circuit
    { 0x10, 0x10, "echodev" },              // Outgoing half echo control device included
    { 0, 0, 0 }
};

// Forward Call Indicators (Q.763 3.23)
static const SignallingFlags s_flags_fwcallind[] = {
    { 0x0001, 0x0000, "national" },         // National/international call indicator
    { 0x0001, 0x0001, "international" },
    { 0x0006, 0x0000, "e2e-none" },         // End-to-end method indicator (none available: only link-by-link)
    { 0x0006, 0x0002, "e2e-pass" },         //   Pass along method available
    { 0x0006, 0x0004, "e2e-sccp" },         //   SCCP along method available
    { 0x0006, 0x0006, "e2e-pass-sccp" },    //   Pass along and SCCP method available
    { 0x0008, 0x0008, "interworking" },     // Interworking indicator (0: SS7 all the way)
    { 0x0010, 0x0010, "e2e-info" },         // End-to-end information available
    { 0x0020, 0x0020, "isup-path" },        // ISUP indicator (ISUP used all the way)
    { 0x00c0, 0x0000, "isup-pref" },        // ISUP preference indicator: preferred all the way
    { 0x00c0, 0x0040, "isup-notreq" },      //   not required all the way
    { 0x00c0, 0x0080, "isup-req" },         //   required all the way
    { 0x0100, 0x0100, "isdn-orig" },        // Originating from ISDN
    { 0x0600, 0x0000, "sccp-none" },        // SCCP method indicator: no indication
    { 0x0600, 0x0200, "sccp-less" },        //   connectionless method available
    { 0x0600, 0x0400, "sccp-conn" },        //   connection oriented method available
    { 0x0600, 0x0600, "sccp-less-conn" },   //   connectionless and connection oriented methods available
    { 0, 0, 0 }
};

// Backward Call Indicators (Q.763 3.5)
static const SignallingFlags s_flags_bkcallind[] = {
    { 0x0003, 0x0001, "no-charge" },        // Charge indicator
    { 0x0003, 0x0002, "charge" },
    { 0x000c, 0x0004, "called-free" },      // Called party's status indicator: subscriber free
    { 0x000c, 0x0008, "called-conn" },      // Called party's status indicator: connect when free
    { 0x0030, 0x0010, "called-ordinary" },  // Called party's category indicator: ordinary subscriber
    { 0x0030, 0x0020, "called-payphone" },  // Called party's category indicator: payphone
    { 0x00c0, 0x0000, "e2e-none" },         // End-to-end method indicator (none available: only link-by-link)
    { 0x00c0, 0x0040, "e2e-pass" },         //   Pass along method available
    { 0x00c0, 0x0080, "e2e-sccp" },         //   SCCP along method available
    { 0x00c0, 0x00c0, "e2e-pass-sccp" },    //   Pass along and SCCP method available
    { 0x0100, 0x0100, "interworking" },     // Interworking indicator (0: SS7 all the way)
    { 0x0200, 0x0200, "e2e-info" },         // End-to-end information available
    { 0x0400, 0x0400, "isup-path" },        // ISUP indicator (ISUP used all the way)
    { 0x0800, 0x0800, "hold-req" },         // Holding indicator: holding requested
    { 0x1000, 0x1000, "isdn-end" },         // Terminating in ISDN
    { 0x2000, 0x2000, "echodev" },          // Incoming half echo control device included
    { 0xc000, 0x0000, "sccp-none" },        // SCCP method indicator: no indication
    { 0xc000, 0x4000, "sccp-less" },        //   connectionless method available
    { 0xc000, 0x8000, "sccp-conn" },        //   connection oriented method available
    { 0xc000, 0xc000, "sccp-less-conn" },   //   connectionless and connection oriented methods available
    { 0, 0, 0 }
};

// Optional Forward Call Indicators (Q.763 3.38)
static const SignallingFlags s_flags_optfwcallind[] = {
    { 0x03, 0x00, "non-CUG" },
    { 0x03, 0x02, "CUG+out" },
    { 0x03, 0x03, "CUG" },
    { 0x04, 0x04, "segmentation" },         // Additional info will be sent in a segmentation message
    { 0x80, 0x80, "CLIR-requested" },
    { 0, 0, 0 }
};

// Optional Backward Call Indicators (Q.763 3.37)
static const SignallingFlags s_flags_optbkcallind[] = {
    { 0x01, 0x01, "inband" },
    { 0x02, 0x02, "diversion-possible" },
    { 0x04, 0x04, "segmentation" },         // Additional info will be sent in a segmentation message
    { 0x08, 0x08, "MLPP-user" },
    { 0, 0, 0 }
};

// Event Information (Q.763 3.21)
static const SignallingFlags s_flags_eventinfo[] = {
    { 0x7f, 0x01, "ringing" },
    { 0x7f, 0x02, "progress" },
    { 0x7f, 0x03, "inband" },
    { 0x7f, 0x04, "forward-busy" },
    { 0x7f, 0x05, "forward-noanswer" },
    { 0x7f, 0x06, "forward-always" },
    { 0x80, 0x80, "restricted" },
    { 0, 0, 0 }
};

// Continuity Indicators (Q.763 3.18)
static const SignallingFlags s_flags_continuity[] = {
    { 0x01, 0x00, "failed" },
    { 0x01, 0x01, "success" },
    { 0, 0, 0 }
};

// Group Supervision Type Indicator (Q.763 3.13)
static const SignallingFlags s_flags_grptypeind[] = {
    { 0x03, 0x00, "maintenance" },
    { 0x03, 0x01, "hw-failure" },
    { 0x03, 0x02, "national" },
    { 0, 0, 0 }
};

// Access Delivery Information (Q.763 3.2)
static const SignallingFlags s_flags_accdelinfo[] = {
    { 0x01, 0x01, "no-setup" },          // No Setup message generated (if 0: A Setup message was generated)
    { 0, 0, 0 }
};

// Calling Party Category (Q.763 3.11)
static TokenDict s_dict_callerCat[] = {
    { "unknown",     0 },                // calling party's category is unknown
    { "operator-FR", 1 },                // operator, language French
    { "operator-EN", 2 },                // operator, language English
    { "operator-DE", 3 },                // operator, language German
    { "operator-RU", 4 },                // operator, language Russian
    { "operator-ES", 5 },                // operator, language Spanish
    { "ordinary",   10 },                // ordinary calling subscriber
    { "priority",   11 },                // calling subscriber with priority
    { "data",       12 },                // data call (voice band data)
    { "test",       13 },                // test call
    { "payphone",   15 },                // payphone
    { 0, 0 }
};

// Transmission Medium Requirement (Q.763 3.54)
static TokenDict s_dict_mediumReq[] = {
    { "speech",          0 },
    { "64kbit",          2 },
    { "3.1khz-audio",    3 },
    { "64kb-preferred",  6 },
    { "2x64kbit",        7 },
    { "384kbit",         8 },
    { "1536kbit",        9 },
    { "1920kbit",       10 },
    { 0, 0 }
};

// Generic Notification Indicator (Q.763 3.25)
static TokenDict s_dict_notifications[] = {
    { "user-suspended",         0x00 },
    { "user-resumed",           0x01 },
    { "call-completion-delay",  0x04 },
    { "conf-established",       0x42 },
    { "conf-disconnected",      0x43 },
    { "party-added",            0x44 },
    { "isolated",               0x45 },
    { "reattached",             0x46 },
    { "party-isolated",         0x47 },
    { "party-reattached",       0x48 },
    { "party-split",            0x49 },
    { "party-disconnected",     0x4a },
    { "conf-floating",          0x4b },
    { "call-waiting",           0x60 },
    { "call-diversion",         0x68 },
    { "call-transfer-alerting", 0x69 },
    { "call-transfer-active",   0x6a },
    { "remote-hold",            0x79 },
    { "remote-retrieval",       0x7a },
    { "call-diverting",         0x7b },
    { 0, 0 }
};

#define MAKE_PARAM(p,s,a,d,t) { SS7MsgISUP::p,s,#p,a,d,t }
static const IsupParam s_paramDefs[] = {
//             name                          len decoder        encoder        table                  Q.763    Other
//                                                                                                     ref      ref
    MAKE_PARAM(AccessDeliveryInformation,      1,decodeFlags,   encodeFlags,   s_flags_accdelinfo),   // 3.2
    MAKE_PARAM(AccessTransport,                0,0,             0,             0),                    // 3.3
    MAKE_PARAM(AutomaticCongestionLevel,       1,decodeInt,     encodeInt,     0),                    // 3.4
    MAKE_PARAM(BackwardCallIndicators,         2,decodeFlags,   encodeFlags,   s_flags_bkcallind),    // 3.5
    MAKE_PARAM(CallDiversionInformation,       0,0,             0,             0),                    // 3.6
    MAKE_PARAM(CallHistoryInformation,         2,decodeInt,     encodeInt,     0),                    // 3.7
    MAKE_PARAM(CallReference,                  0,0,             0,             0),                    // 3.8
    MAKE_PARAM(CalledPartyNumber,              0,decodeDigits,  encodeDigits,  0),                    // 3.9
    MAKE_PARAM(CallingPartyNumber,             0,decodeDigits,  encodeDigits,  0),                    // 3.10
    MAKE_PARAM(CallingPartyCategory,           1,decodeInt,     encodeInt,     s_dict_callerCat),     // 3.11
    MAKE_PARAM(CauseIndicators,                0,decodeCause,   encodeCause,   0),                    // 3.12  Q.850-2.1
    MAKE_PARAM(GroupSupervisionTypeIndicator,  1,decodeFlags,   encodeFlags,   s_flags_grptypeind),   // 3.13
    MAKE_PARAM(CircuitStateIndicator,          0,0,             0,             0),                    // 3.14
    MAKE_PARAM(CUG_InterlockCode,              0,0,             0,             0),                    // 3.15
    MAKE_PARAM(ConnectedNumber,                0,decodeDigits,  encodeDigits,  0),                    // 3.16
    MAKE_PARAM(ConnectionRequest,              0,0,             0,             0),                    // 3.17
    MAKE_PARAM(ContinuityIndicators,           1,decodeFlags,   encodeFlags,   s_flags_continuity),   // 3.18
    MAKE_PARAM(EchoControlInformation,         0,0,             0,             0),                    // 3.19
    MAKE_PARAM(EventInformation,               1,decodeFlags,   encodeFlags,   s_flags_eventinfo),    // 3.21
    MAKE_PARAM(FacilityIndicator,              0,0,             0,             0),                    // 3.22
    MAKE_PARAM(ForwardCallIndicators,          2,decodeFlags,   encodeFlags,   s_flags_fwcallind),    // 3.23
    MAKE_PARAM(GenericDigits,                  0,0,             0,             0),                    // 3.24
    MAKE_PARAM(GenericNotification,            0,decodeNotif,   encodeNotif,   s_dict_notifications), // 3.25
    MAKE_PARAM(GenericNumber,                  0,0,             0,             0),                    // 3.26
    MAKE_PARAM(GenericReference,               0,0,             0,             0),                    // 3.27
    MAKE_PARAM(InformationIndicators,          0,0,             0,             0),                    // 3.28
    MAKE_PARAM(InformationRequestIndicators,   0,0,             0,             0),                    // 3.29
    MAKE_PARAM(LocationNumber,                 0,decodeDigits,  encodeDigits,  0),                    // 3.30
    MAKE_PARAM(MCID_RequestIndicator,          0,0,             0,             0),                    // 3.31
    MAKE_PARAM(MCID_ResponseIndicator,         0,0,             0,             0),                    // 3.32
    MAKE_PARAM(MessageCompatInformation,       0,decodeCompat,  0,             0),                    // 3.33
    MAKE_PARAM(NatureOfConnectionIndicators,   1,decodeFlags,   encodeFlags,   s_flags_naci),         // 3.35
    MAKE_PARAM(NetworkSpecificFacilities,      0,0,             0,             0),                    // 3.36
    MAKE_PARAM(OptionalBackwardCallIndicators, 1,decodeFlags,   encodeFlags,   s_flags_optbkcallind), // 3.37
    MAKE_PARAM(OptionalForwardCallIndicators,  1,decodeFlags,   encodeFlags,   s_flags_optfwcallind), // 3.38
    MAKE_PARAM(OriginalCalledNumber,           0,decodeDigits,  encodeDigits,  0),                    // 3.39
    MAKE_PARAM(OriginationISCPointCode,        0,0,             0,             0),                    // 3.40
    MAKE_PARAM(ParameterCompatInformation,     0,decodeCompat,  0,             0),                    // 3.41
    MAKE_PARAM(PropagationDelayCounter,        2,decodeInt,     encodeInt,     0),                    // 3.42
    MAKE_PARAM(RangeAndStatus,                 0,decodeRangeSt, encodeRangeSt, 0),                    // 3.43
    MAKE_PARAM(RedirectingNumber,              0,decodeDigits,  encodeDigits,  0),                    // 3.44
    MAKE_PARAM(RedirectionInformation,         0,0,             0,             0),                    // 3.45
    MAKE_PARAM(RedirectionNumber,              0,decodeDigits,  encodeDigits,  0),                    // 3.46
    MAKE_PARAM(RedirectionNumberRestriction,   0,0,             0,             0),                    // 3.47
    MAKE_PARAM(RemoteOperations,               0,0,             0,             0),                    // 3.48
    MAKE_PARAM(ServiceActivation,              0,0,             0,             0),                    // 3.49
    MAKE_PARAM(SignallingPointCode,            0,0,             0,             0),                    // 3.50
    MAKE_PARAM(SubsequentNumber,               0,decodeSubseq,  0,             0),                    // 3.51
    MAKE_PARAM(SuspendResumeIndicators,        0,0,             0,             0),                    // 3.52
    MAKE_PARAM(TransitNetworkSelection,        0,0,             0,             0),                    // 3.53
    MAKE_PARAM(TransmissionMediumRequirement,  1,decodeInt,     encodeInt,     s_dict_mediumReq),     // 3.54
    MAKE_PARAM(TransMediumRequirementPrime,    1,decodeInt,     encodeInt,     s_dict_mediumReq),     // 3.55
    MAKE_PARAM(TransmissionMediumUsed,         1,decodeInt,     encodeInt,     s_dict_mediumReq),     // 3.56
    MAKE_PARAM(UserServiceInformation,         0,decodeUSI,     encodeUSI,     0),                    // 3.57  Q.931-4.5.5
    MAKE_PARAM(UserServiceInformationPrime,    0,0,             0,             0),                    // 3.58
    MAKE_PARAM(UserTeleserviceInformation,     0,0,             0,             0),                    // 3.59
    MAKE_PARAM(UserToUserIndicators,           0,0,             0,             0),                    // 3.60
    MAKE_PARAM(UserToUserInformation,          0,0,             0,             0),                    // 3.61
    // No references
    MAKE_PARAM(ApplicationTransport,           0,0,             0,             0),                    //
    MAKE_PARAM(BusinessGroup,                  0,0,             0,             0),                    //
    MAKE_PARAM(CallModificationIndicators,     0,0,             0,             0),                    //
    MAKE_PARAM(CarrierIdentification,          0,0,             0,             0),                    //
    MAKE_PARAM(CircuitIdentificationName,      0,0,             0,             0),                    //
    MAKE_PARAM(CarrierSelectionInformation,    0,0,             0,             0),                    //
    MAKE_PARAM(ChargeNumber,                   0,0,             0,             0),                    //
    MAKE_PARAM(CircuitAssignmentMap,           0,0,             0,             0),                    //
    MAKE_PARAM(CircuitGroupCharactIndicator,   0,0,             0,             0),                    //
    MAKE_PARAM(CircuitValidationRespIndicator, 0,0,             0,             0),                    //
    MAKE_PARAM(CommonLanguage,                 0,0,             0,             0),                    //
    MAKE_PARAM(CUG_CheckResponseIndicators,    0,0,             0,             0),                    //
    MAKE_PARAM(Egress,                         0,0,             0,             0),                    //
    MAKE_PARAM(FacilityInformationIndicators,  0,0,             0,             0),                    //
    MAKE_PARAM(FreephoneIndicators,            0,0,             0,             0),                    //
    MAKE_PARAM(GenericName,                    0,0,             0,             0),                    //
    MAKE_PARAM(HopCounter,                     0,0,             0,             0),                    //
    MAKE_PARAM(Index,                          0,0,             0,             0),                    //
    MAKE_PARAM(Jurisdiction,                   0,0,             0,             0),                    //
    MAKE_PARAM(MLPP_Precedence,                0,0,             0,             0),                    //
    MAKE_PARAM(NetworkTransport,               0,0,             0,             0),                    //
    MAKE_PARAM(NotificationIndicator,          0,0,             0,             0),                    //
    MAKE_PARAM(OperatorServicesInformation,    0,0,             0,             0),                    //
    MAKE_PARAM(OriginatingLineInformation,     0,0,             0,             0),                    //
    MAKE_PARAM(OutgoingTrunkGroupNumber,       0,0,             0,             0),                    //
    MAKE_PARAM(Precedence,                     0,0,             0,             0),                    //
    MAKE_PARAM(ServiceCodeIndicator,           0,0,             0,             0),                    //
    MAKE_PARAM(SpecialProcessingRequest,       0,0,             0,             0),                    //
    MAKE_PARAM(TransactionRequest,             0,0,             0,             0),                    //
    { SS7MsgISUP::EndOfParameters, 0, 0, 0, 0, 0 }
};
#undef MAKE_PARAM

const char* getIsupParamName(unsigned char type)
{
   for (unsigned int i = 0; s_paramDefs[i].type; i++)
	if (type == s_paramDefs[i].type)
	    return s_paramDefs[i].name;
   return 0;
}

// Descriptor of ISUP message common across standards
static const MsgParams s_common_params[] = {
    // call progress and release messages
    { SS7MsgISUP::ACM, true,
	{
	    SS7MsgISUP::BackwardCallIndicators,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CON, true,
	{
	    SS7MsgISUP::BackwardCallIndicators,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::ANM, true,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::REL, true,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::CauseIndicators,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::RLC, true,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::SAM, true,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::SubsequentNumber,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CPR, true,
	{
	    SS7MsgISUP::EventInformation,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CNF, true,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::CauseIndicators,
	SS7MsgISUP::EndOfParameters
	}
    },
    // circuit group reset and acknowledgement
    { SS7MsgISUP::GRS, false,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::GRA, false,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	SS7MsgISUP::EndOfParameters
	}
    },
    // circuit group query
    { SS7MsgISUP::CQM, false,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	SS7MsgISUP::EndOfParameters
	}
    },
    // circuit group blocking, unblocking and acknowledgement
    { SS7MsgISUP::CGB, false,
	{
	    SS7MsgISUP::GroupSupervisionTypeIndicator,
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CGA, false,
	{
	    SS7MsgISUP::GroupSupervisionTypeIndicator,
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CGU, false,
	{
	    SS7MsgISUP::GroupSupervisionTypeIndicator,
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CUA, false,
	{
	    SS7MsgISUP::GroupSupervisionTypeIndicator,
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	SS7MsgISUP::EndOfParameters
	}
    },
    // circuit related messages - most without parameters, only CIC
    { SS7MsgISUP::BLK, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::BLA, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::UBL, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::UBA, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CCR, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::LPA, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::OLM, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::RSC, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::UEC, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::COT, false,
	{
	    SS7MsgISUP::ContinuityIndicators,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::Unknown, false, { SS7MsgISUP::EndOfParameters } }
};

// Descriptor of ITU-T version of ISUP messages
static const MsgParams s_itu_params[] = {
    { SS7MsgISUP::IAM, true,
	{
	    SS7MsgISUP::NatureOfConnectionIndicators,
	    SS7MsgISUP::ForwardCallIndicators,
	    SS7MsgISUP::CallingPartyCategory,
	    SS7MsgISUP::TransmissionMediumRequirement,
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::CalledPartyNumber,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::Unknown, false, { SS7MsgISUP::EndOfParameters } }
};

// Descriptor of ANSI version of ISUP messages
static const MsgParams s_ansi_params[] = {
    { SS7MsgISUP::IAM, true,
	{
	    SS7MsgISUP::NatureOfConnectionIndicators,
	    SS7MsgISUP::ForwardCallIndicators,
	    SS7MsgISUP::CallingPartyCategory,
		SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::UserServiceInformation,
	    SS7MsgISUP::CalledPartyNumber,
		SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::Unknown, false, { SS7MsgISUP::EndOfParameters } }
};

// Generic decode helper function for a single parameter
static bool decodeParam(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    DDebug(isup,DebugAll,"decodeParam(%p,%p,%p,%u) type=0x%02x, size=%u, name='%s'",
	&list,param,buf,len,param->type,param->size,param->name);
    if (param->size && (param->size != len))
	return false;
    if (param->decoder)
	return param->decoder(isup,list,param,buf,len,prefix);
    return decodeRaw(isup,list,param,buf,len,prefix);
}

// Generic encode helper function for a single mandatory parameter
static unsigned char encodeParam(const SS7ISUP* isup, SS7MSU& msu,
    const IsupParam* param, const NamedList* params, ObjList& exclude,
    const String& prefix, unsigned char* buf = 0)
{
    // variable length must not receive fixed buffer
    if (buf && !param->size)
	return 0;
    NamedString* val = params ? params->getParam(prefix+param->name) : 0;
    if (val)
	exclude.append(val)->setDelete(false);
    if (param->encoder)
	return param->encoder(isup,msu,buf,param,val,params,prefix);
    return encodeRaw(isup,msu,buf,param,val,params,prefix);
}

// Generic encode helper for a single optional parameter
static unsigned char encodeParam(const SS7ISUP* isup, SS7MSU& msu,
    const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    // add the parameter type now but remember the old length
    unsigned int len = msu.length();
    unsigned char tmp = param->type;
    msu.append(&tmp,1);

    unsigned char size = 0;
    if (param->encoder)
	size = param->encoder(isup,msu,0,param,val,extra,prefix);
    else
	size = encodeRaw(isup,msu,0,param,val,extra,prefix);
    if (!size) {
	Debug(isup,DebugMild,"Unwinding type storage for failed parameter %s",param->name);
	msu.truncate(len);
    }
    return size;
}

// Locate the description for a parameter by type
static const IsupParam* getParamDesc(SS7MsgISUP::Parameters type)
{
    const IsupParam* param = s_paramDefs;
    for (; param->type != SS7MsgISUP::EndOfParameters; param++) {
	if (param->type == type)
	    return param;
    }
    return 0;
}

// Locate the description for a parameter by name
static const IsupParam* getParamDesc(const String& name)
{
    const IsupParam* param = s_paramDefs;
    for (; param->type != SS7MsgISUP::EndOfParameters; param++) {
	if (name == param->name)
	    return param;
    }
    return 0;
}

// Locate the description table for a message according to protocol type
static const MsgParams* getIsupParams(SS7PointCode::Type type, SS7MsgISUP::Type msg)
{
    const MsgParams* params = 0;
    switch (type) {
	case SS7PointCode::ITU:
	    params = s_itu_params;
	    break;
	case SS7PointCode::ANSI:
	case SS7PointCode::ANSI8:
	    params = s_ansi_params;
	    break;
	default:
	    return 0;
    }
    // search first in specific table
    for (; params->type != SS7MsgISUP::Unknown; params++) {
	if (params->type == msg)
	    return params;
    }
    // then search in common table
    for (params = s_common_params; params->type != SS7MsgISUP::Unknown; params++) {
	if (params->type == msg)
	    return params;
    }
    return 0;
}

#define MAKE_NAME(x) { #x, SS7MsgISUP::x }
static const TokenDict s_names[] = {
    // this list must be kept in synch with the header
    MAKE_NAME(IAM),
    MAKE_NAME(SAM),
    MAKE_NAME(INR),
    MAKE_NAME(INF),
    MAKE_NAME(COT),
    MAKE_NAME(ACM),
    MAKE_NAME(CON),
    MAKE_NAME(FOT),
    MAKE_NAME(ANM),
    MAKE_NAME(REL),
    MAKE_NAME(SUS),
    MAKE_NAME(RES),
    MAKE_NAME(RLC),
    MAKE_NAME(CCR),
    MAKE_NAME(RSC),
    MAKE_NAME(BLK),
    MAKE_NAME(UBL),
    MAKE_NAME(BLA),
    MAKE_NAME(UBA),
    MAKE_NAME(GRS),
    MAKE_NAME(CGB),
    MAKE_NAME(CGU),
    MAKE_NAME(CGA),
    MAKE_NAME(CGBA), // alias
    MAKE_NAME(CUA),
    MAKE_NAME(CMR),
    MAKE_NAME(CMC),
    MAKE_NAME(CMRJ),
    MAKE_NAME(FACR),
    MAKE_NAME(FAA),
    MAKE_NAME(FRJ),
    MAKE_NAME(FAD),
    MAKE_NAME(FAI),
    MAKE_NAME(LPA),
    MAKE_NAME(CSVR),
    MAKE_NAME(CSVS),
    MAKE_NAME(DRS),
    MAKE_NAME(PAM),
    MAKE_NAME(GRA),
    MAKE_NAME(CQM),
    MAKE_NAME(CQR),
    MAKE_NAME(CPR),
    MAKE_NAME(CPG),  // alias
    MAKE_NAME(USR),
    MAKE_NAME(UEC),
    MAKE_NAME(UCIC), // alias
    MAKE_NAME(CNF),
    MAKE_NAME(OLM),
    MAKE_NAME(CRG),
    MAKE_NAME(NRM),
    MAKE_NAME(FAC),
    MAKE_NAME(UPT),
    MAKE_NAME(UPA),
    MAKE_NAME(IDR),
    MAKE_NAME(IRS),
    MAKE_NAME(SGM),
    MAKE_NAME(LOP),
    MAKE_NAME(APM),
    MAKE_NAME(PRI),
    MAKE_NAME(SDN),
    MAKE_NAME(CRA),
    MAKE_NAME(CRM),
    MAKE_NAME(CVR),
    MAKE_NAME(CVT),
    MAKE_NAME(EXM),
    { 0, 0 }
};
#undef MAKE_NAME

const TokenDict* SS7MsgISUP::names()
{
    return s_names;
}

void SS7MsgISUP::toString(String& dest, const SS7Label& label, bool params) const
{
    const char* enclose = "\r\n-----";
    dest = enclose;
    dest << "\r\n" << name() << " [cic=" << m_cic << " label=" << label << ']';
    if (params) {
	unsigned int n = m_params.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* s = m_params.getParam(i);
	    if (s)
		dest << "\r\n  " << s->name() << "='" << *s << "'";
	}
    }
    dest << enclose;
}

void* SS7MsgISUP::getObject(const String& name) const
{
    if (name == "SS7MsgISUP")
	return (void*)this;
    return SignallingMessage::getObject(name);
}


/**
 * Helper functions used to transmit responses
 */
// Push down the protocol stack a REL (Release) message
// @param cic The message CIC
// @param label The routing label for the message
// @param recvLbl True if the given label is from a received message. If true, a new routing
//   label will be created from the received one
// @param sls Signalling Link to use for the new routing label. Ignored if recvLbl is false
// @return Link the message was successfully queued to, negative for error
inline int transmitREL(SS7ISUP* isup, unsigned int cic, const SS7Label& label, bool recvLbl, int sls,
	const char* reason)
{
    SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::REL,cic);
    if (reason)
	m->params().addParam("CauseIndicators",reason);
    return isup->transmitMessage(m,label,recvLbl,sls);
}

// Push down the protocol stack a RLC (Release Complete) message
// @param msg Optional received message to copy release parameters. Ignored if reason is valid
inline int transmitRLC(SS7ISUP* isup, unsigned int cic, const SS7Label& label, bool recvLbl, int sls,
	const char* reason = 0, const SS7MsgISUP* msg = 0)
{
    SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::RLC,cic);
    if (reason && *reason)
	m->params().addParam("CauseIndicators",reason);
    else if (msg)
	m->params().copyParam(((SS7MsgISUP*)msg)->params(),"CauseIndicators",'.');
    else
	m->params().addParam("CauseIndicators","normal-clearing");
    return isup->transmitMessage(m,label,recvLbl,sls);
}

// Push down the protocol stack a CNF (Confusion) message
inline int transmitCNF(SS7ISUP* isup, unsigned int cic, const SS7Label& label, bool recvLbl, int sls,
	const char* reason)
{
    SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CNF,cic);
    if (reason)
	m->params().addParam("CauseIndicators",reason);
    return isup->transmitMessage(m,label,true,sls);
}


/**
 * SS7ISUPCall
 */
SS7ISUPCall::SS7ISUPCall(SS7ISUP* controller, SignallingCircuit* cic,
	const SS7PointCode& local, const SS7PointCode& remote, bool outgoing, int sls)
    : SignallingCall(controller,outgoing),
    m_state(Null),
    m_circuit(cic),
    m_terminate(false),
    m_gracefully(true),
    m_circuitChanged(false),
    m_iamMsg(0),
    m_sgmMsg(0),
    m_relTimer(300000),                  // Q.764: T5  - 5..15 minutes
    m_iamTimer(20000),                   // Q.764: T7  - 20..30 seconds
    m_sgmRecvTimer(3000)                 // Q.764: T34 - 2..4 seconds
{
    if (!(controller && m_circuit)) {
	Debug(isup(),DebugWarn,
	    "SS7ISUPCall(%u). No call controller or circuit. Terminate [%p]",
	    id(),this);
	setTerminate(true,m_circuit ? "temporary-failure" : "congestion");
	return;
    }
    isup()->setLabel(m_label,local,remote,sls);
    if (isup()->debugAt(DebugAll)) {
	String tmp;
	tmp << m_label;
	Debug(isup(),DebugAll,"Call(%u) direction=%s routing-label=%s [%p]",
	    id(),(outgoing ? "outgoing" : "incoming"),tmp.c_str(),this);
    }
}

SS7ISUPCall::~SS7ISUPCall()
{
    if (m_iamMsg)
	m_iamMsg->deref();
    releaseComplete(true);
    Debug(isup(),DebugAll,"Call(%u) destroyed with reason='%s' [%p]",
	id(),m_reason.safe(),this);
    if (controller())
	controller()->releaseCircuit(m_circuit);
}

// Stop waiting for a SGM (Segmentation) message when another message is
//  received by the controller
void SS7ISUPCall::stopWaitSegment(bool discard)
{
    Lock lock(m_callMutex);
    if (!m_sgmMsg)
	return;
    m_sgmRecvTimer.stop();
    if (discard) {
	m_sgmMsg->deref();
	m_sgmMsg = 0;
    }
}

// Helper functions called in getEvent
inline bool timeout(SS7ISUP* isup, SS7ISUPCall* call, SignallingTimer& timer,
	const Time& when, const char* req)
{
    if (!timer.timeout(when.msec()))
	return false;
    timer.stop();
    DDebug(isup,DebugNote,"Call(%u). %s request timed out [%p]",call->id(),req,call);
    return true;
}

// Get an event from this call
SignallingEvent* SS7ISUPCall::getEvent(const Time& when)
{
    Lock lock(m_callMutex);
    if (m_lastEvent || m_state == Released)
	return 0;
    SS7MsgISUP* msg = 0;
    while (true) {
	if (m_terminate) {
	    m_lastEvent = releaseComplete(false,0);
	    break;
	}
	// Check if waiting for SGM
	// Stop if: timeout, the controller stopped the timer or received a message other then SGM
	if (m_sgmMsg) {
	    msg = static_cast<SS7MsgISUP*>(dequeue(false));
	    if (!msg && !m_sgmRecvTimer.timeout(when.msec()) && m_sgmRecvTimer.started())
		return 0;
	    msg = ((msg && msg->type() == SS7MsgISUP::SGM) ? static_cast<SS7MsgISUP*>(dequeue()) : 0);
	    processSegmented(msg,m_sgmRecvTimer.timeout(when.msec()));
	    break;
	}
	// Process received messages
	msg = static_cast<SS7MsgISUP*>(dequeue());
	if (msg && validMsgState(false,msg->type()))
	    switch (msg->type()) {
		case SS7MsgISUP::IAM:
		case SS7MsgISUP::ACM:
		case SS7MsgISUP::CPR:
		case SS7MsgISUP::ANM:
		case SS7MsgISUP::CON:
		    m_sgmMsg = msg;
		    {
			const char* sgmParam = "OptionalBackwardCallIndicators";
			if (msg->type() == SS7MsgISUP::IAM) {
			    copyParamIAM(msg);
			    sgmParam = "OptionalForwardCallIndicators";
			}
			// Check segmentation. Keep the message and start timer if segmented
			if (SignallingUtils::hasFlag(msg->params(),sgmParam,"segmentation")) {
			    m_sgmRecvTimer.start(when.msec());
			    return 0;
			}
		    }
		    msg = 0;
		    processSegmented(0,false);
		    break;
		case SS7MsgISUP::SAM:
		    msg->params().addParam("tone",msg->params().getValue("SubsequentNumber"));
		    m_lastEvent = new SignallingEvent(SignallingEvent::Info,msg,this);
		    break;
		case SS7MsgISUP::RLC:
		    m_gracefully = false;
		case SS7MsgISUP::REL:
		    m_lastEvent = releaseComplete(false,msg);
		    break;
		case SS7MsgISUP::SGM:
		    DDebug(isup(),DebugInfo,"Call(%u). Received late 'SGM' [%p]",id(),this);
		    break;
		default: ;
		    Debug(isup(),DebugStub,"Call(%u). Unhandled '%s' message in getEvent() [%p]",
			id(),msg->name(),this);
	    }
	break;
    }
    if (msg)
	msg->deref();
    // No events: check timeouts
    if (!m_lastEvent) {
	switch (m_state) {
	    case Setup:
		if (timeout(isup(),this,m_iamTimer,when,"IAM"))
		    release();
		break;
	    case Releasing:
		if (timeout(isup(),this,m_relTimer,when,"REL"))
		    m_lastEvent = releaseComplete(false,0,"noresponse");
		break;
	    default: ;
	}
    }
    if (m_lastEvent)
	XDebug(isup(),DebugNote,"Call(%u). Raising event (%p,'%s') [%p]",
	    id(),m_lastEvent,m_lastEvent->name(),this);
    return m_lastEvent;
}

// Send an event to this call
bool SS7ISUPCall::sendEvent(SignallingEvent* event)
{
    Lock lock(m_callMutex);
    if (!event)
	return false;
    if (m_terminate || m_state == Released) {
	delete event;
	return false;
    }
    bool result = false;
    switch (event->type()) {
	case SignallingEvent::NewCall:
	    if (validMsgState(true,SS7MsgISUP::IAM)) {
		if (!event->message()) {
		    DDebug(isup(),DebugNote,
			"Call(%u). No parameters for outgoing call [%p]",id(),this);
		    setTerminate("temporary-failure");
		    break;
		}
		m_iamMsg = new SS7MsgISUP(SS7MsgISUP::IAM,id());
		copyParamIAM(m_iamMsg,true,event->message());
		result = transmitIAM();
	    }
	    break;
	case SignallingEvent::Progress:
	case SignallingEvent::Ringing:
	    if (validMsgState(true,SS7MsgISUP::CPR)) {
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CPR,id());
		m->params().addParam("EventInformation",
		    event->type() == SignallingEvent::Ringing ? "ringing": "progress");
		m_state = Ringing;
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Accept:
	    if (validMsgState(true,SS7MsgISUP::ACM)) {
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::ACM,id());
		if (event->message())
		    m->params().addParam("BackwardCallIndicators",
			event->message()->params().getValue("BackwardCallIndicators"));
		m_state = Accepted;
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Answer:
	    if (validMsgState(true,SS7MsgISUP::ANM)) {
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::ANM,id());
		m_state = Answered;
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Release:
	    if (validMsgState(true,SS7MsgISUP::REL))
		result = release(event);
	    break;
	case SignallingEvent::Info:
	//case SignallingEvent::Message:
	//case SignallingEvent::Transfer:
	default:
	    DDebug(isup(),DebugStub,
		"Call(%u). sendEvent not implemented for '%s' [%p]",
		id(),event->name(),this);
    }
    XDebug(isup(),DebugAll,"Call(%u). Event (%p,'%s') sent. Result: %s [%p]",
	id(),event,event->name(),String::boolText(result),this);
    delete event;
    return result;
}

// Get reserved circuit or this object
void* SS7ISUPCall::getObject(const String& name) const
{
    if (name == "SignallingCircuit")
	return m_circuit;
    if (name == "SS7ISUPCall")
	return (void*)this;
    return SignallingCall::getObject(name);
}

// Replace the circuit reserved for this call. Release the already reserved circuit.
// Retransmit the initial IAM request on success.
// On failure set the termination flag and release the new circuit if valid
bool SS7ISUPCall::replaceCircuit(SignallingCircuit* circuit)
{
    Lock lock(m_callMutex);
    clearQueue();
    if (m_state > Setup || !circuit || !outgoing()) {
	m_iamTimer.stop();
	if (controller()) {
	    controller()->releaseCircuit(m_circuit);
	    controller()->releaseCircuit(circuit);
	}
	setTerminate(false,"normal");
	return false;
    }
    unsigned int oldId = id();
    if (controller())
	controller()->releaseCircuit(m_circuit);
    Debug(isup(),DebugNote,"Call(%u). Circuit replaced by %u [%p]",oldId,id(),this);
    m_circuit = circuit;
    m_circuitChanged = true;
    transmitIAM();
    return true;
}

// Stop timers. Send a RLC (Release Complete) message if it should terminate gracefully
// Decrease the object's refence count and generate a Release event if not final
// @param final True if called from destructor
// @param msg Received message with parameters if any
// @param reason Optional release reason
SignallingEvent* SS7ISUPCall::releaseComplete(bool final, SS7MsgISUP* msg, const char* reason)
{
    m_relTimer.stop();
    m_iamTimer.stop();
    setReason(reason,msg);
    stopWaitSegment(true);
    if (m_state == Released)
	return 0;
    if (isup() && m_gracefully) {
	int sls = transmitRLC(isup(),id(),m_label,false,m_label.sls(),m_reason);
	if (sls != -1)
	    m_label.setSls(sls);
    }
    m_state = Released;
    if (final)
	return 0;
    // Return event and decrease reference counter
    bool create = (msg == 0);
    if (create)
	msg = new SS7MsgISUP(SS7MsgISUP::RLC,id());
    if (m_circuit)
	m_circuit->disconnect();
    msg->params().setParam("reason",m_reason);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Release,msg,this);
    // deref() msg if created here. If received, it will be deref()'d in getEvent()
    if (create)
	msg->deref();
    deref();
    DDebug(isup(),DebugInfo,"Call(%u). Released with reason '%s' [%p]",
	id(),m_reason.safe(),this);
    return event;
}

// Helper function to copy parameters
inline void param(NamedList& dest, NamedList& src, const char* param,
	const char* srcParam, const char* defVal)
{
    dest.addParam(param,src.getValue(srcParam,src.getValue(param,defVal)));
}

// Initialize/set IAM message parameters
// @param msg Valid ISUP message
// @param outgoing Message direction: true for outgoing
// @param sigMsg Valid signalling message with parameters if outgoing
bool SS7ISUPCall::copyParamIAM(SS7MsgISUP* msg, bool outgoing, SignallingMessage* sigMsg)
{
    NamedList& dest = msg->params();
    if (outgoing) {
	NamedList& src = sigMsg->params();
	param(dest,src,"CalledPartyNumber","called","");
	param(dest,src,"CalledPartyNumber.inn","inn",String::boolText(isup()->m_inn));
	param(dest,src,"CalledPartyNumber.nature","callednumtype",isup()->m_numType);
	param(dest,src,"CalledPartyNumber.plan","callednumplan",isup()->m_numPlan);
	param(dest,src,"CallingPartyCategory","callercategory",isup()->m_callerCat);
	dest.copyParam(src,"ForwardCallIndicators");
	param(dest,src,"CallingPartyNumber","caller","");
	param(dest,src,"CallingPartyNumber.nature","callernumtype",isup()->m_numType);
	param(dest,src,"CallingPartyNumber.plan","callernumplan",isup()->m_numPlan);
	param(dest,src,"CallingPartyNumber.restrict","callerpres",isup()->m_numPresentation);
	param(dest,src,"CallingPartyNumber.screened","callerscreening",isup()->m_numScreening);
	param(dest,src,"CallingPartyNumber.complete","complete","true");
	m_format = src.getValue("format",isup()->format());
	dest.addParam("UserServiceInformation",m_format);
	return true;
    }
    // Incoming call
    m_format = dest.getValue("UserServiceInformation",isup()->format());
    dest.setParam("format",m_format);
    dest.setParam("caller",dest.getValue("CallingPartyNumber"));
    //dest.setParam("callername",dest.getValue(""));
    dest.setParam("callernumtype",dest.getValue("CallingPartyNumber.nature"));
    dest.setParam("callernumplan",dest.getValue("CallingPartyNumber.plan"));
    dest.setParam("callerpres",dest.getValue("CallingPartyNumber.restrict"));
    dest.setParam("callerscreening",dest.getValue("CallingPartyNumber.screened"));
    dest.setParam("called",dest.getValue("CalledPartyNumber"));
    dest.setParam("callednumtype",dest.getValue("CalledPartyNumber.nature"));
    dest.setParam("callednumplan",dest.getValue("CalledPartyNumber.plan"));
    dest.setParam("inn",dest.getValue("CalledPartyNumber.inn"));
    return true;
}

// If already releasing, set termination flag. Otherwise, send REL (Release) message
// @param event Event with the parameters. 0 if release is started on some timeout
bool SS7ISUPCall::release(SignallingEvent* event)
{
    m_iamTimer.stop();
    if (event)
	setReason(0,event->message());
    else
	setReason("noresponse",0);
    stopWaitSegment(true);
    XDebug(isup(),DebugAll,"Call(%u). Releasing call with reason '%s' [%p]",
	id(),m_reason.safe(),this);
    if (!isup() || m_state >= Releasing) {
	m_terminate = true;
	return false;
    }
    m_relTimer.start();
    m_state = Releasing;
    if (!isup())
	return 0;
    int sls = transmitREL(isup(),id(),m_label,false,m_label.sls(),m_reason);
    if (sls != -1)
	m_label.setSls(sls);
    return sls != -1;
}

// Set termination reason from received text or message
void SS7ISUPCall::setReason(const char* reason, SignallingMessage* msg)
{
    if (!m_reason.null())
	return;
    if (reason)
	m_reason = reason;
    else if (msg)
	m_reason = msg->params().getValue("CauseIndicators",msg->params().getValue("reason"));
}

// Accept send/receive messages in current state based on call direction
bool SS7ISUPCall::validMsgState(bool send, SS7MsgISUP::Type type)
{
    bool handled = true;
    switch (type) {
	case SS7MsgISUP::IAM:    // Initial address
	    if (m_state != Null || send != outgoing())
		break;
	    return true;
	case SS7MsgISUP::ACM:    // Address complete
	    if (m_state != Setup || send == outgoing())
		break;
	    return true;
	case SS7MsgISUP::CPR:    // Call progress
	    if (m_state < Accepted || m_state >= Releasing)
		break;
	    return true;
	case SS7MsgISUP::ANM:    // Answer
	case SS7MsgISUP::CON:    // Connect
	    if (m_state < Accepted || m_state >= Answered || send == outgoing())
		break;
	    return true;
	case SS7MsgISUP::SAM:    // Subsequent address
	    if (m_state != Setup)
		break;
	    return true;
	case SS7MsgISUP::REL:    // Release
	case SS7MsgISUP::RLC:    // Release complete
	    if (m_state == Null || m_state == Released)
		break;
	    return true;
	case SS7MsgISUP::SGM:    // Segmentation
	    return true;
	default:
	    handled = false;
    }
    Debug(isup(),handled?DebugNote:DebugStub,
	"Call(%u). Can't %s %smessage '%s' in state %u [%p]",
	id(),send?"send":"accept",handled?"":"unhandled ",
	SS7MsgISUP::lookup(type,""),m_state,this);
    return false;
}

// Connect the reserved circuit. Return false if it fails.
// Return true if this call is a signalling only one
bool SS7ISUPCall::connectCircuit()
{
    if (signalOnly())
	return true;
    if (m_circuit && (m_circuit->status() == SignallingCircuit::Connected ||
	m_circuit->connect(m_format)))
	return true;
    Debug(isup(),DebugMild,"Call(%u). Circuit connect failed (format='%s')%s [%p]",
	id(),m_format.safe(),m_circuit?"":". No circuit",this);
    return false;
}

// Transmit the IAM message. Start IAM timer if not started
bool SS7ISUPCall::transmitIAM()
{
    if (!m_iamTimer.started())
	m_iamTimer.start();
    m_state = Setup;
    if (!m_iamMsg)
	return false;
    m_iamMsg->m_cic = id();
    m_iamMsg->ref();
    return transmitMessage(m_iamMsg);
}

// Stop waiting for a SGM (Segmentation) message. Copy parameters to
// the pending segmented message if sgm is valid.
// Change call state and set m_lastEvent
SignallingEvent* SS7ISUPCall::processSegmented(SS7MsgISUP* sgm, bool timeout)
{
    if (sgm)
	if (sgm->type() == SS7MsgISUP::SGM) {
	    // Copy parameters from SGM as defined in Q.763 - Table 49
	    // (Segmentation message) and Q.764 - 2.1.12 (re-assembly)
	    #define COPY_PARAM(param) \
		m_sgmMsg->params().copyParam(sgm->params(),param); \
		m_sgmMsg->params().copyParam(sgm->params(),param,'.');
	    COPY_PARAM("AccessTranport")
	    COPY_PARAM("UserToUserInformation")
	    COPY_PARAM("MessageCompatInformation")
	    COPY_PARAM("GenericDigits")
	    COPY_PARAM("GenericNotification")
	    COPY_PARAM("GenericNumber")
	    #undef COPY_PARAM
	}
	else
	    Debug(isup(),DebugStub,"Call(%u). stopWaitSegment() called with non-SGM message !!! [%p]",
		id(),this);
    else if (timeout)
	Debug(isup(),DebugMild,"Call(%u). Segment waiting message '%s' timed out [%p]",
	    id(),m_sgmMsg->name(),this);
    m_sgmRecvTimer.stop();
    // Raise event, connect the reserved circuit, change call state
    m_iamTimer.stop();
    connectCircuit();
    if (m_circuitChanged) {
	m_sgmMsg->params().setParam("circuit-change","true");
	m_circuitChanged = false;
    }
    m_sgmMsg->params().setParam("format",m_format);
    switch (m_sgmMsg->type()) {
	case SS7MsgISUP::IAM:
	    m_state = Setup;
	    m_lastEvent = new SignallingEvent(SignallingEvent::NewCall,m_sgmMsg,this);
	    break;
	case SS7MsgISUP::ACM:
	    m_state = Accepted;
	    m_lastEvent = new SignallingEvent(SignallingEvent::Accept,m_sgmMsg,this);
	    break;
	case SS7MsgISUP::CPR:
	    m_state = Ringing;
	    m_lastEvent = new SignallingEvent(SignallingEvent::Ringing,m_sgmMsg,this);
	    break;
	case SS7MsgISUP::ANM:
	case SS7MsgISUP::CON:
	    m_state = Answered;
	    m_lastEvent = new SignallingEvent(SignallingEvent::Answer,m_sgmMsg,this);
	    break;
	default:
	    Debug(isup(),DebugStub,"Call(%u). Segment waiting message is '%s' [%p]",
		id(),m_sgmMsg->name(),this);
    }
    m_sgmMsg->deref();
    m_sgmMsg = 0;
    return m_lastEvent;
}

// Transmit message. Set routing label's link if not already set
bool SS7ISUPCall::transmitMessage(SS7MsgISUP* msg)
{
    int sls = isup()->transmitMessage(msg,m_label,false);
    if (sls == -1)
	return false;
    m_label.setSls(sls);
    return true;
}

SS7ISUP* SS7ISUPCall::isup()
{
    return static_cast<SS7ISUP*>(SignallingCall::controller());
}


/**
 * SS7ISUP
 */
SS7ISUP::SS7ISUP(const NamedList& params)
    : SignallingCallControl(params,"isup."),
    m_cicLen(2),
    m_type(SS7PointCode::Other),
    m_defPoint(0),
    m_remotePoint(0),
    m_priossf(0),
    m_sls(255),
    m_inn(false),
    m_rscTimer(0),
    m_rscCic(0)
{
    setName(params.getValue("debugname","isup"));

    const char* stype = params.getValue("pointcodetype");
    m_type = SS7PointCode::lookup(stype);
    if (m_type == SS7PointCode::Other) {
	Debug(this,DebugWarn,"Invalid point code type '%s'",c_safe(stype));
	return;
    }

    m_format = params.getValue("format");
    if (-1 == lookup(m_format,SignallingUtils::dict(0,0),-1))
	m_format = "alaw";

    const char* rpc = params.getValue("remotepointcode");
    m_remotePoint = new SS7PointCode(0,0,0);
    if (!(m_remotePoint->assign(rpc) && m_remotePoint->pack(m_type))) {
	Debug(this,DebugMild,"Invalid remotepointcode='%s'",rpc);
	TelEngine::destruct(m_remotePoint);
    }

    m_priossf |= SS7MSU::getPriority(params.getValue("priority"),SS7MSU::Regular);
    m_priossf |= SS7MSU::getNetIndicator(params.getValue("netindicator"),SS7MSU::National);

    m_inn = params.getBoolValue("inn",m_inn);
    m_numPlan = params.getValue("numplan");
    if (-1 == lookup(m_numPlan,s_dict_numPlan,-1))
	m_numPlan = "unknown";
    m_numType = params.getValue("numtype");
    if (-1 == lookup(m_numType,s_dict_nai,-1))
	m_numType = "unknown";
    m_numPresentation = params.getValue("presentation");
    if (-1 == lookup(m_numPresentation,s_dict_presentation,-1))
	m_numPresentation = "allowed";
    m_numScreening = params.getValue("screening");
    if (-1 == lookup(m_numScreening,s_dict_screening,-1))
	m_numScreening = "user-provided";
    m_callerCat = params.getValue("callercategory");
    if (-1 == lookup(m_callerCat,s_dict_callerCat,-1))
	m_callerCat = "ordinary";

    m_rscTimer.interval(params,"channelsync",60,1000,true,true);

    if (debugAt(DebugInfo)) {
	String s;
	s << "pointcode-type=" << stype;
	s << " format=" << m_format;
	s << " plan/type/pres/screen=" << m_numPlan << "/" << m_numType << "/"
	    << m_numPresentation << "/" << m_numScreening;
	s << " caller-category=" << m_callerCat;
	s << " remote-pointcode=";
	if (m_remotePoint)
	    s << *m_remotePoint;
	else
	    s << "missing";
	s << " priority+SSF=" << (unsigned int)m_priossf;
	Debug(this,DebugInfo,"ISUP Call Controller %s [%p]",s.c_str(),this);
    }
}

SS7ISUP::~SS7ISUP()
{
    cleanup();
    if (m_remotePoint)
	m_remotePoint->destruct();
    Debug(this,DebugInfo,"ISUP Call Controller destroyed [%p]",this);
}

// Append a point code to the list of point codes serviced by this controller
// Set default point code
bool SS7ISUP::setPointCode(SS7PointCode* pc, bool def)
{
    if (!(pc && pc->pack(m_type)))
	return false;
    Lock lock(this);
    // Force default if we are not having one or the list is empty
    def = def || !m_defPoint || !m_pointCodes.skipNull();
    // Force not default if the received point code is the same as the default one
    if (def && m_defPoint && *m_defPoint == *pc)
	def = false;
    SS7PointCode* p = hasPointCode(*pc);
    if (def)
	m_defPoint = p ? p : pc;
    String tmp;
    tmp << (def ? *m_defPoint : *pc);
    if (!p) {
	m_pointCodes.append(pc);
	DDebug(this,DebugAll,"Added new point code '%s'%s",tmp.safe(),def?". Set to default":"");
    }
    else {
	TelEngine::destruct(pc);
	if (def)
	    Debug(this,DebugAll,"Set default point code '%s'",tmp.safe());
    }
    return true;
}

// Check if the given point code is serviced by this controller
SS7PointCode* SS7ISUP::hasPointCode(const SS7PointCode& pc)
{
    Lock lock(this);
    for (ObjList* o = m_pointCodes.skipNull(); o; o = o->skipNext()) {
	SS7PointCode* p = static_cast<SS7PointCode*>(o->get());
	if (*p == pc)
	    return p;
    }
    return 0;
}

SS7MSU* SS7ISUP::createMSU(SS7MsgISUP::Type type, unsigned char ssf,
    const SS7Label& label, unsigned int cic, const NamedList* params) const
{
    return buildMSU(type,SS7MSU::ISUP | (ssf & 0xf0),label,cic,params);
}

// Make an outgoing call
SignallingCall* SS7ISUP::call(SignallingMessage* msg, String& reason)
{
    if (!msg) {
	reason = "invalid-parameter";
	return 0;
    }
    SS7PointCode dest;
    SignallingCircuit* cic = 0;
    Lock lock(this);
    // Check
    while (true) {
	if (!m_defPoint) {
 	    Debug(this,DebugNote,"Source point code is missing");
	    reason = "noconn";
	    break;
	}
	String pc = msg->params().getValue("calledpointcode");
	if (!(dest.assign(pc) && dest.pack(m_type))) {
	    if (!m_remotePoint) {
		Debug(this,DebugNote,
		    "Destination point code is missing (calledpointcode=%s)",pc.safe());
		reason = "noconn";
		break;
	    }
	    dest = *m_remotePoint;
	}
	if (!reserveCircuit(cic)) {
	    Debug(this,DebugNote,"Can't reserve circuit");
	    reason = "congestion";
	    break;
	}
	break;
    }
    SignallingCall* call = 0;
    if (reason.null()) {
	call = new SS7ISUPCall(this,cic,*m_defPoint,dest,true);
	call->ref();
	m_calls.append(call);
	SignallingEvent* event = new SignallingEvent(SignallingEvent::NewCall,msg,call);
	call->sendEvent(event);
	// (re)start RSC timer if not currently reseting
	if (!m_rscCic && m_rscTimer.interval())
	    m_rscTimer.start();
    }
    msg->deref();
    return call;
}

// Converts an ISUP message to a Message Signal Unit and push it down the protocol stack
// The given message is consumed
int SS7ISUP::transmitMessage(SS7MsgISUP* msg, const SS7Label& label, bool recvLbl, int sls)
{
    if (!msg)
	return -1;
    const SS7Label* p = &label;
    SS7Label tmp;
    if (recvLbl) {
	tmp.assign(label.type(),label.opc(),label.dpc(),sls,label.spare());
	p = &tmp;
    }
    if (debugAt(DebugInfo)) {
	String tmp;
	msg->toString(tmp,*p,debugAt(DebugAll));
	Debug(this,DebugInfo,"Sending message (%p)%s",msg,tmp.c_str());
    }
    SS7MSU* msu = createMSU(msg->type(),m_priossf,*p,msg->cic(),&msg->params());
    sls = -1;
    if (msu) {
	sls = transmitMSU(*msu,*p,p->sls());
	TelEngine::destruct(msu);
    }
    if (sls == -1)
	XDebug(this,DebugMild,"Failed to send message (%p): '%s'",msg,msg->name());
    if (m_sls == 255)
	m_sls = (unsigned char)sls;
    msg->deref();
    return sls;
}

void SS7ISUP::cleanup(const char* reason)
{
    lock();
    for (ObjList* o = m_calls.skipNull(); o; o = o->skipNext()) {
	SS7ISUPCall* call = static_cast<SS7ISUPCall*>(o->get());
	call->setTerminate(true,reason);
    }
    releaseCircuit(m_rscCic);
    m_rscTimer.stop();
    unlock();
    clearCalls();
}

// Get a pointer to this object or other data
void* SS7ISUP::getObject(const String& name) const
{
    if (name == "SS7ISUP")
	return (void*)this;
    return SS7Layer4::getObject(name);
}

// Remove all links with other layers. Disposes the memory
void SS7ISUP::destruct()
{
    lock();
    clearCalls();
    unlock();
    SignallingCallControl::attach(0);
    SS7Layer4::attach(0);
    SS7Layer4::destruct();
}

void SS7ISUP::timerTick(const Time& when)
{
    Lock lock(this);
    // Circuit reset
    while (circuits()) {
	// Disabled ?
	if (!m_rscTimer.interval())
	    break;
	if (m_rscTimer.started()) {
	    if (!m_rscTimer.timeout(when.msec()))
		break;
	    m_rscTimer.stop();
	    if (m_rscCic) {
		Debug(this,DebugMild,"Circuit reset timed out for cic=%u",m_rscCic->code());
		releaseCircuit(m_rscCic);
		break;
	    }
	}
	m_rscTimer.start(when.msec());
	// Pick the next circuit to reset. Ignore lock flags
	int flags = SignallingCircuit::LockLocal | SignallingCircuit::LockRemote;
	if (m_defPoint && m_remotePoint && reserveCircuit(m_rscCic,~flags)) {
	    SS7MsgISUP* msg = new SS7MsgISUP(SS7MsgISUP::RSC,m_rscCic->code());
	    SS7Label label(m_type,*m_remotePoint,*m_defPoint,m_sls);
	    transmitMessage(msg,label,false);
	}
	break;
    }
}

// Process a notification generated by the attached network layer
void SS7ISUP::notify(SS7Layer3* link, int sls)
{
    if (!link)
	return;
    DDebug(this,DebugInfo,"L3 (%p,'%s') is %soperational",link,
	link->toString().safe(),link->operational()?"":"not ");
}

SS7MSU* SS7ISUP::buildMSU(SS7MsgISUP::Type type, unsigned char sio,
    const SS7Label& label, unsigned int cic, const NamedList* params) const
{
    // see what mandatory parameters we should put in this message
    const MsgParams* msgParams = getIsupParams(label.type(),type);
    if (!msgParams) {
	const char* name = SS7MsgISUP::lookup(type);
	if (name)
	    Debug(this,DebugWarn,"No parameter table for ISUP MSU type %s [%p]",name,this);
	else
	    Debug(this,DebugWarn,"Cannot create ISUP MSU type 0x%02x [%p]",type,this);
	return 0;
    }
    unsigned int len = m_cicLen + 1;

    const SS7MsgISUP::Parameters* plist = msgParams->params;
    SS7MsgISUP::Parameters ptype;
    // first add the length of mandatory fixed parameters
    while ((ptype = *plist++) != SS7MsgISUP::EndOfParameters) {
	const IsupParam* param = getParamDesc(ptype);
	if (!param) {
	    // this is fatal as we don't know the length
	    Debug(this,DebugGoOn,"Missing description of fixed ISUP parameter 0x%02x [%p]",ptype,this);
	    return 0;
	}
	if (!param->size) {
	    Debug(this,DebugGoOn,"Invalid (variable) description of fixed ISUP parameter 0x%02x [%p]",ptype,this);
	    return 0;
	}
	len += param->size;
    }
    // initialize the pointer array offset just past the mandatory fixed part
    unsigned int ptr = label.length() + 1 + len;
    // then add one pointer octet to each mandatory variable parameter
    while ((ptype = *plist++) != SS7MsgISUP::EndOfParameters) {
	const IsupParam* param = getParamDesc(ptype);
	if (!param) {
	    // this is fatal as we won't be able to populate later
	    Debug(this,DebugGoOn,"Missing description of variable ISUP parameter 0x%02x [%p]",ptype,this);
	    return 0;
	}
	if (param->size)
	    Debug(this,DebugMild,"Invalid (fixed) description of variable ISUP parameter 0x%02x [%p]",ptype,this);
	len++;
    }
    // finally add a pointer to the optional part only if supported by type
    if (msgParams->optional)
	len++;
    SS7MSU* msu = new SS7MSU(sio,label,0,len);
    unsigned char* d = msu->getData(label.length()+1,len);
    unsigned int i = m_cicLen;
    while (i--) {
	*d++ = cic & 0xff;
	cic >>= 8;
    }
    *d++ = type;
    ObjList exclude;
    plist = msgParams->params;
    String prefix = params->getValue("message-prefix");
    // first populate with mandatory fixed parameters
    while ((ptype = *plist++) != SS7MsgISUP::EndOfParameters) {
	const IsupParam* param = getParamDesc(ptype);
	if (!param) {
	    Debug(this,DebugFail,"Stage 2: no description of fixed ISUP parameter 0x%02x [%p]",ptype,this);
	    continue;
	}
	if (!param->size) {
	    Debug(this,DebugFail,"Stage 2: Invalid (variable) description of fixed ISUP parameter %s [%p]",param->name,this);
	    continue;
	}
	if (!encodeParam(this,*msu,param,params,exclude,prefix,d))
	    Debug(this,DebugGoOn,"Could not encode fixed ISUP parameter %s [%p]",param->name,this);
	d += param->size;
    }
    // now populate with mandatory variable parameters
    for (; (ptype = *plist++) != SS7MsgISUP::EndOfParameters; ptr++) {
	const IsupParam* param = getParamDesc(ptype);
	if (!param) {
	    Debug(this,DebugFail,"Stage 2: no description of variable ISUP parameter 0x%02x [%p]",ptype,this);
	    continue;
	}
	if (param->size) {
	    Debug(this,DebugFail,"Stage 2: Invalid (fixed) description of variable ISUP parameter %s [%p]",param->name,this);
	    continue;
	}
	// remember the offset this parameter will actually get stored
	len = msu->length();
	unsigned char size = encodeParam(this,*msu,param,params,exclude,prefix);
	d = msu->getData(0,len+1);
	if (!(size && d)) {
	    Debug(this,DebugGoOn,"Could not encode variable ISUP parameter %s [%p]",param->name,this);
	    continue;
	}
	if ((d[len] != size) || (msu->length() != (len+1+size))) {
	    Debug(this,DebugGoOn,"Invalid encoding variable ISUP parameter %s (len=%u size=%u stor=%u) [%p]",
		param->name,len,size,d[len],this);
	    continue;
	}
	// store pointer to parameter
	d[ptr] = len - ptr;
    }
    if (msgParams->optional && params) {
	// remember the offset past last mandatory == first optional parameter
	len = msu->length();
	// optional parameters are possible - try to set anything left in the message
	unsigned int n = params->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params->getParam(i);
	    if (!ns || exclude.find(ns))
		continue;
	    const IsupParam* param = getParamDesc(ns->name());
	    if (!param)
		continue;
	    unsigned char size = encodeParam(this,*msu,param,ns,params,prefix);
	    if (!size)
		continue;
	    if (len) {
		d = msu->getData(0,len+1);
		d[ptr] = len - ptr;
		len = 0;
	    }
	}
	if (!len) {
	    // we stored some optional parameters so we need to put the terminator
	    DataBlock tmp(0,1);
	    *msu += tmp;
	}
    }
    return msu;
}

// Decode a buffer to a list of parameters
bool SS7ISUP::decodeMessage(NamedList& msg,
    SS7MsgISUP::Type msgType, SS7PointCode::Type pcType,
    const unsigned char* paramPtr, unsigned int paramLen)
{
#ifdef XDEBUG
    String tmp;
    tmp.hexify((void*)paramPtr,paramLen,' ');
    Debug(this,DebugAll,"Decoding msg=%s len=%u: %s [%p]",
	SS7MsgISUP::lookup(msgType),paramLen,tmp.c_str(),this);
#else
    DDebug(this,DebugAll,"Decoding msg=%s len=%u [%p]",
	SS7MsgISUP::lookup(msgType),paramLen,this);
#endif

    // see what parameters we expect for this message
    const MsgParams* params = getIsupParams(pcType,msgType);
    if (!params) {
	Debug(this,DebugGoOn,"Invalid point code or message type [%p]",this);
	return false;
    }

    // Get parameter prefix
    String prefix = msg.getValue("message-prefix");

    // Add protocol and message type
    switch (pcType) {
	case SS7PointCode::ITU:
	    msg.addParam(prefix+"protocol-type","itu-t");
	    break;
	case SS7PointCode::ANSI:
	case SS7PointCode::ANSI8:
	    msg.addParam(prefix+"protocol-type","ansi");
	    break;
	default: ;
    }
    msg.addParam(prefix+"message-type",SS7MsgISUP::lookup(msgType));

    const SS7MsgISUP::Parameters* plist = params->params;
    SS7MsgISUP::Parameters ptype;
    // first decode any mandatory fixed parameters the message should have
    while ((ptype = *plist++) != SS7MsgISUP::EndOfParameters) {
	const IsupParam* param = getParamDesc(ptype);
	if (!param) {
	    // this is fatal as we don't know the length
	    Debug(this,DebugGoOn,"Missing description of fixed ISUP parameter 0x%02x [%p]",ptype,this);
	    return false;
	}
	if (!param->size) {
	    Debug(this,DebugGoOn,"Invalid (variable) description of fixed ISUP parameter %s [%p]",param->name,this);
	    return false;
	}
	if (paramLen < param->size) {
	    Debug(this,DebugWarn,"Truncated ISUP message! [%p]",this);
	    return false;
	}
	if (!decodeParam(this,msg,param,paramPtr,param->size,prefix))
	    Debug(this,DebugWarn,"Could not decode fixed ISUP parameter %s [%p]",param->name,this);
	paramPtr += param->size;
	paramLen -= param->size;
    } // while ((ptype = *plist++)...
    bool mustWarn = true;
    // next decode any mandatory variable parameters the message should have
    while ((ptype = *plist++) != SS7MsgISUP::EndOfParameters) {
	mustWarn = false;
	const IsupParam* param = getParamDesc(ptype);
	if (!param) {
	    // we could skip over unknown mandatory variable length but it's still bad
	    Debug(this,DebugGoOn,"Missing description of variable ISUP parameter 0x%02x [%p]",ptype,this);
	    return false;
	}
	if (param->size)
	    Debug(this,DebugMild,"Invalid (fixed) description of variable ISUP parameter %s [%p]",param->name,this);
	unsigned int offs = paramPtr[0];
	if ((offs < 1) || (offs >= paramLen)) {
	    Debug(this,DebugWarn,"Invalid offset %u (len=%u) ISUP parameter %s [%p]",
		offs,paramLen,param->name,this);
	    return false;
	}
	unsigned int size = paramPtr[offs];
	if ((size < 1) || (offs+size >= paramLen)) {
	    Debug(this,DebugWarn,"Invalid size %u (ofs=%u, len=%u) ISUP parameter %s [%p]",
		size,offs,paramLen,param->name,this);
	    return false;
	}
	if (!decodeParam(this,msg,param,paramPtr+offs+1,size,prefix))
	    Debug(this,DebugWarn,"Could not decode variable ISUP parameter %s (size=%u) [%p]",
		param->name,size,this);
	paramPtr++;
	paramLen--;
    } // while ((ptype = *plist++)...
    // now decode the optional parameters if the message supports them
    if (params->optional) {
	unsigned int offs = paramPtr[0];
	if (offs >= paramLen) {
	    Debug(this,DebugWarn,"Invalid ISUP optional offset %u (len=%u) [%p]",
		offs,paramLen,this);
	    return false;
	}
	else if (offs) {
	    mustWarn = true;
	    // advance pointer past mandatory parameters
	    paramPtr += offs;
	    paramLen -= offs;
	    while (paramLen) {
		ptype = (SS7MsgISUP::Parameters)(*paramPtr++);
		paramLen--;
		if (ptype == SS7MsgISUP::EndOfParameters)
		    break;
		if (paramLen < 2) {
		    Debug(this,DebugWarn,"Only %u octets while decoding optional ISUP parameter 0x%02x [%p]",
			paramLen,ptype,this);
		    return false;
		}
		unsigned int size = *paramPtr++;
		paramLen--;
		if ((size < 1) || (size >= paramLen)) {
		    Debug(this,DebugWarn,"Invalid size %u (len=%u) ISUP optional parameter 0x%02x [%p]",
			size,paramLen,ptype,this);
		    return false;
		}
		const IsupParam* param = getParamDesc(ptype);
		if (!param)
		    Debug(this,DebugMild,"Unknown optional ISUP parameter 0x%02x (size=%u) [%p]",ptype,size,this);
		else if (!decodeParam(this,msg,param,paramPtr,size,prefix))
		    Debug(this,DebugWarn,"Could not decode optional ISUP parameter %s (size=%u) [%p]",param->name,size,this);
		paramPtr += size;
		paramLen -= size;
	    } // while (paramLen)
	} // else if (offs)
	else
	    paramLen = 0;
    }
    if (paramLen && mustWarn)
	Debug(this,DebugWarn,"Got %u garbage octets after message type 0x%02x [%p]",
	    paramLen,msgType,this);
    return true;
}

// Encode an ISUP list of parameters to a buffer
bool SS7ISUP::encodeMessage(DataBlock& buf, SS7MsgISUP::Type msgType, SS7PointCode::Type pcType,
    const NamedList& params, unsigned int* cic)
{
    unsigned int circuit = cic ? *cic : 0;
    SS7Label label(pcType,1,1,1);

    SS7MSU* msu = buildMSU(msgType,1,label,circuit,&params);
    if (!msu)
	return false;
    unsigned int start = 1 + label.length() + (cic ? 0 : m_cicLen);
    buf.assign(((char*)msu->data()) + start,msu->length() - start);
    TelEngine::destruct(msu);
    return true;
}

bool SS7ISUP::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != SS7MSU::ISUP || !hasPointCode(label.dpc())) {
	DDebug(this,DebugAll,"Refusing MSU: %s",msu.getSIF()!=SS7MSU::ISUP?"not ISUP":"invalid point code");
	return false;
    }
    // we should have at least 2 bytes CIC and 1 byte message type
    const unsigned char* s = msu.getData(label.length()+1,3);
    if (!s)
	return false;
    unsigned int len = msu.length()-label.length()-1;
    unsigned int cic = s[0] | (s[1] << 8);
    SS7MsgISUP::Type type = (SS7MsgISUP::Type)s[2];
    const char* name = SS7MsgISUP::lookup(type);
    if (name) {
	bool ok = processMSU(type,cic,s+3,len-3,label,network,sls);
	if (!ok && debugAt(DebugMild)) {
	    String tmp;
	    tmp.hexify((void*)s,len,' ');
	    Debug(this,DebugMild,"Unhandled ISUP type %s, cic=%u, length %u: %s",
		name,cic,len,tmp.c_str());
	}
	return true;
    }
    String tmp;
    tmp.hexify((void*)s,len,' ');
    Debug(this,DebugMild,"Received unknown ISUP type 0x%02x, cic=%u, length %u: %s",
	type,cic,len,tmp.c_str());
    return false;
}

bool SS7ISUP::processMSU(SS7MsgISUP::Type type, unsigned int cic,
    const unsigned char* paramPtr, unsigned int paramLen,
    const SS7Label& label, SS7Layer3* network, int sls)
{
    XDebug(this,DebugAll,"SS7ISUP::processMSU(%u,%u,%p,%u,%p,%p,%d) [%p]",
	type,cic,paramPtr,paramLen,&label,network,sls,this);

    SS7MsgISUP* msg = new SS7MsgISUP(type,cic);
    if (!decodeMessage(msg->params(),type,label.type(),paramPtr,paramLen)) {
	TelEngine::destruct(msg);
	return false;
    }

    if (debugAt(DebugInfo)) {
	String tmp;
	msg->toString(tmp,label,debugAt(DebugAll));
	Debug(this,DebugInfo,"Received message (%p)%s",msg,tmp.c_str());
    }

    switch (msg->type()) {
	case SS7MsgISUP::RLC:
	    // Just reset the circuit if it's a response to RSC request
	    if (m_rscCic && m_rscCic->code() == msg->cic()) {
		resetCircuit(msg->cic(),false);
		break;
	    }
	case SS7MsgISUP::IAM:
	case SS7MsgISUP::SAM:
	case SS7MsgISUP::ACM:
	case SS7MsgISUP::CPR:
	case SS7MsgISUP::ANM:
	case SS7MsgISUP::CON:
	case SS7MsgISUP::REL:
	case SS7MsgISUP::SGM:
	    processCallMsg(msg,label,sls);
	    break;
	default:
	    processControllerMsg(msg,label,sls);
    }
    msg->deref();

    return true;
}

// Process call related messages
void SS7ISUP::processCallMsg(SS7MsgISUP* msg, const SS7Label& label, int sls)
{
    // Find a call for this message, create a new one or drop the message
    SS7ISUPCall* call = findCall(msg->cic());
    const char* reason = 0;
    while (true) {
	#define DROP_MSG(res) { reason = res; break; }
	// Avoid cic == 0
	if (!msg->cic())
	    DROP_MSG("invalid CIC")
	// non IAM message. Drop it if there is no call for it
	if (msg->type() != SS7MsgISUP::IAM) {
	    if (!call)
		DROP_MSG("no call for this CIC")
	    break;
	}
	// IAM message
	SignallingCircuit* circuit = 0;
	// Check collision
	if (call) {
	    // If existing call is an incoming one, drop the message (retransmission ?)
	    if (!call->outgoing())
		DROP_MSG("retransmission")
	    Debug(this,DebugNote,"Incoming call %u collide with existing outgoing",msg->cic());
	    // *** See Q.764 2.9.1.4
	    // Drop the request if the outgoing call already received some response or
	    // the destination point code is greater then the originating and the CIC is even
	    if (call->state() > SS7ISUPCall::Setup)
		DROP_MSG("collision - outgoing call responded")
	    // The greater point code should have the even circuit
	    unsigned int dpc = label.dpc().pack(label.type());
	    unsigned int opc = label.opc().pack(label.type());
	    if (dpc > opc && !(msg->cic() % 2))
		DROP_MSG("collision - dpc greater then opc for even CIC")
	    // Accept the incoming request. Change the call's circuit
	    reserveCircuit(circuit);
	    call->replaceCircuit(circuit);
	    circuit = 0;
	}
	String s = msg->cic();
	int flags = -1;
	// Q.764 2.8.2 - accept test calls even if the remote side is blocked
	// Q.764 2.8.2.3 (xiv) - unblock remote side of the circuit for non-test calls
	if (String(msg->params().getValue("CallingPartyCategory")) == "test") {
	    Debug(this,DebugInfo,"Received test call on circuit %u",msg->cic());
	    flags &= ~SignallingCircuit::LockRemote;
	}
	else
	    blockCircuit(msg->cic(),false,true);
	if (reserveCircuit(circuit,flags,&s,true)) {
	    call = new SS7ISUPCall(this,circuit,label.dpc(),label.opc(),false,sls);
	    m_calls.append(call);
	    break;
	}
	// Congestion: send REL
	transmitREL(this,msg->cic(),label,true,sls,"congestion");
	DROP_MSG("can't reserve circuit")
	#undef DROP_MSG
    }
    if (!reason) {
	msg->ref();
	call->enqueue(msg);
    }
    else {
	if (msg->type() != SS7MsgISUP::IAM && msg->type() != SS7MsgISUP::RLC) {
	    if (msg->type() != SS7MsgISUP::REL)
		transmitRLC(this,msg->cic(),label,true,sls);
	    else
		transmitRLC(this,msg->cic(),label,true,sls,0,msg);
	}
	if (msg->type() != SS7MsgISUP::RLC)
	    Debug(this,DebugNote,"'%s' with cic=%u: %s",msg->name(),msg->cic(),reason);
    }
}

// Process controller related messages
// Q.764 2.1.12: stop waiting for SGM if message is not: COT,BLK,BLA,UBL,UBA,CGB,CGA,CGU,CUA,CQM,CQR
void SS7ISUP::processControllerMsg(SS7MsgISUP* msg, const SS7Label& label, int sls)
{
    const char* reason = 0;
    bool impl = true;
    bool stopSGM = true;
    switch (msg->type()) {
	case SS7MsgISUP::CNF: // Confusion
	    Debug(this,DebugNote,"%s with cause='%s' diagnostic='%s'",msg->name(),
		msg->params().getValue("CauseIndicators"),msg->params().getValue("CauseIndicators.diagnostic"));
	    break;
	case SS7MsgISUP::RSC: // Reset Circuit
	    if (resetCircuit(msg->cic(),true))
		transmitRLC(this,msg->cic(),label,true,sls);
	    else
		reason = "unknown CIC";
	    break;
	case SS7MsgISUP::UBL: // Unblocking
	case SS7MsgISUP::BLK: // Blocking
	    {
	        bool block = (msg->type() == SS7MsgISUP::BLK);
		if (blockCircuit(msg->cic(),block,true))
		    transmitMessage(new SS7MsgISUP(block ? SS7MsgISUP::BLA : SS7MsgISUP::UBA,msg->cic()),label,true,sls);
		else
		    reason = "unknown CIC";
	    }
	    stopSGM = false;
	    break;
	case SS7MsgISUP::GRS: // Circuit Group Reset
	    {
		String rs = msg->params().getValue("RangeAndStatus");
		unsigned int n = rs.toInteger();
		// Q.763 3.43
		if (n < 1 || n > 31) {
		    reason = "invalid range";
		    transmitCNF(this,msg->cic(),label,true,sls,"wrong-message");
		    break;
		}
		String map('0',n);
		char* d = (char*)map.c_str();
		for (unsigned int i = 0; i < n; i++)
		    if (!resetCircuit(msg->cic()+i,true))
			d[i] = '1';
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::GRA,msg->cic());
		m->params().addParam("RangeAndStatus",rs);
		m->params().addParam("RangeAndStatus.map",map);
		transmitMessage(m,label,true,sls);
	    }
	    break;
	case SS7MsgISUP::BLA: // Blocking Acknowledgement
	case SS7MsgISUP::UBA: // Unblocking Acknowledgement
	case SS7MsgISUP::CGA: // Circuit Group Blocking Acknowledgement
	case SS7MsgISUP::CUA: // Circuit Group Unblocking Acknowledgement
	case SS7MsgISUP::CQR: // Circuit Group Query Response (national use)
	    stopSGM = false;
	case SS7MsgISUP::GRA: // Circuit Group Reset Acknowledgement
	    reason = "unexpected response";
	    transmitCNF(this,msg->cic(),label,true,sls,"wrong-state-message");
	    break;
	case SS7MsgISUP::CGB: // Circuit Group Blocking
	case SS7MsgISUP::CGU: // Circuit Group Unblocking
	case SS7MsgISUP::CQM: // Circuit Group Query (national use)
	case SS7MsgISUP::COT: // Continuity
	    stopSGM = false;
	default:
	    impl = false;
	    reason = "not implemented";
	    transmitCNF(this,msg->cic(),label,true,sls,"service-not-implemented");
    }
    if (stopSGM) {
	SS7ISUPCall* call = findCall(msg->cic());
	if (call)
	    call->stopWaitSegment(false);
    }
    if (reason)
	Debug(this,impl?DebugNote:DebugStub,"'%s' with cic=%u: %s",msg->name(),msg->cic(),reason);
}

// Replace a call's circuit if checkCall is true
// Release currently reseting circuit if the code match
// Clear lock flags
// See Q.764 2.9.3.1
bool SS7ISUP::resetCircuit(unsigned int cic, bool checkCall)
{
    SignallingCircuit* circuit = circuits() ? circuits()->find(cic) : 0;
    if (!circuit)
	return false;
    DDebug(this,DebugAll,"Reseting circuit %u",cic);
    if (checkCall) {
	SS7ISUPCall* call = findCall(cic);
	if (call) {
	    SignallingCircuit* newCircuit = 0;
	    reserveCircuit(newCircuit);
	    call->replaceCircuit(newCircuit);
	}
    }
    // Remove lock flags (Q.764 2.9.3)
    int flags = SignallingCircuit::LockRemote | SignallingCircuit::LockLocal;
    if (circuit->locked(flags)) {
	Debug(this,DebugNote,"Unblocking circuit %u",cic);
	circuit->resetLock(flags);
    }
    if (m_rscCic && m_rscCic->code() == cic)
	releaseCircuit(m_rscCic);
    else
	circuit->status(SignallingCircuit::Idle);
    return true;
}

// Block/unblock a circuit
// See Q.764 2.8.2
bool SS7ISUP::blockCircuit(unsigned int cic, bool block, bool remote)
{
    SignallingCircuit* circuit = circuits() ? circuits()->find(cic) : 0;
    if (!circuit)
	return false;
    int flag = remote ? SignallingCircuit::LockRemote : SignallingCircuit::LockLocal;
    // Already blocked/unblocked ?
    if (block == (0 != circuit->locked(flag)))
	return true;
    Debug(this,DebugNote,"%slocking %s side of the circuit %u",block?"B":"Unb",remote?"remote":"local",cic);
    // Replace circuit for call (Q.764 2.8.2.1)
    SS7ISUPCall* call = findCall(cic);
    if (call && call->outgoing() && call->state() == SS7ISUPCall::Setup) {
	SignallingCircuit* newCircuit = 0;
	reserveCircuit(newCircuit);
	call->replaceCircuit(newCircuit);
    }
    if (block)
	circuit->setLock(flag);
    else
	circuit->resetLock(flag);
    return true;
}

SS7ISUPCall* SS7ISUP::findCall(unsigned int cic)
{
    Lock lock(this);
    for (ObjList* o = m_calls.skipNull(); o; o = o->skipNext()) {
	SS7ISUPCall* call = static_cast<SS7ISUPCall*>(o->get());
	if (call->id() == cic)
	    return call;
    }
    return 0;
}


/**
 * SS7BICC
 */
SS7BICC::SS7BICC(const NamedList& params)
    : SS7ISUP(params)
{
    setName(params.getValue("debugname","bicc"));
    m_cicLen = 4;
    Debug(this,DebugInfo,"BICC Call Controller [%p]",this);
}

SS7BICC::~SS7BICC()
{
    cleanup();
    Debug(this,DebugInfo,"BICC Call Controller destroyed [%p]",this);
}

SS7MSU* SS7BICC::createMSU(SS7MsgISUP::Type type, unsigned char ssf,
    const SS7Label& label, unsigned int cic, const NamedList* params) const
{
    return buildMSU(type,SS7MSU::BICC | (ssf & 0xf0),label,cic,params);
}

// Get a pointer to this object or other data
void* SS7BICC::getObject(const String& name) const
{
    if (name == "SS7BICC")
	return (void*)this;
    return SS7ISUP::getObject(name);
}

bool SS7BICC::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != SS7MSU::BICC || !hasPointCode(label.dpc()))
	return false;
    // we should have at least 4 bytes CIC and 1 byte message type
    const unsigned char* s = msu.getData(label.length()+1,5);
    if (!s)
	return false;
    unsigned int len = msu.length()-label.length()-1;
    unsigned int cic = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);
    SS7MsgISUP::Type type = (SS7MsgISUP::Type)s[4];
    const char* name = SS7MsgISUP::lookup(type);
    if (name) {
	bool ok = processMSU(type,cic,s+5,len-5,label,network,sls);
	String tmp;
	tmp.hexify((void*)s,len,' ');
	Debug(this,ok ? DebugInfo : DebugMild,"Unhandled BICC type %s, cic=%u, length %u: %s",
	    name,cic,len,tmp.c_str());
	return true;
    }
    String tmp;
    tmp.hexify((void*)s,len,' ');
    Debug(this,DebugMild,"Received unknown BICC type 0x%02x, cic=%u, length %u: %s",
	type,cic,len,tmp.c_str());
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
