/**
 * queues.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Call distribution and queues with settings from a database.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

static ObjList s_queues;

class QueuedCall : public NamedList
{
public:
    inline QueuedCall(const String& id, const NamedList& params, const String& copyNames)
	: NamedList(id)
	{ copyParams(params,copyNames); m_last = m_time = Time::now(); }
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
    void complete(Message& msg, bool addId = true) const;

protected:
    String m_caller;
    String m_marked;
    String m_billid;
    String m_callerName;
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
    inline QueuedCall* findCall(unsigned int index) const
	{ return static_cast<QueuedCall*>(m_calls[index]); }
    bool addCall(Message& msg);
    inline int removeCall(const String& id, const char* reason)
	{ return removeCall(findCall(id),reason); }
    int removeCall(QueuedCall* call, const char* reason);
    QueuedCall* markCall(const char* mark);
    bool unmarkCall(const String& id);
    void countCalls(unsigned int& marked, unsigned int& unmarked) const;
    QueuedCall* topCall() const;
    int position(const QueuedCall* call) const;
    void listCalls(String& retval);
    void startACD();
    void complete(Message& msg) const;
protected:
    void notify(const char* event, const QueuedCall* call = 0);
    ObjList m_calls;
    u_int64_t m_time;
    u_int64_t m_rate;
private:
    CallsQueue(const char* name);
    CallsQueue(const NamedList& params, const char* name);
    void init();
    const char* m_notify;
    bool m_single;
    bool m_detail;
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
    bool onDrop(Message& msg, String qname);
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

static Configuration s_cfg;
static String s_account;
static String s_chanOutgoing;
static String s_chanIncoming;
static String s_queryQueue;
static String s_queryAvail;
static u_int32_t s_nextTime = 0;
static int s_rescan = 5;
static int s_mintime = 500;

// Utility function, copy all columns to parameters with same name
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


// Fill message with parameters about the call
void QueuedCall::complete(Message& msg, bool addId) const
{
    msg.copyParams(*this);
    if (addId) {
	msg.setParam("id",c_str());
	if (m_marked)
	    msg.setParam("operator",m_marked);
    }
}


// Constructor from database query, parameters are populated later
CallsQueue::CallsQueue(const char* name)
    : NamedList(name),
      m_time(0), m_rate(0),
      m_notify(0), m_single(false), m_detail(false)
{
    Debug(&__plugin,DebugInfo,"Creating queue '%s' from database",name);
    setParam("queue",name);
    s_queues.append(this);
}

// Constructor from config file section, copy parameters from it
CallsQueue::CallsQueue(const NamedList& params, const char* name)
    : NamedList(params),
      m_time(0), m_rate(0),
      m_notify(0), m_single(false), m_detail(false)
{
    Debug(&__plugin,DebugInfo,"Creating queue '%s' from config file",name);
    String::operator=(name);
    setParam("queue",name);
    s_queues.append(this);
}

// Destructor, forget about this queue
CallsQueue::~CallsQueue()
{
    Debug(&__plugin,DebugInfo,"Deleting queue '%s'",c_str());
    s_queues.remove(this,false);
    notify("destroyed");
}

// Create a queue, either from database or from config file
CallsQueue* CallsQueue::create(const char* name, const NamedList& params)
{
    NamedList* sect = s_cfg.getSection("queue " + String(name));
    if (sect && sect->getBoolValue("enabled",true)) {
	// configure queue parameters from file
	CallsQueue* queue = new CallsQueue(*sect,name);
	queue->init();
	return queue;
    }

    if (s_account.null() || s_queryQueue.null())
	return 0;
    String query = s_queryQueue;
    params.replaceParams(query,true);
    Message m("database");
    m.addParam("account",s_account);
    m.addParam("query",query);
    if (!Engine::dispatch(m)) {
	Debug(&__plugin,DebugWarn,"Query on '%s' failed: '%s'",s_account.c_str(),query.c_str());
	return 0;
    }
    Array* res = static_cast<Array*>(m.userObject(YATOM("Array")));
    if (!res || (m.getIntValue(YSTRING("rows")) != 1)) {
	Debug(&__plugin,DebugWarn,"Missing queue '%s'",name);
	return 0;
    }
    CallsQueue* queue = new CallsQueue(name);
    copyArrayParams(*queue,res,1);
    queue->init();
    return queue;
}

// Initialize the queue variables from its parameters
void CallsQueue::init()
{
    int rate = getIntValue("mintime",s_mintime);
    if (rate > 0)
	m_rate = rate * (u_int64_t)1000;
    m_single = getBoolValue("single");
    m_detail = getBoolValue("detail");
    m_notify = getValue("notify");
    notify("created");
}

// Attempt to add a new call to the tail of the queue
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
    int pos = -1;
    QueuedCall* call = new QueuedCall(msg.getValue("id"),msg,
	msg.getValue("copyparams",getValue("copyparams","caller,callername,billid")));
    // high priority calls will go in queue's head instead of tail
    if (msg.getBoolValue("priority")) {
	m_calls.insert(call);
	pos = 0;
	if (m_notify && m_detail) {
	    // all other calls' position in queue changed - notify
	    ObjList* l = m_calls.skipNull();
	    for (; l; l=l->skipNext()) {
		QueuedCall* c = static_cast<QueuedCall*>(l->get());
		if (c != call)
		    notify("position",c);
	    }
	}
    }
    else {
	m_calls.append(call);
	pos = position(call);
    }
    notify("queued",call);
    if (pos >= 0)
	msg.setParam("position",String(pos));
    return true;
}

// Remove and destroy call from the queue, destroy the queue if it becomes empty
int CallsQueue::removeCall(QueuedCall* call, const char* reason)
{
    if (!call)
	return -1;
    int waited = (call->waitingTime() + 500000) / 1000000;
    notify(reason,call);
    int pos = m_detail ? position(call) : -1;
    m_calls.remove(call);
    if (!m_calls.count())
	destruct();
    else if (pos >= 0) {
	// some calls position in queue changed - notify
	for (int n = m_calls.length(); pos < n; pos++) {
	    QueuedCall* c = static_cast<QueuedCall*>(m_calls[pos]);
	    if (c)
		notify("position",c);
	}
    }
    return waited;
}

// Mark a call as being routed to an operator
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

// Unmark a call when the operator call failed
bool CallsQueue::unmarkCall(const String& id)
{
    QueuedCall* call = findCall(id);
    if (!call)
	return false;
    if (m_single) {
	removeCall(call,"noanswer");
	Message* m = new Message("call.drop");
	m->addParam("id",id);
	m->addParam("reason","noanswer");
	Engine::enqueue(m);
	return false;
    }
    call->setMarked();
    return true;
}

// Count the number of calls routed and not routed to an operator
void CallsQueue::countCalls(unsigned int& marked, unsigned int& unmarked) const
{
    marked = unmarked = 0;
    ObjList* l = m_calls.skipNull();
    for (; l; l=l->skipNext()) {
	if (static_cast<QueuedCall*>(l->get())->getMarked())
	    marked++;
	else
	    unmarked++;
    }
}

// Retrieve the call from the head of the queue
QueuedCall* CallsQueue::topCall() const
{
    ObjList* l = m_calls.skipNull();
    return l ? static_cast<QueuedCall*>(l->get()) : 0;
}

// Get the numeric position of a call in queue, -1 if not found
int CallsQueue::position(const QueuedCall* call) const
{
    ObjList* l = m_calls.skipNull();
    for (int pos = 0; l; l=l->skipNext(), pos++)
	if (call == l->get())
	    return pos;
    return -1;
}

// List the calls currently in the queue
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

// Start the call distribution for this queue if required
void CallsQueue::startACD()
{
    if (s_account.null() || s_queryAvail.null() || s_chanOutgoing.null())
	return;
    u_int64_t when = 0;
    if (m_rate) {
	// apply minimum query interval policy
	when = Time::now();
	if (when < m_time)
	    return;
	m_time = when + m_rate;
    }

    unsigned int marked = 0;
    unsigned int unmarked = 0;
    countCalls(marked,unmarked);
    if (!unmarked)
	return;
    unsigned int required = unmarked;
    int maxout = getIntValue("maxout",-1);
    if (maxout >= 0) {
	// put a number limit on outgoing calls
	maxout -= marked;
	if (maxout <= 0)
	    return;
	if (required > (unsigned int)maxout)
	    required = maxout;
    }

    // how many operators are required to handle calls in queue
    setParam("required",String(required));
    // how many total calls are waiting in queue
    setParam("waiting",String(marked+unmarked));
    // how many calls are currently going on to operators
    setParam("current",String(marked));

    String query = s_queryAvail;
    replaceParams(query,true);
    Message msg("database");
    msg.addParam("account",s_account);
    msg.addParam("query",query);
    if (!Engine::dispatch(msg)) {
	Debug(&__plugin,DebugWarn,"Query on '%s' failed: '%s'",s_account.c_str(),query.c_str());
	return;
    }
    Array* res = static_cast<Array*>(msg.userObject(YATOM("Array")));
    if (!res || (msg.getIntValue(YSTRING("rows")) < 1))
	return;
    for (int i = 1; i < res->getRows(); i++) {
	NamedList params("");
	copyArrayParams(params,res,i);
	const char* callto = params.getValue(YSTRING("location"));
	const char* user = params.getValue(YSTRING("username"));
	if (!(callto && user))
	    continue;
	QueuedCall* call = markCall(user);
	// if we failed to pick a waiting call we are done
	if (!call)
	    break;
	Debug(&__plugin,DebugInfo,"Distributing call '%s' to '%s' in group '%s'",
	    call->c_str(),user,c_str());
	Message* ex = new Message("call.execute");
	ex->addParam("called",user);
	call->complete(*ex,false);
	ex->setParam("direct",callto);
	ex->setParam("target",user);
	ex->setParam("callto",s_chanOutgoing);
	ex->setParam("notify",*call);
	ex->setParam("queue",c_str());
	const char* tmp = params.getValue("maxcall",getValue("maxcall"));
	if (tmp)
	    ex->setParam("maxcall",tmp);
	tmp = params.getValue("prompt",getValue("prompt"));
	if (tmp)
	    ex->setParam("prompt",tmp);
	Engine::enqueue(ex);
    }
}

// Emit a queue related notification message
void CallsQueue::notify(const char* event, const QueuedCall* call)
{
    if (!m_notify)
	return;
    Message* m = new Message("chan.notify");
    if (call) {
	call->complete(*m);
	int pos = position(call);
	if (pos >= 0)
	    m->addParam("position",String(pos));
    }
    m->addParam("event",event);
    complete(*m);
    Engine::enqueue(m);
}

// Fill message with parameters about the queue
void CallsQueue::complete(Message& msg) const
{
    msg.addParam("targetid",m_notify);
    msg.addParam("queue",c_str());
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

// Prepare module for unload
bool QueuesModule::unload()
{
    if (!lock(500000))
	return false;
    uninstallRelays();
    unlock();
    s_queues.clear();
    return true;
}

// Add status report parameters
void QueuesModule::statusParams(String& str)
{
    str.append("queues=",",") << s_queues.count();
}

// Put a call in a queue, create queue if required
void QueuesModule::onQueued(Message& msg, String qname)
{
    qname.trimBlanks();
    if (qname.null() || (qname.find('/') >= 0))
	return;
    if (s_chanIncoming.null())
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

// Pick up the call from the head of a queue or a call specified by ID
void QueuesModule::onPickup(Message& msg, String qname)
{
    if (qname.null())
	return;
    String id;
    int sep = qname.find('/');
    if (sep >= 0) {
	id = qname.substr(sep+1);
	qname = qname.substr(0,sep);
    }
    CallsQueue* queue = findQueue(qname);
    if (queue) {
	QueuedCall* call = id ? queue->findCall(id) : queue->topCall();
	if (call) {
	    id = *call;
	    String pid = msg.getValue("id");
	    String waited(queue->removeCall(call,"pickup"));
	    // convert message and let it connect to the queued call
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
	    m->setParam("queuetime",waited);
	    Engine::enqueue(m);
	    return;
	}
	msg.setParam("error","nocall");
	msg.setParam("reason","The call is not in queue");
    }
}

// Handle call.execute messages that put or pick up calls in a queue
void QueuesModule::onExecute(Message& msg, String callto)
{
    if (callto.startSkip("queue/",false))
	onQueued(msg,callto);
    else if (callto.startSkip("pickup/",false))
	onPickup(msg,callto);
}

// Handle call.answered coming from operators of a queue
void QueuesModule::onAnswered(Message& msg, String targetid, String reason)
{
    if (reason == "queued")
	return;
    CallsQueue* queue = findCallQueue(targetid);
    if (!queue)
	return;
    Debug(this,DebugCall,"Answered call '%s' in queue '%s'",
	targetid.c_str(),queue->c_str());
    String waited(queue->removeCall(targetid,"answered"));
    Message* m = new Message("call.update");
    m->addParam("id",targetid);
    m->addParam("queuetime",waited);
    Engine::enqueue(m);
}

// Handle hangups on either caller or operator
void QueuesModule::onHangup(Message& msg, String id)
{
    String notify = msg.getValue("notify");
    String qname = msg.getValue("queue");
    if (notify && qname) {
	CallsQueue* queue = findQueue(qname);
	if (queue) {
	    // operator (outgoing) call failed for any reason
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
    // caller (incoming) did hung up
    Debug(this,DebugCall,"Hung up call '%s' in '%s'",id.c_str(),queue->c_str());
    queue->removeCall(id,"hangup");
}

// Drop the call from the head of a queue or a call specified by ID
bool QueuesModule::onDrop(Message& msg, String qname)
{
    if (qname.null())
	return false;
    String id;
    int sep = qname.find('/');
    if (sep >= 0) {
	id = qname.substr(sep+1);
	qname = qname.substr(0,sep);
    }
    CallsQueue* queue = findQueue(qname);
    if (queue) {
	if (id == "*") {
	    const char* reason = msg.getValue("reason");
	    for (unsigned int i = 0; ; i++) {
		QueuedCall* call = queue->findCall(i);
		if (!call)
		    break;
		Message* m = new Message("call.drop");
		m->addParam("id",*call);
		if (reason)
		    m->addParam("reason",reason);
		Engine::enqueue(m);
	    }
	    return true;
	}
	QueuedCall* call = id ? queue->findCall(id) : queue->topCall();
	if (call) {
	    id = *call;
	    Debug(this,DebugCall,"Dropping call '%s' from '%s'",
		id.c_str(),qname.c_str());
	    msg.setParam("id",id);
	}
    }
    return false;
}

// Command line execute handler
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

// Command line completion handler
bool QueuesModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if ((partLine.null() || (partLine == "status") || (partLine == "debug")) && name().startsWith(partWord))
	msg.retValue().append(name(),"\t");
    return false;
}

// Common message relay handler
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
	case Drop:
	    return onDrop(msg,msg.getValue("id"));
	default:
	    lock.drop();
	    return Module::received(msg,id);
    }
    return false;
}

// Timer message handler, rescan queues
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

// Find the queue in which a call waits
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

// (Re)Initialize the module
void QueuesModule::initialize()
{
    Output("Initializing module Queues for database");
    lock();
    s_cfg = Engine::configFile("queues");
    s_cfg.load();
    s_mintime = s_cfg.getIntValue("general","mintime",500);
    s_rescan = s_cfg.getIntValue("general","rescan",5);
    if (s_rescan < 2)
	s_rescan = 2;
    s_account = s_cfg.getValue("general","account");
    s_chanOutgoing = s_cfg.getValue("channels","outgoing");
    s_chanIncoming = s_cfg.getValue("channels","incoming");
    s_queryQueue = s_cfg.getValue("queries","queue");
    s_queryAvail = s_cfg.getValue("queries","avail");
    unlock();
    if (m_init)
	return;
    m_init = true;
    setup();
    int priority = s_cfg.getIntValue("general","priority",45);
    installRelay(Execute,s_cfg.getIntValue("priorities","call.execute",priority));
    installRelay(Answered,s_cfg.getIntValue("priorities","call.answered",priority));
    installRelay(Private,"chan.hangup",s_cfg.getIntValue("priorities","chan.hangup",priority));
    installRelay(Drop,s_cfg.getIntValue("priorities","call.drop",priority));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
