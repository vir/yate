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

unsigned char IAXTransaction::m_maxInFrames = 100;

IAXTransaction::IAXTransaction(IAXEngine* engine, IAXFullFrame* frame,
	u_int16_t lcallno, const SocketAddr& addr, void* data)
    : Mutex(true),
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
    m_lastMiniFrameOut(0xFFFF),
    m_lastMiniFrameIn(0),
    m_mutexInMedia(true),
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
    m_format(0),
    m_formatIn(0),
    m_formatOut(0),
    m_capability(0)
{
    XDebug(m_engine,DebugAll,"IAXTransaction::IAXTransaction(%u,%u) incoming [%p]",
	localCallNo(),remoteCallNo(),this);
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
	case IAXControl::FwDownl:
	default:
	    XDebug(m_engine,DebugAll,"IAXTransaction::IAXTransaction(%u,%u) incoming [%p]. Unsupported type: %u",
		localCallNo(),remoteCallNo(),this,frame->subclass());
	    return;
    }
    // Append frame to incoming list
    Lock lock(this);
    m_inFrames.append(frame);
    incrementSeqNo(frame,true);
    sendAck(frame->fullFrame());
}

IAXTransaction::IAXTransaction(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr,
	IAXIEList& ieList, void* data)
    : Mutex(true),
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
    m_lastMiniFrameOut(0xFFFF),
    m_lastMiniFrameIn(0),
    m_mutexInMedia(true),
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
    m_format(0),
    m_formatIn(0),
    m_formatOut(0),
    m_capability(0)
{
    XDebug(m_engine,DebugAll,"IAXTransaction::IAXTransaction(%u,%u) outgoing [%p]",
	localCallNo(),remoteCallNo(),this);
    // Init data members
    m_retransCount = engine->retransCount();
    m_retransInterval = engine->retransInterval();
    m_timeToNextPing = m_timeStamp + m_pingInterval;
    init(ieList);
    ieList.clear();
    IAXControl::Type frametype;
    // Create IE list to send
    switch (type) {
	case New:
	    ieList.insertVersion();
	    ieList.appendString(IAXInfoElement::USERNAME,m_username);
	    ieList.appendString(IAXInfoElement::CALLING_NUMBER,m_callingNo);
	    ieList.appendString(IAXInfoElement::CALLING_NAME,m_callingName);
	    ieList.appendString(IAXInfoElement::CALLED_NUMBER,m_calledNo);
	    ieList.appendString(IAXInfoElement::CALLED_CONTEXT,m_calledContext);
	    ieList.appendNumeric(IAXInfoElement::FORMAT,m_format,4);
	    ieList.appendNumeric(IAXInfoElement::CAPABILITY,m_capability,4);
	    frametype = IAXControl::New;
	    break;
	case RegReq:
	case RegRel:
	    ieList.appendString(IAXInfoElement::USERNAME,m_username);
	    ieList.appendNumeric(IAXInfoElement::REFRESH,m_expire,2);
	    frametype = (type == RegReq ? IAXControl::RegReq : IAXControl::RegRel);
	    break;
	case Poke:
	    frametype = IAXControl::Poke;
	    break;
	default:
	    XDebug(m_engine,DebugAll,"IAXTransaction::IAXTransaction(%u,%u) outgoing [%p]. Unsupported type: %u",
		localCallNo(),remoteCallNo(),this,m_type);
	    m_type = Incorrect;
	    return;
    }
    DataBlock d;
    ieList.toBuffer(d);
    if (d.length() > (unsigned int)m_engine->maxFullFrameDataLen()) {
	XDebug(m_engine,DebugAll,"IAXTransaction::IAXTransaction(%u,%u)[%p]. Buffer too long (%u > %u)",
		localCallNo(),remoteCallNo(),this,d.length(),(unsigned int)m_engine->maxFullFrameDataLen());
	d.clear();
    }
    postFrame(IAXFrame::IAX,frametype,(void*)(d.data()),d.length());
    changeState(NewLocalInvite);
}

IAXTransaction::~IAXTransaction()
{
#ifdef XDEBUG
    print();
#endif
    if (state() != Terminating && state() != Terminated)
	sendReject("Server shutdown");
}

