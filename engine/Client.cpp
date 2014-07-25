/**
 * Client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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


#include "yatecbase.h"
#include <stdio.h>
#include <stdarg.h>

#ifndef OUT_BUFFER_SIZE
#define OUT_BUFFER_SIZE 8192
#endif

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
	setBusy,
	getText,
	getCheck,
	getSelect,
	createWindow,
	closeWindow,
	createDialog,
	closeDialog,
	setParams,
	addLines,
	createObject,
	buildMenu,
	removeMenu,
	setImage,
	setImageFit,
	setProperty,
	getProperty,
	openUrl
    };
    ClientThreadProxy(int func, const String& name, bool show, Window* wnd = 0, Window* skip = 0);
    ClientThreadProxy(int func, const String& name, bool show, bool activate,
	Window* wnd = 0, Window* skip = 0);
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
    virtual bool received(Message& msg) {
	    Client::s_engineStarted = true;
	    if (!(Client::self() && Client::self()->postpone(msg,Client::EngineStart)))
		Debug(DebugGoOn,"Failed to postpone %s in client",msg.c_str());
	    return false;
	}
};

// System tray icon definition.
// The NamedPointer keeps the icon parameter list in its data
class TrayIconDef : public NamedPointer
{
    YNOCOPY(TrayIconDef);                // No automatic copies please
public:
    inline TrayIconDef(int prio, NamedList* params)
	: NamedPointer(params ? params->c_str() : "",params),
	m_priority(prio)
	{}
    int m_priority;
private:
    TrayIconDef() : NamedPointer("") {}  // No default constructor
};

namespace { // anonymous
/**
 * Helper class providing a thread on which the Client to run on
 * A thread for the client
 */
class ClientThread : public Thread
{
public:
    /**
     * Constructor
     * @param name Static name of the thread (for debugging purpose only)
     * @param prio Thread priority
     */
    ClientThread(Client* client, Priority prio = Normal)
    : Thread("Client",prio),
      m_client(client)
    { }

    /**
     * Destructor
     */
    inline ~ClientThread()
    {}

    /**
     * Run thread, inherited from Thread
     */
    virtual void run()
    {
	m_client->run();
    }

    /**
     * Clean up, inherited from Thread
     */
    virtual void cleanup()
    {
	m_client->cleanup();
	m_client->setThread(0);
    }

private:
    Client* m_client;
};

}; // anonymous namespace

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
static const String s_wndParamPrefix[] = {"show:","active:","focus:","check:","select:","display:",""};
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
Regexp Client::s_notSelected("^-\\(.*\\)-$");    // Holds a not selected/set value match
Regexp Client::s_guidRegexp("^\\([[:xdigit:]]\\{8\\}\\)-\\(\\([[:xdigit:]]\\{4\\}\\)-\\)\\{3\\}\\([[:xdigit:]]\\{12\\}\\)$");
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
    "display_keypad", "openincomingurl", "addaccountonstartup",
    "dockedchat", "destroychat", "notifychatstate", "showemptychat",
    "sendemptychat"
};
int Client::s_maxConfPeers = 50;
bool Client::s_engineStarted = false;            // Engine started flag
bool Client::s_idleLogicsTick = false;           // Call logics' timerTick()
bool Client::s_exiting = false;                  // Client exiting flag
ClientDriver* ClientDriver::s_driver = 0;
String ClientDriver::s_confName = "conf/client"; // The name of the client's conference room
bool ClientDriver::s_dropConfPeer = true;        // Drop a channel's old peer when terminated while in conference
String ClientDriver::s_device;                   // Currently used audio device
ObjList ClientSound::s_sounds;                   // ClientSound's list
Mutex ClientSound::s_soundsMutex(true,"ClientSound"); // ClientSound's list lock mutex
String ClientSound::s_calltoPrefix = "wave/play/"; // Client sound target prefix
static NamedList s_trayIcons("");                // Tray icon stacks. This list is managed in the client's thread
                                                 // Each item is a NamedPointer whose name is the window name
                                                 // and with ObjList data containing item defs

// Client relays
static const MsgRelay s_relays[] = {
    {"call.cdr",           Client::CallCdr,           90},
    {"ui.action",          Client::UiAction,          150},
    {"user.login",         Client::UserLogin,         50},
    {"user.notify",        Client::UserNotify,        50},
    {"resource.notify",    Client::ResourceNotify,    50},
    {"resource.subscribe", Client::ResourceSubscribe, 50},
    {"clientchan.update",  Client::ClientChanUpdate,  50},
    {"user.roster",        Client::UserRoster,        50},
    {"contact.info",       Client::ContactInfo,       50},
    {0,0,0},
};

// Channel slave type
const TokenDict ClientChannel::s_slaveTypes[] = {
    {"conference",      ClientChannel::SlaveConference},
    {"transfer",        ClientChannel::SlaveTransfer},
    {0,0}
};

// Channel notifications
const TokenDict ClientChannel::s_notification[] = {
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
    {"audioset",        ClientChannel::AudioSet},
    {0,0}
};

// Resource status names
const TokenDict ClientResource::s_statusName[] = {
    {"offline",   ClientResource::Offline},
    {"connecting",ClientResource::Connecting},
    {"online",    ClientResource::Online},
    {"busy",      ClientResource::Busy},
    {"dnd",       ClientResource::Dnd},
    {"away",      ClientResource::Away},
    {"xa",        ClientResource::Xa},
    {0,0}
};

// resource.notify capability names
const TokenDict ClientResource::s_resNotifyCaps[] = {
    {"audio", ClientResource::CapAudio},
    {"filetransfer", ClientResource::CapFileTransfer},
    {"fileinfoshare", ClientResource::CapFileInfo},
    {"resultsetmngt", ClientResource::CapRsm},
    {0,0}
};

// MucRoomMember affiliations
const TokenDict MucRoomMember::s_affName[] = {
    {"owner",   MucRoomMember::Owner},
    {"admin",   MucRoomMember::Admin},
    {"member",  MucRoomMember::Member},
    {"outcast", MucRoomMember::Outcast},
    {"none",    MucRoomMember::AffNone},
    {0,0}
};

// MucRoomMember roles
const TokenDict MucRoomMember::s_roleName[] = {
    {"moderator",   MucRoomMember::Moderator},
    {"participant", MucRoomMember::Participant},
    {"visitor",     MucRoomMember::Visitor},
    {"none",        MucRoomMember::RoleNone},
    {0,0}
};

String ClientContact::s_chatPrefix = "chat";     // Client contact chat window prefix
String ClientContact::s_dockedChatWnd= "dockedchat";            // Docked chat window name
String ClientContact::s_dockedChatWidget = "dockedchatwidget";  // Docked chat widget name
String ClientContact::s_mucsWnd = "mucs";        // MUC rooms window name
String ClientContact::s_chatInput = "message";   // Chat input widget name

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
static inline bool callLogicAction(ClientLogic* logic, Window* wnd, const String& name, NamedList* params)
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
static inline bool callLogicToggle(ClientLogic* logic, Window* wnd, const String& name, bool active)
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
static inline bool callLogicSelect(ClientLogic* logic, Window* wnd, const String& name,
    const String& item, const String& text, const NamedList* items)
{
    if (!logic)
	return false;
    DDebug(ClientDriver::self(),DebugAll,
	"Logic(%s) select='%s' item='%s' items=%p in window (%p,%s) [%p]",
	logic->toString().c_str(),name.c_str(),item.c_str(),items,
	wnd,wnd ? wnd->id().c_str() : "",logic);
    if (!items)
	return logic->select(wnd,name,item,text);
    return logic->select(wnd,name,*items);
}

// Utility function used to check for action/toggle/select preferences
// Check for a substitute
// Check if only a logic should process the action
// Check for a preffered logic to process the action
// Check if a logic should be ignored (not notified)
// Otherwise: check if the action should be ignored
static inline bool hasOverride(const NamedList* params, String& name, String& handle,
    bool& only, bool& prefer, bool& ignore, bool& bailout)
{
    static const String s_ignoreString = "ignore";

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

// Utility: request to client to flash a widget's item (page, list item ...)
static inline void flashItem(bool on, const String& name, const String& item,
    Window* w)
{
    if (!Client::self())
	return;
    Client::self()->setProperty(name,"_yate_flashitem",
	String(on) + ":" + item,w);
}


/**
 * Window
 */
// Constructor with the specified id
Window::Window(const char* id)
    : m_id(id), m_visible(false), m_active(false), m_master(false), m_popup(false),
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
    NamedIterator iter(params);
    for (const NamedString* s = 0; 0 != (s = iter.get());) {
	String n(s->name());
	if (n == YSTRING("title"))
	    title(*s);
	if (n == YSTRING("context"))
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
	else if (n.startSkip("image:",false))
	    ok = setImage(n,*s) && ok;
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

ClientThreadProxy::ClientThreadProxy(int func, const String& name, bool show,
	bool activate, Window* wnd, Window* skip)
    : m_func(func), m_rval(activate),
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
	    m_rval = Client::setVisible(m_name,m_bool,m_rval);
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
	case setBusy:
	    m_rval = client->setBusy(m_name,m_bool,m_wnd,m_skip);
	    break;
	case getText:
	    m_rval = client->getText(m_name,*m_rtext,m_rbool ? *m_rbool : false,m_wnd,m_skip);
	    break;
	case getCheck:
	    m_rval = client->getCheck(m_name,*m_rbool,m_wnd,m_skip);
	    break;
	case getSelect:
	    if (!m_params)
		m_rval = client->getSelect(m_name,*m_rtext,m_wnd,m_skip);
	    else
		m_rval = client->getSelect(m_name,*const_cast<NamedList*>(m_params),m_wnd,m_skip);
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
	case createDialog:
	    m_rval = client->createDialog(m_name,m_wnd,m_text,const_cast<NamedList*>(m_params));
	    break;
	case closeDialog:
	    m_rval = client->closeDialog(m_name,m_wnd,m_skip);
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
	case buildMenu:
	    m_rval = m_params && client->buildMenu(*m_params,m_wnd,m_skip);
	    break;
	case removeMenu:
	    m_rval = m_params && client->removeMenu(*m_params,m_wnd,m_skip);
	    break;
	case setImage:
	    m_rval = client->setImage(m_name,m_text,m_wnd,m_skip);
	    break;
	case setImageFit:
	    m_rval = client->setImageFit(m_name,m_text,m_wnd,m_skip);
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
 * Client
 */
// Constructor
Client::Client(const char *name)
    : m_initialized(false), m_line(0), m_oneThread(true),
    m_defaultLogic(0), m_clientThread(0)
{
    // Set default options
    for (unsigned int i = 0; i < OptCount; i++)
	m_toggles[i] = false;
    m_toggles[OptMultiLines] = true;
    m_toggles[OptKeypadVisible] = true;
    m_toggles[OptAddAccountOnStartup] = true;
    m_toggles[OptNotifyChatState] = true;
    m_toggles[OptDockedChat] = true;
    m_toggles[OptRingIn] = true;
    m_toggles[OptRingOut] = true;
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

bool Client::startup()
{
    DDebug(ClientDriver::self(),DebugAll,"Client::startup() [%p]",this);
    if (m_clientThread) {
	Debug(ClientDriver::self(),DebugNote,"Trying to build a client thread when you already have one '%s' [%p]",
	      m_clientThread->name(),m_clientThread);
	return true;
    }
    else {
	m_clientThread = new ClientThread(this);
	if (!m_clientThread->startup()) {
	    Debug(ClientDriver::self(),DebugWarn,"Failed to startup the client thread '%s' [%p]",m_clientThread->name(),m_clientThread);
	    delete static_cast<ClientThread*>(m_clientThread);
	    m_clientThread = 0;
	    return false;
	}
	Debug(ClientDriver::self(),DebugInfo,"Starting up client thread '%s' [%p]",m_clientThread->name(),m_clientThread);
	return true;
    }
    return false;
}

// Cleanup before halting
void Client::cleanup()
{
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
    // Update icons
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext())
	Client::updateTrayIcon(o->get()->toString());
    // Run
    main();
}

// Check if a message is sent by the client
bool Client::isClientMsg(Message& msg)
{
    String* module = msg.getParam(YSTRING("module"));
    return module && ClientDriver::self() &&
	ClientDriver::self()->name() == *module;
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
bool Client::setVisible(const String& name, bool show, bool activate)
{
    if (!valid())
	return false;
    if (s_client->needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setVisible,name,show,activate);
	return proxy.execute();
    }
    Window* w = getWindow(name);
    if (!w)
	return false;
    w->visible(show);
    if (show && activate)
	w->setActive(w->id(),true);
    return true;
}

// function for obtaining the visibility status of the "name" window
bool Client::getVisible(const String& name)
{
    Window* w = getWindow(name);
    return w && w->visible();
}

// Retrieve the active state of a window
bool Client::getActive(const String& name)
{
    Window* w = Client::self() ? Client::self()->getWindow(name) : 0;
    return w && w->active();
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

// function for inaitializing the client
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
    params.addParam("context",context,false);
    return openPopup("message",&params,parent);
}

// function for opening a confirm type pop-up window with the given text, parent, context
bool Client::openConfirm(const char* text, const Window* parent, const char* context)
{
    NamedList params("");
    params.addParam("text",text);
    params.addParam("modal",String::boolText(parent != 0));
    params.addParam("context",context,false);
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

// Show or hide control busy state
bool Client::setBusy(const String& name, bool on, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setBusy,name,on,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setBusy(name,on);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->setBusy(name,on) || ok;
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

// Retrieve an element's multiple selection
bool Client::getSelect(const String& name, NamedList& items, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::getSelect,name,&items,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->getSelect(name,items);
    for (ObjList* l = m_windows.skipNull(); l; l = l->skipNext()) {
	wnd = static_cast<Window*>(l->get());
	if ((wnd != skip) && wnd->getSelect(name,items))
	    return true;
    }
    return false;
}

// Build a menu from a list of parameters.
bool Client::buildMenu(const NamedList& params, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::buildMenu,String::empty(),&params,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->buildMenu(params);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->buildMenu(params) || ok;
    }
    --s_changing;
    return ok;
}

// Remove a menu
bool Client::removeMenu(const NamedList& params, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::removeMenu,String::empty(),&params,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->removeMenu(params);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->removeMenu(params) || ok;
    }
    --s_changing;
    return ok;
}

// Set an element's image
bool Client::setImage(const String& name, const String& image, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setImage,name,image,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setImage(name,image,false);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->setImage(name,image,false) || ok;
    }
    --s_changing;
    return ok;
}

// Set an element's image. Request to fit the image
bool Client::setImageFit(const String& name, const String& image, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::setImageFit,name,image,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->setImage(name,image,true);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->setImage(name,image,true) || ok;
    }
    --s_changing;
    return ok;
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

// Create a modal dialog owned by a given window
bool Client::createDialog(const String& name, Window* parent, const String& title,
    const String& alias, const NamedList* params)
{
    if (!(valid() && name && parent))
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::createDialog,name,title,alias,params,parent,0);
	return proxy.execute();
    }
    return parent->createDialog(name,title,alias,params);
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

