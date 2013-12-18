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
    GSML3Codec::Type type;
    GSML3Codec::XmlType xmlType;
    uint16_t iei;
    const String name;
    bool isOptional;
    uint16_t length; // in bits
    bool lowerBits;
    unsigned int (*decoder)(const GSML3Codec*,uint8_t,const IEParam*,const uint8_t*&, unsigned int&, XmlElement*&,
	const NamedList&);
    unsigned int (*encoder)(const GSML3Codec*,uint8_t,const IEParam*,XmlElement*,DataBlock&,const NamedList&);
    const void* data;
};

struct RL3Message {
    uint16_t value;
    const String name;
    const IEParam* params;
};


static const String s_pduDecode = "decodeTag";
static const char s_digits[] = "0123456789*#ABC";


static unsigned int decodeParams(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len,
	XmlElement*& out, const IEParam* param, const NamedList& params = NamedList::empty());

static unsigned int  decodeSecHeader(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params);

static unsigned int encodeSecHeader(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params);

static unsigned int decodeRL3Msg(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params);

static unsigned int encodeRL3Msg(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params);


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

static inline uint16_t getLE(const uint8_t*& in, unsigned int& len, bool advance = true)
{
    if (!(in && len >= 2))
	return 0;
    uint16_t l = getUINT16(in,len);
    if (advance)
	advanceBuffer(2,in,len);
    return l;
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

static inline void getMCCMNC(const uint8_t*& in, unsigned int& len, XmlElement* xml, bool advance = true)
{
    if (len < 3 || !xml)
	return;
    String out;
    // get MCC
    out << s_digits[(in[0] & 0x0f)] << s_digits[((in[0] >> 4) & 0x0f)] << s_digits[(in[1] & 0x0f)];
    // get MNC
    out << s_digits[(in[2] & 0x0f)] << s_digits[((in[2] >> 4) & 0x0f)];
    uint8_t digit3 = (in[1] >> 4) & 0x0f;
    if (digit3 != 0x0f)
	out << s_digits[digit3];
    xml->addChildSafe(new XmlElement("MCC_MNC",out));
    if (advance)
	advanceBuffer(3,in,len);
}


// reference ETSI TS 124 007 V11.0.0, section11.2.3.2 Message type octet
static unsigned int decodeMsgType(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param && out))
	return GSML3Codec::ParserErr;
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
	    out->addChildSafe(new XmlElement("NSD",String(nsd)));
	    val &= 0x3f;
	    break;
	}
	default:
	    break;
    }
    const RL3Message* msg = static_cast<const RL3Message*>(param->data);
    msg = findRL3Msg(val,msg);
    if (!msg)
	return GSML3Codec::UnknownMsgType;
    XmlElement* xml = new XmlElement(msg->name);
    addXMLElement(out,xml);
    if (msg->params)
	return decodeParams(codec,proto,in,len,xml,msg->params,params);
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
    //TODO
    return GSML3Codec::NoError;
}

