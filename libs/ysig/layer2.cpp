/**
 * layer2.cpp
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
#include <string.h>


using namespace TelEngine;

static TokenDict s_dict_prio[] = {
	{"regular",  SS7MSU::Regular},
	{"special",  SS7MSU::Special},
	{"circuit",  SS7MSU::Circuit},
	{"facility", SS7MSU::Facility},
	{0,0}
	};

static TokenDict s_dict_netind[] = {
	{"international",      SS7MSU::International},
	{"spareinternational", SS7MSU::SpareInternational},
	{"national",           SS7MSU::National},
	{"reservednational",   SS7MSU::ReservedNational},
	{0,0}
	};


SS7MSU::SS7MSU(unsigned char sio, const SS7Label label, void* value, unsigned int len)
{
    DataBlock::assign(0,1 + label.length() + len);
    unsigned char* d = (unsigned char*)data();
    *d++ = sio;
    label.store(d);
    d += label.length();
    if (value && len)
	::memcpy(d,value,len);
}

SS7MSU::SS7MSU(unsigned char sif, unsigned char ssf, const SS7Label label, void* value, unsigned int len)
{
    DataBlock::assign(0,1 + label.length() + len);
    unsigned char* d = (unsigned char*)data();
    *d++ = (sif & 0x0f) | (ssf & 0xf0);
    label.store(d);
    d += label.length();
    if (value && len)
	::memcpy(d,value,len);
}

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

unsigned char SS7MSU::getPriority(const char* name, unsigned char defVal)
{
    return (unsigned char)lookup(name,s_dict_prio,defVal);
}

unsigned char SS7MSU::getNetIndicator(const char* name, unsigned char defVal)
{
    return (unsigned char)lookup(name,s_dict_netind,defVal);
}


void SS7Layer2::attach(SS7L2User* l2user)
{
    Lock lock(m_l2userMutex);
    if (m_l2user == l2user)
	return;
    SS7L2User* tmp = m_l2user;
    m_l2user = l2user;
    lock.drop();
    if (tmp) {
	const char* name = 0;
	if (engine() && engine()->find(tmp)) {
	    name = tmp->toString().safe();
	    tmp->detach(this);
	}
	Debug(this,DebugAll,"Detached L2 user (%p,'%s') [%p]",tmp,name,this);
    }
    if (!l2user)
	return;
    Debug(this,DebugAll,"Attached L2 user (%p,'%s') [%p]",
	l2user,l2user->toString().safe(),this);
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


SS7MTP2::SS7MTP2(const NamedList& params, unsigned int status)
    : Mutex(true),
      m_status(status), m_lStatus(OutOfService), m_rStatus(OutOfAlignment),
      m_interval(0), m_resend(0), m_abort(0), m_congestion(false),
      m_bsn(127), m_fsn(127), m_bib(true), m_fib(true),
      m_lastBsn(127), m_lastBib(true), m_errors(0),
      m_resendMs(250), m_abortMs(5000), m_dumper(0)
{
    setName(params.getValue("debugname","mtp2"));

    const char* fn = params.getValue("mtp2dump");
    if (fn)
	setDumper(SignallingDumper::create(this,fn,SignallingDumper::Mtp2));
}

SS7MTP2::~SS7MTP2()
{
    setDumper();
}

unsigned int SS7MTP2::status() const
{
    return m_lStatus;
}

void SS7MTP2::setLocalStatus(unsigned int status)
{
    if (status == m_lStatus)
	return;
    DDebug(this,DebugInfo,"Local status change: %s -> %s [%p]",
	statusName(m_lStatus,true),statusName(status,true),this);
    m_lStatus = status;
}

void SS7MTP2::setRemoteStatus(unsigned int status)
{
    if (status == m_rStatus)
	return;
    DDebug(this,DebugInfo,"Remote status change: %s -> %s [%p]",
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
	    startAlignment(params && params->getBoolValue("emergency"));
	    return true;
	case Status:
	    return operational();
	default:
	    return SignallingReceiver::control((SignallingInterface::Operation)oper,params);
    }
}

bool SS7MTP2::notify(SignallingInterface::Notification event)
{
    switch (event) {
	case SignallingInterface::LinkDown:
	    Debug(this,DebugWarn,"Interface is down - realigning [%p]",this);
	    abortAlignment();
	    break;
	case SignallingInterface::LinkUp:
	    Debug(this,DebugInfo,"Interface is up [%p]",this);
	    break;
	default:
	    XDebug(this,DebugMild,"Got error %u: %s [%p]",
		event,lookup(event,SignallingInterface::s_notifName),this);
	    if (++m_errors >= 4) {
		Debug(this,DebugWarn,"Got %d errors - realigning [%p]",m_errors,this);
		abortAlignment();
	    }
    }
    return true;
}

void SS7MTP2::timerTick(const Time& when)
{
    lock();
    bool tout = m_interval && (when >= m_interval);
    if (tout)
	m_interval = 0;
    bool aborting = m_abort && (when >= m_abort);
    if (aborting)
	m_abort = m_resend = 0;
    bool resend = m_resend && (when >= m_resend);
    if (resend)
	m_resend = 0;
    unlock();
    if (aborting) {
	Debug(this,DebugWarn,"Timeout for MSU acknowledgement, realigning [%p]",this);
	abortAlignment();
	return;
    }
    if (operational()) {
	if (tout) {
	    Debug(this,DebugInfo,"Proving period ended, link operational [%p]",this);
	    lock();
	    unsigned int q = m_queue.count();
	    if (q >= 64) {
		// there shouldn't have been that many queued MSUs
		Debug(this,DebugWarn,"Cleaning %u queued MSUs from proved link! [%p]",q,this);
		m_queue.clear();
	    }
	    else if (q) {
		Debug(this,DebugWarn,"Changing FSN of %u MSUs queued in proved link! [%p]",q,this);
		// transmit a FISU just before the bunch of MSUs
		transmitFISU();
		resend = true;
		// reset the FSN of packets still waiting in queue
		ObjList* l = m_queue.skipNull();
		for (; l; l = l->skipNext()) {
		    DataBlock* packet = static_cast<DataBlock*>(l->get());
		    unsigned char* buf = (unsigned char*)packet->data();
		    // update the FSN/FIB in packet, BSN/BIB will be updated later
		    m_fsn = (m_fsn + 1) & 0x7f;
		    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
		}
		Debug(this,DebugNote,"Renumbered %u packets, last FSN=%u [%p]",
		    q,m_fsn,this);
	    }
	    unlock();
	    SS7Layer2::notify();
	}
	if (resend) {
	    int c = 0;
	    lock();
	    ObjList* l = m_queue.skipNull();
	    for (; l; l = l->skipNext()) {
		DataBlock* packet = static_cast<DataBlock*>(l->get());
		unsigned char* buf = (unsigned char*)packet->data();
		// update the BSN/BIB in packet
		buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
		unsigned char pfsn = buf[1] & 0x7f;
		// check if we are retransmitting the last ACKed packet
		if (pfsn == m_lastBsn)
		    m_lastBsn = (m_lastBsn - 1) & 0x7f;
		DDebug(this,DebugInfo,"Resending packet %p with FSN=%u [%p]",
		    packet,pfsn,this);
		transmitPacket(*packet,false,SignallingInterface::SS7Msu);
		c++;
	    }
	    m_resend = Time::now() + (1000 * m_resendMs);
	    Debug(this,DebugNote,"Resent %d packets, last bsn=%u/%u [%p]",
		c,m_lastBsn,m_lastBib,this);
	    unlock();
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
	Debug(this,DebugWarn,"Asked to send too short MSU of length %u [%p]",
	    msu.length(),this);
	return false;
    }
    if (!operational()) {
	DDebug(this,DebugInfo,"Asked to send MSU while not operational [%p]",this);
	return false;
    }
    XDebug(this,DebugAll,"SS7MTP2::transmitMSU(%p) len=%u [%p]",
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
    m_fsn = (m_fsn + 1) & 0x7f;
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    DDebug(this,DebugInfo,"New local bsn=%u/%d fsn=%u/%d [%p]",
	m_bsn,m_bib,m_fsn,m_fib,this);
    m_queue.append(packet);
    DDebug(this,DebugInfo,"There are %u packets in queue [%p]",
	m_queue.count(),this);
    bool ok = false;
    if (operational()) {
	ok = transmitPacket(*packet,false,SignallingInterface::SS7Msu);
	transmitFISU();
    }
    if (!m_abort)
	m_abort = Time::now() + (1000 * m_abortMs);
    if (!m_resend)
	m_resend = Time::now() + (1000 * m_resendMs);
    return ok;
}

// Remove the MSUs in the queue, the upper layer will move them to another link
ObjList* SS7MTP2::recoverMSU()
{
    lock();
    ObjList* lst = 0;
    for (;;) {
	GenObject* pkt = m_queue.remove(false);
	if (!pkt)
	    break;
	if (!lst)
	    lst = new ObjList;
	lst->append(pkt);
    }
    unlock();
    return lst;
}

// Decode a received packet into signalling units
bool SS7MTP2::receivedPacket(const DataBlock& packet)
{
    if (m_dumper)
	m_dumper->dump(packet,false,sls());
    if (packet.length() < 3) {
	XDebug(this,DebugMild,"Received short packet of length %u [%p]",
	    packet.length(),this);
	return false;
    }
    const unsigned char* buf = (const unsigned char*)packet.data();
    unsigned int len = buf[2] & 0x3f;
    if ((len == 0x3f) && (packet.length() > 0x42))
	len = packet.length() - 3;
    else if (len != (packet.length() - 3)) {
	XDebug(this,DebugMild,"Received packet with length indicator %u but length %u [%p]",
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
    XDebug(this,DebugAll,"got bsn=%u/%d fsn=%u/%d local bsn=%u/%d fsn=%u/%d [%p]",
	bsn,bib,fsn,fib,m_bsn,m_bib,m_fsn,m_fib,this);

    if ((m_rStatus == OutOfAlignment) || (m_rStatus == OutOfService)) {
	// sync sequence
	m_bsn = fsn;
	m_bib = fib;
	m_lastBsn = bsn;
	m_lastBib = bib;
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
	Debug(this,DebugMild,"Detected loss of %u packets [%p]",
	    (fsn - m_bsn) & 0x7f,this);
	m_bib = !m_bib;
	DDebug(this,DebugInfo,"New local bsn=%u/%d fsn=%u/%d [%p]",
	    m_bsn,m_bib,m_fsn,m_fib,this);
	break;
    }

    if (m_lastBib != bib) {
	Debug(this,DebugMild,"Remote requested resend remote bsn=%u local fsn=%u [%p]",
	    bsn,m_fsn,this);
	m_lastBib = bib;
	m_resend = Time::now();
    }
    if (m_lastBsn != bsn) {
	Debug(this,DebugNote,"Unqueueing packets in range %u - %u [%p]",
	    m_lastBsn,bsn,this);
	m_lastBsn = bsn;
	int c = 0;
	for (;;) {
	    DataBlock* packet = static_cast<DataBlock*>(m_queue.get());
	    if (!packet) {
		// all packets confirmed - stop resending
		m_resend = 0;
		m_abort = 0;
		break;
	    }
	    unsigned char pfsn = ((const unsigned char*)packet->data())[1] & 0x7f;
	    char diff = bsn - pfsn;
	    if (diff < 0)
		break;
	    c++;
	    DDebug(this,DebugInfo,"Unqueueing packet %p with FSN=%u [%p]",
		packet,pfsn,this);
	    m_queue.remove(packet);
	}
	if (c) {
	    Debug(this,DebugNote,"Unqueued %d packets up to FSN=%u [%p]",c,bsn,this);
	    m_abort = m_resend ? Time::now() + (1000 * m_abortMs) : 0;
	}
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
    DDebug(this,DebugInfo,"New local bsn=%u/%d fsn=%u/%d [%p]",
	m_bsn,m_bib,m_fsn,m_fib,this);
    SS7MSU msu((void*)(buf+3),len,false);
    bool ok = receivedMSU(msu);
    if (!ok) {
	String s;
	s.hexify(msu.data(),msu.length(),' ');
	Debug(this,DebugMild,"Unhandled MSU len=%u Serv: %s, Prio: %s, Net: %s, Data: %s",
	    msu.length(),msu.getServiceName(),msu.getPriorityName(),
	    msu.getIndicatorName(),s.c_str());
    }
    msu.clear(false);
    return ok;
}

bool SS7MTP2::txPacket(const DataBlock& packet, bool repeat, SignallingInterface::PacketType type)
{
    if (transmitPacket(packet,repeat,type)) {
	if (m_dumper)
	    m_dumper->dump(packet,true,sls());
	return true;
    }
    return false;
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
    XDebug(this,DebugAll,"Process LSSU with status %s (L:%s R:%s)",
	statusName(status,true),statusName(m_lStatus,true),statusName(m_rStatus,true));
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
    XDebug(this,DebugAll,"Transmit LSSU with status %s",statusName(buf[3],true));
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
    unsigned int q = m_queue.count();
    if (q)
	Debug(this,DebugWarn,"Starting alignment with %u queued MSUs! [%p]",q,this);
    else
	Debug(this,DebugInfo,"Starting %s alignment [%p]",emergency?"emergency":"normal",this);
    m_status = emergency ? EmergencyAlignment : NormalAlignment;
    m_abort = m_resend = m_interval = 0;
    setLocalStatus(OutOfAlignment);
    m_fsn = 127;
    m_fib = true;
    unlock();
    transmitLSSU();
}

void SS7MTP2::abortAlignment()
{
    lock();
    DDebug(this,DebugNote,"Aborting alignment");
    setLocalStatus(OutOfService);
    m_interval = Time::now() + 1000000;
    m_abort = m_resend = 0;
    m_errors = 0;
    m_fsn = 127;
    m_fib = true;
    unlock();
    SS7Layer2::notify();
}

bool SS7MTP2::startProving()
{
    if (m_interval)
	return false;
    if ((m_status != NormalAlignment) && (m_status != EmergencyAlignment))
	return false;
    if ((m_rStatus != NormalAlignment) && (m_rStatus != EmergencyAlignment))
	return false;
    lock();
    bool emg = (m_rStatus == EmergencyAlignment);
    Debug(this,DebugInfo,"Starting %s proving interval [%p]",
	emg ? "emergency" : "normal",this);
    // proving interval is defined in octet transmission times
    u_int64_t interval = emg ? 4096 : 65536;
    // FIXME: assuming 64 kbit/s, 125 usec/octet
    m_interval = Time::now() + (125 * interval);
    unlock();
    return true;
}

void SS7MTP2::setDumper(SignallingDumper* dumper)
{
    if (dumper == m_dumper)
	return;
    SignallingDumper* tmp = m_dumper;
    m_dumper = dumper;
    delete tmp;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
