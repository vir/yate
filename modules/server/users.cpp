/**
 * users.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Users module
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

namespace {

class UserUpdateHandler;

/**
  * Class UsersModule
  * Module for handling users operations
  */
class UsersModule : public Module
{
public:
    enum Commands {
	CmdUpdate  = 1,
	CmdAdd,
	CmdDelete,
    };
    UsersModule();
    virtual ~UsersModule();
    // Check if a message was sent by us
    inline bool isModule(const Message& msg) {
	    String* module = msg.getParam("module");
	    return module && *module == name();
	}
    // Build a message. Fill the module parameter
    inline Message* message(const char* msg) {
	    Message* m = new Message(msg);
	    m->addParam("module",name());
	    return m;
	}
    //inherited methods
    virtual void initialize();
    // uninstall relays and message handlers
    bool unload();
    // User management
    bool addUser(const NamedList& params, String* error = 0);
    bool deleteUser(const NamedList& params, String* error = 0);
    bool updateUser(const NamedList& params, String* error = 0);
    bool searchUser(const NamedList& params);
    // Notify user changes add/del/update
    void notifyUser(const char* user, const char* notify);
    // Build and dispatch a database message. Return true on success
    bool queryDb(const String& account, const String& query,
	const NamedList& params, String& error, bool search = false);
    // parse the command line
    bool parseParams(const String& line, NamedList& parsed, String& error);
    // Module commands
    static const TokenDict s_cmds[];
protected:
    // inherited methods
    virtual bool received(Message& msg, int id);
    virtual bool commandExecute(String& retVal, const String& line);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
private:
    //flag for first time initialization
    bool m_init;
    // Query strings
    // SQL statement for inserting to database
    String m_insertDB;
    // SQL statement for updating information in the database
    String m_updateDB;
    String m_removeDB;
    // SQL statement for interrogating the database about a contact with a resource
    String m_selectDB;
    // database connection
    String m_accountDB;

    UserUpdateHandler* m_updateHandler;
};


INIT_PLUGIN(UsersModule);

const TokenDict UsersModule::s_cmds[] = {
    {"update",  CmdUpdate},
    {"add",     CmdAdd},
    {"delete",  CmdDelete},
    {0,0}
};

static const char* s_cmdsLine = "users {add user [parameter=value...]|delete user|update user [parameter=value...]}";

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}

/**
  * Class UserUpdateHandler
  * Handles a user.update message
  */
class UserUpdateHandler : public MessageHandler
{
public:
    inline UserUpdateHandler(unsigned int priority = 100)
	: MessageHandler("user.update",priority,__plugin.name())
	{ }
    virtual ~UserUpdateHandler()
	{ }
    virtual bool received(Message& msg);
};


// Get a space separated word from a buffer. msgUnescape() it if requested
// Return false if empty
static bool getWord(String& buf, String& word, bool unescape = false)
{
    XDebug(&__plugin,DebugAll,"getWord(%s)",buf.c_str());
    int pos = buf.find(" ");
    if (pos >= 0) {
	word = buf.substr(0,pos);
	buf = buf.substr(pos + 1);
    }
    else {
	word = buf;
	buf = "";
    }
    if (!word)
	return false;
    if (unescape)
	word.msgUnescape();
    return true;
}


/**
  * UserUpdateHandler
  */
// could change
bool UserUpdateHandler::received(Message& msg)
{
    if (__plugin.isModule(msg))
	return false;
    // TODO
    // see message parameters and what to do with them
    String* operation = msg.getParam("operation");
    String* user = msg.getParam("user");
    if (TelEngine::null(operation) || TelEngine::null(user)) {
	msg.setParam("error","Mandatory parameters missing");
	return false;
    }

    NamedList params("");
    params.addParam("user",*user);
    params.copyParams(msg,"password");
    String msgPrefix = msg.getValue("message-prefix");
    if (!msgPrefix.null()) {
	msgPrefix << ".";
	for (unsigned int i = 0; i < msg.length(); i++) {
	    NamedString* ns = msg.getParam(i);
	    if (ns && ns->name().startsWith(msgPrefix))
		params.addParam(ns->name().substr(msgPrefix.length()),*ns);
	}
    }
    bool ok = false;
    if (*operation == "add")
	ok = __plugin.addUser(params);
    else if (*operation == "delete")
	ok = __plugin.deleteUser(params);
    else if (*operation == "update")
	ok = __plugin.updateUser(params);
    else
	return false;
    if (ok)
	__plugin.notifyUser(*user,*operation);
    else
	msg.setParam("error","failure");
    return ok;
}


