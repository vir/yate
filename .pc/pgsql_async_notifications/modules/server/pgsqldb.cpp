/**
 * pgsqldb.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This is the PostgreSQL support from Yate.
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

#include <stdio.h>
#include <libpq-fe.h>

using namespace TelEngine;
namespace { // anonymous

class PGConn;                            // A database connection
class PgAccount;                         // Database account holding the connection(s)

static ObjList s_accounts;
Mutex s_conmutex(false,"PgSQL::acc");
static unsigned int s_failedConns;

// A database connection
class PgConn : public String
{
    friend class PgAccount;
public:
    PgConn(PgAccount* account = 0);
    ~PgConn();
    inline bool isBusy() const
	{ return m_busy; }
    inline void setBusy(bool busy)
	{ m_busy = busy; }
    // Test if the connection is still OK
    inline bool testDb() const
	{ return m_conn && (CONNECTION_OK == PQstatus(m_conn)); }
    bool initDb();
    void dropDb();
    // Perform the query, fill the message with data
    // Return number of rows, -1 for non-retryable errors and -2 to retry
    int queryDb(const char* query, Message* dest);
    virtual void destruct();
private:
    // Init DB connection
    bool initDbInternal(int retry);
    // Perform the query, fill the message with data
    // Return number of rows, -1 for non-retryable errors and -2 to retry
    int queryDbInternal(const char* query, Message* dest);

    PgAccount* m_account;
    bool m_busy;
    PGconn* m_conn;
};

// Database account holding the connection(s)
class PgAccount : public RefObject, public Mutex
{
    friend class PgConn;
public:
    PgAccount(const NamedList& sect);
    // Try to initialize DB connections. Return true if at least one of them is active
    bool initDb();
    // Make a query
    int queryDb(const char* query, Message* dest);
    bool hasConn();
    virtual const String& toString() const
	{ return m_name; }
    virtual void destroyed();

    inline unsigned int total()
	{ return m_totalQueries; }
    inline unsigned int failed()
	{ return m_failedQueries; }
    inline unsigned int errorred()
	{ return m_errorQueries; }
    inline unsigned int queryTime()
        { return (unsigned int) m_queryTime; }

protected:
    inline void incErrorQueriesSafe() {
	    Lock mylock(m_statsMutex);
	    m_errorQueries++;
	}

private:
    void dropDb();

    String m_name;
    String m_connection;
    String m_encoding;
    int m_retry;
    u_int64_t m_timeout;
    PgConn* m_connPool;
    unsigned int m_connPoolSize;
    // stat counters
    Mutex* m_statsMutex;
    unsigned int m_totalQueries;
    unsigned int m_failedQueries;
    unsigned int m_errorQueries;
    u_int64_t m_queryTime;
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


class PgHandler : public MessageHandler
{
public:
    PgHandler(unsigned int prio = 100)
	: MessageHandler("database",prio,module.name())
	{ }
    virtual bool received(Message& msg);
};


//
// PgConn
//
PgConn::PgConn(PgAccount* account)
    : m_account(account), m_busy(false),
    m_conn(0)
{
}

PgConn::~PgConn()
{
    dropDb();
}

// Initialize the database connection and handler data
bool PgConn::initDb()
{
    if (testDb())
	return true;
    int retry = m_account->m_retry;
    for (int i = 0; i < retry; i++) {
	if (initDbInternal(i + 1))
	    return true;
	Thread::yield();
	if (testDb())
	    return true;
    }
    return false;
}

// Drop the connection
void PgConn::dropDb()
{
    if (!m_conn)
	return;
    PGconn* tmp = m_conn;
    m_conn = 0;
    XDebug(&module,DebugAll,"Connection '%s' dropped [%p]",c_str(),m_account);
    PQfinish(tmp);
}

// Perform the query, fill the message with data
// Return number of rows, -1 for non-retryable errors and -2 to retry
int PgConn::queryDb(const char* query, Message* dest)
{
    int retry = m_account->m_retry;
    for (int i = 0; i < retry; i++) {
	XDebug(&module,DebugAll,"Connection '%s' performing query (retry=%d): %s [%p]",
	    c_str(),i + 1,query,m_account);
	int res = queryDbInternal(query,dest);
	if (res > -2)
	    return res;
    }
    return -2;
}

void PgConn::destruct()
{
    dropDb();
    String::destruct();
}

// Init DB connection
bool PgConn::initDbInternal(int retry)
{
    dropDb();
    // Allow specifying the raw connection string
    Debug(&module,DebugAll,"'%s' intializing connection \"%s\" retry %d [%p]",
	c_str(),m_account->m_connection.c_str(),retry,m_account);
    u_int64_t timeout = Time::now() + m_account->m_timeout;
    m_conn = PQconnectStart(m_account->m_connection.c_str());
    if (!m_conn) {
	Debug(&module,DebugGoOn,"Could not start connection for '%s' [%p]",c_str(),m_account);
	return false;
    }
    PQsetnonblocking(m_conn,1);
    Thread::msleep(1);
    PostgresPollingStatusType polling = PGRES_POLLING_OK;
    struct timeval tm;
    Time::toTimeval(&tm,Thread::idleUsec());
    while (Time::now() < timeout) {
	if (PGRES_POLLING_WRITING == polling || PGRES_POLLING_READING == polling) {
	    // The Postgres library should have done all this internally...
	    Socket sock(PQsocket(m_conn));
	    bool ok = sock.canSelect();
	    bool fatal = false;
	    if (ok) {
		ok = false;
		bool* readOk = (PGRES_POLLING_READING == polling) ? &ok : 0;
		bool* writeOk = (PGRES_POLLING_WRITING == polling) ? &ok : 0;
		if (!sock.select(readOk,writeOk,0,&tm)) {
		    if (sock.canRetry()) {
			Thread::idle();
			ok = false;
		    }
		    else {
		        fatal = true;
			Debug(&module,DebugWarn,
			    "Connection for '%s' failed: socket select failed [%p]",
			    c_str(),m_account);
		    }
		}

	    }
	    else {
		fatal = true;
		Debug(&module,DebugWarn,
		    "Connection for '%s' failed: socket not selectable [%p]",
		    c_str(),m_account);
	    }
	    sock.detach();
	    if (fatal) {
		dropDb();
		return false;
	    }
	    if (!ok)
		continue;
	}
	polling = PQconnectPoll(m_conn);
	switch (PQstatus(m_conn)) {
	    case CONNECTION_BAD:
		Debug(&module,DebugWarn,"Connection for '%s' failed: %s [%p]",
		    c_str(),PQerrorMessage(m_conn),m_account);
		dropDb();
		return false;
	    case CONNECTION_OK:
		Debug(&module,DebugAll,"Connection for '%s' succeeded [%p]",c_str(),m_account);
		if (m_account->m_encoding && PQsetClientEncoding(m_conn,m_account->m_encoding))
		    Debug(&module,DebugWarn,
			"Failed to set encoding '%s' on connection '%s' [%p]",
			m_account->m_encoding.c_str(),c_str(),m_account);
		return true;
	    default:
		break;
	}
	Thread::idle();
	if (Thread::check(false))
	    return false;
    }
    Debug(&module,DebugWarn,"Connection for '%s' timed out [%p]",c_str(),m_account);
    dropDb();
    return false;
}

// Perform the query, fill the message with data
// Return number of rows, -1 for non-retryable errors and -2 to retry
int PgConn::queryDbInternal(const char* query, Message* dest)
{
    if (!initDb())
	// no retry - initDb already tried and failed...
	return -1;
    u_int64_t timeout = Time::now() + m_account->m_timeout;
    if (!PQsendQuery(m_conn,query)) {
	// a connection failure cannot be detected at this point so any
	//  error must be caused by the query itself - bad syntax or so
	Debug(&module,DebugWarn,"Query '%s' for '%s' failed: %s [%p]",
	    query,c_str(),PQerrorMessage(m_conn),m_account);
	if (dest)
	    dest->setParam("error",PQerrorMessage(m_conn));
	// non-retryable, query should be fixed
	return -1;
    }

    if (PQflush(m_conn)) {
	Debug(&module,DebugWarn,"Flush for '%s' failed: %s [%p]",
	    c_str(),PQerrorMessage(m_conn),m_account);
	dropDb();
	if (dest)
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
	    Debug(&module,DebugAll,"Query for '%s' returned %d rows, %d affected [%p]",
		c_str(),totalRows,affectedRows,m_account);
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
				    Debug(&module,DebugGoOn,
					"Query '%s' for '%s': No array column for %d [%p]",
					query,c_str(),k,m_account);
				    continue;
				}
				for (int j = 0; j < rows; j++) {
				    column = column->next();
				    if (!column) {
					// Stop now: we won't get the next row
					Debug(&module,DebugGoOn,
					    "Query '%s' for '%s': No array row %d in column %d [%p]",
					    query,c_str(),j + 1,k,m_account);
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
		Debug(&module,DebugWarn,"Query '%s' for '%s' error: %s [%p]",
		    query,c_str(),PQresultErrorMessage(res),m_account);
		if (dest)
		    dest->setParam("error",PQresultErrorMessage(res));
		m_account->incErrorQueriesSafe();
		module.changed();
	}
	PQclear(res);
    }
    Debug(&module,DebugWarn,"Query timed out for '%s' [%p]",c_str(),m_account);
    if (dest)
	dest->setParam("error","query timeout");
    dropDb();
    return -2;
}


//
// PgAccount
//
PgAccount::PgAccount(const NamedList& sect)
    : Mutex(true,"PgAccount"),
      m_name(sect),
      m_connPool(0), m_connPoolSize(0),
      m_statsMutex(&s_conmutex),
      m_totalQueries(0), m_failedQueries(0),
      m_errorQueries(0), m_queryTime(0)
{
    m_connection = sect.getValue("connection");
    if (m_connection.null()) {
	// build connection string from pieces
	String tmp = sect.getValue("host","localhost");
	m_connection << "host='" << tmp << "'";
	tmp = sect.getValue("port");
	if (tmp)
	    m_connection << " port='" << tmp << "'";
	tmp = sect.getValue("database","yate");
	m_connection << " dbname='" << tmp << "'";
	tmp = sect.getValue("user","postgres");
	m_connection << " user='" << tmp << "'";
	tmp = sect.getValue("password");
	if (tmp)
	    m_connection << " password='" << tmp << "'";
    }
    m_timeout = (u_int64_t)1000 * sect.getIntValue("timeout",10000);
    if (m_timeout < 500000)
	m_timeout = 500000;
    m_retry = sect.getIntValue("retry",5);
    m_encoding = sect.getValue("encoding");
    m_connPoolSize = sect.getIntValue("poolsize",1,1);
    m_connPool = new PgConn[m_connPoolSize];
    for (unsigned int i = 0; i < m_connPoolSize; i++) {
	m_connPool[i].m_account = this;
	m_connPool[i].assign(m_name + "." + String(i + 1));
    }
    Debug(&module,DebugInfo,"Database account '%s' created poolsize=%u [%p]",
	m_name.c_str(),m_connPoolSize,this);
}

// Init the connections the connection
bool PgAccount::initDb()
{
    bool ok = false;
    for (unsigned int i = 0; i < m_connPoolSize; i++)
	ok = m_connPool[i].initDb() || ok;
    return ok;
}

void PgAccount::destroyed()
{
    s_conmutex.lock();
    s_accounts.remove(this,false);
    s_conmutex.unlock();
    dropDb();
    if (m_connPool)
	delete[] m_connPool;
    m_connPoolSize = 0;
    Debug(&module,DebugInfo,"Database account '%s' destroyed [%p]",m_name.c_str(),this);
}

// drop the connection
void PgAccount::dropDb()
{
    for (unsigned int i = 0; i < m_connPoolSize; i++)
	m_connPool[i].dropDb();
}

static bool failure(Message* m)
{
    if (m)
	m->setParam("error","failure");
    return false;
}

int PgAccount::queryDb(const char* query, Message* dest)
{
    if (TelEngine::null(query))
	return -1;
    Debug(&module,DebugAll,"Performing query \"%s\" for '%s'",
	query,m_name.c_str());
    // Use a while() to break to the end to update statistics
    int res = -1;
    u_int64_t start = Time::now();
    while (true) {
	Lock mylock(this,(long)m_timeout);
	if (!mylock.locked()) {
	    Debug(&module,DebugWarn,"Failed to lock '%s' for " FMT64U " usec",
		m_name.c_str(),m_timeout);
	    break;
	}
	// Find a non busy connection
	PgConn* conn = 0;
	PgConn* notConnected = 0;
	for (unsigned int i = 0; i < m_connPoolSize; i++) {
	    if (m_connPool[i].isBusy())
		continue;
	    if (m_connPool[i].testDb()) {
		conn = &(m_connPool[i]);
		break;
	    }
	    if (!notConnected)
		notConnected = &(m_connPool[i]);
        }
	if (!conn)
	    conn = notConnected;
	if (!conn) {
	    // Wait for a connection to become non-busy
	    // Round up the number of intervals to wait
	    unsigned int n = (unsigned int)((m_timeout + 999999) / Thread::idleUsec());
	    for (unsigned int i = 0; i < n; i++) {
		for (unsigned int j = 0; j < m_connPoolSize; j++) {
		    if (!m_connPool[j].isBusy() && m_connPool[j].testDb()) {
			conn = &(m_connPool[j]);
			break;
		    }
		}
		if (conn || Thread::check(false))
		    break;
		Thread::idle();
	    }
	}
	if (conn)
	    conn->setBusy(true);
	else
	    Debug(&module,DebugWarn,"Account '%s' failed to pick a connection [%p]",m_name.c_str(),this);
	mylock.drop();
	if (conn) {
	    res = conn->queryDb(query,dest);
	    conn->setBusy(false);
	}
	break;
    }
    Lock stats(m_statsMutex);
    m_totalQueries++;
    if (res > -2) {
	if (res < 0)
	    m_failedQueries++;
	u_int64_t finish = Time::now() - start;
	m_queryTime += finish;
    }
    stats.drop();
    module.changed();
    if (res < 0)
	failure(dest);
    return res;
}

bool PgAccount::hasConn()
{
    for (unsigned int i = 0; i < m_connPoolSize; i++)
	if (m_connPool[i].testDb())
	    return true;
    return false;
}

static PgAccount* findDb(const String& account)
{
    if (account.null())
	return 0;
    return static_cast<PgAccount*>(s_accounts[account]);
}

bool PgHandler::received(Message& msg)
{
    const String* str = msg.getParam("account");
    if (TelEngine::null(str))
	return false;
    s_conmutex.lock();
    RefPointer<PgAccount> db = findDb(*str);
    s_conmutex.unlock();
    if (!db)
	return false;
    str = msg.getParam("query");
    if (!TelEngine::null(str))
	db->queryDb(*str,&msg);
    db = 0;
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
    s_accounts.clear();
}

void PgModule::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=Total|Failed|Errors|AvgExecTime",",");
}

void PgModule::statusParams(String& str)
{
    s_conmutex.lock();
    str.append("conns=",",") << s_accounts.count();
    str.append("failed=",",") << s_failedConns;
    s_conmutex.unlock();
}

void PgModule::statusDetail(String& str)
{
    s_conmutex.lock();
    for (ObjList* o = s_accounts.skipNull(); o; o = o->skipNext()) {
	PgAccount* acc = static_cast<PgAccount*>(o->get());
	str.append(acc->toString().c_str(),",") << "=" << acc->total() << "|" << acc->failed()
	    << "|" << acc->errorred() << "|";
	if (acc->total() - acc->failed() > 0)
	    str << (acc->queryTime() / (acc->total() - acc->failed()) / 1000); //miliseconds
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
	PgAccount* acc = new PgAccount(*sec);
	if (sec->getBoolValue("autostart",true) && !acc->initDb())
	    TelEngine::destruct(acc);
	s_conmutex.lock();
	if (acc)
	    s_accounts.insert(acc);
	else
	    s_failedConns++;
	s_conmutex.unlock();
    }
}

void PgModule::genUpdate(Message& msg)
{
    unsigned int index = 0;
    s_conmutex.lock();
    for (ObjList* o = s_accounts.skipNull(); o; o = o->skipNext()) {
	PgAccount* acc = static_cast<PgAccount*>(o->get());
	msg.setParam(String("database.") << index,acc->toString());
	msg.setParam(String("total.") << index,String(acc->total()));
	msg.setParam(String("failed.") << index,String(acc->failed()));
	msg.setParam(String("errorred.") << index,String(acc->errorred()));
	msg.setParam(String("hasconn.") << index,String::boolText(acc->hasConn()));
	msg.setParam(String("querytime.") << index,String(acc->queryTime()));
	index++;
    }
    s_conmutex.unlock();
    msg.setParam("count",String(index));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
