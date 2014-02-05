/**
 * callgen.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Call Generator
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

#include <yatephone.h>

#include <stdlib.h>

using namespace TelEngine;
namespace { // anonymous

static Mutex s_mutex(true,"CallGen");
static ObjList s_calls;
static Configuration s_cfg;
static bool s_runs = false;
static int s_total = 0;
static int s_totalst = 0;
static int s_current = 0;
static int s_ringing = 0;
static int s_answers = 0;
static int s_cap10s = 0;

static int s_numcalls = 0;
static const String s_parameters("parameters");

static const char s_mini[] = "callgen {start|stop|drop|pause|resume|single|info|reset|load|save|set paramname[=value]}";
static const char s_help[] = "Commands to control the Call Generator";

class GenConnection : public CallEndpoint
{
public:
    GenConnection(unsigned int lifetime, const String& callto);
    ~GenConnection();
    virtual void disconnected(bool final, const char *reason);
    void ringing();
    void answered();
    void makeSource();
    void makeConsumer();
    void drop(const char *reason);
    inline const String& status() const
	{ return m_status; }
    inline const String& party() const
	{ return m_callto; }
    inline void setTarget(const char *target = 0)
	{ m_target = target; }
    inline const String& getTarget() const
	{ return m_target; }
    inline bool oldAge(u_int64_t now) const
	{ return now > m_finish; }
    static GenConnection* find(const String& id);
    static bool oneCall(String* target = 0);
    static int dropAll(bool resume = false);
private:
    String m_status;
    String m_callto;
    String m_target;
    u_int64_t m_finish;
};

class DummyConsumer : public DataConsumer
{
public:
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
	{ return invalidStamp(); }
};

class GenThread : public Thread
{
public:
    GenThread()
	: Thread("CallGen Caller")
	{ }
    virtual void run();
};

class CleanThread : public Thread
{
public:
    CleanThread()
	: Thread("CallGen Cleaner")
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
    bool doComplete(const String& partLine, const String& partWord, String& rval);
};

class CallGenPlugin : public Plugin
{
public:
    CallGenPlugin();
    virtual ~CallGenPlugin();
    virtual void initialize();
private:
    bool m_first;
    ConnHandler* m_conn;
    CmdHandler* m_cmd;
};


GenConnection::GenConnection(unsigned int lifetime, const String& callto)
    : m_callto(callto)
{
    if (!lifetime)
	lifetime = 60000;
    if (lifetime < 100)
	lifetime = 100;
    m_finish = Time::now() + ((u_int64_t)lifetime * 1000);
    m_status = "calling";
    s_mutex.lock();
    s_calls.append(this);
    String tmp("callgen/");
    tmp << ++s_total;
    setId(tmp);
    ++s_current;
    ++s_totalst;
    s_mutex.unlock();
    Output("Generating %u ms call %s to: %s",lifetime,id().c_str(),m_callto.c_str());
    Message* m = new Message("chan.startup");
    m->addParam("module","callgen");
    m->addParam("id",id());
    m->addParam("called",m_callto);
    Engine::enqueue(m);
}

GenConnection::~GenConnection()
{
    if (!Engine::exiting())
	Output("Ended %s %s to: %s",
	    m_status.c_str(),id().c_str(),m_callto.c_str());
    Message* m = new Message("chan.hangup");
    m->addParam("module","callgen");
    m->addParam("id",id());
    m->addParam("status",m_status);
    Engine::enqueue(m);
    m_status = "destroyed";
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

bool GenConnection::oneCall(String* target)
{
    Message m("call.route");
    m.addParam("module","callgen");
    m.addParam("caller",s_cfg.getValue(s_parameters,YSTRING("caller"),"yate"));
    String callto(s_cfg.getValue(s_parameters,YSTRING("callto")));
    if (callto.null()) {
	String called(s_cfg.getValue(s_parameters,YSTRING("called")));
	if (called.null())
	    return false;
	if (target)
	    *target = called;
	m.addParam("called",called);
	if (!Engine::dispatch(m) || m.retValue().null()) {
	    Debug("CallGen",DebugInfo,"No route to call '%s'",called.c_str());
	    return false;
	}
	callto = m.retValue();
	m.retValue().clear();
    }
    if (target) {
	if (*target)
	    *target << " ";
	*target << callto;
    }
    m = "call.execute";
    m.addParam("callto",callto);
    unsigned int lifetime = s_cfg.getIntValue(s_parameters,YSTRING("maxlife"));
    if (lifetime) {
	unsigned int minlife = s_cfg.getIntValue(s_parameters,YSTRING("minlife"));
	if (minlife && (minlife < lifetime))
	    lifetime -= (int)(((lifetime - minlife) * (int64_t)Random::random()) / RAND_MAX);
    }
    GenConnection* conn = new GenConnection(lifetime,callto);
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
	return true;
    }
    Debug("CallGen",DebugInfo,"Rejecting '%s' unconnected to '%s'",
	conn->id().c_str(),callto.c_str());
    conn->destruct();
    return false;
}

int GenConnection::dropAll(bool resume)
{
    int dropped = 0;
    s_mutex.lock();
    s_runs = false;
    ListIterator iter(s_calls);
    for (;;) {
	RefPointer<GenConnection> c = static_cast<GenConnection*>(iter.get());
	s_mutex.unlock();
	if (!c)
	    break;
	c->drop("dropped");
	c = 0;
	s_mutex.lock();
	++dropped;
    }
    s_runs = resume;
    return dropped;
}

void GenConnection::disconnected(bool final, const char *reason)
{
    Debug("CallGen",DebugInfo,"Disconnected '%s' reason '%s' [%p]",id().c_str(),reason,this);
    if (reason)
	m_status << " (" << reason << ")";
}

void GenConnection::drop(const char *reason)
{
    Debug("CallGen",DebugInfo,"Dropping '%s' reason '%s' [%p]",id().c_str(),reason,this);
    disconnect(reason);
    if (reason)
	m_status << " (" << reason << ")";
}

void GenConnection::ringing()
{
    Debug("CallGen",DebugInfo,"Ringing '%s' [%p]",id().c_str(),this);
    m_status = "ringing";
    s_mutex.lock();
    ++s_ringing;
    bool media =s_cfg.getBoolValue("parameters","earlymedia",true);
    s_mutex.unlock();
    if (media) {
	makeSource();
	makeConsumer();
    }
}

void GenConnection::answered()
{
    Debug("CallGen",DebugInfo,"Answered '%s' [%p]",id().c_str(),this);
    m_status = "answered";
    s_mutex.lock();
    ++s_answers;
    s_mutex.unlock();
    makeSource();
    makeConsumer();
}

void GenConnection::makeSource()
{
    if (getSource())
	return;
    s_mutex.lock();
    String src(s_cfg.getValue("parameters","source"));
    s_mutex.unlock();
    if (src) {
	Message m("chan.attach");
	m.addParam("id",id());
	m.addParam("source",src);
	m.addParam("single",String::boolText(true));
	m.userData(this);
	Engine::dispatch(m);
    }
}

void GenConnection::makeConsumer()
{
    if (getConsumer())
	return;
    s_mutex.lock();
    String cons(s_cfg.getValue("parameters","consumer"));
    s_mutex.unlock();
    if (cons) {
	if ((cons == "dummy") || (cons == "*")) {
	    DummyConsumer* dummy = new DummyConsumer;
	    setConsumer(dummy);
	    dummy->deref();
	}
	else {
	    Message m("chan.attach");
	    m.addParam("id",id());
	    m.addParam("consumer",cons);
	    m.addParam("single",String::boolText(true));
	    m.userData(this);
	    Engine::dispatch(m);
	}
    }
}


bool ConnHandler::received(Message &msg, int id)
{
    String callid(msg.getValue("targetid"));
    if (!callid.startsWith("callgen/",false))
	return false;
    s_mutex.lock();
    RefPointer<GenConnection> conn = GenConnection::find(callid);
    s_mutex.unlock();
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
    Debug("CallGen",DebugInfo,"GenThread::run() [%p]",this);
    int tonext = 10000;
    int calls = 0;
    u_int32_t s10 = Time::secNow() / 10;
    while (!Engine::exiting()) {
	Thread::usleep(tonext);
	tonext = 100000;
	if (!s_runs || (s_numcalls <= 0)) {
	    s_cap10s = calls = 0;
	    continue;
	}
	tonext = 10000;
	u_int32_t s = (Time::secNow() / 10) - s10;
	if (s) {
	    s10 += s;
	    s_cap10s = calls / s;
	    calls = 0;
	}
	Lock lock(s_mutex);
	if (s_current >= s_cfg.getIntValue(s_parameters,YSTRING("maxcalls"),5))
	    continue;
	--s_numcalls;
	tonext = s_cfg.getIntValue(s_parameters,YSTRING("avgdelay"),1000);
	lock.drop();
	tonext = (int)(((int64_t)Random::random() * tonext * 2000) / RAND_MAX);
	if (GenConnection::oneCall())
	    calls++;
    }
    s_cap10s = 0;
}

void CleanThread::run()
{
    Debug("CallGen",DebugInfo,"CleanThread::run() [%p]",this);
    while (!Engine::exiting()) {
	Thread::usleep(100000);
	s_mutex.lock();
	Time t;
	ListIterator iter(s_calls);
	for (;;) {
	    RefPointer<GenConnection> c = static_cast<GenConnection*>(iter.get());
	    s_mutex.unlock();
	    if (!c)
		break;
	    if (c->oldAge(t))
		c->drop("finished");
	    c = 0;
	    s_mutex.lock();
	}
    }
}


static const char* s_cmds[] = {
    "start",
    "stop",
    "drop",
    "pause",
    "resume",
    "single",
    "info",
    "reset",
    "load",
    "save",
    "set",
    0
};

bool CmdHandler::doComplete(const String& partLine, const String& partWord, String& rval)
{
    if (partLine.null() && partWord.null())
	return false;
    if (partLine.null() || (partLine == "help"))
	Module::itemComplete(rval,"callgen",partWord);
    else if (partLine == "callgen") {
	for (const char** list = s_cmds; *list; list++)
	    Module::itemComplete(rval,*list,partWord);
	return true;
    }
    else if (partLine == "callgen set") {
	const NamedList* sect = s_cfg.getSection("parameters");
	if (!sect)
	    return false;
	unsigned int n = sect->length();
	for (unsigned int i = 0; i < n; i++) {
	    const NamedString* param = sect->getParam(i);
	    if (param) {
		if (partWord == (param->name() + "=")) {
		    // complete the value
		    Module::itemComplete(rval,param->name() + "=" + *param,partWord);
		    return true;
		}
		Module::itemComplete(rval,param->name(),partWord);
	    }
	}
	return true;
    }
    return false;
}

bool CmdHandler::doCommand(String& line, String& rval)
{
    if (line.startSkip("set")) {
	int q = line.find('=');
	s_mutex.lock();
	if (q >= 0) {
	    String val = line.substr(q+1).trimBlanks();
	    line = line.substr(0,q).trimBlanks().toLower();
	    s_cfg.setValue(s_parameters,line,val.c_str());
	    rval << "Set '" << line << "' to '" << val << "'";
	}
	else {
	    line.toLower();
	    rval << "Value of '" << line << "' is '" << s_cfg.getValue(s_parameters,line) << "'";
	}
	s_mutex.unlock();
    }
    else if (line == "info") {
	s_mutex.lock();
	rval << "Made " << s_totalst << " calls, "
	    << s_ringing << " ring, "
	    << s_answers << " answered, "
	    << s_current << " running";
	if (s_runs) {
	    int maxcalls = s_cfg.getIntValue(s_parameters,YSTRING("maxcalls"),5);
	    if (maxcalls < 0)
		maxcalls = 0;
	    if (maxcalls)
		rval << " out of " << maxcalls;
	    rval << ", " << s_numcalls << " to go";
	    rval << ", " << (s_cap10s / 10) << "." << (s_cap10s % 10) << " CAPS";
	}
	s_mutex.unlock();
    }
    else if (line == "start") {
	s_mutex.lock();
	s_numcalls = s_cfg.getIntValue(s_parameters,YSTRING("numcalls"),100);
	rval << "Generating " << s_numcalls << " new calls";
	s_runs = true;
	s_mutex.unlock();
    }
    else if (line == "stop") {
	s_mutex.lock();
	s_runs = false;
	s_numcalls = 0;
	s_mutex.unlock();
	int dropped = GenConnection::dropAll();
	rval << "Stopping generator and cleared " << dropped << " calls";
    }
    else if (line == "drop") {
	int dropped = GenConnection::dropAll(s_runs);
	rval << "Cleared " << dropped << " calls and continuing";
    }
    else if (line == "pause") {
	s_runs = false;
	rval << "No longer generating new calls";
    }
    else if (line == "resume") {
	s_mutex.lock();
	rval << "Resumed generating new calls, " << s_numcalls << " to go";
	s_runs = true;
	s_mutex.unlock();
    }
    else if (line == "single") {
	String dest;
	if (GenConnection::oneCall(&dest))
	    rval << "Calling " << dest;
	else {
	    rval << "Failed to start call";
	    if (dest)
		rval << " to " << dest;
	}
    }
    else if (line == "reset") {
	s_mutex.lock();
	s_totalst = 0;
	s_ringing = 0;
	s_answers = 0;
	s_mutex.unlock();
	rval << "Statistics reset";
    }
    else if (line == "load") {
	s_mutex.lock();
	bool ok = s_cfg.load(false);
	if (ok)
	    rval << "Loaded config from " << s_cfg;
	else
	    rval << "Failed to load from " << s_cfg;
	s_mutex.unlock();
    }
    else if (line == "save") {
	s_mutex.lock();
	if (s_cfg.getBoolValue(YSTRING("general"),YSTRING("cansave"),true)) {
	    s_cfg.save();
	    rval << "Saved config to " << s_cfg;
	}
	else
	    rval << "Saving is disabled from config file";
	s_mutex.unlock();
    }
    else if (line.null() || (line == "help") || (line == "?"))
	rval << "Usage: " << s_mini << "\r\n" << s_help;
    else
	return false;
    rval << "\r\n";
    return true;
}

bool CmdHandler::received(Message &msg, int id)
{
    String tmp;
    switch (id) {
	case Status:
	    tmp = msg.getValue(YSTRING("module"));
	    if (tmp.null() || (tmp == YSTRING("callgen"))) {
		s_mutex.lock();
		msg.retValue() << "name=callgen,type=varchans,format=Status|Callto"
		    << ";total=" << s_total
		    << ",ring=" << s_ringing
		    << ",answered=" << s_answers
		    << ",chans=" << s_current;
		if (msg.getBoolValue(YSTRING("details"),true)) {
		    msg.retValue() << ";";
		    ObjList *l = &s_calls;
		    bool first = true;
		    for (; l; l=l->next()) {
			GenConnection *c = static_cast<GenConnection *>(l->get());
			if (c) {
			    if (first)
				first = false;
			    else
				msg.retValue() << ",";
			    msg.retValue() << c->id() << "=" << c->status() << "|" << c->party();
			}
		    }
		}
		msg.retValue() << "\r\n";
		s_mutex.unlock();
		if (tmp)
		    return true;
	    }
	    break;
	case Command:
	    tmp = msg.getValue(YSTRING("line"));
	    if (tmp.startSkip("callgen"))
		return doCommand(tmp,msg.retValue());
	    return doComplete(msg.getValue(YSTRING("partline")),msg.getValue(YSTRING("partword")),msg.retValue());
	    break;
	case Help:
	    tmp = msg.getValue(YSTRING("line"));
	    if (tmp.null() || (tmp == YSTRING("callgen"))) {
		msg.retValue() << "  " << s_mini << "\r\n";
		if (tmp) {
		    msg.retValue() << s_help << "\r\n";
		    return true;
		}
	    }
	    break;
    }
    return false;
}


CallGenPlugin::CallGenPlugin()
    : Plugin("callgen"),
      m_first(true), m_conn(0), m_cmd(0)
{
    Output("Loaded module Call Generator");
}

CallGenPlugin::~CallGenPlugin()
{
    s_mutex.lock();
    Output("Unloading module Call Generator, clearing %u calls",s_calls.count());
    s_runs = false;
    s_mutex.unlock();
    GenConnection::dropAll();
    s_calls.clear();
    TelEngine::destruct(m_conn);
    TelEngine::destruct(m_cmd);
}

void CallGenPlugin::initialize()
{
    Output("Initializing module Call Generator");
    s_mutex.lock();
    s_cfg = Engine::configFile("callgen",Engine::clientMode());
    s_cfg.load(false);
    s_mutex.unlock();
    if (m_first) {
	m_first = false;

	m_conn = new ConnHandler;
	Engine::install(new MessageRelay("call.ringing",m_conn,ConnHandler::Ringing,100,name()));
	Engine::install(new MessageRelay("call.answered",m_conn,ConnHandler::Answered,100,name()));
	Engine::install(new MessageRelay("call.execute",m_conn,ConnHandler::Execute,100,name()));
	Engine::install(new MessageRelay("call.drop",m_conn,ConnHandler::Drop,100,name()));

	m_cmd = new CmdHandler;
	Engine::install(new MessageRelay("engine.status",m_cmd,CmdHandler::Status,100,name()));
	Engine::install(new MessageRelay("engine.command",m_cmd,CmdHandler::Command,100,name()));
	Engine::install(new MessageRelay("engine.help",m_cmd,CmdHandler::Help,100,name()));

	CleanThread* cln = new CleanThread;
	if (!cln->startup()) {
	    Debug(DebugGoOn,"Failed to start call generator cleaner thread");
	    delete cln;
	    return;
	}
	GenThread* gen = new GenThread;
	if (!gen->startup()) {
	    Debug(DebugGoOn,"Failed to start call generator thread");
	    delete gen;
	}
    }
}

INIT_PLUGIN(CallGenPlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
