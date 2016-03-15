/**
 * isup.cpp
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
#include <yatephone.h>
#include <string.h>

using namespace TelEngine;

#ifdef DEBUG
#define ISUP_HANDLE_CIC_EVENT_CONTROL
#else
//#define ISUP_HANDLE_CIC_EVENT_CONTROL
#endif

// Maximum number of mandatory parameters including two terminators
#define MAX_MANDATORY_PARAMS 16

// Timer limits and default values
#define ISUP_T7_MINVAL  20000
#define ISUP_T7_DEFVAL  20000
#define ISUP_T7_MAXVAL  30000
#define ISUP_T9_MINVAL  90000
#define ISUP_T9_DEFVAL  0
#define ISUP_T9_MAXVAL  180000
#define ISUP_T27_MINVAL 30000
#define ISUP_T27_DEFVAL 240000
#define ISUP_T27_MAXVAL 300000
#define ISUP_T34_MINVAL 2000
#define ISUP_T34_DEFVAL 3000
#define ISUP_T34_MAXVAL 4000

// Utility: check if 2 cic codes are in valid range, return range if valid, 0 otherwise
static inline int checkValidRange(int code, int extra)
{
    int range = extra - code;
    return (range > -256 && range < 256) ? range : 0;
}

// Adjust range and status data when needed (a new range is used)
static void adjustRangeAndStatus(char* status, unsigned int& code, unsigned int& range,
    int newRange)
{
    if (!(status && newRange))
	return;
    if (newRange > 0) {
	range = (unsigned int)newRange;
	status[0] = '1';
	::memset(status + 1,'0',range);
    }
    else {
	range = (unsigned int)(-newRange);
	code -= range;
	::memset(status,'0',range);
	status[range] = '1';
    }
    range++;
}

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
static const TokenDict s_dict_nai[] = {
    { "subscriber",        1 },
    { "unknown",           2 },
    { "national",          3 },
    { "international",     4 },
    { "network-specific",  5 },
    { "national-routing",  6 },
    { "specific-routing",  7 },
    { "routing-with-cdn",  8 },
    { 0, 0 }
};

// Numbering Plan Indicator
static const TokenDict s_dict_numPlan[] = {
    { "unknown",  0 },
    { "isdn",     1 },
    { "data",     3 },
    { "telex",    4 },
    { "private",  5 },
    { "national", 6 },
    { 0, 0 }
};

// Address Presentation
static const TokenDict s_dict_presentation[] = {
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
static const TokenDict s_dict_screening[] = {
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

// Generic number qualifier
static const TokenDict s_dict_qual[] = {
    { "dialed-digits",        0 },
    { "called-additional",    1 },
    { "caller-failed",        2 },
    { "caller-not-screened",  3 },
    { "terminating",          4 },
    { "connected-additional", 5 },
    { "caller-additional",    6 },
    { "called-original",      7 },
    { "redirecting",          8 },
    { "redirection",          9 },
    { 0, 0 }
};

// Generic name qualifier
static const TokenDict s_dict_qual_name[] = {
    { "caller" ,           0x20 },
    { "called" ,           0x40 },
    { "redirecting" ,      0x60 },
    { "connected" ,        0x80 },
    { 0, 0 }
};

// Redirection Information (Q,763 3.45) bits CBA
static const TokenDict s_dict_redir_main[] = {
    { "none",                     0 },
    { "rerouted",                 1 },
    { "rerouted-restrict-all",    2 },
    { "diverted",                 3 },
    { "diverted-restrict-all",    4 },
    { "rerouted-restrict-number", 5 },
    { "diverted-restrict-number", 6 },
    { 0, 0 }
};

// Redirection Information (Q,763 3.45) bits HGFE or PONM
static const TokenDict s_dict_redir_reason[] = {
    { "busy",      1 },
    { "noanswer",  2 },
    { "always",    3 },
    { "deflected", 4 },
    { "diverted",  5 },
    { "offline",   6 },
    { 0, 0 }
};

// Message Compatibility Information (Q.763 3.33)
static const SignallingFlags s_flags_msgcompat[] = {
    { 0x01, 0x00, "transit" },           // End node / transit exchange
    { 0x01, 0x01, "end-node" },
    { 0x02, 0x02, "release" },           // Release call indicator
    { 0x04, 0x04, "cnf" },               // Pass on set but not possible: Send CNF / RLC
    { 0x08, 0x08, "discard" },           // Discard / pass on message
    { 0x10, 0x00, "nopass-release" },    // Pass on not possible: Release call
    { 0x10, 0x10, "nopass-discard" },    // Pass on not possible: Discard message
    { 0, 0, 0 }
};

// Parameter Compatibility Information (Q.763 3.41)
static const SignallingFlags s_flags_paramcompat[] = {
    { 0x01, 0x00, "transit" },           // End node / transit exchange
    { 0x01, 0x01, "end-node" },
    { 0x02, 0x02, "release" },           // Release call indicator
    { 0x04, 0x04, "cnf" },               // Parameter pass on set but not possible: Send CNF / RLC
    { 0x08, 0x08, "discard-msg" },       // Discard / pass on message
    { 0x18, 0x10, "discard-param" },     // Discard / pass on parameter (if not discarding message)
    { 0x60, 0x00, "nopass-release" },    // No pass on: release call
    { 0x60, 0x20, "nopass-msg" },        // No pass on: discard message
    { 0x60, 0x40, "nopass-param" },      // No pass on: discard parameter
    { 0x60, 0x60, "nopass-release" },    // Reserved, interpreted as 00
    { 0, 0, 0 }
};

// Application Transport Parameter instruction indicators (Q.763 3.82)
static const SignallingFlags s_flags_apt_indicators[] = {
    { 0x01, 0x01, "release" },           // Release call indicator
    { 0x02, 0x02, "cnf" },               // Send CNF notification
    { 0, 0, 0 }
};

// SLS special values on outbound calls
static const TokenDict s_dict_callSls[] = {
    { "auto", SS7ISUP::SlsAuto    }, // Let Layer3 deal with it
    { "last", SS7ISUP::SlsLatest  }, // Last SLS used
    { "cic",  SS7ISUP::SlsCircuit }, // Lower bits of CIC
    { 0, 0 }
};

// Control operations
static const TokenDict s_dict_control[] = {
    { "validate", SS7MsgISUP::CVT },
    { "query", SS7MsgISUP::CQM },
    { "conttest", SS7MsgISUP::CCR },
    { "reset", SS7MsgISUP::RSC },
    { "block", SS7MsgISUP::BLK },
    { "unblock", SS7MsgISUP::UBL },
    { "release", SS7MsgISUP::RLC },
    { "parttest", SS7MsgISUP::UPT },
    { "available", SS7MsgISUP::UPA },
    { "save", SS7MsgISUP::CtrlSave },
#ifdef ISUP_HANDLE_CIC_EVENT_CONTROL
    { "circuitevent", SS7MsgISUP::CtrlCicEvent },
#endif
    { 0, 0 }
};

static const TokenDict s_dict_CRG_process[] = {
    { "confusion", SS7ISUP::Confusion },
    { "ignore",    SS7ISUP::Ignore },
    { "raw",       SS7ISUP::Raw },
    { "parsed",    SS7ISUP::Parsed },
    { 0, 0 }
};

// Build next available parameter name
static void buildName(const NamedList& list, const IsupParam* param, const String& prefix, String& name)
{
    name = prefix + param->name;
    if (!list.getParam(name))
	return;
    // conflict - find a free index
    for (unsigned int i = 1; ; i++) {
	String tmp(name);
	tmp << "." << i;
	if (!list.getParam(tmp)) {
	    name = tmp;
	    break;
	}
    }
}

// Default decoder, dumps raw octets
static bool decodeRaw(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String raw;
    raw.hexify((void*)buf,len,' ');
    DDebug(isup,DebugInfo,"decodeRaw decoded %s=%s",param->name,raw.c_str());
    String preName;
    buildName(list,param,prefix,preName);
    list.addParam(preName,raw);
    return true;
}

// Raw decoder for unknown/failed parameter, dumps raw octets
static bool decodeRawParam(const SS7ISUP* isup, NamedList& list, unsigned char value,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    String name("Param_");
    name << value;
    IsupParam p;
    p.type = (SS7MsgISUP::Parameters)value;
    p.size = len;
    p.name = name;
    p.decoder = 0;
    p.encoder = 0;
    p.data = 0;
    return decodeRaw(isup,list,&p,buf,len,prefix);
};

// Integer decoder, interprets data as big endian integer
static bool decodeInt(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    unsigned int val = 0;
    while (len--)
	val = (val << 8) | (unsigned int)(*buf++);
    DDebug(isup,DebugAll,"decodeInt decoded %s=%s (%u)",param->name,lookup(val,(const TokenDict*)param->data),val);
    String preName;
    buildName(list,param,prefix,preName);
    SignallingUtils::addKeyword(list,preName,(const TokenDict*)param->data,val);
    return true;
}

// Decoder for ISUP indicators (flags)
static bool decodeFlags(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    const SignallingFlags* flags = (const SignallingFlags*)param->data;
    if (!flags)
	return false;
    String preName;
    buildName(list,param,prefix,preName);
    return SignallingUtils::decodeFlags(isup,list,preName,flags,buf,len);
}

// Utility function - extract just ISUP digits from a parameter
static void getDigits(String& num, unsigned char oddNum, const unsigned char* buf, unsigned int len,
    bool ignoreUnk)
{
    bool odd = (oddNum & 0x80) != 0;
    static const char digits1[] = "0123456789\0BC\0\0.";
    static const char digits2[] = "0123456789ABCDE.";
    const char* digits = ignoreUnk ? digits1 : digits2;
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
    String preName;
    buildName(list,param,prefix,preName);
    switch (param->type) {
	case SS7MsgISUP::MessageCompatInformation:
	    SignallingUtils::decodeFlags(isup,list,preName,s_flags_msgcompat,buf,1);
	    if (buf[0] & 0x80) {
		if (len == 1)
		    return true;
		DDebug(isup,DebugMild,
		    "decodeCompat invalid len=%u for %s with first byte having ext bit set",len,param->name);
		break;
	    }
	    return 0 != SignallingUtils::dumpDataExt(isup,list,preName+".more",buf+1,len-1);
	case SS7MsgISUP::ParameterCompatInformation:
	    for (unsigned int i = 0; i < len;) {
		unsigned char val = buf[i++];
		if (i == len) {
		    Debug(isup,DebugMild,"decodeCompat unexpected end of data (len=%u) for %s",len,param->name);
		    return false;
		}
		const char* paramName = getIsupParamName(val);
		String name = preName;
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
	    decodeRaw(isup,list,param,buf,len,prefix);
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
    unsigned char qualifier = 0;
    if (SS7MsgISUP::GenericNumber == param->type) {
	if (--len < 2)
	    return false;
	qualifier = buf[0];
	buf++;
    }
    unsigned char nai = buf[0] & 0x7f;
    unsigned char plan = (buf[1] >> 4) & 7;
    unsigned char pres = (buf[1] >> 2) & 3;
    unsigned char scrn = buf[1] & 3;
    String tmp;
    getDigits(tmp,buf[0],buf+2,len-2,isup && isup->ignoreUnknownAddrSignals());
    DDebug(isup,DebugAll,"decodeDigits decoded %s='%s' inn/ni=%u nai=%u plan=%u pres=%u scrn=%u",
	param->name,tmp.c_str(),buf[1] >> 7,nai,plan,pres,scrn);
    String preName;
    buildName(list,param,prefix,preName);
    list.addParam(preName,tmp);
    if (SS7MsgISUP::GenericNumber == param->type)
	SignallingUtils::addKeyword(list,preName+".qualifier",s_dict_qual,qualifier);
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
	case SS7MsgISUP::GenericNumber:
	    tmp = ((buf[1] & 0x80) == 0);
	    list.addParam(preName+".complete",tmp);
	    break;
	case SS7MsgISUP::LastDivertingLineIdentity:
	case SS7MsgISUP::PresentationNumber:
	    tmp = ((buf[1] & 0x80) != 0);
	    list.addParam(preName+".pnp",tmp);
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
	case SS7MsgISUP::GenericNumber:
	case SS7MsgISUP::LastDivertingLineIdentity:
	case SS7MsgISUP::PresentationNumber:
	case SS7MsgISUP::CalledINNumber:
	case SS7MsgISUP::OriginalCalledINNumber:
	    SignallingUtils::addKeyword(list,preName+".restrict",s_dict_presentation,pres);
	default:
	    break;
    }
    switch (param->type) {
	case SS7MsgISUP::CallingPartyNumber:
	case SS7MsgISUP::LocationNumber:
	case SS7MsgISUP::ConnectedNumber:
	case SS7MsgISUP::GenericNumber:
	case SS7MsgISUP::LastDivertingLineIdentity:
	case SS7MsgISUP::PresentationNumber:
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
    getDigits(tmp,buf[0],buf+1,len-1,isup && isup->ignoreUnknownAddrSignals());
    DDebug(isup,DebugAll,"decodeSubseq decoded %s='%s'",param->name,tmp.c_str());
    String preName;
    buildName(list,param,prefix,preName);
    list.addParam(preName,tmp);
    return true;
}

// Decoder for circuit group range and status (Q.763 3.43)
static bool decodeRangeSt(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String preName;
    buildName(list,param,prefix,preName);
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
	unsigned int r = range;
	while (r--) {
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
	const char* keyword = lookup(val & 0x7f,(const TokenDict*)param->data);
	if (keyword)
	    flg.append(keyword,",");
	else {
	    String tmp(0x7f & (int)val);
	    flg.append(tmp,",");
	}
	if (val & 0x80)
	    break;
    }
    DDebug(isup,DebugAll,"decodeNotif decoded %s='%s'",param->name,flg.c_str());
    String preName;
    buildName(list,param,prefix,preName);
    list.addParam(preName,flg);
    return true;
}

// Decoder for User Service Information
static bool decodeUSI(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    String preName;
    buildName(list,param,prefix,preName);
    return SignallingUtils::decodeCaps(isup,list,buf,len,preName,true);
}

// Decoder for cause indicators
static bool decodeCause(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    String preName;
    buildName(list,param,prefix,preName);
    return SignallingUtils::decodeCause(isup,list,buf,len,preName,true);
}

// Decoder for application transport parameter
static bool decodeAPT(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 4) {
	if (len == 3)
	    Debug(isup,DebugNote,"Received '%s' with no data",param->name);
	return false;
    }
    // Field extension on more then 1 octet is not supported
    if (0 == (buf[0] & buf[1] & buf[2] & 0x80)) {
	Debug(isup,DebugNote,"Received %s with unsupported extension bits set to 0",
	    param->name);
	return false;
    }
    // Segmentation is not supported
    unsigned char si = (buf[2] & 0x40);
    unsigned char segments = (buf[2] & 0x3f);
    if (!si || segments) {
	Debug(isup,DebugNote,"Received unsupported segmented %s (si=%u segments=%u)",
	    param->name,si,segments);
	return false;
    }
    len -= 3;
    // WARNING: HACK - ApplicationTransport does not follow naming convention
    String preName(prefix + param->name);
    String context((int)(buf[0] & 0x7f));
    list.addParam(preName,context);
    preName << "." << context;
    // Application context identifier
    SignallingUtils::dumpData(isup,list,preName,buf + 3,len);
    // Instruction indicators
    unsigned char inds = (buf[1] & 0x7f);
    SignallingUtils::decodeFlags(isup,list,preName + ".indicators",s_flags_apt_indicators,&inds,1);
    return true;
}

// Decoder for generic name
static bool decodeName(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String val((const char*)buf+1,len-1);
    String preName;
    buildName(list,param,prefix,preName);
    list.addParam(preName,val);
    list.addParam(preName+".available",String::boolText((buf[0] & 0x10) == 0));
    SignallingUtils::addKeyword(list,preName+".qualifier",s_dict_qual_name,buf[0] & 0xe0);
    SignallingUtils::addKeyword(list,preName+".restrict",s_dict_presentation,buf[0] & 0x03);
    DDebug(isup,DebugAll,"decodeName decoded %s='%s'",param->name,val.c_str());
    return true;
}

// Decoder for Redirection information (Q.763 3.45)
static bool decodeRedir(const SS7ISUP* isup, NamedList& list, const IsupParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String preName;
    buildName(list,param,prefix,preName);
    SignallingUtils::addKeyword(list,preName,s_dict_redir_main,buf[0] & 0x07);
    unsigned int reason = buf[0] >> 4;
    if (reason)
	SignallingUtils::addKeyword(list,preName+".reason_original",s_dict_redir_reason,reason);
    if (len > 1) {
	int cnt = buf[1] & 0x07;
	if (cnt)
	    list.addParam(preName+".counter",String(cnt));
	reason = buf[1] >> 4;
	if (reason)
	    SignallingUtils::addKeyword(list,preName+".reason",s_dict_redir_reason,reason);
    }
    return true;
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
    unsigned char size = (unsigned char)raw.length();
    msu.append(&size,1);
    msu += raw;
    return size;
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
    if (val)
	v = SignallingUtils::encodeFlags(isup,*val,(const SignallingFlags*)param->data,param->name);
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

// Encoder for fixed length big-endian integer values
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
    buf += n;
    while (n--) {
	*(--buf) = v & 0xff;
	v >>= 8;
    }
    return param->size;
}

// Utility function - write digit sequences
static unsigned char setDigits(SS7MSU& msu, const char* val, unsigned char nai, int b2 = -1, int b3 = -1, int b0 = -1)
{
    unsigned char buf[32];
    unsigned int len = 1;
    if (b0 >= 0)
	buf[len++] = b0 & 0xff;
    unsigned int naiPos = len++;
    buf[naiPos] = nai & 0x7f;
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
	else if ('A' == c)
	    n = 10;
	else if ('B' == c)
	    n = 11;
	else if ('C' == c)
	    n = 12;
	else if ('D' == c)
	    n = 13;
	else if ('E' == c)
	    n = 14;
	else
	    continue;
	odd = !odd;
	if (odd)
	    buf[len] = n;
	else
	    buf[len++] |= (n << 4);
    }
    if (odd) {
	buf[naiPos] |= 0x80;
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
    String preName;
    if (val)
	preName = val->name();
    else
	preName = prefix + param->name;
    int b0 = -1;
    if (SS7MsgISUP::GenericNumber == param->type) {
	b0 = 0;
	if (val && extra)
	    b0 = 0xff & extra->getIntValue(preName+".qualifier",s_dict_qual,0);
    }
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
	case SS7MsgISUP::GenericNumber:
	    if (val && extra && !extra->getBoolValue(preName+".complete",true))
		b2 |= 0x80;
	    break;
	case SS7MsgISUP::LastDivertingLineIdentity:
	case SS7MsgISUP::PresentationNumber:
	    if (!val || !extra || extra->getBoolValue(preName+".pnp",true))
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
	case SS7MsgISUP::GenericNumber:
	case SS7MsgISUP::LastDivertingLineIdentity:
	case SS7MsgISUP::PresentationNumber:
	case SS7MsgISUP::CalledINNumber:
	case SS7MsgISUP::OriginalCalledINNumber:
	    if (val && extra)
		b2 |= (extra->getIntValue(preName+".restrict",s_dict_presentation) & 3) << 2;
	default:
	    break;
    }
    switch (param->type) {
	case SS7MsgISUP::CallingPartyNumber:
	case SS7MsgISUP::LocationNumber:
	case SS7MsgISUP::ConnectedNumber:
	case SS7MsgISUP::GenericNumber:
	case SS7MsgISUP::LastDivertingLineIdentity:
	case SS7MsgISUP::PresentationNumber:
	    if (val && extra)
		b2 |= extra->getIntValue(preName+".screened",s_dict_screening) & 3;
	default:
	    break;
    }
    return setDigits(msu,val ? val->c_str() : 0,nai,b2,-1,b0);
}

// Special encoder for subsequent number
static unsigned char encodeSubseq(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList*, const String&)
{
    return setDigits(msu,val ? val->c_str() : 0,0);
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
    // Q.763 3.43 Sent range value must be in interval 1..256
    unsigned int range = val->toInteger(0);
    if (range < 1 || range > 256) {
	Debug(isup,DebugNote,"encodeRangeSt invalid range %s=%s",val->name().c_str(),val->safe());
	return 0;
    }
    data[1] = range - 1;
    // Next octets: status bits for the circuits given by range
    NamedString* map = extra->getParam(prefix+param->name+".map");
    if (map && map->length()) {
	// Max status bits is 256. Relevant status bits: range
	unsigned int nBits = map->length();
	if (nBits > 256) {
	    Debug(isup,DebugNote,"encodeRangeSt truncating status bits %u to 256",map->length());
	    nBits = 256;
	}
	unsigned char* src = (unsigned char*)map->c_str();
	unsigned char* dest = data + 1;
	for (unsigned char crtBit = 0; nBits; nBits--, src++) {
	    if (!crtBit) {
		data[0]++;
		*++dest = 0;
	    }
	    if (*src != '0')
		*dest |= (1 << crtBit);
	    crtBit = (crtBit < 7 ? crtBit + 1 : 0);
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
    const NamedList*, const String&)
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
    String preName;
    if (val)
	preName = val->name();
    else
	preName = prefix + param->name;
    DataBlock tmp;
    SignallingUtils::encodeCaps(isup,tmp,*extra,preName,true);
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
    String preName;
    if (val)
	preName = val->name();
    else
	preName = prefix + param->name;
    DataBlock tmp;
    SignallingUtils::encodeCause(isup,tmp,*extra,preName,true);
    DDebug(isup,DebugAll,"encodeCause encoding %s on %u octets",param->name,tmp.length());
    if (tmp.length() < 1)
	return 0;
    msu += tmp;
    return tmp.length() - 1;
}

// Encoder for application transport parameter
static unsigned char encodeAPT(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!param)
	return 0;
    if (TelEngine::null(val)) {
	if (val)
	    Debug(isup,DebugNote,"Failed to encode empty %s",val->name().c_str());
	return 0;
    }
    int context = val->toInteger(-1);
    if (context < 0 || context > 127) {
	// Assume binary parameter representation
	DataBlock data;
	if (!(data.unHexify(val->c_str(),val->length(),' ') && data.length()) ||
	    data.length() < 4 || data.length() > 254) {
	    Debug(isup,DebugNote,"Failed to encode invalid %s=%s",
		param->name,val->c_str());
	    return 0;
	}
	unsigned char len = data.length();
	msu.append(&len,1);
	msu += data;
	return 1 + data.length();
    }
    // WARNING: HACK - ApplicationTransport does not follow naming convention
    String preName(prefix + param->name);
    preName << "." << context;
    unsigned char hdr[4] = {0,(unsigned char)(0x80 | context),0x80,0xc0};  // c0: extension bit set, new sequence bit set
    // Retrieve data. Make sure all bytes are correct and final length don't
    // overflow our return value
    DataBlock data;
    const String& tmp = extra ? (*extra)[preName] : String::empty();
    if (!(data.unHexify(tmp.c_str(),tmp.length(),' ') && data.length()) ||
	data.length() > (255 - sizeof(hdr))) {
	Debug(isup,DebugNote,"Failed to encode invalid %s=%s",
	    param->name,tmp.c_str());
	return 0;
    }
    String indName(preName + ".indicators");
    const String* inds = extra ? extra->getParam(indName) : 0;
    if (inds) {
	unsigned int v = SignallingUtils::encodeFlags(isup,*inds,s_flags_apt_indicators,indName);
	hdr[2] |= (v & 0x7f);
    }
    else {
	// Set default indicators value: send CNF, no call release
	hdr[2] |= 0x02;
    }
    hdr[0] = data.length() + 3;
    msu.append(hdr,sizeof(hdr));
    msu += data;
    return hdr[0];
}

// Encoder for Generic Name
static unsigned char encodeName(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!(param && val) || buf || param->size)
	return 0;
    unsigned int len = val->length() + 1;
    if (len >= 127)
	return 0;
    unsigned char gn[2] = { (unsigned char)len, 3 };
    if (extra) {
	String preName;
	if (val)
	    preName = val->name();
	else
	    preName = prefix + param->name;
	if (!extra->getBoolValue(preName+".available",true))
	    gn[1] |= 0x10;
	gn[1] = (gn[1] & 0x1f) |
	    (extra->getIntValue(preName+".qualifier",s_dict_qual_name,gn[1] & 0xe0) & 0xe0);
	gn[1] = (gn[1] & 0xfc) |
	    (extra->getIntValue(preName+".restrict",s_dict_presentation,gn[1] & 0x03) & 0x03);
    }
    DataBlock tmp(gn,2);
    tmp += *val;
    DDebug(isup,DebugAll,"encodeName encoding %s on %u octets",param->name,tmp.length());
    msu += tmp;
    return len;
}

// Encoder for Redirection information (Q.763 3.45)
static unsigned char encodeRedir(const SS7ISUP* isup, SS7MSU& msu,
    unsigned char* buf, const IsupParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!(param && val) || buf || param->size)
	return 0;
    unsigned char ri[3] = { 2, 0, 0 };
    if (extra) {
	String preName;
	if (val)
	    preName = val->name();
	else
	    preName = prefix + param->name;
	ri[1] = (extra->getIntValue(preName,s_dict_redir_main,0) & 0x07) |
	    ((extra->getIntValue(preName+".reason_original",s_dict_redir_reason,0) & 0x0f) << 4);
	ri[2] = (extra->getIntValue(preName+".counter") & 0x07) |
	    ((extra->getIntValue(preName+".reason",s_dict_redir_reason,0) & 0x0f) << 4);
    }
    DataBlock tmp(ri,3,false);
    msu += tmp;
    tmp.clear(false);
    return ri[0];
}

// Nature of Connection Indicators (Q.763 3.35)
static const SignallingFlags s_flags_naci[] = {
    // TODO: add more flags
    { 0x03, 0x00, "0sat" },
    { 0x03, 0x01, "1sat" },
    { 0x03, 0x02, "2sat" },
    { 0x0c, 0x00, "cont-check-none" },      // Continuity check not required
    { 0x0c, 0x04, "cont-check-this" },      // Continuity check required on this circuit
    { 0x0c, 0x08, "cont-check-prev" },      // Continuity check performed on a previous circuit
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
    { 0x1000, 0x1000, "translated" },       // Number Translated (for portability)
    { 0x2000, 0x2000, "qor-routing" },      // QoR routing attempt in progress
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

// Call Diversion Information (Q.763 3.6)
static const SignallingFlags s_flags_calldivinfo[] = {
    { 0x07, 0x01, "presentation-not-allowed" },
    { 0x07, 0x02, "presentation-with-number" },
    { 0x07, 0x03, "presentation-without-number" },
    { 0x78, 0x08, "busy" },
    { 0x78, 0x10, "noanswer" },
    { 0x78, 0x18, "always" },
    { 0x78, 0x20, "deflected-alerting" },
    { 0x78, 0x28, "deflected-immediate" },
    { 0x78, 0x30, "offline" },
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
    { 0x01, 0x00, "setup-generated" },   // A Setup message was generated
    { 0x01, 0x01, "no-setup" },          // No Setup message generated
    { 0, 0, 0 }
};

// MCID Request or Response Indicators (Q.763 3.31 and 3.32)
static const SignallingFlags s_flags_mcid[] = {
    { 0x01, 0x01, "MCID" },
    { 0x02, 0x02, "holding" },
    { 0, 0, 0 }
};

// ANSI Circuit Validation Response Indicator
static const SignallingFlags s_flags_ansi_cvri[] = {
    { 0x03, 0x00, "failed" },
    { 0x03, 0x01, "success" },
    { 0, 0, 0 }
};

// ANSI Circuit Group Characteristics Indicator
static const SignallingFlags s_flags_ansi_cgci[] = {
    { 0x03, 0x00, "carrier-unknown" },
    { 0x03, 0x01, "carrier-analog" },
    { 0x03, 0x02, "carrier-digital" },
    { 0x03, 0x03, "carrier-mixed" },
    { 0x0c, 0x00, "seize-none" },
    { 0x0c, 0x04, "seize-odd" },
    { 0x0c, 0x08, "seize-even" },
    { 0x0c, 0x0c, "seize-all" },
    { 0x30, 0x00, "alarm-default" },
    { 0x30, 0x10, "alarm-software" },
    { 0x30, 0x20, "alarm-hardware" },
    { 0xc0, 0x00, "continuity-unknown" },
    { 0xc0, 0x40, "continuity-none" },
    { 0xc0, 0x80, "continuity-statistical" },
    { 0xc0, 0xc0, "continuity-call" },
    { 0, 0, 0 }
};

// National Forward Call Indicators (NICC ND 1007 2001 3.2.1)
static const SignallingFlags s_flags_nfci[] = {
    { 0x0001, 0x0000, "cli-blocked" },      // CLI Blocking Indicator (CBI)
    { 0x0001, 0x0001, "cli-allowed" },
    { 0x0002, 0x0002, "translated" },       // Network translated address indicator
    { 0x0004, 0x0004, "iup-priority" },     // Priority access indicator (IUP)
    { 0x0008, 0x0008, "iup-protected" },    // Protection indicator (IUP)
    { 0, 0, 0 }
};

// Calling Party Category (Q.763 3.11)
static const TokenDict s_dict_callerCat[] = {
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
static const TokenDict s_dict_mediumReq[] = {
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
static const TokenDict s_dict_notifications[] = {
    { "user-suspended",         0x00 },
    { "user-resumed",           0x01 },
    { "bearer-service-change",  0x02 },
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

// Number Portability Forward Information (Q.763 3.101)
static const TokenDict s_dict_portability[] = {
    { "not-queried",       1 },
    { "called-not-ported", 2 },
    { "called-ported",     3 },
    { 0, 0 }
};

// ANSI Originating Line Info
static const TokenDict s_dict_oli[] = {
    { "normal",            0 },
    { "multiparty",        1 },
    { "ani-failure",       2 },
    { "hotel-room-id",     6 },
    { "coinless",          7 },
    { "restricted",        8 },
    { "test-call-1",      10 },
    { "aiod-listed-dn",   20 },
    { "identified-line",  23 },
    { "800-call",         24 },
    { "coin-line",        27 },
    { "restricted-hotel", 68 },
    { "test-call-2",      95 },
    { 0, 0 }
};

#define MAKE_PARAM(p,s,a,d,t) { SS7MsgISUP::p,s,#p,a,d,t }
static const IsupParam s_paramDefs[] = {
//             name                          len decoder        encoder        table                  References

    // Standard parameters, references to ITU Q.763
    MAKE_PARAM(AccessDeliveryInformation,      1,decodeFlags,   encodeFlags,   s_flags_accdelinfo),   // 3.2
    MAKE_PARAM(AccessTransport,                0,0,             0,             0),                    // 3.3
    MAKE_PARAM(AutomaticCongestionLevel,       1,decodeInt,     encodeInt,     0),                    // 3.4
    MAKE_PARAM(BackwardCallIndicators,         2,decodeFlags,   encodeFlags,   s_flags_bkcallind),    // 3.5
    MAKE_PARAM(CallDiversionInformation,       1,decodeFlags,   encodeFlags,   s_flags_calldivinfo),  // 3.6
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
    MAKE_PARAM(FacilityIndicator,              1,0,             0,             0),                    // 3.22
    MAKE_PARAM(ForwardCallIndicators,          2,decodeFlags,   encodeFlags,   s_flags_fwcallind),    // 3.23
    MAKE_PARAM(GenericDigits,                  0,0,             0,             0),                    // 3.24
    MAKE_PARAM(GenericNotification,            0,decodeNotif,   encodeNotif,   s_dict_notifications), // 3.25
    MAKE_PARAM(GenericNumber,                  0,decodeDigits,  encodeDigits,  0),                    // 3.26
    MAKE_PARAM(GenericReference,               0,0,             0,             0),                    // 3.27
    MAKE_PARAM(InformationIndicators,          2,0,             0,             0),                    // 3.28
    MAKE_PARAM(InformationRequestIndicators,   2,0,             0,             0),                    // 3.29
    MAKE_PARAM(LocationNumber,                 0,decodeDigits,  encodeDigits,  0),                    // 3.30
    MAKE_PARAM(MCID_RequestIndicator,          1,decodeFlags,   encodeFlags,   s_flags_mcid),         // 3.31
    MAKE_PARAM(MCID_ResponseIndicator,         1,decodeFlags,   encodeFlags,   s_flags_mcid),         // 3.32
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
    MAKE_PARAM(RedirectionInformation,         0,decodeRedir,   encodeRedir,   0),                    // 3.45
    MAKE_PARAM(RedirectionNumber,              0,decodeDigits,  encodeDigits,  0),                    // 3.46
    MAKE_PARAM(RedirectionNumberRestriction,   0,0,             0,             0),                    // 3.47
    MAKE_PARAM(RemoteOperations,               0,0,             0,             0),                    // 3.48
    MAKE_PARAM(ServiceActivation,              0,0,             0,             0),                    // 3.49
    MAKE_PARAM(SignallingPointCode,            0,0,             0,             0),                    // 3.50
    MAKE_PARAM(SubsequentNumber,               0,decodeSubseq,  encodeSubseq,  0),                    // 3.51
    MAKE_PARAM(SuspendResumeIndicators,        1,0,             0,             0),                    // 3.52
    MAKE_PARAM(TransitNetworkSelection,        0,0,             0,             0),                    // 3.53
    MAKE_PARAM(TransmissionMediumRequirement,  1,decodeInt,     encodeInt,     s_dict_mediumReq),     // 3.54
    MAKE_PARAM(TransMediumRequirementPrime,    1,decodeInt,     encodeInt,     s_dict_mediumReq),     // 3.55
    MAKE_PARAM(TransmissionMediumUsed,         1,decodeInt,     encodeInt,     s_dict_mediumReq),     // 3.56
    MAKE_PARAM(UserServiceInformation,         0,decodeUSI,     encodeUSI,     0),                    // 3.57  Q.931-4.5.5
    MAKE_PARAM(UserServiceInformationPrime,    0,0,             0,             0),                    // 3.58
    MAKE_PARAM(UserTeleserviceInformation,     0,0,             0,             0),                    // 3.59
    MAKE_PARAM(UserToUserIndicators,           0,0,             0,             0),                    // 3.60
    MAKE_PARAM(UserToUserInformation,          0,0,             0,             0),                    // 3.61
    MAKE_PARAM(CCSScallIndication,             1,0,             0,             0),                    // 3.63
    MAKE_PARAM(ForwardGVNS,                    0,0,             0,             0),                    // 3.66
    MAKE_PARAM(BackwardGVNS,                   0,0,             0,             0),                    // 3.62
    MAKE_PARAM(CalledINNumber,                 0,decodeDigits,  encodeDigits,  0),                    // 3.73
    MAKE_PARAM(UID_ActionIndicators,           0,0,             0,             0),                    // 3.78
    MAKE_PARAM(UID_CapabilityIndicators,       0,0,             0,             0),                    // 3.79
    MAKE_PARAM(RedirectCapability,             0,0,             0,             0),                    // 3.96
    MAKE_PARAM(RedirectCounter,                0,0,             0,             0),                    // 3.97
    MAKE_PARAM(CCNRpossibleIndicator,          0,0,             0,             0),                    // 3.83
    MAKE_PARAM(PivotRoutingIndicators,         0,0,             0,             0),                    // 3.85
    MAKE_PARAM(CalledDirectoryNumber,          0,0,             0,             0),                    // 3.86
    MAKE_PARAM(OriginalCalledINNumber,         0,0,             0,             0),                    // 3.87
    MAKE_PARAM(CallingGeodeticLocation,        0,0,             0,             0),                    // 3.88
    MAKE_PARAM(HTR_Information,                0,0,             0,             0),                    // 3.89
    MAKE_PARAM(NetworkRoutingNumber,           0,0,             0,             0),                    // 3.90
    MAKE_PARAM(QueryOnReleaseCapability,       0,0,             0,             0),                    // 3.91
    MAKE_PARAM(PivotStatus,                    0,0,             0,             0),                    // 3.92
    MAKE_PARAM(PivotCounter,                   0,0,             0,             0),                    // 3.93
    MAKE_PARAM(PivotRoutingForwardInformation, 0,0,             0,             0),                    // 3.94
    MAKE_PARAM(PivotRoutingBackInformation,    0,0,             0,             0),                    // 3.95
    MAKE_PARAM(RedirectStatus,                 0,0,             0,             0),                    // 3.98
    MAKE_PARAM(RedirectForwardInformation,     0,0,             0,             0),                    // 3.99
    MAKE_PARAM(RedirectBackwardInformation,    0,0,             0,             0),                    // 3.100
    MAKE_PARAM(NumberPortabilityInformation,   0,decodeNotif,   encodeNotif,   s_dict_portability),   // 3.101
    // No references
    MAKE_PARAM(ApplicationTransport,           0,decodeAPT,     encodeAPT,     0),                    // 3.82
    MAKE_PARAM(BusinessGroup,                  0,0,             0,             0),                    //
    MAKE_PARAM(CallModificationIndicators,     0,0,             0,             0),                    //
    MAKE_PARAM(CarrierIdentification,          0,0,             0,             0),                    //
    MAKE_PARAM(CircuitIdentificationName,      0,0,             0,             0),                    //
    MAKE_PARAM(CarrierSelectionInformation,    0,0,             0,             0),                    //
    MAKE_PARAM(ChargeNumber,                   0,0,             0,             0),                    //
    MAKE_PARAM(CircuitAssignmentMap,           0,0,             0,             0),                    //
    MAKE_PARAM(CircuitGroupCharactIndicator,   1,decodeFlags,   encodeFlags,   s_flags_ansi_cgci),    // T1.113 ??
    MAKE_PARAM(CircuitValidationRespIndicator, 1,decodeFlags,   encodeFlags,   s_flags_ansi_cvri),    // T1.113 ??
    MAKE_PARAM(CommonLanguage,                 0,0,             0,             0),                    //
    MAKE_PARAM(CUG_CheckResponseIndicators,    0,0,             0,             0),                    //
    MAKE_PARAM(Egress,                         0,0,             0,             0),                    //
    MAKE_PARAM(FacilityInformationIndicators,  0,0,             0,             0),                    //
    MAKE_PARAM(FreephoneIndicators,            0,0,             0,             0),                    //
    MAKE_PARAM(GenericName,                    0,decodeName,    encodeName,    0),                    //
    MAKE_PARAM(HopCounter,                     1,decodeInt,     encodeInt,     0),                    // 3.80
    MAKE_PARAM(Index,                          0,0,             0,             0),                    //
    MAKE_PARAM(Jurisdiction,                   0,0,             0,             0),                    //
    MAKE_PARAM(MLPP_Precedence,                0,0,             0,             0),                    //
    MAKE_PARAM(NetworkTransport,               0,0,             0,             0),                    //
    MAKE_PARAM(NotificationIndicator,          0,0,             0,             0),                    //
    MAKE_PARAM(OperatorServicesInformation,    0,0,             0,             0),                    //
    MAKE_PARAM(OriginatingLineInformation,     1,decodeInt,     encodeInt,     s_dict_oli),           //
    MAKE_PARAM(OutgoingTrunkGroupNumber,       0,0,             0,             0),                    //
    MAKE_PARAM(Precedence,                     0,0,             0,             0),                    //
    MAKE_PARAM(ServiceCodeIndicator,           0,0,             0,             0),                    //
    MAKE_PARAM(SpecialProcessingRequest,       0,0,             0,             0),                    //
    MAKE_PARAM(TransactionRequest,             0,0,             0,             0),                    //
    // National use (UK-ISUP), references to NICC ND 1007 2001/07
    MAKE_PARAM(NationalForwardCallIndicators,          2,decodeFlags,   encodeFlags,   s_flags_nfci), // 3.2.1
    MAKE_PARAM(NationalForwardCallIndicatorsLinkByLink,0,0,             0,             0),            // 3.2.2
    MAKE_PARAM(PresentationNumber,                     0,decodeDigits,  encodeDigits,  0),            // 3.2.3
    MAKE_PARAM(LastDivertingLineIdentity,              0,decodeDigits,  encodeDigits,  0),            // 3.2.4
    MAKE_PARAM(PartialCLI,                             0,0,             0,             0),            // 3.2.5
    MAKE_PARAM(CalledSubscribersBasicServiceMarks,     0,0,             0,             0),            // 3.2.6
    MAKE_PARAM(CallingSubscribersBasicServiceMarks,    0,0,             0,             0),            // 3.2.7
    MAKE_PARAM(CallingSubscribersOriginatingFacilMarks,0,0,             0,             0),            // 3.2.8
    MAKE_PARAM(CalledSubscribersTerminatingFacilMarks, 0,0,             0,             0),            // 3.2.9
    MAKE_PARAM(NationalInformationRequestIndicators,   0,0,             0,             0),            // 3.2.10
    MAKE_PARAM(NationalInformationIndicators,          0,0,             0,             0),            // 3.2.11
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
    { SS7MsgISUP::SUS, true,
	{
	    SS7MsgISUP::SuspendResumeIndicators,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::RES, true,
	{
	    SS7MsgISUP::SuspendResumeIndicators,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::INR, true,
	{
	    SS7MsgISUP::InformationRequestIndicators,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::INF, true,
	{
	    SS7MsgISUP::InformationIndicators,
	SS7MsgISUP::EndOfParameters,
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
    { SS7MsgISUP::CQR, false,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::RangeAndStatus,
	    SS7MsgISUP::CircuitStateIndicator,
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
    // user part test and response
    { SS7MsgISUP::UPT, true,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::UPA, true,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    // application transport
    { SS7MsgISUP::APM, true,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    // facility
    { SS7MsgISUP::FACR, true,
	{
	    SS7MsgISUP::FacilityIndicator,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::FAA, true,
	{
	    SS7MsgISUP::FacilityIndicator,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::FRJ, true,
	{
	    SS7MsgISUP::FacilityIndicator,
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::CauseIndicators,
	SS7MsgISUP::EndOfParameters
	}
    },
    // miscellaneous
    { SS7MsgISUP::USR, true,
	{
	SS7MsgISUP::EndOfParameters,
	    SS7MsgISUP::UserToUserInformation,
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
    { SS7MsgISUP::RLC, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::EXM, true,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CVT, false,
	{
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::CVR, true,
	{
	    SS7MsgISUP::CircuitValidationRespIndicator,
	    SS7MsgISUP::CircuitGroupCharactIndicator,
	SS7MsgISUP::EndOfParameters,
	SS7MsgISUP::EndOfParameters
	}
    },
    { SS7MsgISUP::Unknown, false, { SS7MsgISUP::EndOfParameters } }
};

// Descriptor for decoding of compatibility parameters of unsupported messages
//  with only optional parameters (all new messages should be like this)
static const MsgParams s_compatibility = {
    SS7MsgISUP::Unknown, true,
    {
    SS7MsgISUP::EndOfParameters,
    SS7MsgISUP::EndOfParameters
    }
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
    DDebug(isup,DebugAll,"encodeParam (mand) (%p,%p,%p,%p) type=0x%02x, size=%u, name='%s'",
	&msu,param,params,buf,param->type,param->size,param->name);
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
    DDebug(isup,DebugAll,"encodeParam (opt) (%p,%p,%p,%p) type=0x%02x, size=%u, name='%s'",
	&msu,param,val,extra,param->type,param->size,param->name);
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

// Hexify a list of isup parameter values/names
static void hexifyIsupParams(String& s, const String& list)
{
    if (!list)
	return;
    ObjList* l = list.split(',',false);
    unsigned int len = l->count();
    if (len) {
	unsigned char* buf = new unsigned char[len];
	len = 0;
	for (ObjList* o = l->skipNull(); o; o = o->skipNext()) {
	    String* str = static_cast<String*>(o->get());
	    int val = str->toInteger(-1);
	    if (val < 0) {
		const IsupParam* p = getParamDesc(*str);
		if (p)
		    val = p->type;
	    }
	    if (val >= 0 && val < 256) {
		// avoid duplicates
		for (unsigned int i = 0; i < len; i++) {
		    if ((unsigned char)val == buf[i]) {
			val = -1;
			break;
		    }
		}
		if (val >= 0)
		    buf[len++] = (unsigned char)val;
	    }
	}
	if (len)
	    s.hexify(buf,len,' ');
	delete[] buf;
    }
    TelEngine::destruct(l);
}

// Check if an unhandled messages has only optional parameters
#define MAKE_CASE(x) case SS7MsgISUP::x:
static bool hasOptionalOnly(SS7MsgISUP::Type msg)
{
    switch (msg) {
	MAKE_CASE(IAM)
	MAKE_CASE(SAM)
	MAKE_CASE(INR)
	MAKE_CASE(INF)
	MAKE_CASE(COT)
	MAKE_CASE(ACM)
	MAKE_CASE(CON)
	MAKE_CASE(REL)
	MAKE_CASE(SUS)
	MAKE_CASE(RES)
	MAKE_CASE(CCR)
	MAKE_CASE(RSC)
	MAKE_CASE(BLK)
	MAKE_CASE(UBL)
	MAKE_CASE(BLA)
	MAKE_CASE(UBA)
	MAKE_CASE(GRS)
	MAKE_CASE(CGB)
	MAKE_CASE(CGU)
	MAKE_CASE(CGA)
	MAKE_CASE(CUA)
	MAKE_CASE(FACR)
	MAKE_CASE(FAA)
	MAKE_CASE(FRJ)
	MAKE_CASE(LPA)
	MAKE_CASE(PAM)
	MAKE_CASE(GRA)
	MAKE_CASE(CQM)
	MAKE_CASE(CQR)
	MAKE_CASE(CPR)
	MAKE_CASE(USR)
	MAKE_CASE(UEC)
	MAKE_CASE(CNF)
	MAKE_CASE(OLM)
	    return false;
	default:
	    return true;
    }
}
#undef MAKE_CASE

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

void SS7MsgISUP::toString(String& dest, const SS7Label& label, bool params,
	const void* raw, unsigned int rawLen) const
{
    const char* enclose = "\r\n-----";
    dest = enclose;
    dest << "\r\n" << name() << " [cic=" << m_cic << " label=" << label << ']';
    if (raw && rawLen) {
	String tmp;
	tmp.hexify((void*)raw,rawLen,' ');
	dest << "  " << tmp;
    }
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


/**
 * Helper functions used to transmit responses
 */

