/**
 * cdrpgsql.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Write the CDR to a PostgreSQL database
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

#include <yatengine.h>

#include <stdio.h>
#include <libpq-fe.h>

using namespace TelEngine;

static PGconn *conn=0;
Mutex dbmutex;

class CdrPgsqlHandler : public MessageHandler
{
public:
    CdrPgsqlHandler(const char *name)
	: MessageHandler(name) { }
    virtual bool received(Message &msg);
private:
};

bool CdrPgsqlHandler::received(Message &msg)
{
    String op(msg.getValue("operation"));
    if (op != "finalize")
	return false;

//    const char *calltime = c_safe(msg.getValue("time"));
    const char *channel  = c_safe(msg.getValue("channel"));
    const char *called   = c_safe(msg.getValue("called"));
    const char *caller   = c_safe(msg.getValue("caller"));
    const char *billtime = c_safe(msg.getValue("billtime"));
    const char *ringtime = c_safe(msg.getValue("ringtime"));
    const char *duration = c_safe(msg.getValue("duration"));
    const char *status   = c_safe(msg.getValue("status"));
    
    Lock lock(dbmutex);
    if (!conn)
    	return false;

    char buffer[2048];
    snprintf(buffer,sizeof(buffer),"INSERT INTO cdr"
	" (channel,caller,called,billtime,ringtime,duration,status)"
	" VALUES ('%s','%s','%s','%s','%s','%s','%s')",
	channel,caller,called,billtime,ringtime,duration,status);

    PGresult *respgsql = PQexec(conn,buffer);
    if (!respgsql || PQresultStatus(respgsql) != PGRES_COMMAND_OK)
        Debug(DebugFail,"Failed to insert in database: %s",
	    PQerrorMessage(conn));
    return false;
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
    msg.retValue() << "name=cdrpgsql,type=misc;conn=" << (conn != 0) <<"\n";
    return false;
}


class CdrPgsqlPlugin : public Plugin
{
public:
    CdrPgsqlPlugin();
    ~CdrPgsqlPlugin();
    virtual void initialize();
private:
    CdrPgsqlHandler *m_handler;
};

CdrPgsqlPlugin::CdrPgsqlPlugin()
    : m_handler(0)
{
    Output("Loaded module CdrFile");
}

CdrPgsqlPlugin::~CdrPgsqlPlugin()
{
    if (conn) {
	PQfinish(conn);
	conn = 0;
    }
}

void CdrPgsqlPlugin::initialize()
{
    char	*pgoptions=NULL,
    		*pgtty=NULL;
    Output("Initializing module Cdr for PostgreSQL");
    Configuration cfg(Engine::configFile("cdrpgsql"));
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
    if (!m_handler) {
    	Output("Installing Cdr for PostgreSQL handler");
	m_handler = new CdrPgsqlHandler("call.cdr");
	Engine::install(m_handler);
	Engine::install(new StatusHandler("engine.status"));
    }
}

INIT_PLUGIN(CdrPgsqlPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
