/**
 * Engine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 */

#include "telengine.h"
#include "yatepaths.h"

#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static unsigned long long s_nextinit = 0;
static bool s_makeworker = true;
static bool s_keepclosing = false;
static int s_super_handle = -1;

static void sighandler(int signal)
{
    switch (signal) {
	case SIGHUP:
	case SIGQUIT:
	    if (s_nextinit <= Time::now())
		Engine::init();
	    s_nextinit = Time::now() + 2000000;
	    break;
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

Engine *Engine::s_self = 0;
int Engine::s_haltcode = -1;
bool Engine::s_init = false;
bool Engine::s_dynplugin = false;
int Engine::s_maxworkers = 10;
int EnginePrivate::count = 0;

ObjList plugins;

class SLib : public GenObject
{
public:
    virtual ~SLib();
    static SLib *load(const char *file);
private:
    SLib(void *handle, const char *file);
    const char *m_file;
    void *m_handle;
};

SLib::SLib(void *handle, const char *file)
    : m_handle(handle)
{
#ifdef DEBUG
    Debug(DebugAll,"SLib::SLib(%p,\"%s\") [%p]",handle,file,this);
#endif
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

SLib *SLib::load(const char *file)
{
#ifdef DEBUG
    Debugger debug("SLib::load","(\"%s\")",file);
#endif
    void *handle = ::dlopen(file,RTLD_NOW);
    if (handle)
	return new SLib(handle,file);
    Debug(DebugWarn,dlerror());
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
    EngineStatusHandler() : MessageHandler("status",0) { }
    virtual bool received(Message &msg);
};

bool EngineStatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"engine"))
	return false;
    msg.retValue() << "engine";
    msg.retValue() << ",plugins=" << plugins.count();
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
#ifdef DEBUG
    Debugger debug("Engine::Engine()"," [%p]",this);
#endif
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
    if (s_super_handle >= 0)
	install(new EngineSuperHandler);
    loadPlugins();
    Debug(DebugInfo,"plugins.count() = %d",plugins.count());
    initPlugins();
    ::signal(SIGINT,sighandler);
    ::signal(SIGTERM,sighandler);
    Debug(DebugInfo,"Engine entering main loop");
    dispatch("engine.start");
    unsigned long corr = 0;
    ::signal(SIGHUP,sighandler);
    ::signal(SIGQUIT,sighandler);
    while (s_haltcode == -1) {
	if (s_init) {
	    s_init = false;
	    initPlugins();
	}

	// Create worker thread if we didn't hear about any of them in a while
	if (s_makeworker && (EnginePrivate::count < s_maxworkers)) {
	    Debug(DebugInfo,"Creating new message dispatching thread");
	    new EnginePrivate;
	}
	else
	    s_makeworker = true;

	// Attempt to sleep until the next full second
	unsigned long t = (Time::now() + corr) % 1000000;
	::usleep(1000000 - t);
	Message *m = new Message("engine.timer");
	m->addParam("time",String((int)m->msgTime().sec()));
	// Try to fine tune the ticker
	t = m->msgTime().usec() % 1000000;
	if (t > 500000)
	    corr -= (1000000-t)/10;
	else
	    corr += t/10;
	enqueue(m);
	Thread::yield();
    }
    Debug(DebugInfo,"Engine exiting with code %d",s_haltcode);
    dispatch("engine.halt");
    m_dispatcher.dequeue();
    Thread::killall();
    m_dispatcher.dequeue();
    ::signal(SIGINT,SIG_DFL);
    ::signal(SIGTERM,SIG_DFL);
    ::signal(SIGHUP,SIG_DFL);
    ::signal(SIGQUIT,SIG_DFL);
    delete this;
    Debug(DebugInfo,"Exiting with %d locked mutexes",Mutex::locks());
    return s_haltcode;
}

Engine *Engine::self()
{
    if (!s_self)
	s_self = new Engine;
    return s_self;
}

