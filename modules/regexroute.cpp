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
	
bool RouteHandler::received(Message &msg)
{
    unsigned long long tmr = Time::now();
    String s(msg.getValue("called"));
    if (s.null())
	return false;
    const char *context = msg.getValue("context","default");
    NamedList *l = s_cfg.getSection(context);
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = l->getParam(i);
	    if (n) {
		Regexp r(n->name());
		if (s.matches(r)) {
		    msg.retValue() = s.replaceMatches(*n);
		    Debug(DebugInfo,"Routing call to '%s' in context '%s' via `%s' by rule #%u '%s' in %llu usec",
			s.c_str(),context,msg.retValue().c_str(),i+1,r.c_str(),Time::now()-tmr);
		    return true;
		}
	    }
	}
    }
    Debug(DebugInfo,"Could not route call to '%s' in context '%s', wasted %llu usec",
	s.c_str(),context,Time::now()-tmr);
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
