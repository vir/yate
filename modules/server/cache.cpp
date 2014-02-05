/**
 * cache.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Cache implementation
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

class CacheItem;                         // A cache item
class Cache;                             // A cache hash list
class CacheThread;                       // Base class for cache threads
class CacheExpireThread;                 // Cache expire thread
class CacheLoadThread;                   // Cache load thread
class EngineHandler;                     // engine.start/stop handler
class CacheModule;

// Max value for cache expire check interval
#define EXPIRE_CHECK_MAX 300
// Min value for cache reload interval in seconds
#define CACHE_RELOAD_MIN 10

class CacheItem : public NamedList
{
    friend class Cache;
public:
    inline CacheItem(const String& id, const NamedList& p, const String& copy,
	u_int64_t expires)
	: NamedList(id), m_expires(0)
	{ update(p,copy,expires); }
    inline void update(const NamedList& p, const String& copy, u_int64_t expires) {
	    m_expires = expires;
	    if (copy)
		copyParams(p,copy);
	    else
		copyParams(p);
	}
    inline u_int64_t expires() const
	{ return m_expires; }
    inline bool timeout(const Time& time) const
	{ return m_expires && m_expires < time; }
protected:
    u_int64_t m_expires;
};

class Cache : public RefObject, public Mutex
{
public:
    Cache(const String& name, int size, const NamedList& params);
    // Retrieve the number of items in cache
    inline unsigned int count() const
	{ return m_count; }
    // Retrieve the cache TTL
    inline u_int64_t cacheTtl() const
	{ return m_cacheTtl; }
    // Check if the cache has reload set
    inline bool canReload()
	{ return m_loadInterval != 0 || m_reload != 0; }
    // Retrieve the mutex protecting a given list
    inline unsigned int index(const String& str) const
	{ return str.hash() % m_list.length(); }
    // Safely retrieve the id matching parameter
    inline void getIdParam(String& param) {
	    Lock lck(this);
	    param = m_idParam;
	}
    // Replace id matching parameter from a list
    // Return true if the id is not empty
    inline bool replaceIdParam(String& param, const NamedList& list) {
	    getIdParam(param);
	    list.replaceParams(param);
	    return !param.null();
	}
    // Safely retrieve DB load info
    void getDbLoad(String& account, String& query, unsigned int& loadChunk,
	Thread::Priority& loadPrio);
    void getDbLoadItemCmd(String& account, String& query, Thread::Priority& loadPrio);
    // Schedule a cache re-load
    bool scheduleLoad(const NamedList& params);
    // Reinit
    inline void update(const NamedList& params)
	{ doUpdate(params,false); }
    // Expire entries
    void expire(const Time& time);
    // Reload the cache if not currently loading and set it to reload
    // Set force to true to ignore the time to reload value
    bool reload(const Time& time, bool force = false);
    // Check if the cache can be loaded. Set the loading flag if true is returned
    // endLoad() must be called when done
    bool startLoad();
    // Reset the loading flag. Set the next re-load time if we have an interval
    void endLoad(bool triggerReload);
    // Copy params from cache item. Return true if found
    bool copyParams(const String& id, NamedList& list, const String* cpParams);
    // Add an item to the cache. Remove an existing one
    // Set dbSave=false when loading from database to avoid saving it again
    void add(const String& id, const NamedList& params, const String* cpParams,
	bool dbSave = true) {
	    Lock lock(this);
	    addUnsafe(id,params,cpParams,dbSave);
	}
    // Add items from NamedList list. Return the number of added items
    unsigned int add(ObjList& list);
    // Add an item from an Array row
    inline void add(Array& array, int row, int cols) {
	    Lock lock(this);
	    addUnsafe(array,row,cols);
	}
    // Add items from Array rows. Return the number of added rows
    unsigned int addRows(Array& array);
    // Clear the cache
    unsigned int clear();
    // Remove an item, decrease the item counter
    unsigned int remove(const String& id, bool regexp = false);
    // Retrieve cache name
    virtual const String& toString() const;
    // Dump the cache to output if XDEBUG is defined
    void dump(const char* oper);
    // Retrieve the item length bit mask
    u_int32_t prefixMask() const
	{ return m_prefixMask; }
    // Set chunk limit and offset to a query
    // Return the number of replaced params
    static int setLimits(String& query, unsigned int chunk, unsigned int offset);
protected:
    virtual void destroyed();
    // (Re)init
    void doUpdate(const NamedList& params, bool first);
    // Add an item to the cache. Remove an existing one
    CacheItem* addUnsafe(const String& id, const NamedList& params, const String* cpParams,
	bool dbSave = true);
    // Add an item from an Array row
    CacheItem* addUnsafe(Array& array, int row, int cols);
    // Find a cache item. This method is not thread safe
    CacheItem* find(const String& id);
    // Find a cache item or prefix. This method is not thread safe
    CacheItem* findPrefix(const String& id);
    // Adjust cache length to limit
    void adjustToLimit(CacheItem* skipAdded);

    String m_name;                       // Cache name
    HashList m_list;                     // The list holding the cache
    u_int64_t m_cacheTtl;                // Cache item TTL (in us)
    unsigned int m_count;                // Current number of items
    unsigned int m_limit;                // Limit the number of cache items
    unsigned int m_limitOverflow;        // Allowed limit overflow
    unsigned int m_loadChunk;            // The number of items to load in each DB load query
    u_int32_t m_prefixMin;               // Minimum length of a prefix
    u_int32_t m_prefixMask;              // Bitmask of loaded lengths
    Thread::Priority m_loadPrio;         // Load thread priority
    bool m_loading;                      // Cache is loading from database
    unsigned int m_loadInterval;         // Cache re-load interval (in seconds)
    u_int64_t m_nextLoad;                // Next time to load the cache
    String m_expireParam;                // Item expire parameter in add() parameters list
    int m_reload;                        // Scheduled reload 0: none, 1: full, -1: items
    ObjList* m_reloadItems;              // Scheduled items to reload
    String m_idParam;                    // Cache id match parameter
    String m_copyParams;                 // Item parameters to store/copy
    String m_account;                    // Database account
    String m_accountLoadCache;           // Load cache account
    String m_queryLoadCache;             // Database load all cache query
    String m_queryLoadItem;              // Database load a cache item query
    String m_queryLoadItemCmd;           // Database load item on command query
    String m_querySave;                  // Database save query
    String m_queryExpire;                // Database expire query
};

class CacheThread : public Thread, public GenObject
{
public:
    CacheThread(const char* name, Priority prio = Normal);
    ~CacheThread();
    // List of running threads (objects are not owned by the list)
    // The list is protected by the plugin mutex
    static ObjList s_threads;
};

class CacheExpireThread : public CacheThread
{
public:
    inline CacheExpireThread()
	: CacheThread("CacheExpireThread")
	{}
    virtual void run();
};

class CacheLoadThread : public CacheThread
{
public:
    inline CacheLoadThread(const String name, Thread::Priority prio, ObjList* items)
	: CacheThread("CacheLoadThread",prio),
	m_cache(name), m_items(items)
	{}
    ~CacheLoadThread()
	{ TelEngine::destruct(m_items); }
    virtual void run();
private:
    String m_cache;
    ObjList* m_items;
};

class CacheModule : public Module
{
public:
    enum Relays {
	LnpBefore = Route,
	LnpAfter = Private,
	CnamBefore = Private << 1,
	CnamAfter = Private << 2,
    };
    CacheModule();
    ~CacheModule();
    // Safely retrieve the database account
    inline void getAccount(String& buf, bool cacheLoad = false) {
	    Lock lock(this);
	    buf = !cacheLoad ? m_account : m_accountLoadCache;
	}
    // Safely retrieve a reference to a cache
    inline void getCache(RefPointer<Cache>& c, const String& name) {
	    Lock lock(this);
	    c = findCache(name);
	}
    // Build/update a cache
    void setupCache(const String& name, const NamedList& params);
    // Load a cache from database
    // Optionally load specific items only (the list will be consumed)
    // Set async=false from loading thread
    void loadCache(const String& name, bool async = true, ObjList* items = 0);
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusModule(String& buf);
    virtual void statusParams(String& buf);
    virtual void statusDetail(String& buf);
    virtual bool commandExecute(String& retVal, const String& line);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    // Find a cache. This method is not thread safe
    Cache* findCache(const String& name);
    // Update cache reload flag
    void updateCacheReload();
    // Add a cache to detail
    void addCacheDetail(String& buf, Cache* cache);
    // Handle messages for LNP
    void handleLnp(Message& msg, bool before);
    // Handle messages for CNAM
    void handleCnam(Message& msg, bool before);
    // Cache load handler
    void commandLoad(Cache* cache, NamedList& params, String& retVal);
    // Cache flush handler
    void commandFlush(Cache* cache, NamedList& params, String& retVal);
    // Help message handler
    bool commandHelp(String& retVal, const String& line);

    bool m_haveCacheReload;              // True if we have caches to reload
    String m_account;                    // Database account
    String m_accountLoadCache;           // Load cache account
    Cache* m_lnpCache;                   // LNP cache
    Cache* m_cnamCache;                  // CNAM cache
};


INIT_PLUGIN(CacheModule);                // The module
ObjList CacheThread::s_threads;          // List of running threads
static bool s_engineStarted = false;     // Engine started flag
static bool s_lnpStoreFailed = false;    // Store failed LNP requests
static bool s_lnpStoreNpdiBefore = true; // Store LNP when already done
static bool s_cnamStoreEmpty = false;    // Store empty caller name in CNAM cache
static unsigned int s_size = 0;          // The number of listst in each cache
static unsigned int s_limit = 0;         // Default cache limit
static unsigned int s_loadChunk = 0;     // The number of cache items to load in each DB load query
static unsigned int s_maxChunks = 1000;  // Maximum number of chunks to load in a cache
static Thread::Priority s_loadPrio = Thread::Normal; // Cache load thread priority
static unsigned int s_cacheTtlSec = 0;   // Default cache item time to live (in seconds)
static u_int64_t s_checkToutInterval = 0;// Interval to check cache timeout

// Used strings: avoid allocation
static const String s_id = "id";
static const String s_regexp = "regexp";
// List of known caches
static const String s_caches[] = {"lnp", "cnam", ""};
// Commands
enum CacheCommands {
    CmdLoad = 0,
    CmdFlush,
    CmdCount
};
static const String s_cmd[CmdCount] = {"load","flush"};
static const String s_cmdCacheFormat = "cache {load|flush} cache_name [[param=value]...]";
static const String s_cmdFormat[CmdCount] = {
    "cache load cache_name [[param=value]...]",
    "cache flush cache_name [[param=value]...]"
};
static const String s_cmdHelp[CmdCount] = {
    "Load a cache from database. Use 'id' (can be repeated) parameter to load specific item(s) only",
    "Flush (clear) a cache's memory. Use 'id' (can be repeated) parameter to delete specific item(s) only"
};


class EngineHandler : public MessageHandler
{
public:
    inline EngineHandler(bool start)
	: MessageHandler(start ? "engine.start" : "engine.stop",100,__plugin.name()),
	  m_start(start)
	{ }
    virtual bool received(Message& msg);
protected:
    bool m_start;
};


// Check if application or current thread are terminating
static inline bool exiting()
{
    return Engine::exiting() || Thread::check(false);
}

// Return a valid unsigned integer
static inline unsigned int safeValue(int val)
{
    return val >= 0 ? val : 0;
}

// Adjust a cache size
static inline unsigned int adjustedCacheSize(int val)
{
    if (val >= 3 && val <= 1024)
	return val;
    if (val > 1024)
	return 1024;
    return 3;
}

// Adjust a cache limit
static inline unsigned int adjustedCacheLimit(int val, int size)
{
    int sq = size * size;
    return (val > sq) ? val : sq;
}

// Adjust a cache TTL
static inline unsigned int adjustedCacheTtl(int val)
{
    return val > 10 ? val : (!val ? 0 : 10);
}

// Adjust a cache load chunk
static inline unsigned int adjustedCacheLoadChunk(int val)
{
    if (val <= 0)
	return 0;
    if (val >= 500 && val <= 50000)
	return val;
    return val < 500 ? 500 : 50000;
}

// Show cache item changes to output
static inline void dumpItem(Cache& c, CacheItem& item, const char* oper)
{
#ifdef XDEBUG
    String tmp;
    item.dump(tmp," ");
    Debug(&__plugin,DebugAll,"Cache(%s) %s %p %s expires=%u [%p]",
	c.toString().c_str(),oper,&item,tmp.c_str(),
	(unsigned int)(item.expires()/1000000),&c);
#endif
}

// Fill a list of parameters from a string
static void fillList(NamedList& list, String& buf)
{
    static const Regexp r("^\\(.* \\)\\?\\([^= ]\\+\\)=\\([^=]*\\)$");
    ObjList items;
    while (buf) {
	if (!buf.matches(r))
	    break;
	items.insert(new NamedString(buf.matchString(2),buf.matchString(3).trimBlanks()));
	buf = buf.matchString(1).trimBlanks();
    }
    GenObject* gen = 0;
    while (0 != (gen = items.remove(false)))
	list.addParam(static_cast<NamedString*>(gen));
}


/*
 * Cache
 */
Cache::Cache(const String& name, int size, const NamedList& params)
    : Mutex(false,"Cache"),
    m_name(name), m_list(size), m_cacheTtl(0), m_count(0), m_limit(0),
    m_limitOverflow(0), m_loadChunk(0), m_prefixMin(0), m_prefixMask(0),
    m_loadPrio(Thread::Normal),
    m_loading(false), m_loadInterval(0), m_nextLoad(0),
    m_reload(0), m_reloadItems(0)

{
    Debug(&__plugin,DebugInfo,"Cache(%s) size=%u [%p]",
	m_name.c_str(),m_list.length(),this);
    m_expireParam << "cache_" << m_name << "_expires";
    doUpdate(params,true);
}

// Reload the cache if not currently loading and set it to reload
// Set force to true to ignore the time to reload value
bool Cache::reload(const Time& time, bool force)
{
    XDebug(&__plugin,DebugAll,
	"Cache(%s)::reload() loadinterval=%u loading=%u reload=%d [%p]",
	m_name.c_str(),m_loadInterval,m_loading,m_reload,this);
    if (m_loading || (!m_reload && !m_loadInterval))
	return false;
    lock();
    String tmp;
    ObjList* items = 0;
    if (!m_reload) {
	if (m_loadInterval && !m_loading && (force || !m_nextLoad || m_nextLoad <= time))
	    tmp = toString();
    }
    else if (!m_loading) {
	if (m_reload > 0) {
	    m_reload--;
	    if (!m_reload) {
		tmp = toString();
		TelEngine::destruct(m_reloadItems);
	    }
	}
	else if (m_reloadItems && m_reloadItems->skipNull()) {
	    m_reload++;
	    if (!m_reload) {
		tmp = toString();
		items = m_reloadItems;
		m_reloadItems = 0;
	    }
	}
	else {
	    TelEngine::destruct(m_reloadItems);
	    m_reload = 0;
	}
    }
    // Loading: release pending items if any
    if (tmp)
	TelEngine::destruct(m_reloadItems);
    unlock();
    if (!tmp)
	return false;
    DDebug(&__plugin,DebugInfo,"Cache(%s) re-loading [%p]",m_name.c_str(),this);
    __plugin.loadCache(tmp,true,items);
    return true;
}

// Check if the cache can be loaded. Set the loading flag if true is returned
// endLoad() must be called when done
bool Cache::startLoad()
{
    Lock lock(this);
    DDebug(&__plugin,DebugInfo,"Cache(%s) startLoad() ok=%u [%p]",
	m_name.c_str(),!m_loading,this);
    if (m_loading)
	return false;
    m_loading = true;
    return true;
}

// Reset the loading flag. Set the next re-load time if we have an interval
void Cache::endLoad(bool triggerReload)
{
    Lock lock(this);
    DDebug(&__plugin,DebugInfo,"Cache(%s) endLoad() [%p]",m_name.c_str(),this);
    m_loading = false;
    if (triggerReload)
	m_nextLoad = m_loadInterval ? (Time::now() + (u_int64_t)m_loadInterval * 1000000) : 0;
}

// Copy params from cache item. Return true if found
bool Cache::copyParams(const String& id, NamedList& list, const String* cpParams)
{
    lock();
    CacheItem* item = findPrefix(id);
    if (!item && m_account && m_queryLoadItem) {
	// Load from database
	String query = m_queryLoadItem;
	NamedList p("");
	p.addParam("id",id);
	p.replaceParams(query);
	Message m("database");
	m.addParam("account",m_account);
	m.addParam("query",query);
	unlock();
	bool ok = Engine::dispatch(m);
	lock();
	const char* error = m.getValue("error");
	if (ok && !error) {
	    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
	    int rows = a ? a->getRows() : 0;
	    if (rows > 0)
		item = addUnsafe(*a,1,a->getColumns());
	    else
		DDebug(&__plugin,DebugAll,"Cache(%s) item '%s' not found in database [%p]",
		    m_name.c_str(),id.c_str(),this);
	}
	else
	    Debug(&__plugin,DebugNote,"Cache(%s) failed to load item '%s' %s [%p]",
		m_name.c_str(),id.c_str(),TelEngine::c_safe(error),this);
    }
    if (item) {
	list.copyParams(*item,!cpParams ? m_copyParams : *cpParams);
	dumpItem(*this,*item,"found in cache");
    }
    unlock();
    return item != 0;
}

// Safely retrieve DB load info
void Cache::getDbLoad(String& account, String& query, unsigned int& loadChunk,
    Thread::Priority& loadPrio)
{
    Lock lock(this);
    account = (m_accountLoadCache ? m_accountLoadCache : m_account);
    query = m_queryLoadCache;
    loadChunk = m_loadChunk;
    loadPrio = m_loadPrio;
}

void Cache::getDbLoadItemCmd(String& account, String& query, Thread::Priority& loadPrio)
{
    Lock lock(this);
    account = m_account;
    query = m_queryLoadItemCmd;
    loadPrio = m_loadPrio;
}

// Schedule a cache re-load
bool Cache::scheduleLoad(const NamedList& params)
{
    Lock lck(this);
    XDebug(&__plugin,DebugAll,"Cache(%s)::scheduleLoad() [%p]",m_name.c_str(),this);
    int delay = params.getIntValue("delay",1);
    if (delay < 1)
	delay = 1;
    if (!params.getParam(s_id)) {
	if (!((m_account || m_accountLoadCache) && m_queryLoadCache))
	    return false;
	// Schedule full cache reload
	m_reload = delay;
	Debug(&__plugin,DebugAll,"Cache(%s) scheduled full reload delay=%d [%p]",
	    m_name.c_str(),delay,this);
	return true;
    }
    // Schedule items re-load if we have DB data and we don't have a pending full reload
    if (!(m_account && m_queryLoadItemCmd) || m_reload > 0)
	return false;
    if (!m_reloadItems)
	m_reloadItems = new ObjList;
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == s_id && *ns && !m_reloadItems->find(*ns))
	    m_reloadItems->append(new String(*ns));
    }
    if (m_reloadItems->skipNull()) {
	m_reload = -delay;
	String buf;
#ifdef DEBUG
	buf << " for";
	buf.append(m_reloadItems," ");
#endif
	Debug(&__plugin,DebugAll,"Cache(%s) scheduled item(s) reload delay=%d%s [%p]",
	    m_name.c_str(),delay,buf.safe(),this);
    }
    else {
	TelEngine::destruct(m_reloadItems);
	m_reload = 0;
    }
    return true;
}

// Expire entries
void Cache::expire(const Time& time)
{
    if (!m_cacheTtl)
	return;
    Lock lck(this);
    if (!m_cacheTtl)
	return;
    XDebug(&__plugin,DebugAll,"Cache(%s) expiring items [%p]",m_name.c_str(),this);
    if (m_account && m_queryExpire) {
	String query = m_queryExpire;
	NamedList p("");
	p.setParam("time",String(time.sec()));
	p.replaceParams(query);
	Message* m = new Message("database");
	m->addParam("account",m_account);
	m->addParam("query",query);
	m->addParam("results",String::boolText(false));
	Engine::enqueue(m);
    }
    unsigned int oldCount = m_count;
    for (unsigned int i = 0; i < m_list.length(); i++) {
	if (exiting())
	    break;
	ObjList* list = m_list.getHashList(i);
	if (list)
	    list = list->skipNull();
	// Stop when found a non timed out item:
	//  we put them in the list in ascending order of timeout
	for (; list; list = list->skipNull()) {
	    CacheItem* item = static_cast<CacheItem*>(list->get());
	    if (!item->timeout(time))
		break;
	    dumpItem(*this,*item,"removing timed out");
	    list->remove();
	    m_count--;
	}
    }
    if (oldCount != m_count)
	dump("Cache::expire()");
}

