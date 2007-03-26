/**
 * Engine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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

#include "yatengine.h"
#include "yateversn.h"

#ifdef _WINDOWS

#include <process.h>
#include <shlobj.h>
#define RTLD_NOW 0
#define dlopen(name,flags) LoadLibrary(name)
#define dlclose !FreeLibrary
#define dlerror() "LoadLibrary error"
#define YSERV_RUN 1
#define YSERV_INS 2
#define YSERV_DEL 4
#define PATH_SEP "\\"
#define CFG_DIR "Yate"

#ifndef SHGetSpecialFolderPath
__declspec(dllimport) BOOL WINAPI SHGetSpecialFolderPathA(HWND,LPSTR,INT,BOOL);
#endif

#else

#include "yatepaths.h"
#include <dirent.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <sys/resource.h>
typedef void* HMODULE;
#define PATH_SEP "/"
#define CFG_DIR ".yate"

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
#define MOD_PATH "." PATH_SEP "modules"
#endif
#ifndef CFG_PATH
#define CFG_PATH "." PATH_SEP "conf.d"
#endif
#define DLL_SUFFIX ".yate"
#define CFG_SUFFIX ".conf"

#define MAX_SANITY 5
#define INIT_SANITY 10
#define MAX_LOGBUFF 4096

static u_int64_t s_nextinit = 0;
static u_int64_t s_restarts = 0;
static bool s_makeworker = true;
static bool s_keepclosing = false;
static bool s_nounload = false;
static int s_super_handle = -1;
static bool s_localsymbol = false;

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
String Engine::s_extramod;
String Engine::s_modsuffix(DLL_SUFFIX);

Engine::RunMode Engine::s_mode = Engine::Stopped;
Engine* Engine::s_self = 0;
int Engine::s_haltcode = -1;
int EnginePrivate::count = 0;
static bool s_init = false;
static bool s_dynplugin = false;
static int s_maxworkers = 10;
static bool s_debug = true;

static bool s_coredump = false;
static bool s_sigabrt = false;
static bool s_lateabrt = false;
static String s_cfgfile;
static const char* s_logfile = 0;
static Configuration s_cfg;
static ObjList plugins;
static ObjList* s_cmds = 0;
static unsigned int s_runid = 0;

class SLib : public GenObject
{
public:
    virtual ~SLib();
    static SLib* load(const char* file, bool local);
private:
    SLib(HMODULE handle, const char* file);
    const char* m_file;
    HMODULE m_handle;
};

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
    msg.retValue() << ",version=" << YATE_VERSION;
    msg.retValue() << ";plugins=" << plugins.count();
    msg.retValue() << ",inuse=" << Engine::self()->usedPlugins();
    msg.retValue() << ",handlers=" << Engine::self()->handlerCount();
    msg.retValue() << ",messages=" << Engine::self()->messageCount();
    msg.retValue() << ",supervised=" << (s_super_handle >= 0);
    msg.retValue() << ",threads=" << Thread::count();
    msg.retValue() << ",workers=" << EnginePrivate::count;
    msg.retValue() << ",mutexes=" << Mutex::count();
    msg.retValue() << ",locks=" << Mutex::locks();
    msg.retValue() << "\r\n";
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

static bool logFileOpen()
{
    if (s_logfile) {
	int fd = ::open(s_logfile,O_WRONLY|O_CREAT|O_APPEND,0640);
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
static bool s_runagain = true;
static pid_t s_childpid = -1;
static pid_t s_superpid = -1;

static void superhandler(int signal)
{
    switch (signal) {
	case SIGHUP:
	    if (s_logrotator) {
		::fprintf(stderr,"Supervisor (%d) closing the log file\n",s_superpid);
		logFileOpen();
		::fprintf(stderr,"Supervisor (%d) reopening the log file\n",s_superpid);
	    }
	    break;
	case SIGINT:
	case SIGTERM:
	case SIGABRT:
	    s_runagain = false;
    }
    if (s_childpid > 0)
	::kill(s_childpid,signal);
}

static void copystream(int dest, int src)
{
    for (;;) {
	char buf[MAX_LOGBUFF];
	int rd = ::read(src,buf,sizeof(buf));
	if (rd <= 0)
	    break;
	::write(dest,buf,rd);
    }
}

static int supervise(void)
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
		tmp += sanity;
		if (tmp > MAX_SANITY)
		    tmp = MAX_SANITY;
		if (sanity < tmp)
		    sanity = tmp;
	    }
	    else if ((errno != EINTR) && (errno != EAGAIN))
		break;
	    // Consume sanity points slightly slower than added
	    for (int i = 0; i < 12; i++) {
		copystream(2,logfd[0]);
		::usleep(100000);
	    }
	}
	::close(wdogfd[0]);
	if (s_childpid > 0) {
	    // Child failed to proof sanity. Kill it - no need to be gentle.
	    ::fprintf(stderr,"Supervisor: killing unresponsive child %d\n",s_childpid);
	    // If -Da was specified try to get a corefile
	    if (s_sigabrt) {
		::kill(s_childpid,SIGABRT);
		::usleep(500000);
	    }
	    ::kill(s_childpid,SIGKILL);
	    ::usleep(10000);
	    ::waitpid(s_childpid,0,WNOHANG);
	    s_childpid = -1;
	}
	if (s_logrotator) {
	    copystream(2,logfd[0]);
	    ::close(logfd[0]);
	}
	if (s_runagain)
	    ::usleep(1000000);
    }
    ::fprintf(stderr,"Supervisor (%d) exiting with code %d\n",s_superpid,retcode);
    return retcode;
}
#endif /* _WINDOWS */

