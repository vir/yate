/**
 * queuesnotify.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Keeps a list of queued calls and emits resource.notify messages
 * when their status changes
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

class QueuedCall;                        // Class holding data to be used to send initial (queued)
                                         // notifications after querying the database for additional info
class ChanNotifyHandler;                 // chan.notify handler used to catch queued calls
class CallCdrHandler;                    // call.cdr handler used to signal queued calls termination
class QueuedCallWorker;                  // Thread processing a call from queue
class QueuesNotifyModule;                // The module

// A queued call
// Process (query database) for data
class QueuedCall : public RefObject, public Mutex
{
public:
    enum Status {
	Queued = 0,
	Pickup = 1,
	Hangup = 2,
	Unknown = 3,
    };
    // Build the call
    QueuedCall(const String& queue, const String& chan, unsigned int start = Time::secNow(),
	const char* caller = 0, const char* called = 0, const char* callername = 0,
	int queuePrio = 0, int callerPrio = 0);
    // Get the queue name
    inline const String& queue() const
	{ return m_queue; }
    // Get the channel id
    inline const String& channelid() const
	{ return m_channelid; }
    // Get the caller
    inline const String& caller() const
	{ return m_caller; }
    // Set/get notify flags
    inline bool notify(int stat) const
	{ return stat < Unknown ? m_notify[stat] : false; }
    void resetNotify(int stat);
    // Set/get destroy flag
    inline bool destroy(u_int64_t now = Time::msecNow()) const
	{ return m_destroyTime && m_destroyTime < now; }
    void setDestroy(u_int64_t now = Time::msecNow());
    inline void setQueueName(const char* name)
	{ m_queueName = name; }
    // Add a priority parameter to a list
    void addPriority(NamedList& params);
    // Build a resource.notify message
    Message* buildResNotify(int stat);
    //
    virtual const String& toString() const
	{ return m_channelid; }
    // Find a queued call
    // Returns a referenced object if found
    static QueuedCall* find(const String& chan);
    // Process a queued call (query database)
    // Enqueue a message when returned from query
    static void process(QueuedCall* call);
    // Get the status value from a string
    static inline int status(const String& event)
	{ return lookup(event,s_events,Unknown); }
    // Get the status text from a value
    static inline const char* statusName(int event)
	{ return lookup(event,s_events); }
    // Event strings associated with queued call status
    static TokenDict s_events[];
protected:
    // Remove from list. Release memory
    virtual void destroyed();
private:
    String m_queue;                      // The queue
    String m_queueName;                  // Queue display name
    String m_channelid;                  // The queued channel
    int m_queuePrio;                     // Queue priority
    int m_callerPrio;                    // Caller priority
    unsigned int m_startTime;            // Call start time
    String m_caller;                     // The caller's number
    String m_called;                     // The called
    String m_callername;                 // The caller's name (used if the database query fails)
    bool m_notify[Unknown];              // Notify flags (prevent multiple notifications)
    u_int64_t m_destroyTime;             // Destroy the object without sending any notification
                                         // if the call was already terminated
};

// Thread processing a call from queue
class QueuedCallWorker : public Thread
{
public:
    QueuedCallWorker(Priority prio = Normal);
    ~QueuedCallWorker();
    virtual void run();
};

class ChanNotifyHandler;
class CallCdrHandler;

// The module
class QueuesNotifyModule : public Module
{
public:
    enum QueryType {
	CallInfo,
	CdrInfo,
    };
    QueuesNotifyModule();
    ~QueuesNotifyModule();
    // Uninstall the relays and message handlers
    bool unload();
    // Send notification from a given call
    // Reset call's notification flag
    void notifyCall(QueuedCall* call, Message* notify, int status);
    // Prepare a database message
    Message* getDBMsg(QueryType type, const char* caller);
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
private:
    bool m_init;
    String m_account;
    String m_queryCallInfo;
    String m_queryCdrInfo;
    ChanNotifyHandler* m_chanNotify;
    CallCdrHandler* m_callCdr;
};


INIT_PLUGIN(QueuesNotifyModule);
static u_int64_t s_destroyInterval = 1000;   // Destroy queued call interval in ms
static bool s_notifyHangupOnUnload = true;   // Notify hangup for queued calls when the module is unloaded
static bool s_addNodeToResource = true;  // Add node name to resource when norifying
static int s_queuedCallPriority = 0;     // The priority of a queued call
static int s_coefQueuePriority = 1;      // Queue coefficient used to calculate the priority for a queued call
static int s_coefCallPriority = 0;       // Call coefficient used to calculate the priority for a queued call
static NamedList s_resNotifStatus("");   // Events to status resource notify translation table
static QueuedCallWorker* s_thread = 0;
static Configuration s_cfg;
static ObjList s_calls;                  // Queued calls (this list must be locked using the plugin)
static int s_sleepMs = 20;               // Loop sleep time

// Event strings associated with queued call status
TokenDict QueuedCall::s_events[] = {
    { "queued",   Queued },
    { "pickup",   Pickup },
    { "answered", Pickup },
    { "hangup",   Hangup },
    { 0, 0 },
};


// Get the status string associated with a queued call status event
static inline const char* resNotifStatus(int queueEvent)
{
    Lock lock(&__plugin);
    return s_resNotifStatus.getValue(lookup(queueEvent,QueuedCall::s_events));
}

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}


// chan.notify handler used to catch queued calls
class ChanNotifyHandler : public MessageHandler
{
public:
    inline ChanNotifyHandler(int prio = 10)
	: MessageHandler("chan.notify",prio,__plugin.name())
	{}
    virtual bool received(Message& msg);
};

// call.cdr handler used to signal queued calls termination
class CallCdrHandler : public MessageHandler
{
public:
    inline CallCdrHandler(int prio = 10)
	: MessageHandler("call.cdr",prio,__plugin.name())
	{}
    virtual bool received(Message& msg);
};


/**
 * QueuedCall
 */
