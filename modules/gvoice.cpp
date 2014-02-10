/**
 * gvoice.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Google Voice(TM) auxiliary module - send DTMF on answer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2012-2014 Null Team
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

using namespace TelEngine;
namespace { // anonymous

class GVChanData : public GenObject
{
public:
    GVChanData(const char* id, const char* tones, const NamedList& params);
    ~GVChanData();
    inline const String& id() const
	{ return m_id; }
    inline void replaceId(const String& value)
	{ m_id = value; }
    virtual const String& toString() const
	{ return id(); }
    // Start the timer
    void start(const String& peerId);
    // Send DTMFs. Return false if no more data to send
    bool sendDtmf(unsigned int time);

protected:
    String m_id;
    String m_peerId;
    String m_tones;
    unsigned int m_delay;
    bool m_outbound;
    unsigned int m_sendTime;
};

class GVModule : public Module
{
public:
    enum Relay {
	ChanHangup = Private,
    };
    GVModule();
    ~GVModule();
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    GVChanData* findChanDtmfData(const String& id);
    bool unload();

private:
    void onTimer(unsigned int time);
    void onExecute(Message& msg);
    void onAnswered(Message& msg);
    void onHangup(Message& msg);

    ObjList m_sendDtmf;                  // List of items to send dtmf
    String m_dtmfText;                   // Default DTMFs to send
    String m_matchParam;                 // Parameter to match
    Regexp m_matchRule;                  // Rule for matched parameter
};


// Module data
INIT_PLUGIN(GVModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}

static unsigned int s_dtmfDelay = 2;     // Default delay
static bool s_dtmfOutbound = false;      // Send to outbound call leg
static unsigned int s_time = 0;


//
// GVChanData
//
GVChanData::GVChanData(const char* id, const char* tones, const NamedList& params)
    : m_id(id), m_tones(tones), m_delay(0), m_outbound(false), m_sendTime(0)
{
    DDebug(&__plugin,DebugAll,"GVChanData '%s' '%s' [%p]",m_id.c_str(),m_tones.c_str(),this);
    m_delay = params.getIntValue(YSTRING("postanm_dtmf_delay"),s_dtmfDelay);
    if (m_delay > 60000)
	m_delay = 60000;
    m_outbound = params.getBoolValue(YSTRING("postanm_dtmf_outbound"),s_dtmfOutbound);
}

GVChanData::~GVChanData()
{
    DDebug(&__plugin,DebugAll,"GVChanData '%s' destroyed [%p]",m_id.c_str(),this);
}

// Start the timer
void GVChanData::start(const String& peerId)
{
    if (m_sendTime)
	return;
    Debug(&__plugin,DebugNote,"GVChanData '%s' starting in %u s [%p]",
	m_id.c_str(),m_delay,this);
    m_peerId = peerId;
    m_sendTime = s_time + m_delay;
}

// Send DTMFs. Return false if no more data to send
bool GVChanData::sendDtmf(unsigned int time)
{
    if (!m_sendTime || m_sendTime > time)
	return true;
    if (m_tones) {
	String tone = m_tones.substr(0,1);
	m_tones = m_tones.substr(1);
	char t = tone.at(0);
	if (('0' <= t && t <= '9') || '*' == t || '#' == t || ('A' <= t && t <= 'D')) {
	    Debug(&__plugin,DebugAll,"GVChanData '%s' sending '%s' [%p]",
		m_id.c_str(),tone.c_str(),this);
	    Message* m = new Message("chan.masquerade");
	    m->addParam("module",__plugin.name());
	    m->addParam("id",m_outbound ? m_id : m_peerId);
	    m->addParam("message","chan.dtmf");
	    m->addParam("text",tone);
	    m->addParam("detected","generated");
	    Engine::enqueue(m);
	}
    }
    if (m_tones.null())
	return false;
    m_sendTime = time + 1;
    return true;
}


//
// GVModule
//
GVModule::GVModule()
    : Module("gvoice","misc")
{
    Output("Loaded module GVoice");
}

GVModule::~GVModule()
{
    Output("Unloading module GVoice");
}

bool GVModule::unload()
{
    Lock mylock(this);
    if (m_sendDtmf.count())
	return false;
    uninstallRelays();
    return true;
}

bool GVModule::received(Message& msg, int id)
{
    switch (id) {
	case Timer:
	    onTimer(msg.getIntValue(YSTRING("time")));
	    break;
	case Execute:
	    onExecute(msg);
	    return false;
	case Answered:
	    onAnswered(msg);
	    return false;
	case ChanHangup:
	    onHangup(msg);
	    return false;
    }
    return Module::received(msg,id);
}

// Send DTMFs for all active calls
void GVModule::onTimer(unsigned int time)
{
     lock();
     s_time = time + 1;
     ListIterator iter(m_sendDtmf);
     while (GVChanData* c = static_cast<GVChanData*>(iter.get())) {
        if (!c->sendDtmf(time))
    	    m_sendDtmf.remove(c);
     }
     unlock();
}

void GVModule::onExecute(Message& msg)
{
    const String& id = msg[YSTRING("id")];
    if (id.null())
	return;

    bool match = true;
    const String& enable = msg[YSTRING("postanm_dtmf")];
    if (enable.toBoolean(false))
	match = false;
    else if (!enable.toBoolean(true))
	return;

    Lock mylock(this);
    if (match && !m_matchRule.matches(msg[m_matchParam]))
	return;
    String text = msg.getValue(YSTRING("postanm_dtmf_text"),m_dtmfText);
    if (text.null()) {
	Debug(this,DebugNote,"Missing DTMFs for chan=%s",id.c_str());
	return;
    }
    m_sendDtmf.append(new GVChanData(id,text,msg));
}

void GVModule::onAnswered(Message& msg)
{
    lock();
    GVChanData* c = static_cast<GVChanData*>(m_sendDtmf[msg[YSTRING("peerid")]]);
    if (c)
	c->start(msg[YSTRING("id")]);
    unlock();
}

void GVModule::onHangup(Message& msg)
{
    const String& id = msg[YSTRING("id")];
    if (id.null())
	return;
    Lock mylock(this);
    m_sendDtmf.remove(id);
}

void GVModule::initialize()
{
     static bool s_init = true;
     Output("Initializing module GVoice");
     Configuration cfg(Engine::configFile("gvoice"));
     if (s_init) {
	setup();
	installRelay(Execute,cfg.getIntValue("general","call.execute",20));
	installRelay(Answered,cfg.getIntValue("general","call.answered",50));
	installRelay(ChanHangup,"chan.hangup",cfg.getIntValue("general","chan.hangup",50));
	s_init = false;
    }
    s_dtmfDelay = cfg.getIntValue("general","dtmf_delay",2);
    s_dtmfOutbound = cfg.getBoolValue("general","dtmf_outbound",false);
    lock();
    m_dtmfText = cfg.getValue("general","dtmf_text","1");
    m_matchParam = cfg.getValue("general","match_param","calleruri");
    m_matchRule = cfg.getValue("general","match_rule","^jingle:.*@voice.google.com/");
    m_matchRule.compile();
    unlock();
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
