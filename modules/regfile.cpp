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

Mutex lmutex;

static Configuration s_cfg(Engine::configFile("regfile"));

ObjList registered;

class AuthHandler : public MessageHandler
{
public:
    AuthHandler(const char *name,int prio)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};
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
    RouteHandler(const char *name, int prio)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler(const char *name, unsigned prio = 1)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

class RegfilePlugin : public Plugin
{
public:
    RegfilePlugin();
    ~RegfilePlugin(){}
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
    String username(msg.getValue("username"));
    msg.retValue() = s_cfg.getValue(username,"password");
    Debug(DebugInfo,"i'm in %i",!msg.retValue().null());
    return (!msg.retValue().null());
};


bool RegistHandler::received(Message &msg)
{
    const char *username  = c_safe(msg.getValue("username"));
    const char *techno  = c_safe(msg.getValue("techno"));
    const char *data  = c_safe(msg.getValue("data"));
    
    
    Lock lock(lmutex);
    if (s_cfg.getSection(username)){
        s_cfg.setValue(username,"register",true);
        s_cfg.setValue(username,"techno",techno);
        s_cfg.setValue(username,"data",data);
    } else
	return false;
    return true;
    
};

bool UnRegistHandler::received(Message &msg)
{
    const char *username  = c_safe(msg.getValue("username"));
    
    Lock lock(lmutex);
    if (s_cfg.getSection(username))
	s_cfg.setValue(username,"register",false);
    else
	return false;
    return true;
};

bool RouteHandler::received(Message &msg)
{
    const char *username  = c_safe(msg.getValue("username"));
    
    Lock lock(lmutex);
    if (s_cfg.getSection(username))
	msg.retValue() = s_cfg.getValue(username,"data");
    else
	return false;
    return true;
};

bool StatusHandler::received(Message &msg)
{
    msg.retValue() << "Regfile,users=";
    for (int i=0;i<s_cfg.count(),i++)
	msg.retValue() << s_cfg.getSection();
    msg.retValue() <<"\n";
    return false;
}

RegfilePlugin::RegfilePlugin()
    : m_authhandler(0),m_registhandler(0),m_routehandler(0),m_statushandler(0)
{
    Output("Loaded module Registration from file");
}

void RegfilePlugin::initialize()
{
    Output("Initializing module Register for file");
    if (!m_authhandler) {
	s_cfg.load();
    	Output("Installing Authentification handler");
	Engine::install(m_authhandler = new AuthHandler("auth",s_cfg.getIntValue("general","auth",10)));
    }
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
	Engine::install(new RouteHandler("route",s_cfg.getIntValue("general","route",100)));
    }
    if (!m_statushandler) {
    	Output("Installing Status handler");
	Engine::install(new StatusHandler("status"));
    }
}

INIT_PLUGIN(RegfilePlugin);
/* vi: set ts=8 sw=4 sts=4 noet: */