IAXTransaction* IAXTransaction::factoryIn(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, const SocketAddr& addr,
	void* data)
{
    IAXTransaction* tr = new IAXTransaction(engine,frame,lcallno,addr,data);
    if (tr->type() != Incorrect)
	return tr;
    tr->deref();
    return 0;
}

IAXTransaction* IAXTransaction::factoryOut(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr,
	IAXIEList& ieList, void* data)
{
    IAXTransaction* tr = new IAXTransaction(engine,type,lcallno,addr,ieList,data);
    if (tr->type() != Incorrect)
	return tr;
    tr->deref();
    return 0;
}

IAXTransaction* IAXTransaction::processFrame(IAXFrame* frame)
{
    if (!frame)
	return 0;

//    if (frame->fullFrame())
//    Output("Transaction(%u,%u) received Frame(%u,%u) (%u,%u) stamp=%u",
//	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->fullFrame()->destCallNo(),frame->sourceCallNo(),frame->timeStamp());

    if (state() == Terminated) {
//    Output("     State is Terminated. DROP");
	sendInval();
	return 0;
    }
    if (state() == Terminating)
	// Local terminate: Accept only Ack. Remote terminate: Accept none.
	if (m_localReqEnd) {
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack))
{
//   if (frame->fullFrame())
//    Output("     State is Terminating and frame is not ACK. DROP");
		return 0;
}

	}
	else
{
//    if (frame->fullFrame())  Output("     State is Terminating on remote request. DROP");
	    return 0;
}
    // Mini frame
    if (!frame->fullFrame())
	return processMedia(frame->data(),frame->timeStamp());
    Lock lock(this);
    m_inTotalFramesCount++;
    // Frame is VNAK ?
    if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::VNAK)
	return retransmitOnVNAK(frame->fullFrame()->iSeqNo());
    // Do we have space?
    if (m_inFrames.count() == m_maxInFrames) {
//    Output("     Buffer overrun. DROP");
	Debug(DebugWarn,"IAXTransaction(%u,%u) - processFrame. Buffer overrun!",localCallNo(),remoteCallNo());
	m_inDroppedFrames++;
	return 0;
    }
    bool fAck = frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack;
    if (!fAck && !isFrameAcceptable(frame->fullFrame()))

{
//    Output("     Not acceptable. DROP");
	return 0;
}
    incrementSeqNo(frame->fullFrame(),true);
    if (!fAck)
	sendAck(frame->fullFrame());
    // Voice full frame: process voice data & format
    if (frame->type() == IAXFrame::Voice && type() == New) {
	if (frame->fullFrame()->subclass() && frame->fullFrame()->subclass() != m_formatIn) {
	    // Format changed.
	    if (m_engine->voiceFormatChanged(this,frame->fullFrame()->subclass()))
		m_formatIn = frame->fullFrame()->subclass();
	    else {
//    Output("     Media format change rejected. DROP");
		DDebug(m_engine,DebugAll,"IAXTransaction(%u,%u) - processFrame. Media format (%u) change rejected!",
			localCallNo(),remoteCallNo(),m_format);
		m_pendingEvent = internalReject(s_iax_modNoMediaFormat);
		return 0;
	    }
	}
	processMedia(frame->data(),frame->timeStamp(),true);
	frame->data().clear();
    }
    // Append frame to incoming frame list
    m_inFrames.append(frame);
//    Output("Transaction(%u,%u) enqueued Frame(%u,%u) stamp=%u",localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->timeStamp());
    DDebug(m_engine,DebugAll,"Transaction(%u,%u) enqueued Frame(%u,%u) stamp=%u [%p]",
	localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->timeStamp(),this);
    return this;
}

