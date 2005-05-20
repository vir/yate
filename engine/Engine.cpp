/**
 * Engine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yatengine.h"
#include "yateversn.h"
#ifdef _WINDOWS
#include <process.h>
#define RTLD_NOW 0
#define dlopen(name,flags) LoadLibrary(name)
#define dlclose !FreeLibrary
#define dlerror() "LoadLibrary error"
#define YSERV_RUN 1
#define YSERV_INS 2
#define YSERV_DEL 4
#else
#include "yatepaths.h"
#include <dirent.h>
#include <dlfcn.h>
#include <sys/wait.h>
typedef void* HMODULE;
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

#include <assert.h>

namespace TelEngine {

class EnginePrivate : public Thread
{
public:
    EnginePrivate()
	: Thread("EnginePrivate")
	{ count++; }
    ~EnginePrivate()
	{ count--; }
    virtual void run();
    static int count;
};

};

using namespace TelEngine;

#ifndef MOD_PATH
#define MOD_PATH "./modules"
#endif
#ifndef CFG_PATH
#define CFG_PATH "./conf.d"
#endif
#define DLL_SUFFIX ".yate"
#define CFG_SUFFIX ".conf"

#define MAX_SANITY 5
#define INIT_SANITY 10

static u_int64_t s_nextinit = 0;
static u_int64_t s_restarts = 0;
static bool s_makeworker = true;
static bool s_keepclosing = false;
static int s_super_handle = -1;

static void sighandler(int signal)
{
    switch (signal) {
#ifndef _WINDOWS
	case SIGHUP:
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

String Engine::s_cfgpath(CFG_PATH);
String Engine::s_cfgsuffix(CFG_SUFFIX);
String Engine::s_modpath(MOD_PATH);
String Engine::s_modsuffix(DLL_SUFFIX);

Engine* Engine::s_self = 0;
int Engine::s_haltcode = -1;
int EnginePrivate::count = 0;
bool s_init = false;
bool s_dynplugin = false;
int s_maxworkers = 10;

const char* s_cfgfile = 0;
Configuration s_cfg;
ObjList plugins;
ObjList* s_cmds = 0;

class SLib : public GenObject
{
public:
    virtual ~SLib();
    static SLib* load(const char* file);
private:
    SLib(HMODULE handle, const char* file);
    const char* m_file;
    HMODULE m_handle;
};

SLib::SLib(HMODULE handle, const char* file)
    : m_handle(handle)
{
    DDebug(DebugAll,"SLib::SLib(%p,'%s') [%p]",handle,file,this);
}

SLib::~SLib()
{
#ifdef DEBUG
    Debugger debug("SLib::~SLib()"," [%p]",this);
#endif
    int err = dlclose(m_handle);
    if (err)
	Debug(DebugGoOn,"Error %d on dlclose(%p)",err,m_handle);
    else if (s_keepclosing) {
	int tries;
	for (tries=0; tries<10; tries++)
	    if (dlclose(m_handle))
		break;
	if (tries)
	    Debug(DebugGoOn,"Made %d attempts to dlclose(%p)",tries,m_handle);
    }
}

SLib* SLib::load(const char* file)
{
    DDebug(DebugAll,"SLib::load('%s')",file);
    HMODULE handle = ::dlopen(file,RTLD_NOW);
    if (handle)
	return new SLib(handle,file);
#ifdef _WINDOWS
    Debug(DebugWarn,"LoadLibrary error %u in '%s'",::GetLastError(),file);
#else
    Debug(DebugWarn,dlerror());
#endif    
    return 0;
}

class EngineSuperHandler : public MessageHandler
{
public:
    EngineSuperHandler() : MessageHandler("engine.timer",0), m_seq(0) { }
    virtual bool received(Message &msg)
	{ ::write(s_super_handle,&m_seq,1); m_seq++; return false; }
    char m_seq;
};

class EngineStatusHandler : public MessageHandler
{
public:
    EngineStatusHandler() : MessageHandler("engine.status",0) { }
    virtual bool received(Message &msg);
};

bool EngineStatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"engine"))
	return false;
    msg.retValue() << "name=engine,type=system";
    msg.retValue() << ",version=" << YATE_MAJOR << "." << YATE_MINOR << "." << YATE_BUILD;
    msg.retValue() << ";plugins=" << plugins.count();
    msg.retValue() << ",inuse=" << Engine::self()->usedPlugins();
    msg.retValue() << ",supervised=" << (s_super_handle >= 0);
    msg.retValue() << ",threads=" << Thread::count();
    msg.retValue() << ",workers=" << EnginePrivate::count;
    msg.retValue() << ",mutexes=" << Mutex::count();
    msg.retValue() << ",locks=" << Mutex::locks();
    msg.retValue() << "\n";
    return false;
}

void EnginePrivate::run()
{
    for (;;) {
	s_makeworker = false;
	Engine::self()->m_dispatcher.dequeue();
	msleep(5,true);
    }
}

static int engineRun()
{
    time_t t = ::time(0);
    Output("Yate (%u) is starting %s",::getpid(),::ctime(&t));
    int retcode = Engine::self()->run();
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
    s_status.dwCurrentState = state;
    SetServiceStatus(s_handler,&s_status);
}

static void WINAPI serviceHandler(DWORD code)
{
    switch (code) {
	case SERVICE_CONTROL_STOP:
	    Engine::halt(0);
	    break;
	case SERVICE_CONTROL_PARAMCHANGE:
	    Engine::init();
	    break;
	default:
	    Debug(DebugWarn,"Got unexpected service control code %u",code);
    }
}

static void serviceMain(DWORD argc, LPTSTR* argv)
{
    s_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s_status.dwCurrentState = SERVICE_START_PENDING;
    s_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PARAMCHANGE;
    s_status.dwWin32ExitCode = NO_ERROR;
    s_status.dwServiceSpecificExitCode = 0;
    s_status.dwCheckPoint = 0;
    s_status.dwWaitHint = 0;
    s_handler = RegisterServiceCtrlHandler("yate",serviceHandler);
    if (!s_handler) {
	Debug(DebugFail,"Could not register service control handler \"yate\", code %u\n",GetLastError());
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

static bool s_runagain = true;
static pid_t s_childpid = -1;

static void superhandler(int signal)
{
    switch (signal) {
	case SIGINT:
	case SIGTERM:
	case SIGABRT:
	    s_runagain = false;
    }
    if (s_childpid > 0)
	::kill(s_childpid,signal);
}

static int supervise(void)
{
    ::fprintf(stderr,"Supervisor (%u) is starting\n",::getpid());
    ::signal(SIGINT,superhandler);
    ::signal(SIGTERM,superhandler);
    ::signal(SIGHUP,superhandler);
    ::signal(SIGQUIT,superhandler);
    ::signal(SIGABRT,superhandler);
    int retcode = 0;
    while (s_runagain) {
	int wdogfd[2];
	if (::pipe(wdogfd)) {
	    int err = errno;
	    ::fprintf(stderr,"Supervisor: pipe failed: %s (%d)\n",::strerror(err),err);
	    return err;
	}
	::fcntl(wdogfd[0],F_SETFL,O_NONBLOCK);
	::fcntl(wdogfd[1],F_SETFL,O_NONBLOCK);
	s_childpid = ::fork();
	if (s_childpid < 0) {
	    int err = errno;
	    ::fprintf(stderr,"Supervisor: fork failed: %s (%d)\n",::strerror(err),err);
	    return err;
	}
	if (s_childpid == 0) {
	    s_super_handle = wdogfd[1];
	    ::close(wdogfd[0]);
	    ::signal(SIGINT,SIG_DFL);
	    ::signal(SIGTERM,SIG_DFL);
	    ::signal(SIGHUP,SIG_DFL);
	    ::signal(SIGQUIT,SIG_DFL);
	    ::signal(SIGABRT,SIG_DFL);
	    return -1;
	}
	::close(wdogfd[1]);
	// Wait for the child to die or block
	for (int sanity = INIT_SANITY; sanity > 0; sanity--) {
	    int status = -1;
	    int tmp = ::waitpid(s_childpid,&status,WNOHANG);
	    if (tmp > 0) {
		// Child exited for some reason
		if (WIFEXITED(status)) {
		    retcode = WEXITSTATUS(status);
		    if (retcode <= 127)
			s_runagain = false;
		    else
			retcode &= 127;
		}
		else if (WIFSIGNALED(status)) {
		    retcode = WTERMSIG(status);
		    ::fprintf(stderr,"Supervisor: child %d died on signal %d\n",s_childpid,retcode);
		}
		s_childpid = -1;
		break;
	    }

	    char buf[MAX_SANITY];
	    tmp = ::read(wdogfd[0],buf,sizeof(buf));
	    if (tmp >= 0) {
		// Timer messages add one sanity point every second
		sanity += tmp;
		if (sanity > MAX_SANITY)
		    sanity = MAX_SANITY;
	    }
	    else if ((errno != EINTR) && (errno != EAGAIN))
		break;
	    // Consume sanity points slightly slower than added
	    ::usleep(1200000);
	}
	::close(wdogfd[0]);
	if (s_childpid > 0) {
	    // Child failed to proof sanity. Kill it - no need to be gentle.
	    ::fprintf(stderr,"Supervisor: killing unresponsive child %d\n",s_childpid);
	    // If -Da was specified try to get a corefile
	    if (s_sigabrt) {
		::kill(s_childpid,SIGABRT);
		::usleep(250000);
	    }
	    ::kill(s_childpid,SIGKILL);
	    ::usleep(10000);
	    ::waitpid(s_childpid,0,WNOHANG);
	    s_childpid = -1;
	}
	if (s_runagain)
	    ::usleep(1000000);
    }
    ::fprintf(stderr,"Supervisor (%d) exiting with code %d\n",::getpid(),retcode);
    return retcode;
}
#endif /* _WINDOWS */

