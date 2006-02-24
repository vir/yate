/**
 * analyzer.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Test call generator and audio quality analyzer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatephone.h>

#include <string.h>
#include <stdio.h>

using namespace TelEngine;

class AnalyzerCons : public DataConsumer
{
    YCLASS(AnalyzerCons,DataConsumer)
public:
    AnalyzerCons(const String& type);
    virtual ~AnalyzerCons();
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    virtual void statusParams(String& str);
protected:
    u_int64_t m_timeStart;
    unsigned long m_tsStart;
    unsigned int m_tsGapCount;
    unsigned long m_tsGapLength;
    bool m_spectrum;
};

class AnalyzerChan : public Channel
{
public:
    AnalyzerChan(const String& type, bool outgoing);
    virtual ~AnalyzerChan();
    virtual void statusParams(String& str);
    virtual bool callRouted(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    void startChannel(NamedList& params);
    void addSource();
    void addConsumer();
protected:
    void localParams(String& str);
    u_int64_t m_timeStart;
    unsigned long m_timeRoute;
    unsigned long m_timeRing;
    unsigned long m_timeAnswer;
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message& msg);
};

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

static int s_res = 1;

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

AnalyzerCons::AnalyzerCons(const String& type)
    : m_timeStart(0), m_tsStart(0), m_tsGapCount(0), m_tsGapLength(0),
      m_spectrum(false)
{
    if (type == "spectrum")
	m_spectrum = true;
}

AnalyzerCons::~AnalyzerCons()
{
}

void AnalyzerCons::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (!m_timeStart) {
	// the first data block may be garbled or have bad timestamp - ignore
	m_timeStart = Time::now();
	m_tsStart = tStamp;
	return;
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
}

void AnalyzerCons::statusParams(String& str)
{
    unsigned long samples = timeStamp() - m_tsStart;
    str.append("gaps=",",") << m_tsGapCount;
    str << ",gaplen=" << (unsigned int)m_tsGapLength;
    str << ",samples=" << (unsigned int)samples;
    if (m_timeStart) {
	u_int64_t utime = Time::now() - m_timeStart;
	utime = utime ? ((1000000 * (u_int64_t)samples) + (utime / 2)) / utime : 0;
	str << ",rate=" << (unsigned int)utime;
    }
}


AnalyzerChan::AnalyzerChan(const String& type, bool outgoing)
    : Channel(__plugin,0,outgoing),
      m_timeStart(Time::now()), m_timeRoute(0), m_timeRing(0), m_timeAnswer(0)
{
    Debug(this,DebugAll,"AnalyzerChan::AnalyzerChan('%s',%s) [%p]",
	type.c_str(),String::boolText(outgoing),this);
    m_address = type;
}

AnalyzerChan::~AnalyzerChan()
{
    Debug(this,DebugAll,"AnalyzerChan::~AnalyzerChan() %s [%p]",id().c_str(),this);
    RefPointer<AnalyzerCons> cons = YOBJECT(AnalyzerCons,getConsumer());
    char buf[32];
    printTime(buf,(unsigned int)(Time::now() - m_timeStart));
    String str(status());
    localParams(str);
    str.append("totaltime=",",") << buf;
    if (cons)
	cons->statusParams(str);
    Output("Analyzer %s finished: %s",id().c_str(),str.c_str());
    Engine::enqueue(message("chan.hangup"));
}

void AnalyzerChan::statusParams(String& str)
{
    Channel::statusParams(str);
    localParams(str);
    RefPointer<AnalyzerCons> cons = YOBJECT(AnalyzerCons,getConsumer());
    if (cons)
	cons->statusParams(str);
}

void AnalyzerChan::localParams(String& str)
{
    char buf[32];
    if (m_timeRoute) {
	printTime(buf,m_timeRoute);
	str.append("routetime=",",") << buf;
    }
    if (m_timeRing) {
	printTime(buf,m_timeRing);
	str.append("ringtime=",",") << buf;
    }
    if (m_timeAnswer) {
	printTime(buf,m_timeAnswer);
	str.append("answertime=",",") << buf;
    }
}

bool AnalyzerChan::callRouted(Message& msg)
{
    if (!m_timeRoute)
	m_timeRoute = Time::now() - m_timeStart;
    return Channel::callRouted(msg);
}

bool AnalyzerChan::msgRinging(Message& msg)
{
    if (!m_timeRing)
	m_timeRing = Time::now() - m_timeStart;
    return Channel::msgRinging(msg);
}

bool AnalyzerChan::msgAnswered(Message& msg)
{
    if (!m_timeAnswer)
	m_timeAnswer = Time::now() - m_timeStart;
    addConsumer();
    addSource();
    return Channel::msgAnswered(msg);
}

void AnalyzerChan::startChannel(NamedList& params)
{
    Message* m = message("chan.startup");
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
}

void AnalyzerChan::addSource()
{
    if (getSource())
	return;
    const char* src = "tone/dial";
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
    AnalyzerCons* cons = new AnalyzerCons(m_address);
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
    AnalyzerChan *ac = new AnalyzerChan(dest,false);
    ac->startChannel(params);
    Message* m = ac->message("call.route",false,true);
    m->addParam("called",tmp);
    if (direct)
	m->addParam("callto",tmp);
    tmp = params.getValue("caller");
    if (tmp.null())
	tmp << prefix() << dest;
    m->addParam("caller",tmp);
    return ac->startRouter(m);
}

bool AnalyzerDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
    if (ch) {
	AnalyzerChan *ac = new AnalyzerChan(dest,true);
	if (ch->connect(ac,msg.getValue("reason"))) {
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

    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));

    if (!ch) {
	Debug(DebugWarn,"Analyzer attach request with no control channel!");
	return false;
    }

    // if single attach was requested we can return true if everything is ok
    bool ret = msg.getBoolValue("single");

    AnalyzerCons *ac = new AnalyzerCons(cons);
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

/* vi: set ts=8 sw=4 sts=4 noet: */
