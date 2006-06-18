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

//static u_int64_t iax_transaction_allocated = 0;
//static u_int64_t iax_transaction_released = 0;

using namespace TelEngine;

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
    m_private(data),
    m_lastMiniFrameOut(0xFFFF),
    m_lastMiniFrameIn(0),
    m_mutexInMedia(true),
    m_retransCount(5),
    m_retransInterval(500),
    m_pingInterval(20000),
    m_timeToNextPing(0),
    m_inTotalFramesCount(0),
    m_inOutOfOrderFrames(0),
    m_inDroppedFrames(0)
{
    XDebug(m_engine,DebugAll,"IAXTransaction::IAXTransaction(%u,%u) incoming [%p]",
	localCallNo(),remoteCallNo(),this);
//    iax_transaction_allocated++;

    if (m_engine) {
	m_retransCount = engine->retransCount();
	m_retransInterval = engine->retransInterval();
    }
    m_timeToNextPing = m_timeStamp + m_pingInterval;
    if (frame->subclass() != IAXControl::New)
	return;
    m_type = New;
    // Append frame to incoming list
    Lock lock(this);
    m_inFrames.append(frame);
    incrementSeqNo(frame,true);
    sendAck(frame->fullFrame());
}

IAXTransaction::IAXTransaction(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr, void* data,
			       const unsigned char* buf, unsigned int len)
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
    m_private(data),
    m_lastMiniFrameOut(0xFFFF),
    m_lastMiniFrameIn(0),
    m_mutexInMedia(true),
    m_retransCount(5),
    m_retransInterval(500),
    m_pingInterval(20000),
    m_timeToNextPing(0),
    m_inTotalFramesCount(0),
    m_inOutOfOrderFrames(0),
    m_inDroppedFrames(0)
{
    XDebug(m_engine,DebugAll,"IAXTransaction::IAXTransaction(%u,%u) outgoing [%p]",
	localCallNo(),remoteCallNo(),this);
//    iax_transaction_allocated++;

    if (m_engine) {
	m_retransCount = engine->retransCount();
	m_retransInterval = engine->retransInterval();
    }
    m_timeToNextPing = m_timeStamp + m_pingInterval;
    if (m_type != New)
	return;
    postFrame(IAXFrame::IAX,IAXControl::New,(void*)buf,len);
    changeState(NewSent);
}

IAXTransaction::~IAXTransaction()
{
//    iax_transaction_released++;
    XDebug(m_engine,DebugAll,"IAXTransaction::~IAXTransaction(%u,%u) - Type: %u. QUEUES: In: %u Out: %u",
	localCallNo(),remoteCallNo(),m_type,m_inFrames.count(),m_outFrames.count());

#ifdef XDEBUG
    if (m_outFrames.count()) {
	Debug(m_engine,DebugAll,"Timestamp: %u. Outgoing frames:",(u_int32_t)timeStamp());
	ObjList* l = m_outFrames.skipNull();
	for(int i = 0; l; l = l->next(), i++) {
	    IAXFrameOut* frame = static_cast<IAXFrameOut*>(l->get());
	    if (frame)
		Debug(m_engine,DebugAll,"%u:   Frame(%u,%u)  -  Timestamp: %u   ACK: %u",
		    i+1,frame->type(),frame->subclass(),frame->timeStamp(),frame->ack());
	}
    }
#endif
}

IAXTransaction* IAXTransaction::factoryIn(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, const SocketAddr& addr, void* data)
{
    switch (frame->subclass()) {
	case IAXControl::New:
	    return new IAXTransaction(engine,frame,lcallno,addr,data);
	case IAXControl::RegReq:
	case IAXControl::RegRel:
	case IAXControl::Poke:
	    return new IAXConnectionlessTransaction(engine,frame,lcallno,addr,data);
	case IAXControl::FwDownl:
	default: ;
    }
    return 0;
}

