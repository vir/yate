/**
 * pbxassist.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * PBX assist module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Author: Monica Tepelus
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

#include <yatepbx.h>


using namespace TelEngine;
namespace { //anonymous

class YPBX_API PBXAssist : public ChanAssist
{
public:
    inline PBXAssist(ChanAssistList* list, const String& id, bool pass, bool guest)
	: ChanAssist(list,id),
	  m_last(0), m_pass(pass), m_guest(guest), m_first(true),
	  m_state("new"), m_keep("")
	{ Debug(list,DebugCall,"Created%s assistant for '%s'",guest ? " guest" : "",id.c_str()); }
    virtual void msgStartup(Message& msg);
    virtual void msgHangup(Message& msg);
    virtual void msgExecute(Message& msg);
    virtual bool msgDisconnect(Message& msg, const String& reason);
    virtual bool msgTone(Message& msg);
    virtual bool msgOperation(Message& msg, const String& operation);
    void chanReplaced(const String& initial, const String& final);
    inline void statusDetail(String& str) const
	{ str.append(id() + "=" + m_state + "|" + m_tones,","); }
protected:
    inline const String& state() const
	{ return m_state; }
    void setState(const String& newState);
    void defState();
    void putPrompt(const char* source = 0, const char* reason = 0);
    bool errorBeep(const char* reason = 0);
    void setGuest(const Message& msg);
    void setParams(const Message& msg);
    void copyParameter(const NamedList& params, const char* dest, const char* src = 0);
    bool cancelTransfer(const String& chId) const;
    bool cancelTransfer() const;
    u_int64_t m_last;
    bool m_pass;
    bool m_guest;
    bool m_first;
    String m_tones;
    String m_peer1;
    String m_room;
    String m_state;
    NamedList m_keep;
private:
    // operation handlers
    bool operSetState(Message& msg, const char* newState = 0);
    bool operPassThrough(Message& msg);
    bool operConference(Message& msg);
    bool operSecondCall(Message& msg);
    bool operOnHold(Message& msg);
    bool operReturnHold(Message& msg);
    bool operReturnConf(Message& msg);
    bool operReturnTone(Message& msg, const char* reason = 0);
    bool operDialTone(Message& msg);
    bool operTransfer(Message& msg);
    bool operDoTransfer(Message& msg);
    bool operForTransfer(Message& msg);
    // post conference operations
    void postConference(Message& msg, int users, bool created);
    void postConference(Message& msg, const String& name, int users);
};

class YPBX_API PBXList : public ChanAssistList
{
public:
    enum {
	Operation = AssistPrivate,
	Replaced,
    };
    inline PBXList()
	: ChanAssistList("pbxassist")
	{ }
    ~PBXList();
    virtual ChanAssist* create(Message& msg, const String& id);
    virtual void initialize();
    virtual void init(int priority);
    virtual bool received(Message& msg, int id);
    virtual bool received(Message& msg, int id, ChanAssist* assist);
protected:
    void statusModule(String& str);
    void statusParams(String& str);
    void statusDetail(String& str);
private:
    void chanReplaced(const NamedList& params);
    void chanReplaced(const String& initial, const String& final);
};

// assist all channels by default?
static bool s_assist = true;

// by default create assistant on chan.startup of incoming calls?
static bool s_incoming = true;

// filter for channel names to enable assistance
static Regexp s_filter;

// filter mode: fail creation if filter does not match or reverse
static bool s_filterFail = false;

// DTMF pass-through all channels by default?
static bool s_pass = false;

// redial from calls that are left on hold or drop them?
static bool s_dialHeld = false;

// allow diversion requested through protocol
static bool s_divProto = false;

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

// Language parameter for tones played by the pbx
static String s_lang;

// Drop conference on hangup
static bool s_dropConfHangup = true;

// Make channels own the conference rooms
static bool s_confOwner = false;

// Conference lonely timeout
static int s_lonely = 0;

// on-hangup transfer list
static ObjList s_transList;
static Mutex s_transMutex(false,"PBXAssist::transfer");

static Configuration s_cfg;

// Utility function - copy parameters requested in "copyparams" parameter
static void copyParams(NamedList& dest, const NamedList& original, const NamedList* pbxkeep = 0)
{
    const String* params = original.getParam("copyparams");
    if (params && *params)
	dest.copyParams(original,*params);
    if (pbxkeep) {
	params = original.getParam("pbxparams");
	if (!params)
	    params = pbxkeep->getParam("copyparams");
	if (params && *params)
	    dest.copyParams(*pbxkeep,*params);
    }
}


PBXList::~PBXList()
{
    s_transMutex.lock();
    unsigned int n = s_transList.count();
    s_transMutex.unlock();
    if (n)
	Debug(this,DebugWarn,"There are %u unfinished transfers in list!",n);
}

ChanAssist* PBXList::create(Message& msg, const String& id)
{
    if (msg == "chan.startup" || msg.userObject(YATOM("Channel"))) {
	// if a filter is set try to match it
	if (s_filter && (s_filterFail == s_filter.matches(id)))
	    return 0;
	// allow routing to enable/disable assistance
	if (msg.getBoolValue("pbxassist",s_assist)) {
	    DDebug(this,DebugCall,"Creating assistant for '%s'",id.c_str());
	    bool guest = false;
	    if (!s_incoming) {
		const NamedString* dir = msg.getParam("direction");
		if (TelEngine::null(dir))
		    dir = msg.getParam("status");
		if (dir && (*dir == "incoming"))
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
    Engine::install(new MessageRelay("chan.operation",this,Operation,priority,name()));
    Engine::install(new MessageRelay("chan.replaced",this,Replaced,priority,name()));
}

void PBXList::initialize()
{
    lock();
    s_cfg = Engine::configFile(name());
    s_cfg.load();
    s_assist = s_cfg.getBoolValue("general","default",true);
    s_incoming = s_cfg.getBoolValue("general","incoming",true);
    s_filter = s_cfg.getValue("general","filter");
    if (s_filter.endsWith("^")) {
	// reverse match on final ^ (makes no sense in a regexp)
	s_filter = s_filter.substr(0,s_filter.length()-1);
	s_filterFail = true;
    }
    else
	s_filterFail = false;
    s_pass = s_cfg.getBoolValue("general","dtmfpass",false);
    s_dialHeld = s_cfg.getBoolValue("general","dialheld",false);
    s_divProto = s_cfg.getBoolValue("general","diversion",false);
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
    s_lang = s_cfg.getValue("general","lang");
    s_dropConfHangup = s_cfg.getBoolValue("general","dropconfhangup",true);
    s_confOwner = s_cfg.getBoolValue("general","confowner",false);
    s_lonely = s_cfg.getIntValue("general","lonelytimeout");
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

// Handler for all messages
bool PBXList::received(Message& msg, int id)
{
    if (Replaced == id) {
	chanReplaced(msg);
	return false;
    }
    return ChanAssistList::received(msg,id);
}

// Handler for chan.replaced
void PBXList::chanReplaced(const NamedList& params)
{
    for (unsigned int i = 0; ; i++) {
	String id("id");
	String newId("newid");
	if (i) {
	    id << "." << i;
	    newId << "." << i;
	}
	const String* initial = params.getParam(id);
	if (!initial)
	    break;
	const String* final = params.getParam(newId);
	if (!final)
	    break;
	if (*initial && *final)
	    chanReplaced(*initial,*final);
    }
}

// Operate single channel ID replacement
void PBXList::chanReplaced(const String& initial, const String& final)
{
    DDebug(this,DebugAll,"Replacing '%s' with '%s'",initial.c_str(),final.c_str());
    // replace in transfer list, test both sides
    s_transMutex.lock();
    for (ObjList* l = s_transList.skipNull(); l; l = l->skipNext()) {
	NamedString* n = static_cast<NamedString*>(l->get());
	if (initial == n->name()) {
	    Debug(this,DebugInfo,"In transfer '%s' replaced '%s' with '%s'",
		n->c_str(),initial.c_str(),final.c_str());
	    const_cast<String&>(n->name()) = final;
	}
	if (initial == *n) {
	    Debug(this,DebugInfo,"In transfer '%s' replaced '%s' with '%s'",
		n->name().c_str(),initial.c_str(),final.c_str());
	    *n = final;
	}
    }
    s_transMutex.unlock();
    // replace in assists
    lock();
    ListIterator iter(calls());
    while (PBXAssist* assist = static_cast<PBXAssist*>(iter.get()))
	assist->chanReplaced(initial,final);
    unlock();
}

void PBXList::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=State|Keys",",");
}

void PBXList::statusParams(String& str)
{
    Module::statusParams(str);
    lock();
    str.append("assisted="+String(calls().count()),",");
    unlock();
    str << ",incoming=" << String::boolText(s_incoming);
    str << ",dtmfpass=" << String::boolText(s_pass);
    str << ",dialheld=" << String::boolText(s_dialHeld);
    str << ",diversion=" << String::boolText(s_divProto);
}

void PBXList::statusDetail(String& str)
{
    Module::statusDetail(str);
    lock();
    ListIterator iter(calls());
    while (const PBXAssist* assist = static_cast<const PBXAssist*>(iter.get()))
	assist->statusDetail(str);
    unlock();
}


// Change the state of the PBX assist
void PBXAssist::setState(const String& newState)
{
    if (newState.null() || (newState == m_state))
	return;
    DDebug(list(),DebugAll,"Chan '%s'%s changed state '%s' -> '%s'",
	id().c_str(),(m_guest ? " (guest)" : ""),m_state.c_str(),newState.c_str());
    m_state = newState;
}

void PBXAssist::defState()
{
    if (!locate(m_peer1))
	m_peer1.clear();
    setState(m_peer1 ? "call" : "new");
}

// Cancel a pending assisted transfer
bool PBXAssist::cancelTransfer(const String& chId) const
{
    Lock lock(s_transMutex);
    for (ObjList* l = s_transList.skipNull(); l; l = l->skipNext()) {
	NamedString* n = static_cast<NamedString*>(l->get());
	if ((chId && (chId == n->name() || chId == *n)) ||
	    (m_peer1 && (m_peer1 == n->name() || m_peer1 == *n))) {
	    // found one of the channels in list, remove entry
	    DDebug(list(),DebugInfo,"Chan '%s' cancelled transfer '%s' - '%s'",
		id().c_str(),n->name().c_str(),n->c_str());
	    l->remove();
	    return true;
	}
    }
    return false;
}

bool PBXAssist::cancelTransfer() const
{
    RefPointer<CallEndpoint> c = locate(id());
    return cancelTransfer(c ? c->getPeerId() : String::empty());
}

// Handler for channel disconnects, may go on hold or to dialtone
bool PBXAssist::msgDisconnect(Message& msg, const String& reason)
{
    Debug(list(),DebugInfo,"Chan '%s'%s disconnected in state '%s', reason '%s'",
	id().c_str(),(m_guest ? " (guest)" : ""),state().c_str(),reason.c_str());
    if (state() == "hangup")
	return ChanAssist::msgDisconnect(msg,reason);

    if ((reason == "hold") || (reason == "park") || (reason == "intrusion")) {
	String onhold = m_keep.getValue("onhold",s_onhold);
	if (onhold) {
	    Channel* c = static_cast<Channel*>(msg.userObject(YATOM("Channel")));
	    if (!c)
		return false;
	    Message *m = c->message("call.execute",false,true);
	    m->addParam("callto",onhold);
	    m->addParam("reason",reason);
	    m->addParam("pbxstate",state());
	    Engine::enqueue(m);
	}
	return false;
    }

    if (state() == "conference")
	defState();

    String transfer;
    s_transMutex.lock();
    for (ObjList* l = s_transList.skipNull(); l; l = l->skipNext()) {
	NamedString* n = static_cast<NamedString*>(l->get());
	if (id() == n->name()) {
	    transfer = *n;
	    l->remove();
	    break;
	}
	if (id() == *n) {
	    transfer = n->name();
	    l->remove();
	    break;
	}
    }
    s_transMutex.unlock();
    if (transfer) {
	CallEndpoint* c1 = static_cast<CallEndpoint*>(msg.userData());
	if (c1) {
	    RefPointer<CallEndpoint>c2 = locate(transfer);
	    if (c2) {
		defState();
		if (c1->connect(c2,"transfer")) {
		    Debug(list(),DebugNote,"Chan '%s' transferred to '%s'",
			id().c_str(),transfer.c_str());
		    Message* m = new Message("chan.operation");
		    m->addParam("operation","setstate");
		    m->addParam("id",transfer);
		    m->addParam("state","*");
		    Engine::enqueue(m);
		    return true;
		}
	    }
	}
	Debug(list(),DebugMild,"Failed to transfer chan '%s' to '%s'",
	    id().c_str(),transfer.c_str());
    }

    String divertReason = reason;
    String called;
    bool proto = s_divProto && msg.getBoolValue("redirect");
    if (proto) {
	// protocol requested redirect or diversion
	divertReason = msg.getValue("divert_reason");
	called = msg.getValue("called");
    }
    else if (reason) {
	// we have a disconnect reason, see if we should divert the call
	called = m_keep.getValue("divert_"+reason);
    }
    if (called && (called != m_keep.getValue("called"))) {
	Message* m = new Message("call.preroute");
	m->addParam("id",id());
	m->addParam("reason",divertReason);
	m->addParam("pbxstate",state());
	m->copyParam(m_keep,"billid");
	m->copyParam(m_keep,"caller");
	if (proto) {
	    m->copyParam(msg,"diverter");
	    m->addParam("divert_reason",divertReason);
	    m->copyParam(msg,"divert_privacy");
	    m->copyParam(msg,"divert_screen");
	}
	copyParams(*m,msg,&m_keep);
	if (isE164(called)) {
	    // divert target is a number so we have to route it
	    m->addParam("called",called);
	    Engine::dispatch(m);
	    *m = "call.route";
	    if (!Engine::dispatch(m) || m->retValue().null() || (m->retValue() == "-") || (m->retValue() == "error")) {
		// routing failed
		TelEngine::destruct(m);
		return errorBeep("no route");
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
	    id().c_str(),divertReason.c_str(),called.c_str());
	*m = "chan.masquerade";
	m->setParam("id",id());
	m->setParam("message","call.execute");
	m->setParam("callto",called);
	m->setParam("reason","divert_"+divertReason);
	m->userData(msg.userData());
	Engine::enqueue(m);
	return true;
    }

    if (!m_guest && (state() != "new")) {
	Channel* c = static_cast<Channel*>(msg.userObject(YATOM("Channel")));
	if (!c)
	    return false;
	Message *m = c->message("call.execute",false,true);
	m->addParam("callto","tone/dial");
	m->addParam("lang",m_keep.getValue("pbxlang",s_lang),false);
	m->addParam("reason","hold");
	m->addParam("pbxstate",state());
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
    Debug(list(),DebugInfo,"Chan '%s' got tone '%s' collected '%s' in state '%s'",
	id().c_str(),tone,m_tones.c_str(),state().c_str());
    if (m_pass) {
	if (m_tones.endsWith(s_retake)) {
	    Debug(list(),DebugCall,"Chan '%s' back in command hunt mode",id().c_str());
	    m_pass = false;
	    m_tones.clear();
	    putPrompt();
	    // we just preserve the last state we were in
	    return true;
	}
	return false;
    }
    if ((int)m_tones.length() < s_minlen)
	return true;

    Lock lock(list());
    bool first = m_first;
    m_first = false;
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
	    if (!(st.matches(state()) || (first && st.matches("first"))))
		continue;
	}

	// first start any desired beeps or prompts
	tmp = sect->getValue("pbxprompt");
	if (tmp) {
	    // * -> default error beep
	    if (tmp[0] == '*')
		tmp = 0;
	    putPrompt(tmp);
	}

	// if we're greedy prepare to exit the loop and skip all other sections
	if (sect->getBoolValue("pbxgreedy"))
	    i = n;

	// we may paste a string instead of just clearing the key buffer
	String newTones = sect->getValue("pastekeys");
	if (newTones) {
	    newTones = m_tones.replaceMatches(newTones);
	    msg.replaceParams(newTones);
	}
	tmp = sect->getValue("operation",*sect);
	if (tmp) {
	    Debug(list(),DebugNote,"Chan '%s' triggered operation '%s' in state '%s' holding '%s'",
		id().c_str(),tmp,state().c_str(),m_peer1.c_str());
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
		    (s->name() == "pbxgreedy") ||
		    (s->name() == "message"))
		    continue;
		String val = m_tones.replaceMatches(*s);
		msg.replaceParams(val);
		m->setParam(s->name(),val);
	    }
	    m->setParam("pbxstate",state());
	    Engine::enqueue(m);
	}
	m_tones = newTones;
    }
    // swallow the tone
    return true;
}

// Start an override prompt to the PBX user
void PBXAssist::putPrompt(const char* source, const char* reason)
{
    if (null(source))
	source = s_error;
    if (!source)
	return;
    Message* m = new Message("chan.masquerade");
    m->addParam("message","chan.attach");
    m->addParam("id",id());
    m->addParam("pbxstate",state());
    m->addParam("override",source);
    m->addParam("lang",m_keep.getValue("pbxlang",s_lang),false);
    m->addParam("single","yes");
    if (reason)
	m->addParam("reason",reason);
    Engine::enqueue(m);
}

// Start an error beep and return false so we can "return errorBeep()"
bool PBXAssist::errorBeep(const char* reason)
{
    if (reason)
	Debug(list(),DebugMild,"Chan '%s' operation failed: %s",id().c_str(),reason);
    putPrompt(0,reason);
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

// Copy one parameter to the keep with optional rename
void PBXAssist::copyParameter(const NamedList& params, const char* dest, const char* src)
{
    if (!src)
	src = dest;
    if (!(src && dest))
	return;
    const char* tmp = params.getValue(src);
    if (!tmp)
	return;
    XDebug(list(),DebugInfo,"For '%s' %s='%s'",id().c_str(),dest,tmp);
    m_keep.setParam(dest,tmp);
}

// Operation message handler - calls one of the operation handlers
bool PBXAssist::msgOperation(Message& msg, const String& operation)
{
    Debug(list(),DebugAll,"Chan '%s'%s executing '%s' in state '%s'",
	id().c_str(),(m_guest ? " (guest)" : ""),operation.c_str(),state().c_str());
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

    if (operation == "dialtone")
	return operDialTone(msg);

    if (operation == "transfer")
	return operTransfer(msg);

    if (operation == "dotransfer")
	return operDoTransfer(msg);

    if (operation == "fortransfer")
	return operForTransfer(msg);

    if (operation == "canceltransfer")
	return cancelTransfer();

    return false;
}

// Set the current state and conference room
bool PBXAssist::operSetState(Message& msg, const char* newState)
{
    String tmp = msg.getValue("state",newState);
    if (tmp == "*")
	defState();
    else
	setState(tmp);
    setGuest(msg);
    setParams(msg);
    const String* room = msg.getParam("room");
    if (room && (*room != m_room)) {
	m_room = *room;
	if (m_room && !m_guest && msg.getBoolValue("confowner",
	    m_keep.getBoolValue("pbxconfowner",s_confOwner))) {
	    Message m("call.conference");
	    m.addParam("id",id());
	    m.addParam("room",m_room);
	    m.addParam("pbxstate",state());
	    m.addParam("confowner",String::boolText(true));
	    copyParams(m,msg);
	    Engine::dispatch(m);
	}
    }
    return true;
}

// Enter DTMF pass-through to peer mode, only look for retake string
bool PBXAssist::operPassThrough(Message& msg)
{
    if (s_retake.null()) {
	Debug(list(),DebugWarn,"Chan '%s' refusing pass-through, retake string is not set!",id().c_str());
	errorBeep("no retake string");
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
    if (state() == "conference")
	return errorBeep("conference in conference");
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep("no channel");
    String peer = c->getPeerId();
    if (peer.startsWith("tone"))
	peer.clear();
    cancelTransfer(peer);
    const char* room = msg.getValue("room",m_room);
    int users = 0;
    bool created = false;
    bool owner = msg.getBoolValue("confowner",m_keep.getBoolValue("pbxconfowner",s_confOwner));

    if (peer) {
	Message m("call.conference");
	m.addParam("id",id());
	m.userData(c);
	m.addParam("lonely",m_keep.getValue("pbxlonelytimeout",String(s_lonely)));
	if (room)
	    m.addParam("room",room);
	m.addParam("pbxstate",state());
	m.addParam("confowner",String::boolText(owner));
	copyParams(m,msg);

	if (Engine::dispatch(m) && m.userData()) {
	    m_room = m.getParam("room");
	    users = m.getIntValue("users");
	    created = m.getBoolValue("newroom");
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
		m2->addParam("pbxstate",state());
		Engine::enqueue(m2);
		// no longer holding it
		m_peer1.clear();
		if (users > 0)
		    users++;
	    }
	}
	else
	    return errorBeep("conference failed");
    }
    else {
	Channel* c = static_cast<Channel*>(msg.userObject(YATOM("Channel")));
	if (!c)
	    return errorBeep("no channel");
	if (!room)
	    return errorBeep("no conference room");
	Message m("call.execute");
	m.userData(c);
	c->complete(m,false);
	m.addParam("callto",room);
	m.addParam("pbxstate",state());
	m.addParam("confowner",String::boolText(owner));
	copyParams(m,msg);
	if (!Engine::dispatch(m))
	    return errorBeep("conference failed");
	m_room = room;
	users = m.getIntValue("users");
	created = m.getBoolValue("newroom");
    }

    setState("conference");
    if (peer) {
	// set the peer's PBX state and room name
	Message* m = new Message("chan.operation");
	m->addParam("operation","setstate");
	m->addParam("id",peer);
	m->addParam("state","conference");
	m->addParam("room",m_room);
	m->addParam("pbxstate",state());
	Engine::enqueue(m);
    }
    postConference(msg,users,created);
    return true;
}

// Make another call, disconnect current peer
bool PBXAssist::operSecondCall(Message& msg)
{
    Message m("call.preroute");
    m.addParam("id",id());
    m.copyParam(m_keep,"billid");
    m.copyParam(m_keep,"caller");
    m.addParam("called",msg.getValue("target"));
    m.addParam("pbxstate",state());
    m.addParam("pbxoper",msg.getValue("operation"),false);
    m.addParam("reason",msg.getValue("reason"),false);
    copyParams(m,msg,&m_keep);
    // no error check as handling preroute is optional
    Engine::dispatch(m);
    m = "call.route";
    if (!Engine::dispatch(m) || m.retValue().null() || (m.retValue() == "-") || (m.retValue() == "error"))
	return errorBeep(m.getValue("reason",m.getValue("error","no route")));

    cancelTransfer();
    m = "chan.masquerade";
    m.setParam("message","call.execute");
    m_keep.setParam("called",msg.getValue("target"));
    m.setParam("callto",m.retValue());
    m.retValue().clear();
    if (Engine::dispatch(m)) {
	setState("call");
	return true;
    }
    return errorBeep(m.getValue("reason",m.getValue("error","call failed")));
}

// Put the peer on hold and connect to old held party or a dialtone
bool PBXAssist::operOnHold(Message& msg)
{
    if (state() == "conference")
	return errorBeep("hold in conference");
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep("no channel");
    RefPointer<CallEndpoint> c2 = locate(m_peer1);
    // no need to check old m_peer1
    if (state() == "dial") {
	m_peer1.clear();
	if (!c2)
	    return errorBeep("no call on hold");
    }
    else {
	m_peer1 = c->getPeerId();
	if (m_peer1.startsWith("tone"))
	    m_peer1.clear();
    }

    const char* reason = msg.getValue("reason","hold");
    Message* m;
    if (c2) {
	m = new Message("chan.operation");
	m->addParam("operation","setstate");
	m->addParam("id",c2->id());
	m->addParam("state","*");
	defState();
	c->connect(c2,reason);
    }
    else {
	m = new Message("chan.masquerade");
	m->addParam("id",id());
	m->addParam("callto","tone/dial");
	m->addParam("message","call.execute");
	m->addParam("lang",m_keep.getValue("pbxlang",s_lang),false);
	m->addParam("reason",reason,false);
	copyParams(*m,msg);
	setState("dial");
    }
    m->addParam("pbxstate",state());
    Engine::enqueue(m);
    return true;
}

// Return to a peer left on hold, hang up the current peer
bool PBXAssist::operReturnHold(Message& msg)
{
    RefPointer<CallEndpoint> c1 = locate(id());
    RefPointer<CallEndpoint> c2 = locate(m_peer1);
    if (!c2)
	m_peer1.clear();
    if (!(c1 && c2))
	return errorBeep("no held channel");
    cancelTransfer(c1->getPeerId());
    m_peer1.clear();
    defState();
    c1->connect(c2,msg.getValue("reason","pickup"));
    return true;
}

// Return to the conference room
bool PBXAssist::operReturnConf(Message& msg)
{
    if ((state() == "conference") || (state() == "new") || m_room.null())
	return errorBeep("cannot return to conference");
    Channel* c = static_cast<Channel*>(msg.userObject(YATOM("Channel")));
    if (!c)
	return errorBeep("no channel");
    bool owner = msg.getBoolValue("confowner",m_keep.getBoolValue("pbxconfowner",s_confOwner));
    Message m("call.execute");
    m.userData(c);
    c->complete(m,false);
    m.addParam("callto",m_room);
    m.addParam("pbxstate",state());
    m.addParam("confowner",String::boolText(owner));
    copyParams(m,msg);
    if (Engine::dispatch(m)) {
	setState("conference");
	postConference(msg,m.getIntValue("users"),m.getBoolValue("newroom"));
	return true;
    }
    return errorBeep(m.getValue("reason",m.getValue("error","conference failed")));
}

// Return to a dialtone, hangup the peer if any
bool PBXAssist::operReturnTone(Message& msg, const char* reason)
{
    cancelTransfer();
    setState(msg.getValue("state","dial"));
    reason = msg.getValue("reason",reason);
    Message* m = new Message("chan.masquerade");
    m->addParam("id",id());
    m->addParam("callto","tone/dial");
    m->addParam("lang",m_keep.getValue("pbxlang",s_lang),false);
    m->addParam("message","call.execute");
    m->addParam("pbxstate",state());
    if (reason)
	m->addParam("reason",reason);
    copyParams(*m,msg);
    Engine::enqueue(m);
    return true;
}

// Put the peer on hold and connect to a dialtone
bool PBXAssist::operDialTone(Message& msg)
{
    if (m_peer1)
	return errorBeep("having another party on hold");
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep("no channel");

    cancelTransfer(c->getPeerId());
    m_peer1 = c->getPeerId();
    if (m_peer1.startsWith("tone"))
	m_peer1.clear();

    return operReturnTone(msg,"hold");
}

// Unassisted transfer operation
bool PBXAssist::operTransfer(Message& msg)
{
    if ((state() == "conference") || (state() == "dial"))
	return errorBeep("cannot transfer blind");
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep("no channel");
    String peer = c->getPeerId();

    Message m("call.preroute");
    m.addParam("id",peer);
    // make call appear as from the other party
    m.addParam("caller",m_keep.getValue("called"));
    m.addParam("called",msg.getValue("target"));
    m.addParam("diverter",m_keep.getValue("caller"),false);
    m.addParam("pbxstate",state());
    m.addParam("pbxoper",msg.getValue("operation"),false);
    m.addParam("reason",msg.getValue("reason"),false);
    copyParams(m,msg,&m_keep);
    // no error check as handling preroute is optional
    Engine::dispatch(m);
    m = "call.route";
    if (!Engine::dispatch(m) || m.retValue().null() || (m.retValue() == "-") || (m.retValue() == "error"))
	return errorBeep(m.getValue("reason",m.getValue("error","no route")));
    cancelTransfer(peer);
    setState(msg.getValue("state","hangup"));
    m = "chan.masquerade";
    m.setParam("message","call.execute");
    m.setParam("callto",m.retValue());
    m.retValue().clear();
    if (Engine::dispatch(m))
	return true;
    defState();
    return errorBeep(m.getValue("reason",m.getValue("error","call failed")));
}

// Connect our current peer to what we have on hold, hangup this channel
bool PBXAssist::operDoTransfer(Message& msg)
{
    if (m_peer1.null() || (state() != "call"))
	return errorBeep("cannot transfer assisted");
    RefPointer<CallEndpoint> c1 = locate();
    if (!c1)
	return errorBeep("no channel");
    c1 = locate(c1->getPeerId());
    RefPointer<CallEndpoint>c2 = locate(m_peer1);
    if (!c2)
	m_peer1.clear();
    if (!(c1 && c2))
	return errorBeep("no held channel");
    cancelTransfer();
    setState(msg.getValue("state","hangup"));
    m_peer1.clear();
    c1->connect(c2,msg.getValue("reason","transfer"));
    return true;
}

// Enter assisted transfer - hold peer and dial another number
bool PBXAssist::operForTransfer(Message& msg)
{
    if (state() == "conference")
	return errorBeep("cannot transfer in conference");
    RefPointer<CallEndpoint> c = locate();
    if (!c)
	return errorBeep("no channel");
    String peer;
    if (state() != "dial") {
	peer = c->getPeerId();
	if (peer.startsWith("tone"))
	    peer.clear();
    }
    if (peer) {
	// check if we already have another party on hold
	if (m_peer1 && (m_peer1 != peer))
	    return errorBeep("having another party on hold");
    }
    Message m("call.preroute");
    m.addParam("id",id());
    m.copyParam(m_keep,"billid");
    m.copyParam(m_keep,"caller");
    m.addParam("called",msg.getValue("target"));
    m.addParam("pbxstate",state());
    m.addParam("pbxoper",msg.getValue("operation"),false);
    m.addParam("reason",msg.getValue("reason"),false);
    copyParams(m,msg,&m_keep);
    // no error check as handling preroute is optional
    Engine::dispatch(m);
    m = "call.route";
    if (!Engine::dispatch(m) || m.retValue().null() || (m.retValue() == "-") || (m.retValue() == "error"))
	return errorBeep(m.getValue("reason",m.getValue("error","no route")));
    if (peer)
	m_peer1 = peer;
    bool onHangup = m_peer1 && msg.getBoolValue("onhangup");
    defState();
    m = "chan.masquerade";
    m.setParam("message","call.execute");
    m.setParam("reason","hold");
    m.setParam("callto",m.retValue());
    m.retValue().clear();
    if (Engine::dispatch(m)) {
	if (onHangup) {
	    peer = c->getPeerId();
	    if (peer) {
		s_transMutex.lock();
		s_transList.append(new NamedString(peer,m_peer1));
		s_transMutex.unlock();
	    }
	}
	return true;
    }
    return errorBeep(m.getValue("reason",m.getValue("error","call failed")));
}

// Execute secondary operation after entering conference
void PBXAssist::postConference(Message& msg, int users, bool created)
{
    if (created) {
	postConference(msg,"opercreate",users);
	postConference(msg,"opercreate" + String(users),users);
    }
    if (users <= 0)
	return;
    postConference(msg,"operusers" + String(users),users);
}

// Try to execute a single secondary operation
void PBXAssist::postConference(Message& msg, const String& name, int users)
{
    const char* oper = msg.getValue(name,m_keep.getValue(name));
    if (TelEngine::null(oper))
	return;
    Message* m = new Message(msg);
    m->setParam("operation",oper);
    m->setParam("pbxstate",state());
    m->setParam("room",m_room);
    m->setParam("users",String(users));
    m->userData(msg.userData());
    Engine::enqueue(m);
}

// Hangup handler, do any cleanups needed
void PBXAssist::msgHangup(Message& msg)
{
    s_transMutex.lock();
    ObjList* l = s_transList.skipNull();
    while (l) {
	NamedString* n = static_cast<NamedString*>(l->get());
	if (id() == n->name() || id() == *n) {
	    // found self in list, remove entry
	    l->remove();
	    if (m_peer1.null())
		break;
	    l = l->skipNull();
	    continue;
	}
	else if (m_peer1 && (m_peer1 == n->name() || m_peer1 == *n)) {
	    // found held call in transfer list, make sure we don't drop it
	    m_peer1.clear();
	}
	l = l->skipNext();
    }
    s_transMutex.unlock();
    if (m_room && (state() == "conference") &&
	m_keep.getBoolValue("pbxdropconfhangup",s_dropConfHangup)) {
	// hangup the conference if we didn't switch out of it
	Message* m = new Message("call.drop");
	m->addParam("id",m_room);
	m->addParam("pbxstate",state());
	Engine::enqueue(m);
    }
    if (m_peer1) {
	// we have a call on hold - redial it or drop it
	Message* m = 0;
	if (s_dialHeld) {
	    // try to redial this number from the held call
	    String tmp = m_keep.getValue("caller");
	    if (tmp) {
		Debug(list(),DebugNote,"Call '%s' dialing '%s' from held call '%s'",
		    id().c_str(),tmp.c_str(),m_peer1.c_str());
		m = new Message("call.route");
		m->addParam("id",m_peer1);
		m->copyParam(m_keep,"billid");
		m->addParam("caller",m_keep.getValue("called"),false);
		m->addParam("called",tmp);
		m->addParam("pbxstate",state());
		m->addParam("reason","onhold");
		if (!Engine::dispatch(m) || m->retValue().null() || (m->retValue() == "-") || (m->retValue() == "error"))
		    TelEngine::destruct(m);
		else {
		    *m = "chan.masquerade";
		    m->setParam("message","call.execute");
		    m->setParam("callto",m->retValue());
		    m->retValue().clear();

		    Message* m2 = new Message("chan.operation");
		    m2->addParam("operation","setstate");
		    m2->addParam("id",m_peer1);
		    m2->addParam("state","*");
		    Engine::enqueue(m2);
		}
	    }
	}
	// if we can't do any better hangup anyone we still have on hold
	if (!m)
	    m = new Message("call.drop");
	m->setParam("id",m_peer1);
	m->setParam("pbxstate",state());
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
    copyParameter(msg,"billid");
    NamedString* status = msg.getParam("status");
    if (status && (*status == "outgoing")) {
	// switch them over so we have them right for later operations
	copyParameter(msg,"caller","called");
	copyParameter(msg,"called","caller");
    }
    else {
	copyParameter(msg,"caller");
	copyParameter(msg,"called");
    }
}

// Execute message handler, copy call and divert information we may need later
void PBXAssist::msgExecute(Message& msg)
{
    DDebug(list(),DebugNote,"Copying execute parameters for '%s'",id().c_str());
    // this gets only called on incoming call legs
    setGuest(msg);
    setParams(msg);
    copyParameter(msg,"billid");
    copyParameter(msg,"caller");
    copyParameter(msg,"called");
    m_keep.copyParam(msg,"divert",'_');
}

// Adjust any channel ID that has been replaced
void PBXAssist::chanReplaced(const String& initial, const String& final)
{
    if (initial == m_peer1) {
	Debug(list(),DebugInfo,"For '%s' replacing peer '%s' with '%s'",
	    id().c_str(),initial.c_str(),final.c_str());
	m_peer1 = final;
    }
}

INIT_PLUGIN(PBXList);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
