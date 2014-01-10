/**
 * codec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * GSM Radio Layer 3 messages coder and decoder
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

#include <yategsm.h>

using namespace TelEngine;

struct IEParam;
struct IEType;
struct RL3ProtoMessage;

struct IEType {
    unsigned int (*decoder)(const GSML3Codec*,uint8_t,const IEParam*,const uint8_t*&, unsigned int&, XmlElement*&,
	const NamedList&);
    unsigned int (*encoder)(const GSML3Codec*,uint8_t,const IEParam*,XmlElement*,DataBlock&,const NamedList&);
    const void* data;
};

struct IEParam {
    GSML3Codec::Type type;
    GSML3Codec::XmlType xmlType;
    uint16_t iei;
    const String name;
    bool isOptional;
    uint16_t length; // in bits
    bool lowerBits;
    const IEType& ieType;
};

struct RL3Message {
    uint16_t value;
    const String name;
    const IEParam* params;
    const IEParam* toMSParams;
};


static const String s_pduCodec = "codecTag";
static const String s_epsSequenceNumber = "SequenceNumber";
static const String s_encAttr = "enc";
static const String s_typeAttr = "type";
static const char s_digits[] = "0123456789";

#define GET_DIGIT(val,str,err,odd) \
    if ((val > 9 && val != 0x0f) || (!odd && val == 0x0f) || (odd && val != 0x0f)) \
	return err; \
    else if (val != 0x0f) \
	str << s_digits[val];

#define SET_DIGIT(c,b,idx,highOctet,err) \
    { \
	if (!(('0' <= c) && (c <= '9'))) \
	    return err; \
	*(b + idx) |= (highOctet ? ((c - '0') << 4) : (c - '0')); \
    }

#define CONDITIONAL_ERROR(param,x,y) (param ? (param->isOptional ? GSML3Codec::x : GSML3Codec::y) : GSML3Codec::y)


static unsigned int decodeParams(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len,
	XmlElement*& out, const IEParam* param, const NamedList& params = NamedList::empty());

static unsigned int encodeParams(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out,
	const IEParam* param, const NamedList& params);

static unsigned int  decodeSecHeader(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params);

static unsigned int encodeSecHeader(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params);

static unsigned int decodeRL3Msg(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params);

static unsigned int encodeRL3Msg(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params);


static bool getBCDDigits(const uint8_t*& in, unsigned int& len, String& digits)
{
    if (!(in && len))
	return true;
    static const char s_bcdDigits[] = "0123456789*#ABC";
    while (len) {
	digits += s_bcdDigits[(*in & 0x0f)];
	uint8_t odd = (*in >> 4);
	if ((odd & 0x0f) != 0x0f)
	    digits += s_bcdDigits[odd];
	else if (len > 1)
	    return false;
	in++;
	len--;
    }
    XDebug(DebugAll,"Decoded BCD digits=%s",digits.c_str());
    return true;
}

static inline uint8_t getUINT8(const uint8_t*& in, unsigned int& len, const IEParam* param)
{
    if (!(in && len && param))
	return 0;
    if (param->length == 4) {
	if (param->lowerBits)
	    return *in & 0x0f;
	len--;
	return (*in++ >> 4);
    }
    if (param->length == 8 && param->type == GSML3Codec::TV) {
	len--;
	return (*in++ & 0x0f);
    }
    len--;
    return *in++;
}

static inline void setUINT8(uint8_t val, DataBlock& where, const IEParam* param)
{
    if (!param)
	return;
    if (param->length == 4 && !param->lowerBits)
	*(((uint8_t*) where.data()) + where.length() - 1) |= (uint8_t)(val << 4);
    else
	where.append(&val,1);
}

static inline void addXMLElement(XmlElement*& dst, XmlElement* what)
{
    if (!what)
	return;
    if (!dst)
	dst = what;
    else
	dst->addChildSafe(what);
}

static inline void advanceBuffer(unsigned int bytes, const uint8_t*& in, unsigned int& len)
{
    if (!(in && len))
	return;
    bytes = (bytes > len ? len : bytes);
    in += bytes;
    len -= bytes;
}

static inline uint16_t getUINT16(const uint8_t* in, unsigned int len)
{
    if (!(in && len >= 2))
	return 0;
    uint16_t l = in[0];
    l = (l << 8) | in[1];
    return l;
}

static inline uint16_t getUINT16(const uint8_t*& in, unsigned int& len, bool advance)
{
    uint16_t l = getUINT16(in,len);
    if (advance)
	advanceBuffer(2,in,len);
    return l;
}

static inline bool setUINT16(uint16_t val, uint8_t* in, unsigned int len)
{
    if (!(in && len >= 2))
	return false;
    *in++ = val >> 8;
    *in++ = val;
    return true;
}

static inline void setUINT16(uint16_t val, uint8_t*& in, unsigned int& len, bool advance)
{
    if (!setUINT16(val,in,len))
	return;
    if (advance)
	len -= 2;
    else
	in -= 2;
}

static inline void getFlags(unsigned int bitmask, const TokenDict* dict, String& out)
{
    if (!dict)
	return;
    for (; dict->token; dict++)
	if (dict->value & bitmask)
	    out.append(dict->token,",");
}


static inline const RL3Message* findRL3Msg(uint16_t val, const RL3Message* where)
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

static inline const RL3Message* findRL3Msg(const char* in, const RL3Message* where)
{
    if (!(where && in))
	return 0;
    while (where->name) {
	if (where->name == in)
	    return where;
	where++;
    }
    return 0;
}

static inline const IEParam* getParams(const GSML3Codec* codec, const RL3Message* msg, bool encode = false)
{
    if (!(codec && msg))
	return 0;
    if (!msg->toMSParams)
	return msg->params;
    switch (codec->flags() & GSML3Codec::MSCoder) {
	case 0:
	    // we are the network
	    if (encode)
		return msg->toMSParams;
	    return msg->params;
	case 1:
	    // we have the role of a mobile station
	    if (encode)
		return msg->params;
	    return msg->toMSParams;
	default:
	    return 0;
    }
    return 0;
}

static inline unsigned int getMCCMNC(const uint8_t*& in, unsigned int& len, XmlElement* xml, bool advance = true)
{
    if (len < 3 || !xml)
	return GSML3Codec::ParserErr;
    String out;
    // get MCC
    GET_DIGIT((in[0] & 0x0f),out,GSML3Codec::ParserErr,false);
    GET_DIGIT(((in[0] >> 4) & 0x0f),out,GSML3Codec::ParserErr,false);
    GET_DIGIT((in[1] & 0x0f),out,GSML3Codec::ParserErr,false);
    // get MNC
    GET_DIGIT((in[2] & 0x0f),out,GSML3Codec::ParserErr,false);
    GET_DIGIT(((in[2] >> 4) & 0x0f),out,GSML3Codec::ParserErr,false);
    GET_DIGIT(((in[1] >> 4) & 0x0f),out,GSML3Codec::ParserErr,true);
    xml->addChildSafe(new XmlElement("PLMNidentity",out));
    if (advance)
	advanceBuffer(3,in,len);
    return GSML3Codec::NoError;
}

static inline unsigned int setMCCMNC(XmlElement* in, uint8_t*& out, unsigned int& len, bool advance = true)
{
    if (!(in && out && len >= 3))
	return GSML3Codec::ParserErr;
    XmlElement* xml = in->findFirstChild(&YSTRING("PLMNidentity"));
    if (!(xml && (xml->getText().length() == 5 || xml->getText().length() == 6)))
	return GSML3Codec::ParserErr;
    const String& text = xml->getText();
    // MCC
    SET_DIGIT(text[0],out,0,false,GSML3Codec::ParserErr)
    SET_DIGIT(text[1],out,0,true,GSML3Codec::ParserErr)
    SET_DIGIT(text[2],out,1,false,GSML3Codec::ParserErr)
    // MNC
    SET_DIGIT(text[3],out,2,false,GSML3Codec::ParserErr)
    SET_DIGIT(text[4],out,2,true,GSML3Codec::ParserErr)
    if (text.length() == 6)
	SET_DIGIT(text[5],out,1,true,GSML3Codec::ParserErr)
    else
	*(out + 1) |= 0xf0;
    if (advance) {
	out += 3;
	len -= 3;
    }
    return GSML3Codec::NoError;
}

static unsigned int decodeInt(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param && out))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeInt(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    unsigned int val = 0;
    switch (param->length) {
	case 4:
	case 8:
	    val = getUINT8(in,len,param);
	    break;
	case 16:
	    val = getUINT16(in,len,true);
	    break;
	case 32:
	    Debug(DebugStub,"Please implement decoding  of  UINT32");
	    break;
	default:
	    Debug(codec->dbg(),DebugNote,"Decoding of integer on %u bits failed [%p]",param->length,codec->ptr());
	    return GSML3Codec::ParserErr;
    }
    XmlElement* xml = new XmlElement(param->name,String(val));
    addXMLElement(out,xml);
    return GSML3Codec::NoError;
}


static unsigned int encodeInt(const GSML3Codec* codec, uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && param && in))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeInt(param=%s(%p),xml=%s(%p)) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    const String* valStr = in->childText(param->name);
    const unsigned int* defVal = static_cast<const unsigned int*>(param->ieType.data);
    unsigned int val = (defVal ? *defVal : 0);
    if (TelEngine::null(valStr) && !defVal)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    if (valStr)
	val = valStr->toInteger(val);
     switch (param->length) {
	case 4:
	    if (val > 0x0f)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    // intentional fall through
	case 8:
	    if (val > 0xff)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    setUINT8(val,out,param);
	    break;
	case 16:
	{
	    if (val > 0xffff)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    uint8_t l[2];
	    setUINT16(val,l,2);
	    out.append(l,2);
	    break;
	}
	case 32:
	    Debug(DebugStub,"Please implement encoding  of  UINT32");
	    break;
	default:
	    Debug(codec->dbg(),DebugNote,"encoding of integer on %u bits failed [%p]",param->length,codec->ptr());
	    return GSML3Codec::ParserErr;
    }

    return GSML3Codec::NoError;
}

// reference ETSI TS 124 007 V11.0.0, section11.2.3.2 Message type octet
static const String s_NSD = "NSD";

static unsigned int decodeMsgType(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param && out))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeMsgType(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    uint8_t val = getUINT8(in,len,param);
    switch (proto) {
	case GSML3Codec::GCC:
	case GSML3Codec::BCC:
	case GSML3Codec::LCS:
	    if (val & 0x80)
		return GSML3Codec::UnknownMsgType;
	    // intentional fall through
	case GSML3Codec::MM:
	case GSML3Codec::CC:
	case GSML3Codec::SS:
	{
	    uint8_t nsd = (val >> 6);
	    out->addChildSafe(new XmlElement(s_NSD,String(nsd)));
	    val &= 0x3f;
	    break;
	}
	default:
	    break;
    }
    const RL3Message* msg = static_cast<const RL3Message*>(param->ieType.data);
    msg = findRL3Msg(val,msg);
    if (!msg)
	return GSML3Codec::UnknownMsgType;
    XmlElement* xml = new XmlElement(param->name);
    xml->setAttribute(s_typeAttr,msg->name);
    addXMLElement(out,xml);
    if (const IEParam* msgParams = getParams(codec,msg))
	return decodeParams(codec,proto,in,len,xml,msgParams,params);
    else {
	String str;
	str.hexify((void*)in,len);
	xml->addText(str);
	advanceBuffer(len,in,len);
    }
    return GSML3Codec::NoError;
}


static unsigned int encodeMsgType(const GSML3Codec* codec, uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && param && in))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeMsgType(param=%s(%p),xml=%s(%p)) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());

    uint8_t val = 0;
    switch (proto) {
	case GSML3Codec::GCC:
	case GSML3Codec::BCC:
	case GSML3Codec::LCS:
	case GSML3Codec::MM:
	case GSML3Codec::CC:
	case GSML3Codec::SS:
	{
	    const String* nsd = in->childText(s_NSD);
	    if (!TelEngine::null(nsd)) {
		uint8_t sd = nsd->toInteger();
		if ((proto == GSML3Codec::GCC || proto == GSML3Codec::BCC || proto == GSML3Codec::LCS) && sd > 1)
		    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
		val |= (sd << 6);
	    }
	    break;
	}
	default:
	    break;
    }
    const RL3Message* msg = static_cast<const RL3Message*>(param->ieType.data);
    in = in->findFirstChild(&param->name);
    if (!in)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    msg = findRL3Msg(in->attribute(s_typeAttr),msg);
    if (!msg) {
	DataBlock d;
	if (!d.unHexify(in->getText())) {
	    Debug(codec->dbg(),DebugWarn,"Failed to unhexify message payload in xml=%s(%p) [%p]",in->tag(),in, codec->ptr());
	    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	}
	out.append(d);
	return GSML3Codec::NoError;
    }
    val |= (msg->value & 0x3f);
    setUINT8(val,out,param);
    if (const IEParam* msgParams = getParams(codec,msg,true))
	return encodeParams(codec,proto,in,out,msgParams,params);
    else {
	DataBlock d;
	if (d.unHexify(in->getText()))
	    out.append(d);
    }
    return GSML3Codec::NoError;
}

// reference ETSI TS 124 007 V11.0.0, section 11.2.3.1.1 Protocol discriminator
static unsigned int decodePD(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodePD(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    XmlElement* payload = 0;
    if (codec->flags() & GSML3Codec::XmlDumpMsg) {
	String s;
	s.hexify((void*)in,len);
	payload = new XmlElement(YSTRING("message_payload"),s);
    }
    uint8_t val = getUINT8(in,len,param);
    unsigned int status = GSML3Codec::NoError;
    const RL3Message* msg = static_cast<const RL3Message*>(param->ieType.data);
    msg = findRL3Msg(val,msg);
    if (!msg) {
	Debug(codec->dbg(),DebugWarn,"Failed to decode Protocol Discriminator %s [%p]",
	    lookup(val,GSML3Codec::s_protoDict,String(val)),codec->ptr());
	return GSML3Codec::UnknownProto;
    }
    XmlElement* xml = new XmlElement(msg->name);
    if (const IEParam* msgParams = getParams(codec,msg))
	status = decodeParams(codec,msg->value,in,len,xml,msgParams,params);
    addXMLElement(out,xml);
    if (payload)
	xml->addChildSafe(payload);
    return status;
}


static unsigned int encodePD(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && param && in))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodePD(param=%s(%p),xml=%s(%p)) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    const RL3Message* msg = static_cast<const RL3Message*>(param->ieType.data);
    msg = findRL3Msg(in->tag(),msg);
    if (!msg) {
	Debug(codec->dbg(),DebugWarn,"Failed to encode Protocol Discriminator %s [%p]",in->tag(),codec->ptr());
	return GSML3Codec::UnknownProto;
    }
    setUINT8(msg->value,out,param);
    String str;
    str.hexify(out.data(),out.length());
    if (const IEParam* msgParams = getParams(codec,msg,true))
	return encodeParams(codec,msg->value,in,out,msgParams,params);
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.21 NAS key set identifier
static const String s_TSC = "TSC";
static const String s_NASKeySetId = "NASKeySetId";
static const String s_NASKeyMapCtxt = "mapped-security-context-for-KSI_SGSN";
static const String s_NASKeyNativCtxt = "native-security-context-for-KSI_ASME";

static unsigned int decodeNASKeyId(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeNASKeyId(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    uint8_t val = getUINT8(in,len,param);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);
    if (val & 0x08)
	xml->addChildSafe(new XmlElement(s_TSC,s_NASKeyMapCtxt));
    else
	xml->addChildSafe(new XmlElement(s_TSC,s_NASKeyNativCtxt));
    xml->addChildSafe(new XmlElement(s_NASKeySetId,String((val & 0x07))));
    return GSML3Codec::NoError;
}

static unsigned int encodeNASKeyId(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeNASKeyId(param=%s(%p),in=%s(%p)) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    XmlElement* xml = in->findFirstChild(&param->name);
    if (!xml)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    uint8_t val = 0;
    XmlElement* child = xml->findFirstChild(&s_TSC);
    if (!child)
	Debug(codec->dbg(),DebugMild,"Missing '%s' element for encoding %s, assuming default [%p]",s_TSC.c_str(),
	    param->name.c_str(),codec->ptr());
    if (child && (child->getText() == s_NASKeyMapCtxt || child->getText().toBoolean() || child->getText() == "1"))
	val |= 0x080;

    child = xml->findFirstChild(&s_NASKeySetId);
    if (!child)
	Debug(codec->dbg(),DebugMild,"Missing '%s' element for encoding %s, assuming default [%p]",s_NASKeySetId.c_str(),
	    param->name.c_str(),codec->ptr());
    else
	val |= (child->getText().toInteger() & 0x07);

    setUINT8(val,out,param);
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.12 EPS mobile identity
static const TokenDict s_epsMobileIdentType[] = {
    {"IMSI", 1},
    {"IMEI", 3},
    {"GUTI", 6},
    {"",     0},
};

static unsigned int decodeEPSMobileIdent(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeEPSMobileIdent(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    uint8_t type = in[0] & 0x07;
    switch (type) {
	case 1:
	case 3:
	{
	    // IMSI / IMEI
	    XmlElement* child = new XmlElement(lookup(type,s_epsMobileIdentType,(type == 1 ? "IMSI" : "IMEI")));
	    xml->addChildSafe(child);

	    String digits;
	    digits.clear();
	    bool odd = (in[0] & 0x08);
	    GSML3Codec::Status err = CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    GET_DIGIT((in[0] >> 4),digits,err,(len == 1));
	    unsigned int index = 1;
	    while (index < len) {
		GET_DIGIT((in[index] & 0x0f),digits,err,false);
		GET_DIGIT((in[index] >> 4),digits,err,(index == len - 1 ? !odd : false));
		index++;
	    }
	    advanceBuffer(index,in,len);

	    child->addText(digits);
	    break;
	}
	case 6:
	{
	    // GUTI
	    if (len < 11)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    advanceBuffer(1,in,len);
	    XmlElement* child = new XmlElement("GUTI");
	    xml->addChildSafe(child);
	    // get MCC_MNC
	    if (getMCCMNC(in,len,child))
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    // get MME Group ID (16 bits)
	    uint16_t groupID = getUINT16(in,len,true);
	    child->addChildSafe(new XmlElement("MMEGroupID",String(groupID)));
	    // get MME Code (8 bits)
	    child->addChildSafe(new XmlElement("MMECode",String(in[0])));
	    advanceBuffer(1,in,len);
	    // get M-TMSI (32 bits)
	    String str = "";
	    str.hexify((void*)in,4);
	    child->addChildSafe(new XmlElement("M_TMSI",str));
	    advanceBuffer(4,in,len);
	    break;
	}
	default:
	    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    }
    if (len) {
	String str;
	str.hexify((void*)in,len);
	xml->addChildSafe(new XmlElement("extraneous_data",str));
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeEPSMobileIdent(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeEPSMobileIdent(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    XmlElement* xml = in->findFirstChild(&param->name);
    if (!(xml && (xml = xml->findFirstChild())))
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    uint8_t type = lookup(xml->getTag(),s_epsMobileIdentType,0xff);
    switch (type) {
	case 1:
	case 3:
	    break;
	case 6:
	{
	    // GUTI
	    DataBlock d(0,7);
	    uint8_t* buf = (uint8_t*)d.data();
	    *buf++ = 0xf6;
	    unsigned int len = 6;
	    if (setMCCMNC(xml,buf,len))
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    // MMEGroupID
	    XmlElement* child = xml->findFirstChild(&YSTRING("MMEGroupID"));
	    unsigned int val = -1;
	    if (!(child && (val = child->getText().toInteger(val) > 0xffff)))
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    setUINT16(val,buf,len,true);
	    // MME Code
	    child = xml->findFirstChild(&YSTRING("MMECode"));
	    if (!(child && (val = child->getText().toInteger(-1) > 0xff)))
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    *buf++ = (uint8_t) val;
	    out.append(d);
	    // M-TMSI
	    d.clear();
	    child = xml->findFirstChild(&YSTRING("M_TMSI"));
	    if (!(child && d.unHexify(child->getText()) && d.length() == 4))
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    out.append(d);
	    break;
	}
	default:
	    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    }
    return GSML3Codec::NoError;
}

static const TokenDict s_UENetworkCapabMandatory[] =
{
    {"EIA7",     0x0001},
    {"EIA6",     0x0002},
    {"EIA5",     0x0004},
    {"EIA4",     0x0008},
    {"128-EIA3", 0x0010},
    {"128-EIA2", 0x0020},
    {"128-EIA1", 0x0040},
    {"EIA0",     0x0080},
    {"EEA7",     0x0100},
    {"EEA6",     0x0200},
    {"EEA5",     0x0400},
    {"EEIA4",    0x0800},
    {"128-EEA3", 0x1000},
    {"128-EEA2", 0x2000},
    {"128-EEA1", 0x4000},
    {"EEA0",     0x8000},
    {"", 0}
};

static const TokenDict s_UENetworkCapabOptional[] =
{
    {"UEA7",      0x000001},
    {"UEA6",      0x000002},
    {"UEA5",      0x000004},
    {"UEA4",      0x000008},
    {"UEA3",      0x000010},
    {"UEA2",      0x000020},
    {"UEA1",      0x000040},
    {"UEA0",      0x000080},
    {"UIA7",      0x000100},
    {"UIA6",      0x000200},
    {"UIA5",      0x000400},
    {"UIA4",      0x000800},
    {"UIA3",      0x001000},
    {"UIA2",      0x002000},
    {"UIA1",      0x004000},
    {"UCS2",      0x008000},
    {"NF",        0x010000},
    {"1xSRVCC",   0x020000},
    {"LCS",       0x040000},
    {"LPP",       0x080000},
    {"ACC-CSFB",  0x100000},
    {"H.245-ASH", 0x200000},
    {"", 0}
};

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.34 UE network capability
static unsigned int decodeUENetworkCapab(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeUENetworkCapab(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    if (len < 2)
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    uint16_t mandBytes = getUINT16(in,len,true);
    String flags;
    getFlags(mandBytes,s_UENetworkCapabMandatory,flags);
    if (len) {
	// optional bytes are present (only 3 defined). If length is longer, those are spare octets and will be ignored
	unsigned int bitmask = 0;
	for (unsigned int i = 0; i < (len < 3 ? len : 3); i++) {
	    bitmask |= (in[i] << 8 * i);
	}
	getFlags(bitmask,s_UENetworkCapabOptional,flags);
    }
    xml->addText(flags);
    return GSML3Codec::NoError;
}

static unsigned int encodeUENetworkCapab(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    //TODO
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.32 Tracking area identity
static unsigned int decodeTAI(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeTAI(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    if (len < 5)
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    // get MCC MNC
    if (getMCCMNC(in,len,xml))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    // get TAC
    String str;
    str.hexify((void*)in,len);
    xml->addChildSafe(new XmlElement("TAC",str));
    advanceBuffer(len,in,len);
    return GSML3Codec::NoError;
}

static unsigned int encodeTAI(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    //TODO
    return GSML3Codec::NoError;
}

static const TokenDict s_splitPgCycle[] = {
    {"704",     0},
    {"71",     65},
    {"72",     66},
    {"74",     67},
    {"75",     68},
    {"77",     69},
    {"79",     70},
    {"80",     71},
    {"83",     72},
    {"86",     73},
    {"88",     74},
    {"90",     75},
    {"92",     76},
    {"96",     77},
    {"101",    78},
    {"103",    79},
    {"107",    80},
    {"112",    81},
    {"116",    82},
    {"118",    83},
    {"128",    84},
    {"141",    85},
    {"144",    86},
    {"150",    87},
    {"160",    88},
    {"171",    89},
    {"176",    90},
    {"192",    91},
    {"214",    92},
    {"224",    93},
    {"235",    94},
    {"256",    95},
    {"288",    96},
    {"320",    97},
    {"352",    98},
    {"", 0}
};

static const TokenDict s_nonDRXTimer[] = {
    {"no-non-DRX-mode" ,        0},
    {"max-1-sec-non-DRX mode",  1},
    {"max-2-sec-non-DRX-mode",  2},
    {"max-4-sec-non-DRX-mode",  3},
    {"max-8-sec-non-DRX-mode",  4},
    {"max-16-sec-non-DRX-mode", 5},
    {"max-32-sec-non-DRX-mode", 6},
    {"max-64-sec-non-DRX-mode", 7},
    {"", 0}
};

static const TokenDict s_drxCycleLength[] = {
    {"not-specified-by-the-MS", 0},
    {"coefficient-6-and-T",     6},
    {"coefficient-7-and-T",     7},
    {"coefficient-8-and-T",     8},
    {"coefficient-9-and-T",     9},
    {"", 0}
};

// reference: ETSI TS 124 008 V11.8.0, 10.5.5.6 DRX parameter
static unsigned int decodeDRX(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeDRX(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    if (len < 2)
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    uint8_t splitCode = *in;
    String splitValue = "1";
    if (splitCode && splitCode < 65)
	splitValue = splitCode;
    else
	splitValue = lookup(splitCode,s_splitPgCycle,splitValue);
    xml->addChildSafe(new XmlElement("SplitPGCycleCode",splitValue));
    xml->addChildSafe(new XmlElement("NonDRXTimer",lookup((in[1] & 0x03),s_nonDRXTimer)));
    xml->addChildSafe(new XmlElement("SplitOnCCCH",String::boolText((in[1] & 0x04))));
    xml->addChildSafe(new XmlElement("CNSpecificDRXCycleLength",lookup((in[1] & 0xf0),
				s_drxCycleLength,s_drxCycleLength[0].token)));
    advanceBuffer(2,in,len);
    return GSML3Codec::NoError;
}

static unsigned int encodeDRX(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    //TODO
    return GSML3Codec::NoError;
}

static const TokenDict s_voiceDomPref[] = {
    {"CS-voice-only",          0},
    {"IMS-PS-voice only",      1},
    {"CS-voice-preferred",     2},
    {"IMS-PS-voice-preferred", 3},
    {"", 0}
};

// reference: ETSI TS 124 008 V11.8.0, section 10.5.5.28 Voice domain preference and UE's usage setting
static unsigned int decodeVoicePref(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeVoicePref(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    if (*in & 0x04)
	xml->addChildSafe(new XmlElement("UEUsageSetting","data-centric"));
    else
	xml->addChildSafe(new XmlElement("UEUsageSetting","voice-centric"));
    uint8_t vd = (*in & 0x03);
    xml->addChildSafe(new XmlElement("VoiceDomainPreference",lookup(vd,s_voiceDomPref,String(vd))));
    return GSML3Codec::NoError;
}

static unsigned int encodeVoicePref(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    //TODO
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 008 V11.6.0, section 10.5.3.5 Location updating type
static const String s_mmFORFlag = "FOR";
static const String s_mmLUT = "LUT";

static const TokenDict s_mmLUTypes[] = {
    {"normal-location-updating", 0},
    {"periodic-updating",        1},
    {"IMSI-attach",              2},
    {"reserved",                 3},
    {"", 0},
};

static unsigned int decodeLocUpdType(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeLocUpdType(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    uint8_t val = getUINT8(in,len,param);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);
    xml->addChildSafe(new XmlElement(s_mmFORFlag,String::boolText(val & 0x08)));
    xml->addChildSafe(new XmlElement(s_mmLUT,lookup(val & 0x03,s_mmLUTypes,"normal-location-updating")));
    return GSML3Codec::NoError;
}

static unsigned int encodeLocUpdType(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    // TODO
    Debug(DebugStub,"encodeLocUpdType() not implemented");
    return GSML3Codec::NoError;
}

static const TokenDict s_ciphKeySN[] = {
    {"0", 0},
    {"1", 1},
    {"2", 2},
    {"3", 3},
    {"4", 4},
    {"5", 5},
    {"6", 6},
    {"no-key/reserved", 7},
    {"", 0},
};

// reference: ETSI TS 124 008 V11.6.0, section 10.5.1.3 Location area identification
const String& s_LAC = "LAC";

static unsigned int decodeLAI(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeLAI(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    if (len * 8 < param->length)
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    // get MCC_MNC
    if (getMCCMNC(in,len,xml))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    // get LAC(16 bits)
    String lac;
    lac.hexify((void*)in,2);
    advanceBuffer(2,in,len);
    xml->addChildSafe(new XmlElement(s_LAC,lac));
    return GSML3Codec::NoError;
}

static unsigned int encodeLAI(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeLAI(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    XmlElement* xml = in->findFirstChild(&param->name);
    if (!xml && !param->isOptional)
	return GSML3Codec::MissingMandatoryIE;
    DataBlock d(0,3);
    uint8_t* buf = (uint8_t*)d.data();
    unsigned int len = 5;
    if (setMCCMNC(xml,buf,len))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    out.append(d);
    d.clear();
    const String* lac = xml->childText(s_LAC);
    if (TelEngine::null(lac) || !d.unHexify(*lac))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    out.append(d);
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 008 V11.6.0, section 10.5.1.4 Mobile identity
static const TokenDict s_mobileIdentType[] = {
    {"no-identity",    0},
    {"IMSI",           1},
    {"IMEI",           2},
    {"IMEISV",         3},
    {"TMSI",           4},
    {"TMGI",           5},
    {"", 0},
};

static unsigned int decodeMobileIdent(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeMobileIdent(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    uint8_t type = in[0] & 0x07;
    XmlElement* child = new XmlElement(lookup(type,s_mobileIdentType,String(type)));
    xml->addChildSafe(child);
    switch (type) {
	case 0:
	case 1:
	case 2:
	case 3:
	{
	    String digits;
	    bool odd = (in[0] & 0x08);
	    GSML3Codec::Status err = CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    GET_DIGIT((in[0] >> 4),digits,err,(len == 1));
	    unsigned int index = 1;
	    while (index < len) {
		GET_DIGIT((in[index] & 0x0f),digits,err,false);
		GET_DIGIT((in[index] >> 4),digits,err,(index == len - 1 ? !odd : false));
		index++;
	    }
	    advanceBuffer(index,in,len);

	    child->addText(digits);
	    break;
	}
	case 4:
	{
	    advanceBuffer(1,in,len);
	    String str = "";
	    str.hexify((void*)in,len);
	    child->addText(str);
	    advanceBuffer(len,in,len);
	    break;
	}
	case 5:
	{
	    // TMGI
	    if (len < 4)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    bool mncMccInd = (in[0] & 0x10);
	    bool sessIdInd = (in[0] & 0x20);
	    advanceBuffer(1,in,len);

	     // get MBMS Service ID (24 bits) - TODO dump as octet string for now
	    String str = "";
	    str.hexify((void*)in,3);
	    child->addChildSafe(new XmlElement("MBMSServiceID",str));
	    advanceBuffer(3,in,len);

	    if (mncMccInd && len < 3)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	     // get MCC_MNC
	    if (getMCCMNC(in,len,child))
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    if (sessIdInd && len < 1)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    child->addChildSafe(new XmlElement("MBMSSessionIdentity",*in));
	    advanceBuffer(1,in,len);
	    break;
	}
	default:
	    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeMobileIdent(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeMobileIdent(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    XmlElement* xml = in->findFirstChild(&param->name);
    if (!(xml && (xml = xml->findFirstChild())))
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    uint8_t type = lookup(xml->getTag(),s_mobileIdentType,0xff);
    switch (type) {
	case 4:
	{
	    // TMSI
	    type |= 0xf0;
	    out.append(&type,1);
	    DataBlock d;
	    if (!d.unHexify(xml->getText())) {
		DDebug(codec->dbg(),DebugWarn,"Failed to unhexify TMSI while encoding mobile identity [%p]",codec->ptr());
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    }
	    out.append(d);
	    break;
	}
	case 0:
	case 1:
	case 2:
	case 3:
	{
	    const String& digits = xml->getText();
	    if (!digits)
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    DataBlock d(0, digits.length() / 2 + 1);
	    uint8_t* buf = (uint8_t*)d.data();
	    *buf |= (type & 0x07);
	    bool odd = (digits.length() % 2);
	    if (odd)
		*buf |= 0x80;
	    GSML3Codec::Status err = CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	    const char* str = digits.c_str();
	    SET_DIGIT(*str,buf,0,true,err)
	    str++;
	    buf++;
	    bool high = false;
	    while (char c = *str) {
		SET_DIGIT(c,buf,0,high,err);
		if (high)
		    buf++;
		high = !high;
		str++;
	    }
	    if (!odd)
		*buf |= 0xf0;
	    out.append(d);
	    break;
	}
	case 5:
	    Debug(DebugStub,"Please implement encoding of TMGI for mobile identity [%p]",codec->ptr());
	default:
	    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    }
    return GSML3Codec::NoError;
}

static const TokenDict s_msNetworkFeatSupport[] = {
    {"MS-does-not-support-the-extended-periodic-timer-in-this-domain", 0},
    {"MS-supports-the-extended-periodic-timer-in-this-domain",         1},
    {"", 0},
};

// reference: ETSI TS 124 008 V11.6.0, section 10.5.3.4 Identity type
static const TokenDict s_mmIdentType[] = {
    {"IMSI",           1},
    {"IMEI",           2},
    {"IMEISV",         3},
    {"TMSI",           4},
    {"TMGI",           5},
    {"", 0},
};

// reference: ETSI TS 124 008 V11.6.0, section 10.5.5.29 P-TMSI type
static const TokenDict s_pTMSIType[] = {
    {"native-P_TMSI",          0},
    {"mapped-P_TMSI",          1},
    {"", 0},
};

static const TokenDict s_mmRejectCause[] = {
    {"IMSI-unknown-in-HLR",                                 0x02},
    {"illegal-MS",                                          0x03},
    {"IMSI-unknown-in-VLR",                                 0x04},
    {"IMEI-not-accepted",                                   0x05},
    {"illegal-ME",                                          0x06},
    {"PLMN-not-allowed",                                    0x0b},
    {"location-Area-not-allowed",                           0x0c},
    {"roaming-not-allowed-in-this-location-area",           0x0d},
    {"no-Suitable-Cells-In-Location Area",                  0x0f},
    {"network-failure",                                     0x11},
    {"MAC-failure",                                         0x14},
    {"synch-failure",                                       0x15},
    {"congestion",                                          0x16},
    {"GSM-authentication-unacceptable",                     0x17},
    {"not-authorized-for-this-CSG",                         0x19},
    {"service-option-not-supported",                        0x20},
    {"requested-service-option-not-subscribed",             0x21},
    {"service-option-temporarily-out-of-order",             0x22},
    {"call-cannot-be-identified",                           0x26},
    {"retry-upon-entry-into-a-new-cell",                    0x30},
    {"retry-upon-entry-into-a-new-cell",                    0x31},
    {"retry-upon-entry-into-a-new-cell",                    0x32},
    {"retry-upon-entry-into-a-new-cell",                    0x33},
    {"retry-upon-entry-into-a-new-cell",                    0x34},
    {"retry-upon-entry-into-a-new-cell",                    0x35},
    {"retry-upon-entry-into-a-new-cell",                    0x36},
    {"retry-upon-entry-into-a-new-cell",                    0x37},
    {"retry-upon-entry-into-a-new-cell",                    0x38},
    {"retry-upon-entry-into-a-new-cell",                    0x38},
    {"retry-upon-entry-into-a-new-cell",                    0x3a},
    {"retry-upon-entry-into-a-new-cell",                    0x3b},
    {"retry-upon-entry-into-a-new-cell",                    0x3c},
    {"retry-upon-entry-into-a-new-cell",                    0x3d},
    {"retry-upon-entry-into-a-new-cell",                    0x3e},
    {"retry-upon-entry-into-a-new-cell",                    0x3f},
    {"semantically-incorrect-message",                      0x5f},
    {"invalid-mandatory-information",                       0x60},
    {"message-type-non-existent-or-not-implemented",        0x61},
    {"message-type-not-compatible-with-the-protocol-state", 0x62},
    {"information-element-non-existent-or-not-implemented", 0x63},
    {"conditional-IE-error",                                0x64},
    {"message-not-compatible-with-the-protocol-state",      0x65},
    {"protocol-error-unspecified",                          0x6f},
    {"", 0},
};

// reference: ETSI TS 124 008 V11.6.0, section 10.5.3.3 CM service type
static const TokenDict s_mmCMServType[] = {
    {"MO-call-establishment-or-PM-connection-establishment", 0x01},
    {"emergency-call-establishment",                         0x02},
    {"SMS",                                                  0x04},
    {"SS-activation",                                        0x08},
    {"voice-group-call-establishment",                       0x09},
    {"voice-broadcast-call-establishment",                   0x0a},
    {"location-services",                                    0x0b},
    {"", 0}
};

// reference: ETSI TS 124 008 V11.6.0, 10.5.1.11 Priority Level
static const TokenDict s_mmPriorityLevel[] = {
    {"no-priority-applied",   0x00},
    {"call-priority-level-4", 0x01},
    {"call-priority-level-3", 0x02},
    {"call-priority-level-2", 0x03},
    {"call-priority-level-1", 0x04},
    {"call-priority-level-0", 0x05},
    {"call-priority-level-B", 0x06},
    {"call-priority-level-A", 0x07},
    {"", 0}
};

// reference ETSI TS 124 007 V11.0.0, section 11.2.3.1.3 Transaction identifier
static const String s_TIFlag = "TIFlag";

static unsigned int decodeTID(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeTID(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    uint8_t val = getUINT8(in,len,param);
    xml->setAttribute(s_TIFlag,String::boolText(val & 0x08));
    val &= 0x07;
    if (val == 7) {
	if (!len)
	    return GSML3Codec::MsgTooShort;
	val = *in++;
	len--;
	if (!(val & 0x80)) {
	    Debug(codec->dbg(),DebugWarn,"Decoding extended TIDs longer than 1 octet not implemented [%p]",codec->ptr());
	    return GSML3Codec::ParserErr;
	}
	xml->setText(String(val & 0x7f));
    }
    else
	xml->setText(String(val));
    return GSML3Codec::NoError;
}

static unsigned int encodeTID(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeTID(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    XmlElement* xml = in->findFirstChild(&param->name);
    if (!xml)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    const String* str = xml->getAttribute(s_TIFlag);
    if (TelEngine::null(str))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    uint8_t val = 0 | (str->toBoolean() ? 0x08 : 0);
    const String& tiStr =  xml->getText();
    if (TelEngine::null(tiStr))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    unsigned int ti = tiStr.toInteger();
    if (ti > 0x7f) {
	Debug(codec->dbg(),DebugWarn,"Encoding extended TIDs longer than 1 octet not implemented [%p]",codec->ptr());
	return GSML3Codec::ParserErr;
    }
    else if (ti >= 7) {
	val |= 0x07;
	setUINT8(val,out,param);
	val = ti;
	out.append(&val,1);
    }
    else {
	val |= ti;
	setUINT8(val,out,param);
    }
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 008 V11.6.0, 10.5.4.21 Progress indicator
static const String s_progIndCoding = "coding";
static const String s_progIndLocation = "location";
static const String s_progInd = "progress";

static const TokenDict s_progIndCoding_dict[] = {
    {"CCITT",       0x00},
    {"reserved",    0x20},
    {"national",    0x40},
    {"GSM-PLMN",    0x60},
    {"", 0}
};

static const TokenDict s_progIndLocation_dict[] = {
    {"U",    0x00},                  // User
    {"LPN",  0x01},                  // Private network serving the local user
    {"LN",   0x02},                  // Public network serving the local user
    {"RLN",  0x04},                  // Public network serving the remote user
    {"RPN",  0x05},                  // Private network serving the remote user
    {"BI",   0x0a},                  // Network beyond the interworking point
    {0,0}
};

static const TokenDict s_progInd_dict[] = {
    {"call-is-not-end-to-end-PLMN/ISDN",         1},
    {"destination-address-in-non-PLMN/ISDN",     2},
    {"origination-address-in-non-PLMN/ISDN",     3},
    {"call-has-returned-to-the-PLMN/ISDN",       4},
    {"in-band-information-available",            8},
    {"in-band-multimedia-CAT-available",         9},
    {"call-is-end-to-end-PLMN/ISDN",            32},
    {"queueing",                                64},
    {"", 0}
};

static unsigned int decodeProgressInd(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeProgressInd(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    if (len < 2 || !(in[0] & 0x80) || !(in[1] & 0x80))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);
    xml->setAttribute(s_progIndCoding,lookup(in[0] & 0x60,s_progIndCoding_dict,"unknown"));
    xml->setAttribute(s_progIndLocation,lookup(in[0] & 0x0f,s_progIndLocation_dict,"unknown"));
    xml->setText(lookup(in[1] & 0x7f,s_progInd_dict,"unspecified"));
    return  CONDITIONAL_ERROR(param,NoError,ParserErr);
}

static unsigned int encodeProgressInd(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeBCDNumber(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    XmlElement* xml = in->findFirstChild(&param->name);
    if (!xml)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    const String* coding = xml->getAttribute(s_progIndCoding);
    const String* loc = xml->getAttribute(s_progIndLocation);
    const String& prog = xml->getText();
    if (TelEngine::null(coding) || TelEngine::null(loc) || TelEngine::null(prog))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    uint8_t buf[2] = {0x80,0x80};
    buf[0] |= lookup(*coding,s_progIndCoding_dict,0x60) & 0x60;
    buf[0] |= lookup(*loc,s_progIndLocation_dict,0) & 0x0f;
    buf[1] |= lookup(prog,s_progInd_dict,0x7f) & 0x7f;
    out.append(buf,2);
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 008 V11.6.0, section 10.5.4.7 Called Party BCD Number
// reference: ETSI TS 124 008 V11.6.0, section 10.5.4.9 Calling party BCD number
static const String s_numberPlan = "plan";
static const String s_numberNature = "nature";
static const String s_numberScreened = "screened";
static const String s_numberRestrict = "restrict";

static const TokenDict s_dict_numNature[] = {
    { "unknown",             0x00 },
    { "international",       0x10 },
    { "national",            0x20 },
    { "network-specific",    0x30 },
    { "dedicated-access",    0x40 },
    { "reserved",            0x50 },
    { "abbreviated",         0x60 },
    { "extension-reserved",  0x70 },
    { 0, 0 },
};

// Numbering Plan Indicator
static const TokenDict s_dict_numPlan[] = {
    { "unknown",      0 },
    { "isdn",         1 },
    { "data",         3 },
    { "telex",        4 },
    { "national",     8 },
    { "private",      9 },
    { "CTS-reserved", 11},
    { "extension-reserved",      15 },
    { 0, 0 },
};

// Address Presentation
static const TokenDict s_dict_presentation[] = {
    { "allowed",     0 },
    { "restricted",  1 },
    { "unavailable", 2 },
    { "reserved",    3 },
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

static unsigned int decodeBCDNumber(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeBCDNumber(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    xml->setAttribute(s_numberNature,lookup((in[0] & 0x70),s_dict_numNature,"unknown"));
    xml->setAttribute(s_numberPlan,lookup((in[0] & 0x0f),s_dict_numPlan,"unknown"));
    if (!(in[0] & 0x80)) {
	advanceBuffer(1,in,len);
	if (!(len && (in[0] & 0x80)))
	    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
	xml->setAttribute(s_numberScreened,lookup((in[0] & 0x03),s_dict_screening,"unknown"));
	xml->setAttribute(s_numberRestrict,lookup((in[0] & 0x60),s_dict_presentation,"unknown"));
    }
    advanceBuffer(1,in,len);

    String bcdDigits;
    if (!getBCDDigits(in,len,bcdDigits))
	return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
    xml->setText(bcdDigits);
    return GSML3Codec::NoError;
}

static unsigned int encodeBCDNumber(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugStub,"Please implement encodeBCDNumber(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    //TODO
    return  CONDITIONAL_ERROR(param,NoError,ParserErr);
}

// reference: ETSI TS 124 008 V11.6.0, section10.5.4.11 Cause
static unsigned int decodeCause(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugStub,"Please implement decodeCause(param=%s(%p),in=%p,len=%u,out=%p) [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    // TODO
    return GSML3Codec::NoError;
}

static unsigned int encodeCause(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugStub,"Please implement encodeCause(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());
    // TODO
    return  CONDITIONAL_ERROR(param,NoError,ParserErr);
}

// reference: ETSI TS 124 301 V11.8.0, section 9.9.4.14 Request type =>
// section 10.5.6.17 in 3GPP TS 24.008
static const TokenDict s_epsReqType[] = {
    {"initialRequest", 1},
    {"handover",       2},
    {"unused",         3},
    {"emergency",      4},
    {"", 0},
};

// reference: ETSI TS 124 301 V11.8.0, section 9.9.4.10 PDN type
static const TokenDict s_epsPdnType[] = {
    {"IPv4",    1},
    {"IPv6",    2},
    {"IPv4v6",  3},
    {"unused",  4},
    {"", 0},
};

// reference: ETSI TS 124 301 V11.8.0, section 9.9.4.10 PDN type
static const TokenDict s_esmEITFlag[] = {
    {"security-protected-ESM-information-transfer-not-required",    0},
    {"security-protected-ESM-information-transfer-required",        1},
    {"", 0},
};

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.11
static const TokenDict s_epsAttachTypes[] = {
    {"EPS-Attach",               1},
    {"combined-EPS-IMSI-attach", 2},
    {"EPS-emergency-attach",     6},
    {"reserved",                 7},
    {"", 0},
};

// reference: ETSI TS 124 008 V11.8.0, section 10.5.5.4 TMSI status
static const TokenDict s_tmsiStatus[] = {
    {"no-valid-TMSI-available",  0},
    {"valid-TMSI-available",     1},
    {"", 0},
};

// reference: ETSI TS 124 301 V11.8.0,9.9.3.0B Additional update type
static const TokenDict s_additionalUpdateType[] = {
    {"no-additional-information",  0},
    {"SMS-only",     1},
    {"", 0},
};

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.45 GUTI type
static const TokenDict s_epsGUTIType[] = {
    {"native-GUTI",  0},
    {"mapped-GUTI",  1},
    {"", 0},
};


// IE Types
#define MAKE_IE_TYPE(x,decoder,encoder,data) const IEType s_type_##x = {decoder,encoder,data};

MAKE_IE_TYPE(Undef,0,0,0)
MAKE_IE_TYPE(MobileIdent,decodeMobileIdent, encodeMobileIdent,0)
MAKE_IE_TYPE(LAI,decodeLAI,encodeLAI,0)
MAKE_IE_TYPE(MMRejectCause,0,0,s_mmRejectCause)
MAKE_IE_TYPE(LocUpdType,decodeLocUpdType, encodeLocUpdType,0)
MAKE_IE_TYPE(CiphKeySN,0,0,s_ciphKeySN)
MAKE_IE_TYPE(MSNetFeatSupp,0,0,s_msNetworkFeatSupport)
MAKE_IE_TYPE(MMIdentType,0,0,s_mmIdentType)
MAKE_IE_TYPE(PTMSIType,0,0,s_pTMSIType)
MAKE_IE_TYPE(CMServType,0,0,s_mmCMServType)
MAKE_IE_TYPE(PrioLevel,0,0,s_mmPriorityLevel)
MAKE_IE_TYPE(ProgressInd,decodeProgressInd,encodeProgressInd,0)
MAKE_IE_TYPE(BCDNumber,decodeBCDNumber,encodeBCDNumber,0)
MAKE_IE_TYPE(Cause,decodeCause,encodeCause,0)

const int s_skipIndDefVal = 0;
MAKE_IE_TYPE(Int,decodeInt,encodeInt,&s_skipIndDefVal)

MAKE_IE_TYPE(TID,decodeTID,encodeTID,0)
MAKE_IE_TYPE(EpsReqType,0,0,s_epsReqType)
MAKE_IE_TYPE(EpsPdnType,0,0,s_epsPdnType)
MAKE_IE_TYPE(EsmEITFlag,0,0,s_esmEITFlag)
MAKE_IE_TYPE(EpsAttachTypes,0,0,s_epsAttachTypes)
MAKE_IE_TYPE(NASKeySetId,decodeNASKeyId,encodeNASKeyId,0)
MAKE_IE_TYPE(EPSMobileIdent,decodeEPSMobileIdent,encodeEPSMobileIdent,0)
MAKE_IE_TYPE(UENetworkCapab,decodeUENetworkCapab,encodeUENetworkCapab,0)
MAKE_IE_TYPE(RL3Msg,decodeRL3Msg,encodeRL3Msg,0)
MAKE_IE_TYPE(TAI,decodeTAI,encodeTAI,0)
MAKE_IE_TYPE(DRX,decodeDRX,encodeDRX,0)
MAKE_IE_TYPE(TMSIStatus,0,0,s_tmsiStatus)
MAKE_IE_TYPE(AdditionalUpdateType,0,0,s_additionalUpdateType)
MAKE_IE_TYPE(VoicePreference,decodeVoicePref,encodeVoicePref,0)
MAKE_IE_TYPE(GUTIType,0,0,s_epsGUTIType)
MAKE_IE_TYPE(SecurityHeader,decodeSecHeader,encodeSecHeader,0)


#define MAKE_IE_PARAM(type,xml,iei,name,optional,length,lowerBits,ieType) \
    {GSML3Codec::type,GSML3Codec::xml,iei,name,optional,length,lowerBits,ieType}

const IEParam s_ie_EndDef = MAKE_IE_PARAM(NoType, Skip, 0, "", 0, 0, 0, s_type_Undef);


// Mobility management message definitions

// reference: ETSI TS 124 008 V11.6.0, section 9.2.12 IMSI detach indication
static const IEParam s_mmIMSIDetachIndParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "MobileStationClassmark",      false,       8,  true, s_type_Undef),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "MobileIdentity",              false,   9 * 8,  true, s_type_MobileIdent),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.2.13 Location updating Accept
static const IEParam s_mmLocationUpdateAckParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "LAI",                         false,   5 * 8,  true, s_type_LAI),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x17, "MobileIdentity",               true,  10 * 8,  true, s_type_MobileIdent),
    MAKE_IE_PARAM(T,      XmlElem, 0xA1, "FollowOnProceed",              true,       8,  true, s_type_Undef),
    MAKE_IE_PARAM(T,      XmlElem, 0xA2, "CTSPermission",                true,       8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x4A, "EquivalentPLMNs",              true,  47 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x34, "EmergencyNumberList",          true,  50 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x35, "PerMST3212",                   true,   3 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.2.14 Location updating reject
// reference: ETSI TS 124 008 V11.6.0, section 9.2.6 CM Service reject
static const IEParam s_mmLocationUpdateRejParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "RejectCause",    false,       8,  true, s_type_MMRejectCause),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x36, "T3246Value",      true,   3 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.2.15 Location updating request
static const IEParam s_mmLocationUpdateReqParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "LocationUpdatingType",        false,       4,  true, s_type_LocUpdType),
    MAKE_IE_PARAM(V,      XmlElem,    0, "CipheringKeySequenceNumber",  false,       4, false, s_type_CiphKeySN),
    MAKE_IE_PARAM(V,      XmlElem,    0, "LAI",                         false,   5 * 8,  true, s_type_LAI),
    MAKE_IE_PARAM(V,      XmlElem,    0, "MobileStationClassmark",      false,       8,  true, s_type_Undef),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "MobileIdentity",              false,   9 * 8,  true, s_type_MobileIdent),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x33, "MobileStationClassmarkForUMTS",true,   5 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xC0, "AdditionalUpdateParameters",   true,       8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "DeviceProperties",             true,       8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xE0, "MSNetworkFeatureSupport",      true,       8,  true, s_type_MSNetFeatSupp),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.2.10 Identity Request
static const IEParam s_mmIdentityReqParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "IdentityType",        false,       8,  true, s_type_MMIdentType),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.2.11 Identity Response
static const IEParam s_mmIdentityRespParams[] = {
    MAKE_IE_PARAM(LV,     XmlElem,    0, "MobileIdentity",  false,   10 * 8,  true, s_type_MobileIdent),
    MAKE_IE_PARAM(TV,     XmlElem, 0xE0, "P_TMSIType",       true,        8,  true, s_type_PTMSIType),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1B, "RAI",              true,    8 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x19, "P_TMSISignature",  true,    5 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.2.9 CM service request
static const IEParam s_mmCMServiceReqParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "CMServiceType",               false,       4,  true, s_type_CMServType),
    MAKE_IE_PARAM(V,      XmlElem,    0, "CipheringKeySequenceNumber",  false,       4, false, s_type_CiphKeySN),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "MobileStationClassmark",      false,   4 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "MobileIdentity",              false,   9 * 8,  true, s_type_MobileIdent),
    MAKE_IE_PARAM(TV,     XmlElem, 0x80, "Priority",                     true,       8,  true, s_type_PrioLevel),
    MAKE_IE_PARAM(TV,     XmlElem, 0xC0, "AdditionalUpdateParameters",   true,       8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "DeviceProperties",             true,       8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.2.8 Abort
// reference: ETSI TS 124 008 V11.6.0, section 9.2.16 MM Status
static const IEParam s_mmAbortParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "RejectCause",    false,       8,  true, s_type_MMRejectCause),
    s_ie_EndDef,
};

static const RL3Message s_mmMsgs[] = {
    //TODO
    {0x01,    "IMSIDetachIndication",      s_mmIMSIDetachIndParams,        0},
    {0x02,    "LocationUpdatingAccept",    s_mmLocationUpdateAckParams,    0},
    {0x04,    "LocationUpdatingReject",    s_mmLocationUpdateRejParams,    0},
    {0x08,    "LocationUpdatingRequest",   s_mmLocationUpdateReqParams,    0},
    {0x18,    "IdentityRequest",           s_mmIdentityReqParams,          0},
    {0x19,    "IdentityResponse",          s_mmIdentityRespParams,         0},
    {0x1b,    "TMSIReallocationComplete",  0,                              0},
    {0x21,    "CMServiceAccept",           0,                              0},
    {0x22,    "CMServiceReject",           s_mmLocationUpdateRejParams,    0},
    {0x23,    "CMServiceAbort",            0,                              0},
    {0x24,    "CMServiceRequest",          s_mmCMServiceReqParams,         0},
    {0x29,    "Abort",                     s_mmAbortParams,                0},
    {0x31,    "MMStatus",                  s_mmAbortParams,                0},
    {0xff,    "",                          0,                              0},
};


// Call control message definitions

// reference: ETSI TS 124 008 V11.6.0, section 9.3.1.2 Alerting (mobile station to network direction)
static const IEParam s_ccAlertFromMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",               true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",               true,   131 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7F, "SSVersion",              true,     3 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.1.1 Alerting (network to mobile station direction)
static const IEParam s_ccAlertToMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",               true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1E, "ProgressIndicator",      true,     4 * 8,  true, s_type_ProgressInd),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",               true,   131 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, 9.3.3 Call proceeding
static const IEParam s_ccCallProceedParams[] = {
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "RepeatIndicator",        true,         8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x04, "BearerCapability1",      true,    16 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x04, "BearerCapability2",      true,    16 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",               true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1E, "ProgressIndicator",      true,     4 * 8,  true, s_type_ProgressInd),
    MAKE_IE_PARAM(TV,     XmlElem, 0x80, "PriorityGranted",        true,         8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x2F, "NetworkCCCapabilities",  true,     3 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, 9.3.23.2 Setup (mobile originating call establishment)
static const IEParam s_ccSetupFromMSParams[] = {
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "BCRepeatIndicator",      true,         8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x04, "BearerCapability1",     false,    16 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x04, "BearerCapability2",      true,    16 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",               true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x5D, "CallingPartySubAddress", true,    23 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x5E, "CalledPartyBCDNumber",  false,    43 * 8,  true, s_type_BCDNumber),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x6D, "CalledPartySubAddress",  true,    23 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "LLCRepeatIndicator",     true,         8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7C, "LowLayerCompatibility1", true,    18 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7C, "LowLayerCompatibility2", true,    18 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "HLCRepeatIndicator",     true,         8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7D, "HighLayerCompatibility1",true,     5 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7D, "HighLayerCompatibility2",true,     5 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",               true,    35 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7F, "SSVersion",              true,     3 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(T,      XmlElem, 0xA1, "CLIRSuppresion",         true,         8,  true, s_type_Undef),
    MAKE_IE_PARAM(T,      XmlElem, 0xA2, "CLIRInvocation",         true,         8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x15, "CCCapabilities",         true,     4 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1D, "FacilityCCBSAdvRA",      true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1B, "FacilityCCBSRANotEssent",true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x2D, "StreamIdentifier",       true,     3 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x40, "SupportedCodecs",        true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(T,      XmlElem, 0xA3, "Redial",                 true,         8,  true, s_type_Undef),
    s_ie_EndDef,
};


// reference: ETSI TS 124 008 V11.6.0, 9.3.23.1 Setup (mobile terminated call establishment)
static const IEParam s_ccSetupToMSParams[] = {
    // TODO
    s_ie_EndDef,
};


// reference: ETSI TS 124 008 V11.6.0, sectin 9.3.5.2 Connect (mobile station to network direction)
static const IEParam s_ccConnFromMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",               true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x4D, "ConnectedSubAddress",    true,    23 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",               true,   131 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7F, "SSVersion",              true,     3 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x2D, "StreamIdentifier",       true,     3 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.5.1 Connect (network to mobile station direction)
static const IEParam s_ccConnToMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",               true,   255 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1E, "ProgressIndicator",      true,     4 * 8,  true, s_type_ProgressInd),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x4C, "ConnectedNumber",        true,    14 * 8,  true, s_type_BCDNumber),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x4D, "ConnectedSubAddress",    true,    23 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",               true,   131 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.7.2 Disconnect (mobile station to network direction)
static const IEParam s_ccDisconnFromMSParams[] = {
    MAKE_IE_PARAM(LV,     XmlElem,    0, "Cause",         false,    31 * 8, true, s_type_Cause),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",       true,   255 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",       true,   131 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7F, "SSVersion",      true,     3 * 8, true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.7.1 Disconnect (network to mobile station direction)
static const IEParam s_ccDisconnToMSParams[] = {
    MAKE_IE_PARAM(LV,     XmlElem,    0, "Cause",               false,    31 * 8, true, s_type_Cause),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",             true,   255 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1E, "ProgressIndicator",    true,     4 * 8, true, s_type_ProgressInd),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",             true,   131 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7B, "AllowedActions",       true,     3 * 8, true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.18.2 Release (mobile station to network direction)
static const IEParam s_ccRelFromMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x08, "Cause",         true,    32 * 8, true, s_type_Cause),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x08, "SecondCause",   true,    32 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",      true,   255 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",      true,   131 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7F, "SSVersion",     true,     3 * 8, true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.18.1 Release (network to mobile station direction)
static const IEParam s_ccRelToMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x08, "Cause",          true,    32 * 8, true, s_type_Cause),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x08, "SecondCause",    true,    32 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",       true,   255 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",       true,   131 * 8, true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.19.2 Release complete (mobile station to network direction)
static const IEParam s_ccRelComplFromMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x08, "Cause",         true,    32 * 8, true, s_type_Cause),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",      true,   255 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",      true,   131 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7F, "SSVersion",     true,     3 * 8, true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.19.1 Release complete (network to mobile station direction)
static const IEParam s_ccRelComplToMSParams[] = {
    MAKE_IE_PARAM(TLV,    XmlElem, 0x08, "Cause",          true,    32 * 8, true, s_type_Cause),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x1C, "Facility",       true,   255 * 8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x7E, "UserUser",       true,   131 * 8, true, s_type_Undef),
    s_ie_EndDef,
};

// reference: ETSI TS 124 008 V11.6.0, section 9.3.19.1 Release complete (network to mobile station direction)
static const IEParam s_ccStatusParams[] = {
    MAKE_IE_PARAM(LV,     XmlElem,    0, "Cause",          false,    31 * 8, true, s_type_Cause),
    MAKE_IE_PARAM(V,      XmlElem,    0, "CallState",      false,         8, true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x24, "AuxiliaryStates", true,     3 * 8, true, s_type_Undef),
    s_ie_EndDef,
};

static const RL3Message s_ccMsgs[] = {
    //TODO
    {0x01,    "Alerting",            s_ccAlertFromMSParams,    s_ccAlertToMSParams},
    {0x02,    "CallProceeding",      s_ccCallProceedParams,    0},
    {0x05,    "Setup",               s_ccSetupFromMSParams,    s_ccSetupToMSParams},
    {0x07,    "Connect",             s_ccConnFromMSParams,     s_ccConnToMSParams},
    {0x0f,    "ConnectAcknowledge",  0,                        0},
    {0x25,    "Disconnect",          s_ccDisconnFromMSParams,  s_ccDisconnToMSParams},
    {0x2d,    "Release",             s_ccRelFromMSParams,      s_ccRelToMSParams},
    {0x2a,    "ReleaseComplete",     s_ccRelComplFromMSParams, s_ccRelComplToMSParams},
    {0x34,    "StatusEnquiry",       0,                        0},
    {0x3d,    "Status",              s_ccStatusParams,         0},
    {0xff,    "",                    0,                        0},
};


// EPS Session Management message definitions

// reference: ETSI TS 124 301 V11.8.0, section 8.3.20 PDN connectivity request
static const IEParam s_epsPdnConnReqParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "RequestType",                 false,       4,  true, s_type_EpsReqType),
    MAKE_IE_PARAM(V,      XmlElem,    0, "PDNType",                     false,       4, false, s_type_EpsPdnType),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "ESMInformationTransferFlag",   true,       8,  true, s_type_EsmEITFlag),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x28, "AccessPointName",              true, 102 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x27, "ProtocolConfigurationOptions", true, 253 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xC0, "DeviceProperties",             true,       8,  true, s_type_Undef),
    s_ie_EndDef,
};

// EPS Session Management Messages
// reference: ETSI TS 124 301 V11.8.0, section 9.8
static const RL3Message s_epsSmMsgs[] = {
    {0xc1, "ActivateDefaultEPSBearerContextRequest",   0,    0},
    {0xc2, "ActivateDefaultEPSBearerContextAccept",    0,    0},
    {0xc3, "ActivateDefaultEPSBearerContextReject",    0,    0},
    {0xc5, "ActivateDedicatedEPSBearerContextRequest", 0,    0},
    {0xc6, "ActivateDedicatedEPSBearerContextAccept",  0,    0},
    {0xc7, "ActivateDedicatedEPSBearerContextReject",  0,    0},
    {0xc9, "ModifyEPSBearerContextRequest",            0,    0},
    {0xca, "ModifyEPSBearerContextAccept",             0,    0},
    {0xcb, "ModifyEPSBearerContextReject",             0,    0},
    {0xcd, "DeactivateEPSBearerContextRequest",        0,    0},
    {0xce, "DeactivateEPSBearerContextaccept",         0,    0},
    {0xd0, "PDNConnectivityRequest",                   s_epsPdnConnReqParams,    0},
    {0xd1, "PDNConnectivityReject",                    0,    0},
    {0xd2, "PDNDisconnectRequest",                     0,    0},
    {0xd3, "PDNDisconnectReject",                      0,    0},
    {0xd4, "BearerResourceAllocationRequest",          0,    0},
    {0xd5, "BearerResourceAllocationReject",           0,    0},
    {0xd6, "BearerResourceModificationRequest",        0,    0},
    {0xd7, "BearerResourceModificationReject",         0,    0},
    {0xd9, "ESMInformationRequest",                    0,    0},
    {0xda, "ESMInformationResponse",                   0,    0},
    {0xdb, "Notification",                             0,    0},
    {0xe8, "ESMStatus",                                0,    0},
    {0xff, "", 0,    0},
};


// EPS Mobile Management message defitions

// reference: ETSI TS 124 301 V11.8.0, section 8.2.4 Attach request
static const IEParam s_epsAttachRequestParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "EPSAttachType",                          false,      4,  true, s_type_EpsAttachTypes),
    MAKE_IE_PARAM(V,      XmlElem,    0, "NASKeySetIdentifier",                    false,      4, false, s_type_NASKeySetId),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "EPSMobileIdentity",                      false, 12 * 8,  true, s_type_EPSMobileIdent),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "UENetworkCapability",                    false, 14 * 8,  true, s_type_UENetworkCapab),
    MAKE_IE_PARAM(LVE,    XmlElem,    0, "ESMMessageContainer",                    false,      0,  true, s_type_RL3Msg),
    MAKE_IE_PARAM(TV,     XmlElem, 0x19,"OldPTMSISignature",                        true,  4 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x50,"AdditionalGUTI",                           true, 13 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0x52,"LastVisitedRegisteredTAI",                 true,  6 * 8,  true, s_type_TAI),
    MAKE_IE_PARAM(TV,     XmlElem, 0x5C,"DRXParameter",                             true,  3 * 8,  true, s_type_DRX),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x31,"MSNetworkCapability",                      true, 10 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0x13,"OldLocationAreaIdentification",            true,  6 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0x90,"TMSIStatus",                               true,      8,  true, s_type_TMSIStatus),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x11,"MobileStationClassmark2",                  true,  5 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x20,"MobileStationClassmark3",                  true, 34 * 8,  true, s_type_Undef),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x40,"SupportedCodecs",                          true,      0,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xF0,"AdditionalUpdateType",                     true,      8,  true, s_type_AdditionalUpdateType),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x5D,"VoiceDomainPreferenceAndUEsUsageSetting",  true,  3 * 8,  true, s_type_VoicePreference),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0,"DeviceProperties",                         true,      8,  true, s_type_Undef),
    MAKE_IE_PARAM(TV,     XmlElem, 0xE0,"OldGUTIType",                              true,      8,  true, s_type_GUTIType),
    MAKE_IE_PARAM(TV,     XmlElem, 0xC0,"MSNetworkFeatureSupport",                  true,      8,  true, s_type_MSNetFeatSupp),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x10,"TMSIBasedNRIContainer",                    true,  4 * 8,  true, s_type_Undef),
    s_ie_EndDef,
};

static const RL3Message s_epsMmMsgs[] = {
    // TODO
    {0x41,    "AttachRequest",     s_epsAttachRequestParams,    0},
    {0xff,    "",                  0,                           0},
};


// Message definitions according to protocol discriminator type

MAKE_IE_TYPE(MM_Msg,decodeMsgType,encodeMsgType,s_mmMsgs)
MAKE_IE_TYPE(CC_Msg,decodeMsgType,encodeMsgType,s_ccMsgs)
MAKE_IE_TYPE(EPS_SM_Msg,decodeMsgType,encodeMsgType,s_epsSmMsgs)

static const IEParam s_mmMessage[] = {
    MAKE_IE_PARAM(V,      XmlElem, 0, "SkipIndicator", false, 4, false, s_type_Int),
    MAKE_IE_PARAM(V,      XmlRoot, 0, "Message",       false, 8, false, s_type_MM_Msg),
    s_ie_EndDef,
};

static const IEParam s_ccMessage[] = {
    MAKE_IE_PARAM(V,      XmlElem, 0, "TID",           false, 4, false, s_type_TID),
    MAKE_IE_PARAM(V,      XmlRoot, 0, "Message",       false, 8, false, s_type_CC_Msg),
    s_ie_EndDef,
};

// reference: ETSI TS 124 301 V11.8.0,section 8.3
static const IEParam s_epsSmMessage[] = {
    MAKE_IE_PARAM(V,      XmlElem, 0, "EPSBearerIdentity", false, 4, false, s_type_Undef),
    MAKE_IE_PARAM(V,      XmlElem, 0, "PTID",              false, 8, false, s_type_Undef),
    MAKE_IE_PARAM(V,      XmlRoot, 0, "Message",           false, 8, false, s_type_EPS_SM_Msg),
    s_ie_EndDef,
};

static const IEParam s_epsMmMessage[] = {
    MAKE_IE_PARAM(V,      XmlElem, 0, "SecurityHeader", false, 4,     false, s_type_SecurityHeader),
    s_ie_EndDef,
};

// reference ETSI TS 124 007 V11.0.0, section  11.2.3.1.1 Protocol discriminator
static const RL3Message s_protoMsg[] = {
    {GSML3Codec::GCC,        "GCC",     0,                 0},
    {GSML3Codec::BCC,        "BCC",     0,                 0},
    {GSML3Codec::EPS_SM,     "EPS_SM",  s_epsSmMessage,    0},
    {GSML3Codec::CC,         "CC",      s_ccMessage,       0},
    {GSML3Codec::GTTP,       "GTTP",    0,                 0},
    {GSML3Codec::MM,         "MM",      s_mmMessage,       0},
    {GSML3Codec::RRM,        "RRM",     0,                 0},
    {GSML3Codec::EPS_MM,     "EPS_MM",  s_epsMmMessage,    0},
    {GSML3Codec::GPRS_MM,    "GPRS_MM", 0,                 0},
    {GSML3Codec::SMS,        "SMS",     0,                 0},
    {GSML3Codec::GPRS_SM,    "GPRS_SM", 0,                 0},
    {GSML3Codec::SS,         "SS",      0,                 0},
    {GSML3Codec::LCS,        "LCS",     0,                 0},
    {GSML3Codec::Extension,  "EXT",     0,                 0},
    {GSML3Codec::Test,       "TEST",    0,                 0},
    {GSML3Codec::Unknown,    "",        0,                 0},
};

MAKE_IE_TYPE(PD,decodePD,encodePD,s_protoMsg)

static const IEParam s_rl3Message[] = {
    MAKE_IE_PARAM(V,       XmlRoot, 0, "PD", false, 4, true, s_type_PD),
    s_ie_EndDef,
};

#undef MAKE_IE_TYPE
#undef MAKE_IE_PARAM

static unsigned int checkIntegrity(const GSML3Codec* codec, const String& mac, uint8_t seq,  const uint8_t*& in,
	unsigned int& len, const NamedList& params)
{
    // TODO
    return GSML3Codec::NoError;
}

static unsigned int addIntegrity(const GSML3Codec* codec, uint8_t seq, DataBlock& data, const NamedList& params)
{
    // TODO - code just to add the octets now
    uint32_t mac = 0;
    data.insert(DataBlock(&mac,sizeof(mac)));
    return GSML3Codec::NoError;
}
static unsigned int decipherNASPdu(const GSML3Codec* codec, const String& mac, uint8_t seq,  const uint8_t*& in,
	unsigned int& len, const NamedList& params)
{
    // TODO
    return GSML3Codec::NoError;
}

static unsigned int cipherNASPdu(const GSML3Codec* codec, uint8_t seq, DataBlock& data, const NamedList& params)
{
    // TODO
    return GSML3Codec::NoError;
}

static unsigned int  decodeSecHeader(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param && out))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeSecHeader(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    uint8_t secVal = getUINT8(in,len,param);
    XmlElement* xml = new XmlElement(param->name,lookup(secVal,GSML3Codec::s_securityHeaders,String(secVal)));
    out->addChildSafe(xml);

    switch (secVal) {
	case GSML3Codec::PlainNAS:
	{
	    if (len < 1)
		return GSML3Codec::MsgTooShort;
	    uint8_t msgType = in[0];
	    unsigned int ok = GSML3Codec::NoError;
	    advanceBuffer(1,in,len);
	    const RL3Message* msg = findRL3Msg(msgType,s_epsMmMsgs);
	    xml = 0;
	    if (!msg) {
		xml = new XmlElement(param->name ? param->name : YSTRING("ie"));
		xml->setText(String(msgType));
	    }
	    else {
		xml = new XmlElement(msg->name);
		if (const IEParam* msgParams = getParams(codec,msg))
		    ok = decodeParams(codec,proto,in,len,xml,msgParams,params);
	    }
	    out->addChildSafe(xml);
	    return ok;
	}
	case GSML3Codec::IntegrityProtect:
	case GSML3Codec::IntegrityProtectNewEPSCtxt:
	case GSML3Codec::IntegrityProtectCiphered:
    	case GSML3Codec::IntegrityProtectCipheredNewEPSCtxt:
	{
	    if (len < 5)
		return GSML3Codec::MsgTooShort;
	    String mac;
	    mac.hexify((void*)in,4);
	    out->addChildSafe(new XmlElement("MAC",mac));
	    uint8_t seq = in[4];
	    out->addChildSafe(new XmlElement("SequenceNumber",String(seq)));
	    // skip over MAC
	    advanceBuffer(4,in,len);
	    if (unsigned int ok = checkIntegrity(codec,mac,seq,in,len,params))
		return ok;
	    // skip over Sequence Number
	    advanceBuffer(1,in,len);
	    if (secVal == GSML3Codec::IntegrityProtectCiphered || secVal == GSML3Codec::IntegrityProtectCiphered)
		decipherNASPdu(codec,mac,seq,in,len,params);
	    return decodeParams(codec,proto,in,len,out,s_rl3Message);
	}
	default:
	    if (secVal >= GSML3Codec::ServiceRequestHeader) {
		//TODO 
		DDebug(codec->dbg(),DebugStub,"decodeSecHeader() for ServiceRequestHeader not implemented [%p]",codec->ptr());
	    }
	    break;
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeSecHeader(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeSecHeader(param=%s(%p),xml=%s(%p) [%p]",param->name.c_str(),param,
	    in->tag(),in,codec->ptr());

    XmlElement* child = in->findFirstChild(&param->name);
    if (!child)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    uint8_t secVal = lookup(child->getText(),GSML3Codec::s_securityHeaders,0xff);
    switch (secVal) {
	case GSML3Codec::PlainNAS:
	{
	    setUINT8(secVal,out,param);
	    in = in->findFirstChild(&YSTRING("Message"));
	    if (!in)
		return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
	    const RL3Message* msg = findRL3Msg(in->attribute(s_typeAttr),s_epsMmMsgs);
	    if (!msg) {
		Debug(codec->dbg(),DebugWarn,"Did not find message type for Plain NAS PDU in %s [%p]",in->tag(),codec->ptr());
		return GSML3Codec::UnknownMsgType;
	    }
	    uint16_t type = msg->value;
	    out.append(&type,1);
	    setUINT8(msg->value,out,param);
	    if (const IEParam* msgParams = getParams(codec,msg,true))
		return encodeParams(codec,proto,in,out,msgParams,params);
	    return GSML3Codec::NoError;
	}
	case GSML3Codec::IntegrityProtect:
	case GSML3Codec::IntegrityProtectNewEPSCtxt:
	case GSML3Codec::IntegrityProtectCiphered:
    	case GSML3Codec::IntegrityProtectCipheredNewEPSCtxt:
	{
	    setUINT8(secVal,out,param);

	    uint8_t seq = 0;
	    const String& seqParam = params[s_epsSequenceNumber];
	    if (seqParam)
		seq = seqParam.toInteger();
	    else {
		child = in->findFirstChild(&s_epsSequenceNumber);
		if (!(child && child->getText())) {
		    Debug(codec->dbg(),DebugWarn,"Missing SequenceNumber param [%p]",codec->ptr());
		    return GSML3Codec::MissingMandatoryIE;
		}
		seq = child->getText().toInteger();
	    }
	    DataBlock d;
	    if (unsigned int stat = encodeParams(codec,proto,in,d,s_rl3Message,params))
		return stat;
	    if (secVal == GSML3Codec::IntegrityProtectCiphered || secVal == GSML3Codec::IntegrityProtectCiphered)
 		if (unsigned int stat = cipherNASPdu(codec,seq,d,params))
		    return stat;
	    d.insert(DataBlock(&seq,1));
	    if (unsigned int stat = addIntegrity(codec,seq,d,params))
		return stat;
	    out.append(d);
	    return GSML3Codec::NoError;
	}
	default:
	    if (secVal >= GSML3Codec::ServiceRequestHeader) {
		//TODO 
		DDebug(codec->dbg(),DebugStub,"encodeSecHeader() for ServiceRequestHeader not implemented [%p]",codec->ptr());
	    }
	    break;
    }
    return GSML3Codec::NoError;
}

static unsigned int decodeRL3Msg(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeRL3Msg(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    if (len < 2)
	return GSML3Codec::MsgTooShort;
    XmlElement* xml = 0;
    if (param->name)
	xml = new XmlElement(param->name);
    unsigned int stat = decodeParams(codec,proto,in,len,xml,s_rl3Message);
    addXMLElement(out,xml);
    return stat;
}

static unsigned int encodeRL3Msg(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    //TODO
    return GSML3Codec::NoError;
}


static unsigned int skipParam(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len,const IEParam* param)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"skipParam() param=%s(%p) of type %s [%p]",param->name.c_str(),param,
	   lookup(param->type,GSML3Codec::s_typeDict,""),codec->ptr());
    switch (param->type) {
	case GSML3Codec::V:
	case GSML3Codec::T:
	    if (param->length == 4) {
		if (!param->lowerBits)
		    advanceBuffer(1,in,len);
		break;
	    }
	    // intentional fall through
	case GSML3Codec::TV:
	    if (len * 8 < param->length)
		return GSML3Codec::MsgTooShort;
	    advanceBuffer(param->length / 8,in,len);
	    break;
	case GSML3Codec::TLV:
	    if (len < 2)
		return GSML3Codec::MsgTooShort;
	    advanceBuffer(1,in,len);
	    // intentional fall through
	case GSML3Codec::LV:
	{
	    if (len < 1)
		return GSML3Codec::MsgTooShort;
	    uint8_t l = *in;
	    advanceBuffer(1,in,len);
	    if (len < l)
		return GSML3Codec::MsgTooShort;
	    advanceBuffer(l,in,len);
	    break;
	}
	case GSML3Codec::TLVE:
	    if (len < 3)
		return GSML3Codec::MsgTooShort;
	    advanceBuffer(1,in,len);
	    // intentional fall through
	case GSML3Codec::LVE:
	{
	    if (len < 2)
		return GSML3Codec::MsgTooShort;
	    uint16_t l = getUINT16(in,len,true);
	    if (len < l)
		return GSML3Codec::MsgTooShort;
	    advanceBuffer(l,in,len);
	    break;
	}
	case GSML3Codec::NoType:
	    break;
    }
    return GSML3Codec::NoError;
}

static unsigned int dumpUnknownIE(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out)
{
    if (!codec)
	return GSML3Codec::ParserErr;
    if (!(in && len))
	return GSML3Codec::NoError;
    DDebug(codec->dbg(),DebugAll,"dumpUnknownIE(in=%p,len=%u) in protocol=%s [%p]",in,len,
	   lookup(proto,GSML3Codec::s_protoDict,"Unknown"),codec->ptr());
    uint8_t iei = *in;
    // bit 8 on 1 indicates one octet length IE of type V/T/TV
    unsigned int dumpOctets = len;
    if (iei & 0x80 || len < 2)
	dumpOctets = len;
    else {
	// it's either TLV or TLVE
	// in EPS MM and EPS MM, if bits 7 to 4 are set on 1 => means IEI is TLVE
	if ((proto == GSML3Codec::EPS_MM || proto == GSML3Codec::EPS_SM) && ((iei & 0x78) == 0x78)) {
	    if (len < 3)
		dumpOctets = len;
	    else {
		uint16_t l = getUINT16(in + 1u,len) + 3u;
		dumpOctets = (len < l ? len : l);
	    }
	}
	else
	    dumpOctets = (len < (in[1] + 2u) ? len : in[1] + 2u);
    }
    if (dumpOctets) {
	XmlElement* xml = new XmlElement("ie");
	addXMLElement(out,xml);
	String dumpStr;
	dumpStr.hexify((void*)in,dumpOctets);
	xml->setText(dumpStr);
	xml->setAttribute(s_encAttr,"hex");
	advanceBuffer(dumpOctets,in,len);
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeUnknownIE(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out)
{
    if (!codec)
	return GSML3Codec::ParserErr;
    if (!in)
	return GSML3Codec::NoError;
    DDebug(codec->dbg(),DebugAll,"encodeUnknownIE(in=%p) in protocol=%s [%p]",in,
	   lookup(proto,GSML3Codec::s_protoDict,"Unknown"),codec->ptr());
    DataBlock d;
    if (!d.unHexify(in->getText())) {
	Debug(codec->dbg(),DebugMild,"Failed to unhexify unknown param=%s(%p) [%p]",in->tag(),in,codec->ptr());
	return GSML3Codec::NoError;
    }
    out.append(d);
    return GSML3Codec::NoError;
}

static unsigned int dumpParamValue(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, const IEParam* param,
    XmlElement*& out)
{
    if (!(codec && in && len))
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"dumpParamValue(in=%p,len=%u) for %sparam%s%s(%p) [%p]",in,len,
	   (!param ? "unknown " : ""),(param ? "=" : ""), (param ? param->name.c_str() : ""),param,codec->ptr());
    if (param) {
	String dumpStr;
	uint8_t skipOctets = 0;
	switch (param->type) {
	    case GSML3Codec::T:
		// there's no value to dump
		break;
	    case GSML3Codec::V:
	    {
		if (param->length == 4) {
		    uint8_t val = 0;
		    if (!param->lowerBits) {
			val |= *in & 0xf0;
			advanceBuffer(1,in,len);
		    }
		    else
			val |= *in & 0x0f;
		    dumpStr.hexify(&val,1);
		}
		else
		    skipOctets = param->length / 8;
		break;
	    }
	    case GSML3Codec::TV:
	    {
		if (param->length == 8) {
		    uint8_t val = *in & 0x0f;
		    advanceBuffer(1,in,len);
		    dumpStr.hexify(&val,1);
		}
		else
		    skipOctets = 1;
		break;
	    }
	    case GSML3Codec::TLV:
		skipOctets = 2;
		break;
	    case GSML3Codec::LV:
		skipOctets = 1;
		break;
	    case GSML3Codec::TLVE:
		skipOctets = 3;
		break;
	    case GSML3Codec::LVE:
		skipOctets = 2;
		break;
	    case GSML3Codec::NoType:
		break;
	}
	if (skipOctets) {
	    const uint8_t* buff = in;
	    unsigned int lbuff = len;
	    if (int status = skipParam(codec,proto,in,len,param))
		return status;
	    if (len <= lbuff)
		dumpStr.hexify((void*)(buff + skipOctets), lbuff - len - skipOctets);
	}
	XmlElement* xml = new XmlElement(param->name);
	addXMLElement(out,xml);
	if (dumpStr) {
	    xml->setText(dumpStr);
	    xml->setAttribute(s_encAttr,"hex");
	}
    }
    else
	return dumpUnknownIE(codec,proto,in,len,out);

    return GSML3Codec::NoError;
}

static unsigned int encodeHexParam(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out,
	const IEParam* param)
{
    if (!(codec && in))
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"encodeHexParam() xml=%s(%p) for %sparam%s%s(%p) [%p]",in->tag(),in,
	   (!param ? "unknown " : ""),(param ? "=" : ""), (param ? param->name.c_str() : ""),param,codec->ptr());
    if (param) {
	DataBlock d;
	if (!d.unHexify(in->getText())) {
	    Debug(codec->dbg(),DebugMild,"Failed to unhexify param=%s(%p) [%p]",in->tag(),in,codec->ptr());
	    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
	}
	// mask for encoding, 1 is for T, 2 is for L, 4 is for LE
	uint8_t mask = 0;
	uint8_t iei = param->iei;
	switch (param->type) {
	    case GSML3Codec::T:
		out.append(&iei,1);
		return GSML3Codec::NoError;
	    case GSML3Codec::V:
	    {
		if (!d.length())
		    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
		if (param->length == 4) {
		    uint8_t val = d[0];
		    if (!param->lowerBits) 
			val = val >> 4;
		    setUINT8(val,out,param);
		    d.clear();
		}
		break;
	    }
	    case GSML3Codec::TV:
	    {
		if (!d.length())
		    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
		if (param->length == 8) {
		    uint8_t val = d[0] & 0x0f;
		    val |= param->iei;
		    out.append(&val,1);
		}
		else
		    mask |= 1;
		break;
	    }
	    case GSML3Codec::TLV:
		mask |= 1;
	    // intentional fall through
	    case GSML3Codec::LV:
		if (d.length() > 0xff)
		    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
		mask |= 2;
		break;
	    case GSML3Codec::TLVE:
		mask |= 1;
	    // intentional fall through
	    case GSML3Codec::LVE:
		if (d.length() > 0xffff)
		    return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);
		mask |= 4;
		break;
	    case GSML3Codec::NoType:
		return GSML3Codec::NoError;
	}
	if (mask & 1) // T
	    out.append(&iei,1);
	if (mask & 2) { // L
	    iei = d.length();
	    out.append(&iei,1);
	}
	else if (mask & 4)  { // LE
	    uint16_t len = d.length();
	    uint8_t l[2];
	    setUINT16(len,l,len);
	    out.append(l,2);
	}
	if (d.length())
	    out.append(d);
    }
    else
	return encodeUnknownIE(codec,proto,in,out);

    return GSML3Codec::NoError;
}

static unsigned int decodeV(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    if (len * 8 < param->length)
	return GSML3Codec::MsgTooShort;
    DDebug(codec->dbg(),DebugAll,"decodeV(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	    if (!(param->ieType.decoder || (param->name && param->length <= 8)))
		return dumpParamValue(codec,proto,in,len,param,out);
	    if (param->ieType.decoder)
		return param->ieType.decoder(codec,proto,param,in,len,out,params);
	    // decode an 1 byte value from a dictionary
	    if (param->name) {
		if (param->length > 8) {
		    DDebug(codec->dbg(),DebugMild,"decodeV() - decoding for values longer than 1 byte not supported, dumping param=%s(%p) [%p]",
			   param->name.c_str(),param,codec->ptr());
		    return dumpParamValue(codec,proto,in,len,param,out);
		}
		uint8_t val = getUINT8(in,len,param);
		XmlElement* xml = new XmlElement(param->name);
	        addXMLElement(out,xml);
		const TokenDict* dict = static_cast<const TokenDict*>(param->ieType.data);
		const char* valStr = lookup(val,dict,0);
		if (!valStr) {
		    String defValStr;
		    defValStr.hexify(&val,1);
		    xml->setText(defValStr);
		    xml->setAttribute(s_encAttr,"hex");
		}
		else
		    xml->setText(valStr);
		return GSML3Codec::NoError;
	    }
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeV(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeV(in=%s(%p),out=%p,param=%s[%p]) [%p]",in->tag(),in,&out,
	   param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	{
	    if (param->length > 8) {
		DDebug(codec->dbg(),DebugMild,"encodeV() - encoding skipped param=%s(%p) longer than 1 byte not implemented[%p]",
			   param->name.c_str(),param,codec->ptr());
		return GSML3Codec::ParserErr;
	    }
	    setUINT8(param->iei,out,param);
	    return GSML3Codec::NoError;
	}
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	{
	    if (param->ieType.encoder)
		return param->ieType.encoder(codec,proto,param,in,out,params);
	    XmlElement* xml = in->findFirstChild(&param->name);
		if (!xml)
		    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
	    if (!(param->ieType.encoder || (param->name && param->length <= 8)))
		return encodeHexParam(codec,proto,xml,out,param);
	    // decode an 1 byte value from a dictionary
	    if (param->name) {
		const TokenDict* dict = static_cast<const TokenDict*>(param->ieType.data);
		uint8_t val = 0;
		if (!dict)
		    val = xml->getText().toInteger(0,16);
		else
		    val = xml->getText().toInteger(dict,0,16);
		setUINT8(val,out,param);
		return GSML3Codec::NoError;
	    }
	    break;
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int decodeLV_LVE(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeLV_LVE(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	{
	    if (!param->ieType.decoder)
		return dumpParamValue(codec,proto,in,len,param,out);
	    bool ext = (param->type == GSML3Codec::LVE);
	    if (len < (ext ? 2 : 1))
		return GSML3Codec::MsgTooShort;
	    unsigned int l = in[0];
	    unsigned int advBytes = 1;
	    if  (ext) {
		l = getUINT16(in,len);
		advBytes = 2;
	    }
	    if (l > len - advBytes)
		return GSML3Codec::MsgTooShort;
	    if (param->length && ((l + advBytes)* 8 > param->length))
		return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);

	    if (param->ieType.decoder) {
		const uint8_t* buf = in + advBytes;
		advanceBuffer(l + advBytes,in,len);
		return param->ieType.decoder(codec,proto,param,buf,l,out,params);
	    }
	    break;
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeLV_LVE(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeLV_LVE(in=%s(%p),out=%p,param=%s[%p]) [%p]",in->tag(),in,&out,
	   param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	{
	    // TODO
	    return GSML3Codec::NoError;
	}
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	{
	    DataBlock d;
	    if (param->ieType.encoder) {
		if (unsigned int status = param->ieType.encoder(codec,proto,param,in,d,params))
		    return status;
	    }
	    else {
		XmlElement* xml = in->findFirstChild(&param->name);
		if (!(xml && d.unHexify(xml->getText())))
		    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
	    }
	    if (param->isOptional && !d.length())
		return GSML3Codec::NoError;
	    if (param->type == GSML3Codec::LVE) {
		uint16_t len = d.length();
		uint8_t l[2];
		setUINT16(len,l,2);
		out.append(l,2);
	    }
	    else {
		uint8_t len = d.length();
		out.append(&len,1);
	    }
	    out.append(d);
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int decodeTV(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeTV(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    if (param->length && (len * 8 < param->length))
	return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);
    if (param->type == GSML3Codec::TV && param->length == 8) {
	if ((param->iei & (*in & 0xf0)) != param->iei)
	    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
    }
    else if (param->iei != *in)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);

    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	{
	    if (!(param->ieType.decoder || (param->name && param->length <= 8)))
		return dumpParamValue(codec,proto,in,len,param,out);

	    if (param->ieType.decoder) {
		uint8_t skip = (param->length == 8 ? 0u : 1u);
		const uint8_t* buf = in + skip;
		unsigned int l = param->length / 8 - skip;
		advanceBuffer(l + skip,in,len);
		return param->ieType.decoder(codec,proto,param,buf,l,out,params);
	    }
	    // decode a max 1 byte value from a dictionary
	    if (param->name) {
		if (param->length > 8) {
		    DDebug(codec->dbg(),DebugMild,"decodeTV() - decoding for TV longer than 1 byte not supported, dumping param=%s(%p) [%p]",
			   param->name.c_str(),param,codec->ptr());
		    return dumpParamValue(codec,proto,in,len,param,out);
		}
		uint8_t val = getUINT8(in,len,param);
		XmlElement* xml = new XmlElement(param->name);
	        addXMLElement(out,xml);
		const TokenDict* dict = static_cast<const TokenDict*>(param->ieType.data);
		const char* valStr = lookup(val,dict,0);
		if (!valStr) {
		    String defValStr;
		    defValStr.hexify(&val,1);
		    xml->setText(defValStr);
		    xml->setAttribute(s_encAttr,"hex");
		}
		else
		    xml->setText(valStr);
		return GSML3Codec::NoError;
	    }
	    break;
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeTV(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeTV(in=%s(%p),out=%p,param=%s[%p]) [%p]",in->tag(),in,&out,
	   param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	{
	    if (param->length > 8) {
		DDebug(codec->dbg(),DebugMild,"encodeTV() - encoding skipped param=%s(%p) longer than 1 byte not implemented[%p]",
			   param->name.c_str(),param,codec->ptr());
		return GSML3Codec::ParserErr;
	    }
	    setUINT8(param->iei,out,param);
	    return GSML3Codec::NoError;
	}
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	{
	    if (param->ieType.encoder) {
		DataBlock d;
		if (unsigned int status = param->ieType.encoder(codec,proto,param,in,d,params))
		    return status;
		if (param->isOptional && !d.length())
		    return GSML3Codec::NoError;
		uint8_t iei = param->iei;
		out.append(&iei,1);
		out.append(d);
	    }
	    else {
		XmlElement* xml = in->findFirstChild(&param->name);
		if (!xml)
		    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
		if (!(param->name && param->length <= 8))
		    return encodeHexParam(codec,proto,xml,out,param);
		
		const TokenDict* dict = static_cast<const TokenDict*>(param->ieType.data);
		uint8_t val = param->iei;
		if (!dict)
		    val |= (xml->getText().toInteger(0,16) & 0x0f);
		else
		    val |= xml->getText().toInteger(dict,0,16);
		out.append(&val,1);
	    }
	    return GSML3Codec::NoError;
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int decodeTLV_TLVE(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"decodeTLV_TLVE(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    bool ext = (param->type == GSML3Codec::TLVE);
    if (len < (ext ? 3 : 2))
	return GSML3Codec::MsgTooShort;
    if (param->iei != *in)
	return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);

    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	{
	    if (!param->ieType.decoder)
		return dumpParamValue(codec,proto,in,len,param,out);
	    unsigned int l = in[1];
	    unsigned int advBytes = 2;
	    if  (param->type == GSML3Codec::LVE) {
		l = getUINT16(in + 1u,len - 1);
		advBytes = 3;
	    }
	    if (l > len - advBytes)
		return GSML3Codec::MsgTooShort;
	    if (param->length && ((l + advBytes) * 8 > param->length))
		return CONDITIONAL_ERROR(param,IncorrectOptionalIE,IncorrectMandatoryIE);

	    if (param->ieType.decoder) {
		const uint8_t* buf = in + advBytes;
		advanceBuffer(l + advBytes,in,len);
		return param->ieType.decoder(codec,proto,param,buf,l,out,params);
	    }
	    break;
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int encodeTLV_TLVE(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
    DDebug(codec->dbg(),DebugAll,"encodeTLV_TLVE(in=%s(%p),out=%p,param=%s[%p]) [%p]",in->tag(),in,&out,
	   param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	{
	    // TODO
	    return GSML3Codec::NoError;
	}
	case GSML3Codec::XmlElem:
	case GSML3Codec::XmlRoot:
	{
	    DataBlock d;
	    if (param->ieType.encoder) {
		if (unsigned int status = param->ieType.encoder(codec,proto,param,in,d,params))
		    return status;
	    }
	    else {
		XmlElement* xml = in->findFirstChild(&param->name);
		if (!(xml && d.unHexify(xml->getText())))
		    return CONDITIONAL_ERROR(param,NoError,MissingMandatoryIE);
	    }
	    if (param->isOptional && !d.length())
		return GSML3Codec::NoError;
	    uint8_t iei = param->iei;
	    out.append(&iei,1);
	    if (param->type == GSML3Codec::TLVE) {
		uint16_t len = d.length();
		uint8_t l[2];
		setUINT16(len,l,2);
		out.append(l,2);
	    }
	    else {
		uint8_t len = d.length();
		out.append(&len,1);
	    }
	    out.append(d);
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int decodeParams(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out, 
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && len && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
#ifdef DEBUG
    Debugger d(DebugAll,"decodeParams()","in=%p,len=%u,out=%p,param=%s(%p)",in,len,out,
	   param->name.c_str(),param,codec->ptr());
#endif
    DDebug(codec->dbg(),DebugAll,"decodeParams(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,
	   param->name.c_str(),param,codec->ptr());
    while (param && param->type != GSML3Codec::NoType) {
	int status = GSML3Codec::NoError;
	switch (param->type) {
	    case GSML3Codec::V:
		status = decodeV(codec,proto,in,len,out,param,params);
		break;
		//TODO
	    case GSML3Codec::T:
		break;
	    case GSML3Codec::TV:
		status = decodeTV(codec,proto,in,len,out,param,params);
		break;
	    case GSML3Codec::LV:
	    case GSML3Codec::LVE:
		status = decodeLV_LVE(codec,proto,in,len,out,param,params);
		break;
	    case GSML3Codec::TLV:
	    case GSML3Codec::TLVE:
		status = decodeTLV_TLVE(codec,proto,in,len,out,param,params);
		break;
	    case GSML3Codec::NoType:
		break;
	}
	if (status)
	    Debug(codec->dbg(),DebugWarn,"Decoding parameter %s failed with status=%s [%p]",param->name.c_str(),
	       lookup(status,GSML3Codec::s_errorsDict,String(status)),codec->ptr());
	else
	    DDebug(codec->dbg(),DebugAll,"Decoding parameter %s finished with status=%s [%p]",param->name.c_str(),
	       lookup(status,GSML3Codec::s_errorsDict,String(status)),codec->ptr());
	if (status && !param->isOptional)
	    return status;
	param++;
    }
    if (len && out) {
	String str;
	str.hexify((void*)in,len);
	out->addChildSafe(new XmlElement("data",str));
	advanceBuffer(len,in,len);
    }
    return GSML3Codec::NoError;
};

static unsigned int encodeParams(const GSML3Codec* codec, uint8_t proto, XmlElement* in, DataBlock& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && param))
	return CONDITIONAL_ERROR(param,NoError,ParserErr);
#ifdef DEBUG
    Debugger d(DebugAll,"encodeParams()"," xml=%s(%p),out=%p,param=%s(%p)",in->tag(),in, &out,
	   param->name.c_str(),param,codec->ptr());
#endif
    unsigned int ok = GSML3Codec::NoError;
    while (param && param->type != GSML3Codec::NoType) {
	int status = GSML3Codec::NoError;
	switch (param->type) {
	    case GSML3Codec::V:
		status = encodeV(codec,proto,in,out,param,params);
		break;
		//TODO
	    case GSML3Codec::T:
		break;
	    case GSML3Codec::TV:
		encodeTV(codec,proto,in,out,param,params);
		break;
	    case GSML3Codec::LV:
	    case GSML3Codec::LVE:
		encodeLV_LVE(codec,proto,in,out,param,params);
		break;
	    case GSML3Codec::TLV:
	    case GSML3Codec::TLVE:
		encodeTLV_TLVE(codec,proto,in,out,param,params);
		break;
	    case GSML3Codec::NoType:
		break;
	}
	XDebug(codec->dbg(),DebugAll,"Encoding parameter %s finished with status=%s [%p]",param->name.c_str(),
	       lookup(status,GSML3Codec::s_errorsDict,String(status)),codec->ptr());
	if (status && !param->isOptional) {
	    Debug(codec->dbg(),DebugMild,"Encoding of mandatory parameter %s finished with status=%s [%p]",param->name.c_str(),
	       lookup(status,GSML3Codec::s_errorsDict,String(status)),codec->ptr());
	    ok = status;
	}
	param++;
    }
    return ok;
};

#undef GET_DIGIT
#undef CONDITIONAL_ERROR


const TokenDict GSML3Codec::s_typeDict[] = {
    {"T",    GSML3Codec::T},
    {"V",    GSML3Codec::V},
    {"TV",   GSML3Codec::TV},
    {"LV",   GSML3Codec::LV},
    {"TLV",  GSML3Codec::TLV},
    {"LVE",  GSML3Codec::LVE},
    {"TLVE", GSML3Codec::TLVE},
    {0, 0},
};

const TokenDict GSML3Codec::s_protoDict[] = {
    {"GCC",       GSML3Codec::GCC},
    {"BCC",       GSML3Codec::BCC},
    {"EPS_SM",    GSML3Codec::EPS_SM},
    {"CC",        GSML3Codec::CC},
    {"GTTP",      GSML3Codec::GTTP},
    {"MM",        GSML3Codec::MM},
    {"RRM",       GSML3Codec::RRM},
    {"EPS_MM",    GSML3Codec::EPS_MM},
    {"GPRS_MM",   GSML3Codec::GPRS_MM},
    {"SMS",       GSML3Codec::SMS},
    {"GPRS_SM",   GSML3Codec::GPRS_SM},
    {"SS",        GSML3Codec::SS},
    {"LCS",       GSML3Codec::LCS},
    {"Extension", GSML3Codec::Extension},
    {"Test",      GSML3Codec::Test},
    {"Unknown",   GSML3Codec::Unknown},
    {0, 0},
};

const TokenDict GSML3Codec::s_securityHeaders[] = {
    {"plain-NAS-message",                                              GSML3Codec::PlainNAS},
    {"integrity-protected",                                            GSML3Codec::IntegrityProtect},
    {"integrity-protected-and-ciphered",                               GSML3Codec::IntegrityProtectCiphered},
    {"integrity-protected-with-new-EPS-security- context",             GSML3Codec::IntegrityProtectNewEPSCtxt},
    {"integrity-protected-and-ciphered-with-new-EPS-security-context", GSML3Codec::IntegrityProtectCipheredNewEPSCtxt},
    {"security-header-for-the-SERVICE-REQUEST-message",                GSML3Codec::ServiceRequestHeader},
    {0, 0},
};

const TokenDict GSML3Codec::s_errorsDict[] = {
    {"NoError",                 GSML3Codec::NoError},
    {"MsgTooShort",             GSML3Codec::MsgTooShort},
    {"UnknownProto",            GSML3Codec::UnknownProto},
    {"ParserErr",               GSML3Codec::ParserErr},
    {"MissingParam",            GSML3Codec::MissingParam},
    {"IncorrectOptionalIE",     GSML3Codec::IncorrectOptionalIE},
    {"IncorrectMandatoryIE",    GSML3Codec::IncorrectMandatoryIE},
    {"MissingMandatoryIE",      GSML3Codec::MissingMandatoryIE},
    {"UnknownMsgType",          GSML3Codec::UnknownMsgType},
    {0, 0},
};

GSML3Codec::GSML3Codec(DebugEnabler* dbg)
    : m_flags(0),
      m_dbg(0),
      m_ptr(0)
{
    DDebug(DebugAll,"Created GSML3Codec [%p]",this);
    setCodecDebug(dbg);
}

unsigned int GSML3Codec::decode(const uint8_t* in, unsigned int len, XmlElement*& out, const NamedList& params)
{
    if (!in || len < 2)
	return MsgTooShort;
    const uint8_t* buff = in;
    unsigned int l = len;
    return decodeParams(this,GSML3Codec::Unknown,buff,l,out,s_rl3Message,params);
}

unsigned int GSML3Codec::encode(const XmlElement* in, DataBlock& out, const NamedList& params)
{
    if (!in)
	return NoError;
    return encodeParams(this,GSML3Codec::Unknown,(XmlElement*)in,out,s_rl3Message,params);
}

unsigned int GSML3Codec::decode(XmlElement* xml, const NamedList& params)
{
    const String& pduMark = params[s_pduCodec];
    if (!(xml && pduMark))
	return MissingParam;
    return decodeXml(xml,params,pduMark);
}

unsigned int GSML3Codec::encode(XmlElement* xml, const NamedList& params)
{
    const String& pduMark = params[s_pduCodec];
    if (!(xml && pduMark))
	return MissingParam;
    return encodeXml(xml,params,pduMark);
}


unsigned int GSML3Codec::decodeXml(XmlElement* xml, const NamedList& params, const String& pduTag)
{
#ifdef DEBUG
    Debugger d(DebugAll,"decodeXml()"," xml=%s (%p) pduTag=%s",xml ? xml->tag() : "",xml,pduTag.c_str());
#endif
    unsigned int status = NoError;
    if (xml->getTag() == pduTag) {
	const String& txt = xml->getText();
	if (txt && xml->hasAttribute(s_encAttr,YSTRING("hex"))) {
	    DataBlock d;
	    if (!d.unHexify(txt)) {
		Debug(dbg(),DebugInfo,"Invalid hexified payload in XmlElement '%s'(%p) [%p]",xml->tag(),xml,ptr());
		return ParserErr;
	    }
	    return  decode((const uint8_t*)d.data(),d.length(),xml,params);
	}
    }

    XmlElement* child = xml->findFirstChild();
    while (child) {
	unsigned int ok = decodeXml(child,params,pduTag);
	if (ok != NoError)
	    status = ok;
	child = xml->findNextChild(child);
    }

    return status;
}

unsigned int GSML3Codec::encodeXml(XmlElement* xml, const NamedList& params, const String& pduTag)
{
#ifdef DEBUG
    Debugger d(DebugAll,"encodeXml()"," xml=%s (%p) pduTag=%s",xml ? xml->tag() : "",xml,pduTag.c_str());
#endif
    unsigned int status = NoError;
    if (xml->getTag() == pduTag) {
	if (xml->hasAttribute(s_encAttr,YSTRING("xml"))) {
	    if (!(xml = xml->findFirstChild())) {
		Debug(dbg(),DebugInfo,"No XML to encode in XmlElement '%s'(%p) [%p]",xml->tag(),xml,ptr());
		return ParserErr;
	    }
	    DataBlock d;
	    unsigned int status = encode(xml,d,params);
	    String s;
	    s.hexify(d.data(),d.length());
	    if (!status) {
		xml->clearChildren();
		xml->setAttribute(s_encAttr,YSTRING("hex"));
	    }
	    xml->setText(s);
	    return status;
	}
    }

    XmlElement* child = xml->findFirstChild();
    while (child) {
	unsigned int ok = encodeXml(child,params,pduTag);
	if (ok != NoError)
	    status = ok;
	child = xml->findNextChild(child);
    }

    return status;
}

void GSML3Codec::setCodecDebug(DebugEnabler* enabler, void* ptr)
{
    m_dbg = enabler ? enabler : m_dbg;
    m_ptr = ptr ? ptr : (void*)this;
}
