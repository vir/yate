/**
 * q921.cpp
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

#include <stdlib.h>


using namespace TelEngine;

/**
 * DEFINEs controlling Q.921 implementation
 * Q921_PASIVE_NOCHECK_PF
 *	Yes: Received UA/DM responses will be validated without checking the P/F bit
 *	No:  Received UA/DM responses without P/F bit set will be dropped
*/
#ifndef Q921_PASIVE_NOCHECK_PF
    #define Q921_PASIVE_NOCHECK_PF
#endif

static const char* s_linkSideNet = "NET";
static const char* s_linkSideCpe = "CPE";

#define Q921_MANAGEMENT_TEI   15         // TEI management message descriptor (first byte). See Q.921 Table 8
#define Q921_TEI_BROADCAST   127         // TEI value for broadcast and management procedures
#define Q921_SAPI_MANAGEMENT  63         // SAPI value for management procedures

static inline const char* linkSide(bool net)
{
    return net ? s_linkSideNet : s_linkSideCpe;
}

static inline void fixParams(NamedList& params, const NamedList* config)
{
    if (config && params.getBoolValue(YSTRING("local-config"),false))
	params.copyParams(*config);
    int rx = params.getIntValue(YSTRING("rxunderrun"));
    if ((rx > 0) && (rx < 2500))
	params.setParam("rxunderrun","2500");
}

// Drop frame reasons
static const char* s_noState = "Not allowed in this state";

// Used to set or compare values that may wrap at 127 boundary
class Modulo128
{
public:
    // Increment a value. Set to 0 if greater the 127
    static inline void inc(u_int8_t& value) {
	    if (value < 127)
		value++;
	    else
		value = 0;
	}

    // Check if a given value is in an interval given by it's margins
    // @param value The value to check
    // @param low The lower margin of the interval
    // @param high The higher margin of the interval
    static inline bool between(u_int8_t value, u_int8_t low, u_int8_t high) {
	    if (low == high)
		return value == low;
	    if (low < high)
		return value >= low && value <= high;
	    // low > high: counter wrapped around
	    return value >= low || value <= high;
	}

    // Get the lower margin of an interval given by it's higher margin and length
    // The interval length is assumed non 0
    // @param high The higher margin of the interval
    // @param len The interval length
    static inline u_int8_t getLow(u_int8_t high, u_int8_t len)
	{ return ((high >= len) ? high - len + 1 : 128 - (len - high)); }

};

/**
 * ISDNQ921
 */
// ****************************************************************************
// NOTE:
// *  Private methods are not thread safe. They are called from public
//      and protected methods which are thread safe
// *  Always drop any lock before calling Layer 3 methods to avoid a deadlock:
//      it may try to establish/release/send data from a different thread
// ****************************************************************************

// Constructor. Set data members. Print them
ISDNQ921::ISDNQ921(const NamedList& params, const char* name, ISDNQ921Management* mgmt, u_int8_t tei)
    : SignallingComponent(name,&params,"isdn-q921"),
      ISDNLayer2(params,name,tei),
      SignallingReceiver(name),
      SignallingDumpable(SignallingDumper::Q921,network()),
      m_management(mgmt),
      m_remoteBusy(false),
      m_timerRecovery(false),
      m_rejectSent(false),
      m_pendingDMSabme(false),
      m_lastPFBit(false),
      m_vs(0),
      m_va(0),
      m_vr(0),
      m_retransTimer(0),
      m_idleTimer(0),
      m_window(7),
      m_n200(3),
      m_txFrames(0),
      m_txFailFrames(0),
      m_rxFrames(0),
      m_rxRejectedFrames(0),
      m_rxDroppedFrames(0),
      m_hwErrors(0),
      m_printFrames(true),
      m_extendedDebug(false),
      m_errorSend(false),
      m_errorReceive(false)
{
    if (mgmt && network())
	autoRestart(false);
    m_retransTimer.interval(params,"t200",1000,1000,false);
    m_idleTimer.interval(params,"t203",2000,10000,false);
    // Adjust idle timeout to data link side
    m_idleTimer.interval(m_idleTimer.interval() + (network() ? -500 : 500));
    m_window.maxVal(params.getIntValue(YSTRING("maxpendingframes"),7));
    if (!m_window.maxVal())
	m_window.maxVal(7);
    setDebug(params.getBoolValue(YSTRING("print-frames"),false),
	params.getBoolValue(YSTRING("extended-debug"),false));
    if (debugAt(DebugInfo)) {
	String tmp;
#ifdef DEBUG
	if (debugAt(DebugAll)) {
	    params.dump(tmp,"\r\n  ",'\'',true);
	    Debug(this,DebugAll,"ISDNQ921::ISDNQ921(%p,'%s',%p,%u) [%p]%s",
		&params,name,mgmt,tei,this,tmp.c_str());
	    tmp.clear();
	}
	tmp << " SAPI/TEI=" << (unsigned int)localSapi() << "/" << (unsigned int)localTei();
	tmp << " auto-restart=" << String::boolText(autoRestart());
	tmp << " max-user-data=" << (unsigned int)maxUserData();
	tmp << " max-pending-frames: " << (unsigned int)m_window.maxVal();
	tmp << " retrans/idle=" << (unsigned int)m_retransTimer.interval()  << "/"
		<< (unsigned int)m_idleTimer.interval();
#endif
	Debug(this,DebugAll,"ISDN Data Link type=%s%s [%p]",
	    linkSide(network()),tmp.safe(),this);
    }
    if (!mgmt)
	setDumper(params.getValue(YSTRING("layer2dump")));
}

// Destructor
ISDNQ921::~ISDNQ921()
{
    Lock lock(l2Mutex());
    ISDNLayer2::attach((ISDNLayer3*)0);
    TelEngine::destruct(SignallingReceiver::attach(0));
    cleanup();
    DDebug(this,DebugAll,
	"ISDN Data Link destroyed. Frames: sent=%u (failed=%u) recv=%u rejected=%u dropped=%u. HW errors=%u [%p]",
	(unsigned int)m_txFrames,(unsigned int)m_txFailFrames,
	(unsigned int)m_rxFrames,(unsigned int)m_rxRejectedFrames,
	(unsigned int)m_rxDroppedFrames,(unsigned int)m_hwErrors,this);
}

