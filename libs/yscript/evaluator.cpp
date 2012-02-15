/**
 * Evaluator.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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

#include "yateclass.h"
#include "yatescript.h"

#include <stdlib.h>
#include <string.h>

using namespace TelEngine;

#define MAKEOP(s,o) { s, ExpEvaluator::Opc ## o }
#define ASSIGN(s,o) { s "=", ExpEvaluator::Opc ## o | ExpEvaluator::OpcAssign }
static const TokenDict s_operators_c[] =
{
    ASSIGN("<<",Shl),
    ASSIGN(">>",Shr),
    ASSIGN("+", Add),
    ASSIGN("-", Sub),
    ASSIGN("*", Mul),
    ASSIGN("/", Div),
    ASSIGN("%", Mod),
    ASSIGN("&", And),
    ASSIGN("|", Or),
    ASSIGN("^", Xor),
    MAKEOP("<<",Shl),
    MAKEOP(">>",Shr),
    MAKEOP("==",Eq),
    MAKEOP("!=",Ne),
    MAKEOP("<=",Le),
    MAKEOP(">=",Ge),
    MAKEOP("<",Lt),
    MAKEOP(">",Gt),
    MAKEOP("&&",LAnd),
    MAKEOP("||",LOr),
    MAKEOP("^^",LXor),
    MAKEOP("+", Add),
    MAKEOP("-", Sub),
    MAKEOP("*", Mul),
    MAKEOP("/", Div),
    MAKEOP("%", Mod),
    MAKEOP("&", And),
    MAKEOP("|", Or),
    MAKEOP("^", Xor),
    MAKEOP(".", Cat),
    MAKEOP("@", As),
    MAKEOP("=", Assign),
    { 0, 0 }
};

static const TokenDict s_unaryOps_c[] =
{
    MAKEOP("++", IncPre),
    MAKEOP("--", DecPre),
    MAKEOP("!", LNot),
    MAKEOP("~", Not),
    MAKEOP("-", Neg),
    { 0, 0 }
};

const TokenDict s_operators_sql[] =
{
    MAKEOP("AND",LAnd),
    MAKEOP("OR",LOr),
    MAKEOP("<<",Shl),
    MAKEOP(">>",Shr),
    MAKEOP("<>",Ne),
    MAKEOP("!=",Ne),
    MAKEOP("<=",Le),
    MAKEOP(">=",Ge),
    MAKEOP("<",Lt),
    MAKEOP(">",Gt),
    MAKEOP("||",Cat),
    MAKEOP("AS",As),
    MAKEOP("+", Add),
    MAKEOP("-", Sub),
    MAKEOP("*", Mul),
    MAKEOP("/", Div),
    MAKEOP("%", Mod),
    MAKEOP("&", And),
    MAKEOP("|", Or),
    MAKEOP("^", Xor),
    MAKEOP("=",Eq),
    { 0, 0 }
};

static const TokenDict s_unaryOps_sql[] =
{
    MAKEOP("NOT", LNot),
    MAKEOP("~", Not),
    MAKEOP("-", Neg),
    { 0, 0 }
};

#undef MAKEOP
#undef ASSIGN


bool ExpExtender::runFunction(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context)
{
    return false;
}

bool ExpExtender::runField(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context)
{
    return false;
}

bool ExpExtender::runAssign(const ExpEvaluator* eval, const ExpOperation& oper, void* context)
{
    return false;
}


ExpEvaluator::ExpEvaluator(const TokenDict* operators, const TokenDict* unaryOps)
    : m_operators(operators), m_unaryOps(unaryOps), m_extender(0)
{
}

ExpEvaluator::ExpEvaluator(ExpEvaluator::Parser style)
    : m_operators(0), m_unaryOps(0), m_extender(0)
{
    switch (style) {
	case C:
	    m_operators = s_operators_c;
	    m_unaryOps = s_unaryOps_c;
	    break;
	case SQL:
	    m_operators = s_operators_sql;
	    m_unaryOps = s_unaryOps_sql;
	    break;
    }
}

ExpEvaluator::ExpEvaluator(const ExpEvaluator& original)
    : m_operators(original.m_operators), m_unaryOps(original.unaryOps()),
      m_extender(0)
{
    extender(original.extender());
    for (ObjList* l = original.m_opcodes.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	m_opcodes.append(new ExpOperation(*o));
    }
}

ExpEvaluator::~ExpEvaluator()
{
    extender(0);
}

void ExpEvaluator::extender(ExpExtender* ext)
{
    if (ext == m_extender)
	return;
    if (ext && !ext->ref())
	return;
    ExpExtender* tmp = m_extender;
    m_extender = ext;
    TelEngine::destruct(tmp);
}

char ExpEvaluator::skipWhites(const char*& expr)
{
    if (!expr)
	return 0;
    while (*expr==' ' || *expr=='\t')
	expr++;
    return *expr;
}

bool ExpEvaluator::keywordChar(char c) const
{
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
	('0' <= c && c <= '9') || (c == '_');
}

ExpEvaluator::Opcode ExpEvaluator::getOperator(const char*& expr, const TokenDict* operators, bool caseInsensitive) const
{
    XDebug(this,DebugAll,"getOperator('%s',%p,%s)",expr,operators,String::boolText(caseInsensitive));
    skipWhites(expr);
    if (operators) {
	bool kw = keywordChar(*expr);
	for (const TokenDict* o = operators; o->token; o++) {
	    const char* s1 = o->token;
	    const char* s2 = expr;
	    do {
		if (!*s1) {
		    if (kw && keywordChar(*s2))
			break;
		    expr = s2;
		    return (ExpEvaluator::Opcode)o->value;
		}
	    } while (condLower(*s1++,caseInsensitive) == condLower(*s2++,caseInsensitive));
	}
    }
    return OpcNone;
}

bool ExpEvaluator::gotError(const char* error, const char* text) const
{
    if (!error)
	error = "unknown error";
    Debug(this,DebugWarn,"Evaluator got error: %s%s%s",error,
	(text ? " at: " : ""),
	c_safe(text));
    return false;
}

bool ExpEvaluator::getInstruction(const char*& expr)
{
    return false;
}

bool ExpEvaluator::getOperand(const char*& expr)
{
    XDebug(this,DebugAll,"getOperand '%s'",expr);
    char c = skipWhites(expr);
    if (!c)
	// end of string
	return true;
    if (c == '(') {
	// parenthesized subexpression
	if (!runCompile(++expr,')'))
	    return false;
	if (skipWhites(expr) != ')')
	    return gotError("Expecting ')'",expr);
	expr++;
	return true;
    }
    Opcode op = getUnaryOperator(expr);
    if (op != OpcNone) {
	if (!getOperand(expr))
	    return false;
	addOpcode(op);
	return true;
    }
    if (getString(expr) || getNumber(expr) || getFunction(expr) || getField(expr))
	return true;
    return gotError("Expecting operand",expr);
}

bool ExpEvaluator::getNumber(const char*& expr)
{
    XDebug(this,DebugAll,"getNumber '%s'",expr);
    char* endp = 0;
    long int val = ::strtol(expr,&endp,0);
    if (!endp || (endp == expr))
	return false;
    expr = endp;
    DDebug(this,DebugAll,"Found %ld",val);
    addOpcode(val);
    return true;
}

bool ExpEvaluator::getString(const char*& expr)
{
    XDebug(this,DebugAll,"getString '%s'",expr);
    char c = skipWhites(expr);
    if (c == '"' || c == '\'') {
	char sep = c;
	const char* start = ++expr;
	while ((c = *expr++)) {
	    if (c != sep)
		continue;
	    String str(start,expr-start-1);
	    DDebug(this,DebugAll,"Found '%s'",str.safe());
	    addOpcode(str);
	    return true;
	}
	return gotError("Expecting string end");
    }
    return false;
}

int ExpEvaluator::getKeyword(const char* str) const
{
    int len = 0;
    for (;; len++) {
	char c = *str++;
	if (c <= ' ' || !keywordChar(c))
	    break;
    }
    return len;
}

bool ExpEvaluator::getFunction(const char*& expr)
{
    XDebug(this,DebugAll,"getFunction '%s'",expr);
    skipWhites(expr);
    int len = getKeyword(expr);
    const char* s = expr+len;
    skipWhites(expr);
    if ((len <= 0) || (skipWhites(s) != '('))
	return false;
    s++;
    int argc = 0;
    // parameter list
    do {
	if (!runCompile(s,')')) {
	    if (!argc && (skipWhites(s) == ')'))
		break;
	    return false;
	}
	argc++;
    } while (getSeparator(s,true));
    if (skipWhites(s) != ')')
	return gotError("Expecting ')' after function",s);
    String str(expr,len);
    expr = s+1;
    DDebug(this,DebugAll,"Found %s()",str.safe());
    addOpcode(OpcFunc,str,argc);
    return true;
}

bool ExpEvaluator::getField(const char*& expr)
{
    XDebug(this,DebugAll,"getField '%s'",expr);
    skipWhites(expr);
    int len = getKeyword(expr);
    if (len <= 0)
	return false;
    if (expr[len] == '(')
	return false;
    String str(expr,len);
    expr += len;
    DDebug(this,DebugAll,"Found %s",str.safe());
    addOpcode(OpcField,str);
    return true;
}

ExpEvaluator::Opcode ExpEvaluator::getOperator(const char*& expr)
{
    return getOperator(expr,m_operators);
}

ExpEvaluator::Opcode ExpEvaluator::getUnaryOperator(const char*& expr)
{
    return getOperator(expr,m_unaryOps);
}

ExpEvaluator::Opcode ExpEvaluator::getPostfixOperator(const char*& expr)
{
    return OpcNone;
}

const char* ExpEvaluator::getOperator(ExpEvaluator::Opcode oper) const
{
    const char* res = lookup(oper,m_operators);
    return res ? res : lookup(oper,m_unaryOps);
}

int ExpEvaluator::getPrecedence(ExpEvaluator::Opcode oper) const
{
    switch (oper) {
	case OpcIncPre:
	case OpcDecPre:
	case OpcIncPost:
	case OpcDecPost:
	case OpcNeg:
	case OpcNot:
	    return 11;
	case OpcMul:
	case OpcDiv:
	case OpcMod:
	case OpcAnd:
	    return 10;
	case OpcAdd:
	case OpcSub:
	case OpcOr:
	case OpcXor:
	    return 9;
	case OpcShl:
	case OpcShr:
	    return 8;
	case OpcCat:
	    return 7;
	// ANY, ALL, SOME = 6
	case OpcLNot:
	    return 5;
	case OpcLt:
	case OpcGt:
	case OpcLe:
	case OpcGe:
	case OpcEq:
	case OpcNe:
	    return 4;
	// IN, BETWEEN, LIKE, MATCHES = 3
	case OpcLAnd:
	    return 2;
	case OpcLOr:
	case OpcLXor:
	    return 1;
	default:
	    return 0;
    }
}

bool ExpEvaluator::getRightAssoc(ExpEvaluator::Opcode oper) const
{
    if (oper & OpcAssign)
	return true;
    switch (oper) {
	case OpcIncPre:
	case OpcDecPre:
	case OpcNeg:
	case OpcNot:
	case OpcLNot:
	    return true;
	default:
	    return false;
    }
}

bool ExpEvaluator::getSeparator(const char*& expr, bool remove)
{
    if (skipWhites(expr) != ',')
	return false;
    if (remove)
	expr++;
    return true;
}

bool ExpEvaluator::runCompile(const char*& expr, char stop)
{
    typedef struct {
	Opcode code;
	int prec;
    } StackedOpcode;
    StackedOpcode stack[10];
    unsigned int stackPos = 0;
    DDebug(this,DebugInfo,"runCompile '%s'",expr);
    if (skipWhites(expr) == ')')
	return false;
    if (expr[0] == '*' && !expr[1]) {
	expr++;
	addOpcode(OpcField,"*");
	return true;
    }
    for (;;) {
	while (skipWhites(expr) && getInstruction(expr))
	    ;
	if (!getOperand(expr))
	    return false;
	Opcode oper;
	while ((oper = getPostfixOperator(expr)) != OpcNone)
	    addOpcode(oper);
	char c = skipWhites(expr);
	if (!c || c == stop || getSeparator(expr,false)) {
	    while (stackPos)
		addOpcode(stack[--stackPos].code);
	    return true;
	}
	oper = getOperator(expr);
	if (oper == OpcNone)
	    return gotError("Operator expected",expr);
	int precedence = 2 * getPrecedence(oper);
	int precAdj = precedence;
	if (getRightAssoc(oper))
	    precAdj++;
	while (stackPos && stack[stackPos-1].prec >= precAdj)
	    addOpcode(stack[--stackPos].code);
	if (stackPos >= (sizeof(stack)/sizeof(StackedOpcode)))
	    return gotError("Compiler stack overflow");
	stack[stackPos].code = oper;
	stack[stackPos].prec = precedence;
	stackPos++;
    }
}

bool ExpEvaluator::trySimplify()
{
    DDebug(this,DebugInfo,"trySimplify");
    bool done = false;
    for (unsigned int i = 0; i < m_opcodes.length(); i++) {
	ExpOperation* o = static_cast<ExpOperation*>(m_opcodes[i]);
	if (!o || o->barrier())
	    continue;
	switch (o->opcode()) {
	    case OpcLAnd:
	    case OpcLOr:
	    case OpcLXor:
	    case OpcAnd:
	    case OpcOr:
	    case OpcXor:
	    case OpcShl:
	    case OpcShr:
	    case OpcAdd:
	    case OpcSub:
	    case OpcMul:
	    case OpcDiv:
	    case OpcMod:
	    case OpcCat:
	    case OpcEq:
	    case OpcNe:
	    case OpcLt:
	    case OpcGt:
	    case OpcLe:
	    case OpcGe:
		if (i >= 2) {
		    ExpOperation* op2 = static_cast<ExpOperation*>(m_opcodes[i-1]);
		    ExpOperation* op1 = static_cast<ExpOperation*>(m_opcodes[i-2]);
		    if (!op1 || !op2)
			continue;
		    if (o->opcode() == OpcLAnd || o->opcode() == OpcAnd || o->opcode() == OpcMul) {
			if ((op1->opcode() == OpcPush && !op1->number() && op2->opcode() == OpcField) ||
			    (op2->opcode() == OpcPush && !op2->number() && op1->opcode() == OpcField)) {
			    (m_opcodes+i)->set(new ExpOperation(0));
			    m_opcodes.remove(op1);
			    m_opcodes.remove(op2);
			    i -= 2;
			    done = true;
			    continue;
			}
		    }
		    if (o->opcode() == OpcLOr) {
			if ((op1->opcode() == OpcPush && op1->number() && op2->opcode() == OpcField) ||
			    (op2->opcode() == OpcPush && op2->number() && op1->opcode() == OpcField)) {
			    (m_opcodes+i)->set(new ExpOperation(1));
			    m_opcodes.remove(op1);
			    m_opcodes.remove(op2);
			    i -= 2;
			    done = true;
			    continue;
			}
		    }
		    if ((op1->opcode() == OpcPush) && (op2->opcode() == OpcPush)) {
			ObjList stack;
			pushOne(stack,new ExpOperation(*op1));
			pushOne(stack,new ExpOperation(*op2));
			if (runOperation(stack,*o)) {
			    // replace operators and operation with computed constant
			    (m_opcodes+i)->set(popOne(stack));
			    m_opcodes.remove(op1);
			    m_opcodes.remove(op2);
			    i -= 2;
			    done = true;
			}
		    }
		}
		break;
	    case OpcNeg:
	    case OpcNot:
	    case OpcLNot:
		if (i >= 1) {
		    ExpOperation* op = static_cast<ExpOperation*>(m_opcodes[i-1]);
		    if (!op)
			continue;
		    if (op->opcode() == OpcPush) {
			ObjList stack;
			pushOne(stack,new ExpOperation(op));
			if (runOperation(stack,*o)) {
			    // replace unary operator and operation with computed constant
			    (m_opcodes+i)->set(popOne(stack));
			    m_opcodes.remove(op);
			    i--;
			    done = true;
			}
		    }
		    else if (op->opcode() == o->opcode() && op->opcode() != OpcLNot) {
			// minus or bit negation applied twice - remove both operators
			m_opcodes.remove(o);
			m_opcodes.remove(op);
			i--;
			done = true;
		    }
		}
		break;
	    default:
		break;
	}
    }
    return done;
}

void ExpEvaluator::addOpcode(ExpEvaluator::Opcode oper, bool barrier)
{
    DDebug(this,DebugAll,"addOpcode %u",oper);
    if (oper == OpcAs) {
	// the second operand is used just for the field name
	ExpOperation* o = 0;
	for (ObjList* l = m_opcodes.skipNull(); l; l=l->skipNext())
	    o = static_cast<ExpOperation*>(l->get());
	if (o && (o->opcode() == OpcField)) {
	    o->m_opcode = OpcPush;
	    o->String::operator=(o->name());
	}
    }
    m_opcodes.append(new ExpOperation(oper,0,0,barrier));
}

void ExpEvaluator::addOpcode(ExpEvaluator::Opcode oper, const String& name, long int value, bool barrier)
{
    DDebug(this,DebugAll,"addOpcode %u '%s' %ld",oper,name.c_str(),value);
    m_opcodes.append(new ExpOperation(oper,name,value,barrier));
}

void ExpEvaluator::addOpcode(const String& value)
{
    DDebug(this,DebugAll,"addOpcode ='%s'",value.c_str());
    m_opcodes.append(new ExpOperation(value));
}

void ExpEvaluator::addOpcode(long int value)
{
    DDebug(this,DebugAll,"addOpcode =%ld",value);
    m_opcodes.append(new ExpOperation(value));
}

void ExpEvaluator::pushOne(ObjList& stack, ExpOperation* oper)
{
    if (oper)
	stack.insert(oper);
}

ExpOperation* ExpEvaluator::popOne(ObjList& stack)
{
    ExpOperation* o = 0;
    for (;;) {
	o = static_cast<ExpOperation*>(stack.get());
	if (o || !stack.next())
	    break;
	// non-terminal NULL - remove the list entry
	stack.remove();
    }
    if (o && o->barrier()) {
	XDebug(DebugAll,"Not popping barrier %u: '%s'='%s'",o->opcode(),o->name().c_str(),o->c_str());
	return 0;
    }
    stack.remove(o,false);
    DDebug(DebugInfo,"Popped: %p",o);
    return o;
}

ExpOperation* ExpEvaluator::popAny(ObjList& stack)
{
    ExpOperation* o = 0;
    for (;;) {
	o = static_cast<ExpOperation*>(stack.get());
	if (o || !stack.next())
	    break;
	// non-terminal NULL - remove the list entry
	stack.remove();
    }
    stack.remove(o,false);
    DDebug(DebugInfo,"Popped: %p",o);
    return o;
}

ExpOperation* ExpEvaluator::popValue(ObjList& stack, void* context) const
{
    ExpOperation* oper = popOne(stack);
    if (!oper || (oper->opcode() != OpcField))
	return oper;
    bool ok = runField(stack,*oper,context);
    TelEngine::destruct(oper);
    return ok ? popOne(stack) : 0;
}

bool ExpEvaluator::runOperation(ObjList& stack, const ExpOperation& oper, void* context) const
{
    DDebug(this,DebugAll,"runOperation(%p,%u,%p) %s",&stack,oper.opcode(),context,getOperator(oper.opcode()));
    switch (oper.opcode()) {
	case OpcPush:
	    pushOne(stack,new ExpOperation(oper));
	    break;
	case OpcAnd:
	case OpcOr:
	case OpcXor:
	case OpcShl:
	case OpcShr:
	case OpcAdd:
	case OpcSub:
	case OpcMul:
	case OpcDiv:
	case OpcMod:
	case OpcEq:
	case OpcNe:
	case OpcLt:
	case OpcGt:
	case OpcLe:
	case OpcGe:
	    {
		ExpOperation* op2 = popValue(stack,context);
		ExpOperation* op1 = popValue(stack,context);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("ExpEvaluator stack underflow");
		}
		switch (oper.opcode()) {
		    case OpcDiv:
		    case OpcMod:
			if (!op2->number())
			    return gotError("Division by zero");
		    case OpcAdd:
			if (op1->isInteger() && op2->isInteger())
			    break;
			// turn addition into concatenation
			{
			    String val = *op1 + *op2;
			    TelEngine::destruct(op1);
			    TelEngine::destruct(op2);
			    DDebug(this,DebugAll,"String result: '%s'",val.c_str());
			    pushOne(stack,new ExpOperation(val));
			    return true;
			}
		    default:
			break;
		}
		long int val = 0;
		switch (oper.opcode()) {
		    case OpcAnd:
			val = op1->number() & op2->number();
			break;
		    case OpcOr:
			val = op1->number() | op2->number();
			break;
		    case OpcXor:
			val = op1->number() ^ op2->number();
			break;
		    case OpcShl:
			val = op1->number() << op2->number();
			break;
		    case OpcShr:
			val = op1->number() >> op2->number();
			break;
		    case OpcAdd:
			val = op1->number() + op2->number();
			break;
		    case OpcSub:
			val = op1->number() - op2->number();
			break;
		    case OpcMul:
			val = op1->number() * op2->number();
			break;
		    case OpcDiv:
			val = op1->number() / op2->number();
			break;
		    case OpcMod:
			val = op1->number() % op2->number();
			break;
		    case OpcLt:
			val = (op1->number() < op2->number()) ? 1 : 0;
			break;
		    case OpcGt:
			val = (op1->number() > op2->number()) ? 1 : 0;
			break;
		    case OpcLe:
			val = (op1->number() <= op2->number()) ? 1 : 0;
			break;
		    case OpcGe:
			val = (op1->number() >= op2->number()) ? 1 : 0;
			break;
		    case OpcEq:
			val = (*op1 == *op2) ? 1 : 0;
			break;
		    case OpcNe:
			val = (*op1 != *op2) ? 1 : 0;
			break;
		    default:
			break;
		}
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
		DDebug(this,DebugAll,"Numeric result: %lu",val);
		pushOne(stack,new ExpOperation(val));
	    }
	    break;
	case OpcLAnd:
	case OpcLOr:
	    {
		ExpOperation* op2 = popValue(stack,context);
		ExpOperation* op1 = popValue(stack,context);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("ExpEvaluator stack underflow");
		}
		bool val = false;
		switch (oper.opcode()) {
		    case OpcLAnd:
			val = op1->number() && op2->number();
			break;
		    case OpcLOr:
			val = op1->number() || op2->number();
			break;
		    default:
			break;
		}
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
		DDebug(this,DebugAll,"Bool result: '%s'",String::boolText(val));
		pushOne(stack,new ExpOperation(val ? 1 : 0));
	    }
	    break;
	case OpcCat:
	    {
		ExpOperation* op2 = popValue(stack,context);
		ExpOperation* op1 = popValue(stack,context);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("ExpEvaluator stack underflow");
		}
		String val = *op1 + *op2;
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
		DDebug(this,DebugAll,"String result: '%s'",val.c_str());
		pushOne(stack,new ExpOperation(val));
	    }
	    break;
	case OpcAs:
	    {
		ExpOperation* op2 = popOne(stack);
		ExpOperation* op1 = popOne(stack);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("ExpEvaluator stack underflow");
		}
		pushOne(stack,new ExpOperation(*op1,*op2));
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
	    }
	    break;
	case OpcNeg:
	case OpcNot:
	case OpcLNot:
	    {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    return gotError("ExpEvaluator stack underflow");
		long int val = op->number();
		TelEngine::destruct(op);
		switch (oper.opcode()) {
		    case OpcNeg:
			val = -val;
			break;
		    case OpcNot:
			val = ~val;
			break;
		    case OpcLNot:
			val = val ? 0 : 1;
			break;
		    default:
			break;
		}
		pushOne(stack,new ExpOperation(val));
	    }
	    break;
	case OpcFunc:
	    return runFunction(stack,oper,context) || gotError("Function call failed");
	case OpcField:
	    pushOne(stack,new ExpOperation(oper));
	    break;
	case OpcIncPre:
	case OpcDecPre:
	case OpcIncPost:
	case OpcDecPost:
	    {
		ExpOperation* fld = popOne(stack);
		if (!fld)
		    return gotError("ExpEvaluator stack underflow");
		if (fld->opcode() != OpcField) {
		    TelEngine::destruct(fld);
		    return gotError("Expecting LValue in operator");
		}
		ExpOperation* val = 0;
		if (!(runField(stack,*fld,context) && (val = popOne(stack)))) {
		    TelEngine::destruct(fld);
		    return false;
		}
		long int num = val->number();
		switch (oper.opcode()) {
		    case OpcIncPre:
			num++;
			(*val) = num;
			break;
		    case OpcDecPre:
			num--;
			(*val) = num;
			break;
		    case OpcIncPost:
			(*val) = num;
			num++;
			break;
		    case OpcDecPost:
			(*val) = num;
			num--;
			break;
		    default:
			break;
		}
		(*fld) = num;
		bool ok = runAssign(*fld,context);
		TelEngine::destruct(fld);
		if (!ok) {
		    TelEngine::destruct(val);
		    return gotError("Assignment failed");
		}
		pushOne(stack,val);
	    }
	    break;
	case OpcAssign:
	    {
		ExpOperation* val = popValue(stack,context);
		ExpOperation* fld = popOne(stack);
		if (!fld || !val) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(val);
		    return gotError("ExpEvaluator stack underflow");
		}
		if (fld->opcode() != OpcField) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(val);
		    return gotError("Expecting LValue in assignment");
		}
		ExpOperation op(*val,fld->name());
		TelEngine::destruct(fld);
		if (!runAssign(op,context)) {
		    TelEngine::destruct(val);
		    return gotError("Assignment failed");
		}
		pushOne(stack,val);
	    }
	    break;
	default:
	    if (oper.opcode() & OpcAssign) {
		// assignment by operation
		ExpOperation* val = popValue(stack,context);
		ExpOperation* fld = popOne(stack);
		if (!fld || !val) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(val);
		    return gotError("ExpEvaluator stack underflow");
		}
		if (fld->opcode() != OpcField) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(val);
		    return gotError("Expecting LValue in assignment");
		}
		pushOne(stack,new ExpOperation(*fld));
		pushOne(stack,fld);
		pushOne(stack,val);
		ExpOperation op((Opcode)(oper.opcode() & ~OpcAssign),
		    oper.name(),oper.number(),oper.barrier());
		if (!runOperation(stack,op,context))
		    return false;
		static const ExpOperation assign(OpcAssign);
		return runOperation(stack,assign,context);
	    }
	    Debug(this,DebugStub,"Please implement operation %u '%s'",
		oper.opcode(),getOperator(oper.opcode()));
	    return false;
    }
    return true;
}

bool ExpEvaluator::runFunction(ObjList& stack, const ExpOperation& oper, void* context) const
{
    DDebug(this,DebugAll,"runFunction(%p,'%s' %ld, %p) ext=%p",
	&stack,oper.name().c_str(),oper.number(),context,(void*)m_extender);
    if (oper.name() == YSTRING("chr")) {
	String res;
	for (long int i = oper.number(); i; i--) {
	    ExpOperation* o = popValue(stack,context);
	    if (!o)
		return gotError("ExpEvaluator stack underflow");
	    res = String((char)o->number()) + res;
	    TelEngine::destruct(o);
	}
	pushOne(stack,new ExpOperation(res));
	return true;
    }
    if (oper.name() == YSTRING("now")) {
	if (oper.number())
	    return gotError("Function expects no arguments");
	pushOne(stack,new ExpOperation(Time::secNow()));
	return true;
    }
    return m_extender && m_extender->runFunction(this,stack,oper,context);
}

bool ExpEvaluator::runField(ObjList& stack, const ExpOperation& oper, void* context) const
{
    DDebug(this,DebugAll,"runField(%p,'%s',%p) ext=%p",
	&stack,oper.name().c_str(),context,(void*)m_extender);
    return m_extender && m_extender->runField(this,stack,oper,context);
}

bool ExpEvaluator::runAssign(const ExpOperation& oper, void* context) const
{
    DDebug(this,DebugAll,"runAssign('%s'='%s',%p) ext=%p",
	oper.name().c_str(),oper.c_str(),context,(void*)m_extender);
    return m_extender && m_extender->runAssign(this,oper,context);
}

bool ExpEvaluator::runEvaluate(const ObjList& opcodes, ObjList& stack, void* context) const
{
    DDebug(this,DebugInfo,"runEvaluate(%p,%p,%p)",&opcodes,&stack,context);
    for (const ObjList* l = opcodes.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	if (!runOperation(stack,*o,context))
	    return false;
    }
    return true;
}

bool ExpEvaluator::runEvaluate(const ObjVector& opcodes, ObjList& stack, void* context, unsigned int index) const
{
    DDebug(this,DebugInfo,"runEvaluate(%p,%p,%p,%u)",&opcodes,&stack,context,index);
    for (; index < opcodes.length(); index++) {
	const ExpOperation* o = static_cast<const ExpOperation*>(opcodes[index]);
	if (o && !runOperation(stack,*o,context))
	    return false;
    }
    return true;
}

bool ExpEvaluator::runEvaluate(ObjList& stack, void* context) const
{
    return runEvaluate(m_opcodes,stack,context);
}

bool ExpEvaluator::runAllFields(ObjList& stack, void* context) const
{
    DDebug(this,DebugAll,"runAllFields(%p,%p)",&stack,context);
    bool ok = true;
    for (ObjList* l = stack.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	if (o->barrier())
	    break;
	if (o->opcode() != OpcField)
	    continue;
	ObjList tmp;
	if (runField(tmp,*o,context)) {
	    ExpOperation* val = popOne(tmp);
	    if (val)
		l->set(val);
	    else
		ok = false;
	}
	else
	    ok = false;
    }
    return ok;
}

int ExpEvaluator::compile(const char* expr)
{
    if (!skipWhites(expr))
	return 0;
    int res = 0;
    do {
	if (!runCompile(expr))
	    return 0;
	res++;
    } while (getSeparator(expr,true));
    return skipWhites(expr) ? 0 : res;
}

bool ExpEvaluator::evaluate(ObjList* results, void* context) const
{
    if (results) {
	results->clear();
	return runEvaluate(*results,context) &&
	    (runAllFields(*results,context) || gotError("Could not evaluate all fields"));
    }
    ObjList res;
    return runEvaluate(res,context);
}

int ExpEvaluator::evaluate(NamedList& results, unsigned int index, const char* prefix, void* context) const
{
    ObjList stack;
    if (!evaluate(stack,context))
	return -1;
    String idx(prefix);
    if (index)
	idx << index << ".";
    int column = 0;
    for (ObjList* r = stack.skipNull(); r; r = r->skipNext()) {
	column++;
	const ExpOperation* res = static_cast<const ExpOperation*>(r->get());
	String name = res->name();
	if (name.null())
	    name = column;
	results.setParam(idx+name,*res);
    }
    return column;
}

int ExpEvaluator::evaluate(Array& results, unsigned int index, void* context) const
{
    Debug(this,DebugStub,"Please implement ExpEvaluator::evaluate(Array)");
    return -1;
}

void ExpEvaluator::dump(const ObjList& codes, String& res) const
{
    for (const ObjList* l = codes.skipNull(); l; l = l->skipNext()) {
	if (res)
	    res << " ";
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	const char* oper = getOperator(o->opcode());
	if (oper) {
	    res << oper;
	    continue;
	}
	switch (o->opcode()) {
	    case OpcPush:
		if (o->isInteger())
		    res << (int)o->number();
		else
		    res << "'" << *o << "'";
		break;
	    case OpcField:
		res << o->name();
		break;
	    case OpcFunc:
		res << o->name() << "(" << (int)o->number() << ")";
		break;
	    default:
		res << "[" << o->opcode() << "]";
	}
    }
}


TableEvaluator::TableEvaluator(const TableEvaluator& original)
    : m_select(original.m_select), m_where(original.m_where),
      m_limit(original.m_limit), m_limitVal(original.m_limitVal)
{
    extender(original.m_select.extender());
}

TableEvaluator::TableEvaluator(ExpEvaluator::Parser style)
    : m_select(style), m_where(style),
      m_limit(style), m_limitVal((unsigned int)-2)
{
}

TableEvaluator::TableEvaluator(const TokenDict* operators, const TokenDict* unaryOps)
    : m_select(operators,unaryOps), m_where(operators,unaryOps),
      m_limit(operators,unaryOps), m_limitVal((unsigned int)-2)
{
}

TableEvaluator::~TableEvaluator()
{
}

void TableEvaluator::extender(ExpExtender* ext)
{
    m_select.extender(ext);
    m_where.extender(ext);
    m_limit.extender(ext);
}

bool TableEvaluator::evalWhere(void* context)
{
    if (m_where.null())
	return true;
    ObjList res;
    if (!m_where.evaluate(res,context))
	return false;
    ObjList* first = res.skipNull();
    if (!first)
	return false;
    const ExpOperation* o = static_cast<const ExpOperation*>(first->get());
    return (o->opcode() == ExpEvaluator::OpcPush) && o->number();
}

bool TableEvaluator::evalSelect(ObjList& results, void* context)
{
    if (m_select.null())
	return false;
    return m_select.evaluate(results,context);
}

unsigned int TableEvaluator::evalLimit(void* context)
{
    if (m_limitVal == (unsigned int)-2) {
	m_limitVal = (unsigned int)-1;
	// hack: use a loop so we can break out of it
	while (!m_limit.null()) {
	    ObjList res;
	    if (!m_limit.evaluate(res,context))
		break;
	    ObjList* first = res.skipNull();
	    if (!first)
		break;
	    const ExpOperation* o = static_cast<const ExpOperation*>(first->get());
	    if (o->opcode() != ExpEvaluator::OpcPush)
		break;
	    int lim = o->number();
	    if (lim < 0)
		lim = 0;
	    m_limitVal = lim;
	    break;
	}
    }
    return m_limitVal;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
