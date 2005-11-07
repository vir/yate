/**
 * extmodule.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Some parts of this code have been stolen shamelessly from app_agi.
 * I think that AGI is great idea.
 * 
 * External module handler
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
static ObjList s_chans;
static ObjList s_modules;
static Mutex s_mutex(true);

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
    virtual void Consume(const DataBlock &data, unsigned long timestamp);
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
    static ExtModChan* build(const char *file, const char *args, int type);
    ~ExtModChan();
    virtual void disconnected(bool final, const char *reason);
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
	{ m_id = id; }
private:
    ExtModChan(const char *file, const char *args, int type);
    ExtModReceiver *m_recv;
    int m_type;
    bool m_running;
    bool m_disconn;
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

class ExtModReceiver : public MessageReceiver, public Mutex
{
public:
    static ExtModReceiver* build(const char *script, const char *args,
	File* ain = 0, File* aout = 0, ExtModChan *chan = 0);
    static ExtModReceiver* find(const String &script);
    ~ExtModReceiver();
    virtual bool received(Message &msg, int id);
    void processLine(const char *line);
    bool outputLine(const char *line);
    void reportError(const char *line);
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
private:
    ExtModReceiver(const char *script, const char *args,
	File* ain = 0, File* aout = 0, ExtModChan *chan = 0);
    bool create(const char *script, const char *args);
    void closeIn();
    void closeOut();
    void closeAudio();
    bool m_dead;
    int m_use;
    pid_t m_pid;
    Stream* m_in;
    Stream* m_out;
    File* m_ain;
    File* m_aout;
    ExtModChan *m_chan;
    String m_script, m_args;
    ObjList m_waiting;
    ObjList m_reenter;
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

class ExtModCommand : public MessageHandler
{
public:
    ExtModCommand(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
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
	int64_t dly = tpos - Time::now();
	if (dly > 0) {
	    XDebug("ExtModSource",DebugAll,"Sleeping for " FMT64 " usec",dly);
	    Thread::usleep(dly);
	}
	if (r <= 0)
	    continue;
	DataBlock buf(data,r,false);
	Forward(buf,m_total/2);
	buf.clear(false);
	m_total += r;
	tpos += (r*1000000ULL/m_brate);
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

void ExtModConsumer::Consume(const DataBlock &data, unsigned long timestamp)
{
    if ((m_str) && !data.null()) {
	m_str->writeData(data);
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
    : CallEndpoint("ExtModule"), m_recv(0), m_type(type), m_disconn(false)
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
	m->addParam("id",m_id);
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
    m_id << (unsigned int)this << "." << (unsigned int)random();
}

bool MsgHolder::decode(const char *s)
{
    return (m_msg.decode(s,m_ret,m_id) == -2);
}

ExtModReceiver* ExtModReceiver::build(const char *script, const char *args,
				      File* ain, File* aout, ExtModChan *chan)
{
    ExtModReceiver* recv = new ExtModReceiver(script,args,ain,aout,chan);
    if (!recv->start()) {
	recv->destruct();
	return 0;
    }
    return recv;
}

ExtModReceiver* ExtModReceiver::find(const String &script)
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

ExtModReceiver::ExtModReceiver(const char *script, const char *args, File* ain, File* aout, ExtModChan *chan)
    : Mutex(true),
      m_dead(false), m_use(0), m_pid(-1),
      m_in(0), m_out(0), m_ain(ain), m_aout(aout),
      m_chan(chan), m_script(script), m_args(args)
{
    Debug(DebugAll,"ExtModReceiver::ExtModReceiver(\"%s\",\"%s\") [%p]",script,args,this);
    m_script.trimBlanks();
    m_args.trimBlanks();
    s_mutex.lock();
    s_modules.append(this);
    s_mutex.unlock();
}


ExtModReceiver::~ExtModReceiver()
{   
    Debug(DebugAll,"ExtModReceiver::~ExtModReceiver() [%p] pid=%d",this,m_pid);
    Lock lock(this);
    /* One destruction is plenty enough */
    m_use = -100;
    s_mutex.lock();
    s_modules.remove(this,false);
    s_mutex.unlock();
    die();
    if (m_pid > 0)
	Debug(DebugWarn,"ExtModReceiver::~ExtModReceiver() [%p] pid=%d",this,m_pid);
    closeAudio();
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
	ext->startup();
	while (m_pid < 0)
	    Thread::yield();
    }
    return (m_pid > 0);
}


