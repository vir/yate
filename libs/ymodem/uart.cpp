/**
 * uart.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Modem
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

#include "yatemodem.h"

#include <string.h>

using namespace TelEngine;

// ETSI EN 300 659-1: 5.2
// Channel seizure signal: block of 300 continuous bits of alternating 0 and 1
// Use both values to detect the begining of an ETSI defined message:
//  the modem might loose the first bits
#define ETSI_CHANNEL_SEIZURE_1 0x55            // 01010101
#define ETSI_CHANNEL_SEIZURE_2 0xaa            // 10101010


// Convert a buffer to a short
static inline short net2short(unsigned char* buffer)
{
    return (buffer[0] << 8) | buffer[1];
}

// Get date and time from system of received param
// dt: month,day,hour,minute;
// Return false if invalid
static bool getDateTime(unsigned char dt[4], String* src = 0, char sep = ':')
{
    static int minDt[4] = {1,1,0,0};
    static int maxDt[4] = {12,31,23,59};

    if (!src) {
	// TODO: implement from system time
	return false;
    }

    ObjList* list = src->split(sep);
    int i = 0;
    for (; i < 4; i++) {
	String* s = static_cast<String*>((*list)[i]);
	int tmp = s ? s->toInteger(-1) : -1;
	if (tmp >= minDt[i] && tmp <= maxDt[i])
	    dt[i] = (unsigned char)tmp;
	else
	    i = 5;
    }
    delete list;
    return (i == 4);
}


// ETSI EN 300 659-3 5.4.4 Reason of caller absence
static TokenDict s_dict_callerAbsence[] = {
    {"unavailable", 0x4f},
    {"restricted",  0x50},
    {0,0}
};

// ETSI EN 300 659-3 5.4.8 Message identification
static TokenDict s_dict_mwiStatus[] = {
    {"removed",     0x00},
    {"reference",   0x55},               // Message reference only
    {"added",       0xff},
    {0,0}
};

// ETSI EN 300 659-3 5.4.12 Call type
static TokenDict s_dict_callType[] = {
    {"voice",          0x01},            // Normal (voice) Call
    {"ccbs-ccnr",      0x02},            // CCBS / CCNR
    {"callername",     0x03},            // Calling Name Delivery
    {"return",         0x04},            // Call Return
    {"alarm",          0x05},            // Alarm Call
    {"download",       0x06},            // Download Function
    {"reverse-charge", 0x07},            // Reverse Charging Call
    {"vpn_external",   0x10},            // External Call (VPN)
    {"vpn_internal",   0x11},            // Internal Call (VPN)
    {"monitoring",     0x50},            // Monitoring Call
    {"message",        0x81},            // Message Waiting Call
    {0,0}
};

// ETSI EN 300 659-3 5.4.16 Caller type
static TokenDict s_dict_callerType[] = {
    {"unknown",             0x00},       // Origination unknown or unavailable
    {"voice",               0x01},       // Voice Call
    {"text",                0x02},       // Text Call
    {"vpn",                 0x03},       // VPN (Virtual Private Network)
    {"mobile",              0x04},       // Mobile phone
    {"mobile-vpn",          0x05},       // Mobile phone + VPN
    {"fax",                 0x06},       // Fax Call
    {"video",               0x07},       // Video Call
    {"e-mail",              0x08},       // E-mail Call
    {"operator",            0x09},       // Operator Call
    {"ordinary-subscriber", 0x0a},       // Ordinary calling subscriber
    {"priority-subscriber", 0x0b},       // Calling subscriber with priority
    {"data",                0x0c},       // Data Call
    {"test",                0x0d},       // Test call
    {"telemetric",          0x0e},       // Telemetric Call
    {"payphone",            0x0f},       // Payphone
    {0,0}
};

// ETSI EN 300 659-3 5.4.15 Forwarded call reason
static TokenDict s_dict_ffwdReason[] = {
    {"unknown",             0x00},       // Unavailable or unknown forwarded call type
    {"busy",                0x01},       // Forwarded call on busy
    {"noanswer",            0x02},       // Forwarded call on no reply
    {"unconditional",       0x03},       // Unconditional forwarded call
    {"deflected-alerted",   0x04},       // Deflected call (after alerting)
    {"deflected-immediate", 0x05},       // Deflected call (immediate)
    {"mobile-not-found",    0x06},       // Forwarded call on inability to reach mobile subscriber
    {0,0}
};


/**
 * ETSIModem
 */
