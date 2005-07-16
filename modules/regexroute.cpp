/**
 * regexroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Regular expressions based routing
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

#include <string.h>

using namespace TelEngine;

static Configuration s_cfg;
static bool s_extended;
static bool s_insensitive;
static Mutex s_mutex;
static ObjList s_extra;

class RouteHandler : public MessageHandler
{
public:
    RouteHandler(int prio)
	: MessageHandler("call.route",prio) { }
    virtual bool received(Message &msg);
};

// handle ${paramname} replacements
static void replaceParams(const Message &msg, String &str)
{
    int p1;
    while ((p1 = str.find("${")) >= 0) {
	int p2 = str.find('}',p1+2);
	if (p2 > 0) {
	    String v = str.substr(p1+2,p2-p1-2);
	    v.trimBlanks();
	    DDebug("RegexRoute",DebugAll,"Replacing parameter '%s'",
		v.c_str());
	    str = str.substr(0,p1) + msg.getValue(v) + str.substr(p2+1);
	}
    }
}

// handle ;paramname[=value] assignments
static void setMessage(Message &msg, String &line)
{
    ObjList *strs = line.split(';');
    bool first = true;
    for (ObjList *p = strs; p; p=p->next()) {
	String *s = static_cast<String*>(p->get());
	if (s)
	    replaceParams(msg,*s);
	if (first) {
	    first = false;
	    line = s ? *s : String::empty();
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
	    else {
		DDebug("RegexRoute",DebugAll,"Clearing parameter '%s'",s->c_str());
		msg.clearParam(s);
	    }
	}
    }
    strs->destruct();
}

// process one context, can call itself recursively
static bool oneContext(Message &msg, String &str, const String &context, String &ret, int depth = 0)
{
    if (context.null())
	return false;
    if (depth > 5) {
	Debug("RegexRoute",DebugWarn,"Possible loop detected, current context '%s'",context.c_str());
	return false;
    }
    NamedList *l = s_cfg.getSection(context);
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = l->getParam(i);
	    if (n) {
		Regexp r(n->name(),s_extended,s_insensitive);
		String val;
		if (r.startsWith("${")) {
		    // handle special matching by param ${paramname}regexp
		    int p = r.find('}');
		    if (p < 3) {
			Debug("RegexRoute",DebugWarn,"Invalid parameter match '%s' in rule #%u in context '%s'",
			    r.c_str(),i+1,context.c_str());
			continue;
		    }
		    val = r.substr(2,p-2);
		    r = r.substr(p+1);
		    val.trimBlanks();
		    r.trimBlanks();
		    if (val.null() || r.null()) {
			Debug("RegexRoute",DebugWarn,"Missing parameter or rule in rule #%u in context '%s'",
			    i+1,context.c_str());
			continue;
		    }
		    DDebug("RegexRoute",DebugAll,"Using message parameter '%s'",
			val.c_str());
		    val = msg.getValue(val);
		}
		else
		    val = str;
		val.trimBlanks();

		if (val.matches(r)) {
		    val = val.replaceMatches(*n);
		    if (val.startSkip("echo") || val.startSkip("output")) {
			// special case: display the line but don't set params
			replaceParams(msg,val);
			Output("%s",val.safe());
			continue;
		    }
		    setMessage(msg,val);
		    val.trimBlanks();
		    if (val.null()) {
			// special case: do nothing on empty target
			continue;
		    }
		    else if (val == "return") {
			NDebug("RegexRoute",DebugAll,"Returning false from context '%s'", context.c_str());
			return false;
		    }
		    else if (val.startSkip("goto") || val.startSkip("jump")) {
			NDebug("RegexRoute",DebugAll,"Jumping to context '%s' by rule #%u '%s'",
			    val.c_str(),i+1,n->name().c_str());
			return oneContext(msg,str,val,ret,depth+1);
		    }
		    else if (val.startSkip("include") || val.startSkip("call")) {
			NDebug("RegexRoute",DebugAll,"Including context '%s' by rule #%u '%s'",
			    val.c_str(),i+1,n->name().c_str());
			if (oneContext(msg,str,val,ret,depth+1)) {
			    DDebug("RegexRoute",DebugAll,"Returning true from context '%s'", context.c_str());
			    return true;
			}
		    }
		    else if (val.startSkip("match") || val.startSkip("newmatch")) {
			if (!val.null()) {
			    NDebug("RegexRoute",DebugAll,"Setting match string '%s' by rule #%u '%s' in context '%s'",
				val.c_str(),i+1,n->name().c_str(),context.c_str());
			    str = val;
			}
		    }
		    else {
			DDebug("RegexRoute",DebugAll,"Returning '%s' for '%s' in context '%s' by rule #%u '%s'",
			    val.c_str(),str.c_str(),context.c_str(),i+1,n->name().c_str());
			ret = val;
			return true;
		    }
		}
	    }
	}
    }
    DDebug("RegexRoute",DebugAll,"Returning false at end of context '%s'", context.c_str());
    return false;
}
	
bool RouteHandler::received(Message &msg)
{
    u_int64_t tmr = Time::now();
    String called(msg.getValue("called"));
    if (called.null())
	return false;
    const char *context = msg.getValue("context","default");
    String ret;
    Lock lock(s_mutex);
    if (oneContext(msg,called,context,ret)) {
	Debug(DebugInfo,"Routing call to '%s' in context '%s' via '%s' in " FMT64 " usec",
	    called.c_str(),context,ret.c_str(),Time::now()-tmr);
	msg.retValue() = ret;
	return true;
    }
    Debug(DebugInfo,"Could not route call to '%s' in context '%s', wasted " FMT64 " usec",
	called.c_str(),context,Time::now()-tmr);
    return false;
};
		    
class PrerouteHandler : public MessageHandler
{
public:
    PrerouteHandler(int prio)
	: MessageHandler("call.preroute",prio) { }
    virtual bool received(Message &msg);
};

bool PrerouteHandler::received(Message &msg)
{
    u_int64_t tmr = Time::now();
    // return immediately if there is already a context
    if (msg.getValue("context"))
	return false;

    String caller(msg.getValue("caller"));
    if (caller.null())
	return false;

    String ret;
    Lock lock(s_mutex);
    if (oneContext(msg,caller,"contexts",ret)) {
	Debug(DebugInfo,"Classifying caller '%s' in context '%s' in " FMT64 " usec",
	    caller.c_str(),ret.c_str(),Time::now()-tmr);
	msg.addParam("context",ret);
	return true;
    }
    Debug(DebugInfo,"Could not classify call from '%s', wasted " FMT64 " usec",
	caller.c_str(),Time::now()-tmr);
    return false;
};
		    
class GenericHandler : public MessageHandler
{
public:
    GenericHandler(const char* name, int prio)
	: MessageHandler(name,prio)
	{
	    Debug(DebugAll,"Installing generic handler for '%s' prio %d [%p]",c_str(),prio,this);
	    s_extra.append(this);
	}
    ~GenericHandler()
	{ s_extra.remove(this,false); }
    virtual bool received(Message &msg);
};

bool GenericHandler::received(Message &msg)
{
    DDebug(DebugAll,"Handling message '%s' [%p]",c_str(),this);
    String ret,what(*this);
    Lock lock(s_mutex);
    return oneContext(msg,what,*this,ret);
}

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
    Lock lock(s_mutex);
    s_cfg = Engine::configFile("regexroute");
    s_cfg.load();
    s_extended = s_cfg.getBoolValue("priorities","extended",false);
    s_insensitive = s_cfg.getBoolValue("priorities","insensitive",false);
    if (m_preroute) {
	delete m_preroute;
	m_preroute = 0;
    }
    if (m_route) {
	delete m_route;
	m_route = 0;
    }
    s_extra.clear();
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
    NamedList *l = s_cfg.getSection("extra");
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = l->getParam(i);
	    if (n)
		Engine::install(new GenericHandler(n->name(),n->toInteger()));
	}
    }
}

INIT_PLUGIN(RegexRoutePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
