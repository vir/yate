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

INIT_PLUGIN(JsModule);

class JsMessage;

class JsAssist : public ChanAssist
{
public:
    enum State {
	NotStarted,
	PreRoute,
	Routing,
	ReRoute,
	Ended,
	Hangup
    };
    inline JsAssist(ChanAssistList* list, const String& id, ScriptRun* runner)
	: ChanAssist(list, id),
	  m_runner(runner), m_state(NotStarted), m_handled(false)
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
    inline JsMessage* message()
	{ return m_message; }
    void handled()
	{ m_handled = true; }
private:
    bool runFunction(const String& name, Message& msg);
    bool runScript(Message* msg, State newState);
    bool setMsg(Message* msg);
    void clearMsg();
    ScriptRun* m_runner;
    State m_state;
    bool m_handled;
    RefPointer<JsMessage> m_message;
};

class JsGlobal : public NamedString
{
public:
    JsGlobal(const char* scriptName, const char* fileName);
    virtual ~JsGlobal();
    bool fileChanged(const char* fileName) const;
    inline JsParser& parser()
	{ return m_jsCode; }
    inline ScriptContext* context()
	{ return m_context; }
    bool runMain();
    static void markUnused();
    static void freeUnused();
    static void initScript(const String& scriptName, const String& fileName);
    inline static void unloadAll()
	{ s_globals.clear(); }
private:
    JsParser m_jsCode;
    RefPointer<ScriptContext> m_context;
    unsigned int m_fileTime;
    bool m_inUse;
    static ObjList s_globals;
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
	    params().addParam(new ExpFunction("dump_r"));
	    params().addParam(new ExpFunction("print_r"));
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
	    XDebug(&__plugin,DebugAll,"JsMessage::JsMessage() [%p]",this);
	}
    inline JsMessage(Message* message, Mutex* mtx, bool owned)
	: JsObject("Message",mtx), m_message(message), m_owned(owned)
	{
	    XDebug(&__plugin,DebugAll,"JsMessage::JsMessage(%p) [%p]",message,this);
	    params().addParam(new ExpFunction("enqueue"));
	    params().addParam(new ExpFunction("dispatch"));
	    params().addParam(new ExpFunction("name"));
	    params().addParam(new ExpFunction("broadcast"));
	    params().addParam(new ExpFunction("retValue"));
	}
    virtual ~JsMessage()
	{
	    XDebug(&__plugin,DebugAll,"JsMessage::~JsMessage() [%p]",this);
	    if (m_owned)
		TelEngine::destruct(m_message);
	}
    virtual NamedList* nativeParams() const
	{ return m_message; }
    virtual void fillFieldNames(ObjList& names)
	{ if (m_message) ScriptContext::fillFieldNames(names,*m_message); }
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpFunction("install"));
	    construct->params().addParam(new ExpFunction("uninstall"));
	}
    inline void clearMsg()
	{ m_message = 0; m_owned = false; }
    inline void setMsg(Message* message, bool owned = false)
	{ m_message = message; m_owned = owned; }
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    ObjList m_handlers;
    Message* m_message;
    bool m_owned;
};

