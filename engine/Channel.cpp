/**
 * Channel.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yatephone.h"

#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

Channel::Channel(Driver* driver, const char* id, bool outgoing)
    : m_peer(0), m_driver(driver), m_outgoing(outgoing), m_id(id)
{
    init();
}

Channel::Channel(Driver& driver, const char* id, bool outgoing)
    : m_peer(0), m_driver(&driver), m_outgoing(outgoing), m_id(id)
{
    init();
}

Channel::~Channel()
{
    status("deleted");
    if (m_driver) {
	m_driver->lock();
	m_driver->channels().remove(this,false);
	m_driver->changed();
	m_driver->unlock();
	m_driver = 0;
    }
    disconnect(true,0);
    m_data.clear();
}

void Channel::init()
{
    status(direction());
    if (m_driver) {
	debugChain(m_driver);
	m_driver->lock();
	if (m_id.null())
	    m_id << m_driver->prefix() << m_driver->nextid();
	m_driver->channels().append(this);
	m_driver->changed();
	m_driver->unlock();
    }
}

const char* Channel::direction() const
{
    return m_outgoing ? "outgoing" : "incoming";
}

bool Channel::connect(Channel* peer)
{
    Debug(DebugInfo,"Channel peer address is [%p]",peer);
    if (!peer) {
	disconnect();
	return false;
    }
    if (peer == m_peer)
	return true;

    ref();
    disconnect();
    peer->ref();
    peer->disconnect();

    m_peer = peer;
    peer->setPeer(this);
    connected();

    return true;
}

void Channel::disconnect(bool final, const char* reason)
{
    if (!m_peer)
	return;

    Channel *temp = m_peer;
    m_peer = 0;
    temp->setPeer(0,reason);
    temp->deref();
    disconnected(final,reason);
    deref();
}

void Channel::setPeer(Channel* peer, const char* reason)
{
    m_peer = peer;
    if (m_peer)
	connected();
    else
	disconnected(false,reason);
}

void Channel::complete(Message& msg, bool minimal) const
{
    msg.setParam("id",m_id);
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
    if (m_peer)
	msg.setParam("peerid",m_peer->id());
}

Message* Channel::message(const char* name, bool minimal) const
{
    Message* msg = new Message(name);
    complete(*msg,minimal);
    return msg;
}

bool Channel::msgRinging(Message& msg)
{
    status("ringing");
    return false;
}

bool Channel::msgAnswered(Message& msg)
{
    status("answered");
    return false;
}

bool Channel::msgTone(Message& msg, const char* tone)
{
    return false;
}

bool Channel::msgText(Message& msg, const char* text)
{
    return false;
}

bool Channel::msgDrop(Message& msg)
{
    return false;
}

void Channel::callAccept(Message& msg)
{
    status("accepted");
}

void Channel::callReject(const char* error, const char* reason)
{
    status("rejected");
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
    else {
	bool dbg = debugEnabled();
	str >> dbg;
	debugEnabled(dbg);
    }
    msg.retValue() << "Channel " << m_id
	<< " debug " << (debugEnabled() ? "on" : "off")
	<< " level " << debugLevel() << "\n";
    return true;
}

DataEndpoint* Channel::getEndpoint(const char* type) const
{
    if (null(type))
	return 0;
    const ObjList* pos = m_data.find(type);
    return pos ? static_cast<DataEndpoint*>(pos->get()) : 0;
}

DataEndpoint* Channel::addEndpoint(const char* type)
{
    if (null(type))
	return 0;
    DataEndpoint* dat = getEndpoint(type);
    if (!dat)
	dat = new DataEndpoint(this,type);
    return dat;
}

void Channel::setSource(DataSource* source, const char* type)
{
    DataEndpoint* dat = addEndpoint(type);
    if (dat)
	dat->setSource(source);
}

DataSource* Channel::getSource(const char* type) const
{
    DataEndpoint* dat = getEndpoint(type);
    return dat ? dat->getSource() : 0;
}

void Channel::setConsumer(DataConsumer* consumer, const char* type)
{
    DataEndpoint* dat = addEndpoint(type);
    if (dat)
	dat->setConsumer(consumer);
}

DataConsumer* Channel::getConsumer(const char* type) const
{
    DataEndpoint* dat = getEndpoint(type);
    return dat ? dat->getConsumer() : 0;
}


TokenDict Module::s_messages[] = {
    { "engine.status",   Module::Status },
    { "engine.timer",    Module::Timer },
    { "module.debug",    Module::Level },
    { "engine.command",  Module::Command },
    { "engine.help",     Module::Help },
    { "call.execute",    Module::Execute },
    { "call.drop",       Module::Drop },
    { "call.ringing",    Module::Ringing },
    { "call.answered",   Module::Answered },
    { "chan.dtmf",       Module::Tone },
    { "chan.text",       Module::Text },
    { "chan.masquerade", Module::Masquerade },
    { "chan.locate",     Module::Locate },
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
    if (m_init)
	return;
    m_init = true;
    installRelay(Status);
    installRelay(Timer);
    installRelay(Level);
}

void Module::changed()
{
    if (s_delay && !m_changed)
	m_changed = Time::now() + s_delay;
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

    if (id == Timer) {
	lock();
	msgTimer(msg);
	unlock();
	return false;
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
    else
	Debug(DebugGoOn,"Invalid relay id %d in module '%s', message '%s'",
	    id,m_name.c_str(),msg.c_str());

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
    else {
	bool dbg = debugEnabled();
	str >> dbg;
	debugEnabled(dbg);
    }
    msg.retValue() << "Module " << m_name
	<< " debug " << (debugEnabled() ? "on" : "off")
	<< " level " << debugLevel() << "\n";
    return true;
}


Driver::Driver(const char* name, const char* type)
    : Module(name,type), m_init(false), m_routing(0), m_routed(0), m_nextid(0)
{
}

void Driver::setup(const char* prefix)
{
    Module::setup();
    if (m_init)
	return;
    m_init = true;
    m_prefix = prefix ? prefix : name().c_str();
    if (m_prefix && !m_prefix.endsWith("/"))
	m_prefix += "/";
    installRelay(Ringing);
    installRelay(Answered);
    installRelay(Tone);
    installRelay(Text);
    installRelay(Masquerade,10);
    installRelay(Locate);
    installRelay(Execute);
    installRelay(Drop);
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
	case Status:
	case Timer:
	case Level:
	    return Module::received(msg,id);
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

    if ((id == Drop) && (dest.null() || (dest == name()) || (dest == type()))) {
	dropAll();
	return false;
    }
    // check if the message was for this driver
    if (!dest.startsWith(m_prefix))
	return false;

    // handle call.execute which should start a new channel
    if (id == Execute)
	return msgExecute(msg,dest);

    Lock lock(this);
    Channel* chan = find(dest);
    if (!chan) {
	DDebug(DebugMild,"Could not find channel '%s'",dest.c_str());
	return false;
    }

    switch (id) {
	case Ringing:
	    return chan->isOutgoing() && chan->msgRinging(msg);
	case Answered:
	    return chan->isOutgoing() && chan->msgAnswered(msg);
	case Tone:
	    return chan->msgTone(msg,msg.getValue("text"));
	case Text:
	    return chan->msgText(msg,msg.getValue("text"));
	case Drop:
	    return chan->msgDrop(msg);
	case Masquerade:
	    msg.setParam("targetid",chan->targetid());
	    msg = msg.getValue("message");
	    msg.clearParam("message");
	    msg.userData(chan);
	    return false;
	case Locate:
	    msg.userData(chan);
	    return true;
    }
    return false;
}

void Driver::dropAll()
{
    lock();
    m_chans.clear();
    unlock();
}

void Driver::genUpdate(Message& msg)
{
    msg.addParam("routed",String(m_routed));
    msg.addParam("routing",String(m_routing));
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
    str.append("format=Status|Address",",");
}

void Driver::statusParams(String& str)
{
    Module::statusParams(str);
    str.append("routed=",",") << m_routed;
    str << ",routing=" << m_routing;
    str << ",chans=" << m_chans.count();
}

void Driver::statusChannels(String& str)
{
    ObjList* l = m_chans.skipNull();
    for (; l; l=l->skipNext()) {
	Channel* c = static_cast<Channel*>(l->get());
	str.append(c->id(),",") << "=" << c->status() << "|" << c->address();
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

unsigned int Driver::nextid()
{
    Lock lock(this);
    return ++m_nextid;
}


Router::Router(Driver* driver, const char* id, Message* msg)
    : m_driver(driver), m_id(id), m_msg(msg)
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
    Debug(DebugAll,"Routing thread for '%s' [%p]",m_id.c_str(),this);
    bool ok = Engine::dispatch(m_msg) && !m_msg->retValue().null();

    m_driver->lock();
    Channel* chan = m_driver->find(m_id);
    if (chan) {
	// this will keep it referenced
	m_msg->userData(chan);
	chan->status("routed");
    }
    m_driver->unlock();

    if (!chan) {
	Debug(DebugMild,"Connection '%s' vanished while routing!",m_id.c_str());
	return false;
    }

    if (ok) {
	*m_msg = "call.execute";
	m_msg->setParam("callto",m_msg->retValue());
	m_msg->retValue().clear();
	ok = Engine::dispatch(m_msg);
	if (ok) {
	    chan->callAccept(*m_msg);
	    chan->deref();
	}
	else
	    chan->callReject("noconn");
    }
    else
	chan->callReject("noroute");

    return ok;
}

void Router::cleanup()
{
    delete m_msg;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
