/**
 * osschan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Oss driver
 * I have to thank you to Mark Spencer because some parts of the code have
 * been taken from chan_oss.c from asterisk.
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

#include <yatephone.h>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <fcntl.h>

#if defined(__linux__)
#include <linux/soundcard.h>
#elif defined (__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

// How long (in usec) before we force I/O direction change
#define MIN_SWITCH_TIME 600000

// Buffer size in bytes - match the preferred 20ms
#define OSS_BUFFER_SIZE 320

using namespace TelEngine;
namespace { // anonymous

class OssDevice;

class OssSource : public ThreadedSource
{
public:
    OssSource(OssDevice* dev);
    ~OssSource();
    bool init();
    virtual void run();
    virtual void cleanup();
private:
    OssDevice* m_device;
    unsigned m_brate;
    unsigned m_total;
    DataBlock m_data;
};

class OssConsumer : public DataConsumer
{
public:
    OssConsumer(OssDevice* dev);
    ~OssConsumer();
    bool init();
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
private:
    OssDevice* m_device;
    unsigned m_total;
};

class OssDevice : public RefObject
{
public:
    OssDevice(const String& dev, unsigned int rate = 8000);
    ~OssDevice();
    void close();
    bool reOpen(int iomode);
    bool setPcmFormat();
    int setInputMode(bool force);
    int setOutputMode(bool force);
    bool timePassed(void);
    inline int fd() const
	{ return m_fd; }
    inline bool closed() const
	{ return m_fd < 0; }
    inline bool fullDuplex() const
	{ return m_fullDuplex; }
    inline unsigned int rate() const
	{ return m_rate; }
    inline const String& device() const
	{ return m_dev; }
private:
    String m_dev;
    unsigned int m_rate;
    bool m_fullDuplex;
    bool m_readMode;
    int m_fd;
    u_int64_t m_lastTime;
};

class OssChan : public CallEndpoint
{
public:
    OssChan(const String& dev, unsigned int rate = 8000);
    ~OssChan();
    bool init();
    virtual void disconnected(bool final, const char *reason);
    void answer();
    inline void setTarget(const char* target = 0)
	{ m_target = target; }
    inline const String& getTarget() const
	{ return m_target; }

private:
    String m_dev;
    String m_target;
    unsigned int m_rate;
};

class OssHandler;

class OssPlugin : public Plugin
{
public:
    OssPlugin();
    virtual void initialize();
    virtual bool isBusy() const;
private:
    OssHandler *m_handler;
};

INIT_PLUGIN(OssPlugin);

class OssHandler : public MessageHandler
{
public:
    OssHandler(const char *name)
	: MessageHandler(name,100,__plugin.name())
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

class DropHandler : public MessageHandler
{
public:
    DropHandler(const char *name)
	: MessageHandler(name,100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class MasqHandler : public MessageHandler
{
public:
    MasqHandler(const char *name, int prio)
	: MessageHandler(name,prio,__plugin.name())
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

OssChan *s_chan = 0;
OssDevice* s_dev = 0;


bool OssSource::init()
{
    m_brate = 2 * m_device->rate();
    m_total = 0;
    if (m_device->setInputMode(false) < 0) {
	Debug(DebugWarn, "Unable to set input mode");
	return false;
    }
    start("Oss Source");
    return true;
}

OssSource::OssSource(OssDevice* dev)
    : m_device(0), m_data(0,OSS_BUFFER_SIZE)
{
    Debug(DebugAll,"OssSource::OssSource(%p) [%p]",dev,this);
    dev->ref();
    m_device = dev;
    if (dev->rate() != 8000)
	m_format << "/" << dev->rate();
}

OssSource::~OssSource()
{
    Debug(DebugAll,"OssSource::~OssSource() [%p] total=%u",this,m_total);
    m_device->deref();
}

void OssSource::run()
{
    int r = 1;
    int len = 0;
    u_int64_t tpos = Time::now();
    while ((r > 0) && looping()) {
	if (m_device->closed()) {
	    Thread::yield();
	    continue;
	}
	unsigned char* ptr = (unsigned char*)m_data.data() + len;
	r = ::read(m_device->fd(), ptr, m_data.length() - len);
	if (r < 0) {
	    if (errno == EINTR || errno == EAGAIN) {
		Thread::yield();
		r = 1;
		continue;
	    }
	    break;
	}
	else if (r == 0) {
	    Thread::yield();
	    r = 1;
	    continue;
	}
	len += r;
	if (len < (int)m_data.length()) {
	    Thread::yield();
	    continue;
	}

	int64_t dly = tpos - Time::now();
	if (dly > 0) {
	    XDebug("OssSource",DebugAll,"Sleeping for " FMT64 " usec",dly);
	    Thread::usleep((unsigned long)dly);
	}
	Forward(m_data);
	m_total += len;
	tpos += (len*1000000ULL/m_brate);
	len = 0;
    }
    Debug(DebugAll,"OssSource [%p] end of data",this);
}

void OssSource::cleanup()
{
    Debug(DebugAll,"OssSource [%p] cleanup, total=%u",this,m_total);
    ThreadedSource::cleanup();
}


bool OssConsumer::init()
{
    m_total = 0;
    if (!m_device->fullDuplex()) {
	/* If we're half duplex, we have to switch to read mode
	   to honor immediate needs if necessary */
	if (m_device->setInputMode(true) < 0) {
	    Debug(DebugWarn, "Unable to set device to input mode");
	    return false;
	}
	return true;
    }
    int res = m_device->setOutputMode(false);
    if (res < 0) {
	Debug(DebugWarn, "Unable to set output device");
	return false;
    } else if (res > 0) {
	/* The device is still in read mode, and it's too soon to change it,
	   so just pretend we wrote it */
	return true;
    }
    return true;
}

