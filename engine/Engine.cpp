/**
 * Engine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatengine.h"
#include "yateversn.h"

#ifdef _WINDOWS

#include <process.h>
#include <shlobj.h>
#define RTLD_NOW 0
#define dlopen(name,flags) LoadLibrary(name)
#define dlclose !FreeLibrary
#define dlsym GetProcAddress
#define dlerror() "LoadLibrary error"
#define YSERV_RUN 1
#define YSERV_INS 2
#define YSERV_DEL 4
#define PATH_SEP "\\"
#ifndef CFG_DIR
#define CFG_DIR "Yate"
#endif

#ifndef SHGetSpecialFolderPath
__declspec(dllimport) BOOL WINAPI SHGetSpecialFolderPathA(HWND,LPSTR,INT,BOOL);
#endif

#else // _WINDOWS

#include "yatepaths.h"
#include <dirent.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <sys/resource.h>
typedef void* HMODULE;
#define PATH_SEP "/"
#ifndef CFG_DIR
#ifdef HAVE_MACOSX_SUPPORT
#define CFG_DIR "Yate"
#else
#define CFG_DIR ".yate"
#endif
#endif

static int s_childsig = 0;

#endif // _WINDOWS

#ifdef HAVE_MACOSX_SUPPORT
#include "MacOSXUtils.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_PRCTL
#include <sys/prctl.h>
#endif

#include <assert.h>

namespace TelEngine {

class EnginePrivate : public Thread
{
public:
    EnginePrivate()
	: Thread("Engine Worker")
	{ count++; }
    ~EnginePrivate()
	{ count--; }
    virtual void run();
    static int count;
};

class EngineCommand : public MessageHandler
{
public:
    EngineCommand()
	: MessageHandler("engine.command",100,"engine")
	{ }
    virtual bool received(Message &msg);
    static void doCompletion(Message &msg, const String& partLine, const String& partWord);
};

};

using namespace TelEngine;

#ifndef MOD_PATH
#define MOD_PATH "." PATH_SEP "modules"
#endif
#ifndef SHR_PATH
#define SHR_PATH "." PATH_SEP "share"
#endif
#ifndef CFG_PATH
#define CFG_PATH "." PATH_SEP "conf.d"
#endif
#ifndef DLL_SUFFIX
#define DLL_SUFFIX ".yate"
#endif
#ifndef CFG_SUFFIX
#define CFG_SUFFIX ".conf"
#endif

// Initialization events capture by default
#ifndef CAPTURE_EVENTS
#define CAPTURE_EVENTS true
#endif

// Maximum number of engine.stop messages we allow
#ifndef MAX_STOP
#define MAX_STOP 5
#endif

// Supervisor control constants

// Maximum the child's sanity pool can grow
#define MAX_SANITY 5
// Initial sanity buffer, allow some init time for the child
#define INIT_SANITY 30
// Minimum (and initial) delay until supervisor restarts child
#define RUNDELAY_MIN 1000000
// Maximum delay until supervisor restarts child, allow system to breathe
#define RUNDELAY_MAX 60000000
// Amount we decerease delay towards minimum each time child proves sanity
#define RUNDELAY_DEC 20000
// Maximum supervised initial delay
#define INITDELAY_MAX 60000
// Size of log relay buffer in bytes
#define MAX_LOGBUFF 4096

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

#ifndef PATH_MAX
#define PATH_MAX 270
#endif

static u_int64_t s_nextinit = 0;
static u_int64_t s_restarts = 0;
static bool s_makeworker = true;
static bool s_keepclosing = false;
static bool s_nounload = false;
static int s_super_handle = -1;
static int s_run_attempt = 0;
static bool s_interactive = true;
static bool s_localsymbol = false;
static bool s_logtruncate = false;
static const char* s_logfile = 0;

static void sighandler(int signal)
{
    switch (signal) {
#ifndef _WINDOWS
	case SIGCHLD:
	    ::waitpid(-1,0,WNOHANG);
	    break;
	case SIGUSR1:
	    Engine::restart(0,true);
	    break;
	case SIGUSR2:
	    Engine::restart(0,false);
	    break;
	case SIGHUP:
	    if (s_interactive) {
		// console got closed so shutdown without writing to console
		if (!s_logfile)
		    Debugger::enableOutput(false);
		Engine::halt(0);
		break;
	    }
	    // intentionally fall through
	case SIGQUIT:
	    if (s_nextinit <= Time::now())
		Engine::init();
	    s_nextinit = Time::now() + 2000000;
	    break;
#endif
	case SIGINT:
	case SIGTERM:
	    Engine::halt(0);
	    break;
    }
}

String Engine::s_node;
String Engine::s_shrpath(SHR_PATH);
String Engine::s_cfgsuffix(CFG_SUFFIX);
String Engine::s_modpath(MOD_PATH);
String Engine::s_modsuffix(DLL_SUFFIX);
ObjList Engine::s_extramod;
NamedList Engine::s_params("");

Engine::RunMode Engine::s_mode = Engine::Stopped;
Engine::CallAccept Engine::s_accept = Engine::Accept;
Engine* Engine::s_self = 0;
bool Engine::s_started = false;
int Engine::s_haltcode = -1;
int EnginePrivate::count = 0;
static String s_cfgpath(CFG_PATH);
static String s_usrpath;
static bool s_createusr = true;
static bool s_init = false;
static bool s_dynplugin = false;
static Engine::PluginMode s_loadMode = Engine::LoadFail;
static int s_minworkers = 1;
static int s_maxworkers = 10;
static int s_exit = -1;
unsigned int Engine::s_congestion = 0;
static Mutex s_congMutex(false,"Congestion");
static bool s_debug = true;
static bool s_capture = CAPTURE_EVENTS;
static int s_maxevents = 25;
static Mutex s_eventsMutex(false,"EventsList");
static ObjList s_events;
static String s_startMsg;
static SharedVars s_vars;
static Mutex s_hooksMutex(true,"HooksList");
static ObjList s_hooks;
static NamedCounter* s_counter = 0;
static NamedCounter* s_workCnt = 0;

const TokenDict Engine::s_callAccept[] = {
    {"accept",      Engine::Accept},
    {"partial",     Engine::Partial},
    {"congestion",  Engine::Congestion},
    {"reject",      Engine::Reject},
    {0,0}
};

#ifdef RLIMIT_CORE
static bool s_coredump = false;
#endif

#ifdef RLIMIT_NOFILE
#ifdef FDSIZE_HACK
static bool s_numfiles = false;
#else
#undef RLIMIT_NOFILE
#endif
#endif

static bool s_sigabrt = false;
static bool s_lateabrt = false;
static String s_cfgfile;
static String s_userdir(CFG_DIR);
static Configuration s_cfg;
static ObjList plugins;
static ObjList* s_cmds = 0;
static unsigned int s_runid = 0;

static EngineCheck* s_engineCheck = 0;

void EngineCheck::setChecker(EngineCheck* ptr)
{
    s_engineCheck = ptr;
}


namespace { // anonymous

class SLib : public String
{
public:
    virtual ~SLib();
    static SLib* load(const char* file, bool local, bool nounload);
    bool unload(bool doNow);
private:
    SLib(HMODULE handle, const char* file, bool nounload, unsigned int count);
    HMODULE m_handle;
    bool m_nounload;
    unsigned int m_count;
};

class EngineSuperHandler : public MessageHandler
{
public:
    EngineSuperHandler()
	: MessageHandler("engine.timer",1,"engine"),
	  m_seq(0)
	{ }
    virtual bool received(Message &msg)
	{ YIGNORE(::write(s_super_handle,&m_seq,1)); m_seq++; return false; }
    char m_seq;
};

class EngineStatusHandler : public MessageHandler
{
public:
    EngineStatusHandler()
	: MessageHandler("engine.status",90,"engine")
	{ }
    virtual bool received(Message &msg);
    static void objects(String& retVal, bool details);
    static int objects(String& str);
};

class EngineHelp : public MessageHandler
{
public:
    EngineHelp()
	: MessageHandler("engine.help",100,"engine")
	{ }
    virtual bool received(Message &msg);
};

class EngineEventList : public ObjList
{
public:
    inline EngineEventList(const char* name)
	: m_name(name)
	{ }

    virtual const String& toString() const
	{ return m_name; }
private:
    String m_name;
};

class EngineEventHandler : public MessageHandler
{
public:
    EngineEventHandler()
	: MessageHandler("module.update",1,"engine")
	{ }
    virtual bool received(Message &msg);
};

class RefList : public RefObject
{
public:
    inline RefList()
	{ }
    virtual void* getObject(const String& name) const
	{ return (name == YATOM("ObjList")) ? (void*)&m_list : RefObject::getObject(name); }
    inline ObjList& list()
	{ return m_list; }
private:
    ObjList m_list;
};

}; // anonymous namespace


// helper function to initialize user application data dir
static void initUsrPath(String& path, const char* newPath = 0)
{
    if (path)
	return;
    if (TelEngine::null(newPath)) {
#ifdef _WINDOWS
	// we force using the ANSI version
	char szPath[MAX_PATH];
	if (SHGetSpecialFolderPathA(NULL,szPath,CSIDL_APPDATA,TRUE))
	    path = szPath;
#elif defined(HAVE_MACOSX_SUPPORT)
	MacOSXUtils::applicationSupportPath(path);
	if (path.null()) {
	    Debug(DebugMild,"Could not get system user path on MacOS X, setting it to $(HOME)");
	    path = ::getenv("HOME");
	}
#else
	path = ::getenv("HOME");
#endif
	if (path.null()) {
	    if (Engine::mode() == Engine::Client)
		Debug(DebugWarn,"Could not get per-user application data path!");
	    path = s_cfgpath;
	}
	if (!path.endsWith(PATH_SEP))
	    path += PATH_SEP;
	path += s_userdir;
    }
    else
	path = newPath;
    if (path.endsWith(PATH_SEP))
	path = path.substr(0,path.length()-1);
}

// Utility: init or decrease Lockable wait value by half
static inline void setLockableWait()
{
    unsigned long lockWait = Lockable::wait();
    if (lockWait) {
	lockWait /= 2;
	if (lockWait < Thread::idleUsec())
	    lockWait = Thread::idleUsec();
    }
    else
	lockWait = 10000000;
    Lockable::wait(lockWait);
}

// helper function to set up the config file name
static void initCfgFile(const char* name)
{
    s_cfgfile = name;
    if (s_cfgfile.endsWith(".exe") || s_cfgfile.endsWith(".EXE"))
	s_cfgfile = s_cfgfile.substr(0,s_cfgfile.length()-4);
}

int EngineStatusHandler::objects(String& str)
{
    int cnt = 0;
    for (const ObjList* l = GenObject::getObjCounters().skipNull(); l; l = l->skipNext()) {
	const NamedCounter* c = static_cast<const NamedCounter*>(l->get());
	if (!c->count())
	    continue;
	str.append(*c,",") << "=" << c->count();
	cnt += c->count();
    }
    return cnt;
}

void EngineStatusHandler::objects(String& retVal, bool details)
{
    retVal << "name=objects,type=system";
    retVal << ";enabled=" << getObjCounting();
    retVal << ",counters=" << getObjCounters().count();
    if (details) {
	String str;
	retVal << ",objects=" << objects(str);
	retVal.append(str,";");
    }
    retVal << "\r\n";
}

bool EngineStatusHandler::received(Message &msg)
{
    bool details = msg.getBoolValue("details",true);
    String sel = msg.getValue("module");
    if (sel && (sel != YSTRING("engine"))) {
	if (sel.startSkip("objects")) {
	    if (sel) {
		msg.retValue() << "name=objects,type=system";
		msg.retValue() << ";enabled=" << getObjCounting();
		const NamedCounter* c = getObjCounter(sel,false);
		msg.retValue() << ";" << sel << "=";
		if (c)
		    msg.retValue() << c->count();
		else
		    msg.retValue() << "(not counted)";
		msg.retValue() << "\r\n";
	    }
	    else
		objects(msg.retValue(),details);
	    return true;
	}
	return false;
    }
    msg.retValue() << "name=engine,type=system";
    msg.retValue() << ",version=" << YATE_VERSION;
    msg.retValue() << ",revision=" << YATE_REVISION;
    msg.retValue() << ",nodename=" << Engine::nodeName();
    msg.retValue() << ";plugins=" << plugins.count();
    msg.retValue() << ",inuse=" << Engine::self()->usedPlugins();
    msg.retValue() << ",handlers=" << Engine::self()->handlerCount();
    msg.retValue() << ",hooks=" << Engine::self()->postHookCount();
    msg.retValue() << ",messages=" << Engine::self()->messageCount();
    msg.retValue() << ",supervised=" << (s_super_handle >= 0);
    msg.retValue() << ",runattempt=" << s_run_attempt;
#ifndef _WINDOWS
    msg.retValue() << ",lastsignal=" << s_childsig;
#endif
    msg.retValue() << ",threads=" << Thread::count();
    msg.retValue() << ",workers=" << EnginePrivate::count;
    msg.retValue() << ",mutexes=" << Mutex::count();
    int locks = Mutex::locks();
    if (locks >= 0)
	msg.retValue() << ",locks=" << locks;
    msg.retValue() << ",semaphores=" << Semaphore::count();
    locks = Semaphore::locks();
    if (locks >= 0)
	msg.retValue() << ",waiting=" << locks;
    msg.retValue() << ",acceptcalls=" << lookup(Engine::accept(),Engine::getCallAcceptStates());
    msg.retValue() << ",congestion=" << Engine::getCongestion();
    if (details) {
	NamedIterator iter(Engine::runParams());
	char sep = ';';
	while (const NamedString* p = iter.get()) {
	    if (p->name().find("path") < 0)
		continue;
	    msg.retValue() << sep << p->name() << "=" << *p;
	    sep = ',';
	}
    }
    msg.retValue() << "\r\n";
    if (getObjCounting() && sel.null())
	objects(msg.retValue(),details);
    return !sel.null();
}

bool EngineEventHandler::received(Message &msg)
{
    if (Engine::nodeName() && !msg.getParam(YSTRING("nodename")))
	msg.addParam("nodename",Engine::nodeName());
    const String* type = msg.getParam(YSTRING("from"));
    if (TelEngine::null(type))
	return false;
    bool full = true;
    const String* text = msg.getParam(YSTRING("fulltext"));
    if (TelEngine::null(text)) {
	text = msg.getParam(YSTRING("text"));
	if (TelEngine::null(text))
	    return false;
	full = false;
    }
    String* ev = 0;
    int level = msg.getBoolValue(YSTRING("operational"),true) ? DebugNote : DebugWarn;
    level = msg.getIntValue(YSTRING("level"),level);
    if (full)
	ev = new CapturedEvent(level,*text);
    else {
	// build a full text with timestamp and sender
	char tstamp[30];
	Debugger::Formatting fmt = Debugger::getFormatting();
	if (Debugger::None == fmt)
	    fmt = Debugger::Relative;
	Debugger::formatTime(tstamp,fmt);
	ev = new CapturedEvent(level,tstamp);
	*ev << "<" << *type << "> " << *text;
	msg.setParam("fulltext",*ev);
    }
    Lock mylock(s_eventsMutex);
    ObjList* list = static_cast<EngineEventList*>(s_events[*type]);
    if (!list) {
	list = new EngineEventList(*type);
	s_events.append(list);
    }
    while ((s_maxevents > 0) && (int)list->count() >= s_maxevents)
	list->remove();
    list->append(ev);
    return false;
}

static const char s_cmdsOptNoUnload[] = "  module {load modulefile|list}\r\n";
static const char s_cmdsOpt[] = "  module {{load|reload} modulefile|unload modulename|list}\r\n";
static const char s_cmdsMsg[] = "Controls the modules loaded in the Telephony Engine\r\n";
static const char s_evtsOpt[] = "  events [clear] [type]\r\n";
static const char s_evtsMsg[] = "Show or clear events or alarms collected since the engine startup\r\n";
static const char s_logvOpt[] = "  logview\r\n";
static const char s_logvMsg[] = "Show log of engine startup and initialization process\r\n";

// get the base name of a module file
static String moduleBase(const String& fname)
{
    int sep = fname.rfind(PATH_SEP[0]);
    if (sep >= 0)
	sep++;
    else
	sep = 0;
    int len = fname.length() - sep;
    if (fname.endsWith(Engine::moduleSuffix()))
	len -= Engine::moduleSuffix().length();
    return fname.substr(sep,len);
}

// perform one completion only if match still possible
static void completeOne(String& ret, const String& str, const char* part)
{
    if (part && !str.startsWith(part))
	return;
    ret.append(str,"\t");
}

static void completeOneModule(String& ret, const String& str, const char* part, ObjList& mods, bool reload)
{
    SLib* s = static_cast<SLib*>(mods[moduleBase(str)]);
    if (s) {
	if (!(reload && s->unload(false)))
	    return;
    }
    else if (reload)
	return;
    completeOne(ret,str,part);
}

// complete a module filename from filesystem
void completeModule(String& ret, const String& part, ObjList& mods, bool reload, const String& rpath = String::empty())
{
    if (part[0] == '.')
	return;
    String path = Engine::modulePath();
    String rdir = rpath;
    int sep = part.rfind(PATH_SEP[0]);
    if (sep >= 0)
	rdir += part.substr(0,sep+1);
    if (rdir) {
	if (!path.endsWith(PATH_SEP))
	    path += PATH_SEP;
	path += rdir;
    }
    if (path.endsWith(PATH_SEP))
	path = path.substr(0,path.length()-1);

    DDebug(DebugInfo,"completeModule path='%s' rdir='%s'",path.c_str(),rdir.c_str());
#ifdef _WINDOWS
    WIN32_FIND_DATA entry;
    HANDLE hf = ::FindFirstFile(path + PATH_SEP "*",&entry);
    if (hf == INVALID_HANDLE_VALUE)
	return;
    do {
	XDebug(DebugInfo,"Found dir entry %s",entry.cFileName);
	if (entry.cFileName[0] == '.')
	    continue;
	if ((entry.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
	    continue;
	if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
	    completeModule(ret,part,mods,reload,rdir + entry.cFileName + PATH_SEP);
	    continue;
	}
	int n = ::strlen(entry.cFileName) - Engine::moduleSuffix().length();
	if ((n > 0) && !::strcmp(entry.cFileName+n,Engine::moduleSuffix()))
	    completeOneModule(ret,rdir + entry.cFileName,part,mods,reload);
    } while (::FindNextFile(hf,&entry));
    ::FindClose(hf);
#else
    DIR *dir = ::opendir(path);
    if (!dir)
	return;
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != 0) {
	XDebug(DebugInfo,"Found dir entry %s",entry->d_name);
	if (entry->d_name[0] == '.')
	    continue;
	struct stat stat_buf;
	if (::stat(path + PATH_SEP + entry->d_name,&stat_buf))
	    continue;
	if (S_ISDIR(stat_buf.st_mode)) {
	    completeModule(ret,part,mods,reload,rdir + entry->d_name + PATH_SEP);
	    continue;
	}
	int n = ::strlen(entry->d_name) - Engine::moduleSuffix().length();
	if ((n > 0) && !::strcmp(entry->d_name+n,Engine::moduleSuffix()))
	    completeOneModule(ret,rdir + entry->d_name,part,mods,reload);
    }
    ::closedir(dir);
#endif
}

// perform command line completion
void EngineCommand::doCompletion(Message &msg, const String& partLine, const String& partWord)
{
    if (partLine.null() || (partLine == YSTRING("help"))) {
	completeOne(msg.retValue(),"module",partWord);
	completeOne(msg.retValue(),"events",partWord);
	completeOne(msg.retValue(),"logview",partWord);
    }
    else if (partLine == YSTRING("status")) {
	completeOne(msg.retValue(),"engine",partWord);
	completeOne(msg.retValue(),"objects",partWord);
    }
    else if (partLine == YSTRING("status objects")) {
	for (ObjList* l = getObjCounters().skipNull();l;l = l->skipNext())
	    completeOne(msg.retValue(),l->get()->toString(),partWord);
    }
    else if (partLine == YSTRING("module")) {
	completeOne(msg.retValue(),"load",partWord);
	if (!s_nounload) {
	    completeOne(msg.retValue(),"unload",partWord);
	    completeOne(msg.retValue(),"reload",partWord);
	}
	completeOne(msg.retValue(),"list",partWord);
    }
    else if (partLine == YSTRING("module load"))
	completeModule(msg.retValue(),partWord,Engine::self()->m_libs,false);
    else if (partLine == YSTRING("module reload"))
	completeModule(msg.retValue(),partWord,Engine::self()->m_libs,true);
    else if (partLine == YSTRING("module unload")) {
	for (ObjList* l = Engine::self()->m_libs.skipNull();l;l = l->skipNext()) {
	    SLib* s = static_cast<SLib*>(l->get());
	    if (s->unload(false))
		completeOne(msg.retValue(),*s,partWord);
	}
    }
    else if (partLine == YSTRING("reload")) {
	for (ObjList* l = plugins.skipNull(); l; l = l->skipNext()) {
	    const Plugin* p = static_cast<const Plugin*>(l->get());
	    completeOne(msg.retValue(),p->name(),partWord);
	}
    }
    else if (partLine == YSTRING("events") || partLine == YSTRING("events clear")) {
	Lock mylock(s_eventsMutex);
	for (ObjList* l = s_events.skipNull(); l; l = l->skipNext()) {
	    const EngineEventList* e = static_cast<const EngineEventList*>(l->get());
	    completeOne(msg.retValue(),e->toString(),partWord);
	}
	completeOne(msg.retValue(),"log",partWord);
	if (partLine == YSTRING("events"))
	    completeOne(msg.retValue(),"clear",partWord);
    }
}

bool EngineCommand::received(Message &msg)
{
    String line = msg.getValue("line");
    if (line.null()) {
	doCompletion(msg,msg.getValue("partline"),msg.getValue("partword"));
	return false;
    }
    if (line.startSkip("control")) {
	int pos = line.find(' ');
	String id = line.substr(0,pos).trimBlanks();
	String ctrl = line.substr(pos+1).trimBlanks();
	if ((pos <= 0) || id.null() || ctrl.null())
	    return false;
	Message m("chan.control");
	m.addParam("targetid",id);
	m.addParam("component",id);
	m.copyParam(msg,"module");
	m.copyParam(msg,"cmd",'_');
	static const Regexp r("^\\(.* \\)\\?\\([^= ]\\+\\)=\\([^=]*\\)$");
	while (ctrl) {
	    if (!ctrl.matches(r)) {
		m.setParam("operation",ctrl);
		break;
	    }
	    m.setParam(ctrl.matchString(2),ctrl.matchString(3).trimBlanks());
	    ctrl = ctrl.matchString(1).trimBlanks();
	}
	if (!Engine::dispatch(m))
	    return false;
	msg.retValue() = m.retValue();
	NamedString* opStatus = m.getParam(YSTRING("operation-status"));
	return !opStatus || opStatus->toBoolean();
    }
    if (!line.startSkip("module")) {
	if (line.startSkip("events") || (line == "logview" && (line.clear(),true))) {
	    bool clear = line.startSkip("clear");
	    line.startSkip("log");
	    if (clear) {
		Engine::clearEvents(line);
		return true;
	    }
	    RefList* list = 0;
	    int cnt = 0;
	    for (const ObjList* l = Engine::events(line); l; l = l->skipNext()) {
		const CapturedEvent* ev = static_cast<const CapturedEvent*>(l->get());
		if (!list)
		    list = new RefList();
		list->list().append(new CapturedEvent(*ev));
		cnt++;
	    }
	    msg.userData(list);
	    TelEngine::destruct(list);
	    (msg.retValue() = "Events: ") << cnt << "\r\n";
	    return true;
	}
	return false;
    }

    bool ok = false;
    int sep = line.find(' ');
    if (sep > 0) {
	String cmd = line.substr(0,sep).trimBlanks();
	String arg = line.substr(sep+1).trimBlanks();
	if ((cmd == "load") || (cmd == "reload")) {
	    bool reload = (cmd == "reload");
	    cmd = moduleBase(arg);
	    SLib* s = static_cast<SLib*>(Engine::self()->m_libs[cmd]);
	    if (s) {
		ok = true;
		if (reload) {
		    if (s->unload(true)) {
			Engine::self()->m_libs.remove(s);
			ok = false;
		    }
		    else
			msg.retValue() = "Module not unloaded: " + arg + "\r\n";
		}
		else
		    msg.retValue() = "Module is already loaded: " + cmd + "\r\n";
	    }
	    if (!ok) {
		ok = Engine::self()->loadPlugin(Engine::modulePath() + PATH_SEP + arg);
		// if we loaded it successfully we must initialize
		if (ok)
		    Engine::self()->initPlugins();
	    }
	}
	else if (cmd == "unload") {
	    SLib* s = static_cast<SLib*>(Engine::self()->m_libs[arg]);
	    if (!s)
		msg.retValue() = "Module not loaded: " + arg + "\r\n";
	    else if (s->unload(true)) {
		Engine::self()->m_libs.remove(s);
		msg.retValue() = "Unloaded module: " + arg + "\r\n";
	    }
	    else
		msg.retValue() = "Could not unload module: " + arg + "\r\n";
	    ok = true;
	}
    }
    else if (line == "list") {
	msg.retValue().clear();
	for (ObjList* l = Engine::self()->m_libs.skipNull();l;l = l->skipNext()) {
	    SLib* s = static_cast<SLib*>(l->get());
	    msg.retValue().append(*s,"\t");
	    if (s->unload(false))
		msg.retValue() += "*";
	}
	msg.retValue() << "\r\n";
	ok = true;
    }
    if (!ok)
	msg.retValue() = "Module operation failed: " + line + "\r\n";
    return true;
}


bool EngineHelp::received(Message &msg)
{
    const char* opts = (s_nounload ? s_cmdsOptNoUnload : s_cmdsOpt);
    String line = msg.getValue("line");
    if (line.null()) {
	msg.retValue() << opts << s_evtsOpt << s_logvOpt;
	return false;
    }
    if (line == YSTRING("module"))
	msg.retValue() << opts << s_cmdsMsg;
    else if (line == YSTRING("events"))
	msg.retValue() << s_evtsOpt << s_evtsMsg;
    else if (line == YSTRING("logview"))
	msg.retValue() << s_logvOpt << s_logvMsg;
    else
	return false;
    return true;
}


void EnginePrivate::run()
{
    setCurrentObjCounter(s_workCnt);
    for (;;) {
	s_makeworker = false;
	Engine::self()->m_dispatcher.dequeue();
	Thread::idle(true);
    }
}


static bool logFileOpen()
{
    if (s_logfile) {
	int flags = O_WRONLY|O_CREAT|O_LARGEFILE;
	if (s_logtruncate) {
	    s_logtruncate = false;
	    flags |= O_TRUNC;
	}
	else
	    flags |= O_APPEND;
	int fd = ::open(s_logfile,flags,0640);
	if (fd >= 0) {
	    // Redirect stdout and stderr to the new file
	    ::fflush(stdout);
	    ::dup2(fd,1);
	    ::fflush(stderr);
	    ::dup2(fd,2);
	    ::close(fd);
	    Debugger::enableOutput(true);
	    return true;
	}
    }
    return false;
}

static int engineRun(EngineLoop loop = 0)
{
    time_t t = ::time(0);
    s_startMsg << "Yate (" << ::getpid() << ") is starting " << ::ctime(&t);
    s_startMsg.trimSpaces();
    Output("%s",s_startMsg.c_str());
    Thread::setCurrentObjCounter((s_counter = GenObject::getObjCounter("engine")));
    s_workCnt = GenObject::getObjCounter("workers");
    int retcode = Engine::self()->engineInit();
    if (!retcode)
	retcode = (loop ? loop() : Engine::self()->run());
    if (!retcode)
	retcode = Engine::self()->engineCleanup();
    t = ::time(0);
    Output("Yate (%u) is stopping %s",::getpid(),::ctime(&t));
    return retcode;
}

#ifdef _WINDOWS

static SERVICE_STATUS_HANDLE s_handler = 0;
static SERVICE_STATUS s_status;

static void setStatus(DWORD state)
{
    if (!s_handler)
	return;
    switch (state) {
	case SERVICE_START_PENDING:
	case SERVICE_STOP_PENDING:
	    break;
	default:
	    s_status.dwCheckPoint = 0;
    }
    s_status.dwCurrentState = state;
    ::SetServiceStatus(s_handler,&s_status);
}

static void checkPoint()
{
    if (!s_handler)
	return;
    s_status.dwCheckPoint++;
    ::SetServiceStatus(s_handler,&s_status);
}

static void WINAPI serviceHandler(DWORD code)
{
    switch (code) {
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
	    Engine::halt(0);
	    setStatus(SERVICE_STOP_PENDING);
	    break;
	case SERVICE_CONTROL_PARAMCHANGE:
	    Engine::init();
	    break;
	case SERVICE_CONTROL_INTERROGATE:
	    break;
	default:
	    Debug(DebugWarn,"Got unexpected service control code %u",code);
    }
    if (s_handler)
	::SetServiceStatus(s_handler,&s_status);
}

static void serviceMain(DWORD argc, LPTSTR* argv)
{
    logFileOpen();
    s_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s_status.dwCurrentState = SERVICE_START_PENDING;
    s_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PARAMCHANGE;
    s_status.dwWin32ExitCode = NO_ERROR;
    s_status.dwServiceSpecificExitCode = 0;
    s_status.dwCheckPoint = 0;
    s_status.dwWaitHint = 0;
    s_handler = ::RegisterServiceCtrlHandler("yate",serviceHandler);
    if (!s_handler) {
	Debug(DebugFail,"Could not register service control handler \"yate\", code %u",::GetLastError());
	return;
    }
    setStatus(SERVICE_START_PENDING);
    engineRun();
}

static SERVICE_TABLE_ENTRY dispatchTable[] =
{
    { TEXT("yate"), (LPSERVICE_MAIN_FUNCTION)serviceMain },
    { NULL, NULL }
};

#else /* _WINDOWS */

