/**
 * extmodule.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * External module handler
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Portions copyright (C) 2005 Maciek Kaminski
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

// Minimum length of the incoming line buffer
#define MIN_INCOMING_LINE 2048
// Default length of the incoming line buffer
#define DEF_INCOMING_LINE 8192
// Maximum length of the incoming line buffer
#define MAX_INCOMING_LINE 65536

// Default message timeout in milliseconds
#define MSG_TIMEOUT 10000

// Safety wait time after we flushed watchers, relays or messages (in ms)
#define WAIT_FLUSH 5

static Configuration s_cfg;
static ObjList s_chans;
static ObjList s_modules;
static Mutex s_mutex(true,"ExtModule");
static Mutex s_uses(false,"ExtModUse");
static int s_waitFlush = WAIT_FLUSH;
static int s_timeout = MSG_TIMEOUT;
static bool s_timebomb = false;
static bool s_pluginSafe = true;
static const char* s_trackName = 0;

static const char* s_cmds[] = {
    "info",
    "start",
    "stop",
    "restart",
    "execute",
    0
};

static const char s_helpExternalCmd[] = "external [info] [stop scriptname] [[start|restart] scriptname [parameter]] [execute progname [parameter]]";
static const char s_helpExternalInfo[] = "List, (re)start and stop scripts or execute an external program";

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
    virtual unsigned long Consume(const DataBlock& data, unsigned long timestamp, unsigned long flags);
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
    inline ExtMessage()
	: Message(""),
	  m_receiver(0), m_accepted(false)
	{ }
    virtual ~ExtMessage();
    void startup(ExtModReceiver* recv);
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
    bool m_accepted;
};

class MsgHolder : public GenObject, public Semaphore
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
    virtual void dispatched(const Message& msg, bool handled);
    bool addWatched(const String& name);
    bool delWatched(const String& name);
    void clear();
protected:
    virtual void destroyed();
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
    static ExtModReceiver* build(const char *script, const char *args, bool ref = false,
	File* ain = 0, File* aout = 0, ExtModChan *chan = 0);
    static ExtModReceiver* build(const char* name, Stream* io, ExtModChan* chan = 0,
	int role = RoleUnknown, const char* conn = 0);
    static ExtModReceiver* find(const String& script);
    virtual void destruct();
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
    bool useUnlocked();
    bool use();
    bool unuse();
    inline const String& scriptFile() const
	{ return m_script; }
    inline const String& commandArg() const
	{ return m_args; }
    inline bool selfWatch() const
	{ return m_selfWatch; }
    inline void setRestart(bool restart)
	{ m_restart = restart; }
    inline bool dead() const
	{ return m_dead || m_quit || (m_use <= 0); }
    void describe(String& rval) const;

private:
    ExtModReceiver(const char* script, const char* args,
	File* ain, File* aout, ExtModChan* chan);
    ExtModReceiver(const char* name, Stream* io, ExtModChan* chan,
	int role, const char* conn);
    bool create(const char* script, const char* args);
    void closeIn();
    void closeOut();
    void closeAudio();
    bool outputLineInternal(const char* line, int len);
    int m_role;
    bool m_dead;
    bool m_quit;
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
    bool m_setdata;
    bool m_writing;
    int m_timeout;
    bool m_timebomb;
    bool m_restart;
    bool m_scripted;
    DataBlock m_buffer;
    String m_script, m_args;
    ObjList m_waiting;
    ObjList m_relays;
    String m_trackName;
    String m_reason;
};

class ExtThread : public Thread
{
public:
    ExtThread(ExtModReceiver* receiver)
	: Thread("ExtMod Receiver"), m_receiver(receiver)
	{ }
    virtual void run()
	{ m_receiver->run(); }
    virtual void cleanup()
	{ m_receiver->cleanup(); }
private:
    ExtModReceiver* m_receiver;
};

class ExtModHandler;

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

INIT_PLUGIN(ExtModulePlugin);

class ExtModHandler : public MessageHandler
{
public:
    ExtModHandler(const char *name, unsigned prio)
	: MessageHandler(name,prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class ExtModCommand : public MessageHandler
{
public:
    ExtModCommand()
	: MessageHandler("engine.command",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
private:
    bool complete(const String& partLine, const String& partWord, String& rval) const;
};

class ExtModStatus : public MessageHandler
{
public:
    ExtModStatus()
	: MessageHandler("engine.status",110,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class ExtModHelp : public MessageHandler
{
public:
    ExtModHelp()
	: MessageHandler("engine.help",100,__plugin.name())
	{ }
    virtual bool received(Message& msg);
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

static void adjustPath(String& script)
{
    if (script.null() || script.startsWith(Engine::pathSeparator()))
	return;
    String tmp = Engine::sharedPath();
    tmp << Engine::pathSeparator() << "scripts";
    tmp = s_cfg.getValue("general","scripts_dir",tmp);
    Engine::runParams().replaceParams(tmp);
    if (!tmp.endsWith(Engine::pathSeparator()))
	tmp += Engine::pathSeparator();
    script = tmp + script;
}

ExtModSource::ExtModSource(Stream* str, ExtModChan* chan)
    : m_str(str), m_brate(16000), m_total(0), m_chan(chan)
{
    Debug(DebugAll,"ExtModSource::ExtModSource(%p) [%p]",str,this);
    if (m_str) {
	chan->setRunning(true);
	start("ExtMod Source");
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
    int r = 1;
    u_int64_t tpos = Time::now();
    while ((r > 0) && looping()) {
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
    }
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

unsigned long ExtModConsumer::Consume(const DataBlock& data, unsigned long timestamp, unsigned long flags)
{
    if ((m_str) && !data.null()) {
	m_str->writeData(data);
	m_total += data.length();
	return invalidStamp();
    }
    return 0;
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
      m_recv(0), m_waitRet(0), m_type(type),
      m_running(false), m_disconn(false), m_waiting(false)
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
    m_recv = ExtModReceiver::build(file,args,true,reader,writer,this);
}

ExtModChan::ExtModChan(ExtModReceiver* recv)
    : CallEndpoint("ExtModule"),
      m_recv(recv), m_waitRet(0), m_type(DataNone),
      m_running(false), m_disconn(false), m_waiting(false)
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
    ExtModReceiver* recv = m_recv;
    m_recv = 0;
    s_mutex.unlock();
    setSource();
    setConsumer();
    if (recv)
	recv->die(false);
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
	String peerId;
	if (getPeerId(peerId) && peerId)
	    m->addParam("peerid",peerId);
	Engine::enqueue(m);
    }
}


MsgHolder::MsgHolder(Message &msg)
    : m_msg(msg), m_ret(false)
{
    // the address of this object should be unique
    char buf[64];
    ::sprintf(buf,"%p.%ld",this,Random::random());
    m_id = buf;
}

bool MsgHolder::decode(const char *s)
{
    return (m_msg.decode(s,m_ret,m_id) == -2);
}


ExtMessage::~ExtMessage()
{
    if (m_receiver) {
	m_receiver->returnMsg(this,m_id,m_accepted);
	m_receiver->unuse();
    }
}

void ExtMessage::startup(ExtModReceiver* recv)
{
    if (recv && m_id && recv->use())
	m_receiver = recv;
    Engine::enqueue(this);
}

void ExtMessage::dispatched(bool accepted)
{
    m_accepted = accepted;
    Message::dispatched(accepted);
}


void MsgWatcher::destroyed()
{
    clear();
    MessagePostHook::destroyed();
}

void MsgWatcher::dispatched(const Message& msg, bool handled)
{
    Lock lock(s_uses);
    ExtModReceiver* recv = m_receiver;
    if (!recv || recv->dead() || (recv->m_watcher != this) || !recv->useUnlocked())
	return;
    if (!lock.acquire(recv)) {
	recv->unuse();
	return;
    }

    if (!recv->selfWatch()) {
	// check if the message was generated by ourselves - avoid reentrance
	ExtMessage* m = YOBJECT(ExtMessage,&msg);
	if (m && m->belongsTo(recv)) {
	    recv->unuse();
	    return;
	}
    }

    ObjList* l = m_watched.skipNull();
    for (; l; l = l->skipNext()) {
	const String* s = static_cast<const String*>(l->get());
	if (s->null() || (*s == msg))
	    break;
    }
    if (l && m_receiver) {
	lock.drop();
	recv->returnMsg(&msg,"",handled);
    }
    recv->unuse();
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

void MsgWatcher::clear()
{
    Engine::self()->setHook(this,true);
    if (!m_receiver)
	return;
    s_uses.lock();
    ExtModReceiver* recv = m_receiver;
    m_receiver = 0;
    if (recv && (recv->m_watcher == this))
	recv->m_watcher = 0;
    s_uses.unlock();
}


ExtModReceiver* ExtModReceiver::build(const char* script, const char* args, bool ref,
    File* ain, File* aout, ExtModChan* chan)
{
    ExtModReceiver* recv = new ExtModReceiver(script,args,ain,aout,chan);
    if (ref) {
	if (!recv->use())
	    return 0;
	if (recv->start())
	    return recv;
	recv->unuse();
	return 0;
    }
    return recv->start() ? recv : 0;
}

ExtModReceiver* ExtModReceiver::build(const char* name, Stream* io, ExtModChan* chan,
    int role, const char* conn)
{
    ExtModReceiver* recv = new ExtModReceiver(name,io,chan,role,conn);
    return recv->start() ? recv : 0;
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

bool ExtModReceiver::useUnlocked()
{
    if (m_use <= 0)
	return false;
    ++m_use;
    return true;
}

bool ExtModReceiver::use()
{
    s_uses.lock();
    bool ok = (m_use > 0);
    if (ok)
	++m_use;
    s_uses.unlock();
    return ok;
}

bool ExtModReceiver::unuse()
{
    s_uses.lock();
    int u = --m_use;
    s_uses.unlock();
    if (!u)
	destruct();
    return (u <= 0);
}

ExtModReceiver::ExtModReceiver(const char* script, const char* args, File* ain, File* aout, ExtModChan* chan)
    : Mutex(true,"ExtModReceiver"),
      m_role(RoleUnknown), m_dead(false), m_quit(false), m_use(1), m_pid(-1),
      m_in(0), m_out(0), m_ain(ain), m_aout(aout),
      m_chan(chan), m_watcher(0),
      m_selfWatch(false), m_reenter(false), m_setdata(true), m_writing(false),
      m_timeout(s_timeout), m_timebomb(s_timebomb), m_restart(false), m_scripted(false),
      m_buffer(0,DEF_INCOMING_LINE), m_script(script), m_args(args), m_trackName(s_trackName)
{
    Debug(DebugAll,"ExtModReceiver::ExtModReceiver(\"%s\",\"%s\") [%p]",script,args,this);
    m_script.trimBlanks();
    m_args.trimBlanks();
    m_role = chan ? RoleChannel : RoleGlobal;
    s_mutex.lock();
    s_modules.append(this);
    s_mutex.unlock();
}

ExtModReceiver::ExtModReceiver(const char* name, Stream* io, ExtModChan* chan, int role, const char* conn)
    : Mutex(true,"ExtModReceiver"),
      m_role(role), m_dead(false), m_quit(false), m_use(1), m_pid(-1),
      m_in(io), m_out(io), m_ain(0), m_aout(0),
      m_chan(chan), m_watcher(0),
      m_selfWatch(false), m_reenter(false), m_setdata(true), m_writing(false),
      m_timeout(s_timeout), m_timebomb(s_timebomb), m_restart(false), m_scripted(false),
      m_buffer(0,DEF_INCOMING_LINE), m_script(name), m_args(conn), m_trackName(s_trackName)
{
    Debug(DebugAll,"ExtModReceiver::ExtModReceiver(\"%s\",%p,%p) [%p]",name,io,chan,this);
    m_script.trimBlanks();
    m_args.trimBlanks();
    if (chan)
	m_role = RoleChannel;
    s_mutex.lock();
    s_modules.append(this);
    s_mutex.unlock();
}

void ExtModReceiver::destruct()
{
    Debug(DebugAll,"ExtModReceiver::destruct() pid=%d [%p]",m_pid,this);
    Lock lock(this);
    // One destruction is plenty enough
    m_use = -1;
    s_mutex.lock();
    s_modules.remove(this,false);
    s_mutex.unlock();
    die();
    if (m_pid > 1)
	Debug(DebugWarn,"ExtModReceiver::destruct() pid=%d [%p]",m_pid,this);
    closeAudio();
    Stream* tmp = m_in;
    m_in = 0;
    if (tmp == m_out)
	m_out = 0;
    delete tmp;
    tmp = m_out;
    m_out = 0;
    delete tmp;
}

void ExtModReceiver::closeIn()
{
    Stream* tmp = m_in;
    if (tmp)
	tmp->terminate();
}

void ExtModReceiver::closeOut()
{
    Stream* tmp = m_out;
    if (tmp)
	tmp->terminate();
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
	if (!ext->startup()) {
	    // self destruct here since there is no thread to do it later
	    unuse();
	    return false;
	}
	while (m_pid < 0)
	    Thread::yield();
    }
    return (m_pid >= 0);
}


bool ExtModReceiver::flush()
{
    lock();
    MsgWatcher* w = m_watcher;
    m_watcher = 0;
    bool needWait = !!w;
    if (w) {
	w->clear();
	Thread::yield();
	TelEngine::destruct(w);
    }
    // Make sure we release all pending messages and not accept new ones
    needWait = needWait || (m_relays.count() != 0);
    if (s_pluginSafe)
	m_relays.clear();
    else {
	ObjList *p = &m_relays;
	for (; p; p=p->next())
	    p->setDelete(false);
    }
    bool flushed = false;
    if (m_waiting.get()) {
	Debug(DebugInfo,"ExtModReceiver releasing %u pending messages [%p]",
	    m_waiting.count(),this);
	m_waiting.clear();
	needWait = flushed = true;
    }
    unlock();
    if (needWait && s_pluginSafe) {
	int ms = s_waitFlush;
	// During shutdown longer delays are not acceptable
	if ((ms > WAIT_FLUSH) && Engine::exiting())
	    ms = WAIT_FLUSH;
	DDebug(DebugAll,"ExtModReceiver sleeping %d ms [%p]",ms,this);
	Thread::msleep(ms);
    }
    return flushed;
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
    Lock mylock(this);
    if (m_dead)
	return;
    m_dead = true;
    m_quit = true;
    use();

    RefPointer<ExtModChan> chan = m_chan;
    m_chan = 0;
    if (chan)
	chan->setRecv(0);
    mylock.drop();

    if (m_scripted && (m_role == RoleGlobal))
	Output("Unloading external module '%s' '%s'",m_script.c_str(),m_args.safe());
    // Give the external script a chance to die gracefully
    closeOut();
    if (m_pid > 1) {
	Debug(DebugAll,"ExtModReceiver::die() waiting for pid=%d to die [%p]",m_pid,this);
	for (int i=0; i<100; i++) {
	    Thread::yield();
	    if (m_pid <= 0)
		break;
	}
    }
    if (m_pid > 1)
	Debug(DebugInfo,"ExtModReceiver::die() pid=%d did not exit? [%p]",m_pid,this);

    // Close the stdout pipe before terminating the process
    closeIn();
    // Release relays and messages since no confirmation can be received anymore
    flush();
#ifndef _WINDOWS
    if (m_pid > 1)
	::kill(m_pid,SIGTERM);
#endif
    if (chan && clearChan)
	chan->disconnect(m_reason);
    if (m_restart && !Engine::exiting()) {
	Debug(DebugMild,"Restarting external '%s' '%s'",m_script.safe(),m_args.safe());
	ExtModReceiver::build(m_script,m_args);
    }
    unuse();
}

bool ExtModReceiver::received(Message &msg, int id)
{
    if (m_dead || m_quit)
	return false;
    lock();
    // check if we are no longer running
    bool ok = (m_pid > 0) && !m_dead && m_in && m_in->valid() && m_out && m_out->valid();
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
	DDebug(DebugAll,"ExtMod queued message %p '%s' [%p]",&msg,msg.c_str(),this);
    }
    else {
	Debug(DebugWarn,"ExtMod could not queue message %p '%s' [%p]",&msg,msg.c_str(),this);
	ok = false;
	fail = true;
    }
    unlock();
    // would be nice to lock the MsgHolder and wait for it to unlock from some
    //  other thread - unfortunately this does not work with all mutexes
    // sorry, Maciek - have to do it work in Windows too :-(
    while (ok) {
	h.lock(Thread::idleUsec());
	lock();
	ok = (m_waiting.find(&h) != 0);
	if (ok && tout && (Time::now() > tout)) {
	    Alarm("extmodule","performance",DebugWarn,"Message %p '%s' did not return in %d msec [%p]",
		&msg,msg.c_str(),m_timeout,this);
	    m_waiting.remove(&h,false);
	    ok = false;
	    fail = true;
	}
	unlock();
    }
    DDebug(DebugAll,"ExtMod message %p '%s' returning %s [%p]",
	&msg,msg.c_str(),String::boolText(h.m_ret),this);
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
    adjustPath(tmp);
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
    if (m_role == RoleGlobal)
	Output("Loading external module '%s' '%s'",m_script.c_str(),args);
    else
	Debug(DebugInfo,"Launched External Script '%s' '%s'",script,args);
    m_in = new File(ext2yate[0]);
    m_out = new File(yate2ext[1]);

    // close what we're not using in the parent
    close(ext2yate[1]);
    close(yate2ext[0]);
    closeAudio();
    m_scripted = true;
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
    if (m_in && !m_in->setBlocking(false))
	Debug("ExtModule",DebugWarn,"Failed to set nonblocking mode, expect trouble [%p]",this);
    int posinbuf = 0;
    bool invalid = true;
    DDebug(DebugAll,"ExtModReceiver::run() entering loop [%p]",this);
    for (;;) {
	use();
	lock();
	char* buffer = static_cast<char*>(m_buffer.data());
	int readsize = m_in ? m_in->readData(buffer+posinbuf,m_buffer.length()-posinbuf) : 0;
	unlock();
	if (unuse())
	    return;
	if (!readsize) {
	    if (m_in)
		Debug("ExtModule",DebugInfo,"Read EOF on %p [%p]",m_in,this);
	    closeIn();
	    flush();
	    if (invalid)
		Debug("ExtModule",DebugWarn,"Never got anything valid from terminated '%s' '%s'",
		    m_script.c_str(),m_args.safe());
	    if (m_chan && m_chan->running())
		Thread::sleep(1);
	    break;
	}
	else if (readsize < 0) {
	    Lock mylock(this);
	    if (m_in && m_in->canRetry()) {
		mylock.drop();
		Thread::idle();
		continue;
	    }
	    if (!m_quit)
		Debug("ExtModule",DebugWarn,"Read error %d on %p [%p]",errno,m_in,this);
	    break;
	}
	XDebug(DebugAll,"ExtModReceiver::run() read %d",readsize);
	int totalsize = readsize + posinbuf;
	if (totalsize >= (int)m_buffer.length()) {
	    Debug("ExtModule",DebugWarn,"Overflow reading in buffer of length %u, closing [%p]",
		m_buffer.length(),this);
	    return;
	}
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
	    readsize = eoline-buffer+1;
	    if (buffer[0]) {
		invalid = invalid && (buffer[0] != '%' || buffer[1] != '%');
		use();
		bool goOut = processLine(buffer);
		if (unuse() || goOut)
		    return;
		if (totalsize >= (int)m_buffer.length()) {
		    Debug("ExtModule",DebugWarn,"Lost data shrinking read buffer to %u, closing [%p]",
			m_buffer.length(),this);
		    return;
		}
	    }
	    totalsize -= readsize;
	    buffer = static_cast<char*>(m_buffer.data());
	    ::memmove(buffer,buffer+readsize,totalsize+1);
	}
	posinbuf = totalsize;
    }
}

bool ExtModReceiver::outputLine(const char* line)
{
    if (TelEngine::null(line))
	return true;
    int len = ::strlen(line);
    if (m_dead || !m_out || !m_out->valid() || !use())
	return false;
    uint64_t tout = (m_timeout > 0) ? (Time::now() + 1000 * (uint64_t)m_timeout) : 0;
    for (;;) {
	Lock mylock(this);
	if (m_dead || !m_out || !m_out->valid()) {
	    unuse();
	    return false;
	}
	if (!m_writing) {
	    m_writing = true;
	    break;
	}
	if (tout && tout < Time::now()) {
	    if (!m_quit)
		Alarm("extmodule","performance",DebugWarn,"Timeout %d msec for %d characters [%p]",
		    m_timeout,len,this);
	    unuse();
	    return false;
	}
	mylock.drop();
	Thread::idle();
    }
    bool ok = outputLineInternal(line,len);
    m_writing = false;
    unuse();
    return ok;
}

bool ExtModReceiver::outputLineInternal(const char* line, int len)
{
    DDebug("ExtModReceiver",DebugAll,"outputLine len=%d '%s' [%p]",len,line,this);
    // since m_out can be non-blocking (the socket) we have to loop
    while (m_out && m_out->valid() && (len > 0) && !m_dead) {
	int w = m_out->writeData(line,len);
	if (w < 0) {
	    if (m_dead || !m_out || !m_out->canRetry())
		return false;
	}
	else {
	    line += w;
	    len -= w;
	}
	if (len > 0)
	    Thread::idle();
    }
    char nl = '\n';
    for (;;) {
	if (m_dead || !m_out)
	    return false;
	int w = m_out->writeData(&nl,1);
	if ((w < 0) && m_out && m_out->canRetry())
	    w = 0;
	if (w > 0)
	    return true;
	if (w < 0)
	    return false;
	Thread::idle();
    }
}

void ExtModReceiver::reportError(const char* line)
{
    Debug("ExtModReceiver",DebugWarn,"Error: '%s'", line);
    outputLine("Error in: " + String(line));
}

void ExtModReceiver::returnMsg(const Message* msg, const char* id, bool accepted)
{
    String ret(msg->encode(accepted,id));
    if (!outputLine(ret) && m_timebomb)
	die();
}

bool ExtModReceiver::addWatched(const String& name)
{
    Lock mylock(this);
    if (m_dead)
	return false;
    if (!m_watcher) {
	m_watcher = new MsgWatcher(this);
	Engine::self()->setHook(m_watcher);
    }
    return m_watcher->addWatched(name);
}

bool ExtModReceiver::delWatched(const String& name)
{
    Lock mylock(this);
    if (m_dead)
	return false;
    return m_watcher && m_watcher->delWatched(name);
}

bool ExtModReceiver::processLine(const char* line)
{
    if (m_dead)
	return false;
    if (m_quit)
	return true;
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
		msg->unlock();
		p->remove(false);
		return false;
	    }
	}
	Debug("ExtModReceiver",(m_dead ? DebugInfo : DebugWarn),
	    "Unmatched%s message: %s [%p]",(m_dead ? " dead" : ""),line,this);
	return false;
    }
    else if (id.startSkip("%%>install:",false)) {
	int prio = 100;
	id >> prio >> ":";
	String fname;
	String fvalue;
	static const Regexp r("^\\([^:]*\\):\\([^:]*\\):\\?\\(.*\\)");
	if (id.matches(r)) {
	    // a filter is specified
	    fname = id.matchString(2);
	    fvalue = id.matchString(3);
	    id = id.matchString(1);
	}
	// sanity checks
	lock();
	bool ok = id && !m_dead && !m_relays.find(id);
	if (ok) {
	    MessageRelay *r = new MessageRelay(id,this,0,prio,m_trackName);
	    if (fname)
		r->setFilter(fname,fvalue);
	    m_relays.append(r);
	    Engine::install(r);
	}
	unlock();
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
	lock();
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
	unlock();
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
	    val.trimBlanks();
	    id = id.substr(0,col);
	    bool ok = false;
	    Lock mylock(this);
	    if (m_dead)
		return false;
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
	    else if (id == "trackparam") {
		if (val.null())
		    val = m_trackName;
		else
		    m_trackName = val;
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
	    else if (id == "bufsize") {
		unsigned int len = val.toInteger(m_buffer.length(),0,
		    MIN_INCOMING_LINE,MAX_INCOMING_LINE);
		if (len > m_buffer.length())
		    m_buffer.append(DataBlock(0,len - m_buffer.length()));
		else if (len < m_buffer.length())
		    m_buffer.assign(m_buffer.data(),len);
		val = m_buffer.length();
		ok = true;
	    }
	    else if (id == "restart") {
		m_restart = m_scripted && (RoleGlobal == m_role) && val.toBoolean(m_restart);
		val = m_restart;
		ok = true;
	    }
	    else if (id == "reenter") {
		m_reenter = val.toBoolean(m_reenter);
		val = m_reenter;
		ok = true;
	    }
	    else if (id == "setdata") {
		m_setdata = val.toBoolean(m_setdata);
		val = m_setdata;
		ok = true;
	    }
	    else if (id == "selfwatch") {
		m_selfWatch = val.toBoolean(m_selfWatch);
		val = m_selfWatch;
		ok = true;
	    }
	    else if (id.startsWith("engine.")) {
		// keep the index in substr in sync with length of "engine."
		const NamedString* param = Engine::runParams().getParam(id.substr(7));
		ok = val.null() && param;
		val = param;
	    }
	    else if (id.startsWith("config.")) {
		ok = val.null();
		// keep the index in substr in sync with length of "config."
		val = id.substr(7);
		int sep = val.find('.');
		if (sep > 0) {
		    const NamedString* key = Engine::config().getKey(val.substr(0,sep).trimBlanks(),
			val.substr(sep+1).trimBlanks());
		    if (key)
			val = *key;
		    else {
			val.clear();
			ok = false;
		    }
		}
		else {
		    ok = (Engine::config().getSection(val) != 0);
		    val.clear();
		}
	    }
	    else if (id == "runid") {
		ok = val.null();
		val = Engine::runId();
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
	m_quit = true;
	outputLine("%%<quit");
	return true;
    }
    else {
	ExtMessage* m = new ExtMessage;
	if (m->decode(line) == -2) {
	    DDebug("ExtModReceiver",DebugAll,"Created message %p '%s' [%p]",m,m->c_str(),this);
	    lock();
	    bool note = true;
	    while (!m_dead && m_chan && m_chan->waiting()) {
		if (note) {
		    note = false;
		    Debug("ExtModReceiver",DebugNote,"Waiting before enqueueing new message %p '%s' [%p]",
			m,m->c_str(),this);
		}
		unlock();
		Thread::yield();
		if (m_dead) {
		    m->destruct();
		    return false;
		}
		lock();
	    }
	    ExtModChan* chan = 0;
	    if ((m_role == RoleChannel) && !m_chan && m_setdata && (*m == "call.execute")) {
		// we delayed channel creation as there was nothing to ref() it
		chan = new ExtModChan(this);
		m_chan = chan;
		m->setParam("id",chan->id());
	    }
	    if (m_setdata)
		m->userData(m_chan);
	    // now the newly created channel is referenced by the message
	    if (chan)
		chan->deref();
	    id = m->id();
	    if (id && !chan) {
		// Copy the user data pointer from waiting message with same id
		ObjList *p = &m_waiting;
		for (; p; p=p->next()) {
		    MsgHolder *h = static_cast<MsgHolder *>(p->get());
		    if (h && (h->m_id == id)) {
			RefObject* ud = h->m_msg.userData();
			Debug("ExtModReceiver",DebugAll,"Copying data pointer %p from %p '%s' [%p]",
			    ud,h->msg(),h->msg()->c_str(),this);
			m->userData(ud);
			break;
		    }
		}
	    }
	    m->startup(this);
	    unlock();
	    return false;
	}
	m->destruct();
    }
    reportError(line);
    return false;
}

void ExtModReceiver::describe(String& rval) const
{
    rval << "\t";
    switch (m_role) {
	case RoleUnknown:
	    rval << "Unknown";
	    break;
	case RoleGlobal:
	    rval << "Global";
	    break;
	case RoleChannel:
	    rval << "Channel";
	    break;
	default:
	    rval << "Invalid";
	    break;
    }
    if (m_dead)
	rval << ", dead, use=" << m_use;
    if (m_chan)
	rval << ", has channel";
    if (m_restart)
	rval << ", autorestart";
    if (m_pid > 0)
	rval << ", pid=" << m_pid;
    rval << "\r\n";
}


bool ExtModHandler::received(Message& msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    static const Regexp r("^external/\\([^/]*\\)/\\([^ ]*\\)\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
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
						  dest.matchString(3).trimBlanks().c_str(),
						  true);
	if (!r)
	    return false;
	bool ok = r->received(msg,1);
	r->unuse();
	return ok;
    }
    ExtModChan *em = ExtModChan::build(dest.matchString(2).c_str(),
				       dest.matchString(3).c_str(),typ);
    if (!em) {
	Debug(DebugGoOn,"Failed to create ExtMod for '%s'",dest.matchString(2).c_str());
	return false;
    }
    ExtModReceiver* recv = em->receiver();
    // new messages must be blocked until connect() returns (if applicable)
    if (ch)
	em->waitMsg(&msg);
    if (!(recv && recv->received(msg,1))) {
	em->waitMsg(0);
	int level = DebugWarn;
	if (msg.getValue("error") || msg.getValue("reason"))
	    level = DebugNote;
	Debug(level,"ExtMod '%s' did not handle call message",dest.matchString(2).c_str());
	em->waiting(false);
	if (recv)
	    recv->unuse();
	em->deref();
	return false;
    }
    recv->unuse();
    if (ch) {
	em->waitMsg(0);
	ch->connect(em,msg.getValue("reason"));
	em->waiting(false);
    }
    em->deref();
    return true;
}


bool ExtModCommand::received(Message& msg)
{
    String line(msg.getValue("line"));
    if (!line.startsWith("external",true))
	return complete(msg.getValue("partline"),msg.getValue("partword"),msg.retValue());;
    line >> "external";
    line.trimBlanks();
    if (line.null() || line == "info") {
	msg.retValue() = "";
	int n = 0;
	Lock lock(s_mutex);
	ObjList *l = &s_modules;
	for (; l; l=l->next()) {
	    ExtModReceiver *r = static_cast<ExtModReceiver *>(l->get());
	    if (r) {
		msg.retValue() << ++n << ". " << r->scriptFile() << " " << r->commandArg() << "\r\n";
		if (line)
		    r->describe(msg.retValue());
	    }
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
    else if (line.startSkip("execute")) {
	if (line.null())
	    return false;
	blank = line.find(' ');
	String exe = line.substr(0,blank);
	adjustPath(exe);
	if (blank >= 0)
	    line = line.substr(blank+1);
	else
	    line.clear();
	bool ok = runProgram(exe,line);
	msg.retValue() = ok ? "External exec attempt\r\n" : "External exec failed\r\n";
	return true;
    }
    ExtModReceiver *r = ExtModReceiver::build(line.substr(0,blank),
	(blank >= 0) ? line.substr(blank+1).c_str() : (const char*)0);
    msg.retValue() = r ? "External start attempt\r\n" : "External command failed\r\n";
    return true;
}

bool ExtModCommand::complete(const String& partLine, const String& partWord, String& rval) const
{
    if (partLine.null() && partWord.null())
	return false;
    if (partLine.null() || partLine == YSTRING("status") || partLine == YSTRING("help"))
	Module::itemComplete(rval,"external",partWord);
    else if (partLine == YSTRING("external")) {
	for (const char** list = s_cmds; *list; list++)
	    Module::itemComplete(rval,*list,partWord);
	return true;
    }
    else if (partLine == YSTRING("external restart") || partLine == YSTRING("external stop")) {
	ObjList mod;
	s_mutex.lock();
	ObjList *l = &s_modules;
	for (; l; l=l->next()) {
	    ExtModReceiver *r = static_cast<ExtModReceiver *>(l->get());
	    if (!r)
		continue;
	    if (mod.find(r->scriptFile()))
		continue;
	    mod.append(new String(r->scriptFile()));
	}
	s_mutex.unlock();
	for (l = mod.skipNull(); l; l = l->skipNext())
	    Module::itemComplete(rval,l->get()->toString(),partWord);
    }
    return false;
}


bool ExtModStatus::received(Message& msg)
{
    const String& dest = msg[YSTRING("module")];
    if (dest && (dest != YSTRING("external")))
	return false;
    s_mutex.lock();
    msg.retValue() << "name=" << __plugin.name()
	<< ",type=misc;scripts=" << s_modules.count()
	<< ",chans=" << s_chans.count() << "\r\n";
    s_mutex.unlock();
    return !dest.null();
}


bool ExtModHelp::received(Message& msg)
{
    const String& line = msg[YSTRING("line")];
    if (line && (line != YSTRING("external")))
	return false;
    msg.retValue() << "  " << s_helpExternalCmd << "\r\n";
    if (line)
	msg.retValue() << s_helpExternalInfo << "\r\n";
    return !line.null();
}


ExtListener::ExtListener(const char* name)
    : Thread("ExtMod Listener"),
      m_name(name), m_role(ExtModReceiver::RoleUnknown)
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
	Debug(DebugConf,"Unknown role '%s' of listener '%s'",role.c_str(),m_name.c_str());
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
	Debug(DebugConf,"Unknown type '%s' of listener '%s'",type.c_str(),m_name.c_str());
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
	Thread::idle(true);
	Socket* skt = m_socket.accept(addr);
	if (!skt) {
	    if (m_socket.canRetry())
		continue;
	    Alarm("extmodule","socket",DebugWarn,"Error on accept(), shutting down ExtListener '%s'",m_name.c_str());
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
		ExtModReceiver::build(m_name,skt,0,m_role,tmp);
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
	Alarm("extmodule","config",DebugWarn,"Could not start listener '%s'",name);
	delete ext;
	ext = 0;
    }
    return ext;
}


ExtModulePlugin::ExtModulePlugin()
    : Plugin("extmodule"),
      m_handler(0)
{
    Output("Loaded module ExtModule");
}

ExtModulePlugin::~ExtModulePlugin()
{
    Output("Unloading module ExtModule");
    s_mutex.lock();
    s_pluginSafe = false;
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
    s_trackName = s_cfg.getBoolValue("general","trackparam",false) ?
	name().c_str() : (const char*)0;
    int wf = s_cfg.getIntValue("general","waitflush",WAIT_FLUSH);
    if (wf < 1)
	wf = 1;
    else if (wf > 100)
	wf = 100;
    s_waitFlush = wf;
    if (!m_handler) {
	m_handler = new ExtModHandler("call.execute",s_cfg.getIntValue("general","priority",100));
	Engine::install(m_handler);
	Engine::install(new ExtModCommand);
	Engine::install(new ExtModStatus);
	Engine::install(new ExtModHelp);
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
		if (n) {
		    String arg = *n;
		    Engine::runParams().replaceParams(arg);
		    ExtModReceiver::build(n->name(),arg);
		}
	    }
	}
	// and now start additional programs
	sect = s_cfg.getSection("execute");
	if (sect) {
	    unsigned int len = sect->length();
	    for (unsigned int i=0; i<len; i++) {
		NamedString *n = sect->getParam(i);
		if (n) {
		    String tmp = n->name();
		    String arg = *n;
		    adjustPath(tmp);
		    Engine::runParams().replaceParams(arg);
		    if (tmp)
			runProgram(tmp,arg);
		}
	    }
	}
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
