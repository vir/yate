/**
 * sccp.cpp
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

#include<stdlib.h>
#include <string.h>


using namespace TelEngine;

#define MAX_MANDATORY_PARAMS 16

// 227 is maximum data length that can be transported by a UDT message
// with 2 full gt present both numbers with 16 digits (bcd encoded)
#define MAX_UDT_LEN 227

#define MAX_INFO_TIMER 1200000 // Maximum interval for sending SST 20 min
// Maximum length of optional parameters: 6 Segmentation, 3 Importance, 1 EOP
#define MAX_OPT_LEN 10
// Minimum data size in a SCCP message
#define MIN_DATA_SIZE 2

#define MAX_DATA_ITU 3952
#define MAX_DATA_ANSI 3904

static const char* s_userMutexName = "SCCPUserTransport";
static const char* s_sccpMutexName = "SCCPUserList";
static const char* s_managementMutexName = "SCCPManagement";
static const char* s_sccpTranslatorMutex = "SCCPTranslator";
static const char* s_sccpSubsystems = "SccpSubsystems";
static const char* s_sccpRemote = "SccpRemote";

struct SCCPParam {
    // numeric type of the parameter
    SS7MsgSCCP::Parameters type;
    // size in octets, zero for variable
    unsigned char size;
    // SS7 name of the parameter
    const char* name;
    // decoder callback function
    bool (*decoder)(const SS7SCCP*,NamedList&,const SCCPParam*,
	const unsigned char*,unsigned int,const String&);
    // encoder callback function
    unsigned char (*encoder)(const SS7SCCP*,SS7MSU&,unsigned char*,
	const SCCPParam*,const NamedString*,const NamedList*,const String&);
    // table data to be used by the callback
    const void* data;
};

struct MsgParams {
    // type of the message described
    SS7MsgSCCP::Type type;
    // does the message support optional part?
    bool optional;
    // parameters, fixed then variable, separated/terminated by EndOfParameters
    //  using an array is a (moderate) waste of space
    const SS7MsgSCCP::Parameters params[MAX_MANDATORY_PARAMS];
};

static const TokenDict s_return_cause[] = {
    { "No translation for an address of such nature",                   SS7SCCP::NoTranslationAddressNature },
    { "No translation for this specific address",                       SS7SCCP::NoTranslationSpecificAddress },
    { "Subsystem congestion",                                           SS7SCCP::SubsystemCongestion },
    { "Subsystem failure",                                              SS7SCCP::SubsystemFailure },
    { "Unequipped user",                                                SS7SCCP::UnequippedUser },
    { "MTP failure",                                                    SS7SCCP::MtpFailure },
    { "Network Congestion",                                             SS7SCCP::NetworkCongestion },
    { "Unqualified",                                                    SS7SCCP::Unqualified },
    { "Error in message transport",                                     SS7SCCP::ErrorInMessageTransport },
    { "Error in local processing",                                      SS7SCCP::ErrorInLocalProcessing },
    { "Destination can not perform reassembly",                         SS7SCCP::DestinationCanNotPerformReassembly },
    { "SCCP failure",                                                   SS7SCCP::SccpFailure },
    { "Hop counter violation",                                          SS7SCCP::HopCounterViolation },
    { "Segmentation not supported",                                     SS7SCCP::SegmentationNotSupported },
    { "Segmentation failure",                                           SS7SCCP::SegmentationFailure },
    // ANSI only
    { "Message change failure",                                         SS7SCCP::MessageChangeFailure },
    { "Invalid INS routing request",                                    SS7SCCP::InvalidINSRoutingRequest },
    { "Invalid ISNI routing request",                                   SS7SCCP::InvalidISNIRoutingRequest },
    { "Unauthorized message",                                           SS7SCCP::UnauthorizedMessage },
    { "Message incompatibility",                                        SS7SCCP::MessageIncompatibility },
    { "Can not perform ISNI constrained routing",                       SS7SCCP::NotSupportedISNIRouting },
    { "Redundant ISNI constrained routing information",                 SS7SCCP::RedundantISNIConstrainedRouting },
    { "Unable to perform ISNI identification",                          SS7SCCP::ISNIIdentificationFailed },
    { 0, 0 }
};

static const TokenDict s_managementMessages[] = {
    { "SSA",    SCCPManagement::SSA },  // Subsystem-allowed
    { "SSP",    SCCPManagement::SSP },  // Subsystem-prohibited
    { "SST",    SCCPManagement::SST },  // Subsystem-status-test
    { "SOR",    SCCPManagement::SOR },  // Subsystem-out-of-service-request
    { "SOG",    SCCPManagement::SOG },  // Subsystem-out-of-service-grant
    { "SSC",    SCCPManagement::SSC },  // SCCP/Subsystem-congested      (ITU  only)
    { "SBR",    SCCPManagement::SBR },  // Subsystem-backup-routing      (ANSI only)
    { "SNR",    SCCPManagement::SNR },  // Subsystem-normal-routing      (ANSI only)
    { "SRT",    SCCPManagement::SRT },  // Subsystem-routing-status-test (ANSI only)
    { 0, 0 }
};

static const TokenDict s_dict_control[] = {
    { "status",                      SS7SCCP::Status },
    { "full-status",                 SS7SCCP::FullStatus },
    { "enable-extended-monitoring",  SS7SCCP::EnableExtendedMonitoring },
    { "disable-extended-monitoring", SS7SCCP::DisableExtendedMonitoring },
    { "enable-print-messages",       SS7SCCP::EnablePrintMsg },
    { "disable-print-messages",      SS7SCCP::DisablePrintMsg },
    { 0, 0 }
};

const TokenDict SCCPManagement::s_broadcastType[] = {
    { "UserOutOfService",            SCCPManagement::UserOutOfService },
    { "UserInService",               SCCPManagement::UserInService },
    { "SignallingPointInaccessible", SCCPManagement::PCInaccessible },
    { "SignallingPointAccessible",   SCCPManagement::PCAccessible },
    { "RemoteSCCPInaccessible",      SCCPManagement::SccpRemoteInaccessible },
    { "RemoteSCCPAccessible",        SCCPManagement::SccpRemoteAccessible },
    { "SignallingPointCongested",    SCCPManagement::PCCongested },
};

static const TokenDict s_sccpNotif[] = {
    { "Coordinate Request",            SCCP::CoordinateRequest },          // (User->SCCP)
    { "Coordinate Confirm",            SCCP::CoordinateConfirm },          // (SCCP->User)
    { "Coordinate Indication",         SCCP::CoordinateIndication },       // (SCCP->User)
    { "Coordinate Response",           SCCP::CoordinateResponse },         // (User->SCCP)
    { "Status Indication",             SCCP::StatusIndication },           // (SCCP->User)
    { "Status Request",                SCCP::StatusRequest },              // (User->SCCP)
    { "PointCode Status Indication",   SCCP::PointCodeStatusIndication },  // (SCCP->User)
    { "Trafic Indication",             SCCP::TraficIndication },
    { "Subsystem Status",              SCCP::SubsystemStatus },            // (SCCP->User)
    { 0, 0 }
};

static const TokenDict s_numberingPlan[] = {
    { "unknown",          0x00 },
    { "isdn",             0x01 },
    { "e164",             0x01 },
    { "generic",          0x02 },
    { "data",             0x03 },
    { "x121",             0x03 },
    { "telex",            0x04 },
    { "maritime-mobile",  0x05 },
    { "e210",             0x05 },
    { "e211",             0x05 },
    { "land-mobile",      0x06 },
    { "e212",             0x06 },
    { "isdn-mobile",      0x07 },
    { "e214",             0x07 },
    { "network-specific", 0x0e },
    { 0, 0 }
};

static const TokenDict s_nai[] = {
    { "unknown",                     0x00 },
    { "subscriber",                  0x01 },
    { "national-reserved",           0x02 },
    { "national-significant",        0x03 },
    { "international",               0x04 },
    { 0, 0 }
};

static const TokenDict s_encodingScheme[] = {
    { "unknown",     0x00 },
    { "bcd",         0x01 },
    { "bcd",         0x02 },
    { 0, 0 }
};

static const TokenDict s_ansiSmi[] = {
    { "unknown",       0x00 },
    { "solitary",      0x01 },
    { "duplicated",    0x02 },
    { 0 , 0 }
};

const TokenDict SCCPManagement::s_states[] = {
    { "allowed",        SCCPManagement::Allowed },
    { "prohibited",     SCCPManagement::Prohibited },
    { "wait-for-grant", SCCPManagement::WaitForGrant },
    { "ignore-tests",   SCCPManagement::IgnoreTests },
    { "unknown",        SCCPManagement::Unknown },
    { 0 , 0 }
};

const TokenDict s_messageReturn[] = {
    { "false", 0x00 },
    { "true",  0x08 },
    { "yes",   0x08 },
    { "on",    0x08 },
    { "enable",0x08 },
    { 0, 0 }
};

static bool compareLabel(const SS7Label& l1, const SS7Label& l2)
{
    if (l1.opc() != l2.opc())
	return false;
    if (l1.dpc() != l2.dpc())
	return false;
    return true;
}

// Helper method increments a number stored in a string
static void incrementNS(NamedString* ns)
{
    if (!ns)
	return;
    int counter = ns->toInteger();
    counter++;
    *ns = String(counter);
}

static bool compareNamedList(const NamedList& nl1, const NamedList& nl2)
{
    if (nl1.length() != nl2.length())
	return false;
    NamedIterator iter(nl1);
    while (const NamedString* pr = iter.get()) {
	const NamedString* pr2 = nl2.getParam(pr->name());
	if (!pr2 || (*pr2 != *pr))
	    return false;
    }
    return true;
}

static void getDictValue(NamedList& list, const char* paramName, int val, const TokenDict* dict)
{
    NamedString* ns = new NamedString(paramName);
    *ns = lookup(val,dict,0);
    if (ns->null()) {
	*ns = String(val).c_str();
    }
    list.setParam(ns);
}

static bool decodeRaw(const SS7SCCP* sccp, NamedList& list, const SCCPParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    String raw;
    raw.hexify((void*)buf,len,' ');
    DDebug(sccp,DebugInfo,"decodeRaw decoded %s=%s",param->name,raw.c_str());
    list.addParam(prefix+param->name,raw);
    return true;
}

// Raw decoder for unknown/failed parameter, dumps raw octets
static bool decodeRawParam(const SS7SCCP* sccp, NamedList& list, unsigned char value,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    String name("Param_");
    name << value;
    SCCPParam p;
    p.type = (SS7MsgSCCP::Parameters)value;
    p.size = len;
    p.name = name;
    p.decoder = 0;
    p.encoder = 0;
    p.data = 0;
    return decodeRaw(sccp,list,&p,buf,len,prefix);
};

static bool decodeInt(const SS7SCCP* sccp, NamedList& list, const SCCPParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    unsigned int val = 0;
    int shift = 0;
    while (len--) {
	val |= ((unsigned int)*buf++) << shift;
	shift += 8;
    }
    DDebug(sccp,DebugAll,"decodeInt decoded %s=%s (%u)",param->name,lookup(val,(const TokenDict*)param->data),val);
    SignallingUtils::addKeyword(list,prefix+param->name,(const TokenDict*)param->data,val);
    return true;
}

static bool decodeProtocolClass(const SS7SCCP* sccp, NamedList& list, const SCCPParam* param,
    const unsigned char* buffer, unsigned int len, const String& prefix)
{
    unsigned char protocol = *buffer++;
    unsigned int pClass = protocol & 0x0f;
    if (pClass > 3) {
	Debug(sccp,DebugWarn,"Received Invalid Protocol Class %d",pClass);
	return false;
    }
    if (pClass < 2) // Protocol class 0 | 1 check return option
	getDictValue(list,prefix + "MessageReturn",protocol >> 4,s_messageReturn);
    list.setParam(prefix + param->name, String(pClass));
    return true;
}

static bool decodeCause(const SS7SCCP* sccp, NamedList& list, const SCCPParam* param,
    const unsigned char* buffer, unsigned int len, const String& prefix)
{
    if (len <  1)
	return false;
    unsigned char cause = *buffer++;
    list.setParam(prefix + param->name,String(cause));
    return true;
}

static bool decodeImportance(const SS7SCCP* sccp, NamedList& list, const SCCPParam* param,
    const unsigned char* buffer, unsigned int len, const String& prefix)
{
    if (len < 1)
	return false;
    int importance = *buffer++ & 0x07;
    list.setParam(prefix + "Importance",String(importance));
    return true;
}

static void getDigits(String& num, bool oddNum, const unsigned char* buf, unsigned int len,
    bool ignoreUnk)
{
    static const char digits1[] = "0123456789\0BC\0\0.";
    static const char digits2[] = "0123456789ABCDE.";
    const char* digits = ignoreUnk ? digits1 : digits2;
    for (unsigned int i = 0; i < len; i++) {
	num += digits[buf[i] & 0x0f];
	if (oddNum && ((i+1) == len))
	    break;
	num += digits[buf[i] >> 4];
    }
}

// Decode methods
static bool decodeItuAddress(const SS7SCCP* sccp, NamedList& params,const SCCPParam* param,
	const unsigned char* buffer, unsigned int length, const String& prefix)
{
    unsigned char addressIndicator = *buffer++;
    length--;
    String prName = prefix + param->name;
    while (true) {
	if ((addressIndicator & 0x01) == 0x01) { // Have Pointcode
	    if (length < 2)
		break;
	    int pointcode = *buffer++;
	    pointcode |= (*buffer++ & 0x3f) << 8;
	    params.addParam(prName + ".pointcode",String(pointcode));
	    length -= 2;
	}
	if ((addressIndicator & 0x02) == 0x02) { // Have SSN
	    if (length < 1)
		break;
	    unsigned char ssn = *buffer++;
	    params.addParam(prName + ".ssn",String(ssn));
	    length --;
	}
	params.addParam(prName + ".route", ((addressIndicator & 0x40) == 0x40)
		? YSTRING("ssn") : YSTRING("gt"));
	unsigned char gti = (addressIndicator >> 2) & 0x0f;
	if (!gti) // No Global Title Present
	    return true;
	bool odd = false;
	String tmp;
	String gtName = prName + ".gt";
	if (gti == 0x01) { // GT includes Nature Of Address Indicator
	    if (length < 1) {
		break;
	    }
	    unsigned char nai = *buffer++;
	    length--;
	    getDictValue(params,gtName + ".nature", nai & 0x7f,s_nai);
	    odd = (nai & 0x80) != 0;
	} else if (gti == 0x02) { // GT includes Translation Type
	    if (length < 1)
		break;
	    params.addParam(gtName + ".translation", String((int)*buffer++));
	    length--;
	    tmp.hexify((void*)buffer,length,' ');
	} else if (gti == 0x03) { // GT includes tt, np & es
	    if (length < 2)
		break;
	    params.addParam(gtName+ ".translation", String((int)*buffer++));
	    length--;
	    unsigned char npes = *buffer++;
	    length--;
	    getDictValue(params,gtName + ".plan", npes >> 4,s_numberingPlan);
	    unsigned int es = npes & 0x0f;
	    getDictValue(params,gtName + ".encoding", es,s_encodingScheme);
	    switch (es) {
		case 1:
		    odd = true;
		case 2:
		    break;
		default:
		    tmp.hexify((void*)buffer,length,' ');
	    }
	} else if (gti == 0x04) { // GT includes tt, np, es & nai
	    if (length < 3)
		break;
	    params.addParam(gtName+ ".translation", String((int)*buffer++));
	    length--;
	    unsigned char npes = *buffer++;
	    unsigned char es = npes & 0x0f;
	    length--;
	    getDictValue(params,gtName + ".plan", npes >> 4,s_numberingPlan);
	    getDictValue(params,gtName + ".encoding", es,s_encodingScheme);
	    getDictValue(params,gtName + ".nature", *buffer++ & 0x7f,s_nai);
	    length--;
	    switch (es) {
		case 1:
		    odd = true;
		case 2:
		    break;
		default:
		    tmp.hexify((void*)buffer,length,' ');
	    }
	} else {
	    Debug(sccp,DebugMild, "Unable to decode ITU GT with GTI = %d",gti);
	    return false;
	}
	if (tmp.null())
	    getDigits(tmp,odd,buffer,length,sccp && sccp->ignoreUnknownAddrSignals());
	params.addParam(gtName, tmp);
	return true;
    }
    Debug(sccp,DebugWarn,"Failed to decode ITU address!!! short message length");
    return false;
}

static bool decodeAnsiAddress(const SS7SCCP* sccp, NamedList& params,const SCCPParam* param,
	const unsigned char* buffer, unsigned int length, const String& prefix)
{
    unsigned char addressIndicator = *buffer++;
    length--;
    String prName = prefix + param->name;
    while (true) {
	if ((addressIndicator & 0x01) == 0x01) { // Have SSN
	    if (length < 1)
		break;
	    params.addParam(prName + ".ssn",String(*buffer++));
	    length --;
	}
	if ((addressIndicator & 0x02) == 0x02) { // Have Pointcode
	    if (length < 3)
		break;
	    unsigned int pointcode = *buffer++;
	    pointcode |= (*buffer++ << 8);
	    pointcode |= (*buffer++ << 16);
	    length -= 3;
	    params.addParam(prName + ".pointcode",String(pointcode));
	}
	params.addParam(prName + ".route", ((addressIndicator & 0x40) == 0x40) ?
		    YSTRING("ssn") : YSTRING("gt"));
	unsigned char gti = (addressIndicator >> 2) & 0x0f;
	if (!gti) // No Global Title Present
	    return true;
	bool odd = false;
	String tmp;
	String gtName = prName + ".gt";
	if (gti == 0x01) { // GT includes tt, np & es
	    if (length < 2)
		break;
	    params.addParam(gtName + ".translation", String((int)*buffer++));
	    length--;
	    unsigned char npes = *buffer++;
	    unsigned char es = npes & 0x0f;
	    length--;
	    getDictValue(params,gtName + ".plan", npes >> 4, s_numberingPlan);
	    getDictValue(params,gtName + ".encoding", es,s_encodingScheme);
	    switch (es) {
		case 1:
		    odd = true;
		case 2:
		    break;
		default:
		    tmp.hexify((void*)buffer,length,' ');
	    }
	} else if (gti == 0x02) { // GT includes Translation Type
	    if (length < 1)
		break;
	    params.addParam(gtName + ".translation", String((int)*buffer++));
	    length--;
	    tmp.hexify((void*)buffer,length,' ');
	} else {
	    Debug(sccp,DebugMild, "Unable to decode ANSI GT with GTI = %d",gti);
	    return false;
	}
	if (tmp.null())
	    getDigits(tmp,odd,buffer,length,sccp && sccp->ignoreUnknownAddrSignals());
	params.addParam(gtName, tmp);
	return true;
    }
    Debug(sccp,DebugWarn,"Failed to decode ANSI address!!! short message length");
    return false;
}

static bool decodeAddress(const SS7SCCP* sccp, NamedList& paramsList,const SCCPParam* param,
	const unsigned char* buffer, unsigned int length, const String& prefix)
{
    if (length < 1)
	return false;
    if (sccp->ITU())
	return decodeItuAddress(sccp,paramsList,param,buffer,length,prefix);
    else
	return decodeAnsiAddress(sccp,paramsList,param,buffer,length,prefix);
}

static bool decodeData(const SS7SCCP* sccp, SS7MsgSCCP* msg, const unsigned char* buffer, unsigned int length)
{
    DataBlock* data = new DataBlock((void*)buffer,length, false);
    msg->setData(data);
    buffer += length;
    return true;
}

static bool decodeSegmentation(const SS7SCCP* sccp, NamedList& params,const SCCPParam* param,
	const unsigned char* buffer, unsigned int length, const String& prefix)
{
    if (length < 4) {
	DDebug(sccp,DebugNote,"Failed to decode %s parameter! Reason length to short.",param->name);
	return false;
    }
    unsigned char segInfo = *buffer++;
    String prName = prefix + param->name;
    params.addParam(prName + ".FirstSegment", String((segInfo & 0x80) == 0x80));
    params.addParam(prName + ".ProtocolClass", String((segInfo & 0x40) >> 6));
    params.addParam(prName + ".RemainingSegments", String(segInfo & 0x0f));
    unsigned int segLocalReference = *buffer++;
    segLocalReference |= *buffer++ << 8;
    segLocalReference |= *buffer++ << 16;
    params.addParam(prName + ".SegmentationLocalReference", String(segLocalReference));
    params.addParam(prName ,"true");
    return true;
}


// Encode methods

static unsigned char encodeRaw(const SS7SCCP* sccp, SS7MSU& msu,
    unsigned char* buf, const SCCPParam* param, const NamedString* val,
    const NamedList*, const String&)
{
    if (!(param && val))
	return 0;
    DDebug(sccp,DebugInfo,"encodeRaw encoding %s=%s",param->name,val->c_str());
    DataBlock raw;
    if (!raw.unHexify(val->c_str(),val->length(),' ')) {
	DDebug(sccp,DebugMild,"encodeRaw failed: invalid string");
	return 0;
    }
    if (!raw.length() || raw.length() > 254 ||
	(param->size && param->size != raw.length())) {
	DDebug(sccp,DebugMild,"encodeRaw failed: param size=%u data length=%u",
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

// Encoder for fixed length little-endian integer values
static unsigned char encodeInt(const SS7SCCP* sccp, SS7MSU& msu, unsigned char* buf,
    const SCCPParam* param, const NamedString* val, const NamedList*, const String&)
{
    if (!param)
	return 0;
    unsigned int n = param->size;
    if (!n)
	return 0;
    unsigned int v = 0;
    if (val)
	v = val->toInteger((const TokenDict*)param->data);
    DDebug(sccp,DebugAll,"encodeInt encoding %s=%u on %u octets",param->name,v,n);
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

static unsigned char encodeProtocolClass(const SS7SCCP* sccp, SS7MSU& msu,
    unsigned char* buf, const SCCPParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!buf) {
	Debug(sccp,DebugWarn,"Request to encode ProtocolClass in a null buffer!!!");
	return 0;
    }
    unsigned char protocolClass = extra->getIntValue(prefix + "ProtocolClass");
    if (protocolClass > 3) {
	Debug(sccp,DebugWarn,"Invalid ProtocolClass value %d, for encoding",protocolClass);
	return 0;
    }
    if (protocolClass < 2) {
	int errorReturn = extra->getIntValue(prefix + "MessageReturn",s_messageReturn);
	protocolClass |= errorReturn << 4;
    }
    *buf = protocolClass;
    return 1;
}

static DataBlock* setDigits(const char* val)
{
    if (!val)
	return 0;
    unsigned char buf[32];
    unsigned int len = 0;
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
    if (odd)
	len++;
    DataBlock* tmp = new DataBlock(buf,len);
    return tmp;
}

static unsigned char encodeItuAddress(const SS7SCCP* sccp, SS7MSU& msu,
    unsigned char* buf, const SCCPParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    unsigned char length = 1;
    unsigned char data[32];
    unsigned char addressIndicator = 0;
    String preName(prefix + param->name);
    bool havePC = extra->getParam(preName + ".pointcode") != 0;
    if (havePC) {
	int pointcode = extra->getIntValue(preName + ".pointcode",0);
	addressIndicator |= 0x01;
	data[++length] = pointcode & 0xff;
	data[++length] = (pointcode >> 8) & 0x3f;
    }
    bool haveSSN = extra->getParam(preName + ".ssn") != 0;
    if (haveSSN) {
	int ssn = extra->getIntValue(preName + ".ssn",0);
	addressIndicator |= 0x02;
	data[++length] = ssn;
    }
    NamedString* route = extra->getParam(preName + ".route");
    if (route && *route == YSTRING("ssn")) { // Marck route on SSN
	if (param->name == YSTRING("CalledPartyAddress") && !haveSSN)
	    Debug(sccp,DebugNote,"Request to route on SSN with no ssn present!");
	addressIndicator |= 0x40;
    }
    NamedString* gtNr = YOBJECT(NamedString,extra->getParam(preName + ".gt"));
    if (!gtNr) { // No Global Title present!!!
	if ((addressIndicator & 0x40) == 0)
	    DDebug(sccp,DebugNote,"RouteIndicator set on global title. But no global title present!!!");
	data[1] = addressIndicator;
	data[0] = length;
	DataBlock tmp(data,length + 1,false);
	msu += tmp;
	tmp.clear(false);
	return data[0];
    }
    NamedString* nature = YOBJECT(NamedString,extra->getParam(preName + ".gt.nature"));
    NamedString* translation = YOBJECT(NamedString,extra->getParam(preName + ".gt.translation"));
    NamedString* plan = YOBJECT(NamedString,extra->getParam(preName + ".gt.plan"));
    NamedString* encoding = YOBJECT(NamedString,extra->getParam(preName + ".gt.encoding"));
    bool odd = false;
    DataBlock* digits = 0;
    if (nature && !translation) { // GT = 0x01
	addressIndicator |= 0x04;
	int nai = nature->toInteger(s_nai);
	odd = (gtNr->length() % 2) ? false : true;
	if (!odd)
	    nai |= 0x80;
	data[++length] = nai & 0xff;
    } else if (translation && !(plan && encoding) && !nature) { // GT = 0x02
	addressIndicator |= 0x08;
	int tt = translation->toInteger();
	data[++length] = tt & 0xff;
	digits = new DataBlock();
	if (!digits->unHexify(*gtNr,gtNr->length(),' ')) {
	    Debug(sccp,DebugInfo,"Setting unknown odd/even number of digits!!");
	    TelEngine::destruct(digits);
	}
    } else if (translation && plan && encoding && !nature) { // GT = 0x03
	addressIndicator |= 0x0c;
	int tt = translation->toInteger();
	data[++length] = tt & 0xff;
	int np = plan->toInteger(s_numberingPlan);
	int es = encoding->toInteger(s_encodingScheme);
	switch (es) {
	    case 1:
	    case 2:
		odd = (gtNr->length() % 2 == 1);
		es = odd ? 1 : 2;
		break;
	    default:
		digits = new DataBlock();
		if (!digits->unHexify(*gtNr,gtNr->length(),' ')) {
		    Debug(sccp,DebugInfo,"Setting unknown odd/even number of digits!!");
		    TelEngine::destruct(digits);
		}
	}
	data[++length] = ((np & 0x0f) << 4) | (es & 0x0f);
    } else if (translation && plan && encoding && nature) { // GT = 0x04
	addressIndicator |= 0x10;
	int tt = translation->toInteger();
	data[++length] = tt & 0xff;
	int np = plan->toInteger(s_numberingPlan);
	int es = encoding->toInteger(s_encodingScheme);
	switch (es) {
	    case 1:
	    case 2:
		odd = (gtNr->length() % 2 == 1);
		es = odd ? 1 : 2;
		break;
	    default:
		digits = new DataBlock();
		if (!digits->unHexify(*gtNr,gtNr->length(),' ')) {
		    Debug(sccp,DebugInfo,"Setting unknown odd/even number of digits!!");
		    TelEngine::destruct(digits);
		}
	}
	data[++length] = ((np & 0x0f) << 4) | (es & 0x0f);
	int nai = nature->toInteger(s_nai);
	data[++length] = nai & 0x7f;
    } else {
	Debug(sccp,DebugWarn,"Can not encode ITU GTI. Unknown GTI value for : nai= %s, Plan & Encoding = %s, TranslationType = %s",
	      nature? "present" : "missing",(plan && encoding)? "present" : "missing",translation ? "present" : "missing");
	return 0;
    }
    data[1] = addressIndicator;
    if (!digits && !(digits = setDigits(*gtNr))) {
	Debug(DebugWarn,"Failed to encode digits!!");
	return 0;
    }
    data[0] = length + digits->length();
    DataBlock tmp(data,length + 1,false);
    msu += tmp;
    msu += *digits;
    tmp.clear(false);
    TelEngine::destruct(digits);
    return data[0];

}

static unsigned char encodeAnsiAddress(const SS7SCCP* sccp, SS7MSU& msu,
    unsigned char* buf, const SCCPParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    unsigned char length = 1;
    unsigned char data[32];
    unsigned char addressIndicator = 0;
    String preName(prefix + param->name);
    bool havePC = extra->getParam(preName + ".pointcode") != 0;
    bool haveSSN = extra->getParam(preName + ".ssn") != 0;
    if (haveSSN) {
	int ssn = extra->getIntValue(preName + ".ssn",0);
	addressIndicator |= 0x01;
	data[++length] = ssn;
    }
    addressIndicator |= 0x80; // Mark the 8 bit from address indicator to national use
    if (havePC) {
	int pointcode = extra->getIntValue(preName + ".pointcode",0);
	addressIndicator |= 0x02;
	data[++length] = pointcode  & 0xff;
	data[++length] = (pointcode >> 8) & 0xff;
	data[++length] = (pointcode >> 16) & 0xff;
    }
    NamedString* route = extra->getParam(preName + ".route");
    if (route && *route == YSTRING("ssn")) { // Marck route on SSN
	if (param->name == YSTRING("CalledPartyAddress") && !haveSSN)
	    Debug(sccp,DebugNote,"Request to route on SSN with no ssn present!");
	addressIndicator |= 0x40;
    }
    NamedString* gtNr = YOBJECT(NamedString,extra->getParam(preName + ".gt"));
    if (!gtNr) { // No Global Title present!!!
	if ((addressIndicator & 0x40) == 0)
	    DDebug(sccp,DebugNote,"RouteIndicator set on global title. But no global title present!!!");
	data[1] = addressIndicator;
	data[0] = length & 0xff;
	DataBlock tmp(data,length + 1,false);
	msu += tmp;
	tmp.clear(false);
	return data[0];
    }
    NamedString* translation = YOBJECT(NamedString,extra->getParam(preName + ".gt.translation"));
    NamedString* plan = YOBJECT(NamedString,extra->getParam(preName + ".gt.plan"));
    NamedString* encoding = YOBJECT(NamedString,extra->getParam(preName + ".gt.encoding"));
    DataBlock* digits = 0;
    bool odd = false;
    if (translation && !(plan && encoding)) { // GT = 0x02
	addressIndicator |= 0x08;
	int tt = translation->toInteger();
	data[++length] = tt & 0xff;
	digits = new DataBlock();
	if (!digits->unHexify(*gtNr,gtNr->length(),' ')) {
	    Debug(sccp,DebugInfo,"Setting unknown odd/even number of digits!!");
	    TelEngine::destruct(digits);
	}
    } else if (translation && plan && encoding) { // GT = 0x01
	addressIndicator |= 0x04;
	int tt = translation->toInteger();
	data[++length] = tt & 0xff;
	int np = plan->toInteger(s_numberingPlan);
	int es = encoding->toInteger(s_encodingScheme);
	switch (es) {
	    case 1:
	    case 2:
		odd = (gtNr->length() % 2 == 1);
		es = odd ? 1 : 2;
		break;
	    default:
		digits = new DataBlock();
		if (!digits->unHexify(*gtNr,gtNr->length(),' ')) {
		    Debug(sccp,DebugInfo,"Setting unknown odd/even number of digits!!");
		    TelEngine::destruct(digits);
		}
	}
	data[++length] = ((np & 0x0f) << 4) | (es & 0x0f);
    } else {
	Debug(sccp,DebugWarn,"Can not encode ANSI GTI. Unknown GTI value for : Plan & Encoding = %s, TranslationType = %s",
	      (plan && encoding)? "present" : "missing",translation ? "present" : "missing");
	return 0;
    }
    data[1] = addressIndicator;
    if (!digits && !(digits = setDigits(*gtNr))) {
	Debug(DebugWarn,"Failed to encode digits!!");
	return 0;
    }
    data[0] = length + digits->length();
    DataBlock tmp(data,length + 1,false);
    msu += tmp;
    msu += *digits;
    tmp.clear(false);
    TelEngine::destruct(digits);
    return data[0];

}

static unsigned char encodeAddress(const SS7SCCP* sccp, SS7MSU& msu,
    unsigned char* buf, const SCCPParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    if (!param || buf || param->size)
	return 0;
    if (sccp->ITU())
	return encodeItuAddress(sccp,msu,buf,param,val,extra,prefix);
    else
	return encodeAnsiAddress(sccp,msu,buf,param,val,extra,prefix);
    return 0;
}

static unsigned char encodeSegmentation(const SS7SCCP* sccp, SS7MSU& msu,
    unsigned char* buf, const SCCPParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    String preName(prefix + param->name);
    unsigned char length = 1;
    unsigned char data[6];
    unsigned char segInfo = 0;
    int leftSgm = extra->getIntValue(preName + ".RemainingSegments",0);
    segInfo |= leftSgm & 0x0f;
    int protocolClass = extra->getIntValue(preName + ".ProtocolClass",0);
    if (protocolClass)
	segInfo |= 0x40;
    bool firstSgm = extra->getBoolValue(preName + ".FirstSegment",false);
    if (firstSgm)
	segInfo |= 0x80;
    data[1] = segInfo;
    unsigned int sgmLocalReference = extra->getIntValue(preName + ".SegmentationLocalReference",0);
    data[++length] = sgmLocalReference & 0xff;
    data[++length] = sgmLocalReference >> 8 & 0xff;
    data[++length] = sgmLocalReference >> 16 & 0xff;

    data[0] = length & 0xff;
    DataBlock tmp(data,length + 1,false);
    msu += tmp;
    tmp.clear(false);
    return data[0];
}

static unsigned char encodeImportance(const SS7SCCP* sccp, SS7MSU& msu,
    unsigned char* buf, const SCCPParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    unsigned char data[6];
    data[0] = 1;
    int importance = extra->getIntValue(prefix + param->name);
    data[1] = importance & 0x07;
    DataBlock tmp(data,2,false);
    msu += tmp;
    tmp.clear(false);
    return data[0];
}

static unsigned int encodeData(const SS7SCCP* sccp, SS7MSU& msu, SS7MsgSCCP* msg)
{
    if (!msg) {
	DDebug(sccp,DebugNote,"Request to encode data for a null message");
	return 0;
    }
    DataBlock* data = msg->getData();
    if (!data) {
	DDebug(sccp,DebugNote,"Request to encode message %s with null data",
	    SS7MsgSCCP::lookup(msg->type()));
	return 0;
    }
    if (data->length() < 2) {
	DDebug(sccp,DebugNote,"Request to encode message %s with short data",
	    SS7MsgSCCP::lookup(msg->type()));
	return 0;
    }
    unsigned int length = data->length();
    unsigned char header[2];
    DataBlock tmp;
    if (msg->isLongDataMessage()) {
	header[0] = length & 0xff;
	header[1] = length >> 8 & 0xff;
	tmp.assign(header,2,false);
    } else {
	header[0] = length & 0xff;
	tmp.assign(header,1,false);
    }
    msu += tmp;
    msu += *data;
    tmp.clear(false);
    return length;
}

static unsigned char encodeCause(const SS7SCCP* sccp, SS7MSU& msu, unsigned char* buf,
    const SCCPParam* param, const NamedString* val, const NamedList*, const String&)
{
    if (!param)
	return 0;
    unsigned int n = param->size;
    if (!n)
	return 0;
    unsigned int v = 0;
    if (val)
	v = val->toInteger();
    DDebug(sccp,DebugAll,"encodeCause encoding %s=%u on %u octets",param->name,v,n);
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

#define MAKE_NAME(x) { #x, SS7MsgSCCP::x }
static const TokenDict s_names[] = {
    // this list must be kept in synch with the header
    MAKE_NAME(CR),
    MAKE_NAME(CC),
    MAKE_NAME(CREF),
    MAKE_NAME(RLSD),
    MAKE_NAME(RLC),
    MAKE_NAME(DT1),
    MAKE_NAME(DT2),
    MAKE_NAME(AK),
    MAKE_NAME(UDT),
    MAKE_NAME(UDTS),
    MAKE_NAME(ED),
    MAKE_NAME(EA),
    MAKE_NAME(RSR),
    MAKE_NAME(RSC),
    MAKE_NAME(ERR),
    MAKE_NAME(IT),
    MAKE_NAME(XUDT),
    MAKE_NAME(XUDTS),
    MAKE_NAME(LUDT),
    MAKE_NAME(LUDTS),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_PARAM(p,s,a,d,t) { SS7MsgSCCP::p,s,#p,a,d,t }
static const SCCPParam s_paramDefs[] = {
//             name                           len decoder            encoder            table                  References

    // Standard parameters
    MAKE_PARAM(DestinationLocalReference,      3,decodeInt,          encodeInt,          0),                    // ITU:Q.713 3.2  | Ansi: 1000112.3 3.2
    MAKE_PARAM(SourceLocalReference,           3,decodeInt,          encodeInt,          0),                    // ITU:Q.713 3.3  | Ansi: 1000112.3 3.3
    MAKE_PARAM(CalledPartyAddress,             0,decodeAddress,      encodeAddress,      0),                    // ITU:Q.713 3.4  | Ansi: 1000112.3 3.4
    MAKE_PARAM(CallingPartyAddress,            0,decodeAddress,      encodeAddress,      0),                    // ITU:Q.713 3.5  | Ansi: 1000112.3 3.5
    MAKE_PARAM(ProtocolClass,                  1,decodeProtocolClass,encodeProtocolClass,0),                    // ITU:Q.713 3.6  | Ansi: 1000112.3 3.6
    MAKE_PARAM(Segmenting,                     0,0,                  0,                  0),                    // ITU:Q.713 3.7  | Ansi: 1000112.3 3.7
    MAKE_PARAM(ReceiveSequenceNumber,          0,0,                  0,                  0),                    // ITU:Q.713 3.8  | Ansi: 1000112.3 3.8
    MAKE_PARAM(Sequencing,                     0,0,                  0,                  0),                    // ITU:Q.713 3.9  | Ansi: 1000112.3 3.9
    MAKE_PARAM(Credit,                         0,0,                  0,                  0),                    // ITU:Q.713 3.10 | Ansi: 1000112.3 3.10
    MAKE_PARAM(ReleaseCause,                   1,decodeCause,        encodeCause,        0),                    // ITU:Q.713 3.11 | Ansi: 1000112.3 3.11
    MAKE_PARAM(ReturnCause,                    1,decodeCause,        encodeCause,        0),                    // ITU:Q.713 3.12 | Ansi: 1000112.3 3.12
    MAKE_PARAM(ResetCause,                     1,decodeCause,        encodeCause,        0),                    // ITU:Q.713 3.13 | Ansi: 1000112.3 3.13
    MAKE_PARAM(ErrorCause,                     1,decodeCause,        encodeCause,        0),                    // ITU:Q.713 3.14 | Ansi: 1000112.3 3.14
    MAKE_PARAM(RefusalCause,                   1,decodeCause,        encodeCause,        0),                    // ITU:Q.713 3.15 | Ansi: 1000112.3 3.15
    MAKE_PARAM(Data,                           0,0,                  0,                  0),                    // ITU:Q.713 3.16 | Ansi: 1000112.3 3.16
    MAKE_PARAM(Segmentation,                   4,decodeSegmentation, encodeSegmentation, 0),                    // ITU:Q.713 3.17 | Ansi: 1000112.3 3.18
    MAKE_PARAM(HopCounter,                     1,decodeInt,          encodeInt,          0),                    // ITU:Q.713 3.18 | Ansi: 1000112.3 3.17
    MAKE_PARAM(Importance,                     0,decodeImportance,   encodeImportance,   0),                    // ITU:Q.713 3.19
    MAKE_PARAM(LongData,                       0,0,                  0,                  0),                    // ITU:Q.713 3.20 | Ansi: 1000112.3 3.20
    MAKE_PARAM(MessageTypeInterworking,        0,0,                  0,                  0),                    // Ansi: 1000112.3 3.22
    MAKE_PARAM(INS,                            0,0,                  0,                  0),                    // Ansi: 1000112.3 3.21
    MAKE_PARAM(ISNI,                           0,0,                  0,                  0),                    // Ansi: 1000112.3 3.19
    { SS7MsgSCCP::EndOfParameters, 0, 0, 0, 0, 0 }
};
#undef MAKE_PARAM

// Descriptor of SCCP message
static const MsgParams s_common_params[] = {
    { SS7MsgSCCP::CR, true,
	{
	    SS7MsgSCCP::SourceLocalReference,
	    SS7MsgSCCP::ProtocolClass,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::CalledPartyAddress,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::CC, true,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::SourceLocalReference,
	    SS7MsgSCCP::ProtocolClass,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::CREF, true,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::RefusalCause,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::RLSD, true,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::SourceLocalReference,
	    SS7MsgSCCP::ReleaseCause,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::RLC, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::SourceLocalReference,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::DT1, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::Sequencing,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::Data,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::DT2, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::Sequencing,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::Data,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::AK, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::ReceiveSequenceNumber,
	    SS7MsgSCCP::Credit,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::UDT, false,
	{
	    SS7MsgSCCP::ProtocolClass,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::CalledPartyAddress,
	    SS7MsgSCCP::CallingPartyAddress,
	    SS7MsgSCCP::Data,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::UDTS, false,
	{
	    SS7MsgSCCP::ReturnCause,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::CalledPartyAddress,
	    SS7MsgSCCP::CallingPartyAddress,
	    SS7MsgSCCP::Data,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::ED, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::Data,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::EA, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::RSR, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::SourceLocalReference,
	    SS7MsgSCCP::ResetCause,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::RSC, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::SourceLocalReference,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::ERR, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::ErrorCause,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::IT, false,
	{
	    SS7MsgSCCP::DestinationLocalReference,
	    SS7MsgSCCP::SourceLocalReference,
	    SS7MsgSCCP::ProtocolClass,
	    SS7MsgSCCP::Sequencing,
	    SS7MsgSCCP::Credit,
	SS7MsgSCCP::EndOfParameters,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::XUDT, true,
	{
	    SS7MsgSCCP::ProtocolClass,
	    SS7MsgSCCP::HopCounter,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::CalledPartyAddress,
	    SS7MsgSCCP::CallingPartyAddress,
	    SS7MsgSCCP::Data,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::LUDT, true,
	{
	    SS7MsgSCCP::ProtocolClass,
	    SS7MsgSCCP::HopCounter,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::CalledPartyAddress,
	    SS7MsgSCCP::CallingPartyAddress,
	    SS7MsgSCCP::LongData,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::XUDTS, true,
	{
	    SS7MsgSCCP::ReturnCause,
	    SS7MsgSCCP::HopCounter,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::CalledPartyAddress,
	    SS7MsgSCCP::CallingPartyAddress,
	    SS7MsgSCCP::Data,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::LUDTS, true,
	{
	    SS7MsgSCCP::ReturnCause,
	    SS7MsgSCCP::HopCounter,
	SS7MsgSCCP::EndOfParameters,
	    SS7MsgSCCP::CalledPartyAddress,
	    SS7MsgSCCP::CallingPartyAddress,
	    SS7MsgSCCP::LongData,
	SS7MsgSCCP::EndOfParameters
	}
    },
    { SS7MsgSCCP::Unknown, false, { SS7MsgSCCP::EndOfParameters } }
};


static bool decodeParam(const SS7SCCP* sccp, NamedList& list, const SCCPParam* param,
    const unsigned char* buf, unsigned int len, const String& prefix)
{
    DDebug(sccp,DebugAll,"decodeParam(%p,%p,%p,%u) type=0x%02x, size=%u, name='%s'",
	&list,param,buf,len,param->type,param->size,param->name);
    if (param->size && (param->size != len))
	return false;
    if (param->decoder)
	return param->decoder(sccp,list,param,buf,len,prefix);
    return decodeRaw(sccp,list,param,buf,len,prefix);
}

// Generic encode helper function for a single mandatory parameter
static unsigned char encodeParam(const SS7SCCP* sccp, SS7MSU& msu,
    const SCCPParam* param, const NamedList* params, ObjList& exclude,
    const String& prefix, unsigned char* buf = 0)
{
    DDebug(sccp,DebugAll,"encodeParam (mand) (%p,%p,%p,%p) type=0x%02x, size=%u, name='%s'",
	&msu,param,params,buf,param->type,param->size,param->name);
    // variable length must not receive fixed buffer
    if (buf && !param->size)
	return 0;
    NamedString* val = params ? params->getParam(prefix+param->name) : 0;
    if (val)
	exclude.append(val)->setDelete(false);
    if (param->encoder)
	return param->encoder(sccp,msu,buf,param,val,params,prefix);
    return encodeRaw(sccp,msu,buf,param,val,params,prefix);
}

// Generic encode helper for a single optional parameter
static unsigned char encodeParam(const SS7SCCP* sccp, SS7MSU& msu,
    const SCCPParam* param, const NamedString* val,
    const NamedList* extra, const String& prefix)
{
    DDebug(sccp,DebugAll,"encodeParam (opt) (%p,%p,%p,%p) type=0x%02x, size=%u, name='%s'",
	&msu,param,val,extra,param->type,param->size,param->name);
    // add the parameter type now but remember the old length
    unsigned int len = msu.length();
    unsigned char tmp = param->type;
    msu.append(&tmp,1);

    unsigned char size = 0;
    if (param->encoder)
	size = param->encoder(sccp,msu,0,param,val,extra,prefix);
    else
	size = encodeRaw(sccp,msu,0,param,val,extra,prefix);
    if (!size) {
	Debug(sccp,DebugMild,"Unwinding type storage for failed parameter %s",param->name);
	msu.truncate(len);
    }
    return size;
}

// Locate the description for a parameter by type
static const SCCPParam* getParamDesc(SS7MsgSCCP::Parameters type)
{
    const SCCPParam* param = s_paramDefs;
    for (; param->type != SS7MsgSCCP::EndOfParameters; param++) {
	if (param->type == type)
	    return param;
    }
    return 0;
}

// Locate the description for a parameter by name
static const SCCPParam* getParamDesc(const String& name)
{
    const SCCPParam* param = s_paramDefs;
    for (; param->type != SS7MsgSCCP::EndOfParameters; param++) {
	if (name == param->name)
	    return param;
    }
    return 0;
}

// Locate the description table for a message according to protocol type
static const MsgParams* getSccpParams(SS7MsgSCCP::Type msg)
{
    const MsgParams* params = 0;
    for (params = s_common_params; params->type != SS7MsgSCCP::Unknown; params++) {
	if (params->type == msg)
	    return params;
    }
    return 0;
}


const TokenDict* SS7MsgSCCP::names()
{
    return s_names;
}

SS7MsgSCCP::~SS7MsgSCCP()
{
    if (m_data) {
	m_data->clear(false);
	TelEngine::destruct(m_data);
    }
}

void SS7MsgSCCP::toString(String& dest, const SS7Label& label, bool params,
	const void* raw, unsigned int rawLen) const
{
    const char* enclose = "\r\n-----";
    dest = enclose;
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
 * SS7MsgSccpReassemble
 */

