/**
 * cdrcombine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Combined CDR builder
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2013-2014 Null Team
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

#include <yatengine.h>

using namespace TelEngine;
namespace { // anonymous

#define PLUGIN "cdrcombine"

class CdrCombinePlugin : public Plugin
{
public:
    CdrCombinePlugin();
    virtual ~CdrCombinePlugin();
    virtual void initialize();
private:
    bool m_first;
};

INIT_PLUGIN(CdrCombinePlugin);

class CdrHandler : public MessageHandler
{
public:
    CdrHandler()
	: MessageHandler("call.cdr",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler()
	: MessageHandler("engine.status",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class CommandHandler : public MessageHandler
{
public:
    CommandHandler()
	: MessageHandler("engine.command",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class CdrParams : public NamedList
{
public:
    explicit inline CdrParams(const char* name)
	: NamedList(name), m_inuse(true)
	{ }
    inline bool inUse() const
	{ return m_inuse; }
    inline void finalize()
	{ m_inuse = false; }
    void setParams(const NamedList& src, bool outgoing);
private:
    static bool isForbidden(const String& name, const String* list);
    bool m_inuse;
};

class CdrCombiner : public CdrParams
{
public:
    explicit inline CdrCombiner(const char* billid)
	: CdrParams(billid)
	{ }
    void updateInit(const NamedList& params, const String& chan);
    bool updateFini(const NamedList& params, const String& chan);
    void getStatus(String& str) const;
private:
    CdrParams* updateParams(const NamedList& params, const String& chan);
    void emitMessage() const;
    ObjList m_out;
};

// List of CDRs in progress
static ObjList s_cdrs;

// This mutex protects the CDR list
static Mutex s_mutex(false,"CdrCombine");

// Non-copiable parameters for all call legs
static const String s_forbidden[] = {
    "operation",
    "direction",
    "billid",
    "cdrtrack",
    "cdrcreate",
    "cdrid",
    ""
};

// Extra non-copiable parameters for outgoing call legs
static const String s_forbidden2[] = {
    "nodename",
    "nodeprefix",
    "cdrwrite",
    "runid",
    ""
};


void CdrParams::setParams(const NamedList& src, bool outgoing)
{
    DDebug(PLUGIN,DebugAll,"Setting params of %s leg '%s'",(outgoing ? "outgoing" : "incoming"),c_str());
    NamedIterator iter(src);
    while (const NamedString* p = iter.get()) {
	if (p->name() == Engine::trackParam())
	    continue;
	if (isForbidden(p->name(),s_forbidden))
	    continue;
	if (outgoing && isForbidden(p->name(),s_forbidden2))
	    continue;
	setParam(p->name(),*p);
    }
}

bool CdrParams::isForbidden(const String& name, const String* list)
{
    for (; !TelEngine::null(list); list++) {
	if (name == *list)
	    return true;
    }
    return false;
}


CdrParams* CdrCombiner::updateParams(const NamedList& params, const String& chan)
{
    const String* ch = getParam(YSTRING("chan"));
    if (ch) {
	if (chan == *ch) {
	    setParams(params,false);
	    return this;
	}
    }
    else if (params[YSTRING("direction")] == YSTRING("incoming")) {
	setParams(params,false);
	return this;
    }
    CdrParams* c = static_cast<CdrParams*>(m_out[chan]);
    if (!c) {
	DDebug(PLUGIN,DebugAll,"Creating CdrParams for '%s' in '%s'",chan.c_str(),c_str());
	c = new CdrParams(chan);
	m_out.insert(c);
    }
    c->setParams(params,true);
    return c;
}

void CdrCombiner::updateInit(const NamedList& params, const String& chan)
{
    updateParams(params,chan);
}

bool CdrCombiner::updateFini(const NamedList& params, const String& chan)
{
    updateParams(params,chan)->finalize();

    // check if this CDR is finalized or not
    if (inUse())
	return false;
    for (ObjList* l = m_out.skipNull(); l; l = l->skipNext()) {
	if (static_cast<const CdrParams*>(l->get())->inUse())
	    return false;
    }
    // all legs are no longer in use - emit message and be destroyed
    emitMessage();
    return true;
}

void CdrCombiner::emitMessage() const
{
    Message* m = new Message("call.cdr",0,true);
    m->addParam("operation","combined");
    m->addParam("billid",c_str());
    m->copyParams(*this);
    int count = 0;
    for (ObjList* l = m_out.skipNull(); l; l = l->skipNext(), count++) {
	String prefix("out_leg");
	if (count)
	    prefix << "." << count;
	prefix << ".";
	NamedIterator iter(*static_cast<const CdrParams*>(l->get()));
	while (const NamedString* p = iter.get())
	    m->addParam(prefix + p->name(),*p);
    }
    Engine::enqueue(m);
}

void CdrCombiner::getStatus(String& str) const
{
    str << c_str()
	<< "=" << getValue(YSTRING("chan")) << "|" << getValue(YSTRING("caller"))
	<< "|" << getValue(YSTRING("called")) << "|" << getValue(YSTRING("address"))
	<< "|" << m_out.count();
}


bool CdrHandler::received(Message &msg)
{
    const String* op = msg.getParam(YSTRING("operation"));
    if (TelEngine::null(op))
	return false;
    bool init = false;
    if (*op == YSTRING("initialize"))
	init = true;
    else if (*op != YSTRING("finalize"))
	return false;

    const String& billid = msg[YSTRING("billid")];
    if (billid.null())
	return false;
    const String& chan = msg[YSTRING("chan")];
    if (chan.null())
	return false;

    s_mutex.lock();
    if (init) {
	CdrCombiner* c = static_cast<CdrCombiner*>(s_cdrs[billid]);
	if (!c) {
	    DDebug(PLUGIN,DebugInfo,"Creating CdrCombiner for '%s'",billid.c_str());
	    c = new CdrCombiner(billid);
	    s_cdrs.append(c);
	}
	c->updateInit(msg,chan);
    }
    else {
	CdrCombiner* c = static_cast<CdrCombiner*>(s_cdrs[billid]);
	if (c) {
	    if (c->updateFini(msg,chan)) {
		DDebug(PLUGIN,DebugInfo,"Removing CdrCombiner for '%s'",billid.c_str());
		s_cdrs.remove(c);
	    }
	}
	else
	    Debug(chan.c_str(),DebugWarn,"CDR finalize without combiner for '%s'",billid.c_str());
    }
    s_mutex.unlock();
    return false;
}


bool StatusHandler::received(Message &msg)
{
    const String* sel = msg.getParam(YSTRING("module"));
    if (!(TelEngine::null(sel) || (*sel == YSTRING("cdrcombine"))))
	return false;
    String st("name=cdrcombine,type=cdr,format=ChanId|Caller|Called|Address|OutLegs");
    s_mutex.lock();
    st << ";cdrs=" << s_cdrs.count();
    if (msg.getBoolValue(YSTRING("details"),true)) {
	st << ";";
	bool first = true;
	for (ObjList* l = s_cdrs.skipNull(); l; l = l->skipNext()) {
	    const CdrCombiner* c = static_cast<const CdrCombiner*>(l->get());
	    if (first)
		first = false;
	    else
		st << ",";
	    c->getStatus(st);
	}
    }
    s_mutex.unlock();
    msg.retValue() << st << "\r\n";
    return false;
}


bool CommandHandler::received(Message &msg)
{
    static const String name("cdrcombine");
    const String* partial = msg.getParam(YSTRING("partline"));
    if (!partial || *partial != YSTRING("status"))
	return false;
    partial = msg.getParam(YSTRING("partword"));
    if (TelEngine::null(partial) || name.startsWith(*partial))
	msg.retValue().append(name,"\t");
    return false;
}


CdrCombinePlugin::CdrCombinePlugin()
    : Plugin("cdrcombine"),
      m_first(true)
{
    Output("Loaded module CdrCombine");
}

CdrCombinePlugin::~CdrCombinePlugin()
{
    Output("Unloading module CdrCombine");
}

void CdrCombinePlugin::initialize()
{
    Output("Initializing module CdrCombine");
    if (m_first) {
	m_first = false;
	Engine::install(new CdrHandler);
	Engine::install(new StatusHandler);
	Engine::install(new CommandHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