/**
  * UsersModule
  */
UsersModule:: UsersModule()
    : Module("users","misc"),
    m_updateHandler(0)
{
    Output("Loaded module Users Management");
}

UsersModule::~UsersModule()
{
    Output("Unloaded module Users Management");
    TelEngine::destruct(m_updateHandler);
}

void UsersModule::initialize()
{
    Output("Initializing module Users Management");

    Configuration cfg(Engine::configFile("users"));
    cfg.load();

    if (!m_init) {
	m_init = true;
	setup();
	installRelay(Halt);
	installRelay(Help);

	m_updateHandler = new UserUpdateHandler();
	Engine::install(m_updateHandler);
	// queries init
	m_insertDB = cfg.getValue("database", "add_user");
	m_updateDB = cfg.getValue("database", "update_user");
	m_removeDB = cfg.getValue("database", "remove_user");
	m_selectDB = cfg.getValue("database", "select_user");

	// database connection init
	m_accountDB = cfg.getValue("database", "account");
    }
}

bool UsersModule::unload()
{
    DDebug(this,DebugAll,"unload()");
    if (!lock(500000))
	return false;
    uninstallRelays();
    Engine::uninstall(m_updateHandler);
    unlock();
    return true;
}

bool UsersModule::addUser(const NamedList& params, String* error)
{
    if (TelEngine::null(m_insertDB))
	return false;
    String tmp;
    if (!error)
	error = &tmp;
    if (!searchUser(params)) {
	if (queryDb(m_accountDB,m_insertDB,params,*error)) {
	    Debug(this,DebugAll,"Added user '%s'",params.getValue("user"));
	    return true;
	}
	if (error->null())
	    *error = "Failure";
    }
    else
	*error = "Already exists";
    Debug(this,DebugInfo,"Failed to add user '%s' error='%s'",
	params.getValue("user"),error->c_str());
    return false;
}

bool UsersModule::deleteUser(const NamedList& params, String* error)
{
    if (TelEngine::null(m_removeDB))
	return false;
    String tmp;
    if (!error)
	error = &tmp;
    if (queryDb(m_accountDB,m_removeDB,params,*error)) {
	Debug(this,DebugAll,"Deleted user '%s'",params.getValue("user"));
	return true;
    }
    if (error->null())
	*error = "User not found";
    Debug(this,DebugInfo,"Failed to delete user '%s' error='%s'",
	params.getValue("user"),error->c_str());
    return false;
}

bool UsersModule::updateUser(const NamedList& params, String* error)
{
    if (TelEngine::null(m_updateDB))
	return false;
    String tmp;
    if (!error)
	error = &tmp;
    if (queryDb(m_accountDB,m_updateDB,params,*error)) {
	Debug(this,DebugAll,"Updated user '%s'",params.getValue("user"));
	return true;
    }
    if (!error)
	*error = "User not found";
    Debug(this,DebugInfo,"Failed to update user '%s' error='%s'",
	params.getValue("user"),error->c_str());
    return false;
}

bool UsersModule::searchUser(const NamedList& params)
{
    if (TelEngine::null(m_selectDB))
	return false;
    String error;
    return queryDb(m_accountDB,m_selectDB,params,error,true);
}

// Notify user changes add/del/update
void UsersModule::notifyUser(const char* user, const char* notify)
{
    Message* m = __plugin.message("user.update");
    m->addParam("notify",notify);
    m->addParam("user",user);
    Engine::enqueue(m);
}

