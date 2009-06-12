/**
 * ClientLogic.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Default client logic
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

// Contact parameters to be copied from lists 
static String s_contactParams = "contact,subscription,group,ask";
// Some UI widgets
static String s_channelList = "channels";
static String s_accountList = "accounts";
static String s_contactList = "contacts";
static String s_logOutgoing = "log_outgoing";
static String s_logIncoming = "log_incoming";
static String s_calltoList = "callto";
// Actions
static String s_actionCall = "call";
static String s_actionAnswer = "answer";
static String s_actionHangup = "hangup";
static String s_actionTransfer = "transfer";
static String s_actionConf = "conference";
static String s_actionHold = "hold";
// Not selected string(s)
static String s_notSelected = "-none-";
// Maximum number of call log entries
static unsigned int s_maxCallHistory = 20;

ObjList ClientLogic::s_accOptions;
ObjList ClientLogic::s_protocols;
Mutex ClientLogic::s_protocolsMutex(true,"ClientProtocols");
// Parameters that are applied from provider template
const char* ClientLogic::s_provParams[] = {
    "server",
    "domain",
    "outbound",
    "port",
    0
};

// strings used for completing account parameters
static String s_accParams[] = {
    "username", "password", "server", "domain",
    "outbound",  "options", ""
};

// Update protocol in account window
static inline void selectProtocolSpec(NamedList& p, const String& proto, bool advanced)
{
    p.setParam("select:acc_protocol",proto);
    if (advanced)
	p.setParam("select:acc_proto_spec","acc_proto_spec_" + proto);
    else
	p.setParam("select:acc_proto_spec","acc_proto_spec_none");
}

// Utility function used to set a widget's text
static inline void setAccParam(NamedList& params, const String& prefix,
    const String& param, const char* defVal)
{
    NamedString* ns = params.getParam("acc_" + param);
    params.setParam(prefix + "_" + param,ns ? ns->c_str() : defVal);
}

// Set the image parameter of a list
static inline void setImageParam(NamedList& p, const char* param,
	const String& image)
{
    static String suffix = "_image";
    p.setParam(param + suffix,Client::s_skinPath + image);
}

// Set a list parameter and it's image
static inline void setImageParam(NamedList& p, const char* param,
	const char* value, const String& image)
{
    p.setParam(param,value);
    setImageParam(p,param,image);
}

// Update protocol specific data
// Set protocol specific widgets: options, address, port ....
// Text widgets' name should start with acc_proto_[protocol]_
// Option widgets' name should start with acc_proto_[protocol]_opt_
static void updateProtocolSpec(NamedList& p, const String& proto, const String& options)
{
    ObjList* obj = options.split(',',false);
    String prefix = "acc_proto_" + proto;
    // Texts
    setAccParam(p,prefix,"resource",proto == "jabber" ? "yate" : "");
    setAccParam(p,prefix,"port",proto == "jabber" ? "5222" : "");
    setAccParam(p,prefix,"address","");
    // Options
    prefix << "_opt_";
    for (ObjList* o = ClientLogic::s_accOptions.skipNull(); o; o = o->skipNext()) {
	String* opt = static_cast<String*>(o->get());
	bool checked = (0 != obj->find(*opt));
	p.setParam("check:" + prefix + *opt,String::boolText(checked));
    }
    TelEngine::destruct(obj);
}

// Utility function used to build channel status
static void buildStatus(String& status, const char* stat, const char* addr,
    const char* id, const char* reason = 0)
{
    status << stat;
    if (addr || id)
	status << ": " << (addr ? addr : id);
    if (reason)
	status << " reason: " << reason;
}

// Check if a given parameter is present in a list.
// Update it from UI if not present or empty
static bool checkParam(NamedList& p, const char* param, const String& widget,
    bool checkNotSel, Window* wnd = 0)
{
    NamedString* tmp = p.getParam(param);
    if (tmp && *tmp)
	return true;
    if (!Client::self())
	return false;
    String value;
    Client::self()->getText(widget,value,false,wnd);
    value.trimBlanks();
    bool ok = value && !(checkNotSel && value.matches(Client::s_notSelected));
    if (ok)
	p.setParam(param,value);
    return ok;
}

// Check if a given contact can be modified
static bool checkContactEdit(NamedList& p, Window* parent)
{
    if (p.getBoolValue("hidden:editable",true))
	return true;
    Client::openMessage(String("Contact ") + p.getValue("name") + " can't be modified !",parent);
    return false;
}

// Utility: activate the calls page
inline void activatePageCalls(ClientLogic* logic, Window* wnd = 0)
{
    static String s_buttonCalls = "ctrlCalls";
    static String s_toggleCalls = "selectitem:framePages:PageCalls";
    Client::self()->setCheck(s_buttonCalls,true,wnd);
    logic->toggle(wnd,s_toggleCalls,true);
}


/**
 * ClientLogic
 */
// Constructor
ClientLogic::ClientLogic(const char* name, int priority)
    : m_durationMutex(true,"ClientLogic::duration"), m_name(name), m_prio(priority)
{
    Debug(ClientDriver::self(),DebugAll,"ClientLogic(%s) [%p]",m_name.c_str(),this);
    Client::addLogic(this);
}

// destructor
ClientLogic::~ClientLogic()
{
    Debug(ClientDriver::self(),DebugAll,"ClientLogic(%s) destroyed [%p]",m_name.c_str(),this);
    clearDurationUpdate();
    Client::removeLogic(this);
}

// obtain the name of the object
const String& ClientLogic::toString() const 
{
    return m_name;
}

// function which interprets given parameters and takes appropiate action
bool ClientLogic::setParams(const NamedList& params)
{
    bool ok = true;
    unsigned int l = params.length();
    for (unsigned int i = 0; i < l; i++) {
	const NamedString* s = params.getParam(i);
	if (s) {
	    String n(s->name());
	    if (n.startSkip("show:",false))
		ok = Client::self()->setShow(n,s->toBoolean()) && ok;
	    else if (n.startSkip("active:",false))
		ok = Client::self()->setActive(n,s->toBoolean()) && ok;
	    else if (n.startSkip("focus:",false))
		ok = Client::self()->setFocus(n,s->toBoolean()) && ok;
	    else if (n.startSkip("check:",false))
		ok = Client::self()->setCheck(n,s->toBoolean()) && ok;
	    else if (n.startSkip("select:",false))
		ok = Client::self()->setSelect(n,*s) && ok;
	    else if (n.find(':') < 0)
		ok = Client::self()->setText(n,*s) && ok;
	    else
		ok = false;
	}
    }
    return ok;
}

// Add a duration object to this client's list
bool ClientLogic::addDurationUpdate(DurationUpdate* duration, bool autoDelete)
{
    if (!duration)
	return false;
    Lock lock(m_durationMutex);
    m_durationUpdate.append(duration)->setDelete(autoDelete);
    DDebug(ClientDriver::self(),DebugInfo,
	"Logic(%s) added duration ('%s',%p) owner=%u",
	m_name.c_str(),duration->toString().c_str(),duration,autoDelete); 
    return true;
}

// Remove a duration object from list
bool ClientLogic::removeDurationUpdate(const String& name, bool delObj)
{
    if (!name)
	return false;
    Lock lock(m_durationMutex);
    DurationUpdate* duration = findDurationUpdate(name,false);
    if (!duration)
	return false;
    m_durationUpdate.remove(duration,false);
    DDebug(ClientDriver::self(),DebugInfo,
	"Logic(%s) removed duration ('%s',%p) delObj=%u",
	m_name.c_str(),duration->toString().c_str(),duration,delObj); 
    lock.drop();
    duration->setLogic(0);
    if (delObj)
	TelEngine::destruct(duration);
    return true;
}

// Remove a duration object from list
bool ClientLogic::removeDurationUpdate(DurationUpdate* duration, bool delObj)
{
    if (!duration)
	return false;
    Lock lock(m_durationMutex);
    ObjList* obj = m_durationUpdate.find(duration);
    if (!obj)
	return false;
    obj->remove(false);
    DDebug(ClientDriver::self(),DebugInfo,
	"Logic(%s) removed duration ('%s',%p) delObj=%u",
	m_name.c_str(),duration->toString().c_str(),duration,delObj); 
    lock.drop();
    duration->setLogic(0);
    if (delObj)
	TelEngine::destruct(duration);
    return true;
}

// Find a duration update by its name
DurationUpdate* ClientLogic::findDurationUpdate(const String& name, bool ref)
{
    Lock lock(m_durationMutex);
    ObjList* obj = m_durationUpdate.find(name);
    if (!obj)
	return 0;
    DurationUpdate* duration = static_cast<DurationUpdate*>(obj->get());
    return (!ref || duration->ref()) ? duration : 0;
}

// Remove all duration objects
void ClientLogic::clearDurationUpdate()
{
    Lock lock(m_durationMutex);
    // Reset logic pointer: some of them may not be destroyed when clearing the list
    ListIterator iter(m_durationUpdate);
    for (GenObject* o = 0; 0 != (o = iter.get());)
	(static_cast<DurationUpdate*>(o))->setLogic();
    m_durationUpdate.clear();
}

// Release memory. Remove from client's list
void ClientLogic::destruct()
{
    clearDurationUpdate();
    Client::removeLogic(this);
    GenObject::destruct();
}

// Init static logic data
void ClientLogic::initStaticData()
{
    // Build account options list
    if (!s_accOptions.skipNull()) {
	s_accOptions.append(new String("allowplainauth"));
	s_accOptions.append(new String("noautorestart"));
	s_accOptions.append(new String("oldstyleauth"));
	s_accOptions.append(new String("tlsrequired"));
    }

    // Build protocol list
    s_protocolsMutex.lock();
    if (!s_protocols.skipNull()) {
	s_protocols.append(new String("sip"));
	s_protocols.append(new String("jabber"));
	s_protocols.append(new String("h323"));
	s_protocols.append(new String("iax"));
    }
    s_protocolsMutex.unlock();
}

