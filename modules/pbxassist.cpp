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
    inline PBXAssist(ChanAssistList* list, const String& id, bool pass, bool guest)
	: ChanAssist(list,id), m_last(0), m_pass(pass), m_guest(guest),
	  m_state("new"), m_keep("")
	{ Debug(list,DebugCall,"Created%s assistant for '%s'",guest ? " guest" : "",id.c_str()); }
    virtual void msgStartup(Message& msg);
    virtual void msgHangup(Message& msg);
    virtual void msgExecute(Message& msg);
    virtual bool msgDisconnect(Message& msg, const String& reason);
    virtual bool msgTone(Message& msg);
    virtual bool msgOperation(Message& msg, const String& operation);
protected:
    bool errorBeep(const char* source = 0);
    void setGuest(const Message& msg);
    void setParams(const Message& msg);
    u_int64_t m_last;
    bool m_pass;
    bool m_guest;
    String m_tones;
    String m_peer1;
    String m_room;
    String m_state;
    NamedList m_keep;
private:
    // operation handlers
    bool operSetState(Message& msg);
    bool operPassThrough(Message& msg);
    bool operConference(Message& msg);
    bool operSecondCall(Message& msg);
    bool operOnHold(Message& msg);
    bool operReturnHold(Message& msg);
    bool operReturnConf(Message& msg);
    bool operReturnTone(Message& msg);
    bool operTransfer(Message& msg);
    bool operDoTransfer(Message& msg);
    bool operForTransfer(Message& msg);
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

// by default create assistant on chan.startup of incoming calls?
static bool s_incoming = true;

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

// Utility function - copy parameters requested in "copyparams" parameter
static void copyParams(NamedList& dest, const NamedList& original, const NamedList* pbxkeep = 0)
{
    const String* params = original.getParam("copyparams");
    if (params && *params)
	dest.copyParams(original,*params);
    if (pbxkeep) {
	params = original.getParam("pbxparams");
	if (params && *params)
	    dest.copyParams(*pbxkeep,*params);
    }
}


