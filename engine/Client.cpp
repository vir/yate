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

using namespace TelEngine;

class UIHandler : public MessageHandler
{
public:
    UIHandler()
	: MessageHandler("ui.action",150)
	{ }
    virtual bool received(Message &msg);
};

			
Window::Window(const char* id)
    : m_id(id), m_visible(false), m_master(false)
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

bool Window::related(const Window* wnd) const
{
    if ((wnd == this) || !wnd || wnd->master())
	return false;
    return true;
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
    : Thread(name), m_line(0)
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

bool Client::addOption(const String& name, const String& item, bool atStart, Window* wnd, Window* skip)
{
    if (wnd)
	return wnd->addOption(name,item,atStart);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->addOption(name,item,atStart) || ok;
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
    return getText(name,item,wnd,skip);
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
	String line;
	getText("line",line,wnd);
	String proto;
	getText("proto",proto,wnd);
	return callStart(target,line,proto);
    }
    else if (name.startsWith("callto:"))
	return callStart(name.substr(7));
    else if (name == "accept") {
	callAccept(m_incoming);
	return true;
    }
    else if (name.startsWith("accept:")) {
	callAccept(name.substr(7));
	return true;
    }
    else if (name == "reject") {
	callReject(m_incoming);
	return true;
    }
    else if (name.startsWith("reject:")) {
	callReject(name.substr(7));
	return true;
    }
    else if (name == "hangup") {
	callHangup(m_incoming);
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
    Message* m = new Message("ui.event");
    m->addParam("event","toggle");
    m->addParam("name",name);
    m->addParam("active",String::boolText(active));
    Engine::enqueue(m);
    return false;
}

bool Client::select(Window* wnd, const String& name, const String& item)
{
    DDebug(ClientDriver::self(),DebugInfo,"Select '%s' '%s' in %p",
	name.c_str(),item.c_str(),wnd);
    setSelect(name,item,0,wnd);
    Message* m = new Message("ui.event");
    m->addParam("event","select");
    m->addParam("name",name);
    m->addParam("item",item);
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
	cc->openMedia();
	Engine::enqueue(cc->message("call.answered",false,true));
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

bool Client::callStart(const String& target, const String& line, const String& proto)
{
    Debug(ClientDriver::self(),DebugInfo,"callStart('%s','%s','%s')",
	target.c_str(),line.c_str(),proto.c_str());
    if (target.null())
	return false;
    ClientChannel* cc = new ClientChannel();
    Message* m = cc->message("call.route");
    Regexp r("^[a-z]\\+/");
    if (r.matches(target.safe()))
	m->setParam("callto",target);
    else
	m->setParam("called",target);
    if (line)
	m->setParam("line",line);
    if (proto)
	m->setParam("protocol",proto);
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
    if (msg && msg->userData()) {
	CallEndpoint* ch = static_cast<CallEndpoint*>(msg->userData());
	ClientChannel* cc = new ClientChannel(ch->id());
	if (cc->connect(ch)) {
	    m_incoming = cc->id();
	    msg->setParam("peerid",m_incoming);
	    msg->setParam("targetid",m_incoming);
	    Engine::enqueue(cc->message("call.ringing",false,true));
	    cc->deref();
	    // notify the UI about the call
	    String tmp("Call from:");
	    tmp << " " << caller;
	    lock();
	    setStatus(tmp);
	    setText("incoming",tmp);
	    setVisible("incoming");
	    unlock();
	    return true;
	}
    }
    return false;
}

void Client::clearIncoming(const String& id)
{
    if (id == m_incoming)
	m_incoming.clear();
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
	ok = Client::self()->addOption(name,msg.getValue("item"),msg.getBoolValue("insert"),wnd);
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
    Client::self()->unlock();
    return ok;
}

// IMPORTANT: having a target means "from inside Yate to the user"
//  An user initiated call must be incoming (no target)
ClientChannel::ClientChannel(const char* target)
    : Channel(ClientDriver::self(),0,target), m_line(0)
{
    m_targetid = target;
    Engine::enqueue(message("chan.startup"));
}

ClientChannel::~ClientChannel()
{
    closeMedia();
    String tmp("Hung up:");
    tmp << " " << (address() ? address() : id());
    if (Client::self()) {
	Client::self()->clearIncoming(id());
	Client::self()->setStatusLocked(tmp);
    }
    Engine::enqueue(message("chan.hangup"));
}

bool ClientChannel::openMedia()
{
    String dev = ClientDriver::device();
    if (dev.null())
	return false;
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

bool ClientChannel::callRouted(Message& msg)
{
    String tmp("Calling:");
    tmp << " " << msg.retValue();
    Client::self()->setStatusLocked(tmp);
    return true;
}

void ClientChannel::callAccept(Message& msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::callAccept() [%p]",this);
    Client::self()->setStatusLocked("Call connected");
    Channel::callAccept(msg);
}

void ClientChannel::callReject(const char* error, const char* reason)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::callReject('%s','%s') [%p]",
	error,reason,this);
    if (!reason)
	reason = error;
    if (!reason)
	reason = "Unknown reason";
    String tmp("Call failed:");
    tmp << " " << reason;
    if (Client::self())
	Client::self()->setStatusLocked(tmp);
    Channel::callReject(error,reason);
}

bool ClientChannel::msgRinging(Message& msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::msgRinging() [%p]",this);
    Client::self()->setStatusLocked("Call ringing");
    return Channel::msgRinging(msg);
}

bool ClientChannel::msgAnswered(Message& msg)
{
    Debug(ClientDriver::self(),DebugAll,"ClientChannel::msgAnswered() [%p]",this);
    Client::self()->setStatusLocked("Call answered");
    openMedia();
    return Channel::msgAnswered(msg);
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

bool ClientDriver::factory(UIFactory* factory, const char* type)
{
    return false;
}

bool ClientDriver::msgExecute(Message& msg, String& dest)
{
    Debug(this,DebugInfo,"msgExecute() '%s'",dest.c_str());
    return (Client::self()) && (Client::self()->callIncoming(msg.getValue("caller"),dest,&msg));
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
