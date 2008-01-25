/**
 * lateroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Last chance routing in call.execute messages.
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

using namespace TelEngine;
namespace { // anonymous

class LateRouter : public Plugin
{
public:
    LateRouter();
    ~LateRouter();
    virtual void initialize();
};

class LateHandler : public MessageHandler
{
public:
    inline LateHandler(unsigned priority)
	: MessageHandler("call.execute",priority)
	{ }
    virtual bool received(Message& msg);
};

static Regexp s_regexp;
static String s_called;
static Mutex s_mutex;
static LateHandler* s_handler = 0;

INIT_PLUGIN(LateRouter);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow) {
	if (!s_mutex.lock(500000))
	    return false;
	TelEngine::destruct(s_handler);
	s_mutex.unlock();
    }
    return true;
}

bool LateHandler::received(Message& msg)
{
    String dest = msg.getValue("callto");
    if (dest.null() || !msg.getBoolValue("lateroute",true))
	return false;
    Lock lock(s_mutex);
    if (s_called.null() || !dest.matches(s_regexp))
	return false;
    String callto = dest;
    dest = dest.replaceMatches(s_called);
    msg.replaceParams(dest);
    if (dest.trimBlanks().null())
	return false;

    String called = msg.getValue("called");
    msg.clearParam("callto");
    msg.setParam("called",dest);
    msg.setParam("lateroute",String::boolText(false));
    msg = "call.route";
    bool ok = Engine::dispatch(msg);
    dest = msg.retValue();
    msg.retValue().clear();
    msg = "call.execute";
    ok = ok && dest && (dest != "-") && (dest != "error");
    if (ok && (dest == callto)) {
	Debug(DebugMild,"Call to '%s' late routed back to itself!",callto.c_str());
	ok = false;
    }
    if (!ok) {
	// restore most of what we changed and let it pass through
	msg.setParam("called",called);
	msg.setParam("callto",callto);
	msg.clearParam("lateroute");
	return false;
    }
    Debug(DebugInfo,"Late routing call to '%s' via '%s'",callto.c_str(),dest.c_str());
    // let it pass through to the new target
    msg.setParam("callto",dest);
    return false;
}


LateRouter::LateRouter()
    : Plugin("lateroute")
{
    Output("Loaded module Late Router");
}

LateRouter::~LateRouter()
{
    Output("Unloading module Late Router");
}

void LateRouter::initialize()
{
    Output("Initializing module Late Router");
    Configuration cfg(Engine::configFile("lateroute"));
    s_mutex.lock();
    s_regexp = cfg.getValue("general","regexp");
    s_called = cfg.getValue("general","called","\\0");
    s_mutex.unlock();
    if (!s_handler && cfg.getBoolValue("general","enabled",(s_regexp && s_called))) {
	s_handler = new LateHandler(cfg.getIntValue("general","priority",75));
	Engine::install(s_handler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
