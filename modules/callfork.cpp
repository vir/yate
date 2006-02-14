/**
 * callfork.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Call Forker
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

#include <yatephone.h>

using namespace TelEngine;

#define MOD_PREFIX "fork"

static Mutex s_mutex(true);
static ObjList s_calls;
static int s_current = 0;

class ForkSlave;

class ForkMaster : public CallEndpoint
{
public:
    ForkMaster();
    virtual ~ForkMaster();
    bool startCalling(Message& msg, ObjList* targets);
    void lostSlave(ForkSlave* slave, const char* reason);
    void msgAnswered(Message& msg, const String& dest);
    void msgProgress(Message& msg, const String& dest);
protected:
    void clear();
    ObjList m_slaves;
    String m_ringing;
    bool m_answered;
};

class ForkSlave : public CallEndpoint
{
public:
    ForkSlave(ForkMaster* master, const char* id);
    virtual ~ForkSlave();
    virtual void disconnected(bool final, const char* reason);
    inline void lostMaster(const char* reason)
	{ m_master = 0; disconnect(reason); }
protected:
    ForkMaster* m_master;
};

class ForkModule : public Module
{
public:
    ForkModule();
    virtual ~ForkModule();
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    bool msgExecute(Message& msg);
    bool msgToMaster(Message& msg, bool answer);
};

INIT_PLUGIN(ForkModule);


ForkMaster::ForkMaster()
    : m_answered(false)
{
    String tmp(MOD_PREFIX "/");
    s_mutex.lock();
    tmp << ++s_current;
    setId(tmp);
    s_calls.append(this);
    s_mutex.unlock();
    DDebug(&__plugin,DebugAll,"ForkMaster::ForkMaster() '%s'",id().c_str());
}

ForkMaster::~ForkMaster()
{
    DDebug(&__plugin,DebugAll,"ForkMaster::~ForkMaster() '%s'",id().c_str());
    s_mutex.lock();
    s_calls.remove(this,false);
    s_mutex.unlock();
    clear();
}

bool ForkMaster::startCalling(Message& msg, ObjList* targets)
{
    RefObject* obj = msg.userData();
    if (!(obj && obj->ref()))
	return false;
    String oid(msg.getValue("id"));
    String error;
    String reason;
    int forks = 0;
    int index = 0;
    for (; targets; targets=targets->next()) {
	// check if maybe the call was already answered
	if (m_answered)
	    break;
	String* dest = static_cast<String*>(targets->get());
	if (!dest)
	    continue;
	String tmp(id());
	tmp << "/" << ++index;
	ForkSlave* slave = new ForkSlave(this,tmp);
	msg.setParam("id",tmp);
	msg.setParam("callto",*dest);
	msg.userData(slave);
	msg.clearParam("error");
	msg.clearParam("reason");
	if (Engine::dispatch(msg)) {
	    m_slaves.append(slave);
	    forks++;
	}
	else {
	    slave->lostMaster(msg.getValue("error","failure"));
	    if (error.null())
		error = msg.getValue("error");
	    if (reason.null())
		reason = msg.getValue("reason");
	}
	slave->deref();
    }
    msg.userData(obj);
    obj->deref();
    msg.setParam("id",oid);
    if (forks)
	Debug(&__plugin,DebugCall,"Call '%s' forked to %d/%d targets",oid.c_str(),forks,index);
    else
	Debug(&__plugin,DebugWarn,"Could not fork '%s' to any of the %d targets",oid.c_str(),index);
    msg.clearParam("callto");
    if (forks || error.null())
	msg.clearParam("error");
    else
	msg.setParam("error",error);
    if (forks || reason.null())
	msg.clearParam("reason");
    else
	msg.setParam("reason",reason);
    msg.setParam("peerid",id());
    msg.setParam("targetid",id());
    return (forks != 0);
}

void ForkMaster::lostSlave(ForkSlave* slave, const char* reason)
{
    s_mutex.lock();
    if (m_ringing == slave->id())
	m_ringing.clear();
    m_slaves.remove(slave,false);
    unsigned int slaves = m_slaves.count();
    s_mutex.unlock();
    DDebug(&__plugin,DebugInfo,"Master '%s' lost slave '%s' remaining %u [%p]",id().c_str(),slave->id().c_str(),slaves,this);
    if (!(m_answered || slaves))
	disconnect(reason);
}

void ForkMaster::msgAnswered(Message& msg, const String& dest)
{
    Lock lock(s_mutex);
    // make sure only the first succeeds
    if (m_answered)
	return;
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return;
    ForkSlave* slave = static_cast<ForkSlave*>(m_slaves[dest]);
    if (!slave)
	return;
    RefPointer<CallEndpoint> call = slave->getPeer();
    if (!call)
	return;
    m_answered = true;
    msg.setParam("peerid",peer->id());
    msg.setParam("targetid",peer->id());
    Debug(&__plugin,DebugCall,"Call '%s' answered by %s '%s'",
	peer->id().c_str(),dest.c_str(),call->id().c_str());
    lock.drop();
    call->connect(peer);
}

void ForkMaster::msgProgress(Message& msg, const String& dest)
{
    Lock lock(s_mutex);
    if (m_answered)
	return;
    if (m_ringing && (m_ringing != dest))
	return;
    if (!m_slaves.find(dest))
	return;
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return;
    if (m_ringing.null())
	m_ringing = dest;
    Debug(&__plugin,DebugCall,"Call '%s' going on %s '%s'",
	peer->id().c_str(),dest.c_str(),msg.getValue("id"));
    msg.setParam("peerid",peer->id());
    msg.setParam("targetid",peer->id());
}

void ForkMaster::clear()
{
    s_mutex.lock();
    ListIterator iter(m_slaves);
    while (RefPointer<ForkSlave> slave = static_cast<ForkSlave*>(iter.get())) {
	m_slaves.remove(slave,false);
	s_mutex.unlock();
	slave->lostMaster("hangup");
	s_mutex.lock();
    }
    s_mutex.unlock();
}


ForkSlave::ForkSlave(ForkMaster* master, const char* id)
    : CallEndpoint(id), m_master(master)
{
    DDebug(&__plugin,DebugAll,"ForkSlave::ForkSlave(%s,'%s')",master->id().c_str(),id);
}

ForkSlave::~ForkSlave()
{
    DDebug(&__plugin,DebugAll,"ForkSlave::~ForkSlave() '%s'",id().c_str());
}

void ForkSlave::disconnected(bool final, const char* reason)
{
    CallEndpoint::disconnected(final,reason);
    if (m_master)
	m_master->lostSlave(this,reason);
}


ForkModule::ForkModule()
    : Module("callfork","misc")
{
    Output("Loaded module Call Forker");
}

ForkModule::~ForkModule()
{
    Output("Unloading module Call Forker");
}

void ForkModule::initialize()
{
    Output("Initializing module Call Forker");
    setup();
    installRelay(Execute);
    installRelay(Answered,20);
    installRelay(Ringing,20);
    installRelay(Progress,20);
}

bool ForkModule::msgExecute(Message& msg)
{
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch)
	return false;
    String dest(msg.getParam("callto"));
    if (!dest.startSkip(MOD_PREFIX))
	return false;
    ObjList* targets = dest.split(' ',false);
    if (!targets)
	return false;
    ForkMaster* master = new ForkMaster;
    bool ok = false;
    if (master->connect(ch,msg.getValue("reason")))
	ok = master->startCalling(msg, targets);
    targets->destruct();
    master->deref();
    return ok;
}

bool ForkModule::msgToMaster(Message& msg, bool answer)
{
    String dest(msg.getParam("targetid"));
    if (!dest.startsWith(MOD_PREFIX "/"))
	return false;
    int slash = dest.rfind('/');
    s_mutex.lock();
    // the fork master will be kept referenced until we finish the work
    RefPointer<ForkMaster> m = static_cast<ForkMaster*>(s_calls[dest.substr(0,slash)]);
    s_mutex.unlock();
    if (m) {
	if (answer)
	    m->msgAnswered(msg,dest);
	else
	    m->msgProgress(msg,dest);
    }
    return false;
}

bool ForkModule::received(Message& msg, int id)
{
    switch (id) {
	case Execute:
	    return msgExecute(msg);
	case Answered:
	    return msgToMaster(msg,true);
	case Progress:
	case Ringing:
	    return msgToMaster(msg,false);
	default:
	    return Module::received(msg,id);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
