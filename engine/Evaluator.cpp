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

#include <stdlib.h>

using namespace TelEngine;

#define MAKEOP(s,o) { s, ExpEvaluator::Opc ## o }
static const TokenDict s_operators_c[] =
{
    MAKEOP("<<",Shl),
    MAKEOP(">>",Shr),
    MAKEOP("==",Eq),
    MAKEOP("!=",Ne),
    MAKEOP("&&",LAnd),
    MAKEOP("||",LOr),
    MAKEOP("^^",LXor),
    MAKEOP("+", Add),
    MAKEOP("-", Sub),
    MAKEOP("*", Mul),
    MAKEOP("/", Div),
    MAKEOP("%", Mod),
    MAKEOP("!", LNot),
    MAKEOP("&", And),
    MAKEOP("|", Or),
    MAKEOP("^", Xor),
    MAKEOP("~", Not),
    MAKEOP(".", Cat),
    MAKEOP("@", As),
    { 0, 0 }
};

const TokenDict s_operators_sql[] =
{
    MAKEOP("AND",LAnd),
    MAKEOP("OR",LOr),
    MAKEOP("NOT", LNot),
    MAKEOP("<<",Shl),
    MAKEOP(">>",Shr),
    MAKEOP("<>",Ne),
    MAKEOP("!=",Ne),
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
    MAKEOP("~", Not),
    MAKEOP("=",Eq),
    { 0, 0 }
};
#undef MAKEOP



bool ExpExtender::runFunction(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper)
{
    return false;
}

bool ExpExtender::runField(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper)
{
    return false;
}


ExpEvaluator::ExpEvaluator(const TokenDict* operators)
    : m_operators(operators), m_extender(0)
{
}

ExpEvaluator::ExpEvaluator(ExpEvaluator::Parser style)
    : m_operators(0), m_extender(0)
{
    switch (style) {
	case C:
	    m_operators = s_operators_c;
	    break;
	case SQL:
	    m_operators = s_operators_sql;
	    break;
    }
}

ExpEvaluator::ExpEvaluator(const ExpEvaluator& original)
    : m_operators(original.m_operators), m_extender(0)
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

char ExpEvaluator::skipWhites(const char*& expr) const
{
    if (!expr)
	return 0;
    while (*expr==' ' || *expr=='\t')
	expr++;
    return *expr;
}

bool ExpEvaluator::gotError(const char* error, const char* text) const
{
    if (!error)
	error = "unknown error";
    Debug(DebugWarn,"Evaluator got error: %s%s%s",error,
	(text ? " at: " : ""),
	c_safe(text));
    return false;
}

bool ExpEvaluator::getOperand(const char*& expr)
{
    XDebug(DebugAll,"getOperand '%s'",expr);
    char c = skipWhites(expr);
    if (!c)
	// end of string
	return true;
    if (c == '(') {
	// parenthesized subexpression
	if (!runCompile(++expr))
	    return false;
	if (skipWhites(expr) != ')')
	    return gotError("Expecting ')'",expr);
	expr++;
	return true;
    }
    if (getString(expr) || getNumber(expr) || getFunction(expr) || getField(expr))
	return true;
    return gotError("Expecting operand",expr);
}

bool ExpEvaluator::getNumber(const char*& expr)
{
    XDebug(DebugAll,"getNumber '%s'",expr);
    char* endp = 0;
    long int val = ::strtol(expr,&endp,0);
    if (!endp || (endp == expr))
	return false;
    expr = endp;
    DDebug(DebugAll,"Found %ld",val);
    addOpcode(val);
    return true;
}

