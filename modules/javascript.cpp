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
#include <yatexml.h>

#define NATIVE_TITLE "[native code]"

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
    virtual void statusParams(String& str);
    virtual bool commandExecute(String& retVal, const String& line);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
private:
    bool evalContext(String& retVal, const String& cmd, ScriptContext* context = 0);
    JsParser m_assistCode;
};

INIT_PLUGIN(JsModule);

class JsMessage;

class JsAssist : public ChanAssist
{
public:
    enum State {
	NotStarted,
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
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgPreroute(Message& msg);
    virtual bool msgRoute(Message& msg);
    virtual bool msgDisconnect(Message& msg, const String& reason);
    bool init();
    inline State state() const
	{ return m_state; }
    inline const char* stateName() const
	{ return stateName(m_state); }
    inline void end()
	{ if (m_state < Ended) m_state = Ended; }
    inline JsMessage* message()
	{ return m_message; }
    inline void handled()
	{ m_handled = true; }
    inline ScriptContext* context()
	{ return m_runner ? m_runner->context() : 0; }
    Message* getMsg(ScriptRun* runner) const;
    static const char* stateName(State st);
private:
    bool runFunction(const String& name, Message& msg);
    bool runScript(Message* msg, State newState);
    bool setMsg(Message* msg);
    void clearMsg(bool fromChannel);
    ScriptRun* m_runner;
    State m_state;
    bool m_handled;
    RefPointer<JsMessage> m_message;
};

class JsGlobal : public NamedString
{
public:
    JsGlobal(const char* scriptName, const char* fileName, bool relPath = true);
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
    static bool reloadScript(const String& scriptName);
    inline static ObjList& globals()
	{ return s_globals; }
    inline static void unloadAll()
	{ s_globals.clear(); }
private:
    JsParser m_jsCode;
    RefPointer<ScriptContext> m_context;
    unsigned int m_fileTime;
    bool m_inUse;
    static ObjList s_globals;
};

class JsShared : public JsObject
{
    YCLASS(JsShared,JsObject)
public:
    inline JsShared(Mutex* mtx)
	: JsObject("Shared",mtx,true)
	{
	    params().addParam(new ExpFunction("inc"));
	    params().addParam(new ExpFunction("dec"));
	    params().addParam(new ExpFunction("get"));
	    params().addParam(new ExpFunction("set"));
	    params().addParam(new ExpFunction("clear"));
	    params().addParam(new ExpFunction("exists"));
	}
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
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
	    params().addParam(new ExpFunction("sleep"));
	    params().addParam(new ExpFunction("usleep"));
	    params().addParam(new ExpFunction("yield"));
	    params().addParam(new ExpFunction("idle"));
	    params().addParam(new ExpFunction("dump_r"));
	    params().addParam(new ExpFunction("print_r"));
	    params().addParam(new ExpWrapper(new JsShared(mtx),"shared"));
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
	    params().addParam(new ExpFunction("getColumn"));
	    params().addParam(new ExpFunction("getRow"));
	    params().addParam(new ExpFunction("getResult"));
	}
    virtual ~JsMessage()
	{
	    XDebug(&__plugin,DebugAll,"JsMessage::~JsMessage() [%p]",this);
	    if (m_owned)
		TelEngine::destruct(m_message);
	    if (Engine::exiting())
		while (m_handlers.remove(false))
		    ;
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
    void getColumn(ObjList& stack, const ExpOperation* col, GenObject* context);
    void getRow(ObjList& stack, const ExpOperation* row, GenObject* context);
    void getResult(ObjList& stack, const ExpOperation& row, const ExpOperation& col, GenObject* context);
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

class JsXML : public JsObject
{
    YCLASS(JsXML,JsObject)
public:
    inline JsXML(Mutex* mtx)
	: JsObject("XML",mtx,true),
	  m_xml(0)
	{
	    XDebug(DebugAll,"JsXML::JsXML() [%p]",this);
	    params().addParam(new ExpFunction("put"));
	    params().addParam(new ExpFunction("getOwner"));
	    params().addParam(new ExpFunction("getParent"));
	    params().addParam(new ExpFunction("unprefixedTag"));
	    params().addParam(new ExpFunction("getTag"));
	    params().addParam(new ExpFunction("getAttribute"));
	    params().addParam(new ExpFunction("setAttribute"));
	    params().addParam(new ExpFunction("removeAttribute"));
	    params().addParam(new ExpFunction("addChild"));
	    params().addParam(new ExpFunction("getChild"));
	    params().addParam(new ExpFunction("getChildren"));
	    params().addParam(new ExpFunction("clearChildren"));
	    params().addParam(new ExpFunction("addText"));
	    params().addParam(new ExpFunction("getText"));
	    params().addParam(new ExpFunction("getChildText"));
	    params().addParam(new ExpFunction("xmlText"));
	}
    inline JsXML(Mutex* mtx, XmlElement* xml, JsXML* owner = 0)
	: JsObject("XML",mtx,false),
	  m_xml(xml), m_owner(owner)
	{
	    XDebug(DebugAll,"JsXML::JsXML(%p,%p) [%p]",xml,owner,this);
	    if (owner) {
		JsObject* proto = YOBJECT(JsObject,owner->params().getParam(protoName()));
		if (proto && proto->ref())
		    params().addParam(new ExpWrapper(proto,protoName()));
	    }
	}
    virtual ~JsXML()
	{
	    if (m_owner) {
		m_xml = 0;
		m_owner = 0;
	    }
	    else
		TelEngine::destruct(m_xml);
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    inline JsXML* owner()
	{ return m_owner ? (JsXML*)m_owner : this; }
    inline const XmlElement* element() const
	{ return m_xml; }
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    static XmlElement* getXml(const String* obj, bool take);
    XmlElement* m_xml;
    RefPointer<JsXML> m_owner;
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
	    params().addParam(new ExpFunction("answered"));
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
    void callToRoute(ObjList& stack, const ExpOperation& oper, GenObject* context);
    void callToReRoute(ObjList& stack, const ExpOperation& oper, GenObject* context);
    JsAssist* m_assist;
};

class JsEngAsync : public ScriptAsync
{
    YCLASS(JsEngAsync,ScriptAsync)
public:
    enum Oper {
	AsyncSleep,
	AsyncUsleep,
	AsyncYield,
	AsyncIdle
    };
    inline JsEngAsync(ScriptRun* runner, Oper op, long int val = 0)
	: ScriptAsync(runner),
	  m_oper(op), m_val(val)
	{ XDebug(DebugAll,"JsEngAsync %d %ld",op,val); }
    virtual bool run();
private:
    Oper m_oper;
    long int m_val;
};

static String s_basePath;
static bool s_engineStop = false;
static bool s_allowAbort = false;
static bool s_allowTrace = false;
static bool s_allowLink = true;

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow) {
	s_engineStop = true;
	JsGlobal::unloadAll();
	return __plugin.unload();
    }
    return true;
}


bool JsEngAsync::run()
{
    switch (m_oper) {
	case AsyncSleep:
	    Thread::sleep(m_val);
	    break;
	case AsyncUsleep:
	    Thread::usleep(m_val);
	    break;
	case AsyncYield:
	    Thread::yield();
	    break;
	case AsyncIdle:
	    Thread::idle();
	    break;
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
	    int limit = s_allowAbort ? DebugFail : DebugGoOn;
	    if (level > DebugAll)
		level = DebugAll;
	    else if (level < limit)
		level = limit;
	    Debug(&__plugin,level,"%s",str.c_str());
	}
    }
    else if (oper.name() == YSTRING("sleep")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	long int val = op->valInteger();
	TelEngine::destruct(op);
	if (val < 0)
	    val = 0;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncSleep,val));
	runner->pause();
    }
    else if (oper.name() == YSTRING("usleep")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	long int val = op->valInteger();
	TelEngine::destruct(op);
	if (val < 0)
	    val = 0;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncUsleep,val));
	runner->pause();
    }
    else if (oper.name() == YSTRING("yield")) {
	if (oper.number() != 0)
	    return false;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncYield));
	runner->pause();
    }
    else if (oper.name() == YSTRING("idle")) {
	if (oper.number() != 0)
	    return false;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncIdle));
	runner->pause();
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


