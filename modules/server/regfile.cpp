/**
 * regfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Ask for a registration from this module.
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

class RegfilePlugin : public Plugin
{
public:
    RegfilePlugin();
    ~RegfilePlugin();
    virtual void initialize();
    void populate(bool first);
private:
    bool m_init;
};

Mutex s_mutex(false,"RegFile");
static Configuration s_cfg(Engine::configFile("regfile"));
static Configuration s_accounts;
static bool s_create = false;
static const String s_general = "general";
static ObjList s_expand;
static int s_count = 0;

INIT_PLUGIN(RegfilePlugin);


class AuthHandler : public MessageHandler
{
public:
    AuthHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class RegistHandler : public MessageHandler
{
public:
    RegistHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class UnRegistHandler : public MessageHandler
{
public:
    UnRegistHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class RouteHandler : public MessageHandler
{
public:
    RouteHandler(const char *name, unsigned prio = 100);
    ~RouteHandler();
    virtual bool received(Message &msg);
private:
    ObjList* m_skip;
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class CommandHandler : public MessageHandler
{
public:
    CommandHandler(const char *name, unsigned prio = 100)
	: MessageHandler(name,prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class ExpireHandler : public MessageHandler
{
public:
    ExpireHandler()
	: MessageHandler("engine.timer",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class ExpandedUser : public ObjList
{
public:
    inline ExpandedUser(const String& username)
	: m_username(username) {}
    virtual const String& toString () const
	{ return m_username; }
private:
    String m_username;
};


static void clearListParams(NamedList& list, const char* name)
{
    if (TelEngine::null(name))
	return;
    NamedString* param = list.getParam(name);
    if (!param)
	return;
    ObjList* l = param->split(',',false);
    for (ObjList* o = l->skipNull(); o; o = o->skipNext())
	list.clearParam(o->get()->toString());
    list.clearParam(param);
    TelEngine::destruct(l);
}

bool expired(const NamedList& list, unsigned int time)
{
    // If eTime is 0 the registration will never expire
    unsigned int eTime = list.getIntValue("expires",0);
    return eTime && eTime < time;
}

// Copy list parameters
static inline void regfileCopyParams(NamedList& dest, NamedList& src, const String& params,
    const String& extra)
{
    String s = params;
    s.append(extra,",");
    dest.copyParams(src,s);
}


bool AuthHandler::received(Message &msg)
{
    if (!msg.getBoolValue(YSTRING("auth_regfile"),true))
	return false;
    String username(msg.getValue("username"));
    if (username.null() || username == s_general)
	return false;
    Lock lock(s_mutex);
    const NamedList* usr = s_cfg.getSection(username);
    if (!usr)
	return false;
    const String* pass = usr->getParam("password");
    if (!pass)
	return false;
    msg.retValue() = *pass;
    Debug(&__plugin,DebugAll,"Authenticating user %s with password length %u",
	username.c_str(),pass->length());
    return true;
}

bool RegistHandler::received(Message &msg)
{
    if (!msg.getBoolValue(YSTRING("register_regfile"),true))
	return false;
    String username(msg.getValue("username"));
    if (username.null() || username == s_general)
	return false;
    const char* driver = msg.getValue("driver");
    const char* data = msg.getValue("data");
    if (!data)
	return false;
    Lock lock(s_mutex);
    int expire = msg.getIntValue("expires",0);
    NamedList* sect = s_cfg.getSection(username);
    if (!sect) {
	if (!s_create)
	    return false;
	Debug(&__plugin,DebugInfo,"Auto creating new user %s",username.c_str());
    }
    NamedList* s = s_accounts.createSection(username);
    if (driver)
	s->setParam("driver",driver);
    s->setParam("data",data);
    // Clear existing route parameters
    clearListParams(*s,"route_params");
    const String& route = msg["route_params"];
    if (route) {
	s->copyParams(msg,route);
	s->setParam("route_params",route);
    }
    s->clearParam("connection",'_');
    s->copyParams(msg,"connection",'_');
    if (expire)
	s->setParam("expires",String(msg.msgTime().sec() + expire));
#ifdef DEBUG
    String tmp;
    s->dump(tmp," ");
    Debug(&__plugin,DebugAll,"Registered user %s",tmp.c_str());
#else
    Debug(&__plugin,DebugAll,"Registered user %s via %s",username.c_str(),data);
#endif
    return true;
}

bool UnRegistHandler::received(Message &msg)
{
    if (!msg.getBoolValue(YSTRING("register_regfile"),true))
	return false;
    const String& username = msg["username"];
    if (username) {
	if (username == s_general)
	    return false;
	Lock lock(s_mutex);
	NamedList* nl = s_accounts.getSection(username);
	if (!nl)
	    return false;
	Debug(&__plugin,DebugAll,"Removing user %s, reason unregistered",username.c_str());
	s_accounts.clearSection(username);
	return true;
    }
    const String& conn = msg["connection_id"];
    if (!conn)
	return false;
    Lock lock(s_mutex);
    ObjList remove;
    unsigned int n = s_accounts.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* nl = s_accounts.getSection(i);
	if (nl && (*nl)["connection_id"] == conn)
	    remove.append(new String(*nl));
    }
    for (ObjList* o = remove.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	Debug(&__plugin,DebugAll,"Removing user %s, reason connection down",s->c_str());
	s_accounts.clearSection(*s);
    }
    return false;
}

RouteHandler::RouteHandler(const char *name, unsigned prio)
    : MessageHandler(name,prio,__plugin.name())
{
    m_skip = String("alternatives,password").split(',');
}

RouteHandler::~RouteHandler()
{
    TelEngine::destruct(m_skip);
}

bool RouteHandler::received(Message &msg)
{
    if (!msg.getBoolValue(YSTRING("route_regfile"),true))
	return false;
    String user = msg.getValue("caller");
    Lock lock(s_mutex);
    NamedList* params = 0;
    if (user) {
	params = s_cfg.getSection(user);
	if (params) {
	    unsigned int n = params->length();
	    for (unsigned int i = 0; i < n; i++) {
		const NamedString* s = params->getParam(i);
		if (s && !m_skip->find(s->name())) {
		    String value = *s;
		    msg.replaceParams(value);
		    msg.setParam(s->name(),value);
		}
	    }
	}
    }

    String username(msg.getValue("called"));
    if (username.null() || username == s_general)
	return false;
    NamedList* ac = s_accounts.getSection(username);
    while (true) {
	String data;
	String extra;
	if (!ac) {
	    if (s_cfg.getSection(username)) {
		msg.setParam("error","offline");
		break;
	    }
	    ObjList* o = s_expand.find(username);
	    if (!o)
		break;
	    ExpandedUser* eu = static_cast<ExpandedUser*>(o->get());
	    if (!eu)
		break;
	    ObjList targets;
	    int count = 0;
	    for (ObjList* ob = eu->skipNull(); ob;ob = ob->skipNext()) {
		String* s = static_cast<String*>(ob->get());
		if (!s)
		    continue;
		NamedList* n = s_accounts.getSection(*s);
		if (!n)
		    continue;
		targets.append(n)->setDelete(false);
		count ++;
	    }
	    if (count == 0) {
		msg.setParam("error","offline");
		break;
	    }
	    if (count == 1) {
		NamedList* n = static_cast<NamedList*>(targets.skipNull()->get());
		data = (*n)["data"];
		regfileCopyParams(msg,*n,(*n)["route_params"],"driver");
	    }
	    else {
		int callto = 1;
		data = "fork";
		for (ObjList* o = targets.skipNull(); o; o = o->skipNext()) {
		    NamedList* n = static_cast<NamedList*>(o->get());
		    String prefix;
		    prefix << "callto." << callto++;
		    const String& d = (*n)["data"];
		    NamedList* target = new NamedList(d);
		    msg.addParam(new NamedPointer(prefix,target,d));
		    extra << " " << d;
		    regfileCopyParams(*target,*n,(*n)["route_params"],"driver");
		}
	    }
	} else {
	    data = ac->getValue("data");
	    regfileCopyParams(msg,*ac,(*ac)["route_params"],"driver");
	}
	msg.retValue() = data;
	Debug(&__plugin,DebugInfo,"Routed '%s' via '%s%s'",username.c_str(),
	    data.c_str(),extra.c_str());
	return true;
    }
    return false;
}

bool ExpireHandler::received(Message &msg)
{
    if ((s_count = (s_count+1) % 30)) // Check for timeouts once at 30 seconds
	return false;
    unsigned int time = msg.msgTime().sec();
    Lock lock(s_mutex);
    int count = s_accounts.sections();
    for (int i = 0;i < count;) {
	NamedList* sec = s_accounts.getSection(i);
	if (sec && *sec != s_general && expired(*sec,time)) {
	    Debug(&__plugin,DebugAll,"Removing user %s, Reason: Registration expired",sec->c_str());
	    s_accounts.clearSection(*sec);
	    count--;
	} else
	   i++;
    }
    if (s_accounts)
	s_accounts.save();
    return false;
}

bool StatusHandler::received(Message &msg)
{
    String dest(msg.getValue("module"));
    if (dest && (dest != "regfile") && (dest != "misc"))
	return false;
    Lock lock(s_mutex);
    unsigned int n = s_cfg.sections();
    if (s_cfg.getSection("general") || !s_cfg.getSection(0))
	n--;
    msg.retValue() << "name=regfile,type=misc;create=" << s_create;
    msg.retValue() << ",defined=" << n;

    unsigned int usrCount = 0;
    String tmp;
    bool details = msg.getBoolValue("details",true);
    unsigned int count = s_accounts.sections();
    for (unsigned int i = 0; i < count; i ++) {
	NamedList* ac = s_accounts.getSection(i);
	if (!ac)
	    continue;
	String data = ac->getValue("data");
	if (data.null())
	    continue;
	usrCount++;
	if (!details)
	    continue;
	if (tmp.null())
	    tmp << ";";
	else
	    tmp << ",";
	for (char* s = const_cast<char*>(data.c_str()); *s; s++) {
	    if (*s < ' ' || *s == ',')
		*s = '?';
	}
	tmp << *ac << "=" << data;
    }
    msg.retValue() << ",users=" << usrCount;
    msg.retValue() << tmp << "\r\n";
    return false;
}

bool CommandHandler::received(Message &msg)
{
    if (msg.getValue("line"))
	return false;
    if (msg["partline"] == "status") {
	const String& partWord = msg["partword"];
	if (partWord.null() || __plugin.name().startsWith(partWord))
	    msg.retValue().append(__plugin.name(),"\t");
    }
    return false;
}

RegfilePlugin::RegfilePlugin()
    : Plugin("regfile"),
      m_init(false)
{
    Output("Loaded module Registration from file");
}

RegfilePlugin::~RegfilePlugin()
{
    Output("Unload module Registration from file");
    if (s_accounts)
	s_accounts.save();
}

void RegfilePlugin::initialize()
{
    Output("Initializing module Register from file");
    Lock lock(s_mutex);
    s_cfg.load();
    bool first = !m_init;
    if (!m_init) {
	m_init = true;
	s_create = s_cfg.getBoolValue("general","autocreate",false);
	String conf = s_cfg.getValue("general","file");
	Engine::self()->runParams().replaceParams(conf);
	if (conf) {
	    s_accounts = conf;
	    s_accounts.load();
	}
	Engine::install(new AuthHandler("user.auth",s_cfg.getIntValue("general","auth",100)));
	Engine::install(new RegistHandler("user.register",s_cfg.getIntValue("general","register",100)));
	Engine::install(new UnRegistHandler("user.unregister",s_cfg.getIntValue("general","register",100)));
	Engine::install(new RouteHandler("call.route",s_cfg.getIntValue("general","route",100)));
	Engine::install(new StatusHandler("engine.status"));
	Engine::install(new CommandHandler("engine.command"));
	Engine::install(new ExpireHandler());
    }
    populate(first);
}

void RegfilePlugin::populate(bool first)
{
    s_expand.clear();
    int count = s_cfg.sections();
    for (int i = 0;i < count; i++) {
	NamedList* nl = s_cfg.getSection(i);
	if (!nl || *nl == s_general)
	    continue;
	DDebug(this,DebugAll,"Loaded account '%s'",nl->c_str());
	String* ids = nl->getParam("alternatives");
	if (!ids)
	    continue;
	ObjList* ob = ids->split(',');
	for (ObjList* o = ob->skipNull(); o;o = o->skipNext()) {
	    String* sec = static_cast<String*>(o->get());
	    if (!sec)
		continue;
	    ObjList* ret = s_expand.find(*sec);
	    ExpandedUser* eu = 0;
	    if (!ret) {
		eu = new ExpandedUser(*sec);
		s_expand.append(eu);
	    } else
		eu = static_cast<ExpandedUser*>(ret->get());
	    eu->append(new String(*nl));
	    DDebug(this,DebugAll,"Added alternative '%s' for account '%s'",sec->c_str(),nl->c_str());
	}
	TelEngine::destruct(ob);
    }
    if (s_create)
	return;
    count = s_accounts.sections();
    for (int i = 0;i < count;i++) {
	NamedList* nl = s_accounts.getSection(i);
	if (!nl)
	    continue;
	// Delete saved accounts logged in on reliable connections on first load
	bool exist = s_cfg.getSection(*nl) != 0;
	if (exist && !(first && nl->getBoolValue("connection_reliable"))) {
	    DDebug(this,DebugAll,"Loaded saved account '%s'",nl->c_str());
	    continue;
	}
	DDebug(this,DebugAll,"Not loading saved account '%s': %s",
	    nl->c_str(),exist ? "logged in on reliable connection" : "account deleted");
	s_accounts.clearSection(*nl);
	count--;
	i--;
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