IAXTransaction* IAXTransaction::factoryOut(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr, void* data,
	const unsigned char* buf, unsigned int len)
{
    switch (type) {
	case New:
	    return new IAXTransaction(engine,type,lcallno,addr,data,buf,len);
	case RegReq:
	case RegRel:
	case Poke:
	    return new IAXConnectionlessTransaction(engine,type,lcallno,addr,data,buf,len);
	case FwDownl:
	default: ;
    }
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
    if (state() == Terminating)
	// Local terminate: Accept only Ack. Remote terminate: Accept none.
	if (m_localReqEnd) {
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack))
		return 0;
	}
	else
	    return 0;
    // Mini frame
    if (!frame->fullFrame())
	return processMedia(frame->data(),frame->timeStamp());
    // Full frame: Lock
    Lock lock(this);
    m_inTotalFramesCount++;
    // Do we have space?
    if (m_inFrames.count() == m_maxInFrames) {
	Debug(DebugWarn,"IAXTransaction(%u,%u) - processFrame. Buffer overrun!",localCallNo(),remoteCallNo());
	m_inDroppedFrames++;
	return 0;
    }
    bool fAck = frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack;
    if (!fAck && !isFrameAcceptable(frame->fullFrame()))
	return 0;
    incrementSeqNo(frame->fullFrame(),true);
    if (!fAck)
	sendAck(frame->fullFrame());
    // Voice full frame: process voice data
    if (frame->type() == IAXFrame::Voice) {
	processMedia(frame->data(),frame->timeStamp(),true);
	frame->data().clear();
    }
    // Append frame to incoming frame list
    m_inFrames.append(frame);
    DDebug(m_engine,DebugAll,"Transaction enqueued Frame(%u,%u) stamp=%u [%p]",
	frame->type(),frame->subclass(),frame->timeStamp(),this);
    return this;
}