IAXTransaction* IAXTransaction::processMedia(DataBlock& data, u_int32_t tStamp, bool voice)
{
    Lock lock(&m_mutexInMedia);
    if (!(voice || (tStamp & 0xffff0000))) {
	// Miniframe timestamp
	int16_t delta = tStamp - m_lastMiniFrameIn;
	if (delta < 0)
	    return 0;
	// add upper bits from last frame
	tStamp |= m_lastMiniFrameIn & 0xffff0000;
	// check if timestamp wrapped around by a miniframe, adjust if so
	if ((tStamp & 0xffff) < (m_lastMiniFrameIn & 0xffff)) {
	    DDebug(m_engine,DebugAll,"Timestamp wraparound, ts=%u last=%u [%p]",tStamp & 0xffff,m_lastMiniFrameIn,this);
	    tStamp += 0x10000;
	}
    }
    int32_t interval = (int32_t)tStamp - m_lastMiniFrameIn;
    if (interval)
	if (interval < 32767)
	    m_lastMiniFrameIn = tStamp; // New frame is newer then the last one
	else
	    return 0;                   // Out of order
    else {
	// Reset timestamp
	m_lastMiniFrameIn = 0;
	return 0;
    }
    m_engine->processMedia(this,data,tStamp);
    return 0;
}

IAXTransaction* IAXTransaction::sendMedia(const DataBlock& data, u_int8_t format)
{
    if (!data.length())
	return 0;
    u_int32_t ts = timeStamp();
    // Format changed or timestamp wrapped around? Send Voice full frame
    if ((u_int16_t)ts < m_lastMiniFrameOut || m_formatOut != format ) {
	if (m_formatOut != format) {
	    DDebug(m_engine,DebugNote,"Outgoing format changed (New: %u, Old: %u). Send VOICE. [%p]",
		format,m_formatOut,this);
	    m_formatOut = format;
	}
	else
	    DDebug(m_engine,DebugNote,"Transaction(%u,%u) Time to send VOICE: ts=%u last=%u [%p]",
		localCallNo(),remoteCallNo(),ts,m_lastMiniFrameOut,this);
	m_lastMiniFrameOut = (u_int16_t)ts;
	postFrame(IAXFrame::Voice,m_formatOut,data.data(),data.length(),ts,true);
	return this;
    }
    // Send mini frame
    m_lastMiniFrameOut = (u_int16_t)ts;
    unsigned char b[4] = {localCallNo() >> 8,localCallNo(),m_lastMiniFrameOut >> 8,m_lastMiniFrameOut};
    DataBlock buf(b,4);
    buf += data;
    m_engine->writeSocket(buf.data(),buf.length(),remoteAddr());
    return this;
}

