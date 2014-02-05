/**
 * sigtran.cpp
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

#define MAX_UNACK 256
#define AVG_DELAY 100

using namespace TelEngine;

#define MAKE_NAME(x) { #x, SIGTRAN::x }
static const TokenDict s_classes[] = {
    // this list must be kept in synch with the header
    MAKE_NAME(MGMT),
    MAKE_NAME(TRAN),
    MAKE_NAME(SSNM),
    MAKE_NAME(ASPSM),
    MAKE_NAME(ASPTM),
    MAKE_NAME(QPTM),
    MAKE_NAME(MAUP),
    MAKE_NAME(CLMSG),
    MAKE_NAME(COMSG),
    MAKE_NAME(RKM),
    MAKE_NAME(IIM),
    MAKE_NAME(M2PA),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_NAME(x) { #x, SIGTRAN::Mgmt##x }
static const TokenDict s_mgmt_types[] = {
    MAKE_NAME(ERR),
    MAKE_NAME(NTFY),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_NAME(x) { #x, SIGTRAN::Ssnm##x }
static const TokenDict s_ssnm_types[] = {
    MAKE_NAME(DUNA),
    MAKE_NAME(DAVA),
    MAKE_NAME(DAUD),
    MAKE_NAME(SCON),
    MAKE_NAME(DUPU),
    MAKE_NAME(DRST),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_NAME(x) { #x, SIGTRAN::Aspsm##x }
static const TokenDict s_aspsm_types[] = {
    MAKE_NAME(UP),
    MAKE_NAME(DOWN),
    MAKE_NAME(BEAT),
    MAKE_NAME(UP_ACK),
    MAKE_NAME(DOWN_ACK),
    MAKE_NAME(BEAT_ACK),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_NAME(x) { #x, SIGTRAN::Asptm##x }
static const TokenDict s_asptm_types[] = {
    MAKE_NAME(ACTIVE),
    MAKE_NAME(INACTIVE),
    MAKE_NAME(ACTIVE_ACK),
    MAKE_NAME(INACTIVE_ACK),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_NAME(x) { #x, SIGTRAN::Rkm##x }
static const TokenDict s_rkm_types[] = {
    MAKE_NAME(REG_REQ),
    MAKE_NAME(REG_RSP),
    MAKE_NAME(DEREG_REQ),
    MAKE_NAME(DEREG_RSP),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_NAME(x) { #x, SIGTRAN::Iim##x }
static const TokenDict s_iim_types[] = {
    MAKE_NAME(REG_REQ),
    MAKE_NAME(REG_RSP),
    MAKE_NAME(DEREG_REQ),
    MAKE_NAME(DEREG_RSP),
    { 0, 0 }
};
#undef MAKE_NAME

#define MAKE_NAME(x) { #x, SS7M2PA::x }
static TokenDict s_m2pa_types[] = {
    MAKE_NAME(UserData),
    MAKE_NAME(LinkStatus),
    { 0, 0 }
};
#undef MAKE_NAME

const TokenDict* SIGTRAN::classNames()
{
    return s_classes;
}

const char* SIGTRAN::typeName(unsigned char msgClass, unsigned char msgType, const char* defValue)
{
    switch (msgClass) {
	case MGMT:
	    return lookup(msgType,s_mgmt_types,defValue);
	case SSNM:
	    return lookup(msgType,s_ssnm_types,defValue);
	case ASPSM:
	    return lookup(msgType,s_aspsm_types,defValue);
	case ASPTM:
	    return lookup(msgType,s_asptm_types,defValue);
	case RKM:
	    return lookup(msgType,s_rkm_types,defValue);
	case IIM:
	    return lookup(msgType,s_iim_types,defValue);
	case M2PA:
	    return lookup(msgType,s_m2pa_types,defValue);
	default:
	    return defValue;
    }
}

SIGTRAN::SIGTRAN(u_int32_t payload, u_int16_t port)
    : m_trans(0), m_payload(payload), m_defPort(port),
      m_transMutex(false,"SIGTRAN::transport")
{
}

SIGTRAN::~SIGTRAN()
{
    attach(0);
}

// Check if a stream in the transport is connected
bool SIGTRAN::connected(int streamId) const
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    return trans && trans->connected(streamId);
}

// Attach a transport to the SIGTRAN instance
void SIGTRAN::attach(SIGTransport* trans)
{
    Lock lock(m_transMutex);
    if (trans == m_trans)
	return;
    if (!(trans && trans->ref()))
	trans = 0;
    SIGTransport* tmp = m_trans;
    m_trans = trans;
    lock.drop();
    if (tmp) {
	tmp->attach(0);
	tmp->destruct();
    }
    if (trans) {
	trans->attach(this);
	SignallingEngine* engine = SignallingEngine::self();
	if (engine)
	    engine->insert(trans);
	trans->deref();
    }
}

// Transmit a SIGTRAN message over the attached transport
bool SIGTRAN::transmitMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId) const
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    return trans && trans->transmitMSG(msgVersion,msgClass,msgType,msg,streamId);
}

bool SIGTRAN::restart(bool force)
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    if (!trans)
	return false;
    trans->reconnect(force);
    return true;
}

bool SIGTRAN::getSocketParams(const String& params, NamedList& result)
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    if (!trans)
	return false;
    trans->getSocketParams(params,result);
    return true;
}

bool SIGTRAN::hasTransportThread()
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    if (!trans)
	return false;
    return trans->hasThread();
}

void SIGTRAN::stopTransportThread()
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    if (trans)
	trans->stopThread();
}

// Attach or detach an user adaptation layer
void SIGTransport::attach(SIGTRAN* sigtran)
{
    if (m_sigtran != sigtran) {
	m_sigtran = sigtran;
	attached(sigtran != 0);
    }
}

// Retrieve the default port to use
u_int32_t SIGTransport::defPort() const
{
    return m_sigtran ? m_sigtran->defPort() : 0;
}

// Request processing from the adaptation layer
bool SIGTransport::processMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId) const
{
    XDebug(this,DebugAll,"Received message class %s type %s (0x%02X) on stream %d",
	lookup(msgClass,s_classes,"Unknown"),
	SIGTRAN::typeName(msgClass,msgType,"Unknown"),msgType,streamId);
    return alive() && m_sigtran && m_sigtran->processMSG(msgVersion,msgClass,msgType,msg,streamId);
}

void SIGTransport::notifyLayer(SignallingInterface::Notification event)
{
    if (alive() && m_sigtran)
	m_sigtran->notifyLayer(event);
}

// Build the common header and transmit a message to the network
bool SIGTransport::transmitMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId)
{
    if (!alive())
	return false;
    XDebug(this,DebugAll,"Sending message class %s type %s (0x%02X) on stream %d",
	lookup(msgClass,s_classes,"Unknown"),
	SIGTRAN::typeName(msgClass,msgType,"Unknown"),msgType,streamId);

    if (!connected(streamId)) {
	Debug(this,DebugMild,"Cannot send message, stream %d not connected [%p]",
	    streamId,this);
	return false;
    }

    unsigned char hdr[8];
    unsigned int len = 8 + msg.length();
    hdr[0] = msgVersion;
    hdr[1] = 0;
    hdr[2] = msgClass;
    hdr[3] = msgType;
    hdr[4] = 0xff & (len >> 24);
    hdr[5] = 0xff & (len >> 16);
    hdr[6] = 0xff & (len >> 8);
    hdr[7] = 0xff & len;

    DataBlock header(hdr,8,false);
    bool ok = transmitMSG(header,msg,streamId);
    header.clear(false);
    return ok;
}

bool SIGTransport::transportNotify(SIGTransport* newTransport, const SocketAddr& addr)
{
    if (alive() && m_sigtran) {
	return m_sigtran->transportNotify(newTransport,addr);
    }
    TelEngine::destruct(newTransport);
    return false;
}


/**
 * Class SIGAdaptation
 */

SIGAdaptation::SIGAdaptation(const char* name, const NamedList* params,
    u_int32_t payload, u_int16_t port)
    : SignallingComponent(name,params), SIGTRAN(payload,port),
      Mutex(true,"SIGAdaptation"), m_maxRetransmit(1000), m_sendHeartbeat(0),
      m_waitHeartbeatAck(0)
{
    DDebug(this,DebugAll,"Creating SIGTRAN UA [%p]",this);
    for (int i = 0; i < 32;i++)
	m_streamsHB[i] = HeartbeatDisabled;
    if (params) {
	m_waitHeartbeatAck.interval(*params,"wait_hb_ack",500,2000,false);
	m_sendHeartbeat.interval(*params,"send_hb",15000,30000,true);
	// The maximum interval in miliseconds allowed for SCTP to retransmit
	// a lost package
	m_maxRetransmit = params->getIntValue("max_interval_retrans",1000);
    }
}

SIGAdaptation::~SIGAdaptation()
{
    DDebug(this,DebugAll,"Destroying SIGTRAN UA [%p]",this);
}

bool SIGAdaptation::initialize(const NamedList* config)
{
    if (transport())
	return true;
    NamedList params("");
    if (resolveConfig(YSTRING("sig"),params,config) ||
	    resolveConfig(YSTRING("basename"),params,config)) {
	DDebug(this,DebugInfo,"Creating transport for SIGTRAN UA [%p]",this);
	params.addParam("basename",params);
	SIGTransport* tr = YSIGCREATE(SIGTransport,&params);
	if (!tr)
	    return false;
	SIGTRAN::attach(tr);
	if (tr->initialize(&params))
	    return true;
	SIGTRAN::attach(0);
    }
    return false;
}

void SIGAdaptation::notifyLayer(SignallingInterface::Notification status)
{
    Lock myLock(this);
    if (status != SignallingInterface::LinkUp) {
	m_waitHeartbeatAck.stop();
	m_sendHeartbeat.stop();
	for (int i = 0;i < 32;i++) {
	    if (m_streamsHB[i] == HeartbeatDisabled)
		continue;
	    m_streamsHB[i] = HeartbeatEnabled;
	}
	return;
    }
    m_sendHeartbeat.start();
    String params = "rto_max";
    NamedList result("sctp_params");
    if (getSocketParams(params,result)) {
	int rtoMax = result.getIntValue(YSTRING("rto_max"));
	unsigned int maxRetrans = rtoMax + AVG_DELAY;
	if (maxRetrans > m_maxRetransmit) {
	    Debug(this,DebugConf,
		    "%s! Maximum SCTP interval to retransmit a packet is: %d, maximum allowed is: %d ",
		    "The SCTP configuration timers are unreliable",
		    maxRetrans,m_maxRetransmit);
	}
    } else
	Debug(this,DebugNote,"Failed to obtain socket params");
}