// A queued call
// Process (query database) for data
QueuedCall::QueuedCall(const String& queue, const String& chan, unsigned int start,
    const char* caller, const char* called, const char* callername,
    int queuePrio, int callerPrio)
    : Mutex(true,"QueuedCall"),
    m_queue(queue), m_channelid(chan),
    m_queuePrio(queuePrio), m_callerPrio(callerPrio),
    m_startTime(start), m_caller(caller),
    m_called(called), m_callername(callername),
    m_destroyTime(0)
{
    DDebug(&__plugin,DebugAll,
	"QueuedCall(%s,%s) created caller=%s called=%s callername=%s [%p]",
	queue.safe(),chan.safe(),caller,called,callername,this);
    for (int i = 0; i < Unknown; i++)
	m_notify[i] = true;
}

void QueuedCall::resetNotify(int stat)
{
    if (stat >= Unknown)
	return;
    Lock lock(this);
    if (!m_notify[stat])
	return;
    XDebug(&__plugin,DebugAll,"QueuedCall(%s,%s) reset '%s' notify flag [%p]",
	m_queue.safe(),m_channelid.safe(),lookup(stat,s_events),this);
    m_notify[stat] = false;
}

void QueuedCall::setDestroy(u_int64_t now)
{
    Lock lock(this);
    for (int i = 0; i < Unknown; i++)
	resetNotify(i);
    if (m_destroyTime)
	return;
    XDebug(&__plugin,DebugAll,"QueuedCall(%s,%s) set destroy [%p]",
	m_queue.safe(),m_channelid.safe(),this);
    m_destroyTime = now + s_destroyInterval;
}

// Add prio to a list
void QueuedCall::addPriority(NamedList& params)
{
    m_queuePrio = Random::random() % 10;
    m_callerPrio = Random::random() % 10;
    int prio = s_coefQueuePriority  * m_queuePrio + s_coefCallPriority * m_callerPrio;
    params.addParam("priority",String(prio));
}

