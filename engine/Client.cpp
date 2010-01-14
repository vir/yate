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

#include <stdio.h>

using namespace TelEngine;

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
	getOptions,
	addTableRow,
	setMultipleRows,
	insertTableRow,
	delTableRow,
	setTableRow,
	getTableRow,
	updateTableRow,
	updateTableRows,
	clearTable,
	getText,
	getCheck,
	getSelect,
	createWindow,
	closeWindow,
	setParams,
	addLines,
	createObject,
	setProperty,
	getProperty,
	openUrl
    };
    ClientThreadProxy(int func, const String& name, bool show, Window* wnd = 0, Window* skip = 0);
    ClientThreadProxy(int func, const String& name, const String& text, Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, const String& text,
	const String& item, bool show, Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, String* rtext, bool* rbool,
	Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, const NamedList* params,
	const Window* parent);
    ClientThreadProxy(int func, const String& name, const String& item, bool start,
	const NamedList* params, Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, const NamedList* params,
	Window* wnd, Window* skip);
    ClientThreadProxy(int func, const String& name, const NamedList* params,
	unsigned int uintVal, bool atStart, Window* wnd, Window* skip);
    ClientThreadProxy(int func, void** addr, const String& name, const String& text, const NamedList* params);
    ClientThreadProxy(int func, const String& name, const String& text,
	const String& item, const NamedList* params, Window* wnd, Window* skip);
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
    unsigned int m_uint;
    void** m_pointer;
};

// holder for a postponed message
class PostponedMessage : public Message
{
public:
    inline PostponedMessage(const Message& msg, int id, bool copyUserData)
	: Message(msg), m_id(id) {
	    if (copyUserData)
		userData(msg.userData());
	}
    inline int id() const
	{ return m_id; }
private:
    int m_id;
};

// engine.start message handler used to notify logics
class EngineStartHandler : public MessageHandler
{
public:
    inline EngineStartHandler(unsigned int prio = 100)
	: MessageHandler("engine.start",prio)
	{}
    virtual bool received(Message& msg);
};


/**
 * Static classes/function/data
 */

// Struct used to build client relays array
struct MsgRelay
{
    const char* name;
    int id;
    int prio;
};

// List of window params prefix handled in setParams()
static String s_wndParamPrefix[] = {"show:","active:","focus:","check:","select:","display:",""};
// Error messages returned by channels
static String s_userBusy = "User busy";
static String s_rejectReason = "Rejected";
static String s_hangupReason = "User hangup";
static String s_cancelReason = "Cancelled";
static unsigned int s_eventLen = 0;              // Log maximum lines (0: unlimited)
static Mutex s_debugMutex(false,"ClientDebug");
static Mutex s_proxyMutex(false,"ClientProxy");
static Mutex s_postponeMutex(false,"ClientPostpone");
static ObjList s_postponed;
static NamedList* s_debugLog = 0;
static ClientThreadProxy* s_proxy = 0;
static bool s_busy = false;
static String s_incomingUrlParam;                // Incoming URL param in call.execute message
Client* Client::s_client = 0;
Configuration Client::s_settings;                // Client settings
Configuration Client::s_actions;                 // Logic preferrences
Configuration Client::s_accounts;                // Accounts
Configuration Client::s_contacts;                // Contacts
Configuration Client::s_providers;               // Provider settings
Configuration Client::s_history;                 // Call log
Configuration Client::s_calltoHistory;           // Dialed destinations history
int Client::s_changing = 0;
Regexp Client::s_notSelected = "^-\\(.*\\)-$";   // Holds a not selected/set value match
ObjList Client::s_logics;
String Client::s_skinPath;                       // Skin path
String Client::s_soundPath;                      // Sounds path
String Client::s_ringInName = "defaultringin";   // Ring name for incoming channels
String Client::s_ringOutName = "defaultringout"; // Ring name for outgoing channels
String Client::s_statusWidget = "status";        // Status widget's name
String Client::s_debugWidget = "log_events";     // Default widget displaying the debug text
// The list of client's toggles
String Client::s_toggles[OptCount] = {
    "multilines", "autoanswer", "ringincoming", "ringoutgoing",
    "activatelastoutcall", "activatelastincall", "activatecallonselect",
    "display_keypad", "openincomingurl"
};
bool Client::s_idleLogicsTick = false;           // Call logics' timerTick()
bool Client::s_exiting = false;                  // Client exiting flag
ClientDriver* ClientDriver::s_driver = 0;
String ClientDriver::s_confName = "conf/client"; // The name of the client's conference room
bool ClientDriver::s_dropConfPeer = true;        // Drop a channel's old peer when terminated while in conference
String ClientDriver::s_device;                   // Currently used audio device
ObjList ClientSound::s_sounds;                   // ClientSound's list
Mutex ClientSound::s_soundsMutex(true,"ClientSound"); // ClientSound's list lock mutex
String ClientSound::s_calltoPrefix = "wave/play/"; // Client sound target prefix

// Client relays
static MsgRelay s_relays[] = {
    {"call.cdr",           Client::CallCdr,           90},
    {"ui.action",          Client::UiAction,          150},
    {"user.login",         Client::UserLogin,         50},
    {"user.notify",        Client::UserNotify,        50},
    {"resource.notify",    Client::ResourceNotify,    50},
    {"resource.subscribe", Client::ResourceSubscribe, 50},
    {"clientchan.update",  Client::ClientChanUpdate,  50},
    {0,0,0},
};

// Channel notifications
TokenDict ClientChannel::s_notification[] = {
    {"startup",         ClientChannel::Startup},
    {"destroyed",       ClientChannel::Destroyed},
    {"active",          ClientChannel::Active},
    {"onhold",          ClientChannel::OnHold},
    {"noticed",         ClientChannel::Noticed},
    {"addresschanged",  ClientChannel::AddrChanged},
    {"routed",          ClientChannel::Routed},
    {"accepted",        ClientChannel::Accepted},
    {"rejected",        ClientChannel::Rejected},
    {"progressing",     ClientChannel::Progressing},
    {"ringing",         ClientChannel::Ringing},
    {"answered",        ClientChannel::Answered},
    {"transfer",        ClientChannel::Transfer},
    {"conference",      ClientChannel::Conference},
    {0,0}
};

String ClientContact::s_chatPrefix = "chat";     // Client contact chat window prefix


// Debug output handler
static void dbg_client_func(const char* buf, int level)
{
    if (!buf)
	return;
    Lock lock(s_debugMutex);
    if (!s_debugLog)
	s_debugLog = new NamedList("");
    s_debugLog->addParam(buf,String(level));
}

// Utility function used in Client::action()
// Output a debug message and calls a logic's action method
inline bool callLogicAction(ClientLogic* logic, Window* wnd, const String& name, NamedList* params)
{
    if (!logic)
	return false;
    DDebug(ClientDriver::self(),DebugAll,
	"Logic(%s) action='%s' in window (%p,%s) [%p]",
	logic->toString().c_str(),name.c_str(),wnd,wnd ? wnd->id().c_str() : "",logic);
    return logic->action(wnd,name,params);
}

// Utility function used in Client::toggle()
// Output a debug message and calls a logic's toggle method
inline bool callLogicToggle(ClientLogic* logic, Window* wnd, const String& name, bool active)
{
    if (!logic)
	return false;
    DDebug(ClientDriver::self(),DebugAll,
	"Logic(%s) toggle='%s' active=%s in window (%p,%s) [%p]",
	logic->toString().c_str(),name.c_str(),String::boolText(active),
	wnd,wnd ? wnd->id().c_str() : "",logic);
    return logic->toggle(wnd,name,active);
}

// Utility function used in Client::select()
// Output a debug message and calls a logic's select method
inline bool callLogicSelect(ClientLogic* logic, Window* wnd, const String& name,
    const String& item, const String& text)
{
    if (!logic)
	return false;
    DDebug(ClientDriver::self(),DebugAll,
	"Logic(%s) select='%s' item='%s' in window (%p,%s) [%p]",
	logic->toString().c_str(),name.c_str(),item.c_str(),
	wnd,wnd ? wnd->id().c_str() : "",logic);
    return logic->select(wnd,name,item,text);
}

// Utility function used to check for action/toggle/select preferences
// Check for a substitute
// Check if only a logic should process the action 
// Check for a preffered logic to process the action 
// Check if a logic should be ignored (not notified)
// Otherwise: check if the action should be ignored
inline bool hasOverride(const NamedList* params, String& name, String& handle,
    bool& only, bool& prefer, bool& ignore, bool& bailout)
{
    static String s_ignoreString = "ignore";

    if (!params)
	return false;
    handle = params->getValue(name);
    // Set name if a substitute is found
    if (handle.startSkip("sameas:",false)) {
	const char* tmp = params->getValue(handle);
	if (tmp) {
	    name = handle;
	    handle = tmp;
	}
	else
	    handle = "";
    }
    // Check logic indications
    if (!handle)
	return false;
    only = handle.startSkip("only:",false);
    if (only)
	return true;
    prefer = handle.startSkip("prefer:",false);
    ignore = !prefer && handle.startSkip("ignore:",false);
    bailout = !ignore && handle == s_ignoreString;
    return true;
}


/**
 * Window
 */
// Constructor with the specified id
Window::Window(const char* id)
    : m_id(id), m_visible(false), m_master(false), m_popup(false),
    m_saveOnClose(true), m_populated(false), m_initialized(false)
{
    DDebug(ClientDriver::self(),DebugAll,"Window(%s) created",m_id.c_str());
}

// destructor
Window::~Window()
{
    if (Client::self())
	Client::self()->m_windows.remove(this,false);
    DDebug(ClientDriver::self(),DebugAll,"Window(%s) destroyed",m_id.c_str());
}

// retrieve the window id
const String& Window::toString() const
{
    return m_id;
}

// set the window title
void Window::title(const String& text)
{
    m_title = text;
}

// set the window context
void Window::context(const String& text)
{
    m_context = text;
}

// checkes if this window is related to the given window
bool Window::related(const Window* wnd) const
{
    if ((wnd == this) || !wnd || wnd->master())
	return false;
    return true;
}

// function for interpreting a set or parameters and take appropiate action
// maybe not needed anymore?
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
	    else if (n.startSkip("show:",false) || n.startSkip("display:",false))
		ok = setShow(n,s->toBoolean()) && ok;
	    else if (n.startSkip("active:",false))
		ok = setActive(n,s->toBoolean()) && ok;
	    else if (n.startSkip("focus:",false))
		ok = setFocus(n,s->toBoolean()) && ok;
	    else if (n.startSkip("check:",false))
		ok = setCheck(n,s->toBoolean()) && ok;
	    else if (n.startSkip("select:",false))
		ok = setSelect(n,*s) && ok;
	    else if (n.startSkip("property:",false)) {
		// Set property: object_name:property_name=value
		int pos = n.find(':');
		if (pos > 0)
		    ok = setProperty(n.substr(0,pos),n.substr(pos + 1),*s) && ok;
		else
		    ok = false;
	    }
	    else if (n.find(':') < 0)
		ok = setText(n,*s) && ok;
	    else
		ok = false;
	}
    }
    return ok;
}

// Append or insert text lines to a widget
bool Window::addLines(const String& name, const NamedList* lines, unsigned int max,
	bool atStart)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub addLines('%s',%p,%u,%s) [%p]",
	name.c_str(),lines,max,String::boolText(atStart),this);
    return false;
}

// stub function for adding a row to a table
bool Window::addTableRow(const String& name, const String& item, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub addTableRow('%s','%s',%p,%s) [%p]",
	name.c_str(),item.c_str(),data,String::boolText(atStart),this);
    return false;
}

// stub function for setting multiple lines at once
bool Window::setMultipleRows(const String& name, const NamedList& data, const String& prefix)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub setMultipleRows('%s',%p,'%s') [%p]",
	name.c_str(),&data,prefix.c_str(),this);
    return false;
}

