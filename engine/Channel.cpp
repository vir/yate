/**
 * Channel.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatephone.h"

#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

// Find if a string appears to be an E164 phone number
bool TelEngine::isE164(const char* str)
{
    if (!str)
	return false;
    // an initial + character is ok, we skip it
    if (*str == '+')
	str++;
    // at least one valid character is required
    if (!*str)
	return false;
    for (;;) {
	switch (*str++) {
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
	    case '*':
	    case '#':
		break;
	    case '\0':
		return true;
	    default:
		return false;
	}
    }
}

static unsigned int s_callid = 0;
static Mutex s_callidMutex(false,"CallID");

// this is to protect against two threads trying to (dis)connect a pair
//  of call endpoints at the same time
static Mutex s_mutex(true,"CallEndpoint");
static Mutex s_lastMutex(false,"CallEndpoint::last");
static const String s_audioType = "audio";
static const String s_copyParams = "copyparams";

// Check if a Lock taken on the common mutex succeeded, wait up to 55s more in congestion
static bool checkRetry(Lock& lock)
{
    if (lock.locked())
	return true;
    Engine::setCongestion("Call endpoint mutex busy");
    bool ok = lock.acquire(s_mutex,55000000);
    Engine::setCongestion();
    return ok;
}


CallEndpoint::CallEndpoint(const char* id)
    : m_peer(0), m_lastPeer(0), m_id(id), m_mutex(0)
{
}

void CallEndpoint::destroyed()
{
#ifdef DEBUG
    ObjList* l = m_data.skipNull();
    for (; l; l=l->skipNext()) {
	DataEndpoint* e = static_cast<DataEndpoint*>(l->get());
	Debug(DebugAll,"Endpoint at %p type '%s' refcount=%d",e,e->name().c_str(),e->refcount());
    }
#endif
    disconnect(true,0,true,0);
    clearEndpoint();
    m_lastPeer = 0;
}

Mutex& CallEndpoint::commonMutex()
{
    return s_mutex;
}

void* CallEndpoint::getObject(const String& name) const
{
    if (name == YATOM("CallEndpoint"))
	return const_cast<CallEndpoint*>(this);
    return RefObject::getObject(name);
}

void CallEndpoint::setId(const char* newId)
{
    m_id = newId;
}

bool CallEndpoint::connect(CallEndpoint* peer, const char* reason, bool notify)
{
    if (!peer) {
	disconnect(reason,notify);
	return false;
    }
    if (peer == m_peer)
	return true;
    if (peer == this) {
	Debug(DebugWarn,"CallEndpoint '%s' trying to connect to itself! [%p]",m_id.c_str(),this);
	return false;
    }
    DDebug(DebugAll,"CallEndpoint '%s' connecting peer %p to [%p]",m_id.c_str(),peer,this);

#if 0
    Lock lock(s_mutex,5000000);
    if (!checkRetry(lock)) {
	Alarm("engine","bug",DebugFail,"Call connect failed - timeout on call endpoint mutex owned by '%s'!",s_mutex.owner());
	Engine::restart(0);
	return false;
    }
#endif

    // are we already dead?
    if (!ref())
	return false;
    disconnect(reason,notify);
    // is our intended peer dead?
    if (!peer->ref()) {
	deref();
	return false;
    }
    peer->disconnect(reason,notify);

    ObjList* l = m_data.skipNull();
    for (; l; l=l->skipNext()) {
	DataEndpoint* e = static_cast<DataEndpoint*>(l->get());
	e->connect(peer->getEndpoint(e->name()));
    }

    m_peer = peer;
    peer->setPeer(this,reason,notify);
    setDisconnect(0);
    connected(reason);

    return true;
}

bool CallEndpoint::disconnect(bool final, const char* reason, bool notify, const NamedList* params)
{
    if (!m_peer)
	return false;
    DDebug(DebugAll,"CallEndpoint '%s' disconnecting peer %p from [%p]",m_id.c_str(),m_peer,this);

    Lock lock(s_mutex,5000000);
    if (!checkRetry(lock)) {
	Alarm("engine","bug",DebugFail,"Call disconnect failed - timeout on call endpoint mutex owned by '%s'!",s_mutex.owner());
	Engine::restart(0);
	return false;
    }

    CallEndpoint *temp = m_peer;
    m_peer = 0;
    m_lastPeer = 0;
    if (!temp)
	return false;

    ObjList* l = m_data.skipNull();
    for (; l; l=l->skipNext()) {
	DataEndpoint* e = static_cast<DataEndpoint*>(l->get());
	DDebug(DebugAll,"Endpoint at %p type '%s' peer %p",e,e->name().c_str(),e->getPeer());
	e->disconnect();
    }

    temp->setPeer(0,reason,notify,params);
    bool dead = !alive();
    if (dead)
	Debug(DebugMild,"CallEndpoint '%s' disconnect called while dead [%p]",m_id.c_str(),this);
    if (final)
	disconnected(true,reason);
    lock.drop();
    temp->deref();
    return dead || deref();
}

void CallEndpoint::setPeer(CallEndpoint* peer, const char* reason, bool notify, const NamedList* params)
{
    m_peer = peer;
    if (m_peer) {
	setDisconnect(0);
	connected(reason);
    }
    else {
	m_lastPeer = 0;
	if (notify) {
	    setDisconnect(params);
	    disconnected(false,reason);
	}
    }
}

bool CallEndpoint::getPeerId(String& id) const
{
    id.clear();
    if (!m_peer)
	return false;
    if (m_peer == m_lastPeer) {
	Lock mylock(s_lastMutex);
	if (m_peer == m_lastPeer) {
	    id = m_lastPeerId;
	    return !id.null();
	}
    }
    Lock lock(s_mutex,5000000);
    if (!checkRetry(lock)) {
	Alarm("engine","bug",DebugFail,"Peer ID failed - timeout on call endpoint mutex owned by '%s'!",s_mutex.owner());
	Engine::restart(0);
	return false;
    }
    if (m_peer) {
	id = m_peer->id();
	return true;
    }
    else
	return false;
}

String CallEndpoint::getPeerId() const
{
    String id;
    getPeerId(id);
    return id;
}

bool CallEndpoint::getLastPeerId(String& id) const
{
    id.clear();
    if (m_lastPeerId.null())
	return false;
    s_lastMutex.lock();
    id = m_lastPeerId;
    s_lastMutex.unlock();
    return !id.null();
}

void CallEndpoint::setLastPeerId()
{
    if (!m_peer)
	return;
    if (m_peer == m_lastPeer)
	return;
    Lock lock(s_mutex,5000000);
    if (!checkRetry(lock)) {
	Alarm("engine","bug",DebugGoOn,"Set last peer ID failed - timeout on call endpoint mutex owned by '%s'!",s_mutex.owner());
	return;
    }
    if (m_peer) {
	s_lastMutex.lock();
	m_lastPeer = m_peer;
	m_lastPeerId = m_peer->id();
	s_lastMutex.unlock();
    }
}

DataEndpoint* CallEndpoint::getEndpoint(const String& type) const
{
    if (type.null())
	return 0;
    const ObjList* pos = m_data.find(type);
    return pos ? static_cast<DataEndpoint*>(pos->get()) : 0;
}

DataEndpoint* CallEndpoint::setEndpoint(const String& type)
{
    if (type.null())
	return 0;
    DataEndpoint* dat = getEndpoint(type);
    if (!dat) {
	dat = new DataEndpoint(this,type);
	if (m_peer)
	    dat->connect(m_peer->getEndpoint(type));
    }
    return dat;
}

void CallEndpoint::setEndpoint(DataEndpoint* endPoint)
{
    if (!(endPoint && endPoint->ref()))
	return;
    if (m_data.find(endPoint)) {
	endPoint->deref();
	return;
    }
    clearEndpoint(endPoint->toString());
    endPoint->disconnect();
    m_data.append(endPoint);
    if (m_peer)
	endPoint->connect(m_peer->getEndpoint(endPoint->toString()));
}

void CallEndpoint::clearEndpoint(const String& type)
{
    if (type.null()) {
	ObjList* l = m_data.skipNull();
	for (; l; l=l->skipNext()) {
	    DataEndpoint* e = static_cast<DataEndpoint*>(l->get());
	    DDebug(DebugAll,"Endpoint at %p type '%s' peer %p",e,e->name().c_str(),e->getPeer());
	    e->disconnect();
	    e->clearCall(this);
	}
	m_data.clear();
    }
    else {
	DataEndpoint* dat = getEndpoint(type);
	if (dat) {
	    m_data.remove(dat,false);
	    dat->disconnect();
	    dat->clearCall(this);
	    dat->destruct();
	}
    }
}

void CallEndpoint::setSource(DataSource* source, const String& type)
{
    DataEndpoint* dat = source ? setEndpoint(type) : getEndpoint(type);
    if (dat)
	dat->setSource(source);
}

DataSource* CallEndpoint::getSource(const String& type) const
{
    DataEndpoint* dat = getEndpoint(type);
    return dat ? dat->getSource() : 0;
}

void CallEndpoint::setConsumer(DataConsumer* consumer, const String& type)
{
    DataEndpoint* dat = consumer ? setEndpoint(type) : getEndpoint(type);
    if (dat)
	dat->setConsumer(consumer);
}

DataConsumer* CallEndpoint::getConsumer(const String& type) const
{
    DataEndpoint* dat = getEndpoint(type);
    return dat ? dat->getConsumer() : 0;
}

bool CallEndpoint::clearData(DataNode* node, const String& type)
{
    if (type.null() || !node)
	return false;
    Lock mylock(DataEndpoint::commonMutex());
    RefPointer<DataEndpoint> dat = getEndpoint(type);
    return dat && dat->clearData(node);
}

const String& CallEndpoint::audioType()
{
    return s_audioType;
}


static const String s_disconnected("chan.disconnected");

// Mutex used to lock disconnect parameters during access
static Mutex s_paramMutex(true,"ChannelParams");

Channel::Channel(Driver* driver, const char* id, bool outgoing)
    : CallEndpoint(id),
      m_parameters(""), m_driver(driver), m_outgoing(outgoing),
      m_timeout(0), m_maxcall(0), m_maxPDD(0), m_dtmfTime(0),
      m_toutAns(0), m_dtmfSeq(0), m_answered(false)
{
    init();
}

Channel::Channel(Driver& driver, const char* id, bool outgoing)
    : CallEndpoint(id),
      m_parameters(""), m_driver(&driver), m_outgoing(outgoing),
      m_timeout(0), m_maxcall(0), m_maxPDD(0), m_dtmfTime(0),
      m_toutAns(0), m_dtmfSeq(0), m_answered(false)
{
    init();
}

Channel::~Channel()
{
#ifdef DEBUG
    Debugger debug(DebugAll,"Channel::~Channel()"," '%s' [%p]",id().c_str(),this);
#endif
    cleanup();
}

void* Channel::getObject(const String& name) const
{
    if (name == YATOM("Channel"))
	return const_cast<Channel*>(this);
    if (name == YATOM("MessageNotifier"))
	return static_cast<MessageNotifier*>(const_cast<Channel*>(this));
    return CallEndpoint::getObject(name);
}

Mutex& Channel::paramMutex()
{
    return s_paramMutex;
}

void Channel::init()
{
    status(direction());
    m_mutex = m_driver;
    if (m_driver) {
	m_driver->lock();
	debugName(m_driver->debugName());
	debugChain(m_driver);
	if (id().null()) {
	    String tmp(m_driver->prefix());
	    tmp << m_driver->nextid();
	    setId(tmp);
	}
	m_driver->unlock();
    }
    // assign a new billid only to incoming calls
    if (m_billid.null() && !m_outgoing)
	m_billid << Engine::runId() << "-" << allocId();
    DDebug(this,DebugInfo,"Channel::init() '%s' [%p]",id().c_str(),this);
}

void Channel::cleanup()
{
    m_timeout = 0;
    m_maxcall = 0;
    m_maxPDD = 0;
    status("deleted");
    m_targetid.clear();
    dropChan();
    m_driver = 0;
    m_mutex = 0;
}

void Channel::filterDebug(const String& item)
{
    if (m_driver) {
	if (m_driver->filterInstalled())
	    debugEnabled(m_driver->filterDebug(item));
	else
	    debugChain(m_driver);
    }
}

void Channel::initChan()
{
    if (!m_driver)
	return;
    Lock mylock(m_driver);
#ifndef NDEBUG
    if (m_driver->channels().find(this)) {
	Debug(DebugGoOn,"Channel '%s' already in list of '%s' driver [%p]",
	    id().c_str(),m_driver->name().c_str(),this);
	return;
    }
#endif
    m_driver->m_total++;
    m_driver->m_chanCount++;
    m_driver->channels().append(this);
    m_driver->changed();
}

void Channel::dropChan()
{
    if (!m_driver)
	return;
    m_driver->lock();
    if (!m_driver)
	Debug(DebugFail,"Driver lost in dropChan! [%p]",this);
    if (m_driver->channels().remove(this,false)) {
	if (m_driver->m_chanCount > 0)
	    m_driver->m_chanCount--;
	m_driver->changed();
    }
    m_driver->unlock();
}

void Channel::zeroRefs()
{
    // remove us from driver's list before calling the destructor
    dropChan();
    CallEndpoint::zeroRefs();
}

void Channel::connected(const char* reason)
{
    CallEndpoint::connected(reason);
    if (m_billid.null()) {
	Channel* peer = YOBJECT(Channel,getPeer());
	if (peer && peer->billid())
	    m_billid = peer->billid();
    }
    Message* m = message("chan.connected",false,true);
    setLastPeerId();
    if (reason)
	m->setParam("reason",reason);
    if (!Engine::enqueue(m))
	TelEngine::destruct(m);
}

void Channel::disconnected(bool final, const char* reason)
{
    if (final || Engine::exiting())
	return;
    // last chance to get reconnected to something
    Message* m = getDisconnect(reason);
    s_paramMutex.lock();
    m_targetid.clear();
    m_parameters.clearParams();
    s_paramMutex.unlock();
    Engine::enqueue(m);
}

void Channel::setDisconnect(const NamedList* params)
{
    DDebug(this,DebugInfo,"setDisconnect(%p) [%p]",params,this);
    s_paramMutex.lock();
    m_parameters.clearParams();
    if (params)
	m_parameters.copyParams(*params);
    s_paramMutex.unlock();
}

void Channel::endDisconnect(const Message& msg, bool handled)
{
}

void Channel::dispatched(const Message& msg, bool handled)
{
    if (s_disconnected == msg)
	endDisconnect(msg,handled);
}

void Channel::setId(const char* newId)
{
    debugName(0);
    CallEndpoint::setId(newId);
    debugName(id());
}

Message* Channel::getDisconnect(const char* reason)
{
    Message* msg = new Message(s_disconnected);
    s_paramMutex.lock();
    msg->copyParams(m_parameters);
    s_paramMutex.unlock();
    complete(*msg);
    if (reason)
	msg->setParam("reason",reason);
    // we will remain referenced until the message is destroyed
    msg->userData(this);
    msg->setNotify();
    return msg;
}

void Channel::status(const char* newstat)
{
    Lock lock(mutex());
    m_status = newstat;
    if (!m_answered && (m_status == YSTRING("answered"))) {
	m_answered = true;
	// stop pre-answer timeout, restart answered timeout
	m_maxcall = 0;
	maxPDD(0);
	if (m_toutAns)
	    timeout(Time::now() + m_toutAns*(u_int64_t)1000);
    }
    else if (m_status == YSTRING("ringing") || m_status == YSTRING("progressing"))
	maxPDD(0);
}

const char* Channel::direction() const
{
    return m_outgoing ? "outgoing" : "incoming";
}

void Channel::setMaxcall(const Message* msg, int defTout)
{
    int tout = msg ? msg->getIntValue(YSTRING("timeout"),defTout) : defTout;
    if (tout > 0) {
	m_toutAns = tout;
	timeout(Time::now() + tout*(u_int64_t)1000);
    }
    else if (tout == 0) {
	m_toutAns = 0;
	timeout(0);
    }
    if (m_answered)
	maxcall(0);
    else if (msg) {
	tout = msg->getIntValue(YSTRING("maxcall"),-1);
	if (tout > 0) {
	    timeout(0);
	    maxcall(Time::now() + tout*(u_int64_t)1000);
	}
	else if (tout == 0)
	    maxcall(0);
    }
}

void Channel::setMaxPDD(const Message& msg)
{
    if (m_answered) {
	maxPDD(0);
	return;
    }
    int tout = msg.getIntValue(YSTRING("maxpdd"),-1);
    if (tout > 0)
	maxPDD(Time::now() + tout * (u_int64_t)1000);
    else if (tout == 0)
	maxPDD(0);
}

void Channel::complete(Message& msg, bool minimal) const
{
    static const String s_hangup("chan.hangup");

    msg.setParam("id",id());
    if (m_driver)
	msg.setParam("module",m_driver->name());
    if (s_hangup == msg) {
	s_paramMutex.lock();
	msg.copyParams(parameters());
	s_paramMutex.unlock();
    }

    if (minimal)
	return;

    if (m_status)
	msg.setParam("status",m_status);
    if (m_address)
	msg.setParam("address",m_address);
    if (m_targetid)
	msg.setParam("targetid",m_targetid);
    if (m_billid)
	msg.setParam("billid",m_billid);
    String peer;
    if (getPeerId(peer))
	msg.setParam("peerid",peer);
    if (getLastPeerId(peer))
	msg.setParam("lastpeerid",peer);
    msg.setParam("answered",String::boolText(m_answered));
    msg.setParam("direction",direction());
}

Message* Channel::message(const char* name, bool minimal, bool data)
{
    Message* msg = new Message(name);
    if (data)
	msg->userData(this);
    complete(*msg,minimal);
    return msg;
}

Message* Channel::message(const char* name, const NamedList* original, const char* params, bool minimal, bool data)
{
    Message* msg = message(name,minimal,data);
    if (original) {
	if (!params)
	    params = original->getValue(s_copyParams);
	if (!null(params))
	    msg->copyParams(*original,params);
    }
    return msg;
}

bool Channel::startRouter(Message* msg)
{
    if (!msg)
	return false;
    if (m_driver) {
	Router* r = new Router(m_driver,id(),msg);
	if (r->startup())
	    return true;
	delete r;
    }
    else
	TelEngine::destruct(msg);
    callRejected("failure","Internal server error");
    // dereference and die if the channel is dynamic
    if (m_driver && m_driver->varchan())
	deref();
    return false;
}

bool Channel::msgProgress(Message& msg)
{
    status("progressing");
    if (m_billid.null())
	m_billid = msg.getValue(YSTRING("billid"));
    return true;
}

bool Channel::msgRinging(Message& msg)
{
    status("ringing");
    if (m_billid.null())
	m_billid = msg.getValue(YSTRING("billid"));
    return true;
}

bool Channel::msgAnswered(Message& msg)
{
    m_maxcall = 0;
    int tout = msg.getIntValue(YSTRING("timeout"),m_toutAns);
    m_toutAns = (tout > 0) ? tout : 0;
    status("answered");
    m_answered = true;
    if (m_billid.null())
	m_billid = msg.getValue(YSTRING("billid"));
    return true;
}

bool Channel::msgTone(Message& msg, const char* tone)
{
    return false;
}

bool Channel::msgText(Message& msg, const char* text)
{
    return false;
}

bool Channel::msgDrop(Message& msg, const char* reason)
{
    m_timeout = m_maxcall = m_maxPDD = 0;
    status(null(reason) ? "dropped" : reason);
    disconnect(reason,msg);
    return true;
}

bool Channel::msgTransfer(Message& msg)
{
    return false;
}

bool Channel::msgUpdate(Message& msg)
{
    return false;
}

bool Channel::msgMasquerade(Message& msg)
{
    if (m_billid.null())
	m_billid = msg.getValue(YSTRING("billid"));
    if (msg == YSTRING("call.answered")) {
	Debug(this,DebugInfo,"Masquerading answer operation [%p]",this);
	m_maxcall = 0;
	maxPDD(0);
	m_status = "answered";
    }
    else if (msg == YSTRING("call.progress")) {
	Debug(this,DebugInfo,"Masquerading progress operation [%p]",this);
	status("progressing");
    }
    else if (msg == YSTRING("call.ringing")) {
	Debug(this,DebugInfo,"Masquerading ringing operation [%p]",this);
	status("ringing");
    }
    else if (msg == YSTRING("chan.dtmf")) {
	// add sequence, stop the message if it was a disallowed DTMF duplicate
	if (dtmfSequence(msg) && m_driver && !m_driver->m_dtmfDups) {
	    Debug(this,DebugNote,"Stopping duplicate '%s' DTMF '%s' [%p]",
		msg.getValue("detected"),msg.getValue("text"),this);
	    return true;
	}
    }
    return false;
}

void Channel::msgStatus(Message& msg)
{
    String par;
    Lock lock(mutex());
    complete(msg);
    statusParams(par);
    lock.drop();
    msg.retValue().clear();
    msg.retValue() << "name=" << id() << ",type=channel;" << par << "\r\n";
}

// Control message handler that is invoked only for messages to this channel
// Find a data endpoint to process it
bool Channel::msgControl(Message& msg)
{
    setMaxcall(msg);
    setMaxPDD(msg);
    for (ObjList* o = m_data.skipNull(); o; o = o->skipNext()) {
	DataEndpoint* dep = static_cast<DataEndpoint*>(o->get());
	if (dep->control(msg))
	    return true;
    }
    return false;
}

void Channel::statusParams(String& str)
{
    if (m_driver)
	str.append("module=",",") << m_driver->name();
    String peer;
    if (getPeerId(peer))
	str.append("peerid=",",") << peer;
    str.append("status=",",") << m_status;
    str << ",direction=" << direction();
    str << ",answered=" << m_answered;
    str << ",targetid=" << m_targetid;
    str << ",address=" << m_address;
    str << ",billid=" << m_billid;
    if (m_timeout || m_maxcall || m_maxPDD) {
	u_int64_t t = Time::now();
	if (m_timeout) {
	    str << ",timeout=";
	    if (m_timeout > t)
		str << (unsigned int)((m_timeout - t + 500) / 1000);
	    else
		str << "expired";
	}
	if (m_maxcall) {
	    str << ",maxcall=";
	    if (m_maxcall > t)
		str << (unsigned int)((m_maxcall - t + 500) / 1000);
	    else
		str << "expired";
	}
	if (m_maxPDD) {
	    str << ",maxpdd=";
	    if (m_maxPDD > t)
		str << (unsigned int)((m_maxPDD - t + 500) / 1000);
	    else
		str << "expired";
	}
    }
}

void Channel::checkTimers(Message& msg, const Time& tmr)
{
    if (timeout() && (timeout() < tmr))
	msgDrop(msg,"timeout");
    else if (maxcall() && (maxcall() < tmr))
	msgDrop(msg,"noanswer");
    else if (maxPDD() && (maxPDD() < tmr))
	msgDrop(msg,"postdialdelay");
}

bool Channel::callPrerouted(Message& msg, bool handled)
{
    status("prerouted");
    // accept a new billid at this stage
    String* str = msg.getParam(YSTRING("billid"));
    if (str)
	m_billid = *str;
    return true;
}

bool Channel::callRouted(Message& msg)
{
    status("routed");
    if (m_billid.null())
	m_billid = msg.getValue(YSTRING("billid"));
    return true;
}

void Channel::callAccept(Message& msg)
{
    status("accepted");
    int defTout = m_driver ? m_driver->timeout() : -1;
    if (defTout <= 0)
	defTout = -1;
    setMaxcall(msg,defTout);
    if (m_billid.null())
	m_billid = msg.getValue(YSTRING("billid"));
    m_targetid = msg.getValue(YSTRING("targetid"));
    String detect = msg.getValue(YSTRING("tonedetect_in"));
    if (detect && detect.toBoolean(true)) {
	if (detect.toBoolean(false))
	    detect = "tone/*";
	toneDetect(detect);
    }
    if (msg.getBoolValue(YSTRING("autoanswer")))
	msgAnswered(msg);
    else if (msg.getBoolValue(YSTRING("autoring")))
	msgRinging(msg);
    else if (msg.getBoolValue(YSTRING("autoprogress")))
	msgProgress(msg);
    else if (m_targetid.null() && msg.getBoolValue(YSTRING("autoanswer"),true)) {
	// no preference exists in the message so issue a notice
	Debug(this,DebugNote,"Answering now call %s because we have no targetid [%p]",
	    id().c_str(),this);
	msgAnswered(msg);
    }
}

void Channel::callConnect(Message& msg)
{
    String detect = msg.getValue(YSTRING("tonedetect_out"));
    if (detect && detect.toBoolean(true)) {
	if (detect.toBoolean(false))
	    detect = "tone/*";
	toneDetect(detect);
    }
}

void Channel::callRejected(const char* error, const char* reason, const Message* msg)
{
    Debug(this,DebugMild,"Call rejected error='%s' reason='%s' [%p]",error,reason,this);
    if (msg) {
	const String* cp = msg->getParam(s_copyParams);
	if (!TelEngine::null(cp)) {
	    s_paramMutex.lock();
	    parameters().copyParams(*msg,*cp);
	    s_paramMutex.unlock();
	}
    }
    status("rejected");
}

bool Channel::dtmfSequence(Message& msg)
{
    if ((msg != YSTRING("chan.dtmf")) || msg.getParam(YSTRING("sequence")))
	return false;
    bool duplicate = false;
    const String* detected = msg.getParam(YSTRING("detected"));
    const String* text = msg.getParam(YSTRING("text"));
    Lock lock(mutex());
    unsigned int seq = m_dtmfSeq;
    if (text && detected &&
	(*text == m_dtmfText) && (*detected != m_dtmfDetected) &&
	(msg.msgTime() < m_dtmfTime))
	duplicate = true;
    else {
	seq = ++m_dtmfSeq;
	m_dtmfTime = msg.msgTime() + 4000000;
	m_dtmfText = text;
	m_dtmfDetected = detected;
    }
    // need to add sequence number used to detect reorders
    msg.addParam("sequence",String(seq));
    msg.addParam("duplicate",String::boolText(duplicate));
    return duplicate;
}

bool Channel::dtmfEnqueue(Message* msg)
{
    if (!msg)
	return false;
    if (dtmfSequence(*msg) && m_driver && !m_driver->m_dtmfDups) {
	Debug(this,DebugNote,"Dropping duplicate '%s' DTMF '%s' [%p]",
	    msg->getValue("detected"),msg->getValue("text"),this);
	TelEngine::destruct(msg);
	return false;
    }
    return Engine::enqueue(msg);
}

bool Channel::dtmfInband(const char* tone)
{
    if (null(tone))
	return false;
    Message m("chan.attach");
    complete(m,true);
    m.userData(this);
    String tmp("tone/dtmfstr/");
    tmp += tone;
    m.setParam("override",tmp);
    m.setParam("single","yes");
    return Engine::dispatch(m);
}

bool Channel::toneDetect(const char* sniffer)
{
    if (null(sniffer))
	sniffer = "tone/*";
    Message m("chan.attach");
    complete(m,true);
    m.userData(this);
    m.setParam("sniffer",sniffer);
    m.setParam("single","yes");
    return Engine::dispatch(m);
}

bool Channel::setDebug(Message& msg)
{
    String str = msg.getValue("line");
    if (str.startSkip("level")) {
	int dbg = debugLevel();
	str >> dbg;
	debugLevel(dbg);
    }
    else if (str == "reset")
	debugChain(m_driver);
    else if (str == "engine")
	debugCopy();
    else if (str.isBoolean())
	debugEnabled(str.toBoolean(debugEnabled()));
    msg.retValue() << "Channel " << id()
	<< " debug " << (debugEnabled() ? "on" : "off")
	<< " level " << debugLevel() << (debugChained() ? " chained" : "") << "\r\n";
    return true;
}

unsigned int Channel::allocId()
{
    s_callidMutex.lock();
    unsigned int id = ++s_callid;
    s_callidMutex.unlock();
    return id;
}

TokenDict Module::s_messages[] = {
    { "engine.status",   Module::Status },
    { "engine.timer",    Module::Timer },
    { "engine.debug",    Module::Level },
    { "engine.command",  Module::Command },
    { "engine.help",     Module::Help },
    { "engine.halt",     Module::Halt },
    { "engine.stop",     Module::Stop },
    { "call.route",      Module::Route },
    { "call.execute",    Module::Execute },
    { "call.drop",       Module::Drop },
    { "call.progress",   Module::Progress },
    { "call.ringing",    Module::Ringing },
    { "call.answered",   Module::Answered },
    { "call.update",     Module::Update },
    { "chan.dtmf",       Module::Tone },
    { "chan.text",       Module::Text },
    { "chan.masquerade", Module::Masquerade },
    { "chan.locate",     Module::Locate },
    { "chan.transfer",   Module::Transfer },
    { "chan.control",	 Module::Control },
    { "msg.execute",     Module::MsgExecute },
    { 0, 0 }
};

unsigned int Module::s_delay = 5;

const char* Module::messageName(int id)
{
    if ((id <= 0) || (id >PubLast))
	return 0;
    return lookup(id,s_messages);
}

Module::Module(const char* name, const char* type, bool earlyInit)
    : Plugin(name,earlyInit), Mutex(true,"Module"),
      m_init(false), m_relays(0), m_type(type), m_changed(0)
{
}

Module::~Module()
{
}

void* Module::getObject(const String& name) const
{
    if (name == YATOM("Module"))
	return const_cast<Module*>(this);
    return Plugin::getObject(name);
}

bool Module::installRelay(int id, const char* name, unsigned priority)
{
    if (!(id && name && priority))
	return false;

    TempObjectCounter cnt(objectsCounter(),true);
    Lock lock(this);
    if (m_relays & id)
	return true;
    m_relays |= id;

    MessageRelay* relay = new MessageRelay(name,this,id,priority,Module::name());
    m_relayList.append(relay)->setDelete(false);
    Engine::install(relay);
    return true;
}

bool Module::installRelay(int id, unsigned priority)
{
    return installRelay(id,messageName(id),priority);
}

bool Module::installRelay(const char* name, unsigned priority)
{
    return installRelay(lookup(name,s_messages),name,priority);
}

bool Module::installRelay(MessageRelay* relay)
{
    if (!relay || ((relay->id() & m_relays) != 0) || m_relayList.find(relay))
	return false;
    m_relays |= relay->id();
    m_relayList.append(relay)->setDelete(false);
    Engine::install(relay);
    return true;
}

bool Module::uninstallRelay(MessageRelay* relay, bool delRelay)
{
    if (!relay || ((relay->id() & m_relays) == 0) || !m_relayList.remove(relay,false))
	return false;
    Engine::uninstall(relay);
    m_relays &= ~relay->id();
    if (delRelay)
	TelEngine::destruct(relay);
    return true;
}

bool Module::uninstallRelay(int id, bool delRelay)
{
    if ((id & m_relays) == 0)
	return false;
    for (ObjList* l = m_relayList.skipNull(); l; l = l->skipNext()) {
	MessageRelay* r = static_cast<MessageRelay*>(l->get());
	if (r->id() != id)
	    continue;
	Engine::uninstall(r);
	m_relays &= ~id;
	l->remove(delRelay);
	break;
    }
    return false;
}


bool Module::uninstallRelays()
{
    while (MessageRelay* relay = static_cast<MessageRelay*>(m_relayList.remove(false))) {
	Engine::uninstall(relay);
	m_relays &= ~relay->id();
	relay->destruct();
    }
    return (0 == m_relays) && (0 == m_relayList.count());
}

void Module::initialize()
{
    setup();
}

void Module::setup()
{
    DDebug(this,DebugAll,"Module::setup()");
    if (m_init)
	return;
    m_init = true;
    installRelay(Timer,90);
    installRelay(Status,110);
    installRelay(Level,120);
    installRelay(Command,120);
}

void Module::changed()
{
    if (s_delay && !m_changed)
	m_changed = Time::now() + s_delay*(u_int64_t)1000000;
}

void Module::msgTimer(Message& msg)
{
    if (m_changed && (msg.msgTime() > m_changed)) {
	Message* m = new Message("module.update");
	m->addParam("module",name());
	m_changed = 0;
	genUpdate(*m);
	Engine::enqueue(m);
    }
}

bool Module::msgRoute(Message& msg)
{
    return false;
}

bool Module::msgCommand(Message& msg)
{
    const NamedString* line = msg.getParam(YSTRING("line"));
    if (line)
	return commandExecute(msg.retValue(),*line);
    if (msg.getParam(YSTRING("partline")) || msg.getParam(YSTRING("partword")))
	return commandComplete(msg,msg.getValue(YSTRING("partline")),msg.getValue(YSTRING("partword")));
    return false;
}

bool Module::commandExecute(String& retVal, const String& line)
{
    return false;
}

bool Module::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if ((partLine == YSTRING("debug")) || (partLine == YSTRING("status")))
	itemComplete(msg.retValue(),name(),partWord);
    return false;
}

bool Module::itemComplete(String& itemList, const String& item, const String& partWord)
{
    if (partWord.null() || item.startsWith(partWord)) {
	itemList.append(item,"\t");
	return true;
    }
    return false;
}

void Module::msgStatus(Message& msg)
{
    String mod, par, det;
    bool details = msg.getBoolValue(YSTRING("details"),true);
    lock();
    statusModule(mod);
    statusParams(par);
    if (details)
	statusDetail(det);
    unlock();
    msg.retValue() << mod << ";" << par;
    if (det)
	msg.retValue() << ";" << det;
    msg.retValue() << "\r\n";
}

void Module::statusModule(String& str)
{
    str.append("name=",",") << name();
    if (m_type)
	str << ",type=" << m_type;
}

void Module::statusParams(String& str)
{
}

void Module::statusDetail(String& str)
{
}

void Module::genUpdate(Message& msg)
{
}

bool Module::received(Message &msg, int id)
{
    if (name().null())
	return false;

    switch (id) {
	case Timer:
	    lock();
	    msgTimer(msg);
	    unlock();
	    return false;
	case Route:
	    return msgRoute(msg);
    }

    String dest = msg.getValue(YSTRING("module"));

    if (id == Status) {
	if (dest == name()) {
	    msgStatus(msg);
	    return true;
	}
	if (dest.null() || (dest == m_type))
	    msgStatus(msg);
	return false;
    }
    else if (id == Level)
	return setDebug(msg,dest);
    else if (id == Command)
	return msgCommand(msg);

    return false;
}

bool Module::setDebug(Message& msg, const String& target)
{
    if (target != name())
	return false;

    NamedCounter* counter = objectsCounter();
    String str = msg.getValue("line");
    if (str.startSkip("level")) {
	int dbg = debugLevel();
	str >> dbg;
	debugLevel(dbg);
    }
    else if (str == "reset") {
	debugLevel(TelEngine::debugLevel());
	debugEnabled(true);
	if (counter)
	    counter->enable(getObjCounting());
    }
    else if (str.startSkip("objects")) {
	bool dbg = (str == "reset") ? getObjCounting() : (counter && counter->enabled());
	str >> dbg;
	if (counter)
	    counter->enable(dbg);
    }
    else if (str.startSkip("filter"))
	m_filter = str;
    else {
	bool dbg = debugEnabled();
	str >> dbg;
	debugEnabled(dbg);
    }
    msg.retValue() << "Module " << name()
	<< " debug " << (debugEnabled() ? "on" : "off")
	<< " level " << debugLevel()
	<< " objects " << ((counter && counter->enabled()) ? "on" : "off");
    if (m_filter)
	msg.retValue() << " filter: " << m_filter;
    msg.retValue() << "\r\n";
    return true;
}

bool Module::filterDebug(const String& item) const
{
    return m_filter.null() ? debugEnabled() : m_filter.matches(item);
}


Driver::Driver(const char* name, const char* type)
    : Module(name,type),
      m_init(false), m_varchan(true),
      m_routing(0), m_routed(0), m_total(0),
      m_nextid(0), m_timeout(0),
      m_maxroute(0), m_maxchans(0), m_chanCount(0), m_dtmfDups(false)
{
    m_prefix << name << "/";
}

void* Driver::getObject(const String& name) const
{
    if (name == YATOM("Driver"))
	return const_cast<Driver*>(this);
    return Module::getObject(name);
}

void Driver::initialize()
{
    setup();
}

void Driver::setup(const char* prefix, bool minimal)
{
    DDebug(this,DebugAll,"Driver::setup('%s',%d)",prefix,minimal);
    Module::setup();
    loadLimits();
    if (m_init)
	return;
    m_init = true;
    m_prefix = prefix ? prefix : name().c_str();
    if (m_prefix && !m_prefix.endsWith("/"))
	m_prefix += "/";
    XDebug(DebugAll,"setup name='%s' prefix='%s'",name().c_str(),m_prefix.c_str());
    installRelay(Masquerade,10);
    installRelay(Locate,40);
    installRelay(Drop,60);
    installRelay(Execute,90);
    installRelay(Control,90);
    if (minimal)
	return;
    installRelay(Tone);
    installRelay(Text);
    installRelay(Ringing);
    installRelay(Answered);
}

bool Driver::isBusy() const
{
    return (m_routing || m_chanCount);
}

Channel* Driver::find(const String& id) const
{
    const ObjList* pos = m_chans.find(id);
    return pos ? static_cast<Channel*>(pos->get()) : 0;
}

bool Driver::received(Message &msg, int id)
{
    if (!m_prefix)
	return false;
    // pick destination depending on message type
    String dest;
    switch (id) {
	case Timer:
	    {
		// check each channel for timeouts
		lock();
		ListIterator iter(m_chans);
		Time t;
		for (;;) {
		    RefPointer<Channel> c = static_cast<Channel*>(iter.get());
		    unlock();
		    if (!c)
			break;
		    c->checkTimers(msg,t);
		    c = 0;
		    lock();
		}
	    }
	    return Module::received(msg,id);
	case Status:
	    // check if it's a channel status request
	    dest = msg.getValue(YSTRING("module"));
	    if (dest.startsWith(m_prefix))
		break;
	    // fall through
	case Level:
	case Route:
	case Command:
	    return Module::received(msg,id);
	case Halt:
	    dropAll(msg);
	    return false;
	case Execute:
	    dest = msg.getValue(YSTRING("callto"));
	    break;
	case Drop:
	case Masquerade:
	case Locate:
	    dest = msg.getValue(YSTRING("id"));
	    break;
	default:
	    dest = msg.getValue(YSTRING("peerid"));
	    // if this channel is not the peer, try to match it as target
	    if (!dest.startsWith(m_prefix))
		dest = msg.getValue(YSTRING("targetid"));
	    break;
    }
    XDebug(DebugAll,"id=%d prefix='%s' dest='%s'",id,m_prefix.c_str(),dest.c_str());

    if (id == Drop) {
	bool exact = (dest == name());
	if (exact || dest.null() || (dest == type())) {
	    dropAll(msg);
	    return exact;
	}
    }

    // handle call.execute which should start a new channel
    if (id == Execute) {
	if (!canAccept(false))
	    return false;
	if (dest.startSkip(m_prefix,false) ||
	    (dest.startSkip("line/",false) && hasLine(msg.getValue(YSTRING("line")))))
	    return msgExecute(msg,dest);
	return false;
    }

    // check if the message was for this driver
    if (!dest.startsWith(m_prefix))
	return false;

    lock();
    RefPointer<Channel> chan = find(dest);
    unlock();
    if (!chan) {
	DDebug(this,DebugMild,"Could not find channel '%s'",dest.c_str());
	return false;
    }

    switch (id) {
	case Status:
	    chan->msgStatus(msg);
	    return true;
	case Progress:
	    return chan->isIncoming() && !chan->isAnswered() && chan->msgProgress(msg);
	case Ringing:
	    return chan->isIncoming() && !chan->isAnswered() && chan->msgRinging(msg);
	case Answered:
	    return chan->isIncoming() && !chan->isAnswered() && chan->msgAnswered(msg);
	case Tone:
	    return chan->msgTone(msg,msg.getValue("text"));
	case Text:
	    return chan->msgText(msg,msg.getValue("text"));
	case Drop:
	    return chan->msgDrop(msg,msg.getValue("reason"));
	case Transfer:
	    return chan->msgTransfer(msg);
	case Update:
	    return chan->msgUpdate(msg);
	case Masquerade:
	    msg = msg.getValue(YSTRING("message"));
	    msg.clearParam(YSTRING("message"));
	    msg.userData(chan);
	    if (chan->msgMasquerade(msg))
		return true;
	    chan->complete(msg,msg.getBoolValue(YSTRING("complete_minimal"),false));
	    return false;
	case Locate:
	    msg.userData(chan);
	    return true;
	case Control:
	    return chan->msgControl(msg);
    }
    return false;
}

void Driver::dropAll(Message &msg)
{
    const char* reason = msg.getValue(YSTRING("reason"));
    lock();
    ListIterator iter(m_chans);
    for (;;) {
	RefPointer<Channel> c = static_cast<Channel*>(iter.get());
	unlock();
	if (!c)
	    break;
	DDebug(this,DebugAll,"Dropping %s channel '%s' @%p [%p]",
	    name().c_str(),c->id().c_str(),static_cast<Channel*>(c),this);
	c->msgDrop(msg,reason);
	c = 0;
	lock();
    }
}

bool Driver::canAccept(bool routers)
{
    if (Engine::exiting())
	return false;
    if (routers && !canRoute())
	return false;
    if (m_maxchans)
	return (m_chanCount < m_maxchans);
    return true;
}

bool Driver::canRoute()
{
    if (Engine::exiting() || (Engine::accept() >= Engine::Congestion))
	return false;
    if (m_maxroute && (m_routing >= m_maxroute))
	return false;
    return true;
}

bool Driver::hasLine(const String& line) const
{
    return false;
}

bool Driver::msgRoute(Message& msg)
{
    String called = msg.getValue(YSTRING("called"));
    if (called.null())
	return false;
    String line = msg.getValue(YSTRING("line"));
    if (line.null())
	line = msg.getValue(YSTRING("account"));
    if (line && hasLine(line)) {
	// asked to route to a line we have locally
	msg.setParam("line",line);
	msg.retValue() = prefix() + called;
	return true;
    }
    return Module::msgRoute(msg);
}

void Driver::genUpdate(Message& msg)
{
    msg.addParam("routed",String(m_routed));
    msg.addParam("routing",String(m_routing));
    msg.addParam("total",String(m_total));
    msg.addParam("chans",String(m_chanCount));
}

void Driver::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=Status|Address|Peer",",");
}

void Driver::statusParams(String& str)
{
    Module::statusParams(str);
    str.append("routed=",",") << m_routed;
    str << ",routing=" << m_routing;
    str << ",total=" << m_total;
    str << ",chans=" << m_chanCount;
}

void Driver::statusDetail(String& str)
{
    ObjList* l = m_chans.skipNull();
    for (; l; l=l->skipNext()) {
	Channel* c = static_cast<Channel*>(l->get());
	str.append(c->id(),",") << "=" << c->status() << "|" << c->address() << "|" << c->getPeerId();
    }
}

bool Driver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    bool ok = false;
    bool listChans = String(msg.getValue(YSTRING("complete"))) == YSTRING("channels");
    if (listChans && (partWord.null() || name().startsWith(partWord)))
	msg.retValue().append(name(),"\t");
    else
	ok = Module::commandComplete(msg,partLine,partWord);
    lock();
    unsigned int nchans = m_chans.count();
    unlock();
    if (nchans && listChans) {
	if (name().startsWith(partWord)) {
	    msg.retValue().append(prefix(),"\t");
	    return ok;
	}
	if (partWord.startsWith(prefix()))
	    ok = true;
	lock();
	ObjList* l = m_chans.skipNull();
	for (; l; l=l->skipNext()) {
	    Channel* c = static_cast<Channel*>(l->get());
	    if (c->id().startsWith(partWord))
		msg.retValue().append(c->id(),"\t");
	}
	unlock();
    }
    return ok;
}

bool Driver::setDebug(Message& msg, const String& target)
{
    if (!target.startsWith(m_prefix))
	return Module::setDebug(msg,target);

    Lock lock(this);
    Channel* chan = find(target);
    if (chan)
	return chan->setDebug(msg);

    return false;
}

void Driver::loadLimits()
{
    timeout(Engine::config().getIntValue(YSTRING("telephony"),"timeout"));
    maxRoute(Engine::config().getIntValue(YSTRING("telephony"),"maxroute"));
    maxChans(Engine::config().getIntValue(YSTRING("telephony"),"maxchans"));
    dtmfDups(Engine::config().getBoolValue(YSTRING("telephony"),"dtmfdups"));
}

unsigned int Driver::nextid()
{
    Lock lock(this);
    return ++m_nextid;
}


Router::Router(Driver* driver, const char* id, Message* msg)
    : Thread("Call Router"), m_driver(driver), m_id(id), m_msg(msg)
{
    if (driver)
	setObjCounter(driver->objectsCounter());
}

void Router::run()
{
    if (!(m_driver && m_msg))
	return;
    m_driver->lock();
    m_driver->m_routing++;
    m_driver->changed();
    m_driver->unlock();
    bool ok = route();
    m_driver->lock();
    m_driver->m_routing--;
    if (ok)
	m_driver->m_routed++;
    m_driver->changed();
    m_driver->unlock();
}

bool Router::route()
{
    DDebug(m_driver,DebugAll,"Routing thread for '%s' [%p]",m_id.c_str(),this);

    RefPointer<Channel> chan;
    String tmp(m_msg->getValue(YSTRING("callto")));
    bool ok = !tmp.null();
    if (ok)
	m_msg->retValue() = tmp;
    else {
	if (*m_msg == YSTRING("call.preroute")) {
	    ok = Engine::dispatch(m_msg);
	    m_driver->lock();
	    chan = m_driver->find(m_id);
	    m_driver->unlock();
	    if (!chan) {
		Debug(m_driver,DebugInfo,"Connection '%s' vanished while prerouting!",m_id.c_str());
		return false;
	    }
	    const String* cp = m_msg->getParam(s_copyParams);
	    if (!TelEngine::null(cp)) {
		Channel::paramMutex().lock();
		chan->parameters().copyParams(*m_msg,*cp);
		Channel::paramMutex().unlock();
	    }
	    bool dropCall = ok && ((m_msg->retValue() == YSTRING("-")) || (m_msg->retValue() == YSTRING("error")));
	    if (dropCall)
		chan->callRejected(m_msg->getValue(YSTRING("error"),"unknown"),
		    m_msg->getValue(YSTRING("reason")),m_msg);
	    else
		dropCall = !chan->callPrerouted(*m_msg,ok);
	    if (dropCall) {
		// get rid of the dynamic chans
		if (m_driver->varchan())
		    chan->deref();
		return false;
	    }
	    chan = 0;
	    *m_msg = "call.route";
	    m_msg->retValue().clear();
	}
	ok = Engine::dispatch(m_msg);
    }

    m_driver->lock();
    chan = m_driver->find(m_id);
    m_driver->unlock();

    if (!chan) {
	Debug(m_driver,DebugInfo,"Connection '%s' vanished while routing!",m_id.c_str());
	return false;
    }
    // chan will keep it referenced even if message user data is changed
    m_msg->userData(chan);

    static const char s_noroute[] = "noroute";
    static const char s_looping[] = "looping";
    static const char s_noconn[] = "noconn";

    if (ok && m_msg->retValue().trimSpaces()) {
	if ((m_msg->retValue() == YSTRING("-")) || (m_msg->retValue() == YSTRING("error")))
	    chan->callRejected(m_msg->getValue(YSTRING("error"),"unknown"),
		m_msg->getValue("reason"),m_msg);
	else if (m_msg->getIntValue(YSTRING("antiloop"),1) <= 0) {
	    const char* error = m_msg->getValue(YSTRING("error"),s_looping);
	    chan->callRejected(error,m_msg->getValue(YSTRING("reason"),
		((s_looping == error) ? "Call is looping" : (const char*)0)),m_msg);
	}
	else if (chan->callRouted(*m_msg)) {
	    *m_msg = "call.execute";
	    m_msg->setParam("callto",m_msg->retValue());
	    m_msg->clearParam(YSTRING("error"));
	    m_msg->retValue().clear();
	    ok = Engine::dispatch(m_msg);
	    if (ok)
		chan->callAccept(*m_msg);
	    else {
		const char* error = m_msg->getValue(YSTRING("error"),s_noconn);
		const char* reason = m_msg->getValue(YSTRING("reason"),
		    ((s_noconn == error) ? "Could not connect to target" : (const char*)0));
		Message m(s_disconnected);
		const String* cp = m_msg->getParam(s_copyParams);
		if (!TelEngine::null(cp))
		    m.copyParams(*m_msg,*cp);
		chan->complete(m);
		m.setParam("error",error);
		m.setParam("reason",reason);
		m.setParam("reroute",String::boolText(true));
		m.userData(chan);
		m.setNotify();
		if (!Engine::dispatch(m))
		    chan->callRejected(error,reason,m_msg);
	    }
	}
    }
    else {
	const char* error = m_msg->getValue(YSTRING("error"),s_noroute);
	chan->callRejected(error,m_msg->getValue(YSTRING("reason"),
	    ((s_noroute == error) ? "No route to call target" : (const char*)0)),m_msg);
    }

    // dereference again if the channel is dynamic
    if (m_driver->varchan())
	chan->deref();
    return ok;
}

void Router::cleanup()
{
    destruct(m_msg);
}


void CallAccount::pickAccountParams(const NamedList& params)
{
    NamedIterator iter(params);
    Lock mylock(m_mutex);
    m_inbParams.clearParams();
    m_outParams.clearParams();
    m_regParams.clearParams();
    while (const NamedString* n = iter.get()) {
	if (n->name().length() <= 4)
	    continue;
	String name = n->name().substr(4).trimSpaces();
	if (n->name().startsWith("reg:"))
	    m_regParams.setParam(name,*n);
	else if (n->name().startsWith("inb:"))
	    m_inbParams.setParam(name,*n);
	else if (n->name().startsWith("out:"))
	    m_outParams.setParam(name,*n);
    }
}

void CallAccount::setInboundParams(NamedList& params)
{
    Lock mylock(m_mutex);
    NamedIterator iter(m_inbParams);
    while (const NamedString* n = iter.get()) {
	String tmp(*n);
	params.replaceParams(tmp);
	params.setParam(n->name(),tmp);
    }
}

void CallAccount::setOutboundParams(NamedList& params)
{
    Lock mylock(m_mutex);
    NamedIterator iter(m_outParams);
    while (const NamedString* n = iter.get()) {
	String tmp(*n);
	params.replaceParams(tmp);
	params.setParam(n->name(),tmp);
    }
}

void CallAccount::setRegisterParams(NamedList& params)
{
    Lock mylock(m_mutex);
    NamedIterator iter(m_regParams);
    while (const NamedString* n = iter.get()) {
	String tmp(*n);
	params.replaceParams(tmp);
	params.setParam(n->name(),tmp);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