// Build a resource.notify message
Message* QueuedCall::buildResNotify(int stat)
{
    Message* m = new Message("resource.notify");
    m->addParam("module",__plugin.name());
    if (stat != QueuedCall::Hangup)
	m->addParam("operation","online");
    else
	m->addParam("operation","offline");
    m->addParam("account",m_queue);
    m->addParam("username",m_queue);
    String res;
    if (s_addNodeToResource)
	res << Engine::nodeName() + "/";
    res << m_channelid;
    m->addParam("instance",res);
    m->addParam("show",resNotifStatus(stat));
    return m;
}

// Find a queued call
// Returns a referenced object if found
QueuedCall* QueuedCall::find(const String& chan)
{
    Lock lock(&__plugin);
    for (ObjList* o = s_calls.skipNull(); o; o = o->skipNext()) {
	QueuedCall* call = static_cast<QueuedCall*>(o->get());
	if (chan == call->m_channelid)
	    return call->ref() ? call : 0;
    }
    return 0;
}

// Utility called in QueuedCall::process()
// Dispatch a 'database' message and get the returned data on success
// Print a debug message on failure
void processQueryDB(Message* msg, QueuedCall* call, Array*& data, const char* query)
{
    data = 0;
    if (!msg)
	return;
    bool ok = Engine::dispatch(*msg);
    // Get data if message succeedded
    if (ok) {
	if (msg->getIntValue("rows") >= 1)
	    data = static_cast<Array*>(msg->userObject(YATOM("Array")));
    }
    else
	Debug(&__plugin,DebugNote,
	    "QueuedCall(%s,%s) query '%s' failed for caller=%s [%p]",
	    call->queue().safe(),call->channelid().safe(),query,
	    call->caller().c_str(),call);
    return;
}


// Process a queued call (query database)
// Enqueue a message when returned from query
// NOTE: Processed call is locked when getting its data and
//  unlocked when the database message is dispatched
void QueuedCall::process(QueuedCall* call)
{
    if (!call)
	return;

    call->lock();
    // Check if already notified
    if (!call->notify(Queued)) {
	call->unlock();
	return;
    }

    // The index of the next parameter to be added to the notify message
    String paramPrefix = __plugin.name();
    unsigned int nextParam = 1;

    // Build the resource notify message
    Message* notify = call->buildResNotify(Queued);
    notify->addParam("message-prefix",paramPrefix);

    // Add prio
    // FIXME: get priorities from database
    call->addPriority(*notify);

    // Get call info
    call->unlock();
    Message* m = __plugin.getDBMsg(QueuesNotifyModule::CallInfo,call->m_caller);
    Array* callinfo = 0;
    processQueryDB(m,call,callinfo,"callinfo");
    call->lock();
    // Check if should return without notifying
    if (!call->notify(Queued) || Engine::exiting() || Thread::check(false)) {
	call->unlock();
	TelEngine::destruct(notify);
	TelEngine::destruct(m);
	return;
    }
    // Append the callinfo
    String prefix = paramPrefix + "." + String(nextParam);
    nextParam++;
    notify->addParam(prefix,"callinfo");
    prefix << ".";
    notify->addParam(prefix + "starttime",String(call->m_startTime));
    notify->addParam(prefix + "caller",call->m_caller);
    notify->addParam(prefix + "called",call->m_called);
    if (call->m_callername)
	notify->addParam(prefix + "name",call->m_callername);
    // Set/override callinfo params
    if (callinfo && callinfo->getRows() >= 2)
	for (int col = 0; col < callinfo->getColumns(); col++) {
	    String* name = YOBJECT(String,callinfo->get(col,0));
	    String* value = YOBJECT(String,callinfo->get(col,1));
	    if (name && *name && value && *value)
		notify->setParam(prefix + *name,*value);
	}
    TelEngine::destruct(m);

    // Get CDR
    call->unlock();
    m = __plugin.getDBMsg(QueuesNotifyModule::CdrInfo,call->m_caller);
    Array* cdr = 0;
    processQueryDB(m,call,cdr,"cdrinfo");
    call->lock();
    // Check if should return without notifying
    if (!call->notify(Queued) || Engine::exiting() || Thread::check(false)) {
	call->unlock();
	TelEngine::destruct(notify);
	TelEngine::destruct(m);
	return;
    }
    if (cdr)
	for (int row = 1; row < cdr->getRows(); row++) {
	    String xmlprefix = paramPrefix + "." + String(nextParam);
	    nextParam++;
	    notify->addParam(xmlprefix,"cdr");
	    xmlprefix << ".";
	    for (int col = 0; col < cdr->getColumns(); col++) {
		String* name = YOBJECT(String,cdr->get(col,0));
		String* value = YOBJECT(String,cdr->get(col,row));
		if (name && *name && value && *value)
		    notify->addParam(xmlprefix + *name,*value);
	    }
	}
    TelEngine::destruct(m);

    // Add queue name parameter(s)
    if (!call->m_queueName)
	call->m_queueName = call->m_queue;
    if (call->m_queueName) {
	String xmlprefix = paramPrefix + "." + String(nextParam);
	nextParam++;
	notify->addParam(xmlprefix,"queue");
	xmlprefix << ".";
	notify->addParam(xmlprefix + "name",call->m_queueName);
    }

    call->unlock();
    __plugin.notifyCall(call,notify,Queued);
}

