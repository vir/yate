/**
 * javascript.cpp
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
#include <yatengine.h>

using namespace TelEngine;

namespace { // anonymous

class ParseNested;
class JsRunner;

class JsContext : public JsObject, public Mutex
{
    YCLASS(JsContext,JsObject)
public:
    inline JsContext()
	: JsObject("Context",this), Mutex(true,"JsContext")
	{
	    params().addParam(new ExpFunction("isNaN"));
	    params().addParam(new ExpFunction("parseInt"));
	    params().addParam(new ExpOperation(ExpOperation::nonInteger(),"NaN"));
	}
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);
    GenObject* resolve(ObjList& stack, String& name, GenObject* context);
private:
    GenObject* resolveTop(ObjList& stack, const String& name, GenObject* context);
    bool runStringFunction(GenObject* obj, const String& name, ObjList& stack, const ExpOperation& oper, GenObject* context);
    bool runStringField(GenObject* obj, const String& name, ObjList& stack, const ExpOperation& oper, GenObject* context);
};

class JsNull : public JsObject
{
public:
    inline JsNull()
	: JsObject(0,"null",true)
	{ }
};

class ExpNull : public ExpWrapper
{
public:
    inline ExpNull()
	: ExpWrapper(new JsNull,"null")
	{ }
    virtual bool valBoolean() const
	{ return false; }
    virtual ExpOperation* clone(const char* name) const
	{ return new ExpNull(static_cast<JsNull*>(object()),name); }
protected:
    inline ExpNull(JsNull* obj, const char* name)
	: ExpWrapper(obj,name)
	{ obj->ref(); }
};

class JsCode : public ScriptCode, public ExpEvaluator
{
    friend class TelEngine::JsFunction;
    friend class TelEngine::JsParser;
    friend class ParseNested;
    friend class JsRunner;
public:
    enum JsOpcode {
	OpcBegin = OpcPrivate + 1,
	OpcEnd,
	OpcFlush,
	OpcIndex,
	OpcEqIdentity,
	OpcNeIdentity,
	OpcFieldOf,
	OpcTypeof,
	OpcNew,
	OpcFor,
	OpcWhile,
	OpcIf,
	OpcElse,
	OpcSwitch,
	OpcCase,
	OpcDefault,
	OpcBreak,
	OpcCont,
	OpcIn,
	OpcOf,
	OpcNext,
	OpcVar,
	OpcWith,
	OpcTry,
	OpcCatch,
	OpcFinally,
	OpcThrow,
	OpcFuncDef,
	OpcReturn,
	OpcJump,
	OpcJumpTrue,
	OpcJumpFalse,
	OpcJRel,
	OpcJRelTrue,
	OpcJRelFalse,
	OpcTrue,
	OpcFalse,
	OpcNull,
	OpcUndefined,
	OpcInclude,
	OpcRequire,
	OpcDelete,
    };
    inline JsCode()
	: ExpEvaluator(C), m_label(0), m_depth(0)
	  { debugName("JsCode"); }

    virtual void* getObject(const String& name) const
    {
	if (name == YSTRING("JsCode"))
            return const_cast<JsCode*>(this);
	if (name == YSTRING("ExpEvaluator"))
            return const_cast<ExpEvaluator*>((const ExpEvaluator*)this);
	return ScriptCode::getObject(name);
    }
    virtual bool initialize(ScriptContext* context) const;
    virtual bool evaluate(ScriptRun& runner, ObjList& results) const;
    virtual ScriptRun* createRunner(ScriptContext* context);
    virtual bool null() const;
    bool link();
    JsObject* parseArray(const char*& expr, bool constOnly);
    JsObject* parseObject(const char*& expr, bool constOnly);
protected:
    virtual void formatLineNo(String& buf, unsigned int line) const;
    virtual bool getString(const char*& expr);
    virtual bool getEscape(const char*& expr, String& str, char sep);
    virtual bool keywordChar(char c) const;
    virtual int getKeyword(const char* str) const;
    virtual char skipComments(const char*& expr, GenObject* context = 0);
    virtual int preProcess(const char*& expr, GenObject* context = 0);
    virtual bool getInstruction(const char*& expr, char stop, GenObject* nested);
    virtual bool getSimple(const char*& expr, bool constOnly = false);
    virtual Opcode getOperator(const char*& expr);
    virtual Opcode getUnaryOperator(const char*& expr);
    virtual Opcode getPostfixOperator(const char*& expr, int precedence);
    virtual const char* getOperator(Opcode oper) const;
    virtual int getPrecedence(ExpEvaluator::Opcode oper) const;
    virtual bool getSeparator(const char*& expr, bool remove);
    virtual bool runOperation(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
private:
    ObjVector m_linked;
    ObjList m_included;
    ObjList m_globals;
    bool preProcessInclude(const char*& expr, bool once, GenObject* context);
    bool getOneInstruction(const char*& expr, GenObject* nested);
    bool parseInner(const char*& expr, JsOpcode opcode, ParseNested* nested);
    bool parseIf(const char*& expr, GenObject* nested);
    bool parseSwitch(const char*& expr, GenObject* nested);
    bool parseFor(const char*& expr, GenObject* nested);
    bool parseWhile(const char*& expr, GenObject* nested);
    bool parseVar(const char*& expr);
    bool parseTry(const char*& expr, GenObject* nested);
    bool parseFuncDef(const char*& expr, bool publish);
    bool evalList(ObjList& stack, GenObject* context) const;
    bool evalVector(ObjList& stack, GenObject* context) const;
    bool jumpToLabel(long int label, GenObject* context) const;
    bool jumpRelative(long int offset, GenObject* context) const;
    bool jumpAbsolute(long int index, GenObject* context) const;
    bool callFunction(ObjList& stack, const ExpOperation& oper, GenObject* context,
	JsFunction* func, bool constr, JsObject* thisObj = 0) const;
    bool callFunction(ObjList& stack, const ExpOperation& oper, GenObject* context,
	long int retIndex, JsFunction* func, ObjList& args,
	JsObject* thisObj, JsObject* scopeObj) const;
    void resolveObjectParams(JsObject* obj, ObjList& stack, GenObject* context) const;
    inline JsFunction* getGlobalFunction(const String& name) const
	{ return YOBJECT(JsFunction,m_globals[name]); }
    long int m_label;
    int m_depth;
};

class JsIterator : public RefObject
{
    YCLASS(JsIterator,RefObject);
public:
    inline JsIterator(const ExpOperation& field, JsObject* obj)
	: m_field(field.clone()), m_obj(obj)
	{ obj->fillFieldNames(m_keys); }
    inline JsIterator(const ExpOperation& field, NamedList* lst)
	: m_field(field.clone())
	{ ScriptContext::fillFieldNames(m_keys,*lst); }
    virtual ~JsIterator()
	{ TelEngine::destruct(m_field); }
    inline ExpOperation& field() const
	{ return *m_field; }
    inline String* get()
	{ return static_cast<String*>(m_keys.remove(false)); }
    inline const String& name() const
	{ return m_name; }
    inline void name(const char* objName)
	{ m_name = objName; }
private:
    ExpOperation* m_field;
    RefPointer<JsObject> m_obj;
    ObjList m_keys;
    String m_name;
};

class JsRunner : public ScriptRun
{
    friend class JsCode;
public:
    inline JsRunner(ScriptCode* code, ScriptContext* context)
	: ScriptRun(code,context),
	  m_paused(false), m_opcode(0), m_index(0)
	{ }
    virtual Status reset(bool init);
    virtual bool pause();
    virtual Status call(const String& name, ObjList& args, ExpOperation* thisObj = 0, ExpOperation* scopeObj = 0);
    virtual bool callable(const String& name);
protected:
    virtual Status resume();
private:
    bool m_paused;
    const ObjList* m_opcode;
    unsigned int m_index;
};

class ParseNested : public GenObject
{
    YCLASS(ParseNested,GenObject)
public:
    inline explicit ParseNested(JsCode* code, GenObject* nested,
	JsCode::JsOpcode oper = (JsCode::JsOpcode)ExpEvaluator::OpcNone)
	: m_code(code), m_nested(static_cast<ParseNested*>(nested)), m_opcode(oper)
	{ }
    inline operator GenObject*()
	{ return this; }
    inline operator JsCode::JsOpcode() const
	{ return m_opcode; }
    inline static JsCode::JsOpcode code(GenObject* nested)
	{ return nested ? *static_cast<ParseNested*>(nested) :
	    (JsCode::JsOpcode)ExpEvaluator::OpcNone; }
    inline static ParseNested* find(GenObject* nested, JsCode::JsOpcode opcode)
	{ return nested ? static_cast<ParseNested*>(nested)->find(opcode) : 0; }
    inline static ParseNested* findMatch(GenObject* nested, JsCode::JsOpcode opcode)
	{ return nested ? static_cast<ParseNested*>(nested)->findMatch(opcode) : 0; }
    static bool parseInner(GenObject* nested, JsCode::JsOpcode opcode, const char*& expr)
	{ ParseNested* inner = findMatch(nested,opcode);
	    return inner && inner->parseInner(expr,opcode); }
protected:
    virtual bool isMatch(JsCode::JsOpcode opcode)
	{ return false; }
    inline bool parseInner(const char*& expr, JsCode::JsOpcode opcode)
	{ return m_code->parseInner(expr,opcode,this); }
    inline ParseNested* find(JsCode::JsOpcode opcode)
	{ return (opcode == m_opcode) ? this :
	    (m_nested ? m_nested->find(opcode) : 0); }
    inline ParseNested* findMatch(JsCode::JsOpcode opcode)
	{ return isMatch(opcode) ? this :
	    (m_nested ? m_nested->findMatch(opcode) : 0); }
private:
    JsCode* m_code;
    ParseNested* m_nested;
    JsCode::JsOpcode m_opcode;
};

#define MAKEOP(s,o) { s, JsCode::Opc ## o }
static const TokenDict s_operators[] =
{
    MAKEOP("===", EqIdentity),
    MAKEOP("!==", NeIdentity),
    MAKEOP(".", FieldOf),
    MAKEOP("in", In),
    MAKEOP("of", Of),
    { 0, 0 }
};

static const TokenDict s_unaryOps[] =
{
    MAKEOP("new", New),
    MAKEOP("typeof", Typeof),
    MAKEOP("delete", Delete),
    { 0, 0 }
};

static const TokenDict s_postfixOps[] =
{
    MAKEOP("++", IncPost),
    MAKEOP("--", DecPost),
    { 0, 0 }
};

static const TokenDict s_instr[] =
{
    MAKEOP("function", FuncDef),
    MAKEOP("for", For),
    MAKEOP("while", While),
    MAKEOP("if", If),
    MAKEOP("else", Else),
    MAKEOP("switch", Switch),
    MAKEOP("case", Case),
    MAKEOP("default", Default),
    MAKEOP("break", Break),
    MAKEOP("continue", Cont),
    MAKEOP("var", Var),
    MAKEOP("with", With),
    MAKEOP("try", Try),
    MAKEOP("catch", Catch),
    MAKEOP("finally", Finally),
    MAKEOP("throw", Throw),
    MAKEOP("return", Return),
    { 0, 0 }
};

static const TokenDict s_constants[] =
{
    MAKEOP("false", False),
    MAKEOP("true", True),
    MAKEOP("null", Null),
    MAKEOP("undefined", Undefined),
    MAKEOP("function", FuncDef),
    { 0, 0 }
};

static const TokenDict s_preProc[] =
{
    MAKEOP("#include", Include),
    MAKEOP("#require", Require),
    { 0, 0 }
};
#undef MAKEOP

#define MAKEOP(o) { "[" #o "]", JsCode::Opc ## o }
static const TokenDict s_internals[] =
{
    MAKEOP(Label),
    MAKEOP(Begin),
    MAKEOP(End),
    MAKEOP(Flush),
    MAKEOP(Jump),
    MAKEOP(JumpTrue),
    MAKEOP(JumpFalse),
    MAKEOP(JRel),
    MAKEOP(JRelTrue),
    MAKEOP(JRelFalse),
    { 0, 0 }
};
#undef MAKEOP

static const ExpNull s_null;


GenObject* JsContext::resolveTop(ObjList& stack, const String& name, GenObject* context)
{
    XDebug(DebugAll,"JsContext::resolveTop '%s'",name.c_str());
    for (ObjList* l = stack.skipNull(); l; l = l->skipNext()) {
	JsObject* jso = YOBJECT(JsObject,l->get());
	if (jso && jso->toString() == YSTRING("()") && jso->hasField(stack,name,context))
	    return jso;
    }
    return this;
}

GenObject* JsContext::resolve(ObjList& stack, String& name, GenObject* context)
{
    GenObject* obj = 0;
    if (name.find('.') < 0)
	obj = resolveTop(stack,name,context);
    else {
	ObjList* list = name.split('.',true);
	for (ObjList* l = list->skipNull(); l; ) {
	    const String* s = static_cast<const String*>(l->get());
	    ObjList* l2 = l->skipNext();
	    if (TelEngine::null(s)) {
		// consecutive dots - not good
		obj = 0;
		break;
	    }
	    if (!obj)
		obj = resolveTop(stack,*s,context);
	    if (!l2) {
		name = *s;
		break;
	    }
	    ExpExtender* ext = YOBJECT(ExpExtender,obj);
	    if (ext) {
		GenObject* adv = ext->getField(stack,*s,context);
		XDebug(DebugAll,"JsContext::resolve advanced to '%s' of %p for '%s'",
		    (adv ? adv->toString().c_str() : 0),ext,s->c_str());
		if (adv)
		    obj = adv;
		else {
		    name.clear();
		    for (; l; l = l->skipNext())
			name.append(l->get()->toString(),".");
		    break;
		}
	    }
	    l = l2;
	}
	TelEngine::destruct(list);
    }
    DDebug(DebugAll,"JsContext::resolve got '%s' %p for '%s'",
	(obj ? obj->toString().c_str() : 0),obj,name.c_str());
    return obj;
}

bool JsContext::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsContext::runFunction '%s' [%p]",oper.name().c_str(),this);
    String name = oper.name();
    GenObject* o = resolve(stack,name,context);
    if (o && o != this) {
	ExpExtender* ext = YOBJECT(ExpExtender,o);
	if (ext) {
	    ExpOperation op(oper,name);
	    return ext->runFunction(stack,op,context);
	}
	if (runStringFunction(o,name,stack,oper,context))
	    return true;
    }
    if (name == YSTRING("isNaN")) {
	bool nan = true;
	ExpOperation* op = popValue(stack,context);
	if (op)
	    nan = !op->isInteger();
	TelEngine::destruct(op);
	ExpEvaluator::pushOne(stack,new ExpOperation(nan));
	return true;
    }
    if (name == YSTRING("parseInt")) {
	long int val = ExpOperation::nonInteger();
	ExpOperation* op1 = popValue(stack,context);
	if (op1) {
	    ExpOperation* op2 = popValue(stack,context);
	    if (op2) {
		int base = op1->number();
		if (base >= 0)
		    val = op2->trimSpaces().toLong(val,base);
	    }
	    else
		val = op1->trimSpaces().toLong(val);
	    TelEngine::destruct(op2);
	}
	TelEngine::destruct(op1);
	ExpEvaluator::pushOne(stack,new ExpOperation(val));
	return true;
    }
    return JsObject::runFunction(stack,oper,context);
}

bool JsContext::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsContext::runField '%s' [%p]",oper.name().c_str(),this);
    String name = oper.name();
    GenObject* o = resolve(stack,name,context);
    if (o && o != this) {
	ExpExtender* ext = YOBJECT(ExpExtender,o);
	if (ext) {
	    ExpOperation op(oper,name);
	    return ext->runField(stack,op,context);
	}
	if (runStringField(o,name,stack,oper,context))
	    return true;
    }
    return JsObject::runField(stack,oper,context);
}

bool JsContext::runStringFunction(GenObject* obj, const String& name, ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    const String* str = YOBJECT(String,obj);
    if (!str)
	return false;
    if (name == YSTRING("charAt")) {
	int idx = 0;
	ObjList args;
	if (extractArgs(stack,oper,context,args)) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    if (op && op->isInteger())
		idx = op->number();
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(String(str->at(idx))));
	return true;
    }
    if (name == YSTRING("indexOf")) {
	int idx = -1;
	ObjList args;
	if (extractArgs(stack,oper,context,args)) {
	    const String* what = static_cast<String*>(args[0]);
	    if (what) {
		ExpOperation* from = static_cast<ExpOperation*>(args[1]);
		int offs = (from && from->isInteger()) ? from->number() : 0;
		if (offs < 0)
		    offs = 0;
		idx = str->find(*what,offs);
	    }
	}
	ExpEvaluator::pushOne(stack,new ExpOperation((long int)idx));
	return true;
    }
    if (name == YSTRING("substr")) {
	ObjList args;
	int offs = 0;
	int len = -1;
	if (extractArgs(stack,oper,context,args)) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    if (op && op->isInteger())
		offs = op->number();
	    op = static_cast<ExpOperation*>(args[1]);
	    if (op && op->isInteger()) {
		len = op->number();
		if (len < 0)
		    len = 0;
	    }
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(str->substr(offs,len)));
	return true;
    }
    if (name == YSTRING("match")) {
	ObjList args;
	String buf(*str);
	if (extractArgs(stack,oper,context,args)) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    ExpWrapper* wrap = YOBJECT(ExpWrapper,op);
	    JsRegExp* rexp = YOBJECT(JsRegExp,wrap);
	    bool ok = false;
	    if (rexp)
		ok = buf.matches(rexp->regexp());
	    else if (!wrap) {
		Regexp r(*static_cast<String*>(op),true);
		ok = buf.matches(r);
	    }
	    if (ok) {
		JsArray* jsa = new JsArray(mutex());
		for (int i = 0; i <= buf.matchCount(); i++)
		    jsa->push(new ExpOperation(buf.matchString(i)));
		jsa->params().addParam(new ExpOperation((long int)buf.matchOffset(),"index"));
		if (rexp)
		    jsa->params().addParam(wrap->clone("input"));
		ExpEvaluator::pushOne(stack,new ExpWrapper(jsa));
		return true;
	    }
	}
	ExpEvaluator::pushOne(stack,s_null.ExpOperation::clone());
	return true;
    }
#define NO_PARAM_STRING_METHOD(method) \
    { \
	ObjList args; \
	extractArgs(stack,oper,context,args); \
	String s(*str); \
	ExpEvaluator::pushOne(stack,new ExpOperation(s.method())); \
    }

    if (name == YSTRING("toLowerCase")) {
	NO_PARAM_STRING_METHOD(toLower);
	return true;
    }
    if (name == YSTRING("toUpperCase")) {
	NO_PARAM_STRING_METHOD(toUpper);
	return true;
    }
    if (name == YSTRING("trim")) {
	NO_PARAM_STRING_METHOD(trimBlanks);
	return true;
    }
#undef NO_PARAM_STRING_METHOD

#define MAKE_WITH_METHOD \
	ObjList args; \
	const char* what = 0; \
	int pos = 0; \
	if (extractArgs(stack,oper,context,args)) { \
	    if (args.skipNull()) { \
		String* tmp = static_cast<String*>(args.skipNull()->get()); \
		if (tmp) \
		    what = tmp->c_str(); \
	    } \
	    if (args.count() >= 2) { \
		String* tmp = static_cast<String*>(args[1]); \
		if (tmp) \
		    pos = tmp->toInteger(0); \
	    } \
	} \
	String s(*str); 

    if (name == YSTRING("startsWith")) {
	MAKE_WITH_METHOD;
	if (pos > 0)
	    s = s.substr(pos);
	ExpEvaluator::pushOne(stack,new ExpOperation(s.startsWith(what)));
	return true;
    }
    if (name == YSTRING("endsWith")) {
	MAKE_WITH_METHOD;
	if (pos > 0)
	    s = s.substr(0,pos);
	ExpEvaluator::pushOne(stack,new ExpOperation(s.endsWith(what)));
	return true; 
    }
#undef MAKE_WITH_METHOD
#define SPLIT_EMPTY() do { \
	array->push(new ExpOperation(*str)); \
	ExpEvaluator::pushOne(stack,new ExpWrapper(array,0)); \
	return true; \
    } while (false);
    if (name == YSTRING("split")) {
	ObjList args;
	JsArray* array = new JsArray(mutex());
	if (!(extractArgs(stack,oper,context,args) && args.skipNull()))
	    SPLIT_EMPTY();
	String* s = static_cast<String*>(args[0]);
	if (!s)
	    SPLIT_EMPTY();
	char ch = s->at(0);
	unsigned int limit = 0;
	ObjList* splits = str->split(ch);
	if (args.count() >= 2) {
	    String* l = static_cast<String*>(args[1]);
	    if (l)
		limit = l->toInteger(splits->count());
	}
	if (!limit)
	    limit = splits->count();
	int i = limit;
	for (ObjList* o = splits->skipNull();o && i > 0;o = o->skipNext(),i--) {
	    String* slice = static_cast<String*>(o->get());
	    array->push(new ExpOperation(*slice));
	}
	ExpEvaluator::pushOne(stack,new ExpWrapper(array,0));
	TelEngine::destruct(splits);
	return true;
    }
#undef SPLIT_EMPTY
    return false;
}

bool JsContext::runStringField(GenObject* obj, const String& name, ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    const String* s = YOBJECT(String,obj);
    if (!s)
	return false;
    if (name == YSTRING("length")) {
	ExpEvaluator::pushOne(stack,new ExpOperation((long int)s->length()));
	return true;
    }
    return false;
}

bool JsContext::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsContext::runAssign '%s'='%s' [%p]",oper.name().c_str(),oper.c_str(),this);
    String name = oper.name();
    GenObject* o = resolve(stack,name,context);
    if (o && o != this) {
	ExpExtender* ext = YOBJECT(ExpExtender,o);
	if (ext) {
	    ExpOperation* op = oper.clone(name);
	    bool ok = ext->runAssign(stack,*op,context);
	    TelEngine::destruct(op);
	    return ok;
	}
    }
    return JsObject::runAssign(stack,oper,context);
}


// Initialize standard globals in the execution context
bool JsCode::initialize(ScriptContext* context) const
{
    if (!context)
	return false;
    JsObject::initialize(context);
    for (ObjList* l = m_globals.skipNull(); l; l = l->skipNext()) {
	ExpOperation* op = static_cast<ExpOperation*>(l->get());
	if (!context->params().getParam(op->name()))
	    context->params().setParam(static_cast<ExpOperation*>(l->get())->clone());
    }
    return true;
}

bool JsCode::evaluate(ScriptRun& runner, ObjList& results) const
{
    if (null())
	return false;
    bool ok = m_linked.length() ? evalVector(results,&runner) : evalList(results,&runner);
    if (!ok)
	return false;
    if (static_cast<JsRunner&>(runner).m_paused)
	return true;
    if (!runAllFields(results,&runner))
	return gotError("Could not evaluate all fields");
    return true;
}

// Convert list to vector and fix label relocations
bool JsCode::link()
{
    if (!m_opcodes.skipNull())
	return false;
    m_linked.assign(m_opcodes);
    unsigned int n = m_linked.count();
    if (!n)
	return false;
    for (unsigned int i = 0; i < n; i++) {
	const ExpOperation* l = static_cast<const ExpOperation*>(m_linked[i]);
	if (!l || l->opcode() != OpcLabel)
	    continue;
	long int lbl = l->number();
	for (unsigned int j = 0; j < n; j++) {
	    const ExpOperation* jmp = static_cast<const ExpOperation*>(m_linked[j]);
	    if (!jmp || jmp->number() != lbl)
		continue;
	    Opcode op = OpcNone;
	    switch (jmp->opcode()) {
		case (Opcode)OpcJump:
		    op = (Opcode)OpcJRel;
		    break;
		case (Opcode)OpcJumpTrue:
		    op = (Opcode)OpcJRelTrue;
		    break;
		case (Opcode)OpcJumpFalse:
		    op = (Opcode)OpcJRelFalse;
		    break;
		default:
		    continue;
	    }
	    long int offs = (long int)i - j;
	    m_linked.set(new ExpOperation(op,0,offs,jmp->barrier()),j);
	}
    }
    return true;
}

void JsCode::formatLineNo(String& buf, unsigned int line) const
{
    unsigned int fnum = (line >> 24) & 0xff;
    if (!fnum)
	return ExpEvaluator::formatLineNo(buf,line);
    buf.clear();
    const GenObject* file = m_included[fnum - 1];
    buf << (file ? file->toString().c_str() : "???") << ":" << (line & 0xffffff);
}

bool JsCode::getString(const char*& expr)
{
    if (inError())
	return false;
    char c = skipComments(expr);
    if (c != '/' && c != '%')
	return ExpEvaluator::getString(expr);
    String str;
    if (!ExpEvaluator::getString(expr,str))
	return false;
    bool extended = true;
    bool insensitive = false;
    if (c == '%') {
	// dialplan pattern - turn it into a regular expression
	insensitive = true;
	String tmp = str;
	tmp.toUpper();
	str = "^";
	char last = '\0';
	int count = 0;
	bool esc = false;
	for (unsigned int i = 0; ; i++) {
	    c = tmp.at(i);
	    if (last && c != last) {
		switch (last) {
		    case 'X':
			str << "[0-9]";
			break;
		    case 'Z':
			str << "[1-9]";
			break;
		    case 'N':
			str << "[2-9]";
			break;
		    case '.':
			str << ".+";
			count = 1;
			break;
		}
		if (count > 1)
		    str << "{" << count << "}";
		last = '\0';
		count = 0;
	    }
	    if (!c) {
		str << "$";
		break;
	    }
	    switch (c) {
		case '.':
		    if (esc) {
			str << c;
			break;
		    }
		    // fall through
		case 'X':
		case 'Z':
		case 'N':
		    last = c;
		    count++;
		    break;
		case '+':
		case '*':
		    str << "\\";
		    // fall through
		default:
		    str << c;
	    }
	    esc = (c == '\\');
	}
    }
    else {
	// regexp - check for flags
	do {
	    c = *expr;
	    switch (c) {
		case 'i':
		    expr++;
		    insensitive = true;
		    break;
		case 'b':
		    expr++;
		    extended = false;
		    break;
		default:
		    c = 0;
	    }
	} while (c);
    }
    XDebug(this,DebugInfo,"Regexp '%s' flags '%s%s'",str.c_str(),
	(insensitive ? "i" : ""),(extended ? "" : "b"));
    JsRegExp* obj = new JsRegExp(0,str,str,insensitive,extended);
    addOpcode(new ExpWrapper(obj));
    return true;
}

bool JsCode::getEscape(const char*& expr, String& str, char sep)
{
    if (sep != '\'' && sep != '"') {
	// this is not a string but a regexp or dialplan template
	char c = *expr++;
	if (!c)
	    return false;
	if (c != '\\' && c != sep)
	    str << '\\';
	str << c;
	return true;
    }
    return ExpEvaluator::getEscape(expr,str,sep);
}

bool JsCode::keywordChar(char c) const
{
    return ExpEvaluator::keywordChar(c) || (c == '$');
}

int JsCode::getKeyword(const char* str) const
{
    int len = 0;
    const char*s = str;
    for (;; len++) {
	char c = *s++;
	if (c <= ' ')
	    break;
	if (keywordChar(c) || (len && (c == '.')))
	    continue;
	break;
    }
    if (len > 1 && (s[-2] == '.'))
	len--;
    if (len && ExpEvaluator::getOperator(str,s_instr) != OpcNone)
	return 0;
    return len;
}

char JsCode::skipComments(const char*& expr, GenObject* context)
{
    char c = skipWhites(expr);
    while (c == '/') {
	if (expr[1] == '/') {
	    // comment to end of line
	    expr+=2;
	    while ((c = *expr) && (c != '\r') && (c != '\n'))
		expr++;
	    c = skipWhites(expr);
	}
	else if (expr[1] == '*') {
	    /* comment to close */
	    expr++;
	    while ((c = skipWhites(expr)) && (c != '*' || expr[1] != '/'))
		expr++;
	    if (c) {
		expr+=2;
		c = skipWhites(expr);
	    }
	}
	else
	    break;
    }
    return c;
}

bool JsCode::preProcessInclude(const char*& expr, bool once, GenObject* context)
{
    if (m_depth > 5)
	return gotError("Possible recursive include");
    JsParser* parser = YOBJECT(JsParser,context);
    if (!parser)
	return false;
    char c = skipComments(expr);
    if (c == '"' || c == '\'') {
	String str;
	if (ExpEvaluator::getString(expr,str)) {
	    DDebug(this,DebugAll,"Found include '%s'",str.safe());
	    parser->adjustPath(str);
	    str.trimSpaces();
	    bool ok = !str.null();
	    if (ok) {
		int idx = m_included.index(str);
		if (!(once && (idx >= 0))) {
		    if (idx < 0) {
			String* s = new String(str);
			m_included.append(s);
			idx = m_included.index(s);
		    }
		    // use the upper bits of line # for file index
		    unsigned int savedLine = m_lineNo;
		    m_lineNo = ((idx + 1) << 24) | 1;
		    m_depth++;
		    ok = parser->parseFile(str,true);
		    m_depth--;
		    m_lineNo = savedLine;
		}
	    }
	    return ok || gotError("Failed to include " + str);
	}
	return false;
    }
    return gotError("Expecting include file",expr);
}

int JsCode::preProcess(const char*& expr, GenObject* context)
{
    int rval = -1;
    for (;;) {
	skipComments(expr);
	JsOpcode opc = (JsOpcode)ExpEvaluator::getOperator(expr,s_preProc);
	switch (opc) {
	    case OpcInclude:
	    case OpcRequire:
		if (preProcessInclude(expr,(OpcRequire == opc),context)) {
		    if (rval < 0)
			rval = 1;
		    else
			rval++;
		}
		else
		    return -1;
		break;
	    default:
		return rval;
	}
    }
}

