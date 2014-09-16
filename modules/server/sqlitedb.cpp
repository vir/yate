/**
 * sqlitedb.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This is the SQLite support from Yate.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2014 Null Team
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
#include <sqlite3.h>

using namespace TelEngine;
namespace { // anonymous

class SqlConn;

static ObjList s_accounts;
Mutex s_conmutex(false,"SQLite::acc");
static unsigned int s_failedConns;
static bool s_sharedCache = false;

// Database account holding the connection(s)
class SqlAccount : public RefObject, public Mutex
{
    friend class SqlConn;
public:
    SqlAccount(const NamedList& sect);
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
    String m_database;
    String m_initialize;
    int m_retry;
    u_int64_t m_timeout;
    SqlConn* m_connPool;
    unsigned int m_connPoolSize;
    // stat counters
    Mutex* m_statsMutex;
    unsigned int m_totalQueries;
    unsigned int m_failedQueries;
    unsigned int m_errorQueries;
    u_int64_t m_queryTime;
};

// A database connection
class SqlConn : public String
{
    friend class SqlAccount;
public:
    SqlConn(SqlAccount* account = 0);
    ~SqlConn();
    inline bool isBusy() const
	{ return m_busy; }
    inline void setBusy(bool busy)
	{ m_busy = busy; }
    inline int retries() const
	{ return m_account ? m_account->m_retry : 5; }
    // Test if the connection is still OK
    inline bool testDb() const
	{ return m_conn != 0; }
    bool initDb();
    void dropDb();
    // Perform the query, fill the message with data, retry in case of errors
    // Return number of rows, -1 for non-retryable errors and -2 for busy / timeout
    int queryDb(const char* query, Message* dest);
    virtual void destruct();
private:
    SqlAccount* m_account;
    bool m_busy;
    sqlite3* m_conn;
};

class SqlModule : public Module
{
public:
    SqlModule();
    ~SqlModule();
protected:
    virtual void initialize();
    virtual void statusModule(String& str);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual void genUpdate(Message& msg);
private:
    bool m_init;
};

static SqlModule module;


class SqlHandler : public MessageHandler
{
public:
    SqlHandler(unsigned int prio = 100)
	: MessageHandler("database",prio,module.name())
	{ }
    virtual bool received(Message& msg);
};


//
// SqlConn
//
SqlConn::SqlConn(SqlAccount* account)
    : m_account(account), m_busy(false),
    m_conn(0)
{
}

SqlConn::~SqlConn()
{
    dropDb();
}

// Initialize the database connection and handler data
bool SqlConn::initDb()
{
    if (testDb())
	return true;
    dropDb();
    Debug(&module,DebugAll,"'%s' opening database \"%s\" [%p]",
	c_str(),m_account->m_database.safe(),m_account);
    if (sqlite3_open(m_account->m_database.safe(),&m_conn) != SQLITE_OK) {
	Debug(&module,DebugWarn,"Failed to open database '%s': %s",
	    c_str(),sqlite3_errmsg(m_conn));
	dropDb();
	return false;
    }
    return true;
}

// Drop the connection
void SqlConn::dropDb()
{
    if (!m_conn)
	return;
    sqlite3* tmp = m_conn;
    m_conn = 0;
    XDebug(&module,DebugAll,"Database '%s' dropped [%p]",c_str(),m_account);
    if (sqlite3_close(tmp) != SQLITE_OK)
	Debug(&module,DebugWarn,"Failed to close database '%s': %s",
	    c_str(),sqlite3_errmsg(tmp));
}

// Perform the query, fill the message with data, retry in case of errors
// Return number of rows, -1 for non-retryable errors and -2 for busy / timeout
int SqlConn::queryDb(const char* query, Message* dest)
{
    if (!initDb())
	// no retry - initDb already tried and failed...
	return -1;
    bool results = dest && dest->getBoolValue("results",true);
    int changed = sqlite3_total_changes(m_conn);
    int rows = 0;
    int cols = -1;

    while (query) {
	while (';' == *query || ' ' == *query || '\t' == *query || '\r' == *query || '\n' == *query)
	    query++;
	if (!*query)
	    break;
	int retry = retries();
	sqlite3_stmt* stmt;
	const char* tail;
	int i;
	// Prepare statement, leave whatever unparsed in tail
	for (i = 0; i >= 0; i++) {
	    if (i)
		Thread::idle();
	    stmt = 0;
	    tail = 0;
	    switch (sqlite3_prepare_v2(m_conn,query,-1,&stmt,&tail)) {
		case SQLITE_OK:
		    i = -2;
		    break;
		case SQLITE_BUSY:
		case SQLITE_LOCKED:
		    sqlite3_finalize(stmt);
		    if (i >= retry)
			return -2;
		    continue;
		default:
		    {
			const char* errStr = sqlite3_errmsg(m_conn);
			Debug(&module,DebugWarn,"Query '%s' for '%s' prepare error: %s [%p]",
			    query,c_str(),errStr,m_account);
			if (dest)
			    dest->setParam("error",errStr);
		    }
		    sqlite3_finalize(stmt);
		    if (results)
			dest->userData(0);
		    return -1;
	    }
	}
	int lr = 0;
	int lc = 0;
	Array* a = 0;
	// Execute statement, collect results if needed
	for (i = 0; i >= 0; ) {
	    if (i)
		Thread::idle();
	    switch (sqlite3_step(stmt)) {
		case SQLITE_DONE:
		    if (lr || !rows) {
			rows = lr;
			cols = lc;
			if (results) {
			    dest->userData(a);
			    a = 0;
			}
		    }
		    i = -2;
		    break;
		case SQLITE_ROW:
		    if (!lr++)
			lc = sqlite3_column_count(stmt);
		    if (!results)
			continue;
		    if (!a) {
			a = new Array(lc,2);
			for (int j = 0; j < lc; j++)
			    a->set(new String(sqlite3_column_name(stmt,j)),j,0);
		    }
		    else
			a->addRow();
		    for (int j = 0; j < lc; j++) {
			GenObject* v = 0;
			switch (sqlite3_column_type(stmt,j)) {
			    case SQLITE_NULL:
				break;
			    case SQLITE_BLOB:
				{
				    // Must do this in two steps to guarantee call order
				    void* data = const_cast<void*>(sqlite3_column_blob(stmt,j));
				    v = new DataBlock(data,sqlite3_column_bytes(stmt,j));
				}
				break;
			    default:
				v = new String(reinterpret_cast<const char*>(sqlite3_column_text(stmt,j)));
			}
			a->set(v,j,lr);
		    }
		    continue;
		case SQLITE_BUSY:
		case SQLITE_LOCKED:
		    if (i++ >= retry) {
			sqlite3_reset(stmt);
			sqlite3_finalize(stmt);
			TelEngine::destruct(a);
			if (results)
			    dest->userData(0);
			return -2;
		    }
		    continue;
		default:
		    {
			const char* errStr = sqlite3_errmsg(m_conn);
			Debug(&module,DebugWarn,"Query '%s' for '%s' execute error: %s [%p]",
			    query,c_str(),errStr,m_account);
			if (dest)
			    dest->setParam("error",errStr);
		    }
		    sqlite3_reset(stmt);
		    sqlite3_finalize(stmt);
		    TelEngine::destruct(a);
		    if (results)
			dest->userData(0);
		    m_account->incErrorQueriesSafe();
		    return -1;
	    }
	}
	// Clean up statement and advance to next one
	sqlite3_reset(stmt);
	sqlite3_finalize(stmt);
	TelEngine::destruct(a);
	query = tail;
    }
    changed = sqlite3_total_changes(m_conn) - changed;
    if (dest) {
	dest->setParam("rows",String(rows));
	if (cols >= 0)
	    dest->setParam("columns",String(cols));
	dest->setParam("affected",String(changed));
    }
    return rows;
}

void SqlConn::destruct()
{
    dropDb();
    String::destruct();
}


//
// SqlAccount
//
SqlAccount::SqlAccount(const NamedList& sect)
    : Mutex(true,"SqlAccount"),
      m_name(sect),
      m_connPool(0), m_connPoolSize(0),
      m_statsMutex(&s_conmutex),
      m_totalQueries(0), m_failedQueries(0),
      m_errorQueries(0), m_queryTime(0)
{
    m_database = sect.getValue("database",":memory:");
    Engine::runParams().replaceParams(m_database);
    m_initialize = sect.getValue("initialize");
    if (m_initialize.startSkip("@",false)) {
	Engine::runParams().replaceParams(m_initialize);
	m_initialize.trimBlanks();
	File f;
	if (f.openPath(m_initialize)) {
	    int64_t len = f.length();
	    if (len > 0 && len <= 65536) {
		DataBlock d(0,len + 1);
		if (f.readData(d.data(),len) == len)
		    m_initialize = (const char*)d.data();
		else {
		    Debug(&module,DebugWarn,"Failed to read init file '%s'",m_initialize.c_str());
		    m_initialize.clear();
		}
	    }
	    else {
		Debug(&module,DebugWarn,"Empty or too long file '%s'",m_initialize.c_str());
		m_initialize.clear();
	    }
	}
	else {
	    Debug(&module,DebugWarn,"Missing init file '%s'",m_initialize.c_str());
	    m_initialize.clear();
	}
    }
    m_timeout = (u_int64_t)1000 * sect.getIntValue("timeout",2000);
    if (m_timeout < 100000)
	m_timeout = 100000;
    m_retry = sect.getIntValue("retry",5,0,100,false);
    // Can create just one connection to temporary or non shared cache in-memory databases
    bool shared = s_sharedCache && !m_database.null();
    shared = shared && (m_database.find(":memory:") < 0) && (m_database.find("mode=memory") < 0);
    shared = shared || (m_database.startsWith("file:") && (m_database.find("cache=shared") >= 0));
    m_connPoolSize = sect.getIntValue("poolsize",1,1);
    if ((m_connPoolSize > 1) && !shared) {
	Debug(&module,DebugConf,"Disabling pooling for non shared cache account '%s'",
	    m_name.c_str());
	m_connPoolSize = 1;
    }
    m_connPool = new SqlConn[m_connPoolSize];
    for (unsigned int i = 0; i < m_connPoolSize; i++) {
	m_connPool[i].m_account = this;
	m_connPool[i].assign(m_name + "." + String(i + 1));
    }
    Debug(&module,DebugInfo,"Database account '%s' created poolsize=%u [%p]",
	m_name.c_str(),m_connPoolSize,this);
}

// Init the connections for the account, run init query
bool SqlAccount::initDb()
{
    bool ok = false;
    for (unsigned int i = 0; i < m_connPoolSize; i++) {
	ok = m_connPool[i].initDb() || ok;
	if (ok && m_initialize && (i == 0) && (m_connPool[i].queryDb(m_initialize,0) < 0))
	    Debug(&module,DebugWarn,"Failed to run initializer for account '%s'",m_name.c_str());
    }
    return ok;
}

void SqlAccount::destroyed()
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
void SqlAccount::dropDb()
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

int SqlAccount::queryDb(const char* query, Message* dest)
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
	SqlConn* conn = 0;
	SqlConn* notConnected = 0;
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

bool SqlAccount::hasConn()
{
    for (unsigned int i = 0; i < m_connPoolSize; i++)
	if (m_connPool[i].testDb())
	    return true;
    return false;
}

static SqlAccount* findDb(const String& account)
{
    if (account.null())
	return 0;
    return static_cast<SqlAccount*>(s_accounts[account]);
}

bool SqlHandler::received(Message& msg)
{
    const String* str = msg.getParam("account");
    if (TelEngine::null(str))
	return false;
    s_conmutex.lock();
    RefPointer<SqlAccount> db = findDb(*str);
    s_conmutex.unlock();
    if (!db)
	return false;
    str = msg.getParam("query");
    if (!TelEngine::null(str))
	db->queryDb(*str,&msg);
    db = 0;
    msg.setParam("dbtype","sqlitedb");
    return true;
}

SqlModule::SqlModule()
    : Module ("sqlitedb","database",true),m_init(false)
{
    String tmp(sqlite3_libversion());
    if (tmp != SQLITE_VERSION)
	Debug(this,DebugConf,"SQLite version mismatch: expecting %s but library is %s",
	    SQLITE_VERSION,tmp.c_str());
    Output("Loaded module SQLite based on %s",SQLITE_VERSION);
}

SqlModule::~SqlModule()
{
    Output("Unloading module SQLite");
    s_accounts.clear();
    if (m_init) {
	sqlite3_shutdown();
	m_init = false;
    }
}

void SqlModule::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=Total|Failed|Errors|AvgExecTime",",");
}

void SqlModule::statusParams(String& str)
{
    s_conmutex.lock();
    str.append("conns=",",") << s_accounts.count();
    str.append("failed=",",") << s_failedConns;
    s_conmutex.unlock();
}

void SqlModule::statusDetail(String& str)
{
    s_conmutex.lock();
    for (ObjList* o = s_accounts.skipNull(); o; o = o->skipNext()) {
	SqlAccount* acc = static_cast<SqlAccount*>(o->get());
	str.append(acc->toString().c_str(),",") << "=" << acc->total() << "|" << acc->failed()
	    << "|" << acc->errorred() << "|";
	if (acc->total() - acc->failed() > 0)
	    str << (acc->queryTime() / (acc->total() - acc->failed()) / 1000); //miliseconds
        else
	    str << "0";
    }
    s_conmutex.unlock();
}

void SqlModule::initialize()
{
    Module::initialize();
    if (m_init)
	return;
    Output("Initializing module SQLite");
    Configuration cfg(Engine::configFile("sqlitedb"));
    s_sharedCache = cfg.getBoolValue("general","shared_cache",false);
    int err = sqlite3_initialize();
    if (err != SQLITE_OK) {
	Alarm(this,DebugWarn,"SQLite initialize failed, code %d",err);
	return;
    }
    sqlite3_enable_shared_cache(s_sharedCache);
    unsigned int i;
    for (i = 0; i < cfg.sections(); i++) {
	NamedList* sec = cfg.getSection(i);
	if (!sec || (*sec == "general"))
	    continue;
	SqlAccount* acc = new SqlAccount(*sec);
	if (sec->getBoolValue("autostart",true) && !acc->initDb())
	    TelEngine::destruct(acc);
	s_conmutex.lock();
	if (acc) {
	    s_accounts.insert(acc);
	    m_init = true;
	}
	else
	    s_failedConns++;
	s_conmutex.unlock();
    }
    if (m_init)
	Engine::install(new SqlHandler(cfg.getIntValue("general","priority",100)));
    else
	sqlite3_shutdown();
}

void SqlModule::genUpdate(Message& msg)
{
    unsigned int index = 0;
    s_conmutex.lock();
    for (ObjList* o = s_accounts.skipNull(); o; o = o->skipNext()) {
	SqlAccount* acc = static_cast<SqlAccount*>(o->get());
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