// stub function for inserting a row to a table
bool Window::insertTableRow(const String& name, const String& item,
    const String& before, const NamedList* data)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub inserTableRow('%s','%s','%s',%p) [%p]",
	name.c_str(),item.c_str(),before.c_str(),data,this);
    return false;
}

// stub function for deleting a row from a table
bool Window::delTableRow(const String& name, const String& item)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub delTableRow('%s','%s') [%p]",
	name.c_str(),item.c_str(),this);
    return false;
}

// stub function for setting the value for a row
bool Window::setTableRow(const String& name, const String& item, const NamedList* data)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub setTableRow('%s','%s',%p) [%p]",
	name.c_str(),item.c_str(),data,this);
    return false;
}

// Set a table row or add a new one if not found
bool Window::updateTableRow(const String& name, const String& item,
    const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub updateTableRow('%s','%s',%p,%s) [%p]",
	name.c_str(),item.c_str(),data,String::boolText(atStart),this);
    return false;
}

// Add or set one or more table row(s). Screen update is locked while changing the table.
// Each data list element is a NamedPointer carrying a NamedList with item parameters.
// The name of an element is the item to update. Set element's value to 'false'
//  to avoid adding a new item.
bool Window::updateTableRows(const String& name, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub updateTableRows('%s',%p,%s) [%p]",
	name.c_str(),data,String::boolText(atStart),this);
    return false;
}

// stub function for retrieving the information from a row
bool Window::getTableRow(const String& name, const String& item, NamedList* data)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub getTableRow('%s','%s',%p) [%p]",
	name.c_str(),item.c_str(),data,this);
    return false;
}

// stub function for clearing a table
bool Window::clearTable(const String& name)
{
    DDebug(ClientDriver::self(),DebugInfo,"stub clearTable('%s') [%p]",
	name.c_str(),this);
    return false;
}

// Check window param prefix
bool Window::isValidParamPrefix(const String& prefix)
{
    for (int i = 0; s_wndParamPrefix[i].length(); i++)
	if (prefix.startsWith(s_wndParamPrefix[i]))
	    return prefix.length() > s_wndParamPrefix[i].length();
    return false;
}


/**
 * UIFactory
 */

ObjList UIFactory::s_factories;

// Constructor. Append itself to the factories list
UIFactory::UIFactory(const char* name)
    : String(name)
{
    s_factories.append(this)->setDelete(false);
    Debug(ClientDriver::self(),DebugAll,"Added factory '%s' [%p]",name,this);
}

// Destructor
UIFactory::~UIFactory()
{
    s_factories.remove(this,false);
    Debug(ClientDriver::self(),DebugAll,"Removed factory '%s' [%p]",c_str(),this);
}

// Ask all factories to create an object of a given type
void* UIFactory::build(const String& type, const char* name, NamedList* params,
	const char* factory)
{
    for (ObjList* o = s_factories.skipNull(); o; o = o->skipNext()) {
	UIFactory* f = static_cast<UIFactory*>(o->get());
	if (!f->canBuild(type) || (factory && *f != factory))
	    continue;
	DDebug(ClientDriver::self(),DebugAll,
	    "Factory '%s' trying to create type='%s' name='%s' [%p]",
	    f->c_str(),type.c_str(),name,f);
	void* p = f->create(type,name,params);
	if (p)
	    return p;
    }
    return 0;
}


/**
 * ClientThreadProxy
 */
ClientThreadProxy::ClientThreadProxy(int func, const String& name, bool show,
	Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(show), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(0), m_uint(0), m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const String& text,
	Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_text(text), m_bool(false), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(0), m_uint(0), m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const String& text,
	const String& item, bool show, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_text(text), m_item(item), m_bool(show), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(0), m_uint(0), m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, String* rtext,
	bool* rbool, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(false), m_rtext(rtext), m_rbool(rbool),
      m_wnd(wnd), m_skip(skip), m_params(0), m_uint(0), m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const NamedList* params,
	const Window* parent)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(false), m_rtext(0), m_rbool(0),
      m_wnd(const_cast<Window*>(parent)), m_skip(0), m_params(params), m_uint(0),
      m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const String& item,
	bool start, const NamedList* params, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_item(item), m_bool(start), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(params), m_uint(0), m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const NamedList* params,
	Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(false), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(params), m_uint(0), m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const NamedList* params,
	unsigned int uintVal, bool atStart, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_bool(atStart), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(params), m_uint(uintVal), m_pointer(0)
{
}

ClientThreadProxy::ClientThreadProxy(int func, void** addr, const String& name,
    const String& text, const NamedList* params)
    : m_func(func), m_rval(false),
      m_name(name), m_text(text), m_bool(false), m_rtext(0), m_rbool(0),
      m_wnd(0), m_skip(0), m_params(params), m_uint(0), m_pointer(addr)
{
}

ClientThreadProxy::ClientThreadProxy(int func, const String& name, const String& text,
    const String& item, const NamedList* params, Window* wnd, Window* skip)
    : m_func(func), m_rval(false),
      m_name(name), m_text(text), m_item(item), m_bool(false), m_rtext(0), m_rbool(0),
      m_wnd(wnd), m_skip(skip), m_params(params), m_uint(0), m_pointer(0)
{
}

void ClientThreadProxy::process()
{
    XDebug(DebugAll,"ClientThreadProxy::process() %d [%p]",m_func,this);
    Client* client = Client::self();
    if (!client || Client::exiting()) {
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
	    m_rval = client->setText(m_name,m_text,m_bool,m_wnd,m_skip);
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
	case setMultipleRows:
	    m_rval = client->setMultipleRows(m_name,*m_params,m_item,m_wnd,m_skip);
	    break;
	case insertTableRow:
	    m_rval = client->insertTableRow(m_name,m_item,m_text,m_params,m_wnd,m_skip);
	    break;
	case delTableRow:
	    m_rval = client->delTableRow(m_name,m_text,m_wnd,m_skip);
	    break;
	case setTableRow:
	    m_rval = client->setTableRow(m_name,m_item,m_params,m_wnd,m_skip);
	    break;
	case updateTableRow:
	    m_rval = client->updateTableRow(m_name,m_item,m_params,m_bool,m_wnd,m_skip);
	    break;
	case updateTableRows:
	    m_rval = client->updateTableRows(m_name,m_params,m_bool,m_wnd,m_skip);
	    break;
	case getTableRow:
	    m_rval = client->getTableRow(m_name,m_item,const_cast<NamedList*>(m_params),m_wnd,m_skip);
	    break;
	case clearTable:
	    m_rval = client->clearTable(m_name,m_wnd,m_skip);
	    break;
	case getText:
	    m_rval = client->getText(m_name,*m_rtext,m_rbool ? *m_rbool : false,m_wnd,m_skip);
	    break;
	case getCheck:
	    m_rval = client->getCheck(m_name,*m_rbool,m_wnd,m_skip);
	    break;
	case getSelect:
	    m_rval = client->getSelect(m_name,*m_rtext,m_wnd,m_skip);
	    break;
	case getOptions:
	    m_rval = client->getOptions(m_name,const_cast<NamedList*>(m_params),m_wnd,m_skip);
	    break;
	case createWindow:
	    m_rval = client->createWindowSafe(m_name,m_text);
	    break;
	case closeWindow:
	    m_rval = client->closeWindow(m_name,m_bool);
	    break;
	case setParams:
	    m_rval = client->setParams(m_params,m_wnd,m_skip);
	    break;
	case addLines:
	    m_rval = client->addLines(m_name,m_params,m_uint,
		m_bool,m_wnd,m_skip);
	    break;
	case createObject:
	    m_rval = client->createObject(m_pointer,m_name,m_text,const_cast<NamedList*>(m_params));
	    break;
	case setProperty:
	    m_rval = client->setProperty(m_name,m_item,m_text,m_wnd,m_skip);
	    break;
	case getProperty:
	    m_rval = client->getProperty(m_name,m_item,m_text,m_wnd,m_skip);
	    break;
	case openUrl:
	    m_rval = client->openUrl(m_name);
	    break;
    }
    s_busy = false;
}

bool ClientThreadProxy::execute()
{
    XDebug(DebugAll,"ClientThreadProxy::execute() %d in %p [%p]",
	m_func,Thread::current(),this);
    s_proxyMutex.lock();
    s_proxy = this;
    s_busy = true;
    while (s_busy)
	Thread::yield();
    s_proxyMutex.unlock();
    return m_rval;
}


/**
 * EngineStartHandler
 */
// Notify logics
bool EngineStartHandler::received(Message& msg)
{
    while (!Client::self())
	Thread::yield(true);
    Client::self()->engineStart(msg);
    return false;
}


/**
 * Client
 */
// Constructor
Client::Client(const char *name)
    : Thread(name), m_initialized(false), m_line(0), m_oneThread(true),
    m_defaultLogic(0)
{
    s_client = this;

    // Set default options
    for (unsigned int i = 0; i < OptCount; i++)
	m_toggles[i] = false;
    m_toggles[OptMultiLines] = true;
    m_toggles[OptKeypadVisible] = true;
    s_incomingUrlParam = Engine::config().getValue("client","incomingcallurlparam",
	"caller_info_uri");

    // Install relays
    for (int i = 0; s_relays[i].name; i++)
	installRelay(s_relays[i].name,s_relays[i].id,s_relays[i].prio);

    // Set paths
    s_skinPath = Engine::config().getValue("client","skinbase");
    if (!s_skinPath)
	s_skinPath << Engine::sharedPath() << Engine::pathSeparator() << "skins";
    s_skinPath << Engine::pathSeparator();
    String skin(Engine::config().getValue("client","skin","default")); 
    if (skin)
	s_skinPath << skin;
    if (!s_skinPath.endsWith(Engine::pathSeparator()))
	s_skinPath << Engine::pathSeparator();
    s_soundPath << Engine::sharedPath() << Engine::pathSeparator() << "sounds" <<
	Engine::pathSeparator();
}

// destructor
Client::~Client()
{
    // Halt the engine
    Engine::halt(0);
}

// Cleanup before halting
void Client::cleanup()
{
    for (ObjList* o = m_relays.skipNull(); o; o = o->skipNext())
	Engine::uninstall(static_cast<MessageRelay*>(o->get()));
    m_relays.clear();
    ClientSound::s_soundsMutex.lock();
    ClientSound::s_sounds.clear();
    ClientSound::s_soundsMutex.unlock();
    m_windows.clear();
    s_client = 0;
    m_oneThread = false;
    do
	idleActions();
    while (ClientDriver::self() && !ClientDriver::self()->check(100000));
}

// Load windows and optionally (re)initialize the client's options
void Client::loadUI(const char* file, bool init)
{
    Debug(ClientDriver::self(),DebugAll,"Client::loadUI() [%p]",this);
    loadWindows(file);
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	Debug(ClientDriver::self(),DebugAll,"Logic(%s) loadedWindows() [%p]",
	    logic->toString().c_str(),logic);
	logic->loadedWindows();
    }
    initWindows();
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	Debug(ClientDriver::self(),DebugAll,"Logic(%s) initializedWindows() [%p]",
	    logic->toString().c_str(),logic);
	logic->initializedWindows();
    }
    if (init) {
	m_initialized = false;
	initClient();
	for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	    ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	    Debug(ClientDriver::self(),DebugAll,"Logic(%s) initializedClient() [%p]",
		logic->toString().c_str(),logic);
	    if (logic->initializedClient())
		break;
	}
	String greeting = Engine::config().getValue("client","greeting","Yate ${version} - ${release}");
	Engine::runParams().replaceParams(greeting);
	if (greeting)
	    setStatus(greeting);
	m_initialized = true;
    }
    // Sanity check: at least one window should be visible
    ObjList* o = m_windows.skipNull();
    for (; o && !getVisible(o->get()->toString()); o = o->skipNext())
	;
    if ((Engine::mode() == Engine::Client) && !o)
	Debug(ClientDriver::self(),DebugWarn,"There is no window visible !!!");
}