#ifdef DEBUG
static TokenDict s_etsiState[] = {
    {"Error",    ETSIModem::StateError},
    {"FSKStart", ETSIModem::WaitFSKStart},
    {"Mark",     ETSIModem::WaitMark},
    {"Msg",      ETSIModem::WaitMsg},
    {"MsgLen",   ETSIModem::WaitMsgLen},
    {"Param",    ETSIModem::WaitParam},
    {"ParamLen", ETSIModem::WaitParamLen},
    {"Data",     ETSIModem::WaitData},
    {"Chksum",   ETSIModem::WaitChksum},
    {0,0}
};
#endif

TokenDict ETSIModem::s_msg[] = {
    {"CallSetup", ETSIModem::MsgCallSetup},
    {"MWI",       ETSIModem::MsgMWI},
    {"Charge",    ETSIModem::MsgCharge},
    {"SMS",       ETSIModem::MsgSMS},
    {0,0}
};

#define MAKE_NAME(x) { #x, ETSIModem::x }
TokenDict ETSIModem::s_msgParams[] = {
    {"datetime",              DateTime},
    {"caller",                CallerId},
    {"called",                CalledId},
    {"callerpres",            CallerIdReason},
    {"callername",            CallerName},
    {"callernamepres",        CallerNameReason},
    {"visualindicator",       VisualIndicator},
    {"message_status",        MessageId},
    {"message_caller",        LastMsgCLI},
    {"service_datetime",      CompDateTime},
    {"networkprovidedcaller", CompCallerId},
    {"calltype",              CallType},
    {"fwd_first",             FirstCalledId},
    {"message_count",         MWICount},
    {"fwd_calltype",          FwdCallType},
    {"callertype",            CallerType},
    {"fwd_last",              RedirNumber},
    {"charge",                Charge},
    {"additionalcharge",      AdditionalCharge},
    {"callduration",          Duration},
    {"netid",                 NetworkID},
    {"carrierid",             CarrierId},
    {"display",               Display},
    {"serviceinfo",           ServiceInfo},
    {"extension",             Extension},
    {"selectfunction",        SelectFunction},
    {0,0}
};

ETSIModem::ETSIModem(const NamedList& params, const char* name)
    : UART(UART::Idle,params,name),
    m_buffer(this),
    m_state(WaitFSKStart),
    m_waitSeizureCount(3),
    m_crtSeizureCount(0)
{
    reset();
}

ETSIModem::~ETSIModem()
{
}

void ETSIModem::reset()
{
    m_buffer.reset();
    m_crtMsg = m_crtParamLen = 0;
    m_chksum = 0;
    m_crtSeizureCount = 0;
    m_state = WaitFSKStart;
    UART::reset();
}

