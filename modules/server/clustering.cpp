/**
 * clustering.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Clustering server support.
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class ClusterModule : public Module
{
public:
    enum {
	Register = Private,
	Cdr = (Private << 1),
    };
    ClusterModule();
    ~ClusterModule();
    bool unload();
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual bool msgRoute(Message& msg);
    virtual bool msgExecute(Message& msg);
    virtual bool msgRegister(Message& msg);
    virtual bool msgCdr(Message& msg);
private:
    String m_prefix;
    String m_myPrefix;
    String m_callto;
    Regexp m_regexp;
    String m_message;
    bool m_init;
    bool m_handleReg;
    bool m_handleCdr;
};

INIT_PLUGIN(ClusterModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}


bool ClusterModule::unload()
{
    if (!lock(500000))
	return false;
    uninstallRelays();
    unlock();
    return true;
}

bool ClusterModule::msgRoute(Message& msg)
{
    String called = msg.getValue("called");
    if (called.null())
	return false;
    Lock lock(this);
    if (!called.startSkip(m_prefix,false))
	return false;
    lock.drop();
    const char* tmp = msg.getValue("sip_x-callto");
    if (called.trimBlanks().null() && !tmp)
	return false;
    Debug(&__plugin,DebugInfo,"Got call to '%s' on this node '%s'",
	called.c_str(),tmp);
    msg.setParam("called",called);
    if (called.null() && tmp) {
	msg.retValue() = tmp;
	tmp = msg.getValue("sip_x-billid");
	if (tmp)
	    msg.setParam("billid",tmp);
	return true;
    }
    return false;
}

bool ClusterModule::msgExecute(Message& msg)
{
    String callto = msg.getValue("callto");
    if (callto.null())
	return false;
    String tmp = callto;
    Lock lock(this);
    if (!callto.startSkip(m_prefix,false))
	return false;
    int sep = callto.find("/");
    if (sep < 0)
	return false;
    String node = callto.substr(0,sep).trimBlanks();
    callto = callto.substr(sep+1);
    if (callto.trimBlanks().null())
	return false;
    DDebug(&__plugin,DebugAll,"Call to '%s' on node '%s'",callto.c_str(),node.c_str());
    // check if the node is to be dynamically allocated
    if ((node == "*") && m_message) {
	Message m(m_message);
	m.addParam("allocate",String::boolText(true));
	m.addParam("nodename",Engine::nodeName());
	m.addParam("callto",callto);
	const char* param = msg.getValue("billid");
	if (param)
	    m.addParam("billid",param);
	param = msg.getValue("username");
	    m.addParam("username",param);
	if (!Engine::dispatch(m) || (m.retValue() == "-") || (m.retValue() == "error")) {
	    const char* error = m.getValue("error","failure");
	    const char* reason = m.getValue("reason");
	    Debug(&__plugin,DebugWarn,"Could not get node for '%s'%s%s%s%s",
		callto.c_str(),
		(error ? ": " : ""), c_safe(error),
		(reason ? ": " : ""), c_safe(reason));
	    if (error)
		msg.setParam("error",error);
	    else
		msg.clearParam("error");
	    if (reason)
		msg.setParam("reason",reason);
	    else
		msg.clearParam("reason");
	    return false;
	}
	node = m.retValue();
	Debug(&__plugin,DebugInfo,"Using node '%s' for '%s'",
	    node.c_str(),callto.c_str());
    }
    msg.setParam("callto",callto);
    // if the call is for the local node just let it through
    if (node.null() || (Engine::nodeName() == node))
	return false;
    if (!node.matches(m_regexp)) {
	msg.setParam("callto",tmp);
	return false;
    }
    String dest = node.replaceMatches(m_callto);
    lock.drop();
    msg.replaceParams(dest);
    if (dest.trimBlanks().null()) {
	msg.setParam("callto",tmp);
	return false;
    }
    Debug(&__plugin,DebugNote,"Call to '%s' on node '%s' goes to '%s'",
	callto.c_str(),node.c_str(),dest.c_str());
    msg.setParam("callto",dest);
    msg.setParam("osip_x-callto",callto);
    msg.setParam("osip_x-billid",msg.getValue("billid"));
    msg.setParam("osip_x-nodename",Engine::nodeName());
    msg.setParam("osip_x-username",msg.getValue("username"));
    return false;
}

bool ClusterModule::msgRegister(Message& msg)
{
    String data = msg.getValue("data");
    if (data.null())
	return false;
    Lock lock(this);
    if (data.startsWith(m_prefix))
	return false;
    msg.setParam("data",m_myPrefix + data);
    return false;
}

bool ClusterModule::msgCdr(Message& msg)
{
    if (!msg.getParam("nodename"))
	msg.addParam("nodename",Engine::nodeName());
    if (!msg.getParam("nodeprefix")) {
	lock();
	msg.addParam("nodeprefix",m_myPrefix);
	unlock();
    }
    return false;
}

bool ClusterModule::received(Message& msg, int id)
{
    switch (id) {
	case Execute:
	    return msgExecute(msg);
	case Register:
	    return m_handleReg && msgRegister(msg);
	case Cdr:
	    return m_handleCdr && msgCdr(msg);
	default:
	    return Module::received(msg,id);
    }
}

ClusterModule::ClusterModule()
    : Module("clustering","misc",true),
      m_init(false), m_handleReg(false), m_handleCdr(false)
{
    Output("Loaded module Clustering");
}

ClusterModule::~ClusterModule()
{
    Output("Unloading module Clustering");
}

void ClusterModule::initialize()
{
    if (Engine::nodeName().null()) {
	Debug(&__plugin,DebugNote,"Node name is empty, clustering disabled.");
	return;
    }
    Output("Initializing module Clustering");
    Configuration cfg(Engine::configFile("clustering"));
    lock();
    m_prefix = cfg.getValue("general","prefix","cluster");
    if (!m_prefix.endsWith("/"))
	m_prefix += "/";
    m_myPrefix = m_prefix + Engine::nodeName() + "/";
    m_regexp = cfg.getValue("general","regexp");
    m_callto = cfg.getValue("general","callto");
    m_message = cfg.getValue("general","locate","cluster.locate");
    m_handleReg = cfg.getBoolValue("general","user.register",true);
    m_handleCdr = cfg.getBoolValue("general","call.cdr",true);
    unlock();
    if (!m_init && cfg.getBoolValue("general","enabled",(m_callto && m_regexp))) {
	setup();
	installRelay(Route,cfg.getIntValue("priorities","call.route",50));
	installRelay(Execute,cfg.getIntValue("priorities","call.execute",50));
	installRelay(Register,"user.register",cfg.getIntValue("priorities","user.register",50));
	installRelay(Cdr,"call.cdr",cfg.getIntValue("priorities","call.cdr",25));
	m_init = true;
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
