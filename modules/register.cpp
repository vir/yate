/**
 * register.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Ask for a registration from this module.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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

#include <telengine.h>

#include <stdio.h>
#include <libpq-fe.h>

using namespace TelEngine;

static PGconn *conn=0;
Mutex dbmutex;

static unsigned s_route_rq = 0;
static unsigned s_route_err = 0;
static unsigned s_route_yes = 0;
static unsigned s_route_no = 0;

class AuthHandler : public MessageHandler
{
public:
    AuthHandler(const char *name)
	: MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class RegistHandler : public MessageHandler
{
public:
    RegistHandler(const char *name)
	: MessageHandler(name),m_init(false) { }
    virtual bool received(Message &msg);
    bool init();
    bool m_init;
};

class UnRegistHandler : public MessageHandler
{
public:
    UnRegistHandler(const char *name)
	: MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class RouteHandler : public MessageHandler
{
public:
    RouteHandler(const char *name)
	: MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler(const char *name, unsigned prio = 1)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};
/**
 * I can't remeber why i have made this class :) 
class RegistThread :  public Thread
{
public:
    RegistThread();
    ~RegistThread();
    void run(void);
};
*/
class RegistPlugin : public Plugin
{
public:
    RegistPlugin();
    ~RegistPlugin();
    virtual void initialize();
private:
    AuthHandler *m_authhandler;
    RegistHandler *m_registhandler;
    UnRegistHandler *m_unregisthandler;
    RouteHandler *m_routehandler;
    StatusHandler *m_statushandler;
};

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
        Debug(DebugFail,"Failed to query from database: %s",
	    PQerrorMessage(conn));
    	return false;
    }
    if (PQntuples(respgsql) == 0) {
        Debug(DebugFail,"No user.");
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
        Debug(DebugFail,"Failed to clear the routepaid table: %s",
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
    String techno  = c_safe(msg.getValue("techno"));
    String data  = c_safe(msg.getValue("data"));
    
    Lock lock(dbmutex);
    if (!conn)
    	return false;

    String c = "SELECT credit,price,e164,context FROM register WHERE username='" + username + "'";
    PGresult *respgsql = PQexec(conn,(const char *)c);
    if (!respgsql || PQresultStatus(respgsql) != PGRES_TUPLES_OK)
    {
        Debug(DebugFail,"Failed to query from database: %s",
	    PQerrorMessage(conn));
    	return false;
    }
    if (PQntuples(respgsql) == 0) {
        Debug(DebugFail,"No credit.");
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
        Debug(DebugFail,"Failed to insert in database: %s",
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
        Debug(DebugFail,"Failed to query from database: %s",
	    PQerrorMessage(conn));
    	return false;
    }
    return true;
};

bool RouteHandler::received(Message &msg)
{
    unsigned long long tmr = Time::now();
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
    if (!respgsql || PQresultStatus(respgsql) != PGRES_TUPLES_OK)
    {
        Debug(DebugFail,"Failed to query from database: %s",
	    PQerrorMessage(conn));
	s_route_err++;
    	return false;
    }
    if (PQntuples(respgsql) == 0) {
        Debug(DebugFail,"No route.");
	s_route_no++;
    	return false;
    }
    msg.retValue() = String(PQgetvalue(respgsql,0,1));
    Debug(DebugInfo,"Routing call to '%s' in context '%s' using '%s' tehnology and data in %llu usec",
		called.c_str(),context.c_str(),msg.retValue().c_str(),Time::now()-tmr);
    s_route_yes++;
    return true;
};

bool StatusHandler::received(Message &msg)
{
    msg.retValue() << "Register,conn=" << (conn != 0) <<"\n";
    return false;
}

RegistPlugin::RegistPlugin()
    : m_authhandler(0),m_registhandler(0),m_routehandler(0),m_statushandler(0)
{
    Output("Loaded module Registration");
}

RegistPlugin::~RegistPlugin()
{
    if (conn) {
	PQfinish(conn);
	conn = 0;
    }
}

void RegistPlugin::initialize()
{
    char *pgoptions=NULL, *pgtty=NULL;
    Output("Initializing module Register for PostgreSQL");
    Configuration cfg(Engine::configFile("register"));
    const char *pghost = c_safe(cfg.getValue("general","host","localhost"));
    const char *pgport = c_safe(cfg.getValue("general","port","5432"));
    const char *dbName = c_safe(cfg.getValue("general","database","yate"));
    const char *dbUser = c_safe(cfg.getValue("general","user","postgres"));
    const char *dbPass = c_safe(cfg.getValue("general","password"));

    Lock lock(dbmutex);
    if (conn)
	PQfinish(conn);
    conn = PQsetdbLogin(pghost,pgport,pgoptions,pgtty,dbName,dbUser,dbPass);
    if (PQstatus(conn) == CONNECTION_BAD) {
	Debug(DebugFail, "Connection to database '%s' failed.", dbName);
	Debug(DebugFail, "%s", PQerrorMessage(conn));
	PQfinish(conn);
	conn = 0;
	return;
    }
    if (!m_registhandler) {
    	Output("Installing Registering handler");
	Engine::install(m_registhandler = new RegistHandler("regist"));
    }
    if (!m_unregisthandler) {
    	Output("Installing UnRegistering handler");
	Engine::install(m_unregisthandler = new UnRegistHandler("unregist"));
    }
    if (!m_authhandler) {
    	Output("Installing Authentification handler");
	Engine::install(m_authhandler = new AuthHandler("auth"));
    }
    if (!m_routehandler) {
    	Output("Installing Route handler");
	Engine::install(m_routehandler = new RouteHandler("route"));
    }
    if (!m_statushandler) {
    	Output("Installing Status handler");
	Engine::install(m_statushandler = new StatusHandler("status"));
    }
}

INIT_PLUGIN(RegistPlugin);
/* vi: set ts=8 sw=4 sts=4 noet: */
