/**
 * cdrbuild.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Cdr builder
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

#include <string.h>

using namespace TelEngine;

enum {
    CdrRing,
    CdrCall,
    CdrRinging,
    CdrAnswer,
    CdrHangup,
    CdrDrop,
    EngHalt
};

class CdrHandler : public MessageHandler
{
public:
    CdrHandler(const char *name, int type)
	: MessageHandler(name), m_type(type) { }
    virtual bool received(Message &msg);
private:
    int m_type;
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("status") { }
    virtual bool received(Message &msg);
};

class CdrBuilder : public String
{
public:
    CdrBuilder(const char *name, const char *caller, const char *called);
    virtual ~CdrBuilder();
    void update(int type, unsigned long long val);
    inline void setStatus(const char *status)
	{ m_status = status; }
    String getStatus() const;
    static CdrBuilder *find(String &id);
private:
    inline static int sec(unsigned long long usec)
	{ return (usec + 500000) / 1000000; }
    unsigned long long
	m_ring,
	m_call,
	m_ringing,
	m_answer,
	m_hangup;
    String m_caller;
    String m_called;
    String m_status;
};

static ObjList cdrs;

CdrBuilder::CdrBuilder(const char *name, const char *caller, const char *called)
    : String(name), m_caller(caller), m_called(called), m_status("unknown")
{
    m_ring = m_call = m_ringing = m_answer = m_hangup = 0;
}

CdrBuilder::~CdrBuilder()
{
    const char *dir = m_ring ?
	(m_call ? "bidir" : "incoming") :
	(m_call ? "outgoing" : "unknown");

    if (!m_hangup)
	m_hangup = Time::now();
    if (!m_ring)
	m_ring = m_call;
    if (!m_call)
	m_call = m_ring;
    if (!m_ringing)
	m_ringing = m_call;
    if (!m_answer)
	m_answer = m_hangup;

    Message *m = new Message("cdr");
    m->addParam("time",String(sec(m_ring)));
    m->addParam("chan",c_str());
    m->addParam("direction",dir);
    m->addParam("caller",m_caller);
    m->addParam("called",m_called);
    m->addParam("duration",String(sec(m_hangup - m_ring)));
    m->addParam("billtime",String(sec(m_hangup - m_answer)));
    m->addParam("ringtime",String(sec(m_answer - m_ringing)));
    m->addParam("status",m_status);
    Engine::enqueue(m);
}

String CdrBuilder::getStatus() const
{
    String s(m_status);
    s << "|" << m_caller << "|" << m_called;
    return s;
}

void CdrBuilder::update(int type, unsigned long long val)
{
    switch (type) {
	case CdrRing:
	    m_ring = val;
	    break;
	case CdrCall:
	    m_call = val;
	    break;
	case CdrRinging:
	    if (!m_ringing)
		m_ringing = val;
	    break;
	case CdrAnswer:
	    m_answer = val;
	    break;
	case CdrHangup:
	    m_hangup = val;
	    break;
    }
}

CdrBuilder *CdrBuilder::find(String &id)
{
    ObjList *l = &cdrs;
    for (; l; l=l->next()) {
	CdrBuilder *b = static_cast<CdrBuilder *>(l->get());
	if (b && (*b == id))
	    return b;
    }
    return 0;
}

bool CdrHandler::received(Message &msg)
{
    static Mutex mutex;
    Lock lock(mutex);
    if (m_type == EngHalt) {
	cdrs.clear();
	return false;
    }
    String id(msg.getValue("id"));
    if (id.null()) {
	id = msg.getValue("driver");
	id += "/";
	id += msg.getValue("span");
	id += "/";
	id += msg.getValue("channel");
	if (id == "//")
	    return false;
    }
    CdrBuilder *b = CdrBuilder::find(id);
    if (!b && ((m_type == CdrRing) || (m_type == CdrCall))) {
	b = new CdrBuilder(id,msg.getValue("caller"),msg.getValue("called"));
	cdrs.append(b);
    }
    if (b) {
	const char *s = msg.getValue("status");
	if (s)
	    b->setStatus(s);
	b->update(m_type,msg.msgTime().usec());
	if (m_type == CdrHangup) {
	    cdrs.remove(b);
	    return false;
	}
    }
    else
	Debug("CdrBuilder",DebugGoOn,"Got message '%s' for untracked id '%s'",
	    msg.c_str(),id.c_str());
    return false;
};
		    
bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"cdrbuild"))
	return false;
    String st("name=cdrbuild,type=cdr,format=Status|Caller|Called");
    st << ";cdrs=" << cdrs.count() << ";";
    ObjList *l = &cdrs;
    for (; l; l=l->next()) {
	CdrBuilder *b = static_cast<CdrBuilder *>(l->get());
	if (b) {
	    st << "," << *b << "=" << b->getStatus();
	}
    }
    msg.retValue() << st << "\n";
    return false;
}
			

class CdrBuildPlugin : public Plugin
{
public:
    CdrBuildPlugin();
    virtual void initialize();
private:
    bool m_first;
};

CdrBuildPlugin::CdrBuildPlugin()
    : m_first(true)
{
    Output("Loaded module CdrBuild");
}

void CdrBuildPlugin::initialize()
{
    Output("Initializing module CdrBuild");
    if (m_first) {
	m_first = false;
	Engine::install(new CdrHandler("ring",CdrRing));
	Engine::install(new CdrHandler("call",CdrCall));
	Engine::install(new CdrHandler("ringing",CdrRinging));
	Engine::install(new CdrHandler("answer",CdrAnswer));
	Engine::install(new CdrHandler("hangup",CdrHangup));
	Engine::install(new CdrHandler("dropcdr",CdrDrop));
	Engine::install(new CdrHandler("engine.halt",EngHalt));
	Engine::install(new StatusHandler);
    }
}

INIT_PLUGIN(CdrBuildPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