// Initialize layer, attach interface if not managed
bool ISDNQ921::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"ISDNQ921::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue(YSTRING("debuglevel_q921"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	setDebug(config->getBoolValue(YSTRING("print-frames"),false),
	    config->getBoolValue(YSTRING("extended-debug"),false));
    }
    if (config && !m_management && !iface()) {
	NamedList params("");
	if (resolveConfig(YSTRING("sig"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    params.addParam("basename",params);
	    params.assign(params + "/D");
	    fixParams(params,config);
	    SignallingInterface* ifc = YSIGCREATE(SignallingInterface,&params);
	    if (!ifc)
		return false;
	    SignallingReceiver::attach(ifc);
	    if (ifc->initialize(&params)) {
		SignallingReceiver::control(SignallingInterface::Enable);
		multipleFrame(0,true,false);
	    }
	    else
		TelEngine::destruct(SignallingReceiver::attach(0));
	}
    }
    return m_management || iface();
}

// Set or release 'multiple frame acknowledged' mode
bool ISDNQ921::multipleFrame(u_int8_t tei, bool establish, bool force)
{
    Lock lock(l2Mutex());
    // Check state. Don't do anything in transition states or if TEI changes
    if ((localTei() != tei) || (state() == WaitEstablish) || (state() == WaitRelease))
	return false;
    // The request wouldn't change our state and we are not forced to fulfill it
    if (!force &&
	((establish && (state() == Established)) ||
	(!establish && (state() == Released))))
	return false;
    XDebug(this,DebugAll,"Process '%s' request, TEI=%u",establish ? "ESTABLISH" : "RELEASE",tei);
    bool result = true;
    if (establish) {
	reset();
	result = sendUFrame(ISDNFrame::SABME,true,true);
	changeState(WaitEstablish,"multiple frame");
	timer(true,false);
    }
    else {
	// Already disconnected: Just notify Layer 3
	if (state() == Released) {
	    lock.drop();
	    if (m_management)
		m_management->multipleFrameReleased(tei,true,false,this);
	    else
		multipleFrameReleased(tei,true,false);
	    return true;
	}
	reset();
	result = sendUFrame(ISDNFrame::DISC,true,true);
	changeState(WaitRelease,"multiple frame");
	timer(true,false);
    }
    return result;
}

// Send data through the HDLC interface
bool ISDNQ921::sendData(const DataBlock& data, u_int8_t tei, bool ack)
{
    if (data.null())
	return false;
    Lock lock(l2Mutex());
    if (ack) {
	if (localTei() != tei || !teiAssigned() || state() == Released || m_window.full())
	    return false;
	// Enqueue and send outgoing data
	ISDNFrame* f = new ISDNFrame(true,network(),localSapi(),localTei(),false,data);
	// Update frame send seq number. Inc our send seq number and window counter
	f->update(&m_vs,0);
	Modulo128::inc(m_vs);
	m_window.inc();
	// Append and try to send frame
	m_outFrames.append(f);
	XDebug(this,DebugAll,"Enqueued data frame (%p). Sequence number: %u",f,f->ns());
	sendOutgoingData();
	return true;
    }
    // Unacknowledged data request
    if (tei != Q921_TEI_BROADCAST) {
	Debug(this,DebugInfo,"Not sending unacknowledged data with TEI %u [%p]",tei,this);
	return false;
    }
    // P/F bit is always false for UI frames. See Q.921 5.2.2
    ISDNFrame* f = new ISDNFrame(false,network(),localSapi(),localTei(),false,data);
    bool result = sendFrame(f);
    TelEngine::destruct(f);
    return result;
}

// Send DISC. Reset data
void ISDNQ921::cleanup()
{
    Lock lock(l2Mutex());
    DDebug(this,DebugAll,"Cleanup in state '%s'",stateName(state()));
    // Don't send DISC if we are disconnected or waiting to become disconnected
    if (state() == Established)
	sendUFrame(ISDNFrame::DISC,true,true);
    reset();
    changeState(Released,"cleanup");
}

// Method called periodically to check timeouts
// Re-sync with remote peer if necessary
void ISDNQ921::timerTick(const Time& when)
{
    // If possible return early without locking
    if (state() == Released)
	return;
    Lock lock(l2Mutex(),SignallingEngine::maxLockWait());
    // Check state again after locking, to be sure it didn't change
    if (!lock.locked() || (state() == Released))
	return;
    // T200 not started
    if (!m_retransTimer.started()) {
	// T203 not started: START
	if (!m_idleTimer.started()) {
	    timer(false,true,when.msec());
	    m_timerRecovery = false;
	    return;
	}
	// T203 started: Timeout ?
	if (!m_idleTimer.timeout(when.msec()))
	    return;
	// Start timer
	XDebug(this,DebugInfo,"T203 expired. Start T200");
	timer(true,false,when.msec());
    }
    // T200 started
    if (!m_retransTimer.timeout(when.msec()))
	return;
    // Q.921 5.6.7: Timeout
    // Done all retransmissions ?
    if (m_n200.full()) {
	reset();
	changeState(Released,"timeout");
	lock.drop();
	multipleFrameReleased(localTei(),false,true);
	if (autoRestart())
	    multipleFrame(localTei(),true,false);
	return;
    }
    // Waiting to establish/release ?
    if (state() == WaitEstablish || state() == WaitRelease) {
	ISDNFrame::Type t = (state() == WaitEstablish) ?
	    ISDNFrame::SABME : ISDNFrame::DISC;
	XDebug(this,DebugAll,"T200 expired. Retransmit '%s'",ISDNFrame::typeName(t));
	sendUFrame(t,true,true,true);
	m_n200.inc();
	timer(true,false,when.msec());
	return;
    }
    // State is Established
    if (!m_timerRecovery) {
	m_n200.reset();
	m_timerRecovery = true;
    }
    // Try to retransmit some data or send RR
    if (!sendOutgoingData(true)) {
	XDebug(this,DebugAll,"T200 expired. Send '%s'",ISDNFrame::typeName(ISDNFrame::RR));
	sendSFrame(ISDNFrame::RR,true,true);
	m_lastPFBit = true;
    }
    m_n200.inc();
    timer(true,false,when.msec());
}

// Process a packet received by the receiver's interface
// Parse data. Validate received frame and process it
bool ISDNQ921::receivedPacket(const DataBlock& packet)
{
    ISDNFrame* f = parsePacket(packet);
    if (!f) {
	if (!m_errorReceive) {
	    m_errorReceive = true;
	    Debug(this,DebugNote,"Received invalid packet with length %u [%p]",packet.length(),this);
	}
	return false;
    }
    m_errorReceive = false;
    // Print & dump
    if (debugAt(DebugInfo) && m_printFrames) {
	String tmp;
	f->toString(tmp,m_extendedDebug);
	Debug(this,DebugInfo,"Received frame (%p):%s",f,tmp.c_str());
    }
    if (f->type() < ISDNFrame::Invalid)
	dump(f->buffer(),false);
    return receivedFrame(f);
}

bool ISDNQ921::receivedFrame(ISDNFrame* frame)
{
    if (!frame)
	return false;
    Lock lock(l2Mutex());
    bool reject = false;
    // Not accepted:
    // If not rejected, for out of range sequence number send
    //     REJ to request retransmission if not already sent or RR to confirm if REJ already sent
    //     Just drop the frame otherwise
    // If rejected (unrecoverable error), re-establish data link
    if (!acceptFrame(frame,reject)) {
	if (!reject) {
	    if (frame->m_error == ISDNFrame::ErrTxSeqNo) {
		if (!m_rejectSent) {
		    sendSFrame(ISDNFrame::REJ,true,true);
		    m_rejectSent = true;
		    m_lastPFBit = true;
		}
		else
		    sendSFrame(ISDNFrame::RR,false,frame->poll());
	    }
	    TelEngine::destruct(frame);
	    return true;
	}
	// Unrecoverable error: re-establish
	Debug(this,DebugNote,"Rejected %s frame %p, reason: '%s'. Restarting",
	    frame->name(),frame,ISDNFrame::typeName(frame->error()));
	TelEngine::destruct(frame);
	reset();
	changeState(WaitEstablish,"received frame");
	sendUFrame(ISDNFrame::SABME,true,true);
	timer(true,false);
	return true;
    }
    // Process
    XDebug(this,DebugAll,"Process %s frame %p in state '%s'",
	frame->name(),frame,ISDNLayer2::stateName(state()));
    bool chgState = false, confirmation = false;
    State newState;
    if (frame->category() == ISDNFrame::Data) {
	bool ack = (frame->type() == ISDNFrame::I);
	if (processDataFrame(frame,ack)) {
	    DataBlock tmp;
	    frame->getData(tmp);
	    lock.drop();
	    receiveData(tmp,localTei());
	}
	frame->deref();
	return true;
    }
    if (frame->category() == ISDNFrame::Supervisory) {
	if (processSFrame(frame)) {
	    // Exit from timer recovery
	    m_timerRecovery = false;
	    if (m_pendingDMSabme) {
		m_pendingDMSabme = false;
		chgState = true;
		newState = WaitEstablish;
	    }
	}
    }
    else
	chgState = processUFrame(frame,newState,confirmation);
    TelEngine::destruct(frame);
    // Change state ?
    if (!chgState)
	return true;
    reset();
    changeState(newState,"received frame");
    switch (newState) {
	case Established:
	    timer(false,true);
	    lock.drop();
	    if (m_management)
		m_management->multipleFrameEstablished(localTei(),confirmation,false,this);
	    else
		multipleFrameEstablished(localTei(),confirmation,false);
	    break;
	case Released:
	    lock.drop();
	    if (m_management)
		m_management->multipleFrameReleased(localTei(),confirmation,false,this);
	    else
		multipleFrameReleased(localTei(),confirmation,false);
	    break;
	case WaitEstablish:
	    sendUFrame(ISDNFrame::SABME,true,true);
	    timer(true,false);
	    break;
	case WaitRelease:
	    sendUFrame(ISDNFrame::DISC,true,true);
	    timer(true,false);
	    break;
    }
    return true;
}

// Process a notification generated by the attached interface
bool ISDNQ921::notify(SignallingInterface::Notification event)
{
    Lock lock(l2Mutex());
    if (event != SignallingInterface::LinkUp)
	m_hwErrors++;
    else {
	Debug(this,DebugInfo,"Received notification %u: '%s'",
	    event,lookup(event,SignallingInterface::s_notifName));
	return true;
    }
    if (event == SignallingInterface::LinkDown) {
	Debug(this,DebugWarn,"Received notification %u: '%s'",
	    event,lookup(event,SignallingInterface::s_notifName));
	reset();
	changeState(Released,"interface down");
	lock.drop();
	multipleFrameReleased(localTei(),false,false);
	if (m_management && !network()) {
	    teiAssigned(false);
	    setRi(0);
	}
	if (autoRestart())
	    multipleFrame(localTei(),true,false);
	return true;
    }
#ifdef DEBUG
    if (!(m_hwErrors % 250))
	Debug(this,DebugNote,"Received notification %u: '%s'. Total=%u",
	    event,lookup(event,SignallingInterface::s_notifName,"Undefined"),m_hwErrors);
#endif
    return true;
}

// Reset data
void ISDNQ921::reset()
{
    Lock lock(l2Mutex());
    XDebug(this,DebugAll,"Reset, total frames: %d [%p]",m_outFrames.count(),this);
    m_remoteBusy = false;
    m_timerRecovery = false;
    m_rejectSent = false;
    m_lastPFBit = false;
    m_n200.reset();
    m_window.reset();
    timer(false,false);
    m_outFrames.clear();
    m_va = m_vs = m_vr = 0;
}

// Acknoledge pending outgoing frames. See Q.921 5.6.3.2
// Remove ack'd frames from queue. Start idle timer
bool ISDNQ921::ackOutgoingFrames(const ISDNFrame* frame)
{
    bool ack = false, unack = false;
    // Acknoledge frames with N(S) up to frame->nr() (not including)
    for (;;) {
	ObjList* obj = m_outFrames.skipNull();
	ISDNFrame* f = obj ? static_cast<ISDNFrame*>(obj->get()) : 0;
	// Stop when no frames or seq number equals nr
	if (!f || frame->nr() == f->ns()) {
	    if (f && f->sent())
		unack = true;
	    break;
	}
	ack = true;
	XDebug(this,DebugAll,
	    "Remove acknowledged data frame (%p). Sequence number: %u",f,f->ns());
	m_window.dec();
	m_outFrames.remove(f,true);
    }
    // Reset T200 if not in timer-recovery condition and ack some frame
    // 5.5.3.2: Note 1: Dont't reset if we've requested a response and haven't got one
    if (!m_timerRecovery && ack &&
	!(frame->type() != ISDNFrame::I && m_lastPFBit))
	timer(false,false);
    // Start T200 if we have unacknowledged data and not already started
    if (unack && !m_retransTimer.started())
	timer(true,false);
    return ack;
}

// Receive I/UI (data) frames (See Q.921 5.6.2)
// Send unacknowledged data to upper layer
// Ack pending outgoing data and confirm (by sending any pending data or an RR confirmation)
bool ISDNQ921::processDataFrame(const ISDNFrame* frame, bool ack)
{
    // Always accept UI
    if (!ack)
	return true;
    // Acknowledged data: accept only when established
    if (state() != Established) {
	dropFrame(frame,s_noState);
	return false;
    }
    m_rejectSent = false;
    m_remoteBusy = false;
    m_vr = frame->ns();
    Modulo128::inc(m_vr);
    XDebug(this,DebugAll,"Set V(R) to %u",m_vr);
    ackOutgoingFrames(frame);
    m_va = frame->nr();
    XDebug(this,DebugAll,"Set V(A) to %u.",m_va);
    // P/F=1: Q.921 5.6.2.1   P/F=0: Q.921 5.6.2.2
    if (frame->poll())
	sendSFrame(ISDNFrame::RR,false,true);
    else
	if (!sendOutgoingData())
	    sendSFrame(ISDNFrame::RR,false,false);
    // Start T203 if T200 not started
    if (!m_retransTimer.started())
	timer(false,true);
    return true;
}

// Process received S (supervisory) frames: RR, REJ, RNR
// All   Ack outgoing frames. Respond with RR if requested
// RR    Send pending frames. Start idle timer
// REJ   Send pending frames. Adjust send frame and expected frame counter if necessary
// RNR   Adjust send frame counter if necessary
bool ISDNQ921::processSFrame(const ISDNFrame* frame)
{
    if (!frame)
	return false;
    Lock lock(l2Mutex());
    if (state() != Established) {
	dropFrame(frame,s_noState);
	return false;
    }
    if (frame->type() == ISDNFrame::RR) {
	// Ack sent data. Send unsent data
	// Respond if it's an unsolicited frame with P/F set to 1
	m_remoteBusy = false;
	ackOutgoingFrames(frame);
	bool sent = sendOutgoingData();
	if (frame->poll()) {
	    // Check if we requested a response. If not, respond if it is a command
	    if (!m_lastPFBit && frame->command())
		sendSFrame(ISDNFrame::RR,false,true);
	    // Don't reset if we've sent any data
	    if (!sent) {
		m_lastPFBit = false;
		timer(false,true);
	    }
	}
	if (!m_retransTimer.started() && !m_idleTimer.started())
	    timer(false,true);
	return false;
    }
    // Q.921 5.6.4: Receiving REJ frames
    if (frame->type() == ISDNFrame::REJ) {
	m_remoteBusy = false;
	// Ack sent data.
	ackOutgoingFrames(frame);
	// Q.921 5.6.4 a) and b)
	bool rspPF = !frame->command() && frame->poll();
	if (!m_timerRecovery || (m_timerRecovery && rspPF)) {
	    m_vs = m_va = frame->nr();
	    XDebug(this,DebugAll,"Set V(S) and V(A) to %u.",m_vs);
	    if (!m_timerRecovery && frame->command() && frame->poll())
		sendSFrame(ISDNFrame::RR,false,true);
	    // Retransmit only if we didn't sent a supervisory frame
	    if (!m_lastPFBit) {
		bool t200 = sendOutgoingData(true);
		timer(t200,!t200);
	    }
	    if (!m_timerRecovery && rspPF)
		Debug(this,DebugNote,"Frame (%p) is a REJ response with P/F set",frame);
	    m_timerRecovery = false;
	    return false;
	}
	// Q.921 5.6.4 c)
	m_va = frame->nr();
	XDebug(this,DebugAll,"Set V(A) to %u.",m_va);
	if (frame->command() && frame->poll())
	    sendSFrame(ISDNFrame::RR,false,true);
	return false;
    }
    // Q.921 5.6.5: Receiving RNR frames
    if (frame->type() == ISDNFrame::RNR) {
	m_remoteBusy = true;
	// Ack sent data.
	ackOutgoingFrames(frame);
	// Respond
	if (frame->poll()) {
	    if (frame->command())
		sendSFrame(ISDNFrame::RR,false,true);
	    else {
		m_timerRecovery = false;
		m_vs = frame->nr();
		XDebug(this,DebugAll,"Set V(S) to %u.",m_vs);
	    }
	}
	if (!m_lastPFBit)
	    timer(true,false);
	return false;
    }
    dropFrame(frame,s_noState);
    return false;
}

//  Receive U frames: UA, DM, SABME, DISC, FRMR
//  UA    If P/F = 0: DROP - not a valid response
//        State is Wait...: it's a valid response: notify layer 3 and change state
//        Otherwise: DROP
//  DM    State is Established or Released
//            P/F = 0: It's an establish request. Send SABME. Change state
//            P/F = 1: If state is Established and timer recovery: schedule establish
//        State is WaitEstablish or WaitRelease and P/F = 1: Release. Notify layer 3
//        Otherwise: DROP
//  SABME State is Established or Released: Confirm. Notify layer 3. Reset
//        State is WaitEstablish: Just confirm
//        State is WaitRelease: Send DM. Release. Notify layer 3
//  DISC  State is Established: Confirm. Release. Notify layer 3
//        State is Released: Just send a DM response
//        State is WaitEstablish: Send DM response. Release. Notify layer 3
//        State is WaitRelease: Just confirm
//  FRMR  If state is Established: re-establish
//        Otherwise: DROP
bool ISDNQ921::processUFrame(const ISDNFrame* frame, State& newState,
	bool& confirmation)
{
    switch (frame->type()) {
	case ISDNFrame::UA:
	    if (!(frame->poll() &&
		(state() == WaitEstablish || state() == WaitRelease)))
		break;
	    newState = (state() == WaitEstablish ? Established : Released);
	    confirmation = true;
	    return true;
	case ISDNFrame::DM:
	    if (state() == Established || state() == Released) {
		if (!frame->poll()) {
		    newState = WaitEstablish;
		    return true;
		}
		if (state() == Established && m_timerRecovery) {
		    m_pendingDMSabme = true;
		    return false;
		}
	    }
	    if (frame->poll()) {
		newState = Released;
		confirmation = true;
		return true;
	    }
	    break;
	case ISDNFrame::SABME:
	    if (state() == Established || state() == Released) {
		sendUFrame(ISDNFrame::UA,false,frame->poll());
		newState = Established;
		confirmation = false;
		return true;
	    }
	    if (state() == WaitEstablish) {
		sendUFrame(ISDNFrame::UA,false,frame->poll());
		return false;
	    }
	    sendUFrame(ISDNFrame::DM,false,frame->poll());
	    newState = Released;
	    confirmation = true;
	    return true;
	case ISDNFrame::DISC:
	    switch (state()) {
		case Established:
		    sendUFrame(ISDNFrame::UA,false,frame->poll());
		    newState = Released;
		    confirmation = false;
		    return true;
		case Released:
		    sendUFrame(ISDNFrame::DM,false,frame->poll());
		    return false;
		case WaitEstablish:
		    sendUFrame(ISDNFrame::DM,false,frame->poll());
		    newState = Released;
		    confirmation = true;
		    return true;
		case WaitRelease:
		    sendUFrame(ISDNFrame::UA,false,frame->poll());
		    return false;
	    }
	    break;
	case ISDNFrame::FRMR:
	    if (state() == Established) {
		newState = WaitEstablish;
		return true;
	    }
	    break;
	default: ;
    }
    dropFrame(frame,s_noState);
    return false;
}

// Accept frame according to Q.921 5.8.5. Reasons to reject:
//	Unknown command/response
//	Invalid N(R)
//	Information field too long
// Update receive counters
bool ISDNQ921::acceptFrame(ISDNFrame* frame, bool& reject)
{
    reject = false;
    // Update received frames
    m_rxFrames++;
    // Check frame only if it's not already invalid
    for (; frame->error() < ISDNFrame::Invalid;) {
	// Check SAPI/TEI
	if (frame->sapi() != localSapi() || frame->tei() != localTei()) {
	    frame->m_error = ISDNFrame::ErrInvalidAddress;
	    break;
	}
	// Drop out of range I frames
	if (frame->type() == ISDNFrame::I && frame->ns() != m_vr) {
	    frame->m_error = ISDNFrame::ErrTxSeqNo;
	    break;
	}
	// Check DISC/SABME commands and UA/DM responses
	if (((frame->type() == ISDNFrame::SABME || frame->type() == ISDNFrame::DISC) &&
	    !frame->command()) ||
	    ((frame->type() == ISDNFrame::UA || frame->type() == ISDNFrame::DM) &&
	    frame->command())) {
	    Debug(this,DebugMild,
		"Received '%s': The remote peer has the same data link side type",
		frame->name());
	    frame->m_error = ISDNFrame::ErrInvalidCR;
	    break;
	}
	// We don't support XID
	if (frame->type() == ISDNFrame::XID) {
	    frame->m_error = ISDNFrame::ErrUnsupported;
	    break;
	}
	// Check N(R) for I or S frames (N(R) is set to 0xFF for U frames):
	// N(R) should be between V(A) and V(S)
	if (frame->nr() < 128 && !Modulo128::between(frame->nr(),m_va,m_vs)) {
	    frame->m_error = ISDNFrame::ErrRxSeqNo;
	    break;
	}
	// Check data length
	if (frame->dataLength() > maxUserData()) {
	    frame->m_error = ISDNFrame::ErrDataLength;
	    break;
	}
	break;
    }
    // Accepted
    if (frame->error() < ISDNFrame::Invalid)
	return true;
    // Frame is invalid. Reject or drop ?
    if (frame->error() == ISDNFrame::ErrUnknownCR ||
	frame->error() == ISDNFrame::ErrRxSeqNo ||
	frame->error() == ISDNFrame::ErrDataLength) {
	// Check if the state allows the rejection. Not allowed if:
	//  - Not in multiple frame operation mode
	if (state() == Established) {
	    m_rxRejectedFrames++;
	    reject = true;
	    return false;
	}
    }
    dropFrame(frame,ISDNFrame::typeName(frame->error()));
    return false;
}

void ISDNQ921::dropFrame(const ISDNFrame* frame, const char* reason)
{
    m_rxDroppedFrames++;
    DDebug(this,DebugNote,
	"Dropping frame (%p): %s. Reason: %s. V(S),V(R),V(A)=%u,%u,%u",
	frame,frame->name(),reason,m_vs,m_vr,m_va);
}

// Send U frames except for UI frames
bool ISDNQ921::sendUFrame(ISDNFrame::Type type, bool command, bool pf,
	bool retrans)
{
    switch (type) {
	case ISDNFrame::SABME:
	case ISDNFrame::DISC:
	case ISDNFrame::DM:
	case ISDNFrame::UA:
	case ISDNFrame::FRMR:
	    break;
	default:
	    return false;
    }
    // Create and send frame
    // U frames don't have an N(R) control data
    ISDNFrame* f = new ISDNFrame(type,command,network(),localSapi(),localTei(),pf);
    f->sent(retrans);
    bool result = sendFrame(f);
    TelEngine::destruct(f);
    return result;
}

// Send S frames
bool ISDNQ921::sendSFrame(ISDNFrame::Type type, bool command, bool pf)
{
    if (!(type == ISDNFrame::RR ||
	type == ISDNFrame::RNR ||
	type == ISDNFrame::REJ))
	return false;
    // Create and send frame
    ISDNFrame* f = new ISDNFrame(type,command,network(),localSapi(),localTei(),pf,m_vr);
    bool result = sendFrame(f);
    TelEngine::destruct(f);
    return result;
}

// Send a frame to remote peer. Dump data on success if we have a dumper
bool ISDNQ921::sendFrame(const ISDNFrame* frame)
{
    if (!frame)
	return false;
    // This should never happen !!!
    if (frame->type() >= ISDNFrame::Invalid) {
	Debug(this,DebugWarn,"Refusing to send '%s' frame",frame->name());
	return false;
    }
    // Print frame
    if (debugAt(DebugInfo) && m_printFrames && !m_errorSend && frame->type() != ISDNFrame::UI) {
	String tmp;
	frame->toString(tmp,m_extendedDebug);
	Debug(this,DebugInfo,"Sending frame (%p):%s",
	    frame,tmp.c_str());
    }
    bool result = m_management ? m_management->sendFrame(frame,this) :
	SignallingReceiver::transmitPacket(frame->buffer(),false,SignallingInterface::Q921);
    // Dump frame if no error and we have a dumper
    if (result) {
	m_txFrames++;
	dump(frame->buffer(),true);
	m_errorSend = false;
    }
    else {
	m_txFailFrames++;
	if (!m_errorSend)
	    Debug(this,DebugNote,"Error sending frame (%p): %s",frame,frame->name());
	m_errorSend = true;
    }
    return result;
}

// Send (or re-send) enqueued data frames
bool ISDNQ921::sendOutgoingData(bool retrans)
{
    bool sent = false;
    for (;;) {
	if (m_remoteBusy || m_window.empty())
	    break;
	ObjList* obj = m_outFrames.skipNull();
	// Queue empty ?
	if (!obj)
	    break;
	ISDNFrame* frame = 0;
	// Not a retransmission: skip already sent frames
	if (!retrans)
	    for (; obj; obj = obj->skipNext()) {
		frame = static_cast<ISDNFrame*>(obj->get());
		if (!frame->sent())
		    break;
	    }
	// Send the remaining unsent frames in window or
	//  the whole queue if it is a retransmission
	for (; obj ; obj = obj->skipNext()) {
	    frame = static_cast<ISDNFrame*>(obj->get());
	    // Update frame receive sequence number
	    frame->update(0,&m_vr);
	    XDebug(this,DebugAll,
		"Sending data frame (%p). Sequence number: %u. Retransmission: %s",
		frame,frame->ns(),String::boolText(frame->sent()));
	    // T200
	    if (!m_retransTimer.started())
		timer(true,false);
	    // Send
	    sendFrame(frame);
	    sent = true;
	    frame->sent(true);
	}
	break;
    }
    return sent;
}

// Start/stop idle or retransmission timers
void ISDNQ921::timer(bool start, bool t203, u_int64_t time)
{
    if (start) {
	if (m_idleTimer.started()) {
	    m_idleTimer.stop();
	    XDebug(this,DebugAll,"T203 stopped");
	}
	// Start anyway. Even if already started
	if (!time)
	     time = Time::msecNow();
	m_retransTimer.start(time);
	XDebug(this,DebugAll,"T200 started. Transmission counter: %u",
	    m_n200.count());
    }
    else {
	m_n200.reset();
	if (m_retransTimer.started()) {
	    m_retransTimer.stop();
	    XDebug(this,DebugAll,"T200 stopped");
	}
	if (t203) {
	    if (!m_idleTimer.started()) {
		if (!time)
		     time = Time::msecNow();
		m_idleTimer.start(time);
		XDebug(this,DebugAll,"T203 started");
	    }
	}
	else if (m_idleTimer.started()) {
	    m_idleTimer.stop();
	    XDebug(this,DebugAll,"T203 stopped");
	}
    }
}

/**
 * ISDNQ921Management
 */
// Constructor
ISDNQ921Management::ISDNQ921Management(const NamedList& params, const char* name, bool net)
    : SignallingComponent(name,&params,"isdn-q921-mgm"),
      ISDNLayer2(params,name),
      SignallingReceiver(name),
      SignallingDumpable(SignallingDumper::Q921,network()),
      m_teiManTimer(0), m_teiTimer(0)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"ISDNQ921Management::ISDNQ921Management(%p,'%s',%s) [%p]%s",
	    &params,name,String::boolText(net),this,tmp.c_str());
    }
