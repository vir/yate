/**
 * moh.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * On hold (music) generator
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

/**
 * Hybrid of tonegen and extmodule. Module for playing music from external
 * processes. Data is read from shell processes started by commands defined
 * in configuration file. Data sources based on external processes are
 * shared by DataEndpoints so number of external processes is limited
 * by number of entries in configuration file.
 * Compiled from tonegen.cpp and extmodule.cpp
 * by Maciek Kaminski (maciejka_at_tiger.com.pl)
 */

#include <yatephone.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>


using namespace TelEngine;
namespace { // anonymous

static Configuration s_cfg;

static ObjList sources;
static ObjList chans;
static Mutex s_mutex(true,"MOH");

class MOHSource : public ThreadedSource
{
public:
    ~MOHSource();
    virtual void run();
    virtual void destroyed();
    inline const String &name()
	{ return m_name; }
    static MOHSource* getSource(String& name, const NamedList& params);
private:
    MOHSource(const String &name, const String &command_line, unsigned int rate = 8000);
    String m_name;
    String m_command_line;
    bool create();
    DataBlock m_data;
    pid_t m_pid;
    int m_in;
    bool m_swap;
    unsigned m_brate;
    u_int64_t m_time;
    String m_id;

};

class MOHChan : public CallEndpoint
{
public:
    MOHChan(String& name, const NamedList& params);
    ~MOHChan();
    virtual void disconnected(bool final, const char *reason);
private:
    static int s_nextid;
};

class MOHHandler;

class MOHPlugin : public Plugin
{
public:
    MOHPlugin();
    ~MOHPlugin();
    virtual void initialize();
private:
    MOHHandler *m_handler;
};

INIT_PLUGIN(MOHPlugin);

class MOHHandler : public MessageHandler
{
public:
    MOHHandler()
	: MessageHandler("call.execute",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler()
	: MessageHandler("chan.attach",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler()
	: MessageHandler("engine.status",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};


MOHSource::MOHSource(const String &name, const String &command_line, unsigned int rate)
    : ThreadedSource("slin"),
      m_name(name), m_command_line(command_line), m_swap(false), m_brate(2*rate)
{
    Debug(DebugAll,"MOHSource::MOHSource('%s','%s',%u) [%p]",name.c_str(),command_line.c_str(),rate,this);
    if (rate != 8000)
	m_format << "/" << rate;
}

MOHSource::~MOHSource()
{
    Debug(DebugAll,"MOHSource::~MOHSource() '%s' [%p]",m_name.c_str(),this);
    if (m_pid > 0)
	::kill(m_pid,SIGTERM);
    if (m_in >= 0) {
	::close(m_in);
	m_in = -1;
    }
}

void MOHSource::destroyed()
{
    DDebug(DebugAll,"MOHSource::destroyed() [%p]",this);
    s_mutex.lock();
    sources.remove(this,false);
    s_mutex.unlock();
    ThreadedSource::destroyed();
}


MOHSource* MOHSource::getSource(String& name, const NamedList& params)
{
    String cmd = s_cfg.getValue("mohs", name);
    unsigned int rate = 8000;
    // honor the rate only if the command knows about it
    if ((cmd.find("${rate}") >= 0) || (cmd.find("${rate$") >= 0))
	rate = params.getIntValue("rate",8000);
    if (params.replaceParams(cmd) > 0) {
	// command is parametrized, suffix name to account for it
	name << "-" << cmd.hash();
	DDebug(DebugInfo,"Parametrized MOH: '%s'",name.c_str());
    }
    ObjList *l = &sources;
    for (; l; l = l->next()) {
	MOHSource *t = static_cast<MOHSource *>(l->get());
	if (t && t->alive() && (t->name() == name)) {
	    t->ref();
	    return t;
	}
    }
    if (cmd) {
	MOHSource *s = new MOHSource(name,cmd,rate);
	if (s->start("MOH Source")) {
	    sources.append(s);
	    return s;
	}
    }
    return 0;
}

bool MOHSource::create()
{
    int pid;
    int ext2yate[2];

    int x;

    if (::pipe(ext2yate)) {
	Debug(DebugWarn, "Unable to create ext->yate pipe: %s",strerror(errno));
	return false;
    }
    pid = ::fork();
    if (pid < 0) {
	Debug(DebugWarn, "Failed to fork(): %s", strerror(errno));
	::close(ext2yate[0]);
	::close(ext2yate[1]);
	return false;
    }
    if (!pid) {
	/* In child - terminate all other threads if needed */
	Thread::preExec();
	/* Try to immunize child from ^C and ^\ */
	::signal(SIGINT,SIG_IGN);
	::signal(SIGQUIT,SIG_IGN);
	/* And restore default handlers for other signals */
	::signal(SIGTERM,SIG_DFL);
	::signal(SIGHUP,SIG_DFL);
	/* Redirect stdout */
	::dup2(ext2yate[1], STDOUT_FILENO);
	/* Close stdin */
	//::close(STDIN_FILENO);
	/* Close everything but stdin/out/ */
	for (x=STDERR_FILENO+1;x<1024;x++)
	    ::close(x);
	/* Execute script */
	if (debugAt(DebugInfo))
	    ::fprintf(stderr, "Execing '%s'\n", m_command_line.c_str());
	::execl("/bin/sh", "sh", "-c", m_command_line.c_str(), (char *)NULL);

	::fprintf(stderr, "Failed to execute '%s': %s\n", m_command_line.c_str(), strerror(errno));
	/* Shit happened. Die as quick and brutal as possible */
	::_exit(1);
    }
    Debug(DebugInfo,"Launched External Script %s, pid: %d", m_command_line.c_str(), pid);
    m_in = ext2yate[0];

    /* close what we're not using in the parent */
    close(ext2yate[1]);

    m_pid = pid;
    return true;
}

void MOHSource::run()
{
    if (!create()) {
	m_pid = 0;
	return;
    }

    m_data.assign(0,(m_brate*20)/1000);
    int r = 1;
    u_int64_t tpos = Time::now();
    m_time = tpos;

    unsigned int pos = 0;
    while ((r > 0) && looping()) {
	unsigned int len = m_data.length() - pos;
	if (m_in >= 0) {
	    unsigned char* ptr = m_data.data(pos,len);
	    r = ptr ? ::read(m_in,ptr,len) : 0;
	}
	else
	    r = len;

	if (r < 0) {
	    if (errno == EINTR) {
		r = 1;
		continue;
	    }
	    break;
	}
	pos += r;
	if (pos < m_data.length()) {
	    if (!r)
		Thread::yield();
	    continue;
	}
	if (m_swap) {
	    uint16_t* p = (uint16_t*)m_data.data();
	    for (int i = 0; i < r; i+= 2) {
		*p = ntohs(*p);
		++p;
	    }
	}
	int64_t dly = tpos - Time::now();
	if (dly > 0) {
	    XDebug("MOH",DebugAll,"Sleeping for " FMT64 " usec",dly);
	    Thread::usleep((unsigned long)dly);
	}
	Forward(m_data);
	pos = 0;
	tpos += (r*1000000ULL/m_brate);
    }
}


int MOHChan::s_nextid = 1;

MOHChan::MOHChan(String& name, const NamedList& params)
    : CallEndpoint("moh")
{
    Debug(DebugAll,"MOHChan::MOHChan(\"%s\") [%p]",name.c_str(),this);
    Lock lock(s_mutex);
    String tmp;
    tmp << "moh/" << s_nextid++;
    setId(tmp);
    chans.append(this);
    MOHSource *s = MOHSource::getSource(name,params);
    if (s) {
	setSource(s);
	s->deref();
    }
    else
	Debug(DebugWarn,"No source '%s' in MOHChan [%p]", name.c_str(), this);
}

MOHChan::~MOHChan()
{
    Debug(DebugAll,"MOHChan::~MOHChan() %s [%p]",id().c_str(),this);
    s_mutex.lock();
    chans.remove(this,false);
    s_mutex.unlock();
}

void MOHChan::disconnected(bool final, const char *reason)
{
    Debugger debug("MOHChan::disconnected()"," '%s' [%p]",reason,this);
}


bool MOHHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    static const Regexp r("^moh/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    String name = dest.matchString(1);
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (ch) {
	MOHChan* mc = new MOHChan(name,msg);
	if (ch->connect(mc,msg.getValue("reason"))) {
	    msg.setParam("peerid",mc->id());
	    mc->deref();
	}
	else {
	    mc->destruct();
	    return false;
	}
    }
    else {
	String callto(msg.getValue("direct"));
	Message m(msg);
	m.retValue().clear();
	m.clearParam("callto");
	m.setParam("id",dest);
	m.setParam("caller",dest);
	if (callto.null()) {
	    m = "call.route";
	    const char *targ = msg.getValue("target");
	    if (!targ)
		targ = msg.getValue("called");
	    if (!targ) {
		Debug(DebugWarn,"MOH outgoing call with no target!");
		return false;
	    }
	    m.setParam("called",targ);
	    if (!Engine::dispatch(m))
		return false;
	    callto = m.retValue();
	    if (callto.null() || (callto == "-"))
		return false;
	    m.retValue().clear();
	}
	m = "call.execute";
	m.addParam("callto",callto);
	MOHChan* mc = new MOHChan(name,msg);
	m.setParam("id",mc->id());
	m.userData(mc);
	if (Engine::dispatch(m)) {
	    msg.setParam("id",mc->id());
	    mc->deref();
	    return true;
	}
	Debug(DebugWarn,"MOH outgoing call not accepted!");
	mc->destruct();
	return false;
    }
    return true;
}


bool AttachHandler::received(Message &msg)
{
    String src(msg.getValue("source"));
    if (src.null())
	return false;
    static const Regexp r("^moh/\\(.*\\)$");
    if (!src.matches(r))
	return false;
    src = src.matchString(1);
    CallEndpoint *ch = static_cast<CallEndpoint *>(msg.userData());
    if (ch) {
	Lock lock(s_mutex);
	MOHSource* t = MOHSource::getSource(src,msg);
	if (t) {
	    ch->setSource(t);
	    t->deref();
	    // Let the message flow if it wants to attach a consumer too
	    return !msg.getValue("consumer");
	}
	Debug(DebugWarn,"No on-hold source '%s' could be attached to [%p]",src.c_str(),ch);
    }
    else
	Debug(DebugWarn,"On-hold '%s' attach request with no data channel!",src.c_str());
    return false;
}


bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"moh"))
	return false;
    msg.retValue() << "name=moh,type=misc"
		   << ";sources=" << sources.count()
		   << ",chans=" << chans.count() << "\r\n";
    return false;
}


MOHPlugin::MOHPlugin()
    : Plugin("moh"),
      m_handler(0)
{
    Output("Loaded module MOH");
}

MOHPlugin::~MOHPlugin()
{
    Output("Unloading module MOH");
    ObjList *l = &chans;
    while (l) {
	MOHChan *t = static_cast<MOHChan *>(l->get());
	if (t)
	    t->disconnect("shutdown");
	if (l->get() == t)
	    l = l->next();
    }
    chans.clear();
    sources.clear();
}

void MOHPlugin::initialize()
{
    Output("Initializing module MOH");
    s_mutex.lock();
    s_cfg = Engine::configFile("moh");
    s_cfg.load();
    s_mutex.unlock();
    if (!m_handler) {
	m_handler = new MOHHandler;
	Engine::install(m_handler);
	Engine::install(new AttachHandler);
	Engine::install(new StatusHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