OssConsumer::OssConsumer(OssDevice* dev)
    : m_device(0)
{
    Debug(DebugAll,"OssConsumer::OssConsumer(%p) [%p]",dev,this);
    dev->ref();
    m_device = dev;
    if (dev->rate() != 8000)
	m_format << "/" << dev->rate();
}

OssConsumer::~OssConsumer()
{
    Debug(DebugAll,"OssConsumer::~OssConsumer() [%p] total=%u",this,m_total);
    m_device->deref();
}

unsigned long OssConsumer::Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags)
{
    if (m_device->closed() || data.null())
	return 0;
    YIGNORE(::write(m_device->fd(),data.data(),data.length()));
    m_total += data.length();
    return invalidStamp();
}


OssChan::OssChan(const String& dev, unsigned int rate)
    : CallEndpoint("oss"),
      m_dev(dev), m_rate(rate)
{
    Debug(DebugAll,"OssChan::OssChan('%s',%u) [%p]",dev.c_str(),rate,this);
    s_chan = this;
}

OssChan::~OssChan()
{
    Debug(DebugAll,"OssChan::~OssChan() [%p]",this);
    setTarget();
    setSource();
    setConsumer();
    s_chan = 0;
}

bool OssChan::init()
{
    OssDevice* dev = new OssDevice(m_dev,m_rate);
    if (dev->closed()) {
	dev->deref();
	return false;
    }
    OssSource* source = new OssSource(dev);
    dev->deref();
    if (!source->init()) {
	source->deref();
	return false;
    }
    setSource(source);
    source->deref();
    OssConsumer* cons = new OssConsumer(dev);
    if (!cons->init()) {
	cons->deref();
	setSource();
	return false;
    }
    setConsumer(cons);
    cons->deref();
    return true;
}

void OssChan::disconnected(bool final, const char *reason)
{
    Debugger debug("OssChan::disconnected()"," '%s' [%p]",reason,this);
    setTarget();
}

void OssChan::answer()
{
    Message* m = new Message("call.answered");
    m->addParam("module","oss");
    String tmp("oss/");
    tmp += m_dev;
    m->addParam("id",tmp);
    if (m_target)
	m->addParam("targetid",m_target);
    Engine::enqueue(m);
}


OssDevice::OssDevice(const String& dev, unsigned int rate)
    : m_dev(dev), m_rate(rate), m_fullDuplex(false), m_readMode(true), m_fd(-1)
{
    Debug(DebugAll,"OssDevice::OssDevice('%s',%u) [%p]",dev.c_str(),rate,this);
    m_fd = ::open(m_dev, O_RDWR|O_NONBLOCK);
    if (m_fd < 0) {
	Debug(DebugWarn, "Unable to open %s: %s", m_dev.c_str(), ::strerror(errno));
	return;
    }
    m_lastTime = Time::now() + MIN_SWITCH_TIME;
    setPcmFormat();
    if (!m_fullDuplex)
	setInputMode(true);
    if (!s_dev)
	s_dev = this;
}

