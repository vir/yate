/**
 * tonedetect.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Detectors for various tones
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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

#include <math.h>

using namespace TelEngine;

namespace { // anonymous

// minimum square of signal energy to even consider detecting
#define THRESHOLD2_ABS 5e+06
// relative square of spectral power from total signal power
#define THRESHOLD2_REL_FAX 0.85
#define THRESHOLD2_REL_ALL 0.75
#define THRESHOLD2_REL_DTMF 0.2

// minimum DTMF detect time
#define DETECT_DTMF_MSEC 32

// 2-pole filter parameters
typedef struct
{
    double gain;
    double y0;
    double y1;
} Params2Pole;

// Half 2-pole filter - the other part is common to all filters
class Tone2PoleFilter
{
public:
    inline Tone2PoleFilter()
	: m_mult(0.0), m_y0(0.0), m_y1(0.0)
	{ }
    inline Tone2PoleFilter(double gain, double y0, double y1)
	: m_mult(1.0/gain), m_y0(y0), m_y1(y1)
	{ init(); }
    inline Tone2PoleFilter(const Params2Pole& params)
	: m_mult(1.0/params.gain), m_y0(params.y0), m_y1(params.y1)
	{ init(); }
    inline void assign(const Params2Pole& params)
	{ m_mult = 1.0/params.gain; m_y0 = params.y0; m_y1 = params.y1; init(); }
    inline void init()
	{ m_val = m_y[1] = m_y[2] = 0.0; }
    inline double value() const
	{ return m_val; }
    void update(double xd);
private:
    double m_mult;
    double m_y0;
    double m_y1;
    double m_val;
    double m_y[3];
};

class ToneConsumer : public DataConsumer
{
    YCLASS(ToneConsumer,DataConsumer)
public:
    ToneConsumer(const String& id, const String& name);
    virtual ~ToneConsumer(); 
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    virtual const String& toString() const
	{ return m_name; }
    inline const String& id() const
	{ return m_id; }
    void setDivert(const Message& msg);
    void init();
private:
    void checkDtmf();
    void checkFax();
    String m_id;
    String m_name;
    String m_divert;
    String m_caller;
    String m_called;
    bool m_detFax;
    bool m_detDtmf;
    char m_dtmfTone;
    int m_dtmfCount;
    double m_xv[3];
    double m_pwr;
    Tone2PoleFilter m_fax;
    Tone2PoleFilter m_dtmfL[4];
    Tone2PoleFilter m_dtmfH[4];
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message& msg);
};

class RecordHandler : public MessageHandler
{
public:
    RecordHandler() : MessageHandler("chan.record") { }
    virtual bool received(Message& msg);
};

class ToneDetectorModule : public Module
{
public:
    ToneDetectorModule();
    virtual ~ToneDetectorModule();
    virtual void initialize();
    virtual void statusParams(String& str);
private:
    bool m_first;
};

static Mutex s_mutex;
static int s_count = 0;

static ToneDetectorModule plugin;

// generated CNG detector (1100Hz) - either of the 2 below:
// mkfilter -Bp -Re 50 -a 0.137500
//  -> 2-pole resonator bandpass, 1100Hz, Q-factor=50
// mkfilter -Bu -Bp -o 1 -a 1.3612500000e-01 1.3887500000e-01
//  -> 2-pole butterworth bandpass, 1100Hz +-11Hz @ -3dB
static Params2Pole s_paramsCNG =
    { 1.167453752e+02, -0.9828688170, 1.2878183436 }; // 1100Hz

// generated CED detector (2100Hz) filter parameters
// mkfilter -Bu -Bp -o 1 -a 2.6062500000e-01 2.6437500000e-01
//  -> 2-pole butterworth bandpass, 2100Hz +-15Hz @ -3dB
static Params2Pole s_paramsCED =
    { 8.587870006e+01, -0.9767113407, -0.1551017476 }; // 2100Hz

// generated DTMF component filter parameters
// 2-pole butterworth bandpass, +-1% @ -3dB
static Params2Pole s_paramsDtmfL[] = {
    { 1.836705768e+02, -0.9891110494, 1.6984655220 }, // 697Hz
    { 1.663521771e+02, -0.9879774290, 1.6354206881 }, // 770Hz
    { 1.504376844e+02, -0.9867055777, 1.5582944783 }, // 852Hz
    { 1.363034877e+02, -0.9853269818, 1.4673997821 }, // 941Hz
};
static Params2Pole s_paramsDtmfH[] = {
    { 1.063096655e+02, -0.9811871438, 1.1532059506 }, // 1209Hz
    { 9.629842594e+01, -0.9792313229, 0.9860778489 }, // 1336Hz
    { 8.720029263e+01, -0.9770643703, 0.7895131023 }, // 1477Hz
    { 7.896493565e+01, -0.9746723483, 0.5613790789 }, // 1633Hz
};

// DTMF table using low, high indexes
static char s_tableDtmf[][5] = {
    "123A", "456B", "789C", "*0#D"
};

// Update a moving average with square of value (so we end with ~ power)
static void updatePwr(double& avg, double val)
{
    avg = 0.95*avg + 0.05*val*val;
}


void Tone2PoleFilter::update(double xd)
{
    m_y[0] = m_y[1]; m_y[1] = m_y[2];
    m_y[2] = (xd * m_mult) +
	(m_y0 * m_y[0]) +
	(m_y1 * m_y[1]);
    updatePwr(m_val,m_y[2]);
}


ToneConsumer::ToneConsumer(const String& id, const String& name)
    : m_id(id), m_name(name), m_detFax(true), m_detDtmf(true),
      m_fax(s_paramsCNG)
{ 
    Debug(&plugin,DebugAll,"ToneConsumer::ToneConsumer(%s,'%s') [%p]",
	id.c_str(),name.c_str(),this);
    for (int i = 0; i < 4; i++) {
	m_dtmfL[i].assign(s_paramsDtmfL[i]);
	m_dtmfH[i].assign(s_paramsDtmfH[i]);
    }
    init();
    String tmp = name;
    if (tmp.find('/') >= 0)
	tmp >> "/";
    if (tmp && (tmp != "*")) {
	// individual detection requested
	m_detFax = m_detDtmf = false;
	ObjList* k = tmp.split(',',false);
	for (ObjList* l = k; l; l = l->next()) {
	    String* s = static_cast<String*>(l->get());
	    if (!s)
		continue;
	    m_detFax = m_detFax || (*s == "fax");
	    m_detDtmf = m_detDtmf || (*s == "dtmf");
	    if (*s == "rfax") {
		// detection of receiving Fax requested
		m_fax.assign(s_paramsCED);
		m_detFax = true;
	    }
	}
	delete k;
    }
    s_mutex.lock();
    s_count++;
    s_mutex.unlock();
}

ToneConsumer::~ToneConsumer()
{
    Debug(&plugin,DebugAll,"ToneConsumer::~ToneConsumer [%p]",this);
    s_mutex.lock();
    s_count--;
    s_mutex.unlock();
}

// Re-init filter(s)
void ToneConsumer::init()
{
    m_xv[1] = m_xv[2] = 0.0;
    m_pwr = 0.0;
    m_fax.init();
    for (int i = 0; i < 4; i++) {
	m_dtmfL[i].init();
	m_dtmfH[i].init();
    }
    m_dtmfTone = '\0';
    m_dtmfCount = 0;
}

// Check if we detected a DTMF
void ToneConsumer::checkDtmf()
{
    int i;
    char c = m_dtmfTone;
    m_dtmfTone = '\0';
    int l = 0;
    double maxL = m_dtmfL[0].value();
    for (i = 1; i < 4; i++) {
	if (maxL < m_dtmfL[i].value()) {
	    maxL = m_dtmfL[i].value();
	    l = i;
	}
    }
    if (maxL < m_pwr*THRESHOLD2_REL_DTMF)
	return;
    int h = 0;
    double maxH = m_dtmfH[0].value();
    for (i = 1; i < 4; i++) {
	if (maxH < m_dtmfH[i].value()) {
	    maxH = m_dtmfH[i].value();
	    h = i;
	}
    }
    if (maxH < m_pwr*THRESHOLD2_REL_DTMF)
	return;
    if ((maxL+maxH) < m_pwr*THRESHOLD2_REL_ALL)
	return;
    char buf[2];
    buf[0] = s_tableDtmf[l][h];
    buf[1] = '\0';
    if (buf[0] != c) {
	XDebug(&plugin,DebugInfo,"DTMF '%s' new candidate on %s, lo=%f, hi=%f, total=%f",
	    buf,m_id.c_str(),maxL,maxH,m_pwr);
	m_dtmfTone = buf[0];
	m_dtmfCount = 1;
	return;
    }
    m_dtmfTone = c;
    XDebug(&plugin,DebugInfo,"DTMF '%s' candidate %d on %s, lo=%f, hi=%f, total=%f",
	buf,m_dtmfCount,m_id.c_str(),maxL,maxH,m_pwr);
    if (m_dtmfCount++ == DETECT_DTMF_MSEC) {
	DDebug(&plugin,DebugInfo,"DTMF '%s' detected on %s, lo=%f, hi=%f, total=%f",
	    buf,m_id.c_str(),maxL,maxH,m_pwr);
	Message *m = new Message("chan.masquerade");
	m->addParam("id",m_id);
	m->addParam("message","chan.dtmf");
	m->addParam("text",buf);
	Engine::enqueue(m);
    }
}

// Check if we detected a Fax CNG tone
void ToneConsumer::checkFax()
{
    if (m_fax.value() < m_pwr*THRESHOLD2_REL_FAX)
	return;
    DDebug(&plugin,DebugInfo,"Fax detected on %s, signal=%f, total=%f",
	m_id.c_str(),m_fax.value(),m_pwr);
    // prepare for new detection
    init();
    m_detFax = false;
    Message* m = new Message("chan.masquerade");
    m->addParam("id",m_id);
    if (m_divert) {
	Debug(&plugin,DebugCall,"Diverting call %s to: %s",
	    m_id.c_str(),m_divert.c_str());
	m->addParam("message","call.execute");
	m->addParam("callto",m_divert);
	m->addParam("reason","fax");
    }
    else
	m->addParam("message","call.fax");
    Engine::enqueue(m);
}

// Feed samples to the filter(s)
void ToneConsumer::Consume(const DataBlock& data, unsigned long timeDelta)
{
    const int16_t* s = (const int16_t*)data.data();
    if (!s)
	return;
    for (unsigned int i=0; i<data.length(); i+=2) {
	m_xv[0] = m_xv[1]; m_xv[1] = m_xv[2];
	m_xv[2] = *s++;
	double dx = m_xv[2] - m_xv[0];
	updatePwr(m_pwr,m_xv[2]);

	// update all active detectors
	if (m_detFax)
	    m_fax.update(dx);
	if (m_detDtmf) {
	    for (int j = 0; j < 4; j++) {
		m_dtmfL[j].update(dx);
		m_dtmfH[j].update(dx);
	    }
	}
	// only do checks evey millisecond
	if (i % 8)
	    continue;
	// is it enough total power to accept a signal?
	if (m_pwr >= THRESHOLD2_ABS) {
	    if (m_detDtmf)
		checkDtmf();
	    if (m_detFax)
		checkFax();
	}
	else {
	    m_dtmfTone = '\0';
	    m_dtmfCount = 0;
	}
    }
    XDebug(&plugin,DebugInfo,"Fax detector on %s: signal=%f, total=%f",
	m_id.c_str(),m_fax.value(),m_pwr);
}

// Copy parameters required for automatic fax call diversion
void ToneConsumer::setDivert(const Message& msg)
{
    NamedString* divert = msg.getParam("fax_divert");
    if (!divert)
	return;
    m_detFax = true;
    // if divert is empty or false disable diverting
    if (divert->null() || !divert->toBoolean(true))
	m_divert.clear();
    else {
	m_divert = *divert;
	m_caller = msg.getValue("caller",m_caller);
	m_called = msg.getValue("called",m_called);
    }
}


// Attach a tone detector on "chan.attach" as consumer or sniffer
bool AttachHandler::received(Message& msg)
{
    String cons(msg.getValue("consumer"));
    if (!cons.startsWith("tone/"))
	cons.clear();
    String snif(msg.getValue("sniffer"));
    if (!snif.startsWith("tone/"))
	snif.clear();
    if (cons.null() && snif.null())
	return false;
    CallEndpoint* ch = static_cast<CallEndpoint *>(msg.userObject("CallEndpoint"));
    if (ch) {
	if (cons) {
	    ToneConsumer* c = new ToneConsumer(ch->id(),cons);
	    c->setDivert(msg);
	    ch->setConsumer(c);
	    c->deref();
	}
	if (snif) {
	    DataEndpoint* de = ch->setEndpoint();
	    // try to reinit sniffer if one already exists
	    ToneConsumer* c = static_cast<ToneConsumer*>(de->getSniffer(snif));
	    if (c) {
		c->init();
		c->setDivert(msg);
	    }
	    else {
		c = new ToneConsumer(ch->id(),snif);
		c->setDivert(msg);
		de->addSniffer(c);
		c->deref();
	    }
	}
	return msg.getBoolValue("single");
    }
    else
	Debug(&plugin,DebugWarn,"ToneDetector attach request with no call endpoint!");
    return false;
}


// Attach a tone detector on "chan.record" - needs just a CallEndpoint
bool RecordHandler::received(Message& msg)
{
    String src(msg.getValue("call"));
    String id(msg.getValue("id"));
    if (!src.startsWith("tone/"))
	return false;
    DataEndpoint* de = static_cast<DataEndpoint *>(msg.userObject("DataEndpoint"));
    CallEndpoint* ch = static_cast<CallEndpoint *>(msg.userObject("CallEndpoint"));
    if (ch) {
	id = ch->id();
	if (!de)
	    de = ch->setEndpoint();
    }
    if (de) {
	ToneConsumer* c = new ToneConsumer(id,src);
	c->setDivert(msg);
	de->setCallRecord(c);
	c->deref();
	return true;
    }
    else
	Debug(&plugin,DebugWarn,"ToneDetector record request with no call endpoint!");
    return false;
}


ToneDetectorModule::ToneDetectorModule()
    : Module("tonedetect","misc"), m_first(true)
{
    Output("Loaded module ToneDetector");
}

ToneDetectorModule::~ToneDetectorModule()
{
    Output("Unloading module ToneDetector");
}

void ToneDetectorModule::statusParams(String& str)
{
    str.append("count=",",") << s_count;
}

void ToneDetectorModule::initialize()
{
    Output("Initializing module ToneDetector");
    setup();
    if (m_first) {
	m_first = false;
	Engine::install(new AttachHandler);
	Engine::install(new RecordHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