// reference ETSI TS 124 007 V11.0.0, section 11.2.3.1.1 Protocol discriminator
static unsigned int decodePD(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodePD(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    XmlElement* payload = 0;
    XmlElement* xml = 0;
    if (codec->flags() & GSML3Codec::XmlDumpMsg) {
	String s;
	s.hexify((void*)in,len);
	payload = new XmlElement(YSTRING("message_payload"),s);
    }
    uint8_t val = getUINT8(in,len,param);
    unsigned int status = GSML3Codec::NoError;
    const RL3Message* msg = static_cast<const RL3Message*>(param->data);
    msg = findRL3Msg(val,msg);
    if (!msg) {
	xml = new XmlElement(param->name ? param->name : YSTRING("ie"));
	xml->setText(String(val));
    }
    else {
	xml = new XmlElement(msg->name);
	if (msg->params)
	    status = decodeParams(codec,proto,in,len,xml,msg->params,params);
    }
    addXMLElement(out,xml);
    if (payload)
	xml->addChildSafe(payload);
    return status;
}


static unsigned int encodePD(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    //TODO
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.21 NAS key set identifier
static unsigned int decodeNASKeyId(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodeNASKeyId(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    uint8_t val = getUINT8(in,len,param);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);
    if (val & 0x08)
	xml->addChildSafe(new XmlElement("TSC","mapped-security-context-for-KSI_SGSN"));
    else
	xml->addChildSafe(new XmlElement("TSC","native-security-context-for-KSI_ASME"));
    xml->addChildSafe(new XmlElement("NASKeySetId",String((val & 0x07))));
    return GSML3Codec::NoError;
}

static unsigned int encodeNASKeyId(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
	DataBlock& out, const NamedList& params)
{
    //TODO
    return GSML3Codec::NoError;
}

// reference: ETSI TS 124 301 V11.8.0, section 9.9.3.12 EPS mobile identity
static unsigned int decodeEPSMobileIdent(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param))
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodeEPSMobileIdent(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    uint8_t type = in[0] & 0x07;
//     bool odd = (in[0] & 0x08);
    switch (type) {
	case 1:
	case 3:
	{
	    // IMSI / IMEI
	    XmlElement* child = new XmlElement((type == 1 ? "IMSI" : "IMEI"));
	    xml->addChildSafe(child);

	    String digits;
	    uint8_t digit = (in[0] >> 4);
	    if ((digit & 0x0f) != 0x0f) {
		digits += s_digits[digit];
		unsigned int index = 1;
		while (index < len) {
		    digits += s_digits[(in[index] & 0x0f)];
		    digit = (in[index++] >> 4);
		    if ((digit & 0x0f) != 0x0f)
			digits += s_digits[digit];
		    else
			break;
		}
		advanceBuffer(index,in,len);
	    }
	    child->addText(digits);
	    break;
	}
	case 6:
	{
	    // GUTI
	    if (len < 11)
		return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);
	    advanceBuffer(1,in,len);
	    XmlElement* child = new XmlElement("GUTI");
	    xml->addChildSafe(child);
	    // get MCC_MNC
	    getMCCMNC(in,len,child);
	    // get MME Group ID (16 bits)
	    uint16_t groupID = getUINT16(in,len);
	    child->addChildSafe(new XmlElement("MMEGroupID",String(groupID)));
	    advanceBuffer(2,in,len);
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
	    return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);
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
    //TODO
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
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodeUENetworkCapab(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    if (len < 2)
	return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    uint16_t mandBytes = getUINT16(in,len);
    String flags;
    getFlags(mandBytes,s_UENetworkCapabMandatory,flags);
    advanceBuffer(2,in,len);
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
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodeTAI(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());

    if (len < 5)
	return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);
    XmlElement* xml = new XmlElement(param->name);
    addXMLElement(out,xml);

    // get MCC MNC
    getMCCMNC(in,len,xml);
    // get TAC
    String str;
    str.hexify((void*)in,len);
    xml->addChildSafe(new XmlElement("TAC",str));
    return GSML3Codec::NoError;
}

static unsigned int encodeTAI(const GSML3Codec* codec,  uint8_t proto, const IEParam* param, XmlElement* in,
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
	return GSML3Codec::ParserErr;
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


#define MAKE_IE_PARAM(type,xml,iei,name,optional,length,lowerBits,decoder,encoder,extra) \
    {GSML3Codec::type,GSML3Codec::xml,iei,name,optional,length,lowerBits,decoder,encoder,extra}

static const RL3Message s_mmMsgs[] = {
    //TODO
    {0xff,    "",                  0},
};

static const IEParam s_mmMessage[] = {
    MAKE_IE_PARAM(V,      Skip,    0, "SkipIndicator", false, 4, false, 0,             0,             0),
    MAKE_IE_PARAM(V,      XmlElem, 0, "MessageType",   false, 8, false, decodeMsgType, encodeMsgType, s_mmMsgs),
    MAKE_IE_PARAM(NoType, Skip,    0, "",              0,     0, 0,     0,             0,             0 ),
};

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

// reference: ETSI TS 124 301 V11.8.0, section 8.3.20 PDN connectivity request
static const IEParam s_epsPdnConnReqParams[] = {

    MAKE_IE_PARAM(V,      XmlElem,    0, "RequestType",                 false,       4,  true, 0,  0, s_epsReqType),
    MAKE_IE_PARAM(V,      XmlElem,    0, "PDNType",                     false,       4, false, 0,  0, s_epsPdnType),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0, "ESMInformationTransferFlag",   true,       8,  true, 0,  0, s_esmEITFlag),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x28, "AccessPointName",              true, 102 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x27, "ProtocolConfigurationOptions", true, 253 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0xC0, "DeviceProperties",             true,       8,  true, 0,  0, 0),
    MAKE_IE_PARAM(NoType, Skip, 0, "", 0, 0, 0, 0, 0, 0),
};

