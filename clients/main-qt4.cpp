/**
 * main-qt4.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt-4 based universal telephony client
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

#include <yatephone.h>
#include "qt4/qt4client.h"

#define WAIT_ENGINE 10000       //wait 10 seconds for engine to halt

using namespace TelEngine;

class EngineThread;

static QtDriver qtdriver(false);
static EngineThread* s_engineThread = 0;

class EngineThread : public Thread
{
public:
    inline EngineThread()
      : Thread("Engine")
      { }
    virtual void run();
    virtual void cleanup();
};

void EngineThread::run()
{
    Engine::self()->run();
    Debug(DebugAll,"Engine stopped running");
}

void EngineThread::cleanup()
{
    Debug(DebugAll,"EngineThread::cleanup() [%p]",this);
    if (QtClient::self())
	QtClient::self()->quit();
    s_engineThread = 0;
}

static int mainLoop()
{
    // create engine from this thread
    Engine::self();
    s_engineThread = new EngineThread;
    if (!s_engineThread->startup())
	return EINVAL;

    // build client if the driver didn't
    if (!QtClient::self())
	QtClient::setSelf(new QtClient());

    // run the client
    if (!Engine::exiting())
	QtClient::self()->run();
    // the client finished running, do cleanup
    QtClient::self()->cleanup();

    Engine::halt(0);
    unsigned long count = WAIT_ENGINE / Thread::idleMsec();
    while (s_engineThread && count--)
	Thread::idle();

    return 0;
}

extern "C" int main(int argc, const char** argv, const char** envp)
{
    TelEngine::Engine::extraPath("qt4");
    return TelEngine::Engine::main(argc,argv,envp,TelEngine::Engine::Client,&mainLoop);
}
/* vi: set ts=8 sw=4 sts=4 noet: */
