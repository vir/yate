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

// Windows
static const String s_wndAccount = "account";           // Account edit/add
static const String s_wndAddrbook = "addrbook";         // Contact edit/add
// Some UI widgets
static const String s_channelList = "channels";
static const String s_accountList = "accounts";
static const String s_contactList = "contacts";
static const String s_logList = "log";
static const String s_calltoList = "callto";
static const String s_account = "account";               // Account selector
// Actions
static const String s_actionCall = "call";
static const String s_actionAnswer = "answer";
static const String s_actionHangup = "hangup";
static const String s_actionTransfer = "transfer";
static const String s_actionConf = "conference";
static const String s_actionHold = "hold";
static const String s_actionLogin = "acc_login";
static const String s_actionLogout = "acc_logout";
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
static void updateProtocolSpec(NamedList& p, const String& proto, const String& options,
    bool edit)
{
    ObjList* obj = options.split(',',false);
    String prefix = "acc_proto_" + proto;
    // Texts
    if (edit)
	setAccParam(p,prefix,"resource","");
    else
	setAccParam(p,prefix,"resource",proto == "jabber" ? "yate" : "");
    setAccParam(p,prefix,"port","");
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

// Utility: activate the calls page
inline void activatePageCalls(ClientLogic* logic, Window* wnd = 0)
{
    static String s_buttonCalls = "ctrlCalls";
    static String s_toggleCalls = "selectitem:framePages:PageCalls";
    Client::self()->setCheck(s_buttonCalls,true,wnd);
    logic->toggle(wnd,s_toggleCalls,true);
}

// Add/Update a contact list item
static void updateContactList(ClientContact& c, const String& inst = String::empty(),
    const char* uri = 0)
{
    DDebug(ClientDriver::self(),DebugAll,"updateContactList(%s,%s,%s)",
	c.toString().c_str(),inst.c_str(),uri);
    NamedList p("");
    p.addParam("name",c.m_name);
    p.addParam("number/uri",TelEngine::null(uri) ? c.uri().c_str() : uri);
    String id;
    c.buildInstanceId(id,inst);
    Client::self()->updateTableRow(s_contactList,id,&p);
}

// Remove all contacts starting with a given string
static void removeContacts(const String& idstart)
{
    NamedList p("");
    if (!Client::self()->getOptions(s_contactList,&p))
	return;
    DDebug(ClientDriver::self(),DebugAll,"removeContacts(%s)",idstart.c_str());
    unsigned int n = p.count();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = p.getParam(i);
	if (param && param->name().startsWith(idstart,false))
	    Client::self()->delTableRow(s_contactList,param->name());
    }
}

// Contact deleted: clear UI
static void contactDeleted(ClientContact& c)
{
    DDebug(ClientDriver::self(),DebugAll,"contactDeleted(%s)",c.toString().c_str());
    // Remove instances from contacts list
    String instid;
    removeContacts(c.buildInstanceId(instid));
}

// Remove all account contacts from UI
static void clearAccountContacts(ClientAccount& a)
{
    DDebug(ClientDriver::self(),DebugAll,"clearAccountContacts(%s)",a.toString().c_str());
    ObjList* o = 0;
    while (0 != (o = a.contacts().skipNull())) {
	ClientContact* c = static_cast<ClientContact*>(o->get());
	contactDeleted(*c);
	a.removeContact(c->toString());
    }
    // Clear account own instances
    if (a.contact() && a.contact()->resources().skipNull()) {
	String instid;
	a.contact()->buildInstanceId(instid);
	a.contact()->resources().clear();
	removeContacts(instid);
    }
}

// Retrieve the selected account
static inline ClientAccount* selectedAccount(ClientAccountList& accounts, Window* wnd = 0)
{
    String account;
    if (Client::valid())
	Client::self()->getSelect(s_accountList,account,wnd);
    return account ? accounts.findAccount(account) : 0;
}

// Build account action item from account id
static inline String& buildAccAction(String& buf, const String& action, ClientAccount* acc)
{
    buf = action + ":" + acc->toString();
    return buf;
}

// Fill acc_login/logout active parameters
static inline void fillAccLoginActive(NamedList& p, ClientAccount* acc)
{
    bool offline = !acc || acc->resource().offline();
    p.addParam("active:" + s_actionLogin,String::boolText(offline));
    p.addParam("active:" + s_actionLogout,String::boolText(!offline));
}

// Fill acc_login/logout item active parameters
static inline void fillAccItemLoginActive(NamedList& p, ClientAccount* acc)
{
    if (!acc)
	return;
    bool offline = !acc || acc->resource().offline();
    String tmp;
    p.addParam("active:" + buildAccAction(tmp,s_actionLogin,acc),String::boolText(offline));
    p.addParam("active:" + buildAccAction(tmp,s_actionLogout,acc),String::boolText(!offline));
}

// Fill acc_del/edit active parameters
static inline void fillAccEditActive(NamedList& p, bool active)
{
    const char* tmp = String::boolText(active);
    p.addParam("active:acc_del",tmp);
    p.addParam("active:acc_edit",tmp);
}

// Update account status and login/logout active status if selected
static void updateAccountStatus(ClientAccount* acc, ClientAccountList* accounts,
    Window* wnd = 0)
{
    if (!acc)
	return;
    NamedList p("");
    acc->fillItemParams(p);
    Client::self()->updateTableRow(s_accountList,acc->toString(),&p,false,wnd);
    // Set login/logout enabled status
    bool selected = accounts && acc == selectedAccount(*accounts,wnd);
    NamedList pp("");
    if (selected)
	fillAccLoginActive(pp,acc);
    fillAccItemLoginActive(pp,acc);
    Client::self()->setParams(&pp,wnd);
}

