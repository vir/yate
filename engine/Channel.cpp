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
    status(m_outgoing ? "outgoing" : "incoming");
    if (m_driver) {
	m_driver->lock();
	m_driver->channels().append(this);
	m_driver->unlock();
    }
}

Channel::~Channel()
{
    status("deleted");
    if (m_driver) {
	m_driver->lock();
	m_driver->channels().remove(this,false);
	m_driver->unlock();
	m_driver = 0;
    }
    disconnect(true,0);
    m_data.clear();
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

void Channel::complete(Message& msg) const
{
    msg.setParam("id",m_id);
    if (m_targetid)
	msg.setParam("targetid",m_targetid);
    if (m_billid)
	msg.setParam("billid",m_billid);
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

Driver::Driver(const char* name)
    : Plugin(name), Mutex(true), m_init(false)
{
}

void Driver::setup(const char* prefix)
{
    if (m_init)
	return;
    m_init = true;
    m_prefix = prefix;
    Engine::install(new MessageRelay("call.ringing",this,Ringing));
    Engine::install(new MessageRelay("call.answered",this,Answered));
    Engine::install(new MessageRelay("chan.dtmf",this,Tone));
    Engine::install(new MessageRelay("chan.text",this,Text));
    Engine::install(new MessageRelay("chan.masquerade",this,Masquerade,10));
    Engine::install(new MessageRelay("call.execute",this,Execute));
    Engine::install(new MessageRelay("call.drop",this,Drop));
}

bool Driver::received(Message &msg, int id)
{
    String dest;
    switch (id) {
	case Execute:
	    dest = msg.getValue("callto");
	    break;
	case Drop:
	case Masquerade:
	    dest = msg.getValue("id");
	    break;
	default:
	    dest = msg.getValue("targetid");
	    break;
    }
    if ((id == Drop) && dest.null()) {
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
    const ObjList* pos = m_chans.find(dest);
    Channel* chan = pos ? static_cast<Channel*>(pos->get()) : 0;
    if (!chan) {
	DDebug(DebugMild,"Could not find channel '%s'",dest);
	return false;
    }

    switch (id) {
	case Ringing:
	    return chan->msgRinging(msg);
	case Answered:
	    return chan->msgAnswered(msg);
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
    }
    return false;
}

void Driver::dropAll()
{
    lock();
    m_chans.clear();
    unlock();
}