// Called when the user selected a line
bool ClientLogic::line(const String& name, Window* wnd)
{
    int l = name.toInteger(-1);
    if (l >= 0 && Client::self()) {
	Client::self()->line(l);
	return true;
    }
    return false;
}

// Show/hide widget(s)
bool ClientLogic::display(NamedList& params, bool widget, Window* wnd)
{
    if (!Client::self())
	return false;

    bool result = false;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* p = params.getParam(i);
	if (!p)
	    continue;
	bool tmp = false;
	if (widget)
	    tmp = Client::self()->setShow(p->name(),p->toBoolean(),wnd);
	else
	    tmp = Client::self()->setVisible(p->name(),p->toBoolean());
	if (tmp)
	    params.clearParam(p->name());
	else
	    result = false;
    }
    return result;
}

// Called when the user pressed the backspace key.
// Erase the last digit from the given item and set focus on it
bool ClientLogic::backspace(const String& name, Window* wnd)
{
    if (!Client::self())
	return false;

    String str;
    if (Client::self()->getText(name,str,false,wnd) &&
	(!str || Client::self()->setText(name,str.substr(0,str.length()-1),false,wnd)))
	Client::self()->setFocus(name,false,wnd);
    return true;
}

// Called when the user pressed a command action
bool ClientLogic::command(const String& name, Window* wnd)
{
    Message* m = new Message("engine.command");
    m->addParam("line",name);
    Engine::enqueue(m);
    return true;
}

// Called when the user changes debug options
bool ClientLogic::debug(const String& name, bool active, Window* wnd)
{
    // pos: module name
    int pos = name.find(':');
    if (pos <= 0)
	return false;
    // posLine: active/inactive command line
    int posLine = name.find(':',pos + 1);
    if (posLine < 0 || posLine - pos < 2)
	return false;
    // Get module/line and enqueue the message
    String module = name.substr(0,pos);
    String line = (active ? name.substr(pos + 1,posLine - pos - 1) : name.substr(posLine + 1));
    Message* m = new Message("engine.debug");
    m->addParam("module",module);
    m->addParam("line",line);
    Engine::enqueue(m);
    return true;
}


/**
 * DefaultLogic
 */
// constructor
DefaultLogic::DefaultLogic(const char* name, int prio)
    : ClientLogic(name,prio), m_accShowAdvanced(false)
{
}

// Declare a NamedList and set a parameter
// Used to avoid use of null pointers
#define USE_SAFE_PARAMS(param,value) NamedList dummy(""); if (!params) params = &dummy; \
    params->setParam(param,value);

// main function which considering de value of the "action" parameter
// Handle actions from user interface
bool DefaultLogic::action(Window* wnd, const String& name, NamedList* params)
{
    // Translate actions from confirmation boxes
    // the window context specifies what action will be taken forward
    if (wnd && wnd->context() && (name == "ok") && (wnd->context() != "ok")) {
	bool ok = action(wnd,wnd->context(),params);
	if (ok)
	    wnd->hide();
	return ok;
    }

    // Show/hide widgets/windows
    bool widget = (name == "display");
    if (widget || name == "show") {
	USE_SAFE_PARAMS("","");
	return display(*params,widget,wnd);
    }

    // Start a call
    if (name == s_actionCall || name == "callto") {
	USE_SAFE_PARAMS("","");
	return callStart(*params,wnd);
    }
    // Start a call from an action specifying the target
    if (name.startsWith("callto:")) {
	USE_SAFE_PARAMS("target",name.substr(7));
	return callStart(*params,wnd);
    }
    // Answer/Hangup
    bool anm = (name == s_actionAnswer);
    if (anm || name == s_actionHangup) {
	if (!m_selectedChannel)
	    return false;
	if (anm)
	    Client::self()->callAnswer(m_selectedChannel);
	else
	    Client::self()->callTerminate(m_selectedChannel);
	return true;
    }
    anm = name.startsWith("answer:");
    if ((anm || name.startsWith("hangup:")) && name.at(7)) {
	if (anm)
	    Client::self()->callAnswer(name.substr(7));
	else
	    Client::self()->callTerminate(name.substr(7));
	return true;
    }
    // Double click on channel: set the active call
    if (name == s_channelList)
	return m_selectedChannel && ClientDriver::self() &&
	    ClientDriver::self()->setActive(m_selectedChannel);
    // Digit(s) pressed
    if (name.startsWith("digit:")) {
	USE_SAFE_PARAMS("digits",name.substr(6));
	return digitPressed(*params,wnd);
    }
    // New line
    if (name.startsWith("line:") && line(name.substr(5),wnd))
	return false;
    // Action taken when receiving a clear action
    if (name.startsWith("clear:") && name.at(6)) {
	String w = name.substr(6);
	// Special care for call logs (call direction is opposite for CDR)
	if (w == s_logOutgoing)
	    return callLogClear(w,"incoming");
	if (w == s_logIncoming)
	    return callLogClear(w,"outgoing");
	if (Client::self() && (Client::self()->clearTable(w,wnd) ||
	    Client::self()->setText(w,"",false,wnd))) {
	    Client::self()->setFocus(w,false,wnd);
	    return true;
	}
    }
    // action taken when receiving a backspace
    if (name.startsWith("back:"))
	return backspace(name.substr(5),wnd);
    if (name.startsWith("command:") && name.at(8))
	return command(name.substr(8),wnd);

    // *** Account management

    // Create a new account or edit an existing one
    bool newAcc = (name == "acc_new");
    if (newAcc || name == "acc_edit" || name == s_accountList)
	return editAccount(newAcc,params,wnd);
    // User pressed ok button in account edit window
    if (name == "acc_accept")
	return acceptAccount(params,wnd);
    // Delete an account
    if (name.startsWith("acc_del")) {
	// Empty: delete the current selection
	if (!name.at(7))
	    return delAccount(String::empty(),wnd);
	// Handle 'acc_del:'
	if (name.length() > 9 && name.at(7) == ':' && name.at(8))
	    return delAccount(name.substr(8),wnd);
    }
    // Login/logout
    bool login = (name == "acc_login");
    if (login || name == "acc_logout") {
	String account;
	if (!(Client::self() && Client::self()->getSelect(s_accountList,account,wnd)))
	    return false;
	NamedList* sect = Client::s_accounts.getSection(account);
	if (!sect)
	    return false;
	return loginAccount(*sect,login);
    }

    // *** Address book actions

    // Call the current contact selection
    if (name == "abk_call" || name == s_contactList)
	return callContact(params,wnd);
    // Add/edit contact
    bool newCont = (name == "abk_new");
    if (newCont || name == "abk_edit")
	return editContact(newCont,params,wnd);
    // Delete a contact
    if (name.startsWith("abk_del")) {
	// Empty: delete the current selection
	if (!name.at(7))
	    return delContact(String::empty(),wnd);
	// Handle 'abk_del:'
	if (name.length() > 9 && name.at(7) == ':' && name.at(8))
	    return delContact(name.substr(8),wnd);
    }
    // User pressed "ok" in a pop-up window like the one
    // for adding/editing a contact
    if (name == "abk_accept")
	return acceptContact(params,wnd);

    // *** Call log management

    // Call a log entry
    bool callOut = (name == "log_out_call");
    if (callOut || name == "log_in_call") {
	String billid;
	if (Client::self() && 
	    Client::self()->getSelect(callOut ? s_logOutgoing : s_logIncoming,billid) &&
	    billid)
	    return callLogCall(billid);
	return false;
    }
    // Create a contact from a call log entry
    callOut = (name == "log_out_contact");
    if (callOut || name == "log_in_contact") {
	String billid;
	if (Client::self() && 
	    Client::self()->getSelect(callOut ? s_logOutgoing : s_logIncoming,billid) &&
	    billid)
	    return callLogCreateContact(billid);
	return false;
    }

    // *** Miscellanous

    // Handle show window actions
    if (name.startsWith("action_show_"))
	Client::self()->setVisible(name.substr(12),true);
    // Help commands
    if (name.startsWith("help:"))
	return help(name,wnd);
    // Hide windows
    if (name == "button_hide" && wnd)
	return Client::self() && Client::self()->setVisible(wnd->toString(),false);

    return false;
}