// Add items from NamedList list
// Return the number of added items
unsigned int Cache::add(ObjList& list)
{
    unsigned int added = 0;
    Lock lck(this);
    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	NamedList* nl = static_cast<NamedList*>(o->get());
	if (addUnsafe(*nl,*nl,0,false))
	    added++;
    }
    return added;
}

// Add items from Array rows. Return the number of added rows
unsigned int Cache::addRows(Array& array)
{
    int rows = array.getRows();
    if (rows < 2)
	return 0;
    int cols = array.getColumns();
    if (cols < 1)
	return 0;
    ObjList** columns = new ObjList*[cols];
    String** titles = new String*[cols];
    lock();
    ObjList* params = m_copyParams.split(',',false);
    unlock();
    int colId = -1;
    for (int i = 0; i < cols; i++) {
	columns[i] = array.getColumn(i);
	titles[i] = 0;
	String* title = columns[i] ? YOBJECT(String,columns[i]->get()) : 0;
	if (TelEngine::null(title))
	    continue;
	if (*title == s_id) {
	    colId = i;
	    titles[i] = title;
	}
	else if (*title == YSTRING("expires") || params->find(*title))
	    titles[i] = title;
    }
    TelEngine::destruct(params);
    if (colId < 0) {
	// Don't release columns and titles content: they are owned by the array
	delete[] columns;
	delete[] titles;
	return 0;
    }
    unsigned int added = 0;
    ObjList pending;
    for (int row = 1; row < rows; row++) {
	NamedList* p = new NamedList("");
	for (int i = 0; i < cols; i++) {
	    if (columns[i])
		columns[i] = columns[i]->next();
	    if (!columns[i] || TelEngine::null(titles[i]))
		continue;
	    String* colVal = YOBJECT(String,columns[i]->get());
	    if (!colVal)
		continue;
	    if (i == colId)
		p->assign(*colVal);
	    else
		p->addParam(titles[i]->c_str(),*colVal);
	}
	if (*p)
	    pending.append(p);
	else
	    TelEngine::destruct(p);
	if (0 != (row % 500))
	    continue;
	// Add pending items, take a breath to let others do their job
	added += add(pending);
	pending.clear();
	Thread::idle();
	if (exiting())
	    break;
    }
    // Add remaining items
    added += add(pending);
    // Don't release columns and titles content: they are owned by the array
    delete[] columns;
    delete[] titles;
    return added;
}

// Clear the cache
unsigned int Cache::clear()
{
    Lock lck(this);
    m_list.clear();
    unsigned int n = m_count;
    m_count = 0;
    m_prefixMask = 0;
    return n;
}

// Remove an item
unsigned int Cache::remove(const String& id, bool regexp)
{
    if (!id)
	return 0;
    if (!regexp) {
	Lock lck(this);
	ObjList* list = m_list.getHashList(id);
	GenObject* gen = list ? list->remove(id,false) : 0;
	if (!gen)
	    return 0;
	CacheItem* item = static_cast<CacheItem*>(gen);
	dumpItem(*this,*item,"removed");
	m_count--;
	TelEngine::destruct(item);
	return 1;
    }
    unsigned int removed = 0;
    for (unsigned int i = 0; i < m_list.length(); i++) {
	Lock lck(this);
	ObjList* list = m_list.getHashList(i);
	if (list)
	    list = list->skipNull();
	while (list) {
	    CacheItem* item = static_cast<CacheItem*>(list->get());
	    if (!id.matches(*item)) {
		list = list->skipNext();
		continue;
	    }
	    dumpItem(*this,*item,"removed");
	    list->remove();
	    list = list->skipNull();
	    removed++;
	    m_count--;
	}
	lck.drop();
	if (exiting())
	    break;
	// Someone may need access to the cache
	Thread::idle();
    }
    return removed;
}

// Retrieve cache name
const String& Cache::toString() const
{
    return m_name;
}

// Dump the cache to output if XDEBUG is defined
void Cache::dump(const char* oper)
{
#ifdef XDEBUG
    if (!__plugin.debugAt(DebugAll))
	return;
    Lock lck(locked() ? 0 : this);
    String data("\r\n-----");
    unsigned int n = 0;
    int64_t now = (int64_t)Time::now();
    for (unsigned int i = 0; i < m_list.length(); i++) {
	ObjList* list = m_list.getHashList(i);
	if (list)
	    list = list->skipNull();
	String rowData;
	unsigned int rn = 0;
	for (; list; list = list->skipNext()) {
	    rn++;
	    CacheItem* item = static_cast<CacheItem*>(list->get());
	    String tmp;
	    item->dump(tmp," ");
	    int ttl = (int)(((int64_t)item->expires() - now) / 1000);
	    rowData << "\r\n  " << ttl / 1000 << "." << ttl % 1000 << " " << tmp;
	}
	if (!rn)
	    continue;
	n += rn;
	data << "\r\n" << i + 1 << " (" << rn << ")" << rowData;
    }
    data << "\r\n-----";
    Debug(&__plugin,DebugAll,"Cache '%s' items=%u location='%s' [%p]%s",
	toString().c_str(),n,oper,this,data.c_str());
#endif
}

// Set chunk limit and offset to a query
// Return the number of replaced params
int Cache::setLimits(String& query, unsigned int chunk, unsigned int offset)
{
    NamedList params("");
    params.addParam("chunk",String(chunk));
    params.addParam("offset",String(offset));
    return params.replaceParams(query);
}

void Cache::destroyed()
{
    Debug(&__plugin,DebugInfo,"Cache(%s) destroyed [%p]",m_name.c_str(),this);
    clear();
    TelEngine::destruct(m_reloadItems);
    RefObject::destroyed();
}

// (Re)init
void Cache::doUpdate(const NamedList& params, bool first)
{
    String account;
    String accountLoadCache;
    __plugin.getAccount(account);
    __plugin.getAccount(accountLoadCache,true);
    Lock lck(this);
    if (first) {
	int ttl = safeValue(params.getIntValue("ttl",s_cacheTtlSec));
	m_cacheTtl = (u_int64_t)adjustedCacheTtl(ttl) * 1000000;
    }
    m_limit = adjustedCacheLimit(params.getIntValue("limit",s_limit),m_list.length());
    if (m_limit)
	m_limitOverflow = m_limit + (m_limit / 100);
    else
	m_limitOverflow = 0;
    m_loadChunk = adjustedCacheLoadChunk(params.getIntValue("loadchunk",s_loadChunk));
    m_loadPrio = Thread::priority(params.getValue("loadcache_priority"),s_loadPrio);
    m_idParam = params.getValue("id_param");
    m_copyParams = params.getValue("copyparams");
    m_account = params.getValue("account",account);
    m_accountLoadCache = params.getValue("account_loadcache",accountLoadCache);
    m_queryLoadCache = params.getValue("query_loadcache");
    m_queryLoadItem = params.getValue("query_loaditem");
    m_queryLoadItemCmd = params.getValue("query_loaditem_command",m_queryLoadItem);
    m_querySave = params.getValue("query_save");
    m_queryExpire = params.getValue("query_expire");
    // Minimum sanity check for cache load
    if (m_loadChunk && m_queryLoadCache) {
	String tmp = m_queryLoadCache;
	if (setLimits(tmp,m_loadChunk,0) < 2) {
	    Debug(&__plugin,DebugNote,"Cache(%s) invalid query_loadcache='%s' for loadchunk=%u [%p]",
		m_name.c_str(),m_queryLoadCache.c_str(),m_loadChunk,this);
	    m_loadChunk = 0;
	}
    }
    if ((m_accountLoadCache || m_account) && m_queryLoadCache) {
	unsigned int interval = params.getIntValue("reload_interval");
	if (interval)
	    m_loadInterval = (interval >= CACHE_RELOAD_MIN) ? interval : CACHE_RELOAD_MIN;
	else
	    m_loadInterval = 0;
    }
    else
	m_loadInterval = 0;
    m_prefixMin = params.getIntValue("shortest_prefix",0);
    if (m_prefixMin > 32)
	m_prefixMin = 32;
    String all;
#ifdef DEBUG
    if (m_account) {
	all << " id_param=" << m_idParam;
	all << " loadchunk=" << m_loadChunk;
	all << " account=" << m_account;
	all << " account_loadcache=" << m_accountLoadCache;
	all << " query_loadcache=" << m_queryLoadCache;
	all << " query_loaditem=" << m_queryLoadItem;
	all << " query_loaditem_command=" << m_queryLoadItemCmd;
	all << " query_save=" << m_querySave;
	all << " query_expire=" << m_queryExpire;
	all << " shortest_prefix=" << m_prefixMin;
    }
#endif
    Debug(&__plugin,DebugInfo,
	"Cache(%s) updated ttl=%u limit=%u reload_interval=%u copyparams='%s'%s [%p]",
	m_name.c_str(),(unsigned int)(m_cacheTtl / 1000000),m_limit,m_loadInterval,
	m_copyParams.safe(),all.safe(),this);
}

// Add an item to the cache. Remove an existing one
CacheItem* Cache::addUnsafe(const String& id, const NamedList& params, const String* cpParams,
    bool dbSave)
{
    XDebug(&__plugin,DebugAll,"Cache::add(%s,%p,'%s',%u) [%p]",
	id.c_str(),&params,TelEngine::c_safe(cpParams),dbSave,this);
    unsigned int idx = index(id);
    ObjList* list = m_list.getHashList(idx);
    if (list)
	list = list->skipNull();
    u_int64_t expires = m_cacheTtl;
    if (dbSave) {
	int tmp = params.getIntValue(m_expireParam);
	if (tmp > 0)
	    expires = (u_int64_t)tmp * 1000000;
    }
    else {
	String* exp = params.getParam("expires");
	if (exp) {
	    int tmp = (int)exp->toInteger();
	    if (tmp > 0)
		expires = (u_int64_t)tmp * 1000000;
	    else {
		XDebug(&__plugin,DebugAll,"Cache(%s) item '%s' already expired [%p]",
		    m_name.c_str(),id.c_str(),this);
		return 0;
	    }
	}
    }
    if (expires)
	expires += Time::now();
    // Search for insert/add point and existing item
    ObjList* insert = 0;
    bool found = false;
    while (list) {
	CacheItem* crt = static_cast<CacheItem*>(list->get());
	if (!insert && crt->expires() > expires) {
	    insert = list;
	    if (found)
		break;
	}
	if (!found && id == crt->toString()) {
	    if (crt->expires() > expires) {
		// Deny update for oldest item
		return crt;
	    }
	    if (insert == list)
		insert = 0;
	    list->remove();
	    found = true;
	    if (insert)
		break;
	}
	ObjList* next = list->skipNext();
	if (next)
	    list = next;
	else
	    break;
    }
    CacheItem* item = new CacheItem(id,params,cpParams ? *cpParams : m_copyParams,expires);
    if (insert)
	insert->insert(item);
    else if (list)
	list->append(item);
    else
	m_list.append(item);
    unsigned int len = id.length();
    if (len > 0 && len <= 32)
	m_prefixMask |= (1 << (len - 1));
    if (dbSave && m_account && m_querySave) {
	String query = m_querySave;
	NamedList p(*item);
	p.setParam("id",item->toString());
	p.setParam("expires",String((unsigned int)(m_cacheTtl / 1000000)));
	p.replaceParams(query);
	Message* m = new Message("database");
	m->addParam("account",m_account);
	m->addParam("query",query);
	m->addParam("results",String::boolText(false));
	Engine::enqueue(m);
    }
    dumpItem(*this,*item,!found ? "added" : "updated");
    if (found)
	return item;
    m_count++;
    if (m_limitOverflow && m_count > m_limitOverflow)
	adjustToLimit(item);
    return item;
}

// Add an item from an Array row
CacheItem* Cache::addUnsafe(Array& array, int row, int cols)
{
    XDebug(&__plugin,DebugAll,"Cache::add(%p,%d,%d) [%p]",&array,row,cols,this);
    NamedList p("");
    for (int col = 0; col < cols; col++) {
	String* colName = YOBJECT(String,array.get(col,0));
	if (TelEngine::null(colName))
	    continue;
	String* colVal = YOBJECT(String,array.get(col,row));
	if (!colVal)
	    continue;
	if (*colName == s_id)
	    p.assign(*colVal);
	else
	    p.addParam(*colName,*colVal);
    }
    return p ? addUnsafe(p,p,0,false) : 0;
}

// Find a cache item. This method is not thread safe
CacheItem* Cache::find(const String& id)
{
    ObjList* o = m_list.find(id);
    return o ? static_cast<CacheItem*>(o->get()) : 0;
}

// Find a cache item or prefix. This method is not thread safe
CacheItem* Cache::findPrefix(const String& id)
{
    CacheItem* it = find(id);
    if (it || !m_prefixMin)
	return it;
    unsigned int len = id.length();
    if (len == 0)
	return 0;
    len--;
    if (len > 32)
	len = 32;
    for (; len >= m_prefixMin; len--) {
	if (m_prefixMask & (1 << (len - 1))) {
	    it = find(id.substr(0,len));
	    if (it)
		return it;
	}
    }
    return 0;
}

// Adjust cache length to limit
void Cache::adjustToLimit(CacheItem* skipAdded)
{
    if (!m_limit || m_count <= m_limit)
	return;
    Debug(&__plugin,DebugAll,"Cache(%s) adjusting to limit %u count=%u [%p]",
	m_name.c_str(),m_limit,m_count,this);
    while (m_count > m_limit) {
	CacheItem* found = 0;
	for (unsigned int i = 0; i < m_list.length(); i++) {
	    ObjList* list = m_list.getHashList(i);
	    if (list)
		list = list->skipNull();
	    CacheItem* item = list ? static_cast<CacheItem*>(list->get()) : 0;
	    if (!item || item == skipAdded)
		continue;
	    if (!found || found->m_expires > item->m_expires)
		found = item;
	}
	if (found) {
	    dumpItem(*this,*found,"removing oldest");
	    m_list.remove(found);
	    m_count--;
	    continue;
	}
	Debug(&__plugin,DebugGoOn,
	    "Cache(%s) can't find the oldest item count=%u limit=%u [%p]",
	    m_name.c_str(),m_count,m_limit,this);
	m_count = m_list.count();
	break;
    }
}


/*
 * CacheThread
 */
CacheThread::CacheThread(const char* name, Priority prio)
    : Thread(name,prio)
{
    Lock lck(__plugin);
    s_threads.append(this)->setDelete(false);
}

CacheThread::~CacheThread()
{
    Lock lck(__plugin);
    s_threads.remove(this,false);
}


/*
 * CacheExpireThread
 */
void CacheExpireThread::run()
{
    static const String s_caches[] = {"lnp", "cnam", ""};
    Debug(&__plugin,DebugAll,"%s start running [%p]",currentName(),this);
    u_int64_t nextCheck = Time::now() + s_checkToutInterval;
    while (true) {
	Thread::idle();
	if (exiting())
	    break;
	Time time;
	if (nextCheck > time)
	    continue;
	for (int i = 0; s_caches[i]; i++) {
	    RefPointer<Cache> cache;
	    __plugin.getCache(cache,s_caches[i]);
	    if (cache)
		cache->expire(time);
	    cache = 0;
	}
	nextCheck = time + s_checkToutInterval;
    }
    Debug(&__plugin,DebugAll,"%s stopped [%p]",currentName(),this);
}


/*
 * CacheLoadThread
 */
void CacheLoadThread::run()
{
    Debug(&__plugin,DebugAll,"%s start running cache=%s [%p]",
	currentName(),m_cache.c_str(),this);
    ObjList* items = m_items;
    m_items = 0;
    __plugin.loadCache(m_cache,false,items);
    Debug(&__plugin,DebugAll,"%s stopped cache=%s [%p]",
	currentName(),m_cache.c_str(),this);
}


/*
 * EngineHandler
 */
bool EngineHandler::received(Message& msg)
{
    if (!m_start) {
	Lock lck(__plugin);
	return 0 != CacheThread::s_threads.skipNull();
    }
    s_engineStarted = true;
    __plugin.loadCache("lnp");
    __plugin.loadCache("cnam");
    return false;
}


/*
 * CacheModule
 */
CacheModule::CacheModule()
    : Module("cache"),
    m_haveCacheReload(false), m_lnpCache(0), m_cnamCache(0)
{
    Output("Loaded module Cache");
}

CacheModule::~CacheModule()
{
    Output("Unloading module Cache");
    TelEngine::destruct(m_lnpCache);
    TelEngine::destruct(m_cnamCache);
}

// Build/update a cache
void CacheModule::setupCache(const String& name, const NamedList& params)
{
    Lock lck(this);
    Cache** c = 0;
    bool lnp = (name == "lnp");
    if (lnp)
	c = &m_lnpCache;
    else if (name == "cnam")
	c = &m_cnamCache;
    else
	return;
    bool enabled = params.getBoolValue("enable");
    if (!*c) {
	if (!enabled)
	    return;
	unsigned int size = adjustedCacheSize(params.getIntValue("size",s_size));
	*c = new Cache(name,size,params);
	// Install relays
	if (lnp) {
	    // LnpBefore is an alias for Route
	    installRelay(LnpBefore,params.getIntValue("routebefore",25));
	    installRelay(LnpAfter,"call.route",params.getIntValue("routeafter",75));
	}
	else {
	    installRelay(CnamBefore,"call.preroute",params.getIntValue("routebefore",25));
	    installRelay(CnamAfter,"call.preroute",params.getIntValue("routeafter",75));
	}
	if (s_engineStarted)
	    loadCache(name);
	lck.drop();
	updateCacheReload();
	return;
    }
    if (enabled) {
	RefPointer<Cache> cache = *c;
	lck.drop();
	cache->update(params);
	cache = 0;
    }
    else
	TelEngine::destruct(*c);
    lck.drop();
    updateCacheReload();
}

// Start cache load thread
// Optionally load specific items only (the list will be consumed)
void CacheModule::loadCache(const String& name, bool async, ObjList* items)
{
    XDebug(this,DebugAll,"loadCache(%s,%u)",name.c_str(),async);
    RefPointer<Cache> cache;
    getCache(cache,name);
    if (!cache) {
	TelEngine::destruct(items);
	return;
    }
    String account;
    String query;
    unsigned int chunk = 0;
    Thread::Priority prio = Thread::Normal;
    if (!items)
	cache->getDbLoad(account,query,chunk,prio);
    else
	cache->getDbLoadItemCmd(account,query,prio);
    if (!(account && query)) {
	TelEngine::destruct(items);
	cache = 0;
	return;
    }
    if (async) {
	cache = 0;
	(new CacheLoadThread(name,prio,items))->startup();
	return;
    }
    bool load = cache->startLoad();
    cache = 0;
    if (!load) {
	TelEngine::destruct(items);
	return;
    }
    unsigned int loaded = 0;
    unsigned int failed = 0;
    unsigned int offset = 0;
    unsigned int max = 0;
    ObjList* crtItem = 0;
    if (!items)
	max = chunk ? s_maxChunks : 1;
    else {
	max = items->count();
	crtItem = items->skipNull();
    }
    Debug(this,DebugInfo,"Loading cache '%s' %s=%u",
	name.c_str(),(!items ? "chunks" : "items"),max);
    // NOTE: Don't return from the loop: we must notify the cache
    for (unsigned int i = 0; i < max; i++) {
	String* id = 0;
	Message m("database");
	m.addParam("account",account);
	if (!items) {
	    if (chunk) {
		String tmp = query;
		Cache::setLimits(tmp,chunk,offset);
		m.addParam("query",tmp);
	    }
	    else
		m.addParam("query",query);
	}
	else if (crtItem) {
	    id = static_cast<String*>(crtItem->get());
	    crtItem = crtItem->skipNext();
	    if (TelEngine::null(id))
		continue;
	    NamedList p("");
	    p.addParam("id",*id);
	    String tmp = query;
	    p.replaceParams(tmp);
	    m.addParam("query",tmp);
	}
	else
	    break;
	bool ok = Engine::dispatch(m);
	if (exiting())
	    break;
	const char* error = m.getValue("error");
	if (!ok || error) {
	    Debug(this,DebugNote,"Failed to load cache '%s' reason=%s",
		name.c_str(),TelEngine::c_safe(error));
	    break;
	}
	getCache(cache,name);
	if (!cache) {
	    Debug(this,DebugInfo,"Cache '%s' vanished while loading",name.c_str());
	    break;
	}
	Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
	int rows = a ? a->getRows() : 0;
	unsigned int loadedRows = (rows > 0) ? rows - 1 : 0;
	if (!items)
	    Debug(this,DebugAll,"Loaded %u rows current chunk=%u for cache '%s'",
		loadedRows,i + 1,name.c_str());
	else
	    Debug(this,DebugAll,"Loaded %u rows id='%s' for cache '%s'",
		loadedRows,id->c_str(),name.c_str());
	if (!loadedRows) {
	    cache = 0;
	    if (!items)
		break;
	    else {
		failed++;
		continue;
	    }
	}
	offset += loadedRows;
	loaded += loadedRows;
	unsigned int added = cache->addRows(*a);
	cache = 0;
	if (added < loadedRows)
	    failed += loadedRows - added;
	if (exiting())
	    break;
	// Stop if got less then requested
	if (chunk && loadedRows < chunk)
	    break;
    }
    bool triggerReload = (items == 0);
    TelEngine::destruct(items);
    getCache(cache,name);
    if (!cache)
	return;
    cache->endLoad(triggerReload);
    cache->dump("CacheModule::loadCache()");
    u_int32_t mask = cache->prefixMask();
    cache = 0;
    Debug(this,DebugInfo,"Loaded %u items (failed=%u) in cache '%s', mask 0x%X",
	loaded,failed,name.c_str(),mask);
    updateCacheReload();
}