#endif
    String baseName = toString();
    m_network = net;
    m_teiManTimer.interval(params,"t202",2500,2600,false);
    m_teiTimer.interval(params,"t201",1000,5000,false);
    setDumper(params.getValue(YSTRING("layer2dump")));
    bool set0 = true;
    if (baseName.endsWith("Management")) {
	baseName = baseName.substr(0,baseName.length()-10);
	set0 = false;
    }
    // If we are NET create one ISDNQ921 for each possible TEI
    for (int i = 0; i < 127; i++) {
	if (network() || (i == 0)) {
	    String qName = baseName;
	    if (!network())
		qName << "-CPE";
	    else if (set0 || (i != 0))
		qName << "-" << i;
	    m_layer2[i] = new ISDNQ921(params,qName,this,i);
	    m_layer2[i]->ISDNLayer2::attach(this);
	}
	else
	    m_layer2[i] = 0;
    }
    if (!network()) {
	m_layer2[0]->teiAssigned(false);
	m_teiManTimer.start();
    }
}

ISDNQ921Management::~ISDNQ921Management()
{
    Lock lock(l2Mutex());
    ISDNLayer2::attach((ISDNLayer3*)0);
    TelEngine::destruct(SignallingReceiver::attach(0));
    for (int i = 0; i < 127; i++)
	TelEngine::destruct(m_layer2[i]);
}

