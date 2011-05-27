/**
 * cache.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Cache implementation
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2011 Null Team
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

#include <yatephone.h>


using namespace TelEngine;
namespace { // anonymous

class CacheItem;                         // A cache item
class Cache;                             // A cache hash list
class CacheExpireThread;                 // Cache expire thread
class CacheLoadThread;                   // Cache load thread
class EngineStartHandler;                // engine.start handler
class CacheModule;

// Max value for cache expire check interval
#define EXPIRE_CHECK_MAX 300

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
    // Retrieve the mutex protecting a given list
    inline unsigned int index(const String& str) const
	{ return str.hash() % m_list.length(); }
    // Safely retrieve DB load info
    inline void getDbLoad(String& account, String& query) {
	    Lock lock(this);
	    account = m_account;
	    query = m_queryLoadCache;
	}
    // Reinit
    inline void update(const NamedList& params)
	{ doUpdate(params,false); }
    // Expire entries
    void expire(const Time& time);
    // Copy params from cache item. Return true if found
    bool copyParams(const String& id, NamedList& list, const String* cpParams);
    // Add an item to the cache. Remove an existing one
    // Set dbSave=false when loading from database to avoid saving it again
    void add(const String& id, const NamedList& params, const String* cpParams,
	bool dbSave = true) {
	    Lock lock(this);
	    addUnsafe(id,params,cpParams,dbSave);
	}
    // Add an item from an Array row
    inline void add(Array& array, int row, int cols) {
	    Lock lock(this);
	    addUnsafe(array,row,cols);
	}
    // Clear the cache
    void clear();
    // Retrieve cache name
    virtual const String& toString() const;
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
    // Adjust cache length to limit
    void adjustToLimit(CacheItem* skipAdded);

    String m_name;                       // Cache name
    HashList m_list;                     // The list holding the cache
    u_int64_t m_cacheTtl;                // Cache item TTL (in us)
    unsigned int m_count;                // Current number of items
    unsigned int m_limit;                // Limit the number of cache items
    unsigned int m_limitOverflow;        // Allowed limit overflow
    String m_copyParams;                 // Item parameters to store/copy
    String m_account;                    // Database account
    String m_queryLoadCache;             // Database load all cache query
    String m_queryLoadItem;              // Database load a cache item query
    String m_querySave;                  // Database save query
    String m_queryExpire;                // Database expire query
};

class CacheExpireThread : public Thread
{
public:
    inline CacheExpireThread()
	: Thread("CacheExpireThread")
	{}
    virtual void run();
};

class CacheLoadThread : public Thread
{
public:
    inline CacheLoadThread(const String name)
	: Thread("CacheLoadThread"), m_cache(name)
	{}
    virtual void run();
private:
    String m_cache;
};

class EngineStartHandler : public MessageHandler
{
public:
    inline EngineStartHandler()
	: MessageHandler("engine.start")
	{}
    virtual bool received(Message& msg);
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
    inline void getAccount(String& buf) {
	    Lock lock(this);
	    buf = m_account;
	}
    // Safely retrieve a reference to a cache
    inline void getCache(RefPointer<Cache>& c, const String& name) {
	    Lock lock(this);
	    if (name == "lnp")
		c = m_lnpCache;
	    else if (name == "cnam")
		c = m_cnamCache;
	}
    // Build/update a cache
    void setupCache(const String& name, const NamedList& params);
    // Load a cache from database
    // Set async=false from loading thread
    void loadCache(const String& name, bool async = true);
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusModule(String& buf);
    virtual void statusParams(String& buf);
    virtual void statusDetail(String& buf);
    // Add a cache to detail
    void addCacheDetail(String& buf, Cache* cache);
    // Handle messages for LNP
    void handleLnp(Message& msg, bool before);
    // Handle messages for CNAM
    void handleCnam(Message& msg, bool before);

    String m_account;                    // Database account
    Cache* m_lnpCache;                   // LNP cache
    Cache* m_cnamCache;                  // CNAM cache
};


INIT_PLUGIN(CacheModule);                // The module
static bool s_engineStarted = false;     // Engine started flag
static bool s_lnpStoreFailed = false;    // Store failed LNP requests
static bool s_lnpStoreNpdiBefore = true; // Store LNP when already done
static bool s_cnamStoreEmpty = false;    // Store empty caller name in CNAM cache
static unsigned int s_size = 0;          // The number of listst in each cache
static unsigned int s_limit = 0;         // Default cache limit
static unsigned int s_cacheTtlSec = 0;   // Default cache item time to live (in seconds)
static u_int64_t s_checkToutInterval = 0;// Interval to check cache timeout

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
    return val > 10 ? val : 10;
}

// Show cache item changes to output
static inline void dumpItem(Cache& c, CacheItem& item, const char* oper)
{
#ifdef DEBUG
    String tmp;
    item.dump(tmp," ");
    Debug(&__plugin,DebugAll,"Cache(%s) %s %p %s expires=%u [%p]",
	c.toString().c_str(),oper,&item,tmp.c_str(),
	(unsigned int)(item.expires()/1000000),&c);
#endif
}


/*
 * Cache
 */
