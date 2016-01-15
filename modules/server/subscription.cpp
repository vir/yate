/**
 * subscription.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Subscription handler and presence notifier
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

// TODO:
// - Implement commands
//   status (user) [instances|contacts]
//   drop subscription [to|from] (user) (contact)
// - Handle automatic (un)subscribe response for known users


#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class SubscriptionState;                 // This class holds subscription states
class Instance;                          // A known instance of an user/contact
class InstanceList;                      // A list of instances
class Contact;                           // An user's contact
class User;                              // An user along with its contacts
class PresenceUser;                      // An presence user along with its contacts
class EventUser;                         // An event user along with its contacts
class ExpireThread;                      // An worker who expires event subscriptions
class UserList;                          // A list of users
class GenericUser;                       // A generic user along with its contacts
class GenericContact;                    // A generic user's contact
class GenericUserList;                   // A list of generic users
class SubMessageHandler;                 // Message handler(s) installed by the module
class SubscriptionModule;                // The module

/*
 * This class holds subscription states
 */
class SubscriptionState
{
public:
    enum Sub {
	None = 0x00,
	To = 0x01,
	From = 0x02,
	PendingIn = 0x10,
	PendingOut = 0x20,
    };
    inline SubscriptionState()
	: m_value(None)
	{}
    inline SubscriptionState(int flags)
	: m_value(flags)
	{}
    inline SubscriptionState(const String& flags)
	: m_value(0)
	{ replace(flags); }
    inline bool to() const
	{ return test(To); }
    inline bool from() const
	{ return test(From); }
    inline bool pendingOut() const
	{ return test(PendingOut); }
    inline bool pendingIn() const
	{ return test(PendingIn); }
    inline void set(int flag)
	{ m_value |= flag; }
    inline void reset(int flag)
	{ m_value &= ~flag; }
    inline void replace(int value)
	{ m_value = value; }
    inline bool test(int mask) const
	{ return (m_value & mask) != 0; }
    inline operator int() const
	{ return m_value; }
    // Replace all flags from a list
    void replace(const String& flags);
    // Build a list from flags
    void toString(String& buf) const;
    // Build a list parameters from flags
    inline void toParam(NamedList& list, const char* param = "subscription") const {
	    String buf;
	    toString(buf);
	    list.addParam(param,buf);
	}
    static const TokenDict s_names[];
private:
    int m_value;                         // The value
};

/*
 * A known instance of an user/contact
 */
class Instance : public String
{
public:
    inline Instance(const char* name, int prio)
	: String(name), m_priority(prio), m_caps(0)
	{}
    ~Instance()
	{ TelEngine::destruct(m_caps); }
    // Add prefixed parameter(s) from this instance
    void addListParam(NamedList& list, unsigned int index);
    inline bool isCaps(const String& capsid) const
	{ return m_caps && *m_caps == capsid; }
    inline void setCaps(const String& capsid, const NamedList& list) {
	    TelEngine::destruct(m_caps);
	    m_caps = new NamedList(capsid);
	    m_caps->copyParams(list,"caps",'.');
	}
    // Copy parameters to a list
    void addCaps(NamedList& list, const String& prefix = String::empty());

    int m_priority;
    NamedList* m_caps;
};

/*
 * A known instance of an user/contact
 */
class InstanceList : public ObjList
{
public:
    // Find an instance
    inline Instance* findInstance(const String& name) {
	    ObjList* o = find(name);
	    return o ? static_cast<Instance*>(o->get()) : 0;
	}
    // Insert an instance in the list. Returns it
    inline Instance* add(const String& name, int prio)
	{ return add(new Instance(name,prio)); }
    // Insert an instance in the list. Returns it
    Instance* add(Instance* inst);
    // Insert or set an existing instance. Returns it
    Instance* set(const String& name, int prio, bool* newInst = 0);
    // Update capabilities for all instances with the given caps id
    void updateCaps(const String& capsid, NamedList& list);
    // Remove an instance. Returns it found and not deleted
    inline Instance* removeInstance(const String& name, bool delObj = true) {
	    ObjList* o = find(name);
	    return o ? static_cast<Instance*>(o->remove(delObj)) : 0;
	}
    // Add prefixed parameter(s) for all instances
    // Return the number of instances added
    unsigned int addListParam(NamedList& list, String* skip = 0);
    // Notify all instances in the list to/from another one
    void notifyInstance(bool online, bool out, const String& from, const String& to,
	const String& inst, const char* data) const;
    // Notify all instances in the list with the same from/to.
    // Notifications are made from/to the given instance to/from all other instances
    void notifySkip(bool online, bool out, const String& notifier,
	const String& inst, const char* data) const;
    // Retrieve data and notify each instance in the list to a given one
    void notifyUpdate(bool online, const String& from, const String& to,
	const String& inst) const;
    // Retrieve data and notify each instance in the list to to another list
    void notifyUpdate(bool online, const String& from, const String& to,
	const InstanceList& dest) const;
};

/*
 * An user's contact
 */
class Contact : public String
{
public:
    inline Contact(const char* name, int sub)
	: String(name), m_subscription(sub)
	{}
    inline Contact(const char* name, const String& sub = String::empty())
	: String(name), m_subscription(sub)
	{}
    // Build a 'database' message used to update changes
    Message* buildUpdateDb(const String& user, bool add = false);
    // Set the contact from an array row
    void set(Array& a, int row);
    void set(String**& titles, ObjList**& data, int len);
    // Build a contact from an array row
    static Contact* build(Array& a, int row);
    static Contact* build(String**& titles, ObjList**& data, int len, int idCol);

    InstanceList m_instances;
    SubscriptionState m_subscription;
};

/*
 * An user's contact
 */
class EventContact : public NamedList
{
public:
    inline EventContact(const String& id, const NamedList& params)
	: NamedList(params), m_sequence(0) {
	    assign(id);
	    m_time = params.getIntValue("expires") * 1000 + Time::msecNow();
	}
    inline virtual ~EventContact()
	{}
    inline bool hasExpired(u_int64_t time)
	{ return time > m_time; }
    virtual const String& toString() const
	{ return *this; }
    inline unsigned int getSeq()
	{ return m_sequence++; }
    inline u_int64_t getTimeLeft()
	{ return m_time - Time::msecNow(); }
    // Notify 'dialog'
    void notify(const Message& msg);
    // Notify MWI
    void notifyMwi(const Message& msg);
    // Notify subscription termination
    void notifyTerminate(const char* reason);
private:
    u_int64_t m_time;
    unsigned int m_sequence;
};

/*
 * An user along with its contacts
 */
class User : public RefObject, public Mutex
{
public:
    User(const char* name);
    virtual ~User();
    inline const String& user() const
	{ return m_user; }
    virtual const String& toString() const
	{ return m_user; }
    ObjList m_list;                      // The list of contacts
protected:
    virtual void destroyed();
private:
    String m_user;                       // The user name
};

/*
 * An user along with its contacts
 */
class PresenceUser : public User
{
public:
    PresenceUser(const char* name);
    virtual ~PresenceUser();
    inline InstanceList& instances()
	{ return m_instances; }
    // Notify all user's instances
    void notify(const Message& msg);
    // Append a new contact
    void appendContact(Contact* c);
    inline Contact* appendContact(const char* name, int sub) {
	    Contact* c = new Contact(name,sub);
	    appendContact(c);
	    return c;
	}
    // Find a contact
    inline Contact* findContact(const String& name) {
	    ObjList* o = m_list.find(name);
	    return o ? static_cast<Contact*>(o->get()) : 0;
	}
    // Check if a contact is subscribed to user's presence
    inline bool isSubFrom(const String& contact) {
	    Contact* c = findContact(contact);
	    return c && c->m_subscription.from();
	}
    // Remove a contact. Return it if found and not deleted
    Contact* removeContact(const String& name, bool delObj = true);
    // Add or remove directed presence. Remove all instances if instance is empty.
    // Update it for all targets if target is empty
    void updateDirectNotify(bool online, const String& instance = String::empty(),
	const String& target = String::empty(), const String& targetInst = String::empty());
    // Notify offline for sent directed presence
    // Remove instance from list or clear the list
    void directNotifyOffline(const String& instance, const char* data);
    // Retrieve a directed notify instance. Create it if not found and requested
    NamedList* findDirectNotify(const String& instance, bool create = false);
    // List of directed notifications
    // Each element is a NamedList whose name is the user's instance
    // Each list's parameter name is the target
    // Parameter value may contain a target's instance
    ObjList m_directNotify;
private:
    InstanceList m_instances;            // The list of instances
};

/*
 * An user along with its contacts
 */
class EventUser : public User
{
public:
    EventUser(const char* name);
    virtual ~EventUser();
    // Notify 'dialog' to all contacts
    void notify(const Message& msg);
    // Notify 'MWI' to all contacts
    void notifyMwi(const Message& msg);
    // Append a new contact
    void appendContact(EventContact* c);
    // Find a contact
    inline EventContact* findContact(const String& name) {
	    ObjList* o = m_list.find(name);
	    return o ? static_cast<EventContact*>(o->get()) : 0;
	}
    void expire(u_int64_t time, const char* event);
    // Remove a contact. Return it if found and not deleted
    EventContact* removeContact(const String& name, bool delObj = true);
};

class ExpireThread : public Thread
{
public:
    ExpireThread(Priority prio = Thread::Normal);
    virtual ~ExpireThread();
    virtual void run();
};

/*
 * A list of users
 */
class UserList : public GenObject, public Mutex
{
public:
    UserList();
    inline ObjList& users()
	{ return m_users; }
    // Find an user. Load it from database if not found and load is true
    // Returns referrenced pointer if found
    PresenceUser* getUser(const String& user, bool load = true, bool force = false);
    // Remove an user from list
    void removeUser(const String& user);
protected:
    // Load an user from database. Build an PresenceUser object and returns it if found
    PresenceUser* askDatabase(const String& name);
private:
    ObjList m_users;                     // Users list
};

/*
 * A generic user along with its contacts
 */
class GenericUser : public RefObject, public Mutex
{
public:
    GenericUser(const char* regexp);
    ~GenericUser();
    inline bool matches(const char* str) const
	{ return m_user.matches(str); }
    inline bool compile()
	{ return m_user.compile(); }
    virtual const String& toString() const
	{ return m_user; }
    // Find a contact matching the given string
    GenericContact* find(const String& contact);
    ObjList m_list;                      // The list of contacts
protected:
    virtual void destroyed();
private:
    Regexp m_user;                       // The user regexp
};

/*
 * A generic user's contact
 */
class GenericContact : public Regexp
{
public:
    inline GenericContact(const char* regexp)
	: Regexp(regexp)
	{}
};

/*
 * A list of generic users
 */
class GenericUserList : public ObjList, public Mutex
{
public:
    GenericUserList();
    // (Re)Load from database
    void load();
    // Find an user matching the given string
    // Returns referenced pointer
    GenericUser* findUser(const String& user);
};

/*
 * Message handler(s) installed by the module
 */
class SubMessageHandler : public MessageHandler
{
public:
    enum {
	ResSubscribe,
	ResNotify,
	UserRoster,
	UserUpdate,
	EngineStart,
	CallCdr,
	Mwi,
    };
    SubMessageHandler(int handler, int prio = 80);
protected:
    virtual bool received(Message& msg);
private:
    int m_handler;
};

/*
 * The module
 */
class SubscriptionModule : public Module
{
public:
    SubscriptionModule();
    virtual ~SubscriptionModule();
    virtual void initialize();
    // Check if a message was sent by us
    inline bool isModule(const Message& msg) const {
	    String* module = msg.getParam("module");
	    return module && *module == name();
	}
    // Build a message to be sent by us
    inline Message* message(const char* msg) const {
	    Message* m = new Message(msg);
	    m->addParam("module",name());
	    return m;
	}
    // Dispatch a message
    inline bool dispatch(Message& msg) const {
	    msg.setParam("module",name());
	    return Engine::dispatch(msg);
	}
    // Enqueue a resource.notify for a given instance
    void notify(bool online, const String& from, const String& to,
	const String& fromInst = String::empty(), const String& toInst = String::empty(),
	const char* data = 0, bool sync = false);
    // Notify (un)subscribed
    void subscribed(bool sub, const String& from, const String& to);
    // Enqueue a resource.subscribe
    void subscribe(bool sub, const String& from, const String& to,
	const String* instance = 0);
    // Enqueue a resource.notify with operation=probe
    void probe(const char* from, const char* to);
    // Dispatch a user.roster message with operation 'update'
    // Load contact data from database
    // Return the database result if requested
    Array* notifyRosterUpdate(const char* username, const char* contact,
	bool retData = false, bool sync = true);
    // Handle 'resource.subscribe' for messages with event
    bool handleResSubscribe(const String& event, const String& subscriber,
	const String& notifier, const String& oper, Message& msg);
    void handleCallCdr(const Message& msg, const String& notif);
    void handleMwi(const Message& msg);
    // Handle 'resource.subscribe' messages with (un)subscribe operation
    bool handleResSubscribe(bool sub, const String& subscriber, const String& notifier,
	Message& msg);
    // Query database for event subscription authorization
    bool askDB(const NamedList& params);
    // Retrieve an event notifier
    // Valid objects are returned with reference counter increased
    EventUser* getEventUser(bool create, const String& notifier, const String& oper);
    // Remove an event's user contact. Remove the user if empty
    // Return true if the contact was removed from user
    bool removeEventUserContact(const String& user, const String& contact,
	const String& event);
    // Handle 'resource.subscribe' messages with query operation
    bool handleResSubscribeQuery(const String& subscriber, const String& notifier,
	Message& msg);
    // Handle online/offline resource.notify from contact or directed notifications
    bool handleResNotify(bool online, Message& msg);
    // Handle resource.notify with operation (un)subscribed
    bool handleResNotifySub(bool sub, const String& from, const String& to,
	Message& msg);
    // Handle resource.notify with operation probe
    bool handleResNotifyProbe(const String& from, const String& to, Message& msg);
    // Update capabilities for all instances with the given caps id
    void updateCaps(const String& capsid, NamedList& list);
    // Handle 'user.roster' messages with operation 'query'
    bool handleUserRosterQuery(const String& user, const String* contact, Message& msg);
    // Handle 'user.roster' messages with operation 'update'
    bool handleUserRosterUpdate(const String& user, const String& contact, Message& msg);
    // Handle 'user.roster' messages with operation 'delete'
    bool handleUserRosterDelete(const String& user, const String& contact, Message& msg);
    // Handle 'user.update' messages with operation 'delete'
    void handleUserUpdateDelete(const String& user, Message& msg);
    // Handle 'call.route' messages
    bool imRoute(Message& msg, const String& type);
    void expireSubscriptions();
    // Build a database message from account and query.
    // Replace query params. Return Message pointer on success
    Message* buildDb(const String& account, const String& query,
	const NamedList& params);
    // Dispatch a database message
    // Return Message pointer on success. Release msg on failure
    Message* queryDb(Message*& msg);

    String m_account;
    String m_userLoadQuery;
    String m_userEventQuery;
    String m_userDeleteQuery;
    String m_contactLoadQuery;
    String m_contactSubSetQuery;
    String m_contactSetQuery;
    String m_contactSetFullQuery;
    String m_contactDeleteQuery;
    String m_genericUserLoadQuery;
    String m_routeCallto;
    UserList m_users;
    Mutex m_eventsMutex;
    ObjList m_events;
    ExpireThread* m_expire;
    GenericUserList m_genericUsers;

protected:
    virtual bool received(Message& msg, int id);
    // Execute commands
    virtual bool commandExecute(String& retVal, const String& line);
    // Handle command complete requests
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    // Notify 'from' instances to 'to'
    void notifyInstances(bool online, PresenceUser& from, PresenceUser& to);
private:
    ObjList m_handlers;                  // Message handlers list
};


INIT_PLUGIN(SubscriptionModule);         // The module
static bool s_singleOffline = true;      // Enqueue a single 'offline' resource.notify
                                         // message when multiple instances are available
static bool s_usersLoaded = false;       // Users were loaded at startup
bool s_check = true;

// Subscription flag names
const TokenDict SubscriptionState::s_names[] = {
    {"none",        None},
    {"to",          To},
    {"from",        From},
    {"pending_in",  PendingIn},
    {"pending_out", PendingOut},
    {0,0},
};

// Message handlers installed by the module
static const TokenDict s_msgHandler[] = {
    {"resource.subscribe",  SubMessageHandler::ResSubscribe},
    {"resource.notify",     SubMessageHandler::ResNotify},
    {"user.roster",         SubMessageHandler::UserRoster},
    {"user.update",         SubMessageHandler::UserUpdate},
    {"engine.start",        SubMessageHandler::EngineStart},
    {"call.cdr",            SubMessageHandler::CallCdr},
    {"mwi",                 SubMessageHandler::Mwi},
    {0,0}
};

static const char* s_cmds[] = {
    "status",                              // Subscription status
    "unsubscribe",                         // Unsubscribe user from contact's presence
    0
};

static inline unsigned int ellapsedMs(u_int64_t start, u_int64_t now = Time::now())
{
    return (unsigned int)((now - start + 500) / 1000);
}

// Decode a list of comma separated flags
static int decodeFlags(const String& str, const TokenDict* flags)
{
    int st = 0;
    ObjList* list = str.split(',',false);
    for (ObjList* ob = list->skipNull(); ob; ob = ob->skipNext())
	st |= lookup(static_cast<String*>(ob->get())->c_str(),flags);
    TelEngine::destruct(list);
    return st;
}

// Encode a value to comma separated list of flags
static void encodeFlags(String& buf, int value, const TokenDict* flags)
{
    if (!flags)
	return;
    for (; flags->token; flags++)
	if (0 != (value & flags->value))
	    buf.append(flags->token,",");
}

// Find a list parameter having a given name and value
static NamedString* getParam(NamedList& list, const String& name, const String& value)
{
    unsigned int n = list.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = list.getParam(i);
	if (ns && ns->name() == name && *ns == value)
	    return ns;
    }
    return 0;
}

// Retrieve array rows and columns number
// Optionally allocate and fill columns and titles from array
// NOTE: The content of allocated buffers is owned by the array: don't release it
static bool arrayData(Array* a, int& rows, int& cols, ObjList**& columns, String**& titles)
{
    if (!a)
	return false;
    rows = a->getRows();
    cols = a->getColumns();
    if (cols < 1 || rows < 1)
	return false;
    columns = new ObjList*[cols];
    titles = new String*[cols];
    for (int i = 0; i < cols; i++) {
	columns[i] = a->getColumn(i);
	titles[i] = columns[i] ? YOBJECT(String,columns[i]->get()) : 0;
    }
#ifdef XDEBUG
    String s;
    for (int i = 0; i < cols; i++)
	s.append(TelEngine::c_safe(titles[i]),"|",true);
    Debug(&__plugin,DebugAll,"arrayData(%p) cols=%d rows=%d titles=%s",
	a,cols,rows,s.c_str());
#endif
    return true;
}

// Clear array data allocated using arrayData()
static void clearArrayData(ObjList**& columns, String**& titles)
{
    if (columns) {
	delete[] columns;
	columns = 0;
    }
    if (titles) {
	delete[] titles;
	titles = 0;
    }
}

// Retrieve the index of a given string in an array of ObjList pointers
static int strIndex(String**& titles, int len, const String& value)
{
    if (!titles)
	return -1;
    for (int i = 0; i < len; i++)
	if (titles[i] && *(titles[i]) == value)
	    return i;
    return -1;
}

// Advance an array of ObjList
static bool advanceObjLists(ObjList**& lists, int len)
{
    if (!lists)
	return false;
    for (int i = 0; i < len; i++)
	if (lists[i])
	    lists[i] = lists[i]->next();
    return true;
}

/*
 * SubscriptionState
 */
// Replace all flags from a list
void SubscriptionState::replace(const String& flags)
{
    m_value = decodeFlags(flags,s_names);
}

// Build a list from flags
void SubscriptionState::toString(String& buf) const
{
    encodeFlags(buf,m_value,s_names);
}


/*
 * Instance
 */
// Add prefixed parameter(s) from this instance
void Instance::addListParam(NamedList& list, unsigned int index)
{
    String prefix("instance.");
    prefix << index;
    list.addParam(prefix,c_str());
    addCaps(list,prefix + ".");
}

// Copy parameters to a list
void Instance::addCaps(NamedList& list, const String& prefix)
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
 * InstanceList
 */
// Insert an instance in the list
Instance* InstanceList::add(Instance* inst)
{
    if (!inst)
	return 0;
    ObjList* o = skipNull();
    for (; o; o = o->skipNext()) {
	Instance* tmp = static_cast<Instance*>(o->get());
	if (inst->m_priority > tmp->m_priority)
	    break;
    }
    if (o)
	o->insert(inst);
    else
	append(inst);
    XDebug(&__plugin,DebugAll,"InstanceList set '%s' prio=%u [%p]",
	inst->c_str(),inst->m_priority,this);
    return inst;
}

// Insert or set an existing instance
Instance* InstanceList::set(const String& name, int prio, bool* newInst)
{
    Instance* inst = 0;
    ObjList* o = find(name);
    if (newInst)
	*newInst = (o == 0);
    if (o) {
	inst = static_cast<Instance*>(o->get());
	// Re-insert if priority changed
	if (inst->m_priority != prio) {
	    o->remove(false);
	    inst->m_priority = prio;
	    add(inst);
	}
    }
    else
	inst = add(name,prio);
    return inst;
}

// Update capabilities for all instances with the given caps id
void InstanceList::updateCaps(const String& capsid, NamedList& list)
{
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Instance* i = static_cast<Instance*>(o->get());
	if (i->isCaps(capsid))
	    i->setCaps(capsid,list);
    }
}

// Add prefixed parameter(s) for all instances
// Return the number of instances added
unsigned int InstanceList::addListParam(NamedList& list, String* skip)
{
    unsigned int n = 0;
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Instance* tmp = static_cast<Instance*>(o->get());
	if (!skip || *skip != *tmp)
	    tmp->addListParam(list,++n);
    }
    return n;
}

// Notify all instances in the list
void InstanceList::notifyInstance(bool online, bool out, const String& from,
    const String& to, const String& inst, const char* data) const
{
    DDebug(&__plugin,DebugAll,
	"InstanceList::notifyInstance(%s,%s,%s,%s,%s,%p) count=%u [%p]",
	online ? "online" : "offline",out ? "from" : "to",
	from.c_str(),to.c_str(),inst.c_str(),data,count(),this);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Instance* tmp = static_cast<Instance*>(o->get());
	if (out)
	    __plugin.notify(online,from,to,*tmp,inst,data);
	else
	    __plugin.notify(online,from,to,inst,*tmp,data);
    }
}

// Notify all instances in the list with the same from/to.
// Notifications are made from/to the given instance to/from all other instances
void InstanceList::notifySkip(bool online, bool out, const String& notifier,
    const String& inst, const char* data) const
{
    DDebug(&__plugin,DebugAll,"InstanceList::notifySkip(%s,%s,%s,%s,%p) [%p]",
	online ? "online" : "offline",out ? "from" : "to",
	notifier.c_str(),inst.c_str(),data,this);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Instance* tmp = static_cast<Instance*>(o->get());
	if (*tmp != inst) {
	    if (out)
		__plugin.notify(online,notifier,notifier,*tmp,inst,data);
	    else
		__plugin.notify(online,notifier,notifier,inst,*tmp,data);
	}
    }
}

// Retrieve data and notify each instance in the list to a given one
void InstanceList::notifyUpdate(bool online, const String& from, const String& to,
    const String& inst) const
{
    DDebug(&__plugin,DebugAll,"InstanceList::notifyUpdate(%s,%s,%s,%s) [%p]",
	online ? "online" : "offline",from.c_str(),to.c_str(),inst.c_str(),this);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Instance* tmp = static_cast<Instance*>(o->get());
	Message* m = 0;
	const char* data = 0;
	if (online) {
	     m = __plugin.message("resource.notify");
	     m->addParam("operation","query");
	     m->addParam("contact",from);
	     m->addParam("instance",*tmp);
	     if (Engine::dispatch(m))
		data = m->getValue("data");
	}
	__plugin.notify(online,from,to,*tmp,inst,data);
	TelEngine::destruct(m);
    }
}

// Retrieve data and notify each instance in the list to to another list
void InstanceList::notifyUpdate(bool online, const String& from, const String& to,
    const InstanceList& dest) const
{
    DDebug(&__plugin,DebugAll,"InstanceList::notifyUpdate(%s,%s,%s) [%p]",
	online ? "online" : "offline",from.c_str(),to.c_str(),this);
    if (!dest.skipNull())
	return;
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Instance* tmp = static_cast<Instance*>(o->get());
	Message* m = 0;
	const char* data = 0;
	if (online) {
	     m = __plugin.message("resource.notify");
	     m->addParam("operation","query");
	     m->addParam("contact",from);
	     m->addParam("instance",*tmp);
	     if (Engine::dispatch(m))
		data = m->getValue("data");
	}
	dest.notifyInstance(online,false,from,to,*tmp,data);
	TelEngine::destruct(m);
    }
}


/*
 * Contact
 */
// Build a 'database' message used to update changes
Message* Contact::buildUpdateDb(const String& user, bool add)
{
    NamedList p("");
    p.addParam("username",user);
    p.addParam("contact",c_str());
    m_subscription.toParam(p);
    DDebug(&__plugin,DebugAll,"Contact::buildUpdateDb() user=%s %s contact=%s sub=%s",
	user.c_str(),add ? "adding" : "updating",c_str(),p.getValue("subscription"));
    return __plugin.buildDb(__plugin.m_account,__plugin.m_contactSubSetQuery,p);
}

// Set the contact from an array row
void Contact::set(Array& a, int row)
{
    int cols = a.getColumns();
    for (int col = 1; col < cols; col++) {
	String* s = YOBJECT(String,a.get(col,0));
	if (!s)
	    continue;
	if (*s == "subscription") {
	    String* sub = YOBJECT(String,a.get(col,row));
	    if (sub)
		m_subscription.replace(*sub);
	}
    }
}

// Set the contact from an array row
void Contact::set(String**& titles, ObjList**& data, int len)
{
    if (!(titles && data))
	return;
    for (int i = 1; i < len; i++) {
	String* title = titles[i];
	if (TelEngine::null(title))
	    continue;
	if (*title == YSTRING("subscription")) {
	    String* sub = YOBJECT(String,data[i] ? data[i]->get() : 0);
	    if (sub)
		m_subscription.replace(*sub);
	}
    }
}

// Build a contact from an array row
Contact* Contact::build(Array& a, int row)
{
    Contact* c = 0;
    int cols = a.getColumns();
    for (int col = 1; col < cols; col++) {
	String* s = YOBJECT(String,a.get(col,0));
	if (!s)
	    continue;
	if (*s == "contact") {
	    String* n = YOBJECT(String,a.get(col,row));
	    if (!TelEngine::null(n))
		c = new Contact(*n,String::empty());
	    break;
	}
    }
    if (c)
	c->set(a,row);
    return c;
}

// Build a contact from an array row
Contact* Contact::build(String**& titles, ObjList**& data, int len, int idCol)
{
    if (!(titles && data))
	return 0;
    String* id = YOBJECT(String,data[idCol] ? data[idCol]->get() : 0);
    if (TelEngine::null(id))
	return 0;
    Contact* c = new Contact(*id,String::empty());
    c->set(titles,data,len);
    return c;
}


/*
 * EventContact
 */
// Notify 'dialog'
void EventContact::notify(const Message& msg)
{
    Message* m = __plugin.message("resource.notify");
    m->copyParams(*this);
    m->setParam("notifyseq",String(getSeq()));
    m->setParam("subscriptionstate","active");
    m->setParam("remaining",String((unsigned int)(getTimeLeft() / 1000)));
    String mType;
    if (msg == "call.cdr")
	mType = "cdr";
    if (mType) {
	m->setParam(mType,String::boolText(true));
	mType << ".";
	unsigned int n = msg.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = msg.getParam(i);
	    if (ns && ns->name())
		m->addParam(mType + ns->name(),*ns);
	}
    }
    Engine::enqueue(m);
}

// Notify MWI
void EventContact::notifyMwi(const Message& msg)
{
    Message* m = __plugin.message("resource.notify");
    m->copyParams(*this);
    if (msg == "mwi" || msg == "mwi.query")
	m->copyParams(msg);
    else {
	m->addParam("message-summary.voicenew","0");
	m->addParam("message-summary.voiceold","0");
    }
    Engine::enqueue(m);
}

// Notify subscription termination
void EventContact::notifyTerminate(const char* reason)
{
    Message* m = __plugin.message("resource.notify");
    m->copyParams(*this);
    m->addParam("subscriptionstate","terminated");
    m->addParam("terminatereason",reason,false);
    Engine::enqueue(m);
}

/*
 * User
 */
User::User(const char* name)
    : Mutex(true,__plugin.name() + ":User"), m_user(name)
{

}

User::~User()
{
    m_list.clear();
    m_user.clear();
}

void User::destroyed()
{
    m_list.clear();
    RefObject::destroyed();
}

/*
 * PresenceUser
 */
PresenceUser::PresenceUser(const char* name)
    : User(name)
{
    DDebug(&__plugin,DebugAll,"PresenceUser::PresenceUser(%s) [%p]",name,this);
}

PresenceUser::~PresenceUser()
{
    DDebug(&__plugin,DebugAll,"PresenceUser::~PresenceUser(%s) [%p]",user().c_str(),this);
    m_list.clear();
}

void PresenceUser::notify(const Message& msg)
{
    Lock lock(this);
    ObjList* o = m_list.skipNull();
    for (; o; o = o->skipNext()) {
	Contact* c = static_cast<Contact*>(o->get());
	if (!c->m_subscription.from())
	    continue;
	if (!c->m_instances.skipNull()) {
	    DDebug(&__plugin,DebugAll,"PresenceUser(%s) no instances for contact %s [%p]",
		user().c_str(),c->c_str(),this);
	    continue;
	}
	DDebug(&__plugin,DebugAll,"PresenceUser(%s) notifying contact %s [%p]",
	    user().c_str(),c->c_str(),this);
	String* oper = msg.getParam("operation");
	bool online = !oper || *oper != "finalize";
	c->m_instances.notifyInstance(online,false,user(),*c,msg.getValue("callid"),0);
    }

}

// Add a contact
void PresenceUser::appendContact(Contact* c)
{
    if (!c)
	return;
    Lock lock(this);
    m_list.append(c);
#ifdef DEBUG
    String sub;
    c->m_subscription.toString(sub);
    DDebug(&__plugin,DebugAll,"PresenceUser(%s) added contact (%p,%s) subscription=%s [%p]",
	user().c_str(),c,c->c_str(),sub.c_str(),this);
#endif
}

// Remove a contact. Return it if found and not deleted
Contact* PresenceUser::removeContact(const String& name, bool delObj)
{
    ObjList* o = m_list.find(name);
    if (!o)
	return 0;
    Contact* c = static_cast<Contact*>(o->get());
#ifdef DEBUG
    String sub;
    c->m_subscription.toString(sub);
    DDebug(&__plugin,DebugAll,"PresenceUser(%s) removed contact (%p,%s) subscription=%s [%p]",
	user().c_str(),c,c->c_str(),sub.c_str(),this);
#endif
    o->remove(delObj);
    return delObj ? 0 : c;
}

// Add or remove directed presence. Remove all instances if instance is empty.
// Update it for all targets if contact is empty
void PresenceUser::updateDirectNotify(bool online, const String& instance,
    const String& target, const String& targetInst)
{
    if (!instance) {
	if (!online) {
	    DDebug(&__plugin,DebugAll,
		"PresenceUser(%s) removing all directed notifications [%p]",
		user().c_str(),this);
	    m_directNotify.clear();
	}
	return;
    }
    if (online && !target)
	return;
    NamedList* p = findDirectNotify(instance,online);
    if (!p)
	return;
    if (online) {
	// Add the target if not found. Keep empty target instances
	// Do nothing if the target instance is already in the list
	// Don't use NamedList::setParam(): this will update the first found target
	if (getParam(*p,target,targetInst))
	    return;
	p->addParam(target,targetInst);
	DDebug(&__plugin,DebugAll,
	    "PresenceUser(%s) added directed notifications inst=%s target=(%s,%s) [%p]",
	    user().c_str(),instance.c_str(),target.c_str(),targetInst.c_str(),this);
	return;
    }
    DDebug(&__plugin,DebugAll,
	"PresenceUser(%s) removing directed notification inst=%s target=(%s,%s) [%p]",
	user().c_str(),instance.c_str(),target.c_str(),targetInst.c_str(),this);
    if (!target) {
	m_directNotify.remove(p);
	return;
    }
    if (targetInst) {
	NamedString* ns = getParam(*p,target,targetInst);
	if (!ns)
	    return;
	p->clearParam(ns);
    }
    else
	p->clearParam(target);
    // Remove empty list
    if (!p->count())
	m_directNotify.remove(p);
}

// Notify offline for sent directed presence
// Remove instance from list or clear the list
void PresenceUser::directNotifyOffline(const String& instance, const char* data)
{
    DDebug(&__plugin,DebugAll,"PresenceUser(%s) directNotifyOffline(%s) [%p]",
	    user().c_str(),instance.c_str(),this);
    for (ObjList* o = m_directNotify.skipNull(); o; o = o->skipNext()) {
	NamedList* p = static_cast<NamedList*>(o->get());
	if (instance && *p != instance)
	    continue;
	unsigned int n = p->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = p->getParam(i);
	    if (ns && ns->name() && !isSubFrom(ns->name()))
		__plugin.notify(false,toString(),ns->name(),instance,*ns,data);
	}
    }
    updateDirectNotify(false,instance);
}

// Retrieve a directed notify instance. Create it if not found and requested
NamedList* PresenceUser::findDirectNotify(const String& instance, bool create)
{
    if (!instance)
	return 0;
    for (ObjList* o = m_directNotify.skipNull(); o; o = o->skipNext()) {
	NamedList* p = static_cast<NamedList*>(o->get());
	if (instance == *p)
	    return p;
    }
    if (!create)
	return 0;
    NamedList* p = new NamedList(instance);
    m_directNotify.append(p);
    return p;
}

/*
 * EventUser
 */
EventUser::EventUser(const char* name)
    : User(name)
{
    DDebug(&__plugin,DebugAll,"EventUser::EventUser(%s) [%p]",name,this);
}

EventUser::~EventUser()
{
    DDebug(&__plugin,DebugAll,"PresenceUser::~PresenceUser(%s) [%p]",user().c_str(),this);
    m_list.clear();
}

// Add a contact
void EventUser::appendContact(EventContact* c)
{
    if (!c)
	return;
    Lock lock(this);
    ObjList* o = m_list.find(c->toString());
    if (o)
	o->set(c);
    else
	m_list.append(c);
    DDebug(&__plugin,DebugAll,"EventUser(%s) added contact (%p,%s) [%p]",
	user().c_str(),c,c->c_str(),this);
}

// Remove a contact. Return it if found and not deleted
EventContact* EventUser::removeContact(const String& name, bool delObj)
{
    Lock lock(this);
    ObjList* o = m_list.find(name);
    if (!o)
	return 0;
    EventContact* c = static_cast<EventContact*>(o->get());
    DDebug(&__plugin,DebugAll,"EventUser(%s) removed contact (%p,%s) [%p]",
	user().c_str(),c,c->c_str(),this);
    o->remove(delObj);
    return delObj ? 0 : c;
}

