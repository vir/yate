/**
 * presence.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Presence module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2009 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#include <yatecbase.h>
#include <yateclass.h>


using namespace TelEngine;


namespace {

#define MIN_COUNT		16	// number of lists for holding presences
#define EXPIRE_CHECK_MAX	10000 	// interval in miliseconds for checking object for expiring
#define TIME_TO_KEEP		30000	// interval in miliseconds for keeping an object in memory

class PresenceList;
class Presence;
class ResNotifyHandler;
class EngineStartHandler;
class PresenceModule;
class ExpirePresence;

/*
 * Class ResNotifyHandler
 * Handles a resource.notify message
 */
class ResNotifyHandler : public MessageHandler
{
public:
    inline ResNotifyHandler(unsigned int priority = 10)
	: MessageHandler("resource.notify", priority)
	{ }
    virtual ~ResNotifyHandler()
	{ }
    virtual bool received(Message& msg);
};

/*
 * Class EngineStartHandler
 * Handles a engine.start message
 */
class EngineStartHandler : public MessageHandler
{
public:
    inline EngineStartHandler()
	: MessageHandler("engine.start",100)
	{}
    virtual bool received(Message& msg);
};

/*
 * class Presence
 * A presence object
 */
class Presence : public GenObject
{
public:
    Presence(const String& id, bool online = true, const char* instance = 0,
	const char* data = 0, unsigned int expiresMsecs = 0);
    ~Presence();
    inline void update(const char* data, unsigned int expireMs) {
	    m_data = data;
	    updateExpireTime(expireMs);
	}
    inline const String& getInstance() const
	{ return m_instance; }
    inline const String& data() const
	{ return m_data; }
    virtual const String& toString() const
	{ return m_id; }
    inline bool hasExpired(u_int64_t time = Time::msecNow()) const
	{ return m_expires && m_expires < time; }
    inline bool isOnline() const
	{ return m_online;}
    inline void setOnline(bool online = true)
	{ m_online = online; }
    inline void updateExpireTime(unsigned int msecs)
	{ m_expires = msecs ? Time::msecNow() + msecs : 0; }
    inline bool isCaps(const String& capsid) const
	{ return m_caps && *m_caps == capsid; }
    inline void setCaps(const String& capsid, const NamedList& list) {
	    TelEngine::destruct(m_caps);
	    m_caps = new NamedList(capsid);
	    m_caps->copyParams(list,"caps",'.');
	}
    // Copy parameters to a list
    void addCaps(NamedList& list, const String& prefix = String::empty());
private:
    String m_id;	// presence id
    String m_instance;	// presence instance, for jabber it represents the resource
    String m_data;	// presence data, format unknown
    u_int64_t m_expires;// time at which this object will expire
    bool m_online;	// online/offline flag
    NamedList* m_caps;  // Capabilities
};

/*
 * class PresenceList
 * A list of presences
 */
class PresenceList : public ObjList, public Mutex
{
public:
    // create a list
    PresenceList();
    ~PresenceList();
    // find all presences with given id, disregarding the instance
    ObjList* findPresence(const String& id);
    // find a presence with the given instance
    inline Presence* findPresence(const String& contact, const String& instance) {
	    ObjList* o = find(contact,instance);
	    return o ? static_cast<Presence*>(o->get()) : 0;
	}
    // Remove an item from list. Optionally delete it
    // Return the item if found and not deleted
    inline Presence* removePresence(const String& contact, const String& instance,
	bool delObj = true) {
	    ObjList* o = find(contact,instance);
	    return o ? static_cast<Presence*>(o->remove(delObj)) : 0;
	}
    // delete expired objects
    void expire();
    // Find an item by id and instance
    ObjList* find(const String& contact, const String& instance);
};

/*
 * Class PresenceModule
 * Module for handling presences
 */