// Process common (MGMT, ASPSM, ASPTM) messages
bool SIGAdaptation::processCommonMSG(unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId)
{
    switch (msgClass) {
	case MGMT:
	    return processMgmtMSG(msgType,msg,streamId);
	case ASPSM:
	    if (msgType == AspsmBEAT || msgType == AspsmBEAT_ACK)
		return processHeartbeat(msgType,msg,streamId);
	    return processAspsmMSG(msgType,msg,streamId);
	case ASPTM:
	    return processAsptmMSG(msgType,msg,streamId);
	default:
	    Debug(this,DebugWarn,"Unsupported message class 0x%02X",msgClass);
	    return false;
    }
}

bool SIGAdaptation::processHeartbeat(unsigned char msgType, const DataBlock& msg,
	int streamId)
{
    XDebug(this,DebugAll,"Received %s in stream %d",lookup(msgType,s_aspsm_types),streamId);
    if (msgType == AspsmBEAT)
	return transmitMSG(ASPSM,AspsmBEAT_ACK,msg,streamId);
    if (msgType != AspsmBEAT_ACK || streamId > 32)
	return false;
    Lock myLock(this);
    // Mark the first stream witch waits to receive heartbeat
    // Do not mark the received stream because some implementations may send
    // heartbeat responses only on stream 0.
    for (int i = 0;i < 32;i++) {
	if (m_streamsHB[i] == HeartbeatWaitResponse) {
	    m_streamsHB[i] = HeartbeatEnabled;
	    return true;
	}
    }
    return false;
}

// Advance to next tag in a message
bool SIGAdaptation::nextTag(const DataBlock& data, int& offset, uint16_t& tag, uint16_t& length)
{
    unsigned int offs = (offset < 0) ? 0 : offset;
    unsigned char* ptr = data.data(offs,4);
    if (!ptr)
	return false;
    unsigned int len = ((uint16_t)ptr[2] << 8) | ptr[3];
    if (len < 4)
	return false;
    if (offset >= 0) {
	// Skip over current parameter
	offs += (len + 3) & ~3;
	ptr = data.data(offs,4);
	if (!ptr)
	    return false;
	len = ((uint16_t)ptr[2] << 8) | ptr[3];
	if (len < 4)
	    return false;
    }
    if ((offs + len) > data.length())
	return false;

    offset = offs;
    tag = ((uint16_t)ptr[0] << 8) | ptr[1];
    length = len - 4;
    return true;
}

// Find a specific tag in a message
bool SIGAdaptation::findTag(const DataBlock& data, int& offset, uint16_t tag, uint16_t& length)
{
    int offs = -1;
    uint16_t type = 0;
    uint16_t len = 0;
    while (nextTag(data,offs,type,len)) {
	if (type == tag) {
	    offset = offs;
	    length = len;
	    return true;
	}
    }
    return false;
}

// Get a 32 bit integer parameter
bool SIGAdaptation::getTag(const DataBlock& data, uint16_t tag, uint32_t& value)
{
    int offs = -1;
    uint16_t len = 0;
    if (findTag(data,offs,tag,len) && (4 == len)) {
	value = data.at(offs + 4) << 24 | data.at(offs + 5) << 16 |
	    data.at(offs + 6) << 8 | data.at(offs + 7);
	return true;
    }
    return false;
}

// Get a string parameter
bool SIGAdaptation::getTag(const DataBlock& data, uint16_t tag, String& value)
{
    int offs = -1;
    uint16_t len = 0;
    if (findTag(data,offs,tag,len)) {
	value.assign((char*)data.data(offs + 4),len);
	return true;
    }
    return false;
}

// Get a raw binary parameter
bool SIGAdaptation::getTag(const DataBlock& data, uint16_t tag, DataBlock& value)
{
    int offs = -1;
    uint16_t len = 0;
    if (findTag(data,offs,tag,len)) {
	value.assign(data.data(offs + 4),len);
	return true;
    }
    return false;
}

// Add a 32 bit integer parameter
void SIGAdaptation::addTag(DataBlock& data, uint16_t tag, uint32_t value)
{
    unsigned char buf[8];
    buf[0] = tag >> 8;
    buf[1] = tag & 0xff;
    buf[2] = 0;
    buf[3] = 8;
    buf[4] = (value >> 24) & 0xff;
    buf[5] = (value >> 16) & 0xff;
    buf[6] = (value >> 8) & 0xff;
    buf[7] = value & 0xff;
    DataBlock tmp(buf,8,false);
    data += tmp;
    tmp.clear(false);
}

// Add a string parameter
void SIGAdaptation::addTag(DataBlock& data, uint16_t tag, const String& value)
{
    unsigned int len = value.length() + 4;
    if (len > 32768)
	return;
    unsigned char buf[4];
    buf[0] = tag >> 8;
    buf[1] = tag & 0xff;
    buf[2] = (len >> 8) & 0xff;
    buf[3] = len & 0xff;
    DataBlock tmp(buf,4,false);
    data += tmp;
    data += value;
    tmp.clear(false);
    len = (len & 3);
    if (len) {
	buf[0] = buf[1] = buf[2] = 0;
	tmp.assign(buf,4 - len,false);
	data += tmp;
	tmp.clear(false);
    }
}

// Add a raw binary parameter
void SIGAdaptation::addTag(DataBlock& data, uint16_t tag, const DataBlock& value)
{
    unsigned int len = value.length() + 4;
    if (len > 32768)
	return;
    unsigned char buf[4];
    buf[0] = tag >> 8;
    buf[1] = tag & 0xff;
    buf[2] = (len >> 8) & 0xff;
    buf[3] = len & 0xff;
    DataBlock tmp(buf,4,false);
    data += tmp;
    data += value;
    tmp.clear(false);
    len = (len & 3);
    if (len) {
	buf[0] = buf[1] = buf[2] = 0;
	tmp.assign(buf,4 - len,false);
	data += tmp;
	tmp.clear(false);
    }
}

void SIGAdaptation::timerTick(const Time& when)
{
    if (m_sendHeartbeat.timeout()) {
	m_sendHeartbeat.stop();
	Lock myLock(this);
	DataBlock data;
	for (int i = 0; i < 32; i++) {
	    if (m_streamsHB[i] == HeartbeatDisabled)
		continue;
	    transmitMSG(ASPSM,AspsmBEAT,data,i);
	    m_streamsHB[i] = HeartbeatWaitResponse;
	}
	m_waitHeartbeatAck.start();
    }
    if (m_waitHeartbeatAck.timeout()) {
	m_waitHeartbeatAck.stop();
	Lock myLock(this);
	for (int i = 0;i < 32;i++) {
	    if (m_streamsHB[i] == HeartbeatWaitResponse) {
		// The stream is freezed
		Debug(this,DebugWarn,
		      "Stream %d is freezed! Restarting transport",i);
		restart(true);
		return;
	    }
	}
	m_sendHeartbeat.start();
    }
}

/**
 * Class SIGAdaptClient
 */

#define MAKE_NAME(x) { #x, SIGAdaptClient::x }
static const TokenDict s_clientStates[] = {
    MAKE_NAME(AspDown),
    MAKE_NAME(AspUpRq),
    MAKE_NAME(AspUp),
    MAKE_NAME(AspActRq),
    MAKE_NAME(AspActive),
    { 0, 0 }
};
#undef MAKE_NAME

static const TokenDict s_uaErrors[] = {
    { "Invalid Version",                        SIGAdaptation::InvalidVersion },
    { "Invalid Interface Identifier",           SIGAdaptation::InvalidIID },
    { "Unsupported Message Class",              SIGAdaptation::UnsupportedMessageClass },
    { "Unsupported Message Type",               SIGAdaptation::UnsupportedMessageType },
    { "Unsupported Traffic Handling Mode",      SIGAdaptation::UnsupportedTrafficMode },
    { "Unexpected Message",                     SIGAdaptation::UnexpectedMessage },
    { "Protocol Error",                         SIGAdaptation::ProtocolError },
    { "Unsupported Interface Identifier Type",  SIGAdaptation::UnsupportedIIDType },
    { "Invalid Stream Identifier",              SIGAdaptation::InvalidStreamIdentifier },
    { "Unassigned TEI",                         SIGAdaptation::UnassignedTEI },
    { "Unrecognized SAPI",                      SIGAdaptation::UnrecognizedSAPI },
    { "Invalid TEI, SAPI combination",          SIGAdaptation::InvalidTEISAPI },
    { "Refused - Management Blocking",          SIGAdaptation::ManagementBlocking },
    { "ASP Identifier Required",                SIGAdaptation::ASPIDRequired },
    { "Invalid ASP Identifier",                 SIGAdaptation::InvalidASPID },
    { "ASP Active for Interface Identifier(s)", SIGAdaptation::ASPActiveIID },
    { "Invalid Parameter Value ",               SIGAdaptation::InvalidParameterValue },
    { "Parameter Field Error",                  SIGAdaptation::ParameterFieldError },
    { "Unexpected Parameter",                   SIGAdaptation::UnexpectedParameter },
    { "Destination Status Unknown",             SIGAdaptation::DestinationStatusUnknown },
    { "Invalid Network Appearance",             SIGAdaptation::InvalidNetworkAppearance },
    { "Missing Parameter",                      SIGAdaptation::MissingParameter },
    { "Invalid Routing Context",                SIGAdaptation::InvalidRoutingContext },
    { "No Configured AS for ASP",               SIGAdaptation::NotConfiguredAS },
    { "Subsystem Status Unknown",               SIGAdaptation::SubsystemStatusUnknown },
    { "Invalid loadsharing label",              SIGAdaptation::InvalidLoadsharingLabel },
    { 0, 0 }
    };

static const TokenDict s_trafficModes[] = {
    { "unused",    SIGAdaptation::TrafficUnused },
    { "override",  SIGAdaptation::TrafficOverride },
    { "loadshare", SIGAdaptation::TrafficLoadShare },
    { "broadcast", SIGAdaptation::TrafficBroadcast },
    { 0, 0 }
};

// Helper storage object
typedef GenPointer<SIGAdaptUser> AdaptUserPtr;