// Push down the protocol stack a RLC (Release Complete) message
// @param msg Optional received message to copy release parameters. Ignored if reason is valid
static int transmitRLC(SS7ISUP* isup, unsigned int cic, const SS7Label& label, bool recvLbl,
    const char* reason = 0, const char* diagnostic = 0, const char* location = 0)
{
    SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::RLC,cic);
    if (!TelEngine::null(reason)) {
	m->params().addParam("CauseIndicators",reason);
	m->params().addParam("CauseIndicators.location",location,false);
	m->params().addParam("CauseIndicators.diagnostic",diagnostic,false);
    }
    return isup->transmitMessage(m,label,recvLbl);
}

// Push down the protocol stack a CNF (Confusion) message
static int transmitCNF(SS7ISUP* isup, unsigned int cic, const SS7Label& label, bool recvLbl,
    const char* reason, const char* diagnostic = 0, const char* location = 0)
{
    SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CNF,cic);
    if (reason)
	m->params().addParam("CauseIndicators",reason);
    if (!location)
	location = isup->location();
    m->params().addParam("CauseIndicators.location",location,false);
    m->params().addParam("CauseIndicators.diagnostic",diagnostic,false);
    return isup->transmitMessage(m,label,recvLbl);
}


// Utility used to check for called number completion
static inline bool isCalledIncomplete(const NamedList& l, const String& p = "CalledPartyNumber")
{
    return !l[p].endsWith(".");
}

// Fill call release or cnf flags from message compatibility info
static void getMsgCompat(SS7MsgISUP* msg, bool& release, bool& cnf)
{
    if (!msg)
	return;
    String* msgCompat = msg->params().getParam(YSTRING("MessageCompatInformation"));
    if (msgCompat) {
	ObjList* l = msgCompat->split(',',false);
	// Use a while() to make sure the list is destroyed
	do {
	    release = (0 != l->find("release"));
	    if (release)
		break;
	    // Discard the message (no pass on). Check if CNF should be sent
	    if (l->find("discard")) {
		cnf = (0 != l->find("cnf"));
		break;
	    }
	    // Pass on set: we didn't passed on the message. Check REL/CNF
	    release = (0 != l->find("nopass-release"));
	    cnf = !release && l->find("cnf");
	} while(false);
	TelEngine::destruct(l);
    }
    else
	cnf = true;
}

