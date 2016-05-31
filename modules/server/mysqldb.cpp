/**
 * mysqldb.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This is the MySQL support from Yate.
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
#include <mysql.h>

#ifndef CLIENT_MULTI_STATEMENTS
#define CLIENT_MULTI_STATEMENTS 0
#define mysql_next_result(x) (-1)
#endif

#if (MYSQL_VERSION_ID > 41000)
#define HAVE_MYSQL_410
#else
#define mysql_warning_count(x) (0)
#define mysql_library_init mysql_server_init
#define mysql_library_end mysql_server_end
#endif

using namespace TelEngine;
namespace { // anonymous

class DbThread;
class DbQuery;
class DbQueryList;
class MySqlConn;
class MyAcct;
class InitThread;

static ObjList s_conns;
static unsigned int s_failedConns;
Mutex s_acctMutex(false,"MySQL::accts");

/**
  * Class MyConn
  * A MySQL connection
  */
class MyConn : public String
{
    friend class DbThread;
    friend class MyAcct;

public:

    inline MyConn(const String& name, MyAcct* conn)
	: String(name),
	  m_conn(0), m_owner(conn),
	  m_thread(0)
	{}
    ~MyConn();

    void closeConn();
    void runQueries();
    int queryDbInternal(DbQuery* query);

private:
    MYSQL* m_conn;
    MyAcct* m_owner;
    DbThread* m_thread;
    bool testDb();
};

/**
  * Class MyAcct
  * A MySQL database account
  */
class MyAcct : public String, public Mutex
{
    friend class MyConn;
public:
    MyAcct(const NamedList* sect);
    ~MyAcct();

    bool initDb();
    int initConns();
    void dropDb();
    inline bool ok() const
	{ return 0 != m_connections.skipNull(); }

    void appendQuery(DbQuery* query);

    void incTotal();
    void incFailed();
    void incErrorred();
    void incQueryTime(u_int64_t with);
    void lostConn();
    void resetConn();
    inline unsigned int total()
	{ return m_totalQueries; }
    inline unsigned int failed()
	{ return m_failedQueries; }
    inline unsigned int errorred()
	{ return m_errorQueries; }
    inline bool hasConn()
	{ return ((int)(m_poolSize - m_failedConns) > 0 ? true : false); }
    inline unsigned int queryTime()
        { return (unsigned int) m_queryTime; } //microseconds
    inline void setRetryWhen()
	{ m_retryWhen = Time::msecNow() + m_retryTime * 1000; }
    inline u_int64_t retryWhen()
	{ return m_retryWhen; }
    inline bool shouldRetryInit()
	{ return m_retryTime && m_connections.count() < (unsigned int)m_poolSize; }
    inline int poolSize()
	{ return m_poolSize; }
private:
    unsigned int m_timeout;
    // interval at which connection initialization should be tried
    unsigned int m_retryTime;
    // when should we try initialization again?
    u_int64_t m_retryWhen;

    String m_host;
    String m_user;
    String m_pass;
    String m_db;
    String m_unix;
    unsigned int m_port;
    bool m_compress;
    String m_encoding;

    int m_poolSize;
    ObjList m_connections;
    ObjList m_queryQueue;

    Semaphore m_queueSem;
    Mutex m_queueMutex;

    // stats counters
    unsigned int m_totalQueries;
    unsigned int m_failedQueries;
    unsigned int m_errorQueries;
    u_int64_t m_queryTime;
    unsigned int m_failedConns;
    Mutex m_incMutex;
};

/**
  * Class DbThread
  * Running thread for a MySQL connection
  */
class DbThread : public Thread
{
public:
    inline DbThread(MyConn* conn)
	: Thread("Mysql Connection"), m_conn(conn)
	{ }
    virtual void run();
    virtual void cleanup();
private:
    MyConn* m_conn;
};

/**
  * Class InitThread
  * Running thread for initializing MySQL connections
  */
class InitThread : public Thread
{
public:
    InitThread();
    ~InitThread();
    virtual void run();
    virtual void cleanup();
};

/**
  * Class MyModule
  * The MySQL database module
  */
