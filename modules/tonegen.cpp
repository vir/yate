/**
 * tonegen.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Tones generator
 */

#include <telengine.h>
#include <telephony.h>

#include <unistd.h>
#include <string.h>

using namespace TelEngine;

static ObjList tones;
static ObjList chans;

typedef struct {
    int nsamples;
    const short *data;
} Tone;

class ToneSource : public ThreadedSource
{
public:
    ~ToneSource();
    virtual void run();
    inline const String &name()
	{ return m_name; }
    static ToneSource *getTone(const String &tone);
private:
    ToneSource(const String &tone);
    static const Tone *getBlock(const String &tone);
    String m_name;
    const Tone *m_tone;
    unsigned m_brate;
    unsigned m_total;
    unsigned long long m_time;
};

class ToneChan : public DataEndpoint
{
public:
    ToneChan(const String &tone);
    ~ToneChan();
    virtual void disconnected();
};

class ToneHandler : public MessageHandler
{
public:
    ToneHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("status") { }
    virtual bool received(Message &msg);
};

class ToneGenPlugin : public Plugin
{
public:
    ToneGenPlugin();
    ~ToneGenPlugin();
    virtual void initialize();
private:
    ToneHandler *m_handler;
};

// 421.052Hz (19 samples @ 8kHz) sine wave, pretty close to standard 425Hz
static const short tone421hz[] = {
    19,
    3246,6142,8371,9694,9965,9157,7357,4759,1645,
    -1645,-4759,-7357,-9157,-9965,-9694,-8371,-6142,-3246,
    0 };

static const Tone t_dial[] = { { 8000, tone421hz }, { 0, 0 } };

static const Tone t_busy[] = { { 4000, tone421hz }, { 4000, 0 }, { 0, 0 } };

static const Tone t_specdial[] = { { 7600, tone421hz }, { 400, 0 }, { 0, 0 } };

static const Tone t_ring[] = { { 8000, tone421hz }, { 32000, 0 }, { 0, 0 } };

ToneSource::ToneSource(const String &tone)
    : m_name(tone), m_tone(0), m_brate(16000), m_total(0), m_time(0)
{
    Debug(DebugAll,"ToneSource::ToneSource(\"%s\") [%p]",tone.c_str(),this);
    m_tone = getBlock(tone);
    tones.append(this);
    if (m_tone)
	start("ToneSource");
}

ToneSource::~ToneSource()
{
    Debug(DebugAll,"ToneSource::~ToneSource() [%p] total=%u",this,m_total);
    if (m_time) {
	m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*1000000ULL + m_time/2) / m_time;
	    Debug(DebugInfo,"ToneSource rate=%llu b/s",m_time);
	}
    }
    tones.remove(this,false);
}

const Tone *ToneSource::getBlock(const String &tone)
{
    if (tone == "dial" || tone == "dt")
	return t_dial;
    else if (tone == "busy" || tone == "bs")
	return t_busy;
    else if (tone == "ring" || tone == "rt")
	return t_ring;
    else if (tone == "specdial" || tone == "sd")
	return t_specdial;
    Debug(DebugWarn,"No waveform is defined for tone '%s'",tone.c_str());
    return 0;
}

ToneSource *ToneSource::getTone(const String &tone)
{
    ObjList *l = &tones;
    for (; l; l = l->next()) {
	ToneSource *t = static_cast<ToneSource *>(l->get());
	if (t && (t->name() == tone)) {
	    t->ref();
	    return t;
	}
    }
    return new ToneSource(tone);
}

void ToneSource::run()
{
    Debug(DebugAll,"ToneSource::run() [%p]",this);
    unsigned long long tpos = Time::now();
    m_time = tpos;
    DataBlock data(0,480);
    int samp = 0; // sample number
    int dpos = 1; // position in data
    const Tone *tone = m_tone;
    int nsam = tone->nsamples;
    while (m_tone) {
	short *d = (short *) data.data();
	for (unsigned int i = data.length()/2; i--; samp++,dpos++) {
	    if (samp >= nsam) {
		samp = 0;
		const Tone *otone = tone;
		tone++;
		if (!tone->nsamples)
		    tone = m_tone;
		nsam = tone->nsamples;
		if (tone != otone)
		    dpos = 1;
	    }
	    if (tone->data) {
		if (dpos > tone->data[0])
		    dpos = 1;
		*d++ = tone->data[dpos];
	    }
	    else
		*d++ = 0;
	}
	long long dly = tpos - Time::now();
	if (dly > 0) {
#ifdef DEBUG
	    Debug("ToneSource",DebugAll,"Sleeping for %lld usec",dly);
#endif
	    ::usleep((unsigned long)dly);
	}
	Forward(data);
	m_total += data.length();
	tpos += (data.length()*1000000ULL/m_brate);
    };
    m_time = Time::now() - m_time;
    m_time = (m_total*1000000ULL + m_time/2) / m_time;
    Debug(DebugAll,"ToneSource [%p] end, total=%u (%llu b/s)",this,m_total,m_time);
    m_time = 0;
}

ToneChan::ToneChan(const String &tone)
    : DataEndpoint("tone")
{
    Debug(DebugAll,"ToneChan::ToneChan(\"%s\") [%p]",tone.c_str(),this);
    chans.append(this);
    ToneSource *t = ToneSource::getTone(tone);
    if (t) {
	setSource(t);
	t->deref();
    }
}

ToneChan::~ToneChan()
{
    Debug(DebugAll,"ToneChan::~ToneChan() [%p]",this);
    chans.remove(this,false);
}

void ToneChan::disconnected()
{
    Debugger debug("ToneChan::disconnected()"," [%p]",this);
    destruct();
}

bool ToneHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^tone/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    String tone = dest.matchString(1);
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd)
	dd->connect(new ToneChan(tone));
    else {
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"Tone outgoing call with no target!");
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
	    ToneChan *tc = new ToneChan(dest.matchString(1).c_str());
	    m.userData(tc);
	    if (Engine::dispatch(m))
		return true;
	    Debug(DebugFail,"Tone outgoing call not accepted!");
	    delete tc;
	}
	else
	    Debug(DebugWarn,"Tone outgoing call but no route!");
	return false;
    }
    return true;
}

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"tonegen"))
	return false;
    msg.retValue() << "tonegen,tones=" << tones.count()
		   << ",chans=" << chans.count() << "\n";
    return false;
}

ToneGenPlugin::ToneGenPlugin()
    : m_handler(0)
{
    Output("Loaded module ToneGen");
}

ToneGenPlugin::~ToneGenPlugin()
{
    Output("Unloading module ToneGen");
    ObjList *l = &chans;
    while (l) {
	ToneChan *t = static_cast<ToneChan *>(l->get());
	if (t)
	    t->disconnect();
	if (l->get() == t)
	    l = l->next();
    }
    chans.clear();
    tones.clear();
}

void ToneGenPlugin::initialize()
{
    Output("Initializing module ToneGen");
    if (!m_handler) {
	m_handler = new ToneHandler("call");
	Engine::install(m_handler);
	Engine::install(new StatusHandler);
    }
}

INIT_PLUGIN(ToneGenPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
