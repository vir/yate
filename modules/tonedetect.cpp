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

#define FAX_THRESHOLD 2000.0

class ToneConsumer : public DataConsumer
{
public:
    ToneConsumer(const String &id);
    virtual ~ToneConsumer(); 
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    inline const String& id() const
	{ return m_id; }
private:
    void init();
    String m_id;
    bool m_found;
    float m_xv[NZEROS+1], m_yv[NPOLES+1], m_avg;
};

class DetectHandler : public MessageHandler
{
public:
    DetectHandler() : MessageHandler("chan.detectdtmf") { }
    virtual bool received(Message &msg);
};

class RecordHandler : public MessageHandler
{
public:
    RecordHandler() : MessageHandler("chan.record") { }
    virtual bool received(Message &msg);
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


ToneConsumer::ToneConsumer(const String &id)
    : m_id(id), m_found(false)
{ 
    Debug(&plugin,DebugAll,"ToneConsumer::ToneConsumer(%s) [%p]",id.c_str(),this);
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
    m_avg = 0.0;
}

void ToneConsumer::Consume(const DataBlock& data, unsigned long timeDelta)
{
    if (m_found || data.null())
	return;
    const int16_t* s= (const int16_t*)data.data();
    for (unsigned int i=0; i<data.length(); i+=2) {
	// mkfilter generated CNG detector (1100Hz)
	m_xv[0] = m_xv[1]; m_xv[1] = m_xv[2];
	m_xv[2] = *s++ / GAIN;
	m_yv[0] = m_yv[1]; m_yv[1] = m_yv[2];
        m_yv[2] = (m_xv[2] - m_xv[0]) +
	    (-0.9828696621 * m_yv[0]) +
	    ( 1.2877708321 * m_yv[1]);
	m_avg = 0.9*m_avg + 0.1*fabs(m_yv[2]);

	if (m_avg > FAX_THRESHOLD) {
	    DDebug(DebugInfo,"Fax detected on %s, average=%f",m_id.c_str(),m_avg);
	    // prepare for new detection
	    init();
	    m_found = true;
	    Message* m = new Message("chan.masquerade");
	    m->addParam("message","call.fax");
	    m->addParam("id",m_id);
	    Engine::enqueue(m);
	    break;
	}
    }
}


// Attach a tone detector on "chan.detectdtmf" - needs a DataSource
bool DetectHandler::received(Message &msg)
{
    String src(msg.getValue("consumer"));
    if (src.null())
	return false;
    Regexp r("^tone/$");
    if (!src.matches(r))
	return false;
    CallEndpoint* ch = static_cast<CallEndpoint *>(msg.userObject("CallEndpoint"));
    if (ch) {
	DataSource* s = ch->getSource();
	if (s) {
	    ToneConsumer* c = new ToneConsumer(ch->id());
	    DataTranslator::attachChain(s,c);
	    c->deref();
	    return true;
	}
    }
    else
	Debug(DebugWarn,"ToneDetector attach request with no data source!");
    return false;
}


// Attach a tone detector on "chan.record" - needs just a CallEndpoint
bool RecordHandler::received(Message &msg)
{
    String src(msg.getValue("call"));
    String id(msg.getValue("id"));
    if (src.null())
	return false;
    Regexp r("^tone/$");
    if (!src.matches(r))
	return false;
    DataEndpoint* de = static_cast<DataEndpoint *>(msg.userObject("DataEndpoint"));
    CallEndpoint* ch = static_cast<CallEndpoint *>(msg.userObject("CallEndpoint"));
    if (ch) {
	id = ch->id();
	if (!de)
	    de = ch->setEndpoint();
    }
    if (de) {
	ToneConsumer* c = new ToneConsumer(id);
	de->setCallRecord(c);
	c->deref();
	return true;
    }
    else
	Debug(DebugWarn,"ToneDetector record request with no call endpoint!");
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
	Engine::install(new DetectHandler);
	Engine::install(new RecordHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