static void setCallsTerminate(ObjList& lst, bool gracefully, const char* reason = 0,
    const char* diagnostic = 0, const char* location = 0)
{
    for (ObjList* o = lst.skipNull(); o; o = o->skipNext()) {
	SS7ISUPCall* call = static_cast<SS7ISUPCall*>(o->get());
	call->setTerminate(gracefully,reason,diagnostic,location);
    }
}


/**
 * SS7ISUPCall
 */
SS7ISUPCall::SS7ISUPCall(SS7ISUP* controller, SignallingCircuit* cic,
	const SS7PointCode& local, const SS7PointCode& remote, bool outgoing,
	int sls, const char* range, bool testCall)
    : SignallingCall(controller,outgoing),
    m_state(Null),
    m_testCall(testCall),
    m_circuit(cic),
    m_cicRange(range),
    m_terminate(false),
    m_gracefully(true),
    m_circuitChanged(false),
    m_circuitTesting(false),
    m_inbandAvailable(false),
    m_replaceCounter(3),
    m_iamMsg(0),
    m_sgmMsg(0),
    m_relMsg(0),
    m_sentSamDigits(0),
    m_relTimer(300000),                  // Q.764: T5  - 5..15 minutes
    m_iamTimer(ISUP_T7_DEFVAL),          // Setup, Testing: Q.764: T7  - 20..30 seconds
                                         // Releasing: Q.764: T1: 15..60 seconds
    m_sgmRecvTimer(ISUP_T34_DEFVAL),     // Q.764: T34 - 2..4 seconds
    m_contTimer(ISUP_T27_DEFVAL),        // Q.764: T27 - 4 minutes
    m_anmTimer(0)                        // Q.764 T9 Q.118: 1.5 - 3 minutes, not always used
{
    if (!(controller && m_circuit)) {
	Debug(isup(),DebugWarn,
	    "SS7ISUPCall(%u). No call controller or circuit. Terminate [%p]",
	    id(),this);
	setTerminate(true,m_circuit ? "temporary-failure" : "congestion");
	return;
    }
    isup()->setLabel(m_label,local,remote,sls);
    if (isup()->m_t7Interval)
	m_iamTimer.interval(isup()->m_t7Interval);
    if (isup()->m_t9Interval)
	m_anmTimer.interval(isup()->m_t9Interval);
    if (isup()->m_t27Interval)
	m_contTimer.interval(isup()->m_t27Interval);
    if (isup()->m_t34Interval)
	m_sgmRecvTimer.interval(isup()->m_t34Interval);
    m_replaceCounter = isup()->m_replaceCounter;
    if (isup()->debugAt(DebugAll)) {
	String tmp;
	tmp << m_label;
	Debug(isup(),DebugAll,"Call(%u) direction=%s routing-label=%s range=%s [%p]",
	    id(),(outgoing ? "outgoing" : "incoming"),tmp.c_str(),m_cicRange.safe(),this);
    }
}

SS7ISUPCall::~SS7ISUPCall()
{
    TelEngine::destruct(m_iamMsg);
    TelEngine::destruct(m_sgmMsg);
    const char* timeout = 0;
    if (m_relTimer.started())
	timeout = " (release timed out)";
    else if (m_contTimer.started())
	timeout = " (T27 timed out)";
    releaseComplete(true,0,0,0 != timeout);
    Debug(isup(),!timeout ? DebugAll : DebugNote,
	"Call(%u) destroyed with reason='%s'%s [%p]",
	id(),m_reason.safe(),TelEngine::c_safe(timeout),this);
    TelEngine::destruct(m_relMsg);
    if (controller()) {
	if (!timeout)
	    controller()->releaseCircuit(m_circuit);
	else
	    isup()->startCircuitReset(m_circuit,m_relTimer.started() ? "T5" : "T16");
    }
    else
	TelEngine::destruct(m_circuit);
}

// Stop waiting for a SGM (Segmentation) message when another message is
//  received by the controller
void SS7ISUPCall::stopWaitSegment(bool discard)
{
    Lock mylock(this);
    if (!m_sgmMsg)
	return;
    m_sgmRecvTimer.stop();
    if (discard)
	TelEngine::destruct(m_sgmMsg);
}

// Helper functions called in getEvent
inline static bool timeout(SS7ISUP* isup, SS7ISUPCall* call, SignallingTimer& timer,
	const Time& when, const char* req, bool stop = true)
{
    if (!timer.timeout(when.msec()))
	return false;
    if (stop)
	timer.stop();
    Debug(isup,DebugNote,"Call(%u). %s timed out [%p]",call->id(),req,call);
    return true;
}

// Get an event from this call
SignallingEvent* SS7ISUPCall::getEvent(const Time& when)
{
    Lock mylock(this,SignallingEngine::maxLockWait());
    if (m_lastEvent || m_state == Released || !mylock.locked())
	return 0;
    SS7MsgISUP* msg = 0;
    while (true) {
	if (m_terminate) {
	    if (m_state < Releasing && m_state > Null)
		if (m_gracefully)
		    m_lastEvent = release();
		else
		    m_lastEvent = releaseComplete(false,0);
	    else if (m_state == Null || m_state == Released) {
		m_gracefully = false;
		m_lastEvent = releaseComplete(false,0);
	    }
	    m_terminate = false;
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
	if (msg && validMsgState(false,msg->type(),(msg->params().getParam(YSTRING("BackwardCallIndicators")) != 0)))
	    switch (msg->type()) {
		case SS7MsgISUP::IAM:
		case SS7MsgISUP::CCR:
		case SS7MsgISUP::COT:
		case SS7MsgISUP::ACM:
		case SS7MsgISUP::EXM:
		case SS7MsgISUP::CPR:
		case SS7MsgISUP::ANM:
		case SS7MsgISUP::CON:
		case SS7MsgISUP::CRG:
		    m_sgmMsg = msg;
		    {
			const char* sgmParam = "OptionalBackwardCallIndicators";
			if (msg->type() == SS7MsgISUP::IAM) {
			    copyParamIAM(msg);
			    setOverlapped(isCalledIncomplete(msg->params()));
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
		    setOverlapped(isCalledIncomplete(msg->params(),"SubsequentNumber"));
		    msg->params().addParam("tone",msg->params().getValue(YSTRING("SubsequentNumber")));
		    msg->params().addParam("dialing",String::boolText(true));
		    m_lastEvent = new SignallingEvent(SignallingEvent::Info,msg,this);
		    break;
		case SS7MsgISUP::RLC:
		    m_gracefully = false;
		    if (m_state < Releasing) {
			setReason(0,msg);
			m_location = isup()->location();
			m_lastEvent = release(0,msg);
		    }
		    else {
		        m_relTimer.stop();
			m_lastEvent = releaseComplete(false,msg);
		    }
		    break;
		case SS7MsgISUP::REL:
		    if (m_state < Releasing) {
		        m_relTimer.stop();
			m_lastEvent = releaseComplete(false,msg);
		    }
		    else
			transmitRLC(isup(),msg->cic(),m_label,false);
		    break;
		case SS7MsgISUP::SGM:
		    DDebug(isup(),DebugInfo,"Call(%u). Received late 'SGM' [%p]",id(),this);
		    break;
		case SS7MsgISUP::SUS:
		    m_lastEvent = new SignallingEvent(SignallingEvent::Suspend,msg,this);
		    break;
		case SS7MsgISUP::RES:
		    m_lastEvent = new SignallingEvent(SignallingEvent::Resume,msg,this);
		    break;
		case SS7MsgISUP::APM:
		    m_lastEvent = new SignallingEvent(SignallingEvent::Generic,msg,this);
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
	    case Testing:
	    case Setup:
		if (timeout(isup(),this,m_iamTimer,when,"IAM")) {
		    m_contTimer.stop();
		    if (m_circuitTesting) {
			if (m_iamMsg)
			    setReason("bearer-cap-not-available",0);
			else {
			    setTerminate(true,"bearer-cap-not-available");
			    break;
			}
		    }
		    else
			setReason("timeout",0);
		    m_lastEvent = release();
		    break;
		}
		if (timeout(isup(),this,m_contTimer,when,"T27",false)) {
		    m_gracefully = false;
		    m_lastEvent = releaseComplete(false,0,0,true);
		}
		break;
	    case Releasing:
		if (timeout(isup(),this,m_relTimer,when,"REL",false))
		    m_lastEvent = releaseComplete(false,0,"noresponse",true);
		else if (timeout(isup(),this,m_iamTimer,when,"T1")) {
		    m_iamTimer.stop();
		    m_iamTimer.start(when.msec());
		    transmitREL();
		}
		break;
	    default:
		if (outgoing() && m_anmTimer.started() && m_state >= Accepted &&
		    m_state < Answered && timeout(isup(),this,m_anmTimer,when,"T9")) {
		    setReason("noresponse",0,0,isup()->location());
		    m_lastEvent = release();
		}
	}
    }
    // Reset overlapped if our state is greater then Setup
    if (m_state > Setup)
	setOverlapped(false,false);
    // Check circuit event
    if (!m_lastEvent && m_circuit) {
	SignallingCircuitEvent* cicEvent = m_circuit->getEvent(when);
	if (cicEvent) {
	    if (isup())
		m_lastEvent = isup()->processCircuitEvent(cicEvent,this);
	    TelEngine::destruct(cicEvent);
	}
    }
    if (m_lastEvent)
	XDebug(isup(),DebugNote,"Call(%u). Raising event (%p,'%s') [%p]",
	    id(),m_lastEvent,m_lastEvent->name(),this);

    return m_lastEvent;
}

// Helper that copies all parameters starting with a capital letter
static void copyUpper(NamedList& dest, const NamedList& src)
{
    static const Regexp r("^[A-Z][A-Za-z0-9_.]\\+$");
    unsigned int n = src.length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* p = src.getParam(i);
	if (!p || !r.matches(p->name()))
	    continue;
	dest.setParam(p->name(),*p);
    }
}

// Send an event to this call
bool SS7ISUPCall::sendEvent(SignallingEvent* event)
{
    Lock mylock(this);
    if (!event)
	return false;
    if (m_terminate || m_state == Released) {
	mylock.drop();
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
		    setTerminate(true,"temporary-failure");
		    break;
		}
		m_iamMsg = new SS7MsgISUP(SS7MsgISUP::IAM,id());
		copyParamIAM(m_iamMsg,true,event->message());
		// Update overlap
		String* called = m_iamMsg->params().getParam(YSTRING("CalledPartyNumber"));
		if (called && (called->length() > isup()->m_maxCalledDigits)) {
		    // Longer than maximum digits allowed - send remainder with SAM
		    m_samDigits = called->substr(isup()->m_maxCalledDigits);
		    *called = called->substr(0,isup()->m_maxCalledDigits);
		    setOverlapped(true);
		}
		else
		    setOverlapped(isCalledIncomplete(m_iamMsg->params()));
		result = transmitIAM();
	    }
	    break;
	case SignallingEvent::Progress:
	case SignallingEvent::Ringing:
	    if (validMsgState(true,SS7MsgISUP::CPR)) {
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CPR,id());
		m->params().addParam("EventInformation",
		    event->type() == SignallingEvent::Ringing ? "ringing": "progress");
		bool inband = m_inbandAvailable;
		if (event->message()) {
		    copyUpper(m->params(),event->message()->params());
		    m_inbandAvailable = m_inbandAvailable ||
			event->message()->params().getBoolValue(YSTRING("earlymedia"));
		    inband = event->message()->params().getBoolValue(YSTRING("send-inband"),m_inbandAvailable);
		}
		if (inband && !outgoing())
		    SignallingUtils::appendFlag(m->params(),"OptionalBackwardCallIndicators","inband");
		m_state = Ringing;
		mylock.drop();
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Accept:
	    if (validMsgState(true,SS7MsgISUP::ACM)) {
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::ACM,id());
		bool inband = m_inbandAvailable;
		if (event->message()) {
		    copyUpper(m->params(),event->message()->params());
		    m_inbandAvailable = m_inbandAvailable ||
			event->message()->params().getBoolValue(YSTRING("earlymedia"));
		    inband = event->message()->params().getBoolValue(YSTRING("send-inband"),m_inbandAvailable);
		}
		if (inband && !outgoing())
		    SignallingUtils::appendFlag(m->params(),"OptionalBackwardCallIndicators","inband");
		m_state = Accepted;
		mylock.drop();
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Answer:
	    if (validMsgState(true,SS7MsgISUP::ANM)) {
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::ANM,id());
		if (event->message())
		    copyUpper(m->params(),event->message()->params());
		m_state = Answered;
		mylock.drop();
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Release:
	    if (validMsgState(true,SS7MsgISUP::REL)) {
		release(event);
		result = true;
	    }
	    break;
	case SignallingEvent::Generic:
	    if (event->message()) {
		const String& oper = event->message()->params()[YSTRING("operation")];
		if (oper == "charge") {
		    if (!validMsgState(true,SS7MsgISUP::CRG))
			break;
		    SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CRG,id());
		    copyUpper(m->params(),event->message()->params());
		    mylock.drop();
		    result = transmitMessage(m);
		    break;
		}
		if (oper != "transport")
		    break;
		if (!validMsgState(true,SS7MsgISUP::APM))
		    break;
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::APM,id());
		copyUpper(m->params(),event->message()->params());
		mylock.drop();
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Suspend:
	    if (event->message()) {
		if (!validMsgState(true,SS7MsgISUP::SUS))
		    break;
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::SUS,id());
		copyUpper(m->params(),event->message()->params());
		mylock.drop();
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Resume:
	    if (event->message()) {
		if (!validMsgState(true,SS7MsgISUP::RES))
		    break;
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::RES,id());
		copyUpper(m->params(),event->message()->params());
		mylock.drop();
		result = transmitMessage(m);
	    }
	    break;
	case SignallingEvent::Info:
	    if (validMsgState(true,SS7MsgISUP::SAM)) {
		mylock.drop();
		transmitSAM(event->message()->params().getValue(YSTRING("tone")));
		result = true;
		break;
	    }
	//case SignallingEvent::Message:
	//case SignallingEvent::Transfer:
	case SignallingEvent::Charge:
	    if (event->message()) {
		if (!validMsgState(true,SS7MsgISUP::CRG))
		    break;
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CRG,id());
		copyUpper(m->params(),event->message()->params());
		mylock.drop();
		result = transmitMessage(m);
	    }
	    break;
	default:
	    DDebug(isup(),DebugStub,
		"Call(%u). sendEvent not implemented for '%s' [%p]",
		id(),event->name(),this);
    }
    // Reset overlapped if our state is greater then Setup
    if (m_state > Setup)
	setOverlapped(false,false);
    XDebug(isup(),DebugAll,"Call(%u). Event (%p,'%s') sent. Result: %s [%p]",
	id(),event,event->name(),String::boolText(result),this);
    mylock.drop();
    delete event;
    return result;
}

// Get reserved circuit or this object
void* SS7ISUPCall::getObject(const String& name) const
{
    if (name == YSTRING("SignallingCircuit"))
	return m_circuit;
    if (name == YSTRING("SS7ISUPCall"))
	return (void*)this;
    return SignallingCall::getObject(name);
}

// Check if the circuit can be replaced
// Returns true unless the counter is already zero
bool SS7ISUPCall::canReplaceCircuit()
{
    if (m_replaceCounter <= 0)
	return false;
    m_replaceCounter--;
    return true;
}

// Replace the circuit reserved for this call. Release the already reserved circuit.
// Retransmit the initial IAM request on success.
// On failure set the termination flag and release the new circuit if valid
bool SS7ISUPCall::replaceCircuit(SignallingCircuit* circuit, SS7MsgISUP* msg)
{
    Lock mylock(this);
    clearQueue();
    if (m_state > Setup || !circuit || !outgoing()) {
        Debug(isup(),DebugNote,"Call(%u). Failed to replace circuit [%p]",id(),this);
	m_iamTimer.stop();
	if (controller()) {
	    controller()->releaseCircuit(m_circuit);
	    controller()->releaseCircuit(circuit);
	}
	setTerminate(false,"congestion");
	TelEngine::destruct(msg);
	return false;
    }
    transmitMessage(msg);
    unsigned int oldId = id();
    if (controller())
	controller()->releaseCircuit(m_circuit);
    m_circuit = circuit;
    Debug(isup(),DebugNote,"Call(%u). Circuit replaced by %u [%p]",oldId,id(),this);
    m_circuitChanged = true;
    return transmitIAM();
}

