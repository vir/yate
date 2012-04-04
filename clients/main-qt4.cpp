/**
 * main-qt4.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt-4 based universal telephony client
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
    Engine::engineRun();
    Debug(DebugAll,"Engine stopped running");
}

void EngineThread::cleanup()
{
    Debug(DebugAll,"EngineThread::cleanup() [%p]",this);
    s_engineThread = 0;
}


extern "C" int main(int argc, const char** argv, const char** envp)
{
    TelEngine::Engine::extraPath("qt4");
    // parse arguments
    int retcode = TelEngine::Engine::main(argc,argv,envp,TelEngine::Engine::ClientMainThread);
    if (retcode)
	return retcode;

    // create engine from this thread
    Engine::self();
    s_engineThread = new EngineThread;
    if (!s_engineThread->startup())
	return EINVAL;

    // build client if the driver didn't
    if (!QtClient::self())
	new QtClient();
    // run the client
    QtClient::self()->run();
    // the client finished running, do cleanup
    QtClient::self()->cleanup();

    // wait for the engine to halt
    Engine::halt(0);
    unsigned long count = WAIT_ENGINE / Thread::idleMsec();
    while (s_engineThread && count--)
	Thread::idle();
    Thread::killall();

    return retcode;
}
/* vi: set ts=8 sw=4 sts=4 noet: */