class JsHandler : public MessageHandler
{
    YCLASS(JsHandler,MessageHandler)
public:
    inline JsHandler(const char* name, unsigned priority, const ExpFunction& func, GenObject* context)
	: MessageHandler(name,priority,__plugin.name()),
	  m_function(func.name(),1)
	{
	    XDebug(&__plugin,DebugAll,"JsHandler::JsHandler('%s',%u,'%s') [%p]",
		name,priority,func.name().c_str(),this);
	    ScriptRun* runner = YOBJECT(ScriptRun,context);
	    if (runner) {
		m_context = runner->context();
		m_code = runner->code();
	    }
	}
    virtual ~JsHandler()
	{
	    XDebug(&__plugin,DebugAll,"JsHandler::~JsHandler() '%s' [%p]",c_str(),this);
	}
    virtual bool received(Message& msg);
private:
    ExpFunction m_function;
    RefPointer<ScriptContext> m_context;
    RefPointer<ScriptCode> m_code;
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
	: JsObject("Channel",mtx,false), m_assist(assist)
	{
	    params().addParam(new ExpFunction("id"));
	    params().addParam(new ExpFunction("peerid"));
	    params().addParam(new ExpFunction("status"));
	    params().addParam(new ExpFunction("direction"));
	    params().addParam(new ExpFunction("answer"));
	    params().addParam(new ExpFunction("hangup"));
	    params().addParam(new ExpFunction("callTo"));
	    params().addParam(new ExpFunction("callJust"));
	    params().addParam(new ExpFunction("playFile"));
	    params().addParam(new ExpFunction("recFile"));
	}
    static void initialize(ScriptContext* context, JsAssist* assist);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    JsAssist* m_assist;
};

static String s_basePath;

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow) {
	JsGlobal::unloadAll();
	return __plugin.unload();
    }
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
	    TelEngine::destruct(op);
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
	    TelEngine::destruct(op);
	}
	if (str) {
	    if (level > DebugAll)
		level = DebugAll;
	    else if (level <= DebugFail)
		level = DebugGoOn;
	    Debug(&__plugin,level,"%s",str.c_str());
	}
    }
    else if (oper.name() == YSTRING("dump_r")) {
	String buf;
	if (oper.number() == 0) {
	    ScriptRun* run = YOBJECT(ScriptRun,context);
	    if (run)
		dumpRecursive(run->context(),buf);
	    else
		dumpRecursive(context,buf);
	}
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		return false;
	    dumpRecursive(op,buf);
	    TelEngine::destruct(op);
	}
	else
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(buf));
    }
    else if (oper.name() == YSTRING("print_r")) {
	if (oper.number() == 0) {
	    ScriptRun* run = YOBJECT(ScriptRun,context);
	    if (run)
		printRecursive(run->context());
	    else
		printRecursive(context);
	}
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		return false;
	    printRecursive(op);
	    TelEngine::destruct(op);
	}
	else
	    return false;
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


bool JsMessage::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (ScriptContext::hasField(stack,oper.name(),context))
	return JsObject::runAssign(stack,oper,context);
    if (!m_message)
	return false;
    ExpWrapper* w = YOBJECT(ExpWrapper,&oper);
    if (w && !w->object())
	m_message->clearParam(oper.name());
    else
	m_message->setParam(new NamedString(oper.name(),oper));
    return true;
}

bool JsMessage::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsMessage::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("broadcast")) {
	if (oper.number() != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(m_message && m_message->broadcast()));
    }
    else if (oper.name() == YSTRING("name")) {
	if (oper.number() != 0)
	    return false;
	if (m_message)
	    ExpEvaluator::pushOne(stack,new ExpOperation(*m_message));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("retValue")) {
	switch (oper.number()) {
	    case 0:
		if (m_message)
		    ExpEvaluator::pushOne(stack,new ExpOperation(m_message->retValue()));
		else
		    ExpEvaluator::pushOne(stack,JsParser::nullClone());
		break;
	    case 1:
		{
		    ExpOperation* op = popValue(stack,context);
		    if (!op)
			return false;
		    if (m_message)
			m_message->retValue() = *op;
		    TelEngine::destruct(op);
		}
		break;
	    default:
		return false;
	}
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
    else if (oper.name() == YSTRING("install")) {
	ObjList args;
	if (extractArgs(stack,oper,context,args) < 2)
	    return false;
	ExpFunction* func = YOBJECT(ExpFunction,args[0]);
	if (!func)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[1]);
	ExpOperation* prio = static_cast<ExpOperation*>(args[2]);
	if (!name)
	    return false;
	unsigned int priority = 100;
	if (prio) {
	    if (prio->isInteger() && (prio->number() >= 0))
		priority = prio->number();
	    else
		return false;
	}
	JsHandler* h = new JsHandler(*name,priority,*func,context);
	m_handlers.append(h);
	Engine::install(h);
    }
    else if (oper.name() == YSTRING("uninstall")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
		m_handlers.clear();
		return true;
	    case 1:
		break;
	    default:
		return false;
	}
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	if (!name)
	    return false;
	m_handlers.remove(*name);
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