bool JsShared::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsShared::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("inc")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 1:
	    case 2:
		break;
	    default:
		return false;
	}
	ExpOperation* param = static_cast<ExpOperation*>(args[0]);
	ExpOperation* modulo = static_cast<ExpOperation*>(args[1]);
	int mod = 0;
	if (modulo && modulo->isInteger())
	    mod = modulo->number();
	if (mod > 1)
	    mod--;
	else
	    mod = 0;
	ExpEvaluator::pushOne(stack,new ExpOperation((long)Engine::sharedVars().inc(*param,mod)));
    }
    else if (oper.name() == YSTRING("dec")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 1:
	    case 2:
		break;
	    default:
		return false;
	}
	ExpOperation* param = static_cast<ExpOperation*>(args[0]);
	ExpOperation* modulo = static_cast<ExpOperation*>(args[1]);
	int mod = 0;
	if (modulo && modulo->isInteger())
	    mod = modulo->number();
	if (mod > 1)
	    mod--;
	else
	    mod = 0;
	ExpEvaluator::pushOne(stack,new ExpOperation((long)Engine::sharedVars().dec(*param,mod)));
    }
    else if (oper.name() == YSTRING("get")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param)
	    return false;
	String buf;
	Engine::sharedVars().get(*param,buf);
	TelEngine::destruct(param);
	ExpEvaluator::pushOne(stack,new ExpOperation(buf));
    }
    else if (oper.name() == YSTRING("set")) {
	if (oper.number() != 2)
	    return false;
	ExpOperation* val = popValue(stack,context);
	if (!val)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param) {
	    TelEngine::destruct(val);
	    return false;
	}
	Engine::sharedVars().set(*param,*val);
	TelEngine::destruct(param);
	TelEngine::destruct(val);
    }
    else if (oper.name() == YSTRING("clear")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param)
	    return false;
	Engine::sharedVars().clear(*param);
	TelEngine::destruct(param);
    }
    else if (oper.name() == YSTRING("exists")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(Engine::sharedVars().exists(*param)));
	TelEngine::destruct(param);
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