// Remove from list. Release memory
void QueuedCall::destroyed()
{
    __plugin.lock();
    s_calls.remove(this,false);
    __plugin.unlock();
    DDebug(&__plugin,DebugAll,"QueuedCall(%s,%s) destroyed [%p]",
	m_queue.safe(),m_channelid.safe(),this);
    RefObject::destroyed();
}


/**
 * ChanNotifyHandler
 */
bool ChanNotifyHandler::received(Message& msg)
{
    int status = QueuedCall::status(msg.getValue("event"));
    if (status == QueuedCall::Unknown)
	return false;

    // Get queue and queued channel
    NamedString* queue = msg.getParam("queue");
    NamedString* chan = msg.getParam("id");
    if (!(queue && chan))
	return false;

    // Find the queued call or build a new one
    DDebug(&__plugin,DebugAll,"%s event=%s queue=%s chan=%s",
	msg.c_str(),msg.getValue("event"),queue->c_str(),chan->c_str());
    QueuedCall* call = QueuedCall::find(*chan);
    if (!call) {
	if (status == QueuedCall::Queued)
	    call = new QueuedCall(*queue,*chan,msg.msgTime().sec(),
		msg.getValue("caller"),msg.getValue("called"),
		msg.getValue("callername"),
		msg.getIntValue("priority",s_queuedCallPriority));
	else
	    call = new QueuedCall(*queue,*chan);
	call->setQueueName(msg.getValue("targetid"));
	call->ref();
	__plugin.lock();
	s_calls.append(call);
	__plugin.unlock();
	return false;
    }

    call->lock();
    Message* notify = call->buildResNotify(status);
    call->unlock();

    __plugin.notifyCall(call,notify,status);
    TelEngine::destruct(call);
    return false;
}


/**
 * CallCdrHandler
 */
bool CallCdrHandler::received(Message& msg)
{
    NamedString* op = msg.getParam("operation");
    if (!(op && *op == "finalize"))
	return false;

    // Find a queued call by its channel
    NamedString* chan = msg.getParam("chan");
    QueuedCall* call = chan ? QueuedCall::find(*chan) : 0;
    if (!call)
	return false;

    DDebug(&__plugin,DebugAll,"%s op=finalize chan=%s",msg.c_str(),chan->c_str());

    call->lock();
    Message* notify = call->buildResNotify(QueuedCall::Hangup);
    call->unlock();

    __plugin.notifyCall(call,notify,QueuedCall::Hangup);
    TelEngine::destruct(call);
    return false;
}


/**
 * QueuedCallWorker
 */
QueuedCallWorker::QueuedCallWorker(Priority prio)
    : Thread("QueuedCall Worker",prio)
{
    s_thread = this;
}

QueuedCallWorker::~QueuedCallWorker()
{
    s_thread = 0;
}

