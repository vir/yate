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
private:
    GenObject* resolveTop(ObjList& stack, const String& name, GenObject* context);
    GenObject* resolve(ObjList& stack, String& name, GenObject* context);
};

class JsCode : public ScriptCode, public ExpEvaluator
{
public:
    enum JsOpcode {
	OpcBegin = OpcPrivate + 1,
	OpcEnd,
	OpcIndex,
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
	OpcInclude,
	OpcRequire,
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
    bool link();
    JsObject* parseArray(const char*& expr, bool constOnly);
    JsObject* parseObject(const char*& expr, bool constOnly);
protected:
    virtual bool getString(const char*& expr);
    virtual bool getEscape(const char*& expr, String& str, char sep);
    virtual bool keywordChar(char c) const;
    virtual int getKeyword(const char* str) const;
    virtual char skipComments(const char*& expr, GenObject* context = 0) const;
    virtual int preProcess(const char*& expr, GenObject* context = 0);
    virtual bool getInstruction(const char*& expr, Opcode nested);
    virtual bool getNumber(const char*& expr);
    virtual Opcode getOperator(const char*& expr);
    virtual Opcode getUnaryOperator(const char*& expr);
    virtual Opcode getPostfixOperator(const char*& expr);
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
    bool preProcessInclude(const char*& expr, bool once, GenObject* context);
    bool evalList(ObjList& stack, GenObject* context) const;
    bool evalVector(ObjList& stack, GenObject* context) const;
    bool jumpToLabel(long int label, GenObject* context) const;
    bool jumpRelative(long int offset, GenObject* context) const;
    long int m_label;
    int m_depth;
};

class JsRunner : public ScriptRun
{
    friend class JsCode;
public:
    inline JsRunner(ScriptCode* code, ScriptContext* context)
	: ScriptRun(code,context),
	  m_paused(false), m_opcode(0), m_index(0)
	{ }
    virtual Status reset();
    virtual Status resume();
private:
    bool m_paused;
    ObjList* m_opcode;
    unsigned int m_index;
};

#define MAKEOP(s,o) { s, JsCode::Opc ## o }
static const TokenDict s_operators[] =
{
    { 0, 0 }
};

static const TokenDict s_unaryOps[] =
{
    MAKEOP("new", New),
    MAKEOP("typeof", Typeof),
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
    MAKEOP("in", In),
    MAKEOP("var", Var),
    MAKEOP("with", With),
    MAKEOP("try", Try),
    MAKEOP("catch", Catch),
    MAKEOP("finally", Finally),
    MAKEOP("throw", Throw),
    MAKEOP("return", Return),
    { 0, 0 }
};

static const TokenDict s_bools[] =
{
    MAKEOP("false", False),
    MAKEOP("true", True),
    { 0, 0 }
};

static const TokenDict s_preProc[] =
{
    MAKEOP("#include", Include),
    MAKEOP("#require", Require),
    { 0, 0 }
};
#undef MAKEOP

GenObject* JsContext::resolveTop(ObjList& stack, const String& name, GenObject* context)
{
    XDebug(DebugAll,"JsContext::resolveTop '%s'",name.c_str());
    for (ObjList* l = stack.skipNull(); l; l = l->skipNext()) {
	JsObject* jso = YOBJECT(JsObject,l->get());
	if (jso && jso->hasField(stack,name,context))
	    return jso;
    }
    return this;
}

GenObject* JsContext::resolve(ObjList& stack, String& name, GenObject* context)
{
    if (name.find('.') < 0)
	return resolveTop(stack,name,context);
    ObjList* list = name.split('.',true);
    GenObject* obj = 0;
    for (ObjList* l = list->skipNull(); l; ) {
	const String* s = static_cast<const String*>(l->get());
	l = l->skipNext();
	if (TelEngine::null(s)) {
	    // consecutive dots - not good
	    obj = 0;
	    break;
	}
	if (!obj)
	    obj = resolveTop(stack,*s,context);
	if (!l) {
	    name = *s;
	    break;
	}
	ExpExtender* ext = YOBJECT(ExpExtender,obj);
	if (ext)
	    obj = ext->getField(stack,*s,context);
    }
    TelEngine::destruct(list);
    XDebug(DebugAll,"JsContext::resolve got '%s' %p for '%s'",
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
    }
    return JsObject::runField(stack,oper,context);
}

bool JsContext::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsContext::runAssign '%s'='%s' [%p]",oper.name().c_str(),oper.c_str(),this);
    String name = oper.name();
    GenObject* o = resolve(stack,name,context);
    if (o && o != this) {
	ExpExtender* ext = YOBJECT(ExpExtender,o);
	if (ext) {
	    ExpOperation op(oper,name);
	    return ext->runAssign(stack,op,context);
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
    if (!m_opcodes.count())
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
	for (unsigned int j = 0; j < n; i++) {
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
	    long int offs = i - j;
	    m_linked.set(new ExpOperation(op,0,offs,jmp->barrier()),j);
	}
    }
    return true;
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
    addOpcode(str);
    //m_opcodes.append(new ExpWrapper(obj));
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

char JsCode::skipComments(const char*& expr, GenObject* context) const
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
	    while ((c = *expr) && (c != '*' || expr[1] != '/'))
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
	char sep = c;
	const char* start = ++expr;
	while ((c = *expr++)) {
	    if (c != sep)
		continue;
	    String str(start,expr-start-1);
	    DDebug(this,DebugAll,"Found include '%s'",str.safe());
	    parser->adjustPath(str);
	    str.trimSpaces();
	    bool ok = !str.null();
	    if (ok) {
		bool already = m_included.find(str);
		if (!(once && already)) {
		    m_depth++;
		    ok = parser->parseFile(str,true);
		    m_depth--;
		    if (ok && !already)
			m_included.append(new String(str));
		}
	    }
	    return ok || gotError("Failed to include " + str);
	}
	expr--;
	return gotError("Expecting string end");
    }
    return gotError("Expecting include file",expr);
}

