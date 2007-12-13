/**
 * extmodule.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * External module handler
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 * Portions copyright (C) 2005 Maciek Kaminski
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

#ifdef _WINDOWS

#include <process.h>

#else
#include <yatepaths.h>

#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>


using namespace TelEngine;
namespace { // anonymous

// Maximum length of an incoming line
#define MAX_INCOMING_LINE 8192

// Default message timeout in milliseconds
#define MSG_TIMEOUT 10000

static Configuration s_cfg;
static ObjList s_chans;
static ObjList s_modules;
static Mutex s_mutex(true);
static int s_timeout = MSG_TIMEOUT;
static bool s_timebomb = false;

class ExtModReceiver;
class ExtModChan;

class ExtModSource : public ThreadedSource
{
public:
    ExtModSource(Stream* str, ExtModChan* chan);
    ~ExtModSource();
    virtual void run();
private:
    Stream* m_str;
    unsigned m_brate;
    unsigned m_total;
    ExtModChan* m_chan;
};

class ExtModConsumer : public DataConsumer
{
public:
    ExtModConsumer(Stream* str);
    ~ExtModConsumer();
    virtual void Consume(const DataBlock& data, unsigned long timestamp);
private:
    Stream* m_str;
    unsigned m_total;
};

class ExtModChan : public CallEndpoint
{
public:
    enum {
	NoChannel,
	DataNone,
	DataRead,
	DataWrite,
	DataBoth
    };
    ExtModChan(ExtModReceiver* recv);
    static ExtModChan* build(const char* file, const char* args, int type);
    ~ExtModChan();
    virtual void disconnected(bool final, const char* reason);
    inline ExtModReceiver* receiver() const
	{ return m_recv; }
    inline void setRecv(ExtModReceiver* recv)
	{ m_recv = recv; }
    inline void setRunning(bool running)
	{ m_running = running; }
    inline bool running() const
	{ return m_running; }
    inline void setDisconn(bool disconn)
	{ m_disconn = disconn; }
    inline bool disconn() const
	{ return m_disconn; }
    inline void setId(const String& id)
	{ CallEndpoint::setId(id); }
    inline const Message* waitMsg() const
	{ return m_waitRet; }
    inline void waitMsg(const Message* msg)
	{ m_waitRet = msg; }
    inline bool waiting() const
	{ return m_waiting; }
    inline void waiting(bool wait)
	{ m_waiting = wait; }
private:
    ExtModChan(const char* file, const char* args, int type);
    ExtModReceiver *m_recv;
    const Message* m_waitRet;
    int m_type;
    bool m_running;
    bool m_disconn;
    bool m_waiting;
};

// Great idea - thanks, Maciek!
class ExtMessage : public Message
{
    YCLASS(ExtMessage,Message)
public:
    inline ExtMessage(ExtModReceiver* recv)
	: Message(""), m_receiver(recv)
	{ }
    void startup();
    virtual void dispatched(bool accepted);
    inline bool belongsTo(ExtModReceiver* recv) const
	{ return m_receiver == recv; }
    inline int decode(const char* str)
	{ return Message::decode(str,m_id); }
    inline const String& id() const
	{ return m_id; }
private:
    ExtModReceiver* m_receiver;
    String m_id;
};

class MsgHolder : public GenObject
{
public:
    MsgHolder(Message &msg);
    Message &m_msg;
    bool m_ret;
    String m_id;
    bool decode(const char *s);
    inline const Message* msg() const
	{ return &m_msg; }
};

// Yet Another of Maciek's ideas
class MsgWatcher : public MessagePostHook
{
public:
    inline MsgWatcher(ExtModReceiver* receiver)
	: m_receiver(receiver)
	{ }
    virtual ~MsgWatcher();
    virtual void dispatched(const Message& msg, bool handled);
    bool addWatched(const String& name);
    bool delWatched(const String& name);
private:
    ExtModReceiver* m_receiver;
    ObjList m_watched;
};

class ExtModReceiver : public MessageReceiver, public Mutex
{
    friend class MsgWatcher;
public:
    enum {
	RoleUnknown,
	RoleGlobal,
	RoleChannel
    };
    static ExtModReceiver* build(const char *script, const char *args,
	File* ain = 0, File* aout = 0, ExtModChan *chan = 0);
    static ExtModReceiver* build(const char* name, Stream* io, ExtModChan* chan = 0, int role = RoleUnknown);
    static ExtModReceiver* find(const String& script);
    ~ExtModReceiver();
    virtual bool received(Message& msg, int id);
    bool processLine(const char* line);
    bool outputLine(const char* line);
    void reportError(const char* line);
    void returnMsg(const Message* msg, const char* id, bool accepted);
    bool addWatched(const String& name);
    bool delWatched(const String& name);
    bool start();
    void run();
    void cleanup();
    bool flush();
    void die(bool clearChan = true);
    inline void use()
	{ m_use++; }
    bool unuse();
    inline const String& scriptFile() const
	{ return m_script; }
    inline const String& commandArg() const
	{ return m_args; }
    inline bool selfWatch() const
	{ return m_selfWatch; }
    inline void setRestart(bool restart)
	{ m_restart = restart; }

private:
    ExtModReceiver(const char* script, const char* args,
	File* ain, File* aout, ExtModChan* chan);
    ExtModReceiver(const char* name, Stream* io, ExtModChan* chan, int role);
    bool create(const char* script, const char* args);
    void closeIn();
    void closeOut();
    void closeAudio();
    int m_role;
    bool m_dead;
    int m_use;
    pid_t m_pid;
    Stream* m_in;
    Stream* m_out;
    File* m_ain;
    File* m_aout;
    ExtModChan* m_chan;
    MsgWatcher* m_watcher;
    bool m_selfWatch;
    bool m_reenter;
    int m_timeout;
    bool m_timebomb;
    bool m_restart;
    String m_script, m_args;
    ObjList m_waiting;
    ObjList m_relays;
    String m_reason;
};

class ExtThread : public Thread
{
public:
    ExtThread(ExtModReceiver* receiver)
	: Thread("ExtModule"), m_receiver(receiver)
	{ }
    virtual void run()
	{ m_receiver->run(); }
    virtual void cleanup()
	{ m_receiver->cleanup(); }
private:
    ExtModReceiver* m_receiver;
};

class ExtModHandler : public MessageHandler
{
public:
    ExtModHandler(const char *name, unsigned prio) : MessageHandler(name,prio) { }
    virtual bool received(Message &msg);
};

class ExtModCommand : public MessageHandler
{
public:
    ExtModCommand(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class ExtListener : public Thread
{
public:
    ExtListener(const char* name);
    virtual bool init(const NamedList& sect);
    virtual void run();
    inline const String& name() const
	{ return m_name; }
    static ExtListener* build(const char* name, const NamedList& sect);
protected:
    Socket m_socket;
    String m_name;
    int m_role;
};

class ExtModulePlugin : public Plugin
{
public:
    ExtModulePlugin();
    ~ExtModulePlugin();
    virtual void initialize();
    virtual bool isBusy() const;
private:
    ExtModHandler *m_handler;
};


static bool runProgram(const char *script, const char *args)
{
#ifdef _WINDOWS
    int pid = ::_spawnl(_P_DETACH,script,args,NULL);
    if (pid < 0) {
	Debug(DebugWarn, "Failed to _spawnl(): %d: %s", errno, strerror(errno));
	return false;
    }
#else
    int pid = ::fork();
    if (pid < 0) {
	Debug(DebugWarn, "Failed to fork(): %d: %s", errno, strerror(errno));
	return false;
    }
    if (!pid) {
	// In child - terminate all other threads if needed
	Thread::preExec();
	// Try to immunize child from ^C and ^\ the console may receive
	::signal(SIGINT,SIG_IGN);
	::signal(SIGQUIT,SIG_IGN);

	// And restore default handlers for other signals
	::signal(SIGTERM,SIG_DFL);
	::signal(SIGHUP,SIG_DFL);
	// Blindly close everything but stdin/out/err
	for (int f=STDERR_FILENO+1;f<1024;f++)
	    ::close(f);
	// Execute script
	if (debugAt(DebugInfo))
	    ::fprintf(stderr, "Execing program '%s' '%s'\n", script, args);
        ::execl(script, script, args, (char *)NULL);
	::fprintf(stderr, "Failed to execute '%s': %d: %s\n", script, errno, strerror(errno));
	// Shit happened. Die as quick and brutal as possible
	::_exit(1);
    }
#endif
    Debug(DebugInfo,"Launched external program %s", script);
    return true;
}


ExtModSource::ExtModSource(Stream* str, ExtModChan* chan)
    : m_str(str), m_brate(16000), m_total(0), m_chan(chan)
{
    Debug(DebugAll,"ExtModSource::ExtModSource(%p) [%p]",str,this);
    if (m_str) {
	chan->setRunning(true);
	start("ExtModSource");
    }
}

ExtModSource::~ExtModSource()
{
    Debug(DebugAll,"ExtModSource::~ExtModSource() [%p] total=%u",this,m_total);
    m_chan->setRunning(false);
    if (m_str) {
	Stream* tmp = m_str;
	m_str = 0;
	delete tmp;
    }
}

void ExtModSource::run()
{
    char data[320];
    int r = 0;
    u_int64_t tpos = Time::now();
    do {
	if (!m_str) {
	    Thread::yield();
	    continue;
	}
	r = m_str->readData(data,sizeof(data));
	if (r < 0) {
	    if (errno == EINTR) {
		r = 1;
		continue;
	    }
	    break;
	}
	// TODO: allow data to provide its own rate
	int64_t dly = tpos - Time::now();
	if (dly > 0) {
	    XDebug("ExtModSource",DebugAll,"Sleeping for " FMT64 " usec",dly);
	    Thread::usleep((unsigned long)dly);
	}
	if (r <= 0)
	    continue;
	DataBlock buf(data,r,false);
	Forward(buf,m_total/2);
	buf.clear(false);
	m_total += r;
	tpos += (r*(u_int64_t)1000000/m_brate);
    } while (r > 0);
    Debug(DebugAll,"ExtModSource [%p] end of data total=%u",this,m_total);
    m_chan->setRunning(false);
}

ExtModConsumer::ExtModConsumer(Stream* str)
    : m_str(str), m_total(0)
{
    Debug(DebugAll,"ExtModConsumer::ExtModConsumer(%p) [%p]",str,this);
}

ExtModConsumer::~ExtModConsumer()
{
    Debug(DebugAll,"ExtModConsumer::~ExtModConsumer() [%p] total=%u",this,m_total);
    if (m_str) {
	Stream* tmp = m_str;
	m_str = 0;
	delete tmp;
    }
}

void ExtModConsumer::Consume(const DataBlock& data, unsigned long timestamp)
{
    if ((m_str) && !data.null()) {
	m_str->writeData(data);
	m_total += data.length();
    }
}

ExtModChan* ExtModChan::build(const char* file, const char* args, int type)
{
    ExtModChan* chan = new ExtModChan(file,args,type);
    if (!chan->m_recv) {
	chan->destruct();
	return 0;
    }
    return chan;
}

ExtModChan::ExtModChan(const char* file, const char* args, int type)
    : CallEndpoint("ExtModule"),
      m_recv(0), m_waitRet(0), m_type(type), m_disconn(false), m_waiting(false)
{
    Debug(DebugAll,"ExtModChan::ExtModChan(%d) [%p]",type,this);
    File* reader = 0;
    File* writer = 0;
    switch (m_type) {
	case DataWrite:
	case DataBoth:
	    {
		reader = new File;
		File* tmp = new File;
		if (File::createPipe(*reader,*tmp)) {
		    setConsumer(new ExtModConsumer(tmp));
		    getConsumer()->deref();
		}
		else
		    delete tmp;
	    }
    }
    switch (m_type) {
	case DataRead:
	case DataBoth:
	    {
		writer = new File;
		File* tmp = new File;
		if (File::createPipe(*tmp,*writer)) {
		    setSource(new ExtModSource(tmp,this));
		    getSource()->deref();
		}
		else
		    delete tmp;
	    }
    }
    s_mutex.lock();
    s_chans.append(this);
    s_mutex.unlock();
    m_recv = ExtModReceiver::build(file,args,reader,writer,this);
}

ExtModChan::ExtModChan(ExtModReceiver* recv)
    : CallEndpoint("ExtModule"), m_recv(recv), m_type(DataNone), m_disconn(false)
{
    Debug(DebugAll,"ExtModChan::ExtModChan(%p) [%p]",recv,this);
    s_mutex.lock();
    s_chans.append(this);
    s_mutex.unlock();
}

ExtModChan::~ExtModChan()
{
    Debugger debug(DebugAll,"ExtModChan::~ExtModChan()"," [%p]",this);
    s_mutex.lock();
    s_chans.remove(this,false);
    s_mutex.unlock();
    setSource();
    setConsumer();
    if (m_recv)
	m_recv->die(false);
}

void ExtModChan::disconnected(bool final, const char *reason)
{
    Debug(DebugAll,"ExtModChan::disconnected() '%s' [%p]",reason,this);
    if (final || Engine::exiting())
	return;
    if (m_disconn) {
	Message* m = new Message("chan.disconnected");
	m->userData(this);
	m->addParam("id",id());
	m->addParam("module","external");
	if (m_recv)
	    m->addParam("address",m_recv->scriptFile());
	if (reason)
	    m->addParam("reason",reason);
	if (getPeer())
	    m->addParam("peerid",getPeer()->id());
	Engine::enqueue(m);
    }
}

MsgHolder::MsgHolder(Message &msg)
    : m_msg(msg), m_ret(false)
{
    // the address of this object should be unique
    char buf[64];
    ::sprintf(buf,"%p.%ld",this,random());
    m_id = buf;
}

bool MsgHolder::decode(const char *s)
{
    return (m_msg.decode(s,m_ret,m_id) == -2);
}


void ExtMessage::startup()
{
    if (m_receiver && m_id)
	m_receiver->use();
    Engine::enqueue(this);
}

void ExtMessage::dispatched(bool accepted)
{
    if (m_receiver && m_id) {
	m_receiver->returnMsg(this,m_id,accepted);
	m_receiver->unuse();
    }
}
  

MsgWatcher::~MsgWatcher()
{
    Engine::self()->setHook(this,true);
    if (m_receiver && (m_receiver->m_watcher == this))
	m_receiver->m_watcher = 0;
    m_receiver = 0;
}

void MsgWatcher::dispatched(const Message& msg, bool handled)
{
    if (!m_receiver)
	return;

    if (!m_receiver->selfWatch()) {
	// check if the message was generated by ourselves - avoid reentrance
	ExtMessage* m = YOBJECT(ExtMessage,&msg);
	if (m && m->belongsTo(m_receiver))
	    return;
    }

    ObjList* l = m_watched.skipNull();
    for (; l; l = l->skipNext()) {
	const String* s = static_cast<const String*>(l->get());
	if (s->null() || (*s == msg)) {
	    m_receiver->returnMsg(&msg,"",handled);
	    break;
	}
    }
}

bool MsgWatcher::addWatched(const String& name)
{
    if (m_watched.find(name))
	return false;
    // wildcard watches will be inserted first for speed reason
    if (name.null())
	m_watched.insert(new String);
    else
	m_watched.append(new String(name));
    return true;
}

bool MsgWatcher::delWatched(const String& name)
{
    GenObject* obj = m_watched[name];
    if (obj) {
	m_watched.remove(obj);
	return true;
    }
    return false;
}


ExtModReceiver* ExtModReceiver::build(const char* script, const char* args,
				      File* ain, File* aout, ExtModChan* chan)
{
    ExtModReceiver* recv = new ExtModReceiver(script,args,ain,aout,chan);
    if (!recv->start()) {
	recv->destruct();
	return 0;
    }
    return recv;
}

ExtModReceiver* ExtModReceiver::build(const char* name, Stream* io, ExtModChan* chan, int role)
{
    ExtModReceiver* recv = new ExtModReceiver(name,io,chan,role);
    if (!recv->start()) {
	recv->destruct();
	return 0;
    }
    return recv;
}

ExtModReceiver* ExtModReceiver::find(const String& script)
{
    Lock lock(s_mutex);
    ObjList *l = &s_modules;
    for (; l; l=l->next()) {
	ExtModReceiver *r = static_cast<ExtModReceiver *>(l->get());
	if (r && (r->scriptFile() == script))
	    return r;
    }
    return 0;
}

bool ExtModReceiver::unuse()
{
    int u = --m_use;
    if (!u)
	destruct();
    return (u <= 0);
}

ExtModReceiver::ExtModReceiver(const char* script, const char* args, File* ain, File* aout, ExtModChan* chan)
    : Mutex(true),
      m_role(RoleUnknown), m_dead(false), m_use(0), m_pid(-1),
      m_in(0), m_out(0), m_ain(ain), m_aout(aout),
      m_chan(chan), m_watcher(0), m_selfWatch(false), m_reenter(false),
      m_timeout(s_timeout), m_timebomb(s_timebomb), m_restart(false),
      m_script(script), m_args(args)
{
    Debug(DebugAll,"ExtModReceiver::ExtModReceiver(\"%s\",\"%s\") [%p]",script,args,this);
    m_script.trimBlanks();
    m_args.trimBlanks();
    m_role = chan ? RoleChannel : RoleGlobal;
    s_mutex.lock();
    s_modules.append(this);
    s_mutex.unlock();
}

ExtModReceiver::ExtModReceiver(const char* name, Stream* io, ExtModChan* chan, int role)
    : Mutex(true),
      m_role(role), m_dead(false), m_use(0), m_pid(-1),
      m_in(io), m_out(io), m_ain(0), m_aout(0),
      m_chan(chan), m_watcher(0), m_selfWatch(false), m_reenter(false),
      m_timeout(s_timeout), m_timebomb(s_timebomb), m_restart(false),
      m_script(name)
{
    Debug(DebugAll,"ExtModReceiver::ExtModReceiver(\"%s\",%p,%p) [%p]",name,io,chan,this);
    m_script.trimBlanks();
    if (chan)
	m_role = RoleChannel;
    s_mutex.lock();
    s_modules.append(this);
    s_mutex.unlock();
}

ExtModReceiver::~ExtModReceiver()
{   
    Debug(DebugAll,"ExtModReceiver::~ExtModReceiver() [%p] pid=%d",this,m_pid);
    Lock lock(this);
    // One destruction is plenty enough
    m_use = -100;
    s_mutex.lock();
    s_modules.remove(this,false);
    s_mutex.unlock();
    die();
    if (m_pid > 1)
	Debug(DebugWarn,"ExtModReceiver::~ExtModReceiver() [%p] pid=%d",this,m_pid);
    closeAudio();
    if (m_restart && !Engine::exiting()) {
	Debug(DebugMild,"Restarting external '%s' '%s'",m_script.safe(),m_args.safe());
	ExtModReceiver::build(m_script,m_args);
    }
}

void ExtModReceiver::closeIn()
{
    if (!m_in)
	return;
    Stream* tmp = m_in;
    m_in = 0;
    if (m_out == tmp)
	m_out = 0;
    if (tmp)
	delete tmp;
}

void ExtModReceiver::closeOut()
{
    if (!m_out)
	return;
    Stream* tmp = m_out;
    m_out = 0;
    if (m_in == tmp)
	m_in = 0;
    if (tmp)
	delete tmp;
}

void ExtModReceiver::closeAudio()
{
    if (m_ain) {
	delete m_ain;
	m_ain = 0;
    }
    if (m_aout) {
	delete m_aout;
	m_aout = 0;
    }
}

bool ExtModReceiver::start()
{
    if (m_pid < 0) {
	ExtThread *ext = new ExtThread(this);
	if (!ext->startup())
	    return false;
	while (m_pid < 0)
	    Thread::yield();
    }
    return (m_pid >= 0);
}


bool ExtModReceiver::flush()
{
    TelEngine::destruct(m_watcher);
    // Make sure we release all pending messages and not accept new ones
    if (!Engine::exiting())
	m_relays.clear();
    else {
	ObjList *p = &m_relays;
	for (; p; p=p->next())
	    p->setDelete(false);
    }
    if (m_waiting.get()) {
	DDebug(DebugAll,"ExtModReceiver releasing %u pending messages [%p]",
	    m_waiting.count(),this);
	m_waiting.clear();
	Thread::yield();
	return true;
    }
    return false;
}

void ExtModReceiver::die(bool clearChan)
{
#ifdef DEBUG
    Debugger debug(DebugAll,"ExtModReceiver::die()"," pid=%d dead=%s [%p]",
	m_pid,m_dead ? "yes" : "no",this);
#else
    Debug(DebugAll,"ExtModReceiver::die() pid=%d dead=%s [%p]",
	m_pid,m_dead ? "yes" : "no",this);
#endif
    if (m_dead)
	return;
    m_dead = true;
    use();
    flush();

    ExtModChan *chan = m_chan;
    m_chan = 0;
    if (chan)
	chan->setRecv(0);

    // Give the external script a chance to die gracefully
    closeOut();
    if (m_pid > 1) {
	Debug(DebugAll,"ExtModReceiver::die() waiting for pid=%d to die",m_pid);
	for (int i=0; i<100; i++) {
	    Thread::yield();
	    if (m_pid <= 0)
		break;
	}
    }
    if (m_pid > 1)
	Debug(DebugInfo,"ExtModReceiver::die() pid=%d did not exit?",m_pid);

    // Now terminate the process and close its stdout pipe
    closeIn();
#ifndef _WINDOWS
    if (m_pid > 1)
	::kill(m_pid,SIGTERM);
#endif
    if (chan && clearChan)
	chan->disconnect(m_reason);
    unuse();
}

bool ExtModReceiver::received(Message &msg, int id)
{
    lock();
    // check if we are no longer running
    bool ok = (m_pid > 0) && m_in && m_out;
    if (ok && !m_reenter) {
	// check if the message was generated by ourselves - avoid reentrance
	ExtMessage* m = YOBJECT(ExtMessage,&msg);
	if (m && m->belongsTo(this))
	    ok = false;
    }
    if (!ok) {
	unlock();
	return false;
    }

    use();
    bool fail = false;
    u_int64_t tout = (m_timeout > 0) ? Time::now() + 1000 * m_timeout : 0;
    MsgHolder h(msg);
    if (outputLine(msg.encode(h.m_id))) {
	m_waiting.append(&h)->setDelete(false);
	DDebug(DebugAll,"ExtMod [%p] queued message '%s' [%p]",this,msg.c_str(),&msg);
    }
    else {
	Debug(DebugWarn,"ExtMod [%p] could not queue message '%s'",this,msg.c_str());
	ok = false;
	fail = true;
    }
    unlock();
    // would be nice to lock the MsgHolder and wait for it to unlock from some
    //  other thread - unfortunately this does not work with all mutexes
    // sorry, Maciek - have to do it work in Windows too :-(
    while (ok) {
	Thread::yield();
	lock();
	ok = (m_waiting.find(&h) != 0);
	if (ok && tout && (Time::now() > tout)) {
	    Debug(DebugWarn,"Message '%s' did not return in %d msec [%p]",
		msg.c_str(),m_timeout,this);
	    m_waiting.remove(&h,false);
	    ok = false;
	    fail = true;
	}
	unlock();
    }
    DDebug(DebugAll,"ExtMod [%p] message '%s' [%p] returning %s",this,msg.c_str(),&msg, h.m_ret ? "true" : "false");
    if (fail && m_timebomb)
	die();
    unuse();
    return h.m_ret;
}

bool ExtModReceiver::create(const char *script, const char *args)
{
#ifdef _WINDOWS
    return false;
#else
    String tmp(script);
    int pid;
    HANDLE ext2yate[2];
    HANDLE yate2ext[2];
    int x;
    if (script[0] != '/') {
	tmp = Engine::sharedPath();
	tmp << Engine::pathSeparator() << "scripts";
	tmp = s_cfg.getValue("general","scripts_dir",tmp);
	if (!tmp.endsWith(Engine::pathSeparator()))
	    tmp += Engine::pathSeparator();
	tmp += script;
    }
    script = tmp.c_str();
    if (::pipe(ext2yate)) {
	Debug(DebugWarn, "Unable to create ext->yate pipe: %s",strerror(errno));
	return false;
    }
    if (pipe(yate2ext)) {
	Debug(DebugWarn, "unable to create yate->ext pipe: %s", strerror(errno));
	::close(ext2yate[0]);
	::close(ext2yate[1]);
	return false;
    }
    pid = ::fork();
    if (pid < 0) {
	Debug(DebugWarn, "Failed to fork(): %s", strerror(errno));
	::close(yate2ext[0]);
	::close(yate2ext[1]);
	::close(ext2yate[0]);
	::close(ext2yate[1]);
	return false;
    }
    if (!pid) {
	// In child - terminate all other threads if needed
	Thread::preExec();
	// Try to immunize child from ^C and ^\ the console may receive
	::signal(SIGINT,SIG_IGN);
	::signal(SIGQUIT,SIG_IGN);
	// And restore default handlers for other signals
	::signal(SIGTERM,SIG_DFL);
	::signal(SIGHUP,SIG_DFL);
	// Redirect stdin and out
	::dup2(yate2ext[0], STDIN_FILENO);
	::dup2(ext2yate[1], STDOUT_FILENO);
	// Set audio in/out handlers
	if (m_ain && m_ain->valid())
	    ::dup2(m_ain->handle(), STDERR_FILENO+1);
	else
	    ::close(STDERR_FILENO+1);
	if (m_aout && m_aout->valid())
	    ::dup2(m_aout->handle(), STDERR_FILENO+2);
	else
	    ::close(STDERR_FILENO+2);
	// Blindly close everything but stdin/out/err/audio
	for (x=STDERR_FILENO+3;x<1024;x++) 
	    ::close(x);
	// Execute script
	if (debugAt(DebugInfo))
	    ::fprintf(stderr, "Execing '%s' '%s'\n", script, args);
        ::execl(script, script, args, (char *)NULL);
	::fprintf(stderr, "Failed to execute '%s': %s\n", script, strerror(errno));
	// Shit happened. Die as quick and brutal as possible
	::_exit(1);
    }
    Debug(DebugInfo,"Launched External Script %s", script);
    m_in = new File(ext2yate[0]);
    m_out = new File(yate2ext[1]);

    // close what we're not using in the parent
    close(ext2yate[1]);
    close(yate2ext[0]);
    closeAudio();
    m_pid = pid;
    return true;
#endif
}

void ExtModReceiver::cleanup()
{
#ifdef DEBUG
    Debugger debug(DebugAll,"ExtModReceiver::cleanup()"," [%p]",this);
#endif
#ifndef _WINDOWS
    // We must call waitpid from here - same thread we started the child
    if (m_pid > 1) {
	// No thread switching if possible
	closeOut();
	Thread::yield();
	int w = ::waitpid(m_pid, 0, WNOHANG);
	if (w == 0) {
	    Debug(DebugWarn,"Process %d has not exited on closing stdin - we'll kill it",m_pid);
	    ::kill(m_pid,SIGTERM);
	    Thread::yield();
	    w = ::waitpid(m_pid, 0, WNOHANG);
	}
	if (w == 0)
	    Debug(DebugWarn,"Process %d has still not exited yet?",m_pid);
	else if ((w < 0) && (errno != ECHILD))
	    Debug(DebugMild,"Failed waitpid on %d: %s",m_pid,strerror(errno));
    }
    if (m_pid > 0)
	m_pid = 0;
#endif
    unuse();
}

void ExtModReceiver::run()
{
    // the i/o streams may be already allocated
    if (m_in && m_out)
	m_pid = 1; // just an indicator, not really init ;-)
    // we must do the forking from this thread so we can later wait() on it
    else if (!create(m_script.safe(),m_args.safe())) {
	m_pid = 0;
	return;
    }
    use();
    if (m_in)
	m_in->setBlocking(false);
    char buffer[MAX_INCOMING_LINE];
    int posinbuf = 0;
    DDebug(DebugAll,"ExtModReceiver::run() entering loop [%p]",this);
    for (;;) {
	use();
	int readsize = m_in ? m_in->readData(buffer+posinbuf,sizeof(buffer)-posinbuf-1) : 0;
	if (unuse())
	    return;
	if (!readsize) {
	    lock();
	    if (m_in)
		Debug("ExtModule",DebugInfo,"Read EOF on %p [%p]",m_in,this);
	    closeIn();
	    flush();
	    unlock();
	    if (m_chan && m_chan->running())
		Thread::sleep(1);
	    break;
	}
	else if (readsize < 0) {
	    if (m_in && m_in->canRetry()) {
		Thread::msleep(5);
		continue;
	    }
	    Debug("ExtModule",DebugWarn,"Read error %d on %p [%p]",errno,m_in,this);
	    break;
	}
	XDebug(DebugAll,"ExtModReceiver::run() read %d",readsize);
	int totalsize = readsize + posinbuf;
	buffer[totalsize]=0;
	for (;;) {
	    char *eoline = ::strchr(buffer,'\n');
	    if (!eoline && ((int)::strlen(buffer) < totalsize))
		eoline=buffer+::strlen(buffer);
	    if (!eoline)
		break;
	    *eoline = 0;
	    if ((eoline > buffer) && (eoline[-1] == '\r'))
		eoline[-1] = 0;
	    if (buffer[0]) {
		use();
		bool goOut = processLine(buffer);
		if (unuse() || goOut)
		    return;
	    }
	    totalsize -= eoline-buffer+1;
	    ::memmove(buffer,eoline+1,totalsize+1);
	}
	posinbuf = totalsize;
    }
}

bool ExtModReceiver::outputLine(const char* line)
{
    DDebug("ExtModReceiver",DebugAll,"%soutputLine '%s'",
	(m_out ? "" : "failing "), line);
    if (!m_out)
	return false;
    m_out->writeData(line);
    char nl = '\n';
    m_out->writeData(&nl,sizeof(nl));
    return true;
}

void ExtModReceiver::reportError(const char* line)
{
    Debug("ExtModReceiver",DebugWarn,"Error: '%s'", line);
    outputLine("Error in: " + String(line));
}

void ExtModReceiver::returnMsg(const Message* msg, const char* id, bool accepted)
{
    String ret(msg->encode(accepted,id));
    lock();
    outputLine(ret);
    unlock();
}

bool ExtModReceiver::addWatched(const String& name)
{
    Lock mylock(this);
    if (!m_watcher) {
	m_watcher = new MsgWatcher(this);
	Engine::self()->setHook(m_watcher);
    }
    return m_watcher->addWatched(name);
}

bool ExtModReceiver::delWatched(const String& name)
{
    Lock mylock(this);
    return m_watcher && m_watcher->delWatched(name);
}

bool ExtModReceiver::processLine(const char* line)
{
    DDebug("ExtModReceiver",DebugAll,"processLine '%s'", line);
    String id(line);
    if (m_role == RoleUnknown) {
	if (id.startSkip("%%>connect:",false)) {
	    int sep = id.find(':');
	    String role;
	    String chan;
	    String type;
	    if (sep >= 0) {
		role = id.substr(0,sep);
		id = id.substr(sep+1);
		sep = id.find(':');
		if (sep >= 0) {
		    chan = id.substr(0,sep);
		    type = id.substr(sep+1);
		}
		else
		    chan = id;
	    }
	    else
		role = id;
	    DDebug("ExtModReceiver",DebugAll,"role '%s' chan '%s' type '%s'",
		role.c_str(),chan.c_str(),type.c_str());
	    if (role == "global") {
		m_role = RoleGlobal;
		return false;
	    }
	    else if (role == "channel") {
		m_role = RoleChannel;
		return false;
	    }
	    Debug(DebugWarn,"Unknown role '%s' received [%p]",role.c_str(),this);
	}
	else
	    Debug(DebugWarn,"Expecting %%%%>connect, received '%s' [%p]",id.c_str(),this);
	return true;
    }
    else if (id.startsWith("%%<message:")) {
	Lock mylock(this);
	ObjList *p = &m_waiting;
	for (; p; p=p->next()) {
	    MsgHolder *msg = static_cast<MsgHolder *>(p->get());
	    if (msg && msg->decode(line)) {
		DDebug("ExtModReceiver",DebugInfo,"Matched message %p [%p]",msg->msg(),this);
		if (m_chan && (m_chan->waitMsg() == msg->msg())) {
		    DDebug("ExtModReceiver",DebugNote,"Entering wait mode on channel %p [%p]",m_chan,this);
		    m_chan->waitMsg(0);
		    m_chan->waiting(true);
		}
		p->remove(false);
		return false;
	    }
	}
	Debug("ExtModReceiver",DebugWarn,"Unmatched message: %s",line);
	return false;
    }
    else if (id.startSkip("%%>install:",false)) {
	int prio = 100;
	id >> prio >> ":";
	bool ok = true;
	String fname;
	String fvalue;
	Regexp r("^\\([^:]*\\):\\([^:]*\\):\\?\\(.*\\)");
	if (id.matches(r)) {
	    // a filter is specified
	    fname = id.matchString(2);
	    fvalue = id.matchString(3);
	    id = id.matchString(1);
	}
	// sanity checks
	ok = ok && id && !m_relays.find(id);
	if (ok) {
	    MessageRelay *r = new MessageRelay(id,this,0,prio);
	    if (fname)
		r->setFilter(fname,fvalue);
	    m_relays.append(r);
	    Engine::install(r);
	}
	if (debugAt(DebugAll)) {
	    String tmp;
	    if (fname)
		tmp << "filter: '" << fname << "'='" << fvalue << "' ";
	    tmp << (ok ? "ok" : "failed");
	    Debug("ExtModReceiver",DebugAll,"Install '%s', prio %d %s",
		id.c_str(),prio,tmp.c_str());
	}
	String out("%%<install:");
	out << prio << ":" << id << ":" << ok;
	outputLine(out);
	return false;
    }
    else if (id.startSkip("%%>uninstall:",false)) {
	int prio = 0;
	bool ok = false;
	ObjList *p = &m_relays;
	for (; p; p=p->next()) {
	    MessageRelay *r = static_cast<MessageRelay *>(p->get());
	    if (r && (*r == id)) {
		prio = r->priority();
		p->remove();
		ok = true;
		break;
	    }
	}
	Debug("ExtModReceiver",DebugAll,"Uninstall '%s' %s", id.c_str(),ok ? "ok" : "failed");
	String out("%%<uninstall:");
	out << prio << ":" << id << ":" << ok;
	outputLine(out);
	return false;
    }
    else if (id.startSkip("%%>watch:",false)) {
	bool ok = addWatched(id);
	Debug("ExtModReceiver",DebugAll,"Watch '%s' %s", id.c_str(),ok ? "ok" : "failed");
	String out("%%<watch:");
	out << id << ":" << ok;
	outputLine(out);
	return false;
    }
    else if (id.startSkip("%%>unwatch:",false)) {
	bool ok = delWatched(id);
	Debug("ExtModReceiver",DebugAll,"Unwatch '%s' %s", id.c_str(),ok ? "ok" : "failed");
	String out("%%<unwatch:");
	out << id << ":" << ok;
	outputLine(out);
	return false;
    }
    else if (id.startSkip("%%>output:",false)) {
	id.trimBlanks();
	Output("%s",id.safe());
	return false;
    }
    else if (id.startSkip("%%>setlocal:",false)) {
	int col = id.find(':');
	if (col > 0) {
	    String val(id.substr(col+1));
	    id = id.substr(0,col);
	    bool ok = false;
	    if (m_chan && (id == "id")) {
		if (val.null())
		    val = m_chan->id();
		else
		    m_chan->setId(val);
		ok = true;
	    }
	    else if (m_chan && (id == "disconnected")) {
		m_chan->setDisconn(val.toBoolean(m_chan->disconn()));
		val = m_chan->disconn();
		ok = true;
	    }
	    else if (id == "reason") {
		m_reason = val;
		ok = true;
	    }
	    else if (id == "timeout") {
		m_timeout = val.toInteger(m_timeout);
		val = m_timeout;
		ok = true;
	    }
	    else if (id == "timebomb") {
		m_timebomb = val.toBoolean(m_timebomb);
		val = m_timebomb;
		ok = true;
	    }
	    else if (id == "restart") {
		m_restart = (RoleGlobal == m_role) && val.toBoolean(m_restart);
		val = m_restart;
		ok = true;
	    }
	    else if (id == "reenter") {
		m_reenter = val.toBoolean(m_reenter);
		val = m_reenter;
		ok = true;
	    }
	    else if (id == "selfwatch") {
		m_selfWatch = val.toBoolean(m_selfWatch);
		val = m_selfWatch;
		ok = true;
	    }
	    DDebug("ExtModReceiver",DebugAll,"Set '%s'='%s' %s",
		id.c_str(),val.c_str(),ok ? "ok" : "failed");
	    String out("%%<setlocal:");
	    out << id << ":" << val << ":" << ok;
	    outputLine(out);
	    return false;
	}
    }
    else if (id == "%%>quit") {
	outputLine("%%<quit");
	return true;
    }
    else {
	ExtMessage* m = new ExtMessage(this);
	if (m->decode(line) == -2) {
	    DDebug("ExtModReceiver",DebugAll,"Created message %p '%s' [%p]",m,m->c_str(),this);
	    lock();
	    bool note = true;
	    while (m_chan && m_chan->waiting()) {
		if (note) {
		    note = false;
		    Debug("ExtModReceiver",DebugNote,"Waiting before enqueueing new message %p '%s' [%p]",
			m,m->c_str(),this);
		}
		unlock();
		Thread::yield();
		lock();
	    }
	    ExtModChan* chan = 0;
	    if ((m_role == RoleChannel) && !m_chan && (*m == "call.execute")) {
		// we delayed channel creation as there was nothing to ref() it
		chan = new ExtModChan(this);
		m_chan = chan;
		m->setParam("id",chan->id());
	    }
	    m->userData(m_chan);
	    // now the newly created channel is referenced by the message
	    if (chan)
		chan->deref();
	    id = m->id();
	    if (id) {
		// Copy the user data pointer from waiting message with same id
		ObjList *p = &m_waiting;
		for (; p; p=p->next()) {
		    MsgHolder *h = static_cast<MsgHolder *>(p->get());
		    if (h && (h->m_id == id)) {
			RefObject* ud = h->m_msg.userData();
			Debug("ExtModReceiver",DebugAll,"Copying data pointer %p from %p",ud,&(h->m_msg));
			m->userData(ud);
			break;
		    }
		}
	    }
	    m->startup();
	    unlock();
	    return false;
	}
	m->destruct();
    }
    reportError(line);
    return false;
}

bool ExtModHandler::received(Message& msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^external/\\([^/]*\\)/\\([^ ]*\\)\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    String t = dest.matchString(1);
    int typ = 0;
    if (t == "nochan")
	typ = ExtModChan::NoChannel;
    else if (t == "nodata")
	typ = ExtModChan::DataNone;
    else if (t == "play")
	typ = ExtModChan::DataRead;
    else if (t == "record")
	typ = ExtModChan::DataWrite;
    else if (t == "playrec")
	typ = ExtModChan::DataBoth;
    else {
	Debug(DebugGoOn,"Invalid ExtModule method '%s', use 'nochan', 'nodata', 'play', 'record' or 'playrec'",
	    t.c_str());
	return false;
    }
    if (typ == ExtModChan::NoChannel) {
	ExtModReceiver *r = ExtModReceiver::build(dest.matchString(2).c_str(),
						  dest.matchString(3).trimBlanks().c_str());
	return r ? r->received(msg,1) : false;
    }
    ExtModChan *em = ExtModChan::build(dest.matchString(2).c_str(),
				       dest.matchString(3).c_str(),typ);
    if (!em) {
	Debug(DebugGoOn,"Failed to create ExtMod for '%s'",dest.matchString(2).c_str());
	return false;
    }
    // new messages must be blocked until connect() returns (if applicable)
    if (ch)
	em->waitMsg(&msg);
    if (!(em->receiver() && em->receiver()->received(msg,1))) {
	em->waitMsg(0);
	int level = DebugWarn;
	if (msg.getValue("error") || msg.getValue("reason"))
	    level = DebugNote;
	Debug(level,"ExtMod '%s' did not handle call message",dest.matchString(2).c_str());
	em->waiting(false);
	em->deref();
	return false;
    }
    if (ch) {
	em->waitMsg(0);
	ch->connect(em);
	em->waiting(false);
    }
    em->deref();
    return true;
}

bool ExtModCommand::received(Message& msg)
{
    String line(msg.getValue("line"));
    if (!line.startsWith("external",true))
	return false;
    line >> "external";
    line.trimBlanks();
    if (line.null()) {
	msg.retValue() = "";
	int n = 0;
	Lock lock(s_mutex);
	ObjList *l = &s_modules;
	for (; l; l=l->next()) {
	    ExtModReceiver *r = static_cast<ExtModReceiver *>(l->get());
	    if (r)
		msg.retValue() << ++n << ". " << r->scriptFile() << " " << r->commandArg() << "\r\n";
	}
	return true;
    }
    int blank = line.find(' ');
    bool start = line.startSkip("start");
    bool restart = start || line.startSkip("restart");
    if (restart || line.startSkip("stop")) {
	if (line.null())
	    return false;
	blank = line.find(' ');
	ExtModReceiver *r = ExtModReceiver::find(line.substr(0,blank));
	if (r) {
	    if (start) {
		msg.retValue() = "External already running\r\n";
		return true;
	    }
	    else {
		r->setRestart(false);
		r->die();
		msg.retValue() = "External command stopped\r\n";
	    }
	}
	else
	    msg.retValue() = "External not running\r\n";
	if (!restart)
	    return true;
    }
    ExtModReceiver *r = ExtModReceiver::build(line.substr(0,blank),
	(blank >= 0) ? line.substr(blank+1).c_str() : (const char*)0);
    msg.retValue() = r ? "External start attempt\r\n" : "External command failed\r\n";
    return true;
}


ExtListener::ExtListener(const char* name)
    : Thread("ExtListener"), m_name(name), m_role(ExtModReceiver::RoleUnknown)
{
}

bool ExtListener::init(const NamedList& sect)
{
    String role(sect.getValue("role"));
    if (role == "global")
	m_role = ExtModReceiver::RoleGlobal;
    else if (role == "channel")
	m_role = ExtModReceiver::RoleChannel;
    else if (role) {
	Debug(DebugWarn,"Unknown role '%s' of listener '%s'",role.c_str(),m_name.c_str());
	return false;
    }
    String type(sect.getValue("type"));
    SocketAddr addr;
    if (type.null())
	return false;
    else if (type == "unix") {
	String path(sect.getValue("path"));
	if (path.null() || !addr.assign(AF_UNIX) || !addr.host(path))
	    return false;
	File::remove(path);
    }
    else if (type == "tcp") {
	String host(sect.getValue("addr","127.0.0.1"));
	int port = sect.getIntValue("port");
	if (host.null() || !port || !addr.assign(AF_INET)
	    || !addr.host(host) || !addr.port(port))
	    return false;
    }
    else {
	Debug(DebugWarn,"Unknown type '%s' of listener '%s'",type.c_str(),m_name.c_str());
	return false;
    }
    if (!m_socket.create(addr.family(),SOCK_STREAM)) {
	Debug(DebugWarn,"Could not create socket for listener '%s' error %d: %s",
	    m_name.c_str(),m_socket.error(),strerror(m_socket.error()));
	return false;
    }
    m_socket.setReuse();
    if (!m_socket.bind(addr)) {
	Debug(DebugWarn,"Could not bind listener '%s' error %d: %s",
	    m_name.c_str(),m_socket.error(),strerror(m_socket.error()));
	return false;
    }
    if (!m_socket.setBlocking(false) || !m_socket.listen())
	return false;
    return startup();
}

void ExtListener::run()
{
    SocketAddr addr;
    for (;;) {
	Thread::msleep(5,true);
	Socket* skt = m_socket.accept(addr);
	if (!skt) {
	    if (m_socket.canRetry())
		continue;
	    Debug(DebugWarn,"Error on accept(), shutting down ExtListener '%s'",m_name.c_str());
	    break;
	}
	String tmp = addr.host();
	if (addr.port())
	    tmp << ":" << addr.port();
	Debug(DebugInfo,"Listener '%s' got connection from '%s'",m_name.c_str(),tmp.c_str());
	switch (m_role) {
	    case ExtModReceiver::RoleUnknown:
	    case ExtModReceiver::RoleGlobal:
	    case ExtModReceiver::RoleChannel:
		ExtModReceiver::build(m_name,skt,0,m_role);
		break;
	    default:
		Debug(DebugWarn,"Listener '%s' hit invalid role %d",m_name.c_str(),m_role);
		delete skt;
	}
    }
}

ExtListener* ExtListener::build(const char* name, const NamedList& sect)
{
    if (null(name))
	return 0;
    ExtListener* ext = new ExtListener(name);
    if (!ext->init(sect)) {
	Debug(DebugGoOn,"Could not start listener '%s'",name);
	delete ext;
	ext = 0;
    }
    return ext;
}


ExtModulePlugin::ExtModulePlugin()
    : m_handler(0)
{
    Output("Loaded module ExtModule");
}

ExtModulePlugin::~ExtModulePlugin()
{
    Output("Unloading module ExtModule");
    s_mutex.lock();
    s_modules.clear();
    // the receivers destroyed above should also clear chans but better be sure
    s_chans.clear();
    s_mutex.unlock();
}

bool ExtModulePlugin::isBusy() const
{
    Lock lock(s_mutex);
    return (s_chans.count() != 0);
}

void ExtModulePlugin::initialize()
{
    Output("Initializing module ExtModule");
    s_cfg = Engine::configFile("extmodule");
    s_cfg.load();
    s_timeout = s_cfg.getIntValue("general","timeout",MSG_TIMEOUT);
    s_timebomb = s_cfg.getBoolValue("general","timebomb",false);
    if (!m_handler) {
	m_handler = new ExtModHandler("call.execute",s_cfg.getIntValue("general","priority",100));
	Engine::install(m_handler);
	Engine::install(new ExtModCommand("engine.command"));
	NamedList *sect = 0;
	int n = s_cfg.sections();
	for (int i = 0; i < n; i++) {
	    sect = s_cfg.getSection(i);
	    if (!sect)
		continue;
	    String s(*sect);
	    if (s.startSkip("listener",true) && s)
		ExtListener::build(s,*sect);
	}
	// start any scripts only after the listeners
	sect = s_cfg.getSection("scripts");
	if (sect) {
	    unsigned int len = sect->length();
	    for (unsigned int i=0; i<len; i++) {
		NamedString *n = sect->getParam(i);
		if (n)
		    ExtModReceiver::build(n->name(),*n);
	    }
	}
	// and now start additional programs
	sect = s_cfg.getSection("execute");
	if (sect) {
	    unsigned int len = sect->length();
	    for (unsigned int i=0; i<len; i++) {
		NamedString *n = sect->getParam(i);
		if (n)
		    runProgram(n->name(),*n);
	    }
	}
    }
}

INIT_PLUGIN(ExtModulePlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
