/**
 * script.cpp
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

ScriptParser::~ScriptParser()
{
    TelEngine::destruct(m_code);
}

void ScriptParser::setCode(ScriptCode* code)
{
    ScriptCode* tmp = m_code;
    if (tmp == code)
	return;
    if (code)
	code->ref();
    m_code = code;
    TelEngine::destruct(tmp);
}


bool ScriptContext::runFunction(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context)
{
    return false;
}

bool ScriptContext::runField(const ExpEvaluator* eval, ObjList& stack, const ExpOperation& oper, void* context)
{
    if (!eval)
	return false;
    XDebug(DebugAll,"ScriptContext::runField '%s'",oper.name().c_str());
    ExpEvaluator::pushOne(stack,new ExpOperation(m_params[oper.name()],oper.name()));
    return true;
}

bool ScriptContext::runAssign(const ExpEvaluator* eval, const ExpOperation& oper, void* context)
{
    if (!eval)
	return false;
    XDebug(DebugAll,"ScriptContext::runAssign '%s'='%s'",oper.name().c_str(),oper.c_str());
    m_params.setParam(oper.name(),oper);
    return true;
}

#define MAKE_NAME(x) { #x, ScriptRun::x }
static const TokenDict s_states[] = {
    MAKE_NAME(Invalid),
    MAKE_NAME(Running),
    MAKE_NAME(Incomplete),
    MAKE_NAME(Succeeded),
    MAKE_NAME(Failed),
    { 0, 0 }
};
#undef MAKE_NAME

ScriptRun::ScriptRun(ScriptCode* code, ScriptContext* context)
    : Mutex(true,"ScriptRun"),
      m_state(Invalid)
{
    if (code)
	code->ref();
    m_code = code;
    if (context)
	context->ref();
    else
	context = new ScriptContext;
    m_context = context;
    reset();
}

ScriptRun::~ScriptRun()
{
    lock();
    m_state = Invalid;
    TelEngine::destruct(m_code);
    TelEngine::destruct(m_context);
    unlock();
}

const char* ScriptRun::textState(Status state)
{
    return lookup(state,s_states,"Unknown");
}

// Reset script (but not the context) to initial state
ScriptRun::Status ScriptRun::reset()
{
    Lock mylock(this);
    // TODO
    m_stack.clear();
    return (m_state = (m_code && m_code->initialize(m_context)) ? Incomplete : Invalid);
}

// Resume execution, run one or more instructions of code
ScriptRun::Status ScriptRun::resume()
{
    Lock mylock(this);
    if (Running != m_state)
	return m_state;
    RefPointer<ScriptCode> code = m_code;
    RefPointer<ScriptContext> ctxt = m_context;
    if (!(code && ctxt))
	return Invalid;
    mylock.drop();
    return code->evaluate(*ctxt,stack()) ? Succeeded : Failed;
}

// Execute one or more instructions of code from where it was left
ScriptRun::Status ScriptRun::execute()
{
    Lock mylock(this);
    if (Incomplete != m_state)
	return m_state;
    m_state = Running;
    mylock.drop();
    Status st = resume();
    if (Running == st)
	st = Incomplete;
    lock();
    if (Running == m_state)
	m_state = st;
    unlock();
    return st;
}

// Execute instructions until succeeds or fails
ScriptRun::Status ScriptRun::run()
{
    reset();
    ScriptRun::Status s = state();
    while (Incomplete == s)
	s = execute();
    return s;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
