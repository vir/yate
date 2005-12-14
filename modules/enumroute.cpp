/**
 * enumroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ENUM routing module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatephone.h>

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>


using namespace TelEngine;

class NAPTR : public GenObject
{
public:
    NAPTR(int ord, int pref, const char* flags, const char* serv, const char* regexp, const char* replace);
    bool replace(String& str);
    inline int order() const
	{ return m_order; }
    inline int pref() const
	{ return m_pref; }
    inline const String& flags() const
	{ return m_flags; }
    inline const String& serv() const
	{ return m_service; }

private:
    int m_order;
    int m_pref;
    String m_flags;
    String m_service;
    Regexp m_regmatch;
    String m_template;
    String m_replace;
};

class EnumHandler : public MessageHandler
{
public:
    inline EnumHandler(unsigned int prio = 90)
	: MessageHandler("call.route",prio)
	{ }
    virtual bool received(Message& msg);
};

class EnumModule : public Module
{
public:
    inline EnumModule()
	: Module("enumroute","route"), m_init(false)
	{ }
    virtual void initialize();
    virtual void statusParams(String& str);
    void genUpdate(Message& msg);
private:
    bool m_init;
};

// weird but NS_MAXSTRING and dn_string() are NOT part of the resolver API...
#ifndef NS_MAXSTRING
#define NS_MAXSTRING 255
#endif

// copy one string (not domain) from response
static int dn_string(const unsigned char* end, const unsigned char* src, char *dest, int maxlen)
{
    int n = src[0];
    maxlen--;
    if (maxlen > n)
	maxlen = n;
    if (dest && (maxlen > 0)) {
	while ((maxlen-- > 0) && (src < end))
	    *dest++ = *++src;
	*dest = 0;
    }
    return n+1;
}


static String s_prefix;
static String s_domain;
static bool s_redirect;
static bool s_sipUsed;
static bool s_iaxUsed;
static bool s_h323Used;
static bool s_telUsed;

static Mutex s_mutex;
static int s_queries = 0;
static int s_routed = 0;
static int s_reroute = 0;

static EnumModule emodule;

// Perform DNS query, return list of only NAPTR records
static ObjList* naptrQuery(const char* dname)
{
    unsigned char buf[2048];
    int r,q,a;
    unsigned char *p, *e;
    r = res_query(dname,ns_c_in,ns_t_naptr,
	buf,sizeof(buf));
    XDebug(&emodule,DebugAll,"res_query %d",r);
    if ((r < 0) || (r > (int)sizeof(buf)))
	return 0;
    p = buf+NS_QFIXEDSZ;
    NS_GET16(q,p);
    NS_GET16(a,p);
    XDebug(&emodule,DebugAll,"questions: %d, answers: %d",q,a);
    p = buf + NS_HFIXEDSZ;
    e = buf + r;
    for (; q > 0; q--) {
	int n = dn_skipname(p,e);
	if (n < 0)
	    return 0;
	p += (n + NS_QFIXEDSZ);
    }
    XDebug(&emodule,DebugAll,"skipped questions");
    ObjList* lst = 0;
    for (; a > 0; a--) {
	int ty,cl,sz;
	long int tt;
	char name[NS_MAXLABEL+1];
	unsigned char* l;
	int n = dn_expand(buf,e,p,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    return lst;
	buf[n] = 0;
	p += n;
	NS_GET16(ty,p);
	NS_GET16(cl,p);
	NS_GET32(tt,p);
	NS_GET16(sz,p);
	XDebug(&emodule,DebugAll,"found '%s' type %d size %d",name,ty,sz);
	l = p;
	p += sz;
	if (ty == ns_t_naptr) {
	    int ord,pr;
	    char fla[NS_MAXSTRING+1];
	    char ser[NS_MAXSTRING+1];
	    char reg[NS_MAXSTRING+1];
	    char rep[NS_MAXLABEL+1];
	    NS_GET16(ord,l);
	    NS_GET16(pr,l);
	    n = dn_string(e,l,fla,sizeof(fla));
	    l += n;
	    n = dn_string(e,l,ser,sizeof(ser));
	    l += n;
	    n = dn_string(e,l,reg,sizeof(reg));
	    l += n;
	    n = dn_expand(buf,e,l,rep,sizeof(rep));
	    l += n;
	    DDebug(&emodule,DebugAll,"order=%d pref=%d flags='%s' serv='%s' regexp='%s' replace='%s'",
		ord,pr,fla,ser,reg,rep);
	    if (!lst)
		lst = new ObjList;
	    NAPTR* ptr;
	    ObjList* cur = lst;
	    // cycle existing records, insert at the right place
	    for (; cur; cur = cur->next()) {
		ptr = static_cast<NAPTR*>(cur->get());
		if (!ptr)
		    continue;
		if (ptr->order() > ord)
		    break;
		if (ptr->order() < ord)
		    continue;
		// sort first by order and then by preference
		if (ptr->pref() > pr)
		    break;
	    }
	    ptr = new NAPTR(ord,pr,fla,ser,reg,rep);
	    if (cur)
		cur->insert(ptr);
	    else
		lst->append(ptr);
	}
    }
    return lst;
}

NAPTR::NAPTR(int ord, int pref, const char* flags, const char* serv, const char* regexp, const char* replace)
    : m_order(ord), m_pref(pref), m_flags(flags), m_service(serv), m_replace(replace)
{
    // use case-sensitive extended regular expressions
    m_regmatch.setFlags(true,false);
    if (!null(regexp)) {
	// look for <sep>regexp<sep>template<sep>
	char sep[2] = { regexp[0], 0 };
	String tmp(regexp+1);
	if (tmp.endsWith(sep)) {
	    int pos = tmp.find(sep);
	    if (pos > 0) {
		m_regmatch = tmp.substr(0,pos);
		m_template = tmp.substr(pos+1,tmp.length()-pos-2);
		XDebug(&emodule,DebugAll,"NAPTR match '%s' template '%s'",m_regmatch.c_str(),m_template.c_str());
	    }
	}
    }
}

// Perform the Regexp replacement, return true if succeeded
bool NAPTR::replace(String& str)
{
    if (m_regmatch && str.matches(m_regmatch)) {
	str = str.replaceMatches(m_template);
	return true;
    }
    return false;
}


bool EnumHandler::received(Message& msg)
{
    String called(msg.getValue("called"));
    if (called.null() && msg.getBoolValue("enumroute",true))
	return false;
    // check if the called starts with international prefix, remove it
    if (!(called.startSkip("+",false) ||
	 (s_prefix && called.startSkip(s_prefix,false))))
	return false;
    bool rval = false;
    // put the standard international prefix in front
    called = "+" + called;
    String tmp;
    for (int i = called.length()-1; i > 0; i--)
	tmp << called.at(i) << ".";
    tmp << s_domain;
    DDebug(&emodule,DebugInfo,"Querying %s",tmp.c_str());
    u_int64_t dt = Time::now();
    ObjList* res = naptrQuery(tmp);
    dt = Time::now() - dt;
    Debug(&emodule,DebugInfo,"Returned %d NAPTR records in %u.%06u s",
	res ? res->count() : 0,
	(unsigned int)(dt / 1000000),
	(unsigned int)(dt % 1000000));
    bool reroute = false;
    if (res) {
	ObjList* cur = res;
	for (; cur; cur = cur->next()) {
	    NAPTR* ptr = static_cast<NAPTR*>(cur->get());
	    if (!ptr)
		continue;
	    DDebug(&emodule,DebugAll,"order=%d pref=%d '%s'",
		ptr->order(),ptr->pref(),ptr->serv().c_str());
	    String serv = ptr->serv();
	    serv.toUpper();
	    if (s_sipUsed && (serv == "E2U+SIP") && ptr->replace(called)) {
		msg.retValue() = "sip/" + called;
		rval = true;
		break;
	    }
	    if (s_iaxUsed && (serv == "E2U+IAX2") && ptr->replace(called)) {
		msg.retValue() = "iax/" + called;
		rval = true;
		break;
	    }
	    if (s_h323Used && (serv == "E2U+H323") && ptr->replace(called)) {
		msg.retValue() = "h323/" + called;
		rval = true;
		break;
	    }
	    if (s_telUsed && (serv == "E2U+TEL") && ptr->replace(called)) {
		if (called.startSkip("tel:",false) ||
		    called.startSkip("TEL:",false) ||
		    called.startSkip("e164:",false) ||
		    called.startSkip("E164:",false))
		{
		    reroute = true;
		    msg.setParam("called",called);
		    break;
		}
	    }
	}
	res->destruct();
    }
    if (rval && s_redirect)
	msg.setParam("redirect",String::boolText(true));
    s_mutex.lock();
    s_queries++;
    if (rval)
	s_routed++;
    if (reroute)
	s_reroute++;
    emodule.changed();
    s_mutex.unlock();
    return rval;
}

void EnumModule::statusParams(String& str)
{
    str.append("queries=",",") << s_queries << ",routed=" << s_routed << ",rerouted=" << s_reroute;
}

void EnumModule::genUpdate(Message& msg)
{
    msg.setParam("queries",String(s_queries));
    msg.setParam("routed",String(s_routed));
    msg.setParam("rerouted",String(s_reroute));
}

void EnumModule::initialize()
{
    Module::initialize();
    Output("Initializing ENUM routing");
    Configuration cfg(Engine::configFile("enumroute"));
    // in most of the world this default international prefix should work
    s_prefix = cfg.getValue("general","prefix","00");
    s_domain = cfg.getValue("general","domain","e164.org");
    s_redirect = cfg.getBoolValue("general","redirect");
    s_sipUsed = cfg.getBoolValue("protocols","sip",true);
    s_iaxUsed = cfg.getBoolValue("protocols","iax",true);
    s_h323Used = cfg.getBoolValue("protocols","h323",true);
    // by default don't support the number rerouting
    s_telUsed = cfg.getBoolValue("protocols","tel",false);
    if (m_init)
	return;
    m_init = true;
    int res = res_init();
    if (res)
	Debug(&emodule,DebugGoOn,"res_init returned error %d",res);
    else
	Engine::install(new EnumHandler(cfg.getIntValue("general","priority",90)));
}

/* vi: set ts=8 sw=4 sts=4 noet: */