// Build and dispatch a database message. Return true on success
bool UsersModule::queryDb(const String& account, const String& query,
    const NamedList& params, String& error, bool search)
{
    Message msg("database");
    msg.addParam("module",name());
    msg.addParam("account", account);
    String tmp = query;
    params.replaceParams(tmp,true);
    msg.addParam("query", tmp);
    msg.addParam("results", String::boolText(true));
    bool ok = Engine::dispatch(msg) && !msg.getParam("error");
    if (ok) {
	if (query != m_insertDB) {
	    if (search)
		ok = msg.getIntValue("rows") > 1;
	    else
		ok = msg.getIntValue("affected") >= 1;
	}
	else {
	    Array* a = static_cast<Array*>(msg.userObject(YATOM("Array")));
	    String* res = a ? YOBJECT(String,a->get(0,1)) : 0;
	    ok = res && (res->toInteger() != 0);
	}
    }
    if (!ok)
	error = msg.getValue("error");
    return ok;
}

bool UsersModule::parseParams(const String& line, NamedList& parsed, String& error)
{
    Debug(this,DebugAll,"parseParams(%s)",line.c_str());
    bool ok = true;
    ObjList* list = line.split(' ',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	int pos = s->find("=");
	// Empty parameter name is not allowed
	if (pos < 1) {
	    error << "Invalid parameter " << *s;
	    ok = false;
	    break;
	}
	String name = s->substr(0,pos);
	String value = s->substr(pos + 1);
	name.msgUnescape();
	value.msgUnescape();
	parsed.addParam(name,value);
	DDebug(&__plugin,DebugAll,"parseParams() found '%s'='%s'",name.c_str(),value.c_str());
    }
    TelEngine::destruct(list);
    return ok;
}

bool UsersModule::received(Message& msg, int id)
{
    if (id == Halt)
	unload();
    else if (id == Help) {
	String line = msg.getValue("line");
	if (line.null()) {
	    msg.retValue() << "  " << s_cmdsLine << "\r\n";
	    return false;
	}
	if (line != name())
	    return false;
	msg.retValue() << "Commands used to control the Users Management module\r\n";
	msg.retValue() << s_cmdsLine << "\r\n";
	return true;
    }
    return Module::received(msg,id);
}

bool UsersModule::commandExecute(String& retVal, const String& line)
{
    String tmp(line);
    if (!tmp.startSkip(name(),false))
	return false;
    tmp.trimSpaces();
    XDebug(this,DebugAll,"commandExecute(%s)",tmp.c_str());
    // Retrieve the command
    String cmdStr;
    int cmd = 0;
    if (getWord(tmp,cmdStr))
	cmd = lookup(cmdStr,s_cmds);
    if (!cmd) {
	retVal << "Unknown command\r\n";
	return true;
    }
    // Retrieve the user
    String user;
    if (!getWord(tmp,user,true)) {
	retVal << "Empty username\r\n";
	return true;
    }
    // Execute the command
    bool ok = false;
    String error;
    if (cmd == CmdUpdate || cmd == CmdAdd || cmd == CmdDelete) {
	NamedList p("");
	p.addParam("user",user);
	if (parseParams(tmp,p,error)) {
	    if (cmd == CmdUpdate)
		ok = updateUser(p,&error);
	    else if (cmd == CmdAdd)
		ok = addUser(p,&error);
	    else
		ok = deleteUser(p,&error);
	}
	if (ok)
	    notifyUser(user,cmdStr);
    }
    else {
	Debug(this,DebugStub,"Command '%s' not implemented",cmdStr.c_str());
	error = "Unknown command";
    }
    retVal << name() << " " << cmdStr << (ok ? " succedded" : " failed");
    if (!ok && error)
	retVal << ". " << error;
    retVal << "\r\n";
    return true;
}

bool UsersModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    XDebug(this,DebugAll,"commandComplete() partLine='%s' partWord=%s",
	partLine.c_str(),partWord.c_str());
    // No line or 'help': complete module name
    if (partLine.null() || partLine == "help")
	return Module::itemComplete(msg.retValue(),name(),partWord);
    // Line is module name: complete module commands
    if (partLine == name()) {
	for (const TokenDict* list = s_cmds; list->token; list++)
	    Module::itemComplete(msg.retValue(),list->token,partWord);
	return true;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
