/**
 * main-client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatengine.h"

#include "resource.h"
#include <commctrl.h>

using namespace TelEngine;

class WinClientThread : public Thread
{
public:
	void run();
};

class WinClientPlugin : public Plugin
{
public:
	WinClientPlugin()
		: m_thread(0)
		{ }
	virtual void initialize(void);
	virtual bool isBusy() const
		{ return true; }
private:
	WinClientThread* m_thread;
};


int CALLBACK mainDialog(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
		case WM_CLOSE:
			EndDialog(wnd,1);
			break;
		default:
			return 0;
	}
	return 1;
}

void WinClientThread::run()
{
	InitCommonControls();
	int ret = DialogBox(0,MAKEINTRESOURCE(IDD_TCLIENT),0,mainDialog);
	if (ret < 0)
		ret = 127;
	Engine::halt(ret);
}

void WinClientPlugin::initialize()
{
	if (!m_thread) {
		m_thread = new WinClientThread;
		if (m_thread->error())
			Engine::halt(1);
		else
			m_thread->startup();
	}
}

INIT_PLUGIN(WinClientPlugin);

// We force mainCRTStartup as entry point (from linker settings) so we get
//  the parser called even for a GUI application
extern "C" int main(int argc, const char** argv, const char** envp)
{
    return Engine::main(argc,argv,envp,TelEngine::Engine::Client);
}