// Create or remove an account's menu
static void setAccountMenu(bool create, ClientAccount* acc)
{
    NamedList p("accountmenu" + acc->toString());
    p.addParam("owner","menuYate");
    if (create) {
	p.addParam("target","menuYate");
	p.addParam("title",acc->toString());
	p.addParam("before","acc_new");
	String in, out;
	buildAccAction(in,s_actionLogin,acc);
	buildAccAction(out,s_actionLogout,acc);
	p.addParam("item:" + in,"Login");
	p.addParam("item:" + out,"Logout");
	p.addParam("image:" + in,Client::s_skinPath + "handshake.png");
	p.addParam("image:" + out,Client::s_skinPath + "handshake_x.png");
	Client::self()->buildMenu(p);
	// Update menu
	NamedList pp("");
	fillAccItemLoginActive(pp,acc);
	Client::self()->setParams(&pp);
    }
    else
	Client::self()->removeMenu(p);
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
// Constructor
DefaultLogic::DefaultLogic(const char* name, int prio)
    : ClientLogic(name,prio),
    m_accounts(0)
{
    m_accounts = new ClientAccountList(name,new ClientAccount(NamedList::empty()));
}

// Destructor
DefaultLogic::~DefaultLogic()
{
    TelEngine::destruct(m_accounts);
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
    if (name.startsWith("clear:") && name.at(6))
	return clearList(name.substr(6),wnd);
    // Delete a list/table item
    if (name.startsWith("deleteitem:") && name.at(11)) {
	String list;
	int pos = name.find(":",11);
	if (pos > 0)
	    return deleteItem(name.substr(11,pos - 11),name.substr(pos + 1),wnd);
	return false;
    }
    // Delete a selected list/table item
    if (name.startsWith("deleteselecteditem:") && name.at(19))
	return deleteSelectedItem(name.substr(19),wnd);

    // 'settext' action
    if (name.startsWith("settext:") && name.at(8)) {
	int pos = name.find(':',9);
	String ctrl;
	String text;
	if (pos > 9) {
	    ctrl = name.substr(8,pos - 8);
	    text = name.substr(pos + 1);
	}
	else
	    ctrl = name.substr(8);
	bool ok = Client::self() && Client::self()->setText(ctrl,text,false,wnd);
	if (ok)
	    Client::self()->setFocus(ctrl,false,wnd);
	return ok;
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
    bool login = (name == s_actionLogin);
    if (login || name == s_actionLogout) {
	ClientAccount* acc = selectedAccount(*m_accounts,wnd);
	return acc ? loginAccount(acc->params(),login) : false;
    }
    login = name.startsWith(s_actionLogin + ":",false);
    if (login || name.startsWith(s_actionLogout + ":",false)) {
	ClientAccount* acc = 0;
	if (login)
	    acc = m_accounts->findAccount(name.substr(s_actionLogin.length() + 1));
	else
	    acc = m_accounts->findAccount(name.substr(s_actionLogout.length() + 1));
	return acc ? loginAccount(acc->params(),login) : false;
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
    bool logCall = (name == "log_call");
    if (logCall || name == "log_contact") {
	String billid;
	if (Client::valid())
	    Client::self()->getSelect(s_logList,billid,wnd);
	if (!billid)
	    return false;
	if (logCall)
	    return callLogCall(billid);
	return callLogCreateContact(billid);
    }
    if (name == "log_clear")
	return callLogClear(s_logList,String::empty());

    // *** Miscellaneous

    // Handle show window actions
    if (name.startsWith("action_show_"))
	Client::self()->setVisible(name.substr(12),true);
    // Help commands
    if (name.startsWith("help:"))
	return help(name,wnd);
    // Hide windows
    if (name == "button_hide" && wnd)
	return Client::self() && Client::self()->setVisible(wnd->toString(),false);
    // Quit
    if (name == "quit") {
	if (!Client::valid())
	    return false;
	Client::self()->quit();
	return true;
    }

    return false;
}

// Handle actions from checkable widgets
bool DefaultLogic::toggle(Window* wnd, const String& name, bool active)
{
    DDebug(ClientDriver::self(),DebugAll,
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
	const char* yText = String::boolText(active);
	const char* nText = String::boolText(!active);
	NamedList p("");
	p.addParam("check:toggle_show_" + wnd->toString(),yText);
	p.addParam("check:action_show_" + wnd->toString(),yText);
	if (wnd->id() == s_wndAccount) {
	    p.addParam("active:acc_new",nText);
	    if (active)
		fillAccEditActive(p,false);
	    else
		fillAccEditActive(p,0 != selectedAccount(*m_accounts));
	}
	else if (wnd->id() == s_wndAddrbook) {
	    p.addParam("active:abk_new",nText);
	    fillContactEditActive(p,!active);
	    fillLogContactActive(p,!active);
	}
	Client::self()->setParams(&p);
	return true;
    }

    // Select item if active. Return true if inactive
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
    if (name == "acc_showadvanced") {
	// Select the page. Set advanced for the current protocol
	String proto;
	if (!active)
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
	ClientAccount* a = item ? m_accounts->findAccount(item) : 0;
	NamedList p("");
	fillAccLoginActive(p,a);
	fillAccEditActive(p,!item.null());
	Client::self()->setParams(&p,wnd);
	return true;
    }

    if (name == s_contactList) {
	if (!Client::valid())
	    return false;
	NamedList p("");
	p.addParam("active:abk_call",String::boolText(!item.null()));
	fillContactEditActive(p,true,&item);
	Client::self()->setParams(&p,wnd);
	return true;
    }

    // Item selected in calls log list
    if (name == s_logList) {
	if (!Client::self())
	    return false;
	const char* active = String::boolText(!item.null());
	NamedList p("");
	p.addParam("active:log_call",active);
	p.addParam("active:log_del",active);
	fillLogContactActive(p,true,&item);
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
	if (!Client::self())
	    return false;
	bool adv = false;
	Client::self()->getCheck("acc_showadvanced",adv,wnd);
	String what = proto + "_" + (adv ? item.safe() : "none");
	return Client::self()->setSelect(proto,what,wnd);
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
	const String& proto = (*sect)["protocol"];
	if (proto) {
	    bool adv = false;
	    Client::self()->getCheck("acc_showadvanced",adv,wnd);
	    selectProtocolSpec(p,proto,adv);
	    updateProtocolSpec(p,proto,(*sect)["options"],wnd && wnd->context());
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
	Client::self()->delTableRow(s_calltoList,*ns);
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
    if (!Client::valid())
	return false;

    // Send digits (DTMF) on active channel
    // or add them to 'callto' box
    const String& digits = params["digits"];
    if (!digits)
	return false;
    if (Client::self()->emitDigits(digits))
	return true;
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
    if (!Client::valid() || Client::self()->getVisible(s_wndAccount))
	return false;
    // Make sure we reset all controls in window
    USE_SAFE_PARAMS("select:acc_providers",s_notSelected);
    bool loginNow = Client::s_settings.getBoolValue("client","acc_loginnow",true);
    params->setParam("check:acc_loginnow",String::boolText(loginNow));
    String acc;
    String proto;
    bool enabled = true;
    if (newAcc) {
	for (const String* par = s_accParams; !par->null(); par++)
	    params->setParam("acc_" + *par,"");
	enabled = Client::s_settings.getBoolValue("client","acc_enabled",true);
	proto = Client::s_settings.getValue("client","acc_protocol","sip");
	// Check if the protocol is valid. Retrieve the first one if invalid
	s_protocolsMutex.lock();
	if (proto && !s_protocols.find(proto))
	    proto = "";
	if (!proto) {
	    ObjList* o = s_protocols.skipNull();
	    if (o)
		proto = o->get()->toString();
	}
	if (!proto)
	    proto = "none";
	s_protocolsMutex.unlock();
    }
    else {
	ClientAccount* a = selectedAccount(*m_accounts,wnd);
	if (!a)
	    return false;
	acc = a->toString();
	enabled = a->startup();
	proto = a->protocol();
	for (const String* par = s_accParams; !par->null(); par++)
	    params->setParam("acc_" + *par,a->params().getValue(*par));
    }
    // Protocol combo and specific widget (page) data
    params->setParam("check:acc_enabled",String::boolText(enabled));
    bool adv = Client::s_settings.getBoolValue("client","acc_showadvanced",true);
    params->setParam("check:acc_showadvanced",String::boolText(adv));
    selectProtocolSpec(*params,proto,adv);
    NamedString* tmp = params->getParam("acc_options");
    s_protocolsMutex.lock();
    for (ObjList* o = s_protocols.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (*s)
	    updateProtocolSpec(*params,*s,tmp ? *tmp : String::empty(),!newAcc);
    }
    s_protocolsMutex.unlock();
    params->setParam("title",newAcc ? "Add account" : "Edit account: " + acc);
    params->setParam("context",acc);
    params->setParam("acc_account",acc);
    return Client::openPopup(s_wndAccount,params);
}

// Utility function used to save a widget's text
static inline void saveAccParam(NamedList& params,
	const String& prefix, const String& param, Window* wnd)
{
    String val;
    Client::self()->getText(prefix + param,val,false,wnd);
    if (val)
	params.setParam(param,val);
    else
	params.clearParam(param);
}

// Called when the user wants to save account data
bool DefaultLogic::acceptAccount(NamedList* params, Window* wnd)
{
    if (!Client::valid())
	return false;
    String account;
    String proto;
    const char* err = 0;
    ClientAccount* edit = 0;
    while (true) {
#define SET_ERR_BREAK(e) { err = e; break; }
	// Check required data
	Client::self()->getText("acc_account",account,false,wnd);
	if (!account)
	    SET_ERR_BREAK("Account name field can't be empty");
	Client::self()->getText("acc_protocol",proto,false,wnd);
	if (!proto)
	    SET_ERR_BREAK("A protocol must be selected");
	ClientAccount* upd = m_accounts->findAccount(account);
	if (wnd && wnd->context())
	    edit = m_accounts->findAccount(wnd->context());
	if (edit) {
	    if (upd && upd != edit) {
		// Don't know what to do: replace the duplicate or rename the editing one
		SET_ERR_BREAK("An account with the same name already exists");
	    }
	}
	else if (upd) {
	    SET_ERR_BREAK("An account with the same name already exists");
	    // TODO: ask to replace
	}
	// Fallthrough to add a new account or check if existing changed
	break;
#undef SET_ERR_BREAK
    }
    if (err) {
	if (!Client::openMessage(err,wnd))
	    Debug(ClientDriver::self(),DebugNote,"Logic(%s). %s",toString().c_str(),err);
	return false;
    }
    NamedList p(account);
    // Account flags
    bool enable = true;
    Client::self()->getCheck("acc_enabled",enable,wnd);
    p.addParam("enabled",String::boolText(enable));
    p.addParam("protocol",proto);
    String prefix = "acc_";
    // Save account parrameters
    for (const String* par = s_accParams; !par->null(); par++)
	saveAccParam(p,prefix,*par,wnd);
    // Special care for protocol specific data
    prefix << "proto_" << proto << "_";
    // Texts
    saveAccParam(p,prefix,"resource",wnd);
    saveAccParam(p,prefix,"port",wnd);
    saveAccParam(p,prefix,"address",wnd);
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
    p.addParam("options",options,false);
    bool login = false;
    Client::self()->getCheck("acc_loginnow",login,wnd);
    if (edit) {
	// Set changed only if online
	bool changed = false;
	if (edit->toString() != account) {
	    if (edit->resource().offline()) {
		// Remove the old account and add the new one
		delAccount(edit->toString(),0);
		edit = 0;
	    }
	    else
		changed = true;
	}
	else if (!edit->resource().offline()) {
	    // Compare account parameters. Avoid parameters not affecting the connection
	    NamedList l1(p), l2(edit->params());
	    l1.clearParam("enabled");
	    l2.clearParam("enabled");
	    String a1, a2;
	    l1.dump(a1,"");
	    l2.dump(a2,"");
	    changed = a1 != a2;
	}
	if (changed) {
	    Client::openMessage("Can't change a registered account",wnd);
	    return false;
	}
    }
    if (!updateAccount(p,login,true))
	return false;
    if (!wnd)
	return true;
    // Hide the window. Save some settings
    bool showAccAdvanced = false;
    Client::self()->getCheck("acc_showadvanced",showAccAdvanced,wnd);
    Client::self()->setVisible(wnd->toString(),false);
    Client::s_settings.setValue("client","acc_protocol",proto);
    Client::s_settings.setValue("client","acc_showadvanced",String::boolText(showAccAdvanced));
    Client::s_settings.setValue("client","acc_enabled",String::boolText(enable));
    Client::s_settings.setValue("client","acc_loginnow",String::boolText(login));
    Client::save(Client::s_settings);
    return true;
}

// Called when the user wants to delete an existing account
bool DefaultLogic::delAccount(const String& account, Window* wnd)
{
    if (!account)
	return deleteSelectedItem(s_accountList + ":",wnd);
    ClientAccount* acc = m_accounts->findAccount(account);
    if (!acc)
	return false;
    // Disconnect
    Engine::enqueue(acc->userlogin(false));
    // Delete from memory and UI. Save the accounts file
    clearAccountContacts(*acc);
    Client::self()->delTableRow(s_account,account);
    Client::self()->delTableRow(s_accountList,account);
    setAccountMenu(false,acc);
    acc->save(false);
    m_accounts->removeAccount(account);
    return true;
}

// Add/set an account to UI. Save accounts file and login if required
bool DefaultLogic::updateAccount(const NamedList& account, bool login, bool save)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) updateAccount(%s,%s,%s)",
	toString().c_str(),account.c_str(),String::boolText(login),String::boolText(save));
    if (!Client::valid() || account.null())
	return false;
    ClientAccount* acc = m_accounts->findAccount(account,true);
    if (!acc) {
	acc = new ClientAccount(account);
	if (m_accounts->appendAccount(acc)) {
	    // Add account menu
	    setAccountMenu(true,acc);
	}
	else
	    TelEngine::destruct(acc);
    }
    else
	acc->m_params = account;
    if (!acc)
	return false;

    // (Re)set account own contact
    String cId;
    String uri;
    const String& user = acc->m_params["username"];
    const String& host = acc->m_params["domain"];
    if (user && host) {
	uri << user << "@" << host;
	ClientContact::buildContactId(cId,acc->toString(),uri);
    }
    else
	cId = acc->toString();
    acc->setContact(new ClientContact(0,NamedList::empty(),cId,uri));
    if (save)
	acc->save();
    // Update account list
    NamedList p("");
    acc->fillItemParams(p);
    Client::self()->updateTableRow(s_accountList,account,&p);
    if (login && Client::s_engineStarted)
	loginAccount(acc->params(),true);
    TelEngine::destruct(acc);
    return true;
}

// Login/logout an account
bool DefaultLogic::loginAccount(const NamedList& account, bool login)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) loginAccount(%s,%s)",
	toString().c_str(),account.c_str(),String::boolText(login));

    Message* m = 0;
    ClientAccount* acc = m_accounts->findAccount(account);
    if (acc)
	m = acc->userlogin(login);
    else {
	m = Client::buildMessage("user.login",account,login ? "login" : "logout");
	if (login)
	    m->copyParams(account);
	else
	    m->copyParams(account,"protocol");
    }
    bool ok = Engine::enqueue(m);
    // Done if failed or logout
    if (!(ok && login))
	return ok;
    // Update UI account status
    if (!(acc && acc->resource().offline() && Client::valid()))
	return true;
    acc->resource().setStatus(ClientResource::Connecting);
    acc->resource().setStatusText("");
    updateAccountStatus(acc,m_accounts);
    return true;
}