class MyModule : public Module
{
public:
    MyModule();
    ~MyModule();
    void statusModule(String& str);
    virtual bool received(Message& msg, int id);
    InitThread* m_initThread;
    inline void startInitThread()
    {
	lock();
	if (!m_initThread)
	    (m_initThread = new InitThread())->startup();
	unlock();
    }
protected:
    virtual void initialize();
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual void genUpdate(Message& msg);
private:
    bool m_init;
};

/**
  * Class DbQuery
  * A MySQL query
  */
class DbQuery : public String, public Semaphore
{
    friend class MyConn;
public:
    inline DbQuery(const String& query, Message* msg)
	: String(query),
	  Semaphore(1,"MySQL::query"),
	  m_msg(msg), m_finished(false)
	{ DDebug( DebugAll, "DbQuery object [%p] created for query '%s'", this, c_str()); }

    inline ~DbQuery()
	{ m_msg = 0;
	  DDebug( DebugAll, "DbQuery object [%p] with query '%s' was destroyed", this, c_str()); }

    inline bool finished()
	{ return m_finished; }

    inline void setFinished()
	{ m_finished = true;
	  if (!m_msg)
	      destruct();
	}

private:
    Message* m_msg;
    bool m_finished;
};

static MyModule module;
static Mutex s_libMutex(false,"MySQL::lib");
static int s_libCounter = 0;


/**
  * Class MyHandler
  * Message handler for "database" message
  */
class MyHandler : public MessageHandler
{
public:
    MyHandler(unsigned int prio = 100)
	: MessageHandler("database",prio,module.name())
	{ }
    virtual bool received(Message& msg);
};


/**
  * MyConn
  */
MyConn::~MyConn()
{
    m_conn = 0;
    //closeConn();
    Debug(&module,DebugAll,"Database connection '%s' destroyed",c_str());
}

void MyConn::closeConn()
{
    DDebug(&module,DebugInfo,"Database connection '%s' trying to close %p",c_str(),m_conn);
    if (!m_conn)
	return;
    MYSQL* tmp = m_conn;
    m_conn = 0;
    mysql_close(tmp);
    String name(*this);
    if (m_owner)
	m_owner->m_connections.remove(this);
    Debug(&module,DebugInfo,"Database connection '%s' closed",name.c_str());
}

void MyConn::runQueries()
{
    while (m_conn && m_owner) {
	Thread::check();
	m_owner->m_queueSem.lock(Thread::idleUsec());

	Lock mylock(m_owner->m_queueMutex);
	DbQuery* query = static_cast<DbQuery*>(m_owner->m_queryQueue.remove(false));
	if (!query)
	    continue;
	m_owner->incTotal();
	mylock.drop();

	DDebug(&module,DebugAll,"Connection '%s' will try to execute '%s'",
	    c_str(),query->c_str());

	int res = queryDbInternal(query);
	if ((res < 0) && query->m_msg)
	    query->m_msg->setParam("error","failure");

	query->unlock();
	query->setFinished();
	DDebug(&module,DebugAll,"Connection '%s' finished executing query",c_str());
    }
}

bool MyConn::testDb()
{
     return m_conn && !mysql_ping(m_conn);
}

