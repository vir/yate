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

using namespace TelEngine;

IAXEngine::IAXEngine(int port, u_int16_t transListCount, u_int16_t retransCount, u_int16_t retransInterval,
	u_int16_t authTimeout, u_int16_t transTimeout, u_int16_t maxFullFrameDataLen,
	u_int32_t format, u_int32_t capab, u_int32_t trunkSendInterval)
    : Mutex(true),
    m_lastGetEvIndex(0),
    m_maxFullFrameDataLen(maxFullFrameDataLen),
    m_startLocalCallNo(0),
    m_transListCount(0),
    m_retransCount(retransCount),
    m_retransInterval(retransInterval),
    m_authTimeout(authTimeout),
    m_transTimeout(transTimeout),
    m_format(format),
    m_capability(capab),
    m_mutexTrunk(true),
    m_trunkSendInterval(trunkSendInterval)
{
    debugName("iaxengine");
    if ((port <= 0) || port > 65535)
	port = 4569;
    if (transListCount < 4)
	transListCount = 4;
    else if (transListCount > 256)
	transListCount = 256;
    m_transList = new ObjList*[transListCount];
    int i;
    for (i = 0; i < transListCount; i++)
	m_transList[i] = new ObjList;
    m_transListCount = transListCount;
    for(i = 0; i <= IAX2_MAX_CALLNO; i++)
	m_lUsedCallNo[i] = false;
    m_socket.create(AF_INET,SOCK_DGRAM);
    SocketAddr addr(AF_INET);
    addr.port(port);
    m_socket.setBlocking(false);
    if (!m_socket.bind(addr))
	Debug(this,DebugWarn,"Failed to bind socket on port %d",port);
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
		XDebug(this,DebugAll,"New incomplete outgoing transaction completed (%u,%u)",
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
	    if (addr == tr->remoteAddr()) {
		// keep transaction referenced but unlock the engine
		RefPointer<IAXTransaction> t = tr;
		lock.drop();
		return t ? t->processFrame(frame) : 0;
	    }
	    continue;
	}
	// Full frame
	// Has a local number assigned? If not, test socket
	if ((frame->fullFrame())->destCallNo() || addr == tr->remoteAddr()) {
	    // keep transaction referenced but unlock the engine
	    RefPointer<IAXTransaction> t = tr;
	    lock.drop();
	    return t ? t->processFrame(frame) : 0;
	}
    }
    // Frame doesn't belong to an existing transaction
    // Test if it is a full frame with an IAX control message that needs a new transaction
    if (!frame->fullFrame() || frame->type() != IAXFrame::IAX)
	return 0;
    switch (frame->fullFrame()->subclass()) {
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
	    if (frame->fullFrame()) {
	        if (frame->fullFrame()->destCallNo())
		    XDebug(this,DebugAll,"Unmatched Frame(%u,%u) for (%u,%u)",
			frame->type(),frame->fullFrame()->subclass(),frame->fullFrame()->destCallNo(),frame->fullFrame()->sourceCallNo());
		else
		    DDebug(this,DebugAll,"Unsupported incoming transaction Frame(%u,%u). Source call no: %u",
			frame->type(),frame->fullFrame()->subclass(),frame->fullFrame()->sourceCallNo());
	    }
	    return 0;
    }
    // Generate local number
    u_int16_t lcn = generateCallNo();
    if (!lcn)
	return 0;
    // Create and add transaction
    tr = IAXTransaction::factoryIn(this,(IAXFullFrame*)frame->fullFrame(),lcn,addr);
    if (tr)
	m_transList[frame->sourceCallNo() % m_transListCount]->append(tr);
    else
	releaseCallNo(lcn);
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
	processEvent(event);
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
	else
	    DDebug(this,DebugMild,"Socket temporary unavailable: %s (%d)",
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

    Lock lock(this);
    // Incomplete transactions
    n += m_incompleteTransList.count();
    // Complete transactions
    for (int i = 0; i < m_transListCount; i++)
	n += m_transList[i]->count();
    return n;
}

void IAXEngine::keepAlive(SocketAddr& addr)
{
    unsigned char buf[12] = {0x80,0,0,0,0,0,0,0,0,0,IAXFrame::IAX,IAXControl::Inval};
    writeSocket(buf,sizeof(buf),addr);
}

bool IAXEngine::processTrunkFrames(u_int32_t time)
{
    Lock lock(&m_mutexTrunk);
    bool sent = false;
    for (ObjList* l = m_trunkList.skipNull(); l; l = l->next()) {
	IAXMetaTrunkFrame* frame = static_cast<IAXMetaTrunkFrame*>(l->get());
	// Frame has mini frame(s) ?
	if (!frame->timestamp())
	    continue;
	int32_t interval = time - frame->timestamp();
        if (!interval || (interval && (u_int32_t)interval < m_trunkSendInterval))
	    continue;
	// If the time wrapped around, send it. Worst case: we'll send an empty frame
	frame->send(time);
	sent = true;
    }
    return sent;
}

void IAXEngine::processEvent(IAXEvent* event)
{
    XDebug(this,DebugAll,"Default processing - deleting event %p Subclass %u",
	event,event->subclass());
    delete event;
}