class PresenceModule : public Module
{
    friend class ExpirePresence;
public:
    PresenceModule();
    virtual ~PresenceModule();
    //inherited methods
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    // Get the list containing a given user
    inline PresenceList* getList(const String& contact)
	{ return m_list + (contact.hash() % m_listCount); }
    // Update capabilities for all instances with the given caps id
    void updateCaps(const String& capsid, Message& msg);
    // append presence
    void addPresence(Presence* pres, bool onlyLocal = false);
    // remove presence
    void removePresence(Presence* pres);
    // remove presence by id
    void removePresenceById(const String& id);
    // find presence by id
    ObjList* findPresenceById(const String& id);
    // find presence with an instance
    Presence* findPresence(const String& contact, const String& instance);
    // update a presence
    void updatePresence(Presence* pres, const char* data);
    // uninstall relays and message handlers
    bool unload();

    // Build a 'database' message used to update presence
    Message* buildUpdateDb(const Presence& pres, bool newPres);
    // Build a 'database' message used to delete presence
    Message* buildDeleteDb(const Presence& pres);

    // database comunnication functions
    bool insertDB(Presence* pres);
    bool updateDB(Presence* pres);
    bool removeDB(Presence* pres, bool allInstances = false, bool allPresences = false, String machine = "");
    bool queryDB(String id, String instance);
    bool getInfoDB(String id, String instance, NamedList* result);

    // Build and dispatch a 'database' message. Replace query params
    // Return a valid Message on success
    Message* queryDb(const String& account, const String& query,
	const NamedList& params);
    // Dispatch a 'database' message. Return a valid Message pointer on success
    // Consume the given pointer
    Message* queryDb(Message* msg);

private:
    // array of lists that hold presences
    PresenceList* m_list;
    // resource.notify handler
    ResNotifyHandler* m_notifyHandler;
    // engine.start handler
    EngineStartHandler* m_engineStartHandler;
    // number of lists in which presences will be held
    unsigned int m_listCount;
    // thread for removing expired objects
    ExpirePresence* m_expireThread;
    // Query strings
    // SQL statement for inserting to database
    String m_insertDB;
    // SQL statement for updating information in the database
    String m_updateDB;
    // SQL statement for remove a presence with a resource
    String m_removeResDB;
    // SQL statement for removing all instances of a contact
    String m_removePresDB;
    // SQL statement for removing all presences of this node
    String m_removeAllDB;
    // SQL statement for interrogating the database about a contact with a resource
    String m_selectResDB;
    // SQL statement for interrogating the database about a contact without a resource
    String m_selectPresDB;
    // database connection
    String m_accountDB;
};

/*
 * class ExpirePresence
 * A thread that deletes old presences
 */
class ExpirePresence : public Thread
{
public:
    // create a thread whick will check for expired objects after the given "checkAfter" interval
    ExpirePresence(unsigned int checkAfter = 5000);
    ~ExpirePresence();
    void run();
    static unsigned int s_expireTime;
private:
    unsigned int m_checkMs;
};

static String s_msgPrefix = "presence";
static unsigned int s_presExpire;        // Presence expire interval (relese memory only)
static unsigned int s_presExpireCheck;   // Presence expire check interval
unsigned int ExpirePresence::s_expireTime = 0;
INIT_PLUGIN(PresenceModule);


UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}

/*
 * ResNotifyHandler
 */