// Handle actions from checkable widgets
bool DefaultLogic::toggle(Window* wnd, const String& name, bool active)
{
    Debug(ClientDriver::self(),DebugAll,
	"Logic(%s) toggle '%s'=%s in window (%p,%s)",
	toString().c_str(),name.c_str(),String::boolText(active),
	wnd,wnd ? wnd->id().c_str() : "");

    // Check for window params
    if (Client::self() && Window::isValidParamPrefix(name)) {
	NamedList p("");
	p.addParam(name,String::boolText(active));
	return Client::self()->setParams(&p,wnd);
    }
    if (name.startsWith("setparams:") && name.at(10) && Client::self()) {
	String tmp = name.substr(10);
	ObjList* obj = tmp.split(';',false);
	NamedList p("");
	for (ObjList* o = obj->skipNull(); o; o = o->skipNext()) {
	    String* s = static_cast<String*>(o->get());
	    const char* param = s->c_str();
	    bool value = active;
	    if (s->at(0) == '!') {
		param++;
		value = !active;
	    }
	    if (*param)
		p.addParam(param,String::boolText(active));
	}
	TelEngine::destruct(obj);
	return Client::self()->setParams(&p);
    }

    // *** Channel actions
    // Hold
    if (name == s_actionHold) {
	if (!ClientDriver::self())
	    return false;
	bool ok = active ? ClientDriver::self()->setActive() :
		m_selectedChannel && ClientDriver::self()->setActive(m_selectedChannel);
	if (!ok)
	    enableCallActions(m_selectedChannel);
	return ok;
    }
    // Transfer
    if (name == s_actionTransfer) {
	// Active: set init flag and wait to select the target
	// Else: reset transfer on currently selected channel
	if (active)
	    m_transferInitiated = m_selectedChannel;
	else if (m_selectedChannel)
	    ClientDriver::setAudioTransfer(m_selectedChannel);
	return true;
    }
    // Conference
    if (name == s_actionConf) {
	bool ok = ClientDriver::setConference(m_selectedChannel,active);
	if (!ok)
	    enableCallActions(m_selectedChannel);
	return ok;
    }

    // Show/hide windows
    if (name.startsWith("showwindow:") && name.at(11)) {
	String what = name.substr(11);
	if (what.startsWith("help:")) {
	    if (active)
		return help(what,wnd);
	    else
		return Client::self() && Client::self()->setVisible("help",false);
	}
	NamedList p("");
	p.addParam(what,String::boolText(active));
	return display(p,false);
    }

    // Visibility: update checkable widgets having the same name as the window
    if (wnd && name == "window_visible_changed") {
	if (!Client::self())
	    return false;
	NamedList p("");
	p.addParam("check:toggle_show_" + wnd->toString(),String::boolText(active));
	p.addParam("check:action_show_" + wnd->toString(),String::boolText(active));
	Client::self()->setParams(&p);
	return true;
    }

    // Select item if active
    // Return true if inactive
    if (name.startsWith("selectitem:")) {
	if (!active)
	    return true;
	String tmp = name.substr(11);
	if (!tmp)
	    return true;
	int pos = tmp.find(':');
	if (pos > 0 && Client::self())
	    return Client::self()->setSelect(tmp.substr(0,pos),tmp.substr(pos + 1),wnd);
	return true;
    }

    // Set debug to window
    if (name == "log_events_debug") {
	bool ok = Client::self() && Client::self()->debugHook(active);
	if (ok && !active) {
	    NamedList p("");
	    p.addParam("check:debug_sniffer",String::boolText(false));
	    p.addParam("check:debug_jingle",String::boolText(false));
	    p.addParam("check:debug_sip",String::boolText(false));
	    p.addParam("check:debug_h323",String::boolText(false));
	    p.addParam("check:debug_iax",String::boolText(false));
	    Client::self()->setParams(&p,wnd);
	}
	return ok;
    }
    // Enable the showing of debug information for a certain module or disable it
    if (name.startsWith("debug:")) {
	if (debug(name.substr(6),active,wnd))
	    return true;
    }

    // Save client settings
    Client::ClientToggle clientOpt = Client::getBoolOpt(name);
    if (clientOpt != Client::OptCount) {
	setClientParam(name,String::boolText(active),true,false);
	return true;
    }

    // Advanced button from account window
    if (name == "toggleAccAdvanced") {
	m_accShowAdvanced = active;
	// Save it
	Client::s_settings.setValue("client","showaccadvanced",String::boolText(m_accShowAdvanced));
	Client::save(Client::s_settings,wnd);
	// Select the page
	// Set advanced for the current protocol
	String proto;
	if (!m_accShowAdvanced)
	    proto = "none";
	else if (Client::self())
	    Client::self()->getSelect("acc_protocol",proto);
	if (proto)
	    toggle(wnd,"selectitem:acc_proto_spec:acc_proto_spec_" + proto,true);
	return true;
    }

    // Commands
    if (name.startsWith("command:") && name.at(8))
	return command(name.substr(8) + (active ? " on" : " off"),wnd);

    // Handle show window actions
    if (name.startsWith("action_show_"))
	Client::self()->setVisible(name.substr(12),active);

    return false;
}

// Handle 'select' actions from user interface
bool DefaultLogic::select(Window* wnd, const String& name, const String& item,
	const String& text)
{
    DDebug(ClientDriver::self(),DebugAll,
	"Logic(%s) select name='%s' item='%s' in window (%p,%s)",
	toString().c_str(),name.c_str(),item.c_str(),wnd,wnd ? wnd->id().c_str() : "");

    if (name == s_accountList) {
	if (!Client::self())
	    return false;
	const char* active = String::boolText(!item.null());
	NamedList p("");
	p.addParam("active:acc_login",active);
	p.addParam("active:acc_logout",active);
	p.addParam("active:acc_del",active);
	p.addParam("active:acc_edit",active);
	Client::self()->setParams(&p,wnd);
	return true;
    }

    if (name == s_contactList) {
	if (!Client::self())
	    return false;
	const char* active = String::boolText(!item.null());
	NamedList p("");
	p.addParam("active:abk_call",active);
	p.addParam("active:abk_del",active);
	p.addParam("active:abk_edit",active);
	Client::self()->setParams(&p,wnd);
	return true;
    }

    // Log out/in calls
    bool out = (name == s_logOutgoing);
    if (out || name == s_logIncoming) {
	if (!Client::self())
	    return false;
	const char* active = String::boolText(!item.null());
	NamedList p("");
	if (out) {
	    p.addParam("active:log_out_call",active);
	    p.addParam("active:log_out_contact",active);
	}
	else {
	    p.addParam("active:log_in_call",active);
	    p.addParam("active:log_in_contact",active);
	}
	Client::self()->setParams(&p,wnd);
	return true;
    }

    // keep the item in sync in all windows
    // if the same object is present in more windows, we will synchronise all of them
    if (Client::self())
	Client::self()->setSelect(name,item,0,wnd);

    // Enable specific actions when a channel is selected
    if (name == s_channelList) {
	updateSelectedChannel(&item);
	return true;
    }
    // when an account is selected, the choice of protocol must be cleared
    // when a protocol is chosen, the choice of account must be cleared
    bool acc = (name == "account");
    if (acc || name == "protocol") {
	if (Client::s_notSelected.matches(item))
	    return true;
	if (acc)
	    return Client::self()->setSelect("protocol",s_notSelected,wnd);
	return Client::self()->setSelect("account",s_notSelected,wnd);
    }
    // Handle protocol selection in edit window: activate advanced options
    if (name == "acc_protocol") {
	static String proto = "acc_proto_spec";
	String what = proto + (m_accShowAdvanced ? "_" + item : "_none");
	return Client::self() && Client::self()->setSelect(proto,what,wnd);
    }

    // Apply provider template
    if (name == "acc_providers") {
	if (item.matches(Client::s_notSelected))
	    return true;
	if (!Client::self())
	    return false;
	// Reset selection
	Client::self()->setSelect(name,s_notSelected,wnd);
	// Get data and update UI
	NamedList* sect = Client::s_providers.getSection(item);
	if (!sect)
	    return false;
	NamedList p("");
	for (const char** par = s_provParams; *par; par++)
	    p.addParam(String("acc_") + *par,sect->getValue(*par));
	NamedString* proto = sect->getParam("protocol");
	if (proto) {
	    selectProtocolSpec(p,*proto,m_accShowAdvanced);
	    NamedString* opt = sect->getParam("options");
	    updateProtocolSpec(p,*proto,opt ? *opt : String::empty());
	}
	Client::self()->setParams(&p,wnd);
	return true;
    }

    // Selection changed in 'callto': do nothing. Just return true to avoid enqueueing ui.event
    if (name == "callto")
	return true;

    return false;
}

// Set a client's parameter. Save the settings file and/or update interface
bool DefaultLogic::setClientParam(const String& param, const String& value,
	bool save, bool update)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) setClientParam(%s,%s,%s,%s)",
	toString().c_str(),param.c_str(),value.c_str(),
	String::boolText(save),String::boolText(update));

    update = update && (0 != Client::self());
    const char* section = 0;
    bool changed = false;

    // Bool params
    Client::ClientToggle opt = Client::getBoolOpt(param);
    if (opt != Client::OptCount) {
	if (value.isBoolean()) { 
	    section = "general";
	    if (Client::self()) {
		bool ok = value.toBoolean();
		changed = Client::self()->setBoolOpt(opt,ok,update);
		// Special care for some controls
		if (opt == Client::OptKeypadVisible)
		    Client::self()->setShow("keypad",ok);
	    }
	}
    }
    else if (param == "username" || param == "callerid" || param == "domain") {
	section = "default";
	changed = true;
	if (update)
	    Client::self()->setText("def_" + param,value);
    }

    if (!section)
	return false;
    if (!changed)
	return true;

    // Update/save settings
    Client::s_settings.setValue(section,param,value);
    if (save)
	Client::save(Client::s_settings);
    return true;
}

// Start an outgoing call
bool DefaultLogic::callStart(NamedList& params, Window* wnd)
{
    if (!(Client::self() && fillCallStart(params,wnd)))
	return false;
    // Delete the number from the "callto" widget and put it in the callto history
    NamedString* ns = params.getParam("target");
    if (ns) {
	Client::self()->delOption(s_calltoList,*ns);
	Client::self()->addOption(s_calltoList,*ns,true);
	Client::self()->setText(s_calltoList,"");
    }
    if (!Client::self()->buildOutgoingChannel(params))
	return false;
    // Activate the calls page
    activatePageCalls(this);
    return true;
}

// function which is called when a digit is pressed
bool DefaultLogic::digitPressed(NamedList& params, Window* wnd)
{
    if (!Client::self())
	return false;

    // Send digits (DTMF) on active channel
    // or add them to 'callto' box
    String digits = params.getValue("digits");
    if (digits && ClientDriver::self() && ClientDriver::self()->activeId()) {
	Client::self()->emitDigits(digits,ClientDriver::self()->activeId());
	return true;
    }
    String target;
    if (isE164(digits) && Client::self()->getText("callto",target)) {
	target += digits;
	if (Client::self()->setText("callto",target)) {
	    Client::self()->setFocus("callto",false);
	    return true;
	}
    }
    return false;
}