// Constructor
SIGAdaptClient::SIGAdaptClient(const char* name, const NamedList* params,
    u_int32_t payload, u_int16_t port)
    : SIGAdaptation(name,params,payload,port),
    m_aspId(-1), m_traffic(TrafficOverride), m_state(AspDown)
{
    if (params) {
#ifdef DEBUG
	String tmp;
	if (debugAt(DebugAll))
	    params->dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugInfo,"SIGAdaptClient(%u,%u) created [%p]%s",
	    payload,port,this,tmp.c_str());
#endif
	m_aspId = params->getIntValue(YSTRING("aspid"),m_aspId);
	m_traffic = (TrafficMode)params->getIntValue(YSTRING("traffic"),s_trafficModes,m_traffic);
    }
    // Enable heartbeat on stream 0; because is unlikely to have a adapt user
    // who uses stream 0
    enableHeartbeat(0);
}

// Attach one user entity to the ASP
void SIGAdaptClient::attach(SIGAdaptUser* user)
{
    if (!user)
	return;
    Lock mylock(this);
    m_users.append(new AdaptUserPtr(user));
    // Enable heartbeat on users stream id
    enableHeartbeat(user->getStreamId());
}

// Detach one user entity from the ASP
void SIGAdaptClient::detach(SIGAdaptUser* user)
{
    if (!user)
	return;
    Lock mylock(this);
    for (ObjList* o = m_users.skipNull(); o; o = o->skipNext()) {
	AdaptUserPtr* p = static_cast<AdaptUserPtr*>(o->get());
	if (*p != user)
	    continue;
	m_users.remove(p,false);
	if (!m_users.count()) {
	    setState(AspDown,false);
	    transmitMSG(ASPSM,AspsmDOWN,DataBlock::empty());
	}
	return;
    }
    // Reset all heartbeat streams
    resetHeartbeat();
    enableHeartbeat(0);
    for (ObjList* o = m_users.skipNull(); o; o = o->skipNext()) {
	AdaptUserPtr* p = static_cast<AdaptUserPtr*>(o->get());
	enableHeartbeat((*p)->getStreamId());
    }
}

// Status notification from transport layer
void SIGAdaptClient::notifyLayer(SignallingInterface::Notification status)
{
    SIGAdaptation::notifyLayer(status);
    switch (status) {
	case SignallingInterface::LinkDown:
	case SignallingInterface::HardwareError:
	    switch (m_state) {
		case AspDown:
		case AspUpRq:
		    break;
		default:
		    setState(AspUpRq);
	    }
	    break;
	case SignallingInterface::LinkUp:
	    if (m_state >= AspUpRq) {
		setState(AspUpRq,false);
		DataBlock data;
		if (m_aspId != -1)
		    addTag(data,0x0011,m_aspId);
		transmitMSG(ASPSM,AspsmUP,data);
	    }
	    break;
	default:
	    return;
    }
}
// Request activation of the ASP
bool SIGAdaptClient::activate()
{
    Lock mylock(this);
    if (m_state >= AspActRq)
	return true;
    if (!transport())
	return false;
    switch (m_state) {
	case AspUpRq:
	    return true;
	case AspDown:
	    setState(AspUpRq,false);
	    {
		DataBlock data;
		if (m_aspId != -1)
		    addTag(data,0x0011,m_aspId);
		mylock.drop();
		transmitMSG(ASPSM,AspsmUP,data);
		return true;
	    }
	case AspUp:
	    setState(AspActRq,false);
	    {
		DataBlock data;
		if (m_traffic != TrafficUnused)
		    addTag(data,0x000b,m_traffic);
		mylock.drop();
		return transmitMSG(ASPTM,AsptmACTIVE,data,1);
	    }
	default:
	    return false;
    }
}

// Change the state of the ASP
void SIGAdaptClient::setState(AspState state, bool notify)
{
    Lock mylock(this);
    if (state == m_state)
	return;
    Debug(this,DebugAll,"ASP state change: %s -> %s [%p]",
	lookup(m_state,s_clientStates,"?"),lookup(state,s_clientStates,"?"),this);
    bool up = aspUp();
    bool act = aspActive();
    m_state = state;
    if (!notify)
	return;
    if (act != aspActive())
	activeChange(aspActive());
    else if (aspUp() && !up) {
	setState(AspActRq,false);
	DataBlock data;
	if (m_traffic != TrafficUnused)
	    addTag(data,0x000b,m_traffic);
	transmitMSG(ASPTM,AsptmACTIVE,data,1);
    }
}

// Notification of activity state change
void SIGAdaptClient::activeChange(bool active)
{
    Debug(this,DebugNote,"ASP traffic is now %s [%p]",
	(active ? "active" : "inactive"),this);
    Lock mylock(this);
    for (ObjList* o = m_users.skipNull(); o; o = o->skipNext()) {
	AdaptUserPtr* p = static_cast<AdaptUserPtr*>(o->get());
	(*p)->activeChange(active);
    }
}

// Process common MGMT messages
bool SIGAdaptClient::processMgmtMSG(unsigned char msgType, const DataBlock& msg, int streamId)
{
    switch (msgType) {
	case SIGTRAN::MgmtERR:
	    {
		u_int32_t errCode = 0;
		if (SIGAdaptation::getTag(msg,0x000c,errCode)) {
		    switch (errCode) {
			case 1:
			    Debug(this,DebugWarn,"SG Reported invalid version");
			    setState(AspDown);
			    return true;
			case 5:
			    Debug(this,DebugWarn,"SG Reported invalid traffic mode %s",
				lookup(m_traffic,s_trafficModes,"Unknown"));
			    setState(AspDown);
			    return true;
			case 14:
			    Debug(this,DebugWarn,"SG Reported ASP ID required");
			    setState(AspDown);
			    return true;
			case 15:
			    Debug(this,DebugWarn,"SG Reported invalid ASP id=%d",m_aspId);
			    setState(AspDown);
			    return true;
			default:
			    Debug(this,DebugWarn,"SG reported error %u: %s",errCode,lookup(errCode,s_uaErrors,"Unknown"));
			    return true;
		    }
		}
	    }
	    break;
	case SIGTRAN::MgmtNTFY:
	    {
		u_int32_t status = 0;
		if (SIGAdaptation::getTag(msg,0x000d,status)) {
		    const char* our = "";
		    if (m_aspId != -1) {
			our = "Some ";
			u_int32_t aspid = 0;
			if (SIGAdaptation::getTag(msg,0x0011,aspid))
			    our = ((int32_t)aspid == m_aspId) ? "Our " : "Other ";
		    }
		    switch (status >> 16) {
			case 1:
			    Debug(this,DebugInfo,"%sASP State Change: %u",our,status & 0xffff);
			    return true;
			case 2:
			    Debug(this,DebugInfo,"%sASP State Info: %u",our,status & 0xffff);
			    return true;
		    }
		}
	    }
	    break;
    }
    Debug(this,DebugStub,"Please handle ASP message %u class MGMT",msgType);
    return false;
}

// Process common ASPSM messages
bool SIGAdaptClient::processAspsmMSG(unsigned char msgType, const DataBlock& msg, int streamId)
{
    switch (msgType) {
	case AspsmUP_ACK:
	    setState(AspUp);
	    return true;
	case AspsmDOWN_ACK:
	    setState(AspDown);
	    return true;
	case AspsmUP:
	case AspsmDOWN:
	    Debug(this,DebugWarn,"Wrong direction for ASPSM %s ASP message!",
		SIGTRAN::typeName(ASPSM,msgType));
	    return false;
    }
    Debug(this,DebugStub,"Please handle ASP message %u class ASPSM",msgType);
    return false;
}

// Process common ASPTM messages
bool SIGAdaptClient::processAsptmMSG(unsigned char msgType, const DataBlock& msg, int streamId)
{
    switch (msgType) {
	case AsptmACTIVE_ACK:
	    setState(AspActive);
	    return true;
	case AsptmINACTIVE_ACK:
	    if (aspUp())
		setState(AspUp);
	    return true;
	case AsptmACTIVE:
	case AsptmINACTIVE:
	    Debug(this,DebugWarn,"Wrong direction for ASPTM %s ASP message!",
		SIGTRAN::typeName(ASPTM,msgType));
	    return false;
    }
    Debug(this,DebugStub,"Please handle ASP message %u class ASPTM",msgType);
    return false;
}


/**
 * Class SIGAdaptServer
 */

// Process common MGMT messages
bool SIGAdaptServer::processMgmtMSG(unsigned char msgType, const DataBlock& msg, int streamId)
{
    Debug(this,DebugStub,"Please handle SG message %u class MGMT",msgType);
    return false;
}

// Process common ASPSM messages
bool SIGAdaptServer::processAspsmMSG(unsigned char msgType, const DataBlock& msg, int streamId)
{
    switch (msgType) {
	case AspsmUP:
	case AspsmDOWN:
	    break;
	case AspsmUP_ACK:
	case AspsmDOWN_ACK:
	    Debug(this,DebugWarn,"Wrong direction for ASPSM %s SG message!",
		SIGTRAN::typeName(ASPSM,msgType));
	    return false;
    }
    Debug(this,DebugStub,"Please handle SG message %u class ASPSM",msgType);
    return false;
}

// Process common ASPTM messages
bool SIGAdaptServer::processAsptmMSG(unsigned char msgType, const DataBlock& msg, int streamId)
{
    switch (msgType) {
	case AsptmACTIVE:
	case AsptmINACTIVE:
	    break;
	case AsptmACTIVE_ACK:
	case AsptmINACTIVE_ACK:
	    Debug(this,DebugWarn,"Wrong direction for ASPTM %s SG message!",
		SIGTRAN::typeName(ASPTM,msgType));
	    return false;
    }
    Debug(this,DebugStub,"Please handle SG message %u class ASPTM",msgType);
    return false;
}


/**
 * Class SIGAdaptUser
 */

SIGAdaptUser::~SIGAdaptUser()
{
    adaptation(0);
}

// Attach an ASP CLient to this user, detach old client
void SIGAdaptUser::adaptation(SIGAdaptClient* adapt)
{
    if (adapt == m_adaptation)
	return;
    if (m_adaptation) {
	m_adaptation->detach(this);
	TelEngine::destruct(m_adaptation);
    }
    m_adaptation = adapt;
    if (adapt && adapt->ref())
	adapt->attach(this);
}


/**
 * Class SS7M2PA
 */

static TokenDict s_state[] = {
    {"Alignment",           SS7M2PA::Alignment},
    {"ProvingNormal",       SS7M2PA::ProvingNormal},
    {"ProvingEmergency",    SS7M2PA::ProvingEmergency},
    {"Ready",               SS7M2PA::Ready},
    {"ProcessorOutage",     SS7M2PA::ProcessorOutage},
    {"ProcessorRecovered",  SS7M2PA::ProcessorRecovered},
    {"Busy",                SS7M2PA::Busy},
    {"BusyEnded",           SS7M2PA::BusyEnded},
    {"OutOfService",        SS7M2PA::OutOfService},
    {0,0}
};

