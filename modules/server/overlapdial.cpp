/**
 * overlapdial.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Overlapped Dialer
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

#define MOD_PREFIX "overlapdial"

static Mutex s_mutex(true);
static ObjList s_calls;
static int s_current = 0;
static int s_minlen = 0;
static int s_maxlen = 16;

class OverlapDialMaster : public CallEndpoint
{
public:
    OverlapDialMaster(const String & dest);
    virtual ~OverlapDialMaster();
    bool startWork(Message& msg);
    void msgDTMF(Message& msg);
    bool gotDigit(char digit);
    bool checkCollectedNumber(String & route);
    bool switchCall(const String & route);
    void sendProgress();
protected:
    void grabLengths(Message & m);
    Message* m_msg; // copy of original call.exeute message
    String m_dest; // callto tail
    String m_collected; // collected dialed number
    unsigned int m_len_min, m_len_max, m_len_fixed;
};

class OverlapDialModule : public Module
{
public:
    OverlapDialModule();
    virtual ~OverlapDialModule();
    bool unload();
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    bool msgExecute(Message& msg);
    bool msgToMaster(Message& msg);
};

INIT_PLUGIN(OverlapDialModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}


OverlapDialMaster::OverlapDialMaster(const String & dest)
    : m_msg(0), m_dest(dest), m_len_min(0), m_len_max(0), m_len_fixed(0)
{
    String tmp(MOD_PREFIX "/");
    s_mutex.lock();
    tmp << ++s_current;
    setId(tmp);
    s_calls.append(this);
    s_mutex.unlock();
    DDebug(&__plugin,DebugCall,"OverlapDialMaster::OverlapDialMaster(%s) '%s'",dest.c_str(),id().c_str());
}

OverlapDialMaster::~OverlapDialMaster()
{
    DDebug(&__plugin,DebugCall,"OverlapDialMaster::~OverlapDialMaster() '%s'",id().c_str());
    s_mutex.lock();
    s_calls.remove(this,false);
    TelEngine::destruct(m_msg);
    s_mutex.unlock();
}

bool OverlapDialMaster::startWork(Message& msg)
{
    m_msg = new Message(msg);
    s_mutex.lock();
    m_len_min = s_minlen;
    m_len_max = s_maxlen;
    s_mutex.unlock();
    grabLengths(*m_msg);
    if(false) {
	msg.setParam("error",m_msg->getValue("error"));
	msg.setParam("reason",m_msg->getValue("reason"));
	return false;
    }
    msg.setParam("peerid",id());
    msg.setParam("targetid",id());
    return true;
}

void OverlapDialMaster::msgDTMF(Message& msg)
{
    String dtmf = msg.getValue("text");
    for(unsigned int i = 0; i < dtmf.length(); ++i)
	if(! gotDigit(dtmf[i]))
	    break;

}

bool OverlapDialMaster::gotDigit(char digit)
{
    Lock lock(s_mutex);
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return false;

    m_collected << digit;
    Debug(&__plugin,DebugCall,"Call '%s' got DTMF '%c', collected so far: '%s'",
	    peer->id().c_str(),digit,m_collected.c_str());

    String route;
    // TODO: implement timeout-based dialing
    if(checkCollectedNumber(route)) {
	Debug(&__plugin,DebugCall,"Call '%s': collected number check passed", peer->id().c_str());
	if(! m_msg->getBoolValue("overlapped", true))
	    sendProgress(); // seize dialing immediately if requested
	if(switchCall(route)) {
	    // successfully switched
	} else {
	    // error switching
	    disconnect("can't connect");
	}
	return false;
    } else {
	if(m_collected.length() >= m_len_max) {
	    // number is too long
	    disconnect("wrong number");
	    return false;
	}
    }
    return true; // want more digits
}

void OverlapDialMaster::grabLengths(Message & m)
{
    m_len_min   = m.getIntValue("minnumlen", m_len_min);
    m_len_max   = m.getIntValue("maxnumlen", m_len_max);
    m_len_fixed = m.getIntValue("numlength", m_len_fixed);
    m.clearParam("minnumlen");
    m.clearParam("maxnumlen");
    m.clearParam("numlength");
}

bool OverlapDialMaster::checkCollectedNumber(String & route)
{
    // Check number length
    if(m_len_fixed && m_collected.length() != m_len_fixed)
	return false;
    if(m_len_min && m_collected.length() < m_len_min)
	return false;
    m_msg->clearParam("callto");
    m_msg->setParam("called",m_collected);
    m_msg->retValue().clear();
    *m_msg = "call.route";
    bool ok = Engine::dispatch(*m_msg);
    if(ok) {
	if(m_msg->retValue().startsWith(MOD_PREFIX, true)) {
	    grabLengths(*m_msg);
	    return false;
	} else
	    route = m_msg->retValue();
    }
    m_msg->retValue().clear();
    return ok;
}

bool OverlapDialMaster::switchCall(const String & route)
{
    RefPointer<CallEndpoint> peer = getPeer();
    Debug(&__plugin,DebugCall,"Switching call '%s' to %s", peer->id().c_str(), route.c_str());
#if 0
    *m_msg = "chan.masquerade";
    m_msg->setParam("id",peer->id());
    m_msg->setParam("message","call.execute");
    m_msg->setParam("callto",route);
    m_msg->retValue().clear();
    if (Engine::dispatch(*m_msg)) {
	return true;
    }
    return false;
#else
    Message * m = new Message(*m_msg);
    *m = "chan.masquerade";
    m->setParam("id",peer->id());
    m->setParam("message","call.execute");
    m->setParam("callto",route);
    m->retValue().clear();
    Engine::enqueue(m);
    return true;
#endif
}

void OverlapDialMaster::sendProgress()
{
    RefPointer<CallEndpoint> peer = getPeer();
#if 0
    Message* m = new Message("call.progress");
    m->addParam("id",id());
    m->addParam("targetid",getPeerId());
    Engine::enqueue(m);
#else
    Message m("call.progress");
    m.addParam("id",id());
    m.addParam("targetid",getPeerId());
    Engine::dispatch(m);
#endif
}



//=======================

OverlapDialModule::OverlapDialModule()
    : Module(MOD_PREFIX,"misc")
{
    Output("Loaded module OverlapDialer");
}

OverlapDialModule::~OverlapDialModule()
{
    Output("Unloading module OverlapDialer");
}

void OverlapDialModule::initialize()
{
    Output("Initializing module OverlapDialer");
    setup();
    installRelay(Execute);
    installRelay(Tone);
}

bool OverlapDialModule::unload()
{
    Lock lock(s_mutex,500000);
    if (!lock.locked())
	return false;
    if (s_calls.count())
	return false;
    uninstallRelays();
    return true;
}

void OverlapDialModule::statusParams(String& str)
{
    s_mutex.lock();
    str.append("total=",",") << s_current << ",active=" << s_calls.count();
    s_mutex.unlock();
}

bool OverlapDialModule::msgExecute(Message& msg)
{
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch)
	return false;
    String dest(msg.getParam("callto"));
    if (!dest.startSkip(MOD_PREFIX))
	return false;
    OverlapDialMaster* master = new OverlapDialMaster(dest);
    bool ok = false;
    if (master->connect(ch,msg.getValue("reason")))
	ok = master->startWork(msg);
    master->deref();
    return ok;
}

bool OverlapDialModule::msgToMaster(Message& msg)
{
    String dest(msg.getParam("targetid"));
    if (!dest.startsWith(MOD_PREFIX "/"))
	return false;
    s_mutex.lock();
    // the master will be kept referenced until we finish the work
    RefPointer<OverlapDialMaster> m = static_cast<OverlapDialMaster*>(s_calls[dest]);
    s_mutex.unlock();
    if (m) {
	m->msgDTMF(msg);
	return true;
    }
    return false;
}

bool OverlapDialModule::received(Message& msg, int id)
{
    switch (id) {
	case Execute:
	    return msgExecute(msg);
	case Tone:
	    return msgToMaster(msg);
	default:
	    return Module::received(msg,id);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */


