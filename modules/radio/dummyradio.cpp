/**
 * dummyradio.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Dummy radio interface
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015 Null Team
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
#include <string.h>
#include <math.h>

using namespace TelEngine;
namespace { // anonymous

class DummyInterface;
class DummyModule;
class DummyThread;


class DummyInterface : public RadioInterface
{
    YCLASS(DummyInterface,RadioInterface)
    friend class DummyModule;
    friend class DummyThread;
public:
    ~DummyInterface();
    virtual unsigned int getInterface(String& devicePath) const
	{ devicePath = m_address; return 0; }
    virtual unsigned int initialize(const NamedList& params);
    virtual unsigned int setParams(NamedList& params, bool shareFate);
    virtual unsigned int setDataDump(int dir = 0, int level = 0,
	const NamedList* params = 0)
	{ return NotSupported; }
    virtual unsigned int send(uint64_t when, float* samples, unsigned size,
	float* powerScale);
    virtual unsigned int recv(uint64_t& when, float* samples, unsigned& size);
    unsigned int setFrequency(uint64_t hz, bool tx);
    unsigned int getFrequency(uint64_t& hz, bool tx) const
	{ hz = tx ? m_txFreq : m_rxFreq; return 0; }
    virtual unsigned int setTxFreq(uint64_t hz)
	{ return setFrequency(hz,true); }
    virtual unsigned int getTxFreq(uint64_t& hz) const
	{ return getFrequency(hz,true); }
    virtual unsigned int setRxFreq(uint64_t hz)
	{ return setFrequency(hz,false); }
    virtual unsigned int getRxFreq(uint64_t& hz) const
	{ return getFrequency(hz,false); }
    virtual unsigned int setFreqOffset(int offs, int* newVal)
	{ return NotSupported; }
    virtual unsigned int setSampleRate(uint64_t hz);
    virtual unsigned int getSampleRate(uint64_t& hz) const
	{ hz = m_sample; return 0; }
    virtual unsigned int setFilter(uint64_t hz);
    virtual unsigned int getFilterWidth(uint64_t& hz) const
	{ hz = m_filter; return 0; }
    virtual unsigned int getTxTime(uint64_t& time) const
	{ time = getTS(); return 0; }
    virtual unsigned int getRxTime(uint64_t& time) const
	{ time = getTS(); return 0; }
    virtual unsigned int setTxPower(const unsigned dBm);
    virtual unsigned int setPorts(unsigned ports)
	{ return NotSupported; }
    virtual unsigned status(int port = -1) const
	{ return (m_totalErr & FatalErrorMask); }

protected:
    DummyInterface(const char* name, const NamedList& config);
    // Method to call after creation to init the interface
    virtual void destroyed();
    uint64_t getTS() const;
    uint64_t getRxUsec(uint64_t ts) const;
    uint64_t getTxUsec(uint64_t ts) const;
    bool control(NamedList& params);
    inline unsigned int sleep(unsigned int us) {
	    while (us) {
		if (us > Thread::idleUsec()) {
		    Thread::usleep(Thread::idleUsec());
		    us -= Thread::idleUsec();
		}
		else {
		    Thread::usleep(us);
		    us = 0;
		}
		if (Thread::check(false))
		    return Cancelled;
	    }
	    return 0;
	}
    void setRxBuffer(uint64_t& when, float* samples, unsigned int size);

private:
    RadioCapability m_caps;
    String m_address;
    uint64_t m_divisor;
    uint64_t m_startTime;
    uint64_t m_sample;
    uint64_t m_filter;
    uint64_t m_rxFreq;
    uint64_t m_txFreq;
    int m_freqError;
    int m_sampleError;
    unsigned int m_freqStep;
    unsigned int m_sampleStep;
    unsigned int m_filterStep;
    uint64_t m_rxSamp;
    uint64_t m_txSamp;
    DataBlock m_rxDataBuf;               // RX data
    unsigned int m_rxDataBufSamples;     // Number of samples in buffer
    unsigned int m_rxDataChunkSamples;   // Number of samples in a buffer chunk (aligned data)
    unsigned int m_rxDataOffs;           // Current offset in buffer (in samples)
    bool m_profiling;                    // Running a profiling tool
    int16_t m_sampleEnergize;            // TX data sample energize
};

class DummyModule : public Module
{
    friend class DummyInterface;
public:
    enum Relay {
	RadioCreate = Private,
    };
    DummyModule();
    ~DummyModule();

protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);

private:
    bool findIface(RefPointer<DummyInterface>& iface, const String& name);
    DummyInterface* createIface(const NamedList& params);

    unsigned int m_ifaceId;
    ObjList m_ifaces;
};


INIT_PLUGIN(DummyModule);

static Configuration s_cfg;

// Energize a number. Refer the input value to the requested energy
static inline int16_t energize(float value, float scale, int16_t refVal, unsigned int& clamped)
{
    int16_t v = (int16_t)::round(value * scale);
    if (v > refVal) {
	clamped++;
	return refVal;
    }
    if (v < -refVal) {
	clamped++;
	return -refVal;
    }
    return v;
}

// Simulate float to int16_t data conversion:
// - Sample energize
// - Bounds check
static void sampleEnergize(float* samples, unsigned int size, float scale,
    unsigned int refVal, unsigned int& clamped)
{
    if (!samples)
	return;
    int16_t buf[1024];
    float scaleI = scale * refVal;
    float scaleQ = scale * refVal;
    while (size) {
	int16_t* b = buf;
	unsigned int n = size > 512 ? 512 : size;
	size -= n;
	while (n--) {
	    *b++ = energize(*samples++,scaleI,refVal,clamped);
	    *b++ = energize(*samples++,scaleQ,refVal,clamped);
	}
    }
}


//
// DummyInterface
//
DummyInterface::DummyInterface(const char* name, const NamedList& config)
    : RadioInterface(name),
      m_startTime(0), m_sample(0), m_filter(0), m_rxFreq(0), m_txFreq(0),
      m_rxDataBufSamples(0), m_rxDataChunkSamples(0), m_rxDataOffs(0),
      m_profiling(false), m_sampleEnergize(0)
{
    debugChain(&__plugin);
    m_address << __plugin.name() << "/" << config;
    m_divisor = 1000000 * config.getInt64Value("slowdown",1,1,1000);
    m_caps.maxPorts = 1;
    m_caps.currPorts = 1;
    m_caps.maxTuneFreq = config.getInt64Value("maxTuneFreq",5000000000LL,100000000LL,50000000000LL);
    m_caps.minTuneFreq = config.getInt64Value("minTuneFreq",500000000LL,250000000LL,5000000000LL);
    m_caps.maxSampleRate = config.getIntValue("maxSampleRate",20000000,5000000,50000000);
    m_caps.minSampleRate = config.getIntValue("minSamplerate",250000,50000,5000000);
    m_caps.maxFilterBandwidth = config.getIntValue("maxFilterBandwidth",5000000,5000000,50000000);
    m_caps.minFilterBandwidth = config.getIntValue("minFilterBandwidth",1500000,100000,5000000);
    if (config.getParam("rx_latency") || config.getParam("tx_latency"))
	Debug(this,DebugConf,"rx_latency/tx_latency are obsolete, please use rxLatency/txLatency");
    m_caps.rxLatency = config.getIntValue("rxLatency",10000,0,50000);
    m_caps.txLatency = config.getIntValue("txLatency",10000,0,50000);
    m_freqError = config.getIntValue("freq_error",0,-10000,10000);
    m_freqStep = config.getIntValue("freq_step",1,1,10000000);
    m_sampleError = config.getIntValue("sample_error",0,-1000,1000);
    m_sampleStep = config.getIntValue("sample_step",1,1,1000);
    m_filterStep = config.getIntValue("filter_step",250000,100000,5000000);
    m_radioCaps = &m_caps;
    m_profiling = config.getBoolValue("profiling",false);
    m_sampleEnergize = config.getIntValue("sample_energize",0,0,10000);
    const String& rxFile = config["rx_file_raw"];
    if (rxFile) {
	File f;
	const char* oper = 0;
	if (f.openPath(rxFile)) {
	    int64_t len = f.length();
	    if (len > 0) {
		if ((len % (2 * sizeof(float))) == 0) {
		    m_rxDataBuf.resize(len);
		    int rd = f.readData(m_rxDataBuf.data(),m_rxDataBuf.length());
		    if (rd != (int)m_rxDataBuf.length()) {
			oper = "read";
			m_rxDataBuf.clear();
		    }
		}
		else
		    Debug(this,DebugConf,"Invalid RX file file '%s' length " FMT64 " [%p]",
			rxFile.c_str(),len,this);
	    }
	    else
		oper = "get length";
	}
	else
	    oper = "open";
	if (oper) {
	    String tmp;
	    Thread::errorString(tmp,f.error());
	    Debug(this,DebugMild,"RX file '%s' %s failed: %d %s [%p]",
		rxFile.c_str(),oper,f.error(),tmp.c_str(),this);
	}
    }
    m_rxDataBufSamples = m_rxDataBuf.length() / (2 * sizeof(float));
    if (m_rxDataBufSamples) {
	m_rxDataChunkSamples = config.getIntValue("rx_buf_chunk",0,0);
	if (m_rxDataChunkSamples && (m_rxDataBufSamples % m_rxDataChunkSamples) != 0) {
	    Debug(this,DebugConf,
		"Ignoring rx_buf_chunk=%u: not a multiple of rx buffer samples %u [%p]",
		m_rxDataChunkSamples,m_rxDataBufSamples,this);
	    m_rxDataChunkSamples = 0;
	}
    }
    Debug(this,DebugAll,"Interface created [%p]",this);
}

DummyInterface::~DummyInterface()
{
    Debug(this,DebugAll,"Interface destroyed [%p]",this);
}

unsigned int DummyInterface::initialize(const NamedList& params)
{
    m_rxSamp = 0;
    m_txSamp = 0;
    m_startTime = Time::now();
    // TODO
    return status();
}

unsigned int DummyInterface::setParams(NamedList& params, bool shareFate)
{
    unsigned int code = 0;
    NamedList failed("");
#ifdef XDEBUG
    String tmp;
    params.dump(tmp,"\r\n");
    Debug(this,DebugAll,"setParams [%p]\r\n-----\r\n%s\r\n-----",this,tmp.c_str());
#endif
    for (ObjList* o = params.paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* ns = static_cast<NamedString*>(o->get());
	if (!ns->name().startsWith("cmd:"))
	    continue;
	String cmd = ns->name().substr(4);
	if (!cmd)
	    continue;
	unsigned int err = 0;
	if (cmd == YSTRING("setSampleRate"))
	    err = setSampleRate(ns->toInt64(0,0,0));
	else if (cmd == YSTRING("setFilter"))
	    err = setFilter(ns->toInt64(0,0,0));
	else {
	    Debug(this,DebugNote,"setParams: unhandled cmd '%s' [%p]",cmd.c_str(),this);
	    err = NotSupported;
	}
	if (err) {
	    if (!code || code == Pending)
		code = err;
	    failed.addParam(cmd + "_failed",String(err));
	    if (shareFate && err != Pending)
		break;
	}
    }
    if (code)
	params.copyParams(failed);
#ifdef XDEBUG
    tmp.clear();
    params.dump(tmp,"\r\n");
    Debug(this,DebugAll,"setParams [%p]\r\n-----\r\n%s\r\n-----",this,tmp.c_str());
#endif
    return code | status();
}

uint64_t DummyInterface::getTS() const
{
    if (!m_startTime)
	return 0;
    uint64_t usec = Time::now() - m_startTime;
    return (m_sample * usec + 500000) / m_divisor;
}

uint64_t DummyInterface::getRxUsec(uint64_t ts) const
{
    if (!(m_startTime && m_sample))
	return 0;
    if (ts < m_caps.rxLatency)
	return m_startTime;
    return (ts - m_caps.rxLatency) * m_divisor / m_sample + m_startTime;
}

uint64_t DummyInterface::getTxUsec(uint64_t ts) const
{
    if (!(m_startTime && m_sample))
	return 0;
    if (ts < m_caps.txLatency)
	return m_startTime;
    return (ts - m_caps.txLatency) * m_divisor / m_sample + m_startTime;
}

unsigned int DummyInterface::send(uint64_t when, float* samples, unsigned size, float* powerScale)
{
    if (!(m_startTime && m_sample))
	return NotInitialized;
    float scale = powerScale ? *powerScale : 1.0f;
    unsigned int clamped = 0;
    if (m_sampleEnergize)
	sampleEnergize(samples,size,scale,m_sampleEnergize,clamped);
    else
	for (unsigned int i = 0; i < 2 * size; i++)
	    if (::fabs(scale * samples[i]) > 1.0f)
		clamped++;
    unsigned int res = 0;
    int64_t delta = getTxUsec(when) - Time::now();
    if (delta > 0) {
	if (m_profiling && delta > (int64_t)Thread::idleUsec())
	    delta = Thread::idleUsec();
	res = sleep(delta);
	// Stop if operation was cancelled
	if (res)
	    return res;
    }
    else if (!m_profiling && delta < 0)
	res = TooLate;
    if (clamped) {
	Debug(this,DebugNote,"Tx data clamped %u/%u [%p]",clamped,size,this);
	res |= Saturation;
    }
    if (when != m_txSamp)
	Debug(this,DebugNote,"Tx discontinuity of " FMT64 ": " FMT64U " -> " FMT64U,
	    (int64_t)(when - m_txSamp),m_txSamp,when);
    m_txSamp = when + size;
    return res | status();
}

unsigned int DummyInterface::recv(uint64_t& when, float* samples, unsigned int& size)
{
    if (!(m_startTime && m_sample))
	return NotInitialized;
    int64_t delta = getRxUsec(when) - Time::now();
    unsigned int res = 0;
    if (delta > 0) {
	// Requested timestamp is in the future
	unsigned int res = sleep(delta);
	// Stop if operation was cancelled
	if (res)
	    return res | status();
    }
    else if (!m_profiling && delta < 0)
	res = TooEarly;
    if (!res && m_rxDataBufSamples)
	setRxBuffer(when,samples,size);
    m_rxSamp = when + size;
    return res | status();
}

unsigned int DummyInterface::setFrequency(uint64_t hz, bool tx)
{
    Debug(this,DebugCall,"setFrequency(" FMT64U ",%s) [%p]",
	hz,(tx ? "tx" : "rx"),this);
    if (hz < m_caps.minTuneFreq || hz > m_caps.maxTuneFreq)
	return OutOfRange;
    uint64_t& freq = tx ? m_txFreq : m_rxFreq;
    freq = m_freqStep * ((hz + m_freqStep / 2) / m_freqStep) + m_freqError;
    return ((hz == freq) ? NoError : NotExact) | status();
}

unsigned int DummyInterface::setSampleRate(uint64_t hz)
{
    Debug(this,DebugCall,"setSampleRate(" FMT64U ") [%p]",hz,this);
    if (hz < m_caps.minSampleRate || hz > m_caps.maxSampleRate)
	return OutOfRange;
    m_sample = m_sampleStep * ((hz + m_sampleStep / 2) / m_sampleStep) + m_sampleError;
    return ((hz == m_sample) ? NoError : NotExact) | status();
}

unsigned int DummyInterface::setFilter(uint64_t hz)
{
    Debug(this,DebugCall,"setFilter(" FMT64U ") [%p]",hz,this);
    if (hz < m_caps.minFilterBandwidth || hz > m_caps.maxFilterBandwidth)
	return OutOfRange;
    m_filter = m_filterStep * ((hz + m_filterStep / 2) / m_filterStep);
    return ((hz == m_filter) ? NoError : NotExact) | status();
}

unsigned int DummyInterface::setTxPower(const unsigned dBm)
{
    Debug(this,DebugCall,"setTxPower(%u) [%p]",dBm,this);
    return status();
}

void DummyInterface::destroyed()
{
    Debug(this,DebugAll,"Destroying %s [%p]",m_address.c_str(),this);
    Lock lck(__plugin);
    __plugin.m_ifaces.remove(this,false);
    lck.drop();
    RadioInterface::destroyed();
}

bool DummyInterface::control(NamedList& params)
{
    const String& oper = params[YSTRING("operation")];
    if (oper == YSTRING("hwioerr"))
	m_totalErr |= HardwareIOError;
    else if (oper == YSTRING("rfhwfail"))
	m_totalErr |= RFHardwareFail;
    else if (oper == YSTRING("envfault"))
	m_totalErr |= EnvironmentalFault;
    else if (oper == YSTRING("rfhwchange"))
	m_lastErr |= RFHardwareChange;
    else
	return false;
    return true;
}

void DummyInterface::setRxBuffer(uint64_t& when, float* samples, unsigned int size)
{
    if (!(m_rxDataBufSamples && samples && size))
	return;
    // Align upper layer RX timestamp if buffer size is given
    //  and it requested a multiple of it
    if (m_rxDataChunkSamples && (when % m_rxDataChunkSamples) == 0) {
	uint64_t ts = getTS();
	if (ts > when)
	    when += m_rxDataChunkSamples * ((ts - when) / m_rxDataChunkSamples);
    }
    // Skip samples in RX buffer according to requested timestamp
    if (m_rxSamp) {
	uint64_t skip = 0;
	if (m_rxSamp < when)
	    skip = (when - m_rxSamp) % m_rxDataBufSamples;
	else
	    skip = (m_rxSamp - when) % m_rxDataBufSamples;
	if (skip) {
	    m_rxDataOffs += skip;
	    if (m_rxDataOffs >= m_rxDataBufSamples)
		m_rxDataOffs -= m_rxDataBufSamples;
	}
    }
    // Force data align
    // Align data offset at chunk size if timestamp is multiple of chunk size
    if (m_rxDataChunkSamples && m_rxDataOffs && (when % m_rxDataChunkSamples) == 0) {
	unsigned int delta = m_rxDataOffs % m_rxDataChunkSamples;
	if (delta) {
	    m_rxDataOffs += m_rxDataChunkSamples - delta;
	    if (m_rxDataOffs >= m_rxDataBufSamples)
		m_rxDataOffs -= m_rxDataBufSamples;
	}
    }
    const float* buf = (float*)m_rxDataBuf.data();
    while (size) {
	unsigned int cpSamples = m_rxDataBufSamples - m_rxDataOffs;
	if (!cpSamples) {
	    m_rxDataOffs = 0;
	    cpSamples = m_rxDataBufSamples;
	}
	if (cpSamples > size)
	    cpSamples = size;
	::memcpy(samples,buf + 2 * m_rxDataOffs,cpSamples * 2 * sizeof(float));
	size -= cpSamples;
	m_rxDataOffs += cpSamples;
	samples += 2 * cpSamples;
    }
}


//
// DummyModule
//
DummyModule::DummyModule()
    : Module("dummyradio","misc",true),
    m_ifaceId(0)
{
    String tmp;
    Output("Loaded module DummyRadio");
}

DummyModule::~DummyModule()
{
    Output("Unloading module DummyRadio");
    if (m_ifaces.skipNull())
	Debug(this,DebugWarn,"Exiting with %u interface(s) in list!!!",m_ifaces.count());
}

void DummyModule::initialize()
{
    Output("Initializing module DummyRadio");
    lock();
    s_cfg = Engine::configFile(name());
    s_cfg.load();
    NamedList* gen = s_cfg.createSection(YSTRING("general"));
    unlock();
    if (!relayInstalled(RadioCreate)) {
	setup();
	installRelay(Halt);
	installRelay(Control);
	installRelay(RadioCreate,"radio.create",gen->getIntValue("priority",110));
    }
}

bool DummyModule::received(Message& msg, int id)
{
    if (id == RadioCreate) {
	if (msg[YSTRING("radio_driver")] != name())
	    return false;
	DummyInterface* ifc = createIface(msg);
	if (ifc) {
	    msg.setParam(new NamedPointer("interface",ifc,name()));
	    return true;
	}
	msg.setParam(YSTRING("error"),"failure");
	return false;
    }
    if (id == Control) {
	RefPointer<DummyInterface> ifc;
	if (findIface(ifc,msg[YSTRING("component")]))
	    return ifc->control(msg);
	return false;
    }
    return Module::received(msg,id);
}

bool DummyModule::findIface(RefPointer<DummyInterface>& iface, const String& name)
{
    Lock lck(this);
    ObjList* o = m_ifaces.find(name);
    if (o)
        iface = static_cast<DummyInterface*>(o->get());
    return iface != 0;
}

DummyInterface* DummyModule::createIface(const NamedList& params)
{
    Lock lck(this);
    const char* profile = params.getValue(YSTRING("profile"),"general");
    NamedList* sect = s_cfg.getSection(profile);
    if (!sect)
	return 0;
    NamedList p(*sect);
    // Override parameters from received params
    const char* prefix = params.getValue(YSTRING("radio_params_prefix"),"radio.");
    if (prefix)
	p.copySubParams(params,prefix,true,true);
    DummyInterface* ifc = new DummyInterface(name() + "/" + String(++m_ifaceId),p);
    m_ifaces.append(ifc)->setDelete(false);
    return ifc;
}

void DummyModule::statusParams(String& str)
{
    Module::statusParams(str);
    Lock lck(this);
    str.append("ifaces=",",") << m_ifaces.count();
}

bool DummyModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (partLine == YSTRING("control")) {
	Lock lck(this);
	for (ObjList* o = m_ifaces.skipNull(); o; o = o->skipNext()) {
	    RefPointer<DummyInterface> ifc = static_cast<DummyInterface*>(o->get());
	    if (ifc)
		itemComplete(msg.retValue(),ifc->toString(),partWord);
	}
	return false;
    }
    String tmp = partLine;
    if (tmp.startSkip("control")) {
	RefPointer<DummyInterface> ifc;
	if (findIface(ifc,tmp)) {
	    itemComplete(msg.retValue(),"hwioerr",partWord);
	    itemComplete(msg.retValue(),"rfhwfail",partWord);
	    itemComplete(msg.retValue(),"envfault",partWord);
	    itemComplete(msg.retValue(),"rfhwchange",partWord);
	    return true;
	}
    }
    return Module::commandComplete(msg,partLine,partWord);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