// Process accumulated byte in Idle state
// Return negative to stop, positive to change state to BitStart, 0 to continue
// See ETSI EN 300 659-1 for data transmission
// 1. Channel seizure signal: block of 300 continuous bits of alternating 0 and 1
// 1. Mark (stop bits) signal: 180(+/-25) or 80(+/-25) bits
// 3. Message transmission: START bit / DATA bits / STOP bit
int ETSIModem::idleRecvByte(unsigned char data)
{
#ifdef XDEBUG
    XDebug(this,DebugAll,"idleRecvByte(%u,0x%02x,'%c') ETSI state=%s [%p]",
	data,data,(data>=32)?(char)data:' ',lookup(m_state,s_etsiState),this);
#endif

    switch (m_state) {
	case WaitFSKStart:
	    if (data == ETSI_CHANNEL_SEIZURE_1 || data == ETSI_CHANNEL_SEIZURE_2) {
		m_crtSeizureCount++;
		if (m_crtSeizureCount == m_waitSeizureCount) {
		    DDebug(this,DebugInfo,"Received FSK start pattern [%p]",this);
		    changeState(WaitMark);
		}
	    }
	    else
		m_crtSeizureCount = 0;
	    return 0;
	case WaitMark:
	    if (data != 0xff)
		return 0;
	    DDebug(this,DebugInfo,"Received mark signal. Waiting message [%p]",this);
	    changeState(WaitMsg);
	    return 1;
	default: ;
    }
    return -1;
}

// Push a data byte into this UART. Reset this UART and call decode after validated a received message
// Return false to stop feeding data
bool ETSIModem::recvByte(unsigned char data)
{
#ifdef XDEBUG
    XDebug(this,DebugAll,"recvByte(%u,0x%02x,'%c') ETSI state=%s [%p]",
	data,data,(data>=32)?(char)data:' ',lookup(m_state,s_etsiState),this);
#endif

    switch (m_state) {
	case WaitData:
	    if (!m_crtParamLen) {
		Debug(this,DebugWarn,"Internal: received unexpected parameter data [%p]",this);
		break;
	    }
	    XDebug(this,DebugAll,"Received parameter data %u [%p]",data,this);
	    if (!m_buffer.accumulate(data))
		break;
	    m_chksum += data;
	    m_crtParamLen--;
	    if (!m_crtParamLen)
		changeState(m_buffer.free() ? WaitParam : WaitChksum);
	    return true;
	case WaitParam:
	    NDebug(this,DebugAll,"Received parameter start %u=%s [%p]",
		data,lookup(data,s_msgParams),this);
	    if (!m_buffer.accumulate(data))
		break;
	    m_chksum += data;
	    changeState(WaitParamLen);
	    return true;
	case WaitParamLen:
	    if (!data || data > m_buffer.free()) {
		Debug(this,DebugNote,
		    "Received invalid parameter length %u (buffer=%u free=%u) [%p]",
		    data,m_buffer.buffer().length(),m_buffer.free(),this);
		break;
	    }
	    NDebug(this,DebugAll,"Received parameter length %u [%p]",data,this);
	    if (!m_buffer.accumulate(data))
		break;
	    m_chksum += data;
	    m_crtParamLen = data;
	    changeState(WaitData);
	    return true;
	case WaitMsgLen:
	    if (data < 3) {
		Debug(this,DebugNote,"Received invalid message length %u [%p]",data,this);
		break;
	    }
	    m_buffer.reset(data);
	    m_chksum = m_crtMsg + data;
	    NDebug(this,DebugAll,"Received message length %u [%p]",data,this);
	    changeState(WaitParam);
	    return true;
	case WaitMsg:
	    if (!lookup(data,s_msg))
		return true;
	    m_crtMsg = data;
	    NDebug(this,DebugInfo,"Received message start: %s [%p]",lookup(m_crtMsg,s_msg),this);
	    changeState(WaitMsgLen);
	    return true;
	case WaitChksum:
	    if (data == (256 - (m_chksum & 0xff))) {
		NDebug(this,DebugAll,"Checksum OK for message %s [%p]",
		    lookup(m_crtMsg,s_msg),this);
		return decode((MsgType)m_crtMsg,m_buffer.buffer());
	    }
	    Debug(this,DebugNote,"Checksum failed for message (recv=%u crt=%u) %s [%p]",
		data,m_chksum,lookup(m_crtMsg,s_msg),this);
	    changeState(StateError);
	    return UART::error(UART::EChksum);
	case StateError:
	    return false;
	default:
#ifdef DEBUG
	    DDebug(this,DebugNote,"Can't process data in state %s [%p]",
		lookup(m_state,s_etsiState),this);
#endif
	    return true;
    }
    changeState(StateError);
    return UART::error(UART::EInvalidData);
}