bool JsCode::getOneInstruction(const char*& expr, GenObject* nested)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"JsCode::getOneInstruction %p '%.30s'",nested,expr);
    if (skipComments(expr) == '{') {
	if (!getInstruction(expr,0,nested))
	    return false;
    }
    else if (!runCompile(expr,";}",nested))
	return false;
    return true;
}

bool JsCode::getInstruction(const char*& expr, char stop, GenObject* nested)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"JsCode::getInstruction %p '%.1s' '%.30s'",nested,&stop,expr);
    if (skipComments(expr) == '{') {
	if (stop == ')')
	    return false;
	expr++;
	for (;;) {
	    if (!runCompile(expr,'}',nested))
		return false;
	    bool sep = false;
	    while (skipComments(expr) && getSeparator(expr,true))
		sep = true;
	    if (*expr == '}' || !sep)
		break;
	}
	if (*expr != '}')
	    return gotError("Expecting '}'",expr);
	expr++;
	return true;
    }
    else if (*expr == ';') {
	expr++;
	return true;
    }
    const char* saved = expr;
    unsigned int savedLine = m_lineNo;
    Opcode op = ExpEvaluator::getOperator(expr,s_instr);
    switch ((JsOpcode)op) {
	case (JsOpcode)OpcNone:
	    return false;
	case OpcThrow:
	    if (!runCompile(expr))
		return false;
	    addOpcode(op);
	    break;
	case OpcReturn:
	    switch (skipComments(expr)) {
		case ';':
		case '}':
		    break;
		default:
		    if (!runCompile(expr,';'))
			return false;
		    if ((skipComments(expr) != ';') && (*expr != '}'))
			return gotError("Expecting ';' or '}'",expr);
	    }
	    addOpcode(op);
	    break;
	case OpcIf:
	    return parseIf(expr,nested);
	case OpcElse:
	    expr = saved;
	    m_lineNo = savedLine;
	    return false;
	case OpcSwitch:
	    return parseSwitch(expr,nested);
	case OpcFor:
	    return parseFor(expr,nested);
	case OpcWhile:
	    return parseWhile(expr,nested);
	case OpcCase:
	    if (!ParseNested::parseInner(nested,OpcCase,expr)) {
		m_lineNo = savedLine;
		return gotError("case not inside switch",saved);
	    }
	    if (skipComments(expr) != ':')
		return gotError("Expecting ':'",expr);
	    expr++;
	    break;
	case OpcDefault:
	    if (!ParseNested::parseInner(nested,OpcDefault,expr)) {
		m_lineNo = savedLine;
		return gotError("Unexpected default instruction",saved);
	    }
	    if (skipComments(expr) != ':')
		return gotError("Expecting ':'",expr);
	    expr++;
	    break;
	case OpcBreak:
	    if (!ParseNested::parseInner(nested,OpcBreak,expr)) {
		m_lineNo = savedLine;
		return gotError("Unexpected break instruction",saved);
	    }
	    if (skipComments(expr) != ';')
		return gotError("Expecting ';'",expr);
	    break;
	case OpcCont:
	    if (!ParseNested::parseInner(nested,OpcCont,expr)) {
		m_lineNo = savedLine;
		return gotError("Unexpected continue instruction",saved);
	    }
	    if (skipComments(expr) != ';')
		return gotError("Expecting ';'",expr);
	    break;
	case OpcVar:
	    return parseVar(expr);
	case OpcTry:
	    return parseTry(expr,nested);
	case OpcFuncDef:
	    return parseFuncDef(expr,!nested);
	default:
	    break;
    }
    return true;
}