void EventUser::notify(const Message& msg)
{
    Lock lock(this);
    for (ObjList* o = m_list.skipNull(); o; o = o->skipNext()) {
	EventContact* c = static_cast<EventContact*>(o->get());
	if (!c)
	    continue;
	const String& notif = msg["caller"];
	if (notif == *c)
	    continue;
	DDebug(&__plugin,DebugAll,"EventUser(%s) notifying 'dialog' to '%s' [%p]",
	    toString().c_str(),c->toString().c_str(),this);
	c->notify(msg);
    }
}

void EventUser::notifyMwi(const Message& msg)
{
    Lock lock(this);
    for (ObjList* o = m_list.skipNull(); o; o = o->skipNext()) {
	EventContact* c = static_cast<EventContact*>(o->get());
	if (!c)
	    continue;
	DDebug(&__plugin,DebugAll,"EventUser(%s) notifying 'mwi' to '%s' [%p]",
	    toString().c_str(),c->toString().c_str(),this);
	c->notifyMwi(msg);
    }
}

void EventUser::expire(u_int64_t time, const char* event)
{
    Lock lock(this);
    ObjList* o = m_list.skipNull();
    while (o) {
	EventContact* c = static_cast<EventContact*>(o->get());
	if (!c->hasExpired(time)) {
	    o = o->skipNext();
	    continue;
	}
	Debug(&__plugin,DebugInfo,
	    "EventUser(%s) subscription of '%s' for event '%s' timed out [%p]",
	    toString().c_str(),c->toString().c_str(),event,this);
	c->notifyTerminate("timeout");
	o->remove();
	o = o->skipNull();
    }
}

/*
 * ExpireThread
 */
ExpireThread::ExpireThread(Priority prio)
    : Thread("ExpireThread", prio)
{
    XDebug(&__plugin,DebugAll,"ExpireThread created [%p]",this);
    Lock lock(__plugin);
    __plugin.m_expire = this;
}

ExpireThread::~ExpireThread()
{
    XDebug(&__plugin,DebugAll,"ExpireThread destroyed [%p]",this);
    Lock lock(__plugin);
    if (__plugin.m_expire) {
	__plugin.m_expire = 0;
	lock.drop();
	Debug(&__plugin,DebugWarn,"ExpireThread abnormally terminated [%p]",this);
    }
}

void ExpireThread::run()
{
    DDebug(&__plugin,DebugAll,"%s start running [%p]",currentName(),this);
    while (!Engine::exiting()) {
	if (s_check) {
	    __plugin.expireSubscriptions();
	    s_check = false;
	}
	Thread::idle(false);
	if (Thread::check(false))
	    break;
    }
    Lock lock(__plugin);
    __plugin.m_expire = 0;
}



/*
 * UserList
 */
UserList::UserList()
    : Mutex(true,__plugin.name() + ":UserList")
{
}

// Find an user. Load it from database if not found
// Returns referrenced pointer if found
PresenceUser* UserList::getUser(const String& user, bool load, bool force)
{
    XDebug(&__plugin,DebugAll,"UserList::getUser(%s)",user.c_str());
    Lock lock(this);
    ObjList* o = m_users.find(user);
    if (o) {
	PresenceUser* u = static_cast<PresenceUser*>(o->get());
	return u->ref() ? u : 0;
    }
    lock.drop();
    if ((s_usersLoaded || !load) && !force)
	return 0;
    PresenceUser* u = askDatabase(user);
    if (!u)
	return 0;
    // Check if the user was already added while unlocked
    Lock lock2(this);
    ObjList* tmp = m_users.find(user);
    if (!tmp)
	m_users.append(u);
    else {
	TelEngine::destruct(u);
	u = static_cast<PresenceUser*>(tmp->get());
    }
    return u->ref() ? u : 0;
}

// Remove an user from list
void UserList::removeUser(const String& user)
{
    Lock lock(this);
    ObjList* o = m_users.find(user);
    if (!o)
	return;
#ifdef DEBUG
    PresenceUser* u = static_cast<PresenceUser*>(o->get());
    Debug(&__plugin,DebugAll,"UserList::removeUser() %p '%s'",u,user.c_str());
#endif
    o->remove();
}

// Load an user from database. Build an PresenceUser and returns it if found
PresenceUser* UserList::askDatabase(const String& name)
{
    if (!name)
	return 0;
    NamedList p("");
    p.addParam("username",name);
    Message* m = __plugin.buildDb(__plugin.m_account,__plugin.m_userLoadQuery,p);
    m = __plugin.queryDb(m);
    if (!m)
	return 0;
#ifdef DEBUG
    u_int64_t start = Time::now();
#endif
    PresenceUser* u = 0;
    Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
    int rows = 0;
    int cols = 0;
    ObjList** columns = 0;
    String** titles = 0;
    if (arrayData(a,rows,cols,columns,titles) && rows > 1) {
	int cntCol = strIndex(titles,cols,YSTRING("username"));
	String* usr = (cntCol >= 0) ? YOBJECT(String,a->get(cntCol,1)) : 0;
	if (!TelEngine::null(usr) && *usr == name)
	    u = new PresenceUser(name);
	else if (TelEngine::null(usr))
	    XDebug(&__plugin,DebugAll,"User '%s' not found in database",name.c_str());
	else
	    Debug(&__plugin,DebugNote,"Database query returned user='%s' for '%s'",
		usr->c_str(),name.c_str());
    }
    if (u) {
	int cntCol = strIndex(titles,cols,YSTRING("contact"));
	if (cntCol < 0)
	    rows = 0;
	for (int i = 1; i < rows; i++) {
	    advanceObjLists(columns,cols);
	    Contact* c = Contact::build(titles,columns,cols,cntCol);
	    if (c)
		u->appendContact(c);
	}
    }
    clearArrayData(columns,titles);
#ifdef DEBUG
    if (u)
	Debug(&__plugin,DebugAll,"Loaded user '%s' contacts=%u in %u ms",
	    name.c_str(),u->m_list.count(),ellapsedMs(start));
#endif
    TelEngine::destruct(m);
    return u;
}


/*
 * GenericUser
 */
GenericUser::GenericUser(const char* regexp)
    : Mutex(true,__plugin.name() + ":GenericUser"),
    m_user(regexp)
{
    DDebug(&__plugin,DebugAll,"GenericUser(%s) [%p]",regexp,this);
}

GenericUser::~GenericUser()
{
    DDebug(&__plugin,DebugAll,"GenericUser(%s) destroyed [%p]",m_user.c_str(),this);
    m_list.clear();
}

// Find a contact matching the given string
GenericContact* GenericUser::find(const String& contact)
{
    for (ObjList* o = m_list.skipNull(); o; o = o->skipNext()) {
	GenericContact* c = static_cast<GenericContact*>(o->get());
	if (c->matches(contact.c_str()))
	    return c;
    }
    return 0;
}

void GenericUser::destroyed()
{
    m_list.clear();
    RefObject::destroyed();
}


/*
 * GenericUserList
 */
GenericUserList::GenericUserList()
    : Mutex(true,__plugin.name() + ":GenericUserList")
{
}

// (Re)Load from database
void GenericUserList::load()
{
    DDebug(&__plugin,DebugAll,"Loading generic users");
    Message* m = __plugin.buildDb(__plugin.m_account,__plugin.m_genericUserLoadQuery,NamedList::empty());
    m = __plugin.queryDb(m);
    Lock lock(this);
    clear();
    if (!m)
	return;
    Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
    if (!a) {
	TelEngine::destruct(m);
	return;
    }
    int rows = a->getRows();
    int cols = a->getColumns();
    for (int i = 1; i < rows; i++) {
	String* user = 0;
	String* contact = 0;
	// Get username
	for (int j = 0; j < cols; j++) {
	    String* tmp = YOBJECT(String,a->get(j,0));
	    if (!tmp)
		continue;
	    if (*tmp == "username")
		user = YOBJECT(String,a->get(j,i));
	    else if (*tmp == "contact")
		contact = YOBJECT(String,a->get(j,i));
	}
	if (!(user && contact))
	    continue;
	GenericContact* c = new GenericContact(*contact);
	if (!c->compile()) {
	    Debug(&__plugin,DebugNote,"Invalid generic contact regexp '%s' for user=%s",
		contact->c_str(),user->c_str());
	    TelEngine::destruct(c);
	    continue;
	}
	GenericUser* u = 0;
	ObjList* o = find(*user);
	if (o)
	    u = static_cast<GenericUser*>(o->get());
	else {
	    u = new GenericUser(*user);
	    if (u->compile())
		append(u);
	    else {
		Debug(&__plugin,DebugNote,"Invalid generic user regexp '%s'",user->c_str());
		TelEngine::destruct(c);
		TelEngine::destruct(u);
	    }
	}
	if (u) {
	    u->lock();
	    u->m_list.append(c);
	    u->unlock();
	    DDebug(&__plugin,DebugAll,"Added generic user='%s' contact='%s'",
		user->c_str(),contact->c_str());
	}
    }
    TelEngine::destruct(m);
}

// Find an user matching the given string
// Returns referenced pointer
GenericUser* GenericUserList::findUser(const String& user)
{
    Lock lock(this);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	GenericUser* u = static_cast<GenericUser*>(o->get());
	if (u->matches(user))
	    return u->ref() ? u : 0;
    }
    return 0;
}


/*
 * SubMessageHandler
 */
SubMessageHandler::SubMessageHandler(int handler, int prio)
    : MessageHandler(lookup(handler,s_msgHandler),prio,__plugin.name()),
      m_handler(handler)
{
}