// Stop timers. Send a RLC (Release Complete) message if it should terminate gracefully
// Decrease the object's refence count and generate a Release event if not final
// @param final True if called from destructor
// @param msg Received message with parameters if any
// @param reason Optional release reason
SignallingEvent* SS7ISUPCall::releaseComplete(bool final, SS7MsgISUP* msg, const char* reason,
    bool timeout)
{
    if (timeout)
	m_gracefully = false;
    m_iamTimer.stop();
    setReason(reason,msg);
    stopWaitSegment(true);
    if (m_state == Released)
	return 0;
    if (isup() && m_gracefully) {
	int sls = transmitRLC(isup(),id(),m_label,false);
	if (sls != -1 && m_label.sls() == 255)
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
inline void param(NamedList& dest, NamedList& src, const String& destParam,
	const String& srcParam, const char* defVal)
{
    const char* val = src.getValue(srcParam,src.getValue(destParam,defVal));
    if ((val != defVal) || !dest.getParam(destParam))
	dest.setParam(destParam,val);
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
	copyUpper(dest,src);
	param(dest,src,"CalledPartyNumber","called","");
	param(dest,src,"CalledPartyNumber.inn","inn",String::boolText(isup()->m_inn));
	param(dest,src,"CalledPartyNumber.nature","callednumtype",isup()->m_numType);
	param(dest,src,"CalledPartyNumber.plan","callednumplan",isup()->m_numPlan);
	param(dest,src,"CallingPartyCategory","callercategory",isup()->m_callerCat);
	param(dest,src,"CallingPartyNumber","caller","");
	param(dest,src,"CallingPartyNumber.nature","callernumtype",isup()->m_numType);
	param(dest,src,"CallingPartyNumber.plan","callernumplan",isup()->m_numPlan);
	param(dest,src,"CallingPartyNumber.restrict","callerpres",isup()->m_numPresentation);
	param(dest,src,"CallingPartyNumber.screened","callerscreening",isup()->m_numScreening);
	param(dest,src,"CallingPartyNumber.complete","complete","true");
	m_format = src.getValue(YSTRING("format"),isup()->format());
	dest.setParam("UserServiceInformation",m_format);
	return true;
    }
    // Incoming call
    m_format = dest.getValue(YSTRING("UserServiceInformation"),isup()->format());
    dest.setParam("format",m_format);
    dest.setParam("caller",dest.getValue(YSTRING("CallingPartyNumber")));
    //dest.setParam("callername",dest.getValue(""));
    dest.setParam("callernumtype",dest.getValue(YSTRING("CallingPartyNumber.nature")));
    dest.setParam("callernumplan",dest.getValue(YSTRING("CallingPartyNumber.plan")));
    dest.setParam("callerpres",dest.getValue(YSTRING("CallingPartyNumber.restrict")));
    dest.setParam("callerscreening",dest.getValue(YSTRING("CallingPartyNumber.screened")));
    dest.setParam("called",dest.getValue(YSTRING("CalledPartyNumber")));
    dest.setParam("callednumtype",dest.getValue(YSTRING("CalledPartyNumber.nature")));
    dest.setParam("callednumplan",dest.getValue(YSTRING("CalledPartyNumber.plan")));
    dest.setParam("inn",dest.getValue(YSTRING("CalledPartyNumber.inn")));
    if (m_label.sls() != 0xff)
	dest.setParam("sls",String((unsigned int)m_label.sls()));
    return true;
}

// If already releasing, set termination flag. Otherwise, send REL (Release) message
// @param event Event with the parameters. 0 if release is started on unspecified interworking
SignallingEvent* SS7ISUPCall::release(SignallingEvent* event, SS7MsgISUP* msg)
{
    m_iamTimer.stop();
    if (event)
	setReason(0,event->message());
    else
	setReason("interworking",0);
    stopWaitSegment(true);
    XDebug(isup(),DebugAll,"Call(%u). Releasing call with reason '%s' [%p]",
	id(),m_reason.safe(),this);
    if (!isup() || m_state >= Releasing) {
	m_terminate = true;
	return 0;
    }
    m_iamTimer.interval(isup() ? isup()->m_t1Interval : 1);
    m_relTimer.interval(isup() ? isup()->m_t5Interval : 1);
    m_iamTimer.start();
    m_relTimer.start();
    m_state = Releasing;
    transmitREL((event && event->message()) ? &(event->message()->params()) : 0);
    if (event)
	return 0;
    bool create = (msg == 0);
    if (create)
	msg = new SS7MsgISUP(SS7MsgISUP::REL,id());
    msg->params().setParam("reason",m_reason);
    SignallingEvent* ev = new SignallingEvent(SignallingEvent::Release,msg,this);
    // deref() msg if created here. If received, it will be deref()'d in getEvent()
    if (create)
        TelEngine::destruct(msg);
    return ev;
}

// Set termination reason from received text or message
void SS7ISUPCall::setReason(const char* reason, SignallingMessage* msg,
    const char* diagnostic, const char* location)
{
    if (!m_reason.null())
	return;
    if (reason) {
	m_reason = reason;
	m_diagnostic = diagnostic;
	m_location = location;
    }
    else if (msg) {
	m_reason = msg->params().getValue(YSTRING("CauseIndicators"),msg->params().getValue(YSTRING("reason")));
	m_diagnostic = msg->params().getValue(YSTRING("CauseIndicators.diagnostic"),diagnostic);
	m_location = msg->params().getValue(YSTRING("CauseIndicators.location"),location);
    }
}

// Accept send/receive messages in current state based on call direction
bool SS7ISUPCall::validMsgState(bool send, SS7MsgISUP::Type type, bool hasBkwCallInd)
{
    bool handled = true;
    switch (type) {
	case SS7MsgISUP::CCR:    // Continuity check
	    if (m_state == Testing && send == outgoing())
		return true;
	    // fall through
	case SS7MsgISUP::IAM:    // Initial address
	    if (m_state != Null || send != outgoing())
		break;
	    return true;
	case SS7MsgISUP::COT:    // Continuity
	    if (m_state != Testing || send != outgoing())
		break;
	    return true;
	case SS7MsgISUP::ACM:    // Address complete
	case SS7MsgISUP::EXM:    // Exit Message (ANSI)
	    if (m_state != Setup || send == outgoing())
		break;
	    return true;
	case SS7MsgISUP::CPR:    // Call progress
	    if (m_state < (hasBkwCallInd ? Setup : Accepted) || m_state >= Releasing)
		break;
	    return true;
	case SS7MsgISUP::CON:    // Connect
	    // CON can be sent/received on not accepted calls
	    if (m_state == Setup && send != outgoing())
		return true;
	case SS7MsgISUP::ANM:    // Answer
	    if (m_state < (hasBkwCallInd ? Setup : Accepted) || m_state >= Answered || send == outgoing())
		break;
	    return true;
	case SS7MsgISUP::SAM:    // Subsequent address
	    if (m_state != Setup || !m_overlap || send != outgoing())
		break;
	    return true;
	case SS7MsgISUP::REL:    // Release
	    if (send && m_state >= Releasing)
		break;
	    // fall through
	case SS7MsgISUP::RLC:    // Release complete
	case SS7MsgISUP::CRG:    // Charging
	    if (m_state == Null || m_state == Released)
		break;
	    return true;
	case SS7MsgISUP::SUS:    // Suspend
	case SS7MsgISUP::RES:    // Resume
	    if (m_state != Answered)
		break;
	    return true;
	case SS7MsgISUP::SGM:    // Segmentation
	case SS7MsgISUP::APM:    // Application Transport
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

// Connect or test the reserved circuit. Return false if it fails.
// Return true if this call is a signalling only one
bool SS7ISUPCall::connectCircuit(const char* special)
{
    bool ok = signalOnly();
    if (TelEngine::null(special))
	special = 0;
    if (m_circuit && !ok) {
	u_int64_t t = Time::msecNow();
	if (special) {
	    m_circuit->updateFormat(m_format,0);
	    ok = m_circuit->setParam("special_mode",special) &&
		m_circuit->status(SignallingCircuit::Special);
	}
	else
	    ok = m_circuit->connected() || m_circuit->connect(m_format);
	t = Time::msecNow() - t;
	if (t > 100) {
	    int level = DebugInfo;
	    if (t > 300)
		level = DebugMild;
	    else if (t > 200)
		level = DebugNote;
	    Debug(isup(),level,"Call(%u). Spent %u ms connecting circuit [%p]",
		id(),(unsigned int)t,this);
	}
#ifdef DEBUG
	else
	    Debug(isup(),DebugAll,"Call(%u). Spent %u ms connecting circuit [%p]",
		id(),(unsigned int)t,this);
#endif
    }
    if (!ok)
	Debug(isup(),DebugMild,"Call(%u). Circuit %s failed (format='%s')%s [%p]",
	    id(),(special ? special : "connect"),
	    m_format.safe(),(m_circuit ? "" : ". No circuit"),this);

    if (m_sgmMsg) {
	if (m_circuitChanged) {
	    m_sgmMsg->params().setParam("circuit-change","true");
	    m_circuitChanged = false;
	}
	m_sgmMsg->params().setParam("format",m_format);
    }
    return ok;
}

// Transmit the IAM message. Start IAM timer if not started
bool SS7ISUPCall::transmitIAM()
{
    if (!m_iamTimer.started())
	m_iamTimer.start();
    if (!m_iamMsg)
	return false;
    if (needsTesting(m_iamMsg)) {
	if (m_circuitTesting && !(isup() && isup()->m_continuity)) {
	    Debug(isup(),DebugWarn,"Call(%u). Continuity check requested but not configured [%p]",
		id(),this);
	    return false;
	}
	m_state = Testing;
	if (m_circuitTesting && !connectCircuit("test:" + isup()->m_continuity))
	    return false;
	Debug(isup(),DebugNote,"Call(%u). %s continuity check [%p]",
	    id(),(m_circuitTesting ? "Executing" : "Forwarding"),this);
    }
    else
	m_state = Setup;
    m_iamMsg->m_cic = id();
    m_iamMsg->ref();
    // Reset SAM digits: this might be a re-send
    m_sentSamDigits = 0;
    bool ok = transmitMessage(m_iamMsg);
    if (ok && m_overlap)
	transmitSAM();
    return ok;
}

// Transmit SAM digits
bool SS7ISUPCall::transmitSAM(const char* extra)
{
    if (!m_overlap)
	return false;
    m_samDigits << extra;
    while (m_samDigits.length() > m_sentSamDigits) {
	unsigned int send = m_samDigits.length() - m_sentSamDigits;
	if (send > isup()->m_maxCalledDigits)
	    send = isup()->m_maxCalledDigits;
	SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::SAM,id());
	String number = m_samDigits.substr(m_sentSamDigits,send);
	m->params().addParam("SubsequentNumber",number);
	bool complete = !isCalledIncomplete(m->params(),"SubsequentNumber");
	bool ok = transmitMessage(m);
	if (ok) {
	    m_sentSamDigits += send;
	    if (complete) {
		if (m_samDigits.length() > m_sentSamDigits)
		    Debug(isup(),DebugNote,
			"Call(%u). Completed number sending remaining='%s' [%p]",
			id(),m_samDigits.substr(m_sentSamDigits).c_str(),this);
		// Reset overlap sending
		setOverlapped(false);
		break;
	    }
	}
	else {
	    Debug(isup(),DebugNote,"Call(%u). Failed to send SAM with '%s' [%p]",
		id(),number.c_str(),this);
	    complete = false;
	    break;
	}
    }
    return true;
}

// (Re)transmit REL. Create and populate the message if needed
// Remember sls
bool SS7ISUPCall::transmitREL(const NamedList* params)
{
    if (!isup())
	return false;
    if (!m_relMsg) {
	m_relMsg = new SS7MsgISUP(SS7MsgISUP::REL,id());
	if (m_reason)
	    m_relMsg->params().addParam("CauseIndicators",m_reason);
	m_relMsg->params().addParam("CauseIndicators.diagnostic",m_diagnostic,false);
	m_relMsg->params().addParam("CauseIndicators.location",m_location,false);
	if (params)
	    copyUpper(m_relMsg->params(),*params);
    }
    // transmitMessage will dereference message so make sure we preserve it
    m_relMsg->ref();
    int sls = isup()->transmitMessage(m_relMsg,m_label,false);
    if (sls != -1 && m_label.sls() == 255)
	m_label.setSls(sls);
    return sls != -1;
}

bool SS7ISUPCall::needsTesting(const SS7MsgISUP* msg)
{
    if ((m_state >= Testing) || !msg)
	return false;
    const String* naci = msg->params().getParam(YSTRING("NatureOfConnectionIndicators"));
    if (!naci)
	return false;
    ObjList* list = naci->split(',',false);
    m_circuitTesting = (0 != list->find("cont-check-this"));
    bool checkIt = m_circuitTesting || (0 != list->find("cont-check-prev"));
    TelEngine::destruct(list);
    return checkIt;
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
	    COPY_PARAM(YSTRING("AccessTranport"))
	    COPY_PARAM(YSTRING("UserToUserInformation"))
	    COPY_PARAM(YSTRING("MessageCompatInformation"))
	    COPY_PARAM(YSTRING("GenericDigits"))
	    COPY_PARAM(YSTRING("GenericNotification"))
	    COPY_PARAM(YSTRING("GenericNumber"))
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
    switch (m_sgmMsg->type()) {
	case SS7MsgISUP::COT:
	    {
		const String* cont = m_sgmMsg->params().getParam(YSTRING("ContinuityIndicators"));
		bool ok = cont && (*cont == YSTRING("success"));
		if (ok) {
		    Debug(isup(),DebugNote,"Call(%u). Continuity check succeeded [%p]",
			id(),this);
		    m_circuitTesting = false;
		}
		else {
		    Debug(isup(),DebugWarn,"Call(%u). Continuity check failed [%p]",
			id(),this);
		    m_contTimer.start();
		    break;
		}
		if (!(ok && m_iamMsg)) {
		    m_lastEvent = new SignallingEvent(SignallingEvent::Info,m_sgmMsg,this);
		    break;
		}
	    }
	    TelEngine::destruct(m_sgmMsg);
	    m_sgmMsg = m_iamMsg;
	    m_iamMsg = 0;
	    // intentionally fall through
	case SS7MsgISUP::IAM:
	    if (needsTesting(m_sgmMsg)) {
		m_state = Testing;
		if (m_circuitTesting && !(isup() && isup()->m_continuity)) {
		    Debug(isup(),DebugWarn,"Call(%u). Continuity check requested but not configured [%p]",
			id(),this);
		    setTerminate(true,"service-not-implemented",0,isup()->location());
		    break;
		}
		if (m_circuitTesting && !connectCircuit(isup()->m_continuity)) {
		    setTerminate(true,"bearer-cap-not-available",0,isup()->location());
		    break;
		}
		Debug(isup(),DebugNote,"Call(%u). Waiting for continuity check [%p]",
		    id(),this);
		// Save message for later
		m_iamMsg = m_sgmMsg;
		m_sgmMsg = 0;
		return 0;
	    }
	    m_state = Setup;
	    if (!connectCircuit() && isup() &&
		(isup()->mediaRequired() >= SignallingCallControl::MediaAlways)) {
		setTerminate(true,"bearer-cap-not-available",0,isup()->location());
		break;
	    }
	    m_sgmMsg->params().setParam("overlapped",String::boolText(m_overlap));
	    m_lastEvent = new SignallingEvent(SignallingEvent::NewCall,m_sgmMsg,this);
	    break;
	case SS7MsgISUP::CCR:
	    if (m_state < Testing) {
		m_state = Testing;
		if (!(isup() && isup()->m_continuity)) {
		    Debug(isup(),DebugWarn,"Call(%u). Continuity check requested but not configured [%p]",
			id(),this);
		    setTerminate(true,"service-not-implemented",0,isup()->location());
		    break;
		}
		m_circuitTesting = true;
		if (!connectCircuit(isup()->m_continuity)) {
		    setTerminate(true,"bearer-cap-not-available",0,isup()->location());
		    break;
		}
		Debug(isup(),DebugNote,"Call(%u). Continuity test only [%p]",
		    id(),this);
	    }
	    else if (!m_circuitTesting) {
		setTerminate(true,"wrong-state-message",0,isup()->location());
		break;
	    }
	    m_contTimer.stop();
	    m_iamTimer.start();
	    if (isup()->m_confirmCCR)
		transmitMessage(new SS7MsgISUP(SS7MsgISUP::LPA,id()));
	    break;
	case SS7MsgISUP::ACM:
	    m_state = Accepted;
	    if (!connectCircuit() && isup() &&
		(isup()->mediaRequired() >= SignallingCallControl::MediaAlways)) {
		setReason("bearer-cap-not-available",0,0,isup()->location());
		m_lastEvent = release();
		break;
	    }
	    m_lastEvent = 0;
	    m_inbandAvailable = m_inbandAvailable ||
		SignallingUtils::hasFlag(m_sgmMsg->params(),"OptionalBackwardCallIndicators","inband");
	    if (isup() && isup()->m_earlyAcm) {
		// If the called party is known free report ringing
		// If it may become free or there is inband audio report progress
		bool ring = SignallingUtils::hasFlag(m_sgmMsg->params(),"BackwardCallIndicators","called-free");
		if (m_inbandAvailable || ring || SignallingUtils::hasFlag(m_sgmMsg->params(),"BackwardCallIndicators","called-conn")) {
		    m_sgmMsg->params().setParam("earlymedia",String::boolText(m_inbandAvailable));
		    m_lastEvent = new SignallingEvent(ring ? SignallingEvent::Ringing : SignallingEvent::Progress,m_sgmMsg,this);
		}
	    }
	    if (!m_lastEvent) {
		m_sgmMsg->params().setParam("earlymedia",String::boolText(m_inbandAvailable));
		m_lastEvent = new SignallingEvent(SignallingEvent::Accept,m_sgmMsg,this);
	    }
	    // intentionally fall through
	case SS7MsgISUP::EXM:
	    // Start T9 timer
	    if (m_anmTimer.interval() && !m_anmTimer.started())
		m_anmTimer.start();
	    break;
	case SS7MsgISUP::CPR:
	    m_state = Ringing;
	    if (!connectCircuit() && isup() &&
		(isup()->mediaRequired() >= SignallingCallControl::MediaRinging)) {
		setTerminate(true,"bearer-cap-not-available",0,isup()->location());
		break;
	    }
	    m_inbandAvailable = m_inbandAvailable ||
		SignallingUtils::hasFlag(m_sgmMsg->params(),"OptionalBackwardCallIndicators","inband") ||
		SignallingUtils::hasFlag(m_sgmMsg->params(),"EventInformation","inband");
	    m_sgmMsg->params().setParam("earlymedia",String::boolText(m_inbandAvailable));
	    m_lastEvent = new SignallingEvent(
		SignallingUtils::hasFlag(m_sgmMsg->params(),"EventInformation","ringing")
	        ? SignallingEvent::Ringing : SignallingEvent::Progress,
	        m_sgmMsg,this);
	    break;
	case SS7MsgISUP::ANM:
	case SS7MsgISUP::CON:
	    m_state = Answered;
	    m_anmTimer.stop();
	    if (!connectCircuit() && isup() &&
		(isup()->mediaRequired() >= SignallingCallControl::MediaAnswered)) {
		setTerminate(true,"bearer-cap-not-available",0,isup()->location());
		break;
	    }
	    m_lastEvent = new SignallingEvent(SignallingEvent::Answer,m_sgmMsg,this);
	    break;
	case SS7MsgISUP::CRG:
	    m_lastEvent = new SignallingEvent(SignallingEvent::Charge,m_sgmMsg,this);
	    break;
	default:
	    Debug(isup(),DebugStub,"Call(%u). Segment waiting message is '%s' [%p]",
		id(),m_sgmMsg->name(),this);
    }
    TelEngine::destruct(m_sgmMsg);
    return m_lastEvent;
}

// Transmit message. Set routing label's link if not already set
bool SS7ISUPCall::transmitMessage(SS7MsgISUP* msg)
{
    if (!msg || !isup()) {
	TelEngine::destruct(msg);
	return false;
    }
    DDebug(isup(),DebugAll,"Call(%u). Transmitting messsage (%s,%p) [%p]",
	id(),msg->name(),msg,this);
    int sls = isup()->transmitMessage(msg,m_label,false);
    if (sls == -1)
	return false;
    if (m_label.sls() == 255)
	m_label.setSls(sls);
    return true;
}

SS7ISUP* SS7ISUPCall::isup() const
{
    return static_cast<SS7ISUP*>(SignallingCall::controller());
}

// Set overlapped flag. Output a debug message
void SS7ISUPCall::setOverlapped(bool on, bool numberComplete)
{
    if (m_overlap == on)
	return;
    m_overlap = on;
    const char* reason = on ? "" : (numberComplete ? " (number complete)" : " (state changed)");
    Debug(isup(),DebugAll,"Call(%u). Overlapped dialing is %s%s [%p]",
	id(),String::boolText(on),reason,this);
}


/**
 * SS7ISUP
 */
SS7ISUP::SS7ISUP(const NamedList& params, unsigned char sio)
    : SignallingComponent(params.safe("SS7ISUP"),&params,"ss7-isup"),
      SignallingCallControl(params,"isup."),
      SS7Layer4(sio,&params),
      m_cicLen(2),
      m_type(SS7PointCode::Other),
      m_defPoint(0),
      m_remotePoint(0),
      m_sls(255),
      m_earlyAcm(true),
      m_inn(false),
      m_defaultSls(SlsLatest),
      m_maxCalledDigits(16),
      m_confirmCCR(true),
      m_dropOnUnknown(true),
      m_ignoreGRSSingle(false),
      m_ignoreCGBSingle(false),
      m_ignoreCGUSingle(false),
      m_duplicateCGB(false),
      m_ignoreUnkDigits(true),
      m_l3LinkUp(false),
      m_chargeProcessType(Confusion),
      m_t1Interval(15000),               // Q.764 T1 15..60 seconds
      m_t5Interval(300000),              // Q.764 T5 5..15 minutes
      m_t7Interval(ISUP_T7_DEFVAL),      // Q.764 T7 20..30 seconds
      m_t9Interval(0),                   // Q.764 T9 Q.118 1.5 - 3 minutes, not always used
      m_t12Interval(20000),              // Q.764 T12 (BLK) 15..60 seconds
      m_t13Interval(300000),             // Q.764 T13 (BLK global) 5..15 minutes
      m_t14Interval(20000),              // Q.764 T14 (UBL) 15..60 seconds
      m_t15Interval(300000),             // Q.764 T15 (UBL global) 5..15 minutes
      m_t16Interval(20000),              // Q.764 T16 (RSC) 15..60 seconds
      m_t17Interval(300000),             // Q.764 T17 5..15 minutes
      m_t18Interval(20000),              // Q.764 T18 (CGB) 15..60 seconds
      m_t19Interval(300000),             // Q.764 T19 (CGB global) 5..15 minutes
      m_t20Interval(20000),              // Q.764 T20 (CGU) 15..60 seconds
      m_t21Interval(300000),             // Q.764 T21 (CGU global) 5..15 minutes
      m_t27Interval(ISUP_T27_DEFVAL),    // Q.764 T27 4 minutes
      m_t34Interval(ISUP_T34_DEFVAL),    // Q.764 T34 2..4 seconds
      m_uptTimer(0),
      m_userPartAvail(true),
      m_uptMessage(SS7MsgISUP::UPT),
      m_uptCicCode(0),
      m_cicWarnLevel(DebugMild),
      m_replaceCounter(3),
      m_rscTimer(0),
      m_rscCic(0),
      m_rscSpeedup(0),
      m_lockTimer(2000),
      m_lockGroup(true),
      m_printMsg(false),
      m_extendedDebug(false)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7ISUP::SS7ISUP(%p) [%p]%s",
	    &params,this,tmp.c_str());
    }
#endif
    const char* stype = params.getValue(YSTRING("pointcodetype"));
    m_type = SS7PointCode::lookup(stype);
    if (m_type == SS7PointCode::Other) {
	Debug(this,DebugWarn,"Invalid point code type '%s'",c_safe(stype));
	return;
    }
    if (m_type == SS7PointCode::ITU)
	m_defaultSls = SlsCircuit;

    m_format = params.getValue(YSTRING("format"));
    if (-1 == lookup(m_format,SignallingUtils::dict(1,0),-1))
	switch (m_type) {
	    case SS7PointCode::ANSI:
	    case SS7PointCode::ANSI8:
	    case SS7PointCode::Japan:
	    case SS7PointCode::Japan5:
		m_format = "mulaw";
		break;
	    default:
		m_format = "alaw";
	}

    const char* rpc = params.getValue(YSTRING("remotepointcode"));
    m_remotePoint = new SS7PointCode(0,0,0);
    if (!(m_remotePoint->assign(rpc,m_type) && m_remotePoint->pack(m_type))) {
	Debug(this,DebugMild,"Invalid remotepointcode='%s'",rpc);
	TelEngine::destruct(m_remotePoint);
    }

    m_lockGroup = params.getBoolValue(YSTRING("lockgroup"),m_lockGroup);
    m_earlyAcm = params.getBoolValue(YSTRING("earlyacm"),m_earlyAcm);
    m_inn = params.getBoolValue(YSTRING("inn"),m_inn);
    m_numPlan = params.getValue(YSTRING("numplan"));
    if (-1 == lookup(m_numPlan,s_dict_numPlan,-1))
	m_numPlan = "unknown";
    m_numType = params.getValue(YSTRING("numtype"));
    if (-1 == lookup(m_numType,s_dict_nai,-1))
	m_numType = "unknown";
    m_numPresentation = params.getValue(YSTRING("presentation"));
    if (-1 == lookup(m_numPresentation,s_dict_presentation,-1))
	m_numPresentation = "allowed";
    m_numScreening = params.getValue(YSTRING("screening"));
    if (-1 == lookup(m_numScreening,s_dict_screening,-1))
	m_numScreening = "user-provided";
    m_callerCat = params.getValue(YSTRING("callercategory"));
    if (-1 == lookup(m_callerCat,s_dict_callerCat,-1))
	m_callerCat = "ordinary";

    m_rscTimer.interval(params,"channelsync",60,300,true,true);
    m_rscInterval = m_rscTimer.interval();

    // Remote user part test
    m_uptTimer.interval(params,"userparttest",10,60,true,true);
    if (m_uptTimer.interval())
	m_userPartAvail = false;
    else
	m_lockTimer.start();

    // Timers
    m_t7Interval = SignallingTimer::getInterval(params,"t7",ISUP_T7_MINVAL,ISUP_T7_DEFVAL,ISUP_T7_MAXVAL,false);
    m_t9Interval = SignallingTimer::getInterval(params,"t9",ISUP_T9_MINVAL,ISUP_T9_DEFVAL,ISUP_T9_MAXVAL,true);
    m_t27Interval = SignallingTimer::getInterval(params,"t27",ISUP_T27_MINVAL,ISUP_T27_DEFVAL,ISUP_T27_MAXVAL,false);
    m_t34Interval = SignallingTimer::getInterval(params,"t34",ISUP_T34_MINVAL,ISUP_T34_DEFVAL,ISUP_T34_MAXVAL,false);

    m_continuity = params.getValue(YSTRING("continuity"));
    m_confirmCCR = params.getBoolValue(YSTRING("confirm_ccr"),true);
    m_dropOnUnknown = params.getBoolValue(YSTRING("drop_unknown"),true);
    m_ignoreGRSSingle = params.getBoolValue(YSTRING("ignore-grs-single"));
    m_ignoreCGBSingle = params.getBoolValue(YSTRING("ignore-cgb-single"));
    m_ignoreCGUSingle = params.getBoolValue(YSTRING("ignore-cgu-single"));
    m_duplicateCGB = params.getBoolValue(YSTRING("duplicate-cgb"),
	(SS7PointCode::ANSI == m_type || SS7PointCode::ANSI8 == m_type));
    m_chargeProcessType = (ChargeProcess)params.getIntValue(YSTRING("charge-process"),s_dict_CRG_process,m_chargeProcessType);
    int testMsg = params.getIntValue(YSTRING("parttestmsg"),s_names,SS7MsgISUP::UPT);
    switch (testMsg) {
	case SS7MsgISUP::CVT:
	    if (SS7PointCode::ANSI != m_type && SS7PointCode::ANSI8 != m_type)
		break;
	    // fall through
	case SS7MsgISUP::RSC:
	case SS7MsgISUP::UBL:
	case SS7MsgISUP::UPT:
	    m_uptMessage = (SS7MsgISUP::Type)testMsg;
    }
    m_replaceCounter = params.getIntValue(YSTRING("max_replaces"),3,0,31);
    m_ignoreUnkDigits = params.getBoolValue(YSTRING("ignore-unknown-digits"),true);
    m_defaultSls = params.getIntValue(YSTRING("sls"),s_dict_callSls,m_defaultSls);
    m_maxCalledDigits = params.getIntValue(YSTRING("maxcalleddigits"),m_maxCalledDigits);
    if (m_maxCalledDigits < 1)
	m_maxCalledDigits = 16;

    setDebug(params.getBoolValue(YSTRING("print-messages"),false),
	params.getBoolValue(YSTRING("extended-debug"),false));

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
	s << " SIF/SSF=" << (unsigned int)sif() << "/" << (unsigned int)ssf();
	s << " lockcircuits=" << params.getValue(YSTRING("lockcircuits"));
	s << " userpartavail=" << String::boolText(m_userPartAvail);
	s << " lockgroup=" << String::boolText(m_lockGroup);
	s << " mediareq=" << lookup(m_mediaRequired,s_mediaRequired);
	const char* sls = lookup(m_defaultSls,s_dict_callSls);
	s << " outboundsls=";
	if (sls)
	    s << sls;
	else
	    s << m_defaultSls;
	if (m_continuity)
	    s << " continuity=" << m_continuity;
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

bool SS7ISUP::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7ISUP::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue(YSTRING("debuglevel_isup"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	setDebug(config->getBoolValue(YSTRING("print-messages"),false),
	    config->getBoolValue(YSTRING("extended-debug"),false));
	m_lockGroup = config->getBoolValue(YSTRING("lockgroup"),m_lockGroup);
	m_earlyAcm = config->getBoolValue(YSTRING("earlyacm"),m_earlyAcm);
	m_continuity = config->getValue(YSTRING("continuity"),m_continuity);
	m_confirmCCR = config->getBoolValue(YSTRING("confirm_ccr"),true);
	m_dropOnUnknown = config->getBoolValue(YSTRING("drop_unknown"),true);
	m_ignoreGRSSingle = config->getBoolValue(YSTRING("ignore-grs-single"));
	m_ignoreCGBSingle = config->getBoolValue(YSTRING("ignore-cgb-single"));
	m_ignoreCGUSingle = config->getBoolValue(YSTRING("ignore-cgu-single"));
	m_duplicateCGB = config->getBoolValue(YSTRING("duplicate-cgb"),
	    (SS7PointCode::ANSI == m_type || SS7PointCode::ANSI8 == m_type));
	int testMsg = config->getIntValue(YSTRING("parttestmsg"),s_names,SS7MsgISUP::UPT);
	switch (testMsg) {
	    case SS7MsgISUP::CVT:
		if (SS7PointCode::ANSI != m_type && SS7PointCode::ANSI8 != m_type)
		    break;
		// fall through
	    case SS7MsgISUP::RSC:
	    case SS7MsgISUP::UBL:
	    case SS7MsgISUP::UPT:
		m_uptMessage = (SS7MsgISUP::Type)testMsg;
	}
	m_replaceCounter = config->getIntValue(YSTRING("max_replaces"),3,0,31);
        m_ignoreUnkDigits = config->getBoolValue(YSTRING("ignore-unknown-digits"),true);
	m_defaultSls = config->getIntValue(YSTRING("sls"),s_dict_callSls,m_defaultSls);
	m_chargeProcessType = (ChargeProcess)config->getIntValue(YSTRING("charge-process"),s_dict_CRG_process,m_chargeProcessType);
	m_mediaRequired = (MediaRequired)config->getIntValue(YSTRING("needmedia"),
	    s_mediaRequired,m_mediaRequired);
        // Timers
	m_t7Interval = SignallingTimer::getInterval(*config,"t7",ISUP_T7_MINVAL,ISUP_T7_DEFVAL,ISUP_T7_MAXVAL,false);
	m_t9Interval = SignallingTimer::getInterval(*config,"t9",ISUP_T9_MINVAL,ISUP_T9_DEFVAL,ISUP_T9_MAXVAL,true);
	m_t27Interval = SignallingTimer::getInterval(*config,"t27",ISUP_T27_MINVAL,ISUP_T27_DEFVAL,ISUP_T27_MAXVAL,false);
	m_t34Interval = SignallingTimer::getInterval(*config,"t34",ISUP_T34_MINVAL,ISUP_T34_DEFVAL,ISUP_T34_MAXVAL,false);
    }
    m_cicWarnLevel = DebugMild;
    return SS7Layer4::initialize(config);
}