// Called when the user wants to add ane account or edit an existing one
bool DefaultLogic::editAccount(bool newAcc, NamedList* params, Window* wnd)
{
    // Make sure we reset all controls in window
    USE_SAFE_PARAMS("select:acc_providers",s_notSelected);
    params->setParam("check:acc_loginnow","true");

    bool enable = true;
    String acc;
    String proto;
    if (newAcc) {
	proto = "--";
	for (const String* par = s_accParams; !par->null(); par++)
	    params->setParam("acc_" + *par,"");
    }
    else {
	if (!Client::self())
	    return false;
	if (!Client::self()->getSelect(s_accountList,acc))
	    return false;
	NamedList* sect = Client::s_accounts.getSection(acc);
	if (sect) {
	    enable = sect->getBoolValue("enabled",true);
	    proto = sect->getValue("protocol");
	    for (const String* par = s_accParams; !par->null(); par++)
		params->setParam("acc_" + *par,sect->getValue(*par));
	}
    }

    // Enable account
    params->setParam("check:acc_enabled",String::boolText(enable));
    // Protocol combo and specific widget (page) data
    selectProtocolSpec(*params,proto,m_accShowAdvanced);
    NamedString* tmp = params->getParam("acc_options");
    s_protocolsMutex.lock();
    for (ObjList* o = s_protocols.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (*s)
	    updateProtocolSpec(*params,*s,tmp ? *tmp : String::empty());
    }
    s_protocolsMutex.unlock();
    params->setParam("context",acc);
    params->setParam("acc_account",acc);
    params->setParam("modal",String::boolText(true));
    return Client::openPopup("account",params,wnd);
}

// Utility function used to save a widget's text
static inline void saveAccParam(NamedList& params,
	const String& prefix, const String& param, Window* wnd)
{
    String val;
    if (!Client::self()->getText(prefix + param,val,false,wnd))
	return;
    if (val)
	params.setParam(param,val);
    else
	params.clearParam(param);
}

// Called when the user wants to save account data
bool DefaultLogic::acceptAccount(NamedList* params, Window* wnd)
{
    if (!Client::self())
	return false;
    
    // Check required data
    String account;
    String proto;
    const char* err = 0;
    while (true) {
#define SET_ERR_BREAK(e) { err = e; break; }
	Client::self()->getText("acc_account",account,false,wnd);
	if (!account)
	    SET_ERR_BREAK("Account name field can't be empty");
	Client::self()->getText("acc_protocol",proto,false,wnd);
	if (!proto)
	    SET_ERR_BREAK("A protocol must be selected");
	break;
#undef SET_ERR_BREAK
    }
    if (err) {
	if (!Client::openMessage(err,wnd))
	    Debug(ClientDriver::self(),DebugNote,"Logic(%s). %s",toString().c_str(),err);
	return false;
    }

    // Get account from file or create a new one if not found
    NamedList* accSect = Client::s_accounts.getSection(account);
    bool newAcc = (accSect == 0);
    if (newAcc) {
	Client::s_accounts.createSection(account);
	accSect = Client::s_accounts.getSection(account);
    }

    // Account flags
    bool enable = true;
    if (!Client::self()->getCheck("acc_enabled",enable,wnd))
	enable = true;
    bool login = false;
    Client::self()->getCheck("acc_loginnow",login,wnd);

    accSect->setParam("enabled",String::boolText(enable));
    accSect->setParam("protocol",proto);
    String prefix = "acc_";
    // Save account parrameters
    for (const String* par = s_accParams; !par->null(); par++)
	saveAccParam(*accSect,prefix,*par,wnd);
    // Special care for protocol specific data
    prefix << "proto_" << proto << "_";
    // Texts
    saveAccParam(*accSect,prefix,"resource",wnd);
    saveAccParam(*accSect,prefix,"port",wnd);
    saveAccParam(*accSect,prefix,"address",wnd);
    // Options
    prefix << "opt_";
    String options;
    for (ObjList* o = s_accOptions.skipNull(); o; o = o->skipNext()) {
	String* opt = static_cast<String*>(o->get());
	bool checked = false;
	Client::self()->getCheck(prefix + *opt,checked,wnd);
	if (checked)
	    options.append(*opt,",");
    }
    if (options)
	accSect->setParam("options",options);
    else
	accSect->clearParam("options");

    if (wnd)
	Client::self()->setVisible(wnd->toString(),false);
    return updateAccount(*accSect,login,true);
}

// Called when the user wants to delete an existing account
bool DefaultLogic::delAccount(const String& account, Window* wnd)
{
    if (!account) {
	String acc;
	if (Client::self() && Client::self()->getSelect(s_accountList,acc) && acc)
	    return Client::openConfirm("Delete account " + acc + "?",wnd,"acc_del:" + acc);
	return false;
    }

    // Delete the account from config and all UI controls
    Client::self()->delTableRow(s_accountList,account);
    Client::self()->delOption("account",account);
    Client::s_accounts.clearSection(account);
    Client::save(Client::s_accounts);
    // Disconnect
    Message* m = new Message("user.login");
    m->addParam("account",account);
    m->addParam("operation","delete");
    Engine::enqueue(m);
    return true;
}

// Add/set an account to UI. Save accounts file and login if required
bool DefaultLogic::updateAccount(const NamedList& account, bool login, bool save)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) updateAccount(%s,%s,%s)",
	toString().c_str(),account.c_str(),String::boolText(login),String::boolText(save));

    if (!Client::self() || account.null())
	return false;
    if (save && !Client::save(Client::s_accounts))
	return false;

    NamedList uiParams(account);
    uiParams.addParam("account",account);
    // Make sure some parameters are present to avoid incorrect UI data on update
    if (!uiParams.getParam("protocol"))
	uiParams.addParam("protocol",""); 
    // Update UI (lists and tables)
    if (!Client::self()->hasOption("account",account))
	Client::self()->addOption("account",account,false);
    Client::self()->updateTableRow(s_accountList,account,&uiParams);
    // Login if required
    if (login)
	return loginAccount(account,true);
    return true;
}

// Login/logout an account
bool DefaultLogic::loginAccount(const NamedList& account, bool login)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) loginAccount(%s,%s)",
	toString().c_str(),account.c_str(),String::boolText(login));

    Message* m = new Message("user.login");
    m->addParam("account",account);
    m->addParam("operation",login ? "login" : "delete");
    // Fill login data
    if (login) {
    	unsigned int n = account.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* p = account.getParam(i);
	    if (p)
		m->addParam(p->name(),*p);
	}
    }
    else
	m->copyParams(account,"protocol");
    return Engine::enqueue(m);
}

// Add/update a contact
bool DefaultLogic::updateContact(const NamedList& params, bool save, bool update)
{
    if (!(Client::self() && (save || update)))
	return false;

    NamedString* target = params.getParam("target");
    if (!target)
	return false;

    // Update UI
    if (update) {
	NamedList tmp(params);
	tmp.setParam("number/uri",*target);
	Client::self()->updateTableRow(s_contactList,*target,&tmp);
    }
    // Save file
    bool ok = true;
    if (save) {
	unsigned int n = params.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params.getParam(i);
	    if (!ns)
		continue;
	    if (*ns)
		Client::s_contacts.setValue(*target,ns->name(),*ns);
	    else
		Client::s_contacts.clearKey(*target,ns->name());
	}
	ok = Client::save(Client::s_contacts);
    }
    // Notify server if this is a client account (stored on server)
    // TODO: implement
    return true;
}

// Called when the user wants to save account data
bool DefaultLogic::acceptContact(NamedList* params, Window* wnd)
{
    if (!Client::self())
	return false;
    
    NamedList p("");
    const char* err = 0;
    String target;
    // Check required data
    while (true) {
#define SET_ERR_BREAK(e) { err = e; break; }
	Client::self()->getText("abk_name",p,false,wnd);
	if (p.null())
	    SET_ERR_BREAK("A contact name must be specified");
	Client::self()->getText("abk_target",target,false,wnd);
	if (target)
	    p.addParam("target",target);
	else
	    SET_ERR_BREAK("Contact number/target field can't be empty");
	break;
#undef SET_ERR_BREAK
    }
    if (!err) {
	// Check if the given contact can be changed
	NamedList pp("");
	if (Client::self()->getTableRow(s_contactList,target,&pp) &&
	    !checkContactEdit(pp,wnd))
	    return false;
	p.addParam("name",p);
	if (wnd)
	    Client::self()->setVisible(wnd->toString(),false);
        return updateContact(p,true,true);
    }
    Client::openMessage(err,wnd);
    return false;
}

// Called when the user wants to add a new contact or edit an existing one
bool DefaultLogic::editContact(bool newCont, NamedList* params, Window* wnd)
{
    // Make sure we reset all controls in window
    NamedList p("");
    if (newCont) {
	p.addParam("abk_name",params ? params->c_str() : "");
	p.addParam("abk_target",params ? params->getValue("target") : "");
    }
    else {
	if (!Client::self())
	    return false;
	String id;
	if (!Client::self()->getSelect(s_contactList,id))
	    return false;
	if (!Client::self()->getTableRow(s_contactList,id,&p))
	    return false;
	if (!checkContactEdit(p,wnd))
	    return false;
	p.addParam("abk_name",p.getValue("name"));
	p.addParam("abk_target",id);
    }
    p.addParam("modal",String::boolText(true));
    return Client::openPopup("addrbook",&p,wnd);
}

