/**
 * register.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Registration, authentication, authorization and accounting from a database.
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
	Init
    };
    AAAHandler(const char* hname, int type, int prio = 50);
    virtual ~AAAHandler();
    virtual const String& name() const;
    virtual bool received(Message& msg);
    virtual bool loadQuery();
    virtual void initQuery();

protected:
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
    bool m_critical;
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
    static int getPriority(const char *name);
    static void addHandler(const char *name, int type);
    static void addHandler(AAAHandler* handler);
    static void addHandler(FallBackHandler* handler);
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
    String m_queryInit;
    String m_queryTimer;
    String m_updateStatus;
    String m_account;
};

static RegistModule module;

// handle ${paramname} replacements
static void replaceParams(String& str, const Message &msg)
{
    int p1;
    while ((p1 = str.find("${")) >= 0) {
	int p2 = str.find('}',p1+2);
	if (p2 > 0) {
	    String v = str.substr(p1+2,p2-p1-2);
	    v.trimBlanks();
	    DDebug(&module,DebugAll,"Replacing parameter '%s'",v.c_str());
	    String tmp = String::sqlEscape(msg.getValue(v));
	    str = str.substr(0,p1) + tmp + str.substr(p2+1);
	}
    }
}

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

static void copyParams(Message &msg,Array *a,const char* resultName=0,int row=0) {
    if (!a)
	return;
    FallBackRoute* fallback = 0;
    for (int j=1; j <a->getRows();j++) {
	Message* m = (j <= 1) ? &msg : new Message(msg);
	for (int i=0; i<a->getColumns();i++) {
	    String* s = YOBJECT(String,a->get(i,0));
	    if (!(s && *s))
		continue;
	    String name = *s;
	    s = YOBJECT(String,a->get(i,j));
	    if (!s)
		continue;
	    if (name == resultName)
		m->retValue() = *s;
	    else
		m->setParam(name,*s);
	}	
	if (j>1) {
	    if (m->retValue().null()) {
		Debug(&module,DebugWarn,"Skipping void route #%d",j);
		delete m;
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
	    delete fallback;
    }
}


AAAHandler::AAAHandler(const char* hname, int type, int prio)
    : MessageHandler(hname,prio),m_type(type)
{
     m_result = s_cfg.getValue(name(),"result");
     m_account = s_cfg.getValue(name(),"account",s_cfg.getValue("default","account"));
}

AAAHandler::~AAAHandler()
{
    s_handlers.remove(this,false);
}

const String& AAAHandler::name() const
{
    return *this;
}

bool AAAHandler::loadQuery()
{
    m_query = s_cfg.getValue(name(),"query");
    return m_query != 0;
}

void AAAHandler::initQuery()
{
    if (m_account.null())
	return;
    String query = s_cfg.getValue(name(),"initquery");
    if (query.null())
	return;
    // no error check at all - we enqueue the query and we're out
    Message* m = new Message("database");
    m->addParam("account",m_account);
    m->addParam("query",query);
    m->addParam("results","false");
    Engine::enqueue(m);
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
    replaceParams(query,msg);
    switch (m_type)
    {
	case Regist:
	{
	    if (s_critical)
		return failure(&msg);
	    Message m("database");
	    m.addParam("account",m_account);
	    m.addParam("query",query);
	    m.addParam("results","false");
	    if (Engine::dispatch(m))
		if (m.getIntValue("affected") >= 1 || m.getIntValue("rows") >=1)
		    return true;
	    return false;
	}
	break;
	case Auth:
	{
	    Message m("database");
	    m.addParam("account",m_account);
	    m.addParam("query",query);
	    if (Engine::dispatch(m))
		if (m.getIntValue("rows") >=1)
		{
		    Array* a = static_cast<Array*>(m.userObject("Array"));
		    copyParams(msg,a,m_result);
		    return true;
		}
	    return false;
	}
	break;
	case PreRoute:
	{
	    if (s_critical)
		return failure(&msg);
	    Message m("database");
	    m.addParam("account",m_account);
	    m.addParam("query",query);
	    if (Engine::dispatch(m))
		if (m.getIntValue("rows") >=1)
		{
		    Array* a = static_cast<Array*>(m.userObject("Array"));
		    copyParams(msg,a,m_result);
		}
	    return false;
	}
	break;
	case Route:
	{
	    if (s_critical)
		return failure(&msg);
	    Message m("database");
	    m.addParam("account",m_account);
	    m.addParam("query",query);
	    if (Engine::dispatch(m))
		if (m.getIntValue("rows") >=1)
		{
		    Array* a = static_cast<Array*>(m.userObject("Array"));
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
	    // no error check - we return false
	    Message m("database");
	    m.addParam("account",m_account);
	    m.addParam("query",query);
	    m.addParam("results","false");
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
	    // no error check at all - we enqueue the query and return false
	    Message* m = new Message("database");
	    m->addParam("account",m_account);
	    m->addParam("query",query);
	    m->addParam("results","false");
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
    m_query = s_cfg.getValue(name(),"cdr_finalize");
    if (m_query.null())
	m_query = s_cfg.getValue(name(),"query");
    return m_queryInitialize || m_queryUpdate || m_query;
}

bool CDRHandler::received(Message& msg)
{
    if (m_account.null())
	return false;
    String query(msg.getValue("operation"));
    if (query == "initialize")
	query = m_queryInitialize;
    else if (query == "update")
	query = m_queryUpdate;
    else if (query == "finalize")
	query = m_query;
    else
	return false;
    if (query.null())
	return false;
    replaceParams(query,msg);
    // failure while accounting is critical
    Message m("database");
    m.addParam("account",m_account);
    m.addParam("query",query);
    bool error = !Engine::dispatch(m) || m.getParam("error");
    if (m_critical && (s_critical != error)) {
	s_critical = error;
	module.changed();
    }
    if (error)
	failure(&msg);
    return false;
}


RegistModule::RegistModule()
    : Module("register","database"), m_init(false), m_accountsmodule(0)
{
    Output("Loaded module Register for database");
}

RegistModule::~RegistModule()
{
    delete m_accountsmodule;
    Output("Unloading module Register for database");
}

void RegistModule::statusParams(String& str)
{
    NamedString* names;
    str.append("critical=",",") << s_critical;
    for (uint i=0; i < s_statusaccounts.count(); i++) {
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

int RegistModule::getPriority(const char *name)
{
    String num;		
    if (!s_cfg.getBoolValue("general",name,true)) {
	if ((name == "chan.disconnected") || (name == "call.answered") || (name == "chan.hangup")) {
	    if (!s_cfg.getBoolValue("general","fallback"))
		return -1;
	    num = "fallback";
	}	
	else
	    return -1;
    }
    num = name;
    int prio = s_cfg.getIntValue("default","priority",50);
    return s_cfg.getIntValue(num,"priority",prio);
}

void RegistModule::addHandler(AAAHandler* handler)
{
    s_handlers.append(handler);
    handler->loadQuery();
    Engine::install(handler);
}

void RegistModule::addHandler(FallBackHandler* handler)
{
    s_handlers.append(handler);
    Engine::install(handler);
}


void RegistModule::addHandler(const char *name, int type)
{
    int prio = getPriority(name);
    if (prio >= 0) {
	if ((type == FallBackHandler::Disconnect) || (FallBackHandler::Answered) || (FallBackHandler::Hangup))
	    addHandler(new FallBackHandler(name,type,prio));
	if (type == AAAHandler::Cdr)
	    addHandler(new CDRHandler(name,prio));
	else
	    addHandler(new AAAHandler(name,type,prio));
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
    : m_init(false)
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
	m->addParam("account",m_account);
	String query(m_updateStatus);
	String status;
	if (msg.getBoolValue("registered"))
	    status="online";
	else
	    status="offline";
	m->addParam("status",status);
	m->addParam("internalaccount",msg.getValue("account"));
	replaceParams(query,*m);
	m->addParam("query",query);
	Engine::enqueue(m);
	return false;
    }
    if (id == Timer) {
	String query;
	if (m_account.null())
	    return false;
	if (m_init)
	    query = m_queryTimer;
	else {
	    query = m_queryInit;
	    m_init=true;
	}
	if (query.null())
	    return false;
	Message m("database");
	m.addParam("account",m_account);
	m.addParam("query",query);
	if (Engine::dispatch(m)) {
	    int rows = m.getIntValue("rows");
	    if (rows>0) {
		for (int i=1 ; i<=rows ; i++) {
		    Message *m1= new Message("user.login");
		    Array* a = static_cast<Array*>(m.userObject("Array"));
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
	Engine::install(new MessageRelay("user.notify",this,Notify,100));
	Engine::install(new MessageRelay("engine.timer",this,Timer,100));
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
