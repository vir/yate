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

#include <stdlib.h>
#include <unistd.h>

using namespace TelEngine;

static Mutex s_mutex(true);
static ObjList s_calls;
static Configuration s_cfg;
static bool s_runs = false;
static int s_total = 0;
static int s_current = 0;
static int s_answers = 0;

static int s_maxcalls = 0;
static int s_numcalls = 0;

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
    inline unsigned long long age() const
	{ return Time::now() - m_start; }
    static GenConnection* find(const String& id);
    static bool oneCall(String* number = 0);
private:
    String m_id;
    String m_status;
    String m_target;
    unsigned long long m_start;
};

class GenThread : public Thread
{
public:
    GenThread()
	: Thread("CallGen")
	{ }
    virtual void run();
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
    bool doCommand(String& line, String& rval);
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
    m_start = Time::now();
    s_mutex.lock();
    s_calls.append(this);
    m_id << "callgen/" << ++s_total;
    ++s_current;
    s_mutex.unlock();
}

GenConnection::~GenConnection()
{
    s_mutex.lock();
    s_calls.remove(this,false);
    --s_current;
    s_mutex.unlock();
}

GenConnection* GenConnection::find(const String& id)
{
    ObjList* l = s_calls.find(id);
    return l ? static_cast<GenConnection*>(l->get()) : 0;
}

bool GenConnection::oneCall(String* number)
{
    int n_min = s_cfg.getIntValue("general","minnum");
    if (n_min <= 0)
	return false;
    int n_max = s_cfg.getIntValue("general","maxnum",n_min);
    if (n_max < n_min)
	return false;
    String num((unsigned)(n_min + ((n_max - n_min) * ::random() / RAND_MAX)));
    Message m("call.route");
    m.addParam("driver","callgen");
    m.addParam("caller",s_cfg.getValue("general","caller","yate"));
    m.addParam("called",num);
    if (!Engine::dispatch(m) || m.retValue().null()) {
	Debug("CallGen",DebugInfo,"No route to call '%s'",num.c_str());
	return false;
    }
    m = "call.execute";
    m.addParam("callto",m.retValue());
    m.retValue().clear();
    GenConnection* conn = new GenConnection;
    m.addParam("id",conn->id());
    m.userData(conn);
    if (Engine::dispatch(m)) {
	conn->setTarget(m.getValue("targetid"));
	if (conn->getTarget().null()) {
	    Debug(DebugInfo,"Answering now generated call %s [%p] because we have no targetid",
		conn->id().c_str(),conn);
	    conn->answered();
	}
	conn->deref();
	if (number)
	    *number = num;
	return true;
    }
    Debug("CallGen",DebugInfo,"Rejecting '%s' unconnected to '%s'",
	conn->id().c_str(),m.getValue("callto"));
    conn->destruct();
    return false;
}

void GenConnection::disconnected(bool final, const char *reason)
{
    Debug("CallGen",DebugInfo,"Disconnected '%s' reason '%s' [%p]",m_id.c_str(),reason,this);
}

void GenConnection::ringing()
{
    Debug("CallGen",DebugInfo,"Ringing '%s' [%p]",m_id.c_str(),this);
}

void GenConnection::answered()
{
    Debug("CallGen",DebugInfo,"Answered '%s' [%p]",m_id.c_str(),this);
    s_mutex.lock();
    ++s_answers;
    s_mutex.unlock();
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

void GenThread::run()
{
    for (;;) {
	::usleep(1000000);
	if (!s_runs || (s_current >= s_maxcalls) || (s_numcalls <= 0))
	    continue;
	--s_numcalls;
	GenConnection::oneCall();
    }
}

bool CmdHandler::doCommand(String& line, String& rval)
{
    if (line.startSkip("set")) {
	int q = line.find('=');
	if (q >= 0) {
	    String val = line.substr(q+1).trimBlanks();
	    line = line.substr(0,q).trimBlanks().toLower();
	    s_cfg.setValue("general",line,val.c_str());
	    rval << "Set '" << line << "' to '" << val << "'";
	}
	else {
	    line.toLower();
	    rval << "Value of '" << line << "' is '" << s_cfg.getValue("general","line") << "'";
	}
    }
    else if (line == "info") {
	rval << "Made " << s_total << " calls, "
	    << s_answers << " answered, "
	    << s_current << " running";
	if (s_runs)
	    rval << ", " << s_numcalls << " to go";
    }
    else if (line == "start") {
	s_numcalls = s_cfg.getIntValue("general","numcalls",100);
	rval << "Generating " << s_numcalls << " new calls";
	s_runs = true;
    }
    else if (line == "stop") {
	s_runs = false;
	s_numcalls = 0;
	s_calls.clear();
	rval << "Stopping generator and clearing calls";
    }
    else if (line == "pause") {
	s_runs = false;
	rval << "No longer generating new calls";
    }
    else if (line == "resume") {
	rval << "Resumed generating new calls, " << s_numcalls << " to go";
	s_runs = true;
    }
    else if (line == "single") {
	String num;
	if (GenConnection::oneCall(&num))
	    rval << "Calling " << num;
	else
	    rval << "Failed to start call";
    }
    else
	return false;
    rval << "\n";
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
		return doCommand(tmp,msg.retValue());
	    break;
	case Help:
	    tmp = msg.getValue("line");
	    if (tmp.null() || (tmp == "callgen")) {
		msg.retValue() << "  callgen {start|stop|pause|resume|single|info|set paramname=value}\n";
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
    s_maxcalls = s_cfg.getIntValue("general","maxcalls",5);
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

	GenThread* gen = new GenThread;
	if (!gen->startup()) {
	    Debug(DebugGoOn,"Failed to start call generator thread");
	    delete gen;
	}
    }
}

INIT_PLUGIN(CallGenPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
