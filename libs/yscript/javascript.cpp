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

using namespace TelEngine;

namespace { // anonymous

class JsContext : public ScriptContext
{
    YCLASS(JsContext,ScriptContext)
public:
    virtual bool runFunction(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context);
    virtual bool runField(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context);
    virtual bool runAssign(const ExpEvaluator* eval, const ExpOperation& oper, void* context);
};

class JsCode : public ScriptCode, public ExpEvaluator
{
    YCLASS(JsCode,ScriptCode)
public:
    enum JsOpcode {
	OpcBegin = OpcPrivate + 1,
	OpcEnd,
	OpcIndex,
	OpcNew,
	OpcFor,
	OpcWhile,
	OpcIf,
	OpcElse,
	OpcSwitch,
	OpcCase,
	OpcBreak,
	OpcCont,
	OpcIn,
	OpcVar,
	OpcWith,
	OpcTry,
	OpcCatch,
	OpcFinally,
	OpcThrow,
	OpcReturn,
    };
    inline JsCode()
	: ExpEvaluator(C), m_label(0)
	  { debugName("JsCode"); }
    virtual bool initialize(ScriptContext* context) const;
    virtual bool evaluate(ScriptContext& context, ObjList& results) const;
protected:
    virtual bool keywordChar(char c) const;
    virtual int getKeyword(const char* str) const;
    virtual bool getInstruction(const char*& expr);
    virtual Opcode getOperator(const char*& expr);
    virtual Opcode getUnaryOperator(const char*& expr);
    virtual Opcode getPostfixOperator(const char*& expr);
    virtual const char* getOperator(Opcode oper) const;
    virtual int getPrecedence(ExpEvaluator::Opcode oper) const;
    virtual bool getSeparator(const char*& expr, bool remove);
    virtual bool runOperation(ObjList& stack, const ExpOperation& oper, void* context) const;
private:
    int m_label;
};

#define MAKEOP(s,o) { s, JsCode::Opc ## o }
static const TokenDict s_operators[] =
{
    { 0, 0 }
};

static const TokenDict s_unaryOps[] =
{
    MAKEOP("new", New),
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
    MAKEOP("function", Func),
    MAKEOP("for", For),
    MAKEOP("while", While),
    MAKEOP("if", If),
    MAKEOP("else", Else),
    MAKEOP("switch", Switch),
    MAKEOP("case", Case),
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
#undef MAKEOP


bool JsContext::runFunction(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context)
{
    return ScriptContext::runFunction(eval,stack,oper,context);
}

bool JsContext::runField(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context)
{
    if (!eval)
	return false;
    XDebug(DebugAll,"JsContext::runField '%s'",oper.name().c_str());
    return ScriptContext::runField(eval,stack,oper,context);
}

bool JsContext::runAssign(const ExpEvaluator* eval, const ExpOperation& oper, void* context)
{
    if (!eval)
	return false;
    XDebug(DebugAll,"JsContext::runAssign '%s'='%s'",oper.name().c_str(),oper.c_str());
    return ScriptContext::runAssign(eval,oper,context);
}


// Initialize standard globals in the execution context
bool JsCode::initialize(ScriptContext* context) const
{
    if (!context)
	return false;
    JsObject::initialize(*context);
    return true;
}

bool JsCode::evaluate(ScriptContext& context, ObjList& results) const
{
    if (null())
	return false;
    return ExpEvaluator::evaluate(results,&context);
}

bool JsCode::keywordChar(char c) const
{
    return ExpEvaluator::keywordChar(c) || (c == '$');
}

int JsCode::getKeyword(const char* str) const
{
    int len = 0;
    for (;; len++) {
	char c = *str++;
	if (c <= ' ')
	    break;
	if (keywordChar(c) || (len && (c == '.')))
	    continue;
	break;
    }
    if (len > 1 && (str[-2] == '.'))
	len--;
    return len;
}

bool JsCode::getInstruction(const char*& expr)
{
    XDebug(this,DebugAll,"JsCode::getInstruction '%s'",expr);
    if (skipWhites(expr) == '{') {
	if (!runCompile(++expr,'}'))
	    return false;
	if (skipWhites(expr) != '}')
	    return gotError("Expecting '}'",expr);
	expr++;
	return true;
    }
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
	default:
	    break;
    }
    return true;
}

ExpEvaluator::Opcode JsCode::getOperator(const char*& expr)
{
    XDebug(this,DebugAll,"JsCode::getOperator '%s'",expr);
    Opcode op = ExpEvaluator::getOperator(expr,s_operators);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getOperator(expr);
}

ExpEvaluator::Opcode JsCode::getUnaryOperator(const char*& expr)
{
    XDebug(this,DebugAll,"JsCode::getUnaryOperator '%s'",expr);
    Opcode op = ExpEvaluator::getOperator(expr,s_unaryOps);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getUnaryOperator(expr);
}

ExpEvaluator::Opcode JsCode::getPostfixOperator(const char*& expr)
{
    XDebug(this,DebugAll,"JsCode::getPostfixOperator '%s'",expr);
    if (skipWhites(expr) == '[') {
	if (!runCompile(++expr,']'))
	    return OpcNone;
	if (skipWhites(expr) != ']') {
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
    switch (skipWhites(expr)) {
	case ']':
	case ';':
	    if (remove)
		expr++;
	    return true;
    }
    return ExpEvaluator::getSeparator(expr,remove);
}

bool JsCode::runOperation(ObjList& stack, const ExpOperation& oper, void* context) const
{
    switch ((JsOpcode)oper.opcode()) {
	case OpcBegin:
	    pushOne(stack,new ExpOperation(OpcBegin));
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
	case OpcNew:
	    {
		ExpOperation* op = popOne(stack);
		if (!op)
		    return gotError("Stack underflow");
		if (op->opcode() != OpcField) {
		    TelEngine::destruct(op);
		    return gotError("Expecting class name");
		}
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
		    return gotError("'try' not found");
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
		    return gotError("Function not found on stack");
		}
		pushOne(stack,op);
	    }
	    break;
	default:
	    return ExpEvaluator::runOperation(stack,oper);
    }
    return true;
}

}; // anonymous namespace


// Parse a piece of Javascript text
bool JsParser::parse(const char* text)
{
    if (TelEngine::null(text))
	return false;
    // TODO
    return false;
}

// Evaluate a string as expression or statement
ScriptRun::Status JsParser::eval(const String& text, ExpOperation** result, ScriptContext* context)
{
    if (TelEngine::null(text))
	return ScriptRun::Invalid;
    JsCode* code = new JsCode;
    if (!code->compile(text)) {
	TelEngine::destruct(code);
	return ScriptRun::Invalid;
    }
    DDebug(DebugAll,"Compiled: %s",code->dump().c_str());
    code->simplify();
    DDebug(DebugAll,"Simplified: %s",code->dump().c_str());
    ScriptContext* ctxt = 0;
    if (!context)
	context = ctxt = new JsContext();
    ScriptRun* runner = new ScriptRun(code,context);
    TelEngine::destruct(ctxt);
    code->extender(runner->context());
    TelEngine::destruct(code);
    ScriptRun::Status rval = runner->run();
    if (result && (ScriptRun::Succeeded == rval))
	*result = ExpEvaluator::popOne(runner->stack());
    TelEngine::destruct(runner);
    return rval;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
