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
    unsigned int n = s_cfg.sections();
    if (!s_cfg.getSection(0))
	--n;
    msg.retValue() << "name=regfile,type=misc;users=" << n << ";";
    bool first = true;
    for (unsigned int i=0;i<s_cfg.sections();i++) {
	NamedList *user = s_cfg.getSection(i);
	if (!user)
	    continue;
	if (first)
	    first = false;
	else
	    msg.retValue() << ",";
	msg.retValue() << *user << "=" << s_cfg.getBoolValue(*user,"register");
    }
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
	Engine::install(m_authhandler = new AuthHandler("user.auth",s_cfg.getIntValue("general","auth",10)));
    }
    if (!m_registhandler) {
    	Output("Installing Registering handler");
	Engine::install(m_registhandler = new RegistHandler("user.register"));
    }
    if (!m_unregisthandler) {
    	Output("Installing UnRegistering handler");
	Engine::install(m_unregisthandler = new UnRegistHandler("user.unregister"));
    }
    if (!m_routehandler) {
    	Output("Installing Route handler");
	Engine::install(m_routehandler = new RouteHandler("call.route",s_cfg.getIntValue("general","route",100)));
    }
    if (!m_statushandler) {
    	Output("Installing Status handler");
	Engine::install(m_statushandler = new StatusHandler("engine.status"));
    }
}

INIT_PLUGIN(RegfilePlugin);
/* vi: set ts=8 sw=4 sts=4 noet: */
