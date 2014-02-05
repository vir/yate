/**
 * lateroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Last chance routing in call.execute and msg.execute messages.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <yatengine.h>

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
    LateHandler(const char* name, unsigned priority, const char* routeType);
    virtual bool received(Message& msg);
protected:
    String m_routeType;
};

static Regexp s_regexp;
static String s_called;
static Mutex s_mutex(false,"LateRoute");
static LateHandler* s_callHandler = 0;
static LateHandler* s_msgHandler = 0;
static const String s_routeTypeParam = "route_type";

INIT_PLUGIN(LateRouter);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow) {
	if (!s_mutex.lock(500000))
	    return false;
	TelEngine::destruct(s_callHandler);
	TelEngine::destruct(s_msgHandler);
	s_mutex.unlock();
    }
    return true;
}


LateHandler::LateHandler(const char* name, unsigned priority, const char* routeType)
    : MessageHandler(name,priority,__plugin.name()),
    m_routeType(routeType)
{
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
    lock.drop();
    msg.replaceParams(dest);
    if (dest.trimBlanks().null())
	return false;

    String called = msg.getValue("called");
    msg.clearParam("callto");
    msg.setParam("called",dest);
    // change the message name to the routing one
    msg = "call.route";
    if (!msg.getParam(s_routeTypeParam))
	msg.addParam(s_routeTypeParam,m_routeType);
    bool ok = Engine::dispatch(msg);
    dest = msg.retValue();
    msg.retValue().clear();
    // and restore it back to this handler's name
    msg = toString();
    ok = ok && dest && (dest != "-") && (dest != "error");
    if (ok && (dest == callto)) {
	Debug(DebugMild,"%s to '%s' late routed back to itself!",
	   msg.getValue(s_routeTypeParam,m_routeType),callto.c_str());
	ok = false;
    }
    if (!ok) {
	// restore most of what we changed and let it pass through
	msg.setParam("called",called);
	msg.setParam("callto",callto);
	return false;
    }
    Debug(DebugInfo,"Late routing %s to '%s' via '%s'",
	msg.getValue(s_routeTypeParam,m_routeType),callto.c_str(),dest.c_str());
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
    static Regexp listCheck("^[[:alnum:]_,-]*$");

    Output("Initializing module Late Router");
    Configuration cfg(Engine::configFile("lateroute"));
    // "regexp" and "called" should be used for backwards compatibility only
    String types = cfg.getValue("general","regexp");
    String called = cfg.getValue("general","called","\\0");
    if (types.null()) {
	types = cfg.getValue("general","types","lateroute,route,pstn,voice");
	if (types && listCheck.matches(types)) {
	    // this is a list,of,types,to,route - convert it to regexp
	    ObjList* list = types.split(',',false);
	    types.clear();
	    types.append(list,"\\|",false);
	    TelEngine::destruct(list);
	    if (types) {
		// regexp is ^\(type1\|type2\|...\)/\(.\+\)$
		types = "^\\(" + types + "\\)/\\(.\\+\\)$";
		// called is the matched part after the slash
		called = "\\2";
	    }
	    else
		called.clear();
	}
	else
	    Debug("lateroute",DebugNote,"Using regexp: '%s'",types.c_str());
    }
    s_mutex.lock();
    s_regexp = types;
    s_called = called;
    s_mutex.unlock();
    DDebug("lateroute",DebugInfo,"regexp='%s' called='%s'",s_regexp.c_str(),s_called.c_str());
    if (!s_callHandler && cfg.getBoolValue("general","enabled",(s_regexp && s_called))) {
	int priority = cfg.getIntValue("general","priority",75);
	s_callHandler = new LateHandler("call.execute",priority,
	    cfg.getValue("route type","call.execute","call"));
	s_msgHandler = new LateHandler("msg.execute",priority,
	    cfg.getValue("route type","msg.execute","msg"));
	Engine::install(s_callHandler);
	Engine::install(s_msgHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