// Called when the user wants to delete an existing contact
bool DefaultLogic::delContact(const String& contact, Window* wnd)
{
    if (!contact) {
	String c;
	if (Client::self() && Client::self()->getSelect(s_contactList,c) && c) {
	    NamedList p("");
	    Client::self()->getTableRow(s_contactList,c,&p);
	    if (!checkContactEdit(p,wnd))
		return false;
	    return Client::openConfirm(String("Delete contact ") +
		p.getValue("name") + "?",wnd,"abk_del:" + c);
	}
	return false;
    }

    // Delete the account from config and all UI controls
    Client::self()->delTableRow(s_contactList,contact);
    Client::self()->delOption("contact",contact);
    Client::s_contacts.clearSection(contact);
    Client::save(Client::s_contacts);
    // Notify server if this is a client account (stored on server)
    // TODO: implement
    return true;
}

// Add/set account providers data
bool DefaultLogic::updateProviders(const NamedList& provider, bool save, bool update)
{
    if (!(save || update))
	return false;
    if (provider.null() || !provider.getBoolValue("enabled",true))
	return false;

    if (save && !Client::save(Client::s_providers))
	return false;
    String ctrl = "acc_providers";
    if (Client::self() && !Client::self()->hasOption(ctrl,provider))
	Client::self()->addOption(ctrl,provider,false);
    return true;
}

// Called when the user wants to call an existing contact
bool DefaultLogic::callContact(NamedList* params, Window* wnd)
{
    if (!Client::self())
	return false;
    NamedList dummy("");
    if (!params) {
	params = &dummy;
	Client::self()->Client::getSelect(s_contactList,*params);
    }

    if (!Client::self()->getTableRow(s_contactList,*params,params))
	return false;

    // Check if the target is a complete one
    Regexp r("^[a-z0-9]\\+/");
    bool complete = r.matches(params->c_str());
    NamedString* tmp = params->getParam("hidden:account");
    // Set account
    // Check for registered account(s) if none and the target is not a complete one
    if (tmp) {
	params->setParam("line",*tmp);
	params->setParam("account",*tmp);
    }
    else if (!complete) {
	// 1 account: call from it without checking it's status
	// n with 1 registered: call from it
	// ELSE: fill callto and activate the calls page
	NamedList accounts("");
	Client::self()->Client::getOptions(s_accountList,&accounts);
	unsigned int n = accounts.length();
	String account;
	if (n == 1) {
	    NamedString* s = accounts.getParam(0);
	    if (s)
		account = s->name();
	}
	else
	    for (unsigned int i = 0; i < n; i++) {
		NamedString* acc = accounts.getParam(i);
		if (!acc)
		    continue;
		NamedList p("");
		Client::self()->getTableRow(s_accountList,acc->name(),&p);
		NamedString* reg = p.getParam("status");
		if (!(reg && *reg == "Registered"))
		    continue;
		// A second account registered
		if (account) {
		    account = "";
		    break;
		}
		account = acc->name();
	    }
	if (account) {
	    params->setParam("line",account);
	    params->setParam("account",account);
	}
	else {
	    Client::self()->setText(s_calltoList,*params);
	    activatePageCalls(this);
	    return true;
	}
    }
    tmp = params->getParam("hidden:protocol");
    if (tmp)
	params->setParam("protocol",*tmp);
    params->setParam("target",*params);

    return callStart(*params);
}

// Update the call log history
bool DefaultLogic::callLogUpdate(NamedList& params, bool save, bool update)
{
    if (!(save || update))
	return false;

    NamedString* id = params.getParam("billid");
    if (!id)
	id = params.getParam("id");
    if (!id)
	return false;

    while (Client::self() && update) {
	// determine if the call was incoming or outgoing
	String* dir = params.getParam("direction");
	if (!dir)
	    break;
	// Remember: directions are opposite of what the user expects
	// choose the table accordingly
	if (*dir == "outgoing")
	    Client::self()->addTableRow(s_logIncoming,*id,&params);
	else if (*dir == "incoming")
	    Client::self()->addTableRow(s_logOutgoing,*id,&params);
	else
	    break;
	Client::self()->addTableRow("log_global",*id,&params);
	break;
    }

    if (!save)
	return true;

    // Update the call history file
    // We don't hold information for more than s_maxCallHistory, so if we reached the
    // limit, we delete the oldest entry to make room
    while (Client::s_history.sections() >= s_maxCallHistory) {
	NamedList* sect = Client::s_history.getSection(0);
	if (!sect)
	    break;
	Client::s_history.clearSection(*sect);
    }
    // write to the file the information about the calls
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = params.getParam(i);
	if (!param)
	    continue;
	Client::s_history.setValue(*id,param->name(),param->c_str());
    }
    return Client::save(Client::s_history);
}

// Clear the specified log and the entries from the history file and save the history file
bool DefaultLogic::callLogClear(const String& table, const String& direction)
{
    // Clear history
    bool save = false;
    unsigned int n = Client::s_history.sections();
    if (direction)
	for (unsigned int i = 0; i < n; i++) {
	    NamedList* sect = Client::s_history.getSection(i);
	    NamedString* dir = sect ? sect->getParam("direction") : 0;
	    if (!dir || *dir != direction)
		continue;
	    Client::s_history.clearSection(*sect);
	    save = true;
	    i--;
	}
    else {
	save = (0 != n);
	Client::s_history.clearSection();
    }

    // Clear table and save the file
    if (Client::self())
	Client::self()->clearTable(table);
    if (save)
	Client::save(Client::s_history);
    return true;
}

// Utility function to get the called from list of parameters built from call.cdr message
// Remember: directions are opposite of what the user expects
inline NamedString* getCdrCalled(NamedList& list, const String& direction)
{
    if (direction == "incoming")
	return list.getParam("called");
    if (direction == "outgoing")
	return list.getParam("caller");
    return 0;
}

// Make an outgoing call to a target picked from the call log
bool DefaultLogic::callLogCall(const String& billid)
{
    NamedList* sect = Client::s_history.getSection(billid);
    if (!sect)
	return false;
    NamedString* direction = sect->getParam("direction");
    if (!direction)
	return false;
    NamedString* called = getCdrCalled(*sect,*direction);
    return called && *called && Client::openConfirm("Call to " + *called,0,"callto:" + *called);
}

// Create a contact from a call log entry
bool DefaultLogic::callLogCreateContact(const String& billid)
{
    NamedList* sect = Client::s_history.getSection(billid);
    if (!sect)
	return false;
    NamedString* direction = sect->getParam("direction");
    if (!direction)
	return false;
    NamedString* called = getCdrCalled(*sect,*direction);
    NamedList p("");
    p.setParam("abk_name",called ? called->c_str() : "");
    p.setParam("abk_target",called ? called->c_str() : "");
    p.setParam("modal",String::boolText(true));
    return Client::openPopup("addrbook",&p);
}

// Process help related actions
bool DefaultLogic::help(const String& name, Window* wnd)
{
    if (!Client::self())
	return false;

    Window* help = Client::self()->getWindow("help");
    if (!help)
	return false;

    // Set the the searched page
    bool show = false;
    int page = help->context().toInteger();
    if (name == "help:home")
	page = 0;
    else if (name == "help:prev")
	page--;
    else if (name == "help:next")
	page++;
    else if (name.startsWith("help:")) {
	page = name.substr(5).toInteger(page);
	show = true;
    }
    if (page < 0)
	page = 0;

    // Get the help file from the help folder
    String helpFile = Engine::config().getValue("client","helpbase");
    if (!helpFile)
	helpFile << Engine::sharedPath() << Engine::pathSeparator() << "help";
    if (!helpFile.endsWith(Engine::pathSeparator()))
	helpFile << Engine::pathSeparator();
    helpFile << page << ".yhlp";

    File f;
    if (!f.openPath(helpFile)) {
	Debug(ClientDriver::self(),DebugNote,"Failed to open help file '%s'",helpFile.c_str());
	return false;
    }
    // if the opening of the help file succeeds, we set it as the text of the help window
    int rd = 0;
    unsigned int len = (unsigned int)f.length();
    if (len != (unsigned int)-1) {
	String helpText(' ',len);
	rd = f.readData(const_cast<char*>(helpText.c_str()),len);
	if (rd == (int)len) {
	    Client::self()->setText("help_text",helpText,true,help);
	    help->context(String(page));
	    if (show)
		Client::self()->setVisible("help",true);
	    return true;
	}
    }
    Debug(ClientDriver::self(),DebugNote,"Read only %d out of %u bytes in file '%s'",
	rd,len,helpFile.c_str());
    return false;
}

// Called by the client after loaded the callto history file
bool DefaultLogic::calltoLoaded()
{
    if (!Client::self())
	return false;
    NamedList* sect = Client::s_calltoHistory.getSection("calls");
    if (!sect)
	return false;
    unsigned int n = sect->length();
    unsigned int max = 0;
    for (unsigned int i = 0; max < s_maxCallHistory && i < n; i++) {
	NamedString* s = sect->getParam(i);
	if (!s || Client::self()->hasOption(s_calltoList,s->name()))
	    continue;
	if (Client::self()->addOption(s_calltoList,s->name(),false))
	    max++;
    }
    Client::self()->setText(s_calltoList,"");
    return false;
}