const char* SS7ISUP::statusName() const
{
    if (exiting())
	return "Exiting";
    if (!m_l3LinkUp)
	return "Layer 3 down";
    if (!m_userPartAvail)
	return "Remote unavailable";
    if (!m_defPoint)
	return "No local PC set";
    if (!m_remotePoint)
	return "No remote PC set";
    return "Operational";
}

void SS7ISUP::attach(SS7Layer3* network)
{
    SS7Layer4::attach(network);
    m_l3LinkUp = network && network->operational();
}

// Append a point code to the list of point codes serviced by this controller
// Set default point code
bool SS7ISUP::setPointCode(SS7PointCode* pc, bool def)
{
    if (!(pc && pc->pack(m_type)))
	return false;
    Lock mylock(this);
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

// Add all point codes described in a parameter list
unsigned int SS7ISUP::setPointCode(const NamedList& params)
{
    unsigned int count = 0;
    unsigned int n = params.length();
    bool hadDef = false;
    for (unsigned int i= 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!ns)
	    continue;
	bool defPc = false;
	if (ns->name() == YSTRING("defaultpointcode"))
	    defPc = true;
	else if (ns->name() != YSTRING("pointcode"))
	    continue;
	SS7PointCode* pc = new SS7PointCode(0,0,0);
	if (pc->assign(*ns,m_type) && setPointCode(pc,defPc && !hadDef)) {
	    count++;
	    if (defPc) {
		if (hadDef)
		    Debug(this,DebugMild,"Added point code '%s' as non-default",ns->safe());
		else
		    hadDef = true;
	    }
	}
	else {
	    Debug(this,DebugWarn,"Invalid '%s'='%s' in parameters '%s'",
		ns->name().c_str(),ns->safe(),params.safe());
	    TelEngine::destruct(pc);
	}
    }
    return count;
}

// Check if the given point code is serviced by this controller
SS7PointCode* SS7ISUP::hasPointCode(const SS7PointCode& pc)
{
    Lock mylock(this);
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
    return buildMSU(type,sif() | (ssf & 0xf0),label,cic,params);
}

// Make an outgoing call
SignallingCall* SS7ISUP::call(SignallingMessage* msg, String& reason)
{
    if (!msg) {
	reason = "noconn";
	return 0;
    }
    if (exiting() || !m_l3LinkUp) {
	Debug(this,DebugInfo,"Denying outgoing call request, reason: %s.",
	    exiting() ? "exiting" : "L3 down");
	TelEngine::destruct(msg);
	reason = "net-out-of-order";
	return 0;
    }
    if (!m_userPartAvail) {
	Debug(this,DebugNote,"Remote User Part is unavailable");
	TelEngine::destruct(msg);
	reason = "noconn";
	return 0;
    }
    SS7PointCode dest;
    SignallingCircuit* cic = 0;
    const char* range = msg->params().getValue(YSTRING("circuits"));
    reason.clear();
    Lock mylock(this);
    // Check
    while (true) {
	if (!m_defPoint) {
 	    Debug(this,DebugNote,"Source point code is missing");
	    reason = "noconn";
	    break;
	}
	String pc = msg->params().getValue(YSTRING("calledpointcode"));
	if (!(dest.assign(pc,m_type) && dest.pack(m_type))) {
	    if (!m_remotePoint) {
		Debug(this,DebugNote,
		    "Destination point code is missing (calledpointcode=%s)",pc.safe());
		reason = "noconn";
		break;
	    }
	    dest = *m_remotePoint;
	}
	for (int attempts = 3; attempts; attempts--) {
	    if (!reserveCircuit(cic,range,SignallingCircuit::LockLockedBusy)) {
		Debug(this,DebugNote,"Can't reserve circuit");
		break;
	    }
	    SS7ISUPCall* call2 = findCall(cic->code());
	    if (!call2)
		break;
	    Debug(this,DebugWarn,"Circuit %u is already used by call %p",
		cic->code(),call2);
	    TelEngine::destruct(cic);
	}
	if (!cic)
	    reason = "congestion";
	break;
    }
    SS7ISUPCall* call = 0;
    if (reason.null()) {
	String* cicParams = msg->params().getParam(YSTRING("circuit_parameters"));
	if (cicParams) {
	    NamedList* p = YOBJECT(NamedList,cicParams);
	    if (p)
		cic->setParams(*p);
	}
	int sls = msg->params().getIntValue(YSTRING("sls"),s_dict_callSls,m_defaultSls);
	switch (sls) {
	    case SlsCircuit:
		if (cic) {
		    sls = cic->code();
		    break;
		}
		// fall through
	    case SlsLatest:
		sls = m_sls;
		break;
	}
	call = new SS7ISUPCall(this,cic,*m_defPoint,dest,true,sls,range);
	call->ref();
	m_calls.append(call);
	SignallingEvent* event = new SignallingEvent(SignallingEvent::NewCall,msg,call);
	// (re)start RSC timer if not currently reseting
	if (!m_rscCic && m_rscTimer.interval())
	    m_rscTimer.start();
	// Drop lock and send the event
	mylock.drop();
	if (!event->sendEvent()) {
	    call->setTerminate(false,"failure");
	    TelEngine::destruct(call);
	    reason = "failure";
	}
    }
    TelEngine::destruct(msg);
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
	switch (sls) {
	    case SlsCircuit:
		sls = msg->cic();
		break;
	    case SlsLatest:
		sls = m_sls;
		break;
	    case SlsDefault:
		sls = label.sls();
		break;
	}
	tmp.assign(label.type(),label.opc(),label.dpc(),sls,label.spare());
	p = &tmp;
    }

    lock();
    SS7MSU* msu = createMSU(msg->type(),ssf(),*p,msg->cic(),&msg->params());

    if (m_printMsg && debugAt(DebugInfo)) {
	String tmp;
	void* data = 0;
	unsigned int len = 0;
	if (m_extendedDebug && msu) {
	    unsigned int offs = 2 + label.length() + m_cicLen;
	    data = msu->getData(offs);
	    len = data ? msu->length() - offs : 0;
	}
	msg->toString(tmp,*p,debugAt(DebugAll),data,len);
	Debug(this,DebugInfo,"Sending message (%p)%s",msg,tmp.c_str());
    }
    else if (debugAt(DebugAll)) {
	String tmp;
	tmp << *p;
	Debug(this,DebugAll,"Sending message '%s' cic=%u label=%s",
	    msg->name(),msg->cic(),tmp.c_str());
    }

    sls = -1;
    if (msu && m_l3LinkUp) {
	unlock();
	sls = transmitMSU(*msu,*p,p->sls());
	lock();
	if ((m_sls == 255) && (sls != -1))
	    m_sls = (unsigned char)sls;
    }
    unlock();
#ifdef XDEBUG
    if (sls == -1)
	Debug(this,DebugMild,"Failed to send message (%p): '%s'",msg,msg->name());
#endif
    TelEngine::destruct(msu);
    TelEngine::destruct(msg);
    return sls;
}

void SS7ISUP::cleanup(const char* reason)
{
    ObjList terminate;
    lock();
    for (ObjList* o = m_calls.skipNull(); o; o = o->skipNext()) {
	SS7ISUPCall* call = static_cast<SS7ISUPCall*>(o->get());
	if (call->ref())
	    terminate.append(call);
    }
    releaseCircuit(m_rscCic);
    m_rscTimer.stop();
    unlock();
    setCallsTerminate(terminate,true,reason);
    clearCalls();
}

// Remove all links with other layers. Disposes the memory
void SS7ISUP::destroyed()
{
    lock();
    clearCalls();
    unlock();
    SignallingCallControl::attach(0);
    SS7Layer4::destroyed();
}

// Utility: find a pending (un)block message for a given circuit
static bool findPendingMsgTimerLock(ObjList& list, unsigned int code)
{
    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	SignallingMessageTimer* m = static_cast<SignallingMessageTimer*>(o->get());
	SS7MsgISUP* msg = static_cast<SS7MsgISUP*>(m->message());
	if (!msg || code < msg->cic())
	    continue;
	if (msg->type() == SS7MsgISUP::BLK || msg->type() == SS7MsgISUP::UBL) {
	    if (msg->cic() == code)
		return true;
	    continue;
	}
	if (msg->type() != SS7MsgISUP::CGB && msg->type() != SS7MsgISUP::CGU)
	    continue;
	const String& map = msg->params()[YSTRING("RangeAndStatus.map")];
	if (map[code - msg->cic()] == '1')
	    return true;
    }
    return false;
}

void SS7ISUP::timerTick(const Time& when)
{
    Lock mylock(this,SignallingEngine::maxLockWait());
    if (!(mylock.locked() && m_l3LinkUp && circuits()))
	return;

    // Test remote user part
    if (m_remotePoint && !m_userPartAvail && m_uptTimer.interval()) {
	if (m_uptTimer.started()) {
	    if (!m_uptTimer.timeout(when.msec()))
		return;
	    DDebug(this,DebugNote,"%s timed out. Retransmitting",lookup(m_uptMessage,s_names));
	}
	ObjList* o = circuits()->circuits().skipNull();
	SignallingCircuit* cic = o ? static_cast<SignallingCircuit*>(o->get()) : 0;
	m_uptCicCode = cic ? cic->code() : 1;
	SS7MsgISUP* msg = new SS7MsgISUP(m_uptMessage,m_uptCicCode);
	SS7Label label(m_type,*m_remotePoint,*m_defPoint,
	    (m_defaultSls == SlsCircuit) ? m_uptCicCode : m_sls);
	m_uptTimer.start(when.msec());
	mylock.drop();
	transmitMessage(msg,label,false);
	return;
    }

    // Blocking/unblocking circuits
    if (m_lockTimer.timeout(when.msec())) {
	DDebug(this,DebugAll,"Re-checking local lock sending");
	m_lockTimer.stop();
	mylock.drop();
	sendLocalLock(when);
	return;
    }

    // Pending messages
    ObjList reInsert;
    ObjList sendMsgs;
    ObjList rsc;
    while (true) {
	SignallingMessageTimer* m = m_pending.timeout(when);
	if (!m)
	    break;
	SS7MsgISUP* msg = static_cast<SS7MsgISUP*>(m->message());
	if (!msg) {
	    TelEngine::destruct(m);
	    continue;
	}
	if (msg->type() != SS7MsgISUP::RSC &&
	    msg->type() != SS7MsgISUP::REL &&
	    msg->type() != SS7MsgISUP::CGB &&
	    msg->type() != SS7MsgISUP::CGU &&
	    msg->type() != SS7MsgISUP::BLK &&
	    msg->type() != SS7MsgISUP::UBL) {
	    Debug(this,DebugStub,"Unhandled pending message '%s'",msg->name());
	    TelEngine::destruct(m);
	    continue;
	}
	// Global timer timed out: set retransmission timer from it
	if (m->global().timeout(when.msec())) {
	    if (msg->type() != SS7MsgISUP::REL) {
		m->interval(m->global().interval());
		m->global().stop();
		m->global().interval(0);
		msg->params().setParam("isup_alert_maint",String::boolText(true));
	    }
	    else {
		Debug(this,DebugNote,"Pending operation '%s' cic=%u timed out",
		    msg->name(),msg->cic());
		SignallingCircuit* c = circuits() ? circuits()->find(msg->cic()) : 0;
		TelEngine::destruct(m);
		if (c && c->ref())
		    rsc.append(c)->setDelete(false);
		continue;
	    }
	}
	// Check if message is still in use
	if (msg->type() == SS7MsgISUP::CGB || msg->type() == SS7MsgISUP::CGU) {
	    String* map = msg->params().getParam(YSTRING("RangeAndStatus.map"));
	    bool ok = !TelEngine::null(map);
	    String removedCics;
	    if (ok) {
		unsigned int nCics = 0;
		int flg = 0;
		int flgReset = 0;
		if ((msg->params()[YSTRING("GroupSupervisionTypeIndicator")] == YSTRING("hw-failure"))) {
		    flg = SignallingCircuit::LockLocalHWFail;
		    flgReset = SignallingCircuit::LockingHWFail;
		}
		else {
		    flg = SignallingCircuit::LockLocalMaint;
		    flgReset = SignallingCircuit::LockingMaint;
		}
		int on = (msg->type() == SS7MsgISUP::CGB) ? flg : 0;
		char* s = (char*)map->c_str();
		for (unsigned int i = 0; i < map->length(); i++) {
		    if (s[i] == '0')
			continue;
		    unsigned int code = msg->cic() + i;
		    SignallingCircuit* cic = circuits()->find(code);
		    if (cic && (on == cic->locked(flg))) {
			nCics++;
			continue;
		    }
		    // Don't reset locking flag if there is another operation in progress
		    if (cic && !(findPendingMsgTimerLock(m_pending,code) ||
			findPendingMsgTimerLock(reInsert,code)) && cic->locked(flgReset)) {
			cic->resetLock(flgReset);
			Debug(this,DebugNote,"Pending %s reset flag=0x%x cic=%u current=0x%x",
			    msg->name(),flgReset,code,cic->locked());;
		    }
		    s[i] = '0';
		    removedCics.append(String(code),",");
		}
		if (nCics)
		    msg->params().setParam("RangeAndStatus",String(nCics));
		else
		    ok = false;
	    }
	    if (!ok) {
		Debug(this,DebugNote,"Removed empty pending operation '%s' cic=%u",
		    msg->name(),msg->cic());
		TelEngine::destruct(m);
		continue;
	    }
	    if (removedCics)
		Debug(this,DebugAll,"Removed cics=%s from pending operation '%s' map cic=%u",
		    removedCics.c_str(),msg->name(),msg->cic());
	}
	else if (msg->type() == SS7MsgISUP::BLK || msg->type() == SS7MsgISUP::UBL) {
	    // We set the following param when sending BLK/UBL for HW fail reason
	    bool maint = !msg->params().getBoolValue(YSTRING("isup_pending_block_hwfail"));
	    int flg = maint ? SignallingCircuit::LockLocalMaint : SignallingCircuit::LockLocalHWFail;
	    int on = (msg->type() == SS7MsgISUP::BLK) ? flg : 0;
	    SignallingCircuit* cic = circuits()->find(msg->cic());
	    if (!cic || on != cic->locked(flg)) {
		flg = maint ? SignallingCircuit::LockingMaint : SignallingCircuit::LockingHWFail;
		// Don't reset locking flag if there is another operation in progress
		if (cic && !(findPendingMsgTimerLock(m_pending,msg->cic()) ||
		    findPendingMsgTimerLock(reInsert,msg->cic())) && cic->locked(flg)) {
		    cic->resetLock(flg);
		    Debug(this,DebugNote,"Pending %s reset flag=0x%x cic=%u current=0x%x",
			msg->name(),flg,cic->code(),cic->locked());
		}
		Debug(this,DebugNote,"Removed empty pending operation '%s' cic=%u",
		    msg->name(),msg->cic());
		TelEngine::destruct(m);
		continue;
	    }
	}
	bool alert = msg->params().getBoolValue(YSTRING("isup_alert_maint"));
	const char* reason = msg->params().getValue(YSTRING("isup_pending_reason"),"");
	Debug(this,!alert ? DebugAll : DebugMild,
	    "Pending operation '%s' cic=%u reason='%s' timed out",
	    msg->name(),msg->cic(),reason);
	if (alert) {
	    // TODO: alert maintenance
	}
	msg->ref();
	reInsert.append(m)->setDelete(false);
	sendMsgs.append(msg)->setDelete(false);
    }
    // Re-insert
    ObjList* o = reInsert.skipNull();
    ObjList* oRsc = rsc.skipNull();
    if (o || oRsc) {
	for (; o; o = o->skipNext())
	    m_pending.add(static_cast<SignallingMessageTimer*>(o->get()),when);
	mylock.drop();
	transmitMessages(sendMsgs);
	for (; oRsc; oRsc = oRsc->skipNext()) {
	    SignallingCircuit* c = static_cast<SignallingCircuit*>(oRsc->get());
	    c->resetLock(SignallingCircuit::Resetting);
	    startCircuitReset(c,"T5");
	}
	return;
    }

    // Circuit reset disabled ?
    if (!m_rscTimer.interval())
	return;
    if (m_rscTimer.started()) {
	if (!m_rscTimer.timeout(when.msec()))
	    return;
	m_rscTimer.stop();
	if (m_rscCic) {
	    Debug(this,DebugMild,"Circuit reset timed out for cic=%u",m_rscCic->code());
	    m_rscCic->resetLock(SignallingCircuit::Resetting);
	    releaseCircuit(m_rscCic);
	    return;
	}
    }
    if (m_rscSpeedup && !--m_rscSpeedup) {
	Debug(this,DebugNote,"Reset interval back to %u ms",m_rscInterval);
	m_rscTimer.interval(m_rscInterval);
    }
    m_rscTimer.start(when.msec());
    // Pick the next circuit to reset. Ignore circuits locally locked or busy
    if (m_defPoint && m_remotePoint &&
	reserveCircuit(m_rscCic,0,SignallingCircuit::LockLocal | SignallingCircuit::LockBusy)) {
	// Avoid already resetting cic
	if (!findPendingMessage(SS7MsgISUP::RSC,m_rscCic->code())) {
	    m_rscCic->setLock(SignallingCircuit::Resetting);
	    SS7MsgISUP* msg = new SS7MsgISUP(SS7MsgISUP::RSC,m_rscCic->code());
	    SS7Label label(m_type,*m_remotePoint,*m_defPoint,
		(m_defaultSls == SlsCircuit) ? m_rscCic->code() : m_sls);
	    DDebug(this,DebugNote,"Periodic restart on cic=%u",m_rscCic->code());
	    mylock.drop();
	    transmitMessage(msg,label,false);
	}
	else
	    releaseCircuit(m_rscCic);
    }
}

// Process a component control request
bool SS7ISUP::control(NamedList& params)
{
    String* ret = params.getParam(YSTRING("completion"));
    const String* oper = params.getParam(YSTRING("operation"));
    const char* cmp = params.getValue(YSTRING("component"));
    int cmd = oper ? oper->toInteger(s_dict_control,-1) : -1;

    if (ret) {
	if (oper && (cmd < 0))
	    return false;
	String part = params.getValue(YSTRING("partword"));
	if (cmp) {
	    if (toString() != cmp)
		return false;
	    for (const TokenDict* d = s_dict_control; d->token; d++)
		Module::itemComplete(*ret,d->token,part);
	    return true;
	}
	return Module::itemComplete(*ret,toString(),part);
    }

    if (!(cmp && toString() == cmp))
	return false;
    Lock mylock(this);
    if (!m_remotePoint)
	return TelEngine::controlReturn(&params,false);
    unsigned int code1 = 1;
    if (circuits()) {
	ObjList* o = circuits()->circuits().skipNull();
	if (o) {
	    SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
	    if (cic)
		code1 = cic->code();
	}
    }
    switch (cmd) {
	case SS7MsgISUP::UPT:
	case SS7MsgISUP::CVT:
	    {
		unsigned int code = params.getIntValue(YSTRING("circuit"),code1);
		SS7MsgISUP* msg = new SS7MsgISUP((SS7MsgISUP::Type)cmd,code);
		SS7Label label(m_type,*m_remotePoint,*m_defPoint,m_sls);
		mylock.drop();
		transmitMessage(msg,label,false);
	    }
	    return TelEngine::controlReturn(&params,true);
	case SS7MsgISUP::CQM:
	    {
		unsigned int code = params.getIntValue(YSTRING("circuit"),code1);
		unsigned int range = params.getIntValue(YSTRING("range"),1);
		SS7MsgISUP* msg = new SS7MsgISUP(SS7MsgISUP::CQM,code);
		msg->params().addParam("RangeAndStatus",String(range));
		SS7Label label(m_type,*m_remotePoint,*m_defPoint,m_sls);
		mylock.drop();
		transmitMessage(msg,label,false);
	    }
	    return TelEngine::controlReturn(&params,true);
	case SS7MsgISUP::CCR:
	    {
		unsigned int code = params.getIntValue(YSTRING("circuit"),code1);
		// TODO: create a test call, not just send CCR
		SS7MsgISUP* msg = 0;
		const String& ok = params[YSTRING("success")];
		if (ok.isBoolean()) {
		    msg = new SS7MsgISUP(SS7MsgISUP::COT,code);
		    msg->params().addParam("ContinuityIndicators",
			ok.toBoolean() ? "success" : "failed");
		}
		else
		    msg = new SS7MsgISUP(SS7MsgISUP::CCR,code);
		SS7Label label(m_type,*m_remotePoint,*m_defPoint,m_sls);
		mylock.drop();
		transmitMessage(msg,label,false);
	    }
	    return TelEngine::controlReturn(&params,true);
	case SS7MsgISUP::RSC:
	    if (0 == (m_rscSpeedup = circuits() ? circuits()->count() : 0))
		return TelEngine::controlReturn(&params,false);
	    // Temporarily speed up reset interval to 10s or as provided
	    m_rscTimer.interval(params,"interval",2,10,false,true);
	    Debug(this,DebugNote,"Fast reset of %u circuits every %u ms",
		m_rscSpeedup,(unsigned int)m_rscTimer.interval());
	    if (m_rscTimer.started())
		m_rscTimer.start(Time::msecNow());
	    return TelEngine::controlReturn(&params,true);
	case SS7MsgISUP::BLK:
	case SS7MsgISUP::UBL:
	    return TelEngine::controlReturn(&params,handleCicBlockCommand(params,cmd == SS7MsgISUP::BLK));
	case SS7MsgISUP::RLC:
	    {
		int code = params.getIntValue(YSTRING("circuit"));
		if (code <= 0)
		    return TelEngine::controlReturn(&params,false);
		SignallingMessageTimer* pending = findPendingMessage(SS7MsgISUP::RSC,code,true);
		if (pending) {
		    resetCircuit((unsigned int)code,false,false);
		    TelEngine::destruct(pending);
		    SS7Label label(m_type,*m_remotePoint,*m_defPoint,m_sls);
		    mylock.drop();
		    transmitRLC(this,code,label,false);
		}
		else {
		    RefPointer<SS7ISUPCall> call;
		    findCall(code,call);
		    if (!call)
			return TelEngine::controlReturn(&params,false);
		    mylock.drop();
		    call->setTerminate(true,params.getValue(YSTRING("reason"),"normal"));
		}
	    }
	    return TelEngine::controlReturn(&params,true);
	case SS7MsgISUP::UPA:
	    if (!m_userPartAvail) {
		const char* oldStat = statusName();
		m_uptTimer.stop();
		m_userPartAvail = true;
		m_lockTimer.start();
		if (statusName() != oldStat) {
		    NamedList params("");
		    params.addParam("from",toString());
		    params.addParam("type","trunk");
		    params.addParam("operational",String::boolText(m_l3LinkUp));
		    params.addParam("available",String::boolText(m_userPartAvail));
		    params.addParam("text",statusName());
		    engine()->notify(this,params);
		}
	    }
	    return TelEngine::controlReturn(&params,true);
	case SS7MsgISUP::CtrlSave:
	    setVerify(true,true);
	    return TelEngine::controlReturn(&params,true);
#ifdef ISUP_HANDLE_CIC_EVENT_CONTROL
	case SS7MsgISUP::CtrlCicEvent:
	    return TelEngine::controlReturn(&params,handleCicEventCommand(params));
#endif
    }
    mylock.drop();
    return SignallingComponent::control(params);
}