// Add/update a contact
bool DefaultLogic::updateContact(const NamedList& params, bool save, bool update)
{
    if (!(Client::valid() && (save || update) && params))
	return false;
    const String& target = params["target"];
    if (!target)
	return false;
    // Fix contact id
    String id;
    String pref;
    ClientContact::buildContactId(pref,m_accounts->localContacts()->toString(),String::empty());
    if (params.startsWith(pref,false))
	id = params;
    else
	ClientContact::buildContactId(id,m_accounts->localContacts()->toString(),params);
    ClientContact* c = m_accounts->findContact(id);
    if (!c)
	c = new ClientContact(m_accounts->localContacts(),params,id,target);
    else if (c) {
	const String& name = params["name"];
	if (name)
	    c->m_name = name;
	c->setUri(target);
    }
    else
	return false;
    // Update UI
    if (update)
	updateContactList(*c);
    // Save file
    bool ok = true;
    if (save && m_accounts->isLocalContact(c)) {
	String name;
	c->getContactSection(name);
	unsigned int n = params.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params.getParam(i);
	    if (!ns)
		continue;
	    if (*ns)
		Client::s_contacts.setValue(name,ns->name(),*ns);
	    else
		Client::s_contacts.clearKey(name,ns->name());
	}
	ok = Client::save(Client::s_contacts);
    }
    // Notify server if this is a client account (stored on server)
    // TODO: implement
    return true;
}

