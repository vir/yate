/**
 * fileinfo.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * File info holder
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2013-2014 Null Team
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

class FIItem;
class FIDirectory;
class FIFileData;
class FIFile;
class FileInfoMsgHandler;
class FIAccount;                         // Holds account data and share items
class FileInfo;

class ResultSetMngt
{
public:
    inline ResultSetMngt()
	{ reset(); }
    inline ResultSetMngt(const NamedList& list)
	{ reset(&list); }
    void reset(const NamedList* list = 0);
    bool m_addRsm;
    int m_max;
    int m_index;
};

class FIFileData : public RefObject
{
    YNOCOPY(FIFileData);
public:
    inline FIFileData(const char* file, u_int64_t size, unsigned int time,
	const char* desc = 0)
	: m_file(file), m_size(size), m_time(time),
	m_description(desc)
	{}
    inline FIFileData(const char* file, const char* desc = 0)
	: m_file(file), m_size(0), m_time(0), m_description(desc)
	{}
    inline const String& file() const
	{ return m_file; }
    inline u_int64_t size() const
	{ return m_size; }
    inline unsigned int time() const
	{ return m_time; }
    inline const String& description() const
	{ return m_description; }
    void addToList(NamedList& list, const String& prefix) const;
    bool operator==(const FIFileData& other);
    inline bool operator!=(const FIFileData& other)
	{ return !operator==(other); }
    virtual const String& toString() const
	{ return m_file; }
    static FIFileData* build(const char* file, const char* desc = 0, int* error = 0);
protected:
    String m_file;                       // File path and name
    u_int64_t m_size;                    // File size
    unsigned int m_time;                 // File time
    String m_description;                // File description

private:
    FIFileData() {}                      // No default contructor
};

class FIItem : public RefObject
{
public:
    inline FIItem(const char* name)
	: m_name(name)
	{}
    inline const String& name() const
	{ return m_name; }
    virtual FIDirectory* directory()
	{ return 0; }
    virtual FIFile* file()
	{ return 0; }
    virtual const String& toString() const
	{ return name(); }
private:
    FIItem() {}
    String m_name;
};

class FIDirectory : public FIItem
{
    friend class FileInfo;
public:
    FIDirectory(const char* name, const char* path, bool setMutex = false, bool updated = false);
    ~FIDirectory();
    inline const String& path() const
	{ return m_path; }
    inline Mutex* mutex()
	{ return m_mutex; }
    inline bool lock(long maxwait = -1)
	{ return m_mutex && m_mutex->lock(maxwait); }
    inline bool unlock()
	{ return m_mutex && m_mutex->unlock(); }
    inline bool updated() const
	{ return m_updated; }
    // Update content from file system
    void update();
    // Add/replace an item. This method is not thread safe
    bool setItemUnsafe(FIItem* item, const String& oldName);
    // Remove an item. This method is not thread safe
    bool removeUnsafe(const String& itemName);
    // Find a directory by path. This method is not thread safe
    FIDirectory* findDirPath(const String& path);
    // Find a file by path. This method is not thread safe
    FIFile* findFilePath(const String& path);
    // Clear children
    inline void clear()
	{ m_children.clear(); }
    virtual FIDirectory* directory()
	{ return this; }
    void addDirInfoRsp(NamedList& list, const ResultSetMngt& rsm);
    // Append FIItem data to a list of parameters
    static void addFIItem(NamedList& list, FIItem* fi, bool full = true,
	const String& prefix = String::empty());

protected:
    inline FIItem* findChild(const String& name) {
	    ObjList* o = m_children.find(name);
	    return o ? static_cast<FIItem*>(o->get()) : 0;
	}
    inline FIDirectory* findDir(const String& name) {
	    FIItem* ch = findChild(name);
	    return ch ? ch->directory() : 0;
	}
    inline FIFile* findFile(const String& name) {
	    FIItem* ch = findChild(name);
	    return ch ? ch->file() : 0;
	}
    // Add a file
    // Replace file data if already in the list and changed
    FIFile* internalAddFile(FIFileData* fd, const String& name);

    String m_path;
    Mutex* m_mutex;
    bool m_updated;
    ObjList m_children;
};

class FIFile : public FIItem
{
    friend class FileInfo;
public:
    inline FIFile(const char* name, FIFileData* data)
	: FIItem(name), m_data(0) {
	    if (data && data->ref())
		m_data = data;
	}
    virtual ~FIFile()
	{ TelEngine::destruct(m_data); }
    inline const FIFileData* data() const
	{ return m_data; }
    virtual FIFile* file()
	{ return this; }
protected:
    FIFileData* m_data;
};

class FileInfoMsgHandler : public MessageHandler
{
public:
    // Message handlers
    // Non-negative enum values will be used as handler priority
    enum {
	EngineStart = -1,
	FileInfo = -2,
	CallRoute = 90,
    };
    FileInfoMsgHandler(int handler, int prio);
protected:
    virtual bool received(Message& msg);
private:
    int m_handler;
};

// Holds account data and share items
class FIAccount : public RefObject, public Mutex
{
public:
    FIAccount(const char* name);
    inline const String& name() const
	{ return m_name; }
    inline bool canRoute() const
	{ return m_canRoute; }
    inline void canRoute(bool val)
	{ m_canRoute = val; }
    // Handle file info set
    bool handleFileInfoSet(const NamedList& list);
    // Handle file info remove
    bool handleFileInfoRemove(const NamedList& list, const String& contact);
    // Handle file info query
    bool handleFileInfoQuery(const NamedList& list);
    // Handle call.route
    bool route(Message& msg, const String& contact);
    // Remove share for a given contact
    bool removeShare(const String& contact);
    virtual const String& toString() const
	{ return name(); }

protected:
    // Find shared for a given contact
    bool findShare(const String& contact, RefPointer<FIDirectory>& dir, bool add = false);
    virtual void destroyed();

    ObjList m_share;                     // List of share dirs. Dir name is the contact name
    bool m_canRoute;                     // File transfers can be routed for this account

private:
    FIAccount() {}                       // No default contructor
    String m_name;
};

class FileInfo : public Module
{
public:
    FileInfo();
    ~FileInfo();
    inline Message* message(const char* msg) {
	    Message* m = new Message(msg);
	    m->addParam("module",name());
	    return m;
	}
    inline void getSendTarget(String& buf) {
	    Lock lck(this);
	    buf = m_sendTarget;
	}
    inline void copyRouteParams(NamedList& dest) {
	    Lock lck(this);
	    dest.copyParams(m_routeParams);
	}
    virtual void initialize();
    bool findAccount(const String& name, RefPointer<FIAccount>& acc, bool add = false);
    bool removeAccount(FIAccount* acc, bool delObj = true);
    bool handleFileInfo(Message& msg);
    bool handleCallRoute(Message& msg);

protected:
    virtual bool received(Message& msg, int id);

    ObjList m_accounts;
    String m_sendTarget;                 // File send target to set when routing
    NamedList m_routeParams;             // Parameters to be set when routing
};

INIT_PLUGIN(FileInfo);
static const char* cfgFile = "fileinfo";
static bool s_engineStarted = false;

// Message handlers installed by the module
static const TokenDict s_msgHandler[] = {
    {"engine.start", FileInfoMsgHandler::EngineStart},
    {"file.info", FileInfoMsgHandler::FileInfo},
    {"call.route", FileInfoMsgHandler::CallRoute},
    {0,0}
};


//
// ResultSetMngt
//
void ResultSetMngt::reset(const NamedList* list)
{
    m_max = -1;
    m_index = -1;
    m_addRsm = false;
    if (!list)
	return;
    m_max = list->getIntValue(YSTRING("rsm_max"),-1);
    m_index = list->getIntValue(YSTRING("rsm_index"),-1);
    m_addRsm = (m_max >= 0) || (m_index >= 0);
}


//
// FIFileData
//
void FIFileData::addToList(NamedList& list, const String& prefix) const
{
    if (time())
	list.addParam(prefix + "time",String(time()));
    list.addParam(prefix + "size",String((unsigned int)size()));
    if (description())
	list.addParam(prefix + "description",description());
}

bool FIFileData::operator==(const FIFileData& other)
{
    if (m_file != other.file())
	return false;
    if (m_size != other.size() || m_time != other.time())
	return false;
    return m_description == other.description();
}

FIFileData* FIFileData::build(const char* file, const char* desc, int* error)
{
    XDebug(&__plugin,DebugAll,"FIFileData::build(%s,%s)",file,desc);
    if (TelEngine::null(file))
	return 0;
    File f;
    bool ok = false;
    u_int64_t size = 0;
    unsigned int time = 0;
    while (f.openPath(file)) {
	int64_t len = f.length();
	if (len < 0 || f.error())
	    break;
	size = len;
	if (!f.getFileTime(time))
	    break;
	ok = true;
	break;
    }
    if (ok)
	return new FIFileData(file,size,time,desc);
    String tmp;
    Thread::errorString(tmp,f.error());
    Debug(&__plugin,DebugNote,"FileData failed to build file '%s': %d '%s'",
	file,f.error(),tmp.c_str());
    if (error)
	*error = f.error();
    return 0;
}


//
// FIDirectory
//
FIDirectory::FIDirectory(const char* name, const char* path, bool setMutex, bool updated)
    : FIItem(name),
    m_path(path),
    m_mutex(0),
    m_updated(updated)
{
    if (setMutex)
	m_mutex = new Mutex(false,"FIDirectory");
}

FIDirectory::~FIDirectory()
{
    if (m_mutex)
	delete m_mutex;
}

// Update content from file system
void FIDirectory::update()
{
    if (m_updated || !m_path)
	return;
    int error = 0;
    ObjList dirs;
    ObjList files;
    bool ok = File::listDirectory(m_path,&dirs,&files,&error);
    if (Thread::check(false))
	return;
    if (!ok) {
	String s;
	Thread::errorString(s,error);
	Debug(&__plugin,DebugNote,"Failed to list directory '%s': %d %s",
	    m_path.c_str(),error,s.c_str());
	return;
    }
    if (m_updated)
	return;
    m_children.clear();
    String p;
    p << m_path << Engine::pathSeparator();
    ObjList* o = dirs.skipNull();
    ObjList* last = &m_children;
    for (; o; o = o->skipNext()) {
	String* n = static_cast<String*>(o->get());
	if (!*n)
	    continue;
	if (Thread::check(false))
	    break;
	last = last->append(new FIDirectory(*n,p + *n));
    }
    o = !Thread::check(false) ? files.skipNull() : 0;
    for (; o; o = o->skipNext()) {
	String* n = static_cast<String*>(o->get());
	if (!*n)
	    continue;
	String tmp = p + *n;
	FIFileData* fd = FIFileData::build(tmp);
	if (fd) {
	    last = last->append(new FIFile(*n,fd));
	    TelEngine::destruct(fd);
	}
	if (Thread::check(false))
	    break;
    }
    m_updated = !Thread::check(false);
    if (!m_updated)
	m_children.clear();
}

// Add/replace an item. This method is not thread safe
bool FIDirectory::setItemUnsafe(FIItem* item, const String& oldName)
{
    if (!item)
	return false;
    ObjList* last = 0;
    if (oldName && oldName != item->name()) {
	ObjList* o = m_children.find(oldName);
	if (o) {
	    if (!m_children.find(item->name())) {
		o->set(item);
		return true;
	    }
	    o->remove();
	}
    }
    for (ObjList* o = m_children.skipNull(); o;) {
	FIItem* it = static_cast<FIItem*>(o->get());
	if (it == item)
	    return true;
	if (it->name() == item->name()) {
	    o->remove();
	    o->append(item);
	    return true;
	}
	ObjList* tmp = o->skipNext();
	if (!tmp) {
	    last = o;
	    break;
	}
	o = tmp;
    }
    if (last)
	last->append(item);
    else
	m_children.append(item);
    return true;
}

// Remove an item. This method is not thread safe
bool FIDirectory::removeUnsafe(const String& itemName)
{
    if (!itemName)
	return false;
    GenObject* gen = m_children.remove(itemName,false);
    XDebug(&__plugin,DebugAll,"FIDirectory::removeUnsafe(%s) found=%p [%p]",
	itemName.c_str(),gen,this);
    if (!gen)
	return false;
    TelEngine::destruct(gen);
    return true;
}

// Find a directory by path. This method is not thread safe
FIDirectory* FIDirectory::findDirPath(const String& path)
{
    ObjList* list = path.split('/',false);
    FIDirectory* dir = this;
    for (ObjList* o = list->skipNull(); dir && o; o = o->skipNext())
	dir = dir->findDir(o->get()->toString());
    TelEngine::destruct(list);
    return dir;
}

// Find a file by path. This method is not thread safe
FIFile* FIDirectory::findFilePath(const String& path)
{
    int pos = path.rfind('/');
    if (pos < 0)
	return findFile(path);
    FIDirectory* d = findDirPath(path.substr(0,pos));
    if (d)
	return d->findFile(path.substr(pos + 1));
    return 0;
}

void FIDirectory::addDirInfoRsp(NamedList& list, const ResultSetMngt& rsm)
{
    // Item count request
    if (rsm.m_max == 0) {
	if (!rsm.m_addRsm)
	    return;
	list.addParam("rsm_count",String(m_children.count()));
	return;
    }
    ObjList* o = m_children.skipNull();
    int index = 0;
    if (rsm.m_index >= 0) {
	while (index < rsm.m_index && o) {
	    index++;
	    o = o->skipNext();
	}
    }
    FIItem* first = 0;
    FIItem* last = 0;
    int maxItems = rsm.m_max > 0 ? rsm.m_max : 0;
    unsigned int n = 1;
    for (; o; o = o->skipNext()) {
	FIItem* item = static_cast<FIItem*>(o->get());
	if (!(item->file() || item->directory()))
	    continue;
	String prefix = "item.";
	prefix << n++;
	prefix << ".";
	addFIItem(list,item,true,prefix);
	if (!first)
	    first = item;
	last = item;
	if (maxItems) {
	    maxItems--;
	    if (!maxItems)
		break;
	}
    }
    if (!rsm.m_addRsm)
	return;
    if (first) {
	list.addParam("rsm_first",first->name());
	list.addParam("rsm_first.index",String(index));
    }
    if (last)
	list.addParam("rsm_last",last->name());
    list.addParam("rsm_count",String(m_children.count()));
}

// Append FIItem data to a list of parameters
void FIDirectory::addFIItem(NamedList& list, FIItem* fi, bool full, const String& prefix)
{
    if (!fi)
	return;
    if (prefix) {
	if (prefix.endsWith("."))
	    list.addParam(prefix.substr(0,prefix.length() - 1),fi->name());
	else
	    list.addParam(prefix,fi->name());
	if (fi->file())
	    list.addParam(prefix + "isfile",String::boolText(true));
    }
    else
	list.addParam("name",fi->name());
    FIFile* f = fi->file();
    if (!f)
	return;
    const FIFileData* d = f->data();
    if (d)
	d->addToList(list,prefix);
}

// Add a file
FIFile* FIDirectory::internalAddFile(FIFileData* fd, const String& fn)
{
    if (!(fd && fn))
	return 0;
    ObjList* o = m_children.find(fn);
    FIFile* f = 0;
    if (!o) {
	if (fd->ref()) {
	    f = new FIFile(fn,fd);
	    m_children.append(f);
	    DDebug(&__plugin,DebugAll,"Dir(%s) added file '%s' (%s) [%p]",
		name().c_str(),fn.c_str(),fd->file().c_str(),this);
	}
    }
    else {
	FIItem* ch = static_cast<FIItem*>(o->get());
	f = ch->file();
	if (f) {
	    const FIFileData* e = f->data();
	    if (e != fd || *fd != *e) {
		if (fd->ref()) {
		    DDebug(&__plugin,DebugAll,"Dir(%s) replacing file '%s' %s -> %s [%p]",
			name().c_str(),fn.c_str(),e ? e->file().c_str() : "",
			fd->file().c_str(),this);
		    f = new FIFile(fn,fd);
		    o->set(f);
		}
		else
		    f = 0;
	    }
	}
	else
	    DDebug(&__plugin,DebugInfo,
		"Dir(%s) can't add file '%s': a non-file item already in the list [%p]",
		name().c_str(),fn.c_str(),this);

    }
    return f;
}


//
// FileInfoMsgHandler
//
FileInfoMsgHandler::FileInfoMsgHandler(int handler, int prio)
    : MessageHandler(lookup(handler,s_msgHandler),prio,__plugin.name()),
    m_handler(handler)
{
}

bool FileInfoMsgHandler::received(Message& msg)
{
    String* module = msg.getParam(YSTRING("module"));
    if (module && *module == __plugin.name())
	return false;
    switch (m_handler) {
	case FileInfo:
	    return __plugin.handleFileInfo(msg);
	case CallRoute:
	    return __plugin.handleCallRoute(msg);
	case EngineStart:
	    s_engineStarted = true;
	    return false;
    }
    return false;
}


//
// FIAccount
//
FIAccount::FIAccount(const char* name)
    : Mutex(false,"FIAccount"),
    m_canRoute(true),
    m_name(name)
{
}

// Handle file info set for a given account
bool FIAccount::handleFileInfoSet(const NamedList& list)
{
    const String& contact = list[YSTRING("contact")];
    XDebug(&__plugin,DebugAll,"Account(%s) handleFileInfoSet(%s) [%p]",
	name().c_str(),contact.c_str(),this);
    RefPointer<FIDirectory> dir;
    String prefix = "item";
    for (unsigned int i = 0; true; i++) {
	String pref = prefix;
	if (i)
	    pref << "." << i;
	String* shareName = list.getParam(pref);
	if (!shareName) {
	    if (i)
		break;
	    continue;
	}
	if (!*shareName)
	    continue;
	// Share name can't contain '/'
	if (shareName->find('/') >= 0) {
	    Debug(&__plugin,DebugNote,"Share name '%s' contains '/' (not accepted)",
		shareName->c_str());
	    continue;
	}
	String path = list[pref + ".path"];
	if (path.endsWith("/") || path.endsWith("\\"))
	    path = path.substr(0,path.length() - 1);
	if (!path)
	    continue;
	FIItem* item = 0;
	if (list.getBoolValue(pref + ".isfile")) {
	    Debug(&__plugin,DebugNote,"Can't set share file: not implemented");
	    continue;
	}
	else
	    item = new FIDirectory(*shareName,path);
	if (!dir) {
	    if (!findShare(contact,dir,true)) {
		TelEngine::destruct(item);
		break;
	    }
	    dir->lock();
	}
	bool ok = dir->setItemUnsafe(item,list[pref + ".oldname"]);
	Debug(&__plugin,ok ? DebugAll : DebugNote,
	    "Account(%s) contact=%s %s item name=%s path=%s [%p]",
	    name().c_str(),contact.c_str(),ok ? "set" : "failed to set",
	    shareName->c_str(),path.c_str(),this);
    }
    if (dir)
	dir->unlock();
    dir = 0;
    return true;
}

// Handle file info remove
bool FIAccount::handleFileInfoRemove(const NamedList& list, const String& contact)
{
    XDebug(&__plugin,DebugAll,"Account(%s) handleFileInfoRemove(%s) [%p]",
	name().c_str(),contact.c_str(),this);
    RefPointer<FIDirectory> dir;
    String prefix = "item";
    bool something = false;
    for (unsigned int i = 0; true; i++) {
	String pref = prefix;
	if (i)
	    pref << "." << i;
	String* shareName = list.getParam(pref);
	if (!shareName) {
	    if (i)
		break;
	    continue;
	}
	something = true;
	if (!*shareName)
	    continue;
	if (!dir) {
	    if (!findShare(contact,dir))
		break;
	    dir->lock();
	}
	if (dir->removeUnsafe(*shareName))
	    Debug(&__plugin,DebugAll,"Account(%s) contact=%s removed item %s [%p]",
		name().c_str(),contact.c_str(),shareName->c_str(),this);
    }
    if (dir)
	dir->unlock();
    dir = 0;
    if (!something)
	return removeShare(contact);
    return true;
}

// Handle file info query for a given account
bool FIAccount::handleFileInfoQuery(const NamedList& list)
{
    const String& contact = list[YSTRING("from")];
    XDebug(&__plugin,DebugAll,"Account(%s) handleFileInfoQuery(%s) [%p]",
	name().c_str(),contact.c_str(),this);
    if (!contact)
	return false;
    String* dir = list.getParam(YSTRING("dir"));
    String* file = dir ? 0 : list.getParam(YSTRING("file"));
    if (!dir && TelEngine::null(file))
	return false;
    RefPointer<FIDirectory> cdir;
    bool ok = findShare(contact,cdir);
    if (!ok) {
	// Don't respond if there is no subscription
	const String& sub = list[YSTRING("subscription")];
	ok = sub && (sub == YSTRING("both") || sub == YSTRING("from"));
    }
    XDebug(&__plugin,ok ? DebugAll : DebugNote,
	"Account(%s) query from '%s' dir=%s file=%s [%p]",name().c_str(),
	contact.c_str(),TelEngine::c_safe(dir),TelEngine::c_safe(file),this);
    Message* m = __plugin.message("file.info");
    m->copyParams(list,"account,id");
    m->addParam("to",contact);
    m->addParam("to_instance",list.getValue(YSTRING("from_instance")),false);
    m->addParam("operation","result");
    if (cdir) {
	cdir->lock();
	if (dir) {
	    FIDirectory* directory = cdir;
	    if (*dir)
		directory = directory->findDirPath(*dir);
	    if (directory) {
		// TODO: Use a separate thread for update in server mode ?
		directory->update();
		ResultSetMngt rsm(list);
		directory->addDirInfoRsp(*m,rsm);
	    }
	}
	else
	    // TODO: update path ?
	    FIDirectory::addFIItem(*m,cdir->findFilePath(*file));
	cdir->unlock();
	cdir = 0;
    }
    Engine::enqueue(m);
    return true;
}

// Handle call.route
bool FIAccount::route(Message& msg, const String& contact)
{
    if (!contact)
	return false;
    const String& file = msg[YSTRING("file_name")];
    if (!file)
	return false;
    RefPointer<FIDirectory> cdir;
    if (!findShare(contact,cdir)) {
	Debug(&__plugin,DebugAll,"Account(%s) routing: contact '%s' not found [%p]",
	    name().c_str(),contact.c_str(),this);
	return false;
    }
    Lock lck(cdir->mutex());
    FIFile* f = cdir->findFilePath(file);
    String s;
    if (f && f->data())
	s = f->data()->file();
    lck.drop();
    Debug(&__plugin,DebugAll,"Account(%s) routing contact='%s' file='%s' found='%s' [%p]",
	name().c_str(),contact.c_str(),file.c_str(),s.c_str(),this);
    if (!s)
	return false;
    __plugin.copyRouteParams(msg);
    __plugin.getSendTarget(msg.retValue());
    msg.retValue() << s;
    return true;
}

// Find shared for a given contact
bool FIAccount::findShare(const String& contact, RefPointer<FIDirectory>& dir, bool add)
{
    if (!contact)
	return false;
    Lock lck(this);
    ObjList* o = m_share.find(contact);
    XDebug(&__plugin,DebugInfo,"Account(%s) findShare('%s',%u) found=%p [%p]",
	name().c_str(),contact.c_str(),add,o,this);
    if (!o) {
	if (!add)
	    return false;
	FIDirectory* d = new FIDirectory(contact,0,true,true);
	o = m_share.append(d);
	Debug(&__plugin,DebugInfo,"Account(%s) added contact '%s' [%p]",
	    name().c_str(),contact.c_str(),this);
    }
    dir = static_cast<FIDirectory*>(o->get());
    return dir != 0;
}

// Remove share for a given contact
bool FIAccount::removeShare(const String& contact)
{
    if (!contact)
	return false;
    Lock lck(this);
    GenObject* gen = m_share.remove(contact,false);
    if (!gen)
	return false;
    lck.drop();
    Debug(&__plugin,DebugInfo,"Account(%s) removed contact '%s' [%p]",
	name().c_str(),contact.c_str(),this);
    TelEngine::destruct(gen);
    return true;
}

void FIAccount::destroyed()
{
    m_share.clear();
    __plugin.removeAccount(this,false);
    RefObject::destroyed();
}


//
// FileInfo
//
FileInfo::FileInfo()
    : Module("fileinfo","misc"),
    m_routeParams("")
{
    Output("Loaded module FileInfo");
}

FileInfo::~FileInfo()
{
    Output("Unloading module FileInfo");
}

// Utility used in FileInfo::initialize()
static inline const NamedList* getSafeSect(Configuration& cfg, const String& name)
{
    NamedList* tmp = cfg.getSection(name);
    if (tmp)
	return tmp;
    return &NamedList::empty();
}

void FileInfo::initialize()
{
    static bool first = true;
    Output("Initializing module FileInfo");
    Configuration cfg(Engine::configFile(cfgFile));
    const NamedList* callRoute = getSafeSect(cfg,YSTRING("call.route"));
    if (first) {
	first = false;
	setup();
	for (const TokenDict* d = s_msgHandler; d->token; d++) {
	    int prio = d->value;
	    if (d->value == FileInfoMsgHandler::CallRoute)
		prio = callRoute->getIntValue("priority",prio);
	    if (prio < 0)
		prio = 100;
	    FileInfoMsgHandler* h = new FileInfoMsgHandler(d->value,prio);
	    Engine::install(h);
	}
    }
    lock();
    m_sendTarget = callRoute->getValue(YSTRING("file_send_target"),"filetransfer/send/");
    m_routeParams.clearParams();
    if (callRoute->getBoolValue(YSTRING("set_default_params"),true)) {
	m_routeParams.addParam("autoclose",String::boolText(true));
	m_routeParams.addParam("wait_on_drop","10000");
    }
    m_routeParams.copySubParams(*callRoute,YSTRING("param_"),true);
    unlock();
}

bool FileInfo::findAccount(const String& name, RefPointer<FIAccount>& acc, bool add)
{
    if (!name)
	return false;
    Lock lck(this);
    ObjList* o = m_accounts.find(name);
    if (!o) {
	if (!add)
	    return false;
	FIAccount* a = new FIAccount(name);
	o = m_accounts.append(a);
	Debug(this,DebugInfo,"Added account '%s' (%p)",a->name().c_str(),a);
    }
    acc = static_cast<FIAccount*>(o->get());
    return acc != 0;
}

bool FileInfo::removeAccount(FIAccount* acc, bool delObj)
{
    if (!acc)
	return false;
    Lock lck(this);
    GenObject* gen = m_accounts.remove(acc,false);
    if (!gen)
	return false;
    lck.drop();
    Debug(this,DebugInfo,"Removed account '%s' (%p) delObj=%u",acc->name().c_str(),acc,delObj);
    if (delObj)
	TelEngine::destruct(gen);
    return true;
}

bool FileInfo::handleFileInfo(Message& msg)
{
    const String& account = msg[YSTRING("account")];
    if (!account)
	return false;
    bool set = false;
    bool create = false;
    bool remove = false;
    bool query = false;
    const String& oper = msg[YSTRING("operation")];
    if (oper == YSTRING("set"))
	create = set = true;
    else if (oper == YSTRING("remove"))
	remove = true;
    else if (oper == YSTRING("query"))
	query = true;
    else
	return false;
    RefPointer<FIAccount> acc;
    if (!findAccount(account,acc,create))
	return false;
    if (create) {
	const String* canRoute = msg.getParam(YSTRING("canroute"));
	if (canRoute)
	    acc->canRoute(canRoute->toBoolean());
    }
    bool ok = false;
    while (true) {
	if (set) {
	    ok = acc->handleFileInfoSet(msg);
	    break;
	}
	if (remove) {
	    const String& contact = msg[YSTRING("contact")];
	    if (contact)
		ok = acc->handleFileInfoRemove(msg,contact);
	    else
		removeAccount(acc);
	    break;
	}
	if (query) {
	    ok = acc->handleFileInfoQuery(msg);
	    break;
	}
	break;
    }
    acc = 0;
    return ok;
}

bool FileInfo::handleCallRoute(Message& msg)
{
    if (msg[YSTRING("format")] != YSTRING("data"))
	return false;
    const String& oper = msg[YSTRING("operation")];
    bool send = (oper == YSTRING("send"));
    if (!send)
	return false;
    const String& account = msg[YSTRING("in_line")];
    if (!account)
	return false;
    // Jingle puts caller party as 'callername'
    const String* contact = 0;
    if (msg[YSTRING("module")] == YSTRING("jingle"))
	contact = msg.getParam(YSTRING("callername"));
    else
	contact = msg.getParam(YSTRING("caller"));
    if (TelEngine::null(contact))
	return false;
    lock();
    ObjList* o = m_accounts.find(account);
    FIAccount* a = o ? static_cast<FIAccount*>(o->get()) : 0;
    if (a && !(a->canRoute() && a->ref()))
	a = 0;
    unlock();
    bool ok = a && a->route(msg,*contact);
    TelEngine::destruct(a);
    return ok;
}

bool FileInfo::received(Message& msg, int id)
{
    return Module::received(msg,id);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
