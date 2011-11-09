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
#define CFG_DIR ".yate"
#endif

static int s_childsig = 0;

#endif // _WINDOWS

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
    EngineCommand() : MessageHandler("engine.command") { }
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
int Engine::s_haltcode = -1;
int EnginePrivate::count = 0;
static String s_cfgpath(CFG_PATH);
static String s_usrpath;
static bool s_createusr = true;
static bool s_init = false;
static bool s_dynplugin = false;
static Engine::PluginMode s_loadMode = Engine::LoadFail;
static int s_maxworkers = 10;
static bool s_debug = true;

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

class EngineHelp : public MessageHandler
{
public:
    EngineHelp() : MessageHandler("engine.help") { }
    virtual bool received(Message &msg);
};


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


bool EngineStatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"engine"))
	return false;
    msg.retValue() << "name=engine,type=system";
    msg.retValue() << ",version=" << YATE_VERSION;
    msg.retValue() << ",nodename=" << Engine::nodeName();
    msg.retValue() << ";plugins=" << plugins.count();
    msg.retValue() << ",inuse=" << Engine::self()->usedPlugins();
    msg.retValue() << ",handlers=" << Engine::self()->handlerCount();
    msg.retValue() << ",messages=" << Engine::self()->messageCount();
    msg.retValue() << ",supervised=" << (s_super_handle >= 0);
    msg.retValue() << ",runattempt=" << s_run_attempt;
#ifndef _WINDOWS
    msg.retValue() << ",lastsignal=" << s_childsig;
#endif
    msg.retValue() << ",threads=" << Thread::count();
    msg.retValue() << ",workers=" << EnginePrivate::count;
    msg.retValue() << ",mutexes=" << Mutex::count();
    msg.retValue() << ",locks=" << Mutex::locks();
    msg.retValue() << ",semaphores=" << Semaphore::count();
    msg.retValue() << ",waiting=" << Semaphore::locks();
    msg.retValue() << ",acceptcalls=" << lookup(Engine::accept(),Engine::getCallAcceptStates());
    if (msg.getBoolValue("details",true)) {
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
    return false;
}

static char s_cmdsOpt[] = "  module {{load|reload} modulefile|unload modulename|list}\r\n";
static char s_cmdsMsg[] = "Controls the modules loaded in the Telephony Engine\r\n";

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
    if (partLine.null() || (partLine == "help"))
	completeOne(msg.retValue(),"module",partWord);
    else if (partLine == "status")
	completeOne(msg.retValue(),"engine",partWord);
    else if (partLine == "module") {
	completeOne(msg.retValue(),"load",partWord);
	completeOne(msg.retValue(),"unload",partWord);
	completeOne(msg.retValue(),"reload",partWord);
	completeOne(msg.retValue(),"list",partWord);
    }
    else if (partLine == "module load")
	completeModule(msg.retValue(),partWord,Engine::self()->m_libs,false);
    else if (partLine == "module reload")
	completeModule(msg.retValue(),partWord,Engine::self()->m_libs,true);
    else if (partLine == "module unload") {
	for (ObjList* l = Engine::self()->m_libs.skipNull();l;l = l->skipNext()) {
	    SLib* s = static_cast<SLib*>(l->get());
	    if (s->unload(false))
		completeOne(msg.retValue(),*s,partWord);
	}
    }
    else if (partLine == "reload") {
	for (ObjList* l = plugins.skipNull(); l; l = l->skipNext()) {
	    const Plugin* p = static_cast<const Plugin*>(l->get());
	    completeOne(msg.retValue(),p->name(),partWord);
	}
    }
}

bool EngineCommand::received(Message &msg)
{
    String line = msg.getValue("line");
    if (line.null()) {
	doCompletion(msg,msg.getValue("partline"),msg.getValue("partword"));
	return false;
    }
    if (!line.startSkip("module"))
	return false;

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
    String line = msg.getValue("line");
    if (line.null()) {
	msg.retValue() << s_cmdsOpt;
	return false;
    }
    if (line != "module")
	return false;
    msg.retValue() << s_cmdsOpt << s_cmdsMsg;
    return true;
}


void EnginePrivate::run()
{
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
	::write(dest,buf,rd);
    }
    rotatelogs();
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
    s_cfg = configFile(s_cfgfile);
    s_cfg.load();
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
    s_maxworkers = s_cfg.getIntValue("general","maxworkers",s_maxworkers);
    s_restarts = s_cfg.getIntValue("general","restarts");
    m_dispatcher.warnTime(1000*(u_int64_t)s_cfg.getIntValue("general","warntime"));
    extraPath(clientMode() ? "client" : "server");
    extraPath(s_cfg.getValue("general","extrapath"));

    s_params.addParam("version",YATE_VERSION);
    s_params.addParam("release",YATE_STATUS YATE_RELEASE);
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
    s_params.addParam("maxworkers",String(s_maxworkers));
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
    DDebug(DebugAll,"Engine::run()");
    install(new EngineStatusHandler);
    install(new EngineCommand);
    install(new EngineHelp);
    loadPlugins();
    Debug(DebugAll,"Loaded %d plugins",plugins.count());
    internalStatisticsStart();
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
    setStatus(SERVICE_RUNNING);
    long corr = 0;
