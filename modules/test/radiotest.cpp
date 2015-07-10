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

using namespace TelEngine;
namespace { // anonymous

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
    bool setTxData();
    bool execute(const String& cmd, const String& param, bool fatal,
	const NamedList* params);
    bool execute(const NamedList& cmds, const char* prefix);
    bool write();
    bool read();
    inline void updateTs(bool tx) {
	    uint64_t ts = 0;
	    if ((tx ? m_radio->getTxTime(ts) : m_radio->getRxTime(ts)) == 0)
		(tx ? m_tx.ts : m_rx.ts) = ts;
	}

    RadioInterface* m_radio;
    bool m_started;
    NamedList m_init;
    NamedList m_params;
    NamedList m_radioParams;
    // TX
    RadioTestIO m_tx;
    unsigned int m_sendBufCount;
    DataBlock m_sendBufData;
    unsigned int m_sendBufSamples;
    // RX
    RadioTestIO m_rx;
    RadioReadBufs m_bufs;
    unsigned int m_skippedBuffs;
    DataBlock m_crt;
    DataBlock m_aux;
    DataBlock m_extra;
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
};

INIT_PLUGIN(RadioTestModule);
static RadioTest* s_test = 0;
static Mutex s_testMutex(false,"RadioTest");

static const char* encloseDashes(String& s, bool extra = false)
{
    static const String s1 = "\r\n-----";
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

static String& appendComplex(String& s, const float* f, unsigned int n, bool fmfG = true)
{
    static const char* s_fmtf1 = "(%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)";
    static const char* s_fmtf2 = "(%.3f,%.3f)";
    static const char* s_fmtg1 = "(%.3g,%.3g) (%.3g,%.3g) (%.3g,%.3g) (%.3g,%.3g)";
    static const char* s_fmtg2 = "(%.3g,%.3g)";

    const char* fmt1 = fmfG ? s_fmtg1 : s_fmtf1;
    const char* fmt2 = fmfG ? s_fmtg2 : s_fmtf2;
    n /= 2;
    if (!(f && n))
	return s;
    String tmp;
    String tmp2;
    unsigned int a = n / 4;
    while (a--) {
	tmp2.printf(512,fmt1,f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7]);
	f += 8;
	tmp.append(tmp2," ");
    }
    a = n % 4;
    while (a--) {
	tmp2.printf(fmt2,f[0],f[1]);
	f += 2;
	tmp.append(tmp2," ");
    }
    return s.append(tmp);
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
    : Thread("RadioTest"),
    m_radio(0),
    m_started(false),
    m_init(""),
    m_params(params),
    m_radioParams(radioParams),
    m_tx(true),
    m_sendBufCount(0),
    m_sendBufSamples(0),
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
	String s;
	m_init.dump(s,"\r\n");
	Debug(this,DebugInfo,"Starting [%p]%s",this,encloseDashes(s,true));
	// Run
	while (!Thread::check(false)) {
	    if (m_tx.enabled && !write())
		break;
	    if (m_rx.enabled && !read())
		break;
	}
	break;
    }
    terminated();
}

void RadioTest::terminated()
{
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
	s << "\r\n" << (io.tx ? "sent=" : "recv=") << io.transferred;
	if (io.transferred) {
	    unsigned int sec = (unsigned int)((now - io.startTime) / 1000000);
	    if (sec)
		s << " (avg: " << (io.transferred / sec) << " samples/sec)";
	}
    }
    Debug(this,DebugInfo,"Terminated [%p]%s",this,encloseDashes(s));
}

bool RadioTest::setTxData()
{
    const String& pattern = m_params["txdata"];
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
    String tmp;
    m_init.addParam("txdata",appendComplex(tmp,m_sendBufData));
    int n = m_params.getIntValue("txdata_repeat");
    if (n > 0) {
	DataBlock tmp = m_sendBufData;
	while (n--)
	    m_sendBufData += tmp;
    }
    m_sendBufSamples = bytes2samplesf(m_sendBufData.length());
    m_init.addParam("send-samples",String(m_sendBufSamples));
    return true;
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
    else if (cmd == YSTRING("calibrate"))
	c = m_radio->autocalDCOffsets();
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
    if (!m_tx.ts)
	updateTs(true);
    unsigned int code = m_radio->send(m_tx.ts,(float*)m_sendBufData.data(),
	m_sendBufSamples);
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
    // TODO: Implement
    Debug(this,DebugStub,"Read not implemented");
    return false;
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
	return false;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

bool RadioTestModule::onCmdControl(Message& msg)
{
    static const char* s_help =
	"\r\ncontrol module_name {start|stop|exec}"
	"\r\n  Test commands"
	"\r\ncontrol module_name help"
	"\r\n  Display control commands help";

    const String& cmd = msg[YSTRING("operation")];
    if (cmd == YSTRING("help")) {
	msg.retValue() << s_help;
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
	    if (!s_test)
		break;
	    s_test->cancel();
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
	    Debug(this,DebugWarn,"Hard cancelling test thread (%p)",s_test);
	    s_test->cancel(true);
	    s_test = 0;
	}
	if (start) {
	    Configuration cfg(Engine::configFile(name()));
	    String n = list.getValue("name","general");
	    NamedList* sect = n ? cfg.getSection(n) : 0;
	    if (sect)
		RadioTest::start(*sect,*cfg.createSection("radio"));
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

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
