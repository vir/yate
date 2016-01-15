/**
 * q931.cpp
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

#include <string.h>


using namespace TelEngine;

/**
 * DEFINEs controlling Q.931 implementation
 * Q931_ACCEPT_RESTART
 *	Controls acceptance of RESTART and RESTART ACK messages even if they don't have the global call reference
 *	Yes: Accept anyway
 *	No:  Don't accept these messages if they don't have the global call reference
 */
#ifndef Q931_ACCEPT_RESTART
//    #define Q931_ACCEPT_RESTART
#endif

#define Q931_MSG_PROTOQ931 0x08          // Q.931 protocol discriminator in the message header

// Clear the bit 7 for each byte in a buffer
static inline void clearBit7(const void* buffer, u_int32_t len)
{
    u_int8_t* data = (u_int8_t*)buffer;
    for (u_int32_t i = 0; i < len; i++)
	data[i] &= 0x7f;
}

// Dump data to a given parameter of a named list. Clear bit 7 if requested
static inline void dumpDataBit7(NamedList* dest, const void* data, u_int32_t len,
	bool keepBit7 = true, const char* name = "unparsed-data")
{
    String tmp((const char*)data,len);
    if (!keepBit7)
	clearBit7(tmp.c_str(),tmp.length());
    dest->addParam(name,tmp);
}

// Fill a message header. header parameter must be large enough to store message header
// Return header length
static inline u_int8_t fillHeader(u_int8_t* header, ISDNQ931Message* msg,
	DebugEnabler* dbg)
{
    header[0] = Q931_MSG_PROTOQ931;
    // Dummy call reference ?
    if (msg->dummyCallRef()) {
	header[1] = 0;
	header[2] = msg->type() & 0x7f;     // Message type. Bit 7 must be 0
	return 3;
    }
    // Check message's call reference length
    if (!msg->callRefLen() || msg->callRefLen() > 4) {
	Debug(dbg,DebugNote,
	    "Can't encode message (%p) with call reference length %u",
	    msg,msg->callRefLen());
	return 0;
    }
    // Call reference length
    header[1] = 0x0f & msg->callRefLen();
    // Set call reference field
    // For the initiator, bit 7 of the first byte of call reference must be 0
    header[2] = msg->initiator() ? 0 : 0x80;
    u_int8_t len = 2;
    u_int8_t shift = msg->callRefLen() * 8;
    do {
	shift -= 8;
	header[len++] |= (u_int8_t)(msg->callRef() >> shift);
    }
    while (shift);
    // Set message type. Bit 7 must be 0
    header[len++] = msg->type() & 0x7f;
    return len;
}

/**
 * IEParam
 * Q.931 message IE parameter description
 */
struct IEParam
{
public:
    inline const char* addParam(NamedList* dest, u_int8_t data,
	const char* defVal = 0) const {
	    const char* tmp = lookup(data & mask,values,defVal);
	    if (tmp)
		dest->addParam(name,tmp);
	    return tmp;
	}
    inline bool addBoolParam(NamedList* dest, u_int8_t data, bool toggle) const {
	    bool result = toggle ^ ((data & mask) != 0);
	    dest->addParam(name,String::boolText(result));
	    return result;
	}
    inline void addIntParam(NamedList* dest, u_int8_t data) const {
	    if (!addParam(dest,data))
		dest->addParam(name,String((unsigned int)(data & mask)));
	}

    inline void dumpData(NamedList* dest, const u_int8_t* data, u_int32_t len) const
	{ SignallingUtils::dumpData(0,*dest,name,data,len); }

    inline void dumpDataBit7(NamedList* dest, const u_int8_t* data, u_int32_t len,
	bool keepBit7) const
	{ ::dumpDataBit7(dest,(const void*)data,len,keepBit7,name); }

    inline int getValue(NamedList* ns, bool applyMask = true, int defVal = 0) const {
	    int tmp = lookup(ns->getValue(name),values,defVal);
	    if (applyMask)
		tmp &= mask;
	    return tmp;
	}

    const char* name;
    u_int8_t mask;
    const TokenDict* values;
};

/**
 * Q931Parser
 * Q.931 message encoder/decoder
 */
class Q931Parser
{
public:
    inline Q931Parser(ISDNQ931ParserData& data)
	: m_settings(&data), m_msg(0), m_codeset(0), m_activeCodeset(0), m_skip(false)
	{}

    // Decode received data.
    // If the message is a SEGMENT decode only the header and the first IE.
    //  If valid, fill the buffer with the rest of the message. If segData is 0, drop the message.
    // @param segData Segment message data
    // @return Valid ISDNQ931Message pointer on success or 0.
    ISDNQ931Message* decode(const DataBlock& buffer, DataBlock* segData);

    // Encode a message.
    // If the message is longer then max allowed and segmentation is allowed, split it into SEGMENT messages
    // Failure reasons:
    //  Message too long and segmentation not allowed
    //  Message too long, segmentation allowed, but too many segments
    // @param msg The message to encode.
    // @param dest List of DataBlock with the message segments.
    // @return The number of segments on success or 0 on failure.
    u_int8_t encode(ISDNQ931Message* msg, ObjList& dest);