// Destroy a modal dialog
bool Client::closeDialog(const String& name, Window* wnd, Window* skip)
{
    if (!valid())
	return false;
    if (needProxy()) {
	ClientThreadProxy proxy(ClientThreadProxy::closeDialog,name,(String*)0,0,wnd,skip);
	return proxy.execute();
    }
    if (wnd)
	return wnd->closeDialog(name);
    ++s_changing;
    bool ok = false;
    for (ObjList* o = m_windows.skipNull(); o; o = o->skipNext()) {
	wnd = static_cast<Window*>(o->get());
	if (wnd != skip)
	    ok = wnd->closeDialog(name) || ok;
    }
    --s_changing;
    return ok;
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
	DDebug(ClientDriver::self(),DebugAll,"Logic(%s) processing %s [%p]",
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
	    case UserRoster:
		processed = logic->handleUserRoster(msg,stop) || processed;
		break;
	    case ContactInfo:
		processed = logic->handleContactInfo(msg,stop) || processed;
		break;
	    case EngineStart:
		logic->engineStart(msg);
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
    if (isUIThread())
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
    static const String sect = "action";

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
    static const String sect = "toggle";

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

// Handle different select() methods
static bool handleSelect(ObjList& logics, Window* wnd, const String& name,
    const String& item, const String& text, const NamedList* items)
{
    static const String sect = "select";

    String substitute = name;
    String handle;
    bool only = false, prefer = false, ignore = false, bailout = false;
    bool ok = false;
    if (hasOverride(Client::s_actions.getSection(sect),substitute,handle,only,prefer,ignore,bailout) &&
	(only || prefer)) {
	ok = callLogicSelect(Client::findLogic(handle),wnd,substitute,item,text,items);
	bailout = only || ok;
    }
    if (bailout)
	return ok;
    for(ObjList* o = logics.skipNull(); o; o = o->skipNext()) {
	ClientLogic* logic = static_cast<ClientLogic*>(o->get());
	if (ignore && handle == logic->toString())
	    continue;
	if (callLogicSelect(logic,wnd,substitute,item,text,items))
	    return true;
    }
    // Not processed: enqueue event
    Message* m = Client::eventMessage("select",wnd,substitute);
    if (!items) {
	m->addParam("item",item);
	m->addParam("text",text,false);
    }
    else {
	NamedIterator iter(*items);
	String prefix = "item.";
	int n = 0;
	for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	    String pref = prefix;
	    pref << n++;
	    m->addParam(pref,ns->name());
	    if (*ns)
		m->addParam(pref + ".text",*ns);
	}
    }
    Engine::enqueue(m);
    return false;
}

// Handle selection changes (list selection changes, focus changes ...)
bool Client::select(Window* wnd, const String& name, const String& item, const String& text)
{
    XDebug(ClientDriver::self(),DebugAll,
	"Select name='%s' item='%s' in window (%p,%s)",
	name.c_str(),item.c_str(),wnd,wnd ? wnd->id().c_str() : "");
    return handleSelect(s_logics,wnd,name,item,text,0);
}

// Handle 'select' with multiple items actions from user interface
bool Client::select(Window* wnd, const String& name, const NamedList& items)
{
    XDebug(ClientDriver::self(),DebugAll,
	"Select name='%s' items=%p in window (%p,%s)",
	name.c_str(),&items,wnd,wnd ? wnd->id().c_str() : "");
    return handleSelect(s_logics,wnd,name,String::empty(),String::empty(),&items);
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
    NamedList* log = 0;
    if (s_debugLog && s_debugMutex.lock(20000)) {
	log = s_debugLog;
	s_debugLog = 0;
	s_debugMutex.unlock();
    }
    // Add to the debug log new information
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
    if (!(isUIThread() && ClientDriver::self()))
	return false;

    while (!driverLock()) {
	if (Engine::exiting() || !ClientDriver::self())
	    return false;
	idleActions();
	Thread::yield();
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
    MessageRelay* relay = new MessageRelay(name,this,id,prio,ClientDriver::self()->name(),true);
    if (Engine::install(relay))
	m_relays.append(relay);
    else
	TelEngine::destruct(relay);
}

// Process an IM message
bool Client::imExecute(Message& msg)
{
    static const String sect = "miscellaneous";

    if (Client::isClientMsg(msg))
	return false;
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
    chan->initChan();
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
    String tmp;
#ifdef DEBUG
    params.dump(tmp," ");
#endif
    Debug(ClientDriver::self(),DebugAll,"Client::buildOutgoingChannel(%s) [%p]",tmp.safe(),this);
    // get the target of the call
    NamedString* target = params.getParam(YSTRING("target"));
    if (TelEngine::null(target))
	return false;
    // Create the channel. Release driver's mutex as soon as possible
    if (!driverLockLoop())
	return false;
    int st = ClientChannel::SlaveNone;
    String masterChan;
    NamedString* slave = params.getParam(YSTRING("channel_slave_type"));
    if (slave) {
	st = ClientChannel::lookupSlaveType(*slave);
	params.clearParam(slave);
	NamedString* m = params.getParam(YSTRING("channel_master"));
	if (st && m)
	    masterChan = *m;
	params.clearParam(m);
    }
    ClientChannel* chan = new ClientChannel(*target,params,st,masterChan);
    chan->initChan();
    if (!(chan->ref() && chan->start(*target,params)))
	TelEngine::destruct(chan);
    driverUnlock();
    if (!chan)
	return false;
    params.addParam("channelid",chan->id());
    if (!st && (getBoolOpt(OptActivateLastOutCall) || !ClientDriver::self()->activeId()))
	ClientDriver::self()->setActive(chan->id());
    TelEngine::destruct(chan);
    return true;
}

// Call execute handler called by the driver
bool Client::callIncoming(Message& msg, const String& dest)
{
    static const String sect = "miscellaneous";

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
	if (!reason && cancel)
	    reason = "cancelled";
	if (!error)
	    error = cancel ? s_cancelReason : s_hangupReason;
    }
    else {
	if (!reason)
	    reason = "busy";
	if (!error)
	    error = s_rejectReason;
    }
    m->addParam("error",error,false);
    m->addParam("reason",reason,false);
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
    XDebug(ClientDriver::self(),DebugAll,"Client::emitDigits(%s,%s)",digits,id.c_str());
    if (!driverLockLoop())
	return false;
    Channel* chan = !id ? ClientDriver::self()->find(ClientDriver::self()->activeId()) :
	ClientDriver::self()->find(id);
    bool ok = (0 != chan);
    if (ok) {
	Debug(chan,DebugAll,"emitDigits(%s) [%p]",digits,chan);
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

// Build a message to be sent by the client.
Message* Client::buildMessage(const char* msg, const String& account, const char* oper)
{
    Message* m = new Message(msg);
    if (ClientDriver::self())
	m->addParam("module",ClientDriver::self()->name());
    m->addParam("operation",oper,false);
    m->addParam("account",account);
    return m;
}

// Build a resource.notify message
Message* Client::buildNotify(bool online, const String& account, const ClientResource* from)
{
    Message* m = buildMessage("resource.notify",account,online ? "online" : "offline");
    if (from) {
	m->addParam("priority",String(from->m_priority));
	m->addParam("status",from->m_text);
	if (from->m_status > ClientResource::Online)
	    m->addParam("show",lookup(from->m_status,ClientResource::s_statusName));
    }
    return m;
}

// Build a resource.subscribe or resource.notify message to request a subscription
//  or respond to a request
Message* Client::buildSubscribe(bool request, bool ok, const String& account,
    const String& contact, const char* proto)
{
    Message* m = 0;
    if (request)
	m = buildMessage("resource.subscribe",account,ok ? "subscribe" : "unsubscribe");
    else
	m = buildMessage("resource.notify",account,ok ? "subscribed" : "unsubscribed");
    m->addParam("protocol",proto,false);
    m->addParam("to",contact);
    return m;
}

// Build an user.roster message
Message* Client::buildUserRoster(bool update, const String& account,
    const String& contact, const char* proto)
{
    Message* m = buildMessage("user.roster",account,update ? "update" : "delete");
    m->addParam("protocol",proto,false);
    m->addParam("contact",contact);
    return m;
}

// Add a new module for handling actions
bool Client::addLogic(ClientLogic* logic)
{
    static const NamedList* s_load = 0;

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

// Append URI escaped String items to a String buffer
void Client::appendEscape(String& buf, ObjList& list, char sep, bool force)
{
    String tmp(sep);
    for (ObjList* o = list.skipNull(); o; o = o->skipNext())
	buf.append(o->get()->toString().uriEscape(sep),tmp,force);
}

// Splits a string at a delimiter character. URI unescape each string in result
ObjList* Client::splitUnescape(const String& buf, char sep, bool emptyOk)
{
    ObjList* list = buf.split(sep,emptyOk);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	*s = s->uriUnescape();
    }
    return list;
}

// Remove characters from a given string
void Client::removeChars(String& buf, const char* chars)
{
    if (TelEngine::null(chars))
	return;
    int pos = 0;
    while (*chars) {
	pos = buf.find(*chars,pos);
	if (pos == -1) {
	    chars++;
	    pos = 0;
	}
	else
	    buf = buf.substr(0,pos) + buf.substr(pos + 1);
    }
}

// Fix a phone number. Remove extra '+' from begining.
// Remove requested characters.
// Clear the number if a non digit char is found
void Client::fixPhoneNumber(String& number, const char* chars)
{
    if (!number)
	return;
    unsigned int n = 0;
    // Remove extra '+' from begining
    while (n < number.length() && number[n] == '+')
	n++;
    bool plus = false;
    if (n) {
	plus = true;
	number = number.substr(n);
    }
    // Remove requested chars
    removeChars(number,chars);
    // Check for valid number (digits only)
    for (n = 0; n < number.length(); n++) {
	switch (number[n]) {
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		continue;
	}
	number.clear();
	break;
    }
    if (number && plus)
	number = "+" + number;
}

// Add a tray icon to a window's stack.
bool Client::addTrayIcon(const String& wndName, int prio, NamedList* params)
{
    if (!params)
	return false;
    if (!(wndName && valid())) {
	TelEngine::destruct(params);
	return false;
    }
    NamedPointer* wnd = YOBJECT(NamedPointer,s_trayIcons.getParam(wndName));
    if (!wnd) {
	wnd = new NamedPointer(wndName);
	s_trayIcons.addParam(wnd);
    }
    ObjList* list = YOBJECT(ObjList,wnd);
    if (!list) {
	list = new ObjList;
	wnd->userData(list);
    }
    ObjList* trayIcon = list->find(*params);
    TrayIconDef* def = 0;
    if (!trayIcon) {
	ObjList* o = list->skipNull();
	for (; o; o = o->skipNext()) {
	    TrayIconDef* d = static_cast<TrayIconDef*>(o->get());
	    if (d->m_priority < prio)
		break;
	}
	def = new TrayIconDef(prio,params);
	if (o)
	    trayIcon = o->insert(def);
	else
	    trayIcon = list->append(def);
    }
    else {
	def = static_cast<TrayIconDef*>(trayIcon->get());
	def->userData(params);
    }
    // Update
    if (Client::self()->initialized() && (trayIcon == list->skipNull()))
	return updateTrayIcon(wndName);
    return true;
}

// Remove a tray icon from a window's stack.
// Show the next one if it's the first
bool Client::removeTrayIcon(const String& wndName, const String& name)
{
    if (!(wndName && name && valid()))
	return false;
    NamedPointer* wnd = YOBJECT(NamedPointer,s_trayIcons.getParam(wndName));
    if (!wnd)
	return false;
    ObjList* list = YOBJECT(ObjList,wnd);
    if (!list)
	return false;
    ObjList* trayIcon = list->find(name);
    if (!trayIcon)
	return false;
    bool upd = Client::self()->initialized() && (trayIcon == list->skipNull());
    trayIcon->remove();
    if (!upd)
	return false;
    if (list->skipNull())
	return updateTrayIcon(wndName);
    // Remove the old one and update the icon
    Window* w = Client::self()->getWindow(wndName);
    if (w) {
	NamedList p("systemtrayicon");
	p.addParam("stackedicon","");
	Client::self()->setParams(&p,w);
    }
    return true;
}

// Update the first tray icon in a window's stack.
// Remove any existing icon the the stack is empty
bool Client::updateTrayIcon(const String& wndName)
{
    if (!(wndName && valid()))
	return false;
    Window* w = Client::self()->getWindow(wndName);
    if (!w)
	return false;
    NamedPointer* wnd = YOBJECT(NamedPointer,s_trayIcons.getParam(wndName));
    if (!wnd)
	return false;
    ObjList* list = YOBJECT(ObjList,wnd);
    if (!list)
	return false;
    ObjList* o = list->skipNull();
    TrayIconDef* def = o ? static_cast<TrayIconDef*>(o->get()) : 0;
    NamedList p("systemtrayicon");
    NamedPointer* np = 0;
    if (def) {
	// Add or replace
	NamedList* nl = YOBJECT(NamedList,def);
	np = new NamedPointer("stackedicon",nl,String::boolText(true));
	p.addParam(np);
    }
    else
	// Remove the old one
	p.addParam("stackedicon","");
    bool ok = Client::self()->setParams(&p,w);
    if (np)
	np->takeData();
    return ok;
}

// Generate a GUID string in the format 8*HEX-4*HEX-4*HEX-4*HEX-12*HEX
void Client::generateGuid(String& buf, const String& extra)
{
    int8_t data[16];
    *(int32_t*)(data + 12) = (u_int32_t)Random::random();
    *(u_int64_t*)(data + 3) = Time::now();
    if (extra)
	*(u_int16_t*)(data + 11) = extra.hash();
    int32_t* d = (int32_t*)data;
    *d = (int32_t)Random::random();
    String tmp;
    tmp.hexify(data,16);
    buf.clear();
    buf << tmp.substr(0,8) << "-" << tmp.substr(8,4) << "-";
    buf << tmp.substr(12,4) << "-" << tmp.substr(16,4) << "-";
    buf << tmp.substr(20);
}

// Replace plain text chars with HTML escape or markup
void Client::plain2html(String& buf, bool spaceEol)
{
    static const String space = " ";
    static const String htmlBr = "<br>";
    static const String htmlAmp = "&amp;";
    static const String htmlLt = "&lt;";
    static const String htmlGt = "&gt;";
    static const String htmlQuot = "&quot;";

    unsigned int i = 0;
    while (i < buf.length()) {
	const String* mark = 0;
	if (buf[i] == '\r' || buf[i] == '\n')
	    mark = spaceEol ? &space : &htmlBr;
	else if (buf[i] == '&')
	    mark = &htmlAmp;
	else if (buf[i] == '<')
	    mark = &htmlLt;
	else if (buf[i] == '>')
	    mark = &htmlGt;
	else if (buf[i] == '\"')
	    mark = &htmlQuot;
	else {
	    i++;
	    continue;
	}
	// Handle "\r\n" as single <br>
	if (buf[i] == '\r' && i != buf.length() - 1 && buf[i + 1] == '\n')
	    buf = buf.substr(0,i) + *mark + buf.substr(i + 2);
	else
	    buf = buf.substr(0,i) + *mark + buf.substr(i + 1);
	i += mark->length();
    }
}

// Find a list parameter by its value
NamedString* Client::findParamByValue(NamedList& list, const String& value,
    NamedString* skip)
{
    NamedIterator iter(list);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (skip && skip == ns)
	    continue;
	if (*ns == value)
	    return (NamedString*)ns;
    }
    return 0;
}

static bool lookupFlag(const char* what, const TokenDict* dict, int& flags)
{
    bool on = true;
    if (what[0] == '!') {
	what++;
	on = false;
    }
    int n = lookup(what,dict);
    if (!n)
	return false;
    if (on)
	flags |= n;
    else
	flags &= ~n;
    return true;
}

// Decode flags from dictionary values found in a list of parameters
int Client::decodeFlags(const TokenDict* dict, const NamedList& params, const String& prefix)
{
    int flgs = 0;
    if (!dict)
	return 0;
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (!*ns)
	    continue;
	const char* what = ns->name();
	if (prefix) {
	    if (!ns->name().startsWith(prefix))
		continue;
	    what += prefix.length();
	}
	lookupFlag(what,dict,flgs);
    }
    return flgs;
}

// Decode flags from dictionary values and comma separated list
int Client::decodeFlags(const TokenDict* dict, const String& flags, int defVal)
{
    if (!(dict && flags))
	return defVal;
    int value = 0;
    bool found = false;
    ObjList* list = flags.split(',',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	const String& s = o->get()->toString();
	found = lookupFlag(s,dict,value) || found;
    }
    TelEngine::destruct(list);
    return found ? value : defVal;
}

// Add path separator at string end. Set destination string.
void Client::addPathSep(String& dest, const String& path, char sep)
{
    if (!path)
	return;
    if (!sep)
	sep = *Engine::pathSeparator();
    dest = path;
    unsigned int len = path.length();
    if (path[len - 1] != sep)
	dest += sep;
}

// Fix path separator. Set it to platform default
void Client::fixPathSep(String& path)
{
    char repl = (*Engine::pathSeparator() == '/') ? '\\' : '/';
    char* s = (char*)path.c_str();
    for (unsigned int i = 0; i < path.length(); i++, s++)
	if (*s == repl)
	    *s = *Engine::pathSeparator();
}

// Remove chars from string end. Set destination string
bool Client::removeEndsWithPathSep(String& dest, const String& src, char sep)
{
    if (!sep)
	sep = *Engine::pathSeparator();
    int pos = (int)(src.length()) - 1;
    if (pos >= 0 && src[pos] == sep)
	dest = src.substr(0,pos);
    else
	dest = src;
    return !dest.null();
}

// Set destination from last item in path
bool Client::getLastNameInPath(String& dest, const String& path, char sep)
{
    int pos = path.rfind(sep ? sep : *Engine::pathSeparator());
    if (pos >= 0)
	dest = path.substr(pos + 1);
    if (!dest)
	dest = path;
    return !dest.null();
}

// Remove last name in path, set destination from remaining
bool Client::removeLastNameInPath(String& dest, const String& path, char sep,
    const String& equalOnly)
{
    int pos = path.rfind(sep ? sep : *Engine::pathSeparator());
    bool ok = pos >= 0 && (!equalOnly || equalOnly == path.substr(pos + 1));
    if (ok)
	dest = path.substr(0,pos);
    return ok;
}

// Add a formatted log line
bool Client::addToLogFormatted(const char* format, ...)
{
    char buf[OUT_BUFFER_SIZE];
    va_list va;
    va_start(va,format);
    ::vsnprintf(buf,sizeof(buf) - 1,format,va);
    va_end(va);
    dbg_client_func(buf,-1);
    return true;
}

// Build an 'ui.event' message
Message* Client::eventMessage(const String& event, Window* wnd, const char* name,
	NamedList* params)
{
    Message* m = new Message("ui.event");
    if (wnd)
	m->addParam("window",wnd->id());
    m->addParam("event",event);
    m->addParam("name",name,false);
    if (params)
	m->copyParams(*params);
    return m;
}

// Incoming (from engine) constructor
ClientChannel::ClientChannel(const Message& msg, const String& peerid)
    : Channel(ClientDriver::self(),0,true),
    m_slave(SlaveNone),
    m_party(msg.getValue(YSTRING("caller"))), m_noticed(false),
    m_line(0), m_active(false), m_silence(false), m_conference(false),
    m_muted(false), m_clientData(0), m_utility(false), m_clientParams("")
{
    Debug(this,DebugCall,"Created incoming from=%s peer=%s [%p]",
	m_party.c_str(),peerid.c_str(),this);
    const char* acc = msg.getValue(YSTRING("in_line"));
    if (TelEngine::null(acc))
	acc = msg.getValue(YSTRING("account"),msg.getValue(YSTRING("line")));
    if (!TelEngine::null(acc)) {
	m_clientParams.addParam("account",acc);
	m_clientParams.addParam("line",acc);
    }
    const char* proto = msg.getValue(YSTRING("protocol"));
    if (TelEngine::null(proto)) {
	const String& module = msg[YSTRING("module")];
	if (module == YSTRING("sip") || module == YSTRING("jingle") ||
	    module == YSTRING("iax") || module == YSTRING("h323"))
	    proto = module;
    }
    m_clientParams.addParam("protocol",proto,false);
    m_partyName = msg.getValue(YSTRING("callername"));
    m_targetid = peerid;
    m_peerId = peerid;
    Message* s = message("chan.startup");
    s->copyParams(msg,YSTRING("caller,callername,called,billid,callto,username"));
    String* cs = msg.getParam(YSTRING("chanstartup_parameters"));
    if (!null(cs))
	s->copyParams(msg,*cs);
    Engine::enqueue(s);
    update(Startup,true,true,"call.ringing",false,true);
}

// Outgoing (to engine) constructor
ClientChannel::ClientChannel(const String& target, const NamedList& params,
    int st, const String& masterChan)
    : Channel(ClientDriver::self(),0,false),
    m_slave(st),
    m_party(target), m_noticed(true), m_line(0), m_active(false),
    m_silence(true), m_conference(false), m_muted(false), m_clientData(0),
    m_utility(false), m_clientParams("")
{
    Debug(this,DebugCall,"Created outgoing to=%s [%p]",
	m_party.c_str(),this);
    m_partyName = params.getValue(YSTRING("calledname"));
    if (m_slave)
	m_master = masterChan;
}

// Constructor for utility channels used to play notifications
ClientChannel::ClientChannel(const String& soundId)
    : Channel(ClientDriver::self(),0,true),
    m_slave(SlaveNone),
    m_noticed(true), m_line(0), m_active(false), m_silence(true),
    m_conference(false), m_clientData(0), m_utility(true),
    m_soundId(soundId), m_clientParams("")
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
    static const String s_cpParams("line,protocol,account,caller,callername,domain,cdrwrite");
    // Build the call.route and chan.startup messages
    Message* m = message("call.route");
    Message* s = message("chan.startup");
    // Make sure we set the target's protocol if we have one
    static const Regexp r("^[a-z0-9]\\+/");
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
    m->copyParams(params,s_cpParams);
    s->copyParams(params,s_cpParams);
    String* cs = params.getParam(YSTRING("chanstartup_parameters"));
    if (!null(cs))
	s->copyParams(params,*cs);
    String cParams = params.getParam(YSTRING("call_parameters"));
    if (cParams)
	m->copyParams(params,cParams);
    cParams.append("call_parameters,line,protocol,account",",");
    cParams.append(params.getValue(YSTRING("client_parameters")),",");
    m_clientParams.copyParams(params,cParams);
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
    // Drop all slaves
    for (ObjList* o = m_slaves.skipNull(); o; o = o->skipNext())
	ClientDriver::dropChan(o->get()->toString());
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
	if (ClientDriver::s_dropConfPeer)
	    ClientDriver::dropChan(m_peerId,"Conference terminated");
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
    m.clearParam(YSTRING("id"));
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
    m.setParam("force",String::boolText(true));
    Engine::dispatch(m);
    if (getConsumer())
	checkSilence();
    else
        Debug(this,DebugNote,"Failed to set data consumer [%p]",this);
    if (!(getSource() || m_muted))
        Debug(this,DebugNote,"Failed to set data source [%p]",this);
    bool ok = ((m_muted || getSource()) && getConsumer());
    update(AudioSet);
    lock.drop();
    if (!ok && Client::self())
	Client::self()->addToLog("Failed to open media channel(s): " + id());
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
    // Join to master conference
    if (m_slave == SlaveConference && m_master) {
	String confName("conf/" + m_master);
	Message m("call.conference");
	m.addParam("room",confName);
	m.addParam("notify",confName);
	m.addParam("maxusers",String(Client::s_maxConfPeers * 2));
	m.userData(this);
	if (Engine::dispatch(m))
	    setConference(confName);
    }
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
    if (m_slave == SlaveTransfer && m_master && !m_transferId)
	ClientDriver::setAudioTransfer(m_master,id());
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
    if (m_slave == SlaveTransfer && m_master && !m_transferId)
	ClientDriver::setAudioTransfer(m_master,id());
    return ret;
}

