/**
 * yate-qt.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt based universal telephony client
 * Author: Dorin Lazar <lazar@deuromedia.ro>
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

#include <stdlib.h>
#include <stdio.h>

#include "../contrib/qt/qtclientform.hpp"
#include <qapplication.h>

using namespace TelEngine;

static String s_device;

class QtClientHandler : public MessageHandler
{
	QtClientForm *m_frm;
public:
	QtClientHandler(int prio, QtClientForm *form)
		: MessageHandler("call.route",prio),m_frm(form) { }
	virtual bool received(Message &msg);
};
	
bool QtClientHandler::received(Message &msg)
{
	String caller(msg.getValue("caller"));
	Debug(DebugInfo,"caller %s",caller.c_str());
	if (caller == s_device)
		return false;
	String called(msg.getValue("called"));
	if (called.null())
		return false;
	String mesg;
	if (!caller)
		caller = msg.getValue("callername");
	mesg << "You have a call from " << caller << " for " << called ;
	Debug(DebugAll,"%s",mesg.c_str());
	m_frm->setDialer (caller.safe());
	if (!m_frm->setStatus(YCS_RINGIN)) {
		Debug (DebugAll, "Unable to proceed with call: busy");
		return false;
	}

	u_int64_t t = Time::now() + 10000000;
	while (Time::now() < t) { 
		if(m_frm->getStatus() == YCS_INCALL) {
			msg.retValue() = s_device;	    
			Debug (DebugAll, "Call accepted<<<<<<<< ");
			return true;
		} else if (m_frm->getStatus() == YCS_IDLE) {
			Debug (DebugAll, "Call rejected<<<<<<<< ");
			return false;
		}
		usleep(50000);
	}
	m_frm->setStatus(YCS_IDLE);
	Debug (DebugAll, "Call rejected (timeout)<<<<<<<< ");
	return false;
};

class QtClientThread : public Thread
{
 public:
	QtClientThread () : Thread("QtClient"),m_app(0),m_frm(0), m_msgHandler(0) {}
	~QtClientThread() {}
 public:
	void run(void);
	void cleanup(void);
 private:
	QApplication *m_app;
	QtClientForm *m_frm;
	QtClientHandler *m_msgHandler;
};

void QtClientThread::run (void)
{
	int argc = 1;
	char *argv[] = {"QYate", NULL};
	m_app = new QApplication(argc, argv);
	m_frm = new QtClientForm(s_device.safe());
	m_app->setMainWidget (m_frm);
	m_frm->show();
	m_msgHandler = new QtClientHandler(1, m_frm);
	Engine::install (m_msgHandler);
	m_app->exec();
	Engine::halt(0);
	Output ("QtClientThread finished");
}

void QtClientThread::cleanup (void)
{
	delete m_frm;
	m_frm = NULL;
	delete m_app;
	m_app = NULL;
	qApp = NULL;
}

class QtYateClientPlugin : public Plugin
{
	QtClientThread *thread;
 public:
	QtYateClientPlugin();
	~QtYateClientPlugin();
	virtual void initialize(void);
	virtual bool isBusy() const { return true; }
};

QtYateClientPlugin::QtYateClientPlugin ()
    : thread(0)
{
}

QtYateClientPlugin::~QtYateClientPlugin ()
{
//	the thread should be already dead at this point
}

void QtYateClientPlugin::initialize (void)
{
    if (!thread && ::getenv("DISPLAY")) {
	Output ("Initializing Qt Client");
	s_device = Engine::config().getValue("client","device","oss//dev/dsp");
	thread = new QtClientThread;
	thread->startup();
    }
}

INIT_PLUGIN(QtYateClientPlugin);

extern "C" int main(int argc, const char** argv, const char** environ)
{
    bool fail = !::getenv("DISPLAY");
    if (fail)
        fputs("Warning: DISPLAY variable is not set\n",stderr);
    return TelEngine::Engine::main(argc,argv,environ,TelEngine::Engine::Client,fail);
}
