/**
 * osschan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Oss driver
 * I have to thank you to Mark Spencer because some parts of the code have 
 * been taken from chan_oss.c from asterisk.
 */

#include <telengine.h>
#include <telephony.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/time.h>

#if defined(__linux__)
#include <linux/soundcard.h>
#elif defined (__FreeBSD__)
#include <machine/soundcard.h>
#else
#include <soundcard.h>
#endif

#define MIN_SWITCH_TIME 600

using namespace TelEngine;

class OssChan;

OssChan *s_chan = 0;

class OssSource : public ThreadedSource
{
public:
    OssSource(OssChan *chan)
	: m_chan(chan)
    {
	Debug(DebugAll,"OssSource::OssSource(%p) [%p]",chan,this);
    }
    bool init();
    ~OssSource();
    virtual void run();
    virtual void cleanup();
private:
    OssChan *m_chan;
    unsigned m_brate;
    unsigned m_total;
};

class OssConsumer : public DataConsumer
{
public:
    OssConsumer(OssChan *chan)
	: m_chan(chan)
    {
	Debug(DebugAll,"OssConsumer::OssConsumer(%p) [%p]",chan,this);
    }
    bool init();
    ~OssConsumer();
    virtual void Consume(const DataBlock &data, unsigned long timeDelta);
private:
    OssChan *m_chan;
    unsigned m_total;
};

class OssChan : public DataEndpoint
{
public:
    OssChan(String dev);
    bool init();
    ~OssChan();
    int setformat();
    virtual void disconnected();
    int soundcard_setinput(bool force);
    int soundcard_setoutput(bool force);
    int time_has_passed(void);
    
    String m_dev;
    int full_duplex;
    int m_fd;
    int readmode;
    struct timeval lasttime;
};

class OssHandler : public MessageHandler
{
public:
    OssHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("status") { }
    virtual bool received(Message &msg);
};

class DropHandler : public MessageHandler
{
public:
    DropHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class OssPlugin : public Plugin
{
public:
    OssPlugin();
    virtual void initialize();
    virtual bool isBusy() const;
private:
    OssHandler *m_handler;
};

bool OssSource::init()
{
    m_brate = 16000;
    m_total = 0;
    if (m_chan->soundcard_setinput(false) < 0) {
	Debug(DebugWarn, "Unable to set input mode\n");
	return false;
    }
    start("OssSource");
    return true;
}

OssSource::~OssSource()
{
    Debug(DebugAll,"OssSource::~OssSource() [%p] total=%u",this,m_total);
    if (m_chan->m_fd >= 0) {
	::close(m_chan->m_fd);
	m_chan->m_fd = -1;
    }
}

void OssSource::run()
{
    int r = 0;
    unsigned long long tpos = Time::now();
    do {
	if (m_chan->m_fd < 0) {
	    Thread::yield();
	    r = 1;
	    continue;
	}
	DataBlock data(0,480);
	r = ::read(m_chan->m_fd, data.data(), data.length());
	if (r < 0) {
	    if (errno == EINTR || errno == EAGAIN) {
		Thread::yield();
		r = 1;
		continue;
	    }
	    break;
	}
	else if (r == 0) 
	{
	    Thread::yield();
	    r =1;
	    continue;
	}
	if (r < (int)data.length())
	    data.assign(data.data(),r);
	long long dly = tpos - Time::now();
	if (dly > 0) {
#ifdef DEBUG
	    Debug("OssSource",DebugAll,"Sleeping for %lld usec",dly);
#endif
	    ::usleep((unsigned long)dly);
	}
	Forward(data,data.length()/2);
	m_total += r;
	tpos += (r*1000000ULL/m_brate);
    } while (r > 0);
    Debug(DebugAll,"OssSource [%p] end of data",this);
}

void OssSource::cleanup()
{
    Debug(DebugAll,"OssSource [%p] cleanup, total=%u",this,m_total);
    m_chan->disconnect();
}

bool OssConsumer::init()
{
    m_total = 0;
    if (!m_chan->full_duplex) {
		/* If we're half duplex, we have to switch to read mode
		   to honor immediate needs if necessary */
	if (m_chan->soundcard_setinput(true) < 0) {
	    Debug(DebugWarn, "Unable to set device to input mode\n");
	    return false;
	}
    return true;
    }
    int res = m_chan->soundcard_setoutput(false);
    if (res < 0) {
	    Debug(DebugWarn, "Unable to set output device\n");
	    return false;
    } else if (res > 0) {
		/* The device is still in read mode, and it's too soon to change it,
		   so just pretend we wrote it */
	return true;
    }
    
    return true;
}

OssConsumer::~OssConsumer()
{
    Debug(DebugAll,"OssConsumer::~OssConsumer() [%p] total=%u",this,m_total);
    if (m_chan->m_fd >= 0) {
	::close(m_chan->m_fd);
	m_chan->m_fd = -1;
    }
}

void OssConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    if ((m_chan->m_fd >= 0) && !data.null()) {
	::write(m_chan->m_fd,data.data(),data.length());
	m_total += data.length();
    }
}