#define setStatus(s)
#define checkPoint()

static bool s_logrotator = false;
static volatile bool s_rotatenow = false;
static volatile bool s_runagain = true;
static unsigned int s_rundelay = RUNDELAY_MIN;
static pid_t s_childpid = -1;
static pid_t s_superpid = -1;

static void superhandler(int signal)
{
    switch (signal) {
	case SIGUSR1:
	case SIGUSR2:
	    s_rundelay = RUNDELAY_MIN;
	    break;
	case SIGHUP:
	    if (!s_interactive) {
		if (s_logrotator)
		    s_rotatenow = true;
		break;
	    }
	    // intentionally fall through
	case SIGINT:
	case SIGTERM:
	case SIGABRT:
	    s_runagain = false;
    }
    if (s_childpid > 0)
	::kill(s_childpid,signal);
}

static void rotatelogs()
{
    if (s_rotatenow) {
	s_rotatenow = false;
	::fprintf(stderr,"Supervisor (%d) closing the log file\n",s_superpid);
	logFileOpen();
	::fprintf(stderr,"Supervisor (%d) reopening the log file\n",s_superpid);
    }
}

static void copystream(int dest, int src)
{
    for (;;) {
	char buf[MAX_LOGBUFF];
	int rd = ::read(src,buf,sizeof(buf));
	if (rd <= 0)
	    break;
	YIGNORE(::write(dest,buf,rd));
    }
    rotatelogs();
}

