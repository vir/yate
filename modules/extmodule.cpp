/**
 * extmodule.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Some parts of this code have been stolen shamelessly from app_agi.
 * I think that AGI is great idea.
 * 
 * External module handler
 */

#include <telengine.h>
#include <telephony.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>


using namespace TelEngine;

static Configuration s_cfg;
static ObjList s_chans;
static ObjList s_modules;

class ExtModReceiver;
class ExtModChan;

class ExtModSource : public ThreadedSource
{
public:
    ExtModSource(int fd, ExtModChan* chan);
    ~ExtModSource();
    virtual void run();
private:
    int m_fd;
    unsigned m_brate;
    unsigned m_total;
    ExtModChan* m_chan;
};

class ExtModConsumer : public DataConsumer
{
public:
    ExtModConsumer(int fd);
    ~ExtModConsumer();
    virtual void Consume(const DataBlock &data);
private:
    int m_fd;
    unsigned m_total;
};

class ExtModChan : public DataEndpoint
{
public:
    enum {
	NoChannel,
	DataNone,
	DataRead,
	DataWrite,
	DataBoth
    };
    static ExtModChan* build(const char *file, const char *args, int type);
    ~ExtModChan();
    virtual void disconnected();
    inline ExtModReceiver* receiver() const
	{ return m_recv; }
    inline void setRecv(ExtModReceiver* recv)
	{ m_recv = recv; }
    inline void setRunning(bool running)
	{ m_running = running; }
    inline bool running() const
	{ return m_running; }
private:
    ExtModChan(const char *file, const char *args, int type);
    ExtModReceiver *m_recv;
    int m_type;
    bool m_running;
};

class MsgHolder : public GenObject
{
public:
    MsgHolder(Message &msg);
    Message &m_msg;
    bool m_ret;
    String m_id;
    bool decode(const char *s);
};

class ExtModReceiver : public MessageReceiver
{
public:
    static ExtModReceiver* build(const char *script, const char *args,
	int ain = -1, int aout = -1, ExtModChan *chan = 0);
    ~ExtModReceiver();
    virtual bool received(Message &msg, int id);
    void processLine(const char *line);
    bool outputLine(const char *line);
    void reportError(const char *line);
    bool start();
    void run();
    void cleanup();
    void die(bool clearChan = true);
private:
    ExtModReceiver(const char *script, const char *args,
	int ain = -1, int aout = -1, ExtModChan *chan = 0);
    bool create(const char *script, const char *args);
    bool m_dead;
    pid_t m_pid;
    int m_in, m_out, m_ain, m_aout;
    ExtModChan *m_chan;
    String m_script, m_args;
    ObjList m_waiting;
    ObjList m_relays;
};

class ExtThread : public Thread
{
public:
    ExtThread(ExtModReceiver *receiver) : Thread("ExtModule"), m_receiver(receiver)
	{ }
    virtual void run()
	{ m_receiver->run(); }
    virtual void cleanup()
	{ m_receiver->cleanup(); }
private:
    ExtModReceiver *m_receiver;
};