JsObject* JsMessage::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsMessage::runConstructor '%s'(%ld)",oper.name().c_str(),oper.number());
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	case 2:
	    break;
	default:
	    return 0;
    }
    ExpOperation* name = static_cast<ExpOperation*>(args[0]);
    ExpOperation* broad = static_cast<ExpOperation*>(args[1]);
    if (!name)
	return 0;
    if (!ref())
	return 0;
    Message* m = new Message(*name,0,broad && broad->valBoolean());
    JsMessage* obj = new JsMessage(m,mutex(),true);
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

void JsMessage::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Message")))
	addConstructor(params,"Message",new JsMessage(mtx));
}


bool JsHandler::received(Message& msg)
{
    DDebug(&__plugin,DebugAll,"JsHandler::received '%s'",c_str());
    if (!m_code)
	return false;
#ifdef DEBUG
    u_int64_t tm = Time::now();
#endif
    ScriptRun* runner = m_code->createRunner(m_context);
    if (!runner)
	return false;
    JsMessage* jm = new JsMessage(&msg,runner->context()->mutex(),false);
    jm->ref();
    ObjList args;
    args.append(new ExpWrapper(jm,"message"));
    ScriptRun::Status rval = runner->call(m_function.name(),args);
    jm->clearMsg();
    bool ok = false;
    if (ScriptRun::Succeeded == rval) {
	ExpOperation* op = ExpEvaluator::popOne(runner->stack());
	if (op) {
	    ok = op->valBoolean();
	    TelEngine::destruct(op);
	}
    }
    TelEngine::destruct(jm);
    TelEngine::destruct(runner);

#ifdef DEBUG
    tm = Time::now() - tm;
    Debug(&__plugin,DebugInfo,"Handler for %s ran for " FMT64U " usec",c_str(),tm);
#endif
    return ok;
}


bool JsFile::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsFile::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
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
    XDebug(DebugAll,"JsChannel::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("id")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(ja->id()));
    }
    else if (oper.name() == YSTRING("peerid")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja)
	    return false;
	RefPointer<CallEndpoint> cp = ja->locate();
	String id;
	if (cp)
	    cp->getPeerId(id);
	if (id)
	    ExpEvaluator::pushOne(stack,new ExpOperation(id));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("status")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja)
	    return false;
	RefPointer<CallEndpoint> cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	if (ch)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ch->status()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("direction")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja)
	    return false;
	RefPointer<CallEndpoint> cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	if (ch)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ch->direction()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("callTo")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja)
	    return false;
	RefPointer<CallEndpoint> cp = ja->locate();
    }
    else if (oper.name() == YSTRING("callJust")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja)
	    return false;
	RefPointer<CallEndpoint> cp = ja->locate();
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
    if (m_runner) {
	ScriptContext* context = m_runner->context();
	if (m_runner->callable("onUnload")) {
	    ScriptRun* runner = m_runner->code()->createRunner(context);
	    if (runner) {
		ObjList args;
		runner->call("onUnload",args);
		TelEngine::destruct(runner);
	    }
	}
	m_message = 0;
	if (context)
	    context->params().clearParams();
	TelEngine::destruct(m_runner);
    }
    else
	m_message = 0;
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
    if (ScriptRun::Invalid == m_runner->reset())
	return false;
    if (!m_runner->callable("onLoad"))
	return true;
    ScriptRun* runner = m_runner->code()->createRunner(m_runner->context());
    if (runner) {
	ObjList args;
	runner->call("onLoad",args);
	TelEngine::destruct(runner);
	return true;
    }
    return false;
}