IAXTransaction* IAXTransaction::processMedia(DataBlock& data, u_int32_t tStamp, bool voice)
{
    Lock lock(&m_mutexInMedia);
    if (!(voice || (tStamp & 0xffff0000))) {
	// miniframe timestamp
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
    // Need to send a full frame Voice ?
    if ((u_int16_t)ts < m_lastMiniFrameOut) {
	XDebug(m_engine,DebugAll,"Time to send VOICE: ts=%u last=%u [%p]",ts,m_lastMiniFrameOut,this);
	m_lastMiniFrameOut = (u_int16_t)ts;
	postFrame(IAXFrame::Voice,format,data.data(),data.length(),ts,true);
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

    if (state() == Terminated)
	return 0;
    Lock lock(this);
    if (state() == Terminating && !m_localReqEnd)
	return getEventTerminating(time);
    // Time to Ping remote peer ?
    if (time > m_timeToNextPing && state() != Terminating) {
	DDebug(m_engine,DebugAll,"Time to PING. %u",(u_int32_t)timeStamp());
	postFrame(IAXFrame::IAX,IAXControl::Ping,0,0,timeStamp(),true);
	m_timeToNextPing = time + m_pingInterval;
    }
    // Process outgoing frames
    ListIterator lout(m_outFrames);
    for (; (obj = lout.get());) {
	IAXFrameOut* frame = static_cast<IAXFrameOut*>(obj);
	ev = getEventResponse(frame,delFrame);
	if((frame->ack() && frame->ackOnly()) || delFrame) {
	    m_outFrames.remove(frame,true);
	    if (ev)
		return ev;
	    continue;
	}
	if (ev)
	    return ev;
	// No response. Timeout ?
	if (frame->timeout()) {
	    if (m_state == Terminating)
		// Client already notified: Terminate transaction
		return terminate(IAXEvent::Timeout);
	    else
		// Client not notified: Notify it and terminate transaction
		return terminate(IAXEvent::Timeout,frame->type(),frame->subclass());
	}
	// Retransmit ?
	if (frame->needRetrans(time)) {
	    if (frame->ack())
		frame->transmitted();   // Frame acknoledged: just update retransmission info
	    else {
		DDebug(m_engine,DebugAll,"Transaction resending Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
		    frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo(),frame->timeStamp(),this);
		sendFrame(frame);       // Retransmission
	    }
	}
    }
    // Process incoming frames
    ListIterator lin(m_inFrames);
    for (; (obj = lin.get());) {
	IAXFullFrame* frame = static_cast<IAXFullFrame*>(obj);
	DDebug(m_engine,DebugAll,"Transaction dequeued Frame(%u,%u) iseq=%u oseq=%u stamp=%u [%p]",
	    frame->type(),frame->subclass(),frame->iSeqNo(),frame->oSeqNo(),frame->timeStamp(),this);
	if (m_state == IAXTransaction::Unknown) 
	    ev = getEventStartTrans(frame,delFrame);  // New transaction
	else
	    ev = getEventRequest(frame,delFrame);
	if (delFrame)
	    m_inFrames.remove(frame,true);    // frame is no longer needded
	if (ev)
	    return ev;
    }
    // No pending outgoing frames. No valid requests. Clear incoming frames queue.
    //m_inDroppedFrames += m_inFrames.count();
    m_inFrames.clear();
    return 0;
}

bool IAXTransaction::sendAccept(u_int32_t format)
{
    if (!(state() == NewRecv || state() == NewRecv_AuthRepRecv))
	return false;
    unsigned char d[6] = {IAXInfoElement::FORMAT,4,format >> 24,format >> 16,format >> 8,format};
    postFrame(IAXFrame::IAX,IAXControl::Accept,d,sizeof(d),0,true);
    changeState(Connected);
    return true;
}

bool IAXTransaction::sendHangup(const char* cause, u_int8_t code)
{
    String s(cause);
    unsigned char d[3];
    DataBlock data,aux;

    if (state() == Terminated || state() == Terminating)
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

    if (state() == Terminated || state() == Terminating)
	return false;
    switch (type()) {
	case New:
	case RegReq:
	case RegRel:
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
    switch (type()) {
	case New:
	    postFrame(IAXFrame::IAX,IAXControl::Reject,data.data(),data.length(),0,true);
	    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Reject call. Cause: '%s'",localCallNo(),remoteCallNo(),cause);
	    break;
	case RegReq:
	case RegRel:
	    postFrame(IAXFrame::IAX,IAXControl::RegRej,data.data(),data.length(),0,true);
	    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Reject registration. Cause: '%s'",localCallNo(),remoteCallNo(),cause);
	    break;
	default: 
	    return false;
    }
    changeState(Terminating);
    m_localReqEnd = true;
    return true;
}

bool IAXTransaction::sendAuthReq(String& username, IAXAuthMethod::Type authmethod, String* authdata)
{
    if (state() != NewRecv)
	return false;
    ObjList ieList;
    ieList.append(new IAXInfoElementString(IAXInfoElement::USERNAME,(unsigned char*)(username.c_str()),username.length()));
    ieList.append(new IAXInfoElementNumeric(IAXInfoElement::AUTHMETHODS,authmethod,2));
    if (authmethod != IAXAuthMethod::Text)
	if (authdata)
	    ieList.append(new IAXInfoElementString(IAXInfoElement::CHALLENGE,(unsigned char*)(authdata->c_str()),authdata->length()));
	else
	    ieList.append(new IAXInfoElementString(IAXInfoElement::CHALLENGE,0,0));
    else
	ieList.append(new IAXInfoElementString(IAXInfoElement::PASSWORD,(unsigned char*)(authdata->c_str()),authdata->length()));
    DataBlock data;
    IAXFrame::createIEListBuffer(&ieList,data);
    postFrame(IAXFrame::IAX,IAXControl::AuthReq,data.data(),data.length(),0,false);
    changeState(NewRecv_AuthReqSent);
    return true;
}

bool IAXTransaction::sendAuthRep(String& iedata, IAXAuthMethod::Type auth)
{
    if (state() != NewSent_AuthReqRecv)
	return false;
    IAXInfoElement::Type ietype;
    switch (auth) {
	case IAXAuthMethod::MD5:
	    ietype = IAXInfoElement::MD5_RESULT;
	    break;
	case IAXAuthMethod::RSA:
	    ietype = IAXInfoElement::RSA_RESULT;
	    break;
	case IAXAuthMethod::Text:
	    ietype = IAXInfoElement::PASSWORD;
	    break;
	default:
	    return false;
    }
    unsigned char d[2] = {ietype,iedata.length()};
    DataBlock data(d,2);
    data.append(iedata);
    postFrame(IAXFrame::IAX,IAXControl::AuthRep,data.data(),data.length(),0,false);
    changeState(NewSent_AuthRepSent);
    return true;
}

bool IAXTransaction::sendText(const char* text)
{
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

IAXConnectionlessTransaction* IAXTransaction::connectionless()
{
    return 0;
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
	Debug(m_engine,DebugInfo,"IAXTransaction(%u,%u) - received frame out of order!!! Send VNAK",
	    localCallNo(),remoteCallNo());
	postFrame(IAXFrame::IAX,IAXControl::VNAK,0,0,0,true);
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

IAXEvent* IAXTransaction::terminate(u_int8_t evType, u_int8_t evClass, u_int8_t evSubclass, ObjList* ieList)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Terminated. Event: %u, Frame(%u,%u)",localCallNo(),remoteCallNo(),evType,evClass,evSubclass);
    IAXEvent* ev = new IAXEvent((IAXEvent::Type)evType,true,this,evClass,evSubclass,ieList);
    changeState(Terminated);
    deref();
    return ev;	
}

IAXEvent* IAXTransaction::waitForTerminate(u_int8_t evType, u_int8_t evClass, u_int8_t evSubclass, ObjList* ieList)
{
    Debug(m_engine,DebugAll,"Transaction(%u,%u) - Terminating. Event: %u, Frame(%u,%u)",localCallNo(),remoteCallNo(),evType,evClass,evSubclass);
    IAXEvent* ev = new IAXEvent((IAXEvent::Type)evType,true,this,evClass,evSubclass,ieList);
    changeState(Terminating);
    m_timeout = m_engine->transactionTimeout() + Time::secNow();
    return ev;
}

void IAXTransaction::postFrame(IAXFrame::Type type, u_int32_t subclass, void* data, u_int16_t len, u_int32_t tStamp, bool ackOnly)
{
    if (state() == Terminated)
	return;
    if (!tStamp)
	tStamp = timeStamp();
    Lock lock(this);
    IAXFrameOut* frame = new IAXFrameOut(type,subclass,m_lCallNo,m_rCallNo,m_oSeqNo,m_iSeqNo,tStamp,
					 (unsigned char*)data,len,m_retransCount,m_retransInterval,ackOnly);
    DDebug(m_engine,DebugAll,"Transaction posting Frame(%u,%u) oseq=%u iseq=%u stamp=%u [%p]",
	type,subclass,m_oSeqNo,m_iSeqNo,tStamp,this);
    incrementSeqNo(frame,false);
    m_outFrames.append(frame);
    sendFrame(frame);
}

bool IAXTransaction::sendFrame(IAXFrameOut* frame)
{
    if (!frame)
	return false;
    bool b = m_engine->writeSocket(frame->data().data(),frame->data().length(),remoteAddr());
    if (!frame->retrans())
	frame->setRetrans();
    frame->transmitted();
    return b;
}

IAXEvent* IAXTransaction::createEvent(u_int8_t evType, const IAXFullFrame* frame, State newState)
{
    ObjList* ieList = 0;
    bool invalidIE;

    if (!frame)
	return 0;
    if (0 == (ieList = IAXFrame::getIEList(frame,invalidIE)) && invalidIE)
	return 0;
    changeState(newState);
    switch (m_state) {
	case Terminating:
	    return waitForTerminate((IAXEvent::Type)evType,frame->type(),frame->subclass(),ieList);
	case Terminated:
	    return terminate((IAXEvent::Type)evType,frame->type(),frame->subclass(),ieList);
	default: ;
    }
    return new IAXEvent((IAXEvent::Type)evType,false,this,frame->type(),frame->subclass(),ieList);
}

IAXEvent* IAXTransaction::createResponse(IAXFrameOut* frame, u_int8_t findType, u_int8_t findSubclass, u_int8_t evType, State newState)
{
    IAXFullFrame* ffind = findInFrame((IAXFrame::Type)findType,findSubclass);
    if (ffind) {
	frame->setAck();
	IAXEvent* ev = createEvent(evType,ffind,newState);
	m_inFrames.remove(ffind,true);
	return ev;
    }
    return 0;
}

IAXEvent* IAXTransaction::getEventResponse(IAXFrameOut* frame, bool& delFrame)
{
    IAXEvent* event;

    delFrame = true;
    bool fAck = findInFrameTimestamp(frame);
    if (fAck)
	frame->setAck();
    // Terminating frame sent. Just wait for ACK
    if (state() == Terminating && fAck)
	return terminate(IAXEvent::Terminated,0,0,0);
    // Accepted or answered ?
    if (fAck) {
	if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Accept)
	    return new IAXEvent(IAXEvent::Accept,false,this,IAXFrame::IAX,IAXControl::Ack,0);
	if (frame->type() == IAXFrame::Control && frame->subclass() == IAXFullFrame::Answer)
	    return new IAXEvent(IAXEvent::Answer,false,this,IAXFrame::IAX,IAXControl::Ack,0);
    }
    switch (m_state) {
	case Connected:
	    break;
	case NewSent:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::New))
		break;
	    // Frame is NEW: AUTHREQ, ACCEPT, REJECT, HANGUP ?
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::AuthReq,IAXEvent::AuthReq,NewSent_AuthReqRecv)))
		return event;
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Accept,IAXEvent::Accept,Connected)))
		return event;
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Reject,IAXEvent::Reject,Terminating)))
		return event;
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Hangup,IAXEvent::Hangup,Terminating)))
		return event;
	    break;
	case NewSent_AuthRepSent:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::AuthRep))
		break;
	    // Frame is AUTHREP: ACCEPT, REJECT, HANGUP ?
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Accept,IAXEvent::Accept,Connected)))
		return event;
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Reject,IAXEvent::Reject,Terminating))) 
		return event;
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Hangup,IAXEvent::Hangup,Terminating)))
		return event;
	    break;
	case NewRecv_AuthReqSent:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::AuthReq))
		break;
	    // Frame is AUTHREQ: AUTHREP, REJECT, HANGUP ?
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::AuthRep,IAXEvent::AuthRep,NewRecv_AuthRepRecv)))
		return event;
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Reject,IAXEvent::Reject,Terminating)))
		return event;
	    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Hangup,IAXEvent::Hangup,Terminating)))
		return event;
	    break;
	default: ;
    }
    // Internal stuff
    return processInternalOutgoingRequest(frame,delFrame);
}

