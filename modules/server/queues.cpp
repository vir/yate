/**
 * queues.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Call distribution and queues with settings from a database.
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

static ObjList s_queues;

class QueuedCall : public String
{
public:
    inline QueuedCall(const String& id, const char* caller = 0)
	: String(id), m_caller(caller)
	{ m_last = m_time = Time::now(); }
    inline int waitingTime(u_int64_t when = Time::now())
	{ return (int)(when - m_time); }
    inline int waitingLast(u_int64_t when = Time::now())
	{ return (int)(when - m_last); }
    inline const String& getMarked() const
	{ return m_marked; }
    inline void setMarked(const char* mark = 0)
	{ m_marked = mark; m_last = Time::now(); }
    inline const String& getCaller() const
	{ return m_caller; }

protected:
    String m_caller;
    String m_marked;
    u_int64_t m_time;
    u_int64_t m_last;
};

class CallsQueue : public NamedList
{
public:
    static CallsQueue* create(const char* name, const NamedList& params);
    ~CallsQueue();
    inline int countCalls() const
	{ return m_calls.count(); }
    inline QueuedCall* findCall(const String& id) const
	{ return static_cast<QueuedCall*>(m_calls[id]); }
    bool addCall(Message& msg);
    inline bool removeCall(const String& id)
	{ return removeCall(findCall(id)); }
    bool removeCall(QueuedCall* call);
    QueuedCall* markCall(const char* mark);
    bool unmarkCall(const String& id);
    unsigned int unmarkedCalls() const;
    QueuedCall* topCall() const;
    void listCalls(String& retval);
    void startACD();
protected:
    ObjList m_calls;
private:
    CallsQueue(const char* name);
};

class QueuesModule : public Module
{
public:
    QueuesModule();
    ~QueuesModule();
    bool unload();
    inline static CallsQueue* findQueue(const String& name)
	{ return static_cast<CallsQueue*>(s_queues[name]); }
    static CallsQueue* findCallQueue(const String& id);
protected:
    virtual void initialize();
    virtual void statusParams(String& str);
    virtual bool received(Message& msg, int id);
    virtual void msgTimer(Message& msg);
    virtual bool commandExecute(String& retVal, const String& line);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    void onExecute(Message& msg, String callto);
    void onAnswered(Message& msg, String targetid, String reason);
    void onHangup(Message& msg, String id);
    void onQueued(Message& msg, String qname);
    void onPickup(Message& msg, String qname);
private:
    bool m_init;
};


INIT_PLUGIN(QueuesModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}

static String s_account;
static String s_chanOutgoing;
static String s_chanIncoming;
static String s_queryQueue;
static String s_queryAvail;
static u_int32_t s_nextTime = 0;
static int s_rescan = 5;

static void copyArrayParams(NamedList& params, Array* a, int row)
{
    if ((!a) || (!row))
	return;
    for (int i = 0; i < a->getColumns(); i++) {
	String* name = YOBJECT(String,a->get(i,0));
	if (!(name && *name))
	    continue;
	String* value = YOBJECT(String,a->get(i,row));
	if (!value)
	    continue;
	params.setParam(*name,*value);
    }
}


CallsQueue::CallsQueue(const char* name)
    : NamedList(name)
{
    Debug(&__plugin,DebugInfo,"Creating queue '%s'",name);
    setParam("queue",name);
    s_queues.append(this);
}

CallsQueue::~CallsQueue()
{
    Debug(&__plugin,DebugInfo,"Deleting queue '%s'",c_str());
    s_queues.remove(this,false);
}

CallsQueue* CallsQueue::create(const char* name, const NamedList& params)
{
    String query = s_queryQueue;
    params.replaceParams(query,true);
    Message m("database");
    m.addParam("account",s_account);
    m.addParam("query",query);
    if (!Engine::dispatch(m)) {
	Debug(&__plugin,DebugWarn,"Query on '%s' failed: '%s'",s_account.c_str(),query.c_str());
	return 0;
    }
    Array* res = static_cast<Array*>(m.userObject("Array"));
    if (!res || (m.getIntValue("rows") != 1)) {
	Debug(&__plugin,DebugWarn,"Missing queue '%s'",name);
	return 0;
    }
    CallsQueue* queue = new CallsQueue(name);
    copyArrayParams(*queue,res,1);
    return queue;
}

bool CallsQueue::addCall(Message& msg)
{
    int maxlen = getIntValue("length");
    if ((maxlen > 0) && (countCalls() >= maxlen)) {
	Debug(&__plugin,DebugWarn,"Queue '%s' is full",c_str());
	return false;
    }
    String tmp = getValue("greeting");
    if (tmp) {
	if (tmp.find('/') < 0)
	    tmp = "wave/play/sounds/" + tmp;
	msg.setParam("greeting",tmp);
    }
    tmp = getValue("onhold");
    if (tmp.find('/') < 0) {
	if (tmp)
	    tmp = "moh/" + tmp;
	else
	    tmp = "tone/ring";
    }
    msg.setParam("source",tmp);
    msg.setParam("callto",s_chanIncoming);
    m_calls.append(new QueuedCall(msg.getValue("id"),msg.getValue("caller")));
    return true;
}

bool CallsQueue::removeCall(QueuedCall* call)
{
    if (!call)
	return false;
    m_calls.remove(call);
    if (!m_calls.count())
	destruct();
    return true;
}

QueuedCall* CallsQueue::markCall(const char* mark)
{
    ObjList* l = m_calls.skipNull();
    for (; l; l=l->skipNext()) {
	QueuedCall* call = static_cast<QueuedCall*>(l->get());
	if (call->getMarked().null()) {
	    call->setMarked(mark);
	    return call;
	}
    }
    return 0;
}

bool CallsQueue::unmarkCall(const String& id)
{
    QueuedCall* call = findCall(id);
    if (!call)
	return false;
    call->setMarked();
    return true;
}

unsigned int CallsQueue::unmarkedCalls() const
{
    unsigned int c = 0;
    ObjList* l = m_calls.skipNull();
    for (; l; l=l->skipNext()) {
	if (static_cast<QueuedCall*>(l->get())->getMarked().null())
	    c++;
    }
    return c;
}

QueuedCall* CallsQueue::topCall() const
{
    ObjList* l = m_calls.skipNull();
    return l ? static_cast<QueuedCall*>(l->get()) : 0;
}

void CallsQueue::listCalls(String& retval)
{
    retval.append("Queue ","\r\n") << c_str() << " " << countCalls();
    u_int64_t when = Time::now();
    ObjList* l = m_calls.skipNull();
    for (; l; l=l->skipNext()) {
	QueuedCall* call = static_cast<QueuedCall*>(l->get());
	retval << "\r\n  " << *call << " " << call->getCaller();
	retval << " (" << (call->waitingLast(when) / 1000000);
	retval << "/" << (call->waitingTime(when) / 1000000) << ")";
	if (call->getMarked())
	    retval << " => " << call->getMarked();
    }
    retval << "\r\n";
}

void CallsQueue::startACD()
{
    if (s_account.null() || s_chanOutgoing.null())
	return;
    unsigned int cnt = unmarkedCalls();
    if (!cnt)
	return;

    // how many operators are required to handle calls in queue
    setParam("required",String(cnt));
    // how many total calls are waiting in queue
    setParam("waiting",String(m_calls.count()));
    String query = s_queryAvail;
    replaceParams(query,true);
    Message msg("database");
    msg.addParam("account",s_account);
    msg.addParam("query",query);
    if (!Engine::dispatch(msg)) {
	Debug(&__plugin,DebugWarn,"Query on '%s' failed: '%s'",s_account.c_str(),query.c_str());
	return;
    }
    Array* res = static_cast<Array*>(msg.userObject("Array"));
    if (!res || (msg.getIntValue("rows") < 1))
	return;
    int timeout = getIntValue("timeout");
    String prompt = getValue("prompt");
    if (prompt && (prompt.find('/') < 0))
	prompt = "wave/play/sounds/" + prompt;
    for (int i = 1; i < res->getRows(); i++) {
	NamedList params("");
	copyArrayParams(params,res,i);
	const char* callto = params.getValue("callto");
	const char* user = params.getValue("username");
	if (!(callto && user))
	    continue;
	QueuedCall* call = markCall(user);
	// if we failed to pick a waiting call we are done
	if (!call)
	    break;
	Debug(&__plugin,DebugInfo,"Distributing call '%s' to '%s' in group '%s'",
	    call->c_str(),user,c_str());
	Message* ex = new Message("call.execute");
	ex->addParam("direct",callto);
	ex->addParam("target",user);
	ex->addParam("caller",call->getCaller());
	ex->addParam("called",user);
	ex->addParam("callto",s_chanOutgoing);
	ex->addParam("notify",*call);
	ex->addParam("queue",c_str());
	if (timeout > 0)
	    ex->addParam("maxcall",String(timeout*1000));
	if (prompt)
	    msg.setParam("prompt",prompt);
	Engine::enqueue(ex);
    }
}


QueuesModule::QueuesModule()
    : Module("queues","misc"), m_init(false)
{
    Output("Loaded module Queues");
}

QueuesModule::~QueuesModule()
{
    Output("Unloading module Queues");
}

bool QueuesModule::unload()
{
    if (!lock(500000))
	return false;
    uninstallRelays();
    unlock();
    s_queues.clear();
    return true;
}

void QueuesModule::statusParams(String& str)
{
    str.append("queues=",",") << s_queues.count();
}

void QueuesModule::onQueued(Message& msg, String qname)
{
    if (qname.null() || (qname.find('/') >= 0))
	return;
    if (s_account.null() || s_chanIncoming.null())
	return;
    msg.setParam("queue",qname);
    CallsQueue* queue = findQueue(qname);
    if (!queue)
	queue = CallsQueue::create(qname,msg);
    if (!queue) {
	msg.setParam("error","noroute");
	msg.setParam("reason","Queue does not exist");
	return;
    }
    if (queue->addCall(msg))
	queue->startACD();
    else {
	msg.setParam("error","congestion");
	msg.setParam("reason","Queue is full");
    }
}

void QueuesModule::onPickup(Message& msg, String qname)
{
    if (qname.null() || (qname.find('/') >= 0))
	return;
    CallsQueue* queue = findQueue(qname);
    if (queue) {
	QueuedCall* call = queue->topCall();
	if (call) {
	    String id = *call;
	    String pid = msg.getValue("id");
	    queue->removeCall(call);
	    msg = "chan.connect";
	    msg.setParam("targetid",id);
	    // a little late... but answer to the queued call
	    Message* m = new Message("call.answered");
	    m->setParam("id",pid);
	    m->setParam("targetid",id);
	    Engine::enqueue(m);
	    // also answer the pickup call
	    m = new Message("call.answered");
	    m->setParam("id",id);
	    m->setParam("targetid",pid);
	    Engine::enqueue(m);
	    return;
	}
    }
    msg.setParam("error","nocall");
    msg.setParam("reason","There are no calls in queue");
}

void QueuesModule::onExecute(Message& msg, String callto)
{
    if (callto.startSkip("queue/",false))
	onQueued(msg,callto);
    else if (callto.startSkip("pickup/",false))
	onPickup(msg,callto);
}

void QueuesModule::onAnswered(Message& msg, String targetid, String reason)
{
    if (reason == "queued")
	return;
    CallsQueue* queue = findCallQueue(targetid);
    if (!queue)
	return;
    Debug(this,DebugCall,"Answered call '%s' in queue '%s'",
	targetid.c_str(),queue->c_str());
    queue->removeCall(targetid);
}

void QueuesModule::onHangup(Message& msg, String id)
{
    String notify = msg.getValue("notify");
    String qname = msg.getValue("queue");
    if (notify && qname) {
	CallsQueue* queue = findQueue(qname);
	if (queue) {
	    Debug(this,DebugCall,"Hung up outgoing call '%s' serving '%s' in '%s'",
		id.c_str(),notify.c_str(),qname.c_str());
	    if (queue->unmarkCall(notify)) {
		queue->startACD();
		return;
	    }
	}
    }
    CallsQueue* queue = findCallQueue(id);
    if (!queue)
	return;
    Debug(this,DebugCall,"Hung up call '%s' in '%s'",id.c_str(),queue->c_str());
    queue->removeCall(id);
}

bool QueuesModule::commandExecute(String& retVal, const String& line)
{
    if (line == "queues") {
	lock();
	ObjList* l = s_queues.skipNull();
	for (; l; l=l->skipNext())
	    static_cast<CallsQueue*>(l->get())->listCalls(retVal);
	unlock();
	return true;
    }
    return false;
}

bool QueuesModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if ((partLine.null() || (partLine == "status") || (partLine == "debug")) && name().startsWith(partWord))
	msg.retValue().append(name(),"\t");
    return false;
}

bool QueuesModule::received(Message& msg, int id)
{
    Lock lock(this);
    switch (id) {
	case Execute:
	    onExecute(msg,msg.getValue("callto"));
	    break;
	case Answered:
	    onAnswered(msg,msg.getValue("targetid"),msg.getValue("reason"));
	    break;
	case Private:
	    onHangup(msg,msg.getValue("id"));
	    break;
	default:
	    lock.drop();
	    return Module::received(msg,id);
    }
    return false;
}

void QueuesModule::msgTimer(Message& msg)
{
    u_int32_t t = msg.msgTime().sec();
    if (t >= s_nextTime) {
	// we rescan queues every 5 seconds
	s_nextTime = t + s_rescan;
	ObjList* l = s_queues.skipNull();
        for (; l; l=l->skipNext())
            static_cast<CallsQueue*>(l->get())->startACD();
    }
    Module::msgTimer(msg);
}

CallsQueue* QueuesModule::findCallQueue(const String& id)
{
    ObjList* l = s_queues.skipNull();
    for (; l; l=l->skipNext()) {
	CallsQueue* queue = static_cast<CallsQueue*>(l->get());
	if (queue->findCall(id))
	    return queue;
    }
    return 0;
}

void QueuesModule::initialize()
{
    Output("Initializing module Queues for database");
    Configuration cfg(Engine::configFile("queues"));
    lock();
    s_rescan = cfg.getIntValue("general","rescan",5);
    if (s_rescan < 2)
	s_rescan = 2;
    s_account = cfg.getValue("general","account");
    s_chanOutgoing = cfg.getValue("channels","outgoing");
    s_chanIncoming = cfg.getValue("channels","incoming");
    s_queryQueue = cfg.getValue("queries","queue");
    s_queryAvail = cfg.getValue("queries","avail");
    unlock();
    if (m_init)
	return;
    m_init = true;
    setup();
    int priority = cfg.getIntValue("general","priority",50);
    installRelay(Execute,priority);
    installRelay(Answered,priority);
    installRelay(Private,"chan.hangup",priority);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