    // Field names
    static const TokenDict s_dict_congestion[];
    static const TokenDict s_dict_bearerTransCap[];
    static const TokenDict s_dict_bearerTransMode[];
    static const TokenDict s_dict_bearerTransRate[];
    static const TokenDict s_dict_bearerProto1[];
    static const TokenDict s_dict_bearerProto2[];
    static const TokenDict s_dict_bearerProto3[];
    static const TokenDict s_dict_typeOfNumber[];
    static const TokenDict s_dict_numPlan[];
    static const TokenDict s_dict_presentation[];
    static const TokenDict s_dict_screening[];
    static const TokenDict s_dict_subaddrType[];
    static const TokenDict s_dict_channelIDSelect_BRI[];
    static const TokenDict s_dict_channelIDSelect_PRI[];
    static const TokenDict s_dict_channelIDUnits[];
    static const TokenDict s_dict_loLayerProto2[];
    static const TokenDict s_dict_loLayerProto3[];
    static const TokenDict s_dict_networkIdType[];
    static const TokenDict s_dict_networkIdPlan[];
    static const TokenDict s_dict_notification[];
    static const TokenDict s_dict_progressDescr[];
    static const TokenDict s_dict_restartClass[];
    static const TokenDict s_dict_signalValue[];

private:
    // Encode a full message. Parameter ieEncoded is true if the IEs buffers are already filled
    // Check if the message fits the maximum length
    // Return 1 on success and 0 on failure
    u_int8_t encodeMessage(ObjList& dest, bool ieEncoded,
	u_int8_t* header, u_int8_t headerLen);
    // Encode each IE into it's buffer
    // Check if the largest buffer fits the maximum message length
    bool encodeIEList(bool& segmented, u_int8_t headerLen);
    // Append a segment buffer to a list. Increase the segment counter
    // Check if the counter is valid (don't exceed the maximum segments count)
    bool appendSegment(ObjList& dest, DataBlock* segment, u_int8_t& count);
    // Reset data. Returns the message
    inline ISDNQ931Message* reset() {
	    ISDNQ931Message* msg = m_msg;
	    m_msg = 0;
	    m_activeCodeset = m_codeset = 0;
	    return msg;
	}
    // Reset data. Returns the value
    inline u_int8_t reset(u_int8_t val) {
	    m_msg = 0;
	    m_activeCodeset = m_codeset = 0;
	    return val;
	}
    // Encode an IE to a buffer
    // Return false on failure
    bool encodeIE(ISDNQ931IE* ie, DataBlock& buffer);
    // Add an error parameter to a given IE
    ISDNQ931IE* errorParseIE(ISDNQ931IE* ie, const char* reason,
	const u_int8_t* data, u_int32_t len);
    // Check the encoding a given IE. Before checking apply a 0x60 mask
    // Add a parameter if the check fails
    bool checkCoding(u_int8_t value, u_int8_t expected, ISDNQ931IE* ie);
    // Skip data until an element with bit 7 (0/1 ext) set is found. Skip this one too
    // Parameter 'crt' is modified in the process. On exit points to the element which won't be skipped
    // Return the number of element to skip
    u_int8_t skipExt(const u_int8_t* data, u_int8_t len, u_int8_t& crt);
    // Parse the received data to get the message header. Create message on success
    // @return False to stop the parser
    bool createMessage(u_int8_t* data, u_int32_t len);
    // Process received Segment message
    ISDNQ931Message* processSegment(const u_int8_t* data, u_int32_t len,
	DataBlock* segData);
    // Parse the received data to get an IE
    // @param data The data to parse
    // @param len Data length
    // @param consumed The number of bytes consumed by the IE
    // @return Pointer to a valid IE or 0 to stop the parser
    ISDNQ931IE* getIE(const u_int8_t* data, u_int32_t len, u_int32_t& consumed);
    // Constructs a fixed (1 byte) length IE
    // @param data The data
    // @return Valid ISDNQ931IE pointer
    ISDNQ931IE* getFixedIE(u_int8_t data);
    // Shift the codeset while parsing
    // @param ie Pointer to a valid ISDNQ931IE of type Shift
    void shiftCodeset(const ISDNQ931IE* ie);
    // Common methods for decoding Bearer capabilities and Low layer compatibility
    void decodeLayer1(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len,
	u_int8_t& crt, const IEParam* ieParam, u_int8_t ieParamIdx);
    void decodeLayer2(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len,
	u_int8_t& crt, const IEParam* ieParam, u_int8_t ieParamIdx);
    void decodeLayer3(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len,
	u_int8_t& crt, const IEParam* ieParam, u_int8_t ieParamIdx);
    // Decode the corresponding variable IE
    ISDNQ931IE* decodeBearerCaps(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeCallIdentity(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeCallState(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeChannelID(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeProgress(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeNetFacility(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeNotification(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeDisplay(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeDateTime(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeKeypad(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeSignal(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeCallingNo(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeCallingSubAddr(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeCalledNo(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeCalledSubAddr(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeRestart(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeSegmented(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeNetTransit(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeLoLayerCompat(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeHiLayerCompat(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    ISDNQ931IE* decodeUserUser(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len);
    // It seems that the Connected number has the same layout as the Calling number IE
    inline ISDNQ931IE* decodeConnectedNo(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len)
	{ return decodeCallingNo(ie,data,len); }
    // Encode the corresponding variable IE
    bool encodeBearerCaps(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeCallState(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeChannelID(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeDisplay(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeCallingNo(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeCalledNo(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeProgress(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeNotification(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeKeypad(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeSignal(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeRestart(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeSendComplete(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeHighLayerCap(ISDNQ931IE* ie, DataBlock& buffer);
    bool encodeUserUser(ISDNQ931IE* ie, DataBlock& buffer);

    ISDNQ931ParserData* m_settings;      // Settings
    ISDNQ931Message* m_msg;              // Current encoded/decoded message
    u_int8_t m_codeset;                  // Current codeset
    u_int8_t m_activeCodeset;            // Active codeset
    bool m_skip;                         // Skip current IE
};


/**
 * ISDNQ931IEData
 */
ISDNQ931IEData::ISDNQ931IEData(bool bri)
    : m_bri(bri),
    m_channelMandatory(true),
    m_channelByNumber(true)
{
}

bool ISDNQ931IEData::processBearerCaps(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	ISDNQ931IE* ie = new ISDNQ931IE(ISDNQ931IE::BearerCaps);
	ie->addParam("transfer-cap",m_transferCapability);
	ie->addParam("transfer-mode",m_transferMode);
	ie->addParam("transfer-rate",m_transferRate);
	ie->addParam("layer1-protocol",m_format);
	// Q.931 Table 4.6: Send Layer 2/3 only in 'packet switching' (0x40) mode
	if (m_transferMode == lookup(0x40,Q931Parser::s_dict_bearerTransMode)) {
	    ie->addParam("layer2-protocol","q921");
	    ie->addParam("layer3-protocol","q931");
	}
	msg->appendSafe(ie);
	return true;
    }
    ISDNQ931IE* ie = msg->getIE(ISDNQ931IE::BearerCaps);
    if (!ie) {
	m_transferCapability = "";
	m_transferMode = "";
	m_transferRate = "";
	return false;
    }
    m_transferCapability = ie->getValue(YSTRING("transfer-cap"));
    m_transferMode = ie->getValue(YSTRING("transfer-mode"));
    m_transferRate = ie->getValue(YSTRING("transfer-rate"));
    m_format = ie->getValue(YSTRING("layer1-protocol"));
    return true;
}

bool ISDNQ931IEData::processChannelID(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	ISDNQ931IE* ie = new ISDNQ931IE(ISDNQ931IE::ChannelID);
	ie->addParam("interface-bri",String::boolText(m_bri));
	ie->addParam("channel-exclusive",String::boolText(m_channelMandatory));
	ie->addParam("channel-select",m_channelSelect);
	ie->addParam("type",m_channelType);
	ie->addParam("channel-by-number",String::boolText(true));
	ie->addParam("channels",m_channels);
	msg->appendSafe(ie);
	return true;
    }
    ISDNQ931IE* ie = msg->getIE(ISDNQ931IE::ChannelID);
    m_channels = "";
    if (!ie) {
	m_channelMandatory = m_channelByNumber = false;
	return false;
    }
    m_bri = ie->getBoolValue(YSTRING("interface-bri"),m_bri);
    m_channelMandatory = ie->getBoolValue(YSTRING("channel-exclusive"));
    m_channelByNumber = ie->getBoolValue(YSTRING("channel-by-number"));
    m_channelType = ie->getValue(YSTRING("type"));
    m_channelSelect = ie->getValue(YSTRING("channel-select"));
    if (m_bri && m_channelSelect) {
	m_channelByNumber = true;
	if (m_channelSelect == "b1")
	    m_channels = "1";
	else if (m_channelSelect == "b2")
	    m_channels = "2";
	else
	    return false;
    }
    // ChannelID IE may repeat if channel is given by number
    if (m_channelByNumber) {
	unsigned int n = ie->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = ie->getParam(i);
	    if (ns && (ns->name() == YSTRING("channels")))
		m_channels.append(*ns,",");
	}
    }
    else
	m_channels = ie->getValue(YSTRING("slot-map"));
    return true;
}

bool ISDNQ931IEData::processProgress(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	// Remove non-isdn-source/non-isdn-destination
	if (data) {
	    if (!data->flag(ISDNQ931::SendNonIsdnSource))
		SignallingUtils::removeFlag(m_progress,"non-isdn-source");
	    if (data->flag(ISDNQ931::IgnoreNonIsdnDest))
		SignallingUtils::removeFlag(m_progress,"non-isdn-destination");
	}
	if (!m_progress.null())
	    msg->appendIEValue(ISDNQ931IE::Progress,"description",m_progress);
    }
    else {
	// Progress may repeat
	ISDNQ931IE* ie = msg->getIE(ISDNQ931IE::Progress);
	for (; ie; ie = msg->getIE(ISDNQ931IE::Progress,ie))
	    m_progress.append(ie->getValue(YSTRING("description")),",");
    }
    return !m_progress.null();
}

bool ISDNQ931IEData::processRestart(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	msg->appendIEValue(ISDNQ931IE::Restart,"class",m_restart);
	return true;
    }
    m_restart = msg->getIEValue(ISDNQ931IE::Restart,"class");
    return !m_restart.null();
}

bool ISDNQ931IEData::processNotification(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	if (data && data->flag(ISDNQ931::CheckNotifyInd)) {
	    int val = lookup(m_notification,Q931Parser::s_dict_notification,-1);
	    if (val < 0 && val > 2)
		return false;
	}
	msg->appendIEValue(ISDNQ931IE::Notification,"notification",m_notification);
	return true;
    }
    m_notification = msg->getIEValue(ISDNQ931IE::Notification,"notification");
    return !m_notification.null();
}

bool ISDNQ931IEData::processCalledNo(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	ISDNQ931IE* ie = new ISDNQ931IE(ISDNQ931IE::CalledNo);
	ie->addParam("number",m_calledNo);
	if (!m_callerType.null())
	    ie->addParam("type",m_calledType);
	if (!m_callerPlan.null())
	    ie->addParam("plan",m_calledPlan);
	msg->appendSafe(ie);
	return true;
    }
    ISDNQ931IE* ie = msg->getIE(ISDNQ931IE::CalledNo);
    if (!ie) {
	m_calledNo = "";
	return false;
    }
    m_calledNo = ie->getValue(YSTRING("number"));
    m_calledType = ie->getValue(YSTRING("type"));
    m_calledPlan = ie->getValue(YSTRING("plan"));
    return true;
}

bool ISDNQ931IEData::processCallingNo(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	if (!m_callerNo)
	    return false;
	ISDNQ931IE* ie = new ISDNQ931IE(ISDNQ931IE::CallingNo);
	ie->addParam("number",m_callerNo);
	if (!m_callerType.null())
	    ie->addParam("type",m_callerType);
	if (!m_callerPlan.null())
	    ie->addParam("plan",m_callerPlan);
	if (data && data->flag(ISDNQ931::ForcePresNetProv)) {
	    ie->addParam("presentation",lookup(0x00,Q931Parser::s_dict_presentation));
	    ie->addParam("screening",lookup(0x03,Q931Parser::s_dict_screening));
	}
	else {
	    ie->addParam("presentation",m_callerPres);
	    ie->addParam("screening",m_callerScreening);
	}
	msg->appendSafe(ie);
	return true;
    }
    ISDNQ931IE* ie = msg->getIE(ISDNQ931IE::CallingNo);
    if (!ie) {
	m_callerNo = "";
	return false;
    }
    m_callerNo = ie->getValue(YSTRING("number"));
    m_callerType = ie->getValue(YSTRING("type"));
    m_callerPlan = ie->getValue(YSTRING("plan"));
    m_callerPres = ie->getValue(YSTRING("presentation"));
    m_callerScreening = ie->getValue(YSTRING("screening"));
    return true;
}

bool ISDNQ931IEData::processCause(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	msg->appendIEValue(ISDNQ931IE::Cause,0,m_reason?m_reason:"normal-clearing");
	return true;
    }
    m_reason = msg->getIEValue(ISDNQ931IE::Cause,0);
    return !m_reason.null();
}

bool ISDNQ931IEData::processDisplay(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	if (m_display.null() || !data || data->flag(ISDNQ931::NoDisplayIE))
	    return false;
	msg->appendIEValue(ISDNQ931IE::Display,"display",m_display);
	return true;
    }
    m_display = msg->getIEValue(ISDNQ931IE::Display,"display");
    return !m_display.null();
}

bool ISDNQ931IEData::processKeypad(ISDNQ931Message* msg, bool add,
	ISDNQ931ParserData* data)
{
    if (!msg)
	return false;
    if (add) {
	msg->appendIEValue(ISDNQ931IE::Keypad,"keypad",m_keypad);
	return true;
    }
    m_keypad = msg->getIEValue(ISDNQ931IE::Keypad,"keypad");
    return !m_keypad.null();
}

/**
 * ISDNQ931State
 */
const TokenDict ISDNQ931State::s_states[] = {
	{"Null",                 Null},
	{"CallInitiated",        CallInitiated},
	{"OverlapSend",          OverlapSend},
	{"OutgoingProceeding",   OutgoingProceeding},
	{"CallDelivered",        CallDelivered},
	{"CallPresent",          CallPresent},
	{"CallReceived",         CallReceived},
	{"ConnectReq",           ConnectReq},
	{"IncomingProceeding",   IncomingProceeding},
	{"Active",               Active},
	{"DisconnectReq",        DisconnectReq},
	{"DisconnectIndication", DisconnectIndication},
	{"SuspendReq",           SuspendReq},
	{"ResumeReq",            ResumeReq},
	{"ReleaseReq",           ReleaseReq},
	{"CallAbort",            CallAbort},
	{"OverlapRecv",          OverlapRecv},
	{"RestartReq",           RestartReq},
	{"Restart",              Restart},
	{0,0}
	};

bool ISDNQ931State::checkStateRecv(int type, bool* retrans)
{
#define STATE_CHECK_RETRANS(st) \
	if (state() == st) { \
	    if (retrans) \
		*retrans = true; \
	    return false; \
	}
    switch (type) {
	case ISDNQ931Message::Setup:
	    STATE_CHECK_RETRANS(CallPresent)
	    if (state() != Null)
		break;
	    return true;
	case ISDNQ931Message::SetupAck:
	    STATE_CHECK_RETRANS(OverlapSend)
	    if (state() != CallInitiated)
		break;
	    return true;
	case ISDNQ931Message::Proceeding:
	    STATE_CHECK_RETRANS(OutgoingProceeding)
	    if (state() != CallInitiated && state() != OverlapSend)
		break;
	    return true;
	case ISDNQ931Message::Alerting:
	    STATE_CHECK_RETRANS(CallDelivered)
	    if (state() != CallInitiated && state() != OutgoingProceeding)
		break;
	    return true;
	case ISDNQ931Message::Connect:
	    STATE_CHECK_RETRANS(Active)
	    if (state() != CallInitiated && state() != OutgoingProceeding &&
		state() != CallDelivered)
		break;
	    return true;
	case ISDNQ931Message::ConnectAck:
	    STATE_CHECK_RETRANS(Active)
	    if (state() != ConnectReq && state() != Active)
		break;
	    return true;
	case ISDNQ931Message::Disconnect:
	    STATE_CHECK_RETRANS(DisconnectIndication)
	    switch (state()) {
		case CallInitiated:
		case OutgoingProceeding:
		case CallDelivered:
		case CallPresent:
		case CallReceived:
		case ConnectReq:
		case IncomingProceeding:
		case Active:
		case OverlapSend:
		    return true;
		default: ;
	    }
	    break;
	default:
	    if (state() == Null)
		break;
	    return true;
    }
    return false;
#undef STATE_CHECK_RETRANS
}

bool ISDNQ931State::checkStateSend(int type)
{
    switch (type) {
	case ISDNQ931Message::Setup:
	    if (state() != Null)
		break;
	    return true;
	case ISDNQ931Message::SetupAck:
	    if (state() != CallPresent)
		break;
	    return true;
	case ISDNQ931Message::Proceeding:
	    if (state() != CallPresent && state() != OverlapRecv)
		break;
	    return true;
	case ISDNQ931Message::Alerting:
	    if (state() != CallPresent && state() != IncomingProceeding)
		break;
	    return true;
	case ISDNQ931Message::Connect:
	    if (state() != CallPresent && state() != IncomingProceeding &&
		state() != CallReceived)
		break;
	    return true;
	case ISDNQ931Message::Disconnect:
	    switch (state()) {
		case OutgoingProceeding:
		case CallDelivered:
		case CallPresent:
		case CallReceived:
		case ConnectReq:
		case IncomingProceeding:
		case Active:
		case OverlapSend:
		    return true;
		default: ;
	    }
	    break;
	case ISDNQ931Message::Progress:
	    if (state() != CallPresent && state() != CallReceived &&
		state() != IncomingProceeding)
		break;
	    return true;
	default:
	    if (state() == Null)
		break;
	    return true;
    }
    return false;
}

/**
 * ISDNQ931Call
 */
#define Q931_CALL_ID this->outgoing(),this->callRef()

ISDNQ931Call::ISDNQ931Call(ISDNQ931* controller, bool outgoing,
	u_int32_t callRef, u_int8_t callRefLen, u_int8_t tei)
    : SignallingCall(controller,outgoing),
    m_callRef(callRef),
    m_callRefLen(callRefLen),
    m_tei(tei),
    m_circuit(0),
    m_circuitChange(false),
    m_channelIDSent(false),
    m_rspBearerCaps(false),
    m_inbandAvailable(false),
    m_net(false),
    m_data(controller && !controller->primaryRate()),
    m_discTimer(0),
    m_relTimer(0),
    m_conTimer(0),
    m_overlapSendTimer(0),
    m_overlapRecvTimer(0),
    m_retransSetupTimer(0),
    m_terminate(false),
    m_destroy(false),
    m_destroyed(false)
{
    Debug(q931(),DebugAll,"Call(%u,%u) direction=%s TEI=%u [%p]",
	Q931_CALL_ID,(outgoing ? "outgoing" : "incoming"),tei,this);
    for (u_int8_t i = 0; i < 127; i++)
	m_broadcast[i] = false;
    if (!controller) {
	Debug(DebugWarn,"ISDNQ931Call(%u,%u). No call controller. Terminate [%p]",
	    Q931_CALL_ID,this);
	m_terminate = m_destroy = true;
	m_data.m_reason = "temporary-failure";
	return;
    }
    m_net = q931() && q931()->network();
    // Init timers
    q931()->setInterval(m_discTimer,305);
    q931()->setInterval(m_relTimer,308);
    q931()->setInterval(m_conTimer,313);
    m_overlapSendTimer.interval(10000);
    m_overlapRecvTimer.interval(20000);
    m_retransSetupTimer.interval(1000);
    if (outgoing)
	reserveCircuit();
}

ISDNQ931Call::~ISDNQ931Call()
{
    q931()->releaseCircuit(m_circuit);
    if (state() != Null)
	sendReleaseComplete("temporary-failure");
    Debug(q931(),DebugAll,"Call(%u,%u) destroyed with reason '%s' [%p]",
	Q931_CALL_ID,m_data.m_reason.c_str(),this);
}

// Set terminate flags and reason
void ISDNQ931Call::setTerminate(bool destroy, const char* reason)
{
    Lock mylock(this);
    if (m_destroyed)
	return;
    if (state() == CallAbort)
	changeState(Null);
    // Check terminate & destroy flags
    if (m_terminate && destroy == m_destroy)
	return;
    m_terminate = true;
    m_destroy = destroy;
    if (m_data.m_reason.null())
	m_data.m_reason = reason;
    DDebug(q931(),DebugInfo,"Call(%u,%u). Set terminate. Destroy: %s [%p]",
	Q931_CALL_ID,String::boolText(m_destroy),this);
}

// Send an event
bool ISDNQ931Call::sendEvent(SignallingEvent* event)
{
    if (!event)
	return false;
    Lock mylock(this);
    DDebug(q931(),DebugAll,"Call(%u,%u). sendEvent(%s) state=%s [%p]",
	Q931_CALL_ID,event->name(),stateName(state()),this);
    if (m_terminate || state() == CallAbort) {
	mylock.drop();
	delete event;
	return false;
    }
    bool retVal = false;
    switch (event->type()) {
	case SignallingEvent::Progress:
	    retVal = sendProgress(event->message());
	    break;
	case SignallingEvent::Ringing:
	    retVal = sendAlerting(event->message());
	    break;
	case SignallingEvent::Accept:
	    if (m_overlap) {
		sendSetupAck();
		m_overlap = false;
		break;
	    }
	    changeState(CallPresent);
	    retVal = sendCallProceeding(event->message());
	    break;
	case SignallingEvent::Answer:
	    changeState(CallPresent);
	    retVal = sendConnect(event->message());
	    break;
	case SignallingEvent::Release:
	    switch (state()) {
		case DisconnectIndication:
		    retVal = sendRelease(0,event->message());
		    break;
		case OutgoingProceeding:
		case CallDelivered:
		case CallPresent:
		case CallReceived:
		case ConnectReq:
		case IncomingProceeding:
		case Active:
		    retVal = sendDisconnect(event->message());
		    break;
		case Null:
		case ReleaseReq:
		case CallAbort:
		    // Schedule destroy
		    m_terminate = m_destroy = true;
		    mylock.drop();
		    delete event;
		    return false;
		default:
		    m_terminate = m_destroy = true;
		    retVal = sendReleaseComplete(event->message() ?
			event->message()->params().getValue(YSTRING("reason")) : 0);
		    break;
	    }
	    break;
	case SignallingEvent::Info:
	    retVal = sendInfo(event->message());
	    break;
	case SignallingEvent::NewCall:
	    retVal = sendSetup(event->message());
	    break;
	default:
	    Debug(q931(),DebugStub,
		"Call(%u,%u). sendEvent not implemented for event '%s' [%p]",
		Q931_CALL_ID,event->name(),this);
    }
    mylock.drop();
    delete event;
    return retVal;
}

// Process received messages. Generate events from them
// Get events from reserved circuit when no call event
SignallingEvent* ISDNQ931Call::getEvent(const Time& when)
{
    Lock mylock(this);
    // Check for last event or destroyed/aborting
    if (m_lastEvent || m_destroyed || state() == CallAbort)
	return 0;
    while (true) {
	// Check for incoming messages
	ISDNQ931Message* msg = static_cast<ISDNQ931Message*>(dequeue());
	// No message: check terminate and timeouts. Try to get a circuit event
	if (!msg) {
	    if (m_terminate)
		m_lastEvent = processTerminate();
	    if (!m_lastEvent)
		m_lastEvent = checkTimeout(when.msec());
	    if (!m_lastEvent)
		m_lastEvent = getCircuitEvent(when);
	    break;
	}
	XDebug(q931(),DebugAll,
	    "Call(%u,%u). Dequeued message (%p): '%s' in state '%s' [%p]",
	    Q931_CALL_ID,msg,msg->name(),stateName(state()),this);
	// Check for unknown madatory IE. See Q.931 7.8.7.1
	if (msg->unknownMandatory()) {
	    Debug(q931(),DebugWarn,
		"Call(%u,%u). Received message (%p): '%s' with unknown mandatory IE [%p]",
		Q931_CALL_ID,msg,msg->name(),this);
	    TelEngine::destruct(msg);
	    m_lastEvent = releaseComplete("missing-mandatory-ie");
	    break;
	}
	switch (msg->type()) {
#define Q931_CALL_PROCESS_MSG(type,method) \
	    case type: \
		m_lastEvent = !m_terminate ? method(msg) : processTerminate(msg); \
		break;
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Alerting,processMsgAlerting)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Proceeding,processMsgCallProceeding)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Connect,processMsgConnect)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::ConnectAck,processMsgConnectAck)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Disconnect,processMsgDisconnect)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Info,processMsgInfo)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Notify,processMsgNotify)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Progress,processMsgProgress)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Release,processMsgRelease)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::ReleaseComplete,processMsgRelease)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Setup,processMsgSetup)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::SetupAck,processMsgSetupAck)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::Status,processMsgStatus)
	    Q931_CALL_PROCESS_MSG(ISDNQ931Message::StatusEnquiry,processMsgStatusEnquiry)
#undef Q931_CALL_PROCESS_MSG
	    case ISDNQ931Message::Suspend:
		sendSuspendRej("service-not-implemented",0);
		break;
	    case ISDNQ931Message::Resume:
		q931()->sendStatus(this,"no-call-suspended",callTei());
		break;
	    case ISDNQ931Message::SuspendAck:
	    case ISDNQ931Message::SuspendRej:
	    case ISDNQ931Message::ResumeAck:
	    case ISDNQ931Message::ResumeRej:
		q931()->sendStatus(this,"wrong-state-message",callTei());
		break;
	    default:
		DDebug(q931(),DebugNote,
		    "Call(%u,%u). Received unknown/not implemented message '%s'. Sending status [%p]",
		    Q931_CALL_ID,msg->name(),this);
		q931()->sendStatus(this,"unknown-message",callTei());
	        // Fall through to destruct the message and check timeouts
	}
	TelEngine::destruct(msg);
	if (!m_lastEvent)
	    m_lastEvent = checkTimeout(when.msec());
	if (!m_lastEvent)
	    m_lastEvent = getCircuitEvent(when);
	break;
    }
    if (!m_lastEvent)
	return 0;
    XDebug(q931(),DebugInfo,"Call(%u,%u). Raising event '%s' state=%s [%p]",
	Q931_CALL_ID,m_lastEvent->name(),stateName(state()),this);
    return m_lastEvent;
}

// Get reserved circuit or this object
void* ISDNQ931Call::getObject(const String& name) const
{
    if (name == YSTRING("SignallingCircuit"))
	return m_circuit;
    if (name == YSTRING("ISDNQ931Call"))
	return (void*)this;
    return SignallingCall::getObject(name);
}

// Data link change state notification from call controller
// Set termination flag. Send status if link is up
void ISDNQ931Call::dataLinkState(bool up)
{
    Lock mylock(this);
    // Q.931 5.8.9. Terminate if not up and not in the active state
    if (!up) {
	if (state() != ISDNQ931Call::Active)
	    setTerminate(true,"net-out-of-order");
	return;
    }
    // Q.931 5.8.8 Terminate in state OverlapSend and OverlapRecv
    if (state() == ISDNQ931Call::OverlapSend ||
	state() == ISDNQ931Call::OverlapRecv) {
	setTerminate(true,"temporary-failure");
    }
    q931()->sendStatus(this,"normal",callTei());
}

// Process termination flags or requests (messages)
SignallingEvent* ISDNQ931Call::processTerminate(ISDNQ931Message* msg)
{
    XDebug(q931(),DebugAll,"Call(%u,%u). processTerminate(%s) state=%s [%p]",
	Q931_CALL_ID,msg?msg->name():"",stateName(state()),this);
    bool complete = m_destroy;
    // We don't have to destroy and not send/received Release: Send Release
    if (!m_destroy && state() != ReleaseReq && state() != DisconnectReq)
	complete = false;
    // Message is Release/ReleaseComplete: terminate
    if (msg) {
	if (msg->type() == ISDNQ931Message::Release ||
	    msg->type() == ISDNQ931Message::ReleaseComplete) {
	    changeState(Null);
	    m_data.processCause(msg,false);
	    complete = true;
	}
	else
	    DDebug(q931(),DebugNote,
		"Call(%u,%u). Dropping received message '%s' while terminating [%p]",
		Q931_CALL_ID,msg->name(),this);
    }
    if (complete)
	return releaseComplete();
    sendRelease("normal-clearing");
    return 0;
}

// Check message timeout for Connect, Disconnect, Release, Setup
SignallingEvent* ISDNQ931Call::checkTimeout(u_int64_t time)
{
#define CALL_TIMEOUT_DEBUG(info) \
    DDebug(q931(),DebugNote, \
	"Call(%u,%u). %s request timed out in state '%s' [%p]", \
	Q931_CALL_ID,info,stateName(state()),this);
    static const char* reason = "timeout";
    switch (state()) {
	case DisconnectReq:
	    if (!m_discTimer.timeout(time))
		break;
	    CALL_TIMEOUT_DEBUG("Disconnect")
	    m_discTimer.stop();
	    sendRelease(reason);
	    break;
	case ReleaseReq:
	    if (!m_relTimer.timeout(time))
		break;
	    CALL_TIMEOUT_DEBUG("Release")
	    m_relTimer.stop();
	    changeState(Null);
	    return releaseComplete(reason);
	case ConnectReq:
	    if (!m_conTimer.timeout(time))
		break;
	    CALL_TIMEOUT_DEBUG("Connect")
	    m_conTimer.stop();
	    m_data.m_reason = reason;
	    sendDisconnect(0);
	    break;
	case CallInitiated:
	    if (!m_retransSetupTimer.timeout(time))
		break;
	    CALL_TIMEOUT_DEBUG("Setup")
	    m_retransSetupTimer.stop();
	    m_data.m_reason = reason;
	    return releaseComplete(reason);
	case OverlapSend:
	    if (!m_overlapSendTimer.timeout(time)) {
		m_overlapSendTimer.stop();
		m_overlapSendTimer.start();
	    }
	    break;
	default: ;
    }
    return 0;
#undef CALL_TIMEOUT_DEBUG
}

// Check received messages for appropriate state or retransmission
// Send status if not accepted and requested by the caller
bool ISDNQ931Call::checkMsgRecv(ISDNQ931Message* msg, bool status)
{
    bool retrans = false;
    if (checkStateRecv(msg->type(),&retrans))
	return true;
    if (retrans)
	XDebug(q931(),DebugAll,
	    "Call(%u,%u). Dropping '%s' retransmission in state '%s' [%p]",
	    Q931_CALL_ID,msg->name(),stateName(state()),this);
    else {
	Debug(q931(),DebugNote,
	    "Call(%u,%u). Received '%s'. Invalid in state '%s'. Drop [%p]",
	    Q931_CALL_ID,msg->name(),stateName(state()),this);
	if (status && state() != Null)
	    q931()->sendStatus(this,"wrong-state-message",callTei());
    }
    return false;
}

// Process ALERTING. See Q.931 3.1.1
// IE: BearerCaps, ChannelID, Progress, Display, Signal, HiLayerCompat
SignallingEvent* ISDNQ931Call::processMsgAlerting(ISDNQ931Message* msg)
{
    if (!checkMsgRecv(msg,true))
	return 0;
    if (m_data.processChannelID(msg,false) && !reserveCircuit())
	return releaseComplete();
    // Notify format and circuit change
    if (m_circuitChange) {
	m_circuitChange = false;
	msg->params().setParam("circuit-change",String::boolText(true));
    }
    if (m_data.processBearerCaps(msg,false) && !m_data.m_format.null())
	msg->params().setParam("format",m_data.m_format);
    // Check if inband ringback is available
    if (m_data.processProgress(msg,false))
	m_inbandAvailable = m_inbandAvailable ||
	    SignallingUtils::hasFlag(m_data.m_progress,"in-band-info");
    msg->params().addParam("earlymedia",String::boolText(m_inbandAvailable));
    changeState(CallDelivered);
    return new SignallingEvent(SignallingEvent::Ringing,msg,this);
}

// Process CALL PROCEEDING. See Q.931 3.1.2
// IE: BearerCaps, ChannelID, Progress, Display, HiLayerCompat
SignallingEvent* ISDNQ931Call::processMsgCallProceeding(ISDNQ931Message* msg)
{
    if (!checkMsgRecv(msg,true))
	return 0;
    if (m_data.processChannelID(msg,false) && !reserveCircuit())
	return releaseComplete();
    // Notify format and circuit change
    if (m_circuitChange) {
	m_circuitChange = false;
	msg->params().setParam("circuit-change",String::boolText(true));
    }
    if (m_data.processBearerCaps(msg,false) && !m_data.m_format.null())
	msg->params().setParam("format",m_data.m_format);
    changeState(OutgoingProceeding);
    return new SignallingEvent(SignallingEvent::Accept,msg,this);
}

// Process CONNECT. See Q.931 3.1.3
// IE: BearerCaps, ChannelID, Progress, Display, DateTime, Signal, LoLayerCompat, HiLayerCompat
SignallingEvent* ISDNQ931Call::processMsgConnect(ISDNQ931Message* msg)
{
    m_retransSetupTimer.stop();
    if (!checkMsgRecv(msg,true))
	return 0;
    if (m_data.processChannelID(msg,false) && !reserveCircuit())
	return releaseComplete();
    // This is the last time we can receive a circuit. Check if we reserved one
    if (!m_circuit)
	return releaseComplete("invalid-message");
    // Notify format and circuit change
    if (m_circuitChange) {
	m_circuitChange = false;
	msg->params().setParam("circuit-change",String::boolText(true));
    }
    if (m_data.processBearerCaps(msg,false) && !m_data.m_format.null())
	msg->params().setParam("format",m_data.m_format);
    changeState(ConnectReq);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Answer,msg,this);
    sendConnectAck(0);
    return event;
}

// Process CONNECT ACK. See Q.931 3.1.4
// IE: Display, Signal
SignallingEvent* ISDNQ931Call::processMsgConnectAck(ISDNQ931Message* msg)
{
    m_conTimer.stop();
    // Check if we've changed state to Active when sent Connect
    bool yes = q931() && !q931()->parserData().flag(ISDNQ931::NoActiveOnConnect);
    if (yes && state() == Active)
	return 0;
    if (!checkMsgRecv(msg,false))
	return 0;
    changeState(Active);
    return 0;
}

// Process DISCONNECT. See Q.931 3.1.5
// IE: Cause, Progress, Display, Signal
SignallingEvent* ISDNQ931Call::processMsgDisconnect(ISDNQ931Message* msg)
{
    if (state() == DisconnectReq) {
	// Disconnect requested concurrently from both sides
	sendRelease();
	return 0;
    }
    if (!checkMsgRecv(msg,false))
	return 0;
    m_discTimer.stop();
    changeState(DisconnectIndication);
    if (m_data.processCause(msg,false))
	msg->params().setParam("reason",m_data.m_reason);
    return new SignallingEvent(SignallingEvent::Release,msg,this);
}

// Process INFORMATION. See Q.931 3.1.6
// IE: SendComplete, Display, Keypad, Signal, CalledNo
SignallingEvent* ISDNQ931Call::processMsgInfo(ISDNQ931Message* msg)
{
    m_lastEvent = checkTimeout(10000);
    // Check complete
    bool complete = (0 != msg->getIE(ISDNQ931IE::SendComplete));
    msg->params().addParam("complete",String::boolText(complete));
    // Display
    m_data.processDisplay(msg,false);
    // Check tones
    const char* tone = msg->getIEValue(ISDNQ931IE::CalledNo,"number");
    if (!tone)
	tone = msg->getIEValue(ISDNQ931IE::Keypad,"keypad");
    if (tone)
	msg->params().addParam("tone",tone);
    return new SignallingEvent(SignallingEvent::Info,msg,this);
}

// Process NOTIFY. See Q.931 3.1.7
// IE: BearerCaps, Notification, Display
SignallingEvent* ISDNQ931Call::processMsgNotify(ISDNQ931Message* msg)
{
    m_data.processNotification(msg,false);
    DDebug(q931(),DebugNote,
	"Call(%u,%u). Received '%s' with '%s'='%s' [%p]",
	Q931_CALL_ID,msg->name(),ISDNQ931IE::typeName(ISDNQ931IE::Notification),
	m_data.m_notification.c_str(),this);
    return 0;
}

// Process PROGRESS. See Q.931 3.1.8
// IE: BearerCaps, Cause, Progress (mandatory), Display, HiLayerCompat
SignallingEvent* ISDNQ931Call::processMsgProgress(ISDNQ931Message* msg)
{
    // Q.931 says that we should ignore the message. We don't
    if (m_data.processProgress(msg,false))
	m_inbandAvailable = m_inbandAvailable ||
	    SignallingUtils::hasFlag(m_data.m_progress,"in-band-info");
    msg->params().addParam("earlymedia",String::boolText(m_inbandAvailable));
    if (m_data.processCause(msg,false))
	msg->params().setParam("reason",m_data.m_reason);
    if (m_data.processDisplay(msg,false))
	msg->params().setParam("callername",m_data.m_display);
    return new SignallingEvent(SignallingEvent::Progress,msg,this);
}

// Process RELEASE and RELEASE COMPLETE. See Q.931 3.1.9/3.1.10
// IE: Cause, Display, Signal
SignallingEvent* ISDNQ931Call::processMsgRelease(ISDNQ931Message* msg)
{
    if (!msg)
	return 0;
    m_discTimer.stop();
    m_relTimer.stop();
    m_conTimer.stop();
    if (!checkMsgRecv(msg,false))
	return 0;
    m_data.processCause(msg,false);
    if (m_data.m_reason.null())
	m_data.m_reason = "normal-clearing";
    msg->params().setParam("reason",m_data.m_reason);
    if (state() != ReleaseReq && msg->type() == ISDNQ931Message::Release)
	changeState(ReleaseReq);
    else
	changeState(Null);
    return releaseComplete();
}

// Process SETUP. See Q.931 3.1.14
// IE: Repeat, BearerCaps, ChannelID, Progress, NetFacility, Display,
//     Keypad, Signal, CallingNo, CallingSubAddr, CalledNo, CalledSubAddr,
//     NetTransit, Repeat, LoLayerCompat, HiLayerCompat
SignallingEvent* ISDNQ931Call::processMsgSetup(ISDNQ931Message* msg)
{
    if (!checkMsgRecv(msg,true))
	return 0;
    changeState(CallPresent);
    // *** BearerCaps. Mandatory
    if (!m_data.processBearerCaps(msg,false))
	return errorNoIE(msg,ISDNQ931IE::BearerCaps,true);
    // Check for multiple BearerCaps
    ISDNQ931IE* bc = msg->getIE(ISDNQ931IE::BearerCaps);
    if (bc && msg->getIE(ISDNQ931IE::BearerCaps,bc))
	m_rspBearerCaps = true;
    // Check if transfer mode is 'circuit'
    if (m_data.m_transferMode != "circuit") {
	Debug(q931(),DebugWarn,
	    "Call(%u,%u). Invalid or missing transfer mode '%s'. Releasing call [%p]",
	    Q931_CALL_ID,m_data.m_transferMode.c_str(),this);
	return errorWrongIE(msg,ISDNQ931IE::BearerCaps,true);
    }
    // *** ChannelID. Mandatory on PRI
    if (msg->getIE(ISDNQ931IE::ChannelID))
	m_data.processChannelID(msg,false);
    else if (q931() && q931()->primaryRate())
	return errorNoIE(msg,ISDNQ931IE::ChannelID,true);
    // Check if channel contains valid PRI/BRI flag
    if (q931() && (m_data.m_bri == q931()->primaryRate())) {
	Debug(q931(),DebugWarn,
	    "Call(%u,%u). Invalid interface type. Releasing call [%p]",
	    Q931_CALL_ID,this);
	return errorWrongIE(msg,ISDNQ931IE::ChannelID,true);
    }
    // Get a circuit from controller
    if (reserveCircuit())
	m_circuit->updateFormat(m_data.m_format,0);
    else if (q931() && q931()->primaryRate())
	return releaseComplete("congestion");
    // *** CalledNo /CallingNo
    m_overlap = !m_data.processCalledNo(msg,false);
    m_data.processCallingNo(msg,false);
    // *** Display
    m_data.processDisplay(msg,false);
    // Set message parameters
    msg->params().setParam("caller",m_data.m_callerNo);
    msg->params().setParam("called",m_data.m_calledNo);
    msg->params().setParam("format",m_data.m_format);
    msg->params().setParam("callername",m_data.m_display);
    msg->params().setParam("callernumtype",m_data.m_callerType);
    msg->params().setParam("callernumplan",m_data.m_callerPlan);
    msg->params().setParam("callerpres",m_data.m_callerPres);
    msg->params().setParam("callerscreening",m_data.m_callerScreening);
    msg->params().setParam("callednumtype",m_data.m_calledType);
    msg->params().setParam("callednumplan",m_data.m_calledPlan);
    msg->params().setParam("overlapped",String::boolText(m_overlap));
    return new SignallingEvent(SignallingEvent::NewCall,msg,this);
}

// Process SETUP ACKNOLEDGE. See Q.931 3.1.14
// IE: ChannelID, Progress, Display, Signal
SignallingEvent* ISDNQ931Call::processMsgSetupAck(ISDNQ931Message* msg)
{
    if (!checkMsgRecv(msg,true))
	return 0;
    if (!m_data.processChannelID(msg,false))
	return errorWrongIE(msg,ISDNQ931IE::ChannelID,true);
    // We don't implement overlap sending. So, just complete the number sending
    SignallingMessage* m = new SignallingMessage;
    m->params().addParam("complete",String::boolText(true));
    sendInfo(m);
    return 0;
}

// Process STATUS. See Q.931 3.1.15, 5.8.11
// Try to recover (retransmit) messages based on received status
// IE: Cause, CallState, Display
SignallingEvent* ISDNQ931Call::processMsgStatus(ISDNQ931Message* msg)
{
    const char* s = msg->getIEValue(ISDNQ931IE::CallState,"state");
    if (!m_data.processCause(msg,false))
	m_data.m_reason = "unknown";
    DDebug(q931(),DebugInfo,
	"Call(%u,%u). Received '%s' state=%s peer-state=%s cause='%s' [%p]",
	Q931_CALL_ID,msg->name(),
	stateName(state()),s,m_data.m_reason.c_str(),this);
    u_int8_t peerState = (u_int8_t)lookup(s,s_states,255);
    // Check for valid state
    if (peerState == 255)
	return 0;
    // Check for Null states (our's and peer's Null state)
    if (state() == Null) {
	if (peerState != Null) {
	    // Change state to allow sending RELEASE COMPLETE
	    changeState(CallAbort);
	    sendReleaseComplete("wrong-state-message");
	}
	return 0;
    }
    if (peerState == Null)
	return releaseComplete();
    // Check peer wrong states (these are states associated with dummy call reference)
    if (peerState == Restart || peerState == RestartReq)
	return releaseComplete("wrong-state-message");
    // Check if we are releasing the call
    // Release the call, even if peer's state is a compatible one
    switch (state()) {
	case DisconnectReq:
	case DisconnectIndication:
	case SuspendReq:
	case ResumeReq:
	case ReleaseReq:
	case CallAbort:
	    return releaseComplete("wrong-state-message");
	default: ;
    }
    // Try to recover
    // This can be done only if we assume that the peer didn't saw our last message
    SignallingMessage* sigMsg = new SignallingMessage;
    bool recover = false;
    switch (state()) {
	case CallReceived:
	    // Sent Alerting
	    // Can recover if peer's state is OutgoingProceeding
	    if (peerState == OutgoingProceeding) {
		changeState(IncomingProceeding);
		sendAlerting(sigMsg);
		recover = true;
	    }
	    break;
	case ConnectReq:
	    // Sent Connect
	    // Can recover if peer's state is OutgoingProceeding or CallDelivered
	    // (saw our Alerting or Proceeding)
	    if (peerState == OutgoingProceeding || peerState == CallDelivered) {
		changeState(CallReceived);
		sendConnect(sigMsg);
		recover = true;
	    }
	    break;
	case IncomingProceeding:
	    // Sent Proceeding
	    // Can recover if peer's state is CallInitiated
	    // TODO: if overlap implemented: check if we received a Setup with full called number
	    if (peerState == CallInitiated) {
		changeState(CallPresent);
		sendCallProceeding(sigMsg);
		recover = true;
	    }
	    break;
	case Active:
	    // Incoming: received ConnectAck. Nothing to be done
	    // Outgoing: Sent ConnectAck. Recover only if peer's state is ConnectReq
	    if (outgoing() && peerState == ConnectReq) {
		changeState(ConnectReq);
		sendConnectAck(sigMsg);
		recover = true;
	    }
	    else if (peerState == Active) {
		Debug(q931(),DebugNote,"Call(%u,%u). Recovering from STATUS, cause='%s' [%p]",
		    Q931_CALL_ID,m_data.m_reason.c_str(),this);
		recover = true;
	    }
	case CallInitiated:	    // We've sent Setup. Can't recover: something went wrong
	case OverlapSend:
	case OverlapRecv:	    // TODO: implement if overlap send/recv is implemented
	case CallDelivered:	    // Received Alerting. Sent nothing. Can't recover
	case CallPresent:	    // Received Setup. Sent nothing. Can't recover
	case OutgoingProceeding:    // Received Proceeding. Sent nothing. Can't recover
	    break;
	default: ;
    }
    TelEngine::destruct(sigMsg);
    if (!recover)
	return releaseComplete("wrong-state-message");
    return 0;
}

// Process STATUS ENQUIRY. See Q.931 3.1.16, 5.8.10
// IE: Display
SignallingEvent* ISDNQ931Call::processMsgStatusEnquiry(ISDNQ931Message* msg)
{
    q931()->sendStatus(this,"status-enquiry-rsp",callTei());
    return 0;
}

// Check if the state allows to send a message
#define MSG_CHECK_SEND(type) \
	if (!(q931() && checkStateSend(type))) { \
	    DDebug(q931(),DebugNote, \
		"Call(%u,%u). Can't send msg='%s' in state=%s. %s [%p]", \
		Q931_CALL_ID,ISDNQ931Message::typeName(type), \
		stateName(state()),(q931()?"Invalid state":"No call controller"),\
		this); \
	    return false; \
	}

// Send ALERTING. See Q.931 3.1.1
// IE: BearerCaps, ChannelID, Progress, Display, Signal, HiLayerCompat
bool ISDNQ931Call::sendAlerting(SignallingMessage* sigMsg)
{
    MSG_CHECK_SEND(ISDNQ931Message::Alerting)
    const char* format = 0;
    if (sigMsg) {
	format = sigMsg->params().getValue(YSTRING("format"));
	m_inbandAvailable = m_inbandAvailable ||
	    sigMsg->params().getBoolValue(YSTRING("earlymedia"),false);
	if (m_inbandAvailable)
	    SignallingUtils::appendFlag(m_data.m_progress,"in-band-info");
    }
    if (format)
	m_data.m_format = format;
    // Change state, send message
    changeState(CallReceived);
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Alerting,this);
    if (m_rspBearerCaps) {
	m_data.processBearerCaps(msg,true);
	m_rspBearerCaps = false;
    }
    if (!m_channelIDSent) {
	if (!q931()->primaryRate()) {
	    m_data.m_channelType = "B";
	    if (m_circuit)
		m_data.m_channelSelect = lookup(m_circuit->code(),Q931Parser::s_dict_channelIDSelect_BRI);
	    if (!m_data.m_channelSelect) {
		TelEngine::destruct(msg);
		return sendReleaseComplete("congestion");
	    }
	}
	m_data.processChannelID(msg,true,&q931()->parserData());
	m_channelIDSent = true;
    }
    m_data.processProgress(msg,true);
    return q931()->sendMessage(msg,callTei());
}

// Send CALL PROCEEDING. See Q.931 3.1.2
// IE: BearerCaps, ChannelID, Progress, Display, HiLayerCompat
bool ISDNQ931Call::sendCallProceeding(SignallingMessage* sigMsg)
{
    MSG_CHECK_SEND(ISDNQ931Message::Proceeding)
    // Change state, send message
    changeState(IncomingProceeding);
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Proceeding,this);
    if (m_rspBearerCaps) {
	m_data.processBearerCaps(msg,true);
	m_rspBearerCaps = false;
    }
    if (!m_channelIDSent) {
	m_data.processChannelID(msg,true);
	m_channelIDSent = true;
    }
    return q931()->sendMessage(msg,callTei());
}

// Send CONNECT. See Q.931 3.1.3
// IE: BearerCaps, ChannelID, Progress, Display, DateTime, Signal,
//     LoLayerCompat, HiLayerCompat
bool ISDNQ931Call::sendConnect(SignallingMessage* sigMsg)
{
    MSG_CHECK_SEND(ISDNQ931Message::Connect)
    // Change state, start timer, send message
    if (q931()->parserData().flag(ISDNQ931::NoActiveOnConnect))
	changeState(ConnectReq);
    else
	changeState(Active);
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Connect,this);
    if (m_rspBearerCaps) {
	m_data.processBearerCaps(msg,true,&q931()->parserData());
	m_rspBearerCaps = false;
    }
    if (!m_channelIDSent) {
	if (!q931()->primaryRate()) {
	    m_data.m_channelType = "B";
	    m_data.m_channelByNumber = true;
	    m_data.m_channelSelect = lookup(m_circuit->code(),Q931Parser::s_dict_channelIDSelect_BRI);
	}
	m_data.processChannelID(msg,true,&q931()->parserData());
	m_channelIDSent = true;
    }
    // Progress indicator
    if (sigMsg) {
	m_data.m_progress = sigMsg->params().getValue(YSTRING("call-progress"));
	m_data.processProgress(msg,true,&q931()->parserData());
    }
    m_conTimer.start();
    return q931()->sendMessage(msg,callTei());
}

// Send CONNECT ACK. See Q.931 3.1.4
// IE: Display, Signal
bool ISDNQ931Call::sendConnectAck(SignallingMessage* sigMsg)
{
    MSG_CHECK_SEND(ISDNQ931Message::ConnectAck)
    // Change state, send message
    changeState(Active);
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::ConnectAck,this);
    // Progress indicator
    if (sigMsg) {
	m_data.m_progress = sigMsg->params().getValue(YSTRING("call-progress"));
	m_data.processProgress(msg,true,&q931()->parserData());
    }
    else
	m_data.m_progress = "";
    return q931()->sendMessage(msg,callTei());
}

// Send DISCONNECT. See Q.931 3.1.5
// IE: Cause, Progress, Display, Signal
bool ISDNQ931Call::sendDisconnect(SignallingMessage* sigMsg)
{
    MSG_CHECK_SEND(ISDNQ931Message::Disconnect)
    m_data.m_reason = "";
    if (sigMsg)
	m_data.m_reason = sigMsg->params().getValue(YSTRING("reason"));
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Disconnect,this);
    m_data.processCause(msg,true);
    changeState(DisconnectReq);
    m_discTimer.start();
    return q931()->sendMessage(msg,callTei());
}

// Send INFORMATION. See Q.931 3.1.6
// IE: SendComplete, Display, Keypad, Signal, CalledNo
bool ISDNQ931Call::sendInfo(SignallingMessage* sigMsg)
{
    if (!sigMsg)
	return false;
    MSG_CHECK_SEND(ISDNQ931Message::Info)
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Info,this);
    // Check send complete complete
    if (sigMsg->params().getBoolValue(YSTRING("complete")))
	msg->appendSafe(new ISDNQ931IE(ISDNQ931IE::SendComplete));
    m_data.m_display = sigMsg->params().getValue(YSTRING("display"));
    m_data.processDisplay(msg,true,&q931()->parserData());
    // Check tones or ringing
    const char* tone = sigMsg->params().getValue(YSTRING("tone"));
    if (tone)
	msg->appendIEValue(ISDNQ931IE::Keypad,"keypad",tone);
    return q931()->sendMessage(msg,callTei());
}

// Send PROGRESS. See Q.931 3.1.8
// IE: BearerCaps, Cause, Progress (mandatory), Display, HiLayerCompat
bool ISDNQ931Call::sendProgress(SignallingMessage* sigMsg)
{
    MSG_CHECK_SEND(ISDNQ931Message::Progress)
    if (sigMsg) {
	m_data.m_progress = sigMsg->params().getValue(YSTRING("progress"));
	m_inbandAvailable = m_inbandAvailable ||
	    sigMsg->params().getBoolValue(YSTRING("earlymedia"),false);
	if (m_inbandAvailable)
	    SignallingUtils::appendFlag(m_data.m_progress,"in-band-info");
    }
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Progress,this);
    m_data.processProgress(msg,true);
    return q931()->sendMessage(msg,callTei());
}

// Send RELEASE. See Q.931 3.1.9
// IE: Cause, Display, Signal
bool ISDNQ931Call::sendRelease(const char* reason, SignallingMessage* sigMsg)
{
    if (state() == ReleaseReq || state() == Null)
	return false;
    // Get reason
    if (!reason && sigMsg)
	reason = sigMsg->params().getValue(YSTRING("reason"),0);
    if (reason)
	m_data.m_reason = reason;
    m_terminate = true;
    changeState(ReleaseReq);
    m_relTimer.start();
    return q931()->sendRelease(this,true,m_data.m_reason,callTei());
}

// Send RELEASE COMPLETE. See Q.931 3.1.10
// IE: Cause, Display, Signal
bool ISDNQ931Call::sendReleaseComplete(const char* reason, const char* diag, u_int8_t tei)
{
    m_relTimer.stop();
    if ((state() == Null) && (0 == tei))
	return false;
    if (reason)
	m_data.m_reason = reason;
    m_terminate = m_destroy = true;
    changeState(Null);
    q931()->releaseCircuit(m_circuit);
    if (callTei() >= 127) {
	for (u_int8_t i = 0; i < 127; i++)
	    if (m_broadcast[i])
		return q931()->sendRelease(this,false,m_data.m_reason,i,diag);
	return true;
    }
    if (0 == tei)
	tei = callTei();
    return q931()->sendRelease(this,false,m_data.m_reason,tei,diag);
}

// Send SETUP. See Q.931 3.1.14
// IE: Repeat, BearerCaps, ChannelID, Progress, NetFacility, Display,
//     Keypad, Signal, CallingNo, CallingSubAddr, CalledNo, CalledSubAddr,
//     NetTransit, Repeat, LoLayerCompat, HiLayerCompat
bool ISDNQ931Call::sendSetup(SignallingMessage* sigMsg)
{
    if (!sigMsg)
	return false;
    MSG_CHECK_SEND(ISDNQ931Message::Setup)
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Setup,this);
    while (true) {
	// TODO: fix it (don't send?) if overlapp dialing is used
	if (q931()->parserData().flag(ISDNQ931::ForceSendComplete))
	    msg->appendSafe(new ISDNQ931IE(ISDNQ931IE::SendComplete));
	// BearerCaps
	m_data.m_transferCapability = "speech";
	m_data.m_transferMode = "circuit";
	m_data.m_transferRate = "64kbit";
	m_data.m_format = sigMsg->params().getValue(YSTRING("format"),q931()->format());
	if (0xffff == lookup(m_data.m_format,Q931Parser::s_dict_bearerProto1,0xffff))
	    m_data.m_format = "alaw";
	m_data.processBearerCaps(msg,true);
	// ChannelID
	if (!m_circuit)
	    break;
	if (m_net || q931()->primaryRate()) {
	    // Reserving a circuit attempted only on PRI or if we are NET
	    if (!reserveCircuit()) {
		m_data.m_reason = "network-busy";
		break;
	    }
	    m_circuit->updateFormat(m_data.m_format,0);
	    m_data.m_channelMandatory = sigMsg->params().getBoolValue(YSTRING("channel-exclusive"),
		q931()->parserData().flag(ISDNQ931::ChannelExclusive));
	    m_data.m_channelByNumber = true;
	    m_data.m_channelType = "B";
	    if (m_data.m_bri) {
		if (m_circuit->code() > 0 && m_circuit->code() < 3)
		    m_data.m_channelSelect = lookup(m_circuit->code(),Q931Parser::s_dict_channelIDSelect_BRI);
		if (!m_data.m_channelSelect) {
		    m_data.m_reason = "network-busy";
		    break;
		}
	    }
	    else {
		m_data.m_channelSelect = "present";
		m_data.m_channels = m_circuit->code();
	    }
	    m_data.processChannelID(msg,true);
	}
	// Progress indicator
	m_data.m_progress = sigMsg->params().getValue(YSTRING("call-progress"));
	m_data.processProgress(msg,true,&q931()->parserData());
	// Display
	m_data.m_display = sigMsg->params().getValue(YSTRING("callername"));
	m_data.processDisplay(msg,true,&q931()->parserData());
	// CallingNo
	m_data.m_callerType = sigMsg->params().getValue(YSTRING("callernumtype"),q931()->numType());
	m_data.m_callerPlan = sigMsg->params().getValue(YSTRING("callernumplan"),q931()->numPlan());
	m_data.m_callerPres = sigMsg->params().getValue(YSTRING("callerpres"),q931()->numPresentation());
	m_data.m_callerScreening = sigMsg->params().getValue(YSTRING("callerscreening"),q931()->numScreening());
	m_data.m_callerNo = sigMsg->params().getValue(YSTRING("caller"));
	m_data.processCallingNo(msg,true);
	// CalledNo
	m_data.m_calledType = sigMsg->params().getValue(YSTRING("callednumtype"));
	m_data.m_calledPlan = sigMsg->params().getValue(YSTRING("callednumplan"));
	m_data.m_calledNo = sigMsg->params().getValue(YSTRING("called"));
	m_data.processCalledNo(msg,true);
	// Send
	changeState(CallInitiated);
	if (m_net && !q931()->primaryRate()) {
	    m_tei = 127;
	    m_retransSetupTimer.start();
	}
	if (q931()->sendMessage(msg,callTei(),&m_data.m_reason))
	    return true;
	msg = 0;
	break;
    }
    TelEngine::destruct(msg);
    setTerminate(true,0);
    return false;
}