SS7MsgSccpReassemble::SS7MsgSccpReassemble(SS7MsgSCCP* msg, const SS7Label& label,
	    unsigned int timeToLive)
    : SS7MsgSCCP(msg->type()), m_label(label), m_callingPartyAddress(""),
    m_segmentationLocalReference(0), m_timeout(0), m_remainingSegments(0),
    m_firstSgmDataLen(0)
{
    m_callingPartyAddress.copySubParams(msg->params(),
	    YSTRING("CallingPartyAddress."));
    m_segmentationLocalReference = msg->params().getIntValue(
	    YSTRING("Segmentation.SegmentationLocalReference"));
    m_timeout = Time::msecNow() + timeToLive;
    m_remainingSegments = msg->params().getIntValue(
	    YSTRING("Segmentation.RemainingSegments"));
    setData(new DataBlock(*msg->getData()));
    params().copyParams(msg->params());
    m_firstSgmDataLen = getData()->length();
    // Update protocol class
    if (msg->params().getIntValue(
	    YSTRING("Segmentation.ProtocolClass"), -1) > 0)
	params().setParam("ProtocolClass",msg->params().getValue(
		YSTRING("Segmentation.ProtocolClass")));

}

SS7MsgSccpReassemble::~SS7MsgSccpReassemble()
{
    TelEngine::destruct(extractData());
}