IAXEvent* IAXTransaction::getEvent(u_int64_t time)
{
    IAXEvent* ev;
    GenObject* obj;
    bool delFrame;

    Lock lock(this);
    if (state() == Terminated)
	return 0;
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
	DDebug(m_engine,DebugAll,"Time to PING. %u",(u_int32_t)timeStamp());
	postFrame(IAXFrame::IAX,IAXControl::Ping,0,0,timeStamp(),false);
	m_timeToNextPing = time + m_pingInterval;
    }
    // Process outgoing frames
    ListIterator lout(m_outFrames);
    for (; (obj = lout.get());) {
	IAXFrameOut* frame = static_cast<IAXFrameOut*>(obj);
	ev = getEventResponse(frame,delFrame);
	if((frame->ack() && frame->ackOnly()) || delFrame) {

//if (frame->type() == IAXFrame::Voice)
//   Output("***********************Transaction(%u,%u) VOICE frame removed. ACK: %s",localCallNo(),remoteCallNo(),frame->ack()?"YES":"NO");


	    m_outFrames.remove(frame,true);
	    if (ev)
		return keepEvent(ev);
	    continue;
	}
	if (ev)
	    return keepEvent(ev);
	// Adjust timeout for acknoledged auth frames sent with no auth response
	if (state() == NewRemoteInvite_AuthSent && frame->ack())
	    frame->adjustAuthTimeout(time + m_engine->authTimeout() * 1000);
	// No response. Timeout ?
	if (frame->timeout()) {
	    if (m_state == Terminating)
		// Client already notified: Terminate transaction
		return keepEvent(terminate(IAXEvent::Timeout,true));
	    else
		// Client not notified: Notify it and terminate transaction
		return keepEvent(terminate(IAXEvent::Timeout,true,frame,false));
	}
	// Retransmit ?
	if (frame->timeForRetrans(time)) {
	    if (frame->ack())
		frame->transmitted();   // Frame acknoledged: just update retransmission info
	    else {
//		Output("Transaction(%u,%u) resending Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
//		    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo(),frame->timeStamp(),this);
		Debug(m_engine,DebugNote,"Transaction(%u,%u) resending Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
		    localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo(),frame->timeStamp(),this);
		sendFrame(frame);       // Retransmission
	    }
	}
    }
    // Process incoming frames
    ListIterator lin(m_inFrames);
    for (; (obj = lin.get());) {
	IAXFullFrame* frame = static_cast<IAXFullFrame*>(obj);
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

bool IAXTransaction::sendAccept()
{
    Lock lock(this);
    if (!((type() == New && (state() == NewRemoteInvite || state() == NewRemoteInvite_RepRecv)) ||
	(type() == RegReq && state() == NewRemoteInvite) ||
	((type() == RegReq || type() == RegRel) && state() == NewRemoteInvite_RepRecv)))
	return false;
    if (type() == New) {
	unsigned char d[6] = {IAXInfoElement::FORMAT,4,m_format >> 24,m_format >> 16,m_format >> 8,m_format};
	postFrame(IAXFrame::IAX,IAXControl::Accept,d,sizeof(d),0,true);
	changeState(Connected);
    }
    else {
	IAXIEList ieList;
	ieList.appendString(IAXInfoElement::USERNAME,m_username);
	ieList.appendString(IAXInfoElement::CALLING_NUMBER,m_callingNo);
	ieList.appendString(IAXInfoElement::CALLING_NAME,m_callingName);
	ieList.appendNumeric(IAXInfoElement::REFRESH,m_expire,2);
	ieList.appendIE(IAXInfoElementBinary::packIP(remoteAddr()));
	DataBlock data;
	ieList.toBuffer(data);
	postFrame(IAXFrame::IAX,IAXControl::RegAck,data.data(),data.length(),0,true);
	changeState(Terminating);
	m_localReqEnd = true;
    }
    return true;
}

bool IAXTransaction::sendHangup(const char* cause, u_int8_t code)
{
    String s(cause);
    unsigned char d[3];
    DataBlock data,aux;

    Lock lock(this);
    if (type() != New || state() == Terminated || state() == Terminating)
	return false;
    if (cause) {
	d[0] = IAXInfoElement::CAUSE;
	d[1] = s.length();
	data.assign(d,2);
	data.append(s);
    }
    if (code) {
	d[0] = IAXInfoElement::CAUSECODE;
	d[1] = 1;
	d[2] = code;
	aux.assign(d,3);
	data += aux;
    }
    postFrame(IAXFrame::IAX,IAXControl::Hangup,data.data(),data.length(),0,true);
    changeState(Terminating);
    m_localReqEnd = true;
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Hangup call. Cause: '%s'",localCallNo(),remoteCallNo(),cause);
    return true;
}

bool IAXTransaction::sendReject(const char* cause, u_int8_t code)
{
    String s(cause);
    unsigned char d[3];
    DataBlock data,aux;

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
	case FwDownl:
	case Poke:
	default:
	    return false;
    }
    if (cause) {
	d[0] = IAXInfoElement::CAUSE;
	d[1] = s.length();
	data.assign(d,2);
	data.append(s);
    }
    if (code) {
	d[0] = IAXInfoElement::CAUSECODE;
	d[1] = 1;
	d[2] = code;
	aux.assign(d,3);
	data += aux;
    }
    postFrame(IAXFrame::IAX,frametype,data.data(),data.length(),0,true);
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Reject. Cause: '%s'",localCallNo(),remoteCallNo(),cause);
    changeState(Terminating);
    m_localReqEnd = true;
    return true;
}

bool IAXTransaction::sendAuth(const String& pwd)
{
    Lock lock(this);
    if (!((type() == New || type() == RegReq || type() == RegRel) && state() == NewRemoteInvite))
	return false;
    m_password = pwd;
    switch (m_authmethod) {
	case IAXAuthMethod::MD5:
	    m_challenge = (int)random();
	    break;
	case IAXAuthMethod::RSA:
	case IAXAuthMethod::Text:
	default:
	    return false;
    }
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,m_username);
    ieList.appendNumeric(IAXInfoElement::AUTHMETHODS,m_authmethod,2);
    ieList.appendString(IAXInfoElement::CHALLENGE,m_challenge);
    DataBlock data;
    ieList.toBuffer(data);
    switch (type()) {
	case New:
	    postFrame(IAXFrame::IAX,IAXControl::AuthReq,data.data(),data.length(),0,false);
	    break;
	case RegReq:
	case RegRel:
	    postFrame(IAXFrame::IAX,IAXControl::RegAuth,data.data(),data.length(),0,false);
	    break;
	default: ;
    }
    changeState(NewRemoteInvite_AuthSent);
    return true;
}