static int supervise(int initDelay)
{
    s_superpid = ::getpid();
    ::fprintf(stderr,"Supervisor (%u) is starting\n",s_superpid);
    ::signal(SIGINT,superhandler);
    ::signal(SIGTERM,superhandler);
    ::signal(SIGHUP,superhandler);
    ::signal(SIGQUIT,superhandler);
    ::signal(SIGABRT,superhandler);
    ::signal(SIGUSR1,superhandler);
    ::signal(SIGUSR2,superhandler);
    ::signal(SIGALRM,SIG_IGN);
    if (initDelay > 0) {
	::fprintf(stderr,"Supervisor (%d) delaying initial start by %u.%02u seconds\n",
		s_superpid,(initDelay + 5) / 1000,((initDelay + 5) / 10) % 100);
	::usleep(initDelay * 1000);
    }
    int retcode = 0;
    while (s_runagain) {
	int wdogfd[2];
	if (::pipe(wdogfd)) {
	    int err = errno;
	    ::fprintf(stderr,"Supervisor: watchdog pipe failed: %s (%d)\n",::strerror(err),err);
	    return err;
	}
	::fcntl(wdogfd[0],F_SETFL,O_NONBLOCK);
	::fcntl(wdogfd[1],F_SETFL,O_NONBLOCK);
	int logfd[2] = { -1, -1 };
	if (s_logrotator) {
	    if (::pipe(logfd)) {
		int err = errno;
		::fprintf(stderr,"Supervisor: log pipe failed: %s (%d)\n",::strerror(err),err);
		return err;
	    }
	    ::fcntl(logfd[0],F_SETFL,O_NONBLOCK);
	}
	// reap any children we may have before spawning a new one
	while (::waitpid(-1,0,WNOHANG) > 0)
	    ;
	rotatelogs();
	s_run_attempt++;
	s_childpid = ::fork();
	if (s_childpid < 0) {
	    int err = errno;
	    ::fprintf(stderr,"Supervisor: fork failed: %s (%d)\n",::strerror(err),err);
	    return err;
	}
	if (s_childpid == 0) {
	    s_super_handle = wdogfd[1];
	    ::close(wdogfd[0]);
	    if (s_logrotator) {
		::close(logfd[0]);
		// Redirect stdout and stderr to the new file
		::fflush(stdout);
		::dup2(logfd[1],1);
		::fflush(stderr);
		::dup2(logfd[1],2);
		::close(logfd[1]);
	    }
	    ::signal(SIGINT,SIG_DFL);
	    ::signal(SIGTERM,SIG_DFL);
	    ::signal(SIGHUP,SIG_DFL);
	    ::signal(SIGQUIT,SIG_DFL);
	    ::signal(SIGABRT,SIG_DFL);
	    return -1;
	}
	::close(wdogfd[1]);
	if (s_logrotator)
	    ::close(logfd[1]);
	// Wait for the child to die or block
	for (int sanity = INIT_SANITY; sanity > 0; sanity--) {
	    int status = -1;
	    int tmp = ::waitpid(s_childpid,&status,WNOHANG);
	    if (tmp > 0) {
		// Child exited for some reason
		if (WIFEXITED(status)) {
		    retcode = WEXITSTATUS(status);
		    ::fprintf(stderr,"Supervisor: child %d exited with code %d\n",s_childpid,retcode);
		    if (retcode <= 127)
			s_runagain = false;
		    else
			retcode &= 127;
		    s_childsig = 0;
		}
		else if (WIFSIGNALED(status)) {
		    retcode = WTERMSIG(status);
		    ::fprintf(stderr,"Supervisor: child %d died on signal %d\n",s_childpid,retcode);
		    s_childsig = retcode;
		}
		s_childpid = -1;
		break;
	    }

	    char buf[MAX_SANITY];
	    tmp = ::read(wdogfd[0],buf,sizeof(buf));
	    if (tmp >= 0) {
		// Timer messages add one sanity point every second
		tmp += sanity;
		if (tmp > MAX_SANITY)
		    tmp = MAX_SANITY;
		if (sanity < tmp)
		    sanity = tmp;
		// Decrement inter-run delay each time child proves sanity
		s_rundelay -= RUNDELAY_DEC;
		if (s_rundelay < RUNDELAY_MIN)
		    s_rundelay = RUNDELAY_MIN;
	    }
	    else if ((errno != EINTR) && (errno != EAGAIN))
		break;
	    // Consume sanity points slightly slower than added
	    for (int i = 0; i < 12; i++) {
		if (s_logrotator)
		    copystream(2,logfd[0]);
		::usleep(100000);
	    }
	}
	::close(wdogfd[0]);
	if (s_childpid > 0) {
	    // Child failed to proof sanity. Kill it - no need to be gentle.
	    ::fprintf(stderr,"Supervisor: killing unresponsive child %d\n",s_childpid);
#ifdef RLIMIT_CORE
	    // If -Da or -C were specified try to get a corefile
	    if (s_sigabrt || s_coredump) {
#else
	    // If -Da was specified try to get a corefile
	    if (s_sigabrt) {
#endif
		::kill(s_childpid,SIGABRT);
		::usleep(500000);
	    }
	    // Try to kill until it dies or we get a termination signal
	    while ((s_childpid > 0) && !::kill(s_childpid,SIGKILL)) {
		if (!s_runagain)
		    break;
		::usleep(100000);
		int status = -1;
		if (::waitpid(s_childpid,&status,WNOHANG) > 0)
		    break;
	    }
	    s_childpid = -1;
	    s_childsig = SIGCHLD;
	}
	if (s_logrotator) {
	    copystream(2,logfd[0]);
	    ::close(logfd[0]);
	}
	if (s_runagain) {
	    if (s_rundelay > RUNDELAY_MIN)
		::fprintf(stderr,"Supervisor (%d) delaying child start by %u.%02u seconds\n",
		    s_superpid,s_rundelay / 1000000,(s_rundelay / 10000) % 100);
	    ::usleep(s_rundelay);
	    // Exponential backoff, double run delay each time we restart child
	    s_rundelay *= 2;
	    if (s_rundelay > RUNDELAY_MAX)
		s_rundelay = RUNDELAY_MAX;
	}
    }
    ::fprintf(stderr,"Supervisor (%d) exiting with code %d\n",s_superpid,retcode);
    return retcode;
}
#endif /* _WINDOWS */


SLib::SLib(HMODULE handle, const char* file, bool nounload, unsigned int count)
    : String(moduleBase(file)),
      m_handle(handle), m_nounload(nounload), m_count(count)
{
    DDebug(DebugAll,"SLib::SLib(%p,'%s',%s,%u) [%p]",
	handle,file,String::boolText(nounload),count,this);
    checkPoint();
}

SLib::~SLib()
{
#ifdef DEBUG
    Debugger debug("SLib::~SLib()"," '%s' %u [%p]",c_str(),m_count,this);
#endif
    unsigned int count = plugins.count();
    if (s_nounload || m_nounload) {
#ifdef _WINDOWS
	typedef void (WINAPI *pFini)(HINSTANCE,DWORD,LPVOID);
	pFini fini = (pFini)GetProcAddress(m_handle,"_DllMainCRTStartup");
	if (!fini)
	    fini = (pFini)GetProcAddress(m_handle,"_CRT_INIT");
	if (fini)
	    fini(m_handle,DLL_PROCESS_DETACH,NULL);
#else
	typedef void (*pFini)();
	pFini fini = (pFini)dlsym(m_handle,"_fini");
	if (fini)
	    fini();
#endif
	if (fini || m_nounload) {
	    count -= plugins.count();
	    if (count != m_count)
		Debug(DebugGoOn,"Finalizing '%s' removed %u out of %u plugins",
		    c_str(),count,m_count);
	    checkPoint();
	    return;
	}
	Debug(DebugWarn,"Could not finalize '%s', will dlclose(%p)",c_str(),m_handle);
    }
    int err = dlclose(m_handle);
    if (err)
	Debug(DebugGoOn,"Error %d on dlclose(%p) of '%s'",err,m_handle,c_str());
    else if (s_keepclosing) {
	int tries;
	for (tries=0; tries<10; tries++)
	    if (dlclose(m_handle))
		break;
	if (tries)
	    Debug(DebugGoOn,"Made %d attempts to dlclose(%p) '%s'",
		tries,m_handle,c_str());
    }
    count -= plugins.count();
    if (count != m_count)
	Debug(DebugGoOn,"Unloading '%s' removed %u out of %u plugins",
	    c_str(),count,m_count);
    checkPoint();
}

SLib* SLib::load(const char* file, bool local, bool nounload)
{
    DDebug(DebugAll,"SLib::load('%s')",file);
    int flags = RTLD_NOW;
#ifdef RTLD_GLOBAL
    if (!local)
	flags |= RTLD_GLOBAL;
#endif
    unsigned int count = plugins.count();
    HMODULE handle = ::dlopen(file,flags);
    if (handle)
	return new SLib(handle,file,nounload,plugins.count() - count);
#ifdef _WINDOWS
    Debug(DebugWarn,"LoadLibrary error %u in '%s'",::GetLastError(),file);
#else
    Debug(DebugWarn,"%s",dlerror());
#endif
    return 0;
}

bool SLib::unload(bool unloadNow)
{
    typedef bool (*pUnload)(bool);
    if (m_nounload)
	return false;
    pUnload unl = (pUnload)dlsym(m_handle,"_unload");
    return (unl != 0) && unl(unloadNow);
}


void SharedVars::get(const String& name, String& rval)
{
    lock();
    rval = m_vars.getValue(name,rval);
    unlock();
}

void SharedVars::set(const String& name, const char* val)
{
    lock();
    m_vars.setParam(name,val);
    unlock();
}

bool SharedVars::create(const String& name, const char* val)
{
    Lock mylock(this);
    if (m_vars.getParam(name))
	return false;
    m_vars.addParam(name,val);
    return true;
}

void SharedVars::clear(const String& name)
{
    lock();
    m_vars.clearParam(name);
    unlock();
}

bool SharedVars::exists(const String& name)
{
    Lock mylock(this);
    return m_vars.getParam(name) != 0;
}

unsigned int SharedVars::inc(const String& name, unsigned int wrap)
{
    Lock mylock(this);
    unsigned int val = m_vars.getIntValue(name);
    if (wrap)
	val = val % (wrap + 1);
    unsigned int nval = val + 1;
    if (wrap)
	nval = nval % (wrap + 1);
    m_vars.setParam(name,String(nval));
    return val;
}

unsigned int SharedVars::dec(const String& name, unsigned int wrap)
{
    Lock mylock(this);
    unsigned int val = m_vars.getIntValue(name);
    if (wrap)
	val = val ? ((val - 1) % (wrap + 1)) : wrap;
    else
	val = val ? (val - 1) : 0;
    m_vars.setParam(name,String(val));
    return val;
}


Engine::Engine()
{
    DDebug(DebugAll,"Engine::Engine() [%p]",this);
    initUsrPath(s_usrpath);
}

Engine::~Engine()
{
#ifdef DEBUG
    Debugger debug("Engine::~Engine()"," libs=%u plugins=%u [%p]",
	m_libs.count(),plugins.count(),this);
#endif
    assert(this == s_self);
    m_dispatcher.clear();
    m_libs.clear();
    s_events.clear();
    s_mode = Stopped;
    s_self = 0;
}

int Engine::engineInit()
{
#ifdef _WINDOWS
    // In Windows we must initialize the socket library very early because even trivial
    //  functions like endianess swapping - ntohl and family - need it to be initialized
    WSADATA wsaData;
    int errc = ::WSAStartup(MAKEWORD(2,2), &wsaData);
    if (errc) {
	Debug(DebugGoOn,"Failed to initialize the Windows Sockets library, error code %d",errc);
	return errc & 127;
    }
#else
    ::signal(SIGPIPE,SIG_IGN);
    ::signal(SIGALRM,SIG_IGN);
#endif
    CapturedEvent::capturing(s_capture);
    s_cfg = configFile(s_cfgfile);
    s_cfg.load();
    s_capture = s_cfg.getBoolValue("general","startevents",s_capture);
    CapturedEvent::capturing(s_capture);
    if (s_capture && s_startMsg)
	CapturedEvent::append(-1,s_startMsg);
    String track = s_cfg.getValue("general","trackparam");
    if (track.null() || track.toBoolean(false))
	track = "handlers";
    else if (!track.toBoolean(true))
	track.clear();
    if (track)
	m_dispatcher.trackParam(track);
#ifdef _WINDOWS
    int winTimerRes = s_cfg.getIntValue("general","wintimer");
    if ((winTimerRes > 0) && (winTimerRes < 100)) {
	typedef ULONG (__stdcall *NTSTR)(ULONG,BOOLEAN,PULONG);
	String err;
	HMODULE ntDll = GetModuleHandle("NTDLL.DLL");
	if (ntDll) {
	    NTSTR ntSTR = (NTSTR)GetProcAddress(ntDll,"NtSetTimerResolution");
	    if (ntSTR) {
		ULONG res = 0;
		unsigned int ntstatus = ntSTR(10000*winTimerRes,true,&res);
		if (ntstatus)
		    err << "NTSTATUS " << ntstatus;
	    }
	    else
		err = "NtSetTimerResolution not found";
	}
	else
	    err = "NTDLL not found (Windows 9x or ME?)";
	if (err)
	    Debug(DebugWarn,"Timer resolution not set: %s",err.c_str());
    }
#endif
    Thread::idleMsec(s_cfg.getIntValue("general","idlemsec",(clientMode() ? 2 * Thread::idleMsec() : 0)));
    SysUsage::init();

    s_runid = Time::secNow();
    if (s_node.trimBlanks().null()) {
	char hostName[HOST_NAME_MAX+1];
	if (::gethostname(hostName,sizeof(hostName)))
	    hostName[0] = '\0';
	s_node = s_cfg.getValue("general","nodename",hostName);
	s_node.trimBlanks();
    }
    const char *modPath = s_cfg.getValue("general","modpath");
    if (modPath)
	s_modpath = modPath;
    s_minworkers = s_cfg.getIntValue("general","minworkers",s_minworkers,1,25);
    s_maxworkers = s_cfg.getIntValue("general","maxworkers",s_maxworkers,s_minworkers);
    s_maxevents = s_cfg.getIntValue("general","maxevents",s_maxevents);
    s_restarts = s_cfg.getIntValue("general","restarts");
    m_dispatcher.warnTime(1000*(u_int64_t)s_cfg.getIntValue("general","warntime"));
    extraPath(clientMode() ? "client" : "server");
    extraPath(s_cfg.getValue("general","extrapath"));

    s_params.addParam("version",YATE_VERSION);
    s_params.addParam("release",YATE_STATUS YATE_RELEASE);
    s_params.addParam("revision",YATE_REVISION);
    s_params.addParam("nodename",s_node);
    s_params.addParam("runid",String(s_runid));
    s_params.addParam("configname",s_cfgfile);
    s_params.addParam("sharedpath",s_shrpath);
    s_params.addParam("configpath",s_cfgpath);
    s_params.addParam("usercfgpath",s_usrpath);
    s_params.addParam("cfgsuffix",s_cfgsuffix);
    s_params.addParam("modulepath",s_modpath);
    s_params.addParam("modsuffix",s_modsuffix);
    s_params.addParam("logfile",s_logfile);
    s_params.addParam("interactive",String::boolText(s_interactive));
    s_params.addParam("clientmode",String::boolText(clientMode()));
    s_params.addParam("supervised",String::boolText(s_super_handle >= 0));
    s_params.addParam("runattempt",String(s_run_attempt));
#ifndef _WINDOWS
    s_params.addParam("lastsignal",String(s_childsig));
#endif
    s_params.addParam("minworkers",String(s_minworkers));
    s_params.addParam("maxworkers",String(s_maxworkers));
    s_params.addParam("maxevents",String(s_maxevents));
    if (track)
	s_params.addParam("trackparam",track);
#ifdef _WINDOWS
    {
	char buf[PATH_MAX];
	DWORD ret = ::GetCurrentDirectoryA(PATH_MAX,buf);
	if (ret && (ret < PATH_MAX))
	    s_params.addParam("workpath",buf);
    }
#elif defined (HAVE_GETCWD)
    {
	char buf[PATH_MAX];
	if (::getcwd(buf,PATH_MAX))
	    s_params.addParam("workpath",buf);
    }
#endif
    NamedList* vars = s_cfg.getSection("variables");
    if (vars) {
	unsigned int n = vars->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* v = vars->getParam(i);
	    if (v)
		s_vars.set(v->name(),*v);
	}
    }
    DDebug(DebugAll,"Engine::run()");
    install(new EngineStatusHandler);
    install(new EngineEventHandler);
    install(new EngineCommand);
    install(new EngineHelp);
    loadPlugins();
    Debug(DebugAll,"Loaded %d plugins",plugins.count());
    if (s_super_handle >= 0) {
	install(new EngineSuperHandler);
	if (s_restarts)
	    s_restarts = 1000000 * s_restarts + Time::now();
    }
    else if (s_restarts) {
	Debug(DebugWarn,"No supervisor - disabling automatic restarts");
	s_restarts = 0;
    }
    initPlugins();
    checkPoint();
    ::signal(SIGINT,sighandler);
    ::signal(SIGTERM,sighandler);
    Debug(DebugAll,"Engine dispatching start message");
    dispatch("engine.start",true);
    s_started = true;
    internalStatisticsStart();
    setStatus(SERVICE_RUNNING);
#ifndef _WINDOWS
    ::signal(SIGHUP,sighandler);
    ::signal(SIGQUIT,sighandler);
    ::signal(SIGCHLD,sighandler);
    ::signal(SIGUSR1,sighandler);
    ::signal(SIGUSR2,sighandler);
#endif
    if (s_startMsg) {
	Message* m = new Message("module.update");
	m->addParam("fulltext",s_startMsg);
	if (nodeName())
	    m->addParam("nodename",nodeName());
	enqueue(m);
    }
    Output("Yate%s engine is initialized and starting up%s%s",
	clientMode() ? " client" : "",s_node.null() ? "" : " on " ,s_node.safe());

    return 0;
}

