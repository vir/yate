/**
 * regexroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Regular expressions based routing
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

#include <string.h>

using namespace TelEngine;

static Configuration s_cfg;

class RouteHandler : public MessageHandler
{
public:
    RouteHandler(int prio)
	: MessageHandler("route",prio) { }
    virtual bool received(Message &msg);
};

static void setMessage(Message &msg, String &line)
{
    ObjList *strs = line.split(';');
    bool first = true;
    for (ObjList *p = strs; p; p=p->next()) {
	String *s = static_cast<String*>(p->get());
	if (first) {
	    first = false;
	    line = s ? *s : "";
	    continue;
	}
	if (s && !s->trimBlanks().null()) {
	    int q = s->find('=');
	    if (q > 0) {
		String n = s->substr(0,q);
		String v = s->substr(q+1);
		n.trimBlanks();
		v.trimBlanks();
		DDebug("RegexRoute",DebugAll,"Setting '%s' to '%s'",n.c_str(),v.c_str());
		msg.setParam(n,v);
	    }
	    else
		Debug("RegexRoute",DebugWarn,"Invalid setting '%s'",s->c_str());
	}
    }
    strs->destruct();
}

static bool oneContext(Message &msg, String &called, const String &context, int depth = 0)
{
    if (!(context && *context))
	return false;
    if (depth > 5) {
	Debug("RegexRoute",DebugWarn,"Loop detected, current context '%s'",context.c_str());
	return false;
    }
    NamedList *l = s_cfg.getSection(context);
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = l->getParam(i);
	    if (n) {
		Regexp r(n->name());
		if (called.matches(r)) {
		    String val = called.replaceMatches(*n);
		    setMessage(msg,val);
		    val.trimBlanks();
		    switch (val[0]) {
			case 0:
			    break;
			case '-':
			    return false;
			case '>':
			    val >> ">";
			    val.trimBlanks();
			    NDebug("RegexRoute",DebugAll,"Jumping to context '%s' by rule #%u '%s'",
				val.c_str(),i+1,r.c_str());
			    return oneContext(msg,called,val,depth+1);
			case '<':
			    val >> "<";
			    val.trimBlanks();
			    NDebug("RegexRoute",DebugAll,"Calling context '%s' by rule #%u '%s'",
				val.c_str(),i+1,r.c_str());
			    if (oneContext(msg,called,val,depth+1)) {
				DDebug("RegexRoute",DebugAll,"Returning true from context '%s'", context.c_str());
				return true;
			    }
			    break;
			case '!':
			    val >> "!";
			    val.trimBlanks();
			    if (!val.null()) {
				NDebug("RegexRoute",DebugAll,"Setting called '%s' by rule #%u '%s'",
				    val.c_str(),i+1,r.c_str());
				called = val;
			    }
			    break;
			default:
			    DDebug("RegexRoute",DebugAll,"Routing call to '%s' in context '%s' via `%s' by rule #%u '%s'",
				called.c_str(),context.c_str(),val.c_str(),i+1,r.c_str());
			    msg.retValue() = val;
			    return true;
		    }
		}
	    }
	}
    }
    DDebug("RegexRoute",DebugAll,"Returning false from context '%s'", context.c_str());
    return false;
}
	
bool RouteHandler::received(Message &msg)
{
    unsigned long long tmr = Time::now();
    String called(msg.getValue("called"));
    if (called.null())
	return false;
    const char *context = msg.getValue("context","default");
    if (oneContext(msg,called,context)) {
	Debug(DebugInfo,"Routing call to '%s' in context '%s' via `%s' in %llu usec",
	    called.c_str(),context,msg.retValue().c_str(),Time::now()-tmr);
	return true;
    }
    Debug(DebugInfo,"Could not route call to '%s' in context '%s', wasted %llu usec",
	called.c_str(),context,Time::now()-tmr);
    return false;
};
		    
class PrerouteHandler : public MessageHandler
{
public:
    PrerouteHandler(int prio)
	: MessageHandler("preroute",prio) { }
    virtual bool received(Message &msg);
};
	
bool PrerouteHandler::received(Message &msg)
{
    unsigned long long tmr = Time::now();
    // return immediately if there is already a context
    if (msg.getValue("context"))
	return false;
 //   String s(msg.getValue("caller"));
    String s(msg.getValue("driver")); s+="/";
    s+=msg.getValue("span"); s+="/";
    s+=msg.getValue("channel"); s+="/";
    s+=msg.getValue("caller");
			
    if (s.null())
	return false;
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
};
		    

class RegexRoutePlugin : public Plugin
{
public:
    RegexRoutePlugin();
    virtual void initialize();
private:
    MessageHandler *m_preroute, *m_route;
};

RegexRoutePlugin::RegexRoutePlugin()
    : m_preroute(0), m_route(0)
{
    Output("Loaded module RegexRoute");
}

void RegexRoutePlugin::initialize()
{
    Output("Initializing module RegexRoute");
    s_cfg = Engine::configFile("regexroute");
    s_cfg.load();
    if (m_preroute) {
	delete m_preroute;
	m_preroute = 0;
    }
    if (m_route) {
	delete m_route;
	m_route = 0;
    }
    unsigned priority = s_cfg.getIntValue("priorities","preroute",100);
    if (priority) {
	m_preroute = new PrerouteHandler(priority);
	Engine::install(m_preroute);
    }
    priority = s_cfg.getIntValue("priorities","route",100);
    if (priority) {
	m_route = new RouteHandler(priority);
	Engine::install(m_route);
    }
}

INIT_PLUGIN(RegexRoutePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
