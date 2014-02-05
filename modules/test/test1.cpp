/**
 * test1.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <unistd.h>

using namespace TelEngine;

static bool noisy = false;

class TestThread : public Thread
{
public:
    virtual void run();
    virtual void cleanup();
};

class TestPlugin1 : public Plugin
{
public:
    TestPlugin1();
    ~TestPlugin1();
    virtual void initialize();
private:
    bool m_first;
};

class TestHandler : public MessageHandler
{
public:
    TestHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

void TestThread::run()
{
    Debug(DebugInfo,"TestThread::run() [%p]",this);
    for (;;) {
	Engine::dispatch(Message("test.thread.direct"));
	Engine::enqueue(new Message("test.thread.queued"));
	::sleep(2);
    }
}

void TestThread::cleanup()
{
    Debug(DebugInfo,"TestThread::cleanup() [%p]",this);
    Debug(DebugInfo,"Thread::current() = %p",Thread::current());
}

bool TestHandler::received(Message &msg)
{
    if (noisy)
	Output("Received message '%s' time=" FMT64U " thread=%p",
	    msg.c_str(),msg.msgTime().usec(),Thread::current());
    return false;
};

TestPlugin1::TestPlugin1()
    : m_first(true)
{
    Output("Hello, I am module TestPlugin1");
}

TestPlugin1::~TestPlugin1()
{
    Message msg("test1.exit","ok");
    msg.addParam("foo","bar").addParam("x","y");
    Engine::dispatch(&msg);
}

void TestPlugin1::initialize()
{
    Output("Initializing module TestPlugin1");
    Configuration *cfg = new Configuration(Engine::configFile("test1"));
    noisy = cfg->getBoolValue("general","noisy");
    int n = cfg->getIntValue("general","threads");
    delete cfg;
    Engine::install(new TestHandler("engine.halt"));
    Engine::install(new TestHandler(""));
    Engine::enqueue(new Message("test.queued1"));
    Engine::enqueue(new Message("test.queued2"));
    if (m_first) {
	m_first = false;
	for (int i=0; i<n; i++) {
	    ::usleep(10000);
	    TestThread *t = new TestThread;
	    t->startup();
	}
    }
}

INIT_PLUGIN(TestPlugin1);

/* vi: set ts=8 sw=4 sts=4 noet: */
