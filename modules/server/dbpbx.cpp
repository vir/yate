/**
 * dbpbx.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * PBX, IVR and multi routing from a database.
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

#include <yatepbx.h>

using namespace TelEngine;
namespace { // anonymous

static Configuration s_cfg(Engine::configFile("dbpbx"));


class DbObject
{
public:
    DbObject(const char* name);
    virtual ~DbObject() { }
    void initQuery();
    virtual bool loadQuery();

protected:
    String m_name;
    String m_account;
};

class DbMultiRouter : public MultiRouter, public DbObject
{
public:
    inline DbMultiRouter()
	: DbObject("router")
	{ }
    virtual bool loadQuery();
    virtual Message* buildExecute(CallInfo& info, bool reroute);
    virtual bool msgRoute(Message& msg, CallInfo& info, bool first);
protected:
    String m_queryRoute;
    String m_queryRetry;
    String m_retryNeeds;
};

class DbPbxPlugin : public Plugin
{
public:
    DbPbxPlugin();
    ~DbPbxPlugin();
protected:
    virtual void initialize();
private:
    bool m_init;
    DbMultiRouter* m_router;
};


// handle ${paramname} replacements
static void replaceParams(String& str, const NamedList &lst)
{
    int p1;
    while ((p1 = str.find("${")) >= 0) {
	int p2 = str.find('}',p1+2);
	if (p2 > 0) {
	    String v = str.substr(p1+2,p2-p1-2);
	    v.trimBlanks();
	    DDebug(DebugAll,"Replacing parameter '%s'",v.c_str());
	    String tmp = String::sqlEscape(lst.getValue(v));
	    str = str.substr(0,p1) + tmp + str.substr(p2+1);
	}
    }
}

// copy parameters from SQL result to a NamedList
static void copyParams(NamedList& lst, Array* a)
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
	    if (s)
		lst.setParam(name,*s);
	}
    }
}


DbObject::DbObject(const char* name)
    : m_name(name)
{
     m_account = s_cfg.getValue(m_name,"account",s_cfg.getValue("default","account"));
}

bool DbObject::loadQuery()
{
    return true;
}

void DbObject::initQuery()
{
    if (m_account.null())
	return;
    String query = s_cfg.getValue(m_name,"initquery");
    if (query.null())
	return;
    // no error check at all - we enqueue the query and we're out
    Message* m = new Message("database");
    m->addParam("account",m_account);
    m->addParam("query",query);
    m->addParam("results","false");
    Engine::enqueue(m);
}

bool DbMultiRouter::loadQuery()
{
    m_queryRoute = s_cfg.getValue(m_name,"queryroute");
    m_queryRetry = s_cfg.getValue(m_name,"queryretry");
    m_retryNeeds = s_cfg.getValue(m_name,"retryneeds");
    return m_queryRoute || m_queryRetry;
}

bool DbMultiRouter::msgRoute(Message& msg, CallInfo& info, bool first)
{
    if (m_queryRoute.null() || m_account.null())
	return false;
    String query(m_queryRoute);
    replaceParams(query,msg);
    Message m("database");
    m.addParam("account",m_account);
    m.addParam("query",query);
    if (Engine::dispatch(m) && m.getIntValue("rows") >=1) {
	Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
	if (a) {
	    copyParams(info,a);
	    copyParams(msg,a);
	    msg.retValue() = info.getValue("callto");
	    return true;
	}
    }
    return false;
}

Message* DbMultiRouter::buildExecute(CallInfo& info, bool reroute)
{
    if (m_queryRetry.null() || m_account.null())
	return 0;
    if (m_retryNeeds && !info.getParam(m_retryNeeds))
	return 0;
    String query(m_queryRetry);
    replaceParams(query,info);
    Message m("database");
    m.addParam("account",m_account);
    m.addParam("query",query);
    if (Engine::dispatch(m) && m.getIntValue("rows") >=1) {
	Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
	if (a) {
	    Message* m = defaultExecute(info);
	    copyParams(info,a);
	    copyParams(*m,a);
	    return m;
	}
    }
    return 0;
}

DbPbxPlugin::DbPbxPlugin()
    : Plugin("dbpbx"), m_init(false), m_router(0)
{
    Output("Loaded module PBX for database");
}

DbPbxPlugin::~DbPbxPlugin()
{
    Output("Unloading module PBX for database");
    if (m_router)
	m_router->destruct();
}

void DbPbxPlugin::initialize()
{
    if (m_init)
	return;
    m_init = true;
    Output("Initializing module PBX for database");
    s_cfg.load();
    if (!m_router && s_cfg.getBoolValue("general","router")) {
	m_router = new DbMultiRouter;
	if (m_router->loadQuery()) {
	    m_router->initQuery();
	    m_router->setup(s_cfg.getIntValue("priorities","router"));
	}
	else
	    TelEngine::destruct(m_router);
    }
}

INIT_PLUGIN(DbPbxPlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