void CacheModule::initialize()
{
    static bool s_first = true;
    static bool s_init = true;
    static bool s_createExpire = true;
    Output("Initializing module Cache");
    Configuration cfg(Engine::configFile("cache"));
    // Globals
    s_size = adjustedCacheSize(cfg.getIntValue("general","size",17));
    s_limit = adjustedCacheLimit(cfg.getIntValue("general","limit",s_limit),s_size);
    s_loadChunk = adjustedCacheLoadChunk(cfg.getIntValue("general","loadchunk"));
    s_maxChunks = safeValue(cfg.getIntValue("general","maxchunks",1000));
    if (!s_maxChunks)
	s_maxChunks = 1;
    else if (s_maxChunks > 10000)
	s_maxChunks = 10000;
    s_loadPrio = Thread::priority(cfg.getValue("general","loadcache_priority"));
    s_cacheTtlSec = adjustedCacheTtl(cfg.getIntValue("general","ttl"));
    unsigned int tmp = safeValue(cfg.getIntValue("general","expire_check_interval",10));
    if (tmp > s_cacheTtlSec)
	tmp = s_cacheTtlSec;
    if (tmp >= 1 && tmp <= EXPIRE_CHECK_MAX)
	s_checkToutInterval = tmp * 1000000;
    else if (tmp)
	s_checkToutInterval = EXPIRE_CHECK_MAX * 1000000;
    else
	s_checkToutInterval = 1000000;
    lock();
    m_account = cfg.getValue("general","account");
    m_accountLoadCache = cfg.getValue("general","account_loadcache");
    unlock();
    // Update cache objects
    NamedList* lnp = cfg.getSection("lnp");
    if (lnp) {
	// Set default params
	if (!lnp->getValue("copyparams"))
	    lnp->setParam("copyparams","routing");
	if (!lnp->getValue("id_param"))
	    lnp->setParam("id_param","${called}");
	setupCache(*lnp,*lnp);
	s_lnpStoreFailed = lnp->getBoolValue("store_failed_requests");
	s_lnpStoreNpdiBefore = lnp->getBoolValue("store_npdi_before");
    }
    NamedList* cnam = cfg.getSection("cnam");
    if (cnam) {
	// Set default params
	if (!cnam->getValue("copyparams"))
	    cnam->setParam("copyparams","callername");
	if (!cnam->getValue("id_param"))
	    cnam->setParam("id_param","${caller}");
	setupCache(*cnam,*cnam);
	s_cnamStoreEmpty = cnam->getBoolValue("store_empty");
    }
    // Init module
    if (s_first) {
	// Install now basic relays
	installRelay(Status,110);
	installRelay(Level,120);
	installRelay(Command,120);
	installRelay(Help,120);
	Engine::install(new EngineHandler(true));
	Engine::install(new EngineHandler(false));
	s_first = false;
    }
    if (s_init) {
	// Setup if we have a cache
	lock();
	bool ok = m_lnpCache || m_cnamCache;
	unlock();
	if (ok) {
	    DDebug(this,DebugAll,"Initializing");
	    setup();
	    s_init = false;
	}
    }
    if (!s_init && s_createExpire) {
	// Create expire thread if we have a cache with non 0 TTL
	lock();
	bool ok = (m_lnpCache && m_lnpCache->cacheTtl()) ||
	    (m_cnamCache && m_cnamCache->cacheTtl());
	unlock();
	if (ok) {
	    DDebug(this,DebugAll,"Creating expire thread");
	    (new CacheExpireThread)->startup();
	    s_createExpire = false;
	}
    }
}

bool CacheModule::received(Message& msg, int id)
{
    if (id == LnpBefore || id == LnpAfter) {
	handleLnp(msg,id == LnpBefore);
	return false;
    }
    if (id == CnamBefore || id == CnamAfter) {
	handleCnam(msg,id == CnamBefore);
	return false;
    }
    if (id == Help)
	return commandHelp(msg.retValue(),msg[YSTRING("line")]);
    if (id == Timer) {
	if (m_haveCacheReload) {
	    for (int i = 0; s_caches[i]; i++) {
		RefPointer<Cache> cache;
		getCache(cache,s_caches[i]);
		if (cache)
		    cache->reload(msg.msgTime());
		cache = 0;
	    }
	}
    }
    return Module::received(msg,id);
}

void CacheModule::statusModule(String& buf)
{
    static const String s_params = "format=Count";
    Module::statusModule(buf);
    buf.append(s_params,",");
}

void CacheModule::statusParams(String& buf)
{
    unsigned int count = 0;
    if (m_lnpCache)
	count++;
    if (m_cnamCache)
	count++;
    String tmp("caches=");
    tmp << count;
    buf.append(tmp,";");
}

void CacheModule::statusDetail(String& buf)
{
    addCacheDetail(buf,m_lnpCache);
    addCacheDetail(buf,m_cnamCache);
}

bool CacheModule::commandExecute(String& retVal, const String& line)
{
    String name = line;
    if (!name.startSkip(this->name()))
	return Module::commandExecute(retVal,line);
    name.trimBlanks();
    int cmd = 0;
    for (; cmd < CmdCount; cmd++)
	if (name.startSkip(s_cmd[cmd]))
	    break;
    if (cmd >= CmdCount)
	return Module::commandExecute(retVal,line);
    name.trimBlanks();
    if (!name) {
	retVal << "Usage: " << s_cmdFormat[cmd] << "\r\n" << s_cmdHelp[cmd] << "\r\n";
	return true;
    }
    NamedList params("");
    fillList(params,name);
    RefPointer<Cache> cache;
    getCache(cache,name);
    if (cache) {
	String buf;
	params.dump(buf," ");
	Debug(this,DebugAll,"Executing command '%s' for cache '%s' params '%s'",
	    s_cmd[cmd].c_str(),cache->toString().c_str(),buf.safe());
	switch (cmd) {
	    case CmdLoad:
		commandLoad(cache,params,retVal);
		break;
	    case CmdFlush:
		commandFlush(cache,params,retVal);
		break;
	    default:
		retVal << "Command not implemented!!!";
	}
	cache = 0;
    }
    else
	retVal << "Cache not found";
    retVal << "\r\n";
    return true;
}

