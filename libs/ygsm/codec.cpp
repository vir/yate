/**
 * codec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * GSM Radio Layer 3 messages coder and decoder
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2013 Null Team
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

#include <yategsm.h>

using namespace TelEngine;

struct IEParam;
struct RL3ProtoMessage;


struct IEParam {
    RL3Codec::Type type;
    RL3Codec::XmlType xmlType;
    uint16_t iei;
    const String name;
    bool isOptional;
    uint16_t length; // in bits
    bool lowerBits;
    unsigned int (*decoder)(const RL3Codec*,uint8_t,const IEParam*,const uint8_t*&, unsigned int&, XmlElement*&);
    unsigned int (*encoder)(const RL3Codec*,uint8_t,const IEParam*,XmlElement*,DataBlock&);
    const void* data;
};

struct RL3Message {
    uint16_t value;
    const String name;
    const IEParam* params;
};


static unsigned int decodeParams(const RL3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len,
	XmlElement*& out, const IEParam* param);

static uint8_t getUINT8(const uint8_t*& in, unsigned int& len, const IEParam* param)
{
    if (!(in && len && param))
	return 0;
    if (param->length == 4) {
	if (param->lowerBits)
	    return *in && 0x0f;
	len--;
	return (*in++ >> 4);
    }
    len--;
    return *in++;
}

static void addXMLElement(XmlElement*& dst, XmlElement* what)
{
    if (!what)
	return;
    if (!dst)
	dst = what;
    else
	dst->addChildSafe(what);
}

static unsigned int decodeMsgType(const RL3Codec* codec,  uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out)
{
    //TODO
    return RL3Codec::NoError;
}


static unsigned int encodeMsgType(const RL3Codec* codec, uint8_t proto, const IEParam* param, XmlElement* in,DataBlock& out)
{
    //TODO
    return RL3Codec::NoError;
}

static const RL3Message* findRL3Msg(uint16_t val, const RL3Message* where)
{
    if (!where)
	return 0;
    while (where->name) {
	if (where->value == val)
	    return where;
	where++;
    }
    return 0;
}

static unsigned int decodePD(const RL3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out)
{
    if (!(codec && in && len && param))
	return RL3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodePD(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out);
    XmlElement* payload = 0;
    XmlElement* xml = 0;
    if (codec->flags() & RL3Codec::XmlDumpMsg) {
	String s;
	s.hexify((void*)in,len);
	payload = new XmlElement(YSTRING("message_payload"),s);
    }
    uint8_t val = getUINT8(in,len,param);
    unsigned int status = RL3Codec::NoError;
    const RL3Message* msg = static_cast<const RL3Message*>(param->data);
    msg = findRL3Msg(val,msg);
    if (!msg) {
	xml = new XmlElement(param->name ? param->name : YSTRING("ie"));
	xml->setText(String(val));
    }
    else {
	xml = new XmlElement(msg->name);
	if (msg->params)
	    status = decodeParams(codec,proto,in,len,xml,msg->params);
    }
    addXMLElement(out,xml);
    if (payload)
	xml->addChildSafe(payload);
    return status;
}


static unsigned int encodePD(const RL3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,DataBlock& out)
{
    //TODO
    return RL3Codec::NoError;
}


static const RL3Message s_mmMsgs[] = {
    //TODO
};

static const IEParam s_mmMessage[] = {
    {RL3Codec::V,      RL3Codec::Skip,    0, "SkipIndicator", false, 4, false, 0,             0,             0},
    {RL3Codec::V,      RL3Codec::XmlElem, 0, "MessageType",   false, 8, false, decodeMsgType, encodeMsgType, s_mmMsgs},
    {RL3Codec::NoType, RL3Codec::Skip,    0, "",              0,     0, 0,     0,             0,             0 },
};

static const RL3Message s_protoMsg[] = {
    {RL3Codec::GCC,        "GCC",     0},
    {RL3Codec::BCC,        "BCC",     0},
    {RL3Codec::EPS_SM,     "EPS_SM",  0},
    {RL3Codec::CC,         "CC",      0},
    {RL3Codec::GTTP,       "GTTP",    0},
    {RL3Codec::MM,         "MM",      s_mmMessage},
    {RL3Codec::RRM,        "RRM",     0},
    {RL3Codec::EPS_MM,     "EPS_MM",  0},
    {RL3Codec::GPRS_MM,    "GPRS_MM", 0},
    {RL3Codec::SMS,        "SMS",     0},
    {RL3Codec::GPRS_SM,    "GPRS_SM", 0},
    {RL3Codec::SS,         "SS",      0},
    {RL3Codec::LCS,        "LCS",     0},
    {RL3Codec::Extension,  "EXT",     0},
    {RL3Codec::Test,       "TEST",    0},
    {RL3Codec::Unknown,    "",        0},
};

static const IEParam s_rl3Message[] = {
    {RL3Codec::V,       RL3Codec::XmlElem, 0, "PD", false, 4, true, decodePD, encodePD, s_protoMsg},
    {RL3Codec::NoType,  RL3Codec::Skip,    0,  "",  0,     0, 0,    0,        0,        0},
};



static unsigned int skipParam(const RL3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len,const IEParam* param)
{
    if (!(codec && in && len && param))
	return RL3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"skipParam() param=%s(%p) of type %s [%p]",param->name.c_str(),param,
	   lookup(param->type,RL3Codec::s_typeDict,""),codec->ptr());
    switch (param->type) {
	case RL3Codec::V:
	case RL3Codec::T:
	    if (param->length == 4) {
		if (!param->lowerBits) {
		    len--;
		    in++;
		}
		break;
	    }
	    // intentional fall through
	case RL3Codec::TV:
	    if (len * 8 < param->length)
		return RL3Codec::MsgTooShort;
	    len -= param->length / 8;
	    in += param->length / 8;
	    break;
	case RL3Codec::TLV:
	    if (len < 2)
		return RL3Codec::MsgTooShort;
	    in++;
	    len--;
	    // intentional fall through
	case RL3Codec::LV:
	{
	    if (len < 1)
		return RL3Codec::MsgTooShort;
	    uint8_t l = *in++;
	    len--;
	    if (len < l)
		return RL3Codec::MsgTooShort;
	    in += l;
	    len -= l;
	    break;
	}
	case RL3Codec::TLVE:
	    if (len < 3)
		return RL3Codec::MsgTooShort;
	    in++;
	    len--;
	    // intentional fall through
	case RL3Codec::LVE:
	{
	    if (len < 2)
		return RL3Codec::MsgTooShort;
	    uint16_t l = in[0];
	    l = (l << 8) | in[1];
	    in += 2;
	    len -= 2;
	    if (len < l)
		return RL3Codec::MsgTooShort;
	    in += l;
	    len -= l;
	    break;
	}
	case RL3Codec::NoType:
	    break;
    }
    return RL3Codec::NoError;
}

static unsigned int dumpUnknownIE(const RL3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out)
{
    if (!(codec))
	return RL3Codec::ParserErr;
    if (!(in && len))
	return RL3Codec::NoError;
    DDebug(codec->dbg(),DebugAll,"dumpUnknownIE(in=%p,len=%u) in protocol=%s [%p]",in,len,
	   lookup(proto,RL3Codec::s_protoDict,"Unknown"),codec->ptr());
    uint8_t iei = *in;
    // bit 8 on 1 indicates one octet length IE of type V/T/TV
    unsigned int dumpOctets = len;
    if (iei & 0x80 || len < 2)
	dumpOctets = len;
    else {
	// it's either TLV or TLVE
	// in EPS MM and EPS MM, if bits 7 to 4 are set on 1 => means IEI is TLVE
	if ((proto == RL3Codec::EPS_MM || proto == RL3Codec::EPS_SM) && ((iei & 0x78) == 0x78)) {
	    if (len < 3)
		dumpOctets = len;
	    else {
		uint16_t l = in[1];
		l = (l << 8) | in[2];
		dumpOctets = (len < (l + 3) ? len : l + 3);
	    }
	}
	else
	    dumpOctets = (len < (in[1] + 2) ? len : in[1] + 2);
    }
    if (dumpOctets) {
	XmlElement* xml = new XmlElement("ie");
	addXMLElement(out,xml);
	String dumpStr;
	dumpStr.hexify((void*)in,dumpOctets);
	xml->setText(dumpStr);
	in += dumpOctets;
	len -= dumpOctets;
    }
    return RL3Codec::NoError;
}

static unsigned int dumpParamValue(const RL3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, const IEParam* param,
    XmlElement*& out)
{
    if (!(codec && in && len))
	return RL3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"dumpParamValue(in=%p,len=%u) for %sparam%s%s(%p) [%p]",in,len,
	   (!param ? "unknown " : ""),(param ? "=" : ""), (param ? param->name.c_str() : ""),param,codec->ptr());
    if (param) {
	String dumpStr;
	uint8_t skipOctets = 0;
	switch (param->type) {
	    case RL3Codec::T:
		// there's no value to dump
		break;
	    case RL3Codec::V:
	    {
		uint8_t val = 0;
		if (param->length == 4) {
		    if (!param->lowerBits) {
			val |= *in & 0xf0;
			len--;
			in++;
		    }
		    else
			val |= *in & 0x0f;
		}
		else {
		    val = *in;
		    len--;
		    in++;
		}
		dumpStr.hexify(&val,1);
		break;
	    }
	    case RL3Codec::TV:
	    {
		if (param->length == 8) {
		    uint8_t val = *in & 0x0f;
		    len--;
		    in++;
		    dumpStr.hexify(&val,1);
		}
		else
		    skipOctets = 1;
		break;
	    }
	    case RL3Codec::TLV:
		skipOctets = 2;
		break;
	    case RL3Codec::LV:
		skipOctets = 1;
		break;
	    case RL3Codec::TLVE:
		skipOctets = 3;
		break;
	    case RL3Codec::LVE:
		skipOctets = 2;
		break;
	    case RL3Codec::NoType:
		break;
	}
	if (skipOctets) {
	    const uint8_t* buff = in;
	    unsigned int lbuff = len;
	    if (int status = skipParam(codec,proto,in,len,param))
		return status;
	    if (len + skipOctets <= lbuff)
		dumpStr.hexify((void*)(buff + skipOctets), lbuff - len - skipOctets);
	}
	XmlElement* xml = new XmlElement(param->name);
	addXMLElement(out,xml);
	if (dumpStr)
	    xml->setText(dumpStr);
    }
    else
	return dumpUnknownIE(codec,proto,in,len,out);

    return RL3Codec::NoError;
}

static unsigned int decodeV(const RL3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out,
	const IEParam* param)
{
    if (!(codec && in && len && param))
	return RL3Codec::ParserErr;
    if (len * 8 < param->length)
	return RL3Codec::MsgTooShort;
    DDebug(codec->dbg(),DebugAll,"decodeV(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case RL3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case RL3Codec::XmlElem:
	    if (!(param->decoder || (param->data && param->name)))
		return dumpParamValue(codec,proto,in,len,param,out);
	    if (param->decoder)
		return param->decoder(codec,proto,param,in,len,out);
	    // decode an 1 byte value from a dictionary
	    if (param->data && param->name) {
		if (param->length > 8) {
		    DDebug(codec->dbg(),DebugMild,"decodeV() - decoding for values longer than 1 byte not supported, dumping param=%s(%p) [%p]",
			   param->name.c_str(),param,codec->ptr());
		    return dumpParamValue(codec,proto,in,len,param,out);
		}
		uint8_t val = getUINT8(in,len,param);
		XmlElement* xml = new XmlElement(param->name);
	        addXMLElement(out,xml);
		const TokenDict* dict = static_cast<const TokenDict*>(param->data);
		if (!dict)
		    xml->setText(String(val));
		else
		    xml->setText(lookup(val,dict,String(val)));
		return RL3Codec::NoError;
	    }
	default:
	    return RL3Codec::ParserErr;
    }
    return RL3Codec::NoError;
}


static unsigned int decodeParams(const RL3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out, 
	const IEParam* param)
{
    if (!(codec && in && len > 2 && param))
	return RL3Codec::ParserErr;
    while (param) {
	int status = RL3Codec::NoError;
	switch (param->type) {
	    case RL3Codec::V:
		status = decodeV(codec,proto,in,len,out,param);
		break;
		//TODO
	    case RL3Codec::T:
	    case RL3Codec::TV:
	    case RL3Codec::LV:
	    case RL3Codec::TLV:
	    case RL3Codec::LVE:
	    case RL3Codec::TLVE:
	    case RL3Codec::NoType:
		break;
	}
	if (status)
	    return status;
    }
    return RL3Codec::NoError;
};

const TokenDict RL3Codec::s_typeDict[] = {
    {"T",    RL3Codec::T},
    {"V",    RL3Codec::V},
    {"TV",   RL3Codec::TV},
    {"LV",   RL3Codec::LV},
    {"TLV",  RL3Codec::TLV},
    {"LVE",  RL3Codec::LVE},
    {"TLVE", RL3Codec::TLVE},
    {0, 0},
};

const TokenDict RL3Codec::s_protoDict[] = {
    {"GCC",       RL3Codec::GCC},
    {"BCC",       RL3Codec::BCC},
    {"EPS_SM",    RL3Codec::EPS_SM},
    {"CC",        RL3Codec::CC},
    {"GTTP",      RL3Codec::GTTP},
    {"MM",        RL3Codec::MM},
    {"RRM",       RL3Codec::RRM},
    {"EPS_MM",    RL3Codec::EPS_MM},
    {"GPRS_MM",   RL3Codec::GPRS_MM},
    {"SMS",       RL3Codec::SMS},
    {"GPRS_SM",   RL3Codec::GPRS_SM},
    {"SS",        RL3Codec::SS},
    {"LCS",       RL3Codec::LCS},
    {"Extension", RL3Codec::Extension},
    {"Test",      RL3Codec::Test},
    {"Unknown",   RL3Codec::Unknown},
    {0, 0},
};

RL3Codec::RL3Codec(DebugEnabler* dbg)
    : m_flags(0),
      m_dbg(0),
      m_ptr(0)
{
    DDebug(DebugAll,"Created RL3Codec [%p]",this);
    setCodecDebug(dbg);
}

unsigned int RL3Codec::decode(const uint8_t* in, unsigned int len, XmlElement*& out)
{
    if (!in || len < 2)
	return MsgTooShort;
    const uint8_t* buff = in;
    unsigned int l = len;
    return decodeParams(this,RL3Codec::Unknown,buff,l,out,s_rl3Message);
}

unsigned int RL3Codec::encode(XmlElement* in, DataBlock& out)
{
    //TODO
    return NoError;
}

void RL3Codec::setCodecDebug(DebugEnabler* enabler, void* ptr)
{
    m_dbg = enabler ? enabler : m_dbg;
    m_ptr = ptr ? ptr : (void*)this;
}