bool JsMessage::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsMessage::runAssign '%s'='%s'",oper.name().c_str(),oper.c_str());
    if (ScriptContext::hasField(stack,oper.name(),context))
	return JsObject::runAssign(stack,oper,context);
    if (!m_message)
	return false;
    if (JsParser::isUndefined(oper))
	m_message->clearParam(oper.name());
    else
	m_message->setParam(new NamedString(oper.name(),oper));
    return true;
}

bool JsMessage::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsMessage::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
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
    else if (oper.name() == YSTRING("getColumn")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
	    case 1:
		break;
	    default:
		return false;
	}
	getColumn(stack,static_cast<ExpOperation*>(args[0]),context);
    }
    else if (oper.name() == YSTRING("getRow")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
	    case 1:
		break;
	    default:
		return false;
	}
	getRow(stack,static_cast<ExpOperation*>(args[0]),context);
    }
    else if (oper.name() == YSTRING("getResult")) {
	ObjList args;
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	if (!(args[0] && args[1]))
	    return false;
	getResult(stack,*static_cast<ExpOperation*>(args[0]),
	    *static_cast<ExpOperation*>(args[1]),context);
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
	const ExpFunction* func = YOBJECT(ExpFunction,args[0]);
	if (!func) {
	    JsFunction* jsf = YOBJECT(JsFunction,args[0]);
	    if (jsf)
		func = jsf->getFunc();
	}
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
	ExpOperation* filterName = static_cast<ExpOperation*>(args[3]);
	ExpOperation* filterValue = static_cast<ExpOperation*>(args[4]);
	if (filterName && filterValue && *filterName)
	    h->setFilter(*filterName,*filterValue);
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

void JsMessage::getColumn(ObjList& stack, const ExpOperation* col, GenObject* context)
{
    Array* arr = m_message ? YOBJECT(Array,m_message->userData()) : 0;
    if (arr && arr->getRows()) {
	int rows = arr->getRows() - 1;
	int cols = arr->getColumns();
	if (col) {
	    // [ val1, val2, val3 ]
	    int idx = -1;
	    if (col->isInteger())
		idx = col->number();
	    else {
		for (int i = 0; i < cols; i++) {
		    GenObject* o = arr->get(i,0);
		    if (o && (o->toString() == *col)) {
			idx = i;
			break;
		    }
		}
	    }
	    if (idx >= 0 && idx < cols) {
		JsArray* jsa = new JsArray(mutex());
		for (int r = 1; r <= rows; r++) {
		    GenObject* o = arr->get(idx,r);
		    if (o)
			jsa->push(new ExpOperation(o->toString()));
		    else
			jsa->push(JsParser::nullClone());
		}
		ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"column"));
		return;
	    }
	}
	else {
	    // { col1: [ val11, val12, val13], col2: [ val21, val22, val23 ] }
	    JsObject* jso = new JsObject("Object",mutex());
	    for (int c = 0; c < cols; c++) {
		const String* name = YOBJECT(String,arr->get(c,0));
		if (TelEngine::null(name))
		    continue;
		JsArray* jsa = new JsArray(mutex());
		for (int r = 1; r <= rows; r++) {
		    GenObject* o = arr->get(c,r);
		    if (o)
			jsa->push(new ExpOperation(o->toString()));
		    else
			jsa->push(JsParser::nullClone());
		}
		jso->params().setParam(new ExpWrapper(jsa,*name));
	    }
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"columns"));
	    return;
	}
    }
    ExpEvaluator::pushOne(stack,JsParser::nullClone());
}

void JsMessage::getRow(ObjList& stack, const ExpOperation* row, GenObject* context)
{
    Array* arr = m_message ? YOBJECT(Array,m_message->userData()) : 0;
    if (arr && arr->getRows()) {
	int rows = arr->getRows() - 1;
	int cols = arr->getColumns();
	if (row) {
	    // { col1: val1, col2: val2 }
	    if (row->isInteger()) {
		int idx = row->number() + 1;
		if (idx > 0 && idx <= rows) {
		    JsObject* jso = new JsObject("Object",mutex());
		    for (int c = 0; c < cols; c++) {
			const String* name = YOBJECT(String,arr->get(c,0));
			if (TelEngine::null(name))
			    continue;
			GenObject* o = arr->get(c,idx);
			if (o)
			    jso->params().setParam(new ExpOperation(o->toString(),*name));
			else
			    jso->params().setParam((JsParser::nullClone(*name)));
		    }
		    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"row"));
		    return;
		}
	    }
	}
	else {
	    // [ { col1: val11, col2: val12 }, { col1: val21, col2: val22 } ]
	    JsArray* jsa = new JsArray(mutex());
	    for (int r = 1; r <= rows; r++) {
		JsObject* jso = new JsObject("Object",mutex());
		for (int c = 0; c < cols; c++) {
		    const String* name = YOBJECT(String,arr->get(c,0));
		    if (TelEngine::null(name))
			continue;
		    GenObject* o = arr->get(c,r);
		    if (o)
			jso->params().setParam(new ExpOperation(o->toString(),*name));
		    else
			jso->params().setParam((JsParser::nullClone(*name)));
		}
		jsa->push(new ExpWrapper(jso));
	    }
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"rows"));
	    return;
	}
    }
    ExpEvaluator::pushOne(stack,JsParser::nullClone());
}

