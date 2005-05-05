/**
 * tonegen.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Tones generator
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

#include <string.h>

using namespace TelEngine;

static ObjList tones;

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
    DataBlock m_data;
    unsigned m_brate;
    unsigned m_total;
    u_int64_t m_time;
};

class ToneChan : public Channel
{
public:
    ToneChan(const String &tone);
    ~ToneChan();
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message &msg);
};

class ToneGenDriver : public Driver
{
public:
    ToneGenDriver();
    ~ToneGenDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
protected:
    void statusModule(String& str);
    void statusParams(String& str);
private:
    AttachHandler* m_handler;
};

INIT_PLUGIN(ToneGenDriver);

// 421.052Hz (19 samples @ 8kHz) sine wave, pretty close to standard 425Hz
static const short tone421hz[] = {
    19,
    3246,6142,8371,9694,9965,9157,7357,4759,1645,
    -1645,-4759,-7357,-9157,-9965,-9694,-8371,-6142,-3246,
    0 };

// 1000Hz (8 samples @ 8kHz) standard digital milliwatt
static const short tone1000hz[] = {
    8,
    8828, 20860, 20860, 8828,
    -8828, -20860, -20860, -8828
    };

static const Tone t_dial[] = { { 8000, tone421hz }, { 0, 0 } };

static const Tone t_busy[] = { { 4000, tone421hz }, { 4000, 0 }, { 0, 0 } };

static const Tone t_specdial[] = { { 7600, tone421hz }, { 400, 0 }, { 0, 0 } };

static const Tone t_ring[] = { { 8000, tone421hz }, { 32000, 0 }, { 0, 0 } };

static const Tone t_congestion[] = { { 2000, tone421hz }, { 2000, 0 }, { 0, 0 } };

static const Tone t_outoforder[] = {
    { 800, tone421hz }, { 800, 0 },
    { 800, tone421hz }, { 800, 0 },
    { 800, tone421hz }, { 800, 0 },
    { 1600, tone421hz }, { 1600, 0 },
    { 0, 0 } };

static const Tone t_mwatt[] = { { 8000, tone1000hz }, { 0, 0 } };

ToneSource::ToneSource(const String &tone)
    : m_name(tone), m_tone(0), m_data(0,480), m_brate(16000), m_total(0), m_time(0)
{
    Debug(DebugAll,"ToneSource::ToneSource(\"%s\") [%p]",tone.c_str(),this);
    m_tone = getBlock(tone);
    tones.append(this);
    if (m_tone)
	start("ToneSource");
}

ToneSource::~ToneSource()
{
    Lock lock(__plugin);
    Debug(DebugAll,"ToneSource::~ToneSource() [%p] total=%u stamp=%lu",this,m_total,timeStamp());
    tones.remove(this,false);
    if (m_time) {
	m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*(u_int64_t)1000000 + m_time/2) / m_time;
	    Debug(DebugInfo,"ToneSource rate=" FMT64U " b/s",m_time);
	}
    }
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
    else if (tone == "congestion" || tone == "cg")
	return t_congestion;
    else if (tone == "outoforder" || tone == "oo")
	return t_outoforder;
    else if (tone == "milliwatt" || tone == "mw")
	return t_mwatt;
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
    u_int64_t tpos = Time::now();
    m_time = tpos;
    int samp = 0; // sample number
    int dpos = 1; // position in data
    const Tone *tone = m_tone;
    int nsam = tone->nsamples;
    while (m_tone) {
	Thread::check();
	short *d = (short *) m_data.data();
	for (unsigned int i = m_data.length()/2; i--; samp++,dpos++) {
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
	int64_t dly = tpos - Time::now();
	if (dly > 0) {
	    XDebug("ToneSource",DebugAll,"Sleeping for " FMT64 " usec",dly);
	    Thread::usleep((unsigned long)dly);
	}
	Forward(m_data,m_data.length()/2);
	m_total += m_data.length();
	tpos += (m_data.length()*(u_int64_t)1000000/m_brate);
    };
    m_time = Time::now() - m_time;
    m_time = (m_total*(u_int64_t)1000000 + m_time/2) / m_time;
    Debug(DebugAll,"ToneSource [%p] end, total=%u (" FMT64U " b/s)",this,m_total,m_time);
    m_time = 0;
}

ToneChan::ToneChan(const String &tone)
    : Channel(__plugin)
{
    Debug(DebugAll,"ToneChan::ToneChan(\"%s\") [%p]",tone.c_str(),this);
    ToneSource *t = ToneSource::getTone(tone);
    if (t) {
	setSource(t);
	t->deref();
    }
    else
	Debug(DebugWarn,"No source tone '%s' in ToneChan [%p]",tone.c_str(),this);
}

ToneChan::~ToneChan()
{
    Debug(DebugAll,"ToneChan::~ToneChan() %s [%p]",id().c_str(),this);
}

bool ToneGenDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	ToneChan *tc = new ToneChan(dest);
	if (ch->connect(tc))
	    tc->deref();
	else {
	    tc->destruct();
	    return false;
	}
    }
    else {
	Message m("call.route");
	m.addParam("module",name());
	String callto(msg.getValue("direct"));
	if (callto.null()) {
	    const char *targ = msg.getValue("target");
	    if (!targ) {
		Debug(DebugWarn,"Tone outgoing call with no target!");
		return false;
	    }
	    callto = msg.getValue("caller");
	    if (callto.null())
		callto << prefix() << dest;
	    m.addParam("called",targ);
	    m.addParam("caller",callto);
	    if (!Engine::dispatch(m)) {
		Debug(DebugWarn,"Tone outgoing call but no route!");
		return false;
	    }
	    callto = m.retValue();
	    m.retValue().clear();
	}
	m = "call.execute";
	m.addParam("callto",callto);
	ToneChan *tc = new ToneChan(dest);
	m.setParam("targetid",tc->id());
	m.userData(tc);
	if (Engine::dispatch(m)) {
	    tc->deref();
	    return true;
	}
	Debug(DebugWarn,"Tone outgoing call not accepted!");
	tc->destruct();
	return false;
    }
    return true;
}

bool AttachHandler::received(Message &msg)
{
    String src(msg.getValue("source"));
    if (src.null())
	return false;
    Regexp r("^tone/\\(.*\\)$");
    if (!src.matches(r))
	return false;
    src = src.matchString(1);
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd) {
	Lock lock(__plugin);
	ToneSource *t = ToneSource::getTone(src);
	if (t) {
	    dd->setSource(t);
	    t->deref();
	    // Let the message flow if it wants to attach a consumer too
	    return !msg.getValue("consumer");
	}
	Debug(DebugWarn,"No source tone '%s' could be attached to [%p]",src.c_str(),dd);
    }
    else
	Debug(DebugWarn,"Tone '%s' attach request with no data channel!",src.c_str());
    return false;
}

void ToneGenDriver::statusModule(String& str)
{
    Module::statusModule(str);
}

void ToneGenDriver::statusParams(String& str)
{
    str << "tones=" << tones.count() << ",chans=" << channels().count();
}

ToneGenDriver::ToneGenDriver()
    : Driver("tone","misc"), m_handler(0)
{
    Output("Loaded module ToneGen");
}

ToneGenDriver::~ToneGenDriver()
{
    Output("Unloading module ToneGen");
    ObjList *l = &channels();
    while (l) {
	ToneChan *t = static_cast<ToneChan *>(l->get());
	if (t)
	    t->disconnect("shutdown");
	if (l->get() == t)
	    l = l->next();
    }
    lock();
    channels().clear();
    tones.clear();
    unlock();
}

void ToneGenDriver::initialize()
{
    Output("Initializing module ToneGen");
    setup(0,true); // no need to install notifications
    Driver::initialize();
    if (!m_handler) {
	m_handler = new AttachHandler;
	Engine::install(m_handler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