// Send SUSPEND REJECT. See Q.931 3.1.20
// IE: Cause, Display
bool ISDNQ931Call::sendSuspendRej(const char* reason, SignallingMessage* sigMsg)
{
    if (!reason && sigMsg)
	reason = sigMsg->params().getValue(YSTRING("reason"));
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::SuspendRej,this);
    msg->appendIEValue(ISDNQ931IE::Cause,0,reason);
    return q931()->sendMessage(msg,callTei());
}

bool ISDNQ931Call::sendSetupAck()
{
    MSG_CHECK_SEND(ISDNQ931Message::SetupAck)
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::SetupAck,this);
    if (!m_channelIDSent) {
	m_data.m_channelType = "B";
	if (m_circuit)
	    m_data.m_channelSelect = lookup(m_circuit->code(),Q931Parser::s_dict_channelIDSelect_BRI);
	if (!m_data.m_channelSelect) {
	    Debug(q931(),DebugNote,"Call(%u,%u). No voice channel available [%p]",
		Q931_CALL_ID,this);
	    return sendReleaseComplete("congestion");
	}
	m_data.processChannelID(msg,true,&q931()->parserData());
	m_channelIDSent = true;
    }
    return q931()->sendMessage(msg,callTei());
}

SignallingEvent* ISDNQ931Call::releaseComplete(const char* reason, const char* diag)
{
    Lock mylock(this);
    if (m_destroyed)
	return 0;
    if (reason)
	m_data.m_reason = reason;
    DDebug(q931(),DebugInfo,
	"Call(%u,%u). Call release in state '%s'. Reason: '%s' [%p]",
	Q931_CALL_ID,stateName(state()),m_data.m_reason.c_str(),this);
    sendReleaseComplete(reason,diag);
    // Cleanup
    q931()->releaseCircuit(m_circuit);
    changeState(Null);
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::ReleaseComplete,this);
    msg->params().addParam("reason",m_data.m_reason);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Release,msg,this);
    TelEngine::destruct(msg);
    deref();
    m_destroyed = m_terminate = m_destroy = true;
    return event;
}

// Get an event from the reserved circuit
SignallingEvent* ISDNQ931Call::getCircuitEvent(const Time& when)
{
    if (!m_circuit)
	return 0;
    SignallingCircuitEvent* ev = m_circuit->getEvent(when);
    if (!ev)
	return 0;
    SignallingEvent* event = 0;
    switch (ev->type()) {
	case SignallingCircuitEvent::Dtmf: {
	    const char* tone = ev->getValue(YSTRING("tone"));
	    if (!(tone && *tone))
		break;
	    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Info,this);
	    msg->params().addParam("tone",tone);
	    msg->params().addParam("inband",String::boolText(true));
	    event = new SignallingEvent(SignallingEvent::Info,msg,this);
	    TelEngine::destruct(msg);
	    }
	    break;
	default: ;
    }
    delete ev;
    return event;
}

// Reserve and connect a circuit. Change the reserved one if it must to
bool ISDNQ931Call::reserveCircuit()
{
    m_circuitChange = false;
    bool anyCircuit = false;
    while (true) {
	// For incoming BRI calls we reserve the circuit only one time (at SETUP)
	if (!(outgoing() || q931()->primaryRate())) {
	    // Check if we are a BRI NET and we should assign any channel
	    int briChan = lookup(m_data.m_channelSelect,Q931Parser::s_dict_channelIDSelect_BRI,3);
	    if (m_net && (briChan == 3))
		anyCircuit = true;
	    else
		m_data.m_channels = briChan;
	    break;
	}
	// Outgoing calls
	if (!m_data.m_channelByNumber) {
	    m_data.m_reason = "service-not-implemented";
	    return false;
	}
	int reqCircuit = m_data.m_channels.toInteger(-1);
	// Check if we don't have a circuit reserved
	if (!m_circuit) {
	    anyCircuit = (outgoing() || (reqCircuit < 0 && !m_data.m_channelMandatory)) &&
		(m_net || q931()->primaryRate());
	    break;
	}
	// Check the received circuit if any
	if ((int)m_circuit->code() == reqCircuit)
	    return true;
	// We already have a circuit and received a different one: force mandatory
	m_data.m_channelMandatory = true;
	break;
    }
    // Reserve the circuit
    m_circuitChange = true;
    if (anyCircuit)
	q931()->reserveCircuit(m_circuit);
    else
	q931()->reserveCircuit(m_circuit,0,-1,&m_data.m_channels,m_data.m_channelMandatory,true);
    if (m_circuit) {
	m_data.m_channels = m_circuit->code();
	u_int64_t t = Time::msecNow();
	if (!m_circuit->connect(m_data.m_format) && !m_net && (state() != ISDNQ931State::CallPresent)) {
	    Debug(q931(),DebugNote,
		"Call(%u,%u). Failed to connect circuit [%p]",Q931_CALL_ID,this);
	    return false;
	}
	t = Time::msecNow() - t;
	if (t > 100) {
	    int level = DebugInfo;
	    if (t > 300)
		level = DebugMild;
	    else if (t > 200)
		level = DebugNote;
	    Debug(q931(),level,"Call(%u,%u). Connected to circuit %u in %u ms [%p]",
		Q931_CALL_ID,m_circuit->code(),(unsigned int)t,this);
	}
#ifdef DEBUG
	else
	    Debug(q931(),DebugAll,"Call(%u,%u). Connected to circuit %u in %u ms [%p]",
		Q931_CALL_ID,m_circuit->code(),(unsigned int)t,this);
#endif
	return true;
    }
    DDebug(q931(),DebugNote,
	"Call(%u,%u). Can't reserve%s circuit [%p]",
	Q931_CALL_ID,(anyCircuit ? " any" : ""),this);
    m_data.m_reason = anyCircuit ? "congestion" : "channel-unacceptable";
    return false;
}

// Print debug message on missing IE
// Generate a Release event if requested by caller
SignallingEvent* ISDNQ931Call::errorNoIE(ISDNQ931Message* msg,
	ISDNQ931IE::Type type, bool release)
{
    Debug(q931(),DebugNote,
	"Call(%u,%u). Received '%s' without mandatory IE '%s' [%p]",
	Q931_CALL_ID,msg->name(),ISDNQ931IE::typeName(type),this);
    if (release) {
	unsigned char c = (unsigned char)type;
	String tmp;
	tmp.hexify(&c,1);
	return releaseComplete("missing-mandatory-ie",tmp);
    }
    return 0;
}

// Print debug message on wrong IE
// Generate a Release event if requested by caller
SignallingEvent* ISDNQ931Call::errorWrongIE(ISDNQ931Message* msg,
	ISDNQ931IE::Type type, bool release)
{
    Debug(q931(),DebugNote,
	"Call(%u,%u). Received '%s' containing IE '%s' with wrong data [%p]",
	Q931_CALL_ID,msg->name(),ISDNQ931IE::typeName(type),this);
    if (release) {
	unsigned char c = (unsigned char)type;
	String tmp;
	tmp.hexify(&c,1);
	return releaseComplete("invalid-ie",tmp);
    }
    return 0;
}

// Change call state
void ISDNQ931Call::changeState(State newState)
{
    if (state() == newState)
	return;
    Debug(q931(),DebugAll,"Call(%u,%u). State '%s' --> '%s' [%p]",
	Q931_CALL_ID,stateName(state()),stateName(newState),this);
    m_state = newState;
}

ISDNQ931* ISDNQ931Call::q931()
{
    return static_cast<ISDNQ931*>(SignallingCall::controller());
}

#undef Q931_CALL_ID
#undef MSG_CHECK_SEND

/**
 * ISDNQ931CallMonitor
 */
ISDNQ931CallMonitor::ISDNQ931CallMonitor(ISDNQ931Monitor* controller, u_int32_t callRef,
	bool netInit)
    : SignallingCall(controller,true),
    m_callRef(callRef),
    m_callerCircuit(0),
    m_calledCircuit(0),
    m_eventCircuit(0),
    m_netInit(netInit),
    m_circuitChange(false),
    m_terminate(false),
    m_terminator("engine")
{
    Debug(q931(),DebugAll,"Monitor(%u) netInit=%s  [%p]",
	m_callRef,String::boolText(netInit),this);
    if (!controller) {
	Debug(DebugWarn,"Monitor(%u). No monitor controller. Terminate [%p]",
	    m_callRef,this);
	m_terminate = true;
	m_data.m_reason = "temporary-failure";
	return;
    }
}

ISDNQ931CallMonitor::~ISDNQ931CallMonitor()
{
    releaseCircuit();
    DDebug(q931(),DebugAll,"Monitor(%u). Destroyed with reason '%s' [%p]",
	m_callRef,m_data.m_reason.c_str(),this);
}

// Get an event from this monitor
SignallingEvent* ISDNQ931CallMonitor::getEvent(const Time& when)
{
    Lock mylock(this);
    // Check for last event or aborting
    if (m_lastEvent || state() == CallAbort)
	return 0;
    if (m_terminate)
	return (m_lastEvent = releaseComplete());
    // Check for incoming messages
    ISDNQ931Message* msg = static_cast<ISDNQ931Message*>(dequeue());
    // No message: check terminate
    if (!msg)
	return (m_lastEvent = getCircuitEvent(when));
    XDebug(q931(),DebugAll,
	"Monitor(%u). Dequeued message (%p): '%s' in state '%s' [%p]",
	m_callRef,msg,msg->name(),stateName(state()),this);
    switch (msg->type()) {
	case ISDNQ931Message::Setup:           m_lastEvent = processMsgSetup(msg); break;
	case ISDNQ931Message::Proceeding:
	case ISDNQ931Message::Alerting:
	case ISDNQ931Message::Connect:         m_lastEvent = processMsgResponse(msg); break;
	case ISDNQ931Message::Disconnect:
	case ISDNQ931Message::Release:
	case ISDNQ931Message::ReleaseComplete: m_lastEvent = processMsgTerminate(msg); break;
	case ISDNQ931Message::Info:            m_lastEvent = processMsgInfo(msg); break;
	case ISDNQ931Message::Notify:
	case ISDNQ931Message::Progress:
	case ISDNQ931Message::SetupAck:
	case ISDNQ931Message::ConnectAck:
	case ISDNQ931Message::Status:
	case ISDNQ931Message::StatusEnquiry:
	case ISDNQ931Message::Suspend:
	case ISDNQ931Message::Resume:
	case ISDNQ931Message::SuspendAck:
	case ISDNQ931Message::SuspendRej:
	case ISDNQ931Message::ResumeAck:
	case ISDNQ931Message::ResumeRej:
	    XDebug(q931(),DebugAll,"Monitor(%u). Ignoring '%s' message [%p]",
		m_callRef,msg->name(),this);
	    break;
	default:
	    DDebug(q931(),DebugNote,"Monitor(%u). Unknown message '%s' [%p]",
		m_callRef,msg->name(),this);
	    // Fall through to destruct the message and check timeouts
    }
    TelEngine::destruct(msg);
    if (!m_lastEvent)
	m_lastEvent = getCircuitEvent(when);
    return m_lastEvent;
}

// Set termination flag
void ISDNQ931CallMonitor::setTerminate(const char* reason)
{
    Lock mylock(this);
    if (state() == CallAbort)
	changeState(Null);
    // Check terminate & destroy flags
    if (m_terminate)
	return;
    m_terminate = true;
    if (reason)
	m_data.m_reason = reason;
    DDebug(q931(),DebugInfo,
	"Monitor(%u). Set terminate [%p]",m_callRef,this);
}

// Get caller's and called's circuit or this object
void* ISDNQ931CallMonitor::getObject(const String& name) const
{
    if (name == YSTRING("SignallingCircuitCaller"))
	return m_callerCircuit;
    if (name == YSTRING("SignallingCircuitCalled"))
	return m_calledCircuit;
    if (name == YSTRING("ISDNQ931CallMonitor"))
	return (void*)this;
    return SignallingCall::getObject(name);
}

// Process SETUP. See Q.931 3.1.14
// IE: Repeat, BearerCaps, ChannelID, Progress, NetFacility, Display,
//     Keypad, Signal, CallingNo, CallingSubAddr, CalledNo, CalledSubAddr,
//     NetTransit, Repeat, LoLayerCompat, HiLayerCompat
SignallingEvent* ISDNQ931CallMonitor::processMsgSetup(ISDNQ931Message* msg)
{
    // These message should come from the call initiator
    if (!msg->initiator())
	return 0;
    changeState(CallPresent);
    // Process IEs
    m_data.processBearerCaps(msg,false);
    m_circuitChange = false;
    if (m_data.processChannelID(msg,false) && reserveCircuit() && m_circuitChange) {
	m_circuitChange = false;
	msg->params().setParam("circuit-change",String::boolText(true));
    }
    m_data.processCalledNo(msg,false);
    m_data.processCallingNo(msg,false);
    m_data.processDisplay(msg,false);
    // Get circuits from controller. Connect the caller's circuit
    if (reserveCircuit())
	connectCircuit(true);
    // Set message parameters
    msg->params().setParam("caller",m_data.m_callerNo);
    msg->params().setParam("called",m_data.m_calledNo);
    msg->params().setParam("format",m_data.m_format);
    msg->params().setParam("callername",m_data.m_display);
    msg->params().setParam("callernumtype",m_data.m_callerType);
    msg->params().setParam("callernumplan",m_data.m_callerPlan);
    msg->params().setParam("callerpres",m_data.m_callerPres);
    msg->params().setParam("callerscreening",m_data.m_callerScreening);
    msg->params().setParam("callednumtype",m_data.m_calledType);
    msg->params().setParam("callednumplan",m_data.m_calledPlan);
    return new SignallingEvent(SignallingEvent::NewCall,msg,this);
}

// Process CALL PROCEEDING. See Q.931 3.1.2
//     IE: BearerCaps, ChannelID, Progress, Display, HiLayerCompat
// Process ALERTING. See Q.931 3.1.1
//     IE: BearerCaps, ChannelID, Progress, Display, Signal, HiLayerCompat
// Process CONNECT. See Q.931 3.1.3
//     IE: BearerCaps, ChannelID, Progress, Display, DateTime, Signal, LoLayerCompat, HiLayerCompat
// All we need is BearerCaps (for data format) and ChannelID (for channel change)
SignallingEvent* ISDNQ931CallMonitor::processMsgResponse(ISDNQ931Message* msg)
{
    SignallingEvent::Type type;
    // These responses should never come from the call initiator
    if (msg->initiator())
	return 0;
    switch (msg->type()) {
	case ISDNQ931Message::Proceeding:
	    if (state() == OutgoingProceeding)
		return 0;
	    changeState(OutgoingProceeding);
	    type = SignallingEvent::Accept;
	    break;
	case ISDNQ931Message::Alerting:
	    if (state() == CallDelivered)
		return 0;
	    changeState(CallDelivered);
	    type = SignallingEvent::Ringing;
	    break;
	case ISDNQ931Message::Connect:
	    if (state() == Active)
		return 0;
	    changeState(Active);
	    type = SignallingEvent::Answer;
	    break;
	default:
	    return 0;
    }
    m_circuitChange = false;
    if (m_data.processChannelID(msg,false) && reserveCircuit() && m_circuitChange) {
	m_circuitChange = false;
	msg->params().setParam("circuit-change",String::boolText(true));
    }
    if (m_data.processBearerCaps(msg,false) && !m_data.m_format.null())
	msg->params().setParam("format",m_data.m_format);
    connectCircuit(true);
    connectCircuit(false);
    return new SignallingEvent(type,msg,this);
}

// Process termination messages Disconnect, Release, ReleaseComplete
SignallingEvent* ISDNQ931CallMonitor::processMsgTerminate(ISDNQ931Message* msg)
{
    if (!msg)
	return 0;
    // Set terminator
    // Usually Disconnect and ReleaseComplete come from the termination initiator
    switch (msg->type()) {
	case ISDNQ931Message::Disconnect:
	case ISDNQ931Message::ReleaseComplete:
	    m_terminator = msg->initiator() ? m_data.m_callerNo : m_data.m_calledNo;
	    break;
	case ISDNQ931Message::Release:
	    m_terminator = msg->initiator() ? m_data.m_calledNo : m_data.m_callerNo;
	    break;
	default:
	    return 0;
    }
    m_data.processCause(msg,false);
    return releaseComplete();
}

// Process INFORMATION. See Q.931 3.1.6
// IE: SendComplete, Display, Keypad, Signal, CalledNo
SignallingEvent* ISDNQ931CallMonitor::processMsgInfo(ISDNQ931Message* msg)
{
    // Check complete
    bool complete = (0 != msg->getIE(ISDNQ931IE::SendComplete));
    if (complete)
	msg->params().addParam("complete",String::boolText(true));
    // Display
    m_data.processDisplay(msg,false);
    // Try to get digits
    const char* tone = msg->getIEValue(ISDNQ931IE::CalledNo,"number");
    if (!tone)
	tone = msg->getIEValue(ISDNQ931IE::Keypad,"keypad");
    if (tone)
	msg->params().addParam("tone",tone);
    msg->params().setParam("fromcaller",String::boolText(msg->initiator()));
    return new SignallingEvent(SignallingEvent::Info,msg,this);
}

// Release monitor
SignallingEvent* ISDNQ931CallMonitor::releaseComplete(const char* reason)
{
    Lock mylock(this);
    if (state() == Null)
	return 0;
    if (reason)
	m_data.m_reason = reason;
    DDebug(q931(),DebugInfo,
	"Monitor(%u). Monitor release in state '%s'. Reason: '%s' [%p]",
	m_callRef,stateName(state()),m_data.m_reason.c_str(),this);
    // Cleanup
    releaseCircuit();
    changeState(Null);
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::ReleaseComplete,
	true,m_callRef,2);
    msg->params().addParam("reason",m_data.m_reason);
    msg->params().addParam("terminator",m_terminator);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Release,msg,this);
    TelEngine::destruct(msg);
    deref();
    return event;
}

// Get an event from the reserved circuit
SignallingEvent* ISDNQ931CallMonitor::getCircuitEvent(const Time& when)
{
    bool fromCaller = true;
    // Select circuit to get event from
    if (m_eventCircuit)
	if (m_eventCircuit == m_callerCircuit) {
	    m_eventCircuit = m_calledCircuit;
	    fromCaller = false;
	}
	else
	    m_eventCircuit = m_callerCircuit;
    else
	m_eventCircuit = m_callerCircuit;
    SignallingCircuitEvent* ev = m_eventCircuit ? m_eventCircuit->getEvent(when) : 0;
    if (!ev)
	return 0;
    SignallingEvent* event = 0;
    switch (ev->type()) {
	case SignallingCircuitEvent::Dtmf: {
	    const char* tone = ev->getValue(YSTRING("tone"));
	    if (!(tone && *tone))
		break;
	    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Info,
		!fromCaller,m_callRef,2);
	    msg->params().addParam("tone",tone);
	    msg->params().addParam("inband",String::boolText(true));
	    msg->params().addParam("fromcaller",String::boolText(fromCaller));
	    event = new SignallingEvent(SignallingEvent::Info,msg,this);
	    TelEngine::destruct(msg);
	    }
	    break;
	default: ;
    }
    delete ev;
    return event;
}

// Reserve circuit for caller and called
// Reserve both circuits or none
bool ISDNQ931CallMonitor::reserveCircuit()
{
    m_circuitChange = false;
    if (!m_data.m_channelByNumber)
	return false;
    // Check the received circuit if any
    unsigned int code = (unsigned int)m_data.m_channels.toInteger(-1);
    if (m_data.m_channels.null())
	return 0 != m_callerCircuit;
    else if (m_callerCircuit && (code == m_callerCircuit->code()))
	return true;
    // Reserve the circuit
    m_circuitChange = true;
    releaseCircuit();
    if (q931()->reserveCircuit(code,m_netInit,&m_callerCircuit,&m_calledCircuit))
	return true;
    DDebug(q931(),DebugNote,
	"Monitor(%u). Can't reserve circuit [%p]",m_callRef,this);
    return false;
}

// Release both reserved circuits
void ISDNQ931CallMonitor::releaseCircuit()
{
    if (m_callerCircuit) {
	q931()->releaseCircuit(m_callerCircuit);
	TelEngine::destruct(m_callerCircuit);
    }
    if (m_calledCircuit) {
	q931()->releaseCircuit(m_calledCircuit);
	TelEngine::destruct(m_calledCircuit);
    }
}

// Connect a reserved circuit
bool ISDNQ931CallMonitor::connectCircuit(bool caller)
{
    if (caller) {
	if (m_callerCircuit && m_callerCircuit->connect(m_data.m_format))
	    return true;
    }
    else
	if (m_calledCircuit && m_calledCircuit->connect(m_data.m_format))
	    return true;
    DDebug(q931(),DebugNote,
	"Monitor(%u). Can't connect circuit for calle%s [%p]",
	m_callRef,caller?"r":"d",this);
    return false;
}

// Change monitor state
void ISDNQ931CallMonitor::changeState(State newState)
{
    if (state() == newState)
	return;
    DDebug(q931(),DebugInfo,
	"Monitor(%u). Changing state from '%s' to '%s' [%p]",
	m_callRef,stateName(state()),stateName(newState),this);
    m_state = newState;
}

ISDNQ931Monitor* ISDNQ931CallMonitor::q931()
{
    return static_cast<ISDNQ931Monitor*>(SignallingCall::controller());
}

/**
 * ISDNQ931ParserData
 */
ISDNQ931ParserData::ISDNQ931ParserData(const NamedList& params, DebugEnabler* dbg)
    : m_dbg(dbg),
    m_maxMsgLen(0),
    m_flags(0),
    m_flagsOrig(0)
{
    m_allowSegment = params.getBoolValue(YSTRING("allowsegmentation"),false);
    m_maxSegments = params.getIntValue(YSTRING("maxsegments"),8);
    m_maxDisplay = params.getIntValue(YSTRING("max-display"),34);
    if (m_maxDisplay != 34 && m_maxDisplay != 82)
	m_maxDisplay = 34;
    m_extendedDebug = params.getBoolValue(YSTRING("extended-debug"),false);
    // Set flags
    String flags = params.getValue(YSTRING("switchtype"));
    SignallingUtils::encodeFlags(0,m_flagsOrig,flags,ISDNQ931::s_swType);
    SignallingUtils::encodeFlags(0,m_flagsOrig,flags,ISDNQ931::s_flags);
    m_flags = m_flagsOrig;
}

/**
 * ISDNQ931
 */
const TokenDict ISDNQ931::s_flags[] = {
	{"sendnonisdnsource",    SendNonIsdnSource},
	{"ignorenonisdndest",    IgnoreNonIsdnDest},
	{"forcepresnetprov",     ForcePresNetProv},
	{"translate31kaudio",    Translate31kAudio},
	{"urditransfercapsonly", URDITransferCapsOnly},
	{"nolayer1caps",         NoLayer1Caps},
	{"ignorenonlockedie",    IgnoreNonLockedIE},
	{"nodisplay",            NoDisplayIE},
	{"nodisplaycharset",     NoDisplayCharset},
	{"forcesendcomplete",    ForceSendComplete},
	{"noactiveonconnect",    NoActiveOnConnect},
	{"checknotifyind",       CheckNotifyInd},
	{"channelexclusive",     ChannelExclusive},
	{0,0},
	};