bool CacheModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (!partLine || partLine == YSTRING("help")) {
	Module::itemComplete(msg.retValue(),name(),partWord);
	return Module::commandComplete(msg,partLine,partWord);
    }
    // Line is module name: complete module commands
    if (partLine == name()) {
	for (int cmd = 0; cmd < CmdCount; cmd++)
	    Module::itemComplete(msg.retValue(),s_cmd[cmd],partWord);
	return Module::commandComplete(msg,partLine,partWord);
    }
    if (!partLine.startsWith(name(),true))
	return Module::commandComplete(msg,partLine,partWord);
    for (int cmd = 0; cmd < CmdCount; cmd++) {
	String tmp = name() + " " + s_cmd[cmd];
	if (!partLine.startsWith(tmp))
	    continue;
	String rest = partLine.substr(tmp.length()).trimBlanks();
	if (rest)
	    return false;
	Lock lck(this);
	for (int i = 0; s_caches[i]; i++)
	    if (findCache(s_caches[i]))
		Module::itemComplete(msg.retValue(),s_caches[i],partWord);
	return false;
    }
    return false;
}

// Find a cache. This method is not thread safe
Cache* CacheModule::findCache(const String& name)
{
    if (name == YSTRING("lnp"))
	return m_lnpCache;
    if (name == YSTRING("cnam"))
	return m_cnamCache;
    return 0;
}

// Update cache reload flag
void CacheModule::updateCacheReload()
{
    bool ok = false;
    for (int i = 0; !ok && s_caches[i]; i++) {
	RefPointer<Cache> cache;
	getCache(cache,s_caches[i]);
	ok = cache && cache->canReload();
	cache = 0;
    }
    Lock lck(this);
    if (m_haveCacheReload == ok)
	return;
    m_haveCacheReload = ok;
    DDebug(this,DebugAll,"Cache reload handler is %u",m_haveCacheReload);
}

// Add a cache to detail
void CacheModule::addCacheDetail(String& buf, Cache* cache)
{
    if (!cache)
	return;
    Lock lock(cache);
    buf.append(cache->toString() + "=" + String(cache->count()),";");
}

// Handle messages for LNP
void CacheModule::handleLnp(Message& msg, bool before)
{
    bool handle = false;
    if (before)
	handle = msg.getBoolValue("querylnp_cache",true);
    else
	handle = msg.getBoolValue("cache_lnp_posthook");
    XDebug(this,DebugAll,"handleLnp(%u) handle=%u",before,handle);
    if (!handle)
	return;
    RefPointer<Cache> lnp;
    getCache(lnp,"lnp");
    if (!lnp)
	return;
    String id;
    if (!lnp->replaceIdParam(id,msg)) {
	lnp = 0;
	return;
    }
    Debug(this,DebugAll,"handleLnp(%s) id=%s routing=%s querylnp=%s npdi=%s",
	(before ? "before" : "after"),id.c_str(),
	msg.getValue("routing"),msg.getValue("querylnp"),msg.getValue("npdi"));
    bool querylnp = msg.getBoolValue("querylnp");
    if (before) {
	if (querylnp) {
	    // LNP requested: check the cache
	    if (lnp->copyParams(id,msg,msg.getParam("cache_lnp_parameters"))) {
		msg.setParam("querylnp",String::boolText(false));
		msg.setParam("npdi",String::boolText(true));
	    }
	    else
		msg.setParam("cache_lnp_posthook",String::boolText(true));
	}
	else if (msg.getBoolValue("npdi") &&
	    msg.getBoolValue("cache_lnp_store",s_lnpStoreNpdiBefore)) {
	    // LNP already done: update cache
	    lnp->add(id,msg,msg.getParam("cache_lnp_parameters"));
	    lnp->dump("CacheModule::handleLnp('before')");
	}
    }
    else if (!querylnp || s_lnpStoreFailed || msg.getBoolValue("npdi")) {
	// querylnp=true: request failed
	// LNP query made locally: update cache
	lnp->add(id,msg,msg.getParam("cache_lnp_parameters"));
	lnp->dump("CacheModule::handleLnp('after')");
    }
    lnp = 0;
}

// Handle messages for CNAM
void CacheModule::handleCnam(Message& msg, bool before)
{
    bool handle = false;
    if (before)
	handle = msg.getBoolValue("querycnam_cache",true);
    else
	handle = msg.getBoolValue("cache_cnam_posthook");
    XDebug(this,DebugAll,"handleCnam(%u) handle=%u",before,handle);
    if (!handle)
	return;
    RefPointer<Cache> cnam;
    getCache(cnam,"cnam");
    if (!cnam)
	return;
    String id;
    if (!cnam->replaceIdParam(id,msg)) {
	cnam = 0;
	return;
    }
    Debug(this,DebugAll,"handleCnam(%s) id=%s callername=%s querycnam=%s",
	(before ? "before" : "after"),id.c_str(),
	msg.getValue("callername"),msg.getValue("querycnam"));
    bool querycnam = msg.getBoolValue("querycnam");
    if (before) {
	if (querycnam) {
	    // CNAM requested: check the cache
	    if (cnam->copyParams(id,msg,msg.getParam("cache_cnam_parameters")))
		msg.setParam("querycnam",String::boolText(false));
	    else
		msg.setParam("cache_cnam_posthook",String::boolText(true));
	}
    }
    else if (!querycnam && (s_cnamStoreEmpty || msg.getValue("callername"))) {
	// querycnam=true: request failed
	// CNAM query made locally: update cache
	cnam->add(id,msg,msg.getParam("cache_cnam_parameters"));
	cnam->dump("CacheModule::handleCnam('after')");
    }
    cnam = 0;
}

// Cache load handler
void CacheModule::commandLoad(Cache* cache, NamedList& params, String& retVal)
{
    if (!cache)
	return;
    if (s_engineStarted && cache->scheduleLoad(params)) {
	updateCacheReload();
	retVal << "Cache load scheduled";
    }
    else
	retVal << "Failed to schedule cache load";
}

// Cache flush handler
void CacheModule::commandFlush(Cache* cache, NamedList& params, String& retVal)
{
    if (!cache)
	return;
    unsigned int n = 0;
    if (!(params.getParam(s_id) || params.getParam(s_regexp)))
	n = cache->clear();
    else {
	NamedIterator iter(params);
	for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	    if (!*ns)
		continue;
	    if (ns->name() == s_id)
		n += cache->remove(*ns);
	    else if (ns->name() == s_regexp) {
		Regexp r(*ns);
		if (r.compile())
		    n += cache->remove(r,true);
		else
		    Debug(this,DebugNote,"Invalid regexp=%s in flush command",ns->c_str());
	    }
	}
    }
    cache->dump("CacheModule::commandFlush()");
    retVal << "Flushed " << n << " item(s)";
}

// Help message handler
bool CacheModule::commandHelp(String& retVal, const String& line)
{
    if (line) {
	if (line != name())
	    return false;
	for (int cmd = 0; cmd < CmdCount; cmd++) {
	    retVal << "  " << s_cmdFormat[cmd] << "\r\n";
	    retVal << s_cmdHelp[cmd] << "\r\n";
	}
	return true;
    }
    retVal << "  " << s_cmdCacheFormat << "\r\n";
    return false;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
