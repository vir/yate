/**
 * callcounters.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Count active call legs per user defined context
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

class Context : public String
{
public:
    inline Context(const String& name)
	: String(name), m_count(0)
	{ }
    inline int count() const
	{ return m_count; }
    inline bool has(const String& id) const
	{ return 0 != m_calls.find(id); }
    inline void add(const String& id)
	{ m_calls.append(new String(id)); ++m_count; }
    bool remove(const String& id);
private:
    ObjList m_calls;
    int m_count;
};

class CallCountersPlugin : public Plugin
{
public:
    CallCountersPlugin();
    virtual ~CallCountersPlugin();
    virtual void initialize();
};


static bool s_allCounters = false;
static String s_paramName;
static String s_paramPrefix;
static String s_direction;

static ObjList s_contexts;
static Mutex s_mutex(false,"CallCounters");

INIT_PLUGIN(CallCountersPlugin);


class CdrHandler : public MessageHandler
{
public:
    CdrHandler(int prio)
	: MessageHandler("call.cdr",prio,__plugin.name())
	{ }
    virtual bool received(Message& msg);
};

class RouteHandler : public MessageHandler
{
public:
    RouteHandler(int prio)
	: MessageHandler("call.route",prio,__plugin.name())
	{ }
    virtual bool received(Message& msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler()
	: MessageHandler("engine.status",100,__plugin.name())
	{ }
    virtual bool received(Message& msg);
};

class CommandHandler : public MessageHandler
{
public:
    CommandHandler()
	: MessageHandler("engine.command",100,__plugin.name())
	{ }
    virtual bool received(Message& msg);
};


bool Context::remove(const String& id)
{
    String* s = static_cast<String*>(m_calls.remove(id,false));
    if (!s)
	return false;
    delete s;
    DDebug(&__plugin,DebugAll,"Removing call '%s' from context '%s'",
	id.c_str(),c_str());
    if (--m_count <= 0) {
	DDebug(&__plugin,DebugInfo,"Removing empty context '%s'",c_str());
	s_contexts.remove(this);
    }
    return true;
}


bool CdrHandler::received(Message& msg)
{
    const String* chan = msg.getParam("chan");
    if (TelEngine::null(chan))
	return false;
    if (s_direction) {
	const String* dir = msg.getParam("direction");
	if (!dir || (*dir != s_direction))
	    return false;
    }
    const String* oper = msg.getParam("operation");
    const String* ctxt = msg.getParam(s_paramName);
    Lock mylock(s_mutex);
    if (oper && (*oper == "finalize")) {
	// finalizing a CDR, remove call from any context
	if (!TelEngine::null(ctxt)) {
	    // first try to search in context, usually it will be there
	    Context* c = static_cast<Context*>(s_contexts[*ctxt]);
	    if (c && c->remove(*chan))
		return false;
	    DDebug(&__plugin,DebugNote,"Call '%s' not removed from '%s'",chan->c_str(),ctxt->c_str());
	}
	// now we have to look in all contexts
	for (ObjList* l = s_contexts.skipNull(); l; l=l->skipNext()) {
	    Context* c = static_cast<Context*>(l->get());
	    if (c->remove(*chan))
		return false;
	}
	DDebug(&__plugin,DebugAll,"Call '%s' not found in any context",chan->c_str());
    } // finalize operation
    else {
	if (TelEngine::null(ctxt))
	    return false;
	// first look up the call in the context it was supposed to be in
	Context* c = static_cast<Context*>(s_contexts[*ctxt]);
	if (c && c->has(*chan))
	    return false;
	// call has new context, remove from any old context
	for (ObjList* l = s_contexts.skipNull(); l; l=l->skipNext()) {
	    Context* c2 = static_cast<Context*>(l->get());
	    if ((c2 != c) && c2->remove(*chan))
		break;
	}
	if (!c) {
	    DDebug(&__plugin,DebugInfo,"Creating context '%s'",ctxt->c_str());
	    c = new Context(*ctxt);
	    s_contexts.append(c);
	}
	DDebug(&__plugin,DebugAll,"Adding call '%s' to context '%s'",
	    chan->c_str(),ctxt->c_str());
	c->add(*chan);
    }
    return false;
};


bool RouteHandler::received(Message& msg)
{
    if (msg.getBoolValue("allcounters",s_allCounters)) {
	Lock mylock(s_mutex);
	for (ObjList* l = s_contexts.skipNull(); l; l=l->skipNext()) {
	    Context* c = static_cast<Context*>(l->get());
	    msg.setParam(s_paramPrefix + "_" + *c,String(c->count()));
	}
    }
    else {
	const String* ctxt = msg.getParam(s_paramName);
	if (TelEngine::null(ctxt))
	    return false;
	Lock mylock(s_mutex);
	Context* c = static_cast<Context*>(s_contexts[*ctxt]);
	if (c)
	    msg.setParam(s_paramPrefix,String(c->count()));
    }
    return false;
};


bool StatusHandler::received(Message &msg)
{
    const String* sel = msg.getParam("module");
    if (!TelEngine::null(sel) && (*sel != __plugin.name()))
	return false;
    String st("name=callcounters,type=misc,format=Context|Count");
    s_mutex.lock();
    st << ";counters=" << s_contexts.count();
    if (msg.getBoolValue("details",true)) {
	st << ";";
	bool first = true;
	for (ObjList* l = s_contexts.skipNull(); l; l=l->skipNext()) {
	    Context* c = static_cast<Context*>(l->get());
	    if (first)
		first = false;
	    else
		st << ",";
	    st << *c << "=" << c->count();
	}
    }
    s_mutex.unlock();
    msg.retValue() << st << "\r\n";
    return false;
}


bool CommandHandler::received(Message &msg)
{
    if (!msg.getParam("line")) {
	String* tmp = msg.getParam("partline");
	if (tmp && (*tmp == "status")) {
	    tmp = msg.getParam("partword");
	    if (!tmp || tmp->null() || __plugin.name().startsWith(*tmp))
		msg.retValue().append(__plugin.name(),"\t");
	}
    }
    return false;
}


CallCountersPlugin::CallCountersPlugin()
    : Plugin("callcounters")
{
    Output("Loaded module CallCounters");
}

CallCountersPlugin::~CallCountersPlugin()
{
    Output("Unloading module CallCounters");
}

void CallCountersPlugin::initialize()
{
    Configuration cfg(Engine::configFile(name().c_str()));
    s_allCounters = cfg.getBoolValue("general","allcounters",false);
    // tracked parameter, direction and priorities cannot be reloaded
    if (s_paramName.null()) {
	s_paramName = cfg.getValue("general","parameter");
	if (s_paramName) {
	    Output("Initializing module CallCounters");
	    s_paramPrefix = s_paramName + "_count";
	    s_direction = cfg.getValue("general","direction","incoming");
	    // pre-hash the strings not protected by mutex
	    s_paramName.hash();
	    s_direction.hash();
	    Engine::install(new CdrHandler(cfg.getIntValue("priorities","call.cdr",20)));
	    Engine::install(new RouteHandler(cfg.getIntValue("priorities","call.route",20)));
	    Engine::install(new CommandHandler);
	    Engine::install(new StatusHandler);
	}
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
