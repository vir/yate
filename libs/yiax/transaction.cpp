/**
 * transaction.cpp
 * Yet Another IAX2 Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Author: Marian Podgoreanu
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

#include <yateiax.h>
#include <stdlib.h>

using namespace TelEngine;

const TokenDict IAXTransaction::s_typeName[] = {
    {"New",       New},
    {"RegReq",    RegReq},
    {"RegRel",    RegRel},
    {"Poke",      Poke},
    {"Incorrect", Incorrect},
    {0,0},
};

const TokenDict IAXTransaction::s_stateName[] = {
    {"Connected",                Connected},
    {"NewLocalInvite",           NewLocalInvite},
    {"NewLocalInvite_AuthRecv",  NewLocalInvite_AuthRecv},
    {"NewLocalInvite_RepSent",   NewLocalInvite_RepSent},
    {"NewRemoteInvite",          NewRemoteInvite},
    {"NewRemoteInvite_AuthSent", NewRemoteInvite_AuthSent},
    {"NewRemoteInvite_RepRecv",  NewRemoteInvite_RepRecv},
    {"Terminating",              Terminating},
    {"Terminated",               Terminated},
    {"Unknown",                  Unknown},
    {0,0},
};

String IAXTransaction::s_iax_modNoAuthMethod("Unsupported or missing authentication method or missing challenge");
String IAXTransaction::s_iax_modNoMediaFormat("Unsupported or missing media format or capability");
String IAXTransaction::s_iax_modInvalidAuth("Invalid authentication request, response or challenge");
String IAXTransaction::s_iax_modNoUsername("Username is missing");

static const String s_voiceBeforeAccept = "Received full Voice before Accept";

unsigned char IAXTransaction::m_maxInFrames = 100;

static inline bool canUpdLastAckSeq(u_int32_t seq, u_int32_t last)
{
    int32_t interval = (int32_t)seq - last;
    return (interval <= 32767 && interval > 0) || interval <= -32767;
}

// Print statistics
void IAXMediaData::print(String& buf)
{
    Lock2 lck(m_inMutex,m_outMutex);
    buf << "PS=" << m_sent << ",OS=" << m_sentBytes;
    buf << ",PR=" << m_recv << ",OR=" << m_recvBytes;
    buf << ",PL=" << m_ooPackets << ",OL=" << m_ooBytes;
    buf << ",PD=" << m_dropOut << ",OD=" << m_dropOutBytes;
}


IAXTransaction::IAXTransaction(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno,
	const SocketAddr& addr, void* data)
    : Mutex(true,"IAXTransaction"),
    m_localInitTrans(false),
    m_localReqEnd(false),
    m_type(Incorrect),
    m_state(Unknown),
    m_destroy(false),
    m_accepted(false),
    m_timeStamp(Time::msecNow() - 1),
    m_timeout(0),
    m_addr(addr),
    m_lCallNo(lcallno),
    m_rCallNo(frame->sourceCallNo()),
    m_oSeqNo(0),
    m_iSeqNo(0),
    m_engine(engine),
    m_userdata(data),
    m_lastFullFrameOut(0),
    m_lastAck(0xFFFF),
    m_pendingEvent(0),
    m_currentEvent(0),
    m_retransCount(5),
    m_retransInterval(500),
    m_pingInterval(20000),
    m_timeToNextPing(0),
    m_inTotalFramesCount(1),
    m_inOutOfOrderFrames(0),
    m_inDroppedFrames(0),
    m_authmethod(IAXAuthMethod::MD5),
    m_expire(60),
    m_format(IAXFormat::Audio), m_formatVideo(IAXFormat::Video),
    m_capability(0), m_callToken(false),
    m_adjustTsOutThreshold(0),
    m_adjustTsOutOverrun(0),
    m_adjustTsOutUnderrun(0),
    m_lastVoiceFrameIn(0),
    m_lastVoiceFrameInTs(0),
    m_reqVoiceVNAK(0),
    m_trunkFrame(0),
    m_trunkFrameCallsSet(false),
    m_trunkOutEfficientUse(false),
    m_trunkOutSend(false),
    m_trunkInSyncUsingTs(true),
    m_trunkInStartTime(0),
    m_trunkInTsDelta(0),
    m_trunkInTsDiffRestart(5000),
    m_trunkInFirstTs(0),
    m_startIEs(0)
{
    switch (frame->subclass()) {
	case IAXControl::New:
	    m_type = New;
	    break;
	case IAXControl::RegReq:
	    m_type = RegReq;
	    break;
	case IAXControl::RegRel:
	    m_type = RegRel;
	    break;
	case IAXControl::Poke:
	    m_type = Poke;
	    break;
	default:
	    Debug(m_engine,DebugNote,"Transaction(%u,%u) incoming with unsupported type %u [%p]",
		localCallNo(),remoteCallNo(),frame->subclass(),this);
	    return;
    }
    init();
    // Append frame to incoming list
    Lock lock(this);
    m_inFrames.append(frame);
    incrementSeqNo(frame,true);
}

IAXTransaction::IAXTransaction(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr,
	IAXIEList& ieList, void* data)
    : Mutex(true,"IAXTransaction"),
    m_localInitTrans(true),
    m_localReqEnd(false),
    m_type(type),
    m_state(Unknown),
    m_destroy(false),
    m_accepted(false),
    m_timeStamp(Time::msecNow() - 1),
    m_timeout(0),
    m_addr(addr),
    m_lCallNo(lcallno),
    m_rCallNo(0),
    m_oSeqNo(0),
    m_iSeqNo(0),
    m_engine(engine),
    m_userdata(data),
    m_lastFullFrameOut(0),
    m_lastAck(0xFFFF),
    m_pendingEvent(0),
    m_currentEvent(0),
    m_retransCount(5),
    m_retransInterval(500),
    m_pingInterval(20000),
    m_timeToNextPing(0),
    m_inTotalFramesCount(0),
    m_inOutOfOrderFrames(0),
    m_inDroppedFrames(0),
    m_authmethod(IAXAuthMethod::MD5),
    m_expire(60),
    m_format(IAXFormat::Audio), m_formatVideo(IAXFormat::Video),
    m_capability(0), m_callToken(false),
    m_adjustTsOutThreshold(0),
    m_adjustTsOutOverrun(0),
    m_adjustTsOutUnderrun(0),
    m_lastVoiceFrameIn(0),
    m_lastVoiceFrameInTs(0),
    m_reqVoiceVNAK(0),
    m_trunkFrame(0),
    m_trunkFrameCallsSet(false),
    m_trunkOutEfficientUse(false),
    m_trunkOutSend(false),
    m_trunkInSyncUsingTs(true),
    m_trunkInStartTime(0),
    m_trunkInTsDelta(0),
    m_trunkInTsDiffRestart(5000),
    m_trunkInFirstTs(0),
    m_startIEs(0)
{
    // Init data members
    if (!m_addr.port()) {
	XDebug(m_engine,DebugAll,
	    "IAXTransaction::IAXTransaction(%u,%u). No remote port. Set to default. [%p]",
	    localCallNo(),remoteCallNo(),this);
	m_addr.port(4569);
    }
    init(ieList);
    m_startIEs = new IAXIEList;
    // Create IE list to send
    switch (type) {
	case New:
	    m_startIEs->insertVersion();
	    if (m_username)
		m_startIEs->appendString(IAXInfoElement::USERNAME,m_username);
	    m_startIEs->appendString(IAXInfoElement::CALLING_NUMBER,m_callingNo);
	    if (!m_startIEs->appendIE(ieList,IAXInfoElement::CALLINGTON))
		m_startIEs->appendNumeric(IAXInfoElement::CALLINGTON,m_engine->callerNumType(),1);
	    if (!m_startIEs->appendIE(ieList,IAXInfoElement::CALLINGPRES))
		m_startIEs->appendNumeric(IAXInfoElement::CALLINGPRES,m_engine->callingPres(),1);
	    if (!m_startIEs->appendIE(ieList,IAXInfoElement::CALLINGTNS))
		m_startIEs->appendNumeric(IAXInfoElement::CALLINGTNS,0,2);
	    if (m_callingName)
		m_startIEs->appendString(IAXInfoElement::CALLING_NAME,m_callingName);
	    m_startIEs->appendString(IAXInfoElement::CALLED_NUMBER,m_calledNo);
	    if (m_calledContext)
		m_startIEs->appendString(IAXInfoElement::CALLED_CONTEXT,m_calledContext);
	    m_startIEs->appendNumeric(IAXInfoElement::FORMAT,m_format.format() | m_formatVideo.format(),4);
	    m_startIEs->appendNumeric(IAXInfoElement::CAPABILITY,m_capability,4);
	    m_startIEs->appendString(IAXInfoElement::CODEC_PREFS,String::empty());
	    if (m_callToken)
		m_startIEs->appendBinary(IAXInfoElement::CALLTOKEN,0,0);
	    break;
	case RegReq:
	case RegRel:
	    m_startIEs->appendString(IAXInfoElement::USERNAME,m_username);
	    if (type == RegReq)
		m_startIEs->appendNumeric(IAXInfoElement::REFRESH,m_expire,2);
	    if (m_callToken)
		m_startIEs->appendBinary(IAXInfoElement::CALLTOKEN,0,0);
	    break;
	case Poke:
	    break;
	default:
	    Debug(m_engine,DebugStub,"Transaction(%u,%u) outgoing with unsupported type %u [%p]",
		localCallNo(),remoteCallNo(),m_type,this);
	    delete m_startIEs;
	    m_startIEs = 0;
	    m_type = Incorrect;
	    return;
    }
    init();
}

IAXTransaction::~IAXTransaction()
{
    if (m_startIEs)
	delete m_startIEs;
    setPendingEvent();
    XDebug(m_engine,DebugAll,"IAXTransaction::~IAXTransaction(%u,%u). [%p]",
	localCallNo(),remoteCallNo(),this);
}

IAXTransaction* IAXTransaction::factoryIn(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno,
	const SocketAddr& addr, void* data)
{
    IAXTransaction* tr = new IAXTransaction(engine,frame,lcallno,addr,data);
    if (tr->type() != Incorrect)
	return tr;
    tr->deref();
    return 0;
}

IAXTransaction* IAXTransaction::factoryOut(IAXEngine* engine, Type type, u_int16_t lcallno,
	const SocketAddr& addr, IAXIEList& ieList, void* data)
{
    IAXTransaction* tr = new IAXTransaction(engine,type,lcallno,addr,ieList,data);
    if (tr->type() != Incorrect)
	return tr;
    tr->deref();
    return 0;
}

// Start an outgoing transaction
void IAXTransaction::start()
{
    Lock lck(this);
    if (!(outgoing() && state() == Unknown && m_startIEs))
	return;
    Debug(m_engine,DebugAll,"Transaction(%u) starting [%p]",localCallNo(),this);
    switch (m_type) {
#define IAXTRANS_START(transtype,frmtype) \
    case transtype: postFrameIes(IAXFrame::IAX,frmtype,m_startIEs); break
	IAXTRANS_START(New,IAXControl::New);
	IAXTRANS_START(RegReq,IAXControl::RegReq);
	IAXTRANS_START(RegRel,IAXControl::RegRel);
	IAXTRANS_START(Poke,IAXControl::Poke);
#undef IAXTRANS_START
	default:
	    Debug(m_engine,DebugStub,"Transaction(%u,%u) outgoing with unsupported type %u [%p]",
		localCallNo(),remoteCallNo(),m_type,this);
	    setDestroy();
	    return;
    }
    m_startIEs = 0;
    changeState(NewLocalInvite);
}

IAXTransaction* IAXTransaction::processFrame(IAXFrame* frame)
{
    if (!frame)
	return 0;
    IAXFullFrame* full = frame->fullFrame();
    if (state() == Terminated) {
	if (full)
	    m_engine->sendInval(full,remoteAddr());
	return 0;
    }
    // Mini frame
    if (!full) {
	if (state() == Terminating)
	    return 0;
	int t = 0;
	if (frame->type() == IAXFrame::Voice)
	    t = IAXFormat::Audio;
	else if (frame->type() == IAXFrame::Video)
	    t = IAXFormat::Video;
	else
	    return 0;
	return processMedia(frame->data(),frame->timeStamp(),t,false,frame->mark());
    }
    Lock lock(this);
    m_inTotalFramesCount++;
    // Frame is VNAK ?
    if (frame->type() == IAXFrame::IAX && full->subclass() == IAXControl::VNAK)
	return retransmitOnVNAK(full->iSeqNo());
    bool fAck = frame->type() == IAXFrame::IAX &&
	(full->subclass() == IAXControl::Ack || full->subclass() == IAXControl::Inval);
    if (!fAck && !isFrameAcceptable(full))
	return 0;
    // Video/Voice full frame: process data & format
    if (type() == New && (frame->type() == IAXFrame::Voice ||
	frame->type() == IAXFrame::Video)) {
	if (state() == Terminating)
	    return 0;
	int t = IAXFormat::Audio;
	if (frame->type() == IAXFrame::Voice) {
	    if (outgoing()) {
		if (!m_accepted) {
		    // Code 101: wrong-state-message
		    IAXEvent* e = checkAcceptRecv(s_voiceBeforeAccept,101);
		    if (e) {
			setPendingEvent(e);
			return 0;
		    }
		}
	    }
	    else if (!m_accepted)
		setPendingEvent(internalReject(s_voiceBeforeAccept,101));
	}
	else
	    t = IAXFormat::Video;
	if (!processMediaFrame(full,t))
	    return 0;
	lock.drop();
	if (t == IAXFormat::Audio) {
	    Lock lck(m_dataAudio.m_inMutex);
	    m_lastVoiceFrameIn = Time::now();
	    m_lastVoiceFrameInTs = frame->timeStamp();
	}
	return processMedia(frame->data(),frame->timeStamp(),t,true,frame->mark());
    }
    // Process incoming Ping
    if (frame->type() == IAXFrame::IAX && full->subclass() == IAXControl::Ping) {
	DDebug(m_engine,DebugAll,"Transaction(%u,%u) received Ping iseq=%u oseq=%u stamp=%u [%p]",
	    localCallNo(),remoteCallNo(),frame->fullFrame()->iSeqNo(),frame->fullFrame()->oSeqNo(),
	    frame->timeStamp(),this);
	postFrame(IAXFrame::IAX,IAXControl::Pong,0,0,frame->timeStamp(),true);
	return 0;
    }
    // Terminating: append only ACK and INVAL frames to incoming frame list
    // We sent ACK for all other and there is nothing else to be done for them
    if (state() == Terminating && !fAck)
	return 0;
    // Do we have enough space to keep this frame ?
    if (m_inFrames.count() == m_maxInFrames) {
	Debug(m_engine,DebugWarn,
	    "Transaction(%u,%u). Incoming buffer overrun (MAX=%u) [%p]",
	    localCallNo(),remoteCallNo(),m_maxInFrames,this);
	m_inDroppedFrames++;
	return 0;
    }
    m_inFrames.append(frame);
    Debug(m_engine,DebugAll,
	"Transaction(%u,%u) enqueued Frame(%u,%u) iseq=%u oseq=%u stamp=%u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),full->subclass(),
	full->iSeqNo(),full->oSeqNo(),frame->timeStamp(),this);
    return this;
}

IAXTransaction* IAXTransaction::processMedia(DataBlock& data, u_int32_t tStamp, int type,
    bool full, bool mark)
{
    if (state() == Terminated || state() == Terminating)
	return 0;
    IAXMediaData* d = getData(type);
    IAXFormat* fmt = getFormat(type);
    if (!(d && fmt)) {
	Debug(m_engine,DebugStub,
	    "IAXTransaction::processMedia() no media data for type '%s' [%p]",
	    IAXFormat::typeName(type),this);
	return 0;
    }
    Lock lck(d->m_inMutex);
    if (type == IAXFormat::Audio && !m_lastVoiceFrameIn) {
	receivedVoiceMiniBeforeFull();
	return 0;
    }
    const IAXFormatDesc& desc = fmt->formatDesc(true);
    if (!desc.format()) {
	if (d->m_showInNoFmt) {
	    Debug(m_engine,DebugInfo,
		"Transaction(%u,%u) received %s data without format [%p]",
		localCallNo(),remoteCallNo(),fmt->typeName(),this);
	    d->m_showInNoFmt = false;
	}
	return 0;
    }
    if (!d->m_startedIn) {
	d->m_startedIn = true;
	Debug(m_engine,DebugAll,"Transaction(%u,%u) started incoming media '%s' [%p]",
	    localCallNo(),remoteCallNo(),fmt->typeName(),this);
    }
    d->m_showInNoFmt = true;
    d->m_recv++;
    d->m_recvBytes += data.length();
    if (!full) {
	// Miniframe or video meta frame timestamp
	// Voice: timestamp is lowest 16 bits
	// Video: timestamp is lowest 15 bits
	u_int32_t mask = 0xffff;
	if (type == IAXFormat::Video)
	    mask = 0x7fff;
	tStamp &= mask;
	// Interval between received timestamp and last one:
	// Negative: wraparound if less then half mask
	int delta = (int)tStamp - (int)(d->m_lastIn & mask);
	if (delta < 0 && ((u_int32_t)-delta) < (mask / 2)) {
	    d->m_ooPackets++;
	    d->m_ooBytes += data.length();
	    DDebug(m_engine,DebugNote,
		"Transaction(%u,%u) dropping %u %s mini data mark=%u ts=%u last=%u [%p]",
		localCallNo(),remoteCallNo(),data.length(),fmt->typeName(),
		mark,tStamp,d->m_lastIn & mask,this);
	    return 0;
	}
	// Add upper bits from last frame, adjust timestamp if wrapped around
	tStamp |= d->m_lastIn & ~mask;
	if (delta < 0) {
	    DDebug(m_engine,DebugInfo,
		"Transaction(%u,%u) timestamp wraparound media=%s ts=%u last=%u [%p]",
		localCallNo(),remoteCallNo(),fmt->typeName(),
		tStamp & mask,d->m_lastIn & mask,this);
	    tStamp += mask + 1;
	}
    }
    bool forward = false;
    if (type != IAXFormat::Video)
	forward = (tStamp > d->m_lastIn);
    else
	forward = (tStamp >= d->m_lastIn);
    if (forward) {
	d->m_lastIn = tStamp; // New frame is newer then the last one
	XDebug(m_engine,DebugAll,
	    "Transaction(%u,%u) forwarding %u %s data mark=%u ts=%u [%p]",
	    localCallNo(),remoteCallNo(),data.length(),fmt->typeName(),
	    mark,tStamp,this);
	m_engine->processMedia(this,data,tStamp * desc.multiplier(),type,mark);
	return 0;
    }
    d->m_ooPackets++;
    d->m_ooBytes += data.length();
    DDebug(m_engine,DebugNote,
	"Transaction(%u,%u) dropping %u %s data full=%u mark=%u ts=%u last=%u [%p]",
	localCallNo(),remoteCallNo(),data.length(),fmt->typeName(),
	full,mark,tStamp,d->m_lastIn,this);
    return 0;
}

static inline unsigned int sendMini(IAXTransaction* tr, const DataBlock& d, u_int32_t ts)
{
    unsigned int sent = 0;
    DataBlock buf;
    IAXFrame::buildMiniFrame(buf,tr->localCallNo(),ts,d.data(),d.length());
    tr->getEngine()->writeSocket(buf.data(),buf.length(),tr->remoteAddr(),0,&sent);
    // Decrease sent bytes with mini frame header
    if (sent > 4)
	return sent - 4;
    return 0;
}

static inline void setTrunkFrameCalls(IAXMetaTrunkFrame* frame, bool& set)
{
    if (set)
	return;
    set = true;
    frame->changeCalls(true);
}

unsigned int IAXTransaction::sendMedia(const DataBlock& data, unsigned int tStamp,
    u_int32_t format, int type, bool mark)
{
    if (!data.length())
	return 0;
    if (state() == Terminated || state() == Terminating)
	return 0;
    IAXFormat* fmt = getFormat(type);
    IAXMediaData* d = getData(type);
    if (!(fmt && d)) {
	Debug(m_engine,DebugStub,
	    "IAXTransaction::sendMedia() no media desc for type '%s' [%p]",
	    IAXFormat::typeName(type),this);
	return 0;
    }
    Lock lck(d->m_outMutex);
    u_int64_t msecNow = Time::msecNow();
    u_int32_t transTs = (u_int32_t)(msecNow - m_timeStamp);
    // Check format change
    bool fmtChanged = (fmt->out() != format);
    if (fmtChanged) {
	Debug(m_engine,DebugNote,
	    "Transaction(%u,%u). Outgoing %s format changed %u --> %u [%p]",
	    localCallNo(),remoteCallNo(),fmt->typeName(),fmt->out(),format,this);
	fmt->set(0,0,&format);
    }
    const IAXFormatDesc& desc = fmt->formatDesc(false);
    u_int32_t ts = 0;
    unsigned int delta = 0;
    if (d->m_startedOut) {
	if (desc.multiplier() > 1) {
	    if (d->m_outFirstSrcTs > tStamp) {
		if (d->m_showOutOldTs) {
		    Debug(m_engine,DebugNote,
			"Transaction(%u,%u) dropping outgoing %s %u bytes with old tStamp=%u (first=%u) [%p]",
			localCallNo(),remoteCallNo(),fmt->typeName(),data.length(),
			tStamp,d->m_outFirstSrcTs,this);
		    d->m_showOutOldTs = false;
		}
		d->dropOut(data.length());
		return 0;
	    }
	    d->m_showOutOldTs = true;
	    unsigned int srcTsDelta = (tStamp - d->m_outFirstSrcTs) / desc.multiplier();
	    ts = d->m_outStartTransTs + srcTsDelta;
	    // Audio
	    if (type == IAXFormat::Audio) {
		if (ts > transTs) {
		    // Voice timestamp is past transaction timestamp
		    // Packets arrived on intervals shorter then expected
		    // Data overrun: decrease timestamp
		    delta = ts - transTs;
		    if (delta >= m_adjustTsOutThreshold) {
			d->dropOut(data.length());
			d->m_outStartTransTs -= m_adjustTsOutOverrun;
			DDebug(m_engine,DebugNote,
			    "Transaction(%u,%u) voice overrun ts=%u transTs=%u [%p]",
			    localCallNo(),remoteCallNo(),ts,transTs,this);
			return 0;
		    }
		}
		else if (ts < transTs) {
		    // Voice timestamp is behind transaction timestamp
		    // Packets arrived on intervals longer then expected
		    // Data underrun: increase timestamp
		    delta = transTs - ts;
		    if (delta >= m_adjustTsOutThreshold) {
			d->m_outStartTransTs += m_adjustTsOutUnderrun;
			DDebug(m_engine,DebugInfo,
			    "Transaction(%u,%u) voice underrun ts=%u transTs=%u [%p]",
			    localCallNo(),remoteCallNo(),ts,transTs,this);
		    }
		}
		// Avoid sending the same timestamp twice
		if (ts == d->m_lastOut)
		    ts++;
	    }
	}
	else {
	    ts = transTs;
	    // Audio: avoid sending the same timestamp twice
	    if (type == IAXFormat::Audio && ts == d->m_lastOut)
		ts++;
	}
    }
    else {
	d->m_startedOut = true;
	d->m_outStartTransTs = transTs;
	d->m_outFirstSrcTs = tStamp;
	ts = d->m_outStartTransTs;
	Debug(m_engine,DebugAll,"Transaction(%u,%u) started outgoing media '%s' [%p]",
	    localCallNo(),remoteCallNo(),fmt->typeName(),this);
    }
    if (ts < d->m_lastOut) {
	d->dropOut(data.length());
	DDebug(m_engine,DebugNote,
	    "Transaction(%u,%u) %s ts %u less then last sent %u [%p]",
	    localCallNo(),remoteCallNo(),fmt->typeName(),ts,d->m_lastOut,this);
	return 0;
    }
    // Format changed or timestamp wrapped around
    // Send a full frame
    bool fullFrame = fmtChanged || !d->m_lastOut;
    if (!fullFrame) {
	// Voice: timestamp is lowest 16 bits
	// Video: timestamp is lowest 15 bits
	u_int32_t mask = 0xffff;
	if (type == IAXFormat::Video)
	    mask = 0x7fff;
	// Timestamp wraparound if mini timestamp is less then last one or
	// we had a media gap greater then mask
	fullFrame = ((ts & mask) < (d->m_lastOut & mask)) || ((ts - d->m_lastOut) > mask);
    }
#ifdef DEBUG
    if (fullFrame && !fmtChanged)
	Debug(m_engine,DebugInfo,
	    "Transaction(%u,%u). Sending full frame for media '%s': ts=%u last=%u [%p]",
	    localCallNo(),remoteCallNo(),fmt->typeName(),ts,d->m_lastOut,this);
#endif
    d->m_lastOut = ts;
    unsigned int sent = 0;
    if (type == IAXFormat::Audio) {
	if (fullFrame) {
	    // Send trunked frame before full frame to keep the media order
	    if (m_trunkFrame) {
		setTrunkFrameCalls(m_trunkFrame,m_trunkFrameCallsSet);
		if (m_trunkOutSend)
		    m_trunkFrame->send();
	    }
	    // Release lock while sending full frame to avoid deadlock with transaction
	    //  mutex
	    // There are places when this mutex is taken after transaction mutex
	    lck.drop();
	    postFrame(IAXFrame::Voice,fmt->out(),data.data(),data.length(),ts,true);
	    lck.acquire(d->m_outMutex);
	    sent = data.length();
	}
	else if (m_trunkFrame) {
	    setTrunkFrameCalls(m_trunkFrame,m_trunkFrameCallsSet);
	    m_trunkOutSend = !(m_trunkOutEfficientUse && m_trunkFrame->calls() <= 1);
	    if (m_trunkOutSend)
		sent = m_trunkFrame->add(localCallNo(),data,ts);
	    else
		sent = sendMini(this,data,ts);
	}
	else
	    sent = sendMini(this,data,ts);
    }
    else if (type == IAXFormat::Video) {
	if (fullFrame) {
	    postFrame(IAXFrame::Video,fmt->out(),data.data(),data.length(),ts,true,mark);
	    sent = data.length();
	}
	else {
	    DataBlock buf;
	    IAXFrame::buildVideoMetaFrame(buf,localCallNo(),ts,mark,data.data(),data.length());
	    m_engine->writeSocket(buf.data(),buf.length(),remoteAddr(),0,&sent);
	    // Decrease with mini frame header
	    if (sent > 6)
		sent -= 6;
	    else
		sent = 0;
	}
    }
    else
	Debug(m_engine,DebugStub,
	    "IAXTransaction::sendMedia() not implemented for type '%s'",fmt->typeName());
    d->m_sent++;
    d->m_sentBytes += sent;
    XDebug(m_engine,sent == data.length() ? DebugAll : DebugNote,
	"Transaction(%u,%u) sent %u/%u media=%s mark=%u ts=%u tStamp=%u transTs=%u [%p]",
	localCallNo(),remoteCallNo(),sent,data.length(),
	fmt->typeName(),mark,ts,tStamp,transTs,this);
    return sent;
}

IAXEvent* IAXTransaction::getEvent(const Time& now)
{
    IAXEvent* ev = 0;
    GenObject* obj;
    bool delFrame;
    Lock lock(this);
    if (state() == Terminated)
	return 0;
    if (m_destroy) {
	if (m_currentEvent)
	    return 0;
	return keepEvent(terminate(IAXEvent::Terminated,true));
    }
    // Outgoing waiting to start
    if (outgoing() && state() == Unknown)
	return 0;
    // Send ack for received frames
    ackInFrames();
    // Do we have a generated event ?
    if (m_currentEvent)
	return 0;
    // Waiting for terminate ?
    if (state() == Terminating) {
	if (now >= m_timeout)
	    return keepEvent(terminate(IAXEvent::Timeout,m_localReqEnd));
	// Nothing to be done if remote requested termination
	// We are waiting for retransmissions
	if (!m_localReqEnd)
	    return 0;
    }
    else if (!m_timeToNextPing || now > m_timeToNextPing) {
	// Send ping
	if (m_timeToNextPing)
	    postFrame(IAXFrame::IAX,IAXControl::Ping,0,0,0,false);
	m_timeToNextPing = now + m_pingInterval * 1000;
    }
    // Do we have a pending event ?
    if (m_pendingEvent) {
	ev = m_pendingEvent;
	m_pendingEvent = 0;
	return keepEvent(ev);
    }
    // Process outgoing frames
    ListIterator lout(m_outFrames);
    IAXFrameOut* lastFrameAck = 0;
    delFrame = false;
    for (; (obj = lout.get());) {
	IAXFrameOut* frame = static_cast<IAXFrameOut*>(obj);
	ev = getEventResponse(frame,delFrame);
	// Frame received ACK or other response ?
	if (frame->ack() || delFrame) {
	    frame->setAck();
	    lastFrameAck = frame;
	    // Frame received non ACK response
	    if (ev || delFrame)
		break;
	    if (frame->ackOnly())
		continue;
	}
	// Adjust timeout for acknowledged auth frames sent with no auth response
	// This is used to give some time to remote peer to send us credentials
	if (state() == NewRemoteInvite_AuthSent && frame->ack() && frame->isAuthReq() &&
	    frame->canSetTimeout()) {
	    frame->setTimeout(now + m_engine->challengeTout() * 1000);
	    DDebug(m_engine,DebugAll,
		"Transaction(%u,%u) set absolute timeout for Frame(%u,%u) [%p]",
		localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),this);
	}
	// No response. Timeout ?
	if (!frame->retransCount()) {
	    if (frame->timeForRetrans(now)) {
		Debug(m_engine,m_state == Terminating ? DebugAll : DebugNote,
		    "Transaction(%u,%u) Frame(%u,%u) timed out [%p]",
		    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),this);
		if (m_state == Terminating)
		    // Client already notified: Terminate transaction
		    ev = terminate(IAXEvent::Timeout,true);
		else
		    // Client not notified: Notify it and terminate transaction
		    ev = terminate(IAXEvent::Timeout,true,frame,false);
	    }
	    break;
	}
	// Retransmit ?
	if (frame->timeForRetrans(now)) {
	    if (frame->ack())
		frame->transmitted();   // Frame acknoledged: just update retransmission info
	    else {
		Debug(m_engine,DebugNote,
		    "Transaction(%u,%u) resending Frame(%u,%u) oseq=%u iseq=%u stamp=%u remaining=%u [%p]",
		    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),
		    frame->oSeqNo(),frame->iSeqNo(),frame->timeStamp(),frame->retransCount() - 1,this);
		sendFrame(frame);       // Retransmission
	    }
	}
    }
    // Set the ACK flag for each frame before lastFrameAck and delete it if it must
    if (lastFrameAck) {
        lout.reset();
        for (; (obj = lout.get());) {
	    IAXFrameOut* frame = static_cast<IAXFrameOut*>(obj);
	    if (frame == lastFrameAck) {
		if (ev || delFrame || frame->ackOnly()) {
	            DDebug(m_engine,DebugAll,
			"Transaction(%u,%u) removing outgoing frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
			localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),
			frame->iSeqNo(),frame->timeStamp(),this);
	            m_outFrames.remove(frame,true);
		}
		break;
	    }
	    frame->setAck();
	    DDebug(m_engine,DebugAll,
		"Transaction(%u,%u) removing outgoing frame(%u,%u) with implicit ACK(%u) oseq=%u iseq=%u stamp=%u [%p]",
		    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),lastFrameAck->oSeqNo(),
		    frame->oSeqNo(),frame->iSeqNo(),frame->timeStamp(),this);
	    if (frame->ackOnly()) {
	        DDebug(m_engine,DebugAll,"Transaction(%u,%u) removing outgoing frame(%u,%u) with implicit ACK(%u) oseq=%u iseq=%u stamp=%u [%p]",
		    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),lastFrameAck->oSeqNo(),
		    frame->oSeqNo(),frame->iSeqNo(),frame->timeStamp(),this);
	        m_outFrames.remove(frame,true);
	    }
	}
    }
    if (ev)
        return keepEvent(ev);
    // Process incoming frames
    for (ObjList* o = m_inFrames.skipNull(); o; o = (delFrame ? o->skipNull() : o->skipNext())) {
	delFrame = false;
	IAXFullFrame* frame = static_cast<IAXFullFrame*>(o->get());
	// If frame is ACK, ignore it
	if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack)
	    continue;
	DDebug(m_engine,DebugAll,
	    "Transaction(%u,%u) processing Frame(%u,%u) iseq=%u oseq=%u stamp=%u [%p]",
	    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->iSeqNo(),
	    frame->oSeqNo(),frame->timeStamp(),this);
	if (m_state == IAXTransaction::Unknown)
	    ev = getEventStartTrans(frame,delFrame);  // New transaction
	else
	    ev = getEventRequest(frame,delFrame);
	if (delFrame) {
	    Debug(m_engine,DebugAll,
		"Transaction(%u,%u) removing incoming Frame(%u,%u) iseq=%u oseq=%u stamp=%u [%p]",
		localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),
		frame->iSeqNo(),frame->oSeqNo(),frame->timeStamp(),this);
	    o->remove();
	}
	if (ev)
	    return keepEvent(ev);
    }
    // No pending outgoing frames. No valid requests. Clear incoming frames queue.
    //m_inDroppedFrames += m_inFrames.count();
    //m_inFrames.clear();
    return 0;
}

bool IAXTransaction::sendAccept(unsigned int* expires)
{
    Lock lock(this);
    if (!((type() == New && (state() == NewRemoteInvite || state() == NewRemoteInvite_RepRecv)) ||
	(type() == RegReq && state() == NewRemoteInvite) ||
	((type() == RegReq || type() == RegRel) && state() == NewRemoteInvite_RepRecv)))
	return false;
    m_accepted = true;
    if (type() == New) {
	IAXIEList* ies = new IAXIEList;
	ies->appendNumeric(IAXInfoElement::FORMAT,m_format.format() | m_formatVideo.format(),4);
	ies->appendNumeric(IAXInfoElement::CAPABILITY,m_capability,4);
	postFrameIes(IAXFrame::IAX,IAXControl::Accept,ies,0,true);
	changeState(Connected);
    }
    else {
	IAXIEList* ies = new IAXIEList;
	ies->appendString(IAXInfoElement::USERNAME,m_username);
	if (type() == RegReq) {
	    if (expires)
		m_expire = *expires;
	    ies->appendNumeric(IAXInfoElement::REFRESH,m_expire,2);
	}
	ies->appendIE(IAXInfoElementBinary::packIP(remoteAddr()));
	postFrameIes(IAXFrame::IAX,IAXControl::RegAck,ies,0,true);
	waitForTerminate();
	m_localReqEnd = true;
    }
    return true;
}

bool IAXTransaction::sendHangup(const char* cause, u_int8_t code)
{
    Lock lock(this);
    if (type() != New || state() == Terminated || state() == Terminating)
	return false;
    IAXIEList* ies = new IAXIEList;
    if (!TelEngine::null(cause))
	ies->appendString(IAXInfoElement::CAUSE,cause);
    if (code)
	ies->appendNumeric(IAXInfoElement::CAUSECODE,code,1);
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Hangup cause='%s' [%p]",
	localCallNo(),remoteCallNo(),cause,this);
    postFrameIes(IAXFrame::IAX,IAXControl::Hangup,ies,0,true);
    waitForTerminate();
    m_localReqEnd = true;
    return true;
}

bool IAXTransaction::sendReject(const char* cause, u_int8_t code)
{
    Lock lock(this);
    if (state() == Terminated || state() == Terminating)
	return false;
    IAXControl::Type frametype;
    switch (type()) {
	case New:
	    frametype = IAXControl::Reject;
	    if (TelEngine::null(cause))
		cause = 0;
	    break;
	case RegReq:
	case RegRel:
	    frametype = IAXControl::RegRej;
	    // Parameters are required for this frame
	    if (!code)
		code = 29;               // Facility rejected
	    if (!cause)
		cause = "";
	    break;
	case Poke:
	default:
	    return false;
    }
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Reject cause='%s' code=%u [%p]",
	localCallNo(),remoteCallNo(),cause,code,this);
    IAXIEList* ies = new IAXIEList;
    if (cause)
	ies->appendString(IAXInfoElement::CAUSE,cause);
    if (code)
	ies->appendNumeric(IAXInfoElement::CAUSECODE,code,1);
    postFrameIes(IAXFrame::IAX,frametype,ies,0,true);
    waitForTerminate();
    m_localReqEnd = true;
    return true;
}

bool IAXTransaction::sendAuth()
{
    Lock lock(this);
    if (!((type() == New || type() == RegReq || type() == RegRel) && state() == NewRemoteInvite))
	return false;
    switch (m_authmethod) {
	case IAXAuthMethod::MD5:
	    m_challenge = (int)Random::random();
	    break;
	case IAXAuthMethod::RSA:
	case IAXAuthMethod::Text:
	default:
	    return false;
    }
    IAXControl::Type t = IAXControl::Unsupport;
    switch (type()) {
	case New:
	    t = IAXControl::AuthReq;
	    break;
	case RegReq:
	case RegRel:
	    t = IAXControl::RegAuth;
	    break;
	default: ;
    }
    if (t != IAXControl::Unsupport) {
	IAXIEList* ies = new IAXIEList;
	ies->appendString(IAXInfoElement::USERNAME,m_username);
	ies->appendNumeric(IAXInfoElement::AUTHMETHODS,m_authmethod,2);
	ies->appendString(IAXInfoElement::CHALLENGE,m_challenge);
	postFrameIes(IAXFrame::IAX,t,ies);
    }
    changeState(NewRemoteInvite_AuthSent);
    return true;
}

bool IAXTransaction::sendAuthReply(const String& response)
{
    Lock lock(this);
    if (state() != NewLocalInvite_AuthRecv)
	return false;
    m_authdata = response;
    IAXIEList* ies = new IAXIEList;
    IAXControl::Type subclass;
    switch (type()) {
	case New:
	    subclass = IAXControl::AuthRep;
	    break;
	case RegReq:
	    subclass = IAXControl::RegReq;
	    ies->appendString(IAXInfoElement::USERNAME,m_username);
	    ies->appendNumeric(IAXInfoElement::REFRESH,m_expire,2);
	    break;
	case RegRel:
	    subclass = IAXControl::RegRel;
	    ies->appendString(IAXInfoElement::USERNAME,m_username);
	    break;
	default:
	    delete ies;
	    return false;
    }
    if (m_authmethod != IAXAuthMethod::MD5) {
	delete ies;
	return false;
    }
    ies->appendString(IAXInfoElement::MD5_RESULT,response);
    postFrameIes(IAXFrame::IAX,subclass,ies);
    changeState(NewLocalInvite_RepSent);
    return true;
}

bool IAXTransaction::sendText(const char* text)
{
    Lock lock(this);
    if (state() != Connected)
	return false;
    String s(text);
    postFrame(IAXFrame::Text,0,(void*)s.c_str(),s.length(),0,true);
    return true;
}

unsigned char IAXTransaction::getMaxFrameList()
{
    return m_maxInFrames;
}

bool IAXTransaction::setMaxFrameList(unsigned char value)
{
    if (value < IAX2_MAX_TRANSINFRAMELIST) {
	m_maxInFrames =  value;
	return true;
    }
    m_maxInFrames = IAX2_MAX_TRANSINFRAMELIST;
    return false;
}

bool IAXTransaction::abortReg()
{
    if (!(type() == RegReq || type() == RegRel) ||
	state() == Terminating || state() == Terminated)
	return false;
    lock();
    m_userdata = 0;
    m_outFrames.clear();
    unlock();
    sendReject("Aborted");
    return true;
}

bool IAXTransaction::enableTrunking(IAXMetaTrunkFrame* trunkFrame, bool efficientUse)
{
    if (!trunkFrame)
	return false;
    Lock lck(m_dataAudio.m_outMutex);
    if (m_trunkFrame)
	return false;
    // Get a reference to the trunk frame
    if (!trunkFrame->ref())
	return false;
    m_trunkOutSend = false;
    m_trunkFrameCallsSet = false;
    m_trunkOutEfficientUse = efficientUse;
    m_trunkFrame = trunkFrame;
    return true;
}

// Process a received call token
void IAXTransaction::processCallToken(const DataBlock& callToken)
{
    Lock lock(this);
    IAXFrameOut* frame = 0;
    if (state() == NewLocalInvite && m_callToken) {
	ObjList* o = m_outFrames.skipNull();
	frame = o ? static_cast<IAXFrameOut*>(o->get()) : 0;
	if (frame && frame->type() != IAXFrame::IAX && frame->subclass() != IAXControl::New)
	    frame = 0;
    }
    m_callToken = false;
    if (!frame) {
	Debug(m_engine,DebugNote,
	    "Transaction(%u,%u). Received call token in invalid state [%p]",
	    localCallNo(),remoteCallNo(),this);
	return;
    }
    frame->updateIEList(false);
    IAXIEList* ies = frame->ieList();
    if (!ies) {
	Debug(m_engine,DebugNote,
	    "Transaction(%u,%u). No IE list in first frame [%p]",
	    localCallNo(),remoteCallNo(),this);
	return;
    }
    IAXInfoElementBinary* ct = static_cast<IAXInfoElementBinary*>(ies->getIE(IAXInfoElement::CALLTOKEN));
    if (ct)
	ct->setData(callToken.data(),callToken.length());
    else
	ies->appendBinary(IAXInfoElement::CALLTOKEN,(unsigned char*)callToken.data(),callToken.length());
    frame->updateBuffer(m_engine->maxFullFrameDataLen());
    sendFrame(frame);
}

// Process incoming audio miniframes from trunk without timestamps
void IAXTransaction::processMiniNoTs(u_int32_t ts, ObjList& blocks, const Time& now)
{
    Lock lck(m_dataAudio.m_inMutex);
    if (!m_lastVoiceFrameIn) {
	receivedVoiceMiniBeforeFull();
	return;
    }
    u_int32_t tStamp = 0;
    if (m_trunkInSyncUsingTs) {
	if (m_trunkInStartTime) {
	    if (ts < m_trunkInFirstTs) {
		// Restart?
		if ((m_trunkInFirstTs - ts) > m_trunkInTsDiffRestart)
		    restartTrunkIn(now,ts);
		else {
		    // Drop
		    for (ObjList* o = blocks.skipNull(); o; o = o->skipNext()) {
			DataBlock* db = static_cast<DataBlock*>(o->get());
			if (db->length()) {
			    m_dataAudio.m_ooPackets++;
			    m_dataAudio.m_ooBytes += db->length();
			}
		    }
		    return;
		}
	    }
	}
	else
	    restartTrunkIn(now,ts);
	tStamp = m_trunkInTsDelta + (ts - m_trunkInFirstTs);
    }
    else
	tStamp = (u_int32_t)((now - m_lastVoiceFrameIn) / 1000) + m_lastVoiceFrameInTs;
    XDebug(m_engine,DebugAll,"(%u,%u) processMiniNoTs(sync=%u packets=%u) %u --> %u [%p]",
	localCallNo(),remoteCallNo(),m_trunkInSyncUsingTs,blocks.count(),ts,tStamp,this);
    lck.drop();
    for (ObjList* o = blocks.skipNull(); o; o = o->skipNext()) {
	DataBlock* db = static_cast<DataBlock*>(o->get());
	// Signal full frame timestamp (we calculate it from full voice frame)
	processMedia(*db,tStamp,IAXFormat::Audio,true);
	tStamp++;
    }
}

void IAXTransaction::print(bool printStats, bool printFrames, const char* location)
{
    if (m_engine && !m_engine->debugAt(DebugAll))
	printFrames = false;
    String buf;
    if (printFrames && (m_outFrames.skipNull() || m_inFrames.skipNull())) {
	buf << "\r\n-----";
	SocketAddr addr;
	ObjList* l;
	buf << "\r\nOutgoing frames: " << m_outFrames.count();
	for(l = m_outFrames.skipNull(); l; l = l->skipNext()) {
	    IAXFrameOut* frame = static_cast<IAXFrameOut*>(l->get());
	    frame->toString(buf,addr,remoteAddr(),false);
	}
	buf << "\r\nIncoming frames: " << m_inFrames.count();
	for(l = m_inFrames.skipNull(); l; l = l->skipNext()) {
	    IAXFullFrame* frame = static_cast<IAXFullFrame*>(l->get());
	    frame->toString(buf,addr,remoteAddr(),true);
	}
	buf << "\r\n-----";
    }
    if (m_type != New) {
	Debug(m_engine,DebugAll,
	    "Transaction(%u,%u) %s remote=%s:%d type=%u state=%u timestamp=" FMT64U " [%p]%s",
	    localCallNo(),remoteCallNo(),location,remoteAddr().host().c_str(),remoteAddr().port(),
	    type(),state(),(u_int64_t)timeStamp(),this,buf.safe());
	return;
    }
    String stats;
    int level = DebugAll;
    if (printStats) {
	stats << " audio: ";
	m_dataAudio.print(stats);
	if (m_formatVideo.format()) {
	   stats << " video: ";
	   m_dataVideo.print(stats);
	}
    }
    if (m_dataAudio.m_dropOut) {
	Lock lck(m_dataAudio.m_outMutex);
	unsigned int total = m_dataAudio.m_dropOut + m_dataAudio.m_sent;
	float percent = (float)m_dataAudio.m_dropOut / (float)total * 100;
	if (percent > 0.5) {
	    if (percent < 3)
		level = DebugInfo;
	    else if (percent < 5)
		level = DebugNote;
	    else
		level = DebugMild;
	}
	if (!printStats)
	    stats << " dropped audio packets=" << m_dataAudio.m_dropOut << "/" << total;
    }
    Debug(m_engine,level,
	"Transaction(%u,%u) %s remote=%s:%d type=%u state=%u timestamp=" FMT64U "%s [%p]%s",
	localCallNo(),remoteCallNo(),location,remoteAddr().host().c_str(),remoteAddr().port(),
	type(),state(),(u_int64_t)timeStamp(),stats.safe(),this,buf.safe());
}

// Cleanup
void IAXTransaction::destroyed()
{
#ifndef DEBUG
    print(false,false,"destroyed");
#else
    print(true,true,"destroyed");
#endif
    resetTrunk();
    if (state() != Terminating && state() != Terminated)
	sendReject("Server shutdown");
    RefObject::destroyed();
}

void IAXTransaction::init(IAXIEList& ieList)
{
    u_int32_t fmt = 0;
    switch (type()) {
	case New:
	    ieList.getString(IAXInfoElement::USERNAME,m_username);
	    ieList.getString(IAXInfoElement::CALLING_NUMBER,m_callingNo);
	    ieList.getString(IAXInfoElement::CALLING_NAME,m_callingName);
	    ieList.getString(IAXInfoElement::CALLED_NUMBER,m_calledNo);
	    ieList.getString(IAXInfoElement::CALLED_CONTEXT,m_calledContext);
	    ieList.getNumeric(IAXInfoElement::FORMAT,fmt);
	    ieList.getNumeric(IAXInfoElement::CAPABILITY,m_capability);
	    m_capability &= m_engine->capability();
	    fmt &= m_capability;
	    m_format.set(&fmt,&fmt,&fmt);
	    m_formatVideo.set(&fmt,&fmt,&fmt);
	    if (outgoing())
		m_callToken = (0 != ieList.getIE(IAXInfoElement::CALLTOKEN));
	    break;
	case RegReq:
	    ieList.getString(IAXInfoElement::CALLED_NUMBER,m_calledNo);
	    ieList.getString(IAXInfoElement::CALLED_CONTEXT,m_calledContext);
	    ieList.getNumeric(IAXInfoElement::REFRESH,m_expire);
	case RegRel:
	    ieList.getString(IAXInfoElement::USERNAME,m_username);
	    if (outgoing())
		m_callToken = (0 != ieList.getIE(IAXInfoElement::CALLTOKEN));
	    break;
	case Poke:
	default: ;
    }
}

bool IAXTransaction::incrementSeqNo(const IAXFullFrame* frame, bool inbound)
{
    if (frame->type() == IAXFrame::IAX)
	switch (frame->subclass()) {
	    case IAXControl::Ack:
	    case IAXControl::VNAK:
	    case IAXControl::TxAcc:
	    case IAXControl::TxCnt:
	    case IAXControl::Inval:
		return false;
	    default: ;
	}
    if (inbound)
	m_iSeqNo++;
    else
	m_oSeqNo++;
    XDebug(m_engine,DebugAll,"Transaction(%u,%u). Incremented %s=%u for Frame(%u,%u) iseq=%u oseq=%u [%p]",
	localCallNo(),remoteCallNo(),inbound ? "iseq" : "oseq", inbound ? m_iSeqNo : m_oSeqNo,
	frame->type(),frame->subclass(),frame->iSeqNo(),frame->oSeqNo(),this);
    return true;
}

bool IAXTransaction::isFrameAcceptable(const IAXFullFrame* frame)
{
    int64_t delta = frame->oSeqNo() - m_iSeqNo;
    if (!delta) {
	incrementSeqNo(frame,true);
	return true;
    }
    if (delta > 0) {
	// We missed some frames before this one: Send VNAK
	Debug(m_engine,DebugInfo,
	    "Transaction(%u,%u). Received Frame(%u,%u) out of order (oseq=%u expecting %u). Send VNAK [%p]",
	    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),m_iSeqNo,this);
	sendVNAK();
	m_inOutOfOrderFrames++;
	return false;
    }
    XDebug(m_engine,DebugAll,
	"Transaction(%u,%u). Received late Frame(%u,%u) with oseq=%u expecting %u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),
	m_iSeqNo,this);
    sendAck(frame);
    return false;
}

bool IAXTransaction::changeState(State newState)
{
    if (state() == newState)
	return true;
    switch (state()) {
	case Terminated:
	    return false;
	case Terminating:
	    if (newState == Terminated)
		break;
	    return false;
	default: ;
    }
    Debug(m_engine,DebugAll,"Transaction(%u,%u) state changed %s --> %s [%p]",
	localCallNo(),remoteCallNo(),stateName(),lookup(newState,s_stateName),this);
    m_state = newState;
    switch (m_state) {
	case Terminated:
	case Terminating:
	    resetTrunk();
	    break;
	default: ;
    }
    return true;
}

IAXEvent* IAXTransaction::terminate(u_int8_t evType, bool local, IAXFullFrame* frame, bool createIEList)
{
    IAXEvent* ev;
    if (createIEList)
	ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,frame);
    else
	if (frame)
	    ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,frame->type(),frame->subclass());
	else
	    ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,0,0);
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Terminated. Event: %u, Frame(%u,%u) [%p]",
	localCallNo(),remoteCallNo(),evType,ev->frameType(),ev->subclass(),this);
    changeState(Terminated);
    deref();
    return ev;
}

IAXEvent* IAXTransaction::waitForTerminate(u_int8_t evType, bool local, IAXFullFrame* frame)
{
    IAXEvent* ev = 0;
    if (evType != IAXEvent::DontSet) {
	ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,frame);
	Debug(m_engine,DebugAll,
	    "Transaction(%u,%u). Terminating. Event: %u, Frame(%u,%u) [%p]",
	    localCallNo(),remoteCallNo(),evType,ev->frameType(),ev->subclass(),this);
    }
    else
	Debug(m_engine,DebugAll,"Transaction(%u,%u). Terminating [%p]",
	    localCallNo(),remoteCallNo(),this);
    changeState(Terminating);
    unsigned int interval = IAXEngine::overallTout(m_retransInterval,m_retransCount);
    m_timeout = Time::now() + interval * 1000;
    return ev;
}

void IAXTransaction::postFrame(IAXFrame::Type type, u_int32_t subclass, void* data,
    u_int16_t len, u_int32_t tStamp, bool ackOnly, bool mark)
{
    Lock lock(this);
    if (state() == Terminated)
	return;
    // Pong and LagRp don't need timestamp to be adjusted
    // Don't adjust for video
    if (type == IAXFrame::IAX) {
	if (subclass != IAXControl::Pong && subclass != IAXControl::LagRp)
	    adjustTStamp(tStamp);
    }
    else if (type != IAXFrame::Video)
	adjustTStamp(tStamp);
    IAXFrameOut* frame = new IAXFrameOut(type,subclass,m_lCallNo,m_rCallNo,m_oSeqNo,m_iSeqNo,tStamp,
	(unsigned char*)data,len,m_retransCount,m_retransInterval,ackOnly,mark);
    postFrame(frame);
}

// Constructs an IAXFrameOut frame, send it to remote peer and put it in the transmission list
void IAXTransaction::postFrameIes(IAXFrame::Type type, u_int32_t subclass, IAXIEList* ies,
    u_int32_t tStamp, bool ackOnly)
{
    Lock lock(this);
    if (state() == Terminated)
	return;
    adjustTStamp(tStamp);
    IAXFrameOut* frame = new IAXFrameOut(type,subclass,m_lCallNo,m_rCallNo,m_oSeqNo,
	m_iSeqNo,tStamp,ies,m_engine->maxFullFrameDataLen(),m_retransCount,
	m_retransInterval,ackOnly);
    postFrame(frame);
}

bool IAXTransaction::sendFrame(IAXFrameOut* frame, bool vnak)
{
    if (!frame)
	return false;
    bool b = m_engine->writeSocket(frame->data().data(),frame->data().length(),remoteAddr(),frame);
    // Don't modify timeout if transmitted as a response to a VNAK
    if (!vnak) {
	if (frame->retrans())     // Retransmission
	    frame->transmitted();
	else                      // First transmission
	    frame->setRetrans();
    }
    return b;
}

IAXEvent* IAXTransaction::createEvent(u_int8_t evType, bool local, IAXFullFrame* frame, State newState)
{
    IAXEvent* ev;
    changeState(newState);
    switch (m_state) {
	case Terminating:
	    ev = waitForTerminate((IAXEvent::Type)evType,local,frame);
	    break;
	case Terminated:
	    ev = terminate((IAXEvent::Type)evType,local,frame);
	    break;
	default:
	    ev = new IAXEvent((IAXEvent::Type)evType,local,false,this,frame);
    }
    if (ev && ev->getList().invalidIEList()) {
	m_engine->sendInval(frame,remoteAddr());
	delete ev;
	ev = waitForTerminate(IAXEvent::Invalid,local,frame);
    }
    return ev;
}

IAXEvent* IAXTransaction::createResponse(IAXFrameOut* frame, u_int8_t findType, u_int8_t findSubclass, u_int8_t evType,
	bool local, State newState)
{
    IAXFullFrame* ffind = findInFrame((IAXFrame::Type)findType,findSubclass);
    if (ffind) {
	frame->setAck();
	IAXEvent* ev = createEvent(evType,local,ffind,newState);
	m_inFrames.remove(ffind,true);
	return ev;
    }
    return 0;
}

IAXEvent* IAXTransaction::getEventResponse(IAXFrameOut* frame, bool& delFrame)
{
    delFrame = false;
    if (findInFrameAck(frame)) {
	frame->setAck();
	// Terminating frame sent
	if (m_state == Terminating) {
	    bool done = false;
	    if (frame->type() == IAXFrame::IAX &&
		(frame->subclass() == IAXControl::Hangup ||
		frame->subclass() == IAXControl::Reject))
		done = true;
	    if (!outgoing()) {
		if (m_type == RegReq || m_type == RegRel) {
		    if (frame->type() == IAXFrame::IAX) {
			if (frame->subclass() == IAXControl::RegAck ||
			    frame->subclass() == IAXControl::RegRej)
			    done = true;
		    }
		}
	    }
	    if (done) {
		// We are waiting for frame ACK
		// Don't terminate if we retransmitted the frame: we might receive a late ACK
		if (frame->retransCount() == m_retransCount)
		    return terminate(IAXEvent::Terminated,true);
		return 0;
	    }
	}
	// Frame only need ACK
	if (frame->ackOnly())
	    return 0;
    }
    // Frame only need ACK. Didn't found it. Return
    if (frame->ackOnly())
	return 0;
    delFrame = true;
    switch (type()) {
	case New:
	    return getEventResponse_New(frame,delFrame);
	case RegReq:
	case RegRel:
	    return getEventResponse_Reg(frame,delFrame);
	case Poke:
	    IAXEvent* event;
	    if (m_state == NewLocalInvite && frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Poke &&
		0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Pong,IAXEvent::Terminated,false,Terminating))) {
		return event;
	    }
	    break;
	default: ;
    }
    delFrame = false;
    // Internal stuff
    return processInternalOutgoingRequest(frame,delFrame);
}

IAXEvent* IAXTransaction::getEventResponse_New(IAXFrameOut* frame, bool& delFrame)
{
    IAXEvent* ev;
    delFrame = true;
    switch (m_state) {
	case Connected:
	    break;
	case NewLocalInvite:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::New))
		break;
	    // Frame is NEW: AUTHREQ, ACCEPT, REJECT, HANGUP ?
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::AuthReq,IAXEvent::AuthReq,false,NewLocalInvite_AuthRecv)))
		return processAuthReq(ev);
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Accept,IAXEvent::Accept,false,Connected)))
		return processAccept(ev);
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Reject,IAXEvent::Reject,false,Terminating)))
		return ev;
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Hangup,IAXEvent::Hangup,false,Terminating)))
		return ev;
	    break;
	case NewLocalInvite_RepSent:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::AuthRep))
		break;
	    // Frame is AUTHREP: ACCEPT, REJECT, HANGUP ?
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Accept,IAXEvent::Accept,false,Connected)))
		return processAccept(ev);
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Reject,IAXEvent::Reject,false,Terminating)))
		return ev;
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Hangup,IAXEvent::Hangup,false,Terminating)))
		return ev;
	    break;
	case NewRemoteInvite_AuthSent:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::AuthReq))
		break;
	    // Frame is AUTHREQ: AUTHREP, REJECT, HANGUP ?
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::AuthRep,
		IAXEvent::AuthRep,false,NewRemoteInvite_RepRecv)))
		return processAuthRep(ev);
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Reject,IAXEvent::Reject,false,Terminating)))
		return ev;
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Hangup,IAXEvent::Hangup,false,Terminating)))
		return ev;
	    break;
	default: ;
    }
    delFrame = false;
    // Internal stuff
    return processInternalOutgoingRequest(frame,delFrame);
}

IAXEvent* IAXTransaction::processAuthReq(IAXEvent* event)
{
    if (event->type() != IAXEvent::AuthReq)
	return event;
    Debug(m_engine,DebugAll,"Transaction(%u,%u). AuthReq received [%p]",
	localCallNo(),remoteCallNo(),this);
    // Valid authmethod & challenge ?
    u_int32_t authmethod;
    bool bAuthMethod = event->getList().getNumeric(IAXInfoElement::AUTHMETHODS,authmethod) && (authmethod & m_authmethod);
    bool bChallenge = event->getList().getString(IAXInfoElement::CHALLENGE,m_challenge);
    if (bAuthMethod && bChallenge)
	return event;
    delete event;
    // Code 47: noresource
    return internalReject(s_iax_modNoAuthMethod,47);
}

IAXEvent* IAXTransaction::processAccept(IAXEvent* event)
{
    if (event->type() != IAXEvent::Accept)
	return event;
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Accept received [%p]",
	localCallNo(),remoteCallNo(),this);
    if (m_accepted)
	return event;
    m_accepted = true;
    if (processAcceptFmt(&event->getList()))
	return event;
    delete event;
    // Code 58: nomedia
    return internalReject(s_iax_modNoMediaFormat,58);
}

IAXEvent* IAXTransaction::processAuthRep(IAXEvent* event)
{
    if (event->type() != IAXEvent::AuthRep)
	return event;
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Auth Reply received [%p]",
	localCallNo(),remoteCallNo(),this);
    event->getList().getString(IAXInfoElement::MD5_RESULT,m_authdata);
    return event;
}

IAXEvent* IAXTransaction::getEventResponse_Reg(IAXFrameOut* frame, bool& delFrame)
{
    IAXEvent* ev;
    delFrame = true;
    switch (m_state) {
	case NewLocalInvite:
	    if (!(frame->type() == IAXFrame::IAX &&
		(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
		break;
	    // Frame is REGREQ ? Find REGACK. Else: Find REGAUTH
	    if (frame->subclass() == IAXControl::RegReq)
		if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegAck,IAXEvent::Accept,false,Terminating)))
		    return processRegAck(ev);
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegAuth,IAXEvent::AuthReq,false,NewLocalInvite_AuthRecv)))
		return processAuthReq(ev);
	    // REGREJ ?
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegRej,IAXEvent::Reject,false,Terminating)))
		return ev;
	    break;
	case NewLocalInvite_RepSent:
	    if (!(frame->type() == IAXFrame::IAX &&
		(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
		break;
	    // Frame is REGREQ/REGREL. Find REGACK, REGREJ
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegAck,IAXEvent::Accept,false,Terminating)))
		return processRegAck(ev);
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegRej,IAXEvent::Reject,false,Terminating)))
		return ev;
	    break;
	case NewRemoteInvite_AuthSent:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::RegAuth))
		break;
	    // Frame is REGAUTH. Find REGREQ/REGREL, REGREJ
	    if (type() == RegReq) {
		if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegReq,IAXEvent::AuthRep,false,NewRemoteInvite_RepRecv)))
		    return processAuthRep(ev);
	    }
	    else {
		if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegRel,IAXEvent::AuthRep,false,NewRemoteInvite_RepRecv)))
		    return processAuthRep(ev);
	    }
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::RegRej,IAXEvent::Reject,false,Terminating)))
		return ev;
	    break;
	default: ;
    }
    delFrame = false;
    return processInternalOutgoingRequest(frame,delFrame);
}

IAXEvent* IAXTransaction::processRegAck(IAXEvent* event)
{
    event->getList().getNumeric(IAXInfoElement::REFRESH,m_expire);
    event->getList().getString(IAXInfoElement::CALLING_NAME,m_callingName);
    event->getList().getString(IAXInfoElement::CALLING_NUMBER,m_callingNo);
    return event;
}

IAXEvent* IAXTransaction::getEventStartTrans(IAXFullFrame* frame, bool& delFrame)
{
    IAXEvent* ev;
    delFrame = true;
    switch (type()) {
	case New:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::New))
		break;
	    ev = createEvent(IAXEvent::New,false,frame,NewRemoteInvite);
	    if (ev) {
		// Check version
		if (!ev->getList().validVersion()) {
		    delete ev;
		    sendReject("Unsupported or missing protocol version");
		    return 0;
		}
		init(ev->getList());
	    }
	    return ev;
	case RegReq:
	case RegRel:
	    if (!(frame->type() == IAXFrame::IAX &&
		(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
		break;
	    ev = createEvent(IAXEvent::New,false,frame,NewRemoteInvite);
	    if (!ev->getList().getIE(IAXInfoElement::USERNAME))
		// Code 96: missing-mandatory-ie
		return internalReject(s_iax_modNoUsername,96);
	    init(ev->getList());
	    return ev;
	case Poke:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Poke))
		break;
	    // Send PONG
	    postFrame(IAXFrame::IAX,IAXControl::Pong,0,0,frame->timeStamp(),true);
	    return createEvent(IAXEvent::Terminated,false,0,Terminating);
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::getEventRequest(IAXFullFrame* frame, bool& delFrame)
{
    XDebug(m_engine,DebugAll,
	"Transaction(%u,%u) getEventRequest() frame %p (%u,%u) oseq: %u iseq: %u [%p]",
	localCallNo(),remoteCallNo(),frame,frame->type(),frame->subclass(),
	frame->oSeqNo(),frame->iSeqNo(),this);
    IAXEvent* ev;
    delFrame = true;
    // INVAL ?
    if (frame->isInval()) {
	Debug(m_engine,DebugAll,"Transaction(%u,%u). Received INVAL. Terminate [%p]",
	    localCallNo(),remoteCallNo(),this);
	return createEvent(IAXEvent::Invalid,false,frame,Terminated);
    }
    switch (type()) {
	case New:
	    return getEventRequest_New(frame,delFrame);
	case RegReq:
	case RegRel:
	    switch (m_state) {
		case NewLocalInvite_AuthRecv:
		case NewRemoteInvite:
		case NewRemoteInvite_RepRecv:
		    if (0 != (ev = remoteRejectCall(frame,delFrame)))
			return ev;
		    break;
		default: ;
	    }
	    break;
	default: ;
    }
    delFrame = false;
    return processInternalIncomingRequest(frame,delFrame);
}

IAXEvent* IAXTransaction::getEventRequest_New(IAXFullFrame* frame, bool& delFrame)
{
    XDebug(m_engine,DebugAll,
	"Transaction(%u,%u) getEventRequest_New() frame %p (%u,%u) oseq: %u iseq: %u [%p]",
	localCallNo(),remoteCallNo(),frame,frame->type(),frame->subclass(),
	frame->oSeqNo(),frame->iSeqNo(),this);
    IAXEvent* ev;
    delFrame = true;
    switch (m_state) {
	case Connected:
	    switch (frame->type()) {
		case IAXFrame::Control:
		    return processMidCallControl(frame,delFrame);
		case IAXFrame::IAX:
		    return processMidCallIAXControl(frame,delFrame);
		case IAXFrame::DTMF:
		    return createEvent(IAXEvent::Dtmf,false,frame,m_state);
		case IAXFrame::Text:
		    return createEvent(IAXEvent::Text,false,frame,m_state);
		case IAXFrame::Noise:
		    return createEvent(IAXEvent::Noise,false,frame,m_state);
		// NOT IMPLEMENTED
		case IAXFrame::Video:
		case IAXFrame::Image:
		case IAXFrame::HTML:
		    return createEvent(IAXEvent::NotImplemented,false,frame,m_state);
		default: ;
	    }
	    break;
	case NewLocalInvite_AuthRecv:
	case NewRemoteInvite:
	case NewRemoteInvite_RepRecv:
	    if (0 != (ev = remoteRejectCall(frame,delFrame)))
		return ev;
	    break;
	default: ;
    }
    delFrame = false;
    return processInternalIncomingRequest(frame,delFrame);
}

IAXFullFrame* IAXTransaction::findInFrame(IAXFrame::Type type, u_int32_t subclass)
{
    for (ObjList* l = m_inFrames.skipNull(); l; l = l->next()) {
	IAXFullFrame* frame = static_cast<IAXFullFrame*>(l->get());
	if (frame && frame->type() == type && frame->subclass() == subclass)
	    return frame;
    }
    return 0;
}

bool IAXTransaction::findInFrameTimestamp(const IAXFullFrame* frameOut, IAXFrame::Type type, u_int32_t subclass)
{
    IAXFullFrame* frame = 0;
    // Loose timestamp check for Ping/Pong
    // Received timestamp can be greater then the sent one
    bool looseTimestamp = type == IAXFrame::IAX && subclass == IAXControl::Pong;
    for (ObjList* l = m_inFrames.skipNull(); l; l = l->skipNext()) {
	frame = static_cast<IAXFullFrame*>(l->get());
	if (frame->type() == type && frame->subclass() == subclass) {
	    bool match = looseTimestamp ? frame->timeStamp() >= frameOut->timeStamp() :
		frame->timeStamp() == frameOut->timeStamp();
	    if (match)
		break;
	}
	frame = 0;
    }
    if (frame) {
	m_inFrames.remove(frame,true);
	return true;
    }
    return false;
}

bool IAXTransaction::findInFrameAck(const IAXFullFrame* frameOut)
{
    if (!frameOut)
	return false;
    if (frameOut->type() == IAXFrame::IAX && frameOut->subclass() == IAXControl::Ping)
	return false;
    IAXFullFrame* frame = 0;
    for (ObjList* l = m_inFrames.skipNull(); l; l = l->skipNext()) {
	frame = static_cast<IAXFullFrame*>(l->get());
	if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack &&
	    frame->timeStamp() == frameOut->timeStamp() && frame->oSeqNo() == frameOut->iSeqNo())
	    break;
	frame = 0;
    }
    if (!frame)
	return false;
    DDebug(m_engine,DebugAll,
	"Transaction(%u,%u). Received ACK for Frame(%u,%u) oseq: %u iseq: %u [%p]",
	localCallNo(),remoteCallNo(),frameOut->type(),frameOut->subclass(),
	frameOut->oSeqNo(),frameOut->iSeqNo(),this);
    m_inFrames.remove(frame,true);
    return true;
}

void IAXTransaction::ackInFrames()
{
    IAXFullFrame* ack = 0;
    for (ObjList* l = m_inFrames.skipNull(); l; l = l->skipNext()) {
	IAXFullFrame* frame = static_cast<IAXFullFrame*>(l->get());
	if (ack && ack->oSeqNo() > frame->oSeqNo())
	    continue;
	if (!(frame->type() == IAXFrame::IAX &&
	    (frame->subclass() == IAXControl::Ack ||  frame->subclass() == IAXControl::Inval
	    || frame->subclass() == IAXControl::LagRq || frame->subclass() ==  IAXControl::Ping)))
	    ack = frame;
    }
    if (ack && canUpdLastAckSeq(ack->oSeqNo(),m_lastAck))
	sendAck(ack);
}

bool IAXTransaction::sendConnected(IAXFullFrame::ControlType subclass, IAXFrame::Type frametype)
{
    if (state() != Connected)
	return false;
    postFrameIes(frametype,subclass,0,0,true);
    return true;
}

void IAXTransaction::sendAck(const IAXFullFrame* frame)
{
    if (!frame)
	return;
    if (canUpdLastAckSeq(frame->oSeqNo(),m_lastAck))
	m_lastAck = frame->oSeqNo();
    IAXFullFrame* f = new IAXFullFrame(IAXFrame::IAX,IAXControl::Ack,localCallNo(),
	remoteCallNo(),frame->iSeqNo(),m_iSeqNo,frame->timeStamp());
    DDebug(m_engine,DebugInfo,
	"Transaction(%u,%u). Send ACK for Frame(%u,%u) oseq: %u iseq: %u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),
	frame->iSeqNo(),this);
    m_engine->writeSocket(f->data().data(),f->data().length(),remoteAddr(),f);
    f->deref();
}

void IAXTransaction::sendVNAK()
{
    IAXFullFrame* f = new IAXFullFrame(IAXFrame::IAX,IAXControl::VNAK,localCallNo(),
	remoteCallNo(),m_oSeqNo,m_iSeqNo,(u_int32_t)timeStamp());
    m_engine->writeSocket(f->data().data(),f->data().length(),remoteAddr(),f);
    f->deref();
}

void IAXTransaction::sendUnsupport(u_int32_t subclass)
{
    IAXIEList* ies = new IAXIEList;
    u_int8_t val = IAXFrame::packSubclass(subclass);
    ies->appendNumeric(IAXInfoElement::IAX_UNKNOWN,val,1);
    postFrameIes(IAXFrame::IAX,IAXControl::Unsupport,ies,0,true);
}

IAXEvent* IAXTransaction::processInternalOutgoingRequest(IAXFrameOut* frame, bool& delFrame)
{
    delFrame = false;
    if (frame->type() != IAXFrame::IAX)
	return 0;
    delFrame = true;
    switch (frame->subclass()) {
	case IAXControl::Ping:
	    if (findInFrameTimestamp(frame,IAXFrame::IAX,IAXControl::Pong))
		return 0;
	    break;
	case IAXControl::LagRq:
	    if (findInFrameTimestamp(frame,IAXFrame::IAX,IAXControl::LagRp))
		return 0;
	    break;
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::processInternalIncomingRequest(const IAXFullFrame* frame, bool& delFrame)
{
    if (!frame)
	return 0;
    delFrame = true;
    if (frame->type() == IAXFrame::IAX) {
	if (frame->subclass() == IAXControl::LagRq) {
	    postFrame(IAXFrame::IAX,IAXControl::LagRp,0,0,frame->timeStamp(),true);
	    return 0;
	}
	if (frame->subclass() == IAXControl::Pong) {
	    sendAck(frame);
	    return 0;
	}
    }
    Debug(m_engine,DebugAll,
	"Transaction(%u,%u) dropping unhandled Frame(%u,%u) oseq: %u iseq: %u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),
	frame->iSeqNo(),this);
    return 0;
}

IAXEvent* IAXTransaction::processMidCallControl(IAXFullFrame* frame, bool& delFrame)
{
    XDebug(m_engine,DebugAll,
	"Transaction(%u,%u) processMidCallControl() frame %p (%u,%u) oseq: %u iseq: %u [%p]",
	localCallNo(),remoteCallNo(),frame,frame->type(),frame->subclass(),
	frame->oSeqNo(),frame->iSeqNo(),this);
    delFrame = true;
    switch (frame->subclass()) {
	case IAXFullFrame::Hangup:
	    return createEvent(IAXEvent::Hangup,false,frame,Terminating);
	case IAXFullFrame::Busy:
	    return createEvent(IAXEvent::Busy,false,frame,Terminating);
	case IAXFullFrame::Ringing:
	    return createEvent(IAXEvent::Ringing,false,frame,m_state);
	case IAXFullFrame::Answer:
	    return createEvent(IAXEvent::Answer,false,frame,Connected);
	case IAXFullFrame::Progressing:
	case IAXFullFrame::Proceeding:
	    return createEvent(IAXEvent::Progressing,false,frame,m_state);
	case IAXFullFrame::Hold:
	case IAXFullFrame::Unhold:
	case IAXFullFrame::Congestion:
	case IAXFullFrame::FlashHook:
	case IAXFullFrame::Option:
	case IAXFullFrame::KeyRadio:
	case IAXFullFrame::UnkeyRadio:
	case IAXFullFrame::VidUpdate:
	    return createEvent(IAXEvent::NotImplemented,false,frame,m_state);
	default: ;
    }
    delFrame = false;
    return processInternalIncomingRequest(frame,delFrame);
}

IAXEvent* IAXTransaction::processMidCallIAXControl(IAXFullFrame* frame, bool& delFrame)
{
    XDebug(m_engine,DebugAll,
	"Transaction(%u,%u) processMidCallIAXControl() frame %p (%u,%u) oseq: %u iseq: %u [%p]",
	localCallNo(),remoteCallNo(),frame,frame->type(),frame->subclass(),
	frame->oSeqNo(),frame->iSeqNo(),this);
    delFrame = true;
    switch (frame->subclass()) {
	case IAXControl::Ping:
	case IAXControl::LagRq:
	case IAXControl::Pong:
	case IAXControl::LagRp:
	case IAXControl::VNAK:
	    return processInternalIncomingRequest(frame,delFrame);
	case IAXControl::Quelch:
	    return createEvent(IAXEvent::Quelch,false,frame,m_state);
	case IAXControl::Unquelch:
	    return createEvent(IAXEvent::Unquelch,false,frame,m_state);
	case IAXControl::Hangup:
	case IAXControl::Reject:
	    return createEvent(IAXEvent::Hangup,false,frame,Terminating);
	case IAXControl::New:
	case IAXControl::Accept:
	case IAXControl::AuthReq:
	case IAXControl::AuthRep:
	    // Already received: Ignore
	    return 0;
	case IAXControl::Inval:
	    return createEvent(IAXEvent::Invalid,false,frame,Terminated);
	case IAXControl::Unsupport:
	    return 0;
	case IAXControl::Transfer:
	case IAXControl::TxReady:
	    sendUnsupport(frame->subclass());
	    return createEvent(IAXEvent::NotImplemented,false,frame,Terminating);
	case IAXControl::DpReq:
	case IAXControl::DpRep:
	case IAXControl::Dial:
	case IAXControl::TxReq:
	case IAXControl::TxCnt:
	case IAXControl::TxAcc:
	case IAXControl::TxRel:
	case IAXControl::TxRej:
	case IAXControl::MWI:
	case IAXControl::Provision:
	case IAXControl::FwData:
	    sendUnsupport(frame->subclass());
	    return createEvent(IAXEvent::NotImplemented,false,frame,state());
	default:
	    sendUnsupport(frame->subclass());
	    return 0;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::remoteRejectCall(IAXFullFrame* frame, bool& delFrame)
{
    delFrame = true;
    switch (type()) {
	case New:
	    if ((frame->type() == IAXFrame::IAX && (frame->subclass() == IAXControl::Hangup || frame->subclass() == IAXControl::Reject)) ||
	        (frame->type() == IAXFrame::Control && frame->subclass() == IAXFullFrame::Hangup))
		return createEvent(IAXEvent::Reject,false,frame,Terminating);
	    break;
	case RegReq:
	case RegRel:
	    if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::RegRej)
		return createEvent(IAXEvent::Reject,false,frame,Terminating);
	    break;
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXTransaction* IAXTransaction::processMediaFrame(const IAXFullFrame* frame, int type)
{
    DDebug(m_engine,DebugAll,
	"Transaction(%u,%u). Received %s (%u,%u) iseq=%u oseq=%u stamp=%u [%p]",
	localCallNo(),remoteCallNo(),IAXFrame::typeText(frame->type()),
	frame->type(),frame->subclass(),
	frame->iSeqNo(),frame->oSeqNo(),frame->timeStamp(),this);
    sendAck(frame);
    IAXFormat* fmt = getFormat(type);
    if (!fmt)
	return this;
    if (!frame->subclass())
	return this;
    // Check the format
    u_int32_t recvFmt = IAXFormat::mask(frame->subclass(),type);
    if (recvFmt == fmt->in())
        return this;
    if (!recvFmt) {
	String tmp;
	IAXFormat::formatList(tmp,frame->subclass());
	Debug(m_engine,DebugInfo,
	    "IAXTransaction(%u,%u). Received %s frame with invalid format=%s (0x%x) [%p]",
	    localCallNo(),remoteCallNo(),IAXFrame::typeText(frame->type()),
	    tmp.c_str(),frame->subclass(),this);
	return this;
    }
    if (!IAXFormat::formatName(recvFmt)) {
	Debug(m_engine,DebugNote,
	    "IAXTransaction(%u,%u). Received %s frame with unknown format=0x%x [%p]",
	    localCallNo(),remoteCallNo(),IAXFrame::typeText(frame->type()),recvFmt,this);
	// Code 58: nomedia
	setPendingEvent(internalReject(s_iax_modNoMediaFormat,58));
	return 0;
    }
    // We might have an incoming media format received with an Accept frame
    if (fmt->in()) {
	// Format changed.
	if (m_engine->mediaFormatChanged(this,type,recvFmt)) {
	    Debug(m_engine,DebugNote,
		"Transaction(%u,%u). Incoming %s format changed %u --> %u [%p]",
		localCallNo(),remoteCallNo(),fmt->typeName(),fmt->in(),recvFmt,this);
	    fmt->set(0,&recvFmt,0);
	}
	else {
	    DDebug(m_engine,DebugAll,
		"IAXTransaction(%u,%u). Format change rejected media=%s current=%u [%p]",
		localCallNo(),remoteCallNo(),fmt->typeName(),fmt->format(),this);
	    // Code 58: nomedia
	    setPendingEvent(internalReject(s_iax_modNoMediaFormat,58));
	    return 0;
        }
    }
    else {
	fmt->set(&recvFmt,0,0);
	if (!m_engine->acceptFormatAndCapability(this,0,type))
	    return 0;
    }
    return this;
}

IAXTransaction* IAXTransaction::retransmitOnVNAK(u_int16_t seqNo)
{
    int c = 0;
    for (ObjList* l = m_outFrames.skipNull(); l; l = l->next()) {
	IAXFrameOut* frame = static_cast<IAXFrameOut*>(l->get());
	if (frame && frame->oSeqNo() >= seqNo) {
	    sendFrame(frame,true);
	    c++;
        }
    }
    DDebug(m_engine,DebugNote,"Transaction(%u,%u). Retransmitted %d frames on VNAK(%u) [%p]",
	localCallNo(),remoteCallNo(),c,seqNo,this);
    return 0;
}

IAXEvent* IAXTransaction::internalReject(const char* reason, u_int8_t code)
{
    Debug(m_engine,DebugAll,
	"Transaction(%u,%u). Internal reject cause='%s' code=%u [%p]",
	localCallNo(),remoteCallNo(),reason,code,this);
    sendReject(reason,code);
    IAXEvent* event = new IAXEvent(IAXEvent::Reject,true,true,this,IAXFrame::IAX,IAXControl::Reject);
    event->getList().appendString(IAXInfoElement::CAUSE,reason);
    if (code)
	event->getList().appendNumeric(IAXInfoElement::CAUSECODE,code,1);
    m_localReqEnd = true;
    return event;
}

void IAXTransaction::eventTerminated(IAXEvent* event)
{
    Lock lock(this);
    if (event && event == m_currentEvent) {
	XDebug(m_engine,DebugAll,"Transaction(%u,%u). Event (%p) terminated. [%p]",
	    localCallNo(),remoteCallNo(),event,this);
	m_currentEvent = 0;
    }
}

void IAXTransaction::adjustTStamp(u_int32_t& tStamp)
{
    if (!tStamp) {
	tStamp = (u_int32_t)timeStamp();
	// Make sure we don't send old timestamp
	IAXMediaData* d = getData(IAXFormat::Audio);
	if (d) {
	    Lock lck(d->m_outMutex);
	    if (tStamp <= d->m_lastOut)
		tStamp = d->m_lastOut + 1;
	}
    }
    // Adjust timestamp to be different from the last sent
    if (tStamp <= m_lastFullFrameOut)
	tStamp = m_lastFullFrameOut + 1;
    m_lastFullFrameOut = tStamp;
}

void IAXTransaction::postFrame(IAXFrameOut* frame)
{
    if (!frame)
	return;
    Debug(m_engine,DebugAll,
	"Transaction(%u,%u) posting Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),
	m_oSeqNo,m_iSeqNo,frame->timeStamp(),this);
    incrementSeqNo(frame,false);
    m_outFrames.append(frame);
    sendFrame(frame);
}

void IAXTransaction::receivedVoiceMiniBeforeFull()
{
    if (state() == Terminated || state() == Terminating)
	return;
    if (m_reqVoiceVNAK > 15)
	return;
    m_reqVoiceVNAK++;
    if (m_reqVoiceVNAK == 3)
	Debug(m_engine,DebugAll,
	    "Transaction(%u,%u) received audio miniframe before full voice frame [%p]",
	    localCallNo(),remoteCallNo(),this);
    if (0 == (m_reqVoiceVNAK % 3))
	sendVNAK();
}

void IAXTransaction::resetTrunk()
{
    if (!m_trunkFrame)
	return;
    if (m_trunkFrameCallsSet)
	m_trunkFrame->changeCalls(false);
    TelEngine::destruct(m_trunkFrame);
}

void IAXTransaction::setPendingEvent(IAXEvent* ev)
{
    if (m_pendingEvent)
	delete m_pendingEvent;
    m_pendingEvent = ev;
}

void IAXTransaction::init()
{
    Debug(m_engine,DebugAll,"Transaction %s call=%u type=%s remote=%s:%d [%p]",
	outgoing() ? "outgoing" : "incoming",localCallNo(),typeName(),m_addr.host().c_str(),
	m_addr.port(),this);
    m_engine->getOutDataAdjust(m_adjustTsOutThreshold,m_adjustTsOutOverrun,m_adjustTsOutUnderrun);
    RefPointer<IAXTrunkInfo> ti;
    if (!m_engine->trunkInfo(ti))
	return;
    m_trunkInSyncUsingTs = ti->m_trunkInSyncUsingTs;
    m_trunkInTsDiffRestart = ti->m_trunkInTsDiffRestart;
    m_retransCount = ti->m_retransCount;
    m_retransInterval = ti->m_retransInterval;
    m_pingInterval = ti->m_pingInterval;
    ti = 0;
}

// Process accept format and caps
bool IAXTransaction::processAcceptFmt(IAXIEList* list)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Processing Accept format [%p]",
	localCallNo(),remoteCallNo(),this);
    if (!list)
	return false;
    u_int32_t fmt = 0;
    list->getNumeric(IAXInfoElement::FORMAT,fmt);
    m_format.set(&fmt,0,0);
    m_formatVideo.set(&fmt,0,0);
    m_engine->acceptFormatAndCapability(this,0,IAXFormat::Audio);
    m_engine->acceptFormatAndCapability(this,0,IAXFormat::Video);
    return m_format.format() || m_formatVideo.format();
}

// Process queued ACCEPT. Reject with given reason/code if not found
// Reject with 'nomedia' if found and format is not acceptable
IAXEvent* IAXTransaction::checkAcceptRecv(const char* reason, u_int8_t code)
{
    IAXFullFrame* f = 0;
    for (ObjList* o = m_inFrames.skipNull(); o; o = o->skipNext()) {
	f = static_cast<IAXFullFrame*>(o->get());
	if (f->type() == IAXFrame::IAX && f->subclass() == IAXControl::Accept)
	    break;
	f = 0;
    }
    if (!f)
	return internalReject(reason,code);
    m_accepted = true;
    if (processAcceptFmt(f->ieList()))
	return 0;
    // Code 58: nomedia
    return internalReject(s_iax_modNoMediaFormat,58);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
