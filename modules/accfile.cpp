/**
 * accfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Account provider for client registrations and settings.
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

#include <yatengine.h>

using namespace TelEngine;
namespace { // anonymous

static Mutex s_mutex;
static Configuration s_cfg(Engine::configFile("accfile"));

class AccHandler : public MessageHandler
{
public:
    AccHandler()
	: MessageHandler("user.account")
	{ }
    virtual bool received(Message &msg);
};

class CmdHandler : public MessageHandler
{
public:
    CmdHandler()
	: MessageHandler("engine.command")
	{ }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler()
	: MessageHandler("engine.status")
	{ }
    virtual bool received(Message &msg);
};

class StartHandler : public MessageHandler
{
public:
    StartHandler()
	: MessageHandler("engine.start",150)
	{ }
    virtual bool received(Message &msg);
};

class AccFilePlugin : public Plugin
{
public:
    AccFilePlugin();
    ~AccFilePlugin(){}
    virtual void initialize();
private:
    bool m_first;
};

static void copyParams(NamedList& dest, const NamedList& src)
{
    for (unsigned int i=0;i<src.length();i++) {
	const NamedString* par = src.getParam(i);
	if (par && par->name() && (par->name() != "operation"))
	    dest.addParam(par->name(),*par);
    }
}

static void emitAccounts(const char* operation = 0)
{
    Lock lock(s_mutex);
    for (unsigned int i=0;i<s_cfg.sections();i++) {
	NamedList* acc = s_cfg.getSection(i);
	if (!(acc && acc->getValue("username") && acc->getBoolValue("enabled",true)))
	    continue;
	Message* m = new Message("user.login");
	copyParams(*m,*acc);
	m->setParam("account",*acc);
	if (operation)
	    m->setParam("operation",operation);
	Engine::enqueue(m);
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
    if (!line.startSkip("accounts"))
	return false;
    if (line == "reload") {
	Lock lock(s_mutex);
	s_cfg.load();
    }
    else
	emitAccounts(line);
    return true;
}

bool StatusHandler::received(Message &msg)
{
    String dest(msg.getValue("module"));
    if (dest && (dest != "accfile") && (dest != "misc"))
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
    msg.retValue() <<"\n";
    return false;
}

bool StartHandler::received(Message &msg)
{
    emitAccounts("login");
    return false;
};

AccFilePlugin::AccFilePlugin()
    : m_first(true)
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
    }
}

INIT_PLUGIN(AccFilePlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