const TokenDict ISDNQ931::s_swType[] = {
	{"euro-isdn-e1",   EuroIsdnE1},
	{"euro-isdn-t1",   EuroIsdnT1},
	{"national-isdn",  NationalIsdn},
	{"dms100",         Dms100},
	{"lucent5e",       Lucent5e},
	{"att4ess",        Att4ess},
	{"qsig",           QSIG},
	{"unknown",        Unknown},
	{0,0}
	};

ISDNQ931::ISDNQ931(const NamedList& params, const char* name)
    : SignallingComponent(name,&params,"isdn-q931"),
      SignallingCallControl(params,"isdn."),
      SignallingDumpable(SignallingDumper::Q931),
      ISDNLayer3(name),
    m_q921(0),
    m_q921Up(false),
    m_networkHint(true),
    m_primaryRate(true),
    m_transferModeCircuit(true),
    m_callRef(1),
    m_callRefLen(2),
    m_callRefMask(0),
    m_parserData(params),
    m_l2DownTimer(0),
    m_recvSgmTimer(0),
    m_syncCicTimer(0),
    m_syncCicCounter(2),
    m_callDiscTimer(0),
    m_callRelTimer(0),
    m_callConTimer(0),
    m_restartCic(0),
    m_lastRestart(0),
    m_syncGroupTimer(0),
    m_segmented(0),
    m_remaining(0),
    m_printMsg(true),
    m_extendedDebug(false),
    m_flagQ921Down(false),
    m_flagQ921Invalid(false)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"ISDNQ931::ISDNQ931(%p,'%s') [%p]%s",
	    &params,name,this,tmp.c_str());
    }
#endif
    m_parserData.m_dbg = this;
    m_networkHint = params.getBoolValue(YSTRING("network"),m_networkHint);
    m_data.m_bri = !(m_primaryRate = params.getBoolValue(YSTRING("primary"),m_primaryRate));
    m_callRefLen = params.getIntValue(YSTRING("callreflen"),m_primaryRate ? 2 : 1);
    if (m_callRefLen < 1 || m_callRefLen > 4)
	m_callRefLen = 2;
    // Set mask. Bit 7 of the first byte of the message header it's used for initiator flag
    m_callRefMask = 0x7fffffff >> (8 * (4 - m_callRefLen));
    // Timers
    m_l2DownTimer.interval(params,"t309",60000,90000,false);
    m_recvSgmTimer.interval(params,"t314",3000,4000,false);
    m_syncCicTimer.interval(params,"t316",4000,5000,false);
    m_syncGroupTimer.interval(params,"channelsync",60,300,true,true);
    m_callDiscTimer.interval(params,"t305",0,5000,false);
    m_callRelTimer.interval(params,"t308",0,5000,false);
    m_callConTimer.interval(params,"t313",0,5000,false);
    m_cpeNumber = params.getValue(YSTRING("number"));
    m_numPlan = params.getValue(YSTRING("numplan"));
    if (0xffff == lookup(m_numPlan,Q931Parser::s_dict_numPlan,0xffff))
	m_numPlan = "unknown";
    m_numType = params.getValue(YSTRING("numtype"));
    if (0xffff == lookup(m_numType,Q931Parser::s_dict_typeOfNumber,0xffff))
	m_numType = "unknown";
    m_numPresentation = params.getValue(YSTRING("presentation"));
    if (0xffff == lookup(m_numPresentation,Q931Parser::s_dict_presentation,0xffff))
	m_numPresentation = "allowed";
    m_numScreening = params.getValue(YSTRING("screening"));
    if (0xffff == lookup(m_numScreening,Q931Parser::s_dict_screening,0xffff))
	m_numScreening = "user-provided";
    m_format = params.getValue(YSTRING("format"));
    if (0xffff == lookup(m_format,Q931Parser::s_dict_bearerProto1,0xffff))
	m_format = "alaw";
    // Debug
    setDebug(params.getBoolValue(YSTRING("print-messages"),false),
	params.getBoolValue(YSTRING("extended-debug"),false));
    if (debugAt(DebugInfo)) {
	String s(network() ? "NET" : "CPE");
#ifdef DEBUG
	s << " type=" << lookup(m_parserData.m_flags,s_swType,"Custom");
	String t;
	for (const TokenDict* p = s_flags; p->token; p++)
	    if (m_parserData.flag(p->value))
		t.append(p->token,",");
	if (!t.null())
	    s << " (" << t << ")";
	s << " pri=" << String::boolText(m_primaryRate);
	s << " format=" << m_format;
	s << " callref-len=" << (unsigned int)m_callRefLen;
	s << " plan/type/pres/screen=" << m_numPlan << "/" <<
	    m_numType << "/" << m_numPresentation << "/" << m_numScreening;
	s << " strategy=" << lookup(strategy(),SignallingCircuitGroup::s_strategy);
	s << " channelsync/l2Down/recvSgm/syncCic=" <<
	    (unsigned int)m_syncGroupTimer.interval() << "/" <<
	    (unsigned int)m_l2DownTimer.interval() << "/" <<
	    (unsigned int)m_recvSgmTimer.interval() << "/" <<
	    (unsigned int)m_syncCicTimer.interval();
	s << " segmentation=" << String::boolText(m_parserData.m_allowSegment);
	s << " max-segments=" << (unsigned int)m_parserData.m_maxSegments;
#else
	s << " type=" << params.getValue(YSTRING("switchtype"));
	s << " pri=" << String::boolText(m_primaryRate);
	s << " format=" << m_format;
	s << " channelsync=" << String::boolText(0 != m_syncGroupTimer.interval());
#endif
	Debug(this,DebugInfo,"ISDN Call Controller %s [%p]",s.c_str(),this);
    }
    setDumper(params.getValue(YSTRING("layer3dump")));
    m_syncGroupTimer.start();
}

ISDNQ931::~ISDNQ931()
{
    if (m_calls.count()) {
	cleanup();
	m_calls.clear();
    }
    TelEngine::destruct(attach((ISDNLayer2*)0));
    TelEngine::destruct(SignallingCallControl::attach(0));
    DDebug(this,DebugAll,"ISDN Call Controller destroyed [%p]",this);
}

// Initialize Q.931 and attach a layer 2
bool ISDNQ931::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"ISDNQ931::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue(YSTRING("debuglevel_q931"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	setDebug(config->getBoolValue(YSTRING("print-messages"),false),
	    config->getBoolValue(YSTRING("extended-debug"),false));
    }
    if (config && !layer2()) {
	const String* name = config->getParam(YSTRING("sig"));
	if (!name)
	    name = config;
	if (!TelEngine::null(name)) {
	    NamedPointer* ptr = YOBJECT(NamedPointer,name);
	    NamedList* linkConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
	    NamedList params(*name + "/Q921");
	    params.addParam("basename",*name);
	    params.addParam("primary",String::boolText(primaryRate()));
	    params.addParam("network",String::boolText(network()));
	    if (linkConfig)
		params.copyParams(*linkConfig);
	    else {
		if (config->hasSubParams(*name + "."))
		    params.copySubParams(*config,*name + ".");
		else {
		    params.addParam("local-config","true");
		    params.copyParams(*config);
		}
		linkConfig = &params;
	    }
	    params.clearParam(YSTRING("debugname"));
	    ISDNLayer2* l2 = YSIGCREATE(ISDNLayer2,&params);
	    if (!l2) {
		Debug(this,DebugWarn,"Could not create ISDN Layer 2 '%s' [%p]",name->c_str(),this);
		return false;
	    }
	    attach(l2);
	    if (!l2->initialize(linkConfig))
		TelEngine::destruct(attach((ISDNLayer2*)0));
	}
    }
    return 0 != layer2();
}

const char* ISDNQ931::statusName() const
{
    if (exiting())
	return "Exiting";
    if (!m_q921)
	return "Layer 2 missing";
    if (!m_q921Up)
	return "Layer 2 down";
    return "Operational";
}

// Check if layer 2 may be up
bool ISDNQ931::q921Up() const
{
    if (!m_q921)
	return false;
    if (m_q921Up)
	return true;
    // Assume BRI NET is always up
    return !primaryRate() && network();
}

// Send a message to layer 2
bool ISDNQ931::sendMessage(ISDNQ931Message* msg, u_int8_t tei, String* reason)
{
    if (!msg) {
	if (reason)
	    *reason = "wrong-message";
	return false;
    }
    Lock lock(l3Mutex());
    if (!q921Up()) {
	if (!m_flagQ921Invalid)
	    Debug(this,DebugNote,
		"Refusing to send message. Layer 2 is missing or down");
	m_flagQ921Invalid = true;
	TelEngine::destruct(msg);
	if (reason)
	    *reason = "net-out-of-order";
	return false;
    }
    m_flagQ921Invalid = false;
    // Print message after running encoder to view dumped data
    ObjList segments;
    u_int8_t count = msg->encode(m_parserData,segments);
    if (debugAt(DebugInfo) && m_printMsg) {
	String tmp;
	msg->toString(tmp,m_extendedDebug);
	Debug(this,DebugInfo,"Sending message (%p)%s",msg,tmp.c_str());
    }
    TelEngine::destruct(msg);
    ObjList* obj = segments.skipNull();
    if (!(count && obj)) {
	Debug(this,DebugNote,"Failed to send message (%p). Parser failure",msg);
	if (reason)
	    *reason = "wrong-message";
	return false;
    }
    if (count != 1)
	DDebug(this,DebugNote,"Message (%p) was segmented in %u parts",msg,count);
    for (; obj; obj = obj->skipNext()) {
	DataBlock* buffer = static_cast<DataBlock*>(obj->get());
	dump(*buffer,true);
	if (!m_q921->sendData(*buffer,tei,true)) {
	    if (reason)
		*reason = "net-out-of-order";
	    return false;
	}
    }
    return true;
}

// Data link up notification from layer 2
// Notify calls
void ISDNQ931::multipleFrameEstablished(u_int8_t tei, bool confirmation, bool timeout, ISDNLayer2* layer2)
{
    l3Mutex().lock();
    bool q921Tmp = m_q921Up;
    m_q921Up = true;
    if (m_q921Up != q921Tmp) {
	NamedList p("");
	p.addParam("type","isdn-q921");
	p.addParam("operational",String::boolText(m_q921Up));
	p.addParam("from",m_q921->toString());
	engine()->notify(this,p);
    }
    DDebug(this,DebugNote,"'Established' %s TEI %u",
	confirmation ? "confirmation" :"indication",tei);
    endReceiveSegment("Data link is up");
    m_l2DownTimer.stop();
    m_flagQ921Down = false;
    l3Mutex().unlock();
    if (confirmation)
	return;
    // Notify calls
    Lock lock(this);
    for (ObjList* obj = m_calls.skipNull(); obj; obj = obj->skipNext())
	(static_cast<ISDNQ931Call*>(obj->get()))->dataLinkState(true);
}

// Data link down notification from layer 2
// Notify calls
void ISDNQ931::multipleFrameReleased(u_int8_t tei, bool confirmation, bool timeout, ISDNLayer2* layer2)
{
    Lock lockLayer(l3Mutex());
    bool q921Tmp = m_q921Up;
    m_q921Up = false;
    if (m_q921Up != q921Tmp) {
	NamedList p("");
	p.addParam("type","isdn-q921");
	p.addParam("operational",String::boolText(m_q921Up));
	p.addParam("from",m_q921->toString());
	engine()->notify(this,p);
    }
    DDebug(this,DebugNote,"'Released' %s TEI %u. Timeout: %s",
	confirmation ? "confirmation" :"indication",tei,String::boolText(timeout));
    endReceiveSegment("Data link is down");
    // Re-establish if layer 2 doesn't have an automatically re-establish procedure
    if (m_q921 && !m_q921->autoRestart()) {
	DDebug(this,DebugNote,"Re-establish layer 2.");
	m_q921->multipleFrame(tei,true,false);
    }
    if (confirmation)
	return;
    if (primaryRate() && !m_l2DownTimer.started()) {
	XDebug(this,DebugAll,"Starting T309 (layer 2 down)");
	m_l2DownTimer.start();
    }
    lockLayer.drop();
    // Notify calls
    Lock lockCalls(this);
    for (ObjList* obj = m_calls.skipNull(); obj; obj = obj->skipNext())
	(static_cast<ISDNQ931Call*>(obj->get()))->dataLinkState(false);
}

// Receive and parse data from layer 2
// Process the message
void ISDNQ931::receiveData(const DataBlock& data, u_int8_t tei, ISDNLayer2* layer2)
{
    XDebug(this,DebugAll,"Received data. Length: %u, TEI: %u",data.length(),tei);
    Lock lock(l3Mutex());
    ISDNQ931Message* msg = getMsg(data);
    if (!msg)
	return;
    // Dummy call reference
    if (msg->dummyCallRef()) {
	sendStatus("service-not-implemented",0,tei);
	TelEngine::destruct(msg);
	return;
    }
    // Global call reference or a message that should have a dummy call reference
    if (!msg->callRef() || msg->type() == ISDNQ931Message::Restart ||
	msg->type() == ISDNQ931Message::RestartAck) {
	processGlobalMsg(msg,tei);
	TelEngine::destruct(msg);
	return;
    }
    bool doMore = true;
    // This is an incoming message:
    //   if initiator is true, the message is for an incoming call
    ISDNQ931Call* call = findCall(msg->callRef(),!msg->initiator(),tei);
    if (call && (call->callTei() == 127) && (call->callRef() == msg->callRef())) {
	// Call was or still is Point-to-Multipoint
	int i;
	switch (msg->type()) {
	    case ISDNQ931Message::Disconnect:
	    case ISDNQ931Message::ReleaseComplete:
		if ((tei < 127) && call->m_broadcast[tei])
		    call->m_broadcast[tei] = false;
		else
		    doMore = false;
		if (call->m_retransSetupTimer.timeout()) {
		    call->m_retransSetupTimer.stop();
		    for (i = 0; i < 127; i++) {
			if (call->m_broadcast[i]) {
			    doMore = false;
			    break;
			}
		    }
		}
		if ((msg->type() != ISDNQ931Message::ReleaseComplete) && !doMore)
		    sendRelease(false,msg->callRefLen(),msg->callRef(),
			tei,!msg->initiator());
		break;
	    case ISDNQ931Message::Connect:
		if (tei >= 127)
		    break;
		call->m_tei = tei;
		call->m_broadcast[tei] = false;
		// All other pending calls are to be aborted
		for (i = 0; i < 127; i++) {
		    if (call->m_broadcast[i]) {
			sendRelease(true,msg->callRefLen(),msg->callRef(),
			    i,!msg->initiator(),"answered");
			call->m_broadcast[i] = false;
			break;
		    }
		}
		break;
	    default:
		if (tei < 127)
		    call->m_broadcast[tei] = true;
	}
    }
    while (doMore) {
	if (call) {
	    if (msg->type() != ISDNQ931Message::Setup &&
		(call->callTei() == 127 || call->callTei() == tei)) {
		call->enqueue(msg);
		msg = 0;
	    }
	    else if (msg->type() != ISDNQ931Message::ReleaseComplete) {
		sendRelease((msg->type() != ISDNQ931Message::Release),
		    msg->callRefLen(),msg->callRef(),tei,
		    !msg->initiator(),"invalid-callref");
	    }
	    break;
	}
	// Check if it is a new incoming call
	if (msg->initiator() && msg->type() == ISDNQ931Message::Setup) {
	    if (!primaryRate() && m_cpeNumber && !network()) {
		// We are a BRI CPE with a number - check the called party field
		ISDNQ931IE* ie = msg->getIE(ISDNQ931IE::CalledNo);
		if (ie) {
		    const String* number = ie->getParam(YSTRING("number"));
		    if (number && !number->startsWith(m_cpeNumber)) {
			DDebug(this,DebugInfo,"Setup was for '%s', not us.",number->c_str());
			break;
		    }
		}
	    }
	    // Accept new calls only if no channel is restarting and not exiting
	    String reason;
	    if (acceptNewCall(false,reason)) {
		call = new ISDNQ931Call(this,false,msg->callRef(),msg->callRefLen(),tei);
		m_calls.append(call);
		call->enqueue(msg);
		msg = 0;
		call = 0;
	    }
	    else
		sendRelease(false,msg->callRefLen(),msg->callRef(),tei,
		    !msg->initiator(),reason);
	    break;
	}
	processInvalidMsg(msg,tei);
	break;
    }
    TelEngine::destruct(call);
    TelEngine::destruct(msg);
}

// Attach layer 2
// Update some data from the attached object
ISDNLayer2* ISDNQ931::attach(ISDNLayer2* q921)
{
    Lock lock(l3Mutex());
    if (m_q921 == q921)
	return 0;
    cleanup(q921 ? "layer 2 attach" : "layer 2 detach");
    ISDNLayer2* tmp = m_q921;
    m_q921 = q921;
    if (m_q921) {
	ISDNQ921* q = YOBJECT(ISDNQ921,m_q921);
	// Adjust timers from the new lower layer
	// Add 1000 ms to minimum value to allow the lower layer to re-establish
        //   the data link before we make a retransmission
	if (q) {
	    m_primaryRate = true;
	    m_data.m_bri = false;
	    u_int64_t min = q->dataTimeout();
	    if (m_callDiscTimer.interval() <= min)
		m_callDiscTimer.interval(min + 1000);
	    if (m_callRelTimer.interval() <= min)
		m_callRelTimer.interval(min + 1000);
	    if (m_callConTimer.interval() <= min)
		m_callConTimer.interval(min + 1000);
	    if (m_l2DownTimer.interval() <= min)
		m_l2DownTimer.interval(min + 1000);
	    if (m_syncCicTimer.interval() <= min)
		m_syncCicTimer.interval(min + 1000);
	    // Adjust some parser flags
	    if (m_parserData.m_flagsOrig == EuroIsdnE1 && !q->network())
		m_parserData.m_flags |= NoDisplayIE;
	    if (m_parserData.m_flagsOrig != QSIG && !q->network())
		m_parserData.m_flags |= NoActiveOnConnect;
	}
	else if (YOBJECT(ISDNQ921Management,m_q921)) {
	    m_primaryRate = false;
	    m_data.m_bri = true;
	    m_callRefLen = 1;
	    m_callRefMask = 0x7f;
	    m_callRef &= m_callRefMask;
	}
	// Adjust parser data message length limit
	m_parserData.m_maxMsgLen = m_q921->maxUserData();
    }
    else {
	// Reset parser data if no layer 2
	m_parserData.m_maxMsgLen = 0;
	m_parserData.m_flags = m_parserData.m_flagsOrig;
    }
    lock.drop();
    if (tmp) {
	if (tmp->layer3() == this) {
	    Debug(this,DebugAll,"Detaching L2 (%p,'%s') [%p]",
		tmp,tmp->toString().safe(),this);
	    tmp->attach(0);
	}
	else {
	    Debug(this,DebugNote,"Layer 2 (%p,'%s') was not attached to us [%p]",
		tmp,tmp->toString().safe(),this);
	    tmp = 0;
	}
    }
    if (!q921)
	return tmp;
    Debug(this,DebugAll,"Attached L2 '%s' (%p,'%s') [%p]",
	(q921->network() ? "NET" : "CPE"),
	q921,q921->toString().safe(),this);
    insert(q921);
    q921->attach(this);
    return tmp;
}

// Make an outgoing call from a given message
SignallingCall* ISDNQ931::call(SignallingMessage* msg, String& reason)
{
    if (!msg) {
	reason = "invalid-parameter";
	return 0;
    }
    Lock lock(l3Mutex());
    if (!acceptNewCall(true,reason)) {
	TelEngine::destruct(msg);
	return 0;
    }
    ISDNQ931Call* call = new ISDNQ931Call(this,true,m_callRef,m_callRefLen);
    if (!call->circuit()) {
	reason = "congestion";
	TelEngine::destruct(call);
	return 0;
    }
    call->ref();
    // Adjust m_callRef. Avoid to use 0
    m_callRef = (m_callRef + 1) & m_callRefMask;
    if (!m_callRef)
	m_callRef = 1;
    m_calls.append(call);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::NewCall,msg,call);
    TelEngine::destruct(msg);
    call->sendEvent(event);
    return call;
}

// Reset data. Terminate calls and pending operations
void ISDNQ931::cleanup(const char* reason)
{
    DDebug(this,DebugAll,"Cleanup. Reason: '%s'",reason);
    terminateCalls(0,reason);
    endReceiveSegment(reason);
    endRestart(false,0);
}

// Set the interval for a given timer
void ISDNQ931::setInterval(SignallingTimer& timer, int id)
{
    switch (id) {
	case 305:
	    timer.interval(m_callDiscTimer.interval());
	    break;
	case 308:
	    timer.interval(m_callRelTimer.interval());
	    break;
	case 313:
	    timer.interval(m_callConTimer.interval());
	    break;
	default:
	    Debug(this,DebugWarn,"Unknown interval %d",id);
    }
}

// Check timeouts for segmented messages, layer 2 down state, restart circuits
void ISDNQ931::timerTick(const Time& when)
{
    Lock mylock(l3Mutex(),SignallingEngine::maxLockWait());
    if (!mylock.locked())
	return;
    // Check segmented message
    if (m_recvSgmTimer.timeout(when.msec()))
	endReceiveSegment("timeout");
    // Terminate all calls if T309 (layer 2 down) timed out
    if (m_l2DownTimer.timeout(when.msec())) {
	m_l2DownTimer.stop();
	if (!m_flagQ921Down)
	    Debug(this,DebugWarn,"Layer 2 was down for " FMT64 " ms",m_l2DownTimer.interval());
	m_flagQ921Down = true;
	cleanup("dest-out-of-order");
    }
    // Restart circuits
    if (!m_syncGroupTimer.interval())
	return;
    if (m_syncGroupTimer.started()) {
	if (m_syncGroupTimer.timeout(when.msec())) {
	    m_syncGroupTimer.stop();
	    sendRestart(when.msec(),false);
	}
	return;
    }
    if (!m_syncCicTimer.started()) {
	m_lastRestart = 0;
	m_syncGroupTimer.start(when.msec());
	return;
    }
    // Terminate restart procedure if timeout
    if (m_syncCicTimer.timeout(when.msec())) {
	m_syncCicTimer.stop();
	m_syncCicCounter.inc();
	if (m_syncCicCounter.full())
	    endRestart(true,when.msec(),true);
	else
	    sendRestart(when.msec(),true);
    }
}

// Find a call by call reference and direction
ISDNQ931Call* ISDNQ931::findCall(u_int32_t callRef, bool outgoing, u_int8_t tei)
{
    Lock lock(this);
    ObjList* obj = m_calls.skipNull();
    for (; obj; obj = obj->skipNext()) {
	ISDNQ931Call* call = static_cast<ISDNQ931Call*>(obj->get());
	if (callRef == call->callRef() && outgoing == call->outgoing()) {
	    if (!primaryRate() && (call->callTei() != tei) && (call->callTei() != 127))
		return 0;
	    return (call->ref() ? call : 0);
	}
    }
    return 0;
}

// Find a call by reserved circuit
ISDNQ931Call* ISDNQ931::findCall(unsigned int circuit)
{
    Lock lock(this);
    ObjList* obj = m_calls.skipNull();
    for (; obj; obj = obj->skipNext()) {
	ISDNQ931Call* call = static_cast<ISDNQ931Call*>(obj->get());
	if (!call->circuit() || call->circuit()->code() != circuit)
	    continue;
	return (call->ref() ? call : 0);
    }
    return 0;
}

// Terminate a call or all of them
void ISDNQ931::terminateCalls(ObjList* list, const char* reason)
{
    Lock lock(this);
    // Terminate all calls if no list
    if (!list) {
	ObjList* obj = m_calls.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    ISDNQ931Call* call = static_cast<ISDNQ931Call*>(obj->get());
	    call->setTerminate(true,reason);
	}
	return;
    }
    // Terminate calls from list
    for (ObjList* obj = list->skipNull(); obj; obj = obj->skipNext()) {
	int circuit = (static_cast<String*>(obj->get()))->toInteger(-1);
	if (circuit == -1)
	    continue;
	ISDNQ931Call* call = findCall(circuit);
	if (call) {
	    call->setTerminate(true,reason);
	    TelEngine::destruct(call);
	    continue;
	}
	// No call for this circuit. Release the circuit
	releaseCircuit(circuit);
    }
}

// Check if new calls are acceptable
bool ISDNQ931::acceptNewCall(bool outgoing, String& reason)
{
    if (exiting() || !q921Up()) {
	Debug(this,DebugInfo,"Denying %s call request, reason: %s.",
	    outgoing ? "outgoing" : "incoming",
	    exiting() ? "exiting" : "link down");
	reason = "net-out-of-order";
	return false;
    }
    return true;
}

// Helper function called in ISDNQ931::receive()
static inline ISDNQ931Message* dropSegMsg(ISDNQ931* q931, ISDNQ931Message* msg,
	const char* reason)
{
    if (reason)
	Debug(q931,DebugNote,"Dropping message segment (%p): '%s'. %s",
	    msg,msg->name(),reason);
    TelEngine::destruct(msg);
    return 0;
}

// Parse received data
// Create a message from it. Validate it. Process segmented messages
ISDNQ931Message* ISDNQ931::getMsg(const DataBlock& data)
{
    Lock lock(l3Mutex());
    DataBlock segData;
    ISDNQ931Message* msg = ISDNQ931Message::parse(m_parserData,data,&segData);
    if (!msg)
	return 0;
    // Print received message
    if (debugAt(DebugInfo) && m_printMsg) {
	String tmp;
	msg->toString(tmp,m_extendedDebug);
	Debug(this,DebugInfo,"Received message (%p)%s",msg,tmp.c_str());
    }
    dump(data,false);
    // Not a segment
    if (msg->type() != ISDNQ931Message::Segment) {
	// We was waiting for a segment: Drop waiting
	if (m_segmented)
	    endReceiveSegment("Received non-segmented message");
	return msg;
    }
    // This is a message segment. Start timer. Get it's parameters
    m_recvSgmTimer.start();
    bool first;
    u_int8_t remaining = 0xff, type = 0xff;
    // Get parameters
    bool valid = false;
    while (true) {
	ISDNQ931IE* ie = msg->getIE(ISDNQ931IE::Segmented);
	if (!ie)
	    break;
	NamedString* ns = ie->getParam(YSTRING("first"));
	if (!ns)
	    break;
	first = ns->toBoolean();
	remaining = (u_int8_t)ie->getIntValue(YSTRING("remaining"),0xff);
	type = (u_int8_t)ie->getIntValue(YSTRING("message"),0xff);
	valid = true;
	break;
    }
    if (!valid || type == 0xff || remaining == 0xff)
	return dropSegMsg(this,msg,"Invalid or missing segmented IE");
    // Check segmented message type
    if (!ISDNQ931Message::typeName(type))
	return dropSegMsg(this,msg,"Unknown segmented message type");
    // SEGMENT message can't be segmented
    if (type == ISDNQ931Message::Segment)
	return dropSegMsg(this,msg,"Segmented message can't be a segment");
    // Check if this is a new one
    if (!m_segmented) {
	// Should be the first segment with a valid call reference
	if (!first || !msg->callRef())
	     return dropSegMsg(this,msg,"Invalid message segment");
	// Create message
	XDebug(this,DebugAll,"Start receiving message segments");
	m_segmented = new ISDNQ931Message((ISDNQ931Message::Type)type,
	    msg->initiator(),msg->callRef(),msg->callRefLen());
	TelEngine::destruct(msg);
	// Put the message header in the buffer
	u_int8_t header[7];
	m_segmentData.assign(header,fillHeader(header,m_segmented,this));
	m_remaining = remaining;
	m_segmentData += segData;
	// Strange case: Segmented message in 1 segment
	if (!remaining)
	    return endReceiveSegment();
	return 0;
    }
    // Sould be a segment for the message we already have
    // Check call identification
    if (m_segmented->initiator() != msg->initiator() ||
	m_segmented->callRef() != msg->callRef()) {
	dropSegMsg(this,msg,"Invalid call identification");
	return endReceiveSegment("Segment with invalid call identification");
    }
    // Check segment parameters
    if (first || m_remaining <= remaining || m_remaining - remaining != 1) {
	dropSegMsg(this,msg,"Invalid Segmented IE parameters");
	return endReceiveSegment("Segment with invalid parameters");
    }
    TelEngine::destruct(msg);
    // Update data
    m_remaining--;
    m_segmentData += segData;
    // End receiving ?
    if (!m_remaining)
	return endReceiveSegment();
    return 0;
}

