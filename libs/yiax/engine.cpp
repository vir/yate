/**
 * engine.cpp
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
#include <yateversn.h>

#include <stdlib.h>
#include <string.h>

using namespace TelEngine;

// Local call number to set when rejecting calls with missing call token
#define IAX2_CALLTOKEN_REJ_CALLNO 1
// Local call number to set when sending call token message
#define IAX2_CALLTOKEN_CALLNO 1
// Minimum value for local call numbers
#define IAX2_MIN_CALLNO 2

// Outgoing data adjust timestamp defaults
#define IAX2_ADJUSTTSOUT_THRES 120
#define IAX2_ADJUSTTSOUT_OVER 120
#define IAX2_ADJUSTTSOUT_UNDER 60


// Build an MD5 digest from secret, address, integer value and engine run id
// MD5(addr.host() + secret + addr.port() + t)
static void buildSecretDigest(String& buf, const String& secret, unsigned int t,
    const SocketAddr& addr)
{
    String tmp;
    tmp << addr.host() << secret << addr.port() << t;
    MD5 md5(tmp);
    buf << md5.hexDigest();
}


IAXEngine::IAXEngine(const char* iface, int port, u_int32_t format, u_int32_t capab,
    const NamedList* params, const char* name)
    : Mutex(true,"IAXEngine"),
    m_trunking(0),
    m_name(name),
    m_lastGetEvIndex(0),
    m_exiting(false),
    m_maxFullFrameDataLen(1400),
    m_startLocalCallNo(0),
    m_transListCount(64),
    m_challengeTout(IAX2_CHALLENGETOUT_DEF),
    m_callToken(false),
    m_callTokenAge(10),
    m_showCallTokenFailures(false),
    m_printMsg(true),
    m_callerNumType(0),
    m_callingPres(0),
    m_format(format),
    m_formatVideo(0),
    m_capability(capab),
    m_adjustTsOutThreshold(IAX2_ADJUSTTSOUT_THRES),
    m_adjustTsOutOverrun(IAX2_ADJUSTTSOUT_OVER),
    m_adjustTsOutUnderrun(IAX2_ADJUSTTSOUT_UNDER),
    m_mutexTrunk(false,"IAXEngine::Trunk"),
    m_trunkInfoMutex(false,"IAXEngine::TrunkInfo")
{
    debugName(m_name);
    if ((port <= 0) || port > 65535)
	port = 4569;
    bool forceBind = true;
    if (params) {
	m_transListCount = params->getIntValue("translist_count",64,4,256);
	m_maxFullFrameDataLen = params->getIntValue("maxfullframedatalen",1400,20);
	m_callTokenSecret = params->getValue("calltoken_secret");
	forceBind = params->getBoolValue("force_bind",true);
    }
    m_transList = new ObjList*[m_transListCount];
    for (unsigned int i = 0; i < m_transListCount; i++)
	m_transList[i] = new ObjList;
    for(unsigned int i = 0; i <= IAX2_MAX_CALLNO; i++)
	m_lUsedCallNo[i] = false;
    if (!m_callTokenSecret)
	for (unsigned int i = 0; i < 3; i++)
	    m_callTokenSecret << (int)(Random::random() ^ Time::now());
    bind(iface,port,forceBind);
    m_startLocalCallNo = 1 + (u_int16_t)(Random::random() % IAX2_MAX_CALLNO);
    if (m_startLocalCallNo < IAX2_MIN_CALLNO)
	m_startLocalCallNo = IAX2_MIN_CALLNO;
    initialize(params ? *params : NamedList::empty());
}

IAXEngine::~IAXEngine()
{
    for (int i = 0; i < m_transListCount; i++)
	TelEngine::destruct(m_transList[i]);
    delete[] m_transList;
}

IAXTransaction* IAXEngine::addFrame(const SocketAddr& addr, IAXFrame* frame)
{
    if (!frame)
	return 0;
    IAXTransaction* tr = 0;
    ObjList* l = 0;
    Lock lock(this);
    // Transaction exists for this frame?
    // Incomplete transactions. They MUST receive a full frame with destination call number set
    IAXFullFrame* full = frame->fullFrame();
    if (full && full->destCallNo()) {
	l = m_incompleteTransList.skipNull();
	for (; l; l = l->next()) {
	    tr = static_cast<IAXTransaction*>(l->get());
	    if (!(tr && tr->localCallNo() == full->destCallNo() && addr == tr->remoteAddr()))
		continue;
	    // Incomplete outgoing receiving call token
	    if (full->type() == IAXFrame::IAX &&
		full->subclass() == IAXControl::CallToken) {
		RefPointer<IAXTransaction> t = tr;
		lock.drop();
		if (!t)
		    return 0;
		full->updateIEList(true);
		IAXIEList* list = full->ieList();
		DataBlock db;
		if (list)
		    list->getBinary(IAXInfoElement::CALLTOKEN,db);
		t->processCallToken(db);
		t = 0;
		return 0;
	    }
	    // Complete transaction
	    tr->m_rCallNo = frame->sourceCallNo();
	    m_incompleteTransList.remove(tr,false);
	    m_transList[frame->sourceCallNo() % m_transListCount]->append(tr);
	    XDebug(this,DebugAll,"New incomplete outgoing transaction completed (%u,%u) [%p]",
		tr->localCallNo(),tr->remoteCallNo(),this);
	    return tr->processFrame(frame);
	}
    }
    // Complete transactions
    l = m_transList[frame->sourceCallNo() % m_transListCount];
    if (l)
	l = l->skipNull();
    for (; l; l = l->skipNext()) {
	tr = static_cast<IAXTransaction*>(l->get());
	if (tr->remoteCallNo() != frame->sourceCallNo())
	    continue;
	// Mini frame
	if (!full) {
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
	if (full->destCallNo() || addr == tr->remoteAddr()) {
	    // keep transaction referenced but unlock the engine
	    RefPointer<IAXTransaction> t = tr;
	    lock.drop();
	    return t ? t->processFrame(frame) : 0;
	}
    }
    // Frame doesn't belong to an existing transaction
    if (exiting()) {
	sendInval(full,addr);
	return 0;
    }
    // Test if it is a full frame with an IAX control message that needs a new transaction
    if (!full || frame->type() != IAXFrame::IAX) {
	if (full)
	    sendInval(full,addr);
	return 0;
    }
    switch (full->subclass()) {
	case IAXControl::New:
	    if (!checkCallToken(addr,*full))
		return 0;
	    break;
	case IAXControl::RegReq:
	case IAXControl::RegRel:
	case IAXControl::Poke:
	    break;
	case IAXControl::Inval:
	case IAXControl::FwDownl:
	case IAXControl::TxCnt:
	case IAXControl::TxAcc:
	    // These are often used as keepalives
	    return 0;
	default:
	    if (full->destCallNo() == 0)
		Debug(this,DebugAll,
		    "Unsupported incoming transaction Frame(%u,%u). Source call no: %u [%p]",
		    frame->type(),full->subclass(),full->sourceCallNo(),this);
	    else
		Debug(this,DebugAll,"Unmatched Frame(%u,%u) for (%u,%u) [%p]",
		    frame->type(),full->subclass(),full->destCallNo(),
		    full->sourceCallNo(),this);
	    sendInval(full,addr);
	    return 0;
    }
    // Generate local number
    u_int16_t lcn = generateCallNo();
    if (lcn) {
	// Create and add transaction
	tr = IAXTransaction::factoryIn(this,full,lcn,addr);
	if (tr)
	    m_transList[frame->sourceCallNo() % m_transListCount]->append(tr);
	else
	    releaseCallNo(lcn);
    }
    if (!tr)
	Debug(this,DebugInfo,"Failed to build incoming transaction for Frame(%u,%u) [%p]",
	    frame->type(),full->subclass(),this);
    return tr;
}

IAXTransaction* IAXEngine::addFrame(const SocketAddr& addr, const unsigned char* buf, unsigned int len)
{
    IAXFrame* frame = IAXFrame::parse(buf,len,this,&addr);
    if (!frame)
	return 0;
    if (m_printMsg && frame->fullFrame() && debugAt(DebugInfo)) {
	String s;
	SocketAddr local;
	m_socket.getSockName(local);
	frame->fullFrame()->toString(s,local,addr,true);
	Debug(this,DebugInfo,"Received frame [%p]%s",this,s.c_str());
    }
    IAXTransaction* tr = addFrame(addr,frame);
    if (!tr)
	frame->deref();
    return tr;
}

// Find a complete transaction
IAXTransaction* IAXEngine::findTransaction(const SocketAddr& addr, u_int16_t rCallNo)
{
    Lock lck(this);
    ObjList* o = m_transList[rCallNo % m_transListCount]->skipNull();
    for (; o; o = o->skipNext()) {
	IAXTransaction* tr = static_cast<IAXTransaction*>(o->get());
	if (tr->remoteCallNo() == rCallNo && addr == tr->remoteAddr())
	    return tr->ref() ? tr : 0;
    }
    return 0;
}

void IAXEngine::sendInval(IAXFullFrame* frame, const SocketAddr& addr)
{
    if (!frame)
	return;
    // Check for frames that should not receive INVAL
    if (frame->type() == IAXFrame::IAX && frame->subclass() == IAXControl::Inval)
	return;
    DDebug(this,DebugInfo,
	"Sending INVAL for unmatched frame(%u,%u) with OSeq=%u ISeq=%u [%p]",
	frame->type(),frame->subclass(),frame->oSeqNo(),frame->iSeqNo(),this);
    IAXFullFrame* f = new IAXFullFrame(IAXFrame::IAX,IAXControl::Inval,frame->destCallNo(),
	frame->sourceCallNo(),frame->iSeqNo(),frame->oSeqNo(),frame->timeStamp());
    writeSocket(f->data().data(),f->data().length(),addr,f);
    f->deref();
}

bool IAXEngine::process()
{
    bool ok = false;
    for (;;) {
	IAXEvent* event = getEvent();
	if (!event)
	    break;
	ok = true;
	if ((event->final() && !event->frameType()) || !event->getTransaction()) {
	    XDebug(this,DebugAll,"Deleting internal event type %u Frame(%u,%u) [%p]",
		event->type(),event->frameType(),event->subclass(),this);
	    delete event;
	    continue;
	}
	processEvent(event);
    }
    return ok;
}

static inline void roundUp10(unsigned int& value)
{
    unsigned int rest = value % 10;
    if (rest)
	value += 10 - rest;
}

// Initialize outgoing data timestamp adjust values
void IAXEngine::initOutDataAdjust(const NamedList& params, IAXTransaction* tr)
{
    const String* thresS = 0;
    const String* overS = 0;
    const String* underS = 0;
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == YSTRING("adjust_ts_out_threshold"))
	    thresS = ns;
	else if (ns->name() == YSTRING("adjust_ts_out_over"))
	    overS = ns;
	else if (ns->name() == YSTRING("adjust_ts_out_under"))
	    underS = ns;
    }
    // No need to set transaction's data if no parameter found
    if (tr && !(thresS || overS || underS))
	return;
    Lock lck(tr ? (Mutex*)tr : (Mutex*)this);
    unsigned int thresDef = IAX2_ADJUSTTSOUT_THRES;
    unsigned int overDef = IAX2_ADJUSTTSOUT_OVER;
    unsigned int underDef = IAX2_ADJUSTTSOUT_UNDER;
    if (tr) {
	thresDef = tr->m_adjustTsOutThreshold;
	overDef = tr->m_adjustTsOutOverrun;
	underDef = tr->m_adjustTsOutUnderrun;
    }
    unsigned int thres = thresS ? thresS->toInteger(thresDef,0,20,300) : thresDef;
    unsigned int over = overS ? overS->toInteger(overDef,0,10) : overDef;
    unsigned int under = underS ? underS->toInteger(underDef,0,10) : underDef;
    bool adjusted = false;
    // Round down to multiple of 10
    roundUp10(thres);
    roundUp10(over);
    roundUp10(under);
    // Overrun must not be greater then threshold
    if (over > thres) {
	over = thres;
	adjusted = true;
    }
    // Underrun must be less then 2 * threshold
    unsigned int doubleThres = 2 * thres;
    if (under >= doubleThres) {
	under = doubleThres - 10;
	adjusted = true;
    }
    if (tr) {
	tr->m_adjustTsOutThreshold = thres;
	tr->m_adjustTsOutOverrun = over;
	tr->m_adjustTsOutUnderrun = under;
	Debug(this,DebugAll,
	    "Transaction(%u,%u) adjust ts out set to thres=%u over=%u under=%u [%p]",
	    tr->localCallNo(),tr->remoteCallNo(),thres,over,under,tr);
	return;
    }
    m_adjustTsOutThreshold = thres;
    m_adjustTsOutOverrun = over;
    m_adjustTsOutUnderrun = under;
    if (adjusted)
	Debug(this,DebugConf,
	    "Adjust ts out set to thres=%u over=%u under=%u from thres=%s over=%s under=%s [%p]",
	    thres,over,under,TelEngine::c_safe(thresS),
	    TelEngine::c_safe(overS),TelEngine::c_safe(underS),this);
    else
	Debug(this,DebugAll,"Adjust ts out set to thres=%u over=%u under=%u [%p]",
	    thres,over,under,this);
}

// (Re)Initialize the engine
void IAXEngine::initialize(const NamedList& params)
{
    m_callToken = params.getBoolValue("calltoken_in");
    m_callTokenAge = params.getIntValue("calltoken_age",10,1,25);
    m_showCallTokenFailures = params.getBoolValue("calltoken_printfailure");
    m_rejectMissingCallToken = params.getBoolValue("calltoken_rejectmissing",true);
    m_printMsg = params.getBoolValue("printmsg",true);
    m_callerNumType = lookup(params["numtype"],IAXInfoElement::s_typeOfNumber);
    m_callingPres = lookup(params["presentation"],IAXInfoElement::s_presentation) |
	lookup(params["screening"],IAXInfoElement::s_screening);
    m_challengeTout = params.getIntValue("challenge_timeout",
	IAX2_CHALLENGETOUT_DEF,IAX2_CHALLENGETOUT_MIN);
    initOutDataAdjust(params);
    IAXTrunkInfo* ti = new IAXTrunkInfo;
    ti->initTrunking(params,"trunk_");
    ti->init(params);
    Lock lck(m_trunkInfoMutex);
    m_trunkInfoDef = ti;
#ifdef XDEBUG
    String tiS;
    m_trunkInfoDef->dump(tiS,"\r\n");
    Debug(this,DebugAll,"Initialized trunk info defaults: [%p]\r\n-----\r\n%s\r\n-----",
	this,tiS.c_str());
#endif
    TelEngine::destruct(ti);
}

void IAXEngine::readSocket(SocketAddr& addr)
{
    unsigned char buf[1500];

    while (1) {
	if (Thread::check(false))
	    break;
	int len = m_socket.recvFrom(buf,sizeof(buf),addr);
	if (len == Socket::socketError()) {
	    if (!m_socket.canRetry()) {
		String tmp;
		Thread::errorString(tmp,m_socket.error());
		Debug(this,DebugWarn,"Socket read error: %s (%d) [%p]",
		    tmp.c_str(),m_socket.error(),this);
	    }
	    Thread::idle(false);
	    continue;
	}
	addFrame(addr,buf,len);
    }
}

bool IAXEngine::writeSocket(const void* buf, int len, const SocketAddr& addr,
    IAXFullFrame* frame, unsigned int* sent)
{
    if (m_printMsg && frame && debugAt(DebugInfo)) {
	String s;
	SocketAddr local;
	m_socket.getSockName(local);
	frame->toString(s,local,addr,false);
	Debug(this,DebugInfo,"Sending frame [%p]%s",this,s.c_str());
    }
    len = m_socket.sendTo(buf,len,addr);
    if (len == Socket::socketError()) {
	if (!m_socket.canRetry()) {
	    String tmp;
	    Thread::errorString(tmp,m_socket.error());
	    Alarm(this,"socket",DebugWarn,"Socket write error: %s (%d) [%p]",
		tmp.c_str(),m_socket.error(),this);
	}
#ifdef DEBUG
	else {
	    String tmp;
	    Thread::errorString(tmp,m_socket.error());
	    Debug(this,DebugMild,"Socket temporary unavailable: %s (%d) [%p]",
		tmp.c_str(),m_socket.error(),this);
	}
#endif
	return false;
    }
    if (sent)
	*sent = (unsigned int)len;
    return true;
}

void IAXEngine::runGetEvents()
{
    while (1) {
	if (Thread::check(false))
	    break;
	if (!process())
	    Thread::idle(false);
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
	    DDebug(this,DebugAll,"Transaction(%u,%u) removed [%p]",
		transaction->localCallNo(),transaction->remoteCallNo(),this);
	}
	else {
	    DDebug(this,DebugAll,
		"Trying to remove transaction(%u,%u) but does not exist [%p]",
		transaction->localCallNo(),transaction->remoteCallNo(),this);
	}
    }
    else {
	DDebug(this,DebugAll,"Transaction(%u,%u) (incomplete outgoing) removed [%p]",
	    transaction->localCallNo(),transaction->remoteCallNo(),this);
    }
}

// Check if there are any transactions in the engine
bool IAXEngine::haveTransactions()
{
    Lock lock(this);
    // Incomplete transactions
    if (m_incompleteTransList.skipNull())
	return true;
    // Complete transactions
    for (int i = 0; i < m_transListCount; i++)
	if (m_transList[i]->skipNull())
	    return true;
    return false;
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

void IAXEngine::keepAlive(const SocketAddr& addr)
{
#if 0
    unsigned char buf[12] = {0x80,0,0,0,0,0,0,0,0,0,IAXFrame::IAX,IAXControl::Inval};
    writeSocket(buf,sizeof(buf),addr);
#endif
    IAXFullFrame* f = new IAXFullFrame(IAXFrame::IAX,IAXControl::Inval,0,0,0,0,0);
    writeSocket(f->data().data(),f->data().length(),addr,f);
    f->deref();
}

// Decode a DATETIME value
void IAXEngine::decodeDateTime(u_int32_t dt, unsigned int& year, unsigned int& month,
    unsigned int& day, unsigned int& hour, unsigned int& minute, unsigned int& sec)
{
   // RFC 5456 Section 8.6.28
   year = 2000 + ((dt & 0xfe000000) >> 25);
   month = (dt & 0x1e00000) >> 21;
   day = (dt & 0x1f0000) >> 16;
   hour = (dt & 0xf800) >> 11;
   minute = (dt & 0x7e0) >> 5;
   sec = dt & 0x1f;
}

// Calculate overall timeout from interval and retransmission counter
unsigned int IAXEngine::overallTout(unsigned int interval, unsigned int nRetrans)
{
    unsigned int tmp = interval;
    for (unsigned int i = 1; i <= nRetrans; i++)
	tmp += interval * (1 << i);
    return tmp;
}

bool IAXEngine::processTrunkFrames(const Time& time)
{
    Lock lck(m_mutexTrunk);
    bool sent = false;
    for (ObjList* l = m_trunkList.skipNull(); l;) {
	if (Thread::check(false))
	    break;
	IAXMetaTrunkFrame* frame = static_cast<IAXMetaTrunkFrame*>(l->get());
	if (frame->refcount() != 1) {
	    l = l->skipNext();
	    if (frame->timerTick(time))
		sent = true;
	    continue;
	}
	Debug(this,DebugAll,
	    "Removing trunk frame (%p) '%s:%d' timestamps=%s maxlen=%u interval=%ums [%p]",
	    frame,frame->addr().host().c_str(),frame->addr().port(),
	    String::boolText(frame->trunkTimestamps()),frame->maxLen(),
	    frame->sendInterval(),this);
	l->remove();
	l = l->skipNull();
    }
    return sent;
}

void IAXEngine::processEvent(IAXEvent* event)
{
    XDebug(this,DebugAll,"Default processing - deleting event %p Subclass %u [%p]",
	event,event->subclass(),this);
    delete event;
}

IAXEvent* IAXEngine::getEvent(const Time& now)
{
    IAXTransaction* tr;
    IAXEvent* ev;
    ObjList* l;

    lock();
    // Find for incomplete transactions
    l = m_incompleteTransList.skipNull();
    for (; l; l = l->next()) {
	if (Thread::check(false))
	    break;
	tr = static_cast<IAXTransaction*>(l->get());
	if (tr && 0 != (ev = tr->getEvent(now))) {
	    unlock();
	    return ev;
	}
    }
    // Find for complete transactions, start with current index
    while (m_lastGetEvIndex < m_transListCount) {
	if (Thread::check(false))
	    break;
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
	    if (0 != (ev = t->getEvent(now)))
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
	m_startLocalCallNo = IAX2_MIN_CALLNO;
    for (i = m_startLocalCallNo; i <= IAX2_MAX_CALLNO; i++)
	if (!m_lUsedCallNo[i]) {
	    m_lUsedCallNo[i] = true;
	    return i;
	}
    for (i = IAX2_MIN_CALLNO; i < m_startLocalCallNo; i++)
	if (!m_lUsedCallNo[i]) {
	    m_lUsedCallNo[i] = true;
	    return i;
	}
    Debug(this,DebugWarn,"Unable to generate call number. Transaction count: %u [%p]",
	transactionCount(),this);
    return 0;
}

void IAXEngine::releaseCallNo(u_int16_t lcallno)
{
    m_lUsedCallNo[lcallno] = false;
}

IAXTransaction* IAXEngine::startLocalTransaction(IAXTransaction::Type type,
    const SocketAddr& addr, IAXIEList& ieList, bool refTrans, bool startTrans)
{
    Lock lck(this);
    if (exiting())
	return 0;
    u_int16_t lcn = generateCallNo();
    if (!lcn)
	return 0;
    IAXTransaction* tr = IAXTransaction::factoryOut(this,type,lcn,addr,ieList);
    if (tr) {
	if (!refTrans || tr->ref()) {
	    m_incompleteTransList.append(tr);
	    if (startTrans)
		tr->start();
	}
	else
	    TelEngine::destruct(tr);
    }
    if (!tr)
	releaseCallNo(lcn);
    return tr;
}

// Bind the socket. Terminate it before trying
bool IAXEngine::bind(const char* iface, int port, bool force)
{
    if (m_socket.valid())
	m_socket.terminate();
    m_addr.clear();
    if (!m_socket.create(AF_INET,SOCK_DGRAM)) {
	String tmp;
	Thread::errorString(tmp,m_socket.error());
	Alarm(this,"socket",DebugWarn,"Failed to create socket. %d: '%s' [%p]",
	    m_socket.error(),tmp.c_str(),this);
	return false;
    }
    if (!m_socket.setBlocking(false)) {
	String tmp;
	Thread::errorString(tmp,m_socket.error());
	Alarm(this,"socket",DebugWarn,
	    "Failed to set socket non blocking operation mode. %d: '%s' [%p]",
	    m_socket.error(),tmp.c_str(),this);
	m_socket.terminate();
	return false;
    }
    SocketAddr addr(AF_INET);
    addr.host(iface);
    addr.port(port ? port : 4569);
    bool ok = m_socket.bind(addr);
    if (!ok) {
	String tmp;
	Thread::errorString(tmp,m_socket.error());
	Alarm(this,"socket",DebugWarn,"Failed to bind socket on '%s:%d'%s. %d: '%s' [%p]",
	    c_safe(iface),port,force ? " - trying a random port" : "",
	    m_socket.error(),tmp.c_str(),this);
	if (force) {
	    addr.port(0);
	    ok = m_socket.bind(addr);
	    if (!ok)
		Alarm(this,"socket",DebugWarn,"Failed to bind on any port for iface='%s' [%p]",
		    iface,this);
	    else {
		ok = m_socket.getSockName(addr);
		if (!ok)
		    Debug(this,DebugWarn,"Failed to retrieve bound address [%p]",this);
	    }
	}
    }
    if (!ok) {
	m_socket.terminate();
	return false;
    }
    m_addr = addr;
    if (!m_addr.host())
	m_addr.host("0.0.0.0");
    String s;
    if (addr.host() != iface && !TelEngine::null(iface))
	s << " (" << iface << ")";
    Debug(this,DebugInfo,"Bound on '%s:%d'%s [%p]",
	m_addr.host().c_str(),m_addr.port(),s.safe(),this);
    return true;
}

// Check call token on incoming call requests.
bool IAXEngine::checkCallToken(const SocketAddr& addr, IAXFullFrame& frame)
{
    XDebug(this,DebugAll,"IAXEngine::checkCallToken('%s:%d') calltoken=%u [%p]",
	addr.host().c_str(),addr.port(),m_callToken,this);
    if (!m_callToken)
	return true;
    frame.updateIEList(true);
    IAXIEList* list = frame.ieList();
    IAXInfoElementBinary* ct = 0;
    if (list)
	ct = static_cast<IAXInfoElementBinary*>(list->getIE(IAXInfoElement::CALLTOKEN));
    // No call token support
    if (!ct) {
	if (m_showCallTokenFailures)
	    Debug(this,DebugNote,
		"Missing required %s parameter in call request %u from '%s:%d' [%p]",
		IAXInfoElement::ieText(IAXInfoElement::CALLTOKEN),frame.sourceCallNo(),
		addr.host().c_str(),addr.port(),this);
	if (m_rejectMissingCallToken) {
	    IAXIEList* ies = new IAXIEList;
	    ies->appendString(IAXInfoElement::CAUSE,"CALLTOKEN support required");
	    IAXFullFrame* rsp = new IAXFullFrame(IAXFrame::IAX,IAXControl::Reject,
		IAX2_CALLTOKEN_REJ_CALLNO,frame.sourceCallNo(),0,1,2,
		ies,maxFullFrameDataLen());
	    writeSocket(addr,rsp);
	    TelEngine::destruct(rsp);
	}
	return false;
    }
    // Request with call token
    if (ct->data().length()) {
	String tmp((char*)ct->data().data(),ct->data().length());
	int age = addrSecretAge(tmp,m_callTokenSecret,addr);
	XDebug(this,DebugAll,"Call request %u from '%s:%d' with call token age=%d [%p]",
	    frame.sourceCallNo(),addr.host().c_str(),addr.port(),age,this);
	if (age >= 0 && age <= m_callTokenAge)
	    return true;
	if (m_showCallTokenFailures)
	    Debug(this,DebugNote,
		"Ignoring call request %u from '%s:%d' with %s call token age=%d [%p]",
		frame.sourceCallNo(),addr.host().c_str(),addr.port(),
		(age > 0) ? "old" : "invalid",age,this);
	return false;
    }
    // Request with empty call token: send one
    String tmp;
    buildAddrSecret(tmp,m_callTokenSecret,addr);
    IAXIEList* ies = new IAXIEList;
    ies->appendBinary(IAXInfoElement::CALLTOKEN,(unsigned char*)tmp.c_str(),tmp.length());
    IAXFullFrame* rsp = new IAXFullFrame(IAXFrame::IAX,IAXControl::CallToken,
	IAX2_CALLTOKEN_CALLNO,frame.sourceCallNo(),0,1,1,ies,maxFullFrameDataLen());
    writeSocket(addr,rsp);
    TelEngine::destruct(rsp);
    return false;
}

bool IAXEngine::acceptFormatAndCapability(IAXTransaction* trans, unsigned int* caps, int type)
{
    if (!trans)
	return false;
    u_int32_t transCapsNonType = IAXFormat::clear(trans->m_capability,type);
    IAXFormat* fmt = trans->getFormat(type);
    if (!fmt) {
	DDebug(this,DebugStub,"acceptFormatAndCapability() No media %s in transaction [%p]",
	    IAXFormat::typeName(type),this);
	trans->m_capability = transCapsNonType;
	return false;
    }
    u_int32_t transCapsType = IAXFormat::mask(trans->m_capability,type);
    u_int32_t capability = transCapsType & m_capability;
    if (caps)
	capability &= IAXFormat::mask(*caps,type);
    trans->m_capability = transCapsNonType | capability;
    XDebug(this,DebugAll,
	"acceptFormatAndCapability trans(%u,%u) type=%s caps(trans/our/param/result)=%u/%u/%u/%u [%p]",
	trans->localCallNo(),trans->remoteCallNo(),
	fmt->typeName(),transCapsType,IAXFormat::mask(m_capability,type),
	caps ? IAXFormat::mask(*caps,type) : 0,capability,this);
    // Valid capability ?
    if (!capability) {
	// Warn if we should have media
	if (type == IAXFormat::Audio || 0 != (trans->outgoing() ? fmt->in() : fmt->out()))
	    Debug(this,DebugNote,"Transaction(%u,%u) no common format(s) for media '%s' [%p]",
		trans->localCallNo(),trans->remoteCallNo(),fmt->typeName(),trans);
	// capability is 0, use it to set format
	if (trans->outgoing())
	    fmt->set(&capability,&capability,0);
	else
	    fmt->set(&capability,0,&capability);
	return false;
    }
    u_int32_t format = fmt->format();
    // Received format is valid ?
    if (0 == (format & capability)) {
	format = (type == IAXFormat::Audio) ? m_format : 0;
	format = IAXFormat::pickFormat(capability,format);
    }
    if (format) {
	fmt->set(&format,&format,&format);
	Debug(this,DebugAll,"Transaction(%u,%u) set format %u (%s) for media '%s' [%p]",
	    trans->localCallNo(),trans->remoteCallNo(),format,fmt->formatName(),
	    fmt->typeName(),trans);
    }
    else
	Debug(this,DebugNote,
	    "Transaction(%u,%u) failed to choose a common format for media '%s' [%p]",
	    trans->localCallNo(),trans->remoteCallNo(),fmt->typeName(),trans);
    return format != 0;
}

void IAXEngine::defaultEventHandler(IAXEvent* event)
{
    DDebug(this,DebugAll,
	"defaultEventHandler - Event type: %u. Frame - Type: %u Subclass: %u [%p]",
	event->type(),event->frameType(),event->subclass(),this);
    IAXTransaction* tr = event->getTransaction();
    switch (event->type()) {
	case IAXEvent::New:
	    tr->sendReject("Feature not implemented or unsupported");
	    break;
	default: ;
    }
}

// Set the exiting flag
void IAXEngine::setExiting()
{
    Lock lck(this);
    m_exiting = true;
}

static bool getTrunkingInfo(RefPointer<IAXTrunkInfo>& ti, IAXEngine* engine,
    const NamedList* params, const String& prefix, bool out)
{
    if (!engine->trunkInfo(ti))
	return false;
    if (!params)
	return true;
    IAXTrunkInfo* tmp = new IAXTrunkInfo;
    tmp->initTrunking(*params,prefix,ti,out,!out);
    ti = tmp;
    TelEngine::destruct(tmp);
    return true;
}

void IAXEngine::enableTrunking(IAXTransaction* trans, const NamedList* params,
    const String& prefix)
{
    if (!trans || trans->type() != IAXTransaction::New)
	return;
    RefPointer<IAXTrunkInfo> ti;
    if (getTrunkingInfo(ti,this,params,prefix,true))
	enableTrunking(trans,*ti);
    ti = 0;
}

// Enable trunking for the given transaction. Allocate a trunk meta frame if needed.
void IAXEngine::enableTrunking(IAXTransaction* trans, IAXTrunkInfo& data)
{
    if (!trans || trans->type() != IAXTransaction::New)
	return;
    Lock lock(m_mutexTrunk);
    if (m_trunking >= 0) {
	m_trunking++;
	if (m_trunking == 1 || 0 == ((m_trunking - 1) % 200))
	    Debug(this,DebugNote,"Failed to enable trunking: not available [%p]",this);
	return;
    }
    IAXMetaTrunkFrame* frame;
    // Already enabled ?
    for (ObjList* l = m_trunkList.skipNull(); l; l = l->skipNext()) {
	frame = static_cast<IAXMetaTrunkFrame*>(l->get());
	if (frame->addr() == trans->remoteAddr()) {
	    trans->enableTrunking(frame,data.m_efficientUse);
	    return;
	}
    }
    frame = new IAXMetaTrunkFrame(this,trans->remoteAddr(),data.m_timestamps,
	data.m_maxLen,data.m_sendInterval);
    if (trans->enableTrunking(frame,data.m_efficientUse)) {
	m_trunkList.append(frame);
	Debug(this,DebugAll,
	    "Added trunk frame (%p) '%s:%d' timestamps=%s maxlen=%u interval=%ums [%p]",
	    frame,frame->addr().host().c_str(),frame->addr().port(),
	    String::boolText(frame->trunkTimestamps()),frame->maxLen(),
	    frame->sendInterval(),this);
    }
    else
	TelEngine::destruct(frame);
}

// Init incoming trunking data for a given transaction
void IAXEngine::initTrunkIn(IAXTransaction* trans, const NamedList* params,
    const String& prefix)
{
    if (!trans)
	return;
    RefPointer<IAXTrunkInfo> ti;
    if (getTrunkingInfo(ti,this,params,prefix,false))
	initTrunkIn(trans,*ti);
    ti = 0;
}

// Init incoming trunking data for a given transaction
void IAXEngine::initTrunkIn(IAXTransaction* trans, IAXTrunkInfo& data)
{
    if (!trans)
	return;
    trans->m_trunkInSyncUsingTs = data.m_trunkInSyncUsingTs;
    trans->m_trunkInTsDiffRestart = data.m_trunkInTsDiffRestart;
#ifdef XDEBUG
    String tmp;
    data.dump(tmp," ",false,true,false);
    Debug(this,DebugAll,"initTrunkIn(%p) callno=%u set %s [%p]",
	trans,trans->localCallNo(),tmp.c_str(),this);
#endif
}

void IAXEngine::runProcessTrunkFrames()
{
    while (1) {
	if (Thread::check(false))
	    break;
	processTrunkFrames();
	Thread::msleep(2,false);
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

// Build a time signed secret used to authenticate an IP address
void IAXEngine::buildAddrSecret(String& buf, const String& secret, const SocketAddr& addr)
{
    unsigned int t = Time::secNow();
    buildSecretDigest(buf,secret,t,addr);
    buf << "." << t;
}

// Decode a secret built using buildAddrSecret()
int IAXEngine::addrSecretAge(const String& buf, const String& secret, const SocketAddr& addr)
{
    int pos = buf.find('.');
    if (pos < 1)
	return -1;
    int t = buf.substr(pos + 1).toInteger();
    String tmp;
    buildSecretDigest(tmp,secret,t,addr);
    return (tmp == buf.substr(0,pos)) ? ((int)Time::secNow() - t) : -1;
}

/*
* IAXEvent
*/
IAXEvent::IAXEvent(Type type, bool local, bool final, IAXTransaction* transaction, u_int8_t frameType, u_int32_t subclass)
    : m_type(type), m_frameType(frameType), m_subClass(subclass),
    m_local(local), m_final(final), m_transaction(0), m_ieList(0)
{
    if (transaction && transaction->ref())
	m_transaction = transaction;
    m_ieList = new IAXIEList;
}

IAXEvent::IAXEvent(Type type, bool local, bool final, IAXTransaction* transaction, IAXFullFrame* frame)
    : m_type(type), m_frameType(0), m_subClass(0), m_local(local),
    m_final(final), m_transaction(0), m_ieList(0)
{
    if (transaction && transaction->ref())
	m_transaction = transaction;
    if (frame) {
	m_frameType = frame->type();
	m_subClass = frame->subclass();
	frame->updateIEList(true);
	m_ieList = frame->removeIEList(false);
    }
    if (!m_ieList)
	m_ieList = new IAXIEList;
}

IAXEvent::~IAXEvent()
{
    if (m_final && m_transaction && m_transaction->state() == IAXTransaction::Terminated)
	m_transaction->getEngine()->removeTransaction(m_transaction);
    if (m_transaction) {
	m_transaction->eventTerminated(this);
	m_transaction->deref();
    }
    if (m_ieList)
	delete m_ieList;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