static const TokenDict s_m2pa_dict_control[] = {
    { "pause",              SS7M2PA::Pause },
    { "resume",             SS7M2PA::Resume },
    { "align",              SS7M2PA::Align },
    { "transport_restart",  SS7M2PA::TransRestart },
    { 0, 0 }
};

SS7M2PA::SS7M2PA(const NamedList& params)
    : SignallingComponent(params.safe("SS7M2PA"),&params,"ss7-m2pa"),
      SIGTRAN(5,3565),
      m_seqNr(0xffffff), m_needToAck(0xffffff), m_lastAck(0xffffff), m_maxQueueSize(MAX_UNACK),
      m_localStatus(OutOfService), m_state(OutOfService),
      m_remoteStatus(OutOfService), m_transportState(Idle), m_connFailCounter(0),
      m_connFailThreshold(0), m_mutex(true,"SS7M2PA"), m_t1(0), m_t2(0), m_t3(0),
      m_t4(0), m_ackTimer(0), m_confTimer(0), m_oosTimer(0),m_waitOosTimer(0),
      m_connFailTimer(0), m_autostart(false), m_sequenced(false), m_dumpMsg(false)

{
    // Alignment ready timer ~45s
    m_t1.interval(params,"t1",45000,50000,false);
    // Not Aligned timer ~5s
    m_t2.interval(params,"t2",5000,5500,false);
    // Aligned timer ~1s
    m_t3.interval(params,"t3",1000,1500,false);
    // Proving timer Normal ~8s, Emergency ~0.5s
    m_t4.interval(params,"t4",500,8000,false);
    // Acknowledge timer ~1s
    m_ackTimer.interval(params,"ack_timer",1000,1100,false);
    // Confirmation timer 1/2 t4
    m_confTimer.interval(params,"conf_timer",50,150,false);
    // Out of service timer
    m_oosTimer.interval(params,"oos_timer",3000,5000,false);
    m_waitOosTimer.interval(params,"wait_oos",500,1000,false);
    m_connFailTimer.interval(params,"conn_test",50000,300000,false);
    m_connFailThreshold = params.getIntValue(YSTRING("conn_threshold"),3);
    m_sequenced = params.getBoolValue(YSTRING("sequenced"),false);
    // Maximum unacknowledged messages, max_unack+1 will force an ACK
    m_maxUnack = params.getIntValue(YSTRING("max_unack"),4);
    if (m_maxUnack > 10)
	m_maxUnack = 10;
    m_maxQueueSize = params.getIntValue(YSTRING("max_queue_size"),MAX_UNACK);
    if (m_maxQueueSize < 16)
	m_maxQueueSize = 16;
    if (m_maxQueueSize > 65356)
	m_maxQueueSize = 65356;
    DDebug(this,DebugAll,"Creating SS7M2PA [%p]",this);
}

SS7M2PA::~SS7M2PA()
{
    Lock lock(m_mutex);
    m_ackList.clear();
    DDebug(this,DebugAll,"Destroying SS7M2PA [%p]",this);
}

void SS7M2PA::destroyed()
{
    stopTransportThread();
    SIGTRAN::attach(0);
    SS7Layer2::destroyed();
}

bool SS7M2PA::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7M2PA::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    m_dumpMsg = config && config->getBoolValue(YSTRING("dumpMsg"),false);
    m_autostart = !config || config->getBoolValue(YSTRING("autostart"),true);
    m_autoEmergency = !config || config->getBoolValue(YSTRING("autoemergency"),true);
    if (config && !transport()) {
	NamedList params("");
	if (resolveConfig(YSTRING("sig"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    params.addParam("basename",params);
	    params.addParam("protocol","ss7");
	    params.addParam("listen-notify","false");
	    SIGTransport* tr = YSIGCREATE(SIGTransport,&params);
	    if (!tr)
		return false;
	    SIGTRAN::attach(tr);
	    if (!tr->initialize(&params))
		SIGTRAN::attach(0);
	    m_sequenced = config->getBoolValue(YSTRING("sequenced"),transport() ?
		transport()->reliable() : false);
	}
    }
    return transport() && control(Resume,const_cast<NamedList*>(config));
}

void SS7M2PA::dumpMsg(u_int8_t version, u_int8_t mClass, u_int8_t type,
    const DataBlock& data, int stream, bool send)
{
    String dump = "SS7M2PA ";
    dump << (send ? "Sending:" : "Received:");
    dump << "\r\n-----";
    String indent = "\r\n  ";
    dump << indent << "Version: " << version;
    dump << "    " << "Message class: " << mClass;
    dump << "    " << "Message type: " << lookup(type,s_m2pa_types,"Unknown");
    dump << indent << "Stream: " << stream;
    if (data.length() >= 8) {
	u_int32_t bsn = (data[1] << 16) | (data[2] << 8) | data[3];
	u_int32_t fsn = (data[5] << 16) | (data[6] << 8) | data[7];
	dump << indent << "FSN : " << fsn << "	BSN: " << bsn;
	if (type == LinkStatus) {
	    u_int32_t status = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
	    dump << indent << "Status: " << lookup(status,s_state);
	}
	else {
	    String hex;
	    hex.hexify((u_int8_t*)data.data() + 8,data.length() - 8,' ');
	    dump << indent << "Data: " << hex;
	}
    }
    dump << "\r\n-----";
    Debug(this,DebugInfo,"%s",dump.c_str());
}

bool SS7M2PA::processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId)
{
    if (msgClass != M2PA) {
	Debug(this,(msg.null() ? DebugInfo : DebugWarn),
	    "Received non M2PA message class %d",msgClass);
	dumpMsg(msgVersion,msgClass,msgType,msg,streamId,false);
	return false;
    }
    if (m_dumpMsg)
	dumpMsg(msgVersion,msgClass,msgType,msg,streamId,false);
    Lock lock(m_mutex);
    if (!operational() && msgType == UserData)
	return false;
    if (!decodeSeq(msg,(u_int8_t)msgType))
	return false;
    DataBlock data(msg);
    data.cut(-8);
    if (!data.length())
	return true;
    if (msgType == LinkStatus)
	return m_sequenced ? processSLinkStatus(data,streamId) : processLinkStatus(data,streamId);
#ifdef DEBUG
    if (streamId != 1)
	Debug(this,DebugNote,"Received data message on Link status stream");
#endif
    lock.drop();
    data.cut(-1); // priority octet
    SS7MSU msu(data);
    return receivedMSU(msu);
}

bool SS7M2PA::nextBsn(u_int32_t bsn) const
{
    u_int32_t n = (0x1000000 + m_seqNr - bsn) & 0xffffff;
    if (n > m_maxQueueSize) {
	Debug(this,DebugWarn,"Maximum number of unacknowledged messages reached!!!");
	return false;
    }
    n = (0x1000000 + bsn - m_lastAck) & 0xffffff;
    return (n != 0) && (n <= m_maxQueueSize);
}

bool SS7M2PA::decodeSeq(const DataBlock& data,u_int8_t msgType)
{
    if (data.length() < 8)
	return false;
    u_int32_t bsn = (data[1] << 16) | (data[2] << 8) | data[3];
    u_int32_t fsn = (data[5] << 16) | (data[6] << 8) | data[7];
    if (msgType == LinkStatus) {
	// Do not check sequence numbers if either end is OutOfService
	if (OutOfService == m_state)
	    return true;
	if (data.length() >= 12) {
	    u_int32_t status = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
	    if (OutOfService == status)
		return true;
	}
	if (fsn != m_needToAck) {
	    Debug(this,DebugWarn,"Received LinkStatus with wrong sequence %d, expected %d in state %s",
		fsn,m_needToAck,lookup(m_localStatus,s_state));
	    abortAlignment("Wrong Sequence number");
	    transmitLS();
	    return false;
	}
	if (bsn == m_lastAck)
	    return true;
	// If we are here means that something went wrong
	abortAlignment("msgType == LinkStatus");
	transmitLS();
	return false;
    }
    // UserData
    bool ok = false;
    if (fsn == getNext(m_needToAck)) {
	m_needToAck = fsn;
	ok = true;
	if (m_confTimer.started()) {
	    if (++m_confCounter >= m_maxUnack) {
		m_confTimer.stop();
		sendAck();
	    }
	}
	else if (m_maxUnack) {
	    m_confCounter = 0;
	    m_confTimer.start();
	}
	else
	    sendAck();
    }
    else if (fsn != m_needToAck) {
	abortAlignment("Received Out of sequence frame");
	transmitLS();
	return false;
    }
    while (nextBsn(bsn) && removeFrame(getNext(m_lastAck)))
	;
    if (bsn != m_lastAck) {
	abortAlignment(String("Received unexpected bsn: ") << bsn);
	transmitLS();
	return false;
    }
    m_lastSeqRx = (m_needToAck & 0x00ffffff) | 0x01000000;
    return ok;
}

void SS7M2PA::timerTick(const Time& when)
{
    SS7Layer2::timerTick(when);
    Lock lock(m_mutex,SignallingEngine::maxLockWait());
    if (!lock.locked())
	return;
    if (m_confTimer.timeout(when.msec())) {
	sendAck(); // Acknowledge last received message before endpoint drops down the link
	m_confTimer.stop();
    }
    if (m_ackTimer.timeout(when.msec())) {
	m_ackTimer.stop();
	if (!transport() || transport()->reliable()) {
	    lock.drop();
	    abortAlignment("Ack timer timeout");
	} else
	    retransData();
    }
    if (m_waitOosTimer.timeout(when.msec())) {
	m_waitOosTimer.stop();
	setLocalStatus(OutOfService);
	transmitLS();
    }
    if (m_connFailTimer.timeout(when.msec())) {
	m_connFailTimer.stop();
	if (m_connFailCounter >= m_connFailThreshold) {
	    Debug(this,DebugMild,
		  "Connection proving failed but transport was not restarted!");
	    restart(true);
	}
	m_connFailCounter = 0;
    }
    if (m_oosTimer.timeout(when.msec())) {
	m_oosTimer.stop();
	if (m_transportState == Established)
	    abortAlignment("Out of service timeout");
	else
	    m_oosTimer.start();
	return;
    }
    if (m_t2.timeout(when.msec())) {
	abortAlignment("T2 timeout");
	setLocalStatus(Alignment);
	transmitLS();
	m_t2.start();
	return;
    }
    if (m_t3.timeout(when.msec())) {
	m_t3.stop();
	abortAlignment("T3 timeout");
	return;
    }
    if (m_t4.started()) {
	if (m_t4.timeout(when.msec())) {
	    m_t4.stop();
	    setLocalStatus(Ready);
	    transmitLS();
	    m_t1.start();
	    return;
	}
	// Retransmit proving state
	if ((when & 0x3f) == 0)
	    transmitLS();
    }
    if (m_t1.timeout(when.msec())) {
	m_t1.stop();
	abortAlignment("T1 timeout");
    }
}