int Engine::run()
{
    // engine loop
    long corr = 0;
    int stops = MAX_STOP;
    while (s_haltcode == -1 || ((--stops >= 0) && dispatch("engine.stop",true))) {
	if (s_cmds) {
	    Output("Executing initial commands");
	    for (ObjList* c = s_cmds->skipNull(); c; c=c->skipNext()) {
		String* s = static_cast<String*>(c->get());
		Message m("engine.command");
		m.addParam("line",*s);
		if (dispatch(m)) {
		    if (m.retValue())
			Output("%s",m.retValue().c_str());
		}
		else
		    Debug(DebugWarn,"Unrecognized command '%s'",s->c_str());
	    }
	    destruct(s_cmds);
	}

	if (s_init) {
	    s_init = false;
	    initPlugins();
	}

	if (s_debug) {
	    // one-time sending of debug setup messages
	    s_debug = false;
	    const NamedList* sect = s_cfg.getSection("debug");
	    if (sect) {
		unsigned int n = sect->length();
		for (unsigned int i = 0; i < n; i++) {
		    const NamedString* str = sect->getParam(i);
		    if (!(str && str->name() && *str))
			continue;
		    Message* m = new Message("engine.debug");
		    m->addParam("module",str->name());
		    m->addParam("line",*str);
		    enqueue(m);
		}
	    }
	}
	else if (s_capture) {
	    // end capturing startup messages
	    s_capture = false;
	    CapturedEvent::capturing(false);
	}

	if (s_exit >= 0) {
	    halt(s_exit);
	    s_exit = -1;
	}

	// Create worker thread if we didn't hear about any of them in a while
	if (s_makeworker && (EnginePrivate::count < s_maxworkers)) {
	    int build = s_minworkers - EnginePrivate::count;
	    if (EnginePrivate::count)
		Alarm("engine","performance",(build > -3) ? DebugMild : DebugWarn,
		    "Creating new message dispatching thread (%d running)",EnginePrivate::count);
	    else
		Debug(DebugInfo,"Creating first %d message dispatching threads",build);
	    do {
		(new EnginePrivate)->startup();
	    } while (--build > 0);
	}
	else
	    s_makeworker = true;

	if (s_restarts && (Time::now() >= s_restarts)) {
	    if (!(usedPlugins() || dispatch("engine.busy"))) {
		s_haltcode = 128;
		break;
	    }
	    DDebug(DebugAll,"Engine busy - will try to restart later");
	    // If we cannot restart now try again in 10s
	    s_restarts = Time::now() + 10000000;
	}

	// Attempt to sleep until the next full second
	long t = 1000000 - (long)(Time::now() % 1000000) - corr;
	if (t < 250000)
	    t += 1000000;
	XDebug(DebugAll,"Sleeping for %ld",t);
	Thread::usleep(t);
	Message* m = new Message("engine.timer",0,true);
	m->addParam("time",String(m->msgTime().sec()));
	if (nodeName())
	    m->addParam("nodename",nodeName());
	if (s_haltcode == -1) {
	    // Try to fine tune the ticker unless exiting
	    t = (long)(m->msgTime().usec() % 1000000);
	    if (t > 500000)
		corr -= (1000000-t)/10;
	    else
		corr += t/10;
	    XDebug(DebugAll,"Adjustment at %ld, corr %ld",t,corr);
	}
	enqueue(m);
	Thread::yield();
    }
    s_haltcode &= 0xff;
    return 0;
}