IAXEvent* IAXTransaction::getEventStartTrans(IAXFullFrame* frame, bool& delFrame)
{
    delFrame = true;
    IAXEvent* ev = 0;
    switch (type()) {
	case New:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::New))
		break;
	    ev = createEvent(IAXEvent::NewCall,frame,NewRecv);
	    if (ev) {
		IAXInfoElement* ie = ev->getIE(IAXInfoElement::VERSION);
		if (!ie || (static_cast<IAXInfoElementNumeric*>(ie))->data() > IAX_PROTOCOL_VERSION) {
		    delete ev;
		    ev = 0;
		    sendReject("Unsupported or missing protocol version");
		    return 0;
		}
	    }
	    return ev;
	case Poke:
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Poke))
		break;
	    // Send PONG
	    postFrame(IAXFrame::IAX,IAXControl::Pong,0,0,frame->timeStamp(),true);
	    return createEvent(IAXEvent::Terminated,frame,Terminating);
	case RegReq:
	case RegRel:
	    if (!(frame->type() == IAXFrame::IAX && 
		(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
		break;
	    return createEvent(IAXEvent::NewRegistration,frame,RegRecv);
	case FwDownl:
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::getEventRequest(IAXFullFrame* frame, bool& delFrame)
{
    IAXEvent* event;

    // INVAL ?
    delFrame = true;
    if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Inval) {
	Debug(m_engine,DebugAll,"IAXTransaction(%u,%u) - Received INVAL. Terminate.",localCallNo(),remoteCallNo());
	return createEvent(IAXEvent::Invalid,frame,Terminated);
    }
    switch (m_state) {
	case Connected:
	    switch (frame->type()) {
		case IAXFrame::Voice:
		    return createEvent(IAXEvent::Voice,frame,m_state);
		case IAXFrame::Control:
		    return processMidCallControl(frame,delFrame);
		case IAXFrame::IAX:
		    return processMidCallIAXControl(frame,delFrame);
		case IAXFrame::DTMF:
		    return createEvent(IAXEvent::Dtmf,frame,m_state);
		case IAXFrame::Text:
		    return createEvent(IAXEvent::Text,frame,m_state);
		case IAXFrame::Noise:
		    return createEvent(IAXEvent::Noise,frame,m_state);
		// NOT IMPLEMENTED
		case IAXFrame::Video:
		case IAXFrame::Image:
		case IAXFrame::HTML:
		    return createEvent(IAXEvent::NotImplemented,frame,m_state);
		default: ;
	    }
	    break;
	case NewSent_AuthReqRecv:
	case NewRecv:
	case NewRecv_AuthRepRecv:
	    if (0 != (event = remoteRejectCall(frame,delFrame)))
		return event;
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
	    return createEvent(IAXEvent::Hangup,frame,Terminating);
	case IAXFullFrame::Busy:
	    return createEvent(IAXEvent::Busy,frame,Terminating);
	case IAXFullFrame::Ringing:
	    return createEvent(IAXEvent::Ringing,frame,m_state);
	case IAXFullFrame::Answer:
	    return createEvent(IAXEvent::Answer,frame,Connected);
	case IAXFullFrame::Progressing:
	case IAXFullFrame::Proceeding:
	    return createEvent(IAXEvent::Progressing,frame,m_state);
	case IAXFullFrame::Hold:
	case IAXFullFrame::Unhold:
	case IAXFullFrame::Congestion:
	case IAXFullFrame::FlashHook:
	case IAXFullFrame::Option:
	case IAXFullFrame::KeyRadio:
	case IAXFullFrame::UnkeyRadio:
	case IAXFullFrame::VidUpdate:
	    return createEvent(IAXEvent::NotImplemented,frame,m_state);
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
	    return createEvent(IAXEvent::Quelch,frame,m_state);
	case IAXControl::Unquelch:
	    return createEvent(IAXEvent::Unquelch,frame,m_state);
	case IAXControl::Hangup:
	case IAXControl::Reject:
	    return createEvent(IAXEvent::Hangup,frame,Terminating);
	case IAXControl::New:
	case IAXControl::Accept:
	case IAXControl::AuthReq:
	case IAXControl::AuthRep:
	    // Already received: Ignore
	    return 0;
	case IAXControl::Inval:
	    return createEvent(IAXEvent::Invalid,frame,Terminated);
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
		return createEvent(IAXEvent::Reject,frame,Terminating);
	    break;
	case RegReq:
	case RegRel:
	    if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::RegRej)
		return createEvent(IAXEvent::Reject,frame,Terminating);
	    break;
	default: ;
    }
    delFrame = false;
    return 0;
}

IAXEvent* IAXTransaction::getEventTerminating(u_int64_t time)
{
    if (time > m_timeout) {
	Debug(m_engine,DebugAll,"Transaction(%u,%u) - Timeout on remote request. Timestamp: " FMT64U,
	    localCallNo(),remoteCallNo(),timeStamp());
	return terminate(IAXEvent::Timeout);
    }
    return 0;
}

/**
 * IAXConnectionlessTransaction
 */
IAXConnectionlessTransaction::IAXConnectionlessTransaction(IAXEngine* engine, IAXFullFrame* frame,
	u_int16_t lcallno, const SocketAddr& addr, void* data)
    : IAXTransaction(engine,frame,lcallno,addr,0),
      m_expire(60), m_userdata(0)
{
    // Set transaction type
    switch (frame->subclass()) {
	case IAXControl::RegReq:
	    m_type = RegReq;
	    break;
	case IAXControl::RegRel:
	    m_type = RegRel;
	    break;
	case IAXControl::Poke:
	    m_type = Poke;
	    break;
	case IAXControl::New:
	case IAXControl::FwDownl:
	default:
	    return;
    }
    // Frame is RegReq/RegRel
    if (type() == RegReq || type() == RegRel) {
	bool invalid;
	ObjList* ieList = IAXFrame::getIEList(frame,invalid);
	if (ieList) {
	    IAXInfoElement* ie = IAXFrame::getIE(ieList,IAXInfoElement::USERNAME);
	    if (ie)
		m_username = (static_cast<IAXInfoElementString*>(ie))->data();
	    ie = IAXFrame::getIE(ieList,IAXInfoElement::REFRESH);
	    if (ie)
		m_expire = (static_cast<IAXInfoElementNumeric*>(ie))->data();
	    delete ieList;
	}
    }
    // Append frame to incoming list
    Lock lock(this);
    m_inFrames.append(frame);
    incrementSeqNo(frame,true);
    sendAck(frame->fullFrame());
    XDebug(m_engine,DebugAll,"IAXConnectionlessTransaction::IAXConnectionlessTransaction(%u,%u) incoming Type: %u [%p]",
	localCallNo(),remoteCallNo(),m_type,this);
}

IAXConnectionlessTransaction::IAXConnectionlessTransaction(IAXEngine* engine, Type type, u_int16_t lcallno, 
	const SocketAddr& addr, void* data, const unsigned char* buf, unsigned int len)
    : IAXTransaction(engine,type,lcallno,addr,data,buf,len),
      m_expire(60), m_userdata(0)
{
    switch (type) {
	case RegReq:
	    postFrame(IAXFrame::IAX,IAXControl::RegReq,(void*)buf,len);
	    changeState(RegSent);
	    break;
	case RegRel:
	    postFrame(IAXFrame::IAX,IAXControl::RegRel,(void*)buf,len);
	    changeState(RegSent);
	    break;
	case Poke:
	    postFrame(IAXFrame::IAX,IAXControl::Poke);
	    changeState(PokeSent);
	    break;
	case FwDownl:
	case New:
	default:
	    return;
    }
    // Frame is RegReq/RegRel
    IAXRegData* regdata = static_cast<IAXRegData*>(data);
    if (!regdata)
	return;
    m_username = regdata->m_username;
    m_password = regdata->m_password;
    m_callingNo = regdata->m_callingNo;
    m_callingName = regdata->m_callingName;
    m_expire = regdata->m_expire;
    m_name = regdata->m_name;
    m_userdata = regdata->m_userdata;
    XDebug(m_engine,DebugAll,"IAXConnectionlessTransaction::IAXConnectionlessTransaction(%u,%u) outgoing Type: %u [%p]",
	localCallNo(),remoteCallNo(),m_type,this);
}

IAXConnectionlessTransaction::~IAXConnectionlessTransaction()
{
    XDebug(m_engine,DebugAll,"IAXConnectionlessTransaction::~IAXConnectionlessTransaction(%u,%u) [%p]",
	localCallNo(),remoteCallNo(),this);
}

IAXTransaction* IAXConnectionlessTransaction::processFrame(IAXFrame* frame)
{
    if (!(frame && frame->fullFrame()))
	return 0;
    if (state() == Terminated) {
	sendInval();
	return 0;
    }
    if (state() == Terminating)
	// Local terminate: Accept only ACK. Remote terminate: Accept none.
	if (m_localReqEnd) {
	    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack))
		return 0;
	}
	else
	    return 0;
    // Full frame: Lock
    Lock lock(this);
    m_inTotalFramesCount++;
    // Do we have space?
    if (m_inFrames.count() == m_maxInFrames) {
	Debug(m_engine,DebugWarn,"IAXConnectionlessTransaction(%u,%u) - processFrame. Buffer overrun! [%p]",
	    localCallNo(),remoteCallNo(),this);
	//TODO: WARN - Too many frames in buffer  Send Congestion ?
	m_inDroppedFrames++;
	return 0;
    }
    bool fAck = frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Ack;
    if (!fAck && !isFrameAcceptable(frame->fullFrame()))
	return 0;
    incrementSeqNo(frame->fullFrame(),true);
    if (!fAck)
	sendAck(frame->fullFrame());
    // Append frame to incoming frame list
    m_inFrames.append(frame);
    return this;
}

