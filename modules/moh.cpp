/**
 * moh.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Music (on hold) generator
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

static Configuration s_cfg;

static ObjList sources;
static ObjList chans;
static Mutex mutex(true);

class MOHSource : public ThreadedSource
{
public:
    ~MOHSource();
    virtual void run();
    inline const String &name()
	{ return m_name; }
    static MOHSource *getSource(const String &name);
private:
    MOHSource(const String &name, const String &command_line);
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
    MOHChan(const String &name);
    ~MOHChan();
    virtual void disconnected(bool final, const char *reason);
    inline const String &id() const
	{ return m_id; }
private:
    String m_id;
    static int s_nextid;
};

class MOHHandler : public MessageHandler
{
public:
    MOHHandler() : MessageHandler("call.execute") { }
    virtual bool received(Message &msg);
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("engine.status") { }
    virtual bool received(Message &msg);
};

class MOHPlugin : public Plugin
{
public:
    MOHPlugin();
    ~MOHPlugin();
    virtual void initialize();
private:
    MOHHandler *m_handler;
};

MOHSource::MOHSource(const String &name, const String &command_line)
    :   ThreadedSource("slin"), m_name(name), m_command_line(command_line), m_swap(false), m_brate(16000)
{
    Debug(DebugAll,"MOHSource::MOHSource(\"%s\", \"%s\") [%p]", name.c_str(), command_line.c_str(), this);
}

MOHSource::~MOHSource()
{
    Lock lock(mutex);
    Debug(DebugAll,"MOHSource::~MOHSource() [%p]",this);
    sources.remove(this,false);
    if (m_pid > 0)
	::kill(m_pid,SIGTERM);
    if (m_in >= 0) {
	::close(m_in);
	m_in = -1;
    }
}


MOHSource *MOHSource::getSource(const String &name)
{
    String cmd;
    ObjList *l = &sources;
    for (; l; l = l->next()) {
	MOHSource *t = static_cast<MOHSource *>(l->get());
	if (t && (t->name() == name)) {
	    t->ref();
	    return t;
	}
    }
    cmd = s_cfg.getValue("mohs", name);
    if (cmd) {
	MOHSource *s = new MOHSource(name, cmd);
	if(s->start()) {
	    sources.append(s);
	    return s;
	} else
	    return (MOHSource *) NULL;
    } else 
	return (MOHSource *) NULL;
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
    int r = 0;
    u_int64_t tpos = Time::now();
    m_time = tpos;

    do {
	r = (m_in >= 0) ? ::read(m_in,m_data.data(),m_data.length()) : m_data.length();

	if (r < 0) {
	    if (errno == EINTR) {
		r = 1;
		continue;
	    }
	    break;
	}
	if (r < (int)m_data.length())
	    m_data.assign(m_data.data(),r);
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
	    ::usleep((unsigned long)dly);
	}
	Forward(m_data,m_data.length()*8000/m_brate);
	tpos += (r*1000000ULL/m_brate);
    } while (r > 0);
}

int MOHChan::s_nextid = 1;

MOHChan::MOHChan(const String &name)
    : CallEndpoint("moh")
{
    Debug(DebugAll,"MOHChan::MOHChan(\"%s\") [%p]",name.c_str(),this);
    Lock lock(mutex);
    m_id << "moh/" << s_nextid++;
    chans.append(this);
    MOHSource *s = MOHSource::getSource(name);
    if (s) {
	setSource(s);
	s->deref();
    }
    else
	Debug(DebugWarn,"No source '%s' in MOHChan [%p]", name.c_str(), this);
}

MOHChan::~MOHChan()
{
    Debug(DebugAll,"MOHChan::~MOHChan() %s [%p]",m_id.c_str(),this);
    mutex.lock();
    chans.remove(this,false);
    mutex.unlock();
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
    Regexp r("^moh/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    String name = dest.matchString(1);
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	MOHChan *mc = new MOHChan(name);
	if (ch->connect(mc))
	    mc->deref();
	else {
	    mc->destruct();
	    return false;
	}
    }
    else {
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"MOH outgoing call with no target!");
	    return false;
	}
	Message m("call.route");
	m.addParam("id",dest);
	m.addParam("caller",dest);
	m.addParam("called",targ);
	if (Engine::dispatch(m)) {
	    m = "call.execute";
	    m.addParam("callto",m.retValue());
	    m.retValue() = 0;
	    MOHChan *mc = new MOHChan(dest.matchString(1).c_str());
	    m.setParam("id",mc->id());
	    m.userData(mc);
	    if (Engine::dispatch(m)) {
		mc->deref();
		return true;
	    }
	    Debug(DebugWarn,"MOH outgoing call not accepted!");
	    mc->destruct();
	}
	else
	    Debug(DebugWarn,"MOH outgoing call but no route!");
	return false;
    }
    return true;
}

bool AttachHandler::received(Message &msg)
{
    String src(msg.getValue("source"));
    if (src.null())
	return false;
    Regexp r("^moh/\\(.*\\)$");
    if (!src.matches(r))
	return false;
    src = src.matchString(1);
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd) {
	Lock lock(mutex);
	MOHSource *t = MOHSource::getSource(src);
	if (t) {
	    dd->setSource(t);
	    t->deref();
	    // Let the message flow if it wants to attach a consumer too
	    return !msg.getValue("consumer");
	}
	Debug(DebugWarn,"No on-hold source '%s' could be attached to [%p]",src.c_str(),dd);
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
		   << ",chans=" << chans.count() << "\n";
    return false;
}

MOHPlugin::MOHPlugin()
    : m_handler(0)
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
    s_cfg = Engine::configFile("moh");
    s_cfg.load();
    if (!m_handler) {
	m_handler = new MOHHandler;
	Engine::install(m_handler);
	Engine::install(new AttachHandler);
	Engine::install(new StatusHandler);
    }
}

INIT_PLUGIN(MOHPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