SLib::SLib(HMODULE handle, const char* file)
    : m_handle(handle)
{
    DDebug(DebugAll,"SLib::SLib(%p,'%s') [%p]",handle,file,this);
    checkPoint();
}

SLib::~SLib()
{
#ifdef DEBUG
    Debugger debug("SLib::~SLib()"," [%p]",this);
#endif
    if (s_nounload) {
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
	if (fini) {
	    checkPoint();
	    return;
	}
	Debug(DebugWarn,"Could not finalize, will dlclose(%p)",m_handle);
    }
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
    checkPoint();
}

SLib* SLib::load(const char* file, bool local)
{
    DDebug(DebugAll,"SLib::load('%s')",file);
    int flags = RTLD_NOW;
#ifdef RTLD_GLOBAL
    if (!local)
	flags |= RTLD_GLOBAL;
#endif
    HMODULE handle = ::dlopen(file,flags);
    if (handle)
	return new SLib(handle,file);
#ifdef _WINDOWS
    Debug(DebugWarn,"LoadLibrary error %u in '%s'",::GetLastError(),file);
#else
    Debug(DebugWarn,dlerror());
#endif    
    return 0;
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
    s_mode = Stopped;
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
	Debug(DebugGoOn,"Failed to initialize the Windows Sockets library, error code %d",errc);
	return errc & 127;
    }
#else
    ::signal(SIGPIPE,SIG_IGN);
#endif
    SysUsage::init();
    s_runid = Time::secNow();
    s_cfg = configFile(s_cfgfile);
    s_cfg.load();
    Debug(DebugAll,"Engine::run()");
    install(new EngineStatusHandler);
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
    dispatch("engine.start");
    setStatus(SERVICE_RUNNING);
    long corr = 0;
#ifndef _WINDOWS
    ::signal(SIGHUP,sighandler);
    ::signal(SIGQUIT,sighandler);
    ::signal(SIGCHLD,sighandler);
    ::signal(SIGUSR1,sighandler);
    ::signal(SIGUSR2,sighandler);
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

	// Create worker thread if we didn't hear about any of them in a while
	if (s_makeworker && (EnginePrivate::count < s_maxworkers)) {
	    Debug(EnginePrivate::count ? DebugMild : DebugInfo,
		"Creating new message dispatching thread (%d running)",EnginePrivate::count);
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
	Message *m = new Message("engine.timer");
	m->addParam("time",String((int)m->msgTime().sec()));
	// Try to fine tune the ticker
	t = (long)(m->msgTime().usec() % 1000000);
	if (t > 500000)
	    corr -= (1000000-t)/10;
	else
	    corr += t/10;
	XDebug(DebugAll,"Adjustment at %ld, corr %ld",t,corr);
	enqueue(m);
	Thread::yield();
    }
    s_haltcode &= 0xff;
    Output("Yate engine is shutting down with code %d",s_haltcode);
    setStatus(SERVICE_STOP_PENDING);
    dispatch("engine.halt");
    checkPoint();
    Thread::msleep(200);
    m_dispatcher.dequeue();
    checkPoint();
    // We are occasionally doing things that can cause crashes so don't abort
    abortOnBug(s_sigabrt && s_lateabrt);
    Thread::killall();
    checkPoint();
    m_dispatcher.dequeue();
    ::signal(SIGINT,SIG_DFL);
    ::signal(SIGTERM,SIG_DFL);