void IAXConnectionlessTransaction::fillRegData(IAXRegData& regdata)
{
    regdata.m_username = m_username;
    regdata.m_password = m_password;
    regdata.m_callingNo = m_callingNo;
    regdata.m_callingName = m_callingName;
    regdata.m_expire = m_expire;
    regdata.m_name = m_name;
    regdata.m_userdata = m_userdata;
}

bool IAXConnectionlessTransaction::sendReg(String& authdata, IAXAuthMethod::Type authmethod)
{
    if (!(state() == RegSent_RegAuthRecv && (type() == RegReq || type() == RegRel)))
	return false;
    IAXInfoElement::Type result;
    switch (authmethod) {
	case IAXAuthMethod::MD5:
	    result = IAXInfoElement::MD5_RESULT;
	    break;
	case IAXAuthMethod::RSA:
	    result = IAXInfoElement::RSA_RESULT;
	    break;
	case IAXAuthMethod::Text:
	    result = IAXInfoElement::PASSWORD;
	    break;
	default:
	    return false;
    }
    ObjList ieList;
    ieList.append(new IAXInfoElementString(IAXInfoElement::USERNAME,(unsigned char*)(m_username.c_str()),m_username.length()));
    ieList.append(new IAXInfoElementString(result,(unsigned char*)(authdata.c_str()),authdata.length()));
    DataBlock data;
    IAXFrame::createIEListBuffer(&ieList,data);
    if (type() == RegReq)
	postFrame(IAXFrame::IAX,IAXControl::RegReq,data.data(),data.length(),0,false);
    else
	postFrame(IAXFrame::IAX,IAXControl::RegRel,data.data(),data.length(),0,false);
    changeState(RegSent_RegSent);
    return true;
}

