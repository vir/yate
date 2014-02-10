/**
 * accfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Account provider for client registrations and settings.
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

class AccFilePlugin : public Plugin
{
public:
    AccFilePlugin();
    ~AccFilePlugin(){}
    virtual void initialize();
private:
    bool m_first;
};

static Mutex s_mutex(false,"AccFile");
static Configuration s_cfg(Engine::configFile("accfile"));

static char s_helpOpt[] = "  accounts [reload|{login|logout|...} [account]]\r\n";
static char s_helpMsg[] = "Controls client accounts (to other servers) operations\r\n";

INIT_PLUGIN(AccFilePlugin);


class AccHandler : public MessageHandler
{
public:
    AccHandler()
	: MessageHandler("user.account",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class CmdHandler : public MessageHandler
{
public:
    CmdHandler()
	: MessageHandler("engine.command",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class HelpHandler : public MessageHandler
{
public:
    HelpHandler()
	: MessageHandler("engine.help",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler()
	: MessageHandler("engine.status",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class StartHandler : public MessageHandler
{
public:
    StartHandler()
	: MessageHandler("engine.start",150,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};


static void copyParams(NamedList& dest, const NamedList& src)
{
    for (unsigned int i=0;i<src.length();i++) {
	const NamedString* par = src.getParam(i);
	if (par && par->name() && (par->name() != "operation"))
	    dest.addParam(par->name(),*par);
    }
}

static bool emitAccounts(const char* operation, const String& account = String::empty())
{
    bool ok = account.null();
    Lock lock(s_mutex);
    for (unsigned int i=0;i<s_cfg.sections();i++) {
	NamedList* acc = s_cfg.getSection(i);
	if (!(acc && acc->getBoolValue("enabled",(acc->getValue("username") != 0))))
	    continue;
	if (account && (account != *acc))
	    continue;
	Message* m = new Message("user.login");
	copyParams(*m,*acc);
	m->setParam("account",*acc);
	if (operation)
	    m->setParam("operation",operation);
	ok = Engine::enqueue(m);
    }
    return ok;
}

static bool operAccounts(const String& operation, const String& account = String::empty())
{
    if (operation == "reload") {
	Lock lock(s_mutex);
	s_cfg.load();
	return true;
    }
    return emitAccounts(operation,account);
}

// perform command line completion
static void doCompletion(Message &msg, const String& partLine, const String& partWord)
{
    if (partLine.null() || (partLine == "help") || (partLine == "status"))
	Module::itemComplete(msg.retValue(),"accounts",partWord);
    else if (partLine == "accounts") {
	Module::itemComplete(msg.retValue(),"reload",partWord);
	Module::itemComplete(msg.retValue(),"login",partWord);
	Module::itemComplete(msg.retValue(),"logout",partWord);
    }
    else if ((partLine == "accounts login") || (partLine == "accounts logout")) {
	for (unsigned int i=0;i<s_cfg.sections();i++) {
	    NamedList* acc = s_cfg.getSection(i);
	    if (acc && acc->getValue("username") && acc->getBoolValue("enabled",true))
		Module::itemComplete(msg.retValue(),*acc,partWord);
	}
    }
}


bool AccHandler::received(Message &msg)
{
    String action = msg.getValue("operation");
    if (action.null())
	return false;
    if (action == "list") {
	for (unsigned int i=0;i<s_cfg.sections();i++) {
	    NamedList* acc = s_cfg.getSection(i);
	    if (!(acc && acc->getValue("username") && acc->getBoolValue("enabled",true)))
		continue;
	    msg.retValue().append(*acc,",");
	}
	return false;
    }
    String account = msg.getValue("account");
    if (account.null())
	return false;
    Lock lock(s_mutex);
    NamedList* acc = s_cfg.getSection(account);
    if (acc) {
	copyParams(msg,*acc);
	return true;
    }
    return false;
}


bool CmdHandler::received(Message &msg)
{
    String line = msg.getValue("line");
    if (line.null()) {
	doCompletion(msg,msg.getValue("partline"),msg.getValue("partword"));
	return false;
    }
    if (!line.startSkip("accounts"))
	return false;

    bool ok = false;
    int sep = line.find(' ');
    if (sep > 0)
	ok = operAccounts(line.substr(0,sep).trimBlanks(),line.substr(sep+1).trimBlanks());
    else
	ok = operAccounts(line);
    if (!ok)
	msg.retValue() = "Accounts operation failed: " + line + "\r\n";
    return true;
}


bool HelpHandler::received(Message &msg)
{
    String line = msg.getValue("line");
    if (line.null()) {
	msg.retValue() << s_helpOpt;
	return false;
    }
    if (line != "accounts")
	return false;
    msg.retValue() << s_helpOpt << s_helpMsg;
    return true;
}


bool StatusHandler::received(Message &msg)
{
    String dest(msg.getValue("module"));
    bool exact = (dest == "accfile");
    if (dest && !exact && (dest != "accounts") && (dest != "misc"))
	return false;
    Lock lock(s_mutex);
    unsigned int n = s_cfg.sections();
    if (!s_cfg.getSection(0))
	--n;
    msg.retValue() << "name=accfile,type=misc;users=" << n;
    if (msg.getBoolValue("details",true)) {
	msg.retValue() << ";";
	bool first = true;
	for (unsigned int i=0;i<s_cfg.sections();i++) {
	    NamedList *acct = s_cfg.getSection(i);
	    if (!acct)
		continue;
	    const char* user = acct->getValue("username");
	    if (first)
		first = false;
	    else
		msg.retValue() << ",";
	    msg.retValue() << *acct << "=" << user;
	}
    }
    msg.retValue() << "\r\n";
    return exact;
}


bool StartHandler::received(Message &msg)
{
    emitAccounts("login");
    return false;
};


AccFilePlugin::AccFilePlugin()
    : Plugin("accfile"),
      m_first(true)
{
    Output("Loaded module Accounts from file");
}

void AccFilePlugin::initialize()
{
    Output("Initializing module Accounts from file");
    if (m_first) {
	Lock lock(s_mutex);
	m_first = false;
	s_cfg.load();
	Engine::install(new StatusHandler);
	Engine::install(new StartHandler);
	Engine::install(new CmdHandler);
	Engine::install(new HelpHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
