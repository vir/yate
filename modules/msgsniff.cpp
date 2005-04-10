/*
 * msgsniff.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A sample message sniffer that inserts a wildcard message handler
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

#include <yatengine.h>

#include <unistd.h>

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

bool SniffHandler::received(Message &msg)
{
    if (msg == "engine.timer")
	return false;
    String par;
    unsigned n = msg.length();
    for (unsigned i = 0; i < n; i++) {
	NamedString *s = msg.getParam(i);
	if (s)
	    par << "\n  param['" << s->name() << "'] = '" << *s << "'";
    }
    Output("Sniffed '%s' time=" FMT64 " thread=%p data=%p retval='%s'%s",
	msg.c_str(),msg.msgTime().usec(),
	Thread::current(),
	msg.userData(),msg.retValue().c_str(),par.safe());
    return false;
};

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
	Engine::install(new SniffHandler);
    }
}

INIT_PLUGIN(MsgSniff);

/* vi: set ts=8 sw=4 sts=4 noet: */