// set status for when a call was answered, set the flags for different actions
// accordingly, and attach media channels
bool ClientChannel::msgAnswered(Message& msg)
{
    Lock lock(m_mutex);
    Debug(this,DebugCall,"msgAnswered() [%p]",this);
    m_reason.clear();
    if (m_slave == SlaveTransfer && m_master && !m_transferId)
	ClientDriver::setAudioTransfer(m_master,id());
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
	m->addParam("address",m_address,false);
	if (notif != Noticed && m_noticed)
	    m->addParam("noticed",String::boolText(true));
	if (m_active)
	    m->addParam("active",String::boolText(true));
	m->addParam("transferid",m_transferId,false);
	if (m_conference)
 	    m->addParam("conference",String::boolText(m_conference));
	if (m_slave) {
	    m->addParam("channel_slave_type",::lookup(m_slave,s_slaveTypes),false);
	    m->addParam("channel_master",m_master);
	}
    }
    if (m_silence)
	m->addParam("silence",String::boolText(true));
    Engine::enqueue(m);
}

// Get channel peer used to reconnect
CallEndpoint* ClientChannel::getReconnPeer(bool ref)
{
    String tmp;
    getReconnPeer(tmp);
    if (!tmp)
	return 0;
    Message m("chan.locate");
    m.addParam("id",tmp);
    Engine::dispatch(m);
    CallEndpoint* c = static_cast<Channel*>(YOBJECT(CallEndpoint,m.userData()));
    return (c && (!ref || c->ref())) ? c : 0;
}