// Called when the user wants to save contact data
bool DefaultLogic::acceptContact(NamedList* params, Window* wnd)
{
    if (!Client::valid())
	return false;

    const char* err = 0;
    String id;
    String name;
    String target;
    // Check required data
    while (true) {
#define SET_ERR_BREAK(e) { err = e; break; }
	Client::self()->getText("abk_name",name,false,wnd);
	if (!name)
	    SET_ERR_BREAK("A contact name must be specified");
	Client::self()->getText("abk_target",target,false,wnd);
	if (!target)
	    SET_ERR_BREAK("Contact number/target field can't be empty");
	// Check if adding/editing contact. Generate a new contact id
	if (wnd && wnd->context())
	    id = wnd->context();
	else {
	    String tmp;
	    tmp << (unsigned int)Time::msecNow() << "_" << (int)Engine::runId();
	    ClientContact::buildContactId(id,m_accounts->localContacts()->toString(),tmp);
	}
	ClientContact* existing = m_accounts->localContacts()->findContact(id);
	ClientContact* dup = 0;
	if (existing) {
	    if (existing->m_name == name && existing->uri() == target) {
		// No changes: return
		if (wnd)
		    Client::self()->setVisible(wnd->toString(),false);
		return true;
	    }
	    dup = m_accounts->localContacts()->findContact(&name,0,&id);
	}
	else
	    dup = m_accounts->localContacts()->findContact(&name);
	if (dup)
	    SET_ERR_BREAK("A contact with the same name already exists!");
	break;
#undef SET_ERR_BREAK
    }
    if (err) {
	Client::openMessage(err,wnd);
	return false;
    }
    NamedList p(id);
    p.addParam("name",name);
    p.addParam("target",target);
    if (!updateContact(p,true,true))
	return false;
    if (wnd)
	Client::self()->setVisible(wnd->toString(),false);
    return true;
}

