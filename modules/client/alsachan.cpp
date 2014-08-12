/**
 * alsachan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Alsa driver
 *
 * Copyright (C) 2005 Pablo Sampere
 * Derived from osschan.cpp Copyright (C) Null Team
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

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#if defined(__linux__)
#include <linux/soundcard.h>
#elif defined (__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#define MIN_SWITCH_TIME 600000

using namespace TelEngine;
namespace { // anonymous

static Mutex s_mutex(false,"AlsaChan");

class AlsaDevice;

class AlsaSource : public ThreadedSource
{
public:
    AlsaSource(AlsaDevice* dev);
    ~AlsaSource();
    bool init();
    virtual void run();
    virtual void cleanup();
private:
    AlsaDevice* m_device;
    unsigned m_brate;
    unsigned m_total;
};

class AlsaConsumer : public DataConsumer
{
public:
    AlsaConsumer(AlsaDevice* dev);
    ~AlsaConsumer();
    bool init();
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
private:
    AlsaDevice* m_device;
    unsigned m_total;
};

class AlsaDevice : public RefObject
{
public:
    AlsaDevice(const String& dev, unsigned int rate = 8000);
    ~AlsaDevice();
    bool timePassed(void);
    bool open();
    void close();
    int write(void *buffer, int frames);
    int read(void *buffer, int frames);
    inline bool closed() const
	{ return m_closed; }
    inline unsigned int rate() const
	{ return m_rate; }
    inline const String& device() const
	{ return m_dev; }
private:
    String m_dev;
    String m_dev_in;
    String m_dev_out;
    String m_initdata;
    bool m_closed;
    unsigned int m_rate;
    snd_pcm_t *m_handle_in;
    snd_pcm_t *m_handle_out;
    u_int64_t m_lastTime;
};

class AlsaChan : public CallEndpoint
{
public:
    AlsaChan(const String& dev, unsigned int rate = 8000);
    ~AlsaChan();
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

class AlsaHandler;

class AlsaPlugin : public Plugin
{
public:
    AlsaPlugin();
    virtual void initialize();
    virtual bool isBusy() const;
private:
    AlsaHandler *m_handler;
};

INIT_PLUGIN(AlsaPlugin);

class AlsaHandler : public MessageHandler
{
public:
    AlsaHandler(const char *name)
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

AlsaChan *s_chan = 0;
AlsaDevice* s_dev = 0;


bool AlsaSource::init()
{
    m_brate = 2 * m_device->rate();
    m_total = 0;
    start("Alsa Source",Thread::High);
    return true;
}

AlsaSource::AlsaSource(AlsaDevice* dev)
    : m_device(0)
{
    Debug(DebugNote,"AlsaSource::AlsaSource(%p) [%p]",dev,this);
    dev->ref();
    m_device = dev;
    if (dev->rate() != 8000)
	m_format << "/" << dev->rate();
}

AlsaSource::~AlsaSource()
{
    Debug(DebugNote,"AlsaSource::~AlsaSource() [%p] total=%u",this,m_total);
    m_device->deref();
}

void AlsaSource::run()
{
    int r = 1;
    DataBlock data(0,(m_brate*20)/1000);
    while ((r > 0) && looping()) {
	if (m_device->closed())
	    m_device->open();
	r = m_device->read(data.data(), data.length()/2);
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
	if (r*2 == (int)data.length())
	    Forward(data);
	else {
	    DataBlock d2(data.data(),r*2,false);
	    Forward(d2);
	    d2.clear(false);
	}
	m_total += r;
    }
    Debug(DebugWarn,"AlsaSource [%p] end of data",this);
}

void AlsaSource::cleanup()
{
    Debug(DebugNote,"AlsaSource [%p] cleanup, total=%u",this,m_total);
    ThreadedSource::cleanup();
}


bool AlsaConsumer::init()
{
    m_total = 0;
    return true;
}

AlsaConsumer::AlsaConsumer(AlsaDevice* dev)
    : m_device(0)
{
    Debug(DebugNote,"AlsaConsumer::AlsaConsumer(%p) [%p]",dev,this);
    dev->ref();
    m_device = dev;
    if (dev->rate() != 8000)
	m_format << "/" << dev->rate();
}

AlsaConsumer::~AlsaConsumer()
{
    Debug(DebugNote,"AlsaConsumer::~AlsaConsumer() [%p] total=%u",this,m_total);
    m_device->deref();
}

unsigned long AlsaConsumer::Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags)
{
    if (m_device->closed() || data.null())
	return 0;
    m_device->write(data.data(),data.length()/2);
    m_total += data.length();
    return invalidStamp();
}


AlsaChan::AlsaChan(const String& dev, unsigned int rate)
    : CallEndpoint("alsa"),
      m_dev(dev), m_rate(rate)
{
    Debug(DebugNote,"AlsaChan::AlsaChan('%s',%u) [%p]",dev.c_str(),rate,this);
    s_chan = this;
}

AlsaChan::~AlsaChan()
{
    Debug(DebugNote,"AlsaChan::~AlsaChan() [%p]",this);
    setTarget();
    setSource();
    setConsumer();
    s_chan = 0;
}

bool AlsaChan::init()
{
    if (s_dev)
	return false;
    AlsaDevice* dev = new AlsaDevice(m_dev,m_rate);
    if (dev->closed()) {
	dev->deref();
	return false;
    }
    AlsaSource* source = new AlsaSource(dev);
    dev->deref();
    if (!source->init()) {
	source->deref();
	return false;
    }
    setSource(source);
    source->deref();
    AlsaConsumer* cons = new AlsaConsumer(dev);
    if (!cons->init()) {
	cons->deref();
	setSource();
	return false;
    }
    setConsumer(cons);
    cons->deref();
    return true;
}


AlsaDevice::AlsaDevice(const String& dev, unsigned int rate)
    : m_dev(dev), m_dev_in(dev), m_dev_out(dev), m_closed(true), m_rate(rate),
      m_handle_in(0), m_handle_out(0)
{
    Debug(DebugNote,"AlsaDevice::AlsaDevice('%s',%u) [%p]",m_dev.c_str(),rate,this);
    int p = dev.find('/');
    if (p>0) {
        m_dev_in = dev.substr(0,p);
        int q = dev.substr(p+1).find('/');
        m_dev_out = dev.substr(p+1,q);
        if (m_dev_out.null()) m_dev_out = m_dev_in;
        if(q>0) m_initdata = dev.substr(p+2+q);
    }
    open();
};

bool AlsaDevice::open()
{
    int err;
    snd_pcm_hw_params_t *hw_params = NULL;
    unsigned int rate_in = m_rate;
    unsigned int rate_out = m_rate;
    int direction=0;
    snd_pcm_uframes_t period_size_in = 20 * 4;
    snd_pcm_uframes_t buffer_size_in= period_size_in * 16;
    snd_pcm_uframes_t period_size_out = 20 * 4;
    snd_pcm_uframes_t buffer_size_out= period_size_out * 16;
    snd_pcm_sw_params_t *swparams = NULL;
    Debug(DebugNote, "Opening ALSA input device %s",m_dev_in.c_str());
    Lock lock(s_mutex);
    if ((err = snd_pcm_open (&m_handle_in, m_dev_in.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
	Debug(DebugWarn, "cannot open audio device %s (%s)", m_dev.c_str(),snd_strerror (err));
	return false;
    }
    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
	Debug(DebugWarn, "cannot allocate hardware parameter structure (%s)", snd_strerror (err));
	return false;
    }
    if ((err = snd_pcm_hw_params_any (m_handle_in, hw_params)) < 0) Debug(DebugWarn, "cannot initialize hardware parameter structure (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_access (m_handle_in, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) Debug(DebugWarn, "cannot set access type (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_format (m_handle_in, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) Debug(DebugWarn, "cannot set sample format (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_rate_near (m_handle_in, hw_params, &rate_in, &direction)) < 0) Debug(DebugWarn, "cannot set sample rate %u (%s)", m_rate, snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_channels (m_handle_in, hw_params, 1)) < 0) Debug(DebugWarn, "cannot set channel count (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_period_size_near(m_handle_in, hw_params, &period_size_in, &direction)) < 0) Debug(DebugWarn, "cannot set period size (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_buffer_size_near(m_handle_in, hw_params, &buffer_size_in)) < 0) Debug(DebugWarn, "cannot set buffer size (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params (m_handle_in, hw_params)) < 0) Debug(DebugWarn, "cannot set parameters (%s)", snd_strerror (err));
    snd_pcm_hw_params_free (hw_params);

    Debug(DebugNote, "Opening ALSA output device %s",m_dev_out.c_str());
    if ((err = snd_pcm_open (&m_handle_out, m_dev_out.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
	Debug(DebugWarn, "cannot open audio device %s (%s)", m_dev.c_str(), snd_strerror (err));
	return false;
    }

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
	Debug(DebugWarn, "cannot allocate hardware parameter structure (%s)", snd_strerror (err));
	return false;
    }
    if ((err = snd_pcm_hw_params_any (m_handle_out, hw_params)) < 0) Debug(DebugWarn, "cannot initialize hardware parameter structure (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_access (m_handle_out, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) Debug(DebugWarn, "cannot set access type (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_format (m_handle_out, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) Debug(DebugWarn, "cannot set sample format (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_rate_near (m_handle_out, hw_params, &rate_out, 0)) < 0) Debug(DebugWarn, "cannot set sample rate %u (%s)", m_rate, snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_channels (m_handle_out, hw_params, 1)) < 0) Debug(DebugWarn, "cannot set channel count (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_period_size_near(m_handle_out, hw_params, &period_size_out, &direction)) < 0) Debug(DebugWarn, "cannot set period size (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params_set_buffer_size_near(m_handle_out, hw_params, &buffer_size_out)) < 0) Debug(DebugWarn, "cannot set buffer size (%s)", snd_strerror (err));
    if ((err = snd_pcm_hw_params (m_handle_out, hw_params)) < 0) Debug(DebugWarn, "cannot set parameters (%s)", snd_strerror (err));
    snd_pcm_hw_params_free (hw_params);

    snd_pcm_uframes_t val;

    snd_pcm_sw_params_alloca(&swparams);
    snd_pcm_sw_params_current(m_handle_out, swparams);

    if ((err = snd_pcm_sw_params_get_start_threshold( swparams, &val)) < 0) Debug(DebugWarn, "cannot get start threshold: (%s)", snd_strerror (err));
    if ((err = snd_pcm_sw_params_get_stop_threshold( swparams, &val)) < 0) Debug(DebugWarn, "cannot get stop threshold: (%s)", snd_strerror (err));
    if ((err = snd_pcm_sw_params_get_boundary( swparams, &val)) < 0) Debug(DebugWarn, "cannot get boundary: (%s)", snd_strerror (err));
    if ((err = snd_pcm_sw_params_set_silence_threshold(m_handle_out, swparams, 0)) < 0) Debug(DebugWarn, "cannot set silence threshold: (%s)", snd_strerror (err));
    if ((err = snd_pcm_sw_params_set_silence_size(m_handle_out, swparams, 0)) < 0) Debug(DebugWarn, "cannot set silence size: (%s)", snd_strerror (err));
    if ((err = snd_pcm_sw_params(m_handle_out, swparams)) < 0) Debug(DebugWarn, "cannot set sw param: (%s)", snd_strerror (err));

    Debug(DebugNote, "Alsa(%s/%s) %u/%u %u/%u %u/%u", m_dev_in.c_str(),m_dev_out.c_str(),rate_in,rate_out,(unsigned int)period_size_in,(unsigned int)period_size_out,(unsigned int)buffer_size_in,(unsigned int)buffer_size_out);
    m_closed = false;
    m_lastTime = Time::now() + MIN_SWITCH_TIME;
    if (!s_dev)
	s_dev = this;
    return true;
}

void AlsaDevice::close()
{
    m_closed = true;
    if (m_handle_in) {
	snd_pcm_drop (m_handle_in);
	snd_pcm_close (m_handle_in);
	m_handle_in = 0;
    }
    if (m_handle_out) {
	snd_pcm_drop (m_handle_out);
	snd_pcm_close (m_handle_out);
	m_handle_out = 0;
    }
    if (s_dev == this)
	s_dev = 0;
}

AlsaDevice::~AlsaDevice()
{
    Debug(DebugNote,"AlsaDevice::~AlsaDevice [%p]",this);
    close();
}

int AlsaDevice::read(void *buffer, int frames)
{
    if (closed() || !m_handle_in)
	return 0;
    int rc = ::snd_pcm_readi(m_handle_in, buffer, frames);
    if (rc <= 0) {
	int err = rc;
	if (err == -EPIPE) {    /* under-run */
	    Debug(DebugWarn, "ALSA read underrun: %s", snd_strerror(err));
	    err = snd_pcm_prepare(m_handle_in);
	    if (err < 0)
		Debug(DebugWarn, "ALSA read can't recover from underrun, prepare failed: %s", snd_strerror(err));
	    return 0;
	} else if (err == -ESTRPIPE) {
	    while ((err = snd_pcm_resume(m_handle_in)) == -EAGAIN)
		sleep(1);       /* wait until the suspend flag is released */
	    if (err < 0) {
		err = snd_pcm_prepare(m_handle_in);
		if (err < 0)
		    Debug(DebugWarn, "ALSA read can't recover from suspend, prepare failed: %s", snd_strerror(err));
	    }
	    return 0;
	}
	return err;
    }
    return rc;
}