#ifndef _WINDOWS
    ::signal(SIGHUP,SIG_DFL);
    ::signal(SIGQUIT,SIG_DFL);
#endif
    delete this;
    Debug(DebugAll,"Exiting with %d locked mutexes",Mutex::locks());
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

const char* Engine::pathSeparator()
{
    return PATH_SEP;
}

String Engine::configFile(const char* name, bool user)
{
    String path;
    if (user) {
#ifdef _WINDOWS
	// we force using the ANSI version
	char szPath[MAX_PATH];
	if (SHGetSpecialFolderPathA(NULL,szPath,CSIDL_APPDATA,TRUE))
	    path = szPath;
#else
	path = ::getenv("HOME");
#endif
    }
    if (path.null())
	path = s_cfgpath;
    else {
	if (!path.endsWith(PATH_SEP))
	    path += PATH_SEP;
	path += CFG_DIR;
	::mkdir(path,S_IRWXU);
    }
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
	p = plugins.append(plugin);
	p->setDelete(s_dynplugin);
    }
    else if (p)
	p->remove(false);
    return true;
}

bool Engine::loadPlugin(const char* file, bool local)
{
    s_dynplugin = false;
    SLib *lib = SLib::load(file,local);
    s_dynplugin = true;
    if (lib) {
	m_libs.append(lib);
	return true;
    }
    return false;
}

bool Engine::loadPluginDir(const String& relPath)
{
#ifdef DEBUG
    Debugger debug("Engine::loadPluginDir","('%s')",relPath.c_str());
#endif
    bool defload = s_cfg.getBoolValue("general","modload",true);
    String path = s_modpath;
    if (relPath) {
	if (!path.endsWith(PATH_SEP))
	    path += PATH_SEP;
	path += relPath;
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
	XDebug(DebugInfo,"Found dir entry %s",entry.cFileName);
	int n = ::strlen(entry.cFileName) - s_modsuffix.length();
	if ((n > 0) && !::strcmp(entry.cFileName+n,s_modsuffix)) {
	    if (s_cfg.getBoolValue("modules",entry.cFileName,defload))
		loadPlugin(path + PATH_SEP + entry.cFileName);
	}
    } while (::FindNextFile(hf,&entry));
    ::FindClose(hf);
#else
    DIR *dir = ::opendir(path);
    if (!dir) {
	Debug(DebugWarn,"Engine::loadPlugins() failed directory '%s'",path.safe());
	return false;
    }
    struct dirent *entry;
    while ((entry = ::readdir(dir)) != 0) {
	XDebug(DebugInfo,"Found dir entry %s",entry->d_name);
	int n = ::strlen(entry->d_name) - s_modsuffix.length();
	if ((n > 0) && !::strcmp(entry->d_name+n,s_modsuffix)) {
	    if (s_cfg.getBoolValue("modules",entry->d_name,defload))
		loadPlugin(path + PATH_SEP + entry->d_name,
		    s_cfg.getBoolValue("localsym",entry->d_name,s_localsymbol));
	}
    }
    ::closedir(dir);
#endif
    return true;
}