OssDevice::~OssDevice()
{
    Debug(DebugAll,"OssDevice::~OssDevice [%p]",this);
    close();
}

void OssDevice::close()
{
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
    if (s_dev == this)
	s_dev = 0;
}

// Check if we should force a mode change
bool OssDevice::timePassed(void)
{
    return Time::now() > m_lastTime;
}

bool OssDevice::setPcmFormat()
{
    // set fragment to 4 buffers, 2^9=512 bytes each
    int fmt = (4 << 16) | 9;
    int res = ::ioctl(m_fd, SNDCTL_DSP_SETFRAGMENT, &fmt);
    if (res < 0)
	Debug(DebugWarn, "Unable to set fragment size - sound may be choppy");

    // try to set full duplex mode
    res = ::ioctl(m_fd, SNDCTL_DSP_SETDUPLEX, 0);
    if (res >= 0) {
	Debug(DebugInfo,"OSS audio device is full duplex");
	m_fullDuplex = true;
    }

    // format 16-bit signed linear
    fmt = AFMT_S16_LE;
    res = ::ioctl(m_fd, SNDCTL_DSP_SETFMT, &fmt);
    if (res < 0) {
	Debug(DebugWarn, "Unable to set format to 16-bit signed");
	return false;
    }

    // disable stereo mode
    fmt = 0;
    res = ::ioctl(m_fd, SNDCTL_DSP_STEREO, &fmt);
    if (res < 0) {
	Debug(DebugWarn, "Failed to set audio device to mono");
	return false;
    }

    // try to set the desired speed (8/16/32kHz) and check if it was actually set
    fmt = m_rate;
    res = ::ioctl(m_fd, SNDCTL_DSP_SPEED, &fmt);
    if (res < 0) {
	Debug(DebugWarn, "Failed to set audio device speed");
	return false;
    }
    if (fmt != (int)m_rate)
	Debug(DebugWarn, "Requested %d Hz, got %d Hz - sound may be choppy", m_rate, fmt);
    return true;
}

// Close and reopen the DSP device in a new mode
bool OssDevice::reOpen(int iomode)
{
    int fdesc = m_fd;
    m_fd = -1;
    ::ioctl(fdesc, SNDCTL_DSP_RESET);
    ::close(fdesc);
    fdesc = ::open(m_dev.c_str(), iomode | O_NONBLOCK);
    if (fdesc < 0) {
	Debug(DebugWarn, "Unable to re-open DSP device: %s", ::strerror(errno));
	return false;
    }
    m_fd = fdesc;
    return true;
}

// Make sure at least input mode is available
int OssDevice::setInputMode(bool force)
{
    if (m_fullDuplex || (m_readMode && !force))
	return 0;
    m_readMode = true;
    if (force || timePassed()) {
	if (reOpen(O_RDONLY) && setPcmFormat())
	    return 0;
	return -1;
    }
    return 1;
}

// Make sure at least output mode is available
int OssDevice::setOutputMode(bool force)
{
    if (m_fullDuplex || (!m_readMode && !force))
	return 0;
    m_readMode = false;
    if (force || timePassed()) {
	if (reOpen(O_WRONLY) && setPcmFormat())
	    return 0;
	return -1;
    }
    return 1;
}


