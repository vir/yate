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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatengine.h>
#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

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
    DumbChannel(const char* addr, const NamedList& exeMsg, bool outgoing) :
      Channel(__plugin, 0, outgoing)
    {
	m_address = addr;
	Message* s = message("chan.startup",exeMsg);
	if (outgoing)
	    s->copyParams(exeMsg,"caller,callername,called,billid,callto,username");
	Engine::enqueue(s);
    };
    ~DumbChannel();
    virtual void disconnected(bool final, const char *reason);
    inline void setTargetid(const char* targetid)
	{ m_targetid = targetid; }
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
    CallEndpoint *dd = YOBJECT(CallEndpoint,msg.userData());
    if (dd) {
	DumbChannel *c = new DumbChannel(dest,msg,true);
	c->initChan();
	if (dd->connect(c)) {
	    c->callConnect(msg);
	    msg.setParam("peerid", c->id());
	    msg.setParam("targetid", c->id());
	    c->setTargetid(dd->id());
	    // autoring unless parameter is already set in message
	    if (!msg.getParam("autoring"))
		msg.addParam("autoring","true");
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

    DumbChannel* c = new DumbChannel(dest,msg,false);
    c->initChan();

    String caller = msg.getValue("caller");
    if (caller.null())
	caller << prefix() << dest;

    Message m("call.route");
    m.addParam("driver","dumb");
    m.addParam("id", c->id());
    m.addParam("caller",caller);
    m.addParam("called",targ);
    m.copyParam(msg,"callername");
    m.copyParam(msg,"maxcall");
    m.copyParam(msg,"timeout");
    m.copyParams(msg,msg.getValue("copyparams"));

    const String& callto = msg["direct"];
    if (callto || Engine::dispatch(m)) {
	m = "call.execute";
	if (callto)
	    m.addParam("callto",callto);
	else {
	    m.addParam("callto",m.retValue());
	    m.retValue().clear();
	}
	m.setParam("id", c->id());
	m.userData(c);
	if (Engine::dispatch(m) && c->callRouted(m)) {
	    c->callAccept(m);
	    msg.copyParam(m,"id");
	    msg.copyParam(m,"peerid");
	    const char* targetid = m.getValue("targetid");
	    if (targetid) {
		msg.setParam("targetid",targetid);
		c->setTargetid(targetid);
	    }
	    c->deref();
	    return true;
	}
	else {
	    msg.copyParam(m,"error");
	    msg.copyParam(m,"reason");
	}
	Debug(this,DebugWarn,"Outgoing call not accepted!");
    }
    else
	Debug(this,DebugWarn,"Outgoing call but no route!");
    c->destruct();
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

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
