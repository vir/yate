/**
 * Client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#include "yatecbase.h"
#include "yateversn.h"

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

class UICdrHandler : public MessageHandler
{
public:
    UICdrHandler()
	: MessageHandler("call.cdr",90)
	{ }
    virtual bool received(Message &msg);
};

class UIUserHandler : public MessageHandler
{
public:
    UIUserHandler()
	: MessageHandler("user.login",50)
	{ }
    virtual bool received(Message &msg);
};

class ClientThreadProxy
{
public:
    enum {
	setVisible,
	openPopup,
	hasElement,
	setShow,
	setText,
	setActive,
	setFocus,
	setCheck,
	setSelect,
	setUrgent,
	hasOption,
	addOption,
	delOption,
	addTableRow,
	delTableRow,
	setTableRow,
	getTableRow,
	clearTable,
	getText,
	getCheck,
	getSelect,
    };
    ClientThreadProxy(int func, const String& name, bool show, Window* wnd = 0, Window* skip = 0);
    ClientThreadProxy(int func, const String& name, const String& text, Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, const String& text, const String& item, bool show, Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, String* rtext, bool* rbool, Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, const NamedList* params, const Window* parent);
    ClientThreadProxy(int func, const String& name, const String& item, bool start, const NamedList* params, Window* wnd, Window* skip);
    void process();
    bool execute();
private:
    int m_func;
    bool m_rval;
    String m_name;
    String m_text;
    String m_item;
    bool m_bool;
    String* m_rtext;
    bool* m_rbool;
    Window* m_wnd;
    Window* m_skip;
    const NamedList* m_params;
};


// utility function to check if a string begins and ends with -dashes-
static bool checkDashes(const String& str)
{
    return str.startsWith("-") && str.endsWith("-");
}

// utility function to make empty a string that begins and ends with -dashes-
// returns true if fixed string is empty
static bool fixDashes(String& str)
{
    if (checkDashes(str))
	str.clear();
    str.trimBlanks();
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

void Window::context(const String& text)
{
    m_context = text;
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
	    if (n == "context")
		context(*s);
	    else if (n.startSkip("show:",false))
		ok = setShow(n,s->toBoolean()) && ok;
	    else if (n.startSkip("active:",false))
		ok = setActive(n,s->toBoolean()) && ok;
	    else if (n.startSkip("focus:",false))
		ok = setFocus(n,s->toBoolean()) && ok;
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

bool Window::addTableRow(const String& name, const String& item, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub addTableRow('%s','%s',%p,%s) [%p]",
	name.c_str(),item.c_str(),data,String::boolText(atStart),this);
    return false;
}

bool Window::delTableRow(const String& name, const String& item)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub delTableRow('%s','%s') [%p]",
	name.c_str(),item.c_str(),this);
    return false;
}

bool Window::setTableRow(const String& name, const String& item, const NamedList* data)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub setTableRow('%s','%s',%p) [%p]",
	name.c_str(),item.c_str(),data,this);
    return false;
}

bool Window::getTableRow(const String& name, const String& item, NamedList* data)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub getTableRow('%s','%s',%p) [%p]",
	name.c_str(),item.c_str(),data,this);
    return false;
}

bool Window::clearTable(const String& name)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub clearTable('%s') [%p]",
	name.c_str(),this);
    return false;
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


static Mutex s_proxyMutex;
static ClientThreadProxy* s_proxy = 0;
static bool s_busy = false;

ClientThreadProxy::ClientThreadProxy(int func, const String& name, bool show, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(show), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const String& text, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_text(text), m_bool(false), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const String& text, const String& item, bool show, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_text(text), m_item(item), m_bool(show), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, String* rtext, bool* rbool, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(false), m_rtext(rtext), m_rbool(rbool),
      m_wnd(wnd), m_skip(skip), m_params(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const NamedList* params, const Window* parent)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(false), m_rtext(0), m_rbool(0),
      m_wnd(const_cast<Window*>(parent)), m_skip(0), m_params(params)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const String& item, bool start, const NamedList* params, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_item(item), m_bool(start), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(params)
{
}

void ClientThreadProxy::process()
{
    Debugger debug(DebugAll,"ClientThreadProxy::process()"," %d [%p]",m_func,this);
    Client* client = Client::self();
    if (!client) {
	s_busy = false;
	return;
    }
    switch (m_func) {
	case setVisible:
	    m_rval = Client::setVisible(m_name,m_bool);
	    break;
	case openPopup:
	    m_rval = Client::openPopup(m_name,m_params,m_wnd);
	    break;
	case hasElement:
	    m_rval = client->hasElement(m_name,m_wnd,m_skip);
	    break;
	case setShow:
	    m_rval = client->setShow(m_name,m_bool,m_wnd,m_skip);
	    break;
	case setText:
	    m_rval = client->setText(m_name,m_text,m_wnd,m_skip);
	    break;
	case setActive:
	    m_rval = client->setActive(m_name,m_bool,m_wnd,m_skip);
	    break;
	case setFocus:
	    m_rval = client->setFocus(m_name,m_bool,m_wnd,m_skip);
	    break;
	case setCheck:
	    m_rval = client->setCheck(m_name,m_bool,m_wnd,m_skip);
	    break;
	case setSelect:
	    m_rval = client->setSelect(m_name,m_text,m_wnd,m_skip);
	    break;
	case setUrgent:
	    m_rval = client->setUrgent(m_name,m_bool,m_wnd,m_skip);
	    break;
	case hasOption:
	    m_rval = client->hasOption(m_name,m_text,m_wnd,m_skip);
	    break;
	case addOption:
	    m_rval = client->addOption(m_name,m_item,m_bool,m_text,m_wnd,m_skip);
	    break;
	case delOption:
	    m_rval = client->delOption(m_name,m_text,m_wnd,m_skip);
	    break;
	case addTableRow:
	    m_rval = client->addTableRow(m_name,m_item,m_params,m_bool,m_wnd,m_skip);
	    break;
	case delTableRow:
	    m_rval = client->delTableRow(m_name,m_text,m_wnd,m_skip);
	    break;
	case setTableRow:
	    m_rval = client->setTableRow(m_name,m_item,m_params,m_wnd,m_skip);
	    break;
	case getTableRow:
	    m_rval = client->getTableRow(m_name,m_item,const_cast<NamedList*>(m_params),m_wnd,m_skip);
	    break;
	case clearTable:
	    m_rval = client->clearTable(m_name);
	    break;
	case getText:
	    m_rval = client->getText(m_name,*m_rtext,m_wnd,m_skip);
	    break;
	case getCheck:
	    m_rval = client->getCheck(m_name,*m_rbool,m_wnd,m_skip);
	    break;
	case getSelect:
	    m_rval = client->getSelect(m_name,*m_rtext,m_wnd,m_skip);
	    break;
    }
    s_busy = false;
}

bool ClientThreadProxy::execute()
{
    Debugger debug(DebugAll,"ClientThreadProxy::execute()"," %d in %p [%p]",
	m_func,Thread::current(),this);
    s_proxyMutex.lock();
    s_proxy = this;
    s_busy = true;
    while (s_busy)
	Thread::yield();
    s_proxyMutex.unlock();
    return m_rval;
}


Client* Client::s_client = 0;
int Client::s_changing = 0;
static Configuration s_accounts;
static Configuration s_contacts;
static Configuration s_providers;
static Configuration s_history;

// Parameters that are stored with account
static const char* s_accParams[] = {
    "username",
    "password",
    "server",
    "domain",
    "outbound",
    0
};

// Parameters that are applied from provider template
static const char* s_provParams[] = {
    "server",
    "domain",
    "outbound",
    0
};


Client::Client(const char *name)
    : Thread(name), m_initialized(false), m_line(0), m_oneThread(true),
      m_multiLines(false), m_autoAnswer(false)
{
    s_client = this;
    Engine::install(new UICdrHandler);
    Engine::install(new UIUserHandler);
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
    setStatus(Engine::config().getValue("client","greeting","Yate " YATE_VERSION " " YATE_RELEASE));
    m_initialized = true;
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
    if (s_client && s_client->needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setVisible,name,show);
	return proxy.execute();
    }
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
    s_accounts = Engine::configFile("client_accounts",true);
    s_accounts.load();
    unsigned int n = s_accounts.sections();
    unsigned int i;
    for (i=0; i<n; i++) {
	NamedList* sect = s_accounts.getSection(i);
	if (sect) {
	    if (!hasOption("accounts",*sect))
		addOption("accounts",*sect,false);
	    Message* m = new Message("user.login");
	    m->addParam("account",*sect);
//	    m->addParam("operation","create");
	    unsigned int n2 = sect->length();
	    for (unsigned int j=0; j<n2; j++) {
		NamedString* param = sect->getParam(j);
		if (param)
		    m->addParam(param->name(),*param);
	    }
	    Engine::enqueue(m);
	}
    }

    s_contacts = Engine::configFile("client_contacts",true);
    s_contacts.load();
    n = s_contacts.sections();
    for (i=0; i<n; i++) {
	NamedList* sect = s_contacts.getSection(i);
	if (sect) {
	    if (!hasOption("contacts",*sect))
		addOption("contacts",*sect,false);
	}
    }

    s_providers = Engine::configFile("providers");
    s_providers.load();
    n = s_providers.sections();
    for (i=0; i<n; i++) {
	NamedList* sect = s_providers.getSection(i);
	if (sect && sect->getBoolValue("enabled",true)) {
	    if (!hasOption("acc_providers",*sect))
		addOption("acc_providers",*sect,false);
	}
    }

    s_history = Engine::configFile("client_history",true);
    s_history.load();
    n = s_history.sections();
    for (i=0; i<n; i++) {
	NamedList* sect = s_history.getSection(i);
	if (sect)
	    updateCallHist(*sect);
    }

    bool tmp =
	getWindow("channels") || hasElement("channels") ||
	getWindow("lines") || hasElement("lines");
    m_multiLines = Engine::config().getBoolValue("client","multilines",tmp);
    tmp = false;
    getCheck("autoanswer",tmp);
    m_autoAnswer = Engine::config().getBoolValue("client","autoanswer",tmp);
    setCheck("multilines",m_multiLines);
    setCheck("autoanswer",m_autoAnswer);
    Window* help = getWindow("help");
    if (help)
	action(help,"help_home");
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
    if (s_client && s_client->needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::openPopup,name,params,parent);
	return proxy.execute();
    }
    Window* wnd = getWindow(name);
    if (!wnd)
	return false;
    wnd->context("");
    if (params)
	wnd->setParams(*params);
    if (parent)
	wnd->setOver(parent);
    wnd->show();
    return true;
}

bool Client::openMessage(const char* text, const Window* parent, const char* context)
{
    NamedList params("");
    params.addParam("text",text);
    params.addParam("modal",String::boolText(parent != 0));
    if (!null(context))
	params.addParam("context",context);
    return openPopup("message",&params,parent);
}

bool Client::openConfirm(const char* text, const Window* parent, const char* context)
{
    NamedList params("");
    params.addParam("text",text);
    params.addParam("modal",String::boolText(parent != 0));
    if (!null(context))
	params.addParam("context",context);
    return openPopup("confirm",&params,parent);
}

bool Client::hasElement(const String& name, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::hasElement,name,false,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setShow,name,visible,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setActive,name,active,wnd,skip);
	return proxy.execute();
    }
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

bool Client::setFocus(const String& name, bool select, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setFocus,name,select,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setFocus(name,select);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setFocus(name,select) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::setText(const String& name, const String& text, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setText,name,text,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setCheck,name,checked,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setSelect,name,item,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setUrgent,name,urgent,wnd,skip);
	return proxy.execute();
    }
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

bool Client::hasOption(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::hasOption,name,item,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->hasOption(name,item);
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip) && wnd->hasOption(name,item))
	    return true;
    }
    return false;
}

bool Client::addOption(const String& name, const String& item, bool atStart, const String& text, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::addOption,name,text,item,atStart,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::delOption,name,item,wnd,skip);
	return proxy.execute();
    }
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

bool Client::addTableRow(const String& name, const String& item, const NamedList* data, bool atStart, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::addTableRow,name,item,atStart,data,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->addTableRow(name,item,data,atStart);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->addTableRow(name,item,data,atStart) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::delTableRow(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::delTableRow,name,item,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->delTableRow(name,item);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->delTableRow(name,item) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::setTableRow(const String& name, const String& item, const NamedList* data, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setTableRow,name,item,false,data,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setTableRow(name,item,data);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setTableRow(name,item,data) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::getTableRow(const String& name, const String& item, NamedList* data, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getTableRow,name,item,false,data,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->getTableRow(name,item,data);
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip) && wnd->getTableRow(name,item,data))
	    return true;
    }
    return false;
}

bool Client::clearTable(const String& name, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::clearTable,name,false,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->clearTable(name);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->clearTable(name) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::getText(const String& name, String& text, Window* wnd, Window* skip)
{
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getText,name,&text,0,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getCheck,name,0,&checked,wnd,skip);
	return proxy.execute();
    }
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
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getSelect,name,&item,0,wnd,skip);
	return proxy.execute();
    }
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
    lockOther();
    bool ok = setStatus(text,wnd);
    unlockOther();
    return ok;
}

bool Client::action(Window* wnd, const String& name)
{
    DDebug(ClientDriver::self(),DebugInfo,"Action '%s' in %p",name.c_str(),wnd);
    // hack to simplify actions from confirmation boxes
    if (wnd && wnd->context() && (name == "ok") && (wnd->context() != "ok")) {
	bool ok = action(wnd,wnd->context());
	if (ok)
	    wnd->hide();
	return ok;
    }
    if (name.startsWith("help_show:")) {
	Window* help = getWindow("help");
	if (help)
	    wnd = help;
    }
    if (name == "call" || name == "callto") {
	String target;
	getText("callto",target,wnd);
	target.trimBlanks();
	if (target.null())
	    return false;
	String line;
	getText("line",line,wnd);
	line.trimBlanks();
	fixDashes(line);
	String proto;
	getText("protocol",proto,wnd);
	proto.trimBlanks();
	fixDashes(proto);
	String account;
	getText("account",account,wnd);
	account.trimBlanks();
	fixDashes(account);
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
	if (m_activeId) {
	    emitDigit(name.at(6));
	    return true;
	}
	String target;
	Window* win = (wnd && hasElement("callto",wnd)) ? wnd : 0;
	if (getText("callto",target)) {
	    String digits = name.at(6);
	    if (isE164(digits)) {
		target += digits;
		if (setText("callto",target,win)) {
		    setFocus("callto",false,win);
		    return true;
		}
	    }
	}
    }
    else if (name.startsWith("line:")) {
	int l = name.substr(5).toInteger(-1);
	if (l >= 0) {
	    line(l);
	    return true;
	}
    }
    else if (name.startsWith("clear:")) {
	// clear a text field or table
	String wid = name.substr(6);
	Window* win = (wnd && hasElement(wid,wnd)) ? wnd : 0;
	if (wid && (setText(wid,"",win) || clearTable(wid,win))) {
	    setFocus(wid,false,win);
	    return true;
	}
    }
    else if (name.startsWith("back:")) {
	// delete last character (backspace)
	String wid = name.substr(5);
	String str;
	Window* win = (wnd && hasElement(wid,wnd)) ? wnd : 0;
	if (getText(wid,str,win)) {
	    if (str.null() || setText(wid,str.substr(0,str.length()-1),win)) {
		setFocus(wid,false,win);
		return true;
	    }
	}
    }
    // accounts window actions
    else if (name == "acc_new") {
	NamedList params("");
	params.setParam("select:acc_providers","--");
	params.setParam("acc_account","");
	params.setParam("acc_username","");
	params.setParam("acc_password","");
	params.setParam("acc_server","");
	params.setParam("acc_domain","");
	params.setParam("acc_outbound","");
	params.setParam("modal",String::boolText(true));
	if (openPopup("account",&params,wnd))
	    return true;
    }
    else if ((name == "acc_edit") || (name == "accounts")) {
	String acc;
	if (getSelect("accounts",acc,wnd)) {
	    NamedList params("");
	    params.setParam("context",acc);
	    params.setParam("select:acc_providers","--");
	    params.setParam("acc_account",acc);
	    NamedList* sect = s_accounts.getSection(acc);
	    if (sect) {
		params.setParam("select:acc_protocol",sect->getValue("protocol"));
		for (const char** par = s_accParams; *par; par++) {
		    String name;
		    name << "acc_" << *par;
		    params.setParam(name,sect->getValue(*par));
		}
	    }
	    params.setParam("modal",String::boolText(true));
	    if (openPopup("account",&params,wnd))
		return true;
	}
	else
	    return false;
    }
    else if (name == "acc_del") {
	String acc;
	if (getSelect("accounts",acc,wnd) && acc) {
	    if (openConfirm("Delete account "+acc,wnd,name + ":" + acc))
		return true;
	}
	else
	    return false;
    }
    else if (name.startsWith("acc_del:")) {
	String acc = name.substr(8);
	s_accounts.clearSection(acc);
	s_accounts.save();
	Message* m = new Message("user.login");
	m->addParam("account",acc);
	m->addParam("operation","delete");
	Engine::enqueue(m);
	return true;
    }
    else if (name == "acc_accept") {
	String newAcc;
	if (getText("acc_account",newAcc,wnd) && newAcc) {
	    String proto;
	    if (!(getSelect("acc_protocol",proto,wnd) && proto)) {
		Debug(ClientDriver::self(),DebugWarn,"No protocol is set for account '%s' in %p",newAcc.c_str(),wnd);
		return false;
	    }
	    // check if the account name has changed, delete old if so
	    if (wnd && wnd->context() && (wnd->context() != newAcc)) {
		s_accounts.clearSection(wnd->context());
		Message* m = new Message("user.login");
		m->addParam("account",wnd->context());
		m->addParam("operation","delete");
		Engine::enqueue(m);
	    }
	    if (!hasOption("accounts",newAcc))
		addOption("accounts",newAcc,false);
	    Message* m = new Message("user.login");
	    m->addParam("account",newAcc);
//	    m->addParam("operation","create");
	    s_accounts.setValue(newAcc,"protocol",proto);
	    m->addParam("protocol",proto);
	    for (const char** par = s_accParams; *par; par++) {
		String name;
		name << "acc_" << *par;
		String val;
		if (getText(name,val,wnd)) {
		    if (val.null())
			s_accounts.clearKey(newAcc,*par);
		    else {
			s_accounts.setValue(newAcc,*par,val);
			m->addParam(*par,val);
		    }
		}
	    }
	    Engine::enqueue(m);
	    s_accounts.save();
	    if (wnd)
		wnd->hide();
	    return true;
	}
    }
    // address book window actions
    else if ((name == "abk_call") || (name == "contacts")) {
	String cnt;
	if (getSelect("contacts",cnt,wnd) && cnt) {
	    NamedList* sect = s_contacts.getSection(cnt);
	    if (sect) {
		String* callto = sect->getParam("callto");
		if (!(callto && *callto))
		    callto = sect->getParam("number");
		if (callto && *callto && openConfirm("Call to "+*callto,wnd,"callto:" + *callto))
		    return true;
	    }
	}
	else
	    return false;
    }
    else if (name == "abk_new") {
	NamedList params("");
	params.setParam("abk_contact","");
	params.setParam("abk_callto","");
	params.setParam("abk_number","");
	params.setParam("modal",String::boolText(true));
	if (openPopup("addrbook",&params,wnd))
	    return true;
    }
    else if (name == "abk_edit") {
	String cnt;
	if (getSelect("contacts",cnt,wnd)) {
	    NamedList params("");
	    params.setParam("abk_contact",cnt);
	    params.setParam("abk_callto",s_contacts.getValue(cnt,"callto"));
	    params.setParam("abk_number",s_contacts.getValue(cnt,"number"));
	    params.setParam("context",cnt);
	    params.setParam("modal",String::boolText(true));
	    if (openPopup("addrbook",&params,wnd))
		return true;
	}
	else
	    return false;
    }
    else if (name == "abk_del") {
	String cnt;
	if (getSelect("contacts",cnt,wnd)) {
	    if (openConfirm("Delete contact "+cnt,wnd,name + ":" + cnt))
		return true;
	}
	else
	    return false;
    }
    else if (name.startsWith("abk_del:")) {
	String cnt = name.substr(8);
	delOption("contacts",cnt);
	s_contacts.clearSection(cnt);
	s_contacts.save();
	return true;
    }
    else if (name == "abk_accept") {
	String newAbk;
	if (getText("abk_contact",newAbk,wnd) && newAbk) {
	    // check if the contact name has changed, delete old if so
	    if (wnd && wnd->context() && (wnd->context() != newAbk)) {
		s_contacts.clearSection(wnd->context());
		delOption("contacts",wnd->context());
	    }
	    if (!hasOption("contacts",newAbk))
		addOption("contacts",newAbk,false);
	    String tmp;
	    if (getText("abk_callto",tmp,wnd))
		s_contacts.setValue(newAbk,"callto",tmp);
	    else
		s_contacts.clearKey(newAbk,"callto");
	    if (getText("abk_number",tmp,wnd))
		s_contacts.setValue(newAbk,"number",tmp);
	    else
		s_contacts.clearKey(newAbk,"callto");
	    s_contacts.save();
	    if (wnd)
		wnd->hide();
	    return true;
	}
	else
	    return false;
    }
    // outgoing (placed) call log actions
    else if (name == "log_out_clear") {
	if (clearTable("log_outgoing")) {
	    for (unsigned int i = 0; i < s_history.sections(); i++) {
		NamedList* sect = s_history.getSection(i);
		if (!sect)
		    continue;
		String* dir = sect->getParam("direction");
		// directions are backwards
		if (dir && (*dir == "incoming")) {
		    s_history.clearSection(*sect);
		    i--;
		}
	    }
	    s_history.save();
	    return true;
	}
    }
    else if ((name == "log_out_call") || (name == "log_outgoing")) {
	NamedList log("");
	if (getTableRow("log_outgoing","",&log,wnd)) {
	    String* called = log.getParam("called");
	    if (called && *called && openConfirm("Call to "+*called,wnd,"callto:" + *called))
		return true;
	}
	else
	    return false;
    }
    else if (name == "log_out_contact") {
	NamedList log("");
	if (getTableRow("log_outgoing","",&log,wnd)) {
	    String* called = log.getParam("called");
	    if (called && *called) {
		NamedList params("");
		params.setParam("abk_contact","");
		params.setParam("abk_callto","");
		params.setParam("abk_number",*called);
		params.setParam("modal",String::boolText(true));
		if (openPopup("addrbook",&params,wnd))
		    return true;
	    }
	}
	else
	    return false;
    }
    // incoming (received) call log actions
    else if (name == "log_in_clear") {
	if (clearTable("log_incoming")) {
	    for (unsigned int i = 0; i < s_history.sections(); i++) {
		NamedList* sect = s_history.getSection(i);
		if (!sect)
		    continue;
		String* dir = sect->getParam("direction");
		// directions are backwards, remember?
		if (dir && (*dir == "outgoing")) {
		    s_history.clearSection(*sect);
		    i--;
		}
	    }
	    s_history.save();
	    return true;
	}
    }
    else if ((name == "log_in_call") || (name == "log_incoming")) {
	NamedList log("");
	if (getTableRow("log_incoming","",&log,wnd)) {
	    String* caller = log.getParam("caller");
	    if (caller && *caller && openConfirm("Call to "+*caller,wnd,"callto:" + *caller))
		return true;
	}
	else
	    return false;
    }
    else if (name == "log_in_contact") {
	NamedList log("");
	if (getTableRow("log_incoming","",&log,wnd)) {
	    String* caller = log.getParam("caller");
	    if (caller && *caller) {
		NamedList params("");
		params.setParam("abk_contact","");
		params.setParam("abk_callto","");
		params.setParam("abk_number",*caller);
		params.setParam("modal",String::boolText(true));
		if (openPopup("addrbook",&params,wnd))
		    return true;
	    }
	}
	else
	    return false;
    }
    // mixed call log actions
    else if (name == "log_clear") {
	if (clearTable("log_global")) {
	    s_history.clearSection();
	    s_history.save();
	    return true;
	}
    }
    // help window actions
    else if (wnd && name.startsWith("help_")) {
	bool show = false;
	int page = wnd->context().toInteger();
	if (name == "help_home")
	    page = 0;
	else if (name == "help_prev")
	    page--;
	else if (name == "help_next")
	    page++;
	else if (name.startsWith("help_page:"))
	    page = name.substr(10).toInteger(page);
	else if (name.startsWith("help_show:")) {
	    page = name.substr(10).toInteger(page);
	    show = true;
	}
	if (page < 0)
	    page = 0;
	String helpFile = Engine::config().getValue("client","helpbase");
	if (helpFile.null())
	    helpFile << Engine::modulePath() << Engine::pathSeparator() << "help";
	if (!helpFile.endsWith(Engine::pathSeparator()))
	    helpFile << Engine::pathSeparator();
	helpFile << page << ".yhlp";
	File f;
	if (!f.openPath(helpFile)) {
	    Debug(ClientDriver::self(),DebugMild,"Could not open help file '%s'",helpFile.c_str());
	    return false;
	}
	unsigned int len = f.length();
	if (len) {
	    String helpText(' ',len);
	    int rd = f.readData(const_cast<char*>(helpText.c_str()),len);
	    if (rd == (int)len) {
		setText("help_text",helpText,wnd);
		wnd->context(page);
		if (show)
		    wnd->show();
	    }
	    else
		Debug(ClientDriver::self(),DebugWarn,"Read only %d out of %u bytes in file %s",
		    rd,len,helpFile.c_str());
	    return true;
	}
    }

    // unknown/unhandled - generate a message for them
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
    // handle the window visibility buttons, these will sync toggles themselves
    if (setVisible(name,active))
	return true;
    else if (name.startsWith("display:")) {
	if (setShow(name.substr(8),active,wnd))
	    return true;
    }
    // keep the toggle in sync in all windows
    setCheck(name,active,0,wnd);
    if (name == "autoanswer") {
	m_autoAnswer = active;
	return true;
    }
    if (name == "multilines") {
	m_multiLines = active;
	return true;
    }

    // unknown/unhandled - generate a message for them
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
    // keep the item in sync in all windows
    setSelect(name,item,0,wnd);
    if (name == "channels") {
	updateFrom(item);
	return true;
    }
    else if (name == "account") {
	if (checkDashes(item))
	    return true;
	// selecting an account unselects protocol
	if (setSelect("protocol","") || setSelect("protocol","--"))
	    return true;
    }
    else if (name == "protocol") {
	if (checkDashes(item))
	    return true;
	// selecting a protocol unselects account
	if (setSelect("account","") || setSelect("account","--"))
	    return true;
    }
    else if (name == "acc_providers") {
	// apply provider template
	if (checkDashes(item))
	    return true;
	// reset selection after we apply it
	if (!setSelect(name,""))
	    setSelect(name,"--");
	NamedList* sect = s_providers.getSection(item);
	if (!sect)
	    return false;
	setSelect("acc_protocol",sect->getValue("protocol"));
	for (const char** par = s_provParams; *par; par++) {
	    String name;
	    name << "acc_" << *par;
	    setText(name,sect->getValue(*par));
	}
	return true;
    }

    // unknown/unhandled - generate a message for them
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
    if (!driverLockLoop())
	return;
    ClientChannel* cc = static_cast<ClientChannel*>(ClientDriver::self()->find(callId));
    if (cc) {
	cc->ref();
	cc->callAnswer();
	setChannelInternal(cc);
	cc->deref();
    }
    driverUnlock();
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
    if (!driverLockLoop())
	return false;
    ClientChannel* cc = new ClientChannel(target);
    selectChannel(cc);
    Message* m = cc->message("call.route");
    driverUnlock();
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
    if (!ClientDriver::self())
	return false;
    Channel* chan = ClientDriver::self()->find(m_activeId);
    if (!chan)
	return false;
    char buf[2];
    buf[0] = digit;
    buf[1] = '\0';
    Message* m = chan->message("chan.dtmf");
    m->addParam("text",buf);
    Engine::enqueue(m);
    return true;
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
	lockOther();
	ClientChannel* cc = new ClientChannel(caller,ch->id(),msg);
	selectChannel(cc);
	unlockOther();
	if (cc->connect(ch,msg->getValue("reason"))) {
	    m_activeId = cc->id();
	    msg->setParam("peerid",m_activeId);
	    msg->setParam("targetid",m_activeId);
	    Engine::enqueue(cc->message("call.ringing",false,true));
	    lockOther();
	    // notify the UI about the call
	    String tmp("Call from:");
	    tmp << " " << caller;
	    setStatus(tmp);
	    setText("incoming",tmp);
	    String* info = msg->getParam("caller_info_uri");
	    if (info && (info->startsWith("http://",false) || info->startsWith("https://",false)))
		setText("caller_info",*info);
	    info = msg->getParam("caller_icon_uri");
	    if (info && (info->startsWith("http://",false) || info->startsWith("https://",false)))
		setText("caller_icon",*info);
	    if (m_autoAnswer) {
		cc->callAnswer();
		setChannelInternal(cc);
	    }
	    else {
		if (!(m_multiLines && setVisible("channels")))
		    setVisible("incoming");
	    }
	    unlockOther();
	    cc->deref();
	    return true;
	}
    }
    return false;
}

bool Client::callRouting(const String& caller, const String& called, Message* msg)
{
    // route here all calls by default
    return true;
}

void Client::updateCDR(const Message& msg)
{
    if (!updateCallHist(msg))
	return;
    String id = msg.getParam("billid");
    if (id.null())
	id = msg.getParam("id");
    // it worked before - but paranoia can be fun
    if (id.null())
	return;
    while (s_history.sections() >= 20) {
	NamedList* sect = s_history.getSection(0);
	if (!sect)
	    break;
	s_history.clearSection(*sect);
    }
    unsigned int n = msg.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = msg.getParam(i);
	if (!param)
	    continue;
	s_history.setValue(id,param->name(),param->c_str());
    }
    s_history.save();
}

bool Client::updateCallHist(const NamedList& params)
{
    String* dir = params.getParam("direction");
    if (!dir)
	return false;
    String* id = params.getParam("billid");
    if (!id || id->null())
	id = params.getParam("id");
    if (!id || id->null())
	return false;
    String table;
    // remember, directions are opposite of what the user expects
    if (*dir == "outgoing")
	table = "log_incoming";
    else if (*dir == "incoming")
	table = "log_outgoing";
    else
	return false;
    bool ok = addTableRow(table,*id,&params);
    ok = addTableRow("log_global",*id,&params) || ok;
    return ok;
}

void Client::clearActive(const String& id)
{
    if (id == m_activeId)
	updateFrom(0);
}

void Client::addChannel(ClientChannel* chan)
{
    addOption("channels",chan->id(),false,chan->description());
}

void Client::setChannel(ClientChannel* chan)
{
    Debug(ClientDriver::self(),DebugAll,"setChannel %p",chan);
    if (!chan)
	return;
    lockOther();
    setChannelInternal(chan);
    unlockOther();
}

void Client::setChannelInternal(ClientChannel* chan)
{
    setChannelDisplay(chan);
    bool upd = !m_multiLines;
    if (!upd) {
	String tmp;
	upd = getSelect("channels",tmp) && (tmp == chan->id());
    }
    if (upd)
	updateFrom(chan);
}

void Client::setChannelDisplay(ClientChannel* chan)
{
    String tmp(chan->description());
    if (!setUrgent(chan->id(),chan->flashing()) && chan->flashing())
	tmp << " <<<";
    setText(chan->id(),tmp);
}

void Client::delChannel(ClientChannel* chan)
{
    lockOther();
    clearActive(chan->id());
    delOption("channels",chan->id());
    unlockOther();
}

void Client::selectChannel(ClientChannel* chan, bool force)
{
    if (!chan)
	return;
    if (force || m_activeId.null()) {
	setSelect("channels",chan->id());
	updateFrom(chan);
    }
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
    m_activeId = chan ? chan->id().c_str() : "";
    enableAction(chan,"accept");
    enableAction(chan,"reject");
    enableAction(chan,"hangup");
    enableAction(chan,"voicemail");
    enableAction(chan,"transfer");
    enableAction(chan,"conference");
    setActive("call",m_multiLines || m_activeId.null());
}

void Client::enableAction(const ClientChannel* chan, const String& action)
{
    setActive(action,chan && chan->enableAction(action));
}

void Client::idleActions()
{
    // arbitrary limit to let other threads run too
    for (int i = 0; i < 4; i++) {
	if (!s_busy)
	    return;
	ClientThreadProxy* tmp = s_proxy;
	s_proxy = 0;
	if (!tmp)
	    return;
	tmp->process();
    }
}

bool Client::driverLock(long maxwait)
{
    if (maxwait < 0)
	maxwait = 0;
    return ClientDriver::self() && ClientDriver::self()->lock(maxwait);
}

bool Client::driverLockLoop()
{
    if (!(isCurrent() && ClientDriver::self()))
	return false;

    while (!driverLock()) {
	if (Engine::exiting() || !ClientDriver::self())
	    return false;
	idleActions();
	yield();
    }
    return true;
}

void Client::driverUnlock()
{
    if (ClientDriver::self())
	ClientDriver::self()->unlock();
}


bool UICdrHandler::received(Message &msg)
{
    if (!Client::self())
	return false;

    String* op = msg.getParam("operation");
    if (!(op && (*op == "finalize")))
	return false;
    op = msg.getParam("chan");
    if (!(op && op->startsWith("client/",false)))
	return false;

    // block until client finishes initialization
    while (!Client::self()->initialized())
	Thread::msleep(10);

    Client::self()->updateCDR(msg);
    return false;
}


bool UIHandler::received(Message &msg)
{
    if (!Client::self())
	return false;
    String action(msg.getValue("action"));
    if (action.null())
	return false;

    // block until client finishes initialization
    while (!Client::self()->initialized())
	Thread::msleep(10);

    Window* wnd = Client::getWindow(msg.getValue("window"));
    if (action == "set_status")
	return Client::self()->setStatusLocked(msg.getValue("status"),wnd);
    else if (action == "show_message") {
	Client::self()->lockOther();
	bool ok = Client::openMessage(msg.getValue("text"),Client::getWindow(msg.getValue("parent")),msg.getValue("context"));
	Client::self()->unlockOther();
	return ok;
    }
    else if (action == "show_confirm") {
	Client::self()->lockOther();
	bool ok = Client::openConfirm(msg.getValue("text"),Client::getWindow(msg.getValue("parent")),msg.getValue("context"));
	Client::self()->unlockOther();
	return ok;
    }
    String name(msg.getValue("name"));
    if (name.null())
	return false;
    DDebug(ClientDriver::self(),DebugAll,"UI action '%s' on '%s' in %p",
	action.c_str(),name.c_str(),wnd);
    bool ok = false;
    Client::self()->lockOther();
    if (action == "set_text")
	ok = Client::self()->setText(name,msg.getValue("text"),wnd);
    else if (action == "set_toggle")
	ok = Client::self()->setCheck(name,msg.getBoolValue("active"),wnd);
    else if (action == "set_select")
	ok = Client::self()->setSelect(name,msg.getValue("item"),wnd);
    else if (action == "set_active")
	ok = Client::self()->setActive(name,msg.getBoolValue("active"),wnd);
    else if (action == "set_focus")
	ok = Client::self()->setFocus(name,msg.getBoolValue("select"),wnd);
    else if (action == "set_visible")
	ok = Client::self()->setShow(name,msg.getBoolValue("visible"),wnd);
    else if (action == "has_option")
	ok = Client::self()->hasOption(name,msg.getValue("item"),wnd);
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
    Client::self()->unlockOther();
    return ok;
}


bool UIUserHandler::received(Message &msg)
{
    if (!Client::self())
	return false;
    String account = msg.getValue("account");
    if (account.null())
	return false;

    // block until client finishes initialization
    while (!Client::self()->initialized())
	Thread::msleep(10);

    Client::self()->lockOther();
    String op = msg.getParam("operation");
    if ((op == "create") || (op == "login") || op.null()) {
	if (!Client::self()->hasOption("account",account))
	    Client::self()->addOption("account",account,false);
    }
    else if (op == "delete") {
	Client::self()->delOption("account",account);
	Client::self()->delOption("accounts",account);
    }
    Client::self()->unlockOther();
    return false;
}


// IMPORTANT: having a target means "from inside Yate to the user"
//  An user initiated call must be incoming (no target)
ClientChannel::ClientChannel(const String& party, const char* target, const Message* msg)
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
    Message* s = message("chan.startup");
    if (msg) {
	s->setParam("caller",msg->getValue("caller"));
	s->setParam("called",msg->getValue("called"));
	s->setParam("billid",msg->getValue("billid"));
    }
    Engine::enqueue(s);
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
    installRelay(Route,200);
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
	Client::self()->lockOther();
	ObjList* l = &channels();
	for (; l; l = l->next()) {
	    ClientChannel* cc = static_cast<ClientChannel*>(l->get());
	    if (cc) {
		cc->update(false);
		Client::self()->setChannelInternal(cc);
	    }
	}
	Client::self()->unlockOther();
    }
}

bool ClientDriver::msgRoute(Message& msg)
{
    // don't route here our own calls
    if (name() == msg.getValue("module"))
	return false;
    if (Client::self() && Client::self()->callRouting(msg.getValue("caller"),msg.getValue("called"),&msg)) {
	msg.retValue() = name() + "/*";
	return true;
    }
    return false;
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
