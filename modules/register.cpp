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

/*
static unsigned s_route_rq = 0;
static unsigned s_route_err = 0;
static unsigned s_route_yes = 0;
static unsigned s_route_no = 0;
*/

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
    AAAHandler(const char* name, int type, int prio = 50);
    virtual ~AAAHandler();
    virtual bool received(Message& msg);
    bool ok();
    int queryDb(const char* query, Message* dest = 0);

private:
    bool initDb(int retry = 0);
    void dropDb();
    bool testDb();
    bool startDb();
    int queryDbInternal(const char* query, Message* dest);
    String m_query;
    String m_result;
    Mutex m_dbmutex;
    int m_type;
    PGconn *m_conn;
    int m_retry;
    u_int64_t m_timeout;
    bool m_first;
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
    void addHandler(const char *name, int type);
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

AAAHandler::AAAHandler(const char* name, int type, int prio)
    : MessageHandler(name,prio), m_dbmutex(true),
      m_type(type), m_conn(0), m_retry(0), m_timeout(0), m_first(true)
{
    initDb();
    s_handlers.append(this);
}

AAAHandler::~AAAHandler()
{
    s_handlers.remove(this,false);
    dropDb();
}

// initialize the database connection and handler data
bool AAAHandler::initDb(int retry)
{
    if (null())
	return false;
    Lock lock(m_dbmutex);
    m_query = s_cfg.getValue(*this,"query");
    if (m_query.null())
	return false;
    // allow specifying the raw connection string
    String conn(s_cfg.getValue(*this,"connection"));
    if (conn.null())
	conn = s_cfg.getValue("default","connection");
    if (conn.null()) {
	// else build it from pieces
	const char* host = s_cfg.getValue("default","host","localhost");
	host = s_cfg.getValue(*this,"host",host);
	int port = s_cfg.getIntValue("default","port",5432);
	port = s_cfg.getIntValue(*this,"port",port);
	const char* name = s_cfg.getValue("default","database","yate");
	name = s_cfg.getValue(*this,"database","yate");
	const char* user = s_cfg.getValue("default","user","postgres");
	user = s_cfg.getValue(*this,"user","postgres");
	const char* pass = s_cfg.getValue("default","password");
	pass = s_cfg.getValue(*this,"password");
	if (TelEngine::null(host) || (port <= 0) || TelEngine::null(name))
	    return false;
	conn << "host='" << host << "' port=" << port << " dbname='" << name << "'";
	if (user) {
	    conn << " user='" << user << "'";
	    if (pass)
		conn << " password='" << pass << "'";
	}
    }
    m_result = s_cfg.getValue(*this,"result");
    int t = s_cfg.getIntValue("default","retry",5);
    m_retry = s_cfg.getIntValue(*this,"retry",t);
    t = s_cfg.getIntValue("default","timeout",10000);
    m_timeout = (u_int64_t)1000 * s_cfg.getIntValue(*this,"timeout",t);
    Debug(&module,DebugAll,"Initiating connection \"%s\" retry %d",conn.c_str(),retry);
    u_int64_t timeout = Time::now() + m_timeout;
    m_conn = PQconnectStart(conn.c_str());
    if (!m_conn) {
	Debug(&module,DebugGoOn,"Could not start connection for '%s'",c_str());
	return false;
    }
    PQsetnonblocking(m_conn,1);
    while (Time::now() < timeout) {
	switch (PQstatus(m_conn)) {
	    case CONNECTION_BAD:
		Debug(&module,DebugWarn,"Connection for '%s' failed: %s",c_str(),PQerrorMessage(m_conn));
		dropDb();
		return false;
	    case CONNECTION_OK:
		Debug(&module,DebugAll,"Connection for '%s' succeeded",c_str());
		if (m_first) {
		    m_first = false;
		    // first time we got connected - execute the initialization
		    const char* query = s_cfg.getValue(*this,"initquery");
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
    Debug(&module,DebugWarn,"Connection timed out for '%s'",c_str());
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
	    query,c_str(),PQerrorMessage(m_conn));
	// non-retryable, query should be fixed
	return -1;
    }

    if (PQflush(m_conn)) {
	Debug(&module,DebugWarn,"Flush for '%s' failed: %s",
	    c_str(),PQerrorMessage(m_conn));
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
	    Debug(&module,DebugAll,"Query for '%s' returned %d rows",c_str(),totalRows);
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
				c_str(),totalRows,rows);
			else
			    copyParams(*dest,res);
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
    Debug(&module,DebugWarn,"Query timed out for '%s'",c_str());
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
	query,c_str());
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
	case Route:
	    if (s_critical)
		return failure(&msg);
	    // ok if we got some result
	    return queryDb(query,&msg) > 0;
	    break;
	case UnRegist:
	    // no error check - we return false
	    queryDb(query,&msg);
	    break;
	case Cdr:
	    {
		String tmp(msg.getValue("operation"));
		if (tmp != "finalize")
		    return false;
		// failure while accounting is critical
		bool error = (queryDb(query) < 0);
		if (s_critical != error) {
		    s_critical = error;
		    module.changed();
		}
		if (error)
		    failure(&msg);
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
	    }
	    // no error check at all - we return false
	    queryDb(query);
	    break;
    }
    return false;
}

