/**
 * register.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Ask for a registration from this module.
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

#include <stdio.h>
#include <libpq-fe.h>

using namespace TelEngine;

static Configuration s_cfg(Engine::configFile("register"));
static bool s_critical = false;
static u_int32_t s_nextTime = 0;
static int s_expire = 30;
static ObjList s_handlers;


class AAAHandler : public MessageHandler
{
public:
    enum {
	Regist,
	UnRegist,
	Auth,
	Route,
	Cdr,
	Timer
    };
    AAAHandler(const char* hname, int type, int prio = 50);
    virtual ~AAAHandler();
    virtual const String& name() const;
    virtual bool received(Message& msg);
    bool ok();
    int queryDb(const char* query, Message* dest = 0);
    bool initDb(int retry = 0);

protected:
    virtual bool loadQuery();
    String m_query;

private:
    void dropDb();
    bool testDb();
    bool startDb();
    int queryDbInternal(const char* query, Message* dest);
    String m_result;
    Mutex m_dbmutex;
    int m_type;
    PGconn *m_conn;
    int m_retry;
    u_int64_t m_timeout;
    bool m_first;
};

class CDRHandler : public AAAHandler
{
public:
    CDRHandler(const char* hname, int prio = 50);
    virtual ~CDRHandler();
    virtual const String& name() const;
    virtual bool received(Message& msg);

protected:
    virtual bool loadQuery();
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
private:
    static int getPriority(const char *name);
    static void addHandler(const char *name, int type);
    static void addHandler(AAAHandler* handler);
    bool m_init;
};

static RegistModule module;

// perform escaping of special SQL characters
static void sqlEscape(String& str)
{
    unsigned int i;
    int e = 0;
    for (i = 0; i < str.length(); i++) {
	switch (str[i]) {
	    case '\'':
	    case '\\':
		++e;
	}
    }
    if (!e)
	return;
    String tmp(' ',str.length()+e);
    char* d = const_cast<char*>(tmp.c_str());
    const char* s = str.c_str();
    while (char c = *s++) {
	switch (c) {
	    case '\'':
	    case '\\':
		*d++ = '\\';
	}
	*d++ = c;
    }
    str = tmp;
}

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
	    String tmp(msg.getValue(v));
	    sqlEscape(tmp);
	    str = str.substr(0,p1) + tmp + str.substr(p2+1);
	}
    }
}

// copy parameters from SQL result to a Message												    
static void copyParams(Message& msg, const PGresult* res, const char* resultName = 0, int row = 0)
{
    int n = PQnfields(res);
    for (int i = 0; i < n; i++) {
	String name(PQfname(res,i));
	if (name.null() || PQgetisnull(res,row,i))
	    continue;
	const char* val = PQgetvalue(res,row,i);
	if (name == resultName)
	    msg.retValue() = val;
	else
	    msg.setParam(name,val);
    }
}

AAAHandler::AAAHandler(const char* hname, int type, int prio)
    : MessageHandler(hname,prio), m_dbmutex(true),
      m_type(type), m_conn(0), m_retry(0), m_timeout(0), m_first(true)
{
}

AAAHandler::~AAAHandler()
{
    s_handlers.remove(this,false);
    dropDb();
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

// initialize the database connection and handler data
bool AAAHandler::initDb(int retry)
{
    if (null())
	return false;
    Lock lock(m_dbmutex);
    if (!loadQuery())
	return false;
    // allow specifying the raw connection string
    String conn(s_cfg.getValue(name(),"connection"));
    if (conn.null())
	conn = s_cfg.getValue("default","connection");
    if (conn.null()) {
	// else build it from pieces
	const char* host = s_cfg.getValue("default","host","localhost");
	host = s_cfg.getValue(name(),"host",host);
	int port = s_cfg.getIntValue("default","port",5432);
	port = s_cfg.getIntValue(name(),"port",port);
	const char* dbname = s_cfg.getValue("default","database","yate");
	dbname = s_cfg.getValue(name(),"database",dbname);
	const char* user = s_cfg.getValue("default","user","postgres");
	user = s_cfg.getValue(name(),"user",user);
	const char* pass = s_cfg.getValue("default","password");
	pass = s_cfg.getValue(name(),"password",pass);
	if (TelEngine::null(host) || (port <= 0) || TelEngine::null(dbname))
	    return false;
	conn << "host='" << host << "' port=" << port << " dbname='" << dbname << "'";
	if (user) {
	    conn << " user='" << user << "'";
	    if (pass)
		conn << " password='" << pass << "'";
	}
    }
    m_result = s_cfg.getValue(name(),"result");
    int t = s_cfg.getIntValue("default","retry",5);
    m_retry = s_cfg.getIntValue(name(),"retry",t);
    t = s_cfg.getIntValue("default","timeout",10000);
    m_timeout = (u_int64_t)1000 * s_cfg.getIntValue(name(),"timeout",t);
    Debug(&module,DebugAll,"Initiating connection \"%s\" retry %d",conn.c_str(),retry);
    u_int64_t timeout = Time::now() + m_timeout;
    m_conn = PQconnectStart(conn.c_str());
    if (!m_conn) {
	Debug(&module,DebugGoOn,"Could not start connection for '%s'",name().c_str());
	return false;
    }
    PQsetnonblocking(m_conn,1);
    while (Time::now() < timeout) {
	switch (PQstatus(m_conn)) {
	    case CONNECTION_BAD:
		Debug(&module,DebugWarn,"Connection for '%s' failed: %s",name().c_str(),PQerrorMessage(m_conn));
		dropDb();
		return false;
	    case CONNECTION_OK:
		Debug(&module,DebugAll,"Connection for '%s' succeeded",name().c_str());
		if (m_first) {
		    m_first = false;
		    // first time we got connected - execute the initialization
		    const char* query = s_cfg.getValue(name(),"initquery");
		    if (query) {
			queryDb(query);
			return testDb();
		    }
		}
		return true;
	    default:
		break;
	}
	PQconnectPoll(m_conn);
	Thread::yield();
    }
    Debug(&module,DebugWarn,"Connection timed out for '%s'",name().c_str());
    dropDb();
    return false;
}

// drop the connection
void AAAHandler::dropDb()
{
    Lock lock(m_dbmutex);
    if (!m_conn)
	return;
    PGconn* tmp = m_conn;
    m_conn = 0;
    lock.drop();
    PQfinish(tmp);
}

// test it the connection is still OK
bool AAAHandler::testDb()
{
    return m_conn && (CONNECTION_OK == PQstatus(m_conn));
}

// public, thread safe version
bool AAAHandler::ok()
{
    Lock lock(m_dbmutex);
    return testDb();
}

// try to get up the connection, retry if we have to
bool AAAHandler::startDb()
{
    if (testDb())
	return true;
    for (int i = 0; i < m_retry; i++) {
	if (initDb(i))
	    return true;
	Thread::yield();
	if (testDb())
	    return true;
    }
    return false;
}

// perform the query, fill the message with data
//  return number of rows, -1 for non-retryable errors and -2 to retry
int AAAHandler::queryDbInternal(const char* query, Message* dest)
{
    Lock lock(m_dbmutex);
    if (!startDb())
	// no retry - startDb already tried and failed...
	return -1;

    u_int64_t timeout = Time::now() + m_timeout;
    if (!PQsendQuery(m_conn,query)) {
	// a connection failure cannot be detected at this point so any
	//  error must be caused by the query itself - bad syntax or so
	Debug(&module,DebugWarn,"Query \"%s\" for '%s' failed: %s",
	    query,name().c_str(),PQerrorMessage(m_conn));
	// non-retryable, query should be fixed
	return -1;
    }

    if (PQflush(m_conn)) {
	Debug(&module,DebugWarn,"Flush for '%s' failed: %s",
	    name().c_str(),PQerrorMessage(m_conn));
	dropDb();
	return -2;
    }

    int totalRows = 0;
    while (Time::now() < timeout) {
	PQconsumeInput(m_conn);
	if (PQisBusy(m_conn)) {
	    Thread::yield();
	    continue;
	}
	PGresult* res = PQgetResult(m_conn);
	if (!res) {
	    // last result already received and processed - exit successfully
	    Debug(&module,DebugAll,"Query for '%s' returned %d rows",name().c_str(),totalRows);
	    return totalRows;
	}
	ExecStatusType stat = PQresultStatus(res);
	switch (stat) {
	    case PGRES_TUPLES_OK:
		// we got some data - but maybe zero rows or binary...
		if (dest) {
		    int rows = PQntuples(res);
		    if ((rows > 0) && !PQbinaryTuples(res)) {
			totalRows += rows;
			if (totalRows > 1)
			    Debug(&module,DebugFail,"Query for '%s' returning %d (%d) rows!",
				name().c_str(),totalRows,rows);
			else
			    copyParams(*dest,res,m_result);
		    }
		}
		break;
	    case PGRES_COMMAND_OK:
		// no data returned
		break;
	    case PGRES_COPY_IN:
	    case PGRES_COPY_OUT:
		// data transfers - ignore them
		break;
	    default:
		Debug(&module,DebugWarn,"Query error: %s",PQresultErrorMessage(res));
	}
	PQclear(res);
    }
    Debug(&module,DebugWarn,"Query timed out for '%s'",name().c_str());
    dropDb();
    return -2;
}

// little helper function to make code cleaner
static bool failure(Message* m)
{
    if (m)
	m->setParam("error","failure");
    return false;
}

int AAAHandler::queryDb(const char* query, Message* dest)
{
    if (TelEngine::null(query))
	return -1;
    Debug(&module,DebugAll,"Performing query \"%s\" for '%s'",
	query,name().c_str());
    for (int i = 0; i < m_retry; i++) {
	int res = queryDbInternal(query,dest);
	if (res > -2) {
	    if (res < 0)
		failure(dest);
	    // ok or non-retryable error, get out of here
	    return res;
	}
    }
    failure(dest);
    return -2;
}

bool AAAHandler::received(Message& msg)
{
    if (m_query.null())
	return false;
    String query(m_query);
    replaceParams(query,msg);
    switch (m_type)
    {
	case Regist:
	    if (s_critical)
		return failure(&msg);
	    // no error -> ok
	    return queryDb(query,&msg) >= 0;
	    break;
	case Auth:
	    if (s_critical)
		return failure(&msg);
	    // ok if we got some result
	    return queryDb(query,&msg) > 0;
	    break;
	case Route:
	    if (s_critical)
		return failure(&msg);
	    {
	    // ok if we got some result
		int rows = queryDb(query,&msg);
		if ((rows == 1) && (msg.retValue().null())) {
		    // we know about the user but has no address of record
		    msg.retValue() = "-";
		    msg.setParam("error","offline");
		}
		return (rows > 0);
	    }
	    break;
	case UnRegist:
	    // no error check - we return false
	    queryDb(query,&msg);
	    break;
	case Timer:
	    {
		u_int32_t t = msg.msgTime().sec();
		if (t >= s_nextTime)
		    // we expire users every 30 seconds
		    s_nextTime = t + s_expire;
		else
		    return false;
	    }
	    // no error check at all - we return false
	    queryDb(query);
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
    bool error = (queryDb(query) < 0);
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
    Output("Loaded module Register for PostgreSQL");
}

RegistModule::~RegistModule()
{
    Output("Unloading module Register for PostgreSQL");
}

void RegistModule::statusParams(String& str)
{
    str.append("critical=",",") << s_critical;
    ObjList* l = s_handlers.skipNull();
    for (; l; l = l->skipNext()) {
	AAAHandler* h = static_cast<AAAHandler*>(l->get());
	str << "," << h->name() << "=" << h->ok();
    }
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
    handler->initDb();
    s_handlers.append(handler);
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
    Output("Initializing module Register for PostgreSQL");
    s_expire = s_cfg.getIntValue("general","expires",s_expire);
    addHandler("call.cdr",AAAHandler::Cdr);
    addHandler("linetracker",AAAHandler::Cdr);
    addHandler("user.auth",AAAHandler::Auth);
    addHandler("engine.timer",AAAHandler::Timer);
    addHandler("user.unregister",AAAHandler::UnRegist);
    addHandler("user.register",AAAHandler::Regist);
    addHandler("call.route",AAAHandler::Route);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