class ParseLoop : public ParseNested
{
    friend class JsCode;
public:
    inline ParseLoop(JsCode* code, GenObject* nested, JsCode::JsOpcode oper, long int lblCont, long int lblBreak)
	: ParseNested(code,nested,oper),
	  m_lblCont(lblCont), m_lblBreak(lblBreak)
	{ }
protected:
    virtual bool isMatch(JsCode::JsOpcode opcode)
	{ return JsCode::OpcBreak == opcode || JsCode::OpcCont == opcode; }
private:
    long int m_lblCont;
    long int m_lblBreak;
};

class ParseSwitch : public ParseNested
{
    friend class JsCode;
public:
    enum SwitchState {
	Before,
	InCase,
	InDefault
    };
    inline ParseSwitch(JsCode* code, GenObject* nested, long int lblBreak)
	: ParseNested(code,nested,JsCode::OpcSwitch),
	  m_lblBreak(lblBreak), m_lblDefault(0), m_state(Before)
	{ }
    inline SwitchState state() const
	{ return m_state; }
protected:
    virtual bool isMatch(JsCode::JsOpcode opcode)
	{ return JsCode::OpcCase == opcode || JsCode::OpcDefault == opcode ||
	    JsCode::OpcBreak == opcode; }
private:
    long int m_lblBreak;
    long int m_lblDefault;
    SwitchState m_state;
    ObjList m_cases;
};

// Parse keywords inner to specific instructions
bool JsCode::parseInner(const char*& expr, JsOpcode opcode, ParseNested* nested)
{
    switch (*nested) {
	case OpcFor:
	case OpcWhile:
	    {
		ParseLoop* block = static_cast<ParseLoop*>(nested);
		switch (opcode) {
		    case OpcBreak:
			XDebug(this,DebugAll,"Parsing loop:break '%.30s'",expr);
			addOpcode((Opcode)OpcJump,block->m_lblBreak);
			break;
		    case OpcCont:
			XDebug(this,DebugAll,"Parsing loop:continue '%.30s'",expr);
			addOpcode((Opcode)OpcJump,block->m_lblCont);
			break;
		    default:
			return false;
		}
	    }
	    break;
	case OpcSwitch:
	    {
		ParseSwitch* block = static_cast<ParseSwitch*>(nested);
		switch (opcode) {
		    case OpcCase:
			if (block->state() == ParseSwitch::InDefault)
			    return gotError("Encountered case after default",expr);
			if (!getSimple(expr,true))
			    return gotError("Expecting case constant",expr);
			XDebug(this,DebugAll,"Parsing switch:case: '%.30s'",expr);
			block->m_state = ParseSwitch::InCase;
			block->m_cases.append(popOpcode());
			addOpcode(OpcLabel,++m_label);
			block->m_cases.append(new ExpOperation((Opcode)OpcJumpTrue,0,m_label));
			break;
		    case OpcDefault:
			if (block->state() == ParseSwitch::InDefault)
			    return gotError("Duplicate default case",expr);
			XDebug(this,DebugAll,"Parsing switch:default: '%.30s'",expr);
			block->m_state = ParseSwitch::InDefault;
			block->m_lblDefault = ++m_label;
			addOpcode(OpcLabel,block->m_lblDefault);
			break;
		    case OpcBreak:
			XDebug(this,DebugAll,"Parsing switch:break '%.30s'",expr);
			addOpcode((Opcode)OpcJump,static_cast<ParseSwitch*>(nested)->m_lblBreak);
			break;
		    default:
			return false;
		}
	    }
	    break;
	default:
	    return false;
    }
    return true;
}

bool JsCode::parseIf(const char*& expr, GenObject* nested)
{
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    if (!runCompile(++expr,')'))
	return false;
    if (skipComments(expr) != ')')
	return gotError("Expecting ')'",expr);
    ExpOperation* cond = addOpcode((Opcode)OpcJumpFalse,++m_label);
    expr++;
    if (!getOneInstruction(expr,nested))
	return false;
    skipComments(expr);
    const char* save = expr;
    unsigned int savedLine = m_lineNo;
    if (*expr == ';')
	skipComments(++expr);
    if ((JsOpcode)ExpEvaluator::getOperator(expr,s_instr) == OpcElse) {
	ExpOperation* jump = addOpcode((Opcode)OpcJump,++m_label);
	addOpcode(OpcLabel,cond->number());
	if (!getOneInstruction(expr,nested))
	    return false;
	addOpcode(OpcLabel,jump->number());
    }
    else {
	expr = save;
	m_lineNo = savedLine;
	addOpcode(OpcLabel,cond->number());
    }
    return true;
}

bool JsCode::parseSwitch(const char*& expr, GenObject* nested)
{
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    addOpcode((Opcode)OpcBegin);
    if (!runCompile(++expr,')'))
	return false;
    if (skipComments(expr) != ')')
	return gotError("Expecting ')'",expr);
    if (skipComments(++expr) != '{')
	return gotError("Expecting '{'",expr);
    expr++;
    ExpOperation* jump = addOpcode((Opcode)OpcJump,++m_label);
    ParseSwitch parseStack(this,nested,++m_label);
    for (;;) {
	if (!runCompile(expr,'}',parseStack))
	    return false;
	bool sep = false;
	while (skipComments(expr) && getSeparator(expr,true))
	    sep = true;
	if (*expr == '}' || !sep)
	    break;
    }
    if (*expr != '}')
	return gotError("Expecting '}'",expr);
    expr++;
    // implicit break at end
    addOpcode((Opcode)OpcJump,parseStack.m_lblBreak);
    addOpcode(OpcLabel,jump->number());
    while (ExpOperation* c = static_cast<ExpOperation*>(parseStack.m_cases.remove(false))) {
	ExpOperation* j = static_cast<ExpOperation*>(parseStack.m_cases.remove(false));
	if (!j)
	    break;
	addOpcode(c,c->lineNumber());
	addOpcode((Opcode)OpcCase);
	addOpcode(j,c->lineNumber());
    }
    // if no case matched drop the expression
    addOpcode(OpcDrop);
    if (parseStack.m_lblDefault)
	addOpcode((Opcode)OpcJump,parseStack.m_lblDefault);
    addOpcode(OpcLabel,parseStack.m_lblBreak);
    addOpcode((Opcode)OpcFlush);
    return true;
}

bool JsCode::parseFor(const char*& expr, GenObject* nested)
{
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    addOpcode((Opcode)OpcBegin);
    if ((skipComments(++expr) != ';') && !runCompile(expr,')'))
	return false;
    long int cont = 0;
    long int jump = ++m_label;
    long int body = ++m_label;
    // parse initializer
    if (skipComments(expr) == ';') {
	long int check = body;
	if (skipComments(++expr) != ';') {
	    check = ++m_label;
	    addOpcode(OpcLabel,check);
	    addOpcode((Opcode)OpcBegin);
	    // parse condition
	    if (!runCompile(expr))
		return false;
	    if (skipComments(expr) != ';')
		return gotError("Expecting ';'",expr);
	    addOpcode((Opcode)OpcEnd);
	    addOpcode((Opcode)OpcJumpFalse,jump);
	}
	addOpcode((Opcode)OpcJump,body);
	if (skipComments(++expr) == ')')
	    cont = check;
	else {
	    cont = ++m_label;
	    addOpcode(OpcLabel,cont);
	    addOpcode((Opcode)OpcBegin);
	    // parse increment
	    if (!runCompile(expr,')'))
		return false;
	    addOpcode((Opcode)OpcFlush);
	    addOpcode((Opcode)OpcJump,check);
	}
    }
    else {
	cont = ++m_label;
	addOpcode(OpcLabel,cont);
	addOpcode((Opcode)OpcNext);
	addOpcode((Opcode)OpcJumpFalse,jump);
    }
    if (skipComments(expr) != ')')
	return gotError("Expecting ')'",expr);
    ParseLoop parseStack(this,nested,OpcFor,cont,jump);
    addOpcode(OpcLabel,body);
    if (!getOneInstruction(++expr,parseStack))
	return false;
    addOpcode((Opcode)OpcJump,cont);
    addOpcode(OpcLabel,jump);
    addOpcode((Opcode)OpcFlush);
    return true;
}