Cache::Cache(const String& name, int size, const NamedList& params)
    : Mutex(false,"Cache"),
    m_name(name), m_list(size), m_cacheTtl(0), m_count(0), m_limit(0),
    m_limitOverflow(0)
{
    Debug(&__plugin,DebugInfo,"Cache(%s) size=%u [%p]",
	m_name.c_str(),m_list.length(),this);
    doUpdate(params,true);
}

// Copy params from cache item. Return true if found
bool Cache::copyParams(const String& id, NamedList& list, const String* cpParams)
{
    lock();
    CacheItem* item = find(id);
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
	    Array* a = static_cast<Array*>(m.userObject("Array"));
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
	Engine::enqueue(m);
    }
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
}

// Clear the cache
void Cache::clear()
{
    Lock lck(this);
    m_list.clear();
    m_count = 0;
}

// Retrieve cache name
const String& Cache::toString() const
{
    return m_name;
}

void Cache::destroyed()
{
    Debug(&__plugin,DebugInfo,"Cache(%s) destroyed [%p]",m_name.c_str(),this);
    clear();
    RefObject::destroyed();
}

// (Re)init
void Cache::doUpdate(const NamedList& params, bool first)
{
    String account;
    __plugin.getAccount(account);
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
    m_copyParams = params.getValue("copyparams");
    m_account = params.getValue("account",account);
    m_queryLoadCache = params.getValue("query_loadcache");
    m_queryLoadItem = params.getValue("query_loaditem");
    m_querySave = params.getValue("query_save");
    m_queryExpire = params.getValue("query_expire");
    String all;
#ifdef DEBUG
    if (m_account) {
	all << " copyparams=" << m_copyParams;
	all << " account=" << m_account;
	all << " query_loadcache=" << m_queryLoadCache;
	all << " query_loaditem=" << m_queryLoadItem;
	all << " query_save=" << m_querySave;
	all << " query_expire=" << m_queryExpire;
    }
#endif
    Debug(&__plugin,DebugInfo,"Cache(%s) updated ttl=%u limit=%u%s [%p]",
	m_name.c_str(),(unsigned int)(m_cacheTtl / 1000000),m_limit,all.safe(),this);
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
    u_int64_t expires = 0;
    if (!dbSave) {
	String* exp = params.getParam("expires");
	if (exp) {
	    int tmp = (int)exp->toInteger();
	    if (tmp > 0)
		expires = Time::now() + tmp * 1000000;
	    else {
		XDebug(&__plugin,DebugAll,"Cache(%s) item '%s' already expired [%p]",
		    m_name.c_str(),id.c_str(),this);
		return 0;
	    }
	}
    }
    if (!expires)
	expires = Time::now() + m_cacheTtl;
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
    if (dbSave && m_account && m_querySave) {
	String query = m_querySave;
	NamedList p(*item);
	p.setParam("id",item->toString());
	p.setParam("expires",String((unsigned int)(m_cacheTtl / 1000000)));
	p.replaceParams(query);
	Message* m = new Message("database");
	m->addParam("account",m_account);
	m->addParam("query",query);
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
	if (*colName == "id")
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
    __plugin.loadCache(m_cache,false);
    Debug(&__plugin,DebugAll,"%s stopped cache=%s [%p]",
	currentName(),m_cache.c_str(),this);
}


/*
 * EngineStartHandler
 */
bool EngineStartHandler::received(Message& msg)
{
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
    m_lnpCache(0), m_cnamCache(0)
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
}

// Start cache load thread
void CacheModule::loadCache(const String& name, bool async)
{
    XDebug(this,DebugAll,"loadCache(%s,%u)",name.c_str(),async);
    RefPointer<Cache> cache;
    getCache(cache,name);
    if (!cache)
	return;
    String account;
    String query;
    cache->getDbLoad(account,query);
    cache = 0;
    if (!(account && query))
	return;
    if (async) {
	cache = 0;
	(new CacheLoadThread(name))->startup();
	return;
    }
    Debug(this,DebugInfo,"Loading cache '%s'",name.c_str());
    Message m("database");
    m.addParam("account",account);
    m.addParam("query",query);
    bool ok = Engine::dispatch(m);
    if (exiting())
	return;
    const char* error = m.getValue("error");
    if (!ok || error) {
	Debug(this,DebugNote,"Failed to load cache '%s' %s",
	    name.c_str(),TelEngine::c_safe(error));
	return;
    }
    getCache(cache,name);
    if (!cache) {
	Debug(this,DebugInfo,"Cache '%s' vanished while loading",name.c_str());
	return;
    }
    Array* a = static_cast<Array*>(m.userObject("Array"));
    int rows = a ? a->getRows() : 0;
    int cols = a ? a->getColumns() : 0;
    for (int row = 1; row < rows; row++) {
	if (exiting())
	    break;
	cache->add(*a,row,cols);
	// Take a breath, let others do their job
	if (0 == (row % 500))
	    Thread::idle();
    }
    Debug(this,DebugInfo,"Loaded %d items in cache '%s'",rows ? rows - 1 : 0,name.c_str());
    cache = 0;
}

void CacheModule::initialize()
{
    static bool s_first = true;
    static bool s_init = true;
    Output("Initializing module Cache");
    Configuration cfg(Engine::configFile("cache"));
    // Globals
    s_size = adjustedCacheSize(cfg.getIntValue("general","size",17));
    s_limit = adjustedCacheLimit(cfg.getIntValue("general","limit",s_limit),s_size);
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
    unlock();
    // Update cache objects
    NamedList* lnp = cfg.getSection("lnp");
    if (lnp) {
	// Set default copyparams
	if (!lnp->getValue("copyparams"))
	    lnp->setParam("copyparams","routing");
	setupCache(*lnp,*lnp);
	s_lnpStoreFailed = lnp->getBoolValue("store_failed_requests");
	s_lnpStoreNpdiBefore = lnp->getBoolValue("store_npdi_before");
    }
    NamedList* cnam = cfg.getSection("cnam");
    if (cnam) {
	// Set default copyparams
	if (!cnam->getValue("copyparams"))
	    cnam->setParam("copyparams","callername");
	setupCache(*cnam,*cnam);
	s_cnamStoreEmpty = cnam->getBoolValue("store_empty");
    }
    // Init module
    if (s_first) {
	// Install now basic relays
	installRelay(Status,110);
	installRelay(Level,120);
	installRelay(Command,120);
	Engine::install(new EngineStartHandler);
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
	    // Expire thread
	    (new CacheExpireThread)->startup();
	    s_init = false;
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
    if (!(before || msg.getBoolValue("cache_lnp_posthook")))
	return;
    const String& called = msg["called"];
    if (!called)
	return;
    RefPointer<Cache> lnp;
    getCache(lnp,"lnp");
    if (!lnp)
	return;
    Debug(this,DebugAll,"handleLnp(%s) called=%s routing=%s querylnp=%s npdi=%s",
	(before ? "before" : "after"),msg.getValue("called"),
	msg.getValue("routing"),msg.getValue("querylnp"),msg.getValue("npdi"));
    bool querylnp = msg.getBoolValue("querylnp");
    if (before) {
	if (querylnp) {
	    // LNP requested: check the cache
	    if (lnp->copyParams(called,msg,msg.getParam("cache_lnp_parameters")))
		msg.setParam("querylnp",String::boolText(false));
	    else
		msg.setParam("cache_lnp_posthook",String::boolText(true));
	}
	else if (msg.getBoolValue("npdi") &&
	    msg.getBoolValue("cache_lnp_store",s_lnpStoreNpdiBefore)) {
	    // LNP already done: update cache
	    lnp->add(called,msg,msg.getParam("cache_lnp_parameters"));
	}
    }
    else if (!querylnp || s_lnpStoreFailed || msg.getBoolValue("npdi")) {
	// querylnp=true: request failed
	// LNP query made locally: update cache
	lnp->add(called,msg,msg.getParam("cache_lnp_parameters"));
    }
    lnp = 0;
}

// Handle messages for CNAM
void CacheModule::handleCnam(Message& msg, bool before)
{
    if (!(before || msg.getBoolValue("cache_cnam_posthook")))
	return;
    const String& caller = msg["caller"];
    if (!caller)
	return;
    RefPointer<Cache> cnam;
    getCache(cnam,"cnam");
    if (!cnam)
	return;
    Debug(this,DebugAll,"handleCnam(%s) caller=%s callername=%s querycnam=%s",
	(before ? "before" : "after"),msg.getValue("caller"),
	msg.getValue("callername"),msg.getValue("querycnam"));
    bool querycnam = msg.getBoolValue("querycnam");
    if (before) {
	if (querycnam) {
	    // CNAM requested: check the cache
	    if (cnam->copyParams(caller,msg,msg.getParam("cache_cnam_parameters")))
		msg.setParam("querycnam",String::boolText(false));
	    else
		msg.setParam("cache_cnam_posthook",String::boolText(true));
	}
    }
    else if (!querycnam && (s_cnamStoreEmpty || msg.getValue("callername"))) {
	// querycnam=true: request failed
	// CNAM query made locally: update cache
	cnam->add(caller,msg,msg.getParam("cache_cnam_parameters"));
    }
    cnam = 0;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