// Process ui.action message
bool DefaultLogic::handleUiAction(Message& msg, bool& stopLogic)
{
    if (!Client::self())
	return false;
    // get action
    NamedString* action = msg.getParam("action");
    if (!action)
	return false;

    // block until client finishes initialization
    while (!Client::self()->initialized())
	Thread::msleep(10);
    // call the appropiate function for the given action
    Window* wnd = Client::getWindow(msg.getValue("window"));
    if (*action == "set_status")
	return Client::self()->setStatusLocked(msg.getValue("status"),wnd);
    else if (*action == "add_log")
	return Client::self()->addToLog(msg.getValue("text"));
    else if (*action == "show_message") {
	Client::self()->lockOther();
	bool ok = Client::openMessage(msg.getValue("text"),Client::getWindow(msg.getValue("parent")),msg.getValue("context"));
	Client::self()->unlockOther();
	return ok;
    }
    else if (*action == "show_confirm") {
	Client::self()->lockOther();
	bool ok = Client::openConfirm(msg.getValue("text"),Client::getWindow(msg.getValue("parent")),msg.getValue("context"));
	Client::self()->unlockOther();
	return ok;
    }
    // get the name of the widget for which the action is meant
    String name(msg.getValue("name"));
    if (name.null())
	return false;
    DDebug(ClientDriver::self(),DebugAll,"UI action '%s' on '%s' in %p",
	action->c_str(),name.c_str(),wnd);
    bool ok = false;
    Client::self()->lockOther();
    if (*action == "set_text")
	ok = Client::self()->setText(name,msg.getValue("text"),false,wnd);
    else if (*action == "set_toggle")
	ok = Client::self()->setCheck(name,msg.getBoolValue("active"),wnd);
    else if (*action == "set_select")
	ok = Client::self()->setSelect(name,msg.getValue("item"),wnd);
    else if (*action == "set_active")
	ok = Client::self()->setActive(name,msg.getBoolValue("active"),wnd);
    else if (*action == "set_focus")
	ok = Client::self()->setFocus(name,msg.getBoolValue("select"),wnd);
    else if (*action == "set_visible")
	ok = Client::self()->setShow(name,msg.getBoolValue("visible"),wnd);
    else if (*action == "has_option")
	ok = Client::self()->hasOption(name,msg.getValue("item"),wnd);
    else if (*action == "add_option")
	ok = Client::self()->addOption(name,msg.getValue("item"),msg.getBoolValue("insert"),msg.getValue("text"),wnd);
    else if (*action == "del_option")
	ok = Client::self()->delOption(name,msg.getValue("item"),wnd);
    else if (*action == "get_text") {
	String text;
	ok = Client::self()->getText(name,text,false,wnd);
	if (ok)
	    msg.retValue() = text;
    }
    else if (*action == "get_toggle") {
	bool check;
	ok = Client::self()->getCheck(name,check,wnd);
	if (ok)
	    msg.retValue() = check;
    }
    else if (*action == "get_select") {
	String item;
	ok = Client::self()->getSelect(name,item,wnd);
	if (ok)
	    msg.retValue() = item;
    }
    else if (*action == "window_show")
	ok = Client::setVisible(name,true);
    else if (*action == "window_hide")
	ok = Client::setVisible(name,false);
    else if (*action == "window_popup")
	ok = Client::openPopup(name,&msg,Client::getWindow(msg.getValue("parent")));
    Client::self()->unlockOther();
    return ok;
}

// Process call.cdr message
bool DefaultLogic::handleCallCdr(Message& msg, bool& stopLogic)
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

    // Update UI/history
    callLogUpdate(msg,true,true);
    return false;
}

// Process user.login message
bool DefaultLogic::handleUserLogin(Message& msg, bool& stopLogic)
{
    return false;
}

// Process user.notify message
bool DefaultLogic::handleUserNotify(Message& msg, bool& stopLogic)
{
    if (!Client::self())
	return false;
    // get name of the account
    NamedString* account = msg.getParam("account");
    if (!account)
	return false;
    // see if it's registered, get the protocol and reason
    bool reg = msg.getBoolValue("registered");
    const char* proto = msg.getValue("protocol");
    const char* reason = msg.getValue("reason");
    String txt = reg ? "Registered" : "Unregistered";
    if (proto)
	txt << " " << proto;
    txt << " account " << *account;
    if (reason)
	txt << " reason: " << reason;

    // block until client finishes initialization
    while (!Client::self()->initialized())
	Thread::msleep(10);

    // Update interface
    NamedList p("");
    p.copyParams(msg,"account,protocol");
    String status = (reg ? "Registered" : "Unregistered");
    if (!reg && reason)
	status << ": " << reason; 
    p.addParam("status",status); 
    Client::self()->setTableRow(s_accountList,*account,&p);
    // Remove account's contacts if unregistered
    while (!reg) {
	NamedList contacts("");
	if (!Client::self()->getOptions(s_contactList,&contacts))
	    break;
	unsigned int n = contacts.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* param = contacts.getParam(i);
	    NamedList p("");
	    if (!(param && Client::self()->getTableRow(s_contactList,param->name(),&p)))
		continue;
	    NamedString* acc = p.getParam("hidden:account");
	    if (acc && *acc == *account)
		Client::self()->delOption(s_contactList,param->name());
	}
	break;
    }

    Client::self()->setStatusLocked(txt);
    return false;
}

// Utility used when handling resource.notify
// Parse a jabber JID: node@domain[/resource]
static bool decodeJID(const String& jid, String& bare, String& res)
{
    int pos = jid.find("/");
    if (pos < 0) {
	bare = jid;
	res = "";
    }
    else {
	bare = jid.substr(0,pos);
	res = jid.substr(pos + 1);
    }
    return !bare.null();
}

// Process resource.notify message
bool DefaultLogic::handleResourceNotify(Message& msg, bool& stopLogic)
{
    if (!Client::self())
	return false;

    NamedString* module = msg.getParam("module");
    // Avoid loopback
    if (module && ClientDriver::self() && ClientDriver::self()->name() == *module)
	return false;

    NamedString* account = msg.getParam("account");
    if (!(account && Client::self()->hasOption(s_accountList,*account)))
	return false;

    // Check for contact list update
    NamedString* contact = msg.getParam("contact");
    if (contact) {
	DDebug(ClientDriver::self(),DebugStub,
	    "Logic(%s) account=%s contact='%s' [%p]",
	    toString().c_str(),account->c_str(),contact->c_str(),this);
	return true;
    }

    // Presence update
    contact = msg.getParam("from");
    if (!contact)
	return false;

    // Process errors
    NamedString* error = msg.getParam("error");
    if (error) {
	String txt;
	txt << "Account '" << *account << "' received error='" << *error <<
	    "' from '" << *contact << "'";
	Client::self()->addToLog(txt);
	return true;
    }

    // Subscription status
    NamedString* status = msg.getParam("status");
    bool sub = (status && *status == "subscribed");
    if (sub || (status && *status == "unsubscribed")) {
	String txt;
	txt << "Account '" << *account << "'. Contact '" << *contact <<
	    (sub ? " accepted" : " removed") << "' subscription";
	Client::self()->addToLog(txt);
	return true;
    }

    // Update presence
    bool offline = (status && *status == "offline");
    NamedString* proto = msg.getParam("protocol");
    bool jabber = proto && *proto && (*proto == "jabber" || *proto == "xmpp" || *proto == "jingle");
    String contactName = *contact;
    String contactUri = *contact;
    // Special care for jabber
    if (jabber) {
	String res;
	if (!decodeJID(*contact,contactName,res))
	    return false;
	contactName.toLower();
	if (offline) {
	    // Get all contacts
	    NamedList p("");
	    Client::self()->getOptions(s_contactList,&p);
	    unsigned int n = p.length();
	    // Remove all contact's resources if res is null
	    // Check account
	    String compare = contactName + "/" + res;
	    for (unsigned int i = 0; i < n; i++) {
		NamedString* s = p.getParam(i);
		if (!s)
		    continue;
		// Check if this a candidate
		// Resource: full
		// No resource: check bare
		if ((res && s->name() != compare) || (!res && s->name().startsWith(compare)))
		    continue;
		// This is a candidate: check account
		NamedList c("");
		Client::self()->getTableRow(s_contactList,s->name(),&c);
		NamedString* acc = c.getParam("hidden:account");
		// Delete if account matches
		if (acc && *acc == *account)
		    Client::self()->delTableRow(s_contactList,s->name());
	    }
	    return true;
	}
	// Presence: ignore if no resource
	if (!res)
	    return false;
	contactUri = contactName + "/" + res;
    }
    else if (offline)
	Client::self()->delTableRow(s_contactList,*contact);

    if (!offline) {
	NamedList p("");
	p.addParam("name",contactName);
	p.addParam("number/uri",contactUri);
	p.addParam("hidden:account",*account);
	p.addParam("hidden:protocol",msg.getValue("protocol"));
	p.addParam("hidden:editable",String::boolText(!jabber));
	Client::self()->updateTableRow(s_contactList,contactUri,&p);
    }
	
    return true;
}

// Process resource.subscribe message
bool DefaultLogic::handleResourceSubscribe(Message& msg, bool& stopLogic)
{
    if (!Client::self())
	return false;

    NamedString* module = msg.getParam("module");
    // Avoid loopback
    if (module && ClientDriver::self() && ClientDriver::self()->name() == *module)
	return false;

    NamedString* account = msg.getParam("account");
    NamedString* oper = msg.getParam("operation");
    NamedString* contact = msg.getParam("from");
    if (!(account && oper && contact))
	return false;
    bool sub = (*oper == "subscribe");
    if (!sub && *oper != "unsubscribe")
	return false;

    if (!Client::self()->hasOption(s_accountList,*account))
	return false;
    Message* m = new Message("resource.notify");
    m->addParam("module",ClientDriver::self()->name());
    m->copyParam(msg,"protocol");
    m->addParam("account",*account);
    m->addParam("to",*contact);
    m->addParam("status",*oper + "d");
    Engine::enqueue(m);
    return true;
}