// EPS Session Management Messages
// reference: ETSI TS 124 301 V11.8.0, section 9.8
static const RL3Message s_epsSmMsgs[] = {
    {0xc1, "ActivateDefaultEPSBearerContextRequest",   0},
    {0xc2, "ActivateDefaultEPSBearerContextAccept",    0},
    {0xc3, "ActivateDefaultEPSBearerContextReject",    0},
    {0xc5, "ActivateDedicatedEPSBearerContextRequest", 0},
    {0xc6, "ActivateDedicatedEPSBearerContextAccept",  0},
    {0xc7, "ActivateDedicatedEPSBearerContextReject",  0},
    {0xc9, "ModifyEPSBearerContextRequest",            0},
    {0xca, "ModifyEPSBearerContextAccept",             0},
    {0xcb, "ModifyEPSBearerContextReject",             0},
    {0xcd, "DeactivateEPSBearerContextRequest",        0},
    {0xce, "DeactivateEPSBearerContextaccept",         0},
    {0xd0, "PDNConnectivityRequest",                   s_epsPdnConnReqParams},
    {0xd1, "PDNConnectivityReject",                    0},
    {0xd2, "PDNDisconnectRequest",                     0},
    {0xd3, "PDNDisconnectReject",                      0},
    {0xd4, "BearerResourceAllocationRequest",          0},
    {0xd5, "BearerResourceAllocationReject",           0},
    {0xd6, "BearerResourceModificationRequest",        0},
    {0xd7, "BearerResourceModificationReject",         0},
    {0xd9, "ESMInformationRequest",                    0},
    {0xda, "ESMInformationResponse",                   0},
    {0xdb, "Notification",                             0},
    {0xe8, "ESMStatus",                                0},
    {0xff, "", 0},
};

// reference: ETSI TS 124 301 V11.8.0,section 8.3
static const IEParam s_epsSmMessage[] = {
    MAKE_IE_PARAM(V,      XmlElem, 0, "EPSBearerIdentity", false, 4, false, 0,             0,             0),
    MAKE_IE_PARAM(V,      XmlElem, 0, "PTID",              false, 8, false, 0,             0,             0),
    MAKE_IE_PARAM(V,      XmlElem, 0, "MessageType",       false, 8, false, decodeMsgType, encodeMsgType, s_epsSmMsgs),
    MAKE_IE_PARAM(NoType, Skip,    0, "",                  0,     0, 0,     0,             0,             0 ),
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

// reference: ETSI TS 124 301 V11.8.0, section 8.2.4 Attach request
static const IEParam s_epsAttachRequestParams[] = {
    MAKE_IE_PARAM(V,      XmlElem,    0, "EPSAttachType",                          false,      4,  true, 0, 0, s_epsAttachTypes),
    MAKE_IE_PARAM(V,      XmlElem,    0, "NASKeySetIdentifier",                    false,      4, false, decodeNASKeyId,       encodeNASKeyId,0),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "EPSMobileIdentity",                      false, 12 * 8,  true, decodeEPSMobileIdent, encodeEPSMobileIdent, 0),
    MAKE_IE_PARAM(LV,     XmlElem,    0, "UENetworkCapability",                    false, 14 * 8,  true, decodeUENetworkCapab, encodeUENetworkCapab, 0),
    MAKE_IE_PARAM(LVE,    XmlElem,    0, "ESMMessageContainer",                    false,      0,  true, decodeRL3Msg, encodeRL3Msg, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0x19,"OldPTMSISignature",                        true,  4 * 8,  true, 0, 0, 0),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x50,"AdditionalGUTI",                           true, 13 * 8,  true, 0, 0, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0x52,"LastVisitedRegisteredTAI",                 true,  6 * 8,  true, decodeTAI, encodeTAI, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0x5C,"DRXParameter",                             true,  3 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x31,"MSNetworkCapability",                      true, 10 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0x13,"OldLocationAreaIdentification",            true,  6 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0x90,"TMSIStatus",                               true,      8,  true, 0,  0, s_tmsiStatus),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x11,"MobileStationClassmark2",                  true,  5 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x20,"MobileStationClassmark3",                  true, 34 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x40,"SupportedCodecs",                          true,      0,  true, 0,  0, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0xF0,"AdditionalUpdateType",                     true,      8,  true, 0,  0, s_additionalUpdateType),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x5D,"VoiceDomainPreferenceAndUEsUsageSetting",  true,  3 * 8,  true, decodeVoicePref, encodeVoicePref, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0xD0,"DeviceProperties",                         true,      8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0xE0,"OldGUTIType",                              true,      8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TV,     XmlElem, 0xC0,"MSNetworkFeatureSupport",                  true,      8,  true, 0,  0, 0),
    MAKE_IE_PARAM(TLV,    XmlElem, 0x10,"TMSIBasedNRIContainer",                    true,  4 * 8,  true, 0,  0, 0),
    MAKE_IE_PARAM(NoType, Skip, 0, "", 0, 0, 0, 0, 0, 0),
};