bool OssHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    static const Regexp r("^oss/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (s_chan) {
	msg.setParam("error","busy");
	return false;
    }
    OssChan *chan = new OssChan(dest.matchString(1).c_str(),msg.getIntValue("rate",8000));
    if (!chan->init())
    {
	chan->destruct();
	return false;
    }
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    Debug(DebugInfo,"We are routing to device '%s'",dest.matchString(1).c_str());
    if (ch && chan->connect(ch,msg.getValue("reason"))) {
	chan->setTarget(msg.getValue("id"));
	msg.setParam("peerid",dest);
	msg.setParam("targetid",dest);
	chan->answer();
	chan->deref();
    }
    else {
        const char *direct = msg.getValue("direct");
	if (direct)
	{
	    Message m("call.execute");
	    m.addParam("module","oss");
	    m.addParam("id",dest);
	    m.addParam("caller",dest);
	    m.addParam("callto",direct);
	    m.userData(chan);
	    if (Engine::dispatch(m)) {
		chan->setTarget(m.getValue("targetid"));
		msg.addParam("targetid",chan->getTarget());
		chan->deref();
		return true;
	    }
	    Debug(DebugInfo,"OSS outgoing call not accepted!");
	    chan->destruct();
	    return false;
	}
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"OSS outgoing call with no target!");
	    chan->destruct();
	    return false;
	}
	Message m("call.route");
	m.addParam("module","oss");
	m.addParam("id",dest);
	m.addParam("caller",dest);
	m.addParam("called",targ);
	if (Engine::dispatch(m)) {
	    m = "call.execute";
	    m.addParam("callto",m.retValue());
	    m.retValue() = 0;
	    m.userData(chan);
	    if (Engine::dispatch(m)) {
		chan->setTarget(m.getValue("targetid"));
		msg.addParam("targetid",chan->getTarget());
		chan->deref();
		return true;
	    }
	    Debug(DebugInfo,"OSS outgoing call not accepted!");
	}
	else
	    Debug(DebugWarn,"OSS outgoing call but no route!");
	chan->destruct();
	return false;
    }

    return true;
}


bool StatusHandler::received(Message &msg)
{
    const String* sel = msg.getParam("module");
    if (sel && (*sel != "oss"))
	return false;
    msg.retValue() << "name=oss,type=misc;osschan=" << (s_chan != 0 ) << "\r\n";
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


bool MasqHandler::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (msg.getParam("message") && id.startsWith("oss/")) {
	msg = msg.getValue("message");
	msg.clearParam("message");
	if (s_chan) {
	    msg.addParam("targetid",s_chan->getTarget());
	    msg.userData(s_chan);
	}
    }
    return false;
}


bool AttachHandler::received(Message &msg)
{
    int more = 2;
    String src(msg.getValue("source"));
    if (src.null())
	more--;
    else {
	if (!src.startSkip("oss/",false))
	    src = "";
    }

    String cons(msg.getValue("consumer"));
    if (cons.null())
	more--;
    else {
	if (!cons.startSkip("oss/",false))
	    cons = "";
    }

    if (src.null() && cons.null())
	return false;

    if (src && cons && (src != cons)) {
	Debug(DebugWarn,"OSS asked to attach source '%s' and consumer '%s'",src.c_str(),cons.c_str());
	return false;
    }

    DataEndpoint *dd = static_cast<DataEndpoint*>(msg.userObject(YATOM("DataEndpoint")));
    if (!dd) {
	CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
	if (ch)
	    dd = ch->setEndpoint();
    }
    if (!dd) {
	Debug(DebugWarn,"OSS attach request with no control or data channel!");
	return false;
    }

    const String& name = src ? src : cons;
    int rate = msg.getIntValue("rate",8000);
    OssDevice* dev = new OssDevice(name,rate);
    if (dev->closed()) {
	dev->deref();
	dev = s_dev;
	if (dev && msg.getBoolValue("force")) {
	    Debug(DebugInfo,"OSS forcibly closing device '%s'",dev->device().c_str());
	    dev->close();
	    for (int i = 0; s_dev && (i < 10); i++)
		Thread::idle();
	    dev = new OssDevice(name,rate);
	    if (dev->closed()) {
		dev->deref();
		return false;
	    }
	}
	else
	    return false;
    }

    if (src) {
	OssSource* s = new OssSource(dev);
	if (s->init())
	    dd->setSource(s);
	s->deref();
    }

    if (cons) {
	OssConsumer* c = new OssConsumer(dev);
	if (c->init())
	    dd->setConsumer(c);
	c->deref();
    }

    dev->deref();

    // Stop dispatching if we handled all requested
    return !more;
}


OssPlugin::OssPlugin()
    : Plugin("osschan"),
      m_handler(0)
{
    Output("Loaded module OssChan");
}

void OssPlugin::initialize()
{
    Output("Initializing module OssChan");
    if (!m_handler) {
	m_handler = new OssHandler("call.execute");
	Engine::install(new DropHandler("call.drop"));
	Engine::install(new MasqHandler("chan.masquerade",10));
	Engine::install(m_handler);
	Engine::install(new StatusHandler);
	Engine::install(new AttachHandler);
    }
}

bool OssPlugin::isBusy() const
{
    return (s_chan != 0);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
