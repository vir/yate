/*
 * msgsniff.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A sample message sniffer that inserts a wildcard message handler
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

#include <yatengine.h>

using namespace TelEngine;
namespace { // anonymous

static const char* s_debugs[] =
{
    "on",
    "off",
    "enable",
    "disable",
    "true",
    "false",
    "yes",
    "no",
    "filter",
    0
};


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
static Regexp s_filter;
static Mutex s_mutex(false,"FilterSniff");

static void dumpParams(const Message &msg, String& par)
{
    unsigned n = msg.length();
    for (unsigned i = 0; i < n; i++) {
	const NamedString *s = msg.getParam(i);
	if (s) {
	    par << "\r\n  param['" << s->name() << "'] = ";
	    if (s->name() == "password")
		par << "(hidden)";
	    else
		par << "'" << *s << "'";
	}
    }
}

bool SniffHandler::received(Message &msg)
{
    if (msg == YSTRING("engine.timer"))
	return false;
    if (msg == YSTRING("engine.command")) {
	static const String name("sniffer");
	String line(msg.getValue(YSTRING("line")));
	if (line.startSkip(name)) {
	    line >> s_active;
	    line.trimSpaces();
	    if (line.startSkip("filter")) {
		s_mutex.lock();
		s_filter = line;
		s_mutex.unlock();
	    }
	    msg.retValue() << "Message sniffer is " << (s_active ? "on" : "off");
	    if (s_active && s_filter)
		msg.retValue() << " filter " << s_filter;
	    msg.retValue() << "\r\n";
	    return true;
	}
	line = msg.getParam(YSTRING("partline"));
	if (line.null()) {
	    if (name.startsWith(msg.getValue(YSTRING("partword"))))
		msg.retValue().append(name,"\t");
	}
	else if (name == line) {
	    line = msg.getValue(YSTRING("partword"));
	    for (const char** b = s_debugs; *b; b++)
		if (line.null() || String(*b).startsWith(line))
		    msg.retValue().append(*b,"\t");
	}
    }
    if (!s_active)
	return false;
    Lock lock(s_mutex);
    if (s_filter && !s_filter.matches(msg))
	return false;
    lock.drop();
    String par;
    dumpParams(msg,par);
    Output("Sniffed '%s' time=%u.%06u%s\r\n  thread=%p '%s'\r\n  data=%p\r\n  retval='%s'%s",
	msg.c_str(),
	(unsigned int)(msg.msgTime().usec() / 1000000),
	(unsigned int)(msg.msgTime().usec() % 1000000),
	(msg.broadcast() ? " (broadcast)" : ""),
	Thread::current(),
	Thread::currentName(),
	msg.userData(),
	msg.retValue().c_str(),
	par.safe());
    return false;
};


void HookHandler::dispatched(const Message& msg, bool handled)
{
    if ((!s_active) || (msg == YSTRING("engine.timer")))
	return;
    Lock lock(s_mutex);
    if (s_filter && !s_filter.matches(msg))
	return;
    lock.drop();
    u_int64_t dt = Time::now() - msg.msgTime().usec();
    String par;
    dumpParams(msg,par);
    const char* rval = msg.retValue().c_str();
    const char* rsep = "'";
    if (handled && rval && (rval[0] != '-' || rval[1]) && (msg == YSTRING("user.auth"))) {
	rval = "(hidden)";
	rsep = "";
    }
    Output("Returned %s '%s' delay=%u.%06u%s\r\n  thread=%p '%s'\r\n  data=%p\r\n  retval=%s%s%s%s",
	String::boolText(handled),
	msg.c_str(),
	(unsigned int)(dt / 1000000),
	(unsigned int)(dt % 1000000),
	(msg.broadcast() ? " (broadcast)" : ""),
	Thread::current(),
	Thread::currentName(),
	msg.userData(),
	rsep,rval,rsep,
	par.safe());
}


MsgSniff::MsgSniff()
    : Plugin("msgsniff"),
      m_first(true)
{
    Output("Loaded module MsgSniffer");
}

void MsgSniff::initialize()
{
    Output("Initializing module MsgSniffer");
    if (m_first) {
	m_first = false;
	s_active = Engine::config().getBoolValue("general","msgsniff",false);
	s_mutex.lock();
	s_filter = Engine::config().getValue("general","filtersniff");
	s_mutex.unlock();
	Engine::install(new SniffHandler);
	Engine::self()->setHook(new HookHandler);
    }
}

INIT_PLUGIN(MsgSniff);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