// Process a notification generated by the attached network layer
void SS7ISUP::notify(SS7Layer3* link, int sls)
{
    if (!(link && network()))
	return;
    Lock mylock(this);
    SS7Route::State state = m_remotePoint ?
	network()->getRouteState(m_type,*m_remotePoint) : SS7Route::Unknown;
    bool linkTmp = m_l3LinkUp;
    bool partAvail = m_userPartAvail;
    const char* oldStat = statusName();
    // Copy linkset operational state
    m_l3LinkUp = network()->operational();
    // Reset remote user part's availability state if supported
    // Force UPT re-send
    if (m_uptTimer.interval() && (!m_l3LinkUp || (SS7Route::Prohibited == state))) {
	m_uptTimer.stop();
	m_userPartAvail = false;
    }
    Debug(this,DebugInfo,
	"L3 '%s' sls=%d is %soperational.%s Route is %s. Remote User Part is %savailable",
	link->toString().safe(),sls,
	(link->operational() ? "" : "not "),
	(network() == link ? "" : (m_l3LinkUp ? " L3 is up." : " L3 is down.")),
	SS7Route::stateName(state),
	(m_userPartAvail ? "" : "un"));
    if (linkTmp != m_l3LinkUp || partAvail != m_userPartAvail) {
	NamedList params("");
	params.addParam("from",toString());
	params.addParam("type","trunk");
	params.addParam("operational",String::boolText(m_l3LinkUp));
	params.addParam("available",String::boolText(m_userPartAvail));
	params.addParam("link",link->toString());
	if (statusName() != oldStat)
	    params.addParam("text",statusName());
	engine()->notify(this,params);
    }
}

SS7MSU* SS7ISUP::buildMSU(SS7MsgISUP::Type type, unsigned char sio,
    const SS7Label& label, unsigned int cic, const NamedList* params) const
{
    // Special treatment for charge message
    // Check if it is in raw format
    if (type == SS7MsgISUP::CRG && params && params->getParam(YSTRING("Charge")))
	return encodeRawMessage(type,sio,label,cic,(*params)[YSTRING("Charge")]);

    if (type == SS7MsgISUP::PAM && params)
	return encodeRawMessage(type,sio,label,cic,(*params)[YSTRING("PassAlong")]);
    // see what mandatory parameters we should put in this message
    const MsgParams* msgParams = getIsupParams(label.type(),type);
    if (!msgParams) {
	if (!hasOptionalOnly(type)) {
	    const char* name = SS7MsgISUP::lookup(type);
	    if (name)
		Debug(this,DebugWarn,"No parameter table for ISUP MSU type %s [%p]",name,this);
	    else
		Debug(this,DebugWarn,"Cannot create ISUP MSU type 0x%02x [%p]",type,this);
	    return 0;
	}
	msgParams = &s_compatibility;
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
#ifdef XDEBUG
    if (params && debugAt(DebugAll)) {
	String tmp;
	params->dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7ISUP::buildMSU params:%s",
	    tmp.c_str());
    }
#endif
    ObjList exclude;
    plist = msgParams->params;
    String prefix = params->getValue(YSTRING("message-prefix"));
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
	    if (prefix && !ns->name().startsWith(prefix))
		continue;
	    String tmp(ns->name());
	    tmp >> prefix.c_str();
	    static const Regexp s_suffix("\\.[0-9]\\+$");
	    if (tmp.matches(s_suffix)) {
		tmp.assign(tmp,tmp.matchOffset());
		// WARNING: HACK - ApplicationTransport does not follow naming convention
		if (tmp == YSTRING("ApplicationTransport"))
		    continue;
	    }
	    const IsupParam* param = getParamDesc(tmp);
	    unsigned char size = 0;
	    if (param)
		size = encodeParam(this,*msu,param,ns,params,prefix);
	    else if (tmp.startSkip("Param_",false)) {
		int val = tmp.toInteger(-1);
		if (val >= 0 && val <= 255) {
		    IsupParam p;
		    p.name = tmp;
		    p.type = (SS7MsgISUP::Parameters)val;
		    p.size = 0;
		    p.encoder = 0;
		    size = encodeParam(this,*msu,&p,ns,params,prefix);
		}
	    }
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

SS7MSU* SS7ISUP::encodeRawMessage(SS7MsgISUP::Type type, unsigned char sio,
    const SS7Label& label, unsigned int cic, const String& param) const
{
    DataBlock raw;
    if (!raw.unHexify(param.c_str(),param.length(),' ')) {
	DDebug(this,DebugMild,"Encode raw charge failed: invalid string");
	return 0;
    }
    if (raw.length() > 254) {
	DDebug(this,DebugMild,"Encode raw charge failed: data length=%u",
		raw.length());
	return 0;
    }
    SS7MSU* msu = new SS7MSU(sio,label,0,m_cicLen + 1);
    unsigned char* d = msu->getData(label.length()+1,m_cicLen + 1);
    unsigned int i = m_cicLen;
    while (i--) {
	*d++ = cic & 0xff;
	cic >>= 8;
    }
    *d++ = type;
    *msu += raw;
    return msu;
}



// Decode a buffer to a list of parameters
bool SS7ISUP::decodeMessage(NamedList& msg,
    SS7MsgISUP::Type msgType, SS7PointCode::Type pcType,
    const unsigned char* paramPtr, unsigned int paramLen)
{
    String msgTypeName((int)msgType);
    const char* msgName = SS7MsgISUP::lookup(msgType,msgTypeName);
#ifdef XDEBUG
    String tmp;
    tmp.hexify((void*)paramPtr,paramLen,' ');
    Debug(this,DebugAll,"Decoding msg=%s len=%u: %s [%p]",
	msgName,paramLen,tmp.c_str(),this);
#else
    DDebug(this,DebugAll,"Decoding msg=%s len=%u [%p]",
	msgName,paramLen,this);
#endif

    // see what parameters we expect for this message
    const MsgParams* params = getIsupParams(pcType,msgType);
    if (!params) {
	if (hasOptionalOnly(msgType)) {
	    Debug(this,DebugNote,"Unsupported message %s, decoding compatibility [%p]",msgName,this);
	    params = &s_compatibility;
	}
	else if (msgType != SS7MsgISUP::PAM) {
	    Debug(this,DebugWarn,"Unsupported message %s or point code type [%p]",msgName,this);
	    return false;
	}
	else if (!paramLen) {
	    // PAM message must have at least 1 byte for message type
	    Debug(this,DebugNote,"Empty %s [%p]",msgName,this);
	    return false;
	}
    }

    // Get parameter prefix
    String prefix = msg.getValue(YSTRING("message-prefix"));

    // Add protocol and message type
    if (!msg.getValue(prefix+"protocol-type")) {
	switch (pcType) {
	    case SS7PointCode::ITU:
		msg.setParam(prefix+"protocol-type","itu-t");
		break;
	    case SS7PointCode::ANSI:
	    case SS7PointCode::ANSI8:
		msg.setParam(prefix+"protocol-type","ansi");
		break;
	    default: ;
	}
    }
    msg.addParam(prefix+"message-type",msgName);

    // Special decoder for PAM
    if (msgType == SS7MsgISUP::PAM) {
        String raw;
	raw.hexify((void*)paramPtr,paramLen,' ');
        msg.addParam(prefix + "PassAlong",raw);
	return true;
    }

    // Decode raw CRG if specified
    if (msgType == SS7MsgISUP::CRG && getChargeProcessType() != Parsed) {
	String raw;
	raw.hexify((void*)paramPtr,paramLen,' ');
	msg.addParam(prefix + "Charge",raw);
	return true;
    }

    String unsupported;
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
	if (!decodeParam(this,msg,param,paramPtr,param->size,prefix)) {
	    Debug(this,DebugWarn,"Could not decode fixed ISUP parameter %s [%p]",param->name,this);
	    decodeRaw(this,msg,param,paramPtr,param->size,prefix);
	    SignallingUtils::appendFlag(unsupported,param->name);
	}
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
	if (!decodeParam(this,msg,param,paramPtr+offs+1,size,prefix)) {
	    Debug(this,DebugWarn,"Could not decode variable ISUP parameter %s (size=%u) [%p]",
		param->name,size,this);
	    decodeRaw(this,msg,param,paramPtr+offs+1,size,prefix);
	    SignallingUtils::appendFlag(unsupported,param->name);
	}
	paramPtr++;
	paramLen--;
    } // while ((ptype = *plist++)...
    // now decode the optional parameters if the message supports them
    if (params->optional) {
	unsigned int offs = paramLen ? paramPtr[0] : 0;
	if (offs >= paramLen) {
	    if (paramLen) {
		Debug(this,DebugWarn,"Invalid ISUP optional offset %u (len=%u) [%p]",
		    offs,paramLen,this);
		return false;
	    }
	    Debug(this,DebugMild,"ISUP message %s lacking optional parameters [%p]",
		msgName,this);
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
		if (!param) {
		    Debug(this,DebugMild,"Unknown optional ISUP parameter 0x%02x (size=%u) [%p]",ptype,size,this);
		    decodeRawParam(this,msg,ptype,paramPtr,size,prefix);
		    SignallingUtils::appendFlag(unsupported,String((unsigned int)ptype));
		}
		else if (!decodeParam(this,msg,param,paramPtr,size,prefix)) {
		    Debug(this,DebugWarn,"Could not decode optional ISUP parameter %s (size=%u) [%p]",param->name,size,this);
		    decodeRaw(this,msg,param,paramPtr,size,prefix);
		    SignallingUtils::appendFlag(unsupported,param->name);
		}
		paramPtr += size;
		paramLen -= size;
	    } // while (paramLen)
	} // else if (offs)
	else
	    paramLen = 0;
    }
    if (unsupported)
	msg.addParam(prefix + "parameters-unsupported",unsupported);
    String release,cnf,npRelease;
    String pCompat(prefix + "ParameterCompatInformation.");
    unsigned int n = msg.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = msg.getParam(i);
	if (!(ns && ns->name().startsWith(pCompat) && !ns->name().endsWith(".more")))
	    continue;
	ObjList* l = ns->split(',',false);
	for (ObjList* ol = l->skipNull(); ol; ol = ol->skipNext()) {
	    String* s = static_cast<String*>(ol->get());
	    if (*s == YSTRING("release")) {
		SignallingUtils::appendFlag(release,ns->name().substr(pCompat.length()));
		break;
	    }
	    if (*s == YSTRING("cnf"))
		SignallingUtils::appendFlag(cnf,ns->name().substr(pCompat.length()));
	    if (*s == YSTRING("nopass-release"))
		SignallingUtils::appendFlag(npRelease,ns->name().substr(pCompat.length()));
	}
	TelEngine::destruct(l);
    }
    if (release)
	msg.setParam(prefix + "parameters-unhandled-release",release);
    if (cnf)
	msg.setParam(prefix + "parameters-unhandled-cnf",cnf);
    if (npRelease)
	msg.setParam(prefix + "parameters-nopass-release",npRelease);
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

// Process parameter compatibility lists
// Terminate an existing call or send CNF
// Return true if any parameter compatibility was handled
bool SS7ISUP::processParamCompat(const NamedList& list, unsigned int cic, bool* callReleased)
{
    if (!cic)
	return true;
    const String& prefix = list[YSTRING("message-prefix")];
    // Release call params
    String relCall = list[prefix + "parameters-unhandled-release"];
    relCall.append(list[prefix + "parameters-nopass-release"],",");
    if (relCall) {
	Lock lock(this);
	SS7ISUPCall* call = findCall(cic);
	Debug(this,DebugNote,
	    "Terminating call (%p) on cic=%u: unknown/unhandled params='%s' [%p]",
	    call,cic,relCall.c_str(),this);
	String diagnostic;
	hexifyIsupParams(diagnostic,relCall);
	if (call) {
	    lock.drop();
	    call->setTerminate(true,"unknown-ie",diagnostic,m_location);
	}
	else if (m_remotePoint) {
	    // No call: make sure the circuit is released at remote party
	    SS7Label label(m_type,*m_remotePoint,*m_defPoint,
		(m_defaultSls == SlsCircuit) ? cic : m_sls);
	    lock.drop();
	    transmitRLC(this,cic,label,false,"unknown-ie",diagnostic,m_location);
	}
	if (callReleased)
	    *callReleased = true;
	return true;
    }
    // Send CNF params
    const String& cnf = list[prefix + "parameters-unhandled-cnf"];
    if (!cnf)
	return false;
    DDebug(this,DebugAll,"processParamCompat() cic=%u sending CNF for '%s' [%p]",
	cic,cnf.c_str(),this);
    String diagnostic;
    hexifyIsupParams(diagnostic,cnf);
    if (diagnostic && m_remotePoint) {
	SS7Label label(m_type,*m_remotePoint,*m_defPoint,
	    (m_defaultSls == SlsCircuit) ? cic : m_sls);
	transmitCNF(this,cic,label,false,"unknown-ie",diagnostic,m_location);
    }
    return !diagnostic.null();
}

HandledMSU SS7ISUP::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != sif() || !hasPointCode(label.dpc()) || !handlesRemotePC(label.opc()))
	return HandledMSU::Rejected;
    // we should have at least 2 bytes CIC and 1 byte message type
    const unsigned char* s = msu.getData(label.length()+1,3);
    if (!s) {
	Debug(this,DebugNote,"Got short MSU");
	return false;
    }
    unsigned int len = msu.length()-label.length()-1;
    unsigned int cic = s[0] | (s[1] << 8);
    SS7MsgISUP::Type type = (SS7MsgISUP::Type)s[2];
    String name = SS7MsgISUP::lookup(type);
    if (!name) {
        String tmp;
	tmp.hexify((void*)s,len,' ');
	Debug(this,DebugMild,"Received unknown ISUP type 0x%02x, cic=%u, length %u: %s",
	    type,cic,len,tmp.c_str());
	name = (int)type;
    }
    if (!(circuits() && circuits()->find(cic))) {
	Debug(this,m_cicWarnLevel,"Received ISUP type 0x%02x (%s) for unknown cic=%u",
	    type,name.c_str(),cic);
	m_cicWarnLevel = DebugAll;
	return HandledMSU::NoCircuit;
    }
    bool ok = processMSU(type,cic,s+3,len-3,label,network,sls);
    if (!ok && debugAt(DebugMild)) {
	String tmp;
	tmp.hexify((void*)s,len,' ');
	Debug(this,DebugMild,"Unhandled ISUP type %s, cic=%u, length %u: %s",
	    name.c_str(),cic,len,tmp.c_str());
    }
    return ok;
}

bool SS7ISUP::processMSU(SS7MsgISUP::Type type, unsigned int cic,
    const unsigned char* paramPtr, unsigned int paramLen,
    const SS7Label& label, SS7Layer3* network, int sls)
{
    XDebug(this,DebugAll,"SS7ISUP::processMSU(%u,%u,%p,%u,%p,%p,%d) [%p]",
	type,cic,paramPtr,paramLen,&label,network,sls,this);

    SS7MsgISUP* msg = new SS7MsgISUP(type,cic);
    if (!SS7MsgISUP::lookup(type)) {
	String tmp;
	tmp.hexify(&type,1);
	msg->params().assign("Message_" + tmp);
    }
    if (!decodeMessage(msg->params(),type,label.type(),paramPtr,paramLen)) {
	TelEngine::destruct(msg);
	return false;
    }

    if (m_printMsg && debugAt(DebugInfo)) {
	String tmp;
	msg->toString(tmp,label,debugAt(DebugAll),
	    m_extendedDebug ? paramPtr : 0,paramLen);
	Debug(this,DebugInfo,"Received message (%p)%s",msg,tmp.c_str());
    }
    else if (debugAt(DebugAll)) {
	String tmp;
	tmp << label;
	Debug(this,DebugAll,"Received message '%s' cic=%u label=%s",
	    msg->name(),msg->cic(),tmp.c_str());
    }

    // TODO: check parameters-unsupported vs. ParameterCompatInformation

    // Check if we expected some response to UPT
    // Ignore
    if (!m_userPartAvail && m_uptTimer.started()) {
	m_uptTimer.stop();
	const char* oldStat = statusName();
	m_userPartAvail = true;
	m_lockTimer.start();
	Debug(this,DebugInfo,"Remote user part is available");
	if (statusName() != oldStat) {
	    NamedList params("");
	    params.addParam("from",toString());
	    params.addParam("type","trunk");
	    params.addParam("operational",String::boolText(m_l3LinkUp));
	    params.addParam("available",String::boolText(m_userPartAvail));
	    params.addParam("text",statusName());
	    engine()->notify(this,params);
	}
	if (msg->cic() == m_uptCicCode &&
	    (msg->type() == SS7MsgISUP::UPA ||
	     msg->type() == SS7MsgISUP::CVR ||
	     msg->type() == SS7MsgISUP::CNF ||
	     msg->type() == SS7MsgISUP::UEC)) {
	    m_uptCicCode = 0;
	    TelEngine::destruct(msg);
	    return true;
	}
    }

    switch (msg->type()) {
	case SS7MsgISUP::IAM:
	case SS7MsgISUP::SAM:
	case SS7MsgISUP::ACM:
	case SS7MsgISUP::EXM:
	case SS7MsgISUP::CPR:
	case SS7MsgISUP::ANM:
	case SS7MsgISUP::CON:
	case SS7MsgISUP::REL:
	case SS7MsgISUP::SGM:
	case SS7MsgISUP::CCR:
	case SS7MsgISUP::COT:
	case SS7MsgISUP::APM:
	case SS7MsgISUP::SUS:
	case SS7MsgISUP::RES:
	    processCallMsg(msg,label,sls);
	    break;
	case SS7MsgISUP::CRG:
	    switch (getChargeProcessType()) {
		case Confusion:
		    processControllerMsg(msg,label,sls);
		case Ignore:
		    break;
		default:
		    processCallMsg(msg,label,sls);
	    }
	    break;
	case SS7MsgISUP::RLC:
	    if (m_rscCic && m_rscCic->code() == msg->cic())
		processControllerMsg(msg,label,sls);
	    else {
		SignallingMessageTimer* m = findPendingMessage(SS7MsgISUP::RSC,msg->cic(),true);
		if (m) {
		    DDebug(this,DebugAll,"RSC confirmed for pending cic=%u",msg->cic());
		    resetCircuit(msg->cic(),false,false);
		    TelEngine::destruct(m);
		}
		else
		    processCallMsg(msg,label,sls);
	    }
	    break;
	default:
	    processControllerMsg(msg,label,sls);
    }

    TelEngine::destruct(msg);
    return true;
}

// MTP notification that remote user part is unavailable
void SS7ISUP::receivedUPU(SS7PointCode::Type type, const SS7PointCode node,
    SS7MSU::Services part, unsigned char cause, const SS7Label& label, int sls)
{
    if (part != sif() || !handlesRemotePC(node))
	return;
    if (!(m_userPartAvail && m_uptTimer.interval()))
	return;
    const char* oldStat = statusName();
    Debug(this,DebugNote,"Remote User Part is unavailable (received UPU)");
    m_userPartAvail = false;
    m_uptTimer.start();
    if (statusName() != oldStat) {
	NamedList params("");
	params.addParam("from",toString());
	params.addParam("type","trunk");
	params.addParam("operational",String::boolText(m_l3LinkUp));
	params.addParam("available",String::boolText(m_userPartAvail));
	params.addParam("text",statusName());
	engine()->notify(this,params);
    }
}

// Process an event received from a non-reserved circuit
SignallingEvent* SS7ISUP::processCircuitEvent(SignallingCircuitEvent*& event,
    SignallingCall* call)
{
    if (!event)
	return 0;
    SignallingEvent* ev = 0;
    switch (event->type()) {
	case SignallingCircuitEvent::Alarm:
	case SignallingCircuitEvent::NoAlarm:
	    if (event->circuit()) {
		lock();
		bool block = (event->type() == SignallingCircuitEvent::Alarm);
		bool blocked = (0 != event->circuit()->locked(SignallingCircuit::LockLocalHWFail));
		// Avoid notifying the same state
		if (block != blocked) {
		    event->circuit()->hwLock(block,false,true,true);
		    if (!m_lockTimer.started())
			m_lockTimer.start();
		    if (block)
			cicHwBlocked(event->circuit()->code(),String("1"));
		}
		unlock();
		ev = new SignallingEvent(event,call);
	    }
	    break;
	case SignallingCircuitEvent::Dtmf:
	    if (event->getValue(YSTRING("tone"))) {
		SignallingMessage* msg = new SignallingMessage(event->c_str());
		msg->params().addParam("tone",event->getValue(YSTRING("tone")));
		msg->params().addParam("inband",event->getValue(YSTRING("inband"),String::boolText(true)));
		ev = new SignallingEvent(SignallingEvent::Info,msg,call);
		TelEngine::destruct(msg);
	    }
	    break;
	default:
	    ev = new SignallingEvent(event,call);
    }
    TelEngine::destruct(event);
    return ev;
}

// Initiate a circuit reset
bool SS7ISUP::startCircuitReset(SignallingCircuit*& cic, const String& timer)
{
    if (!cic)
	return false;
    bool ok = false;
    do {
	Lock lock(this);
	// Check if the circuit can be reset
	// Do nothing on locally locked circuit: this would clear our lock
	// state at remote side. See Q.764 2.9.3.1
	if (cic->locked(SignallingCircuit::LockLocal)) {
	    Debug(this,DebugNote,
		"Failed to start reset on locally locked circuit (cic=%u timer=%s) [%p]",
		cic->code(),timer.c_str(),this);
	    ok = SignallingCallControl::releaseCircuit(cic);
	    break;
	}
	// Check if there is any management operation in progress on the cic
	if (cic->locked(SignallingCircuit::LockBusy))
	    break;
	bool relTimeout = (timer == "T5");
	Debug(this,!relTimeout ? DebugAll : DebugNote,
	    "Starting circuit %u reset on timer %s [%p]",
	    cic->code(),timer.c_str(),this);
	// TODO: alert maintenance if T5 timer expired
	SignallingMessageTimer* m = 0;
	if (relTimeout)
	    m = new SignallingMessageTimer(m_t17Interval);
	else
	    m = new SignallingMessageTimer(m_t16Interval,m_t17Interval);
	m = m_pending.add(m);
	if (m) {
	    cic->setLock(SignallingCircuit::Resetting);
	    SS7MsgISUP* msg = new SS7MsgISUP(SS7MsgISUP::RSC,cic->code());
	    msg->params().addParam("isup_pending_reason",timer,false);
	    if (relTimeout)
		msg->params().addParam("isup_alert_maint",String::boolText(true));
	    msg->ref();
	    m->message(msg);
	    lock.drop();
	    ok = true;
	    SS7Label label;
	    if (setLabel(label,msg->cic()))
		transmitMessage(msg,label,false);
	}
	else {
	    Debug(this,DebugNote,
		"Failed to add circuit %u reset to pending messages timer=%s [%p]",
		cic->code(),timer.c_str(),this);
	    ok = SignallingCallControl::releaseCircuit(cic);
	}
    } while (false);
    TelEngine::destruct(cic);
    return ok;
}