int Engine::engineCleanup()
{
    Output("Yate engine is shutting down with code %d",s_haltcode);
    CapturedEvent::capturing(false);
    setStatus(SERVICE_STOP_PENDING);
    ::signal(SIGINT,SIG_DFL);
    Lock myLock(s_hooksMutex);
    for (ObjList* o = s_hooks.skipNull();o;o = o->skipNext()) {
	MessageHook* mh = static_cast<MessageHook*>(o->get());
	mh->clear();
    }
    myLock.drop();
    dispatch("engine.halt",true);
    checkPoint();
    Thread::msleep(200);
    m_dispatcher.dequeue();
    checkPoint();
    // We are occasionally doing things that can cause crashes so don't abort
    abortOnBug(s_sigabrt && s_lateabrt);
    Thread::killall();
    checkPoint();
    m_dispatcher.dequeue();
    ::signal(SIGTERM,SIG_DFL);
#ifndef _WINDOWS
    ::signal(SIGHUP,SIG_DFL);
    ::signal(SIGQUIT,SIG_DFL);
#endif
    delete this;
    int mux = Mutex::locks();
    if (mux < 0)
	mux = 0;
    unsigned int cnt = plugins.count();
    plugins.clear();
    if (mux || cnt)
	Debug(DebugGoOn,"Exiting with %d locked mutexes and %u plugins loaded!",mux,cnt);
    if (GenObject::getObjCounting()) {
	String str;
	int obj = EngineStatusHandler::objects(str);
	if (str)
	    Debug(DebugNote,"Exiting with %d allocated objects: %s",obj,str.c_str());
    }
#ifdef _WINDOWS
    ::WSACleanup();
#endif
    setStatus(SERVICE_STOPPED);
    return s_haltcode;
}