bool SS7M2PA::removeFrame(u_int32_t bsn)
{
    Lock lock(m_mutex);
    for (ObjList* o = m_ackList.skipNull();o;o = o->skipNext()) {
	DataBlock* d = static_cast<DataBlock*>(o->get());
	u_int32_t seq = (d->at(5) << 16) | (d->at(6) << 8) | d->at(7);
	if (bsn != seq)
	    continue;
	m_lastAck = bsn;
	m_ackList.remove(d);
	m_ackTimer.stop();
	return true;
    }
    Debug(this,DebugWarn,"Failed to remove frame %d! Frame is missing!",bsn);
    return false;
}

void SS7M2PA::setLocalStatus(unsigned int status)
{
    if (status == m_localStatus)
	return;
    DDebug(this,DebugInfo,"Local status change %s -> %s [%p]",
	lookup(m_localStatus,s_state),lookup(status,s_state),this);
    if (status == Ready)
	m_ackList.clear();
    m_localStatus = status;
}

void SS7M2PA::setRemoteStatus(unsigned int status)
{
    if (status == m_remoteStatus)
	return;
    DDebug(this,DebugInfo,"Remote status change %s -> %s [%p]",
	lookup(m_remoteStatus,s_state),lookup(status,s_state),this);
    m_remoteStatus = status;
}

bool SS7M2PA::aligned() const
{
    switch (m_localStatus) {
	case ProvingNormal:
	case ProvingEmergency:
	case Ready:
	    switch (m_remoteStatus) {
		case ProvingNormal:
		case ProvingEmergency:
		case Ready:
		    return true;
	    }
    }
    return false;
}

bool SS7M2PA::operational() const
{
    return m_localStatus == Ready && m_remoteStatus == Ready;
}

void SS7M2PA::sendAck()
{
    DataBlock data;
    setHeader(data);
    if (m_dumpMsg)
	dumpMsg(1,M2PA,UserData,data,1,true);
    transmitMSG(1,M2PA,UserData,data,1);
}

unsigned int SS7M2PA::status() const
{
    switch (m_localStatus) {
	case ProvingNormal:
	case ProvingEmergency:
	    return SS7Layer2::OutOfAlignment;
	case Ready:
	    switch (m_remoteStatus) {
		case Ready:
		    return SS7Layer2::NormalAlignment;
		case ProcessorOutage:
		    return SS7Layer2::ProcessorOutage;
		case Busy:
		    return SS7Layer2::Busy;
		case OutOfService:
		    return SS7Layer2::OutOfService;
		default:
		    return SS7Layer2::OutOfAlignment;
	    }
    }
    return SS7Layer2::OutOfService;
}

bool SS7M2PA::control(NamedList& params)
{
    String* ret = params.getParam(YSTRING("completion"));
    const String* oper = params.getParam(YSTRING("operation"));
    const char* cmp = params.getValue(YSTRING("component"));
    int cmd = oper ? oper->toInteger(s_m2pa_dict_control,-1) : -1;
    if (ret) {
	if (oper && (cmd < 0))
	    return false;
	String part = params.getValue(YSTRING("partword"));
	if (cmp) {
	    if (toString() != cmp)
		return false;
	    for (const TokenDict* d = s_m2pa_dict_control; d->token; d++)
		Module::itemComplete(*ret,d->token,part);
	    return true;
	}
	return Module::itemComplete(*ret,toString(),part);
    }
    if (!(cmp && toString() == cmp))
	return false;
    return TelEngine::controlReturn(&params,(cmd >= 0) && control((M2PAOperations)cmd,&params));
}

bool SS7M2PA::control(M2PAOperations oper, NamedList* params)
{
    if (params) {
	m_autostart = params->getBoolValue(YSTRING("autostart"),m_autostart);
	m_autoEmergency = params->getBoolValue(YSTRING("autoemergency"),m_autoEmergency);
	m_maxUnack = params->getIntValue(YSTRING("max_unack"),m_maxUnack);
	if (m_maxUnack > 10)
	    m_maxUnack = 10;
    }
    switch (oper) {
	case Pause:
	    m_state = OutOfService;
	    abortAlignment("Control request pause.");
	    transmitLS();
	    return TelEngine::controlReturn(params,true);
	case Resume:
	    if (aligned() || !m_autostart)
		return TelEngine::controlReturn(params,true);
	case Align:
	{
	    m_state = getEmergency(params) ? ProvingEmergency : ProvingNormal;
	    abortAlignment("Control request align.");
	    return TelEngine::controlReturn(params,true);
	}
	case Status:
	    return TelEngine::controlReturn(params,operational());
	case TransRestart:
	    return TelEngine::controlReturn(params,restart(true));
	default:
	    return TelEngine::controlReturn(params,false);
    }
}

void SS7M2PA::startAlignment(bool emergency)
{
    setLocalStatus(OutOfService);
    transmitLS();
    if (!m_sequenced)
	setLocalStatus(Alignment);
    m_oosTimer.start();
    SS7Layer2::notify();
}

void SS7M2PA::transmitLS(int streamId)
{
    if (m_transportState != Established)
	return;
    if (m_state == OutOfService)
	m_localStatus = OutOfService;
    DataBlock data;
    setHeader(data);
    u_int8_t ms[4];
    ms[2] = ms[1] = ms[0] = 0;
    ms[3] = m_localStatus;
    data.append(ms,4);
    if (m_dumpMsg)
	dumpMsg(1,M2PA, 2,data,streamId,true);
    transmitMSG(1,M2PA, 2, data,streamId);
    XDebug(this,DebugInfo,"Sending LinkStatus %s",lookup(m_localStatus,s_state));
}

void SS7M2PA::setHeader(DataBlock& data)
{
    u_int8_t head[8];
    head[0] = head[4] = 0;
    head[1] = (m_needToAck >> 16) & 0xff;
    head[2] = (m_needToAck >> 8) & 0xff;
    head[3] = m_needToAck & 0xff ;
    head[5] = (m_seqNr >> 16) & 0xff;
    head[6] = (m_seqNr >> 8) & 0xff;
    head[7] = m_seqNr & 0xff ;
    data.append(head,8);
}

void SS7M2PA::abortAlignment(const char* info)
{
    m_connFailCounter++;
    if (!m_connFailTimer.started())
	m_connFailTimer.start();
    else if (m_connFailCounter >= m_connFailThreshold) {
	restart(true);
	m_connFailTimer.stop();
    }
    if (info)
	Debug(this,DebugNote,"Aborting alignment: %s",info);
    setLocalStatus(OutOfService);
    setRemoteStatus(OutOfService);
    m_needToAck = m_lastAck = m_seqNr = 0xffffff;
    m_confTimer.stop();
    m_ackTimer.stop();
    m_oosTimer.stop();
    m_t2.stop();
    m_t3.stop();
    m_t4.stop();
    m_t1.stop();
    if (m_state == ProvingNormal || m_state == ProvingEmergency) {
	startAlignment();
	if (m_sequenced)
	    m_waitOosTimer.start();
    } else
	SS7Layer2::notify();
}

bool SS7M2PA::processLinkStatus(DataBlock& data,int streamId)
{
    if (data.length() < 4)
	return false;
    u_int32_t status = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (m_remoteStatus == status && status != OutOfService)
	return true;

    XDebug(this,DebugAll,"Received link status: %s, local status : %s, requested status %s",
	lookup(status,s_state),lookup(m_localStatus,s_state),lookup(m_state,s_state));
    switch (status) {
	case Alignment:
	    m_oosTimer.stop();
	    if (m_t2.started()) {
		m_t2.stop();
		setLocalStatus(m_state);
		m_t3.start();
		transmitLS();
	    }
	    else if (m_state == ProvingNormal || m_state == ProvingEmergency)
		transmitLS();
	    else
		return false;
	    setRemoteStatus(status);
	    break;
	case ProvingNormal:
	case ProvingEmergency:
	    m_t2.stop();
	    if (m_localStatus != ProvingNormal && m_localStatus != ProvingEmergency &&
		(m_localStatus == Alignment && m_t3.started()))
		return false;
	    if (m_t3.started()) {
		m_t3.stop();
		if (status == ProvingEmergency || m_state == ProvingEmergency)
		    m_t4.fire(Time::msecNow() + (m_t4.interval() / 16));
		else
		    m_t4.start();
	    }
	    else if (m_state == ProvingNormal || m_state == ProvingEmergency) {
		setLocalStatus(status);
		transmitLS();
		if (status == ProvingEmergency || m_state == ProvingEmergency)
		    m_t4.fire(Time::msecNow() + (m_t4.interval() / 16));
		else
		    m_t4.start();
	    }
	    setRemoteStatus(status);
	    break;
	case Ready:
	    if (m_localStatus != Ready) {
		setLocalStatus(Ready);
		transmitLS();
	    }
	    setRemoteStatus(status);
	    m_lastSeqRx = -1;
	    SS7Layer2::notify();
	    m_oosTimer.stop();
	    m_t2.stop();
	    m_t3.stop();
	    m_t4.stop();
	    m_t1.stop();
	    break;
	case ProcessorRecovered:
	    transmitLS();
	    setRemoteStatus(status);
	    break;
	case BusyEnded:
	    setRemoteStatus(Ready);
	    SS7Layer2::notify();
	    break;
	case ProcessorOutage:
	case Busy:
	    setRemoteStatus(status);
	    SS7Layer2::notify();
	    break;
	case OutOfService:
	    m_oosTimer.stop();
	    if (m_localStatus == Ready) {
		abortAlignment("Received : LinkStatus Out of service, local status Ready");
		SS7Layer2::notify();
	    }
	    if ((m_state == ProvingNormal || m_state == ProvingEmergency)) {
		if (m_localStatus == Alignment) {
		    transmitLS();
		    if (!m_t2.started())
			m_t2.start();
		} else if (m_localStatus == OutOfService)
		    startAlignment();
		else
		    abortAlignment("Recv remote OOS");
	    }
	    setRemoteStatus(status);
	    break;
	default:
	    Debug(this,DebugNote,"Received unknown link status message %d",status);
	    return false;
    }
    return true;
}