Engine::Engine()
{
    DDebug(DebugAll,"Engine::Engine() [%p]",this);
}

Engine::~Engine()
{
#ifdef DEBUG
    Debugger debug("Engine::~Engine()"," [%p]",this);
#endif
    assert(this == s_self);
    m_dispatcher.clear();
    plugins.clear();
    m_libs.clear();
    s_self = 0;
}

int Engine::run()
{
#ifdef _WINDOWS
    // In Windows we must initialize the socket library very early because even trivial
    //  functions like endianess swapping - ntohl and family - need it to be initialized
    WSADATA wsaData;
    int errc = ::WSAStartup(MAKEWORD(2,2), &wsaData);
    if (errc) {
	Debug(DebugFail,"Failed to initialize the Windows Sockets library, error code %d",errc);
	return errc & 127;
    }
#endif
    s_cfg = configFile(s_cfgfile);
    s_cfg.load();
    Debug(DebugAll,"Engine::run()");
    install(new EngineStatusHandler);
    loadPlugins();
    Debug(DebugInfo,"plugins.count() = %d",plugins.count());
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
    ::signal(SIGINT,sighandler);
    ::signal(SIGTERM,sighandler);
    Debug(DebugInfo,"Engine dispatching start message");
    dispatch("engine.start");
    setStatus(SERVICE_RUNNING);
    unsigned long corr = 0;
#ifndef _WINDOWS
    ::signal(SIGHUP,sighandler);
    ::signal(SIGQUIT,sighandler);
    ::signal(SIGPIPE,SIG_IGN);
#endif
    Output("Yate engine is initialized and starting up");
    while (s_haltcode == -1) {
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
	    s_cmds->destruct();
	    s_cmds = 0;
	}

	if (s_init) {
	    s_init = false;
	    initPlugins();
	}

	// Create worker thread if we didn't hear about any of them in a while
	if (s_makeworker && (EnginePrivate::count < s_maxworkers)) {
	    Debug(DebugInfo,"Creating new message dispatching thread (%d running)",EnginePrivate::count);
	    EnginePrivate *prv = new EnginePrivate;
	    prv->startup();
	}
	else
	    s_makeworker = true;

	if (s_restarts && (Time::now() >= s_restarts)) {
	    if (!(usedPlugins() || dispatch("engine.busy"))) {
		s_haltcode = 128;
		break;
	    }
	    // If we cannot restart now try again in 10s
	    s_restarts = Time::now() + 10000000;
	}

	// Attempt to sleep until the next full second
	unsigned long t = (unsigned long)((Time::now() + corr) % 1000000);
	Thread::usleep(1000000 - t);
	Message *m = new Message("engine.timer");
	m->addParam("time",String((int)m->msgTime().sec()));
	// Try to fine tune the ticker
	t = (unsigned long)(m->msgTime().usec() % 1000000);
	if (t > 500000)
	    corr -= (1000000-t)/10;
	else
	    corr += t/10;
	enqueue(m);
	Thread::yield();
    }
    s_haltcode &= 0xff;
    Output("Yate engine is shutting down with code %d",s_haltcode);
    setStatus(SERVICE_STOP_PENDING);
    dispatch("engine.halt");
    Thread::msleep(200);
    m_dispatcher.dequeue();
    Thread::killall();
    m_dispatcher.dequeue();
    ::signal(SIGINT,SIG_DFL);
    ::signal(SIGTERM,SIG_DFL);