bool ExtModReceiver::flush()
{
    /* Make sure we release all pending messages and not accept new ones */
    if (!Engine::exiting())
	m_relays.clear();
    else {
	ObjList *p = &m_relays;
	for (; p; p=p->next())
	    p->setDelete(false);
    }
    if (m_waiting.get()) {
	DDebug(DebugAll,"ExtModReceiver releasing pending messages [%p]",this);
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

    /* Give the external script a chance to die gracefully */
    closeOut();
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

    /* Now terminate the process and close its stdout pipe */
    closeIn();
    if (m_pid > 0)
	::kill(m_pid,SIGTERM);
    if (chan && clearChan)
	chan->disconnect();
    unuse();
}

bool ExtModReceiver::received(Message &msg, int id)
{
    lock();
    /* Check if we are no longer running or the message was generated
       by ourselves - avoid reentrance */
    if ((m_pid <= 0) || (!m_in) || (!m_out) || m_reenter.find(&msg)) {
	unlock();
	return false;
    }

    use();
    MsgHolder h(msg);
    if (outputLine(msg.encode(h.m_id))) {
	m_waiting.append(&h)->setDelete(false);
	DDebug(DebugAll,"ExtMod [%p] queued message '%s' [%p]",this,msg.c_str(),&msg);
    }
    unlock();
    for (;;) {
	Thread::yield();
	lock();
        bool ok = !m_waiting.find(&h);
	unlock();
	if (ok)
	    break;
    }
    DDebug(DebugAll,"ExtMod [%p] message '%s' [%p] returning %s",this,msg.c_str(),&msg, h.m_ret ? "true" : "false");
    unuse();
    return h.m_ret;
}

bool ExtModReceiver::create(const char *script, const char *args)
{
    String tmp(script);
    int pid;
    HANDLE ext2yate[2];
    HANDLE yate2ext[2];
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
	if (m_ain && m_ain->valid())
	    ::dup2(m_ain->handle(), STDERR_FILENO+1);
	else
	    ::close(STDERR_FILENO+1);
	if (m_aout && m_aout->valid())
	    ::dup2(m_aout->handle(), STDERR_FILENO+2);
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
    m_in = new File(ext2yate[0]);
    m_out = new File(yate2ext[1]);

    /* close what we're not using in the parent */
    close(ext2yate[1]);
    close(yate2ext[0]);
    closeAudio();
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
	m_pid = 0;
    }
    unuse();
}

void ExtModReceiver::run()
{
    /* We must do the forking from this thread */
    if (!create(m_script.safe(),m_args.safe())) {
	m_pid = 0;
	return;
    }
    use();
    char buffer[1024];
    int posinbuf = 0;
    DDebug(DebugAll,"ExtModReceiver::run() entering loop [%p]",this);
    for (;;) {
	use();
	int readsize = m_in ? m_in->readData(buffer+posinbuf,sizeof(buffer)-posinbuf-1) : 0;
	DDebug(DebugAll,"ExtModReceiver::run() read %d",readsize);
	if (unuse())
	    return;
	if (!readsize) {
	    lock();
	    Debug("ExtModule",DebugInfo,"Read EOF on %p [%p]",m_in,this);
	    closeIn();
	    flush();
	    unlock();
	    if (m_chan && m_chan->running())
		Thread::sleep(1);
	    break;
	}
	else if (readsize < 0) {
	    Debug("ExtModule",DebugWarn,"Read error %d on %p [%p]",errno,m_in,this);
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
	    use();
	    if (buffer[0])
		processLine(buffer);
	    if (unuse())
		return;
	    totalsize -= eoline-buffer+1;
	    ::memmove(buffer,eoline+1,totalsize+1);
	}
	posinbuf = totalsize;
    }
}

