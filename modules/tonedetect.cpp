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

#define NZEROS 2
#define NPOLES 2
#define GAIN   1.167519293e+02

#define FAX_THRESHOLD_ABS 2000.0
#define FAX_THRESHOLD_REL 0.8

class ToneConsumer : public DataConsumer
{
public:
    ToneConsumer(const String& id, const String& name);
    virtual ~ToneConsumer(); 
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    virtual const String& toString() const
	{ return m_name; }
    inline const String& id() const
	{ return m_id; }
    void init();
private:
    static void update(double& avg, double val);
    String m_id;
    String m_name;
    bool m_found;
    double m_xv[NZEROS+1], m_yv[NPOLES+1];
    double m_pwr, m_sig;
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
private:
    bool m_first;
};

static ToneDetectorModule plugin;


ToneConsumer::ToneConsumer(const String& id, const String& name)
    : m_id(id), m_name(name), m_found(false)
{ 
    Debug(&plugin,DebugAll,"ToneConsumer::ToneConsumer(%s,'%s') [%p]",
	id.c_str(),name.c_str(),this);
    init();
}

ToneConsumer::~ToneConsumer()
{
    Debug(&plugin,DebugAll,"ToneConsumer::~ToneConsumer [%p]",this);
}

void ToneConsumer::init()
{
    m_xv[0] = m_xv[1] = 0.0;
    m_yv[0] = m_yv[1] = 0.0;
    m_pwr = m_sig = 0.0;
}

void ToneConsumer::update(double& avg, double val)
{
    avg = 0.9*avg + 0.1*val*val;
}

void ToneConsumer::Consume(const DataBlock& data, unsigned long timeDelta)
{
    if (m_found || data.null())
	return;
    const int16_t* s= (const int16_t*)data.data();
    for (unsigned int i=0; i<data.length(); i+=2) {
	// mkfilter generated CNG detector (1100Hz)
	m_xv[0] = m_xv[1]; m_xv[1] = m_xv[2];
	m_yv[0] = m_yv[1]; m_yv[1] = m_yv[2];
	m_xv[2] = *s++ / GAIN;
        m_yv[2] = (m_xv[2] - m_xv[0]) +
	    (-0.9828696621 * m_yv[0]) +
	    ( 1.2877708321 * m_yv[1]);
	update(m_pwr,m_xv[2]);
	update(m_sig,m_yv[2]);

	if ((m_sig > FAX_THRESHOLD_ABS) && (m_sig > m_pwr*FAX_THRESHOLD_REL)) {
	    DDebug(&plugin,DebugInfo,"Fax detected on %s, signal=%f, total=%f",
		m_id.c_str(),m_sig,m_pwr);
	    // prepare for new detection
	    init();
	    m_found = true;
	    Message* m = new Message("chan.masquerade");
	    m->addParam("message","call.fax");
	    m->addParam("id",m_id);
	    Engine::enqueue(m);
	    return;
	}
    }
    XDebug(&plugin,DebugInfo,"Fax detector on %s: signal=%f, total=%f",
	m_id.c_str(),m_sig,m_pwr);
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
	    ch->setConsumer(c);
	    c->deref();
	}
	if (snif) {
	    DataEndpoint* de = ch->setEndpoint();
	    // try to reinit sniffer if one already exists
	    ToneConsumer* c = static_cast<ToneConsumer*>(de->getSniffer(snif));
	    if (c)
		c->init();
	    else {
		c = new ToneConsumer(ch->id(),snif);
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