// Terminate receiving segmented message
ISDNQ931Message* ISDNQ931::endReceiveSegment(const char* reason)
{
    Lock lock(l3Mutex());
    m_recvSgmTimer.stop();
    if (!m_segmented)
	return 0;
    // Clear some data
    TelEngine::destruct(m_segmented);
    m_remaining = 0;
    // Drop ?
    if (reason) {
	Debug(this,DebugNote,"Drop receiving message segment. %s",reason);
	m_segmentData.clear();
	return 0;
    }
    // Received all message: reassembly
    XDebug(this,DebugNote,"Reassambly message segment(s)");
    ISDNQ931Message* msg = ISDNQ931Message::parse(m_parserData,m_segmentData,0);
    m_segmentData.clear();
    if (msg && debugAt(DebugInfo) && m_printMsg) {
	String tmp;
	msg->toString(tmp,m_extendedDebug);
	Debug(this,DebugInfo,"Completed segmented message. (%p)%s",msg,tmp.c_str());
    }
    return msg;
}

// Process messages with global call reference and messages that should have it
void ISDNQ931::processGlobalMsg(ISDNQ931Message* msg, u_int8_t tei)
{
    if (!msg)
	return;
    switch (msg->type()) {
	case ISDNQ931Message::Restart:
	case ISDNQ931Message::RestartAck:
	    // These messages must have a global call reference
	    if (msg->callRef()) {
#ifndef Q931_ACCEPT_RESTART
		Debug(this,DebugNote,
		    "Dropping (%p): '%s' without global call reference",
		    msg,msg->name());
		sendStatus("invalid-message",m_callRefLen,tei);
		return;
#else
		DDebug(this,DebugNote,"(%p): '%s' without global call reference",
		    msg,msg->name());
#endif
	    }
	    if (msg->type() == ISDNQ931Message::Restart) {
		processMsgRestart(msg,tei);
		return;
	    }
	    if (m_restartCic) {
		String tmp = msg->getIEValue(ISDNQ931IE::ChannelID,"channels");
		if (m_restartCic->code() == (unsigned int)tmp.toInteger(-1))
		    endRestart(true,0);
		else
		    Debug(this,DebugWarn,
			"'%s' with invalid circuit(s) '%s'. We've requested '%u'",
			msg->name(),tmp.c_str(),m_restartCic->code());
		}
	    else
		sendStatus("wrong-state-message",m_callRefLen,tei);
	    return;
	case ISDNQ931Message::Status:
	    break;
	default:
	    Debug(this,DebugNote,"Dropping (%p): '%s' with global call reference",
		msg,msg->name());
	    sendStatus("invalid-callref",m_callRefLen,tei);
	    return;
    }
    // Message is a STATUS one
    DDebug(this,m_restartCic ? DebugWarn : DebugInfo,
	"'%s' with global call reference. State: '%s'. Cause: '%s'",
	msg->name(),
	msg->getIEValue(ISDNQ931IE::CallState,"state","Unknown/missing"),
	msg->getIEValue(ISDNQ931IE::Cause,0,"Unknown/missing"));
}

// Process restart requests
// See Q.931 5.5
void ISDNQ931::processMsgRestart(ISDNQ931Message* msg, u_int8_t tei)
{
    m_data.processRestart(msg,false);
    m_data.processChannelID(msg,false);
    m_data.m_reason = "";
    ObjList* list = m_data.m_channels.split(',',false);
    unsigned char buf = 0;
    DDebug(this,DebugInfo,"Received '%s' class=%s circuits=%s",
	msg->name(),m_data.m_restart.c_str(),m_data.m_channels.c_str());

    while (true) {
	if (m_data.m_restart == YSTRING("channels")) {
	    if (list->count() > 0)
		terminateCalls(list,"resource-unavailable");
	    else {
		m_data.m_reason = "invalid-ie";
		buf = ISDNQ931IE::ChannelID;
	    }
	    break;
	}

	bool single = (m_data.m_restart == YSTRING("interface"));
	bool all = !single && (m_data.m_restart == YSTRING("all-interfaces"));
	// If all interfaces is specified, ChannelID must not be present
	// If ChannelID is present and allowed, it must contain a single channel code
	if (!(single || all) || (all && list->count() > 0) ||
	    (single && list->count() > 1)) {
	    m_data.m_reason = "invalid-ie";
	    buf = ISDNQ931IE::Restart;
	    break;
	}

	// Terminate all calls if class is 'all-interfaces'
	if (all) {
	    terminateCalls(0,"resource-unavailable");
	    break;
	}

	// Done if no circuits
	if (!circuits())
	    break;

	// Identify the span containing the D-channel
	SignallingCircuitSpan* span = 0;
	if (list->count()) {
	    unsigned int code = static_cast<String*>(list->get())->toInteger(0);
	    SignallingCircuit* cic = circuits()->find(code);
	    if (cic)
		span = cic->span();
	}
	else {
	    // FIXME: Make a proper implementation: identify the span containing the active D-channel
	    // Use the first span
	    ObjList* o = circuits()->m_spans.skipNull();
	    if (o)
		span = static_cast<SignallingCircuitSpan*>(o->get());
	}
	if (span) {
	    // Fill a list with all circuit codes used to reset and terminate calls
	    ObjList m_terminate;
	    for (ObjList* o = circuits()->circuits().skipNull(); o; o = o->skipNext()) {
		SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
		if (span == cic->span())
		    m_terminate.append(new String(cic->code()));
	    }
	    terminateCalls(&m_terminate,"resource-unavailable");
	}
	else
	    Debug(this,DebugNote,
		"Unable to identify span containing D-channel for '%s' request class=%s circuit=%s",
		msg->name(),m_data.m_restart.c_str(),m_data.m_channels.c_str());
	break;
    }
    TelEngine::destruct(list);

    // ACK if no error
    if (m_data.m_reason.null()) {
	ISDNQ931Message* m = new ISDNQ931Message(ISDNQ931Message::RestartAck,
	    false,0,m_callRefLen);
	m->append(msg->removeIE(ISDNQ931IE::ChannelID));
	m->append(msg->removeIE(ISDNQ931IE::Restart));
	sendMessage(m,tei);
	return;
    }

    String diagnostic;
    if (buf)
	diagnostic.hexify(&buf,1);
    Debug(this,DebugNote,
	"Invalid '%s' request class=%s circuits=%s reason='%s' diagnostic=%s",
	msg->name(),m_data.m_restart.c_str(),m_data.m_channels.c_str(),
	m_data.m_reason.c_str(),diagnostic.c_str());
    sendStatus(m_data.m_reason,m_callRefLen,tei,0,false,ISDNQ931Call::Null,0,diagnostic);
}

// Process messages with invalid call reference. See Q.931 5.8
void ISDNQ931::processInvalidMsg(ISDNQ931Message* msg, u_int8_t tei)
{
    if (!msg)
	return;
    DDebug(this,DebugNote,"Received (%p): '%s' with invalid call reference %u [%p]",
	msg,msg->name(),msg->callRef(),this);
    switch (msg->type()) {
	case ISDNQ931Message::Resume:
	case ISDNQ931Message::Setup:
	case ISDNQ931Message::ReleaseComplete:
	    break;
	case ISDNQ931Message::Release:
	    sendRelease(false,msg->callRefLen(),msg->callRef(),
		tei,!msg->initiator(),"invalid-callref");
	    break;
	case ISDNQ931Message::Status:
	    // Assume our call state to be Null. See Q.931 5.8.11
	    // Ignore the message if the reported state is Null
	    {
	    String s = msg->getIEValue(ISDNQ931IE::CallState,"state");
	    if (s != ISDNQ931Call::stateName(ISDNQ931Call::Null))
		sendRelease(false,msg->callRefLen(),msg->callRef(),
		    tei,!msg->initiator(),"wrong-state-message");
	    }
	    break;
	case ISDNQ931Message::StatusEnquiry:
	    sendStatus("status-enquiry-rsp",msg->callRefLen(),msg->callRef(),
		tei,!msg->initiator(),ISDNQ931Call::Null);
	    break;
	default:
	    sendRelease(true,msg->callRefLen(),msg->callRef(),
		tei,!msg->initiator(),"invalid-callref");
	    return;
    }
}

// Try to reserve a circuit if none. Send a restart request on it's behalf
// Start counting the restart interval if no circuit reserved
void ISDNQ931::sendRestart(u_int64_t time, bool retrans)
{
    Lock lock(l3Mutex());
    m_syncCicTimer.stop();
    if (!primaryRate())
	return;
    if (m_restartCic) {
	if (!retrans)
	    return;
    }
    else {
	unsigned int count = circuits() ? circuits()->count() : 0;
	for (m_lastRestart++; m_lastRestart <= count; m_lastRestart++) {
	    String tmp(m_lastRestart);
	    if (reserveCircuit(m_restartCic,0,-1,&tmp,true))
		break;
	}
	if (!m_restartCic) {
	    m_lastRestart = 0;
	    m_syncGroupTimer.start(time ? time : Time::msecNow());
	    return;
	}
    }
    String s(m_restartCic->code());
    DDebug(this,DebugNote,"%s restart for circuit(s) '%s'",
	!retrans ? "Sending" : "Retransmitting",s.c_str());
    // Create the message
    ISDNQ931Message* msg = new ISDNQ931Message(ISDNQ931Message::Restart,true,
	0,m_callRefLen);
    // Don't add 'interface' parameter. We always send the channels, not the interface
    ISDNQ931IE* ie = new ISDNQ931IE(ISDNQ931IE::ChannelID);
    ie->addParam("interface-bri",String::boolText(!primaryRate()));
    ie->addParam("channel-exclusive",String::boolText(true));
    ie->addParam("channel-select","present");
    ie->addParam("type","B");
    ie->addParam("channel-by-number",String::boolText(true));
    ie->addParam("channels",s);
    msg->appendSafe(ie);
    msg->appendIEValue(ISDNQ931IE::Restart,"class","channels");
    m_syncCicTimer.start(time ? time : Time::msecNow());
    sendMessage(msg,0);
}

// End our restart requests
// Release reserved circuit. Continue restarting circuits if requested
void ISDNQ931::endRestart(bool restart, u_int64_t time, bool timeout)
{
    Lock lock(l3Mutex());
    m_syncCicTimer.stop();
    m_syncCicCounter.reset();
    if (m_restartCic) {
	if (!timeout)
	    XDebug(this,DebugInfo,"Ending restart for circuit(s) '%u'",m_restartCic->code());
	else
	    Debug(this,DebugInfo,"Restart timed out for circuit(s) '%u'",
		m_restartCic->code());
	releaseCircuit(m_restartCic);
	m_restartCic = 0;
    }
    if (restart)
	sendRestart(time,false);
    else {
	m_lastRestart = 0;
	m_syncGroupTimer.start(time ? time : Time::msecNow());
    }
}

// Send STATUS. See Q.931 3.1.16
// IE: Cause, CallState, Display
bool ISDNQ931::sendStatus(const char* cause, u_int8_t callRefLen, u_int32_t callRef,
	u_int8_t tei, bool initiator, ISDNQ931Call::State state, const char* display,
	const char* diagnostic)
{
    if (!primaryRate())
	return false;
    // Create message
    ISDNQ931Message* msg = 0;
    if (callRefLen)
	msg = new ISDNQ931Message(ISDNQ931Message::Status,initiator,callRef,callRefLen);
    else
	msg = new ISDNQ931Message(ISDNQ931Message::Status);
    // Set our state for dummy or global call references
    if (!(callRef && callRefLen))
	state = m_restartCic ? ISDNQ931Call::RestartReq : ISDNQ931Call::Null;
    // Add IEs
    ISDNQ931IE* ie = msg->appendIEValue(ISDNQ931IE::Cause,0,cause);
    // We always send status about the local network
    ie->addParamPrefix("location","LN");
    if (diagnostic && ie)
	ie->addParamPrefix("diagnostic",diagnostic);
    msg->appendIEValue(ISDNQ931IE::CallState,"state",ISDNQ931Call::stateName(state));
    if (display)
	msg->appendIEValue(ISDNQ931IE::Display,"display",display);
    return sendMessage(msg,tei);
}

// Send RELEASE (See Q.931 3.1.9) or RELEASE COMPLETE (See Q.931 3.1.10)
// IE: Cause, Display, Signal
bool ISDNQ931::sendRelease(bool release, u_int8_t callRefLen, u_int32_t callRef,
	u_int8_t tei, bool initiator, const char* cause, const char* diag,
	const char* display, const char* signal)
{
    // Create message
    ISDNQ931Message::Type t = release ? ISDNQ931Message::Release : ISDNQ931Message::ReleaseComplete;
    ISDNQ931Message* msg = new ISDNQ931Message(t,initiator,callRef,callRefLen);
    // Add IEs
    if (cause) {
	ISDNQ931IE* ie = msg->appendIEValue(ISDNQ931IE::Cause,0,cause);
	if (diag)
	    ie->addParamPrefix("diagnostic",diag);
    }
    if (display)
	msg->appendIEValue(ISDNQ931IE::Display,"display",display);
    if (signal)
	msg->appendIEValue(ISDNQ931IE::Signal,"signal",signal);
    return sendMessage(msg,tei);
}

/**
 * ISDNQ931Monitor
 */
ISDNQ931Monitor::ISDNQ931Monitor(const NamedList& params, const char* name)
    : SignallingComponent(name,&params,"isdn-q931-mon"),
      SignallingCallControl(params,"isdn."),
      ISDNLayer3(name),
      m_q921Net(0), m_q921Cpe(0), m_cicNet(0), m_cicCpe(0),
      m_parserData(params),
      m_printMsg(true), m_extendedDebug(false)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"ISDNQ931Monitor::ISDNQ931Monitor(%p,'%s') [%p]%s",
	    &params,name,this,tmp.c_str());
    }
#endif
    // Set parser data. Accept maximum data length
    m_parserData.m_maxMsgLen = 0xffffffff;
    m_parserData.m_dbg = this;
    // Debug
    setDebug(params.getBoolValue(YSTRING("print-messages"),true),
	params.getBoolValue(YSTRING("extended-debug"),false));
}

ISDNQ931Monitor::~ISDNQ931Monitor()
{
    terminateMonitor(0,0);
    TelEngine::destruct(attach((ISDNQ921Passive*)0,true));
    TelEngine::destruct(attach((ISDNQ921Passive*)0,false));
    TelEngine::destruct(attach((SignallingCircuitGroup*)0,true));
    TelEngine::destruct(attach((SignallingCircuitGroup*)0,false));
    m_calls.clear();
    DDebug(this,DebugAll,"ISDN Monitor destroyed [%p]",this);
}

// Initialize the monitor and attach both passive layer 2
bool ISDNQ931Monitor::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"ISDNQ931Monitor::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue(YSTRING("debuglevel_q931"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	setDebug(config->getBoolValue(YSTRING("print-messages"),false),
	    config->getBoolValue(YSTRING("extended-debug"),false));
	for (int i = 0; i <= 1; i++) {
	    bool net = (0 == i);
	    if (net && m_q921Net)
		continue;
	    if (!net && m_q921Cpe)
		continue;
	    NamedString* name = config->getParam(net ? "sig-net" : "sig-cpe");
	    if (name) {
		NamedPointer* ptr = YOBJECT(NamedPointer,name);
		NamedList* linkConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
		NamedList params(name->c_str());
		params.addParam("basename",*name);
		if (linkConfig)
		    params.copyParams(*linkConfig);
		else {
		    params.copySubParams(*config,*name + ".");
		    linkConfig = &params;
		}
		ISDNQ921Passive* l2 = YSIGCREATE(ISDNQ921Passive,&params);
		if (!l2)
		    return false;
		attach(l2,net);
		if (!l2->initialize(linkConfig))
		    TelEngine::destruct(attach((ISDNQ921Passive*)0,net));
	    }
	}
    }
    return m_q921Net && m_q921Cpe;
}

const char* ISDNQ931Monitor::statusName() const
{
    if (exiting())
	return "Exiting";
    if (!(m_q921Net && m_q921Cpe))
	return "Layer 2 missing";
    return "Operational";
}

// Notification from layer 2 of data link set/release command or response
void ISDNQ931Monitor::dataLinkState(u_int8_t tei, bool cmd, bool value, ISDNLayer2* layer2)
{
#ifdef DEBUG
    if (debugAt(DebugInfo)) {
	String tmp;
	if (cmd)
	    tmp << "'" << (value ? "Establish" : "Release") << "' request";
	else
	    tmp << "'" << (value ? "YES" : "NO") << "' response";
	DDebug(this,DebugInfo,"Captured %s from '%s'. Clearing monitors",
	    tmp.c_str(),layer2->debugName());
    }
#endif
    terminateMonitor(0,"net-out-of-order");
}

// Notification from layer 2 of data link idle timeout
void ISDNQ931Monitor::idleTimeout(ISDNLayer2* layer2)
{
    DDebug(this,DebugInfo,"Idle timeout from '%s'. Clearing monitors",
	layer2->debugName());
    terminateMonitor(0,"net-out-of-order");
}

// Receive data
void ISDNQ931Monitor::receiveData(const DataBlock& data, u_int8_t tei, ISDNLayer2* layer2)
{
    XDebug(this,DebugAll,"Received data. Length: %u, TEI: %u",data.length(),tei);
    //TODO: Implement segmentation
    ISDNQ931Message* msg = ISDNQ931Message::parse(m_parserData,data,0);
    if (!msg)
	return;
    msg->params().setParam("monitor-sender",layer2->debugName());
    // Print received message
    if (debugAt(DebugInfo) && m_printMsg) {
	String tmp;
	msg->toString(tmp,m_extendedDebug);
	Debug(this,DebugInfo,"Captured message from '%s' (%p)%s",
	    layer2->debugName(),msg,tmp.c_str());
    }
    else
	DDebug(this,DebugInfo,"Captured '%s' (call ref: %u) from '%s'",
	    msg->name(),msg->callRef(),layer2->debugName());
    // Drop some messages
    if (dropMessage(msg)) {
	if (msg->type() == ISDNQ931Message::Restart ||
	    msg->type() == ISDNQ931Message::RestartAck)
	    processMsgRestart(msg);
	else
	    DDebug(this,DebugInfo,"Dropping message message (%p): '%s' from '%s'",
		msg,msg->name(),layer2->debugName());
	TelEngine::destruct(msg);
	return;
    }
    // Find a monitor for this message or create a new one
    ISDNQ931CallMonitor* mon = findMonitor(msg->callRef(),true);
    while (true) {
	if (mon) {
	    mon->enqueue(msg);
	    msg = 0;
	    break;
	}
	// Check if it is a new incoming call
	if (msg->initiator() && msg->type() == ISDNQ931Message::Setup) {
	    lock();
	    ISDNQ931CallMonitor* newMon = new ISDNQ931CallMonitor(this,msg->callRef(),m_q921Net == layer2);
	    m_calls.append(newMon);
	    unlock();
	    newMon->enqueue(msg);
	    msg = 0;
	    break;
	}
	DDebug(this,DebugInfo,
	    "Dropping message message (%p): '%s' from '%s'. Missing monitor for call %u",
	    msg,msg->name(),layer2->debugName(),msg->callRef());
	break;
    }
    TelEngine::destruct(mon);
    TelEngine::destruct(msg);
}

// Attach ISDN Q.921 pasive transport that monitors one side of the link
ISDNQ921Passive* ISDNQ931Monitor::attach(ISDNQ921Passive* q921, bool net)
{
    Lock lock(l3Mutex());
    // Yes, this is a reference to a pointer
    ISDNQ921Passive*& which = net ? m_q921Net : m_q921Cpe;
    // Make no change if same transport
    if (which == q921)
	return 0;
    terminateMonitor(0,q921 ? "layer 2 attach" : "layer 2 detach");
    ISDNQ921Passive* tmp = which;
    which = q921;
    lock.drop();
    const char* type = net ? "NET" : "CPE";
    if (tmp) {
	if (tmp->layer3() == this) {
	    Debug(this,DebugAll,"Detaching L2 %s (%p,'%s') [%p]",
		type,tmp,tmp->toString().safe(),this);
	    static_cast<ISDNLayer2*>(tmp)->attach(0);
	}
	else {
	    Debug(this,DebugNote,"Layer 2 %s (%p,'%s') was not attached to us [%p]",
		type,tmp,tmp->toString().safe(),this);
	    tmp = 0;
	}
    }
    if (!q921)
	return tmp;
    Debug(this,DebugAll,"Attached L2 %s (%p,'%s') [%p]",
	type,q921,q921->toString().safe(),this);
    insert(q921);
    q921->ISDNLayer2::attach(this);
    return tmp;
}

// Attach a circuit group to this call controller
SignallingCircuitGroup* ISDNQ931Monitor::attach(SignallingCircuitGroup* circuits, bool net)
{
    Lock lock(l3Mutex());
    // Yes, this is a reference to a pointer
    SignallingCircuitGroup*& which = net ? m_cicNet : m_cicCpe;
    SignallingCircuitGroup* tmp = which;
    // Don't attach if it's the same object
    if (tmp == circuits)
	return 0;
    terminateMonitor(0,circuits ? "circuit group attach" : "circuit group detach");
    if (tmp && circuits) {
	Debug(this,DebugNote,
	    "Attached circuit group (%p) '%s' while we already have one (%p) '%s'",
	    circuits,circuits->debugName(),tmp,tmp->debugName());
    }
#ifdef DEBUG
    else if (circuits)
	Debug(this,DebugAll,"Circuit group (%p) '%s' attached",circuits,circuits->debugName());
    else
	Debug(this,DebugAll,"Circuit group (%p) '%s' detached",tmp,tmp->debugName());
#endif
    which = circuits;
    return tmp;
}

// Method called periodically to check timeouts
void ISDNQ931Monitor::timerTick(const Time& when)
{
}

// Reserve the same circuit code from both circuit groups
// This is an atomic operation: if one circuit fails to be reserved, both of them will fail
bool ISDNQ931Monitor::reserveCircuit(unsigned int code, bool netInit,
	SignallingCircuit** caller, SignallingCircuit** called)
{
    Lock lock(l3Mutex());
    if (!(m_cicNet && m_cicCpe))
	return false;
    String cic(code);
    if (netInit) {
	*caller = m_cicNet->reserve(cic,true);
	*called = m_cicCpe->reserve(cic,true);
    }
    else {
	*caller = m_cicCpe->reserve(cic,true);
	*called = m_cicNet->reserve(cic,true);
    }
    if (*caller && *called)
	return true;
    releaseCircuit(*caller);
    releaseCircuit(*called);
    return false;
}

// Release a circuit from both groups
bool ISDNQ931Monitor::releaseCircuit(SignallingCircuit* circuit)
{
    Lock lock(l3Mutex());
    if (!circuit)
	return false;
    if (m_cicNet == circuit->group())
	return m_cicNet->release(circuit,true);
    if (m_cicCpe == circuit->group())
	return m_cicCpe->release(circuit,true);
    return false;
}

// Process a restart or restart acknoledge message
// Terminate the monitor having the circuit given in restart message
void ISDNQ931Monitor::processMsgRestart(ISDNQ931Message* msg)
{
    if (msg->type() == ISDNQ931Message::Restart) {
	m_data.processRestart(msg,false);
	if (m_data.m_restart != "channels") {
	    DDebug(this,DebugNote,"Unsupported '%s' request (class: '%s')",
		msg->name(),m_data.m_restart.c_str());
	    return;
	}
    }
    m_data.processChannelID(msg,false);
    ObjList* list = m_data.m_channels.split(',',false);
    if (!list) {
	DDebug(this,DebugNote,"Incorrect '%s' message (circuit(s): '%s')",
	    msg->name(),m_data.m_channels.c_str());
	return;
    }
    if (!m_printMsg)
	DDebug(this,DebugInfo,"Received '%s' message for circuit(s) '%s'",
	    msg->name(),m_data.m_channels.c_str());
    // Terminate monitor(s)
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	ISDNQ931CallMonitor* mon = findMonitor(s->toInteger(-1),false);
	if (mon) {
	    terminateMonitor(mon,"resource-unavailable");
	    TelEngine::destruct(mon);
	}
    }
    delete list;
}

// Find a call monitor by call reference or reserved circuit
ISDNQ931CallMonitor* ISDNQ931Monitor::findMonitor(unsigned int value, bool byCallRef)
{
    Lock lock(this);
    ObjList* obj = m_calls.skipNull();
    if (byCallRef) {
	for (; obj; obj = obj->skipNext()) {
	    ISDNQ931CallMonitor* mon = static_cast<ISDNQ931CallMonitor*>(obj->get());
	    if (value == mon->m_callRef)
		return (mon->ref() ? mon : 0);
	}
	return 0;
    }
    // Find by reserved circuit
    for (; obj; obj = obj->skipNext()) {
	ISDNQ931CallMonitor* mon = static_cast<ISDNQ931CallMonitor*>(obj->get());
	if (mon->m_callerCircuit && value == mon->m_callerCircuit->code())
	    return (mon->ref() ? mon : 0);
    }
    return 0;
}

// Drop some messages
bool ISDNQ931Monitor::dropMessage(const ISDNQ931Message* msg)
{
    if (msg->dummyCallRef())
	return true;
    // Global call reference or a message that should have a dummy call reference
    if (!msg->callRef() || msg->type() == ISDNQ931Message::Restart ||
	msg->type() == ISDNQ931Message::RestartAck)
	return true;
    return false;
}

// Terminate all monitors or only one
void ISDNQ931Monitor::terminateMonitor(ISDNQ931CallMonitor* mon, const char* reason)
{
    Lock lock(this);
    if (mon) {
	mon->setTerminate(reason);
	return;
    }
    // Terminate all monitors
    ObjList* obj = m_calls.skipNull();
    for (; obj; obj = obj->skipNext()) {
	mon = static_cast<ISDNQ931CallMonitor*>(obj->get());
	mon->setTerminate(reason);
    }
}

/**
 * ISDNQ931IE
 */
const TokenDict ISDNQ931IE::s_type[] = {
	{"Shift",                       Shift},
	{"More data",                   MoreData},
	{"Sending complete",            SendComplete},
	{"Congestion level",            Congestion},
	{"Repeat indicator",            Repeat},
	{"Segmented",                   Segmented},
	{"Bearer capability",           BearerCaps},
	{"Cause",                       Cause},
	{"Call identity",               CallIdentity},
	{"Call state",                  CallState},
	{"Channel identification",      ChannelID},
	{"Progress indicator",          Progress},
	{"Network-specific facilities", NetFacility},
	{"Notification indicator",      Notification},
	{"Display",                     Display},
	{"Date/time",                   DateTime},
	{"Keypad facility",             Keypad},
	{"Signal",                      Signal},
	{"Connected number",            ConnectedNo},
	{"Calling number",              CallingNo},
	{"Calling party subaddress",    CallingSubAddr},
	{"Called number",               CalledNo},
	{"Called party subaddress",     CalledSubAddr},
	{"Transit network selection",   NetTransit},
	{"Restart indicator",           Restart},
	{"Low layer compatibility",     LoLayerCompat},
	{"High layer compatibility",    HiLayerCompat},
	// Not used
	{"User-user",                   UserUser},
	{"Escape",                      Escape},
	{0,0}
	};


ISDNQ931IE::ISDNQ931IE(u_int16_t type)
	: NamedList(""), m_type(type)
{
    *(String*)this = typeName(m_type,"Unknown");
}

ISDNQ931IE::~ISDNQ931IE()
{
}

void ISDNQ931IE::toString(String& dest, bool extendedDebug, const char* before)
{
    dest << before;
    dest << *((NamedList*)this);
    // Append content ?
    if (extendedDebug) {
	// Add condeset and value
 	dest << " (codeset=" << (m_type >> 8) << " type=" << (u_int8_t)m_type << ')';
	String tmp;
	// Dump data
	if (m_buffer.length()) {
	    tmp.hexify(m_buffer.data(),m_buffer.length(),' ');
	    dest << "   " << tmp;
	}
	// Show fields
	tmp = before;
	tmp << "  ";
	for (unsigned int i = 0; ; i++) {
	    NamedString* param = getParam(i);
	    if (!param)
		break;
	    dest << tmp << param->name() << '=' << *param;
	}
    }
}

/**
 * ISDNQ931Message
 */
const TokenDict ISDNQ931Message::s_type[] = {
	{"ALERTING",           Alerting},
	{"CALL PROCEEDING",    Proceeding},
	{"CONNECT",            Connect},
	{"CONNECT ACK",        ConnectAck},
	{"PROGRESS",           Progress},
	{"SETUP",              Setup},
	{"SETUP ACK",          SetupAck},
	{"RESUME",             Resume},
	{"RESUME ACK",         ResumeAck},
	{"RESUME REJECT",      ResumeRej},
	{"SUSPEND",            Suspend},
	{"SUSPEND ACK",        SuspendAck},
	{"SUSPEND REJECT",     SuspendRej},
	{"USER INFO",          UserInfo},
	{"DISCONNECT",         Disconnect},
	{"RELEASE",            Release},
	{"RELEASE COMPLETE",   ReleaseComplete},
	{"RESTART",            Restart},
	{"RESTART ACK",        RestartAck},
	{"SEGMENT",            Segment},
	{"CONGESTION CONTROL", CongestionCtrl},
	{"INFORMATION",        Info},
	{"NOTIFY",             Notify},
	{"STATUS",             Status},
	{"STATUS ENQUIRY",     StatusEnquiry},
	{0,0}
	};

