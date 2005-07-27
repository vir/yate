/**
 * Client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include "yatecbase.h"

#include <stdio.h>

using namespace TelEngine;

class UIHandler : public MessageHandler
{
public:
    UIHandler()
	: MessageHandler("ui.action",150)
	{ }
    virtual bool received(Message &msg);
};

// utility function to check if a string begins and ends with -dashes-
static bool checkDashes(String& str)
{
    if (str.startsWith("-") && str.endsWith("-"))
	str.clear();
    return str.null();
}
			
Window::Window(const char* id)
    : m_id(id), m_visible(false), m_master(false), m_popup(false)
{
}

Window::~Window()
{
    if (Client::self())
	Client::self()->m_windows.remove(this,false);
}

const String& Window::toString() const
{
    return m_id;
}

void Window::title(const String& text)
{
    m_title = text;
}

bool Window::related(const Window* wnd) const
{
    if ((wnd == this) || !wnd || wnd->master())
	return false;
    return true;
}

bool Window::setParams(const NamedList& params)
{
    bool ok = true;
    unsigned int l = params.length();
    for (unsigned int i = 0; i < l; i++) {
	const NamedString* s = params.getParam(i);
	if (s) {
	    String n(s->name());
	    if (n == "title")
		title(*s);
	    else if (n.startSkip("show:",false))
		ok = setShow(n,s->toBoolean()) && ok;
	    else if (n.startSkip("active:",false))
		ok = setActive(n,s->toBoolean()) && ok;
	    else if (n.startSkip("check:",false))
		ok = setCheck(n,s->toBoolean()) && ok;
	    else if (n.startSkip("select:",false))
		ok = setSelect(n,*s) && ok;
	    else if (n.find(':') < 0)
		ok = setText(n,*s) && ok;
	    else
		ok = false;
	}
    }
    return ok;
}


UIFactory::UIFactory(const char* type, const char* name)
    : String(name)
{
    if (ClientDriver::self() && ClientDriver::self()->factory(this,type))
	return;
    Debug(ClientDriver::self(),DebugGoOn,"Could not register '%s' factory type '%s'",
	name,type);
}

UIFactory::~UIFactory()
{
    if (ClientDriver::self())
	ClientDriver::self()->factory(this,0);
}


Client* Client::s_client = 0;
int Client::s_changing = 0;

Client::Client(const char *name)
    : Thread(name), m_line(0),
      m_multiLines(false), m_autoAnswer(false)
{
    s_client = this;
    Engine::install(new UIHandler);
}

Client::~Client()
{
    m_windows.clear();
    s_client = 0;
    Engine::halt(0);
}

void Client::run()
{
    loadWindows();
    Message msg("ui.event");
    msg.setParam("event","load");
    Engine::dispatch(msg);
    initWindows();
    initClient();
    updateFrom(0);
    setStatus("");
    msg.setParam("event","init");
    Engine::dispatch(msg);
    main();
}

Window* Client::getWindow(const String& name)
{
    if (!s_client)
	return 0;
    ObjList* l = s_client->m_windows.find(name);
    return static_cast<Window*>(l ? l->get() : 0);
}

ObjList* Client::listWindows()
{
    if (!s_client)
	return 0;
    ObjList* lst = 0;
    for (ObjList* l = &s_client->m_windows; l; l = l->next()) {
	Window* w = static_cast<Window*>(l->get());
	if (w) {
	    if (!lst)
		lst = new ObjList;
	    lst->append(new String(w->id()));
	}
    }
    return lst;
}

bool Client::setVisible(const String& name, bool show)
{
    Window* w = getWindow(name);
    if (!w)
	return false;
    w->visible(show);
    return true;
}

bool Client::getVisible(const String& name)
{
    Window* w = getWindow(name);
    return w && w->visible();
}

void Client::initWindows()
{
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	Window* w = static_cast<Window*>(l->get());
	if (w)
	    w->init();
    }
}

void Client::initClient()
{
    m_multiLines =
	getWindow("channels") || hasElement("channels") ||
	getWindow("lines") || hasElement("lines");
    setCheck("multilines",m_multiLines);
    getCheck("autoanswer",m_autoAnswer);
}

void Client::moveRelated(const Window* wnd, int dx, int dy)
{
    if (!wnd)
	return;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	Window* w = static_cast<Window*>(l->get());
	if (w && (w != wnd) && wnd->related(w))
	    w->moveRel(dx,dy);
    }
}

bool Client::openPopup(const String& name, const NamedList* params, const Window* parent)
{
    Window* wnd = getWindow(name);
    if (!wnd)
	return false;
    if (params)
	wnd->setParams(*params);
    if (parent)
	wnd->setOver(parent);
    wnd->show();
    return true;
}

bool Client::hasElement(const String& name, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->hasElement(name);
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip) && wnd->hasElement(name))
	    return true;
    }
    return false;
}

bool Client::setShow(const String& name, bool visible, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->setShow(name,visible);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setShow(name,visible) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::setActive(const String& name, bool active, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->setActive(name,active);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setActive(name,active) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::setText(const String& name, const String& text, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->setText(name,text);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setText(name,text) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::setCheck(const String& name, bool checked, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->setCheck(name,checked);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setCheck(name,checked) || ok;
    }
    --s_changing;
    return ok;
}	    

bool Client::setSelect(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->setSelect(name,item);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setSelect(name,item) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::setUrgent(const String& name, bool urgent, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->setUrgent(name,urgent);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setUrgent(name,urgent) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::addOption(const String& name, const String& item, bool atStart, const String& text, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->addOption(name,item,atStart,text);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->addOption(name,item,atStart,text) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::delOption(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->delOption(name,item);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->delOption(name,item) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::getText(const String& name, String& text, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->getText(name,text);
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip) && wnd->getText(name,text))
	    return true;
    }
    return false;
}

bool Client::getCheck(const String& name, bool& checked, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->getCheck(name,checked);
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip) && wnd->getCheck(name,checked))
	    return true;
    }
    return false;
}

bool Client::getSelect(const String& name, String& item, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->getSelect(name,item);
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip) && wnd->getSelect(name,item))
	    return true;
    }
    return false;
}

bool Client::setStatus(const String& text, Window* wnd)
{
    Debug(ClientDriver::self(),DebugInfo,"Status '%s' in window %p",text.c_str(),wnd);
    return setText("status",text,wnd);
}

bool Client::setStatusLocked(const String& text, Window* wnd)
{
    lock();
    bool ok = setStatus(text,wnd);
    unlock();
    return ok;
}

bool Client::action(Window* wnd, const String& name)
{
    DDebug(ClientDriver::self(),DebugInfo,"Action '%s' in %p",name.c_str(),wnd);
    if (name == "call" || name == "callto") {
	String target;
	getText("callto",target,wnd);
	target.trimBlanks();
	if (target.null())
	    return false;
	String line;
	getText("line",line,wnd);
	line.trimBlanks();
	checkDashes(line);
	String proto;
	getText("protocol",proto,wnd);
	proto.trimBlanks();
	checkDashes(proto);
	String account;
	getText("account",account,wnd);
	account.trimBlanks();
	checkDashes(account);
	return callStart(target,line,proto,account);
    }
    else if (name.startsWith("callto:"))
	return callStart(name.substr(7));
    else if (name == "accept") {
	callAccept(m_activeId);
	return true;
    }
    else if (name.startsWith("accept:")) {
	callAccept(name.substr(7));
	return true;
    }
    else if (name == "reject") {
	callReject(m_activeId);
	return true;
    }
    else if (name.startsWith("reject:")) {
	callReject(name.substr(7));
	return true;
    }
    else if (name == "hangup") {
	callHangup(m_activeId);
	return true;
    }
    else if (name.startsWith("hangup:")) {
	callHangup(name.substr(7));
	return true;
    }
    else if (name.startsWith("digit:")) {
	emitDigit(name.at(6));
	return true;
    }
    else if (name.startsWith("line:")) {
	int l = name.substr(5).toInteger(-1);
	if (l >= 0) {
	    line(l);
	    return true;
	}
    }
    Message* m = new Message("ui.event");
    if (wnd)
	m->addParam("window",wnd->id());
    m->addParam("event","action");
    m->addParam("name",name);
    Engine::enqueue(m);
    return false;
}

bool Client::toggle(Window* wnd, const String& name, bool active)
{
    DDebug(ClientDriver::self(),DebugInfo,"Toggle '%s' %s in %p",
	name.c_str(),String::boolText(active),wnd);
    if (setVisible(name,active))
	return true;
    setCheck(name,active,0,wnd);
    if (name == "autoanswer") {
	m_autoAnswer = active;
	return true;
    }
    if (name == "multilines") {
	m_multiLines = active;
	return true;
    }
    Message* m = new Message("ui.event");
    if (wnd)
	m->addParam("window",wnd->id());
    m->addParam("event","toggle");
    m->addParam("name",name);
    m->addParam("active",String::boolText(active));
    Engine::enqueue(m);
    return false;
}

bool Client::select(Window* wnd, const String& name, const String& item, const String& text)
{
    DDebug(ClientDriver::self(),DebugInfo,"Select '%s' '%s' in %p",
	name.c_str(),item.c_str(),wnd);
    setSelect(name,item,0,wnd);
    if (name == "channels") {
	updateFrom(item);
	return true;
    }
    Message* m = new Message("ui.event");
    if (wnd)
	m->addParam("window",wnd->id());
    m->addParam("event","select");
    m->addParam("name",name);
    m->addParam("item",item);
    if (text)
	m->addParam("text",text);
    Engine::enqueue(m);
    return false;
}

void Client::line(int newLine)
{
    Debug(ClientDriver::self(),DebugInfo,"line(%d)",newLine);
    m_line = newLine;
}

void Client::callAccept(const char* callId)
{
    Debug(ClientDriver::self(),DebugInfo,"callAccept('%s')",callId);
    ClientChannel* cc = static_cast<ClientChannel*>(ClientDriver::self()->find(callId));
    if (cc) {
	cc->ref();
	cc->callAnswer();
	setChannelInternal(cc);
	cc->deref();
    }
}

void Client::callReject(const char* callId)
{
    Debug(ClientDriver::self(),DebugInfo,"callReject('%s')",callId);
    if (!ClientDriver::self())
	return;
    Message* m = new Message("call.drop");
    m->addParam("id",callId ? callId : ClientDriver::self()->name().c_str());
    m->addParam("error","rejected");
    m->addParam("reason","Refused");
    Engine::enqueue(m);
}

void Client::callHangup(const char* callId)
{
    Debug(ClientDriver::self(),DebugInfo,"callHangup('%s')",callId);
    if (!ClientDriver::self())
	return;
    Message* m = new Message("call.drop");
    m->addParam("id",callId ? callId : ClientDriver::self()->name().c_str());
    m->addParam("reason","User hangup");
    Engine::enqueue(m);
}

bool Client::callStart(const String& target, const String& line,
    const String& proto, const String& account)
{
    Debug(ClientDriver::self(),DebugInfo,"callStart('%s','%s','%s','%s')",
	target.c_str(),line.c_str(),proto.c_str(),account.c_str());
    if (target.null())
	return false;
    ClientChannel* cc = new ClientChannel(target);
    Message* m = cc->message("call.route");
    Regexp r("^[a-z0-9]\\+/");
    bool hasProto = r.matches(target.safe());
    if (hasProto)
	m->setParam("callto",target);
    else if (proto)
	m->setParam("callto",proto + "/" + target);
    else
	m->setParam("called",target);
    if (line)
	m->setParam("line",line);
    if (proto)
	m->setParam("protocol",proto);
    if (account)
	m->setParam("account",account);
    return cc->startRouter(m);
}

bool Client::emitDigit(char digit)
{
    Debug(ClientDriver::self(),DebugInfo,"emitDigit('%c')",digit);
    return false;
}

bool Client::callIncoming(const String& caller, const String& dest, Message* msg)
{
    Debug(ClientDriver::self(),DebugAll,"callIncoming [%p]",this);
    if (m_activeId && !m_multiLines) {
	if (msg) {
	    msg->setParam("error","busy");
	    msg->setParam("reason","User busy");
	}
	return false;
    }
    if (msg && msg->userData()) {
	CallEndpoint* ch = static_cast<CallEndpoint*>(msg->userData());
	lock();
	ClientChannel* cc = new ClientChannel(caller,ch->id());
	unlock();
	if (cc->connect(ch)) {
	    m_activeId = cc->id();
	    msg->setParam("peerid",m_activeId);
	    msg->setParam("targetid",m_activeId);
	    Engine::enqueue(cc->message("call.ringing",false,true));
	    lock();
	    // notify the UI about the call
	    String tmp("Call from:");
	    tmp << " " << caller;
	    setStatus(tmp);
	    if (m_autoAnswer) {
		cc->callAnswer();
		setChannelInternal(cc);
	    }
	    else {
		setText("incoming",tmp);
		if (!(m_multiLines && setVisible("channels")))
		    setVisible("incoming");
	    }
	    unlock();
	    cc->deref();
	    return true;
	}
    }
    return false;
}

void Client::clearActive(const String& id)
{
    if (id == m_activeId)
	m_activeId.clear();
}

void Client::addChannel(ClientChannel* chan)
{
    addOption("channels",chan->id(),false,chan->description());
}

void Client::setChannel(ClientChannel* chan)
{
    Debug(ClientDriver::self(),DebugAll,"setChannel %p",chan);
    lock();
    setChannelInternal(chan);
    unlock();
}

void Client::setChannelInternal(ClientChannel* chan)
{
    String tmp(chan->description());
    if (!setUrgent(chan->id(),chan->flashing()) && chan->flashing())
	tmp << " <<<";
    setText(chan->id(),tmp);
    if (getSelect("channels",tmp) && (tmp == chan->id()))
	updateFrom(chan);
}

void Client::delChannel(ClientChannel* chan)
{
    lock();
    clearActive(chan->id());
    delOption("channels",chan->id());
    unlock();
}

void Client::updateFrom(const String& id)
{
    ClientChannel* chan = 0;
    if (ClientDriver::self())
	chan = static_cast<ClientChannel*>(ClientDriver::self()->find(id));
    if (chan)
	chan->noticed();
    updateFrom(chan);
}

void Client::updateFrom(const ClientChannel* chan)
{
    m_activeId = chan ? chan->id() : "";
    enableAction(chan,"accept");
    enableAction(chan,"reject");
    enableAction(chan,"hangup");
    enableAction(chan,"voicemail");
    enableAction(chan,"transfer");
    enableAction(chan,"conference");
}

void Client::enableAction(const ClientChannel* chan, const String& action)
{
    setActive(action,chan && chan->enableAction(action));
}

bool UIHandler::received(Message &msg)
{
    if (!Client::self())
	return false;
    String action(msg.getValue("action"));
    if (action.null())
	return false;
    Window* wnd = Client::getWindow(msg.getValue("window"));
    if (action == "set_status")
	return Client::self()->setStatusLocked(msg.getValue("status"),wnd);
    String name(msg.getValue("name"));
    if (name.null())
	return false;
    DDebug(ClientDriver::self(),DebugAll,"UI action '%s' on '%s' in %p",
	action.c_str(),name.c_str(),wnd);
    bool ok = false;
    Client::self()->lock();
    if (action == "set_text")
	ok = Client::self()->setText(name,msg.getValue("text"),wnd);
    else if (action == "set_toggle")
	ok = Client::self()->setCheck(name,msg.getBoolValue("active"),wnd);
    else if (action == "set_select")
	ok = Client::self()->setSelect(name,msg.getValue("item"),wnd);
    else if (action == "set_active")
	ok = Client::self()->setActive(name,msg.getBoolValue("active"),wnd);
    else if (action == "set_visible")
	ok = Client::self()->setShow(name,msg.getBoolValue("visible"),wnd);
    else if (action == "add_option")
	ok = Client::self()->addOption(name,msg.getValue("item"),msg.getBoolValue("insert"),msg.getValue("text"),wnd);
    else if (action == "del_option")
	ok = Client::self()->delOption(name,msg.getValue("item"),wnd);
    else if (action == "get_text") {
	String text;
	ok = Client::self()->getText(name,text,wnd);
	if (ok)
	    msg.retValue() = text;
    }
    else if (action == "get_toggle") {
	bool check;
	ok = Client::self()->getCheck(name,check,wnd);
	if (ok)
	    msg.retValue() = check;
    }
    else if (action == "get_select") {
	String item;
	ok = Client::self()->getSelect(name,item,wnd);
	if (ok)
	    msg.retValue() = item;
    }
    else if (action == "window_show")
	ok = Client::setVisible(name,true);
    else if (action == "window_hide")
	ok = Client::setVisible(name,false);
    else if (action == "window_popup")
	ok = Client::openPopup(name,&msg,Client::getWindow(msg.getValue("parent")));
    Client::self()->unlock();
    return ok;
}

// IMPORTANT: having a target means "from inside Yate to the user"
//  An user initiated call must be incoming (no target)
ClientChannel::ClientChannel(const String& party, const char* target)
    : Channel(ClientDriver::self(),0,(target != 0)),
      m_party(party), m_line(0), m_flashing(false),
      m_canAnswer(false), m_canTransfer(false), m_canConference(false)
{
    m_time = Time::now();
    m_targetid = target;
    if (target) {
	m_flashing = true;
	m_canAnswer = true;
    }
    update(false);
    if (Client::self())
	Client::self()->addChannel(this);
    Engine::enqueue(message("chan.startup"));
}

ClientChannel::~ClientChannel()
{
    closeMedia();
    String tmp("Hung up:");
    tmp << " " << (address() ? address() : id());
    if (Client::self()) {
	Client::self()->delChannel(this);
	Client::self()->setStatusLocked(tmp);
    }
    Engine::enqueue(message("chan.hangup"));
}

bool ClientChannel::openMedia(bool replace)
{
    String dev = ClientDriver::device();
    if (dev.null())
	return false;
    if ((!replace) && getSource() && getConsumer())
	return true;
    Message m("chan.attach");
    complete(m,true);
    m.setParam("source",dev);
    m.setParam("consumer",dev);
    m.userData(this);
    return Engine::dispatch(m);
}

void ClientChannel::closeMedia()
{
    setSource();
    setConsumer();
}

void ClientChannel::line(int newLine)
{
    m_line = newLine;
    m_address.clear();
    if (m_line > 0)
	m_address << "line/" << m_line;
}

void ClientChannel::update(bool client)
{
    String desc;
    if (m_canAnswer)
	desc = "Ringing";
    // directions are from engine's perspective so reverse them for user
    else if (isOutgoing())
	desc = "Incoming";
    else desc = "Outgoing";
    desc << " " << m_party;
    unsigned int sec = (unsigned int)((Time::now() - m_time + 500000) / 1000000);
    char buf[32];
    ::snprintf(buf,sizeof(buf)," [%02u:%02u:%02u]",sec/3600,(sec/60)%60,sec%60);
    desc << buf;
    CallEndpoint* peer = getPeer();
    if (peer) {
	peer->ref();
	String tmp;
	if (peer->getConsumer())
	    tmp = peer->getConsumer()->getFormat();
	if (tmp.null())
	    tmp = "-";
	desc << " [" << tmp;
	tmp.clear();
	if (peer->getSource())
	    tmp = peer->getSource()->getFormat();
	peer->deref();
	if (tmp.null())
	    tmp = "-";
	desc << "/" << tmp << "]";
    }
    desc << " " << id();
    m_desc = desc;
    XDebug(ClientDriver::self(),DebugAll,"update %d '%s'",client,desc.c_str());
    if (client && Client::self())
	Client::self()->setChannel(this);
}

bool ClientChannel::enableAction(const String& action) const
{
    if (action == "hangup")
	return true;
    else if ((action == "accept") || (action == "reject") || (action == "voicemail"))
	return m_canAnswer;
    else if (action == "transfer")
	return m_canTransfer;
    else if (action == "conference")
	return m_canConference;
    return false;
}

bool ClientChannel::callRouted(Message& msg)
{
    String tmp("Calling:");
    tmp << " " << msg.retValue();
    Client::self()->setStatusLocked(tmp);
    update();
    return true;
}

void ClientChannel::callAccept(Message& msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::callAccept() [%p]",this);
    Client::self()->setStatusLocked("Call connected");
    Channel::callAccept(msg);
    update();
}

void ClientChannel::callRejected(const char* error, const char* reason, const Message* msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::callReject('%s','%s',%p) [%p]",
	error,reason,msg,this);
    if (!reason)
	reason = error;
    if (!reason)
	reason = "Unknown reason";
    String tmp("Call failed:");
    tmp << " " << reason;
    if (Client::self())
	Client::self()->setStatusLocked(tmp);
    Channel::callRejected(error,reason,msg);
    m_flashing = true;
    m_canConference = m_canTransfer = m_canAnswer = false;
    update();
}

bool ClientChannel::msgProgress(Message& msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::msgProgress() [%p]",this);
    Client::self()->setStatusLocked("Call progressing");
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
    if (ch && ch->getSource())
	openMedia();
    bool ret = Channel::msgProgress(msg);
    update();
    return ret;
}

bool ClientChannel::msgRinging(Message& msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::msgRinging() [%p]",this);
    Client::self()->setStatusLocked("Call ringing");
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
    if (ch && ch->getSource())
	openMedia();
    bool ret = Channel::msgRinging(msg);
    update();
    return ret;
}

bool ClientChannel::msgAnswered(Message& msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::msgAnswered() [%p]",this);
    m_time = Time::now();
    m_flashing = true;
    m_canAnswer = false;
    m_canConference = true;
    m_canTransfer = true;
    Client::self()->setStatusLocked("Call answered");
    openMedia();
    bool ret = Channel::msgAnswered(msg);
    update();
    return ret;
}

void ClientChannel::callAnswer()
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::callAnswer() [%p]",this);
    m_time = Time::now();
    m_flashing = false;
    m_canAnswer = false;
    m_canConference = true;
    m_canTransfer = true;
    status("answered");
    Client::self()->setStatus("Call answered");
    openMedia();
    update(false);
    Engine::enqueue(message("call.answered",false,true));
}


ClientDriver* ClientDriver::s_driver = 0;
String ClientDriver::s_device;

ClientDriver::ClientDriver()
    : Driver("client","misc")
{
    s_driver = this;
}

ClientDriver::~ClientDriver()
{
    s_driver = 0;
}

void ClientDriver::setup()
{
    Driver::setup();
    installRelay(Halt);
    installRelay(Progress);
}

bool ClientDriver::factory(UIFactory* factory, const char* type)
{
    return false;
}

bool ClientDriver::msgExecute(Message& msg, String& dest)
{
    Debug(this,DebugInfo,"msgExecute() '%s'",dest.c_str());
    return (Client::self()) && (Client::self()->callIncoming(msg.getValue("caller"),dest,&msg));
}

void ClientDriver::msgTimer(Message& msg)
{
    Driver::msgTimer(msg);
    if (Client::self()) {
	Client::self()->lock();
	ObjList* l = &channels();
	for (; l; l = l->next()) {
	    ClientChannel* cc = static_cast<ClientChannel*>(l->get());
	    if (cc) {
		cc->update(false);
		Client::self()->setChannelInternal(cc);
	    }
	}
	Client::self()->unlock();
    }
}

ClientChannel* ClientDriver::findLine(int line)
{
    if (line < 1)
	return 0;
    Lock mylock(this);
    ObjList* l = &channels();
    for (; l; l = l->next()) {
	ClientChannel* cc = static_cast<ClientChannel*>(l->get());
	if (cc && (cc->line() == line))
	    return cc;
    }
    return 0;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