int JsCode::preProcess(const char*& expr, GenObject* context)
{
    int rval = -1;
    for (;;) {
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

bool JsCode::getInstruction(const char*& expr, Opcode nested)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"JsCode::getInstruction '%.30s' %u",expr,nested);
    if (skipComments(expr) == '{') {
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
    const char* saved = expr;
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
	    runCompile(expr);
	    addOpcode(op);
	    break;
	case OpcIf:
	    if (skipComments(expr) != '(')
		return gotError("Expecting '('",expr);
	    if (!runCompile(++expr,')'))
		return false;
	    if (skipComments(expr) != ')')
		return gotError("Expecting ')'",expr);
	    {
		ExpOperation* cond = addOpcode((Opcode)OpcJumpFalse,++m_label);
		if (!runCompile(++expr,';'))
		    return false;
		if (skipComments(expr) == ';')
		    expr++;
		const char* save = expr;
		if ((JsOpcode)ExpEvaluator::getOperator(expr,s_instr) == OpcElse) {
		    ExpOperation* jump = addOpcode((Opcode)OpcJump,++m_label);
		    addOpcode(OpcLabel,cond->number());
		    if (!runCompile(++expr))
			return false;
		    if (skipComments(expr) == ';')
			expr++;
		    addOpcode(OpcLabel,jump->number());
		}
		else {
		    expr = save;
		    addOpcode(OpcLabel,cond->number());
		}
	    }
	    break;
	case OpcElse:
	    expr = saved;
	    return false;
	case OpcWhile:
	    {
		ExpOperation* lbl = addOpcode(OpcLabel,++m_label);
		if (skipComments(expr) != '(')
		    return gotError("Expecting '('",expr);
		if (!runCompile(++expr,')',op))
		    return false;
		if (skipComments(expr) != ')')
		    return gotError("Expecting ')'",expr);
		ExpOperation* jump = addOpcode((Opcode)OpcJumpFalse,++m_label);
		if (!runCompile(++expr))
		    return false;
		addOpcode((Opcode)OpcJump,lbl->number());
		addOpcode(OpcLabel,jump->number());
	    }
	    break;
	case OpcCase:
	    if (OpcSwitch != (JsOpcode)nested)
		return gotError("Case not in switch",saved);
	    break;
	case OpcDefault:
	    if (OpcSwitch != (JsOpcode)nested)
		return gotError("Default not in switch",saved);
	    break;
	case OpcTry:
	    addOpcode(op);
	    if (!runCompile(expr,0,op))
		return false;
	    {
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
		if ((JsOpcode)ExpEvaluator::getOperator(expr,s_instr) == OpcFinally) {
		    if (!runCompile(expr))
			return false;
		}
	    }
	case OpcFuncDef:
	    {
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
		ExpOperation* jump = addOpcode((Opcode)OpcJump,++m_label);
		while (skipComments(expr) != ')') {
		    if (!getField(expr))
			return gotError("Expecting formal argument",expr);
		    if (skipComments(expr) == ',')
			expr++;
		}
		expr++;
		if (skipComments(expr) != '{')
		    return gotError("Expecting '{'",expr);
		expr++;
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
	    }
	    break;
	default:
	    break;
    }
    return true;
}