// Drop peer used to reconnect
void ClientChannel::dropReconnPeer(const char* reason)
{
    String tmp;
    getReconnPeer(tmp);
    if (tmp)
	ClientDriver::dropChan(tmp,reason);
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
    installRelay(MsgExecute);
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
    // don't route here our own messages
    if (name() == msg[YSTRING("module")])
	return false;
    String* routeType = msg.getParam(YSTRING("route_type"));
    if (routeType) {
	if (*routeType == YSTRING("msg")) {
	    if (!(Client::self() && Client::self()->imRouting(msg)))
		return false;
	    msg.retValue() = name() + "/*";
	    return true;
	}
	if (*routeType != YSTRING("call"))
	    return Driver::msgRoute(msg);
    }
    if (Client::self() && Client::self()->callRouting(msg)) {
	msg.retValue() = name() + "/*";
	return true;
    }
    return Driver::msgRoute(msg);
}

bool ClientDriver::received(Message& msg, int id)
{
    if (id == MsgExecute || id == Text) {
	if (Client::isClientMsg(msg))
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
    m.addParam("reason",reason,false);
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
bool ClientDriver::setConference(const String& id, bool in, const String* confName,
    bool buildFromChan)
{
    Lock lock(s_driver);
    if (!s_driver)
	return false;

    String dummy;
    if (!confName) {
	if (buildFromChan) {
	    dummy << "conf/" << id;
	    confName = &dummy;
	}
	else
	    confName = &s_confName;
    }

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
	m.addParam("maxusers",String(Client::s_maxConfPeers * 2));
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
	    cp = static_cast<CallEndpoint*>(m.userData()->getObject(YATOM("CallEndpoint")));
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

// Drop a channel
void ClientDriver::dropChan(const String& chan, const char* reason, bool peer)
{
    if (!peer) {
	Message* m = Client::buildMessage("call.drop",String::empty());
	m->addParam("id",chan);
	m->addParam("reason",reason,false);
	Engine::enqueue(m);
	return;
    }
    ClientChannel* cc = ClientDriver::findChan(chan);
    if (cc)
	cc->dropReconnPeer(reason);
    TelEngine::destruct(cc);
}


/**
 * ClientAccount
 */
// Constructor
ClientAccount::ClientAccount(const char* proto, const char* user, const char* host,
    bool startup, ClientContact* contact)
    : Mutex(true,"ClientAccount"),
    m_params(""), m_resource(0), m_contact(0)
{
    m_params.addParam("enabled",String::boolText(startup));
    m_params.addParam("protocol",proto,false);
    m_params.addParam("username",user,false);
    m_params.addParam("domain",host,false);
    setResource(new ClientResource(m_params.getValue(YSTRING("resource"))));
    setContact(contact);
    Debug(ClientDriver::self(),DebugAll,"Created client account='%s' [%p]",
	toString().c_str(),this);
}

// Constructor. Build an account from a list of parameters.
ClientAccount::ClientAccount(const NamedList& params, ClientContact* contact)
    : Mutex(true,"ClientAccount"),
    m_params(params), m_resource(0), m_contact(0)
{
    setResource(new ClientResource(m_params.getValue(YSTRING("resource"))));
    setContact(contact);
    Debug(ClientDriver::self(),DebugAll,"Created client account='%s' [%p]",
	toString().c_str(),this);
}

// Set account own contact
void ClientAccount::setContact(ClientContact* contact)
{
    Lock lock(this);
    if (m_contact == contact)
	return;
    if (m_contact)
	m_contact->m_owner = 0;
    TelEngine::destruct(m_contact);
    m_contact = contact;
    if (m_contact) {
	m_contact->m_owner = this;
	m_contact->setSubscription("both");
    }
}

// Get this account's resource
ClientResource* ClientAccount::resource(bool ref)
{
    Lock lock(this);
    if (!m_resource)
	return 0;
    return (!ref || m_resource->ref()) ? m_resource : 0;
}

// Set this account's resource
void ClientAccount::setResource(ClientResource* res)
{
    if (!res)
	return;
    Lock lock(this);
    if (res == m_resource)
	return;
    TelEngine::destruct(m_resource);
    m_resource = res;
}

// Save this account to client accounts file or remove it
bool ClientAccount::save(bool ok, bool savePwd)
{
    static const String s_oldId("old_id");
    bool changed = false;
    // Handle id changes (new version generate an internal id)
    String old = m_params[s_oldId];
    NamedList* oldSect = old ? Client::s_accounts.getSection(old) : 0;
    if (oldSect) {
	changed = true;
	Client::s_accounts.clearSection(old);
    }
    m_params.clearParam(s_oldId);
    NamedList* sect = Client::s_accounts.getSection(toString());
    if (ok) {
	if (!sect)
	    sect = Client::s_accounts.createSection(toString());
	if (sect) {
	    changed = true;
	    *sect = m_params;
	    if (!savePwd)
		sect->clearParam(YSTRING("password"));
	    // Don't save internal (temporary parameters)
	    sect->clearParam(YSTRING("internal"),'.');
	    sect->assign(toString());
	}
    }
    else if (sect) {
	changed = true;
	Client::s_accounts.clearSection(toString());
    }
    if (!changed)
	return true;
    bool saved = Client::save(Client::s_accounts);
    if (ok && !saved)
	m_params.addParam("old_id",old,false);
    return saved;
}

// Find a contact by its id
ClientContact* ClientAccount::findContact(const String& id, bool ref)
{
    if (!id)
	return 0;
    Lock lock(this);
    ClientContact* c = 0;
    if (!m_contact || id != m_contact->toString()) {
	ObjList* o = m_contacts.find(id);
	c = o ? static_cast<ClientContact*>(o->get()) : 0;
    }
    else
	c = m_contact;
    return c && (!ref || c->ref()) ? c : 0;
}

// Find a contact by name and/or uri
ClientContact* ClientAccount::findContact(const String* name, const String* uri,
    const String* skipId, bool ref)
{
    if (!(name || uri))
	return 0;
    Lock lock(this);
    for (ObjList* o = m_contacts.skipNull(); o; o = o->skipNext()) {
	ClientContact* c = static_cast<ClientContact*>(o->get());
	if ((skipId && *skipId == c->toString()) ||
	    (name && *name != c->m_name) || (uri && *uri != c->uri()))
	    continue;
	return (!ref || c->ref()) ? c : 0;
    }
    return 0;
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

// Find a contact by its URI (build an id from account and uri)
ClientContact* ClientAccount::findContactByUri(const String& uri, bool ref)
{
    if (!uri)
	return 0;
    Lock lock(this);
    String id;
    ClientContact::buildContactId(id,toString(),uri);
    return findContact(id,ref);
}

// Find a MUC room by its id
MucRoom* ClientAccount::findRoom(const String& id, bool ref)
{
    if (!id)
	return 0;
    Lock lock(this);
    ObjList* o = m_mucs.find(id);
    if (!o)
	return 0;
    MucRoom* r = static_cast<MucRoom*>(o->get());
    return (!ref || r->ref()) ? r : 0;
}

// Find a MUC room by its uri
MucRoom* ClientAccount::findRoomByUri(const String& uri, bool ref)
{
    Lock lock(this);
    String id;
    ClientContact::buildContactId(id,toString(),uri);
    return findRoom(id,ref);
}

// Find any contact (regular or MUC room) by its id
ClientContact* ClientAccount::findAnyContact(const String& id, bool ref)
{
    ClientContact* c = findContact(id,ref);
    return c ? c : findRoom(id,ref);
}

// Build a contact and append it to the list
ClientContact* ClientAccount::appendContact(const String& id, const char* name,
    const char* uri)
{
    Lock lock(this);
    if (!id || findContact(id))
	return 0;
    ClientContact* c = new ClientContact(this,id,name,uri);
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
	c = findRoom(id);
    if (!c || c == m_contact)
	return 0;
    c->m_owner = 0;
    bool regular = !c->mucRoom();
    if (regular)
	m_contacts.remove(c,false);
    else
	m_mucs.remove(c,false);
    lock.drop();
    Debug(ClientDriver::self(),DebugAll,
	"Account(%s) removed %s '%s' uri='%s' delObj=%u [%p]",
	toString().c_str(),regular ? "contact" : "MUC room",
	c->toString().c_str(),c->uri().c_str(),delObj,this);
    if (delObj)
	TelEngine::destruct(c);
    return c;
}

// Clear MUC rooms
void ClientAccount::clearRooms(bool saved, bool temp)
{
    if (!(saved || temp))
	return;
    Lock lock(this);
    ListIterator iter(m_mucs);
    for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	MucRoom* r = static_cast<MucRoom*>(gen);
	if (r->local() || r->remote()) {
	    if (saved)
		m_mucs.remove(r);
	}
	else if (temp)
	    m_mucs.remove(r);
    }
}

// Build a login/logout message from account's data
Message* ClientAccount::userlogin(bool login, const char* msg)
{
    Message* m = Client::buildMessage(msg,toString(),login ? "login" : "logout");
    if (login) {
	m->copyParams(m_params);
	m->clearParam(YSTRING("internal"),'.');
    }
    else
	m->addParam("protocol",protocol(),false);
    return m;
}

// Build a message used to update or query account userdata
Message* ClientAccount::userData(bool update, const String& data, const char* msg)
{
    Message* m = Client::buildMessage(msg,toString(),update ? "update" : "query");
    m->addParam("data",data,false);
    if (!update || data != YSTRING("chatrooms"))
	return m;
    m->setParam("data.count","0");
    unsigned int n = 0;
    Lock lock(this);
    for (ObjList* o = m_mucs.skipNull(); o; o = o->skipNext()) {
	MucRoom* r = static_cast<MucRoom*>(o->get());
	if (!r->remote())
	    continue;
	String prefix;
	prefix << "data." << ++n;
	m->addParam(prefix,r->uri());
	prefix << ".";
	m->addParam(prefix + "name",r->m_name,false);
	if (r->m_password) {
	    Base64 b((void*)r->m_password.c_str(),r->m_password.length());
	    String tmp;
	    b.encode(tmp);
	    m->addParam(prefix + "password",tmp);
	}
	for (ObjList* o = r->groups().skipNull(); o; o = o->skipNext())
	    m->addParam(prefix + "group",o->get()->toString(),false);
	NamedIterator iter(r->m_params);
	for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	    // Skip local/remote params
	    if (ns->name() != YSTRING("local") && ns->name() != YSTRING("remote") &&
		!ns->name().startsWith("internal."))
		m->addParam(prefix + ns->name(),*ns);
	}
    }
    m->setParam("data.count",String(n));
    return m;
}

// Fill a list used to update an account list item
void ClientAccount::fillItemParams(NamedList& list)
{
    list.addParam("account",toString());
    list.addParam("protocol",m_params.getValue(YSTRING("protocol")));
    const char* sName = resource().statusName();
    NamedString* status = new NamedString("status",sName);
    status->append(resource().m_text,": ");
    list.addParam(status);
}

// Utility used in ClientAccount::setupDataDir
static bool showAccError(ClientAccount* a, String* errStr, const String& fail,
    const char* what, int error = 0, const char* errorStr = 0)
{
    String tmp;
    if (!errStr)
	errStr = &tmp;
    if (error) {
	Thread::errorString(*errStr,error);
	*errStr = String(error) + " " + *errStr;
    }
    else
    	*errStr = errorStr;
    *errStr = fail + " '" + what + "': " + *errStr;
    Debug(ClientDriver::self(),DebugWarn,"Account(%s) %s [%p]",
	a->toString().c_str(),errStr->c_str(),a);
    return false;
}

// Set account data directory. Make sure it exists.
// Move all files from the old one if changed
bool ClientAccount::setupDataDir(String* errStr, bool saveAcc)
{
    String dir;
    String user = m_params[YSTRING("username")];
    user.toLower();
    String domain = m_params.getValue(YSTRING("domain"),m_params.getValue(YSTRING("server")));
    domain.toLower();
    dir << protocol().hash() << "_" << user.hash() << "_" << domain.hash();
    if (dataDir() == dir) {
	String s;
	s << Engine::configPath(true) << Engine::pathSeparator() << dataDir();
	ObjList d;
	ObjList f;
	File::listDirectory(s,&d,&f);
	if (d.find(dataDir()))
	    return true;
	if (f.find(dataDir()))
	    return showAccError(this,errStr,"Failed to create directory",s,0,
		"A file with the same name already exists");
	// Not found: clear old directory
	m_params.clearParam(YSTRING("datadirectory"));
    }
    String path = Engine::configPath(true);
    // Check if already there
    int error = 0;
    ObjList dirs;
    ObjList files;
    File::listDirectory(path,&dirs,&files,&error);
    if (error)
	return showAccError(this,errStr,"Failed to list directory",path,error);
    String fullPath = path + Engine::pathSeparator() + dir;
    if (files.find(dir))
	return showAccError(this,errStr,"Failed to create directory",fullPath,0,
	    "A file with the same name already exists");
    const String& existing = dataDir();
    ObjList* oldDir = existing ? dirs.find(existing) : 0;
    if (dirs.find(dir)) {
	if (oldDir) {
	    // Move dirs and files from old directory
	    String old = path + Engine::pathSeparator() + existing;
	    ObjList all;
	    File::listDirectory(old,&all,&all,&error);
	    if (!error) {
		bool ok = true;
		for (ObjList* o = all.skipNull(); o; o = o->skipNext()) {
		    String* item = static_cast<String*>(o->get());
		    String oldItem = old + Engine::pathSeparator() + *item;
		    String newItem = fullPath + Engine::pathSeparator() + *item;
		    File::rename(oldItem,newItem,&error);
		    if (!error)
			continue;
		    ok = false;
		    String tmp;
		    Thread::errorString(tmp,error);
		    Debug(ClientDriver::self(),DebugWarn,
			"Account(%s) failed to move '%s' to '%s': %d %s [%p]",
			toString().c_str(),oldItem.c_str(),newItem.c_str(),
			error,tmp.c_str(),this);
		    error = 0;
		}
		// Delete it if all moved
		if (ok) {
		    File::rmDir(old,&error);
		    if (error)
			showAccError(this,errStr,"Failed to delete directory",old,error);
		}
	    }
	    else
		showAccError(this,errStr,"Failed to list directory",old,error);
	}
    }
    else {
	if (oldDir) {
	    // Rename it
	    String old = path + Engine::pathSeparator() + existing;
	    File::rename(old,fullPath,&error);
	    if (error)
		return showAccError(this,errStr,"Failed to rename existing directory",
		    old,error);
	}
	else {
	    // Create a new one
	    File::mkDir(fullPath,&error);
	    if (error)
		return showAccError(this,errStr,"Failed to create directory",
		    fullPath,error);
	}
    }
    m_params.setParam("datadirectory",dir);
    if (saveAcc) {
	NamedList* sect = Client::s_accounts.getSection(toString());
	if (sect) {
	    sect->setParam("datadirectory",dir);
	    Client::s_accounts.save();
	}
    }
    // Set account meta data
    loadDataDirCfg();
    NamedList* sect = m_cfg.createSection("general");
    sect->setParam("account",toString());
    sect->copyParams(m_params,"protocol,username,domain,server");
    m_cfg.save();
    return true;
}

// Load configuration file from data directory
bool ClientAccount::loadDataDirCfg(Configuration* cfg, const char* file)
{
    if (TelEngine::null(file))
	return false;
    if (!cfg)
	cfg = &m_cfg;
    if (!dataDir())
	setupDataDir(0,false);
    const String& dir = dataDir();
    if (!dir)
	return false;
    *cfg = "";
    *cfg << Engine::configPath(true) + Engine::pathSeparator() + dir;
    *cfg << Engine::pathSeparator() << file;
    return cfg->load();
}

// Clear account data directory
bool ClientAccount::clearDataDir(String* errStr)
{
    if (!dataDir())
	setupDataDir(0,false);
    const String& dir = dataDir();
    if (!dir)
	return false;
    // Check base path
    String tmp(Engine::configPath(true));
    ObjList dirs;
    File::listDirectory(tmp,&dirs,0);
    if (!dirs.find(dir))
	return true;
    // Delete files
    tmp << Engine::pathSeparator() << dir;
    int error = 0;
    bool ok = false;
    ObjList files;
    if (File::listDirectory(tmp,0,&files,&error)) {
	for (ObjList* o = files.skipNull(); o; o = o->skipNext()) {
	    String file(tmp + Engine::pathSeparator() + o->get()->toString());
	    int err = 0;
	    if (!File::remove(file,&err)) {
		if (!error)
		    error = err;
	    }
	}
	if (!error)
	    ok = File::rmDir(tmp,&error);
    }
    return ok ? ok : showAccError(this,errStr,"Failed to clear data directory",tmp,error);
}

// Load contacts from configuration file
void ClientAccount::loadContacts(Configuration* cfg)
{
    if (!cfg)
	cfg = &m_cfg;
    unsigned int n = cfg->sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = cfg->getSection(i);
	if (!(sect && sect->c_str()))
	    continue;
	const String& type = (*sect)[YSTRING("type")];
	if (type == YSTRING("groupchat")) {
	    String id;
	    ClientContact::buildContactId(id,toString(),*sect);
	    MucRoom* room = findRoom(id);
	    if (!room)
		room = new MucRoom(this,id,0,*sect);
	    room->groups().clear();
	    NamedIterator iter(*sect);
	    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
		if (ns->name() == YSTRING("type"))
		    continue;
		if (ns->name() == YSTRING("name"))
		    room->m_name = *ns;
		else if (ns->name() == YSTRING("password"))
		    room->m_password = *ns;
		else if (ns->name() == YSTRING("group")) {
		    if (*ns)
			room->appendGroup(*ns);
		}
		else
		    room->m_params.setParam(ns->name(),*ns);
	    }
	    room->setLocal(true);
	    Debug(ClientDriver::self(),DebugAll,
		"Account(%s) loaded MUC room '%s' [%p]",
		toString().c_str(),room->uri().c_str(),this);
	}
    }
}