bool JsCode::parseWhile(const char*& expr, GenObject* nested)
{
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    addOpcode((Opcode)OpcBegin);
    long int cont = ++m_label;
    addOpcode(OpcLabel,cont);
    if (!runCompile(++expr,')'))
	return false;
    if (skipComments(expr) != ')')
	return gotError("Expecting ')'",expr);
    long int jump = ++m_label;
    addOpcode((Opcode)OpcJumpFalse,jump);
    ParseLoop parseStack(this,nested,OpcWhile,cont,jump);
    if (!getOneInstruction(++expr,parseStack))
	return false;
    addOpcode((Opcode)OpcJump,cont);
    addOpcode(OpcLabel,jump);
    addOpcode((Opcode)OpcFlush);
    return true;
}

bool JsCode::parseVar(const char*& expr)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"parseVar '%.30s'",expr);
    skipComments(expr);
    int len = ExpEvaluator::getKeyword(expr);
    if (len <= 0 || expr[len] == '(')
	return gotError("Expecting variable name",expr);
    String str(expr,len);
    if (str.toInteger(s_instr,-1) >= 0 || str.toInteger(s_constants,-1) >= 0)
	return gotError("Not a valid variable name",expr);
    DDebug(this,DebugAll,"Found variable '%s'",str.safe());
    addOpcode((Opcode)OpcVar,str);
    return true;
}

bool JsCode::parseTry(const char*& expr, GenObject* nested)
{
    addOpcode((Opcode)OpcTry);
    ParseNested parseStack(this,nested,OpcTry);
    if (!runCompile(expr,(const char*)0,parseStack))
	return false;
    skipComments(expr);
    if ((JsOpcode)ExpEvaluator::getOperator(expr,s_instr) == OpcCatch) {
	if (skipComments(expr) != '(')
	    return gotError("Expecting '('",expr);
	if (!getField(++expr))
	    return gotError("Expecting formal argument",expr);
	if (skipComments(expr) != ')')
	    return gotError("Expecting ')'",expr);
	if (!runCompile(++expr))
	    return false;
    }
    skipComments(expr);
    if ((JsOpcode)ExpEvaluator::getOperator(expr,s_instr) == OpcFinally) {
	if (!runCompile(expr))
	    return false;
    }
    return true;
}

bool JsCode::parseFuncDef(const char*& expr, bool publish)
{
    XDebug(this,DebugAll,"JsCode::parseFuncDef '%.30s'",expr);
    skipComments(expr);
    int len = getKeyword(expr);
    String name;
    if (len > 0) {
	name.assign(expr,len);
	expr += len;
    }
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    expr++;
    ObjList args;
    while (skipComments(expr) != ')') {
	len = getKeyword(expr);
	if (len > 0) {
	    args.append(new String(expr,len));
	    expr += len;
	}
	else
	    return gotError("Expecting formal argument",expr);
	if ((skipComments(expr) == ',') && (skipComments(++expr) == ')'))
	    return gotError("Expecting formal argument",expr);
    }
    if (skipComments(++expr) != '{')
	return gotError("Expecting '{'",expr);
    expr++;
    ExpOperation* jump = addOpcode((Opcode)OpcJump,++m_label);
    ExpOperation* lbl = addOpcode(OpcLabel,++m_label);
    for (;;) {
	if (!runCompile(expr,'}'))
	    return false;
	bool sep = false;
	while (skipComments(expr) && getSeparator(expr,true))
	    sep = true;
	if (*expr == '}' || !sep)
	    break;
    }
    if (*expr != '}')
	return gotError("Expecting '}'",expr);
    expr++;
    addOpcode((Opcode)OpcReturn);
    addOpcode(OpcLabel,jump->number());
    JsFunction* obj = new JsFunction(0,name,&args,lbl->number(),this);
    addOpcode(new ExpWrapper(obj,name));
    if (publish && name && obj->ref())
	m_globals.append(new ExpWrapper(obj,name));
    return true;
}

ExpEvaluator::Opcode JsCode::getOperator(const char*& expr)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getOperator '%.30s'",expr);
    skipComments(expr);
    Opcode op = ExpEvaluator::getOperator(expr,s_operators);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getOperator(expr);
}

ExpEvaluator::Opcode JsCode::getUnaryOperator(const char*& expr)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getUnaryOperator '%.30s'",expr);
    skipComments(expr);
    Opcode op = ExpEvaluator::getOperator(expr,s_unaryOps);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getUnaryOperator(expr);
}

ExpEvaluator::Opcode JsCode::getPostfixOperator(const char*& expr, int precedence)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getPostfixOperator '%.30s'",expr);
    if (skipComments(expr) == '[') {
	// The Indexing operator has maximum priority!
	// No need to check it.
	if (!runCompile(++expr,']'))
	    return OpcNone;
	if (skipComments(expr) != ']') {
	    gotError("Expecting ']'",expr);
	    return OpcNone;
	}
	expr++;
	return (Opcode)OpcIndex;
    }
    skipComments(expr);
    const char* save = expr;
    unsigned int savedLine = m_lineNo;
    Opcode op = ExpEvaluator::getOperator(expr,s_postfixOps);
    if (OpcNone != op) {
	if (getPrecedence(op) >= precedence)
	    return op;
	expr = save;
	m_lineNo = savedLine;
	return OpcNone;
    }
    return ExpEvaluator::getPostfixOperator(expr,precedence);
}

const char* JsCode::getOperator(Opcode oper) const
{
    if ((int)oper == (int)OpcIndex)
	return "[]";
    const char* tmp = ExpEvaluator::getOperator(oper);
    if (!tmp) {
	tmp = lookup(oper,s_operators);
	if (!tmp) {
	    tmp = lookup(oper,s_unaryOps);
	    if (!tmp) {
		tmp = lookup(oper,s_postfixOps);
		if (!tmp) {
		    tmp = lookup(oper,s_instr);
		    if (!tmp)
			tmp = lookup(oper,s_internals);
		}
	    }
	}
    }
    return tmp;
}

int JsCode::getPrecedence(ExpEvaluator::Opcode oper) const
{
    switch (oper) {
	case OpcEqIdentity:
	case OpcNeIdentity:
	    return 40;
	case OpcDelete:
	case OpcNew:
	case OpcTypeof:
	    return 110;
	case OpcFieldOf:
	case OpcIndex:
	    return 140;
	default:
	    return ExpEvaluator::getPrecedence(oper);
    }
}

bool JsCode::getSeparator(const char*& expr, bool remove)
{
    if (inError())
	return false;
    switch (skipComments(expr)) {
	case ']':
	case ';':
	    if (remove)
		expr++;
	    return true;
    }
    return ExpEvaluator::getSeparator(expr,remove);
}

bool JsCode::getSimple(const char*& expr, bool constOnly)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"JsCode::getSimple(%s) '%.30s'",String::boolText(constOnly),expr);
    skipComments(expr);
    const char* save = expr;
    unsigned int savedLine = m_lineNo;
    switch ((JsOpcode)ExpEvaluator::getOperator(expr,s_constants)) {
	case OpcFalse:
	    addOpcode(false);
	    return true;
	case OpcTrue:
	    addOpcode(true);
	    return true;
	case OpcNull:
	    addOpcode(s_null.ExpOperation::clone());
	    return true;
	case OpcUndefined:
	    addOpcode(new ExpWrapper(0,"undefined"));
	    return true;
	case OpcFuncDef:
	    if (constOnly) {
		expr = save;
		m_lineNo = savedLine;
		return false;
	    }
	    return parseFuncDef(expr,false);
	default:
	    break;
    }
    JsObject* jso = parseArray(expr,constOnly);
    if (!jso)
	jso = parseObject(expr,constOnly);
    if (!jso)
	return ExpEvaluator::getSimple(expr,constOnly);
    addOpcode(new ExpWrapper(ExpEvaluator::OpcCopy,jso));
    return true;
}

// Parse an inline Javascript Array: [ item1, item2, ... ]
JsObject* JsCode::parseArray(const char*& expr, bool constOnly)
{
    if (skipComments(expr) != '[')
	return 0;
    expr++;
    JsArray* jsa = new JsArray;
    for (bool first = true; ; first = false) {
	if (skipComments(expr) == ']') {
	    expr++;
	    break;
	}
	if (!first) {
	    if (*expr != ',') {
		TelEngine::destruct(jsa);
		break;
	    }
	    expr++;
	}
	bool ok = constOnly ? getSimple(expr,true) : getOperand(expr,false);
	if (!ok) {
	    TelEngine::destruct(jsa);
	    break;
	}
	ExpOperation* oper = popOpcode();
	if (oper && oper->opcode() == OpcField)
	    oper->assign(oper->name());
	jsa->push(oper);
    }
    return jsa;
}


