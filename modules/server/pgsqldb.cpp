/**
 * pgsqldb.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This is the PostgreSQL support from Yate.
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

#include <stdio.h>
#include <libpq-fe.h>

using namespace TelEngine;
namespace { // anonymous

static ObjList s_conns;
Mutex s_conmutex(false,"PgSQL::conn");
static unsigned int s_failedConns;

class PgConn : public RefObject, public Mutex
{
public:
    PgConn(const NamedList* sect);
    ~PgConn();
    virtual const String& toString() const
	{ return m_name; }
    virtual void destroyed();

    bool ok();
    int queryDb(const char* query, Message* dest = 0);
    bool initDb(int retry = 0);

    inline unsigned int total()
	{ return m_totalQueries; }
    inline unsigned int failed()
	{ return m_failedQueries; }
    inline unsigned int errorred()
	{ return m_errorQueries; }
    inline unsigned int queryTime()
        { return (unsigned int) m_queryTime; }
    inline void setConn(bool conn = true)
	{ m_hasConn = conn; }
    inline bool hasConn()
	{ return m_hasConn; }

private:
    void dropDb();
    bool testDb();
    bool startDb();
    int queryDbInternal(const char* query, Message* dest);
    String m_name,m_connection;
    String m_encoding;
    int m_retry;
    u_int64_t m_timeout;
    PGconn *m_conn;

    // stat counters
    unsigned int m_totalQueries;
    unsigned int m_failedQueries;
    unsigned int m_errorQueries;
    u_int64_t m_queryTime;
    bool m_hasConn;
};

class PgHandler : public MessageHandler
{
public:
    PgHandler(unsigned int prio = 100)
	: MessageHandler("database",prio)
	{ }
    virtual bool received(Message& msg);
};

class PgModule : public Module
{
public:
    PgModule();
    ~PgModule();
protected:
    virtual void initialize();
    virtual void statusModule(String& str);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual void genUpdate(Message& msg);
private:
    bool m_init;
};

static PgModule module;

PgConn::PgConn(const NamedList* sect)
    : Mutex(true,"PgConn"),
      m_name(*sect), m_conn(0),
      m_totalQueries(0), m_failedQueries(0),
      m_errorQueries(0), m_queryTime(0), m_hasConn(false)
{
    m_connection = sect->getValue("connection");
    if (m_connection.null()) {
	// build connection string from pieces
	String tmp = sect->getValue("host","localhost");
	m_connection << "host='" << tmp << "'";
	tmp = sect->getValue("port");
	if (tmp)
	    m_connection << " port='" << tmp << "'";
	tmp = sect->getValue("database","yate");
	m_connection << " dbname='" << tmp << "'";
	tmp = sect->getValue("user","postgres");
	m_connection << " user='" << tmp << "'";
	tmp = sect->getValue("password");
	if (tmp)
	    m_connection << " password='" << tmp << "'";
    }
    m_timeout = (u_int64_t)1000 * sect->getIntValue("timeout",10000);
    if (m_timeout < 500000)
	m_timeout = 500000;
    m_retry = sect->getIntValue("retry",5);
    m_encoding = sect->getValue("encoding");
}

PgConn::~PgConn()
{
}

void PgConn::destroyed()
{
    s_conmutex.lock();
    s_conns.remove(this,false);
    s_conmutex.unlock();
    dropDb();
    Debug(&module,DebugInfo,"Database account '%s' destroyed",m_name.c_str());
}

// initialize the database connection and handler data
bool PgConn::initDb(int retry)
{
    Lock lock(this);
    // allow specifying the raw connection string
    Debug(&module,DebugAll,"Initiating connection \"%s\" retry %d",m_connection.c_str(),retry);
    u_int64_t timeout = Time::now() + m_timeout;
    m_conn = PQconnectStart(m_connection.c_str());
    if (!m_conn) {
	Debug(&module,DebugGoOn,"Could not start connection for '%s'",m_name.c_str());
	return false;
    }
    PQsetnonblocking(m_conn,1);
    Thread::msleep(1);
    PostgresPollingStatusType polling = PGRES_POLLING_OK;
    while (Time::now() < timeout) {
	if (PGRES_POLLING_WRITING == polling || PGRES_POLLING_READING == polling) {
	    // the Postgres library should have done all this internally...
	    SOCKET s = PQsocket(m_conn);
	    struct timeval tm;
	    Time::toTimeval(&tm,Thread::idleUsec());
	    fd_set fs;
	    FD_ZERO(&fs);
	    FD_SET(s,&fs);
	    ::select(s+1,
		((PGRES_POLLING_READING == polling) ? &fs : 0),
		((PGRES_POLLING_WRITING == polling) ? &fs : 0),
		0,&tm);
	    if (!FD_ISSET(s,&fs))
		continue;
	}
	polling = PQconnectPoll(m_conn);
	switch (PQstatus(m_conn)) {
	    case CONNECTION_BAD:
		Debug(&module,DebugWarn,"Connection for '%s' failed: %s",m_name.c_str(),PQerrorMessage(m_conn));
		dropDb();
		return false;
	    case CONNECTION_OK:
		Debug(&module,DebugAll,"Connection for '%s' succeeded",m_name.c_str());
		if (m_encoding && PQsetClientEncoding(m_conn,m_encoding))
		    Debug(&module,DebugWarn,"Failed to set encoding '%s' on connection '%s'",
			m_encoding.c_str(),m_name.c_str());
		return true;
	    default:
		break;
	}
	Thread::idle();
    }
    Debug(&module,DebugWarn,"Connection timed out for '%s'",m_name.c_str());
    dropDb();
    return false;
}

// drop the connection
void PgConn::dropDb()
{
    if (!m_conn)
	return;
    Lock mylock(this);
    if (!m_conn)
	return;
    PGconn* tmp = m_conn;
    m_conn = 0;
    mylock.drop();
    PQfinish(tmp);
}

// test it the connection is still OK
bool PgConn::testDb()
{
    return m_conn && (CONNECTION_OK == PQstatus(m_conn));
}

// public, thread safe version
bool PgConn::ok()
{
    Lock mylock(this,m_timeout);
    return mylock.locked() && testDb();
}

// try to get up the connection, retry if we have to
bool PgConn::startDb()
{
    setConn(true);
    if (testDb())
	return true;
    for (int i = 0; i < m_retry; i++) {
	if (initDb(i))
	    return true;
	Thread::yield();
	if (testDb())
	    return true;
    }
    setConn(false);
    return false;
}

// perform the query, fill the message with data
//  return number of rows, -1 for non-retryable errors and -2 to retry
int PgConn::queryDbInternal(const char* query, Message* dest)
{
    Lock mylock(this,m_timeout);
    if (!mylock.locked()) {
	Debug(&module,DebugWarn,"Failed to lock '%s' for " FMT64U " usec",
	    m_name.c_str(),m_timeout);
	return -1;
    }
    if (!startDb())
	// no retry - startDb already tried and failed...
	return -1;

    u_int64_t timeout = Time::now() + m_timeout;
    if (!PQsendQuery(m_conn,query)) {
	// a connection failure cannot be detected at this point so any
	//  error must be caused by the query itself - bad syntax or so
	Debug(&module,DebugWarn,"Query \"%s\" for '%s' failed: %s",
	    query,m_name.c_str(),PQerrorMessage(m_conn));
	dest->setParam("error",PQerrorMessage(m_conn));
	// non-retryable, query should be fixed
	return -1;
    }

    if (PQflush(m_conn)) {
	Debug(&module,DebugWarn,"Flush for '%s' failed: %s",
	    m_name.c_str(),PQerrorMessage(m_conn));
	dropDb();
	dest->setParam("error",PQerrorMessage(m_conn));
	return -2;
    }

    int totalRows = 0;
    int affectedRows = 0;
    while (Time::now() < timeout) {
	PQconsumeInput(m_conn);
	if (PQisBusy(m_conn)) {
	    Thread::yield();
	    continue;
	}
	PGresult* res = PQgetResult(m_conn);
	if (!res) {
	    // last result already received and processed - exit successfully
	    Debug(&module,DebugAll,"Query for '%s' returned %d rows, %d affected",m_name.c_str(),totalRows,affectedRows);
	    if (dest) {
		dest->setParam("rows",String(totalRows));
		dest->setParam("affected",String(affectedRows));
	    }
	    return totalRows;
	}
	ExecStatusType stat = PQresultStatus(res);
	switch (stat) {
	    case PGRES_TUPLES_OK:
		// we got some data - but maybe zero rows or binary...
		if (dest) {
		    affectedRows += String(PQcmdTuples(res)).toInteger();
		    int columns = PQnfields(res);
		    int rows = PQntuples(res);
		    if (rows > 0) {
			totalRows += rows;
			dest->setParam("columns",String(columns));
			if (dest->getBoolValue("results",true) && !PQbinaryTuples(res)) {
			    Array *a = new Array(columns,rows+1);
			    for (int k = 0; k < columns; k++) {
				ObjList* column = a->getColumn(k);
				if (column)
				    column->set(new String(PQfname(res,k)));
				else {
				    Debug(&module,DebugGoOn,"No array column for %d",k);
				    continue;
				}
				for (int j = 0; j < rows; j++) {
				    column = column->next();
				    if (!column) {
					// Stop now: we won't get the next row
					Debug(&module,DebugGoOn,"No array row %d in column %d",j + 1,k);
					break;
				    }
				    // skip over NULL values
				    if (PQgetisnull(res,j,k))
					continue;
				    GenObject* v = 0;
				    if (PQfformat(res,k))
					v = new DataBlock(PQgetvalue(res,j,k),PQgetlength(res,j,k));
				    else
					v = new String(PQgetvalue(res,j,k));
				    column->set(v);
				}
			    }
			    dest->userData(a);
			    a->deref();
			}
		    }
		}
		break;
	    case PGRES_COMMAND_OK:
		if (dest)
		    affectedRows += String(PQcmdTuples(res)).toInteger();
		// no data returned
		break;
	    case PGRES_COPY_IN:
	    case PGRES_COPY_OUT:
		// data transfers - ignore them
		break;
	    default:
		Debug(&module,DebugWarn,"Query error: %s",PQresultErrorMessage(res));
		dest->setParam("error",PQresultErrorMessage(res));
		m_errorQueries++;
		module.changed();
	}
	PQclear(res);
    }
    Debug(&module,DebugWarn,"Query timed out for '%s'",m_name.c_str());
    dest->setParam("error","query timeout");
    dropDb();
    return -2;
}

static bool failure(Message* m)
{
    if (m)
	m->setParam("error","failure");
    return false;
}

int PgConn::queryDb(const char* query, Message* dest)
{
    if (TelEngine::null(query))
	return -1;
    Debug(&module,DebugAll,"Performing query \"%s\" for '%s'",
	query,m_name.c_str());
    m_totalQueries++;
    module.changed();
    u_int64_t start = Time::now();
    for (int i = 0; i < m_retry; i++) {
	int res = queryDbInternal(query,dest);
	if (res > -2) {
	    if (res < 0) {
		failure(dest);
		m_failedQueries++;
		module.changed();
	    }
	    // ok or non-retryable error, get out of here
	    u_int64_t finish = Time::now() - start;
	    m_queryTime += finish;
	    module.changed();
	    return res;
	}
    }
    failure(dest);
    return -2;
}

static PgConn* findDb(const String& account)
{
    if (account.null())
	return 0;
    return static_cast<PgConn*>(s_conns[account]);
}

bool PgHandler::received(Message& msg)
{
    const String* str = msg.getParam("account");
    if (TelEngine::null(str))
	return false;
    s_conmutex.lock();
    RefPointer<PgConn> db = findDb(*str);
    s_conmutex.unlock();
    if (!db)
	return false;
    str = msg.getParam("query");
    if (!TelEngine::null(str))
	db->queryDb(*str,&msg);
    msg.setParam("dbtype","pgsqldb");
    return true;
}

PgModule::PgModule()
    : Module ("pgsqldb","database",true),m_init(false)
{
    Output("Loaded module PostgreSQL");
}

PgModule::~PgModule()
{
    Output("Unloading module PostgreSQL");
    s_conns.clear();
}

void PgModule::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=Total|Failed|Errors|AvgExecTime",",");
}

void PgModule::statusParams(String& str)
{
    s_conmutex.lock();
    str.append("conns=",",") << s_conns.count();
    str.append("failed=",",") << s_failedConns;
    s_conmutex.unlock();
}

void PgModule::statusDetail(String& str)
{
    s_conmutex.lock();
    for (unsigned int i = 0; i < s_conns.count(); i++) {
	PgConn* conn = static_cast<PgConn*>(s_conns[i]);
	str.append(conn->toString().c_str(),",") << "=" << conn->total() << "|" << conn->failed()
			<< "|" << conn->errorred() << "|";
	if (conn->total() - conn->failed() > 0)
	    str << (conn->queryTime() / (conn->total() - conn->failed()) / 1000); //miliseconds
        else
	    str << "0";
    }
    s_conmutex.unlock();
}

void PgModule::initialize()
{
    Module::initialize();
    if (m_init)
	return;
    m_init = true;
    Output("Initializing module PostgreSQL");
    Configuration cfg(Engine::configFile("pgsqldb"));
    Engine::install(new PgHandler(cfg.getIntValue("general","priority",100)));
    unsigned int i;
    for (i = 0; i < cfg.sections(); i++) {
	NamedList* sec = cfg.getSection(i);
	if (!sec || (*sec == "general"))
	    continue;
	PgConn* conn = new PgConn(sec);
	if (sec->getBoolValue("autostart",true))
	    conn->initDb();
	s_conmutex.lock();
	if (conn->ok())
	    s_conns.insert(conn);
	else
	    s_failedConns++;
	s_conmutex.unlock();
    }

}

void PgModule::genUpdate(Message& msg)
{
    unsigned int index = 0;
    for (ObjList* o = s_conns.skipNull(); o; o = o->next()) {
	PgConn* conn = static_cast<PgConn*>(o->get());
	msg.setParam(String("database.") << index,conn->toString());
	msg.setParam(String("total.") << index,String(conn->total()));
	msg.setParam(String("failed.") << index,String(conn->failed()));
	msg.setParam(String("errorred.") << index,String(conn->errorred()));
	msg.setParam(String("hasconn.") << index,String::boolText(conn->hasConn()));
	msg.setParam(String("querytime.") << index,String(conn->queryTime()));
	index++;
    }
    msg.setParam("count",String(index));
}
}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
