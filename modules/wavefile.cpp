/**
 * wavefile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wave file driver (record+playback)
 */

#include <telengine.h>
#include <telephony.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>


using namespace TelEngine;

class WaveChan;

class WaveSource : public ThreadedSource
{
public:
    WaveSource(const char *file, WaveChan *chan);
    ~WaveSource();
    virtual void run();
    virtual void cleanup();
private:
    WaveChan *m_chan;
    int m_fd;
    unsigned m_brate;
    unsigned m_total;
};

class WaveConsumer : public DataConsumer
{
public:
    WaveConsumer(const char *file);
    ~WaveConsumer();
    virtual void Consume(const DataBlock &data);
private:
    int m_fd;
    unsigned m_total;
};

class WaveChan : public DataEndpoint
{
public:
    WaveChan(const char *file, bool record);
    ~WaveChan();
    virtual void disconnected();
};

class WaveHandler : public MessageHandler
{
public:
    WaveHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class WaveFilePlugin : public Plugin
{
public:
    WaveFilePlugin();
    virtual void initialize();
private:
    WaveHandler *m_handler;
};

WaveSource::WaveSource(const char *file, WaveChan *chan)
    : m_chan(chan), m_fd(-1), m_brate(16000), m_total(0)
{
    Debug(DebugAll,"WaveSource::WaveSource(\"%s\") [%p]",file,this);
    m_fd = ::open(file,O_RDONLY|O_NOCTTY);
    if (m_fd >= 0)
	start("WaveSource");
    else
	Debug(DebugFail,"Opening '%s': error %d: %s",
	    file, errno, ::strerror(errno));
}

WaveSource::~WaveSource()
{
    Debug(DebugAll,"WaveSource::~WaveSource() [%p] total=%u",this,m_total);
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
}

void WaveSource::run()
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
	    Debug("WaveSource",DebugAll,"Sleeping for %lld usec",dly);
#endif
	    ::usleep((unsigned long)dly);
	}
	Forward(data);
	m_total += r;
	tpos += (r*1000000ULL/m_brate);
    } while (r > 0);
    Debug(DebugAll,"WaveSource [%p] end of data",this);
}

void WaveSource::cleanup()
{
    Debug(DebugAll,"WaveSource [%p] cleanup, total=%u",this,m_total);
    m_chan->disconnect();
}

WaveConsumer::WaveConsumer(const char *file)
    : m_fd(-1), m_total(0)
{
    Debug(DebugAll,"WaveConsumer::WaveConsumer(\"%s\") [%p]",file,this);
    m_fd = ::creat(file,S_IRUSR|S_IWUSR);
    if (m_fd < 0)
	Debug(DebugFail,"Creating '%s': error %d: %s",
	    file, errno, ::strerror(errno));
}

WaveConsumer::~WaveConsumer()
{
    Debug(DebugAll,"WaveConsumer::~WaveConsumer() [%p] total=%u",this,m_total);
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
}

void WaveConsumer::Consume(const DataBlock &data)
{
    if ((m_fd >= 0) && !data.null()) {
	::write(m_fd,data.data(),data.length());
	m_total += data.length();
    }
}

WaveChan::WaveChan(const char *file, bool record)
    : DataEndpoint("wavefile")
{
    Debug(DebugAll,"WaveChan::WaveChan(%s) [%p]",(record ? "record" : "play"),this);
    if (record) {
	setConsumer(new WaveConsumer(file));
	getConsumer()->deref();
    }
    else {
	setSource(new WaveSource(file,this));
	getSource()->deref();
    }
}

WaveChan::~WaveChan()
{
    Debug(DebugAll,"WaveChan::~WaveChan() [%p]",this);
}

void WaveChan::disconnected()
{
    Debugger debug("WaveChan::disconnected()"," [%p]",this);
    destruct();
}

bool WaveHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (!msg.userData()) {
	Debug(DebugFail,"Wave call found but no data channel!");
	return false;
    }
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dest.matchString(1) == "record") {
	Debug(DebugInfo,"Record to wave file '%s'",dest.matchString(2).c_str());
	dd->connect(new WaveChan(dest.matchString(2).c_str(),true));
	return true;
    }
    else if (dest.matchString(1) == "play") {
	Debug(DebugInfo,"Play from wave file '%s'",dest.matchString(2).c_str());
	dd->connect(new WaveChan(dest.matchString(2).c_str(),false));
	return true;
    }
    Debug(DebugFail,"Invalid wavefile method '%s', use 'record' or 'play'",
	dest.matchString(1).c_str());
    return false;
}

WaveFilePlugin::WaveFilePlugin()
    : m_handler(0)
{
    Output("Loaded module WaveFile");
}

void WaveFilePlugin::initialize()
{
    Output("Initializing module WaveFile");
    if (!m_handler) {
	m_handler = new WaveHandler("call");
	Engine::install(m_handler);
    }
}

INIT_PLUGIN(WaveFilePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