// Called when the user wants to add a new contact or edit an existing one
bool DefaultLogic::editContact(bool newCont, NamedList* params, Window* wnd)
{
    if (!Client::valid())
	return false;
    // Make sure we reset all controls in window
    NamedList p("");
    if (newCont) {
	p.addParam("abk_name",params ? params->c_str() : "");
	p.addParam("abk_target",params ? params->getValue("target") : "");
    }
    else {
	String cont;
	Client::self()->getSelect(s_contactList,cont);
	ClientContact* c = cont ? m_accounts->findContactByInstance(cont) : 0;
	if (!(c && m_accounts->isLocalContact(c)))
	    return false;
	p.addParam("context",c->toString());
	p.addParam("abk_name",c->m_name);
	p.addParam("abk_target",c->uri());
    }
    return Client::openPopup(s_wndAddrbook,&p);
}

// Called when the user wants to delete an existing contact
bool DefaultLogic::delContact(const String& contact, Window* wnd)
{
    if (!Client::valid())
	return false;
    if (!contact)
	return deleteSelectedItem(s_contactList + ":",wnd);
    ClientContact* c = m_accounts->findContactByInstance(contact);
    if (!(c && m_accounts->isLocalContact(c)))
	return false;
    // Delete the contact from config and all UI controls
    contactDeleted(*c);
    String sectName;
    c->getContactSection(sectName);
    Client::s_contacts.clearSection(sectName);
    m_accounts->localContacts()->removeContact(contact);
    Client::save(Client::s_contacts);
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
    return Client::valid() && Client::self()->updateTableRow("acc_providers",provider);
}

// Called when the user wants to call an existing contact
bool DefaultLogic::callContact(NamedList* params, Window* wnd)
{
    if (!Client::valid())
	return false;
    NamedList dummy("");
    if (!params) {
	params = &dummy;
	Client::self()->getSelect(s_contactList,*params);
    }
    if (!Client::self()->getTableRow(s_contactList,*params,params))
	return false;
    const String& target = (*params)["number/uri"];
    if (!target)
	return false;
    bool call = true;
    String account;
    String proto;
    ClientContact* c = m_accounts->findContactByInstance(*params);
    if (!m_accounts->isLocalContact(c)) {
	// Not a local contact: check if it belongs to registered account
	if (c && c->account() && c->account()->resource().online()) {
	    account = c->account()->toString();
	    proto = c->account()->protocol();
	}
	call = !account.null();
    }
    else {
	static const Regexp r("^[a-z0-9]\\+/");
	if (!r.matches(target)) {
	    // Incomplete target:
	    //   1 registered account: call from it
	    //   ELSE: fill callto and activate the calls page
	    // Skip the jabber protocol: we can't call incomplete targets on it
	    String skip("jabber");
	    ClientAccount* a = m_accounts->findSingleRegAccount(&skip);
	    if (a) {
		account = a->toString();
		proto = a->protocol();
	    }
	    call = !account.null();
	}
    }
    if (call) {
	NamedList p("");
	p.addParam("line",account,false);
	p.addParam("account",account,false);
	p.addParam("target",target);
	p.addParam("protocol",proto,false);
	return callStart(p);
    }
    Client::self()->setText(s_calltoList,target);
    activatePageCalls(this);
    return true;
}