bool JsCode::getNumber(const char*& expr)
{
    if (inError())
	return false;
    switch ((JsOpcode)ExpEvaluator::getOperator(expr,s_bools)) {
	case OpcFalse:
	    addOpcode(false);
	    return true;
	case OpcTrue:
	    addOpcode(true);
	    return true;
	default:
	    break;
    }
    return ExpEvaluator::getNumber(expr);
}

ExpEvaluator::Opcode JsCode::getOperator(const char*& expr)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getOperator '%.30s'",expr);
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
    Opcode op = ExpEvaluator::getOperator(expr,s_unaryOps);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getUnaryOperator(expr);
}

ExpEvaluator::Opcode JsCode::getPostfixOperator(const char*& expr)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getPostfixOperator '%.30s'",expr);
    if (skipComments(expr) == '[') {
	if (!runCompile(++expr,']'))
	    return OpcNone;
	if (skipComments(expr) != ']') {
	    gotError("Expecting ']'",expr);
	    return OpcNone;
	}
	expr++;
	return (Opcode)OpcIndex;
    }
    Opcode op = ExpEvaluator::getOperator(expr,s_postfixOps);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getPostfixOperator(expr);
}

const char* JsCode::getOperator(Opcode oper) const
{
    if (oper < OpcPrivate)
	return ExpEvaluator::getOperator(oper);
    if ((int)oper == (int)OpcIndex)
	return "[]";
    const char* tmp = lookup(oper,s_operators);
    if (!tmp) {
	tmp = lookup(oper,s_unaryOps);
	if (!tmp) {
	    tmp = lookup(oper,s_postfixOps);
	    if (!tmp)
		tmp = lookup(oper,s_instr);
	}
    }
    return tmp;
}