ISDNQ931Message::ISDNQ931Message(Type type, bool initiator,
	u_int32_t callRef, u_int8_t callRefLen)
    : SignallingMessage(typeName(type)),
    m_type(type),
    m_initiator(initiator),
    m_callRef(callRef),
    m_callRefLen(callRefLen),
    m_unkMandatory(false),
    m_dummy(false)
{
}

ISDNQ931Message::ISDNQ931Message(Type type)
    : SignallingMessage(typeName(type)),
    m_type(type),
    m_initiator(false),
    m_callRef(0),
    m_callRefLen(0),
    m_unkMandatory(false),
    m_dummy(true)
{
}

ISDNQ931Message::ISDNQ931Message(Type type, ISDNQ931Call* call)
    : SignallingMessage(typeName(type)),
    m_type(type),
    m_initiator(false),
    m_callRef(0),
    m_callRefLen(0),
    m_unkMandatory(false),
    m_dummy(false)
{
    if (!call)
	return;
    m_initiator = call->outgoing();
    m_callRef = call->callRef();
    m_callRefLen = call->callRefLen();
}

ISDNQ931Message::~ISDNQ931Message()
{
}

// Get an IE from list starting from the begining or from a given point
ISDNQ931IE* ISDNQ931Message::getIE(ISDNQ931IE::Type type, ISDNQ931IE* base)
{
    ObjList* obj = m_ie.skipNull();
    // Set start point after base if non 0
    if (base) {
	for (; obj; obj = obj->skipNext())
	    if (base == obj->get()) {
		obj = obj->skipNext();
		break;
	    }
    }
    for (; obj; obj = obj->skipNext()) {
	ISDNQ931IE* ie = static_cast<ISDNQ931IE*>(obj->get());
	if (ie->type() == type)
	    return ie;
    }
    return 0;
}

// Remove an IE from list and returns it
ISDNQ931IE* ISDNQ931Message::removeIE(ISDNQ931IE::Type type, ISDNQ931IE* base)
{
    ObjList* obj = m_ie.skipNull();
    // Set start point after base if non 0
    if (base) {
	for (; obj; obj = obj->skipNext())
	    if (base == obj->get()) {
		obj = obj->skipNext();
		break;
	    }
    }
    ISDNQ931IE* ie = 0;
    for (; obj; obj = obj->skipNext()) {
	ie = static_cast<ISDNQ931IE*>(obj->get());
	if (ie->type() == type)
	    break;
	ie = 0;
    }
    if (ie)
	m_ie.remove(ie,false);
    return ie;
}

// Safely appends an IE to the list
bool ISDNQ931Message::appendSafe(ISDNQ931IE* ie)
{
    if (!ie)
	return false;
    // Special care for some IEs:
    //     Don't append Shift or Segment. Don't accept Repeat for now
    switch (ie->type()) {
	case ISDNQ931IE::Shift:
	case ISDNQ931IE::Segmented:
	case ISDNQ931IE::Repeat:
	    delete ie;
	    return false;
    }
    // This is not a safe way, but is good for now
    // TODO: Insert the IE in the proper order. Insert Shift if nedded. Special care for Repeat IE
    append(ie);
    return true;
}

void ISDNQ931Message::toString(String& dest, bool extendedDebug, const char* indent) const
{
#define STARTLINE(indent) "\r\n" << indent
    const char* enclose = "-----";
    String ind = indent;
    ind << "  ";
    dest << STARTLINE(indent) << enclose;
    dest << STARTLINE(indent) << name() << STARTLINE(ind);
    if (!m_dummy) {
	dest << "[From initiator=" << String::boolText(m_initiator);
	dest << " CallRef=" << (unsigned int)m_callRef << ']';
    }
    else
	dest << "[Dummy call reference]";
    // Dump message header
    if (extendedDebug && m_buffer.length()) {
	String s;
	s.hexify(m_buffer.data(),m_buffer.length(),' ');
	dest << "   " << s;
    }
    // Add IEs
    String ieBefore;
    ieBefore << STARTLINE(ind);
    ObjList* obj = m_ie.skipNull();
    for (; obj; obj = obj->skipNext()) {
	ISDNQ931IE* ie = static_cast<ISDNQ931IE*>(obj->get());
	ie->toString(dest,extendedDebug,ieBefore);
    }
    dest << STARTLINE(indent) << enclose;
#undef STARTLINE
}

void* ISDNQ931Message::getObject(const String& name) const
{
    if (name == YSTRING("ISDNQ931Message"))
	return (void*)this;
    return SignallingMessage::getObject(name);
}

u_int8_t ISDNQ931Message::encode(ISDNQ931ParserData& parserData, ObjList& dest)
{
    Q931Parser parser(parserData);
    return parser.encode(this,dest);
}

ISDNQ931Message* ISDNQ931Message::parse(ISDNQ931ParserData& parserData,
	const DataBlock& buffer, DataBlock* segData)
{
    Q931Parser parser(parserData);
    return parser.decode(buffer,segData);
}

/**
 * Q931Parser
 */
#define Q931_MSG_PROTOQ931 0x08          // Q.931 protocol discriminator
// Get bit 7 to check if the current byte is extended to the next one
// Used to parse message IE
#define Q931_EXT_FINAL(val) ((val & 0x80) != 0)

// Max values for some IEs
#define Q931_MAX_BEARERCAPS_LEN    12
#define Q931_MAX_SEGMENTED_LEN     4
//#define Q931_MAX_CAUSE_LEN         32
#define Q931_MAX_CHANNELID_LEN     255
#define Q931_MAX_CALLINGNO_LEN     255
#define Q931_MAX_CALLEDNO_LEN      255
#define Q931_MAX_KEYPAD_LEN        34

// Parse errors
static const char* s_errorNoData  = "no data";
static const char* s_errorWrongData  = "inconsistent data";
static const char* s_errorUnsuppCoding = "unsupported coding standard";

//
// IE descriptions
//

// *** Fixed (1 byte length) IEs

// 4.5.14
const TokenDict Q931Parser::s_dict_congestion[] = {
	{"recv-ready",     0x00},      // Receiver ready
	{"recv-not-ready", 0x0f},      // Receiver not ready
	// aliases for level=...
	{"yes",            0x00},
	{"true",           0x00},
	{"no",             0x0f},
	{"false",          0x0f},
	{0,0}
	};

static const IEParam s_ie_ieFixed[] = {
	{"lock",       0x08, 0},                             // Shift
	{"codeset",    0x07, 0},                             // Shift
	{"level",      0x0f, Q931Parser::s_dict_congestion}, // Congestion
	{"indication", 0x0f, 0},                             // Repeat
	{0,0,0}
	};

// *** Q.931 4.5.5: Bearer capability

// Q.931 4.5.5. Information transfer capability: Bits 0-4
const TokenDict Q931Parser::s_dict_bearerTransCap[] = {
	{"speech",       0x00},          // Speech
	{"udi",          0x08},          // Unrestricted digital information
	{"rdi",          0x09},          // Restricted digital information
	{"3.1khz-audio", 0x10},          // 3.1 khz audio
	{"udi-ta",       0x11},          // Unrestricted digital information with tone/announcements
	{"video",        0x18},          // Video
	{0,0}
	};

// Q.931 4.5.5. Transfer mode: Bits 5,6
const TokenDict Q931Parser::s_dict_bearerTransMode[] = {
	{"circuit",      0x00},          // Circuit switch mode
	{"packet",       0x40},          // Packet mode
	{0,0}
	};

// Q.931 4.5.5. Transfer rate: Bits 0-4
const TokenDict Q931Parser::s_dict_bearerTransRate[] = {
	{"packet",        0x00},         // Packet mode use
	{"64kbit",        0x10},         // 64 kbit/s
	{"2x64kbit",      0x11},         // 2x64 kbit/s
	{"384kbit",       0x13},         // 384 kbit/s
	{"1536kbit",      0x15},         // 1536 kbit/s
	{"1920kbit",      0x17},         // 1920 kbit/s
	{"multirate",     0x18},         // Multirate (64 kbit/s base rate)
	{0,0}
	};

// Q.931 4.5.5. User information Layer 1 protocol: Bits 0-4
const TokenDict Q931Parser::s_dict_bearerProto1[] = {
	{"v110",          0x01},         // Recomendation V.110 and X.30
	{"mulaw",         0x02},         // Recomendation G.711 mu-law
	{"alaw",          0x03},         // Recomendation G.711 A-law
	{"g721",          0x04},         // Recomendation G.721 32kbit/s ADPCM and I.460
	{"h221",          0x05},         // Recomendation H.221 and H.242
	{"non-CCITT",     0x07},         // Non CCITT standardized rate adaption
	{"v120",          0x08},         // Recomendation V.120
	{"x31",           0x09},         // Recomendation X.31 HDLC flag stuffing
	{0,0}
	};

// Q.931 4.5.5. User information Layer 2 protocol: Bits 0-4
const TokenDict Q931Parser::s_dict_bearerProto2[] = {
	{"q921",          0x02},         // Recommendation Q.921 or I441
	{"x25",           0x06},         // Recommendation X.25 link layer
	{0,0}
	};

// Q.931 4.5.5. User information Layer 3 protocol: Bits 0-4
const TokenDict Q931Parser::s_dict_bearerProto3[] = {
	{"q931",          0x02},         // Recommendation Q.931 or I451
	{"x25",           0x06},         // Recommendation X.25 packet layer
	{0,0}
	};

// IE description
static const IEParam s_ie_ieBearerCaps[] = {
	{"transfer-cap",      0x1f, Q931Parser::s_dict_bearerTransCap},   // Tranfer capability
	{"transfer-mode",     0x60, Q931Parser::s_dict_bearerTransMode},  // Transfer mode
	{"transfer-rate",     0x1f, Q931Parser::s_dict_bearerTransRate},  // Transfer rate
	{"rate-multiplier",   0x7f, 0},                                   // Rate multiplier
	{"layer1-protocol",   0x1f, Q931Parser::s_dict_bearerProto1},     // Layer 1 protocol
	{"layer1-data",       0xff, 0},                                   // Unparsed layer 1 data (for modems)
	{"layer2-protocol",   0x1f, Q931Parser::s_dict_bearerProto2},     // Layer 2 protocol
	{"layer3-protocol",   0x1f, Q931Parser::s_dict_bearerProto3},     // Layer 3 protocol
	{0,0,0}
	};

// *** Q.931 4.5.6: Call identity

// IE description
static const IEParam s_ie_ieCallIdentity[] = {
	{"identity", 0, 0},           // Call identity data
	{0,0,0}
	};

// *** Q.931 4.5.7: Call state

// Call state values: bit 0-5

// IE description
static const IEParam s_ie_ieCallState[] = {
	{"state", 0x3f, ISDNQ931Call::s_states},
	{0,0,0}
	};

// *** Q.931 4.5.8:  Called party number
//     Q.931 4.5.10: Calling party number

// Q.931 4.5.10 Type of number: Bits 4-6
const TokenDict Q931Parser::s_dict_typeOfNumber[] = {
	{"unknown",          0x00},      // Unknown
	{"international",    0x10},      // International number
	{"national",         0x20},      // National number
	{"net-specific",     0x30},      // Network specific number
	{"subscriber",       0x40},      // Subscriber number
	{"abbreviated",      0x60},      // Abbreviated number
	{"reserved",         0x70},      // Reserved for extension
	{0,0}
	};

// Q.931 4.5.10 Numbering plan: Bits 0-3. Apply only for type 0,1,2,4
const TokenDict Q931Parser::s_dict_numPlan[] = {
	{"unknown",          0x00},      // Unknown
	{"isdn",             0x01},      // ISDN/telephoby numbering plan
	{"data",             0x03},      // Data numbering plan
	{"telex",            0x04},      // Telex numbering plan
	{"national",         0x08},      // National numbering plan
	{"private",          0x09},      // Private numbering plan
	{"reserved",         0x0f},      // Reserved for extension
	{0,0}
	};

// Q.931 4.5.10 Presentation indicator: Bits 5,6
const TokenDict Q931Parser::s_dict_presentation[] = {
	{"allowed",          0x00},      // Presentation allowed
	{"restricted",       0x20},      // Presentation restricted
	{"unavailable",      0x40},      // Number not available due to interworking
	{"reserved",         0x50},      // Reserved
	// Aliases for presentation=...
	{"yes",              0x00},
	{"true",             0x00},
	{"no",               0x20},
	{"false",            0x20},
	{0,0}
	};

// Q.931 4.5.10 Presentation indicator: Bits 0,1
const TokenDict Q931Parser::s_dict_screening[] = {
	{"user-provided",        0x00},  // User-provided, not screened
	{"user-provided-passed", 0x01},  // User-provided, verified and passed
	{"user-provided-failed", 0x02},  // User-provided, verified and failed
	{"network-provided",     0x03},  // Network provided
	// Aliases for screening=...
	{"yes",                  0x01},  // User-provided, verified and passed
	{"true",                 0x01},
	{"no",                   0x00},  // User-provided, not screened
	{"false",                0x00},
	{0,0}
	};

// IE description
static const IEParam s_ie_ieNumber[] = {
	{"type",         0x70, Q931Parser::s_dict_typeOfNumber}, // Type of number
	{"plan",         0x0f, Q931Parser::s_dict_numPlan},      // Numbering plan
	{"presentation", 0x60, Q931Parser::s_dict_presentation}, // Presentation
	{"screening",    0x03, Q931Parser::s_dict_screening},    // Screening
	{"number",       0x7f, 0},                               // The number
	{0,0,0}
	};

// *** Q.931 4.5.9:  Called party subaddress
//     Q.931 4.5.11: Calling party subaddress

// Q.931 4.5.9 Type of subaddress: Bits 5-6
const TokenDict Q931Parser::s_dict_subaddrType[] = {
	{"nsap",  0x00},                 // NSAP (CCITT Rec. X.213/ISO 8348 AD2)
	{"user",  0x20},                 // User-specified
	{0,0}
	};

// IE description
static const IEParam s_ie_ieSubAddress[] = {
	{"type",         0x60, Q931Parser::s_dict_subaddrType}, // Type of subaddress
	{"odd",          0x10, 0},                              // Odd/even indicator of number of address signals
	{"subaddress",   0xff, 0},                              // Subaddress information
	{0,0,0}
	};

// *** Q.931 4.5.13: Channel identification

// Q.931 4.5.13. Channel id selection for BRI interface: Bits 0,1
const TokenDict Q931Parser::s_dict_channelIDSelect_BRI[] = {
	{"none", 0x00},                  // No channel
	{"b1",   0x01},                  // B1 channel
	{"b2",   0x02},                  // B2 channel
	{"any",  0x03},                  // Any channel
	{0,0}
	};

// Q.931 4.5.13. Channel id selection for PRI interface: Bits 0,1
const TokenDict Q931Parser::s_dict_channelIDSelect_PRI[] = {
	{"none",     0x00},              // No channel
	{"present",  0x01},              // Defined by the following bytes
	{"reserved", 0x02},              // Reserved value
	{"any",      0x03},              // Any channel
	{0,0}
	};

// Q.931 4.5.13. Channel type: Bits 0-3
const TokenDict Q931Parser::s_dict_channelIDUnits[] = {
	{"B",   0x03},                   // B-channel
	{"H0",  0x06},                   // H0-channel
	{"H11", 0x08},                   // H11-channel
	{"H12", 0x09},                   // H12-channel
	{0,0}
	};

// IE description
static const IEParam s_ie_ieChannelID[] = {
	{"interface-bri",     0x20, 0},                          // Interface it's a basic rate one
	{"channel-exclusive", 0x08, 0},                          // The indicated B channel is exclusive/preferred
	{"d-channel",         0x04, 0},                          // The channel identified is the D-channel
	{"channel-select",    0x03, Q931Parser::s_dict_channelIDSelect_BRI}, // Channel select for BRI interface
	{"channel-select",    0x03, Q931Parser::s_dict_channelIDSelect_PRI}, // Channel select for PRI interface
	{"interface",         0x7f, 0},                          // Interface identifier
	{"channel-by-number", 0x10, 0},                          // Channel is given by number or slot map
	{"type",              0x0f, Q931Parser::s_dict_channelIDUnits},      // Channel type
	{"channels",          0x7f, 0},                          // Channel number(s)
	{"slot-map",          0xff, 0},                          // Slot-map
	{0,0,0}
	};

// *** Q.931 4.5.15: Date/time

// IE description
static const IEParam s_ie_ieDateTime[] = {
	{"year",   0xff, 0},                // Integer value
	{"month",  0xff, 0},
	{"day",    0xff, 0},
	{"hour",   0xff, 0},
	{"minute", 0xff, 0},
	{"second", 0xff, 0},
	{0,0,0}
	};

// *** Q.931 4.5.16: Display

// IE description
static const IEParam s_ie_ieDisplay[] = {
	{"charset", 0x7f, 0},             // Charset, if any
	{"display", 0x7f, 0},             // IA5 characters
	{0,0,0}
	};

// *** Q.931 4.5.17: High layer compatibility

// IE description
static const IEParam s_ie_ieHiLayerCompat[] = {
	{"interpretation", 0x1c, 0},     // Interpretation
	{"presentation",   0x03, 0},     // Presentation method or protocol profile
	{"layer",          0x7f, 0},     // High layer characteristics identification if presentation is 0x01
	{"layer",          0x7f, 0},     // High layer characteristics identification for other values of interpretation
	{"layer-ext",      0x7f, 0},     // Extended high layer characteristics identification if presentation is 0x01
	{"layer-ext",      0x7f, 0},     // Extended high layer characteristics identification for other values of interpretation
	{0,0,0}
	};

// *** Q.931 4.5.18: Keypad facility

// IE description
static const IEParam s_ie_ieKeypad[] = {
	{"keypad", 0, 0},                // IA5 characters
	{0,0,0}
	};

// *** Q.931 4.5.19: Low layer compatibility

// Q.931 4.5.19. User information Layer 2 protocol: Bits 0-4
const TokenDict Q931Parser::s_dict_loLayerProto2[] = {
	{"iso1745",       0x01},         // Basic mode ISO 1745
	{"q921",          0x02},         // Recommendation Q.921 or I441
	{"x25",           0x06},         // Recommendation X.25 link layer
	{"x25-multilink", 0x0f},         // Recommendation X.25 multilink
	{"lapb",          0x08},         // Extended LAPB; for half duplex operation
	{"hdlc-arm",      0x09},         // HDLC ARM (ISO 4335)
	{"hdlc-nrm",      0x0a},         // HDLC NRM (ISO 4335)
	{"hdlc-abm",      0x0b},         // HDLC ABM (ISO 4335)
	{"lan",           0x0c},         // LAN logical link control
	{"x75",           0x0d},         // Recommendation X.75. Single Link Procedure (SLP)
	{"q922",          0x0e},         // Recommendation Q.922
	{"q922-core",     0x0f},         // Core aspects of Recommendation Q.922
	{"user",          0x10},         // User specified
	{"iso7776",       0x11},         // ISO 7776 DTE-DTE operation
	{0,0}
	};

// Q.931 4.5.19. User information Layer 3 protocol: Bits 0-4
const TokenDict Q931Parser::s_dict_loLayerProto3[] = {
	{"q931",          0x02},         // Recommendation Q.931 or I451
	{"x25",           0x06},         // Recommendation X.25 packet layer
	{"iso8208",       0x07},         // ISO/IEC 8208 (X.25 packet level protocol for data terminal equipment)
	{"x223",          0x08},         // CCITT Rec. X.223|ISO 8878
	{"iso8473",       0x09},         // ISO/IEC 8473 (OSI connectionless mode protocol)
	{"t70",           0x0a},         // Recommendation T.70 minimum network layer
	{"iso-tr-9577",   0x0b},         // ISO/IEC TR 9577 (Protocol identification in the network layer)
	{"user",          0x10},         // User specified
	{0,0}
	};

// IE description
static const IEParam s_ie_ieLoLayerCompat[] = {
	{"transfer-cap",      0x1f, Q931Parser::s_dict_bearerTransCap},       // Tranfer capability
	{"out-band",          0x40, 0},                           // Outband negotiation possible or not
	{"transfer-mode",     0x60, Q931Parser::s_dict_bearerTransMode},      // Transfer mode
	{"transfer-rate",     0x1f, Q931Parser::s_dict_bearerTransRate},      // Transfer rate
	{"rate-multiplier",   0x7f, 0},                           // Rate multiplier
	{"layer1-protocol",   0x1f, Q931Parser::s_dict_bearerProto1},         // Layer 1 protocol
	{"layer1-data",       0xff, 0},                           // Unparsed layer 1 data
	{"layer2-protocol",   0x1f, Q931Parser::s_dict_loLayerProto2},        // Layer 2 protocol
	{"layer2-data",       0xff, 0},                           // Unparsed layer 2 data
	{"layer2-window-size",0x1f, 0},                           // Window size (k)
	{"layer3-protocol",   0x1f, Q931Parser::s_dict_loLayerProto3},        // Layer 3 protocol
	{"layer3-mode",       0x60, 0},                           // Mode for CCITT 'layer3-protocol' values
	{"layer3-user-data",  0x7f, 0},                           // Optional Layer 3 user data
	{"layer3-7a",         0x7f, 0},                           // Unparsed octet 7a
	{"layer3-def-size",   0x1f, 0},                           // Default packet size
	{"layer3-packet-size",0x7f, 0},                           // Packet window size
	{0,0,0}
	};

// *** Q.931 4.5.21: Network-specific facilities
//     Q.931 4.5.29: Transit network selection

// Q.931 4.5.21. Type of network identification: Bits 4-6
const TokenDict Q931Parser::s_dict_networkIdType[] = {
	{"user",          0x00},         // User specified
	{"national",      0x20},         // National network identification
	{"international", 0x30},         // International network identification
	{0,0}
	};

// Q.931 4.5.21. Network identification plan: Bits 0-3
const TokenDict Q931Parser::s_dict_networkIdPlan[] = {
	{"unknown",       0x00},         // Unknown
	{"carrier",       0x01},         // Carrier identification code
	{"data",          0x03},         // Data network identification code (Recommendation X.121)
	{0,0}
	};

// Q.931 4.5.21: Network-specific facilities
static const IEParam s_ie_ieNetFacility[] = {
	{"type",     0x70, Q931Parser::s_dict_networkIdType}, // Type of network identification
	{"plan",     0x0f, Q931Parser::s_dict_networkIdPlan}, // Network identification plan
	{"id",       0xff, 0},                                // Network identification
	{"facility", 0xff, 0},                                // Network-specific facility
	{0,0,0}
	};

// Q.931 4.5.29: Transit network selection
static const IEParam s_ie_ieNetTransit[] = {
	{"type", 0x70, Q931Parser::s_dict_networkIdType}, // Type of network identification
	{"plan", 0x0f, Q931Parser::s_dict_networkIdPlan}, // Network identification plan
	{"id",   0xff, 0},                                // Network identification
	{0,0,0}
	};

// *** Q.931 4.5.22: Notification

const TokenDict Q931Parser::s_dict_notification[] = {
	{"suspended",             0x00},
	{"resumed",               0x01},
	{"bearer-service-change", 0x02},
	{0,0}
	};

// IE description
static const IEParam s_ie_ieNotification[] = {
	{"notification", 0x7f, Q931Parser::s_dict_notification},
	{0,0,0}
	};

// *** Q.931 4.5.23: Progress indication

// Progress description: Bits 0-6
const TokenDict Q931Parser::s_dict_progressDescr[] = {
	{"non-isdn",              0x01}, // Call is not end-to-end ISDN, further call progress info may be present in-band
	{"non-isdn-destination",  0x02}, // Destination address is non ISDN
	{"non-isdn-source",       0x03}, // Source address is non ISDN
	{"return-to-isdn",        0x04}, // Call has returned to the ISDN
	{"interworking",          0x05}, // Interworking has occurred and has resulted in a telecommunication change
	{"in-band-info",          0x08}, // In-band info or an appropriate pattern is now available
	{0,0}
	};

// IE description
static const IEParam s_ie_ieProgress[] = {
	{"location",    0x0f, SignallingUtils::locations()},
	{"description", 0x7f, Q931Parser::s_dict_progressDescr},
	{0,0,0}
	};

// *** Q.931 4.5.25: Restart indicator

// Class: Bits 0-2
const TokenDict Q931Parser::s_dict_restartClass[] = {
	{"channels",       0x00},        // Indicated channels
	{"interface",      0x06},        // Single interface
	{"all-interfaces", 0x07},        // All interfaces
	{0,0}
	};

// IE description
static const IEParam s_ie_ieRestart[] = {
	{"class", 0x07, Q931Parser::s_dict_restartClass},
	{0,0,0}
	};

// *** Q.931 4.5.26: Segmented message

// IE description
static const IEParam s_ie_ieSegmented[] = {
	{"first",     0x80, 0},          // First/subsequent segment
	{"remaining", 0x7f, 0},          // Number of segments remaining
	{"message",   0x7f, 0},          // Segmented message type
	{0,0,0}
	};

// *** Q.931 4.5.28: Signal

// Q.931 4.5.28 Signal values: first byte
const TokenDict Q931Parser::s_dict_signalValue[] = {
	{"dial",         0x00},  // Dial tone on
	{"ring",         0x01},  // Ring back tone on
	{"intercept",    0x02},  // Intercept tone on
	{"congestion",   0x03},  // Network congestion tone on
	{"busy",         0x04},  // Busy tone on
	{"confirm",      0x05},  // Confirm tone on
	{"answer",       0x06},  // Answer tone on
	{"call-waiting", 0x07},  // Call waiting tone on
	{"off-hook",     0x08},  // Off-hook tone on
	{"preemption",   0x09},  // Preemption tone on
	{"tones-off",    0x3f},  // Tones off
	{"patern0",      0x40},  // Alering on - patern 0
	{"patern1",      0x41},  // Alering on - patern 1
	{"patern2",      0x42},  // Alering on - patern 2
	{"patern3",      0x43},  // Alering on - patern 3
	{"patern4",      0x44},  // Alering on - patern 4
	{"patern5",      0x45},  // Alering on - patern 5
	{"patern6",      0x46},  // Alering on - patern 6
	{"patern7",      0x47},  // Alering on - patern 7
	{"alerting-off", 0x4f},  // Alerting off
	{0,0}
	};

// IE description
static const IEParam s_ie_ieSignal[] = {
	{"signal", 0xff, Q931Parser::s_dict_signalValue}, // Signal value
	{0,0,0}
	};

// *** Q.931 4.5.30: User-user

// IE description
static const IEParam s_ie_ieUserUser[] = {
	{"protocol",    0xff, 0},        // Protocol discriminator
	{"information", 0xff, 0},        // User information
	{0,0,0}
	};

// Decode received buffer
ISDNQ931Message* Q931Parser::decode(const DataBlock& buffer, DataBlock* segData)
{
    XDebug(m_settings->m_dbg,DebugAll,"Start parse %u bytes",buffer.length());
    // Set data
    u_int8_t* data = (u_int8_t*)buffer.data();
    u_int32_t len = buffer.length();
    // Parse header. Create message
    if (!createMessage(data,len))
	return reset();
    // Skip header bytes:
    //   3: protocol discriminator, call reference length, message type
    //   n: call reference
    u_int32_t consumed = 3 + m_msg->callRefLen();
    ISDNQ931IE* ie = 0;
    // Parse SEGMENT
    if (m_msg->type() == ISDNQ931Message::Segment) {
	len -= consumed;
	data += consumed;
	return processSegment(data,len,segData);
    }
    // Parse IEs
    m_activeCodeset = m_codeset = 0;
    for (;;) {
	// Append IE if any
	if (ie) {
	    // Skip non-locked IEs if told to do so
	    if (m_settings->flag(ISDNQ931::IgnoreNonLockedIE)) {
		bool ignore = false;
		if (ie->type() == ISDNQ931IE::Shift)
		    ignore = m_skip = !ie->getBoolValue(YSTRING("lock"),false);
		else if (m_skip) {
		    ignore = true;
		    m_skip = false;
		}
		if (ignore) {
		    String* s = static_cast<String*>(ie);
		    *s = String("ignored-") + *s;
		}
	    }
	    XDebug(m_settings->m_dbg,DebugAll,"Adding IE '%s'. %u bytes consumed [%p]",
		ie->c_str(),consumed,m_msg);
	    if (m_settings->m_extendedDebug)
		ie->m_buffer.assign(data,consumed);
	    m_msg->append(ie);
	}
	// Reset the active codeset
	m_activeCodeset = m_codeset;
	// End of data ?
	if (consumed >= len)
	    break;
	len -= consumed;
	data += consumed;
	consumed = 0;
	ie = getIE(data,len,consumed);
	if (!ie)
	    break;
	// Check shift
	if (ie->type() == ISDNQ931IE::Shift)
	    shiftCodeset(ie);
    }
    return reset();
}

