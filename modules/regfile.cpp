/**
 * regfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Ask for a registration from this module.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

using namespace TelEngine;

Mutex lmutex;

static Configuration s_cfg(Engine::configFile("regfile"));

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
    StatusHandler(const char *name, unsigned prio = 100)
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
};

bool AuthHandler::received(Message &msg)
{
    String username(msg.getValue("username"));
    const NamedList* sect = s_cfg.getSection(username);
    if (sect) {
	msg.retValue() = sect->getValue("password");
	return true;
    }
    return false;
};


bool RegistHandler::received(Message &msg)
{
    const char *username = msg.getValue("username");
    const char *driver   = msg.getValue("driver");
    const char *data     = msg.getValue("data");
    if (!(username && data))
	return false;

    Lock lock(lmutex);
    if (s_cfg.getSection(username)) {
	Debug("RegFile",DebugInfo,"Registered '%s' via '%s'",username,data);
        s_cfg.setValue(username,"driver",driver);
        s_cfg.setValue(username,"data",data);
	return true;
    }
    return false;
};

bool UnRegistHandler::received(Message &msg)
{
    const char *username = msg.getValue("username");
    if (!username)
	return false;
    
    Lock lock(lmutex);
    if (s_cfg.getSection(username)) {
	Debug("RegFile",DebugInfo,"Unregistered '%s'",username);
	s_cfg.clearKey(username,"data");
	return true;
    }
    return false;
};

bool RouteHandler::received(Message &msg)
{
    const char* username = msg.getValue("called");
    if (!username)
	return false;
    
    Lock lock(lmutex);
    const char* data = s_cfg.getValue(username,"data");
    if (data) {
	Debug("RegFile",DebugInfo,"Routed '%s' via '%s'",username,data);
	msg.retValue() = data;
	return true;
    }
    return false;
};

bool StatusHandler::received(Message &msg)
{
    Lock lock(lmutex);
    unsigned int n = s_cfg.sections();
    if (!s_cfg.getSection(0))
	--n;
    msg.retValue() << "name=regfile,type=misc;users=" << n << ";";
    bool first = true;
    for (unsigned int i=0;i<s_cfg.sections();i++) {
	NamedList *user = s_cfg.getSection(i);
	if (!user)
	    continue;
	const char* data = s_cfg.getValue(*user,"data");
	if (first)
	    first = false;
	else
	    msg.retValue() << ",";
	msg.retValue() << *user << "=" << (data != 0);
    }
    msg.retValue() <<"\n";
    return false;
}

RegfilePlugin::RegfilePlugin()
    : m_authhandler(0)
{
    Output("Loaded module Registration from file");
}

void RegfilePlugin::initialize()
{
    Output("Initializing module Register for file");
    if (!m_authhandler) {
	s_cfg.load();
    	Output("Installing handlers");
	Engine::install(m_authhandler = new AuthHandler("user.auth",s_cfg.getIntValue("general","auth",100)));
	Engine::install(new RegistHandler("user.register"));
	Engine::install(new UnRegistHandler("user.unregister"));
	Engine::install(new RouteHandler("call.route",s_cfg.getIntValue("general","route",100)));
	Engine::install(new StatusHandler("engine.status"));
    }
}

INIT_PLUGIN(RegfilePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