void JsMessage::getResult(ObjList& stack, const ExpOperation& row, const ExpOperation& col, GenObject* context)
{
    Array* arr = m_message ? YOBJECT(Array,m_message->userData()) : 0;
    if (arr && arr->getRows() && row.isInteger()) {
	int rows = arr->getRows() - 1;
	int cols = arr->getColumns();
	int r = row.number();
	if (r >= 0 && r < rows) {
	    int c = -1;
	    if (col.isInteger())
		c = col.number();
	    else {
		for (int i = 0; i < cols; i++) {
		    GenObject* o = arr->get(i,0);
		    if (o && (o->toString() == col)) {
			c = i;
			break;
		    }
		}
	    }
	    if (c >= 0 && c < cols) {
		GenObject* o = arr->get(c,r + 1);
		if (o) {
		    ExpEvaluator::pushOne(stack,new ExpOperation(o->toString()));
		    return;
		}
	    }
	}
    }
    ExpEvaluator::pushOne(stack,JsParser::nullClone());
}

JsObject* JsMessage::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsMessage::runConstructor '%s'(%ld)",oper.name().c_str(),oper.number());
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
    if (s_engineStop || !m_code)
	return false;
    DDebug(&__plugin,DebugInfo,"Running %s(message) handler for '%s'",
	m_function.name().c_str(),c_str());
#ifdef DEBUG
    u_int64_t tm = Time::now();
#endif
    ScriptRun* runner = m_code->createRunner(m_context,NATIVE_TITLE);
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
    Debug(&__plugin,DebugInfo,"Handler for '%s' ran for " FMT64U " usec",c_str(),tm);
#endif
    return ok;
}


bool JsFile::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsFile::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
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


