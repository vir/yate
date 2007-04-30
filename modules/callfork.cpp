/**
 * callfork.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Call Forker
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

#define MOD_PREFIX "fork"

static Mutex s_mutex(true);
static ObjList s_calls;
static int s_current = 0;

class ForkSlave;

class ForkMaster : public CallEndpoint
{
public:
    ForkMaster(ObjList* targets);
    virtual ~ForkMaster();
    bool startCalling(Message& msg);
    void lostSlave(ForkSlave* slave, const char* reason);
    void msgAnswered(Message& msg, const String& dest);
    void msgProgress(Message& msg, const String& dest);
protected:
    void clear(bool softly);
    String* getNextDest();
    bool forkSlave(const char* dest);
    bool callContinue();
    ObjList m_slaves;
    String m_ringing;
    Regexp m_failures;
    int m_index;
    bool m_answered;
    bool m_rtpForward;
    bool m_rtpStrict;
    bool m_fake;
    ObjList* m_targets;
    Message* m_exec;
    String m_reason;
    String m_media;
};

class ForkSlave : public CallEndpoint
{
public:
    enum Type {
	Regular = 0,
	Auxiliar,
	Persistent
    };
    ForkSlave(ForkMaster* master, const char* id);
    virtual ~ForkSlave();
    virtual void disconnected(bool final, const char* reason);
    inline void lostMaster(const char* reason)
	{ m_master = 0; disconnect(reason); }
    inline Type type() const
	{ return m_type; }
    inline void setType(Type type)
	{ m_type = type; }
protected:
    ForkMaster* m_master;
    Type m_type;
};

class ForkModule : public Module
{
public:
    ForkModule();
    virtual ~ForkModule();
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    bool msgExecute(Message& msg);
    bool msgToMaster(Message& msg, bool answer);
};

static TokenDict s_calltypes[] = {
    { "regular",    ForkSlave::Regular    },
    { "auxiliar",   ForkSlave::Auxiliar   },
    { "persistent", ForkSlave::Persistent },
    { 0, 0 }
};

INIT_PLUGIN(ForkModule);


ForkMaster::ForkMaster(ObjList* targets)
    : m_index(0), m_answered(false), m_rtpForward(false), m_rtpStrict(false),
      m_fake(false), m_targets(targets), m_exec(0), m_reason("hangup")
{
    String tmp(MOD_PREFIX "/");
    s_mutex.lock();
    tmp << ++s_current;
    setId(tmp);
    s_calls.append(this);
    s_mutex.unlock();
    DDebug(&__plugin,DebugAll,"ForkMaster::ForkMaster(%p) '%s'",targets,id().c_str());
}

ForkMaster::~ForkMaster()
{
    DDebug(&__plugin,DebugAll,"ForkMaster::~ForkMaster() '%s'",id().c_str());
    s_mutex.lock();
    s_calls.remove(this,false);
    s_mutex.unlock();
    clear(false);
}

String* ForkMaster::getNextDest()
{
    String* ret = 0;
    while (!ret && m_targets && m_targets->count())
	ret = static_cast<String*>(m_targets->remove(false));
    return ret;
}

bool ForkMaster::forkSlave(const char* dest)
{
    if (null(dest))
	return false;
    bool ok = false;
    String tmp(id());
    tmp << "/" << ++m_index;
    ForkSlave* slave = new ForkSlave(this,tmp);
    m_exec->clearParam("error");
    m_exec->clearParam("reason");
    m_exec->clearParam("peerid");
    m_exec->clearParam("targetid");
    m_exec->clearParam("fork.ringer");
    m_exec->clearParam("fork.autoring");
    m_exec->clearParam("fork.calltype");
    m_exec->setParam("cdrtrack",String::boolText(false));
    m_exec->setParam("id",tmp);
    m_exec->setParam("callto",dest);
    m_exec->setParam("rtp_forward",String::boolText(m_rtpForward));
    m_exec->userData(slave);
    m_exec->msgTime() = Time::now();
    const char* error = "failure";
    bool autoring = false;
    if (Engine::dispatch(m_exec)) {
	ok = true;
	autoring = m_exec->getBoolValue("fork.autoring");
	if (m_ringing.null() && (autoring || m_exec->getBoolValue("fork.ringer")))
	    m_ringing = tmp;
	else
	    autoring = false;
	if (m_rtpForward) {
	    String rtp(m_exec->getValue("rtp_forward"));
	    if (rtp != "accepted") {
		error = "nomedia";
		int level = DebugWarn;
		if (m_rtpStrict) {
		    ok = false;
		    level = DebugCall;
		}
		Debug(&__plugin,level,"Call '%s' did not get RTP forward from '%s' target '%s'",
		    getPeerId().c_str(),slave->getPeerId().c_str(),dest);
	    }
	}
    }
    else
	error = m_exec->getValue("error",error);
    if (ok) {
	ForkSlave::Type type = static_cast<ForkSlave::Type>(m_exec->getIntValue("fork.calltype",s_calltypes,ForkSlave::Regular));
	Debug(&__plugin,DebugCall,"Call '%s' calling on %s '%s' target '%s'",
	    getPeerId().c_str(),lookup(type,s_calltypes),tmp.c_str(),dest);
	slave->setType(type);
	m_slaves.append(slave);
	if (autoring) {
	    Message* ring = new Message(m_exec->getValue("fork.automessage","call.ringing"));
	    ring->addParam("id",slave->getPeerId());
	    ring->addParam("peerid",tmp);
	    ring->addParam("targetid",tmp);
	    Engine::enqueue(ring);
	}
    }
    else
	slave->lostMaster(error);
    slave->deref();
    return ok;
}

bool ForkMaster::startCalling(Message& msg)
{
    m_exec = new Message(msg);
    // stoperror is OBSOLETE
    m_failures = msg.getValue("fork.stop",msg.getValue("stoperror"));
    m_exec->clearParam("stoperror");
    m_exec->clearParam("fork.stop");
    m_exec->setParam("fork.master",id());
    m_exec->setParam("fork.origid",getPeerId());
    m_rtpForward = msg.getBoolValue("rtp_forward");
    m_rtpStrict = msg.getBoolValue("rtpstrict");
    if (!callContinue()) {
	msg.setParam("error",m_exec->getValue("error"));
	msg.setParam("reason",m_exec->getValue("reason"));
	return false;
    }
    if (m_rtpForward) {
	String tmp(m_exec->getValue("rtp_forward"));
	if (tmp != "accepted") {
	    // no RTP forwarding from now on
	    m_rtpForward = false;
	    tmp = String::boolText(false);
	}
	msg.setParam("rtp_forward",tmp);
    }
    msg.setParam("peerid",id());
    msg.setParam("targetid",id());
    return true;
}

bool ForkMaster::callContinue()
{
    int forks = 0;
    while (m_exec && !m_answered) {
	// get the fake media source at start of each group
	m_media = m_exec->getValue("fork.fake");
	String* dest = getNextDest();
	if (!dest)
	    break;
	if (*dest == "|") {
	    dest->destruct();
	    if (forks)
		break;
	    continue;
	}
	if (forkSlave(*dest))
	    ++forks;
	dest->destruct();
    }
    return (forks > 0);
}

void ForkMaster::lostSlave(ForkSlave* slave, const char* reason)
{
    Lock lock(s_mutex);
    bool ringing = m_ringing == slave->id();
    if (ringing) {
	m_fake = false;
	m_ringing.clear();
	clearEndpoint();
    }
    m_slaves.remove(slave,false);
    if (m_answered)
	return;
    if (reason && m_failures && m_failures.matches(reason)) {
	Debug(&__plugin,DebugCall,"Call '%s' terminating early on reason '%s'",
	    getPeerId().c_str(),reason);
    }
    else {
	unsigned int regulars = 0;
	unsigned int auxiliars = 0;
	unsigned int persistents = 0;
	for (ObjList* l = m_slaves.skipNull(); l; l = l->skipNext()) {
	    switch (static_cast<const ForkSlave*>(l->get())->type()) {
		case ForkSlave::Auxiliar:
		    auxiliars++;
		    break;
		case ForkSlave::Persistent:
		    persistents++;
		    break;
		default:
		    regulars++;
		    break;
	    }
	}
	Debug(&__plugin,DebugNote,"Call '%s' lost%s slave '%s' reason '%s' remaining %u regulars, %u auxiliars, %u persistent",
	    getPeerId().c_str(),ringing ? " ringing" : "",
	    slave->id().c_str(),reason,
	    regulars,auxiliars,persistents);
	if (auxiliars && !regulars) {
	    Debug(&__plugin,DebugNote,"Dropping remaining %u auxiliars",auxiliars);
	    clear(true);
	}
	if (regulars || callContinue())
	    return;
	Debug(&__plugin,DebugCall,"Call '%s' failed by '%s' after %d attempts with reason '%s'",
	    getPeerId().c_str(),id().c_str(),m_index,reason);
    }
    lock.drop();
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
    m_media.clear();
    m_fake = false;
    m_answered = true;
    m_reason = msg.getValue("reason","pickup");
    Debug(&__plugin,DebugCall,"Call '%s' answered on '%s' by '%s'",
	peer->id().c_str(),dest.c_str(),call->id().c_str());
    msg.setParam("peerid",peer->id());
    msg.setParam("targetid",peer->id());
    lock.drop();
    clearEndpoint();
    call->connect(peer);
}

void ForkMaster::msgProgress(Message& msg, const String& dest)
{
    Lock lock(s_mutex);
    if (m_answered)
	return;
    if (m_ringing && (m_ringing != dest))
	return;
    
    ForkSlave* slave = static_cast<ForkSlave*>(m_slaves[dest]);
    if (!slave)
	return;
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return;
    DataEndpoint* dataEp = getEndpoint();
    if (m_ringing.null())
	m_ringing = dest;
    if (m_fake || !dataEp) {
	const CallEndpoint* call = slave->getPeer();
	if (!call)
	    call = static_cast<const CallEndpoint*>(msg.userObject("CallEndpoint"));
	if (call) {
	    dataEp = call->getEndpoint();
	    if (dataEp) {
		// don't use the media if it has no format and fake is possible
		if ((m_fake || m_media) &&
		    !(dataEp->getSource() && dataEp->getSource()->getFormat()))
		    dataEp = 0;
		else {
		    m_fake = false;
		    setEndpoint(dataEp);
		    m_media.clear();
		}
	    }
	}
    }
    msg.setParam("peerid",peer->id());
    msg.setParam("targetid",peer->id());
    if (m_media) {
	Debug(&__plugin,DebugInfo,"Call '%s' faking media '%s'",
	    peer->id().c_str(),m_media.c_str());
	String newMsg;
	if (m_exec)
	    newMsg = m_exec->getValue("fork.fakemessage");
	Message m("chan.attach");
	m.userData(this);
	m.addParam("id",id());
	m.addParam("source",m_media);
	m.addParam("single",String::boolText(true));
	m_media.clear();
	lock.drop();
	if (Engine::dispatch(m)) {
	    m_fake = true;
	    if (newMsg)
		msg = newMsg;
	}
    }
    Debug(&__plugin,DebugNote,"Call '%s' going on '%s' to '%s'%s%s",
	peer->id().c_str(),dest.c_str(),msg.getValue("id"),
	(dataEp || m_fake) ? " with audio data" : "",
	m_fake ? " (fake)" : "");
}

void ForkMaster::clear(bool softly)
{
    RefPointer<ForkSlave> slave;
    s_mutex.lock();
    ListIterator iter(m_slaves);
    while (slave = static_cast<ForkSlave*>(iter.get())) {
	if (softly && (slave->type() == ForkSlave::Persistent))
	    continue;
	m_slaves.remove(slave,false);
	s_mutex.unlock();
	slave->lostMaster(m_reason);
	s_mutex.lock();
	slave = 0;
    }
    if (softly) {
	s_mutex.unlock();
	return;
    }
    if (m_exec) {
	m_exec->destruct();
	m_exec = 0;
    }
    if (m_targets) {
	m_targets->destruct();
	m_targets = 0;
    }
    s_mutex.unlock();
}


ForkSlave::ForkSlave(ForkMaster* master, const char* id)
    : CallEndpoint(id), m_master(master), m_type(Regular)
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

void ForkModule::statusParams(String& str)
{
    s_mutex.lock();
    str.append("total=",",") << s_current << ",forks=" << s_calls.count();
    s_mutex.unlock();
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
    ForkMaster* master = new ForkMaster(targets);
    bool ok = false;
    if (master->connect(ch,msg.getValue("reason")))
	ok = master->startCalling(msg);
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

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