// run function for the main thread
void Client::run()
{
    Debug(ClientDriver::self(),DebugAll,"Client::run() [%p]",this);
    ClientLogic::initStaticData();
    m_defaultLogic = createDefaultLogic();
    loadUI();
    // Run
    main();
    s_exiting = true;
    // Drop all calls
    ClientDriver::dropCalls();
    // Notify termination to logics
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	Debug(ClientDriver::self(),DebugAll,"Logic(%s) exitingClient() [%p]",
	    logic->toString().c_str(),logic);
	logic->exitingClient();
    }
    // Make sure we drop all channels whose peers are not client channels
    Message m("call.drop");
    m.addParam("reason","shutdown");
    Engine::dispatch(m);
    TelEngine::destruct(m_defaultLogic);
    exitClient();
}

// retrieve the window named by the value of "name" from the client's list of windows 
Window* Client::getWindow(const String& name)
{
    if (!valid())
	return 0;
    ObjList* l = s_client->m_windows.find(name);
    return static_cast<Window*>(l ? l->get() : 0);
}

// function for obtaining a list of all windows that the client uses
ObjList* Client::listWindows()
{
    if (!valid())
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

// Open an URL (link) in the client's thread
bool Client::openUrlSafe(const String& url)
{
    if (!valid())
	return false;
    if (s_client->needProxy()) {
	// The 'false' parameter is here because there is no constructor with name only
	ClientThreadProxy proxy(ClientThreadProxy::openUrl,url,false);
	return proxy.execute();
    }
    return openUrl(url);
}

// function for setting the visibility attribute of the "name" window
bool Client::setVisible(const String& name, bool show)
{
    if (!valid())
	return false;
    if (s_client->needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setVisible,name,show);
	return proxy.execute();
    }
    Window* w = getWindow(name);
    if (!w)
	return false;
    w->visible(show);
    return true;
}

// function for obtaining the visibility status of the "name" window
bool Client::getVisible(const String& name)
{
    Window* w = getWindow(name);
    return w && w->visible();
}

// Create the default logic
ClientLogic* Client::createDefaultLogic()
{
    return new DefaultLogic;
}

// function for initiating the windows
void Client::initWindows()
{
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	Window* w = static_cast<Window*>(l->get());
	if (w)
	    w->init();
    }
}

// function for initializing the client
void Client::initClient()
{
    s_eventLen = Engine::config().getIntValue("client","eventlen",10240);
    if (s_eventLen > 65535)
	s_eventLen = 65535;
    else if (s_eventLen && (s_eventLen < 1024))
	s_eventLen = 1024;

    // Load the settings file
    s_settings = Engine::configFile("client_settings",true);
    s_settings.load();

    // Load the accounts file and notify logics
    s_accounts = Engine::configFile("client_accounts",true);
    s_accounts.load();
    unsigned int n = s_accounts.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_accounts.getSection(i);
	if (!sect)
	    continue;
	for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	    ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	    if (logic->updateAccount(*sect,sect->getBoolValue("enabled",true),false))
		break;
	}
    }

    // Load the contacts file and notify logics
    s_contacts = Engine::configFile("client_contacts",true);
    s_contacts.load();
    n = s_contacts.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_contacts.getSection(i);
	if (!sect)
	    continue;
	// Make sure we have a name
	if (!sect->getParam("name"))
	    sect->addParam("name",*sect);
	for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext())
	    if ((static_cast<ClientLogic*>(o->get()))->updateContact(*sect,false,true))
		break;
    }

    // Load the providers file and notify logics
    s_providers = Engine::configFile("providers");
    s_providers.load();
    n = s_providers.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_providers.getSection(i);
	if (!sect)
	    continue;
	for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext())
	    if ((static_cast<ClientLogic*>(o->get()))->updateProviders(*sect,false,true))
		break;
    }

    // Load the log file and notify logics
    s_history = Engine::configFile("client_history",true);
    s_history.load();
    n = s_history.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_history.getSection(i);
	if (!sect)
	    continue;
	for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext())
	    if ((static_cast<ClientLogic*>(o->get()))->callLogUpdate(*sect,false,true))
		break;
    }

    // Load the callto history
    s_calltoHistory = Engine::configFile("client_calltohistory",true);
    s_calltoHistory.load();
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext())
	if ((static_cast<ClientLogic*>(o->get()))->calltoLoaded())
	    break;
}

// function for moving simultaneously two related windows
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

// function for opening the pop-up window that has the id "name" with the given parameters
bool Client::openPopup(const String& name, const NamedList* params, const Window* parent)
{   
    if (!valid())
	return false;
    if (s_client->needProxy()) {
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

// function for opening a message type pop-up window with the given text, parent, context
bool Client::openMessage(const char* text, const Window* parent, const char* context)
{
    NamedList params("");
    params.addParam("text",text);
    params.addParam("modal",String::boolText(parent != 0));
    if (!null(context))
	params.addParam("context",context);
    return openPopup("message",&params,parent);
}

// function for opening a confirm type pop-up window with the given text, parent, context
bool Client::openConfirm(const char* text, const Window* parent, const char* context)
{
    NamedList params("");
    params.addParam("text",text);
    params.addParam("modal",String::boolText(parent != 0));
    if (!null(context))
	params.addParam("context",context);
    return openPopup("confirm",&params,parent);
}

// check if the window has a widget named "name"
bool Client::hasElement(const String& name, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for controlling the visibility attribute of the "name" widget from the window given as a parameter
// if no window is given, we search for it 
bool Client::setShow(const String& name, bool visible, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for controlling the enabled attribute of the "name" widget from the "wnd" window
bool Client::setActive(const String& name, bool active, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for controlling the focus attribute of the "name" widget from the "wnd" window
bool Client::setFocus(const String& name, bool select, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for setting the text of the widget identified by "name"
bool Client::setText(const String& name, const String& text, bool richText,
    Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setText,name,text,"",richText,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setText(name,text,richText);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->setText(name,text,richText) || ok;
    }
    --s_changing;
    return ok;
}

// function that controls the checked attribute of checkable widgets
bool Client::setCheck(const String& name, bool checked, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for selecting the widget named "name" from the "wnd" window if given, else look for the widget
bool Client::setSelect(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for handling an action that requires immediate action on the "name" widget
bool Client::setUrgent(const String& name, bool urgent, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for checking if the "name" widget has the specified item
bool Client::hasOption(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for adding a new option to the "name" widget from the "wnd" window, if given
bool Client::addOption(const String& name, const String& item, bool atStart, const String& text, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for deleting an option from the "name" widget from the "wnd" window, if given
bool Client::delOption(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// Get an element's items
bool Client::getOptions(const String& name, NamedList* items,
	Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getOptions,name,items,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->getOptions(name,items);
    ++s_changing;
    bool ok = false;
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->getOptions(name,items) || ok;
    }
    --s_changing;
    return ok;
}

// Append or insert text lines to a widget
bool Client::addLines(const String& name, const NamedList* lines, unsigned int max, 
	bool atStart, Window* wnd, Window* skip)
{
    if (!(lines && valid()))
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::addLines,name,lines,max,atStart,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->addLines(name,lines,max,atStart);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->addLines(name,lines,max,atStart) || ok;
    }
    --s_changing;
    return ok;
}

// function for adding a new row to a table with the "name" id
bool Client::addTableRow(const String& name, const String& item, const NamedList* data,
	bool atStart, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

bool Client::setMultipleRows(const String& name, const NamedList& data, const String& prefix, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setMultipleRows,name,prefix,false,&data,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setMultipleRows(name,data,prefix);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->setMultipleRows(name,data,prefix) || ok;
    }
    --s_changing;
    return ok;
}

// Function to insert a new row into a table with the "name" id
bool Client::insertTableRow(const String& name, const String& item,
    const String& before, const NamedList* data, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::insertTableRow,name,before,item,data,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->insertTableRow(name,item,before,data);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->insertTableRow(name,item,before,data) || ok;
    }
    --s_changing;
    return ok;
}

// function for deleting a row from the "name" table
bool Client::delTableRow(const String& name, const String& item, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for changing the value of a row from the "name" table
bool Client::setTableRow(const String& name, const String& item, const NamedList* data, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for obtaining the information from a specific row from the "name" table
bool Client::getTableRow(const String& name, const String& item, NamedList* data, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// Set a table row or add a new one if not found
bool Client::updateTableRow(const String& name, const String& item,
    const NamedList* data, bool atStart, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::updateTableRow,name,item,
	    atStart,data,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->updateTableRow(name,item,data,atStart);
    ++s_changing;
    bool ok = false;
    for (ObjList* l = m_windows.skipNull(); l; l = l->skipNext()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->updateTableRow(name,item,data,atStart) || ok;
    }
    --s_changing;
    return ok;
}

// Add or set one or more table row(s)
bool Client::updateTableRows(const String& name, const NamedList* data, bool atStart,
	Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::updateTableRows,name,String::empty(),
	    atStart,data,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->updateTableRows(name,data,atStart);
    ++s_changing;
    bool ok = false;
    for (ObjList* l = m_windows.skipNull(); l; l = l->skipNext()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip))
	    ok = wnd->updateTableRows(name,data,atStart) || ok;
    }
    --s_changing;
    return ok;
}

// function for deleting all row from a table given by the name parameter
bool Client::clearTable(const String& name, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// function for obtaining the text from the "name" widget
bool Client::getText(const String& name, String& text, bool richText, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getText,name,&text,&richText,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->getText(name,text,richText);
    ObjList* l = &m_windows;
    for (; l; l = l->next()) {
	wnd = static_cast<Window*>(l->get());
	if (wnd && (wnd != skip) && wnd->getText(name,text,richText))
	    return true;
    }
    return false;
}

// function for obtaining the status of the checked attribute for the "name" checkable attribute
bool Client::getCheck(const String& name, bool& checked, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// get the iten currently selected from the "name" widget
bool Client::getSelect(const String& name, String& item, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
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

// Set a property
bool Client::setProperty(const String& name, const String& item, const String& value,
	Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setProperty,name,value,item,false,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setProperty(name,item,value);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->setProperty(name,item,value) || ok;
    }
    --s_changing;
    return ok;
}

// Get a property
bool Client::getProperty(const String& name, const String& item, String& value,
    Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getProperty,name,value,item,false,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->getProperty(name,item,value);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o && !ok; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->getProperty(name,item,value);
    }
    --s_changing;
    return ok;
}

// Create a window with a given name
bool Client::createWindowSafe(const String& name, const String& alias)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::createWindow,name,alias,0,0);
	return proxy.execute();
    }
    if (!createWindow(name,alias))
	return false;
    ObjList* obj = m_windows.find(alias.null() ? name : alias);
    if (!obj)
	return false;
    (static_cast<Window*>(obj->get()))->init();
    return true;
}

// Ask to an UI factory to create an object in the UI's thread
bool Client::createObject(void** dest, const String& type, const char* name,
	NamedList* params)
{
    if (!(dest && valid()))
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::createObject,dest,type,name,params);
	return proxy.execute();
    }
    *dest = UIFactory::build(type,name,params);
    return (0 != *dest);
}

// Hide/close a window with a given name
bool Client::closeWindow(const String& name, bool hide)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::closeWindow,name,hide);
	return proxy.execute();
    }
    Window* wnd = getWindow(name);
    if (!wnd)
	return false;
    if (hide)
	wnd->hide();
    else if (wnd->canClose())
	TelEngine::destruct(wnd);
    else
	return false;
    return true;
}

// Set multiple window parameters
bool Client::setParams(const NamedList* params, Window* wnd, Window* skip)
{
    if (!(params && valid()))
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setParams,String::empty(),
	    (NamedList*)params,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setParams(*params);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd && (wnd != skip))
	    ok = wnd->setParams(*params) || ok;
    }
    --s_changing;
    return ok;
}