OssChan::OssChan(String dev)
    : DataEndpoint("oss"),m_dev(dev),full_duplex(0), m_fd(-1), readmode(1) 
{
    Debug(DebugAll,"OssChan::OssChan dev [%s] [%p]",dev.c_str(),this);
    s_chan = this;
}

OssChan::~OssChan()
{
    Debug(DebugAll,"OssChan::~OssChan() [%p]",this);
    s_chan = 0;
}

bool OssChan::init()
{
    m_fd = ::open(m_dev,  O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
	Debug(DebugWarn, "Unable to open %s: %s\n", m_dev.c_str(), strerror(errno));
	return false;
    }
    gettimeofday(&lasttime, NULL);
    setformat();
    if (!full_duplex)
	soundcard_setinput(true);
	    
    OssSource *source = new OssSource(this);
    if (!source->init())
    {
	delete source;
	return false;		
    }
    setSource(source);
    source->deref();
    OssConsumer *cons = new OssConsumer(this);
    if (!cons->init())
    {
	delete cons;
	return false;		
    }	
    setConsumer(cons);
    cons->deref();
    return true;
}
int OssChan::time_has_passed(void)
{
    struct timeval tv;
    int ms;
    gettimeofday(&tv, NULL);
    ms = (tv.tv_sec - lasttime.tv_sec) * 1000 +
	(tv.tv_usec - lasttime.tv_usec) / 1000;
    if (ms > MIN_SWITCH_TIME)
	return -1;
    return 0;
}

int OssChan::setformat()
{
    int fmt = AFMT_S16_LE;
    int res = ::ioctl(m_fd, SNDCTL_DSP_SETFMT, &fmt);
    if (res < 0) {
	Debug(DebugWarn, "Unable to set format to 16-bit signed\n");
	return -1;
    }
    res = ::ioctl(m_fd, SNDCTL_DSP_SETDUPLEX, 0);
    if (res >= 0) {
	Debug(DebugInfo,"OSS audio device is full duplex\n");
	full_duplex = -1;
    }
    fmt = 0;
    res = ::ioctl(m_fd, SNDCTL_DSP_STEREO, &fmt);
    if (res < 0) {
	Debug(DebugWarn, "Failed to set audio device to mono\n");
	return -1;
    }
    int desired = 8000;
    fmt = desired;
    res = ::ioctl(m_fd, SNDCTL_DSP_SPEED, &fmt);
    if (res < 0) {
	Debug(DebugWarn, "Failed to set audio device speed\n");
	return -1;
    }
    if (fmt != desired)
	Debug(DebugWarn, "Requested %d Hz, got %d Hz -- sound may be choppy\n", desired, fmt);
    fmt = (2 << 16) | 8;
    res = ::ioctl(m_fd, SNDCTL_DSP_SETFRAGMENT, &fmt);
    if (res < 0)
	Debug(DebugWarn, "Unable to set fragment size -- sound may be choppy\n");
    return 0;
}