bool IAXTransaction::sendAuthReply()
{
    Lock lock(this);
    if (state() != NewLocalInvite_AuthRecv)
	return false;
    IAXIEList ieList;
    String authdata;
    IAXControl::Type subclass;
    switch (type()) {
	case New:
	    subclass = IAXControl::AuthRep;
	    break;
	case RegReq:
	    subclass = IAXControl::RegReq;
	    ieList.appendString(IAXInfoElement::USERNAME,m_username);
	    break;
	case RegRel:
	    subclass = IAXControl::RegRel;
	    ieList.appendString(IAXInfoElement::USERNAME,m_username);
	    break;
	default:
	    return false;
    }
    if (m_authmethod != IAXAuthMethod::MD5)
	return false;
    m_engine->getMD5FromChallenge(authdata,m_challenge,m_password);
    ieList.appendString(IAXInfoElement::MD5_RESULT,authdata);
    DataBlock data;
    ieList.toBuffer(data);
    postFrame(IAXFrame::IAX,subclass,data.data(),data.length(),0,false);
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
    m_userdata = 0;
    sendReject();
    return true;
}

void IAXTransaction::print()
{
    Debug(m_engine,DebugAll,"IAXTransaction - START PRINT [%p]",this);
    Output("Local call no:    %u\nRemote call no:   %u\nType:             %u\nState:            %u\nTimestamp:        %llu",
	localCallNo(),remoteCallNo(),type(),state(),(long unsigned long)timeStamp());
    Output("Queues:\nOutgoing:         %u",m_outFrames.count());
    ObjList* l;
    int i;
    for(i = 0, l = m_outFrames.skipNull(); l; l = l->next(), i++) {
	IAXFrameOut* frame = static_cast<IAXFrameOut*>(l->get());
	if (frame)
	    Output("     %5u Type: %3u Subclass: %3u Out: %5u In: %5u Timestamp: %5u Ack: %u AckOnly: %u",
		i+1,frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo(), frame->timeStamp(),frame->ack(),frame->ackOnly());
    }
    Output("Incoming:         %u",m_inFrames.count());
    for(i = 0, l = m_inFrames.skipNull(); l; l = l->next(), i++) {
	IAXFullFrame* frame = static_cast<IAXFrameOut*>(l->get());
	if (frame)
	    Output("     %5u Type: %3u Subclass: %3u Out: %5u In: %5u Timestamp: %5u",
		i+1,frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo(), frame->timeStamp());
    }
    Debug(m_engine,DebugAll,"IAXTransaction - END PRINT [%p]",this);
}

