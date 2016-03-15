/**
 * javascript.cpp
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
#include <yatengine.h>

//#define STATS_TRACE "jstrace"

using namespace TelEngine;

namespace { // anonymous

class ParseNested;
class JsRunner;
class JsCodeStats;

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
    bool runStringFunction(GenObject* obj, const String& name, ObjList& stack, const ExpOperation& oper, GenObject* context);
    bool runStringField(GenObject* obj, const String& name, ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    GenObject* resolveTop(ObjList& stack, const String& name, GenObject* context);
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
    virtual ExpOperation* copy(Mutex* mtx) const
	{ return clone(name()); }
protected:
    inline ExpNull(JsNull* obj, const char* name)
	: ExpWrapper(obj,name)
	{ obj->ref(); }
};

class JsCodeFile : public String
{
public:
    inline JsCodeFile(const String& file)
	: String(file), m_fileTime(0)
	{ File::getFileTime(file,m_fileTime); }
    inline JsCodeFile(const String& file, unsigned int fTime)
	: String(file), m_fileTime(fTime)
	{ }
    inline unsigned int fileTime() const
	{ return m_fileTime; }
    inline bool fileChanged() const
	{ unsigned int t = 0; File::getFileTime(c_str(),t); return t != m_fileTime; }
private:
    unsigned int m_fileTime;
};

struct JsEntry
{
    long int number;
    unsigned int index;
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
	OpcDelete,
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
	OpcPragma,
    };
    inline JsCode()
	: ExpEvaluator(C),
	  m_pragmas(""), m_label(0), m_depth(0), m_entries(0), m_traceable(false)
	{ debugName("JsCode"); }
    ~JsCode();
    virtual void* getObject(const String& name) const
    {
	if (name == YATOM("JsCode"))
            return const_cast<JsCode*>(this);
	if (name == YATOM("ExpEvaluator"))
            return const_cast<ExpEvaluator*>((const ExpEvaluator*)this);
	return ScriptCode::getObject(name);
    }
    virtual bool initialize(ScriptContext* context) const;
    virtual bool evaluate(ScriptRun& runner, ObjList& results) const;
    virtual ScriptRun* createRunner(ScriptContext* context, const char* title);
    virtual bool null() const;
    virtual void dump(String& res, bool loneNo = false) const;
    bool link();
    inline bool traceable() const
	{ return m_traceable; }
    JsObject* parseArray(ParsePoint& expr, bool constOnly, Mutex* mtx);
    JsObject* parseObject(ParsePoint& expr, bool constOnly, Mutex* mtx);
    inline const NamedList& pragmas() const
	{ return m_pragmas; }
    inline static unsigned int getLineNo(unsigned int line)
	{ return line & 0xffffff; }
    inline static unsigned int getFileNo(unsigned int line)
	{ return (line >> 24) & 0xff; }
    inline unsigned int getFileCount() const
	{ return m_included.length(); }
    const String& getFileAt(unsigned int index) const;
    inline const String& getFileName(unsigned int line) const
	{ return getFileAt(getFileNo(line)); }
    bool scriptChanged() const;
protected:
    inline void trace(bool allowed)
	{ m_traceable = allowed; }
    void setBaseFile(const String& file);
    virtual void formatLineNo(String& buf, unsigned int line) const;
    virtual bool getString(ParsePoint& expr);
    virtual bool getEscape(const char*& expr, String& str, char sep);
    virtual bool keywordLetter(char c) const;
    virtual int getKeyword(const char* str) const;
    virtual char skipComments(ParsePoint& expr, GenObject* context = 0);
    virtual int preProcess(ParsePoint& expr, GenObject* context = 0);
    virtual bool getInstruction(ParsePoint& expr, char stop, GenObject* nested);
    virtual bool getSimple(ParsePoint& expr, bool constOnly = false);
    virtual Opcode getOperator(ParsePoint& expr);
    virtual Opcode getUnaryOperator(ParsePoint& expr);
    virtual Opcode getPostfixOperator(ParsePoint& expr, int precedence);
    virtual const char* getOperator(Opcode oper) const;
    virtual int getPrecedence(ExpEvaluator::Opcode oper) const;
    virtual bool getSeparator(ParsePoint& expr, bool remove);
    virtual bool runOperation(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context) const;
private:
    ObjVector m_linked;
    ObjList m_included;
    ObjList m_globals;
    NamedList m_pragmas;
    bool preProcessInclude(ParsePoint& expr, bool once, GenObject* context);
    bool preProcessPragma(ParsePoint& expr, GenObject* context);
    bool getOneInstruction(ParsePoint& expr, GenObject* nested);
    bool parseInner(ParsePoint& expr, JsOpcode opcode, ParseNested* nested);
    bool parseIf(ParsePoint& expr, GenObject* nested);
    bool parseSwitch(ParsePoint& expr, GenObject* nested);
    bool parseFor(ParsePoint& expr, GenObject* nested);
    bool parseWhile(ParsePoint& expr, GenObject* nested);
    bool parseVar(ParsePoint& expr);
    bool parseTry(ParsePoint& expr, GenObject* nested);
    bool parseFuncDef(ParsePoint& expr, bool publish);
    bool parseSimple(ParsePoint& expr, bool constOnly, Mutex* mtx = 0);
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
    void resolveObjectParams(JsObject* object, ObjList& stack, GenObject* context, JsContext* ctxt,
	    JsObject* objProto, JsArray* arrayProto) const;
    inline JsFunction* getGlobalFunction(const String& name) const
	{ return YOBJECT(JsFunction,m_globals[name]); }
    long int m_label;
    int m_depth;
    JsEntry* m_entries;
    bool m_traceable;
};

class JsIterator : public RefObject
{
    YCLASS(JsIterator,RefObject)
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

class JsLineStats : public GenObject
{
public:
    inline JsLineStats(unsigned int lineNo, unsigned int instr, u_int64_t usec)
	: lineNumber(lineNo), operations(instr), microseconds(usec), isCall(false)
	{ }
    unsigned int lineNumber;
    unsigned int operations;
    u_int64_t microseconds;
    bool isCall;
};

class JsCallStats : public JsLineStats
{
public:
    inline JsCallStats(const char* name, unsigned int caller, unsigned int called, unsigned int instr, u_int64_t usec)
	: JsLineStats(caller,instr,usec),
	  funcName(name), callsCount(1), calledLine(called)
	{ isCall = true; }
    String funcName;
    unsigned int callsCount;
    unsigned int calledLine;
};

class JsFuncStats : public String
{
public:
    inline JsFuncStats(const char* name, unsigned int lineNo)
	: String(name), lineNumber(lineNo)
	{ }
    unsigned int lineNumber;
    ObjList funcLines;
    void updateLine(unsigned int lineNo, u_int64_t usec);
    void updateCall(const char* name, unsigned int caller, unsigned int called, unsigned int instr, u_int64_t usec);
};

class JsCodeStats : public Mutex, public RefObject
{
    YCLASS(JsCodeStats,RefObject)
public:
    JsCodeStats(JsCode* code, const char* file = 0);
    ~JsCodeStats();
    virtual const String& toString() const
	{ return m_fileName; }
    JsFuncStats* getFuncStats(const char* name, unsigned int lineNo);
    void dump();
    void dump(const char* file);
    void dump(Stream& file);
private:
    RefPointer<JsCode> m_code;
    String m_fileName;
    ObjList m_funcStats;
};

class JsCallInfo : public String
{
    friend class JsRunner;
private:
    inline JsCallInfo(JsFuncStats* stats, const char* name,
	unsigned int caller, unsigned int called, unsigned int instr, u_int64_t time)
	: String(name),
	  funcStats(stats), callerLine(caller), calledLine(called), startInstr(instr), startTime(time)
	{ }
    inline void traceLine(unsigned int line, u_int64_t time)
	{ if (funcStats) funcStats->updateLine(line,time); }
    inline void traceCall(const JsCallInfo* call, unsigned int instr, u_int64_t time)
	{ if (call && funcStats) funcStats->updateCall(call->c_str(),call->callerLine,call->calledLine,instr,time); }

    JsFuncStats* funcStats;
    unsigned int callerLine;
    unsigned int calledLine;
    unsigned int startInstr;
    u_int64_t startTime;
};

class JsRunner : public ScriptRun
{
    YCLASS(JsRunner,ScriptRun)
    friend class JsCode;
public:
    inline JsRunner(ScriptCode* code, ScriptContext* context, const char* title)
	: ScriptRun(code,context),
	  m_paused(false), m_tracing(false), m_opcode(0), m_index(0),
	  m_instr(0), m_lastLine(0), m_lastTime(0), m_totalTime(0), m_callInfo(0)
	{ traceCheck(title); }
    virtual ~JsRunner()
	{ if (m_tracing) traceDump(); }
    inline bool tracing() const
	{ return m_tracing; }
    virtual Status reset(bool init);
    virtual bool pause();
    virtual Status call(const String& name, ObjList& args, ExpOperation* thisObj = 0, ExpOperation* scopeObj = 0);
    virtual bool callable(const String& name);
    void traceStart(const char* title, const char* file = 0);
    void tracePrep(const ExpOperation& oper);
    void tracePost(const ExpOperation& oper);
    void traceCall(const ExpOperation& oper, const JsFunction& func);
    void traceReturn();
protected:
    virtual Status resume();
    void traceDump();
    void traceCheck(const char* title);
    void traceStart(const char* title, JsCodeStats* stats);
private:
    bool m_paused;
    bool m_tracing;
    const ObjList* m_opcode;
    unsigned int m_index;
    unsigned int m_instr;
    unsigned int m_lastLine;
    u_int64_t m_lastTime;
    u_int64_t m_totalTime;
    JsCallInfo* m_callInfo;
    ObjList m_traceStack;
    RefPointer<JsCodeStats> m_stats;
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
    static bool parseInner(GenObject* nested, JsCode::JsOpcode opcode, ParsePoint& expr)
	{ ParseNested* inner = findMatch(nested,opcode);
	    return inner && inner->parseInner(expr,opcode); }
protected:
    virtual bool isMatch(JsCode::JsOpcode opcode)
	{ return false; }
    inline bool parseInner(ParsePoint& expr, JsCode::JsOpcode opcode)
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

class NativeFields : public ObjList
{
public:
    inline NativeFields()
    {
	append(new String("length"));
	append(new String("charAt"));
	append(new String("charCodeAt"));
	append(new String("indexOf"));
	append(new String("substr"));
	append(new String("match"));
	append(new String("toLowerCase"));
	append(new String("toUpperCase"));
	append(new String("trim"));
	append(new String("sqlEscape"));
	append(new String("startsWith"));
	append(new String("endsWith"));
	append(new String("split"));
	append(new String("toString"));
	append(new String("isNaN"));
	append(new String("parseInt"));
    }
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
    MAKEOP("#pragma", Pragma),
    { 0, 0 }
};
#undef MAKEOP

#define MAKEOP(o) { "[" #o "]", JsCode::Opc ## o }
static const TokenDict s_internals[] =
{
    MAKEOP(Field),
    MAKEOP(Func),
    MAKEOP(Push),
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
static const String s_noFile = "[no file]";
static const NativeFields s_nativeFields;

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
	name.clear();
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
	    name.append(*s,".");
	    if (!l2)
		break;
	    ExpExtender* ext = YOBJECT(ExpExtender,obj);
	    if (ext) {
		GenObject* adv = ext->getField(stack,name,context);
		XDebug(DebugAll,"JsContext::resolve advanced to '%s' of %p for '%s'",
		    (adv ? adv->toString().c_str() : 0),ext,s->c_str());
		if (adv) {
		    if (YOBJECT(ExpExtender,adv)) {
			obj = adv;
			name.clear();
		    }
		    else if (l->count() == 2) { // there is only one other field after this one
			s = static_cast<const String*>(l2->get());
			if (!TelEngine::null(s) && s_nativeFields.find(*s)) {
			    obj = adv;
			    name.clear();
			}
		    }
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
	int64_t val = ExpOperation::nonInteger();
	ObjList args;
	extractArgs(stack,oper,context,args);
	ExpOperation* op1 = static_cast<ExpOperation*>(args[0]);
	if (op1) {
	    int base = 0;
	    ExpOperation* op2 = static_cast<ExpOperation*>(args[1]);
	    if (op2) {
		base = (int)op2->valInteger();
		if (base < 2 || base > 36)
		    base = 0;
	    }
	    val = op1->trimSpaces().toInt64(val,base);
	}
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
		idx = (int)op->number();
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(String(str->at(idx))));
	return true;
    }
    if (name == YSTRING("charCodeAt")) {
	int idx = 0;
	ObjList args;
	if (extractArgs(stack,oper,context,args)) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    if (op && op->isInteger())
		idx = (int)op->number();
	}
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)(uint8_t)str->at(idx)));
	return true;
    }
    if (name == YSTRING("indexOf")) {
	int idx = -1;
	ObjList args;
	if (extractArgs(stack,oper,context,args)) {
	    const String* what = static_cast<String*>(args[0]);
	    if (what) {
		ExpOperation* from = static_cast<ExpOperation*>(args[1]);
		int offs = (from && from->isInteger()) ? (int)from->number() : 0;
		if (offs < 0)
		    offs = 0;
		idx = str->find(*what,offs);
	    }
	}
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)idx));
	return true;
    }
    if (name == YSTRING("substr")) {
	ObjList args;
	int offs = 0;
	int len = -1;
	if (extractArgs(stack,oper,context,args)) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    if (op && op->isInteger())
		offs = (int)op->number();
	    op = static_cast<ExpOperation*>(args[1]);
	    if (op && op->isInteger()) {
		len = (int)op->number();
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
		JsArray* jsa = new JsArray(context,mutex());
		for (int i = 0; i <= buf.matchCount(); i++)
		    jsa->push(new ExpOperation(buf.matchString(i)));
		jsa->params().addParam(new ExpOperation((int64_t)buf.matchOffset(),"index"));
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
    if (name == YSTRING("sqlEscape")) {
	NO_PARAM_STRING_METHOD(sqlEscape);
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
	JsArray* array = new JsArray(context,mutex());
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
    if (name == YSTRING("toString")) {
	ObjList args;
	extractArgs(stack,oper,context,args);
	ExpOperation* op = YOBJECT(ExpOperation,str);
	if (op && op->isInteger()) {
	    if (op->isBoolean()) {
		ExpEvaluator::pushOne(stack,new ExpOperation(String::boolText(op->valBoolean())));
		return true;
	    }
	    ExpOperation* tmp = static_cast<ExpOperation*>(args[0]);
	    int radix = tmp ? (int)tmp->valInteger() : 0;
	    if (radix < 2 || radix > 36)
		radix = 10;
	    static const char s_base[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	    int64_t n = op->valInteger();
	    bool neg = false;
	    if (n < 0) {
		n = -n;
		neg = true;
	    }
	    String s;
	    char buf[2];
	    buf[1] = '\0';
	    do {
		buf[0] = s_base[n % radix];
		s = buf + s;
	    } while ((n = n / radix));
	    tmp = static_cast<ExpOperation*>(args[1]);
	    int len = tmp ? (int)tmp->valInteger() : 0;
	    if (len > 1) {
		if (neg)
		    len--;
		while (len > (int)s.length())
		    s = "0" + s;
	    }
	    if (neg)
		s = "-" + s;
	    ExpEvaluator::pushOne(stack,new ExpOperation(s));
	    return true;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(*str));
	return true;
    }
    return false;
}

bool JsContext::runStringField(GenObject* obj, const String& name, ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    const String* s = YOBJECT(String,obj);
    if (!s)
	return false;
    if (name == YSTRING("length")) {
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)s->length()));
	return true;
    }
    return false;
}

bool JsContext::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsContext::runAssign '%s'='%s' (%s) [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),this);
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


JsCode::~JsCode()
{
    delete[] m_entries;
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
    delete[] m_entries;
    m_entries = 0;
    unsigned int n = m_linked.count();
    if (!n)
	return false;
    unsigned int entries = 0;
    for (unsigned int i = 0; i < n; i++) {
	const ExpOperation* l = static_cast<const ExpOperation*>(m_linked[i]);
	if (!l || l->opcode() != OpcLabel)
	    continue;
	long int lbl = (long int)l->number();
	if (lbl >= 0 && l->barrier())
	    entries++;
	for (unsigned int j = 0; j < n; j++) {
	    const ExpOperation* jmp = static_cast<const ExpOperation*>(m_linked[j]);
	    if (!jmp || jmp->number() != lbl)
		continue;
	    Opcode op = OpcNone;
	    switch ((int)jmp->opcode()) {
		case OpcJump:
		    op = (Opcode)OpcJRel;
		    break;
		case OpcJumpTrue:
		    op = (Opcode)OpcJRelTrue;
		    break;
		case OpcJumpFalse:
		    op = (Opcode)OpcJRelFalse;
		    break;
		default:
		    continue;
	    }
	    long int offs = (long int)i - j;
	    ExpOperation* newJump = new ExpOperation(op,0,offs,jmp->barrier());
	    newJump->lineNumber(jmp->lineNumber());
	    m_linked.set(newJump,j);
	}
    }
    if (entries) {
	m_entries = new JsEntry[entries+1];
	unsigned int e = 0;
	for (unsigned int j = 0; j < n; j++) {
	    const ExpOperation* l = static_cast<const ExpOperation*>(m_linked[j]);
	    if (l && l->barrier() && l->opcode() == OpcLabel && l->number() >= 0) {
		m_entries[e].number = (long int)l->number();
		m_entries[e++].index = j;
	    }
	}
	m_entries[entries].number = -1;
	m_entries[entries].index = 0;
    }
    return true;
}

const String& JsCode::getFileAt(unsigned int index) const
{
    if (!index)
	return s_noFile;
    const GenObject* file = m_included[index - 1];
    return file ? file->toString() : s_noFile;
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

bool JsCode::getString(ParsePoint& expr)
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

bool JsCode::keywordLetter(char c) const
{
    return ExpEvaluator::keywordLetter(c) || (c == '$');
}

int JsCode::getKeyword(const char* str) const
{
    int len = 0;
    const char*s = str;
    for (;; len++) {
	char c = *s++;
	if (c <= ' ')
	    break;
	if (!len && keywordDigit(c))
	    return 0;
	if (keywordChar(c))
	    continue;
	if (len && (c == '.')) {
	    if (!keywordLetter(s[0]))
		return 0;
	    continue;
	}
	break;
    }
    if (len > 1 && (s[-2] == '.'))
	len--;
    if (len && ExpEvaluator::getOperator(str,s_instr) != OpcNone)
	return 0;
    return len;
}

char JsCode::skipComments(ParsePoint& expr, GenObject* context)
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

void JsCode::setBaseFile(const String& file)
{
    if (file.null() || m_depth || m_included.find(file))
	return;
    m_included.append(new JsCodeFile(file));
    int idx = m_included.index(file);
    m_lineNo = ((idx + 1) << 24) | 1;
}

bool JsCode::scriptChanged() const
{
    for (const ObjList* l = m_included.skipNull(); l; l = l->skipNext())
	if (static_cast<const JsCodeFile*>(l->get())->fileChanged())
	    return true;
    return false;
}

bool JsCode::preProcessInclude(ParsePoint& expr, bool once, GenObject* context)
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
	    parser->adjustPath(str,true);
	    str.trimSpaces();
	    bool ok = !str.null();
	    if (ok) {
		int idx = m_included.index(str);
		if (!(once && (idx >= 0))) {
		    if (idx < 0) {
			String* s = new JsCodeFile(str);
			m_included.append(s);
			idx = m_included.index(s);
		    }
		    // use the upper bits of line # for file index
		    unsigned int savedLine = expr.m_lineNo;
		    expr.m_lineNo = m_lineNo = ((idx + 1) << 24) | 1;
		    m_depth++;
		    ok = parser->parseFile(str,true);
		    m_depth--;
		    expr.m_lineNo = m_lineNo = savedLine;
		}
	    }
	    return ok || gotError("Failed to include " + str);
	}
	return false;
    }
    return gotError("Expecting include file",expr);
}

bool JsCode::preProcessPragma(ParsePoint& expr, GenObject* context)
{
    skipComments(expr);
    int len = ExpEvaluator::getKeyword(expr);
    if (len <= 0)
	return gotError("Expecting pragma code",expr);
    ParsePoint str = expr;
    str += len;
    char c = skipComments(str);
    if (c == '"' || c == '\'') {
	String val;
	if (ExpEvaluator::getString(str,val)) {
	    m_pragmas.setParam(String(expr,len),val);
	    expr = str;
	    return true;
	}
	return gotError("Expecting pragma value",expr);
    }
    return gotError("Expecting pragma string",expr);
}

int JsCode::preProcess(ParsePoint& expr, GenObject* context)
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
	    case OpcPragma:
		if (!preProcessPragma(expr,context))
		    return -1;
		break;
	    default:
		return rval;
	}
    }
}

bool JsCode::getOneInstruction(ParsePoint& expr, GenObject* nested)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"JsCode::getOneInstruction %p '%.30s'",nested,(const char*)expr);
    const char* savedSeps = expr.m_searchedSeps;
    unsigned int count = expr.m_count;
    if (skipComments(expr) == '{') {
	expr.m_searchedSeps = "}";
	if (!getInstruction(expr,0,nested))
	    return false;
    }
    else {
	expr.m_searchedSeps = ";}";
	expr.m_count = 0;
	if (!runCompile(expr,";}",nested))
	    return false;
	if (skipComments(expr)  == ';') {
	    expr.m_foundSep = ';';
	    expr++;
	}
    }
    expr.m_searchedSeps = savedSeps;
    if (!expr.m_searchedSeps || expr.m_count)
	expr.m_foundSep = 0;
    expr.m_count = count;
    return true;
}

bool JsCode::getInstruction(ParsePoint& expr, char stop, GenObject* nested)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"JsCode::getInstruction %p '%.1s' 'separators=%s' 'count=%u' '%.30s'",nested,&stop,expr.m_searchedSeps,
	   expr.m_count,(const char*)expr);
    if (skipComments(expr) == '{') {
	if (stop == ')')
	    return false;
	expr++;
	expr.m_count++;
	for (;;) {
	    if (!runCompile(expr,'}',nested))
		return false;
	    bool sep = false;
	    while (skipComments(expr) && getSeparator(expr,true))
		sep = true;
	    if (*expr.m_expr == '}' || !sep)
		break;
	}
	if (*expr != '}')
	    return gotError("Expecting '}'",expr);
	expr++;
	expr.m_foundSep = '}';
	if (expr.m_count > 0)
	    expr.m_count--;
	return true;
    }
    else if (*expr == ';') {
	expr++;
	expr.m_foundSep = ';';
	return true;
    }
    expr.m_foundSep = 0;
    ParsePoint saved = expr;
    Opcode op = ExpEvaluator::getOperator(expr,s_instr);
    switch ((int)op) {
	case OpcNone:
	    return false;
	case OpcThrow:
	    if (!runCompile(expr))
		return false;
	    addOpcode(op);
	    break;
	case OpcReturn:
	{
	    int64_t pop = ExpOperation::nonInteger();
	    switch (skipComments(expr)) {
		case ';':
		case '}':
		    break;
		case '{':
		    {
			saved = expr;
			JsObject* jso = parseObject(expr,false,0);
			if (!jso)
			    return gotError("Expecting valid object",saved);
			if (skipComments(expr) != ';') {
			    TelEngine::destruct(jso);
			    return gotError("Expecting ';'",expr);
			}
			addOpcode(new ExpWrapper(ExpEvaluator::OpcCopy,jso));
			pop = 1;
		    }
		    break;
		default:
		    if (!runCompile(expr,';'))
			return false;
		    pop = 1;
		    if ((skipComments(expr) != ';') && (*expr != '}'))
			return gotError("Expecting ';' or '}'",expr);
	    }
	    addOpcode(op,pop);
	    break;
	}
	case OpcIf:
	    return parseIf(expr,nested);
	case OpcElse:
	    expr = saved;
	    return false;
	case OpcSwitch:
	    return parseSwitch(expr,nested);
	case OpcFor:
	    return parseFor(expr,nested);
	case OpcWhile:
	    return parseWhile(expr,nested);
	case OpcCase:
	    if (!ParseNested::parseInner(nested,OpcCase,expr)) {
		expr.m_lineNo = saved.m_lineNo;
		return gotError("case not inside switch",saved);
	    }
	    if (skipComments(expr) != ':')
		return gotError("Expecting ':'",expr);
	    expr++;
	    break;
	case OpcDefault:
	    if (!ParseNested::parseInner(nested,OpcDefault,expr)) {
		expr.m_lineNo = saved.m_lineNo;
		return gotError("Unexpected default instruction",saved);
	    }
	    if (skipComments(expr) != ':')
		return gotError("Expecting ':'",expr);
	    expr++;
	    break;
	case OpcBreak:
	    if (!ParseNested::parseInner(nested,OpcBreak,expr)) {
		expr.m_lineNo = saved.m_lineNo;
		return gotError("Unexpected break instruction",saved);
	    }
	    if (skipComments(expr) != ';')
		return gotError("Expecting ';'",expr);
	    break;
	case OpcCont:
	    if (!ParseNested::parseInner(nested,OpcCont,expr)) {
		expr.m_lineNo = saved.m_lineNo;
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
    }
    return true;
}

class ParseLoop : public ParseNested
{
    friend class JsCode;
public:
    inline ParseLoop(JsCode* code, GenObject* nested, JsCode::JsOpcode oper, int64_t lblCont, int64_t lblBreak)
	: ParseNested(code,nested,oper),
	  m_lblCont(lblCont), m_lblBreak(lblBreak)
	{ }
protected:
    virtual bool isMatch(JsCode::JsOpcode opcode)
	{ return JsCode::OpcBreak == opcode || JsCode::OpcCont == opcode; }
private:
    int64_t m_lblCont;
    int64_t m_lblBreak;
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
    int64_t m_lblBreak;
    int64_t m_lblDefault;
    SwitchState m_state;
    ObjList m_cases;
};

// Parse keywords inner to specific instructions
bool JsCode::parseInner(ParsePoint& expr, JsOpcode opcode, ParseNested* nested)
{
    switch (*nested) {
	case OpcFor:
	case OpcWhile:
	    {
		ParseLoop* block = static_cast<ParseLoop*>(nested);
		switch (opcode) {
		    case OpcBreak:
			XDebug(this,DebugAll,"Parsing loop:break '%.30s'",(const char*)expr);
			addOpcode((Opcode)OpcJump,block->m_lblBreak);
			break;
		    case OpcCont:
			XDebug(this,DebugAll,"Parsing loop:continue '%.30s'",(const char*)expr);
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
			XDebug(this,DebugAll,"Parsing switch:case: '%.30s'",(const char*)expr);
			block->m_state = ParseSwitch::InCase;
			block->m_cases.append(popOpcode());
			addOpcode(OpcLabel,(int64_t)++m_label);
			block->m_cases.append(new ExpOperation((Opcode)OpcJumpTrue,0,m_label));
			break;
		    case OpcDefault:
			if (block->state() == ParseSwitch::InDefault)
			    return gotError("Duplicate default case",expr);
			XDebug(this,DebugAll,"Parsing switch:default: '%.30s'",(const char*)expr);
			block->m_state = ParseSwitch::InDefault;
			block->m_lblDefault = ++m_label;
			addOpcode(OpcLabel,block->m_lblDefault);
			break;
		    case OpcBreak:
			XDebug(this,DebugAll,"Parsing switch:break '%.30s'",(const char*)expr);
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

bool JsCode::parseIf(ParsePoint& expr, GenObject* nested)
{
    XDebug(this,DebugAll,"JsCode::parseIf() '%.30s'",(const char*)expr);
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    if (!runCompile(++expr,')'))
	return false;
    if (skipComments(expr) != ')')
	return gotError("Expecting ')'",expr);
    ExpOperation* cond = addOpcode((Opcode)OpcJumpFalse,(int64_t)++m_label);
    expr++;
    if (!getOneInstruction(expr,nested))
	return false;
    skipComments(expr);
    ParsePoint save = expr;
    if ((JsOpcode)ExpEvaluator::getOperator(expr,s_instr) == OpcElse) {
	ExpOperation* jump = addOpcode((Opcode)OpcJump,(int64_t)++m_label);
	addOpcode(OpcLabel,cond->number());
	if (!getOneInstruction(expr,nested))
	    return false;
	addOpcode(OpcLabel,jump->number());
    }
    else {
	expr = save;
	addOpcode(OpcLabel,cond->number());
    }
    return true;
}

bool JsCode::parseSwitch(ParsePoint& expr, GenObject* nested)
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
    const char* savedSeps = expr.m_searchedSeps;
    expr.m_searchedSeps = "";
    ExpOperation* jump = addOpcode((Opcode)OpcJump,(int64_t)++m_label);
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
    expr.m_searchedSeps = savedSeps;
    if (!expr.m_searchedSeps || expr.m_count)
	expr.m_foundSep = 0;
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

bool JsCode::parseFor(ParsePoint& expr, GenObject* nested)
{
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    addOpcode((Opcode)OpcBegin);
    if ((skipComments(++expr) != ';') && !runCompile(expr,')'))
	return false;
    int64_t cont = 0;
    int64_t jump = ++m_label;
    int64_t body = ++m_label;
    // parse initializer
    if (skipComments(expr) == ';') {
	int64_t check = body;
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

bool JsCode::parseWhile(ParsePoint& expr, GenObject* nested)
{
    if (skipComments(expr) != '(')
	return gotError("Expecting '('",expr);
    addOpcode((Opcode)OpcBegin);
    int64_t cont = ++m_label;
    addOpcode(OpcLabel,cont);
    if (!runCompile(++expr,')'))
	return false;
    if (skipComments(expr) != ')')
	return gotError("Expecting ')'",expr);
    int64_t jump = ++m_label;
    addOpcode((Opcode)OpcJumpFalse,jump);
    ParseLoop parseStack(this,nested,OpcWhile,cont,jump);
    if (!getOneInstruction(++expr,parseStack))
	return false;
    addOpcode((Opcode)OpcJump,cont);
    addOpcode(OpcLabel,jump);
    addOpcode((Opcode)OpcFlush);
    return true;
}

bool JsCode::parseVar(ParsePoint& expr)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"parseVar '%.30s'",(const char*)expr);
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

bool JsCode::parseTry(ParsePoint& expr, GenObject* nested)
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

bool JsCode::parseFuncDef(ParsePoint& expr, bool publish)
{
    XDebug(this,DebugAll,"JsCode::parseFuncDef '%.30s'",(const char*)expr);
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
    ExpOperation* jump = addOpcode((Opcode)OpcJump,(int64_t)++m_label);
    ExpOperation* lbl = addOpcode(OpcLabel,(int64_t)++m_label,true);
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
    // Add the implicit "return undefined" at end of function
    addOpcode((Opcode)OpcReturn);
    addOpcode(OpcLabel,jump->number());
    JsFunction* obj = new JsFunction(0,name,&args,(long int)lbl->number(),this);
    addOpcode(new ExpWrapper(obj,name));
    if (publish && name && obj->ref())
	m_globals.append(new ExpWrapper(obj,name));
    return true;
}

ExpEvaluator::Opcode JsCode::getOperator(ParsePoint& expr)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getOperator line=0x%X '%.30s'",lineNumber(),(const char*)expr);
    skipComments(expr);
    Opcode op = ExpEvaluator::getOperator(expr,s_operators);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getOperator(expr);
}

ExpEvaluator::Opcode JsCode::getUnaryOperator(ParsePoint& expr)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getUnaryOperator line=0x%X '%.30s'",lineNumber(),(const char*)expr);
    skipComments(expr);
    Opcode op = ExpEvaluator::getOperator(expr,s_unaryOps);
    if (OpcNone != op)
	return op;
    return ExpEvaluator::getUnaryOperator(expr);
}

ExpEvaluator::Opcode JsCode::getPostfixOperator(ParsePoint& expr, int precedence)
{
    if (inError())
	return OpcNone;
    XDebug(this,DebugAll,"JsCode::getPostfixOperator line=0x%X '%.30s'",lineNumber(),(const char*)expr);
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
    ParsePoint save = expr;
    Opcode op = ExpEvaluator::getOperator(expr,s_postfixOps);
    if (OpcNone != op) {
	if (getPrecedence(op) >= precedence)
	    return op;
	expr = save;
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
    switch ((int)oper) {
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

bool JsCode::getSeparator(ParsePoint& expr, bool remove)
{
    if (inError())
	return false;
    switch (skipComments(expr)) {
	case ';':
	    expr.m_foundSep =';';
	case ']':
	    if (remove)
		expr++;
	    return true;
    }
    return ExpEvaluator::getSeparator(expr,remove);
}

bool JsCode::getSimple(ParsePoint& expr, bool constOnly)
{
    return parseSimple(expr,constOnly);
}

bool JsCode::parseSimple(ParsePoint& expr, bool constOnly, Mutex* mtx)
{
    if (inError())
	return false;
    XDebug(this,DebugAll,"JsCode::parseSimple(%s) '%.30s'",String::boolText(constOnly),(const char*)expr);
    skipComments(expr);
    ParsePoint save = expr;
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
		return false;
	    }
	    return parseFuncDef(expr,false);
	default:
	    break;
    }
    JsObject* jso = parseArray(expr,constOnly,mtx);
    if (!jso)
	jso = parseObject(expr,constOnly,mtx);
    if (!jso)
	return ExpEvaluator::getSimple(expr,constOnly);
    addOpcode(new ExpWrapper(ExpEvaluator::OpcCopy,jso));
    return true;
}

// Parse an inline Javascript Array: [ item1, item2, ... ]
JsObject* JsCode::parseArray(ParsePoint& expr, bool constOnly, Mutex* mtx)
{
    if (skipComments(expr) != '[')
	return 0;
    expr++;
    JsArray* jsa = new JsArray(mtx,"[object Array]");
    for (bool first = true; ; first = false) {
	if (skipComments(expr) == ']') {
	    expr++;
	    break;
	}
	if (first) {
	    if (*expr == ',') {
		ParsePoint next = expr;
		next++;
		// A construct like [,] creates an empty Array
		if (skipComments(next) == ']') {
		    next++;
		    expr = next;
		    break;
		}
	    }
	}
	else {
	    if (*expr != ',') {
		TelEngine::destruct(jsa);
		break;
	    }
	    expr++;
	}
	// Swallow the single comma allowed after last item
	if (skipComments(expr) == ']') {
	    expr++;
	    break;
	}
	// Successive commas insert an undefined between them
	if (skipComments(expr) == ',') {
	    jsa->push(new ExpWrapper(0,"undefined"));
	    continue;
	}
	bool ok = constOnly ? parseSimple(expr,true,mtx) : getOperand(expr,false);
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
JsObject* JsCode::parseObject(ParsePoint& expr, bool constOnly, Mutex* mtx)
{
    if (skipComments(expr) != '{')
	return 0;
    expr++;
    JsObject* jso = new JsObject(mtx,"[object Object]");
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
	    // A single comma is allowed after last property
	    if (skipComments(expr) == '}') {
		expr++;
		break;
	    }
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
	bool ok = constOnly ? parseSimple(expr,true,mtx) : getOperand(expr,false);
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
    JsRunner* sr = static_cast<JsRunner*>(context);
    if (sr && sr->tracing())
	sr->tracePrep(oper);
    switch ((int)oper.opcode()) {
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
		    // try to obtain an object on which to run the field
		    ScriptContext* ctx = YOBJECT(ScriptContext,op1);
		    if (ctx && ctx->runField(stack,*op2,context)) {
			TelEngine::destruct(op1);
			TelEngine::destruct(op2);
			break;
		    }
		    else {
			// op1 is not an object, it's a string
			JsContext* jsCtx = 0;
			if (sr)
			    jsCtx = static_cast<JsContext*>(sr->context());
			if (jsCtx && jsCtx->runStringField(op1,op2->name(),stack,*op2,context)) {
			    TelEngine::destruct(op1);
			    TelEngine::destruct(op2);
			    break;
			}
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
		pushOne(stack,new ExpOperation(op->typeOf()));
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
				op = 0;
				break;
			    }
			}
			// fall through
		    default:
			TelEngine::destruct(op);
			return gotError("Expecting class name",oper.lineNumber());
		}
		if (!op)
		    break;
		ExpFunction ctr(op->name(),(long int)op->number());
		ctr.lineNumber(oper.lineNumber());
		TelEngine::destruct(op);
		if (!runOperation(stack,ctr,context))
		    return false;
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
		ExpOperation* op = oper.valInteger() ? popValue(stack,context) : new ExpWrapper(0,"undefined");
		ExpOperation* thisObj = 0;
		bool ok = false;
		while (ExpOperation* drop = popAny(stack)) {
		    ok = drop->barrier() && (drop->opcode() == OpcFunc);
		    long int lbl = (long int)drop->number();
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
		if (sr && sr->tracing())
		    sr->traceReturn();
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
		    ExpOperation assign(OpcAssign);
		    assign.lineNumber(oper.lineNumber());
		    pushOne(stack,iter->field().clone());
		    if (iter->name())
			pushOne(stack,new ExpOperation(OpcField,iter->name() + "." + *n));
		    else
			pushOne(stack,new ExpOperation(*n));
		    TelEngine::destruct(n);
		    ok = runOperation(stack,assign,context);
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
			val = !val;
		    default:
			break;
		}
		if (val)
		    break;
	    }
	    // fall through
	case OpcJump:
	case OpcJRel:
	    switch ((JsOpcode)oper.opcode()) {
		case OpcJump:
		case OpcJumpTrue:
		case OpcJumpFalse:
		    if (!jumpToLabel((long int)oper.number(),context))
			return gotError("Label not found",oper.lineNumber());
		    break;
		case OpcJRel:
		case OpcJRelTrue:
		case OpcJRelFalse:
		    if (!jumpRelative((long int)oper.number(),context))
			return gotError("Relative jump failed",oper.lineNumber());
		    break;
		default:
		    return gotError("Internal error",oper.lineNumber());
	    }
	    break;
	case OpcDelete:
	    {
		ExpOperation* op = popOne(stack);
		if (!op)
		    return gotError("Stack underflow",oper.lineNumber());
		if (op->opcode() != OpcField) {
		    pushOne(stack,new ExpOperation(true));
		    TelEngine::destruct(op);
		    break;
		}
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
	    }
	    break;
	case OpcCopy:
	    if (!ExpEvaluator::runOperation(stack,oper,context))
		return false;
	    resolveObjectParams(YOBJECT(JsObject,stack.get()),stack,context);
	    break;
	default:
	    if (!ExpEvaluator::runOperation(stack,oper,context))
		return false;
    }
    if (sr && sr->tracing())
	sr->tracePost(oper);
    return true;
}

void JsCode::resolveObjectParams(JsObject* object, ObjList& stack, GenObject* context, JsContext* ctxt, JsObject* objProto, JsArray* arrayProto) const
{
    DDebug(this,DebugAll,"JsCode::resolveObjectParams(%p,%p,%p,%p,%p,%p)",
	object,&stack,context,ctxt,objProto,arrayProto);
    for (unsigned int i = 0;i < object->params().length();i++) {
	String* param = object->params().getParam(i);
	JsObject* tmpObj = YOBJECT(JsObject,param);
	if (tmpObj) {
	    resolveObjectParams(tmpObj,stack,context,ctxt,objProto,arrayProto);
	    continue;
	}
	ExpOperation* op = YOBJECT(ExpOperation,param);
	if (!op || op->opcode() != OpcField)
	    continue;
	String name = *op;
	JsObject* jsobj = YOBJECT(JsObject,ctxt->resolve(stack,name,context));
	if (!jsobj) {
	    object->params().setParam(new ExpWrapper(0,op->name()));
	    continue;
	}
	NamedString* ns = jsobj->getField(stack,name,context);
	if (!ns) {
	    object->params().setParam(new ExpWrapper(0,op->name()));
	    continue;
	}
	ExpOperation* objOper = YOBJECT(ExpOperation,ns);
	NamedString* temp = 0;
	if (objOper)
	    temp = objOper->clone(op->name());
	else
	    temp = new NamedString(op->name(),*ns);
	object->params().setParam(temp);
    }
    if (object->frozen() || object->params().getParam(JsObject::protoName()))
	return;
    JsArray* arr = YOBJECT(JsArray,object);
    if (arr) {
	if (arrayProto && arrayProto->ref())
	    object->params().addParam(new ExpWrapper(arrayProto,JsObject::protoName()));
    }
    else if (objProto && objProto->ref())
	object->params().addParam(new ExpWrapper(objProto,JsObject::protoName()));
}

void JsCode::resolveObjectParams(JsObject* object, ObjList& stack, GenObject* context) const
{
    if (!(object && context))
	return;
    ScriptRun* sr = static_cast<ScriptRun*>(context);
    JsContext* ctx = YOBJECT(JsContext,sr->context());
    if (!ctx)
	return;
    JsObject* objProto = 0;
    JsFunction* objCtr = YOBJECT(JsFunction,ctx->params().getParam(YSTRING("Object")));
    if (objCtr)
	objProto = YOBJECT(JsObject,objCtr->params().getParam(YSTRING("prototype")));

    JsArray* arrayProto = 0;
    objCtr = YOBJECT(JsFunction,ctx->params().getParam(YSTRING("Array")));
    if (objCtr)
	arrayProto = YOBJECT(JsArray,objCtr->params().getParam(YSTRING("prototype")));

    resolveObjectParams(object,stack,context,ctx,objProto,arrayProto);
}

bool JsCode::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context) const
{
    DDebug(this,DebugAll,"JsCode::runFunction(%p,'%s' " FMT64 ", %p) ext=%p",
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
	if (m_entries) {
	    for (const JsEntry* e = m_entries; e->number >= 0; e++) {
		if (e->number == label) {
		    runner->m_index = e->index;
		    XDebug(this,DebugInfo,"Fast jumped to index %u",e->index);
		    return true;
		}
	    }
	}
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
    XDebug(this,DebugInfo,"JsCode::callFunction(%p," FMT64 ",%p) in %s'%s' this=%p",
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
    if (!jumpToLabel(func->label(),context))
	return false;
    JsRunner* jsr = static_cast<JsRunner*>(context);
    if (jsr && jsr->tracing())
	jsr->traceCall(oper,*func);
    return true;
}

ScriptRun* JsCode::createRunner(ScriptContext* context, const char* title)
{
    if (!context)
	return 0;
    return new JsRunner(this,context,title);
}

bool JsCode::null() const
{
    return m_linked.null() && !m_opcodes.skipNull();
}

void JsCode::dump(String& res, bool lineNo) const
{
    if (m_linked.null())
	return ExpEvaluator::dump(res,lineNo);
    for (unsigned int i = 0; i < m_linked.length(); i++) {
	const ExpOperation* o = static_cast<const ExpOperation*>(m_linked[i]);
	if (!o)
	    continue;
	if (res)
	    res << " ";
	ExpEvaluator::dump(*o,res,lineNo);
    }
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
	JsContext* ctx = YOBJECT(JsContext,context());
	if (ctx)
	    func = YOBJECT(JsFunction,ctx->getField(stack(),name,this));
    }
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

void JsRunner::traceStart(const char* title, const char* file)
{
    if (m_tracing)
	return;
    m_tracing = true;
    if (TelEngine::null(file) || !code())
	return;
    Debug(DebugInfo,"Preparing Javascript trace file '%s'",file);
    JsCodeStats* stats = new JsCodeStats(static_cast<JsCode*>(code()),file);
    traceStart(title,stats);
    TelEngine::destruct(stats);
}

void JsRunner::traceStart(const char* title, JsCodeStats* stats)
{
    m_stats = stats;
    if (m_stats) {
	m_tracing = true;
	if (!m_callInfo) {
	    if (TelEngine::null(title))
		title = "[main flow]";
	    m_stats->lock();
	    JsFuncStats* fs = m_stats->getFuncStats(title,0);
	    m_stats->unlock();
	    m_traceStack.insert(m_callInfo = new JsCallInfo(fs,title,0,0,0,0));
	}
    }
}

void JsRunner::traceDump()
{
    if (!m_stats)
	Debug(DebugNote,"Executed %u operations in " FMT64U " usec",m_instr,m_totalTime);
}

void JsRunner::traceCheck(const char* title)
{
    if (!(code()))
	return;
    JsCode* c = static_cast<JsCode*>(code());
    if (!c->traceable())
	return;
    static const String s_tracingPragma = "trace";
    const NamedString* ns = c->pragmas().getParam(s_tracingPragma);
    if (!(ns && ns->toBoolean(true)))
	return;
    if (ns->toBoolean(false) || !context()) {
	traceStart(title);
	return;
    }
    static const String s_tracingObj = "__trace__";
    NamedString* obj = context()->params().getParam(s_tracingObj);
    ExpWrapper* w = YOBJECT(ExpWrapper,obj);
    if (w) {
	JsCodeStats* stats = YOBJECT(JsCodeStats,w->object());
	if (stats) {
	    DDebug(DebugInfo,"Using shared trace file '%s'",stats->toString().c_str());
	    traceStart(title,stats);
	}
	return;
    }
    else if (obj) {
	traceStart(title);
	return;
    }
    traceStart(title,ns->c_str());
    if (m_stats) {
	m_stats->ref();
	context()->params().setParam(new ExpWrapper(m_stats,s_tracingObj));
    }
}

void JsRunner::tracePrep(const ExpOperation& oper)
{
    if (!m_lastTime)
	m_lastTime = Time::now();
    m_lastLine = oper.lineNumber();
    m_instr++;
}

void JsRunner::tracePost(const ExpOperation& oper)
{
    u_int64_t time = Time::now();
    u_int64_t diff = 0;
    if (m_lastTime) {
	diff = time - m_lastTime;
	m_totalTime += diff;
    }
    m_lastTime = m_paused ? 0 : time;

    if (diff && m_callInfo && m_stats) {
#ifdef STATS_TRACE
	String line;
	static_cast<const JsCode*>(code())->formatLineNo(line,m_lastLine);
	Debug(STATS_TRACE,DebugNote,"Operation %u %s @ %s %s took " FMT64U " usec",
	    oper.opcode(),static_cast<const JsCode*>(code())->getOperator(oper.opcode()),
	    line.c_str(),m_callInfo->c_str(),diff);
#endif
	m_stats->lock();
	m_callInfo->traceLine(m_lastLine,diff);
	m_stats->unlock();
    }
}

void JsRunner::traceCall(const ExpOperation& oper, const JsFunction& func)
{
    const ExpOperation* o = 0;
    if (m_opcode)
	o = static_cast<const ExpOperation*>(m_opcode->get());
    if (!o)
	o = static_cast<const ExpOperation*>(static_cast<const JsCode*>(code())->m_linked[m_index]);
    if (!o) {
	String str;
	static_cast<const JsCode*>(code())->formatLineNo(str,m_lastLine);
	Debug(DebugWarn,"Current operation unavailable in %s [%p]",str.c_str(),this);
	return;
    }

    const String& name = func.getFunc()->name();
    JsFuncStats* fs = 0;
    if (m_stats) {
	m_stats->lock();
	if (m_lastTime) {
	    u_int64_t diff = Time::now() - m_lastTime;
	    m_totalTime += diff;
	    m_lastTime = 0;
	    if (m_callInfo)
		m_callInfo->traceLine(m_lastLine,diff);
	}
	fs = m_stats->getFuncStats(name,o->lineNumber());
	m_stats->unlock();
    }
#ifdef STATS_TRACE
    String caller,called;
    static_cast<const JsCode*>(code())->formatLineNo(caller,m_lastLine);
    static_cast<const JsCode*>(code())->formatLineNo(called,o->lineNumber());
    Debug(STATS_TRACE,DebugCall,"Call %s %s -> %s, instr=%u",
	name.c_str(),caller.c_str(),called.c_str(),m_instr);
#endif
    m_traceStack.insert(m_callInfo = new JsCallInfo(fs,name,m_lastLine,o->lineNumber(),m_instr,m_totalTime));
}

void JsRunner::traceReturn()
{
    JsCallInfo* info = static_cast<JsCallInfo*>(m_traceStack.remove(false));
    if (!info) {
	String str;
	static_cast<const JsCode*>(code())->formatLineNo(str,m_lastLine);
	Debug(DebugWarn,"Stats stack underflow in %s [%p]",str.c_str(),this);
	return;
    }
    m_callInfo = static_cast<JsCallInfo*>(m_traceStack.get());

    unsigned int instr = m_instr - info->startInstr;
    u_int64_t time = Time::now();
    u_int64_t timeInstr = 0;
    if (m_lastTime) {
	timeInstr = time - m_lastTime;
	m_totalTime += timeInstr;
	m_lastTime = 0;
    }
    u_int64_t timeCall = m_totalTime - info->startTime;

    if (m_stats && timeInstr) {
	m_stats->lock();
	info->traceLine(m_lastLine,timeInstr);
	m_stats->unlock();
    }
    if (m_callInfo && m_stats) {
#ifdef STATS_TRACE
	String caller,called;
	static_cast<const JsCode*>(code())->formatLineNo(caller,info->callerLine);
	static_cast<const JsCode*>(code())->formatLineNo(called,info->calledLine);
	Debug(STATS_TRACE,DebugCall,"Ret %s %s -> %s, %u oper / " FMT64U " usec",
	    info->c_str(),caller.c_str(),called.c_str(),instr,timeCall);
#endif
	m_stats->lock();
	m_callInfo->traceCall(info,instr,timeCall);
	m_stats->unlock();
    }
    else {
	String caller,called;
	static_cast<const JsCode*>(code())->formatLineNo(caller,info->callerLine);
	static_cast<const JsCode*>(code())->formatLineNo(called,info->calledLine);
	Debug(DebugNote,"Function '%s' %s -> %s took %u operations / " FMT64U " usec",
	    info->c_str(),caller.c_str(),called.c_str(),instr,timeCall);
    }
    TelEngine::destruct(info);
}


void JsFuncStats::updateLine(unsigned int lineNo, u_int64_t usec)
{
    if (!lineNumber)
	lineNumber = lineNo;
#ifdef STATS_TRACE
    Debug(STATS_TRACE,DebugAll,"Updating %u:%u in %s [%u:%u] with " FMT64U " usec",
	JsCode::getFileNo(lineNo),JsCode::getLineNo(lineNo),c_str(),
	JsCode::getFileNo(lineNumber),JsCode::getLineNo(lineNumber),usec);
#endif
    ObjList* l = &funcLines;
    while (l) {
	JsLineStats* s = static_cast<JsLineStats*>(l->get());
	if (s) {
	    if (s->lineNumber == lineNo && !s->isCall) {
		s->operations++;
		s->microseconds += usec;
		return;
	    }
	    if (s->lineNumber > lineNo)
		break;
	}
	ObjList* ln = l->next();
	if (ln) {
	    l = ln;
	    continue;
	}
	l->append(new JsLineStats(lineNo,1,usec));
	return;
    }
    l->insert(new JsLineStats(lineNo,1,usec));
}

void JsFuncStats::updateCall(const char* name, unsigned int caller, unsigned int called, unsigned int instr, u_int64_t usec)
{
    ObjList* l = &funcLines;
    while (l) {
	JsLineStats* s = static_cast<JsLineStats*>(l->get());
	if (s) {
	    if (s->lineNumber == caller && s->isCall) {
		JsCallStats* cs = static_cast<JsCallStats*>(s);
		if (cs->calledLine == called) {
		    cs->operations += instr;
		    cs->microseconds += usec;
		    cs->callsCount++;
		    return;
		}
	    }
	    if (s->lineNumber > caller)
		break;
	}
	ObjList* ln = l->next();
	if (ln) {
	    l = ln;
	    continue;
	}
#ifndef TRACE_RAW_NAME
	String tmp(name);
	if (called)
	    tmp << " [" << JsCode::getFileNo(called) << ":" << JsCode::getLineNo(called) << "]";
	name = tmp;
#endif
	l->append(new JsCallStats(name,caller,called,instr,usec));
	return;
    }
#ifndef TRACE_RAW_NAME
    String tmp(name);
    if (called)
	tmp << " [" << JsCode::getFileNo(called) << ":" << JsCode::getLineNo(called) << "]";
    name = tmp;
#endif
    l->insert(new JsCallStats(name,caller,called,instr,usec));
}


JsCodeStats::JsCodeStats(JsCode* code, const char* file)
    : Mutex(false,"JsCodeStats"),
      m_fileName(file)
{
    m_code = code;
    if (!code)
	return;
}

JsCodeStats::~JsCodeStats()
{
    dump();
}

JsFuncStats* JsCodeStats::getFuncStats(const char* name, unsigned int lineNo)
{
    String tmp(name);
#ifndef TRACE_RAW_NAME
    if (lineNo)
	tmp << " [" << JsCode::getFileNo(lineNo) << ":" << JsCode::getLineNo(lineNo) << "]";
#endif
    ObjList* l = &m_funcStats;
    while (l) {
	JsFuncStats* s = static_cast<JsFuncStats*>(l->get());
	if (s) {
	    if (s->lineNumber == lineNo && tmp == *s)
		return s;
	    if (s->lineNumber > lineNo)
		break;
	}
	ObjList* ln = l->next();
	if (ln) {
	    l = ln;
	    continue;
	}
	s = new JsFuncStats(tmp,lineNo);
	l->append(s);
	return s;
    }
    JsFuncStats* s = new JsFuncStats(tmp,lineNo);
    l->insert(s);
    return s;
}

void JsCodeStats::dump()
{
    dump(m_fileName);
    m_fileName.clear();
}

void JsCodeStats::dump(const char* file)
{
    File f;
    if (file && m_code && f.openPath(file,true,false,true)) {
	Debug(DebugInfo,"Writing trace file '%s'",file);
	dump(f);
    }
}

void JsCodeStats::dump(Stream& file)
{
    if (!m_code)
	return;
    String fl,fn,cfn,cfl;
    NamedList lMap(""), nMap("");
    int ifl = 1, ifn = 1;
    file.writeData("events: Operations Microseconds\n");
    for (ObjList* f = m_funcStats.skipNull(); f; f = f->skipNext()) {
	String str("\n");
	const JsFuncStats* fs = static_cast<const JsFuncStats*>(f->get());
	String tmp = m_code->getFileName(fs->lineNumber);
	if (fl != tmp) {
	    fl = tmp;
	    tmp = lMap[fl];
	    if (tmp.null()) {
		tmp << "(" << ifl++ << ")";
		lMap.addParam(fl,tmp);
		tmp << " " << fl;
	    }
	    str << "fl=" << tmp << "\n";
	}
	if (fn != *fs) {
	    fn = *fs;
	    tmp = nMap[fn];
	    if (tmp.null()) {
		tmp << "(" << ifn++ << ")";
		nMap.addParam(fn,tmp);
		tmp << " " << fn;
	    }
	    str << "fn=" << tmp << "\n";
	}
	for (ObjList* l = fs->funcLines.skipNull(); l; l = l->skipNext()) {
	    const JsLineStats* ls = static_cast<const JsLineStats*>(l->get());
	    tmp = m_code->getFileName(ls->lineNumber);
	    if (fl != tmp) {
		fl = tmp;
		tmp = lMap[fl];
		if (tmp.null()) {
		    tmp << "(" << ifl++ << ")";
		    lMap.addParam(fl,tmp);
		    tmp << " " << fl;
		}
		str << "fl=" << tmp << "\n";
	    }
	    if (ls->isCall) {
		const JsCallStats* cs = static_cast<const JsCallStats*>(ls);
		tmp = m_code->getFileName(cs->calledLine);
		if (cfl != tmp) {
		    cfl = tmp;
		    tmp = lMap[cfl];
		    if (tmp.null()) {
			tmp << "(" << ifl++ << ")";
			lMap.addParam(cfl,tmp);
			tmp << " " << cfl;
		    }
		    str << "cfl=" << tmp << "\n";
		}
		if (cfn != cs->funcName) {
		    cfn = cs->funcName;
		    tmp = nMap[cfn];
		    if (tmp.null()) {
			tmp << "(" << ifn++ << ")";
			nMap.addParam(cfn,tmp);
			tmp << " " << cfn;
		    }
		    str << "cfn=" << tmp << "\n";
		}
		str << "calls=" << cs->callsCount << " "
		    << JsCode::getLineNo(cs->calledLine) << "\n";
	    }
	    str << JsCode::getLineNo(ls->lineNumber) << " " << ls->operations << " "
		<< ls->microseconds << "\n";
	}
	file.writeData(str);
    }
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
	// func.apply(new_this)
	// func.apply(new_this,["array","of","params",...])
	switch (oper.number()) {
	    case 1:
	    case 2:
		break;
	    default:
		return false;
	}
	ObjList args;
	extractArgs(this,stack,oper,context,args);
	JsObject* thisObj = YOBJECT(JsObject,args[0]);
	JsArray* callArgs = YOBJECT(JsArray,args[1]);
	int argc = 0;
	if (callArgs) {
	    int32_t len = callArgs->length();
	    for (int32_t i = 0; i < len; i++)
		if (callArgs->runField(stack,ExpOperation((int64_t)i,String(i)),context))
		    argc++;
	}
	ExpFunction func(toString(),argc);
	return runDefined(stack,func,context,thisObj);
    }
    else if (oper.name() == YSTRING("call")) {
	// func.call(new_this)
	// func.call(new_this,param1,param2,...)
	if (!oper.number())
	    return false;
	ObjList args;
	extractArgs(this,stack,oper,context,args);
	JsObject* thisObj = YOBJECT(JsObject,args[0]);
	int argc = 0;
	ObjList* l = args.next();
	if (l) {
	    while (ExpOperation* op = static_cast<ExpOperation*>(l->remove(false))) {
		ExpEvaluator::pushOne(stack,op);
		argc++;
	    }
	}
	ExpFunction func(toString(),argc);
	return runDefined(stack,func,context,thisObj);
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
void JsParser::adjustPath(String& script, bool extraInc) const
{
    if (script.null() || script.startsWith(Engine::pathSeparator()))
	return;
    if (extraInc && m_includePath && File::exists(m_includePath + script))
	script = m_includePath + script;
    else
	script = m_basePath + script;
}

// Create Javascript context
ScriptContext* JsParser::createContext() const
{
    return new JsContext;
}

ScriptRun* JsParser::createRunner(ScriptCode* code, ScriptContext* context, const char* title) const
{
    if (!code)
	return 0;
    ScriptContext* ctxt = 0;
    if (!context)
	context = ctxt = createContext();
    ScriptRun* runner = new JsRunner(code,context,title);
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
bool JsParser::parse(const char* text, bool fragment, const char* file, int len)
{
    if (TelEngine::null(text))
	return false;
    String::stripBOM(text);
    JsCode* jsc = static_cast<JsCode*>(code());
    ParsePoint expr(text,0,(jsc ? jsc->lineNumber() : 0),file);
    if (fragment)
	return jsc && jsc->compile(expr,this);
    m_parsedFile.clear();
    jsc = new JsCode;
    setCode(jsc);
    jsc->deref();
    expr.m_eval = jsc;
    if (!TelEngine::null(file)) {
	jsc->setBaseFile(file);
	expr.m_fileName = file;
	expr.m_lineNo = jsc->lineNumber();
    }
    if (!jsc->compile(expr,this)) {
	setCode(0);
	return false;
    }
    m_parsedFile = file;
    DDebug(DebugAll,"Compiled: %s",jsc->ExpEvaluator::dump().c_str());
    jsc->simplify();
    DDebug(DebugAll,"Simplified: %s",jsc->ExpEvaluator::dump().c_str());
    if (m_allowLink) {
	jsc->link();
#ifdef DEBUG
#ifdef XDEBUG
	Debug(DebugAll,"Linked: %s",jsc->ExpEvaluator::dump(true).c_str());
#else
	Debug(DebugAll,"Linked: %s",jsc->ExpEvaluator::dump(false).c_str());
#endif
#endif
    }
    jsc->trace(m_allowTrace);
    return true;
}

// Check if the script, path or any included files have changed
bool JsParser::scriptChanged(const char* file) const
{
    if (TelEngine::null(file))
	return true;
    const JsCode* c = static_cast<const JsCode*>(code());
    if (!c)
	return true;
    String tmp(file);
    adjustPath(tmp);
    return (parsedFile() != tmp) || c->scriptChanged();
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
ExpOperation* JsParser::parseJSON(const char* text, Mutex* mtx, ObjList* stack, GenObject* context)
{
    if (!text)
	return 0;
    ExpOperation* ret = 0;
    JsCode* code = new JsCode;
    ParsePoint pp(text,code);
    if (code->parseSimple(pp,true,mtx))
	ret = code->popOpcode();
    if (stack)
	code->resolveObjectParams(YOBJECT(JsObject,ret),*stack,context);
    TelEngine::destruct(code);
    return ret;
}

// Return a "null" object wrapper
ExpOperation* JsParser::nullClone(const char* name)
{
    return TelEngine::null(name) ? s_null.ExpOperation::clone() : s_null.clone(name);
}

// Return the "null" object
JsObject* JsParser::nullObject()
{
    JsObject* n = YOBJECT(JsObject,s_null.object());
    return (n && n->ref()) ? n : 0;
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
