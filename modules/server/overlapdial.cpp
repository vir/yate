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
static unsigned int s_timeout = 2500;

class TimerThread : public Thread
{
public:
    class EventReceiver
    {
    public:
	virtual ~EventReceiver() { }
	virtual void TimerEvent() = 0;
    };
    struct QueuedEvent
    {
	EventReceiver * m_receiver;
	uint64_t m_when;
	QueuedEvent * m_next;
	QueuedEvent(EventReceiver * r, uint64_t when): m_receiver(r), m_when(when), m_next(NULL) { }
	void fire() {
	    m_receiver->TimerEvent();
	}
    };
public:
    TimerThread()
	: Thread("Overlapdial timer thread")
	, m_mutex(true, "Overlapdial timer mutex")
	, m_signal(1, "Overlapdial timer signal")
	, m_events(NULL)
    {
    }
    ~TimerThread()
    {
	QueuedEvent * e = m_events;
	while(e) {
	    m_events = e->m_next;
	    delete e;
	    e = m_events;
	}
    }
    void add(EventReceiver * receiver, unsigned int when)
    {
	Lock lock(m_mutex);
	uint64_t event_time = Time::now() + when;
	QueuedEvent ** t = &m_events;
	while(*t) {
	    if((*t)->m_when > event_time)
		break;
	    t = &(*t)->m_next;
	}
	QueuedEvent * e = new QueuedEvent(receiver, event_time);
	e->m_next = *t;
	*t = e;
	if(t == &m_events)
	    update_timer();
    }
    void del(EventReceiver * receiver)
    {
	Lock lock(m_mutex);
	QueuedEvent ** t = &m_events;
	while(*t) {
	    if((*t)->m_receiver == receiver) {
		QueuedEvent * e = *t;
		*t = e->m_next;
		delete e;
		if(t == &m_events)
		    update_timer();
	    } else
		t = &(*t)->m_next;
	}
    }
    void shutdown()
    {
	m_mutex.lock();
	cancel(false); // set exit flag
	m_signal.unlock();
	m_mutex.unlock();
	m_running.lock(); // wait for worker thread shutdown
	m_running.unlock();
    }
private:
    inline void update_timer()
    {
	m_signal.unlock();
    }
    inline long get_next_event_delay() const
    {
	if(! m_events)
	    return 5000000;
	long r = m_events->m_when - Time::now();
	if(r >= 0)
	    return r;
	return 0;
    }
    virtual void run()
    {
	m_signal.lock(0);
	Lock running_lock(m_running);
	long wait;
	wait = get_next_event_delay();
	for(;;) {
	    Debug(DebugCall,"TimerThread::run(): waiting for %ld uS", wait);
	    bool b = m_signal.lock(wait); // wait for event or timeout
	    Debug(DebugCall,"TimerThread::run(): got %s! (events: %p)", b ? "event" : "timeout", m_events);
	    m_mutex.lock();
	    if(Engine::exiting() || check(false)) // check exit flag
		break;
	    while(m_events && m_events->m_when <= Time::now()) {
		QueuedEvent * e = m_events;
		m_events = e->m_next;
		m_mutex.unlock();
		e->fire();
		delete e;
		m_mutex.lock();
	    }
	    wait = get_next_event_delay();
	    m_mutex.unlock();
	}
	m_mutex.unlock(); // locked just before 'break' a fiew lines above
	m_signal.unlock();
	Debug(DebugCall,"TimerThread::run(): worker thread exiting, bye-bye!");
    }
    Mutex m_mutex, m_running;
    Semaphore m_signal;
    QueuedEvent * m_events;
};

class OverlapDialModule;
class OverlapDialMaster : public CallEndpoint, public TimerThread::EventReceiver
{
    enum CheckNumResult { NeedMore, Complete, Error };
public:
    OverlapDialMaster(OverlapDialModule& module, const String & dest);
    virtual ~OverlapDialMaster();
    bool startWork(Message& msg);
    void msgDTMF(Message& msg);
    bool gotDigit(char digit);
    CheckNumResult checkCollectedNumber(bool timeout = false);
    bool checkCollectedNumberOuter(bool timeout);
    bool switchCall();
    void sendProgress();
protected:
    virtual void TimerEvent();
    void updateParams(Message & m);
    void startStopTimer(bool start);
    OverlapDialModule& m_module; // reference to the module object
    Message* m_msg; // copy of original call.exeute message
    String m_dest; // callto tail
    String m_collected; // collected dialed number
    unsigned int m_len_min, m_len_max, m_len_fix, m_timeout;
    String m_route;
};

class OverlapDialModule : public Module
{
public:
    OverlapDialModule();
    virtual ~OverlapDialModule();
    bool unload();
    TimerThread* timer() { return m_timer; }
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    bool msgExecute(Message& msg);
    bool msgToMaster(Message& msg);
private:
    TimerThread * m_timer;
};

INIT_PLUGIN(OverlapDialModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}