void IAXTransaction::init(IAXIEList& ieList)
{
    switch (type()) {
	case New:
	    ieList.getString(IAXInfoElement::USERNAME,m_username);
	    ieList.getString(IAXInfoElement::PASSWORD,m_password);
	    ieList.getString(IAXInfoElement::CALLING_NUMBER,m_callingNo);
	    ieList.getString(IAXInfoElement::CALLING_NAME,m_callingName);
	    ieList.getString(IAXInfoElement::CALLED_NUMBER,m_calledNo);
	    ieList.getString(IAXInfoElement::CALLED_CONTEXT,m_calledContext);
	    ieList.getNumeric(IAXInfoElement::FORMAT,m_format);
	    ieList.getNumeric(IAXInfoElement::CAPABILITY,m_capability);
	    m_formatIn = m_formatOut = m_format;
	    break;
	case RegReq:
	case RegRel:
	    ieList.getString(IAXInfoElement::USERNAME,m_username);
	    ieList.getString(IAXInfoElement::PASSWORD,m_password);
	    ieList.getNumeric(IAXInfoElement::REFRESH,m_expire);
	    break;
	case Poke:
	case FwDownl:
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
    XDebug(m_engine,DebugAll,"Incremented %s=%u for Frame(%u,%u) iseq=%u oseq=%u [%p]",
	inbound ? "iseq" : "oseq", inbound ? m_iSeqNo : m_oSeqNo,
	frame->type(),frame->subclass(),
	frame->iSeqNo(),frame->oSeqNo(),this);
    return true;
}

bool IAXTransaction::isFrameAcceptable(const IAXFullFrame* frame)
{
    int64_t delta = frame->oSeqNo() - m_iSeqNo;
    if (!delta)
	return true;
    if (delta > 0) {
	// We missed some frames before this one: Send VNAK
	Debug(m_engine,DebugInfo,"IAXTransaction(%u,%u) - received frame out of order! oseq=%u expecting %u. Send VNAK",
	    localCallNo(),remoteCallNo(),frame->oSeqNo(),m_iSeqNo);
	sendVNAK();
	m_inOutOfOrderFrames++;
    }
    DDebug(m_engine,DebugInfo,"IAXTransaction(%u,%u) - received late frame with oseq=%u expecting %u [%p]",
	localCallNo(),remoteCallNo(),frame->oSeqNo(),m_iSeqNo,this);
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
    //Output("TRANSACTION: State change:  %u --> %u",m_state,newState);
    m_state = newState;
    return true;
}

IAXEvent* IAXTransaction::terminate(u_int8_t evType, bool local, const IAXFullFrame* frame, bool createIEList)
{
    IAXEvent* ev;
    if (createIEList)
	ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,frame);
    else
	if (frame)
	    ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,frame->type(),frame->subclass());
	else
	    ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,0,0);
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Terminated. Event: %u, Frame(%u,%u)",
	localCallNo(),remoteCallNo(),evType,ev->frameType(),ev->subclass());
    changeState(Terminated);
    deref();
    return ev;
}

IAXEvent* IAXTransaction::waitForTerminate(u_int8_t evType, bool local, const IAXFullFrame* frame)
{
    IAXEvent* ev = new IAXEvent((IAXEvent::Type)evType,local,true,this,frame);
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Terminating. Event: %u, Frame(%u,%u)",
	localCallNo(),remoteCallNo(),evType,ev->frameType(),ev->subclass());
    changeState(Terminating);
    m_timeout = (m_engine->transactionTimeout() + Time::secNow()) * 1000;
    return ev;
}

void IAXTransaction::postFrame(IAXFrame::Type type, u_int32_t subclass, void* data, u_int16_t len, u_int32_t tStamp, bool ackOnly)
{
    Lock lock(this);
    if (state() == Terminated)
	return;
    if (!tStamp) {
	tStamp = timeStamp();
	if (m_lastFullFrameOut) {
	    // adjust timestamp to be different from the last sent
	    int32_t delta = tStamp - m_lastFullFrameOut;
	    if (delta <= 0)
		tStamp = m_lastFullFrameOut+1;
	}
	m_lastFullFrameOut = tStamp;
    }
    IAXFrameOut* frame = new IAXFrameOut(type,subclass,m_lCallNo,m_rCallNo,m_oSeqNo,m_iSeqNo,tStamp,
			 (unsigned char*)data,len,m_retransCount,m_retransInterval,ackOnly);
    DDebug(m_engine,DebugAll,"Transaction posting Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
	type,subclass,m_oSeqNo,m_iSeqNo,tStamp,this);
    incrementSeqNo(frame,false);
    m_outFrames.append(frame);
    sendFrame(frame);
}

bool IAXTransaction::sendFrame(IAXFrameOut* frame, bool vnak)
{
    if (!frame)
	return false;
    bool b = m_engine->writeSocket(frame->data().data(),frame->data().length(),remoteAddr());
    // Don't modify timeout if transmitted as a response to a VNAK
    if (!vnak)
	frame->transmitted();
    return b;
}