// Parse an inline Javascript Object: { prop1: value1, "prop 2": value2, ... }
JsObject* JsCode::parseObject(const char*& expr, bool constOnly)
{
    if (skipComments(expr) != '{')
	return 0;
    expr++;
    JsObject* jso = new JsObject;
    for (bool first = true; ; first = false) {
	if (skipComments(expr) == '}') {
	    expr++;
	    break;
	}
	if (!first) {
	    if (*expr != ',') {
		TelEngine::destruct(jso);
		break;
	    }
	    expr++;
	}
	char c = skipComments(expr);
	String name;
	int len = getKeyword(expr);
	if (len > 0) {
	    name.assign(expr,len);
	    expr += len;
	}
	else if ((c != '"' && c != '\'') || !ExpEvaluator::getString(expr,name)) {
	    TelEngine::destruct(jso);
	    break;
	}
	if (skipComments(expr) != ':') {
	    TelEngine::destruct(jso);
	    break;
	}
	expr++;
	bool ok = constOnly ? getSimple(expr,true) : getOperand(expr,false);
	if (!ok) {
	    TelEngine::destruct(jso);
	    break;
	}
	ExpOperation* op = popOpcode();
	if (!op) {
	    TelEngine::destruct(jso);
	    break;
	}
	if (op->opcode() == OpcField)
	    op->assign(op->name());
	const_cast<String&>(op->name()) = name;
	jso->params().setParam(op);
    }
    return jso;
}

bool JsCode::runOperation(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    switch ((JsOpcode)oper.opcode()) {
	case OpcEqIdentity:
	case OpcNeIdentity:
	    {
		ExpOperation* op2 = popValue(stack,context);
		ExpOperation* op1 = popValue(stack,context);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		}
		bool eq = (op1->opcode() == op2->opcode());
		if (eq) {
		    ExpWrapper* w1 = YOBJECT(ExpWrapper,op1);
		    ExpWrapper* w2 = YOBJECT(ExpWrapper,op2);
		    if (w1 || w2)
			eq = w1 && w2 && w1->object() == w2->object();
		    else
			eq = (op1->number() == op2->number()) && (*op1 == *op2);
		}
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
		if ((JsOpcode)oper.opcode() == OpcNeIdentity)
		    eq = !eq;
		pushOne(stack,new ExpOperation(eq));
	    }
	    break;
	case OpcBegin:
	    pushOne(stack,new ExpOperation((Opcode)OpcBegin));
	    break;
	case OpcEnd:
	case OpcFlush:
	    {
		ExpOperation* op = 0;
		if ((JsOpcode)oper.opcode() == OpcEnd) {
		    op = popOne(stack);
		    if (op && (op->opcode() == (Opcode)OpcBegin)) {
			TelEngine::destruct(op);
			break;
		    }
		}
		bool done = false;
		ExpOperation* o;
		while ((o = static_cast<ExpOperation*>(stack.remove(false)))) {
		    done = (o->opcode() == (Opcode)OpcBegin);
		    TelEngine::destruct(o);
		    if (done)
			break;
		}
		if (!done)
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		if (op)
		    pushOne(stack,op);
	    }
	    break;
	case OpcIndex:
	    {
		ExpOperation* op2 = popValue(stack,context);
		ExpOperation* op1 = popOne(stack);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("Stack underflow",oper.lineNumber());
		}
		if (op1->opcode() != OpcField) {
		    ScriptContext* ctx = YOBJECT(ScriptContext,op1);
		    if (ctx) {
			ExpOperation fld(OpcField,*op2);
			if (ctx->runField(stack,fld,context)) {
			    TelEngine::destruct(op1);
			    TelEngine::destruct(op2);
			    break;
			}
		    }
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("Expecting field name",oper.lineNumber());
		}
		pushOne(stack,new ExpOperation(OpcField,op1->name() + "." + *op2));
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
	    }
	    break;
	case OpcFieldOf:
	    {
		ExpOperation* op2 = popOne(stack);
		ExpOperation* op1 = popOne(stack);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("Stack underflow",oper.lineNumber());
		}
		if (op2->opcode() != OpcField) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("Expecting field names",oper.lineNumber());
		}
		if (op1->opcode() != OpcField) {
		    ScriptContext* ctx = YOBJECT(ScriptContext,op1);
		    if (ctx && ctx->runField(stack,*op2,context)) {
			TelEngine::destruct(op1);
			TelEngine::destruct(op2);
			break;
		    }
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("Expecting field names",oper.lineNumber());
		}
		pushOne(stack,new ExpOperation(OpcField,op1->name() + "." + op2->name()));
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
	    }
	    break;
	case OpcTypeof:
	    {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    return gotError("Stack underflow",oper.lineNumber());
		switch (op->opcode()) {
		    case OpcPush:
		    case OpcCopy:
			{
			    const char* txt = "string";
			    ExpWrapper* w = YOBJECT(ExpWrapper,op);
			    if (w)
				txt = w->object() ? "object" : "undefined";
			    else if (op->isInteger())
				txt = "number";
			    pushOne(stack,new ExpOperation(txt));
			}
			break;
		    case OpcFunc:
			pushOne(stack,new ExpOperation("function"));
			break;
		    default:
			pushOne(stack,new ExpOperation("internal"));
		}
		TelEngine::destruct(op);
	    }
	    break;
	case OpcVar:
	    {
		for (ObjList* l = stack.skipNull(); l; l = l->skipNext()) {
		    JsObject* jso = YOBJECT(JsObject,l->get());
		    if (jso && jso->toString() == YSTRING("()")) {
			if (!jso->hasField(stack,oper.name(),context)) {
			    XDebug(this,DebugInfo,"Creating variable '%s' in scope",
				oper.name().c_str());
			    jso->params().setParam(new ExpWrapper(0,oper.name()));
			}
			break;
		    }
		}
	    }
	    break;
	case OpcNew:
	    {
		ExpOperation* op = popOne(stack);
		if (!op)
		    return gotError("Stack underflow",oper.lineNumber());
		switch (op->opcode()) {
		    case OpcField:
			break;
		    case OpcPush:
			{
			    ExpWrapper* w = YOBJECT(ExpWrapper,op);
			    if (w && w->object()) {
				pushOne(stack,op);
				return true;
			    }
			}
			// fall through
		    default:
			TelEngine::destruct(op);
			return gotError("Expecting class name",oper.lineNumber());
		}
		ExpFunction ctr(op->name(),op->number());
		TelEngine::destruct(op);
		return runOperation(stack,ctr,context);
	    }
	    break;
	case OpcThrow:
	    {
		ExpOperation* op = popOne(stack);
		if (!op)
		    return gotError("Stack underflow",oper.lineNumber());
		bool ok = false;
		while (ExpOperation* drop = popAny(stack)) {
		    JsOpcode c = (JsOpcode)drop->opcode();
		    TelEngine::destruct(drop);
		    if (c == OpcTry) {
			ok = true;
			break;
		    }
		}
		if (!ok)
		    return gotError("Uncaught exception: " + *op,oper.lineNumber());
		pushOne(stack,op);
	    }
	    break;
	case OpcReturn:
	    {
		ExpOperation* op = popValue(stack,context);
		ExpOperation* thisObj = 0;
		bool ok = false;
		while (ExpOperation* drop = popAny(stack)) {
		    ok = drop->barrier() && (drop->opcode() == OpcFunc);
		    long int lbl = drop->number();
		    if (ok && (lbl < -1)) {
			lbl = -lbl;
			XDebug(this,DebugInfo,"Returning this=%p from constructor '%s'",
			    thisObj,drop->name().c_str());
			if (thisObj) {
			    TelEngine::destruct(op);
			    op = thisObj;
			    thisObj = 0;
			}
		    }
		    if (drop->opcode() == OpcPush) {
			ExpWrapper* wrap = YOBJECT(ExpWrapper,drop);
			if (wrap && wrap->name() == YSTRING("()")) {
			    JsObject* jso = YOBJECT(JsObject,wrap->object());
			    if (jso) {
				wrap = YOBJECT(ExpWrapper,jso->params().getParam(YSTRING("this")));
				if (wrap) {
				    TelEngine::destruct(thisObj);
				    thisObj = wrap->clone(wrap->name());
				}
			    }
			}
		    }
		    TelEngine::destruct(drop);
		    if (ok) {
			ok = jumpAbsolute(lbl,context);
			break;
		    }
		}
		TelEngine::destruct(thisObj);
		if (!ok) {
		    TelEngine::destruct(op);
		    return gotError("Return outside function call",oper.lineNumber());
		}
		if (op)
		    pushOne(stack,op);
	    }
	    break;
	case OpcIn:
	case OpcOf:
	    {
		ExpOperation* obj = popOne(stack);
		ExpOperation* fld = popOne(stack);
		String name;
		if (obj && obj->opcode() == OpcField) {
		    name = obj->name();
		    bool ok = runField(stack,*obj,context);
		    TelEngine::destruct(obj);
		    obj = ok ? popOne(stack) : 0;
		}
		if (!fld || !obj) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(obj);
		    return gotError("Stack underflow",oper.lineNumber());
		}
		if (fld->opcode() != OpcField) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(obj);
		    return gotError("Expecting field name",oper.lineNumber());
		}
		bool isOf = ((JsOpcode)oper.opcode() == OpcOf);
		JsIterator* iter = 0;
		JsObject* jso = YOBJECT(JsObject,obj);
		if (jso)
		    iter = new JsIterator(*fld,jso);
		else {
		    NamedList* lst = YOBJECT(NamedList,obj);
		    if (lst)
			iter = new JsIterator(*fld,lst);
		}
		ExpWrapper* wrap = 0;
		if (iter) {
		    if (isOf)
			iter->name(name ? name : obj->name());
		    wrap = new ExpWrapper(iter);
#ifdef DEBUG
		    *wrap << fld->name() << (isOf ? " of " : " in ") << obj->name();
		    Debug(this,DebugInfo,"Created iterator: '%s'",wrap->c_str());
#endif
		}
		TelEngine::destruct(fld);
		TelEngine::destruct(obj);
		if (wrap)
		    pushOne(stack,wrap);
		else
		    return gotError("Expecting iterable object",oper.lineNumber());
	    }
	    break;
	case OpcNext:
	    {
		JsIterator* iter = 0;
		ExpOperation* op;
		while (!iter) {
		    op = popValue(stack,context);
		    if (!op)
			return gotError("Stack underflow",oper.lineNumber());
		    iter = YOBJECT(JsIterator,op);
		    if (!iter)
			TelEngine::destruct(op);
		}
		bool ok = false;
		String* n = iter->get();
		if (n) {
		    XDebug(DebugInfo,"Iterator got item: '%s'",n->c_str());
		    static const ExpOperation s_assign(OpcAssign);
		    pushOne(stack,iter->field().clone());
		    if (iter->name())
			pushOne(stack,new ExpOperation(OpcField,iter->name() + "." + *n));
		    else
			pushOne(stack,new ExpOperation(*n));
		    TelEngine::destruct(n);
		    ok = runOperation(stack,s_assign,context);
		}
		if (ok) {
		    // assign pushes the value back on stack
		    TelEngine::destruct(popOne(stack));
		    pushOne(stack,op);
		}
		else
		    TelEngine::destruct(op);
		pushOne(stack,new ExpOperation(ok));
	    }
	    break;
	case OpcCase:
	    {
		ExpOperation* cons = popValue(stack,context);
		ExpOperation* expr = popValue(stack,context);
		if (!cons || !expr) {
		    TelEngine::destruct(cons);
		    TelEngine::destruct(expr);
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		}
		bool eq = false;
		JsRegExp* rex = YOBJECT(JsRegExp,cons);
		if (rex)
		    eq = rex->regexp().matches(*expr);
		else if (expr->opcode() == cons->opcode()) {
		    ExpWrapper* w1 = YOBJECT(ExpWrapper,expr);
		    ExpWrapper* w2 = YOBJECT(ExpWrapper,cons);
		    if (w1 || w2)
			eq = w1 && w2 && w1->object() == w2->object();
		    else
			eq = (expr->number() == cons->number()) && (*expr == *cons);
		}
		if (!eq) {
		    // put expression back on stack, we'll need it later
		    pushOne(stack,expr);
		    expr = 0;
		}
		TelEngine::destruct(cons);
		TelEngine::destruct(expr);
		pushOne(stack,new ExpOperation(eq));
	    }
	    break;
	case OpcJumpTrue:
	case OpcJumpFalse:
	case OpcJRelTrue:
	case OpcJRelFalse:
	    {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    return gotError("Stack underflow",oper.lineNumber());
		bool val = op->valBoolean();
		TelEngine::destruct(op);
		switch ((JsOpcode)oper.opcode()) {
		    case OpcJumpTrue:
		    case OpcJRelTrue:
			if (!val)
			    return true;
			break;
		    case OpcJumpFalse:
		    case OpcJRelFalse:
			if (val)
			    return true;
			break;
		    default:
			break;
		}
	    }
	    // fall through
	case OpcJump:
	case OpcJRel:
	    switch ((JsOpcode)oper.opcode()) {
		case OpcJump:
		case OpcJumpTrue:
		case OpcJumpFalse:
		    return jumpToLabel(oper.number(),context) || gotError("Label not found",oper.lineNumber());
		case OpcJRel:
		case OpcJRelTrue:
		case OpcJRelFalse:
		    return jumpRelative(oper.number(),context) || gotError("Relative jump failed",oper.lineNumber());
		default:
		    return false;
	    }
	    break;
	case OpcDelete:
	{
	    ExpOperation* op = popOne(stack);
	    if (!(context && op))
		return false;
	    if (op->opcode() != OpcField) {
		pushOne(stack,new ExpOperation(true));
		TelEngine::destruct(op);
		return true;
	    }
	    ScriptRun* sr = static_cast<ScriptRun*>(context);
	    JsObject* obj = 0;
	    String name = op->name();
	    TelEngine::destruct(op);
	    JsContext* ctx = YOBJECT(JsContext,sr->context());
	    if (ctx)
		obj = YOBJECT(JsObject,ctx->resolve(stack,name,context));
	    bool ret = false;
	    if (obj && (!obj->frozen() || !obj->hasField(stack,name,context))
		    && obj->toString() != YSTRING("()")) {
		obj->params().clearParam(name);
		ret = true;
	    }
	    DDebug(DebugAll,"Deleted '%s' : %s",name.c_str(),String::boolText(ret));
	    pushOne(stack,new ExpOperation(ret));
	    return true;
	}
	case OpcCopy:
	    if (!ExpEvaluator::runOperation(stack,oper,context))
		return false;
	    resolveObjectParams(YOBJECT(JsObject,stack.get()),stack,context);
	    return true;
	default:
	    return ExpEvaluator::runOperation(stack,oper,context);
    }
    return true;
}