#ifndef _WINDOWS
    ::signal(SIGHUP,SIG_DFL);
    ::signal(SIGQUIT,SIG_DFL);
#endif
    delete this;
    Debug(DebugInfo,"Exiting with %d locked mutexes",Mutex::locks());
#ifdef _WINDOWS
    ::WSACleanup();
#endif
    setStatus(SERVICE_STOPPED);
    return s_haltcode;
}

Engine* Engine::self()
{
    if (!s_self)
	s_self = new Engine;
    return s_self;
}

String Engine::configFile(const char* name)
{
    return s_cfgpath+"/"+name+s_cfgsuffix;
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
	p = plugins.append(plugin);
	p->setDelete(s_dynplugin);
    }
    else if (p)
	p->remove(false);
    return true;
}

bool Engine::loadPlugin(const char* file)
{
    s_dynplugin = false;
    SLib *lib = SLib::load(file);
    s_dynplugin = true;
    if (lib) {
	m_libs.append(lib);
	return true;
    }
    return false;
}

void Engine::loadPlugins()
{
#ifdef DEBUG
    Debugger debug("Engine::loadPlugins()");
#endif
    bool defload = s_cfg.getBoolValue("general","modload",true);
    const char *name = s_cfg.getValue("general","modpath");
    if (name)
	s_modpath = name;
    s_maxworkers = s_cfg.getIntValue("general","maxworkers",s_maxworkers);
    s_restarts = s_cfg.getIntValue("general","restarts");
    NamedList *l = s_cfg.getSection("preload");
    if (l) {
        unsigned int len = l->length();
        for (unsigned int i=0; i<len; i++) {
            NamedString *n = l->getParam(i);
            if (n && n->toBoolean())
                loadPlugin(n->name());
	}
    }
#ifdef _WINDOWS
    WIN32_FIND_DATA entry;
    HANDLE hf = ::FindFirstFile(s_modpath+"\\*",&entry);
    if (hf == INVALID_HANDLE_VALUE) {
	Debug(DebugFail,"Engine::loadPlugins() failed directory '%s'",s_modpath.safe());
	return;
    }
    do {
	XDebug(DebugInfo,"Found dir entry %s",entry.cFileName);
	int n = ::strlen(entry.cFileName) - s_modsuffix.length();
	if ((n > 0) && !::strcmp(entry.cFileName+n,s_modsuffix)) {
	    if (s_cfg.getBoolValue("modules",entry.cFileName,defload))
		loadPlugin(s_modpath+"\\"+entry.cFileName);
	}
    } while (::FindNextFile(hf,&entry));
    ::FindClose(hf);
#else
    DIR *dir = ::opendir(s_modpath);
    if (!dir) {
	Debug(DebugFail,"Engine::loadPlugins() failed directory '%s'",s_modpath.safe());
	return;
    }
    struct dirent *entry;
    while ((entry = ::readdir(dir)) != 0) {
	XDebug(DebugInfo,"Found dir entry %s",entry->d_name);
	int n = ::strlen(entry->d_name) - s_modsuffix.length();
	if ((n > 0) && !::strcmp(entry->d_name+n,s_modsuffix)) {
	    if (s_cfg.getBoolValue("modules",entry->d_name,defload))
		loadPlugin(s_modpath+"/"+entry->d_name);
	}
    }
    ::closedir(dir);
#endif
    l = s_cfg.getSection("postload");
    if (l) {
        unsigned int len = l->length();
        for (unsigned int i=0; i<len; i++) {
            NamedString *n = l->getParam(i);
            if (n && n->toBoolean())
                loadPlugin(n->name());
	}
    }
}