bool SubMessageHandler::received(Message& msg)
{
    if (m_handler == ResNotify) {
	if (__plugin.isModule(msg) || msg.getParam("event"))
	    return false;
	String* oper = msg.getParam("operation");
	if (TelEngine::null(oper))
	    return false;
	// online/offline
	bool online = (*oper == "update" || *oper == "online");
	if (online  || *oper == "delete" || *oper == "offline")
	    return __plugin.handleResNotify(online,msg);
	if (*oper == "updatecaps") {
	    String* capsid = msg.getParam("caps.id");
	    if (!TelEngine::null(capsid))
		__plugin.updateCaps(*capsid,msg);
	    return false;
	}
	String* src = msg.getParam("from");
	String* dest = msg.getParam("to");
	if (TelEngine::null(src) || TelEngine::null(dest))
	    return false;
	// (un)subscribed
	bool sub = (*oper == "subscribed");
	if (sub || *oper == "unsubscribed")
	    return __plugin.handleResNotifySub(sub,*src,*dest,msg);
	// probe
	if (*oper == "probe")
	    return __plugin.handleResNotifyProbe(*src,*dest,msg);
	return false;
    }
    if (m_handler == ResSubscribe) {
	if (__plugin.isModule(msg))
	    return false;
	String* oper = msg.getParam("operation");
	String* notifier = msg.getParam("notifier");
	String* subscriber = msg.getParam("subscriber");
	if (TelEngine::null(oper) || TelEngine::null(subscriber) || TelEngine::null(notifier))
	    return false;
	String* event = msg.getParam("event");
	if (event) {
	    if (!__plugin.m_userEventQuery)
		return false;
	    return __plugin.handleResSubscribe(*event,*subscriber,*notifier,*oper,msg);
	}
	bool sub = (*oper == "subscribe");
	if (sub || *oper == "unsubscribe")
	    return __plugin.handleResSubscribe(sub,*subscriber,*notifier,msg);
	if (*oper == "query")
	    return __plugin.handleResSubscribeQuery(*subscriber,*notifier,msg);
	return false;
    }
    if (m_handler == UserRoster) {
	if (__plugin.isModule(msg))
	    return false;
	XDebug(&__plugin,DebugAll,"%s oper='%s' user='%s' contact='%s'",
	    msg.c_str(),msg.getValue("operation"),msg.getValue("username"),
	    msg.getValue("contact"));
	String* oper = msg.getParam("operation");
	if (TelEngine::null(oper))
	    return false;
	String* user = msg.getParam("username");
	String* contact = msg.getParam("contact");
	if (TelEngine::null(user))
	    return false;
	if (*oper == "query")
	    return __plugin.handleUserRosterQuery(*user,contact,msg);
	if (TelEngine::null(contact))
	    return false;
	if (*oper == "update")
	    return __plugin.handleUserRosterUpdate(*user,*contact,msg);
	if (*oper == "delete")
	    return __plugin.handleUserRosterDelete(*user,*contact,msg);
	return false;
    }
    if (m_handler == UserUpdate) {
	String* notif = msg.getParam("notify");
	if (TelEngine::null(notif))
	    return false;
	String* user = msg.getParam("user");
	if (TelEngine::null(user))
	    return false;
	if (*notif == "delete")
	    __plugin.handleUserUpdateDelete(*user,msg);
	else if (s_usersLoaded && *notif == "add")
	    TelEngine::destruct(__plugin.m_users.getUser(*user,true,true));
	return false;
    }
    if (m_handler == EngineStart) {
	Configuration cfg(Engine::configFile("subscription"));
	const char* loadAll = cfg.getValue("general","user_roster_load_all");
	if (!TelEngine::null(loadAll)) {
	    s_usersLoaded = true;
	    XDebug(&__plugin,DebugAll,"Loading all users");
	    NamedList p("");
	    Message* m = __plugin.buildDb(__plugin.m_account,loadAll,p);
	    m = __plugin.queryDb(m);
	    if (m) {
		u_int64_t start = Time::now();
		unsigned int n = 0;
		unsigned int nc = 0;
		Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
		ObjList** columns = 0;
		String** titles = 0;
		int rows = 0;
		int cols = 0;
		if (arrayData(a,rows,cols,columns,titles)) {
		    int usrCol = strIndex(titles,cols,YSTRING("username"));
		    int cntCol = strIndex(titles,cols,YSTRING("contact"));
		    __plugin.m_users.lock();
		    for (int i = 1; i < rows; i++) {
			advanceObjLists(columns,cols);
			String* s = 0;
			if (usrCol >= 0 && columns[usrCol])
			    s = YOBJECT(String,columns[usrCol]->get());
			if (!s)
			    continue;
			PresenceUser* u =  __plugin.m_users.getUser(*s,false);
			if (!u) {
			    n++;
			    u = new PresenceUser(*s);
			    __plugin.m_users.users().append(u);
			    u->ref();
			}
			if (cntCol >= 0) {
			    Contact* c = Contact::build(titles,columns,cols,cntCol);
			    if (c)
				u->appendContact(c);
			}
			TelEngine::destruct(u);
			nc++;
		    }
		    __plugin.m_users.unlock();
		}
		clearArrayData(columns,titles);
		TelEngine::destruct(m);
		Debug(&__plugin,DebugAll,"Loaded %u users and %u contacts in %u ms",
		    n,nc,ellapsedMs(start));
	    }
	    else
		Alarm(&__plugin,"database",DebugMild,"Failed to load users");
	}
	__plugin.m_genericUsers.load();
	return false;
    }
    if (m_handler == CallCdr) {
	String* notif = msg.getParam("external");
	if (TelEngine::null(notif))
	    return false;
	__plugin.handleCallCdr(msg,*notif);
	return false;
    }
    if (m_handler == Mwi) {
	String* oper = msg.getParam("operation");
	if (!oper || *oper != "notify")
	    return false;
	__plugin.handleMwi(msg);
	return true;
    }

    Debug(&__plugin,DebugStub,"SubMessageHandler(%s) not handled!",msg.c_str());
    return false;
}


/*
 * SubscriptionModule Module
 */
SubscriptionModule::SubscriptionModule()
    : Module("subscription","misc",true), m_eventsMutex(true,"subscription:events")
{
    Output("Loaded module Subscriptions");
}

SubscriptionModule::~SubscriptionModule()
{
    Output("Unloading module Subscriptions");
}

void SubscriptionModule::initialize()
{
    Output("Initializing module Subscriptions");
    Configuration cfg(Engine::configFile("subscription"));
    if (m_handlers.skipNull()) {
	// Reload generic users (wait engine.start for the first load)
	m_genericUsers.load();
    }
    else {
	m_account = cfg.getValue("general","account");
	m_userLoadQuery = cfg.getValue("general","user_roster_load");
	m_userEventQuery = cfg.getValue("general","user_event_auth");
	m_userDeleteQuery = cfg.getValue("general","user_roster_delete");
	m_contactLoadQuery = cfg.getValue("general","contact_load");
	m_contactSubSetQuery = cfg.getValue("general","contact_subscription_set");
	m_contactSetQuery = cfg.getValue("general","contact_set");
	m_contactSetFullQuery = cfg.getValue("general","contact_set_full");
	m_contactDeleteQuery = cfg.getValue("general","contact_delete");
	m_genericUserLoadQuery = cfg.getValue("general","generic_roster_load");

	if (m_userEventQuery)
	    (new ExpireThread())->startup();

	// Install relays
	setup();
	installRelay(Halt);
	installRelay(Route,cfg.getIntValue("priorities","call.route",100));
	// Install handlers
	for (const TokenDict* d = s_msgHandler; d->token; d++) {
	    if (d->value == SubMessageHandler::CallCdr && !m_userEventQuery)
		continue;
	    SubMessageHandler* h = new SubMessageHandler(d->value);
	    Engine::install(h);
	    m_handlers.append(h);
	}
    }
    Lock lck(this);
    m_routeCallto = cfg.getValue("general","route_callto","jabber/${called}");
    if (!m_routeCallto)
	Debug(this,DebugConf,"Empty 'route_callto' in config");
}

// Enqueue a resource.notify for a given instance
// data: optional data used to override instance's data
void SubscriptionModule::notify(bool online, const String& from, const String& to,
    const String& fromInst, const String& toInst, const char* data, bool sync)
{
    const char* what = online ? "online" : "offline";
    Debug(this,DebugAll,"notify=%s notifier=%s (%s) subscriber=%s (%s)",
	what,from.c_str(),fromInst.c_str(),to.c_str(),toInst.c_str());
    Message* m = message("resource.notify");
    m->addParam("operation",what);
    m->addParam("from",from);
    m->addParam("to",to);
    if (fromInst)
	m->addParam("from_instance",fromInst);
    if (toInst)
	m->addParam("to_instance",toInst);
    if (!TelEngine::null(data))
	m->addParam("data",data);
    if (!sync)
	Engine::enqueue(m);
    else {
	Engine::dispatch(m);
	TelEngine::destruct(m);
    }
}

// Notify (un)subscribed
void SubscriptionModule::subscribed(bool sub, const String& from, const String& to)
{
    Debug(this,DebugAll,"subscribed(%s) from=%s to=%s",
	String::boolText(sub),from.c_str(),to.c_str());
    Message* m = message("resource.notify");
    m->addParam("operation",sub ? "subscribed" : "unsubscribed");
    m->addParam("from",from);
    m->addParam("to",to);
    Engine::enqueue(m);
}

// Enqueue a resource.subscribe
void SubscriptionModule::subscribe(bool sub, const String& from, const String& to,
    const String* instance)
{
    const char* what = sub ? "subscribe" : "unsubscribe";
    Debug(this,DebugAll,"Requesting %s subscriber=%s notifier=%s",
	what,from.c_str(),to.c_str());
    Message* m = message("resource.subscribe");
    m->addParam("operation",what);
    m->addParam("subscriber",from);
    m->addParam("notifier",to);
    if (!TelEngine::null(instance))
	m->addParam("instance",*instance);
    Engine::enqueue(m);
}

// Enqueue a resource.notify with operation=probe
void SubscriptionModule::probe(const char* from, const char* to)
{
    Message* m = message("resource.notify");
    m->addParam("operation","probe");
    m->addParam("from",from);
    m->addParam("to",to);
    Engine::enqueue(m);
}

// Dispatch a user.roster message with operation 'update'
// Load contact data from database
// Return the database result if requested
Array* SubscriptionModule::notifyRosterUpdate(const char* username, const char* contact,
    bool retData, bool sync)
{
    NamedList p("");
    p.addParam("username",username);
    p.addParam("contact",contact);
    Message* m = buildDb(m_account,m_contactLoadQuery,p);
    m = queryDb(m);
    Array* data = 0;
    if (m && m->getIntValue("rows") >= 1) {
	data = static_cast<Array*>(m->userObject(YATOM("Array")));
	if (data && data->ref())
	    m->userData(0);
	else
	    data = 0;
    }
    TelEngine::destruct(m);
    if (!data)
	return 0;

    Message* mu = message("user.roster");
    mu->addParam("notify","update");
    mu->addParam("username",username);
    mu->addParam("contact.count","1");
    String prefix("contact.1");
    mu->addParam(prefix,contact);
    prefix << ".";
    // Add contact data
    int cols = data->getColumns();
    for (int col = 1; col < cols; col++) {
	String* name = YOBJECT(String,data->get(col,0));
	if (TelEngine::null(name) || *name == "username" || *name == "contact")
	    continue;
	String* value = YOBJECT(String,data->get(col,1));
	if (!value)
	    continue;
	mu->addParam(prefix + *name,*value);
    }
    if (sync) {
	Engine::dispatch(mu);
	TelEngine::destruct(mu);
    }
    else
	Engine::enqueue(mu);

    if (!retData)
	TelEngine::destruct(data);
    return data;
}

// Handle 'resource.subscribe' for messages with event
bool SubscriptionModule::handleResSubscribe(const String& event, const String& subscriber,
    const String& notifier, const String& oper, Message& msg)
{
    DDebug(this,DebugAll,"handleResSubscribe(%s,%s,%s,%s)",
	event.c_str(),subscriber.c_str(),notifier.c_str(),oper.c_str());
    if (oper != "subscribe")
	return removeEventUserContact(notifier,subscriber,event);
    if (!askDB(msg)) {
	// Remove subscriber if no longer allowed
	removeEventUserContact(notifier,subscriber,event);
	return false;
    }
    EventUser* user = getEventUser(true,notifier,event);
    if (!user)
	return false;
    Message m("cdr.query");
    if (event == "dialog")
	m.addParam("external",notifier);
    else {
	m = "mwi.query";
	m.addParam("subscriber",subscriber);
	m.addParam("notifier",notifier);
    }
    user->lock();
    EventContact* c = new EventContact(subscriber,msg);
    user->appendContact(c);
    if (Engine::dispatch(m))
	event == "dialog" ? c->notify(m) : c->notifyMwi(m);
    else
	event == "dialog" ? c->notify(msg) : c->notifyMwi(msg);
    user->unlock();
    TelEngine::destruct(user);
    return true;
}

// Retrieve an event notifier
// Valid objects are returned with reference counter increased
EventUser* SubscriptionModule::getEventUser(bool create, const String& notifier,
    const String& event)
{
    Lock lock(m_eventsMutex);
    ObjList* o = m_events.find(event);
    if (!o && create) {
	o = m_events.append(new NamedList(event));
	XDebug(this,DebugAll,"Added list for event '%s'",event.c_str());
    }
    if (!o)
	return 0;
    NamedList* evList = static_cast<NamedList*>(o->get());
    // Find Notifier list
    NamedString* ns = evList->getParam(notifier);
    NamedPointer* np = static_cast<NamedPointer*>(ns);
    if (!np) {
	if (!create)
	    return 0;
	np = new NamedPointer(notifier,new EventUser(notifier));
	DDebug(this,DebugAll,"Adding user '%s' event '%s'",notifier.c_str(),event.c_str());
	evList->addParam(np);
    }
    EventUser* user = static_cast<EventUser*>(np->userData());
    return (user && user->ref()) ? user : 0;
}