// perform the query, fill the message with data
//  return number of rows, -1 for error
int MyConn::queryDbInternal(DbQuery* query)
{
    if (!testDb()) {
 	m_owner->lostConn();
 	m_owner->incFailed();
	return -1;
    }
    m_owner->resetConn();
    u_int64_t start = Time::now();

    if (mysql_real_query(m_conn,query->safe(),query->length())) {
	Debug(&module,DebugWarn,"Query for '%s' failed: %s",c_str(),mysql_error(m_conn));
	u_int64_t duration = Time::now() - start;
	m_owner->incQueryTime(duration);
	m_owner->incErrorred();
	return -1;
    }

#ifdef DEBUG
    u_int64_t inter = Time::now();
#endif
    int total = 0;
    unsigned int warns = 0;
    unsigned int affected = 0;
    do {
	MYSQL_RES* res = mysql_store_result(m_conn);
	warns += mysql_warning_count(m_conn);
	affected += (unsigned int)mysql_affected_rows(m_conn);
	if (res) {
	    unsigned int cols = mysql_num_fields(res);
	    unsigned int rows = (unsigned int)mysql_num_rows(res);
	    Debug(&module,DebugAll,"Got result set %p rows=%u cols=%u",res,rows,cols);
	    total += rows;
	    if (query->m_msg) {
		MYSQL_FIELD* fields = mysql_fetch_fields(res);
		query->m_msg->setParam("columns",String(cols));
		query->m_msg->setParam("rows",String(rows));
		Array *a = new Array(cols,rows+1);
		unsigned int c;
		ObjList** columns = new ObjList*[cols];
		// get top of columns and add field names
		for (c = 0; c < cols; c++) {
		    columns[c] = a->getColumn(c);
		    if (columns[c])
			columns[c]->set(new String(fields[c].name));
		    else
			Debug(&module,DebugGoOn,"No array for column %u",c);
		}
		// and now data row by row
		for (unsigned int r = 1; r <= rows; r++) {
		    MYSQL_ROW row = mysql_fetch_row(res);
		    if (!row)
			break;
		    unsigned long* len = mysql_fetch_lengths(res);
		    for (c = 0; c < cols; c++) {
			// advance pointer in each column
			if (columns[c])
			    columns[c] = columns[c]->next();
			if (!(columns[c] && row[c]))
			    continue;
			bool binary = false;
			switch (fields[c].type) {
			    case MYSQL_TYPE_STRING:
			    case MYSQL_TYPE_VAR_STRING:
			    case MYSQL_TYPE_BLOB:
				// field may hold binary data
				binary = (63 == fields[c].charsetnr);
			    default:
				break;
			}
			if (binary) {
			    if (!len)
				continue;
			    columns[c]->set(new DataBlock(row[c],len[c]));
			}
			else
			    columns[c]->set(new String(row[c]));
		    }
		}
		delete[] columns;
		query->m_msg->userData(a);
		a->deref();
	    }
	    mysql_free_result(res);
	}
    } while (!mysql_next_result(m_conn));

    u_int64_t finish = Time::now();
    m_owner->incQueryTime(finish - start);
#ifdef DEBUG
    Debug(&module,DebugAll,"Query time for '%s' is %u+%u ms",c_str(),
	(unsigned int)((inter-start+500)/1000),
	(unsigned int)((finish-inter+500)/1000));
#endif

    if (query->m_msg) {
	query->m_msg->setParam("affected",String(affected));
	if (warns)
	    query->m_msg->setParam("warnings",String(warns));
    }
    return total;
}

/**
  * MyAcct
  */
MyAcct::MyAcct(const NamedList* sect)
    : String(*sect),
      Mutex(true,"MySQL::acct"),
      m_poolSize(sect->getIntValue("poolsize",1,1)),
      m_queueSem(m_poolSize,"MySQL::queue"),
      m_queueMutex(false,"MySQL::queue"),
      m_totalQueries(0), m_failedQueries(0), m_errorQueries(0),
      m_queryTime(0), m_failedConns(0),
      m_incMutex(false,"MySQL::inc")
{
    int tout = sect->getIntValue("timeout",10000);
    // round to seconds
    m_timeout = (tout + 500) / 1000;
    // but make sure it doesn't round to zero unless zero was requested
    if (tout && !m_timeout)
	m_timeout = 1;
    m_host = sect->getValue("host");
    m_user = sect->getValue("user","mysql");
    m_pass = sect->getValue("password");
    m_db = sect->getValue("database","yate");
    m_port = sect->getIntValue("port");
    m_unix = sect->getValue("socket");
    m_compress = sect->getBoolValue("compress");
    m_encoding = sect->getValue("encoding");

    Debug(&module, DebugNote, "For account '%s' connection pool size is %d",
	c_str(),m_poolSize);

    m_retryTime = sect->getIntValue("initretry",10); // default value is 10 seconds
    setRetryWhen(); // set retry interval
}

MyAcct::~MyAcct()
{
    Debug(&module, DebugNote, "~MyAcct()");
    s_conns.remove(this,false);
    // FIXME: should we try to do it from this thread?
    dropDb();
}

