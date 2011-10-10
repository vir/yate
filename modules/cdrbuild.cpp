/**
 * cdrbuild.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Cdr builder
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

#include <yatengine.h>

#include <string.h>
#include <stdio.h>

using namespace TelEngine;
namespace { // anonymous

enum {
    CdrStart,
    CdrCall,
    CdrRoute,
    CdrProgress,
    CdrRinging,
    CdrAnswer,
    CdrUpdate,
    CdrHangup,
    CdrDrop,
    EngHalt
};

class CdrHandler : public MessageHandler
{
public:
    CdrHandler(const char *name, int type, int prio = 50)
	: MessageHandler(name,prio), m_type(type) { }
    virtual bool received(Message &msg);
private:
    int m_type;
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("engine.status") { }
    virtual bool received(Message &msg);
};

class CommandHandler : public MessageHandler
{
public:
    CommandHandler() : MessageHandler("engine.command") { }
    virtual bool received(Message &msg);
};

// Collects CDR information and emits the call.cdr messages when needed
class CdrBuilder : public NamedList
{
public:
    CdrBuilder(const char *name);
    virtual ~CdrBuilder();
    void update(int type, u_int64_t val, const char* status = 0);
    bool update(const Message& msg, int type, u_int64_t val);
    void emit(const char *operation = 0);
    String getStatus() const;
    static CdrBuilder* find(String &id);
private:
    u_int64_t
	m_start,
	m_call,
	m_ringing,
	m_answer,
	m_hangup;
    String m_dir;
    String m_status;
    String m_cdrId;
    bool m_first;
    bool m_write;
};

// CDR extra parameter name with an overwrite flag
class Param : public String
{
public:
    inline Param(const char* name, bool replace)
	: String(name), m_overwrite(replace)
	{ }
    inline bool overwrite() const
	{ return m_overwrite; }
    inline void overwrite(bool replace)
	{ m_overwrite = replace; }
private:
    bool m_overwrite;
};

// Temporarily keep the ID of hungup channels to prevent race issues
class Hungup : public String
{
public:
    inline Hungup(const String& id, bool emitHangup)
	: String(id),
	  m_hangup(emitHangup), m_expires(Time::now() + s_exp)
	{ DDebug("cdrbuild",DebugInfo,"Hungup '%s'",id.c_str()); }
    inline u_int64_t expires() const
	{ return m_expires; }
    inline bool hangup()
	{ return m_hangup && !(m_hangup = false); }
    static u_int64_t s_exp;
private:
    bool m_hangup;
    u_int64_t m_expires;
};


static ObjList s_cdrs;
static ObjList s_hungup;
u_int64_t Hungup::s_exp = 5000000;

// This mutex protects both the CDR list and the params list
static Mutex s_mutex(false,"CdrBuild");
static ObjList s_params;
static int s_res = 1;
static int s_seq = 0;
static String s_runId;

// Time resolutions
static TokenDict const s_timeRes[] = {
    { "sec",  0 },
    { "msec", 1 },
    { "usec", 2 },
    { 0, 0 },
};

// Default but overridable parameters
static struct _params {
    const char* name;
    bool overwrite;
} const s_defParams[] = {
    { "billid",     true },
    { "reason",     true },
    { "address",    false },
    { "caller",     false },
    { "called",     false },
    { "calledfull", false },
    { "username",   false },
    { 0, false },
};

// Internally built, non-overridable parameters
static const char* const s_forbidden[] = {
    "time",
    "chan",
    "operation",
    "direction",
    "status",
    "duration",
    "billtime",
    "ringtime",
    "cdrwrite",
    "cdrtrack",
    "cdrcreate",
    "cdrid",
    "runid",
    0
};

// Print time with configured resolution
static const char* printTime(char* buf,u_int64_t usec)
{
    switch (s_res) {
	case 2:
	    // microsecond resolution
	    sprintf(buf,"%u.%06u",(unsigned int)(usec / 1000000),(unsigned int)(usec % 1000000));
	    break;
	case 1:
	    // millisecond resolution
	    usec = (usec + 500) / 1000;
	    sprintf(buf,"%u.%03u",(unsigned int)(usec / 1000),(unsigned int)(usec % 1000));
	    break;
	default:
	    // 1-second resolution
	    usec = (usec + 500000) / 1000000;
	    sprintf(buf,"%u",(unsigned int)usec);
    }
    return buf;
}

// Expire hungup guard records
static void expireHungup()
{
    Time t;
    while (Hungup* h = static_cast<Hungup*>(s_hungup.get())) {
	if (h->expires() > t.usec())
	    return;
	DDebug("cdrbuild",DebugInfo,"Expiring hungup guard for '%s'",h->c_str());
	s_hungup.remove(h);
    }
}


CdrBuilder::CdrBuilder(const char *name)
    : NamedList(name), m_dir("unknown"), m_status("unknown"),
      m_first(true), m_write(true)
{
    m_start = m_call = m_ringing = m_answer = m_hangup = 0;
    m_cdrId = ++s_seq;
}

CdrBuilder::~CdrBuilder()
{
    if (!m_hangup) {
	// chan.hangup not seen yet - mark the record if possible
	if (!getParam("reason"))
	    addParam("reason","CDR shutdown");
    }
    emit("finalize");
    if (Hungup::s_exp && !s_hungup.find(*this))
	s_hungup.append(new Hungup(*this,false));
}

void CdrBuilder::emit(const char *operation)
{
    if (null())
	return;
    u_int64_t t_hangup = m_hangup ? m_hangup : Time::now();

    u_int64_t
	t_call = m_call, t_ringing = m_ringing, t_answer = m_answer;
    if (!m_start)
	m_start = t_call;
    if (!t_call)
	t_call = m_start;
    if (!t_call)
	t_call = m_start = t_hangup;

    if (!t_answer)
	t_answer = t_hangup;
    else if (t_answer > t_hangup)
	t_answer = t_hangup;

    if (!t_ringing)
	t_ringing = t_answer;
    else if (t_ringing > t_answer)
	t_ringing = t_answer;

    if (!operation)
	operation = m_first ? "initialize" : "update";
    m_first = false;

    DDebug("cdrbuild",DebugAll,"Emit '%s' for '%s' status '%s'",
	operation,c_str(),m_status.c_str());
    char buf[64];
    Message *m = new Message("call.cdr",0,true);
    m->addParam("time",printTime(buf,m_start));
    m->addParam("chan",c_str());
    m->addParam("cdrid",m_cdrId);
    m->addParam("runid",s_runId);
    m->addParam("operation",operation);
    m->addParam("direction",m_dir);
    m->addParam("duration",printTime(buf,t_hangup - m_start));
    m->addParam("billtime",printTime(buf,t_hangup - t_answer));
    m->addParam("ringtime",printTime(buf,t_answer - t_ringing));
    m->addParam("status",m_status);
    if (!getValue("external")) {
	const char* ext = 0;
	if (m_dir == YSTRING("incoming"))
	    ext = getValue(YSTRING("caller"));
	else if (m_dir == YSTRING("outgoing"))
	    ext = getValue(YSTRING("called"));
	if (ext)
	    m->setParam("external",ext);
    }
    m->addParam("cdrwrite",String::boolText(m_write));
    unsigned int n = length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* s = getParam(i);
	if (!s)
	    continue;
	m->addParam(s->name(),*s);
    }
    Engine::enqueue(m);
}

String CdrBuilder::getStatus() const
{
    String s(m_status);
    s << "|" << getValue(YSTRING("caller")) << "|" << getValue(YSTRING("called")) <<
	"|" << getValue(YSTRING("billid"));
    unsigned int sec = 0;
    if (m_start)
	sec = (Time::now() - m_start + 500000) / 1000000;
    s << "|" << sec;
    return s;
}

void CdrBuilder::update(int type, u_int64_t val, const char* status)
{
    switch (type) {
	case CdrStart:
	    if (!m_start)
		m_start = val;
	    break;
	case CdrCall:
	    m_call = val;
	    break;
	case CdrProgress:
	case CdrRinging:
	    if (!m_ringing)
		m_ringing = val;
	    break;
	case CdrAnswer:
	    if (!m_answer)
		m_answer = val;
	    break;
	case CdrHangup:
	    m_hangup = val;
	    break;
    }
    if (!TelEngine::null(status))
	m_status = status;
}

bool CdrBuilder::update(const Message& msg, int type, u_int64_t val)
{
    if (type == CdrDrop) {
	Debug("cdrbuild",DebugNote,"%s CDR for '%s'",
	    (m_first ? "Dropping" : "Closing"),c_str());
	// if we didn't generate an initialize generate no finalize
	if (m_first)
	    clear();
	else {
	    // set a reason if none was set or one is explicitely provided
	    const char* reason = msg.getValue(YSTRING("reason"));
	    if (!(reason || getValue(YSTRING("reason"))))
		reason = "CDR dropped";
	    if (reason)
		setParam("reason",reason);
	}
	s_cdrs.remove(this);
	return true;
    }
    // cdrwrite must be consistent over all emitted messages so we read it once
    if (m_first)
	m_write = msg.getBoolValue(YSTRING("cdrwrite"),true);
    unsigned int n = msg.length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* s = msg.getParam(i);
	if (!s)
	    continue;
	if (s->null())
	    continue;
	if (s->name() == YSTRING("status")) {
	    m_status = *s;
	    if ((m_status == YSTRING("incoming")) || (m_status == YSTRING("outgoing")))
		m_dir = m_status;
	}
	else if (s->name() == YSTRING("direction"))
	    m_dir = *s;
	else {
	    // search the parameter
	    Param* p = static_cast<Param*>(s_params[s->name()]);
	    if (!p)
		continue;
	    bool overwrite = p->overwrite();
	    String* str = getParam(s->name());
	    // parameter is not yet stored - store a copy
	    if (!str)
		addParam(s->name(),*s);
	    // parameter is stored but we should overwrite it
	    else if (overwrite)
		*str = *s;
	}
    }

    update(type,val);

    if (type == CdrHangup) {
	s_cdrs.remove(this);
	// object is now destroyed, "this" no longer valid
	return false;
    }

    emit();
    return false;
}

CdrBuilder* CdrBuilder::find(String &id)
{
    return static_cast<CdrBuilder*>(s_cdrs[id]);
}


bool CdrHandler::received(Message &msg)
{
    Lock lock(s_mutex);
    if (m_type == EngHalt) {
	unsigned int n = s_cdrs.count();
	if (n)
	    Debug("cdrbuild",DebugWarn,"Forcibly finalizing %u CDR records.",n);
	s_cdrs.clear();
	return false;
    }
    if ((m_type == CdrProgress) && !msg.getBoolValue(YSTRING("earlymedia"),false))
	return false;
    bool track = true;
    if (m_type == CdrUpdate) {
	const String* oper = msg.getParam(YSTRING("operation"));
	if (oper && (*oper != YSTRING("cdrbuild")))
	    track = false;
    }
    if (!msg.getBoolValue(YSTRING("cdrtrack"),track))
	return false;
    String id(msg.getValue(YSTRING("id")));
    if (m_type == CdrDrop) {
	if (!id.startSkip("cdrbuild/",false))
	    return false;
    }
    if (id.null())
	return false;
    bool rval = false;
    int type = m_type;
    int level = DebugInfo;
    CdrBuilder *b = CdrBuilder::find(id);
    if (!b) {
	switch (type) {
	    case CdrStart:
	    case CdrCall:
	    case CdrAnswer:
		{
		    expireHungup();
		    Hungup* h = static_cast<Hungup*>(s_hungup[id]);
		    if (h) {
			if (h->hangup())
			    // seen hangup but not emitted call.cdr - do it now
			    type = CdrHangup;
			else {
			    // post-hangup answer cause billing problems so warn
			    level = (CdrAnswer == type) ? DebugWarn : DebugNote;
			    break;
			}
		    }
		}
		if ((type != CdrHangup) && !msg.getBoolValue(YSTRING("cdrcreate"),true))
		    break;
		b = new CdrBuilder(id);
		s_cdrs.append(b);
		break;
	    case CdrHangup:
		expireHungup();
		if (Hungup::s_exp && !s_hungup.find(id))
		    // remember to emit a finalize if we ever see a startup
		    s_hungup.append(new Hungup(id,true));
		else
		    level = DebugMild;
		break;
	}
    }
    if (b)
	rval = b->update(msg,type,msg.msgTime().usec());
    else
	Debug("cdrbuild",level,"Got message '%s' for untracked id '%s'",
	    msg.c_str(),id.c_str());
    if ((type == CdrRinging) || (type == CdrProgress) || (type == CdrAnswer)) {
	id = msg.getValue(YSTRING("peerid"));
	if (id.null())
	    id = msg.getValue(YSTRING("targetid"));
	if (id && (b = CdrBuilder::find(id))) {
	    b->update(type,msg.msgTime().usec(),msg.getValue("status"));
	    b->emit();
	}
    }
    return rval;
};


bool StatusHandler::received(Message &msg)
{
    const String* sel = msg.getParam(YSTRING("module"));
    if (!(TelEngine::null(sel) || (*sel == YSTRING("cdrbuild"))))
	return false;
    String st("name=cdrbuild,type=cdr,format=Status|Caller|Called|BillId|Duration");
    s_mutex.lock();
    expireHungup();
    st << ";cdrs=" << s_cdrs.count() << ",hungup=" << s_hungup.count();
    if (msg.getBoolValue(YSTRING("details"),true)) {
	st << ";";
	ObjList *l = &s_cdrs;
	bool first = true;
	for (; l; l=l->next()) {
	    CdrBuilder *b = static_cast<CdrBuilder *>(l->get());
	    if (b) {
		if (first)
		    first = false;
		else
		    st << ",";
		st << *b << "=" << b->getStatus();
	    }
	}
    }
    s_mutex.unlock();
    msg.retValue() << st << "\r\n";
    return false;
}


bool CommandHandler::received(Message &msg)
{
    static const String name("cdrbuild");
    const String* partial = msg.getParam(YSTRING("partline"));
    if (!partial || *partial != YSTRING("status"))
	return false;
    partial = msg.getParam(YSTRING("partword"));
    if (TelEngine::null(partial) || name.startsWith(*partial))
	msg.retValue().append(name,"\t");
    return false;
}


class CdrBuildPlugin : public Plugin
{
public:
    CdrBuildPlugin();
    virtual ~CdrBuildPlugin();
    virtual void initialize();
private:
    bool m_first;
};

CdrBuildPlugin::CdrBuildPlugin()
    : Plugin("cdrbuild"),
      m_first(true)
{
    Output("Loaded module CdrBuild");
}

CdrBuildPlugin::~CdrBuildPlugin()
{
    Output("Unloading module CdrBuild");
}

void CdrBuildPlugin::initialize()
{
    Output("Initializing module CdrBuild");
    Configuration cfg(Engine::configFile("cdrbuild"));
    s_res = cfg.getIntValue("general","resolution",s_timeRes,1);
    int exp = cfg.getIntValue("general","guardtime",5000);
    if (exp < 0)
	exp = 0;
    else if (exp > 600000)
	exp = 600000;
    s_mutex.lock();
    Hungup::s_exp = 1000 * (u_int64_t)exp;
    s_params.clear();
    const struct _params* params = s_defParams;
    for (; params->name; params++)
	s_params.append(new Param(params->name,params->overwrite));
    const NamedList* sect = cfg.getSection("parameters");
    if (sect) {
	unsigned int n = sect->length();
	for (unsigned int i = 0; i < n; i++) {
	    const NamedString* p = sect->getParam(i);
	    if (!p)
		continue;
	    const char* const* f = s_forbidden;
	    for (; *f; f++)
		if (p->name() == *f)
		    break;
	    if (*f) {
		Debug("cdrbuild",DebugWarn,"Cannot override parameter '%s'",p->name().c_str());
		continue;
	    }
	    Param* par = static_cast<Param*>(s_params[p->name()]);
	    if (par)
		par->overwrite(p->toBoolean(par->overwrite()));
	    else
		s_params.append(new Param(p->name(),p->toBoolean(false)));
	}
    }
    s_mutex.unlock();
    if (m_first) {
	m_first = false;
	s_runId = Engine::runId();
	Engine::install(new CdrHandler("chan.startup",CdrStart));
	Engine::install(new CdrHandler("call.route",CdrRoute));
	Engine::install(new CdrHandler("call.execute",CdrCall));
	Engine::install(new CdrHandler("call.progress",CdrProgress));
	Engine::install(new CdrHandler("call.ringing",CdrRinging));
	Engine::install(new CdrHandler("call.answered",CdrAnswer));
	Engine::install(new CdrHandler("call.update",CdrUpdate));
	Engine::install(new CdrHandler("chan.hangup",CdrHangup,150));
	Engine::install(new CdrHandler("call.drop",CdrDrop));
	Engine::install(new CdrHandler("engine.halt",EngHalt,150));
	Engine::install(new StatusHandler);
	Engine::install(new CommandHandler);
    }
}

INIT_PLUGIN(CdrBuildPlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