bool SS7M2PA::processSLinkStatus(DataBlock& data,int streamId)
{
    if (data.length() < 4)
	return false;
    u_int32_t status = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (m_remoteStatus == status && status != OutOfService)
	return true;
    if (m_waitOosTimer.started())
	return true;
    Debug(this,DebugAll,"Received link status: %s, local status : %s, requested status %s",
	lookup(status,s_state),lookup(m_localStatus,s_state),lookup(m_state,s_state));
    switch (status) {
	case Alignment:
	    m_oosTimer.stop();
	    if (m_localStatus == Alignment && m_t2.started()) {
		m_t2.stop();
		if (m_state == ProvingNormal || m_state == ProvingEmergency) {
		    setLocalStatus(m_state);
		    transmitLS();
		    m_t3.start();
		}
	    } else if (m_localStatus == OutOfService) {
		setLocalStatus(Alignment);
		transmitLS();
		m_t3.start();
	    } else
		abortAlignment("Out of order alignment message");
	    setRemoteStatus(status);
	    break;
	case ProvingNormal:
	case ProvingEmergency:
	    m_t2.stop();
	    if (m_localStatus == Alignment && m_t3.started()) {
		m_t3.stop();
		setLocalStatus(status);
		transmitLS();
		if (status == ProvingEmergency || m_state == ProvingEmergency)
		    m_t4.fire(Time::msecNow() + (m_t4.interval() / 16));
		else
		    m_t4.start();
	    } else if (m_localStatus == ProvingNormal || m_localStatus == ProvingEmergency) {
		m_t3.stop();
		if (status == ProvingEmergency || m_state == ProvingEmergency)
		    m_t4.fire(Time::msecNow() + (m_t4.interval() / 16));
		else
		    m_t4.start();
	    } else
		abortAlignment("Out of order proving message");
	    setRemoteStatus(status);
	    break;
	case Ready:
	    if (m_localStatus == ProvingNormal || m_localStatus == ProvingEmergency) {
		setLocalStatus(Ready);
		transmitLS();
	    } else if (m_localStatus != Ready) {
		abortAlignment("Out of order Ready message");
		return true;
	    }
	    setRemoteStatus(status);
	    m_lastSeqRx = -1;
	    SS7Layer2::notify();
	    m_oosTimer.stop();
	    m_t2.stop();
	    m_t3.stop();
	    m_t4.stop();
	    m_t1.stop();
	    break;
	case ProcessorRecovered:
	    transmitLS();
	    setRemoteStatus(status);
	    break;
	case BusyEnded:
	    setRemoteStatus(Ready);
	    SS7Layer2::notify();
	    break;
	case ProcessorOutage:
	case Busy:
	    setRemoteStatus(status);
	    SS7Layer2::notify();
	    break;
	case OutOfService:
	    if (!(m_state == ProvingNormal || m_state == ProvingEmergency)) {
		abortAlignment("Requested Pause");
		setRemoteStatus(status);
		return true;
	    }
	    if (m_localStatus == OutOfService) {
		m_oosTimer.stop();
		setLocalStatus(Alignment);
		transmitLS();
		if (!m_t2.started())
		    m_t2.start();
	    } else if (m_localStatus == Alignment)
		transmitLS();
	    else {
		abortAlignment("Remote OOS");
		m_waitOosTimer.fire(Time::msecNow() + (m_waitOosTimer.interval() / 2));
	    }
	    setRemoteStatus(status);
	    break;
	default:
	    Debug(this,DebugNote,"Received unknown link status message %d",status);
	    return false;
    }
    return true;
}

void SS7M2PA::recoverMSU(int sequence)
{
    if (operational()) {
	Debug(this,DebugMild,"Recover MSU from sequence %d while link is operational",sequence);
	return;
    }
    Debug(this,DebugInfo,"Recovering MSUs from sequence %d",sequence);
    for (;;) {
	m_mutex.lock();
	DataBlock* pkt = static_cast<DataBlock*>(m_ackList.remove(false));
	m_mutex.unlock();
	if (!pkt)
	    break;
	unsigned char* head = pkt->data(0,8);
	if (head) {
	    int seq = head[7] | ((int)head[6] << 8) | ((int)head[5] << 16);
	    if (sequence < 0 || ((seq - sequence) & 0x00ffffff) < 0x007fffff) {
		sequence = -1;
		SS7MSU msu(head + 8,pkt->length() - 8);
		recoveredMSU(msu);
	    }
	    else
		Debug(this,DebugAll,"Not recovering MSU with seq=%d, requested %d",
		    seq,sequence);
	}
	TelEngine::destruct(pkt);
    }
}

void SS7M2PA::retransData()
{
    for (ObjList* o = m_ackList.skipNull();o;o = o->skipNext()) {
	DataBlock* msg = static_cast<DataBlock*>(o->get());
	u_int8_t* head = (u_int8_t*)msg->data();
	head[1] = (m_needToAck >> 16) & 0xff;
	head[2] = (m_needToAck >> 8) & 0xff;
	head[3] = m_needToAck & 0xff ;
	if (m_confTimer.started())
	    m_confTimer.stop();
	if (!m_ackTimer.started())
	    m_ackTimer.start();
	transmitMSG(1,M2PA, 1, *msg,1);
    }
}

bool SS7M2PA::transmitMSU(const SS7MSU& msu)
{
    if (msu.length() < 3) {
	Debug(this,DebugWarn,"Asked to send too short MSU of length %u [%p]",
	    msu.length(),this);
	return false;
    }
    // If we don't have an attached interface don't bother
    if (!transport())
	return false;
    Lock lock(m_mutex);
    if (!operational())
	return false;
    DataBlock packet;
    increment(m_seqNr);
    setHeader(packet);
    if (m_confTimer.started())
	m_confTimer.stop();
    static const DataBlock priority(0,1);
    packet += priority;
    packet += msu;
    m_ackList.append(new DataBlock(packet));
    if (m_dumpMsg)
	dumpMsg(1,M2PA,1,packet,1,true);
    if (!m_ackTimer.started())
	m_ackTimer.start();
    return transmitMSG(1,M2PA,1,packet,1);
}

void SS7M2PA::notifyLayer(SignallingInterface::Notification event)
{
    switch (event) {
	case SignallingInterface::LinkDown:
	    m_transportState = Idle;
	    m_connFailCounter = 0;
	    abortAlignment("LinkDown");
	    m_connFailTimer.stop();
	    m_connFailCounter = 0;
	    SS7Layer2::notify();
	    break;
	case SignallingInterface::LinkUp:
	{
	    m_transportState = Established;
	    Debug(this,DebugInfo,"Interface is up [%p]",this);
	    String params = "rto_max";
	    NamedList result("sctp_params");
	    if (getSocketParams(params,result)) {
		int rtoMax = result.getIntValue(YSTRING("rto_max"));
		unsigned int maxRetrans = rtoMax + (int)m_confTimer.interval() + AVG_DELAY;
		if (maxRetrans > m_ackTimer.interval()) {
		    Debug(this,DebugConf,
			  "%s (%d) is greater than ack timer (%d)! Max RTO: %d, conf timer %d, avg delay: %d",
			  "The maximum time interval to retransmit a packet",
			  maxRetrans,(int)m_ackTimer.interval(),
			  rtoMax,(int)m_confTimer.interval(),AVG_DELAY);
		}
	    } else
		Debug(this,DebugNote,"Failed to obtain socket params");
	    if (m_autostart)
		startAlignment();
	    SS7Layer2::notify();
	    break;
	}
	case SignallingInterface::HardwareError:
	    abortAlignment("HardwareError");
	    if (m_autostart && (m_transportState == Established))
		startAlignment();
	    SS7Layer2::notify();
	    break;
	default:
	    return;
    }
}


bool SS7M2UAClient::processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId)
{
    u_int32_t iid = (u_int32_t)-1;
    if (MGMT == msgClass && getTag(msg,0x0001,iid)) {
	Lock mylock(this);
	for (ObjList* o = users().skipNull(); o; o = o->skipNext()) {
	    AdaptUserPtr* p = static_cast<AdaptUserPtr*>(o->get());
	    RefPointer<SS7M2UA> m2ua = static_cast<SS7M2UA*>(static_cast<SIGAdaptUser*>(*p));
	    if (!m2ua || (m2ua->iid() != (int32_t)iid))
		continue;
	    mylock.drop();
	    return m2ua->processMGMT(msgType,msg,streamId);
	}
	Debug(this,DebugStub,"Unhandled M2UA MGMT message type %u for IID=%u",msgType,iid);
	return false;
    }
    else if (MAUP != msgClass)
	return processCommonMSG(msgClass,msgType,msg,streamId);
    switch (msgType) {
	case 2: // Establish Request
	case 4: // Release Request
	case 7: // State Request
	case 10: // Data Retrieval Request
	    Debug(this,DebugWarn,"Received M2UA SG request %u on ASP side!",msgType);
	    return false;
    }
    getTag(msg,0x0001,iid);
    Lock mylock(this);
    for (ObjList* o = users().skipNull(); o; o = o->skipNext()) {
	AdaptUserPtr* p = static_cast<AdaptUserPtr*>(o->get());
	RefPointer<SS7M2UA> m2ua = static_cast<SS7M2UA*>(static_cast<SIGAdaptUser*>(*p));
	if (!m2ua || (m2ua->iid() != (int32_t)iid))
	    continue;
	mylock.drop();
	return m2ua->processMAUP(msgType,msg,streamId);
    }
    Debug(this,DebugStub,"Unhandled M2UA message type %u for IID=%d",msgType,(int32_t)iid);
    return false;
}


SS7M2UA::SS7M2UA(const NamedList& params)
    : SignallingComponent(params.safe("SS7M2UA"),&params,"ss7-m2ua"),
      m_retrieve(50),
      m_iid(params.getIntValue(YSTRING("iid"),-1)),
      m_linkState(LinkDown), m_rpo(false),
      m_longSeq(false)
{
    DDebug(DebugInfo,"Creating SS7M2UA [%p]",this);
    m_retrieve.interval(params,"retrieve",5,200,true);
    m_longSeq = params.getBoolValue(YSTRING("longsequence"));
    m_lastSeqRx = -2;
}

