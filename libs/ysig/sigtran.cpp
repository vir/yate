/**
 * sigtran.cpp
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

const TokenDict* SIGTRAN::classNames()
{
    return s_classes;
}

SIGTRAN::SIGTRAN()
    : m_trans(0), m_transMutex(false,"SIGTRAN::transport")
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


// Attach or detach an user adaptation layer
void SIGTransport::attach(SIGTRAN* sigtran)
{
    if (m_sigtran != sigtran) {
	m_sigtran = sigtran;
	attached(sigtran != 0);
    }
}

// Request processing from the adaptation layer
bool SIGTransport::processMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId) const
{
    return m_sigtran && m_sigtran->processMSG(msgVersion,msgClass,msgType,msg,streamId);
}

void SIGTransport::notifyLayer(SignallingInterface::Notification event)
{
    if (m_sigtran)
	m_sigtran->notifyLayer(event);
}
// Build the common header and transmit a message to the network
bool SIGTransport::transmitMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId)
{
    if (!connected(streamId))
	return false;

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

static TokenDict s_messageType[] = {
    {"UserData",   SS7M2PA::UserData},
    {"LinkStatus", SS7M2PA::LinkStatus},
    {0,0}
};

SS7M2PA::SS7M2PA(const NamedList& params)
    : SignallingComponent(params.safe("SS7M2PA"),&params),
      m_seqNr(0xffffff), m_needToAck(0xffffff), m_lastAck(0xffffff),
      m_localStatus(OutOfService), m_state(OutOfService),
      m_remoteStatus(OutOfService), m_transportState(Idle), m_mutex(String("Mutex:") + debugName()), m_t1(0),
      m_t2(0), m_t3(0), m_t4(0), m_ackTimer(0), m_confTimer(0), m_dumpMsg(false)

{
    // Alignment ready timer ~45s
    m_t1.interval(params,"t1",45000,50000,false);
    // Not Aligned timer ~5s
    m_t2.interval(params,"t2",5000,5500,false);
    // Aligned timer ~1s
    m_t3.interval(params,"t3",1000,1500,false);
    // Proving timer Normal ~8s, Emergency ~0.5s
    m_t4.interval(params,"t4",1000,1000,false);
    // Acknowledge timer ~1s
    m_ackTimer.interval(params,"ack_timer",1000,1100,false);
    // Confirmation timer 1/2 t4
    m_confTimer.interval(params,"conf_timer",500,600,false);
    DDebug(this,DebugAll,"Creating SS7M2PA [%p]",this);
}

SS7M2PA::~SS7M2PA()
{
    Lock lock(m_mutex);
    m_ackList.clear();
    m_bufMsg.clear();
    DDebug(this,DebugAll,"Destroying SS7M2PA [%p]",this);
}

bool SS7M2PA::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7M2PA::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    m_dumpMsg = config->getBoolValue("dumpMsg",false);
    m_autostart = config->getBoolValue("autostart",true);
    if (config && !transport()) {
	NamedString* name = config->getParam("sig");
	if (!name)
	    name = config->getParam("basename");
	if (name) {
	    NamedPointer* ptr = YOBJECT(NamedPointer,name);
	    NamedList* trConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
	    NamedList params(name->c_str());
	    params.addParam("basename",*name);
	    params.addParam("protocol","ss7");
	    if (trConfig)
		params.copyParams(*trConfig);
	    else {
		params.copySubParams(*config,params + ".");
		trConfig = &params;
	    }
	    SIGTransport* tr = YSIGCREATE(SIGTransport,&params);
	    if (!tr)
		return false;
	    SIGTRAN::attach(tr);
	    if (!tr->initialize(trConfig))
		SIGTRAN::attach(0);
	}
    }
    if (transport())
	m_reliable = transport()->reliable();
    return transport() && (control(Resume,const_cast<NamedList*>(config)));
}

void SS7M2PA::dumpMsg(u_int8_t version, u_int8_t mClass, u_int8_t type,
    const DataBlock& data, int stream, bool send)
{
    String dump = "SS7M2PA ";
    dump << (send ? "Sending:" : "Received:");
    dump << "\n-----";
    String indent = "\n  ";
    dump << indent << "Version: " << version;
    dump << "    " << "Message class: " << mClass;
    dump << "    " << "Message type: " << lookup(type,s_messageType,"Unknown");
    dump << indent << "Stream: " << stream;
    u_int32_t fsn = (data[1] << 16) | (data[2] << 8) | data[3];
    u_int32_t bsn = (data[5] << 16) | (data[6] << 8) | data[7];
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
    dump << "\n-----";
    Debug(this,DebugInfo,"%s",dump.c_str());
}

bool SS7M2PA::processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId)
{
    if (msgClass != M2PA) {
	Debug(this,DebugWarn,"Received non M2PA message class %d",msgClass);
	dumpMsg(msgVersion,msgClass,msgType,msg,streamId,false);
	return false;
    }
    if (m_dumpMsg)
	dumpMsg(msgVersion,msgClass,msgType,msg,streamId,false);
    Lock lock(m_mutex);
    if (!operational() && msgType == UserData) {
	if (m_remoteStatus != ProcessorOutage || m_remoteStatus != Busy)
	    return false;
	// If we are not operational buffer the received messages and ack them when we are op
	m_bufMsg.append(new DataBlock(msg));
	DDebug(this,DebugAll,"Buffering data message while non operational, %d messages buffered",
	    m_bufMsg.count());
	return true;
    }
    if (!operational() && msgType == UserData)
	return false;
    if (!decodeSeq(msg,(u_int8_t)msgType))
	return false;
    DataBlock data(msg);
    data.cut(-8);
    if (!data.length())
	return true;
    if (msgType == LinkStatus)
	return processLinkStatus(data,streamId);
#ifdef DEBUG
    if (streamId != 1)
	Debug(this,DebugNote,"Received data message on Link status stream");
#endif
    lock.drop();
    SS7MSU msu(data);
    return receivedMSU(msu);
}

bool SS7M2PA::decodeSeq(const DataBlock& data,u_int8_t msgType)
{
    if (data.length() < 8)
	return false;
    u_int32_t fsn = (data[1] << 16) | (data[2] << 8) | data[3];
    u_int32_t bsn = (data[5] << 16) | (data[6] << 8) | data[7];
    if (msgType == LinkStatus) {
	if (fsn != m_needToAck) {
	    DDebug(this,DebugNote,"Received LinkStatus message with wrong sequence number %d expected %d",
		fsn,m_needToAck);
	    abortAlignment("Wrong Sequence number");
	    transmitLS();
	    return false;
	}
	if (bsn == getNext(m_lastAck))
	    removeFrame(bsn);
	if (bsn == m_lastAck)
	    return true;
	// If we are here meens that something went wrong
	abortAlignment("msgType == LinkStatus");
	transmitLS();
	return false;
    }
    if (fsn != getNext(m_needToAck) && fsn != m_needToAck) {
	abortAlignment("Received Out of sequence frame");
	transmitLS();
	return false;
    }
    else {
	if (fsn == getNext(m_needToAck) && operational()) {
	    if (m_confTimer.started()) {
		sendAck();
		m_confTimer.stop();
	    }
	    m_needToAck = fsn;
	    m_confTimer.start();
	}
	else if (!operational())
	    m_bufMsg.append(new DataBlock(data));
    }
    if (bsn == getNext(m_lastAck))
	removeFrame(bsn);
    if (bsn != m_lastAck) {
	abortAlignment(String("Received unexpected bsn: ") << bsn);
	transmitLS();
	return false;
    }
    return true;
}

void SS7M2PA::timerTick(const Time& when)
{
    Lock lock(m_mutex);
    if (m_confTimer.started() && m_confTimer.timeout(when.msec())) {
	sendAck(); // Acknowledge last received message before endpoint drops down the link
	m_confTimer.stop();
    }
    if (m_ackTimer.started() && m_ackTimer.timeout(when.msec())) {
	m_ackTimer.stop();
	if (m_reliable) {
	    lock.drop();
	    abortAlignment("Ack timer timeout");
	} else
	    retransData();
    }
    if (m_t2.started() && m_t2.timeout(when.msec())) {
	m_t2.stop();
	abortAlignment("T2 timeout");
	return;
    }
    if (m_t3.started() && m_t3.timeout(when.msec())) {
	m_t3.stop();
	abortAlignment("T3 timeout");
	return;
    }
    if (m_t4.started() && m_t4.timeout(when.msec())) {
	m_t4.stop();
	setLocalStatus(Ready);
	transmitLS();
	m_t1.start();
	return;
    }
    if (m_t1.started() && m_t1.timeout(when.msec())) {
	m_t1.stop();
	abortAlignment("T1 timeout");
    }
}

void SS7M2PA::removeFrame(u_int32_t bsn)
{
    Lock lock(m_mutex);
    for (ObjList* o = m_ackList.skipNull();o;o = o->skipNext()) {
	DataBlock* d = static_cast<DataBlock*>(o->get());
	u_int32_t seq = (d->at(1) << 16) | (d->at(2) << 8) | d->at(3);
	if (bsn != seq)
	    continue;
	m_lastAck = bsn;
	m_ackList.remove(d);
	m_ackTimer.stop();
	break;
    }
}

void SS7M2PA::setLocalStatus(unsigned int status)
{
    if (status == m_localStatus)
	return;
    DDebug(this,DebugInfo,"Local status change %s -> %s [%p]",
	lookup(m_localStatus,s_state),lookup(status,s_state),this);
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
    dumpMsg(1,M2PA,UserData,data,1,true);
    transmitMSG(1,M2PA,UserData,data,1);
}

bool SS7M2PA::control(Operation oper, NamedList* params)
{
    switch (oper) {
	case Pause:
	    m_state = OutOfService;
	    abortAlignment("Control request pause.");
	    transmitLS();
	    return true;
	case Resume:
	    if (aligned())
		return true;
	case Align:
	{
	    bool em = params && params->getBoolValue("emergency");
	    m_state = em ? ProvingEmergency : ProvingNormal;
	    if (m_autostart)
		startAlignment();
	    return true;
	}
	case Status:
	    return operational();
	default:
	    return false;
    }
}

void SS7M2PA::startAlignment(bool emergency)
{
    setLocalStatus(OutOfService);
    transmitLS();
    setLocalStatus(Alignment);
    SS7Layer2::notify();
}

void SS7M2PA::transmitLS(int streamId)
{
    if (m_transportState != Established)
	return;
    DataBlock data;
    setHeader(data);
    u_int8_t ms[4];
    ms[1] = ms[2] = ms[3] = ms[0] = 0;
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
    head[1] = (m_seqNr >> 16) & 0xff;
    head[2] = (m_seqNr >> 8) & 0xff;
    head[3] = m_seqNr & 0xff ;
    head[5] = (m_needToAck >> 16) & 0xff;
    head[6] = (m_needToAck >> 8) & 0xff;
    head[7] = m_needToAck & 0xff ;
    data.append(head,8);
}

void SS7M2PA::abortAlignment(String from)
{
    DDebug(this,DebugNote,"Aborting alignment: %s",from.c_str());
    setLocalStatus(OutOfService);
    setRemoteStatus(OutOfService);
    m_needToAck = m_lastAck = m_seqNr = 0xffffff;
    if (m_confTimer.started())
	m_confTimer.stop();
    if (m_ackTimer.started())
	m_ackTimer.stop();
    if (m_t2.started())
	m_t2.stop();
    if (m_t3.started())
	m_t3.stop();
    if (m_t4.started())
	m_t4.stop();
    if (m_t1.started())
	m_t1.stop();
    if (m_state == ProvingNormal || m_state == ProvingEmergency)
	startAlignment();
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
	    if (m_localStatus != ProvingNormal && m_localStatus != ProvingEmergency &&
		(m_localStatus == Alignment && m_t3.started()))
		return false;
	    if (m_t3.started()) {
		m_t3.stop();
		m_t4.start();
	    }
	    else if (m_state == ProvingNormal || m_state == ProvingEmergency) {
		setLocalStatus(status);
		transmitLS();
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
	    SS7Layer2::notify();
	    if (m_t3.started())
		m_t3.stop();
	    if (m_t4.started())
		m_t4.stop();
	    if (m_t1.started())
		m_t1.stop();
	    if (m_bufMsg.count())
		dequeueMsg();
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
	    if (m_localStatus == Ready) {
		abortAlignment("Received : LinkStatus Out of service, local status Ready");
		SS7Layer2::notify();
	    }
	    if ((m_state == ProvingNormal || m_state == ProvingEmergency)) {
		if (m_localStatus == Alignment) {
		    transmitLS();
		    m_t2.start();
		} else if (m_localStatus == OutOfService)
		    startAlignment();
		else
		    return false;
	    }
	    setRemoteStatus(status);
	    break;
	default:
	    Debug(this,DebugNote,"Received unknown link status message %d",status);
	    return false;
    }
    return true;
}

ObjList* SS7M2PA::recoverMSU()
{
    Lock lock(m_mutex);
    ObjList* lst = 0;
    for (;;) {
	DataBlock* pkt = static_cast<DataBlock*>(m_ackList.remove(false));
	if (!pkt)
	    break;
	if (pkt->length() > 8) {
	    SS7MSU* msu = new SS7MSU(8 + (char*)pkt->data(),pkt->length() - 8);
	    if (!lst)
		lst = new ObjList;
	    lst->append(msu);
	}
	TelEngine::destruct(pkt);
    }
    return lst;
}

void SS7M2PA::retransData()
{
    for (ObjList* o = m_ackList.skipNull();o;o = o->skipNext()) {
	DataBlock* msg = static_cast<DataBlock*>(o->get());
	u_int8_t* head = (u_int8_t*)msg->data();
	head[5] = (m_needToAck >> 16) & 0xff;
	head[6] = (m_needToAck >> 8) & 0xff;
	head[7] = m_needToAck & 0xff ;
	if (m_confTimer.started())
	    m_confTimer.stop();
	transmitMSG(1,M2PA, 1, *msg,1);
	if (!m_ackTimer.started())
	    m_ackTimer.start();
    }
}

void SS7M2PA::dequeueMsg()
{
    for (ObjList* o = m_bufMsg.skipNull();o;o = o->skipNext()) {
	DataBlock* msg = static_cast<DataBlock*>(o->get());
	if (!decodeSeq(*msg,UserData))
	    return;
	msg->cut(-8); // Remove M2PA Header
	SS7MSU msu(*msg);
	receivedMSU(msu);
	sendAck();
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
    DataBlock packet;
    increment(m_seqNr);
    setHeader(packet);
    if (m_confTimer.started())
	m_confTimer.stop();
    packet += msu;
    m_ackList.append(new DataBlock(packet));
    if (m_dumpMsg)
	dumpMsg(1,M2PA,1,packet,1,true);
    bool ok = transmitMSG(1,M2PA,1,packet,1);
    lock.drop();
    if (!m_ackTimer.started())
	m_ackTimer.start();
    return ok;
}

void SS7M2PA::notifyLayer(SignallingInterface::Notification event)
{
    switch (event) {
	case SignallingInterface::LinkDown:
	    m_transportState = Idle;
	    m_seqNr = m_needToAck = m_lastAck = 0xffffff;
	    abortAlignment("LinkDown");
	    SS7Layer2::notify();
	    break;
	case SignallingInterface::LinkUp:
	    m_transportState = Established;
	    Debug(this,DebugInfo,"Interface is up [%p]",this);
	    if (m_autostart)
		startAlignment();
	    SS7Layer2::notify();
	    break;
	default:
	    return;
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