int AlsaDevice::write(void *buffer, int frames)
{
    if (closed() || !m_handle_out)
	return 0;
    int rc = snd_pcm_writei(m_handle_out, buffer, frames);
    if (rc == -EPIPE) {
	Debug(DebugWarn, "ALSA write underrun occurred");
	snd_pcm_prepare(m_handle_out);
	DDebug(DebugInfo, "ALSA write underrun fix frame 1");
	rc = snd_pcm_writei(m_handle_out, buffer, frames);
	if (rc == -EPIPE)
	    snd_pcm_prepare(m_handle_out);
	DDebug(DebugInfo, "ALSA write underrun fix frame 2");
	rc = snd_pcm_writei(m_handle_out, buffer, frames); //to catch-up missed time
    	if (rc == -EPIPE)
	    snd_pcm_prepare(m_handle_out);
    } else if (rc < 0)
	Debug(DebugWarn,"ALSA error from writei: %s",::snd_strerror(rc));
    else if (rc != (int)frames)
	Debug(DebugWarn,"ALSA short write, writei wrote %d frames", rc);
    return rc;
}

bool AlsaDevice::timePassed(void)
{
    return Time::now() > m_lastTime;
}


void AlsaChan::disconnected(bool final, const char *reason)
{
    Debugger debug("AlsaChan::disconnected()"," '%s' [%p]",reason,this);
    setTarget();
}