int MyAcct::initConns()
{
    int count = m_connections.count();

    DDebug(&module,DebugInfo,"MyAcct::initConns() - %d connections initialized already, pool required is of %d connections for '%s'",
	count,m_poolSize,c_str());
    // set new retry interval
    setRetryWhen();

    for (int i = count; i < m_poolSize; i++) {
	MyConn* mySqlConn = new MyConn(toString() + "." + String(i), this);

	mySqlConn->m_conn = mysql_init(mySqlConn->m_conn);
	if (!mySqlConn->m_conn) {
	    Debug(&module,DebugGoOn,"Could not start connection %d for '%s'",i,c_str());
	    return i;
	}
	DDebug(&module,DebugAll,"Connection '%s' for account '%s' was created",mySqlConn->c_str(),c_str());

	if (m_compress)
	    mysql_options(mySqlConn->m_conn,MYSQL_OPT_COMPRESS,0);

	mysql_options(mySqlConn->m_conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&m_timeout);

#ifdef MYSQL_OPT_READ_TIMEOUT
	mysql_options(mySqlConn->m_conn,MYSQL_OPT_READ_TIMEOUT,(const char*)&m_timeout);
#endif

#ifdef MYSQL_OPT_WRITE_TIMEOUT
	mysql_options(mySqlConn->m_conn,MYSQL_OPT_WRITE_TIMEOUT,(const char*)&m_timeout);
#endif

	if (mysql_real_connect(mySqlConn->m_conn,m_host,m_user,m_pass,m_db,m_port,m_unix,CLIENT_MULTI_STATEMENTS)) {

#ifdef MYSQL_OPT_RECONNECT
	    // this option must be set after connect - bug in mysql client library
	    my_bool reconn = 1;
	    mysql_options(mySqlConn->m_conn,MYSQL_OPT_RECONNECT,(const char*)&reconn);
#endif

#ifdef HAVE_MYSQL_SET_CHARSET
	    if (m_encoding && mysql_set_character_set(mySqlConn->m_conn,m_encoding))
		Debug(&module,DebugWarn,"Failed to set encoding '%s' on connection '%s'",
		    m_encoding.c_str(),mySqlConn->c_str());
#else
	    if (m_encoding)
		Debug(&module,DebugWarn,"Your client library does not support setting the character set");
#endif
	    DbThread* thread = new DbThread(mySqlConn);

	    if (thread->startup())
		m_connections.append(mySqlConn);
	    else {
	    	delete thread;
		TelEngine::destruct(mySqlConn);
	    }
	}
	else {
	    TelEngine::destruct(mySqlConn);
	    return i;
	}
    }

    return m_poolSize;
}

// initialize the database connection
bool MyAcct::initDb()
{
    Lock lock(this);
    // allow specifying the raw connection string
    Debug(&module,DebugNote,"Initiating pool of %d connections for '%s'",
	m_poolSize,c_str());

    s_libMutex.lock();
    if (0 == s_libCounter++) {
	DDebug(&module,DebugAll,"Initializing the MySQL library");
	mysql_library_init(0,0,0);
    }
    s_libMutex.unlock();

    int initCons = initConns();
    if (!initCons) {
	Alarm(&module,"database",DebugWarn,"Could not initiate any connections for account '%s', trying again in %d seconds",
	    c_str(),m_retryTime);
	module.startInitThread();
	return true;
    }
    if (initCons != m_poolSize) {
	Alarm(&module,"database",DebugMild,"Could initiate only %d of %d connections for account '%s', trying again in %d seconds",
	    initCons,m_poolSize,c_str(),m_retryTime);
	module.startInitThread();
    }
    return true;
}

// drop the connection
void MyAcct::dropDb()
{
    Lock mylock(this);

    ObjList* o = m_connections.skipNull();
    for (; o; o = o->skipNext()) {
	MyConn* c = static_cast<MyConn*>(o->get());
	if (c)
	    c->closeConn();
    }
    m_queryQueue.clear();
    Debug(&module,DebugNote,"Database account '%s' closed",c_str());

    s_libMutex.lock();
    if (0 == --s_libCounter) {
	DDebug(&module,DebugInfo,"Deinitializing the MySQL library");
	mysql_library_end();
    }
    s_libMutex.unlock();
}