bool SS7MsgSccpReassemble::canProcess(const SS7MsgSCCP* msg, const SS7Label& label)
{
    if (!compareLabel(m_label,label))
	return false;
    if (m_segmentationLocalReference !=
	    (u_int32_t)msg->params().getIntValue(YSTRING("Segmentation.SegmentationLocalReference")))
	return false;
    NamedList address("");
    address.copySubParams(msg->params(),YSTRING("CallingPartyAddress."));
    return compareNamedList(address,m_callingPartyAddress);
}

SS7MsgSccpReassemble::Return SS7MsgSccpReassemble::appendSegment(SS7MsgSCCP* msg, const SS7Label& label)
{
    if (!msg)
	return Rejected;
    if (!canProcess(msg,label))
	return Rejected;
    if ((m_remainingSegments - 1) != msg->params().getIntValue(YSTRING("Segmentation.RemainingSegments"),-1)) {
	DDebug("SS7MsgSccpReassemble",DebugNote,"Received out of sequence segment %d : %d",
	       msg->params().getIntValue(YSTRING("Segmentation.RemainingSegments"),-1),m_remainingSegments);
	return Error;
    }
    m_remainingSegments--;
    if (m_firstSgmDataLen < msg->getData()->length()) {
	DDebug("SS7MsgSccpReassemble",DebugNote,"Received data segment bigger than first data segment");
	return Error;
    }
    getData()->append(*msg->getData());
    return m_remainingSegments == 0 ? Finished : Accepted;
}

/**
 * class SCCP
 */

SCCP::SCCP()
    : m_translatorLocker(true,s_sccpTranslatorMutex), m_usersLocker(true,s_sccpMutexName), m_translator(0)
{
}

SCCP::~SCCP()
{
    DDebug(this,DebugAll,"Destroying SCCP [%p]",this);
    // If we have undetached users scream as hard as we can
    if (m_users.skipNull())
	Debug(this,DebugGoOn,"Destroying SCCP with %d undetached users!!!",m_users.count());
    if (m_translator)
	Debug(this,DebugGoOn,"Destroying SCCP with an valid translator!!!");
}

void SCCP::attach(SCCPUser* user)
{
    if (!user)
	return;
    DDebug(this,DebugAll,"Attaching user (%p)",user);
    Lock lock(m_usersLocker);
    // Detach it if already exists
    detach(user);
    // Append the user
    m_users.append(user)->setDelete(false);
}

void SCCP::detach(SCCPUser* user)
{
    if (!user)
	return;
    Lock lock(m_usersLocker);
    m_users.remove(user,false);
}

void SCCP::attachGTT(GTT* gtt)
{
    Lock lock(m_translatorLocker);
    if (gtt == m_translator)
	return;
    m_translator = gtt;
}

NamedList* SCCP::translateGT(const NamedList& params, const String& prefix, const String& nextPrefix)
{
    Lock lock(m_translatorLocker);
    if (!m_translator) {
	Debug(this,isEndpoint() ? DebugInfo : DebugMild,
	      "Failed to translate Global Title! Reason: No GTT attached to sccp [%p]",this);
	return 0;
    }
    RefPointer<GTT> translator = m_translator;
    if (!translator)
	return 0;
    lock.drop();
    return translator->routeGT(params,prefix,nextPrefix);
}

HandledMSU SCCP::pushMessage(DataBlock& data, NamedList& params, int ssn)
{
    m_usersLocker.lock();
    ListIterator iter(m_users);
    SCCPUser* usr = 0;
    params.setParam("ssn",String(ssn));
    while ((usr = YOBJECT(SCCPUser,iter.get()))) {
	RefPointer<SCCPUser> pointer = usr;
	if (!pointer)
	    continue;
	m_usersLocker.unlock();
	HandledMSU handled = pointer->receivedData(data,params);
	switch (handled) {
	    case HandledMSU::Accepted:
	    case HandledMSU::Failure:
		return handled;
	    case HandledMSU::Rejected:
	    default:
		break; // break switch
	}
	m_usersLocker.lock();
    }
    m_usersLocker.unlock();
    DDebug(this,DebugInfo,"SCCP data message was not processed by any user!");
    return HandledMSU::Unequipped;
}

HandledMSU SCCP::notifyMessage(DataBlock& data, NamedList& params, int ssn)
{
    m_usersLocker.lock();
    ListIterator iter(m_users);
    SCCPUser* usr = 0;
    params.setParam("ssn",String(ssn));
    while ((usr = YOBJECT(SCCPUser,iter.get()))) {
	RefPointer<SCCPUser> pointer = usr;
	if (!pointer)
	    continue;
	m_usersLocker.unlock();
	HandledMSU handled = pointer->notifyData(data,params);
	switch (handled) {
	    case HandledMSU::Accepted:
	    case HandledMSU::Failure:
		return handled;
	    case HandledMSU::Rejected:
	    default:
		break; // break switch
	}
	m_usersLocker.lock();
    }
    m_usersLocker.unlock();
    DDebug(this,DebugAll,"SCCP notify message was not processed by any user!");
    return HandledMSU::Unequipped;
}

bool SCCP::managementMessage(Type type, NamedList& params)
{
    m_usersLocker.lock();
    ListIterator iter(m_users);
    bool ret = false;
    SCCPUser* usr = 0;
    while ((usr = YOBJECT(SCCPUser,iter.get()))) {
	RefPointer<SCCPUser> pointer = usr;
	if (!pointer)
	    continue;
	m_usersLocker.unlock();
	if (pointer->managementNotify(type,params))
	    ret = true;
	m_usersLocker.lock();
    }
    m_usersLocker.unlock();
    return ret;
}

int SCCP::sendMessage(DataBlock& data, const NamedList& params)
{
    Debug(this,DebugStub,"Please implement SCCP sendMessage");
    return false;
}

bool SCCP::managementStatus(Type type, NamedList& params)
{
    DDebug(this,DebugStub,"Please implement SCCP::managementStatus()!!");
    return false;
}

void SCCP::resolveGTParams(SS7MsgSCCP* msg, const NamedList* gtParams)
{
    if (!msg || !gtParams)
	return;
    msg->params().clearParam(YSTRING("CalledPartyAddress"),'.');
    for (unsigned int i = 0;i < gtParams->length();i++) {
	NamedString* val = gtParams->getParam(i);
	if (val && (val->name().startsWith("gt") || val->name() == YSTRING("pointcode") ||
		val->name() == YSTRING("ssn") || val->name() == YSTRING("route")))
	    msg->params().setParam("CalledPartyAddress." + val->name(),*val);
    }
    NamedString* param = 0;
    if ((param = gtParams->getParam(YSTRING("sccp"))))
	msg->params().setParam(param->name(),*param);
    if (!gtParams->hasSubParams(YSTRING("CallingPartyAddress.")))
	return;
    msg->params().clearParam(YSTRING("CallingPartyAddress"),'.');
    msg->params().copySubParams(*gtParams,YSTRING("CallingPartyAddress."),false);
}

/**
 * class SCCPUser
 */

SCCPUser::SCCPUser(const NamedList& config)
    : SignallingComponent(config,&config),
      m_sccp(0), m_sccpMutex(true,s_userMutexName), m_sls(-1)
{
    String tmp;
    config.dump(tmp,"\r\n  ",'\'',true);
    DDebug(DebugAll,"SCCPUser::SCCPUser(%s)",tmp.c_str());
}

SCCPUser::~SCCPUser()
{
    DDebug(this,DebugAll,"Destroying SCCPUser [%p]",this);
}

void SCCPUser::destroyed()
{
    Lock lock(m_sccpMutex);
    if (m_sccp)
	attach(0);
    lock.drop();
    SignallingComponent::destroyed();
}

void SCCPUser::attach(SCCP* sccp)
{
    Lock lock(m_sccpMutex);
    if (!sccp) {
	if (!m_sccp) {
	    DDebug(this,DebugNote,"Request to attach null sccp!!! ");
	    return;
	}
	m_sccp->detach(this);
	TelEngine::destruct(m_sccp);
	return;
    }
    if (m_sccp == sccp) {
	sccp->deref();
	DDebug(this,DebugInfo,"Requesting to attach the same sccp (%p)",m_sccp);
	return;
    }

    SCCP* temp = m_sccp;
    m_sccp = sccp;
    // Do not ref the sccp because we already have an reference
    m_sccp->attach(this);
    // Destruct the old sccp
    if (temp) {
	temp->detach(this);
	TelEngine::destruct(temp);
	temp = 0;
    }
}

bool SCCPUser::initialize(const NamedList* config)
{
    DDebug(this,DebugInfo,"SCCPUser::initialize(%p) [%p]",config,this);
    if (engine()) {
	NamedList params("sccp");
	if (!resolveConfig(YSTRING("sccp"),params,config))
	    params.addParam("local-config","true");
	// NOTE SS7SCCP is created on demand!!!
	// engine ->build method will search for the requested sccc and
	// if it was found will return it with the ref counter incremented
	// if it wasn't found the refcounter will be 1
	// For this behavior SCCPUser attach method will not reference the sccp
	// pointer instead will use the reference of engine build
	if (params.toBoolean(true))
	    attach(YOBJECT(SCCP,engine()->build("SCCP",params,true)));
    } else
	Debug(this,DebugWarn,"SccpUser::initialize() can not attach sccp; null SigEngine!");
    return m_sccp != 0;
}

bool SCCPUser::sendData(DataBlock& data, NamedList& params)
{
    if (!m_sccp) {
	Debug(this,DebugMild,"Can not send data! No Sccp attached!");
	return false;
    }
    bool sequenceControl = params.getBoolValue("sequenceControl",false);
    params.addParam("ProtocolClass",(sequenceControl ? "1" : "0"));
    int sls = params.getIntValue("sls",-1);
    if (sls < 0) {
	// Preserve the sls only if sequence control is requested
	if (sequenceControl)
	    sls = m_sls;
	if (sls < 0)
	    sls = Random::random() & 0xff;
    }
    else
	sls &= 0xff;
    params.setParam("sls", String(sls));
    if (sccp()->sendMessage(data,params) < 0)
	return false;
    m_sls = sls; // Keep the last SLS sent
    return true;
}

bool SCCPUser::sccpNotify(SCCP::Type type, NamedList& params)
{
    if (!m_sccp) {
	Debug(this,DebugMild,"Can not send data! No Sccp attached!");
	return false;
    }
    return sccp()->managementStatus(type,params);
}

HandledMSU SCCPUser::receivedData(DataBlock& data, NamedList& params)
{
    Debug(DebugStub,"Please implement SCCPUser::receivedData(DataBlock& data, const NamedList& params)");
    return 0;
}

HandledMSU SCCPUser::notifyData(DataBlock& data, NamedList& params)
{
    Debug(DebugStub,"Please implement SCCPUser::notifyData(DataBlock& data, const NamedList& params)");
    return 0;
}

bool SCCPUser::managementNotify(SCCP::Type type, NamedList& params)
{
    Debug(this,DebugStub,"Please implement SCCPUser::managementNotify()");
    return false;
}

/**
 * class GTT
 */

GTT::GTT(const NamedList& config)
    : SignallingComponent(config.safe("GTT"),&config,"ss7-gtt"),
      m_sccp(0)
{
}

GTT::~GTT()
{
    if (m_sccp) {
	m_sccp->attachGTT(0);
	TelEngine::destruct(m_sccp);
	m_sccp = 0;
    }
}

bool GTT::initialize(const NamedList* config)
{
    DDebug(this,DebugInfo,"GTT::initialize(%p) [%p]",config,this);
    if (engine()) {
	NamedList params("sccp");
	if (!resolveConfig(YSTRING("sccp"),params,config))
	    params.addParam("local-config","true");
	if (params.toBoolean(true))
	    attach(YOBJECT(SCCP,engine()->build("SCCP",params,true)));
    } else
	Debug(this,DebugWarn,"GTT::initialize() can not attach sccp; null SigEngine");
    return m_sccp != 0;
}

NamedList* GTT::routeGT(const NamedList& gt, const String& prefix, const String& nextPrefix)
{
    Debug(DebugStub,"Please implement NamedList* GTT::routeGT(%s,%s,%s)",
	    gt.c_str(),prefix.c_str(),nextPrefix.c_str());
    return 0;
}

void GTT::attach(SCCP* sccp)
{
    if (!sccp)
	return;
    if (m_sccp == sccp) {
	sccp->deref();
	return;
    }
    SCCP* tmp = m_sccp;
    m_sccp = sccp;
    m_sccp->attachGTT(this);
    if (!tmp)
	return;
    TelEngine::destruct(tmp);
    tmp = 0;
}

void GTT::destroyed()
{
    if (m_sccp) {
	m_sccp->attachGTT(0);
	TelEngine::destruct(m_sccp);
	m_sccp = 0;
    }
    SignallingComponent::destroyed();
}

/**
 * SCCPManagement
 */
SCCPManagement::SCCPManagement(const NamedList& params, SS7PointCode::Type type)
    : SignallingComponent(params,&params,"ss7-sccp-mgm"),
      Mutex(true, s_managementMutexName), m_remoteSccp(),
    m_statusTest(), m_localSubsystems(), m_concerned(), m_pcType(type), m_sccp(0), m_unknownSubsystems("ssn"),
    m_subsystemFailure(0), m_routeFailure(0), m_autoAppend(false), m_printMessages(false)
{
    DDebug(DebugAll,"Creating SCCP management (%p)",this);
    // stat.info timer
    m_testTimeout = params.getIntValue(YSTRING("test-timer"),5000);
    if (m_testTimeout < 5000)
	m_testTimeout = 5000;
    else if (m_testTimeout > 10000)
	m_testTimeout = 10000;
    // coord.chg timer
    m_coordTimeout = params.getIntValue(YSTRING("coord-timer"),1000);
    if (m_coordTimeout < 1000)
	m_coordTimeout = 1000;
    if (m_coordTimeout > 2000)
	m_coordTimeout = 2000;
    m_ignoreStatusTestsInterval = params.getIntValue(YSTRING("ignore-tests"),1000);
    m_printMessages = params.getBoolValue(YSTRING("print-messages"), false);
    m_autoAppend = params.getBoolValue(YSTRING("auto-monitor"),false);
    for (unsigned int i = 0;i < params.length();i++) {
	NamedString* param = params.getParam(i);
	if (!param)
	    continue;
	XDebug(this,DebugAll,"Parsing param %s : %s",param->name().c_str(),param->c_str());
	if (param->name() == YSTRING("remote")) {
	    SccpRemote* rem = new SccpRemote(m_pcType);
	    if (rem->initialize(*param))
		m_remoteSccp.append(rem);
	    else {
		Debug(this,DebugConf,"Failed to initialize remote sccp %s",param->c_str());
		TelEngine::destruct(rem);
	    }
	} else if (param->name() == YSTRING("concerned")) {
	    SccpRemote* rem = new SccpRemote(m_pcType);
	    if (rem->initialize(*param))
		m_concerned.append(rem);
	    else {
		Debug(this,DebugConf,"Failed to initialize concerned sccp %s",param->c_str());
		TelEngine::destruct(rem);
	    }
	}
    }
    NamedString* lsubs = params.getParam(YSTRING("local-subsystems"));
    ObjList* list = lsubs ? lsubs->split(',') : 0;
    if (!list)
	return;
    for (ObjList* o = list->skipNull();o;o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	unsigned char ssn = s->toInteger();
	if (ssn < 2)
	    continue;
	m_localSubsystems.append(new SccpLocalSubsystem(ssn,getCoordTimeout(),
		    getIgnoreTestsInterval()));
    }
    TelEngine::destruct(list);
}

SCCPManagement::~SCCPManagement()
{
    DDebug(this,DebugAll,"Destroing SCCPManagement %p",this);
    m_sccp = 0;
}

void SCCPManagement::attach(SS7SCCP* sccp)
{
    Lock lock(this);
    if (!sccp || m_sccp)
	return;
    m_sccp = sccp;
}

bool SCCPManagement::initialize(const NamedList* config)
{
    if (!config) {
	DDebug(this,DebugNote,"Request to initialize sccp management from null conf");
	return true;
    }
    Lock lock(this);
#ifdef DEBUG
    String dst;
    config->dump(dst,"\r\n");
    Debug(this,DebugInfo,"Initializeing SCCPManagement(%p) %s",this,dst.c_str());
#endif
    m_printMessages = config->getBoolValue(YSTRING("print-messages"), m_printMessages);
    return true;
}

void SCCPManagement::pointcodeStatus(SS7Layer3* link, bool operational)
{
    if (!sccp() || !operational) {
	DDebug(this,DebugNote,"Can not process pointcode status sccp(%p) , is up : %s",
	       sccp(), String::boolText(operational));
	return;
    }
    lock();
    for (ObjList* o = m_remoteSccp.skipNull();o;o = o->skipNext()) {
	SccpRemote* rsccp = static_cast<SccpRemote*>(o->get());
	SS7Route::State state = sccp()->network()->getRouteState(m_pcType,rsccp->getPointCode());
	XDebug(this,DebugAll,"Checking route status for remote sccp %s oldState: '%s' newState: '%s'",
	       rsccp->toString().c_str(),stateName(rsccp->getState()),SS7Route::stateName(state));
	if ((int)state != (int)rsccp->getState()) {
	    unlock();
	    manageSccpRemoteStatus(rsccp,state);
	    lock();
	}
    }
    unlock();
}

void SCCPManagement::routeStatus(SS7PointCode::Type type, const SS7PointCode& node, SS7Route::State state)
{
    if (!sccp() || !sccp()->isLayer3Up()) {
	DDebug(this,DebugNote,"Can not process pointcode status sccp(%p) , is up : %s",
	       sccp(), sccp() ? String::boolText(sccp()->isLayer3Up()) : "false");
	return;
    }
    lock();
    for (ObjList* o = m_remoteSccp.skipNull();o;o = o->skipNext()) {
	SccpRemote* rsccp = static_cast<SccpRemote*>(o->get());
#ifdef XDEBUG
	String dest;
	dest << " Local: " << rsccp->getPointCode() << " remote : " << node;
	XDebug (this,DebugNote,"Processing routeStatus %s oldState: '%s' newState: '%s'", dest.c_str(),
		stateName(rsccp->getState()),SS7Route::stateName(state));
#endif
	if (rsccp->getPointCode() != node)
	    continue;
	if ((int)rsccp->getState() == (int)state)
	    break;
	RefPointer<SccpRemote> ref = rsccp;
	if (!ref)
	    continue;
	unlock();
	manageSccpRemoteStatus(rsccp,state);
	return;
    }
    unlock();
}

bool SCCPManagement::handleMessage(int msgType, unsigned char ssn, unsigned char smi, NamedList& params)
{
    int pointcode = params.getIntValue(YSTRING("pointcode"));
    Lock lock(this);
    bool sendMsg = false;
    MsgType msg = SSA;
    switch (msgType) {
	case SSA:
	case SSP:
	{
	    SccpSubsystem* sccpSub = new SccpSubsystem(ssn);
	    SccpRemote* rsccp = new SccpRemote(pointcode,m_pcType);
	    lock.drop();
	    if (ssn == 1 && msgType == SSA)
		manageSccpRemoteStatus(rsccp,SS7Route::Allowed);
	    else if (ssn > 1)
		handleSubsystemStatus(sccpSub, msgType == SSA, rsccp, smi);
	    else
		Debug(this,DebugWarn,"Received Invalid sccp message %s for ssn %d",
		      lookup(msgType,s_managementMessages), ssn);
	    TelEngine::destruct(sccpSub);
	    TelEngine::destruct(rsccp);
	    return true;
	}
	case SST: // Received sst
	{
	    if (ssn == 1)  { // SST is initiated for local sccp send ssa
		sendMsg = true;
		break;
	    }
	    SccpLocalSubsystem* sccps = getLocalSubsystem(ssn);
	    if (sccps) {
		XDebug(this,DebugAll,"Received SST for %d state: %s ignoreTests %s",
		       ssn,stateName(sccps->getState()),String::boolText(sccps->ignoreTests()));
		if (sccps->ignoreTests())
		    return true;
		if (sccps->getState() == SCCPManagement::Allowed) {
		    sendMsg = true;
		    break;
		}
		lock.drop();
		if (!managementMessage(SCCP::SubsystemStatus,params))
		    return true;
		String* status = params.getParam(YSTRING("subsystem-status"));
		if (status && *status == YSTRING("UserInService"))
		    sendMessage(msg,params);
		return true;
	    }
	    if (!sendMsg)
		Debug(this,DebugConf,"Received SST from: '%s' for missing local subsystem %d",
		    params.getValue(YSTRING("RemotePC")),ssn);
	    break;
	}
	case SOR:
	{
	    lock.drop();
	    managementMessage(SCCP::CoordinateIndication,params);
	    return true;
	}
	case SOG:
	    handleSog(ssn,pointcode);
	    return true;
	default:
	    Debug(sccp(),DebugNote,"Received unknown management Message '%s'",
		  lookup(msgType,s_managementMessages));
    }
    lock.drop();
    if (sendMsg)
	sendMessage(msg,params);
    return true;
}

bool SCCPManagement::managementMessage(SCCP::Type type, NamedList& params)
{
    if (!m_sccp)
	return false;
    return m_sccp->managementMessage(type,params);
}

void SCCPManagement::putValue(NamedList& params,int val,const char* name, bool dict)
{
    if (val < 0)
	return;
    if (!dict)
	params.setParam(name,String(val));
    else
	params.setParam(name,lookup(val,s_broadcastType));
}

void SCCPManagement::localBroadcast(SCCP::Type type, int pointcode, int sps,
		int rss, int rl, int ssn, int ss)
{
    if (!m_sccp)
	return;
    NamedList params("lb");
    putValue(params,pointcode,"pointcode");
    putValue(params,rl,"restriction-level");
    putValue(params,ssn,"ssn");
    putValue(params,sps,"signalling-point-status",true);
    putValue(params,ss,"subsystem-status",true);
    putValue(params,rss,"remote-sccp-status",true);
    m_sccp->managementMessage(type,params);
}

SccpLocalSubsystem* SCCPManagement::getLocalSubsystem(unsigned char ssn)
{
    Lock lock(this);
    for (ObjList* o = m_localSubsystems.skipNull();o;o = o->skipNext()) {
	SccpLocalSubsystem* ss = static_cast<SccpLocalSubsystem*>(o->get());
	if (ss && ss->getSSN() == ssn)
	    return ss;
    }
    return 0;
}

bool SCCPManagement::processMessage(SS7MsgSCCP* message)
{
    Debug(DebugStub,"Please implement management message decoder");
    return true;
}

const TokenDict* SCCPManagement::broadcastType()
{
    return s_broadcastType;
}

