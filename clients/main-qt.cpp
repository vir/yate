/**
 * qtclient.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt based universal telephony client
 * Author: Dorin Lazar <lazar@deuromedia.ro>
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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

#include <telengine.h>
#include <telephony.h>

#include <stdlib.h>

#include "../contrib/qt/qtclientform.hpp"
#include <qapplication.h>

using namespace TelEngine;

class QtClientThread : public Thread
{
 public:
	QtClientThread () : Thread("QtClient"),m_app(0),m_frm(0) {}
	~QtClientThread() {}
 public:
	void run(void);
	void cleanup(void);
 private:
	QApplication *m_app;
	QtClientForm *m_frm;
};

void QtClientThread::run (void)
{
	int argc = 1;
	char *argv[] = {"QYate", NULL};
	m_app = new QApplication(argc, argv);
	m_frm = new QtClientForm();
	m_app->setMainWidget (m_frm);
	m_frm->show();
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
	Output ("Loading QtYateClientPlugin");
}

QtYateClientPlugin::~QtYateClientPlugin ()
{
//	the thread should be already dead at this point
	Output ("Unloaded QtYateClientPlugin");
}

void QtYateClientPlugin::initialize (void)
{
    if (!thread && ::getenv("DISPLAY")) {
	Output ("Initializing Qt Client");
	thread = new QtClientThread;
	thread->startup();
    }
}

INIT_PLUGIN(QtYateClientPlugin);
