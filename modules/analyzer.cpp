/**
 * analyzer.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Test call generator and audio quality analyzer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
 *
 * FFT routine taken from Murphy McCauley's FFT DLL based in turn on the
 *  work of Don Cross <dcross@intersrv.com>
 * See http://www.fullspectrum.com/deeth/programming/fft.html
 *  and http://www.constantthought.com/
 * Thank both for a sleek routine that doesn't even fill a floppy ;-)
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
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_2PI
#define M_2PI (2*M_PI)
#endif

using namespace TelEngine;

// Asynchronous FFT on a power of 2 sample buffer
class AsyncFFT : public Thread
{
public:
    virtual ~AsyncFFT();
    static AsyncFFT* create(unsigned int length, Priority prio = Low);
    inline unsigned int length() const
	{ return m_length; }
    inline bool ready() const
	{ return m_ready; }
    double operator[](int index) const;
    bool prepare(const short* samples);
    inline void stop()
	{ m_stop = true; }
    virtual void run();
private:
    AsyncFFT(unsigned int length, Priority prio);
    unsigned int revBits(unsigned int index, unsigned int numBits);
    void compute();
    bool m_ready;
    bool m_start;
    bool m_stop;
    unsigned int m_length;
    double* m_real;
    double* m_imag;
    unsigned int m_nBits;
};

class AnalyzerCons : public DataConsumer
{
    YCLASS(AnalyzerCons,DataConsumer)
public:
    AnalyzerCons(const String& type);
    virtual ~AnalyzerCons();
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    virtual void statusParams(String& str);
protected:
    DataBlock m_data;
    u_int64_t m_timeStart;
    unsigned long m_tsStart;
    unsigned int m_tsGapCount;
    unsigned long m_tsGapLength;
    AsyncFFT* m_spectrum;
};

class AnalyzerChan : public Channel
{
public:
    AnalyzerChan(const String& type, bool outgoing);
    virtual ~AnalyzerChan();
    virtual void statusParams(String& str);
    virtual bool callRouted(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    void startChannel(NamedList& params);
    void addSource();
    void addConsumer();
protected:
    void localParams(String& str);
    u_int64_t m_timeStart;
    unsigned long m_timeRoute;
    unsigned long m_timeRing;
    unsigned long m_timeAnswer;
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message& msg);
};

class AnalyzerDriver : public Driver
{
public:
    AnalyzerDriver();
    virtual ~AnalyzerDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    bool startCall(NamedList& params, const String& dest);
private:
    AttachHandler* m_handler;
};

INIT_PLUGIN(AnalyzerDriver);

static int s_res = 1;

static const char* printTime(char* buf,unsigned long usec)
{
    switch (s_res) {
	case 2:
	    // microsecond resolution
	    sprintf(buf,"%u.%06u",(unsigned int)(usec / 1000000),(unsigned int)(usec % 1000000));
	    break;
	case 1:
	    // millisecond resolution
	    usec = (usec + 500) / 1000;
	    sprintf(buf,"%u.%03u",(unsigned int)(usec / 1000),(unsigned int)(usec % 1000));
	    break;
	default:
	    // 1-second resolution
	    usec = (usec + 500000) / 1000000;
	    sprintf(buf,"%u",(unsigned int)usec);
    }
    return buf;
}


AsyncFFT* AsyncFFT::create(unsigned int length, Priority prio)
{
    if (length < 2)
	return 0;
    // thanks to 'byang' for this cute power of two test!
    if (length & (length - 1))
	return 0;
    AsyncFFT* fft = new AsyncFFT(length,prio);
    if (fft->startup())
	return fft;
    delete fft;
    return 0;
}

AsyncFFT::AsyncFFT(unsigned int length, Priority prio)
    : Thread("AsyncFFT",prio), m_ready(false), m_start(false), m_stop(false),
      m_length(0), m_real(0), m_imag(0), m_nBits(0)
{
    for (unsigned int i = 0; i <= 256 ;i++)
	if (m_length & (1 << i)) {
	    m_nBits = i;
	    break;
	}
    if (!m_nBits)
	return;
    m_real = new double[m_length];
    m_imag = new double[m_length];
}

AsyncFFT::~AsyncFFT()
{
    m_ready = false;
    m_start = false;
    delete[] m_real;
    delete[] m_imag;
}

double AsyncFFT::operator[](int index) const
{
    if ((index < 0) || ((unsigned int)index > (m_length >> 1)))
	return 0.0;
    if (!m_ready)
	return 0.0;
    return m_real[index];
}

void AsyncFFT::run()
{
    while (!m_stop) {
	while (!m_start)
	    Thread::msleep(5);
	if (m_stop)
	    return;
	m_ready = false;
	compute();
	m_ready = true;
	m_start = false;
    }
}

bool AsyncFFT::prepare(const short* samples)
{
    if (m_start || m_stop || !(samples && m_real))
	return false;
    m_ready = false;
    XDebug(&__plugin,DebugAll,"Preparing FFT buffer from %u samples [%p]",m_length,this);
    unsigned int i, j;
    for (i = 0; i < m_length; i++) {
	j = revBits(i,m_nBits);
	m_real[i] = samples[j];
	m_imag[i] = 0.0;
    }
    m_start = true;
    return true;
}

unsigned int AsyncFFT::revBits(unsigned int index, unsigned int numBits)
{
    unsigned int i, rev;
    for (i = rev = 0; i < numBits; i++) {
	rev = (rev << 1) | (index & 1);
	index >>= 1;
    }
    return rev;
}

void AsyncFFT::compute()
{
#ifdef XDEBUG
    Debug(&__plugin,DebugAll,"Computing FFT with length %u [%p]",m_length,this);
    Time t;
#endif
    unsigned int i, j, n;
    unsigned int blockEnd = 1;
    for (unsigned int blockSize = 2; blockSize <= m_length; blockSize <<= 1) {
	double delta_angle = M_2PI / blockSize;
	double sm1 = ::sin(-delta_angle);
	double sm2 = ::sin(-2 * delta_angle);
	double cm1 = ::cos(-delta_angle);
	double cm2 = ::cos(-2 * delta_angle);
	double w = 2 * cm1;
	double ar[3], ai[3];

	for (i = 0; i < m_length; i += blockSize) {
	    ar[1] = cm1;
	    ai[1] = sm1;
	    ar[2] = cm2;
	    ai[2] = sm2;

	    if (m_stop)
		return;
	    for (j = i, n = 0; n < blockEnd; j++, n++) {
		ar[0] = w*ar[1] - ar[2];
		ar[2] = ar[1];
		ar[1] = ar[0];
		ai[0] = w*ai[1] - ai[2];
		ai[2] = ai[1];
		ai[1] = ai[0];

		unsigned int k = j + blockEnd;
		double tr = ar[0]*m_real[k] - ai[0]*m_imag[k];
		double ti = ar[0]*m_imag[k] + ai[0]*m_real[k];
		m_real[k] = m_real[j] - tr;
		m_imag[k] = m_imag[j] - ti;
		m_real[j] += tr;
		m_imag[j] += ti;
	    }
	}
    }
#ifdef XDEBUG
    Debug(&__plugin,DebugAll,"Computing FFT with length %u took " FMT64U " usec [%p]",
	m_length,Time::now()-t,this);
#endif
}


AnalyzerCons::AnalyzerCons(const String& type)
    : m_timeStart(0), m_tsStart(0), m_tsGapCount(0), m_tsGapLength(0),
      m_spectrum(false)
{
    unsigned int len = 0;
    if (type == "spectrum")
	len = 1024;
    else if (type == "fft1024")
	len = 1024;
    else if (type == "fft512")
	len = 512;
    else if (type == "fft256")
	len = 256;
    else if (type == "fft128")
	len = 128;
    if (len)
	m_spectrum = AsyncFFT::create(len);
}

AnalyzerCons::~AnalyzerCons()
{
    if (m_spectrum) {
	AsyncFFT* tmp = m_spectrum;
	m_spectrum = 0;
	tmp->stop();
    }
}

void AnalyzerCons::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (!m_timeStart) {
	// the first data block may be garbled or have bad timestamp - ignore
	m_timeStart = Time::now();
	m_tsStart = tStamp;
	return;
    }
    unsigned int samples = data.length() / 2;
    long delta = tStamp - timeStamp() - samples;
    if (delta) {
	XDebug(&__plugin,DebugMild,"Got %u samples with ts=%lu but old ts=%lu (delta=%ld)",
	    samples,tStamp,timeStamp(),delta);
	if (delta < 0)
	    delta = -delta;
	m_tsGapCount++;
	m_tsGapLength += delta;
    }
    if (!m_spectrum)
	return;
    m_data += data;
    unsigned int len = 2 * m_spectrum->length();
    if (m_data.length() < len)
	return;
    // limit the length of the buffer
    int toCut = data.length() - (2 * len);
    if (toCut > 0)
	m_data.cut(-toCut);
    if (m_spectrum->prepare((const short*)m_data.data()))
	m_data.cut(-(int)len);
}

void AnalyzerCons::statusParams(String& str)
{
    unsigned long samples = timeStamp() - m_tsStart;
    str.append("gaps=",",") << m_tsGapCount;
    str << ",gaplen=" << (unsigned int)m_tsGapLength;
    str << ",samples=" << (unsigned int)samples;
    if (m_timeStart) {
	u_int64_t utime = Time::now() - m_timeStart;
	utime = utime ? ((1000000 * (u_int64_t)samples) + (utime / 2)) / utime : 0;
	str << ",rate=" << (unsigned int)utime;
    }
}


AnalyzerChan::AnalyzerChan(const String& type, bool outgoing)
    : Channel(__plugin,0,outgoing),
      m_timeStart(Time::now()), m_timeRoute(0), m_timeRing(0), m_timeAnswer(0)
{
    Debug(this,DebugAll,"AnalyzerChan::AnalyzerChan('%s',%s) [%p]",
	type.c_str(),String::boolText(outgoing),this);
    m_address = type;
}

AnalyzerChan::~AnalyzerChan()
{
    Debug(this,DebugAll,"AnalyzerChan::~AnalyzerChan() %s [%p]",id().c_str(),this);
    RefPointer<AnalyzerCons> cons = YOBJECT(AnalyzerCons,getConsumer());
    char buf[32];
    printTime(buf,(unsigned int)(Time::now() - m_timeStart));
    String str(status());
    localParams(str);
    str.append("totaltime=",",") << buf;
    if (cons)
	cons->statusParams(str);
    Output("Analyzer %s finished: %s",id().c_str(),str.c_str());
    Engine::enqueue(message("chan.hangup"));
}

void AnalyzerChan::statusParams(String& str)
{
    Channel::statusParams(str);
    localParams(str);
    RefPointer<AnalyzerCons> cons = YOBJECT(AnalyzerCons,getConsumer());
    if (cons)
	cons->statusParams(str);
}

void AnalyzerChan::localParams(String& str)
{
    char buf[32];
    if (m_timeRoute) {
	printTime(buf,m_timeRoute);
	str.append("routetime=",",") << buf;
    }
    if (m_timeRing) {
	printTime(buf,m_timeRing);
	str.append("ringtime=",",") << buf;
    }
    if (m_timeAnswer) {
	printTime(buf,m_timeAnswer);
	str.append("answertime=",",") << buf;
    }
}

bool AnalyzerChan::callRouted(Message& msg)
{
    if (!m_timeRoute)
	m_timeRoute = Time::now() - m_timeStart;
    return Channel::callRouted(msg);
}

bool AnalyzerChan::msgRinging(Message& msg)
{
    if (!m_timeRing)
	m_timeRing = Time::now() - m_timeStart;
    return Channel::msgRinging(msg);
}

bool AnalyzerChan::msgAnswered(Message& msg)
{
    if (!m_timeAnswer)
	m_timeAnswer = Time::now() - m_timeStart;
    addConsumer();
    addSource();
    return Channel::msgAnswered(msg);
}

void AnalyzerChan::startChannel(NamedList& params)
{
    Message* m = message("chan.startup");
    const char* tmp = params.getValue("caller");
    if (tmp)
	m->addParam("caller",tmp);
    tmp = params.getValue("called");
    if (tmp)
	m->addParam("called",tmp);
    if (isOutgoing()) {
	tmp = params.getValue("billid");
	if (tmp)
	    m->addParam("billid",tmp);
    }
    Engine::enqueue(m);
    if (isOutgoing()) {
	addConsumer();
	addSource();
    }
}

void AnalyzerChan::addSource()
{
    if (getSource())
	return;
    const char* src = "tone/dial";
    if (m_address.startsWith("tone/"))
	src = m_address;
    Message m("chan.attach");
    complete(m,true);
    m.addParam("source",src);
    m.addParam("single","true");
    m.userData(this);
    if (!Engine::dispatch(m))
	Debug(this,DebugWarn,"Could not attach source '%s' [%p]",src,this);
}

void AnalyzerChan::addConsumer()
{
    if (getConsumer())
	return;
    AnalyzerCons* cons = new AnalyzerCons(m_address);
    setConsumer(cons);
    cons->deref();
}

bool AnalyzerDriver::startCall(NamedList& params, const String& dest)
{
    bool direct = true;
    String tmp(params.getValue("direct"));
    if (tmp.null()) {
	direct = false;
	tmp = params.getValue("target");
	if (tmp.null()) {
	    Debug(DebugWarn,"Analyzer outgoing call with no target!");
	    return false;
	}
    }
    // this is an incoming call!
    AnalyzerChan *ac = new AnalyzerChan(dest,false);
    ac->startChannel(params);
    Message* m = ac->message("call.route",false,true);
    m->addParam("called",tmp);
    if (direct)
	m->addParam("callto",tmp);
    tmp = params.getValue("caller");
    if (tmp.null())
	tmp << prefix() << dest;
    m->addParam("caller",tmp);
    return ac->startRouter(m);
}

bool AnalyzerDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
    if (ch) {
	AnalyzerChan *ac = new AnalyzerChan(dest,true);
	if (ch->connect(ac,msg.getValue("reason"))) {
	    msg.setParam("peerid",ac->id());
	    ac->startChannel(msg);
	    ac->deref();
	    return true;
	}
	else {
	    ac->destruct();
	    return false;
	}
    }
    else
	return startCall(msg,dest);
}

bool AttachHandler::received(Message& msg)
{
    String cons(msg.getValue("consumer"));
    if (!cons.startSkip(__plugin.prefix(),false))
	cons.clear();
    if (cons.null())
	return false;

    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));

    if (!ch) {
	Debug(DebugWarn,"Analyzer attach request with no control channel!");
	return false;
    }

    // if single attach was requested we can return true if everything is ok
    bool ret = msg.getBoolValue("single");

    AnalyzerCons *ac = new AnalyzerCons(cons);
    ch->setConsumer(ac);
    ac->deref();
    return ret;
}

AnalyzerDriver::AnalyzerDriver()
    : Driver("analyzer","misc"), m_handler(0)
{
    Output("Loaded module Analyzer");
}

AnalyzerDriver::~AnalyzerDriver()
{
    Output("Unloading module Analyzer");
    lock();
    channels().clear();
    unlock();
}

void AnalyzerDriver::initialize()
{
    Output("Initializing module Analyzer");
    setup(0,true); // no need to install notifications
    Driver::initialize();
    installRelay(Ringing);
    installRelay(Answered);
    installRelay(Halt);
    if (!m_handler) {
	m_handler = new AttachHandler;
	Engine::install(m_handler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
