/**
 * randcall.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A sample random call generator
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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

#include <yatengine.h>

#include <stdlib.h>

using namespace TelEngine;

class RandThread : public Thread
{
public:
    RandThread() : Thread("RandThread") { }
    virtual void run();
};

class RouteThread : public Thread
{
public:
    RouteThread() : Thread("RandRouteThread") { }
    virtual void run();
};

class RandPlugin : public Plugin
{
public:
    RandPlugin();
    virtual void initialize();
    RandThread *m_thread;
};

class TestHandler : public MessageHandler
{
public:
    TestHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

void RandThread::run()
{
    for (int i = 0; i < 10; i++) {
	Thread::usleep(Random::random() % 10000);
	RouteThread *thread = new RouteThread;
	if (thread->error()) {
	    Debug(DebugFail,"New thread error!");
	    break;
	}
	thread->startup();
    }
    Debug(DebugInfo,"No longer creating new calls");
}

void RouteThread::run()
{
    Thread::usleep(Random::random() % 1000000);
    Message *m = new Message("call");
    m->addParam("callto","wave/play//dev/zero");
    m->addParam("target",String((int)(Random::random() % 1000000)));
    if (!Engine::dispatch(m))
	Debug(DebugMild,"Noone processed call from '%s' to '%s'",
	    m->getValue("callto"),m->getValue("target"));
    delete m;
#if 0
    String id("random/"+String((int)Random::random() %1000));
    Message *m = new Message("preroute");
	m->addParam("id",id);
	m->addParam("caller",String((int)(Random::random() % 1000000)));
	m->addParam("called",String((int)(Random::random() % 1000000)));
	Engine::dispatch(m);
	*m = "route";
	bool routed = Engine::dispatch(m);
	Debug(DebugMild,"Routed %ssuccessfully in " FMT64 " usec",(routed ? "" : "un"),
	    Time::now()-m->msgTime().usec());
	if (routed) {
	    Thread::usleep(Random::random() % 1000000);
	    m->addParam("callto",m->retValue());
	    m->retValue() = "";
	    *m = "call";
	    m->msgTime() = Time::now();
	    if (Engine::dispatch(m)) {
		Thread::usleep(Random::random() % 5000000);
		if ((Random::random() % 100) < 33) {
		    *m = "answered";
		    m->msgTime() = Time::now();
		    m->addParam("status","answered");
		    Engine::dispatch(m);
		    Thread::usleep(Random::random() % 10000000);
		}
		else if ((Random::random() % 100) < 50)
		    *m = "busy";
		else
		    *m = "no answer";
	    }
	    else {
		Debug(DebugMild,"Noone processed call to '%s'",m->getValue("callto"));
		m->addParam("status","rejected");
	    }
	    *m = "hangup";
	    m->msgTime() = Time::now();
	    Engine::dispatch(m);
	}
	delete m;
#endif
}

RandPlugin::RandPlugin()
    : Plugin("randplugin","misc"),
      m_thread(0)
{
    Output("Loaded random call generator");
}

void RandPlugin::initialize()
{
    Output("Initializing module RandPlugin");
    if (!m_thread) {
	m_thread = new RandThread;
	m_thread->startup();
    }
}

INIT_PLUGIN(RandPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
