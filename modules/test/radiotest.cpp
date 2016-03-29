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
    inline float* sendBuf()
	{ return (float*)m_sendBufData.data(0); }

    RadioInterface* m_radio;
    RadioTestRecv* m_recv;
    bool m_started;
    NamedList m_init;
    NamedList m_params;
    NamedList m_radioParams;
    // TX
    RadioTestIO m_tx;
    bool m_newTxData;
    unsigned int m_phase;
    unsigned int m_sendBufCount;
    DataBlock m_sendBufData;
    unsigned int m_sendBufSamples;
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

static inline unsigned int bytes2samplesf(unsigned int len)
{
    return len / (2 * sizeof(float));
}

// Convert a string to double
// Make sure the value is in interval [-1..1]
static inline double getDoubleSample(const String& str, double defVal = 0)
{
    double d = str.toDouble(defVal);
    if (d < -1)
	return -1;
    if (d > 1)
	return 1;
    return d;
}

static inline void getComplexSample(Complex& c, const String& str,
    double defRe, double defIm)
{
    int pos = str.find(',');
    if (pos >= 0)
	c.set(getDoubleSample(str.substr(0,pos),defRe),
	    getDoubleSample(str.substr(pos + 1),defIm));
    else
	c.set(getDoubleSample(str,defRe),defIm);
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

static String& appendComplex(String& s, const float* f, unsigned int n, bool fmfG = true)
{
    static const char* s_fmtf1 = "(%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)";
    static const char* s_fmtf2 = "(%.3f,%.3f)";
    static const char* s_fmtg1 = "(%.3g,%.3g) (%.3g,%.3g) (%.3g,%.3g) (%.3g,%.3g)";
    static const char* s_fmtg2 = "(%.3g,%.3g)";

    const char* fmt1 = fmfG ? s_fmtg1 : s_fmtf1;
    const char* fmt2 = fmfG ? s_fmtg2 : s_fmtf2;
    DataBlock tmp((void*)f,n * sizeof(float),false);
    dumpSamplesFloat(s,tmp,fmt1,fmt2," ");
    tmp.clear(false);
    return s;
}

static inline String& appendComplex(String& s, const DataBlock& buf)
{
    return appendComplex(s,(const float*)buf.data(),buf.length() / sizeof(float));
}

// Read samples from string
static bool readSamples(DataBlock& buf, const String& list)
{
    ObjList* l = list.split(',');
    unsigned int n = l->count();
    if (n < 2 || (n % 2) != 0) {
	TelEngine::destruct(l);
	return false;
    }
    buf.resize(n * sizeof(float));
    float* f = (float*)buf.data(0);
    for (ObjList* o = l->skipNull(); f && o; o = o->skipNext()) {
	*f = static_cast<String*>(o->get())->toDouble();
	if (*f >= -1 && *f <= 1)
	    f++;
	else
	    f = 0;
    }
    TelEngine::destruct(l);
    if (!f)
	buf.clear();
    return f != 0;
}


//
// RadioTest
//
RadioTest::RadioTest(const NamedList& params, const NamedList& radioParams)
    : Thread("RadioTest",Thread::priority(params["priority"])),
    m_radio(0),
    m_recv(0),
    m_started(false),
    m_init(""),
    m_params(params),
    m_radioParams(radioParams),
    m_tx(true),
    m_newTxData(false),
    m_phase(0),
    m_sendBufCount(0),
    m_sendBufSamples(0),
    m_pulse(0),
    m_rx(false),
    m_skippedBuffs(0)
{
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
    Debug(this,DebugInfo,"Initializing... [%p]",this);
    while (true) {
	// Init
	// Init test data
	m_tx.enabled = true;
	if (!setTxData())
	    break;
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
	bool ok = Engine::dispatch(m);
	NamedPointer* np = YOBJECT(NamedPointer,m.getParam(YSTRING("interface")));
	m_radio = np ? YOBJECT(RadioInterface,np) : 0;
	if (!m_radio) {
	    const String& e = m[YSTRING("error")];
	    Debug(this,DebugNote,"Failed to create radio interface: %s",
		e.safe(ok ? "Missing interface" : "Message not handled"));
	    break;
	}
	np->takeData();
	if (!execute(m_params,"init:"))
	    break;
	if (m_radio->initialize() != 0) {
	    Debug(this,DebugNote,"Failed to initialize radio interface [%p]",this);
	    break;
	}
	if (!execute(m_params,"cmd:"))
	    break;
	if (m_params.getBoolValue("init_only"))
	    break;
	if (m_rx.enabled) {
	    m_recv = RadioTestRecv::start(this);
	    if (!m_recv) {
		Debug(this,DebugWarn,"Failed to start read data thread [%p]",this);
		break;
	    }
	}
	String s;
	m_init.dump(s,"\r\n");
	Debug(this,DebugInfo,"Starting [%p]%s",this,encloseDashes(s,true));
	// Run
	while (!Thread::check(false) && write()) {
	    if (m_rx.enabled && !m_recv && !Thread::check(false)) {
		Debug(this,DebugWarn,"Read data thread abnormally terminated [%p]",this);
		break;
	    }
	}
	readStop();
	break;
    }
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
}

bool RadioTest::setTxData()
{
    const String& pattern = m_params["txdata"];
    bool dataRepeat = true;
    bool dataDump = true;
    m_newTxData = false;
    if (pattern) {
	if (pattern == "single-tone" || pattern == "circle") {
	    float tmp[] = {1,0,0,1,-1,0,0,-1};
	    m_sendBufData.assign(tmp,sizeof(tmp));
	    m_init.addParam("txpattern",pattern);
	}
	else if (pattern == "zero") {
	    float tmp[] = {0,0};
	    m_sendBufData.assign(tmp,sizeof(tmp));
	    m_init.addParam("txpattern",pattern);
	}
	else if (pattern == "two-circles") {
	    unsigned int samples = m_params.getIntValue("txdata_length",819,50);
	    m_sendBufData.assign(0,samplesf2bytes(samples));
	    m_newTxData = true;
	    m_phase = 0;
	    dataRepeat = false;
	    dataDump = false;
	    m_init.addParam("txpattern",pattern);
	}
	else if (pattern == "pulse") {
	    unsigned int samples = m_params.getIntValue("txdata_length",10000,50);
	    m_sendBufData.assign(0,samplesf2bytes(samples));
	    unsigned int defVal = (samples > 2) ? (samples - 2) : 2;
	    m_pulse = m_params.getIntValue("pulse",defVal,2,10000000);
	    m_pulseData.resetStorage(2);
	    getComplexSample(m_pulseData[0],m_params["pulse1"],1,1);
	    getComplexSample(m_pulseData[1],m_params["pulse2"],-1,-1);
	    m_newTxData = true;
	    m_phase = 0;
	    dataRepeat = false;
	    dataDump = false;
	    m_init.addParam("txpattern",pattern);
	    m_init.addParam("pulse",String(m_pulse));
	}
	else {
	    if (!readSamples(m_sendBufData,pattern)) {
		Debug(this,DebugConf,"Invalid tx data pattern [%p]",this);
		return false;
	    }
	    m_init.addParam("txpattern","...");
	}
    }
    else {
	Debug(this,DebugConf,"Missing tx data pattern [%p]",this);
	return false;
    }
    if (dataDump) {
	String tmp;
	m_init.addParam("txdata",appendComplex(tmp,m_sendBufData));
    }
    if (dataRepeat) {
	int n = m_params.getIntValue("txdata_repeat");
	if (n > 0) {
	    DataBlock tmp = m_sendBufData;
	    while (n--)
		m_sendBufData += tmp;
	}
    }
    m_sendBufSamples = bytes2samplesf(m_sendBufData.length());
    m_init.addParam("send-samples",String(m_sendBufSamples));
    return true;
}

void RadioTest::regenerateTxData()
{
    // Fs / 4 data
    static const float s_cs4[4] = {1,0,-1,0};
    // Fs / 8 data
    static const float s_r2 = M_SQRT1_2;
    static const float s_cs8[8] = {1,s_r2,0,-s_r2,-1,-s_r2,0,s_r2};

    float* ip = sendBuf();
    if (m_pulse)
	for (unsigned int i = 0; i < m_sendBufSamples; i++, ip += 2, m_phase++) {
	    unsigned int idx = m_phase % m_pulse;
	    if (idx < m_pulseData.length()) {
		Complex& c = m_pulseData[idx];
		ip[0] = c.re();
		ip[1] = c.im();
	    }
	    else
		::memset(ip,0,2 * sizeof(float));
	}
    else
	for (unsigned int i = 0; i < m_sendBufSamples; i++) {
	    *ip++ = 0.5 * (s_cs4[m_phase % 4] + s_cs8[m_phase % 8]);
	    *ip++ = -0.5 * (s_cs4[(m_phase + 1) % 4] + s_cs8[(m_phase + 2) % 8]);
	    m_phase++;
	}
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
    unsigned int code = m_radio->send(m_tx.ts,sendBuf(),m_sendBufSamples);
    if (!code) {
	m_tx.ts += m_sendBufSamples;
	m_tx.transferred += m_sendBufSamples;
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
    unsigned int n = (5000 + Thread::idleMsec()) / Thread::idleMsec();
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
	installRelay(Halt);
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
    if (start || !cmd || cmd == YSTRING("stop")) {
	// Stop the test
	while (s_test) {
	    s_test->cancel();
	    if (s_test->m_recv)
		s_test->m_recv->cancel();
	    lck.drop();
	    // Wait for 5 seconds before hard cancelling
	    unsigned int n = (5000 + Thread::idleMsec()) / Thread::idleMsec();
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
	if (start) {
	    Configuration cfg(Engine::configFile(name()));
	    String n = list.getValue("name","test");
	    NamedList* sect = n ? cfg.getSection(n) : 0;
	    if (sect) {
		NamedList params(sect->c_str());
		const char* inc = sect->getValue(YSTRING("include"));
		if (inc)
		    params.copyParams(*cfg.createSection(inc));
		params.copyParams(*sect);
		RadioTest::start(params,*cfg.createSection("radio"));
	    }
	    else
		Debug(this,DebugNote,"Failed to start test '%s': missing config section",
		    n.c_str());
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
