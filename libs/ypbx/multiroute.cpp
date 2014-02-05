/**
 * multiroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Multiple routing implementation
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

#include <yatepbx.h>

#include <stdlib.h>
#include <stdarg.h>

using namespace TelEngine;


bool CallInfo::copyParam(const NamedList& original, const String& name, bool clear)
{
    NamedString* param = original.getParam(name);
    if (param) {
	setParam(name,param->c_str());
	return true;
    }
    else if (clear)
	clearParam(name);
    return false;
}

void CallInfo::copyParams(const NamedList& original, bool clear, ...)
{
    va_list va;
    va_start(va,clear);
    while (const char* name = va_arg(va,const char*))
	copyParam(original,name,clear);
    va_end(va);
}

void CallInfo::fillParam(NamedList& target, const String& name, bool clear)
{
    NamedString* param = getParam(name);
    if (param)
	target.setParam(name,param->c_str());
    else if (clear)
	target.clearParam(name);
}

void CallInfo::fillParams(NamedList& target)
{
    unsigned int n = length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = getParam(i);
	if (param)
	    target.setParam(param->name(),param->c_str());
    }
}


CallInfo* CallList::find(const String& id)
{
    if (id.null())
	return 0;
    return static_cast<CallInfo*>(m_calls[id]);
}

CallInfo* CallList::find(const CallEndpoint* call)
{
    ObjList* l = m_calls.skipNull();
    for (; l; l = l->skipNext()) {
	CallInfo* info = static_cast<CallInfo*>(l->get());
	if (info->call() == call)
	    return info;
    }
    return 0;
}


MultiRouter::MultiRouter(const char* trackName)
    : Mutex(true,"MultiRouter"),
      m_trackName(trackName),
      m_relRoute(0), m_relExecute(0),
      m_relHangup(0), m_relDisconnected(0)
{
}

MultiRouter::~MultiRouter()
{
    Engine::uninstall(m_relRoute);
    Engine::uninstall(m_relExecute);
    Engine::uninstall(m_relDisconnected);
    Engine::uninstall(m_relHangup);
}

void MultiRouter::setup(int priority)
{
    if (priority <= 0)
	priority = 20;
    if (!m_relHangup)
	Engine::install(m_relHangup =
	    new MessageRelay("chan.hangup",this,Hangup,priority,m_trackName));
    if (!m_relDisconnected)
	Engine::install(m_relDisconnected =
	    new MessageRelay("chan.disconnected",this,Disconnected,priority,m_trackName));
    if (!m_relExecute)
	Engine::install(m_relExecute =
	    new MessageRelay("call.execute",this,Execute,priority,m_trackName));
    if (!m_relRoute)
	Engine::install(m_relRoute =
	    new MessageRelay("call.route",this,Route,priority,m_trackName));
}

bool MultiRouter::received(Message& msg, int id)
{
    CallEndpoint* call = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
    bool first = false;
    CallInfo* info = 0;
    String chanid(msg.getValue("id"));
    Lock lock(this);
    if (call)
	info = m_list.find(call);
    if (info) {
	if (*info != chanid) {
	    Debug(DebugGoOn,"Channel mismatch! call=%p id='%s' stored='%s'",
		call,chanid.c_str(),info->c_str());
	    return false;
	}
    }
    else
	info = m_list.find(chanid);
    if (info) {
	if (!call)
	    call = info->call();
	else if (!info->call())
	    info->setCall(call);
	else if (info->call() != call) {
	    Debug(DebugGoOn,"Channel mismatch! id='%s' call=%p stored=%p",
		chanid.c_str(),call,info->call());
	    return false;
	}
    }
    else if ((id == Route) || (id == Execute)) {
	info = new CallInfo(chanid,call);
	info->copyParams(msg,false,"module","address","billid","caller","called","callername",0);
	m_list.append(info);
	first = true;
	DDebug(DebugInfo,"MultiRouter built '%s' @ %p for %p",
	    chanid.c_str(),info,call);
    }
    else
	return false;
    DDebug(DebugAll,"MultiRouter::received '%s' for '%s' info=%p call=%p",
	msg.c_str(),chanid.c_str(),info,call);
    switch (id) {
	case Route:
	    return msgRoute(msg,*info,first);
	case Execute:
	    if (!call)
		return false;
	    return msgExecute(msg,*info,first);
	case Disconnected:
	    return msgDisconnected(msg,*info);
	case Hangup:
	    info->clearCall();
	    msgHangup(msg,*info);
	    m_list.remove(info);
	    DDebug(DebugInfo,"MultiRouter destroyed '%s' @ %p",info->c_str(),info);
	    info->destruct();
	    break;
	default:
	    Debug(DebugFail,"Invalid id %d in MultiRouter::received()",id);
    }
    return false;
}

bool MultiRouter::msgRoute(Message& msg, CallInfo& info, bool first)
{
    return false;
}

bool MultiRouter::msgExecute(Message& msg, CallInfo& info, bool first)
{
    return false;
}

bool MultiRouter::msgDisconnected(Message& msg, CallInfo& info)
{
    info.copyParams(msg,true,"reason","error",0);
    Message* m = buildExecute(info,msg.getBoolValue("reroute"));
    if (m) {
	m->userData(info.call());
	Engine::enqueue(m);
	return true;
    }
    return false;
}

void MultiRouter::msgHangup(Message& msg, CallInfo& info)
{
}

Message* MultiRouter::defaultExecute(CallInfo& info, const char* route)
{
    Message* m = new Message("call.execute");
    m->addParam("id",info);
    info.fillParams(*m);
    if (!null(route))
	m->setParam("callto",route);
    return m;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