bool JsAssist::setMsg(Message* msg)
{
    if (!m_runner)
	return false;
    ScriptContext* ctx = m_runner->context();
    if (!ctx)
	return false;
    Lock mylock(ctx->mutex());
    if (!mylock.locked())
	return false;
    if (m_message)
	return false;
    ObjList stack;
    ScriptContext* chan = YOBJECT(ScriptContext,ctx->getField(stack,YSTRING("Channel"),m_runner));
    if (!chan)
	return false;
    JsMessage* jsm = YOBJECT(JsMessage,chan->getField(stack,YSTRING("message"),m_runner));
    if (jsm)
	jsm->setMsg(msg,false);
    else {
	jsm = new JsMessage(msg,ctx->mutex(),false);
	ExpWrapper wrap(jsm,"message");
	if (!chan->runAssign(stack,wrap,m_runner))
	    return false;
    }
    m_message = jsm;
    m_handled = false;
    return true;
}

void JsAssist::clearMsg()
{
    Lock mylock((m_runner && m_runner->context()) ? m_runner->context()->mutex() : 0);
    if (!m_message)
	return;
    m_message->clearMsg();
    m_message = 0;
    if (mylock.locked()) {
	ObjList stack;
	ScriptContext* chan = YOBJECT(ScriptContext,m_runner->context()->getField(stack,YSTRING("Channel"),m_runner));
	if (chan) {
	    static const ExpWrapper s_undef(0,"message");
	    chan->runAssign(stack,s_undef,m_runner);
	}
    }
}

bool JsAssist::runScript(Message* msg, State newState)
{
    XDebug(&__plugin,DebugInfo,"JsAssist::runScript('%s') for '%s'",
	msg->c_str(),id().c_str());

    if (m_state >= Ended)
	return false;
#ifdef DEBUG
    u_int64_t tm = Time::now();
#endif
    if (!setMsg(msg)) {
	Debug(&__plugin,DebugWarn,"Failed to set message '%s' in '%s'",
	    msg->c_str(),id().c_str());
	return false;
    }

    switch (m_runner->execute()) {
	case ScriptRun::Invalid:
	case ScriptRun::Succeeded:
	    if (m_state < Ended)
		m_state = Ended;
	default:
	    break;
    }
    bool handled = m_handled;
    clearMsg();

#ifdef DEBUG
    tm = Time::now() - tm;
    Debug(&__plugin,DebugInfo,"Script for %s ran for " FMT64U " usec",id().c_str(),tm);
#endif
    return handled;
}

bool JsAssist::runFunction(const String& name, Message& msg)
{
    if (!(m_runner && m_runner->callable(name)))
	return false;
    DDebug(&__plugin,DebugInfo,"Running function %s in '%s'",name.c_str(),id().c_str());
#ifdef DEBUG
    u_int64_t tm = Time::now();
#endif
    ScriptRun* runner = __plugin.parser().createRunner(m_runner->context());
    if (!runner)
	return false;

    JsMessage* jm = new JsMessage(&msg,runner->context()->mutex(),false);
    jm->ref();
    ObjList args;
    args.append(new ExpWrapper(jm,"message"));
    ScriptRun::Status rval = runner->call(name,args);
    jm->clearMsg();
    TelEngine::destruct(jm);
    TelEngine::destruct(runner);

#ifdef DEBUG
    tm = Time::now() - tm;
    Debug(&__plugin,DebugInfo,"Call to %s ran for " FMT64U " usec",name.c_str(),tm);
#endif
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
    return runScript(&msg,PreRoute);
}

bool JsAssist::msgRoute(Message& msg)
{
    return runScript(&msg,Routing);
}

bool JsAssist::msgDisconnect(Message& msg, const String& reason)
{
    return runScript(&msg,ReRoute);
}


ObjList JsGlobal::s_globals;

