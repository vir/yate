/**
 * pgsqlroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Postgres SQL based routing
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
#include <telephony.h>

#include <libpq-fe.h>
#include <string.h>

using namespace TelEngine;

static PGconn *conn=0;
static Mutex dbmutex;

static unsigned s_route_rq = 0;
static unsigned s_route_err = 0;
static unsigned s_route_yes = 0;
static unsigned s_route_no = 0;

class RouteHandler : public MessageHandler
{
public:
    RouteHandler(const char *name, unsigned prio = 1)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};
	
bool RouteHandler::received(Message &msg)
{
    char buffer[2048];
    unsigned long long tmr = Time::now();
    String called(msg.getValue("called"));
    if (called.null())
	return false;
    Lock lock(dbmutex);
    if (!conn)
    	return false;
    s_route_rq++;
    const char *context = c_safe(msg.getValue("context","default"));
    snprintf(buffer,sizeof(buffer),"SELECT tehno,data,length (prefix) as lll"
	" from route where prefix= substring('%s',1,length(prefix))"
	" and context='%s' order by lll desc LIMIT 1",called.c_str(),context);
    PGresult *respgsql = PQexec(conn,buffer);
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
    msg.retValue() = String(PQgetvalue(respgsql,0,0))+"/" + String(PQgetvalue(respgsql,0,1));
    Debug(DebugInfo,"Routing call to '%s' in context '%s' using '%s' tehnology and data in %llu usec",
		called.c_str(),context,msg.retValue().c_str(),Time::now()-tmr);
    s_route_yes++;
    return true;
};
		    
class PrerouteHandler : public MessageHandler
{
public:
    PrerouteHandler(const char *name, unsigned prio = 1)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};
	
bool PrerouteHandler::received(Message &msg)
{
    char buffer[2048];
//    char select_called[200];
//    char select_channel[200];
    unsigned long long tmr = Time::now();
    // return immediately if there is already a context
    if (msg.getValue("context"))
	return false;
    String caller(msg.getValue("caller"));
    if (caller.null())
	return false;
    Lock lock(dbmutex);
    if (!conn)
    	return false;
    String called(msg.getValue("called"));
    if (!caller.null())
//    snprintf(select_called,sizeof(select_called),"and called='%s'",called.c_str());
    snprintf(buffer,sizeof(buffer),"SELECT context,length (caller) as lll from preroute where caller= substring('%s',1,length(caller)) order by lll desc limit 1;",caller.c_str());
    PGresult *respgsql = PQexec(conn,buffer);
    if (!respgsql || PQresultStatus(respgsql) != PGRES_TUPLES_OK)
    {
        Debug(DebugFail,"Failed to query from database: %s",
	    PQerrorMessage(conn));
    	return false;
    }
    if (PQntuples(respgsql) == 0) {
        Debug(DebugFail,"No preroute.");
    	return false;
    }
    msg.addParam("context",PQgetvalue(respgsql,0,0));
    Debug(DebugInfo,"Classifying caller '%s' in context '%s' in %llu usec",
	caller.c_str(),msg.getValue("context"),Time::now()-tmr);
    return true;
    
#if 0 
    NamedList *l = s_cfg.getSection("contexts");
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = l->getParam(i);
	    if (n) {
		Regexp r(n->name());
		if (s.matches(r)) {
		    msg.addParam("context",s.replaceMatches(*n));
		    Debug(DebugInfo,"Classifying caller '%s' in context '%s' by rule #%u '%s' in %llu usec",
			s.c_str(),msg.getValue("context"),i+1,r.c_str(),Time::now()-tmr);
		    return true;
		}
	    }
	}
    }
    Debug(DebugInfo,"Could not classify call from '%s', wasted %llu usec",
	s.c_str(),Time::now()-tmr);
    return false;
#endif
};
		    
class StatusHandler : public MessageHandler
{
public:
    StatusHandler(const char *name, unsigned prio = 1)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"pgsqlroute"))
	return false;

    msg.retValue() << "PgSQLroute,conn=" << (conn != 0);
    msg.retValue() << ",total=" << s_route_rq << ",errors=" << s_route_err;
    msg.retValue() << ",routed=" << s_route_yes << ",noroute=" << s_route_no;
    msg.retValue() << "\n";
    return false;
}



class PGSQLRoutePlugin : public Plugin
{
public:
    PGSQLRoutePlugin();
    ~PGSQLRoutePlugin();
    virtual void initialize();
private:
    bool m_first;
};

PGSQLRoutePlugin::PGSQLRoutePlugin()
    : m_first(true)
{
    Output("Loaded module PGSQLRoute");
}

PGSQLRoutePlugin::~PGSQLRoutePlugin()
{
    if (conn) {
	PQfinish(conn);
	conn = 0;
    }
}

void PGSQLRoutePlugin::initialize()
{
    char       *pgoptions=NULL,
               *pgtty=NULL;
    
    Output("Initializing module PGSQLRoute");
    Configuration cfg(Engine::configFile("pgsqlroute"));
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
    // don't bother to install handlers until we are connected
    if (m_first && conn) {
	m_first = false;
	unsigned prio = cfg.getIntValue("general","priority",100);
	Engine::install(new PrerouteHandler("preroute",prio));
	Engine::install(new RouteHandler("route",prio));
	Engine::install(new StatusHandler("status"));
    }
}

INIT_PLUGIN(PGSQLRoutePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