bool ExpEvaluator::getString(const char*& expr)
{
    XDebug(DebugAll,"getString '%s'",expr);
    char c = skipWhites(expr);
    if (c == '"' || c == '\'') {
	char sep = c;
	const char* start = ++expr;
	while ((c = *expr++)) {
	    if (c != sep)
		continue;
	    String str(start,expr-start-1);
	    DDebug(DebugAll,"Found '%s'",str.safe());
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
	if (c <= ' ')
	    break;
	if (('0' <= c && c <= '9') ||
	    ('a' <= c && c <= 'z') ||
	    ('A' <= c && c <= 'Z') ||
	    (c == '_'))
	    continue;
	break;
    }
    return len;
}

bool ExpEvaluator::getFunction(const char*& expr)
{
    XDebug(DebugAll,"getFunction '%s'",expr);
    skipWhites(expr);
    int len = getKeyword(expr);
    if ((len <= 0) || (expr[len] != '('))
	return false;
    const char* s = expr+len+1;
    int argc = 0;
    // parameter list
    do {
	if (!runCompile(s)) {
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
    DDebug(DebugAll,"Found %s()",str.safe());
    addOpcode(OpcFunc,str,argc);
    return true;
}

bool ExpEvaluator::getField(const char*& expr)
{
    XDebug(DebugAll,"getField '%s'",expr);
    skipWhites(expr);
    int len = getKeyword(expr);
    if (len <= 0)
	return false;
    if (expr[len] == '(')
	return false;
    String str(expr,len);
    expr += len;
    DDebug(DebugAll,"Found %s",str.safe());
    addOpcode(OpcField,str);
    return true;
}

ExpEvaluator::Opcode ExpEvaluator::getOperator(const char*& expr) const
{
    XDebug(DebugAll,"getOperator '%s'",expr);
    skipWhites(expr);
    if (m_operators) {
	for (const TokenDict* o = m_operators; o->token; o++) {
	    const char* s1 = o->token;
	    const char* s2 = expr;
	    do {
		if (!*s1) {
		    expr = s2;
		    return (ExpEvaluator::Opcode)o->value;
		}
	    } while (*s1++ == *s2++);
	}
    }
    return OpcNone;
}

const char* ExpEvaluator::getOperator(ExpEvaluator::Opcode oper) const
{
    return lookup(oper,m_operators);
}

int ExpEvaluator::getPrecedence(ExpEvaluator::Opcode oper)
{
    switch (oper) {
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

bool ExpEvaluator::getSeparator(const char*& expr, bool remove)
{
    if (skipWhites(expr) != ',')
	return false;
    if (remove)
	expr++;
    return true;
}

bool ExpEvaluator::runCompile(const char*& expr)
{
    typedef struct {
	Opcode code;
	int prec;
    } StackedOpcode;
    StackedOpcode stack[10];
    unsigned int stackPos = 0;
    DDebug(DebugInfo,"runCompile '%s'",expr);
    if (skipWhites(expr) == ')')
	return false;
    if (expr[0] == '*' && !expr[1]) {
	expr++;
	addOpcode(OpcField,"*");
	return true;
    }
    for (;;) {
	if (!getOperand(expr))
	    return false;
	char c = skipWhites(expr);
	if (!c || c == ')' || getSeparator(expr,false)) {
	    while (stackPos)
		addOpcode(stack[--stackPos].code);
	    return true;
	}
	Opcode oper = getOperator(expr);
	if (oper == OpcNone)
	    return gotError("Operator expected",expr);
	int precedence = getPrecedence(oper);
	while (stackPos && stack[stackPos-1].prec >= precedence)
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
    DDebug(DebugInfo,"trySimplify");
    bool done = false;
    for (unsigned int i = 0; i < m_opcodes.length(); i++) {
	ExpOperation* o = static_cast<ExpOperation*>(m_opcodes[i]);
	if (!o)
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
			stack.append(new ExpOperation(*op1));
			stack.append(new ExpOperation(*op2));
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
	    default:
		break;
	}
    }
    return done;
}

void ExpEvaluator::addOpcode(ExpEvaluator::Opcode oper)
{
    DDebug(DebugAll,"addOpcode %u",oper);
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
    m_opcodes.append(new ExpOperation(oper));
}

void ExpEvaluator::addOpcode(ExpEvaluator::Opcode oper, const String& name, long int value)
{
    DDebug(DebugAll,"addOpcode %u '%s' %ld",oper,name.c_str(),value);
    m_opcodes.append(new ExpOperation(oper,name,value));
}

void ExpEvaluator::addOpcode(const String& value)
{
    DDebug(DebugAll,"addOpcode ='%s'",value.c_str());
    m_opcodes.append(new ExpOperation(value));
}

void ExpEvaluator::addOpcode(long int value)
{
    DDebug(DebugAll,"addOpcode =%ld",value);
    m_opcodes.append(new ExpOperation(value));
}

ExpOperation* ExpEvaluator::popOne(ObjList& stack)
{
    GenObject* o = 0;
    for (ObjList* l = stack.skipNull(); l; l=l->skipNext())
	o = l->get();
    stack.remove(o,false);
    DDebug(DebugInfo,"Popped: %p",o);
    return static_cast<ExpOperation*>(o);
}

bool ExpEvaluator::runOperation(ObjList& stack, const ExpOperation& oper) const
{
    DDebug(DebugAll,"runOperation(%p,%u) %s",&stack,oper.opcode(),getOperator(oper.opcode()));
    switch (oper.opcode()) {
	case OpcPush:
	    stack.append(new ExpOperation(oper));
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
		ExpOperation* op2 = popOne(stack);
		ExpOperation* op1 = popOne(stack);
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
		DDebug(DebugAll,"Numeric result: %lu",val);
		stack.append(new ExpOperation(val));
	    }
	    break;
	case OpcLAnd:
	case OpcLOr:
	    {
		ExpOperation* op2 = popOne(stack);
		ExpOperation* op1 = popOne(stack);
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
		DDebug(DebugAll,"Bool result: '%s'",String::boolText(val));
		stack.append(new ExpOperation(val ? 1 : 0));
	    }
	    break;
	case OpcCat:
	    {
		ExpOperation* op2 = popOne(stack);
		ExpOperation* op1 = popOne(stack);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("ExpEvaluator stack underflow");
		}
		String val = *op1 + *op2;
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
		DDebug(DebugAll,"String result: '%s'",val.c_str());
		stack.append(new ExpOperation(val));
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
		stack.append(new ExpOperation(*op1,*op2));
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
	    }
	    break;
	case OpcFunc:
	    return runFunction(stack,oper);
	case OpcField:
	    return runField(stack,oper);
	default:
	    Debug(DebugStub,"Please implement operation %u",oper.opcode());
	    return false;
    }
    return true;
}

bool ExpEvaluator::runFunction(ObjList& stack, const ExpOperation& oper) const
{
    DDebug(DebugAll,"runFunction(%p,'%s' %ld) ext=%p",
	&stack,oper.name().c_str(),oper.number(),(void*)m_extender);
    if (oper.name() == YSTRING("chr")) {
	String res;
	for (long int i = oper.number(); i; i--) {
	    ExpOperation* o = popOne(stack);
	    if (!o)
		return gotError("ExpEvaluator stack underflow");
	    res = String((char)o->number()) + res;
	    TelEngine::destruct(o);
	}
	stack.append(new ExpOperation(res));
	return true;
    }
    if (oper.name() == YSTRING("now")) {
	if (oper.number())
	    return gotError("Function expects no arguments");
	stack.append(new ExpOperation(Time::secNow()));
	return true;
    }
    return m_extender && m_extender->runFunction(this,stack,oper);
}

bool ExpEvaluator::runField(ObjList& stack, const ExpOperation& oper) const
{
    DDebug(DebugAll,"runField(%p,'%s') ext=%p",
	&stack,oper.name().c_str(),(void*)m_extender);
    return m_extender && m_extender->runField(this,stack,oper);
}

bool ExpEvaluator::runEvaluate(ObjList& stack) const
{
    DDebug(DebugInfo,"runEvaluate(%p)",&stack);
    for (ObjList* l = m_opcodes.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	if (!runOperation(stack,*o))
	    return false;
    }
    return true;
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

bool ExpEvaluator::evaluate(ObjList* results) const
{
    ObjList res;
    if (results)
	results->clear();
    else
	results = &res;
    return runEvaluate(*results);
}

int ExpEvaluator::evaluate(NamedList& results, unsigned int index, const char* prefix) const
{
    ObjList stack;
    if (!evaluate(stack))
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

int ExpEvaluator::evaluate(Array& results, unsigned int index) const
{
    Debug(DebugStub,"Please implement ExpEvaluator::evaluate(Array)");
    return -1;
}

String ExpEvaluator::dump() const
{
    String res;
    for (ObjList* l = m_opcodes.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	const char* oper = getOperator(o->opcode());
	if (oper) {
	    res << " " << oper;
	    continue;
	}
	switch (o->opcode()) {
	    case OpcPush:
		if (o->number())
		    res << " " << (int)o->number();
		else
		    res << " '" << *o << "'";
		break;
	    case OpcField:
		res << " " << o->name();
		break;
	    case OpcFunc:
		res << " " << o->name() << "(" << (int)o->number() << ")";
		break;
	    default:
		res << " [" << o->opcode() << "]";
	}
    }
    return res;
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

TableEvaluator::TableEvaluator(const TokenDict* operators)
    : m_select(operators), m_where(operators),
      m_limit(operators), m_limitVal((unsigned int)-2)
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

bool TableEvaluator::evalWhere()
{
    if (m_where.null())
	return true;
    ObjList res;
    if (!m_where.evaluate(res))
	return false;
    ObjList* first = res.skipNull();
    if (!first)
	return false;
    const ExpOperation* o = static_cast<const ExpOperation*>(first->get());
    return (o->opcode() == ExpEvaluator::OpcPush) && o->number();
}

bool TableEvaluator::evalSelect(ObjList& results)
{
    if (m_select.null())
	return false;
    return m_select.evaluate(results);
}

unsigned int TableEvaluator::evalLimit()
{
    if (m_limitVal == (unsigned int)-2) {
	m_limitVal = (unsigned int)-1;
	// hack: use a loop so we can break out of it
	while (!m_limit.null()) {
	    ObjList res;
	    if (!m_limit.evaluate(res))
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