void AlsaChan::answer()
{
    Message* m = new Message("call.answered");
    m->addParam("module","alsa");
    String tmp("alsa/");
    tmp += m_dev;
    m->addParam("id",tmp);
    if (m_target)
	m->addParam("targetid",m_target);
    Engine::enqueue(m);
}


bool AlsaHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    static const Regexp r("^alsa/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (s_chan) {
	msg.setParam("error","busy");
	return false;
    }
    AlsaChan *chan = new AlsaChan(dest.matchString(1).c_str(),msg.getIntValue("rate",8000));
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
	if (direct) {
	    Message m("call.execute");
	    m.addParam("module","alsa");
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
	    Debug(DebugInfo,"Alsa outgoing call not accepted!");
	    chan->destruct();
	    return false;
	}
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"Alsa outgoing call with no target!");
	    chan->destruct();
	    return false;
	}
	Message m("call.route");
	m.addParam("module","alsa");
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
	    Debug(DebugInfo,"Alsa outgoing call not accepted!");
	}
	else
	    Debug(DebugWarn,"Alsa outgoing call but no route!");
	chan->destruct();
	return false;
    }

    return true;
}


bool StatusHandler::received(Message &msg)
{
    const String* sel = msg.getParam("module");
    if (sel && (*sel != "alsa"))
	return false;
    msg.retValue() << "name=alsa,type=misc;alsachan=" << (s_chan != 0 ) << "\r\n";
    return false;
}