// Remove an event's user contact
// Remove the user if empty
bool SubscriptionModule::removeEventUserContact(const String& user, const String& contact,
    const String& event)
{
    Lock lock(m_eventsMutex);
    ObjList* o = m_events.find(event);
    if (!o)
	return false;
    NamedList* evList = static_cast<NamedList*>(o->get());
    NamedString* ns = evList->getParam(user);
    if (!ns)
	return false;
    NamedPointer* np = static_cast<NamedPointer*>(ns);
    EventUser* u = static_cast<EventUser*>(np->userData());
    if (!u)
	return false;
    EventContact* c = u->removeContact(contact,false);
    if (!c)
	return false;
    TelEngine::destruct(c);
    if (!u->m_list.skipNull()) {
	DDebug(this,DebugAll,"Removing empty user '%s' event '%s'",user.c_str(),event.c_str());
	evList->clearParam(ns);
	// Remove empty list also
	if (!evList->count()) {
	    DDebug(this,DebugAll,"Removing empty event list '%s'",evList->c_str());
	    o->remove();
	}
    }
    return true;
}

// Query database for event subscription authorization
bool SubscriptionModule::askDB(const NamedList& params)
{
    Message* m = buildDb(m_account,m_userEventQuery,params);
    if (!m)
	return false;
    m = queryDb(m);
    if (!m)
	return false;
    // Fail it if there is no record in database
    bool ok = (m->getIntValue("rows") > 0);
    TelEngine::destruct(m);
    return ok;
}

void SubscriptionModule::handleCallCdr(const Message& msg, const String& notif)
{
    DDebug(this,DebugAll,"handleCallCdr() notifier=%s",notif.c_str());
    EventUser* user = getEventUser(false,notif,"dialog");
    if (user) {
	user->notify(msg);
	TelEngine::destruct(user);
    }
    PresenceUser* pu = 0;
    m_users.lock();
    for (ObjList* o = m_users.users().skipNull(); o; o = o->skipNext()) {
	pu = static_cast<PresenceUser*>(o->get());
	if (pu->user().substr(0,pu->user().find("@")) == notif) {
	    pu->ref();
	    break;
	}
	pu = 0;
    }
    m_users.unlock();
    if (!pu)
	return;
    pu->notify(msg);
    TelEngine::destruct(pu);
}

void SubscriptionModule::handleMwi(const Message& msg)
{
    EventUser* user = getEventUser(false,msg.getValue("notifier"),"message-summary");
    if (user)
	user->notifyMwi(msg);
    TelEngine::destruct(user);
}

// Handle 'resource.subscribe' messages with (un)subscribe operation
bool SubscriptionModule::handleResSubscribe(bool sub, const String& subscriber,
    const String& notifier, Message& msg)
{
    DDebug(this,DebugAll,"handleResSubscribe(%s) subscriber=%s notifier=%s",
	String::boolText(sub),subscriber.c_str(),notifier.c_str());
    // Check if the subscriber and/or notifier are in the list (our server)
    PresenceUser* from = m_users.getUser(subscriber);
    PresenceUser* to = m_users.getUser(notifier);
    bool rsp = false;

    // Process the subscriber's state. Use a while() to break
    while (from) {
	Lock lock(from);
	Contact* c = from->findContact(notifier);
	Message* m = 0;
	bool newContact = (c == 0);
	if (c) {
	    if (sub) {
		// Subscription request
		// Not subscribed: remember pending out request
		// Subscribed: reset pending out flag if set
		if (c->m_subscription.to() == c->m_subscription.pendingOut()) {
		    if (!c->m_subscription.to())
			c->m_subscription.set(SubscriptionState::PendingOut);
		    else
			c->m_subscription.reset(SubscriptionState::PendingOut);
		    m = c->buildUpdateDb(subscriber);
		}
	    }
	    else {
		// Subscription termination request
		bool changed = c->m_subscription.to() || c->m_subscription.pendingOut();
		// Make sure the 'To' and 'PendingOut' are not set
		c->m_subscription.reset(SubscriptionState::To | SubscriptionState::PendingOut);
		if (changed)
		    m = c->buildUpdateDb(subscriber);
	    }
	}
	else {
	    if (sub) {
		// Add 'notifier' to the contact list if subscription is requested
		// TODO: Check credentials
		c = new Contact(notifier,SubscriptionState::PendingOut);
		m = c->buildUpdateDb(subscriber,true);
	    }
	    if (!c)
		break;
	}
	lock.drop();
	if (m)
	    m = queryDb(m);
	if (m) {
	    bool ok = true;
	    if (newContact) {
		// Append the new contact. Check if not already added while not locked
		Lock lck(from);
		ok = !from->findContact(notifier);
		if (ok)
		    from->appendContact(c);
		else
		    TelEngine::destruct(c);
	    }
	    // Notify changes
	    if (ok)
		notifyRosterUpdate(subscriber,notifier);
	    TelEngine::destruct(m);
	}
	break;
    }
    // Process the notifier's state. Use a while() to break
    while (to) {
	Lock lock(to);
	Contact* c = to->findContact(subscriber);
	if (!c)
	    break;
	Message* m = 0;
	bool unsubscribed = !sub && c->m_subscription.from();
	rsp = !sub || c->m_subscription.from();
	if (sub) {
	    // Subscription request
	    // Not subscribed: remember pending in request
	    // Subscribed: reset pending in flag if set
	    if (c->m_subscription.from() == c->m_subscription.pendingIn()) {
		if (!c->m_subscription.from())
		    c->m_subscription.set(SubscriptionState::PendingIn);
		else
		    c->m_subscription.reset(SubscriptionState::PendingIn);
		m = c->buildUpdateDb(notifier);
	    }
	}
	else {
	    if (c->m_subscription.from() || c->m_subscription.pendingIn()) {
		c->m_subscription.reset(SubscriptionState::From | SubscriptionState::PendingIn);
		m = c->buildUpdateDb(notifier);
	    }
	}
	lock.drop();
	if (m)
	    TelEngine::destruct(queryDb(m));
	// Notify subscription change and 'offline'
	if (unsubscribed) {
	    notify(false,notifier,subscriber);
	    notifyRosterUpdate(notifier,subscriber);
	}
	// Respond on behalf of the notifier
	if (rsp) {
	    // Internally handle the message before sending it if the destination was found
	    // (update destination data)
	    if (from) {
		Message tmp("resource.notify");
		handleResNotifySub(sub,notifier,subscriber,tmp);
	    }
	    subscribed(sub,notifier,subscriber);
	}
	break;
    }
    TelEngine::destruct(from);
    TelEngine::destruct(to);
    return rsp;
}

// Handle 'resource.subscribe' messages with query operation
bool SubscriptionModule::handleResSubscribeQuery(const String& subscriber,
    const String& notifier, Message& msg)
{
    DDebug(this,DebugAll,"handleResSubscribeQuery() subscriber=%s notifier=%s",
	subscriber.c_str(),notifier.c_str());
    if (subscriber == notifier)
	return true;
    bool ok = false;
    // Check generic users
    GenericUser* gu = m_genericUsers.findUser(notifier);
    if (gu) {
	gu->lock();
	GenericContact* c = gu->find(subscriber);
	ok = c != 0;
	gu->unlock();
	TelEngine::destruct(gu);
	if (ok)
	    return true;
    }
    PresenceUser* u = m_users.getUser(notifier);
    if (u) {
	u->lock();
	Contact* c = u->findContact(subscriber);
	ok = c && c->m_subscription.from();
	u->unlock();
	TelEngine::destruct(u);
    }
    DDebug(this,DebugInfo,"handleResSubscribeQuery() subscriber=%s notifier=%s auth=%u",
	subscriber.c_str(),notifier.c_str(),ok);
    return ok;
}

// Handle online/offline resource.notify from contact or directed notifications
bool SubscriptionModule::handleResNotify(bool online, Message& msg)
{
    String* contact = msg.getParam("contact");
    if (TelEngine::null(contact)) {
	// TODO: handle generic users
	// TODO: handle offline without 'to' or without instance
	bool fromLocal = msg.getBoolValue("from_local",true);
	bool toLocal = msg.getBoolValue("to_local",true);
	if (!(fromLocal || toLocal))
	    return false;
	String* inst = msg.getParam("from_instance");
	if (TelEngine::null(inst))
	    return false;
	String* from = msg.getParam("from");
	String* to = msg.getParam("to");
	if(TelEngine::null(from) || TelEngine::null(to))
	    return false;
	DDebug(this,DebugAll,"handleResNotify(%s) from=%s instance=%s to=%s",
	    String::boolText(online),from->c_str(),inst->c_str(),to->c_str());
	// Update directed notifications for contacts not in sender's roster
	// or not having a subscription 'from'
	PresenceUser* src = fromLocal ? m_users.getUser(*from) : 0;
	Lock lock(src);
	if (src) {
	    src->lock();
	    if (!src->isSubFrom(*to))
		src->updateDirectNotify(online,*inst,*to,msg.getValue("to_instance"));
	    src->unlock();
	    TelEngine::destruct(src);
	}
	// Update instance capabilities to target's instances
	PresenceUser* u = toLocal ? m_users.getUser(*to) : 0;
	if (!u)
	    return false;
	u->lock();
	Contact* c = u->findContact(*from);
	if (c) {
	    if (online) {
		Instance* i = c->m_instances.set(*inst,msg.getIntValue("priority"));
		String* capsid = msg.getParam("caps.id");
		if (!TelEngine::null(capsid))
		    i->setCaps(*capsid,msg);
	    }
	    else
		c->m_instances.remove(*inst);
	}
	u->unlock();
	TelEngine::destruct(u);
	return false;
    }
    String* inst = msg.getParam("instance");
    DDebug(this,DebugAll,"handleResNotify(%s) contact=%s instance=%s",
	String::boolText(online),contact->c_str(),TelEngine::c_safe(inst));
    PresenceUser* u = m_users.getUser(*contact);
    if (!u)
	return false;
    u->lock();
    bool notify = false;
    bool newInstance = false;
    if (online) {
	// Update/add instance. Set notify
	if (!TelEngine::null(inst)) {
	    notify = true;
	    int prio = msg.getIntValue("priority");
	    Instance* i = u->instances().set(*inst,prio,&newInstance);
	    String* capsid = msg.getParam("caps.id");
	    if (!TelEngine::null(capsid))
		i->setCaps(*capsid,msg);
	    if (newInstance)
		DDebug(this,DebugAll,"handleResNotify(online) user=%s added instance=%s prio=%d",
		    contact->c_str(),inst->c_str(),prio);
	}
    }
    else {
	// Remove instance or clear the list
	if (!TelEngine::null(inst)) {
	    Instance* i = u->instances().removeInstance(*inst,false);
	    if (i) {
		notify = true;
		DDebug(this,DebugAll,"handleResNotify(offline) user=%s removed instance=%s",
		    contact->c_str(),inst->c_str());
		TelEngine::destruct(i);
	    }
	    u->directNotifyOffline(*inst,msg.getValue("data"));
	}
	else {
	    notify = (0 != u->instances().skipNull());
	    if (notify) {
		DDebug(this,DebugAll,"handleResNotify(offline) user=%s removed %u instances",
		    contact->c_str(),u->instances().count());
		u->instances().clear();
	    }
	    u->directNotifyOffline(String::empty(),msg.getValue("data"));
	}
    }
    if (notify) {
	const char* data = msg.getValue("data");
	// Notify contacts (from user) and new online user (from contacts)
	// Send pending in subscription requests to user's new instance
	// Re-send pending out subscription requests each time a new instance is notified
	for (ObjList* o = u->m_list.skipNull(); o; o = o->skipNext()) {
	    Contact* c = static_cast<Contact*>(o->get());
	    if (newInstance && c->m_subscription.pendingIn())
		subscribe(true,c->toString(),u->toString(),inst);
	    bool fromContact = newInstance && c->m_subscription.to();
	    bool pendingOut = !fromContact && newInstance && c->m_subscription.pendingOut();
	    if (!(c->m_subscription.from() || fromContact || pendingOut))
		continue;
	    PresenceUser* dest = m_users.getUser(*c);
	    DDebug(this,DebugAll,
		"handleResNotify(%s) user=%s instance=%s processing %s contact=%s sub=0x%x",
		String::boolText(online),u->user().c_str(),TelEngine::c_safe(inst),
		dest ? "local" : "remote",c->c_str(),(int)c->m_subscription);
	    if (!dest) {
		// User not found, it may belong to other domain
		// Send presence and probe it if our user is online
		if (c->m_subscription.from()) {
		    if (online)
			__plugin.notify(true,u->toString(),*c,*inst,String::empty(),data);
		    else
			__plugin.notify(false,u->toString(),*c,inst ? *inst : String::empty());
		}
		if (online) {
		    probe(u->toString(),*c);
		    if (pendingOut)
			subscribe(true,u->toString(),c->toString());
		}
		continue;
	    }
	    dest->lock();
	    // Notify user's instance to all contact's instances
	    if (c->m_subscription.from())
		dest->instances().notifyInstance(online,false,u->toString(),
		    dest->toString(),inst ? *inst : String::empty(),data);
	    // Notify all contact's instances to the new user's instance
	    if (fromContact)
		dest->instances().notifyUpdate(online,dest->toString(),
		    u->toString(),*inst);
	    else if (pendingOut) {
		// Both parties are known: handle pending out internally
		Message tmp("resource.subscribe");
		handleResSubscribe(true,u->toString(),c->toString(),tmp);
	    }
	    dest->unlock();
	    TelEngine::destruct(dest);
	}
	// Notify the instance to all other user's instance
	// Notify a new instance about other user's instances
	if (!TelEngine::null(inst)) {
	    u->instances().notifySkip(online,false,u->toString(),*inst,data);
	    if (newInstance && online)
		u->instances().notifySkip(online,true,u->toString(),*inst,data);
	}
    }
    u->unlock();
    TelEngine::destruct(u);
    return false;
}