// Process call related messages
void SS7ISUP::processCallMsg(SS7MsgISUP* msg, const SS7Label& label, int sls)
{
    // Find a call for this message, create a new one or drop the message
    RefPointer<SS7ISUPCall> call;
    findCall(msg->cic(),call);
    const char* reason = 0;
    while (true) {
	#define DROP_MSG(res) { reason = res; break; }
	// Avoid cic == 0
	if (!msg->cic())
	    DROP_MSG("invalid CIC")
	// non IAM message. Drop it if there is no call for it
	if ((msg->type() != SS7MsgISUP::IAM) && (msg->type() != SS7MsgISUP::CCR)) {
	    if (!call) {
		if (msg->type() == SS7MsgISUP::REL)
		    DROP_MSG("no call")
		if (msg->type() != SS7MsgISUP::RLC) {
		    // Initiate circuit reset
		    SignallingCircuit* cic = 0;
		    String s(msg->cic());
		    if (reserveCircuit(cic,0,SignallingCircuit::LockLockedBusy,&s))
			startCircuitReset(cic,"T16");
		}
		return;
	    }
	    break;
	}
	// IAM or CCR message
	SignallingCircuit* circuit = 0;
	// Check collision
	if (call) {
	    // If existing call is an incoming one, drop the message (retransmission ?)
	    if (!call->outgoing()) {
		if (msg->type() == SS7MsgISUP::CCR)
		    break;
		else
		    DROP_MSG("retransmission")
	    }
	    Debug(this,DebugNote,"Incoming call %u collide with existing outgoing",msg->cic());
	    // *** See Q.764 2.9.1.4
	    // Drop the request if the outgoing call already received some response or
	    // the destination point code is greater then the originating and the CIC is even
	    if (call->state() > SS7ISUPCall::Setup)
		DROP_MSG("collision - outgoing call responded")
	    // The greater point code should have the even circuit
	    unsigned int dpc = label.dpc().pack(label.type());
	    unsigned int opc = label.opc().pack(label.type());
	    bool controlling = (dpc > opc);
	    bool even = (0 == (msg->cic() % 2));
	    if (controlling == even)
		DROP_MSG("collision - we control the CIC")
	    // Accept the incoming request. Change the call's circuit
	    reserveCircuit(circuit,call->cicRange(),SignallingCircuit::LockLockedBusy);
	    call->replaceCircuit(circuit);
	    circuit = 0;
	    call = 0;
	}
	int flags = SignallingCircuit::LockLockedBusy;
	// Q.764 2.8.2 - accept test calls even if the remote side is blocked
	// Q.764 2.8.2.3 (xiv) - unblock remote side of the circuit for non-test calls
	if ((msg->type() == SS7MsgISUP::CCR) ||
	    (msg->params()[YSTRING("CallingPartyCategory")] == YSTRING("test"))) {
	    Debug(this,DebugInfo,"Received test call on circuit %u",msg->cic());
	    flags = 0;
	}
	else {
	    circuit = circuits() ? circuits()->find(msg->cic()) : 0;
	    if (circuit && circuit->locked(SignallingCircuit::LockRemote)) {
		Debug(this,DebugNote,"Unblocking remote circuit %u on IAM request",msg->cic());
		circuit->hwLock(false,true,0!=circuit->locked(SignallingCircuit::LockRemoteHWFail),false);
		circuit->maintLock(false,true,0!=circuit->locked(SignallingCircuit::LockRemoteMaint),false);
		m_verifyEvent = true;
	    }
	    circuit = 0;
	}
	String s(msg->cic());
	if (reserveCircuit(circuit,0,flags,&s,true)) {
	    call = new SS7ISUPCall(this,circuit,label.dpc(),label.opc(),false,label.sls(),
		0,msg->type() == SS7MsgISUP::CCR);
	    m_calls.append(call);
	    break;
	}
	// Congestion: send REL
	SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::REL,msg->cic());
	m->params().addParam("CauseIndicators","congestion");
	transmitMessage(m,label,true);
	DROP_MSG("can't reserve circuit")
	#undef DROP_MSG
    }
    if (!reason) {
	msg->ref();
	call->enqueue(msg);
    }
    else {
	if (msg->type() != SS7MsgISUP::IAM && msg->type() != SS7MsgISUP::RLC)
	    transmitRLC(this,msg->cic(),label,true);
	if (msg->type() != SS7MsgISUP::RLC)
	    Debug(this,DebugNote,"'%s' with cic=%u: %s",msg->name(),msg->cic(),reason);
    }
}

unsigned int getRangeAndStatus(NamedList& nl, unsigned int minRange, unsigned int maxRange,
    unsigned int maxMap = 0, String** map = 0, unsigned int nCicsMax = 0)
{
    unsigned int range = nl.getIntValue(YSTRING("RangeAndStatus"));
    if (range < minRange || range > maxRange)
	return 0;
    if (!maxMap)
	return range;
    NamedString* ns = nl.getParam(YSTRING("RangeAndStatus.map"));
    if (!ns || ns->length() > maxMap || ns->length() < range)
	return 0;
    if (map) {
	if (nCicsMax) {
	    // Check the number of bits set to 1 (circuits affected)
	    for (unsigned int i = 0; i < ns->length(); i++) {
		if ((*ns)[i] != '1')
		    continue;
		if (!nCicsMax)
                   return 0;
		nCicsMax--;
           }
	}
	*map = ns;
    }
    return range;
}

// Retrieve maintenance/hwfail type indicator
// Return false if invalid
static bool getGrpTypeInd(SS7ISUP* isup, SS7MsgISUP* msg, bool& hwFail, NamedString** ns = 0)
{
    if (!msg)
	return false;
    NamedString* s = msg->params().getParam(YSTRING("GroupSupervisionTypeIndicator"));
    if (s) {
	if (ns)
	    *ns = s;
	hwFail = (*s == YSTRING("hw-failure"));
	if (hwFail || (*s == YSTRING("maintenance")))
	    return true;
    }
    Debug(isup,DebugNote,"%s with unknown/unsupported GroupSupervisionTypeIndicator=%s [%p]",
	msg->name(),TelEngine::c_safe(s),isup);
    return false;
}

// Utility: set invalid-ie reason and diagnostic
static inline void setInvalidIE(unsigned char ie, const char*& reason, String& diagnostic)
{
    reason = "invalid-ie";
    diagnostic.hexify(&ie,1);
}

// Process controller related messages
// Q.764 2.1.12: stop waiting for SGM if message is not: COT,BLK,BLA,UBL,UBA,CGB,CGA,CGU,CUA,CQM,CQR
void SS7ISUP::processControllerMsg(SS7MsgISUP* msg, const SS7Label& label, int sls)
{
    const char* reason = 0;
    String diagnostic;
    bool impl = true;
    bool stopSGM = false;

    // TODO: Check if segmentation should stop for all affected circuits received withing range (CGB,GRS, ...)

    switch (msg->type()) {
	case SS7MsgISUP::CNF: // Confusion
	    // TODO: check if this message was received in response to RSC, UBL, UBK, CGB, CGU
	    Debug(this,DebugNote,"%s with cic=%u cause='%s' diagnostic='%s'",
		msg->name(),msg->cic(),
		msg->params().getValue(YSTRING("CauseIndicators")),
		msg->params().getValue(YSTRING("CauseIndicators.diagnostic")));
	    stopSGM = true;
	    break;
	case SS7MsgISUP::RLC: // Release Complete
	    // Response to RSC: reset local lock flags. Release m_rscCic
	    resetCircuit(msg->cic(),false,false);
	    break;
	case SS7MsgISUP::RSC: // Reset Circuit
	    if (resetCircuit(msg->cic(),true,true)) {
		// Send BLK on previously blocked cic: Q.764 2.9.3.1 c)
		lock();
		SignallingCircuit* cic = circuits() ? circuits()->find(msg->cic()) : 0;
		SS7MsgISUP* m = 0;
		if (cic && cic->locked(SignallingCircuit::LockLocalMaint) &&
		    !cic->locked(SignallingCircuit::LockingMaint))
		    m = buildCicBlock(cic,true,true);
		unlock();
		if (m)
		    transmitMessage(m,label,true);
		transmitRLC(this,msg->cic(),label,true);
	    }
	    else
		reason = "unknown-channel";
	    stopSGM = true;
	    break;
	case SS7MsgISUP::GRS: // Circuit Group Reset
	    stopSGM = true;
	    {
		// Q.763 3.43 min=1 max=31
		unsigned int n = getRangeAndStatus(msg->params(),1,31);
		if (!n) {
		    Debug(this,DebugNote,"%s with invalid range %s",msg->name(),
			msg->params().getValue(YSTRING("RangeAndStatus")));
		    break;
		}
		else if (n == 1 && m_ignoreGRSSingle) {
		    Debug(this,DebugAll,"Ignoring %s with range 1",msg->name());
		    break;
		}
		String map('0',n);
		char* d = (char*)map.c_str();
		for (unsigned int i = 0; i < n; i++)
		    if (!resetCircuit(msg->cic()+i,true,true))
			d[i] = '1';
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::GRA,msg->cic());
		m->params().addParam("RangeAndStatus",String(n));
		m->params().addParam("RangeAndStatus.map",map);
		transmitMessage(m,label,true);
	    }
	    break;
	case SS7MsgISUP::UBL: // Unblocking
	    if (blockCircuit(msg->cic(),false,true,false,true,true))
		transmitMessage(new SS7MsgISUP(SS7MsgISUP::UBA,msg->cic()),label,true);
	    else
		reason = "unknown-channel";
	    break;
	case SS7MsgISUP::BLK: // Blocking
	    if (blockCircuit(msg->cic(),true,true,false,true,true)) {
		transmitMessage(new SS7MsgISUP(SS7MsgISUP::BLA,msg->cic()),label,true);
		// Replace circuit for outgoing call in initial state
		replaceCircuit(msg->cic(),String('1'));
	    }
	    else
		reason = "unknown-channel";
	    break;
	case SS7MsgISUP::UBA: // Unblocking Acknowledgement
	case SS7MsgISUP::BLA: // Blocking Acknowledgement
	    {
		bool block = (msg->type() == SS7MsgISUP::BLA);
		SS7MsgISUP::Type type = block ? SS7MsgISUP::BLK : SS7MsgISUP::UBL;
		SignallingMessageTimer* t = findPendingMessage(type,msg->cic(),true);
		if (t) {
		    SS7MsgISUP* m = static_cast<SS7MsgISUP*>(t->message());
		    bool hw = m && m->params().getBoolValue(YSTRING("isup_pending_block_hwfail"));
		    DDebug(this,m ? DebugAll : DebugNote,"%s confirmed for pending cic=%u",
			  block ? "BLK" : "UBL",msg->cic());
		    TelEngine::destruct(t);
		    blockCircuit(msg->cic(),block,false,hw,true,false,true);
		    sendLocalLock();
		}
		else
		    reason = "wrong-state-message";
	    }
	    break;
	case SS7MsgISUP::CGA: // Circuit Group Blocking Acknowledgement
	case SS7MsgISUP::CUA: // Circuit Group Unblocking Acknowledgement
	    // Q.763 3.43 range can be 1..256. Max bits set to 1 should be 32
	    // Bit: 0-no indication 1-block/unblock
	    {
		bool hwFail = false;
		NamedString* grpSuperType = 0;
		if (!getGrpTypeInd(this,msg,hwFail,&grpSuperType))
		    break;
		String* srcMap = 0;
		unsigned int nCics = getRangeAndStatus(msg->params(),1,256,256,&srcMap,32);
		if (!nCics) {
		    Debug(this,DebugNote,"%s (%s) cic=%u with invalid range %s or map=%s",
			msg->name(),grpSuperType->c_str(),msg->cic(),
			msg->params().getValue(YSTRING("RangeAndStatus")),
			msg->params().getValue(YSTRING("RangeAndStatus.map")));
		    break;
		}
		bool block = (msg->type() == SS7MsgISUP::CGA);
		lock();
		// Check for correct response: same msg type, circuit code, type indicator, circuit map
		SS7MsgISUP::Type type = block ? SS7MsgISUP::CGB : SS7MsgISUP::CGU;
		SignallingMessageTimer* t = findPendingMessage(type,msg->cic(),
		    grpSuperType->name(),*grpSuperType);
		if (!t) {
		    Debug(this,DebugNote,"%s (%s) cic=%u: no request for it in our queue",
			msg->name(),grpSuperType->c_str(),msg->cic());
		    unlock();
		    break;
		}
		SS7MsgISUP* m = static_cast<SS7MsgISUP*>(t->message());
		String map;
		while (m) {
		    // Check map
		    map = m->params()[YSTRING("RangeAndStatus.map")];
		    if (!map)
			break;
		    if (map.length() != nCics) {
			map.clear();
			break;
		    }
		    for (unsigned int i = 0; i < map.length(); i++)
			if (map[i] == '0' && (*srcMap)[i] != '0') {
			    map.clear();
			    break;
			}
		    break;
		}
		if (map) {
		    DDebug(this,DebugAll,"%s (%s) confirmed for pending cic=%u",
			m->name(),grpSuperType->c_str(),msg->cic());
		    m_pending.remove(t);
		}
		unlock();
		if (!map) {
		    Debug(this,DebugNote,"%s (%s) cic=%u with unnacceptable range %s or map=%s",
			msg->name(),grpSuperType->c_str(),msg->cic(),
			msg->params().getValue(YSTRING("RangeAndStatus")),
			msg->params().getValue(YSTRING("RangeAndStatus.map")));
		    break;
		}
		for (unsigned int i = 0; i < map.length(); i++)
		    if (map[i] != '0')
			blockCircuit(msg->cic()+i,block,false,hwFail,true,false,true);
		sendLocalLock();
	    }
	    break;
	case SS7MsgISUP::CGB: // Circuit Group Blocking
	case SS7MsgISUP::CGU: // Circuit Group Unblocking
	    // Q.763 3.43 range can be 1..256. Max bits set to 1 should be 32
	    // Bit: 0-no indication 1-block/unblock
	    {
		bool hwFail = false;
		if (!getGrpTypeInd(this,msg,hwFail))
		    break;
	        bool block = (msg->type() == SS7MsgISUP::CGB);
		String* srcMap = 0;
		unsigned int nCics = getRangeAndStatus(msg->params(),1,256,256,&srcMap,32);
		if (!nCics) {
		    Debug(this,DebugNote,"%s with invalid range %s or map=%s",msg->name(),
			msg->params().getValue(YSTRING("RangeAndStatus")),
			msg->params().getValue(YSTRING("RangeAndStatus.map")));
		    break;
		}
		else if (nCics == 1 && ((block && m_ignoreCGBSingle) || (!block && m_ignoreCGUSingle))) {
		    Debug(this,DebugAll,"Ignoring %s with range 1",msg->name());
		    break;
		}
		String map('0',srcMap->length());
		char* d = (char*)map.c_str();
		for (unsigned int i = 0; i < srcMap->length(); i++)
		    if (srcMap->at(i) != '0' && blockCircuit(msg->cic()+i,block,true,hwFail,true,true))
			d[i] = '1';
		SS7MsgISUP* m = new SS7MsgISUP(block?SS7MsgISUP::CGA:SS7MsgISUP::CUA,msg->cic());
		m->params().copyParam(msg->params(),"GroupSupervisionTypeIndicator");
		m->params().addParam("RangeAndStatus",String(nCics));
		m->params().addParam("RangeAndStatus.map",map);
		transmitMessage(m,label,true);
		// Replace circuits for outgoing calls in initial state
		// Terminate all others when blocking for hw failure
		if (block) {
		    if (hwFail)
			cicHwBlocked(msg->cic(),map);
		    else
			replaceCircuit(msg->cic(),map);
		}
	    }
	    break;
	case SS7MsgISUP::UEC: // Unequipped CIC (national use)
	    Debug(this,DebugWarn,"%s for cic=%u. Circuit is unequipped on remote side",
		msg->name(),msg->cic());
	    blockCircuit(msg->cic(),true,true,false,true,true);
	    break;
	case SS7MsgISUP::UPT: // User Part Test
	    transmitMessage(new SS7MsgISUP(SS7MsgISUP::UPA,msg->cic()),label,true);
	    break;
	case SS7MsgISUP::UPA: // User Part Available
	    if (m_uptCicCode && m_uptCicCode == msg->cic()) {
		DDebug(this,DebugInfo,"Received valid %s",msg->name());
		m_uptCicCode = 0;
	    }
#ifdef DEBUG
	    else
		Debug(this,DebugMild,"Received unexpected %s",msg->name());
#endif
	    break;
	case SS7MsgISUP::GRA: // Circuit Group Reset Acknowledgement
	    // TODO: stop receiving segments
	    reason = "wrong-state-message";
	    break;
	case SS7MsgISUP::CVT: // Circuit Validation Test (ANSI)
	    if (circuits() && circuits()->find(msg->cic())) {
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CVR,msg->cic());
		m->params().addParam("CircuitValidationRespIndicator","success");
		transmitMessage(m,label,true);
	    }
	    else
		reason = "unknown-channel";
	    break;
	case SS7MsgISUP::CQM: // Circuit Group Query (national use)
	    if (circuits()) {
		// Q.763 3.43 min=1 max=31
		unsigned int n = getRangeAndStatus(msg->params(),1,31);
		if (!n) {
		    reason = "invalid-ie";
		    break;
		}
		DataBlock si(0,n);
		for (unsigned int i = 0; i < n; i++) {
		    unsigned char* state = si.data(i);
		    if (!state)
			break;
		    SignallingCircuit* circuit = circuits()->find(msg->cic()+i);
		    if (circuit && (circuit->status() != SignallingCircuit::Missing)) {
			switch (circuit->locked(SignallingCircuit::LockLocalMaint | SignallingCircuit::LockRemoteMaint)) {
			    case SignallingCircuit::LockLocalMaint:
				*state = 0x01; // locally maint blocked
				break;
			    case SignallingCircuit::LockRemoteMaint:
				*state = 0x02; // remote maint blocked
				break;
			    case SignallingCircuit::LockLocalMaint | SignallingCircuit::LockRemoteMaint:
				*state = 0x03; // locally and remote maint blocked
				break;
			}
			switch (circuit->locked(SignallingCircuit::LockLocalHWFail | SignallingCircuit::LockRemoteHWFail)) {
			    case SignallingCircuit::LockLocalHWFail:
				*state |= 0x1c; // locally hw blocked
				continue;
			    case SignallingCircuit::LockRemoteHWFail:
				*state |= 0x2c; // locally hw blocked
				continue;
			    case SignallingCircuit::LockLocalHWFail | SignallingCircuit::LockRemoteHWFail:
				*state |= 0x3c; // locally and remotely hw blocked
				continue;
			}
			if (circuit->connected())
			    *state |= 0x04; // incoming busy
			else if (!circuit->available())
			    *state |= 0x08; // outgoing busy
			else
			    *state |= 0x0c; // idle
		    }
		    else
			*state = 0x03; // Unequipped
		}
		String tmp;
		tmp.hexify(si.data(),si.length(),' ');
		DDebug(this,DebugInfo,"Sending CQR (%u+%u): %s",msg->cic(),n,tmp.c_str());
		SS7MsgISUP* m = new SS7MsgISUP(SS7MsgISUP::CQR,msg->cic());
		m->params().addParam("RangeAndStatus",String(n));
		m->params().addParam("CircuitStateIndicator",tmp);
		transmitMessage(m,label,true);
	    }
	    else
		reason = "unknown-channel";
	    break;
	case SS7MsgISUP::CQR: // Circuit Group Query Response (national use)
	case SS7MsgISUP::CVR: // Circuit Validation Response (ANSI)
	case SS7MsgISUP::LPA: // Loopback Acknowledge (national use)
	    // Known but not implemented responses, just ignore them
	    impl = false;
	    break;
	default:
	    impl = false;
	    // Q.764 2.9.5.1: call in Setup state:
	    // incoming: drop it and reset cic
	    // outgoing: repeat IAM and reset cic
	    lock();
	    SS7ISUPCall* call = findCall(msg->cic());
	    if (call)
		call->ref();
	    unlock();
	    if (m_dropOnUnknown && call && call->earlyState() && msg->type() != SS7MsgISUP::CRG) {
		Debug(this,DebugNote,
		    "Received unexpected message for call %u (%p) in initial state",
		    msg->cic(),call);
		if (call->outgoing())
		    replaceCircuit(msg->cic(),String("1"),false);
		else {
		    call->setTerminate(false,"normal",0,m_location);
		    SignallingCircuit* c = call->m_circuit;
		    if (c && c->ref())
			startCircuitReset(c,String::empty());
		}
	    }
	    else {
		bool cnf = false;
		bool release = false;
		getMsgCompat(msg,release,cnf);
		if (cnf || release) {
		    reason = "unknown-message";
		    unsigned char type = msg->type();
		    diagnostic.hexify(&type,1);
		    if (release) {
			if (call)
			    call->setTerminate(true,reason,diagnostic,m_location);
			else
			    transmitRLC(this,msg->cic(),label,true,reason,diagnostic,m_location);
			// Avoid sending CNF
			reason = 0;
		    }
		}
	    }
	    TelEngine::destruct(call);
    }
    if (stopSGM) {
	RefPointer<SS7ISUPCall> call;
	findCall(msg->cic(),call);
	if (call)
	    call->stopWaitSegment(false);
	call = 0;
    }
    if (reason || !impl) {
	Debug(this,impl?DebugNote:DebugStub,"'%s' with cic=%u: %s",
	    msg->name(),msg->cic(),(reason ? reason : "Not implemented, ignoring"));
	if (reason)
	    transmitCNF(this,msg->cic(),label,true,reason,diagnostic);
    }
}

// Replace a call's circuit if checkCall is true
// Release currently reseting circuit if the code match
// Clear lock flags
// See Q.764 2.9.3.1
bool SS7ISUP::resetCircuit(unsigned int cic, bool remote, bool checkCall)
{
    SignallingCircuit* circuit = circuits() ? circuits()->find(cic) : 0;
    if (!circuit)
	return false;
    DDebug(this,DebugAll,"Reseting circuit %u",cic);
    if (checkCall) {
	RefPointer<SS7ISUPCall> call;
	findCall(cic,call);
	if (call) {
	    if (call->outgoing() && call->state() == SS7ISUPCall::Setup) {
	        SignallingCircuit* newCircuit = 0;
		reserveCircuit(newCircuit,call->cicRange(),SignallingCircuit::LockLockedBusy);
		call->replaceCircuit(newCircuit);
	    }
	    else
		call->setTerminate(false,"normal");
	}
    }
    // Remove remote lock flags (Q.764 2.9.3.1)
    if (remote && circuit->locked(SignallingCircuit::LockRemote)) {
	Debug(this,DebugNote,"Unblocking remote circuit %u on reset request",cic);
	circuit->hwLock(false,true,0!=circuit->locked(SignallingCircuit::LockRemoteHWFail),false);
	circuit->maintLock(false,true,0!=circuit->locked(SignallingCircuit::LockRemoteMaint),false);
	m_verifyEvent = true;
    }
    // Remove pending RSC/REL. Reset 'Resetting' flag'
    SignallingMessageTimer* m = findPendingMessage(SS7MsgISUP::RSC,cic,true);
    if (!m)
	m = findPendingMessage(SS7MsgISUP::REL,cic,true);
    if (m) {
	Debug(this,DebugAll,"Pending %s`cic=%u removed",m->message()->name(),cic);
	TelEngine::destruct(m);
    }
    circuit->resetLock(SignallingCircuit::Resetting);
    if (m_rscCic && m_rscCic->code() == cic)
	releaseCircuit(m_rscCic);
    else
	circuit->status(SignallingCircuit::Idle);
    return true;
}

// Block/unblock a circuit
// See Q.764 2.8.2
bool SS7ISUP::blockCircuit(unsigned int cic, bool block, bool remote, bool hwFail,
	bool changed, bool changedState, bool resetLocking)
{
    XDebug(this,DebugAll,"blockCircuit(%u,%u,%u,%u,%u,%u,%u)",
	cic,block,remote,hwFail,changed,changedState,resetLocking);
    SignallingCircuit* circuit = circuits() ? circuits()->find(cic) : 0;
    if (!circuit)
	return false;

    bool something = false;
    if (hwFail)
	something = circuit->hwLock(block,remote,changed,changedState);
    else
	something = circuit->maintLock(block,remote,changed,changedState);
    if (resetLocking && !remote)
	circuit->resetLock(hwFail ? SignallingCircuit::LockingHWFail : SignallingCircuit::LockingMaint);

    if (something) {
	Debug(this,DebugNote,"%s %s side of circuit %u. Current flags 0x%x",
	    (block ? "Blocked" : "Unblocked"),
	    (remote ? "remote" : "local"),
	    cic,circuit->locked(-1));
	m_verifyEvent = true;
    }
    return true;
}

SS7ISUPCall* SS7ISUP::findCall(unsigned int cic)
{
    for (ObjList* o = m_calls.skipNull(); o; o = o->skipNext()) {
	SS7ISUPCall* call = static_cast<SS7ISUPCall*>(o->get());
	if (call->id() == cic)
	    return call;
    }
    return 0;
}

// Utility used in sendLocalLock()
// Check if a circuit has lock change flag set and can be locked (not busy)
static inline bool canLock(SignallingCircuit* cic, bool hw)
{
    if (hw)
	return cic->locked(SignallingCircuit::LockLocalHWFailChg) &&
            !cic->locked(SignallingCircuit::LockingHWFail | SignallingCircuit::Resetting);
    return cic->locked(SignallingCircuit::LockLocalMaintChg) &&
	!cic->locked(SignallingCircuit::LockingMaint | SignallingCircuit::Resetting);
}