bool DropHandler::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (id.null() || id.startsWith("alsa/")) {
	if (s_chan) {
	    Debug("AlsaDropper",DebugInfo,"Dropping call");
	    s_chan->disconnect();
	}
	return !id.null();
    }
    return false;
}


bool MasqHandler::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (msg.getParam("message") && id.startsWith("alsa/")) {
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
    if (s_dev && !msg.getBoolValue("force"))
	return false;
    int more = 2;
    String src(msg.getValue("source"));
    if (src.null())
	more--;
    else {
	if (!src.startSkip("alsa/",false))
	    src = "";
    }

    String cons(msg.getValue("consumer"));
    if (cons.null())
	more--;
    else {
	if (!cons.startSkip("alsa/",false))
	    cons = "";
    }

    if (src.null() && cons.null())
	return false;

    if (src && cons && (src != cons)) {
	Debug(DebugWarn,"Alsa asked to attach source '%s' and consumer '%s'",src.c_str(),cons.c_str());
	return false;
    }

    DataEndpoint *dd = static_cast<DataEndpoint*>(msg.userObject(YATOM("DataEndpoint")));
    if (!dd) {
	CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
	if (ch)
	    dd = ch->setEndpoint();
    }
    if (!dd) {
	Debug(DebugWarn,"Alsa attach request with no control or data channel!");
	return false;
    }

    const String& name = src ? src : cons;
    int rate = msg.getIntValue("rate",8000);
    AlsaDevice* dev = new AlsaDevice(name,rate);
    if (dev->closed()) {
	dev->deref();
	dev = s_dev;
	if (dev) {
	    Debug(DebugInfo,"Alsa forcibly closing device '%s'",dev->device().c_str());
	    dev->close();
	    for (int i = 0; s_dev && (i < 10); i++)
		Thread::idle();
	    dev = new AlsaDevice(name,rate);
	    if (dev->closed()) {
		dev->deref();
		return false;
	    }
	}
	else
	    return false;
    }

    if (src) {
	AlsaSource* s = new AlsaSource(dev);
	if (s->init())
	    dd->setSource(s);
	s->deref();
    }

    if (cons) {
	AlsaConsumer* c = new AlsaConsumer(dev);
	if (c->init())
	    dd->setConsumer(c);
	c->deref();
    }

    dev->deref();

    // Stop dispatching if we handled all requested
    return !more;
}


AlsaPlugin::AlsaPlugin()
    : Plugin("alsachan"),
      m_handler(0)
{
    Output("Loaded module AlsaChan");
}

void AlsaPlugin::initialize()
{
    Output("Initializing module AlsaChan");
    if (!m_handler) {
	m_handler = new AlsaHandler("call.execute");
	Engine::install(new DropHandler("call.drop"));
	Engine::install(new MasqHandler("chan.masquerade",10));
	Engine::install(m_handler);
	Engine::install(new StatusHandler);
	Engine::install(new AttachHandler);
    }
}

bool AlsaPlugin::isBusy() const
{
    return (s_dev != 0);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
