/**
 * jsobject.cpp
 * Yet Another (Java)script library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
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

#include "yatescript.h"
#include <string.h>

using namespace TelEngine;

namespace { // anonymous

// Object object
class JsObjectObj : public JsObject
{
    YCLASS(JsObjectObj,JsObject)
public:
    inline JsObjectObj(Mutex* mtx)
	: JsObject("Object",mtx,true)
	{
	}
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpFunction("keys"));
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
	: JsObject("Date",mtx,true),
	  m_time(0), m_msec(0), m_offs(0)
	{
	    params().addParam(new ExpFunction("getDate"));
	    params().addParam(new ExpFunction("getDay"));
	    params().addParam(new ExpFunction("getFullYear"));
	    params().addParam(new ExpFunction("getHours"));
	    params().addParam(new ExpFunction("getMilliseconds"));
	    params().addParam(new ExpFunction("getMinutes"));
	    params().addParam(new ExpFunction("getMonth"));
	    params().addParam(new ExpFunction("getSeconds"));
	    params().addParam(new ExpFunction("getTime"));
	    params().addParam(new ExpFunction("getTimezoneOffset"));

	    params().addParam(new ExpFunction("getUTCDate"));
	    params().addParam(new ExpFunction("getUTCDay"));
	    params().addParam(new ExpFunction("getUTCFullYear"));
	    params().addParam(new ExpFunction("getUTCHours"));
	    params().addParam(new ExpFunction("getUTCMilliseconds"));
	    params().addParam(new ExpFunction("getUTCMinutes"));
	    params().addParam(new ExpFunction("getUTCMonth"));
	    params().addParam(new ExpFunction("getUTCSeconds"));
	}
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpFunction("now"));
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
protected:
    inline JsDate(Mutex* mtx, u_int64_t msecs, bool local = false)
	: JsObject("Date",mtx),
	  m_time((unsigned int)(msecs / 1000)), m_msec((unsigned int)(msecs % 1000)), m_offs(Time::timeZone())
	{ if (local) m_time -= m_offs; }
    inline JsDate(Mutex* mtx, const char* name, unsigned int time, unsigned int msec, unsigned int offs)
	: JsObject(mtx,name),
	  m_time(time), m_msec(msec), m_offs(offs)
	{ }
    virtual JsObject* clone(const char* name) const
	{ return new JsDate(mutex(),name,m_time,m_msec,m_offs); }
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    unsigned int m_time;
    unsigned int m_msec;
    int m_offs;
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
	    params().addParam(new ExpFunction("random"));
	}
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

}; // anonymous namespace


// Helper function that does the actual object printing
static void dumpRecursiveObj(const GenObject* obj, String& buf, unsigned int depth, ObjList& seen)
{
    if (!obj)
	return;
    String str(' ',2 * depth);
    if (seen.find(obj)) {
	str << "(recursivity encountered)";
	buf.append(str,"\r\n");
	return;
    }
    const NamedString* nstr = YOBJECT(NamedString,obj);
    const NamedPointer* nptr = YOBJECT(NamedPointer,nstr);
    const char* type = nstr ? (nptr ? "NamedPointer" : "NamedString") : "???";
    const char* subType = 0;
    const ScriptContext* scr = YOBJECT(ScriptContext,obj);
    const ExpWrapper* wrap = 0;
    bool objRecursed = false;
    if (scr) {
	const JsObject* jso = YOBJECT(JsObject,scr);
	if (jso) {
	    objRecursed = (seen.find(jso) != 0);
	    if ((jso != obj) && !objRecursed)
		seen.append(jso)->setDelete(false);
	    if (YOBJECT(JsArray,scr))
		type = "JsArray";
	    else if (YOBJECT(JsFunction,scr))
		type = "JsFunction";
	    else if (YOBJECT(JsRegExp,scr))
		type = "JsRegExp";
	    else
		type = "JsObject";
	}
	else
	    type = "ScriptContext";
    }
    seen.append(obj)->setDelete(false);
    const ExpOperation* exp = YOBJECT(ExpOperation,nstr);
    if (exp && !scr) {
	if ((wrap = YOBJECT(ExpWrapper,exp)))
	    type = wrap->object() ? "ExpWrapper" : "Undefined";
	else if (YOBJECT(ExpFunction,exp))
	    type = "ExpFunction";
	else {
	    type = "ExpOperation";
	    subType = exp->typeOf();
	}
    }
    if (nstr)
	str << "'" << nstr->name() << "' = '" << *nstr << "'";
    else
	str << "'" << obj->toString() << "'";
    str << " (" << type << (subType ? ", " : "") << subType << ")";
    if (objRecursed)
	str << " (already seen)";
    buf.append(str,"\r\n");
    if (objRecursed)
	return;
    str.clear();
    if (scr) {
	NamedIterator iter(scr->params());
	while (const NamedString* p = iter.get())
	    dumpRecursiveObj(p,buf,depth + 1,seen);
	if (scr->nativeParams()) {
	    iter = *scr->nativeParams();
	    while (const NamedString* p = iter.get())
		dumpRecursiveObj(p,buf,depth + 1,seen);
	}
    }
    else if (wrap)
	dumpRecursiveObj(wrap->object(),buf,depth + 1,seen);
    else if (nptr)
	dumpRecursiveObj(nptr->userData(),buf,depth + 1,seen);
}


const String JsObject::s_protoName("__proto__");

JsObject::JsObject(const char* name, Mutex* mtx, bool frozen)
    : ScriptContext(String("[object ") + name + "]"),
      m_frozen(frozen), m_mutex(mtx)
{
    XDebug(DebugAll,"JsObject::JsObject('%s',%p,%s) [%p]",
	name,mtx,String::boolText(frozen),this);
    params().addParam(new ExpFunction("freeze"));
    params().addParam(new ExpFunction("isFrozen"));
    params().addParam(new ExpFunction("toString"));
    params().addParam(new ExpFunction("hasOwnProperty"));
}

JsObject::JsObject(Mutex* mtx, const char* name, bool frozen)
    : ScriptContext(name),
      m_frozen(frozen), m_mutex(mtx)
{
    XDebug(DebugAll,"JsObject::JsObject(%p,'%s',%s) [%p]",
	mtx,name,String::boolText(frozen),this);
}

JsObject::JsObject(GenObject* context, Mutex* mtx, bool frozen)
    : ScriptContext("[object Object]"),
      m_frozen(frozen), m_mutex(mtx)
{
    setPrototype(context,YSTRING("Object"));
}

JsObject::~JsObject()
{
    XDebug(DebugAll,"JsObject::~JsObject '%s' [%p]",toString().c_str(),this);
}

JsObject* JsObject::copy(Mutex* mtx) const
{
    JsObject* jso = new JsObject(mtx,toString(),frozen());
    deepCopyParams(jso->params(),params(),mtx);
    return jso;
}

void JsObject::dumpRecursive(const GenObject* obj, String& buf)
{
    ObjList seen;
    dumpRecursiveObj(obj,buf,0,seen);
}

void JsObject::printRecursive(const GenObject* obj)
{
    String buf;
    dumpRecursive(obj,buf);
    Output("%s",buf.c_str());
}

void JsObject::setPrototype(GenObject* context, const String& objName)
{
    ScriptContext* ctxt = YOBJECT(ScriptContext,context);
    if (!ctxt) {
	ScriptRun* sr = static_cast<ScriptRun*>(context);
	if (!(sr && (ctxt = YOBJECT(ScriptContext,sr->context()))))
	    return;
    }
    JsObject* objCtr = YOBJECT(JsObject,ctxt->params().getParam(objName));
    if (objCtr) {
	JsObject* proto = YOBJECT(JsObject,objCtr->params().getParam(YSTRING("prototype")));
	if (proto && proto->ref())
	    params().addParam(new ExpWrapper(proto,protoName()));
    }
}

JsObject* JsObject::buildCallContext(Mutex* mtx, JsObject* thisObj)
{
    JsObject* ctxt = new JsObject(mtx,"()");
    if (thisObj && thisObj->alive())
	ctxt->params().addParam(new ExpWrapper(thisObj,"this"));
    return ctxt;
}

void JsObject::fillFieldNames(ObjList& names)
{
    ScriptContext::fillFieldNames(names,params(),"__");
    const NamedList* native = nativeParams();
    if (native)
	ScriptContext::fillFieldNames(names,*native);
#ifdef XDEBUG
    String tmp;
    tmp.append(names,",");
    Debug(DebugInfo,"JsObject::fillFieldNames: %s",tmp.c_str());
#endif
}

bool JsObject::hasField(ObjList& stack, const String& name, GenObject* context) const
{
    if (ScriptContext::hasField(stack,name,context))
	return true;
    const ScriptContext* proto = YOBJECT(ScriptContext,params().getParam(protoName()));
    if (proto && proto->hasField(stack,name,context))
	return true;
    NamedList* np = nativeParams();
    return np && np->getParam(name);
}

NamedString* JsObject::getField(ObjList& stack, const String& name, GenObject* context) const
{
    NamedString* fld = ScriptContext::getField(stack,name,context);
    if (fld)
	return fld;
    const ScriptContext* proto = YOBJECT(ScriptContext,params().getParam(protoName()));
    if (proto) {
	fld = proto->getField(stack,name,context);
	if (fld)
	    return fld;
    }
    NamedList* np = nativeParams();
    if (np)
	return np->getParam(name);
    return 0;
}

JsObject* JsObject::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (!ref())
	return 0;
    JsObject* obj = clone("[object " + oper.name() + "]");
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsObject::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugInfo,"JsObject::runFunction() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    NamedString* param = getField(stack,oper.name(),context);
    if (!param)
	return false;
    ExpFunction* ef = YOBJECT(ExpFunction,param);
    if (ef)
	return runNative(stack,oper,context);
    JsFunction* jf = YOBJECT(JsFunction,param);
    if (jf) {
	JsObject* objThis = 0;
	if (toString() != YSTRING("()"))
	    objThis = this;
	return jf->runDefined(stack,oper,context,objThis);
    }
    return false;
}

bool JsObject::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runField() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    const String* param = getField(stack,oper.name(),context);
    if (param) {
	ExpFunction* ef = YOBJECT(ExpFunction,param);
	if (ef)
	    ExpEvaluator::pushOne(stack,ef->ExpOperation::clone());
	else {
	    ExpWrapper* w = YOBJECT(ExpWrapper,param);
	    if (w)
		ExpEvaluator::pushOne(stack,w->clone(oper.name()));
	    else {
		JsObject* jso = YOBJECT(JsObject,param);
		if (jso && jso->ref())
		    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,oper.name()));
		else {
		    ExpOperation* o = YOBJECT(ExpOperation,param);
		    ExpEvaluator::pushOne(stack,o ? new ExpOperation(*o,oper.name(),false) : new ExpOperation(*param,oper.name(),true));
		}
	    }
	}
    }
    else
	ExpEvaluator::pushOne(stack,new ExpWrapper(0,oper.name()));
    return true;
}

bool JsObject::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runAssign() '%s'='%s' (%s) in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),toString().c_str(),this);
    if (frozen()) {
	Debug(DebugWarn,"Object '%s' is frozen",toString().c_str());
	return false;
    }
    ExpFunction* ef = YOBJECT(ExpFunction,&oper);
    if (ef)
	params().setParam(ef->ExpOperation::clone());
    else {
	ExpWrapper* w = YOBJECT(ExpWrapper,&oper);
	if (w) {
	    JsFunction* jsf = YOBJECT(JsFunction,w->object());
	    if (jsf)
		jsf->firstName(oper.name());
	    params().setParam(w->clone(oper.name()));
	}
	else
	    params().setParam(oper.clone());
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
    else if (oper.name() == YSTRING("hasOwnProperty")) {
	bool ok = true;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		continue;
	    ok = ok && params().getParam(*op);
	    TelEngine::destruct(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else
	return false;
    return true;
}

ExpOperation* JsObject::popValue(ObjList& stack, GenObject* context)
{
    ExpOperation* oper = ExpEvaluator::popOne(stack);
    if (!oper || (oper->opcode() != ExpEvaluator::OpcField))
	return oper;
    XDebug(DebugAll,"JsObject::popValue() field '%s' in '%s' [%p]",
	oper->name().c_str(),toString().c_str(),this);
    bool ok = runMatchingField(stack,*oper,context);
    TelEngine::destruct(oper);
    return ok ? ExpEvaluator::popOne(stack) : 0;
}

// Static method that adds an object to a parent
void JsObject::addObject(NamedList& params, const char* name, JsObject* obj)
{
    params.addParam(new NamedPointer(name,obj,obj->toString()));
}

// Static method that adds a constructor to a parent
void JsObject::addConstructor(NamedList& params, const char* name, JsObject* obj)
{
    JsFunction* ctr = new JsFunction(obj->mutex(),name);
    ctr->params().addParam(new NamedPointer("prototype",obj,obj->toString()));
    obj->initConstructor(ctr);
    params.addParam(new NamedPointer(name,ctr,ctr->toString()));
}

// Static method that pops arguments off a stack to a list in proper order
int JsObject::extractArgs(JsObject* obj, ObjList& stack, const ExpOperation& oper,
    GenObject* context, ObjList& arguments)
{
    if (!obj || !oper.number())
	return 0;
    for (int i = (int)oper.number(); i;  i--) {
	ExpOperation* op = obj->popValue(stack,context);
	JsFunction* jsf = YOBJECT(JsFunction,op);
	if (jsf)
	    jsf->firstName(op->name());
	arguments.insert(op);
    }
    return (int)oper.number();
}

// Static helper method that deep copies all parameters
void JsObject::deepCopyParams(NamedList& dst, const NamedList& src, Mutex* mtx)
{
    NamedIterator iter(src);
    while (const NamedString* p = iter.get()) {
	ExpOperation* oper = YOBJECT(ExpOperation,p);
	if (oper)
	    dst.addParam(oper->copy(mtx));
	else
	    dst.addParam(p->name(),*p);
    }
}

// Initialize standard globals in the execution context
void JsObject::initialize(ScriptContext* context)
{
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& p = context->params();
    static_cast<String&>(p) = "[object Global]";
    if (!p.getParam(YSTRING("Object")))
	addConstructor(p,"Object",new JsObjectObj(mtx));
    if (!p.getParam(YSTRING("Function")))
	addConstructor(p,"Function",new JsFunction(mtx));
    if (!p.getParam(YSTRING("Array")))
	addConstructor(p,"Array",new JsArray(mtx));
    if (!p.getParam(YSTRING("RegExp")))
	addConstructor(p,"RegExp",new JsRegExp(mtx));
    if (!p.getParam(YSTRING("Date")))
	addConstructor(p,"Date",new JsDate(mtx));
    if (!p.getParam(YSTRING("Math")))
	addObject(p,"Math",new JsMath(mtx));
}


bool JsObjectObj::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("constructor"))
	ExpEvaluator::pushOne(stack,new ExpWrapper(new JsObject("Object",mutex())));
    else if (oper.name() == YSTRING("keys")) {
	ExpOperation* op = 0;
	GenObject* obj = 0;
	if (oper.number() == 0) {
	    ScriptRun* run = YOBJECT(ScriptRun,context);
	    if (run)
		obj = run->context();
	    else
		obj = context;
	}
	else if (oper.number() == 1) {
	    op = popValue(stack,context);
	    if (!op)
		return false;
	    obj = op;
	}
	else
	    return false;
	const NamedList* lst = YOBJECT(NamedList,obj);
	if (lst) {
	    NamedIterator iter(*lst);
	    JsArray* jsa = new JsArray(context,mutex());
	    while (const NamedString* ns = iter.get())
		if (ns->name() != protoName())
		    jsa->push(new ExpOperation(ns->name(),0,true));
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"keys"));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
	TelEngine::destruct(op);
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


JsArray::JsArray(Mutex* mtx)
    : JsObject("Array",mtx), m_length(0)
{
    params().addParam(new ExpFunction("push"));
    params().addParam(new ExpFunction("pop"));
    params().addParam(new ExpFunction("concat"));
    params().addParam(new ExpFunction("join"));
    params().addParam(new ExpFunction("reverse"));
    params().addParam(new ExpFunction("shift"));
    params().addParam(new ExpFunction("unshift"));
    params().addParam(new ExpFunction("slice"));
    params().addParam(new ExpFunction("splice"));
    params().addParam(new ExpFunction("sort"));
    params().addParam(new ExpFunction("indexOf"));
    params().addParam(new ExpFunction("lastIndexOf"));
    params().addParam("length","0");
}

JsArray::JsArray(GenObject* context, Mutex* mtx)
    : JsObject(mtx,"[object Array]"), m_length(0)
{
    setPrototype(context,YSTRING("Array"));
}

JsObject* JsArray::copy(Mutex* mtx) const
{
    JsArray* jsa = new JsArray(mtx,toString(),frozen());
    deepCopyParams(jsa->params(),params(),mtx);
    jsa->setLength(length());
    return jsa;
}

void JsArray::push(ExpOperation* item)
{
    if (!item)
	return;
    unsigned int pos = m_length;
    while (params().getParam(String(pos)))
	pos++;
    const_cast<String&>(item->name()) = pos;
    params().addParam(item);
    setLength(pos + 1);
}

bool JsArray::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsArray::runAssign() '%s'='%s' (%s) in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),toString().c_str(),this);
    if (oper.name() == YSTRING("length")) {
	int newLen = oper.toInteger(-1);
	if (newLen < 0)
	    return false;
	for (int i = newLen; i < length(); i++)
	    params().clearParam(String(i));
	setLength(newLen);
	return true;
    }
    else if (!JsObject::runAssign(stack,oper,context))
	return false;
    int idx = oper.toString().toInteger(-1) + 1;
    if (idx && idx > m_length)
	setLength(idx);
    return true;
}

bool JsArray::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsArray::runField() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("length")) {
	// Reflects the number of elements in an array.
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)length()));
	return true;
    }
    return JsObject::runField(stack,oper,context);
}

void JsArray::initConstructor(JsFunction* construct)
{
    construct->params().addParam(new ExpFunction("isArray"));
}

JsObject* JsArray::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (!ref())
	return 0;
    JsArray* obj = static_cast<JsArray*>(clone("[object " + oper.name() + "]"));
    unsigned int len = oper.number();
    for (unsigned int i = len; i;  i--) {
	ExpOperation* op = obj->popValue(stack,context);
	if ((len == 1) && op->isInteger() && (op->number() >= 0) && (op->number() <= 0xffffffff)) {
	    len = op->number();
	    TelEngine::destruct(op);
	    break;
	}
	const_cast<String&>(op->name()) = i - 1;
	obj->params().paramList()->insert(op);
    }
    obj->setLength(len);
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsArray::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsArray::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("isArray")) {
	// Static function that checks if the argument is an Array
	ObjList args;
	extractArgs(this,stack,oper,context,args);
	ExpEvaluator::pushOne(stack,new ExpOperation(YOBJECT(JsArray,args[0])));
    }
    else if (oper.name() == YSTRING("push")) {
	// Adds one or more elements to the end of an array and returns the new length of the array.
	ObjList args;
	if (!extractArgs(this,stack,oper,context,args))
	    return false;
	while (ExpOperation* op = static_cast<ExpOperation*>(args.remove(false))) {
	    const_cast<String&>(op->name()) = (unsigned int)m_length++;
	    params().addParam(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)length()));
    }
    else if (oper.name() == YSTRING("pop")) {
	// Removes the last element from an array and returns that element
	if (oper.number())
	    return false;
	NamedString* last = 0;
	while ((m_length > 0) && !last)
	    last = params().getParam(String(--m_length));
	if (!last)
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,0));
	else {
	    params().paramList()->remove(last,false);
	    ExpOperation* op = YOBJECT(ExpOperation,last);
	    if (!op) {
		op = new ExpOperation(*last,0,true);
		TelEngine::destruct(last);
	    }
	    ExpEvaluator::pushOne(stack,op);
	}
    }
    else if (oper.name() == YSTRING("concat")) {
	// Returns a new array comprised of this array joined with other array(s) and/or value(s).
	// var num1 = [1, 2, 3];
	// var num2 = [4, 5, 6];
	// var num3 = [7, 8, 9];
	//
	// creates array [1, 2, 3, 4, 5, 6, 7, 8, 9]; num1, num2, num3 are unchanged
	// var nums = num1.concat(num2, num3);

	// var alpha = ['a', 'b', 'c'];
	// creates array ["a", "b", "c", 1, 2, 3], leaving alpha unchanged
	// var alphaNumeric = alpha.concat(1, [2, 3]);

	ObjList args;
	extractArgs(this,stack,oper,context,args);

	JsArray* array = new JsArray(context,mutex());
	// copy this array - only numerically indexed elements!
	for (int i = 0; i < m_length; i++) {
	    NamedString* ns = params().getParam(String(i));
	    ExpOperation* op = YOBJECT(ExpOperation,ns);
	    op = op ? op->clone() : new ExpOperation(*ns,ns->name(),true);
	    array->params().addParam(op);
	}
	array->setLength(length());
	// add parameters - either basic types or elements of Array
	while (ExpOperation* op = static_cast<ExpOperation*>(args.remove(false))) {
	    JsArray* ja = YOBJECT(JsArray,op);
	    if (ja) {
		int len = ja->length();
		for (int i = 0; i < len; i++) {
		    NamedString* ns = ja->params().getParam(String(i));
		    ExpOperation* arg = YOBJECT(ExpOperation,ns);
		    arg = arg ? arg->clone() : new ExpOperation(*ns,0,true);
		    const_cast<String&>(arg->name()) = (unsigned int)array->m_length++;
		    array->params().addParam(arg);
		}
		TelEngine::destruct(op);
	    }
	    else {
		const_cast<String&>(op->name()) = (unsigned int)array->m_length++;
		array->params().addParam(op);
	    }
	}
	ExpEvaluator::pushOne(stack,new ExpWrapper(array));
    }
    else if (oper.name() == YSTRING("join")) {
	// Joins all elements of an array into a string
	// var a = new Array("Wind","Rain","Fire");
	// var myVar1 = a.join();      // assigns "Wind,Rain,Fire" to myVar1
	// var myVar2 = a.join(", ");  // assigns "Wind, Rain, Fire" to myVar2
	// var myVar3 = a.join(" + "); // assigns "Wind + Rain + Fire" to myVar3
	String separator = ",";
	if (oper.number()) {
	    ExpOperation* op = popValue(stack,context);
	    separator = *op;
	    TelEngine::destruct(op);
	}
	String result;
	for (int32_t i = 0; i < length(); i++)
	    result.append(params()[String(i)],separator);
	ExpEvaluator::pushOne(stack,new ExpOperation(result));
    }
    else if (oper.name() == YSTRING("reverse")) {
	// Reverses the order of the elements of an array -- the first becomes the last, and the last becomes the first.
	// var myArray = ["one", "two", "three"];
	// myArray.reverse(); => three, two, one
	if (oper.number())
	    return false;
	int i1 = 0;
	int i2 = length() - 1;
	for (; i1 < i2; i1++, i2--) {
	    String s1(i1);
	    String s2(i2);
	    NamedString* n1 = params().getParam(s1);
	    NamedString* n2 = params().getParam(s2);
	    if (n1)
		const_cast<String&>(n1->name()) = s2;
	    if (n2)
		const_cast<String&>(n2->name()) = s1;
	}
	ref();
	ExpEvaluator::pushOne(stack,new ExpWrapper(this));
    }
    else if (oper.name() == YSTRING("shift")) {
	// Removes the first element from an array and returns that element
	// var myFish = ["angel", "clown", "mandarin", "surgeon"];
	// println("myFish before: " + myFish);
	// var shifted = myFish.shift();
	// println("myFish after: " + myFish);
	// println("Removed this element: " + shifted);
	// This example displays the following:

	// myFish before: angel,clown,mandarin,surgeon
	// myFish after: clown,mandarin,surgeon
	// Removed this element: angel
	if (oper.number())
	    return false;
	ObjList* l = params().paramList()->find("0");
	if (l) {
	    NamedString* ns = static_cast<NamedString*>(l->get());
	    params().paramList()->remove(ns,false);
	    ExpOperation* op = YOBJECT(ExpOperation,ns);
	    if (!op) {
		op = new ExpOperation(*ns,0,true);
		TelEngine::destruct(ns);
	    }
	    ExpEvaluator::pushOne(stack,op);
	    // shift : value n+1 becomes value n
	    for (int32_t i = 0; ; i++) {
		ns = static_cast<NamedString*>((*params().paramList())[String(i + 1)]);
		if (!ns) {
		    setLength(i);
		    break;
		}
		const_cast<String&>(ns->name()) = i;
	    }
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,0));
    }
    else if (oper.name() == YSTRING("unshift")) {
	// Adds one or more elements to the front of an array and returns the new length of the array
	// myFish = ["angel", "clown"];
	// println("myFish before: " + myFish);
	// unshifted = myFish.unshift("drum", "lion");
	// println("myFish after: " + myFish);
	// println("New length: " + unshifted);
	// This example displays the following:
	// myFish before: ["angel", "clown"]
	// myFish after: ["drum", "lion", "angel", "clown"]
	// New length: 4
	// shift array
	int32_t shift = (int32_t)oper.number();
	if (shift >= 1) {
	    for (int32_t i = length() + shift - 1; i >= shift; i--) {
		NamedString* ns = static_cast<NamedString*>((*params().paramList())[String(i - shift)]);
		if (ns) {
		    String index(i);
		    params().clearParam(index);
		    const_cast<String&>(ns->name()) = index;
		}
	    }
	    for (int32_t i = shift - 1; i >= 0; i--) {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    continue;
	        const_cast<String&>(op->name()) = i;
		params().paramList()->insert(op);
	    }
	    setLength(length() + shift);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)length()));
    }
    else if (oper.name() == YSTRING("slice"))
	return runNativeSlice(stack,oper,context);
    else if (oper.name() == YSTRING("splice"))
	return runNativeSplice(stack,oper,context);
    else if (oper.name() == YSTRING("sort")) {
	return runNativeSort(stack,oper,context);
    }
    else if (oper.name() == YSTRING("toString")) {
	// Override the JsObject toString method
	// var monthNames = ['Jan', 'Feb', 'Mar', 'Apr'];
	// var myVar = monthNames.toString(); // assigns "Jan,Feb,Mar,Apr" to myVar.
	String separator = ",";
	String result;
	for (int32_t i = 0; i < length(); i++)
	    result.append(params()[String(i)],separator);
	ExpEvaluator::pushOne(stack,new ExpOperation(result));
    } else if (oper.name() == YSTRING("indexOf") || oper.name() == YSTRING("lastIndexOf")) {
	// arr.indexOf(searchElement[,startIndex = 0[,"fieldName"]])
	// arr.lastIndexOf(searchElement[,startIndex = arr.length-1[,"fieldName"]])
	ObjList args;
	if (!extractArgs(this,stack,oper,context,args)) {
	    Debug(DebugWarn,"Failed to extract arguments!");
	    return false;
	}
	ExpOperation* op1 = static_cast<ExpOperation*>(args.remove(false));
	if (!op1)
	    return false;
	ExpWrapper* w1 = YOBJECT(ExpWrapper,op1);
	ExpOperation* fld = 0;
	int dir = 1;
	int pos = 0;
	if (oper.name().at(0) == 'l') {
	    dir = -1;
	    pos = length() - 1;
	}
	if (args.skipNull()) {
	    String* spos = static_cast<String*>(args.remove(false));
	    if (spos) {
		pos = spos->toInteger(pos);
		if (pos < 0)
		    pos += length();
		if (dir > 0) {
		    if (pos < 0)
			pos = 0;
		}
		else if (pos >= length())
		    pos = length() - 1;
	    }
	    TelEngine::destruct(spos);
	    fld = static_cast<ExpOperation*>(args.remove(false));
	}
	int index = -1;
	for (int i = pos; ; i += dir) {
	    if (dir > 0) {
		if (i >= length())
		    break;
	    }
	    else if (i < 0)
		break;
	    ExpOperation* op2 = static_cast<ExpOperation*>(params().getParam(String(i)));
	    if (op2 && !TelEngine::null(fld)) {
		const ExpExtender* ext = YOBJECT(ExpExtender,op2);
		if (!ext)
		    continue;
		op2 = YOBJECT(ExpOperation,ext->getField(stack,*fld,context));
	    }
	    if (!op2 || op2->opcode() != op1->opcode())
		continue;
	    ExpWrapper* w2 = YOBJECT(ExpWrapper,op2);
	    if (w1 || w2) {
		if (w1 && w2 && w1->object() == w2->object()) {
		    index = i;
		    break;
		}
	    } else if ((op1->number() == op2->number()) && (*op1 == *op2)) {
		index = i;
		break;
	    }
	}
	TelEngine::destruct(op1);
	TelEngine::destruct(fld);
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)index));
	return true;
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

bool JsArray::runNativeSlice(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    // Extracts a section of an array and returns a new array.
    // var myHonda = { color: "red", wheels: 4, engine: { cylinders: 4, size: 2.2 } };
    // var myCar = [myHonda, 2, "cherry condition", "purchased 1997"];
    // var newCar = myCar.slice(0, 2);
    int32_t begin = 0, end = length();
    switch (oper.number()) {
	case 2:
	    {   // get end of interval
		ExpOperation* op = popValue(stack,context);
		if (op && op->isInteger())
		    end = op->number();
		TelEngine::destruct(op);
	    }
	// intentional fallthrough
	case 1:
	    {
		ExpOperation* op = popValue(stack,context);
		if (op && op->isInteger())
		    begin = op->number();
		TelEngine::destruct(op);
	    }
	    break;
	case 0:
	    break;
	default:
	    // maybe we should ignore the rest of the given parameters?
	    return false;
    }
    if (begin < 0) {
	begin = length() + begin;
	if (begin < 0)
	    begin = 0;
    }
    if (end < 0)
	end = length() + end;
 
    JsArray* array = new JsArray(context,mutex());
    for (int32_t i = begin; i < end; i++) {
	NamedString* ns = params().getParam(String(i));
	if (!ns) {
	    // if missing, insert undefined element in array also
	    array->m_length++;
	    continue;
	}
	ExpOperation* arg = YOBJECT(ExpOperation,ns);
	arg = arg ? arg->clone() : new ExpOperation(*ns,0,true);
	const_cast<String&>(arg->name()) = (unsigned int)array->m_length++;
	array->params().addParam(arg);
    }
    ExpEvaluator::pushOne(stack,new ExpWrapper(array));
    return true;
}

bool JsArray::runNativeSplice(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    // Changes the content of an array, adding new elements while removing old elements.
    // Returns an array containing the removed elements
    // array.splice(index , howMany[, element1[, ...[, elementN]]])
    // array.splice(index ,[ howMany[, element1[, ...[, elementN]]]])
    ObjList args;
    int argc = extractArgs(this,stack,oper,context,args);
    if (!argc)
	return false;
    // get start index
    int32_t len = length();
    ExpOperation* op = static_cast<ExpOperation*>(args.remove(false));
    int32_t begin = (int)(op->number() > len ? len : op->number());
    if (begin < 0)
	begin = len + begin > 0 ? len + begin : 0;
    TelEngine::destruct(op);
    argc--;
    // get count of objects to delete
    int32_t delCount = len - begin;
    if (argc) {
	op = static_cast<ExpOperation*>(args.remove(false));
	// howMany is negative, set it to 0
	if (op->number() < 0)
	    delCount = 0;
	// if howMany is greater than the length of remaining elements from start index, do not set it
	else if (op->number() < delCount)
	    delCount = op->number();
	TelEngine::destruct(op);
	argc--;
    }

    // remove elements
    JsArray* removed = new JsArray(context,mutex());
    for (int32_t i = begin; i < begin + delCount; i++) {
	NamedString* ns = params().getParam(String(i));
	if (!ns) {
	    // if missing, insert undefined element in array also
	    removed->m_length++;
	    continue;
	}
	params().paramList()->remove(ns,false);
	ExpOperation* op = YOBJECT(ExpOperation,ns);
	if (!op) {
	    op = new ExpOperation(*ns,0,true);
	    TelEngine::destruct(ns);
	}
	const_cast<String&>(op->name()) = (unsigned int)removed->m_length++;
	removed->params().addParam(op);
    }

    int32_t shiftIdx = argc - delCount;
    // shift elements to make room for those that are to be inserted or move the ones that remained
    // after delete
    if (shiftIdx > 0) {
	for (int32_t i = m_length - 1; i >= begin + delCount; i--) {
	    NamedString* ns = static_cast<NamedString*>((*params().paramList())[String(i)]);
	    if (ns)
		const_cast<String&>(ns->name()) = i + shiftIdx;
	}
    }
    else if (shiftIdx < 0) {
	for (int32_t i = begin + delCount; i < m_length; i++) {
	    NamedString* ns = static_cast<NamedString*>((*params().paramList())[String(i)]);
	    if (ns)
		const_cast<String&>(ns->name()) = i + shiftIdx;
	}
    }
    setLength(length() + shiftIdx);
    // insert the new elements
    for (int i = 0; i < argc; i++) {
	ExpOperation* arg = static_cast<ExpOperation*>(args.remove(false));
	const_cast<String&>(arg->name()) = (unsigned int)(begin + i);
	params().addParam(arg);
    }
    ExpEvaluator::pushOne(stack,new ExpWrapper(removed));
    return true;
}

class JsComparator
{
public:
    JsComparator(const char* funcName, ScriptRun* runner)
	: m_name(funcName), m_runner(runner), m_failed(false)
	{ }
    const char* m_name;
    ScriptRun* m_runner;
    bool m_failed;
};

int compare(GenObject* op1, GenObject* op2, void* data)
{
    JsComparator* cmp = static_cast<JsComparator*>(data);
    if (cmp && cmp->m_failed)
	return 0;
    if (!(cmp && cmp->m_runner))
	return ::strcmp(*(static_cast<String*>(op1)),*(static_cast<String*>(op2)));
    ScriptRun* runner = cmp->m_runner->code()->createRunner(cmp->m_runner->context());
    if (!runner)
	return 0;
    ObjList stack;
    stack.append((static_cast<ExpOperation*>(op1))->clone());
    stack.append((static_cast<ExpOperation*>(op2))->clone());
    ScriptRun::Status rval = runner->call(cmp->m_name,stack);
    int ret = 0;
    if (ScriptRun::Succeeded == rval) {
	ExpOperation* sret = static_cast<ExpOperation*>(ExpEvaluator::popOne(runner->stack()));
	if (sret) {
	    ret = sret->toInteger();
	    TelEngine::destruct(sret);
	}
	else
	    cmp->m_failed = true;
    }
    else
	cmp->m_failed = true;
    TelEngine::destruct(runner);
    return ret;
}

bool JsArray::runNativeSort(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList arguments;
    ExpOperation* op = 0;
    if (extractArgs(this,stack,oper,context,arguments))
	op = static_cast<ExpOperation*>(arguments[0]);
    ScriptRun* runner = YOBJECT(ScriptRun,context);
    if (op && !runner)
	return false;
    ObjList sorted;
    ObjList* last = &sorted;
    // Copy the arguments in a ObjList for sorting
    for (ObjList* o = params().paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* str = static_cast<NamedString*>(o->get());
	if (str->name().toInteger(-1) > -1)
	    (last = last->append(str))->setDelete(false);
    }
    JsComparator* comp = op ? new JsComparator(op->name() ,runner) : 0;
    sorted.sort(&compare,comp);
    bool ok = comp ? !comp->m_failed : true;
    delete comp;
    if (ok) {
	for (ObjList* o = params().paramList()->skipNull(); o;) {
	    NamedString* str = static_cast<NamedString*>(o->get());
	    if (str && str->name().toInteger(-1) > -1)
		o->remove(false);
	    else
		o = o->skipNext();
	}
	int i = 0;
	last = params().paramList()->last();
	for (ObjList* o = sorted.skipNull();o; o = o->skipNull()) {
	    ExpOperation* slice = static_cast<ExpOperation*>(o->remove(false));
	    const_cast<String&>(slice->name()) = i++;
	    last = last->append(slice);
	}
    }
    return ok;
}


JsRegExp::JsRegExp(Mutex* mtx)
    : JsObject("RegExp",mtx)
{
    params().addParam(new ExpFunction("test"));
    params().addParam(new ExpFunction("valid"));
}

JsRegExp::JsRegExp(Mutex* mtx, const char* name, const char* rexp, bool insensitive, bool extended, bool frozen)
    : JsObject(mtx,name,frozen),
      m_regexp(rexp,extended,insensitive)
{
    params().addParam(new ExpFunction("test"));
    params().addParam(new ExpFunction("valid"));
    params().addParam("ignoreCase",String::boolText(insensitive));
    params().addParam("basicPosix",String::boolText(!extended));
}

JsRegExp::JsRegExp(Mutex* mtx, const Regexp& rexp, bool frozen)
    : JsObject("RegExp",mtx),
      m_regexp(rexp)
{
    params().addParam(new ExpFunction("test"));
    params().addParam(new ExpFunction("valid"));
    params().addParam("ignoreCase",String::boolText(rexp.isCaseInsensitive()));
    params().addParam("basicPosix",String::boolText(!rexp.isExtended()));
}

bool JsRegExp::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsRegExp::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("test")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	bool ok = op && regexp().matches(*op);
	TelEngine::destruct(op);
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("valid")) {
	if (oper.number())
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(regexp().compile()));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

bool JsRegExp::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsRegExp::runAssign() '%s'='%s' (%s) in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),toString().c_str(),this);
    if (!JsObject::runAssign(stack,oper,context))
	return false;
    if (oper.name() == YSTRING("ignoreCase"))
	regexp().setFlags(regexp().isExtended(),oper.toBoolean());
    else if (oper.name() == YSTRING("basicPosix"))
	regexp().setFlags(!oper.toBoolean(),regexp().isCaseInsensitive());
    return true;
}

JsObject* JsRegExp::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	case 2:
	    break;
	default:
	    return 0;
    }
    ExpOperation* pattern = static_cast<ExpOperation*>(args[0]);
    ExpOperation* flags = static_cast<ExpOperation*>(args[1]);
    if (!pattern)
	return 0;
    bool insensitive = false;
    bool extended = true;
    if (flags && *flags)  {
	const char* f = *flags;
	char c = *f++;
	while (c) {
	    switch (c) {
		case 'i':
		    c = *f++;
		    insensitive = true;
		    break;
		case 'b':
		    c = *f++;
		    extended = false;
		    break;
		default:
		    c = 0;
	    }
	}
    }
    if (!ref())
	return 0;
    JsRegExp* obj = new JsRegExp(mutex(),*pattern,*pattern,insensitive,extended);
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}


bool JsMath::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsMath::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("abs")) {
	if (!oper.number())
	    return false;
	int64_t n = 0;
	for (int i = (int)oper.number(); i; i--) {
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
	int64_t n = LLONG_MIN;
	for (int i = (int)oper.number(); i; i--) {
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
	int64_t n = LLONG_MAX;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (op->isInteger() && op->number() < n)
		n = op->number();
	    TelEngine::destruct(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(n));
    }
    else if (oper.name() == YSTRING("random")) {
	long min = 0;
	long max = LONG_MAX;
	ObjList args;
	if (extractArgs(stack,oper,context,args)) {
	    if (args.skipNull()) {
		const String* mins = static_cast<String*>(args[0]);
		if (mins)
		    min = mins->toLong(0);
	    }
	    if (args.count() >= 2) {
		const String* maxs = static_cast<String*>(args[1]);
		if (maxs)
		    max = maxs->toLong(max);
	    }
	}
	if (min < 0 || max < 0 || min >= max)
	    return false;
	int64_t rand = (max > (min + 1)) ? (Random::random() % (max - min)) : 0;
	ExpEvaluator::pushOne(stack,new ExpOperation(rand + min));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


JsObject* JsDate::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsDate::runConstructor '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    JsObject* obj = 0;
    switch (extractArgs(stack,oper,context,args)) {
	case 0:
	    obj = new JsDate(mutex(),Time::msecNow());
	    break;
	case 1:
	    {
		ExpOperation* val = static_cast<ExpOperation*>(args[0]);
		if (val && val->isInteger())
		    obj = new JsDate(mutex(),val->number());
	    }
	    break;
	case 3:
	case 6:
	case 7:
	    {
		unsigned int parts[7];
		for (int i = 0; i < 7; i++) {
		    parts[i] = 0;
		    ExpOperation* val = static_cast<ExpOperation*>(args[i]);
		    if (val) {
			if (val->isInteger())
			    parts[i] = (int)val->number();
			else
			    return 0;
		    }
		}
		// Date components use local time, month starts from 0
		if (parts[1] < 12)
		    parts[1]++;
		u_int64_t time = Time::toEpoch(parts[0],parts[1],parts[2],parts[3],parts[4],parts[5]);
		obj = new JsDate(mutex(),1000 * time + parts[6],true);
	    }
	    break;
	default:
	    return 0;
    }
    if (obj && ref())
	obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsDate::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsDate::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("now")) {
	// Returns the number of milliseconds elapsed since 1 January 1970 00:00:00 UTC.
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)Time::msecNow()));
    }
    else if (oper.name() == YSTRING("getDate")) {
	// Returns the day of the month for the specified date according to local time.
	// The value returned by getDate is an integer between 1 and 31.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)day));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getDay")) {
	// Get the day of the week for the date (0 is Sunday and returns values 0-6)
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0, wday = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec,&wday))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)wday));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getFullYear")) {
	// Returns the year of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)year));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getHours")) {
	// Returns the hour ( 0 - 23) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)hour));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getMilliseconds")) {
	// Returns just the milliseconds part ( 0 - 999 )
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_msec));
    }
    else if (oper.name() == YSTRING("getMinutes")) {
	// Returns the minute ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)minute));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getMonth")) {
	// Returns the month ( 0 - 11 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)month - 1));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getSeconds")) {
	// Returns the second ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)sec));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getTime")) {
	// Returns the time in milliseconds since UNIX Epoch
	ExpEvaluator::pushOne(stack,new ExpOperation(1000 * ((int64_t)m_time) + (int64_t)m_msec));
    }
    else if (oper.name() == YSTRING("getTimezoneOffset")) {
	// Returns the UTC to local difference in minutes, positive goes west
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)(m_offs / -60)));
    }
    else if (oper.name() == YSTRING("getUTCDate")) {
	// Returns the day of the month for the specified date according to local time.
	// The value returned by getDate is an integer between 1 and 31.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)day));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCDay")) {
	// Get the day of the week for the date (0 is Sunday and returns values 0-6)
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0, wday = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec,&wday))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)wday));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCFullYear")) {
	// Returns the year of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)year));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCHours")) {
	// Returns the hour ( 0 - 23) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)hour));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCMilliseconds")) {
	// Returns just the milliseconds part ( 0 - 999 )
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_msec));
    }
    else if (oper.name() == YSTRING("getUTCMinutes")) {
	// Returns the minute ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)minute));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCMonth")) {
	// Returns the month ( 0 - 11 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)month - 1));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCSeconds")) {
	// Returns the second ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)sec));
	else
	    return false;
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