void Engine::internalStatisticsStart()
{
    // This is here so runtime analyzers can start or reset statistics
    //  after the cruft of module load + global objects initialization
}

Engine* Engine::self()
{
    if (!s_self)
	s_self = new Engine;
    return s_self;
}

void Engine::setCongestion(const char* reason)
{
    unsigned int cong = 2;
    s_congMutex.lock();
    if (reason)
	cong = ++s_congestion;
    else if (s_congestion)
	cong = --s_congestion;
    s_congMutex.unlock();
    switch (cong) {
	case 0:
	    Alarm("engine","performance",DebugNote,"Engine congestion ended");
	    break;
	case 1:
	    if (reason)
		Alarm("engine","performance",DebugWarn,"Engine is congested: %s",reason);
	    break;
    }
}

const char* Engine::pathSeparator()
{
    return PATH_SEP;
}

const String& Engine::configPath(bool user)
{
    if (user) {
	if (s_createusr) {
	    // create user data dir on first request
	    s_createusr = false;
	    if (::mkdir(s_usrpath,S_IRWXU) == 0)
		Debug(DebugNote,"Created user data directory: '%s'",s_usrpath.c_str());
	}
	return s_usrpath;
    }
    return s_cfgpath;
}

String Engine::configFile(const char* name, bool user)
{
    String path = configPath(user);
    if (!path.endsWith(PATH_SEP))
	path += PATH_SEP;
    return path + name + s_cfgsuffix;
}

const Configuration& Engine::config()
{
    return s_cfg;
}

bool Engine::Register(const Plugin* plugin, bool reg)
{
    DDebug(DebugInfo,"Engine::Register(%p,%d)",plugin,reg);
    ObjList *p = plugins.find(plugin);
    if (reg) {
	if (p)
	    return false;
	if (plugin->earlyInit()) {
	    s_loadMode = LoadEarly;
	    p = plugins.insert(plugin);
	}
	else
	    p = plugins.append(plugin);
	p->setDelete(s_dynplugin);
    }
    else if (p)
	p->remove(false);
    return true;
}

bool Engine::loadPlugin(const char* file, bool local, bool nounload)
{
    s_dynplugin = false;
    s_loadMode = Engine::LoadLate;
    SLib *lib = SLib::load(file,local,nounload);
    s_dynplugin = true;
    if (lib) {
	switch (s_loadMode) {
	    case LoadFail:
		delete lib;
		return false;
	    case LoadEarly:
		// load early - unload late
		m_libs.append(lib);
		break;
	    default:
		m_libs.insert(lib);
		break;
	}
	return true;
    }
    return false;
}

void Engine::pluginMode(PluginMode mode)
{
    s_loadMode = mode;
}

void Engine::tryPluginFile(const String& name, const String& path, bool defload)
{
    XDebug(DebugInfo,"Found dir entry: %s",name.c_str());
    if (s_modsuffix && !name.endsWith(s_modsuffix))
	return;
    const String* s = s_cfg.getKey(YSTRING("modules"),name);
    if (s) {
	if (!s->toBoolean(defload || s->null()))
	    return;
    }
    else if (!defload)
	return;

    loadPlugin(path + PATH_SEP + name,
	s_cfg.getBoolValue(YSTRING("localsym"),name,s_localsymbol),
	s_cfg.getBoolValue(YSTRING("nounload"),name));
}

bool Engine::loadPluginDir(const String& relPath)
{
#ifdef DEBUG
    Debugger debug("Engine::loadPluginDir","('%s')",relPath.c_str());
#endif
    bool defload = s_cfg.getBoolValue("general","modload",true);
    String path = s_modpath;
    static const Regexp r("^\\([/\\]\\|[[:alpha:]]:[/\\]\\).");
    if (r.matches(relPath))
	path = relPath;
    else if (relPath) {
	if (!path.endsWith(PATH_SEP))
	    path += PATH_SEP;
	path += relPath;
    }
    if (path.endsWith(s_modsuffix)) {
	int sep = path.rfind(PATH_SEP[0]);
	if (sep >= 0)
	    sep++;
	else
	    sep = 0;
	String name = path.substr(sep);
	if (loadPlugin(path,s_cfg.getBoolValue("localsym",name,s_localsymbol),
	    s_cfg.getBoolValue("nounload",name)))
	    return true;
    }
    if (path.endsWith(PATH_SEP))
	path = path.substr(0,path.length()-1);
#ifdef _WINDOWS
    WIN32_FIND_DATA entry;
    HANDLE hf = ::FindFirstFile(path + PATH_SEP "*",&entry);
    if (hf == INVALID_HANDLE_VALUE) {
	Debug(DebugWarn,"Engine::loadPlugins() failed directory '%s'",path.safe());
	return false;
    }
    do {
	tryPluginFile(entry.cFileName,path,defload);
    } while (::FindNextFile(hf,&entry) && !exiting());
    ::FindClose(hf);
#else
    DIR *dir = ::opendir(path);
    if (!dir) {
	Debug(DebugWarn,"Engine::loadPlugins() failed directory '%s'",path.safe());
	return false;
    }
    struct dirent *entry;
    while (((entry = ::readdir(dir)) != 0) && !exiting())
	tryPluginFile(entry->d_name,path,defload);
    ::closedir(dir);
#endif
    return true;
}

void Engine::loadPlugins()
{
    NamedList *l = s_cfg.getSection("preload");
    if (l) {
        unsigned int len = l->length();
        for (unsigned int i=0; i<len; i++) {
            NamedString *n = l->getParam(i);
            if (n && n->toBoolean(n->null())) {
        	String path(n->name());
        	s_params.replaceParams(path);
                loadPlugin(path);
            }
	    if (exiting())
		break;
	}
    }
    loadPluginDir(String::empty());
    while (GenObject* extra = s_extramod.remove(false)) {
	loadPluginDir(extra->toString());
	extra->destruct();
    }
    l = s_cfg.getSection("postload");
    if (l) {
        unsigned int len = l->length();
        for (unsigned int i=0; i<len; i++) {
	    if (exiting())
		return;
            NamedString *n = l->getParam(i);
            if (n && n->toBoolean(n->null())) {
        	String path(n->name());
        	s_params.replaceParams(path);
                loadPlugin(path);
	    }
	}
    }
}

void Engine::initPlugins()
{
    if (exiting())
	return;
    Output("Initializing plugins");
    dispatch("engine.init",true);
    ObjList *l = plugins.skipNull();
    for (; l; l = l->skipNext()) {
	Plugin *p = static_cast<Plugin *>(l->get());
	TempObjectCounter cnt(p->objectsCounter(),true);
	p->initialize();
	if (exiting()) {
	    Output("Initialization aborted, exiting...");
	    return;
	}
    }
    Output("Initialization complete");
}

int Engine::usedPlugins()
{
    int used = 0;
    ObjList *l = plugins.skipNull();
    for (; l; l = l->skipNext()) {
	Plugin *p = static_cast<Plugin *>(l->get());
	if (p->isBusy())
	    used++;
    }
    return used;
}

void Engine::extraPath(const String& path)
{
    if (path.null() || s_extramod.find(path))
	return;
    s_extramod.append(new String(path));
}

void Engine::userPath(const String& path)
{
    if (path.null())
	return;
    if (s_usrpath.null())
	s_userdir = path;
    else
	Debug(DebugWarn,"Engine::userPath('%s') called too late!",path.c_str());
}

void Engine::halt(unsigned int code)
{
    if (s_haltcode == -1)
	s_haltcode = code;
}

bool Engine::restart(unsigned int code, bool gracefull)
{
    if ((s_super_handle < 0) || (s_haltcode != -1))
	return false;
    if (gracefull)
	s_restarts = 1;
    else
	s_haltcode = (code & 0xff) | 0x80;
    return true;
}

void Engine::init()
{
    s_init = true;
}

bool Engine::init(const String& name)
{
    if (exiting() || !s_self)
	return false;
    if (name.null() || name == "*" || name == "all") {
	s_init = true;
	return true;
    }
    Output("Initializing plugin '%s'",name.c_str());
    Message msg("engine.init",0,true);
    msg.addParam("plugin",name);
    if (nodeName())
	msg.addParam("nodename",nodeName());
    bool ok = s_self->m_dispatcher.dispatch(msg);
    Plugin* p = static_cast<Plugin*>(plugins[name]);
    if (p) {
	TempObjectCounter cnt(p->objectsCounter(),true);
	p->initialize();
	ok = true;
    }
    return ok;
}

bool Engine::install(MessageHandler* handler)
{
    return s_self ? s_self->m_dispatcher.install(handler) : false;
}

bool Engine::uninstall(MessageHandler* handler)
{
    return s_self ? s_self->m_dispatcher.uninstall(handler) : false;
}

bool Engine::enqueue(Message* msg, bool skipHooks)
{
    if (!msg)
	return false;
    if (!skipHooks) {
	Lock myLock(s_hooksMutex);
	for (ObjList* o = s_hooks.skipNull();o;o = o->skipNext()) {
	    MessageHook* hook = static_cast<MessageHook*>(o->get());
	    if (!hook || !hook->matchesFilter(*msg))
		continue;
	    RefPointer<MessageHook> rhook = hook;
	    myLock.drop();
	    rhook->enqueue(msg);
	    return true;
	}
    }
    return s_self ? s_self->m_dispatcher.enqueue(msg) : false;
}

bool Engine::dispatch(Message* msg)
{
    return (msg && s_self) ? s_self->m_dispatcher.dispatch(*msg) : false;
}

bool Engine::dispatch(Message& msg)
{
    return s_self ? s_self->m_dispatcher.dispatch(msg) : false;
}

bool Engine::dispatch(const char* name, bool broadcast)
{
    if (!(s_self && name && *name))
	return false;
    Message msg(name,0,broadcast);
    if (nodeName())
	msg.addParam("nodename",nodeName());
    return s_self->m_dispatcher.dispatch(msg);
}

bool Engine::installHook(MessageHook* hook)
{
    Lock myLock(s_hooksMutex);
    if (!hook || s_hooks.find(hook))
	return false;
    s_hooks.append(hook);
    return true;
}

void Engine::uninstallHook(MessageHook* hook)
{
    if (!hook)
	return;
    Lock myLock(s_hooksMutex);
    hook->clear();
    s_hooks.remove(hook);
}

unsigned int Engine::runId()
{
    return s_runid;
}

const ObjList* Engine::events(const String& type)
{
    if (type.null())
	return CapturedEvent::events().skipNull();
    Lock mylock(s_eventsMutex);
    const ObjList* list = static_cast<const EngineEventList*>(s_events[type]);
    return list ? list->skipNull() : 0;
}

void Engine::clearEvents(const String& type)
{
    Lock mylock(s_eventsMutex);
    if (type.null())
	CapturedEvent::eventsRw().clear();
    else
	s_events.remove(type);
}

SharedVars& Engine::sharedVars()
{
    return s_vars;
}