#ifndef _WINDOWS
    ::signal(SIGHUP,sighandler);
    ::signal(SIGQUIT,sighandler);
    ::signal(SIGCHLD,sighandler);
    ::signal(SIGUSR1,sighandler);
    ::signal(SIGUSR2,sighandler);
#endif
    Output("Yate%s engine is initialized and starting up%s%s",
	clientMode() ? " client" : "",s_node.null() ? "" : " on " ,s_node.safe());
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
	Message* m = new Message("engine.timer",0,true);
	m->addParam("time",String((int)m->msgTime().sec()));
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
    Output("Yate engine is shutting down with code %d",s_haltcode);
    setStatus(SERVICE_STOP_PENDING);
    ::signal(SIGINT,SIG_DFL);
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
    unsigned int cnt = plugins.count();
    plugins.clear();
    if (mux || cnt)
	Debug(DebugGoOn,"Exiting with %d locked mutexes and %u plugins loaded!",mux,cnt);
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
	XDebug(DebugInfo,"Found dir entry %s",entry.cFileName);
	int n = ::strlen(entry.cFileName) - s_modsuffix.length();
	if ((n > 0) && !::strcmp(entry.cFileName+n,s_modsuffix)) {
	    if (s_cfg.getBoolValue("modules",entry.cFileName,defload))
		loadPlugin(path + PATH_SEP + entry.cFileName,false,
		    s_cfg.getBoolValue("nounload",entry.cFileName));
	}
    } while (::FindNextFile(hf,&entry) && !exiting());
    ::FindClose(hf);
#else
    DIR *dir = ::opendir(path);
    if (!dir) {
	Debug(DebugWarn,"Engine::loadPlugins() failed directory '%s'",path.safe());
	return false;
    }
    struct dirent *entry;
    while (((entry = ::readdir(dir)) != 0) && !exiting()) {
	XDebug(DebugInfo,"Found dir entry %s",entry->d_name);
	int n = ::strlen(entry->d_name) - s_modsuffix.length();
	if ((n > 0) && !::strcmp(entry->d_name+n,s_modsuffix)) {
	    if (s_cfg.getBoolValue("modules",entry->d_name,defload))
		loadPlugin(path + PATH_SEP + entry->d_name,
		    s_cfg.getBoolValue("localsym",entry->d_name,s_localsymbol),
		    s_cfg.getBoolValue("nounload",entry->d_name));
	}
    }
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
            if (n && n->toBoolean()) {
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
            if (n && n->toBoolean()) {
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

bool Engine::dispatch(const char* name, bool broadcast)
{
    if (!(s_self && name && *name))
	return false;
    Message msg(name,0,broadcast);
    if (nodeName())
	msg.addParam("nodename",nodeName());
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
"   -e pathname    Path to shared files directory (" SHR_PATH ")\n"
"   -c pathname    Path to conf files directory (" CFG_PATH ")\n"
"   -u pathname    Path to user files directory (%s)\n"
"   -m pathname    Path to modules directory (" MOD_PATH ")\n"
"   -x relpath     Relative path to extra modules directory (can be repeated)\n"
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
"     d            Disable locking debugging and safety features\n"
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
"     z            Timestamp debugging in local timezone YYYYMMDDhhmmss.uuuuuu\n"
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
    ::fprintf(stdout,"Yate " YATE_VERSION " " YATE_STATUS YATE_RELEASE "\n");
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
			supervised = true;
			break;
		    case 'r':
			s_logrotator = true;
			break;
#endif
		    case 't':
			s_logtruncate = true;
			break;
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
		    case 'e':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_shrpath=argv[++i];
			break;
		    case 'c':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_cfgpath=argv[++i];
			break;
		    case 'u':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			usrpath=argv[++i];
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
		    case 'x':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			extraPath(argv[++i]);
			break;
		    case 'N':
			if (i+1 >= argc) {
			    noarg(client,argv[i]);
			    return ENOENT;
			}
			pc = 0;
			s_node=argv[++i];
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
				    break;
				case 'd':
				    Lockable::disableSafety();
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
				case 'z':
				    tstamp = Debugger::TextLocal;
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
	retcode = supervise();
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
	retcode = engineRun();

    return retcode;
}

void Engine::help(bool client, bool errout)
{
    initUsrPath(s_usrpath);
    usage(client, errout ? stderr : stdout);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