IAXEvent* IAXEngine::getEvent(u_int64_t time)
{
    IAXTransaction* tr;
    IAXEvent* ev;
    ObjList* l;

    lock();
    // Find for incomplete transactions
    l = m_incompleteTransList.skipNull();
    for (; l; l = l->next()) {
	tr = static_cast<IAXTransaction*>(l->get());
	if (tr && 0 != (ev = tr->getEvent(time))) {
	    unlock();
	    return ev;
	}
	continue;
    }
    // Find for complete transactions, start with current index
    while (m_lastGetEvIndex < m_transListCount) {
	l = m_transList[m_lastGetEvIndex++]->skipNull();
	if (!l)
	    continue;
	ListIterator iter(*l);
	for (;;) {
	    tr = static_cast<IAXTransaction*>(iter.get());
	    // end of iteration?
	    if (!tr)
		break;
	    RefPointer<IAXTransaction> t = tr;
	    // dead pointer?
	    if (!t)
		continue;
	    unlock();
	    if (0 != (ev = t->getEvent(time)))
		return ev;
	    lock();
	}
    }
    m_lastGetEvIndex = 0;
    unlock();
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

IAXTransaction* IAXEngine::startLocalTransaction(IAXTransaction::Type type, const SocketAddr& addr, IAXIEList& ieList, bool trunking)
{
    Lock lock(this);
    u_int16_t lcn = generateCallNo();
    if (!lcn)
	return 0;
    IAXTransaction* tr = IAXTransaction::factoryOut(this,type,lcn,addr,ieList);
    if (tr) {
	m_incompleteTransList.append(tr);
	if (trunking)
	    enableTrunking(tr);
    }
    else
	releaseCallNo(lcn);
    return tr;
}

bool IAXEngine::acceptFormatAndCapability(IAXTransaction* trans)
{
    if (!trans)
	return false;
    u_int32_t format = trans->format();
    u_int32_t capability = m_capability & trans->capability();
    // Valid capability ?
    if (!capability)
	return false;
    for (;;) {
	// Received format is valid ?
	if (0 != (format & capability) && IAXFormat::audioText(format))
	    break;
	// Local format is valid ?
	format = m_format;
	if (0 != (m_format & capability) && IAXFormat::audioText(format))
	    break;
	// No valid format: choose one from capability
	format = 0;
	u_int32_t i = 0;
	for (; IAXFormat::audioData[i].value; i++)
	    if (0 != (capability & IAXFormat::audioData[i].value))
		break;
	if (IAXFormat::audioData[i].value) {
	    format = IAXFormat::audioData[i].value;
	    break;
	}
	return false;
    }
    trans->m_format = format;
    trans->m_capability = capability;
    if (trans->outgoing())
	trans->m_formatIn = format;
    else
	trans->m_formatOut = format;
    return true;
}

void IAXEngine::defaultEventHandler(IAXEvent* event)
{
    DDebug(this,DebugAll,"defaultEventHandler - Event type: %u. Frame - Type: %u Subclass: %u",
	event->type(),event->frameType(),event->subclass());
    IAXTransaction* tr = event->getTransaction();
    switch (event->type()) {
	case IAXEvent::New:
	    tr->sendReject("Feature not implemented or unsupported");
	    break;
	default: ;
    }
}

void IAXEngine::enableTrunking(IAXTransaction* trans)
{
    if (!trans || trans->type() != IAXTransaction::New)
	return;
    Lock lock(&m_mutexTrunk);
    IAXMetaTrunkFrame* frame;
    // Already enabled ?
    for (ObjList* l = m_trunkList.skipNull(); l; l = l->next()) {
	frame = static_cast<IAXMetaTrunkFrame*>(l->get());
	if (frame && frame->addr() == trans->remoteAddr()) {
	    trans->enableTrunking(frame);
	    return;
	}
    }
    frame = new IAXMetaTrunkFrame(this,trans->remoteAddr());
    if (trans->enableTrunking(frame))
	m_trunkList.append(frame);
    // Deref frame: Only transactions are allowed to keep references for it
    frame->deref();
}

void IAXEngine::removeTrunkFrame(IAXMetaTrunkFrame* trunkFrame)
{
    Lock lock(&m_mutexTrunk);
    m_trunkList.remove(trunkFrame,false);
}

void IAXEngine::runProcessTrunkFrames()
{
    while (1) {
	processTrunkFrames();
	Thread::msleep(2,true);
    }
}

void IAXEngine::getMD5FromChallenge(String& md5data, const String& challenge, const String& password)
{
    MD5 md5;
    md5 << challenge << password;
    md5data = md5.hexDigest();
}

bool IAXEngine::isMD5ChallengeCorrect(const String& md5data, const String& challenge, const String& password)
{
    MD5 md5;
    md5 << challenge << password;
    return md5data == md5.hexDigest();
}

/*
* IAXEvent
*/
IAXEvent::IAXEvent(Type type, bool local, bool final, IAXTransaction* transaction, u_int8_t frameType, u_int32_t subclass)
    : m_type(type), m_frameType(frameType), m_subClass(subclass), m_local(local), m_final(final), m_transaction(0)

{
    if (transaction && transaction->ref())
	m_transaction = transaction;
}

IAXEvent::IAXEvent(Type type, bool local, bool final, IAXTransaction* transaction, const IAXFullFrame* frame)
    : m_type(type), m_frameType(0), m_subClass(0), m_local(local), m_final(final), m_transaction(0), m_ieList(frame)

{
    if (transaction && transaction->ref())
	m_transaction = transaction;
    if (frame) {
	m_frameType = frame->type();
	m_subClass = frame->subclass();
    }
}

IAXEvent::~IAXEvent()
{
    if (m_final && m_transaction && m_transaction->state() == IAXTransaction::Terminated)
	m_transaction->getEngine()->removeTransaction(m_transaction);
    if (m_transaction) {
	m_transaction->eventTerminated(this);
	m_transaction->deref();
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