void MyAcct::incTotal()
{
    XDebug(&module,DebugAll,"MyAcct::incTotal() [%p] - currently there have been %d queries",this,m_totalQueries);
    m_incMutex.lock();
    m_totalQueries++;
    m_incMutex.unlock();
    module.changed();
}

void MyAcct::incFailed()
{
    XDebug(&module,DebugAll,"MyAcct::incfailed() [%p] - currently there have been %d failed queries",this,m_failedQueries);
    m_incMutex.lock();
    m_failedQueries++;
    m_incMutex.unlock();
    module.changed();
}

void MyAcct::incErrorred()
{
    XDebug(&module,DebugAll,"MyAcct::incErrorred() [%p] - currently there have been %d errorred queries",this,m_errorQueries);
    m_incMutex.lock();
    m_errorQueries++;
    m_incMutex.unlock();
    module.changed();
}

void MyAcct::incQueryTime(u_int64_t with)
{
    XDebug(&module,DebugAll,"MyAcct::incQueryTime(with=" FMT64 ") [%p]",with,this);
    m_incMutex.lock();
    m_queryTime += with;
    m_incMutex.unlock();
    module.changed();
}

void MyAcct::lostConn()
{
    DDebug(&module,DebugAll,"MyAcct::lostConn() [%p]",this);
    m_incMutex.lock();
    if (m_failedConns < (unsigned int) m_poolSize)
	m_failedConns++;
    m_incMutex.unlock();
    module.changed();
}

void MyAcct::resetConn()
{
    DDebug(&module,DebugAll,"MyAcct::hasConn() [%p]",this);
    m_incMutex.lock();
    m_failedConns = 0;
    m_incMutex.unlock();
}

void MyAcct::appendQuery(DbQuery* query)
{
    DDebug(&module, DebugAll, "Account '%s' received a new query %p",c_str(),query);
    m_queueMutex.lock();
    m_queryQueue.append(query);
    m_queueMutex.unlock();
    m_queueSem.unlock();
}

/**
  * DbThread
  */
void DbThread::run()
{
    mysql_thread_init();
    m_conn->m_thread = this;
    m_conn->runQueries();
}

void DbThread::cleanup()
{
    Debug(&module,DebugInfo,"Cleaning up connection %p thread [%p]",m_conn,this);
    if (m_conn) {
	m_conn->m_thread = 0;
	m_conn->closeConn();
    }
    mysql_thread_end();
}


static MyAcct* findDb(const String& account)
{
    if (account.null())
	return 0;
    ObjList* l = s_conns.find(account);
    return l ? static_cast<MyAcct*>(l->get()) : 0;
}

/**
  * MyHandler
  */
bool MyHandler::received(Message& msg)
{
    const String* str = msg.getParam("account");
    if (TelEngine::null(str))
	return false;
    Lock lock(s_acctMutex);
    MyAcct* db = findDb(*str);
    if (!(db && db->ok()))
	return false;
    Lock lo(db);
    lock.drop();

    str = msg.getParam("query");
    if (!TelEngine::null(str)) {
	if (msg.getBoolValue("results",true)) {
	    DbQuery* q = new DbQuery(*str,&msg);
	    db->appendQuery(q);

	    while (!q->finished()) {
		Thread::check();
		q->lock(Thread::idleUsec());
	    }
	    TelEngine::destruct(q);
	}
	else
	    db->appendQuery(new DbQuery(*str,0));
    }
    msg.setParam("dbtype","mysqldb");
    return true;
}

/**
  * InitThread
  */
InitThread::InitThread()
    : Thread("Mysql Init")
{
    Debug(&module,DebugAll,"InitThread created [%p]",this);
}

InitThread::~InitThread()
{
    Debug(&module,DebugAll,"InitThread thread terminated [%p]",this);
    module.lock();
    module.m_initThread = 0;
    module.unlock();
}