bool Client::addToLog(const String& text)
{
    dbg_client_func(text,-1);
    return true;
}

// set the status of the client
bool Client::setStatus(const String& text, Window* wnd)
{
    Debug(ClientDriver::self(),DebugInfo,"Status '%s' in window %p",text.c_str(),wnd);
    addToLog(text);
    return setText(s_statusWidget,text,false,wnd);
}

bool Client::setStatusLocked(const String& text, Window* wnd)
{
    lockOther();
    bool ok = setStatus(text,wnd);
    unlockOther();
    return ok;
}

// Change debug output
bool Client::debugHook(bool active)
{
    if (ClientDriver::self())
	ClientDriver::self()->debugEnabled(!active);
    Debugger::setOutput(active ? dbg_client_func : 0);
    return true;
}

// Process received messages
bool Client::received(Message& msg, int id)
{
    bool processed = false;
    bool stop = false;
    for (ObjList* o = s_logics.skipNull(); !stop && o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	Debug(ClientDriver::self(),DebugAll,"Logic(%s) processing %s [%p]",
	    logic->toString().c_str(),msg.c_str(),logic);
	switch (id) {
	    case CallCdr:
		processed = logic->handleCallCdr(msg,stop) || processed;
		break;
	    case UiAction:
		processed = logic->handleUiAction(msg,stop) || processed;
		break;
	    case UserLogin:
		processed = logic->handleUserLogin(msg,stop) || processed;
		break;
	    case UserNotify:
		processed = logic->handleUserNotify(msg,stop) || processed;
		break;
	    case ResourceNotify:
		processed = logic->handleResourceNotify(msg,stop) || processed;
		break;
	    case ResourceSubscribe:
		processed = logic->handleResourceSubscribe(msg,stop) || processed;
		break;
	    case ClientChanUpdate:
		processed = logic->handleClientChanUpdate(msg,stop) || processed;
		break;
	    default:
		processed = logic->defaultMsgHandler(msg,id,stop) || processed;
	}
    }
    return processed;
}

// Postpone messages to be redispatched from UI thread
bool Client::postpone(const Message& msg, int id, bool copyUserData)
{
    if (isCurrent())
	return false;
    PostponedMessage* postponed = new PostponedMessage(msg,id,copyUserData);
    s_postponeMutex.lock();
    s_postponed.append(postponed);
    s_postponeMutex.unlock();
    return true;
}

// Handle actions from user interface
bool Client::action(Window* wnd, const String& name, NamedList* params)
{
    static String sect = "action";

    XDebug(ClientDriver::self(),DebugAll,"Action '%s' in window (%p,%s)",
	name.c_str(),wnd,wnd ? wnd->id().c_str() : "");

    String substitute = name;
    String handle;
    bool only = false, prefer = false, ignore = false, bailout = false;
    bool ok = false;
    if (hasOverride(s_actions.getSection(sect),substitute,handle,only,prefer,ignore,bailout) &&
	(only || prefer)) {
	ok = callLogicAction(findLogic(handle),wnd,substitute,params);
	bailout = only || ok;
    }
    if (bailout)
	return ok;
    for(ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (ignore && handle == logic->toString())
	    continue;
	if (callLogicAction(logic,wnd,substitute,params))
	    return true;
    }
    // Not processed: enqueue event
    Engine::enqueue(eventMessage("action",wnd,substitute,params));
    return false;
}

// Deal with toggle widget events
bool Client::toggle(Window* wnd, const String& name, bool active)
{
    static String sect = "toggle";

    XDebug(ClientDriver::self(),DebugAll,
	"Toggle name='%s' active='%s' in window (%p,%s)",
	name.c_str(),String::boolText(active),wnd,wnd ? wnd->id().c_str() : "");

    String substitute = name;
    String handle;
    bool only = false, prefer = false, ignore = false, bailout = false;
    bool ok = false;
    if (hasOverride(s_actions.getSection(sect),substitute,handle,only,prefer,ignore,bailout) &&
	(only || prefer)) {
	ok = callLogicToggle(findLogic(handle),wnd,substitute,active);
	bailout = only || ok;
    }
    if (bailout)
	return ok;
    for(ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (ignore && handle == logic->toString())
	    continue;
	if (callLogicToggle(logic,wnd,substitute,active))
	    return true;
    }
    // Not processed: enqueue event
    Message* m = eventMessage("toggle",wnd,substitute);
    m->addParam("active",String::boolText(active));
    Engine::enqueue(m);
    return false;
}

// Handle selection changes (list selection changes, focus changes ...) 
bool Client::select(Window* wnd, const String& name, const String& item, const String& text)
{
    static String sect = "select";

    XDebug(ClientDriver::self(),DebugAll,
	"Select name='%s' item='%s' in window (%p,%s)",
	name.c_str(),item.c_str(),wnd,wnd ? wnd->id().c_str() : "");

    String substitute = name;
    String handle;
    bool only = false, prefer = false, ignore = false, bailout = false;
    bool ok = false;
    if (hasOverride(s_actions.getSection(sect),substitute,handle,only,prefer,ignore,bailout) &&
	(only || prefer)) {
	ok = callLogicSelect(findLogic(handle),wnd,substitute,item,text);
	bailout = only || ok;
    }
    if (bailout)
	return ok;
    for(ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (ignore && handle == logic->toString())
	    continue;
	if (callLogicSelect(logic,wnd,substitute,item,text))
	    return true;
    }
    // Not processed: enqueue event
    Message* m = eventMessage("select",wnd,substitute);
    m->addParam("item",item);
    if (text)
	m->addParam("text",text);
    Engine::enqueue(m);
    return false;
}

// function for setting the current line
void Client::line(int newLine)
{
    Debug(ClientDriver::self(),DebugInfo,"line(%d)",newLine);
    m_line = newLine;
}

// actions taken when the client is idle, has nothing to do
void Client::idleActions()
{
    s_debugMutex.lock();
    NamedList* log = s_debugLog;
    s_debugLog = 0;
    s_debugMutex.unlock();
    // add to the debug log new information  
    if (log) {
	addLines(s_debugWidget,log,s_eventLen);
	TelEngine::destruct(log);
    }
    // Tick the logics
    if (s_idleLogicsTick) {
	s_idleLogicsTick = false;
	Time time;
	for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext())
	    (static_cast<ClientLogic*>(o->get()))->idleTimerTick(time);
    }
    // Dispatch postponed messages
    ObjList postponed;
    int postponedCount = 0;
    s_postponeMutex.lock();
    for (;;) {
	// First move some messages from the global list to the local one
	GenObject* msg = s_postponed.remove(false);
	if (!msg)
	    break;
	postponed.append(msg);
	// arbitrary limit to avoid freezing the user interface
	if (++postponedCount >= 16)
	    break;
    }
    s_postponeMutex.unlock();
    if (postponedCount) {
	Debug(ClientDriver::self(),DebugInfo,"Dispatching %d postponed messages",postponedCount);
	for (;;) {
	    PostponedMessage* msg = static_cast<PostponedMessage*>(postponed.remove(false));
	    if (!msg)
		break;
	    received(*msg,msg->id());
	    delete msg;
	}
    }
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

// Request to a logic to set a client's parameter. Save the settings file
// and/or update interface
bool Client::setClientParam(const String& param, const String& value,
	bool save, bool update)
{
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (logic->setClientParam(param,value,save,update))
	    return true;
    }
    return false;
}

// Called when the user pressed the backspace key
bool Client::backspace(const String& name, Window* wnd)
{
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (logic->backspace(name,wnd))
	    return true;
    }
    return false;
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


// Create and install a message relay owned by this client.
void Client::installRelay(const char* name, int id, int prio)
{
    if (!(name && *name))
	return;
    Debug(ClientDriver::self(),DebugAll,"installRelay(%s,%d,%d)",name,id,prio);
    MessageRelay* relay = new MessageRelay(name,this,id,prio);
    if (Engine::install(relay))
	m_relays.append(relay);
    else
	TelEngine::destruct(relay);
}

// Process an IM message
bool Client::imExecute(Message& msg)
{
    static String sect = "miscellaneous";

    XDebug(ClientDriver::self(),DebugAll,"Client::imExecute [%p]",this);
    // Check for a preferred or only logic
    String name = "imincoming";
    String handle;
    bool only = false, prefer = false, ignore = false, bailout = false;
    bool ok = false;
    if (hasOverride(s_actions.getSection(sect),name,handle,only,prefer,ignore,bailout) &&
	(only || prefer)) {
	ClientLogic* logic = findLogic(handle);
	if (logic)
	    ok = logic->imIncoming(msg);
	bailout = only || ok;
    }
    if (bailout)
	return ok;
    // Ask the logics to create a channel
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (ignore && handle == logic->toString())
	    continue;
	Debug(ClientDriver::self(),DebugAll,"Logic(%s) imIncoming [%p]",
	    logic->toString().c_str(),logic);
	if (logic->imIncoming(msg))
	    return true;
    }
    return false;
}

// Build an incoming channel
bool Client::buildIncomingChannel(Message& msg, const String& dest)
{
    Debug(ClientDriver::self(),DebugAll,"Client::buildIncomingChannel() [%p]",this);
    if (!(msg.userData() && ClientDriver::self()))
	return false;
    CallEndpoint* peer = static_cast<CallEndpoint*>(msg.userData());
    if (!peer)
	return false;
    ClientDriver::self()->lock();
    ClientChannel* chan = new ClientChannel(msg,peer->id());
    ClientDriver::self()->unlock();
    bool ok = chan->connect(peer,msg.getValue("reason"));
    // Activate or answer
    if (ok) {
	// Open an incoming URL if configured
	if (getBoolOpt(OptOpenIncomingUrl)) {
	    String* url = msg.getParam(s_incomingUrlParam);
	    if (!null(url) && Client::self() && !Client::self()->openUrlSafe(*url))
		Debug(ClientDriver::self(),DebugMild,"Failed to open incoming url=%s",url->c_str());
	}
	msg.setParam("targetid",chan->id());
	if (!getBoolOpt(OptAutoAnswer)) {
	    if (getBoolOpt(OptActivateLastInCall) && !ClientDriver::self()->activeId())
		ClientDriver::self()->setActive(chan->id());
	}
	else
	    chan->callAnswer();
    }
    TelEngine::destruct(chan);
    return ok;
}

// Build an outgoing channel
bool Client::buildOutgoingChannel(NamedList& params)
{
    Debug(ClientDriver::self(),DebugAll,"Client::buildOutgoingChannel() [%p]",this);
    // get the target of the call
    NamedString* target = params.getParam("target");
    if (TelEngine::null(target))
	return false;
    // Create the channel. Release driver's mutex as soon as possible
    if (!driverLockLoop())
	return false;
    ClientChannel* chan = new ClientChannel(*target,params);
    if (!(chan->ref() && chan->start(*target,params)))
	TelEngine::destruct(chan);
    driverUnlock();
    if (!chan)
	return false;
    params.addParam("channelid",chan->id());
    if (getBoolOpt(OptActivateLastOutCall) || !ClientDriver::self()->activeId())
	ClientDriver::self()->setActive(chan->id());
    TelEngine::destruct(chan);
    return true;
}