// Remove from owner. Release data
void ClientAccount::destroyed()
{
    lock();
    TelEngine::destruct(m_resource);
    TelEngine::destruct(m_contact);
    // Clear contacts. Remove their owner before
    for (ObjList* o = m_contacts.skipNull(); o; o = o->skipNext())
	(static_cast<ClientContact*>(o->get()))->m_owner = 0;
    m_contacts.clear();
    for (ObjList* o = m_mucs.skipNull(); o; o = o->skipNext())
	(static_cast<ClientContact*>(o->get()))->m_owner = 0;
    m_mucs.clear();
    unlock();
    Debug(ClientDriver::self(),DebugAll,"Destroyed client account=%s [%p]",
	toString().c_str(),this);
    RefObject::destroyed();
}

// Method used by the contact to append itself to this account's list
void ClientAccount::appendContact(ClientContact* contact, bool muc)
{
    if (!contact)
	return;
    Lock lock(this);
    if (!muc)
	m_contacts.append(contact);
    else
	m_mucs.append(contact);
    contact->m_owner = this;
    Debug(ClientDriver::self(),DebugAll,
	"Account(%s) added contact '%s' name='%s' uri='%s' muc=%s [%p]",
	toString().c_str(),contact->toString().c_str(),contact->m_name.c_str(),
	contact->uri().c_str(),String::boolText(muc),this);
}