// Handle resource.notify with operation (un)subscribed
bool SubscriptionModule::handleResNotifySub(bool sub, const String& src, const String& dest,
    Message& msg)
{
    DDebug(this,DebugAll,"handleResNotifySub(%s,%s,%s)",
	String::boolText(sub),src.c_str(),dest.c_str());

    PresenceUser* from = msg.getBoolValue("from_local",true) ? m_users.getUser(src) : 0;
    PresenceUser* to = msg.getBoolValue("to_local",true) ? m_users.getUser(dest) : 0;
    bool notifyFrom = false;
    while (from) {
	Lock lock(from);
	Contact* c = from->findContact(dest);
	Message* updExist = 0;
	// Add it to the list if subscribed and not found
	if (!c) {
	    if (sub) {
		c = new Contact(dest,SubscriptionState::From);
		Message* m = c->buildUpdateDb(src,true);
		m = queryDb(m);
		if (m) {
		    from->appendContact(c);
		    DDebug(this,DebugAll,"User '%s' added contact '%s' on 'subscribed'",
			src.c_str(),dest.c_str());
		    TelEngine::destruct(m);
		    notifyFrom = true;
		}
		else
		    TelEngine::destruct(c);
	    }
	    if (!c)
		break;
	}
	else {
	    bool changed = c->m_subscription.pendingIn();
	    c->m_subscription.reset(SubscriptionState::PendingIn);
	    if (sub) {
		if (!c->m_subscription.from()) {
		    c->m_subscription.set(SubscriptionState::From);
		    changed = true;
		    notifyFrom = true;
		}
	    }
	    else {
		if (c->m_subscription.from()) {
		    c->m_subscription.reset(SubscriptionState::From);
		    changed = true;
		    notifyFrom = true;
		}
	    }
	    if (changed)
		updExist = c->buildUpdateDb(src);
	}
	lock.drop();
	if (updExist) {
	    updExist = queryDb(updExist);
	    if (!updExist)
		notifyFrom = false;
	    TelEngine::destruct(updExist);
	}
	if (!notifyFrom)
	    break;
	// Synchronous notify "unavailable' to contact if unsubscribed
	// to make sure the notification is received before any other contact list changes
	if (!sub)
	    this->notify(false,src,dest,String::empty(),String::empty(),0,true);
	notifyRosterUpdate(src,dest);
	break;
    }
    while (to) {
	Lock lock(to);
	Contact* c = to->findContact(src);
	if (!c)
	    break;
	bool changed = c->m_subscription.test(SubscriptionState::PendingOut);
	c->m_subscription.reset(SubscriptionState::PendingOut);
	bool notify = !sub && changed;
	if (sub) {
	    if (!c->m_subscription.to()) {
		c->m_subscription.set(SubscriptionState::To);
		changed = true;
		notify = true;
	    }
	}
	else {
	    if (c->m_subscription.to()) {
		c->m_subscription.reset(SubscriptionState::To);
		changed = true;
		notify = true;
	    }
	}
	Message* m = 0;
	if (changed)
	    m = c->buildUpdateDb(dest);
	bool probeSubscriber = notify && c->m_subscription.to() && !from;
	lock.drop();
	m = queryDb(m);
	// Notify user roster change on success
	if (m) {
	    TelEngine::destruct(m);
	    if (notify)
		notifyRosterUpdate(dest,src);
	}
	// Probe remote subscriber (local subscribers will automatically notify presence)
	if (probeSubscriber)
	    probe(dest,src);
	break;
    }
    // Notify sender's presence on subscription aproval
    // Re-dispatch the subscription aproval message to a remote user before it
    bool retVal = false;
    if (notifyFrom && sub) {
	Lock lck(from);
	if (from->instances().skipNull()) {
	    if (to) {
		Lock lck2(to);
		notifyInstances(true,*from,*to);
	    }
	    else {
		__plugin.dispatch(msg);
		from->instances().notifyUpdate(true,src,dest,String::empty());
		retVal = true;
	    }
	}
    }
    TelEngine::destruct(from);
    TelEngine::destruct(to);
    return retVal;
}

// Handle resource.notify with operation probe
bool SubscriptionModule::handleResNotifyProbe(const String& from, const String& to,
    Message& msg)
{
    bool toLocal = msg.getBoolValue("to_local");
    DDebug(this,DebugAll,"handleResNotifyProbe(%s,%s) toLocal=%u",
	from.c_str(),to.c_str(),toLocal);
    const String* src = 0;
    const String* dest = 0;
    if (toLocal) {
	src = &from;
	dest = &to;
    }
    else {
	src = &to;
	dest = &from;
    }
    PresenceUser* user = m_users.getUser(*dest);
    if (!user)
	return false;
    user->lock();
    bool ok = false;
    Contact* c = 0;
    if (from != to) {
	c = user->findContact(*src);
	ok = c && c->m_subscription.from();
    }
    else
	ok = true;
    bool sync = msg.getBoolValue("sync");
    if (ok) {
	if (sync) {
	    unsigned int n = 0;
	    if (toLocal)
		n = user->instances().addListParam(msg);
	    else if (c)
		n = c->m_instances.addListParam(msg);
	    msg.setParam("instance.count",String(n));
	}
	else {
	    String* inst = msg.getParam("from_instance");
	    user->instances().notifyUpdate(true,*dest,*src,inst ? *inst : String::empty());
	}
    }
    user->unlock();
    TelEngine::destruct(user);
    return ok || sync;
}

// Update capabilities for all instances with the given caps id
void SubscriptionModule::updateCaps(const String& capsid, NamedList& list)
{
    m_users.lock();
    for (ObjList* o = m_users.users().skipNull(); o; o = o->skipNext()) {
	PresenceUser* u = static_cast<PresenceUser*>(o->get());
	u->instances().updateCaps(capsid,list);
	for (ObjList* c = u->m_list.skipNull(); c; c = c->skipNext())
	    (static_cast<Contact*>(c->get()))->m_instances.updateCaps(capsid,list);
    }
    m_users.unlock();
    // TODO: handle generic users
}


// Handle 'user.roster' messages with operation 'query'
bool SubscriptionModule::handleUserRosterQuery(const String& user, const String* contact,
    Message& msg)
{
    DDebug(this,DebugAll,"handleUserRosterQuery() user=%s contact=%s",
	user.c_str(),TelEngine::c_safe(contact));
    Message* m = 0;
    NamedList p("");
    p.addParam("username",user);
    if (TelEngine::null(contact))
	m = buildDb(m_account,m_userLoadQuery,p);
    else {
	p.addParam("contact",*contact);
	m = buildDb(m_account,m_contactLoadQuery,p);
    }
    m = queryDb(m);
    if (!m)
	return false;
    bool hierarchical = msg.getBoolValue(YSTRING("hierarchical"));
    Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
    int rows = 0;
    int cols = 0;
    unsigned int n = 0;
#ifdef DEBUG
    u_int64_t start = Time::now();
#endif
    ObjList** columns = 0;
    String** titles = 0;
    if (arrayData(a,rows,cols,columns,titles)) {
	int cntCol = strIndex(titles,cols,YSTRING("contact"));
	int usrCol = strIndex(titles,cols,YSTRING("username"));
	for (int row = 1; row < rows; row++) {
	    String cPrefix("contact.");
	    cPrefix << ++n;
	    NamedList* p = 0;
	    String prefix;
	    if (hierarchical)
		p = new NamedList("");
	    else
		prefix << cPrefix << ".";
	    for (int col = 1; col < cols; col++) {
		if (columns[col])
		    columns[col] = columns[col]->next();
		// Skip username column, missing object or empty title
		if (col == usrCol || !columns[col] || TelEngine::null(titles[col]))
		    continue;
		String* value = YOBJECT(String,columns[col]->get());
		if (!value)
		    continue;
		if (col != cntCol) {
		    if (p)
			p->addParam(*(titles[col]),*value);
		    else
			msg.addParam(prefix + *(titles[col]),*value);
		}
		else if (p)
		    p->assign(*value);
		else
		    msg.addParam(cPrefix,*value);
	    }
	    if (p)
		msg.addParam(new NamedPointer(cPrefix,p,*p));
	}
	if (n)
	    msg.addParam("contact.count",String(n));
    }
    clearArrayData(columns,titles);
#ifdef DEBUG
    Debug(this,DebugAll,"Filled %u contacts in %u ms for user '%s' hierarchical=%u",
	n,ellapsedMs(start),user.c_str(),hierarchical);
#endif
    TelEngine::destruct(m);
    return true;
}

// Handle 'user.roster' messages with operation 'update'
bool SubscriptionModule::handleUserRosterUpdate(const String& user, const String& contact,
    Message& msg)
{
    DDebug(this,DebugAll,"handleUserRosterUpdate() user=%s contact=%s",
	user.c_str(),contact.c_str());

    // Check if the user exists
    PresenceUser* u = m_users.getUser(user);
    if (!u)
	return false;

    NamedList p("");
    String params("username,contact");
    NamedString* cParams = msg.getParam("contact.parameters");
    if (!TelEngine::null(cParams))
	params.append(*cParams,",");
    p.copyParams(msg,params);
    bool full = msg.getBoolValue("full");
    Message* m = buildDb(m_account,full ? m_contactSetFullQuery : m_contactSetQuery,p);
    m = queryDb(m);
    if (!m) {
	TelEngine::destruct(u);
	return false;
    }
    // Load the contact to get all its data
    // The data will be used to notify changes and handle contact
    //  subscription related notifications
    // Notify the update before notifying the instances
    Array* contactData = notifyRosterUpdate(user,contact,true);
    if (!contactData) {
	TelEngine::destruct(u);
	TelEngine::destruct(m);
	return true;
    }

    // Check if contact changed
    u->lock();
    SubscriptionState oldSub;
    Contact* c = u->findContact(contact);
    bool newContact = (c == 0);
    if (c) {
	oldSub.replace((int)c->m_subscription);
	c->set(*contactData,1);
    }
    else {
	c = Contact::build(*contactData,1);
	if (c)
	    u->appendContact(c);
    }
    TelEngine::destruct(contactData);
    // Notify instances
    if (c) {
	PresenceUser* dest = m_users.getUser(contact);
	Lock lock(dest);
	bool doProbe = false;
	// To contact if it's subscribed to user's presence and it's new one
	// or subscription changed
	if (c->m_subscription.from() && (newContact || !oldSub.from())) {
	    if (dest) {
		if (dest->instances().skipNull() && u->instances().skipNull())
		    u->instances().notifyUpdate(true,user,contact,dest->instances());
	    }
	    else
		doProbe = true;
	}
	// From contact to user
	if (c->m_subscription.to()) {
	    if (newContact)
		doProbe = (dest == 0);
	    else if (!oldSub.to()) {
		if (dest) {
		    if (dest->instances().skipNull() && u->instances().skipNull())
			dest->instances().notifyUpdate(true,contact,user,u->instances());
		}
		else
		    doProbe = true;
	    }
	}
	lock.drop();
	TelEngine::destruct(dest);
	if (doProbe && c->m_subscription.to())
	    probe(user,contact);
    }
    u->unlock();
    TelEngine::destruct(u);
    TelEngine::destruct(m);
    return true;
}

