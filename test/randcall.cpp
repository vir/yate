/*
    test.c
    This file holds the entry point of the Telephony Engine
*/

#include <telengine.h>

#include <unistd.h>
#include <stdlib.h>

using namespace TelEngine;

class RandThread : public Thread
{
public:
    RandThread() : Thread("RandThread") { }
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
    for (;;) {
	::usleep(::random() % 5000000);
	String id("random/"+String((int)::random() %1000));
	Message *m = new Message("preroute");
	m->addParam("id",id);
	m->addParam("caller",String((int)(::random() % 1000000)));
	m->addParam("called",String((int)(::random() % 1000000)));
	Engine::dispatch(m);
	*m = "route";
	bool routed = Engine::dispatch(m);
	Debug(DebugMild,"Routed %ssuccessfully in %llu usec",(routed ? "" : "un"),
	    Time::now()-m->msgTime().usec());
	if (routed) {
	    m->addParam("callto",m->retValue());
	    m->retValue() = "";
	    *m = "call";
	    m->msgTime() = Time::now();
	    if (Engine::dispatch(m)) {
		::usleep(::random() % 5000000);
		if ((::random() % 100) < 33) {
		    *m = "answered";
		    m->msgTime() = Time::now();
		    m->addParam("status","answered");
		    Engine::dispatch(m);
		    ::usleep(::random() % 10000000);
		}
		else if ((::random() % 100) < 50)
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
    }
}

RandPlugin::RandPlugin()
    : m_thread(0)
{
    Output("Loaded random call generator");
}

void RandPlugin::initialize()
{
    Output("Initializing module RandPlugin");
    if (!m_thread)
	m_thread = new RandThread;
}

INIT_PLUGIN(RandPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