void Engine::loadPlugins()
{
    const char *name = s_cfg.getValue("general","modpath");
    if (name)
	s_modpath = name;
    name = s_cfg.getValue("general","extrapath");
    if (name)
	s_extramod = name;
    s_maxworkers = s_cfg.getIntValue("general","maxworkers",s_maxworkers);
    s_restarts = s_cfg.getIntValue("general","restarts");
    m_dispatcher.warnTime(1000*(u_int64_t)s_cfg.getIntValue("general","warntime"));
    NamedList *l = s_cfg.getSection("preload");
    if (l) {
        unsigned int len = l->length();
        for (unsigned int i=0; i<len; i++) {
            NamedString *n = l->getParam(i);
            if (n && n->toBoolean())
                loadPlugin(n->name());
	}
    }
    loadPluginDir(String::empty());
    if (s_extramod)
	loadPluginDir(s_extramod);
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
    Output("Initializing plugins");
    dispatch("engine.init");
    ObjList *l = plugins.skipNull();
    for (; l; l = l->skipNext()) {
	Plugin *p = static_cast<Plugin *>(l->get());
	p->initialize();
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

unsigned int Engine::runId()
{
    return s_runid;
}

static void usage(bool client, FILE* f)
{
    ::fprintf(f,
"Usage: yate [options] [commands ...]\n"
"   -h, --help     Display help message (this one) and exit\n"
"   -V, --version  Display program version and exit\n"
"   -v             Verbose debugging (you can use more than once)\n"
"   -q             Quieter debugging (you can use more than once)\n"
"%s"
"   -p filename    Write PID to file\n"
"   -l filename    Log to file\n"
"   -n configname  Use specified configuration name (%s)\n"
"   -c pathname    Path to conf files directory (" CFG_PATH ")\n"
"   -m pathname    Path to modules directory (" MOD_PATH ")\n"
"   -w directory   Change working directory\n"
#ifdef RLIMIT_CORE
"   -C             Enable core dumps if possible\n"
#endif
"   -D[options]    Special debugging options\n"
"     a            Abort if bugs are encountered\n"
"     m            Attempt to debug mutex deadlocks\n"
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
"     t            Timestamp debugging messages relative to program start\n"
"     e            Timestamp debugging messages based on EPOCH (1-1-1970 GMT)\n"
"     f            Timestamp debugging in GMT format YYYYMMDDhhmmss.uuuuuu\n"
    ,client ? "" :
#ifdef _WINDOWS
"   --service      Run as Windows service\n"
"   --install      Install the Windows service\n"
"   --remove       Remove the Windows service\n"
#else
"   -d             Daemonify, suppress output unless logged\n"
"   -s             Supervised, restart if crashes or locks up\n"
"   -r             Enable rotation of log file (needs -s and -l)\n"
#endif
    ,s_cfgfile.safe());
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
    ::fprintf(stdout,"Yate " YATE_VERSION " " YATE_RELEASE "\n");
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
    Debugger::Formatting tstamp = Debugger::None;
    bool colorize = false;
    const char* pidfile = 0;
    const char* workdir = 0;
    int debug_level = debugLevel();

    const char* cfgfile = ::strrchr(argv[0],'/');
    if (!cfgfile)
	cfgfile = ::strrchr(argv[0],'\\');
    if (cfgfile)
	cfgfile++;

    if (!cfgfile)
	cfgfile = argv[0][0] ? argv[0] : "yate";

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
		    case 'r':
			s_logrotator = true;
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
			s_logfile=argv[++i];
			break;
		    case 'n':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			cfgfile=argv[++i];
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
		    case 'w':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			workdir = argv[++i];
			break;
#ifdef RLIMIT_CORE
		    case 'C':
			s_coredump = true;
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
				    Mutex::wait(10000000);
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
				    s_haltcode++;
				    break;
				case 'w':
				    s_makeworker = false;
				    break;
				case 'o':
				    colorize = true;
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
				default:
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

    s_mode = mode;

    s_cfgfile = cfgfile;
    if (s_cfgfile.endsWith(".exe") || s_cfgfile.endsWith(".EXE"))
	s_cfgfile = s_cfgfile.substr(0,s_cfgfile.length()-4);

    if (workdir)
	::chdir(workdir);

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

#ifdef _WINDOWS
    if (!service)
#endif
	logFileOpen();

    debugLevel(debug_level);
    abortOnBug(s_sigabrt);

#ifdef RLIMIT_CORE
    while (s_coredump) {
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

    int retcode = -1;
#ifndef _WINDOWS
    if (supervised)
	retcode = supervise();
    if (retcode >= 0)
	return retcode;
#endif

    Debugger::setFormatting(tstamp);

#ifdef _WINDOWS
    if (service)
	retcode = ::StartServiceCtrlDispatcher(dispatchTable) ? 0 : ::GetLastError();
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
