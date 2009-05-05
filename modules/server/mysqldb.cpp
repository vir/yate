/**
 * mysqldb.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This is the MySQL support from Yate.
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

static ObjList s_conns;
Mutex s_conmutex(false,"MySQL::conn");

class MyConn : public GenObject, public Mutex
{
    friend class DbThread;
public:
    MyConn(const NamedList* sect);
    ~MyConn();
    virtual const String& toString() const
	{ return m_name; }

    int queryDb(const char* query, Message* dest = 0);
    bool initDb();
    void dropDb();
    inline bool ok() const
	{ return 0 != m_thread; }
    void runQueries();

private:
    bool testDb();
    bool startDb();
    int queryDbInternal();
    String m_name;
    unsigned int m_timeout;
    MYSQL* m_conn;
    DbThread* m_thread;
    String m_host;
    String m_user;
    String m_pass;
    String m_db;
    String m_unix;
    unsigned int m_port;
    bool m_compress;
    String m_encoding;
    String m_query;
    Message* m_msg;
    int m_res;
    volatile bool m_go;
};

class DbThread : public Thread
{
public:
    inline DbThread(MyConn* conn)
	: Thread("mysqldb"), m_conn(conn)
	{ }
    virtual void run();
    virtual void cleanup();
private:
    MyConn* m_conn;
};

class MyHandler : public MessageHandler
{
public:
    MyHandler(unsigned int prio = 100)
	: MessageHandler("database",prio)
	{ }
    virtual bool received(Message& msg);
};

class MyModule : public Module
{
public:
    MyModule();
    ~MyModule();
protected:
    virtual void initialize();
    virtual void statusParams(String& str);
private:
    bool m_init;
};

static MyModule module;
static Mutex s_libMutex(false,"MySQL::lib");
static int s_libCounter = 0;

MyConn::MyConn(const NamedList* sect)
    : Mutex(true,"MyConn"),
      m_name(*sect),
      m_conn(0), m_thread(0), m_msg(0), m_go(false)
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
}

MyConn::~MyConn()
{ 
    s_conns.remove(this,false);
    // FIXME: should we try to do it from this thread?
    dropDb();
}

// initialize the database connection
bool MyConn::initDb()
{
    Lock lock(this);
    // allow specifying the raw connection string
    Debug(&module,DebugInfo,"Initiating connection for '%s'",m_name.c_str());
    m_conn = mysql_init(m_conn);
    if (!m_conn) {
	Debug(&module,DebugGoOn,"Could not start connection for '%s'",m_name.c_str());
	return false;
    }
    if (m_compress)
	mysql_options(m_conn,MYSQL_OPT_COMPRESS,0);
    mysql_options(m_conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&m_timeout);
#ifdef MYSQL_OPT_READ_TIMEOUT
    mysql_options(m_conn,MYSQL_OPT_READ_TIMEOUT,(const char*)&m_timeout);
#endif
#ifdef MYSQL_OPT_WRITE_TIMEOUT
    mysql_options(m_conn,MYSQL_OPT_WRITE_TIMEOUT,(const char*)&m_timeout);
#endif
    if (mysql_real_connect(m_conn,m_host,m_user,m_pass,m_db,m_port,m_unix,CLIENT_MULTI_STATEMENTS)) {
#ifdef MYSQL_OPT_RECONNECT
	// this option must be set after connect - bug in mysql client library
	my_bool reconn = 1;
	mysql_options(m_conn,MYSQL_OPT_RECONNECT,(const char*)&reconn);
#endif
#ifdef HAVE_MYSQL_SET_CHARSET
	if (m_encoding && mysql_set_character_set(m_conn,m_encoding))
	    Debug(&module,DebugWarn,"Failed to set encoding '%s' on connection '%s'",
		m_encoding.c_str(),m_name.c_str());
#else
	if (m_encoding)
	    Debug(&module,DebugWarn,"Your client library does not support setting the character set");
#endif
	return true;
    }
    Debug(&module,DebugWarn,"Connection for '%s' failed: %s",m_name.c_str(),mysql_error(m_conn));
    return false;
}

// drop the connection
void MyConn::dropDb()
{
    Lock lock(this);
    m_res = -1;
    m_go = false;
    if (!m_conn)
	return;
    MYSQL* tmp = m_conn;
    m_conn = 0;
    lock.drop();
    mysql_close(tmp);
    Debug(&module,DebugInfo,"Database connection '%s' closed",m_name.c_str());
}

// test it the connection is still OK
bool MyConn::testDb()
{
    return m_conn && !mysql_ping(m_conn);
}

// perform the query, fill the message with data
//  return number of rows, -1 for error
int MyConn::queryDbInternal()
{
    if (!testDb())
	return -1;

    if (mysql_real_query(m_conn,m_query.safe(),m_query.length())) {
	Debug(&module,DebugWarn,"Query for '%s' failed: %s",m_name.c_str(),mysql_error(m_conn));
	return -1;
    }

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
	    if (m_msg) {
		MYSQL_FIELD* fields = mysql_fetch_fields(res);
		m_msg->setParam("columns",String(cols));
		m_msg->setParam("rows",String(rows));
		if (m_msg->getBoolValue("results",true)) {
		    Array *a = new Array(cols,rows+1);
		    unsigned int c;
		    // add field names
		    for (c = 0; c < cols; c++)
			a->set(new String(fields[c].name),c,0);
		    // and now data row by row
		    for (unsigned int r = 1; r <= rows; r++) {
			MYSQL_ROW row = mysql_fetch_row(res);
			if (!row)
			    break;
			unsigned long* len = mysql_fetch_lengths(res);
			for (c = 0; c < cols; c++) {
			    if (!row[c])
				continue;
			    if (63 == fields[c].charsetnr) {
				// field holds binary data
				if (!len)
				    continue;
				a->set(new DataBlock(row[c],len[c]),c,r);
			    }
			    else
				a->set(new String(row[c]),c,r);
			}
		    }
		    m_msg->userData(a);
		    a->deref();
		}
	    }
	    mysql_free_result(res);
	}
    } while (!mysql_next_result(m_conn));

    if (m_msg) {
	m_msg->setParam("affected",String(affected));
	if (warns)
	    m_msg->setParam("warnings",String(warns));
    }
    return total;
}

void MyConn::runQueries()
{
    while (m_conn) {
	if (m_go) {
	    DDebug(&module,DebugAll,"Running query \"%s\" for '%s'",
		m_query.c_str(),m_name.c_str());
	    m_res = queryDbInternal();
	    m_msg = 0;
	    m_query.clear();
	    m_go = false;
	    Thread::yield(true);
	}
	else
	    Thread::msleep(10,true);
    }
}

static bool failure(Message* m)
{
    if (m)
	m->setParam("error","failure");
    return false;
}

int MyConn::queryDb(const char* query, Message* dest)
{
    if (TelEngine::null(query))
	return -1;
    DDebug(&module,DebugAll,"Proxying query \"%s\" for '%s'",
	query,m_name.c_str());
    m_msg = dest;
    m_query = query;
    m_go = true;
    while (m_go)
	Thread::msleep(1);
    if (m_res < 0)
	failure(dest);
    return m_res;
}


void DbThread::run()
{
    s_libMutex.lock();
    if (0 == s_libCounter++) {
	DDebug(&module,DebugAll,"Initializing the MySQL library");
	mysql_library_init(0,0,0);
    }
    s_libMutex.unlock();
    mysql_thread_init();
    if (m_conn->initDb()) {
	m_conn->m_thread = this;
	m_conn->runQueries();
    }
}

void DbThread::cleanup()
{
    Debug(&module,DebugNote,"Cleaning up connection %p",m_conn);
    if (m_conn) {
	m_conn->m_thread = 0;
	m_conn->dropDb();
    }
    mysql_thread_end();
    s_libMutex.lock();
    if (0 == --s_libCounter) {
	DDebug(&module,DebugAll,"Deinitializing the MySQL library");
	mysql_library_end();
    }
    s_libMutex.unlock();
}


static MyConn* findDb(String& account)
{
    if (account.null())
	return 0;
    ObjList* l = s_conns.find(account);
    return l ? static_cast<MyConn *>(l->get()): 0;
}


bool MyHandler::received(Message& msg)
{
    String tmp(msg.getValue("account"));
    if (tmp.null())
	return false;
    Lock lock(s_conmutex);
    MyConn* db = findDb(tmp);
    if (!(db && db->ok()))
	return false;
    Lock lo(db);
    lock.drop();
    String query(msg.getValue("query"));
    db->queryDb(query,&msg);
    msg.setParam("dbtype","mysqldb");
    return true;
}


MyModule::MyModule()
    : Module ("mysqldb","database",true),m_init(true)
{
    Output("Loaded module MySQL based on %s",mysql_get_client_info());
}

MyModule::~MyModule()
{
    Output("Unloading module MySQL");
    s_conns.clear();
}

void MyModule::statusParams(String& str)
{
    str.append("conns=",",") << s_conns.count();
}

void MyModule::initialize()
{
    Output("Initializing module MySQL");
    Module::initialize();
    Configuration cfg(Engine::configFile("mysqldb"));
    if (m_init)
	Engine::install(new MyHandler(cfg.getIntValue("general","priority",100)));
    m_init = false;
    for (unsigned int i = 0; i < cfg.sections(); i++) {
	NamedList* sec = cfg.getSection(i);
	if (!sec || (*sec == "general"))
	    continue;
	MyConn* conn = findDb(*sec);
	if (conn) {
	    if (!conn->ok()) {
		Debug(this,DebugNote,"Reinitializing connection '%s'",conn->toString().c_str());
		DbThread* thr = new DbThread(conn);
		if (!thr->startup())
		    delete thr;
	    }
	    continue;
	}
	conn = new MyConn(sec);
	DbThread* thr = new DbThread(conn);
	if (thr->startup())
	    s_conns.insert(conn);
	else {
	    delete thr;
	    TelEngine::destruct(conn);
	}
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
