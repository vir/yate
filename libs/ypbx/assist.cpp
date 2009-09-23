/**
 * assist.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common C++ base classes for PBX related plugins
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

#include <yatepbx.h>

using namespace TelEngine;

bool ChanAssistList::received(Message& msg, int id)
{
    String* chanId = msg.getParam("id");
    if (!chanId || chanId->null())
	return (id < Private) && Module::received(msg,id);
    Lock mylock(this);
    RefPointer <ChanAssist> ca = find(*chanId);
    switch (id) {
	case Startup:
	    if (ca) {
		Debug(this,DebugNote,"Channel '%s' already assisted!",chanId->c_str());
		mylock.drop();
		ca->msgStartup(msg);
		return false;
	    }
	    ca = create(msg,*chanId);
	    if (ca) {
		m_calls.append(ca);
		mylock.drop();
		ca->msgStartup(msg);
	    }
	    return false;
	case Hangup:
	    if (ca) {
		removeAssist(ca);
		mylock.drop();
		ca->msgHangup(msg);
		ca->deref();
		ca = 0;
	    }
	    return false;
	case Execute:
	    if (ca) {
		mylock.drop();
		ca->msgExecute(msg);
		return false;
	    }
	    ca = create(msg,*chanId);
	    if (ca) {
		m_calls.append(ca);
		mylock.drop();
		ca->msgStartup(msg);
		ca->msgExecute(msg);
	    }
	    return false;
	case Disconnected:
	    mylock.drop();
	    return ca && ca->msgDisconnect(msg,msg.getValue("reason"));
	default:
	    mylock.drop();
	    if (ca)
		return received(msg,id,ca);
	    return (id < Private) && Module::received(msg,id);
    }
}

void ChanAssistList::removeAssist(ChanAssist* assist)
{
    lock();
    m_calls.remove(assist,false);
    unlock();
}

void ChanAssistList::initialize()
{
    Module::initialize();
    if (m_first) {
	m_first = false;
	init();
    }
}

bool ChanAssistList::received(Message& msg, int id, ChanAssist* assist)
{
    return false;
}

void ChanAssistList::init(int priority)
{
    installRelay(Execute,priority);
    Engine::install(new MessageRelay("chan.startup",this,Startup,priority));
    Engine::install(new MessageRelay("chan.hangup",this,Hangup,priority));
    Engine::install(new MessageRelay("chan.disconnected",this,Disconnected,priority));
}


ChanAssist::~ChanAssist()
{
    Debug(list(),DebugAll,"Assistant for '%s' deleted",id().c_str());
    if (list())
	list()->removeAssist(this);
}

void ChanAssist::msgStartup(Message& msg)
{
    Debug(list(),DebugInfo,"Assistant for '%s' startup",id().c_str());
}

void ChanAssist::msgHangup(Message& msg)
{
    Debug(list(),DebugInfo,"Assistant for '%s' hangup",id().c_str());
}

void ChanAssist::msgExecute(Message& msg)
{
    Debug(list(),DebugInfo,"Assistant for '%s' execute",id().c_str());
}

bool ChanAssist::msgDisconnect(Message& msg, const String& reason)
{
    Debug(list(),DebugInfo,"Assistant for '%s' disconnected, reason '%s'",
	id().c_str(),reason.c_str());
    return false;
}

RefPointer<CallEndpoint> ChanAssist::locate(const String& id)
{
    if (id.null())
	return 0;
    Message m("chan.locate");
    m.addParam("id",id);
    return static_cast<CallEndpoint*>(Engine::dispatch(m) ? m.userData() : 0);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