// Set a date time digits string
inline void setDateTime(String& dest, const char* data, unsigned int count)
{
    dest << data[0] << data[1];
    for (unsigned int i = 2; i < count; i += 2)
	dest << ':' << data[i] << data[i+1];
}

// Process (decode) a valid received buffer. Call recvParams() after decoding the message
// Return false to stop processing data
bool ETSIModem::decode(MsgType msg, const DataBlock& buffer)
{
    NamedList params("");
    DDebug(this,DebugAll,"Decoding message %s [%p]",lookup(msg,s_msg),this);

    unsigned char* data = (unsigned char*)buffer.data();
    for (unsigned int i = 0; i < buffer.length();) {
	unsigned char param = data[i++];                                // Param type
	const char* pname = lookup(param,s_msgParams);
	unsigned int len = data[i++];                                   // Param length (non 0)
	unsigned char* pdata = data + i;
	// End of buffer: Force index outside the end of buffer
	if (i < buffer.length())
	    i += data[i-1];
	else
	    i++;
	if (i > buffer.length()) {
	    Debug(this,DebugWarn,"Unexpected end of %s parameter [%p]",pname,this);
	    return UART::error(UART::EInvalidData);
	}

	String tmp;

#define CHECK_LEN(expected) \
	if (len != expected) { \
	    Debug(this,DebugNote,"Invalid len=%u (expected %u) for %s parameter [%p]",len,expected,pname,this); \
	    continue; \
	}
#define SET_PARAM_FROM_DATA(paramname) \
	tmp.assign((char*)pdata,len); \
	params.addParam(paramname,tmp);
#define SET_PARAM_FROM_DICT(paramname,dict) \
	tmp = lookup(*pdata,dict,"unknown"); \
	params.addParam(paramname,tmp);
	// Process parameters
	// References are the sections from ETSI EN 300 659-3
	switch (param) {
	    case CallerId:               // 5.4.2
		SET_PARAM_FROM_DATA("caller")
		break;
	    case CallerName:             // 5.4.5
		SET_PARAM_FROM_DATA("callername")
		break;
	    case CallerIdReason:         // 5.4.4
		CHECK_LEN(1)
		SET_PARAM_FROM_DICT("callerpres",s_dict_callerAbsence)
		break;
	    case CallerNameReason:       // 5.4.6
		CHECK_LEN(1)
		SET_PARAM_FROM_DICT("callernamepres",s_dict_callerAbsence)
		break;
	    case DateTime:               // 5.4.1
		CHECK_LEN(8)
		setDateTime(tmp,(char*)pdata,8);
		params.addParam("datetime",tmp);
		break;
	    case CompDateTime:           // 5.4.10
		if (param == CompDateTime && len != 8 && len != 10) {
		    Debug(this,DebugNote,
			"Invalid len=%u (expected 8 or 10) for %s parameter [%p]",len,pname,this);
		    continue;
		}
		setDateTime(tmp,(char*)pdata,len);
		params.addParam("service_datetime",tmp);
		break;
	    case CalledId:               // 5.4.3
		SET_PARAM_FROM_DATA("called")
		break;
	    case CallType:               // 5.4.12
		CHECK_LEN(1)
		SET_PARAM_FROM_DICT("calltype",s_dict_callType)
		break;
	    case CallerType:             // 5.4.16
		CHECK_LEN(1)
		SET_PARAM_FROM_DICT("originator_type",s_dict_callerType)
		break;
	    case VisualIndicator:        // 5.4.7
		CHECK_LEN(1)
		if (*pdata == 0 || *pdata == 255)
		    tmp = String::boolText(*pdata != 0);
		else
		    tmp = (int)(*pdata);
		params.addParam("visualindicator",tmp);
		break;
	    case MessageId:              // 5.4.8
		CHECK_LEN(3)
		SET_PARAM_FROM_DICT("message_status",s_dict_mwiStatus)
		params.addParam("message_ref",String(net2short(pdata + 1)));
		DDebug(this,DebugInfo,
		    "Decoded %s parameter (status=%s ref=%d) [%p]",
		    pname,tmp.c_str(),net2short(pdata + 1),this);
		continue;
	    case LastMsgCLI:             // 5.4.9
		SET_PARAM_FROM_DATA("message_caller")
		break;
	    case CompCallerId:           // 5.4.11
		SET_PARAM_FROM_DATA("caller_networkprovided")
		break;
	    case FirstCalledId:          // 5.4.13
		SET_PARAM_FROM_DATA("ffwd_first")
		break;
	    case MWICount:               // 5.4.14
		CHECK_LEN(1)
		tmp = (int)(*pdata);
		params.addParam("message_count",tmp);
		break;
	    case FwdCallType:            // 5.4.15
		CHECK_LEN(1)
		SET_PARAM_FROM_DICT("ffwd_reason",s_dict_ffwdReason)
		break;
	    case RedirNumber:            // 5.4.17
		SET_PARAM_FROM_DATA("ffwd_last")
		break;
	    case Charge:                 // 5.4.18
	        Debug(this,DebugStub,"Skipping %s parameter [%p]",pname,this);
		continue;
	    case AdditionalCharge:       // 5.4.19
	        Debug(this,DebugStub,"Skipping %s parameter [%p]",pname,this);
		continue;
	    case Duration:               // 5.4.20
		CHECK_LEN(6)
		setDateTime(tmp,(char*)pdata,6);
		params.addParam("duration",tmp);
		break;
	    case NetworkID:              // 5.4.21
		SET_PARAM_FROM_DATA("netid")
		break;
	    case CarrierId:              // 5.4.22
		SET_PARAM_FROM_DATA("carrierid")
		break;
	    case SelectFunction:         // 5.4.23
	        Debug(this,DebugStub,"Skipping %s parameter [%p]",pname,this);
		continue;
	    case Display:                // 5.4.24
	        Debug(this,DebugStub,"Skipping %s parameter [%p]",pname,this);
		continue;
	    case ServiceInfo:            // 5.4.25
		CHECK_LEN(1)
		if (*pdata > 1)
		    tmp = (int)(*pdata);
		else
		    tmp = *pdata ? "active" : "not-active";
		params.addParam("service_info",tmp);
		break;
	    case Extension:              // 5.4.26
	        Debug(this,DebugStub,"Skipping %s parameter [%p]",pname,this);
		continue;
	}
#undef SET_PARAM_FROM_DATA
#undef SET_PARAM_FROM_DICT
#undef CHECK_LEN

	DDebug(this,DebugAll,"Decoded %s=%s [%p]",pname,tmp.c_str(),this);
    }
    if (recvParams(msg,params))
	return true;
    return UART::error(EStopped);
}

