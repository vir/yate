/**
 * callgen.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 * 
 * Call Generator
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

using namespace TelEngine;

static Mutex s_mutex(true);
static ObjList s_calls;
static Configuration s_cfg;
static int s_total = 0;
static int s_current = 0;

class GenConnection : public DataEndpoint
{
public:
    GenConnection();
    ~GenConnection();
    virtual const String& toString() const
        { return m_id; }
    virtual void disconnected(bool final, const char *reason);
    void ringing();
    void answered();
    void hangup();
    inline const String& id() const
	{ return m_id; }
    inline const String& status() const
	{ return m_status; }
    inline void setTarget(const char *target = 0)
        { m_target = target; }
    inline const String& getTarget() const
        { return m_target; }
    static GenConnection* find(const String& id);
private:
    String m_id;
    String m_status;
    String m_target;
};

class ConnHandler : public MessageReceiver
{
public:
    enum {
        Ringing,
        Answered,
        Execute,
	Drop,
    };
    virtual bool received(Message &msg, int id);
};

class CmdHandler : public MessageReceiver
{
public:
    enum {
	Drop,
	Status,
	Command,
	Help
    };
    virtual bool received(Message &msg, int id);
    bool doCommand(String& line);
};

class CallGenPlugin : public Plugin
{
public:
    CallGenPlugin();
    virtual ~CallGenPlugin();
    virtual void initialize();
private:
    bool m_first;
};

GenConnection::GenConnection()
{
    s_mutex.lock();
    s_calls.append(this);
    s_mutex.unlock();
}

GenConnection::~GenConnection()
{
    s_mutex.lock();
    s_calls.remove(this,false);
    s_mutex.unlock();
}

GenConnection* GenConnection::find(const String& id)
{
    ObjList* l = s_calls.find(id);
    return l ? static_cast<GenConnection*>(l->get()) : 0;
}

void GenConnection::disconnected(bool final, const char *reason)
{
    Debug(DebugInfo,"Disconnected '%s' reason '%s' [%p]",m_id.c_str(),reason,this);
}

void GenConnection::ringing()
{
    Debug(DebugInfo,"Ringing '%s' [%p]",m_id.c_str(),this);
}

void GenConnection::answered()
{
    Debug(DebugInfo,"Answered '%s' [%p]",m_id.c_str(),this);
}

void GenConnection::hangup()
{
}

bool ConnHandler::received(Message &msg, int id)
{
    String callid(msg.getValue("targetid"));
    if (!callid.startsWith("callgen/",false))
	return false;
    GenConnection *conn = GenConnection::find(callid);
    if (!conn) {
	Debug(DebugInfo,"Target '%s' was not found in list",callid.c_str());
	return false;
    }
    String text(msg.getValue("text"));
    switch (id) {
        case Answered:
	    conn->answered();
	    break;
        case Ringing:
	    conn->ringing();
	    break;
	case Execute:
	    break;
	case Drop:
	    break;
    }
    return true;
}

bool CmdHandler::doCommand(String& line)
{
    if (line.startSkip("set")) {
    }
    else if (line == "info") {
    }
    else if (line == "start") {
    }
    else if (line == "stop") {
    }
    else if (line == "pause") {
    }
    else if (line == "single") {
    }
    else
	return false;
    return true;
}

bool CmdHandler::received(Message &msg, int id)
{
    String tmp;
    switch (id) {
        case Status:
	    tmp = msg.getValue("module");
	    if (tmp.null() || (tmp == "callgen")) {
		msg.retValue() << "name=callgen,type=misc;total=" << s_total
		    << ",current=" << s_current << "\n";
		if (tmp)
		    return true;
	    }
	    break;
        case Command:
	    tmp = msg.getValue("line");
	    if (tmp.startSkip("callgen"))
		return doCommand(tmp);
	    break;
	case Help:
	    tmp = msg.getValue("line");
	    if (tmp.null() || (tmp == "callgen")) {
		msg.retValue() << "  callgen {start|stop|pause|single|info|set paramname=value}\n";
		if (tmp)
		    return true;
	    }
	    break;
    }
    return false;
}

CallGenPlugin::CallGenPlugin()
    : m_first(true)
{
    Output("Loaded module Call Generator");
}

CallGenPlugin::~CallGenPlugin()
{
    Output("Unloading module Call Generator");
    s_calls.clear();
}

void CallGenPlugin::initialize()
{
    Output("Initializing module Call Generator");
    s_cfg = Engine::configFile("callgen");
    s_cfg.load();
    if (m_first) {
	m_first = false;
	ConnHandler* coh = new ConnHandler;
	Engine::install(new MessageRelay("call.ringing",coh,ConnHandler::Ringing));
	Engine::install(new MessageRelay("call.answered",coh,ConnHandler::Answered));
	Engine::install(new MessageRelay("call.execute",coh,ConnHandler::Execute));
	Engine::install(new MessageRelay("call.drop",coh,ConnHandler::Drop));
	CmdHandler* cmh = new CmdHandler;
	Engine::install(new MessageRelay("engine.status",cmh,CmdHandler::Status));
	Engine::install(new MessageRelay("engine.command",cmh,CmdHandler::Command));
	Engine::install(new MessageRelay("engine.help",cmh,CmdHandler::Help));
    }
}

INIT_PLUGIN(CallGenPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