// Handle 'user.roster' messages with operation 'delete'
bool SubscriptionModule::handleUserRosterDelete(const String& user, const String& contact,
    Message& msg)
{
    DDebug(this,DebugAll,"handleUserRosterDelete() user=%s contact=%s",
	user.c_str(),contact.c_str());
    Message* m = buildDb(m_account,m_contactDeleteQuery,msg);
    m = queryDb(m);
    if (!m)
	return false;
    TelEngine::destruct(m);
    // Find the user before notifying the operation: notify instances before remove
    PresenceUser* u = m_users.getUser(user);
    if (u) {
	u->lock();
	Contact* c = u->removeContact(contact,false);
	if (c) {
	    // Notify 'offline' to both parties
	    if (c->m_subscription.to())
		notify(false,contact,user);
	    if (c->m_subscription.from())
		notify(false,user,contact);
	    // Contact is a known user: update user subscription in it's list and
	    //  notify it if it has any instances
	    // Unknown user: unsubcribe it and request unsubscribe
	    PresenceUser* uc = m_users.getUser(contact);
	    if (uc) {
		uc->lock();
		Contact* cc = uc->findContact(user);
		if (cc) {
		    int flgs = SubscriptionState::From | SubscriptionState::To |
			SubscriptionState::PendingOut;
		    bool update = cc->m_subscription.test(flgs);
		    bool changed = update || cc->m_subscription.pendingIn();
		    cc->m_subscription.reset(flgs | SubscriptionState::PendingIn);
		    // Save data before update notification (use saved data in notification)
		    if (changed) {
			Message* m = cc->buildUpdateDb(contact);
			TelEngine::destruct(queryDb(m));
		    }
		    if (update)
			notifyRosterUpdate(contact,user,false,false);
		}
		uc->unlock();
		TelEngine::destruct(uc);
	    }
	    else {
		subscribed(false,user,contact);
		subscribe(false,user,contact);
	    }
	    TelEngine::destruct(uc);
	    TelEngine::destruct(c);
	}
	u->unlock();
	TelEngine::destruct(u);
    }
    Message* mu = message("user.roster");
    mu->addParam("notify","delete");
    mu->addParam("username",user);
    mu->addParam("contact",contact);
    Engine::enqueue(mu);
    return true;
}

// Handle 'user.update' messages with operation 'delete'
void SubscriptionModule::handleUserUpdateDelete(const String& user, Message& msg)
{
    DDebug(this,DebugAll,"handleUserUpdateDelete() user=%s",user.c_str());
    PresenceUser* u = m_users.getUser(user);
    if (!u)
	return;
    u->lock();
    for (ObjList* o = u->m_list.skipNull(); o; o = o->skipNext()) {
	Contact* c = static_cast<Contact*>(o->get());
	if (c->m_subscription.from())
	    notify(false,user,c->toString());
    }
    u->unlock();
    TelEngine::destruct(u);
    // Remove the user from memory and database roster
    m_users.removeUser(user);
    NamedList p("");
    p.addParam("username",user);
    Message* m = buildDb(m_account,m_userDeleteQuery,p);
    TelEngine::destruct(queryDb(m));
}

// Handle 'call.route' messages
bool SubscriptionModule::imRoute(Message& msg, const String& type)
{
    String* caller = msg.getParam("caller");
    String* called = msg.getParam("called");
    if (TelEngine::null(caller) || TelEngine::null(called))
	return false;
    DDebug(this,DebugAll,"%s caller=%s called=%s",
	msg.c_str(),caller->c_str(),called->c_str());
    PresenceUser* u = m_users.getUser(*called);
    if (!u) {
	XDebug(this,DebugAll,
	    "%s '%s' caller=%s called=%s destination is an unknown user",
	    msg.c_str(),type.c_str(),caller->c_str(),called->c_str());
	return false;
    }
    bool auth = msg.getBoolValue("auth");
    bool ok = true;
    unsigned int n = 0;
    String* tmp = msg.getParam("called_instance");
    bool haveInst = !TelEngine::null(tmp);
    u->lock();
    // An instance was given
    if (haveInst) {
	if (!auth || u->findContact(*caller) || *caller == *called) {
	    Instance* inst = u->instances().findInstance(*tmp);
	    if (inst)
		inst->addListParam(msg,++n);
	}
	else
	    ok = false;
    }
    // No instance given or requested to fallback to online instances
    if (ok && !n &&
	(!haveInst || msg.getBoolValue(YSTRING("fallback_online_instances")))) {
	String* skip = 0;
	if (*caller == *called)
	    skip = msg.getParam("caller_instance");
	else
	    ok = !auth || u->findContact(*caller);
	if (ok)
	    n = u->instances().addListParam(msg,skip);
    }
    u->unlock();
    TelEngine::destruct(u);
    if (!ok)
	return false;
    msg.addParam("instance.count",String(n));
    if (n) {
	lock();
	msg.retValue() = m_routeCallto;
	unlock();
	msg.replaceParams(msg.retValue());
	if (!msg.retValue())
	    return false;
	Debug(this,DebugAll,"Routing '%s' caller=%s called=%s to '%s' instances=%u",
	    type.c_str(),caller->c_str(),called->c_str(),msg.retValue().c_str(),n);
    }
    return n != 0;
}

void SubscriptionModule::expireSubscriptions()
{
    u_int64_t time = Time::msecNow();
    Lock lock(m_eventsMutex);
    ObjList* o = m_events.skipNull();
    while (o) {
	NamedList* nl = static_cast<NamedList*>(o->get());
	unsigned int n = nl->length();
	ObjList remove;
	for (unsigned int i = 0; i < n ; i++) {
	    NamedString* ns = nl->getParam(i);
	    if (!ns)
		continue;
	    NamedPointer* np = static_cast<NamedPointer*>(ns);
	    EventUser* eu = static_cast<EventUser*>(np->userData());
	    if (!eu)
		continue;
	    XDebug(this,DebugAll,"Expiring user '%s' event '%s'",
		eu->toString().c_str(),nl->c_str());
	    eu->expire(time,nl->c_str());
	    if (!eu->m_list.skipNull())
		remove.append(ns)->setDelete(false);
	}
	ObjList* oo = remove.skipNull();
	if (!oo) {
	    o = o->skipNext();
	    continue;
	}
	for (; oo; oo = oo->skipNext()) {
	    NamedString* ns = static_cast<NamedString*>(oo->get());
	    NamedPointer* np = static_cast<NamedPointer*>(ns);
	    EventUser* eu = static_cast<EventUser*>(np->userData());
	    if (!eu)
		continue;
	    DDebug(this,DebugAll,"Removing empty user '%s' event '%s'",
		eu->toString().c_str(),nl->c_str());
	    nl->clearParam(ns);
	}
	if (nl->count())
	    o = o->skipNext();
	else {
	    DDebug(this,DebugAll,"Removing empty event list '%s'",nl->c_str());
	    o->remove();
	    o = o->skipNull();
	}
    }
}

// Build a database message from account and query.
// Replace query params. Return Message pointer on success
Message* SubscriptionModule::buildDb(const String& account, const String& query,
	const NamedList& params)
{
    XDebug(this,DebugAll,"buildDb(%s,%s)",account.c_str(),query.c_str());
    if (!(account && query))
	return 0;
    Message* m = new Message("database");
    m->addParam("account",account);
    String tmp = query;
    params.replaceParams(tmp,true);
    m->addParam("query",tmp);
    return m;
}

// Dispatch a database message
// Return Message pointer on success. Release msg on failure
Message* SubscriptionModule::queryDb(Message*& msg)
{
    if (!msg)
	return 0;
    bool ok = Engine::dispatch(msg) && !msg->getParam("error");
    if (!ok) {
	Debug(this,DebugNote,"Database query=%s failed error=%s",
	    msg->getValue("query"),msg->getValue("error"));
	TelEngine::destruct(msg);
    }
    return msg;
}

bool SubscriptionModule::received(Message& msg, int id)
{
    switch (id) {
	case Timer:
	    s_check = true;
	    break;
	case Route:
	    {
		if (!m_routeCallto)
		    return false;
		String* t = msg.getParam(YSTRING("route_type"));
		return t && (*t == YSTRING("msg")) && imRoute(msg,*t);
	    }
	case Halt:
	    Lock lock(this);
	    if (m_expire)
	        m_expire->cancel(false);
	    lock.drop();
	    while (m_expire)
		Thread::yield();
	    // Uninstall message handlers
	    for (ObjList* o = m_handlers.skipNull(); o; o = o->skipNext()) {
		SubMessageHandler* h = static_cast<SubMessageHandler*>(o->get());
		Engine::uninstall(h);
	    }
	    DDebug(this,DebugAll,"Halted");
	    break;
    }
    return Module::received(msg,id);
}

bool SubscriptionModule::commandExecute(String& retVal, const String& line)
{
    String l = line;
    l.startSkip(name());
    l.trimSpaces();
    if (l.startSkip("status")) {
	l.trimSpaces();
	String user = "";
//	extractName(l,user);
	String contact = "";
//	extractName(l.substr(user.length() + 2,-1),contact);
	if (user.null() || contact.null()) {
	    retVal << "Expected <PresenceUser,Contact> pair";
	    DDebug(this,DebugInfo,"Command Execute 2 : return false user->null() || contact->null()");
	    return false;
	}
	DDebug(this,DebugInfo,"Command Execute , operation status for: %s, to %s",user.c_str(),contact.c_str());
//	retVal << "Subscription state for user: " << user << " and contact: "
//	    << contact << " is: " << m_users.getSubscription(user,contact);
	return true;
    }
    if (l.startSkip("unsubscribe")) {
	l.trimSpaces();
	String* contact = new String();
	String* user = new String();
	ObjList* ob = l.split(' ',false);
	int counter = 0;
	for (ObjList* o = ob->skipNull(); o; o = o->skipNext()) {
	    switch (counter) {
		case 0:
		    user = static_cast<String*>(o->get());
		    break;
		case 1:
		    contact = static_cast<String*>(o->get());
		    break;
		default:
		    retVal << "Expected <PresenceUser,Contact> pair";
		    return false;
	    }
	    counter += 1;
	}
	if (user->null() || contact->null()) {
	    retVal << "Expected <PresenceUser,Contact> pair";
	    return false;
	}
	// TODO unsubscribe the user
	retVal << "PresenceUser: " << *user << " successfully unsubscribed from " << *contact << "'s presence";
    }
    return false;
}

bool SubscriptionModule::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    if (partLine.null() || (partLine == "help"))
	Module::itemComplete(msg.retValue(),name(),partWord);
    else if (partLine == name()) {
	for (const char** list = s_cmds; *list; list++)
	    Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }

    return Module::commandComplete(msg,partLine,partWord);
}

// Notify 'from' instances to 'to'
void SubscriptionModule::notifyInstances(bool online, PresenceUser& from, PresenceUser& to)
{
    if (!to.instances().skipNull())
	return;
    // Source has instances: notify them to destination
    // Source has no instance: notify offline to destination
    if (from.instances().skipNull()) {
	if (online || !s_singleOffline)
	    from.instances().notifyUpdate(online,from.toString(),to.toString(),to.instances());
	else
	    notify(false,from.toString(),to.toString());
    }
    else if (online)
	to.instances().notifyInstance(false,false,from.toString(),to.toString(),String::empty(),0);
}

} /* anonymous namespace */

/* vi: set ts=8 sw=4 sts=4 noet: */