void SCCPManagement::notify(SCCP::Type type, NamedList& params)
{
    if (!m_sccp)
	return;
#ifdef DEBUG
    String tmp;
    params.dump(tmp,"\r\n");
    Debug(this,DebugAll,"User notify %s : \r\n%s",lookup(type,s_sccpNotif),tmp.c_str());
#endif
    unsigned char ssn = params.getIntValue(YSTRING("ssn"));
    if (ssn < 2) {
	Debug(this,DebugNote,"Received management notify with invalid ssn %d",ssn);
	return;
    }
    unsigned char smi = params.getIntValue(YSTRING("smi")); // subsystem multiplicity indicator
    if (smi > 3) {
	Debug(this,DebugNote, "Received management notify message with unknown smi: %d , ssn: %d",
		  smi,ssn);
	smi = 0;
    }
    switch (type) {
	case SCCP::CoordinateRequest: // Affected subsystem, subsystem multiplicity indicator
	    handleCoordinateChanged(ssn,smi,params);
	    break;
	case SCCP::CoordinateResponse:// Affected subsystem, subsystem multiplicity indicator
	    params.setParam(YSTRING("pointcode"),String(m_sccp->getPackedPointCode()));
	    sendMessage(SOG,params);
	    break;
	case SCCP::StatusRequest: // Affected subsystem, subsystem multiplicity indicator, user status
	{
	    const char* subsystemStatus = params.getValue(YSTRING("subsystem-status"));
	    int status = lookup(subsystemStatus,broadcastType());
	    if (status != UserOutOfService && status != UserInService) {
		Debug(this,DebugNote,"Reveived subsystem status indication with wrong subsystem status: %s",
			subsystemStatus);
		return;
	    }
	    SccpSubsystem* sub = new SccpSubsystem(ssn);
	    handleSubsystemStatus(sub, status == UserInService, 0, smi);
	    TelEngine::destruct(sub);
	    break;
	}
	default:
	    Debug(this,DebugNote,"Unhandled message '%s' received from attached users!",
		  lookup(type,s_sccpNotif));
    }
}

void SCCPManagement::handleSog(unsigned char ssn, int pointcode)
{
    for (ObjList* ol = m_localSubsystems.skipNull();ol;ol = ol->skipNext()) {
	SccpLocalSubsystem* sls = static_cast<SccpLocalSubsystem*>(ol->get());
	if (sls->receivedSOG(ssn,pointcode))
	    break;
    }
}

void SCCPManagement::handleCoordinateChanged(unsigned char ssn, int smi, const NamedList& params)
{
    Lock lock(this);
    SccpLocalSubsystem* sub = getLocalSubsystem(ssn);
    if (!sub) {
	Debug(this,DebugInfo,"Dinamicaly appending ssn %d to local subsystems list!",ssn);
	sub = new SccpLocalSubsystem(ssn,m_coordTimeout,m_ignoreStatusTestsInterval,smi);
	m_localSubsystems.append(sub);
    }
    sub->ref();
    lock.drop();
    if (sub->getState() == SCCPManagement::Prohibited)
	Debug(this,DebugStub,"Subsystem %d wishes to go oos but is already oos! Logic Bug?",sub->getSSN());
    sub->clearBackups();
    int count = params.getIntValue(YSTRING("backups"));
    for (int i = 0;i < count; i++) {
	String name = "backup.";
	name << i;
	int subsys = params.getIntValue(name + ".ssn", -1);
	int pointcode = params.getIntValue(name + ".pointcode",-1);
	if (pointcode <= 0) {
	    Debug(this,DebugStub,"Coordinate change request to a local subsystem!");
	    continue;
	}
	if (subsys < 2 || pointcode < 0) {
	    Debug(this,DebugMild,"Invalid backup subsystem pc:%d, ssn:%d",pointcode,subsys);
	    continue;
	}
	RemoteBackupSubsystem* bs = new RemoteBackupSubsystem(subsys,pointcode,true);
	sub->appendBackup(bs);
	NamedList data("");
	data.setParam("smi",String(smi));
	data.setParam("ssn",String(subsys));
	data.setParam("pointcode",String(pointcode));
	data.setParam("RemotePC",String(pointcode));
	sendMessage(SOR,data);
    }
    sub->startCoord();
    sub->setState(WaitForGrant);
    TelEngine::destruct(sub);
}

SccpRemote* SCCPManagement::getRemoteSccp(int pointcode)
{
    for (ObjList* o = m_remoteSccp.skipNull();o;o = o->skipNext()) {
	SccpRemote* rsccp = static_cast<SccpRemote*>(o->get());
	if (rsccp->getPackedPointcode() == pointcode)
	    return rsccp;
    }
    return 0;
}

void SCCPManagement::routeFailure(SS7MsgSCCP* msg)
{
    if (!m_sccp)
	return;
    Lock lock(this);
    m_routeFailure++;
    if (!msg || !msg->params().getParam(YSTRING("RemotePC"))) {
	DDebug(this,DebugNote,"Route failure, with no pointcode present!");
	return;
    }
    int pointcode = msg->params().getIntValue(YSTRING("RemotePC"));
    if (pointcode < 1) {
	Debug(this,DebugWarn,"Remote pointcode %d is invalid!",pointcode);
	return;
    }
    if (pointcode == m_sccp->getPackedPointCode())
	return;
    SccpRemote* rsccp = getRemoteSccp(pointcode);
    if (rsccp && rsccp->getState() == SCCPManagement::Prohibited) {
	lock.drop();
	updateTables(rsccp);
	return;
    }
    if (!rsccp) {
	if (m_autoAppend) {
	    Debug(this,DebugNote,"Dynamic appending remote sccp %d to state monitoring list",
		  pointcode);
	    rsccp = new SccpRemote(pointcode,m_pcType);
	    m_remoteSccp.append(rsccp);
	} else
	    Debug(this,DebugMild,
		  "Remote sccp '%d' state is not monitored! Future message routing may not reach target!",
		  pointcode);
    }
    RefPointer<SccpRemote>ref = rsccp;
    lock.drop();
    if (!ref)
	return;
    manageSccpRemoteStatus(rsccp,SS7Route::Prohibited);
}

void SCCPManagement::subsystemFailure(SS7MsgSCCP* msg, const SS7Label& label)
{
    if (!m_sccp) {
	DDebug(this,DebugNote,"Request to process subsystem failure with no sccp attached!");
	return;
    }
    if (!msg || !msg->params().getParam(YSTRING("CalledPartyAddress.ssn"))) {
	DDebug(this,DebugNote,"Subsystem failure! no ssn");
	return;
    }
    int ssn = msg->params().getIntValue(YSTRING("CalledPartyAddress.ssn"),0);
    if (ssn <= 1) {
	DDebug(this,DebugNote,"Subsystem failure, invalid ssn: '%d'",ssn);
	return;
    }
    Lock lock(this);
    // Find local subsystem and change status
    SccpLocalSubsystem* ss = getLocalSubsystem(ssn);
    if (ss)
	ss->setState(SCCPManagement::Prohibited);
    if (m_sccp->extendedMonitoring()) {
	m_subsystemFailure++;
	NamedString* sub = msg->params().getParam(YSTRING("CalledPartyAddress.ssn"));
	if (sub) {
	    NamedString* ssnParam = m_unknownSubsystems.getParam(*sub);
	    if (ssnParam)
		incrementNS(ssnParam);
	    else
		m_unknownSubsystems.setParam(*sub,"1");
	}
    }
    lock.drop();
    notifyConcerned(SSP,ssn,0);
}

void SCCPManagement::subsystemsStatus(String& dest,bool extended)
{
    Lock lock(this);
    if (m_localSubsystems.skipNull()) {
	dest << "Local subsystems state : count: " << m_localSubsystems.count() << "\r\n";
	for (ObjList* o = m_localSubsystems.skipNull();o;o = o->skipNext()) {
	    SccpLocalSubsystem* ss = static_cast<SccpLocalSubsystem*>(o->get());
	    if (!ss)
		continue;
	    ss->dump(dest);
	    dest << "\r\n";
	}
    }
    if (m_subsystemFailure == 0) {
	dest << "\r\nMissing Local Subsystem: " << m_subsystemFailure;
	if (!extended)
	    return;
	for (unsigned int i = 0;i < m_unknownSubsystems.length();i++) {
	    NamedString* ssn = m_unknownSubsystems.getParam(i);
	    if (!ssn)
		continue;
	    dest << "\r\nReceived: " << *ssn << " packets for subsystem : " << ssn->name();
	}
    }
    if (!m_remoteSccp.skipNull())
	return;
    dest << "\r\nRemoteSccp: count: " << m_remoteSccp.count();
    for (ObjList* o = m_remoteSccp.skipNull(); o;o = o->skipNext()) {
	SccpRemote* sr = static_cast<SccpRemote*>(o->get());
	if (!sr)
	    continue;
	sr->dump(dest,true);
    }
}

void SCCPManagement::updateTables(SccpRemote* rsccp, SccpSubsystem* ssn)
{
    if (!rsccp && !ssn) {
	Debug(sccp(),DebugMild,"Request to update tables but no pointcode or ssn present!!");
	return;
    }
    if (!sccp()) {
	DDebug(this,DebugMild,"Request to update tables with no sccp attached");
	return;
    }
    const SS7PointCode* local = rsccp ? &rsccp->getPointCode() : sccp()->getLocalPointCode();
    if (!local) {
	Debug(sccp(),DebugWarn,"Can not update tables, no pointcode present!");
	return;
    }
    NamedList params("sccp.update");
    params.setParam("pointcode",String(local->pack(m_pcType)));
    params.setParam("pc-type",String((int)m_pcType));
    if (rsccp)
	params.setParam("pc-state",stateName(rsccp->getState()));
    params.setParam("component",sccp()->toString());
    if (ssn) {
	params.setParam("subsystem",String(ssn->getSSN()));
	params.setParam("subsystem-state",stateName(ssn->getState()));
    }
    sccp()->updateTables(params);
}

void SCCPManagement::routeStatus(String& dest,bool extended)
{
    dest << "\r\nRouting Status:";
    dest << "\r\nMessages Failed to be routed: " << m_routeFailure;
    if (!extended)
	return;
    // TODO call gtt print unknown translations
}

void SCCPManagement::timerTick(const Time& when)
{
    if (!lock(SignallingEngine::maxLockWait()))
	return;
    ObjList coordt;
    for (ObjList* o = m_localSubsystems.skipNull();o;o = o->skipNext()) {
	SccpLocalSubsystem* ss = static_cast<SccpLocalSubsystem*>(o->get());
	if (!ss)
	    continue;
	if (ss->timeout() && ss->ref())
	    coordt.append(ss);
    }
    // Use another list to append the sst's because the alternative is expensive for timer tick
    // (ListIterator)
    ObjList ssts;
    for (ObjList* o = m_statusTest.skipNull();o;o = o->skipNext()) {
	SubsystemStatusTest* sst = static_cast<SubsystemStatusTest*>(o->get());
	if (!sst->timeout())
	    continue;
	if (sst->ref())
	    ssts.append(sst);
    }
    unlock();
    if (coordt.skipNull())
	for (ObjList* o = coordt.skipNull();o;o = o->skipNext()) {
	    SccpLocalSubsystem* ss = static_cast<SccpLocalSubsystem*>(o->get());
	    ss->manageTimeout(this);
	}
    if (!ssts.skipNull())
	return;
    for (ObjList* o = ssts.skipNull();o;o = o->skipNext()) {
	SubsystemStatusTest* sst = static_cast<SubsystemStatusTest*>(o->get());
	if (!sst)
	    continue;
	if (sst->markAllowed() && sst->getSubsystem()->getSSN() == 1) {
	    manageSccpRemoteStatus(sst->getRemote(),SS7Route::Allowed);
	    continue;
	}
	sst->restartTimer();
	if (!sendSST(sst->getRemote(),sst->getSubsystem()))
	    sst->setAllowed(false);
    }
}

void SCCPManagement::stopSst(SccpRemote* remoteSccp, SccpSubsystem* rSubsystem, SccpSubsystem* less)
{
    if (!remoteSccp)
	return;
    Lock lock(this);
    ListIterator iter(m_statusTest);
    SubsystemStatusTest* sst = 0;
    while ((sst = YOBJECT(SubsystemStatusTest,iter.get()))) {
	if (sst->getRemote()->getPointCode() != remoteSccp->getPointCode())
	    continue;
	if (sst->getSubsystem()) {
	    if (rSubsystem && rSubsystem->getSSN() != sst->getSubsystem()->getSSN())
		continue;
	    if (less && less->getSSN() == sst->getSubsystem()->getSSN())
		continue;
	}
	m_statusTest.remove(sst);
    }
}

bool SCCPManagement::sendSST(SccpRemote* remote, SccpSubsystem* sub)
{
    NamedList params("");
    params.setParam("pointcode",String(remote->getPackedPointcode()));
    params.setParam("RemotePC",String(remote->getPackedPointcode()));
    params.setParam("smi",String(sub->getSmi()));
    params.setParam("ssn",String(sub->getSSN()));
    return sendMessage(SST,params);
}

void SCCPManagement::startSst(SccpRemote* remoteSccp, SccpSubsystem* rSubsystem)
{
    if (!remoteSccp || !rSubsystem)
	return;
    DDebug(this,DebugNote,"Requested to start test for pc : %d  ssn: %d",remoteSccp->getPackedPointcode(),rSubsystem->getSSN());
    Lock lock(this);
    for (ObjList* o = m_statusTest.skipNull();o;o = o->skipNext()) {
	SubsystemStatusTest* sst = static_cast<SubsystemStatusTest*>(o->get());
	if (sst->getRemote()->getPointCode() != remoteSccp->getPointCode())
	    continue;
	if (sst->getSubsystem() && rSubsystem->getSSN() == sst->getSubsystem()->getSSN())
	    return; // We already have the test
    }
    SubsystemStatusTest* sst = new SubsystemStatusTest(m_testTimeout);
    if (!sst->startTest(remoteSccp,rSubsystem)) {
	TelEngine::destruct(sst);
	return;
    }
    m_statusTest.append(sst);
    lock.drop();
    if (!sendSST(remoteSccp,rSubsystem))
	sst->setAllowed(false);
}

void SCCPManagement::mtpEndRestart()
{
    if (!m_sccp)
	return;
    lock();
    ListIterator iter(m_concerned);
    SccpRemote* sr = 0;
    while ((sr = YOBJECT(SccpRemote,iter.get()))) {
	SS7Route::State state = sccp()->network()->getRouteState(m_pcType,sr->getPointCode());
	RefPointer<SccpRemote> ptr = sr;
	unlock();
	if (sr->getState() !=  (SccpStates)state)
	    manageSccpRemoteStatus(sr,state); // Update remote sccp state
	if (state != SS7Route::Allowed) {
	    lock();
	    continue;
	}
	NamedList params("");
	params.setParam("pointcode",String(m_sccp->getPackedPointCode()));
	params.setParam("RemotePC",String(sr->getPackedPointcode()));
	params.setParam("smi","0");
	params.setParam("ssn","1");
	sendMessage(SSA,params);
	lock();
    }
    unlock();
}

void SCCPManagement::notifyConcerned(MsgType msg, unsigned char ssn, int smi)
{
    DDebug(this,DebugAll,"Notify concerned: msg '%s' ssn: '%d', smi: %d",
	   lookup(msg,s_managementMessages),ssn,smi);
    if (!sccp())
	return;
    Lock lock(this);
    ObjList concerned;
    for (ObjList* o = m_concerned.skipNull();o;o = o->skipNext()) {
	SccpRemote* rsccp = static_cast<SccpRemote*>(o->get());
	if (!rsccp || !rsccp->getSubsystem(ssn))
	    continue;
	if (rsccp->ref())
	    concerned.append(rsccp);
    }
    if (!concerned.skipNull()) {
	DDebug(this,DebugNote,"No Concerned pointcode for ssn %d",ssn);
	return;
    }
    NamedList params("");
    params.setParam("ssn",String((int)ssn));
    params.setParam("pointcode",String(sccp()->getPackedPointCode()));
    params.setParam("smi",String(smi));
    lock.drop();
    for (ObjList* o = concerned.skipNull();o; o = o->skipNext()) {
	SccpRemote* rsccp = static_cast<SccpRemote*>(o->get());
	if (!rsccp)
	    continue;
	params.setParam("RemotePC",String(rsccp->getPackedPointcode()));
	sendMessage(msg,params);
    }
}

void SCCPManagement::sccpUnavailable(const SS7PointCode& pointcode, unsigned char cause)
{
#ifdef DEBUG
    String dest;
    dest << pointcode;
    Debug(this,DebugInfo,"Received UPU %s cause : %d",dest.c_str(), cause);
#endif
    Lock lock(this);
    SccpRemote* rsccp = getRemoteSccp(pointcode.pack(m_pcType));
    // Do not process UPU if we do not monitor the remote sccp state
    if (!rsccp)
	return;
    rsccp->setState(SCCPManagement::Prohibited);
    // Stop all subsystem status tests
    ListIterator iter(m_statusTest);
    SubsystemStatusTest* test = 0;
    bool testStarted = false;
    while ((test = YOBJECT(SubsystemStatusTest,iter.get()))) {
	if (!test || !test->getRemote() || pointcode != test->getRemote()->getPointCode())
	    continue;
	// Do not stop test for SSN = 1 if the cause is not Unequipped
	SccpSubsystem* sub = test->getSubsystem();
	if (sub->getSSN() == 1 && cause != HandledMSU::Unequipped) {
	    testStarted = true;
	    continue;
	}
	m_statusTest.remove(test);
    }
    if (!testStarted && cause != HandledMSU::Unequipped) {
	SubsystemStatusTest* sst = new SubsystemStatusTest(m_testTimeout);
	SccpSubsystem* sub = new SccpSubsystem(1);
	if (!sst->startTest(rsccp,new SccpSubsystem(1))) {
	    TelEngine::destruct(sst);
	    TelEngine::destruct(sub);
	    return;
	}
	TelEngine::destruct(sub);
	m_statusTest.append(sst);
	sst->setAllowed(false);
    }
    lock.drop();
    localBroadcast(SCCP::StatusIndication,rsccp->getPackedPointcode(),-1,SccpRemoteInaccessible);
}

void SCCPManagement::printMessage(String& dest, MsgType type, const NamedList& params)
{
    const char* enclose = "\r\n-----";
    dest = enclose;
    dest << "\r\n " << lookup(type,s_managementMessages);
    dest << " pc: " << params.getValue(YSTRING("pointcode")) << ", ";
    dest << "ssn: " << params.getValue(YSTRING("ssn")) << ", ";
    dest << "smi: " << params.getValue(YSTRING("smi"));
    if (type == SSC) {
	dest << ", cl: " << params.getValue(YSTRING("congestion-level"));
    }
    dest << enclose;
}

/**
 * SccpLocalSubsystem
 */

SccpLocalSubsystem::SccpLocalSubsystem(unsigned char ssn, u_int64_t coordInterval, u_int64_t ignoreInterval,unsigned char smi)
    : Mutex(true,s_sccpSubsystems), m_ssn(ssn), m_smi(smi), m_state(SCCPManagement::Allowed),
    m_coordTimer(coordInterval), m_ignoreTestsTimer(ignoreInterval), m_backups(), m_receivedAll(true)
{
    DDebug("SccpSubsystem", DebugAll,"Creating sccp subsystem [%p] with ssn '%d', smi '%d'",this,ssn,smi);
}

SccpLocalSubsystem::~SccpLocalSubsystem()
{
    DDebug("SccpSubsystem", DebugAll,"Destroing sccp subsystem [%p] with ssn '%d'",this,m_ssn);
}

bool SccpLocalSubsystem::timeout()
{
    Lock lock(this);
    if (m_coordTimer.timeout()) {
	m_coordTimer.stop();
	m_receivedAll = true;
	for (ObjList* o = m_backups.skipNull();o;o = o->skipNext()) {
	    RemoteBackupSubsystem* sbs = static_cast<RemoteBackupSubsystem*>(o->get());
	    if (sbs->waitingForGrant())
		m_receivedAll = false;
	}
	if (m_receivedAll)
	    m_ignoreTestsTimer.start();
	return true;
    }
    if (m_ignoreTestsTimer.timeout()) {
	m_state = SCCPManagement::Prohibited;
	m_ignoreTestsTimer.stop();
    }
    return false;
}

void SccpLocalSubsystem::manageTimeout(SCCPManagement* mgm)
{
    if (!mgm)
	return;
    if (m_receivedAll) {
	mgm->localBroadcast(SCCP::CoordinateConfirm,-1,-1,-1,-1,m_ssn,m_smi);
	mgm->notifyConcerned(SCCPManagement::SSP,m_ssn,m_smi);
	m_state = SCCPManagement::IgnoreTests;
	return;
    }
    m_state = SCCPManagement::Allowed;
    /// TODO send local broadcast with request denied!!!
}

void SccpLocalSubsystem::dump(String& dest)
{
    dest << "Subsystem: " << m_ssn << " , smi: " << m_smi;
    dest << ", state: " << SCCPManagement::stateName(m_state) << " ";
}

bool SccpLocalSubsystem::receivedSOG(unsigned char ssn, int pointcode)
{
    Lock lock(this);
    for (ObjList* o = m_backups.skipNull();o;o = o->skipNext()) {
	RemoteBackupSubsystem* sbs = static_cast<RemoteBackupSubsystem*>(o->get());
	if (!sbs->equals(ssn,pointcode))
	    continue;
	sbs->permisionGranted();
	return true;
    }
    return false;
}

void SccpLocalSubsystem::setIgnoreTests(bool ignore)
{
    if (ignore)
	m_ignoreTestsTimer.start();
    else
	m_ignoreTestsTimer.stop();
}

/**
 * SccpRemote
 */

SccpRemote::SccpRemote(const SS7PointCode::Type type)
    : Mutex(true, s_sccpRemote), m_pointcode(type,0), m_pointcodeType(type), m_state(SCCPManagement::Allowed)
{
    DDebug("RemoteSccp",DebugAll,"Creating remote sccp [%p]",this);
}

SccpRemote::SccpRemote(unsigned int pointcode, SS7PointCode::Type pcType)
    : m_pointcode(pcType,pointcode), m_pointcodeType(pcType), m_state(SCCPManagement::Allowed)
{
    DDebug("RemoteSccp",DebugAll,"Creating remote sccp [%p] for pointcode %d",this,pointcode);
}

SccpRemote::~SccpRemote()
{
#ifdef XDEBUG
    String tmp;
    tmp << m_pointcode;
    Debug("RemoteSccp",DebugAll,"Destroying remote sccp [%p], %s",this,tmp.c_str());
#endif
}

bool SccpRemote::initialize(const String& params)
{
    ObjList* o = params.split(':',false);
    if (!o)
	return false;
    String* pointcode = static_cast<String*>(o->get());
    if (!pointcode) {
	TelEngine::destruct(o);
	return false;
    }
    bool pointcodeAssigned = false;
    if (pointcode->find('-') > 0)
	pointcodeAssigned = m_pointcode.assign(*pointcode,m_pointcodeType);
    else
	pointcodeAssigned = m_pointcode.unpack(m_pointcodeType,pointcode->toInteger());
    if (!pointcodeAssigned) {
	TelEngine::destruct(o);
	return false;
    }
    ObjList* subsystems = o->skipNext();
    while (subsystems) {
	String* sub = static_cast<String*>(subsystems->get());
	if (!sub)
	    break;
	subsystems = sub->split(',',false);
	if (!subsystems)
	    break;
	for (ObjList* ob = subsystems->skipNull();ob;ob = ob->skipNext()) {
	    String* subsystem = static_cast<String*>(ob->get());
	    unsigned int ssn = subsystem->toInteger(256);
	    if (ssn > 255) {
		DDebug(DebugConf,"Skipping ssn %d for pointcode %d Value too big!",
		       ssn,m_pointcode.pack(m_pointcodeType));
		continue;
	    }
	    m_subsystems.append(new SccpSubsystem(ssn));
	}
	TelEngine::destruct(subsystems);
	break;
    }
    TelEngine::destruct(o);
    return true;
}