// Call execute handler called by the driver
bool Client::callIncoming(Message& msg, const String& dest)
{
    static String sect = "miscellaneous";

    XDebug(ClientDriver::self(),DebugAll,"Client::callIncoming [%p]",this);
    // if we are in single line mode and we have already a channel, reject the call
    if (ClientDriver::self() && ClientDriver::self()->isBusy() && !getBoolOpt(OptMultiLines)) {
	msg.setParam("error","busy");
	msg.setParam("reason",s_userBusy);
	return false;
    }
    // Check for a preferred or only logic
    String name = "callincoming";
    String handle;
    bool only = false, prefer = false, ignore = false, bailout = false;
    bool ok = false;
    if (hasOverride(s_actions.getSection(sect),name,handle,only,prefer,ignore,bailout) &&
	(only || prefer)) {
	ClientLogic* logic = findLogic(handle);
	if (logic)
	    ok = logic->callIncoming(msg,dest);
	bailout = only || ok;
    }
    if (bailout)
	return ok;
    // Ask the logics to create a channel
    for (ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (ignore && handle == logic->toString())
	    continue;
	Debug(ClientDriver::self(),DebugAll,"Logic(%s) callIncoming [%p]",
	    logic->toString().c_str(),logic);
	if (logic->callIncoming(msg,dest))
	    return true;
    }
    return false;
}

// Accept an incoming call
void Client::callAnswer(const String& id, bool setActive)
{
    Debug(ClientDriver::self(),DebugInfo,"callAccept('%s')",id.c_str());
    if (!driverLockLoop())
	return;
    ClientChannel* chan = static_cast<ClientChannel*>(ClientDriver::self()->find(id));
    if (chan)
	chan->callAnswer(setActive);
    driverUnlock();
}

// Terminate a call
void Client::callTerminate(const String& id, const char* reason, const char* error)
{
    Debug(ClientDriver::self(),DebugInfo,"callTerminate(%s)",id.c_str());
    // Check if the channel exists
    Lock lock(ClientDriver::self());
    if (!ClientDriver::self())
	return;
    Channel* chan = ClientDriver::self()->find(id);
    if (!chan)
	return;
    bool hangup = chan->isAnswered();
    bool cancel = !hangup && chan->isIncoming();
    lock.drop();
    // Drop the call
    Message* m = new Message("call.drop");
    m->addParam("id",id);
    if (hangup || cancel) {
	if (!error && cancel)
	    error = "cancelled";
	if (!reason)
	    reason = cancel ? s_cancelReason : s_hangupReason;
    }
    else {
	if (!error)
	    error = "rejected";
	if (!reason)
	    reason = s_rejectReason;
    }
    if (!null(error))
	m->addParam("error",error);
    if (!null(reason))
	m->addParam("reason",reason);
    Engine::enqueue(m);
}

// Get the active channel if any
ClientChannel* Client::getActiveChannel()
{
    return ClientDriver::self() ? ClientDriver::self()->findActiveChan() : 0;
}

// Start/stop ringer
bool Client::ringer(bool in, bool on)
{
    String* what = in ? &s_ringInName : &s_ringOutName;
    bool ok = in ? getBoolOpt(OptRingIn) : getBoolOpt(OptRingOut);
    Lock lock(ClientSound::s_soundsMutex);
    DDebug(ClientDriver::self(),DebugAll,"Ringer in=%s on=%s",
	String::boolText(in),String::boolText(on));
    if (!on)
	ClientSound::stop(*what);
    else if (*what)
	return ok && ClientSound::start(*what,false);
    else
	return false;
    return true;
}

// Send DTMFs on selected channel
bool Client::emitDigits(const char* digits, const String& id)
{
    Debug(ClientDriver::self(),DebugInfo,"emitDigit(%s,%s)",digits,id.c_str());
    if (!driverLockLoop())
	return false;
    ClientChannel* chan = static_cast<ClientChannel*>(ClientDriver::self()->find(id));
    bool ok = (0 != chan);
    if (ok) {
	Message* m = chan->message("chan.dtmf");
	m->addParam("text",digits);
	Engine::enqueue(m);
    }
    driverUnlock();
    return ok;
}

// Set a boolean option of this client
bool Client::setBoolOpt(ClientToggle toggle, bool value, bool updateUi)
{
    if (toggle >= OptCount)
	return false;

    if (m_toggles[toggle] == value && !updateUi)
	return false;

    m_toggles[toggle] = value;
    if (updateUi)
	setCheck(s_toggles[toggle],value);

    // Special options
    switch (toggle) {
	case OptRingIn:
	    if (!value)
		ringer(true,false);
	    break;
	case OptRingOut:
	    if (!value)
		ringer(false,false);
	    break;
	default: ;
    }
    return true;
}

// Engine start notification. Notify all registered logics
void Client::engineStart(Message& msg)
{
    // Wait for init
    while (!initialized())
	Thread::yield();
    for(ObjList* o = s_logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	DDebug(ClientDriver::self(),DebugAll,"Logic(%s) processing engine.start [%p]",
	    logic->toString().c_str(),logic);
	logic->engineStart(msg);
    }
}

// Add a new module for handling actions
bool Client::addLogic(ClientLogic* logic)
{
    static NamedList* s_load = 0;

    // Load logic actions file
    if (!s_actions.getSection(0)) {
	s_actions = Engine::configFile("client_actions",false);
	s_actions.load();
	s_load = s_actions.getSection("load");
    }

    if (!logic || s_logics.find(logic))
	return false;

    // Check if we should accept logic load
    // If not in config, accept only if priority is negative
    // Else: check boolean value or accept only valid positive integer values
    String* param = s_load ? s_load->getParam(logic->toString()) : 0;
    bool deny = true;
    if (param) {
	if (param->isBoolean())
	    deny = !param->toBoolean();
	else
	    deny = param->toInteger(-1) < 0;
    }
    else if (logic->priority() < 0)
	deny = false;
    if (deny) {
	Debug(DebugInfo,"Skipping client logic %p name=%s prio=%d%s%s",
	    logic,logic->toString().c_str(),logic->priority(),
	    param ? " config value: " : " not found in config",
	    param ? param->c_str() : "");
	return false;
    }

    // Add the logic
    if (logic->priority() < 0)
	logic->m_prio = -logic->priority();
    bool dup = (0 != s_logics.find(logic->toString()));
    Debug(dup ? DebugGoOn : DebugInfo,"Adding client logic%s %p name=%s prio=%d",
	dup ? " [DUPLICATE]" : "",logic,logic->toString().c_str(),logic->priority());

    for (ObjList* l = s_logics.skipNull(); l; l = l->skipNext()) {
	ClientLogic* obj = static_cast<ClientLogic*>(l->get());
	if (logic->priority() <= obj->priority()) {
	    l->insert(logic)->setDelete(false);
	    return true;
	}
    }
    s_logics.append(logic)->setDelete(false);
    return true;
}

// Remove a logic from the list
void Client::removeLogic(ClientLogic* logic)
{
    if (!(logic && s_logics.find(logic)))
	return;
    Debug(ClientDriver::self(),DebugInfo,"Removing logic %p name=%s",
	logic,logic->toString().c_str());
    s_logics.remove(logic,false);
}

// Convenience method to retrieve a logic
inline ClientLogic* Client::findLogic(const String& name)
{
    ObjList* o = s_logics.find(name);
    return o ? static_cast<ClientLogic*>(o->get()) : 0;
}

// Save a configuration file. Call openMessage() on failure
bool Client::save(Configuration& cfg, Window* parent, bool showErr)
{
    DDebug(ClientDriver::self(),DebugAll,"Saving '%s'",cfg.c_str());
    if (cfg.save())
	return true;
    String s = "Failed to save configuration file " + cfg;
    if (!(showErr && self() && self()->openMessage(s,parent)))
	Debug(ClientDriver::self(),DebugWarn,"%s",s.c_str());
    return false;
}

// Check if a string names a client's boolean option
Client::ClientToggle Client::getBoolOpt(const String& name)
{
    for (int i = 0; i < OptCount; i++)
	if (s_toggles[i] == name)
	    return (ClientToggle)i;
    return OptCount;
}

// Build an 'ui.event' message
Message* Client::eventMessage(const String& event, Window* wnd, const char* name,
	NamedList* params)
{
    Message* m = new Message("ui.event");
    if (wnd)
	m->addParam("window",wnd->id());
    m->addParam("event",event);
    if (name && *name)
	m->addParam("name",name);
    if (params) {
	unsigned int n = params->count();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* p = params->getParam(i);
	    if (p)
		m->addParam(p->name(),*p);
	}
    }
    return m;
}

// Incoming (from engine) constructor
ClientChannel::ClientChannel(const Message& msg, const String& peerid)
    : Channel(ClientDriver::self(),0,true),
    m_party(msg.getValue("caller")), m_noticed(false),
    m_line(0), m_active(false), m_silence(false), m_conference(false),
    m_muted(false), m_clientData(0), m_utility(false)
{
    Debug(this,DebugCall,"Created incoming from=%s peer=%s [%p]",
	m_party.c_str(),peerid.c_str(),this);
    m_targetid = peerid;
    m_peerId = peerid;
    Message* s = message("chan.startup");
    s->copyParams(msg,"caller,callername,called,billid,callto,username");
    String* cs = msg.getParam("chanstartup_parameters");
    if (!null(cs))
	s->copyParams(msg,*cs);
    Engine::enqueue(s);
    update(Startup,true,true,"call.ringing",false,true);
}

// Outgoing (to engine) constructor
ClientChannel::ClientChannel(const String& target, const NamedList& params)
    : Channel(ClientDriver::self(),0,false),
    m_party(target), m_noticed(true), m_line(0), m_active(false),
    m_silence(true), m_conference(false), m_muted(false), m_clientData(0),
    m_utility(false)
{
    Debug(this,DebugCall,"Created outgoing to=%s [%p]",
	m_party.c_str(),this);
}

// Constructor for utility channels used to play notifications
ClientChannel::ClientChannel(const String& soundId)
    : Channel(ClientDriver::self(),0,true),
    m_noticed(true), m_line(0), m_active(false), m_silence(true),
    m_conference(false), m_clientData(0), m_utility(true),
    m_soundId(soundId)
{
    Lock lock(ClientSound::s_soundsMutex);
    ClientSound* s = ClientSound::find(m_soundId);
    if (s) {
	s->setChannel(id(),true);
	update(Startup);
    }
    else
	m_soundId = "";
}

// Destructor
ClientChannel::~ClientChannel()
{
    XDebug(this,DebugInfo,"ClientChannel::~ClientChannel() [%p]",this);
}

// Init and start router for an outgoing (to engine), not utility, channel
bool ClientChannel::start(const String& target, const NamedList& params)
{
    // Build the call.route and chan.startup messages
    Message* m = message("call.route");
    Message* s = message("chan.startup");
    // Make sure we set the target's protocol if we have one
    Regexp r("^[a-z0-9]\\+/");
    String to = target;
    const char* param = "callto";
    if (!r.matches(target.safe())) {
	const char* proto = params.getValue("protocol");
	if (proto)
	    to = String(proto) + "/" + target;
	else
	    param = "called";
    }
    m->setParam(param,to);
    s->setParam("called",to);
    m->copyParams(params,"line,protocol,account,caller,callername,domain,cdrwrite");
    s->copyParams(params,"line,protocol,account,caller,callername,domain,cdrwrite");
    String* cs = params.getParam("chanstartup_parameters");
    if (!null(cs))
	s->copyParams(params,*cs);
    Engine::enqueue(s);
    if (startRouter(m)) {
	update(Startup);
	return true;
    }
    return false;
}

void ClientChannel::destroyed()
{
    Debug(this,DebugCall,"Destroyed [%p]",this);
    if (m_utility) {
	Lock lock(ClientSound::s_soundsMutex);
	ClientSound* s = ClientSound::find(m_soundId);
	if (s) {
	    update(Destroyed,false);
	    s->setChannel(id(),false);
	}
	m_soundId = "";
	lock.drop();
	Lock lck(m_mutex);
	setClientData();
	lck.drop();
	Channel::destroyed();
	return;
    }
    Lock lock(m_mutex);
    if (m_conference) {
	// Drop old peer if conference
	if (ClientDriver::s_dropConfPeer) {
	    Message* m = new Message("call.drop");
	    m->addParam("id",m_peerId);
	    m->addParam("reason","Conference terminated");
	    Engine::enqueue(m);
	}
    }
    else if (m_transferId)
	ClientDriver::setAudioTransfer(id());
    // Reset driver's active id
    ClientDriver* drv = static_cast<ClientDriver*>(driver());
    if (drv && id() == drv->activeId())
	drv->setActive();
    setMedia();
    update(Destroyed,false,false,"chan.hangup");
    setClientData();
    lock.drop();
    Channel::destroyed();
}

