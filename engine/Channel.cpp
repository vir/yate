/**
 * Channel.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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
static Mutex s_callidMutex;

// this is to protect against two threads trying to (dis)connect a pair
//  of call endpoints at the same time
static Mutex s_mutex(true);

CallEndpoint::CallEndpoint(const char* id)
    : m_peer(0), m_id(id), m_mutex(0)
{
}

CallEndpoint::~CallEndpoint()
{
#ifdef DEBUG
    ObjList* l = m_data.skipNull();
    for (; l; l=l->skipNext()) {
	DataEndpoint* e = static_cast<DataEndpoint*>(l->get());
	Debug(DebugAll,"Endpoint at %p type '%s' refcount=%d",e,e->name().c_str(),e->refcount());
    }
#endif
    disconnect(true,0,true);
    m_data.clear();
}

Mutex& CallEndpoint::commonMutex()
{
    return s_mutex;
}

void* CallEndpoint::getObject(const String& name) const
{
    if (name == "CallEndpoint")
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
    if (!s_mutex.lock(5000000)) {
	Debug(DebugFail,"Call connect failed - deadlock on call endpoint mutex!");
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
    connected(reason);

#if 0
    s_mutex.unlock();
#endif

    return true;
}

bool CallEndpoint::disconnect(bool final, const char* reason, bool notify)
{
    if (!m_peer)
	return false;
    DDebug(DebugAll,"CallEndpoint '%s' disconnecting peer %p from [%p]",m_id.c_str(),m_peer,this);

    Lock lock(s_mutex,5000000);
    if (!lock.mutex()) {
	Debug(DebugFail,"Call disconnect failed - deadlock on call endpoint mutex!");
	Engine::restart(0);
	return false;
    }

    CallEndpoint *temp = m_peer;
    m_peer = 0;
    if (!temp)
	return false;

    ObjList* l = m_data.skipNull();
    for (; l; l=l->skipNext()) {
	DataEndpoint* e = static_cast<DataEndpoint*>(l->get());
	DDebug(DebugAll,"Endpoint at %p type '%s' peer %p",e,e->name().c_str(),e->getPeer());
	e->disconnect();
    }

    temp->setPeer(0,reason,notify);
    if (final)
	disconnected(true,reason);
    lock.drop();
    temp->deref();
    return deref();
}

void CallEndpoint::setPeer(CallEndpoint* peer, const char* reason, bool notify)
{
    m_peer = peer;
    if (m_peer)
	connected(reason);
    else if (notify)
	disconnected(false,reason);
}

DataEndpoint* CallEndpoint::getEndpoint(const char* type) const
{
    if (null(type))
	return 0;
    const ObjList* pos = m_data.find(type);
    return pos ? static_cast<DataEndpoint*>(pos->get()) : 0;
}

DataEndpoint* CallEndpoint::setEndpoint(const char* type)
{
    if (null(type))
	return 0;
    DataEndpoint* dat = getEndpoint(type);
    if (!dat) {
	dat = new DataEndpoint(this,type);
	if (m_peer)
	    dat->connect(m_peer->getEndpoint(type));
    }
    return dat;
}

void CallEndpoint::clearEndpoint(const char* type)
{
    if (null(type)) {
	ObjList* l = m_data.skipNull();
	for (; l; l=l->skipNext()) {
	    DataEndpoint* e = static_cast<DataEndpoint*>(l->get());
	    DDebug(DebugAll,"Endpoint at %p type '%s' peer %p",e,e->name().c_str(),e->getPeer());
	    e->disconnect();
	}
	m_data.clear();
    }
    else {
	DataEndpoint* dat = getEndpoint(type);
	if (dat) {
	    m_data.remove(dat,false);
	    dat->disconnect();
	    dat->destruct();
	}
    }
}

void CallEndpoint::setSource(DataSource* source, const char* type)
{
    DataEndpoint* dat = source ? setEndpoint(type) : getEndpoint(type);
    if (dat)
	dat->setSource(source);
}

DataSource* CallEndpoint::getSource(const char* type) const
{
    DataEndpoint* dat = getEndpoint(type);
    return dat ? dat->getSource() : 0;
}

void CallEndpoint::setConsumer(DataConsumer* consumer, const char* type)
{
    DataEndpoint* dat = consumer ? setEndpoint(type) : getEndpoint(type);
    if (dat)
	dat->setConsumer(consumer);
}

DataConsumer* CallEndpoint::getConsumer(const char* type) const
{
    DataEndpoint* dat = getEndpoint(type);
    return dat ? dat->getConsumer() : 0;
}

Channel::Channel(Driver* driver, const char* id, bool outgoing)
    : CallEndpoint(id),
      m_driver(driver), m_outgoing(outgoing), m_timeout(0), m_maxcall(0), m_answered(false)
{
    init();
}

Channel::Channel(Driver& driver, const char* id, bool outgoing)
    : CallEndpoint(id),
      m_driver(&driver), m_outgoing(outgoing), m_timeout(0), m_maxcall(0), m_answered(false)
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
    if (name == "Channel")
	return const_cast<Channel*>(this);
    return CallEndpoint::getObject(name);
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
	m_driver->m_total++;
	m_driver->channels().append(this);
	m_driver->changed();
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

void Channel::dropChan()
{
    if (!m_driver)
	return;
    m_driver->lock();
    if (!m_driver)
	Debug(DebugFail,"Driver lost in dropChan! [%p]",this);
    if (m_driver->channels().remove(this,false))
	m_driver->changed();
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
    Channel* peer = YOBJECT(Channel,getPeer());
    if (peer && peer->billid() && m_billid.null())
	m_billid = peer->billid();
}

void Channel::disconnected(bool final, const char* reason)
{
    if (final || Engine::exiting())
	return;
    // last chance to get reconnected to something
    Message* m = message("chan.disconnected");
    m_targetid.clear();
    // we will remain referenced until the message is destroyed
    m->userData(this);
    if (reason)
	m->setParam("reason",reason);
    Engine::enqueue(m);
}

void Channel::setId(const char* newId)
{
    debugName(0);
    CallEndpoint::setId(newId);
    debugName(id());
}

void Channel::status(const char* newstat)
{
    m_status = newstat;
    m_answered = m_answered || (m_status == "answered");
}

const char* Channel::direction() const
{
    return m_outgoing ? "outgoing" : "incoming";
}

void Channel::setMaxcall(const Message* msg)
{
    int tout = msg ? msg->getIntValue("maxcall") : 0;
    if (tout > 0)
	maxcall(Time::now() + tout*(u_int64_t)1000);
    else
	maxcall(0);
}

void Channel::complete(Message& msg, bool minimal) const
{
    msg.setParam("id",id());
    if (m_driver)
	msg.setParam("module",m_driver->name());

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
    if (getPeer())
	msg.setParam("peerid",getPeer()->id());
    msg.setParam("answered",String::boolText(m_answered));
}

Message* Channel::message(const char* name, bool minimal, bool data)
{
    Message* msg = new Message(name);
    if (data)
	msg->userData(this);
    complete(*msg,minimal);
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
	delete msg;
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
	m_billid = msg.getValue("billid");
    return true;
}

bool Channel::msgRinging(Message& msg)
{
    status("ringing");
    if (m_billid.null())
	m_billid = msg.getValue("billid");
    return true;
}

bool Channel::msgAnswered(Message& msg)
{
    m_maxcall = 0;
    m_answered = true;
    status("answered");
    if (m_billid.null())
	m_billid = msg.getValue("billid");
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
    m_timeout = m_maxcall = 0;
    status(null(reason) ? "dropped" : reason);
    disconnect(reason);
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

void Channel::msgStatus(Message& msg)
{
    String par;
    Lock lock(mutex());
    complete(msg);
    statusParams(par);
    lock.drop();
    msg.retValue().clear();
    msg.retValue() << "name=" << id() << ",type=channel;" << par << "\n";
}

void Channel::statusParams(String& str)
{
    if (m_driver)
	str.append("module=",",") << m_driver->name();
    if (getPeer())
	str.append("peerid=",",") << getPeer()->id();
    str.append("status=",",") << m_status;
    str << ",direction=" << direction();
    str << ",answered=" << m_answered;
    str << ",targetid=" << m_targetid;
    str << ",address=" << m_address;
    str << ",billid=" << m_billid;
    if (m_timeout || m_maxcall) {
	u_int64_t t = Time::now();
	if (m_timeout) {
	    str << ",timeout=";
	    if (m_timeout > t)
		str << (unsigned int)((m_timeout - t) / 1000);
	    else
		str << "expired";
	}
	if (m_maxcall) {
	    str << ",maxcall=";
	    if (m_maxcall > t)
		str << (unsigned int)((m_maxcall - t) / 1000);
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
}

bool Channel::callPrerouted(Message& msg, bool handled)
{
    status("prerouted");
    // accept a new billid at this stage
    String* str = msg.getParam("billid");
    if (str)
	m_billid = *str;
    return true;
}

bool Channel::callRouted(Message& msg)
{
    status("routed");
    if (m_billid.null())
	m_billid = msg.getValue("billid");
    return true;
}

void Channel::callAccept(Message& msg)
{
    status("accepted");
    int tout = msg.getIntValue("timeout", m_driver ? m_driver->timeout() : 0);
    if (tout > 0)
	timeout(Time::now() + tout*(u_int64_t)1000);
    if (m_billid.null())
	m_billid = msg.getValue("billid");
    m_targetid = msg.getValue("targetid");
    if (msg.getBoolValue("autoanswer"))
	msgAnswered(msg);
    else if (msg.getBoolValue("autoring"))
	msgRinging(msg);
    else if (msg.getBoolValue("autoprogress"))
	msgProgress(msg);
    else if (m_targetid.null() && msg.getBoolValue("autoanswer",true)) {
	// no preference exists in the message so issue a notice
	Debug(this,DebugNote,"Answering now call %s because we have no targetid [%p]",
	    id().c_str(),this);
	msgAnswered(msg);
    }
}

void Channel::callRejected(const char* error, const char* reason, const Message* msg)
{
    Debug(this,DebugMild,"Call rejected error='%s' reason='%s' [%p]",error,reason,this);
    status("rejected");
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
	<< " level " << debugLevel() << (debugChained() ? " chained" : "") << "\n";
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
    { 0, 0 }
};

unsigned int Module::s_delay = 5;

const char* Module::messageName(int id)
{
    if ((id <= 0) || (id >PubLast))
	return 0;
    return lookup(id,s_messages);
}

Module::Module(const char* name, const char* type)
    : Plugin(name), Mutex(true),
      m_init(false), m_relays(0), m_name(name), m_type(type), m_changed(0)
{
    debugName(m_name);
}

Module::~Module()
{
    debugName(0);
}

void* Module::getObject(const String& name) const
{
    if (name == "Module")
	return const_cast<Module*>(this);
    return Plugin::getObject(name);
}

bool Module::installRelay(const char* name, int id, unsigned priority)
{
    if (!(id && name))
	return false;

    Lock lock(this);
    if (m_relays & id)
	return true;
    m_relays |= id;

    Engine::install(new MessageRelay(name,this,id,priority));
    return true;
}

bool Module::installRelay(int id, unsigned priority)
{
    return installRelay(messageName(id),id,priority);
}

bool Module::installRelay(const char* name, unsigned priority)
{
    return installRelay(name,lookup(name,s_messages),priority);
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
	m->addParam("module",m_name);
	m_changed = 0;
	genUpdate(*m);
	Engine::enqueue(m);
    }
}

bool Module::msgRoute(Message& msg)
{
    return false;
}

void Module::msgStatus(Message& msg)
{
    String mod, par;
    lock();
    statusModule(mod);
    statusParams(par);
    unlock();
    msg.retValue() << mod << ";" << par << "\n";
}

void Module::statusModule(String& str)
{
    str.append("name=",",") << m_name;
    if (m_type)
	str << ",type=" << m_type;
}

void Module::statusParams(String& str)
{
}

void Module::genUpdate(Message& msg)
{
}

bool Module::received(Message &msg, int id)
{
    if (!m_name)
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

    String dest = msg.getValue("module");

    if (id == Status) {
	if (dest == m_name) {
	    msgStatus(msg);
	    return true;
	}
	if (dest.null() || (dest == m_type))
	    msgStatus(msg);
	return false;
    }
    else if (id == Level)
	return setDebug(msg,dest);

    return false;
}

bool Module::setDebug(Message& msg, const String& target)
{
    if (target != m_name)
	return false;

    String str = msg.getValue("line");
    if (str.startSkip("level")) {
	int dbg = debugLevel();
	str >> dbg;
	debugLevel(dbg);
    }
    else if (str == "reset") {
	debugLevel(TelEngine::debugLevel());
	debugEnabled(true);
    }
    else if (str.startSkip("filter"))
	m_filter = str;
    else {
	bool dbg = debugEnabled();
	str >> dbg;
	debugEnabled(dbg);
    }
    msg.retValue() << "Module " << m_name
	<< " debug " << (debugEnabled() ? "on" : "off")
	<< " level " << debugLevel();
    if (m_filter)
	msg.retValue() << " filter: " << m_filter;
    msg.retValue() << "\n";
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
      m_maxroute(0), m_maxchans(0)
{
    m_prefix << name << "/";
}

void* Driver::getObject(const String& name) const
{
    if (name == "Driver")
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
    if (minimal)
	return;
    installRelay(Tone);
    installRelay(Text);
    installRelay(Ringing);
    installRelay(Answered);
}

bool Driver::isBusy() const
{
    return (m_routing || m_chans.count());
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
		    if (c->timeout() && (c->timeout() < t))
			c->msgDrop(msg,"timeout");
		    else if (c->maxcall() && (c->maxcall() < t))
			c->msgDrop(msg,"noanswer");
		    lock();
		}
	    }
	case Status:
	    // check if it's a channel status request
	    dest = msg.getValue("module");
	    if (dest.startsWith(m_prefix))
		break;
	case Level:
	case Route:
	    return Module::received(msg,id);
	case Halt:
	    dropAll(msg);
	    return false;
	case Execute:
	    dest = msg.getValue("callto");
	    break;
	case Drop:
	case Masquerade:
	case Locate:
	    dest = msg.getValue("id");
	    break;
	default:
	    dest = msg.getValue("targetid");
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
    // check if the message was for this driver
    if (!dest.startsWith(m_prefix))
	return false;

    // handle call.execute which should start a new channel
    if (id == Execute) {
	if (!canAccept(false))
	    return false;
	dest.startSkip(m_prefix,false);
	return msgExecute(msg,dest);
    }

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
	    return chan->isIncoming() && chan->msgProgress(msg);
	case Ringing:
	    return chan->isIncoming() && chan->msgRinging(msg);
	case Answered:
	    return chan->isIncoming() && chan->msgAnswered(msg);
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
	    msg = msg.getValue("message");
	    msg.clearParam("message");
	    msg.userData(chan);
	    chan->complete(msg);
	    return false;
	case Locate:
	    msg.userData(chan);
	    return true;
    }
    return false;
}

void Driver::dropAll(Message &msg)
{
    const char* reason = msg.getValue("reason");
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
    if (m_maxchans) {
	Lock mylock(this);
	return ((signed)m_chans.count() < m_maxchans);
    }
    return true;
}

bool Driver::canRoute()
{
    if (Engine::exiting())
	return false;
    if (m_maxroute && (m_routing >= m_maxroute))
	return false;
    return true;
}

void Driver::genUpdate(Message& msg)
{
    msg.addParam("routed",String(m_routed));
    msg.addParam("routing",String(m_routing));
    msg.addParam("total",String(m_total));
    msg.addParam("chans",String(m_chans.count()));
}

void Driver::msgStatus(Message& msg)
{
    String mod, par, c;
    lock();
    statusModule(mod);
    statusParams(par);
    statusChannels(c);
    unlock();
    msg.retValue() << mod << ";" << par << ";" << c << "\n";
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
    str << ",chans=" << m_chans.count();
}

void Driver::statusChannels(String& str)
{
    ObjList* l = m_chans.skipNull();
    for (; l; l=l->skipNext()) {
	Channel* c = static_cast<Channel*>(l->get());
	str.append(c->id(),",") << "=" << c->status() << "|" << c->address() << "|" << c->getPeerId();
    }
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
    timeout(Engine::config().getIntValue("telephony","timeout"));
    maxRoute(Engine::config().getIntValue("telephony","maxroute"));
    maxChans(Engine::config().getIntValue("telephony","maxchans"));
}

unsigned int Driver::nextid()
{
    Lock lock(this);
    return ++m_nextid;
}


Router::Router(Driver* driver, const char* id, Message* msg)
    : Thread("Call Router"), m_driver(driver), m_id(id), m_msg(msg)
{
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
    String tmp(m_msg->getValue("callto"));
    bool ok = !tmp.null();
    if (ok)
	m_msg->retValue() = tmp;
    else {
	if (*m_msg == "call.preroute") {
	    ok = Engine::dispatch(m_msg);
	    m_driver->lock();
	    chan = m_driver->find(m_id);
	    m_driver->unlock();
	    if (!chan) {
		Debug(m_driver,DebugInfo,"Connection '%s' vanished while prerouting!",m_id.c_str());
		return false;
	    }
	    bool dropCall = ok && ((m_msg->retValue() == "-") || (m_msg->retValue() == "error"));
	    if (dropCall)
		chan->callRejected(m_msg->getValue("error","unknown"),
		    m_msg->getValue("reason"),m_msg);
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

    if (ok) {
	if ((m_msg->retValue() == "-") || (m_msg->retValue() == "error"))
	    chan->callRejected(m_msg->getValue("error","unknown"),
		m_msg->getValue("reason"),m_msg);
	else if (m_msg->getIntValue("antiloop",1) <= 0)
	    chan->callRejected(m_msg->getValue("error","looping"),
		m_msg->getValue("reason","Call is looping"),m_msg);
	else if (chan->callRouted(*m_msg)) {
	    *m_msg = "call.execute";
	    m_msg->setParam("callto",m_msg->retValue());
	    m_msg->clearParam("error");
	    m_msg->retValue().clear();
	    ok = Engine::dispatch(m_msg);
	    if (ok)
		chan->callAccept(*m_msg);
	    else {
		const char* error = m_msg->getValue("error","noconn");
		const char* reason = m_msg->getValue("reason","Could not connect to target");
		Message m("chan.disconnected");
		chan->complete(m);
		m.setParam("error",error);
		m.setParam("reason",reason);
		m.setParam("reroute",String::boolText(true));
		m.userData(chan);
		if (!Engine::dispatch(m))
		    chan->callRejected(error,reason,m_msg);
	    }
	}
    }
    else
	chan->callRejected(m_msg->getValue("error","noroute"),
	    m_msg->getValue("reason","No route to call target"),m_msg);

    // dereference again if the channel is dynamic
    if (m_driver->varchan())
	chan->deref();
    return ok;
}

void Router::cleanup()
{
    delete m_msg;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
