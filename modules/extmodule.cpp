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

class ExtModReceiver;

class ExtModSource : public ThreadedSource
{
public:
    ExtModSource(int fd);
    ~ExtModSource();
    virtual void run();
private:
    int m_fd;
    unsigned m_brate;
    unsigned m_total;
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
	DataNone = 0,
	DataRead,
	DataWrite,
	DataBoth
    };
    ExtModChan(const char *file, int type);
    ~ExtModChan();
    virtual void disconnected();
private:
    ExtModReceiver *m_recv;
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
    ExtModReceiver(const char *script, const char *args,
	int ain = -1, int aout = -1, ExtModChan *chan = 0);
    ~ExtModReceiver();
    virtual bool received(Message &msg, int id);
    void processLine(const char *line);
    void outputLine(const char *line);
    bool create(const char *script, const char *args);
    void run();
private:
    pid_t m_pid;
    int m_in, m_out, m_ain, m_aout;
    ExtModChan *m_chan;
    String m_script, m_args;
    ObjList m_waiting;
};

class ExtThread : public Thread
{
public:
    ExtThread(ExtModReceiver *receiver) : Thread("ExtModule"), m_receiver(receiver)
	{ }
    virtual void run()
	{ m_receiver->run(); }
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

ExtModSource::ExtModSource(int fd)
    : m_fd(fd), m_brate(16000), m_total(0)
{
    Debug(DebugAll,"ExtModSource::ExtModSource(%d) [%p]",fd,this);
    if (m_fd >= 0)
	start("ExtModSource");
}

ExtModSource::~ExtModSource()
{
    Debug(DebugAll,"ExtModSource::~ExtModSource() [%p] total=%u",this,m_total);
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

ExtModChan::ExtModChan(const char *file, int type)
    : DataEndpoint("ExtModule"), m_recv(0)
{
    Debug(DebugAll,"ExtModChan::ExtModChan(%d) [%p]",type,this);
    int wfifo[2] = { -1, -1 };
    int rfifo[2] = { -1, -1 };
    switch (type) {
	case DataWrite:
	case DataBoth:
	    ::pipe(wfifo);
	    setConsumer(new ExtModConsumer(wfifo[1]));
	    getConsumer()->deref();
    }
    switch (type) {
	case DataRead:
	case DataBoth:
	    ::pipe(rfifo);
	    setSource(new ExtModSource(rfifo[0]));
	    getSource()->deref();
    }
    m_recv = new ExtModReceiver(file,"",wfifo[0],rfifo[1],this);
}

ExtModChan::~ExtModChan()
{
    Debug(DebugAll,"ExtModChan::~ExtModChan() [%p]",this);
}

void ExtModChan::disconnected()
{
    Debugger debug("ExtModChan::disconnected()"," [%p]",this);
//    destruct();
}

MsgHolder::MsgHolder(Message &msg)
    : m_msg(msg), m_ret(false)
{
    m_id = (int)this;
}

bool MsgHolder::decode(const char *s)
{
    return (m_msg.decode(s,m_ret,m_id) == -2);
}

ExtModReceiver::ExtModReceiver(const char *script, const char *args, int ain, int aout, ExtModChan *chan)
    : m_pid(-1), m_in(-1), m_out(-1), m_ain(ain), m_aout(aout),
      m_chan(chan), m_script(script), m_args(args)
{
    Debug(DebugAll,"ExtModReceiver::ExtModReceiver(\"%s\",\"%s\") [%p]",script,args,this);
    new ExtThread(this);
}

ExtModReceiver::~ExtModReceiver()
{   
    Debug(DebugAll,"ExtModReceiver::~ExtModReceiver() [%p] pid=%d",this,m_pid);
    /* Give the external script a chance to die gracefully */
    ::kill(m_pid,SIGTERM);
    ::close(m_in);
    ::close(m_out);
    if (m_chan)
	m_chan->disconnect();
    int w = ::waitpid(m_pid, 0, WNOHANG);
    if (w == 0)
	Debug(DebugWarn, "Process %d has not exited yet?",m_pid);
    else if (w < 0)
	Debug(DebugWarn, "Failed waitpid on %d: %s",m_pid,strerror(errno));
}

bool ExtModReceiver::received(Message &msg, int id)
{
    MsgHolder h(msg);
    m_waiting.append(&h);
    Debug(DebugAll,"ExtMod [%p] queued message '%s' [%p]",this,msg.c_str(),&msg);
    outputLine(msg.encode(h.m_id));
    while (m_waiting.find(&h))
	Thread::yield();
    Debug(DebugAll,"ExtMod [%p] message '%s' [%p] returning %s",this,msg.c_str(),&msg, h.m_ret ? "true" : "false");
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
    pid = Thread::fork();
    if (pid < 0) {
	Debug(DebugWarn, "Failed to fork(): %s", strerror(errno));
	return false;
    }
    if (!pid) {
	/* Terminate all other threads if needed */
	Thread::preExec();
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
	/* Close everything but stdin/out/error/audio */
	for (x=STDERR_FILENO+3;x<1024;x++) 
	    ::close(x);
	/* Execute script */
	::fprintf(stderr, "Execing '%s' '%s'\n", script, args);
        ::execl(script, script, args, (char *)NULL);
	::fprintf(stderr, "Failed to execute '%s': %s\n", script, strerror(errno));
	::exit(1);
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

void ExtModReceiver::run()
{
    if (!create(m_script.safe(),m_args.safe())) {
	destruct();
	return;
    }
    char buffer[1024];
    int posinbuf = 0;
    for (;;) {
	int readsize = ::read(m_in,buffer+posinbuf,sizeof(buffer)-posinbuf-1);
	if (!readsize) {
	    destruct();
	    return;
	}
	else if (readsize < 0) {
	    Debug("ExtModule",DebugWarn,"Read error %d on %d",errno,m_in);
	    destruct();
	    return;
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

void ExtModReceiver::outputLine(const char *line)
{
    Debug("ExtModReceiver",DebugInfo,"outputLine '%s'", line);
    ::write(m_out,line,::strlen(line));
    char nl = '\n';
    ::write(m_out,&nl,sizeof(nl));
}

void ExtModReceiver::processLine(const char *line)
{
    Debug("ExtModReceiver",DebugInfo,"processLine '%s'", line);
    ObjList *p = &m_waiting;
    for (; p; p=p->next()) {
	MsgHolder *msg = static_cast<MsgHolder *>(p->get());
	if (msg && msg->decode(line)) {
	    Debug("ExtModReceiver",DebugInfo,"Matched message");
	    p->remove(false);
	    return;
	}
    }
    Message m("");
    String id;
    if (m.decode(line,id) == -2) {
	Debug("ExtModReceiver",DebugInfo,"Created message [%p]",this);
	outputLine(m.encode(Engine::dispatch(m),id));
	Debug("ExtModReceiver",DebugInfo,"Dispatched message [%p]",this);
	return;
    }
    Debug("ExtModReceiver",DebugWarn,"Error: '%s'", line);
}

bool ExtModHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^external/\\([^/]*\\)/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    String t = dest.matchString(1);
    int typ = 0;
    if (t == "none")
	typ = ExtModChan::DataNone;
    else if (t == "read")
	typ = ExtModChan::DataRead;
    else if (t == "write")
	typ = ExtModChan::DataWrite;
    else if (t == "both")
	typ = ExtModChan::DataBoth;
    else {
	Debug(DebugFail,"Invalid ExtModule method '%s', use 'none', 'read', 'write' or 'both'",
	    t.c_str());
	return false;
    }
    if (typ != ExtModChan::DataNone && !dd) {
	Debug(DebugFail,"ExtMod '%s' call found but no data channel!",t.c_str());
	return false;
    }
    ExtModChan *em = new ExtModChan(dest.matchString(2).c_str(),typ);
    if (!em) {
	Debug(DebugFail,"Failed to create ExtMod for '%s'",dest.matchString(2).c_str());
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
		if (n) {
		    new ExtModReceiver(n->name(),*n);
		}
	    }
	}
    }
}

INIT_PLUGIN(ExtModulePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