void ClientChannel::connected(const char* reason)
{
    DDebug(this,DebugCall,"Connected reason=%s [%p]",reason,this);
    Channel::connected(reason);
    if (!m_utility)
	return;

    // Utility channel: set media
    if (ClientDriver::self() && ClientDriver::self()->activeId())
	return;
    String dev = ClientDriver::device();
    if (dev.null())
	return;
    DDebug(this,DebugCall,"Utility channel opening media [%p]",this);
    Message m("chan.attach");
    complete(m,true);
    m.userData(this);
    m.clearParam("id");
    m.setParam("consumer",dev);
    ClientSound::s_soundsMutex.lock();
    ClientSound* s = ClientSound::find(m_soundId);
    if (s && s->stereo())
	m.addParam("stereo",String::boolText(true));
    ClientSound::s_soundsMutex.unlock();
    Engine::dispatch(m);
    if (!getConsumer())
        Debug(this,DebugNote,"Utility channel failed to set data consumer [%p]",this);
}

void ClientChannel::disconnected(bool final, const char* reason)
{
    Debug(this,DebugCall,"Disconnected reason=%s [%p]",reason,this);
    Channel::disconnected(final,reason);
    if (!m_reason)
	m_reason = reason;
    setActive(false);
    // Reset transfer
    if (m_transferId && !m_conference)
	ClientDriver::setAudioTransfer(id());
}

// Check if our consumer's source sent any data
// Don't set the silence flag is already reset
void ClientChannel::checkSilence()
{
    if (!m_silence)
	return;
    m_silence = !(getConsumer() && getConsumer()->getConnSource() &&
	DataNode::invalidStamp() != getConsumer()->getConnSource()->timeStamp());
#ifdef DEBUG
    if (!m_silence)
	Debug(this,DebugInfo,"Got audio data [%p]",this);
#endif
}

// Open/close media
bool ClientChannel::setMedia(bool open, bool replace)
{
    Lock lock(m_mutex);

    // Check silence (we might already have a consumer)
    checkSilence();

    // Remove source/consumer if replacing
    if (!open) {
	if (getSource() || getConsumer()) {
	    Debug(this,DebugInfo,"Closing media channels [%p]",this);
	    setSource();
	    setConsumer();
	}
	return true;
    }

    String dev = ClientDriver::device();
    if (dev.null())
	return false;
    if (!replace && getSource() && getConsumer())
	return true;
    Debug(this,DebugAll,"Opening media channels [%p]",this);
    Message m("chan.attach");
    complete(m,true);
    m.userData(this);
    m.setParam("consumer",dev);
    if (!m_muted)
	m.setParam("source",dev);
    Engine::dispatch(m);
    if (getConsumer())
	checkSilence();
    else
        Debug(this,DebugNote,"Failed to set data consumer [%p]",this);
    if (!(getSource() || m_muted))
        Debug(this,DebugNote,"Failed to set data source [%p]",this);
    bool ok = ((m_muted || getSource()) && getConsumer());
    lock.drop();
    if (!ok && Client::self()) {
	String tmp = "Failed to open media channel(s)";
	Client::self()->setStatusLocked(tmp);
    }
    return ok;
}

// Set/reset this channel's data source/consumer
bool ClientChannel::setActive(bool active, bool upd)
{
    if (m_utility)
	return false;
    Lock lock(m_mutex);
    // Don't activate it if envolved in a transfer
    noticed();
    if (active && m_transferId && !m_conference)
	return false;
    // Reset data source to make sure we remove any MOH
    if (active)
	setSource();
    if (isAnswered())
	setMedia(active);
    // Don't notify if nothing changed
    if (m_active == active)
	return true;
    Debug(this,DebugInfo,"Set active=%s [%p]",String::boolText(active),this);
    m_active = active;
    if (!upd)
	return true;
    update(active ? Active : OnHold);
    // TODO: notify the peer if answered
    return true;
}

// Set/reset this channel's muted flag. Set media if on
bool ClientChannel::setMuted(bool on, bool upd)
{
    Lock lock(m_mutex);
    if (m_muted == on)
	return true;

    Debug(this,DebugInfo,"Set muted=%s [%p]",String::boolText(on),this);
    m_muted = on;
    if (m_active) {
	if (m_muted)
	    setSource();
	else
	    setMedia(true);
    }
    if (upd)
	update(Mute);
    return true;
}

// Set/reset the transferred peer's id. Enqueue clientchan.update if changed
void ClientChannel::setTransfer(const String& target)
{
    Lock lock(m_mutex);
    if (m_conference || m_transferId == target)
	return;
    if (target)
	Debug(this,DebugCall,"Transferred to '%s' [%p]",target.c_str(),this);
    else
	Debug(this,DebugCall,"Transfer released [%p]",this);
    m_transferId = target;
    setMedia(!m_transferId && m_active && isAnswered());
    update(Transfer);
}

// Set/reset the conference data. Enqueue clientchan.update if changed.
void ClientChannel::setConference(const String& target)
{
    Lock lock(m_mutex);
    if (m_transferId == target && !m_transferId)
	return;
    Debug(this,DebugCall,"%sing conference room '%s' [%p]",
	target ? "Enter" : "Exit",target ? target.c_str() : m_transferId.c_str(),this);
    m_transferId = target;
    m_conference = (0 != m_transferId);
    setMedia(m_active && isAnswered());
    update(Conference);
}

// Notice this channel. Enqueue a clientchan.update message
void ClientChannel::noticed()
{
    Lock lock(m_mutex);
    if (m_noticed)
	return;
    m_noticed = true;
    update(Noticed);
}

// Set the channel's line (address)
void ClientChannel::line(int newLine)
{
    Lock lock(m_mutex);
    m_line = newLine;
    m_address.clear();
    if (m_line > 0) {
	m_address << "line/" << m_line;
	update(AddrChanged);
    }
}

// Outgoing call routed: enqueue update message
bool ClientChannel::callRouted(Message& msg)
{
    Lock lock(m_mutex);
    update(Routed,true,false);
    return true;
}

// Outgoing call accepted: enqueue update message
void ClientChannel::callAccept(Message& msg)
{
    Debug(this,DebugCall,"callAccept() [%p]",this);
    Channel::callAccept(msg);
    Lock lock(m_mutex);
    getPeerId(m_peerId);
    Debug(this,DebugInfo,"Peer id set to %s",m_peerId.c_str());
    update(Accepted);
}

// Outgoing call rejected: reset and and enqueue update message
void ClientChannel::callRejected(const char* error, const char* reason, const Message* msg)
{
    Debug(this,DebugCall,"callRejected('%s','%s',%p) [%p]",
	error,reason,msg,this);
    setMedia();
    if (!reason)
	reason = error;
    if (!reason)
	reason = "Unknown reason";
    Channel::callRejected(error,reason,msg);
    setActive(false);
    m_reason = reason;
    update(Rejected,true,false);
}

// Outgoing call progress
// Check for early media: start ringing tone if missing and the channel is active
// Enqueue update message
bool ClientChannel::msgProgress(Message& msg)
{
    Debug(this,DebugCall,"msgProgress() [%p]",this);
    if (active() && peerHasSource(msg))
	setMedia(true);
    bool ret = Channel::msgProgress(msg);
    update(Progressing);
    return ret;
}

// Outgoing call ringing
// Check for early media: start ringing tone if missing and the channel is active
// Enqueue update message
bool ClientChannel::msgRinging(Message& msg)
{
    Debug(this,DebugCall,"msgRinging() [%p]",this);
    if (active() && peerHasSource(msg))
	setMedia(true);
    bool ret = Channel::msgRinging(msg);
    update(Ringing);
    return ret;
}

// set status for when a call was answered, set the flags for different actions
// accordingly, and attach media channels
bool ClientChannel::msgAnswered(Message& msg)
{
    Lock lock(m_mutex);
    Debug(this,DebugCall,"msgAnswered() [%p]",this);
    m_reason.clear();
    // Active: Open media if the peer has a source
    if (active() && peerHasSource(msg))
	setMedia(true);
    m_silence = false;
    bool ret = Channel::msgAnswered(msg);
    update(Answered);
    return ret;
}

// Dropped notification
bool ClientChannel::msgDrop(Message& msg, const char* reason)
{
    Lock lock(m_mutex);
    noticed();
    Debug(this,DebugCall,"msgDrop() reason=%s [%p]",reason,this);
    if (!m_reason)
	m_reason = reason;
    // Reset transfer
    if (m_transferId && !m_conference)
	ClientDriver::setAudioTransfer(id());
    setActive(false,!Engine::exiting());
    lock.drop();
    return Channel::msgDrop(msg,reason);
}

// Answer the call if not answered
// Activate the channel
void ClientChannel::callAnswer(bool setActive)
{
    Lock lock(m_mutex);
    noticed();
    if (!isAnswered()) {
	Debug(this,DebugCall,"callAnswer() [%p]",this);
	m_reason.clear();
	status("answered");
	update(Answered,true,true,"call.answered",false,true);
    }
    // Activating channel will set the media
    if (setActive && ClientDriver::self())
	ClientDriver::self()->setActive(id());
}

// Enqueue clientchan.update message
void ClientChannel::update(int notif, bool chan, bool updatePeer,
    const char* engineMsg, bool minimal, bool data)
{
    if (m_utility) {
	if (m_soundId) {
	    const char* op = lookup(notif);
	    if (!op)
		return;
	    Message* m = new Message("clientchan.update");
	    m->addParam("notify",op);
	    m->addParam("utility",String::boolText(true));
	    m->addParam("sound",m_soundId);
	    Engine::enqueue(m);
	}
	return;
    }
    if (engineMsg)
	Engine::enqueue(message(engineMsg,minimal,data));
    if (updatePeer) {
	CallEndpoint* peer = getPeer();
	if (peer && peer->ref()) {
	    if (peer->getConsumer())
		m_peerOutFormat = peer->getConsumer()->getFormat();
	    if (peer->getSource())
		m_peerInFormat = peer->getSource()->getFormat();
	    TelEngine::destruct(peer);
	}
    }
    const char* op = lookup(notif);
    if (!op)
	return;
    Message* m = new Message("clientchan.update");
    m->addParam("notify",op);
    // Add extended params only if we don't set the channel
    if (chan)
	m->userData(this);
    else {
	m->userData(m_clientData);
	m->addParam("id",id());
	m->addParam("direction",isOutgoing() ? "incoming" : "outgoing");
	if (m_address)
	    m->addParam("address",m_address);
	if (notif != Noticed && m_noticed)
	    m->addParam("noticed",String::boolText(true));
	if (m_active)
	    m->addParam("active",String::boolText(true));
	if (m_transferId)
 	    m->addParam("transferid",m_transferId);
	if (m_conference)
 	    m->addParam("conference",String::boolText(m_conference));
    }
    if (m_silence)
	m->addParam("silence",String::boolText(true));
    Engine::enqueue(m);
}


/**
 * ClientDriver
 */
ClientDriver::ClientDriver()
    : Driver("client","misc")
{
    s_driver = this;
}

ClientDriver::~ClientDriver()
{
    s_driver = 0;
}

// install relays
void ClientDriver::setup()
{
    Driver::setup();
    Engine::install(new EngineStartHandler);
    installRelay(Halt);
    installRelay(Progress);
    installRelay(Route,200);
    installRelay(Text);
    installRelay(ImRoute);
    installRelay(ImExecute);
}