void JsCode::resolveObjectParams(JsObject* object, ObjList& stack, GenObject* context) const
{
    if (!(object && context))
	return;
    ScriptRun* sr = static_cast<ScriptRun*>(context);
    JsContext* ctx = YOBJECT(JsContext,sr->context());
    if (!ctx)
	return;
    for (unsigned int i = 0;i < object->params().length();i++) {
	String* param = object->params().getParam(i);
	JsObject* tmpObj = YOBJECT(JsObject,param);
	if (tmpObj) {
	    resolveObjectParams(tmpObj,stack,context);
	    continue;
	}
	ExpOperation* op = YOBJECT(ExpOperation,param);
	if (!op || op->opcode() != OpcField)
	    continue;
	String name = *op;
	JsObject* jsobj = YOBJECT(JsObject,ctx->resolve(stack,name,context));
	if (!jsobj)
	    continue;
	NamedString* ns = jsobj->getField(stack,name,context);
	if (!ns)
	    continue;
	ExpOperation* objOper = YOBJECT(ExpOperation,ns);
	NamedString* temp = 0;
	if (objOper)
	    temp = objOper->clone(op->name());
	else
	    temp = new NamedString(op->name(),*ns);
	object->params().setParam(temp);
    }
}

bool JsCode::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"JsCode::runFunction(%p,'%s' %ld, %p) ext=%p",
	&stack,oper.name().c_str(),oper.number(),context,extender());
    if (context) {
	ScriptRun* sr = static_cast<ScriptRun*>(context);
	if (sr->context()->runFunction(stack,oper,context))
	    return true;
    }
    return extender() && extender()->runFunction(stack,oper,context);
}

bool JsCode::runField(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"JsCode::runField(%p,'%s',%p) ext=%p",
	&stack,oper.name().c_str(),context,extender());
    if (context) {
	ScriptRun* sr = static_cast<ScriptRun*>(context);
	if (sr->context()->runField(stack,oper,context))
	    return true;
    }
    return extender() && extender()->runField(stack,oper,context);
}

bool JsCode::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"JsCode::runAssign('%s'='%s',%p) ext=%p",
	oper.name().c_str(),oper.c_str(),context,extender());
    if (context) {
	ScriptRun* sr = static_cast<ScriptRun*>(context);
	if (sr->context()->runAssign(stack,oper,context))
	    return true;
    }
    return extender() && extender()->runAssign(stack,oper,context);
}

bool JsCode::evalList(ObjList& stack, GenObject* context) const
{
    XDebug(this,DebugInfo,"JsCode::evalList(%p,%p)",&stack,context);
    JsRunner* runner = static_cast<JsRunner*>(context);
    const ObjList* (& opcode) = runner->m_opcode;
    while (opcode) {
	const ExpOperation* o = static_cast<const ExpOperation*>(opcode->get());
	opcode = opcode->skipNext();
	if (!runOperation(stack,*o,context))
	    return false;
	if (runner->m_paused)
	    break;
    }
    return true;
}

bool JsCode::evalVector(ObjList& stack, GenObject* context) const
{
    XDebug(this,DebugInfo,"JsCode::evalVector(%p,%p)",&stack,context);
    JsRunner* runner = static_cast<JsRunner*>(context);
    unsigned int& index = runner->m_index;
    while (index < m_linked.length()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(m_linked[index++]);
	if (o && !runOperation(stack,*o,context))
	    return false;
	if (runner->m_paused)
	    break;
    }
    return true;
}

bool JsCode::jumpToLabel(long int label, GenObject* context) const
{
    if (!context)
	return false;
    JsRunner* runner = static_cast<JsRunner*>(context);
    if (m_opcodes.skipNull()) {
	for (ObjList* l = m_opcodes.skipNull(); l; l = l->skipNext()) {
	    const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	    if (o->opcode() == OpcLabel && o->number() == label) {
		runner->m_opcode = l->skipNext();
		XDebug(this,DebugInfo,"Jumped to label %ld",label);
		return true;
	    }
	}
    }
    else {
	unsigned int n = m_linked.length();
	if (!n)
	    return false;
	for (unsigned int i = 0; i < n; i++) {
	    const ExpOperation* o = static_cast<const ExpOperation*>(m_linked[i]);
	    if (o && o->opcode() == OpcLabel && o->number() == label) {
		runner->m_index = i;
		XDebug(this,DebugInfo,"Jumped to index %u",i);
		return true;
	    }
	}
    }
    return false;
}

bool JsCode::jumpRelative(long int offset, GenObject* context) const
{
    if (!context)
	return false;
    unsigned int& index = static_cast<JsRunner*>(context)->m_index;
    long int i = index + offset;
    if (i < 0 || i > (long int)m_linked.length())
	return false;
    index = i;
    XDebug(this,DebugInfo,"Jumped relative %+ld to index %ld",offset,i);
    return true;
}

bool JsCode::jumpAbsolute(long int index, GenObject* context) const
{
    if (!context)
	return false;
    JsRunner* runner = static_cast<JsRunner*>(context);
    if (m_linked.length()) {
	if (index < 0) {
	    runner->m_index = m_linked.length();
	    return true;
	}
	if (index > (long int)m_linked.length())
	    return false;
	runner->m_index = index;
    }
    else {
	if (index < 0) {
	    runner->m_opcode = 0;
	    return true;
	}
	long int i = 0;
	for (const ObjList* l = &m_opcodes; ; l = l->next(), i++) {
	    if (i == index) {
		runner->m_opcode = l;
		break;
	    }
	    if (!l)
		break;
	}
	if (i != index)
	    return false;
    }
    XDebug(this,DebugInfo,"Jumped absolute to index %ld",index);
    return true;
}

bool JsCode::callFunction(ObjList& stack, const ExpOperation& oper, GenObject* context,
    JsFunction* func, bool constr, JsObject* thisObj) const
{
    if (!(func && context))
	return false;
    XDebug(this,DebugInfo,"JsCode::callFunction(%p,%lu,%p) in %s'%s' this=%p",
	&stack,oper.number(),context,(constr ? "constructor " : ""),
	func->toString().c_str(),thisObj);
    JsRunner* runner = static_cast<JsRunner*>(context);
    long int index = runner->m_index;
    if (!m_linked.length()) {
	const ObjList* o = runner->m_opcode;
	index = -1;
	long int i = 0;
	for (const ObjList* l = &m_opcodes; ; l = l->next(), i++) {
	    if (l == o) {
		index = i;
		break;
	    }
	    if (!l)
		break;
	}
    }
    if (index < 0) {
	Debug(this,DebugWarn,"Oops! Could not find return point!");
	return false;
    }
    ExpOperation* op = 0;
    if (constr) {
	index = -index;
	op = popOne(stack);
	if (op && !thisObj)
	    thisObj = YOBJECT(JsObject,op);
    }
    if (thisObj && !thisObj->ref())
	thisObj = 0;
    TelEngine::destruct(op);
    ObjList args;
    JsObject::extractArgs(func,stack,oper,context,args);
    return callFunction(stack,oper,context,index,func,args,thisObj,0);
}

