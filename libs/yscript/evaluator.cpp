/**
 * evaluator.cpp
 * Yet Another (Java)script library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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

#include "yateclass.h"
#include "yatescript.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace TelEngine;

#define MAX_SIMPLIFY 16

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

RefObject* ExpExtender::refObj()
{
    return 0;
}

bool ExpExtender::hasField(ObjList& stack, const String& name, GenObject* context) const
{
    return false;
}

NamedString* ExpExtender::getField(ObjList& stack, const String& name, GenObject* context) const
{
    return 0;
}

bool ExpExtender::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    return false;
}

bool ExpExtender::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    return false;
}

bool ExpExtender::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    return false;
}

ParsePoint& ParsePoint::operator=(ParsePoint& parsePoint)
{
    m_expr = parsePoint.m_expr;
    m_count = parsePoint.m_count;
    m_searchedSeps = parsePoint.m_searchedSeps;
    m_fileName = parsePoint.m_fileName;
    return operator=(parsePoint.m_lineNo);
}

ParsePoint& ParsePoint::operator=(unsigned int line)
{
    m_lineNo = line;
    if (m_eval)
	m_eval->m_lineNo = line;
    return *this;
}

ExpEvaluator::ExpEvaluator(const TokenDict* operators, const TokenDict* unaryOps)
    : m_operators(operators), m_unaryOps(unaryOps), m_lastOpcode(&m_opcodes),
      m_inError(false), m_lineNo(1), m_extender(0)
{
}

ExpEvaluator::ExpEvaluator(ExpEvaluator::Parser style)
    : m_operators(0), m_unaryOps(0), m_lastOpcode(&m_opcodes),
    m_inError(false), m_lineNo(1), m_extender(0)
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
    : m_operators(original.m_operators), m_unaryOps(original.unaryOps()), m_lastOpcode(&m_opcodes),
      m_inError(false), m_lineNo(original.lineNumber()), m_extender(0)
{
    extender(original.extender());
    for (ObjList* l = original.m_opcodes.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	m_lastOpcode = m_lastOpcode->append(o->clone());
    }
}

ExpEvaluator::~ExpEvaluator()
{
    extender(0);
}

bool ExpEvaluator::null() const
{
    return !m_opcodes.skipNull();
}

void ExpEvaluator::extender(ExpExtender* ext)
{
    if (ext == m_extender)
	return;
    if (ext && ext->refObj() && !ext->refObj()->ref())
	return;
    ExpExtender* tmp = m_extender;
    m_extender = ext;
    if (tmp)
	TelEngine::destruct(tmp->refObj());
}

char ExpEvaluator::skipWhites(ParsePoint& expr)
{
    if (!expr.m_expr)
	return 0;
    for (; ; expr++) {
	char c = *expr;
	switch (c) {
	    case ' ':
	    case '\t':
		continue;
	    case '\r':
		expr.m_lineNo = ++m_lineNo;
		if (expr[1] == '\n')
		    expr++;
		continue;
	    case '\n':
		expr.m_lineNo = ++m_lineNo;
		if (expr[1] == '\r')
		    expr++;
		continue;
	    default:
		return c;
	}
    }
}

bool ExpEvaluator::keywordLetter(char c) const
{
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || (c == '_');
}

bool ExpEvaluator::keywordDigit(char c) const
{
    return ('0' <= c && c <= '9');
}

bool ExpEvaluator::keywordChar(char c) const
{
    return keywordLetter(c) || keywordDigit(c);
}

char ExpEvaluator::skipComments(ParsePoint& expr, GenObject* context)
{
    return skipWhites(expr);
}

int ExpEvaluator::preProcess(ParsePoint& expr, GenObject* context)
{
    return -1;
}

ExpEvaluator::Opcode ExpEvaluator::getOperator(const char*& expr, const TokenDict* operators, bool caseInsensitive) const
{
    XDebug(this,DebugAll,"getOperator('%.30s',%p,%s)",expr,operators,String::boolText(caseInsensitive));
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

bool ExpEvaluator::gotError(const char* error, const char* text, unsigned int line) const
{
    if (!error) {
	if (!text)
	    return false;
	error = "unknown error";
    }
    if (!line)
	line = lineNumber();
    String lineNo;
    formatLineNo(lineNo,line);
    Debug(this,DebugWarn,"Evaluator error: %s in %s%s%.50s",error,
	lineNo.c_str(),(text ? " at: " : ""),c_safe(text));
    return false;
}

bool ExpEvaluator::gotError(const char* error, const char* text, unsigned int line)
{
    m_inError = true;
    return const_cast<const ExpEvaluator*>(this)->gotError(error,text);
}

void ExpEvaluator::formatLineNo(String& buf, unsigned int line) const
{
    buf.clear();
    buf << "line " << line;
}

bool ExpEvaluator::getInstruction(ParsePoint& expr, char stop, GenObject* nested)
{
    return false;
}

bool ExpEvaluator::getOperand(ParsePoint& expr, bool endOk, int precedence)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"getOperand line=0x%X '%.30s'",lineNumber(),(const char*)expr);
    if (!getOperandInternal(expr, endOk, precedence))
	return false;
    Opcode oper;
    while ((oper = getPostfixOperator(expr,precedence)) != OpcNone)
	addOpcode(oper);
    return true;
}

bool ExpEvaluator::getOperandInternal(ParsePoint& expr, bool endOk, int precedence)
{
    char c = skipComments(expr);
    if (!c)
	// end of string
	return endOk;
    if (c == '(') {
	// parenthesized subexpression
	if (!runCompile(++expr,')'))
	    return false;
	if (skipComments(expr) != ')')
	    return gotError("Expecting ')'",expr);
	expr++;
	return true;
    }
    if (getNumber(expr))
	return true;
    Opcode op = getUnaryOperator(expr);
    if (op != OpcNone) {
	if (!getOperand(expr,false,getPrecedence(op)))
	    return false;
	addOpcode(op);
	return true;
    }
    if (getSimple(expr) || getFunction(expr) || getField(expr))
	return true;
    return gotError("Expecting operand",expr);
}

bool ExpEvaluator::getSimple(ParsePoint& expr, bool constOnly)
{
    return getString(expr) || getNumber(expr);
}

bool ExpEvaluator::getNumber(ParsePoint& expr)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"getNumber line=0x%X '%.30s'",lineNumber(),(const char*)expr);
    char* endp = 0;
    int64_t val = ::strtoll(expr,&endp,0);
    if (!endp || (endp == expr))
	return false;
    expr = endp;
    DDebug(this,DebugAll,"Found " FMT64,val);
    addOpcode(val);
    return true;
}

bool ExpEvaluator::getString(ParsePoint& expr)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"getString line=0x%X '%.30s'",lineNumber(),(const char*)expr);
    char c = skipComments(expr);
    if (c == '"' || c == '\'') {
	String str;
	if (getString(expr,str)) {
	    addOpcode(str);
	    return true;
	}
    }
    return false;
}

bool ExpEvaluator::getString(const char*& expr, String& str)
{
    char sep = *expr++;
    const char* start = expr;
    while (char c = *expr++) {
	if (c != '\\' && c != sep)
	    continue;
	String tmp(start,expr-start-1);
	str += tmp;
	if (c == sep) {
	    DDebug(this,DebugAll,"Found '%s'",str.safe());
	    return true;
	}
	tmp.clear();
	if (!getEscape(expr,tmp,sep))
	    break;
	str += tmp;
	start = expr;
    }
    expr--;
    return gotError("Expecting string end");
}

bool ExpEvaluator::getEscape(const char*& expr, String& str, char sep)
{
    char c = *expr++;
    switch (c) {
	case '\0':
	    return false;
	case 'b':
	    c = '\b';
	    break;
	case 'f':
	    c = '\f';
	    break;
	case 'n':
	    c = '\n';
	    break;
	case 'r':
	    c = '\r';
	    break;
	case 't':
	    c = '\t';
	    break;
	case 'v':
	    c = '\v';
	    break;
    }
    str = c;
    return true;
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

bool ExpEvaluator::getFunction(ParsePoint& expr)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"getFunction line=0x%X '%.30s'",lineNumber(),(const char*)expr);
    skipComments(expr);
    int len = getKeyword(expr);
    ParsePoint s = expr;
    s.m_expr = s.m_expr+len;
    if ((len <= 0) || (skipComments(s) != '(')) {
	m_lineNo = expr.lineNumber();
	return false;
    }
    s++;
    int argc = 0;
    // parameter list
    do {
	if (!runCompile(s,')')) {
	    if (!argc && (skipComments(s) == ')'))
		break;
	    m_lineNo = expr.lineNumber();
	    return false;
	}
	argc++;
    } while (getSeparator(s,true));
    if (skipComments(s) != ')')
	return gotError("Expecting ')' after function",s);
    unsigned int line = expr.lineNumber();
    String str(expr,len);
    expr.m_expr = s.m_expr+1;
    expr.m_lineNo = lineNumber();
    DDebug(this,DebugAll,"Found %s()",str.safe());
    addOpcode(OpcFunc,str,argc,false,line);
    return true;
}

bool ExpEvaluator::getField(ParsePoint& expr)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"getField line=0x%X '%.30s'",lineNumber(),(const char*)expr);
    skipComments(expr);
    int len = getKeyword(expr);
    if (len <= 0)
	return false;
    if (expr[len] == '(')
	return false;
    String str(expr,len);
    expr += len;
    DDebug(this,DebugAll,"Found field '%s'",str.safe());
    addOpcode(OpcField,str);
    return true;
}

ExpEvaluator::Opcode ExpEvaluator::getOperator(ParsePoint& expr)
{
    skipComments(expr);
    return getOperator(expr,m_operators);
}

ExpEvaluator::Opcode ExpEvaluator::getUnaryOperator(ParsePoint& expr)
{
    skipComments(expr);
    return getOperator(expr,m_unaryOps);
}

ExpEvaluator::Opcode ExpEvaluator::getPostfixOperator(ParsePoint& expr, int priority)
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
	    return 120;
	case OpcNeg:
	case OpcNot:
	case OpcLNot:
	    return 110;
	case OpcMul:
	case OpcDiv:
	case OpcMod:
	case OpcAnd:
	    return 100;
	case OpcAdd:
	case OpcSub:
	case OpcOr:
	case OpcXor:
	    return 90;
	case OpcShl:
	case OpcShr:
	    return 80;
	case OpcCat:
	    return 70;
	// ANY, ALL, SOME = 60
	case OpcLt:
	case OpcGt:
	case OpcLe:
	case OpcGe:
	    return 50;
	case OpcEq:
	case OpcNe:
	    return 40;
	// IN, BETWEEN, LIKE, MATCHES = 30
	case OpcLAnd:
	    return 20;
	case OpcLOr:
	case OpcLXor:
	    return 10;
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

bool ExpEvaluator::getSeparator(ParsePoint& expr, bool remove)
{
    if (skipComments(expr) != ',')
	return false;
    if (remove)
	expr++;
    return true;
}

bool ExpEvaluator::runCompile(ParsePoint& expr, char stop, GenObject* nested)
{
    char buf[2];
    const char* stopStr = 0;
    if (stop) {
	buf[0] = stop;
	buf[1] = '\0';
	stopStr = buf;
    }
    return runCompile(expr,stopStr,nested);
}

bool ExpEvaluator::runCompile(ParsePoint& expr, const char* stop, GenObject* nested)
{
    typedef struct {
	Opcode code;
	int prec;
	unsigned int line;
    } StackedOpcode;
    StackedOpcode stack[10];
    unsigned int stackPos = 0;
#ifdef DEBUG
    Debugger debug(DebugInfo,"runCompile()"," '%s' %p '%.30s'",TelEngine::c_safe(stop),nested,(const char*)expr);
#endif
    if (skipComments(expr) == ')')
	return false;
    m_inError = false;
    if (expr[0] == '*' && !expr[1]) {
	expr++;
	addOpcode(OpcField,"*");
	return true;
    }
    char stopChar = stop ? stop[0] : '\0';
    for (;;) {
	while (!stackPos && skipComments(expr) && (!stop || !::strchr(stop,*expr)) && getInstruction(expr,stopChar,nested))
	   if (!expr.m_count && expr.m_searchedSeps && expr.m_foundSep && ::strchr(expr.m_searchedSeps,expr.m_foundSep))
		return true;
	if (inError())
	    return false;
	char c = skipComments(expr);
	if (c && stop && ::strchr(stop,c)) {
	    expr.m_foundSep = c;
	    return true;
	}
	if (!getOperand(expr))
	    return false;
	Opcode oper;
	while ((oper = getPostfixOperator(expr)) != OpcNone)
	    addOpcode(oper);
	if (inError())
	    return false;
	c = skipComments(expr);
	if (!c || (stop && ::strchr(stop,c)) || getSeparator(expr,false)) {
	    while (stackPos) {
		stackPos--;
		addOpcode(stack[stackPos].code,false,stack[stackPos].line);
	    }
	    return true;
	}
	if (inError())
	    return false;
	skipComments(expr);
	oper = getOperator(expr);
	if (oper == OpcNone)
	    return gotError("Operator or separator expected",expr);
	int precedence = 2 * getPrecedence(oper);
	int precAdj = precedence;
	// precedence being equal favor right associative operators
	if (getRightAssoc(oper))
	    precAdj++;
	while (stackPos && stack[stackPos-1].prec >= precAdj) {
	    stackPos--;
	    addOpcode(stack[stackPos].code,false,stack[stackPos].line);
	}
	if (stackPos >= (sizeof(stack)/sizeof(StackedOpcode)))
	    return gotError("Compiler stack overflow");
	stack[stackPos].code = oper;
	stack[stackPos].prec = precedence;
	stack[stackPos].line = lineNumber();
	stackPos++;
    }
}

bool ExpEvaluator::trySimplify()
{
    DDebug(this,DebugInfo,"trySimplify");
    bool done = false;
    ObjList* opcodes = &m_opcodes;
    for (unsigned int i = 0; ; i++) {
	while ((i > MAX_SIMPLIFY) && opcodes->next()) {
	    // limit backtrace depth
	    opcodes = opcodes->next();
	    i--;
	}
	ExpOperation* o = static_cast<ExpOperation*>(opcodes->at(i));
	if (!o) {
	    if (i >= opcodes->length())
		break;
	    else
		continue;
	}
	if (o->barrier())
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
		    ExpOperation* op2 = static_cast<ExpOperation*>(opcodes->at(i-1));
		    ExpOperation* op1 = static_cast<ExpOperation*>(opcodes->at(i-2));
		    if (!op1 || !op2)
			continue;
		    if (o->opcode() == OpcLAnd || o->opcode() == OpcAnd || o->opcode() == OpcMul) {
			if ((op1->opcode() == OpcPush && !op1->number() && op2->opcode() == OpcField) ||
			    (op2->opcode() == OpcPush && !op2->number() && op1->opcode() == OpcField)) {
			    ExpOperation* newOp = (o->opcode() == OpcLAnd) ? new ExpOperation(false) : new ExpOperation((int64_t)0);
			    newOp->lineNumber(o->lineNumber());
			    ((*opcodes)+i)->set(newOp);
			    opcodes->remove(op1);
			    opcodes->remove(op2);
			    i -= 2;
			    done = true;
			    continue;
			}
		    }
		    if (o->opcode() == OpcLOr) {
			if ((op1->opcode() == OpcPush && op1->number() && op2->opcode() == OpcField) ||
			    (op2->opcode() == OpcPush && op2->number() && op1->opcode() == OpcField)) {
			    ExpOperation* newOp = new ExpOperation(true);
			    newOp->lineNumber(o->lineNumber());
			    ((*opcodes)+i)->set(newOp);
			    opcodes->remove(op1);
			    opcodes->remove(op2);
			    i -= 2;
			    done = true;
			    continue;
			}
		    }
		    if ((op1->opcode() == OpcPush) && (op2->opcode() == OpcPush)) {
			ObjList stack;
			pushOne(stack,op1->clone());
			pushOne(stack,op2->clone());
			if (runOperation(stack,*o)) {
			    // replace operators and operation with computed constant
			    ExpOperation* newOp = popOne(stack);
			    newOp->lineNumber(o->lineNumber());
			    ((*opcodes)+i)->set(newOp);
			    opcodes->remove(op1);
			    opcodes->remove(op2);
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
		    ExpOperation* op = static_cast<ExpOperation*>(opcodes->at(i-1));
		    if (!op)
			continue;
		    if (op->opcode() == OpcPush) {
			ObjList stack;
			pushOne(stack,op->clone());
			if (runOperation(stack,*o)) {
			    // replace unary operator and operation with computed constant
			    ExpOperation* newOp = popOne(stack);
			    newOp->lineNumber(o->lineNumber());
			    ((*opcodes)+i)->set(newOp);
			    opcodes->remove(op);
			    i--;
			    done = true;
			}
		    }
		    else if (op->opcode() == o->opcode() && op->opcode() != OpcLNot) {
			// minus or bit negation applied twice - remove both operators
			opcodes->remove(o);
			opcodes->remove(op);
			i--;
			done = true;
		    }
		}
		break;
	    default:
		break;
	}
    }
    m_lastOpcode = opcodes->last();
    return done;
}

void ExpEvaluator::addOpcode(ExpOperation* oper, unsigned int line)
{
    if (!oper)
	return;
    if (!line)
	line = lineNumber();
    DDebug(this,DebugAll,"addOpcode %u (%s) line=0x%X",
	oper->opcode(),getOperator(oper->opcode()),line);
    oper->lineNumber(line);
    m_lastOpcode = m_lastOpcode->append(oper);
}

ExpOperation* ExpEvaluator::addOpcode(ExpEvaluator::Opcode oper, bool barrier, unsigned int line)
{
    if (!line)
	line = lineNumber();
    DDebug(this,DebugAll,"addOpcode %u (%s) line=0x%X",
	oper,getOperator(oper),line);
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
    ExpOperation* op = new ExpOperation(oper,0,ExpOperation::nonInteger(),barrier);
    op->lineNumber(line);
    m_lastOpcode = m_lastOpcode->append(op);
    return op;
}

ExpOperation* ExpEvaluator::addOpcode(ExpEvaluator::Opcode oper, int64_t value, bool barrier)
{
    DDebug(this,DebugAll,"addOpcode %u (%s) " FMT64 " line=0x%X",
	oper,getOperator(oper),value,lineNumber());
    ExpOperation* op = new ExpOperation(oper,0,value,barrier);
    op->lineNumber(lineNumber());
    m_lastOpcode = m_lastOpcode->append(op);
    return op;
}

ExpOperation* ExpEvaluator::addOpcode(ExpEvaluator::Opcode oper, const String& name,
    int64_t value, bool barrier, unsigned int line)
{
    if (!line)
	line = lineNumber();
    DDebug(this,DebugAll,"addOpcode %u (%s) '%s' " FMT64 " line=0x%X",
	oper,getOperator(oper),name.c_str(),value,line);
    ExpOperation* op = new ExpOperation(oper,name,value,barrier);
    op->lineNumber(line);
    m_lastOpcode = m_lastOpcode->append(op);
    return op;
}

ExpOperation* ExpEvaluator::addOpcode(const String& value)
{
    DDebug(this,DebugAll,"addOpcode ='%s' line=0x%X",value.c_str(),lineNumber());
    ExpOperation* op = new ExpOperation(value);
    op->lineNumber(lineNumber());
    m_lastOpcode = m_lastOpcode->append(op);
    return op;
}

ExpOperation* ExpEvaluator::addOpcode(int64_t value)
{
    DDebug(this,DebugAll,"addOpcode =" FMT64 " line=0x%X",value,lineNumber());
    ExpOperation* op = new ExpOperation(value);
    op->lineNumber(lineNumber());
    m_lastOpcode = m_lastOpcode->append(op);
    return op;
}

ExpOperation* ExpEvaluator::addOpcode(bool value)
{
    DDebug(this,DebugAll,"addOpcode =%s line=0x%X",String::boolText(value),lineNumber());
    ExpOperation* op = new ExpOperation(value);
    op->lineNumber(lineNumber());
    m_lastOpcode = m_lastOpcode->append(op);
    return op;
}

ExpOperation* ExpEvaluator::popOpcode()
{
    ObjList* l = &m_opcodes;
    for (ObjList* p = l; p; p = p->next()) {
	if (p->get())
	    l = p;
    }
    return static_cast<ExpOperation*>(l->remove(false));
}

unsigned int ExpEvaluator::getLineOf(ExpOperation* op1, ExpOperation* op2, ExpOperation* op3)
{
    if (op1 && op1->lineNumber())
	return op1->lineNumber();
    if (op2 && op2->lineNumber())
	return op2->lineNumber();
    if (op3 && op3->lineNumber())
	return op3->lineNumber();
    return 0;
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
	XDebug(DebugInfo,"Not popping barrier %u: '%s'='%s'",o->opcode(),o->name().c_str(),o->c_str());
	return 0;
    }
    stack.remove(o,false);
#ifdef DEBUG
    Debug(DebugAll,"popOne: %p%s%s",o,(o ? " " : ""),(o ? o->typeOf() : ""));
#endif
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
#ifdef DEBUG
    Debug(DebugAll,"popAny: %p%s%s '%s'",o,(o ? " " : ""),
	(o ? o->typeOf() : ""),(o ? o->name().safe() : (const char*)0));
#endif
    return o;
}

ExpOperation* ExpEvaluator::popValue(ObjList& stack, GenObject* context) const
{
    ExpOperation* oper = popOne(stack);
    if (!oper || (oper->opcode() != OpcField))
	return oper;
    XDebug(DebugAll,"ExpEvaluator::popValue() field '%s' [%p]",
	oper->name().c_str(),this);
    bool ok = runField(stack,*oper,context);
    TelEngine::destruct(oper);
    return ok ? popOne(stack) : 0;
}

bool ExpEvaluator::runOperation(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"runOperation(%p,%u,%p) %s",&stack,oper.opcode(),context,getOperator(oper.opcode()));
    XDebug(this,DebugAll,"stack: %s",dump(stack).c_str());
    bool boolRes = true;
    switch (oper.opcode()) {
	case OpcPush:
	case OpcField:
	    pushOne(stack,oper.clone());
	    break;
	case OpcCopy:
	    {
		Mutex* mtx = 0;
		ScriptRun* runner = YOBJECT(ScriptRun,&oper);
		if (runner) {
		    if (runner->context())
			mtx = runner->context()->mutex();
		    if (!mtx)
			mtx = runner;
		}
		pushOne(stack,oper.copy(mtx));
	    }
	    break;
	case OpcNone:
	case OpcLabel:
	    break;
	case OpcDrop:
	    TelEngine::destruct(popOne(stack));
	    break;
	case OpcDup:
	    {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		pushOne(stack,op->clone());
		pushOne(stack,op);
	    }
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
	    boolRes = false;
	    // fall through
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
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		}
		switch (oper.opcode()) {
		    case OpcDiv:
		    case OpcMod:
			if (!op2->toNumber())
			    return gotError("Division by zero",oper.lineNumber());
			break;
		    case OpcAdd:
			if (op1->isNumber() && op2->isNumber())
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
		int64_t val = 0;
		bool handled = true;
		switch (oper.opcode()) {
		    case OpcAnd:
			val = op1->valInteger() & op2->valInteger();
			break;
		    case OpcOr:
			val = op1->valInteger() | op2->valInteger();
			break;
		    case OpcXor:
			val = op1->valInteger() ^ op2->valInteger();
			break;
		    case OpcShl:
			val = op1->valInteger() << op2->valInteger();
			break;
		    case OpcShr:
			val = op1->valInteger() >> op2->valInteger();
			break;
		    case OpcLt:
			val = (op1->valInteger() < op2->valInteger()) ? 1 : 0;
			break;
		    case OpcGt:
			val = (op1->valInteger() > op2->valInteger()) ? 1 : 0;
			break;
		    case OpcLe:
			val = (op1->valInteger() <= op2->valInteger()) ? 1 : 0;
			break;
		    case OpcGe:
			val = (op1->valInteger() >= op2->valInteger()) ? 1 : 0;
			break;
		    case OpcEq:
		    case OpcNe:
		    {
			ExpWrapper* w1 = YOBJECT(ExpWrapper,op1);
			ExpWrapper* w2 = YOBJECT(ExpWrapper,op2);
			if (op1->opcode() == op2->opcode() && w1 && w2)
			    val = w1->object() == w2->object() ? 1 : 0;
			else
			    val = (*op1 == *op2) ? 1 : 0;
			if (oper.opcode() == OpcNe)
			    val = val ? 0 : 1;
			break;
		    }
		    default:
			handled = false;
			break;
		}
		if (!handled) {
		    val = ExpOperation::nonInteger();
		    int64_t op1Val = op1->toNumber();
		    int64_t op2Val = op2->toNumber();
		    if (op1Val != ExpOperation::nonInteger() && op2Val != ExpOperation::nonInteger()) {
			switch(oper.opcode()) {
			    case OpcAdd:
				val = op1Val + op2Val;
				break;
			    case OpcSub:
				val = op1Val - op2Val;
				break;
			    case OpcMul:
				val = op1Val * op2Val;
				break;
			    case OpcDiv:
				val = op1Val / op2Val;
				break;
			    case OpcMod:
				val = op1Val % op2Val;
				break;
			    default:
				break;
			}
		    }
		}
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
		if (boolRes) {
		    DDebug(this,DebugAll,"Bool result: '%s'",String::boolText(val != 0));
		    pushOne(stack,new ExpOperation(val != 0));
		}
		else {
		    DDebug(this,DebugAll,"Numeric result: " FMT64,val);
		    pushOne(stack,new ExpOperation(val));
		}
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
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		}
		bool val = false;
		switch (oper.opcode()) {
		    case OpcLAnd:
			val = op1->valBoolean() && op2->valBoolean();
			break;
		    case OpcLOr:
			val = op1->valBoolean() || op2->valBoolean();
			break;
		    default:
			break;
		}
		TelEngine::destruct(op1);
		TelEngine::destruct(op2);
		DDebug(this,DebugAll,"Bool result: '%s'",String::boolText(val));
		pushOne(stack,new ExpOperation(val));
	    }
	    break;
	case OpcCat:
	    {
		ExpOperation* op2 = popValue(stack,context);
		ExpOperation* op1 = popValue(stack,context);
		if (!op1 || !op2) {
		    TelEngine::destruct(op1);
		    TelEngine::destruct(op2);
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
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
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		}
		pushOne(stack,op1->clone(*op2));
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
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		switch (oper.opcode()) {
		    case OpcNeg:
			pushOne(stack,new ExpOperation(-op->toNumber()));
			break;
		    case OpcNot:
			pushOne(stack,new ExpOperation(~op->valInteger()));
			break;
		    case OpcLNot:
			pushOne(stack,new ExpOperation(!op->valBoolean()));
			break;
		    default:
			pushOne(stack,new ExpOperation(op->valInteger()));
			break;
		}
		TelEngine::destruct(op);
	    }
	    break;
	case OpcFunc:
	    return runFunction(stack,oper,context) ||
		gotError("Function '" + oper.name() + "' call failed",oper.lineNumber());
	case OpcIncPre:
	case OpcDecPre:
	case OpcIncPost:
	case OpcDecPost:
	    {
		ExpOperation* fld = popOne(stack);
		if (!fld)
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		if (fld->opcode() != OpcField) {
		    TelEngine::destruct(fld);
		    return gotError("Expecting LValue in operator",oper.lineNumber());
		}
		ExpOperation* val = 0;
		if (!(runField(stack,*fld,context) && (val = popOne(stack)))) {
		    TelEngine::destruct(fld);
		    return false;
		}
		int64_t num = val->valInteger();
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
		bool ok = runAssign(stack,*fld,context);
		TelEngine::destruct(fld);
		if (!ok) {
		    TelEngine::destruct(val);
		    return gotError("Assignment failed",oper.lineNumber());
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
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		}
		if (fld->opcode() != OpcField) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(val);
		    return gotError("Expecting LValue in assignment",oper.lineNumber());
		}
		ExpOperation* op = val->clone(fld->name());
		TelEngine::destruct(fld);
		bool ok = runAssign(stack,*op,context);
		TelEngine::destruct(op);
		if (!ok) {
		    TelEngine::destruct(val);
		    return gotError("Assignment failed",oper.lineNumber());
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
		    return gotError("ExpEvaluator stack underflow",oper.lineNumber());
		}
		if (fld->opcode() != OpcField) {
		    TelEngine::destruct(fld);
		    TelEngine::destruct(val);
		    return gotError("Expecting LValue in assignment",oper.lineNumber());
		}
		pushOne(stack,fld->clone());
		pushOne(stack,fld);
		pushOne(stack,val);
		ExpOperation op((Opcode)(oper.opcode() & ~OpcAssign),
		    oper.name(),oper.number(),oper.barrier());
		op.lineNumber(oper.lineNumber());
		if (!runOperation(stack,op,context))
		    return false;
		ExpOperation assign(OpcAssign);
		assign.lineNumber(oper.lineNumber());
		return runOperation(stack,assign,context);
	    }
	    Debug(this,DebugStub,"Please implement operation %u '%s'",
		oper.opcode(),getOperator(oper.opcode()));
	    return false;
    }
    return true;
}

bool ExpEvaluator::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"runFunction(%p,'%s' " FMT64 ", %p) ext=%p",
	&stack,oper.name().c_str(),oper.number(),context,(void*)m_extender);
    if (oper.name() == YSTRING("chr")) {
	String res;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* o = popValue(stack,context);
	    if (!o)
		return gotError("ExpEvaluator stack underflow",oper.lineNumber());
	    res = String((char)o->number()) + res;
	    TelEngine::destruct(o);
	}
	pushOne(stack,new ExpOperation(res));
	return true;
    }
    if (oper.name() == YSTRING("now")) {
	if (oper.number())
	    return gotError("Function expects no arguments",oper.lineNumber());
	pushOne(stack,new ExpOperation((int64_t)Time::secNow()));
	return true;
    }
    return m_extender && m_extender->runFunction(stack,oper,context);
}

bool ExpEvaluator::runField(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"runField(%p,'%s',%p) ext=%p",
	&stack,oper.name().c_str(),context,(void*)m_extender);
    return m_extender && m_extender->runField(stack,oper,context);
}

bool ExpEvaluator::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"runAssign('%s'='%s',%p) ext=%p",
	oper.name().c_str(),oper.c_str(),context,(void*)m_extender);
    return m_extender && m_extender->runAssign(stack,oper,context);
}

bool ExpEvaluator::runEvaluate(const ObjList& opcodes, ObjList& stack, GenObject* context) const
{
    DDebug(this,DebugInfo,"runEvaluate(%p,%p,%p)",&opcodes,&stack,context);
    for (const ObjList* l = opcodes.skipNull(); l; l = l->skipNext()) {
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	if (!runOperation(stack,*o,context))
	    return false;
    }
    return true;
}

bool ExpEvaluator::runEvaluate(const ObjVector& opcodes, ObjList& stack, GenObject* context, unsigned int index) const
{
    DDebug(this,DebugInfo,"runEvaluate(%p,%p,%p,%u)",&opcodes,&stack,context,index);
    for (; index < opcodes.length(); index++) {
	const ExpOperation* o = static_cast<const ExpOperation*>(opcodes[index]);
	if (o && !runOperation(stack,*o,context))
	    return false;
    }
    return true;
}

bool ExpEvaluator::runEvaluate(ObjList& stack, GenObject* context) const
{
    return runEvaluate(m_opcodes,stack,context);
}

bool ExpEvaluator::runAllFields(ObjList& stack, GenObject* context) const
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

int ExpEvaluator::compile(ParsePoint& expr, GenObject* context)
{
    if (!expr.m_eval)
	expr.m_eval = this;
    if (!skipComments(expr,context))
	return 0;
    int res = 0;
    for (;;) {
	int pre;
	m_inError = false;
	while ((pre = preProcess(expr,context)) >= 0)
	    res += pre;
	if (inError())
	    return 0;
	if (!runCompile(expr))
	    return 0;
	res++;
	bool sep = false;
	while (getSeparator(expr,true))
	    sep = true;
	if (!sep)
	    break;
    }
    return skipComments(expr,context) ? 0 : res;
}

bool ExpEvaluator::evaluate(ObjList* results, GenObject* context) const
{
    if (results) {
	results->clear();
	return runEvaluate(*results,context) &&
	    (runAllFields(*results,context) || gotError("Could not evaluate all fields"));
    }
    ObjList res;
    return runEvaluate(res,context);
}

int ExpEvaluator::evaluate(NamedList& results, unsigned int index, const char* prefix, GenObject* context) const
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

int ExpEvaluator::evaluate(Array& results, unsigned int index, GenObject* context) const
{
    Debug(this,DebugStub,"Please implement ExpEvaluator::evaluate(Array)");
    return -1;
}

void ExpEvaluator::dump(const ExpOperation& oper, String& res, bool lineNo) const
{
    switch (oper.opcode()) {
	case OpcPush:
	case OpcCopy:
	    if (oper.isInteger())
		res << oper.number();
	    else
		res << "'" << oper << "'";
	    break;
	case OpcField:
	    res << oper.name();
	    break;
	case OpcFunc:
	    res << oper.name() << "(" << oper.number() << ")";
	    break;
	default:
	    {
	        const char* name = getOperator(oper.opcode());
		if (name)
		    res << name;
		else
		    res << "[" << oper.opcode() << "]";
	    }
	    if (oper.number() && oper.isInteger())
		res << "(" << oper.number() << ")";
    }
    if (lineNo && oper.lineNumber()) {
	char buf[24];
	::sprintf(buf," (@0x%X)",oper.lineNumber());
	res << buf;
    }
}

void ExpEvaluator::dump(const ObjList& codes, String& res, bool lineNo) const
{
    for (const ObjList* l = codes.skipNull(); l; l = l->skipNext()) {
	if (res)
	    res << " ";
	const ExpOperation* o = static_cast<const ExpOperation*>(l->get());
	dump(*o,res,lineNo);
    }
}

void ExpEvaluator::dump(String& res, bool lineNo) const
{
    return dump(m_opcodes,res,lineNo);
}

int64_t ExpOperation::valInteger(int64_t defVal) const
{
    return isInteger() ? number() : defVal;
}

int64_t ExpOperation::toNumber() const
{
    if (isInteger())
	return number();
    return toInt64(nonInteger());
}

bool ExpOperation::valBoolean(bool defVal) const
{
    return isInteger() ? (number() != 0) : (defVal || !null());
}

const char* ExpOperation::typeOf() const
{
    switch (opcode()) {
	case ExpEvaluator::OpcPush:
	case ExpEvaluator::OpcCopy:
	    return isInteger() ? ( isBoolean() ? "boolean" : "number" ) : (isNumber() ? "number" : "string");
	case ExpEvaluator::OpcFunc:
	    return "function";
	default:
	    return "internal";
    }
}

ExpOperation* ExpOperation::clone(const char* name) const
{
    ExpOperation* op = new ExpOperation(*this,name);
    op->lineNumber(lineNumber());
    return op;
}


ExpOperation* ExpFunction::clone(const char* name) const
{
    XDebug(DebugInfo,"ExpFunction::clone('%s') [%p]",name,this);
    ExpFunction* op = new ExpFunction(name,(long int)number());
    op->lineNumber(lineNumber());
    return op;
}


ExpOperation* ExpWrapper::clone(const char* name) const
{
    RefObject* r = YOBJECT(RefObject,object());
    XDebug(DebugInfo,"ExpWrapper::clone('%s') %s=%p [%p]",
	name,(r ? "ref" : "obj"),object(),this);
    if (r)
	r->ref();
    ExpWrapper* op = new ExpWrapper(object(),name);
    static_cast<String&>(*op) = *this;
    op->lineNumber(lineNumber());
    return op;
}

ExpOperation* ExpWrapper::copy(Mutex* mtx) const
{
    JsObject* jso = YOBJECT(JsObject,m_object);
    if (!jso)
	return ExpOperation::clone();
    XDebug(DebugInfo,"ExpWrapper::copy(%p) [%p]",mtx,this);
    ExpWrapper* op = new ExpWrapper(jso->copy(mtx),name());
    static_cast<String&>(*op) = *this;
    op->lineNumber(lineNumber());
    return op;
}

const char* ExpWrapper::typeOf() const
{
    switch (opcode()) {
	case ExpEvaluator::OpcPush:
	case ExpEvaluator::OpcCopy:
	    return object() ? "object" : "undefined";
	default:
	    return ExpOperation::typeOf();
    }
}

bool ExpWrapper::valBoolean(bool defVal) const
{
    if (!m_object)
	return defVal;
    return !JsParser::isNull(*this);
}

void* ExpWrapper::getObject(const String& name) const
{
    if (name == YATOM("ExpWrapper"))
	return const_cast<ExpWrapper*>(this);
    void* obj = ExpOperation::getObject(name);
    if (obj)
	return obj;
    return m_object ? m_object->getObject(name) : 0;
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

bool TableEvaluator::evalWhere(GenObject* context)
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

bool TableEvaluator::evalSelect(ObjList& results, GenObject* context)
{
    if (m_select.null())
	return false;
    return m_select.evaluate(results,context);
}

unsigned int TableEvaluator::evalLimit(GenObject* context)
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
	    int lim = (int)o->number();
	    if (lim < 0)
		lim = 0;
	    m_limitVal = lim;
	    break;
	}
    }
    return m_limitVal;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