// Process chan.startup message
bool DefaultLogic::handleClientChanUpdate(Message& msg, bool& stopLogic)
{
#define CHANUPD_ID (chan ? chan->id() : *id)
#define CHANUPD_ADDR (chan ? chan->address() : String::empty())

    if (!Client::self())
	return false;
    int notif = ClientChannel::lookup(msg.getValue("notify"));
    if (notif == ClientChannel::Destroyed) {
	if (!Client::valid())
	    return false;
	String id = msg.getValue("id");
	// Reset init transfer if destroyed
	if (m_transferInitiated && m_transferInitiated == id)
	    m_transferInitiated = "";
	// Stop incoming ringer if there are no more incoming channels
	if (ClientSound::started(Client::s_ringInName) && ClientDriver::self()) {
	    ClientDriver::self()->lock();
	    ObjList* o = ClientDriver::self()->channels().skipNull();
	    for (; o; o = o->skipNext())
		if ((static_cast<Channel*>(o->get()))->isOutgoing())
		    break;
	    ClientDriver::self()->unlock();
	    if (!o)
		Client::self()->ringer(true,false);
	}
	Client::self()->delOption(s_channelList,id);
	enableCallActions(m_selectedChannel);
	String status;
	buildStatus(status,"Hung up",msg.getValue("address"),id,msg.getValue("reason"));
	Client::self()->setStatusLocked(status);
	return false;
    }
    // Set some data from channel
    ClientChannel* chan = static_cast<ClientChannel*>(msg.userData());
    // We MUST have an ID
    NamedString* id = 0;
    if (!chan)
	id = msg.getParam("id");
    if (!(chan || id))
	return false;
    bool outgoing = chan ? chan->isOutgoing() : msg.getBoolValue("outgoing");
    bool noticed = chan ? chan->isNoticed() : msg.getBoolValue("noticed");
    bool active = chan ? chan->active() : msg.getBoolValue("active");
    bool silence = msg.getBoolValue("silence");
    bool notConf = !(chan ? chan->conference() : msg.getBoolValue("conference"));

    // Stop ringing on not silenced active outgoing channels
    if (active && !outgoing && !silence)
	Client::self()->ringer(false,false);

    // Update UI
    NamedList p("");
    bool updateFormats = true;
    bool enableActions = false;
    bool setStatus = notConf;
    String status;
    switch (notif) {
	case ClientChannel::Active:
	    enableActions = true;
	    updateFormats = false;
    	    buildStatus(status,"Call active",CHANUPD_ADDR,CHANUPD_ID);
	    Client::self()->setSelect(s_channelList,CHANUPD_ID);
	    setImageParam(p,"party",outgoing ? "down_active.png" : "up_active.png");
	    if (outgoing) {
		if (noticed)
		    Client::self()->ringer(true,false);
	    }
	    else {
		Client::self()->ringer(true,false);
		if (silence)
		    Client::self()->ringer(false,true);
	    }
	    break;
	case ClientChannel::OnHold:
	    enableActions = true;
	    buildStatus(status,"Call on hold",CHANUPD_ADDR,CHANUPD_ID);
	    setImageParam(p,"party",outgoing ? "down.png" : "up.png");
	    if (outgoing) {
		if (noticed)
		    Client::self()->ringer(true,false);
	    }
	    else {
		Client::self()->ringer(true,false);
		Client::self()->ringer(false,false);
	    }
	    break;
	case ClientChannel::Ringing:
	    buildStatus(status,"Call ringing",CHANUPD_ADDR,CHANUPD_ID);
	    if (notConf)
		setImageParam(p,"time","chan_ringing.png");
	    break;
	case ClientChannel::Noticed:
	    // Stop incoming ringer
	    Client::self()->ringer(true,false);
	    buildStatus(status,"Call noticed",CHANUPD_ADDR,CHANUPD_ID);
	    break;
	case ClientChannel::Progressing:
	    buildStatus(status,"Call progressing",CHANUPD_ADDR,CHANUPD_ID);
	    if (notConf)
		setImageParam(p,"time","chan_progress.png");
	    break;
	case ClientChannel::Startup:
	    enableActions = true;
	    // Create UI entry
	    if (chan && Client::self()->addTableRow(s_channelList,CHANUPD_ID,&p)) {
		DurationUpdate* d = new DurationUpdate(this,false,CHANUPD_ID,"time");
		chan->setClientData(d);
		TelEngine::destruct(d);
	    }
	    else
		return false;
	    setImageParam(p,"party",chan ? chan->party() : "",outgoing ? "down.png" : "up.png");
	    setImageParam(p,"time","",outgoing ? "chan_ringing.png" : "chan_idle.png");
	    // Start incoming ringer if there is no active channel
	    if (outgoing && notConf) {
		ClientChannel* ch = ClientDriver::findActiveChan();
		if (!ch)
		    Client::self()->ringer(true,true);
		else
		    TelEngine::destruct(ch);
	    }
	    setStatus = false;
	    p.setParam("status",outgoing ? "incoming" : "outgoing");
	    break;
	case ClientChannel::Accepted:
	    buildStatus(status,"Calling target",0,0);
	    break;
	case ClientChannel::Answered:
	    enableActions = true;
	    buildStatus(status,"Call answered",CHANUPD_ADDR,CHANUPD_ID);
	    setImageParam(p,"time","answer.png");
	    // Stop incoming ringer
	    Client::self()->ringer(true,false);
	    if (active)
		Client::self()->ringer(false,false);
	    break;
	case ClientChannel::Routed:
	    updateFormats = false;
	    buildStatus(status,"Calling",chan ? chan->party() : "",0,0);
	    if (notConf)
		setImageParam(p,"time","chan_routed.png");
	    break;
	case ClientChannel::Rejected:
	    updateFormats = false;
	    buildStatus(status,"Call failed",CHANUPD_ADDR,CHANUPD_ID,msg.getValue("reason"));
	    break;
	case ClientChannel::Transfer:
	    updateFormats = false;
	    enableActions = true;
	    // Transferred
	    if (chan && chan->transferId() && notConf) {
		setStatus = false;
		ClientChannel* trans = ClientDriver::findChan(chan->transferId());
		setImageParam(p,"status",trans ? trans->party() : "","transfer.png");
		TelEngine::destruct(trans);
	    	buildStatus(status,"Call transferred",CHANUPD_ADDR,CHANUPD_ID);
	    }
	    else if (notConf)
		setImageParam(p,"status","","");
	    break;
	case ClientChannel::Conference:
	    updateFormats = false;
	    enableActions = true;
	    if (notConf)
		setImageParam(p,"status","","");
	    else {
		const char* s = (chan && chan->transferId()) ? chan->transferId().safe() : "";
		setImageParam(p,"status",s,"conference.png");
	    }
	    break;
	default:
	    enableActions = true;
	    updateFormats = false;
	    buildStatus(status,String("Call notification=") + msg.getValue("notify"),
		CHANUPD_ADDR,CHANUPD_ID);
    }

    if (enableActions && m_selectedChannel == CHANUPD_ID)
	enableCallActions(m_selectedChannel);
    if (status)
	Client::self()->setStatusLocked(status);
    if (updateFormats && chan) {
	String fmt;
	fmt << (chan->peerOutFormat() ? chan->peerOutFormat().c_str() : "-");
	fmt << "/";
	fmt << (chan->peerInFormat() ? chan->peerInFormat().c_str() : "-");
	p.addParam("format",fmt);
    }
    if (setStatus && chan)
	p.setParam("status",chan->status());
    Client::self()->setTableRow(s_channelList,CHANUPD_ID,&p);
    return false;

#undef CHANUPD_ID
#undef CHANUPD_ADDR
}

// Default message processor called for id's not defined in client.
bool DefaultLogic::defaultMsgHandler(Message& msg, int id, bool& stopLogic)
{
    if (id == Client::ChanNotify) {
	String event = msg.getValue("event");
	if (event != "left")
	    return false;

	// Check if we have a channel in conference whose peer is the one who left
	const char* peer = msg.getValue("lastpeerid");
	ClientChannel* chan = ClientDriver::findChanByPeer(peer);
	if (!chan)
	    return false;
	if (chan->conference()) {
	    DDebug(ClientDriver::self(),DebugInfo,
		"Channel %s left the conference. Terminating %s",
		peer,chan->id().c_str());
	    // Try to use Client's way first
	    if (Client::self())
		Client::self()->callTerminate(chan->id());
	    else
		chan->disconnect("Peer left the conference");
	}
	TelEngine::destruct(chan);
	return false;
    }
    return false;
}

// Client created and initialized all windows
void DefaultLogic::initializedWindows()
{
    if (!Client::self())
	return;

    // Fill protocol lists
    String proto = "protocol";
    String acc_proto = "acc_protocol";
    if (!Client::self()->hasOption(proto,s_notSelected))
	Client::self()->addOption(proto,s_notSelected,true);
    s_protocolsMutex.lock();
    for (ObjList* o = s_protocols.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (!*s)
	    continue;
	if (!Client::self()->hasOption(proto,*s))
	    Client::self()->addOption(proto,*s,false);
	if (!Client::self()->hasOption(acc_proto,*s))
	    Client::self()->addOption(acc_proto,*s,false);
    }
    s_protocolsMutex.unlock();
    // Add account/providers 'not selected' item
    String tmp = "account";
    if (!Client::self()->hasOption(tmp,s_notSelected))
	Client::self()->addOption(tmp,s_notSelected,true);
    tmp = "acc_providers";
    if (!Client::self()->hasOption(tmp,s_notSelected))
	Client::self()->addOption(tmp,s_notSelected,true);

    // Make sure the active page is the calls one
    activatePageCalls(this);
}