// Encode a message to a buffer. If buffer is too long, split it into segments if allowed
u_int8_t Q931Parser::encode(ISDNQ931Message* msg, ObjList& dest)
{
    if (!msg)
	return 0;
    m_msg = msg;
    // Set message header buffer
    // Proto discriminator (1) + call reference length (1) + call reference (max 4) + type (1) + [Segmented IE]
    u_int8_t header[7 + Q931_MAX_SEGMENTED_LEN];
    ::memset(header,0,sizeof(header));
    u_int8_t headerLen = fillHeader(header,m_msg,m_settings->m_dbg);
    if (!headerLen) {
	reset();
	return 0;
    }
    if (m_settings->m_extendedDebug)
	msg->m_buffer.assign(header,headerLen);
    // We assume that at this point the IE list is ready to be encoded as it is
    // Check if segmentation is allowed
    if (!m_settings->m_allowSegment)
	return encodeMessage(dest,false,header,headerLen);
    // Segmentation is allowed
    bool segmented = false;
    // Encode each IE into it's buffer. Check if the largest IE will fit in a message
    if (!encodeIEList(segmented,headerLen))
	return reset(0);
    // Check if the message is segmented
    if (!segmented)
	return encodeMessage(dest,true,header,headerLen);
    // Message will be segmented. Change the header
    // Change the message type to Segment. Append Segmented IE
    u_int8_t msgType = header[headerLen - 1];        // Message type it's the last byte of the header
    header[headerLen - 1] = 0x7f & (u_int8_t)ISDNQ931Message::Segment;
    header[headerLen++] = 0x7f & (u_int8_t)ISDNQ931IE::Segmented;
    header[headerLen++] = 2;                         // IE information length after IE header
    u_int8_t remainingIdx = headerLen;               // Remember the index to write the remaining segments count
    header[headerLen++] = 0;                         // Reserved space for remaining segments
    header[headerLen++] = msgType;                   // Message type
    // Create message segments
    ObjList* obj = m_msg->ieList()->skipNull();
    u_int8_t count = 0;
    DataBlock* segment = 0;
    while (true) {
	ISDNQ931IE* ie = static_cast<ISDNQ931IE*>(obj->get());
	DataBlock* data = &(ie->m_buffer);
	obj = obj->skipNext();
	// Force append when done with the list
	bool append = (bool)(!obj);
	if (!segment)
	    segment = new DataBlock(header,headerLen);
	// Add data to buffer if we have enough place
	// Force append if new data excceeds the segment length
	if (segment->length() + data->length() <= m_settings->m_maxMsgLen) {
	    *segment += *data;
	    data = 0;
	}
	else
	    append = true;
	// Append segment to list
	if (append) {
	    if (!appendSegment(dest,segment,count)) {
		count = 0;
		break;
	    }
	    segment = 0;
	}
	// Append data to segment if not already added
	if (data) {
	    if (!segment)
		segment = new DataBlock(header,headerLen);
	    *segment += *data;
	}
	// Keep going if we have more IEs
	if (obj)
	    continue;
	// No more IEs. Check if last one was added to segment
	if (segment && !appendSegment(dest,segment,count)) {
	    count = 0;
	    break;
	}
	break;
    }
    if (!count) {
	dest.clear();
	return reset(0);
    }
    u_int8_t remaining = count;
    bool first = true;
    obj = dest.skipNull();
    for (; obj; obj = obj->skipNext()) {
	segment = static_cast<DataBlock*>(obj->get());
	u_int8_t* data = (u_int8_t*)(segment->data());
	if (!first)
	    data[remainingIdx] = --remaining;
	else {
	    data[remainingIdx] = 0x80 | --remaining;
	    first = false;
	}
    }
    return reset(count);
}

// Create message segments if segmented
u_int8_t Q931Parser::encodeMessage(ObjList& dest, bool ieEncoded,
	u_int8_t* header, u_int8_t headerLen)
{
    DataBlock* buf = new DataBlock(header,headerLen);
    ObjList* obj = m_msg->ieList()->skipNull();
    for (; obj; obj = obj->skipNext()) {
	ISDNQ931IE* ie = static_cast<ISDNQ931IE*>(obj->get());
	// Encode current IE if not already encoded
	if (!ieEncoded && !encodeIE(ie,ie->m_buffer)) {
	    delete buf;
	    return reset(0);
	}
	// Check for valid data length
	if (buf->length() + ie->m_buffer.length() > m_settings->m_maxMsgLen) {
	    Debug(m_settings->m_dbg,DebugWarn,
		"Can't encode message. Length %u exceeds limit %u [%p]",
		buf->length() + ie->m_buffer.length(),m_settings->m_maxMsgLen,m_msg);
	    delete buf;
	    return reset(0);
	}
	*buf += ie->m_buffer;
    }
    dest.append(buf);
    return reset(1);
}

// Encode a list of IEs
bool Q931Parser::encodeIEList(bool& segmented, u_int8_t headerLen)
{
    segmented = false;
    ObjList* obj = m_msg->ieList()->skipNull();
    // Empty message
    if (!obj)
	return true;
    // Encode each IE into it's buffer
    u_int32_t dataLen = headerLen;
    ISDNQ931IE* ieMax = 0;
    for (; obj; obj = obj->skipNext()) {
	// Encode current IE
	ISDNQ931IE* ie = static_cast<ISDNQ931IE*>(obj->get());
	if (!encodeIE(ie,ie->m_buffer))
	    return false;
	// Check if the message will be segmented
	if (!segmented) {
	    dataLen += ie->m_buffer.length();
	    if (dataLen > m_settings->m_maxMsgLen)
		segmented = true;
	}
	// Keep the IE with the largest buffer
	if (!ieMax || ieMax->m_buffer.length() < ie->m_buffer.length())
	    ieMax = ie;
    }
    // Check if the largest IE buffer fits a message
    if (ieMax && ieMax->m_buffer.length() > m_settings->m_maxMsgLen - headerLen) {
	Debug(m_settings->m_dbg,DebugWarn,
	    "Can't encode message. IE '%s' with length %u won't fit limit %u [%p]",
	    ieMax->c_str(),ieMax->m_buffer.length(),m_settings->m_maxMsgLen,m_msg);
	return false;
    }
    return true;
}

// Append a segment to a given list
bool Q931Parser::appendSegment(ObjList& dest, DataBlock* segment,
	u_int8_t& count)
{
    count++;
    // We can't split a message in more then 128 segments (see Q.931 4.5.26)
    if (count <= m_settings->m_maxSegments) {
	dest.append(segment);
	return true;
    }
    delete segment;
    Debug(m_settings->m_dbg,DebugWarn,
	"Can't encode message. Too many segments [%p]",m_msg);
    return false;
}

// Encode a single IE
bool Q931Parser::encodeIE(ISDNQ931IE* ie, DataBlock& buffer)
{
    switch (ie->type()) {
	case ISDNQ931IE::BearerCaps:      return encodeBearerCaps(ie,buffer);
	case ISDNQ931IE::Cause:
	    {
		DataBlock tmp;
		if (SignallingUtils::encodeCause(
		    static_cast<SignallingComponent*>(m_settings->m_dbg),
		    tmp,*ie,ISDNQ931IE::typeName(ie->type()),false)) {
		    unsigned char id = ISDNQ931IE::Cause;
		    buffer.assign(&id,1);
		    buffer += tmp;
		    return true;
		}
		return false;
	    }
	case ISDNQ931IE::Display:         return encodeDisplay(ie,buffer);
	case ISDNQ931IE::CallingNo:       return encodeCallingNo(ie,buffer);
	case ISDNQ931IE::CalledNo:        return encodeCalledNo(ie,buffer);
	case ISDNQ931IE::CallState:       return encodeCallState(ie,buffer);
	case ISDNQ931IE::ChannelID:       return encodeChannelID(ie,buffer);
	case ISDNQ931IE::Progress:        return encodeProgress(ie,buffer);
	case ISDNQ931IE::Notification:    return encodeNotification(ie,buffer);
	case ISDNQ931IE::Keypad:          return encodeKeypad(ie,buffer);
	case ISDNQ931IE::Signal:          return encodeSignal(ie,buffer);
	case ISDNQ931IE::Restart:         return encodeRestart(ie,buffer);
	case ISDNQ931IE::SendComplete:    return encodeSendComplete(ie,buffer);
	case ISDNQ931IE::HiLayerCompat:   return encodeHighLayerCap(ie,buffer);
	case ISDNQ931IE::UserUser:        return encodeUserUser(ie,buffer);
    }
    Debug(m_settings->m_dbg,DebugMild,"Encoding not implemented for IE '%s' [%p]",
	ie->c_str(),m_msg);
    // Encode anyway. Only type with length=0
    u_int8_t header[2] = {(u_int8_t)ie->type(),0};
    buffer.assign(header,sizeof(header));
    return true;
}

ISDNQ931IE* Q931Parser::errorParseIE(ISDNQ931IE* ie,
	const char* reason, const u_int8_t* data, u_int32_t len)
{
    Debug(m_settings->m_dbg,DebugNote,"Error parse IE ('%s'): %s [%p]",
	ie->c_str(),reason,m_msg);
    ie->addParam("error",reason);
    if (len)
	SignallingUtils::dumpData(0,*ie,"error-data",data,len);
    return ie;
}

// Check the coding standard of an IE
bool Q931Parser::checkCoding(u_int8_t value, u_int8_t expected, ISDNQ931IE* ie)
{
    value &= 0x60;
    if (value == expected)
	return true;
    String s = lookup(value,SignallingUtils::codings(),0);
    if (s.null())
	s = (unsigned int)value;
    ie->addParam("coding",s.c_str());
    return false;
}

// Skip extended bytes until a byte with bit 0 is reached
u_int8_t Q931Parser::skipExt(const u_int8_t* data, u_int8_t len, u_int8_t& crt)
{
    u_int8_t skip = 0;
    for (; crt < len && !Q931_EXT_FINAL(data[crt]); crt++, skip++) ;
    if (crt < len) {
	crt++;
	skip++;
    }
    return skip;
}

// Create a message from received data (parse message header)
// See Q.931 5.8.1, 5.8.2, 5.8.3.1 for protocol discriminator, message length
//   and call reference length errors
bool Q931Parser::createMessage(u_int8_t* data, u_int32_t len)
{
    bool initiator = false;
    u_int32_t callRef = 0;
    u_int8_t callRefLen = 0;
    // We should have at least 3 bytes:
    //   1 for protocol discriminator, 1 for call reference and 1 for message type
    if (!data || len < 3) {
	Debug(m_settings->m_dbg,DebugWarn,
	    "Not enough data (%u) for message header",len);
	return false;
    }
    // Check protocol discriminator
    if (data[0] != Q931_MSG_PROTOQ931) {
	Debug(m_settings->m_dbg,DebugWarn,"Unknown protocol discriminator %u",data[0]);
	return false;
    }
    // Check for dummy call reference
    if (data[1]) {
	// Call id length: bits 4-7 of the 2nd byte should be 0
	if (data[1] & 0xf0) {
	    Debug(m_settings->m_dbg,DebugWarn,
		"Call reference length %u is incorrect",data[1]);
	    return false;
	}
	// Call id length: bits 0-3 of the 2nd byte
	callRefLen = data[1] & 0x0f;
	// Initiator flag: bit 7 of the 3rd byte - 0: From initiator. 1: To initiator
	initiator = (data[2] & 0x80) == 0;
	// We should have at least (callRefLen + 3) bytes:
	//   1 for protocol discriminator, 1 for call reference length,
	//   1 for message type and the call reference
	if ((unsigned int)(callRefLen + 3) > len) {
	    Debug(m_settings->m_dbg,DebugWarn,
		"Call reference length %u greater then data length %u",
		callRefLen,len);
	    return false;
	}
	// Call reference
	switch (callRefLen) {
	    case 4:
		callRef = (data[2] & 0x7f) << 24 | data[3] << 16 | data[4] << 8 | data[5];
		break;
	    case 3:
		callRef = (data[2] & 0x7f) << 16 | data[3] << 8 | data[4];
		break;
	    case 2:
		callRef = (data[2] & 0x7f) << 8 | data[3];
		break;
	    case 1:
		callRef = data[2] & 0x7f;
		break;
	    default:
		Debug(m_settings->m_dbg,DebugWarn,
		    "Unsupported call reference length %u",callRefLen);
		return false;
	}
    }
    // Message type: bits 0-6 of the 1st byte after the call reference
    u_int8_t t = data[callRefLen + 2] & 0x7f;
    if (!ISDNQ931Message::typeName(t)) {
	Debug(m_settings->m_dbg,DebugNote,"Unknown message type %u",t);
	return false;
    }
    if (callRefLen)
	m_msg = new ISDNQ931Message((ISDNQ931Message::Type)t,initiator,callRef,
	    callRefLen);
    else
	m_msg = new ISDNQ931Message((ISDNQ931Message::Type)t);
    if (m_settings->m_extendedDebug)
	m_msg->m_buffer.assign(data,callRefLen + 3);
    XDebug(m_settings->m_dbg,DebugAll,"Created message (%p): '%s'",
	m_msg,m_msg->name());
    return true;
}

// Process received Segment message
ISDNQ931Message* Q931Parser::processSegment(const u_int8_t* data, u_int32_t len,
	DataBlock* segData)
{
    if (!segData) {
	Debug(m_settings->m_dbg,DebugNote,
	    "Dropping segment message. Not allowed [%p]",m_msg);
	TelEngine::destruct(m_msg);
	return reset();
    }
    u_int32_t consumed = 0;
    ISDNQ931IE* ie = getIE(data,len,consumed);
    if (!ie) {
	TelEngine::destruct(m_msg);
	return reset();
    }
    if (ie->type() != ISDNQ931IE::Segmented || consumed > len) {
	Debug(m_settings->m_dbg,DebugNote,
	    "Dropping segment message with missing or invalid Segmented IE [%p]",
	    m_msg);
	delete ie;
	TelEngine::destruct(m_msg);
	return reset();
    }
    m_msg->append(ie);
    segData->assign((void*)(data + consumed),len - consumed);
    return reset();
}

// Get a single IE from a buffer
ISDNQ931IE* Q931Parser::getIE(const u_int8_t* data, u_int32_t len, u_int32_t& consumed)
{
    consumed = 0;
    if (!(data && len))
	return 0;
    // Check if this is a fixed (1 byte length) or variable length IE
    // Fixed: Bit 7 is 1. See Q.931 4.5.1
    if ((data[0] >> 7)) {
	consumed = 1;
	return getFixedIE(data[0]);
    }
    // Get type
    u_int16_t type = ((u_int16_t)m_activeCodeset << 8) | data[0];
    // Variable length
    // Check/Get length. Byte 2 is the length of the rest of the IE
    u_int8_t ieLen = ((len == 1) ? 1 : data[1]);
    XDebug(m_settings->m_dbg,DebugAll,"Decoding IE %u=%s len=%u [%p]",
	type,ISDNQ931IE::typeName(type,"Unknown"),ieLen,m_msg);
    if (len == 1 || ieLen > len - 2) {
	Debug(m_settings->m_dbg,DebugNote,
	    "Invalid variable IE length %u. Remaing data: %u [%p]",
	    ieLen,len,m_msg);
	consumed = len;
	return 0;
    }
    consumed = 2 + ieLen;
    // Skip type and length
    u_int8_t* ieData = (u_int8_t*)data + 2;
    switch (type) {
#define CASE_DECODE_IE(id,method) case id: return method(new ISDNQ931IE(id),ieData,ieLen);
	CASE_DECODE_IE(ISDNQ931IE::BearerCaps,decodeBearerCaps)
	CASE_DECODE_IE(ISDNQ931IE::Display,decodeDisplay)
	CASE_DECODE_IE(ISDNQ931IE::CallingNo,decodeCallingNo)
	CASE_DECODE_IE(ISDNQ931IE::CalledNo,decodeCalledNo)
	CASE_DECODE_IE(ISDNQ931IE::CallIdentity,decodeCallIdentity)
	CASE_DECODE_IE(ISDNQ931IE::CallState,decodeCallState)
	CASE_DECODE_IE(ISDNQ931IE::ChannelID,decodeChannelID)
	CASE_DECODE_IE(ISDNQ931IE::Progress,decodeProgress)
	CASE_DECODE_IE(ISDNQ931IE::NetFacility,decodeNetFacility)
	CASE_DECODE_IE(ISDNQ931IE::Notification,decodeNotification)
	CASE_DECODE_IE(ISDNQ931IE::DateTime,decodeDateTime)
	CASE_DECODE_IE(ISDNQ931IE::Keypad,decodeKeypad)
	CASE_DECODE_IE(ISDNQ931IE::Signal,decodeSignal)
	CASE_DECODE_IE(ISDNQ931IE::ConnectedNo,decodeConnectedNo)
	CASE_DECODE_IE(ISDNQ931IE::CallingSubAddr,decodeCallingSubAddr)
	CASE_DECODE_IE(ISDNQ931IE::CalledSubAddr,decodeCalledSubAddr)
	CASE_DECODE_IE(ISDNQ931IE::Restart,decodeRestart)
	CASE_DECODE_IE(ISDNQ931IE::Segmented,decodeSegmented)
	CASE_DECODE_IE(ISDNQ931IE::NetTransit,decodeNetTransit)
	CASE_DECODE_IE(ISDNQ931IE::LoLayerCompat,decodeLoLayerCompat)
	CASE_DECODE_IE(ISDNQ931IE::HiLayerCompat,decodeHiLayerCompat)
	CASE_DECODE_IE(ISDNQ931IE::UserUser,decodeUserUser)
#undef CASE_DECODE_IE
	case ISDNQ931IE::Cause:
	    {
		ISDNQ931IE* ie = new ISDNQ931IE(type);
		if (SignallingUtils::decodeCause(
		    static_cast<SignallingComponent*>(m_settings->m_dbg),
		    *ie,ieData,ieLen,ie->c_str(),false))
		    return ie;
		TelEngine::destruct(ie);
		return 0;
	    }
	default: ;
    }
    // Unknown or unhandled IE
    // Check bits 4-7: If 0: the value MUST be a known one (See Q.931, Table 4-3, Note 5)
    if ((data[0] >> 4) == 0) {
	Debug(m_settings->m_dbg,DebugMild,
	    "Found unknown mandatory IE: %u [%p]",type,m_msg);
	m_msg->setUnknownMandatory();
    }
    ISDNQ931IE* ie = new ISDNQ931IE(type);
    SignallingUtils::dumpData(0,*ie,"dumped-data",ieData,ieLen);
    return ie;
}

// Check Shift IE. Change current codeset
void Q931Parser::shiftCodeset(const ISDNQ931IE* ie)
{
    bool locking = ie->getBoolValue(YSTRING("lock"),false);
    int value = ie->getIntValue(YSTRING("codeset"),0);
    XDebug(m_settings->m_dbg,DebugAll,
	"Process %s shift with codeset %u [%p]",
	locking?"locking":"non locking",value,m_msg);
    // Values 1,2,3 are reserved
    if (value && value < 4) {
	Debug(m_settings->m_dbg,DebugNote,
	    "Ignoring shift with reserved codeset [%p]",m_msg);
	return;
    }
    // Non locking shift
    if (!locking) {
	DDebug(m_settings->m_dbg,DebugNote,
	    "Non locking shift. Set active codeset to %u [%p]",
	    value,m_msg);
	m_activeCodeset = value;
	return;
    }
    // Locking shift. MUST not be lower then the current one
    if (value < m_codeset) {
	Debug(m_settings->m_dbg,DebugNote,
	    "Ignoring locking shift with lower value %u then the current one %u [%p]",
	    value,m_codeset,m_msg);
	return;
    }
    m_activeCodeset = m_codeset = value;
    DDebug(m_settings->m_dbg,DebugNote,
	"Locking shift. Codeset set to %u [%p]",m_codeset,m_msg);
}

// Parse a single fixed length IE
ISDNQ931IE* Q931Parser::getFixedIE(u_int8_t data)
{
    // Type1: bits 7-4 define the IE type. Bits 3-0 contain the value
    // Type2: bits 7-4 are 1010. The type is the whole byte
    u_int16_t type = data & 0xf0;
    if (type == 0xa0)
	type = data;
    type |= (u_int16_t)m_activeCodeset << 8;
    ISDNQ931IE* ie = new ISDNQ931IE(type);
    switch (type) {
	// Type 1
	case ISDNQ931IE::Shift:
	    s_ie_ieFixed[0].addBoolParam(ie,data,true);
	    s_ie_ieFixed[1].addIntParam(ie,data);
	    break;
	case ISDNQ931IE::Congestion:
	    s_ie_ieFixed[2].addIntParam(ie,data);
	    break;
	case ISDNQ931IE::Repeat:
	    s_ie_ieFixed[3].addIntParam(ie,data);
	    break;
	// Type 2
	case ISDNQ931IE::MoreData:
	case ISDNQ931IE::SendComplete:
	    break;
	default:
	    SignallingUtils::dumpData(0,*ie,"Unknown fixed IE",&data,1);
    }
    return ie;
}

// Q.931 4.5.5
ISDNQ931IE* Q931Parser::decodeBearerCaps(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
#define CHECK_INDEX(idx) {if (idx >= len) return errorParseIE(ie,len ? s_errorWrongData : s_errorNoData,0,0);}
    CHECK_INDEX(0)
    // Byte 0: Coding standard (bit 5,6), Information transfer capability (bit 0-4)
    // Translate transfer cap 0x08 to 0x10
    if (!checkCoding(data[0],0,ie))                       // Check coding standard (CCITT: 0)
	return errorParseIE(ie,s_errorUnsuppCoding,data,len);
    s_ie_ieBearerCaps[0].addIntParam(ie,data[0]);
    if (m_settings->flag(ISDNQ931::Translate31kAudio)) {
	NamedString* ns = ie->getParam(s_ie_ieBearerCaps[0].name);
	if (ns && *ns == lookup(0x08,s_ie_ieBearerCaps[0].values))
	    *ns = lookup(0x10,s_ie_ieBearerCaps[0].values);
    }
    // End of data ?
    CHECK_INDEX(1)
    // Byte 1: Transfer mode (bit 5,6), Transfer rate (bit 0-4)
    s_ie_ieBearerCaps[1].addIntParam(ie,data[1]);         // Transfer mode
    s_ie_ieBearerCaps[2].addIntParam(ie,data[1]);         // Transfer rate
    u_int8_t crt = 2;
    // Figure 4.11 Note 1: Next byte is the rate multiplier if the transfer rate is 'multirate' (0x18)
    if ((data[1] & 0x1f) == 0x18) {
	CHECK_INDEX(2)
	s_ie_ieBearerCaps[3].addIntParam(ie,data[2]);
	crt = 3;
    }
    // Get user information layer data
    u_int8_t crtLayer = 0;
    while (true) {
	// End of data ?
	if (crt >= len)
	    return ie;
	// Get and check layer (must be greater than the current one)
	u_int8_t layer = (data[crt] & 0x60) >> 5;
	if (layer <= crtLayer || layer > 3)
	    return errorParseIE(ie,s_errorWrongData,data + crt,len - crt);
	crtLayer = layer;
	// Process layer information
	switch (crtLayer) {
	    case 1:
		decodeLayer1(ie,data,len,crt,s_ie_ieBearerCaps,4);
		continue;
	    case 2:
		decodeLayer2(ie,data,len,crt,s_ie_ieBearerCaps,6);
		continue;
	    case 3:
		decodeLayer3(ie,data,len,crt,s_ie_ieBearerCaps,7);
	}
	break;
    }
    // Dump any remaining data
    if (crt < len)
	SignallingUtils::dumpData(0,*ie,"garbage",data+crt,len-crt);
    return ie;
#undef CHECK_INDEX
}

// Q.931 4.5.6
ISDNQ931IE* Q931Parser::decodeCallIdentity(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieCallIdentity[0].dumpData(ie,data,len);
    return ie;
}

// Q.931 4.5.7
ISDNQ931IE* Q931Parser::decodeCallState(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    if (!checkCoding(data[0],0,ie))                            // Check coding standard (CCITT: 0)
	return errorParseIE(ie,s_errorUnsuppCoding,data,len);
    s_ie_ieCallState[0].addIntParam(ie,data[0]);
    if (len > 1)
	SignallingUtils::dumpData(0,*ie,"garbage",data + 1,len - 1);
    return ie;
}

// Q.931 4.5.13
ISDNQ931IE* Q931Parser::decodeChannelID(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
#define DUMP_DATA_AND_EXIT {if (crt < len) SignallingUtils::dumpData(0,*ie,"garbage",data+crt,len-crt); return ie;}
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // Byte 0
    // Bit 6 - Interface identifier	   0: implicit 1: identified by the next byte(s)
    // Bit 5 - Interface type		   0: basic 1: other (e.g. primary rate)
    // Bit 3 - Preferred/exclusive channel 0: indicated channel is preferred 1: only indicated channel is acceptable
    // Bit 2 - Identified channel is a D-channel or not
    // Bit 0,1 - Channel selection
    bool briInterface = s_ie_ieChannelID[0].addBoolParam(ie,data[0],true);  // Interface type
    s_ie_ieChannelID[1].addBoolParam(ie,data[0],false);                     // Preferred/Exclusive B channel
    s_ie_ieChannelID[2].addBoolParam(ie,data[0],false);                     // D-channel flag
    // Channel selection
    if (briInterface)
	s_ie_ieChannelID[3].addParam(ie,data[0]);                           // Channel select for BRI interface
    else
	s_ie_ieChannelID[4].addParam(ie,data[0]);                           // Channel select for PRI interface
    // Optional Byte 1: Interface identifier if present
    u_int8_t crt = 1;
    bool interfaceIDExplicit = (data[0] & 0x40) != 0;
    if (interfaceIDExplicit) {
	if (len == 1)
	    return errorParseIE(ie,s_errorWrongData,0,0);
	// Calculate length of the interface ID
	for (; crt < len && !Q931_EXT_FINAL(data[crt]); crt++);
	s_ie_ieChannelID[5].dumpData(ie,data + 1,crt - 1);
	crt++;
    }
    // See Q.931 Figure 4.18, Note 2 and 5. Terminate if it's a BRI interface or the interface is explicitely given
    // If not a BRI interface or the interface is not explicit:
    //   check channel selection. If 1: the channel is indicated by the following bytes = continue
    if (briInterface || interfaceIDExplicit || 1 != (data[0] & 0x03))
	DUMP_DATA_AND_EXIT
    // Optional Byte: Coding standard (bit 5,6), Channel indication (bit 4), Channel type (bit 0-3)
    // Check coding standard (CCITT: 0)
    if (crt >= len)
	return ie;
    if (!checkCoding(data[crt],0,ie))
	return errorParseIE(ie,s_errorUnsuppCoding,data + crt,len - crt);
    bool byNumber = s_ie_ieChannelID[6].addBoolParam(ie,data[crt],true);    // Channel is indicated by number/slot-map
    s_ie_ieChannelID[7].addIntParam(ie,data[crt]);                          // Channel type
    crt++;
    // Optional Byte: Channel number or slot map
    // The rest of the data is a list of channels or the slot map
    if (crt >= len)
	return ie;
    u_int8_t idx = byNumber ? 8 : 9;
    String param;
    for (; crt < len; crt++) {
	String tmp((unsigned int)(data[crt] & s_ie_ieChannelID[idx].mask));
	param.append(tmp,",");
	// Bit 7 is used to end channel numbers. See Q.931 Figure 4.18 Note 3
	if (byNumber && Q931_EXT_FINAL(data[crt])) {
	    crt++;
	    break;
	}
    }
    ie->addParam(s_ie_ieChannelID[idx].name,param);
    DUMP_DATA_AND_EXIT
#undef DUMP_DATA_AND_EXIT
}

// Q.931 4.5.23
ISDNQ931IE* Q931Parser::decodeProgress(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // data[0]: Bits 5,6: coding standard
    //          Bits 0-3: Location
    if (!checkCoding(data[0],0,ie))                              // Check coding standard (CCITT: 0)
	return errorParseIE(ie,s_errorUnsuppCoding,data,len);
    s_ie_ieProgress[0].addIntParam(ie,data[0]);
    // data[1]: Progress indication
    if (len == 1)
	return errorParseIE(ie,s_errorWrongData,0,0);
    s_ie_ieProgress[1].addIntParam(ie,data[1]);
    // Dump any remaining data
    if (len > 2)
	SignallingUtils::dumpData(0,*ie,"garbage",data + 2,len - 2);
    return ie;
}