bool JsXML::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsXML::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
    ObjList args;
    int argc = extractArgs(stack,oper,context,args);
    if (oper.name() == YSTRING("put")) {
	if (argc < 2 || argc > 3)
	    return false;
	ScriptContext* list = YOBJECT(ScriptContext,static_cast<ExpOperation*>(args[0]));
	ExpOperation* name = static_cast<ExpOperation*>(args[1]);
	ExpOperation* text = static_cast<ExpOperation*>(args[2]);
	if (!name || !list || !m_xml)
	    return false;
	NamedList* params = list->nativeParams();
	if (!params)
	    params = &list->params();
	params->clearParam(*name);
	String txt;
	if (text && text->valBoolean())
	    m_xml->toString(txt);
	if (!text || (text->valInteger() != 1))
	    params->addParam(new NamedPointer(*name,new XmlElement(*m_xml),txt));
	else
	    params->addParam(*name,txt);
    }
    else if (oper.name() == YSTRING("getOwner")) {
	if (argc != 0)
	    return false;
	if (m_owner && m_owner->ref())
	    ExpEvaluator::pushOne(stack,new ExpWrapper(m_owner));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getParent")) {
	if (argc != 0)
	    return false;
	XmlElement* xml = m_xml ? m_xml->parent() : 0;
	if (xml)
	    ExpEvaluator::pushOne(stack,new ExpWrapper(new JsXML(mutex(),xml,owner())));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("unprefixedTag")) {
	if (argc != 0)
	    return false;
	if (m_xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_xml->unprefixedTag(),m_xml->unprefixedTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getTag")) {
	if (argc != 0)
	    return false;
	if (m_xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_xml->getTag(),m_xml->getTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getAttribute")) {
	if (argc != 1)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	if (!name)
	    return false;
	const String* attr = 0;
	if (m_xml)
	    attr = m_xml->getAttribute(*name);
	if (attr)
	    ExpEvaluator::pushOne(stack,new ExpOperation(*attr,name->name()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("setAttribute")) {
	if (!m_xml)
	    return false;
	if (argc != 2)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	ExpOperation* val = static_cast<ExpOperation*>(args[1]);
	if (!name || !val)
	    return false;
	if (JsParser::isUndefined(*val) || JsParser::isNull(*val))
	    m_xml->removeAttribute(*name);
	else
	    m_xml->setAttribute(*name,*val);
    }
    else if (oper.name() == YSTRING("removeAttribute")) {
	if (argc != 1)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	if (!name)
	    return false;
	if (m_xml)
	    m_xml->removeAttribute(*name);
    }
    else if (oper.name() == YSTRING("addChild")) {
	if (argc < 1 || argc > 2)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	ExpOperation* val = static_cast<ExpOperation*>(args[1]);
	if (!name)
	    return false;
	if (!m_xml)
	    return false;
	JsArray* jsa = YOBJECT(JsArray,name);
	if (jsa) {
	    for (long i = 0; i < jsa->length(); i++) {
		String n((unsigned int)i);
		JsXML* x = YOBJECT(JsXML,jsa->getField(stack,n,context));
		if (x && x->element()) {
		    XmlElement* xml = new XmlElement(*x->element());
		    if (XmlSaxParser::NoError != m_xml->addChild(xml)) {
			TelEngine::destruct(xml);
			return false;
		    }
		}
	    }
	    return true;
	}
	XmlElement* xml = 0;
	JsXML* x = YOBJECT(JsXML,name);
	if (x && x->element())
	    xml = new XmlElement(*x->element());
	else
	    xml = new XmlElement(name->c_str());
	if (val)
	    xml->addText(*val);
	if (XmlSaxParser::NoError == m_xml->addChild(xml))
	    ExpEvaluator::pushOne(stack,new ExpWrapper(new JsXML(mutex(),xml,owner())));
	else {
	    TelEngine::destruct(xml);
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
	}
    }
    else if (oper.name() == YSTRING("getChild")) {
	if (argc > 2)
	    return false;
	XmlElement* xml = 0;
	if (m_xml)
	    xml = m_xml->findFirstChild(static_cast<ExpOperation*>(args[0]),static_cast<ExpOperation*>(args[1]));
	if (xml)
	    ExpEvaluator::pushOne(stack,new ExpWrapper(new JsXML(mutex(),xml,owner())));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getChildren")) {
	if (argc > 2)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	ExpOperation* ns = static_cast<ExpOperation*>(args[1]);
	XmlElement* xml = 0;
	if (m_xml)
	    xml = m_xml->findFirstChild(name,ns);
	if (xml) {
	    JsArray* jsa = new JsArray(mutex());
	    while (xml) {
		jsa->push(new ExpWrapper(new JsXML(mutex(),xml,owner())));
		xml = m_xml->findNextChild(xml,name,ns);
	    }
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"children"));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("clearChildren")) {
	if (argc)
	    return false;
	if (m_xml)
	    m_xml->clearChildren();
    }
    else if (oper.name() == YSTRING("addText")) {
	if (argc != 1)
	    return false;
	ExpOperation* text = static_cast<ExpOperation*>(args[0]);
	if (!m_xml || !text)
	    return false;
	if (!TelEngine::null(text))
	    m_xml->addText(*text);
    }
    else if (oper.name() == YSTRING("getText")) {
	if (argc)
	    return false;
	if (m_xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_xml->getText(),m_xml->unprefixedTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getChildText")) {
	if (argc > 2)
	    return false;
	XmlElement* xml = 0;
	if (m_xml)
	    xml = m_xml->findFirstChild(static_cast<ExpOperation*>(args[0]),static_cast<ExpOperation*>(args[1]));
	if (xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(xml->getText(),xml->unprefixedTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("xmlText")) {
	if (argc)
	    return false;
	if (m_xml) {
	    ExpOperation* op = new ExpOperation("",m_xml->unprefixedTag());
	    m_xml->toString(*op);
	    ExpEvaluator::pushOne(stack,op);
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

JsObject* JsXML::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsXML::runConstructor '%s'(%ld) [%p]",oper.name().c_str(),oper.number(),this);
    JsXML* obj = 0;
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	    {
		ExpOperation* text = static_cast<ExpOperation*>(args[0]);
		XmlElement* xml = getXml(text,false);
		if (!xml)
		    return 0;
		obj = new JsXML(mutex(),xml);
	    }
	    break;
	case 2:
	    {
		JsObject* jso = YOBJECT(JsObject,args[0]);
		ExpOperation* name = static_cast<ExpOperation*>(args[1]);
		if (!jso || !name)
		    return 0;
		XmlElement* xml = getXml(jso->getField(stack,*name,context),false);
		if (!xml)
		    return 0;
		obj = new JsXML(mutex(),xml);
	    }
	    break;
	default:
	    return 0;
    }
    if (!obj)
	return 0;
    if (!ref()) {
	TelEngine::destruct(obj);
	return 0;
    }
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

XmlElement* JsXML::getXml(const String* obj, bool take)
{
    if (!obj)
	return 0;
    XmlElement* xml = 0;
    NamedPointer* nptr = YOBJECT(NamedPointer,obj);
    if (nptr) {
	xml = YOBJECT(XmlElement,nptr);
	if (xml) {
	    if (take) {
		nptr->takeData();
		return xml;
	    }
	    return new XmlElement(*xml);
	}
    }
    XmlDomParser parser;
    if (!(parser.parse(obj->c_str()) || parser.completeText()))
	return 0;
    if (!(parser.document() && parser.document()->root(true)))
	return 0;
    return new XmlElement(*parser.document()->root());
}

void JsXML::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("XML")))
	addConstructor(params,"XML",new JsXML(mtx));
}


bool JsChannel::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsChannel::runNative '%s'(%ld)",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("id")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ja->id()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
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
	RefPointer<CallEndpoint> cp;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	if (ch)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ch->status()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("direction")) {
	if (oper.number())
	    return false;
	RefPointer<CallEndpoint> cp;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	if (ch)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ch->direction()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("answered")) {
	if (oper.number())
	    return false;
	RefPointer<CallEndpoint> cp;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	ExpEvaluator::pushOne(stack,new ExpOperation(ch && ch->isAnswered()));
    }
    else if (oper.name() == YSTRING("answer")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (ja) {
	    Message* m = new Message("call.answered");
	    m->addParam("targetid",ja->id());
	    Engine::enqueue(m);
	}
    }
    else if (oper.name() == YSTRING("hangup")) {
	if (oper.number() > 1)
	    return false;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	ExpOperation* op = popValue(stack,context);
	RefPointer<JsAssist> ja = m_assist;
	if (ja) {
	    Message* m = new Message("call.drop");
	    m->addParam("id",ja->id());
	    if (op && !op->null()) {
		m->addParam("reason",*op);
		// there may be a race between chan.disconnected and call.drop so set in both
		Message* msg = ja->getMsg(runner);
		if (msg)
		    msg->setParam("reason",*op);
	    }
	    ja->end();
	    Engine::enqueue(m);
	}
	TelEngine::destruct(op);
	if (runner)
	    runner->pause();
    }
    else if (oper.name() == YSTRING("callTo") || oper.name() == YSTRING("callJust")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja) {
	    TelEngine::destruct(op);
	    return false;
	}
	switch (ja->state()) {
	    case JsAssist::Routing:
		callToRoute(stack,*op,context);
		break;
	    case JsAssist::ReRoute:
		callToReRoute(stack,*op,context);
		break;
	    default:
		break;
	}
	TelEngine::destruct(op);
	if (oper.name() == YSTRING("callJust"))
	    ja->end();
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsChannel::callToRoute(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ScriptRun* runner = YOBJECT(ScriptRun,context);
    if (!runner)
	return;
    Message* msg = m_assist->getMsg(YOBJECT(ScriptRun,context));
    if (!msg) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToRoute(): No message!");
	return;
    }
    if (oper.null() || JsParser::isNull(oper) || JsParser::isUndefined(oper)) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToRoute(): Invalid target!");
	return;
    }
    msg->retValue() = oper;
    m_assist->handled();
    runner->pause();
}

void JsChannel::callToReRoute(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ScriptRun* runner = YOBJECT(ScriptRun,context);
    if (!runner)
	return;
    Message* msg = m_assist->getMsg(YOBJECT(ScriptRun,context));
    if (!msg) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToReRoute(): No message!");
	return;
    }
    Channel* chan = YOBJECT(Channel,msg->userData());
    if (!chan) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToReRoute(): No channel!");
	return;
    }
    String target = oper;
    target.trimSpaces();
    if (target.null() || JsParser::isNull(oper) || JsParser::isUndefined(oper)) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToRoute(): Invalid target!");
	return;
    }
    Message* m = chan->message("call.execute",false,true);
    m->setParam("callto",target);
    // copy params except those already set
    unsigned int n = msg->length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* p = msg->getParam(i);
	if (p && !m->getParam(p->name()))
	    m->addParam(p->name(),*p);
    }
    Engine::enqueue(m);
    m_assist->handled();
    runner->pause();
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