bool ISDNQ921Management::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"ISDNQ921Management::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config)
	debugLevel(config->getIntValue(YSTRING("debuglevel_q921mgmt"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
    if (config && !iface()) {
	NamedList params("");
	if (resolveConfig(YSTRING("sig"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    params.addParam("basename",params);
	    params.assign(params + "/D");
	    fixParams(params,config);
	    SignallingInterface* ifc = YSIGCREATE(SignallingInterface,&params);
	    if (!ifc)
		return false;
	    SignallingReceiver::attach(ifc);
	    if (ifc->initialize(&params))
		SignallingReceiver::control(SignallingInterface::Enable);
	    else
		TelEngine::destruct(SignallingReceiver::attach(0));
	}
    }
    return 0 != iface();
}

void ISDNQ921Management::engine(SignallingEngine* eng)
{
    SignallingComponent::engine(eng);
    for (int i = 0; i < 127; i++)
	if (m_layer2[i])
	    m_layer2[i]->engine(eng);
}

void ISDNQ921Management::cleanup()
{
    Lock lock(l2Mutex());
    for (int i = 0;i < 127; i++)
	if (m_layer2[i])
	    m_layer2[i]->cleanup();
}

bool ISDNQ921Management::multipleFrame(u_int8_t tei, bool establish, bool force)
{
    if (tei >= 127)
	return false;
    m_sapi = Q921_SAPI_MANAGEMENT;
    l2Mutex().lock();
    RefPointer<ISDNQ921> q921 = m_layer2[network() ? tei : 0];
    l2Mutex().unlock();
    return q921 && q921->multipleFrame(tei,establish,force);
}

bool ISDNQ921Management::sendFrame(const ISDNFrame* frame, const ISDNQ921* q921)
{
    if (!frame)
	return false;
    Lock lock(l2Mutex());
    if (SignallingReceiver::transmitPacket(frame->buffer(),false,SignallingInterface::Q921)) {
	dump(frame->buffer(),true);
	return true;
    }
    return false;
}

bool ISDNQ921Management::sendData(const DataBlock& data, u_int8_t tei, bool ack)
{
    if (tei > Q921_TEI_BROADCAST)
	return false;
    if (tei == Q921_TEI_BROADCAST)
	ack = false;
    int auxTei = tei;

    Lock lock(l2Mutex());
    if (!network()) {
       if (m_layer2[0] && m_layer2[0]->teiAssigned())
           auxTei = 0;
       else
           return false;
    }
    if (ack)
       return m_layer2[auxTei] && m_layer2[auxTei]->sendData(data,tei,true);

    // P/F bit is always false for UI frames. See Q.921 5.2.2
    ISDNFrame* f = new ISDNFrame(false,network(),0,tei,false,data);
    bool ok = sendFrame(f);
    lock.drop();
    TelEngine::destruct(f);
    return ok;
}

void ISDNQ921Management::multipleFrameEstablished(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> l3 = m_layer3;
    m_layer3Mutex.unlock();
    if (l3)
	l3->multipleFrameEstablished(tei,confirm,timeout,layer2);
    else
	Debug(this,DebugNote,"'Established' notification. No Layer 3 attached");
}

void ISDNQ921Management::multipleFrameReleased(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> l3 = m_layer3;
    m_layer3Mutex.unlock();
    if (l3)
	l3->multipleFrameReleased(tei,confirm,timeout,layer2);
    else
	Debug(this,DebugNote,"'Released' notification. No Layer 3 attached");
}

void ISDNQ921Management::dataLinkState(u_int8_t tei, bool cmd, bool value, ISDNLayer2* layer2)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> l3 = m_layer3;
    m_layer3Mutex.unlock();
    if (l3)
	l3->dataLinkState(tei,cmd,value,layer2);
    else
	Debug(this,DebugNote,"Data link notification. No Layer 3 attached");
}

void ISDNQ921Management::receiveData(const DataBlock& data, u_int8_t tei, ISDNLayer2* layer2)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> l3 = m_layer3;
    m_layer3Mutex.unlock();
    if (!network()) {
	l2Mutex().lock();
	if (m_layer2[0])
	    tei = m_layer2[0]->localTei();
	l2Mutex().unlock();
    }
    if (l3)
	l3->receiveData(data,tei,layer2);
    else
	Debug(this,DebugNote,"Data received. No Layer 3 attached");
}

// Process a Signalling Packet received by the interface
bool ISDNQ921Management::receivedPacket(const DataBlock& packet)
{
    Lock lock(l2Mutex());
    ISDNFrame* frame = parsePacket(packet);
    if (!frame)
	return false;
    if (frame->type() < ISDNFrame::Invalid)
	dump(frame->buffer(),false);
    // Non UI frame (even invalid): send it to the appropriate Layer 2
    if (frame->type() != ISDNFrame::UI) {
	if (network()) {
	    if (m_layer2[frame->tei()] && m_layer2[frame->tei()]->m_ri) {
		lock.drop();
		return m_layer2[frame->tei()]->receivedFrame(frame);
	    }
	    sendTeiManagement(ISDNFrame::TeiRemove,0,frame->tei());
	    lock.drop();
	    TelEngine::destruct(frame);
	    return false;
	}
	else if (m_layer2[0] && m_layer2[0]->m_ri && m_layer2[0]->localTei() == frame->tei()) {
	    lock.drop();
	    return m_layer2[0]->receivedFrame(frame);
	}
	return false;
    }
    if (!processTeiManagement(frame)) {
	DataBlock tmp;
	frame->getData(tmp);
	u_int8_t tei = frame->tei();
	TelEngine::destruct(frame);
	receiveData(tmp,tei,m_layer2[0]);
	return true;
    }
    // FIXME
    TelEngine::destruct(frame);
    return true;
}

// Periodically called method to take care of timers
void ISDNQ921Management::timerTick(const Time& when)
{
    if (network()) {
	if (m_teiTimer.started() && m_teiTimer.timeout(when.msec())) {
	    for (u_int8_t i = 0; i < 127; i++) {
		if (m_layer2[i] && !m_layer2[i]->m_checked) {
		    m_layer2[i]->setRi(0);
		    m_layer2[i]->teiAssigned(false);
		    multipleFrameReleased(i,false,true,this);
		}
	    }
	    m_teiTimer.stop();
	}
    }
    else if (m_layer2[0]) {
	if (m_layer2[0]->teiAssigned())
	    m_teiManTimer.stop();
	else if (!m_teiManTimer.started())
	    m_teiManTimer.start();
	else if (m_teiManTimer.timeout(when.msec())) {
	    m_teiManTimer.stop();
	    u_int16_t ri = m_layer2[0]->m_ri;
	    while (!ri)
		ri = (u_int16_t)Random::random();
	    m_layer2[0]->m_tei = 0;
	    m_layer2[0]->setRi(ri);
	    sendTeiManagement(ISDNFrame::TeiReq,ri,Q921_TEI_BROADCAST);
	}
    }
}

// Forward interface notifications to controlled Q.921
bool ISDNQ921Management::notify(SignallingInterface::Notification event)
{
    DDebug(this,DebugInfo,"Received notification %u: '%s'",
	event,lookup(event,SignallingInterface::s_notifName));
    for (u_int8_t i = 0; i < 127; i++)
	if (m_layer2[i])
	    m_layer2[i]->notify(event);
    return true;
}

// Process TEI management frames according to their type
bool ISDNQ921Management::processTeiManagement(ISDNFrame* frame)
{
    if (!frame)
	return false;
    if (!frame->checkTeiManagement())
	return false;
    DataBlock data;
    frame->getData(data);
    u_int8_t ai = ISDNFrame::getAi(data);
    u_int16_t ri = ISDNFrame::getRi(data);
    u_int8_t type = ISDNFrame::getType(data);
    XDebug(this,DebugAll,"Management frame type=0x%02X ri=%u ai=%u",type,ri,ai);
    switch (type) {
	case ISDNFrame::TeiReq:
	    processTeiRequest(ri,ai,frame->poll());
	    break;
	case ISDNFrame::TeiRemove:
	    processTeiRemove(ai);
	    break;
	case ISDNFrame::TeiCheckReq:
	    processTeiCheckRequest(ai,frame->poll());
	    break;
	case ISDNFrame::TeiAssigned:
	    processTeiAssigned(ri,ai);
	    break;
	case ISDNFrame::TeiDenied:
	    processTeiDenied(ri);
	    break;
	case ISDNFrame::TeiCheckRsp:
	    processTeiCheckResponse(ri,ai);
	    break;
	case ISDNFrame::TeiVerify:
	    processTeiVerify(ai,frame->poll());
	    break;
	default:
	    Debug(this,DebugNote,"Unknown management frame type 0x%02X",type);
    }
    return true;
}

// Build and send a TEI management frame
bool ISDNQ921Management::sendTeiManagement(ISDNFrame::TeiManagement type,
    u_int16_t ri, u_int8_t ai, u_int8_t tei, bool pf)
{
    DataBlock data;
    if (!ISDNFrame::buildTeiManagement(data,type,ri,ai)) {
	Debug(this,DebugNote,"Could not build TEI management frame");
	return false;
    }
    ISDNFrame* frame = new ISDNFrame(false,network(),
	Q921_SAPI_MANAGEMENT,tei,pf,data);
    bool ok = sendFrame(frame);
    TelEngine::destruct(frame);
    return ok;
}

// We are NET, a CPE has requested a TEI assignment
void ISDNQ921Management::processTeiRequest(u_int16_t ri, u_int8_t ai, bool pf)
{
    if (!network() || !ri)
	return;
    if (ai < 127 && m_layer2[ai] && m_layer2[ai]->m_ri == ri) {
	// TEI already assigned to same reference number, confirm it
	sendTeiManagement(ISDNFrame::TeiAssigned,ri,ai,Q921_TEI_BROADCAST,pf);
	return;
    }
    u_int8_t i;
    for (i = 0; i < 127; i++) {
	if (!m_layer2[i])
	    continue;
	if (m_layer2[i]->m_ri == ri) {
	    // Reference number already used for different TEI
	    sendTeiManagement(ISDNFrame::TeiDenied,ri,ai,Q921_TEI_BROADCAST,pf);
	    return;
	}
    }
    for (i = 64; i < 127; i++) {
	if (m_layer2[i]->m_ri != 0)
	    continue;
	// Found a free dynamic TEI slot, assign to given reference number
	if (sendTeiManagement(ISDNFrame::TeiAssigned,ri,i,Q921_TEI_BROADCAST,pf)) {
	    m_layer2[i]->setRi(ri);
	    m_layer2[i]->reset();
	}
	return;
    }
    // All dynamic TEI slots are in use, deny new request
    sendTeiManagement(ISDNFrame::TeiDenied,ri,Q921_TEI_BROADCAST,pf);
    m_teiTimer.stop();
    // Mark all dynamic TEI slots as not checked and ask them to check
    for (i = 64; i < 127; i++)
	if (m_layer2[i])
	    m_layer2[i]->m_checked = false;
    sendTeiManagement(ISDNFrame::TeiCheckReq,0,Q921_TEI_BROADCAST);
    m_teiTimer.start();
}

// We are CPE, NET asked us to remove our TEI
void ISDNQ921Management::processTeiRemove(u_int8_t ai)
{
    if (network())
	return;
    u_int8_t tei = m_layer2[0]->localTei();
    if ((ai == tei) || (ai == Q921_TEI_BROADCAST && tei >= 64)) {
	Debug(this,((tei < 64) ? DebugMild : DebugInfo),"Removing our TEI %u",tei);
	m_layer2[0]->teiAssigned(false);
	m_layer2[0]->setRi(0);
	multipleFrameReleased(ai,false,false,this);
	m_teiManTimer.start();
    }
}

// We are CPE, NET is checking our TEI
void ISDNQ921Management::processTeiCheckRequest(u_int8_t ai, bool pf)
{
    if (network())
	return;
    if (m_layer2[0]->m_ri && ((ai == Q921_TEI_BROADCAST) || (ai == m_layer2[0]->localTei())))
	sendTeiManagement(ISDNFrame::TeiCheckRsp,m_layer2[0]->m_ri,ai,Q921_TEI_BROADCAST,pf);
}

// We are NET and received a TEI check response to our request
void ISDNQ921Management::processTeiCheckResponse(u_int16_t ri, u_int8_t ai)
{
    if (!network())
	return;
    if ((ai >= 127) || !m_layer2[ai])
	return;
    if (m_layer2[ai]->m_ri == ri)
	m_layer2[ai]->m_checked = true;
    else if (sendTeiManagement(ISDNFrame::TeiRemove,ri,ai))
	m_layer2[ai]->setRi(0);
}

// We are CPE and the NET assigned a TEI, possibly to us
void ISDNQ921Management::processTeiAssigned(u_int16_t ri, u_int8_t ai)
{
    if (network())
	return;
    if (m_layer2[0]->m_ri != ri)
	return;
    m_teiManTimer.stop();
    m_layer2[0]->m_tei = ai;
    m_layer2[0]->teiAssigned(true);
    multipleFrame(ai,true,true);
}

// We are CPE and the NET denied assigning a TEI, possibly to us
void ISDNQ921Management::processTeiDenied(u_int16_t ri)
{
    if (network())
	return;
    if (m_layer2[0]->m_ri != ri)
	return;
    m_layer2[0]->setRi(0);
    m_teiManTimer.start();
}

// We are NET, a CPE is asking to be verified
void ISDNQ921Management::processTeiVerify(u_int8_t ai, bool pf)
{
    if (!network())
	return;
    if ((ai < 127) && m_layer2[ai] && m_layer2[ai]->m_ri)
	sendTeiManagement(ISDNFrame::TeiCheckReq,0,ai,Q921_TEI_BROADCAST,pf);
}


/**
 * ISDNQ921Passive
 */
// Constructor. Set data members. Print them
ISDNQ921Passive::ISDNQ921Passive(const NamedList& params, const char* name)
    : SignallingComponent(name,&params,"isdn-q921-passive"),
      ISDNLayer2(params,name),
      SignallingReceiver(name),
      SignallingDumpable(SignallingDumper::Q921,network()),
      m_checkLinkSide(false),
      m_idleTimer(0),
      m_lastFrame(255),
      m_rxFrames(0),
      m_rxDroppedFrames(0),
      m_hwErrors(0),
      m_printFrames(true),
      m_extendedDebug(false),
      m_errorReceive(false)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"ISDNQ921Passive::ISDNQ921Passive(%p,'%s') [%p]%s",
	    &params,name,this,tmp.c_str());
    }
