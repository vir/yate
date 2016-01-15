/**
 * cdrbuild.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Cdr builder
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

#include <yatengine.h>

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

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

class CdrBuildPlugin : public Plugin
{
public:
    CdrBuildPlugin();
    virtual ~CdrBuildPlugin();
    virtual void initialize();
private:
    bool m_first;
};

INIT_PLUGIN(CdrBuildPlugin);

class CdrHandler : public MessageHandler
{
public:
    CdrHandler(const char *name, int type, int prio = 50)
	: MessageHandler(name,prio,__plugin.name()),
	  m_type(type)
	{ }
    virtual bool received(Message &msg);
private:
    int m_type;
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
    u_int64_t m_statusTime;
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

class StatusThread : public Thread
{
public:
    inline StatusThread()
	:m_exit(false),m_maxSleep(25)
	{ }
    inline ~StatusThread()
	{ }
    virtual void run();
    inline void exit()
	{ m_exit = true; }
private:
    bool m_exit;
    int m_maxSleep;
};

class CustomTimer : public String {
public:
    inline CustomTimer(bool relative = false)
	:m_enabled(false), m_gmt(false), m_relative(relative),m_usecCount(0),
	m_usecIndex(-1)
	{}
    virtual ~CustomTimer()
	{}
    void process(const String& value);
    void getTime(String& ret, u_int64_t time);
    void getRelativeTime(String& ret, u_int64_t time);
    bool m_enabled;
private:
    void extractUsec(const String& param);
    bool m_gmt;
    bool m_relative;
    int m_usecCount;
    int m_usecIndex;
};


static ObjList s_cdrs;
static ObjList s_hungup;
CustomTimer m_startTime;
CustomTimer m_answerTime;
CustomTimer m_hangupTime;
CustomTimer m_durationTime(true);
u_int64_t Hungup::s_exp = 5000000;

// This mutex protects both the CDR list and the params list
static Mutex s_mutex(false,"CdrBuild");
static ObjList s_params;
static int s_res = 1;
static int s_seq = 0;
static String s_runId;
static bool s_cdrUpdates = true;
static bool s_cdrStatus = false;
static bool s_statusAnswer = true;
static bool s_ringOnProgress = false;
static unsigned int s_statusUpdate = 60000;
static StatusThread* s_updaterThread = 0;

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

static const char* printTime(char* buf, u_int64_t usec, int count)
{
    switch (count) {
	case 1:
	    usec = (usec + 50000) / 100000;
	    sprintf(buf,"%01u",(unsigned int)(usec % 10));
	    break;
	case 2:
	    usec = (usec + 5000) / 10000;
	    sprintf(buf,"%02u",(unsigned int)(usec % 100));
	    break;
	case 3:
	    usec = (usec + 500) / 1000;
	    sprintf(buf,"%03u",(unsigned int)(usec % 1000));
	    break;
	case 4:
	    usec = (usec + 50) / 100;
	    sprintf(buf,"%04u",(unsigned int)(usec % 10000));
	    break;
	case 5:
	    usec = (usec + 5) / 10;
	    sprintf(buf,"%05u",(unsigned int)(usec % 100000));
	    break;
	default:
	    sprintf(buf,"%06u",(unsigned int)(usec % 1000000));
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
    m_statusTime = m_start = m_call = m_ringing = m_answer = m_hangup = 0;
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

    if (String(operation) == YSTRING("update") && !s_cdrUpdates)
	return;

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
    String tmp;

    if (m_startTime.m_enabled) {
	m_startTime.getTime(tmp,m_start);
	m->addParam("call_start_time",tmp);
    }
    if (m_answerTime.m_enabled) {
	m_answerTime.getTime(tmp,t_answer);
	m->addParam("call_answer_time",tmp);
    }
    if (m_hangupTime.m_enabled) {
	m_hangupTime.getTime(tmp,t_hangup);
	m->addParam("call_hangup_time",tmp);
    }

    if (m_durationTime.m_enabled) {
	m_durationTime.getTime(tmp,t_hangup - m_start);
	m->addParam("call_duration",tmp);
    }

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
	sec = (int)((Time::now() - m_start + 500000) / 1000000);
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
	if (s_updaterThread)
	    s_updaterThread->exit();
	return false;
    }
    if ((m_type == CdrProgress) &&
	!(msg.getBoolValue(YSTRING("earlymedia"),false) ||
	msg.getBoolValue(YSTRING("ringing"),s_ringOnProgress)))
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
    if (b) {
	rval = b->update(msg,type,msg.msgTime().usec());
	if (type == CdrAnswer && !b->m_statusTime)
	    b->m_statusTime = Time::msecNow() + (s_statusAnswer ? 0 : s_statusUpdate);
    } else
	Debug("cdrbuild",level,"Got message '%s' for untracked id '%s'",
	    msg.c_str(),id.c_str());
    if ((type == CdrRinging) || (type == CdrProgress) || (type == CdrAnswer)) {
	id = msg.getValue(YSTRING("peerid"));
	if (id.null())
	    id = msg.getValue(YSTRING("targetid"));
	if (id && (b = CdrBuilder::find(id))) {
	    b->update(type,msg.msgTime().usec(),msg.getValue("status"));
	    b->emit();
	    if (type == CdrAnswer && !b->m_statusTime)
		b->m_statusTime = Time::msecNow() + (s_statusAnswer ? 0 : s_statusUpdate);
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

void StatusThread::run()
{
    // Check if we should emit cdr status
    Time now;
    s_mutex.lock();
    for (ObjList* o = s_cdrs.skipNull();o;o = o->skipNext()) {
	CdrBuilder* cdr = static_cast<CdrBuilder*>(o->get());
	if (cdr->getStatus() != YSTRING("answered"))
	    continue;
	if (cdr->m_statusTime && (cdr->m_statusTime < now.msec())) {
	    cdr->emit("status");
	    cdr->m_statusTime = s_statusUpdate ? (now.msec() + s_statusUpdate) : (u_int64_t)-1;
	}
    }
    s_mutex.unlock();

    // Check cdrs for timeout and emit cdr status
    while (!m_exit) {
	Thread::msleep(m_maxSleep);
	Lock lock(s_mutex);
	Time t;
	for (ObjList* o = s_cdrs.skipNull();o;o = o->skipNext()) {
	    CdrBuilder* cdr = static_cast<CdrBuilder*>(o->get());
	    if (cdr->m_statusTime && (cdr->m_statusTime < t.msec())) {
		cdr->emit("status");
		cdr->m_statusTime = s_statusUpdate ? (t.msec() + s_statusUpdate) : (u_int64_t)-1;
	    }
	}
    }
}

void CustomTimer::process(const String& value)
{
    if (value.length() == 0) {
	m_enabled = false;
	return;
    }
    if (m_relative) {
	extractUsec(value);
	return;
    }
    String tmp = value;
    int count = 4;
    // YYYY or YY the year
    int i = tmp.find(YSTRING("YYYY"));
    if (i < 0) {
	i = tmp.find(YSTRING("YY"));
	if (i >= 0)
	    count = 2;
    }
    if (i >= 0)
	tmp = tmp.substr(0,i) + ((count == 2) ? YSTRING("%y") : YSTRING("%Y")) +
		tmp.substr(i + count);
    // MM month
    i = tmp.find(YSTRING("MM"));
    if (i >= 0)
	tmp = tmp.substr(0,i) + YSTRING("%m") + tmp.substr(i + 2);
    // DD day
    i = tmp.find(YSTRING("DD"));
    if (i >= 0)
	tmp = tmp.substr(0,i) + YSTRING("%d") + tmp.substr(i + 2);
    // HH hour
    i = tmp.find(YSTRING("HH"));
    if (i >= 0)
	tmp = tmp.substr(0,i) + YSTRING("%H") + tmp.substr(i + 2);
    // mm minutes
    i = tmp.find(YSTRING("mm"));
    if (i >= 0)
	tmp = tmp.substr(0,i) + YSTRING("%M") + tmp.substr(i + 2);
    // SS seconds
    i = tmp.find(YSTRING("SS"));
    if (i >= 0)
	tmp = tmp.substr(0,i) + YSTRING("%S") + tmp.substr(i + 2);
    // UTC time zone
    i = tmp.find(YSTRING("UTC"));
    m_gmt = i >= 0;
    if (i >= 0)
	tmp = tmp.substr(0,i) + YSTRING("%Z") + tmp.substr(i + 3);
    else
	tmp += " %Z";

    m_enabled = true;
    extractUsec(tmp);
}

void CustomTimer::extractUsec(const String& param)
{
    String tmp = param;
    int i = tmp.find('u');
    if (i < 0) {
	assign(tmp);
	return;
    }
    int count = 1;
    char c;
    while ((c = tmp.at(i + count)) == 'u')
	count++;
    m_usecCount = (count > 6) ? 6 : count;
    //m_usecCount = pow(10,6 - count);
    m_usecIndex = i;
    if (i >= 0)
	tmp = tmp.substr(0,i) + tmp.substr(i + count);
    assign(tmp);
}

void CustomTimer::getTime(String& ret, u_int64_t time)
{
    DataBlock data(0,length() + 100);
    char* buf = (char*)data.data();
    time_t rawtime = time / 1000000;
    String tmp = *this;
    if (m_usecIndex >= 0) {
	char buf1[10];
	printTime(buf1,time,m_usecCount);
	String usec(buf1);
	tmp = tmp.substr(0,m_usecIndex) + usec + tmp.substr(m_usecIndex);
    } else if (time % 1000000 > 500000){
	rawtime ++;
    }
    if (m_relative) {
	getRelativeTime(tmp,time);
	ret.assign(tmp);
	return;
    }
    struct tm * timeinfo;
    if (!m_gmt)
	timeinfo = localtime(&rawtime);
    else
	timeinfo = gmtime(&rawtime);
    int len = strftime (buf, length() + 100, tmp, timeinfo);
    ret.assign(buf,len);
}

void CustomTimer::getRelativeTime(String& ret, u_int64_t time)
{
    u_int64_t timeLeft = time / 1000000;
    String tmp = ret;
    int index = tmp.find(YSTRING("HH"));
    if (index >= 0) {
	int h = (int)(timeLeft / 3600);
	timeLeft = timeLeft % 3600;
	String aux = "";
	if (h <= 9)
	    aux = "0";
	tmp = tmp.substr(0,index) + aux + String(h) + tmp.substr(index + 2);
    }

    index = tmp.find(YSTRING("mm"));
    if (index >= 0) {
	int m = (int)(timeLeft / 60);
	timeLeft = timeLeft % 60;
	String aux = "";
	if (m <= 9)
	    aux = "0";
	tmp = tmp.substr(0,index) + aux + String(m) + tmp.substr(index + 2);
    }

    index = tmp.find(YSTRING("SS"));
    if (index >= 0) {
	String aux = "";
	if (timeLeft <= 9)
	    aux = "0";
	tmp = tmp.substr(0,index) + aux + String((int)timeLeft) + tmp.substr(index + 2);
    }
    ret.assign(tmp);
}

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
    s_cdrUpdates = cfg.getBoolValue("general","updates",true);
    s_cdrStatus = cfg.getBoolValue("general","status",false);
    s_statusAnswer = cfg.getBoolValue("general","status_answer",true);
    int sUpdate = cfg.getIntValue("general","status_interval",60);
    if (sUpdate <= 0)
	s_statusUpdate = 0;
    else if (sUpdate < 60)
	s_statusUpdate = 60000;
    else if (sUpdate > 600)
	s_statusUpdate = 600000;
    else
	s_statusUpdate = sUpdate * 1000;
    s_ringOnProgress = cfg.getBoolValue("general","ring_on_progress",false);;

    if (s_cdrStatus && !s_updaterThread) {
	s_updaterThread = new StatusThread();
	s_updaterThread->startup();
    } else if (s_updaterThread && !s_cdrStatus) {
	s_updaterThread->exit();
	s_updaterThread = 0;
    }

    while (true) {
	NamedList* timers = cfg.getSection("formatted-timers");
	if (!timers) {
	    m_startTime.m_enabled = false;
	    m_answerTime.m_enabled = false;
	    m_hangupTime.m_enabled = false;
	    break;
	}
	NamedString* param = timers->getParam("call_start_time");
	if (param)
	    m_startTime.process(*param);
	m_startTime.m_enabled = param != 0;

	param = timers->getParam("call_answer_time");
	if (param)
	    m_answerTime.process(*param);
	m_answerTime.m_enabled = param != 0;

	param = timers->getParam("call_hangup_time");
	if (param)
	    m_hangupTime.process(*param);
	m_hangupTime.m_enabled = param != 0;

	param = timers->getParam("call_duration");
	if (param)
	    m_durationTime.process(*param);
	m_durationTime.m_enabled = param != 0;
	break;
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

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