IAXEvent* IAXTransaction::createEvent(u_int8_t evType, bool local, const IAXFullFrame* frame, State newState)
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
    delFrame = true;
    if (findInFrameAck(frame)) {

//Output("*************  Transaction(%u,%u) Found ACK for frame(%u,%u) stamp=%u",localCallNo(),remoteCallNo(),frame->type(),frame->subclass(),frame->timeStamp());

	frame->setAck();
	// Terminating frame sent
	if (m_state == Terminating)
	    return terminate(IAXEvent::Terminated,true);
	// Frame only need ACK
	if (frame->ackOnly())
{

//Output("      ******* Frame only need ACK");


	    return 0;

}
    }
    // Frame only need ACK. Didn't found it. Return
    if (frame->ackOnly()) {
	delFrame = false;
	return 0;
    }
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
    return 0;
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
		return ev;
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
		return ev;
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Reject,IAXEvent::Reject,false,Terminating)))
		return ev;
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::Hangup,IAXEvent::Hangup,false,Terminating)))
		return ev;
	    break;
	case NewRemoteInvite_AuthSent:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::AuthReq))
		break;
	    // Frame is AUTHREQ: AUTHREP, REJECT, HANGUP ?
	    if (0 != (ev = createResponse(frame,IAXFrame::IAX,IAXControl::New,IAXEvent::AuthRep,false,NewRemoteInvite_RepRecv)))
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
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - AuthReq received",localCallNo(),remoteCallNo());
    if (event->type() == IAXEvent::Invalid)
	return event;
    // Valid authmethod & challenge ?
    u_int32_t authmethod;
    bool bAuthMethod = event->getList().getNumeric(IAXInfoElement::AUTHMETHODS,authmethod) && authmethod == (u_int32_t)m_authmethod;
    bool bChallenge = event->getList().getString(IAXInfoElement::CHALLENGE,m_challenge);
    IAXEvent* retEv;
    if (bAuthMethod && bChallenge) {
	Debug(m_engine,DebugAll,"Transaction(%u,%u) - Internal authentication reply",localCallNo(),remoteCallNo());
	sendAuthReply();
	retEv = event;
    }
    else {
	retEv = internalReject(s_iax_modNoAuthMethod);
	delete event;
    }
    return retEv;
}