// Append command line arguments form current config.
void Engine::buildCmdLine(String& line)
{
    String D;
    switch (Debugger::getFormatting()) {
	case Debugger::None:      D << 'n'; break;
	case Debugger::Absolute:  D << 'e'; break;
	case Debugger::Relative:  D << 't'; break;
	case Debugger::Textual:   D << 'f'; break;
	case Debugger::TextLocal: D << 'z'; break;
	case Debugger::TextSep:   D << 'F'; break;
	case Debugger::TextLSep:  D << 'Z'; break;
	default:
	    Debug(DebugStub,"buildCmdLine() unhandled debugger formatting %d",
		Debugger::getFormatting());
    }
    if (s_sigabrt)
	D << 'a';
    if (s_lateabrt)
	D << 's';
    if (Lockable::safety())
	D << 'd';
    if (D)
	line.append("-D" + D," ");
    int level = TelEngine::debugLevel();
    if (level > DebugWarn)
	line.append("-" + String('v',level - DebugWarn)," ");
    else if (level < DebugWarn)
	line.append("-" + String('q',DebugWarn - level)," ");
    line.append("--starttime " + String(Debugger::getStartTimeSec())," ");
}

// Utility: retrieve next String in list, adjust the ObjList parameter
static inline bool nextString(String*& tmp, ObjList*& crt, String* add = 0,
    const String& param = String::empty())
{
    ObjList* next = crt->skipNext();
    if (next) {
	crt = next;
	tmp = static_cast<String*>(crt->get());
	return true;
    }
    if (add)
	add->append(param," ");
    return false;
}

// Initialize library from from command line arguments.
// Enable debugger output
void Engine::initLibrary(const String& line, String* output)
{
    bool colorize = false;
    Debugger::Formatting fmtTime = Debugger::TextLSep;
    uint32_t ts = 0;
    int level = TelEngine::debugLevel();
    Lockable::startUsingNow();
    // Check for options
    ObjList* lst = line.split(' ',false);
    String unkArgs;
    String missingParams;
    String* tmp = 0;
    bool inopt = true;
    for (ObjList* o = lst->skipNull(); o; o = o->skipNext()) {
#define ENGINE_SET_VAL_BREAK(check,dest,src) case check: dest = src; break
#define ENGINE_INSTR_BREAK(check,instr) case check: instr; break
	String& param = *static_cast<String*>(o->get());
	const char* pc = param;
	if (!(inopt && (pc[0] == '-') && pc[1])) {
	    unkArgs.append(param," ");
	    continue;
	}
	while (pc && *++pc) {
	    switch (*pc) {
		case '-':
		    if (!*++pc)
			inopt = false;
		    else if (!::strcmp(pc,"starttime")) {
			if (nextString(tmp,o,&missingParams,param))
			    ts = (uint32_t)tmp->toLong(0,0,0);
		    }
		    else
			unkArgs.append(param," ");
		    pc = 0;
		    continue;
		ENGINE_INSTR_BREAK('v',++level);
		ENGINE_INSTR_BREAK('q',--level);
		case 'D':
		    while (*++pc) {
			switch (*pc) {
			    ENGINE_SET_VAL_BREAK('n',fmtTime,Debugger::None);
			    ENGINE_SET_VAL_BREAK('e',fmtTime,Debugger::Absolute);
			    ENGINE_SET_VAL_BREAK('t',fmtTime,Debugger::Relative);
			    ENGINE_SET_VAL_BREAK('f',fmtTime,Debugger::Textual);
			    ENGINE_SET_VAL_BREAK('z',fmtTime,Debugger::TextLocal);
			    ENGINE_SET_VAL_BREAK('F',fmtTime,Debugger::TextSep);
			    ENGINE_SET_VAL_BREAK('Z',fmtTime,Debugger::TextLSep);
			    ENGINE_SET_VAL_BREAK('o',colorize,true);
			    ENGINE_SET_VAL_BREAK('a',s_sigabrt,true);
			    ENGINE_SET_VAL_BREAK('s',s_lateabrt,true);
			    ENGINE_INSTR_BREAK('m',setLockableWait());
			    ENGINE_INSTR_BREAK('d',Lockable::enableSafety());
			    default:
				unkArgs.append("-D" + String(*pc)," ");
			}
		    }
		    pc = 0;
		    break;
		default:
		    unkArgs.append(param," ");
		    pc = 0;
	    }
	}
#undef ENGINE_SET_VAL_BREAK
#undef ENGINE_INSTR_BREAK
    }
    TelEngine::destruct(lst);
    Thread::idleMsec(0);
    abortOnBug(s_sigabrt);
    TelEngine::debugLevel(level);
    Debugger::setFormatting(fmtTime,ts);
    Debugger::enableOutput(true,colorize);
    if (output) {
	if (unkArgs)
	    *output << "\r\nUnknown argument(s): " << unkArgs;
	if (missingParams)
	    *output << "\r\nMissing parameter for argument(s): " << missingParams;
#ifdef DEBUG
	*output << "\r\ncmdline=" << line;
	*output << "\r\ndebuglevel=" << level;
	*output << "\r\nfmtTime=" << fmtTime;
	*output << "\r\nstart-time=" << ts;
	*output << "\r\ncolorize=" << String::boolText(colorize);
	*output << "\r\nsigabort=" << String::boolText(s_sigabrt);
	*output << "\r\nlateabort=" << String::boolText(s_lateabrt);
	*output << "\r\nlockable-wait=" << (uint64_t)Lockable::wait();
	*output << "\r\nlockable-safety=" << String::boolText(Lockable::safety());
#endif
    }
}

// Cleanup library. Set late abort, kill all threads
int Engine::cleanupLibrary()
{
    // We are occasionally doing things that can cause crashes so don't abort
    abortOnBug(s_sigabrt && s_lateabrt);
    Thread::killall();
    int mux = Mutex::locks();
    if (mux < 0)
	mux = 0;
    if (mux)
	Debug(DebugGoOn,"Exiting with %d locked mutexes!",mux);
    if (GenObject::getObjCounting()) {
	String str;
	int obj = EngineStatusHandler::objects(str);
	if (str)
	    Debug(DebugNote,"Exiting with %d allocated objects: %s",obj,str.c_str());
    }
    return s_haltcode & 0xff;
}

static void usage(bool client, FILE* f)
{
    ::fprintf(f,
"Usage: yate [options] [commands ...]\n"
"   -h, --help     Display help message (this one) and exit\n"
"   -V, --version  Display program version and exit\n"
"   -v             Verbose logging (you can use more than once)\n"
"   -q             Quieter logging (you can use more than once)\n"
"%s"
"   -p filename    Write PID to file\n"
"   -l filename    Log to file\n"
"   -n configname  Use specified configuration name (%s)\n"
"   -e pathname    Path to shared files directory (" SHR_PATH ")\n"
"   -c pathname    Path to conf files directory (" CFG_PATH ")\n"
"   -u pathname    Path to user files directory (%s)\n"
"   -m pathname    Path to modules directory (" MOD_PATH ")\n"
"   -x dirpath     Absolute or relative path to extra modules directory (can be repeated)\n"
"   -w directory   Change working directory\n"
"   -N nodename    Set the name of this node in a cluster\n"
#ifdef RLIMIT_CORE
"   -C             Enable core dumps if possible\n"
#endif
#ifdef RLIMIT_NOFILE
"   -F             Increase the maximum file handle to compiled value\n"
#endif
"   -t             Truncate log file, don't append to it\n"
"   -D[options]    Special debugging options\n"
"     a            Abort if bugs are encountered\n"
"     m            Attempt to debug mutex deadlocks\n"
"     d            Enable locking debugging and safety features\n"
#ifdef RTLD_GLOBAL
"     l            Try to keep module symbols local\n"
#endif
"     c            Call dlclose() until it gets an error\n"
"     u            Do not unload modules on exit, just finalize\n"
"     i            Reinitialize after 1st initialization\n"
"     x            Exit immediately after initialization\n"
"     w            Delay creation of 1st worker thread\n"
"     o            Colorize output using ANSI codes\n"
"     s            Abort on bugs even during shutdown\n"
"     O            Attempt to debug object allocations\n"
"     n            Do not timestamp debugging messages\n"
"     t            Timestamp debugging messages relative to program start\n"
"     e            Timestamp debugging messages based on EPOCH (1-1-1970 GMT)\n"
"     f            Timestamp debugging in GMT format YYYYMMDDhhmmss.uuuuuu\n"
"     F            Timestamp debugging in GMT format YYYY-MM-DD_hh:mm:ss.uuuuuu\n"
"     z            Timestamp debugging in local timezone YYYYMMDDhhmmss.uuuuuu\n"
"     Z            Timestamp debugging in local timezone YYYY-MM-DD_hh:mm:ss.uuuuuu\n"
    ,client ? "" :
#ifdef _WINDOWS
"   --service      Run as Windows service\n"
"   --install      Install the Windows service\n"
"   --remove       Remove the Windows service\n"
#else
"   -d             Daemonify, suppress output unless logged\n"
"   -s[=msec]      Supervised, restart if crashes or locks up, optionally sleeps initially\n"
"   -r             Enable rotation of log file (needs -s and -l)\n"
#endif
    ,s_cfgfile.safe()
    ,s_usrpath.safe());
}

static void badopt(bool client, char chr, const char* opt)
{
    if (chr)
	::fprintf(stderr,"Invalid character '%c' in option '%s'\n",chr,opt);
    else
	::fprintf(stderr,"Invalid option '%s'\n",opt);
    usage(client,stderr);
}

static void noarg(bool client, const char* opt)
{
    ::fprintf(stderr,"Missing parameter to option '%s'\n",opt);
    usage(client,stderr);
}

static void version()
{
    ::fprintf(stdout,"Yate " YATE_VERSION " " YATE_STATUS YATE_RELEASE " r" YATE_REVISION "\n");
}

