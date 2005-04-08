/**
 * Engine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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
#include <windows.h>
#include <io.h>
#include <process.h>
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_APPEND _O_APPEND
#define O_CREAT _O_CREAT
#define open _open
#define dup2 _dup2
#define read _read
#define write _write
#define close _close
#define getpid _getpid
#define RTLD_NOW 0
#define dlopen(name,flags) LoadLibrary(name)
#define dlclose !FreeLibrary
#define dlerror() "LoadLibrary error"
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
bool Engine::s_init = false;
bool Engine::s_dynplugin = false;
int Engine::s_maxworkers = 10;
int EnginePrivate::count = 0;

const char* s_cfgfile = 0;
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
	yield();
    }
}

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
	    Debug(DebugInfo,"Creating new message dispatching thread");
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
    dispatch("engine.halt");
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
    Configuration cfg(configFile(s_cfgfile));
    bool defload = cfg.getBoolValue("general","modload",true);
    const char *name = cfg.getValue("general","modpath");
    if (name)
	s_modpath = name;
    s_maxworkers = cfg.getIntValue("general","maxworkers",s_maxworkers);
    s_restarts = cfg.getIntValue("general","restarts");
    NamedList *l = cfg.getSection("preload");
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
	    if (cfg.getBoolValue("modules",entry.cFileName,defload))
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
	    if (cfg.getBoolValue("modules",entry->d_name,defload))
		loadPlugin(s_modpath+"/"+entry->d_name);
	}
    }
    ::closedir(dir);
#endif
    l = cfg.getSection("postload");
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

#ifndef _WINDOWS
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
	for (int sanity = MAX_SANITY; sanity > 0; sanity--) {
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

static void usage(FILE* f)
{
    ::fprintf(f,
"Usage: yate [options] [commands ...]\n"
"   -h             Help message (this one)\n"
"   -v             Verbose debugging (you can use more than once)\n"
"   -q             Quieter debugging (you can use more than once)\n"
#ifndef _WINDOWS
"   -d             Daemonify, suppress output unless logged\n"
"   -s             Supervised, restart if crashes or locks up\n"
#endif
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
#endif
    ,s_cfgfile);
}

static void badopt(char chr, const char* opt)
{
    if (chr)
	::fprintf(stderr,"Invalid character '%c' in option '%s'\n",chr,opt);
    else
	::fprintf(stderr,"Invalid option '%s'\n",opt);
    usage(stderr);
}

static void noarg(const char* opt)
{
    ::fprintf(stderr,"Missing parameter to option '%s'\n",opt);
    usage(stderr);
}

int Engine::main(int argc, const char** argv, const char** environ)
{
#ifndef _WINDOWS
    bool daemonic = false;
    bool supervised = false;
#endif
    const char *pidfile = 0;
    const char *logfile = 0;
    int debug_level = debugLevel();

    s_cfgfile = ::strrchr(argv[0],'/');
    if (s_cfgfile)
	s_cfgfile++;
    else
	s_cfgfile = "yate";

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
			    usage(stdout);
			    return 0;
			}
			badopt(0,argv[i]);
			return EINVAL;
			break;
		    case 'h':
			usage(stdout);
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
			    noarg(argv[i]);
			    return ENOENT;
			}
			pc = 0;
			pidfile=argv[++i];
			break;
		    case 'l':
			if (i+1 >= argc) {
			    noarg(argv[i]);
			    return ENOENT;
			}
			pc = 0;
			logfile=argv[++i];
			break;
		    case 'n':
			if (i+1 >= argc) {
			    noarg(argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_cfgfile=argv[++i];
			break;
		    case 'c':
			if (i+1 >= argc) {
			    noarg(argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_cfgpath=argv[++i];
			break;
		    case 'm':
			if (i+1 >= argc) {
			    noarg(argv[i]);
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
				default:
				    badopt(*pc,argv[i]);
				    return EINVAL;
			    }
			}
			pc = 0;
			break;
#endif
		    default:
			badopt(*pc,argv[i]);
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

#ifndef _WINDOWS
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

    time_t t = ::time(0);
    Output("Yate (%u) is starting %s",::getpid(),::ctime(&t));
    retcode = self()->run();
    t = ::time(0);
    Output("Yate (%u) is stopping %s",::getpid(),::ctime(&t));
    return retcode;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
