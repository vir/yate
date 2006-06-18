/**
 * engine.cpp
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
#include <yateversn.h>

#include <stdlib.h>
#include <string.h>

//static u_int64_t iax_events_allocated = 0;
//static u_int64_t iax_events_released = 0;

using namespace TelEngine;

IAXEngine::IAXEngine(int transCount, int retransCount, int retransInterval, int maxFullFrameDataLen, u_int32_t transTimeout)
    : Mutex(true), m_transList(0), m_transListCount(0), m_retransCount(retransCount),
      m_retransInterval(retransInterval), m_maxFullFrameDataLen(maxFullFrameDataLen), m_transactionTimeout(transTimeout)
{
    debugName("iaxengine");
    if (transCount < 4)
	transCount = 4;
    else if (transCount > 256)
	transCount = 256;
    m_transList = new ObjList*[transCount];
    int i;
    for (i = 0; i < transCount; i++)
	m_transList[i] = new ObjList;
    m_transListCount = transCount;
    for(i = 0; i <= IAX2_MAX_CALLNO; i++)
	m_lUsedCallNo[i] = false;
    m_lastGetEvIndex = 0;
    m_socket.create(AF_INET,SOCK_DGRAM);
    SocketAddr addr(AF_INET);
    addr.port(4569);
    m_socket.setBlocking(false);
    if (!m_socket.bind(addr))
	Debug(this,DebugWarn,"Failed to bind socket!");
    m_startLocalCallNo = 1 + (u_int16_t)(random() % IAX2_MAX_CALLNO);
}

IAXEngine::~IAXEngine()
{
    for (int i = 0; i < m_transListCount; i++)
	delete m_transList[i];
    delete[] m_transList;
}

IAXTransaction* IAXEngine::addFrame(const SocketAddr& addr, IAXFrame* frame)
{
    if (!frame)
	return 0;
    IAXTransaction* tr;
    ObjList* l;
    Lock lock(this);
    // Transaction exists for this frame?
    // Incomplete transactions. They MUST receive a full frame
    if (frame->fullFrame()) {
	l = m_incompleteTransList.skipNull();
	for (; l; l = l->next()) {
	    tr = static_cast<IAXTransaction*>(l->get());
	    if (!(tr && tr->localCallNo() == frame->fullFrame()->destCallNo() && addr == tr->remoteAddr()))
		continue;
	    // Complete transaction
	    if (tr->processFrame(frame)) {
		tr->m_rCallNo = frame->sourceCallNo();
		m_incompleteTransList.remove(tr,false);
		m_transList[frame->sourceCallNo() % m_transListCount]->append(tr);
		XDebug(this,DebugAll,"New remote transaction completed (%u,%u)",
		    tr->localCallNo(),tr->remoteCallNo());
		return tr;
	    }
	    break;
	}
    }
    // Complete transactions
    l = m_transList[frame->sourceCallNo() % m_transListCount];
    for (; l; l = l->next()) {
	tr = static_cast<IAXTransaction*>(l->get());
	if (!(tr && tr->remoteCallNo() == frame->sourceCallNo()))
	    continue;
	// Mini frame
	if (!frame->fullFrame()) {
	    if (addr == tr->remoteAddr())
		return tr->processFrame(frame);
	    continue;
	}
	// Full frame
	// Has a local number assigned? If not, test socket
	if ((frame->fullFrame())->destCallNo() || addr == tr->remoteAddr())
	    return tr->processFrame(frame);
    }
    // Frame doesn't belong to an existing transaction
    // Test if it is a full frame with an IAX control message that needs a new transaction
    if (!frame->fullFrame() || frame->type() != IAXFrame::IAX)
	return 0;
    switch (frame->subclass()) {
	case IAXControl::New:
	case IAXControl::RegReq:
	case IAXControl::RegRel:
	case IAXControl::Poke:
	    break;
	case IAXControl::Inval:
	    // These are often used as keepalives
	    return 0;
	case IAXControl::FwDownl:
	default:
	    DDebug(this,DebugAll,"Unsupported incoming transaction Frame(%u,%u)",
		frame->type(),frame->subclass());
	    return 0;
    }
    // Generate local number
    u_int16_t lcn = generateCallNo();
    if (!lcn)
	return 0;
    // Create and add transaction
    tr = IAXTransaction::factoryIn(this,(IAXFullFrame*)frame->fullFrame(),lcn,addr);
    m_transList[frame->sourceCallNo() % m_transListCount]->append(tr);
    return tr;
}

IAXTransaction* IAXEngine::addFrame(const SocketAddr& addr, const unsigned char* buf, unsigned int len)
{
    IAXFrame* frame = IAXFrame::parse(buf,len,this,&addr);
    if (!frame)
	return 0;
    IAXTransaction* tr = addFrame(addr,frame);
    if (!tr)
	frame->deref();
    return tr;
}

bool IAXEngine::process()
{
    bool ok = false;
    for (;;) {
	IAXEvent* event = getEvent(Time::msecNow());
	if (!event)
	    break;
	ok = true;
	if ((event->final() && !event->frameType()) || !event->getTransaction()) {
	    XDebug(this,DebugAll,"Deleting internal event type %u Frame(%u,%u)",
		event->type(),event->frameType(),event->subclass());
	    delete event;
	    continue;
	}
	if (!event->getTransaction()->connectionless()) 
	    processEvent(event);
	else
	    processConnectionlessEvent(event);
    }
    return ok;
}

void IAXEngine::readSocket(SocketAddr& addr)
{
    unsigned char buf[1500];

    while (1) {
	int len = m_socket.recvFrom(buf,sizeof(buf),addr);
	if (len == Socket::socketError()) {
	    if (!m_socket.canRetry())
		Debug(this,DebugWarn,"Socket read error: %s (%d)",
		    ::strerror(m_socket.error()),m_socket.error());
	    Thread::msleep(1,true);
	    continue;
	}
	addFrame(addr,buf,len);
    }
}

bool IAXEngine::writeSocket(const void* buf, int len, const SocketAddr& addr)
{
    len = m_socket.sendTo(buf,len,addr);
    if (len == Socket::socketError()) {
	if (!m_socket.canRetry())
	    Debug(this,DebugWarn,"Socket write error: %s (%d)",
		::strerror(m_socket.error()),m_socket.error());
	return false;
    }
    return true;
}

void IAXEngine::runGetEvents()
{
    while (1) {
	if (!process()) {
	    Thread::msleep(2,true);
	    continue;
	}
    }
}

void IAXEngine::removeTransaction(IAXTransaction* transaction)
{
    if (!transaction)
	return;
    Lock lock(this);
    releaseCallNo(transaction->localCallNo());
    if (!m_incompleteTransList.remove(transaction,false)) {
	if (m_transList[transaction->remoteCallNo() % m_transListCount]->remove(transaction,false)) {
	    DDebug(this,DebugAll,"Transaction(%u,%u) removed",
		transaction->localCallNo(),transaction->remoteCallNo());
	}
	else {
	    DDebug(this,DebugAll,"Trying to remove transaction(%u,%u) but does not exist",
		transaction->localCallNo(),transaction->remoteCallNo());
	}
    }
    else {
	DDebug(this,DebugAll,"Transaction(%u,%u) (incomplete outgoing) removed",
	    transaction->localCallNo(),transaction->remoteCallNo());
    }
}

u_int32_t IAXEngine::transactionCount()
{
    u_int32_t n = 0;
    ObjList* l;

    Lock lock(this);
    // Incomplete transactions
    for (l = m_incompleteTransList.skipNull(); l; l = l->next())
	if (l->get())
	    n++;
    // Complete transactions
    for (int i = 0; i < m_transListCount; i++)
	for (l = m_transList[i]->skipNull(); l; l = l->next())
	    if (l->get())
		n++;	
    return n;	
}

void IAXEngine::keepAlive(SocketAddr& addr)
{
    unsigned char buf[12] = {0x80,0,0,0,0,0,0,0,0,0,IAXFrame::IAX,IAXControl::Inval};
    writeSocket(buf,sizeof(buf),addr);
}

void IAXEngine::processEvent(IAXEvent* event)
{
    XDebug(this,DebugAll,"Default processing - deleting event %p Subclass %u",
	event,event->subclass());
    delete event;
}

void IAXEngine::processConnectionlessEvent(IAXEvent* event)
{
    XDebug(this,DebugAll,"Default conectionless processing - deleting event %p Subclass %u",
	event,event->subclass());
    delete event;
}

IAXEvent* IAXEngine::getEvent(u_int64_t time)
{
    IAXTransaction* tr;
    IAXEvent* ev;
    ObjList* l;

    Lock lock(this);
    // Find for incomplete transactions
    l = m_incompleteTransList.skipNull();
    for (; l; l = l->next()) {
	tr = static_cast<IAXTransaction*>(l->get());
	if (tr && 0 != (ev = tr->getEvent(time)))
	    return ev;
	continue;
    }
    // Find for complete transactions
    for (; m_lastGetEvIndex < m_transListCount; m_lastGetEvIndex++) {
	l = m_transList[m_lastGetEvIndex]->skipNull();
	for (; l; l = l->next()) {
	    tr = static_cast<IAXTransaction*>(l->get());
	    if (tr && 0 != (ev = tr->getEvent(time)))
		return ev;
	    continue;
	}
    }
    m_lastGetEvIndex = 0;
    return 0;
}

//TODO: Optimize generateCallNo & releaseCallNo
u_int16_t IAXEngine::generateCallNo()
{
    u_int16_t i;

    m_startLocalCallNo++;
    if (m_startLocalCallNo > IAX2_MAX_CALLNO)
	m_startLocalCallNo = 1;
    for (i = m_startLocalCallNo; i <= IAX2_MAX_CALLNO; i++)
	if (!m_lUsedCallNo[i]) {
	    m_lUsedCallNo[i] = true;
	    return i;
	}
    for (i = 1; i < m_startLocalCallNo; i++)
	if (!m_lUsedCallNo[i]) {
	    m_lUsedCallNo[i] = true;
	    return i;
	}
    Debug(this,DebugWarn,"Unable to generate call number. Transaction count: %u",transactionCount());
    return 0;
}

void IAXEngine::releaseCallNo(u_int16_t lcallno)
{
    m_lUsedCallNo[lcallno] = false;
}

IAXTransaction* IAXEngine::startLocalTransaction(IAXTransaction::Type type, const SocketAddr& addr, ObjList* ieList, IAXRegData* regdata)
{
    DataBlock data;
    bool localIEList = (ieList == 0);

    switch (type) {
	case IAXTransaction::New:
	case IAXTransaction::RegReq:
	case IAXTransaction::RegRel:
	case IAXTransaction::Poke:
	    break;
	case IAXTransaction::FwDownl:
	default:
	    Debug(this,DebugWarn,"Unsupported new transaction type %u requested",type);
	    return 0;
    }
    // Add fields
    if (type == IAXTransaction::New) {
	if (localIEList)
	    ieList = new ObjList;
	// ADD Version
	if (!IAXFrame::getIE(ieList,IAXInfoElement::VERSION))
	    ieList->insert(new IAXInfoElementNumeric(IAXInfoElement::VERSION,IAX_PROTOCOL_VERSION,2));
    }
    if (ieList) {
	IAXFrame::createIEListBuffer(ieList,data);
	if (localIEList)
	    delete ieList;
	if (data.length() > (unsigned int)m_maxFullFrameDataLen) {
	    Debug(this,DebugWarn,"New transaction IE list buffer too long (%u > %u)",
		data.length(),m_maxFullFrameDataLen);
	    return 0;
	}
    }
    Lock lock(this);
    u_int16_t lcn = generateCallNo();
    if (!lcn)
	return 0;
    switch (type) {
	case IAXTransaction::New:
	case IAXTransaction::Poke:
	    regdata = 0;
	    break;
	case IAXTransaction::RegReq:
	case IAXTransaction::RegRel:
	    break;
	default: ;
    }
    IAXTransaction* tr = IAXTransaction::factoryOut(this,type,lcn,addr,regdata,(unsigned char*)data.data(),data.length());
    m_incompleteTransList.append(tr);
    return tr;
}

/*
* IAXEvent
*/
IAXEvent::IAXEvent(Type type, bool final, IAXTransaction* transaction, u_int8_t frameType, u_int8_t subclass, ObjList* ieList)
    : m_type(type), m_frameType(frameType), m_subClass(subclass), m_final(final), m_transaction(0), m_ieList(ieList)
{
//    iax_events_allocated++;

    if (transaction && transaction->ref())
	m_transaction = transaction;
}

IAXEvent::~IAXEvent()
{
//    iax_events_released++;

    if (m_final && m_transaction && m_transaction->state() == IAXTransaction::Terminated) {
	m_transaction->getEngine()->removeTransaction(m_transaction);
    }
    if (m_transaction)
	m_transaction->deref();
    if (m_ieList)
	delete m_ieList;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