IAXEvent* IAXTransaction::processAuthRep(IAXEvent* event)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Auth Reply received",localCallNo(),remoteCallNo());
    if (event->type() == IAXEvent::Invalid)
	return event;
    event->getList().getString(IAXInfoElement::MD5_RESULT,m_authdata);
    IAXEvent* retEv;
    if (type() == RegReq || type() == RegRel) {
	if (!IAXEngine::isMD5ChallengeCorrect(m_authdata,m_challenge,m_password))
	    retEv = internalReject(s_iax_modInvalidAuth);
	else
	    retEv = internalAccept();
        delete event;
    }
    else
	retEv = event;
    return retEv;
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
		if (!ev->getList().validVersion()) {
		    delete ev;
		    sendReject("Unsupported or missing protocol version");
		    return 0;
		}
		init(ev->getList());
	    }
	    return ev;
	case Poke:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Poke))
		break;
	    // Send PONG
	    postFrame(IAXFrame::IAX,IAXControl::Pong,0,0,frame->timeStamp(),true);
	    return createEvent(IAXEvent::Terminated,false,0,Terminating);
	case RegReq:
	case RegRel:
	    if (!(frame->type() == IAXFrame::IAX &&
		(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
		break;
	    ev = createEvent(IAXEvent::New,false,frame,NewRemoteInvite);
	    init(ev->getList());
	    return ev;
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
	Debug(m_engine,DebugAll,"IAXTransaction(%u,%u) - Received INVAL. Terminate [%p]",
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
	case Poke:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Poke))
		break;
	    // Send PONG
	    postFrame(IAXFrame::IAX,IAXControl::Pong,0,0,frame->timeStamp());
	    changeState(Terminating);
	    return 0;
	case FwDownl:
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
    for (ObjList* l = m_inFrames.skipNull(); l; l = l->next()) {
	frame = static_cast<IAXFullFrame*>(l->get());
	if (frame && frame->type() == type && frame->subclass() == subclass && frame->timeStamp() == frameOut->timeStamp())
	    break;
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

bool IAXTransaction::sendConnected(IAXFullFrame::ControlType subclass, IAXFrame::Type frametype)
{
    if (state() != Connected)
	return false;
    postFrame(frametype,subclass,0,0,0,true);
    return true;
}

void IAXTransaction::sendAck(const IAXFullFrame* frame)
{
    if (!frame)
	return;
    unsigned char buf[12] = {0x80 | localCallNo() >> 8,localCallNo(),remoteCallNo() >> 8,remoteCallNo(),
			     frame->timeStamp() >> 24,frame->timeStamp() >> 16,frame->timeStamp() >> 8,frame->timeStamp(),
			     frame->iSeqNo(),m_iSeqNo,IAXFrame::IAX,IAXControl::Ack};
    m_engine->writeSocket(buf,12,remoteAddr());
}

void IAXTransaction::sendInval()
{
    u_int32_t ts = timeStamp();
    unsigned char buf[12] = {0x80 | localCallNo() >> 8,localCallNo(),remoteCallNo() >> 8,remoteCallNo(),
			     ts >> 24,ts >> 16,ts >> 8,ts,
			     m_oSeqNo++,m_iSeqNo,IAXFrame::IAX,IAXControl::Inval};
    m_engine->writeSocket(buf,12,remoteAddr());
}

void IAXTransaction::sendVNAK()
{
    u_int32_t ts = timeStamp();
    unsigned char buf[12] = {0x80 | localCallNo() >> 8,localCallNo(),remoteCallNo() >> 8,remoteCallNo(),
			     ts >> 24,ts >> 16,ts >> 8,ts,
			     m_oSeqNo,m_iSeqNo,IAXFrame::IAX,IAXControl::VNAK};
    m_engine->writeSocket(buf,sizeof(buf),remoteAddr());
}

IAXEvent* IAXTransaction::processInternalOutgoingRequest(IAXFrameOut* frame, bool& delFrame)
{
    if (frame->type() != IAXFrame::IAX)
	return 0;
    delFrame = true;
    switch (frame->subclass()) {
	case IAXControl::Ping:
	    if (findInFrameTimestamp(frame,IAXFrame::IAX,IAXControl::Pong))
		return 0;
	    break;
	case IAXControl::Pong:
	    return 0;
	case IAXControl::LagRq:
	    if (findInFrameTimestamp(frame,IAXFrame::IAX,IAXControl::LagRp))
		return 0;
	    break;
	case IAXControl::LagRp:
	    return 0;
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::processInternalIncomingRequest(const IAXFullFrame* frame, bool& delFrame)
{
    if (frame->type() != IAXFrame::IAX)
	return 0;
    delFrame = true;
    switch (frame->subclass()) {
	case IAXControl::Ping:
	    postFrame(IAXFrame::IAX,IAXControl::Pong,0,0,frame->timeStamp(),true);
	    return 0;
	case IAXControl::LagRq:
	    postFrame(IAXFrame::IAX,IAXControl::LagRp,0,0,frame->timeStamp(),true);
	    return 0;
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::processMidCallControl(const IAXFullFrame* frame, bool& delFrame)
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

IAXEvent* IAXTransaction::processMidCallIAXControl(const IAXFullFrame* frame, bool& delFrame)
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
	    break;
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::remoteRejectCall(const IAXFullFrame* frame, bool& delFrame)
{
    delFrame = true;
    switch (type()) {
	case New:
	    if (frame->type() == IAXFrame::IAX && (frame->subclass() == IAXControl::Hangup || frame->subclass() == IAXControl::Reject))
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
    DDebug(m_engine,DebugNote,"Transaction(%u,%u) - Retransmitted %d frames on VNAK(%u)",
	localCallNo(),remoteCallNo(),c,seqNo);
    return 0;
}

IAXEvent* IAXTransaction::internalAccept()
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Internal accept",localCallNo(),remoteCallNo());
    sendAccept();
    return new IAXEvent(IAXEvent::Accept,true,true,this,IAXFrame::IAX,IAXControl::Accept);
}

IAXEvent* IAXTransaction::internalReject(String& reason)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Internal reject: '%s'",localCallNo(),remoteCallNo(),reason.c_str());
    sendReject(reason);
    IAXEvent* event = new IAXEvent(IAXEvent::Reject,true,true,this,IAXFrame::IAX,IAXControl::Reject);
    event->getList().appendString(IAXInfoElement::CAUSE,reason);
    m_localReqEnd = true;
    return event;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
