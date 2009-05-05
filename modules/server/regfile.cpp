/**
 * regfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Ask for a registration from this module.
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

#include <yatengine.h>

using namespace TelEngine;
namespace { // anonymous

Mutex lmutex(false,"RegFile");

static Configuration s_cfg(Engine::configFile("regfile"));

static const String s_general = "general";
static bool s_create = false;


class AuthHandler : public MessageHandler
{
public:
    AuthHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

class RegistHandler : public MessageHandler
{
public:
    RegistHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

class UnRegistHandler : public MessageHandler
{
public:
    UnRegistHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

class RouteHandler : public MessageHandler
{
public:
    RouteHandler(const char *name, unsigned prio = 100)
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
    if (username.null() || username == s_general)
	return false;
    const NamedList* sect = s_cfg.getSection(username);
    if (sect) {
	const String* pass = sect->getParam("password");
	if (!pass)
	    return false;
	msg.retValue() = *pass;
	return true;
    }
    return false;
};


bool RegistHandler::received(Message &msg)
{
    String username(msg.getValue("username"));
    if (username.null() || username == s_general)
	return false;

    const char *driver = msg.getValue("driver");
    const char *data   = msg.getValue("data");
    if (!data)
	return false;

    Lock lock(lmutex);
    NamedList* sect = s_cfg.getSection(username);
    if (s_create && !sect) {
	Debug("RegFile",DebugNote,"Auto creating new user '%s'",username.c_str());
	s_cfg.createSection(username);
	sect = s_cfg.getSection(username);
    }
    if (sect) {
	Debug("RegFile",DebugInfo,"Registered '%s' via '%s'",username.c_str(),data);
        sect->setParam("driver",driver);
        sect->setParam("data",data);
	return true;
    }
    return false;
};

bool UnRegistHandler::received(Message &msg)
{
    String username(msg.getValue("username"));
    if (username.null() || username == s_general)
	return false;
    
    Lock lock(lmutex);
    if (s_cfg.getSection(username)) {
	Debug("RegFile",DebugInfo,"Unregistered '%s'",username.c_str());
	s_cfg.clearKey(username,"data");
	return true;
    }
    return false;
};

bool RouteHandler::received(Message &msg)
{
    String username(msg.getValue("called"));
    if (username.null() || username == s_general)
	return false;
    
    Lock lock(lmutex);
    const NamedList* sect = s_cfg.getSection(username);
    if (sect) {
	const char* data = sect->getValue("data");
	if (data) {
	    Debug("RegFile",DebugInfo,"Routed '%s' via '%s'",username.c_str(),data);
	    msg.retValue() = data;
	    msg.setParam("driver",sect->getValue("driver"));
	    return true;
	}
	// signal to other modules we know about this user but it's offline
	msg.setParam("error","offline");
    }
    return false;
};

bool StatusHandler::received(Message &msg)
{
    String dest(msg.getValue("module"));
    if (dest && (dest != "regfile") && (dest != "misc"))
        return false;
    Lock lock(lmutex);
    unsigned int n = s_cfg.sections();
    if (!s_cfg.getSection(0))
	--n;
    msg.retValue() << "name=regfile,type=misc;users=" << n << ",create=" << s_create;
    if (msg.getBoolValue("details",true)) {
    msg.retValue() << ";";
	bool first = true;
	for (unsigned int i=0;i<s_cfg.sections();i++) {
	    NamedList *user = s_cfg.getSection(i);
	    if (!user || (*user == s_general))
		continue;
	    const char* data = s_cfg.getValue(*user,"data");
	    if (first)
		first = false;
	    else
		msg.retValue() << ",";
	    msg.retValue() << *user << "=" << (data ? data : "offline");
	}
    }
    msg.retValue() << "\r\n";
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
	s_create = s_cfg.getBoolValue("general","autocreate");
	Engine::install(m_authhandler = new AuthHandler("user.auth",s_cfg.getIntValue("general","auth",100)));
	Engine::install(new RegistHandler("user.register",s_cfg.getIntValue("general","register",100)));
	Engine::install(new UnRegistHandler("user.unregister",s_cfg.getIntValue("general","register",100)));
	Engine::install(new RouteHandler("call.route",s_cfg.getIntValue("general","route",100)));
	Engine::install(new StatusHandler("engine.status"));
    }
}

INIT_PLUGIN(RegfilePlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