// Append a parameter to a buffer
// Truncate it or set error if fail is true and parameter length exceeds maxLen
// Return: 0 if the parameter is missing
//         -1 if the parameter is too long
//         1 on success
int appendParam(ObjList& msg, NamedList& params, unsigned char value,
	unsigned char maxLen, bool fail)
{
    NamedString* ns = params.getParam(lookup(value,ETSIModem::s_msgParams));
    if (!ns)
	return 0;
    unsigned char len = ns->length();
    if (len > maxLen) {
	if (fail) {
	    params.setParam("error",ns->name() + "-too-long");
	    return -1;
	}
	len = maxLen;
    }
    DataBlock* data = new DataBlock;
    unsigned char a[2] = {value,len};
    FSKModem::addRaw(*data,a,sizeof(a));
    FSKModem::addRaw(*data,(void*)ns->c_str(),len);
    msg.append(data);
    return 1;
}

// Append a parameter to a buffer from a list or dictionary
void appendParam(ObjList& msg, NamedList& params, unsigned char value,
	TokenDict* dict, unsigned char defValue)
{
    unsigned char a[3] = {value,1};
    const char* name = lookup(value,ETSIModem::s_msgParams);
    a[2] = lookup(params.getValue(name),dict,defValue);
    msg.append(new DataBlock(a,sizeof(a)));
}