static const RL3Message s_epsMmMsgs[] = {
    {0x41,    "AttachRequest",     s_epsAttachRequestParams},
    {0xff,    "",                  0},
};

static const IEParam s_epsMmMessage[] = {
    MAKE_IE_PARAM(V,      XmlElem, 0, "SecurityHeader", false, 4,     false, decodeSecHeader, encodeSecHeader, 0),
    MAKE_IE_PARAM(NoType, Skip,    0, "",               0,     0,     0,     0, 0, 0),
};


static const RL3Message s_protoMsg[] = {
    {GSML3Codec::GCC,        "GCC",     0},
    {GSML3Codec::BCC,        "BCC",     0},
    {GSML3Codec::EPS_SM,     "EPS_SM",  s_epsSmMessage},
    {GSML3Codec::CC,         "CC",      0},
    {GSML3Codec::GTTP,       "GTTP",    0},
    {GSML3Codec::MM,         "MM",      s_mmMessage},
    {GSML3Codec::RRM,        "RRM",     0},
    {GSML3Codec::EPS_MM,     "EPS_MM",  s_epsMmMessage},
    {GSML3Codec::GPRS_MM,    "GPRS_MM", 0},
    {GSML3Codec::SMS,        "SMS",     0},
    {GSML3Codec::GPRS_SM,    "GPRS_SM", 0},
    {GSML3Codec::SS,         "SS",      0},
    {GSML3Codec::LCS,        "LCS",     0},
    {GSML3Codec::Extension,  "EXT",     0},
    {GSML3Codec::Test,       "TEST",    0},
    {GSML3Codec::Unknown,    "",        0},
};

static const IEParam s_rl3Message[] = {
    MAKE_IE_PARAM(V,       XmlElem, 0, "PD", false, 4, true, decodePD, encodePD, s_protoMsg),
    MAKE_IE_PARAM(NoType,  Skip,    0,  "",  0,     0, 0,    0,        0,        0),
};

static unsigned int checkIntegrity(const GSML3Codec* codec, const String& mac, uint8_t seq,  const uint8_t*& in,
	unsigned int& len, const NamedList& params)
{
    // TODO
    return GSML3Codec::NoError;
}

static unsigned int decipherNASPdu(const GSML3Codec* codec, const String& mac, uint8_t seq,  const uint8_t*& in,
	unsigned int& len, const NamedList& params)
{
    // TODO
    return GSML3Codec::NoError;
}

static unsigned int  decodeSecHeader(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
    if (!(codec && in && len && param && out))
	return GSML3Codec::ParserErr;
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
		if (msg->params)
		    ok = decodeParams(codec,proto,in,len,xml,msg->params,params);
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
    //TODO
    return GSML3Codec::NoError;
}

static unsigned int decodeRL3Msg(const GSML3Codec* codec, uint8_t proto, const IEParam* param, const uint8_t*& in,
	unsigned int& len, XmlElement*& out, const NamedList& params)
{
        DDebug(codec->dbg(),DebugAll,"decodeRL3Msg(param=%s(%p),in=%p,len=%u,out=%p [%p]",param->name.c_str(),param,
	    in,len,out,codec->ptr());
    if (!(codec && in && len && param))
	return GSML3Codec::ParserErr;
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
	return GSML3Codec::ParserErr;
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
	    uint16_t l = getLE(in,len);
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
    if (!(codec))
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
	advanceBuffer(dumpOctets,in,len);
    }
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
	if (dumpStr)
	    xml->setText(dumpStr);
    }
    else
	return dumpUnknownIE(codec,proto,in,len,out);

    return GSML3Codec::NoError;
}