// Q.931 4.5.21
ISDNQ931IE* Q931Parser::decodeNetFacility(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // data[0]: Length of network identification (0 bytes or at least 2 bytes)
    // If non 0: data[1]: Bits 4-6: Type of network identification. Bits 0-3: Network identification plan
    //           data[2] --> data[crt-1]: Network identification
    // data[crt]: Network specific facilities
    // Start of 'Network specific facilities'
    u_int8_t crt = data[0] + 1;
    // Check if the indicated length is correct
    if (crt >= len)
	return errorParseIE(ie,s_errorWrongData,data,len);
    // Network identification exists
    if (crt > 1) {
	// Mandatory: data[1], data[2]
	if (crt < 3)
	     return errorParseIE(ie,s_errorWrongData,data + 1,1);
	s_ie_ieNetFacility[0].addIntParam(ie,data[1]);
	s_ie_ieNetFacility[1].addIntParam(ie,data[1]);
	s_ie_ieNetFacility[2].dumpDataBit7(ie,data + 2,crt - 2,true);
    }
    // Network specific facilities
    s_ie_ieNetFacility[3].addIntParam(ie,data[crt]);
    // Dump any remaining data
    crt++;
    if (crt < len)
	SignallingUtils::dumpData(0,*ie,"garbage",data + crt,len - crt);
    return ie;
}

// Q.931 4.5.22
ISDNQ931IE* Q931Parser::decodeNotification(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieNotification[0].addIntParam(ie,data[0]);
    // Dump any remaining data
    if (len > 1)
	SignallingUtils::dumpData(0,*ie,"garbage",data + 1,len - 1);
    return ie;
}

// Q.931 4.5.15
ISDNQ931IE* Q931Parser::decodeDateTime(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
#define DATETIME_SET(crt) \
    if (crt >= len) return errorParseIE(ie,s_errorWrongData,0,0);\
    s_ie_ieDateTime[crt].addIntParam(ie,data[crt]);
#define DATETIME_SET_OPT(crt) \
    if (crt >= len) return ie;\
    s_ie_ieDateTime[crt].addIntParam(ie,data[crt]); \
    crt++;
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    DATETIME_SET(0)
    DATETIME_SET(1)
    DATETIME_SET(2)
    u_int8_t crt = 3;
    DATETIME_SET_OPT(crt)
    DATETIME_SET_OPT(crt)
    DATETIME_SET_OPT(crt)
    // Dump any remaining data
    if (crt < len)
	SignallingUtils::dumpData(0,*ie,"garbage",data + crt,len - crt);
    return ie;
#undef DATETIME_SET
#undef DATETIME_SET_OPT
}

// Q.931 4.5.16
ISDNQ931IE* Q931Parser::decodeDisplay(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // Check charset
    if (0 != (data[0] & 0x80)) {
	s_ie_ieDisplay[0].addIntParam(ie,data[0]);
	data++;
	len--;
    }
    s_ie_ieDisplay[1].dumpDataBit7(ie,data,len,false);
    return ie;
}

// Q.931 4.5.18
ISDNQ931IE* Q931Parser::decodeKeypad(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieKeypad[0].dumpDataBit7(ie,data,len,false);
    return ie;
}

// Q.931 4.5.28
ISDNQ931IE* Q931Parser::decodeSignal(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieSignal[0].addIntParam(ie,data[0]);
    // Dump any remaining data
    if (len > 1)
	SignallingUtils::dumpData(0,*ie,"garbage",data + 1,len - 1);
    return ie;
}

// Q.931 4.5.10
ISDNQ931IE* Q931Parser::decodeCallingNo(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // Byte 0: Type of number (bit 4-6), Numbering plan (bit 0-3)
    // Number type
    s_ie_ieNumber[0].addParam(ie,data[0]);                // Type of number
    switch (data[0] & 0x70) {                             // Numbering plan (applicable only if type is 0,1,2,4)
	case 0x00: case 0x10: case 0x20: case 0x40:
	    s_ie_ieNumber[1].addParam(ie,data[0]);
    }
    // End of data ?
    if (len == 1)
	return ie;
    // Optional Byte 1: Presentation indicator (bit 5,6), Screening (bit 0,1)
    // Presentation exists ?
    u_int8_t crt = Q931_EXT_FINAL(data[0]) ? 1 : 2;
    if (crt == 2) {
	s_ie_ieNumber[2].addParam(ie,data[1]);
	s_ie_ieNumber[3].addParam(ie,data[1]);
    }
    // Rest of data: The number
    if (crt < len)
	s_ie_ieNumber[4].dumpDataBit7(ie,data + crt,len - crt,false);
    return ie;
}

// Q.931 4.5.11
ISDNQ931IE* Q931Parser::decodeCallingSubAddr(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieSubAddress[0].addIntParam(ie,data[0]);
    s_ie_ieSubAddress[1].addBoolParam(ie,data[0],false);
    if (len == 1)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieSubAddress[2].dumpData(ie,data + 1,len - 1);
    return ie;
}

// Q.931 4.5.8
ISDNQ931IE* Q931Parser::decodeCalledNo(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // Byte 0: Type of number (bit 4-6), Numbering plan (bit 0-3)
    s_ie_ieNumber[0].addParam(ie,data[0]);                // Type of number
    switch (data[0] & 0x70) {                             // Numbering plan (applicable only if type is 0,1,2,4)
	case 0x00: case 0x10: case 0x20: case 0x40:
	    s_ie_ieNumber[1].addParam(ie,data[0]);
    }
    // Rest of data: The number
    if (len > 1)
	s_ie_ieNumber[4].dumpDataBit7(ie,data + 1,len - 1,false);
    return ie;
}

// Q.931 4.5.9
ISDNQ931IE* Q931Parser::decodeCalledSubAddr(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieSubAddress[0].addIntParam(ie,data[0]);
    s_ie_ieSubAddress[1].addBoolParam(ie,data[0],false);
    if (len == 1)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieSubAddress[2].dumpData(ie,data + 1,len - 1);
    return ie;
}

// Q.931 4.5.25
ISDNQ931IE* Q931Parser::decodeRestart(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // data[0]: Bits 0-2: Restart class
    s_ie_ieRestart[0].addIntParam(ie,data[0]);
    // Dump any remaining data
    if (len > 1)
	SignallingUtils::dumpData(0,*ie,"garbage",data + 1,len - 1);
    return ie;
}

// Q.931 4.5.26
ISDNQ931IE* Q931Parser::decodeSegmented(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // data[0]: bit 7: First/subsequent segment. bits 0-6: number of segments remaining
    s_ie_ieSegmented[0].addBoolParam(ie,data[0],false);
    s_ie_ieSegmented[1].addIntParam(ie,data[0]);
    // Segmented message type
    if (len == 1)
	return errorParseIE(ie,s_errorWrongData,0,0);
    s_ie_ieSegmented[2].addIntParam(ie,data[1]);
    // Dump any remaining data
    if (len > 2)
	SignallingUtils::dumpData(0,*ie,"garbage",data + 2,len - 2);
    return ie;
}

// Q.931 4.5.29
ISDNQ931IE* Q931Parser::decodeNetTransit(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // data[0]: Bits 4-6: Type of network identification. Bits 0-3: Network identification plan
    s_ie_ieNetTransit[0].addIntParam(ie,data[0]);
    s_ie_ieNetTransit[1].addIntParam(ie,data[0]);
    // Network identification
    if (len == 1)
	return errorParseIE(ie,s_errorNoData,0,0);
    s_ie_ieNetTransit[2].dumpDataBit7(ie,data + 1,len - 1,false);
    return ie;
}

// Q.931 4.5.19
ISDNQ931IE* Q931Parser::decodeLoLayerCompat(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
#define CHECK_INDEX(idx) {if (idx >= len) return errorParseIE(ie,len ? s_errorWrongData : s_errorNoData,0,0);}
    CHECK_INDEX(0)
    // data[0]: Bits 5,6: coding standard. Bits 0-4: transfer capability
    if (!checkCoding(data[0],0,ie))                             // Check coding standard (CCITT: 0)
	return errorParseIE(ie,s_errorUnsuppCoding,data,len);
    s_ie_ieLoLayerCompat[0].addIntParam(ie,data[0]);
    u_int8_t crt = 1;
    // Out-band negotiation is present only if data[0] has bit 7 not set
    if (!Q931_EXT_FINAL(data[0])) {
	CHECK_INDEX(1)
	s_ie_ieLoLayerCompat[1].addBoolParam(ie,data[1],false);
	crt = 2;
    }
    CHECK_INDEX(crt)
    // Transfer mode and transfer rate
    s_ie_ieLoLayerCompat[2].addIntParam(ie,data[1]);
    s_ie_ieLoLayerCompat[3].addIntParam(ie,data[1]);
    crt++;
    // Rate multiplier. Only if transfer rate is 'multirate'
    if ((data[crt-1] & 0x1f) == 0x18) {
	CHECK_INDEX(crt)
	s_ie_ieLoLayerCompat[4].addIntParam(ie,data[1]);
	crt++;
    }
    // Get user information layer data
    u_int8_t crtLayer = 0;
    while (true) {
	// End of data ?
	if (crt >= len)
	    return ie;
	// Get and check layer (must be greater than the current one)
	u_int8_t layer = (data[crt] & 0x60) >> 5;
	if (layer <= crtLayer || layer > 3)
	    return errorParseIE(ie,s_errorWrongData,data + crt,len - crt);
	crtLayer = layer;
	// Process layer information
	switch (crtLayer) {
	    case 1:
		decodeLayer1(ie,data,len,crt,s_ie_ieLoLayerCompat,5);
		continue;
	    case 2:
		decodeLayer2(ie,data,len,crt,s_ie_ieLoLayerCompat,7);
		continue;
	    case 3:
		decodeLayer3(ie,data,len,crt,s_ie_ieLoLayerCompat,10);
	}
	break;
    }
    // Dump any remaining data
    if (crt < len)
	SignallingUtils::dumpData(0,*ie,"garbage",data + crt,len - crt);
    return ie;
#undef CHECK_INDEX
}

// Q.931 4.5.17
ISDNQ931IE* Q931Parser::decodeHiLayerCompat(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // data[0]: Bits 5,6: coding standard
    //          Bits 2-4: interpretation
    //          Bits 0,1: presenttion
    if (!checkCoding(data[0],0,ie))                          // Check coding standard (CCITT: 0)
	return errorParseIE(ie,s_errorUnsuppCoding,data,len);
    s_ie_ieHiLayerCompat[0].addIntParam(ie,data[0]);         // interpretation
    s_ie_ieHiLayerCompat[1].addIntParam(ie,data[0]);         // presentation
    if (len == 1)
	return errorParseIE(ie,s_errorWrongData,0,0);
    u_int8_t crt = 2;
    u_int8_t presIndex = ((data[0] & 0x03) == 0x01) ? 2 : 4;
    // High layer characteristics identification
    s_ie_ieHiLayerCompat[presIndex].addIntParam(ie,data[1]);
    // Extended high layer characteristics identification
    if (!Q931_EXT_FINAL(data[1])) {
	if (len == 2)
	    return errorParseIE(ie,s_errorWrongData,0,0);
	s_ie_ieHiLayerCompat[presIndex+1].addIntParam(ie,data[2]);
	crt = 3;
    }
    // Dump any remaining data
    if (crt < len)
	SignallingUtils::dumpData(0,*ie,"garbage",data + crt,len - crt);
    return ie;
}

// Q.931 4.5.30
ISDNQ931IE* Q931Parser::decodeUserUser(ISDNQ931IE* ie, const u_int8_t* data,
	u_int32_t len)
{
    if (!len)
	return errorParseIE(ie,s_errorNoData,0,0);
    // data[0]: Protocol discriminator
    s_ie_ieUserUser[0].addIntParam(ie,data[0]);
    if (len == 1)
	return errorParseIE(ie,s_errorWrongData,0,0);
    // Remaining data: user information
    s_ie_ieUserUser[1].dumpData(ie,data + 1,len - 1);
    return ie;
}

void Q931Parser::decodeLayer1(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len,
	u_int8_t& crt, const IEParam* ieParam, u_int8_t ieParamIdx)
{
    ieParam[ieParamIdx].addIntParam(ie,data[crt]);
    crt++;
    // Done with layer 1 data ?
    if (Q931_EXT_FINAL(data[crt-1]))
	return;
    // Skip data up to (and including) the first byte with bit 7 set
    u_int8_t skip = skipExt(data,len,crt);
    if (skip)
	ieParam[ieParamIdx+1].dumpData(ie,data + crt - skip,skip);
}

void Q931Parser::decodeLayer2(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len,
	u_int8_t& crt, const IEParam* ieParam, u_int8_t ieParamIdx)
{
#define CHECK_INDEX \
    if (Q931_EXT_FINAL(data[crt-1])) \
	return; \
    if (crt >= len) { \
	errorParseIE(ie,s_errorWrongData,0,0); \
	return; \
    }
    ieParam[ieParamIdx].addIntParam(ie,data[crt]);
    crt++;
    // This is all for bearer capabilities
    if (ie->type() == ISDNQ931IE::BearerCaps)
	return;
    // IE is 'Low layer compatibility'
    // Skip data: see Q.931 Table 4-16 description for octet 6a
    CHECK_INDEX
    ieParam[ieParamIdx+1].addIntParam(ie,data[crt]);
    crt++;
    // This byte should be the window size
    CHECK_INDEX
    ieParam[ieParamIdx+2].addIntParam(ie,data[crt]);
    crt++;
#undef CHECK_INDEX
}

void Q931Parser::decodeLayer3(ISDNQ931IE* ie, const u_int8_t* data, u_int32_t len,
	u_int8_t& crt, const IEParam* ieParam, u_int8_t ieParamIdx)
{
#define CHECK_INDEX \
    if (Q931_EXT_FINAL(data[crt-1])) \
	return; \
    if (crt >= len) { \
	errorParseIE(ie,s_errorWrongData,0,0); \
	return; \
    }
    ieParam[ieParamIdx].addIntParam(ie,data[crt]);
    crt++;
    // This is all for bearer capabilities
    if (ie->type() == ISDNQ931IE::BearerCaps)
	return;
    // IE is 'Low layer compatibility'
    CHECK_INDEX
    // See Q.931 Figure 4-25 Notes 7,8
    bool advance = false;
    switch (data[crt-1] & 0x1f) {
	// x25, iso8208, x223
	case 0x06: case 0x07: case 0x08:
	    ieParam[ieParamIdx+1].addIntParam(ie,data[crt]);
	    advance = true;
	    break;
	// User specified
	case 0x10:
	    ieParam[ieParamIdx+2].addIntParam(ie,data[crt]);
	    break;
	default:
	    ieParam[ieParamIdx+3].addIntParam(ie,data[crt]);
    }
    crt++;
    if (!advance)
	return;
    // Default packet size
    CHECK_INDEX
    ieParam[ieParamIdx+4].addIntParam(ie,data[crt]);
    crt++;
    // Packet window size
    CHECK_INDEX
    ieParam[ieParamIdx+5].addIntParam(ie,data[crt]);
    crt++;
#undef CHECK_INDEX
}

#define CHECK_IE_LENGTH(len,maxlen) \
    if (len > maxlen) { \
	Debug(m_settings->m_dbg,DebugNote, \
	    "Can't encode '%s' IE. Length %lu exceeds maximum allowed %u [%p]", \
	    ie->c_str(),(long unsigned int)len,maxlen,m_msg); \
	return false; \
    }

bool Q931Parser::encodeBearerCaps(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[8] = {(u_int8_t)ie->type(),2,0x80,0x80};
    // 2: Coding standard (bit 5,6) 0:CCITT, Transfer capability (bit 0-4)
    // Translate '3.1khz-audio' (0x10) to 0x08
    data[2] |= s_ie_ieBearerCaps[0].getValue(ie);
    u_int8_t transCap = data[2] & 0x1f;
    if (m_settings->flag(ISDNQ931::Translate31kAudio) && (0x10 == transCap)) {
	transCap = 0x08;
	data[2] = (data[2] & 0xd0) | 0x08;
    }
    // 3: Transfer mode (bit 5,6), Transfer rate (bit 0-4)
    data[3] |= s_ie_ieBearerCaps[1].getValue(ie);
    // Figure 4.11 Note 1: Next byte is the rate multiplier if the transfer
    //  rate is 'multirate' (0x18)
    u_int8_t transRate = s_ie_ieBearerCaps[2].getValue(ie);
    data[3] |= transRate;
    if (transRate == 0x18) {
	data[1] = 3;
	data[4] = 0x80 | s_ie_ieBearerCaps[3].getValue(ie);
    }
    // Check if this all data we'll send with Bearer Capability
    unsigned int layer = 1;
    if (m_settings->flag(ISDNQ931::NoLayer1Caps) ||
	(m_settings->flag(ISDNQ931::URDITransferCapsOnly) &&
	(transCap == 0x08 || transCap == 0x09)))
	layer = 4;
    // User information layer data
    // Bit 7 = 1, Bits 5,6 = layer, Bits 0-4: the value
    // Layer 1 data is at index 4 in s_ie_ieBearerCaps
    // Layer 2 data is at index 6 in s_ie_ieBearerCaps
    // Layer 3 data is at index 7 in s_ie_ieBearerCaps
    for (unsigned int idx = 4; layer < 4; idx++) {
	int tmp = s_ie_ieBearerCaps[idx].getValue(ie,false,-1);
	if (tmp == -1) {
	    DDebug(m_settings->m_dbg,DebugAll,
		"Stop encoding '%s' IE. No user information layer %d protocol [%p]",
		ie->c_str(),layer,m_msg);
	    break;
	}
	data[1]++;
	data[data[1] + 1] = 0x80 | ((u_int8_t)layer << 5) |
	    ((u_int8_t)tmp & s_ie_ieBearerCaps[idx].mask);
	if (layer == 1)
	    layer += 2;
	else
	    layer++;
    }
    CHECK_IE_LENGTH(data[1] + 2,Q931_MAX_BEARERCAPS_LEN)
    buffer.assign(data,data[1] + 2);
    return true;
}

bool Q931Parser::encodeCallState(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[3] = {(u_int8_t)ie->type(),1,0};
    u_int8_t callstate = (u_int8_t)s_ie_ieCallState[0].getValue(ie,false,255);
    if (callstate == 255) {
	const char* name = s_ie_ieCallState[0].name;
	Debug(m_settings->m_dbg,DebugNote,
	    "Can't encode '%s' IE with unknown or missing field %s=%s [%p]",
	    ie->c_str(),name,ie->getValue(name),m_msg);
	return false;
    }
    data[2] |= callstate & s_ie_ieCallState[0].mask;
    buffer.assign(data,sizeof(data));
    return true;
}

bool Q931Parser::encodeChannelID(ISDNQ931IE* ie, DataBlock& buffer)
{
    DataBlock dataBuffer;
    u_int8_t tmp;
    // *** Byte 0
    // Bit 6 - Interface identifier		0: implicit 1: identified by the next byte(s)
    // Bit 5 - Interface type			0: basic 1: other (e.g. primary rate)
    // Bit 3 - Preferred/exclusive channel	0: indicated channel is preferred 1: only indicated channel is acceptable
    // Bit 2 - Identified channel is a D-channel or not
    // Bit 0,1	- Channel selection
    tmp = 0x80;
    String interfaceID = ie->getValue(s_ie_ieChannelID[5].name);
    if (!interfaceID.null()) {
	Debug(m_settings->m_dbg,DebugWarn,
	    "Can't encode '%s' IE. Interface identifier encoding not implemeted [%p]",
	    ie->c_str(),m_msg);
	return false;
	//TODO: tmp |= 0x40;
    }
    // BRI flag is 0 is briInterface is true
    bool briInterface = ie->getBoolValue(s_ie_ieChannelID[0].name);
    if (!briInterface)
	tmp |= s_ie_ieChannelID[0].mask;
    if (ie->getBoolValue(s_ie_ieChannelID[1].name))    // Preferred/Exclusive B channel
	tmp |= s_ie_ieChannelID[1].mask;
    if (ie->getBoolValue(s_ie_ieChannelID[2].name))    // D-channel flag
	tmp |= s_ie_ieChannelID[2].mask;
    // Channel selection
    if (briInterface)
	tmp |= (u_int8_t)s_ie_ieChannelID[3].getValue(ie);
    else
	tmp |= (u_int8_t)s_ie_ieChannelID[4].getValue(ie);
    dataBuffer.assign(&tmp,1);
    // Optional Byte 1: Interface identifier if present
    if (!interfaceID.null()) {
	if (!interfaceID.length() || interfaceID.length() > 254) {
	    Debug(m_settings->m_dbg,DebugNote,
		"Can't encode '%s' IE with incorrect interface identifier length %u [%p]",
		ie->c_str(),interfaceID.length(),m_msg);
	    return false;
	}
	//TODO: Encode interface identifier. Add to dataBuffer
    }
    // See Q.931 Figure 4.18, Note 2 and 5. Terminate if it's a BRI interface or the interface is explicitely given
    // If not a BRI interface or the interface is not explicit:
    //   check channel selection. If 1: the channel is indicated by the following bytes
    if (!(briInterface || !interfaceID.null() || 1 != (tmp & 0x03))) {
	tmp = 0x80;                                    // Coding standard 0: CCITT
	// Channel is indicated by number/slot-map flag is 0 for number
	bool byNumber = ie->getBoolValue(s_ie_ieChannelID[6].name);
	if (!byNumber)
	    tmp |= s_ie_ieChannelID[6].mask;
	tmp |= s_ie_ieChannelID[7].getValue(ie);       // Channel type
	dataBuffer += DataBlock(&tmp,1);
	String s;
	if (byNumber)
	    s = ie->getValue(s_ie_ieChannelID[8].name);
	else
	    s = ie->getValue(s_ie_ieChannelID[9].name);
	ObjList* list = s.split(',',false);
	ObjList* obj = list->skipNull();
	unsigned int count = list->count();
	for (; obj; obj = obj->skipNext(), count--) {
	    tmp = (static_cast<String*>(obj->get()))->toInteger(255);
	    if (tmp == 255)
		continue;
	    // Last octet must have bit 7 set to 1
	    if (count == 1)
		tmp |= 0x80;
	    else
		tmp &= 0x7f;
	    dataBuffer += DataBlock(&tmp,1);
	}
	delete list;
    }
    // Create buffer
    u_int8_t header[2] = {(u_int8_t)ie->type(),(u_int8_t)dataBuffer.length()};
    CHECK_IE_LENGTH(dataBuffer.length() + sizeof(header),Q931_MAX_CHANNELID_LEN)
    buffer.assign(header,sizeof(header));
    buffer += dataBuffer;
    return true;
}

bool Q931Parser::encodeDisplay(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t header[3] = {(u_int8_t)ie->type(),0,0x80};
    u_int8_t headerLen = 2;
    // Check charset
    if (!m_settings->flag(ISDNQ931::NoDisplayCharset)) {
	headerLen++;
	header[1] = 1;
	header[2] |= 0x31;
    }
    // Process display
    String display = ie->getValue(s_ie_ieDisplay[1].name);
    // Check size (the charset will steel a char from display)
    unsigned int maxlen = m_settings->m_maxDisplay - headerLen;
    if (display.length() > maxlen) {
	Debug(m_settings->m_dbg,DebugMild,
	    "Truncating '%s' IE. Size %u greater then %u [%p]",
	    ie->c_str(),display.length(),maxlen,m_msg);
	display = display.substr(0,maxlen);
    }
    header[1] += display.length();
    clearBit7(display.c_str(),display.length());
    // Encode
    CHECK_IE_LENGTH(display.length() + headerLen,m_settings->m_maxDisplay)
    buffer.assign(header,headerLen);
    buffer.append(display);
    return true;
}

bool Q931Parser::encodeCallingNo(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[4] = {(u_int8_t)ie->type(),1,0x80,0x80};
    // Byte 2: Type of number (bit 4-6), Numbering plan (bit 0-3)
    u_int8_t tmp = s_ie_ieNumber[0].getValue(ie);           // Type of number
    data[2] |= tmp;
    switch (tmp) {                                          // Numbering plan (applicable only if type is 0,1,2,4)
	case 0x00: case 0x10: case 0x20: case 0x40:
	    data[2] |= s_ie_ieNumber[1].getValue(ie);
    }
    // Optional: Presentation indicator (bit 5,6), Screening (bit 0,1)
    String s = ie->getValue(s_ie_ieNumber[2].name);
    if (!s.null()) {
	data[1] = 2;                // Set length
	data[2] &= 0x7f;            // Clear bit 7 to signal the presence of the next octet
	data[3] |= s_ie_ieNumber[2].getValue(ie);
	data[3] |= s_ie_ieNumber[3].getValue(ie);
    }
    // Rest of data: The number
    String number = ie->getValue(s_ie_ieNumber[4].name);
    clearBit7(number.c_str(),number.length());
    u_int8_t dataLen = data[1] + 2;
    CHECK_IE_LENGTH(number.length() + dataLen,Q931_MAX_CALLINGNO_LEN)
    data[1] += number.length();
    buffer.assign(data,dataLen);
    buffer += number;
    return true;
}

bool Q931Parser::encodeCalledNo(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[3] = {(u_int8_t)ie->type(),1,0x80};
    // Byte 2: Type of number (bit 4-6), Numbering plan (bit 0-3)
    u_int8_t tmp = s_ie_ieNumber[0].getValue(ie);           // Type of number
    data[2] |= tmp;
    switch (tmp) {                                          // Numbering plan (applicable only if type is 0,1,2,4)
	case 0x00: case 0x10: case 0x20: case 0x40:
	    data[2] |= s_ie_ieNumber[1].getValue(ie);
    }
    // Rest of data: The number
    String number = ie->getValue(s_ie_ieNumber[4].name);
    clearBit7(number.c_str(),number.length());
    CHECK_IE_LENGTH(number.length() + sizeof(data),Q931_MAX_CALLEDNO_LEN)
    data[1] += number.length();
    buffer.assign(data,sizeof(data));
    buffer += number;
    return true;
}

bool Q931Parser::encodeProgress(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[4] = {(u_int8_t)ie->type(),2,0x80,0x80};
    // data[2]: Bits 5,6: coding standard
    //          Bits 0-3: Location
    // Coding standard (0: CCITT). If no location, set it to 0x01: "LPN"
    data[2] |= s_ie_ieProgress[0].getValue(ie,true,0x01);
    // data[3]: Progress indicatior
    data[3] |= s_ie_ieProgress[1].getValue(ie);
    buffer.assign(data,sizeof(data));
    return true;
}

bool Q931Parser::encodeNotification(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[3] = {(u_int8_t)ie->type(),1,0x80};
    data[2] |= s_ie_ieNotification[0].getValue(ie,true,0xff);
    buffer.assign(data,sizeof(data));
    return true;
}

bool Q931Parser::encodeKeypad(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[2] = {(u_int8_t)ie->type(),0};
    // Process keypad
    String keypad = ie->getValue(s_ie_ieKeypad[0].name);
    CHECK_IE_LENGTH(keypad.length() + sizeof(data),Q931_MAX_KEYPAD_LEN)
    data[1] = keypad.length();
    clearBit7(keypad.c_str(),keypad.length());
    // Encode
    buffer.assign(data,sizeof(data));
    buffer.append(keypad);
    return true;
}

bool Q931Parser::encodeSignal(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[3] = {(u_int8_t)ie->type(),1,0};
    data[2] = s_ie_ieSignal[0].getValue(ie,true,0xff);
    buffer.assign(data,sizeof(data));
    return true;
}

bool Q931Parser::encodeRestart(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[3] = {(u_int8_t)ie->type(),1,0x80};
    data[2] |= s_ie_ieRestart[0].getValue(ie,true,0xff);
    buffer.assign(data,sizeof(data));
    return true;
}

bool Q931Parser::encodeSendComplete(ISDNQ931IE* ie, DataBlock& buffer)
{
    u_int8_t data[1] = {(u_int8_t)ie->type()};
    buffer.assign(data,sizeof(data));
    return true;
}

bool Q931Parser::encodeHighLayerCap(ISDNQ931IE* ie, DataBlock& buffer)
{
    //        **coding standard **
    //octet 1:information element identifier 7d
    //      2:the length of contents
    //      3:bit -8 extension set to 1
    //            -7-6 coding standad
    //            -5-4-3 interpretation
    //            -2-1 presentation method of protocol profile
    //      4:bit -8 extension set to 0
    //            -7-1 high layer caracteristics identification

    // TODO: implement it!
    u_int8_t tmp[4];
    tmp[0]=0x7d; tmp[1]=0x02; tmp[2]=0x91; tmp[3]=0x81;
    buffer.assign(tmp,sizeof(tmp));
    return true;
}

bool Q931Parser::encodeUserUser(ISDNQ931IE* ie, DataBlock& buffer)
{
    // TODO: implement it!
    u_int8_t tmp[10];
    tmp[0]=0x7e;tmp[1]=0x08;tmp[2]=0x04;tmp[3]=0x30;tmp[4]=0x39;
    tmp[5]=0x32;tmp[6]=0x21;tmp[7]=0x30;tmp[8]=0x39;tmp[9]=0x32;
    buffer.assign(tmp,sizeof(tmp));
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