JsGlobal::JsGlobal(const char* scriptName, const char* fileName)
    : NamedString(scriptName,fileName),
      m_fileTime(0), m_inUse(true)
{
    m_jsCode.basePath(s_basePath);
    m_jsCode.adjustPath(*this);
    DDebug(&__plugin,DebugAll,"Loading global Javascript '%s' from '%s'",name().c_str(),c_str());
    File::getFileTime(c_str(),m_fileTime);
    if (m_jsCode.parseFile(*this))
	Debug(&__plugin,DebugInfo,"Parsed '%s' script: %s",name().c_str(),c_str());
    else if (*this)
	Debug(&__plugin,DebugWarn,"Failed to parse '%s' script: %s",name().c_str(),c_str());
}

JsGlobal::~JsGlobal()
{
    DDebug(&__plugin,DebugAll,"Unloading global Javascript '%s'",name().c_str());
    if (m_jsCode.callable("onUnload")) {
	ScriptRun* runner = m_jsCode.createRunner(m_context);
	if (runner) {
	    ObjList args;
	    runner->call("onUnload",args);
	    TelEngine::destruct(runner);
	}
    }
    if (m_context)
	m_context->params().clearParams();
}

bool JsGlobal::fileChanged(const char* fileName) const
{
    if (m_jsCode.basePath() != s_basePath)
	return true;
    String tmp(fileName);
    m_jsCode.adjustPath(tmp);
    if (tmp != *this)
	return true;
    unsigned int time;
    File::getFileTime(tmp,time);
    return (time != m_fileTime);
}

void JsGlobal::markUnused()
{
    ListIterator iter(s_globals);
    while (JsGlobal* script = static_cast<JsGlobal*>(iter.get()))
	script->m_inUse = false;
}

void JsGlobal::freeUnused()
{
    ListIterator iter(s_globals);
    while (JsGlobal* script = static_cast<JsGlobal*>(iter.get()))
	if (!script->m_inUse)
	    s_globals.remove(script);
}

void JsGlobal::initScript(const String& scriptName, const String& fileName)
{
    if (fileName.null())
	return;
    JsGlobal* script = static_cast<JsGlobal*>(s_globals[scriptName]);
    if (script) {
	if (script->fileChanged(fileName)) {
	    s_globals.remove(script,false);
	    TelEngine::destruct(script);
	}
	else {
	    script->m_inUse = true;
	    return;
	}
    }
    script = new JsGlobal(scriptName,fileName);
    script->runMain();
    s_globals.append(script);
}

bool JsGlobal::runMain()
{
    ScriptRun* runner = m_jsCode.createRunner(m_context);
    if (!runner)
	return false;
    if (!m_context)
	m_context = runner->context();
    JsObject::initialize(runner->context());
    JsEngine::initialize(runner->context());
    JsMessage::initialize(runner->context());
    JsFile::initialize(runner->context());
    ScriptRun::Status st = runner->run();
    TelEngine::destruct(runner);
    return (ScriptRun::Succeeded == st);
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
	case Halt:
	    JsGlobal::unloadAll();
	    return false;
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
    tmp = cfg.getValue("general","routing");
    m_assistCode.adjustPath(tmp);
    if (m_assistCode.parseFile(tmp))
	Debug(this,DebugInfo,"Parsed routing script: %s",tmp.c_str());
    else if (tmp)
	Debug(this,DebugWarn,"Failed to parse script: %s",tmp.c_str());
    unlock();
    JsGlobal::markUnused();
    NamedList* sect = cfg.getSection("scripts");
    if (sect) {
	unsigned int len = sect->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = sect->getParam(i);
	    if (n)
		JsGlobal::initScript(n->name(),*n);
	}
    }
    JsGlobal::freeUnused();
}

void JsModule::init(int priority)
{
    ChanAssistList::init(priority);
    installRelay(Halt);
    installRelay(Route,priority);
    installRelay(Ringing,priority);
    installRelay(Answered,priority);
    Engine::install(new MessageRelay("call.preroute",this,Preroute,priority,name()));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