bool ResNotifyHandler::received(Message& msg)
{
    // TODO
    // see message parameters and what to do with them
    String* node = msg.getParam("nodename");
    if (!TelEngine::null(node) && *node != Engine::nodeName())
	__plugin.removeDB(0, true, true, *node);

    String* operation = msg.getParam("operation");
    if (TelEngine::null(operation))
	return false;

    if (*operation == "updatecaps") {
	String* capsid = msg.getParam("caps.id");
	if (TelEngine::null(capsid))
	    return false;
	DDebug(&__plugin,DebugAll,"Processing %s oper=%s capsid=%s",
	    msg.c_str(),operation->c_str(),capsid->c_str());
	__plugin.updateCaps(*capsid,msg);
	return false;
    }

    String* contact = msg.getParam("contact");
    if (TelEngine::null(contact))
	return false;
    DDebug(&__plugin,DebugAll,"Processing %s contact=%s oper=%s",
	msg.c_str(),contact->c_str(),operation->c_str());
    String* instance = msg.getParam("instance");
    PresenceList* list = __plugin.getList(*contact);
    if (*operation == "online" || *operation == "update") {
	if (TelEngine::null(instance))
	    return false;
	Lock lock(list);
	Presence* pres = list->findPresence(*contact,*instance);
	bool newPres = (pres == 0);
	if (newPres) {
	    pres = new Presence(*contact,true,*instance);
	    list->append(pres);
	}
	pres->update(msg.getValue("data"),s_presExpire);
	String* capsid = msg.getParam("caps.id");
	if (!TelEngine::null(capsid))
	    pres->setCaps(*capsid,msg);
	// Update database only if we expire the data from memory
	if (s_presExpire) {
	    Message* m = __plugin.buildUpdateDb(*pres,newPres);
	    lock.drop();
	    TelEngine::destruct(__plugin.queryDb(m));
	}
    }
    else if (*operation == "remove" || *operation == "offline") {
	if (TelEngine::null(instance)) {
	    // TODO: all contact's instances are offline
	    return false;
	}
	list->lock();
	Presence* pres = list->removePresence(*contact,*instance,false);
	list->unlock();
	// Remove from database only if we expire the data from memory
	if (pres && s_presExpire)
	    TelEngine::destruct(__plugin.queryDb(__plugin.buildDeleteDb(*pres)));
	TelEngine::destruct(pres);
    }
    else if (*operation == "query") {
	Lock lock(list);
	if (!TelEngine::null(instance)) {
	    Presence* pres = list->findPresence(*contact,*instance);
	    if (pres) {
		msg.addParam("data",pres->data());
		msg.addParam("nodename",Engine::nodeName());
		pres->addCaps(msg);
		return true;
	    }
	}
	else {
	    ObjList* l = list->findPresence(*contact);
	    if (!l)
		return false;
	    msg.addParam("message-prefix",s_msgPrefix);
	    unsigned int n = 0;
	    String prefix = s_msgPrefix + ".";
	    for (ObjList* o = l->skipNull(); o; o = o->skipNext()) {
		Presence* pres = static_cast<Presence*>(o->get());
		String param;
		param << prefix << ++n << ".";
		msg.addParam(param + "instance",pres->getInstance());
		msg.addParam(param + "data",pres->data());
		msg.addParam(param + "nodename",Engine::nodeName());
		pres->addCaps(msg,param);
	    }
	    msg.addParam(prefix + "count",String(n));
	    TelEngine::destruct(l);
	    return n != 0;
	}
    }
    return false;
}


/*
 * EngineStartHandler
 */
bool EngineStartHandler::received(Message& msg)
{
    __plugin.removeDB(0,true,true);
    return false;
}

/*
 * Presence
 */
Presence::Presence(const String& id, bool online, const char* instance,
    const char* data, unsigned int expireMs)
    : m_id(id), m_instance(instance), m_data(data), m_online(online),
    m_caps(0)
{
    updateExpireTime(expireMs);
    DDebug(&__plugin,DebugAll,"Presence contact='%s' instance='%s' online=%s [%p]",
	id.c_str(),instance,String::boolText(m_online),this);
}

Presence::~Presence()
{
    TelEngine::destruct(m_caps);
    DDebug(&__plugin,DebugAll,"Presence contact='%s' instance='%s' destroyed [%p]",
	m_id.c_str(),m_instance.c_str(),this);
}

// Copy parameters to a list
void Presence::addCaps(NamedList& list, const String& prefix)
{
    if (!m_caps)
	return;
    if (!prefix) {
	list.copyParams(*m_caps);
	return;
    }
    unsigned int n = m_caps->count();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = m_caps->getParam(i);
	if (ns)
	    list.addParam(prefix + ns->name(),*ns);
    }
}