#define MKSTATE(x) { #x, JsAssist::x }
static const TokenDict s_states[] = {
    MKSTATE(NotStarted),
    MKSTATE(Routing),
    MKSTATE(ReRoute),
    MKSTATE(Ended),
    MKSTATE(Hangup),
    { 0, 0 }
};

JsAssist::~JsAssist()
{
    if (m_runner) {
	ScriptContext* context = m_runner->context();
	if (m_runner->callable("onUnload")) {
	    ScriptRun* runner = m_runner->code()->createRunner(context,NATIVE_TITLE);
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

const char* JsAssist::stateName(State st)
{
    return lookup(st,s_states,"???");
}

bool JsAssist::init()
{
    if (!m_runner)
	return false;
    ScriptContext* ctx = m_runner->context();
    JsObject::initialize(ctx);
    JsEngine::initialize(ctx);
    JsChannel::initialize(ctx,this);
    JsMessage::initialize(ctx);
    JsFile::initialize(ctx);
    JsXML::initialize(ctx);
    if (ScriptRun::Invalid == m_runner->reset(true))
	return false;
    ScriptContext* chan = YOBJECT(ScriptContext,ctx->getField(m_runner->stack(),YSTRING("Channel"),m_runner));
    if (chan) {
	JsMessage* jsm = YOBJECT(JsMessage,chan->getField(m_runner->stack(),YSTRING("message"),m_runner));
	if (!jsm) {
	    jsm = new JsMessage(0,ctx->mutex(),false);
	    ExpWrapper wrap(jsm,"message");
	    if (!chan->runAssign(m_runner->stack(),wrap,m_runner))
		return false;
	}
	if (jsm && jsm->ref()) {
	    JsObject* cc = JsObject::buildCallContext(ctx->mutex(),jsm);
	    jsm->ref();
	    cc->params().setParam(new ExpWrapper(jsm,"message"));
	    ExpEvaluator::pushOne(m_runner->stack(),new ExpWrapper(cc,cc->toString(),true));
	}
    }
    if (!m_runner->callable("onLoad"))
	return true;
    ScriptRun* runner = m_runner->code()->createRunner(m_runner->context(),NATIVE_TITLE);
    if (runner) {
	ObjList args;
	runner->call("onLoad",args);
	TelEngine::destruct(runner);
	return true;
    }
    return false;
}

Message* JsAssist::getMsg(ScriptRun* runner) const
{
    if (!runner)
	runner = m_runner;
    if (!runner)
	return 0;
    ScriptContext* ctx = runner->context();
    if (!ctx)
	return 0;
    ObjList stack;
    ScriptContext* chan = YOBJECT(ScriptContext,ctx->getField(stack,YSTRING("Channel"),runner));
    if (!chan)
	return 0;
    JsMessage* jsm = YOBJECT(JsMessage,chan->getField(stack,YSTRING("message"),runner));
    if (!jsm)
	return 0;
    return static_cast<Message*>(jsm->nativeParams());
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
    else
	return false;
    m_message = jsm;
    m_handled = false;
    return true;
}

void JsAssist::clearMsg(bool fromChannel)
{
    Lock mylock((m_runner && m_runner->context()) ? m_runner->context()->mutex() : 0);
    if (!m_message)
	return;
    m_message->clearMsg();
    m_message = 0;
    if (fromChannel && mylock.locked()) {
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
    XDebug(&__plugin,DebugInfo,"JsAssist::runScript('%s') for '%s' in state %s",
	msg->c_str(),id().c_str(),stateName());

    if (m_state >= Ended)
	return false;
    if (m_state < newState)
	m_state = newState;
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
    clearMsg(m_state >= Ended);

#ifdef DEBUG
    tm = Time::now() - tm;
    Debug(&__plugin,DebugInfo,"Script for '%s' ran for " FMT64U " usec",id().c_str(),tm);
#endif
    return handled;
}

bool JsAssist::runFunction(const String& name, Message& msg)
{
    if (!(m_runner && m_runner->callable(name)))
	return false;
    DDebug(&__plugin,DebugInfo,"Running function %s(message) in '%s' state %s",
	name.c_str(),id().c_str(),stateName());
#ifdef DEBUG
    u_int64_t tm = Time::now();
#endif
    ScriptRun* runner = __plugin.parser().createRunner(m_runner->context(),NATIVE_TITLE);
    if (!runner)
	return false;

    JsMessage* jm = new JsMessage(&msg,runner->context()->mutex(),false);
    jm->ref();
    ObjList args;
    args.append(new ExpWrapper(jm,"message"));
    ScriptRun::Status rval = runner->call(name,args);
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
    Debug(&__plugin,DebugInfo,"Call to %s() ran for " FMT64U " usec",name.c_str(),tm);
#endif
    return ok;
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

bool JsAssist::msgRinging(Message& msg)
{
    return runFunction("onRinging",msg);
}

bool JsAssist::msgAnswered(Message& msg)
{
    return runFunction("onAnswered",msg);
}

bool JsAssist::msgPreroute(Message& msg)
{
    return runFunction("onPreroute",msg);
}

bool JsAssist::msgRoute(Message& msg)
{
    return runScript(&msg,Routing);
}

bool JsAssist::msgDisconnect(Message& msg, const String& reason)
{
    return runFunction("onDisconnected",msg) || runScript(&msg,ReRoute);
}


ObjList JsGlobal::s_globals;

JsGlobal::JsGlobal(const char* scriptName, const char* fileName, bool relPath)
    : NamedString(scriptName,fileName),
      m_fileTime(0), m_inUse(true)
{
    m_jsCode.basePath(s_basePath);
    if (relPath)
	m_jsCode.adjustPath(*this);
    m_jsCode.link(s_allowLink);
    m_jsCode.trace(s_allowTrace);
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
	ScriptRun* runner = m_jsCode.createRunner(m_context,NATIVE_TITLE);
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
    Lock mylock(__plugin);
    ListIterator iter(s_globals);
    while (JsGlobal* script = static_cast<JsGlobal*>(iter.get()))
	if (!script->m_inUse) {
	    s_globals.remove(script,false);
	    mylock.drop();
	    TelEngine::destruct(script);
	    mylock.acquire(__plugin);
	}
}

void JsGlobal::initScript(const String& scriptName, const String& fileName)
{
    if (fileName.null())
	return;
    Lock mylock(__plugin);
    JsGlobal* script = static_cast<JsGlobal*>(s_globals[scriptName]);
    if (script) {
	if (script->fileChanged(fileName)) {
	    s_globals.remove(script,false);
	    mylock.drop();
	    TelEngine::destruct(script);
	    mylock.acquire(__plugin);
	}
	else {
	    script->m_inUse = true;
	    return;
	}
    }
    script = new JsGlobal(scriptName,fileName);
    s_globals.append(script);
    mylock.drop();
    script->runMain();
}

bool JsGlobal::reloadScript(const String& scriptName)
{
    if (scriptName.null())
	return false;
    Lock mylock(__plugin);
    JsGlobal* script = static_cast<JsGlobal*>(s_globals[scriptName]);
    if (!script)
	return false;
    String fileName = *script;
    if (fileName.null())
	return false;
    s_globals.remove(script,false);
    mylock.drop();
    TelEngine::destruct(script);
    mylock.acquire(__plugin);
    script = new JsGlobal(scriptName,fileName,false);
    s_globals.append(script);
    mylock.drop();
    return script->runMain();
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
    JsXML::initialize(runner->context());
    ScriptRun::Status st = runner->run();
    TelEngine::destruct(runner);
    return (ScriptRun::Succeeded == st);
}


static const char* s_cmds[] = {
    "info",
    "eval",
    "reload",
    0
};

static const char* s_cmdsLine = "  javascript {info|eval[=context] instructions...|reload script}";


JsModule::JsModule()
    : ChanAssistList("javascript",true)
{
    Output("Loaded module Javascript");
}

JsModule::~JsModule()
{
    Output("Unloading module Javascript");
}

void JsModule::statusParams(String& str)
{
    lock();
    str << "globals=" << JsGlobal::globals().count() << ",routing=" << calls().count();
    unlock();
}

bool JsModule::commandExecute(String& retVal, const String& line)
{
    String cmd = line;
    if (!cmd.startSkip(name()))
	return false;
    cmd.trimSpaces();

    if (cmd.null() || cmd == YSTRING("info")) {
	retVal.clear();
	lock();
	ListIterator iter(JsGlobal::globals());
	while (JsGlobal* script = static_cast<JsGlobal*>(iter.get()))
	    retVal << script->name() << " = " << *script << "\r\n";
	iter.assign(calls());
	while (JsAssist* assist = static_cast<JsAssist*>(iter.get()))
	    retVal << assist->id() << ": " << assist->stateName() << "\r\n";
	unlock();
	return true;
    }

    if (cmd.startSkip("reload") && cmd.trimSpaces())
	return JsGlobal::reloadScript(cmd);

    if (cmd.startSkip("eval=",false) && cmd.trimSpaces()) {
	String scr;
	cmd.extractTo(" ",scr).trimSpaces();
	if (scr.null() || cmd.null())
	    return false;
	Lock mylock(this);
	JsGlobal* script = static_cast<JsGlobal*>(JsGlobal::globals()[scr]);
	if (script) {
	    RefPointer<ScriptContext> ctxt = script->context();
	    mylock.drop();
	    return evalContext(retVal,cmd,ctxt);
	}
	JsAssist* assist = static_cast<JsAssist*>(calls()[scr]);
	if (assist) {
	    RefPointer<ScriptContext> ctxt = assist->context();
	    mylock.drop();
	    return evalContext(retVal,cmd,ctxt);
	}
	retVal << "Cannot find script context: " << scr << "\n\r";
	return true;
    }

    if (cmd.startSkip("eval") && cmd.trimSpaces())
	return evalContext(retVal,cmd);

    return false;
}

bool JsModule::evalContext(String& retVal, const String& cmd, ScriptContext* context)
{
    JsParser parser;
    parser.basePath(s_basePath);
    parser.link(s_allowLink);
    parser.trace(s_allowTrace);
    if (!parser.parse(cmd)) {
	retVal << "parsing failed\r\n";
	return true;
    }
    ScriptRun* runner = parser.createRunner(context,"[command line]");
    if (!context) {
	JsObject::initialize(runner->context());
	JsEngine::initialize(runner->context());
	JsMessage::initialize(runner->context());
	JsFile::initialize(runner->context());
	JsXML::initialize(runner->context());
    }
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
	itemComplete(msg.retValue(),name(),partWord);
    else if (partLine == name()) {
	static const String s_eval("eval=");
	if (partWord.startsWith(s_eval)) {
	    lock();
	    ListIterator iter(JsGlobal::globals());
	    while (JsGlobal* script = static_cast<JsGlobal*>(iter.get()))
		if (!script->name().null())
		    itemComplete(msg.retValue(),s_eval + script->name(),partWord);
	    iter.assign(calls());
	    while (JsAssist* assist = static_cast<JsAssist*>(iter.get()))
		itemComplete(msg.retValue(),s_eval + assist->id(),partWord);
	    unlock();
	    return true;
	}
	for (const char** list = s_cmds; *list; list++)
	    itemComplete(msg.retValue(),*list,partWord);
	return true;
    }
    else if (partLine == YSTRING("javascript reload")) {
	lock();
	ListIterator iter(JsGlobal::globals());
	while (JsGlobal* script = static_cast<JsGlobal*>(iter.get())) {
	    if (!script->name().null())
		itemComplete(msg.retValue(),script->name(),partWord);
	}
	unlock();
	return true;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

bool JsModule::received(Message& msg, int id)
{
    switch (id) {
	case Help:
	    {
		const String* line = msg.getParam("line");
		if (TelEngine::null(line)) {
		    msg.retValue() << s_cmdsLine << "\r\n";
		    return false;
		}
		if (name() != *line)
		    return false;
	    }
	    msg.retValue() << s_cmdsLine << "\r\n";
	    msg.retValue() << "Controls and executes Javascript commands\r\n";
	    return true;
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
			return ca && ca->msgRinging(msg);
		    case Answered:
			return ca && ca->msgAnswered(msg);
		}
	    }
	    break;
	case Halt:
	    s_engineStop = true;
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
    ScriptRun* runner = m_assistCode.createRunner(0,NATIVE_TITLE);
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
    installRelay(Help);
    Configuration cfg(Engine::configFile("javascript"));
    String tmp = Engine::sharedPath();
    tmp << Engine::pathSeparator() << "scripts";
    tmp = cfg.getValue("general","scripts_dir",tmp);
    if (!tmp.endsWith(Engine::pathSeparator()))
	tmp += Engine::pathSeparator();
    s_basePath = tmp;
    s_allowAbort = cfg.getBoolValue("general","allow_abort");
    s_allowTrace = cfg.getBoolValue("general","allow_trace");
    s_allowLink = cfg.getBoolValue("general","allow_link",true);
    lock();
    m_assistCode.clear();
    m_assistCode.basePath(tmp);
    m_assistCode.link(s_allowLink);
    m_assistCode.trace(s_allowTrace);
    tmp = cfg.getValue("general","routing");
    m_assistCode.adjustPath(tmp);
    if (m_assistCode.parseFile(tmp))
	Debug(this,DebugInfo,"Parsed routing script: %s",tmp.c_str());
    else if (tmp)
	Debug(this,DebugWarn,"Failed to parse script: %s",tmp.c_str());
    JsGlobal::markUnused();
    unlock();
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
