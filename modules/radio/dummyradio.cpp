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
#include <unistd.h>
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
    virtual unsigned int getTime(uint64_t& time) const
	{ time = getTS(); return 0; }
    virtual unsigned int setTxPower(const unsigned dBm);
    virtual unsigned int setPorts(unsigned ports) const
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
    unsigned int m_rxLatency;
    unsigned int m_txLatency;
    uint64_t m_rxSamp;
    uint64_t m_txSamp;
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


//
// DummyInterface
//
DummyInterface::DummyInterface(const char* name, const NamedList& config)
    : RadioInterface(name),
      m_startTime(0), m_sample(0), m_filter(0), m_rxFreq(0), m_txFreq(0)
{
    debugChain(&__plugin);
    m_address << __plugin.name() << "/" << config;
    m_divisor = 1000000 * config.getInt64Value("slowdown",1,1,1000);
    m_caps.maxPorts = 1;
    m_caps.currPorts = 1;
    m_caps.maxTuneFreq = config.getInt64Value("maxTuneFreq",5000000000LL,100000000LL,50000000000LL);
    m_caps.minTuneFreq = config.getInt64Value("minTuneFreq",500000000LL,250000000LL,5000000000LL);
    m_caps.maxOutputPower = config.getIntValue("maxOutputPower",30,0,50);
    m_caps.minOutputPower = config.getIntValue("minOutputPower",0,-10,30);
    m_caps.maxInputSaturation = config.getIntValue("maxInputSaturation",-30,-50,0);
    m_caps.minInputSaturation = config.getIntValue("minInputSaturation",-50,-80,-30);
    m_caps.maxSampleRate = config.getIntValue("maxSampleRate",20000000,5000000,50000000);
    m_caps.minSampleRate = config.getIntValue("minSamplerate",250000,50000,5000000);
    m_caps.maxFilterBandwidth = config.getIntValue("maxFilterBandwidth",5000000,5000000,50000000);
    m_caps.minFilterBandwidth = config.getIntValue("minFilterBandwidth",1500000,100000,5000000);
    m_freqError = config.getIntValue("freq_error",0,-10000,10000);
    m_freqStep = config.getIntValue("freq_step",1,1,10000000);
    m_sampleError = config.getIntValue("sample_error",0,-1000,1000);
    m_sampleStep = config.getIntValue("sample_step",1,1,1000);
    m_filterStep = config.getIntValue("filter_step",250000,100000,5000000);
    m_rxLatency = config.getIntValue("rx_latency",10000,0,50000);
    m_txLatency = config.getIntValue("tx_latency",10000,0,50000);
    m_radioCaps = &m_caps;
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
    if (ts < m_rxLatency)
	return m_startTime;
    return (ts - m_rxLatency) * m_divisor / m_sample + m_startTime;
}

uint64_t DummyInterface::getTxUsec(uint64_t ts) const
{
    if (!(m_startTime && m_sample))
	return 0;
    if (ts < m_txLatency)
	return m_startTime;
    return (ts - m_txLatency) * m_divisor / m_sample + m_startTime;
}

unsigned int DummyInterface::send(uint64_t when, float* samples, unsigned size, float* powerScale)
{
    if (!(m_startTime && m_sample))
	return NotInitialized;
    float scale = powerScale ? *powerScale : 1.0f;
    unsigned int res = 0;
    int64_t delta = getTxUsec(when) - Time::now();
    if (delta > 0)
	::usleep(delta);
    else
	res |= TooLate;
    for (unsigned int i = 0; i < 2 * size; i++) {
	if (::fabs(scale * samples[i]) > 1.0f)
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
    // TODO
     return NotSupported;
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
    return ((dBm >= m_caps.minOutputPower && dBm <= m_caps.minOutputPower) ? NoError : NotExact) | status();
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