/*
 * PresenceList
 */
PresenceList::PresenceList()
    : Mutex("PresenceList")
{
    XDebug(&__plugin,DebugAll,"PresenceList() [%p]",this);
}

PresenceList::~PresenceList()
{
    XDebug(&__plugin,DebugAll, "PresenceList destroyed [%p]",this);
}

ObjList* PresenceList::findPresence(const String& id)
{
    if (TelEngine::null(id))
	return 0;
    DDebug(&__plugin,DebugAll,"PresenceList::findPresence('%s') [%p]",id.c_str(),this);
    ObjList* res = 0;
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	if (id == o->get()->toString()) {
	    if (!res)
		res = new ObjList();
	    res->append(o->get())->setDelete(false);
	}
    }
    return res;
}

void PresenceList::expire()
{
    u_int64_t time = Time::msecNow();
    Lock lock(this);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Presence* pres = static_cast<Presence*>(o->get());
	if (pres->hasExpired(time)) {
	    Debug(&__plugin,DebugAll,"Presence (%p) contact=%s instance=%s expired",
		pres,pres->toString().c_str(),pres->getInstance().c_str());
	    __plugin.removePresence(pres);
	}
    }
}

// Find an item by id and instance
ObjList* PresenceList::find(const String& contact, const String& instance)
{
    if (contact.null() || instance.null())
	return 0;
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Presence* pres = static_cast<Presence*>(o->get());
	if (contact == pres->toString() && instance == pres->getInstance())
	    return o;
    }
    return 0;
}


/*
 * ExpirePresence
 */
ExpirePresence::ExpirePresence(unsigned int checkAfter)
    : Thread("ExpirePresence"), m_checkMs(checkAfter)
{
    __plugin.lock();
    __plugin.m_expireThread = this;
    __plugin.unlock();
}

ExpirePresence::~ExpirePresence()
{
    Debug(&__plugin,DebugAll,"ExpirePresence thread terminated [%p]",this);
    __plugin.lock();
    __plugin.m_expireThread = 0;
    __plugin.unlock();
}

void ExpirePresence::run()
{
    Debug(&__plugin,DebugAll,"%s started [%p]",currentName(),this);
    while (true) {
	if (Thread::check(false) || Engine::exiting())
	    break;
	if (s_expireTime < m_checkMs)
	    Thread::idle();
	else {
	    s_expireTime = 0;
	    for (unsigned int i = 0; i < __plugin.m_listCount; i++)
		__plugin.m_list[i].expire();
	}
    }
}


/*
 * PresenceModule
 */
PresenceModule::PresenceModule()
    : Module("presence", "misc"),
    m_list(0), m_notifyHandler(0), m_engineStartHandler(0),
    m_listCount(0), m_expireThread(0)
{
    Output("Loaded module Presence");
}

PresenceModule::~PresenceModule()
{
    Output("Unloaded module Presence");
    TelEngine::destruct(m_notifyHandler);
    TelEngine::destruct(m_engineStartHandler);
    if (m_list)
	delete[] m_list;
}