void QueuedCallWorker::run()
{
    bool processed = true;
    while (true) {
	if (!processed) {
	    u_int64_t now = Time::msecNow();
	    // Remove destroyed calls
	    __plugin.lock();
	    ListIterator iter(s_calls);
	    for (GenObject* o = 0; 0 != (o = iter.get());) {
	    	QueuedCall* call = static_cast<QueuedCall*>(o);
	    	if (call->destroy(now)) {
		    // Someone might still have a reference to the call:
		    // Avoid double destruct from here
		    s_calls.remove(call,false);
		    TelEngine::destruct(call);
		}
	    }
	    __plugin.unlock();
	    Thread::msleep(s_sleepMs,true);
	}
	else
	    Thread::yield(true);
	// Pick a call to process
	processed = false;
	__plugin.lock();
	QueuedCall* call = 0;
	for (ObjList* o = s_calls.skipNull(); o; o = o->skipNext()) {
	    call = static_cast<QueuedCall*>(o->get());
	    if (call->notify(QueuedCall::Queued) && call->ref())
		break;
	    call = 0;
	}
	__plugin.unlock();
	if (!call)
	    continue;
	// Process the call
	QueuedCall::process(call);
	TelEngine::destruct(call);
	processed = true;
    }
}


/**
 * QueuesNotifyModule
 */
QueuesNotifyModule::QueuesNotifyModule()
    : Module("queuesnotify","misc"),
    m_init(false), m_chanNotify(0), m_callCdr(0)
{
    Output("Loaded module Queues Notify");
}

QueuesNotifyModule::~QueuesNotifyModule()
{
    Output("Unloading module Queues Notify");
}

// Unload the module: uninstall the relays and message handlers
bool QueuesNotifyModule::unload()
{
    DDebug(this,DebugAll,"Cleanup");
    if (!lock(500000))
	return false;
    uninstallRelays();
    TelEngine::destruct(m_chanNotify);
    TelEngine::destruct(m_callCdr);
    // Clear calls
    for (ObjList* o = s_calls.skipNull(); o; o = o->skipNext()) {
	QueuedCall* call = static_cast<QueuedCall*>(o->get());
	Message* notify = 0;
	call->lock();
	// Notify the call
	if (s_notifyHangupOnUnload)
	    notify = call->buildResNotify(QueuedCall::Hangup);
	// Destroy as soon as possible
	call->setDestroy(0);
	call->unlock();
	if (notify)
	    notifyCall(call,notify,QueuedCall::Hangup);
    }
    unlock();
    // Stop worker
    if (s_thread) {
	Debug(this,DebugAll,"Cancelling worker(s)");
	s_thread->cancel(false);
	while (s_thread)
	    Thread::yield(true);
	Debug(this,DebugAll,"Worker(s) terminated");
    }
    return true;
}

// Send notification from a given call
void QueuesNotifyModule::notifyCall(QueuedCall* call, Message* notify, int status)
{
    if (!call)
	return;
    Lock lock(call);
    if (!call->notify(status)) {
	TelEngine::destruct(notify);
	return;
    }
    call->resetNotify(status);
    Debug(this,DebugAll,"Call(%s,%s) notifying status=%s (%s) [%p]",
	call->queue().c_str(),call->channelid().c_str(),
	QueuedCall::statusName(status),resNotifStatus(status),call);
    Engine::enqueue(notify);
    // Schedule for destroy if terminated
    if (status == QueuedCall::Hangup)
	call->setDestroy();
}

// Prepare a database message
Message* QueuesNotifyModule::getDBMsg(QueryType type, const char* caller)
{
    Lock lock(this);
    if (m_account.null() || null(caller))
	return 0;
    String tmp;
    switch (type) {
	case CallInfo:
	    tmp = m_queryCallInfo;
	    break;
	case CdrInfo:
	    tmp = m_queryCdrInfo;
	    break;
    }
    if (tmp.null())
	return 0;

    Message* m = new Message("database");
    m->addParam("account",m_account);
    lock.drop();
    NamedList p("");
    p.addParam("caller",caller);
    p.replaceParams(tmp,true);
    m->addParam("query",tmp);
    return m;
}