bool ExtModReceiver::outputLine(const char *line)
{
    DDebug("ExtModReceiver",DebugAll,"outputLine '%s'", line);
    if (!m_out)
	return false;
    m_out->writeData(line);
    char nl = '\n';
    m_out->writeData(&nl,sizeof(nl));
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
    DDebug("ExtModReceiver",DebugAll,"processLine '%s'", line);
    {
	Lock mylock(this);
	ObjList *p = &m_waiting;
	for (; p; p=p->next()) {
	    MsgHolder *msg = static_cast<MsgHolder *>(p->get());
	    if (msg && msg->decode(line)) {
		DDebug("ExtModReceiver",DebugInfo,"Matched message");
		p->remove(false);
		return;
	    }
	}
    }
    String id(line);
    if (startSkip(id,"%%>install:")) {
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
    else if (startSkip(id,"%%>setlocal:")) {
	int col = id.find(':');
	if (col > 0) {
	    String val(id.substr(col+1));
	    id = id.substr(0,col);
	    bool ok = false;
	    if (m_chan && (id == "id")) {
		m_chan->setId(val);
		ok = true;
	    }
	    else if (m_chan && (id == "disconnected")) {
		m_chan->setDisconn(val.toBoolean(m_chan->disconn()));
		val = m_chan->disconn();
		ok = true;
	    }
	    Debug("ExtModReceiver",DebugAll,"Set '%s'='%s' %s",
		id.c_str(),val.c_str(),ok ? "ok" : "failed");
	    String out("%%<setlocal:");
	    out << id << ":" << val << ":" << ok;
	    outputLine(out);
	    return;
	}
    }
    else {
	Message* m = new Message("");
	if (m->decode(line,id) == -2) {
	    DDebug("ExtModReceiver",DebugAll,"Created message %p '%s' [%p]",m,m->c_str(),this);
	    lock();
	    m->userData(m_chan);
	    if (id.null()) {
		/* Empty id means no answer is desired - enqueue and forget */
		Engine::enqueue(m);
		unlock();
		return;
	    }
	    /* Copy the user data pointer from waiting message with same id */
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
	    /* Temporary add to the reenter list to avoid reentrance */
	    m_reenter.append(m)->setDelete(false);
	    unlock();
	    String ret(m->encode(Engine::dispatch(m),id));
	    lock();
	    outputLine(ret);
	    m_reenter.remove(m,false);
	    unlock();
	    m->destruct();
	    return;
	}
	m->destruct();
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
    if (!(em->receiver() && em->receiver()->received(msg,1))) {
	Debug(DebugWarn,"ExtMod '%s' did not handle call message",dest.matchString(2).c_str());
	em->deref();
	return false;
    }
    if (ch)
	ch->connect(em);
    em->deref();
    return true;
}

bool ExtModCommand::received(Message &msg)
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
		msg.retValue() << ++n << ". " << r->scriptFile() << " " << r->commandArg() << "\n";
	}
	return true;
    }
    int blank = line.find(' ');
    if (blank <= 0) {
	if (line == "stop")
	    return false;
	ExtModReceiver *r = ExtModReceiver::build(line,0);
	msg.retValue() = r ? "External start attempt\n" : "External command failed!\n";
	return true;
    }
    if (line.startSkip("stop")) {
	if (line.null())
	    return false;
	ExtModReceiver *r = ExtModReceiver::find(line);
	if (r) {
	    r->destruct();
	    msg.retValue() = "External command stopped\n";
	}
	else
	    msg.retValue() = "External not running\n";
	return true;
    }
    ExtModReceiver *r = ExtModReceiver::build(line.substr(0,blank),line.substr(blank+1));
    msg.retValue() = r ? "External start attempt\n" : "External command failed\n";
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
    if (!m_handler) {
	m_handler = new ExtModHandler("call.execute");
	Engine::install(m_handler);
	Engine::install(new ExtModCommand("engine.command"));
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