SccpSubsystem* SccpRemote::getSubsystem(int ssn)
{
    Lock lock(this);
    for (ObjList* o = m_subsystems.skipNull();o;o = o->skipNext()) {
	SccpSubsystem* sub = static_cast<SccpSubsystem*>(o->get());
	if (sub && sub->getSSN() == ssn)
	    return sub;
    }
    return 0;
}

void SccpRemote::setState(SCCPManagement::SccpStates state)
{
    if (m_state == state)
	return;
    Lock lock(this);
    m_state = state;
    for (ObjList* o = m_subsystems.skipNull();o;o = o->skipNext()) {
	SccpSubsystem* sub = static_cast<SccpSubsystem*>(o->get());
	sub->setState(state);
    }
}

void SccpRemote::dump(String& dest, bool extended)
{
    Lock lock(this);
    dest << "\r\n----Sccp : " << m_pointcode;
    dest << " (" << m_pointcode.pack(m_pointcodeType) << "," << SS7PointCode::lookup(m_pointcodeType) << ") ";
    dest << "State : " << SCCPManagement::stateName(m_state) << "; ";
    if (extended) {
	dest << "Subsystems : " << m_subsystems.count() << "; ";
	for (ObjList* o = m_subsystems.skipNull();o;o = o->skipNext()) {
	    SccpSubsystem* ss = static_cast<SccpSubsystem*>(o->get());
	    if (!ss)
		continue;
	    ss->dump(dest);
	    dest << " | ";
	}
    }
    dest << "----";
}

bool SccpRemote::changeSubsystemState(int ssn,SCCPManagement::SccpStates newState)
{
    Lock lock(this);
    SccpSubsystem* ss = getSubsystem(ssn);
    if (!ss)
	return true;
    if (ss->getState() == newState)
	return false;
    ss->setState(newState);
    return true;
}

/**
 * SubsystemStatusTest
 */

SubsystemStatusTest::~SubsystemStatusTest()
{
    DDebug("SST",DebugAll,"Stoping SST for pc: '%d' ssn: '%d'", m_remoteSccp ? m_remoteSccp->getPackedPointcode() : 0,
		m_remoteSubsystem ? m_remoteSubsystem->getSSN() : 0);
    if (m_remoteSccp)
	TelEngine::destruct(m_remoteSccp);
    if (m_remoteSubsystem)
	TelEngine::destruct(m_remoteSubsystem);
}

bool SubsystemStatusTest::startTest(SccpRemote* remoteSccp, SccpSubsystem* rSubsystem)
{
    if (!remoteSccp || !remoteSccp->ref())
	return false;
    m_remoteSccp = remoteSccp;
    if (!rSubsystem || !rSubsystem->ref()) {
	TelEngine::destruct(m_remoteSccp);
	return false;
    }
#ifdef DEBUG
    String dump;
    remoteSccp->dump(dump,false);
    Debug("SST",DebugInfo,"Starting subsystem status test for '%s' ssn = '%d' subsystem state : %s",
	  dump.c_str(),rSubsystem->getSSN(),SCCPManagement::stateName(rSubsystem->getState()));
#endif
    m_remoteSubsystem = rSubsystem;
    m_statusInfo.start();
    if (rSubsystem->getSSN() == 1)
	m_markAllowed = true;
    return true;
}

void SubsystemStatusTest::restartTimer()
{
    m_interval *= 2;
    if (m_interval > MAX_INFO_TIMER)
	m_interval = MAX_INFO_TIMER;
    m_statusInfo.fire(Time::msecNow() + m_interval);
}

/**
 * class SS7SCCP
 */
SS7SCCP::SS7SCCP(const NamedList& params)
    : SignallingComponent(params,&params), SS7Layer4(SS7MSU::SCCP|SS7MSU::National,&params), Mutex(true,params),
    m_type(SS7PointCode::Other), m_localPointCode(0), m_management(0), m_hopCounter(15),
    m_msgReturnStatus(""), m_segTimeout(0), m_ignoreUnkDigits(false), m_layer3Up(false),
    m_maxUdtLength(220), m_totalSent(0), m_totalReceived(0), m_errors(0),
    m_totalGTTranslations(0), m_gttFailed(0), m_extendedMonitoring(false), m_mgmName("sccp-mgm"),
    m_printMsg(false), m_extendedDebug(false), m_endpoint(true)
{
    DDebug(this,DebugInfo,"Creating new SS7SCCP [%p]",this);
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7SCCP::SS7SCCP(%p) [%p]%s",
	    &params,this,tmp.c_str());
    }
#endif
    const char* stype = params.getValue(YSTRING("pointcodetype"));
    m_type = SS7PointCode::lookup(stype);
    if (m_type == SS7PointCode::Other) {
	Debug(this,DebugConf,"Invalid point code type '%s'",c_safe(stype));
	return;
    }
    String* lpc = params.getParam(YSTRING("localpointcode"));
    m_localPointCode = new SS7PointCode(0,0,0);
    bool pointcodeAssigned = false;
    if (lpc) {
	 if (lpc->find('-') > 0)
	    pointcodeAssigned = m_localPointCode->assign(*lpc,m_type);
	else
	    pointcodeAssigned = m_localPointCode->unpack(m_type,lpc->toInteger());
    }
    if (!pointcodeAssigned) {
	Debug(this,DebugWarn,"Invalid localpointcode='%s'",lpc ? lpc->c_str() : "null");
	Debug(this,DebugConf,"No local PointCode configured!! GT translations with no local PointCode may lead to undesired behavior");
	TelEngine::destruct(m_localPointCode);
	m_localPointCode = 0;
    }
    int hc = params.getIntValue("hopcounter",15);
    if (hc < 1 || hc > 15)
	hc = 15;
    m_hopCounter = hc;
    m_ignoreUnkDigits = params.getBoolValue(YSTRING("ignore-unknown-digits"),true);
    m_printMsg = params.getBoolValue(YSTRING("print-messages"),false);
    m_extendedDebug = params.getBoolValue(YSTRING("extended-debug"),false);
    m_extendedMonitoring = params.getBoolValue(YSTRING("extended-monitoring"),false);
    m_maxUdtLength = params.getIntValue(YSTRING("max-udt-length"),MAX_UDT_LEN);
    m_segTimeout = params.getIntValue(YSTRING("segmentation-timeout"),10000);
    m_mgmName = params.getValue(YSTRING("management"));
    m_endpoint = params.getBoolValue(YSTRING("endpoint"),true);
    if (m_segTimeout < 5000)
	m_segTimeout = 5000;
    if (m_segTimeout > 20000)
	m_segTimeout = 20000;
    if ((m_type == SS7PointCode::ITU || m_type == SS7PointCode::ANSI) && m_localPointCode) {
	NamedList mgmParams("sccp-mgm");
	if (!resolveConfig(YSTRING("management"),mgmParams,&params))
	    mgmParams.addParam("local-config","true");
	mgmParams.setParam("type",m_type == SS7PointCode::ITU ? "ss7-sccp-itu-mgm" : "ss7-sccp-ansi-mgm");
	if (mgmParams.toBoolean(true)) {
	    if (m_type == SS7PointCode::ITU)
		m_management = YOBJECT(SS7ItuSccpManagement,YSIGCREATE(SCCPManagement,&mgmParams));
	    else if (m_type == SS7PointCode::ANSI)
		m_management = YOBJECT(SS7AnsiSccpManagement,YSIGCREATE(SCCPManagement,&mgmParams));
	}
	if (!m_management)
	    Debug(this,DebugWarn,"Failed to create sccp management!");
	else if (m_management->initialize(&mgmParams))
	    m_management->attach(this);
    } else
	Debug(this,DebugConf,"Created SS7SCCP '%p' without management! No local pointcode pressent!",this);

}

SS7SCCP::~SS7SCCP()
{
    if (m_localPointCode)
	m_localPointCode->destruct();
    DDebug(this,DebugAll,"Destroying SS7SCCP [%p]",this);
}

bool SS7SCCP::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7SCCP::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	m_printMsg = config->getBoolValue(YSTRING("print-messages"),m_printMsg);
	m_extendedDebug = config->getBoolValue(YSTRING("extended-debug"),m_extendedDebug);
	m_ignoreUnkDigits = config->getBoolValue(YSTRING("ignore-unknown-digits"),m_ignoreUnkDigits);
	m_maxUdtLength = config->getIntValue(YSTRING("max-udt-length"),m_maxUdtLength);
	m_endpoint = config->getBoolValue(YSTRING("endpoint"),m_endpoint);
	int hc = config->getIntValue("hopcounter",m_hopCounter);
	if (hc < 1 || hc > 15)
	    hc = 15;
	m_hopCounter = hc;
	m_extendedMonitoring = config->getBoolValue(YSTRING("extended-monitoring"),m_extendedMonitoring);
    }
    if (m_management)
	SignallingComponent::insert(m_management);
    return SS7Layer4::initialize(config);
}

void  SS7SCCP::destroyed()
{
    if (m_management)
	TelEngine::destruct(m_management);
    SS7Layer4::destroyed();
}

void SS7SCCP::attach(SS7Layer3* network)
{
    SS7Layer4::attach(network);
    setNetworkUp(network && network->operational());
}

bool SS7SCCP::managementStatus(Type type, NamedList& params)
{
    if (m_management)
	m_management->notify(type,params);
    return false;
}

void SS7SCCP::timerTick(const Time& when)
{
    if (!lock(SignallingEngine::maxLockWait()))
	return;
    for (ObjList* o = m_reassembleList.skipNull();o;) {
        SS7MsgSccpReassemble* usr = YOBJECT(SS7MsgSccpReassemble,o->get());
        if (usr->timeout()) {
            o->remove();
            o = o->skipNull();
        }
        else
            o = o->skipNext();
   }
   unlock();
}

void SS7SCCP::ajustMessageParams(NamedList& params, SS7MsgSCCP::Type type)
{
    if (type == SS7MsgSCCP::UDT || type == SS7MsgSCCP::UDTS)
	return;
    int hopCounter = params.getIntValue(YSTRING("HopCounter"),0);
    if (hopCounter < 1 || hopCounter > 15)
	params.setParam("HopCounter",String(m_hopCounter));
    if (ITU() && params.getParam(YSTRING("Importance"))) {
	int importance = params.getIntValue(YSTRING("Importance"));
	int temp = checkImportanceLevel(type, importance);
	if (importance != temp)
	    params.setParam(YSTRING("Importance"),String(temp));
    }
}

// Called by routing method to send a msu
int SS7SCCP::transmitMessage(SS7MsgSCCP* sccpMsg, bool local)
{
    if (!sccpMsg || !sccpMsg->getData())
	return -1;
    if (unknownPointCodeType()) {
	Debug(this,DebugGoOn,"SCCP unavailable!! Reason Unknown pointcode type %s",SS7PointCode::lookup(m_type));
	return -1;
    }
    Lock lock(this);
    if (!m_layer3Up) {
	DDebug(this,DebugNote,"Can not send sccp message, L3 is down");
	return -1;
    }

    int dpc = getPointCode(sccpMsg,"CalledPartyAddress","RemotePC",true);
    if (dpc == -2) {
	lock.drop();
	return routeLocal(sccpMsg);
    }
    int opc = getPointCode(sccpMsg,"CallingPartyAddress","LocalPC",false);
    lock.drop();
    if (dpc < 0 || opc < 0) {
	if (m_management)
	    m_management->routeFailure(sccpMsg);
	return -1;
    }
    return sendSCCPMessage(sccpMsg,dpc,opc,local);
}

int SS7SCCP::sendSCCPMessage(SS7MsgSCCP* sccpMsg,int dpc,int opc, bool local)
{
    Lock lock(this);
    int sls = sccpMsg->params().getIntValue(YSTRING("sls"),-1);
    SS7PointCode dest(m_type,dpc);
    SS7PointCode orig(m_type,opc > 0 ? opc : m_localPointCode->pack(m_type));
    // Build the routing label
    SS7Label outLabel(m_type,dest,orig,sls);
    if (sccpMsg->getData()->length() > m_maxUdtLength) {
	lock.drop();
	return segmentMessage(sccpMsg,outLabel,local);
    }
    // Check route indicator
    if (!sccpMsg->params().getParam("CalledPartyAddress.route")) {
	// Set route indicator. If have pointcode and ssn, route on ssn
	if (sccpMsg->params().getParam(YSTRING("RemotePC")) &&
		sccpMsg->params().getIntValue(YSTRING("CalledPartyAddress.ssn"),0) != 0) {
	    sccpMsg->params().setParam("CalledPartyAddress.route","ssn");
	} else
	    sccpMsg->params().setParam("CalledPartyAddress.route","gt");
    }
    // Build the msu
    SS7MSU* msu = buildMSU(sccpMsg,outLabel);
    lock.drop();
    if (!msu)
	return segmentMessage(sccpMsg,outLabel,local);
    printMessage(msu,sccpMsg,outLabel);
    sls = transmitMSU(*msu,outLabel,sls);
#ifdef DEBUG
    if (sls < 0)
	Debug(this,DebugNote,"Failed to transmit message %s. %d",SS7MsgSCCP::lookup(sccpMsg->type()),sls);
#endif
    // CleanUp memory
    TelEngine::destruct(msu);
    return sls;
}

bool SS7SCCP::fillLabelAndReason(String& dest,const SS7Label& label,const SS7MsgSCCP* msg)
{
    dest << " Routing label : " << label;
    if (!isSCLCSMessage(msg->type()))
	return false;
    dest << " Reason: ";
    dest << lookup(msg->params().getIntValue(YSTRING("ReturnCause")),s_return_cause,
	    "Unknown");
    return true;
}

// Obtain a pointcode from called/calling party address
// Return: -1 On Error; -2 If the message should be routed to a local sccp; else  the pointcode
int SS7SCCP::getPointCode(SS7MsgSCCP* msg, const String& prefix, const char* pCode, bool translate)
{
    if (!msg)
	return -1;
    bool havePointCode = false;
    NamedString* pcNs = msg->params().getParam(pCode);
    if (pcNs && pcNs->toInteger(0) > 0)
	havePointCode = true;
    if (!havePointCode) {
	pcNs = msg->params().getParam(prefix + ".pointcode");
	if (pcNs && pcNs->toInteger(0) > 0) {
	    msg->params().setParam(new NamedString(pCode,*pcNs));
	    havePointCode = true;
	}
    }
    if (!havePointCode && translate) { // CalledParyAddress with no pointcode. Check for Global Title
	NamedList* route = translateGT(msg->params(),prefix,
		YSTRING("CallingPartyAddress"));
	m_totalGTTranslations++;
	if (!route) {
	    m_gttFailed++;
	    return -1;
	}
	resolveGTParams(msg,route);
	NamedString* localRouting = route->getParam(YSTRING("sccp"));
	if (localRouting && *localRouting != toString()) {
	    msg->params().copyParam(*route,YSTRING("RemotePC"));
	    TelEngine::destruct(route);
	    return -2;
	}
	bool havePC = route->getParam(pCode) != 0;
	NamedString* trpc = route->getParam(YSTRING("pointcode"));
	if (!trpc && !havePC) {
	    Debug(this,DebugWarn,"The GT has not been translated to a pointcode!!");
	    TelEngine::destruct(route);
	    return -1;
	}
	if (!havePC)
	    msg->params().setParam(pCode,*trpc);
	else
	    msg->params().setParam(pCode,route->getValue(pCode));
	TelEngine::destruct(route);
    } else if (!havePointCode && !translate) { // CallingPartyAddress with no pointcode. Assign sccp pointcode
	if (!m_localPointCode) {
	    Debug(this,DebugWarn,"Can not build routing label. No local pointcode present and no pointcode present in CallingPartyAddress");
	    return -1;
	}
	return m_localPointCode->pack(m_type);
    }
    return msg->params().getIntValue(pCode);
}

int SS7SCCP::routeLocal(SS7MsgSCCP* msg)
{
    if (!msg) {
	Debug(this,DebugWarn,"Failed to route local! Null message!");
	return -1;
    }
    NamedString* sccp = msg->params().getParam(YSTRING("sccp"));
    if (!sccp || *sccp == toString()) {
	Debug(this,DebugStub,
		"Requested to local route sccp message without sccp component!");
	return -1;
    }
    int dpc = msg->params().getIntValue("RemotePC",-1);
    if (dpc < 0)
	dpc = msg->params().getIntValue("CalledPartyAddress.pointcode",-1);
    if (dpc < 0) {
	Debug(this,DebugNote,
		"Unable to route local sccp message! No pointcode present.");
	return -1;
    }
    if (!engine()) {
	Debug(this,DebugMild,
		"Unable to route local sccp message! No engine attached!");
	return -1;
    }
    RefPointer<SS7SCCP> sccpCmp = YOBJECT(SS7SCCP,
		engine()->find(*sccp,YSTRING("SS7SCCP")));
    if (!sccpCmp) {
	Debug(this,DebugNote,
		"Unable to route local sccp message! SCCP component %s not found!",
		sccp->c_str());
	return -1;
    }
    msg->params().clearParam(YSTRING("LocalPC"));
    msg->params().clearParam(YSTRING("CallingPartyAddress.pointcode"));
    return sccpCmp->sendSCCPMessage(msg,dpc,-1,false);
}

int SS7SCCP::checkImportanceLevel(int msgType, int initialImportance)
{
    if ((isSCLCMessage(msgType) && isSCLCSMessage(msgType))) {
	Debug(this,DebugStub,"Check Importance level for a SCOC message!");
	return 0;
    }
    if (isSCLCMessage(msgType)) // Max importance level is 6 and default is 4 for UDT,XUDT and LUDT
	return (initialImportance >= 0 && initialImportance <= 6) ? initialImportance : 4;
    if (isSCLCSMessage(msgType)) // Max importance level is 3 and default is 3 for UDTS,XUDTS and LUDTS
	return (initialImportance >= 0 && initialImportance <= 3) ? initialImportance : 3;
    return initialImportance;
}

void SS7SCCP::checkSCLCOptParams(SS7MsgSCCP* msg)
{
    if (!msg || msg->type() == SS7MsgSCCP::UDT || !isSCLCMessage(msg->type())) // UDT does not have optional parameters
	return;
    if (!ITU()) {
	msg->params().clearParam(YSTRING("Importance"));
	return;
    }
    msg->params().clearParam(YSTRING("ISNI"));
    msg->params().clearParam(YSTRING("INS"));
    msg->params().clearParam(YSTRING("MessageTypeInterworking"));
}

// This method is called to send connectionless data
int SS7SCCP::sendMessage(DataBlock& data, const NamedList& params)
{
    if (unknownPointCodeType()) {
	Debug(this,DebugGoOn,"SCCP unavailable!! Reason Unknown pointcode type %s",SS7PointCode::lookup(m_type));
	return -1;
    }
#ifdef XDEBUG
    String tmp;
    params.dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugAll,"SS7SCCP::sendMessage() [%p]%s",this,tmp.c_str());
#endif
    Lock lock1(this);
    SS7MsgSCCP* sccpMsg = 0;
    // Do not check for data length here! If message data is too long the message
    // change procedure will be initiated in segmentMessage method
    if (params.getParam(YSTRING("Importance")) && m_type == SS7PointCode::ITU) {
	// We have Importance optional parameter. Send XUDT. ITU only
	sccpMsg = new SS7MsgSCCP(SS7MsgSCCP::XUDT);
    } else if ((params.getParam(YSTRING("ISNI")) || params.getParam(YSTRING("INS"))) &&
		 m_type == SS7PointCode::ANSI) {
	// XUDT message ANSI only
	sccpMsg = new SS7MsgSCCP(SS7MsgSCCP::XUDT);
    } else if (params.getParam(YSTRING("HopCounter"))) {
	sccpMsg = new SS7MsgSCCP(SS7MsgSCCP::XUDT);
    } else // In rest send Unit Data Messages
	sccpMsg = new SS7MsgSCCP(SS7MsgSCCP::UDT);

    if (!sccpMsg) {
	Debug(this,DebugWarn,"Failed to create SCCP message!");
	m_errors++;
	return -1;
    }
    sccpMsg->params().copyParams(params); // Copy the parameters to message
    sccpMsg->params().setParam("generated","local");
    if (m_localPointCode)
	sccpMsg->params().setParam("LocalPC",String(getPackedPointCode()));
    ajustMessageParams(sccpMsg->params(),sccpMsg->type());
    if (params.getBoolValue(YSTRING("CallingPartyAddress.pointcode"),false) && m_localPointCode)
	sccpMsg->params().setParam("CallingPartyAddress.pointcode",String(getPackedPointCode()));
    // Avoid sending optional parameters that aren't specified by protocol
    if (sccpMsg->type() == SS7MsgSCCP::XUDT || sccpMsg->type() == SS7MsgSCCP::LUDT)
	checkSCLCOptParams(sccpMsg);
    // Append data to message
    sccpMsg->setData(&data);
    lock1.drop();
    int ret = transmitMessage(sccpMsg,true);
    sccpMsg->removeData();
    TelEngine::destruct(sccpMsg);
    lock();
    if (ret >= 0)
	m_totalSent++;
    else
	m_errors++;
    unlock();
    return ret;
}

// This method approximates the length of sccp address
unsigned int SS7SCCP::getAddressLength(const NamedList& params, const String& prefix)
{
    unsigned int length = 2; // Parameter length + Address information octet
    if (params.getParam(prefix + ".ssn"))
	length++; // One octet for ssn
    if (params.getParam(prefix + ".pointcode"))
	length += ITU() ? 2 : 3; // Pointcode has 2 octets on ITU and 3 on ANSI
    const NamedString* gtNr = YOBJECT(NamedString,params.getParam(prefix + ".gt"));
    if (!gtNr)
	return length;
    DataBlock data;
    if (!data.unHexify(*gtNr,gtNr->length(),' ')) {
	length += gtNr->length() / 2 + gtNr->length() % 2;
    } else
	length += data.length();
    const NamedString* nature = YOBJECT(NamedString,params.getParam(prefix + ".gt.nature"));
    const NamedString* translation = YOBJECT(NamedString,params.getParam(prefix + ".gt.translation"));
    const NamedString* plan = YOBJECT(NamedString,params.getParam(prefix + ".gt.plan"));
    const NamedString* encoding = YOBJECT(NamedString,params.getParam(prefix + ".gt.encoding"));
    if (nature)
	length++;
    if (translation)
	length++;
    if (plan && encoding)
	length++;
    return length;
}

void SS7SCCP::getMaxDataLen(const SS7MsgSCCP* msg, const SS7Label& label,
	unsigned int& udt, unsigned int& xudt, unsigned int& ludt)
{
    if (!network()) {
	Debug(this,DebugGoOn,"No Network Attached!!!");
	return;
    }

    unsigned int maxLen = network()->getRouteMaxLength(m_type,label.dpc().pack(m_type));
    if (maxLen < 272) {
	DDebug(this,DebugInfo,"Received MSU size (%d) lower than maximum TDM!",
	       maxLen);
	maxLen = 272;
    }
    bool ludtSupport = maxLen > 272; // 272 maximum msu size
    maxLen -= (label.length() + 1); // subtract label length and SIO octet
    // Now max length represents the maximum length of SCCP message
    // Adjust maxLen to represent maximum data in the message.
    unsigned int headerLength = 3; // MsgType + ProtocolClass
    // Memorize pointer start to adjust data size.
    unsigned int pointersStart = headerLength;
    maxLen -= headerLength;
    // We have 3 mandatory variable parameters CallingAddress, CalledAddress,
    // and Data and the pointer to optional parameters + 1 data length
    headerLength += 5;
    headerLength += getAddressLength(msg->params(), "CalledPartyAddress");
    headerLength += getAddressLength(msg->params(), "CallingPartyAddress");
    ludt = 0;
    unsigned int sccpParamsSize = headerLength - pointersStart;
    // 254 = 255 max data length - 1 hopcounter - 1 optional parameters pointer +
    //       1 data length indicator
    if (maxLen > 254 + sccpParamsSize)
	udt = 255;
    else
	udt = maxLen - sccpParamsSize;
    // Append optional parameters length
    sccpParamsSize += MAX_OPT_LEN;

    if (ludtSupport) {
	unsigned int maxSupported  = ITU() ? MAX_DATA_ITU : MAX_DATA_ANSI;
	if (maxLen < maxSupported) {
	    ludt = maxLen - sccpParamsSize;
	    ludt -= 5; // The pointers and data length are on 2 octets
	} else
	    ludt = maxSupported;
    }
    // 254 represents the maximum value that can be stored
    if (maxLen < 254)
	xudt = maxLen - sccpParamsSize;
    // Adjust data length to make sure that the pointer to optional parameters
    // is not bigger than max unsigned char value
    xudt = 254 - sccpParamsSize;
}

