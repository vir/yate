/*
 * msgsniff.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A sample message sniffer that inserts a wildcard message handler
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

#include <yatengine.h>

using namespace TelEngine;

class MsgSniff : public Plugin
{
public:
    MsgSniff();
    virtual void initialize();
private:
    bool m_first;
};

class SniffHandler : public MessageHandler
{
public:
    SniffHandler() : MessageHandler(0,0) { }
    virtual bool received(Message &msg);
};

class HookHandler : public MessagePostHook
{
public:
    virtual void dispatched(const Message& msg, bool handled);
};

static bool s_active = true;

static void dumpParams(const Message &msg, String& par)
{
    unsigned n = msg.length();
    for (unsigned i = 0; i < n; i++) {
	const NamedString *s = msg.getParam(i);
	if (s) {
	    par << "\n  param['" << s->name() << "'] = ";
	    if (s->name() == "password")
		par << "(hidden)";
	    else
		par << "'" << *s << "'";
	}
    }
}

bool SniffHandler::received(Message &msg)
{
    if (msg == "engine.timer")
	return false;
    if (msg == "engine.command") {
	String line(msg.getValue("line"));
	if (line.startSkip("sniffer")) {
	    line >> s_active;
	    msg.retValue() << "Message sniffer is " << (s_active ? "on" : "off") << "\n";
	    return true;
	}
    }
    if (!s_active)
	return false;
    String par;
    dumpParams(msg,par);
    Output("Sniffed '%s' time=%u.%06u\n  thread=%p '%s'\n  data=%p\n  retval='%s'%s",
	msg.c_str(),
	(unsigned int)(msg.msgTime().usec() / 1000000),
	(unsigned int)(msg.msgTime().usec() % 1000000),
	Thread::current(),
	Thread::currentName(),
	msg.userData(),
	msg.retValue().c_str(),
	par.safe());
    return false;
};


void HookHandler::dispatched(const Message& msg, bool handled)
{
    if ((!s_active) || (msg == "engine.timer"))
	return;
    u_int64_t dt = Time::now() - msg.msgTime().usec();
    String par;
    dumpParams(msg,par);
    Output("Returned %s '%s' delay=%u.%06u\n  thread=%p '%s'\n  data=%p\n  retval='%s'%s",
	String::boolText(handled),
	msg.c_str(),
	(unsigned int)(dt / 1000000),
	(unsigned int)(dt % 1000000),
	Thread::current(),
	Thread::currentName(),
	msg.userData(),
	msg.retValue().c_str(),
	par.safe());
}


MsgSniff::MsgSniff()
    : m_first(true)
{
    Output("Loaded module MsgSniffer");
}

void MsgSniff::initialize()
{
    Output("Initializing module MsgSniffer");
    if (m_first) {
	m_first = false;
	s_active = Engine::config().getBoolValue("general","msgsniff",false);
	Engine::install(new SniffHandler);
	Engine::self()->setHook(new HookHandler);
    }
}

INIT_PLUGIN(MsgSniff);

/* vi: set ts=8 sw=4 sts=4 noet: */