/**
 * ClientAccountList
 */
// Destructor
ClientAccountList::~ClientAccountList()
{
    TelEngine::destruct(m_localContacts);
}

// Check if a contact is locally stored
bool ClientAccountList::isLocalContact(ClientContact* c) const
{
    return m_localContacts && c && c->account() == m_localContacts;
}

// Find an account
ClientAccount* ClientAccountList::findAccount(const String& id, bool ref)
{
    Lock lock(this);
    if (m_localContacts && m_localContacts->toString() == id)
	return (!ref || m_localContacts->ref()) ? m_localContacts : 0;
    if (!id)
	return 0;
    ObjList* o = m_accounts.find(id);
    if (o) {
	ClientAccount* a = static_cast<ClientAccount*>(o->get());
	return (!ref || a->ref()) ? a : 0;
    }
    return 0;
}

// Find an account's contact by its URI
ClientContact* ClientAccountList::findContactByUri(const String& account, const String& uri,
    bool ref)
{
    Lock lock(this);
    ClientAccount* acc = findAccount(account,false);
    return acc ? acc->findContactByUri(uri,ref) : 0;
}

// Find an account's contact
ClientContact* ClientAccountList::findContact(const String& account, const String& id,
    bool ref)
{
    Lock lock(this);
    ClientAccount* acc = findAccount(account,false);
    return acc ? acc->findContact(id,ref) : 0;
}

// Find an account's contact from a built id
ClientContact* ClientAccountList::findContact(const String& builtId, bool ref)
{
    String account;
    ClientContact::splitContactId(builtId,account);
    return findContact(account,builtId,ref);
}

// Find a contact an instance id
ClientContact* ClientAccountList::findContactByInstance(const String& id, String* instance,
    bool ref)
{
    String account,contact;
    ClientContact::splitContactInstanceId(id,account,contact,instance);
    return findContact(account,contact,ref);
}

// Find a MUC room by its id
MucRoom* ClientAccountList::findRoom(const String& id, bool ref)
{
    String account;
    ClientContact::splitContactId(id,account);
    Lock lock(this);
    ClientAccount* acc = findAccount(account);
    return acc ? acc->findRoom(id,ref) : 0;
}

// Find a MUC room by member id
MucRoom* ClientAccountList::findRoomByMember(const String& id, bool ref)
{
    String account,contact;
    ClientContact::splitContactInstanceId(id,account,contact);
    Lock lock(this);
    ClientAccount* acc = findAccount(account);
    return acc ? acc->findRoom(contact,ref) : 0;
}

// Find any contact (regular or MUC room) by its id
ClientContact* ClientAccountList::findAnyContact(const String& id, bool ref)
{
    String account;
    ClientContact::splitContactId(id,account);
    Lock lock(this);
    ClientAccount* acc = findAccount(account);
    return acc ? acc->findAnyContact(id,ref) : 0;
}

// Check if there is a single registered account and return it
ClientAccount* ClientAccountList::findSingleRegAccount(const String* skipProto, bool ref)
{
    Lock lock(this);
    ClientAccount* found = 0;
    for (ObjList* o = m_accounts.skipNull(); o; o = o->skipNext()) {
	ClientAccount* a = static_cast<ClientAccount*>(o->get());
	if (!a->resource().online() || (skipProto && *skipProto == a->protocol()))
	    continue;
	if (!found)
	    found = a;
	else {
	    found = 0;
	    break;
	}
    }
    return (found && (!ref || found->ref())) ? found : 0;
}

// Append a new account
bool ClientAccountList::appendAccount(ClientAccount* account)
{
    if (!account || findAccount(account->toString()) || !account->ref())
	return false;
    m_accounts.append(account);
    DDebug(ClientDriver::self(),DebugAll,"List(%s) added account '%s'",
	c_str(),account->toString().c_str());
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
	c_str(),obj->get()->toString().c_str());
    obj->remove();
}


/**
 * ClientContact
 */
// Constructor. Append itself to the owner's list
ClientContact::ClientContact(ClientAccount* owner, const char* id, const char* name,
    const char* uri)
    : m_name(name ? name : id), m_params(""), m_owner(owner), m_online(false),
    m_uri(uri), m_dockedChat(false), m_share("")
{
    m_dockedChat = Client::valid() && Client::self()->getBoolOpt(Client::OptDockedChat);
    m_id = id ? id : uri;
    XDebug(ClientDriver::self(),DebugAll,"ClientContact(%p) id=%s uri=%s [%p]",
	owner,m_id.c_str(),m_uri.c_str(),this);
    if (m_owner)
	m_owner->appendContact(this);
    updateShare();
    // Generate chat window name
    buildIdHash(m_chatWndName,s_chatPrefix);
}

// Constructor. Build a contact from a list of parameters.
ClientContact::ClientContact(ClientAccount* owner, const NamedList& params, const char* id,
    const char* uri)
    : m_name(params.getValue(YSTRING("name"),params)), m_params(""),
    m_owner(owner), m_online(false), m_uri(uri), m_dockedChat(false), m_share("")
{
    m_dockedChat = Client::valid() && Client::self()->getBoolOpt(Client::OptDockedChat);
    m_id = id ? id : params.c_str();
    XDebug(ClientDriver::self(),DebugAll,"ClientContact(%p) id=%s uri=%s [%p]",
	owner,m_id.c_str(),m_uri.c_str(),this);
    if (m_owner)
	m_owner->appendContact(this);
    updateShare();
    // Generate chat window name
    buildIdHash(m_chatWndName,s_chatPrefix);
}

// Constructor. Append itself to the owner's list
ClientContact::ClientContact(ClientAccount* owner, const char* id, bool mucRoom)
    : m_params(""), m_owner(owner), m_online(false), m_id(id), m_dockedChat(false),
    m_share("")
{
    if (m_owner)
	m_owner->appendContact(this,mucRoom);
    if (!mucRoom) {
	m_dockedChat = Client::valid() && Client::self()->getBoolOpt(Client::OptDockedChat);
	buildIdHash(m_chatWndName,s_chatPrefix);
    }
    updateShare();
}

// Set contact's subscription
bool ClientContact::setSubscription(const String& value)
{
    if (m_subscription == value)
	return false;
    m_subscription = value;
    m_sub = 0;
    if (m_subscription == YSTRING("both"))
	m_sub = SubFrom | SubTo;
    else if (m_subscription == YSTRING("from"))
	m_sub = SubFrom;
    else if (m_subscription == YSTRING("to"))
	m_sub = SubTo;
    return true;
}

// Check if this contact has a chat widget (window or docked item)
bool ClientContact::hasChat()
{
    Window* w = getChatWnd();
    if (!w)
	return false;
    if (m_dockedChat)
	return Client::self()->getTableRow(s_dockedChatWidget,toString(),0,w);
    return true;
}

// Flash chat window/item to notify the user
void ClientContact::flashChat(bool on)
{
    Window* w = getChatWnd();
    if (!w)
	return;
    if (on)
	Client::self()->setUrgent(w->id(),true,w);
    if (m_dockedChat)
	flashItem(on,s_dockedChatWidget,toString(),w);
}

// Send chat to contact (enqueue a msg.execute message)
bool ClientContact::sendChat(const char* body, const String& res,
    const String& type, const char* state)
{
    Message* m = Client::buildMessage("msg.execute",accountName());
    m->addParam("type",type,false);
    m->addParam("called",m_uri);
    m->addParam("called_instance",res,false);
    m->addParam("body",body);
    if (mucRoom())
	m->addParam("muc",String::boolText(true));
    if (!TelEngine::null(state) && (!type || type == YSTRING("chat") || type == YSTRING("groupchat")))
	m->addParam("chatstate",state);
    return Engine::enqueue(m);
}

// Retrieve the contents of the chat input widget
void ClientContact::getChatInput(String& text, const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    if (m_dockedChat) {
	NamedList p("");
	p.addParam(name,"");
	Client::self()->getTableRow(s_dockedChatWidget,toString(),&p,w);
	text = p[name];
    }
    else
	Client::self()->getText(name,text,false,w);
}

// Set the chat input widget text
void ClientContact::setChatInput(const String& text, const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    if (m_dockedChat) {
	NamedList p("");
	p.addParam(name,text);
	Client::self()->setTableRow(s_dockedChatWidget,toString(),&p,w);
    }
    else
	Client::self()->setText(name,text,false,w);
}

// Retrieve the contents of the chat history widget
void ClientContact::getChatHistory(String& text, bool richText, const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    if (m_dockedChat) {
	String param;
	if (richText)
	    param << "getrichtext:";
	param << name;
	NamedList p("");
	p.addParam(param,"");
	Client::self()->getTableRow(s_dockedChatWidget,toString(),&p,w);
	text = p[param];
    }
    else
	Client::self()->getText(name,text,richText,w);
}

// Set the contents of the chat history widget
void ClientContact::setChatHistory(const String& text, bool richText, const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    if (m_dockedChat) {
	NamedList p("");
	if (richText)
	    p.addParam("setrichtext:" + name,text);
	else
	    p.addParam(name,text);
	Client::self()->setTableRow(s_dockedChatWidget,toString(),&p,w);
    }
    else
	Client::self()->setText(name,text,richText,w);
}

// Add an entry to chat history
void ClientContact::addChatHistory(const String& what, NamedList*& params, const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name && params)) {
	TelEngine::destruct(params);
	return;
    }
    NamedList* lines = new NamedList("");
    lines->addParam(new NamedPointer(what,params,String::boolText(true)));
    if (m_dockedChat) {
	NamedList p("");
	p.addParam(new NamedPointer("addlines:" + name,lines));
	Client::self()->setTableRow(s_dockedChatWidget,toString(),&p,w);
    }
    else {
	Client::self()->addLines(name,lines,0,false,w);
	TelEngine::destruct(lines);
    }
    params = 0;
}

// Retrieve a chat widget' property
void ClientContact::getChatProperty(const String& name, const String& prop,
    String& value)
{
    Window* w = getChatWnd();
    if (!(w && name && prop))
	return;
    if (m_dockedChat) {
	String param;
	param << "property:" << name << ":" << prop;
	NamedList p("");
	p.addParam(param,"");
	Client::self()->getTableRow(s_dockedChatWidget,toString(),&p,w);
	value = p[param];
    }
    else
	Client::self()->getProperty(name,prop,value,w);
}

// Set a chat widget' property
void ClientContact::setChatProperty(const String& name, const String& prop,
    const String& value)
{
    Window* w = getChatWnd();
    if (!(w && name && prop))
	return;
    if (m_dockedChat) {
	NamedList p("");
	p.addParam("property:" + name + ":" + prop,value);
	Client::self()->setTableRow(s_dockedChatWidget,toString(),&p,w);
    }
    else
	Client::self()->setProperty(name,prop,value,w);
}

// Show or hide this contact's chat window or docked item
bool ClientContact::showChat(bool visible, bool active)
{
    Window* w = getChatWnd();
    if (!w)
	return false;
    if (!visible) {
	if (m_dockedChat)
	    return Client::self()->delTableRow(s_dockedChatWidget,toString(),w);
	return Client::self()->setVisible(m_chatWndName,false);
    }
    bool ok = Client::self()->getVisible(w->id()) || Client::self()->setVisible(w->id(),true);
    if (active) {
	if (m_dockedChat)
	    Client::self()->setSelect(s_dockedChatWidget,toString(),w);
	Client::self()->setActive(w->id(),true,w);
    }
    return ok;
}