void InitThread::run()
{
    mysql_thread_init();
    while(!Engine::exiting()) {
	Thread::sleep(1,true);
	bool retryAgain = false;
	s_acctMutex.lock();
	for (ObjList* o = s_conns.skipNull(); o; o = o->next()) {
	    MyAcct* acc = static_cast<MyAcct*>(o->get());
	    if (acc->shouldRetryInit() && acc->retryWhen() <= Time::msecNow()) {
		int count = acc->initConns();
		if (count < acc->poolSize())
		    Debug(&module,(count ? DebugMild : DebugWarn),"Account '%s' has %d initialized connections out of"
			" a pool of %d",acc->c_str(),count,acc->poolSize());
		else
		    Debug(&module,DebugInfo,"All connections for account '%s' have been initialized, pool size is %d",
			  acc->c_str(),acc->poolSize());
	    }
	    if (acc->shouldRetryInit())
		retryAgain = true;
	}
	s_acctMutex.unlock();
	if (!retryAgain)
	    break;
    }
}

void InitThread::cleanup()
{
    Debug(&module,DebugInfo,"InitThread::cleanup() [%p]",this);
    mysql_thread_end();
}

/**
  * MyModule
  */
MyModule::MyModule()
    : Module ("mysqldb","database",true),
      m_initThread(0),
      m_init(true)
{
    Output("Loaded module MySQL based on %s",mysql_get_client_info());
}

MyModule::~MyModule()
{
    Output("Unloading module MySQL");
    s_conns.clear();
    s_failedConns = 0;
    // Wait for expire thread termination
    while (m_initThread)
	Thread::idle();
}

void MyModule::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=Total|Failed|Errors|AvgExecTime",",");
}

void MyModule::statusParams(String& str)
{
    str.append("conns=",",") << s_conns.count();
    str.append("failed=",",") << s_failedConns;
}

void MyModule::statusDetail(String& str)
{
    for (unsigned int i = 0; i < s_conns.count(); i++) {
	MyAcct* acc = static_cast<MyAcct*>(s_conns[i]);
	str.append(acc->c_str(),",") << "=" << acc->total() << "|" << acc->failed() << "|" << acc->errorred() << "|";
	if (acc->total() - acc->failed() > 0)
	    str << (acc->queryTime() / (acc->total() - acc->failed()) / 1000); //miliseconds
        else
	    str << "0";
    }
}

void MyModule::initialize()
{
    Output("Initializing module MySQL");
    Module::initialize();
    Configuration cfg(Engine::configFile("mysqldb"));
    if (m_init)
	Engine::install(new MyHandler(cfg.getIntValue("general","priority",100)));
    installRelay(Halt);
    m_init = false;
    s_failedConns = 0;
    for (unsigned int i = 0; i < cfg.sections(); i++) {
	NamedList* sec = cfg.getSection(i);
	if (!sec || (*sec == "general"))
	    continue;
	MyAcct* conn = findDb(*sec);
	if (conn) {
	    if (!conn->ok()) {
		Debug(this,DebugNote,"Reinitializing connection '%s'",conn->toString().c_str());
		conn->initDb();
	    }
	    continue;
	}

	conn = new MyAcct(sec);
	s_conns.insert(conn);
	if (!conn->initDb()) {
	    s_conns.remove(conn);
	    s_failedConns++;
	}
    }
}

bool MyModule::received(Message& msg, int id)
{
    if (id == Halt) {
	if (m_initThread)
	    m_initThread->cancel(true);
    }
    return Module::received(msg,id);
}

void MyModule::genUpdate(Message& msg)
{
    Lock lock(this);
    unsigned int index = 0;
    for (ObjList* o = s_conns.skipNull(); o; o = o->next()) {
	MyAcct* acc = static_cast<MyAcct*>(o->get());
	msg.setParam(String("database.") << index,acc->toString());
	msg.setParam(String("total.") << index,String(acc->total()));
	msg.setParam(String("failed.") << index,String(acc->failed()));
	msg.setParam(String("errorred.") << index,String(acc->errorred()));
	msg.setParam(String("hasconn.") << index,String::boolText(acc->hasConn()));
	msg.setParam(String("querytime.") << index,String(acc->queryTime()));
	index++;
    }
    msg.setParam("count",String(index));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