ChanAssist* PBXList::create(Message& msg, const String& id)
{
    if (msg == "chan.startup" || msg.userObject("Channel")) {
	// if a filter is set try to match it
	if (s_filter && !s_filter.matches(id))
	    return 0;
	// allow routing to enable/disable assistance
	if (msg.getBoolValue("pbxassist",s_assist)) {
	    DDebug(this,DebugCall,"Creating assistant for '%s'",id.c_str());
	    bool guest = false;
	    if (!s_incoming) {
		const NamedString* status = msg.getParam("status");
		if (status && (*status == "incoming"))
		    guest = true;
	    }
	    return new PBXAssist(this,id,msg.getBoolValue("dtmfpass",s_pass),msg.getBoolValue("pbxguest",guest));
	}
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
    s_assist = s_cfg.getBoolValue("general","default",true);
    s_incoming = s_cfg.getBoolValue("general","incoming",true);
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
    if (s_cfg.getBoolValue("general","enabled",false))
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

    if (reason) {
	// we have a reason, see if we should divert the call
	String called = m_keep.getValue("divert_"+reason);
	if (called && (called != m_keep.getValue("called"))) {
	    Message* m = new Message("call.preroute");
	    m->addParam("id",id());
	    m->addParam("reason",reason);
	    m->addParam("pbxstate",m_state);
	    m->copyParam(m_keep,"billid");
	    m->copyParam(m_keep,"caller");
	    copyParams(*m,msg,&m_keep);
	    if (isE164(called)) {
		// divert target is a number so we have to route it
		m->addParam("called",called);
		Engine::dispatch(m);
		*m = "call.route";
		if (!Engine::dispatch(m) || m->retValue().null() || (m->retValue() == "-") || (m->retValue() == "error")) {
		    // routing failed
		    TelEngine::destruct(m);
		    return errorBeep();
		}
		called = m->retValue();
		m->retValue().clear();
		m->msgTime() = Time::now();
	    }
	    else {
		// diverting to resource, add old called for reference
		m->copyParam(m_keep,"called");
	    }
	    Debug(list(),DebugCall,"Chan '%s' divert on '%s' to '%s'",
		id().c_str(),reason.c_str(),called.c_str());
	    *m = "chan.masquerade";
	    m->setParam("id",id());
	    m->setParam("message","call.execute");
	    m->setParam("callto",called);
	    m->setParam("reason","divert_"+reason);
	    m->userData(msg.userData());
	    Engine::enqueue(m);
	    return true;
	}
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
    if (m_guest)
	return false;
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
	    errorBeep();
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

	// first start any desired beeps or prompts
	tmp = sect->getValue("pbxprompt");
	if (tmp) {
	    // * -> default error beep
	    if (tmp[0] == '*')
		tmp = 0;
	    errorBeep(tmp);
	}

	// we may paste a string instead of just clearing the key buffer
	String newTones = sect->getValue("pastekeys");
	if (newTones) {
	    newTones = m_tones.replaceMatches(newTones);
	    msg.replaceParams(newTones);
	}
	tmp = sect->getValue("operation",*sect);
	if (tmp) {
	    Debug(list(),DebugNote,"Chan '%s' triggered operation '%s' in state '%s' holding '%s'",
		id().c_str(),tmp,m_state.c_str(),m_peer1.c_str());
	    // we need special handling for transparent passing of keys
	    if (String(tmp) == "transparent") {
		String keys = sect->getValue("text");
		if (keys) {
		    keys = m_tones.replaceMatches(keys);
		    msg.replaceParams(keys);
		    msg.setParam("text",keys);
		}
		m_tones = newTones;
		// let the DTMF message pass through
		return false;
	    }
	    Message* m = new Message("chan.masquerade");
	    m->addParam("id",id());
	    m->addParam("message",sect->getValue("message","chan.operation"));
	    m->addParam("operation",tmp);
	    unsigned int len = sect->length();
	    for (unsigned int idx = 0; idx < len; idx++) {
		const NamedString* s = sect->getParam(idx);
		if ((s->name() == "trigger") ||
		    (s->name() == "pastekeys") ||
		    (s->name() == "pbxstates") ||
		    (s->name() == "operation") ||
		    (s->name() == "pbxprompt") ||
		    (s->name() == "message"))
		    continue;
		String val = m_tones.replaceMatches(*s);
		msg.replaceParams(val);
		m->setParam(s->name(),val);
	    }
	    m->setParam("pbxstate",m_state);
	    Engine::enqueue(m);
	}
	m_tones = newTones;
    }
    // swallow the tone
    return true;
}

// Start an error beep and return false so we can "return errorBeep()"
bool PBXAssist::errorBeep(const char* source)
{
    if (null(source))
	source = s_error;
    if (!source)
	return false;
    Message* m = new Message("chan.masquerade");
    m->addParam("message","chan.attach");
    m->addParam("id",id());
    m->addParam("pbxstate",m_state);
    m->addParam("override",source);
    m->addParam("single","yes");
    Engine::enqueue(m);
    return false;
}

// Change the guest status according to message parameters
void PBXAssist::setGuest(const Message& msg)
{
    bool guest = msg.getBoolValue("pbxguest",m_guest);
    if (guest != m_guest) {
	Debug(list(),DebugNote,"Chan '%s' %s guest mode",id().c_str(),guest ? "entering" : "leaving");
	m_guest = guest;
    }
}

// Copy extra PBX parameters from message
void PBXAssist::setParams(const Message& msg)
{
    const String* params = msg.getParam("pbxparams");
    if (params && *params)
	m_keep.copyParams(msg,*params);
}

// Operation message handler - calls one of the operation handlers
bool PBXAssist::msgOperation(Message& msg, const String& operation)
{
    if (operation == "setstate")
	return operSetState(msg);

    if (operation == "passthrough")
	return operPassThrough(msg);

    if (operation == "conference")
	return operConference(msg);

    if (operation == "secondcall")
	return operSecondCall(msg);

    if (operation == "onhold")
	return operOnHold(msg);

    if (operation == "returnhold")
	return operReturnHold(msg);

    if (operation == "returnconf")
	return operReturnConf(msg);

    if (operation == "returntone")
	return operReturnTone(msg);

    if (operation == "transfer")
	return operTransfer(msg);

    if (operation == "dotransfer")
	return operDoTransfer(msg);

    if (operation == "fortransfer")
	return operForTransfer(msg);

    return false;
}

// Set the current state and conference room
bool PBXAssist::operSetState(Message& msg)
{
    m_state = msg.getValue("state",m_state);
    m_room = msg.getValue("room",m_room);
    setGuest(msg);
    setParams(msg);
    return true;
}

// Enter DTMF pass-through to peer mode, only look for retake string
bool PBXAssist::operPassThrough(Message& msg)
{
    if (s_retake.null()) {
	Debug(list(),DebugWarn,"Chan '%s' refusing pass-through, retake string is not set!",id().c_str());
	errorBeep();
	return true;
    }
    Debug(list(),DebugCall,"Chan '%s' entering tone pass-through mode",id().c_str());
    m_pass = true;
    m_tones.clear();
    // don't change state as we have a special variable for this mode
    return true;
}

// Turn the call into a conference or connect back to one it left
bool PBXAssist::operConference(Message& msg)
{
    if (m_state == "conference")
	return errorBeep();
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep();
    String peer = c->getPeerId();
    const char* room = msg.getValue("room",m_room);

    if (peer && !peer.startsWith("tone")) {
	Message m("call.conference");
	m.addParam("id",id());
	m.userData(c);
	m.addParam("lonely","yes");
	if (room)
	    m.addParam("room",room);
	m.addParam("pbxstate",m_state);
	copyParams(m,msg);

	if (Engine::dispatch(m) && m.userData()) {
	    m_room = m.getParam("room");
	    if (m_peer1 && (m_peer1 != peer)) {
		// take the held party in the conference
		Message* m2 = new Message("chan.masquerade");
		m2->addParam("id",m_peer1);
		m2->addParam("message","call.execute");
		m2->addParam("callto",m_room);
		copyParams(*m2,msg);
		Engine::enqueue(m2);
		// also set held peer's PBX state if it has one
		m2 = new Message("chan.operation");
		m2->addParam("operation","setstate");
		m2->addParam("id",m_peer1);
		m2->addParam("state","conference");
		m2->addParam("room",m_room);
		m2->addParam("pbxstate",m_state);
		Engine::enqueue(m2);
		// no longer holding it
		m_peer1.clear();
	    }
	}
    }
    else {
	Channel* c = static_cast<Channel*>(msg.userObject("Channel"));
	if (!c)
	    return errorBeep();
	if (!room)
	    return errorBeep();
	m_room = room;
	Message* m = c->message("call.execute",false,true);
	m->addParam("callto",room);
	m->addParam("pbxstate",m_state);
	copyParams(*m,msg);
	Engine::enqueue(m);
    }

    m_state = "conference";
    if (peer) {
	// set the peer's PBX state and room name
	Message* m = new Message("chan.operation");
	m->addParam("operation","setstate");
	m->addParam("id",peer);
	m->addParam("state","conference");
	m->addParam("room",m_room);
	m->addParam("pbxstate",m_state);
	Engine::enqueue(m);
    }
    return true;
}

// Make another call, disconnect current peer
bool PBXAssist::operSecondCall(Message& msg)
{
    Message* m = new Message("call.preroute");
    m->addParam("id",id());
    m->copyParam(m_keep,"billid");
    m->copyParam(m_keep,"caller");
    m->addParam("called",msg.getValue("target"));
    m->addParam("pbxstate",m_state);
    copyParams(*m,msg,&m_keep);
    // no error check as handling preroute is optional
    Engine::dispatch(m);
    *m = "call.route";
    if (!Engine::dispatch(m) || m->retValue().null() || (m->retValue() == "-") || (m->retValue() == "error")) {
	TelEngine::destruct(m);
	return errorBeep();
    }
    m_state = "call";
    *m = "chan.masquerade";
    m->setParam("message","call.execute");
    m_keep.setParam("called",msg.getValue("target"));
    m->setParam("callto",m->retValue());
    m->retValue().clear();
    Engine::enqueue(m);
    return true;
}

// Put the peer on hold and connect to old held party or a dialtone
bool PBXAssist::operOnHold(Message& msg)
{
    if (m_state == "conference")
	return errorBeep();
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep();
    RefPointer<CallEndpoint> c2 = locate(m_peer1);
    // no need to check old m_peer1
    if (m_state == "dial")
	m_peer1.clear();
    else
	m_peer1 = c->getPeerId();

    Message* m;
    if (c2) {
	m = new Message("chan.operation");
	m->addParam("operation","setstate");
	m->addParam("id",c2->id());
	m->addParam("state","call");
	m_state = "call";
	c->connect(c2,"hold");
    }
    else {
	m = new Message("chan.masquerade");
	m->addParam("id",id());
	m->addParam("callto","tone/dial");
	m->addParam("message","call.execute");
	m->addParam("reason","hold");
	copyParams(*m,msg);
	m_state = "dial";
    }
    m->addParam("pbxstate",m_state);
    Engine::enqueue(m);
    return true;
}

// Return to a peer left on hold, hang up the current peer
bool PBXAssist::operReturnHold(Message& msg)
{
    RefPointer<CallEndpoint> c1 = locate(id());
    RefPointer<CallEndpoint> c2 = locate(m_peer1);
    if (!(c1 && c2))
	return errorBeep();
    m_peer1.clear();
    m_state = "call";
    c1->connect(c2);
    return true;
}

// Return to the conference room
bool PBXAssist::operReturnConf(Message& msg)
{
    if ((m_state == "conference") || (m_state == "new") || m_room.null())
	return errorBeep();
    Channel* c = static_cast<Channel*>(msg.userObject("Channel"));
    if (!c)
	return errorBeep();
    m_state = "conference";
    Message* m = c->message("call.execute",false,true);
    m->addParam("callto",m_room);
    m->addParam("pbxstate",m_state);
    copyParams(*m,msg);
    Engine::enqueue(m);
    return true;
}

// Return to a dialtone, hangup the peer if any
bool PBXAssist::operReturnTone(Message& msg)
{
    m_state = "dial";
    Message* m = new Message("chan.masquerade");
    m->addParam("id",id());
    m->addParam("callto","tone/dial");
    m->addParam("message","call.execute");
    m->addParam("pbxstate",m_state);
    copyParams(*m,msg);
    Engine::enqueue(m);
    return true;
}

// Unassisted transfer operation
bool PBXAssist::operTransfer(Message& msg)
{
    if ((m_state == "conference") || (m_state == "dial"))
	return errorBeep();
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep();
    String peer = c->getPeerId();

    Message* m = new Message("call.preroute");
    m->addParam("id",peer);
    // make call appear as from the other party
    m->addParam("caller",m_keep.getValue("called"));
    m->addParam("called",msg.getValue("target"));
    m->addParam("pbxstate",m_state);
    copyParams(*m,msg,&m_keep);
    // no error check as handling preroute is optional
    Engine::dispatch(m);
    *m = "call.route";
    if (!Engine::dispatch(m) || m->retValue().null() || (m->retValue() == "-") || (m->retValue() == "error")) {
	TelEngine::destruct(m);
	return errorBeep();
    }
    m_state = "dial";
    *m = "chan.masquerade";
    m->setParam("message","call.execute");
    m->setParam("callto",m->retValue());
    m->retValue().clear();
    Engine::enqueue(m);
    return true;
}

// Connect our current peer to what we have on hold, hangup this channel
bool PBXAssist::operDoTransfer(Message& msg)
{
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
    return true;
}

// Enter assisted transfer - hold peer and dial another number
bool PBXAssist::operForTransfer(Message& msg)
{
    if (m_state == "conference")
	return errorBeep();
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep();
    String peer;
    if (m_state != "dial")
	peer = c->getPeerId();
    if (peer) {
	// check if we already have another party on hold
	if (m_peer1 && (m_peer1 != peer))
	    return errorBeep();
	m_peer1 = peer;
    }
    Message* m = new Message("call.preroute");
    m->addParam("id",id());
    m->copyParam(m_keep,"billid");
    m->copyParam(m_keep,"caller");
    m->addParam("called",msg.getValue("target"));
    m->addParam("pbxstate",m_state);
    copyParams(*m,msg,&m_keep);
    // no error check as handling preroute is optional
    Engine::dispatch(m);
    *m = "call.route";
    if (!Engine::dispatch(m) || m->retValue().null() || (m->retValue() == "-") || (m->retValue() == "error")) {
	TelEngine::destruct(m);
	return errorBeep();
    }
    m_state = "fortransfer";
    *m = "chan.masquerade";
    m->setParam("message","call.execute");
    m->setParam("reason","hold");
    m->setParam("callto",m->retValue());
    m->retValue().clear();
    Engine::enqueue(m);
    return true;
}

// Hangup handler, do any cleanups needed
void PBXAssist::msgHangup(Message& msg)
{
    if (m_peer1) {
	// hangup anyone we have on hold
	Message* m = new Message("call.drop");
	m->addParam("id",m_peer1);
	m->addParam("pbxstate",m_state);
	Engine::enqueue(m);
    }
    ChanAssist::msgHangup(msg);
}

// Startup handler, copy call information we may need later
void PBXAssist::msgStartup(Message& msg)
{
    DDebug(list(),DebugNote,"Copying startup parameters for '%s'",id().c_str());
    setGuest(msg);
    setParams(msg);
    m_keep.setParam("billid",msg.getValue("billid"));
    NamedString* status = msg.getParam("status");
    if (status && (*status == "outgoing")) {
	// switch them over so we have them right for later operations
	m_keep.setParam("caller",msg.getValue("called"));
	m_keep.setParam("called",msg.getValue("caller"));
    }
    else {
	m_keep.setParam("called",msg.getValue("called"));
	m_keep.setParam("caller",msg.getValue("caller"));
    }
}

// Execute message handler, copy call and divert information we may need later
void PBXAssist::msgExecute(Message& msg)
{
    DDebug(list(),DebugNote,"Copying execute parameters for '%s'",id().c_str());
    // this gets only called on incoming call legs
    setGuest(msg);
    setParams(msg);
    m_keep.setParam("billid",msg.getValue("billid"));
    m_keep.setParam("called",msg.getValue("called"));
    m_keep.setParam("caller",msg.getValue("caller"));
    m_keep.copyParam(msg,"divert",'_');
}

INIT_PLUGIN(PBXList);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