// Get the chat window
Window* ClientContact::getChatWnd()
{
    if (!Client::valid())
	return 0;
    if (mucRoom())
	return Client::self()->getWindow(s_mucsWnd);
    if (m_dockedChat)
	return Client::self()->getWindow(s_dockedChatWnd);
    return Client::self()->getWindow(m_chatWndName);
}

// Create the chat window
void ClientContact::createChatWindow(bool force, const char* name)
{
    if (force)
	destroyChatWindow();
    if (hasChat())
	return;
    if (!Client::valid())
	return;
    if (m_dockedChat) {
	Window* w = getChatWnd();
	if (!w)
	    return;
	Client::self()->addTableRow(s_dockedChatWidget,toString(),0,false,w);
	return;
    }
    if (TelEngine::null(name))
	name = s_chatPrefix;
    Client::self()->createWindowSafe(name,m_chatWndName);
    Window* w = getChatWnd();
    if (!w)
	return;
    NamedList p("");
    p.addParam("context",toString());
    updateChatWindow(p);
}

// Update the chat window
void ClientContact::updateChatWindow(const NamedList& params, const char* title,
    const char* icon)
{
    Window* w = getChatWnd();
    if (!w)
	return;
    if (m_dockedChat) {
	Client::self()->setTableRow(s_dockedChatWidget,toString(),&params,w);
	return;
    }
    NamedList p(params);
    p.addParam("title",title,false);
    p.addParam("image:" + m_chatWndName,icon,false);
    Client::self()->setParams(&p,w);
}

// Check if the contact chat is active
bool ClientContact::isChatActive()
{
    Window* w = getChatWnd();
    if (!w)
	return false;
    // We are in the client's thread: use the Window's method
    if (!w->active())
	return false;
    if (!m_dockedChat)
	return true;
    String sel;
    Client::self()->getSelect(s_dockedChatWidget,sel,w);
    return sel == toString();
}

// Close the chat window or destroy docked chat item
void ClientContact::destroyChatWindow()
{
    Window* w = getChatWnd();
    if (!w)
	return;
    if (m_dockedChat)
	Client::self()->delTableRow(s_dockedChatWidget,toString(),w);
    else
	Client::self()->closeWindow(m_chatWndName,false);
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
	m_owner ? m_owner->toString().c_str() : "",m_uri.c_str(),group.c_str(),this);
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
	m_owner ? m_owner->toString().c_str() : "",m_uri.c_str(),group.c_str(),this);
    return true;
}

// Replace contact's groups from a list of parameters (handle 'group' parameters)
bool ClientContact::setGroups(const NamedList& list, const String& param)
{
    Lock lock(m_owner);
    ObjList* grps = 0;
    NamedIterator iter(list);
    const NamedString* ns = 0;
    while (0 != (ns = iter.get())) {
	if (ns->name() != param)
	    continue;
	if (!grps)
	    grps = new ObjList;
	grps->append(new String(*ns));
    }
    if (grps) {
	bool changed = false;
	String oldGrps, newGrps;
	oldGrps.append(m_groups,",");
	newGrps.append(grps,",");
	changed = (oldGrps != newGrps);
	if (changed) {
	    m_groups.clear();
	    for (ObjList* o = grps->skipNull(); o; o = o->skipNext())
		appendGroup(o->get()->toString());
	}
	TelEngine::destruct(grps);
	return changed;
    }
    if (m_groups.skipNull()) {
	m_groups.clear();
	return true;
    }
    return false;
}

// Find the resource with the lowest status
ClientResource* ClientContact::status(bool ref)
{
    ClientResource* res = 0;
    for (ObjList* o = resources().skipNull(); o; o = o->skipNext()) {
	ClientResource* r = static_cast<ClientResource*>(o->get());
	if (!res || res->m_status > r->m_status)
	    res = r;
	if (res->m_status == ClientResource::Online)
	    break;
    }
    return (res && (!ref || res->ref())) ? res : 0;
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
	if ((static_cast<ClientResource*>(o->get()))->caps().flag(ClientResource::CapAudio))
	    break;
    if (!o)
	return 0;
    ClientResource* r = static_cast<ClientResource*>(o->get());
    return (!ref || r->ref()) ? r : 0;
}

// Get the first resource with file transfer capability capability
ClientResource* ClientContact::findFileTransferResource(bool ref)
{
    Lock lock(m_owner);
    ObjList* o = m_resources.skipNull();
    for (; o; o = o->skipNext())
	if ((static_cast<ClientResource*>(o->get()))->caps().flag(ClientResource::CapFileTransfer))
	    break;
    if (!o)
	return 0;
    ClientResource* r = static_cast<ClientResource*>(o->get());
    return (!ref || r->ref()) ? r : 0;
}

// Append a resource having a given id
ClientResource* ClientContact::appendResource(const String& id)
{
    if (findResource(id))
	return 0;
    ClientResource* r = new ClientResource(id);
    if (!insertResource(r))
	TelEngine::destruct(r);
    return r;
}

// Insert a resource in the list by its priority.
// If the resource is already there it will be extracted and re-inserted
bool ClientContact::insertResource(ClientResource* res)
{
    if (!res || findResource(res->toString()))
	return false;
    ObjList* found = m_resources.find(res);
    if (found)
	found->remove(false);
    // Insert it
    ObjList* o = m_resources.skipNull();
    for (; o; o = o->skipNext()) {
	ClientResource* r = static_cast<ClientResource*>(o->get());
	if (r->m_priority < res->m_priority)
	    break;
    }
    if (o)
	o->insert(res);
    else
	m_resources.append(res);
    if (!found)
	DDebug(ClientDriver::self(),DebugAll,
	    "Account(%s) contact='%s' added resource '%s' prio=%d [%p]",
	    accountName().c_str(),m_uri.c_str(),
	    res->toString().c_str(),res->m_priority,this);
    return true;
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
	m_owner ? m_owner->toString().c_str() : "",m_uri.c_str(),id.c_str(),this);
    return true;
}

// (re)load shared list
void ClientContact::updateShare()
{
    m_share.clearParams();
    if (!(account() && uri()))
	return;
    NamedList* sect = account()->m_cfg.getSection("share " + uri());
    if (!sect)
	return;
    for (int n = 1; true; n++) {
	String s(n);
	NamedString* ns = sect->getParam(s);
	if (!ns)
	    break;
	if (!*ns)
	    continue;
	setShareDir((*sect)[s + ".name"],*ns,false);
    }
}

// Save share list
void ClientContact::saveShare()
{
    if (!(account() && uri()))
	return;
    String tmp;
    tmp << "share " << uri();
    NamedList* sect = account()->m_cfg.getSection(tmp);
    bool changed = false;
    if (haveShare()) {
	if (!sect)
	    sect = account()->m_cfg.createSection(tmp);
	sect->clearParams();
	NamedIterator iter(m_share);
	int n = 1;
	for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	    String s(n++);
	    sect->addParam(s,ns->name());
	    if (*ns && *ns != ns->name())
		sect->addParam(s + ".name",*ns);
	}
	changed = true;
    }
    else if (sect) {
	changed = true;
	account()->m_cfg.clearSection(tmp);
    }
    if (!changed)
	return;
    if (account()->m_cfg.save())
	return;
    int code = Thread::lastError();
    String s;
    Thread::errorString(s,code);
    Debug(ClientDriver::self(),DebugNote,
	"Account(%s) contact='%s' failed to save shared: %d %s [%p]",
	m_owner ? m_owner->toString().c_str() : "",m_uri.c_str(),code,s.c_str(),this);
}

// Clear share list
void ClientContact::clearShare()
{
    if (!haveShare())
	return;
    m_share.clearParams();
    saveShare();
}

// Set a share directory
bool ClientContact::setShareDir(const String& shareName, const String& dirPath, bool save)
{
    String path;
    if (!Client::removeEndsWithPathSep(path,dirPath))
	return false;
    String name = shareName;
    if (!name)
	Client::getLastNameInPath(name,path);
    NamedString* ns = m_share.getParam(path);
    NamedString* other = Client::findParamByValue(m_share,name,ns);
    if (other)
	return false;
    bool changed = false;
    if (ns) {
	if (*ns != name) {
	    changed = true;
	    *ns = name;
	}
    }
    else {
	changed = true;
	m_share.addParam(path,name);
    }
    if (changed && save)
	saveShare();
    return changed;
}

// Remove a shared item
bool ClientContact::removeShare(const String& name, bool save)
{
    NamedString* ns = m_share.getParam(name);
    if (!ns)
	return false;
    m_share.clearParam(ns);
    if (save)
	saveShare();
    return true;
}

// Check if the list of shared contains something
bool ClientContact::haveShared() const
{
    for (ObjList* o = m_shared.skipNull(); o; o = o->skipNext()) {
	ClientDir* d = static_cast<ClientDir*>(o->get());
	if (d->children().skipNull())
	    return true;
    }
    return false;
}

// Retrieve shared data for a given resource
ClientDir* ClientContact::getShared(const String& name, bool create)
{
    if (!name)
	return 0;
    ObjList* o = m_shared.find(name);
    if (!o && create)
	o = m_shared.append(new ClientDir(name));
    return o ? static_cast<ClientDir*>(o->get()) : 0;
}

// Remove shared data
bool ClientContact::removeShared(const String& name, ClientDir** removed)
{
    bool chg = false;
    if (name) {
	GenObject* gen = m_shared.remove(name,false);
	chg = (gen != 0);
	if (removed)
	    *removed = static_cast<ClientDir*>(gen);
	else
	    TelEngine::destruct(gen);
    }
    else {
	chg = (0 != m_shared.skipNull());
	m_shared.clear();
    }
    return chg;
}

// Split a contact instance id in account/contact/instance parts
void ClientContact::splitContactInstanceId(const String& src, String& account,
    String& contact, String* instance)
{
    int pos = src.find('|');
    if (pos < 0) {
	account = src.uriUnescape();
	return;
    }
    account = src.substr(0,pos).uriUnescape();
    int pp = src.find('|',pos + 1);
    if (pp > pos) {
	contact = src.substr(0,pp);
	if (instance)
	    *instance = src.substr(pp + 1).uriUnescape();
    }
    else
	contact = src;
}

// Remove from owner
void ClientContact::removeFromOwner()
{
    if (!m_owner)
	return;
    Lock lock(m_owner);
    m_owner->removeContact(m_id,false);
    m_owner = 0;
}

// Remove from owner. Release data
void ClientContact::destroyed()
{
    // Remove from owner now to make sure the contact is not deleted
    // from owner while beeing destroyed
    removeFromOwner();
    if (!mucRoom() && Client::valid() &&
	Client::self()->getBoolOpt(Client::OptDestroyChat))
	destroyChatWindow();
    RefObject::destroyed();
}


/*
 * MucRoom
 */
// Constructor
MucRoom::MucRoom(ClientAccount* owner, const char* id, const char* name,
    const char* uri, const char* nick)
    : ClientContact(owner,id,true),
    m_index(0),
    m_resource(0)
{
    String rid;
    buildInstanceId(rid,m_id);
    m_resource = new MucRoomMember(rid,nick);
    m_name = name;
    m_uri = uri;
    if (!owner)
	return;
    if (owner->contact())
	m_resource->m_uri = owner->contact()->uri();
    m_resource->m_instance = owner->resource().toString();
}

// Check if the user can kick a given room member
bool MucRoom::canKick(MucRoomMember* member) const
{
    if (!(member && available()) || ownMember(member))
	return false;
    return m_resource->m_role == MucRoomMember::Moderator &&
	member->m_role > MucRoomMember::RoleNone &&
	member->m_role < MucRoomMember::Moderator;
}

// Check if the user can ban a given room member
bool MucRoom::canBan(MucRoomMember* member) const
{
    if (!(member && available()) || ownMember(member))
	return false;
    // Only admins and owners are allowed to ban non admin/owners
    return m_resource->m_affiliation >= MucRoomMember::Admin &&
	member->m_affiliation < MucRoomMember::Admin;
}