#endif
    m_idleTimer.interval(params,"idletimeout",4000,30000,false);
    m_checkLinkSide = detectType();
    setDebug(params.getBoolValue(YSTRING("print-frames"),false),
	params.getBoolValue(YSTRING("extended-debug"),false));
    DDebug(this,DebugInfo,
	"ISDN Passive Data Link type=%s autodetect=%s idle-timeout=%u [%p]",
	linkSide(network()),String::boolText(detectType()),
	(unsigned int)m_idleTimer.interval(),this);
    m_idleTimer.start();
    // Try to dump from specific parameter, fall back to generic
    const char* dump = network() ? "layer2dump-net" : "layer2dump-cpe";
    setDumper(params.getValue(dump,params.getValue(YSTRING("layer2dump"))));
}

// Destructor
ISDNQ921Passive::~ISDNQ921Passive()
{
    Lock lock(l2Mutex());
    ISDNLayer2::attach(0);
    TelEngine::destruct(SignallingReceiver::attach(0));
    cleanup();
    DDebug(this,DebugAll,
	"ISDN Passive Data Link destroyed. Frames: recv=%u dropped=%u. HW errors=%u [%p]",
	(unsigned int)m_rxFrames,(unsigned int)m_rxDroppedFrames,(unsigned int)m_hwErrors,this);
}

