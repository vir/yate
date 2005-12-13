/**
 * dumbchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 * Copyright (C) 2005 Maciek Kaminski
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

#include <yatengine.h>
#include <yatephone.h>

using namespace TelEngine;

class DumbDriver : public Driver
{
public:
    DumbDriver();
    ~DumbDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
};

INIT_PLUGIN(DumbDriver);

class DumbChannel :  public Channel
{
public:
    DumbChannel(const char* addr = 0) : Channel(__plugin) {
	m_address = addr;
	Engine::enqueue(message("chan.startup"));
    };
    ~DumbChannel();
    virtual void disconnected(bool final, const char *reason);
};

void DumbChannel::disconnected(bool final, const char *reason)
{
    Debug(DebugAll,"DumbChannel::disconnected() '%s'",reason);
    Channel::disconnected(final,reason);
}

DumbChannel::~DumbChannel()
{
    Debug(this,DebugAll,"DumbChannel::~DumbChannel() src=%p cons=%p",getSource(),getConsumer());
    Engine::enqueue(message("chan.hangup"));
}


bool DumbDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint *dd = static_cast<CallEndpoint *>(msg.userData());
    if (dd) {
	DumbChannel *c = new DumbChannel(dest);
	if (dd->connect(c)) {
	    msg.setParam("peerid", c->id());
	    c->deref();
	    return true;
	}
	else {
	    c->destruct();
	    return false;
	}
    }

    const char *targ = msg.getValue("target");
    if (!targ) {
	Debug(this,DebugWarn,"Outgoing call with no target!");
	return false;
    }

    Message m("call.route");
    m.addParam("driver","dumb");
    m.addParam("id", dest);
    m.addParam("caller",dest);
    m.addParam("called",targ);

    if (Engine::dispatch(m)) {
	m = "call.execute";
	m.addParam("callto",m.retValue());
	if (msg.getParam("maxcall"))
	    m.setParam("maxcall", msg.getParam("maxcall")->c_str());
	m.retValue().clear();
	DumbChannel *c = new DumbChannel(dest);
	m.setParam("id", c->id());
	m.userData(c);
	if (Engine::dispatch(m)) {
	    c->deref();
	    msg.setParam("id", m.getParam("id")->c_str());
	    msg.setParam("peerid", m.getParam("peerid")->c_str());
	    return true;
	}
	Debug(this,DebugWarn,"Outgoing call not accepted!");
	c->destruct();
    }
    else
	Debug(this,DebugWarn,"Outgoing call but no route!");
    return false;
}

DumbDriver::DumbDriver()
    : Driver("dumb", "misc")
{
    Output("Loaded module DumbChannel");
}

DumbDriver::~DumbDriver()
{
    Output("Unloading module DumbChannel");
}

void DumbDriver::initialize()
{
    Output("Initializing module DumbChannel");
    setup();
    Output("DumbChannel initialized");
}

/* vi: set ts=8 sw=4 sts=4 noet: */
