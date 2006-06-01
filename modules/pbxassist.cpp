/**
 * pbxassist.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * PBX assist module
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

#include <yatepbx.h>

using namespace TelEngine;
namespace { //anonymous

class YPBX_API PBXAssist : public ChanAssist
{
public:
    inline PBXAssist(ChanAssistList* list, const String& id)
	: ChanAssist(list,id), m_last(0), m_pass(false)
	{ }
    virtual void msgHangup(Message& msg);
    virtual bool msgDisconnect(Message& msg, const String& reason);
    virtual bool msgTone(Message& msg);
    virtual bool msgOperation(Message& msg, const String& operation);
protected:
    bool rememberPeer(const String& peer);
    u_int64_t m_last;
    bool m_pass;
    String m_tones;
    String m_peer1;
    String m_peer2;
};

class YPBX_API PBXList : public ChanAssistList
{
public:
    enum {
	Operation = AssistPrivate
    };
    inline PBXList()
	: ChanAssistList("pbxassist")
	{ }
    virtual ChanAssist* create(Message& msg, const String& id);
    virtual void initialize();
    virtual void init(int priority);
    virtual bool received(Message& msg, int id, ChanAssist* assist);
};

// Inter-tone timeout in usec
static unsigned int s_timeout = 30000000;
// Minimum sequence length
static int s_minlen = 2;
// Maximum sequence length
static int s_maxlen = 20;
// Take back control command
static String s_retake;
// On Hold (music)
static String s_onhold;

// The entire module configuration
static Configuration s_cfg;

ChanAssist* PBXList::create(Message& msg, const String& id)
{
    if (msg.userObject("Channel"))
	return new PBXAssist(this,id);
    return 0;
}

void PBXList::init(int priority)
{
    priority = s_cfg.getIntValue("general","priority",priority);
    ChanAssistList::init(priority);
    installRelay(Tone,priority);
    Engine::install(new MessageRelay("chan.operation",this,Operation,priority));
}

void PBXList::initialize()
{
    lock();
    s_cfg = Engine::configFile(name());
    s_cfg.load();
    s_minlen = s_cfg.getIntValue("general","minlen",2);
    if (s_minlen < 1)
	s_minlen = 1;
    s_maxlen = s_cfg.getIntValue("general","maxlen",20);
    if (s_maxlen < s_minlen)
	s_maxlen = s_minlen;
    int tout = s_cfg.getIntValue("general","timeout",30000);
    if (tout < 1000)
	tout = 1000;
    if (tout > 1800000)
	tout = 1800000;
    s_timeout = (unsigned int)tout * 1000;
    s_retake = s_cfg.getValue("general","retake","###");
    s_onhold = s_cfg.getValue("general","onhold","moh/default");
    unlock();
    if (s_cfg.getBoolValue("general","enabled",true))
	ChanAssistList::initialize();
}

bool PBXList::received(Message& msg, int id, ChanAssist* assist)
{
    switch (id) {
	case Tone:
	    return static_cast<PBXAssist*>(assist)->msgTone(msg);
	case Operation:
	    return static_cast<PBXAssist*>(assist)->msgOperation(msg,msg.getValue("operation"));
	default:
	    return false;
    }
}


bool PBXAssist::msgDisconnect(Message& msg, const String& reason)
{
    if ((reason == "hold") || (reason == "park")) {
	if (s_onhold) {
	    msg = "call.execute";
	    msg.setParam("callto",s_onhold);
	}
	return false;
    }
    return ChanAssist::msgDisconnect(msg,reason);
}

bool PBXAssist::msgTone(Message& msg)
{
    const char* tone = msg.getValue("text");
    if (null(tone))
	return false;
    if (m_last && m_tones && ((m_last + s_timeout) < msg.msgTime())) {
	Debug(list(),DebugNote,"Chan '%s' collect timeout, clearing tones '%s'",id().c_str(),m_tones.c_str());
	m_tones.clear();
    }
    m_last = msg.msgTime();
    m_tones += tone;
    // truncate collected number to some decent length
    if ((int)m_tones.length() > s_maxlen)
	m_tones = m_tones.substr(-s_maxlen);
    Debug(list(),DebugCall,"Chan '%s' got tone '%s' collected '%s'",id().c_str(),tone,m_tones.c_str());
    if ((int)m_tones.length() < s_minlen)
	return false;
    if (m_pass) {
	// we are in pass-through mode, only look for takeback comand
	if (m_tones.endsWith(s_retake)) {
	    Debug(list(),DebugCall,"Chan '%s' back in tone collect mode",id().c_str());
	    m_pass = false;
	    m_tones.clear();
	    return true;
	}
	return false;
    }
    Lock lock(list());
    int n = s_cfg.sections();
    for (int i = 0; i < n; i++) {
	NamedList* sect = s_cfg.getSection(i);
	if (!sect)
	    continue;
	const char* tmp = sect->getValue("trigger");
	if (!tmp)
	    continue;
	Regexp r(tmp);
	if (!m_tones.matches(r))
	    continue;
	// good! we matched the trigger sequence
	tmp = sect->getValue("operation",*sect);
	if (tmp) {
	    Debug(list(),DebugInfo,"Chan '%s' triggered operation '%s'",id().c_str(),tmp);
	    if (sect->getBoolValue("remember",true))
		rememberPeer(msg.getValue("peerid"));
	    // now masquerade the message
	    Message* m = new Message("chan.masquerade");
	    m->addParam("id",id());
	    m->addParam("message",sect->getValue("message","chan.operation"));
	    m->addParam("operation",tmp);
	    unsigned int len = sect->length();
	    for (unsigned int idx = 0; idx < len; idx++) {
		const NamedString* s = sect->getParam(idx);
		if ((s->name() == "trigger") ||
		    (s->name() == "operation") ||
		    (s->name() == "remember") ||
		    (s->name() == "message"))
		    continue;
		m->addParam(s->name(),m_tones.replaceMatches(*s));
	    }
	    Engine::enqueue(m);
	}
	m_tones.clear();
	return true;
    }
    return false;
}

bool PBXAssist::msgOperation(Message& msg, const String& operation)
{
    if (operation == "passthrough") {
	if (s_retake.null()) {
	    Debug(list(),DebugWarn,"Chan '%s' refusing pass-through, retake string is not set!",id().c_str());
	    return true;
	}
	Debug(list(),DebugCall,"Chan '%s' entering tone pass-through mode",id().c_str());
	m_pass = true;
	m_tones.clear();
	return true;
    }
    else if (operation == "conference") {
	RefPointer<CallEndpoint> c = locate();
	if (!c)
	    return false;
	String peer = c->getPeerId();
	rememberPeer(peer);
	Message* m = new Message("call.conference");
	m->addParam("id",id());
	m->addParam("callto",s_onhold);
	Engine::enqueue(m);
	return true;
    }
    return false;
}

void PBXAssist::msgHangup(Message& msg)
{
    RefPointer<CallEndpoint> c1 = locate(m_peer1);
    if (c1) {
	RefPointer<CallEndpoint> c2 = locate(m_peer2);
	if (c2) {
	    // we hung up having two peers on hold - join them
	    Debug(list(),DebugCall,"Chan '%s' doing autotransfer '%s' <-> '%s'",
		id().c_str(),m_peer1.c_str(),m_peer2.c_str());
	    c1->connect(c2);
	}
    }
}

bool PBXAssist::rememberPeer(const String& peer)
{
    if (peer.null() || (peer == id()))
	return false;
    if (peer == m_peer1)
	return true;
    if (m_peer1.null()) {
	m_peer1 = peer;
	return true;
    }
    if (peer == m_peer2)
	return true;
    if (m_peer2.null()) {
	m_peer2 = peer;
	return true;
    }
    Debug(list(),DebugMild,"Channel '%s' can not remember '%s', both slots full",
	id().c_str(),peer.c_str());
    return false;
}
	
INIT_PLUGIN(PBXList);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