bool ISDNQ921Passive::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"ISDNQ921Passive::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue(YSTRING("debuglevel_q921"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	setDebug(config->getBoolValue(YSTRING("print-frames"),false),
	    config->getBoolValue(YSTRING("extended-debug"),false));
    }
    if (config && !iface()) {
	NamedList params("");
	if (resolveConfig(YSTRING("sig"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    params.addParam("basename",params);
	    params.assign(params + "/D");
	    params.addParam("readonly",String::boolText(true));
	    fixParams(params,config);
	    SignallingInterface* ifc = YSIGCREATE(SignallingInterface,&params);
	    if (!ifc)
		return false;
	    SignallingReceiver::attach(ifc);
	    if (ifc->initialize(&params))
		SignallingReceiver::control(SignallingInterface::Enable);
	    else
		TelEngine::destruct(SignallingReceiver::attach(0));
	}
    }
    return 0 != iface();
}

// Reset data
void ISDNQ921Passive::cleanup()
{
    Lock lock(l2Mutex());
    m_idleTimer.start();
}

// Called periodically by the engine to check timeouts
// Check idle timer. Notify upper layer on timeout
void ISDNQ921Passive::timerTick(const Time& when)
{
    Lock lock(l2Mutex(),SignallingEngine::maxLockWait());
    if (!(lock.locked() && m_idleTimer.timeout(when.msec())))
	return;
    // Timeout. Notify layer 3. Restart timer
    XDebug(this,DebugNote,"Timeout. Channel was idle for " FMT64 " ms",m_idleTimer.interval());
    m_idleTimer.start(when.msec());
    lock.drop();
    idleTimeout();
}

// Process a packet received by the receiver's interface
bool ISDNQ921Passive::receivedPacket(const DataBlock& packet)
{
    if (!packet.length())
	return false;
    Lock lock(l2Mutex());
    XDebug(this,DebugAll,"Received packet (Length: %u)",packet.length());
    ISDNFrame* frame = parsePacket(packet);
    if (!frame) {
	if (!m_errorReceive)
	    Debug(this,DebugNote,"Received invalid frame (Length: %u)",packet.length());
	m_errorReceive = true;
	return false;
    }
    m_errorReceive = false;
    // Print & dump
    if (debugAt(DebugInfo) && m_printFrames) {
	String tmp;
	frame->toString(tmp,m_extendedDebug);
	Debug(this,DebugInfo,"Received frame (%p):%s",frame,tmp.c_str());
    }
    if (frame->type() < ISDNFrame::Invalid)
	dump(frame->buffer(),false);
    // Received enough data to parse. Assume the channel not idle (restart timer)
    // If accepted, the frame is a data frame or a unnumbered (SABME,DISC,UA,DM) one
    //   Drop retransmissions of data frames
    //   Send data or notification to the upper layer
    m_idleTimer.start();
    lock.drop();
    bool cmd,value;
    if (acceptFrame(frame,cmd,value)) {
	if (frame->category() == ISDNFrame::Data) {
	    if (m_lastFrame != frame->ns()) {
		DataBlock tmp;
		frame->getData(tmp);
		m_lastFrame = frame->ns();
		receiveData(tmp,localTei());
	    }
	}
	else
	    dataLinkState(localTei(),cmd,value);
    }
    TelEngine::destruct(frame);
    return true;
}

// Process a notification generated by the attached interface
bool ISDNQ921Passive::notify(SignallingInterface::Notification event)
{
    Lock lock(l2Mutex());
    if (event != SignallingInterface::LinkUp)
	m_hwErrors++;
    else {
	Debug(this,DebugInfo,"Received notification %u: '%s'",
	    event,lookup(event,SignallingInterface::s_notifName));
	return true;
    }
    if (event == SignallingInterface::LinkDown)
	Debug(this,DebugWarn,"Received notification %u: '%s'",
	    event,lookup(event,SignallingInterface::s_notifName));
#ifdef DEBUG
    else if (!(m_hwErrors % 250))
	Debug(this,DebugNote,"Received notification %u: '%s'. Total=%u",
	    event,lookup(event,SignallingInterface::s_notifName,"Undefined"),m_hwErrors);
#endif
    return true;
}

// Accept frame according to Q.921 5.8.5
// Filter received frames. Accept only frames that would generate a notification to the upper layer:
// UI/I and valid SABME/DISC/UA/DM
bool ISDNQ921Passive::acceptFrame(ISDNFrame* frame, bool& cmd, bool& value)
{
    // Update received frames
    m_rxFrames++;
    // Frame already invalid
    if (frame->error() >= ISDNFrame::Invalid)
	return dropFrame(frame);
    // Check SAPI/TEI
    if (frame->sapi() != localSapi() || frame->tei() != localTei())
	return dropFrame(frame,ISDNFrame::typeName(ISDNFrame::ErrInvalidAddress));
    // Valid UI/I
    if (frame->category() == ISDNFrame::Data)
	return true;
    // Check DISC/SABME commands and UA/DM responses
    cmd = (frame->type() == ISDNFrame::SABME || frame->type() == ISDNFrame::DISC);
    bool response = (frame->type() == ISDNFrame::UA || frame->type() == ISDNFrame::DM);
    if (m_checkLinkSide &&
	((cmd && !frame->command()) || (response && frame->command()))) {
	if (detectType()) {
	    m_checkLinkSide = false;
	    changeType();
	}
	else {
	    Debug(this,DebugMild,
		"Received '%s': The remote peer has the same data link side type",
		frame->name());
	    return dropFrame(frame,ISDNFrame::typeName(ISDNFrame::ErrInvalidCR));
	}
    }
    // Normally, SABME/DISC commands and UA/DM responses should have the P/F bit set
    if (cmd || response) {
	if (!frame->poll())
#ifndef Q921_PASIVE_NOCHECK_PF
	    return dropFrame(frame,"P/F bit not set");
#else
	    DDebug(this,DebugNote,"Received '%s' without P/F bit set",frame->name());
#endif
	m_checkLinkSide = detectType();
	if (cmd)
	    value = (frame->type() == ISDNFrame::SABME);
	else
	    value = (frame->type() == ISDNFrame::UA);
	return true;
    }
    // Drop valid frames without debug message (it would be too much) and without counting them:
    //    Supervisory frames (Since we don't synchronize, we don't process them)
    //    Unsupported valid unnumbered frames (e.g. XID, UA/DM with P/F bit set ....)
    if (frame->type() < ISDNFrame::Invalid)
	return false;
    return dropFrame(frame);
}

bool ISDNQ921Passive::dropFrame(const ISDNFrame* frame, const char* reason)
{
    m_rxDroppedFrames++;
    DDebug(this,DebugNote,"Dropping frame (%p): %s. Reason: %s",
	frame,frame->name(),reason ? reason : ISDNFrame::typeName(frame->error()));
    return false;
}

/**
 * ISDNLayer2
 */
const TokenDict ISDNLayer2::m_states[] = {
	{"Released",      Released},
	{"WaitEstablish", WaitEstablish},
	{"Established",   Established},
	{"WaitRelease",   WaitRelease},
	{0,0}
	};

ISDNLayer2::ISDNLayer2(const NamedList& params, const char* name, u_int8_t tei)
    : SignallingComponent(name,&params),
      m_layer3(0),
      m_layerMutex(true,"ISDNLayer2::layer"),
      m_layer3Mutex(true,"ISDNLayer2::layer3"),
      m_state(Released),
      m_network(false),
      m_detectType(false),
      m_sapi(0),
      m_tei(0),
      m_ri(0),
      m_lastUp(0),
      m_checked(false),
      m_teiAssigned(false),
      m_autoRestart(true),
      m_maxUserData(260)
{
    XDebug(this,DebugAll,"ISDNLayer2 '%s' comp=%p [%p]",name,static_cast<const SignallingComponent*>(this),this);
    m_network = params.getBoolValue(YSTRING("network"),false);
    m_detectType = params.getBoolValue(YSTRING("detect"),false);
    int tmp = params.getIntValue(YSTRING("sapi"),0);
    m_sapi = (tmp >= 0 && tmp <= Q921_SAPI_MANAGEMENT) ? tmp : 0;
    tmp = params.getIntValue(YSTRING("tei"),tei);
    m_tei = (tmp >= 0 && tmp < Q921_TEI_BROADCAST) ? tmp : 0;
    teiAssigned(true);
    m_autoRestart = params.getBoolValue(YSTRING("auto-restart"),true);
    m_maxUserData = params.getIntValue(YSTRING("maxuserdata"),260);
    if (!m_maxUserData)
	m_maxUserData = 260;
}

ISDNLayer2::~ISDNLayer2()
{
    if (m_layer3)
	Debug(this,DebugGoOn,"Destroyed with Layer 3 (%p) attached",m_layer3);
    attach(0);
    XDebug(this,DebugAll,"~ISDNLayer2");
}

// Attach an ISDN Q.931 Layer 3 if the given parameter is different from the one we have
void ISDNLayer2::attach(ISDNLayer3* layer3)
{
    Lock lock(m_layer3Mutex);
    if (m_layer3 == layer3)
	return;
    cleanup();
    ISDNLayer3* tmp = m_layer3;
    m_layer3 = layer3;
    lock.drop();
    if (tmp) {
	if (engine() && engine()->find(tmp))
	    tmp->attach(0);
	Debug(this,DebugAll,"Detached L3 (%p,'%s') [%p]",
	    tmp,tmp->toString().safe(),this);
    }
    if (!layer3)
	return;
    Debug(this,DebugAll,"Attached L3 (%p,'%s') [%p]",layer3,layer3->toString().safe(),this);
    insert(layer3);
    layer3->attach(this);
}

// Parse a received packet, create a frame from it
ISDNFrame* ISDNLayer2::parsePacket(const DataBlock& packet)
{
    if (packet.null())
	return 0;
    Lock lock(m_layerMutex);
    ISDNFrame* frame = ISDNFrame::parse(packet,this);
#ifdef XDEBUG
    if (frame) {
	if (debugAt(DebugAll)) {
	    String tmp;
	    frame->toString(tmp,true);
	    Debug(this,DebugInfo,"Parsed frame (%p):%s",frame,tmp.c_str());
	}
    }
    else
	Debug(this,DebugWarn,"Packet with length %u invalid [%p]",packet.length(),this);
#endif
    return frame;
}

// Indication/confirmation of 'multiple frame acknowledged' mode established
void ISDNLayer2::multipleFrameEstablished(u_int8_t tei, bool confirmation, bool timeout)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> tmp = m_layer3;
    m_layer3Mutex.unlock();
    if (tmp)
	tmp->multipleFrameEstablished(tei,confirmation,timeout,this);
    else
	Debug(this,DebugNote,"'Established' notification. No Layer 3 attached");
}