// Create a buffer containing the byte representation of a message to be sent
//  and another one with the header
bool ETSIModem::createMsg(NamedList& params, DataBlock& data)
{
    int type = lookup(params,s_msg);
    switch (type) {
	case MsgCallSetup:
	    break;
	case MsgMWI:
	case MsgCharge:
	case MsgSMS:
	    Debug(this,DebugStub,"Create message '%s' not implemented [%p]",
		params.c_str(),this);
	    return false;
	default:
	    Debug(this,DebugNote,"Can't create unknown message '%s' [%p]",
		params.c_str(),this);
	    return false;
    }

    ObjList msg;
    bool fail = !params.getBoolValue("force-send",true);

    // DateTime - ETSI EN 300 659-3 - 5.4.1
    String datetime = params.getValue("datetime");
    unsigned char dt[4];
    bool ok = false;
    if (datetime.isBoolean())
	if (datetime.toBoolean())
	    ok = getDateTime(dt);
	else ;
    else
	ok = getDateTime(dt,&datetime);
    if (ok) {
	DataBlock* dtParam = new DataBlock(0,10);
	unsigned char* d = (unsigned char*)dtParam->data();
	d[0] = DateTime;
	d[1] = 8;
	// Set date and time: %.2d%.2d%.2d%.2d month:day:hour:minute
	for (int i = 0, j = 2; i < 4; i++, j += 2) {
	    d[j] = '0' + dt[i] / 10;
	    d[j+1] = '0' + dt[i] % 10;
	}
	msg.append(dtParam);
    }
    else
	DDebug(this,DebugInfo,"Can't set datetime parameter from '%s' [%p]",
	    datetime.c_str(),this);

    // CallerId/CallerIdReason - ETSI EN 300 659-3 - 5.4.2: Max caller id 20
    // Parameter is missing: append reason (default caller absence: 0x4f: unavailable)
    int res = appendParam(msg,params,CallerId,20,fail);
    if (res == -1)
	return false;
    if (!res)
	appendParam(msg,params,CallerIdReason,s_dict_callerAbsence,0x4f);

    // CallerName/CallerNameReason - ETSI EN 300 659-3 - 5.4.5: Max caller name 50
    // Parameter is missing: append reason (default callername absence: 0x4f: unavailable)
    res = appendParam(msg,params,CallerName,50,fail);
    if (res == -1)
	return false;
    if (!res)
	appendParam(msg,params,CallerNameReason,s_dict_callerAbsence,0x4f);

    // Build message
    unsigned char len = 0;
    unsigned char hdr[2] = {type};
    data.assign(&hdr,sizeof(hdr));

    for (ObjList* o = msg.skipNull(); o; o = o->skipNext()) {
	DataBlock* msgParam = static_cast<DataBlock*>(o->get());
	if (len + msgParam->length() > 255) {
	    if (!fail) {
		Debug(this,DebugNote,"Trucating %s message length to %u bytes [%p]",
		    params.c_str(),data.length(),this);
		break;
	    }
	    params.setParam("error","message-too-long");
	    return false;
	}
	len += msgParam->length();
	data += *msgParam;
    }
    if (!len) {
	params.setParam("error","empty-message");
	return false;
    }

    unsigned char* buf = ((unsigned char*)(data.data()));
    buf[1] = len;
    m_chksum = 0;
    for (unsigned int i = 0; i < data.length(); i++)
	m_chksum += buf[i];
    unsigned char crcVal = 256 - (m_chksum & 0xff);
    FSKModem::addRaw(data,&crcVal,1);
    return true;
}