void SS7SCCP::printMessage(const SS7MSU* msu, const SS7MsgSCCP* sccpMsg, const
	SS7Label& label) {

    if (m_printMsg && debugAt(DebugInfo)) {
	String tmp;
	const void* data = 0;
	unsigned int len = 0;
	if (m_extendedDebug && msu) {
	    unsigned int offs = label.length() + 4;
	    data = msu->getData(offs);
	    len = data ? msu->length() - offs : 0;
	}
	String tmp1;
	fillLabelAndReason(tmp1,label,sccpMsg);
	sccpMsg->toString(tmp,label,debugAt(DebugAll),data,len);
	Debug(this,DebugInfo,"Sending message (%p) '%s' %s %s",sccpMsg,
		SS7MsgSCCP::lookup(sccpMsg->type()),tmp1.c_str(),tmp.c_str());
    } else if (debugAt(DebugAll)) {
	String tmp;
	bool debug = fillLabelAndReason(tmp,label,sccpMsg);
	Debug(this,debug ? DebugInfo : DebugAll,"Sending message '%s' %s",
		sccpMsg->name(),tmp.c_str());
    }
}

ObjList* SS7SCCP::getDataSegments(unsigned int dataLength,
	unsigned int maxSegmentSize)
{
    DDebug(DebugAll,"getDataSegments(%u,%u)",dataLength,maxSegmentSize);
    ObjList* segments = new ObjList();
    // The first sccp segment must be the largest
    int segmentSize = maxSegmentSize - 1;
    int dataLeft = dataLength;
    unsigned int totalSent = 0;
    int sgSize = maxSegmentSize;
    if (dataLength - maxSegmentSize <= MIN_DATA_SIZE)
	sgSize = maxSegmentSize - MIN_DATA_SIZE;
    segments->append(new SS7SCCPDataSegment(0,sgSize));
    dataLeft -= sgSize;
    totalSent += sgSize;
    while (dataLeft > 0) {
	sgSize = 0;
	if ((dataLeft - segmentSize) > MIN_DATA_SIZE) { // Make sure that the left segment is longer than 2
	    sgSize = segmentSize;
	} else if (dataLeft > segmentSize) {
	    sgSize = segmentSize - MIN_DATA_SIZE;
	} else {
	    sgSize = dataLeft;
	}
	XDebug(this,DebugAll,"Creating new data segment total send %d, segment size %d",
	       totalSent,sgSize);
	segments->append(new SS7SCCPDataSegment(totalSent,sgSize));
	dataLeft -= sgSize;
	totalSent += sgSize;
    }
    return segments;
}

SS7SCCPDataSegment* getAndRemoveDataSegment(ObjList* obj)
{
    if (!obj)
	return 0;
    ObjList* o = obj->skipNull();
    if (!o)
	return 0;
    SS7SCCPDataSegment* sgm = static_cast<SS7SCCPDataSegment*>(o->get());
    obj->remove(sgm,false);
    return sgm;
}

int SS7SCCP::segmentMessage(SS7MsgSCCP* origMsg, const SS7Label& label, bool local)
{
    if (!origMsg)
	return -1;
    unsigned int udtLength = 0;
    unsigned int xudtLength = 0;
    unsigned int ludtLength = 0;
    getMaxDataLen(origMsg,label,udtLength,xudtLength,ludtLength);
    unsigned int dataLen = 0;

    DDebug(this,DebugInfo, "Got max data len : udt (%d) : xudt (%d) ludt (%d)",
	   udtLength,xudtLength,ludtLength);
    if (udtLength < 2 && xudtLength < 2 && ludtLength < 2)
	return -1;
    int sls = origMsg->params().getIntValue(YSTRING("sls"),-1);
    DataBlock* data = origMsg->getData();
    if (!data)
	return -1;
    // Verify if we should bother to send the message
    if (data->length() > (ITU() ? MAX_DATA_ITU : MAX_DATA_ANSI)) {
	Debug(this,DebugNote,
	      "Unable to send SCCP message! Data length (%d) is too long",
	      data->length());
	return -1;
    }

    SS7MsgSCCP::Type msgType = origMsg->type();
    if (data->length() <= udtLength && origMsg->canBeUDT()) {
	msgType = isSCLCMessage(msgType) ? SS7MsgSCCP::UDT : SS7MsgSCCP::UDTS;
	dataLen = udtLength;
    } else if (data->length() <= xudtLength) {
	msgType = isSCLCMessage(msgType) ? SS7MsgSCCP::XUDT : SS7MsgSCCP::XUDTS;
	dataLen = xudtLength;
    } else if (data->length() <= ludtLength) {
	msgType = isSCLCMessage(msgType) ? SS7MsgSCCP::LUDT : SS7MsgSCCP::LUDTS;
	dataLen = ludtLength;
    } else { // Segmentation is needed!!!
	if (ludtLength > 2) { // send ludt
	    msgType = isSCLCMessage(msgType) ? SS7MsgSCCP::LUDT : SS7MsgSCCP::LUDTS;
	    dataLen = ludtLength;
	} else if (xudtLength > 2) { // Send Ludt
	    msgType = isSCLCMessage(msgType) ? SS7MsgSCCP::XUDT : SS7MsgSCCP::XUDTS;
	    dataLen = xudtLength;
	} else {
	    Debug(this,DebugWarn,
		  "Unable to segment message!! Invalid data len params! XUDT data len = %d, LUDT data len = %d",
		  xudtLength,ludtLength);
	}
    }
    origMsg->updateType(msgType);
    origMsg->params().clearParam(YSTRING("Segmentation"),'.');
    // Send the message if it fits in a single message
    if (data->length() <= dataLen) {
	Lock lock(this);
	ajustMessageParams(origMsg->params(),origMsg->type());
	SS7MSU* msu = buildMSU(origMsg,label,false);
	if (!msu) {
	    Debug(this,DebugGoOn,"Failed to build msu from sccpMessage %s",
		SS7MsgSCCP::lookup(origMsg->type()));
	    return -1;
	}
	printMessage(msu,origMsg,label);
	lock.drop();
	sls = transmitMSU(*msu,label,sls);
#ifdef DEBUG
	if (sls < 0)
	    Debug(this,DebugNote,"Failed to transmit message %s. %d",
		  SS7MsgSCCP::lookup(origMsg->type()),sls);
#endif
	// CleanUp memory
	TelEngine::destruct(msu);
	return sls;
    }
    // Verify if we should bother to segment the message
    if ((data->length() > 16 * (dataLen - 1)) && !isSCLCSMessage(msgType)) {
	Debug(DebugNote,
	      "Unable to segment SCCP message! Data length (%d) excedes max data allowed (%d)",
	      data->length(),(16 * (dataLen - 1)));
	return -1;
    }

    // Start segmentation process
    lock();
    ObjList* listSegments = getDataSegments(data->length(),dataLen);

    // Build message params
    NamedList msgData("");
    msgData.copyParams(origMsg->params());
    ajustMessageParams(msgData,msgType);

    // Set segmentation local reference for this message
    msgData.setParam("Segmentation","");
    if (!msgData.getParam(YSTRING("Segmentation.SegmentationLocalReference")))
	msgData.setParam("Segmentation.SegmentationLocalReference",String((u_int32_t)Random::random()));
    int segments = listSegments->count();
    msgData.setParam("Segmentation.ProtocolClass",msgData.getValue(YSTRING("ProtocolClass")));
    if (isSCLCMessage(msgType))
	msgData.setParam("ProtocolClass","1"); // Segmentation is using in sequence delivery option
    bool msgReturn = msgData.getBoolValue(YSTRING("MessageReturn"),false);
    sls = msgData.getIntValue(YSTRING("sls"),-1);

    // Transmit first segment
    SS7MsgSCCP* msg = new SS7MsgSCCP(msgType);
    msg->params().copyParams(msgData);
    DataBlock temp;
    SS7SCCPDataSegment* sg = getAndRemoveDataSegment(listSegments);
    if (!sg) {
	Debug(DebugStub,"Unable to extract first data segment!!!");
	TelEngine::destruct(msg);
	TelEngine::destruct(listSegments);
	return -1;
    }
    sg->fillSegment(temp,*data);
    msg->params().setParam("Segmentation.RemainingSegments",
	    String(isSCLCMessage(msgType) ? --segments : 0));
    msg->params().setParam("Segmentation.FirstSegment","true");
    msg->setData(&temp);
    SS7MSU* msu = buildMSU(msg,label,false);
    msg->removeData();
    temp.clear(false);
    if (!msu) {
	Debug(this,DebugGoOn,"Failed to build msu from sccpMessage %s",
		SS7MsgSCCP::lookup(msgType));
	TelEngine::destruct(msg);
	TelEngine::destruct(listSegments);
	return -1;
    }
    printMessage(msu,msg,label);
    unlock();
    sls = transmitMSU(*msu,label,sls);
#ifdef DEBUG
    if (sls < 0)
	Debug(this,DebugNote,"Failed to transmit message %s. %d",
		SS7MsgSCCP::lookup(msgType),sls);
#endif
    TelEngine::destruct(msu);
    TelEngine::destruct(msg);
    TelEngine::destruct(sg);
    if (sls < 0) {
	if (msgReturn && !local)
	    returnMessage(origMsg,MtpFailure);
	Debug(this,DebugNote,"Failed to transmit first segment of message");
	TelEngine::destruct(listSegments);
	return sls;
    }
    if (isSCLCSMessage(msgType)) {
	TelEngine::destruct(listSegments);
	return sls;
    }
    lock();
    msgData.setParam("Segmentation.FirstSegment","false");
    // Set message return option only for the first segment
    msgData.setParam("MessageReturn","false");
    while ((sg = getAndRemoveDataSegment(listSegments))) {
	msg = new SS7MsgSCCP(msgType);
	msg->params().copyParams(msgData);
	sg->fillSegment(temp,*data);
	TelEngine::destruct(sg);
	msg->params().setParam("Segmentation.RemainingSegments",String(--segments));
	msg->setData(&temp);
	SS7MSU* msu = buildMSU(msg,label,false);
	msg->removeData();
	temp.clear(false);
	if (!msu) {
	    Debug(this,DebugGoOn,"Failed to build msu from sccpMessage %s",
		    SS7MsgSCCP::lookup(msgType));
	    TelEngine::destruct(msg);
	    TelEngine::destruct(listSegments);
	    return -1;
	}
	printMessage(msu,msg,label);
	unlock();
	sls = transmitMSU(*msu,label,sls);
#ifdef DEBUG
	if (sls < 0)
	    Debug(this,DebugNote,"Failed to transmit message %s. %d",
		    SS7MsgSCCP::lookup(msgType),sls);
#endif
	TelEngine::destruct(msg);
	TelEngine::destruct(msu);
	if (sls < 0) {
	    if (msgReturn && !local)
		returnMessage(origMsg,MtpFailure);
	    Debug(this,DebugNote,"Failed to transmit segment of %s message remaining segments %d",
		  SS7MsgSCCP::lookup(msgType),segments);
	    return sls;
	}
	lock();
    }
    if (segments != 0)
	Debug(this,DebugStub,"Bug in segment message!! RemainingSegments %d",segments);
    TelEngine::destruct(listSegments);
    unlock();
    return sls;
}

SS7MsgSccpReassemble::Return SS7SCCP::reassembleSegment(SS7MsgSCCP* segment,
	    const SS7Label& label, SS7MsgSCCP*& msg)
{
    if (segment->params().getBoolValue(YSTRING("Segmentation.FirstSegment"))) {
	for (ObjList* o = m_reassembleList.skipNull(); o; o = o->skipNext()) {
	    SS7MsgSccpReassemble* reass = static_cast <SS7MsgSccpReassemble*>(o->get());
	    if (!reass || !reass->canProcess(segment,label))
		continue;
	    m_reassembleList.remove(reass);
	    DDebug(this,DebugNote,"Duplicate first segment received!");
	    return SS7MsgSccpReassemble::Error;
	}
	SS7MsgSccpReassemble* reass = new SS7MsgSccpReassemble(segment,label,m_segTimeout);
	m_reassembleList.append(reass);
	return SS7MsgSccpReassemble::Accepted;
    }

    SS7MsgSccpReassemble::Return ret = SS7MsgSccpReassemble::Rejected;
    for (ObjList* o = m_reassembleList.skipNull(); o; o = o->skipNext()) {
	SS7MsgSccpReassemble* reass = static_cast <SS7MsgSccpReassemble*>(o->get());
	if (!reass)
	    continue;
	ret = reass->appendSegment(segment,label);
	if (ret == SS7MsgSccpReassemble::Rejected)
	    continue;
	if (ret == SS7MsgSccpReassemble::Error) {
	    m_reassembleList.remove(reass,false);
	    msg = reass;
	    return ret;
	}
	if (ret == SS7MsgSccpReassemble::Finished) {
	    m_reassembleList.remove(reass,false);
	    msg = reass;
	}
	return ret;
    }
    return ret;
}