// Initialize client from settings
bool DefaultLogic::initializedClient()
{
    m_accShowAdvanced = Client::s_settings.getBoolValue("client","showaccadvanced",m_accShowAdvanced);
    if (Client::self())
	Client::self()->setCheck("toggleAccAdvanced",m_accShowAdvanced);

    // Check if global settings override the users'
    NamedList* clientSect = Engine::config().getSection("client");
    bool globalOverride = clientSect && clientSect->getBoolValue("globaloverride",false);

    // Booleans
    for (unsigned int i = 0; i < Client::OptCount; i++) {
	bool tmp = Client::self()->getBoolOpt((Client::ClientToggle)i);
	bool active = true;
	if (globalOverride) {
	    String* over = clientSect->getParam(Client::s_toggles[i]);
	    if (over) {
		tmp = over->toBoolean(tmp);
		active = false;
	    }
	    else
		tmp = Client::s_settings.getBoolValue("general",Client::s_toggles[i],tmp);
	}
	else {
	    tmp = Engine::config().getBoolValue("client",Client::s_toggles[i],tmp);
	    tmp = Client::s_settings.getBoolValue("general",Client::s_toggles[i],tmp);
	}
	if (Client::self())
	    Client::self()->setActive(Client::s_toggles[i],active);
	setClientParam(Client::s_toggles[i],String::boolText(tmp),false,true);
    }

    // Other string parameters
    setClientParam("username",Client::s_settings.getValue("default","username"),false,true);
    setClientParam("callerid",Client::s_settings.getValue("default","callerid"),false,true);
    setClientParam("domain",Client::s_settings.getValue("default","domain"),false,true);
    // Create default ring sound
    String ring = Client::s_settings.getValue("general","ringinfile",Client::s_soundPath + "ring.wav");
    Client::self()->createSound(Client::s_ringInName,ring);
    ring = Client::s_settings.getValue("general","ringoutfile",Client::s_soundPath + "tone.wav");
    Client::self()->createSound(Client::s_ringOutName,ring);

    // Enable call actions
    enableCallActions(m_selectedChannel);

    // Set chan.notify handler
    if (Client::self())
	Client::self()->installRelay("chan.notify",Client::ChanNotify,100);
    return false;
}

// Client is exiting: save settings
void DefaultLogic::exitingClient()
{
    clearDurationUpdate();

    if (!Client::self())
	return;

    String tmp;
    if (Client::self()->getText("def_username",tmp))
	Client::s_settings.setValue("default","username",tmp);
    tmp.clear();
    if (Client::self()->getText("def_callerid",tmp))
	Client::s_settings.setValue("default","callerid",tmp);
    tmp.clear();
    if (Client::self()->getText("def_domain",tmp))
	Client::s_settings.setValue("default","domain",tmp);
    Client::save(Client::s_settings);

    // Save callto history
    NamedList p("");
    if (Client::self()->getOptions(s_calltoList,&p)) {
	NamedList* sect = Client::s_calltoHistory.getSection("calls");
	if (sect)
	    Client::s_calltoHistory.clearSection(*sect);
	Client::s_calltoHistory.createSection("calls");
	sect = Client::s_calltoHistory.getSection("calls");
	unsigned int n = p.length();
	unsigned int max = 0;
	for (unsigned int i = 0; max < s_maxCallHistory && i < n; i++) {
	    NamedString* s = p.getParam(i);
	    if (!s)
		continue;
	    max++;
	    sect->addParam(s->name(),*s);
	}
	Client::save(Client::s_calltoHistory);
    }
}

// Update from UI the selected item in channels list
void DefaultLogic::updateSelectedChannel(const String* item)
{
    String old = m_selectedChannel;
    if (item)
	m_selectedChannel = *item;
    else if (Client::self())
	Client::self()->getSelect(s_channelList,m_selectedChannel);
    else
	m_selectedChannel = "";
    if (old != m_selectedChannel)
	channelSelectionChanged(old);
}

// Method called by the client when idle
void DefaultLogic::idleTimerTick(Time& time)
{
    for (ObjList* o = m_durationUpdate.skipNull(); o; o = o->skipNext())
	(static_cast<DurationUpdate*>(o->get()))->update(time.sec(),&s_channelList);
}

// Enable call actions
bool DefaultLogic::enableCallActions(const String& id)
{
    if (!Client::self())
	return false;
    ClientChannel* chan = id.null() ? 0 : ClientDriver::findChan(id);
    NamedList p("");

    // Answer/Hangup/Hold
    p.addParam("active:" + s_actionAnswer,String::boolText(chan && chan->isOutgoing() && !chan->isAnswered()));
    p.addParam("active:" + s_actionHangup,String::boolText(0 != chan));
    bool canHold = chan && chan->isAnswered();
    p.addParam("active:" + s_actionHold,String::boolText(canHold));
    p.addParam("check:" + s_actionHold,String::boolText(canHold && !chan->active()));

    // Transfer
    // Not allowed on conference channels
    bool active = false;
    bool checked = false;
    bool conf = chan && chan->conference();
    if (chan && !conf) {
	Lock lock(chan->driver());
	if (chan->driver() && chan->driver()->channels().count() > 1)
	    active = true;
	lock.drop();
	checked = (0 != chan->transferId());
    }
    p.addParam("active:" + s_actionTransfer,String::boolText(active));
    p.addParam("check:" + s_actionTransfer,String::boolText(active && checked));

    // Activate/deactivate conference button
    active = (0 != chan && chan->isAnswered());
    p.addParam("active:" + s_actionConf,String::boolText(active));
    p.addParam("check:" + s_actionConf,String::boolText(active && conf));

    TelEngine::destruct(chan);
    Client::self()->setParams(&p);
    return true;
}

// Fill call start parameter list from UI
bool DefaultLogic::fillCallStart(NamedList& p, Window* wnd)
{
    if (!checkParam(p,"target","callto",false,wnd))
	return false;
    checkParam(p,"line","line",false,wnd);
    checkParam(p,"protocol","protocol",true,wnd);
    checkParam(p,"account","account",true,wnd);
    checkParam(p,"caller","def_username",false,wnd);
    checkParam(p,"callername","def_callerid",false,wnd);
    checkParam(p,"domain","def_domain",false,wnd);
    return true;
}

// Notification on selection changes in channels list
void DefaultLogic::channelSelectionChanged(const String& old)
{
    Debug(ClientDriver::self(),DebugInfo,"channelSelectionChanged() to '%s' old='%s'",
	m_selectedChannel.c_str(),old.c_str());
    while (true) {
	// Check if the transfer button was pressed
	if (m_transferInitiated && m_transferInitiated == old) {
	    m_transferInitiated = "";
	    bool transfer = false;
	    if (Client::self())
		Client::self()->getCheck(s_actionTransfer,transfer);
	    if (transfer) {
		if (ClientDriver::setAudioTransfer(old,m_selectedChannel))
		    break;
		else if (Client::self())
		    Client::self()->setStatusLocked("Failed to transfer");
	    }
	}
	m_transferInitiated = "";
	// Set the active channel
	if (Client::self()->getBoolOpt(Client::OptActivateCallOnSelect) &&
	    m_selectedChannel && ClientDriver::self())
	    ClientDriver::self()->setActive(m_selectedChannel);
	break;
    }
    enableCallActions(m_selectedChannel);
}


/**
 * DurationUpdate
 */

// Destructor
DurationUpdate::~DurationUpdate()
{
    setLogic();
}

// Get a string representation of this object
const String& DurationUpdate::toString() const
{
    return m_id;
}

// Build a duration string representation and add the parameter to a list
unsigned int DurationUpdate::buildTimeParam(NamedList& dest, unsigned int secNow,
	bool force)
{
    return buildTimeParam(dest,m_name,m_startTime,secNow,force);
}

// Build a duration string representation hh:mm:ss. The hours are added only if non 0
unsigned int DurationUpdate::buildTimeString(String& dest, unsigned int secNow,
	bool force)
{
    return buildTimeString(dest,m_startTime,secNow,force);
}

// Set the logic used to update this duration object. Remove from the old one
void DurationUpdate::setLogic(ClientLogic* logic, bool owner)
{
    if (m_logic) {
	m_logic->removeDurationUpdate(this,false);
	m_logic = 0;
    }
    m_logic = logic;
    if (m_logic)
	m_logic->addDurationUpdate(this,owner);
}

// Update UI if duration is non 0
unsigned int DurationUpdate::update(unsigned int secNow, const String* table,
    Window* wnd, Window* skip, bool force)
{
    NamedList p("");
    unsigned int duration = buildTimeParam(p,secNow,force);
    if ((duration || force) && Client::self()) {
	if (table)
	    Client::self()->setTableRow(*table,toString(),&p,wnd,skip);
	else
	    Client::self()->setParams(&p,wnd,skip);
    }
    return duration;
}

// Build a duration string representation and add the parameter to a list
unsigned int DurationUpdate::buildTimeParam(NamedList& dest, const char* param,
    unsigned int secStart, unsigned int secNow, bool force)
{
    String tmp;
    unsigned int duration = buildTimeString(tmp,secStart,secNow,force);
    if (duration || force)
	dest.addParam(param,tmp);
    return duration;
}

// Build a duration string representation hh:mm:ss. The hours are added only if non 0
unsigned int DurationUpdate::buildTimeString(String& dest, unsigned int secStart,
    unsigned int secNow, bool force)
{
    if (secNow < secStart)
	secNow = secStart;
    unsigned int duration = secNow - secStart;
    if (!(duration || force))
	return 0;
    unsigned int hrs = duration / 3600;
    if (hrs)
	dest << hrs << ":";
    unsigned int rest = duration % 3600;
    unsigned int mins = rest / 60;
    unsigned int secs = rest % 60;
    dest << ((hrs && mins < 10) ? "0" : "") << mins << ":" << (secs < 10 ? "0" : "") << secs;
    return duration;
}

// Release memory. Remove from updater
void DurationUpdate::destroyed()
{
    setLogic();
    RefObject::destroyed();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