void PresenceModule::initialize()
{
    Output("Initializing module Presence");

    Configuration cfg(Engine::configFile("presence"));
    cfg.load();

    if (!m_list) {
	setup();
	installRelay(Halt);

	m_notifyHandler = new ResNotifyHandler();
	Engine::install(m_notifyHandler);
	m_engineStartHandler = new EngineStartHandler();
	Engine::install(m_engineStartHandler);

	m_listCount = cfg.getIntValue("general", "listcount", MIN_COUNT);
	if (m_listCount < MIN_COUNT)
	    m_listCount = MIN_COUNT;
	m_list = new PresenceList[m_listCount];

	// expire thread?
	// TODO: Make sure these values are correct
	s_presExpireCheck = cfg.getIntValue("general", "expirecheck");
	if (s_presExpireCheck < 0)
	    s_presExpireCheck = 0;
	if (s_presExpireCheck) {
	    if (s_presExpireCheck > EXPIRE_CHECK_MAX)
		s_presExpireCheck = EXPIRE_CHECK_MAX;
	    s_presExpire = cfg.getIntValue("general", "expiretime", TIME_TO_KEEP);
	    (new ExpirePresence(s_presExpireCheck))->startup();
	}

	// queries init
	m_insertDB = cfg.getValue("database", "insert_presence", "");
	m_updateDB = cfg.getValue("database", "update_presence", "");
	m_removeResDB = cfg.getValue("database", "remove_instance", "");
	m_removePresDB = cfg.getValue("database", "remove_presence", "");
	m_removeAllDB = cfg.getValue("database", "remove_all", "");
	m_selectResDB = cfg.getValue("database", "select_instance", "");
	m_selectPresDB = cfg.getValue("database", "select_presence", "");

	// database connection init
	m_accountDB = cfg.getValue("database", "account");
    }
}

bool PresenceModule::unload()
{
    DDebug(this,DebugAll,"unload()");
    if (!lock(500000))
	return false;
    uninstallRelays();
    Engine::uninstall(m_notifyHandler);
    Engine::uninstall(m_engineStartHandler);
    if (m_expireThread)
	m_expireThread->cancel();
    unlock();
    // Wait for expire thread termination
    while (m_expireThread)
	Thread::yield();
    return true;
}

bool PresenceModule::received(Message& msg, int id)
{
    if (id == Timer)
	ExpirePresence::s_expireTime += 1000;
    else if (id == Halt) {
	unload();
	DDebug(this,DebugAll,"Halted");
    }
    return Module::received(msg,id);
}

// Update capabilities for all instances with the given caps id
void PresenceModule::updateCaps(const String& capsid, Message& msg)
{
    for (unsigned int i = 0; i < m_listCount; i++) {
	Lock lock(m_list[i]);
	for (ObjList* o = m_list[i].skipNull(); o; o = o->skipNext()) {
	    Presence* p = static_cast<Presence*>(o->get());
	    if (p->isCaps(capsid))
		p->setCaps(capsid,msg);
	}
    }
}

void PresenceModule::addPresence(Presence* pres, bool onlyLocal)
{
    if (!pres)
	return;
    unsigned int index = pres->toString().hash() % m_listCount;
    DDebug(this,DebugAll,"Adding presence (%p) contact='%s' instance='%s'",
	pres,pres->toString().c_str(),pres->getInstance().c_str());
    Lock lock(m_list[index]);
    m_list[index].append(pres);
    pres->updateExpireTime(s_presExpire);
    if (!onlyLocal)
	insertDB(pres);
}

void PresenceModule::removePresence(Presence* pres)
{
    if (!pres)
	return;
    unsigned int index = pres->toString().hash() % m_listCount;
    DDebug(this,DebugAll,"Removing presence (%p) contact=%s instance=%s",
	pres,pres->toString().c_str(),pres->getInstance().c_str());
    Lock lock(m_list[index]);
    if (!pres->isOnline())
	removeDB(pres);
    m_list[index].remove(pres);
}

void PresenceModule::removePresenceById(const String& id)
{
    if (TelEngine::null(id))
	return;
    unsigned int index = id.hash() % m_listCount;
    Lock lock(m_list[index]);
    for (ObjList* o = m_list[index].skipNull(); o; o = o->skipNext()) {
	Presence* pres = static_cast<Presence*>(o->get());
	if (pres->toString() == id) {
	    m_list[index].remove(pres);
	    removeDB(pres, true);
	}
    }
   // list->remove(id);
}

void PresenceModule::updatePresence(Presence* pres, const char* data)
{
    if (!pres)
	return;
    DDebug(this,DebugAll,"updatePresence() contact='%s' instance='%s' data='%s'",
	pres->toString().c_str(),pres->getInstance().c_str(),data);
    pres->update(data,s_presExpire);
    updateDB(pres);
}

