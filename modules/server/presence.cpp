/**
 * presence.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Presence module
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


#include <yatecbase.h>
#include <yateclass.h>


using namespace TelEngine;


namespace {

#define MIN_COUNT               16      // Minimum allowed value for presence list count
#define MAX_COUNT               256     // Maximum allowed value for presence list count
#define EXPIRE_CHECK_MAX        10000   // interval in miliseconds for checking object for expiring
#define TIME_TO_KEEP            60000   // interval in miliseconds for keeping an object in memory
#define TIME_TO_KEEP_MIN        10000   // Minimum allowed value for presence expiry interval
#define TIME_TO_KEEP_MAX        300000  // Maximum allowed value for presence expiry interval

class PresenceList;
class Presence;
class ResNotifyHandler;
class EngineStartHandler;
class PresenceModule;
class ExpirePresence;

/*
 * class Presence
 * A presence object
 */
class Presence : public GenObject
{
public:
    Presence(const String& id, bool online = true, const char* instance = 0,
	const char* data = 0, unsigned int expiresMsecs = 0, const char* node = 0);
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
    inline const String& node() const
	{ return m_nodeName; }
    // Check if a given node is the same as ours
    inline bool isNode(const String& node) {
	    return node == m_nodeName || (!node && m_nodeName == Engine::nodeName()) ||
		(!m_nodeName && node == Engine::nodeName());
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
    String m_nodeName;  // Location
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
    Presence* removePresence(const String& contact, const String& instance,
	const String* node = 0, bool delObj = true);
    // delete expired objects
    void expire();
    // Find an item by id and instance
    ObjList* find(const String& contact, const String& instance);
};

class ResNotifyhandler;
class EngineStarthandler;

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
static unsigned int s_presExpire = 0;            // Presence expire interval (relese memory only)
unsigned int ExpirePresence::s_expireTime = 0;

INIT_PLUGIN(PresenceModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}


/*
 * Class ResNotifyHandler
 * Handles a resource.notify message
 */
class ResNotifyHandler : public MessageHandler
{
public:
    inline ResNotifyHandler(unsigned int priority = 10)
	: MessageHandler("resource.notify",priority,__plugin.name())
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
	: MessageHandler("engine.start",100,__plugin.name())
	{}
    virtual bool received(Message& msg);
};

/*
 * ResNotifyHandler
 */
bool ResNotifyHandler::received(Message& msg)
{
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
    const String& node = msg["nodename"];
    DDebug(&__plugin,DebugAll,"Processing %s contact=%s oper=%s node=%s",
	msg.c_str(),contact->c_str(),operation->c_str(),node.safe());
    String* instance = msg.getParam("instance");
    PresenceList* list = __plugin.getList(*contact);
    if (*operation == "online" || *operation == "update") {
	if (TelEngine::null(instance))
	    return false;
	Lock lock(list);
	Presence* pres = list->findPresence(*contact,*instance);
	bool newPres = (pres == 0);
	if (newPres) {
	    pres = new Presence(*contact,true,*instance,0,0,node);
	    list->append(pres);
	}
	else if (!pres->isNode(node)) {
	    // Instance online on more then 1 node
	    Debug(&__plugin,DebugNote,
		"User('%s') duplicate online instance '%s' on node '%s' (current '%s')",
		contact->c_str(),instance->c_str(),node.c_str(),pres->node().c_str());
	    return false;
	}
	pres->update(msg.getValue("data"),s_presExpire);
	String* capsid = msg.getParam("caps.id");
	if (!TelEngine::null(capsid))
	    pres->setCaps(*capsid,msg);
	Debug(&__plugin,DebugAll,"User '%s' instance=%s node=%s is online",
	    contact->c_str(),instance->c_str(),pres->node().c_str());
	// Update database only if we expire the data from memory
	//  and the instance is located on this machine
	if (s_presExpire && node == Engine::nodeName()) {
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
	Presence* pres = list->removePresence(*contact,*instance,&node,false);
	list->unlock();
	if (pres)
	    Debug(&__plugin,DebugAll,"User '%s' instance=%s node=%s is offline",
		contact->c_str(),instance->c_str(),pres->node().safe());
	// Remove from database only if we expire the data from memory
	//  and the instance is located on this machine
	if (pres && s_presExpire && node == Engine::nodeName())
	    TelEngine::destruct(__plugin.queryDb(__plugin.buildDeleteDb(*pres)));
	TelEngine::destruct(pres);
    }
    else if (*operation == "query") {
	Lock lock(list);
	if (!TelEngine::null(instance)) {
	    Presence* pres = list->findPresence(*contact,*instance);
	    if (pres) {
		msg.addParam("data",pres->data());
		if (pres->node())
		    msg.addParam("nodename",pres->node());
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
		if (pres->node())
		    msg.addParam(param + "nodename",pres->node());
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
    const char* data, unsigned int expireMs, const char* node)
    : m_id(id), m_instance(instance), m_data(data), m_online(online),
    m_caps(0), m_nodeName(node)
{
    updateExpireTime(expireMs);
    DDebug(&__plugin,DebugAll,
	"Presence contact='%s' instance='%s' online=%s node=%s [%p]",
	id.c_str(),instance,String::boolText(m_online),node,this);
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

// Remove an item from list. Optionally delete it
// Return the item if found and not deleted
Presence* PresenceList::removePresence(const String& contact, const String& instance,
    const String* node, bool delObj)
{
    ObjList* o = find(contact,instance);
    if (!o)
	return 0;
    if (node && !(static_cast<Presence*>(o->get()))->isNode(node))
	return 0;
    return static_cast<Presence*>(o->remove(delObj));
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

// Retrieve an integer value from config
// Check bounds
static unsigned int getCfgUInt(Configuration& cfg, const char* par, int def, int min, int max,
    bool allowZero = false, const char* sect = "general")
{
    int i = cfg.getIntValue(sect,par,def);
    if (i < 0)
	i = 0;
    if (!i && allowZero)
	return 0;
    if (i < min)
	return min;
    if (i > max)
	return max;
    return i;
}

void PresenceModule::initialize()
{
    Output("Initializing module Presence");

    Configuration cfg(Engine::configFile("presence"));

    if (!m_list) {
	setup();
	installRelay(Halt);

	m_notifyHandler = new ResNotifyHandler();
	Engine::install(m_notifyHandler);
	m_engineStartHandler = new EngineStartHandler();
	Engine::install(m_engineStartHandler);

	m_listCount = getCfgUInt(cfg,"listcount",MIN_COUNT,MIN_COUNT,MAX_COUNT);
	m_list = new PresenceList[m_listCount];

	// database connection init
	m_accountDB = cfg.getValue("database","account");
	if (m_accountDB)
	    m_removeAllDB = cfg.getValue("database","remove_all");

	// expire thread?
	unsigned int chk = getCfgUInt(cfg,"expirecheck",0,1000,EXPIRE_CHECK_MAX,true);
	String disable;
	while (chk) {
	    if (!m_accountDB) {
		disable = "database account not set";
		break;
	    }
#define GET_QUERY(var,param) { \
    var = cfg.getValue("database",param); \
    if (!var) { \
	disable << "'" << param << "' is empty"; \
	break; \
    } \
}
	    // Init database queries
	    GET_QUERY(m_insertDB,"insert_presence");
	    GET_QUERY(m_updateDB,"update_presence");
	    GET_QUERY(m_removeResDB,"remove_instance");
	    GET_QUERY(m_removePresDB,"remove_presence");
	    GET_QUERY(m_selectResDB,"select_instance");
	    GET_QUERY(m_selectPresDB,"select_presence");
#undef GET_QUERY
	    s_presExpire = getCfgUInt(cfg,"expiretime",TIME_TO_KEEP,TIME_TO_KEEP_MIN,
		TIME_TO_KEEP_MAX);
	    if (s_presExpire < chk)
		s_presExpire = chk;
	    (new ExpirePresence(chk))->startup();
	    break;
	}
	if (disable) {
	    Debug(this,DebugNote,"Disabled presence expiring: %s",disable.c_str());
	    m_insertDB.clear();
	    m_updateDB.clear();
	    m_removeResDB.clear();
	    m_removePresDB.clear();
	    m_selectResDB.clear();
	    m_selectPresDB.clear();
	}
	Debug(this,DebugAll,"Initialized lists=%u expirecheck=%u expiretime=%u account=%s",
	      m_listCount,chk,s_presExpire,m_accountDB.c_str());
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