OverlapDialMaster::OverlapDialMaster(OverlapDialModule& module, const String & dest)
    : m_module(module), m_msg(0), m_dest(dest)
    , m_len_min(0), m_len_max(0), m_len_fix(0), m_timeout(0)
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
    if(m_module.timer())
	m_module.timer()->del(this);
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
    m_timeout = s_timeout;
    s_mutex.unlock();
    updateParams(*m_msg);
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

void OverlapDialMaster::TimerEvent()
{
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return;
    Debug(&__plugin,DebugCall,"Call '%s' overlap dial timeout, collected: '%s'",
	    peer->id().c_str(),m_collected.c_str());
    OverlapDialMaster::checkCollectedNumberOuter(true);
}

bool OverlapDialMaster::gotDigit(char digit)
{
    //Lock lock(s_mutex);
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return false;

    m_collected << digit;
    Debug(&__plugin,DebugCall,"Call '%s' got '%c', collected: '%s'",
	    peer->id().c_str(),digit,m_collected.c_str());
    return OverlapDialMaster::checkCollectedNumberOuter(false);
}

void OverlapDialMaster::startStopTimer(bool start)
{
    if(! m_module.timer())
	return;
    m_module.timer()->del(this);
    if(start) {
	Debug(&__plugin,DebugCall,"Call '%s' overlap dial timer started, %u milliseconds", getPeer()->id().c_str(), m_timeout);
	m_module.timer()->add(this, m_timeout * 1000); // microseconds
    }
}

void OverlapDialMaster::updateParams(Message & m)
{
    m_len_min = m.getIntValue("minnumlen", m_len_min);
    m_len_max = m.getIntValue("maxnumlen", m_len_max);
    m_len_fix = m.getIntValue("fixnumlen", m.getIntValue("numlength", 0));
    m_timeout = m.getIntValue("numtimeout", m_timeout);
    m.clearParam("minnumlen");
    m.clearParam("maxnumlen");
    m.clearParam("fixnumlen");
    m.clearParam("numlength");
    m.clearParam("numtimeout");
    if(m_timeout <= 300)
	m_timeout *= 1000; // convert to milliseconds
}

OverlapDialMaster::CheckNumResult OverlapDialMaster::checkCollectedNumber(bool timeout)
{
    if(m_len_fix && m_collected.length() > m_len_fix)
	return Error;
    if(m_len_max && m_collected.length() > m_len_max)
	return Error;
    if(m_len_fix && m_collected.length() < m_len_fix)
	return NeedMore;
    if(m_len_min && m_collected.length() < m_len_min)
	return NeedMore;
    if(!m_len_fix && !timeout) // collect digits till timeout
	return NeedMore;

    // Sanity checks passed, let's ask someone else what to do now
    m_msg->clearParam("callto");
    m_msg->setParam("called",m_collected);
    m_msg->retValue().clear();
    *m_msg = "call.route";
    bool ok = Engine::dispatch(*m_msg);
    if(ok) {
	if(m_msg->retValue().startsWith(MOD_PREFIX, true)) {
	    updateParams(*m_msg);
	    return NeedMore;
	} else
	    m_route = m_msg->retValue();
    }
    m_msg->retValue().clear();
    return ok ? Complete : NeedMore;
}

bool OverlapDialMaster::checkCollectedNumberOuter(bool timeout)
{
    switch(checkCollectedNumber(timeout)) {
    case Complete:
	if(! m_msg->getBoolValue("overlapped", true))
	    sendProgress(); // seize dialing immediately if requested
	if(switchCall()) {
	    // successfully switched
	} else {
	    // error switching
	    disconnect("can't connect");
	}
	return false;
    case Error:
	disconnect("wrong number");
	return false;
    case NeedMore:
	startStopTimer(! m_len_fix);
	return true; // want more digits
    }
    return true; // should never happen
}

bool OverlapDialMaster::switchCall()
{
    RefPointer<CallEndpoint> peer = getPeer();
    Debug(&__plugin,DebugCall,"Switching call '%s' to %s", peer->id().c_str(), m_route.c_str());
    Message * m = new Message(*m_msg);
    *m = "chan.masquerade";
    m->setParam("id",peer->id());
    m->setParam("message","call.execute");
    m->setParam("callto",m_route);
    m->retValue().clear();
    Engine::enqueue(m);
    return true;
}

void OverlapDialMaster::sendProgress()
{
    RefPointer<CallEndpoint> peer = getPeer();
    Message m("call.progress");
    m.addParam("id",id());
    m.addParam("targetid",getPeerId());
    Engine::dispatch(m);
}



//=======================

OverlapDialModule::OverlapDialModule()
    : Module(MOD_PREFIX,"misc")
    , m_timer(NULL)
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
    if(! m_timer) {
	m_timer = new TimerThread;
	if(! m_timer->startup()) {
	    delete m_timer;
	    m_timer = NULL;
	    Debug(&__plugin, DebugGoOn, "Error starting timer thread");
	}
    }
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
    if(m_timer)
	m_timer->shutdown();
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
    OverlapDialMaster* master = new OverlapDialMaster(*this, dest);
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