bool IAXConnectionlessTransaction::sendRegAuth(String& password, IAXAuthMethod::Type authmethod, String* authdata)
{
    if (!(state() == RegRecv && (type() == RegReq || type() == RegRel)))
	return false;
    m_password = password;
    if (authdata)
	m_challenge = *authdata;
    ObjList ieList;
    ieList.append(new IAXInfoElementString(IAXInfoElement::USERNAME,(unsigned char*)(m_username.c_str()),m_username.length()));
    ieList.append(new IAXInfoElementNumeric(IAXInfoElement::AUTHMETHODS,authmethod,2));
    if (authmethod != IAXAuthMethod::Text)
	ieList.append(new IAXInfoElementString(IAXInfoElement::CHALLENGE,(unsigned char*)(m_challenge.c_str()),m_challenge.length()));
    DataBlock data;
    IAXFrame::createIEListBuffer(&ieList,data);
    postFrame(IAXFrame::IAX,IAXControl::RegAuth,data.data(),data.length(),0,false);
    changeState(RegRecv_RegAuthSent);
    return true;
}

bool IAXConnectionlessTransaction::sendRegAck()
{
    if (!((state() == RegRecv && type() == RegReq) ||
	(state() == RegRecv_RegRecv && (type() == RegReq || type() == RegRel))))
	return false;
    ObjList ieList;
    ieList.append(new IAXInfoElementString(IAXInfoElement::USERNAME,(unsigned char*)(m_username.c_str()),m_username.length()));
    ieList.append(new IAXInfoElementString(IAXInfoElement::CALLING_NUMBER,(unsigned char*)(m_callingNo.c_str()),m_callingNo.length()));
    ieList.append(new IAXInfoElementString(IAXInfoElement::CALLING_NAME,(unsigned char*)(m_callingName.c_str()),m_callingName.length()));
    ieList.append(new IAXInfoElementNumeric(IAXInfoElement::REFRESH,m_expire,2));
    ieList.append(IAXInfoElementBinary::packIP(remoteAddr()));
    DataBlock data;
    IAXFrame::createIEListBuffer(&ieList,data);
    postFrame(IAXFrame::IAX,IAXControl::RegAck,data.data(),data.length(),0,false);
    changeState(Terminating);
    m_localReqEnd = true;
    return true;
}