// Indication/confirmation of 'multiple frame acknowledged' mode released
void ISDNLayer2::multipleFrameReleased(u_int8_t tei, bool confirmation, bool timeout)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> tmp = m_layer3;
    m_layer3Mutex.unlock();
    if (tmp)
	tmp->multipleFrameReleased(tei,confirmation,timeout,this);
    else
	Debug(this,DebugNote,"'Released' notification. No Layer 3 attached");
}

// Data link state change command/response
void ISDNLayer2::dataLinkState(u_int8_t tei, bool cmd, bool value)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> tmp = m_layer3;
    m_layer3Mutex.unlock();
    if (tmp)
	tmp->dataLinkState(tei,cmd,value,this);
    else
	Debug(this,DebugNote,"Data link notification. No Layer 3 attached");
}

// Notify layer 3 of data link idle timeout
void ISDNLayer2::idleTimeout()
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> tmp = m_layer3;
    m_layer3Mutex.unlock();
    if (tmp)
	tmp->idleTimeout(this);
    else
	Debug(this,DebugNote,"Data link idle timeout. No Layer 3 attached");
}

// Indication of received data
void ISDNLayer2::receiveData(const DataBlock& data, u_int8_t tei)
{
    m_layer3Mutex.lock();
    RefPointer<ISDNLayer3> tmp = m_layer3;
    m_layer3Mutex.unlock();
    if (tmp)
	tmp->receiveData(data,tei,this);
    else
	Debug(this,DebugNote,"Data received. No Layer 3 attached");
}

// Change TEI ASSIGNED state
void ISDNLayer2::teiAssigned(bool status)
{
    Lock lock(m_layerMutex);
    if (m_teiAssigned == status)
	return;
    m_teiAssigned = status;
    DDebug(this,DebugAll,"%s 'TEI assigned' state",
	m_teiAssigned ? "Enter" : "Exit from");
    if (!m_teiAssigned)
	cleanup();
}

// Change the data link status while in TEI ASSIGNED state
void ISDNLayer2::changeState(State newState, const char* reason)
{
    Lock lock(m_layerMutex);
    if (m_state == newState)
	return;
    if (Established != newState)
	m_lastUp = 0;
    else if (!m_lastUp)
	m_lastUp = Time::secNow();
    if (!m_teiAssigned && (newState != Released))
	return;
    DDebug(this,DebugInfo,"Changing state from '%s' to '%s'%s%s%s",
	stateName(m_state),stateName(newState),
	(reason ? " (" : ""),c_safe(reason),(reason ? ")" : ""));
    m_state = newState;
}

// Change the interface type
bool ISDNLayer2::changeType()
{
    Lock lock(m_layerMutex);
    Debug(this,DebugNote,"Interface type changed from '%s' to '%s'",
	linkSide(m_network),linkSide(!m_network));
    m_network = !m_network;
    return true;
}

/**
 * ISDNFrame
 */
// Flags used to set/get frame type
#define Q921FRAME_U                 0x03 // U frame
#define Q921FRAME_S                 0x01 // S frame
// U frame: P/F bit
#define Q921FRAME_U_GET_PF          0x10 // Mask to get bit 4: the P/F bit
#define Q921FRAME_U_RESET_PF        0xef // Mask to reset bit 4: the P/F bit
// Masks used to set/get command/response bits
#define Q921FRAME_CR_RR             0x01 // S frame
#define Q921FRAME_CR_UI             0x03 // U frame
#define Q921FRAME_CR_RNR            0x05 // S frame
#define Q921FRAME_CR_REJ            0x09 // S frame
#define Q921FRAME_CR_DM             0x0f // U frame
#define Q921FRAME_CR_DISC           0x43 // U frame
#define Q921FRAME_CR_FRMR           0x87 // U frame
#define Q921FRAME_CR_UA             0x63 // U frame
#define Q921FRAME_CR_SABME          0x6f // U frame
#define Q921FRAME_CR_XID            0xaf // U frame

// Set the address field of a frame header
// buf  Destination buffer
// cr   Command/response type
// network True if the sender is the network side of the data link
// sapi SAPI value
// tei  TEI value
static inline void setAddress(u_int8_t* buf, bool cr, bool network,
	u_int8_t sapi, u_int8_t tei)
{
    // Bit 0 is always 0. Set SAPI and C/R bit (bit 1)
    cr = cr ? ISDNFrame::commandBit(network) : ISDNFrame::responseBit(network);
    buf[0] = sapi << 2;
    if (cr)
	buf[0] |= 0x02;
    // Bit 1 is always 1. Set TEI
    buf[1] = (tei << 1) | 0x01;
}

// Set the control field of an U frame header
// buf  Destination buffer
// cr   Command/response value: P/F bit (bit 4) is 0
// pf   P/F bit
static inline void setControlU(u_int8_t* buf, u_int8_t cr, bool pf)
{
    if (pf)
	buf[2] = cr | Q921FRAME_U_GET_PF;
    else
	buf[2] = cr;
}

// Set the control field of an S or I frame header
// buf    Destination buffer
// cr_ns  S frame: Command/response value (P/F bit (bit 4) is 0)
//        I frame: N(S) value
// nr     N(R) value to set
// pf     P/F bit
static inline void setControl(u_int8_t* buf, u_int8_t cr_ns, u_int8_t nr, bool pf)
{
    buf[2] = cr_ns;
    buf[3] = nr << 1;
    if (pf)
	buf[3] |= 0x01;
}

const TokenDict ISDNFrame::s_types[] = {
	{"DISC", DISC},
	{"DM", DM},
	{"FRMR", FRMR},
	{"I", I},
	{"REJ", REJ},
	{"RNR", RNR},
	{"RR", RR},
	{"SABME", SABME},
	{"UA", UA},
	{"UI", UI},
	{"XID", XID},
	{"Invalid frame", Invalid},
	{"Unknown command/response", ErrUnknownCR},
	{"Invalid header length", ErrHdrLength},
	{"Information field too long", ErrDataLength},
	{"Invalid N(R) (transmiter receive) sequence number", ErrRxSeqNo},
	{"Invalid N(S) (transmiter send) sequence number", ErrTxSeqNo},
	{"Invalid 'extended address' bit(s)", ErrInvalidEA},
	{"Invalid SAPI/TEI", ErrInvalidAddress},
	{"Unsupported command/response", ErrUnsupported},
	{"Invalid command/response flag", ErrInvalidCR},
	{0,0}
	};

// NOTE:
//   In constructors, the values of SAPI, TEI, N(S), N(R) are not checked to be in their interval:
//	this is done by the parser (when receiveing) and by ISDNLayer2 when assigning these values

// Constructs an undefined frame. Used by the parser
ISDNFrame::ISDNFrame(Type type)
    : m_type(type),
      m_error(type),
      m_category(Error),
      m_command(false),
      m_sapi(0),
      m_tei(0),
      m_poll(false),
      m_ns(0xFF),
      m_nr(0xFF),
      m_headerLength(0),
      m_dataLength(0),
      m_sent(false)
{
}

// Create U/S frames: SABME/DM/DISC/UA/FRMR/XID/RR/RNR/REJ
ISDNFrame::ISDNFrame(Type type, bool command, bool senderNetwork,
	u_int8_t sapi, u_int8_t tei, bool pf, u_int8_t nr)
    : m_type(type),
      m_error(type),
      m_category(Error),
      m_command(command),
      m_senderNetwork(senderNetwork),
      m_sapi(sapi),
      m_tei(tei),
      m_poll(pf),
      m_ns(0xFF),
      m_nr(nr),
      m_headerLength(3),
      m_dataLength(0),
      m_sent(false)
{
    u_int8_t buf[4];
    setAddress(buf,m_command,m_senderNetwork,m_sapi,m_tei);
    u_int8_t cr = 0;
#define Q921_CASE_SET_CRMASK(compare,rvalue,hdrLen,category) \
	case compare: cr = rvalue; m_headerLength = hdrLen; m_category = category; break;
    switch (m_type) {
	Q921_CASE_SET_CRMASK(SABME,Q921FRAME_CR_SABME,3,Unnumbered)
	Q921_CASE_SET_CRMASK(DM,Q921FRAME_CR_DM,3,Unnumbered)
	Q921_CASE_SET_CRMASK(DISC,Q921FRAME_CR_DISC,3,Unnumbered)
	Q921_CASE_SET_CRMASK(UA,Q921FRAME_CR_UA,3,Unnumbered)
	Q921_CASE_SET_CRMASK(FRMR,Q921FRAME_CR_FRMR,3,Unnumbered)
	Q921_CASE_SET_CRMASK(RR,Q921FRAME_CR_RR,4,Supervisory)
	Q921_CASE_SET_CRMASK(RNR,Q921FRAME_CR_RNR,4,Supervisory)
	Q921_CASE_SET_CRMASK(REJ,Q921FRAME_CR_REJ,4,Supervisory)
	Q921_CASE_SET_CRMASK(XID,Q921FRAME_CR_XID,3,Unnumbered)
	default:
	    return;
    }
#undef Q921_CASE_SET_CRMASK
    // Set control field
    if (m_headerLength == 3)
	setControlU(buf,cr,m_poll);
    else
	setControl(buf,cr,m_nr,m_poll);
    // Set frame buffer
    m_buffer.assign(buf,m_headerLength);
}