bool SS7M2UA::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7M2UA::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    m_autostart = !config || config->getBoolValue(YSTRING("autostart"),true);
    m_autoEmergency = !config || config->getBoolValue(YSTRING("autoemergency"),true);
    if (config && !adaptation()) {
	m_iid = config->getIntValue(YSTRING("iid"),m_iid);
	NamedList params("");
	if (resolveConfig(YSTRING("client"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    DDebug(this,DebugInfo,"Creating adaptation '%s' for SS7 M2UA [%p]",
		params.c_str(),this);
	    params.addParam("basename",params);
	    SS7M2UAClient* client =
		YOBJECT(SS7M2UAClient,engine()->build("SS7M2UAClient",params,false));
	    if (!client)
		return false;
	    adaptation(client);
	    client->initialize(&params);
	    TelEngine::destruct(client);
	}
    }
    return transport() && control(Resume,const_cast<NamedList*>(config));
}

bool SS7M2UA::control(Operation oper, NamedList* params)
{
    if (params) {
	m_autostart = params->getBoolValue(YSTRING("autostart"),m_autostart);
	m_autoEmergency = params->getBoolValue(YSTRING("autoemergency"),m_autoEmergency);
	m_longSeq = params->getBoolValue(YSTRING("longsequence"),m_longSeq);
    }
    switch (oper) {
	case Pause:
	    if (aspActive()) {
		DataBlock buf;
		if (m_iid >= 0)
		    SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
		// Release Request
		if (!adaptation()->transmitMSG(SIGTRAN::MAUP,4,buf,getStreamId()))
		    return TelEngine::controlReturn(params,false);
		getSequence();
	    }
	    m_linkState = LinkDown;
	    if (!m_retrieve.started())
		SS7Layer2::notify();
	    return TelEngine::controlReturn(params,true);
	case Resume:
	    if (operational())
		return TelEngine::controlReturn(params,true);
	    if (!m_autostart)
		return TelEngine::controlReturn(params,activate());
	    if (m_retrieve.started()) {
		if (LinkDown == m_linkState)
		    m_linkState = getEmergency(params,false) ? LinkReqEmg : LinkReq;
		return TelEngine::controlReturn(params,activate());
	    }
	    // fall through
	case Align:
	    if (aspActive()) {
		if (operational()) {
		    m_linkState = LinkDown;
		    SS7Layer2::notify();
		}
		bool emg = (LinkUpEmg == m_linkState) || (LinkReqEmg == m_linkState);
		emg = getEmergency(params,emg);
		m_linkState = emg ? LinkReqEmg : LinkReq;
		DataBlock buf;
		if (m_iid >= 0)
		    SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
		SIGAdaptation::addTag(buf,0x0302,(emg ? 2 : 3));
		// State Request
		if (!adaptation()->transmitMSG(SIGTRAN::MAUP,7,buf,getStreamId()))
		    return TelEngine::controlReturn(params,false);
		buf.clear();
		if (m_iid >= 0)
		    SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
		// Establish Request
		return TelEngine::controlReturn(params,
			adaptation()->transmitMSG(SIGTRAN::MAUP,2,buf,getStreamId()));
	    }
	    return TelEngine::controlReturn(params,activate());
	case Status:
	    return TelEngine::controlReturn(params,operational());
	default:
	    return TelEngine::controlReturn(params,false);
    }
}

unsigned int SS7M2UA::status() const
{
    switch (m_linkState) {
	case LinkDown:
	    return SS7Layer2::OutOfService;
	case LinkUp:
	    return m_rpo ? SS7Layer2::ProcessorOutage : SS7Layer2::NormalAlignment;
	case LinkUpEmg:
	    return m_rpo ? SS7Layer2::ProcessorOutage : SS7Layer2::EmergencyAlignment;
    }
    return SS7Layer2::OutOfAlignment;
}

bool SS7M2UA::transmitMSU(const SS7MSU& msu)
{
    if (msu.length() < 3) {
	Debug(this,DebugWarn,"Asked to send too short MSU of length %u [%p]",
	    msu.length(),this);
	return false;
    }
    Lock mylock(adaptation());
    // If we don't have an attached interface don't bother
    if (!transport())
	return false;
    DataBlock buf;
    if (m_iid >= 0)
	SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
    SIGAdaptation::addTag(buf,0x0300,msu);
    // Data
    return adaptation()->transmitMSG(SIGTRAN::MAUP,1,buf,getStreamId());
}

void SS7M2UA::recoverMSU(int sequence)
{
    Lock mylock(adaptation());
    if (sequence >= 0 && aspUp() && transport()) {
	Debug(this,DebugInfo,"Retrieving MSUs from sequence %d from M2UA SG",sequence);
	DataBlock buf;
	if (m_iid >= 0)
	    SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
	// Retrieve MSGS action
	SIGAdaptation::addTag(buf,0x0306,(u_int32_t)0);
	SIGAdaptation::addTag(buf,0x0307,(u_int32_t)sequence);
	// Data Retrieval Request
	adaptation()->transmitMSG(SIGTRAN::MAUP,10,buf,getStreamId());
    }
}

int SS7M2UA::getSequence()
{
    if (m_lastSeqRx == -1) {
	m_lastSeqRx = -2;
	Lock mylock(adaptation());
	if (aspUp() && transport()) {
	    Debug(this,DebugInfo,"Requesting sequence number from M2UA SG");
	    DataBlock buf;
	    if (m_iid >= 0)
		SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
	    // Retrieve BSN action
	    SIGAdaptation::addTag(buf,0x0306,(u_int32_t)1);
	    // Data Retrieval Request
	    if (adaptation()->transmitMSG(SIGTRAN::MAUP,10,buf,getStreamId()))
		m_retrieve.start();
	}
    }
    return m_lastSeqRx;
}

void SS7M2UA::timerTick(const Time& when)
{
    SS7Layer2::timerTick(when);
    if (m_retrieve.timeout(when.msec())) {
	m_retrieve.stop();
	if (m_lastSeqRx == -2) {
	    Debug(this,DebugWarn,"Sequence retrieval from M2UA SG timed out");
	    SS7Layer2::notify();
	}
	if (m_linkState != LinkDown)
	    control(Resume);
    }
}

bool SS7M2UA::processMGMT(unsigned char msgType, const DataBlock& msg, int streamId)
{
    const char* err = "Unhandled";
    switch (msgType) {
	case SIGTRAN::MgmtERR:
	    {
		u_int32_t errCode = 0;
		if (SIGAdaptation::getTag(msg,0x000c,errCode)) {
		    switch (errCode) {
			case 2:
			    Debug(this,DebugWarn,"M2UA SG reported invalid IID=%d",m_iid);
			    m_linkState = LinkDown;
			    return true;
			default:
			    Debug(this,DebugWarn,"M2UA SG reported error %u: %s",errCode,lookup(errCode,s_uaErrors,"Unknown"));
			    return true;
		    }
		}
	    }
	    err = "Error";
	    break;
    }
    Debug(this,DebugStub,"%s M2UA MGMT message type %u",err,msgType);
    return false;
}

bool SS7M2UA::processMAUP(unsigned char msgType, const DataBlock& msg, int streamId)
{
    const char* err = "Unhandled";
    switch (msgType) {
	case 1: // Data
	    {
		SS7MSU data;
		if (!SIGAdaptation::getTag(msg,0x0300,data)) {
		    err = "Missing data in";
		    break;
		}
		u_int32_t corrId;
		if (SIGAdaptation::getTag(msg,0x0013,corrId)) {
		    // Correlation ID present, send Data Ack
		    DataBlock buf;
		    SIGAdaptation::addTag(buf,0x0013,corrId);
		    adaptation()->transmitMSG(SIGTRAN::MAUP,15,buf,streamId);
		}
		return receivedMSU(data);
	    }
	    break;
	case 3: // Establish Confirm
	    m_lastSeqRx = -1;
	    m_linkState = LinkUp;
	    m_congestion = 0;
	    m_rpo = false;
	    SS7Layer2::notify();
	    return true;
	case 5: // Release Confirm
	case 6: // Release Indication
	    activeChange(false);
	    return true;
	case 8: // State Confirm
	    err = "Ignoring";
	    break;
	case 9: // State Indication
	    {
		u_int32_t evt = 0;
		if (!SIGAdaptation::getTag(msg,0x0303,evt)) {
		    err = "Missing state event";
		    break;
		}
		bool oper = operational();
		switch (evt) {
		    case 1:
			Debug(this,DebugInfo,"Remote entered Processor Outage");
			m_rpo = true;
			break;
		    case 2:
			Debug(this,DebugInfo,"Remote exited Processor Outage");
			m_rpo = false;
			break;
		}
		if (operational() != oper)
		    SS7Layer2::notify();
	    }
	    return true;
	case 11: // Data Retrieval Confirm
	    {
		u_int32_t res = 0;
		if (!SIGAdaptation::getTag(msg,0x0308,res)) {
		    err = "Missing retrieval result";
		    break;
		}
		if (res) {
		    err = "Retrieval failed";
		    break;
		}
		if (SIGAdaptation::getTag(msg,0x0306,res) && (res == 1)) {
		    // Action was BSN retrieval
		    res = (u_int32_t)-1;
		    if (!SIGAdaptation::getTag(msg,0x0307,res)) {
			err = "Missing BSN field in retrieval";
			m_lastSeqRx = -3;
			postRetrieve();
			break;
		    }
		    Debug(this,DebugInfo,"Recovered sequence number %u",res);
		    if (m_longSeq || res & 0xffffff80)
			res = (res & 0x00ffffff) | 0x01000000;
		    m_lastSeqRx = res;
		    postRetrieve();
		    return true;
		}
	    }
	    break;
	case 12: // Data Retrieval Indication
	case 13: // Data Retrieval Complete Indication
	    {
		SS7MSU data;
		if (!SIGAdaptation::getTag(msg,0x0300,data)) {
		    if (msgType == 13)
			return true;
		    err = "Missing data in";
		    break;
		}
		return recoveredMSU(data);
	    }
	    break;
	case 14: // Congestion Indication
	    {
		u_int32_t cong = 0;
		if (!SIGAdaptation::getTag(msg,0x0304,cong)) {
		    err = "Missing congestion state";
		    break;
		}
		u_int32_t disc = 0;
		SIGAdaptation::getTag(msg,0x0305,disc);
		int level = disc ? DebugWarn : (cong ? DebugMild : DebugNote);
		Debug(this,level,"Congestion level %u, discard level %u",cong,disc);
		m_congestion = cong;
	    }
	    return true;
    }
    Debug(this,DebugStub,"%s M2UA MAUP message type %u",err,msgType);
    return false;
}

void SS7M2UA::postRetrieve()
{
    if (!m_retrieve.started())
	return;
    m_retrieve.stop();
    SS7Layer2::notify();
    m_retrieve.fire(Time::msecNow()+100);
}

void SS7M2UA::activeChange(bool active)
{
    if (!active) {
	getSequence();
	m_congestion = 0;
	m_rpo = false;
	switch (m_linkState) {
	    case LinkUpEmg:
		m_linkState = LinkReqEmg;
		if (!m_retrieve.started())
		    SS7Layer2::notify();
		break;
	    case LinkUp:
		m_linkState = LinkReq;
		if (!m_retrieve.started())
		    SS7Layer2::notify();
		break;
	    case LinkReqEmg:
	    case LinkReq:
		break;
	    default:
		return;
	}
    }
    control(Resume);
}

bool SS7M2UA::operational() const
{
    return (m_linkState >= LinkUp) && !m_rpo;
}

/**
 * ISDNIUAClient
 */

bool ISDNIUAClient::processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId)
{
    u_int32_t iid = (u_int32_t)-1;
    if (MGMT == msgClass && getTag(msg,0x0001,iid)) {
	Lock mylock(this);
	for (ObjList* o = users().skipNull(); o; o = o->skipNext()) {
	    AdaptUserPtr* p = static_cast<AdaptUserPtr*>(o->get());
	    RefPointer<ISDNIUA> iua = static_cast<ISDNIUA*>(static_cast<SIGAdaptUser*>(*p));
	    if (!iua || (iua->iid() != (int32_t)iid))
		continue;
	    mylock.drop();
	    return iua->processMGMT(msgType,msg,streamId);
	}
	Debug(this,DebugStub,"Unhandled IUA MGMT message type %u for IID=%u",msgType,iid);
	return false;
    }
    else if (QPTM != msgClass)
	return processCommonMSG(msgClass,msgType,msg,streamId);
    switch (msgType) {
	case 1: // Data Request Message
	case 3: // Unit Data Request Message
	case 5: // Establish Request
	case 8: // Release Request
	    Debug(this,DebugWarn,"Received IUA SG request %u on ASP side!",msgType);
	    return false;
    }
    getTag(msg,0x0001,iid);
    Lock mylock(this);
    for (ObjList* o = users().skipNull(); o; o = o->skipNext()) {
	AdaptUserPtr* p = static_cast<AdaptUserPtr*>(o->get());
	RefPointer<ISDNIUA> iua = static_cast<ISDNIUA*>(static_cast<SIGAdaptUser*>(*p));
	if (!iua || (iua->iid() != (int32_t)iid))
	    continue;
	mylock.drop();
	return iua->processQPTM(msgType,msg,streamId);
    }
    Debug(this,DebugStub,"Unhandled IUA message type %u for IID=%d",msgType,(int32_t)iid);
    return false;
}