IAXConnectionlessTransaction* IAXConnectionlessTransaction::connectionless()
{
    return this;
}

IAXEvent* IAXConnectionlessTransaction::getEventResponse(IAXFrameOut* frame, bool& delFrame)
{
    IAXEvent* event;

    delFrame = true;
    bool fAck = findInFrameTimestamp(frame);
    if (fAck)
	frame->setAck();
    // Terminating frame sent. Just wait for ACK
    if (m_state == Terminating && fAck)
	return terminate(IAXEvent::Terminated,0,0,0);
    switch (type()) {
	case RegReq:
	case RegRel:
	    switch (m_state) {
		case RegSent:
		    if (!(frame->type() == IAXFrame::IAX && 
			(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
			break;
		    // Frame is REGREQ ? Find REGACK. Else: Find REGAUTH
		    if (frame->subclass() == IAXControl::RegReq)
			if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegAck,IAXEvent::RegAck,Terminating)))
			    return event;
		    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegAuth,IAXEvent::RegAuth,RegSent_RegAuthRecv)))
			return event;
		    // REGREJ ?
		    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegRej,IAXEvent::Reject,Terminating)))
			return event;
		    break;
		case RegSent_RegSent:
		    if (!(frame->type() == IAXFrame::IAX && 
			(frame->subclass() == IAXControl::RegReq || frame->subclass() == IAXControl::RegRel)))
			break;
		    // Frame is REGREQ/REGREL. Find REGACK, REGREJ
		    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegAck,IAXEvent::RegAck,Terminating)))
			return event;
		    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegRej,IAXEvent::Reject,Terminating)))
			return event;
		    break;
		case RegRecv_RegAuthSent:
		    if (!(frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::RegAuth))
			break;
		    // Frame is REGAUTH. Find REGREQ/REGREL, REGREJ
		    if (type() == RegReq) {
			if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegReq,IAXEvent::RegRecv,RegRecv_RegRecv)))
			    return event;
		    }
		    else {
			if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegRel,IAXEvent::RegRecv,RegRecv_RegRecv)))
			    return event;
		    }
		    if (0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::RegRej,IAXEvent::Reject,Terminating)))
			return event;
		    break;
		default: ;
	    }
	    break;
	case Poke:
	    if (m_state == PokeSent && frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Poke &&
		0 != (event = createResponse(frame,IAXFrame::IAX,IAXControl::Pong,IAXEvent::Terminated,Terminating))) {
		frame->setAck();
		return event;
	    }
	    break;
	default: ;
    }
    delFrame = false;
    return processInternalOutgoingRequest(frame,delFrame);
}

IAXEvent* IAXConnectionlessTransaction::getEventRequest(IAXFullFrame* frame, bool& delFrame)
{
    IAXEvent* event;

    delFrame = true;
    // INVAL ?
    if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Inval) {
	Debug(m_engine,DebugAll,"IAXTransaction(%u,%u) - Received INVAL. Terminate [%p]",
	    localCallNo(),remoteCallNo(),this);
	return createEvent(IAXEvent::Invalid,frame,Terminated);
    }
    delFrame = true;
    switch (type()) {
	case RegReq:
	case RegRel:
	    switch (m_state) {
		case RegSent_RegAuthRecv:
		case RegRecv:
		case RegRecv_RegRecv:
		    if (0 != (event = remoteRejectCall(frame,delFrame)))
			return event;
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

/* vi: set ts=8 sw=4 sts=4 noet: */