// Create I/UI frames
ISDNFrame::ISDNFrame(bool ack, bool senderNetwork, u_int8_t sapi, u_int8_t tei,
	bool pf, const DataBlock& data)
    : m_type(I),
      m_error(I),
      m_category(Data),
      m_command(true),
      m_senderNetwork(senderNetwork),
      m_sapi(sapi),
      m_tei(tei),
      m_poll(pf),
      m_ns(0),
      m_nr(0),
      m_headerLength(4),
      m_dataLength(data.length()),
      m_sent(false)
{
    if (!ack) {
	m_type = m_error = UI;
	m_headerLength = 3;
	m_ns = m_nr = 0xff;
    }
    u_int8_t buf[4];
    setAddress(buf,m_command,m_senderNetwork,m_sapi,m_tei);
    if (m_type == I)
	setControl(buf,m_ns << 1,m_nr << 1,m_poll);
    else
	setControlU(buf,Q921FRAME_CR_UI,m_poll);
    m_buffer.assign(buf,m_headerLength);
    m_buffer += data;
}

ISDNFrame::~ISDNFrame()
{
}

// Update transmitter send and transmitter receive values for I (data) frames
void ISDNFrame::update(u_int8_t* ns, u_int8_t* nr)
{
#define NS (((u_int8_t*)(m_buffer.data()))[2])
#define NR (((u_int8_t*)(m_buffer.data()))[3])
    if (m_type != I)
	return;
    if (ns) {
	m_ns = *ns;
	// For I frames bit 0 of N(S) is always 0
	NS = m_ns << 1;
    }
    if (nr) {
	m_nr = *nr;
	// Keep the P/F bit (bit 0)
	NR = (m_nr << 1) | (NR & 0x01);
    }
#undef NR
#undef NS
}

// Put the frame in a string for debug purposes
void ISDNFrame::toString(String& dest, bool extendedDebug) const
{
#define STARTLINE(indent) "\r\n" << indent
    const char* enclose = "\r\n-----";
    const char* ind = "  ";
    dest << enclose;
    dest << STARTLINE("") << name();
    // Dump header
    if (extendedDebug) {
	String tmp;
	tmp.hexify((void*)buffer().data(),headerLength(),' ');
	dest << " - Header dump: " << tmp;
    }
    if (m_error >= Invalid)
	dest << STARTLINE(ind) << "Error: " << typeName(m_error);
    // Address
    dest << STARTLINE(ind) << "SAPI=" << (unsigned int)m_sapi;
    dest << "  TEI=" << (unsigned int)m_tei;
    dest << "  Type=" << (m_command ? "Command" : "Response");
    // Control
    dest << "  Poll/Final=" << (m_poll ? '1' : '0');
    dest << "  Sequence numbers: ";
    switch (m_type) {
	case I:
	    dest << "Send=" << (unsigned int)m_ns;
	    dest << " Recv=" << (unsigned int)m_nr;
	    break;
	case RR:
	case RNR:
	case REJ:
	    dest << "Send=N/A Recv=" << (unsigned int)m_nr;
	    break;
	default: ;
	    dest << "Send=N/A Recv=N/A";
    }
    // Data
    dest << STARTLINE(ind) << "Retransmission=" << String::boolText(m_sent);
    dest << "  Length: Header=" << (unsigned int)m_headerLength;
    dest << " Data=" << (unsigned int)m_dataLength;
    // Dump data
    if (extendedDebug && m_dataLength) {
	String tmp;
	tmp.hexify((char*)buffer().data() + headerLength(),m_dataLength,' ');
        dest << STARTLINE(ind) << "Data dump: " << tmp;
    }
    dest << enclose;
#undef STARTLINE
}

// Parse received buffer. Set frame data. Header description:
// Address: 2 bytes
// Control: 1 or 2 bytes
// Data: Variable
//
// Address field: 2 bytes (1 and 2)
//    Check EA bits: bit 0 of byte 0 must be 0; bit 0 of byte 1 must be 1
//    C/R (command/response) bit: bit 1 of byte 0
//    SAPI: Bits 2-7 of byte 0
//    TEI: Bits 1-7 of byte 1
// Control field: 1 byte (byte 2) for U frames and 2 bytes (bytes 2 and 3) for I/S frames
//     Frame type: Bits 0,1 of of byte 2
//     P/F (Poll/Final) bit: I/S frame: bit 0 of byte 3. U frame: bit 4 of the byte 2
//     Command/response code: I frame: none. S frame: byte 2. U frame: byte 2 with P/F bit reset
ISDNFrame* ISDNFrame::parse(const DataBlock& data, ISDNLayer2* receiver)
{
    // We MUST have 2 bytes for address and at least 1 byte for control field
    if (!receiver || data.length() < 3)
	return 0;
    ISDNFrame* frame = new ISDNFrame(Invalid);
    const u_int8_t* buf = (const u_int8_t*)(data.data());
    // *** Address field: 2 bytes
    // Check EA bits
    if ((buf[0] & 0x01) || !(buf[1] & 0x01)) {
	frame->m_buffer = data;
	frame->m_headerLength = frame->m_buffer.length();
	frame->m_error = ErrInvalidEA;
	return frame;
    }
    // Get C/R bit, SAPI, TEI
    // C/R: (Q.921 Table 1):
    //   network --> user      Command: 1   Response: 0
    //   user    --> network   Command: 0   Response: 1
    // The sender of this frame is the other side of the receiver
    frame->m_senderNetwork = !receiver->network();
    frame->m_command = isCommand(buf[0] & 0x02,frame->m_senderNetwork);
    frame->m_sapi = buf[0] >> 2;
    frame->m_tei = buf[1] >> 1;
    // *** Control field: 1 (U frame) or 2 (I/S frame) bytes
    // Get frame type: I/U/S. I/S frame type control field is 2 bytes long
    u_int8_t type = buf[2] & 0x03;
    if (type != Q921FRAME_U && data.length() < 4) {
	frame->m_buffer = data;
	frame->m_headerLength = 3;
	frame->m_error = ErrHdrLength;
	return frame;
    }
    // Adjust frame header length. Get P/F bit
    // Get counters. Set frame type
#define Q921_CASE_SETTYPE(compare,rvalue,category)\
	case compare: frame->m_type = frame->m_error = rvalue; frame->m_category = category; break;
    switch (type) {
	case Q921FRAME_U:
	    frame->m_headerLength = 3;
	    frame->m_poll = (buf[2] & Q921FRAME_U_GET_PF) ? true : false;
	    switch (buf[2] & Q921FRAME_U_RESET_PF) {
		Q921_CASE_SETTYPE(Q921FRAME_CR_UA,UA,Unnumbered)
		Q921_CASE_SETTYPE(Q921FRAME_CR_DM,DM,Unnumbered)
		Q921_CASE_SETTYPE(Q921FRAME_CR_DISC,DISC,Unnumbered)
		Q921_CASE_SETTYPE(Q921FRAME_CR_SABME,SABME,Unnumbered)
		Q921_CASE_SETTYPE(Q921FRAME_CR_UI,UI,Data)
		Q921_CASE_SETTYPE(Q921FRAME_CR_FRMR,FRMR,Unnumbered)
		Q921_CASE_SETTYPE(Q921FRAME_CR_XID,XID,Unnumbered)
		default:
		    frame->m_type = Invalid;
		    frame->m_error = ErrUnknownCR;
	    }
	    break;
	case Q921FRAME_S:
	    frame->m_headerLength = 4;
	    frame->m_poll = (buf[3] & 0x01) ? true : false;
	    frame->m_nr = buf[3] >> 1;
	    switch (buf[2]) {
		Q921_CASE_SETTYPE(Q921FRAME_CR_RR,RR,Supervisory)
		Q921_CASE_SETTYPE(Q921FRAME_CR_RNR,RNR,Supervisory)
		Q921_CASE_SETTYPE(Q921FRAME_CR_REJ,REJ,Supervisory)
		default:
		    frame->m_type = Invalid;
		    frame->m_error = ErrUnknownCR;
	    }
	    break;
	default:            // I frame
	    frame->m_type = frame->m_error = I;
	    frame->m_category = Data;
	    frame->m_headerLength = 4;
	    frame->m_poll = (buf[3] & 0x01) ? true : false;
	    frame->m_ns = buf[2] >> 1;
	    frame->m_nr = buf[3] >> 1;
    }
#undef Q921_CASE_SETTYPE
    // Copy buffer. Set data length
    frame->m_buffer = data;
    frame->m_dataLength = data.length() - frame->m_headerLength;
    return frame;
}

// Get the Reference number from a frame data block
u_int16_t ISDNFrame::getRi(const DataBlock& data)
{
    int i = data.at(2);
    if (i < 0)
	return 0;
    return (u_int16_t)((data.at(1) << 8) | i);
}

// Build a TEI management message buffer
bool ISDNFrame::buildTeiManagement(DataBlock& data, u_int8_t type, u_int16_t ri, u_int8_t ai)
{
    u_int8_t d[5] = { Q921_MANAGEMENT_TEI, (u_int8_t)(ri >> 8), (u_int8_t)ri,
	type, (u_int8_t)((ai << 1) | 1) };
    data.assign(d,5);
    return true;
}

// Check if a message buffer holds a TEI management frame
bool ISDNFrame::checkTeiManagement() const
{
    const u_int8_t* d = m_buffer.data(m_headerLength);
    return (d && (type() == UI) && (m_dataLength >= 5) && (d[0] == Q921_MANAGEMENT_TEI));
}

/* vi: set ts=8 sw=4 sts=4 noet: */