// Update the call log history
bool DefaultLogic::callLogUpdate(const NamedList& params, bool save, bool update)
{
    if (!(save || update))
	return false;
    String* bid = params.getParam("billid");
    const String& id = bid ? (const String&)(*bid) : params["id"];
    if (!id)
	return false;
    if (Client::valid() && update) {
	// Remember: directions are opposite of what the user expects
	const String& dir = params["direction"];
	bool outgoing = (dir == "incoming");
	if (outgoing || dir == "outgoing") {
	    // Skip if there is no remote party
	    const String& party = cdrRemoteParty(params,outgoing);
	    if (party) {
		NamedList p("");
		String time;
		Client::self()->formatDateTime(time,(unsigned int)params.getDoubleValue("time"),
		    "yyyy.MM.dd hh:mm",false);
		p.addParam("party",party);
		p.addParam("party_image",Client::s_skinPath + (outgoing ? "up.png" : "down.png"));
		p.addParam("time",time);
		time.clear();
		Client::self()->formatDateTime(time,(unsigned int)params.getDoubleValue("duration"),
		    "hh:mm:ss",true);
		p.addParam("duration",time);
		Client::self()->updateTableRow(s_logList,id,&p);
	    }
	}
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
    // Write to the file the information about the calls
    NamedList* sect = Client::s_history.createSection(id);
    if (!sect)
	return false;
    *sect = params;
    sect->assign(id);
    return Client::save(Client::s_history);
}

// Remove a call log item
bool DefaultLogic::callLogDelete(const String& billid)
{
    if (!billid)
	return false;
    bool ok = true;
    if (Client::valid())
	ok = Client::self()->delTableRow(s_logList,billid);
    NamedList* sect = Client::s_history.getSection(billid);
    if (!sect)
	return ok;
    Client::s_history.clearSection(*sect);
    return Client::save(Client::s_history) && ok;
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

// Make an outgoing call to a target picked from the call log
bool DefaultLogic::callLogCall(const String& billid)
{
    NamedList* sect = Client::s_history.getSection(billid);
    if (!sect)
	return false;
    const String& party = cdrRemoteParty(*sect);
    return party && Client::openConfirm("Call to '" + party + "'?",0,"callto:" + party);
}

// Create a contact from a call log entry
bool DefaultLogic::callLogCreateContact(const String& billid)
{
    NamedList* sect = Client::s_history.getSection(billid);
    if (!sect)
	return false;
    const String& party = cdrRemoteParty(*sect);
    NamedList p(party);
    p.setParam("target",party);
    return editContact(true,&p);
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
	Thread::idle();
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
	ok = Client::self()->delTableRow(name,msg.getValue("item"),wnd);
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
    if (msg["operation"] != "finalize")
	return false;
    if (!msg["chan"].startsWith("client/",false))
	return false;
    if (Client::self()->postpone(msg,Client::CallCdr,false))
	stopLogic = true;
    else
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
    if (Client::self()->postpone(msg,Client::UserNotify,false)) {
	stopLogic = true;
	return false;
    }
    const String& account = msg["account"];
    if (!account)
	return false;
    ClientAccount* acc = m_accounts->findAccount(account);
    if (!acc)
	return false;
    bool reg = msg.getBoolValue("registered");
    // Notify status
    String txt = reg ? "Registered" : "Unregistered";
    txt.append(acc->params().getValue("protocol")," ");
    txt << " account " << account;
    const String& reason = msg["reason"];
    txt.append(reason," reason: ");
    Client::self()->setStatusLocked(txt);
    int stat = ClientResource::Online;
    if (reg) {
	// Clear account register option
	NamedString* opt = acc->m_params.getParam("options");
	if (opt) {
	    ObjList* list = opt->split(',',false);
	    ObjList* o = list->find("register");
	    if (o) {
		o->remove();
		opt->clear();
		opt->append(list,",");
		if (opt->null())
		    acc->m_params.clearParam(opt);
		acc->save();
		// TODO: update account edit if displayed
	    }
	    TelEngine::destruct(list);
 	}
	acc->resource().m_id = msg.getValue("instance");
	// Add account to accounts selector(s)
	Client::self()->updateTableRow(s_account,account);
    }
    else {
	// Remove account from selector(s)
	Client::self()->delTableRow(s_account,account);
	if (msg.getBoolValue("autorestart"))
	    stat = ClientResource::Connecting;
	else {
	    stat = ClientResource::Offline;
	    // Reset resource name to configured
	    acc->resource().m_id = acc->m_params.getValue("resource");
	}
	clearAccountContacts(*acc);
    }
    bool changed = acc->resource().setStatus(stat);
    changed = acc->resource().setStatusText(reg ? String::empty() : reason) || changed;
    if (changed)
	updateAccountStatus(acc,m_accounts);
    return false;
}

// Process user.roster message
bool DefaultLogic::handleUserRoster(Message& msg, bool& stopLogic)
{
    if (!Client::valid() || Client::isClientMsg(msg))
	return false;
    const String& oper = msg["operation"];
    if (!oper)
	return false;
    bool remove = (oper != "update");
    if (remove && oper != "delete")
	return false;
    // Postpone message processing
    if (Client::self()->postpone(msg,Client::UserRoster)) {
	stopLogic = true;
	return false;
    }
    int n = msg.getIntValue("contact.count");
    if (n < 1)
	return false;
    const String& account = msg["account"];
    ClientAccount* a = account ? m_accounts->findAccount(account) : 0;
    if (!a)
	return false;
    ObjList removed;
    for (int i = 1; i <= n; i++) {
	String pref("contact." + String(i));
	const String& uri = msg[pref];
	if (!uri)
	    continue;
	String id;
	ClientContact::buildContactId(id,account,uri);
	ClientContact* c = a->findContact(id);
	// Avoid account's own contact
	if (c && c == a->contact())
	    continue;
	if (remove) {
	    if (!c)
		continue;
	    removed.append(a->removeContact(id,false));
	    continue;
	}
	pref << ".";
	// Add/update contact
	const char* cName = msg.getValue(pref + "name",uri);
	bool changed = (c == 0);
	if (c) {
	    changed = (c->m_name != cName);
	    if (changed)
		c->m_name = cName;
	}
	else {
	    c = a->appendContact(id,cName);
	    if (!c)
		continue;
	    c->setUri(uri);
	}
	const String& sub = msg[pref + "subscription"];
	if (c->m_subscription != sub) {
	    c->m_subscription = sub;
	    changed = true;
	}
	const String& grps = msg[pref + "groups"];
	if (grps) {
	    String oldGrp;
	    oldGrp.append(c->groups(),",");
	    changed = changed || oldGrp != grps;
	    c->groups().clear();
	    ObjList* list = grps.split(',',false);
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext())
		c->appendGroup(o->get()->toString());
	    TelEngine::destruct(list);
	}
	else if (c->groups().skipNull()) {
	    c->groups().clear();
	    changed = true;
	}
	if (!changed)
	    continue;
    }
    // Update UI
    for (ObjList* o = removed.skipNull(); o; o = o->skipNext())
	contactDeleted(*static_cast<ClientContact*>(o->get()));
    return true;
}

// Process resource.notify message
bool DefaultLogic::handleResourceNotify(Message& msg, bool& stopLogic)
{
    if (!Client::valid() || Client::isClientMsg(msg))
	return false;
    const String& contact = msg["contact"];
    if (!contact)
	return false;
    const String& oper = msg["operation"];
    if (!oper)
	return false;
    // Postpone message processing
    if (Client::self()->postpone(msg,Client::ResourceNotify)) {
	stopLogic = true;
	return false;
    }
    const String& account = msg["account"];
    ClientAccount* a = account ? m_accounts->findAccount(account) : 0;
    ClientContact* c = a ? a->findContactByUri(contact) : 0;
    if (!c)
	return false;
    const String& inst = msg["instance"];
    Debug(ClientDriver::self(),DebugAll,
	"Logic(%s) account=%s contact=%s instance=%s operation=%s",
	name().c_str(),account.c_str(),contact.c_str(),inst.safe(),oper.c_str());
    bool ownContact = c == a->contact();
    String instid;
    bool online = false;
    // Use a while() to break to the end
    while (true) {
	// Avoid account own instance
	if (ownContact && inst && inst == a->resource().toString())
	    return false;
	online = (oper == "online");
	if (online || oper == "offline") {
	    if (!c)
		break;
	    if (online) {
		if (!inst)
		    break;
		ClientResource* res = c->findResource(inst);
		if (!res)
		    res = new ClientResource(inst);
		// Update resource
		res->setAudio(msg.getBoolValue("caps.audio"));
		res->setPriority(msg.getIntValue("priority"));;
		res->setStatusText(msg.getValue("status"));
		int stat = ::lookup(msg.getValue("show"),ClientResource::s_statusName);
		if (stat < ClientResource::Online)
		    stat = ClientResource::Online;
		res->setStatus(stat);
		// (Re)insert the resource
		c->insertResource(res);
		// Update/set resource in contacts list (only for resources with audio caps)
		if (res->m_audio)
		    instid = inst;
	    }
	    else {
		if (inst)
		    c->removeResource(inst);
		else
		    c->resources().clear();
		// Remove resource(s) from contacts list
		c->buildInstanceId(instid,inst);
	    }
	    break;
	}
	// TODO: handle other operations like received errors
	break;
    }
    if (instid) {
	if (online)
	    updateContactList(*c,instid,msg.getValue("uri"));
	else
	    removeContacts(instid);
    }
    return false;
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
    NamedString* contact = msg.getParam("contact");
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
	Client::self()->delTableRow(s_channelList,id);
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

// Utility: set check parameter from another list
void setCheck(NamedList& p, const NamedList& src, const String& param, bool defVal = true)
{
    bool ok = src.getBoolValue(param,defVal);
    p.addParam("check:" + param,String::boolText(ok));
}

// Initialize client from settings
bool DefaultLogic::initializedClient()
{
    if (!Client::self())
	return false;

    NamedList dummy("client");
    NamedList* cSect = Client::s_settings.getSection("client");
    if (!cSect)
	cSect = &dummy;
    NamedList* cGen = Client::s_settings.getSection("general");
    if (!cGen)
	cGen = &dummy;

    // Account edit defaults
    NamedList p("");
    setCheck(p,*cSect,"acc_showadvanced");
    setCheck(p,*cSect,"acc_enabled");
    setCheck(p,*cSect,"acc_loginnow");
    Client::self()->setParams(&p);

    // Check if global settings override the users'
    bool globalOverride = Engine::config().getBoolValue("client","globaloverride",false);

    // Booleans
    for (unsigned int i = 0; i < Client::OptCount; i++) {
	bool tmp = Client::self()->getBoolOpt((Client::ClientToggle)i);
	bool active = true;
	if (globalOverride) {
	    String* over = Engine::config().getKey("client",Client::s_toggles[i]);
	    if (over) {
		tmp = over->toBoolean(tmp);
		active = false;
	    }
	    else
		tmp = cGen->getBoolValue(Client::s_toggles[i],tmp);
	}
	else {
	    tmp = Engine::config().getBoolValue("client",Client::s_toggles[i],tmp);
	    tmp = cGen->getBoolValue(Client::s_toggles[i],tmp);
	}
	Client::self()->setActive(Client::s_toggles[i],active);
	setClientParam(Client::s_toggles[i],String::boolText(tmp),false,true);
    }

    // Other string parameters
    setClientParam("username",Client::s_settings.getValue("default","username"),false,true);
    setClientParam("callerid",Client::s_settings.getValue("default","callerid"),false,true);
    setClientParam("domain",Client::s_settings.getValue("default","domain"),false,true);
    // Create default ring sound
    String ring = cGen->getValue("ringinfile",Client::s_soundPath + "ring.wav");
    Client::self()->createSound(Client::s_ringInName,ring);
    ring = cGen->getValue("ringoutfile",Client::s_soundPath + "tone.wav");
    Client::self()->createSound(Client::s_ringOutName,ring);

    // Enable call actions
    enableCallActions(m_selectedChannel);

    // Set chan.notify handler
    Client::self()->installRelay("chan.notify",Client::ChanNotify,100);
    return false;
}

// Client is exiting: save settings
void DefaultLogic::exitingClient()
{
    clearDurationUpdate();

    if (!Client::valid())
	return;

    // Hide some windows to avoid displying them the next time we start
    Client::self()->setVisible(s_wndAccount,false);
    if (Client::self()->getVisible(s_wndAddrbook))
	Client::self()->setVisible(s_wndAddrbook,false);
    else
	// Avoid open account add the next time we start if the user closed the window
	setClientParam(Client::s_toggles[Client::OptAddAccountOnStartup],
	    String(false),true,false);

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
	NamedList* sect = Client::s_calltoHistory.createSection("calls");
	sect->clearParams();
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

// Engine start notification. Connect startup accounts
void DefaultLogic::engineStart(Message& msg)
{
    ObjList* o = m_accounts->accounts().skipNull();
    if (o) {
	for (; o; o = o->skipNext()) {
	    ClientAccount* a = static_cast<ClientAccount*>(o->get());
	    if (a->resource().offline() && a->startup())
		loginAccount(a->params(),true);
	}
    }
    else if (Client::valid() &&
	Client::self()->getBoolOpt(Client::OptAddAccountOnStartup)) {
	// Add account
	editAccount(true,0);
    }
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

// Fill contact edit/delete active parameters
void DefaultLogic::fillContactEditActive(NamedList& list, bool active, const String* item)
{
    if (active) {
	if (!Client::self())
	    return;
	if (!Client::self()->getVisible(s_wndAddrbook)) {
	    ClientContact* c = 0;
	    if (item)
		c = !item->null() ? m_accounts->findContactByInstance(*item) : 0;
	    else {
		String sel;
		Client::self()->getSelect(s_contactList,sel);
		c = sel ? m_accounts->findContactByInstance(sel) : 0;
	    }
	    active = c && m_accounts->isLocalContact(c);
	}
	else
	    active = false;
    }
    const char* ok = String::boolText(active);
    list.addParam("active:abk_del",ok);
    list.addParam("active:abk_edit",ok);
}

// Fill log contact active parameter
void DefaultLogic::fillLogContactActive(NamedList& list, bool active, const String* item)
{
    if (active) {
	if (!Client::self())
	    return;
	if (!Client::self()->getVisible(s_wndAddrbook)) {
	    if (item)
		active = !item->null();
	    else {
		String sel;
		active = Client::self()->getSelect(s_logList,sel) && sel;
	    }
	}
	else
	    active = false;
    }
    list.addParam("active:log_contact",String::boolText(active));
}


// Clear a list/table. Handle specific lists like CDR, accounts, contacts
bool DefaultLogic::clearList(const String& action, Window* wnd)
{
    if (!(Client::valid() && action))
	return false;
    // Check for a confirmation text
    int pos = action.find(":");
    String list;
    if (pos > 0)
	list = action.substr(0,pos);
    else if (pos < 0)
	list = action;
    if (!list)
	return false;
    if (pos > 0) {
	String text = action.substr(pos + 1);
	if (!text) {
	    // Handle some known lists
	    if (list == s_logList)
		text = "Clear call history?";
	}
	if (text)
	    return Client::openConfirm(text,wnd,"clear:" + list);
    }
    DDebug(ClientDriver::self(),DebugAll,"DefaultLogic::clearList(%s,%p)",
	list.c_str(),wnd);
    // Handle CDR
    if (list == s_logList)
	return callLogClear(s_logList,String::empty());
    bool ok = Client::self()->clearTable(list,wnd) || Client::self()->setText(list,"",false,wnd);
    if (ok)
	Client::self()->setFocus(list,false,wnd);
    return ok;
}

// Delete a list/table item. Handle specific lists like CDR
bool DefaultLogic::deleteItem(const String& list, const String& item, Window* wnd)
{
    if (!(Client::valid() && list && item))
	return false;
    DDebug(ClientDriver::self(),DebugAll,"DefaultLogic::deleteItem(%s,%s,%p)",
	list.c_str(),item.c_str(),wnd);
    // Handle known lists
    if (list == s_contactList)
	return delContact(item,wnd);
    if (list == s_accountList)
	return delAccount(item,wnd);
    if (list == s_logList)
	return callLogDelete(item);
    // Remove table row
    return Client::self()->delTableRow(list,item,wnd);
}

// Handle list/table selection deletion
bool DefaultLogic::deleteSelectedItem(const String& action, Window* wnd)
{
    if (!Client::valid())
	return false;
    DDebug(ClientDriver::self(),DebugAll,"DefaultLogic::deleteSelectedItem(%s,%p)",
	action.c_str(),wnd);
    // Check for a confirmation text
    int pos = action.find(":");
    String list;
    if (pos > 0)
	list = action.substr(0,pos);
    else if (pos < 0)
	list = action;
    if (!list)
	return false;
    String item;
    Client::self()->getSelect(list,item,wnd);
    if (!item)
	return false;
    if (pos > 0) {
	String text = action.substr(pos + 1);
	if (!text) {
	    // Handle some known lists
	    if (list == s_logList)
		text = "Delete the selected call log?";
	    else if (list == s_accountList)
		text << "Delete account '" + item + "'?";
	    else if (list == s_contactList) {
		ClientContact* c = m_accounts->findContactByInstance(item);
		if (!(c && m_accounts->isLocalContact(c)))
		    return false;
		text << "Delete contact '" << c->m_name + "'?";
	    }
	}
	if (text)
	    return Client::openConfirm(text,wnd,"deleteitem:" + list + ":" + item);
    }
    return deleteItem(list,item,wnd);
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
