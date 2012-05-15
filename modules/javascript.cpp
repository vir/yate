/**
 * javascript.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Javascript channel support based on libyscript
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011 Null Team
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
#include <yatescript.h>

using namespace TelEngine;
namespace { // anonymous

class JsModule : public ChanAssistList
{
public:
    enum {
	Preroute = AssistPrivate
    };
    JsModule();
    virtual ~JsModule();
    virtual void initialize();
    virtual void init(int priority);
    virtual ChanAssist* create(Message& msg, const String& id);
    bool unload();
    virtual bool received(Message& msg, int id);
    virtual bool received(Message& msg, int id, ChanAssist* assist);
    inline JsParser& parser()
	{ return m_assistCode; }
protected:
    virtual bool commandExecute(String& retVal, const String& line);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
private:
    JsParser m_assistCode;
};

class JsAssist : public ChanAssist
{
public:
    enum State {
	NotRouted,
	Routing,
	ReRoute,
    };
    inline JsAssist(ChanAssistList* list, const String& id, ScriptRun* runner)
	: ChanAssist(list, id), m_runner(runner), m_state(NotRouted)
	{ }
    virtual ~JsAssist();
    virtual void msgStartup(Message& msg);
    virtual void msgHangup(Message& msg);
    virtual void msgExecute(Message& msg);
    virtual void msgRinging(Message& msg);
    virtual void msgAnswered(Message& msg);
    virtual bool msgPreroute(Message& msg);
    virtual bool msgRoute(Message& msg);
    virtual bool msgDisconnect(Message& msg, const String& reason);
    bool init();
private:
    bool runFunction(const char* name, Message& msg);
    ScriptRun* m_runner;
    State m_state;
};

#define MKDEBUG(lvl) params().addParam(new ExpOperation((long int)Debug ## lvl,"Debug" # lvl))
class JsEngine : public JsObject
{
    YCLASS(JsEngine,JsObject)
public:
    inline JsEngine(Mutex* mtx)
	: JsObject("Engine",mtx,true)
	{
	    MKDEBUG(Fail);
	    MKDEBUG(Test);
	    MKDEBUG(GoOn);
	    MKDEBUG(Conf);
	    MKDEBUG(Stub);
	    MKDEBUG(Warn);
	    MKDEBUG(Mild);
	    MKDEBUG(Call);
	    MKDEBUG(Note);
	    MKDEBUG(Info);
	    MKDEBUG(All);
	    params().addParam(new ExpFunction("output"));
	    params().addParam(new ExpFunction("debug"));
	}
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};
#undef MKDEBUG

class JsMessage : public JsObject
{
    YCLASS(JsMessage,JsObject)
public:
    inline JsMessage(Mutex* mtx)
	: JsObject("Message",mtx,true), m_message(0), m_owned(false)
	{
	    XDebug(DebugAll,"JsMessage::JsMessage() [%p]",this);
	    params().addParam(new ExpFunction("constructor"));
	}
    inline JsMessage(Message* message, Mutex* mtx, bool owned)
	: JsObject("Message",mtx), m_message(message), m_owned(owned)
	{
	    XDebug(DebugAll,"JsMessage::JsMessage(%p) [%p]",message,this);
	    params().addParam(new ExpFunction("enqueue"));
	    params().addParam(new ExpFunction("dispatch"));
	    params().addParam(new ExpFunction("broadcast"));
	}
    virtual ~JsMessage()
	{
	    XDebug(DebugAll,"JsMessage::~JsMessage() [%p]",this);
	    if (m_owned)
		TelEngine::destruct(m_message);
	}
    virtual void runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    inline void clearMsg()
	{ m_message = 0; m_owned = false; }
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    Message* m_message;
    bool m_owned;
};

class JsFile : public JsObject
{
    YCLASS(JsFile,JsObject)
public:
    inline JsFile(Mutex* mtx)
	: JsObject("File",mtx,true)
	{
	    XDebug(DebugAll,"JsFile::JsFile() [%p]",this);
	    params().addParam(new ExpFunction("exists"));
	    params().addParam(new ExpFunction("remove"));
	    params().addParam(new ExpFunction("rename"));
	    params().addParam(new ExpFunction("mkdir"));
	    params().addParam(new ExpFunction("rmdir"));
	    params().addParam(new ExpFunction("getFileTime"));
	    params().addParam(new ExpFunction("setFileTime"));
	}
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

class JsChannel : public JsObject
{
    YCLASS(JsChannel,JsObject)
public:
    inline JsChannel(JsAssist* assist, Mutex* mtx)
	: JsObject("Channel",mtx,true), m_assist(assist)
	{
	    params().addParam(new ExpFunction("id"));
	    params().addParam(new ExpFunction("status"));
	    params().addParam(new ExpFunction("answer"));
	    params().addParam(new ExpFunction("hangup"));
	    params().addParam(new ExpFunction("callTo"));
	    params().addParam(new ExpFunction("playFile"));
	    params().addParam(new ExpFunction("recFile"));
	}
    static void initialize(ScriptContext* context, JsAssist* assist);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    JsAssist* m_assist;
};

static String s_basePath;

INIT_PLUGIN(JsModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}


bool JsEngine::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("output")) {
	String str;
	for (long int i = oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (str)
		str = *op + " " + str;
	    else
		str = *op;
	}
	if (str)
	    Output("%s",str.c_str());
    }
    else if (oper.name() == YSTRING("debug")) {
	int level = DebugNote;
	String str;
	for (long int i = oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		continue;
	    if ((i == 1) && oper.number() > 1 && op->isInteger())
		level = op->number();
	    else if (*op) {
		if (str)
		    str = *op + " " + str;
		else
		    str = *op;
	    }
	}
	if (str) {
	    if (level > DebugAll)
		level = DebugAll;
	    else if (level <= DebugFail)
		level = DebugGoOn;
	    Debug(&__plugin,level,"%s",str.c_str());
	}
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsEngine::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Engine")))
	addObject(params,"Engine",new JsEngine(mtx));
}


bool JsMessage::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("broadcast")) {
	if (oper.number() != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(m_message && m_message->broadcast()));
    }
    else if (oper.name() == YSTRING("enqueue")) {
	if (oper.number() != 0)
	    return false;
	bool ok = false;
	if (m_owned) {
	    Message* m = m_message;
	    clearMsg();
	    if (m)
		freeze();
	    ok = m && Engine::enqueue(m);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("dispatch")) {
	if (oper.number() != 0)
	    return false;
	bool ok = false;
	if (m_owned && m_message) {
	    Message* m = m_message;
	    clearMsg();
	    ok = Engine::dispatch(*m);
	    m_message = m;
	    m_owned = true;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsMessage::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.number() != 1)
	return;
    ExpOperation* op = popValue(stack,context);
    if (!op)
	return;
    Message* m = new Message(*op);
    ExpEvaluator::pushOne(stack,new ExpWrapper(new JsMessage(m,mutex(),true)));
    TelEngine::destruct(op);
}

void JsMessage::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Message")))
	addObject(params,"Message",new JsMessage(mtx));
}


bool JsFile::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("exists")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::exists(*op)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("remove")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::remove(*op)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("rename")) {
	if (oper.number() != 2)
	    return false;
	ExpOperation* newName = popValue(stack,context);
	if (!newName)
	    return false;
	ExpOperation* oldName = popValue(stack,context);
	if (!oldName) {
	    TelEngine::destruct(newName);
	    return false;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(File::rename(*oldName,*newName)));
	TelEngine::destruct(oldName);
	TelEngine::destruct(newName);
    }
    else if (oper.name() == YSTRING("mkdir")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::mkDir(*op)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("rmdir")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::rmDir(*op)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("getFileTime")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	unsigned int epoch = 0;
	long int fTime = File::getFileTime(*op,epoch) ? (signed long int)epoch : -1;
	ExpEvaluator::pushOne(stack,new ExpOperation(fTime));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("setFileTime")) {
	if (oper.number() != 2)
	    return false;
	ExpOperation* fTime = popValue(stack,context);
	if (!fTime)
	    return false;
	ExpOperation* fName = popValue(stack,context);
	if (!fName) {
	    TelEngine::destruct(fTime);
	    return false;
	}
	bool ok = fTime->isInteger() && (fTime->number() >= 0) &&
	    File::setFileTime(*fName,fTime->number());
	TelEngine::destruct(fTime);
	TelEngine::destruct(fName);
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsFile::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("File")))
	addObject(params,"File",new JsFile(mtx));
}


bool JsChannel::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("id")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ja->id()));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("status")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (ja) {
	    RefPointer<CallEndpoint> cp = ja->locate();
	    Channel* ch = YOBJECT(Channel,cp);
	    if (ch)
		ExpEvaluator::pushOne(stack,new ExpOperation(ch->status()));
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("callTo")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (ja) {
	    RefPointer<CallEndpoint> cp = ja->locate();
	    Channel* ch = YOBJECT(Channel,cp);
	    if (ch)
		ExpEvaluator::pushOne(stack,new ExpOperation(ch->status()));
	}
	else
	    return false;
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsChannel::initialize(ScriptContext* context, JsAssist* assist)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Channel")))
	addObject(params,"Channel",new JsChannel(assist,mtx));
}


JsAssist::~JsAssist()
{
    TelEngine::destruct(m_runner);
}

bool JsAssist::init()
{
    if (!m_runner)
	return false;
    JsObject::initialize(m_runner->context());
    JsEngine::initialize(m_runner->context());
    JsChannel::initialize(m_runner->context(),this);
    JsMessage::initialize(m_runner->context());
    JsFile::initialize(m_runner->context());
    ScriptRun::Status rval = m_runner->run();
    m_runner->reset();
    return (ScriptRun::Succeeded == rval);
}

bool JsAssist::runFunction(const char* name, Message& msg)
{
    if (!m_runner)
	return false;
    DDebug(&__plugin,DebugInfo,"Running function %s in '%s'",name,id().c_str());
    JsParser jp;
    String tmp;
    tmp << name << "()";
    jp.parse(tmp);
    ScriptRun* runner = jp.createRunner(m_runner->context());
    JsMessage* jm = new JsMessage(&msg,m_runner->context()->mutex(),false);
    ExpEvaluator::pushOne(runner->stack(),new ExpWrapper(jm,"message"));
    ScriptRun::Status rval = runner->run();
    jm->clearMsg();
    TelEngine::destruct(runner);
    return (ScriptRun::Succeeded == rval);
}

void JsAssist::msgStartup(Message& msg)
{
    runFunction("onStartup",msg);
}

void JsAssist::msgHangup(Message& msg)
{
    runFunction("onHangup",msg);
}

void JsAssist::msgExecute(Message& msg)
{
    runFunction("onExecute",msg);
}

void JsAssist::msgRinging(Message& msg)
{
    runFunction("onRinging",msg);
}

void JsAssist::msgAnswered(Message& msg)
{
    runFunction("onAnswered",msg);
}

bool JsAssist::msgPreroute(Message& msg)
{
    return false;
}

bool JsAssist::msgRoute(Message& msg)
{
    m_state = Routing;
    return false;
}

bool JsAssist::msgDisconnect(Message& msg, const String& reason)
{
    return false;
}


JsModule::JsModule()
    : ChanAssistList("javascript",true)
{
    Output("Loaded module Javascript");
}

JsModule::~JsModule()
{
    Output("Unloading module Javascript");
}

bool JsModule::commandExecute(String& retVal, const String& line)
{
    if (!line.startsWith("js "))
	return false;
    String cmd = line.substr(3).trimSpaces();
    if (cmd.null())
	return false;

    JsParser parser;
    parser.basePath(s_basePath);
    if (!parser.parse(cmd)) {
	retVal << "parsing failed\r\n";
	return true;
    }
    ScriptRun* runner = parser.createRunner();
    JsObject::initialize(runner->context());
    JsEngine::initialize(runner->context());
    JsMessage::initialize(runner->context());
    JsFile::initialize(runner->context());
    ScriptRun::Status st = runner->run();
    if (st == ScriptRun::Succeeded) {
	while (ExpOperation* op = ExpEvaluator::popOne(runner->stack())) {
	    retVal << "'" << op->name() << "'='" << *op << "'\r\n";
	    TelEngine::destruct(op);
	}
    }
    else
	retVal << ScriptRun::textState(st) << "\r\n";
    TelEngine::destruct(runner);
    return true;
}

bool JsModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    if (partLine.null() || (partLine == "help"))
	itemComplete(msg.retValue(),"js",partWord);
    return Module::commandComplete(msg,partLine,partWord);
}

bool JsModule::received(Message& msg, int id)
{
    switch (id) {
	case Preroute:
	case Route:
	    {
		const String* chanId = msg.getParam("id");
		if (TelEngine::null(chanId))
		    break;
		Lock mylock(this);
		RefPointer <JsAssist> ca = static_cast<JsAssist*>(find(*chanId));
		switch (id) {
		    case Preroute:
			if (ca) {
			    mylock.drop();
			    return ca->msgPreroute(msg);
			}
			ca = static_cast<JsAssist*>(create(msg,*chanId));
			if (ca) {
			    calls().append(ca);
			    mylock.drop();
			    ca->msgStartup(msg);
			    return ca->msgPreroute(msg);
			}
			return false;
		    case Route:
			if (ca) {
			    mylock.drop();
			    return ca->msgRoute(msg);
			}
			ca = static_cast<JsAssist*>(create(msg,*chanId));
			if (ca) {
			    calls().append(ca);
			    mylock.drop();
			    ca->msgStartup(msg);
			    return ca->msgRoute(msg);
			}
			return false;
		} // switch (id)
	    }
	    break;
	case Ringing:
	case Answered:
	    {
		const String* chanId = msg.getParam("peerid");
		if (TelEngine::null(chanId))
		    return false;
		Lock mylock(this);
		RefPointer <JsAssist> ca = static_cast<JsAssist*>(find(*chanId));
		if (!ca)
		    return false;
		switch (id) {
		    case Ringing:
			if (ca)
			    ca->msgRinging(msg);
			return false;
		    case Answered:
			if (ca)
			    ca->msgAnswered(msg);
			return false;
		}
	    }
	    break;
    } // switch (id)
    return ChanAssistList::received(msg,id);
}

bool JsModule::received(Message& msg, int id, ChanAssist* assist)
{
    return ChanAssistList::received(msg,id,assist);
}

ChanAssist* JsModule::create(Message& msg, const String& id)
{
    lock();
    ScriptRun* runner = m_assistCode.createRunner();
    unlock();
    if (!runner)
	return 0;
    DDebug(this,DebugInfo,"Creating Javascript for '%s'",id.c_str());
    JsAssist* ca = new JsAssist(this,id,runner);
    if (ca->init())
	return ca;
    TelEngine::destruct(ca);
    return 0;
}

bool JsModule::unload()
{
    uninstallRelays();
    return true;
}

void JsModule::initialize()
{
    Output("Initializing module Javascript");
    ChanAssistList::initialize();
    setup();
    Configuration cfg(Engine::configFile("javascript"));
    String tmp = Engine::sharedPath();
    tmp << Engine::pathSeparator() << "scripts";
    tmp = cfg.getValue("general","scripts_dir",tmp);
    if (!tmp.endsWith(Engine::pathSeparator()))
	tmp += Engine::pathSeparator();
    s_basePath = tmp;
    lock();
    m_assistCode.clear();
    m_assistCode.basePath(tmp);
    tmp = cfg.getValue("scripts","routing");
    m_assistCode.adjustPath(tmp);
    if (m_assistCode.parseFile(tmp))
	Debug(this,DebugInfo,"Parsed routing script: %s",tmp.c_str());
    else if (tmp)
	Debug(this,DebugWarn,"Failed to parse script: %s",tmp.c_str());
    unlock();
}

void JsModule::init(int priority)
{
    ChanAssistList::init(priority);
    installRelay(Route,priority);
    installRelay(Ringing,priority);
    installRelay(Answered,priority);
    Engine::install(new MessageRelay("call.preroute",this,Preroute,priority));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
