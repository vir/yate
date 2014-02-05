/**
 * enumroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ENUM routing module
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

#define ENUM_DEF_TIMEOUT 3
#define ENUM_DEF_RETRIES 2
#define ENUM_DEF_MINLEN  8
#define ENUM_DEF_MAXCALL 30000

class EnumModule : public Module
{
public:
    inline EnumModule()
	: Module("enumroute","route"), m_init(false)
	{ }
    virtual void initialize();
    virtual void statusParams(String& str);
    void genUpdate(Message& msg);
private:
    bool m_init;
};

static String s_prefix;
static String s_forkStop;
static String s_domains;
static unsigned int s_minlen;
static int s_timeout;
static int s_retries;
static int s_maxcall;

static bool s_redirect;
static bool s_autoFork;
static bool s_sipUsed;
static bool s_iaxUsed;
static bool s_h323Used;
static bool s_xmppUsed;
static bool s_telUsed;
static bool s_voiceUsed;
static bool s_pstnUsed;
static bool s_voidUsed;

static Mutex s_mutex(false,"EnumRoute");
static int s_queries = 0;
static int s_routed = 0;
static int s_reroute = 0;

static EnumModule emodule;

class EnumHandler : public MessageHandler
{
public:
    inline EnumHandler(unsigned int prio = 90)
	: MessageHandler("call.route",prio,emodule.name())
	{ }
    virtual bool received(Message& msg);
private:
    static bool resolve(Message& msg,bool canRedirect);
    static void addRoute(String& dest,const String& src);
};


// Routing message handler, performs checks and calls resolve method
bool EnumHandler::received(Message& msg)
{
    if (s_domains.null() || !msg.getBoolValue("enumroute",true))
	return false;
    // perform per-thread initialization of resolver and timeout settings
    if (!Resolver::init(s_timeout,s_retries))
	return false;
    return resolve(msg,s_telUsed);
}

// Resolver function, may call itself recursively at most once
bool EnumHandler::resolve(Message& msg,bool canRedirect)
{
    // give preference to full (e164) called number if exists
    String called(msg.getValue("calledfull"));
    if (called.null())
	called = msg.getValue("called");
    if (called.null())
	return false;
    // check if the called starts with international prefix, remove it
    if (!(called.startSkip("+",false) ||
	 (s_prefix && called.startSkip(s_prefix,false))))
	return false;
    if (called.length() < s_minlen)
	return false;
    s_mutex.lock();
    ObjList* domains = s_domains.split(',',false);
    s_mutex.unlock();
    if (!domains)
	return false;
    bool rval = false;
    // put the standard international prefix in front
    called = "+" + called;
    String tmp;
    for (int i = called.length()-1; i > 0; i--)
	tmp << called.at(i) << ".";
    u_int64_t dt = Time::now();
    ObjList res;
    for (ObjList* l = domains; l; l = l->next()) {
	const String* s = static_cast<const String*>(l->get());
	if (!s || s->null())
	    continue;
	int result = Resolver::naptrQuery(tmp + *s,res);
	if ((result == 0) && res.skipNull())
	    break;
    }
    dt = Time::now() - dt;
    Debug(&emodule,DebugInfo,"Returned %d NAPTR records in %u.%06u s",
	res.count(),(unsigned int)(dt / 1000000),(unsigned int)(dt % 1000000));
    TelEngine::destruct(domains);
    bool reroute = false;
    bool unassigned = false;
    if (res.skipNull()) {
	msg.retValue().clear();
	bool autoFork = msg.getBoolValue("autofork",s_autoFork);
	for (ObjList* cur = res.skipNull(); cur; cur = cur->skipNext()) {
	    NaptrRecord* ptr = static_cast<NaptrRecord*>(cur->get());
	    DDebug(&emodule,DebugAll,"order=%d pref=%d '%s'",
		ptr->order(),ptr->pref(),ptr->serv().c_str());
	    String serv = ptr->serv();
	    serv.toUpper();
	    String callto = called;
	    if (s_sipUsed && (serv == "E2U+SIP") && ptr->replace(callto)) {
		addRoute(msg.retValue(),"sip/" + callto);
		rval = true;
		if (autoFork)
		    continue;
		break;
	    }
	    if (s_iaxUsed && (serv == "E2U+IAX2") && ptr->replace(callto)) {
		addRoute(msg.retValue(),"iax/" + callto);
		rval = true;
		if (autoFork)
		    continue;
		break;
	    }
	    if (s_h323Used && (serv == "E2U+H323") && ptr->replace(callto)) {
		addRoute(msg.retValue(),"h323/" + callto);
		rval = true;
		if (autoFork)
		    continue;
		break;
	    }
	    if (s_xmppUsed && (serv == "E2U+XMPP") && ptr->replace(callto)) {
		addRoute(msg.retValue(),"jingle/" + callto);
		rval = true;
		if (autoFork)
		    continue;
		break;
	    }
	    if (s_pstnUsed && serv.startsWith("E2U+PSTN") && ptr->replace(callto)) {
		addRoute(msg.retValue(),"pstn/" + callto);
		rval = true;
		if (autoFork)
		    continue;
		break;
	    }
	    if (s_voiceUsed && serv.startsWith("E2U+VOICE") && ptr->replace(callto)) {
		addRoute(msg.retValue(),"voice/" + callto);
		rval = true;
		if (autoFork)
		    continue;
		break;
	    }
	    if (canRedirect && (serv == "E2U+TEL") && ptr->replace(callto)) {
		if (callto.startSkip("tel:",false) ||
		    callto.startSkip("TEL:",false) ||
		    callto.startSkip("e164:",false) ||
		    callto.startSkip("E164:",false))
		{
		    reroute = true;
		    rval = false;
		    msg.setParam("called",callto);
		    msg.clearParam("calledfull");
		    if (msg.retValue()) {
			Debug(&emodule,DebugMild,"Redirect drops collected route: %s",
			    msg.retValue().c_str());
			msg.retValue().clear();
		    }
		    break;
		}
		continue;
	    }
	    if (s_voidUsed && serv.startsWith("E2U+VOID") && ptr->replace(callto)) {
		// remember it's unassigned but still continue scanning
		unassigned = true;
	    }
	}
    }
    s_mutex.lock();
    if (rval) {
	if (msg.retValue().startsWith("fork",true)) {
	    msg.setParam("maxcall",String(s_maxcall));
	    msg.setParam("fork.stop",s_forkStop);
	}
        else if (s_redirect)
	    msg.setParam("redirect",String::boolText(true));
    }
    s_queries++;
    if (rval)
	s_routed++;
    if (reroute)
	s_reroute++;
    emodule.changed();
    s_mutex.unlock();
    if (reroute)
	return resolve(msg,false);
    if (unassigned && !rval) {
	rval = true;
	msg.retValue() = "-";
	msg.setParam("error","unallocated");
    }
    return rval;
}

// Add one route to the result, take care of forking
void EnumHandler::addRoute(String& dest,const String& src)
{
    if (dest.null())
	dest = src;
    else {
	if (!dest.startsWith("fork",true))
	    dest = "fork " + dest;
	dest << " | " << src;
    }
}


void EnumModule::statusParams(String& str)
{
    str.append("queries=",",") << s_queries << ",routed=" << s_routed << ",rerouted=" << s_reroute;
}

void EnumModule::genUpdate(Message& msg)
{
    msg.setParam("queries",String(s_queries));
    msg.setParam("routed",String(s_routed));
    msg.setParam("rerouted",String(s_reroute));
}

void EnumModule::initialize()
{
    Module::initialize();
    Configuration cfg(Engine::configFile("enumroute"));
    int prio = cfg.getIntValue("general","priority",0);
    if ((prio <= 0) && !m_init)
	return;
    Output("Initializing ENUM routing");
    s_mutex.lock();
    // in most of the world this default international prefix should work
    s_prefix = cfg.getValue("general","prefix","00");
    s_domains = cfg.getValue("general","domains");
    if (s_domains.null()) {
	// old style, just for compatibility
	s_domains = cfg.getValue("general","domain","e164.arpa");
	s_domains.append(cfg.getValue("general","backup","e164.org"),",");
    }
    s_forkStop = cfg.getValue("general","forkstop","busy");
    s_mutex.unlock();
    DDebug(&emodule,DebugInfo,"Domain list: %s",s_domains.c_str());
    s_minlen = cfg.getIntValue("general","minlen",ENUM_DEF_MINLEN);
    int tmp = cfg.getIntValue("general","timeout",ENUM_DEF_TIMEOUT);
    // limit between 1 and 10 seconds
    if (tmp < 1)
	tmp = 1;
    if (tmp > 10)
	tmp = 10;
    s_timeout = tmp;
    tmp = cfg.getIntValue("general","retries",ENUM_DEF_RETRIES);
    // limit between 1 and 5 retries
    if (tmp < 1)
	tmp = 1;
    if (tmp > 5)
	tmp = 5;
    s_retries = tmp;
    // overall a resolve attempt will take at most 50s per domain

    tmp = cfg.getIntValue("general","maxcall",ENUM_DEF_MAXCALL);
    // limit between 2 and 120 seconds
    if (tmp < 2000)
	tmp = 2000;
    if (tmp > 120000)
	tmp = 120000;
    s_maxcall = tmp;

    s_redirect = cfg.getBoolValue("general","redirect");
    s_autoFork = cfg.getBoolValue("general","autofork");
    s_sipUsed  = cfg.getBoolValue("protocols","sip",true);
    s_iaxUsed  = cfg.getBoolValue("protocols","iax",true);
    s_h323Used = cfg.getBoolValue("protocols","h323",true);
    s_xmppUsed = cfg.getBoolValue("protocols","jingle",true);
    s_voidUsed = cfg.getBoolValue("protocols","void",true);
    // by default don't support the number rerouting
    s_telUsed  = cfg.getBoolValue("protocols","tel",false);
    // also don't enable gateways by default as more setup is needed
    s_pstnUsed = cfg.getBoolValue("protocols","pstn",false);
    s_voiceUsed= cfg.getBoolValue("protocols","voice",false);
    if (m_init || (prio <= 0))
	return;
    m_init = true;
    if (Resolver::available(Resolver::Naptr))
	Engine::install(new EnumHandler(cfg.getIntValue("general","priority",prio)));
    else
	Debug(&emodule,DebugGoOn,"NAPTR resolver is not available on this platform");
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
