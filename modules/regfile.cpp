/**
 * regfile.cpp
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

using namespace TelEngine;

static Configuration s_cfg(Engine::configFile("regfile"));

class AuthHandler : public MessageHandler
{
public:
    AuthHandler(const char *name)
	: MessageHandler(name) { }
    virtual bool received(Message &msg);
};
/*
class RegistHandler : public MessageHandler
{
public:
    RegistHandler(const char *name)
	: MessageHandler(name) { }
    virtual bool received(Message &msg);
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
*/

class RegfilePlugin : public Plugin
{
public:
    RegfilePlugin();
    ~RegfilePlugin(){}
    virtual void initialize();
private:
    AuthHandler *m_authhandler;
    /*RegistHandler *m_registhandler;
    UnRegistHandler *m_unregisthandler;
    RouteHandler *m_routehandler;
    StatusHandler *m_statushandler;*/
};

bool AuthHandler::received(Message &msg)
{
    String username(msg.getValue("username"));
    msg.retValue() = s_cfg.getValue(username,"password");
    return (!msg.retValue().null());
};

#if 0

bool RegistHandler::received(Message &msg)
{
    const char *username  = c_safe(msg.getValue("username"));
    const char *techno  = c_safe(msg.getValue("techno"));
    const char *data  = c_safe(msg.getValue("data"));
    
    
    Lock lock(dbmutex);
    if (!conn)
    	return false;
    
    char buffer[2048];
    snprintf(buffer,sizeof(buffer),"SELECT credit,price,e164,context FROM register WHERE username='%s'",username);
    PGresult *respgsql = PQexec(conn,buffer);
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
    
    const char *credit  = PQgetvalue(respgsql,0,0);
    const char *price  = PQgetvalue(respgsql,0,1);
    const char *prefix = PQgetvalue(respgsql,0,2);
    const char *context = PQgetvalue(respgsql,0,3);

    snprintf(buffer,sizeof(buffer),"INSERT INTO routepaid (context,prefix,tehno,data,price) VALUES ('%s','%s','%s','%s',%s);",context,prefix,techno,data,price);

    PGresult *respgsql1 = PQexec(conn,buffer);
    if (!respgsql1 || PQresultStatus(respgsql1) != PGRES_COMMAND_OK)
        Debug(DebugFail,"Failed to insert in database: %s",
	    PQerrorMessage(conn));
    msg.retValue() = prefix;
    Debug(DebugInfo,"prefix in register este %s",prefix);
    return true;
    
};

bool UnRegistHandler::received(Message &msg)
{
    const char *prefix  = c_safe(msg.getValue("prefix"));
    Debug(DebugInfo,"prefix=%s",prefix);
    
    Lock lock(dbmutex);
    if (!conn)
    	return false;

    char buffer[2048];
    snprintf(buffer,sizeof(buffer),"DELETE from routepaid WHERE prefix='%s'",prefix);
    PGresult *respgsql = PQexec(conn,buffer);
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
    return true;
    
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
    snprintf(buffer,sizeof(buffer),"SELECT tehno,data,length (prefix) as lll,price"
	" from routepaid where prefix= substring('%s',1,length(prefix))"
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

bool StatusHandler::received(Message &msg)
{
    msg.retValue() << "Regfile,conn=" << (conn != 0) <<"\n";
    return false;
}

#endif
RegfilePlugin::RegfilePlugin()
    : m_authhandler(0)//,m_registhandler(0),m_routehandler(0),m_statushandler(0)
{
    Output("Loaded module Registration from file");
}

void RegfilePlugin::initialize()
{
    Output("Initializing module Register for file");
    s_cfg.load();
    if (!m_authhandler) {
    	Output("Installing Authentification handler");
	Engine::install(m_authhandler = new AuthHandler("auth"));
    }
    /*
    if (!m_registhandler) {
    	Output("Installing Registering handler");
	Engine::install(new RegistHandler("regist"));
    }
    if (!m_unregisthandler) {
    	Output("Installing UnRegistering handler");
	Engine::install(new UnRegistHandler("unregist"));
    }
    if (!m_routehandler) {
    	Output("Installing Route handler");
	Engine::install(new RouteHandler("route"));
    }
    if (!m_statushandler) {
    	Output("Installing Status handler");
	Engine::install(new StatusHandler("status"));
    }*/
}

INIT_PLUGIN(RegfilePlugin);
/* vi: set ts=8 sw=4 sts=4 noet: */
