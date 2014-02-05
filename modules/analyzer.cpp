/**
 * analyzer.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Test call generator and audio quality analyzer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * FFT routine taken from Murphy McCauley's FFT DLL based in turn on the
 *  work of Don Cross <dcross@intersrv.com>
 * See http://www.fullspectrum.com/deeth/programming/fft.html
 *  and http://www.constantthought.com/
 * Thank both for a sleek routine that doesn't even fill a floppy ;-)
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

// minimum allowed for the maximum
#define ALLOW_MIN 2500.0
// threshold from max we consider a peak
#define PEAKS_THR 0.015
// expected number of peaks
#define PEAKS_NUM 2

#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_2PI
#define M_2PI (2*M_PI)
#endif

#ifndef M_4PI
#define M_4PI (4*M_PI)
#endif

using namespace TelEngine;
namespace { // anonymous

// Asynchronous FFT on a power of 2 sample buffer
class AsyncFFT : public Thread
{
public:
    enum WinType {
	None = 0,
	Rectangle = None,
	Triangle,
	Bartlett = Triangle,
	Hanning,
	Hamming,
	Blackman,
	FlatTop
    };
    virtual ~AsyncFFT();
    static AsyncFFT* create(unsigned int length, WinType window = Rectangle, Priority prio = Low);
    inline unsigned int samples() const
	{ return m_length; }
    inline unsigned int length() const
	{ return m_length >> 1; }
    inline bool ready() const
	{ return m_ready; }
    double at(int index) const;
    inline double operator[](int index) const
	{ return at(index); }
    bool prepare(const short* samp);
    inline void stop()
	{ m_notify = 0; m_stop = true; }
    virtual void run();
    inline void setNotify(Runnable* notified = 0)
	{ m_notify = notified; }
private:
    AsyncFFT(unsigned int length, WinType window, Priority prio);
    void buildWindow(WinType window);
    unsigned int revBits(unsigned int index);
    void compute();
    bool m_ready;
    bool m_start;
    bool m_stop;
    Runnable* m_notify;
    unsigned int m_length;
    double* m_window;
    double* m_real;
    double* m_imag;
    unsigned int m_nBits;
    const char* m_winName;
};

class AnalyzerCons : public DataConsumer, public Runnable
{
    YCLASS(AnalyzerCons,DataConsumer)
public:
    AnalyzerCons(const String& type, const char* window = 0);
    virtual ~AnalyzerCons();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    virtual void statusParams(String& str, Message* msg = 0);
    virtual void run();
protected:
    DataBlock m_data;
    u_int64_t m_timeStart;
    unsigned long m_tsStart;
    unsigned int m_tsGapCount;
    unsigned long m_tsGapLength;
    AsyncFFT* m_spectrum;
    unsigned long m_total;
    unsigned long m_valid;
    bool m_analyze;
};

class AnalyzerChan : public Channel
{
public:
    AnalyzerChan(const String& type, bool outgoing, const char* window = 0);
    virtual ~AnalyzerChan();
    virtual void destroyed();
    virtual void statusParams(String& str);
    virtual bool callRouted(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual void checkTimers(Message& msg, const Time& tmr);
    void startChannel(NamedList& params);
    void addSource();
    void addConsumer();
protected:
    void setDuration(NamedList& params);
    void localParams(String& str, Message* msg = 0);
    u_int64_t m_stopTime;
    u_int64_t m_timeStart;
    unsigned long m_timeRoute;
    unsigned long m_timeRing;
    unsigned long m_timeAnswer;
    String m_window;
};

class AttachHandler;

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

static TokenDict dict_windows[] = {
    { "rectangle", AsyncFFT::Rectangle },
    { "no",        AsyncFFT::None },
    { "none",      AsyncFFT::None },
    { "triangle",  AsyncFFT::Triangle },
    { "bartlett",  AsyncFFT::Bartlett },
    { "hanning",   AsyncFFT::Hanning },
    { "hamming",   AsyncFFT::Hamming },
    { "blackman",  AsyncFFT::Blackman },
    { "flattop",   AsyncFFT::FlatTop },
    { 0,   0 }
};

static Mutex s_mutex(false,"Analyzer");

static int s_res = 1;

class AttachHandler : public MessageHandler
{
public:
    AttachHandler()
	: MessageHandler("chan.attach",100,__plugin.name())
	{ }
    virtual bool received(Message& msg);
};

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


AsyncFFT* AsyncFFT::create(unsigned int length, WinType window, Priority prio)
{
    if (length < 2)
	return 0;
    // thanks to 'byang' for this cute power of two test!
    if (length & (length - 1))
	return 0;
    AsyncFFT* fft = new AsyncFFT(length,window,prio);
    if (fft->startup())
	return fft;
    delete fft;
    return 0;
}

AsyncFFT::AsyncFFT(unsigned int length, WinType window, Priority prio)
    : Thread("Async FFT",prio),
      m_ready(false), m_start(false), m_stop(false), m_notify(0),
      m_length(0), m_window(0), m_real(0), m_imag(0), m_nBits(0), m_winName(0)
{
    DDebug(&__plugin,DebugAll,"AsyncFFT::AsyncFFT(%u) [%p]",length,this);
    for (unsigned int i = 0; i <= 256 ;i++)
	if (length & (1 << i)) {
	    m_nBits = i;
	    break;
	}
    if (!m_nBits)
	return;
    m_length = length;
    m_real = new double[m_length];
    m_imag = new double[m_length];
    buildWindow(window);
}

AsyncFFT::~AsyncFFT()
{
    DDebug(&__plugin,DebugAll,"AsyncFFT::~AsyncFFT() [%p]",this);
    m_notify = 0;
    m_ready = false;
    m_start = false;
    delete[] m_real;
    delete[] m_imag;
    delete[] m_window;
}

void AsyncFFT::buildWindow(WinType window)
{
    m_winName = lookup(window,dict_windows);
    if (window == Rectangle)
	return;
    m_window = new double[m_length];
    unsigned int n2 = m_length >> 1;
    for (unsigned int i = 0; i < m_length; i++) {
	double omega = i * M_2PI / m_length;
	switch (window) {
	    case Triangle:
		{
		    int k = i - n2;
		    if (k > 0)
			k = -k;
		    k += n2;
		    m_window[i] = k*1.0/n2;
		}
		break;
	    case Hanning:
		m_window[i] = 0.5 - 0.5 * ::cos(omega);
		break;
	    case Hamming:
		m_window[i] = 0.54 - 0.46 * ::cos(omega);
		break;
	    case Blackman:
		m_window[i] = 0.42 - 0.5 * ::cos(omega) + 0.08 * ::cos(2*omega);
		break;
	    case FlatTop:
		m_window[i] = 0.2810639 - 0.5208972 * ::cos(omega) + 0.1980399 * ::cos(2*omega);
		break;
	    default:
		m_window[i] = 1.0;
	}
    }
}

double AsyncFFT::at(int index) const
{
    if ((index < 0) || ((unsigned int)index > (m_length >> 1)))
	return 0.0;
    if (!m_ready)
	return 0.0;
    return m_real[index];
}

void AsyncFFT::run()
{
    DDebug(&__plugin,DebugAll,"AsyncFFT::run() [%p]",this);
    while (!m_stop) {
	while (!m_start) {
	    Thread::idle();
	    if (m_stop)
		return;
	}
	m_ready = false;
	compute();
	m_ready = true;
	s_mutex.lock();
	if (m_notify)
	    m_notify->run();
	s_mutex.unlock();
	m_start = false;
    }
}

bool AsyncFFT::prepare(const short* samp)
{
    if (m_start || m_stop || !(samp && m_real))
	return false;
    m_ready = false;
    XDebug(&__plugin,DebugAll,"Preparing FFT buffer from %u samples [%p]",m_length,this);
    unsigned int i, j;
    for (i = 0; i < m_length; i++) {
	j = revBits(i);
	if (m_window)
	    m_real[i] = m_window[j] * samp[j];
	else
	    m_real[i] = samp[j];
	m_imag[i] = 0.0;
    }
    m_start = true;
    return true;
}

unsigned int AsyncFFT::revBits(unsigned int index)
{
    unsigned int i, rev;
    for (i = rev = 0; i < m_nBits; i++) {
	rev = (rev << 1) | (index & 1);
	index >>= 1;
    }
    return rev;
}

void AsyncFFT::compute()
{
#ifdef XDEBUG
    Debug(&__plugin,DebugInfo,"Computing FFT with length %u [%p]",m_length,this);
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
	blockEnd = blockSize;
    }
    n = m_length >> 1;
    for (i = 0; i < n; i++)
	m_real[i] = ::sqrt(m_real[i]*m_real[i] + m_imag[i]*m_imag[i]) / n;
#ifdef XDEBUG
    Debug(&__plugin,DebugInfo,"Computing FFT with length %u took " FMT64U " usec [%p]",
	m_length,Time::now()-t,this);
#endif

#ifdef XDEBUG
    for (i = 0; i < n; i++)
	Debug(&__plugin,DebugAll,"fft[%u] = %0.2f",i,m_real[i]);
#endif
}


AnalyzerCons::AnalyzerCons(const String& type, const char* window)
    : m_timeStart(0), m_tsStart(0), m_tsGapCount(0), m_tsGapLength(0),
      m_spectrum(0), m_total(0), m_valid(0), m_analyze(false)
{
    DDebug(&__plugin,DebugAll,"AnalyzerCons::AnalyzerCons('%s') [%p]",
	type.c_str(),this);
    unsigned int len = 0;
    if ((type == "probe") || type.startsWith("tone/probe")) {
	len = 256;
	m_analyze = true;
	m_spectrum = AsyncFFT::create(len,(AsyncFFT::WinType)lookup(window,dict_windows,AsyncFFT::Rectangle));
	m_spectrum->setNotify(this);
	return;
    }
    else if (type == "fft1024")
	len = 1024;
    else if (type == "fft512")
	len = 512;
    else if (type == "fft256")
	len = 256;
    else if (type == "fft128")
	len = 128;
    else if (type == "fft64")
	len = 64;
    if (len) {
	m_spectrum = AsyncFFT::create(len,(AsyncFFT::WinType)lookup(window,dict_windows,AsyncFFT::Triangle));
	m_spectrum->setNotify(this);
    }
}

AnalyzerCons::~AnalyzerCons()
{
    DDebug(&__plugin,DebugAll,"AnalyzerCons::~AnalyzerCons() %p [%p]",m_spectrum,this);
    s_mutex.lock();
    if (m_spectrum) {
	AsyncFFT* tmp = m_spectrum;
	m_spectrum = 0;
	tmp->stop();
    }
    s_mutex.unlock();
}

unsigned long AnalyzerCons::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (!m_timeStart) {
	// the first data block may be garbled or have bad timestamp - ignore
	m_timeStart = Time::now();
	m_tsStart = tStamp;
	return invalidStamp();
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
	return invalidStamp();
    m_data += data;
    unsigned int len = 2 * m_spectrum->samples();
    if (m_data.length() < len)
	return invalidStamp();
    // limit the length of the buffer
    int toCut = data.length() - (2 * len);
    if (toCut > 0) {
	DDebug(&__plugin,DebugInfo,"Dropping %d samples [%p]",toCut/2,this);
	m_data.cut(-toCut);
    }
    if (m_spectrum->prepare((const short*)m_data.data()))
	m_data.cut(-(int)len);
    return invalidStamp();
}

void AnalyzerCons::run()
{
    // this method is called with the mutex hold
    if (!m_spectrum)
	return;
    unsigned int n = m_spectrum->length();
    double max = 0.0;
    unsigned int i;
    for (i = 1; i < n; i++) {
	double val = m_spectrum->at(i);
	if (max < val)
	    max = val;
    }

    if (!m_analyze)
	return;

    double limit = max;
    if (max < ALLOW_MIN) {
	// don't start until we get some data
	if (!m_total)
	    return;
	limit = ALLOW_MIN;
    }

    unsigned int peaks = 0;
    limit *= PEAKS_THR;
    for (i = 1; i < n; i++) {
	if (m_spectrum->at(i) > limit)
	    peaks++;
    }
    DDebug(&__plugin,DebugInfo,"Got %u peaks, limit=%f, max=%f [%p]",peaks,limit,max,this);

    m_total++;
    if (peaks == PEAKS_NUM)
	m_valid++;
}

void AnalyzerCons::statusParams(String& str, Message* msg)
{
    unsigned long samples = timeStamp() - m_tsStart;
    str.append("gaps=",",") << m_tsGapCount;
    str << ",gaplen=" << (unsigned int)m_tsGapLength;
    str << ",samples=" << (unsigned int)samples;
    if (m_timeStart) {
	u_int64_t utime = Time::now() - m_timeStart;
	utime = utime ? ((1000000 * (u_int64_t)samples) + (utime / 2)) / utime : 0;
	str << ",rate=" << (unsigned int)utime;
	if (msg)
	    msg->setParam("rate",String((unsigned int)utime));
    }
    if (m_total > 0) {
	double q = m_valid * 100.0 / m_total;
	char buf[64];
	snprintf(buf,sizeof(buf)-1,"quality=%0.2f",q);
	str.append(buf,",");
	if (msg) {
	    char quality[32];
	    snprintf(quality,sizeof(quality)-1,"%0.2f",q);
	    msg->setParam("quality",quality);
	}
    }
    if (!msg)
	return;
    msg->setParam("gaps",String(m_tsGapCount));
    msg->setParam("gaplen",String((unsigned int)m_tsGapLength));
    msg->setParam("samples",String(((unsigned int)samples)));
}


AnalyzerChan::AnalyzerChan(const String& type, bool outgoing, const char* window)
    : Channel(__plugin,0,outgoing), m_stopTime(0),
      m_timeStart(Time::now()), m_timeRoute(0), m_timeRing(0), m_timeAnswer(0),
      m_window(window)
{
    DDebug(this,DebugAll,"AnalyzerChan::AnalyzerChan('%s',%s) [%p]",
	type.c_str(),String::boolText(outgoing),this);
    m_address = type;
}

AnalyzerChan::~AnalyzerChan()
{
    DDebug(this,DebugAll,"AnalyzerChan::~AnalyzerChan() %s [%p]",id().c_str(),this);
    Engine::enqueue(message("chan.hangup"));
}

void AnalyzerChan::destroyed()
{
    RefPointer<AnalyzerCons> cons = YOBJECT(AnalyzerCons,getConsumer());
    char buf[32];
    printTime(buf,(unsigned int)(Time::now() - m_timeStart));
    String str(status());
    Message* msg = message("call.analyzer");
    localParams(str,msg);
    str.append("totaltime=",",") << buf;
    msg->setParam("totaltime",buf);
    if (cons)
	cons->statusParams(str,msg);
    Output("Finished '%s' status: %s",id().c_str(),str.c_str());
    Channel::destroyed();
    Engine::enqueue(msg);
}

void AnalyzerChan::statusParams(String& str)
{
    Channel::statusParams(str);
    localParams(str);
    RefPointer<AnalyzerCons> cons = YOBJECT(AnalyzerCons,getConsumer());
    if (cons)
	cons->statusParams(str);
}

void AnalyzerChan::localParams(String& str, Message* msg)
{
    char buf[32];
    if (m_timeRoute) {
	printTime(buf,m_timeRoute);
	str.append("routetime=",",") << buf;
	if (msg)
	    msg->setParam("routetime",buf);
    }
    if (m_timeRing) {
	printTime(buf,m_timeRing);
	str.append("ringtime=",",") << buf;
	if (msg)
	    msg->setParam("ringtime",buf);
    }
    if (m_timeAnswer) {
	printTime(buf,m_timeAnswer);
	str.append("answertime=",",") << buf;
	if (msg)
	    msg->setParam("answertime",buf);
    }
}

bool AnalyzerChan::callRouted(Message& msg)
{
    if (!m_timeRoute)
	m_timeRoute = (unsigned long)(Time::now() - m_timeStart);
    setDuration(msg);
    return Channel::callRouted(msg);
}

bool AnalyzerChan::msgRinging(Message& msg)
{
    if (!m_timeRing)
	m_timeRing = (unsigned long)(Time::now() - m_timeStart);
    return Channel::msgRinging(msg);
}

bool AnalyzerChan::msgAnswered(Message& msg)
{
    if (!m_timeAnswer)
	m_timeAnswer = (unsigned long)(Time::now() - m_timeStart);
    addConsumer();
    addSource();
    return Channel::msgAnswered(msg);
}

void AnalyzerChan::checkTimers(Message& msg, const Time& tmr)
{
    if (m_stopTime && (m_stopTime < tmr))
	msgDrop(msg,"finished");
    else
	Channel::checkTimers(msg,tmr);
}

void AnalyzerChan::startChannel(NamedList& params)
{
    Message* m = message("chan.startup",params);
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
    setDuration(params);
}

void AnalyzerChan::setDuration(NamedList& params)
{
    int t = params.getIntValue("duration",120000);
    if (t > 0)
	m_stopTime = Time::now() + 1000 * (uint64_t)t;
}

void AnalyzerChan::addSource()
{
    if (getSource())
	return;
    const char* src = "tone/probe";
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
    AnalyzerCons* cons = new AnalyzerCons(m_address,m_window);
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
    AnalyzerChan *ac = new AnalyzerChan(dest,false,params.getValue("window"));
    ac->initChan();
    ac->startChannel(params);
    Message* m = ac->message("call.route",false,true);
    m->addParam("called",tmp);
    if (direct)
	m->addParam("callto",tmp);
    tmp = params.getValue("caller");
    if (tmp.null())
	tmp << prefix() << dest;
    m->addParam("caller",tmp);
    params.setParam("id",ac->id());
    return ac->startRouter(m);
}

bool AnalyzerDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
    if (ch) {
	AnalyzerChan *ac = new AnalyzerChan(dest,true,msg.getValue("window"));
	ac->initChan();
	if (ch->connect(ac,msg.getValue("reason"))) {
	    ac->callConnect(msg);
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

    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));

    if (!ch) {
	Debug(DebugWarn,"Analyzer attach request with no control channel!");
	return false;
    }

    // if single attach was requested we can return true if everything is ok
    bool ret = msg.getBoolValue("single");

    AnalyzerCons *ac = new AnalyzerCons(cons,msg.getValue("window"));
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

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
