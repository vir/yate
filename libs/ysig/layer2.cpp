/**
 * layer2.cpp
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
#include <string.h>


using namespace TelEngine;

static const TokenDict s_dict_prio[] = {
    { "regular",  SS7MSU::Regular },
    { "special",  SS7MSU::Special },
    { "circuit",  SS7MSU::Circuit },
    { "facility", SS7MSU::Facility },
    { 0, 0 }
};

static const TokenDict s_dict_netind[] = {
    { "international",      SS7MSU::International },
    { "spareinternational", SS7MSU::SpareInternational },
    { "national",           SS7MSU::National },
    { "reservednational",   SS7MSU::ReservedNational },
    { 0, 0 }
};

static const TokenDict s_dict_control[] = {
    { "pause",  SS7Layer2::Pause },
    { "resume", SS7Layer2::Resume },
    { "align",  SS7Layer2::Align },
    { 0, 0 }
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

void SS7Layer2::timerTick(const Time& when)
{
    SignallingComponent::timerTick(when);
    if (!m_l2userMutex.lock(SignallingEngine::maxLockWait()))
	return;
    RefPointer<SS7L2User> tmp = m_notify ? m_l2user : 0;
    m_notify = false;
    m_l2userMutex.unlock();
    if (tmp) {
	XDebug(this,DebugAll,"SS7Layer2 notifying user [%p]",this);
	tmp->notify(this);
    }
}

void SS7Layer2::notify()
{
    unsigned int wasUp = 0;
    bool doNotify = false;
    if (!operational()) {
	wasUp = upTime();
	m_lastUp = 0;
	doNotify = (wasUp != 0);
    }
    else if (!m_lastUp) {
	m_lastUp = Time::secNow();
	doNotify = true;
    }
    m_l2userMutex.lock();
    m_notify = true;
    m_l2userMutex.unlock();
    if (doNotify && engine()) {
	String text(statusName());
	if (wasUp)
	    text << ", was up " << wasUp;
	NamedList params("");
	params.addParam("from",toString());
	params.addParam("type","ss7-layer2");
	params.addParam("operational",String::boolText(operational()));
	params.addParam("text",text);
	engine()->notify(this,params);
    }
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

bool SS7Layer2::control(NamedList& params)
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
    if (!(cmp && toString() == cmp))
	return false;
    return TelEngine::controlReturn(&params,(cmd >= 0) && control((Operation)cmd,&params));
}

bool SS7Layer2::getEmergency(NamedList* params, bool emg) const
{
    if (m_autoEmergency && !emg) {
	const SS7MTP3* mtp3 = YOBJECT(SS7MTP3,m_l2user);
	if (mtp3 && !mtp3->linksActive())
	    emg = true;
    }
    if (params)
	emg = params->getBoolValue(YSTRING("emergency"),emg);
    return emg;
}

bool SS7Layer2::inhibit(int setFlags, int clrFlags)
{
    int old = m_inhibited;
    m_inhibited = (m_inhibited | setFlags) & ~clrFlags;
    if (old != m_inhibited || (setFlags & clrFlags)) {
	bool cycle = (setFlags & Inactive) && operational();
	if (cycle)
	    control(Pause);
	Debug(this,DebugNote,"Link inhibition changed 0x%02X -> 0x%02X [%p]",
	    old,m_inhibited,this);
	if (operational())
	    notify();
	if (cycle)
	    control(Resume);
    }
    return true;
}


SS7MTP2::SS7MTP2(const NamedList& params, unsigned int status)
    : SignallingComponent(params.safe("SS7MTP2"),&params,"ss7-mtp2"),
      SignallingDumpable(SignallingDumper::Mtp2),
      Mutex(true,"SS7MTP2"),
      m_status(status), m_lStatus(OutOfService), m_rStatus(OutOfAlignment),
      m_interval(0), m_resend(0), m_abort(0), m_fillTime(0), m_congestion(false),
      m_bsn(127), m_fsn(127), m_bib(true), m_fib(true),
      m_lastFsn(128), m_lastBsn(127), m_lastBib(true), m_errors(0), m_maxErrors(64),
      m_resendMs(250), m_abortMs(5000), m_fillIntervalMs(20), m_fillLink(true),
      m_autostart(false), m_flushMsus(true)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7MTP2::SS7MTP2(%p,%s) [%p]%s",
	    &params,statusName(true),this,tmp.c_str());
    }
#endif
    m_fillLink = params.getBoolValue(YSTRING("filllink"),m_fillLink);
    m_maxErrors = params.getIntValue(YSTRING("maxerrors"),64);
    if (m_maxErrors < 8)
	m_maxErrors = 8;
    else if (m_maxErrors > 256)
	m_maxErrors = 256;
    setDumper(params.getValue(YSTRING("layer2dump")));
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
    m_fillTime = 0;
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

bool SS7MTP2::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7MTP2::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue(YSTRING("debuglevel_mtp2"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	m_autoEmergency = config->getBoolValue(YSTRING("autoemergency"),true);
	unsigned int maxErrors = config->getIntValue(YSTRING("maxerrors"),m_maxErrors);
	if (maxErrors < 8)
	    m_maxErrors = 8;
	else if (maxErrors > 256)
	    m_maxErrors = 256;
	else
	    m_maxErrors = maxErrors;
    }
    m_autostart = !config || config->getBoolValue(YSTRING("autostart"),true);
    m_flushMsus = !config || config->getBoolValue(YSTRING("flushmsus"),true);
    if (config && !iface()) {
	NamedList params("");
	if (resolveConfig(YSTRING("sig"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    params.addParam("basename",params);
	    params.addParam("protocol","ss7");
	    int rx = params.getIntValue(YSTRING("rxunderrun"));
	    if ((rx > 0) && (rx < 25))
		params.setParam("rxunderrun","25");
	    SignallingInterface* ifc = YSIGCREATE(SignallingInterface,&params);
	    if (!ifc)
		return false;
	    SignallingReceiver::attach(ifc);
	    if (!(ifc->initialize(&params) && control((Operation)SignallingInterface::Enable,&params)))
		TelEngine::destruct(SignallingReceiver::attach(0));
	}
    }
    return iface() && control(Resume,const_cast<NamedList*>(config));
}

bool SS7MTP2::control(Operation oper, NamedList* params)
{
    if (params) {
	lock();
	m_fillLink = params->getBoolValue(YSTRING("filllink"),m_fillLink);
	m_autoEmergency = params->getBoolValue(YSTRING("autoemergency"),m_autoEmergency);
	m_autostart = params->getBoolValue(YSTRING("autostart"),m_autostart);
	m_flushMsus = params->getBoolValue(YSTRING("flushmsus"),m_flushMsus);
	unsigned int maxErrors = params->getIntValue(YSTRING("maxerrors"),m_maxErrors);
	if (maxErrors < 8)
	    m_maxErrors = 8;
	else if (maxErrors > 256)
	    m_maxErrors = 256;
	else
	    m_maxErrors = maxErrors;
	// The following are for test purposes
	if (params->getBoolValue(YSTRING("toggle-bib")))
	    m_bib = !m_bib;
	if (params->getBoolValue(YSTRING("toggle-fib")))
	    m_fib = !m_fib;
	int tmp = params->getIntValue(YSTRING("change-fsn"));
	if (tmp)
	    m_fsn = (m_fsn + tmp) & 0x7f;
	unlock();
	tmp = params->getIntValue(YSTRING("send-lssu"),-1);
	if (tmp >= 0)
	    transmitLSSU(tmp);
	if (params->getBoolValue(YSTRING("send-fisu")))
	    transmitFISU();
	if (params->getBoolValue(YSTRING("simulate-error")))
	    notify(SignallingInterface::HardwareError);
    }
    switch (oper) {
	case Pause:
	    abortAlignment(false);
	    return TelEngine::controlReturn(params,true);
	case Resume:
	    if (aligned() || !m_autostart)
		return TelEngine::controlReturn(params,true);
	    // fall-through
	case Align:
	    startAlignment(getEmergency(params));
	    return TelEngine::controlReturn(params,true);
	case Status:
	    return TelEngine::controlReturn(params,operational());
	default:
	    return SignallingReceiver::control((SignallingInterface::Operation)oper,params);
    }
}

bool SS7MTP2::notify(SignallingInterface::Notification event)
{
    switch (event) {
	case SignallingInterface::LinkDown:
	    Debug(this,DebugWarn,"Interface is down - realigning [%p]",this);
	    abortAlignment(m_autostart);
	    break;
	case SignallingInterface::LinkUp:
	    Debug(this,DebugInfo,"Interface is up [%p]",this);
	    control(Resume);
	    break;
	default:
	    XDebug(this,DebugMild,"Got error %u: %s [%p]",
		event,lookup(event,SignallingInterface::s_notifName),this);
	    {
		unsigned int err = (m_errors += 256) >> 8;
		if (err >= (operational() ? m_maxErrors :
		    ((m_rStatus == EmergencyAlignment) ? 1 : 4))) {
		    Debug(this,DebugWarn,"Got %u errors - realigning [%p]",err,this);
		    abortAlignment(m_autostart);
		}
	    }
    }
    return true;
}

void SS7MTP2::timerTick(const Time& when)
{
    SS7Layer2::timerTick(when);
    if (!lock(SignallingEngine::maxLockWait()))
	return;
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
	abortAlignment(m_autostart);
	return;
    }
    if (operational()) {
	if (tout) {
	    Debug(this,DebugInfo,"Proving period ended, link operational [%p]",this);
	    lock();
	    m_lastSeqRx = -1;
	    unsigned int q = m_queue.count();
	    if (!q)
		;
	    else if (m_flushMsus || q >= 64) {
		// there shouldn't have been that many queued MSUs
		Debug(this,DebugWarn,"Cleaning %u queued MSUs from proved link! [%p]",q,this);
		m_queue.clear();
	    }
	    else {
		Debug(this,DebugNote,"Changing FSN of %u MSUs queued in proved link! [%p]",q,this);
		// transmit a FISU just before the bunch of MSUs
		transmitFISU();
		resend = true;
		// reset the FSN of packets still waiting in queue
		m_lastBsn = m_fsn;
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
	    m_fib = m_lastBib;
	    ObjList* l = m_queue.skipNull();
	    for (; l; l = l->skipNext()) {
		DataBlock* packet = static_cast<DataBlock*>(l->get());
		unsigned char* buf = (unsigned char*)packet->data();
		// update the BSN/BIB in packet
		buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
		// also adjust the FIB but not FSN
		if (m_fib)
		    buf[1] |= 0x80;
		else
		    buf[1] &= 0x7f;
		Debug(this,DebugInfo,"Resending packet %p with FSN=%u [%p]",
		    packet,buf[1] & 0x7f,this);
		txPacket(*packet,false,SignallingInterface::SS7Msu);
		c++;
	    }
	    if (c) {
		m_resend = Time::now() + (1000 * m_resendMs);
		m_fillTime = 0;
		Debug(this,DebugInfo,"Resent %d packets, last bsn=%u/%u [%p]",
		    c,m_lastBsn,m_lastBib,this);
	    }
	    unlock();
	}
    }
    else if (tout) {
	switch (m_lStatus) {
	    case OutOfService:
		if (m_status != OutOfService)
		    setLocalStatus(OutOfAlignment);
		break;
	    case OutOfAlignment:
		Debug(this,DebugMild,"Initial alignment timed out, retrying");
		break;
	}
    }
    if (when >= m_fillTime) {
	if (operational())
	    transmitFISU();
	else
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
#ifdef XDEBUG
    String tmp;
    tmp.hexify((void*)msu.data(),msu.length(),' ');
    XDebug(this,DebugAll,"SS7MTP2::transmitMSU(%p) len=%u: %s [%p]",
	&msu,msu.length(),tmp.c_str(),this);
#endif
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
    m_fillTime = 0;
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    DDebug(this,DebugInfo,"New local bsn=%u/%d fsn=%u/%d [%p]",
	m_bsn,m_bib,m_fsn,m_fib,this);
    m_queue.append(packet);
    DDebug(this,DebugInfo,"There are %u packets in queue [%p]",
	m_queue.count(),this);
    bool ok = false;
    if (operational()) {
	ok = txPacket(*packet,false,SignallingInterface::SS7Msu);
	transmitFISU();
    }
    if (!m_abort)
	m_abort = Time::now() + (1000 * m_abortMs);
    if (!m_resend)
	m_resend = Time::now() + (1000 * m_resendMs);
    return ok;
}

// Remove the MSUs in the queue, the upper layer will move them to another link
void SS7MTP2::recoverMSU(int sequence)
{
    Debug(this,DebugInfo,"Recovering MSUs from sequence %d",sequence);
    for (;;) {
	lock();
	DataBlock* pkt = static_cast<DataBlock*>(m_queue.remove(false));
	unlock();
	if (!pkt)
	    break;
	unsigned char* head = pkt->data(0,4);
	if (head) {
	    int seq = head[1] & 0x7f;
	    if (sequence < 0 || ((seq - sequence) & 0x7f) < 0x3f) {
		sequence = -1;
		SS7MSU msu(head + 3,pkt->length() - 3);
		recoveredMSU(msu);
	    }
	    else
		Debug(this,DebugAll,"Not recovering MSU with seq=%d, requested %d",
		    seq,sequence);
	}
	TelEngine::destruct(pkt);
    }
}

// Decode a received packet into signalling units
bool SS7MTP2::receivedPacket(const DataBlock& packet)
{
    dump(packet,false,sls());
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

    // adjust error counter
    if (m_errors && operational())
	m_errors--;
    // process LSSU and FISU to detect link status changes
    switch (len) {
	case 2:
	    processLSSU(buf[3] + (buf[4] << 8));
	    break;
	case 1:
	    processLSSU(buf[3]);
	    break;
	case 0:
	    processFISU();
	    break;
    }

    // check sequence numbers
    unsigned char bsn = buf[0] & 0x7f;
    unsigned char fsn = buf[1] & 0x7f;
    bool bib = (buf[0] & 0x80) != 0;
    bool fib = (buf[1] & 0x80) != 0;
    // lock the object as we modify members
    lock();
    // sequence control as explained by Q.703 5.2.2
    unsigned char diff = (fsn - m_bsn) & 0x7f;
    XDebug(this,DebugAll,"got bsn=%u/%d fsn=%u/%d local bsn=%u/%d fsn=%u/%d diff=%u len=%u [%p]",
	bsn,bib,fsn,fib,m_bsn,m_bib,m_fsn,m_fib,diff,len,this);
    if (aligned()) {
	// received FSN should be only 1 ahead of last we handled
	if (diff > 1) {
	    if (diff < 64)
		Debug(this,DebugMild,"We lost %u packets, remote fsn=%u local bsn=%u [%p]",
		    (diff - 1),fsn,m_bsn,this);
	    if (fsn != m_lastFsn) {
		m_lastFsn = fsn;
		// toggle BIB to request immediate retransmission
		m_bib = !m_bib;
		DDebug(this,DebugInfo,"New local bsn=%u/%d fsn=%u/%d [%p]",
		    m_bsn,m_bib,m_fsn,m_fib,this);
	    }
	}
	else
	    m_lastFsn = 128;

	if (m_lastBib != bib) {
	    Debug(this,DebugNote,"Remote requested resend remote bsn=%u local fsn=%u [%p]",
		bsn,m_fsn,this);
	    m_lastBib = bib;
	    m_resend = Time::now();
	}
	unqueueAck(bsn);
	// end proving now if received MSU with correct sequence
	if (m_interval && (diff == 1))
	    m_interval = Time::now();
    }
    else {
	// keep sequence numbers in sync with the remote
	m_bsn = fsn;
	m_bib = fib;
	m_lastBsn = bsn;
	m_lastBib = bib;
	m_fillTime = 0;
    }
    unlock();

    if (len < 3)
	return true;
    // just drop MSUs if not operational or out of sequence
    if (!((diff == 1) && operational()))
	return false;
    m_lastSeqRx = m_bsn = fsn;
    m_fillTime = 0;
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

// Remove from send queue confirmed packets up to received BSN
void SS7MTP2::unqueueAck(unsigned char bsn)
{
    if (m_lastBsn == bsn)
	return;
    // positive acknowledgement - Q.703 6.3.1
    DDebug(this,DebugNote,"Unqueueing packets in range %u - %u [%p]",
	m_lastBsn,bsn,this);
    int c = 0;
    for (;;) {
	unsigned char efsn = (m_lastBsn + 1) & 0x7f;
	DataBlock* packet = static_cast<DataBlock*>(m_queue.get());
	if (!packet) {
	    Debug(this,DebugMild,"Queue empty while expecting packet with FSN=%u [%p]",
		efsn,this);
	    m_lastBsn = bsn;
	    // all packets confirmed - stop resending
	    m_resend = 0;
	    m_abort = 0;
	    break;
	}
	unsigned char pfsn = ((const unsigned char*)packet->data())[1] & 0x7f;
	if (pfsn != efsn)
	    Debug(this,DebugMild,"Found in queue packet with FSN=%u expected %u [%p]",
		pfsn,efsn,this);
	c++;
	XDebug(this,DebugInfo,"Unqueueing packet %p with FSN=%u [%p]",
	    packet,pfsn,this);
	m_queue.remove(packet);
	m_lastBsn = pfsn;
	if (pfsn == bsn) {
	    if (m_queue.count() == 0) {
		// all packets confirmed - stop resending
		m_resend = 0;
		m_abort = 0;
	    }
	    break;
	}
    }
    if (c) {
	DDebug(this,DebugNote,"Unqueued %d packets up to FSN=%u [%p]",c,bsn,this);
	m_abort = m_resend ? Time::now() + (1000 * m_abortMs) : 0;
    }
}

// Transmit packet to interface, dump it if successfull
bool SS7MTP2::txPacket(const DataBlock& packet, bool repeat, SignallingInterface::PacketType type)
{
    if (transmitPacket(packet,repeat,type)) {
	dump(packet,true,sls());
	return true;
    }
    return false;
}

// Process incoming FISU
void SS7MTP2::processFISU()
{
    if (m_fillLink && !aligned())
	m_fillTime = 0;
}

// Process incoming LSSU
void SS7MTP2::processLSSU(unsigned int status)
{
    status &= 0x07;
    XDebug(this,DebugAll,"Process LSSU with status %s (L:%s R:%s)",
	statusName(status,true),statusName(m_lStatus,true),statusName(m_rStatus,true));
    bool unaligned = !aligned();
    setRemoteStatus(status);
    if (status == Busy) {
	if (unaligned)
	    abortAlignment(m_autostart);
	else
	    m_congestion = true;
	return;
    }
    // cancel any timer except aborted or initial alignment
    switch (status) {
	case OutOfAlignment:
	case NormalAlignment:
	case EmergencyAlignment:
	    if (m_lStatus == OutOfService) {
		if (m_status != OutOfService)
		    setLocalStatus(OutOfAlignment);
		break;
	    }
	    if (!(unaligned && startProving()))
		setLocalStatus(m_status);
	    break;
	default:
	    if (!m_interval) {
		if (m_status != OutOfService)
		    abortAlignment(m_autostart);
	    }
	    else if (m_lStatus != OutOfService && m_lStatus != OutOfAlignment)
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
    bool repeat = m_fillLink && (m_status != OutOfService);
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    DataBlock packet(buf,buf[2]+3,false);
    XDebug(this,DebugAll,"Transmit LSSU with status %s",statusName(buf[3],true));
    bool ok = txPacket(packet,repeat,SignallingInterface::SS7Lssu);
    m_fillTime = Time::now() + (1000 * m_fillIntervalMs);
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
    bool ok = txPacket(packet,m_fillLink,SignallingInterface::SS7Fisu);
    m_fillTime = Time::now() + (1000 * m_fillIntervalMs);
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
	Debug(this,DebugInfo,"Starting %s alignment [%p]",
	    (emergency ? "emergency" : "normal"),this);
    m_bsn = m_fsn = 127;
    m_bib = m_fib = true;
    if (m_lStatus != OutOfService) {
	setLocalStatus(OutOfService);
	unlock();
	transmitLSSU();
	lock();
    }
    m_status = emergency ? EmergencyAlignment : NormalAlignment;
    m_abort = m_resend = 0;
    setLocalStatus(OutOfAlignment);
    m_interval = Time::now() + 5000000;
    unlock();
    transmitLSSU();
    SS7Layer2::notify();
}

void SS7MTP2::abortAlignment(bool retry)
{
    lock();
    DDebug(this,DebugNote,"Aborting alignment [%p]",this);
    if (!retry)
	m_status = OutOfService;
    setLocalStatus(OutOfService);
    m_interval = Time::now() + 1000000;
    m_abort = m_resend = 0;
    m_errors = 0;
    m_bsn = m_fsn = 127;
    m_bib = m_fib = true;
    m_fillTime = 0;
    unlock();
    transmitLSSU();
    SS7Layer2::notify();
}

bool SS7MTP2::startProving()
{
    if (!aligned())
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

/* vi: set ts=8 sw=4 sts=4 noet: */
