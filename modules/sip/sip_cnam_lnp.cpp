/**
 * sip_cnam_lnp.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Query CNAM and LNP databases using SIP INVITE
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class QuerySipDriver : public Driver
{
public:
    QuerySipDriver();
    ~QuerySipDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest)
	{ return false; }
    virtual bool msgPreroute(Message& msg);
    virtual bool msgRoute(Message& msg);
protected:
    bool received(Message& msg, int id);
    int waitFor(const Channel* c);
};

INIT_PLUGIN(QuerySipDriver);

static Configuration s_cfg;
static ObjList s_waiting;
static int s_notify = 0;
static const String s_cnam = "cnam";
static const String s_lnp = "lnp";


class QuerySipChannel :  public Channel
{
public:
    enum Operation {
	CNAM,
	LNP,
    };
    QuerySipChannel(const char* addr, Operation type, Message* msg);
    ~QuerySipChannel();
    virtual void disconnected(bool final, const char *reason);
private:
    void endCnam(const NamedList& params);
    void endLnp(const NamedList& params);
    Operation m_type;
    Message* m_msg;
};

QuerySipChannel::QuerySipChannel(const char* num, Operation type, Message* msg)
    : Channel(__plugin, 0, false),
      m_type(type), m_msg(msg)
{
    switch (m_type) {
	case CNAM:
	    (m_address = "cnam/") << num;
	    break;
	case LNP:
	    (m_address = "lnp/") << num;
	    break;
    }
}

QuerySipChannel::~QuerySipChannel()
{
    s_notify++;
}

void QuerySipChannel::disconnected(bool final, const char *reason)
{
    DDebug(this,DebugAll,"QuerySipChannel::disconnected() '%s'",reason);
    paramMutex().lock();
    switch (m_type) {
	case CNAM:
	    endCnam(parameters());
	    break;
	case LNP:
	    endLnp(parameters());
	    break;
    }
    paramMutex().unlock();
}

void QuerySipChannel::endCnam(const NamedList& params)
{
    int code = params.getIntValue(YSTRING("cause_sip"));
    if (!params.getBoolValue(YSTRING("redirect"),((code / 100) == 3)))
	return;
    // Caller Name is in the description of the P-Asserted-Identity URI
    URI ident(params.getValue(YSTRING("sip_p-asserted-identity")));
    if (ident.null())
	return;
    m_msg->setParam("querycnam",String::boolText(false));
    String cnam = ident.getDescription();
    if (cnam.null())
	return;
    Debug(this,DebugInfo,"CNAM '%s' for '%s'",cnam.c_str(),ident.getUser().c_str());
    m_msg->setParam("callername",cnam);
}

void QuerySipChannel::endLnp(const NamedList& params)
{
    if (!params.getBoolValue(YSTRING("redirect")))
	return;
    // Routing Number and NPDI are in the Contact header - already parsed
    String called = params.getValue(YSTRING("called"));
    called.toLower();
    if (called.null())
	return;
    ObjList* list = called.split(';',false);
    if (!list)
	return;
    m_msg->setParam("querylnp",String::boolText(false));
    bool npdi = false;
    String rn;
    for (ObjList* item = list->skipNull(); item; item = item->skipNext()) {
	String* s = static_cast<String*>(item->get());
	s->trimSpaces();
	if (s->null())
	    continue;
	int pos = s->find('=');
	if (pos < 0) {
	    npdi = npdi || (*s == YSTRING("npdi"));
	}
	else if (pos > 0) {
	    String key = s->substr(0,pos);
	    key.trimSpaces();
	    if (key == YSTRING("rn"))
		rn = s->substr(pos+1).trimSpaces();
	    else if (key == YSTRING("npdi"))
		npdi = s->substr(pos+1).toBoolean(true);
	}
    }
    TelEngine::destruct(list);
    Debug(this,DebugInfo,"LNP rn='%s' npdi=%s",rn.c_str(),String::boolText(npdi));
    if (rn)
	m_msg->setParam("routing",rn);
    m_msg->setParam("npdi",String::boolText(npdi));
}


bool QuerySipDriver::msgPreroute(Message& msg)
{
    bool handle = msg.getBoolValue(YSTRING("querycnam_sip"),true);
    DDebug(this,DebugAll,"QuerySipDriver::msgPreroute(%s)",String::boolText(handle));
    if (!handle)
	return false;
    Lock mylock(this);
    String callto = s_cfg.getValue(s_cnam,YSTRING("callto"));
    if (callto.null())
	return false;
    String caller = s_cfg.getValue(s_cnam,YSTRING("caller"),"${caller}");
    msg.replaceParams(caller);
    if (!msg.getBoolValue(YSTRING("querycnam"),TelEngine::isE164(caller) && !msg.getParam(YSTRING("callername"))))
	return false;
    String called = s_cfg.getValue(s_cnam,YSTRING("called"),"${called}");
    String domain = s_cfg.getValue(s_cnam,YSTRING("domain"));
    int timeout = s_cfg.getIntValue(s_cnam,YSTRING("timeout"),5000);
    int flags = s_cfg.getIntValue(s_cnam,YSTRING("flags"),-1);
    mylock.drop();
    msg.replaceParams(callto);
    msg.replaceParams(called);
    msg.replaceParams(domain);
    if (timeout < 1000)
	timeout = 1000;
    else if (timeout > 30000)
	timeout = 30000;
    if (callto.startsWith("sip:"))
	callto = "sip/" + callto;
    QuerySipChannel* c = new QuerySipChannel(caller,QuerySipChannel::CNAM,&msg);
    c->initChan();
    Message* m = c->message("call.execute",false,true);
    m->addParam("callto",callto);
    m->addParam("caller",caller);
    m->addParam("called",called);
    m->addParam("domain",domain,false);
    m->addParam("timeout",String(timeout));
    if (-1 != flags)
	m->addParam("xsip_flags",String(flags));
    m->addParam("media",String::boolText(false));
    m->addParam("pbxassist",String::boolText(false));
    m->addParam("cdrtrack",String::boolText(false));
    m->addParam("cdrwrite",String::boolText(false));
    m->addParam("copyparams","pbxassist,cdrwrite");
    m->addParam("querycnam",String::boolText(false));
    c->deref();
    if (!Engine::enqueue(m))
	delete m;
    int t = waitFor(c);
    Debug(this,(t > 500) ? DebugNote : DebugAll,"CNAM lookup took %d msec",t);
    return false;
}

bool QuerySipDriver::msgRoute(Message& msg)
{
    bool handle = msg.getBoolValue(YSTRING("querylnp_sip"),true);
    DDebug(this,DebugAll,"QuerySipDriver::msgRoute(%s)",String::boolText(handle));
    if (!handle)
	return false;
    Lock mylock(this);
    String callto = s_cfg.getValue(s_lnp,YSTRING("callto"));
    if (callto.null())
	return false;
    String called = s_cfg.getValue(s_lnp,YSTRING("called"),"${called}");
    msg.replaceParams(called);
    if (!msg.getBoolValue(YSTRING("querylnp"),TelEngine::isE164(called) && !msg.getBoolValue(YSTRING("npdi"))))
	return false;
    String caller = s_cfg.getValue(s_lnp,YSTRING("caller"),"${caller}");
    String domain = s_cfg.getValue(s_lnp,YSTRING("domain"));
    int timeout = s_cfg.getIntValue(s_lnp,YSTRING("timeout"),5000);
    int flags = s_cfg.getIntValue(s_lnp,YSTRING("flags"),-1);
    mylock.drop();
    msg.replaceParams(callto);
    msg.replaceParams(caller);
    msg.replaceParams(domain);
    if (timeout < 1000)
	timeout = 1000;
    else if (timeout > 30000)
	timeout = 30000;
    if (callto.startsWith("sip:"))
	callto = "sip/" + callto;
    QuerySipChannel* c = new QuerySipChannel(caller,QuerySipChannel::LNP,&msg);
    c->initChan();
    Message* m = c->message("call.execute",false,true);
    m->addParam("callto",callto);
    m->addParam("caller",caller);
    m->addParam("called",called);
    m->addParam("domain",domain,false);
    m->addParam("timeout",String(timeout));
    if (-1 != flags)
	m->addParam("xsip_flags",String(flags));
    m->addParam("media",String::boolText(false));
    m->addParam("pbxassist",String::boolText(false));
    m->addParam("cdrtrack",String::boolText(false));
    m->addParam("cdrwrite",String::boolText(false));
    m->addParam("copyparams","pbxassist,cdrwrite");
    m->addParam("querylnp",String::boolText(false));
    c->deref();
    if (!Engine::enqueue(m))
	delete m;
    int t = waitFor(c);
    Debug(this,(t > 500) ? DebugNote : DebugAll,"LNP lookup took %d msec",t);
    return false;
}

bool QuerySipDriver::received(Message& msg, int id)
{
    return (Private == id) ? msgPreroute(msg) : Driver::received(msg,id);
}

int QuerySipDriver::waitFor(const Channel* c)
{
    u_int64_t t = Time::msecNow();
    for (;;) {
	Lock mylock(this);
	if (!channels().find(c))
	    return (int)(Time::msecNow() - t);
	int n = s_notify;
	mylock.drop();
	while (n == s_notify)
	    Thread::idle();
    }
}

QuerySipDriver::QuerySipDriver()
    : Driver("sip_cnam_lnp", "misc")
{
    Output("Loaded module SipCnamLnp");
}

QuerySipDriver::~QuerySipDriver()
{
    Output("Unloading module SipCnamLnp");
}

void QuerySipDriver::initialize()
{
    Output("Initializing module SipCnamLnp");
    setup("qsip/",true);
    lock();
    s_cfg = Engine::configFile(name());
    s_cfg.load();
    unlock();
    installRelay(Private,"call.preroute",s_cfg.getIntValue("priorities","call.preroute",50));
    installRelay(Route,s_cfg.getIntValue("priorities","call.route",50));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