// Common message relay handler
bool QueuesNotifyModule::received(Message& msg, int id)
{
    if (id == Halt)
	unload();
    return Module::received(msg,id);
}

// Fill with list data
void QueuesNotifyModule::statusParams(String& str)
{
    Lock lock(this);
    str.append("calls=",",") << s_calls.count();
}

void QueuesNotifyModule::statusDetail(String& str)
{
    str << "format=Queue|NotifiedQueued|Hungup";
    lock();
    ObjList calls;
    ObjList* o = 0;
    for (o = s_calls.skipNull(); o; o = o->skipNext())
	calls.append(new String(o->get()->toString()));
    unlock();
    // Fill details
    for (o = calls.skipNull(); o; o = o->skipNext()) {
	// Memo: find() returns a referenced object
	QueuedCall* call = QueuedCall::find(o->get()->toString());
	if (!call)
	    continue;
	call->lock();
	str << ";" << call->channelid() << "=" << call->queue();
	str << "|" << String::boolText(!call->notify(QueuedCall::Queued));
	str << "|" << String::boolText(!call->notify(QueuedCall::Hangup));
	call->unlock();
	TelEngine::destruct(call);
    }
}

// Set event status from config
static inline void setEvStatus(const NamedList& src, int event, const char* defValue)
{
    const char* eventName = lookup(event,QueuedCall::s_events);
    if (!(eventName && *eventName))
	return;
    NamedString* ns = src.getParam(eventName);
    if (ns && *ns)
	s_resNotifStatus.setParam(eventName,*ns);
    else
	s_resNotifStatus.setParam(eventName,defValue);
}

// (Re)Initialize the module
void QueuesNotifyModule::initialize()
{
    Output("Initializing module Queues Notify");

    lock();
    s_cfg = Engine::configFile("queuesnotify");
    s_cfg.load();
    NamedList dummy("");

    // General section
    NamedList* general = s_cfg.getSection("general");
    if (!general)
	general = &dummy;
    m_account = general->getValue("account");
    s_notifyHangupOnUnload = general->getBoolValue("notifyhanguponunload",true);
    s_addNodeToResource = general->getBoolValue("addnodenametoresource",true);
    s_sleepMs = general->getIntValue("defsleep",20);
    if (s_sleepMs < 5)
	s_sleepMs = 5;
    if (s_sleepMs > 1000)
	s_sleepMs = 1000;

    // Events to status translation table
    NamedList* status = s_cfg.getSection("events");
    if (!status)
	status = &dummy;
    setEvStatus(*status,QueuedCall::Queued,"online");
    setEvStatus(*status,QueuedCall::Pickup,"dnd");
    setEvStatus(*status,QueuedCall::Hangup,"offline");

    // Caller info
    NamedList* queued = s_cfg.getSection("queued");
    if (queued) {
	m_queryCallInfo = queued->getValue("callinfo");
	m_queryCdrInfo = queued->getValue("cdrinfo");
	if (!m_queryCallInfo)
	    Debug(&__plugin,DebugInfo,"Query 'callinfo' not configured");
	if (!m_queryCdrInfo)
	    Debug(&__plugin,DebugInfo,"Query 'cdrinfo' not configured");
    }
    else {
	m_queryCallInfo.clear();
	m_queryCdrInfo.clear();
    }

    unlock();

    if (debugAt(DebugAll)) {
	String tmp;
	tmp << "account=" << m_account;
	tmp << " notifyhanguponunload=" << String::boolText(s_notifyHangupOnUnload);
	Debug(this,DebugAll,"Initialized: %s",tmp.c_str());
    }

    if (m_init)
	return;

    m_init = true;
    setup();
    installRelay(Halt);
    m_chanNotify = new ChanNotifyHandler;
    m_callCdr = new CallCdrHandler;
    Engine::install(m_chanNotify);
    Engine::install(m_callCdr);
    (new QueuedCallWorker)->startup();
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