void Engine::initPlugins()
{
#ifdef DEBUG
    Debugger debug("Engine::initPlugins()");
#else
    Debug(DebugInfo,"Engine::initPlugins()");
#endif
    dispatch("engine.init");
    ObjList *l = plugins.skipNull();
    for (; l; l = l->skipNext()) {
	Plugin *p = static_cast<Plugin *>(l->get());
	p->initialize();
    }
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

void Engine::halt(unsigned int code)
{
    s_haltcode = code;
}

void Engine::init()
{
    s_init = true;
}

bool Engine::install(MessageHandler* handler)
{
    return s_self ? s_self->m_dispatcher.install(handler) : false;
}

bool Engine::uninstall(MessageHandler* handler)
{
    return s_self ? s_self->m_dispatcher.uninstall(handler) : false;
}

bool Engine::enqueue(Message* msg)
{
    return (msg && s_self) ? s_self->m_dispatcher.enqueue(msg) : false;
}

bool Engine::dispatch(Message* msg)
{
    return (msg && s_self) ? s_self->m_dispatcher.dispatch(*msg) : false;
}

bool Engine::dispatch(Message& msg)
{
    return s_self ? s_self->m_dispatcher.dispatch(msg) : false;
}

bool Engine::dispatch(const char* name)
{
    if (!(s_self && name && *name))
	return false;
    Message msg(name);
    return s_self->m_dispatcher.dispatch(msg);
}

static bool s_sigabrt = false;

static void usage(bool client, FILE* f)
{
    ::fprintf(f,
"Usage: yate [options] [commands ...]\n"
"   -h             Help message (this one)\n"
"   -v             Verbose debugging (you can use more than once)\n"
"   -q             Quieter debugging (you can use more than once)\n"
"%s"
"   -p filename    Write PID to file\n"
"   -l filename    Log to file\n"
"   -n configname  Use specified configuration name (%s)\n"
"   -c pathname    Path to conf files directory (" CFG_PATH ")\n"
"   -m pathname    Path to modules directory (" MOD_PATH ")\n"
#ifndef NDEBUG
"   -D[options]    Special debugging options\n"
"     a            Abort if bugs are encountered\n"
"     c            Call dlclose() until it gets an error\n"
"     i            Reinitialize after 1st initialization\n"
"     x            Exit immediately after initialization\n"
"     w            Delay creation of 1st worker thread\n"
"     t            Timestamp debugging messages\n"
#endif
    ,client ? "" :
#ifdef _WINDOWS
"   --service      Run as Windows service\n"
"   --install      Install the Windows service\n"
"   --remove       Remove the Windows service\n"
#else
"   -d             Daemonify, suppress output unless logged\n"
"   -s             Supervised, restart if crashes or locks up\n"
#endif
    ,s_cfgfile);
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

int Engine::main(int argc, const char** argv, const char** env, RunMode mode, bool fail)
{
#ifdef _WINDOWS
    int service = 0;
#else
    bool daemonic = false;
    bool supervised = false;
#endif
    bool client = (mode == Client);
    bool tstamp = false;
    const char *pidfile = 0;
    const char *logfile = 0;
    int debug_level = debugLevel();

    s_cfgfile = ::strrchr(argv[0],'/');
    if (!s_cfgfile)
	s_cfgfile = ::strrchr(argv[0],'\\');
    if (s_cfgfile)
	s_cfgfile++;

    if (!s_cfgfile)
	s_cfgfile = argv[0][0] ? argv[0] : "yate";

    int i;
    bool inopt = true;
    for (i=1;i<argc;i++) {
	const char *pc = argv[i];
	if (inopt && (pc[0] == '-') && pc[1]) {
	    while (pc && *++pc) {
		switch (*pc) {
		    case '-':
			if (!*++pc) {
			    inopt=false;
			    pc=0;
			    continue;
			}
			if (!::strcmp(pc,"help")) {
			    usage(client,stdout);
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
			badopt(client,0,argv[i]);
			return EINVAL;
			break;
		    case 'h':
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
			supervised = true;
			break;
#endif
		    case 'p':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			pidfile=argv[++i];
			break;
		    case 'l':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			logfile=argv[++i];
			break;
		    case 'n':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_cfgfile=argv[++i];
			break;
		    case 'c':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_cfgpath=argv[++i];
			break;
		    case 'm':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_modpath=argv[++i];
			break;
#ifndef NDEBUG
		    case 'D':
			while (*++pc) {
			    switch (*pc) {
				case 'a':
				    s_sigabrt = true;
				    break;
				case 'c':
				    s_keepclosing = true;
				    break;
				case 'i':
				    s_init = true;
				    break;
				case 'x':
				    s_haltcode++;
				    break;
				case 'w':
				    s_makeworker = false;
				    break;
				case 't':
				    tstamp = true;
				    break;
				default:
				    badopt(client,*pc,argv[i]);
				    return EINVAL;
			    }
			}
			pc = 0;
			break;
#endif
		    default:
			badopt(client,*pc,argv[i]);
			return EINVAL;
		}
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
	    ::fprintf(stderr,"Could not open Service Manager, code %u\n",GetLastError());
	    return EPERM;
	}
	SC_HANDLE sv = OpenService(sc,"yate",DELETE|SERVICE_STOP);
	if (sv) {
	    ControlService(sv,SERVICE_CONTROL_STOP,0);
	    if (!DeleteService(sv)) {
		DWORD err = GetLastError();
		if (err != ERROR_SERVICE_MARKED_FOR_DELETE)
		    ::fprintf(stderr,"Could not delete Service, code %u\n",err);
	    }
	    CloseServiceHandle(sv);
	}
	else {
	    DWORD err = GetLastError();
	    if (err != ERROR_SERVICE_DOES_NOT_EXIST)
		::fprintf(stderr,"Could not open Service, code %u\n",err);
	}
	CloseServiceHandle(sc);
	return 0;
    }
    if (service & YSERV_INS) {
	char buf[1024];
	if (!GetModuleFileName(0,buf,sizeof(buf))) {
	    ::fprintf(stderr,"Could not find my own executable file, code %u\n",GetLastError());
	    return EINVAL;
	}
	if (mode != Server)
	    ::strncat(buf," --service",sizeof(buf));
	SC_HANDLE sc = OpenSCManager(0,0,SC_MANAGER_ALL_ACCESS);
	if (!sc) {
	    ::fprintf(stderr,"Could not open Service Manager, code %u\n",GetLastError());
	    return EPERM;
	}
	SC_HANDLE sv = CreateService(sc,"yate","Yet Another Telephony Engine",GENERIC_EXECUTE,
	    SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,
	    buf,0,0,0,0,0);
	if (sv)
	    CloseServiceHandle(sv);
	else
	    ::fprintf(stderr,"Could not create Service, code %u\n",GetLastError());
	CloseServiceHandle(sc);
	if (!(service & YSERV_RUN))
	    return 0;
    }
#else
    if (client && (daemonic || supervised)) {
	::fprintf(stderr,"Options -d and -s not supported in client mode\n");
	return EINVAL;
    }
    if (daemonic) {
	Debugger::enableOutput(false);
	// Make sure X client modules fail initialization in daemon mode
	::unsetenv("DISPLAY");
	if (::daemon(1,0) == -1) {
	    int err = errno;
	    ::fprintf(stderr,"Daemonification failed: %s (%d)\n",::strerror(err),err);
	    return err;
	}
    }
#endif

    if (pidfile) {
	int fd = ::open(pidfile,O_WRONLY|O_CREAT,0644);
	if (fd >= 0) {
	    char pid[32];
	    ::snprintf(pid,sizeof(pid),"%u\n",::getpid());
	    ::write(fd,pid,::strlen(pid));
	    ::close(fd);
	}
    }

    if (logfile) {
	int fd = ::open(logfile,O_WRONLY|O_CREAT|O_APPEND,0640);
	if (fd >= 0) {
	    // Redirect stdout and stderr to the new file
	    ::fflush(stdout);
	    ::dup2(fd,1);
	    ::fflush(stderr);
	    ::dup2(fd,2);
	    ::close(fd);
	    Debugger::enableOutput(true);
	}
    }

    debugLevel(debug_level);
    abortOnBug(s_sigabrt);

    int retcode = -1;
#ifndef _WINDOWS
    if (supervised)
	retcode = supervise();
    if (retcode >= 0)
	return retcode;
#endif

    if (tstamp)
	setDebugTimestamp();

#ifdef _WINDOWS
    if (service)
	retcode = StartServiceCtrlDispatcher(dispatchTable) ? 0 : GetLastError();
    else
#endif
	retcode = engineRun();

    return retcode;
}

void Engine::help(bool client, bool errout)
{
    usage(client, errout ? stderr : stdout);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
