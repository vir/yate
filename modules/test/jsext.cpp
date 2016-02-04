/**
 * jsext.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Javascript extensions test
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2014 Null Team
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

#include <yatengine.h>
#include <yatescript.h>

using namespace TelEngine;

class JsExtObj : public JsObject
{
    YCLASS(JsExtObj,JsObject)
public:
    inline JsExtObj(Mutex* mtx)
	: JsObject("ExtObj",mtx,true)
	{
	    Debug(DebugAll,"JsExtObj::JsExtObj(%p) [%p]",mtx,this);
	}
    inline JsExtObj(Mutex* mtx, const char* val)
	: JsObject("ExtObj",mtx,true),
	  m_val(val)
	{
	    Debug(DebugAll,"JsExtObj::JsExtObj(%p,'%s') [%p]",mtx,val,this);
	    params().addParam(new ExpFunction("test"));
	}
    virtual ~JsExtObj()
	{
	    Debug(DebugAll,"JsExtObj::~JsExtObj() [%p]",this);
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    String m_val;
};


class JsExtHandler : public MessageHandler
{
public:
    JsExtHandler()
	: MessageHandler("script.init",90,"jsext")
	{ }
    virtual bool received(Message& msg);
};

class JsExtPlugin : public Plugin
{
public:
    JsExtPlugin();
    virtual void initialize();
private:
    JsExtHandler* m_handler;
};


JsObject* JsExtObj::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    Debug(DebugAll,"JsExtObj::runConstructor '%s'(" FMT64 ") [%p]",oper.name().c_str(),oper.number(),this);
    const char* val = 0;
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	    val = static_cast<ExpOperation*>(args[0])->c_str();
	    // fall through
	case 0:
	    return new JsExtObj(mutex(),val);
	default:
	    return 0;
    }
}

void JsExtObj::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("ExtObj")))
	addConstructor(params,"ExtObj",new JsExtObj(mtx));
    else
	Debug(DebugInfo,"An ExtObj already exists, nothing to do");
}

bool JsExtObj::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("test")) {
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	String tmp;
	tmp << "ExtObj: '" << m_val << "' argc=" << argc;
	for (int i = 0; i < argc; i++) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[i]);
	    tmp << " '" << *op << "'";
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(tmp));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


static const Regexp s_libs("\\(^\\|,\\)jsext\\($\\|,\\)");
static const Regexp s_objs("\\(^\\|,\\)ExtObj\\($\\|,\\)");

bool JsExtHandler::received(Message& msg)
{
    ScriptContext* ctx = YOBJECT(ScriptContext,msg.userData());
    const String& lang = msg[YSTRING("language")];
    Debug(DebugInfo,"Received script.init, language: %s, context: %p",lang.c_str(),ctx);
    if ((lang && (lang != YSTRING("javascript"))) || !ctx)
	return false;
    bool ok = msg.getBoolValue(YSTRING("startup"))
	|| s_libs.matches(msg.getValue(YSTRING("libraries")))
	|| s_objs.matches(msg.getValue(YSTRING("objects")));
    if (ok)
	JsExtObj::initialize(ctx);
    return ok;
}


JsExtPlugin::JsExtPlugin()
    : Plugin("jsext",true), m_handler(0)
{
    Output("Hello, I am module JsExtPlugin");
}

void JsExtPlugin::initialize()
{
    Output("Initializing module JsExtPlugin");
    if (!m_handler)
	Engine::install((m_handler = new JsExtHandler));
}

INIT_PLUGIN(JsExtPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
