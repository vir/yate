/**
 * uart.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Modem
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

#include "yatemodem.h"

using namespace TelEngine;

// ETSI EN 300 659-1: 5.2
// Channel seizure signal: block of 300 continuous bits of alternating 0 and 1
#define ETSI_CHANNEL_SEIZURE_1 0x55            // 01010101
#define ETSI_CHANNEL_SEIZURE_2 0xaa            // 10101010

// Convert a buffer to a short
inline short net2short(unsigned char* buffer)
{
    return (buffer[0] << 8) | buffer[1];
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

TokenDict ETSIModem::s_msg[] = {
    {"CallSetup", ETSIModem::MsgCallSetup},
    {"MWI",       ETSIModem::MsgMWI},
    {"Charge",    ETSIModem::MsgCharge},
    {"SMS",       ETSIModem::MsgSMS},
    {0,0}
};

#define MAKE_NAME(x) { #x, ETSIModem::x }
TokenDict ETSIModem::s_msgParams[] = {
    MAKE_NAME(DateTime),
    MAKE_NAME(CallerId),
    MAKE_NAME(CalledId),
    MAKE_NAME(CallerIdReason),
    MAKE_NAME(CallerName),
    MAKE_NAME(CallerNameReason),
    MAKE_NAME(VisualIndicator),
    MAKE_NAME(MessageId),
    MAKE_NAME(LastMsgCLI),
    MAKE_NAME(CompDateTime),
    MAKE_NAME(CompCallerId),
    MAKE_NAME(CallType),
    MAKE_NAME(FirstCalledId),
    MAKE_NAME(MWICount),
    MAKE_NAME(FwdCallType),
    MAKE_NAME(CallerType),
    MAKE_NAME(RedirNumber),
    MAKE_NAME(Charge),
    MAKE_NAME(AdditionalCharge),
    MAKE_NAME(Duration),
    MAKE_NAME(NetworkID),
    MAKE_NAME(CarrierId),
    MAKE_NAME(Display),
    MAKE_NAME(ServiceInfo),
    MAKE_NAME(Extension),
    MAKE_NAME(SelectFunction),
    {0,0}
};
#undef MAKE_NAME

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
    XDebug(this,DebugAll,"idleRecvByte(%u,0x%02x,'%c') ETSI state=%s [%p]",
	data,data,(data>=32)?(char)data:' ',lookup(m_state,s_etsiState),this);

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
    XDebug(this,DebugAll,"recvByte(%u,0x%02x,'%c') ETSI state=%s [%p]",
	data,data,(data>=32)?(char)data:' ',lookup(m_state,s_etsiState),this);

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
	    DDebug(this,DebugNote,"Can't process data in state %s [%p]",
		lookup(m_state,s_etsiState),this);
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
		    tmp = String::boolText(*pdata);
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

// Append a buffer to a data block
inline void addData(DataBlock& dest, void* buf, unsigned int len)
{
    DataBlock tmp(buf,len,false);
    dest += tmp;
    tmp.clear(false);
}

// Append a parameter to a buffer
// Truncate it or set error if fail is true and parameter length exceeds maxLen
int appendParam(ObjList& msg, NamedList& params, const char* name,
	unsigned char value, unsigned char maxLen, bool fail)
{
    String tmp = params.getValue(name);
    if (!tmp)
	return 0;
    unsigned char len = tmp.length() <= maxLen ? tmp.length() : maxLen;
    if (len != tmp.length() && fail) {
	params.setParam("error",String(name) + String("-too-long"));
	return -1;
    }
    DataBlock* data = new DataBlock;
    unsigned char a[2] = {value,len};
    addData(*data,a,sizeof(a));
    addData(*data,(void*)tmp.c_str(),len);
    msg.append(data);
    return 1;
}

// Append a parameter to a buffer from a list or dictionary
void appendParam(ObjList& msg, NamedList& params, const char* name,
	unsigned char value, TokenDict* dict, unsigned char defValue)
{
    unsigned char a[3] = {value,1};
    a[2] = lookup(params.getValue(name),dict,defValue);
    msg.append(new DataBlock(a,sizeof(a)));
}

// Create a buffer containing the byte representation of a message to be sent
//  and another one with the header
bool ETSIModem::createMsg(NamedList& params, DataBlock& data, DataBlock*& header)
{
    int type = lookup(params,s_msg);
    switch (type) {
	case MsgCallSetup:
	    break;
	case MsgMWI:
	case MsgCharge:
	case MsgSMS:
	    Debug(this,DebugStub,"Create message '%s' not implemented [%p]",params.c_str(),this);
	    return false;
	default:
	    Debug(this,DebugNote,"Can't create unknown message '%s' [%p]",params.c_str(),this);
	    return false;
    }

    ObjList msg;
    bool fail = !params.getBoolValue("force-send",true);

    bool datetime = params.getBoolValue("datetime",true);
    if (datetime) {
	// TODO: set date and time
    }

    // ETSI EN 300 659-3 - 5.4.2: Max caller id 20
    int res = appendParam(msg,params,"caller",CallerId,20,fail);
    if (res == -1)
	return false;
    // Default caller absence: 0x4f: unavailable
    if (!res)
	appendParam(msg,params,"callerpres",CallerIdReason,s_dict_callerAbsence,0x4f);

    // ETSI EN 300 659-3 - 5.4.5: Max caller name 50
    res = appendParam(msg,params,"callername",CallerName,50,fail);
    if (res == -1)
	return false;
    if (!res)
	appendParam(msg,params,"callerpres",CallerNameReason,s_dict_callerAbsence,0x4f);

    // Build message
    unsigned char len = 0;

    unsigned char hdr[2] = {type};
    data.assign(&hdr,sizeof(hdr));

    for (ObjList* o = msg.skipNull(); o; o = o->skipNext()) {
	DataBlock* msgParam = static_cast<DataBlock*>(o->get());
	if (len + msgParam->length() > 255) {
	    if (!fail)
		break;
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
    addData(data,&crcVal,1);
    return true;
}

// Change the state of this ETSI modem
void ETSIModem::changeState(State newState)
{
    if (m_state == newState)
	return;
    XDebug(this,DebugInfo,"ETSI changed state from %s to %s [%p]",
	lookup(m_state,s_etsiState),lookup(newState,s_etsiState),this);
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
    XDebug(this,DebugAll,"recvBit(%c) state=%s [%p]",
	value?'1':'0',lookup(m_state,s_uartState),this);

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
    XDebug(this,DebugAll,"UART changed state from %s to %s [%p]",
	lookup(m_state,s_uartState),lookup(newState,s_uartState),this);
    m_state = newState;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
