/**
 * pbxassist.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * PBX assist module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 * Author: Monica Tepelus
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
    inline PBXAssist(ChanAssistList* list, const String& id, bool pass)
	: ChanAssist(list,id), m_last(0), m_pass(pass), m_state("new")
	{ Debug(list,DebugCall,"Created assistant for '%s'",id.c_str()); }
    virtual void msgHangup(Message& msg);
    virtual bool msgDisconnect(Message& msg, const String& reason);
    virtual bool msgTone(Message& msg);
    virtual bool msgOperation(Message& msg, const String& operation);
protected:
    bool errorBeep();
    u_int64_t m_last;
    bool m_pass;
    String m_tones;
    String m_peer1;
    String m_room;
    String m_state;
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

// assist all channels by default?
static bool s_assist = true;

// filter for channel names to enable assistance
static Regexp s_filter;

// DTMF pass-through all channels by default?
static bool s_pass = false;

// interdigit timeout, clear collected digits if last was older than this
static unsigned int s_timeout = 30000000;

// minimum length of collected digits before we start interpreting
static int s_minlen = 2;

// maximum length of collected digits, drop old if longer
static int s_maxlen = 20;

// command to take back from DTMF pass-through mode
static String s_retake;

// on-hold (music) source name
static String s_onhold;

// error beep override source name
static String s_error;

static Configuration s_cfg;

ChanAssist* PBXList::create(Message& msg, const String& id)
{
    Debug(this,DebugCall,"Asked to create assistant for '%s'",id.c_str());
    if (msg == "chan.startup" || msg.userObject("Channel")) {
	// if a filter is set try to match it
	if (s_filter && !s_filter.matches(id))
	    return 0;
	// allow routing to enable/disable assistance
	if (msg.getBoolValue("pbxassist",s_assist))
	    return new PBXAssist(this,id,msg.getBoolValue("dtmfpass",s_pass));
    }
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
    s_assist = s_cfg.getBoolValue("general","default",false);
    s_filter = s_cfg.getValue("general","filter");
    s_pass = s_cfg.getBoolValue("general","dtmfpass",false);
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
    s_error = s_cfg.getValue("general","error","tone/outoforder");
    unlock();
    if (s_cfg.getBoolValue("general","enabled",true))
	ChanAssistList::initialize();
}

// Handler for relayed messages, call corresponding assistant method
bool PBXList::received(Message& msg, int id, ChanAssist* assist)
{
    // check if processing was explicitely disallowed
    if (!msg.getBoolValue("pbxassist",true))
	return false;
    switch (id) {
	case Tone:
	    return static_cast<PBXAssist*>(assist)->msgTone(msg);
	case Operation:
	    return static_cast<PBXAssist*>(assist)->msgOperation(msg,msg.getValue("operation"));
	default:
	    return false;
    }
}


// Handler for channel disconnects, may go on hold or to dialtone
bool PBXAssist::msgDisconnect(Message& msg, const String& reason)
{
    if (m_state == "hangup")
	return ChanAssist::msgDisconnect(msg,reason);

    if ((reason == "hold") || (reason == "park")) {
	if (s_onhold) {
	    Channel* c = static_cast<Channel*>(msg.userObject("Channel"));
	    if (!c)
		return false;
	    Message *m = c->message("call.execute",false,true);
	    m->addParam("callto",s_onhold);
	    m->addParam("reason","hold");
	    m->addParam("pbxstate",m_state);
	    Engine::enqueue(m);
	}
	return false;
    }

    if (m_state != "new") {
	Channel* c = static_cast<Channel*>(msg.userObject("Channel"));
	if (!c)
	    return false;
	Message *m = c->message("call.execute",false,true);
	m->addParam("callto","tone/dial");
	m->addParam("reason","hold");
	m->addParam("pbxstate",m_state);
	Engine::enqueue(m);
	return false;
    }

    return ChanAssist::msgDisconnect(msg,reason);
}

// Handler for channel's DTMF
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
    if ((int)m_tones.length() > s_maxlen)
	m_tones = m_tones.substr(-s_maxlen);
    Debug(list(),DebugInfo,"Chan '%s' got tone '%s' collected '%s'",id().c_str(),tone,m_tones.c_str());
    if (m_pass) {
	if (m_tones.endsWith(s_retake)) {
	    Debug(list(),DebugCall,"Chan '%s' back in command hunt mode",id().c_str());
	    m_pass = false;
	    m_tones.clear();
	    // we just preserve the last state we were in
	    return true;
	}
	return false;
    }
    if ((int)m_tones.length() < s_minlen)
	return true;

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

	tmp = sect->getValue("pbxstates");
	if (tmp) {
	    Regexp st(tmp);
	    if (!st.matches(m_state))
		continue;
	}

	tmp = sect->getValue("operation",*sect);
	if (tmp) {
	    Debug(list(),DebugNote,"Chan '%s' triggered operation '%s' in state '%s' holding '%s'",
		id().c_str(),tmp,m_state.c_str(),m_peer1.c_str());
	    Message* m = new Message("chan.masquerade");
	    m->addParam("id",id());
	    m->addParam("message",sect->getValue("message","chan.operation"));
	    m->addParam("operation",tmp);
	    unsigned int len = sect->length();
	    for (unsigned int idx = 0; idx < len; idx++) {
		const NamedString* s = sect->getParam(idx);
		if ((s->name() == "trigger") ||
		    (s->name() == "pbxstates") ||
		    (s->name() == "operation") ||
		    (s->name() == "message"))
		    continue;
		m->addParam(s->name(),m_tones.replaceMatches(*s));
	    }
	    m->addParam("pbxstate",m_state);
	    Engine::enqueue(m);
	}
	m_tones.clear();
    }
    // swallow the tone
    return true;
}

// Start an error beep and return false so we can "return errorBeep()"
bool PBXAssist::errorBeep()
{
    if (s_error.null())
	return false;
    Message *m=new Message("chan.masquerade");
    m->addParam("message","chan.attach");
    m->addParam("id",id());
    m->addParam("pbxstate",m_state);
    m->addParam("override",s_error);
    m->addParam("single","yes");
    Engine::enqueue(m);
    return false;
}

// Operation message handler
bool PBXAssist::msgOperation(Message& msg, const String& operation)
{
    if (operation == "passthrough") {
	// enter DTMF pass-through to peer mode, only look for retake string
	if (s_retake.null()) {
	    Debug(list(),DebugWarn,"Chan '%s' refusing pass-through, retake string is not set!",id().c_str());
	    return true;
	}
	Debug(list(),DebugCall,"Chan '%s' entering tone pass-through mode",id().c_str());
	m_pass = true;
	m_tones.clear();
	// don't change state as we have a special variable for this mode
	return true;
    }

    else if (operation == "setstate") {
	// just set the current state and conference room
	m_state = msg.getValue("state",m_state);
	m_room = msg.getValue("room",m_room);
	return true;
    }

    else if (operation == "conference") {
	// turn the call into a conference or connect back to one it left
	if (m_state=="conference")
	    return errorBeep();
	RefPointer<CallEndpoint> c = locate();
	if (!c)
	    return errorBeep();
	String peer = c->getPeerId();
	const char* room = msg.getValue("room",m_room);

	if (peer && !peer.startsWith("tone"))
	{
	    Message m("call.conference");
	    m.addParam("id",id());
	    m.userData(c);
	    m.addParam("lonely","yes");
	    if (room)
		m.addParam("room",room);
	    m.addParam("pbxstate",m_state);

	    if (Engine::dispatch(m) && m.userData()) {
		m_room=m.getParam("room");
		if (m_peer1 && (m_peer1 != peer)) {
		    // take the held party in the conference
		    Message* m = new Message("chan.masquerade");
		    m->addParam("id",m_peer1);
		    m->addParam("message","call.execute");
		    m->addParam("callto",m_room);
		    Engine::enqueue(m);
		    // also set held peer's PBX state if it has one
		    m = new Message("chan.operation");
		    m->addParam("operation","setstate");
		    m->addParam("id",m_peer1);
		    m->addParam("state","conference");
		    m->addParam("room",m_room);
		    m->addParam("pbxstate",m_state);
		    Engine::enqueue(m);
		    // no longer holding it
		    m_peer1.clear();
		}
	    }
	}
	else 
	{
	    Channel* c = static_cast<Channel*>(msg.userObject("Channel"));
	    if (!c)
		return errorBeep();
	    if (!room)
		return errorBeep();
	    m_room = room;
	    Message *m = c->message("call.execute",false,true);
	    m->addParam("callto",room);
	    m->addParam("pbxstate",m_state);
	    Engine::enqueue(m);
	}

	m_state="conference";
	if (peer) {
	    // set the peer's PBX state and room name
	    Message *m=new Message("chan.operation");
	    m->addParam("operation","setstate");
	    m->addParam("id",peer);
	    m->addParam("state","conference");
	    m->addParam("room",m_room);
	    m->addParam("pbxstate",m_state);
	    Engine::enqueue(m);
	}
	return true;
    }

    else if (operation == "secondcall") {
	// make another call, disconnect peer
	Message m("call.preroute");
	m.addParam("id",id());
	m.addParam("called",msg.getValue("target"));
	m.addParam("pbxstate",m_state);
	// no error check as handling preroute is optional
	Engine::dispatch(m);
	m = "call.route";
	if (!Engine::dispatch(m))
	    return errorBeep();
	if (m.retValue().null())
	    return errorBeep();
	m_state="call";
	String peer = m.retValue();
	Message *m2=new Message("chan.masquerade");
	m2->addParam("message","call.execute");
	m2->addParam("id",id());
	m2->addParam("callto",peer);
	m2->addParam("pbxstate",m_state);
	Engine::enqueue(m2);
	return true;
    }

    else if (operation == "onhold") {
	// put the peer on hold and connect to old held party or a dialtone
	if (m_state=="conference")
	    return errorBeep();
	RefPointer<CallEndpoint> c = locate();
	if (!c)
	    return errorBeep();
	RefPointer<CallEndpoint> c2 = locate(m_peer1);
	// no need to check old m_peer1
	if (m_state=="dial")
	    m_peer1.clear();
	else
	    m_peer1 = c->getPeerId();

	Message* m;
	if (c2) {
	    m = new Message("chan.operation");
	    m->addParam("operation","setstate");
	    m->addParam("id",c2->id());
	    m->addParam("state","call");
	    m_state="call";
	    c->connect(c2,"hold");
	}
	else {
	    m = new Message("chan.masquerade");
	    m->addParam("id",id());
	    m->addParam("callto","tone/dial");
	    m->addParam("message","call.execute");
	    m->addParam("reason","hold");
	    m_state="dial";
	}
	m->addParam("pbxstate",m_state);
	Engine::enqueue(m);
	return true;
    }

    else if (operation == "returnhold") {
	// return to a peer left on hold, hang up the current peer
	RefPointer<CallEndpoint> c1 = locate(id());
	RefPointer<CallEndpoint> c2 = locate(m_peer1);
	if (!(c1 && c2))
	    return errorBeep();
	m_peer1.clear();
	m_state="call";
	c1->connect(c2);
	return true;
    }

    else if (operation == "returnconf") {
	// return to the conference
	if ((m_state=="conference") || (m_state=="new") || m_room.null())
	    return errorBeep();
	Channel* c = static_cast<Channel*>(msg.userObject("Channel"));
	if (!c)
	    return errorBeep();
	m_state="conference";
	Message *m= c->message("call.execute",false,true);
	m->addParam("callto",m_room);
	m->addParam("pbxstate",m_state);
	Engine::enqueue(m);
	return true;
    }

    else if (operation == "returntone") {
	// return to a dialtone, hangup the peer if any
	m_state="dial";
	Message *m= new Message("chan.masquerade");
	m->addParam("id",id());
	m->addParam("callto","tone/dial");
	m->addParam("message","call.execute");
	m->addParam("pbxstate",m_state);
	Engine::enqueue(m);
	return true;
    }

    else if (operation == "dotransfer") {
	// connect our current peer to what we have on hold, hangup this channel
	if (m_peer1.null() || ((m_state != "call") && (m_state != "fortransfer")))
	    return errorBeep();
	RefPointer<CallEndpoint> c1 = locate();
	if (!c1)
	    return errorBeep();
	c1 = locate(c1->getPeerId());
	RefPointer<CallEndpoint>c2 = locate(m_peer1);
	if (!(c1 && c2))
	    return errorBeep();
	m_state=msg.getValue("state","hangup");
	m_peer1.clear();
	c1->connect(c2);
    }

    else if (operation == "transfer") {
	// unassisted transfer
	if ((m_state=="conference") || (m_state=="dial"))
	    return errorBeep();
	RefPointer<CallEndpoint> c = locate();
	if (!c)
	    return errorBeep();
	String peer = c->getPeerId();

	Message m1("call.preroute");
	m1.addParam("id",id());
	m1.addParam("called",msg.getValue("target"));
	m1.addParam("pbxstate",m_state);
	// no error check as handling preroute is optional
	Engine::dispatch(m1);
	m1 = "call.route";
	if (!Engine::dispatch(m1))
	    return errorBeep();
	if (m1.retValue().null())
	    return errorBeep();
	String callto = m1.retValue();
	m_state="dial";
	Message *m2=new Message("chan.masquerade");
	m2->addParam("id",peer);
	m2->addParam("message","call.execute");
	m2->addParam("callto",callto);
	m2->addParam("pbxstate",m_state);
	Engine::enqueue(m2);
	return true;
    }

    else if (operation == "fortransfer") {
	// enter assisted transfer - hold peer and dial another number
	if (m_state=="conference")
	    return errorBeep();
	RefPointer<CallEndpoint> c = locate();
	if (!c)
	    return errorBeep();
	String peer;
	if (m_state!="dial")
	    peer = c->getPeerId();
	if (peer) {
	    // check if we already have another party on hold
	    if (m_peer1 && (m_peer1 != peer))
		return errorBeep();
	    m_peer1 = peer;
	}
	Message m1("call.preroute");
	m1.addParam("id",id());
	m1.addParam("called",msg.getValue("target"));
	m1.addParam("pbxstate",m_state);
	// no error check as handling preroute is optional
	Engine::dispatch(m1);
	m1 = "call.route";
	if (!Engine::dispatch(m1))
	    return errorBeep();
	if (m1.retValue().null())
	    return errorBeep();
	String callto = m1.retValue();
	m_state="fortransfer";
	Message *m2=new Message("chan.masquerade");
	m2->addParam("message","call.execute");
	m2->addParam("id",id());
	m2->addParam("callto",callto);
	m2->addParam("reason","hold");
	m2->addParam("pbxstate",m_state);
	Engine::enqueue(m2);
	return true;
    }

    return false;
}

void PBXAssist::msgHangup(Message& msg)
{
    if (m_peer1) {
	// hangup anyone we have on hold
	Message *m=new Message("call.drop");
	m->addParam("id",m_peer1);
	m->addParam("pbxstate",m_state);
	Engine::enqueue(m);
    }
    ChanAssist::msgHangup(msg);
}

INIT_PLUGIN(PBXList);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