ISDNIUA::ISDNIUA(const NamedList& params, const char *name, u_int8_t tei)
    : SignallingComponent(params.safe(name ? name : "ISDNIUA"),&params,"isdn-iua"),
      ISDNLayer2(params,name,tei),
      m_iid(params.getIntValue(YSTRING("iid"),-1))
{
    DDebug(DebugInfo,"Creating ISDNIUA [%p]",this);
}

ISDNIUA::~ISDNIUA()
{
    Lock lock(l2Mutex());
    cleanup();
    ISDNLayer2::attach((ISDNLayer3*)0);
}

bool ISDNIUA::multipleFrame(u_int8_t tei, bool establish, bool force)
{
    Lock lock(l2Mutex());
    if (!transport())
	return false;
    if ((localTei() != tei) || (state() == WaitEstablish) || (state() == WaitRelease))
	return false;
    if (!force &&
	((establish && (state() == Established)) ||
	(!establish && (state() == Released))))
	return false;
    XDebug(this,DebugAll,"Process '%s' request, TEI=%u",
	establish ? "ESTABLISH" : "RELEASE",tei);

    DataBlock buf;
    if (m_iid >= 0)
	SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
    u_int32_t dlci = 0x10000 | ((unsigned int)tei << 17);
    SIGAdaptation::addTag(buf,0x0005,dlci);
    if (establish)
	changeState(WaitEstablish,"multiple frame");
    else {
	SIGAdaptation::addTag(buf,0x000f,(u_int32_t)(force ? 2 : 0));
	changeState(WaitRelease,"multiple frame");
	multipleFrameReleased(tei,true,false);
    }
    // Establish Request or Release Request
    return adaptation()->transmitMSG(SIGTRAN::QPTM,(establish ? 5 : 8),buf,getStreamId());
}

bool ISDNIUA::sendData(const DataBlock& data, u_int8_t tei, bool ack)
{
    if (data.null())
	return false;
    Lock lock(l2Mutex());
    if (!transport())
	return false;
    DataBlock buf;
    if (m_iid >= 0)
	SIGAdaptation::addTag(buf,0x0001,(u_int32_t)m_iid);
    u_int32_t dlci = 0x10000 | ((unsigned int)tei << 17);
    SIGAdaptation::addTag(buf,0x0005,dlci);
    SIGAdaptation::addTag(buf,0x000e,data);
    // Data Request Message or Unit Data Request Message
    return adaptation()->transmitMSG(SIGTRAN::QPTM,(ack ? 1 : 3),buf,getStreamId());
}

void ISDNIUA::cleanup()
{
    Lock lock(l2Mutex());
    DDebug(this,DebugAll,"Cleanup in state '%s'",stateName(state()));
    if (state() == Established)
	multipleFrame(localTei(),false,true);
    changeState(Released,"cleanup");
}

bool ISDNIUA::processMGMT(unsigned char msgType, const DataBlock& msg, int streamId)
{
    const char* err = "Unhandled";
    switch (msgType) {
	case SIGTRAN::MgmtERR:
	    {
		u_int32_t errCode = 0;
		if (SIGAdaptation::getTag(msg,0x000c,errCode)) {
		    switch (errCode) {
			case 2:
			    Debug(this,DebugWarn,"IUA SG reported invalid IID=%d",m_iid);
			    changeState(Released,"invalid IID");
			    multipleFrameReleased(localTei(),false,true);
			    return true;
			case 10:
			    Debug(this,DebugWarn,"IUA SG reported unassigned TEI");
			    changeState(Released,"unassigned TEI");
			    multipleFrameReleased(localTei(),false,true);
			    return true;
			case 12:
			    Debug(this,DebugWarn,"IUA SG reported unrecognized SAPI");
			    changeState(Released,"unrecognized SAPI");
			    multipleFrameReleased(localTei(),false,true);
			    return true;
			default:
			    Debug(this,DebugWarn,"IUA SG reported error %u: %s",errCode,lookup(errCode,s_uaErrors,"Unknown"));
			    return true;
		    }
		}
	    }
	    err = "Error";
	    break;
	case 2: // TEI Status Request
	    err = "Wrong direction TEI Status Request";
	    break;
	case 3: // TEI Status Confirm
	case 4: // TEI Status Indication
	    {
		u_int32_t status = 0;
		if (!SIGAdaptation::getTag(msg,0x0010,status)) {
		    err = "Missing TEI status in";
		    break;
		}
		u_int32_t dlci = 0;
		if (!SIGAdaptation::getTag(msg,0x0005,dlci)) {
		    err = "Missing DLCI in";
		    break;
		}
		u_int8_t tei = (dlci >> 17) & 0x7e;
		Debug(this,DebugNote,"%sTEI %u Status is %s",
		    (localTei() == tei ? "Our " : ""),tei,
		    (status ? "unassigned" : "assigned"));
		if (status && (localTei() == tei)) {
		    changeState(Released,"unassigned TEI");
		    multipleFrameReleased(localTei(),false,true);
		}
		return true;
	    }
	case 5: // TEI Query Request
	    err = "Wrong direction TEI Status Query";
	    break;
    }
    Debug(this,DebugStub,"%s IUA MGMT message type %u",err,msgType);
    return false;
}

bool ISDNIUA::processQPTM(unsigned char msgType, const DataBlock& msg, int streamId)
{
    const char* err = "Unhandled";
    switch (msgType) {
	case 2: // Data Request Message
	case 4: // Unit Data Request Message
	    {
		u_int32_t dlci = 0;
		if (!SIGAdaptation::getTag(msg,0x0005,dlci)) {
		    err = "Missing DLCI in";
		    break;
		}
		DataBlock data;
		if (!SIGAdaptation::getTag(msg,0x000e,data)) {
		    err = "Missing data in";
		    break;
		}
		receiveData(data,(dlci >> 17) & 0x7e);
		return true;
	    }
	    break;
	case 6: // Establish Confirm
	case 7: // Establish Indication
	    changeState(Established);
	    multipleFrameEstablished(localTei(),(6 == msgType),false);
	    return true;
	case 9: // Release Confirm
	    changeState(Released,"remote confirm");
	    multipleFrameReleased(localTei(),true,false);
	    return true;
	case 10: // Release Indication
	    {
		u_int32_t reason = 0;
		if (SIGAdaptation::getTag(msg,0x000f,reason))
		    Debug(this,DebugMild,"IUA SG released interface, reason %d",reason);
		else
		    Debug(this,DebugMild,"IUA SG released interface, no reason");
	    }
	    changeState(Released,"remote indication");
	    multipleFrameReleased(localTei(),false,true);
	    return true;
    }
    Debug(this,DebugStub,"%s IUA QPTM message type %u",err,msgType);
    return false;
}

void ISDNIUA::activeChange(bool active)
{
    if (active) {
	if (m_autostart)
	    multipleFrame(localTei(),true,false);
    }
    else {
	changeState(Released,"remote inactive");
	multipleFrameReleased(localTei(),false,true);
    }
}

bool ISDNIUA::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"ISDNIUA::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    m_autostart = !config || config->getBoolValue(YSTRING("autostart"),true);
    if (config && !adaptation()) {
	m_iid = config->getIntValue(YSTRING("iid"),m_iid);
	NamedList params("");
	if (resolveConfig(YSTRING("client"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    DDebug(this,DebugInfo,"Creating adaptation '%s' for ISDN UA [%p]",
		params.c_str(),this);
	    params.addParam("basename",params);
	    ISDNIUAClient* client =
		YOBJECT(ISDNIUAClient,engine()->build("ISDNIUAClient",params,false));
	    if (!client)
		return false;
	    adaptation(client);
	    client->initialize(&params);
	    TelEngine::destruct(client);
	}
    }
    if (!transport())
	return false;
    return (m_autostart && aspActive()) ? multipleFrame(localTei(),true,false) : activate();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