// if we receive a message for an incoming call, we pass the message on
// to the callIncoming function to handle it
bool ClientDriver::msgExecute(Message& msg, String& dest)
{
    Debug(this,DebugInfo,"msgExecute() '%s'",dest.c_str());
    return Client::self() && Client::self()->callIncoming(msg,dest);
}

// Timer notification
void ClientDriver::msgTimer(Message& msg)
{
    Driver::msgTimer(msg);
    // Tell the client to tick the logigs if busy
    if (isBusy())
    	Client::setLogicsTick();
}

// Routing handler
bool ClientDriver::msgRoute(Message& msg)
{
    // don't route here our own calls
    if (name() == msg.getValue("module"))
	return false;
    if (Client::self() && Client::self()->callRouting(msg)) {
	msg.retValue() = name() + "/*";
	return true;
    }
    return Driver::msgRoute(msg);
}

bool ClientDriver::received(Message& msg, int id)
{
    if (id == ImRoute) {
	// don't route here our own messages
	if (name() == msg.getValue("module"))
	    return false;
	if (!(Client::self() && Client::self()->imRouting(msg)))
	    return false;
	msg.retValue() = name() + "/*";
	return true;
    }
    if (id == ImExecute || id == Text) {
	if (name() == msg.getValue("module"))
	    return false;
	return Client::self() && Client::self()->imExecute(msg);
    }
    if (id == Halt) {
	dropCalls();
	if (Client::self())
	    Client::self()->quit();
    }
    return Driver::received(msg,id);
}

// Set/reset the active channel
bool ClientDriver::setActive(const String& id)
{
    Lock lock(this);
    bool ok = false;
    // Hold the old one
    if (m_activeId && m_activeId != id) {
	ClientChannel* chan = findChan(m_activeId);
	ok = chan && chan->setActive(false);
	TelEngine::destruct(chan);
    }
    m_activeId = "";
    // Select the new one
    if (!id)
	return ok;
    ClientChannel* chan = findChan(id);
    ok = chan && chan->setActive(true);
    TelEngine::destruct(chan);
    if (ok)
	m_activeId = id;
    return ok;
}

// find a channel with the specified line
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

// Drop all calls belonging to the active driver
void ClientDriver::dropCalls(const char* reason)
{
    Message m("call.drop");
    if (!reason && Engine::exiting())
	reason = "shutdown";
    if (reason)
	m.addParam("reason",reason);
    if (self())
	self()->dropAll(m);
}

// Attach/detach client channels peers' source/consumer
bool ClientDriver::setAudioTransfer(const String& id, const String& target)
{
    DDebug(s_driver,DebugInfo,"setAudioTransfer(%s,%s)",id.c_str(),target.safe());

    // Get master (id) and its peer
    ClientChannel* master = findChan(id);
    if (!master)
	return false;
    CallEndpoint* masterPeer = master->getPeer();
    if (!(masterPeer && masterPeer->ref()))
	masterPeer = 0;

    // Release conference or transfer
    String tmp = master->transferId();
    if (master->conference())
	setConference(id,false);
    else if (master->transferId())
	master->setTransfer();

    // First remove any slave's transfer
    ClientChannel* slave = findChan(tmp);
    if (slave && !slave->conference()) {
	setAudioTransfer(slave->id());
	if (masterPeer) {
	    CallEndpoint* slavePeer = slave->getPeer();
	    if (slavePeer && slavePeer->ref()) {
		DDebug(s_driver,DebugAll,"setAudioTransfer detaching peers for %s - %s",
		    master->id().c_str(),slave->id().c_str());
		DataTranslator::detachChain(masterPeer->getSource(),slavePeer->getConsumer());
		DataTranslator::detachChain(slavePeer->getSource(),masterPeer->getConsumer());
		TelEngine::destruct(slavePeer);
	    }
	}
    }
    TelEngine::destruct(slave);

    // Set new transfer: we must have a valid target
    bool ok = true;
    CallEndpoint* slavePeer = 0;
    while (target) {
	ok = false;
	if (!masterPeer)
	    break;
	slave = findChan(target);
	if (!slave)
	    break;
	if (slave->conference())
	    break;
	slavePeer = slave->getPeer();
	if (!(slavePeer && slavePeer->ref())) {
	    slavePeer = 0;
	    break;
	}
	// The new target may be involved in a transfer
	if (slave->transferId())
	    setAudioTransfer(target);
	DDebug(s_driver,DebugAll,"setAudioTransfer attaching peers for %s - %s",
	    master->id().c_str(),slave->id().c_str());
	ok = DataTranslator::attachChain(masterPeer->getSource(),slavePeer->getConsumer()) &&
	     DataTranslator::attachChain(slavePeer->getSource(),masterPeer->getConsumer());
	// Fallback on failure
	if (!ok) {
	    DataTranslator::detachChain(masterPeer->getSource(),slavePeer->getConsumer());
	    DataTranslator::detachChain(slavePeer->getSource(),masterPeer->getConsumer());
	}
	break;
    }

    // Set channels on success
    if (target) {
	if (ok) {
	    master->setTransfer(slave->id());
	    slave->setTransfer(master->id());
	}
	else
	    Debug(s_driver,DebugNote,
		"setAudioTransfer failed to attach peers for %s - %s",
		master->id().c_str(),target.c_str());
    }

    // Release references
    TelEngine::destruct(slavePeer);
    TelEngine::destruct(slave);
    TelEngine::destruct(masterPeer);
    TelEngine::destruct(master);
    return ok;
}

// Attach/detach a client channel to/from a conference room
bool ClientDriver::setConference(const String& id, bool in, const String* confName)
{
    Lock lock(s_driver);
    if (!s_driver)
	return false;

    if (!confName)
	confName = &s_confName;

    DDebug(s_driver,DebugInfo,"setConference id=%s in=%s conf=%s",
	id.c_str(),String::boolText(in),confName->c_str());
    ClientChannel* chan = findChan(id);
    if (!chan)
	return false;

    bool ok = false;
    if (in) {
	// Check if already in conference (or if the conference room is the same)
	// Remove transfer
	if (chan->conference()) {
	    if (chan->transferId() == *confName) {
		TelEngine::destruct(chan);
		return true;;
	    }
	    setConference(id,false);
	}
	else if (chan->transferId())
	    setAudioTransfer(id);
	Message m("call.conference");
	m.addParam("room",*confName);
	m.addParam("notify",*confName);
	m.userData(chan);
	ok = Engine::dispatch(m);
	if (ok)
	    chan->setConference(*confName);
	else
	    Debug(s_driver,DebugNote,"setConference failed for '%s'",id.c_str());
    }
    else {
	Message m("chan.locate");
	m.addParam("id",chan->m_peerId);
	Engine::dispatch(m);
	CallEndpoint* cp = 0;
	if (m.userData())
	    cp = static_cast<CallEndpoint*>(m.userData()->getObject("CallEndpoint"));
	const char* reason = "Unable to locate peer";
	if (cp) {
	    ok = chan->connect(cp,"Conference terminated");
	    if (ok)
		chan->setConference();
	    else
		reason = "Connect failed";
	}
	if (!ok)
	    Debug(s_driver,DebugNote,"setConference failed to re-connect '%s'. %s",
		id.c_str(),reason);
    }
    TelEngine::destruct(chan);
    return ok;
}

// Find a channel with the specified id
ClientChannel* ClientDriver::findChan(const String& id)
{
    Lock lock(s_driver);
    if (!s_driver)
	return 0;
    Channel* chan = s_driver->find(id);
    return (chan && chan->ref()) ? static_cast<ClientChannel*>(chan) : 0;
}

// Get a referenced channel whose stored peer is the given one
ClientChannel* ClientDriver::findChanByPeer(const String& peer)
{
    Lock lock(s_driver);
    if (!s_driver)
	return 0;
    for (ObjList* o = s_driver->channels().skipNull(); o; o = o->skipNext()) {
	ClientChannel* c = static_cast<ClientChannel*>(o->get());
	if (c && c->m_peerId == peer)
	    return c->ref() ? c : 0;
    }
    return 0;
}


/**
 * ClientAccount
 */
// Constructor
ClientAccount::ClientAccount(const char* proto, const char* user,
	const char* host, bool startup)
    : Mutex(true,"ClientAccount"),
    m_port(0), m_startup(startup), m_expires(-1), m_connected(false),
    m_resource(0)
{
    setIdUri(proto,user,host);
    DDebug(ClientDriver::self(),DebugAll,"Created client account=%s [%p]",
	m_uri.c_str(),this);
}

// Constructor. Build an account from a list of parameters.
ClientAccount::ClientAccount(const NamedList& params)
    : Mutex(true,"ClientAccount"),
    m_port(0), m_startup(false), m_expires(-1), m_connected(false),
    m_resource(0)
{
    setIdUri(params.getValue("protocol"),params.getValue("username"),params.getValue("domain"));
    m_startup = params.getBoolValue("enable");
    m_password = params.getValue("password");
    const char* res = params.getValue("resource");
    if (res)
	setResource(new ClientResource(res));
    m_server = params.getValue("server");
    m_options = params.getValue("options");
    m_port = params.getIntValue("port",m_port);
    m_outbound = params.getValue("outbound");
    m_expires = params.getIntValue("expires",m_expires);
    DDebug(ClientDriver::self(),DebugAll,"Created client account=%s [%p]",
	m_uri.c_str(),this);
}

// Get this account's resource
ClientResource* ClientAccount::resource(bool ref)
{
    Lock lock(this);
    if (!m_resource)
	return 0;
    return (!ref || m_resource->ref()) ? m_resource : 0; 
}

// Set/reset this account's resource
void ClientAccount::setResource(ClientResource* res)
{
    Lock lock(this);
    TelEngine::destruct(m_resource);
    m_resource = res;
}

// Find a contact by its id
ClientContact* ClientAccount::findContact(const String& id, bool ref)
{
    Lock lock(this);
    ObjList* obj = m_contacts.find(id);
    if (!obj)
	return 0;
    ClientContact* c = static_cast<ClientContact*>(obj->get());
    return (!ref || c->ref()) ? c : 0;
}

// Find a contact having a given id and resource
ClientContact* ClientAccount::findContact(const String& id, const String& resid, 
    bool ref)
{
    Lock lock(this);
    ClientContact* c = findContact(id,false);
    if (!(c && c->findResource(resid)))
	return 0;
    return (!ref || c->ref()) ? c : 0;
}

// Build a contact and append it to the list
ClientContact* ClientAccount::appendContact(const String& id, const char* name)
{
    Lock lock(this);
    if (!id || findContact(id))
	return 0;
    ClientContact* c = new ClientContact(this,id,name);
    return c;
}

// Build a contact and append it to the list
ClientContact* ClientAccount::appendContact(const NamedList& params)
{
    Lock lock(this);
    if (params.null() || findContact(params))
	return 0;
    ClientContact* c = new ClientContact(this,params);
    return c;
}

// Remove a contact from list. Reset contact's owner
ClientContact* ClientAccount::removeContact(const String& id, bool delObj)
{
    Lock lock(this);
    ClientContact* c = findContact(id);
    if (!c)
	return 0;
    m_contacts.remove(c,false);
    c->m_owner = 0;
    lock.drop();
    Debug(ClientDriver::self(),DebugAll,
	"Account(%s) removed contact '%s' delObj=%u [%p]",
	m_uri.c_str(),c->uri().c_str(),delObj,this);
    if (delObj)
	TelEngine::destruct(c);
    return c;
}

