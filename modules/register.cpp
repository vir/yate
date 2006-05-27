/**
 * register.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Registration, authentication, authorization and accounting from a database.
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
namespace { // anonymous

static Configuration s_cfg(Engine::configFile("register"));
static bool s_critical = false;
static u_int32_t s_nextTime = 0;
static int s_expire = 30;
static bool s_errOffline = true;
static ObjList s_handlers;


class AAAHandler : public MessageHandler
{
public:
    enum {
	Regist,
	UnRegist,
	Auth,
	PreRoute,
	Route,
	Cdr,
	Timer
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
    bool m_init;
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
static void copyParams(Message& msg, Array* a, const char* resultName = 0, int row = 0)
{
    if (!a)
	return;
    for (int i = 0; i < a->getColumns(); i++) {
	String* s = YOBJECT(String,a->get(i,0));
	if (!(s && *s))
	    continue;
	String name = *s;
	for (int j = 1; j < a->getRows(); j++) {
	    s = YOBJECT(String,a->get(i,j));
	    if (!s)
		continue;
	    if (name == resultName)
		msg.retValue() = *s;
	    else
		msg.setParam(name,*s);
	}
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
	    {
		u_int32_t t = msg.msgTime().sec();
		if (t >= s_nextTime)
		    // we expire users every 30 seconds
		    s_nextTime = t + s_expire;
		else
		    return false;
	    }
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
    : Module("register","database"), m_init(false)
{
    Output("Loaded module Register for database");
}

RegistModule::~RegistModule()
{
    Output("Unloading module Register for database");
}

void RegistModule::statusParams(String& str)
{
    str.append("critical=",",") << s_critical;
}

bool RegistModule::received(Message& msg, int id)
{
    if (id == Private) {
	ObjList* l = s_handlers.skipNull();
	for (; l; l=l->skipNext())
	    static_cast<AAAHandler*>(l->get())->initQuery();
	return false;
    }
    return Module::received(msg,id);
}

int RegistModule::getPriority(const char *name)
{
    if (!s_cfg.getBoolValue("general",name))
	return -1;
    int prio = s_cfg.getIntValue("default","priority",50);
    return s_cfg.getIntValue(name,"priority",prio);
}

void RegistModule::addHandler(AAAHandler* handler)
{
    s_handlers.append(handler);
    handler->loadQuery();
    Engine::install(handler);
}

void RegistModule::addHandler(const char *name, int type)
{
    int prio = getPriority(name);
    if (prio >= 0) {
	Output("Installing priority %d handler for '%s'",prio,name);
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
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