static unsigned int decodeV(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out,
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && len && param))
	return GSML3Codec::ParserErr;
    if (len * 8 < param->length)
	return GSML3Codec::MsgTooShort;
    DDebug(codec->dbg(),DebugAll,"decodeV(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	    if (!(param->decoder || (param->name && param->length <= 8)))
		return dumpParamValue(codec,proto,in,len,param,out);
	    if (param->decoder)
		return param->decoder(codec,proto,param,in,len,out,params);
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
		const TokenDict* dict = static_cast<const TokenDict*>(param->data);
		if (!dict)
		    xml->setText(String(val));
		else
		    xml->setText(lookup(val,dict,String(val)));
		return GSML3Codec::NoError;
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
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodeLV_LVE(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	{
	    if (!param->decoder)
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

	    if (param->decoder) {
		const uint8_t* buf = in + advBytes;
		advanceBuffer(l + advBytes,in,len);
		return param->decoder(codec,proto,param,buf,l,out,params);
	    }
	    break;
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
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodeTV(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    if (param->length && (len * 8 < param->length))
	return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);
    if (param->type == GSML3Codec::TV && param->length == 8) {
	if ((param->iei & (*in & 0xf0)) != param->iei)
	    return GSML3Codec::MismatchedIEI;
    }
    else if ((~param->iei) & *in)
	return GSML3Codec::MismatchedIEI;

    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	{
	    if (!(param->decoder || (param->name && param->length <= 8)))
		return dumpParamValue(codec,proto,in,len,param,out);

	    if (param->decoder) {
		uint8_t skip = (param->length == 8 ? 0u : 1u);
		const uint8_t* buf = in + skip;
		unsigned int l = param->length / 8 - skip;
		advanceBuffer(l + skip,in,len);
		return param->decoder(codec,proto,param,buf,l,out,params);
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
		const TokenDict* dict = static_cast<const TokenDict*>(param->data);
		if (!dict)
		    xml->setText(String(val));
		else
		    xml->setText(lookup(val,dict,String(val)));
		return GSML3Codec::NoError;
	    }
	    break;
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
	return GSML3Codec::ParserErr;
    DDebug(codec->dbg(),DebugAll,"decodeTLV_TLVE(in=%p,len=%u,out=%p,param=%s[%p]) [%p]",in,len,out,param->name.c_str(),param,codec->ptr());
    bool ext = (param->type == GSML3Codec::TLVE);
    if (len < (ext ? 3 : 2))
	return GSML3Codec::MsgTooShort;
    if ((~param->iei) & *in)
	return GSML3Codec::MismatchedIEI;

    switch (param->xmlType) {
	case GSML3Codec::Skip:
	    return skipParam(codec,proto,in,len,param);
	case GSML3Codec::XmlElem:
	{
	    if (!param->decoder)
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
		return (param->isOptional ? GSML3Codec::IncorrectOptionalIE : GSML3Codec::IncorrectMandatoryIE);

	    if (param->decoder) {
		const uint8_t* buf = in + advBytes;
		advanceBuffer(l + advBytes,in,len);
		return param->decoder(codec,proto,param,buf,l,out,params);
	    }
	    break;
	}
	default:
	    return GSML3Codec::ParserErr;
    }
    return GSML3Codec::NoError;
}

static unsigned int decodeParams(const GSML3Codec* codec, uint8_t proto, const uint8_t*& in, unsigned int& len, XmlElement*& out, 
	const IEParam* param, const NamedList& params)
{
    if (!(codec && in && len > 2 && param))
	return GSML3Codec::ParserErr;
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
	XDebug(codec->dbg(),DebugAll,"Decoding parameter %s finished with status=%s [%p]",param->name.c_str(),
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
    {"MismatchedIEI",           GSML3Codec::MismatchedIEI},
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
    return decodeParams(this,GSML3Codec::Unknown,buff,l,out,s_rl3Message);
}

unsigned int GSML3Codec::encode(const XmlElement* in, DataBlock& out, const NamedList& params)
{
    //TODO
    return NoError;
}

unsigned int GSML3Codec::decode(XmlElement* xml, const NamedList& params)
{
    const String& pduMark = params[s_pduDecode];
    if (!(xml && pduMark))
	return MissingParam;
    return decodeXml(xml,params,pduMark);
}

unsigned int GSML3Codec::encode(XmlElement* xml, const NamedList& params)
{
    //TODO
    return NoError;
}


unsigned int GSML3Codec::decodeXml(XmlElement* xml, const NamedList& params, const String& pduTag)
{
#ifdef DEBUG
    Debugger d(DebugAll,"decodeXml()"," xml=%s (%p) pduTag=%s",xml ? xml->tag() : "",xml,pduTag.c_str());
#endif
    unsigned int status = NoError;
    if (xml->getTag() == pduTag) {
	const String& txt = xml->getText();
	if (txt && xml->hasAttribute(YSTRING("enc"),YSTRING("hex"))) {
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

void GSML3Codec::setCodecDebug(DebugEnabler* enabler, void* ptr)
{
    m_dbg = enabler ? enabler : m_dbg;
    m_ptr = ptr ? ptr : (void*)this;
}