#if 0
class StatusHandler : public MessageHandler
{
public:
    StatusHandler(const char *name, unsigned prio = 1)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

/**
 * I can't remeber why i have made this class :) 
 * Now i remeber, to be able to expire users.
 * */
class ExpireThread :  public Thread
{
public:
    ExpireThread(){};
    ~ExpireThread(){};
    void run(void);
};

void ExpireThread::run(void)
{
   if (querydb(query))
	Debug(DebugInfo,"i can't verify the expire time");
    

}




bool AuthHandler::received(Message &msg)
{
//    const char *calltime = c_safe(msg.getValue("time"));
    String username  = c_safe(msg.getValue("username"));
    
    Lock lock(dbmutex);
    if (!conn)
    	return false;

    String s = "SELECT password FROM register WHERE username='" + username + "'";
    PGresult *respgsql = PQexec(conn,(const char *)s);
    if (!respgsql || PQresultStatus(respgsql) != PGRES_TUPLES_OK)
    {
        Debug(DebugWarn,"Failed to query from database: %s",
	    PQerrorMessage(conn));
    	return false;
    }
    if (PQntuples(respgsql) == 0) {
        Debug(DebugAll,"No user.");
    	return false;
    }
    msg.retValue() << PQgetvalue(respgsql,0,0);
    return true;
};

bool RegistHandler::init()
{
    /**
     * We must clear the routing table when loading the new table, to not 
     * leave any garbage there
     */
    String s = "DELETE FROM routepaid";
    PGresult *respgsql = PQexec(conn,(const char *)s);
    if (PQresultStatus(respgsql) != PGRES_COMMAND_OK)
    {
        Debug(DebugWarn,"Failed to clear the routepaid table: %s",
	    PQerrorMessage(conn));
	return false;
    }
    return true;
}

bool RegistHandler::received(Message &msg)
{
    if (!m_init)
    {
	init();
	m_init= true;
    }
    String username  = c_safe(msg.getValue("username"));
    String techno  = c_safe(msg.getValue("driver"));
    String data  = c_safe(msg.getValue("data"));
    
    Lock lock(dbmutex);
    if (!conn)
    	return false;

    String c = "SELECT credit,price,e164,context FROM register WHERE username='" + username + "'";
    PGresult *respgsql = PQexec(conn,(const char *)c);
    if (!respgsql || PQresultStatus(respgsql) != PGRES_TUPLES_OK)
    {
        Debug(DebugWarn,"Failed to query from database: %s",
	    PQerrorMessage(conn));
    	return false;
    }
    if (PQntuples(respgsql) == 0) {
        Debug(DebugAll,"No credit.");
    	return false;
    }
    
    String price  = PQgetvalue(respgsql,0,1);
    String prefix = PQgetvalue(respgsql,0,2);
    String context = PQgetvalue(respgsql,0,3);
    if (price.null())
	price = 0;
    if (context.null())
	context = "default";

    c = "INSERT INTO routepaid (context,prefix,tehno,data,price,username) VALUES ('" + context + "','" + prefix + "','" + techno + "','" + data + "'," + price +",'" + username + "')";

    PGresult *respgsql1 = PQexec(conn,(const char *)c);
    if (!respgsql1 || PQresultStatus(respgsql1) != PGRES_COMMAND_OK)
        Debug(DebugWarn,"Failed to insert in database: %s",
	    PQerrorMessage(conn));
    msg.retValue() = prefix;
    return true;
};

bool UnRegistHandler::received(Message &msg)
{
    String username  = c_safe(msg.getValue("username"));
    
    Lock lock(dbmutex);
    if (!conn)
    	return false;

    String s = "DELETE from routepaid WHERE username='" + username + "'";
    PGresult *respgsql = PQexec(conn,(const char *)s);
    if (PQresultStatus(respgsql) != PGRES_COMMAND_OK)
    {
        Debug(DebugWarn,"Failed to query from database: %s",
	    PQerrorMessage(conn));
    	return false;
    }
    return true;
};

bool RouteHandler::received(Message &msg)
{
    u_int64_t tmr = Time::now();
    String called(msg.getValue("called"));
    if (called.null())
	return false;
    Lock lock(dbmutex);
    if (!conn)
    	return false;
    s_route_rq++;
    String context = c_safe(msg.getValue("context","default"));
    String s = "SELECT tehno,data,length (prefix) as lll,price"
	" from routepaid where prefix= substring('" +  called + "',1,length(prefix))"
	" and context='" + context + "' order by lll desc LIMIT 1";
    Debug(DebugInfo,"%s",s.c_str());
    PGresult *respgsql = PQexec(conn,(const char *)s);
    if (!respgsql || PQresultStatus(respgsql) != PGRES_TUPLES_OK) {
    {
        Debug(DebugWarn,"Failed to query from database: %s",
	    PQerrorMessage(conn));
	s_route_err++;
    	return false;
    }
    if (PQntuples(respgsql) == 0) {
        Debug(DebugAll,"No route.");
	s_route_no++;
    	return false;
    }
    msg.setParam("driver",PQgetvalue(respgsql,0,0));
    msg.retValue() = PQgetvalue(respgsql,0,1);
    Debug(DebugInfo,"Routing call to '%s' in context '%s' using '%s' tehnology and data in " FMT64 " usec",
		called.c_str(),context.c_str(),msg.retValue().c_str(),Time::now()-tmr);
    s_route_yes++;
    return true;
};

bool StatusHandler::received(Message &msg)
{
    msg.retValue() << "name=register,type=misc;conn=" << (conn != 0) <<"\n";
    return false;
}
#endif

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
	str << "," << *h << "=" << h->ok();
    }
}

void RegistModule::addHandler(const char *name, int type)
{
    if (!s_cfg.getBoolValue("general",name))
	return;
    int prio = s_cfg.getIntValue("default","priority",50);
    prio = s_cfg.getIntValue(name,"priority",prio);
    Output("Installing priority %d handler for '%s'",prio,name);
    Engine::install(new AAAHandler(name,type,prio));
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
    addHandler("user.register",AAAHandler::Regist);
    addHandler("user.unregister",AAAHandler::UnRegist);
    addHandler("user.auth",AAAHandler::Auth);
    addHandler("call.route",AAAHandler::Route);
    addHandler("call.cdr",AAAHandler::Cdr);
    addHandler("engine.timer",AAAHandler::Timer);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