int JsCode::getPrecedence(ExpEvaluator::Opcode oper) const
{
    switch (oper) {
	case OpcNew:
	case OpcIndex:
	    return 12;
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

// Parse an inline Javascript Array: [ item1, item2, ... ]
JsObject* JsCode::parseArray(const char*& expr, bool constOnly)
{
    if (skipComments(expr) != '[')
	return 0;
    expr++;
    JsObject* jso = new JsObject;
    do {
	bool ok = constOnly ? (getNumber(expr) || getString(expr)) : getOperand(expr,false);
	if (!ok) {
	    TelEngine::destruct(jso);
	    break;
	}
    } while (jso && (skipComments(expr) == ',') && expr++);
    if (skipComments(expr) != ']')
	TelEngine::destruct(jso);
    return jso;
}


// Parse an inline Javascript Object: { prop1: value1, prop2: value2, ... }
JsObject* JsCode::parseObject(const char*& expr, bool constOnly)
{
    if (skipComments(expr) != '{')
	return 0;
    expr++;
    JsObject* jso = new JsObject;
    do {
	skipComments(expr);
	int len = getKeyword(expr);
	if (len <= 0) {
	    TelEngine::destruct(jso);
	    break;
	}
	String name;
	name.assign(expr,len);
	expr += len;
	if (skipComments(expr) != ':') {
	    TelEngine::destruct(jso);
	    break;
	}
	expr++;
	bool ok = constOnly ? (getNumber(expr) || getString(expr)) : getOperand(expr,false);
	if (!ok) {
	    TelEngine::destruct(jso);
	    break;
	}
    } while (jso && (skipComments(expr) == ',') && expr++);
    if (skipComments(expr) != '}')
	TelEngine::destruct(jso);
    return jso;
}

bool JsCode::runOperation(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    switch ((JsOpcode)oper.opcode()) {
	case OpcBegin:
	    pushOne(stack,new ExpOperation((Opcode)OpcBegin));
	    break;
	case OpcEnd:
	    {
		ExpOperation* op = popOne(stack);
		ObjList* b = 0;
		for (ObjList* l = stack.skipNull(); l; l=l->skipNext()) {
		    ExpOperation* o = static_cast<ExpOperation*>(l->get());
		    if (o && (o->opcode() == (Opcode)OpcBegin))
			b = l;
		}
		if (!b) {
		    TelEngine::destruct(op);
		    return gotError("ExpEvaluator stack underflow");
		}
		b->clear();
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
		    return gotError("Stack underflow");
		}
		if (op1->opcode() != OpcField) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("Expecting field name");
		}
		pushOne(stack,new ExpOperation(OpcField,op1->name() + "." + *op2));
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
	    }
	    break;
	case OpcTypeof:
	    {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    return gotError("Stack underflow");
		switch (op->opcode()) {
		    case OpcPush:
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
	case OpcNew:
	    {
		ExpOperation* op = popOne(stack);
		if (!op)
		    return gotError("Stack underflow");
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
			return gotError("Expecting class name");
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
		    return gotError("Stack underflow");
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
		    return gotError("Uncaught exception: " + *op);
		pushOne(stack,op);
	    }
	    break;
	case OpcReturn:
	    {
		ExpOperation* op = popOne(stack);
		bool ok = false;
		while (ExpOperation* drop = popAny(stack)) {
		    ok = drop->opcode() == OpcFunc;
		    int n = drop->number();
		    TelEngine::destruct(drop);
		    if (ok) {
			DDebug(this,DebugAll,"return popping %d off stack",n);
			while (n-- > 0)
			    TelEngine::destruct(popAny(stack));
			break;
		    }
		}
		if (!ok) {
		    TelEngine::destruct(op);
		    return gotError("Return outside function call");
		}
		pushOne(stack,op);
	    }
	    break;
	case OpcJumpTrue:
	case OpcJumpFalse:
	case OpcJRelTrue:
	case OpcJRelFalse:
	    {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    return gotError("Stack underflow");
		bool val = op->number() != 0;
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
		    return jumpToLabel(oper.number(),context) || gotError("Label not found");
		case OpcJRel:
		case OpcJRelTrue:
		case OpcJRelFalse:
		    return jumpRelative(oper.number(),context) || gotError("Relative jump failed");
		default:
		    return false;
	    }
	    break;
	default:
	    return ExpEvaluator::runOperation(stack,oper,context);
    }
    return true;
}

bool JsCode::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"runFunction(%p,'%s' %ld, %p) ext=%p",
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
    DDebug(this,DebugAll,"runField(%p,'%s',%p) ext=%p",
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
    DDebug(this,DebugAll,"runAssign('%s'='%s',%p) ext=%p",
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
    XDebug(this,DebugInfo,"evalList(%p,%p)",&stack,context);
    JsRunner* runner = static_cast<JsRunner*>(context);
    ObjList*& opcode = runner->m_opcode;
    if (!opcode)
	opcode = m_opcodes.skipNull();
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
    XDebug(this,DebugInfo,"evalVector(%p,%p)",&stack,context);
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
    for (ObjList* l = m_opcodes.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	if (o->opcode() == OpcLabel && o->number() == label) {
	    runner->m_opcode = l;
	    return true;
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
    return true;
}


ScriptRun::Status JsRunner::reset()
{
    Status s = ScriptRun::reset();
    m_opcode = 0;
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
    if (!c->evaluate(*this,stack()))
	return Failed;
    return m_paused ? Incomplete : Succeeded;
}

}; // anonymous namespace


// Adjust a script file include path
void JsParser::adjustPath(String& script)
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

// Parse a piece of Javascript text
bool JsParser::parse(const char* text, bool fragment)
{
    if (TelEngine::null(text))
	return false;
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

/* vi: set ts=8 sw=4 sts=4 noet: */