int OssChan::soundcard_setinput(bool force)
{
    if (full_duplex || (readmode && !force))
		return 0;
	readmode = -1;
	if (force || time_has_passed()) {
		ioctl(m_fd, SNDCTL_DSP_RESET);
		close(m_fd);
		/* dup2(0, sound); */
		m_fd = open(m_dev.c_str(), O_RDONLY | O_NONBLOCK);
		if (m_fd < 0) {
			Debug(DebugWarn, "Unable to re-open DSP device: %s\n", ::strerror(errno));
			return -1;
		}
		if (setformat()) {
			return -1;
		}
		return 0;
	}
	return 1;
}


int OssChan::soundcard_setoutput(bool force)
{
	/* Make sure the soundcard is in output mode.  */
	if (full_duplex || (!readmode && !force))
		return 0;
	readmode = 0;
	if (force || time_has_passed()) {
		::ioctl(m_fd, SNDCTL_DSP_RESET);
		/* Keep the same fd reserved by closing the sound device and copying stdin at the same
		   time. */
		/* dup2(0, sound); */ 
		::close(m_fd);
		m_fd = open(m_dev.c_str(), O_WRONLY |O_NONBLOCK);
		if (m_fd < 0) {
			Debug(DebugWarn, "Unable to re-open DSP device: %s\n", strerror(errno));
			return -1;
		}
		if (setformat()) {
			return -1;
		}
		return 0;
	}
	return 1;
}

void OssChan::disconnected()
{
    Debugger debug("OssChan::disconnected()"," [%p]",this);
    destruct();
}

bool OssHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^oss/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (s_chan)
	return false;
    OssChan *chan = new OssChan(dest.matchString(1).c_str());
    if (!chan->init())
    {
	delete chan;
	return false;
    }
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    Debug(DebugInfo,"We are routing to device '%s'",dest.matchString(1).c_str());
    if (dd)
	dd->connect(chan);
    else {
        const char *direct = msg.getValue("direct");
	if (direct)
	{
	    Message m("call");
	    m.addParam("id",dest);
	    m.addParam("caller",dest);
	    m.addParam("callto",direct);
	    m.userData(chan);
	    if (Engine::dispatch(m))
		return true;
	    Debug(DebugFail,"OSS outgoing call not accepted!");
	    return false;
	}	
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"OSS outgoing call with no target!");
	    return false;
	}
	Message m("preroute");
	m.addParam("id",dest);
	m.addParam("caller",dest);
	m.addParam("called",targ);
	Engine::dispatch(m);
	m = "route";
	if (Engine::dispatch(m)) {
	    m = "call";
	    m.addParam("callto",m.retValue());
	    m.retValue() = 0;
	    m.userData(chan);
	    if (Engine::dispatch(m))
		return true;
	    Debug(DebugFail,"OSS outgoing call not accepted!");
	}
	else
	    Debug(DebugWarn,"OSS outgoing call but no route!");
	delete chan;
	return false;
    }

    return true;
}

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"oss"))
	return false;
    msg.retValue() << "oss,osschan=" << (s_chan != 0 ) << "\n";
    return false;
}


bool DropHandler::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (id.null() || id.startsWith("oss/")) {
	if (s_chan) {
	    Debug("OssDropper",DebugInfo,"Dropping call");
	    s_chan->disconnect();
	}
	return !id.null();
    }
    return false;
}

OssPlugin::OssPlugin()
    : m_handler(0)
{
    Output("Loaded module OssChan");
}

void OssPlugin::initialize()
{
    Output("Initializing module OssChan");
    if (!m_handler) {
	m_handler = new OssHandler("call");
	Engine::install(new DropHandler("drop"));
	Engine::install(m_handler);
	Engine::install(new StatusHandler);
    }
}

bool OssPlugin::isBusy() const
{
    return (s_chan != 0);
}

INIT_PLUGIN(OssPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