// Utility: check if a circuit needs lock and not currently locking
static inline void checkNeedLock(SignallingCircuit* cic, bool& needLock)
{
    if (needLock)
	return;
    needLock = cic->locked(SignallingCircuit::LockLocalChg) &&
        !cic->locked(SignallingCircuit::LockingHWFail | SignallingCircuit::LockingMaint);
}

// Send blocking/unblocking messages
// Return false if no request was sent
bool SS7ISUP::sendLocalLock(const Time& when)
{
    Lock lock(this);
    if (!circuits())
	return false;
    bool needLock = false;
    ObjList msgs;
    while (true) {
	bool hwReq = false;
	bool lockReq = false;
	unsigned int code = 0;
	int locking = 0;
	// Peek a starting circuit whose local state changed
	ObjList* o = circuits()->circuits().skipNull();
	SignallingCircuitSpan* span = 0;
	for (; o; o = o->skipNext()) {
	    SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
	    if (canLock(cic,true)) {
		hwReq = true;
		lockReq = (0 != cic->locked(SignallingCircuit::LockLocalHWFail));
		locking = SignallingCircuit::LockingHWFail;
	    }
	    else if (canLock(cic,false)) {
		hwReq = false;
		lockReq = (0 != cic->locked(SignallingCircuit::LockLocalMaint));
		locking = SignallingCircuit::LockingMaint;
	    }
	    else {
		checkNeedLock(cic,needLock);
		continue;
	    }
	    code = cic->code();
	    span = cic->span();
	    cic->setLock(locking);
	    o = o->skipNext();
	    break;
	}
	if (!code)
	    break;
	// If remote doesn't support group block/unblock just send BLK/UBL
	if (!m_lockGroup)
	    o = 0;
	// Check if we can pick a range of circuits within the same span
	//  with the same operation to do
	// Q.763 3.43: range can be 2..256. Bit: 0-no indication 1-block/unblock.
	// Max bits set to 1 must be 32
	char d[256];
	d[0] = '1';
	unsigned int cics = 1;
	unsigned int lockRange = 1;
	ObjList* cicPos = o;
	int newRange = 0;
	int flag = hwReq ? SignallingCircuit::LockLocalHWFail : SignallingCircuit::LockLocalMaint;
	for (; o && cics < 32 && lockRange < 256; o = o->skipNext()) {
	    SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
	    // Presume all circuits belonging to the same span to follow each other in the list
	    if (span != cic->span())
		break;
	    // Make sure the circuit codes are sequential
	    if ((code + lockRange) != cic->code()) {
		if (!newRange)
		    newRange = checkValidRange(code,cic->code());
		checkNeedLock(cic,needLock);
		continue;
	    }
	    // Add circuit to map. Skip busy circuits
	    // Circuit must have the same lock type and flags as the base circuit's
	    if (canLock(cic,hwReq) && (lockReq == (0 != cic->locked(flag)))) {
		cic->setLock(locking);
		d[lockRange] = '1';
		cics++;
	    }
	    else {
		checkNeedLock(cic,needLock);
		d[lockRange] = '0';
	    }
	    lockRange++;
	}
	if (cics == 1) {
	    if (lockRange > 1) {
		// Shorten the range: be nice to remote party, no need to check the whole map
		if (hwReq)
		    lockRange = 2;
		else
		    lockRange = 1;
	    }
	    else if (m_lockGroup && hwReq) {
		if (!newRange) {
		    // Bad luck: check for a code before the circuit
		    for (o = circuits()->circuits().skipNull(); o && o != cicPos; o = o->skipNext()) {
			SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
			if (span != cic->span())
			    continue;
			newRange = checkValidRange(code,cic->code());
			if (newRange)
			    break;
		    }
		}
		if (newRange)
		    adjustRangeAndStatus(d,code,lockRange,newRange);
		else
		    Debug(this,DebugNote,
			"Failed to pick a second circuit to group HW %sblock cic=%u [%p]",
			lockReq ? "" : "un",code,this);
	    }
	}
	else {
	    // Shorten range
	    unsigned int last = lockRange;
	    while (d[--last] == '0')
		lockRange--;
	}
	// Build and send the message
	// Don't send individual circuit blocking for HW failure (they are supposed
	//  to be sent for maintenance reason)
	String map(d,lockRange);
	SS7MsgISUP* msg = 0;
	SignallingMessageTimer* t = 0;
	if (m_lockGroup && (map.length() > 1 || hwReq)) {
	    msg = new SS7MsgISUP((lockReq ? SS7MsgISUP::CGB : SS7MsgISUP::CGU),code);
	    msg->params().addParam("GroupSupervisionTypeIndicator",
		(hwReq ? "hw-failure" : "maintenance"));
	    msg->params().addParam("RangeAndStatus",String(map.length()));
	    msg->params().addParam("RangeAndStatus.map",map);
	    if (lockReq)
		t = new SignallingMessageTimer(m_t18Interval,m_t19Interval);
	    else
		t = new SignallingMessageTimer(m_t20Interval,m_t21Interval);
	}
	else {
	    msg = new SS7MsgISUP(lockReq ? SS7MsgISUP::BLK : SS7MsgISUP::UBL,code);
	    // Remember HW/maintenance flag
	    if (hwReq)
		msg->params().addParam("isup_pending_block_hwfail",String::boolText(true));
	    if (lockReq)
		t = new SignallingMessageTimer(m_t12Interval,m_t13Interval);
	    else
		t = new SignallingMessageTimer(m_t14Interval,m_t15Interval);
	}
	t->message(msg);
	m_pending.add(t);
	msg->ref();
	msgs.append(msg)->setDelete(false);
    }
    // Restart timer if we still have cics needing lock
    DDebug(this,DebugAll,"%s circuit locking timer",needLock ? "Starting" : "Stopping");
    if (needLock)
	m_lockTimer.start(when.msec());
    else
	m_lockTimer.stop();
    lock.drop();
    return transmitMessages(msgs);
}

// Fill label from local/remote point codes
bool SS7ISUP::setLabel(SS7Label& label, unsigned int cic)
{
    Lock lock(this);
    if (!(m_remotePoint && m_defPoint))
	return false;
    label.assign(m_type,*m_remotePoint,*m_defPoint,
	(m_defaultSls == SlsCircuit) ? cic : m_sls);
    return true;
}

// Retrieve a pending message
SignallingMessageTimer* SS7ISUP::findPendingMessage(SS7MsgISUP::Type type, unsigned int cic,
    bool remove)
{
    Lock lock(this);
    for (ObjList* o = m_pending.skipNull(); o; o = o->skipNext()) {
	SignallingMessageTimer* m = static_cast<SignallingMessageTimer*>(o->get());
	SS7MsgISUP* msg = static_cast<SS7MsgISUP*>(m->message());
	if (msg && msg->type() == type && msg->cic() == cic) {
	    if (remove)
		o->remove(false);
	    return m;
	}
    }
    return 0;
}

// Retrieve a pending message with given parameter
SignallingMessageTimer* SS7ISUP::findPendingMessage(SS7MsgISUP::Type type, unsigned int cic,
    const String& param, const String& value, bool remove)
{
    Lock lock(this);
    for (ObjList* o = m_pending.skipNull(); o; o = o->skipNext()) {
	SignallingMessageTimer* m = static_cast<SignallingMessageTimer*>(o->get());
	SS7MsgISUP* msg = static_cast<SS7MsgISUP*>(m->message());
	if (msg && msg->type() == type && msg->cic() == cic && msg->params()[param] == value) {
	    if (remove)
		o->remove(false);
	    return m;
	}
    }
    return 0;
}

// Transmit a list of messages. Return true if at least 1 message was sent
bool SS7ISUP::transmitMessages(ObjList& list)
{
    ObjList* o = list.skipNull();
    if (!o)
	return false;
    for (; o; o = o->skipNext()) {
	SS7MsgISUP* msg = static_cast<SS7MsgISUP*>(o->get());
    	SS7Label label;
	setLabel(label,msg->cic());
	if (m_duplicateCGB && (msg->type() == SS7MsgISUP::CGB)) {
	    // ANSI needs the CGB duplicated
	    msg->ref();
	    transmitMessage(msg,label,false);
	}
	transmitMessage(msg,label,false);
    }
    return true;
}

// Utility: check if a circuit exists and can be start an (un)block operation
static const char* checkBlockCic(SignallingCircuit* cic, bool block, bool maint,
    bool force)
{
    if (!cic)
	return "not found";
    int flg = cic->locked(maint ? SignallingCircuit::LockLocalMaint :
	SignallingCircuit::LockLocalHWFail);
    if ((block == (0 != flg)) && !force)
	return "already in the same state";
    flg = maint ? SignallingCircuit::LockingMaint : SignallingCircuit::LockingHWFail;
    if (cic->locked(flg | SignallingCircuit::Resetting) && !force)
	return "busy locking or resetting";
    return 0;
}

// Handle circuit(s) (un)block command
bool SS7ISUP::handleCicBlockCommand(const NamedList& p, bool block)
{
    if (!circuits())
	return false;
    SS7MsgISUP* msg = 0;
    SS7MsgISUP::Type remove = SS7MsgISUP::Unknown;
    bool force = p.getBoolValue(YSTRING("force"));
    String* param = p.getParam(YSTRING("circuit"));
    bool remote = p.getBoolValue(YSTRING("remote"));
    Lock mylock(this);
    if (param) {
	if (remote) {
	    unsigned int code = param->toInteger();
	    return handleCicBlockRemoteCommand(p,&code,1,block);
	}
	SignallingCircuit* cic = circuits()->find(param->toInteger());
	msg = buildCicBlock(cic,block,force);
	if (!msg)
	    return false;
	if (force)
	    remove = block ? SS7MsgISUP::UBL : SS7MsgISUP::BLK;
    }
    else {
	// NOTE: we assume the circuits belongs to the same span for local (un)block
	param = p.getParam(YSTRING("circuits"));
	if (TelEngine::null(param)) {
	    Debug(this,DebugNote,"Circuit '%s' missing circuit(s)",
		p.getValue(YSTRING("operation")));
	    return false;
	}
	// Parse the range
	unsigned int count = 0;
	unsigned int* cics = SignallingUtils::parseUIntArray(*param,1,0xffffffff,count,true);
	if (!cics) {
	    // Allow '*' (all circuits) for remote
	    if (!(remote && *param == YSTRING("*"))) {
		SignallingCircuitRange* range = circuits()->findRange(*param);
		if (range)
		    cics = range->copyRange(count);
	    }
	    else {
		String tmp;
		circuits()->getCicList(tmp);
		SignallingCircuitRange* range = new SignallingCircuitRange(tmp);
		cics = range->copyRange(count);
		TelEngine::destruct(range);
	    }
	    if (!cics) {
		Debug(this,DebugNote,"Circuit group '%s': invalid circuits=%s",
		    p.getValue(YSTRING("operation")),param->c_str());
		return false;
	    }
	}
	if (remote) {
	    bool ok = handleCicBlockRemoteCommand(p,cics,count,block);
	    delete[] cics;
	    return ok;
	}
	if (count > 32) {
	    Debug(this,DebugNote,"Circuit group '%s': too many circuits %u (max=32)",
		p.getValue(YSTRING("operation")),count);
	    delete[] cics;
	    return false;
	}
	// Check if all circuits can be (un)blocked
	ObjList list;
	bool maint = !p.getBoolValue(YSTRING("hwfail"));
	for (unsigned int i = 0; i < count; i++) {
	    SignallingCircuit* c = circuits()->find(cics[i]);
	    const char* reason = checkBlockCic(c,block,maint,force);
	    if (reason) {
		Debug(this,DebugNote,"Circuit group '%s' range=%s failed for cic=%u: %s",
		    p.getValue(YSTRING("operation")),param->c_str(),cics[i],reason);
		delete[] cics;
		return false;
	    }
	    list.append(c)->setDelete(false);
	}
	// Retrieve the code: the lowest circuit code
	unsigned int code = cics[0];
	for (unsigned int i = 1; i < count; i++)
	    if (cics[i] < code)
		code = cics[i];
	// Build the range. Fail if falling outside maximum range
	char d[256];
	::memset(d,'0',256);
	d[0] = '1';
	unsigned int lockRange = 1;
	unsigned int nCics = 0;
	for (; nCics < count; nCics++) {
	    if (code == cics[nCics])
		continue;
	    unsigned int pos = cics[nCics] - code;
	    if (pos > 255)
		break;
	    d[pos++] = '1';
	    if (pos > lockRange)
		lockRange = pos;
	}
	delete[] cics;
	if (nCics != count) {
	    Debug(this,DebugNote,"Circuit group '%s': invalid circuit map=%s",
		p.getValue(YSTRING("operation")),param->c_str());
	    return false;
	}
	if (nCics == 1) {
	    // Try to pick another circuit for map
	    SignallingCircuit* cic = static_cast<SignallingCircuit*>(list.skipNull()->get());
	    int newRange = 0;
	    for (ObjList* o = circuits()->circuits().skipNull(); o ; o = o->skipNext()) {
		SignallingCircuit* c = static_cast<SignallingCircuit*>(o->get());
		if (c->span() != cic->span() || c == cic)
		    continue;
		newRange = checkValidRange(cic->code(),c->code());
		if (newRange)
		    break;
	    }
	    if (!newRange) {
		Debug(this,DebugNote,
		    "Circuit group '%s': failed to pick another circuit to send group command",
		    p.getValue(YSTRING("operation")));
		return false;
	    }
	    adjustRangeAndStatus(d,code,lockRange,newRange);
	}
	// Ok: block circuits and send the request
	int flg = maint ? SignallingCircuit::LockingMaint : SignallingCircuit::LockingHWFail;
	for (ObjList* o = list.skipNull(); o ; o = o->skipNext()) {
	    SignallingCircuit* c = static_cast<SignallingCircuit*>(o->get());
	    blockCircuit(c->code(),block,false,!maint,true,true);
	    c->setLock(flg);
	}
	String map(d,lockRange);
        msg = new SS7MsgISUP(block ? SS7MsgISUP::CGB : SS7MsgISUP::CGU,code);
	msg->params().addParam("GroupSupervisionTypeIndicator",
	    (maint ? "maintenance" : "hw-failure"));
	msg->params().addParam("RangeAndStatus",String(map.length()));
	msg->params().addParam("RangeAndStatus.map",map);
	SignallingMessageTimer* t = 0;
	if (block)
	    t = new SignallingMessageTimer(m_t18Interval,m_t19Interval);
	else
	    t = new SignallingMessageTimer(m_t20Interval,m_t21Interval);
        t->message(msg);
	m_pending.add(t);
	msg->ref();
	if (force)
	    remove = block ? SS7MsgISUP::CGU : SS7MsgISUP::CGB;
    }
    if (SS7MsgISUP::Unknown != remove) {
	bool removed = false;
	SignallingMessageTimer* pending = 0;
	if (remove != SS7MsgISUP::CGB && remove != SS7MsgISUP::CGU) {
	    while (0 != (pending = findPendingMessage(remove,msg->cic(),true))) {
		TelEngine::destruct(pending);
		removed = true;
	    }
	}
	else {
	    NamedString* ns = msg->params().getParam(YSTRING("GroupSupervisionTypeIndicator"));
	    while (ns &&
		0 != (pending = findPendingMessage(remove,msg->cic(),ns->name(),*ns,true))) {
		TelEngine::destruct(pending);
		removed = true;
	    }
	}
	if (removed)
	    Debug(this,DebugNote,"Removed pending operation '%s' cic=%u",
		SS7MsgISUP::lookup(remove),msg->cic());
    }
    SS7Label label;
    setLabel(label,msg->cic());
    mylock.drop();
    if (m_duplicateCGB && (msg->type() == SS7MsgISUP::CGB)) {
	// ANSI needs the CGB duplicated
	msg->ref();
	transmitMessage(msg,label,false);
    }
    transmitMessage(msg,label,false);
    return true;
}

// Handle remote circuit(s) (un)block command
bool SS7ISUP::handleCicBlockRemoteCommand(const NamedList& p, unsigned int* cics,
    unsigned int count, bool block)
{
    if (!(cics && count))
	return false;
    bool hwFail = p.getBoolValue(YSTRING("hwfail"));
    if (debugAt(DebugNote)) {
	String s;
	for (unsigned int i = 0; i < count; i++)
	    s.append(String(cics[i]),",");
	Debug(this,DebugNote,"Circuit remote '%s' command: hwfail=%s circuits=%s [%p]",
	    p.getValue(YSTRING("operation")),String::boolText(hwFail),s.c_str(),this);
    }
    bool found = false;
    for (unsigned int i = 0; i < count; i++) {
	if (blockCircuit(cics[i],block,true,hwFail,true,true))
	    found = true;
	else
	    Debug(this,DebugNote,"Circuit remote '%s' command: cic %u not found [%p]",
		p.getValue(YSTRING("operation")),cics[i],this);
    }
    if (found)
	m_verifyEvent = true;
    return found;
}

// Handle circuit(s) event generation command
bool SS7ISUP::handleCicEventCommand(const NamedList& p)
{
    if (!circuits())
	return false;
    int evType = p.getIntValue(YSTRING("type"));
    if (evType <= 0) {
	Debug(this,DebugNote,"Control '%s': invalid type '%s'",
	    p.getValue(YSTRING("operation")),p.getValue(YSTRING("type")));
	return false;
    }
    ObjList cics;
    String* param = p.getParam(YSTRING("circuit"));
    if (param) {
	SignallingCircuit* cic = circuits()->find(param->toInteger());
	if (!cic) {
	    Debug(this,DebugNote,"Control '%s' circuit %s not found",
		p.getValue(YSTRING("operation")),param->c_str());
	    return false;
	}
	cics.append(cic)->setDelete(false);
    }
    else {
	param = p.getParam(YSTRING("circuits"));
	if (TelEngine::null(param)) {
	    Debug(this,DebugNote,"Control '%s' missing circuit(s)",
		p.getValue(YSTRING("operation")));
	    return false;
	}
	// Parse the range
	unsigned int count = 0;
	unsigned int* cList = SignallingUtils::parseUIntArray(*param,1,0xffffffff,count,true);
	if (!cList) {
	    Debug(this,DebugNote,"Control '%s' invalid circuits=%s",
		p.getValue(YSTRING("operation")),param->c_str());
	    return false;
	}
	for (unsigned int i = 0; i < count; i++) {
	    SignallingCircuit* cic = circuits()->find(cList[i]);
	    if (cic) {
		cics.append(cic)->setDelete(false);
		continue;
	    }
	    Debug(this,DebugNote,"Control '%s' circuit %u not found",
		p.getValue(YSTRING("operation")),cList[i]);
	    cics.clear();
	    break;
	}
	delete[] cList;
    }
    ObjList* o = cics.skipNull();
    if (!o)
	return false;
    for (; o; o = o->skipNext()) {
	SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
	SignallingCircuitEvent* cicEvent = new SignallingCircuitEvent(cic,
	    (SignallingCircuitEvent::Type)evType);
	cicEvent->copyParams(p);
	SignallingEvent* ev = processCircuitEvent(cicEvent,0);
	TelEngine::destruct(cicEvent);
	if (ev)
	    delete ev;
    }
    return true;
}

// Try to start single circuit (un)blocking. Set a pending operation on success
// @param force True to ignore resetting/(un)blocking flags of the circuit
// Return built message to be sent on success
SS7MsgISUP* SS7ISUP::buildCicBlock(SignallingCircuit* cic, bool block, bool force)
{
    const char* reason = checkBlockCic(cic,block,true,force);
    if (reason) {
	Debug(this,DebugNote,"Failed to start circuit %sblocking for %u: %s",
	    block ? "" : "un",cic ? cic->code() : 0,reason);
	return 0;
    }
    blockCircuit(cic->code(),block,false,false,true,true);
    cic->setLock(SignallingCircuit::LockingMaint);
    SS7MsgISUP* m = new SS7MsgISUP(block ? SS7MsgISUP::BLK : SS7MsgISUP::UBL,cic->code());
    SignallingMessageTimer* t = 0;
    if (block)
	t = new SignallingMessageTimer(m_t12Interval,m_t13Interval);
    else
        t = new SignallingMessageTimer(m_t14Interval,m_t15Interval);
    t->message(m);
    m_pending.add(t);
    m->ref();
    return m;
}

// Replace circuit for outgoing calls in Setup state
void SS7ISUP::replaceCircuit(unsigned int cic, const String& map, bool rel)
{
    ObjList calls;
    lock();
    for (unsigned int i = 0; i < map.length(); i++) {
	if (map[i] != '1')
	    continue;
        // Replace circuit for call (Q.764 2.8.2.1)
	SS7ISUPCall* call = findCall(cic + i);
	if (call && call->outgoing() && call->state() == SS7ISUPCall::Setup &&
	    call->ref())
	    calls.append(call);
    }
    unlock();
    for (ObjList* o = calls.skipNull(); o; o = o->skipNext()) {
	SS7ISUPCall* call = static_cast<SS7ISUPCall*>(o->get());
	Debug(this,DebugInfo,"Replacing remotely blocked cic=%u for existing call",call->id());
	SignallingCircuit* newCircuit = 0;
	if (call->canReplaceCircuit())
	    reserveCircuit(newCircuit,call->cicRange(),SignallingCircuit::LockLockedBusy);
	if (!newCircuit) {
	    call->setTerminate(rel,"congestion",0,m_location);
	    if (!rel) {
		SignallingCircuit* c = call->m_circuit;
		if (c && c->ref())
		    startCircuitReset(c,String::empty());
	    }
	    continue;
	}
	lock();
	SignallingCircuit* c = circuits()->find(call->id());
	SS7MsgISUP* m = 0;
	if (c && !c->locked(SignallingCircuit::Resetting)) {
	    c->setLock(SignallingCircuit::Resetting);
	    m = new SS7MsgISUP(rel ? SS7MsgISUP::REL : SS7MsgISUP::RSC,call->id());
	    if (rel) {
		m->params().addParam("CauseIndicators","normal");
		m->params().addParam("CauseIndicators.location",m_location,false);
	    }
	    m->ref();
	}
	unlock();
	call->replaceCircuit(newCircuit,m);
	if (m) {
	    SignallingMessageTimer* t = 0;
	    if (rel)
		t = new SignallingMessageTimer(m_t1Interval,m_t5Interval);
	    else
		t = new SignallingMessageTimer(m_t16Interval,m_t17Interval);
	    t->message(m);
	    m_pending.add(t);
	}
    }
}

// Handle circuit hw-fail block
// Replace cics for outgoing calls. Terminate incoming
void SS7ISUP::cicHwBlocked(unsigned int cic, const String& map)
{
    Debug(this,DebugNote,"Circuit(s) in HW failure cic=%u map=%s",cic,map.c_str());
    replaceCircuit(cic,map,true);
    ObjList terminate;
    lock();
    for (unsigned int i = 0; i < map.length(); i++) {
	if (map[i] != '1')
	    continue;
	SS7ISUPCall* call = findCall(cic + i);
	// We've made retransmit attempt for outgoing
	bool processed = !call || (call->outgoing() && call->state() == SS7ISUPCall::Setup);
	if (!processed && call->ref())
	    terminate.append(call);
    }
    unlock();
    setCallsTerminate(terminate,true,"normal",0,m_location);
}


/**
 * SS7BICC
 */
SS7BICC::SS7BICC(const NamedList& params, unsigned char sio)
    : SignallingComponent(params.safe("SS7BICC"),&params,"ss7-bicc"),
      SS7ISUP(params,sio)
{
    m_cicLen = 4;
    Debug(this,DebugInfo,"BICC Call Controller [%p]",this);
}

SS7BICC::~SS7BICC()
{
    cleanup();
    Debug(this,DebugInfo,"BICC Call Controller destroyed [%p]",this);
}

HandledMSU SS7BICC::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != sif() || !hasPointCode(label.dpc()) || !handlesRemotePC(label.opc()))
	return HandledMSU::Rejected;
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
	return ok;
    }
    String tmp;
    tmp.hexify((void*)s,len,' ');
    Debug(this,DebugMild,"Received unknown BICC type 0x%02x, cic=%u, length %u: %s",
	type,cic,len,tmp.c_str());
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
