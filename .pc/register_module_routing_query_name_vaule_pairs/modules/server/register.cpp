/**
 * register.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Registration, authentication, authorization and accounting from a database.
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

static Configuration s_cfg(Engine::configFile("register"));
static bool s_critical = false;
static u_int32_t s_nextTime = 0;
static int s_expire = 30;
static bool s_errOffline = true;
static ObjList s_handlers;

static NamedList s_statusaccounts("StatusAccounts");
static HashList s_fallbacklist;

class AAAHandler : public MessageHandler
{
    YCLASS(AAAHandler,MessageHandler)
public:
    enum {
	Regist,
	UnRegist,
	Auth,
	PreRoute,
	Route,
	Cdr,
	Timer,
	Init,
	DialogNotify,
	MWINotify,
	Subscribe,
	SubscribeTimer
    };
    AAAHandler(const char* hname, int type, int prio = 50);
    virtual ~AAAHandler();
    void loadAccount();
    static void prepareQuery(Message& msg, const String& account, const String& query, bool results);
    virtual const String& name() const;
    virtual bool received(Message& msg);
    virtual bool loadQuery();
    virtual void initQuery();
    virtual void chkConfig();

protected:
    void indirectQuery(String& query);
    int m_type;
    String m_query;
    String m_result;
    String m_account;
};

class CDRHandler : public AAAHandler
{
public:
    CDRHandler(const char* hname, int prio = 50);
    virtual ~CDRHandler();
    virtual const String& name() const;
    virtual bool received(Message& msg);
    virtual bool loadQuery();

protected:
    String m_name;
    String m_queryInitialize;
    String m_queryUpdate;
    String m_queryCombined;
    bool m_critical;
};

// Base class for event notification handlers
class EventNotify : public AAAHandler
{
public:
    EventNotify(const char* hname, int type, const char* event, int prio = 50);
    virtual ~EventNotify() {}
    virtual const String& name() const;
    virtual bool loadQuery();
protected:
    // Fill account/query and dispatch the message
    // Return the data array if valid or 0 if message dispatch fails or no data
    Array* queryDatabase(Message& msg, const String& notifier, unsigned int& rows);
    // Create a notify message, fill it with notifier, event, subscription data
    // and additional parameters
    Message* message(const String& notifier, Array& subscriptions,
	unsigned int row, NamedList& params);
    // Notify all subscribers returned from a database message
    inline void notifyAll(const String& notifier, Array& subscriptions,
	unsigned int rows, NamedList& params) {
	    for (unsigned int row = 1; row <= rows; row++)
		Engine::enqueue(message(notifier,subscriptions,row,params));
	}

    String m_name;                       // Section name for this handler
    String m_event;                      // Event to notify
    String m_querySubs;                  // Query used to get subscriptions
};

// call.cdr. Notify subscribers to 'dialog' event on call state changes
class DialogNotify : public EventNotify
{
public:
    inline DialogNotify(const char* hname, int prio = 50)
	: EventNotify(hname,AAAHandler::DialogNotify,"dialog",prio)
	{}
    virtual bool received(Message& msg);
};

// user.notify. Notify subscribers to 'message-summary' event on user message status
class MWINotify : public EventNotify
{
public:
    inline MWINotify(const char* hname, int prio = 50)
	: EventNotify(hname,AAAHandler::MWINotify,"message-summary",prio)
	{}
    virtual bool received(Message& msg);
};

class SubscribeHandler : public AAAHandler
{
public:
    SubscribeHandler(const char* hname, int type, int prio = 50);
    virtual ~SubscribeHandler();
    virtual const String& name() const;
    virtual bool received(Message& msg);
    virtual bool loadQuery();

protected:
    String m_name;
    String m_querySubscribe;
    String m_queryUnsubscribe;
};

class SubscribeTimerHandler : public AAAHandler
{
public:
    SubscribeTimerHandler(const char* hname, int type, int prio = 50);
    virtual ~SubscribeTimerHandler();
    virtual const String& name() const;
    virtual bool received(Message& msg);
    virtual bool loadQuery();
protected:
    String m_name;
    int m_expireTime;
    u_int32_t m_nextTime;
    String m_queryExpire;
};

class AccountsModule;
class FallBackHandler;
class RegistModule : public Module
{
public:
    RegistModule();
    ~RegistModule();
protected:
    virtual void initialize();
    virtual void statusParams(String& str);
    virtual bool received(Message& msg, int id);
private:
    static int getPriority(const String& name);
    void addHandler(const char *name, int type);
    void addHandler(AAAHandler* handler);
    void addHandler(FallBackHandler* handler);
    bool m_init;
    AccountsModule *m_accountsmodule;
};

class FallBackRoute : public String
{
public:
    inline FallBackRoute(const String& id)
	: String(id)
	{}

    // add a message to the end of the routes
    inline void append(Message* msg)
	{ m_msglist.append(msg); }

    // get the topmost message and remove it from list
    inline Message* get()
	{ return static_cast<Message*>(m_msglist.remove(false)); }
private:
    ObjList m_msglist;
};

class FallBackHandler : public MessageHandler
{
public:
    enum {
	 Answered = 100,
	 Disconnect,
	 Hangup
    };
    inline FallBackHandler(const char* hname, int type, int prio = 50)
	: MessageHandler(hname,prio),m_type(type)
	{ m_stoperror = s_cfg.getValue("general","stoperror"); }

    virtual ~FallBackHandler()
	{ s_handlers.remove(this,false); }

    virtual bool received(Message &msg);

private:
    int m_type;
    Regexp m_stoperror;
};

class AccountsModule : public MessageReceiver
{
public:
    enum {
	Notify=50,
	Timer,
    };
    AccountsModule();
    ~AccountsModule();
protected:
    virtual bool received(Message &msg, int id);
    virtual void initialize();
private:
    bool m_init;
    u_int32_t m_nextTime;
    String m_queryInit;
    String m_queryTimer;
    String m_updateStatus;
    String m_account;
};

static RegistModule module;

// copy parameters from SQL result to a Message

static void copyParams2(Message &msg, Array* a, int row = 0)
{
    if ((!a) || (!row))
	return;
    for (int i = 0; i < a->getColumns(); i++) {
	String* s = YOBJECT(String,a->get(i,0));
	if (!(s && *s))
	    continue;
	String name = *s;
	s = YOBJECT(String,a->get(i,row));
	if (!s)
	    continue;
	msg.setParam(name,*s);
    }
}

// copy parameters from multiple SQL result rows to a Message
// returns true if resultName was found in columns

static bool copyParams(Message &msg, Array *a, const String& resultName)
{
    if (!a)
	return false;
    bool ok = false;
    FallBackRoute* fallback = 0;
    for (int j=1; j <a->getRows();j++) {
	Message* m = (j <= 1) ? &msg : new Message(msg);
	for (int i=0; i<a->getColumns();i++) {
	    const String* name = YOBJECT(String,a->get(i,0));
	    if (!(name && *name))
		continue;
	    bool res = (*name == resultName);
	    ok = ok || res;
	    const String* s = YOBJECT(String,a->get(i,j));
	    if (!s)
		continue;
	    if (res)
		m->retValue() = *s;
	    else
		m->setParam(*name,*s);
	}
	if (j>1) {
	    if (m->retValue().null()) {
		Debug(&module,DebugWarn,"Skipping void route #%d",j);
		m->destruct();
		continue;
	    }
	    if (!fallback)
		fallback = new FallBackRoute(msg.getValue("id"));
	    *m = "call.execute";
	    m->setParam("callto",m->retValue());
	    m->retValue().clear();
	    m->clearParam("error");
	    fallback->append(m);
	}
    }
    if (fallback) {
	Message mlocate("chan.locate");
	mlocate.addParam("id",msg.getValue("id"));
	if (static_cast<CallEndpoint*>(Engine::dispatch(mlocate) ? mlocate.userData() : 0))
	    s_fallbacklist.append(fallback);
	else
	    fallback->destruct();
    }
    return ok;
}


AAAHandler::AAAHandler(const char* hname, int type, int prio)
    : MessageHandler(hname,prio),m_type(type)
{
}

AAAHandler::~AAAHandler()
{
    s_handlers.remove(this,false);
}

void AAAHandler::loadAccount()
{
    m_result = s_cfg.getValue(name(),"result");
    m_account = s_cfg.getValue(name(),"account",s_cfg.getValue("default","account"));
}

const String& AAAHandler::name() const
{
    return *this;
}

bool AAAHandler::loadQuery()
{
    m_query = s_cfg.getValue(name(),"query");
    indirectQuery(m_query);
    return !m_query.null();
}

// replace a "@query" with the result of that query
void AAAHandler::indirectQuery(String& query)
{
    if (m_account.null())
	return;
    if (!query.startSkip("@",false))
	return;
    Engine::runParams().replaceParams(query,true);
    query.trimBlanks();
    if (query.null())
	return;
    Message m("database");
    prepareQuery(m,m_account,query,true);
    query.clear();
    // query must return exactly one row, one column
    if (!Engine::dispatch(m) || (m.getIntValue(YSTRING("rows")) != 1) || (m.getIntValue(YSTRING("columns")) != 1))
	return;
    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
    if (!a)
	return;
    query = YOBJECT(String,a->get(0,1));
    Debug(&module,DebugInfo,"For '%s' fetched query '%s'",name().c_str(),query.c_str());
}

// add the account and query to the "database" message
void AAAHandler::prepareQuery(Message& msg, const String& account, const String& query, bool results)
{
    Debug(&module,DebugInfo,"On account '%s' performing query '%s'%s",
	account.c_str(),query.c_str(),(results ? " expects results" : ""));
    msg.setParam("account",account);
    msg.setParam("query",query);
    msg.setParam("results",String::boolText(results));
}

// run the initialization query
void AAAHandler::initQuery()
{
    if (m_account.null())
	return;
    String query = s_cfg.getValue(name(),"initquery");
    indirectQuery(query);
    Engine::runParams().replaceParams(query,true);
    if (query.null())
	return;
    // no error check needed as we can't fix - enqueue the query and we're out
    Message* m = new Message("database");
    prepareQuery(*m,m_account,query,false);
    Engine::enqueue(m);
}

void AAAHandler::chkConfig()
{
    if (m_query && m_account.null())
	Alarm(&module,"config",DebugMild,"Missing database account for '%s'",name().c_str());
}

// little helper function to make code cleaner
static bool failure(Message* m)
{
    if (m)
	m->setParam("error","failure");
    return false;
}

bool AAAHandler::received(Message& msg)
{
    if (m_query.null() || m_account.null())
	return false;
    String query(m_query);
    String account(m_account);
    msg.replaceParams(query,true);
    msg.replaceParams(account,true);
    if (query.null() || account.null())
	return false;

    switch (m_type)
    {
	case Regist:
	{
	    if (!msg.getBoolValue(YSTRING("register_register"),true))
		return false;
	    if (s_critical)
		return failure(&msg);
	    Message m("database");
	    prepareQuery(m,account,query,true);
	    if (Engine::dispatch(m))
		if (m.getIntValue("affected") >= 1 || m.getIntValue("rows") >=1)
		    return true;
	    return false;
	}
	break;
	case Auth:
	{
	    if (!msg.getBoolValue(YSTRING("auth_register"),true))
		return false;
	    Message m("database");
	    prepareQuery(m,account,query,true);
	    if (Engine::dispatch(m))
		if (m.getIntValue("rows") >=1)
		{
		    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
		    if (!copyParams(msg,a,m_result)) {
			Debug(&module,DebugWarn,"Misconfigured result column for '%s'",name().c_str());
			msg.setParam("error","failure");
			return false;
		    }
		    return true;
		}
	    return false;
	}
	break;
	case PreRoute:
	{
	    if (!msg.getBoolValue(YSTRING("preroute_register"),true))
		return false;
	    if (s_critical)
		return failure(&msg);
	    Message m("database");
	    prepareQuery(m,account,query,true);
	    if (Engine::dispatch(m))
		if (m.getIntValue("rows") >=1)
		{
		    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
		    copyParams(msg,a,m_result);
		}
	    return false;
	}
	break;
	case Route:
	{
	    if (!msg.getBoolValue(YSTRING("route_register"),true))
		return false;
	    if (s_critical)
		return failure(&msg);
	    Message m("database");
	    prepareQuery(m,account,query,true);
	    if (Engine::dispatch(m))
		if (m.getIntValue("rows") >=1)
		{
		    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
		    copyParams(msg,a,m_result);
		    if (msg.retValue().null())
		    {
			// we know about the user but has no address of record
			if (s_errOffline) {
			    msg.retValue() = "-";
			    msg.setParam("error","offline");
			    msg.setParam("reason","Offline");
			}
			return false;
		    }
		    return true;
		}
	    return false;
	}
	break;
	case UnRegist:
	{
	    if (!msg.getBoolValue(YSTRING("register_register"),true))
		return false;
	    // no error check needed on unregister - we return false
	    Message m("database");
	    prepareQuery(m,account,query,true);
	    // we don't enqueue the message because we must assure ourselves that this message is processed synchronously
	    Engine::dispatch(m);
	}
	break;
	case Timer:
	{
	    u_int32_t t = msg.msgTime().sec();
	    if (t >= s_nextTime)
		// we expire users every 30 seconds
		s_nextTime = t + s_expire;
	    else
		return false;
	    // no error check needed - we enqueue the query and return false
	    Message* m = new Message("database");
	    prepareQuery(*m,account,query,false);
	    Engine::enqueue(m);
	}
	break;
    }
    return false;
}

CDRHandler::CDRHandler(const char* hname, int prio)
    : AAAHandler("call.cdr",Cdr,prio), m_name(hname)
{
    m_critical = s_cfg.getBoolValue(m_name,"critical",(m_name == "call.cdr"));
}

CDRHandler::~CDRHandler()
{
}

const String& CDRHandler::name() const
{
    return m_name;
}

bool CDRHandler::loadQuery()
{
    m_queryInitialize = s_cfg.getValue(name(),"cdr_initialize");
    m_queryUpdate = s_cfg.getValue(name(),"cdr_update");
    m_queryCombined = s_cfg.getValue(name(),"cdr_combined");
    m_query = s_cfg.getValue(name(),"cdr_finalize");
    if (m_query.null())
	m_query = s_cfg.getValue(name(),"query");
    indirectQuery(m_queryInitialize);
    indirectQuery(m_queryUpdate);
    indirectQuery(m_queryCombined);
    indirectQuery(m_query);
    return m_queryInitialize || m_queryUpdate || m_queryCombined || m_query;
}

bool CDRHandler::received(Message& msg)
{
    if (!msg.getBoolValue("cdrwrite_register",true))
	return false;
    if (m_account.null())
	return false;
    // Don't update CDR if told so
    if (!msg.getBoolValue("cdrwrite",true))
	return false;
    String query(msg.getValue("operation"));
    if (query == YSTRING("initialize"))
	query = m_queryInitialize;
    else if (query == YSTRING("update"))
	query = m_queryUpdate;
    else if (query == YSTRING("combined"))
	query = m_queryCombined;
    else if (query == YSTRING("finalize"))
	query = m_query;
    else
	return false;

    if (query.null())
	return false;
    String account(m_account);
    msg.replaceParams(query,true);
    msg.replaceParams(account,true);
    if (query.null() || account.null())
	return false;

    // failure while accounting is critical
    Message m("database");
    prepareQuery(m,account,query,true);
    bool error = !Engine::dispatch(m) || m.getParam("error");
    if (m_critical && (s_critical != error)) {
	s_critical = error;
	module.changed();
    }
    if (error)
	failure(&msg);
    return false;
}


// EventNotify
EventNotify::EventNotify(const char* hname, int type, const char* event, int prio)
    : AAAHandler(hname,type,prio), m_name("resource.subscribe"), m_event(event)
{
}

const String& EventNotify::name() const
{
    return m_name;
}

bool EventNotify::loadQuery()
{
    m_querySubs = s_cfg.getValue(m_name,"subscribe_notify");
    indirectQuery(m_querySubs);
    if (!m_querySubs)
	Debug(&module,DebugNote,
	    "Notify(%s). Invalid 'subscribe_notify' in section '%s'",
	    m_event.c_str(),m_name.c_str());
    return !m_querySubs.null();
}

// Fill account/query and dispatch the message
// Return false if message dispatch fails or no data is returned
Array* EventNotify::queryDatabase(Message& msg, const String& notifier,
	unsigned int& rows)
{
    NamedList nl("");
    nl.addParam("notifier",notifier);
    nl.addParam("event",m_event);
    String query = m_querySubs;
    String account = m_account;
    nl.replaceParams(query,true);
    nl.replaceParams(account,true);
    prepareQuery(msg,account,query,true);
    if (!Engine::dispatch(msg))
	return 0;
    rows = msg.getIntValue("rows",0);
    Array* subscriptions = static_cast<Array*>(msg.userObject(YATOM("Array")));
    if (!(subscriptions && rows))
	return 0;
    DDebug(&module,DebugAll,
	"Notify(%s). Found %u subscriber(s) for '%s' notifier",
	m_event.c_str(),rows,notifier.c_str());
    return subscriptions;
}

// Create a notify message, fill it with notifier, event, subscription data
// and additional parameters
Message* EventNotify::message(const String& notifier, Array& subscriptions,
	unsigned int row, NamedList& params)
{
    Message* notify = new Message("resource.notify");
    notify->addParam("notifier",notifier);
    notify->addParam("event",m_event);
    copyParams2(*notify,&subscriptions,row);
    unsigned int n = params.count();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (ns)
	   notify->addParam(ns->name(),*ns);
    }
    return notify;
}


// call.cdr handler: Notifications on dialog state changes
bool DialogNotify::received(Message& msg)
{
    if(!(m_account && m_querySubs))
	return false;

    XDebug(&module,DebugAll,
	"Notify(dialog). operation=%s external=%s status=%s chan=%s",
	msg.getValue("operation"),msg.getValue("external"),
	msg.getValue("status"),msg.getValue("chan"));

    // Get call id and state to be notified
    String callState;
    String operation(msg.getValue("operation"));
    if (operation == "update") {
	String status = msg.getValue("status");
	if (!status)
	    return false;
	if (status == "connected" || status == "answered")
	    callState = "confirmed";
	else if (status == "calling" || status == "ringing" || status == "progressing" ||
	    status == "incoming" || status == "outgoing")
	    callState = "early";
	else if (status == "redirected")
	    callState = "rejected";
	else if (status == "destroyed")
	    callState = "terminated";
    }
    else if (operation == "initialize")
	callState = "trying";
    else if (operation == "finalize")
	callState = "terminated";
    if (!callState)
	return false;
    String id = msg.getValue("chan");
    if (!id)
	return false;

    // Get notifier from message and its subscriptions from database
    String notifier = msg.getValue("external");
    Message m("database");
    unsigned int rows;
    Array* subscriptions = queryDatabase(m,notifier,rows);
    // Notify
    if (subscriptions) {
	NamedList nl("");
	nl.addParam("dialog.id",id);
	nl.addParam("dialog.direction", msg.getValue("direction"));
	nl.addParam("dialog.state",callState);
	notifyAll(notifier,*subscriptions,rows,nl);
    }
    return false;
}


// user.notify handler: Notifications on message(s) state changes
bool MWINotify::received(Message& msg)
{
    if(!(m_account && m_querySubs))
	return false;
    const char* v = msg.getValue("voicemail");
    if (!v)
	return false;

    // TODO: change debug
    Debug(&module,DebugNote,"Notify(message-summary). username=%s",msg.getValue("username"));

    String notifier = msg.getValue("username");
    if (!notifier)
	return false;

    Message m("database");
    unsigned int rows;
    Array* subscriptions = queryDatabase(m,notifier,rows);
    if (subscriptions) {
	NamedList nl("");
	nl.addParam("message-summary.voicenew",msg.getValue("voicenew"));
	nl.addParam("message-summary.voiceold",msg.getValue("voiceold"));
	notifyAll(notifier,*subscriptions,rows,nl);
    }
    return false;
}


SubscribeHandler::SubscribeHandler(const char* hname, int type, int prio)
	: AAAHandler(hname,Subscribe, prio), m_name(hname)
{
}

SubscribeHandler::~SubscribeHandler()
{
}

const String& SubscribeHandler::name() const
{
    return m_name;
}

bool SubscribeHandler::loadQuery()
{
    m_querySubscribe = s_cfg.getValue(name(),"subscribe_subscribe");
    indirectQuery(m_querySubscribe);
    if (!m_querySubscribe)
	Debug(&module,DebugNote,
	    "Invalid 'subscribe_subscribe' in section '%s'",m_name.c_str());
    m_queryUnsubscribe = s_cfg.getValue(name(),"subscribe_unsubscribe");
    indirectQuery(m_queryUnsubscribe);
    if (!m_queryUnsubscribe)
	Debug(&module,DebugNote,
	    "Invalid 'subscribe_unsubscribe' in section '%s'",m_name.c_str());
    return m_querySubscribe || m_queryUnsubscribe;
}

bool SubscribeHandler::received(Message& msg)
{
    if (!m_account)
	return false;

    DDebug(&module,DebugAll,
	"Subscribe. operation=%s notifier=%s subscriber=%s event=%s notifyto=%s",
	msg.getValue("operation"),msg.getValue("notifier"),
	msg.getValue("subscriber"),msg.getValue("event"),msg.getValue("notifyto"));

    String query = msg.getValue("operation");
    bool subscribe = true;
    if(query == "subscribe")
	query = m_querySubscribe;
    else if (query == "unsubscribe") {
	subscribe = false;
	query = m_queryUnsubscribe;
    }
    else
	query = "";

    if (!query)
	return false;

    String account = m_account;
    msg.replaceParams(query,true);
    msg.replaceParams(account,true);
    Message m("database");
    prepareQuery(m,account,query,true);
    int rows = 0;
    if(!Engine::dispatch(m)) {
	msg.setParam("reason","failure");
	return false;
    }

    if(1 != (rows = m.getIntValue("rows",0))) {
	msg.setParam("reason","forbidden");
	return false;
    }

    Message* notify = new Message("resource.notify");
    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
    if (subscribe) {
	copyParams2(*notify,a,1);
	notify->addParam("subscriptionstate","active");
    }
    else {
	String* s = YOBJECT(String, a ? a->get(0,0) : 0);
	int count = s ? s->toInteger() : 0;
	if(count != 1) {
	    msg.setParam("reason","forbidden");
	    TelEngine::destruct(notify);
	    return false;
	}
	notify->copyParams(msg,"subscriber,notifier,notifyto,event,data");
	notify->addParam("subscriptionstate","terminated");
    }
    Engine::enqueue(notify);
    return true;
}


SubscribeTimerHandler::SubscribeTimerHandler(const char* hname, int type, int prio)
	: AAAHandler("engine.timer", type, prio), m_name("resource.subscribe")
{
    m_expireTime = s_cfg.getIntValue(m_name,"expires",s_cfg.getIntValue("general","expires",30));
    m_nextTime = 0;
}

SubscribeTimerHandler::~SubscribeTimerHandler()
{
}

const String& SubscribeTimerHandler::name() const
{
    return m_name;
}

bool SubscribeTimerHandler::loadQuery()
{
    m_queryExpire = s_cfg.getValue(name(), "subscribe_expire");
    indirectQuery(m_queryExpire);
    if (!m_queryExpire)
	Debug(&module,DebugNote,
	    "Invalid 'subscribe_expire' in section '%s'",name().safe());
    return !m_queryExpire.null();
}

bool SubscribeTimerHandler::received(Message& msg)
{
    if(!(m_account && m_queryExpire))
	return false;

    u_int32_t t = msg.msgTime().sec();
    if( t >= m_nextTime)
	m_nextTime = t + m_expireTime;
    else
	return false;

    if(!m_queryExpire)
	return false;

    Message m("database");
    String account = m_account;
    String query = m_queryExpire;
    msg.replaceParams(query,true);
    msg.replaceParams(account,true);
    prepareQuery(m,account,query,true);
    if(!Engine::dispatch(m))
	return false;

    int rows = m.getIntValue("rows",0);
    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
    if(!a || rows < 1)
	return false;
    for(int i = 1; i <= rows; i++) {
	Message* notify = new Message("resource.notify");
	copyParams2(*notify,a,i);
	notify->addParam("subscriptionstate","terminated");
	notify->addParam("terminatereason","timeout");
	DDebug(&module,DebugNote,"Subscription expired: notifier=%s subscriber=%s event=%s",
	    notify->getValue("notifier"),notify->getValue("subscriber"),notify->getValue("event"));
	Engine::enqueue(notify);
    }
    return false;
}


RegistModule::RegistModule()
    : Module("register","database"), m_init(false), m_accountsmodule(0)
{
    Output("Loaded module Register for database");
}

RegistModule::~RegistModule()
{
    TelEngine::destruct(m_accountsmodule);
    Output("Unloading module Register for database");
}

void RegistModule::statusParams(String& str)
{
    NamedString* names;
    str.append("critical=",",") << s_critical;
    for (unsigned int i=0; i < s_statusaccounts.count(); i++) {
	names = s_statusaccounts.getParam(i);
	if (names)
    	    str << "," << names->name() << "=" << names->at(0);
    }
}

bool RegistModule::received(Message& msg, int id)
{
    if (id == Private) {
	if (s_cfg.getBoolValue("general","accounts"))
	    m_accountsmodule= new AccountsModule();
	ObjList* l = s_handlers.skipNull();
	for (; l; l=l->skipNext()) {
	    AAAHandler* h = YOBJECT(AAAHandler,l->get());
	    if (h)
		h->initQuery();
	}
	return false;
    }
    return Module::received(msg,id);
}

int RegistModule::getPriority(const String& name)
{
    bool fb = (name == "chan.disconnected") || (name == "call.answered") || (name == "chan.hangup");
    // allow to default enable all fallback related messages in a single place
    if (!s_cfg.getBoolValue("general",name,fb && s_cfg.getBoolValue("general","fallback")))
	    return -1;

    int prio = s_cfg.getIntValue("default","priority",50);
    // also allow a 2nd default priority for fallback messages
    if (fb)
	prio = s_cfg.getIntValue("fallback","priority",prio);
    return s_cfg.getIntValue(name,"priority",prio);
}

void RegistModule::addHandler(AAAHandler* handler)
{
    String trackName(name());
    if (trackName && handler->priority())
	trackName << ":" << handler->priority();
    handler->trackName(trackName);
    handler->loadAccount();
    s_handlers.append(handler);
    handler->loadQuery();
    handler->chkConfig();
    Engine::install(handler);
}

void RegistModule::addHandler(FallBackHandler* handler)
{
    String trackName(name());
    if (trackName && handler->priority())
	trackName << ":" << handler->priority();
    handler->trackName(trackName);
    s_handlers.append(handler);
    Engine::install(handler);
}


void RegistModule::addHandler(const char *name, int type)
{
    int prio = getPriority(name);
    if (prio >= 0) {
	switch (type) {
	    case FallBackHandler::Disconnect:
	    case FallBackHandler::Answered:
	    case FallBackHandler::Hangup:
		addHandler(new FallBackHandler(name,type,prio));
		break;
	    case AAAHandler::Cdr:
		addHandler(new CDRHandler(name,prio));
		break;
	    case AAAHandler::DialogNotify:
		addHandler(new DialogNotify(name,prio));
		break;
	    case AAAHandler::MWINotify:
		addHandler(new MWINotify(name,prio));
		break;
	    case AAAHandler::Subscribe:
		addHandler(new SubscribeHandler(name, type, prio));
		break;
	    case AAAHandler::SubscribeTimer:
		addHandler(new SubscribeTimerHandler(name, type, prio));
		break;
	    default:
		addHandler(new AAAHandler(name,type,prio));
	}
    }
}

void RegistModule::initialize()
{
    s_critical = false;
    if (m_init)
	return;
    m_init = true;
    setup();
    Output("Initializing module Register for database");
    s_expire = s_cfg.getIntValue("general","expires",s_expire);
    s_errOffline = s_cfg.getBoolValue("call.route","offlineauto",true);
    Engine::install(new MessageRelay("engine.start",this,Private,150));
    addHandler("call.cdr",AAAHandler::Cdr);
    addHandler("linetracker",AAAHandler::Cdr);
    addHandler("user.auth",AAAHandler::Auth);
    addHandler("engine.timer",AAAHandler::Timer);
    addHandler("user.unregister",AAAHandler::UnRegist);
    addHandler("user.register",AAAHandler::Regist);
    addHandler("call.preroute",AAAHandler::PreRoute);
    addHandler("call.route",AAAHandler::Route);

    addHandler("chan.disconnected",FallBackHandler::Disconnect);
    addHandler("chan.hangup",FallBackHandler::Hangup);
    addHandler("call.answered",FallBackHandler::Answered);

    if (s_cfg.getBoolValue("general","subscriptions",false)) {
	addHandler("call.cdr",AAAHandler::DialogNotify);
	addHandler("user.notify",AAAHandler::MWINotify);
	addHandler("resource.subscribe", AAAHandler::Subscribe);
	addHandler("engine.timer", AAAHandler::SubscribeTimer);
    }
}

bool FallBackHandler::received(Message &msg)
{
    switch (m_type)
    {
	case Answered:
	{
	    GenObject* route = s_fallbacklist[msg.getValue("targetid")];
	    s_fallbacklist.remove(route);
	    return false;
	}
	break;
	case Hangup:
	{
	    GenObject* route = s_fallbacklist[msg.getValue("id")];
	    s_fallbacklist.remove(route);
	    return false;
	}
	break;
	case Disconnect:
	{
	    String reason=msg.getValue("reason");
	    if (m_stoperror && m_stoperror.matches(reason)) {
		//stop fallback on this error
		GenObject* route = s_fallbacklist[msg.getValue("id")];
		s_fallbacklist.remove(route);
		return false;
	    }

	    FallBackRoute* route = static_cast<FallBackRoute*>(s_fallbacklist[msg.getValue("id")]);
	    if (route) {
		Message* r = route->get();
		if (r) {
		    r->userData(msg.userData());
		    Engine::enqueue(r);
		    return true;
		}
		s_fallbacklist.remove(route);
	    }
	    return false;
	}
	break;
    }
    return false;
}


AccountsModule::AccountsModule()
    : m_init(false), m_nextTime(0)
{
    Output("Loaded modules Accounts for database");
    m_account = s_cfg.getValue("accounts","account", s_cfg.getValue("default","account"));
    m_queryInit = s_cfg.getValue("accounts","initquery");
    m_queryTimer = s_cfg.getValue("accounts","timerquery");
    m_updateStatus = s_cfg.getValue("accounts","statusquery");
    initialize();
}

AccountsModule::~AccountsModule()
{
    Output("Unloading module Accounts for database");
}

bool AccountsModule::received(Message &msg, int id)
{
    if (id == Notify) {
	String name(msg.getValue("account"));
	if (name.null())
	    return false;
	name << "(" << msg.getValue("protocol") << ")";
	s_statusaccounts.setParam(name,msg.getValue("registered"));
	Message *m = new Message("database");
	String account(m_account);
	msg.replaceParams(account,true);
	String query(m_updateStatus);
	String status;
	if (msg.getBoolValue("registered"))
	    status="online";
	else
	    status="offline";
	m->addParam("status",status);
	m->addParam("internalaccount",msg.getValue("account"));
	m->replaceParams(query,true);
	AAAHandler::prepareQuery(*m,account,query,false);
	Engine::enqueue(m);
	return false;
    }
    if (id == Timer) {
	if (m_account.null())
	    return false;
	u_int32_t t = msg.msgTime().sec();
	if (t >= m_nextTime)
	    // we look for account changes every 30 seconds
	    m_nextTime = t + s_expire;
	else
	    return false;

	String query;
	if (m_init)
	    query = m_queryTimer;
	else {
	    query = m_queryInit;
	    m_init=true;
	}
	if (query.null())
	    return false;
	Message m("database");
	String account(m_account);
	msg.replaceParams(account,true);
	AAAHandler::prepareQuery(m,account,query,true);
	if (Engine::dispatch(m)) {
	    int rows = m.getIntValue("rows");
	    if (rows>0) {
		for (int i=1 ; i<=rows ; i++) {
		    Message *m1= new Message("user.login");
		    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
		    copyParams2(*m1,a, i);
		    Engine::enqueue(m1);
		}
		return false;
	    }
	}
	return false;
    }
    return false;
}

void AccountsModule::initialize()
{
    if (s_cfg.getBoolValue("general","accounts")) {
	Engine::install(new MessageRelay("user.notify",this,Notify,100,module.name()));
	Engine::install(new MessageRelay("engine.timer",this,Timer,100,module.name()));
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
