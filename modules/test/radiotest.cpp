/**
 * radiotest.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Radio interface test
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015 Null Team
 * Copyright (C) 2015 LEGBA Inc
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
#include <yateradio.h>
#include <yatemath.h>

using namespace TelEngine;
namespace { // anonymous

class RadioTestRecv;
class RadioTestModule;

class RadioTestIO
{
public:
    inline RadioTestIO(bool _tx)
	: tx(_tx), enabled(false), startTime(0), ts(0), transferred(0)
	{}
    bool tx;
    bool enabled;
    uint64_t startTime;
    uint64_t ts;
    uint64_t transferred;
};

class RadioTest : public Thread, public DebugEnabler
{
    friend class RadioTestRecv;
    friend class RadioTestModule;
public:
    RadioTest(const NamedList& params, const NamedList& radioParams);
    ~RadioTest()
	{ terminated(); }
    bool command(const String& cmd, const NamedList& params);
    static bool start(const NamedList& params, const NamedList& radioParams);

protected:
    virtual void cleanup()
	{ terminated(); }
    virtual void run();
    void terminated();
    void initPulse();
    bool setTxData();
    void regenerateTxData();
    bool execute(const String& cmd, const String& param, bool fatal,
	const NamedList* params);
    bool execute(const NamedList& cmds, const char* prefix);
    bool write();
    bool read();
    void readTerminated(RadioTestRecv* th);
    void readStop();
    void hardCancelRecv();
    inline void updateTs(bool tx) {
	    uint64_t ts = 0;
	    if ((tx ? m_radio->getTxTime(ts) : m_radio->getRxTime(ts)) == 0) {
		(tx ? m_tx.ts : m_rx.ts) = ts;
		Debug(this,DebugInfo,"Updated %s ts=" FMT64U " [%p]",
		    (tx ? "TX" : "RX"),ts,this);
	    }
	}
    bool wait(const String& param);

    RadioInterface* m_radio;
    RadioTestRecv* m_recv;
    bool m_started;
    unsigned int m_repeat;
    NamedList m_init;
    NamedList m_params;
    NamedList m_radioParams;
    // TX
    RadioTestIO m_tx;
    bool m_newTxData;
    unsigned int m_phase;
    unsigned int m_sendBufCount;
    ComplexVector m_sendBufData;
    // Pulse
    unsigned int m_pulse;
    ComplexVector m_pulseData;
    // RX
    RadioTestIO m_rx;
    RadioReadBufs m_bufs;
    unsigned int m_skippedBuffs;
    DataBlock m_crt;
    DataBlock m_aux;
    DataBlock m_extra;
};

class RadioTestRecv : public Thread
{
public:
    inline RadioTestRecv(RadioTest* test)
	: Thread("RadioTestRecv"), m_test(test)
	{}
    ~RadioTestRecv()
	{ notify(); }
    void cleanup()
	{ notify(); }
    static inline RadioTestRecv* start(RadioTest* test) {
	    RadioTestRecv* tmp = new RadioTestRecv(test);
	    if (tmp->startup())
		return tmp;
	    delete tmp;
	    return 0;
	}
protected:
    virtual void run() {
	    if (!m_test)
		return;
	    while (!Thread::check(false) && m_test->read())
		;
	    notify();
	}
    inline void notify() {
	    RadioTest* tmp = m_test;
	    m_test = 0;
	    if (tmp)
		tmp->readTerminated(this);
	}
    RadioTest* m_test;
};

class RadioTestModule : public Module
{
public:
    RadioTestModule();
    ~RadioTestModule();

protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    bool onCmdControl(Message& msg);
    bool test(const String& cmd = String::empty(),
	const NamedList& params = NamedList::empty());
    void processRadioDataFile(NamedList& params);
};

INIT_PLUGIN(RadioTestModule);
static RadioTest* s_test = 0;
static Mutex s_testMutex(false,"RadioTest");

static inline unsigned int threadIdleIntervals(unsigned int ms)
{
    return (ms + Thread::idleMsec()) / Thread::idleMsec();
}

static inline bool validFloatSample(float val)
{
    return (val >= -1.0F) && (val <= 1.0F);
}

static inline const char* encloseDashes(String& s, bool extra = false)
{
    static const String s1 = "\r\n-----";
    if (s)
	s = s1 + (extra ? "\r\n" : "") + s + s1;
    return s.safe();
}

static inline unsigned int samplesf2bytes(unsigned int samples)
{
    return samples * 2 * sizeof(float);
}

static String& dumpSamplesFloat(String& s, const DataBlock& buf,
    const char* fmt4, const char* fmt, const char* sep, unsigned int maxDump = 0)
{
    unsigned int samples = buf.length() / (2 * sizeof(float));
    const float* f = (const float*)buf.data();
    if (!(f && samples))
	return s;
    if (maxDump && maxDump < samples)
	samples = maxDump;
    String tmp;
    unsigned int a = samples / 4;
    for (; a; a--, f += 8)
	s.append(tmp.printf(512,fmt4,f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7]),sep);
    for (a = samples % 4; a; a--, f += 2)
	s.append(tmp.printf(fmt,f[0],f[1]),sep);
    return s;
}

static String& dumpSamplesInt16(String& s, const DataBlock& buf,
    const char* fmt4, const char* fmt, const char* sep, unsigned int maxDump = 0)
{
    unsigned int samples = buf.length() / (2 * sizeof(int16_t));
    const int16_t* f = (const int16_t*)buf.data();
    if (!(f && samples))
	return s;
    if (maxDump && maxDump < samples)
	samples = maxDump;
    String tmp;
    unsigned int a = samples / 4;
    for (; a; a--, f += 8)
	s.append(tmp.printf(512,fmt4,f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7]),sep);
    for (a = samples % 4; a; a--, f += 2)
	s.append(tmp.printf(fmt,f[0],f[1]),sep);
    return s;
}

static inline bool boolSetError(String& s, const char* e = 0)
{
    s = e;
    return false;
}

// Parse a comma separated list of float values to complex vector
static bool parseVector(String& error, const String& str, ComplexVector& buf)
{
    if (!str)
	return boolSetError(error,"empty");
    ObjList* list = str.split(',');
    unsigned int len = list->length();
    if ((len < 2) || (len % 2) != 0)
	return boolSetError(error,"invalid length");
    buf.resetStorage(len);
    ObjList* o = list;
    for (float* b = (float*)buf.data(); o; o = o->next(), b++) {
	if (!o->get())
	    continue;
	*b = (static_cast<String*>(o->get()))->toDouble();
	if (!validFloatSample(*b))
	    break;
    }
    TelEngine::destruct(list);
    return (o == 0) ? true : boolSetError(error,"invalid data range");
}

static inline void generateCircleQuarter(Complex*& c, float amplitude, float i, float q,
    unsigned int loops, float angle, float iSign, float qSign)
{
    (c++)->set(i * amplitude,q * amplitude);
    if (!loops)
	return;
    float angleStep = M_PI_2 / (loops + 1);
    if (angle)
	angleStep = -angleStep;
    iSign *= amplitude;
    qSign *= amplitude;
    for (; loops; --loops, ++c) {
	angle += angleStep;
	c->set(iSign * ::cosf(angle),qSign * ::sinf(angle));
    }
}

// Parse a complex numbers pattern
// forcePeriodic=true: Force lenExtend=false and lenRequired=true for periodic
//                     patterns (like 'circle')
// lenExtend=true: Extend destination buffer to be minimum 'len'. 'lenRequired' is ignored
// lenRequired=true: 'len' MUST be a multiple of generated vector's length
static bool buildVector(String& error, const String& pattern, ComplexVector& vector,
    unsigned int len = 0, bool forcePeriodic = true, bool lenExtend = true,
    bool lenRequired = false, unsigned int* pLen = 0)
{
    if (!pattern)
	return boolSetError(error,"empty");
    bool isPeriodic = false;
    String p = pattern;
    ComplexVector v;
    // Check for circles
    if (p.startSkip("circle",false)) {
	unsigned int cLen = 4;
	bool rev = false;
	float div = 1;
	if (!p || p == YSTRING("_reverse"))
	    // circle[_reverse]
	    rev = !p.null();
	else if (p.startSkip("_div_",false)) {
	    // circle_div[_reverse]_{divisor}
	    rev = p.startSkip("reverse_",false);
	    if (!p)
		return boolSetError(error);
	    div = p.toDouble();
	}
	else if (p.startSkip("_points_",false)) {
	    // circle_points[_reverse]_{value}[_div_{divisor}]
	    rev = p.startSkip("reverse_",false);
	    if (!p)
		return boolSetError(error);
	    int pos = p.find('_');
	    if (pos < 0)
		cLen = p.toInteger(0,0,0);
	    else {
		// Expecting div
		cLen = p.substr(0,pos).toInteger(0,0,0);
		p = p.substr(pos + 1);
		if (!(p.startSkip("div_",false) && p))
		    return boolSetError(error);
		div = p.toDouble();
	    }
	}
	else
	    return boolSetError(error);
	// Circle length MUST be a multiple of 4
	if (!cLen || (cLen % 4) != 0)
	    return boolSetError(error,"invalid circle length");
	if (div < 1)
	    return boolSetError(error,"invalid circle div");
	v.resetStorage(cLen);
	Complex* c = v.data();
	float amplitude = 1.0F / div;
	float direction = rev ? -1 : 1;
	unsigned int n = (cLen - 4) / 4;
	generateCircleQuarter(c,amplitude,1,0,n,0,1,direction);
	generateCircleQuarter(c,amplitude,0,direction,n,M_PI_2,-1,direction);
	generateCircleQuarter(c,amplitude,-1,0,n,0,-1,-direction);
	generateCircleQuarter(c,amplitude,0,-direction,n,M_PI_2,1,-direction);
	isPeriodic = true;
    }
    else if (pattern == YSTRING("zero")) {
	// Fill with 0
	vector.resetStorage(len ? len : 1);
	if (pLen)
	    *pLen = 1;
	return true;
    }
    else if (p.startSkip("fill_",false)) {
	// Fill with value: fill_{real}_{imag}
	int pos = p.find('_');
	if (pos < 1 || p.find('_',pos + 1) > 0)
	    return boolSetError(error);
	float re = p.substr(0,pos).toDouble();
	float im = p.substr(pos + 1).toDouble();
	if (validFloatSample(re) && validFloatSample(im)) {
	    vector.resetStorage(len ? len : 1);
	    vector.fill(Complex(re,im));
	    if (pLen)
		*pLen = 1;
	    return true;
	}
	return boolSetError(error,"invalid data range");
    }
    else if (!parseVector(error,pattern,v))
	// Parse list of values
	return false;
    if (!v.length())
	return boolSetError(error,"empty result");
    if (pLen)
	*pLen = v.length();
    if (isPeriodic && forcePeriodic) {
	lenExtend = false;
	lenRequired = true;
    }
    // Try to extend data
    if (!len || (len == v.length()) || !(lenExtend || lenRequired))
	vector = v;
    else {
	if (lenExtend) {
	    if (len < v.length())
		len = v.length();
	    unsigned int rest = len % v.length();
	    if (rest)
		len += v.length() - rest;
	}
	else if ((len < v.length()) || ((len % v.length()) != 0))
	    return boolSetError(error,"required/actual length mismatch");
	vector.resetStorage(len);
	for (unsigned int i = 0; (i + v.length()) < len; i += v.length())
	    vector.slice(i,v.length()).copy(v,v.length());
    }
    return true;
}


//
// RadioTest
//
RadioTest::RadioTest(const NamedList& params, const NamedList& radioParams)
    : Thread("RadioTest",Thread::priority(params["priority"])),
    m_radio(0),
    m_recv(0),
    m_started(false),
    m_repeat(0),
    m_init(""),
    m_params(params),
    m_radioParams(radioParams),
    m_tx(true),
    m_newTxData(false),
    m_phase(0),
    m_sendBufCount(0),
    m_pulse(0),
    m_rx(false),
    m_skippedBuffs(0)
{
    m_params.setParam("orig_test_name",params.c_str());
    m_params.assign(__plugin.name() + "/" + params.c_str());
    debugName(m_params);
    debugChain(&__plugin);
}

bool RadioTest::command(const String& cmd, const NamedList& params)
{
    Debug(this,DebugNote,"Unknown command '%s' [%p]",cmd.c_str(),this);
    return false;
}

bool RadioTest::start(const NamedList& params, const NamedList& radioParams)
{
    s_test = new RadioTest(params,radioParams);
    if (s_test->startup())
	return true;
    delete s_test;
    Debug(&__plugin,DebugNote,"Failed to start test thread");
    return false;
}

void RadioTest::run()
{
    readStop();
    m_started = true;
    m_init.clearParams();
    bool ok = false;
    unsigned int repeat = m_params.getIntValue("repeat",1,1);
    Debug(this,DebugInfo,"Initializing repeat=%u [%p]",repeat,this);
    while (true) {
	// Init
	// Init test data
	m_tx.enabled = true;
	if (!setTxData())
	    break;
	m_sendBufCount = m_params.getIntValue("send_buffers",0,0);
	if (m_sendBufCount)
	    m_init.addParam("send_buffers",String(m_sendBufCount));
	m_rx.enabled = !m_params.getBoolValue("sendonly");
	if (m_rx.enabled) {
	    unsigned int n = m_params.getIntValue("readsamples",256,1);
	    m_bufs.reset(n,0);
	    m_crt.assign(0,samplesf2bytes(m_bufs.bufSamples()));
	    m_aux = m_crt;
	    m_extra = m_crt;
	    m_bufs.crt.samples = (float*)m_crt.data(0);
	    m_bufs.aux.samples = (float*)m_aux.data(0);
	    m_bufs.extra.samples = (float*)m_extra.data(0);
	    m_init.addParam("readsamples",String(n));
	}
	// Create radio
	Message m(m_radioParams);
	m.assign("radio.create");
	m.setParam("module",__plugin.name());
	bool radioOk = Engine::dispatch(m);
	NamedPointer* np = YOBJECT(NamedPointer,m.getParam(YSTRING("interface")));
	m_radio = np ? YOBJECT(RadioInterface,np) : 0;
	if (!m_radio) {
	    const String& e = m[YSTRING("error")];
	    Debug(this,DebugNote,"Failed to create radio interface: %s",
		e.safe(radioOk ? "Missing interface" : "Message not handled"));
	    break;
	}
	np->takeData();
	
	NamedList files("");
	for (ObjList* o = m_params.paramList()->skipNull(); o; o = o->skipNext()) {
	    NamedString* ns = static_cast<NamedString*>(o->get());
	    if (!ns->name().startsWith("file:"))
		continue;
	    String file = *ns;
	    NamedList tmp("");
	    tmp.addParam("now",String(Time::secNow()));
	    tmp.replaceParams(file);
	    if (file && execute("devparam:" + ns->name().substr(5),file,true,0))
		files.addParam(ns->name(),file);
	}
	m_params.clearParam("file",':');
	m_params.copyParams(files);
	
	if (!execute(m_params,"init:"))
	    break;
	unsigned int status = m_radio->initialize(m_radioParams);
	if (status) {
	    if (RadioInterface::Pending == status) {
		unsigned int wait = m_params.getIntValue("wait_pending_init",0,0);
		if (wait)
		    status = m_radio->pollPending(RadioInterface::PendingInitialize,wait);
		else {
		    while (!Thread::check(false)) {
			status = m_radio->pollPending(RadioInterface::PendingInitialize);
			if (!status || RadioInterface::Pending != status)
			    break;
			Thread::idle();
		    }
		    if (Thread::check(false))
			status = RadioInterface::Cancelled;
		}
	    }
	    if (status && status != RadioInterface::Cancelled) {
		Debug(this,DebugNote,"Failed to initialize radio interface: %u %s [%p]",
		    status,RadioInterface::errorName(status),this);
		break;
	    }
	}
	if (!execute(m_params,"cmd:"))
	    break;
	if (!wait("wait_after_init"))
	    break;
	ok = true;
	if (m_params.getBoolValue("init_only"))
	    break;
#define TEST_FAIL_BREAK { ok = false; break; }
	if (m_rx.enabled) {
	    m_recv = RadioTestRecv::start(this);
	    if (!m_recv) {
		Debug(this,DebugWarn,"Failed to start read data thread [%p]",this);
		TEST_FAIL_BREAK;
	    }
	}
	String s;
	m_init.dump(s,"\r\n");
	Debug(this,DebugInfo,"Starting [%p]%s",this,encloseDashes(s,true));
	// Run
	while (!Thread::check(false)) {
	    if (!write())
		TEST_FAIL_BREAK;
	    if (m_rx.enabled && !m_recv && !Thread::check(false)) {
		Debug(this,DebugWarn,"Read data thread abnormally terminated [%p]",this);
		TEST_FAIL_BREAK;
	    }
	}
	readStop();
#undef TEST_FAIL_BREAK
	break;
    }
    if (ok && (repeat > 1) && !Thread::check(false))
	m_repeat = --repeat;
    terminated();
}

void RadioTest::terminated()
{
    readStop();
    s_testMutex.lock();
    if (s_test == this)
	s_test = 0;
    s_testMutex.unlock();
    TelEngine::destruct(m_radio);
    if (!m_started)
	return;
    m_started = false;
    String s;
    RadioTestIO* txrx[2] = {&m_tx,&m_rx};
    uint64_t now = Time::now();
    for (uint8_t i = 0; i < 2; i++) {
	RadioTestIO& io = *(txrx[i]);
	if (!io.enabled)
	    continue;
	String prefix = io.tx ? "tx_" : "rx_";
	s << "\r\n" << prefix << "transferred=" << io.transferred;
	if (io.transferred) {
	    unsigned int sec = (unsigned int)((now - io.startTime) / 1000000);
	    if (sec)
		s << " (avg: " << (io.transferred / sec) << " samples/sec)";
	}
	s << "\r\n" << prefix << "timestamp=" << io.ts;
    }
    Debug(this,DebugInfo,"Terminated [%p]%s",this,encloseDashes(s));
    if (!m_repeat)
	return;
    Debug(this,DebugNote,"Restarting repeat=%u [%p]",m_repeat,this);
    Message* m = new Message("chan.control");
    m->addParam("module",__plugin.name());
    m->addParam("component",__plugin.name());
    m->addParam("operation","restart");
    m->addParam("name",m_params.getValue("orig_test_name"));
    m->addParam("repeat",String(m_repeat));
    m->copySubParams(m_params,"file:",false,true);
    Engine::enqueue(m);
}

bool RadioTest::setTxData()
{
    const String& pattern = m_params["txdata"];
    if (!pattern) {
	Debug(this,DebugConf,"Missing tx data pattern [%p]",this);
	return false;
    }
    m_newTxData = true;
    m_phase = 0;
    m_pulse = 0;
    String tp;
    if (pattern == "two-circles") {
	m_sendBufData.resetStorage(m_params.getIntValue("txdata_length",819,50));
	m_init.addParam("txpattern",pattern);
    }
    else if (pattern == "pulse") {
	unsigned int samples = m_params.getIntValue("txdata_length",10000,50);
	m_sendBufData.resetStorage(samples);
	unsigned int defVal = (samples > 2) ? (samples - 2) : 2;
	m_pulse = m_params.getIntValue("pulse",defVal,2,10000000);
	String pattern = m_params.getValue("pulse_pattern","1,1,-1,-1");
	String e;
	bool ok = parseVector(e,pattern,m_pulseData);
	if (!ok || m_pulseData.length() < 2 || m_pulseData.length() > (m_pulse / 3)) {
	    bool sh = (m_pulseData.length() < 2);
	    Debug(this,DebugConf,"Invalid pulse_pattern '%s': %s [%p]",
		pattern.c_str(),e.safe(sh ? "too short" : "too long"),this);
	    return false;
	}
	m_init.addParam("txpattern",pattern);
	m_init.addParam("pulse",String(m_pulse));
	String s;
	unsigned int h = m_pulseData.length() > 10 ? 10 : m_pulseData.length();
	m_pulseData.head(h).dump(s,Math::dumpComplex," ","%g,%g");
	m_init.addParam("pulse_pattern",pattern);
    }
    else {
	m_newTxData = false;
	String e;
	if (!buildVector(e,pattern,m_sendBufData)) {
	    Debug(this,DebugConf,"Invalid tx data pattern '%s': %s [%p]",
		pattern.c_str(),e.safe("unknown"),this);
	    return false;
	}
	unsigned int len = m_sendBufData.length();
	int n = m_params.getIntValue("txdata_repeat");
	if (n > 0) {
	    ComplexVector tmp(m_sendBufData);
	    m_sendBufData.resetStorage(n * len);
	    for (unsigned int i = 0; i < m_sendBufData.length(); i += len)
		m_sendBufData.slice(i,len).copy(tmp,len);
	}
	m_init.addParam("txpattern",pattern.substr(0,50));
	String s;
	m_sendBufData.head(len > 20 ? 20 : len).dump(s,Math::dumpComplex,",","%g,%g");
	if (!s.startsWith(pattern))
	    m_init.addParam("txdata",s);
    }
    m_init.addParam("send_samples",String(m_sendBufData.length()));
    return true;
}

void RadioTest::regenerateTxData()
{
    // Fs / 4 data
    static const float s_cs4[4] = {1,0,-1,0};
    // Fs / 8 data
    static const float s_r2 = M_SQRT1_2;
    static const float s_cs8[8] = {1,s_r2,0,-s_r2,-1,-s_r2,0,s_r2};

    Complex* last = 0;
    Complex* c = m_sendBufData.data(0,m_sendBufData.length(),last);
    if (m_pulse) {
	m_sendBufData.bzero();
	for (; c != last; ++c, ++m_phase) {
	    unsigned int idx = m_phase % m_pulse;
	    if (idx < m_pulseData.length())
		*c = m_pulseData[idx];
	}
    }
    else
	for (; c != last; ++c, ++m_phase)
	    c->set(0.5 * (s_cs4[m_phase % 4] + s_cs8[m_phase % 8]),
		-0.5 * (s_cs4[(m_phase + 1) % 4] + s_cs8[(m_phase + 2) % 8]));
}

bool RadioTest::execute(const String& cmd, const String& param, bool fatal,
    const NamedList* params)
{
    XDebug(this,DebugAll,"execute(%s,%s) [%p]",cmd.c_str(),param.c_str(),this);
    unsigned int c = RadioInterface::Failure;
    if (cmd == YSTRING("samplerate"))
	c = m_radio->setSampleRate(param.toInteger());
    else if (cmd == YSTRING("filter"))
	c = m_radio->setFilter(param.toInteger());
    else if (cmd == YSTRING("txfrequency"))
	c = m_radio->setTxFreq(param.toInteger());
    else if (cmd == YSTRING("rxfrequency"))
	c = m_radio->setRxFreq(param.toInteger());
    else if (cmd == YSTRING("loopback"))
	c = m_radio->setLoopback(param);
    else if (cmd == YSTRING("calibrate"))
	c = m_radio->calibrate();
    else if (cmd.startsWith("devparam:")) {
	NamedList tmp("");
	if (params)
	    tmp.copySubParams(*params,cmd + "_");
	tmp.setParam("cmd:" + cmd,param);
	c = m_radio->setParams(tmp);
    }
    else {
	Debug(this,DebugNote,"Unhandled command '%s' [%p]",cmd.c_str(),this);
	return true;
    }
    if (c == 0 || !fatal)
	return true;
    Debug(this,DebugNote,"'%s' failed with %u '%s' [%p]",cmd.c_str(),c,
	RadioInterface::errorName(c),this);
    return false;
}

bool RadioTest::execute(const NamedList& cmds, const char* prefix)
{
    for (const ObjList* o = cmds.paramList()->skipNull(); o; o = o->skipNext()) {
	const NamedString* ns = static_cast<const NamedString*>(o->get());
	String s = ns->name();
	if (s.startSkip(prefix,false) &&
	    !execute(s,*ns,cmds.getBoolValue(s + "_fatal",true),&cmds))
	    return false;
    }
    return true;
}

bool RadioTest::write()
{
    if (!m_tx.startTime)
	m_tx.startTime = Time::now();
    if (m_newTxData)
	regenerateTxData();
    if (!m_tx.ts)
	updateTs(true);
    unsigned int code = m_radio->send(m_tx.ts,(float*)m_sendBufData.data(),
	m_sendBufData.length());
    if (!code) {
	m_tx.ts += m_sendBufData.length();
	m_tx.transferred += m_sendBufData.length();
	if (!m_sendBufCount)
	    return true;
	m_sendBufCount--;
	return m_sendBufCount > 0;
    }
    if (code != RadioInterface::Cancelled)
	Debug(this,DebugNote,"Send error: %u '%s' [%p]",
	    code,RadioInterface::errorName(code),this);
    return false;
}

bool RadioTest::read()
{
    if (!m_rx.startTime)
	m_rx.startTime = Time::now();
    if (!m_rx.ts)
	updateTs(false);
    m_skippedBuffs = 0;
    unsigned int code = m_radio->read(m_rx.ts,m_bufs,m_skippedBuffs);
    if (!code) {
	if (m_bufs.full(m_bufs.crt))
	    m_rx.transferred += m_bufs.bufSamples();
	return true;
    }
    if (code != RadioInterface::Cancelled)
	Debug(this,DebugNote,"Recv error: %u '%s' [%p]",
	    code,RadioInterface::errorName(code),this);
    return false;
}

void RadioTest::readTerminated(RadioTestRecv* th)
{
    Lock lck(s_testMutex);
    if (m_recv == th)
	m_recv = 0;
}

void RadioTest::readStop()
{
    if (!m_recv)
	return;
    Lock lck(s_testMutex);
    if (!m_recv)
	return;
    m_recv->cancel();
    lck.drop();
    // Wait for 5 seconds before hard cancelling
    unsigned int n = threadIdleIntervals(5000);
    while (m_recv && n) {
	Thread::idle();
	n--;
    }
    lck.acquire(s_testMutex);
    if (m_recv)
	hardCancelRecv();
}

void RadioTest::hardCancelRecv()
{
    if (!m_recv)
	return;
    Debug(this,DebugWarn,"Hard cancelling read data thread (%p) [%p]",m_recv,this);
    m_recv->cancel(true);
    m_recv = 0;
}

bool RadioTest::wait(const String& param)
{
    unsigned int wait = m_params.getIntValue(param,0,0);
    if (!wait)
	return true;
    Debug(this,DebugInfo,"Waiting '%s' %ums [%p]",param.c_str(),wait,this);
    unsigned int n = threadIdleIntervals(wait);
    for (; n && !Thread::check(false); n--)
	Thread::idle();
    return n == 0;
}


//
// RadioTestModule
//
RadioTestModule::RadioTestModule()
    : Module("radiotest","misc")
{
    Output("Loaded module Radio Test");
}

RadioTestModule::~RadioTestModule()
{
    Output("Unloading module Radio Test");
    if (s_test)
	Debug(this,DebugWarn,"Exiting while test is running!!!");
}

void RadioTestModule::initialize()
{
    Output("Initializing module Radio Test");
    if (!relayInstalled(Halt)) {
	setup();
	installRelay(Halt,120);
	installRelay(Control);
    }
}

bool RadioTestModule::received(Message& msg, int id)
{
    if (id == Control) {
	if (msg[YSTRING("component")] == name())
	    return onCmdControl(msg);
	return false;
    }
    else if (id == Halt)
	test();
    return Module::received(msg,id);
}

bool RadioTestModule::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine == YSTRING("control")) {
	itemComplete(msg.retValue(),name(),partWord);
	return false;
    }
    String tmp = partLine;
    if (tmp.startSkip("control") && tmp == name()) {
	// Complete commands
	itemComplete(msg.retValue(),"start",partWord);
	itemComplete(msg.retValue(),"exec",partWord);
	itemComplete(msg.retValue(),"stop",partWord);
	itemComplete(msg.retValue(),"radiodatafile",partWord);
	itemComplete(msg.retValue(),"help",partWord);
	return false;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

bool RadioTestModule::onCmdControl(Message& msg)
{
    static const char* s_help =
	"\r\ncontrol radiotest {start [name=conf_sect_name]|stop|exec}"
	"\r\n  Test commands"
	"\r\ncontrol radiotest radiodatafile [sect=conf_sect_name]"
	"\r\n  Read radio data file. Process it according to given section parameters.";

    const String& cmd = msg[YSTRING("operation")];
    if (cmd == YSTRING("help")) {
	msg.retValue() << s_help;
	return true;
    }
    if (cmd == YSTRING("radiodatafile")) {
	processRadioDataFile(msg);
	return true;
    }
    return test(cmd,msg);
}

// control module_name test oper={start|stop|.....} params...
bool RadioTestModule::test(const String& cmd, const NamedList& list)
{
    static bool s_exec = false;

    Lock lck(s_testMutex);
    while (s_exec) {
	lck.drop();
	Thread::idle();
	if (Thread::check(false))
	    return false;
	lck.acquire(s_testMutex);
    }
    bool start = (cmd == YSTRING("start"));
    bool restart = !start && (cmd == YSTRING("restart"));
    if (start || restart || !cmd || cmd == YSTRING("stop")) {
	// Stop the test
	while (s_test) {
	    s_test->cancel();
	    if (s_test->m_recv)
		s_test->m_recv->cancel();
	    lck.drop();
	    // Wait for 5 seconds before hard cancelling
	    unsigned int n = threadIdleIntervals(5000);
	    while (s_test && n) {
		Thread::idle();
		n--;
	    }
	    lck.acquire(s_testMutex);
	    if (!s_test)
		break;
	    s_test->hardCancelRecv();
	    Debug(this,DebugWarn,"Hard cancelling test thread (%p)",s_test);
	    s_test->cancel(true);
	    s_test = 0;
	}
	while (start || restart) {
	    Configuration cfg(Engine::configFile(name()));
	    String n = list.getValue("name",start ? "test" : 0);
	    NamedList* sect = n ? cfg.getSection(n) : 0;
	    if (!sect) {
		Debug(this,DebugNote,"Failed to start test '%s': missing config section",
		    n.c_str());
		break;
	    }
	    NamedList params(sect->c_str());
	    const char* inc = sect->getValue(YSTRING("include"));
	    if (inc)
		params.copyParams(*cfg.createSection(inc));
	    params.copyParams(*sect);
	    if (restart) {
		unsigned int repeat = list.getIntValue("repeat",0,0);
		if (!repeat)
		    break;
		params.setParam("repeat",String(repeat));
		params.clearParam("file",':');
		params.copySubParams(list,"file:",false,true);
	    }
	    params.setParam(YSTRING("first"),String::boolText(start));
	    const char* radioSect = params.getValue("radio_section","radio");
	    RadioTest::start(params,*cfg.createSection(radioSect));
	    break;
	}
    }
    else if (s_test)
	s_test->command(cmd,list);
    else
	Debug(this,DebugInfo,"Test is not running");
    s_exec = false;
    return true;
}

void RadioTestModule::processRadioDataFile(NamedList& params)
{
    Configuration cfg(Engine::configFile(name()));
    const char* s = params.getValue("sect","radiodatafile");
    NamedList* p = cfg.getSection(s);
    if (!p) {
	Debug(this,DebugNote,
	    "Can't handle radio data file process: no section '%s' in config",s);
	return;
    }
    const char* file = p->getValue("input");
    if (!file) {
	Debug(this,DebugNote,"Radio data file process sect='%s': missing file",s);
	return;
    }
    RadioDataFile d("RadioTest");
    if (!d.open(file,0,this))
	return;
    const RadioDataDesc& desc = d.desc();
    String error;
#define RADIO_FILE_ERROR(what,value) { \
    error << what << " " << value; \
    break; \
}
    while (true) {
	if (desc.m_signature[2])
	    RADIO_FILE_ERROR("unhandled version",desc.m_signature[2]);
	if (desc.m_sampleLen != 2)
	    RADIO_FILE_ERROR("unhandled sample length",desc.m_sampleLen);
	if (desc.m_ports != 1)
	    RADIO_FILE_ERROR("unhandled ports",desc.m_ports);
	String fmt;
	unsigned int sz = 0;
	switch (desc.m_elementType) {
	    case RadioDataDesc::Float:
		fmt = p->getValue(YSTRING("fmt-float"),"%+g%+gj");
		sz = sizeof(float);
		break;
	    case RadioDataDesc::Int16:
		fmt = p->getValue(YSTRING("fmt-int"),"%+d%+dj");
		sz = sizeof(int16_t);
		break;
	    default:
		RADIO_FILE_ERROR("unhandled element type",desc.m_elementType);
	}
	if (error)
	    break;
	File fOut;
	const String& output = (*p)[YSTRING("output")];
	if (output && !fOut.openPath(output,true,false,true,false,false,true)) {
	    String tmp;
	    Thread::errorString(tmp,fOut.error());
	    error.printf("Failed to open output file '%s' - %d %s",
		output.c_str(),fOut.error(),tmp.c_str());
	    break;
	}
	const String* sepParam = p->getParam(YSTRING("separator"));
	const char* sep = sepParam ? sepParam->c_str() : " ";
	bool dumpData = fOut.valid() ? true : p->getBoolValue(YSTRING("dumpdata"),true);
	unsigned int dumpStart = p->getIntValue(YSTRING("recstart"),1,1);
	unsigned int dumpCount = p->getIntValue(YSTRING("reccount"),0,0);
	unsigned int dumpMax = p->getIntValue(YSTRING("recsamples"),0,0);
	const String& recFmt = (*p)[YSTRING("recformat")];
	Debug(this,DebugAll,"Processing radio data file '%s'",file);
	NamedList special("");
	special.addParam("newline","\r\n");
	special.addParam("tab","\t");
	special.replaceParams(fmt);
	String fmt4 = fmt + sep + fmt + sep + fmt + sep + fmt;
	uint64_t ts = 0;
	DataBlock buf;
	unsigned int n = 0;
	uint64_t oldTs = 0;
	bool first = true;
	unsigned int sampleBytes = sz * 2;
	while (!Thread::check(false) && d.read(ts,buf,this) && buf.length()) {
	    n++;
	    if ((buf.length() % sampleBytes) != 0) {
		error.printf("record=%u len=%u - length is not a multiple of samples",
		    n,buf.length());
		break;
	    }
	    if (n < dumpStart)
		continue;
	    String str;
	    if (dumpData) {
		if (!d.sameEndian() && !d.fixEndian(buf,sz))
		    RADIO_FILE_ERROR("unhandled endiannes for element type",desc.m_elementType);
		switch (desc.m_elementType) {
		    case RadioDataDesc::Float:
			dumpSamplesFloat(str,buf,fmt4,fmt,sep,dumpMax);
			break;
		    case RadioDataDesc::Int16:
			dumpSamplesInt16(str,buf,fmt4,fmt,sep,dumpMax);
			break;
		    default:
			RADIO_FILE_ERROR("unhandled element type",desc.m_elementType);
		}
		if (error)
		    break;
	    }
	    int64_t delta = 0;
	    if (first)
		first = false;
	    else
		delta = ts - oldTs;
	    oldTs = ts;
	    unsigned int samples = buf.length() / sampleBytes;
	    if (fOut.valid()) {
		if (str) {
		    if (recFmt) {
			NamedList nl("");
			nl.addParam("timestamp",String(ts));
			nl.addParam("data",str);
			nl.addParam("ts-delta",String(delta));
			nl.addParam("samples",String(samples));
			nl.addParam("newline","\r\n");
			nl.addParam("separator",sep);
			str = recFmt;
			nl.replaceParams(str);
		    }
		    else
			str += sep;
		    int wr = str ? fOut.writeData(str.c_str(),str.length()) : str.length();
		    if (wr != (int)str.length()) {
			String tmp;
			Thread::errorString(tmp,fOut.error());
			error.printf("Failed to write (%d/%u) output file '%s' - %d %s",
			    wr,str.length(),output.c_str(),fOut.error(),tmp.c_str());
			break;
		    }
		}
	    }
	    else
		Output("%u: TS=" FMT64U " bytes=%u samples=%u delta=" FMT64 "%s",
		    n,ts,buf.length(),samples,delta,encloseDashes(str,true));
	    if (dumpCount) {
		dumpCount--;
		if (!dumpCount)
		    break;
	    }
	}
	break;
    }
#undef RADIO_FILE_ERROR
    if (error)
	Debug(this,DebugNote,"Processing radio data file '%s': %s",file,error.c_str());
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