// Change the state of this ETSI modem
void ETSIModem::changeState(State newState)
{
    if (m_state == newState)
	return;
#ifdef XDEBUG
    XDebug(this,DebugInfo,"ETSI changed state from %s to %s [%p]",
	lookup(m_state,s_etsiState),lookup(newState,s_etsiState),this);
#endif
    m_state = newState;
}


/**
 * UART
 */
#ifdef XDEBUG
static TokenDict s_uartState[] = {
    {"Idle",   UART::Idle},
    {"Start",  UART::BitStart},
    {"Data",   UART::BitData},
    {"Parity", UART::BitParity},
    {"Stop",   UART::BitStop},
    {"Error",  UART::UARTError},
    {0,0}
};
#endif

TokenDict UART::s_errors[] = {
    {"framing",      UART::EFraming},
    {"parity",       UART::EParity},
    {"chksum",       UART::EChksum},
    {"invalid-data", UART::EInvalidData},
    {"unknown",      UART::EUnknown},
    {"terminated",   UART::EStopped},
    {"",             UART::ENone},
    {0,0}
};

UART::UART(State state, const NamedList& params, const char* name)
    : m_modem(params,this),
    m_state(Idle),
    m_error(ENone),
    m_parity(0),
    m_expectedParity(false),
    m_accumulator(8)
{
    debugName(name);

    unsigned char dataBits = params.getIntValue("databits",8);
    if (dataBits < 1 || dataBits > 8)
	dataBits = 8;
    m_accumulator.dataBits(dataBits);

    m_parity = params.getIntValue("parity");

    reset(state);
}

void UART::reset(State st)
{
    changeState(st);
    m_error = ENone;
    m_modem.reset();
    m_accumulator.reset();
}

// Push a bit of data into this UART
// Return false to stop feeding data
bool UART::recvBit(bool value)
{
#ifdef XDEBUG
    Debug(this,DebugAll,"recvBit(%c) state=%s [%p]",
	value?'1':'0',lookup(m_state,s_uartState),this);
#endif

    int res = 0;

    switch (m_state) {
	case Idle:
	    res = m_accumulator.accumulate(value);
	    if (res & 0xffffff00)
		return true;
	    res = idleRecvByte((unsigned char)res);
	    if (res < 0)
		return error(EUnknown);
	    if (res)
		changeState(BitStart);
	    break;
	case BitData:
	    res = m_accumulator.accumulate(value);
	    if (res & 0xffffff00)
		return true;
	    if (recvByte((unsigned char)res))
		if (!m_parity)
		    changeState(BitStop);
		else {
		    // TODO: get parity and set the expected one
		    changeState(BitParity);
		}
	    else
		return error(EUnknown);
	    break;
	case BitStart:
	    if (!value)
		changeState(BitData);
	    break;
	case BitStop:
	    if (value)
		changeState(BitStart);
	    else
		return error(EFraming);
	    break;
	case BitParity:
	    if (value == m_expectedParity)
		changeState(BitStop);
	    else
		return error(EParity);
	    break;
	default:
	    return false;
    }
    return true;
}

// Set error state
bool UART::error(Error e)
{
    changeState(UARTError);
    if (m_error == ENone) {
	m_error = e;
	if (m_error != EStopped)
	    Debug(this,DebugNote,"Error detected: %u '%s' [%p]",
		m_error,lookup(m_error,s_errors),this);
    }
    return false;
}

// Change the state of this UART
void UART::changeState(State newState)
{
    if (m_state == newState)
	return;
#ifdef XDEBUG
    Debug(this,DebugAll,"UART changed state from %s to %s [%p]",
	lookup(m_state,s_uartState),lookup(newState,s_uartState),this);
#endif
    m_state = newState;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