bool Engine::Register(const Plugin *plugin, bool reg)
{
#ifdef DEBUG
    Debug(DebugInfo,"Engine::Register(%p,%d)",plugin,reg);
#endif
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

bool Engine::loadPlugin(const char *file)
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
    Configuration cfg(configFile("yate"));
    bool defload = cfg.getBoolValue("general","modload",true);
    const char *name = cfg.getValue("general","modpath");
    if (name)
	s_modpath = name;
    NamedList *l = cfg.getSection("preload");
    if (l) {
        unsigned int len = l->length();
        for (unsigned int i=0; i<len; i++) {
            NamedString *n = l->getParam(i);
            if (n && n->toBoolean())
                loadPlugin(n->name());
	}
    }
    DIR *dir = ::opendir(s_modpath);
    if (!dir) {
	Debug(DebugFail,"Engine::loadPlugins() failed opendir()");
	return;
    }
    struct dirent *entry;
    while ((entry = ::readdir(dir)) != 0) {
#ifdef DEBUG
	Debug(DebugInfo,"Found dir entry %s",entry->d_name);
#endif
	int n = ::strlen(entry->d_name) - s_modsuffix.length();
	if ((n > 0) && !::strcmp(entry->d_name+n,s_modsuffix)) {
	    if (cfg.getBoolValue("modules",entry->d_name,defload))
		loadPlugin(s_modpath+"/"+entry->d_name);
	}
    }
    ::closedir(dir);
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
    ObjList *l = &plugins;
    for (; l; l = l->next()) {
	Plugin *p = static_cast<Plugin *>(l->get());
	if (p)
	    p->initialize();
    }
}

void Engine::halt(unsigned int code)
{
    s_haltcode = code;
}

void Engine::init()
{
    s_init = true;
}

bool Engine::install(MessageHandler *handler)
{
    return s_self ? s_self->m_dispatcher.install(handler) : false;
}

bool Engine::uninstall(MessageHandler *handler)
{
    return s_self ? s_self->m_dispatcher.uninstall(handler) : false;
}

bool Engine::enqueue(Message *msg)
{
    return (msg && s_self) ? s_self->m_dispatcher.enqueue(msg) : false;
}

bool Engine::dispatch(Message *msg)
{
    return (msg && s_self) ? s_self->m_dispatcher.dispatch(*msg) : false;
}

bool Engine::dispatch(Message &msg)
{
    return s_self ? s_self->m_dispatcher.dispatch(msg) : false;
}

bool Engine::dispatch(const char *name)
{
    if (!(s_self && name && *name))
	return false;
    Message msg(name);
    return s_self->m_dispatcher.dispatch(msg);
}


static pid_t s_childpid = -1;
static bool s_runagain = true;

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
		    s_runagain = false;
		    retcode = WEXITSTATUS(status);
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
	    // Consume sanity points slighly slower than added
	    ::usleep(1200000);
	}
	::close(wdogfd[0]);
	if (s_childpid > 0) {
	    // Child failed to proof sanity. Kill it - noo need to be gentle.
	    ::fprintf(stderr,"Supervisor: killing unresponsive child %d\n",s_childpid);
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

static void usage(FILE *f)
{
    ::fprintf(f,
"Usage: yate [options]\n"
"   -h             Help message (this one)\n"
"   -v             Verbose debugging (you can use more than once)\n"
"   -q             Quieter debugging (you can use more than once)\n"
"   -d             Daemonify, suppress output unless logged\n"
"   -s             Supervised, restart if crashes or locks up\n"
"   -l filename    Log to file\n"
"   -p filename    Write PID to file\n"
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
    );
}

static void badopt(char chr, const char *opt)
{
    if (chr)
	::fprintf(stderr,"Invalid character '%c' in option '%s'\n",chr,opt);
    else
	::fprintf(stderr,"Invalid option '%s'\n",opt);
    usage(stderr);
}

static void noarg(const char *opt)
{
    ::fprintf(stderr,"Missing parameter to option '%s'\n",opt);
    usage(stderr);
}

int Engine::main(int argc, const char **argv, const char **environ)
{
    bool daemonic = false;
    bool supervised = false;
    int debug_level = debugLevel();
    const char *logfile = 0;
    const char *pidfile = 0;
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
		    case 'd':
			daemonic = true;
			break;
		    case 's':
			supervised = true;
			break;
		    case 'l':
			if (i+1 >= argc) {
			    noarg(argv[i]);
			    return ENOENT;
			}
			pc = 0;
			logfile=argv[++i];
			break;
		    case 'p':
			if (i+1 >= argc) {
			    noarg(argv[i]);
			    return ENOENT;
			}
			pc = 0;
			pidfile=argv[++i];
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
				    abortOnBug(true);
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
	    ::fprintf(stderr,"Invalid non-option '%s'\n",pc);
	    usage(stderr);
	    return EINVAL;
	}
    }
    if (daemonic) {
	Debugger::enableOutput(false);
	if (::daemon(1,0) == -1) {
	    int err = errno;
	    ::fprintf(stderr,"Daemonification failed: %s (%d)\n",::strerror(err),err);
	    return err;
	}
    }
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

    int retcode = supervised ? supervise() : -1;
    if (retcode >= 0)
	return retcode;

    time_t t = ::time(0);
    Output("Yate (%u) is starting %s",::getpid(),::ctime(&t));
    retcode = self()->run();
    t = ::time(0);
    Output("Yate (%u) is stopping %s",::getpid(),::ctime(&t));
    return retcode;
}