ObjList* PresenceModule::findPresenceById(const String& id)
{
    if (TelEngine::null(id))
	return 0;
    unsigned int index = id.hash() % m_listCount;
    Lock lock(m_list[index]);
    ObjList* ol = m_list[index].findPresence(id);
    NamedList info("");
    if (!getInfoDB(id, "", &info))
	return ol;
    int count = info.getIntValue("count");
    for (int i = 1; i <= count; i++) {
	String prefix = String(i) + ".";
	String* instance = info.getParam(prefix + "instance");
	const char* data = info.getValue(prefix + "data");
	if (instance && !findPresence(id, *instance)) {
	    Presence* pres = new Presence(id, true, *instance, data);
	    ol->append(pres)->setDelete(false);
	}
    }
    return ol;
}

Presence* PresenceModule::findPresence(const String& contact, const String& instance)
{
    if (TelEngine::null(contact))
	return 0;
    DDebug(this,DebugAll,"findPresence('%s','%s')",contact.c_str(),instance.c_str());
    unsigned int index = contact.hash() % m_listCount;
    Presence* pres = m_list[index].findPresence(contact, instance);
    if (pres)
	return pres;
    NamedList info("");
    if (!getInfoDB(contact, instance, &info))
	return 0;
    int count = info.getIntValue("count");
    for (int i = 1; i <= count; i++) {
	String data = info.getValue("data");
	Presence* pres = new Presence(contact, true, instance, data);
	addPresence(pres, true);
	return pres;
    }
    return 0;
}

// Build a 'database' message used to update presence
Message* PresenceModule::buildUpdateDb(const Presence& pres, bool newPres)
{
    String& query = newPres ? m_insertDB : m_updateDB;
    if (!(m_accountDB && query))
	return 0;
    Message* msg = new Message("database");
    msg->addParam("account",m_accountDB);
    NamedList p("");
    p.addParam("contact",pres.toString());
    p.addParam("instance",pres.getInstance());
    p.addParam("nodename",Engine::nodeName());
    p.addParam("data",pres.data());
    String tmp = query;
    p.replaceParams(tmp,true);
    msg->addParam("query",tmp);
    return msg;
}

// Build a 'database' message used to delete presence
Message* PresenceModule::buildDeleteDb(const Presence& pres)
{
    if (!(m_accountDB && m_removeResDB))
	return 0;
    Message* msg = new Message("database");
    msg->addParam("account",m_accountDB);
    NamedList p("");
    p.addParam("contact",pres.toString());
    p.addParam("instance",pres.getInstance());
    String tmp = m_removeResDB;
    p.replaceParams(tmp,true);
    msg->addParam("query",tmp);
    return msg;
}

bool PresenceModule::insertDB(Presence* pres)
{
    if (!pres || TelEngine::null(m_insertDB))
	return false;
    NamedList p("");
    p.addParam("contact", pres->toString());
    p.addParam("instance", pres->getInstance());
    p.addParam("nodename", Engine::nodeName());
    p.addParam("data", pres->data());
    Message* msg = queryDb(m_accountDB, m_insertDB, p);
    if (msg) {
	TelEngine::destruct(msg);
	return true;
    }
    return false;
}

bool PresenceModule::updateDB(Presence* pres)
{
    if (!pres || TelEngine::null(m_updateDB))
	return false;
    NamedList p("");
    p.addParam("contact", pres->toString());
    p.addParam("instance", pres->getInstance());
    p.addParam("nodename", Engine::nodeName());
    p.addParam("data", pres->data());
    Message* msg = queryDb(m_accountDB, m_updateDB, p);
    if (msg) {
	TelEngine::destruct(msg);
	return true;
    }
    return false;
}