bool JsCode::callFunction(ObjList& stack, const ExpOperation& oper, GenObject* context,
	long int retIndex, JsFunction* func, ObjList& args,
	JsObject* thisObj, JsObject* scopeObj) const
{
    pushOne(stack,new ExpOperation(OpcFunc,oper.name(),retIndex,true));
    if (scopeObj)
	pushOne(stack,new ExpWrapper(scopeObj,"()"));
    JsObject* ctxt = JsObject::buildCallContext(func->mutex(),thisObj);
    for (unsigned int idx = 0; ; idx++) {
	const String* name = func->formalName(idx);
	if (!name)
	    break;
	ExpOperation* param = static_cast<ExpOperation*>(args.remove(false));
	if (param)
	    ctxt->params().setParam(param->clone(*name));
	else
	    ctxt->params().setParam(new ExpWrapper(0,*name));
	TelEngine::destruct(param);
    }
    pushOne(stack,new ExpWrapper(ctxt,ctxt->toString(),true));
    return jumpToLabel(func->label(),context);
}

ScriptRun* JsCode::createRunner(ScriptContext* context)
{
    if (!context)
	return 0;
    return new JsRunner(this,context);
}


bool JsCode::null() const
{
    return !(m_opcodes.skipNull() || m_linked.count());
}

ScriptRun::Status JsRunner::reset(bool init)
{
    Status s = ScriptRun::reset(init);
    m_opcode = code() ? static_cast<const JsCode*>(code())->m_opcodes.skipNull() : 0;
    m_index = 0;
    return s;
}

ScriptRun::Status JsRunner::resume()
{
    Lock mylock(this);
    if (Running != state())
	return state();
    RefPointer<ScriptCode> c = code();
    if (!(c && context()))
	return Invalid;
    m_paused = false;
    mylock.drop();
    mylock.acquire(context()->mutex());
    if (!c->evaluate(*this,stack()))
	return Failed;
    return m_paused ? Incomplete : Succeeded;
}

bool JsRunner::pause()
{
    Lock mylock(this);
    if (m_paused)
	return true;
    switch (state()) {
	case Running:
	case Incomplete:
	    DDebug(DebugAll,"Pausing Javascript runner [%p]",this);
	    m_paused = true;
	    return true;
	default:
	    return false;
    }
}

ScriptRun::Status JsRunner::call(const String& name, ObjList& args,
    ExpOperation* thisObj, ExpOperation* scopeObj)
{
    Lock mylock(this);
    if (Invalid == state()) {
	TelEngine::destruct(thisObj);
	TelEngine::destruct(scopeObj);
	return Invalid;
    }
    const JsCode* c = static_cast<const JsCode*>(code());
    if (!(c && context())) {
	TelEngine::destruct(thisObj);
	TelEngine::destruct(scopeObj);
	return Invalid;
    }
    JsFunction* func = c->getGlobalFunction(name);
    if (!func) {
	TelEngine::destruct(thisObj);
	TelEngine::destruct(scopeObj);
	return Failed;
    }
    JsObject* jsThis = YOBJECT(JsObject,thisObj);
    if (jsThis && !jsThis->ref())
	jsThis = 0;
    JsObject* jsScope = YOBJECT(JsObject,scopeObj);
    if (jsScope && !jsScope->ref())
	jsScope = 0;
    TelEngine::destruct(thisObj);
    TelEngine::destruct(scopeObj);
    reset(false);
    // prepare a function call stack
    ExpOperation oper(ExpEvaluator::OpcFunc,name,args.count());
    if (!c->callFunction(stack(),oper,this,-1,func,args,jsThis,jsScope))
	return Failed;
    mylock.drop();
    // continue normal execution like in run()
    ScriptRun::Status s = state();
    while (Incomplete == s)
	s = execute();
    return s;
}

bool JsRunner::callable(const String& name)
{
    Lock mylock(this);
    if (Invalid == state())
	return false;
    const JsCode* c = static_cast<const JsCode*>(code());
    return (c && context() && c->getGlobalFunction(name));
}

}; // anonymous namespace


JsFunction::JsFunction(Mutex* mtx)
    : JsObject("Function",mtx,true),
      m_label(0), m_code(0), m_func("")
{
    init();
}

JsFunction::JsFunction(Mutex* mtx, const char* name, ObjList* args, long int lbl, ScriptCode* code)
    : JsObject(mtx,String("[function ") + name + "()]",false),
      m_label(lbl), m_code(code), m_func(name)
{
    init();
    if (args) {
	while (GenObject* arg = args->remove(false))
	    m_formal.append(arg);
    }
    unsigned int argc = m_formal.count();
    static_cast<ExpOperation&>(m_func) = argc;
    params().addParam("length",String(argc));
}

JsObject* JsFunction::copy(Mutex* mtx) const
{
    ObjList args;
    for (ObjList* l = m_formal.skipNull(); l; l = l->skipNext())
	args.append(new String(l->get()->toString()));
    return new JsFunction(mtx,0,&args,label(),m_code);
}

void JsFunction::init()
{
    params().addParam(new ExpFunction("apply"));
    params().addParam(new ExpFunction("call"));
}

bool JsFunction::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsFunction::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("apply")) {
	// func.apply(new_this,["array","of","params",...])
	if (oper.number() != 2)
	    return false;
    }
    else if (oper.name() == YSTRING("call")) {
	// func.call(new_this,param1,param2,...)
	if (!oper.number())
	    return false;
    }
    else {
	JsObject* obj = YOBJECT(JsObject,params().getParam(YSTRING("prototype")));
	return obj ? obj->runNative(stack,oper,context) : JsObject::runNative(stack,oper,context);
    }
    return true;
}

bool JsFunction::runDefined(ObjList& stack, const ExpOperation& oper, GenObject* context, JsObject* thisObj)
{
    XDebug(DebugAll,"JsFunction::runDefined() in '%s' this=%p [%p]",
	toString().c_str(),thisObj,this);
    JsObject* newObj = 0;
    JsObject* proto = YOBJECT(JsObject,getField(stack,"prototype",context));
    if (proto) {
	// found prototype, build object
	newObj = proto->runConstructor(stack,oper,context);
	if (!newObj)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpWrapper(newObj,oper.name()));
	thisObj = newObj;
    }
    JsCode* code = YOBJECT(JsCode,m_code);
    XDebug(DebugAll,"JsFunction::runDefined code=%p proto=%p %s=%p [%p]",
	code,proto,(newObj ? "new" : "this"),thisObj,this);
    if (code) {
	if (!code->callFunction(stack,oper,context,this,(proto != 0),thisObj))
	    return false;
	if (newObj && newObj->ref())
	    ExpEvaluator::pushOne(stack,new ExpWrapper(newObj,oper.name()));
	return true;
    }
    return proto || runNative(stack,oper,context);
}


// Adjust a script file include path
void JsParser::adjustPath(String& script) const
{
    if (script.null() || script.startsWith(Engine::pathSeparator()))
	return;
    script = m_basePath + script;
}

// Create Javascript context
ScriptContext* JsParser::createContext() const
{
    return new JsContext;
}

ScriptRun* JsParser::createRunner(ScriptCode* code, ScriptContext* context) const
{
    if (!code)
	return 0;
    ScriptContext* ctxt = 0;
    if (!context)
	context = ctxt = createContext();
    ScriptRun* runner = new JsRunner(code,context);
    TelEngine::destruct(ctxt);
    return runner;
}

// Check if function or method exists
bool JsParser::callable(const String& name)
{
    const JsCode* c = static_cast<const JsCode*>(code());
    return (c && c->getGlobalFunction(name));
}

// Parse a piece of Javascript text
bool JsParser::parse(const char* text, bool fragment)
{
    if (TelEngine::null(text))
	return false;
    String::stripBOM(text);
    if (fragment)
	return code() && static_cast<JsCode*>(code())->compile(text,this);
    JsCode* code = new JsCode;
    setCode(code);
    code->deref();
    if (!code->compile(text,this)) {
	setCode(0);
	return false;
    }
    DDebug(DebugAll,"Compiled: %s",code->dump().c_str());
    code->simplify();
    DDebug(DebugAll,"Simplified: %s",code->dump().c_str());
    if (m_allowLink)
	code->link();
    return true;
}

// Evaluate a string as expression or statement
ScriptRun::Status JsParser::eval(const String& text, ExpOperation** result, ScriptContext* context)
{
    if (TelEngine::null(text))
	return ScriptRun::Invalid;
    JsParser parser;
    if (!parser.parse(text))
	return ScriptRun::Invalid;
    ScriptRun* runner = parser.createRunner(context);
    ScriptRun::Status rval = runner->run();
    if (result && (ScriptRun::Succeeded == rval))
	*result = ExpEvaluator::popOne(runner->stack());
    TelEngine::destruct(runner);
    return rval;
}

// Parse JSON using native methods
JsObject* JsParser::parseJSON(const char* text)
{
    JsCode* code = new JsCode;
    JsObject* jso = code->parseObject(text,true);
    TelEngine::destruct(code);
    return jso;
}

// Return a "null" object wrapper
ExpOperation* JsParser::nullClone(const char* name)
{
    return TelEngine::null(name) ? s_null.ExpOperation::clone() : s_null.clone(name);
}

// Check if an object is identic to null
bool JsParser::isNull(const ExpOperation& oper)
{
    ExpWrapper* w = YOBJECT(ExpWrapper,&oper);
    return w && (w->object() == s_null.object());
}

// Check if an operation is undefined
bool JsParser::isUndefined(const ExpOperation& oper)
{
    ExpWrapper* w = YOBJECT(ExpWrapper,&oper);
    return w && !w->object();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