// Build a muc.room message used to login/logoff
Message* MucRoom::buildJoin(bool join, bool history, unsigned int sNewer)
{
    Message* m = buildMucRoom(join ? "login" : "logout");
    m->addParam("nick",m_resource->m_name,false);
    if (!join)
	return m;
    m->addParam("password",m_password,false);
    m->addParam("history",String::boolText(history));
    if (history) {
	if (sNewer)
	    m->addParam("history.newer",String(sNewer));
    }
    return m;
}

// Retrieve a room member (or own member) by its nick
MucRoomMember* MucRoom::findMember(const String& nick)
{
    if (nick == m_resource->m_name)
	return m_resource;
    for (ObjList* o = m_resources.skipNull(); o; o = o->skipNext()) {
	MucRoomMember* r = static_cast<MucRoomMember*>(o->get());
	if (nick == r->m_name)
	    return r;
    }
    return 0;
}

// Retrieve a room member (or own member) by its contact and instance
MucRoomMember* MucRoom::findMember(const String& contact, const String& instance)
{
    if (!(contact && instance))
	return 0;
    if (m_resource->m_instance == instance && (m_resource->m_uri &= contact))
	return m_resource;
    for (ObjList* o = m_resources.skipNull(); o; o = o->skipNext()) {
	MucRoomMember* r = static_cast<MucRoomMember*>(o->get());
	if (r->m_instance == instance && (r->m_uri &= contact))
	    return r;
    }
    return 0;
}

// Retrieve a room member (or own member) by its id
MucRoomMember* MucRoom::findMemberById(const String& id)
{
    if (ownMember(id))
	return m_resource;
    return static_cast<MucRoomMember*>(findResource(id));
}

// Check if a given member has chat displayed
bool MucRoom::hasChat(const String& id)
{
    Window* w = getChatWnd();
    return w && Client::self()->getTableRow(s_dockedChatWidget,id,0,w);
}

// Flash chat window/item to notify the user
void MucRoom::flashChat(const String& id, bool on)
{
    Window* w = getChatWnd();
    if (!w)
	return;
    if (on)
	Client::self()->setUrgent(w->id(),true,w);
    flashItem(on,s_dockedChatWidget,id,w);
}

// Retrieve the contents of the chat input widget
void MucRoom::getChatInput(const String& id, String& text, const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    NamedList p("");
    p.addParam(name,"");
    Client::self()->getTableRow(s_dockedChatWidget,id,&p,w);
    text = p[name];
}

// Set the chat input widget text
void MucRoom::setChatInput(const String& id, const String& text, const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    NamedList p("");
    p.addParam(name,text);
    Client::self()->setTableRow(s_dockedChatWidget,id,&p,w);
}

// Retrieve the contents of the chat history widget
void MucRoom::getChatHistory(const String& id, String& text, bool richText,
    const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    String param;
    if (richText)
	param << "getrichtext:";
    param << name;
    NamedList p("");
    p.addParam(param,"");
    Client::self()->getTableRow(s_dockedChatWidget,id,&p,w);
    text = p[param];
}

// Set the contents of the chat history widget
void MucRoom::setChatHistory(const String& id, const String& text, bool richText,
    const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name))
	return;
    NamedList p("");
    if (richText)
	p.addParam("setrichtext:" + name,text);
    else
	p.addParam(name,text);
    Client::self()->setTableRow(s_dockedChatWidget,id,&p,w);
}

// Add an entry to chat history
void MucRoom::addChatHistory(const String& id, const String& what, NamedList*& params,
    const String& name)
{
    Window* w = getChatWnd();
    if (!(w && name && params)) {
	TelEngine::destruct(params);
	return;
    }
    NamedList* lines = new NamedList("");
    lines->addParam(new NamedPointer(what,params,String::boolText(true)));
    NamedList p("");
    p.addParam(new NamedPointer("addlines:" + name,lines));
    Client::self()->setTableRow(s_dockedChatWidget,id,&p,w);
    params = 0;
}

// Set a chat widget' property
void MucRoom::setChatProperty(const String& id, const String& name, const String& prop,
    const String& value)
{
    Window* w = getChatWnd();
    if (!(w && name && prop))
	return;
    NamedList p("");
    p.addParam("property:" + name + ":" + prop,value);
    Client::self()->setTableRow(s_dockedChatWidget,id,&p,w);
}

// Show or hide a member's chat
bool MucRoom::showChat(const String& id, bool visible, bool active)
{
    Window* w = getChatWnd();
    if (!w)
	return false;
    if (!visible)
	return Client::self()->delTableRow(s_dockedChatWidget,id,w);
    bool ok = Client::self()->setVisible(w->id(),true);
    if (active) {
	Client::self()->setSelect(s_dockedChatWidget,id,w);
	Client::self()->setActive(w->id(),true,w);
    }
    return ok;
}

// Create a member's chat
void MucRoom::createChatWindow(const String& id, bool force, const char* name)
{
    if (force)
	destroyChatWindow(id);
    if (hasChat(id))
	return;
    if (!Client::valid())
	return;
    MucRoomMember* m = static_cast<MucRoomMember*>(findResource(id,true));
    Window* w = m ? getChatWnd() : 0;
    if (w) {
	NamedList p("");
	p.addParam("item_type",ownMember(m) ? "mucroom" : "mucprivchat");
	Client::self()->addTableRow(s_dockedChatWidget,id,&p,false,w);
    }
    TelEngine::destruct(m);
}

// Update member parameters in chat window
void MucRoom::updateChatWindow(const String& id, const NamedList& params)
{
    Window* w = getChatWnd();
    if (w)
	Client::self()->setTableRow(s_dockedChatWidget,id,&params,w);
}

// Check if the contact chat is active
bool MucRoom::isChatActive(const String& id)
{
    Window* w = getChatWnd();
    if (!w)
	return false;
    // We are in the client's thread: use the Window's method
    if (!w->active())
	return false;
    String sel;
    Client::self()->getSelect(s_dockedChatWidget,sel,w);
    return sel == id;
}

// Close a member's chat
void MucRoom::destroyChatWindow(const String& id)
{
    Window* w = getChatWnd();
    if (!w)
	return;
    if (id)
	Client::self()->delTableRow(s_dockedChatWidget,id,w);
    else {
	NamedList tmp("");
	tmp.addParam(m_resource->toString(),"");
	for (ObjList* o = m_resources.skipNull(); o; o = o->skipNext())
	    tmp.addParam(o->get()->toString(),"");
	Client::self()->updateTableRows(s_dockedChatWidget,&tmp,false,w);
    }
}

// Retrieve a room member (or own member) by its id
ClientResource* MucRoom::findResource(const String& id, bool ref)
{
    ClientResource* res = 0;
    if (m_resource->toString() == id)
	res = static_cast<ClientResource*>(m_resource);
    else
	res = ClientContact::findResource(id,false);
    return (res && (!ref || res->ref())) ? res : 0;
}

// Append a member having a given nick
ClientResource* MucRoom::appendResource(const String& nick)
{
    if (!nick || findMember(nick))
	return 0;
    String id;
    buildInstanceId(id,String(++m_index));
    MucRoomMember* m = new MucRoomMember(id,nick);
    m_resources.append(m);
    return static_cast<ClientResource*>(m);
}

// Remove a contact having a given nick
bool MucRoom::removeResource(const String& nick, bool delChat)
{
    MucRoomMember* member = findMember(nick);
    if (!member || ownMember(member))
	return false;
    if (delChat)
	destroyChatWindow(member->toString());
    m_resources.remove(member);
    return true;
}

// Release data
void MucRoom::destroyed()
{
    Debug(ClientDriver::self(),DebugAll,"MucRoom(%s) account=%s destroyed [%p]",
	uri().c_str(),accountName().c_str(),this);
    if (!m_resource->offline() && m_owner)
	Engine::enqueue(buildJoin(false));
    // Remove from owner now to make sure the contact is not deleted
    // from owner while beeing destroyed
    removeFromOwner();
    destroyChatWindow();
    TelEngine::destruct(m_resource);
    ClientContact::destroyed();
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
    if (!m_started)
	Debug(ClientDriver::self(),DebugNote,"Failed to start sound %s",c_str());
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
    chan->initChan();
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


//
// ClientDir
//
// Recursively check if all (sub)directores were updated
bool ClientDir::treeUpdated() const
{
    if (!m_updated)
	return false;
    for (ObjList* o = m_children.skipNull(); o; o = o->skipNext()) {
	ClientFileItem* it = static_cast<ClientFileItem*>(o->get());
	ClientDir* dir = it->directory();
	if (dir && !dir->treeUpdated())
	    return false;
    }
    return true;
}

// Build and add a sub-directory if not have one already
ClientDir* ClientDir::addDir(const String& name)
{
    if (!name)
	return 0;
    ClientFileItem* it = findChild(name);
    if (it) {
	if (it->directory())
	    return it->directory();
    }
    ClientDir* d = new ClientDir(name);
    addChild(d);
    return d;
}

// Build sub directories from path
ClientDir* ClientDir::addDirPath(const String& path, const char* sep)
{
    if (!path)
	return 0;
    if (TelEngine::null(sep))
	return addDir(path);
    int pos = path.find(sep);
    if (pos < 0)
	return addDir(path);
    String rest = path.substr(pos + 1);
    String name = path.substr(0,pos);
    ClientDir* d = this;
    if (name)
	d = addDir(name);
    if (!d)
	return 0;
    return rest ? d->addDirPath(rest) : d;
}

// Add a copy of known children types
void ClientDir::copyChildren(const ObjList& list)
{
    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	ClientFileItem* item = static_cast<ClientFileItem*>(o->get());
	if (item->file())
	    addChild(new ClientFile(*item->file()));
	else if (item->directory())
	    addChild(new ClientDir(*item->directory()));
    }
}

// Add a list of children, consume the objects
void ClientDir::addChildren(ObjList& list)
{
    for (ObjList* o = list.skipNull(); o; o = o->skipNull()) {
	ClientFileItem* item = static_cast<ClientFileItem*>(o->remove(false));
	addChild(item);
    }
}

// Add/replace an item
bool ClientDir::addChild(ClientFileItem* item)
{
    if (!item)
	return false;
    ObjList* last = 0;
    for (ObjList* o = m_children.skipNull(); o; o = o->skipNext()) {
	ClientFileItem* it = static_cast<ClientFileItem*>(o->get());
	if (it == item)
	    return true;
	if (it->name() == item->name()) {
	    o->remove();
	    o->append(item);
	    return true;
	}
	ObjList* tmp = o->skipNext();
	if (!tmp) {
	    last = o;
	    break;
	}
	o = tmp;
    }
    if (last)
	last->append(item);
    else
	m_children.append(item);
    return true;
}

// Find a child by path
ClientFileItem* ClientDir::findChild(const String& path, const char* sep)
{
    if (!path)
	return 0;
    if (TelEngine::null(sep))
	return findChildName(path);
    int pos = path.find(sep);
    if (pos < 0)
	return findChildName(path);
    String rest = path.substr(pos + 1);
    String name = path.substr(0,pos);
    if (!name)
	return findChild(rest,sep);
    ClientFileItem* ch = findChildName(name);
    if (!ch)
	return 0;
    // Nothing more in the path, return found child
    if (!name)
	return ch;
    // Found child can't contain children: error
    ClientDir* d = ch->directory();
    if (d)
	return d->findChild(rest,sep);
    return 0;
}


//
// NamedInt
//
// Add an item to a list. Replace existing item with the same name
void NamedInt::addToListUniqueName(ObjList& list, NamedInt* obj)
{
    if (!obj)
	return;
    ObjList* last = &list;
    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	NamedInt* ni = static_cast<NamedInt*>(o->get());
	if (*ni == *obj) {
	    o->set(obj);
	    return;
	}
	last = o;
    }
    last->append(obj);
}

// Clear all items with a given value
void NamedInt::clearValue(ObjList& list, int val)
{
    for (ObjList* o = list.skipNull(); o;) {
	NamedInt* ni = static_cast<NamedInt*>(o->get());
	if (ni->value() != val)
	    o = o->skipNext();
	else {
	    o->remove();
	    o = o->skipNull();
	}
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