bool PresenceModule::removeDB(Presence* pres, bool allInstances, bool allPresences, String machine)
{
    NamedList queryList("");
    String query;
    if (TelEngine::null(machine))
	queryList.addParam("nodename", Engine::nodeName());
    else
	queryList.addParam("nodename", machine);
    if (allPresences) {
	query = m_removeAllDB;
    }
    else {
	if (!pres)
	    return false;
	if (!allInstances) {
	    query = m_removeResDB;
	    queryList.addParam("instance", pres->getInstance());
	}
	else
	    query = m_removePresDB;
	queryList.addParam("contact", pres->toString());
    }
    Message* msg = queryDb(m_accountDB, query, queryList);
    if (msg) {
	int n = msg->getIntValue("affected");
	if (n > 0)
	    Debug(this, DebugInfo, "Removed %d items from database", n);
	TelEngine::destruct(msg);
	return true;
    }
    return true;
}

// check only if a contact with/without instance is present, return true or false
bool PresenceModule::queryDB(String id, String instance)
{
    if (TelEngine::null(id))
	return false;
    NamedList queryList(""); 
    String query;
    queryList.addParam("contact", id);
    if (TelEngine::null(instance))
	query = m_selectPresDB;
    else {
	queryList.addParam("instance", instance);
	query = m_selectResDB;
    }
    Message* msg = queryDb(m_accountDB, query, queryList);
    if (msg) {
	bool ok = msg->getIntValue("rows") > 0;
	TelEngine::destruct(msg);
	return ok;
    }
    return false;
}

//  Interrogate database about a contact and retrieve data about it
bool PresenceModule::getInfoDB(String id, String instance, NamedList* result)
{
    if (TelEngine::null(id))
	return false;
    NamedList queryList("");
    String query;
    queryList.addParam("contact", id);
    if (TelEngine::null(instance))
	query = m_selectPresDB;
    else {
	queryList.addParam("instance", instance);
	query = m_selectResDB;
    }
    Message* msg = queryDb(m_accountDB, query, queryList);
    Array* res = msg ? static_cast<Array*>(msg->userObject("Array")) : 0;
    int n = res ? msg->getIntValue("rows") : 0;
    if (!msg || n < 1) {
	TelEngine::destruct(msg);
	return false;
    }
    result->setParam("count", String(res->getRows() - 1));
    for (int i = 0; i < res->getColumns(); i++) {
	String* colName = YOBJECT(String, res->get(i, 0));
	if (!(colName && *colName))
	    continue;
	for (int j = 1; j < res->getRows(); j++) {
	    String* val = YOBJECT(String, res->get(i, j));
	    String paramName = String(j);
	    paramName << "." << *colName;
	    if (!val)
		continue;
	    result->setParam(paramName, *val);
	}
    }
    TelEngine::destruct(msg);
    return true;
}

// Build and dispatch a 'database' message. Replace query params
Message* PresenceModule::queryDb(const String& account, const String& query,
    const NamedList& params)
{
    Message* msg = new Message("database");
    msg->addParam("account", account);
    String tmp = query;
    params.replaceParams(tmp,true);
    msg->addParam("query", tmp);
    msg->addParam("results", String::boolText(true));
    if (!Engine::dispatch(msg) || msg->getParam("error")) {
	DDebug(this,DebugNote,"Database query '%s' failed error='%s'",
	    tmp.c_str(),msg->getValue("error"));
	TelEngine::destruct(msg);
    }
    return msg;
}

// Dispatch a 'database' message. Return a valid Message pointer on success
// Consume the given pointer
Message* PresenceModule::queryDb(Message* msg)
{
    if (!msg)
	return 0;
    if (!Engine::dispatch(msg) || msg->getParam("error")) {
	DDebug(this,DebugNote,"Database query '%s' failed error='%s'",
	    msg->getValue("query"),msg->getValue("error"));
	TelEngine::destruct(msg);
    }
    return msg;
}

}

/* vi: set ts=8 sw=4 sts=4 noet: */
