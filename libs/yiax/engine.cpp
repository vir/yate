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

// Local call number to set when rejecting calls with missing call token
#define IAX2_CALLTOKEN_REJ_CALLNO 1
// Local call number to set when sending call token message
#define IAX2_CALLTOKEN_CALLNO 1
// Minimum value for local call numbers
#define IAX2_MIN_CALLNO 2

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


IAXEngine::IAXEngine(const char* iface, int port, u_int16_t transListCount, u_int16_t retransCount, u_int16_t retransInterval,
	u_int16_t authTimeout, u_int16_t transTimeout, u_int16_t maxFullFrameDataLen,
	u_int32_t format, u_int32_t capab, u_int32_t trunkSendInterval, bool authRequired,
	NamedList* params)
    : Mutex(true,"IAXEngine"),
    m_lastGetEvIndex(0),
    m_authRequired(authRequired),
    m_maxFullFrameDataLen(maxFullFrameDataLen),
    m_startLocalCallNo(0),
    m_transListCount(0),
    m_retransCount(retransCount),
    m_retransInterval(retransInterval),
    m_authTimeout(authTimeout),
    m_transTimeout(transTimeout),
    m_callToken(false),
    m_callTokenAge(10),
    m_showCallTokenFailures(false),
    m_printMsg(true),
    m_format(format),
    m_formatVideo(0),
    m_capability(capab),
    m_mutexTrunk(true,"IAXEngine::Trunk"),
    m_trunkSendInterval(trunkSendInterval)
{
    debugName("iaxengine");
    Debug(this,DebugAll,"Automatically request authentication set to '%s'.",
	(authRequired?"YES":"NO"));
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
    if (params)
	m_callTokenSecret = params->getValue("calltoken_secret");
    if (!m_callTokenSecret)
	for (i = 0; i < 3; i++)
	    m_callTokenSecret << (int)(Random::random() ^ Time::now());
    m_socket.create(AF_INET,SOCK_DGRAM);
    SocketAddr addr(AF_INET);
    addr.host(iface);
    addr.port(port);
    m_socket.setBlocking(false);
    bool ok = m_socket.bind(addr);
    if (!ok) {
	bool force = !params || params->getBoolValue("force_bind",true);
	String tmp;
	Thread::errorString(tmp,m_socket.error());
	Debug(this,DebugWarn,"Failed to bind socket on '%s:%d'%s. %d: '%s'",
	    c_safe(iface),port,force ? " - trying a random port" : "",
	    m_socket.error(),tmp.c_str());
	if (force) {
	    addr.port(0);
	    ok = m_socket.bind(addr);
	    if (!ok)
		Debug(this,DebugWarn,"Failed to bind on any port");
	    else {
		ok = m_socket.getSockName(addr);
		if (!ok)
		    Debug(this,DebugWarn,"Failed to retrieve bound address");
	    }
	}
    }
    if (ok)
	Debug(this,DebugInfo,"Bound on '%s:%d'",addr.host().c_str(),addr.port());
    m_startLocalCallNo = 1 + (u_int16_t)(Random::random() % IAX2_MAX_CALLNO);
    if (m_startLocalCallNo < IAX2_MIN_CALLNO)
	m_startLocalCallNo = IAX2_MIN_CALLNO;
    if (params)
	initialize(*params);
    else
	initialize(NamedList::empty());
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
    IAXTransaction* tr;
    ObjList* l;
    Lock lock(this);
    // Transaction exists for this frame?
    // Incomplete transactions. They MUST receive a full frame
    IAXFullFrame* fullFrame = frame->fullFrame();
    if (fullFrame) {
	l = m_incompleteTransList.skipNull();
	for (; l; l = l->next()) {
	    tr = static_cast<IAXTransaction*>(l->get());
	    if (!(tr && tr->localCallNo() == fullFrame->destCallNo() && addr == tr->remoteAddr()))
		continue;
	    // Incomplete outgoing receiving call token
	    if (fullFrame->type() == IAXFrame::IAX &&
		fullFrame->subclass() == IAXControl::CallToken) {
		RefPointer<IAXTransaction> t = tr;
		lock.drop();
		if (!t)
		    return 0;
		fullFrame->updateIEList(true);
		IAXIEList* list = fullFrame->ieList();
		DataBlock db;
		if (list)
		    list->getBinary(IAXInfoElement::CALLTOKEN,db);
		t->processCallToken(db);
		t = 0;
		return 0;
	    }
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
	if (!fullFrame) {
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
	if (fullFrame->destCallNo() || addr == tr->remoteAddr()) {
	    // keep transaction referenced but unlock the engine
	    RefPointer<IAXTransaction> t = tr;
	    lock.drop();
	    return t ? t->processFrame(frame) : 0;
	}
    }
    // Frame doesn't belong to an existing transaction
    // Test if it is a full frame with an IAX control message that needs a new transaction
    if (!fullFrame || frame->type() != IAXFrame::IAX)
	return 0;
    switch (fullFrame->subclass()) {
	case IAXControl::New:
	     if (!checkCallToken(addr,*fullFrame))
		return 0;
	case IAXControl::RegReq:
	case IAXControl::RegRel:
	case IAXControl::Poke:
	    break;
	case IAXControl::Inval:
	    // These are often used as keepalives
	    return 0;
	case IAXControl::FwDownl:
	default:
#ifdef DEBUG
	    if (fullFrame) {
	        if (fullFrame->destCallNo() == 0)
		    Debug(this,DebugAll,"Unsupported incoming transaction Frame(%u,%u). Source call no: %u",
			frame->type(),fullFrame->subclass(),fullFrame->sourceCallNo());
#ifdef XDEBUG
		else
		    Debug(this,DebugAll,"Unmatched Frame(%u,%u) for (%u,%u)",
			frame->type(),fullFrame->subclass(),fullFrame->destCallNo(),fullFrame->sourceCallNo());
#endif
	    }
#endif
	    return 0;
    }
    // Generate local number
    u_int16_t lcn = generateCallNo();
    if (!lcn)
	return 0;
    // Create and add transaction
    tr = IAXTransaction::factoryIn(this,fullFrame,lcn,addr);
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
    if (m_printMsg && frame->fullFrame() && debugAt(DebugInfo)) {
	String s;
	SocketAddr local;
	m_socket.getSockName(local);
	frame->fullFrame()->toString(s,local,addr,true);
	Debug(this,DebugInfo,"Received frame.%s",s.c_str());
    }
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

// (Re)Initialize the engine
void IAXEngine::initialize(const NamedList& params)
{
    m_callToken = params.getBoolValue("calltoken_in");
    int callTokenAge = params.getIntValue("calltoken_age",10);
    if (callTokenAge > 1 && callTokenAge < 25)
	m_callTokenAge = callTokenAge;
    else
	m_callTokenAge = 10;
    m_showCallTokenFailures = params.getBoolValue("calltoken_printfailure");
    m_rejectMissingCallToken = params.getBoolValue("calltoken_rejectmissing",true);
    m_printMsg = params.getBoolValue("printmsg",true);
}

void IAXEngine::readSocket(SocketAddr& addr)
{
    unsigned char buf[1500];

    while (1) {
	int len = m_socket.recvFrom(buf,sizeof(buf),addr);
	if (len == Socket::socketError()) {
	    if (!m_socket.canRetry()) {
		String tmp;
		Thread::errorString(tmp,m_socket.error());
		Debug(this,DebugWarn,"Socket read error: %s (%d)",
		    tmp.c_str(),m_socket.error());
	    }
	    Thread::idle(true);
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
	Debug(this,DebugInfo,"Sending frame.%s",s.c_str());
    }
    len = m_socket.sendTo(buf,len,addr);
    if (len == Socket::socketError()) {
	if (!m_socket.canRetry()) {
	    String tmp;
	    Thread::errorString(tmp,m_socket.error());
	    Debug(this,DebugWarn,"Socket write error: %s (%d)",
		tmp.c_str(),m_socket.error());
	}
#ifdef DEBUG
	else {
	    String tmp;
	    Thread::errorString(tmp,m_socket.error());
	    Debug(this,DebugMild,"Socket temporary unavailable: %s (%d)",
		tmp.c_str(),m_socket.error());
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
	if (!process()) {
	    Thread::idle(true);
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
#if 0
    unsigned char buf[12] = {0x80,0,0,0,0,0,0,0,0,0,IAXFrame::IAX,IAXControl::Inval};
    writeSocket(buf,sizeof(buf),addr);
#endif
    IAXFullFrame* f = new IAXFullFrame(IAXFrame::IAX,IAXControl::Inval,0,0,0,0,0);
    writeSocket(f->data().data(),f->data().length(),addr,f);
    f->deref();
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

// Check call token on incoming call requests.
bool IAXEngine::checkCallToken(const SocketAddr& addr, IAXFullFrame& frame)
{
    XDebug(this,DebugAll,"IAXEngine::checkCallToken('%s:%d') calltoken=%u",
	addr.host().c_str(),addr.port(),m_callToken);
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
		"Missing required %s parameter in call request %u from '%s:%d'",
		IAXInfoElement::ieText(IAXInfoElement::CALLTOKEN),frame.sourceCallNo(),
		addr.host().c_str(),addr.port());
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
	XDebug(this,DebugAll,"Call request %u from '%s:%d' with call token age=%d",
	    frame.sourceCallNo(),addr.host().c_str(),addr.port(),age);
	if (age >= 0 && age <= m_callTokenAge)
	    return true;
	if (m_showCallTokenFailures)
	    Debug(this,DebugNote,
		"Ignoring call request %u from '%s:%d' with %s call token age=%d",
		frame.sourceCallNo(),addr.host().c_str(),addr.port(),
		(age > 0) ? "old" : "invalid",age);
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
	DDebug(this,DebugStub,"acceptFormatAndCapability() No media %s in transaction",
	    IAXFormat::typeName(type));
	trans->m_capability = transCapsNonType;
	return false;
    }
    u_int32_t transCapsType = IAXFormat::mask(trans->m_capability,type);
    u_int32_t capability = transCapsType & m_capability;
    if (caps)
	capability &= IAXFormat::mask(*caps,type);
    trans->m_capability = transCapsNonType | capability;
    XDebug(this,DebugAll,
	"acceptFormatAndCapability trans(%u,%u) type=%s caps(trans/our/param/result)=%u/%u/%u/%u",
	trans->localCallNo(),trans->remoteCallNo(),
	fmt->typeName(),transCapsType,IAXFormat::mask(m_capability,type),
	caps ? IAXFormat::mask(*caps,type) : 0,capability);
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