class ExtModHandler : public MessageHandler
{
public:
    ExtModHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class ExtModulePlugin : public Plugin
{
public:
    ExtModulePlugin();
    ~ExtModulePlugin();
    virtual void initialize();
private:
    ExtModHandler *m_handler;
};


ExtModSource::ExtModSource(int fd, ExtModChan* chan)
    : m_fd(fd), m_brate(16000), m_total(0), m_chan(chan)
{
    Debug(DebugAll,"ExtModSource::ExtModSource(%d) [%p]",fd,this);
    if (m_fd >= 0) {
	chan->setRunning(true);
	start("ExtModSource");
    }
}

ExtModSource::~ExtModSource()
{
    Debug(DebugAll,"ExtModSource::~ExtModSource() [%p] total=%u",this,m_total);
    m_chan->setRunning(false);
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
}

void ExtModSource::run()
{
    DataBlock data(0,480);
    int r = 0;
    unsigned long long tpos = Time::now();
    do {
	r = ::read(m_fd,data.data(),data.length());
	if (r < 0) {
	    if (errno == EINTR) {
		r = 1;
		continue;
	    }
	    break;
	}
	if (r < (int)data.length())
	    data.assign(data.data(),r);
	long long dly = tpos - Time::now();
	if (dly > 0) {
#ifdef DEBUG
	    Debug("ExtModSource",DebugAll,"Sleeping for %lld usec",dly);
#endif
	    ::usleep((unsigned long)dly);
	}
	Forward(data);
	m_total += r;
	tpos += (r*1000000ULL/m_brate);
    } while (r > 0);
    Debug(DebugAll,"ExtModSource [%p] end of data total=%u",this,m_total);
    m_chan->setRunning(false);
}

ExtModConsumer::ExtModConsumer(int fd)
    : m_fd(fd), m_total(0)
{
    Debug(DebugAll,"ExtModConsumer::ExtModConsumer(%d) [%p]",fd,this);
}

ExtModConsumer::~ExtModConsumer()
{
    Debug(DebugAll,"ExtModConsumer::~ExtModConsumer() [%p] total=%u",this,m_total);
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
}

void ExtModConsumer::Consume(const DataBlock &data)
{
    if ((m_fd >= 0) && !data.null()) {
	::write(m_fd,data.data(),data.length());
	m_total += data.length();
    }
}

ExtModChan* ExtModChan::build(const char *file, const char *args, int type)
{
    ExtModChan* chan = new ExtModChan(file,args,type);
    if (!chan->m_recv) {
	chan->destruct();
	return 0;
    }
    return chan;
}

ExtModChan::ExtModChan(const char *file, const char *args, int type)
    : DataEndpoint("ExtModule"), m_recv(0), m_type(type)
{
    Debug(DebugAll,"ExtModChan::ExtModChan(%d) [%p]",type,this);
    int wfifo[2] = { -1, -1 };
    int rfifo[2] = { -1, -1 };
    switch (m_type) {
	case DataWrite:
	case DataBoth:
	    ::pipe(wfifo);
	    setConsumer(new ExtModConsumer(wfifo[1]));
	    getConsumer()->deref();
    }
    switch (m_type) {
	case DataRead:
	case DataBoth:
	    ::pipe(rfifo);
	    setSource(new ExtModSource(rfifo[0],this));
	    getSource()->deref();
    }
    s_chans.append(this);
    m_recv = ExtModReceiver::build(file,args,wfifo[0],rfifo[1],this);
}

ExtModChan::~ExtModChan()
{
    Debug(DebugAll,"ExtModChan::~ExtModChan() [%p]",this);
    s_chans.remove(this,false);
    if (m_recv)
	m_recv->die(false);
}

void ExtModChan::disconnected()
{
    Debugger debug("ExtModChan::disconnected()"," [%p]",this);
}

MsgHolder::MsgHolder(Message &msg)
    : m_msg(msg), m_ret(false)
{
    // the address of this object should be unique
    m_id = (int)this;
}

bool MsgHolder::decode(const char *s)
{
    return (m_msg.decode(s,m_ret,m_id) == -2);
}

ExtModReceiver* ExtModReceiver::build(const char *script, const char *args,
				      int ain, int aout, ExtModChan *chan)
{
    ExtModReceiver* recv = new ExtModReceiver(script,args,ain,aout,chan);
    if (!recv->start()) {
	recv->destruct();
	return 0;
    }
    return recv;
}

ExtModReceiver::ExtModReceiver(const char *script, const char *args, int ain, int aout, ExtModChan *chan)
    : m_dead(false), m_pid(-1), m_in(-1), m_out(-1), m_ain(ain), m_aout(aout),
      m_chan(chan), m_script(script), m_args(args)
{
    Debug(DebugAll,"ExtModReceiver::ExtModReceiver(\"%s\",\"%s\") [%p]",script,args,this);
    s_modules.append(this);
}


ExtModReceiver::~ExtModReceiver()
{   
    Debug(DebugAll,"ExtModReceiver::~ExtModReceiver() [%p] pid=%d",this,m_pid);
    s_modules.remove(this,false);
    die();
    if (m_pid > 0)
	Debug(DebugWarn,"ExtModReceiver::~ExtModReceiver() [%p] pid=%d",this,m_pid);
}

bool ExtModReceiver::start()
{
    if (m_pid < 0) {
	new ExtThread(this);
	while (m_pid < 0)
	    Thread::yield();
    }
    return (m_pid > 0);
}

void ExtModReceiver::die(bool clearChan)
{
#ifdef DEBUG
    Debugger debug(DebugAll,"ExtModReceiver::die()"," [%p]",this);
#endif
    if (m_dead)
	return;
    Debug(DebugAll,"ExtModReceiver::die() pid=%d",m_pid);
    m_dead = true;
    /* Give the external script a chance to die gracefully */
    if (m_out != -1) {
	::close(m_out);
	m_out = -1;
    }
    if (m_pid > 0) {
	Debug(DebugAll,"ExtModReceiver::die() waiting for pid=%d to die",m_pid);
	for (int i=0; i<100; i++) {
	    Thread::yield();
	    if (m_pid <= 0)
		break;
	}
    }
    if (m_pid > 0)
	Debug(DebugInfo,"ExtModReceiver::die() pid=%d did not exit?",m_pid);
    /* Make sure we release all pending messages and not accept new ones */
    if (!Engine::exiting())
	m_relays.clear();
    else {
	ObjList *p = &m_relays;
	for (; p; p=p->next())
	    p->setDelete(false);
    }
    if (m_waiting.get()) {
	m_waiting.clear();
	Thread::yield();
    }
    /* Now terminate the process and close its stdout pipe */
    if (m_pid > 0)
	::kill(m_pid,SIGTERM);
    if (m_in != -1) {
	::close(m_in);
	m_in = -1;
    }
    if (m_chan) {
	m_chan->setRecv(0);
	if (clearChan)
	    m_chan->disconnect();
	m_chan = 0;
    }
}

bool ExtModReceiver::received(Message &msg, int id)
{
    if (m_pid <= 0)
	return false;
    /* Check if the message was generated by ourselves - avoid reentrance */
    if (m_waiting.find(&msg))
	return false;

    MsgHolder h(msg);
    m_waiting.append(&h)->setDelete(false);
#ifdef DEBUG
    Debug(DebugAll,"ExtMod [%p] queued message '%s' [%p]",this,msg.c_str(),&msg);
#endif
    /* We use id to signal a call directly from the "call" message handler */
    outputLine(msg.encode(h.m_id));
    while (m_waiting.find(&h))
	Thread::yield();
#ifdef DEBUG
    Debug(DebugAll,"ExtMod [%p] message '%s' [%p] returning %s",this,msg.c_str(),&msg, h.m_ret ? "true" : "false");
#endif
    return h.m_ret;
}

bool ExtModReceiver::create(const char *script, const char *args)
{
    String tmp(script);
    int pid;
    int ext2yate[2];
    int yate2ext[2];
    int x;
    if (script[0] != '/') {
	tmp = s_cfg.getValue("general","scripts_dir","scripts/") + tmp;
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
	/* In child - terminate all other threads if needed */
	Thread::preExec();
	/* Try to immunize child from ^C and ^\ */
	::signal(SIGINT,SIG_IGN);
	::signal(SIGQUIT,SIG_IGN);
	/* And restore default handlers for other signals */
	::signal(SIGTERM,SIG_DFL);
	::signal(SIGHUP,SIG_DFL);
	/* Redirect stdin and out */
	::dup2(yate2ext[0], STDIN_FILENO);
	::dup2(ext2yate[1], STDOUT_FILENO);
	/* Set audio in/out handlers */
	if (m_ain != -1)
	    ::dup2(m_ain, STDERR_FILENO+1);
	else
	    ::close(STDERR_FILENO+1);
	if (m_aout != -1)
	    ::dup2(m_aout, STDERR_FILENO+2);
	else
	    ::close(STDERR_FILENO+2);
	/* Close everything but stdin/out/err/audio */
	for (x=STDERR_FILENO+3;x<1024;x++) 
	    ::close(x);
	/* Execute script */
	if (debugAt(DebugInfo))
	    ::fprintf(stderr, "Execing '%s' '%s'\n", script, args);
        ::execl(script, script, args, (char *)NULL);
	::fprintf(stderr, "Failed to execute '%s': %s\n", script, strerror(errno));
	/* Shit happened. Die as quick and brutal as possible */
	::_exit(1);
    }
    Debug(DebugInfo,"Launched External Script %s", script);
    m_in = ext2yate[0];
    m_out = yate2ext[1];

    /* close what we're not using in the parent */
    close(ext2yate[1]);
    close(yate2ext[0]);
    m_pid = pid;
    return true;
}

void ExtModReceiver::cleanup()
{
#ifdef DEBUG
    Debugger debug(DebugAll,"ExtModReceiver::cleanup()"," [%p]",this);
#endif
    /* We must call waitpid from here - same thread we started the child */
    if (m_pid > 0) {
	/* No thread switching if possible */
	if (m_out != -1) {
	    ::close(m_out);
	    m_out = -1;
	}
	Thread::yield();
	int w = ::waitpid(m_pid, 0, WNOHANG);
	if (w == 0) {
	    Debug(DebugWarn, "Process %d has not exited on closing stdin - we'll kill it",m_pid);
	    ::kill(m_pid,SIGTERM);
	    Thread::yield();
	    w = ::waitpid(m_pid, 0, WNOHANG);
	}
	if (w == 0)
	    Debug(DebugWarn, "Process %d has still not exited yet?",m_pid);
	else if (w < 0)
	    Debug(DebugMild, "Failed waitpid on %d: %s",m_pid,strerror(errno));
	m_pid = 0;
    }
    destruct();
}

void ExtModReceiver::run()
{
    if (!create(m_script.safe(),m_args.safe())) {
	m_pid = 0;
	return;
    }
    char buffer[1024];
    int posinbuf = 0;
#ifdef DEBUG
    Debug(DebugAll,"ExtModReceiver::run() entering loop [%p]",this);
#endif
    for (;;) {
	int readsize = (m_in >= 0) ? ::read(m_in,buffer+posinbuf,sizeof(buffer)-posinbuf-1) : 0;
#ifdef DEBUG
	Debug(DebugAll,"ExtModReceiver::run() read %d",readsize);
#endif
	if (!readsize) {
	    Debug("ExtModule",DebugInfo,"Read EOF on %d [%p]",m_in,this);
	    if (m_chan && m_chan->running())
		::usleep(1000000);
	    break;
	}
	else if (readsize < 0) {
	    Debug("ExtModule",DebugWarn,"Read error %d on %d [%p]",errno,m_in,this);
	    break;
	}
	int totalsize = readsize + posinbuf;
	buffer[totalsize]=0;
	for (;;) {
	    char *eoline = ::strchr(buffer,'\n');
	    if (!eoline && ((int)::strlen(buffer) < totalsize))
		eoline=buffer+::strlen(buffer);
	    if (!eoline)
		break;
	    *eoline=0;
	    if (buffer[0])
		processLine(buffer);
	    totalsize -= eoline-buffer+1;
	    ::memmove(buffer,eoline+1,totalsize+1);
	}
	posinbuf = totalsize;
    }
}

bool ExtModReceiver::outputLine(const char *line)
{
#ifdef DEBUG
    Debug("ExtModReceiver",DebugAll,"outputLine '%s'", line);
#endif
    if (m_out < 0)
	return false;
    ::write(m_out,line,::strlen(line));
    char nl = '\n';
    ::write(m_out,&nl,sizeof(nl));
    return true;
}

void ExtModReceiver::reportError(const char *line)
{
    Debug("ExtModReceiver",DebugWarn,"Error: '%s'", line);
    outputLine("Error in: " + String(line));
}

static bool startSkip(String &s, const char *keyword)
{
    if (s.startsWith(keyword,false)) {
        s >> keyword;
        return true;
    }
    return false;
}

void ExtModReceiver::processLine(const char *line)
{
#ifdef DEBUG
    Debug("ExtModReceiver",DebugAll,"processLine '%s'", line);
#endif
    ObjList *p = &m_waiting;
    for (; p; p=p->next()) {
	MsgHolder *msg = static_cast<MsgHolder *>(p->get());
	if (msg && msg->decode(line)) {
#ifdef DEBUG
	    Debug("ExtModReceiver",DebugInfo,"Matched message");
#endif
	    p->remove(false);
	    return;
	}
    }
    String id(line);
    if (startSkip(id,"%%>install:")) {
	int prio = 100;
	id >> prio >> ":";
	bool ok = true;
	ObjList *p = &m_relays;
	for (; p; p=p->next()) {
	    MessageRelay *r = static_cast<MessageRelay *>(p->get());
	    if (r && (*r == id)) {
		ok = false;
		break;
	    }
	}
	if (ok) {
	    MessageRelay *r = new MessageRelay(id,this,0,prio);
	    m_relays.append(r);
	    Engine::install(r);
	}
	Debug("ExtModReceiver",DebugAll,"Install '%s', prio %d %s", id.c_str(),prio,ok ? "ok" : "failed");
	String out("%%<install:");
	out << prio << ":" << id << ":" << ok;
	outputLine(out);
	return;
    }
    else if (startSkip(id,"%%>uninstall:")) {
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
	return;
    }
    else {
	Message m("");
	if (m.decode(line,id) == -2) {
#ifdef DEBUG
	    Debug("ExtModReceiver",DebugAll,"Created message [%p]",this);
#endif
	    m.userData(m_chan);
	    /* Temporary add to the waiting list to avoid reentrance */
	    m_waiting.append(&m)->setDelete(false);
	    outputLine(m.encode(Engine::dispatch(m),id));
	    m_waiting.remove(&m,false);
	    return;
	}
    }
    reportError(line);
}

bool ExtModHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^external/\\([^/]*\\)/\\([^ ]*\\)\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
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
	Debug(DebugFail,"Invalid ExtModule method '%s', use 'nochan', 'nodata', 'play', 'record' or 'playrec'",
	    t.c_str());
	return false;
    }
    if (typ == ExtModChan::NoChannel) {
	ExtModReceiver *r = ExtModReceiver::build(dest.matchString(2).c_str(),
						  dest.matchString(3).trimBlanks().c_str());
	return r ? r->received(msg,1) : false;
    }
    if (typ != ExtModChan::DataNone && !dd) {
	Debug(DebugGoOn,"ExtMod '%s' call found but no data channel!",t.c_str());
	return false;
    }
    ExtModChan *em = ExtModChan::build(dest.matchString(2).c_str(),
				       dest.matchString(3).c_str(),typ);
    if (!em) {
	Debug(DebugGoOn,"Failed to create ExtMod for '%s'",dest.matchString(2).c_str());
	return false;
    }
    if (!(em->receiver() && em->receiver()->received(msg,1))) {
	Debug(DebugWarn,"ExtMod '%s' did not handle call message",dest.matchString(2).c_str());
	em->deref();
	return false;
    }
    if (dd && dd->connect(em))
	em->deref();
    return true;
}

ExtModulePlugin::ExtModulePlugin()
    : m_handler(0)
{
    Output("Loaded module ExtModule");
}

ExtModulePlugin::~ExtModulePlugin()
{
    Output("Unloading module ExtModule");
    s_modules.clear();
    // the receivers destroyed above should also clear chans but better be sure
    s_chans.clear();
}

void ExtModulePlugin::initialize()
{
    Output("Initializing module ExtModule");
    s_cfg = Engine::configFile("extmodule");
    s_cfg.load();
    if (!m_handler) {
	m_handler = new ExtModHandler("call");
	Engine::install(m_handler);
	NamedList *list = s_cfg.getSection("scripts");
	if (list)
	{
	    unsigned int len = list->length();
	    for (unsigned int i=0; i<len; i++) {
		NamedString *n = list->getParam(i);
		if (n)
		    ExtModReceiver::build(n->name(),*n);
	    }
	}
    }
}

INIT_PLUGIN(ExtModulePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