SS7MSU* SS7SCCP::buildMSU(SS7MsgSCCP* msg, const SS7Label& label, bool checkLength) const
{
    // see what mandatory parameters we should put in this message
    const MsgParams* msgParams = getSccpParams(msg->type());
    if (!msgParams) {
	const char* name = SS7MsgSCCP::lookup(msg->type());
	if (name)
	    Debug(this,DebugWarn,"No parameter table for SCCP MSU type %s [%p]",name,this);
	else
	    Debug(this,DebugWarn,"Cannot create SCCP MSU type 0x%02x [%p]",msg->type(),this);
	return 0;
    }
    unsigned int len = 1;

    const SS7MsgSCCP::Parameters* plist = msgParams->params;
    SS7MsgSCCP::Parameters ptype;
    // first add the length of mandatory fixed parameters
    while ((ptype = *plist++) != SS7MsgSCCP::EndOfParameters) {
	const SCCPParam* param = getParamDesc(ptype);
	if (!param) {
	    // this is fatal as we don't know the length
	    Debug(this,DebugGoOn,"Missing description of fixed SCCP parameter 0x%02x [%p]",ptype,this);
	    return 0;
	}
	if (!param->size) {
	    Debug(this,DebugGoOn,"Invalid (variable) description of fixed SCCP parameter 0x%02x [%p]",ptype,this);
	    return 0;
	}
	len += param->size;
    }
    bool ludt = msg->isLongDataMessage();
    int pointerLen = ludt ? 2 : 1;
    // initialize the pointer array offset just past the mandatory fixed part
    unsigned int ptr = label.length() + 1 + len;
    // then add one pointer octet to each mandatory variable parameter
    while ((ptype = *plist++) != SS7MsgSCCP::EndOfParameters) {
	const SCCPParam* param = getParamDesc(ptype);
	if (!param) {
	    // this is fatal as we won't be able to populate later
	    Debug(this,DebugGoOn,"Missing description of variable SCCP parameter 0x%02x [%p]",ptype,this);
	    return 0;
	}
	if (param->size)
	    Debug(this,DebugMild,"Invalid (fixed) description of variable SCCP parameter 0x%02x [%p]",ptype,this);
	len += pointerLen;
    }
    // finally add a pointer to the optional part only if supported by type
    if (msgParams->optional)
	len += pointerLen;
    SS7MSU* msu = new SS7MSU(sio(),label,0,len);
    unsigned char* d = msu->getData(label.length()+1,len);
    *d++ = msg->type();
    ObjList exclude;
    plist = msgParams->params;
    String prefix = msg->params().getValue(YSTRING("message-prefix"));
    // first populate with mandatory fixed parameters
    while ((ptype = *plist++) != SS7MsgSCCP::EndOfParameters) {
	const SCCPParam* param = getParamDesc(ptype);
	if (!param) {
	    Debug(this,DebugFail,"Stage 2: no description of fixed SCCP parameter 0x%02x [%p]",ptype,this);
	    continue;
	}
	if (!param->size) {
	    Debug(this,DebugFail,"Stage 2: Invalid (variable) description of fixed SCCP parameter %s [%p]",param->name,this);
	    continue;
	}
	if (!encodeParam(this,*msu,param,&msg->params(),exclude,prefix,d))
	    Debug(this,DebugGoOn,"Could not encode fixed SCCP parameter %s [%p]",param->name,this);
	d += param->size;
    }
    // now populate with mandatory variable parameters
    for (; (ptype = *plist++) != SS7MsgSCCP::EndOfParameters; ptr += pointerLen) {
	const SCCPParam* param = getParamDesc(ptype);
	if (!param) {
	    Debug(this,DebugFail,"Stage 2: no description of variable SCCP parameter 0x%02x [%p]",ptype,this);
	    continue;
	}
	if (param->size) {
	    Debug(this,DebugFail,"Stage 2: Invalid (fixed) description of variable SCCP parameter %s [%p]",param->name,this);
	    continue;
	}
	// remember the offset this parameter will actually get stored
	len = msu->length();
	unsigned int size = 0;
	if (ptype == SS7MsgSCCP::Data || ptype == SS7MsgSCCP::LongData) {
	    size = encodeData(this,*msu,msg);
	    if (ptype == SS7MsgSCCP::Data) {
		// Data parameter is the last of variable mandatory parameters
		// Check if the pointer to variable part may be bigger than 255
		// (max unsigned char value)
		if (checkLength && ((len + size + MAX_OPT_LEN) > 254)) {
		    TelEngine::destruct(msu);
		    return 0;
		}
	    }
	} else
	    size = encodeParam(this,*msu,param,&msg->params(),exclude,prefix);
	d = msu->getData(0,len+1);
	if (!(size && d)) {
	    Debug(this,DebugGoOn,"Could not encode variable SCCP parameter %s [%p]",param->name,this);
	    continue;
	}
	if (ptype != SS7MsgSCCP::LongData && ((d[len] != size) || (msu->length() != (len + size + 1)))) {
	    Debug(this,DebugGoOn,"Invalid encoding variable SCCP parameter %s (len=%u size=%u stor=%u msuLength = %u) [%p]",
		param->name,len,size,d[len],msu->length(),this);
	    continue;
	}
	// store pointer to parameter
	unsigned int storedLength = len - ptr;
	if (!ludt) {
	    d[ptr] = storedLength;
	    continue;
	}
	storedLength --;
	d[ptr] = storedLength & 0xff;
	d[ptr+1] = storedLength >> 8;
    }
    if (msgParams->optional) {
	// remember the offset past last mandatory == first optional parameter
	len = msu->length();
	// optional parameters are possible - try to set anything left in the message
	unsigned int n = msg->params().length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = msg->params().getParam(i);
	    if (!ns || exclude.find(ns))
		continue;
	    if (prefix && !ns->name().startsWith(prefix))
		continue;
	    String tmp(ns->name());
	    tmp >> prefix.c_str();
	    const SCCPParam* param = getParamDesc(tmp);
	    unsigned char size = 0;
	    if (param)
		size = encodeParam(this,*msu,param,ns,&msg->params(),prefix);
	    else if (tmp.startSkip("Param_",false)) {
		int val = tmp.toInteger(-1);
		if (val >= 0 && val <= 255) {
		    SCCPParam p;
		    p.name = tmp;
		    p.type = (SS7MsgSCCP::Parameters)val;
		    p.size = 0;
		    p.encoder = 0;
		    size = encodeParam(this,*msu,&p,ns,&msg->params(),prefix);
		}
	    }
	    if (!size)
		continue;
	    if (len) {
		d = msu->getData(0,len+1);
		unsigned int storedLength = len - ptr;
		if (ludt) {
		    storedLength --;
		    d[ptr] = storedLength & 0xff;
		    d[ptr+1] = storedLength >> 8;
		} else {
		    // Do not try to set the pointer to optional parameters
		    // if is bigger than max unsigned char value because will
		    // result in a malformed packet!
		    if (storedLength > 255) {
			Debug(this,checkLength ? DebugAll : DebugStub,
			      "Build MSU the pointer to optional parameters is bigger than 255!!!! %d",
			      storedLength);
			TelEngine::destruct(msu);
			return 0;
		    }
		    d[ptr] = storedLength;
		}
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

HandledMSU SS7SCCP::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != sif()) // SCCP message?
	return HandledMSU::Rejected;
    Lock lock(this);
    if (unknownPointCodeType()) {
	DDebug(this,DebugNote,"Rejecting MSU! Reason Unknown pointcode type");
	lock.drop();
	return HandledMSU::Rejected;
    }
    if (m_localPointCode && *m_localPointCode != label.dpc()) { // Is the msu for us?
	lock.drop();
	return HandledMSU::Rejected;
    }
    lock.drop();
    const unsigned char* s = msu.getData(label.length() + 1,1);
    if (!s) {
	Debug(this,DebugNote,"Got short MSU");
	return false;
    }
    unsigned int len = msu.length()-label.length()-1;
    SS7MsgSCCP::Type type = (SS7MsgSCCP::Type)s[0];
    String name = SS7MsgSCCP::lookup(type);
    if (!name) {
        String tmp;
	tmp.hexify((void*)s,len,' ');
	DDebug(this,DebugMild,"Received unknown SCCP type 0x%02x, length %u: %s",
	    type,len,tmp.c_str());
	return false;
    }
    bool ok = processMSU(type,s+1,len-1,label,network,sls);
    if (!ok && debugAt(DebugMild)) {
	String tmp;
	tmp.hexify((void*)s,len,' ');
	Debug(this,DebugMild,"Unhandled SCCP message %s,  length %u: %s",
	    name.c_str(),len,tmp.c_str());
    }
    return ok;
}

bool SS7SCCP::processMSU(SS7MsgSCCP::Type type, const unsigned char* paramPtr,
	unsigned int paramLen, const SS7Label& label, SS7Layer3* network, int sls)
{
    XDebug(this,DebugAll,"SS7SCCP::processMSU(%u,%p,%u,%p,%p,%d) [%p]",
	type,paramPtr,paramLen,&label,network,sls,this);

    Lock lock(this);
    SS7MsgSCCP* msg = new SS7MsgSCCP(type);
    if (!decodeMessage(msg,label.type(),paramPtr,paramLen)) {
	DDebug(this,DebugNote,"Failed to decode SCCP message!");
	m_errors++;
	TelEngine::destruct(msg);
	return false;
    }
    msg->params().setParam("LocalPC",String(label.dpc().pack(m_type)));
    msg->params().setParam("RemotePC",String(label.opc().pack(m_type)));
    msg->params().setParam("generated","remote");
    // Set the sls in case of STP routing for sequence control
    msg->params().setParam("sls",String(label.sls()));
    if (m_printMsg && debugAt(DebugInfo)) {
	String tmp;
	msg->toString(tmp,label,debugAt(DebugAll),
	    m_extendedDebug ? paramPtr : 0,paramLen);
	String tmp1;
	fillLabelAndReason(tmp1,label,msg);
	Debug(this,DebugInfo,"Received message (%p) '%s' %s %s",
	      msg,SS7MsgSCCP::lookup(msg->type()),tmp1.c_str(),tmp.c_str());
    } else if (debugAt(DebugAll)) {
	String tmp;
	bool debug = fillLabelAndReason(tmp,label,msg);
	Debug(this,debug ? DebugInfo : DebugAll,"Received message '%s' %s",
	    msg->name(),tmp.c_str());
    }
    // Form here something will happened with the message!
    // return true
    m_totalReceived++;
    int protocolClass = msg->params().getIntValue(YSTRING("ProtocolClass"), -1);
    if (isSCOCMsg(msg->type())) {
	Debug(DebugWarn,"Received Connection oriented message!!");
	if (msg->type() != SS7MsgSCCP::CR) { // Received Connection Oriented message other than Connect Request
	    // Drop the message
	    DDebug(this,DebugNote,"Received message %s without a connection!",SS7MsgSCCP::lookup(msg->type()));
	    TelEngine::destruct(msg);
	    return true;
	}
	// Send Connection Refused
	SS7MsgSCCP* ref = new SS7MsgSCCP(SS7MsgSCCP::CREF);
	ref->params().setParam("DestinationLocalReference",msg->params().getValue(YSTRING("SourceLocalReference")));
	ref->params().setParam("RefusalCause",String(0x13)); // Unequipped user
	SS7Label outLabel(label.type(),label.opc(),label.dpc(),label.sls());
	SS7MSU* msu = buildMSU(ref,outLabel);
	if (!msu)
	    Debug(this,DebugWarn,"Failed to build msu from sccpMessage %s",
		SS7MsgSCCP::lookup(ref->type()));
	lock.drop();
	transmitMSU(*msu,outLabel,outLabel.sls());
	TelEngine::destruct(msu);
	TelEngine::destruct(ref);
	TelEngine::destruct(msg);
	return true;
    }
    // If the Calling party address does not contain route information
    // Set OPC as Calling Party Address pointcode
    if (((protocolClass == 0 || protocolClass == 1) &&
	    isSCLCMessage(msg->type())) || isSCLCSMessage(msg->type())) { // ConnectionLess message
	lock.drop();
	routeSCLCMessage(msg,label);
    } else {
	Debug(this,DebugMild,"Received bad message! Inconsistence between msg type %s and protocol class %d",
		SS7MsgSCCP::lookup(msg->type()),protocolClass);
    }
    TelEngine::destruct(msg);
    return true;
}

// Process a SCCP message! return false if an error was detected
bool SS7SCCP::routeSCLCMessage(SS7MsgSCCP*& msg, const SS7Label& label)
{
    Lock lock(this);
    if (!msg) {
	Debug(this,DebugWarn,"Request to route null sccp message");
	m_errors++;
	return false;
    }
    if (msg->params().getParam(YSTRING("Segmentation"))) {
	// Verify if we had received Segmentation parameter with only one segment
	// and let it pass trough
	// The reassamblation of XUTDS and LUDTS is optional but, for code flow
	// purpose, we are manageing it.
	if (msg->params().getIntValue(YSTRING("Segmentation.RemainingSegments"),0) != 0 ||
		!msg->params().getBoolValue(YSTRING("Segmentation.FirstSegment"),true)) {
	    // We have segmentation parameter with multiple segments
	    SS7MsgSCCP* finishead = 0;
	    int ret = reassembleSegment(msg,label,finishead);
	    if (ret == SS7MsgSccpReassemble::Accepted ||
		    ret == SS7MsgSccpReassemble::Rejected)
		return true;
	    if (ret == SS7MsgSccpReassemble::Error) {
		// For XUDTS and LUDTS messages the message return should allways
		// be false
		if (finishead && finishead->params().getBoolValue(YSTRING("MessageReturn"),false))
		    returnMessage(finishead,SegmentationFailure);
		if (finishead)
		    TelEngine::destruct(finishead);
		m_errors++;
		return true;
	    }
	    if (!finishead) {
		Debug(this,DebugStub,"Sccp Message finishead to reassemble but the message was not returned");
		return true;
	    }
	    TelEngine::destruct(msg);
	    msg = finishead;
	}
    }
    int errorCode = -1;
    NamedString* route = msg->params().getParam(YSTRING("CalledPartyAddress.route"));
    bool msgReturn = msg->params().getBoolValue(YSTRING("MessageReturn"),false);
    bool informManagement = false;
    while (route && *route != YSTRING("ssn")) {
	if (!msg->params().getParam(YSTRING("CalledPartyAddress.gt"))) {
	    if (m_endpoint && msg->params().getParam(YSTRING("CalledPartyAddress.ssn")))
		break; // If we are endpoint and we have a ssn try to process the message
	    Debug(this,DebugInfo,"Message requested to be routed on gt but no gt present!");
	    break;
	}
	NamedList* gtRoute = translateGT(msg->params(),"CalledPartyAddress",
		"CallingPartyAddress");
	m_totalGTTranslations++;
	if (!gtRoute) {
	    if (m_endpoint && msg->params().getParam(YSTRING("CalledPartyAddress.ssn")))
		break; // If we are endpoint and we have a ssn try to process the message
	    m_gttFailed++;
	    errorCode = NoTranslationSpecificAddress;
	    Debug(this,DebugInfo,"No Gt Found for : %s, or all routes are down!",
		  msg->params().getValue(YSTRING("CalledPartyAddress.gt")));
	    break;
	}
	resolveGTParams(msg,gtRoute);
	NamedString* localRouting = gtRoute->getParam(YSTRING("sccp"));
	if (localRouting && *localRouting != toString()) {
	    msg->params().copyParam(*gtRoute,YSTRING("RemotePC"));
	    TelEngine::destruct(gtRoute);
	    lock.drop();
	    return routeLocal(msg) >= 0;
	}
	bool haveRemotePC = gtRoute->getParam(YSTRING("RemotePC")) != 0;
	if (!gtRoute->getParam(YSTRING("pointcode")) && !haveRemotePC) {
	    if (m_endpoint) {
		// If we have an ssn try to process the message
		if (msg->params().getParam(YSTRING("CalledPartyAddress.ssn")))
		    break;
		if (gtRoute->getParam(YSTRING("ssn"))) {
		    msg->params().setParam("CalledPartyAddress.ssn",gtRoute->getValue(YSTRING("ssn")));
		    break;
		}
	    }
	    Debug(this,DebugWarn,"The GT has not been translated to a pointcode!!");
	    TelEngine::destruct(gtRoute);
	    errorCode = NoTranslationAddressNature;
	    break;
	}
	msg->params().clearParam(YSTRING("CalledPartyAddress"),'.');
	for (unsigned int i = 0;i < gtRoute->length();i++) {
	    NamedString* val = gtRoute->getParam(i);
    	    if (val && (val->name().startsWith("gt") || val->name() == YSTRING("pointcode") ||
		    val->name() == YSTRING("ssn") || val->name() == YSTRING("route")))
		msg->params().setParam("CalledPartyAddress." + val->name(),*val);
	}
	int pointcode = haveRemotePC ? gtRoute->getIntValue(YSTRING("RemotePC")) :
	    msg->params().getIntValue(YSTRING("CalledPartyAddress.pointcode"));

	TelEngine::destruct(gtRoute);
	if (msg->params().getIntValue(YSTRING("CalledPartyAddress.ssn"),-1) == 1) {
	    Debug(this,DebugNote,"GT Routing Warn!! Message %s global title translated for management!",
		  SS7MsgSCCP::lookup(msg->type()));
	    m_errors++;
	    return false; // Management message with global title translation
	}
	if (!m_localPointCode)
	    Debug(this,DebugConf,
		  "No local PointCode configured!! GT translations with no local PointCode may lead to undesired behavior");
	if (msg->params().getParam(YSTRING("HopCounter"))) {
	    int hopcounter = msg->params().getIntValue(YSTRING("HopCounter"));
	    hopcounter --;
	    if (hopcounter <= 0) {
		errorCode = HopCounterViolation;
		break;
	    }
	    msg->params().setParam("HopCounter",String(hopcounter));
	}
	// If from the translated gt resulted a pointcode other then ours forward the message
	if (pointcode > 0 && m_localPointCode && (unsigned int)pointcode != m_localPointCode->pack(m_type)) {
	    msg->params().setParam("RemotePC",String(pointcode));
	    lock.drop();
	    if (transmitMessage(msg) >= 0)
		return true;
	    informManagement = true;
	    errorCode = MtpFailure;
	}
	break;
    }
    if (errorCode >= 0) {
	m_errors++;
	lock.drop();
	if (informManagement && m_management)
	    m_management->routeFailure(msg);
	if (msgReturn)
	    returnMessage(msg,errorCode);
	else
	    Debug(this,DebugInfo,"Dropping message %s. Reason: %s",
		    SS7MsgSCCP::lookup(msg->type()),lookup(errorCode,s_return_cause));
	return false;
    }
    int ssn = msg->params().getIntValue(YSTRING("CalledPartyAddress.ssn"),-1);
    errorCode = SccpFailure;
    while (ssn > 0) {
	if (ssn == 0) {
	    Debug(this,DebugNote,"Requested user with ssn 0!");
	    errorCode = UnequippedUser;
	    break;
	}
	if (ssn == 1) { // Local Management message ?
	    while (true) {
		int protocolClass = msg->params().getIntValue(YSTRING("ProtocolClass"));
		// SCCP management messages need to have protocol class 0 with no special options
		if (protocolClass != 0 || msgReturn)
		    break;
		// Remote SSN must be management SSN (1)
		if (msg->params().getIntValue(YSTRING("CallingPartyAddress.ssn"),-1) != 1)
		    break;
		if (m_management) {
		    lock.drop();
		    return m_management->processMessage(msg);
		}
		break;
	    }
#ifdef DEBUG
	    String tmp;
	    msg->params().dump(tmp,"\r\n  ",'\'',true);
	    Debug(this,DebugNote,"Received invalid SCCPManagement message! %s",tmp.c_str());
#endif
	    m_errors++;
	    return false;
	}
	// If we are here that means that the message is for local processing!
	switch (msg->type()) {
	    case SS7MsgSCCP::XUDT:
	    case SS7MsgSCCP::LUDT:
	    case SS7MsgSCCP::UDT:
		lock.drop();
		{
		    int ret = pushMessage(*msg->getData(),msg->params(),ssn);
		    if (ret == HandledMSU::Accepted)
			return true;
		    if (m_management)
			m_management->subsystemFailure(msg,label);
		    errorCode = (ret == HandledMSU::Unequipped) ? UnequippedUser : SubsystemFailure;
		}
		break;
	    case SS7MsgSCCP::XUDTS:
	    case SS7MsgSCCP::LUDTS:
	    case SS7MsgSCCP::UDTS:
		if (m_extendedMonitoring)
		    archiveMessage(msg);
		DDebug(this,DebugAll,"Received service message %s. Reason: %s",
			SS7MsgSCCP::lookup(msg->type()),lookup(msg->params().getIntValue(YSTRING("ReturnCause")),s_return_cause));
		msg->params().setParam("location","remote");
		lock.drop();
		notifyMessage(*msg->getData(),msg->params(),ssn);
		// Do not bother to verify the return code, because there is nothing that we can do for service messages
		return true;
	    default:
		Debug(this,DebugWarn,"Received unknown SCLC msg type %d",msg->type());
		errorCode = ErrorInLocalProcessing;
		break;
	}
	break;
    }
    m_errors++;
    lock.drop();
    if (msgReturn)
	returnMessage(msg,errorCode);
    else
	Debug(this,DebugInfo,"Dropping message %s. Reason: %s",
	      SS7MsgSCCP::lookup(msg->type()),lookup(errorCode,s_return_cause));
    return false;
}

void SS7SCCP::returnMessage(SS7MsgSCCP* message, int error)
{
    DDebug(this,DebugInfo,"Returning message %s! reason : %s",SS7MsgSCCP::lookup(message->type()),lookup(error,s_return_cause));
    if (!message) {
	DDebug(this,DebugNote,"Message return method called for a null message!!");
	return;
    }
    if (!message->getData()) {
	DDebug(this,DebugWarn,"Message Return initiated with no data parameter");
	return;
    }
    SS7MsgSCCP* msg = 0;
    switch (message->type()) {
	case SS7MsgSCCP::UDT:
	    msg = new SS7MsgSCCP(SS7MsgSCCP::UDTS);
	    break;
	case SS7MsgSCCP::XUDT:
	    msg = new SS7MsgSCCP(SS7MsgSCCP::XUDTS);
	    break;
	case SS7MsgSCCP::LUDT:
	    msg = new SS7MsgSCCP(SS7MsgSCCP::LUDTS);
	    break;
	default:
	    DDebug(this,DebugInfo,"Message return procedure initiated for wrong message type %s",
			SS7MsgSCCP::lookup(msg->type()));
	    return;
    }
    if (!msg) {
	Debug(this,DebugStub,"Implementation bug!! null SCCP message");
	return;
    }
    msg->params().copyParams(message->params());
    switchAddresses(message->params(),msg->params());
    msg->params().setParam("ReturnCause",String(error));
    msg->setData(message->getData());
    msg->params().clearParam(YSTRING("ProtocolClass"),'.');
    msg->params().clearParam(YSTRING("Segmentation"),'.');
    msg->params().clearParam(YSTRING("MessageReturn"),'.');
    if (msg->params().getParam(YSTRING("Importance")))
	msg->params().setParam("Importance","3"); // Default value for service messages
    if (msg->params().getParam(YSTRING("HopCounter")))
	msg->params().setParam("HopCounter",String(m_hopCounter));
    transmitMessage(msg,true);
    msg->removeData();
    TelEngine::destruct(msg);
}

void SS7SCCP::switchAddresses(const NamedList& source, NamedList& dest)
{
    // First remove the called and calling party address from dest
    dest.clearParam(YSTRING("CalledPartyAddress"),'.');
    dest.clearParam(YSTRING("CallingPartyAddress"),'.');
    dest.clearParam(YSTRING("LocalPC"));
    dest.clearParam(YSTRING("RemotePC"));
    if (source.getParam(YSTRING("LocalPC")))
	dest.setParam("LocalPC",source.getValue(YSTRING("LocalPC")));
    // Do not set RemotePC because the message can fail after a gt was performed
    // and than RemotePC represents message destination pc rather then
    // originating pc. Obtain return address from CallingPartyAddress
    // Copy the params
    for (unsigned int i = 0;i < source.length();i++) {
	NamedString* param = source.getParam(i);
	if (!param || !param->name().startsWith("Call"))
	    continue;
	String name = param->name();
	if (name.startSkip(YSTRING("CalledPartyAddress"),false))
	    dest.setParam(new NamedString("CallingPartyAddress" + name,*param));
	if (name.startSkip(YSTRING("CallingPartyAddress"),false))
	    dest.setParam(new NamedString("CalledPartyAddress" + name,*param));
    }
}

bool SS7SCCP::decodeMessage(SS7MsgSCCP* msg, SS7PointCode::Type pcType,
    const unsigned char* paramPtr, unsigned int paramLen)
{
    if (!msg)
	return false;
    String msgTypeName((int)msg->type());
    const char* msgName = SS7MsgSCCP::lookup(msg->type(),msgTypeName);
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
    const MsgParams* params = getSccpParams(msg->type());
    if (!params) {
	Debug(this,DebugWarn,"Parameters list could not be found for message %s [%p]",msgName,this);
	return false;
    }

    // Get parameter prefix
    String prefix = msg->params().getValue(YSTRING("message-prefix"));

    // Add protocol and message type
    switch (pcType) {
	case SS7PointCode::ITU:
	    msg->params().addParam(prefix+"protocol-type","itu-t");
	    break;
	case SS7PointCode::ANSI:
	case SS7PointCode::ANSI8:
	    msg->params().addParam(prefix+"protocol-type","ansi");
	    break;
	default: ;
    }
    msg->params().addParam(prefix+"message-type",msgName);

    String unsupported;
    const SS7MsgSCCP::Parameters* plist = params->params;
    SS7MsgSCCP::Parameters ptype;
    // first decode any mandatory fixed parameters the message should have
    while ((ptype = *plist++) != SS7MsgSCCP::EndOfParameters) {
	const SCCPParam* param = getParamDesc(ptype);
	if (!param) {
	    // this is fatal as we don't know the length
	    Debug(this,DebugGoOn,"Missing description of fixed SCCP parameter 0x%02x [%p]",ptype,this);
	    return false;
	}
	if (!param->size) {
	    Debug(this,DebugGoOn,"Invalid (variable) description of fixed SCCP parameter %s [%p]",param->name,this);
	    return false;
	}
	if (paramLen < param->size) {
	    Debug(this,DebugWarn,"Truncated SCCP message! [%p]",this);
	    return false;
	}
	DDebug(this,DebugAll,"Decoding fixed SCCP Param %s",param->name);
	if (!decodeParam(this,msg->params(),param,paramPtr,param->size,prefix)) {
	    Debug(this,DebugWarn,"Could not decode fixed SCCP parameter %s [%p]",param->name,this);
	    decodeRaw(this,msg->params(),param,paramPtr,param->size,prefix);
	    unsupported.append(param->name,",");
	}
	paramPtr += param->size;
	paramLen -= param->size;
    } // while ((ptype = *plist++)...
    bool mustWarn = true;
    bool ludt = msg->isLongDataMessage();
    // next decode any mandatory variable parameters the message should have
    while ((ptype = *plist++) != SS7MsgSCCP::EndOfParameters) {
	mustWarn = false;
	const SCCPParam* param = getParamDesc(ptype);
	if (!param) {
	    // we could skip over unknown mandatory variable length but it's still bad
	    Debug(this,DebugGoOn,"Missing description of variable SCCP parameter 0x%02x [%p]",ptype,this);
	    return false;
	}
	if (param->size)
	    Debug(this,DebugMild,"Invalid (fixed) description of variable SCCP parameter %s [%p]",param->name,this);
	if (!paramPtr || paramLen <= 0) {
	    Debug(this,DebugGoOn,
		  "Unexpected end of stream!! Expecting to decode variabile parameter %s but there is no data left!!!",
		   param->name);
	    return false;
	}
	unsigned int offs = paramPtr[0];
	if (ludt) {
	    offs |= (paramPtr[1] << 8);
	    paramPtr++;
	    paramLen--;
	}
	if ((offs < 1) || (offs >= paramLen)) {
	    Debug(this,DebugWarn,"Invalid offset %u (len=%u) SCCP parameter %s [%p]",
		offs,paramLen,param->name,this);
	    return false;
	}
	unsigned int size = paramPtr[offs];
	if (ptype == SS7MsgSCCP::LongData) {
	    size |= (paramPtr[++offs] << 8);
	    size --;
	}
	if ((size < 1) || (offs+size >= paramLen)) {
	    Debug(this,DebugWarn,"Invalid size %u (ofs=%u, len=%u) SCCP parameter %s [%p]",
		size,offs,paramLen,param->name,this);
	    return false;
	}
	bool decoded = false;
	if (ptype == SS7MsgSCCP::Data || ptype == SS7MsgSCCP::LongData) {
	    if (!decodeData(this,msg,paramPtr+offs+1,size)) {
		Debug(this,DebugWarn,"Could not decode data SCCP parameter %s (size=%u) [%p]",
			param->name,size,this);
		decodeRaw(this,msg->params(),param,paramPtr+offs+1,size,prefix);
	    }
	    decoded = true;
	}
	if (!decoded && !decodeParam(this,msg->params(),param,paramPtr+offs+1,size,prefix)) {
	    Debug(this,DebugWarn,"Could not decode variable SCCP parameter %s (size=%u) [%p]",
		param->name,size,this);
	    decodeRaw(this,msg->params(),param,paramPtr+offs+1,size,prefix);
	    unsupported.append(param->name,",");
	}
	paramPtr++;
	paramLen--;
    } // while ((ptype = *plist++)...
    // now decode the optional parameters if the message supports them
    if (params->optional) {
	unsigned int offs = 0;
	if (paramLen) {
	    if (ludt && paramLen > 1) {
		offs = paramPtr[0] | (paramPtr[1] << 8);
		paramPtr++;
		paramLen--;
	    } else if (!ludt)
		offs = paramPtr[0];
	}
	if (offs >= paramLen) {
	    if (paramLen) {
		Debug(this,DebugWarn,"Invalid SCCP optional offset %u (len=%u) [%p]",
		    offs,paramLen,this);
		return false;
	    }
	    Debug(this,DebugMild,"SCCP message %s lacking optional parameters [%p]",
		msgName,this);
	}
	else if (offs) {
	    mustWarn = true;
	    // advance pointer past mandatory parameters
	    paramPtr += offs;
	    paramLen -= offs;
	    while (paramLen) {
		ptype = (SS7MsgSCCP::Parameters)(*paramPtr++);
		paramLen--;
		if (ptype == SS7MsgSCCP::EndOfParameters)
		    break;
		if (paramLen < 2) {
		    Debug(this,DebugWarn,"Only %u octets while decoding optional SCCP parameter 0x%02x [%p]",
			paramLen,ptype,this);
		    return false;
		}
		unsigned int size = *paramPtr++;
		paramLen--;
		if ((size < 1) || (size >= paramLen)) {
		    Debug(this,DebugWarn,"Invalid size %u (len=%u) SCCP optional parameter 0x%02x [%p]",
			size,paramLen,ptype,this);
		    return false;
		}
		const SCCPParam* param = getParamDesc(ptype);
		if (!param) {
		    Debug(this,DebugMild,"Unknown optional SCCP parameter 0x%02x (size=%u) [%p]",ptype,size,this);
		    decodeRawParam(this,msg->params(),ptype,paramPtr,size,prefix);
		    unsupported.append(String((unsigned int)ptype),",");
		}
		else if (!decodeParam(this,msg->params(),param,paramPtr,size,prefix)) {
		    Debug(this,DebugWarn,"Could not decode optional SCCP parameter %s (size=%u) [%p]",param->name,size,this);
		    decodeRaw(this,msg->params(),param,paramPtr,size,prefix);
		    unsupported.append(param->name,",");
		}
		paramPtr += size;
		paramLen -= size;
	    } // while (paramLen)
	} // else if (offs)
	else
	    paramLen = 0;
    }
    if (unsupported)
	msg->params().addParam(prefix + "parameters-unsupported",unsupported);
    if (paramLen && mustWarn)
	Debug(this,DebugWarn,"Got %u garbage octets after message type 0x%02x [%p]",
	    paramLen,msg->type(),this);
    return true;
}


void SS7SCCP::receivedUPU(SS7PointCode::Type type, const SS7PointCode node,
	SS7MSU::Services part, unsigned char cause, const SS7Label& label, int sls)
{
    if (part != sif() || !m_management) // not SCCP
	return;
    m_management->sccpUnavailable(node,cause);
}

bool SS7SCCP::control(NamedList& params)
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
    if (toString() != cmp)
	return false;
    Lock lock(this);
    switch (cmd) {
	case Status:
	    printStatus(false);
	    return TelEngine::controlReturn(&params,true);
	case FullStatus:
	    if (m_extendedMonitoring)
		printStatus(true);
	    else
		Output("Extended monitoring disabled!! Full Status unavailable!");
	    return TelEngine::controlReturn(&params,true);
	case EnableExtendedMonitoring:
	    m_extendedMonitoring = true;
	    return TelEngine::controlReturn(&params,true);
	case DisableExtendedMonitoring:
	    m_extendedMonitoring = false;
	    return TelEngine::controlReturn(&params,true);
	case EnablePrintMsg:
	    m_printMsg = true;
	    return TelEngine::controlReturn(&params,true);
	case DisablePrintMsg:
	    m_printMsg = false;
	    return TelEngine::controlReturn(&params,true);
    }
    return TelEngine::controlReturn(&params,false);
}