int Engine::main(int argc, const char** argv, const char** env, RunMode mode, EngineLoop loop, bool fail)
{
#ifdef _WINDOWS
    int service = 0;
#else
    bool daemonic = false;
    int supervised = 0;
#endif
    bool client = (mode == Client);
    Debugger::Formatting tstamp = Debugger::TextLSep;
    bool colorize = false;
    const char* pidfile = 0;
    const char* workdir = 0;
    const char* usrpath = 0;
    int debug_level = debugLevel();

    Lockable::startUsingNow();

    const char* cfgfile = ::strrchr(argv[0],'/');
    if (!cfgfile)
	cfgfile = ::strrchr(argv[0],'\\');
    if (cfgfile)
	cfgfile++;

    if (!cfgfile)
	cfgfile = argv[0][0] ? argv[0] : "yate";
    initCfgFile(cfgfile);

    int i;
    bool inopt = true;
    for (i=1;i<argc;i++) {
	const char *pc = argv[i];
	if (inopt && (pc[0] == '-') && pc[1]) {
	    // skip over Mac OS X process serial number
	    if (!::strncmp(pc,"-psn_",5))
		continue;
	    while (pc && *++pc) {
		const char* param = 0;

#define GET_PARAM \
    if ('=' == pc[1]) param = pc + 2; \
    else if (i+1 < argc) param = argv[++i]; \
    else { noarg(client,argv[i]); return ENOENT; } \
    pc = 0

		switch (*pc) {
		    case '-':
			if (!*++pc) {
			    inopt=false;
			    pc=0;
			    continue;
			}
			if (!::strcmp(pc,"help")) {
			    s_mode = mode;
			    initUsrPath(s_usrpath);
			    usage(client,stdout);
			    return 0;
			}
			else if (!::strcmp(pc,"version")) {
			    version();
			    return 0;
			}
#ifdef _WINDOWS
			else if (!(client ||::strcmp(pc,"service"))) {
			    service |= YSERV_RUN;
			    pc = 0;
			    continue;
			}
			else if (!(client || ::strcmp(pc,"install"))) {
			    service |= YSERV_INS;
			    pc = 0;
			    continue;
			}
			else if (!(client || ::strcmp(pc,"remove"))) {
			    service |= YSERV_DEL;
			    pc = 0;
			    continue;
			}
#endif
			initUsrPath(s_usrpath);
			badopt(client,0,argv[i]);
			return EINVAL;
			break;
		    case 'h':
			s_mode = mode;
			initUsrPath(s_usrpath);
			usage(client,stdout);
			return 0;
		    case 'v':
			debug_level++;
			break;
		    case 'q':
			debug_level--;
			break;
#ifndef _WINDOWS
		    case 'd':
			daemonic = true;
			break;
		    case 's':
			supervised = -1;
			if ('=' == pc[1]) {
			    long int ms = ::strtol(pc+2,0,0);
			    if (ms > INITDELAY_MAX)
				supervised = INITDELAY_MAX;
			    else if (ms > 0)
				supervised = ms;
			    pc = 0;
			}
			break;
		    case 'r':
			s_logrotator = true;
			break;
#endif
		    case 't':
			s_logtruncate = true;
			break;
		    case 'p':
			GET_PARAM;
			pidfile = param;
			break;
		    case 'l':
			GET_PARAM;
			s_logfile = param;
			break;
		    case 'n':
			GET_PARAM;
			cfgfile = param;
			break;
		    case 'e':
			GET_PARAM;
			s_shrpath = param;
			break;
		    case 'c':
			GET_PARAM;
			s_cfgpath = param;
			break;
		    case 'u':
			GET_PARAM;
			usrpath = param;
			break;
		    case 'm':
			GET_PARAM;
			s_modpath = param;
			break;
		    case 'w':
			GET_PARAM;
			workdir = param;
			break;
		    case 'x':
			GET_PARAM;
			extraPath(param);
			break;
		    case 'N':
			GET_PARAM;
			s_node = param;
			break;
#ifdef RLIMIT_CORE
		    case 'C':
			s_coredump = true;
			break;
#endif
#ifdef RLIMIT_NOFILE
		    case 'F':
			s_numfiles = true;
			break;
#endif
		    case 'D':
			while (*++pc) {
			    switch (*pc) {
				case 'a':
				    s_sigabrt = true;
				    break;
				case 's':
				    s_lateabrt = true;
				    break;
				case 'm':
				    setLockableWait();
				    break;
				case 'd':
				    Lockable::enableSafety();
				    break;
#ifdef RTLD_GLOBAL
				case 'l':
				    s_localsymbol = true;
				    break;
#endif
				case 'c':
				    s_keepclosing = true;
				    break;
				case 'u':
				    s_nounload = true;
				    break;
				case 'i':
				    s_init = true;
				    break;
				case 'x':
				    s_exit++;
				    break;
				case 'w':
				    s_makeworker = false;
				    break;
				case 'o':
				    colorize = true;
				    break;
				case 'O':
				    GenObject::setObjCounting(true);
				    break;
				case 'n':
				    tstamp = Debugger::None;
				    break;
				case 'e':
				    tstamp = Debugger::Absolute;
				    break;
				case 't':
				    tstamp = Debugger::Relative;
				    break;
				case 'f':
				    tstamp = Debugger::Textual;
				    break;
				case 'z':
				    tstamp = Debugger::TextLocal;
				    break;
				case 'F':
				    tstamp = Debugger::TextSep;
				    break;
				case 'Z':
				    tstamp = Debugger::TextLSep;
				    break;
				default:
				    initUsrPath(s_usrpath);
				    badopt(client,*pc,argv[i]);
				    return EINVAL;
			    }
			}
			pc = 0;
			break;
		    case 'V':
			version();
			return 0;
		    default:
			initUsrPath(s_usrpath);
			badopt(client,*pc,argv[i]);
			return EINVAL;
		}

#undef GET_PARAM

	    }
	}
	else {
	    if (!s_cmds)
		s_cmds = new ObjList;
	    s_cmds->append(new String(argv[i]));
	}
    }

    if (fail)
	return EINVAL;

    s_mode = mode;

    initCfgFile(cfgfile);
    initUsrPath(s_usrpath,usrpath);

    if (workdir && ::chdir(workdir)) {
	int err = errno;
	::fprintf(stderr,"Could not change working directory to '%s': %s (%d)\n",
	    workdir,::strerror(err),err);
	return err;
    }

    if (s_engineCheck && !s_engineCheck->check(s_cmds))
	return s_haltcode;

#ifdef _WINDOWS
    if ((mode == Server) && !service)
	service = YSERV_RUN;

    if (service & YSERV_DEL) {
	if (service & (YSERV_RUN|YSERV_INS)) {
	    ::fprintf(stderr,"Option --remove prohibits --install and --service\n");
	    return EINVAL;
	}
	SC_HANDLE sc = OpenSCManager(0,0,SC_MANAGER_ALL_ACCESS);
	if (!sc) {
	    ::fprintf(stderr,"Could not open Service Manager, code %u\n",::GetLastError());
	    return EPERM;
	}
	SC_HANDLE sv = OpenService(sc,"yate",DELETE|SERVICE_STOP);
	if (sv) {
	    ControlService(sv,SERVICE_CONTROL_STOP,0);
	    if (!DeleteService(sv)) {
		DWORD err = ::GetLastError();
		if (err != ERROR_SERVICE_MARKED_FOR_DELETE)
		    ::fprintf(stderr,"Could not delete Service, code %u\n",err);
	    }
	    CloseServiceHandle(sv);
	}
	else {
	    DWORD err = ::GetLastError();
	    if (err != ERROR_SERVICE_DOES_NOT_EXIST)
		::fprintf(stderr,"Could not open Service, code %u\n",err);
	}
	CloseServiceHandle(sc);
	return 0;
    }
    if (service & YSERV_INS) {
	char buf[1024];
	if (!GetModuleFileName(0,buf,sizeof(buf))) {
	    ::fprintf(stderr,"Could not find my own executable file, code %u\n",::GetLastError());
	    return EINVAL;
	}
	String s(buf);
	if (mode != Server)
	    s << " --service";
	if (workdir)
	    s << " -w \"" << workdir << "\"";

	SC_HANDLE sc = OpenSCManager(0,0,SC_MANAGER_ALL_ACCESS);
	if (!sc) {
	    ::fprintf(stderr,"Could not open Service Manager, code %u\n",::GetLastError());
	    return EPERM;
	}
	SC_HANDLE sv = CreateService(sc,"yate","Yet Another Telephony Engine",GENERIC_EXECUTE,
	    SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,
	    s.c_str(),0,0,0,0,0);
	if (sv)
	    CloseServiceHandle(sv);
	else
	    ::fprintf(stderr,"Could not create Service, code %u\n",::GetLastError());
	CloseServiceHandle(sc);
	if (!(service & YSERV_RUN))
	    return 0;
    }
#else
    if (client && (daemonic || supervised)) {
	::fprintf(stderr,"Options -d and -s not supported in client mode\n");
	return EINVAL;
    }
    if (colorize && s_logfile) {
	::fprintf(stderr,"Option -Do not supported when logging to file\n");
	return EINVAL;
    }
    if (s_logrotator && !(supervised && s_logfile)) {
	::fprintf(stderr,"Option -r needs supervisor and logging to file\n");
	return EINVAL;
    }
    Debugger::enableOutput(true,colorize);
    if (daemonic) {
	Debugger::enableOutput(false);
	// Make sure X client modules fail initialization in daemon mode
	::unsetenv("DISPLAY");
	if (::daemon(1,0) == -1) {
	    int err = errno;
	    ::fprintf(stderr,"Daemonification failed: %s (%d)\n",::strerror(err),err);
	    return err;
	}
	s_interactive = false;
    }
#endif

    if (pidfile) {
	int fd = ::open(pidfile,O_WRONLY|O_CREAT,0644);
	if (fd >= 0) {
	    char pid[32];
	    ::snprintf(pid,sizeof(pid),"%u\n",::getpid());
	    YIGNORE(::write(fd,pid,::strlen(pid)));
	    ::close(fd);
	}
    }

#ifdef _WINDOWS
    if (!service)
#endif
	logFileOpen();

    debugLevel(debug_level);
    abortOnBug(s_sigabrt);

#ifdef RLIMIT_CORE
    while (s_coredump) {
#ifdef HAVE_PRCTL
#ifdef PR_SET_DUMPABLE
	prctl(PR_SET_DUMPABLE,1,0,0,0);
#endif
#endif
	struct rlimit lim;
	if (!::getrlimit(RLIMIT_CORE,&lim)) {
	    errno = 0;
	    lim.rlim_cur = lim.rlim_max;
	    // if limit is zero but user is root set limit to infinity
	    if (!(lim.rlim_cur || ::getuid()))
		lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	    if (lim.rlim_cur && !::setrlimit(RLIMIT_CORE,&lim))
		break;
	}
	Debug(DebugWarn,"Could not enable core dumps: %s (%d)",
	    errno ? strerror(errno) : "hard limit",errno);
	break;
    }
#endif
#ifdef RLIMIT_NOFILE
    while (s_numfiles) {
	struct rlimit lim;
	if (!::getrlimit(RLIMIT_NOFILE,&lim)) {
	    errno = 0;
	    if (lim.rlim_cur >= FDSIZE_HACK)
		break;
	    lim.rlim_cur = FDSIZE_HACK;
	    if (lim.rlim_max < FDSIZE_HACK)
		lim.rlim_max = FDSIZE_HACK;
	    if (!::setrlimit(RLIMIT_NOFILE,&lim))
		break;
	}
	Debug(DebugWarn,"Could not increase max file handle: %s (%d)",
	    errno ? strerror(errno) : "hard limit",errno);
	break;
    }
#endif

    int retcode = -1;
#ifndef _WINDOWS
    if (supervised)
	retcode = supervise(supervised);
    if (retcode >= 0)
	return retcode;
#endif

    Debugger::setFormatting(tstamp);

#ifdef _WINDOWS
    if (service) {
	s_interactive = false;
	retcode = ::StartServiceCtrlDispatcher(dispatchTable) ? 0 : ::GetLastError();
    }
    else
#endif
	retcode = engineRun(loop);

    return retcode;
}

void Engine::help(bool client, bool errout)
{
    initUsrPath(s_usrpath);
    usage(client, errout ? stderr : stdout);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