// Build a login/logout message from account's data
Message* ClientAccount::userlogin(bool login, const char* msg)
{
#define SAFE_FILL(param,value) { if(value) m->addParam(param,value); }
    Message* m = new Message(msg);
    m->addParam("account",m_id);
    m->addParam("operation",login ? "create" : "delete");
    // Fill login data    
    if (login) {
	SAFE_FILL("username",m_uri.getUser());
	SAFE_FILL("password",m_password);
	SAFE_FILL("domain",m_uri.getHost());
	lock();
	if (m_resource)
	    SAFE_FILL("resource",m_resource->toString());
	unlock();
	SAFE_FILL("server",m_server);
	SAFE_FILL("options",m_options);
	if (m_port)
	    m->addParam("port",String(m_port));
	SAFE_FILL("outbound",m_outbound);
	if (m_expires >= 0)
	    m->addParam("expires",String(m_expires));
    }
    SAFE_FILL("protocol",m_id.getProtocol());
#undef SAFE_FILL
    return m;
}

// Remove from owner. Release data
void ClientAccount::destroyed()
{
    lock();
    setResource();
    // Clear contacts. Remove their owner before
    for (ObjList* o = m_contacts.skipNull(); o; o = o->skipNext())
	(static_cast<ClientContact*>(o->get()))->m_owner = 0;
    m_contacts.clear();
    unlock();
    DDebug(ClientDriver::self(),DebugAll,"Destroyed client account=%s [%p]",
	m_uri.c_str(),this);
    RefObject::destroyed();
}

// Method used by the contact to append itself to this account's list
void ClientAccount::appendContact(ClientContact* contact)
{
    if (!contact)
	return;
    Lock lock(this);
    m_contacts.append(contact);
    contact->m_owner = this;
    Debug(ClientDriver::self(),DebugAll,
	"Account(%s) added contact '%s' [%p]",
	m_uri.c_str(),contact->uri().c_str(),this);
}


/**
 * ClientAccountList
 */
// Find an account
ClientAccount* ClientAccountList::findAccount(const String& id, bool ref)
{
    Lock lock(this);
    ObjList* obj = m_accounts.find(id);
    if (!obj)
	return 0;
    ClientAccount* a = static_cast<ClientAccount*>(obj->get());
    return (!ref || a->ref()) ? a : 0;
}

// Find an account's contact
ClientContact* ClientAccountList::findContact(const String& account, const String& id, bool ref)
{
    Lock lock(this);
    ClientAccount* acc = findAccount(account,false);
    return acc ? acc->findContact(id,ref) : 0;
}

// Find an account's contact from a built id
ClientContact* ClientAccountList::findContact(const String& builtId, bool ref)
{
    String account, contact;
    ClientContact::splitContactId(builtId,account,contact);
    return findContact(account,contact,ref);
}

// Append a new account
bool ClientAccountList::appendAccount(ClientAccount* account)
{
    if (!account || findAccount(account->toString()) || !account->ref())
	return false;
    m_accounts.append(account);
    DDebug(ClientDriver::self(),DebugAll,"List(%s) added account '%s'",
	c_str(),account->uri().c_str());
    return true;
}

// Remove an account
void ClientAccountList::removeAccount(const String& id)
{
    Lock lock(this);
    ObjList* obj = m_accounts.find(id);
    if (!obj)
	return;
    DDebug(ClientDriver::self(),DebugAll,"List(%s) removed account '%s'",
	c_str(),(static_cast<ClientAccount*>(obj->get()))->uri().c_str());
    obj->remove();
}

/**
 * ClientContact
 */
// Constructor. Append itself to the owner's list
ClientContact::ClientContact(ClientAccount* owner, const char* id, const char* name,
    bool chat)
    : m_name(name ? name : id), m_owner(owner), m_uri(id)
{
    m_id = m_uri;
    m_id.toLower();
    XDebug(ClientDriver::self(),DebugAll,"ClientContact(%p,%s) [%p]",
	owner,m_uri.c_str(),this);
    if (m_owner)
	m_owner->appendContact(this);
    if (chat)
	createChatWindow();
}

// Constructor. Build a contact from a list of parameters.
ClientContact::ClientContact(ClientAccount* owner, NamedList& params, bool chat)
    : m_name(params.getValue("name",params)), m_owner(owner), m_uri(params)
{
    m_id = m_uri;
    m_id.toLower();
    XDebug(ClientDriver::self(),DebugAll,"ClientContact(%p,%s) [%p]",
	owner,m_uri.c_str(),this);
    if (m_owner)
	m_owner->appendContact(this);
    if (chat)
	createChatWindow();
}

// Create the chat window
void ClientContact::createChatWindow(bool force, const char* name)
{
    if (force)
	destroyChatWindow();
    if (hasChat())
	return;

    // Generate chat window name and create the window
    MD5 md5(m_id);
    m_chatWndName = s_chatPrefix + md5.hexDigest();
    if (Client::self())
	Client::self()->createWindowSafe(name,m_chatWndName);
    Window* w = Client::self()->getWindow(m_chatWndName);
    if (!w)
	return;
    String id;
    buildContactId(id);
    w->context(id);
    NamedList tmp("");
    tmp.addParam("contactname",m_name);
    Client::self()->setParams(&tmp,w);
    return;
}

// Find a group this contact might belong to
String* ClientContact::findGroup(const String& group)
{
    Lock lock(m_owner);
    ObjList* obj = m_groups.find(group);
    return obj ? static_cast<String*>(obj->get()) : 0;
}

// Append a group to this contact
bool ClientContact::appendGroup(const String& group)
{
    Lock lock(m_owner);
    if (findGroup(group))
	return false;
    m_groups.append(new String(group));
    DDebug(ClientDriver::self(),DebugAll,
	"Account(%s) contact='%s' added group '%s' [%p]",
	m_owner ? m_owner->uri().c_str() : "",m_uri.c_str(),group.c_str(),this);
    return true;
}

// Remove a contact's group
bool ClientContact::removeGroup(const String& group)
{
    Lock lock(m_owner);
    ObjList* obj = m_groups.find(group);
    if (!obj)
	return false;
    obj->remove();
    DDebug(ClientDriver::self(),DebugAll,
	"Account(%s) contact='%s' removed group '%s' [%p]",
	m_owner ? m_owner->uri().c_str() : "",m_uri.c_str(),group.c_str(),this);
    return true;
}

// Find a resource having a given id
ClientResource* ClientContact::findResource(const String& id, bool ref)
{
    Lock lock(m_owner);
    ObjList* obj = m_resources.find(id);
    if (!obj)
	return 0;
    ClientResource* r = static_cast<ClientResource*>(obj->get());
    return (!ref || r->ref()) ? r : 0;
}

// Get the first resource with audio capability
ClientResource* ClientContact::findAudioResource(bool ref)
{
    Lock lock(m_owner);
    ObjList* o = m_resources.skipNull();
    for (; o; o = o->skipNext())
	if ((static_cast<ClientResource*>(o->get()))->m_audio)
	    break;
    if (!o)
	return 0;
    ClientResource* r = static_cast<ClientResource*>(o->get());
    return (!ref || r->ref()) ? r : 0;
}

// Append a resource having a given id
ClientResource* ClientContact::appendResource(const String& id)
{
    Lock lock(m_owner);
    if (findResource(id))
	return 0;
    ClientResource* r = new ClientResource(id);
    m_resources.append(r);
    DDebug(ClientDriver::self(),DebugAll,
	"Account(%s) contact='%s' added resource '%s' [%p]",
	m_owner ? m_owner->uri().c_str() : "",m_uri.c_str(),id.c_str(),this);
    return r;
}

// Remove a resource having a given id
bool ClientContact::removeResource(const String& id)
{
    Lock lock(m_owner);
    ObjList* obj = m_resources.find(id);
    if (!obj)
	return false;
    obj->remove();
    DDebug(ClientDriver::self(),DebugAll,
	"Account(%s) contact='%s' removed resource '%s' [%p]",
	m_owner ? m_owner->uri().c_str() : "",m_uri.c_str(),id.c_str(),this);
    return true;
}

// Remove from owner. Release data
void ClientContact::destroyed()
{
    destroyChatWindow();
    if (m_owner) {
	Lock lock(m_owner);
	m_owner->removeContact(m_id,false);
	m_owner = 0;
    }
    RefObject::destroyed();
}


/**
 * ClientSound
 */
// Start playing the file
bool ClientSound::start(bool force)
{
    if (m_started && !force)
	return true;
    stop();
    DDebug(ClientDriver::self(),DebugInfo,"Starting sound %s",c_str());
    m_started = doStart();
    return m_started;
}

// Stop playing the file
void ClientSound::stop()
{
    if (!m_started)
	return;
    DDebug(ClientDriver::self(),DebugInfo,"Stopping sound %s",c_str());
    doStop();
    m_started = false;
}

// Set/reset channel on sound start/stop
void ClientSound::setChannel(const String& chan, bool ok)
{
    // Reset
    if (!ok) {
	if (m_channel && m_channel == chan)
	    doStop();
	return;
    }
    // Set
    if (m_started) {
	if (m_channel == chan)
	    return;
	else
	    doStop();
    }
    m_channel = chan;
    m_started = true;
}

// Attach this sound to a channel
bool ClientSound::attachSource(ClientChannel* chan)
{
    if (!chan)
	return false;
    Message* m = new Message("chan.attach");
    m->userData(chan);
    m->addParam("source",s_calltoPrefix + file());
    m->addParam("autorepeat",String::boolText(m_repeat != 1));
    return Engine::enqueue(m);
}

// Check if a sound is started
bool ClientSound::started(const String& name)
{
    if (!name)
	return false;
    Lock lock(s_soundsMutex);
    ObjList* obj = s_sounds.find(name);
    return obj ? (static_cast<ClientSound*>(obj->get()))->started() : false;
}

// Create a sound
bool ClientSound::build(const String& id, const char* file, const char* device,
    unsigned int repeat, bool resetExisting, bool stereo)
{
    if (!id)
	return false;
    Lock lock(s_soundsMutex);
    ClientSound* s = find(id);
    if (s) {
	if (resetExisting) {
	    s->file(file,stereo);
	    s->device(device);
	    s->setRepeat(repeat);
	}
	return false;
    }
    s = new ClientSound(id,file,device);
    s->setRepeat(repeat);
    s->m_stereo = stereo;
    s_sounds.append(s);
    DDebug(ClientDriver::self(),DebugAll,"Created sound '%s' file=%s device=%s [%p]",
	id.c_str(),file,device,s);
    return true;
}

// Start playing a given sound
bool ClientSound::start(const String& name, bool force)
{
    if (!name)
	return false;
    Lock lock(s_soundsMutex);
    ObjList* obj = s_sounds.find(name);
    if (!obj)
	return false;
    return (static_cast<ClientSound*>(obj->get()))->start(force);
}

// Stop playing a given sound
void ClientSound::stop(const String& name)
{
    if (!name)
	return;
    Lock lock(s_soundsMutex);
    ObjList* obj = s_sounds.find(name);
    if (!obj)
	return;
    (static_cast<ClientSound*>(obj->get()))->stop();
}

// Find a sound object
ClientSound* ClientSound::find(const String& token, bool byName)
{
    if (!token)
	return 0;
    Lock lock(s_soundsMutex);
    if (byName) {
	ObjList* obj = s_sounds.find(token);
	return obj ? static_cast<ClientSound*>(obj->get()) : 0;
    }
    // Find by file
    for (ObjList* o = s_sounds.skipNull(); o; o = o->skipNext()) {
	ClientSound* sound = static_cast<ClientSound*>(o->get());
	if (token == sound->file())
	    return sound;
    }
    return 0;
}

bool ClientSound::doStart()
{
    if (file().null())
	return false;
    Message m("call.execute");
    m.addParam("callto",s_calltoPrefix + file());
    ClientChannel* chan = new ClientChannel(toString());
    m.userData(chan);
    m.addParam("autorepeat",String::boolText(m_repeat != 1));
    TelEngine::destruct(chan);
    return Engine::dispatch(m);
}

void ClientSound::doStop()
{
    if (m_channel) {
	ClientChannel* chan = ClientDriver::findChan(m_channel);
	if (chan)
	    chan->disconnect();
	TelEngine::destruct(chan);
    }
    m_channel = "";
    m_started = false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
