/**
 * transaction.cpp
 * Yet Another IAX2 Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 * Author: Marian Podgoreanu
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

#include <yateiax.h>
#include <stdlib.h>

using namespace TelEngine;

String IAXTransaction::s_iax_modNoAuthMethod("Unsupported or missing authentication method or missing challenge");
String IAXTransaction::s_iax_modNoMediaFormat("Unsupported or missing media format or capability");
String IAXTransaction::s_iax_modInvalidAuth("Invalid authentication request, response or challenge");
String IAXTransaction::s_iax_modNoUsername("Username is missing");

unsigned char IAXTransaction::m_maxInFrames = 100;


// Print statistics
void IAXMediaData::print(String& buf)
{
    Lock lck(this);
    buf << "PS=" << m_sent << ",OS=" << m_sentBytes;
    buf << ",PR=" << m_recv << ",OR=" << m_recvBytes;
    buf << ",PL=" << m_ooPackets << ",OL=" << m_ooBytes;
}


IAXTransaction::IAXTransaction(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, 
	const SocketAddr& addr, void* data)
    : Mutex(true,"IAXTransaction"),
    m_localInitTrans(false),
    m_localReqEnd(false),
    m_type(Incorrect),
    m_state(Unknown),
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
    m_trunkFrame(0)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u) incoming type=%u remote=%s:%d [%p]",
	localCallNo(),remoteCallNo(),m_type,m_addr.host().c_str(),m_addr.port(),this);
    // Setup transaction
    m_retransCount = engine->retransCount();
    m_retransInterval = engine->retransInterval();
    m_timeToNextPing = m_timeStamp + m_pingInterval;
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
    m_trunkFrame(0)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u) outgoing type=%u remote=%s:%d [%p]",
	localCallNo(),remoteCallNo(),m_type,m_addr.host().c_str(),m_addr.port(),this);
    // Init data members
    if (!m_addr.port()) {
	XDebug(m_engine,DebugAll,
	    "IAXTransaction::IAXTransaction(%u,%u). No remote port. Set to default. [%p]",
	    localCallNo(),remoteCallNo(),this);
	m_addr.port(4569);
    }
    m_retransCount = engine->retransCount();
    m_retransInterval = engine->retransInterval();
    m_timeToNextPing = m_timeStamp + m_pingInterval;
    init(ieList);
    IAXControl::Type frametype;
    IAXIEList* ies = new IAXIEList;
    // Create IE list to send
    switch (type) {
	case New:
	    ies->insertVersion();
	    ies->appendString(IAXInfoElement::USERNAME,m_username);
	    ies->appendString(IAXInfoElement::CALLING_NUMBER,m_callingNo);
	    ies->appendString(IAXInfoElement::CALLING_NAME,m_callingName);
	    ies->appendString(IAXInfoElement::CALLED_NUMBER,m_calledNo);
	    ies->appendString(IAXInfoElement::CALLED_CONTEXT,m_calledContext);
	    ies->appendNumeric(IAXInfoElement::FORMAT,m_format.format() | m_formatVideo.format(),4);
	    ies->appendNumeric(IAXInfoElement::CAPABILITY,m_capability,4);
	    if (m_callToken)
		ies->appendBinary(IAXInfoElement::CALLTOKEN,0,0);
	    frametype = IAXControl::New;
	    break;
	case RegReq:
	case RegRel:
	    ies->appendString(IAXInfoElement::USERNAME,m_username);
	    ies->appendNumeric(IAXInfoElement::REFRESH,m_expire,2);
	    frametype = (type == RegReq ? IAXControl::RegReq : IAXControl::RegRel);
	    break;
	case Poke:
	    frametype = IAXControl::Poke;
	    break;
	default:
	    Debug(m_engine,DebugStub,"Transaction(%u,%u) outgoing with unsupported type %u [%p]",
		localCallNo(),remoteCallNo(),m_type,this);
	    delete ies;
	    m_type = Incorrect;
	    return;
    }
    postFrameIes(IAXFrame::IAX,frametype,ies);
    changeState(NewLocalInvite);
}

IAXTransaction::~IAXTransaction()
{
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

// Retrieve the media of a given type
IAXFormat* IAXTransaction::getFormat(int type)
{
    if (type == IAXFormat::Audio)
	return &m_format;
    if (type == IAXFormat::Video)
	return &m_formatVideo;
    return 0;
}

// Retrieve the media data for a given type
IAXMediaData* IAXTransaction::getData(int type)
{
    if (type == IAXFormat::Audio)
	return &m_dataAudio;
    if (type == IAXFormat::Video)
	return &m_dataVideo;
    return 0;
}

IAXTransaction* IAXTransaction::processFrame(IAXFrame* frame)
{
    if (!frame)
	return 0;
    if (state() == Terminated) {
	sendInval();
	return 0;
    }
    if (state() == Terminating) {
	// Local terminate: Accept only Ack. Remote terminate: Accept none.
	if (m_localReqEnd && frame->fullFrame()) {
	    if (!(frame->type() == IAXFrame::IAX && frame->fullFrame()->subclass() == IAXControl::Ack))
		return 0;
	}
	else
	    return 0;
    }
    // Mini frame
    if (!frame->fullFrame()) {
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
    if (frame->type() == IAXFrame::IAX && frame->fullFrame()->subclass() == IAXControl::VNAK)
	return retransmitOnVNAK(frame->fullFrame()->iSeqNo());
    // Do we have enough space to keep this frame ?
    if (m_inFrames.count() == m_maxInFrames) {
	Debug(m_engine,DebugWarn,"Transaction(%u,%u). processFrame. Buffer overrun! (MAX=%u)",
	    localCallNo(),remoteCallNo(),m_maxInFrames);
	m_inDroppedFrames++;
	return 0;
    }
    bool fAck = frame->type() == IAXFrame::IAX && frame->fullFrame()->subclass() == IAXControl::Ack;
    if (!fAck && !isFrameAcceptable(frame->fullFrame()))
	return 0;
    incrementSeqNo(frame->fullFrame(),true);
    // Video/Voice full frame: process data & format
    if (type() == New && (frame->type() == IAXFrame::Voice ||
	frame->type() == IAXFrame::Video)) {
	int t = IAXFormat::Audio;
	if (frame->type() == IAXFrame::Video)
	    t = IAXFormat::Video;
	if (!processMediaFrame(frame->fullFrame(),t))
	    return 0;
	// Frame accepted: process voice data
	lock.drop();
	return processMedia(frame->data(),frame->timeStamp(),t,true,frame->mark());
    }
    // Process incoming Ping
    if (frame->type() == IAXFrame::IAX && frame->fullFrame()->subclass() == IAXControl::Ping) {
	DDebug(m_engine,DebugAll,"Transaction(%u,%u) received Ping iseq=%u oseq=%u stamp=%u [%p]",
	    localCallNo(),remoteCallNo(),frame->fullFrame()->iSeqNo(),frame->fullFrame()->oSeqNo(),
	    frame->timeStamp(),this);
	postFrame(IAXFrame::IAX,IAXControl::Pong,0,0,frame->timeStamp(),true);
	return 0;
    }
    // Append frame to incoming frame list
    m_inFrames.append(frame);
    DDebug(m_engine,DebugAll,"Transaction(%u,%u) enqueued Frame(%u,%u) iseq=%u oseq=%u stamp=%u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->fullFrame()->subclass(),
	frame->fullFrame()->iSeqNo(),frame->fullFrame()->oSeqNo(),frame->timeStamp(),this);
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
	    "IAXTransaction::processMedia() no media data for type '%s'",
	    IAXFormat::typeName(type));
	return 0;
    }
    Lock lck(d);
    if (!fmt->in()) {
	if (d->m_showInNoFmt) {
	    Debug(m_engine,DebugInfo,
		"Transaction(%u,%u) received %s data without format [%p]",
		localCallNo(),remoteCallNo(),fmt->typeName(),this);
	    d->m_showInNoFmt = false;
	}
	return 0;
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
	m_engine->processMedia(this,data,tStamp,type,mark);
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

unsigned int IAXTransaction::sendMedia(const DataBlock& data, u_int32_t format,
    int type, bool mark)
{
    if (!data.length())
	return 0;
    if (state() == Terminated || state() == Terminating)
	return 0;
    IAXFormat* fmt = getFormat(type);
    IAXMediaData* d = getData(type);
    if (!(fmt && d)) {
	Debug(m_engine,DebugStub,
	    "IAXTransaction::sendMedia() no media desc for type '%s'",
	    IAXFormat::typeName(type));
	return 0;
    }
    d->m_sent++;
    d->m_sentBytes += data.length();
    u_int32_t ts = (u_int32_t)timeStamp();
    // Avoid sending the same timestamp twice for non video
    if (type != IAXFormat::Video && d->m_lastOut && ts == d->m_lastOut)
	ts++;
    // Format changed or timestamp wrapped around
    // Send a full frame
    bool fullFrame = (fmt->out() != format) || !d->m_lastOut;
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
    if (fullFrame) {
	if (fmt->out() != format) {
	    Debug(m_engine,DebugNote,
		"Transaction(%u,%u). Outgoing %s format changed %u --> %u [%p]",
		localCallNo(),remoteCallNo(),fmt->typeName(),fmt->out(),format,this);
	    fmt->set(0,0,&format);
	}
#ifdef DEBUG
	else
	    Debug(m_engine,DebugInfo,
		"Transaction(%u,%u). Sending full frame for media '%s': ts=%u last=%u [%p]",
		localCallNo(),remoteCallNo(),fmt->typeName(),ts,d->m_lastOut,this);
#endif
    }
    d->m_lastOut = ts;
    unsigned int sent = 0;
    if (type == IAXFormat::Audio) {
	if (fullFrame) {
	    // Send trunked frame before full frame to keep the media order
	    if (m_trunkFrame)
		m_engine->sendTrunkFrame(m_trunkFrame);
	    postFrame(IAXFrame::Voice,fmt->out(),data.data(),data.length(),ts,true);
	    sent = data.length();
	}
	else if (m_trunkFrame) {
	    m_trunkFrame->add(localCallNo(),data,ts);
	    sent = data.length();
	}
	else {
	    DataBlock buf;
	    IAXFrame::buildMiniFrame(buf,localCallNo(),ts,data.data(),data.length());
	    m_engine->writeSocket(buf.data(),buf.length(),remoteAddr(),0,&sent);
	    // Decrease with mini frame header
	    if (sent > 4)
		sent -= 4;
	    else
		sent = 0;
	}
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
    DDebug(m_engine,sent == data.length() ? DebugAll : DebugNote,
	"Transaction(%u,%u) sent %u/%u media=%s mark=%u ts=%u [%p]",
	localCallNo(),remoteCallNo(),sent,data.length(),
	fmt->typeName(),mark,ts,this);
    return sent;
}

IAXEvent* IAXTransaction::getEvent(u_int64_t time)
{
    IAXEvent* ev = 0;
    GenObject* obj;
    bool delFrame;

    Lock lock(this);
    if (state() == Terminated)
	return 0;
    // Send ack for received frames
    ackInFrames();
    // Do we have a generated event ?
    if (m_currentEvent)
	return 0;
    // Waiting on remote cleanup ?
    if (state() == Terminating && !m_localReqEnd)
	return getEventTerminating(time);
    // Do we have a pending event ?
    if (m_pendingEvent) {
	ev = m_pendingEvent;
	m_pendingEvent = 0;
	return keepEvent(ev);
    }
    // Time to Ping remote peer ?
    if (time > m_timeToNextPing && state() != Terminating) {
	postFrame(IAXFrame::IAX,IAXControl::Ping,0,0,(u_int32_t)timeStamp(),false);
	m_timeToNextPing = time + m_pingInterval;
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
	// Adjust timeout for acknoledged auth frames sent with no auth response
	if (state() == NewRemoteInvite_AuthSent && frame->ack())
	    frame->adjustAuthTimeout(time + m_engine->authTimeout() * 1000);
	// No response. Timeout ?
	if (frame->timeout()) {
	    if (m_state == Terminating)
		// Client already notified: Terminate transaction
		ev = terminate(IAXEvent::Timeout,true);
	    else
		// Client not notified: Notify it and terminate transaction
		ev = terminate(IAXEvent::Timeout,true,frame,false);
	    break;
	}
	// Retransmit ?
	if (frame->timeForRetrans(time)) {
	    if (frame->ack())
		frame->transmitted();   // Frame acknoledged: just update retransmission info
	    else {
		Debug(m_engine,DebugNote,"Transaction(%u,%u) resending Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
		    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo(),frame->timeStamp(),this);
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
	            DDebug(m_engine,DebugAll,"Transaction(%u,%u) removing outgoing frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
			localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),
			frame->iSeqNo(),frame->timeStamp(),this);
	            m_outFrames.remove(frame,true);
		}
		break;
	    }
	    frame->setAck();
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
    ListIterator lin(m_inFrames);
    for (; (obj = lin.get());) {
	IAXFullFrame* frame = static_cast<IAXFullFrame*>(obj);
	// If frame is ACK, ignore it
	if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack)
	    continue;
	DDebug(m_engine,DebugAll,"Transaction(%u,%u) dequeued Frame(%u,%u) iseq=%u oseq=%u stamp=%u [%p]",
	    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->iSeqNo(),frame->oSeqNo(),frame->timeStamp(),this);
	if (m_state == IAXTransaction::Unknown)
	    ev = getEventStartTrans(frame,delFrame);  // New transaction
	else
	    ev = getEventRequest(frame,delFrame);
	if (delFrame)
	    m_inFrames.remove(frame,true);    // frame is no longer needded
	if (ev)
	    return keepEvent(ev);
    }
    // No pending outgoing frames. No valid requests. Clear incoming frames queue.
    //m_inDroppedFrames += m_inFrames.count();
    m_inFrames.clear();
    return 0;
}

bool IAXTransaction::sendAccept(unsigned int* expires)
{
    Lock lock(this);
    if (!((type() == New && (state() == NewRemoteInvite || state() == NewRemoteInvite_RepRecv)) ||
	(type() == RegReq && state() == NewRemoteInvite) ||
	((type() == RegReq || type() == RegRel) && state() == NewRemoteInvite_RepRecv)))
	return false;
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
	changeState(Terminating);
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
    postFrameIes(IAXFrame::IAX,IAXControl::Hangup,ies,0,true);
    changeState(Terminating);
    m_localReqEnd = true;
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Hangup call. Cause: '%s'",localCallNo(),remoteCallNo(),cause);
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
	    break;
	case RegReq:
	case RegRel:
	    frametype = IAXControl::RegRej;
	    break;
	case Poke:
	default:
	    return false;
    }
    IAXIEList* ies = new IAXIEList;
    if (!TelEngine::null(cause))
	ies->appendString(IAXInfoElement::CAUSE,cause);
    if (code)
	ies->appendNumeric(IAXInfoElement::CAUSECODE,code,1);
    postFrameIes(IAXFrame::IAX,frametype,ies,0,true);
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Reject. Cause: '%s'",localCallNo(),remoteCallNo(),cause);
    changeState(Terminating);
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

bool IAXTransaction::enableTrunking(IAXMetaTrunkFrame* trunkFrame)
{
    if (m_trunkFrame)
	return false;
    // Get a reference to the trunk frame
    if (!(trunkFrame && trunkFrame->ref()))
	return false;
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

void IAXTransaction::print(bool printStats, bool printFrames, const char* location)
{
    if (m_engine && !m_engine->debugAt(DebugAll))
	return;
    String stats;
    if (printStats && m_type == New) {
	stats << " audio: ";
	m_dataAudio.print(stats);
	stats << " video: ";
	m_dataVideo.print(stats);
    }
    String buf;
    if (printFrames) {
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
    }
    Debug(m_engine,DebugAll,
	"Transaction(%u,%u) %s remote=%s:%u type=%u state=%u timestamp=" FMT64U "%s [%p]%s%s%s",
	localCallNo(),remoteCallNo(),location,remoteAddr().host().c_str(),remoteAddr().port(),
	type(),state(),(u_int64_t)timeStamp(),stats.safe(),this,
	buf ? "\r\n-----" : "",buf.safe(),buf ? "\r\n-----" : "");
}

// Cleanup
void IAXTransaction::destroyed()
{
#ifndef DEBUG
    print(false,false,"destroyed");
#else
    print(true,true,"destroyed");
#endif
    TelEngine::destruct(m_trunkFrame);
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
    if (!delta)
	return true;
    if (delta > 0) {
	// We missed some frames before this one: Send VNAK
	Debug(m_engine,DebugInfo,"Transaction(%u,%u). Received Frame(%u,%u) out of order! oseq=%u expecting %u. Send VNAK",
	    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),m_iSeqNo);
	sendVNAK();
	m_inOutOfOrderFrames++;
	return false;
    }
    DDebug(m_engine,DebugInfo,"Transaction(%u,%u). Received late Frame(%u,%u) with oseq=%u expecting %u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),m_iSeqNo,this);
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
    XDebug(m_engine,DebugInfo,"Transaction(%u,%u). State change: %u --> %u [%p]",localCallNo(),remoteCallNo(),m_state,newState,this);
    m_state = newState;
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
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Terminated. Event: %u, Frame(%u,%u)",
	localCallNo(),remoteCallNo(),evType,ev->frameType(),ev->subclass());
    changeState(Terminated);
    deref();
    return ev;
}

IAXEvent* IAXTransaction::waitForTerminate(u_int8_t evType, bool local, IAXFullFrame* frame)
{
    IAXEvent* ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,frame);
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Terminating. Event: %u, Frame(%u,%u)",
	localCallNo(),remoteCallNo(),evType,ev->frameType(),ev->subclass());
    changeState(Terminating);
    m_timeout = (m_engine->transactionTimeout() + Time::secNow()) * 1000;
    return ev;
}

void IAXTransaction::postFrame(IAXFrame::Type type, u_int32_t subclass, void* data,
    u_int16_t len, u_int32_t tStamp, bool ackOnly, bool mark)
{
    Lock lock(this);
    if (state() == Terminated)
	return;
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
	sendInval();
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
	if (m_state == Terminating)
	    return terminate(IAXEvent::Terminated,true);
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
    Debug(m_engine,DebugAll,"Transaction(%u,%u). AuthReq received",localCallNo(),remoteCallNo());
    // Valid authmethod & challenge ?
    u_int32_t authmethod;
    bool bAuthMethod = event->getList().getNumeric(IAXInfoElement::AUTHMETHODS,authmethod) && (authmethod & m_authmethod);
    bool bChallenge = event->getList().getString(IAXInfoElement::CHALLENGE,m_challenge);
    if (bAuthMethod && bChallenge)
	return event;
    delete event;
    return internalReject(s_iax_modNoAuthMethod);
}

IAXEvent* IAXTransaction::processAccept(IAXEvent* event)
{
    if (event->type() != IAXEvent::Accept)
	return event;
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Accept received",localCallNo(),remoteCallNo());
    u_int32_t fmt = 0;
    event->getList().getNumeric(IAXInfoElement::FORMAT,fmt);
    m_format.set(&fmt,0,0);
    m_formatVideo.set(&fmt,0,0);
    m_engine->acceptFormatAndCapability(this,0,IAXFormat::Audio);
    m_engine->acceptFormatAndCapability(this,0,IAXFormat::Video);
    if (m_format.format() || m_formatVideo.format())
	return event;
    delete event;
    return internalReject(s_iax_modNoMediaFormat);
}

IAXEvent* IAXTransaction::processAuthRep(IAXEvent* event)
{
    if (event->type() != IAXEvent::AuthRep)
	return event;
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Auth Reply received",localCallNo(),remoteCallNo());
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
		// Check username
		if (!m_username)
		    return internalReject(s_iax_modNoUsername);
	    }
	    return ev;
	case RegReq:
	case RegRel:
	    if (!(frame->type() == IAXFrame::IAX &&
		(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
		break;
	    ev = createEvent(IAXEvent::New,false,frame,NewRemoteInvite);
	    init(ev->getList());
	    // Check username
	    if (!m_username)
		return internalReject(s_iax_modNoUsername);
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
    IAXEvent* ev;
    delFrame = true;
    // INVAL ?
    if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Inval) {
	Debug(m_engine,DebugAll,"IAXTransaction(%u,%u). Received INVAL. Terminate [%p]",
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
    }
    if (frame) {
	m_inFrames.remove(frame,true);
	return true;
    }
    return false;
}

bool IAXTransaction::findInFrameAck(const IAXFullFrame* frameOut)
{
    IAXFullFrame* frame = 0;
    for (ObjList* l = m_inFrames.skipNull(); l; l = l->next()) {
	frame = static_cast<IAXFullFrame*>(l->get());
	if (frame && frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack &&
	    frame->timeStamp() == frameOut->timeStamp() && frame->oSeqNo() == frameOut->iSeqNo())
	    break;
	frame = 0;
    }
    if (frame) {
	m_inFrames.remove(frame,true);
	return true;
    }
    return false;
}

void IAXTransaction::ackInFrames()
{
    IAXFullFrame* ack = 0;
    for (ObjList* l = m_inFrames.skipNull(); l; l = l->next()) {
	IAXFullFrame* frame = static_cast<IAXFullFrame*>(l->get());
	if (frame && frame->type() == IAXFrame::IAX && frame->subclass() != IAXControl::Ack)
	    ack = frame;
    }
    if (ack) {
	int32_t interval = (int32_t)ack->oSeqNo() - m_lastAck;
	if (interval > 32767 || (interval > -32767 && interval <= 0))
	    // Frame is older then the last ack'd
	    return;
	m_lastAck = ack->oSeqNo();
	sendAck(ack);
    }
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
    IAXFullFrame* f = new IAXFullFrame(IAXFrame::IAX,IAXControl::Ack,localCallNo(),
	remoteCallNo(),frame->iSeqNo(),m_iSeqNo,frame->timeStamp());
    DDebug(m_engine,DebugInfo,"Transaction(%u,%u). Send ACK for Frame(%u,%u) oseq: %u iseq: %u",
	    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo());
    m_engine->writeSocket(f->data().data(),f->data().length(),remoteAddr(),f);
    f->deref();
}

void IAXTransaction::sendInval()
{
    IAXFullFrame* f = new IAXFullFrame(IAXFrame::IAX,IAXControl::Inval,localCallNo(),
	remoteCallNo(),m_oSeqNo++,m_iSeqNo,(u_int32_t)timeStamp());
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
    delFrame = false;
    if (frame->type() != IAXFrame::IAX)
	return 0;
    if (frame->subclass() == IAXControl::LagRq) {
	postFrame(IAXFrame::IAX,IAXControl::LagRp,0,0,frame->timeStamp(),true);
	delFrame = true;
    }
    return 0;
}

IAXEvent* IAXTransaction::processMidCallControl(IAXFullFrame* frame, bool& delFrame)
{
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
    return 0;
}

IAXEvent* IAXTransaction::processMidCallIAXControl(IAXFullFrame* frame, bool& delFrame)
{
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
	default: ;
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

IAXEvent* IAXTransaction::getEventTerminating(u_int64_t time)
{
    if (time > m_timeout) {
	Debug(m_engine,DebugAll,"Transaction(%u,%u) - Cleanup on remote request. Timestamp: " FMT64U,
	    localCallNo(),remoteCallNo(),timeStamp());
	return terminate(IAXEvent::Timeout,false);
    }
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
	m_pendingEvent = internalReject(s_iax_modNoMediaFormat);
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
	    m_pendingEvent = internalReject(s_iax_modNoMediaFormat);
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
    DDebug(m_engine,DebugNote,"Transaction(%u,%u). Retransmitted %d frames on VNAK(%u)",
	localCallNo(),remoteCallNo(),c,seqNo);
    return 0;
}

IAXEvent* IAXTransaction::internalAccept()
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Internal accept",localCallNo(),remoteCallNo());
    sendAccept();
    return new IAXEvent(IAXEvent::Accept,true,true,this,IAXFrame::IAX,IAXControl::Accept);
}

IAXEvent* IAXTransaction::internalReject(String& reason)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u). Internal reject: '%s'",localCallNo(),remoteCallNo(),reason.c_str());
    sendReject(reason);
    IAXEvent* event = new IAXEvent(IAXEvent::Reject,true,true,this,IAXFrame::IAX,IAXControl::Reject);
    event->getList().appendString(IAXInfoElement::CAUSE,reason);
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
    if (tStamp)
	return;
    tStamp = (u_int32_t)timeStamp();
    if (m_lastFullFrameOut) {
	// adjust timestamp to be different from the last sent
	int32_t delta = tStamp - m_lastFullFrameOut;
	if (delta <= 0)
	    tStamp = m_lastFullFrameOut + 1;
    }
    m_lastFullFrameOut = tStamp;
}

void IAXTransaction::postFrame(IAXFrameOut* frame)
{
    if (!frame)
	return;
    DDebug(m_engine,DebugAll,
	"Transaction(%u,%u) posting Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),
	m_oSeqNo,m_iSeqNo,frame->timeStamp(),this);
    incrementSeqNo(frame,false);
    m_outFrames.append(frame);
    sendFrame(frame);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
