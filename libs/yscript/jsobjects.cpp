/**
 * jsobject.cpp
 * Yet Another (Java)script library
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatescript.h"

using namespace TelEngine;

namespace { // anonymous

// Base class for all native objects that hold a NamedList
class JsNative : public JsObject
{
    YCLASS(JsNative,JsObject)
public:
    inline JsNative(const char* name, NamedList* list, Mutex* mtx)
	: JsObject(name,mtx), m_list(list)
	{ }
    virtual NamedList& list()
	{ return *m_list; }
    virtual const NamedList& list() const
	{ return *m_list; }
private:
    NamedList* m_list;
};

// Array object
class JsArray : public JsObject
{
    YCLASS(JsArray,JsObject)
public:
    inline JsArray(Mutex* mtx)
	: JsObject("Array",mtx)
	{ }
};

// Object constructor
class JsConstructor : public JsFunction
{
    YCLASS(JsConstructor,JsFunction)
public:
    inline JsConstructor(Mutex* mtx)
	: JsFunction(mtx)
	{ }
};

// Object object
class JsObjectObj : public JsObject
{
    YCLASS(JsObjectObj,JsObject)
public:
    inline JsObjectObj(Mutex* mtx)
	: JsObject("Object",mtx,true)
	{
	    params().addParam(new ExpFunction("constructor"));
	}
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

// Date object
class JsDate : public JsObject
{
    YCLASS(JsDate,JsObject)
public:
    inline JsDate(Mutex* mtx)
	: JsObject("Date",mtx,true)
	{
	    params().addParam(new ExpFunction("now"));
	    params().addParam(new ExpFunction("getDate"));
	    params().addParam(new ExpFunction("getDay"));
	    params().addParam(new ExpFunction("getFullYear"));
	    params().addParam(new ExpFunction("getHours"));
	    params().addParam(new ExpFunction("getMilliseconds"));
	    params().addParam(new ExpFunction("getMinutes"));
	    params().addParam(new ExpFunction("getMonth"));
	    params().addParam(new ExpFunction("getSeconds"));
	    params().addParam(new ExpFunction("getTime"));
	}
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

// Math class - not really an object, all methods are static
class JsMath : public JsObject
{
    YCLASS(JsMath,JsObject)
public:
    inline JsMath(Mutex* mtx)
	: JsObject("Math",mtx,true)
	{
	    params().addParam(new ExpFunction("abs"));
	    params().addParam(new ExpFunction("max"));
	    params().addParam(new ExpFunction("min"));
	}
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

}; // anonymous namespace


// Helper function that adds an object to a parent
static inline void addObject(NamedList& params, const char* name, JsObject* obj)
{
    params.addParam(new NamedPointer(name,obj,obj->toString()));
}

JsObject::JsObject(const char* name, Mutex* mtx, bool frozen)
    : ScriptContext(String("[Object ") + name + "]"),
      m_frozen(frozen), m_mutex(mtx)
{
    XDebug(DebugAll,"JsObject::JsObject('%s',%p,%s) [%p]",
	name,mtx,String::boolText(frozen),this);
    params().addParam(new ExpFunction("freeze"));
    params().addParam(new ExpFunction("isFrozen"));
    params().addParam(new ExpFunction("toString"));
}

JsObject::~JsObject()
{
    XDebug(DebugAll,"JsObject::~JsObject '%s' [%p]",toString().c_str(),this);
}

bool JsObject::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugInfo,"JsObject::runFunction() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    NamedString* param = params().getParam(oper.name());
    if (!param)
	return false;
    ExpFunction* ef = YOBJECT(ExpFunction,param);
    if (ef)
	return runNative(stack,oper,context);
    JsFunction* jf = YOBJECT(JsFunction,param);
    if (jf)
	return jf->runDefined(stack,oper,context);
    JsObject* jso = YOBJECT(JsObject,param);
    if (jso) {
	ExpFunction op("constructor",oper.number());
	return jso->runFunction(stack,op,context);
    }
    return false;
}

bool JsObject::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runField() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    const String* param = params().getParam(oper.name());
    if (param) {
	ExpFunction* ef = YOBJECT(ExpFunction,param);
	if (ef)
	    ExpEvaluator::pushOne(stack,new ExpFunction(oper.name(),ef->number()));
	else {
	    JsFunction* jf = YOBJECT(JsFunction,param);
	    if (jf)
		ExpEvaluator::pushOne(stack,new ExpFunction(oper.name()));
	    else {
		JsObject* jo = YOBJECT(JsObject,param);
		if (jo) {
		    jo->ref();
		    ExpEvaluator::pushOne(stack,new ExpWrapper(jo,oper.name()));
		}
		else
		    ExpEvaluator::pushOne(stack,new ExpOperation(*param,oper.name(),true));
	    }
	}
    }
    else
	ExpEvaluator::pushOne(stack,new ExpWrapper(0,oper.name()));
    return true;
}

bool JsObject::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runAssign() '%s'='%s' in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),toString().c_str(),this);
    if (frozen()) {
	Debug(DebugNote,"Object '%s' is frozen",toString().c_str());
	return false;
    }
    ExpFunction* ef = YOBJECT(ExpFunction,&oper);
    if (ef)
	params().setParam(new ExpFunction(oper.name(),oper.number()));
    else {
	ExpWrapper* w = YOBJECT(ExpWrapper,&oper);
	if (w) {
	    GenObject* o = w->object();
	    RefObject* r = YOBJECT(RefObject,o);
	    if (r)
		r->ref();
	    if (o)
		params().setParam(new NamedPointer(oper.name(),o,o->toString()));
	    else
		params().clearParam(oper.name());
	}
	else
	    params().setParam(oper.name(),oper);
    }
    return true;
}

bool JsObject::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("freeze"))
	freeze();
    else if (oper.name() == YSTRING("isFrozen"))
	ExpEvaluator::pushOne(stack,new ExpOperation(frozen()));
    else if (oper.name() == YSTRING("toString"))
	ExpEvaluator::pushOne(stack,new ExpOperation(params()));
    else
	return false;
    return true;
}

ExpOperation* JsObject::popValue(ObjList& stack, GenObject* context)
{
    ExpOperation* oper = ExpEvaluator::popOne(stack);
    if (!oper || (oper->opcode() != ExpEvaluator::OpcField))
	return oper;
    bool ok = runField(stack,*oper,context);
    TelEngine::destruct(oper);
    return ok ? ExpEvaluator::popOne(stack) : 0;
}

// Initialize standard globals in the execution context
void JsObject::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& p = context->params();
    static_cast<String&>(p) = "[Object Global]";
    if (!p.getParam(YSTRING("Object")))
	addObject(p,"Object",new JsObjectObj(mtx));
    if (!p.getParam(YSTRING("Function")))
	addObject(p,"Function",new JsFunction(mtx));
    if (!p.getParam(YSTRING("Date")))
	addObject(p,"Date",new JsDate(mtx));
    if (!p.getParam(YSTRING("Math")))
	addObject(p,"Math",new JsMath(mtx));
    if (!p.getParam(YSTRING("isNaN")))
	p.addParam(new ExpFunction("isNaN"));
}


bool JsObjectObj::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("constructor"))
	ExpEvaluator::pushOne(stack,new ExpWrapper(new JsObject("Object",mutex())));
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

bool JsMath::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("abs")) {
	if (!oper.number())
	    return false;
	long int n = 0;
	for (long int i = oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (op->isInteger())
		n = op->number();
	    TelEngine::destruct(op);
	}
	if (n < 0)
	    n = -n;
	ExpEvaluator::pushOne(stack,new ExpOperation(n));
    }
    else if (oper.name() == YSTRING("max")) {
	if (!oper.number())
	    return false;
	long int n = LONG_MIN;
	for (long int i = oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (op->isInteger() && op->number() > n)
		n = op->number();
	    TelEngine::destruct(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(n));
    }
    else if (oper.name() == YSTRING("min")) {
	if (!oper.number())
	    return false;
	long int n = LONG_MAX;
	for (long int i = oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (op->isInteger() && op->number() < n)
		n = op->number();
	    TelEngine::destruct(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(n));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


bool JsDate::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    return JsObject::runNative(stack,oper,context);
}


bool JsFunction::runDefined(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