void SS7SCCP::printStatus(bool extended)
{
    String dest = "";
    dumpArchive(dest,extended);
    if (!m_management)
	return;
    m_management->subsystemsStatus(dest);
    m_management->routeStatus(dest);
    Output("SCCP '%s' [%p] Time: " FMT64 " Status:%s",debugName(),this,Time::msecNow(),dest.c_str());
}

void SS7SCCP::notify(SS7Layer3* link, int sls)
{
    if (!(link && network()))
	return;
    setNetworkUp(network()->operational());
    if (m_management)
	m_management->pointcodeStatus(link,network()->operational());
}

void SS7SCCP::setNetworkUp(bool operational)
{
    if (m_layer3Up == operational)
	return;
    m_layer3Up = operational;
    if (!m_management)
	return;
    DDebug(this,DebugInfo,"L3 is %s %p",operational ? "operational" : "down", m_management);
    if (m_layer3Up)
	m_management->mtpEndRestart();
    else
	m_management->stopSSTs();

}

void SS7SCCP::routeStatusChanged(SS7PointCode::Type type, const SS7PointCode& node, SS7Route::State state)
{
#ifdef DEBUG
    String dump;
    dump << node;
    DDebug(this,DebugAll,"Route status changed %s %s %p",dump.c_str(),SS7Route::stateName(state),m_management);
#endif
    state = network()->getRouteState(type,node);
    if (m_management)
	m_management->routeStatus(type,node,state);
}

void SS7SCCP::archiveMessage(SS7MsgSCCP* msg)
{
    if (!msg)
	return;
    const char* type = SS7MsgSCCP::lookup(msg->type());
    NamedString* msgType = m_msgReturnStatus.getParam(type);
    if (msgType)
	incrementNS(msgType);
    else
	m_msgReturnStatus.addParam(type,"1");
    const char* code = msg->params().getValue(YSTRING("ReturnCode"));
    NamedString* retCode = m_msgReturnStatus.getParam(code);
    if (retCode)
	incrementNS(retCode);
    else
	m_msgReturnStatus.addParam(code,"1");
}

void SS7SCCP::dumpArchive(String& msg, bool extended)
{
    msg << "\r\nMessages Sent :          " << m_totalSent;
    msg << "\r\nMessages Received :      " << m_totalReceived;
    msg << "\r\nGT Translations :        " << m_totalGTTranslations;
    msg << "\r\nErrors :                 " << m_errors;
    msg << "\r\nGT Translations failed : " << m_gttFailed;
    NamedString* udts = m_msgReturnStatus.getParam(SS7MsgSCCP::lookup(SS7MsgSCCP::UDTS));
    if (udts)
	msg << "\r\n" << udts->name() << " : " << *udts;
    NamedString* xudts = m_msgReturnStatus.getParam(SS7MsgSCCP::lookup(SS7MsgSCCP::XUDTS));
    if (xudts)
	msg << "\r\n" << xudts->name() << " : " << *xudts;
    NamedString* ludts = m_msgReturnStatus.getParam(SS7MsgSCCP::lookup(SS7MsgSCCP::LUDTS));
    if (ludts)
	msg << "\r\n" << ludts->name() << " : " << *ludts;
    if (!extended)
	return;
    msg << "\r\n Error Causes:";
    for (unsigned int i = 0;i < m_msgReturnStatus.length();i++) {
	NamedString* param = m_msgReturnStatus.getParam(i);
	if (!param || param == udts || param == xudts || param == ludts)
	    continue;
	const char* error = lookup(param->name().toInteger(),s_return_cause);
	if (!error)
	    continue;
	msg << "\r\nCount: " << *param << " Error: " << error;
    }
}

bool SS7SCCP::isSCOCMsg(int msgType)
{
    switch (msgType) {
	case SS7MsgSCCP::CR:
	case SS7MsgSCCP::CC:
	case SS7MsgSCCP::CREF:
	case SS7MsgSCCP::RLSD:
	case SS7MsgSCCP::RLC:
	case SS7MsgSCCP::DT1:
	case SS7MsgSCCP::DT2:
	case SS7MsgSCCP::AK:
	case SS7MsgSCCP::ED:
	case SS7MsgSCCP::EA:
	case SS7MsgSCCP::RSR:
	case SS7MsgSCCP::RSC:
	case SS7MsgSCCP::ERR:
	case SS7MsgSCCP::IT:
	    return true;
	default:
	    return false;
    }
    return false;
}

/**
 * SS7ItuSCCPManagement
 */

SS7ItuSccpManagement::SS7ItuSccpManagement(const NamedList& params)
    : SCCPManagement(params,SS7PointCode::ITU)
{
    DDebug(this,DebugAll,"Creating SS7ItuSccpManagement(%s) %p",params.c_str(),this);
}

bool SS7ItuSccpManagement::processMessage(SS7MsgSCCP* message)
{
    if (!sccp())
	return false;
    DataBlock* data = message->getData();
    if (!data) {
	Debug(sccp(),DebugNote,"Request to process Itu management message with no data!");
	return false;
    }
    if (data->length() < 5) {
	Debug(sccp(),DebugNote,"Received short management message!");
	return false;
    }
    const unsigned char* paramsPtr = (const unsigned char*)data->data();
    unsigned char msg = *paramsPtr++;
    const char* msgType = lookup(msg,s_managementMessages);
    if (!msgType) {
	Debug(sccp(),DebugNote,"Received unknown management message! 0x%x",msg);
	return false;
    }
    if (msg > SSC) {
	Debug(sccp(),DebugNote,"Received unknown ITU management message! 0x%x",msg);
	return false;
    }
    // After msg type is SSN
    message->params().setParam("ssn",String((int)*paramsPtr++));
    // Pointcode 2 o
    int pointcode = *paramsPtr++;
    pointcode |= (*paramsPtr++ & 0x3f) << 8;
    message->params().setParam("pointcode",String(pointcode));
    // Subsystem Multiplicity Indicator
    message->params().setParam("smi",String(*paramsPtr++ & 0x03));
    // If message type is SSC decode congestion level
    if (msg == SSC) {
	if (!paramsPtr) {
	    Debug(sccp(),DebugNote,"Failed to decode SSC congestion level parameter! Reason short message.");
	    return false;
	}
	message->params().setParam("congestion-level",String(*paramsPtr & 0x0f));
    }
    if (printMessagess()) {
	String dest;
	printMessage(dest, (MsgType)msg,message->params());
	Debug(this,DebugInfo,"Received message %s",dest.c_str());
    }
    return handleMessage(msg,message->params());
}

bool SS7ItuSccpManagement::sendMessage(SCCPManagement::MsgType msgType, const NamedList& params)
{
    if (!sccp())
	return false;
    if (printMessagess()) {
	String dest;
	printMessage(dest, msgType,params);
	Debug(this,DebugInfo,"Sending message %s",dest.c_str());
    }
    unsigned char ssn = params.getIntValue(YSTRING("ssn"));
    int pointcode = params.getIntValue(YSTRING("pointcode"));
    int smi = params.getIntValue(YSTRING("smi"));
    int dataLen = msgType == SSC ? 6 : 5;
    DataBlock data(0,dataLen);
    unsigned char * d = (unsigned char*)data.data();
    d[0] = msgType;
    d[1] = ssn;
    d[2] = pointcode & 0xff;
    d[3] = (pointcode >> 8) & 0x3f;
    d[4] = smi & 0x03;
    if (msgType == SSC)
	d[5] = params.getIntValue(YSTRING("congestion-level"),0) & 0x0f;
    int localPC = sccp()->getPackedPointCode();
    SS7MsgSCCP* msg = new SS7MsgSCCP(SS7MsgSCCP::UDT);
    const char* remotePC = params.getValue(YSTRING("RemotePC"));
    msg->params().setParam("ProtocolClass","0");
    msg->params().setParam("CalledPartyAddress.ssn","1");
    msg->params().setParam("CalledPartyAddress.pointcode",remotePC);
    msg->params().setParam("CalledPartyAddress.route","ssn");
    msg->params().setParam("CallingPartyAddress.ssn","1");
    msg->params().setParam("CallingPartyAddress.route","ssn");
    msg->params().setParam("CallingPartyAddress.pointcode",String(localPC));
    msg->params().setParam("LocalPC",String(localPC));
    msg->params().setParam("RemotePC",remotePC);
    msg->setData(&data);
    bool ret = sccp()->transmitMessage(msg) >= 0;
    if (!ret)
	Debug(this,DebugNote,"Failed to send management message %s to remote %s",
	      lookup(msgType,s_managementMessages),params.getValue(YSTRING("RemotePC")));
    msg->extractData();
    TelEngine::destruct(msg);
    return ret;
}

void SS7ItuSccpManagement::manageSccpRemoteStatus(SccpRemote* rsccp, SS7Route::State newState)
{
    if (!rsccp)
	return;
#ifdef XDEBUG
    String pc;
    rsccp->dump(pc,false);
    XDebug(this,DebugInfo,"Remote sccp '%s' status changed, new state: %s",pc.c_str(),SS7Route::stateName(newState));
#endif
    switch (newState) {
	case SS7Route::Congestion:
	    Debug(sccp(),DebugStub,"Please implement SCCPManagement Congestion");
	    break;
	case SS7Route::Allowed:
	{
	    // Set state should set the state to all subsystems
	    rsccp->setState(SCCPManagement::Allowed);
	    updateTables(rsccp);
	    rsccp->resetCongestion();
	    // Discontinue the Subsystem Status Test for SSN = 1
	    SccpSubsystem* ss = new SccpSubsystem(1);
	    stopSst(rsccp,ss);
	    TelEngine::destruct(ss);
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),PCAccessible,-1,0);
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),-1,SccpRemoteAccessible,0);
	    break;
	}
	case SS7Route::Prohibited:
	{
	    rsccp->setState(SCCPManagement::Prohibited);
	    updateTables(rsccp);
	    // Discontinue all tests for the remote sccp
	    SccpSubsystem* ss = new SccpSubsystem(1);
	    stopSst(rsccp,0,ss); // Stop all sst except management
	    // Do not start SST if the route is down the message will fail to be
	    // sent. The status will be changed to allowed when the route is up
	    TelEngine::destruct(ss);
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),PCInaccessible,-1,0);
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),-1,SccpRemoteInaccessible,0);
	    break;
	}
	case SS7Route::Unknown:
	    rsccp->setState(SCCPManagement::Unknown);
	    break;
	default:
	    DDebug(this,DebugNote,"Unhandled remote sccp status '%s'",SS7Route::stateName(newState));
    }
}

bool SS7ItuSccpManagement::handleMessage(int msgType, NamedList& params)
{
    unsigned char ssn = params.getIntValue(YSTRING("ssn"));
    if (ssn == 0) {
	Debug(this,DebugNote,"Received management message '%s' with invalid ssn '%d'",
	      lookup(msgType,s_managementMessages),ssn);
	return false;
    }
    unsigned char smi = params.getIntValue(YSTRING("smi")); // subsystem multiplicity indicator
    if (smi != 0) {
	if (smi > 3) {
	    Debug(this,DebugWarn, "Received management message '%s' with unknown smi: '%d' , ssn: '%d'",
		  lookup(msgType,s_managementMessages),smi,ssn);
	    smi = 0;
	} else
	    DDebug(this,DebugNote,"Received management message '%s' with national smi: %d",
		   lookup(msgType,s_managementMessages),smi);
    }
    switch (msgType) {
	case SSC:
	    Debug(this,DebugStub,"Please implement subsystem congested!");
	    break;
	default:
	    return SCCPManagement::handleMessage(msgType,ssn,smi,params);
    }
    return true;
}

void SS7ItuSccpManagement::handleSubsystemStatus(SccpSubsystem* subsystem, bool allowed, SccpRemote* remote, int smi)
{
    if (!subsystem) {
	Debug(sccp(),DebugWarn,"Request to handle subsystem status with no subsystem!");
	return;
    }
    SCCPManagement::SccpStates ssnState = allowed ? SCCPManagement::Allowed : SCCPManagement::Prohibited;
    subsystem->setState(ssnState);
    DDebug(this,DebugInfo,"Handle subsystem status for pc: '%d' ssn: '%d' status %s", remote ? remote->getPackedPointcode() : 0,
		subsystem ? subsystem->getSSN() : 0, stateName(ssnState));
    Lock lock(this);
    bool localSubsystem = false;
    // Change the status of the subsystem
    if (!remote || remote->getPointCode() == *sccp()->getLocalPointCode()) { // LocalSubsystem
	SccpLocalSubsystem* subs = getLocalSubsystem(subsystem->getSSN());
	if (subs) {
	    if (subs->getState() == ssnState) // Same state? do nothing
		return;
	    subs->resetTimers();
	    subs->setState(ssnState);
	} else // Append dynamically
	    m_localSubsystems.append(new SccpLocalSubsystem(subsystem->getSSN(),getCoordTimeout(),
		    getIgnoreTestsInterval()));
	localSubsystem = true;
    } else {
	SccpRemote* rsccp = getRemoteSccp(remote->getPackedPointcode());
	if (rsccp && !rsccp->changeSubsystemState(subsystem->getSSN(),ssnState))
	    return;
    }
    // Stop all subsystem status tests
    if (!localSubsystem && allowed)
	stopSst(remote,subsystem);
    else if (!localSubsystem) // Initiate subsystem status test
	startSst(remote,subsystem);
    lock.drop();
    // update translation tables
    if (!localSubsystem)
	updateTables(remote,subsystem);
    // Local Broadcast user in/out of service
    NamedList params("");
    if (!localSubsystem)
	params.setParam("pointcode",String(remote->getPackedPointcode()));
    params.setParam("ssn",String(subsystem->getSSN()));
    params.setParam("subsystem-status",lookup(allowed ? UserInService : UserOutOfService,broadcastType()));
    managementMessage(SCCP::StatusIndication,params);
    // Send broadcast for all concerned signalling points
    ///TODO for now we send only for local interested subsystems
    if (!localSubsystem)
	return;
    notifyConcerned(allowed ? SSA : SSP, subsystem->getSSN(),smi);
}

/**
 * SS7AnsiSCCPManagement
 */
SS7AnsiSccpManagement::~SS7AnsiSccpManagement()
{
    DDebug(this,DebugAll,"Destroing Ansi Sccp Management(%p)",this);
}

bool SS7AnsiSccpManagement::processMessage(SS7MsgSCCP* message)
{
    if (!sccp()) {
	return false;
    }
    DataBlock* data = message->getData();
    if (!data) {
	DDebug(sccp(),DebugNote,"Request to process Ansi management message with no data!");
	return false;
    }
    if (data->length() < 6) {
	DDebug(sccp(),DebugNote,"Received short Ansi management message! %d",data->length());
	return false;
    }
    const unsigned char* paramsPtr = (const unsigned char*)data->data();
    unsigned char msg = *paramsPtr++;
    const char* msgType = lookup(msg,s_managementMessages);
    if (!msgType) {
	DDebug(sccp(),DebugNote,"Received unknown management message! 0x%x",msg);
	return false;
    }
    if (msg > 0x05 && msg < 0xfd) {
	DDebug(sccp(),DebugNote,"Received unknown Ansi management message! 0x%x",msg);
	return false;
    }
    // After msg type is SSN
    message->params().setParam("ssn",String((int)*paramsPtr++));
    // Pointcode 2 o
    unsigned int pointcode = *paramsPtr++;
    pointcode |= (*paramsPtr++ << 8);
    pointcode |= (*paramsPtr++ << 16);
    message->params().setParam("pointcode",String(pointcode));
    // Subsystem Multiplicity Indicator
    message->params().setParam("SMI",String(*paramsPtr++ & 0x03));

    if (printMessagess()) {
	String dest;
	printMessage(dest, (MsgType)msg,message->params());
	Debug(this,DebugInfo,"Received message %s",dest.c_str());
    }
    return handleMessage(msg,message->params());
}

bool SS7AnsiSccpManagement::sendMessage(SCCPManagement::MsgType msgType, const NamedList& params)
{
    if (!sccp())
	return false;
    if (printMessagess()) {
	String dest;
	printMessage(dest, msgType,params);
	Debug(this,DebugInfo,"Sending message %s",dest.c_str());
    }
    unsigned char ssn = params.getIntValue(YSTRING("ssn"));
    int pointcode = params.getIntValue(YSTRING("pointcode"));
    int smi = params.getIntValue(YSTRING("smi"));
    DataBlock data(0,6);
    unsigned char * d = (unsigned char*)data.data();
    d[0] = msgType;
    d[1] = ssn;
    d[2] = pointcode & 0xff;
    d[3] = (pointcode >> 8) & 0xff;
    d[4] = (pointcode >> 16) & 0xff;
    d[5] = smi & 0x03;
    int localPC = sccp()->getPackedPointCode();
    SS7MsgSCCP* msg = new SS7MsgSCCP(SS7MsgSCCP::UDT);
    const char* remotePC = params.getValue(YSTRING("RemotePC"));
    msg->params().setParam("ProtocolClass","0");
    msg->params().setParam("CalledPartyAddress.ssn","1");
    msg->params().setParam("CalledPartyAddress.pointcode",remotePC);
    msg->params().setParam("CalledPartyAddress.route","ssn");
    msg->params().setParam("CallingPartyAddress.ssn","1");
    msg->params().setParam("CallingPartyAddress.route","ssn");
    msg->params().setParam("CallingPartyAddress.pointcode",String(localPC));
    msg->params().setParam("LocalPC",String(localPC));
    msg->params().setParam("RemotePC",remotePC);
    msg->setData(&data);
    bool ret = sccp()->transmitMessage(msg) >= 0;
    if (!ret)
	Debug(this,DebugNote,"Failed to send management message %s to remote %s",
	      lookup(msgType,s_managementMessages),params.getValue(YSTRING("RemotePC")));
    msg->extractData();
    TelEngine::destruct(msg);
    return ret;
}

bool SS7AnsiSccpManagement::handleMessage(int msgType, NamedList& params)
{
    unsigned char ssn = params.getIntValue(YSTRING("ssn"));
    if (ssn == 0) {
	Debug(this,DebugNote,"Received management message '%s' with invalid ssn '%d'",
	      lookup(msgType,s_managementMessages),ssn);
	return false;
    }
    unsigned char smi = params.getIntValue(YSTRING("smi")); // subsystem multiplicity indicator
    if (!lookup(smi,s_ansiSmi)) {
	Debug(this,DebugWarn, "Received management message '%s' with invalid smi: '%d' , ssn: '%d'",
		  lookup(msgType,s_managementMessages),smi,ssn);
	smi = 0;
    }
    switch (msgType) {
	case SBR:
	case SNR:
	case SRT:
	    Debug(this,DebugStub,"Please implement %s message handling!",lookup(msgType,s_managementMessages));
	    break;
	default:
	    return SCCPManagement::handleMessage(msgType,ssn,smi,params);
    }
    return true;
}

void SS7AnsiSccpManagement::manageSccpRemoteStatus(SccpRemote* rsccp, SS7Route::State newState)
{
    if (!rsccp)
	return;
#ifdef XDEBUG
    String pc;
    rsccp->dump(pc,false);
    XDebug(this,DebugInfo,"Remote sccp '%s' status changed, new state: %s",pc.c_str(),SS7Route::stateName(newState));
#endif
    switch (newState) {
	case SS7Route::Congestion:
	    Debug(sccp(),DebugStub,"Please implement SCCPManagement Congestion");
	    break;
	case SS7Route::Allowed:
	{
	    // Set state should set the state to all subsystems
	    rsccp->setState(SCCPManagement::Allowed);
	    rsccp->resetCongestion();
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),
		    PCAccessible,-1,0);
	    // Discontinue all subsystem status tests
	    stopSst(rsccp);
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),
		    -1,SccpRemoteAccessible,0);
	    updateTables(rsccp);
	    rsccp->lock();
	    ListIterator ssns(rsccp->getSubsystems());
	    rsccp->unlock();
	    SccpSubsystem* ss = 0;
	    while ((ss = YOBJECT(SccpSubsystem,ssns.get())))
		localBroadcast(SCCP::StatusIndication,-1,-1,-1,-1,ss->getSSN(),UserInService);
	    break;
	}
	case SS7Route::Prohibited:
	{
	    rsccp->setState(SCCPManagement::Prohibited);
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),
		    PCInaccessible,-1,0);
	    SccpSubsystem* ss = new SccpSubsystem(1);
	    stopSst(rsccp,ss);
	    TelEngine::destruct(ss);
	    updateTables(rsccp);
	    localBroadcast(SCCP::PointCodeStatusIndication,rsccp->getPackedPointcode(),
		    -1,SccpRemoteInaccessible,0);
	    rsccp->lock();
	    ListIterator ssns(rsccp->getSubsystems());
	    rsccp->unlock();
	    SccpSubsystem* ss1 = 0;
	    while ((ss1 = YOBJECT(SccpSubsystem,ssns.get())))
		localBroadcast(SCCP::StatusIndication,-1,-1,-1,-1,ss1->getSSN(),UserOutOfService);
	    break;
	}
	case SS7Route::Unknown:
	    rsccp->setState(SCCPManagement::Unknown);
	    break;
	default:
	    DDebug(this,DebugNote,"Unhandled remote sccp status '%s'",SS7Route::stateName(newState));
    }
}

void SS7AnsiSccpManagement::handleSubsystemStatus(SccpSubsystem* subsystem, bool allowed, SccpRemote* remote, int smi)
{
    if (!subsystem || subsystem->getSSN() <= 0) {
	Debug(sccp(),DebugWarn,"Request to handle subsystem status with no subsystem!");
	return;
    }
    SCCPManagement::SccpStates ssnState = allowed ? SCCPManagement::Allowed : SCCPManagement::Prohibited;
    subsystem->setState(ssnState);
    DDebug(this,DebugInfo,"Handle subsystem status for pc: '%d' ssn: '%d' status %s", remote ? remote->getPackedPointcode() : 0,
		subsystem ? subsystem->getSSN() : 0, stateName(ssnState));
    Lock lock(this);
    bool localSubsystem = false;
    // Change the status of the subsystem
    if (!remote || remote->getPointCode() == *sccp()->getLocalPointCode()) { // LocalSubsystem
	SccpLocalSubsystem* subs = getLocalSubsystem(subsystem->getSSN());
	if (subs) {
	    if (subs->getState() == ssnState) // Same state? do nothing
		return;
	    subs->resetTimers();
	    subs->setState(ssnState);
	} else // Append dynamically
	    m_localSubsystems.append(new SccpLocalSubsystem(subsystem->getSSN(),getCoordTimeout(),
		    getIgnoreTestsInterval()));
	localSubsystem = true;
    } else {
	SccpRemote* rsccp = getRemoteSccp(remote->getPackedPointcode());
	if (rsccp && !rsccp->changeSubsystemState(subsystem->getSSN(),ssnState))
	    return;
    }
    // Stop all subsystem status tests
    if (!localSubsystem && allowed)
	stopSst(remote,subsystem);
    else if (!localSubsystem) // Initiate subsystem status test
	startSst(remote,subsystem);
    lock.drop();
    // update translation tables
    if (!localSubsystem)
	updateTables(remote,subsystem);
    // Local Broadcast user in/out of service
    localBroadcast(SCCP::StatusIndication,localSubsystem ? -1 :remote->getPackedPointcode(),
	    -1,-1,-1,subsystem->getSSN(), allowed ? UserInService : UserOutOfService);
    // Send broadcast for all concerned signalling points
    ///TODO for now we send only for local interested subsystems
    if (!localSubsystem)
	return;
    notifyConcerned(allowed ? SSA : SSP, subsystem->getSSN(),smi);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
