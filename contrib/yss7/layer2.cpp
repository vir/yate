/**
 * layer2.cpp
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
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

#include "yatess7.h"


using namespace TelEngine;

SS7MSU::~SS7MSU()
{
}

bool SS7MSU::valid() const
{
    return (3 < length()) && (length() < 273);
}

#define CASE_STR(x) case x: return #x
const char* SS7MSU::getServiceName() const
{
    switch (getSIF()) {
	CASE_STR(SNM);
	CASE_STR(MTN);
	CASE_STR(MTNS);
	CASE_STR(SCCP);
	CASE_STR(TUP);
	CASE_STR(ISUP);
	CASE_STR(DUP_C);
	CASE_STR(DUP_F);
	CASE_STR(MTP_T);
	CASE_STR(BISUP);
	CASE_STR(SISUP);
    }
    return 0;
}

const char* SS7MSU::getPriorityName() const
{
    switch (getPrio()) {
	CASE_STR(Regular);
	CASE_STR(Special);
	CASE_STR(Circuit);
	CASE_STR(Facility);
    }
    return 0;
}

const char* SS7MSU::getIndicatorName() const
{
    switch (getNI()) {
	CASE_STR(International);
	CASE_STR(SpareInternational);
	CASE_STR(National);
	CASE_STR(ReservedNational);
    }
    return 0;
}
#undef CASE_STR


void SS7Layer2::attach(SS7L2User* l2user)
{
    if (m_l2user == l2user)
	return;
    Debug(toString(),DebugStub,"Please implement SS7Layer2::attach()");
    m_l2user = l2user;
    if (!l2user)
	return;
    insert(l2user);
    l2user->attach(this);
}

unsigned int SS7Layer2::status() const
{
    return ProcessorOutage;
}

const char* SS7Layer2::statusName(unsigned int status, bool brief) const
{
    switch (status) {
	case OutOfAlignment:
	    return brief ? "O" : "Out Of Alignment";
	case NormalAlignment:
	    return brief ? "N" : "Normal Alignment";
	case EmergencyAlignment:
	    return brief ? "E" : "Emergency Alignment";
	case OutOfService:
	    return brief ? "OS" : "Out Of Service";
	case ProcessorOutage:
	    return brief ? "PO" : "Processor Outage";
	case Busy:
	    return brief ? "B" : "Busy";
	default:
	    return brief ? "?" : "Unknown Status";
    }
}

bool SS7Layer2::control(Operation oper, NamedList* params)
{
    return false;
}


SS7MTP2::SS7MTP2(unsigned int status)
    : Mutex(false),
      m_status(status), m_lStatus(OutOfService), m_rStatus(OutOfAlignment),
      m_interval(0), m_congestion(false),
      m_bsn(127), m_fsn(127), m_bib(true), m_fib(true)
{
    setName("mtp2");
}

unsigned int SS7MTP2::status() const
{
    return m_lStatus;
}

void SS7MTP2::setLocalStatus(unsigned int status)
{
    if (status == m_lStatus)
	return;
    DDebug(engine(),DebugInfo,"Local status change: %s -> %s [%p]",
	statusName(m_lStatus,true),statusName(status,true),this);
    m_lStatus = status;
}

void SS7MTP2::setRemoteStatus(unsigned int status)
{
    if (status == m_rStatus)
	return;
    DDebug(engine(),DebugInfo,"Remote status change: %s -> %s [%p]",
	statusName(m_rStatus,true),statusName(status,true),this);
    m_rStatus = status;
}

bool SS7MTP2::aligned() const
{
    return ((m_lStatus == NormalAlignment) || (m_lStatus == EmergencyAlignment)) &&
	((m_rStatus == NormalAlignment) || (m_rStatus == EmergencyAlignment));
}

bool SS7MTP2::operational() const
{
    return aligned() && !m_interval;
}

bool SS7MTP2::control(Operation oper, NamedList* params)
{
    switch (oper) {
	case Pause:
	    m_status = OutOfService;
	    abortAlignment();
	    return true;
	case Resume:
	    if (aligned())
		return true;
	    // fall-through
	case Align:
	    startAlignment();
	    return true;
	    break;
	case Status:
	    return operational();
	default:
	    return SignallingReceiver::control((SignallingInterface::Operation)oper,params);
    }
}

void SS7MTP2::timerTick(const Time& when)
{
    lock();
    bool tout = m_interval && (when >= m_interval);
    if (tout)
	m_interval = 0;
    unlock();
    if (operational()) {
	if (tout) {
	    Debug(engine(),DebugInfo,"Proving period ended, link operational [%p]",this);
	    SS7Layer2::notify();
	}
	transmitFISU();
    }
    else {
	if (tout && (m_lStatus == OutOfService)) {
	    switch (m_status) {
		case NormalAlignment:
		case EmergencyAlignment:
		    setLocalStatus(OutOfAlignment);
		    break;
		default:
		    setLocalStatus(m_status);
	    }
	}
	transmitLSSU();
    }
}

// Transmit a MSU retaining a copy for retransmissions
bool SS7MTP2::transmitMSU(const SS7MSU& msu)
{
    if (msu.length() < 3) {
	Debug(engine(),DebugWarn,"Asked to send too short MSU of length %u [%p]",
	    msu.length(),this);
	return false;
    }
    if (!operational()) {
	DDebug(engine(),DebugInfo,"Asked to send MSU while not operational [%p]",this);
	return false;
    }
    XDebug(engine(),DebugAll,"SS7MTP2::transmitMSU(%p) len=%u [%p]",
	&msu,msu.length(),this);
    // if we don't have an attached interface don't bother
    if (!iface())
	return false;

    DataBlock* packet = new DataBlock(0,3);
    *packet += msu;

    // set BSN+BIB, FSN+FIB, LENGTH in the 3 extra bytes
    unsigned char* buf = (unsigned char*)packet->data();
    buf[2] = (msu.length() > 0x3f) ? 0x3f : msu.length() & 0x3f;
    // lock the object so we can safely use member variables
    Lock lock(this);
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    m_fsn = (m_fsn + 1) & 0x7f;
    m_queue.append(packet);
    return transmitPacket(*packet,false,SignallingInterface::SS7Msu);
}

// Decode a received packet into signalling units
bool SS7MTP2::receivedPacket(const DataBlock& packet)
{
    if (packet.length() < 3) {
	XDebug(engine(),DebugMild,"Received short packet of length %u [%p]",
	    packet.length(),this);
	return false;
    }
    const unsigned char* buf = (const unsigned char*)packet.data();
    unsigned int len = buf[2] & 0x3f;
    if ((len == 0x3f) && (packet.length() > 0x42))
	len = packet.length() - 3;
    else if (len != (packet.length() - 3)) {
	XDebug(engine(),DebugMild,"Received packet with length indicator %u but length %u [%p]",
	    len,packet.length(),this);
	return false;
    }
    // packet length is valid, check sequence numbers
    unsigned char bsn = buf[0] & 0x7f;
    unsigned char fsn = buf[1] & 0x7f;
    bool bib = (buf[0] & 0x80) != 0;
    bool fib = (buf[1] & 0x80) != 0;
    // lock the object as we modify members
    lock();
    XDebug(engine(),DebugInfo,"got bsn=%u/%d fsn=%u/%d local bsn=%u/%d fsn=%u/%d [%p]",
	bsn,bib,fsn,fib,m_bsn,m_bib,m_fsn,m_fib,this);

    if ((m_rStatus == OutOfAlignment) || (m_rStatus == OutOfService)) {
	// sync sequence
	m_bsn = fsn;
	m_bib = fib;
    }
    // sequence control as explained by Q.703 5.2.2
    bool same = (fsn == m_bsn);
    bool next = false;

    // hack - use a while so we can break out
    while (!same) {
	if (len >= 3) {
	    next = (fsn == ((m_bsn + 1) & 0x7f));
	    if (next)
		break;
	}
	Debug(engine(),DebugMild,"Detected loss of %u packets",(fsn - m_bsn) & 0x7f);
	m_bib = !m_bib;
	break;
    }
    unlock();

    //TODO: implement Q.703 6.3.1

    switch (len) {
	case 2:
	    processLSSU(buf[3] + (buf[4] << 8));
	    return true;
	case 1:
	    processLSSU(buf[3]);
	    return true;
	case 0:
	    processFISU();
	    return true;
    }
    // just drop MSUs
    if (!(next && operational()))
	return false;
    m_bsn = fsn;
    SS7MSU msu((void*)(buf+3),len,false);
    bool ok = receivedMSU(msu);
    if (!ok) {
	String s;
	s.hexify(msu.data(),msu.length(),' ');
	Debug(toString(),DebugMild,"Unhandled MSU len=%u Serv: %s, Prio: %s, Net: %s, Data: %s",
	    msu.length(),msu.getServiceName(),msu.getPriorityName(),
	    msu.getIndicatorName(),s.c_str());
    }
    msu.clear(false);
    return ok;
}

// Process incoming FISU
void SS7MTP2::processFISU()
{
    if (!aligned())
	transmitLSSU();
}

// Process incoming LSSU
void SS7MTP2::processLSSU(unsigned int status)
{
    status &= 0x07;
    bool unaligned = true;
    switch (m_rStatus) {
	case NormalAlignment:
	case EmergencyAlignment:
	    unaligned = false;
    }
    if (status == Busy) {
	if (unaligned)
	    abortAlignment();
	else
	    m_congestion = true;
	return;
    }
    setRemoteStatus(status);
    // cancel any timer except aborted alignment
    switch (status) {
	case OutOfAlignment:
	case NormalAlignment:
	case EmergencyAlignment:
	    if (!(unaligned && startProving()))
		setLocalStatus(m_status);
	    break;
	default:
	    if (!m_interval)
		abortAlignment();
	    else if (m_lStatus != OutOfService)
		m_interval = 0;
    }
}

// Emit a locally generated LSSU
bool SS7MTP2::transmitLSSU(unsigned int status)
{
    unsigned char buf[5];
    buf[2] = 1;
    buf[3] = status & 0xff;
    status = (status >> 8) & 0xff;
    if (status) {
	// we need 2-byte LSSU to fit
	buf[2] = 2;
	buf[4] = status;
    }
    // lock the object so we can safely use member variables
    lock();
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    DataBlock packet(buf,buf[2]+3,false);
    bool ok = transmitPacket(packet,true,SignallingInterface::SS7Lssu);
    unlock();
    packet.clear(false);
    return ok;
}

// Emit a locally generated FISU
bool SS7MTP2::transmitFISU()
{
    unsigned char buf[3];
    buf[2] = 0;
    // lock the object so we can safely use member variables
    lock();
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    DataBlock packet(buf,3,false);
    bool ok = transmitPacket(packet,true,SignallingInterface::SS7Fisu);
    unlock();
    packet.clear(false);
    return ok;
}

void SS7MTP2::startAlignment(bool emergency)
{
    lock();
    m_status = emergency ? EmergencyAlignment : NormalAlignment;
    m_interval = 0;
    setLocalStatus(OutOfAlignment);
    m_queue.clear();
    unlock();
    transmitLSSU();
}

void SS7MTP2::abortAlignment()
{
    lock();
    setLocalStatus(OutOfService);
    m_interval = Time::now() + 1000000;
    m_queue.clear();
    unlock();
    SS7Layer2::notify();
}

bool SS7MTP2::startProving()
{
    if (m_interval || !aligned())
	return false;
    lock();
    bool emg = (m_rStatus == EmergencyAlignment);
    Debug(engine(),DebugInfo,"Starting %s proving interval [%p]",
	emg ? "emergency" : "normal",this);
    // proving interval is defined in octet transmission times
    u_int64_t interval = emg ? 4096 : 65536;
    // FIXME: assuming 64 kbit/s, 125 usec/octet
    m_interval = Time::now() + (125 * interval);
    unlock();
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
