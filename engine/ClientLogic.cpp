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

namespace TelEngine {

// A client wizard
class ClientWizard : public String
{
public:
    ClientWizard(const String& wndName, ClientAccountList* accounts, bool temp = false);
    // Check if a given window is the wizard
    inline bool isWindow(Window* w)
	{ return w && w->id() == toString(); }
    // Retrieve the wizard window
    inline Window* window() const
	{ return Client::valid() ? Client::self()->getWindow(toString()) : 0; }
    // Retrieve the account
    inline ClientAccount* account()
	{ return (m_accounts && m_account) ? m_accounts->findAccount(m_account) : 0; }
    // Start the wizard. Show the window
    virtual void start() {
	    reset(true);
	    changePage(String::empty());
	    Client::self()->setVisible(toString(),true,true);
	}
    virtual void reset(bool full)
	{}
    // Handle actions from wizard window. Return true if handled
    virtual bool action(Window* w, const String& name, NamedList* params = 0);
    // Handle checkable widgets status changes in wizard window
    // Return true if handled
    virtual bool toggle(Window* w, const String& name, bool active);
    // Handle selection changes notifications. Return true if handled
    virtual bool select(Window* w, const String& name, const String& item,
	const String& text = String::empty())
	{ return false; }
    // Handle user.notify messages. Restart the wizard if the operating account is offline
    // Return true if handled
    virtual bool handleUserNotify(const String& account, bool ok, const char* reason = 0);
    // Widgets
    static const String s_pagesWidget;   // Wizard pages UI widget
    static const String s_actionNext;    // The name of the 'next' action
    static const String s_actionPrev;    // The name of the 'previous' action
    static const String s_actionCancel;  // The name of the 'cancel async operation' action
protected:
    virtual void onNext()
	{}
    virtual void onPrev()
	{}
    virtual void onCancel()
	{}
    // Wizard window visibility changed notification.
    virtual void windowVisibleChanged(bool visible) {
	    if (!visible)
		reset(true);
	}
    // Retrieve the current page from UI
    inline void currentPage(String& page) const {
	    Window* w = window();
	    if (w)
		Client::self()->getSelect(s_pagesWidget,page,w);
	}
    // Check if a given page is the current one
    inline bool isCurrentPage(const String& page) const {
	    String p;
	    currentPage(p);
	    return p && p == page;
	}
    // Retrieve the selected account
    ClientAccount* account(const String& list);
    // Update wizard actions active status
    void updateActions(NamedList& p, bool canPrev, bool canNext, bool canCancel);
    // Change the wizard page
    virtual bool changePage(const String& page, const String& old = String::empty())
	{ return false; }

    ClientAccountList* m_accounts;       // The list of accounts if needed
    String m_account;                    // The account used by the wizard
    bool m_temp;                         // Wizard window is a temporary one
};

// New account wizard
// The accounts list object is not owned by the wizard
class AccountWizard : public ClientWizard
{
public:
    inline AccountWizard(ClientAccountList* accounts)
	: ClientWizard("accountwizard",accounts)
	{}
    ~AccountWizard()
	{ reset(true); }
    virtual void reset(bool full);
    virtual bool handleUserNotify(const String& account, bool ok, const char* reason = 0);
protected:
    virtual void onNext();
    virtual void onPrev();
    virtual void onCancel();
    virtual bool changePage(const String& page, const String& old = String::empty());
};

// Join MUC room wizard
// The accounts list object is not owned by the wizard
class JoinMucWizard : public ClientWizard
{
public:
    // Build a join MUC wizard. Show the join page if temporary
    JoinMucWizard(ClientAccountList* accounts, NamedList* tempParams = 0);
    ~JoinMucWizard()
	{ reset(true); }
    // Start the wizard. Show the window
    virtual void start(bool add = false);
    virtual void reset(bool full);
    // Handle actions from wizard window. Return true if handled
    virtual bool action(Window* w, const String& name, NamedList* params = 0);
    // Handle selection changes notifications. Return true if handled
    virtual bool select(Window* w, const String& name, const String& item,
	const String& text = String::empty());
    // Handle checkable widgets status changes in wizard window
    // Return true if handled
    virtual bool toggle(Window* w, const String& name, bool active);
    // Process contact.info message
    bool handleContactInfo(Message& msg, const String& account, const String& oper,
	const String& contact);
    // Handle user.notify messages. Update the accounts list
    virtual bool handleUserNotify(const String& account, bool ok, const char* reason = 0);
protected:
    virtual void onNext();
    virtual void onPrev();
    virtual void onCancel();
    virtual bool changePage(const String& page, const String& old = String::empty());
    // Handle the join room action
    void joinRoom();
    // Retrieve the selected MUC server if not currently requesting one
    bool selectedMucServer(String* buf = 0);
    // Set/reset servers query
    void setQuerySrv(bool on, const char* domain = 0);
    // Set/reset rooms query
    void setQueryRooms(bool on, const char* domain = 0);
    // Update UI progress params
    void addProgress(NamedList& dest, bool on, const char* target);
    // Update 'next' button status on select server page
    void updatePageMucServerNext();
private:
    bool m_add;
    bool m_queryRooms;                   // Requesting rooms from server
    bool m_querySrv;                     // Requesting MUC server(s)
    ObjList m_requests;                  // Info/items requests id
    String m_lastPage;                   // Last visited page
};

// Class holding an account status item and
// global account status data (the list of available status items)
class AccountStatus : public String
{
public:
    inline AccountStatus(const char* name)
	: String(name), m_status(ClientResource::Offline)
	{}
    inline int status() const
	{ return m_status; }
    inline const String& text() const
	{ return m_text; }
    // Retrieve current status item
    static inline AccountStatus* current()
	{ return s_current; }
    // Find an item
    static inline AccountStatus* find(const String& name) {
	    ObjList* o = s_items.find(name);
	    return o ? static_cast<AccountStatus*>(o->get()) : 0;
	}
    // Change the current item. Save to config if changed
    // Return true if an item with the given name was found
    static bool setCurrent(const String& name);
    // Append/set an item. Save to config if changed
    static void set(const String& name, int stat, const String& text, bool save = false);
    // Load the list from config
    static void load();
    // Initialize the list
    static void init();
    // Update current status in UI
    static void updateUi();
private:
    static ObjList s_items;              // Items
    static AccountStatus* s_current;     // Current status
    int m_status;
    String m_text;
};

// This class holds a pending request sent by the client
class PendingRequest : public String
{
public:
    inline PendingRequest(const char* id, const String& account, const String& target)
	: String(id), m_account(account), m_target(target),
	m_mucServer(false), m_mucRooms(false)
	{}
    // Find an item
    static inline PendingRequest* find(const String& name) {
	    ObjList* o = s_items.find(name);
	    return o ? static_cast<PendingRequest*>(o->get()) : 0;
	}
    // Remove all account's requests
    static void clear(const String& account);
    // Request info/items from target
    static PendingRequest* request(bool info, ClientAccount* acc, const String& target,
	bool mucserver);
    // Request MUC rooms from target
    static bool requestMucRooms(ClientAccount* acc, const String& target);
    static ObjList s_items;
    String m_account;                    // The account
    String m_target;                     // Request target
    bool m_mucServer;                    // True if we are searching for MUC services
    bool m_mucRooms;                     // True if we are serching for MUC rooms
};

// Chat state notificator
// This class is not thread safe. Data MUST be changed from client's thread
class ContactChatNotify : public String
{
public:
    enum State {
	None = 0,
	Active,
	Composing,
	Paused,
	Inactive,
    };
    // Update timers
    inline void updateTimers(const Time& time) {
	    m_paused = time.msec() + s_pauseInterval;
	    m_inactive = time.msec() + s_inactiveInterval;
	}
    // Check for timeout. Reset the timer if a notification is returned
    State timeout(Time& time);
    // Send the notification
    static void send(State state, ClientContact* c, MucRoom* room, MucRoomMember* member);
    // Add or remove items from list. Notify active/composing if changed
    // Don't notify active if empty and 'notify' is false
    static void update(ClientContact* c, MucRoom* room, MucRoomMember* member,
	bool empty, bool notify = true);
    // Check timeouts. Send notifications
    static bool checkTimeouts(ClientAccountList& list, Time& time);
    // Clear list
    static inline void clear()
	{ s_items.clear(); }
    // State names
    static const TokenDict s_states[];
private:
    inline ContactChatNotify(const String& id, bool mucRoom, bool mucMember,
	const Time& time = Time())
	: String(id), m_mucRoom(mucRoom), m_mucMember(mucMember),
	m_paused(0), m_inactive(0)
	{ updateTimers(time); }
    static u_int64_t s_pauseInterval;    // Interval to send paused notification
    static u_int64_t s_inactiveInterval;     // Interval to send gone notification
    static ObjList s_items;              // Item list
    bool m_mucRoom;                      // Regular contact or muc room
    bool m_mucMember;                    // Room member
    u_int64_t m_paused;                  // Time to send paused
    u_int64_t m_inactive;                    // Time to send gone
};

}; // namespace TelEngine

using namespace TelEngine;

// Windows
static const String s_wndMain = "mainwindow";           // mainwindow
static const String s_wndAccount = "account";           // Account edit/add
static const String s_wndAddrbook = "addrbook";         // Contact edit/add
static const String s_wndChatContact = "chatcontact";   // Chat contact edit/add
static const String s_wndMucInvite = "mucinvite";       // MUC invite
static const String s_wndAcountList = "accountlist";    // Accounts list
static const String s_wndFileTransfer = "fileprogress"; // File transfer
// Some UI widgets
static const String s_mainwindowTabs = "mainwindowTabs";
static const String s_channelList = "channels";
static const String s_accountList = "accounts";         // Global accounts list
static const String s_contactList = "contacts";
static const String s_logList = "log";
static const String s_calltoList = "callto";
static const String s_account = "account";               // Account selector
static const String s_chatAccount = "chataccount";       // List of chat accounts
static const String s_chatContactList = "chat_contacts"; // List of chat contacts
static const String s_mucAccounts = "mucaccount";        // List of accounts supporting MUC
static const String s_mucSavedRooms = "mucsavedrooms";   // List of saved MUC rooms
static const String s_mucMembers = "muc_members";        // List of MUC room members
static const String s_accProtocol = "acc_protocol";         // List of protocols in account add/edit
static const String s_accWizProtocol = "accwiz_protocol";   // List of protocols in account wizard
static const String s_accProviders = "acc_providers";       // List of providers in account add/edit
static const String s_accWizProviders = "accwiz_providers"; // List of providers in account wizard
static const String s_inviteContacts = "invite_contacts";   // List of contacts in muc invite
static const String s_fileProgressList = "fileprogresslist";  // List of file transfers
// Actions
static const String s_actionShowCallsList = "showCallsList";
static const String s_actionShowNotification = "showNotification";
static const String s_actionShowInfo = "showNotificationInfo";
static const String s_actionPendingChat = "showPendingChat";
static const String s_actionCall = "call";
static const String s_actionAnswer = "answer";
static const String s_actionHangup = "hangup";
static const String s_actionTransfer = "transfer";
static const String s_actionConf = "conference";
static const String s_actionHold = "hold";
static const String s_actionLogin = "acc_login";
static const String s_actionLogout = "acc_logout";
static const String s_chat = "chatcontact_chat";
static const String s_chatCall = "chatcontact_call";
static const String s_chatNew = "chatcontact_new";
static const String s_chatRoomNew = "chatroom_new";
static const String s_chatShowLog = "chatcontact_showlog";
static const String s_chatEdit = "chatcontact_edit";
static const String s_chatDel = "chatcontact_del";
static const String s_chatInfo = "chatcontact_info";
static const String s_chatSub = "chatcontact_subscribe";
static const String s_chatUnsubd = "chatcontact_unsubscribed";
static const String s_chatUnsub = "chatcontact_unsubscribe";
static const String s_chatShowOffline = "chatcontact_showoffline";
static const String s_chatFlatList = "chatcontact_flatlist";
static const String s_chatSend = "send_chat";
static const String s_fileSend = "send_file";
static const String s_fileSendPrefix = "send_file:";
static const String s_mucJoin = "room_join";
static const String s_mucChgSubject = "room_changesubject";
static const String s_mucChgNick = "room_changenick";
static const String s_mucSave = "room_save";
static const String s_mucInvite = "room_invite_contacts";
static const String s_mucPrivChat = "room_member_chat";
static const String s_mucKick = "room_member_kick";
static const String s_mucBan = "room_member_ban";
static const String s_mucRoomShowLog = "room_showlog";
static const String s_mucMemberShowLog = "room_member_showlog";
static const String s_storeContact = "storecontact";
static const String s_mucInviteAdd = "invite_add";
// Not selected string(s)
static String s_notSelected = "-none-";
// Maximum number of call log entries
static unsigned int s_maxCallHistory = 20;
// Global account status
ObjList AccountStatus::s_items;
AccountStatus* AccountStatus::s_current = 0;
// Pending requests
ObjList PendingRequest::s_items;
// Client wizard
const String ClientWizard::s_pagesWidget = "pages";
const String ClientWizard::s_actionNext = "next";
const String ClientWizard::s_actionPrev = "prev";
const String ClientWizard::s_actionCancel = "cancel";
// Wizards managed by the default logic
static AccountWizard* s_accWizard = 0;
static JoinMucWizard* s_mucWizard = 0;
// Chat state notificator
const TokenDict ContactChatNotify::s_states[] = {
    {"active",    Active},
    {"composing", Composing},
    {"paused",    Paused},
    {"inactive",  Inactive},
    {0,0}
};
u_int64_t ContactChatNotify::s_pauseInterval = 30000;     // Paused notification
u_int64_t ContactChatNotify::s_inactiveInterval = 300000; // Inactive notification
ObjList ContactChatNotify::s_items;           // Item list
// ClientLogic
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
// Common account parameters (protocol independent)
static const String s_accParams[] = {
    "username", "password", ""
};
// Common account boolean parameters (protocol independent)
static const String s_accBoolParams[] = {
    "savepassword", ""
};
// Account protocol dependent parameters
static const String s_accProtoParams[] = {
    "server", "domain", "outbound", "options", "resource", "port", "interval",
    "authname", ""
};
// Account protocol dependent parameters set in lists (param=default_value)
static NamedList s_accProtoParamsSel("");
// Resource status images
static const TokenDict s_statusImage[] = {
    {"status_offline.png",   ClientResource::Offline},
    {"status_connecting.png",ClientResource::Connecting},
    {"status_online.png",    ClientResource::Online},
    {"status_busy.png",      ClientResource::Busy},
    {"status_dnd.png",       ClientResource::Dnd},
    {"status_away.png",      ClientResource::Away},
    {"status_xa.png",        ClientResource::Xa},
    {0,0}
};
// Saved rooms
static Configuration s_mucRooms;
// Actions from notification area
enum PrivateNotifAction {
    PrivNotificationOk = 1,
    PrivNotificationReject,
    PrivNotificationLogin,
    PrivNotificationAccEdit,
    PrivNotificationAccounts,
    PrivNotification1,
    PrivNotification2,
    PrivNotification3,
};
static const TokenDict s_notifPrefix[] = {
    {"messages_ok:",          PrivNotificationOk},
    {"messages_reject:",      PrivNotificationReject},
    {"messages_login:",       PrivNotificationLogin},
    {"messages_acc_edit:",    PrivNotificationAccEdit},
    {"messages_accounts:",    PrivNotificationAccounts},
    {"messages_1:",           PrivNotification1},
    {"messages_2:",           PrivNotification2},
    {"messages_3:",           PrivNotification3},
    {0,0,}
};
enum ChatLogEnum {
    ChatLogSaveAll = 1,
    ChatLogSaveUntilLogout,
    ChatLogNoSave
};
// Archive save data
const TokenDict s_chatLogDict[] = {
    {"chat_save_all",         ChatLogSaveAll},
    {"chat_save_untillogout", ChatLogSaveUntilLogout},
    {"chat_nosave",           ChatLogNoSave},
    {0,0}
};
static ChatLogEnum s_chatLog = ChatLogSaveAll;
// Temporary wizards
static ObjList s_tempWizards;
// Chat state templates
static NamedList s_chatStates("");
// Changing docked chat state
static bool s_changingDockedChat = false;
// Pending chat items managed in the client's thread
static ObjList s_pendingChat;
// Google MUC domain
static const String s_googleMucDomain = "groupchat.google.com";
// Miscellaneous
static const String s_jabber = "jabber";
static const String s_sip = "sip";
static const String s_gmailDomain = "gmail.com";
static const String s_googleDomain = "google.com";
static const String s_fileOpenSendPrefix = "send_fileopen:";
static const String s_fileOpenRecvPrefix = "recv_fileopen:";
static String s_lastFileDir;             // Last directory used to send/recv file
static String s_lastFileFilter;          // Last filter used to pick a file to send

// Dump a list of parameters to output if XDEBUG is defined
static inline void dumpList(const NamedList& p, const char* text, Window* w = 0)
{
#ifdef XDEBUG
    String tmp;
    p.dump(tmp,"\r\n");
    String wnd;
    if (w)
	wnd << " window=" << w->id();
    Debug(ClientDriver::self(),DebugInfo,"%s%s\r\n-----\r\n%s\r\n-----",text,wnd.safe(),tmp.safe());
#endif
}

// Check reason and error for auth failure texts
static bool isNoAuth(const String& reason, const String& error)
{
    static const String s_noAuth[] = {"noauth", "not-authorized", "invalid-authzid", ""};
    for (int i = 0; s_noAuth[i]; i++)
	if (reason == s_noAuth[i] || error == s_noAuth[i])
	    return true;
    return false;
}

// Split user@domain
static inline void splitContact(const String& contact, String& user, String& domain)
{
    int pos = contact.find('@');
    if (pos >= 0) {
	user = contact.substr(0,pos);
	domain = contact.substr(pos + 1);
    }
    else
	domain = contact;
}

// Utility: check if a string changed, set it, return true if changed
static inline bool setChangedString(String& dest, const String& src)
{
    if (dest == src)
	return false;
    dest = src;
    return true;
}

// Utility: check if a list parametr changed, set it, return true if changed
static inline bool setChangedParam(NamedList& dest, const String& param,
    const String& src)
{
    String* exist = dest.getParam(param);
    if (exist)
	return setChangedString(*exist,src);
    dest.addParam(param,src);
    return true;
}

// Append failure reason/error to a string
static void addError(String& buf, NamedList& list)
{
    String* error = list.getParam(YSTRING("error"));
    String* reason = list.getParam(YSTRING("reason"));
    if (TelEngine::null(error)) {
	if (TelEngine::null(reason))
	    return;
	error = reason;
	reason = 0;
    }
    buf.append(*error,": ");
    if (!TelEngine::null(reason))
	buf << " (" << *reason << ")";
}

// Build contact name: name <uri>
static inline void buildContactName(String& buf, ClientContact& c)
{
    buf = c.m_name;
    if (c.m_name != c.uri())
	buf << " <" << c.uri() << ">";
}

// Compare list parameters given in array
// Return true if equal
static bool sameParams(const NamedList& l1, const NamedList& l2, const String* params)
{
    if (!params)
	return false;
    while (*params && l1[*params] == l2[*params])
	params++;
    return params->null();
}

// Compare list parameters given in NamedList
// Return true if equal
static bool sameParams(const NamedList& l1, const NamedList& l2, const NamedList& params)
{
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());)
	if (l1[ns->name()] != l2[ns->name()])
	    return false;
    return true;
}

// Build an user.login message
// Clear account password if not saved
static Message* userLogin(ClientAccount* a, bool login)
{
    if (!a)
	return 0;
    Message* m = a->userlogin(login);
    if (login && !a->params().getBoolValue(YSTRING("savepassword")))
	a->m_params.clearParam(YSTRING("password"));
    return m;
}

// Retrieve a contact or MUC room from name:id.
// For MUC rooms the id is assumed to be a member id.
// Return true if the prefix was found
static bool getPrefixedContact(const String& name, const String& prefix, String& id,
    ClientAccountList* list, ClientContact** c, MucRoom** room)
{
    if (!(list && (room || c)))
	return false;
    int pos = name.find(':');
    if (pos < 0 || name.substr(0,pos) != prefix)
	return false;
    id = name.substr(pos + 1);
    if (c)
	*c = list->findContact(id);
    if (!(c && *c) && room)
	*room = list->findRoomByMember(id);
    return true;
}

// Check if a protocol is a telephony one
// FIXME: find a better way to detect it
static inline bool isTelProto(const String& proto)
{
    return proto != s_jabber;
}

// Check if a given account is a gmail one
static inline bool isGmailAccount(ClientAccount* acc)
{
    if (!(acc && acc->contact()))
	return false;
    return (acc->contact()->uri().getHost() &= s_gmailDomain) ||
	(acc->contact()->uri().getHost() &= s_googleDomain);
}

// Check if a given domain is a Google MUC server
static inline bool isGoogleMucDomain(const String& domain)
{
    return (domain &= s_googleMucDomain);
}

// Retrieve protocol specific page name in UI
static const String& getProtoPage(const String& proto)
{
    static const String s_default = "default";
    static const String s_none = "none";
    if (proto == s_jabber)
	return s_jabber;
    if (proto == s_sip)
	return s_sip;
    if (proto)
	return s_default;
    return s_none;
}

// Show a confirm dialog box in a given window
static bool showInput(Window* wnd, const String& name, const char* text,
    const char* context, const char* title, const char* input = 0)
{
    if (!(Client::valid() && name))
	return false;
    NamedList p("");
    p.addParam("inputdialog_text",text);
    p.addParam("inputdialog_input",input);
    p.addParam("property:" + name + ":_yate_context",context);
    return Client::self()->createDialog(YSTRING("input"),wnd,title,name,&p);
}

// Show a confirm dialog box in a given window
static bool showConfirm(Window* wnd, const char* text, const char* context)
{
    static const String name = "confirm_dialog";
    if (!Client::valid())
	return false;
    NamedList p("");
    p.addParam("text",text);
    p.addParam("property:" + name + ":_yate_context",context);
    return Client::self()->createDialog(YSTRING("confirm"),wnd,String::empty(),name,&p);
}

// Show an error dialog box in a given window
// Return false to simplify code
static bool showError(Window* wnd, const char* text)
{
    static const String name = "error_dialog";
    if (!Client::valid())
	return false;
    NamedList p("");
    p.addParam("text",text);
    Client::self()->createDialog(YSTRING("message"),wnd,String::empty(),"error_dialog",&p);
    return false;
}

// Show an error dialog box in a given window
static inline bool showAccDupError(Window* wnd)
{
    return showError(wnd,"Another account with the same protocol, username and host already exists!");
}

// Show select account error dialog box in a given window
static inline bool showAccSelect(Window* wnd)
{
    return showError(wnd,"You must choose an account");
}

// Show a duplicate room error dialog box in a given window
static inline bool showRoomDupError(Window* wnd)
{
    return showError(wnd,"A chat room with the same username and server already exist!");
}

// Check text changes for user@domain
// Username changes: find '@', set domain if found
static bool checkUriTextChanged(Window* w, const String& sender, const String& text,
    const String& usrName, const String& dName = String::empty())
{
    if (sender != usrName)
	return false;
    int pos = text.find('@');
    if (pos >= 0) {
	NamedList p("");
	p.addParam(usrName,text.substr(0,pos));
	if (dName) {
	    String d = text.substr(pos + 1);
	    if (d) {
		String tmp;
		if (Client::self()->getText(dName,tmp,false,w) && !tmp) {
		    p.addParam(dName,d);
		    p.addParam("focus:" + dName,String::boolText(false));
		}
	    }
	}
	Client::self()->setParams(&p,w);
    }
    return true;
}

// Check a room chat at groupchat.google.com
// Show an error if invalid
static bool checkGoogleRoom(const String& contact, Window* w = 0)
{
    String room;
    String domain;
    splitContact(contact,room,domain);
    if (!isGoogleMucDomain(domain))
	return true;
    if (room.startSkip("private-chat-",false) && Client::s_guidRegexp.matches(room))
	return true;
    String text;
    text << "Invalid room '" << contact << "' for this domain!";
    text << "\r\nThe format must be private-chat-8*HEX-4*HEX-4*HEX-4*HEX-12*HEX";
    text << "\r\nE.g. private-chat-1a34561f-2d34-1111-dF23-29adc0347418";
    if (w)
	showError(w,text);
    else
	Client::openMessage(text);
    return false;
}

// Check an URI read from UI
// Show an error if invalid
static bool checkUri(Window* w, const String& user, const String& domain, bool muc)
{
    String text;
    if (user) {
	if (user.find('@') < 0) {
	    if (domain) {
		if (domain.find('@') >= 0)
		    text << "Invalid domain";
	    }
	    else
		text << "Domain can't be empty";
	}
	else
	    text << "Invalid " << (muc ? "room id" : "username");
    }
    else
	text << (muc ? "Room id" : "Username") << " can't be empty";
    if (text) {
	showError(w,text);
	return false;
    }
    if (!muc)
	return true;
    return checkGoogleRoom(user + "@" + domain,w);
}

// Retrieve resource status image with path
static inline String resStatusImage(int stat)
{
    const char* img = lookup(stat,s_statusImage);
    if (img)
	return Client::s_skinPath + img;
    return String();
}

// Retrieve the status of a contact
static inline int contactStatus(ClientContact& c)
{
    ClientResource* res = c.status();
    if (res)
	return res->m_status;
    return c.online() ? ClientResource::Online : ClientResource::Offline;
}

// Set the image parameter of a list
static inline void setImageParam(NamedList& p, const char* param,
    const char* image)
{
    static const String suffix = "_image";
    p.setParam(param + suffix,Client::s_skinPath + image);
}

// Set a list parameter and it's image
static inline void setImageParam(NamedList& p, const char* param,
    const char* value, const char* image)
{
    p.setParam(param,value);
    setImageParam(p,param,image);
}

// Select a single item in a list containing exactly 1 item not
// matching s_notSelected
// Select the last item otherwise if selLast is true
static bool selectListItem(const String& name, Window* w, bool selLast = true,
    bool selNotSelected = true)
{
    NamedList p("");
    Client::self()->getOptions(name,&p,w);
    NamedString* sel = 0;
    unsigned int n = p.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = p.getParam(i);
	if (!ns || Client::s_notSelected.matches(ns->name()))
	    continue;
	if (!sel || selLast)
	    sel = ns;
	else {
	    sel = 0;
	    break;
	}
    }
    if (sel)
	return Client::self()->setSelect(name,sel->name(),w);
    return selNotSelected && Client::self()->setSelect(name,s_notSelected,w);
}

// Build a parameter list used to update an item in notification area
static inline void buildNotifAreaId(String& id, const char* itemType, const String& account,
    const String& contact = String::empty())
{
    id = itemType;
    ClientContact::buildContactId(id,account,contact);
}

// Build a parameter list used to update an item in notification area
static NamedList* buildNotifArea(NamedList& list, const char* itemType, const String& account,
    const String& contact, const char* title = 0, const char* extraParams = 0)
{
    String id;
    buildNotifAreaId(id,itemType,account,contact);
    NamedList* upd = new NamedList(id);
    list.addParam(new NamedPointer(id,upd,String::boolText(true)));
    upd->addParam("item_type",itemType);
    upd->addParam("account",account);
    upd->addParam("contact",contact,false);
    upd->addParam("title",title,false);
    String params("item_type,account,contact,title");
    params.append(extraParams,",");
    upd->addParam("_yate_itemparams",params);
    return upd;
}

// Show/hide a button in generic notification. Set its title also
static inline void setGenericNotif(NamedList& list, int index, const char* title)
{
    String name;
    name << "messages_" << index;
    list.addParam("show:" + name,String::boolText(!TelEngine::null(title)));
    list.addParam(name,title);
}

// Customize buttons in generic notification
static void setGenericNotif(NamedList& list, const char* title1 = 0,
    const char* title2 = 0, const char* title3 = 0)
{
    setGenericNotif(list,1,title1);
    setGenericNotif(list,2,title2);
    setGenericNotif(list,3,title3);
}

// Remove a notification area account/contact item
static inline void removeNotifArea(const char* itemType, const String& account,
    const String& contact = String::empty(), Window* wnd = 0)
{
    String id;
    buildNotifAreaId(id,itemType,account,contact);
    Client::self()->delTableRow(YSTRING("messages"),id,wnd);
}

// Remove all notifications belonging to an account
static void removeAccNotifications(ClientAccount* acc)
{
    if (!acc)
	return;
    const String& account = acc->toString();
    removeNotifArea("loginfail",account);
    removeNotifArea("rosterreqfail",account);
}

// Request to the client to log a chat entry
static bool logChat(ClientContact* c, unsigned int time, bool send, bool delayed,
    const String& body, bool roomChat = true, const String& nick = String::empty())
{
    if (!c)
	return false;
    if (s_chatLog != ChatLogSaveAll && s_chatLog != ChatLogSaveUntilLogout)
	return false;
    if (!Client::self())
	return false;
    MucRoom* room = c->mucRoom();
    NamedList p("");
    p.addParam("account",c->accountName());
    p.addParam("contact",c->uri());
    if (!room) {
	p.addParam("contactname",c->m_name);
	p.addParam("sender",send ? "" : c->m_name.c_str());
    }
    else {
	p.addParam("muc",String::boolText(true));
	p.addParam("roomchat",String::boolText(roomChat));
	p.addParam("contactname",roomChat ? room->resource().m_name : nick);
	p.addParam("sender",send ? "" : nick.c_str());
    }
    p.addParam("time",String(time));
    p.addParam("send",String::boolText(send));
    if (!send && delayed)
	p.addParam("delayed",String::boolText(true));
    p.addParam("text",body);
    return Client::self()->action(0,YSTRING("archive:logchat"),&p);
}

// Show contact archive log
static bool logShow(ClientContact* c, bool roomChat = true,
    const String& nick = String::empty())
{
    if (!(c && Client::self()))
	return false;
    MucRoom* room = c->mucRoom();
    NamedList p("");
    p.addParam("account",c->accountName());
    p.addParam("contact",c->uri());
    if (room) {
	p.addParam("muc",String::boolText(true));
	p.addParam("roomchat",String::boolText(roomChat));
	p.addParam("contactname",nick,false);
    }
    return Client::self()->action(0,YSTRING("archive:showchat"),&p);
}

// Close archive session
static bool logCloseSession(ClientContact* c, bool roomChat = true,
    const String& nick = String::empty())
{
    if (!(c && Client::self()))
	return false;
    MucRoom* room = c->mucRoom();
    NamedList p("");
    p.addParam("account",c->accountName());
    p.addParam("contact",c->uri());
    if (room) {
	p.addParam("muc",String::boolText(true));
	p.addParam("roomchat",String::boolText(roomChat));
	p.addParam("contactname",nick,false);
    }
    return Client::self()->action(0,YSTRING("archive:closechatsession"),&p);
}

// Clear an account's log
static bool logClearAccount(const String& account)
{
    if (!Client::self())
	return false;
    NamedList p("");
    p.addParam("account",account);
    return Client::self()->action(0,YSTRING("archive:clearaccountnow"),&p);
}

// Close all MUC log sessions of a room
static void logCloseMucSessions(MucRoom* room)
{
    if (!room)
	return;
    Window* w = room->getChatWnd();
    if (w) {
	NamedList p("");
	Client::self()->getOptions(ClientContact::s_dockedChatWidget,&p,w);
	unsigned int n = p.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = p.getParam(i);
	    if (!(ns && ns->name()))
		continue;
	    MucRoomMember* m = room->findMemberById(ns->name());
	    if (m)
		logCloseSession(room,false,m->m_name);
	}
    }
    else {
	for (ObjList* o = room->resources().skipNull(); o; o = o->skipNext()) {
	    MucRoomMember* m = static_cast<MucRoomMember*>(o->get());
	    logCloseSession(room,false,m->m_name);
	}
    }
    logCloseSession(room);
}

// Update protocol related page(s) in account edit/add or wizard
static void selectProtocolSpec(NamedList& p, const String& proto, bool advanced,
    const String& protoList)
{
    p.setParam("select:" + protoList,proto);
    p.setParam("select:acc_proto_cfg","acc_proto_cfg_" + getProtoPage(proto));
    p.setParam("select:acc_proto_advanced",
	"acc_proto_advanced_" + getProtoPage(advanced ? proto : String::empty()));
}

// Update protocol specific data
// Set protocol specific widgets: options, address, port ....
// Text widgets' name should start with acc_proto_protocolpagename_
// Option widgets' name should start with acc_proto_protocolpagename_opt_
static void updateProtocolSpec(NamedList& p, const String& proto, bool edit,
    const NamedList& params = NamedList::empty())
{
    DDebug(ClientDriver::self(),DebugAll,"updateProtocolSpec(%s,%u,%s)",
	proto.c_str(),edit,params.c_str());
    // Account common params
    String prefix = "acc_";
    for (const String* par = s_accParams; !par->null(); par++)
	p.setParam(prefix + *par,params.getValue(*par));
    // Protocol specific params
    prefix << "proto_" << getProtoPage(proto) << "_";
    for (const String* par = s_accProtoParams; !par->null(); par++)
	p.setParam(prefix + *par,params.getValue(*par));
    NamedIterator iter(s_accProtoParamsSel);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());)
	p.setParam(prefix + ns->name(),params.getValue(ns->name(),*ns));
    // Set default resource for new accounts if not already set
    if (!edit && proto == s_jabber) {
	String rname = prefix + "resource";
	if (!p.getValue(rname))
	    p.setParam(rname,Engine::config().getValue("client","resource","Yate"));
    }
    // Options
    prefix << "opt_";
    ObjList* opts = params["options"].split(',',false);
    for (ObjList* o = ClientLogic::s_accOptions.skipNull(); o; o = o->skipNext()) {
	String* opt = static_cast<String*>(o->get());
	bool checked = (opts && 0 != opts->find(*opt));
	p.setParam("check:" + prefix + *opt,String::boolText(checked));
    }
    TelEngine::destruct(opts);
    dumpList(p,"updateProtocolSpec");
}

// Handle protocol/providers select for DefaultLogic in account edit/add or wizard
static bool handleProtoProvSelect(Window* w, const String& name, const String& item)
{
    // Flag used to avoid resetting the providers list in provider change handler
    static bool s_changing = false;
    // Handle protocol selection in edit or wizard window:
    // Show/hide protocol specific options
    // Select nothing in providers
    bool noWiz = (name == s_accProtocol);
    if (noWiz || name == s_accWizProtocol) {
	if (!Client::valid())
	    return false;
	bool adv = false;
	Client::self()->getCheck(YSTRING("acc_showadvanced"),adv,w);
	NamedList p("");
	selectProtocolSpec(p,item,adv,name);
	// Reset provider if not changing due to provider change
	if (!s_changing)
	    p.setParam("select:" + (noWiz ? s_accProviders : s_accWizProviders),s_notSelected);
	dumpList(p,"Handle protocol select",w);
	Client::self()->setParams(&p,w);
	return true;
    }
    // Apply provider template
    noWiz = (name == s_accProviders);
    if (!noWiz && name != s_accWizProviders)
	return false;
    if (Client::s_notSelected.matches(item))
	return true;
    if (!Client::valid())
	return true;
    // Get data and update UI
    NamedList* sect = Client::s_providers.getSection(item);
    if (!sect)
	return true;
    NamedList p("");
    const String& proto = (*sect)[YSTRING("protocol")];
    bool adv = false;
    Client::self()->getCheck(YSTRING("acc_showadvanced"),adv,w);
    selectProtocolSpec(p,proto,adv,noWiz ? s_accProtocol : s_accWizProtocol);
    updateProtocolSpec(p,proto,w && w->context(),*sect);
    dumpList(p,"Handle provider select",w);
    // Avoid resetting protocol while applying provider
    s_changing = true;
    Client::self()->setParams(&p,w);
    s_changing = false;
    return true;
}

// Update the protocol list from global
// filterTypeTel: Optional pointer to protocol telephony/IM type
// specParams: Optional pointer to parameters list used to update protocol specs
// firstProto: Optional pointer to String to be filled with the first protocol in the list
static void updateProtocolList(Window* w, const String& list, bool* filterTypeTel = 0,
    NamedList* specParams = 0, String* firstProto = 0)
{
    DDebug(ClientDriver::self(),DebugAll,"updateProtocolList(%p,%s,%p,%p,%p)",
	w,list.c_str(),filterTypeTel,specParams,firstProto);
    ObjList tmp;
    ClientLogic::s_protocolsMutex.lock();
    for (ObjList* o = ClientLogic::s_protocols.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (TelEngine::null(s))
	    continue;
	if (!filterTypeTel || *filterTypeTel == isTelProto(*s))
	    tmp.append(new String(*s));
    }
    ClientLogic::s_protocolsMutex.unlock();
    for (ObjList* o = tmp.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (TelEngine::null(s))
	    continue;
	bool ok = list.null() || Client::self()->updateTableRow(list,*s,0,false,w);
	if (ok && firstProto && firstProto->null())
	    *firstProto = *s;
	if (specParams)
	    updateProtocolSpec(*specParams,*s,false);
    }
}

// Update a provider item in a given list
// filterTypeTel: Optional pointer to protocol telephony/IM type
static bool updateProvidersItem(Window* w, const String& list, const NamedList& prov,
    bool* filterTypeTel = 0)
{
    if (!Client::valid())
	return false;
    const String& proto = prov[YSTRING("protocol")];
    if (proto && (!filterTypeTel || *filterTypeTel == isTelProto(proto)))
	return Client::self()->updateTableRow(list,prov,0,false,w);
    return false;
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
static bool checkParam(NamedList& p, const String& param, const String& widget,
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
    bool ok = value && !(checkNotSel && Client::s_notSelected.matches(value));
    if (ok)
	p.setParam(param,value);
    return ok;
}

// Utility: activate the calls page
static void activatePageCalls(Window* wnd = 0, bool selTab = true)
{
    if (!Client::valid())
	return;
    NamedList p("");
    p.addParam("check:ctrlCalls",String::boolText(true));
    p.addParam("select:framePages","PageCalls");
    if (selTab)
	p.addParam("select:" + s_mainwindowTabs,"tabTelephony");
    Client::self()->setParams(&p,wnd);
}

// Check if the calls page is active
static bool isPageCallsActive(Window* wnd, bool checkTab)
{
    if (!Client::valid())
	return false;
    String sel;
    if (checkTab) {
	Client::self()->getSelect(s_mainwindowTabs,sel,wnd);
	if (sel != YSTRING("tabTelephony"))
	    return false;
	sel.clear();
    }
    Client::self()->getSelect(YSTRING("framePages"),sel,wnd);
    return sel == YSTRING("PageCalls");
}

// Retrieve a contact edit/info window.
// Create it if requested and not found.
// Set failExists to true to return 0 if already exists
static Window* getContactInfoEditWnd(bool edit, bool room, ClientContact* c,
    bool create = false, bool failExists = false)
{
    if (!Client::valid())
	return 0;
    const char* wnd = 0;
    if (edit) {
	if (c && c->mucRoom())
	    room = true;
	wnd = !room ? "contactedit" : "chatroomedit";
    }
    else
	wnd = "contactinfo";
    String wname(wnd);
    wname << "_" << (c ? c->toString().c_str() : String((unsigned int)Time::msecNow()).c_str());
    Window* w = Client::self()->getWindow(wname);
    if (w)
	return failExists ? 0 : w;
    if (!create)
	return 0;
    Client::self()->createWindowSafe(wnd,wname);
    w = Client::self()->getWindow(wname);
    if (w && c) {
	NamedList p("");
	p.addParam("context",c->toString());
	if (!edit)
	    p.addParam("property:" + s_chatEdit + ":_yate_identity",
	        s_chatEdit + ":" + c->toString());
	Client::self()->setParams(&p,w);
    }
    return w;
}

// Update account list in chat account add windows
// Select single updated account
static void updateChatAccountList(const String& account, bool upd)
{
    if (!(Client::valid() && account))
	return;
    ObjList* list = Client::listWindows();
    for (ObjList* o = (list ? list->skipNull() : 0); o; o = o->skipNext()) {
	String* id = static_cast<String*>(o->get());
	bool isContact = id->startsWith("contactedit_");
	if (!(isContact || id->startsWith("chatroomedit_")))
	    continue;
	Window* w = Client::self()->getWindow(*id);
	if (!w || (isContact && w->context()))
	    continue;
	if (upd) {
	    Client::self()->updateTableRow(s_chatAccount,account,0,false,w);
	    selectListItem(s_chatAccount,w,false,false);
	}
	else {
	    // Avoid showing another selected account
	    String tmp;
	    Client::self()->getSelect(s_chatAccount,tmp,w);
	    if (tmp && tmp == account)
		Client::self()->setSelect(s_chatAccount,s_notSelected,w);
	    Client::self()->delTableRow(s_chatAccount,account,w);
	}
    }
    TelEngine::destruct(list);
}

// Retrieve an account's enter password window
// Create it if requested and not found.
static Window* getAccPasswordWnd(const String& account, bool create)
{
    if (!(Client::valid() && account))
	return 0;
    String wname(account + "EnterPassword");
    Window* w = Client::self()->getWindow(wname);
    if (!create)
	return w;
    if (!w) {
	Client::self()->createWindowSafe(YSTRING("inputpwd"),wname);
	w = Client::self()->getWindow(wname);
	if (!w) {
	    Debug(ClientDriver::self(),DebugNote,"Failed to build account password window!");
	    return 0;
	}
    }
    NamedList p("");
    String text;
    text << "Enter password for account '" << account << "'";
    p.addParam("inputpwd_text",text);
    p.addParam("inputpwd_password","");
    p.addParam("check:inputpwd_savepassword",String::boolText(false));
    p.addParam("context","loginpassword:" + account);
    Client::self()->setParams(&p,w);
    Client::self()->setVisible(wname,true,true);
    return w;
}

// Close an account's password window
static void closeAccPasswordWnd(const String& account)
{
    Window* w = getAccPasswordWnd(account,false);
    if (w)
	Client::self()->closeWindow(w->toString());
}

// Retrieve an account's enter credentials window
// Create it if requested and not found.
static Window* getAccCredentialsWnd(const NamedList& account, bool create,
    const String& text = String::empty())
{
    if (!(Client::valid() && account))
	return 0;
    String wname(account + "EnterCredentials");
    Window* w = Client::self()->getWindow(wname);
    if (!create)
	return w;
    if (!w) {
	Client::self()->createWindowSafe(YSTRING("inputacccred"),wname);
	w = Client::self()->getWindow(wname);
	if (!w) {
	    Debug(ClientDriver::self(),DebugNote,"Failed to build account credentials window!");
	    return 0;
	}
    }
    NamedList p("");
    p.addParam("inputacccred_text",text);
    p.addParam("inputacccred_username",account.getValue(YSTRING("username")));
    p.addParam("inputacccred_password",account.getValue(YSTRING("password")));
    p.addParam("check:inputacccred_savepassword",
	String(account.getBoolValue(YSTRING("savepassword"))));
    p.addParam("context","logincredentials:" + account);
    Client::self()->setParams(&p,w);
    Client::self()->setVisible(wname,true,true);
    return w;
}

// Close an account's enter credentials window
static void closeAccCredentialsWnd(const String& account)
{
    NamedList tmp(account);
    Window* w = getAccCredentialsWnd(tmp,false);
    if (w)
	Client::self()->closeWindow(w->toString());
}

// Build a chat history item parameter list
static NamedList* buildChatParams(const char* text, const char* sender,
    unsigned int sec, bool delay = false, const char* delaysource = 0)
{
    NamedList* p = new NamedList("");
    p->addParam("text",text);
    p->addParam("sender",sender,false);
    String ts;
    String dl;
    if (!delay)
	Client::self()->formatDateTime(ts,sec,"hh:mm:ss",false);
    else {
	Client::self()->formatDateTime(ts,sec,"dd.MM.yyyy hh:mm:ss",false);
	if (!TelEngine::null(delaysource))
	    dl << "\r\nDelayed by: " << delaysource;
    }
    p->addParam("time",ts,false);
    p->addParam("delayed_by",dl,false);
    return p;
}

// Build a chat state history item parameter list
static bool buildChatState(String& buf, const NamedList& params, const char* sender)
{
    const String& state = params[YSTRING("chatstate")];
    if (!state)
	return false;
    buf = s_chatStates[state];
    if (!buf)
	return true;
    NamedList tmp("");
    tmp.addParam("sender",sender);
    tmp.addParam("state",state);
    tmp.replaceParams(buf);
    return true;
}

// Add a notification text in contact's chat history
static void addChatNotify(ClientContact& c, const char* text,
    unsigned int sec = Time::secNow(), const char* what = "notify",
    const String& roomId = String::empty())
{
    if (!c.hasChat())
	return;
    NamedList* p = buildChatParams(text,0,sec);
    MucRoom* room = c.mucRoom();
    if (!room)
	c.addChatHistory(what,p);
    else
	room->addChatHistory(roomId ? roomId : room->resource().toString(),what,p);
}

// Add an online/offline notification text in contact's chat history
static inline void addChatNotify(ClientContact& c, bool online,
    bool account = false, unsigned int sec = Time::secNow())
{
    String text;
    if (!account)
	text << c.m_name;
    else
	text = "Account";
    text << " is " << (online ? "online" : "offline");
    addChatNotify(c,text,sec);
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
    // Add chat notification and update status
    if (c.hasChat() && c.online()) {
	addChatNotify(c,false);
	NamedList p("");
	String img = resStatusImage(ClientResource::Offline);
	p.addParam("image:status_image",img);
	p.addParam("status_text",ClientResource::statusDisplayText(ClientResource::Offline));
	c.updateChatWindow(p,0,img);
    }
    // Remove from chat
    Client::self()->delTableRow(s_chatContactList,c.toString());
    // Remove instances from contacts list
    String instid;
    removeContacts(c.buildInstanceId(instid));
    // Close chat session
    logCloseSession(&c);
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

// Set account own contact
static void setAccountContact(ClientAccount* acc)
{
    if (!acc)
	return;
    URI tmp(acc->toString());
    String uri(tmp.getUser() + "@" + tmp.getHost());
    String cId;
    ClientContact::buildContactId(cId,acc->toString(),uri);
    acc->setContact(new ClientContact(0,cId,acc->toString(),uri));
}

// Retrieve the selected account
static ClientAccount* selectedAccount(ClientAccountList& accounts, Window* wnd = 0,
    const String& list = String::empty())
{
    String account;
    if (!Client::valid())
	return 0;
    if (!list)
	Client::self()->getSelect(s_accountList,account,wnd);
    else
	Client::self()->getSelect(list,account,wnd);
    return account ? accounts.findAccount(account) : 0;
}

// Retrieve the chat contact
static ClientContact* selectedChatContact(ClientAccountList& accounts,
    Window* wnd = 0, bool rooms = true)
{
    String c;
    if (Client::valid())
	Client::self()->getSelect(s_chatContactList,c,wnd);
    if (!c)
	return 0;
    return rooms ? accounts.findAnyContact(c) : accounts.findContact(c);
}

// Build account action item from account id
static inline String& buildAccAction(String& buf, const String& action, ClientAccount* acc)
{
    buf = action + ":" + acc->toString();
    return buf;
}

// Fill acc_login/logout active parameters
static void fillAccLoginActive(NamedList& p, ClientAccount* acc)
{
    if (acc && isTelProto(acc->protocol())) {
	p.addParam("active:" + s_actionLogin,String::boolText(true));
	p.addParam("active:" + s_actionLogout,String::boolText(true));
    }
    else {
	bool offline = !acc || acc->resource().offline();
	p.addParam("active:" + s_actionLogin,String::boolText(acc && offline));
	p.addParam("active:" + s_actionLogout,String::boolText(!offline));
    }
}

// Fill acc_del/edit active parameters
static inline void fillAccEditActive(NamedList& p, bool active)
{
    const char* tmp = String::boolText(active);
    p.addParam("active:acc_del",tmp);
    p.addParam("active:acc_edit",tmp);
}

// Utility function used to save a widget's text
static inline void saveParam(NamedList& params,
    const String& prefix, const String& param, Window* wnd)
{
    String val;
    Client::self()->getText(prefix + param,val,false,wnd);
    params.setParam(param,val);
}

// Utility function used to save a widget's check state
static inline void saveCheckParam(NamedList& params, const String& prefix,
    const String& param, Window* wnd, bool defVal = false)
{
    Client::self()->getCheck(prefix + param,defVal,wnd);
    params.setParam(param,String::boolText(defVal));
}

// Retrieve account protocol, username, host from UI
static bool getAccount(Window* w, String* proto, String* user, String* host)
{
    if (!(proto || user || host))
	return false;
    bool noWiz = !s_accWizard->isWindow(w);
    String p;
    if (host && !proto)
	proto = &p;
    if (proto) {
	Client::self()->getText(noWiz ? s_accProtocol : s_accWizProtocol,
	    *proto,false,w);
	if (proto->null()) {
	    showError(w,"A protocol must be selected");
	    return false;
	}
    }
    if (user) {
	Client::self()->getText("acc_username",*user,false,w);
	if (user->null()) {
	    showError(w,"Account username is mandatory");
	    return false;
	}
    }
    if (host) {
	String prefix;
	prefix << "acc_proto_" << getProtoPage(*proto) << "_";
	Client::self()->getText(prefix + "domain",*host,false,w);
	if (host->null()) {
	    if (*proto == s_jabber) {
		showError(w,"Account domain is mandatory for the selected protocol");
		return false;
	    }
	    Client::self()->getText(prefix + "server",*host,false,w);
	    if (host->null()) {
		showError(w,"You must enter a domain or server");
		return false;
	    }
	}
    }
    return true;
}

// Read room data from a window
// Check the room pointer on return: 0 means failure
// Return true if connection data changed (username, server, password)
static bool getRoom(Window* w, ClientAccount* acc, bool permanent,
    bool denyExist, MucRoom*& r, bool& dataChanged, bool hasRoomSrv = true)
{
    r = 0;
    if (!w)
	return false;
    if (!acc) {
	showError(w,"No account selected");
	return false;
    }
    if (!acc->resource().online()) {
	showError(w,"The account is offline");
	return false;
    }
    String contact;
    String room;
    String server;
    if (hasRoomSrv) {
	Client::self()->getText(YSTRING("room_room"),room,false,w);
	Client::self()->getText(YSTRING("room_server"),server,false,w);
	contact << room << "@" << server;
    }
    else {
	Client::self()->getText(YSTRING("room_uri"),contact,false,w);
	splitContact(contact,room,server);
    }
    if (!checkUri(w,room,server,true))
	return false;
    String id;
    ClientContact::buildContactId(id,acc->toString(),contact);
    r = acc->findRoom(id);
    bool changed = (r == 0);
    dataChanged = changed;
    if (!r) {
	// Check if a contact with the same user@domain already exists
	if (permanent && acc->findContact(id)) {
	    showError(w,"A contact with the same username and domain already exist");
	    return false;
	}
	r = new MucRoom(acc,id,0,contact,0);
    }
    else if (denyExist && (r->local() || r->remote())) {
	r = 0;
	return showRoomDupError(w);
    }
    String nick;
    String pwd;
    String name;
    Client::self()->getText(YSTRING("room_nick"),nick,false,w);
    Client::self()->getText(YSTRING("room_password"),pwd,false,w);
    // Join room wizard don't have name set, avoid resetting it
    if (hasRoomSrv)
	Client::self()->getText(YSTRING("room_name"),name,false,w);
    else
	name = r->m_name;
    bool autoJoin = false;
    Client::self()->getCheck(YSTRING("room_autojoin"),autoJoin,w);
    bool hist = true;
    Client::self()->getCheck(YSTRING("room_history"),hist,w);
    String lastHist;
    if (hist) {
	bool t = false;
	if (Client::self()->getCheck(YSTRING("room_historylast"),t,w) && t)
	    Client::self()->getText(YSTRING("room_historylast_value"),lastHist,false,w);
    }
    if (lastHist.toInteger() < 1)
	lastHist.clear();
    // Update room data. Check if connection related data changed
    if (setChangedString(r->m_password,pwd))
	changed = dataChanged = true;
    dataChanged = setChangedString(r->m_name,name ? name : contact) || dataChanged;
    dataChanged = setChangedParam(r->m_params,YSTRING("nick"),nick) || dataChanged;
    dataChanged = setChangedParam(r->m_params,YSTRING("autojoin"),String::boolText(autoJoin)) ||
	dataChanged;
    dataChanged = setChangedParam(r->m_params,YSTRING("history"),String::boolText(hist)) ||
	dataChanged;
    dataChanged = setChangedParam(r->m_params,YSTRING("historylast"),lastHist) || dataChanged;
    // Permanent room
    if (permanent) {
	if (!(r->local() && r->remote()))
	    dataChanged = true;
	r->setLocal(true);
	r->setRemote(true);
    }
    return changed;
}

// Fill a list used to update muc room edit/join window
static void fillRoomParams(NamedList& p, MucRoom* r = 0, bool hasRoomSrv = true)
{
    bool autoJoin = false;
    bool hist = true;
    String last;
    if (r) {
	p.addParam("room_account",r->accountName());
	if (hasRoomSrv) {
	    p.addParam("room_room",r->uri().getUser());
	    p.addParam("room_server",r->uri().getHost());
	}
	else
	    p.addParam("room_uri",r->uri());
	p.addParam("room_nick",r->m_params[YSTRING("nick")]);
	p.addParam("room_password",r->m_password);
	p.addParam("room_name",r->m_name);
	autoJoin = r->m_params.getBoolValue(YSTRING("autojoin"));
	hist = r->m_params.getBoolValue(YSTRING("history"));
	if (hist)
	    last = r->m_params[YSTRING("historylast")];
    }
    else {
	p.addParam("room_account","");
	if (hasRoomSrv) {
	    p.addParam("room_room","");
	    p.addParam("room_server","");
	}
	else
	    p.addParam("room_uri","");
	p.addParam("room_nick","");
	p.addParam("room_password","");
	p.addParam("room_name","");
    }
    p.addParam("check:room_autojoin",String::boolText(autoJoin));
    p.addParam("check:room_history",String::boolText(hist));
    p.addParam("check:room_historylast",String::boolText(hist && last));
    if (last.toInteger() <= 0)
	last = "30";
    p.addParam("room_historylast_value",last);
}

// Retrieve account data from UI
static bool getAccount(Window* w, NamedList& p, ClientAccountList& accounts)
{
    if (!Client::valid())
	return false;
    String proto;
    String user;
    String host;
    if (!getAccount(w,&proto,&user,&host))
	return false;
    String id;
    p.assign(DefaultLogic::buildAccountId(id,proto,user,host));
    p.addParam("enabled",String::boolText(true));
    // Account flags
    p.addParam("protocol",proto);
    String prefix = "acc_";
    // Save account parameters
    for (const String* par = s_accParams; !par->null(); par++)
	saveParam(p,prefix,*par,w);
    for (const String* par = s_accBoolParams; !par->null(); par++)
	saveCheckParam(p,prefix,*par,w);
    prefix << "proto_" << getProtoPage(proto) << "_";
    for (const String* par = s_accProtoParams; !par->null(); par++)
	saveParam(p,prefix,*par,w);
    NamedIterator iter(s_accProtoParamsSel);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());)
	saveParam(p,prefix,ns->name(),w);
    // Options
    prefix << "opt_";
    String options;
    for (ObjList* o = ClientLogic::s_accOptions.skipNull(); o; o = o->skipNext()) {
	String* opt = static_cast<String*>(o->get());
	bool checked = false;
	Client::self()->getCheck(prefix + *opt,checked,w);
	if (checked)
	    options.append(*opt,",");
    }
    bool reg = false;
    Client::self()->getCheck(YSTRING("acc_register"),reg,w);
    if (reg)
	options.append("register",",");
    p.setParam("options",options);
    dumpList(p,"Got account",w);
    return true;
}

// Update account status and login/logout active status if selected
static void updateAccountStatus(ClientAccount* acc, ClientAccountList* accounts,
    Window* wnd = 0)
{
    if (!acc)
	return;
    NamedList p("");
    acc->fillItemParams(p);
    p.addParam("check:enabled",String::boolText(acc->startup()));
    p.addParam("status_image",resStatusImage(acc->resource().m_status),false);
    Client::self()->updateTableRow(s_accountList,acc->toString(),&p,false,wnd);
    // Remove pending requests if offline
    if (acc->resource().offline())
	PendingRequest::clear(acc->toString());
    // Set login/logout enabled status
    bool selected = accounts && acc == selectedAccount(*accounts,wnd);
    NamedList pp("");
    if (selected)
	fillAccLoginActive(pp,acc);
    Client::self()->setParams(&pp,wnd);
}

// Add account pending status
static void addAccPendingStatus(NamedList& p, ClientAccount* acc, AccountStatus* stat = 0)
{
    if (!(acc && acc->hasPresence()))
	return;
    if (!stat)
	stat = AccountStatus::current();
    if (!stat)
	return;
    const char* s = lookup(stat->status(),ClientResource::s_statusName);
    acc->m_params.addParam("internal.status.status",s,false);
    p.addParam("show",s,false);
    acc->m_params.addParam("internal.status.text",stat->text(),false);
    p.addParam("status",stat->text(),false);
}

// Set account status from global. Update UI. Notify remote party
// Use current status if none specified
static void setAccountStatus(ClientAccountList* accounts, ClientAccount* acc,
    AccountStatus* stat = 0, NamedList* upd = 0, bool checkPwd = true)
{
    if (!acc)
	return;
    if (!stat)
	stat = AccountStatus::current();
    if (!stat)
	return;
    Debug(ClientDriver::self(),DebugInfo,
	"setAccountsStatus(%s) set=(%u,%s) acc=(%u,%s)",
	acc->toString().c_str(),stat->status(),stat->text().c_str(),
	acc->resource().m_status,acc->resource().m_text.c_str());
    if (acc->resource().m_status == ClientResource::Connecting &&
	stat->status() != ClientResource::Offline)
	return;
    bool changed = false;
    bool login = false;
    bool logout = false;
    switch (stat->status()) {
	case ClientResource::Online:
	    if (acc->resource().m_status == ClientResource::Offline)
		changed = login = true;
	    else {
		changed = acc->resource().setStatus(stat->status());
		if (acc->hasPresence())
		    changed = acc->resource().setStatusText(stat->text()) || changed;
	    }
	    break;
	case ClientResource::Offline:
	    changed = logout = !acc->resource().offline();
	    break;
	case ClientResource::Busy:
	case ClientResource::Dnd:
	case ClientResource::Away:
	case ClientResource::Xa:
	    if (!acc->hasPresence()) {
		// Just connect the account if not already connected
		changed = login = acc->resource().offline();
		break;
	    }
	    if (!acc->resource().offline()) {
		changed = acc->resource().setStatus(stat->status());
		changed = acc->resource().setStatusText(stat->text()) || changed;
	    }
	    else
		changed = login = true;
	    break;
    }
    if (!changed)
	return;
    acc->m_params.clearParam(YSTRING("internal.status"),'.');
    Message* m = 0;
    if (login || logout) {
	if (login && checkPwd && !acc->params().getValue(YSTRING("password"))) {
	    getAccPasswordWnd(acc->toString(),true);
	    return;
	}
	m = userLogin(acc,login);
	// Update account status
	if (login) {
	    acc->resource().m_status = ClientResource::Connecting;
	    addAccPendingStatus(*m,acc,stat);
	    // Make sure we see the login fail notification
	    acc->m_params.clearParam(YSTRING("internal.nologinfail"));
	}
	else {
	    acc->resource().m_status = ClientResource::Offline;
	    // Avoid notification when disconnected
	    acc->m_params.setParam("internal.nologinfail",String::boolText(true));
	    // Remove account notifications now
	    removeAccNotifications(acc);
	}
	acc->resource().setStatusText();
    }
    else
	m = Client::buildNotify(true,acc->toString(),acc->resource(false));
    NamedList set("");
    NamedList* p = 0;
    if (upd)
	p = new NamedList("");
    else
	p = &set;
    p->addParam("status_image",resStatusImage(acc->resource().m_status),false);
    const char* sName = acc->resource().statusName();
    NamedString* status = new NamedString("status",sName);
    status->append(acc->resource().m_text,": ");
    p->addParam(status);
    if (upd)
	upd->addParam(new NamedPointer(acc->toString(),p,String::boolText(false)));
    else
	Client::self()->setTableRow(s_accountList,acc->toString(),p);
    if (accounts)
	updateAccountStatus(acc,accounts);
    Engine::enqueue(m);
}

// Set enabled accounts status from global. Update UI. Notify remote party
static void setAccountsStatus(ClientAccountList* accounts)
{
    if (!Client::s_engineStarted)
	return;
    if (!accounts)
	return;
    // Update UI
    AccountStatus* stat = AccountStatus::current();
    AccountStatus::updateUi();
    NamedList upd("");
    for (ObjList* o = accounts->accounts().skipNull(); o; o = o->skipNext()) {
	ClientAccount* acc = static_cast<ClientAccount*>(o->get());
	if (!acc->startup())
	    continue;
	setAccountStatus(accounts,acc,stat,&upd);
    }
    if (upd.count())
	Client::self()->updateTableRows(s_accountList,&upd);
}

// Login account proxy for DefaultLogic::loginAccount()
// Check password if required
static bool loginAccount(ClientLogic* logic, const NamedList& account, bool login,
    bool checkPwd = true)
{
    if (login && checkPwd && !account.getValue(YSTRING("password")))
	return 0 != getAccPasswordWnd(account,true);
    return logic && logic->loginAccount(account,login);
}

// Fill a list used to update a chat contact UI
// data: fill contact name, account ...
// status: fill contact status
static void fillChatContact(NamedList& p, ClientContact& c, bool data, bool status,
    bool roomContact = false)
{
    if (!(data || status))
	return;
    // Fill status
    if (roomContact && c.mucRoom())
	p.addParam("type","chatroom");
    if (status) {
	ClientResource* res = c.status();
	int stat = c.online() ? ClientResource::Online : ClientResource::Offline;
	if (res)
	    stat = res->m_status;
	String text;
	if (!roomContact) {
	    String img = resStatusImage(stat);
	    p.addParam("image:status_image",img,false);
	    p.addParam("name_image",img,false);
	    if (res)
		text = res->m_text;
	}
	else
	    p.addParam("name_image",Client::s_skinPath + "muc_16.png");
	p.addParam("status_text",text ? text.c_str() : ClientResource::statusDisplayText(stat));
	p.addParam("status",lookup(stat,ClientResource::s_statusName));
    }
    // Fill other data
    if (!data)
	return;
    p.addParam("account",c.accountName());
    p.addParam("name",c.m_name);
    p.addParam("contact",c.uri());
    p.addParam("subscription",c.m_subscription);
    if (!c.mucRoom()) {
	NamedString* groups = new NamedString("groups");
	Client::appendEscape(*groups,c.groups());
	p.addParam(groups);
    }
    else
	p.addParam("groups","Chat Rooms");
}

// Enable/disable chat contacts actions
static void enableChatActions(ClientContact* c, bool checkVisible = true)
{
    if (!Client::valid())
	return;
    // Check if the chat tab is visible
    if (c && checkVisible) {
	String tab;
	Client::self()->getSelect(s_mainwindowTabs,tab);
	if (tab != YSTRING("tabChat"))
	    c = 0;
    }
    const char* s = String::boolText(c != 0);
    bool mucRoom = c && c->mucRoom();
    NamedList p("");
    p.addParam("active:" + s_chat,s);
    p.addParam(s_chat,!mucRoom ? "Chat" : "Join");
    p.addParam("active:" + s_chatCall,String::boolText(!mucRoom && c && c->findAudioResource()));
    p.addParam("active:" + s_fileSend,String::boolText(!mucRoom && c && c->findFileTransferResource()));
    p.addParam("active:" + s_chatShowLog,s);
    p.addParam("active:" + s_chatEdit,s);
    p.addParam("active:" + s_chatDel,s);
    const char* noRoomOk = String::boolText(!mucRoom && c);
    p.addParam("active:" + s_chatInfo,noRoomOk);
    p.addParam("active:" + s_chatSub,noRoomOk);
    p.addParam("active:" + s_chatUnsubd,noRoomOk);
    p.addParam("active:" + s_chatUnsub,noRoomOk);
    Client::self()->setParams(&p);
}

// Change a contact's docked chat status
// Re-create the chat if the contact has one
static void changeDockedChat(ClientContact& c, bool on)
{
    static const String s_histParam("history");
    static const String s_itCountParam("_yate_tempitemcount");
    static const String s_itReplParam("_yate_tempitemreplace");
    if (!c.hasChat()) {
	c.setDockedChat(on);
	return;
    }
    // Get chat history and message
    String history;
    String input;
    c.getChatHistory(history,true);
    c.getChatInput(input);
    // Retrieve properties
    String tempItemCount;
    String tempItemReplace;
    c.getChatProperty(s_histParam,s_itCountParam,tempItemCount);
    c.getChatProperty(s_histParam,s_itReplParam,tempItemReplace);
    // Re-create chat
    c.destroyChatWindow();
    c.setDockedChat(on);
    c.createChatWindow();
    NamedList p("");
    fillChatContact(p,c,true,true);
    ClientResource* res = c.status();
    c.updateChatWindow(p,"Chat [" + c.m_name + "]",
	resStatusImage(res ? res->m_status : ClientResource::Offline));
    // Set old chat state
    c.setChatHistory(history,true);
    c.setChatInput(input);
    c.setChatProperty(s_histParam,s_itCountParam,tempItemCount);
    c.setChatProperty(s_histParam,s_itReplParam,tempItemReplace);
    c.showChat(true);
}

// Retrieve the selected item in muc room members list
static MucRoomMember* selectedRoomMember(MucRoom& room)
{
    Window* w = room.getChatWnd();
    if (!w)
	return 0;
    NamedList p("");
    String tmp("getselect:" + s_mucMembers);
    p.addParam(tmp,"");
    Client::self()->getTableRow(ClientContact::s_dockedChatWidget,room.resource().toString(),&p,w);
    const String& id = p[tmp];
    return room.findMemberById(id);
}

// Enable/disable MUC room actions
static void enableMucActions(NamedList& p, MucRoom& room, MucRoomMember* member,
    bool roomActions = true)
{
    // Room related actions
    if (roomActions) {
	p.addParam("active:" + s_mucChgSubject,String::boolText(room.canChangeSubject()));
	p.addParam("active:" + s_mucChgNick,String::boolText(room.resource().online()));
	p.addParam("active:" + s_mucInvite,String::boolText(room.canInvite()));
    }
    // Member related actions
    if (member && !room.ownMember(member)) {
	p.addParam("active:" + s_mucPrivChat,String::boolText(room.canChatPrivate()));
	p.addParam("active:" + s_mucKick,String::boolText(member->online() && room.canKick(member)));
	p.addParam("active:" + s_mucBan,
	    String::boolText(member->online() && member->m_uri && room.canBan(member)));
    }
    else {
	const char* no = String::boolText(false);
	p.addParam("active:" + s_mucPrivChat,no);
	p.addParam("active:" + s_mucKick,no);
	p.addParam("active:" + s_mucBan,no);
    }
}

// Update the status of a MUC room member
static void updateMucRoomMember(MucRoom& room, MucRoomMember& item, Message* msg = 0)
{
    NamedList* pList = new NamedList("");
    NamedList* pChat = 0;
    const char* upd = String::boolText(true);
    bool canChat = false;
    if (room.ownMember(item.toString())) {
	canChat = room.canChat();
	fillChatContact(*pList,room,true,true);
	pChat = new NamedList(*pList);
	// Override some parameters
	pChat->setParam("name",room.uri());
	pList->setParam("name",item.m_name);
	pList->setParam("groups","Me");
	// Enable actions
	enableMucActions(*pChat,room,selectedRoomMember(room));
	if (item.offline()) {
	    pChat->addParam("room_subject","");
	    // Mark all members offline
	    for (ObjList* o = room.resources().skipNull(); o; o = o->skipNext()) {
		MucRoomMember* m = static_cast<MucRoomMember*>(o->get());
		if (!m->offline()) {
		    m->m_status = ClientResource::Offline;
		    updateMucRoomMember(room,*m);
		}
	    }
	    // Add destroy notification in room chat
	    if (msg && msg->getBoolValue(YSTRING("muc.destroyed"))) {
		String text("Room was destroyed");
		// Room destroyed
		const char* rr = msg->getValue(YSTRING("muc.destroyreason"));
		if (!TelEngine::null(rr))
		    text << " (" << rr << ")";
		const char* altRoom = msg->getValue(YSTRING("muc.alternateroom"));
		if (!TelEngine::null(altRoom))
		    text << "\r\nPlease join " << altRoom;
		addChatNotify(room,text,msg->msgTime().sec());
	    }
	}
    }
    else {
	pList->addParam("account",room.accountName());
	pList->addParam("name",item.m_name);
	pList->addParam("groups",lookup(item.m_role,MucRoomMember::s_roleName));
	pList->addParam("status_text",ClientResource::statusDisplayText(item.m_status));
	String uri = item.m_uri;
	if (uri)
	    uri.append(item.m_instance,"/");
	pList->addParam("contact",uri,false);
	String img = resStatusImage(item.m_status);
	pList->addParam("image:status_image",img);
	pList->addParam("name_image",img);
	if (room.hasChat(item.toString())) {
	    pChat = new NamedList(*pList);
	    pChat->setParam("name",room.uri() + " - " + item.m_name);
	    canChat = room.canChatPrivate() && item.online();
	}
	// Remove offline non members from members list
	if (item.offline() && item.m_affiliation <= MucRoomMember::Outcast)
	    upd = 0;
    }
    // Update members list in room page
    NamedList tmp("");
    NamedList* params = new NamedList("");
    params->addParam(new NamedPointer(item.toString(),pList,upd));
    tmp.addParam(new NamedPointer("updatetablerows:" + s_mucMembers,params));
    room.updateChatWindow(room.resource().toString(),tmp);
    if (pChat) {
	// Enable/disable chat
	pChat->addParam("active:" + s_chatSend,String::boolText(canChat));
	pChat->addParam("active:message",String::boolText(canChat));
	room.updateChatWindow(item.toString(),*pChat);
	TelEngine::destruct(pChat);
    }
}

// Show a MUC room's chat. Create and initialize it if not found
static void createRoomChat(MucRoom& room, MucRoomMember* member = 0, bool active = true)
{
    if (!member)
	member = &room.resource();
    if (room.hasChat(member->toString())) {
	room.showChat(member->toString(),true,active);
	return;
    }
    room.createChatWindow(member->toString());
    updateMucRoomMember(room,*member);
    if (!room.ownMember(member)) {
	room.showChat(member->toString(),true,active);
	return;
    }
    // Build context menu(s)
    NamedList tmp("");
    // Room context menu
    String menuName("menu_" + room.resource().toString());
    NamedList* pRoom = new NamedList(menuName);
    pRoom->addParam("title","Room");
    pRoom->addParam("item:" + s_mucSave,"");
    pRoom->addParam("item:","");
    pRoom->addParam("item:" + s_mucChgNick,"");
    pRoom->addParam("item:" + s_mucChgSubject,"");
    pRoom->addParam("item:","");
    pRoom->addParam("item:" + s_mucInvite,"");
    pRoom->addParam("item:","");
    pRoom->addParam("item:" + s_mucRoomShowLog,"");
    tmp.addParam(new NamedPointer("setmenu",pRoom,""));
    // Members context menu
    menuName << "_" << s_mucMembers;
    NamedList* pMembers = new NamedList(menuName);
    pMembers->addParam("item:" + s_mucPrivChat,"");
    pMembers->addParam("item:","");
    pMembers->addParam("item:" + s_mucKick,"");
    pMembers->addParam("item:" + s_mucBan,"");
    pMembers->addParam("item:","");
    pMembers->addParam("item:" + s_mucMemberShowLog,"");
    NamedList* p = new NamedList("");
    p->addParam(new NamedPointer("contactmenu",pMembers));
    tmp.addParam(new NamedPointer("setparams:" + s_mucMembers,p));
    room.updateChatWindow(room.resource().toString(),tmp);
    // Show it
    room.showChat(member->toString(),true,active);
}

// Reset a MUC room. Destroy chat window
static void clearRoom(MucRoom* room)
{
    if (!room)
	return;
    if (!room->resource().offline()) {
	Engine::enqueue(room->buildJoin(false));
	room->resource().setStatus(ClientResource::Offline);
    }
    room->resource().m_affiliation = MucRoomMember::AffNone;
    room->resource().m_role = MucRoomMember::RoleNone;
    room->destroyChatWindow();
}

// Show a contact's info window
// Update it and, optionally, activate it
static bool updateContactInfo(ClientContact* c, bool create = false, bool activate = false)
{
    static const String s_groups("groups");
    static const String s_resources("resources");
    if (!c)
	return false;
    Window* w = getContactInfoEditWnd(false,false,c,create);
    if (!w)
	return false;
    NamedList p("");
    p.addParam("title","Contact info [" + c->uri() + "]");
    p.addParam("name",c->m_name);
    p.addParam("username",c->uri());
    p.addParam("account",c->accountName());
    p.addParam("subscription",c->m_subscription);
    Client::self()->setParams(&p,w);
    // Add groups
    Client::self()->clearTable(s_groups,w);
    for (ObjList* o = c->groups().skipNull(); o; o = o->skipNext())
	Client::self()->addOption(s_groups,o->get()->toString(),false,String::empty(),w);
    // Update resources
    Client::self()->clearTable(s_resources,w);
    NamedList upd("");
    for (ObjList* o = c->resources().skipNull(); o; o = o->skipNext()) {
	ClientResource* r = static_cast<ClientResource*>(o->get());
	NamedList* l = new NamedList(r->toString());
	l->addParam("name",r->m_name);
	l->addParam("name_image",resStatusImage(r->m_status),false);
	l->addParam("status",r->m_text);
	if (r->m_audio)
	    l->addParam("audio_image",Client::s_skinPath + "phone.png");
	upd.addParam(new NamedPointer(r->toString(),l,String::boolText(true)));
    }
    Client::self()->updateTableRows(s_resources,&upd,false,w);
    // Show the window and activate it
    Client::self()->setVisible(w->id(),true,activate);
    return true;
}

// Show an edit/add chat contact window
static bool showContactEdit(ClientAccountList& accounts, bool room = false,
    ClientContact* c = 0)
{
    Window* w = getContactInfoEditWnd(true,room,c,true,true);
    if (!w) {
	// Activate it if found
	w = c ? getContactInfoEditWnd(true,room,c) : 0;
	if (w)
	    Client::self()->setActive(w->id(),true,w);
	return w != 0;
    }
    if (c && c->mucRoom())
	room = true;
    NamedList p("");
    const char* add = String::boolText(c == 0);
    const char* edit = String::boolText(c != 0);
    if (!room) {
	p.addParam("show:chataccount",add);
	p.addParam("show:frame_uri",add);
	p.addParam("show:chatcontact_account",edit);
	p.addParam("show:chatcontact_uri",edit);
	Client::self()->clearTable("groups",w);
	// Add groups used by all accounts
	NamedList upd("");
	for (ObjList* o = accounts.accounts().skipNull(); o; o = o->skipNext()) {
	    ClientAccount* a = static_cast<ClientAccount*>(o->get());
	    if (!a->hasChat())
		continue;
	    for (ObjList* oc = a->contacts().skipNull(); oc; oc = oc->skipNext()) {
		ClientContact* cc = static_cast<ClientContact*>(oc->get());
		for (ObjList* og = cc->groups().skipNull(); og; og = og->skipNext()) {
		    const String& grp = og->get()->toString();
		    NamedString* param = upd.getParam(grp);
		    NamedList* p = 0;
		    if (!param) {
			p = new NamedList(grp);
			p->addParam("group",grp);
			p->addParam("check:group",String::boolText(c == cc));
			upd.addParam(new NamedPointer(grp,p,String::boolText(true)));
		    }
		    else if (c == cc) {
			p = YOBJECT(NamedList,param);
			if (p)
			    p->setParam("check:group",String::boolText(true));
		    }
		}
	    }
	}
	Client::self()->updateTableRows(YSTRING("groups"),&upd,false,w);
	p.addParam("show:request_subscribe",String::boolText(c == 0));
    }
    if (c) {
	p.addParam("context",c->toString());
	String title;
	if (!room) {
	    title = "Edit friend ";
	    if (c->m_name && (c->m_name != c->uri()))
		title << "'" << c->m_name << "' ";
	}
	else
	    title = "Edit chat room ";
	title << "<" << c->uri() << ">";
	p.addParam("title",title);
	p.addParam("chatcontact_account",c->accountName());
	p.addParam("name",c->m_name);
	p.addParam("chatcontact_uri",c->uri());
	MucRoom* r = room ? c->mucRoom() : 0;
	if (r)
	    fillRoomParams(p,r);
    }
    else {
	p.addParam("context","");
	if (!room) {
	    p.addParam("title","Add friend");
	    p.addParam("username","");
	    p.addParam("domain","");
	    p.addParam("name","");
	    p.addParam("check:request_subscribe",String::boolText(true));
	}
	else {
	    p.addParam("title","Add chat room");
	    fillRoomParams(p);
	}
    }
    // Fill accounts. Select single account or room account
    if (!c || c->mucRoom()) {
	Client::self()->addOption(s_chatAccount,s_notSelected,false,String::empty(),w);
	for (ObjList* o = accounts.accounts().skipNull(); o; o = o->skipNext()) {
	    ClientAccount* a = static_cast<ClientAccount*>(o->get());
	    if (a->resource().online() && a->hasChat())
		Client::self()->addOption(s_chatAccount,a->toString(),false,String::empty(),w);
	}
	if (!(c && c->mucRoom()))
	    selectListItem(s_chatAccount,w,false,false);
	else
	    p.addParam("select:" + s_chatAccount,c->accountName());
    }
    Client::self()->setParams(&p,w);
    Client::self()->setVisible(w->id(),true,true);
    return true;
}

// Find a temporary wizard
static inline ClientWizard* findTempWizard(Window* wnd)
{
    if (!wnd)
	return 0;
    ObjList* o = s_tempWizards.find(wnd->id());
    return o ? static_cast<ClientWizard*>(o->get()) : 0;
}

// Retrieve selected contacts from UI
static void getSelectedContacts(ObjList& list, const String& name, Window* w,
    const String& itemToGet)
{
    if (!Client::valid())
	return;
    String param("check:" + itemToGet);
    NamedList p("");
    Client::self()->getOptions(name,&p,w);
    NamedIterator iter(p);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (!ns->name())
	    continue;
	NamedList* tmp = new NamedList(ns->name());
	Client::self()->getTableRow(name,*tmp,tmp,w);
	if (tmp->getBoolValue(param))
	    list.append(tmp);
	else
	    TelEngine::destruct(tmp);
    }
}

// Show the MUC invite window
static bool showMucInvite(ClientContact& contact, ClientAccountList* accounts)
{
    if (!Client::valid())
	return false;
    Window* w = Client::self()->getWindow(s_wndMucInvite);
    if (!w)
	return false;
    NamedList p("");
    MucRoom* room = contact.mucRoom();
    if (room) {
	p.addParam("invite_room",room->uri());
    }
    else {
	p.addParam("invite_room","");
	p.addParam("invite_password","");
    }
    p.addParam("show:label_room",String::boolText(room != 0));
    p.addParam("show:invite_room",String::boolText(room != 0));
    p.addParam("show:label_password",String::boolText(room == 0));
    p.addParam("show:invite_password",String::boolText(room == 0));
    p.addParam("invite_account",contact.accountName());
    p.addParam("invite_text","");
    String showOffline;
    Client::self()->getProperty(s_inviteContacts,YSTRING("_yate_showofflinecontacts"),showOffline,w);
    p.addParam("check:muc_invite_showofflinecontacts",showOffline);
    Client::self()->setParams(&p,w);
    Client::self()->clearTable(s_inviteContacts,w);
    if (accounts) {
	NamedList rows("");
	String sel;
	if (!room)
	    sel = contact.uri();
	for (ObjList* oa = accounts->accounts().skipNull(); oa; oa = oa->skipNext()) {
	    ClientAccount* a = static_cast<ClientAccount*>(oa->get());
	    for (ObjList* oc = a->contacts().skipNull(); oc; oc = oc->skipNext()) {
	        ClientContact* c = static_cast<ClientContact*>(oc->get());
		int stat = contactStatus(*c);
		String id = c->uri();
		// Check if contact already added
		NamedString* added = rows.getParam(id);
		if (added) {
		    NamedList* nl = YOBJECT(NamedList,added);
		    int aStat = nl ? nl->getIntValue(YSTRING("contact_status_value")) :
			ClientResource::Unknown;
		    // At least one of them offline: the greater status wins
		    if ((aStat < ClientResource::Online ||
			stat < ClientResource::Online) && stat < aStat)
			continue;
		    // Both online: the lesser status wins
		    if (stat >= aStat)
			continue;
		    rows.clearParam(added);
		}
		// Add the contact
		NamedList* p = new NamedList(id);
		fillChatContact(*p,*c,true,true);
		p->addParam("contact_status_value",String(stat));
		if (id == sel)
		    p->addParam("check:name",String::boolText(true));
		rows.addParam(new NamedPointer(id,p,String::boolText(true)));
	    }
	}
	Client::self()->updateTableRows(s_inviteContacts,&rows,false,w);
	if (sel)
	    Client::self()->setSelect(s_inviteContacts,sel,w);
    }
    Client::self()->setVisible(s_wndMucInvite,true,true);
    return true;
}

// Build a muc.room message
static Message* buildMucRoom(const char* oper, const String& account, const String& room,
    const char* reason = 0, const char* contact = 0)
{
    Message* m = Client::buildMessage("muc.room",account,oper);
    m->addParam("room",room,false);
    m->addParam("contact",contact,false);
    m->addParam("reason",reason,false);
    return m;
}

// Show advanced UI controls
// Conditionally show call account selector and set its selection
static void setAdvancedMode(bool* show = 0)
{
    if (!Client::valid())
	return;
    bool ok = show ? *show : Client::s_settings.getBoolValue("client","advanced_mode");
    const char* val = String::boolText(ok);
    NamedList p("");
    p.addParam("check:advanced_mode",val);
    p.addParam("show:frame_call_protocol",val);
    // Show/hide account selector. Select single account
    bool showAcc = ok;
    NamedString* account = 0;
    NamedList accounts("");
    Client::self()->getOptions(s_account,&accounts);
    for (unsigned int i = accounts.length(); i > 0; i--) {
	NamedString* ns = accounts.getParam(i - 1);
	if (!ns || Client::s_notSelected.matches(ns->name()))
	    continue;
	if (!account)
	    account = ns;
	else {
	    account = 0;
	    showAcc = true;
	    break;
	}
    }
    p.addParam("show:frame_call_account",String::boolText(showAcc));
    if (account)
	p.addParam("select:" + s_account,account->name());
    Client::self()->setParams(&p);
}

// Open a choose file dialog used to send/receive file(s)
static bool chooseFileTransfer(bool send, const String& action, Window* w,
    const char* file = 0)
{
    static const String s_allFilesFilter = "All files (*)";
    if (!Client::valid())
	return false;
    NamedList p("");
    p.addParam("action",action);
    p.addParam("dir",s_lastFileDir,false);
    if (send) {
	String filters;
	filters << "Image files (*.jpg *.jpeg *.png *bmp *gif *.tiff *.tif)";
	filters << "|Video files (*.avi *.divx *.xvid *.mpg *.mpeg)";
	filters << "|Portable Document Format files (*.pdf)";
	filters << "|" << s_allFilesFilter;
	p.addParam("filters",filters);
	p.addParam("caption","Choose file to send");
	p.addParam("selectedfilter",s_lastFileFilter ? s_lastFileFilter : s_allFilesFilter);
    }
    else {
	p.addParam("save",String::boolText(true));
	p.addParam("selectedfile",file,false);
	p.addParam("chooseanyfile",String::boolText(true));
    }
    return Client::self()->chooseFile(w,p);
}

// Update a file transfer item
// addNew: true to add a new item if not found
static bool updateFileTransferItem(bool addNew, const String& id, NamedList& params,
    bool setVisible = false)
{
    if (!Client::valid())
	return false;
    Window* w = Client::self()->getWindow(s_wndFileTransfer);
    if (!w)
	return false;
    NamedList p("");
    NamedPointer* np = new NamedPointer(id,&params,String::boolText(addNew));
    p.addParam(np);
    bool ok = Client::self()->updateTableRows(s_fileProgressList,&p,false,w);
    np->takeData();
    if (setVisible)
	Client::self()->setVisible(s_wndFileTransfer,true);
    return ok;
}

// Retrieve a file transfer item
// Delete the item from list. Drop the channel
static bool getFileTransferItem(const String& id, NamedList& params, Window* w = 0)
{
    if (!Client::valid())
	return false;
    if (!w)
	w = Client::self()->getWindow(s_wndFileTransfer);
    return w && Client::self()->getTableRow(s_fileProgressList,id,&params,w);
}

// Drop a file transfer item
// Delete the item from list. Drop the channel
static bool dropFileTransferItem(const String& id)
{
    if (!Client::valid())
	return false;
    Window* w = Client::self()->getWindow(s_wndFileTransfer);
    if (!w)
	return false;
    NamedList p("");
    getFileTransferItem(id,p,w);
    const String& chan = p[YSTRING("channel")];
    if (chan) {
	Message* m = Client::buildMessage("call.drop",String::empty());
	m->addParam("id",chan);
	m->addParam("reason",p.getBoolValue(YSTRING("send")) ? "cancelled" : "closed");
	Engine::enqueue(m);
    }
    bool ok = Client::self()->delTableRow(s_fileProgressList,id,w);
    // Close window if empty
    NamedList items("");
    Client::self()->getOptions(s_fileProgressList,&items,w);
    if (!items.getParam(0))
	Client::self()->setVisible(s_wndFileTransfer,false);
    return ok;
}

// Add a tray icon to the mainwindow stack
static bool addTrayIcon(const String& type)
{
    if (!type)
	return false;
    int prio = 0;
    String triggerAction;
    bool doubleClickAction = true;
    NamedList* iconParams = 0;
    String name;
    name << "mainwindow_" << type << "_icon";
    const char* specific = 0;
    String info = "Yate Client";
    if (type == "main") {
	prio = Client::TrayIconMain;
	iconParams = new NamedList(name);
	iconParams->addParam("icon",Client::s_skinPath + "null_team-32.png");
	triggerAction = "action_toggleshow_mainwindow";
	doubleClickAction = false;
    }
    else if (type == "incomingcall") {
	prio = Client::TrayIconIncomingCall;
	iconParams = new NamedList(name);
	iconParams->addParam("icon",Client::s_skinPath + "tray_incomingcall.png");
	info << "\r\nAn incoming call is waiting";
	triggerAction = s_actionShowCallsList;
	specific = "View calls";
    }
    else if (type == "notification" || type == "info") {
	iconParams = new NamedList(name);
	if (type == "notification") {
	    prio = Client::TrayIconNotification;
	    iconParams->addParam("icon",Client::s_skinPath + "tray_notification.png");
	    triggerAction = s_actionShowNotification;
	} 
	else {
	    prio = Client::TrayIconInfo;
	    iconParams->addParam("icon",Client::s_skinPath + "tray_info.png");
	    triggerAction = s_actionShowInfo;
	}
	info << "\r\nA notification is requiring your attention";
	specific = "View notifications";
    }
    else if (type == "incomingchat") {
	prio = Client::TrayIconIncomingChat;
	iconParams = new NamedList(name);
	iconParams->addParam("icon",Client::s_skinPath + "tray_incomingchat.png");
	info << "\r\nYou have unread chat";
	triggerAction = s_actionPendingChat;
	specific = "View chat";
    }
    if (!iconParams)
	return false;
    iconParams->addParam("tooltip",info);
    iconParams->addParam("dynamicActionTrigger:string",triggerAction,false);
    if (doubleClickAction)
	iconParams->addParam("dynamicActionDoubleClick:string",triggerAction,false);
    // Add the menu
    NamedList* pMenu = new NamedList("menu_" + type);
    pMenu->addParam("item:quit","Quit");
    pMenu->addParam("item:","");
    pMenu->addParam("item:action_show_mainwindow","Show application");
    if (prio != Client::TrayIconMain && triggerAction && specific) {
	pMenu->addParam("item:","");
	pMenu->addParam("item:" + triggerAction,specific);
    }
    iconParams->addParam(new NamedPointer("menu",pMenu));
    return Client::addTrayIcon(YSTRING("mainwindow"),prio,iconParams);
}

// Remove a tray icon from mainwindow stack
static inline bool removeTrayIcon(const String& type)
{
    return type &&
	Client::removeTrayIcon(YSTRING("mainwindow"),"mainwindow_" + type + "_icon");
}

// Notify incoming chat to the user
static void notifyIncomingChat(ClientContact* c, const String& id = String::empty())
{
    if (!(c && Client::valid()))
	return;
    MucRoom* room = c->mucRoom();
    if (!room) {
	if (c->isChatActive())
	    return;
	c->flashChat();
    }
    else {
	if (!id || room->isChatActive(id))
	    return;
	room->flashChat(id);
    }
    const String& str = !room ? c->toString() : id;
    if (!s_pendingChat.find(str))
	s_pendingChat.append(new String(str));
    addTrayIcon(YSTRING("incomingchat"));
}

// Show the first chat item in pending chat
static void showPendingChat(ClientAccountList* accounts)
{
    if (!(accounts && Client::valid()))
	return;
    bool tryAgain = true;
    while (tryAgain) {
	String* id = static_cast<String*>(s_pendingChat.remove(false));
	if (!s_pendingChat.skipNull()) {
	    removeTrayIcon(YSTRING("incomingchat"));
	    tryAgain = false;
	}
	if (!id)
	    break;
	ClientContact* c = accounts->findContact(*id);
	MucRoom* room = !c ? accounts->findRoomByMember(*id) : 0;
	if (c) {
	    if (c->hasChat()) {
		c->flashChat(false);
		c->showChat(true,true);
	    }
	    else
		c = 0;
	}
	else if (room) {
	    if (room->hasChat(*id)) {
		room->flashChat(*id,false);
		room->showChat(*id,true,true);
	    }
	    else
		room = 0;
	}
	TelEngine::destruct(id);
	tryAgain = !(c || room);
    }
}

// Remove an item from pending chat
// Stop flashing it if a list is given
static void removePendingChat(const String& id, ClientAccountList* accounts = 0)
{
    if (!(id && Client::valid()))
	return;
    s_pendingChat.remove(id);
    if (!s_pendingChat.skipNull())
	removeTrayIcon(YSTRING("incomingchat"));
    if (!accounts)
	return;
    ClientContact* c = accounts->findContact(id);
    MucRoom* room = !c ? accounts->findRoomByMember(id) : 0;
    if (c)
	c->flashChat(false);
    else if (room)
	room->flashChat(id,false);
}

// Set offline to MUCs belonging to a given account
static void setOfflineMucs(ClientAccount* acc)
{
    if (!acc || Client::exiting())
	return;
    for (ObjList* o = acc->mucs().skipNull(); o; o = o->skipNext()) {
	MucRoom* room = static_cast<MucRoom*>(o->get());
	if (room->resource().offline())
	    continue;
	room->resource().m_status = ClientResource::Offline;
	room->resource().m_affiliation = MucRoomMember::AffNone;
	room->resource().m_role = MucRoomMember::RoleNone;
	updateMucRoomMember(*room,room->resource());
    }
}

// Update telephony account selector(s)
static void updateTelAccList(bool ok, ClientAccount* acc)
{
    if (!acc)
	return;
    DDebug(ClientDriver::self(),DebugAll,"updateTelAccList(%d,%p)",ok,acc);
    if (ok && (isTelProto(acc->protocol()) || isGmailAccount(acc)))
	Client::self()->updateTableRow(s_account,acc->toString());
    else
	Client::self()->delTableRow(s_account,acc->toString());
}

// Query roster on a given account
static bool queryRoster(ClientAccount* acc)
{
    if (!acc)
	return false;
    Message* m = Client::buildMessage("user.roster",acc->toString(),"query");
    m->copyParams(acc->params(),YSTRING("protocol"));
    return Engine::enqueue(m);
}


/**
 * ClientWizard
 */
ClientWizard::ClientWizard(const String& wndName, ClientAccountList* accounts, bool temp)
    : String(wndName),
    m_accounts(accounts),
    m_temp(temp)
{
    if (!temp)
	return;
    // Build a temporary window
    String name = wndName;
    name << (unsigned int)Time::msecNow();
    assign(name);
    if (Client::valid())
	Client::self()->createWindowSafe(wndName,name);
    Window* w = window();
    if (w)
	Client::self()->setProperty(*this,YSTRING("_yate_destroyonhide"),String::boolText(true),w);
}

// Handle actions from user interface
bool ClientWizard::action(Window* w, const String& name, NamedList* params)
{
    if (!isWindow(w))
	return false;
    XDebug(ClientDriver::self(),DebugAll,"ClientWizard(%s)::action(%s,%p) [%p]",
	c_str(),name.c_str(),params,this);
    if (name == s_actionNext) {
	onNext();
	return true;
    }
    if (name == s_actionPrev) {
	onPrev();
	return true;
    }
    if (name == s_actionCancel) {
	onCancel();
	return true;
    }
    return false;
}

// Handle checkable widgets status changes in wizard window
bool ClientWizard::toggle(Window* w, const String& name, bool active)
{
    if (!isWindow(w))
	return false;
    XDebug(ClientDriver::self(),DebugAll,"ClientWizard(%s)::toggle(%s,%u) [%p]",
	c_str(),name.c_str(),active,this);
    if (name == YSTRING("window_visible_changed")) {
	windowVisibleChanged(active);
	return false;
    }
    return false;
}

// Handle user.notify messages. Restart the wizard if the operating account is offline
// Return true if handled
bool ClientWizard::handleUserNotify(const String& account, bool ok, const char* reason)
{
    if (!(m_account && m_account == account))
	return false;
    DDebug(ClientDriver::self(),DebugAll,"ClientWizard(%s)::handleUserNotify(%s,%u)",
	c_str(),account.c_str(),ok);
    if (ok)
	return true;
    reset(true);
    if (Client::valid() && Client::self()->getVisible(toString())) {
	start();
	showError(window(),
	    "The selected account is offline.\r\nChoose another one or close the wizard");
    }
    return true;
}

// Retrieve the selected account
ClientAccount* ClientWizard::account(const String& list)
{
    Window* w = m_accounts ? window() : 0;
    ClientAccount* acc = w ? selectedAccount(*m_accounts,w,list) : 0;
    if (acc)
	m_account = acc->toString();
    else
	m_account.clear();
    XDebug(ClientDriver::self(),DebugAll,"ClientWizard(%s) current account is %s",
	c_str(),m_account.c_str());
    return acc;
}

// Update next/prev actions active status
void ClientWizard::updateActions(NamedList& p, bool canPrev, bool canNext, bool canCancel)
{
    p.addParam("active:" + s_actionPrev,String::boolText(canPrev));
    p.addParam("active:" + s_actionNext,String::boolText(canNext));
    p.addParam("active:" + s_actionCancel,String::boolText(canCancel));
}


/*
 * AccountWizard
 */
// Release the account. Disconnect it if deleted
void AccountWizard::reset(bool full)
{
    if (!m_account)
	return;
    if (full && m_accounts) {
	if (!(Engine::exiting() || Client::exiting())) {
	    ClientAccount* acc = account();
	    if (acc) {
		Engine::enqueue(userLogin(acc,false));
		acc->m_params.setParam("internal.nologinfail",String::boolText(true));
	    }
	}
	m_accounts->removeAccount(m_account);
    }
    DDebug(ClientDriver::self(),DebugAll,
	"AccountWizard(%s) reset account delObj=%u",c_str(),full);
    m_account.clear();
}

// Handle user.notify messages
bool AccountWizard::handleUserNotify(const String& account, bool ok,
    const char* reason)
{
    if (!m_account || m_account != account)
	return false;
    DDebug(ClientDriver::self(),DebugAll,"AccountWizard(%s)::handleUserNotify(%s,%u)",
	c_str(),account.c_str(),ok);
    String s;
    if (ok)
	s << "Succesfully created account '" << account << "'";
    else {
	s << "Failed to connect account '" << account << "'";
	s.append(reason,"\r\n");
    }
    Window* w = window();
    if (w) {
	NamedList p("");
	p.addParam("accwiz_result",s);
	updateActions(p,!ok,false,false);
	Client::self()->setParams(&p,w);
    }
    reset(!ok);
    return true;
}

void AccountWizard::onNext()
{
    String page;
    currentPage(page);
    if (!page)
	return;
    if (page == YSTRING("pageAccType"))
	changePage(YSTRING("pageServer"),page);
    else if (page == YSTRING("pageServer")) {
	// Check if we have a host (domain or server)
	String host;
	if (getAccount(window(),0,0,&host))
	    changePage(YSTRING("pageAccount"),page);
    }
    else if (page == YSTRING("pageAccount")) {
	if (!m_accounts)
	    return;
	Window* w = window();
	// Check if we have a valid, non duplicate account
	String proto, user, host;
	if (getAccount(w,&proto,&user,&host)) {
	    if (!m_accounts->findAccount(URI(proto,user,host)))
		changePage(YSTRING("pageConnect"),page);
	    else
		showAccDupError(w);
	}
    }
}

void AccountWizard::onPrev()
{
    String page;
    currentPage(page);
    if (page == YSTRING("pageServer"))
	changePage(YSTRING("pageAccType"),page);
    else if (page == YSTRING("pageAccount"))
	changePage(YSTRING("pageServer"),page);
    else if (page == YSTRING("pageConnect"))
	changePage(YSTRING("pageAccount"),page);
}

void AccountWizard::onCancel()
{
    handleUserNotify(m_account,false,"Cancelled");
}

// Change the wizard page
bool AccountWizard::changePage(const String& page, const String& old)
{
    Window* w = window();
    if (!w)
	return false;
    // Provider name (update server page after resetting it)
    String provName;
    const char* nextText = "Next";
    bool canPrev = true;
    bool canNext = true;
    bool canCancel = false;
    NamedList p("");
    // Use a do {} while() to break to the end and set page
    do {
	if (!page || page == YSTRING("pageAccType")) {
	    canPrev = false;
	    // Init all wizard if first show
	    if (old)
		break;
	    p.addParam("check:acc_type_telephony",String::boolText(true));
	    p.addParam("check:acc_type_gtalk",String::boolText(false));
	    p.addParam("check:acc_type_facebook",String::boolText(false));
	    p.addParam("check:acc_type_im",String::boolText(false));
	    p.addParam("check:acc_register",String::boolText(false));
	    break;
	}
	if (page == YSTRING("pageServer")) {
	    // Don't reset the page if not comming from previous
	    if (old && old != YSTRING("pageAccType"))
		break;
	    bool tel = true;
	    Client::self()->getCheck(YSTRING("acc_type_telephony"),tel,w);
	    // Fill protocols
	    Client::self()->clearTable(s_accWizProtocol,w);
	    String proto;
	    updateProtocolList(w,s_accWizProtocol,&tel,&p,&proto);
	    // Fill providers
	    Client::self()->clearTable(s_accWizProviders,w);
	    Client::self()->addOption(s_accWizProviders,s_notSelected,false,String::empty(),w);
	    unsigned int n = Client::s_providers.sections();
	    for (unsigned int i = 0; i < n; i++) {
		NamedList* sect = Client::s_providers.getSection(i);
		if (sect && sect->getBoolValue(YSTRING("enabled"),true))
		    updateProvidersItem(w,s_accWizProviders,*sect,&tel);
	    }
	    Client::self()->setSelect(s_accWizProviders,s_notSelected,w);
	    // Select provider
	    bool prov = false;
	    Client::self()->getCheck(YSTRING("acc_type_gtalk"),prov,w);
	    if (Client::self()->getCheck(YSTRING("acc_type_gtalk"),prov,w) && prov)
		provName = "GTalk";
	    else if (Client::self()->getCheck(YSTRING("acc_type_facebook"),prov,w) && prov)
		provName = "Facebook";
	    else {
	        // Show/hide the advanced page
		bool adv = false;
		Client::self()->getCheck(YSTRING("acc_showadvanced"),adv,w);
		selectProtocolSpec(p,proto,adv,s_accWizProtocol);
	    }
	    if (provName && !Client::self()->setSelect(s_accWizProviders,provName,w)) {
		showError(w,"Provider data not found for selected account type!");
		return false;
	    }
	    break;
	}
	if (page == YSTRING("pageAccount")) {
	    nextText = "Login";
	    // Don't reset the page if not comming from previous
	    if (old && old != YSTRING("pageServer"))
		break;
	    p.addParam("acc_username","");
	    p.addParam("acc_password","");
	    break;
	}
	if (page == YSTRING("pageConnect")) {
	    if (!m_accounts || m_account)
		return false;
	    Window* w = window();
	    if (!w)
		return false;
	    NamedList a("");
	    if (!getAccount(w,a,*m_accounts))
		return false;
	    // Build account. Start login
	    ClientAccount* acc = new ClientAccount(a);
	    if (!m_accounts->appendAccount(acc)) {
		showAccDupError(w);
		TelEngine::destruct(acc);
		return false;
	    }
	    m_account = a;
	    setAccountContact(acc);
	    Message* m = userLogin(acc,true);
	    addAccPendingStatus(*m,acc);
	    m->addParam("send_presence",String::boolText(false));
	    m->addParam("request_roster",String::boolText(false));
	    acc->resource().m_status = ClientResource::Connecting;
	    TelEngine::destruct(acc);
	    Engine::enqueue(m);
	    p.addParam("accwiz_result","Connecting ...");
	    canPrev = canNext = false;
	    canCancel = true;
	    break;
	}
	return false;
    }
    while (false);
    p.addParam(s_actionNext,nextText,false);
    p.addParam("select:" + s_pagesWidget,page ? page : "pageAccType");
    updateActions(p,canPrev,canNext,canCancel);
    Client::self()->setParams(&p,w);
    if (provName)
	handleProtoProvSelect(w,s_accWizProviders,provName);
    return true;
}

/*
 * JoinMucWizard
 */
JoinMucWizard::JoinMucWizard(ClientAccountList* accounts, NamedList* tempParams)
    : ClientWizard("joinmucwizard",accounts,tempParams != 0),
    m_add(false),
    m_queryRooms(false),
    m_querySrv(false)
{
    if (!tempParams)
	return;
    reset(true);
    Window* w = window();
    if (!w)
	return;
    Client::self()->setParams(tempParams,w);
    Client::self()->setShow(YSTRING("room_autojoin"),false,w);
    changePage(YSTRING("pageJoinRoom"));
    Client::self()->setVisible(toString(),true,true);
}

// Start the wizard. Show the window
void JoinMucWizard::start(bool add)
{
    reset(true);
    changePage(String::empty());
    Window* w = window();
    if (!w)
	return;
    m_add = add;
    NamedList p("");
    const char* addOk = String::boolText(add);
    if (!add)
	p.addParam("title","Join Chat Room Wizard");
    else
	p.addParam("title","Add Chat Room Wizard");
    p.addParam("show:room_autojoin",addOk);
    Client::self()->setParams(&p,w);
    Client::self()->setVisible(toString(),true,true);
}

void JoinMucWizard::reset(bool full)
{
    selectListItem(s_mucAccounts,window());
    m_account.clear();
    m_lastPage.clear();
    setQuerySrv(false);
    setQueryRooms(false);
}

// Handle actions from wizard window. Return true if handled
bool JoinMucWizard::action(Window* w, const String& name, NamedList* params)
{
    if (!(Client::valid() && isWindow(w)))
	return false;
    if (ClientWizard::action(w,name,params))
	return true;
    // Query MUC services
    if (name == YSTRING("muc_query_servers")) {
	// Cancel
	if (m_querySrv) {
	    setQuerySrv(false);
	    return true;
	}
	ClientAccount* acc = account();
	if (!acc)
	    return true;
	String domain;
	Client::self()->getText(YSTRING("muc_domain"),domain,false,w);
	Message* m = Client::buildMessage("contact.info",acc->toString(),"queryitems");
	if (!domain && acc->contact())
	    domain = acc->contact()->uri().getHost();
	m->addParam("contact",domain,false);
	Engine::enqueue(m);
	setQuerySrv(true,domain);
	m_requests.clear();
	m_requests.append(new String(domain));
	return true;
    }
    if (name == YSTRING("textchanged")) {
	const String& sender = params ? (*params)[YSTRING("sender")] : String::empty();
	if (!sender)
	    return true;
	const String& text = (*params)[YSTRING("text")];
	if (sender == YSTRING("muc_server") || sender == YSTRING("room_room")) {
	    String page;
	    currentPage(page);
	    if (page == YSTRING("pageMucServer")) {
		if (!checkUriTextChanged(w,sender,text,sender))
		    return false;
		updatePageMucServerNext();
	    }
	    return true;
	}
	return false;
    }
    return false;
}

// Handle selection changes notifications. Return true if handled
bool JoinMucWizard::select(Window* w, const String& name, const String& item,
    const String& text)
{
    if (!isWindow(w))
	return false;
    XDebug(ClientDriver::self(),DebugAll,"JoinMucWizard(%s)::select(%s,%s,%s)",
	c_str(),name.c_str(),item.c_str(),text.c_str());
    if (name == s_mucAccounts) {
	account(s_mucAccounts);
	String page;
	currentPage(page);
	if (page == YSTRING("pageAccount")) {
	    NamedList p("");
	    updateActions(p,false,!m_account.null(),false);
	    Client::self()->setParams(&p,w);
	}
	return true;
    }
    if (name == YSTRING("muc_rooms")) {
	updatePageMucServerNext();
	return true;
    }
    return false;
}

// Handle checkable widgets status changes in wizard window
// Return true if handled
bool JoinMucWizard::toggle(Window* w, const String& name, bool active)
{
    if (!isWindow(w))
	return false;
    if (name == YSTRING("mucserver_joinroom") || name == YSTRING("mucserver_queryrooms")) {
	if (!active)
	    return true;
	String page;
	currentPage(page);
	if (page == YSTRING("pageMucServer"))
	    updatePageMucServerNext();
	return true;
    }
    return ClientWizard::toggle(w,name,active);
}

// Process contact.info message
bool JoinMucWizard::handleContactInfo(Message& msg, const String& account,
    const String& oper, const String& contact)
{
    if (m_temp)
	return false;
    if (!m_account || m_account != account)
	return false;
    bool ok = (oper == YSTRING("result"));
    if (!ok && oper != YSTRING("error"))
	return false;
    const String& req = msg[YSTRING("requested_operation")];
    bool info = (req == YSTRING("queryinfo"));
    if (!info && req != YSTRING("queryitems"))
	return false;
    ObjList* o = m_requests.find(contact);
    if (!o)
	return false;
    DDebug(ClientDriver::self(),DebugAll,
	"JoinMucWizard(%s) handleContactInfo() contact=%s oper=%s req=%s",
	c_str(),contact.c_str(),oper.c_str(),req.c_str());
    if (!info && m_queryRooms) {
	Window* w = ok ? window() : 0;
	if (w) {
	    NamedList upd("");
	    int n = msg.getIntValue(YSTRING("item.count"));
	    for (int i = 1; i <= n; i++) {
		String pref("item." + String(i));
		const String& item = msg[pref];
		if (!item)
		    continue;
		NamedList* p = new NamedList("");
		p->addParam("room",item);
		p->addParam("name",msg.getValue(pref + ".name"),false);
		upd.addParam(new NamedPointer(item,p,String::boolText(true)));
	    }
	    Client::self()->updateTableRows("muc_rooms",&upd,false,w);
	}
	if (!(ok && msg.getBoolValue(YSTRING("partial")))) {
	    o->remove();
	    setQueryRooms(false);
	}
	return true;
    }
    if (!m_querySrv)
	return false;
    if (info) {
	if (ok && contact && msg.getBoolValue(YSTRING("caps.muc"))) {
	    Window* w = window();
	    if (w)
		Client::self()->updateTableRow(YSTRING("muc_server"),contact,0,false,w);
	}
    }
    else if (ok) {
	NamedList upd("");
	int n = msg.getIntValue(YSTRING("item.count"));
	for (int i = 1; i <= n; i++) {
	    String pref("item." + String(i));
	    const String& item = msg[pref];
	    if (!item)
		continue;
	    DDebug(ClientDriver::self(),DebugAll,
		"JoinMucWizard(%s) requesting info from %s",c_str(),item.c_str());
	    Message* m = Client::buildMessage("contact.info",m_account,"queryinfo");
	    m->addParam("contact",item,false);
	    Engine::enqueue(m);
	    m_requests.append(new String(item));
	}
    }
    if (!(ok && msg.getBoolValue(YSTRING("partial"))))
	o->remove();
    if (!o->skipNull())
	setQuerySrv(false);
    return true;
}

// Handle user.notify messages. Update the accounts list
bool JoinMucWizard::handleUserNotify(const String& account, bool ok, const char* reason)
{
    if (!m_accounts || m_temp)
	return false;
    ClientAccount* acc = m_accounts->findAccount(account);
    // Add/remove to/from MUC
    if (!(acc && acc->hasChat()))
	return false;
    Window* w = window();
    if (!w)
	return false;
    if (ok)
	Client::self()->updateTableRow(s_mucAccounts,account,0,false,w);
    else {
	ClientWizard::account(s_mucAccounts);
	// Avoid showing another selected account
	if (m_account && m_account == account)
	    Client::self()->setSelect(s_mucAccounts,s_notSelected,w);
	Client::self()->delTableRow(s_mucAccounts,account,w);
    }
    if (m_account && m_account == account)
	return ClientWizard::handleUserNotify(account,ok,reason);
    return true;
}

// Join MUC room wizard
void JoinMucWizard::onNext()
{
    String page;
    currentPage(page);
    if (!page)
	return;
    if (page == YSTRING("pageAccount")) {
	if (!m_add)
	    changePage(YSTRING("pageChooseRoomServer"),page);
	else
	    changePage(YSTRING("pageMucServer"),page);
    }
    else if (page == YSTRING("pageChooseRoomServer")) {
	bool join = false;
	Window* w = window();
	if (w && Client::self()->getCheck(YSTRING("muc_use_saved_room"),join,w))
	    changePage(join ? YSTRING("pageJoinRoom") : YSTRING("pageMucServer"),page);
    }
    else if (page == YSTRING("pageMucServer")) {
	Window* w = window();
	bool join = true;
	if (w && Client::self()->getCheck(YSTRING("mucserver_joinroom"),join,w))
	    changePage(join ? YSTRING("pageJoinRoom") : YSTRING("pageRooms"),page);
    }
    else if (page == YSTRING("pageRooms"))
	changePage(YSTRING("pageJoinRoom"),page);
    else if (page == YSTRING("pageJoinRoom"))
	joinRoom();
}

void JoinMucWizard::onPrev()
{
    String page;
    currentPage(page);
    if (page == YSTRING("pageChooseRoomServer"))
	changePage(YSTRING("pageAccount"),page);
    else if (page == YSTRING("pageMucServer")) {
	if (!m_add)
	    changePage(YSTRING("pageChooseRoomServer"),page);
	else
	    changePage(YSTRING("pageAccount"),page);
    }
    else if (page == YSTRING("pageJoinRoom"))
	changePage(m_lastPage,page);
    else if (page == YSTRING("pageRooms"))
	changePage(YSTRING("pageMucServer"),page);
}

void JoinMucWizard::onCancel()
{
    if (isCurrentPage(YSTRING("pageMucServer")))
	setQuerySrv(false);
    else if (isCurrentPage(YSTRING("pageRooms")))
	setQueryRooms(false);
}

// Change the wizard page
bool JoinMucWizard::changePage(const String& page, const String& old)
{
    Window* w = window();
    if (!w)
	return false;
    const char* nextText = "Next";
    bool canPrev = true;
    bool canNext = true;
    bool canCancel = false;
    NamedList p("");
    // Use a do {} while() to break to the end and set page
    do {
	if (!page || page == YSTRING("pageAccount")) {
	    canPrev = false;
	    if (!old) {
		Client::self()->updateTableRow(s_mucAccounts,s_notSelected,0,true,w);
		selectListItem(s_mucAccounts,window());
	    }
	    canNext = (0 != account(s_mucAccounts));
	    break;
	}
	if (page == YSTRING("pageChooseRoomServer")) {
	    ClientAccount* a = account(s_mucAccounts);
	    if (old == YSTRING("pageAccount") && !a)
		return showAccSelect(w);
	    // Add rooms from account
	    Client::self()->clearTable(s_mucSavedRooms,w);
	    if (a) {
		for (ObjList* o = a->mucs().skipNull(); o; o = o->skipNext()) {
		    MucRoom* r = static_cast<MucRoom*>(o->get());
		    if (r->local() || r->remote())
			Client::self()->updateTableRow(s_mucSavedRooms,r->uri(),0,false,w);
		}
	    }
	    // Add saved rooms
	    unsigned int n = s_mucRooms.sections();
	    for (unsigned int i = 0; i < n; i++) {
		NamedList* sect = s_mucRooms.getSection(i);
		if (sect)
		    Client::self()->updateTableRow(s_mucSavedRooms,*sect,0,false,w);
	    }
	    bool useSaved = true;
	    if (w) {
		String tmp;
		Client::self()->getSelect(s_mucSavedRooms,tmp,w);
		useSaved = !tmp.null();
	    }
	    if (useSaved)
		p.addParam("check:muc_use_saved_room",String::boolText(true));
	    else
		p.addParam("check:muc_choose_server",String::boolText(true));
	    break;
	}
	if (page == YSTRING("pageMucServer")) {
	    setQuerySrv(false);
	    setQueryRooms(false);
	    canNext = selectedMucServer();
	    // Reset the page if comming from previous
	    if (old == YSTRING("pageChooseRoomServer") || old == YSTRING("pageAccount")) {
		p.addParam("check:mucserver_joinroom",String::boolText(true));
		p.addParam("room_room","");
	    }
	    break;
	}
	if (page == YSTRING("pageRooms")) {
	    // Request rooms
	    if (old != YSTRING("pageMucServer"))
		break;
	    ClientAccount* acc = account();
	    if (!acc)
		return false;
	    String target;
	    selectedMucServer(&target);
	    if (target) {
		Client::self()->clearTable(YSTRING("muc_rooms"),w);
		Message* m = Client::buildMessage("contact.info",acc->toString(),"queryitems");
		m->addParam("contact",target);
		Engine::enqueue(m);
		m_requests.clear();
		m_requests.append(new String(target));
	    }
	    else {
		showError(w,"You must choose a MUC server");
		return false;
	    }
	    break;
	}
	if (page == YSTRING("pageJoinRoom")) {
	    if (m_temp) {
		canPrev = false;
		nextText = "Join";
		break;
	    }
	    ClientAccount* acc = account();
	    if (!acc)
		return false;
	    String room;
	    String server;
	    String nick;
	    String pwd;
	    MucRoom* r = 0;
	    if (old == YSTRING("pageRooms")) {
		String sel;
		Client::self()->getSelect("muc_rooms",sel,w);
		splitContact(sel,room,server);
	    }
	    else if (old == YSTRING("pageMucServer")) {
		Client::self()->getText(YSTRING("room_room"),room,false,w);
		selectedMucServer(&server);
	    }
	    else if (old == YSTRING("pageChooseRoomServer")) {
		String tmp;
		Client::self()->getSelect(s_mucSavedRooms,tmp,w);
		if (!tmp)
		    return false;
		r = acc->findRoomByUri(tmp);
		if (r && !(r->local() || r->remote()))
		    r = 0;
		if (r) {
		    room = r->uri().getUser();
		    server = r->uri().getHost();
		}
		else {
		    NamedList* sect = s_mucRooms.getSection(tmp);
		    if (sect) {
			splitContact(*sect,room,server);
			nick = (*sect)[YSTRING("nick")];
			pwd = (*sect)[YSTRING("password")];
		    }
		    if (!(room && server)) {
			Client::self()->delTableRow(s_mucSavedRooms,tmp,w);
			s_mucRooms.clearSection(tmp);
			s_mucRooms.save();
			showError(w,"Deleted unknown/invalid room");
			return false;
		    }
		}
	    }
	    if (!checkUri(w,room,server,true))
		return false;
	    fillRoomParams(p,r,false);
	    if (!r) {
		p.setParam("room_account",acc->toString());
		p.setParam("room_uri",room + "@" + server);
		if (!nick && acc->contact())
		    nick = acc->contact()->uri().getUser();
		p.setParam("room_nick",nick);
		p.setParam("room_password",pwd);
	    }
	    nextText = "Join";
	    break;
	}
	return false;
    }
    while (false);
    p.addParam(s_actionNext,nextText,false);
    p.addParam("select:" + s_pagesWidget,page ? page.c_str() : "pageAccount");
    if (page != YSTRING("pageRooms"))
	updateActions(p,canPrev,canNext,canCancel);
    Client::self()->setParams(&p,w);
    if (page == YSTRING("pageRooms")) {
	String target;
	bool on = (old == YSTRING("pageMucServer"));
	if (on)
	    selectedMucServer(&target);
	setQueryRooms(on,target);
    }
    else if (page == YSTRING("pageMucServer"))
	updatePageMucServerNext();
    // Safe to remember the last page here: it might be the received page
    m_lastPage = old;
    return true;
}

// Handle the join room action
void JoinMucWizard::joinRoom()
{
    Window* w = window();
    if (!w)
	return;
    ClientAccount* acc = 0;
    if (!m_temp)
	acc = account();
    else if (m_accounts) {
	String tmp;
	Client::self()->getText(YSTRING("room_account"),tmp,false,w);
	acc = tmp ? m_accounts->findAccount(tmp) : 0;
    }
    bool dataChanged = false;
    MucRoom* r = 0;
    bool changed = getRoom(w,acc,m_add,m_add,r,dataChanged,false);
    if (!r)
	return;
    if (r->local() || r->remote()) {
	if (dataChanged)
	    Client::self()->action(w,s_storeContact + ":" + r->toString());
    }
    else {
	s_mucRooms.clearSection(r->uri());
	NamedList* sect = s_mucRooms.createSection(r->uri());
	if (sect) {
	    sect->addParam("nick",r->m_params[YSTRING("nick")],false);
	    sect->addParam("password",r->m_password,false);
	    s_mucRooms.save();
	}
    }
    NamedList params("");
    params.addParam("force",String::boolText(changed));
    if (Client::self()->action(w,s_mucJoin + ":" + r->toString(),&params))
	Client::self()->setVisible(toString(),false);
}

// Retrieve the selected MUC server if not currently requesting one
bool JoinMucWizard::selectedMucServer(String* buf)
{
    if (m_querySrv)
	return false;
    Window* w = window();
    if (!w)
	return false;
    String tmp;
    if (!buf)
	buf = &tmp;
    Client::self()->getText(YSTRING("muc_server"),*buf,false,w);
    return !buf->null();
}

// Set/reset servers query
void JoinMucWizard::setQuerySrv(bool on, const char* domain)
{
    if (!on)
	m_requests.clear();
    m_querySrv = on;
    XDebug(ClientDriver::self(),DebugAll,"JoinMucWizard(%s) query srv is %s",
	c_str(),String::boolText(on));
    Window* w = window();
    if (!w)
	return;
    NamedList p("");
    const char* active = String::boolText(!m_querySrv);
    p.addParam("active:muc_server",active);
    p.addParam("active:muc_domain",active);
    p.addParam("active:muc_query_servers",active);
    p.addParam("active:mucserver_joinroom",active);
    p.addParam("active:room_room",active);
    p.addParam("active:mucserver_queryrooms",active);
    addProgress(p,m_querySrv,domain);
    if (isCurrentPage(YSTRING("pageMucServer")))
	updateActions(p,!m_querySrv,selectedMucServer(),m_querySrv);
    Client::self()->setParams(&p,w);
}

// Set/reset rooms query
void JoinMucWizard::setQueryRooms(bool on, const char* domain)
{
    if (!isCurrentPage(YSTRING("pageRooms")))
	return;
    Window* w = window();
    if (!w)
	return;
    m_queryRooms = on;
    XDebug(ClientDriver::self(),DebugAll,"JoinMucWizard(%s) query rooms is %s",
	c_str(),String::boolText(on));
    NamedList p("");
    p.addParam("active:muc_rooms",String::boolText(!m_queryRooms));
    addProgress(p,m_queryRooms,domain);
    String sel;
    if (!m_queryRooms)
	Client::self()->getSelect(YSTRING("muc_rooms"),sel,w);
    updateActions(p,!m_queryRooms,!sel.null(),m_queryRooms);
    Client::self()->setParams(&p,w);
}

// Update UI progress params
void JoinMucWizard::addProgress(NamedList& dest, bool on, const char* target)
{
    dest.addParam("show:frame_progress",String::boolText(on));
    if (on) {
	String tmp("Waiting");
	tmp.append(target," for ");
	dest.addParam("progress_text",tmp + " ...");
    }
}

// Update 'next' button status on select server page
void JoinMucWizard::updatePageMucServerNext()
{
    Window* w = window();
    if (!w)
	return;
    if (m_querySrv)
	return;
    bool on = false;
    while (true) {
	String tmp;
	Client::self()->getText(YSTRING("muc_server"),tmp,false,w);
	if (!tmp)
	    break;
	bool join = false;
	Client::self()->getCheck(YSTRING("mucserver_joinroom"),join,w);
	if (join) {
	    tmp.clear();
	    Client::self()->getText(YSTRING("room_room"),tmp,false,w);
	    if (!tmp)
		break;
	}
	on = true;
	break;
    }
    Client::self()->setActive(s_actionNext,on,w);
}


/*
 * AccountStatus
 */
// Change the current item. Save to config if changed
bool AccountStatus::setCurrent(const String& name)
{
    AccountStatus* s = find(name);
    if (!s)
	return false;
    s_current = s;
    updateUi();
    Client::s_settings.setValue("accountstatus","default",s_current->toString());
    Client::s_settings.save();
    return true;
}

// Append/set an item. Save to config if changed
void AccountStatus::set(const String& name, int stat, const String& text, bool save)
{
    if (stat == ClientResource::Unknown || stat == ClientResource::Connecting)
	return;
    AccountStatus* item = find(name);
    if (!item) {
	item = new AccountStatus(name);
	s_items.append(item);
    }
    bool changed = item->m_status != stat || item->m_text != text;
    if (!changed)
	return;
    item->m_status = stat;
    item->m_text = text;
    if (!save)
	return;
    String s = lookup(item->status(),ClientResource::s_statusName);
    s << "," << item->text();
    Client::s_settings.setValue("accountstatus",item->toString(),s);
    Client::s_settings.save();
}

// Load the list from config
void AccountStatus::load()
{
    static bool loaded = false;
    if (loaded)
	return;
    NamedList* as = Client::s_settings.getSection("accountstatus");
    if (!as)
	return;
    // Load the list from config
    loaded = true;
    unsigned int n = as->length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = as->getParam(i);
	if (!(ns && ns->name()) || ns->name() == "default")
	    continue;
	int stat = ClientResource::Unknown;
	String text;
	int pos = ns->find(',');
	if (pos > 0) {
	    stat = lookup(ns->substr(0,pos),ClientResource::s_statusName,stat);
	    text = ns->substr(pos + 1);
	}
	else
	    stat = lookup(*ns,ClientResource::s_statusName,stat);
	set(ns->name(),stat,text);
    }
    setCurrent((*as)["default"]);
}

// Initialize the list
void AccountStatus::init()
{
    if (s_items.skipNull())
	return;
    const TokenDict* d = ClientResource::s_statusName;
    for (; d && d->token; d++)
	set(d->token,d->value,String::empty());
    setCurrent(lookup(ClientResource::Online,ClientResource::s_statusName));
}

// Update 
void AccountStatus::updateUi()
{
    if (!(s_current && Client::self()))
	return;
    NamedList p("");
    p.addParam("image:global_account_status",resStatusImage(s_current->status()));
    String info("Current status: ");
    if (s_current->text())
	info << s_current->text();
    else
	info << ClientResource::statusDisplayText(s_current->status());
    p.addParam("property:global_account_status:toolTip",info);
    Client::self()->setParams(&p);
}


/*
 * PendingRequest
 */
// Remove all account's requests
void PendingRequest::clear(const String& account)
{
    for (ObjList* o = s_items.skipNull(); o; ) {
	PendingRequest* req = static_cast<PendingRequest*>(o->get());
	if (req->m_account != account)
	    o = o->skipNext();
	else {
	    o->remove();
	    o = o->skipNull();
	}
    }
}

// Request info/items from target
PendingRequest* PendingRequest::request(bool info, ClientAccount* acc, const String& target,
    bool mucserver)
{
    if (!acc)
	return 0;
    String id;
    id << acc->toString() << "_" << target << "_" << info << "_" << mucserver;
    PendingRequest* req = find(id);
    if (req)
	return req;
    req = new PendingRequest(id,acc->toString(),target);
    req->m_mucServer = mucserver;
    s_items.append(req);
    XDebug(ClientDriver::self(),DebugAll,"ClientLogic added pending request '%s'",
	req->toString().c_str());
    Message* m = Client::buildMessage("contact.info",acc->toString(),
	info ? "queryinfo" : "queryitems");
    m->addParam("contact",target,false);
    m->addParam("notify",id);
    Engine::enqueue(m);
    return req;
}

// Request MUC rooms from target
bool PendingRequest::requestMucRooms(ClientAccount* acc, const String& target)
{
    if (!acc)
	return false;
    String id;
    id << acc->toString() << "_" << target << "_mucrooms";
    if (find(id))
	return true;
    PendingRequest* req = new PendingRequest(id,acc->toString(),target);
    req->m_mucRooms = true;
    s_items.append(req);
    Message* m = Client::buildMessage("contact.info",acc->toString(),"queryitems");
    m->addParam("contact",target,false);
    m->addParam("notify",id);
    Engine::enqueue(m);
    return true;
}


/*
 * ContactChatNotify
 */
// Check for timeout. Reset the timer if a notification is returned
ContactChatNotify::State ContactChatNotify::timeout(Time& time)
{
    if (m_paused) {
	if (m_paused > time.msec())
	    return None;
	m_paused = 0;
	return Paused;
    }
    if (m_inactive) {
	if (m_inactive > time.msec())
	    return None;
	m_inactive = 0;
	return Inactive;
    }
    return None;
}

// Send the notification
void ContactChatNotify::send(State state, ClientContact* c, MucRoom* room,
    MucRoomMember* member)
{
    const char* s = ::lookup(state,s_states);
    if (!s)
	return;
    if (c)
	c->sendChat(0,String::empty(),String::empty(),s);
    else if (room)
	room->sendChat(0,member ? member->m_name : String::empty(),String::empty(),s);
}

// Add or remove items from list. Notify active/composing if changed
void ContactChatNotify::update(ClientContact* c, MucRoom* room, MucRoomMember* member,
    bool empty, bool notify)
{
    if (!(c || room))
	return;
    const String& id = c ? c->toString() : (member ? member->toString() : room->toString());
    if (!id)
	return;
    ObjList* found = s_items.find(id);
    State st = Composing;
    if (empty) {
	if (!found)
	    return;
	found->remove();
	st = Active;
    }
    else {
	Time time;
	if (found) {
	    ContactChatNotify* item = static_cast<ContactChatNotify*>(found->get());
	    // Send composing if sent any other notification
	    notify = !(item->m_paused && item->m_inactive);
	    item->updateTimers(time);
	}
	else {
	    s_items.append(new ContactChatNotify(id,room != 0,member != 0,time));
	    notify = true;
	}
	// Make sure the logic is receiving timer notifications
	Client::setLogicsTick();
    }
    if (notify)
	send(st,c,room,member);
}

// Check timeouts. Send notifications
bool ContactChatNotify::checkTimeouts(ClientAccountList& list, Time& time)
{
    ObjList* o = s_items.skipNull();
    while (o) {
	ContactChatNotify* item = static_cast<ContactChatNotify*>(o->get());
	State state = item->timeout(time);
	if (state != None) {
	    // Send notification
	    // Remove the item if there is no chat for it
	    ClientContact* c = 0;
	    MucRoom* room = 0;
	    MucRoomMember* member = 0;
	    if (!item->m_mucRoom) {
		c = list.findContact(item->toString());
		if (c && !c->hasChat())
		    c = 0;
	    }
	    else if (item->m_mucMember) {
		room = list.findRoomByMember(item->toString());
		if (room) {
		    member = room->findMemberById(item->toString());
		    if (!member)
			room = 0;
		}
		if (room && !room->hasChat(member->toString()))
		    room = 0;
	    }
	    else {
		room = list.findRoom(item->toString());
		if (room && !room->hasChat(room->toString()))
		    room = 0;
	    }
	    if (c || room)
		item->send(state,c,room,member);
	    else {
		// Not found: remove from  list
		o->remove();
		o = o->skipNull();
		continue;
	    }
	}
	o = o->skipNext();
    }
    return 0 != s_items.skipNull();
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
    // Build account status
    AccountStatus::init();

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

// Save a contact into a configuration file.
bool ClientLogic::saveContact(Configuration& cfg, ClientContact* c, bool save)
{
    if (!c)
	return false;
    String sectName(c->uri());
    sectName.toLower();
    NamedList* sect = cfg.createSection(sectName);
    MucRoom* room = c->mucRoom();
    if (room) {
	sect->setParam("type","groupchat");
	sect->setParam("name",room->m_name);
	sect->setParam("password",room->m_password);
    }
    else
	sect->setParam("type","chat");
    sect->copyParams(c->m_params);
    sect->clearParam(YSTRING("group"));
    for (ObjList* o = c->groups().skipNull(); o; o = o->skipNext())
	sect->addParam("group",o->get()->toString(),false);
    sect->clearParam(YSTRING("internal"),'.');
    return !save || cfg.save();
}

// Delete a contact from a configuration file
bool ClientLogic::clearContact(Configuration& cfg, ClientContact* c, bool save)
{
    if (!c)
	return false;
    String sectName(c->uri());
    cfg.clearSection(sectName.toLower());
    return !save || cfg.save();
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
	    tmp = Client::self()->setVisible(p->name(),p->toBoolean(),true);
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
    ObjList* modules = name.substr(0,pos).split(',',false);
    String line = (active ? name.substr(pos + 1,posLine - pos - 1) : name.substr(posLine + 1));
    for (ObjList* o = modules->skipNull(); o; o = o->skipNext()) {
        Message* m = new Message("engine.debug");
	m->addParam("module",o->get()->toString());
	m->addParam("line",line);
	Engine::enqueue(m);
    }
    TelEngine::destruct(modules);
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
    s_accWizard = new AccountWizard(m_accounts);
    s_mucWizard = new JoinMucWizard(m_accounts);
    // Init chat states
    s_chatStates.addParam("composing","${sender} is typing ...");
    s_chatStates.addParam("paused","${sender} stopped typing");
    s_chatStates.addParam("gone","${sender} ended chat session");
    s_chatStates.addParam("inactive","${sender} is idle");
    s_chatStates.addParam("active","");
    // Account select params default value
    s_accProtoParamsSel.addParam("ip_transport","UDP");
}

// Destructor
DefaultLogic::~DefaultLogic()
{
    TelEngine::destruct(s_accWizard);
    TelEngine::destruct(s_mucWizard);
    TelEngine::destruct(m_accounts);
}

// main function which considering de value of the "action" parameter
// Handle actions from user interface
bool DefaultLogic::action(Window* wnd, const String& name, NamedList* params)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) action '%s' in window (%p,%s)",
	toString().c_str(),name.c_str(),wnd,wnd ? wnd->id().c_str() : "");

    // Translate actions from confirmation boxes
    // the window context specifies what action will be taken forward
    if (wnd && wnd->context() && (name == "ok") && (wnd->context() != "ok")) {
	bool ok = action(wnd,wnd->context(),params);
	if (ok)
	    wnd->hide();
	return ok;
    }

    // Show/hide widgets/windows
    bool widget = (name == YSTRING("display"));
    if (widget || name == YSTRING("show"))
	return params ? display(*params,widget,wnd) : false;

    // Start a call
    if (name == s_actionCall || name == YSTRING("callto")) {
	NamedList dummy("");
	if (!params)
	    params = &dummy;
	return callStart(*params,wnd,name);
    }

    // Start a call from an action specifying the target
    if (name.startsWith("callto:")) {
	NamedList dummy("");
	if (!params)
	    params = &dummy;
    	params->setParam("target",name.substr(7));
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
	NamedList dummy("");
	if (!params)
	    params = &dummy;
	params->setParam("digits",name.substr(6));
	return digitPressed(*params,wnd);
    }
    // New line
    if (name.startsWith("line:") && line(name.substr(5),wnd))
	return false;
    // Action taken when receiving a clear action
    if (name.startsWith("clear:") && name.at(6))
	return clearList(name.substr(6),wnd);
    // Delete a list/table item
    bool confirm = name.startsWith("deleteitemconfirm:");
    if (confirm || name.startsWith("deleteitem:")) {
	String list;
	int start = confirm ? 18 : 11;
	int pos = name.find(":",start);
	if (pos > 0)
	    return deleteItem(name.substr(start,pos - start),name.substr(pos + 1),wnd,confirm);
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

    // *** Specific action handlers
    if (handleChatContactAction(name,wnd) ||
	handleMucsAction(name,wnd,params) ||
	handleChatContactEditOk(name,wnd) ||
	handleChatRoomEditOk(name,wnd) ||
	handleFileTransferAction(name,wnd,params))
	return true;

    // *** MUC
    if (name == YSTRING("joinmuc_wizard")) {
	s_mucWizard->start();
	return true;
    }

    // *** Account management

    // Create a new account or edit an existing one
    bool newAcc = (name == YSTRING("acc_new"));
    if (newAcc || name == YSTRING("acc_edit") || name == s_accountList)
	return editAccount(newAcc,params,wnd);
    if (name == YSTRING("acc_new_wizard")) {
	s_accWizard->start();
	return true;
    }
    // User pressed ok button in account edit window
    if (name == YSTRING("acc_accept"))
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
	return acc ? ::loginAccount(this,acc->params(),login) : false;
    }
    login = name.startsWith(s_actionLogin + ":",false);
    if (login || name.startsWith(s_actionLogout + ":",false)) {
	ClientAccount* acc = 0;
	if (login)
	    acc = m_accounts->findAccount(name.substr(s_actionLogin.length() + 1));
	else
	    acc = m_accounts->findAccount(name.substr(s_actionLogout.length() + 1));
	return acc ? ::loginAccount(this,acc->params(),login) : false;
    }
    // Account status
    if (name.startsWith("setStatus")) {
	if (AccountStatus::setCurrent(name.substr(9).toLower()))
	    setAccountsStatus(m_accounts);
	return true;
    }

    // *** Address book actions

    // Call the current contact selection
    if (name == YSTRING("abk_call") || name == s_contactList)
	return callContact(params,wnd);
    // Add/edit contact
    bool newCont = (name == YSTRING("abk_new"));
    if (newCont || name == YSTRING("abk_edit"))
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
    if (name == YSTRING("abk_accept"))
	return acceptContact(params,wnd);

    // *** Call log management
    bool logCall = (name == YSTRING("log_call"));
    if (logCall || name == YSTRING("log_contact")) {
	String billid;
	if (Client::valid())
	    Client::self()->getSelect(s_logList,billid,wnd);
	if (!billid)
	    return false;
	if (logCall)
	    return callLogCall(billid,wnd);
	return callLogCreateContact(billid);
    }
    if (name == YSTRING("log_clear"))
	return callLogClear(s_logList,String::empty());

    // *** Miscellaneous

    // List item changed
    if (name == YSTRING("listitemchanged")) {
	if (!(params && Client::valid()))
	    return false;
	const String& list =  (*params)[YSTRING("list")];
	if (!list)
	    return false;
	const String& item = (*params)[YSTRING("item")];
	if (!item)
	    return false;
	if (list == s_accountList) {
	    NamedList tmp("");
	    if (!Client::self()->getTableRow(list,item,&tmp,wnd))
		return false;
	    String* enabled = tmp.getParam(YSTRING("check:enabled"));
	    if (enabled) {
		bool ok = enabled->toBoolean();
		ClientAccount* acc = m_accounts->findAccount(item);
		if (acc && ok != acc->startup()) {
		    acc->startup(ok);
		    acc->save(true,acc->params().getBoolValue(YSTRING("savepassword")));
		    // Update telephony account selector(s)
		    updateTelAccList(ok,acc);
		    setAdvancedMode();
		    if (Client::s_engineStarted) {
			if (ok)
			    setAccountStatus(m_accounts,acc);
			else
			    loginAccount(acc->params(),false);
		    }
		}
	    }
	}
	return false;
    }
    // OK actions
    if (name == YSTRING("ok")) {
	if (wnd && wnd->id() == s_wndMucInvite)
	    return handleMucInviteOk(wnd);
    }
    // Handle show window actions
    if (name.startsWith("action_show_"))
	Client::self()->setVisible(name.substr(12),true,true);
    if (name.startsWith("action_toggleshow_")) {
	String wnd = name.substr(18);
	return wnd && Client::self() &&
	    Client::self()->setVisible(wnd,!Client::self()->getVisible(wnd),true);
    }
    // Help commands
    if (name.startsWith("help:"))
	return help(name,wnd);
    // Hide windows
    if (name == YSTRING("button_hide") && wnd)
	return Client::self() && Client::self()->setVisible(wnd->toString(),false);
    // Show/hide messages
    bool showMsgs = (name == YSTRING("messages_show") || name == s_actionShowNotification ||
	name == s_actionShowInfo);
    if (showMsgs || name == YSTRING("messages_close")) {
	bool notif = (name == s_actionShowNotification); 
	if (notif || name == s_actionShowInfo) {
	    removeTrayIcon(notif ? YSTRING("notification") : YSTRING("info")); 
	    if (wnd && Client::valid())
		Client::self()->setVisible(wnd->id(),true,true);
	}
	return showNotificationArea(showMsgs,wnd);
    }
    // Dialog actions
    // Return 'true' to close the dialog
    bool dlgRet = false;
    if (handleDialogAction(name,dlgRet,wnd))
	return dlgRet;
    // Wizard actions
    if (s_accWizard->action(wnd,name,params) ||
	s_mucWizard->action(wnd,name,params))
	return true;
    ClientWizard* wiz = findTempWizard(wnd);
    if (wiz && wiz->action(wnd,name,params))
	return true;
    // Actions from notification area
    if (handleNotificationAreaAction(name,wnd))
	return true;
    if (name == YSTRING("textchanged"))
	return handleTextChanged(params,wnd);
    // Input password/credentials
    bool inputPwd = name.startsWith("loginpassword:");
    if (inputPwd || name.startsWith("logincredentials:"))
	return handleAccCredInput(wnd,name.substr(inputPwd ? 14 : 17),inputPwd);
    if (name == s_actionShowCallsList) {
	if (Client::valid()) {
	    Client::self()->ringer(true,false);
	    Client::self()->setVisible(YSTRING("mainwindow"),true,true);
	    activatePageCalls();
	    removeTrayIcon(YSTRING("incomingcall"));
	}
	return true;
    }
    if (name == s_actionPendingChat) {
	showPendingChat(m_accounts);
	return true;
    }
    // Quit
    if (name == YSTRING("quit")) {
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
		p.addParam(param,String::boolText(value));
	}
	TelEngine::destruct(obj);
	return Client::self()->setParams(&p);
    }

    // *** Channel actions
    // Hold
    if (name == s_actionHold) {
	if (!ClientDriver::self())
	    return false;
	bool ok = !active ? ClientDriver::self()->setActive() :
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
		return Client::valid() && Client::self()->setVisible("help",false);
	}
	return Client::valid() && Client::self()->setVisible(what,active,true);
    }

    // Wizard toggle
    if (s_accWizard->toggle(wnd,name,active) ||
	s_mucWizard->toggle(wnd,name,active))
	return true;

    // Visibility: update checkable widgets having the same name as the window
    if (wnd && name == YSTRING("window_visible_changed")) {
	if (!Client::valid())
	    return false;
	const char* yText = String::boolText(active);
	const char* nText = String::boolText(!active);
	NamedList p("");
	p.addParam("check:toggle_show_" + wnd->toString(),yText);
	p.addParam("check:action_show_" + wnd->toString(),yText);
	if (wnd->id() == s_wndAccount || s_accWizard->isWindow(wnd)) {
	    p.addParam("active:acc_new",nText);
	    p.addParam("active:acc_new_wizard",nText);
	    if (active)
		fillAccEditActive(p,false);
	    else
		fillAccEditActive(p,0 != selectedAccount(*m_accounts));
	    // Enable/disable account edit in notification area
	    NamedList params("messages");
	    NamedList* p = new NamedList("");
	    p->addParam("active:messages_acc_edit",String::boolText(!active));
	    params.addParam(new NamedPointer("applyall",p));
	    Client::self()->setParams(&params);
	}
	else if (wnd->id() == s_wndAddrbook) {
	    p.addParam("active:abk_new",nText);
	    fillContactEditActive(p,!active);
	    fillLogContactActive(p,!active);
	}
	else if (s_mucWizard->isWindow(wnd)) {
	    p.addParam("active:joinmuc_wizard",nText);
	    p.addParam("active:" + s_chatRoomNew,nText);
	}
	else if (wnd->id() == ClientContact::s_mucsWnd) {
	    // Hidden: destroy/close all MUCS, close log sessions
	    if (!active) {
		// Remove from pending chat
		NamedList p("");
		Client::self()->getOptions(ClientContact::s_dockedChatWidget,&p,wnd);
		unsigned int n = p.length();
		for (unsigned int i = 0; i < n; i++) {
		    NamedString* ns = p.getParam(i);
		    if (ns && ns->name())
			removePendingChat(ns->name());
		}
		ObjList* o = m_accounts->accounts().skipNull();
		for (; o; o = o->skipNext()) {
		    ClientAccount* acc = static_cast<ClientAccount*>(o->get());
		    ListIterator iter(acc->mucs());
		    for (GenObject* gen = 0; 0 != (gen = iter.get());) {
			MucRoom* room = static_cast<MucRoom*>(gen);
			logCloseMucSessions(room);
			if (room->local() || room->remote())
			    clearRoom(room);
			else
			    TelEngine::destruct(room);
		    }
		    if (acc->resource().online())
			updateChatRoomsContactList(true,acc);
		}
	    }
	}
	else if (wnd->id() == ClientContact::s_dockedChatWnd) {
	    // Clear chat pages when hidden
	    // Close chat sessions
	    if (!active) {
		if (!s_changingDockedChat) {
		    NamedList p("");
		    Client::self()->getOptions(ClientContact::s_dockedChatWidget,&p,wnd);
		    unsigned int n = p.length();
		    for (unsigned int i = 0; i < n; i++) {
			NamedString* ns = p.getParam(i);
			if (ns && ns->name()) {
			    removePendingChat(ns->name());
			    logCloseSession(m_accounts->findContact(ns->name()));
			}
		    }
		}
		Client::self()->clearTable(ClientContact::s_dockedChatWidget,wnd);
	    }
	}
	else if (wnd->id().startsWith(ClientContact::s_chatPrefix)) {
	    // Close chat session if not active and not destroyed due
	    //  to docked chat changes
	    if (!(active || s_changingDockedChat))
		logCloseSession(m_accounts->findContact(wnd->context()));
	}
	else {
	    ClientWizard* wiz = !active ? findTempWizard(wnd) : 0;
	    if (wiz)
		s_tempWizards.remove(wnd->id());
	}
	Client::self()->setParams(&p);
	return true;
    }
    // Window active changed
    if (wnd && name == YSTRING("window_active_changed")) {
	if (active) {
	    // Remove contact from pending when activated
	    if (wnd->id() == ClientContact::s_dockedChatWnd) {
		String sel;
		if (Client::self()->getSelect(ClientContact::s_dockedChatWidget,sel,wnd))
		    removePendingChat(sel,m_accounts);
	    }
	    else if (wnd->id().startsWith(ClientContact::s_chatPrefix))
		removePendingChat(wnd->context());
	}
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
    if (name == YSTRING("log_events_debug")) {
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
    if (name == YSTRING("acc_showadvanced")) {
	if (!Client::valid())
	    return false;
	// Select the page(s)
	String proto;
	if (active) {
	    bool wiz = s_accWizard->isWindow(wnd);
	    Client::self()->getSelect(wiz ? s_accWizProtocol : s_accProtocol,proto);
	}
	toggle(wnd,"selectitem:acc_proto_advanced:acc_proto_advanced_" + getProtoPage(proto),true);
	// Keep all in sync
	Client::self()->setCheck(name,active);
	// Save it
	Client::s_settings.setValue("client",name,String::boolText(active));
	Client::save(Client::s_settings);
	return true;
    }
    if (name == YSTRING("advanced_mode")) {
	setAdvancedMode(&active);
	Client::s_settings.setValue(YSTRING("client"),name,String::boolText(active));
	Client::save(Client::s_settings);
	return true;
    }

    // Commands
    if (name.startsWith("command:") && name.at(8))
	return command(name.substr(8) + (active ? " on" : " off"),wnd);

    // Handle show window actions
    if (name.startsWith("action_show_"))
	Client::self()->setVisible(name.substr(12),active,true);

    // Chat log options
    if (active) {
	int v = lookup(name,s_chatLogDict);
	if (v == ChatLogSaveAll || v == ChatLogSaveUntilLogout || v == ChatLogNoSave) {
	    s_chatLog = (ChatLogEnum)v;
	    Client::s_settings.setValue(YSTRING("client"),"logchat",name);
	    Client::s_settings.save();
	}
    }

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
	if (!Client::valid())
	    return false;
	ClientAccount* a = item ? m_accounts->findAccount(item) : 0;
	NamedList p("");
	fillAccLoginActive(p,a);
	fillAccEditActive(p,!item.null() && !Client::self()->getVisible(s_wndAccount));
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

    if (name == s_chatContactList) {
	enableChatActions(item ? m_accounts->findAnyContact(item) : 0);
	return true;
    }

    if (name == s_mainwindowTabs) {
	ClientContact* c = 0;
	if (item == YSTRING("tabChat"))
	    c = selectedChatContact(*m_accounts,wnd);
	else if (isPageCallsActive(wnd,false)) {
	    if (Client::valid())
		Client::self()->ringer(true,false);
	    removeTrayIcon(YSTRING("incomingcall"));
	}
	enableChatActions(c,false);
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

    // Page changed in telephony tab
    if (name == YSTRING("framePages")) {
    	if (isPageCallsActive(wnd,true)) {
	    Client::self()->ringer(true,false);
	    removeTrayIcon(YSTRING("incomingcall"));
	}
	return false;
    }

    // Avoid sync with other contact add window
    if (name == s_chatAccount)
	return false;

    // keep the item in sync in all windows
    // if the same object is present in more windows, we will synchronise all of them
    if (Client::self())
	Client::self()->setSelect(name,item,0,wnd);

    // Enable specific actions when a channel is selected
    if (name == s_channelList) {
    	if (isPageCallsActive(wnd,true)) {
	    Client::self()->ringer(true,false);
	    removeTrayIcon(YSTRING("incomingcall"));
	}
	updateSelectedChannel(&item);
	return true;
    }
    // when an account is selected, the choice of protocol must be cleared
    // when a protocol is chosen, the choice of account must be cleared
    bool acc = (name == YSTRING("account"));
    if (acc || name == YSTRING("protocol")) {
	if (Client::s_notSelected.matches(item))
	    return true;
	if (acc)
	    return Client::self()->setSelect(YSTRING("protocol"),s_notSelected,wnd);
	return Client::self()->setSelect(YSTRING("account"),s_notSelected,wnd);
    }

    // Handle protocol/providers select in account edit/add or wizard
    if (handleProtoProvSelect(wnd,name,item))
	return true;

    // Wizard select
    if (s_accWizard->select(wnd,name,item,text) ||
	s_mucWizard->select(wnd,name,item,text))
	return true;

    // Specific select handlers
    if (handleMucsSelect(name,item,wnd,text))
	return true;

    // Selection changed in docked (room) chat
    if (name == ClientContact::s_dockedChatWidget) {
	if (item)
	    removePendingChat(item,m_accounts);
	return true;
    }

    // No more notifications: remove the tray icon
    if (name == YSTRING("messages")) {
	if (!item) {
	    removeTrayIcon(YSTRING("notification"));
	    removeTrayIcon(YSTRING("info"));
	}
	return true;
    }

    // Selection changed in 'callto': do nothing. Just return true to avoid enqueueing ui.event
    if (name == YSTRING("callto"))
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
	    if (Client::valid()) {
		bool ok = value.toBoolean();
		changed = Client::self()->setBoolOpt(opt,ok,update);
		// Special care for some controls
		if (opt == Client::OptKeypadVisible)
		    Client::self()->setShow(YSTRING("keypad"),ok);
		if (changed && opt == Client::OptDockedChat) {
		    // Change contacts docked chat
		    s_changingDockedChat = true;
		    for (ObjList* o = m_accounts->accounts().skipNull(); o; o = o->skipNext()) {
			ClientAccount* a = static_cast<ClientAccount*>(o->get());
			if (!a->hasChat())
			    continue;
			for (ObjList* oo = a->contacts().skipNull(); oo; oo = oo->skipNext()) {
			    ClientContact* c = static_cast<ClientContact*>(oo->get());
			    changeDockedChat(*c,ok);
			}
		    }
		    s_changingDockedChat = false;
		}
		// Clear notifications if disabled
		if (opt == Client::OptNotifyChatState && !ok)
		    ContactChatNotify::clear();
	    }
	}
    }
    else if (param == YSTRING("username") || param == YSTRING("callerid") ||
	param == YSTRING("domain")) {
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

// Process an IM message
bool DefaultLogic::imIncoming(Message& msg)
{
    bool stopLogic = false;
    return defaultMsgHandler(msg,Client::MsgExecute,stopLogic);
}

// Call execute handler called by the client.
bool DefaultLogic::callIncoming(Message& msg, const String& dest)
{
    if (!Client::self())
	return false;
    const String& fmt = msg[YSTRING("format")];
    if (!fmt || fmt != YSTRING("data")) {
	// Set params for incoming google voice call
	if (msg[YSTRING("module")] == YSTRING("jingle")) {
	    URI uri(msg[YSTRING("callername")]);
	    if (uri.getHost() == YSTRING("voice.google.com")) {
		msg.setParam("dtmfmethod","rfc2833");
		msg.setParam("jingle_flags","noping");
	    }
	}
	return Client::self()->buildIncomingChannel(msg,dest);
    }
    if (!(msg.userData() && ClientDriver::self() && Client::self()))
	return false;
    CallEndpoint* peer = static_cast<CallEndpoint*>(msg.userData());
    if (!peer)
	return false;
    const String& file = msg[YSTRING("file_name")];
    if (!file)
	return false;
    const String& oper = msg[YSTRING("operation")];
    if (oper != YSTRING("receive"))
	return false;
    Message m(msg);
    m.userData(msg.userData());
    m.setParam("callto","dumb/");
    if (!Engine::dispatch(m))
	return false;
    String targetid = m[YSTRING("targetid")];
    if (!targetid)
	return false;
    msg.setParam("targetid",targetid);
    static const String extra = "targetid,file_name,file_size,file_md5,file_time";
    const String& contact = msg[YSTRING("callername")];
    const String& account = msg[YSTRING("in_line")];
    ClientAccount* a = account ? m_accounts->findAccount(account) : 0;
    ClientContact* c = a ? a->findContactByUri(contact) : 0;
    NamedList rows("");
    NamedList* upd = buildNotifArea(rows,"incomingfile",account,contact,"Incoming file",extra);
    upd->copyParams(msg,extra);
    String text;
    text << "Incoming file '" << file << "'";
    String buf;
    if (c)
	buildContactName(buf,*c);
    else
	buf = contact;
    text.append(buf,"\r\nContact: ");
    text.append(account,"\r\nAccount: ");
    upd->addParam("text",text);
    showNotificationArea(true,Client::self()->getWindow(s_wndMain),&rows);
    return true;
}

// Start an outgoing call
bool DefaultLogic::callStart(NamedList& params, Window* wnd, const String& cmd)
{
    if (!(Client::self() && fillCallStart(params,wnd)))
	return false;
    String target;
    const String& ns = params[YSTRING("target")];
    if (cmd == s_actionCall) {
	// Check google voice target on gmail accounts
	String account = params.getValue(YSTRING("account"),params.getValue(YSTRING("line")));
	if (account && isGmailAccount(m_accounts->findAccount(account))) {
	    // Allow calling user@domain
	    int pos = ns.find('@');
	    bool valid = (pos > 0) && (ns.find('.',pos + 2) >= pos);
	    if (!valid) {
		target = ns;
		Client::fixPhoneNumber(target,"().- ");
	    }
	    if (target) {
		target = target + "@voice.google.com";
		params.addParam("ojingle_version","0");
		params.addParam("ojingle_flags","noping");
		params.addParam("redirectcount","5");
		params.addParam("checkcalled",String::boolText(false));
		params.addParam("dtmfmethod","rfc2833");
		String callParams = params[YSTRING("call_parameters")];
		callParams.append("redirectcount,checkcalled,dtmfmethod,ojingle_version,ojingle_flags",",");
		params.setParam("call_parameters",callParams);
	    }
	    else if (!valid) {
		showError(wnd,"Incorrect number");
		Debug(ClientDriver::self(),DebugNote,
		    "Failed to call: invalid gmail number '%s'",params.getValue("target"));
		return false;
	    }
	}
    }
    // Delete the number from the "callto" widget and put it in the callto history
    if (ns) {
	Client::self()->delTableRow(s_calltoList,ns);
	Client::self()->addOption(s_calltoList,ns,true);
	Client::self()->setText(s_calltoList,"");
    }
    if (target)
	params.setParam("target",target);
    if (!Client::self()->buildOutgoingChannel(params))
	return false;
    // Activate the calls page
    activatePageCalls();
    return true;
}

// function which is called when a digit is pressed
bool DefaultLogic::digitPressed(NamedList& params, Window* wnd)
{
    if (!Client::valid())
	return false;

    // Send digits (DTMF) on active channel
    // or add them to 'callto' box
    const String& digits = params[YSTRING("digits")];
    if (!digits)
	return false;
    if (Client::self()->emitDigits(digits))
	return true;
    String target;
    if (isE164(digits) && Client::self()->getText(YSTRING("callto"),target)) {
	target += digits;
	if (Client::self()->setText(YSTRING("callto"),target)) {
	    Client::self()->setFocus(YSTRING("callto"),false);
	    return true;
	}
    }
    return false;
}

// Called when the user wants to add an account or edit an existing one
bool DefaultLogic::editAccount(bool newAcc, NamedList* params, Window* wnd)
{
    return internalEditAccount(newAcc,0,params,wnd);
}

// Called when the user wants to save account data
bool DefaultLogic::acceptAccount(NamedList* params, Window* wnd)
{
    if (!(Client::valid() && wnd))
	return false;
    NamedList p("");
    if (!getAccount(wnd,p,*m_accounts))
	return false;
    const String& replace = wnd ? wnd->context() : String::empty();
    if (replace) {
	ClientAccount* edit = m_accounts->findAccount(replace);
	if (edit) {
	    ClientAccount* acc = m_accounts->findAccount(p);
	    if (acc && acc != edit) {
		// Don't know what to do: replace the duplicate or rename the editing one
		showAccDupError(wnd);
		return false;
	    }
	}
    }
    if (!updateAccount(p,true,replace))
	return false;
    // Hide the window. Save some settings
    Client::self()->setVisible(wnd->toString(),false);
    Client::s_settings.setValue(YSTRING("client"),"acc_protocol",p["protocol"]);
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
    Engine::enqueue(userLogin(acc,false));
    // Delete from memory and UI. Save the accounts file
    removeAccNotifications(acc);
    closeAccPasswordWnd(account);
    closeAccCredentialsWnd(account);
    clearAccountContacts(*acc);
    updateChatRoomsContactList(false,acc);
    Client::self()->delTableRow(s_account,account);
    Client::self()->delTableRow(s_accountList,account);
    acc->save(false);
    String error;
    if (!acc->clearDataDir(&error) && error)
	notifyGenericError(error,account);
    m_accounts->removeAccount(account);
    return true;
}

// Add/set an account
bool DefaultLogic::updateAccount(const NamedList& account, bool login, bool save)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) updateAccount(%s,%s,%s)",
	toString().c_str(),account.c_str(),String::boolText(login),String::boolText(save));
    // Load account status if not already done
    AccountStatus::load();
    if (!Client::valid() || account.null())
	return false;
    return updateAccount(account,false,String::empty(),true);
}

// Login/logout an account
bool DefaultLogic::loginAccount(const NamedList& account, bool login)
{
    DDebug(ClientDriver::self(),DebugAll,"Logic(%s) loginAccount(%s,%s)",
	toString().c_str(),account.c_str(),String::boolText(login));

    Message* m = 0;
    ClientAccount* acc = m_accounts->findAccount(account);
    ClientResource::Status newStat = ClientResource::Unknown;
    if (acc) {
	m = userLogin(acc,login);
	if (login) {
	    if (acc->resource().offline() || !isTelProto(acc->protocol()))
		newStat = ClientResource::Connecting;
	}
	else {
	    newStat = ClientResource::Offline;
	    // Don't show a notification when disconnected
	    if (!login)
	        acc->m_params.setParam("internal.nologinfail",String::boolText(true));
	}
    }
    else {
	m = Client::buildMessage("user.login",account,login ? "login" : "logout");
	if (login)
	    m->copyParams(account);
	else
	    m->copyParams(account,YSTRING("protocol"));
    }
    Engine::enqueue(m);
    if (newStat != ClientResource::Unknown) {
	acc->resource().setStatus(newStat);
	acc->resource().setStatusText("");
        updateAccountStatus(acc,m_accounts);
    }
    return true;
}

// Add/update a contact
bool DefaultLogic::updateContact(const NamedList& params, bool save, bool update)
{
    if (!(Client::valid() && (save || update) && params))
	return false;
    const String& target = params[YSTRING("target")];
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
	const String& name = params[YSTRING("name")];
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
    return ok;
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
	Client::self()->getText(YSTRING("abk_name"),name,false,wnd);
	if (!name)
	    SET_ERR_BREAK("A contact name must be specified");
	Client::self()->getText(YSTRING("abk_target"),target,false,wnd);
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
	p.addParam("abk_target",params ? params->getValue(YSTRING("target")) : "");
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
    String id = c->toString();
    m_accounts->localContacts()->removeContact(id);
    Client::save(Client::s_contacts);
    return true;
}

// Add/set account providers data
bool DefaultLogic::updateProviders(const NamedList& provider, bool save, bool update)
{
    if (!(save || update))
	return false;
    if (provider.null() || !provider.getBoolValue(YSTRING("enabled"),true))
	return false;
    if (save && !Client::save(Client::s_providers))
	return false;
    return updateProvidersItem(0,s_accProviders,provider);
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
    const String& target = (*params)[YSTRING("number/uri")];
    if (!target)
	return false;
    bool call = true;
    String account;
    String proto;
    String cmd;
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
	    Client::self()->getSelect(s_account,account);
	    call = !account.null();
	    if (call)
		cmd = s_actionCall;
	}
    }
    if (call) {
	NamedList p("");
	p.addParam("line",account,false);
	p.addParam("account",account,false);
	p.addParam("target",target);
	p.addParam("protocol",proto,false);
	return callStart(p,0,cmd);
    }
    Client::self()->setText(s_calltoList,target);
    activatePageCalls();
    return true;
}

// Update the call log history
bool DefaultLogic::callLogUpdate(const NamedList& params, bool save, bool update)
{
    if (!(save || update))
	return false;
    String* bid = params.getParam(YSTRING("billid"));
    const String& id = bid ? (const String&)(*bid) : params[YSTRING("id")];
    if (!id)
	return false;
    if (Client::valid() && update) {
	// Remember: directions are opposite of what the user expects
	const String& dir = params[YSTRING("direction")];
	bool outgoing = (dir == YSTRING("incoming"));
	if (outgoing || dir == YSTRING("outgoing")) {
	    // Skip if there is no remote party
	    const String& party = cdrRemoteParty(params,outgoing);
	    if (party) {
		NamedList p("");
		String time;
		Client::self()->formatDateTime(time,(unsigned int)params.getDoubleValue(YSTRING("time")),
		    "yyyy.MM.dd hh:mm",false);
		p.addParam("party",party);
		p.addParam("party_image",Client::s_skinPath + (outgoing ? "up.png" : "down.png"));
		p.addParam("time",time);
		time.clear();
		Client::self()->formatDateTime(time,(unsigned int)params.getDoubleValue(YSTRING("duration")),
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
	    NamedString* dir = sect ? sect->getParam(YSTRING("direction")) : 0;
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
bool DefaultLogic::callLogCall(const String& billid, Window* wnd)
{
    NamedList* sect = Client::s_history.getSection(billid);
    if (!sect)
	return false;
    const String& party = cdrRemoteParty(*sect);
    return party && action(wnd,"callto:" + party);
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
    if (name == YSTRING("help:home"))
	page = 0;
    else if (name == YSTRING("help:prev"))
	page--;
    else if (name == YSTRING("help:next"))
	page++;
    else if (name.startsWith("help:")) {
	page = name.substr(5).toInteger(page);
	show = true;
    }
    if (page < 0)
	page = 0;

    // Get the help file from the help folder
    String helpFile = Engine::config().getValue(YSTRING("client"),"helpbase");
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
	    Client::self()->setText(YSTRING("help_text"),helpText,true,help);
	    help->context(String(page));
	    if (show)
		Client::self()->setVisible(YSTRING("help"),true);
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
    NamedList* sect = Client::s_calltoHistory.getSection(YSTRING("calls"));
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
    NamedString* action = msg.getParam(YSTRING("action"));
    if (!action)
	return false;

    // block until client finishes initialization
    while (!Client::self()->initialized())
	Thread::idle();
    // call the appropiate function for the given action
    Window* wnd = Client::getWindow(msg.getValue(YSTRING("window")));
    if (*action == YSTRING("set_status"))
	return Client::self()->setStatusLocked(msg.getValue(YSTRING("status")),wnd);
    else if (*action == YSTRING("add_log"))
	return Client::self()->addToLog(msg.getValue(YSTRING("text")));
    else if (*action == YSTRING("show_message")) {
	Client::self()->lockOther();
	bool ok = Client::openMessage(msg.getValue(YSTRING("text")),
	    Client::getWindow(msg.getValue(YSTRING("parent"))),msg.getValue(YSTRING("context")));
	Client::self()->unlockOther();
	return ok;
    }
    else if (*action == YSTRING("show_confirm")) {
	Client::self()->lockOther();
	bool ok = Client::openConfirm(msg.getValue(YSTRING("text")),
	    Client::getWindow(msg.getValue(YSTRING("parent"))),msg.getValue(YSTRING("context")));
	Client::self()->unlockOther();
	return ok;
    }
    else if (*action == YSTRING("notify_error")) {
	const String* text = msg.getParam(YSTRING("text"));
	if (TelEngine::null(text))
	    return false;
	Client::self()->lockOther();
	notifyGenericError(*text,msg.getValue(YSTRING("account")),
	    msg.getValue(YSTRING("contact")),msg.getValue(YSTRING("title")));
	Client::self()->unlockOther();
	return true;
    }
    // get the name of the widget for which the action is meant
    String name(msg.getValue(YSTRING("name")));
    if (name.null())
	return false;
    DDebug(ClientDriver::self(),DebugAll,"UI action '%s' on '%s' in %p",
	action->c_str(),name.c_str(),wnd);
    bool ok = false;
    Client::self()->lockOther();
    if (*action == YSTRING("set_text"))
	ok = Client::self()->setText(name,msg.getValue(YSTRING("text")),false,wnd);
    else if (*action == YSTRING("set_toggle"))
	ok = Client::self()->setCheck(name,msg.getBoolValue(YSTRING("active")),wnd);
    else if (*action == YSTRING("set_select"))
	ok = Client::self()->setSelect(name,msg.getValue(YSTRING("item")),wnd);
    else if (*action == YSTRING("set_active"))
	ok = Client::self()->setActive(name,msg.getBoolValue(YSTRING("active")),wnd);
    else if (*action == YSTRING("set_focus"))
	ok = Client::self()->setFocus(name,msg.getBoolValue(YSTRING("select")),wnd);
    else if (*action == YSTRING("set_visible"))
	ok = Client::self()->setShow(name,msg.getBoolValue(YSTRING("visible")),wnd);
    else if (*action == YSTRING("has_option"))
	ok = Client::self()->hasOption(name,msg.getValue(YSTRING("item")),wnd);
    else if (*action == YSTRING("add_option"))
	ok = Client::self()->addOption(name,msg.getValue(YSTRING("item")),
	    msg.getBoolValue(YSTRING("insert")),msg.getValue(YSTRING("text")),wnd);
    else if (*action == YSTRING("del_option"))
	ok = Client::self()->delTableRow(name,msg.getValue(YSTRING("item")),wnd);
    else if (*action == YSTRING("get_text")) {
	String text;
	ok = Client::self()->getText(name,text,false,wnd);
	if (ok)
	    msg.retValue() = text;
    }
    else if (*action == YSTRING("get_toggle")) {
	bool check;
	ok = Client::self()->getCheck(name,check,wnd);
	if (ok)
	    msg.retValue() = check;
    }
    else if (*action == YSTRING("get_select")) {
	String item;
	ok = Client::self()->getSelect(name,item,wnd);
	if (ok)
	    msg.retValue() = item;
    }
    else if (*action == YSTRING("window_show"))
	ok = Client::setVisible(name,true);
    else if (*action == YSTRING("window_hide"))
	ok = Client::setVisible(name,false);
    else if (*action == YSTRING("window_popup"))
	ok = Client::openPopup(name,&msg,Client::getWindow(msg[YSTRING("parent")]));
    Client::self()->unlockOther();
    return ok;
}

// Process call.cdr message
bool DefaultLogic::handleCallCdr(Message& msg, bool& stopLogic)
{
    if (!Client::self())
	return false;
    if (msg[YSTRING("operation")] != YSTRING("finalize"))
	return false;
    if (!msg[YSTRING("chan")].startsWith("client/",false))
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
    if (!Client::valid())
	return false;
    if (Client::self()->postpone(msg,Client::UserNotify,false)) {
	stopLogic = true;
	return false;
    }
    const String& account = msg[YSTRING("account")];
    if (!account)
	return false;
    bool reg = msg.getBoolValue(YSTRING("registered"));
    const String& reasonStr = msg[YSTRING("reason")];
    const char* reason = reasonStr;
    // Notify wizards
    s_mucWizard->handleUserNotify(account,reg,reason);
    bool save = s_accWizard->handleUserNotify(account,reg,reason);
    bool fromWiz = save;
    ClientAccount* acc = m_accounts->findAccount(account);
    if (!acc)
	return false;
    // Always remove roster request notification when account status changed
    removeNotifArea(YSTRING("rosterreqfail"),account);
    // Notify status
    String txt = reg ? "Registered" : "Unregistered";
    txt << " account " << account;
    txt.append(reason," reason: ");
    Client::self()->setStatusLocked(txt);
    int stat = ClientResource::Online;
    String regStat;
    if (reg) {
	// Remove account failure notification if still there
	removeNotifArea(YSTRING("loginfail"),account);
	closeAccPasswordWnd(account);
	closeAccCredentialsWnd(account);
	// Clear account register option
	NamedString* opt = acc->m_params.getParam(YSTRING("options"));
	if (opt) {
	    ObjList* list = opt->split(',',false);
	    ObjList* o = list->find(YSTRING("register"));
	    if (o) {
		save = true;
		o->remove();
		opt->clear();
		opt->append(list,",");
		if (opt->null())
		    acc->m_params.clearParam(opt);
	    }
	    TelEngine::destruct(list);
 	}
	acc->resource().m_id = msg.getValue(YSTRING("instance"));
	// Set account status from pending data
	int tmp = acc->params().getIntValue(YSTRING("internal.status.status"),ClientResource::s_statusName);
	if (tmp > stat)
	    stat = tmp;
	regStat = acc->params().getValue(YSTRING("internal.status.text"));
	// Update chat accounts. Request MUCs
	if (acc->hasChat()) {
	    updateChatAccountList(account,true);
	    Engine::enqueue(acc->userData(false,"chatrooms"));
	    // Auto join rooms
	    for (ObjList* o = acc->mucs().skipNull(); o; o = o->skipNext()) {
		MucRoom* r = static_cast<MucRoom*>(o->get());
		if (r->m_params.getBoolValue(YSTRING("autojoin")) &&
		    checkGoogleRoom(r->uri()))
		    joinRoom(r);
	    }
	}
    }
    else {
	bool noFail = acc->params().getBoolValue(YSTRING("internal.nologinfail"));
	bool reConn = acc->params().getBoolValue(YSTRING("internal.reconnect"));
	// Show login failure message if not requested by the user
	if (!(noFail || reConn)) {
	    const String& error = msg[YSTRING("error")];
	    bool noAuth = isNoAuth(reasonStr,error);
	    String text(noAuth ? "Login failed for account '" : "Failed to connect account '");
	    text << account << "'";
	    if (reasonStr || error) {
		text << "\r\nReason: ";
		if (reasonStr) {
		    text << reasonStr;
		    if (error && reasonStr != error)
			text << " (" << error << ")";
		}
		else
		    text << error;
	    }
	    if (!(noAuth && getAccCredentialsWnd(acc->params(),true,text))) {
		NamedList rows("");
		NamedList* upd = buildNotifArea(rows,"loginfail",account,String::empty(),"Login failure");
		upd->addParam("text",text);
		// Enable/disable account edit
		const char* ok = String::boolText(!Client::self()->getVisible(s_wndAccount));
		upd->addParam("active:messages_acc_edit",ok);
		showNotificationArea(true,Client::self()->getWindow(s_wndMain),&rows);
	    }
	    else {
	    	// Remove account failure notification if still there
		removeNotifArea(YSTRING("loginfail"),account);
	    }
	}
	if (msg.getBoolValue(YSTRING("autorestart")))
	    stat = ClientResource::Connecting;
	else {
	    if (!reConn) {
		stat = ClientResource::Offline;
		if (s_chatLog == ChatLogSaveUntilLogout)
		    logClearAccount(account);
	    }
	    else {
		stat = ClientResource::Connecting;
		acc->m_params.clearParam(YSTRING("internal.reconnect"));
		// Re-connect the account
		Message* m = userLogin(acc,true);
		addAccPendingStatus(*m,acc);
		Engine::enqueue(m);
		// Clear the reason to avoid displaying it (we requested the disconnect)
		reason = 0;
	    }
	    // Reset resource name to configured
	    acc->resource().m_id = acc->m_params.getValue(YSTRING("resource"));
	}
	clearAccountContacts(*acc);
	setOfflineMucs(acc);
	// Remove from chat accounts
	if (acc->hasChat())
	    updateChatAccountList(account,false);
    }
    // (Un)Load chat rooms
    updateChatRoomsContactList(reg,acc);
    // Clear some internal params
    acc->m_params.clearParam(YSTRING("internal.nologinfail"));
    if (stat != ClientResource::Connecting)
	acc->m_params.clearParam(YSTRING("internal.status"),'.');
    bool changed = acc->resource().setStatus(stat);
    changed = acc->resource().setStatusText(reg ? regStat.c_str() : reason) || changed;
    if (changed)
	updateAccountStatus(acc,m_accounts);
    else if (!reg)
	PendingRequest::clear(acc->toString());
    if (save)
	acc->save(true,acc->params().getBoolValue(YSTRING("savepassword")));
    // Update telephony account selector(s)
    updateTelAccList(acc->startup() && reg,acc);
    setAdvancedMode();
    // Added from wizard
    // Update account status to server: notify presence and request roster or
    //  disconnect it of global presence is 'offline'
    if (fromWiz) {
	if (AccountStatus::current() &&
	    AccountStatus::current()->status() != ClientResource::Offline) {
	    if (!isTelProto(acc->protocol())) {
		Message* m = Client::buildNotify(true,acc->toString(),
		    acc->resource(false));
		Engine::enqueue(m);
		queryRoster(acc);
	    }
	}
	else
	    setAccountStatus(m_accounts,acc);
    }
    return false;
}

// Process user.roster message
bool DefaultLogic::handleUserRoster(Message& msg, bool& stopLogic)
{
    if (!Client::valid() || Client::isClientMsg(msg))
	return false;
    const String& oper = msg[YSTRING("operation")];
    if (!oper)
	return false;
    // Postpone message processing
    if (Client::self()->postpone(msg,Client::UserRoster)) {
	stopLogic = true;
	return false;
    }
    const String& account = msg[YSTRING("account")];
    ClientAccount* a = account ? m_accounts->findAccount(account) : 0;
    if (!a)
	return false;
    if (oper == YSTRING("error") || oper == YSTRING("queryerror") ||
	oper == YSTRING("result")) {
	showUserRosterNotification(a,oper,msg,msg[YSTRING("contact")]);
	return false;
    }
    bool remove = (oper != YSTRING("update"));
    if (remove && oper != YSTRING("delete"))
	return false;
    int n = msg.getIntValue(YSTRING("contact.count"));
    if (n < 1)
	return false;
    bool queryRsp = msg.getBoolValue(YSTRING("queryrsp"));
    if (queryRsp)
	removeNotifArea(YSTRING("rosterreqfail"),account);
    ObjList removed;
    NamedList chatlist("");
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
	    if (!queryRsp)
		showUserRosterNotification(a,oper,msg,uri);
	    removed.append(a->removeContact(id,false));
	    continue;
	}
	pref << ".";
	// Add/update contact
	const char* cName = msg.getValue(pref + "name",uri);
	bool newContact = (c == 0);
	bool changed = newContact;
	if (c)
	    changed = setChangedString(c->m_name,cName) || changed;
	else {
	    c = a->appendContact(id,cName,uri);
	    if (!c)
		continue;
	}
	const String& sub = msg[pref + "subscription"];
	changed = setChangedString(c->m_subscription,sub) || changed;
	// Get groups
	changed = c->setGroups(msg,pref + "group") || changed;
	if (changed) {
	    // Update info window if displayed
	    updateContactInfo(c);
	    // Show update notification
	    if (!queryRsp)
		showUserRosterNotification(a,oper,msg,uri,newContact);
	}
	if (!(changed && a->hasChat()))
	    continue;
	NamedList* p = new NamedList(c->toString());
	fillChatContact(*p,*c,true,newContact);
	chatlist.addParam(new NamedPointer(c->toString(),p,String::boolText(true)));
	if (c->hasChat())
	    c->updateChatWindow(*p,"Chat [" + c->m_name + "]");
    }
    // Update UI
    for (ObjList* o = removed.skipNull(); o; o = o->skipNext())
	contactDeleted(*static_cast<ClientContact*>(o->get()));
    Client::self()->updateTableRows(s_chatContactList,&chatlist,false);
    return true;
}

// Process resource.notify message
bool DefaultLogic::handleResourceNotify(Message& msg, bool& stopLogic)
{
    if (!Client::valid() || Client::isClientMsg(msg))
	return false;
    const String& contact = msg[YSTRING("contact")];
    if (!contact)
	return false;
    const String& oper = msg[YSTRING("operation")];
    if (!oper)
	return false;
    // Postpone message processing
    if (Client::self()->postpone(msg,Client::ResourceNotify)) {
	stopLogic = true;
	return false;
    }
    const String& account = msg[YSTRING("account")];
    ClientAccount* a = account ? m_accounts->findAccount(account) : 0;
    if (!a)
	return false;
    const String& inst = msg[YSTRING("instance")];
    if (msg.getBoolValue(YSTRING("muc")))
	return handleMucResNotify(msg,a,contact,inst,oper);
    ClientContact* c = a->findContactByUri(contact);
    if (!c)
	return false;
    Debug(ClientDriver::self(),DebugAll,
	"Logic(%s) account=%s contact=%s instance=%s operation=%s",
	name().c_str(),account.c_str(),contact.c_str(),inst.safe(),oper.c_str());
    bool ownContact = c == a->contact();
    String instid;
    bool online = false;
    bool statusChanged = false;
    bool oldOnline = c->online();
    // Use a while() to break to the end
    while (true) {
	// Avoid account own instance
	if (ownContact && inst && inst == a->resource().toString())
	    return false;
	online = (oper == YSTRING("online"));
	if (online || oper == YSTRING("offline")) {
	    if (online) {
		c->setOnline(true);
		if (!inst) {
		    statusChanged = !oldOnline;
		    break;
		}
		statusChanged = true;
		ClientResource* res = c->findResource(inst);
		if (!res)
		    res = new ClientResource(inst);
		// Update resource
		res->setFileTransfer(msg.getBoolValue(YSTRING("caps.filetransfer")));
		res->setAudio(msg.getBoolValue(YSTRING("caps.audio")));
		res->setPriority(msg.getIntValue(YSTRING("priority")));
		res->setStatusText(msg.getValue(YSTRING("status")));
		int stat = msg.getIntValue(YSTRING("show"),ClientResource::s_statusName);
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
		if (inst) {
		    statusChanged = c->removeResource(inst);
		    if (!c->resources().skipNull()) {
			statusChanged = statusChanged || oldOnline;
			c->setOnline(false);
		    }
		}
		else if (c->online()) {
		    statusChanged = true;
		    c->resources().clear();
		    c->setOnline(false);
		}
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
    if (statusChanged) {
	NamedList p("");
	fillChatContact(p,*c,false,true);
	Client::self()->setTableRow(s_chatContactList,c->toString(),&p);
	if (c->hasChat()) {
	    bool newOnline = c->online();
	    ClientResource* res = c->status();
	    int stat = newOnline ? ClientResource::Online : ClientResource::Offline;
	    c->updateChatWindow(p,0,resStatusImage(res ? res->m_status : stat));
	    if (oldOnline != newOnline)
		addChatNotify(*c,newOnline,false,msg.msgTime().sec());
	}
	// Update info window if displayed
	updateContactInfo(c);
	// Update chat actions in main contacts list (main window)
	String sel;
	Client::self()->getSelect(s_chatContactList,sel,Client::self()->getWindow(s_wndMain));
	if (c->toString() == sel)
	    enableChatActions(c);
    }
    return false;
}

// Process resource.subscribe message
bool DefaultLogic::handleResourceSubscribe(Message& msg, bool& stopLogic)
{
    if (!Client::valid() || Client::isClientMsg(msg))
	return false;
    const String& account = msg[YSTRING("account")];
    const String& contact = msg[YSTRING("subscriber")];
    const String& oper = msg[YSTRING("operation")];
    if (!(account && contact && oper))
	return false;
    // Postpone message processing
    if (Client::self()->postpone(msg,Client::ResourceSubscribe)) {
	stopLogic = true;
	return false;
    }
    ClientAccount* a = m_accounts->findAccount(account);
    if (!a)
	return false;
    bool sub = (oper == YSTRING("subscribe"));
    if (!sub && oper != YSTRING("unsubscribe"))
	return false;
    ClientContact* c = a->findContactByUri(contact);
    if (c && c == a->contact())
	return false;
    Debug(ClientDriver::self(),DebugAll,
	"Logic(%s) account=%s contact=%s operation=%s",
	name().c_str(),account.c_str(),contact.c_str(),oper.c_str());
    if (sub && a->resource().online()) {
	NamedList rows("");
	NamedList* upd = buildNotifArea(rows,"subscription",account,contact,"Subscription request");
	String cname;
	if (c && c->m_name && (c->m_name != contact))
	    cname << "'" << c->m_name << "' ";
	upd->addParam("name",cname);
	String s = "Contact ${name}<${contact}> requested subscription on account '${account}'.";
	upd->replaceParams(s);
	upd->addParam("text",s);
	showNotificationArea(true,Client::self()->getWindow(s_wndMain),&rows);
    }
    return true;
}

// Process chan.startup message
bool DefaultLogic::handleClientChanUpdate(Message& msg, bool& stopLogic)
{
#define CHANUPD_ID (chan ? chan->id() : *id)
#define CHANUPD_ADDR (chan ? chan->address() : String::empty())

    if (!Client::self())
	return false;
    if (Client::self()->postpone(msg,Client::ClientChanUpdate,true)) {
	stopLogic = true;
	return false;
    }
    // Ignore utility channels (playing sounds)
    if (msg.getBoolValue(YSTRING("utility")))
	return false;
    int notif = ClientChannel::lookup(msg.getValue(YSTRING("notify")));
    if (notif == ClientChannel::Destroyed) {
	if (!Client::valid())
	    return false;
	String id = msg.getValue(YSTRING("id"));
	// Reset init transfer if destroyed
	if (m_transferInitiated && m_transferInitiated == id)
	    m_transferInitiated = "";
	// Stop incoming ringer if there are no more incoming channels
	bool haveIncoming = false;
	if (ClientDriver::self()) {
	    Lock lock(ClientDriver::self());
	    ObjList* o = ClientDriver::self()->channels().skipNull();
	    for (; o; o = o->skipNext())
		if ((static_cast<Channel*>(o->get()))->isOutgoing()) {
		    haveIncoming = true;
		    break;
		}
	}
	if (!haveIncoming) {
	    removeTrayIcon(YSTRING("incomingcall"));
	    Client::self()->ringer(true,false);
	    Client::self()->ringer(false,false);
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
	id = msg.getParam(YSTRING("id"));
    if (!(chan || id))
	return false;
    bool outgoing = chan ? chan->isOutgoing() : msg.getBoolValue(YSTRING("outgoing"));
    bool noticed = chan ? chan->isNoticed() : msg.getBoolValue(YSTRING("noticed"));
    bool active = chan ? chan->active() : msg.getBoolValue(YSTRING("active"));
    bool silence = msg.getBoolValue(YSTRING("silence"));
    bool notConf = !(chan ? chan->conference() : msg.getBoolValue(YSTRING("conference")));

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
	case ClientChannel::AudioSet:
	    if (chan) {
		bool mic = chan->muted() || (0 != chan->getSource());
		bool speaker = (0 != chan->getConsumer());
		notifyNoAudio(!(mic && speaker),mic,speaker,chan);
	    }
	    break;
	case ClientChannel::OnHold:
	    enableActions = true;
	    buildStatus(status,"Call inactive",CHANUPD_ADDR,CHANUPD_ID);
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
	    if (outgoing) {
		addTrayIcon(YSTRING("incomingcall"));
		Client::self()->setUrgent(s_wndMain,true,Client::self()->getWindow(s_wndMain));
	    }
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
	    if (outgoing)
		removeTrayIcon(YSTRING("incomingcall"));
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

// Process contact.info message
bool DefaultLogic::handleContactInfo(Message& msg, bool& stopLogic)
{
    if (!Client::valid() || Client::isClientMsg(msg))
	return false;
    const String& account = msg[YSTRING("account")];
    if (!account)
	return false;
    const String& oper = msg[YSTRING("operation")];
    if (!oper)
	return false;
    // Postpone message processing
    if (Client::self()->postpone(msg,Client::ContactInfo)) {
	stopLogic = true;
	return false;
    }
    const String& contact = msg[YSTRING("contact")];
    DDebug(ClientDriver::self(),DebugAll,
	"Logic(%s)::handleContactInfo() account=%s contact=%s operation=%s",
	name().c_str(),account.c_str(),contact.c_str(),oper.c_str());
    // Notify the MUC wizard
    s_mucWizard->handleContactInfo(msg,account,oper,contact);
    return false;
}

// Default message processor called for id's not defined in client.
bool DefaultLogic::defaultMsgHandler(Message& msg, int id, bool& stopLogic)
{
    if (id == Client::ChanNotify) {
	String event = msg.getValue(YSTRING("event"));
	if (event != YSTRING("left"))
	    return false;

	// Check if we have a channel in conference whose peer is the one who left
	const char* peer = msg.getValue(YSTRING("lastpeerid"));
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
    if (id == Client::MsgExecute) {
	if (!Client::valid() || Client::isClientMsg(msg))
	    return false;
	if (Client::self()->postpone(msg,Client::MsgExecute))
	    return true;
	const String& account = msg[YSTRING("account")];
	if (!account)
	    return false;
	ClientAccount* acc = m_accounts->findAccount(account);
	if (!acc)
	    return false;
	const String& type = msg[YSTRING("type")];
	String tmp;
	ClientContact::buildContactId(tmp,account,msg.getValue(YSTRING("caller")));
	ClientContact* c = acc->findContact(tmp);
	bool chat = (!type || type == YSTRING("chat"));
	if (c) {
	    if (chat) {
		String* delay = msg.getParam(YSTRING("delay_time"));
		unsigned int time = !delay ? msg.msgTime().sec() : (unsigned int)delay->toInteger(0);
		const char* ds = !delay ? "" : msg.getValue(YSTRING("delay_by"));
		String chatState;
		bool hasState = !delay && buildChatState(chatState,msg,c->m_name);
		const String& body = msg[YSTRING("body")];
		NamedList* p = 0;
		if (body || (!hasState &&
		    Client::self()->getBoolOpt(Client::OptShowEmptyChat)))
		    p = buildChatParams(body,c->m_name,time,0 != delay,ds);
		// Active state with no body or notification: remove last notification
		//  if the contact has a chat
		bool resetNotif = false;
		if (c->hasChat())
		    resetNotif = !p && !chatState && msg[YSTRING("chatstate")] == YSTRING("active");
		else
		    chatState.clear();
		if (p || chatState || resetNotif) {
		    if (!c->hasChat()) {
			c->createChatWindow();
			NamedList p("");
			fillChatContact(p,*c,true,true);
			ClientResource* res = c->status();
			c->updateChatWindow(p,"Chat [" + c->m_name + "]",
			    resStatusImage(res ? res->m_status : ClientResource::Offline));
		    }
		    c->showChat(true);
		    if (chatState)
			addChatNotify(*c,chatState,msg.msgTime().sec(),"tempnotify");
		    if (p) {
			logChat(c,time,false,delay != 0,body);
			c->addChatHistory(!delay ? YSTRING("chat_in") : YSTRING("chat_delayed"),p);
			notifyIncomingChat(c);
		    }
		    if (resetNotif)
			c->setChatProperty(YSTRING("history"),YSTRING("_yate_tempitemcount"),String((int)0));
		}
	    }
	    else
		DDebug(ClientDriver::self(),DebugStub,
		    "DefaultLogic unhandled message type=%s",type.c_str());
	    return true;
	}
	MucRoom* room = acc->findRoom(tmp);
	if (!room)
	    return false;
	bool mucChat = !chat && type == YSTRING("groupchat");
	if (!(mucChat || chat)) {
	    Debug(ClientDriver::self(),DebugStub,
		"DefaultLogic unhandled MUC message type=%s",type.c_str());
	    return true;
	}
	const String& body = msg[YSTRING("body")];
	String* delay = mucChat ? msg.getParam(YSTRING("delay_time")) : 0;
	const String& nick = msg[YSTRING("caller_instance")];
	MucRoomMember* member = room->findMember(nick);
	// Accept delayed (history) group chat from unknown nick
	if (!member && !(mucChat && delay))
	    return false;
	unsigned int time = !delay ? msg.msgTime().sec() : (unsigned int)delay->toInteger(0);
	// Check subject changes (empty subject is allowed)
	String* subject = mucChat ? msg.getParam(YSTRING("subject")) : 0;
	if (subject) {
	    NamedList tmp("");
	    tmp.addParam("room_subject",*subject);
	    room->updateChatWindow(room->resource().toString(),tmp);
	    // Show any notification from room
	    if (body)
		addChatNotify(*room,body,msg.msgTime().sec());
	    String text(nick);
	    text << " changed room subject to '" << *subject << "'";
	    if (delay) {
		NamedList* p = buildChatParams(text,"",time,0,0);
		room->addChatHistory(room->resource().toString(),YSTRING("chat_delayed"),p);
		notifyIncomingChat(room,room->resource().toString());
	    }
	    else
		addChatNotify(*room,text,msg.msgTime().sec());
	    return true;
	}
	// Ignore non delayed chat returned by the room
	if (!delay && (!member || room->ownMember(member)))
	    return true;
	String chatState;
	bool hasState = !delay && buildChatState(chatState,msg,member->m_name);
	NamedList* p = 0;
	if (body || (!hasState &&
	    Client::self()->getBoolOpt(Client::OptShowEmptyChat)))
	    p = buildChatParams(body,member ? member->m_name : nick,time,0,0);
	const String& id = mucChat ? room->resource().toString() : member->toString();
	// Active state with no body or notification: remove last notification
	bool resetNotif = false;
	if (room->hasChat(id))
	    resetNotif = !p && !chatState && msg[YSTRING("chatstate")] == YSTRING("active");
	else
	    chatState.clear();
	if (p || chatState || resetNotif) {
	    if (chat)
		createRoomChat(*room,member,false);
	    if (chatState)
		addChatNotify(*room,chatState,msg.msgTime().sec(),"tempnotify",id);
	    if (p) {
		room->addChatHistory(id,!delay ? YSTRING("chat_in") : YSTRING("chat_delayed"),p);
		notifyIncomingChat(room,id);
		if (body)
		    logChat(room,time,false,delay != 0,body,mucChat,nick);
	    }
	    if (resetNotif)
		room->setChatProperty(id,YSTRING("history"),YSTRING("_yate_tempitemcount"),String((int)0));
	}
	return true;
    }
    if (id == Client::MucRoom) {
	static const char* extra = "room,password,reason,contact_instance";
	if (!Client::valid() || Client::isClientMsg(msg))
	    return false;
	if (Client::self()->postpone(msg,Client::MucRoom))
	    return true;
	const String& account = msg[YSTRING("account")];
	ClientAccount* acc = account ? m_accounts->findAccount(account) : 0;
	if (!acc)
	    return false;
	const String& oper = msg[YSTRING("operation")];
	const String& room = msg[YSTRING("room")];
	String tmp;
	if (room)
	    ClientContact::buildContactId(tmp,account,room);
	MucRoom* r = tmp ? acc->findRoom(tmp) : 0;
        // Invite
	if (oper == YSTRING("invite")) {
	    // Account already there
	    if (r && r->resource().online())
		return false;
	    const String& contact = msg[YSTRING("contact")];
	    if (!contact) {
		Message* m = buildMucRoom("decline",account,room,"Unnaceptable anonymous invitation!");
		return Engine::enqueue(m);
	    }
	    NamedList rows("");
	    NamedList* upd = buildNotifArea(rows,"mucinvite",account,contact,"Join chat room",extra);
	    upd->copyParams(msg,extra);
	    String cname;
	    ClientContact* c = acc->findContactByUri(contact);
	    if (c && c->m_name && (c->m_name != contact))
		cname << "'" << c->m_name << "' ";
	    upd->addParam("name",cname);
	    String s = "Contact ${name}<${contact}> invites you to join chat room '${room}' on account '${account}'.\r\n${reason}";
	    upd->replaceParams(s);
	    upd->addParam("text",s);
	    showNotificationArea(true,Client::self()->getWindow(s_wndMain),&rows);
	    return true;
	}
	return false;
    }
    if (id == Client::TransferNotify)
	return handleFileTransferNotify(msg,stopLogic);
    if (id == Client::UserData)
	return handleUserData(msg,stopLogic);
    return false;
}

// Client created and initialized all windows
void DefaultLogic::initializedWindows()
{
    if (!Client::valid())
	return;
    // Add 'not selected' item
    Client::self()->updateTableRow(YSTRING("protocol"),s_notSelected,0,true);
    Client::self()->updateTableRow(s_accProviders,s_notSelected,0,true);
    Client::self()->updateTableRow(YSTRING("account"),s_notSelected,0,true);
    // Fill protocol lists
    bool tel = true;
    updateProtocolList(0,YSTRING("protocol"),&tel);
    updateProtocolList(0,s_accProtocol);
    // Make sure the active page is the calls one
    activatePageCalls(0,false);
}

// Utility: set check parameter from another list
static inline void setCheck(NamedList& p, const NamedList& src, const String& param,
    bool defVal = true)
{
    bool ok = src.getBoolValue(param,defVal);
    p.addParam("check:" + param,String::boolText(ok));
}

// Initialize client from settings
bool DefaultLogic::initializedClient()
{
    if (!Client::self())
	return false;

    addTrayIcon("main");

    // Load account status
    AccountStatus::load();
    AccountStatus::updateUi();

    // Load muc rooms
    s_mucRooms = Engine::configFile("client_mucrooms",true);
    s_mucRooms.load(false);

    Window* wMain = Client::self()->getWindow(s_wndMain);

    NamedList dummy("client");
    NamedList* cSect = Client::s_settings.getSection("client");
    if (!cSect)
	cSect = &dummy;
    NamedList* cGen = Client::s_settings.getSection("general");
    if (!cGen)
	cGen = &dummy;

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

    setAdvancedMode();
    // Other string parameters
    setClientParam("username",Client::s_settings.getValue("default","username"),false,true);
    setClientParam("callerid",Client::s_settings.getValue("default","callerid"),false,true);
    setClientParam("domain",Client::s_settings.getValue("default","domain"),false,true);
    // Create default ring sound
    // Try to build native for wave file
    String ring = cGen->getValue("ringinfile",Client::s_soundPath + "ring.wav");
    bool wave = ring.endsWith(".wav");
    if (!(wave && Client::self()->createSound(Client::s_ringInName,ring))) {
	if (wave)
	    ring = Client::s_soundPath + "ring.au";
	ClientSound::build(Client::s_ringInName,ring);
    }
    ring = cGen->getValue("ringoutfile",Client::s_soundPath + "tone.wav");
    Client::self()->createSound(Client::s_ringOutName,ring);

    // Enable call actions
    enableCallActions(m_selectedChannel);

    // Set handlers
    Client::self()->installRelay("chan.notify",Client::ChanNotify,100);
    Client::self()->installRelay("muc.room",Client::MucRoom,100);
    Client::self()->installRelay("transfer.notify",Client::TransferNotify,100);
    Client::self()->installRelay("user.data",Client::UserData,100);

    // File transfer
    s_lastFileDir = Client::s_settings.getValue("filetransfer","dir");
    s_lastFileFilter = Client::s_settings.getValue("filetransfer","filter");

    // Chat log
    int v = lookup(cSect->getValue("logchat"),s_chatLogDict);
    if (v == ChatLogSaveAll || v == ChatLogSaveUntilLogout || v == ChatLogNoSave)
	s_chatLog = (ChatLogEnum)v;

    // Update settings
    NamedList p("");
    // Chat contacts list options
    String tmp;
    Client::self()->getProperty(s_chatContactList,"_yate_showofflinecontacts",tmp,wMain);
    p.addParam("check:" + s_chatShowOffline,String(tmp.toBoolean(true)));
    tmp.clear();
    Client::self()->getProperty(s_chatContactList,"_yate_flatlist",tmp,wMain);
    p.addParam("check:" + s_chatFlatList,String(tmp.toBoolean(true)));
    tmp.clear();
    Client::self()->getProperty(s_chatContactList,"_yate_hideemptygroups",tmp,wMain);
    p.addParam("check:chatcontact_hideemptygroups",String(tmp.toBoolean(true)));
    // Show last page in main tab
    p.addParam("select:" + s_mainwindowTabs,cSect->getValue("main_active_page","tabChat"));
    // Settings
    p.addParam("check:" + String(lookup(s_chatLog,s_chatLogDict)),String::boolText(true));
    // Account edit defaults
    setCheck(p,*cSect,"acc_showadvanced",false);
    setCheck(p,*cSect,"acc_enabled");
    Client::self()->setParams(&p);

    // Build chat contacts context menu(s)
    NamedList pcm(s_chatContactList);
    NamedList* pChat = new NamedList("menu_" + s_chatContactList);
    pChat->addParam("item:" + s_chatNew,"");
    pChat->addParam("item:" + s_chatRoomNew,"");
    pChat->addParam("item:","");
    pChat->addParam("item:" + s_chatShowOffline,"");
    pChat->addParam("item:" + s_chatFlatList,"");
    pcm.addParam(new NamedPointer("menu",pChat));
    NamedList* pChatMenu = new NamedList("menu_" + s_chatContactList + "_contact");
    pChatMenu->addParam("item:" + s_chat,"");
    pChatMenu->addParam("item:" + s_chatCall,"");
    pChatMenu->addParam("item:" + s_fileSend,"");
    pChatMenu->addParam("item:" + s_chatShowLog,"");
    pChatMenu->addParam("item:" + s_chatInfo,"");
    pChatMenu->addParam("item:" + s_chatEdit,"");
    pChatMenu->addParam("item:" + s_chatDel,"");
    pChatMenu->addParam("item:","");
    pChatMenu->addParam("item:" + s_chatNew,"");
    pChatMenu->addParam("item:" + s_chatRoomNew,"");
    pChatMenu->addParam("item:","");
    pChatMenu->addParam("item:" + s_chatShowOffline,"");
    pChatMenu->addParam("item:" + s_chatFlatList,"");
    pcm.addParam(new NamedPointer("contactmenu",pChatMenu));
    NamedList* pChatRoomMenu = new NamedList("menu_" + s_chatContactList + "_chatroom");
    pChatRoomMenu->addParam("item:" + s_chat,"");
    pChatRoomMenu->addParam("item:" + s_chatShowLog,"");
    pChatRoomMenu->addParam("item:" + s_chatEdit,"");
    pChatRoomMenu->addParam("item:" + s_chatDel,"");
    pChatRoomMenu->addParam("item:","");
    pChatRoomMenu->addParam("item:" + s_chatNew,"");
    pChatRoomMenu->addParam("item:" + s_chatRoomNew,"");
    pChatRoomMenu->addParam("item:","");
    pChatRoomMenu->addParam("item:" + s_chatShowOffline,"");
    pChatRoomMenu->addParam("item:" + s_chatFlatList,"");
    pcm.addParam(new NamedPointer("chatroommenu",pChatRoomMenu));
    Client::self()->setParams(&pcm);
    enableChatActions(0);
    // Set gobal account status menu
    NamedList pStatusMenu("");
    pStatusMenu.addParam("owner","global_account_status");
    pStatusMenu.addParam("item:setStatusOnline","");
    pStatusMenu.addParam("item:setStatusBusy","");
    pStatusMenu.addParam("item:setStatusAway","");
    pStatusMenu.addParam("item:setStatusXa","");
    pStatusMenu.addParam("item:setStatusDnd","");
    pStatusMenu.addParam("item:","");
    pStatusMenu.addParam("item:setStatusOffline","");
    Client::self()->buildMenu(pStatusMenu);

    // Activate the main window if not disabled from UI
    if (wMain) {
	String a;
	Client::self()->getProperty(wMain->id(),"_yate_activateonstartup",a,wMain);
	if (a.toBoolean(true))
	    Client::self()->setActive(wMain->id(),true,wMain);
    }
    return false;
}

// Client is exiting: save settings
void DefaultLogic::exitingClient()
{
    clearDurationUpdate();

    if (!Client::valid())
	return;

    // Avoid open account add the next time we start if the user closed the window
    if (!Client::self()->getVisible(s_accWizard->toString()))
	setClientParam(Client::s_toggles[Client::OptAddAccountOnStartup],
	    String(false),true,false);
    // Reset wizards
    s_accWizard->reset(true);
    s_mucWizard->reset(true);
    Client::self()->setVisible(s_accWizard->toString(),false);
    Client::self()->setVisible(s_mucWizard->toString(),false);
    // Hide some windows to avoid displaying them the next time we start
    Client::self()->setVisible(s_wndAccount,false);
    Client::self()->setVisible(s_wndChatContact,false);
    Client::self()->setVisible(ClientContact::s_dockedChatWnd,false);
    Client::self()->setVisible(s_wndAddrbook,false);
    Client::self()->setVisible(s_wndMucInvite,false);
    Client::self()->setVisible(s_wndFileTransfer,false);

    // Save some settings identity
    String tmp;
    if (Client::self()->getText("def_username",tmp))
	Client::s_settings.setValue("default","username",tmp);
    tmp.clear();
    if (Client::self()->getText("def_callerid",tmp))
	Client::s_settings.setValue("default","callerid",tmp);
    tmp.clear();
    if (Client::self()->getText("def_domain",tmp))
	Client::s_settings.setValue("default","domain",tmp);
    tmp.clear();
    Window* wMain = Client::self()->getWindow(s_wndMain);
    if (wMain)
	Client::self()->getSelect(s_mainwindowTabs,tmp,wMain);
    Client::s_settings.setValue("client","main_active_page",tmp);
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
    if (old == m_selectedChannel)
        return;
    // Stop incoming ringer
    if (Client::valid())
	Client::self()->ringer(true,false);
    channelSelectionChanged(old);
}

// Engine start notification. Connect startup accounts
void DefaultLogic::engineStart(Message& msg)
{
    // Set account status or start add wizard
    if (m_accounts->accounts().skipNull())
	setAccountsStatus(m_accounts);
    else if (Client::valid() &&
	Client::self()->getBoolOpt(Client::OptAddAccountOnStartup)) {
	// Start add account wizard
	s_accWizard->start();
    }
}

// Method called by the client when idle
void DefaultLogic::idleTimerTick(Time& time)
{
    for (ObjList* o = m_durationUpdate.skipNull(); o; o = o->skipNext())
	(static_cast<DurationUpdate*>(o->get()))->update(time.sec(),&s_channelList);
    if (Client::valid() && Client::self()->getBoolOpt(Client::OptNotifyChatState) &&
	ContactChatNotify::checkTimeouts(*m_accounts,time))
	Client::setLogicsTick();
}

// Enable call actions
bool DefaultLogic::enableCallActions(const String& id)
{
    if (!Client::self())
	return false;
    ClientChannel* chan = id.null() ? 0 : ClientDriver::findChan(id);
    DDebug(ClientDriver::self(),DebugInfo,"enableCallActions(%s) chan=%p",
	id.c_str(),chan);
    NamedList p("");

    // Answer/Hangup/Hold
    p.addParam("active:" + s_actionAnswer,String::boolText(chan && chan->isOutgoing() && !chan->isAnswered()));
    p.addParam("active:" + s_actionHangup,String::boolText(0 != chan));
    p.addParam("active:" + s_actionHold,String::boolText(chan != 0));
    p.addParam("check:" + s_actionHold,String::boolText(chan && chan->active()));

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
    if (!checkParam(p,YSTRING("target"),YSTRING("callto"),false,wnd))
	return false;
    checkParam(p,YSTRING("line"),YSTRING("account"),true,wnd);
    checkParam(p,YSTRING("protocol"),YSTRING("protocol"),true,wnd);
    checkParam(p,YSTRING("account"),YSTRING("account"),true,wnd);
    checkParam(p,YSTRING("caller"),YSTRING("def_username"),false);
    checkParam(p,YSTRING("callername"),YSTRING("def_callerid"),false);
    checkParam(p,YSTRING("domain"),YSTRING("def_domain"),false);
    return true;
}

// Notification on selection changes in channels list
void DefaultLogic::channelSelectionChanged(const String& old)
{
    DDebug(ClientDriver::self(),DebugInfo,"channelSelectionChanged() to '%s' old='%s'",
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
	    return showConfirm(wnd,text,"clear:" + list);
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
bool DefaultLogic::deleteItem(const String& list, const String& item, Window* wnd, bool confirm)
{
    if (!(Client::valid() && list && item))
	return false;
    DDebug(ClientDriver::self(),DebugAll,"DefaultLogic::deleteItem(%s,%s,%p,%u)",
	list.c_str(),item.c_str(),wnd,confirm);
    String context;
    if (confirm)
	context << "deleteitem:" << list << ":" << item;
    // Handle known lists
    if (list == s_chatContactList) {
	ClientContact* c = m_accounts->findAnyContact(item);
	if (!c)
	    return false;
	MucRoom* r = c->mucRoom();
	if (context) {
	    String text("Delete ");
	    text << (!r ? "friend " : "chat room ");
	    String name;
	    buildContactName(name,*c);
	    text << name << " from account '" << c->accountName() << "'?";
	    return showConfirm(wnd,text,context);
	}
	if (!r)
	    Engine::enqueue(Client::buildUserRoster(false,c->accountName(),c->uri()));
	else {
	    ClientAccount* acc = r->account();
	    bool saveServerRooms = (acc != 0) && r->remote();
	    if (acc)
		ClientLogic::clearContact(acc->m_cfg,r);
	    updateChatRoomsContactList(false,0,r);
	    r->setLocal(false);
	    r->setRemote(false);
	    if (saveServerRooms)
		Engine::enqueue(acc->userData(true,"chatrooms"));
	}
	return true;
    }
    if (list == s_contactList) {
	if (context) {
	    ClientContact* c = m_accounts->findContactByInstance(item);
	    if (!(c && m_accounts->isLocalContact(c)))
		return false;
	    return showConfirm(wnd,"Delete contact '" + c->m_name + "'?",context);
	}
	return delContact(item,wnd);
    }
    if (list == s_accountList) {
	if (context)
	    return showConfirm(wnd,"Delete account '" + item + "'?",context);
	return delAccount(item,wnd);
    }
    if (list == s_logList) {
	if (context)
	    return showConfirm(wnd,"Delete the selected call log?",context);
	return callLogDelete(item);
    }
    if (list == ClientContact::s_dockedChatWidget) {
	if (wnd && wnd->id() == ClientContact::s_mucsWnd) {
	    MucRoom* room = m_accounts->findRoomByMember(item);
	    if (room && room->ownMember(item)) {
		if (context) {
		    // Request confirmation if there is an opened private chat
		    ObjList* o = room->resources().skipNull();
		    for (; o; o = o->skipNext()) {
			MucRoomMember* m = static_cast<MucRoomMember*>(o->get());
			if (room->hasChat(m->toString())) {
			    String text;
			    text << "You have active chat in room " << room->uri();
			    text << ".\r\nDo you want to proceed?";
			    return showConfirm(wnd,text,context);
			}
		    }
		}
		logCloseMucSessions(room);
		if (room->local() || room->remote()) {
		    clearRoom(room);
		    if (room->account() && room->account()->resource().online())
			updateChatRoomsContactList(true,0,room);
		}
		else
		    TelEngine::destruct(room);
	    }
	    else if (room) {
		MucRoomMember* m = room->findMemberById(item);
		if (m)
		    logCloseSession(room,false,m->m_name);
		Client::self()->delTableRow(list,item,wnd);
	    }
	    return true;
	}
	if (wnd && wnd->id() == ClientContact::s_dockedChatWnd) {
	    if (!s_changingDockedChat)
		logCloseSession(m_accounts->findContact(item));
	    Client::self()->delTableRow(ClientContact::s_dockedChatWidget,item,wnd);
	    return true;
	}
    }
    // Remove table row
    return Client::self()->delTableRow(list,item,wnd);
}

// Handle list/table selection deletion
bool DefaultLogic::deleteSelectedItem(const String& action, Window* wnd)
{
    if (!Client::valid())
	return false;
    DDebug(ClientDriver::self(),DebugAll,"DefaultLogic::deleteSelectedItem(%s,%p) wnd=%s",
	action.c_str(),wnd,wnd ? wnd->id().c_str() : "");
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
    return item && deleteItem(list,item,wnd,pos > 0);
}

// Handle text changed notification
bool DefaultLogic::handleTextChanged(NamedList* params, Window* wnd)
{
    if (!(params && wnd))
	return false;
    String sender = (*params)[YSTRING("sender")];
    if (!sender)
	return false;
    // Username changes in contact add/edit
    bool isContact = wnd->id().startsWith("contactedit_");
    if (isContact || wnd->id().startsWith("chatroomedit_")) {
	if (!Client::valid())
	    return false;
	const String& text = (*params)["text"];
	if (isContact) {
	    if (!wnd->context() &&
		checkUriTextChanged(wnd,sender,text,YSTRING("username"),YSTRING("domain")))
		return true;
	}
	else if (checkUriTextChanged(wnd,sender,text,YSTRING("room_room"),YSTRING("room_server")))
	    return true;
	return false;
    }
    // Chat input changes
    if (Client::valid() && Client::self()->getBoolOpt(Client::OptNotifyChatState)) {
	ClientContact* c = 0;
	MucRoom* room = 0;
	String id;
	if (sender == ClientContact::s_chatInput)
	    c = m_accounts->findContact(wnd->context());
	else
	    getPrefixedContact(sender,ClientContact::s_chatInput,id,m_accounts,&c,&room);
	MucRoomMember* m = (!c && room) ? room->findMemberById(id) : 0;
	if (c || m) {
	    String* text = params->getParam(YSTRING("text"));
	    String tmp;
	    if (!text) {
		text = &tmp;
		if (c)
		    c->getChatInput(tmp);
		else
		    room->getChatInput(id,tmp);
	    }
	    ContactChatNotify::update(c,room,m,text->null());
	}
    }
    return false;
}

// Handle file transfer actions
bool DefaultLogic::handleFileTransferAction(const String& name, Window* wnd,
    NamedList* params)
{
    if (!Client::valid())
	return false;
    ClientContact* c = 0;
    String file;
    if (name == s_fileSend) {
	String contact;
	if (params)
	    contact = params->getValue(YSTRING("contact"));
	if (!contact)
	    Client::self()->getSelect(s_chatContactList,contact,wnd);
	c = contact ? m_accounts->findContact(contact) : 0;
    }
    else if (name.startsWith(s_fileSendPrefix,false))
	c = m_accounts->findContact(name.substr(s_fileSendPrefix.length()));
    else if (name.startsWith(s_fileOpenSendPrefix,false)) {
	// Choose file dialog action (params ? ok : cancel)
	file = params ? params->getValue(YSTRING("file")) : "";
	if (!file)
	    return true;
	// Update/save last dir and filter
	s_lastFileDir = params->getValue(YSTRING("dir"));
	s_lastFileFilter = params->getValue(YSTRING("filter"));
	Client::s_settings.setValue("filetransfer","dir",s_lastFileDir);
	Client::s_settings.setValue("filetransfer","filter",s_lastFileFilter);
	// Retrieve the contact
	c = m_accounts->findContact(name.substr(s_fileOpenSendPrefix.length()));
    }
    else if (name.startsWith(s_fileOpenRecvPrefix,false)) {
	file = params ? params->getValue(YSTRING("file")) : "";
	if (!file)
	    return true;
	String id = name.substr(s_fileOpenRecvPrefix.length());
	NamedList item("");
	Client::self()->getTableRow(YSTRING("messages"),id,&item,wnd);
	const String& chan = item[YSTRING("targetid")];
	if (chan) {
	    // Add file transfer item
	    NamedList p(chan);
	    String text;
	    String buf;
	    const String& account = item[YSTRING("account")];
	    const String& contact = item[YSTRING("contact")];
	    ClientAccount* a = account ? m_accounts->findAccount(account) : 0;
	    ClientContact* c = a ? a->findContactByUri(contact) : 0;
	    if (c)
		buildContactName(buf,*c);
	    else
		buf = contact;
	    text << "Receiving '" << file << "'";
	    text.append(buf," from ");
	    p.addParam("send",String::boolText(false));
	    p.addParam("text",text);
	    p.addParam("select:progress","0");
	    p.addParam("account",account);
	    p.addParam("contact",contact);
	    p.addParam("contact_name",buf,false);
	    p.addParam("file",file);
	    p.addParam("channel",chan);
	    updateFileTransferItem(true,p,p,true);
	    // Remove the file
	    File::remove(file);
	    // Attach the consumer
	    Message m("chan.masquerade");
	    m.addParam("message","chan.attach");
	    m.addParam("id",chan);
	    m.addParam("consumer","filetransfer/receive/" + file);
	    m.copyParams(item);
	    m.addParam("autoclose",String::boolText(false));
	    m.addParam("notify",chan);
	    m.addParam("notify_progress",String::boolText(true));
	    Engine::dispatch(m);
	    // Answer the call
	    Message* anm = new Message("chan.masquerade");
	    anm->addParam("message","call.answered");
	    anm->addParam("id",chan);
	    Engine::enqueue(anm);
	}
	// Remove notification
	Client::self()->delTableRow(YSTRING("messages"),id,wnd);
	// Update/save last dir
	s_lastFileDir = params->getValue(YSTRING("dir"));
	Client::s_settings.setValue("filetransfer","dir",s_lastFileDir);
	return true;
    }
    else if (name.startsWith("fileprogress_close:",false)) {
	// Close file transfer
	String id = name.substr(19);
	if (id)
	    dropFileTransferItem(id);
	return true;
    }
    else
	return false;
    if (!c)
	return false;
    if (!file)
	return chooseFileTransfer(true,s_fileOpenSendPrefix + c->toString(),wnd);
    ClientResource* res = c->findFileTransferResource();
    Message m("call.execute");
    m.addParam("callto","filetransfer/send/" + file);
    String direct("jingle/" + c->uri());
    if (res)
	direct << "/" << res->toString();
    m.addParam("direct",direct);
    m.addParam("line",c->accountName(),false);
    m.addParam("getfilemd5",String::boolText(true));
    m.addParam("getfileinfo",String::boolText(true));
    m.addParam("notify_progress",String::boolText(true));
    m.addParam("autoclose",String::boolText(false));
    m.addParam("send_chunk_size","4096");
    m.addParam("send_interval","10");
    String notify(c->toString());
    notify << String(file.hash()) << (int)Time::now();
    m.addParam("notify",notify);
    if (!Engine::dispatch(m)) {
	String s;
	s << "Failed to send '" << file << "' to " << c->uri();
	s.append(m.getValue("error"),"\r\n");
	showError(wnd,s);
	return false;
    }
    NamedList p(notify);
    String text;
    String buf;
    buildContactName(buf,*c);
    text << "Sending '" << file << "' to " << buf;
    p.addParam("send",String::boolText(true));
    p.addParam("text",text);
    p.addParam("select:progress","0");
    p.addParam("account",c->accountName());
    p.addParam("contact",c->uri());
    p.addParam("contact_name",buf,false);
    p.addParam("file",file);
    p.addParam("channel",m[YSTRING("id")]);
    updateFileTransferItem(true,notify,p,true);
    return true;
}

// Handle file transfer notifications
bool DefaultLogic::handleFileTransferNotify(Message& msg, bool& stopLogic)
{
    const String& id = msg[YSTRING("targetid")];
    if (!id)
	return false;
    if (Client::self()->postpone(msg,Client::TransferNotify)) {
	stopLogic = true;
	return true;
    }
    const String& status = msg[YSTRING("status")];
    String progress;
    String text;
    bool running = status != YSTRING("terminated");
    if (running) {
	int trans = msg.getIntValue(YSTRING("transferred"));
	int total = msg.getIntValue(YSTRING("total"));
	if (total && total > trans)
	    progress = (int)((int64_t)trans * 100 / total);
    }
    else {
	NamedList p("");
	getFileTransferItem(id,p);
	const String& error = msg[YSTRING("error")];
	bool send = msg.getBoolValue(YSTRING("send"));
	if (!error) {
	    progress = "100";
	    text << "Succesfully " << (send ? "sent '" : "received '");
	    text << p["file"] << "'";
	    text << (send ? " to " : " from ") << p["contact_name"];
	}
	else {
	    text << "Failed to " << (send ? "send '" : "receive '");
	    text << p["file"] << "'";
	    text << (send ? " to " : " from ") << p["contact_name"];
	    text << "\r\nError: " << error;
	}
    }
    if (!(progress || text))
	return true;
    NamedList p(id);
    p.addParam("text",text,false);
    p.addParam("select:progress",progress,false);
    if (!running)
	p.addParam("cancel","Close");
    updateFileTransferItem(false,id,p);
    return true;
}

// Handle user.data messages.
bool DefaultLogic::handleUserData(Message& msg, bool& stopLogic)
{
    if (!Client::valid() || Client::isClientMsg(msg))
	return false;
    if (Client::self()->postpone(msg,Client::UserData)) {
	stopLogic = true;
	return false;
    }
    const String& data = msg[YSTRING("data")];
    if (!data)
	return false;
    const String& account = msg[YSTRING("account")];
    ClientAccount* a = account ? m_accounts->findAccount(account) : 0;
    if (!(a && a->resource().online()))
	return false;
    const String& oper = msg[YSTRING("operation")];
    if (!oper)
	return false;
    bool ok = (oper == YSTRING("result"));
    if (!ok && oper != YSTRING("error"))
	return false;
    const String& requested = msg[YSTRING("requested_operation")];
    bool upd = (requested == YSTRING("update"));
    if (ok) {
	if (upd) {
	    // Update succeeded
	    return true;
	}
	// Handle request
	if (data == YSTRING("chatrooms")) {
	    // Update MUC rooms
	    unsigned int n = msg.getIntValue(YSTRING("data.count"));
	    const NamedString* ns = 0;
	    NamedIterator iter(msg);
	    bool changed = false;
	    for (unsigned int i = 1; i <= n; i++) {
		String prefix;
		prefix << "data." << i;
		const String& uri = msg[prefix];
		if (!uri)
		    continue;
		prefix << ".";
		String id;
		ClientContact::buildContactId(id,a->toString(),uri);
		MucRoom* r = a->findRoom(id);
		String pwd = msg[prefix + "password"];
		if (pwd) {
		    Base64 b((void*)pwd.c_str(),pwd.length());
		    DataBlock tmp;
		    b.decode(tmp);
		    pwd.assign((const char*)tmp.data(),tmp.length());
		}
		const String& name = msg[prefix + "name"];
		if (r) {
		    changed = setChangedString(r->m_name,name) || changed;
		    changed = setChangedString(r->m_password,pwd) || changed;
		    changed = setChangedParam(r->m_params,"autojoin",msg[prefix + "autojoin"]) ||
			changed;
		}
		else {
		    changed = true;
		    r = new MucRoom(a,id,name,uri);
		    r->m_password = pwd;
		    r->setLocal(false);
		}
		r->setRemote(true);
		// Copy other params
		for (iter.reset(); 0 != (ns = iter.get());) {
		    if (!ns->name().startsWith(prefix))
			continue;
		    String param = ns->name().substr(prefix.length());
		    if (param == YSTRING("group"))
			continue;
		    changed = setChangedParam(r->m_params,param,*ns) || changed;
		}
		Debug(ClientDriver::self(),DebugAll,
		    "Account(%s) updated remote MUC room '%s' [%p]",
		    account.c_str(),r->uri().c_str(),a);
		// Auto join
		if (changed && r->m_params.getBoolValue("autojoin") &&
		    checkGoogleRoom(r->uri()))
		    joinRoom(r);
	    }
	    if (changed)
		updateChatRoomsContactList(true,a);
	    // Merge local/remote rooms
	    bool saveRemote = false;
	    for (ObjList* o = a->mucs().skipNull(); o; o = o->skipNext()) {
		MucRoom* r = static_cast<MucRoom*>(o->get());
		if (r->local()) {
		    if (!r->remote()) {
			r->setRemote(true);
			saveRemote = true;
		    }
		}
		else if (r->remote()) {
		    r->setLocal(true);
		    saveContact(a->m_cfg,r);
		}
	    }
	    if (saveRemote)
		Engine::enqueue(a->userData(true,"chatrooms"));
	}
    }
    else {
	String error;
	String reason = msg[YSTRING("error")];
	if (reason) {
	    error << reason;
	    const String& res = msg["reason"];
	    if (res)
		error << " (" << res << ")";
	}
	else
	    error << msg[YSTRING("reason")];
	Debug(ClientDriver::self(),DebugNote,
	    "Account(%s) private data %s '%s' failed: %s",
	    account.c_str(),requested.c_str(),data.c_str(),error.c_str());
    }
    return true;
}

// Show a generic notification
void DefaultLogic::notifyGenericError(const String& text, const String& account,
    const String& contact, const char* title)
{
    NamedList list("");
    NamedList* upd = buildNotifArea(list,"generic",account,contact,title);
    setGenericNotif(*upd);
    upd->addParam("text",text);
    showNotificationArea(true,Client::self()->getWindow(s_wndMain),&list);
}

// Show/hide no audio notification (chan==0: initial check)
void DefaultLogic::notifyNoAudio(bool show, bool micOk, bool speakerOk,
    ClientChannel* chan)
{
    if (!Client::valid())
	return;
    Window* w = Client::self()->getWindow(s_wndMain);
    if (!show) {
	String id;
	buildNotifAreaId(id,"noaudio",String::empty());
	Client::self()->delTableRow("messages",id,w);
	return;
    }
    if (micOk && speakerOk)
	return;
    NamedList list("");
    NamedList* upd = buildNotifArea(list,"noaudio",String::empty(),
	String::empty(),"Audio failure");
    String text;
    if (chan) {
	text << "Failed to open ";
	if (!(micOk || speakerOk))
	    text << "audio";
	else if (micOk)
	    text << "speaker";
	else
	    text << "microphone";
	text << ".\r\nPlease check your sound card";
    }
    else
	return;
    upd->addParam("text",text);
    setGenericNotif(*upd);
    Client::self()->updateTableRows("messages",&list,false,w);
    NamedList p("");
    const char* ok = String::boolText(show);
    p.addParam("check:messages_show",ok);
    p.addParam("show:frame_messages",ok);
    Client::self()->setParams(&p,w);
}

// Utility used in DefaultLogic::updateChatRoomsContactList
// Build (un)load a chat room parameter
static void addChatRoomParam(NamedList& upd, bool load, MucRoom* room)
{
    if (!(room && (!load || room->local() || room->remote())))
	return;
    NamedList* p = new NamedList(room->toString());
    if (load)
	fillChatContact(*p,*room,true,true,true);
    upd.addParam(new NamedPointer(*p,p,load ? String::boolText(true) : ""));
}

// (Un)Load chat rooms
void DefaultLogic::updateChatRoomsContactList(bool load, ClientAccount* acc,
    MucRoom* room)
{
    if (!(Client::valid() && (acc || room)))
	return;
    NamedList upd("");
    if (acc) {
	for (ObjList* o = acc->mucs().skipNull(); o; o = o->skipNext())
	    addChatRoomParam(upd,load,static_cast<MucRoom*>(o->get()));
    }
    else
	addChatRoomParam(upd,load,room);
    Client::self()->updateTableRows(s_chatContactList,&upd,false);
}

// Join a MUC room. Create/show chat. Update its status
void DefaultLogic::joinRoom(MucRoom* room, bool force)
{
    if (!room)
	return;
    if (!room->resource().offline()) {
	if (force) {
	    room->m_params.setParam("internal.reconnect",String::boolText(true));
	    Engine::enqueue(room->buildJoin(false));
	}
	createRoomChat(*room);
	return;
    }
    room->resource().m_name = room->m_params.getValue("nick");
    if (!room->resource().m_name && room->account()) {
	if (room->account()->contact())
	    room->resource().m_name = room->account()->contact()->uri().getUser();
	if (!room->resource().m_name)
	    room->resource().m_name = room->account()->params().getValue(YSTRING("username"));
    }
    if (!checkGoogleRoom(room->uri()))
	return;
    bool hist = room->m_params.getBoolValue(YSTRING("history"),true);
    unsigned int lastMinutes = 0;
    if (hist)
	lastMinutes = room->m_params.getIntValue(YSTRING("historylast"));
    Message* m = room->buildJoin(true,hist,lastMinutes * 60);
    room->resource().m_status = ClientResource::Connecting;
    updateChatRoomsContactList(true,0,room);
    createRoomChat(*room);
    Engine::enqueue(m);
}

// Utility used in updateAccount
static void updAccDelOld(ClientAccount*& old, ClientLogic* logic)
{
    if (!old)
	return;
    if (!old->resource().offline())
	Engine::enqueue(userLogin(old,false));
    logic->delAccount(old->toString(),0);
    TelEngine::destruct(old);
}

// Add/set an account
bool DefaultLogic::updateAccount(const NamedList& account, bool save,
    const String& replace, bool loaded)
{
    DDebug(ClientDriver::self(),DebugAll,
	"ClientLogic(%s)::updateAccount(%s) save=%u replace=%s loaded=%u",
	toString().c_str(),account.c_str(),save,replace.safe(),loaded);
    ClientAccount* repl = replace ? m_accounts->findAccount(replace,true) : 0;
    ClientAccount* acc = m_accounts->findAccount(account,true);
    // This should never happen
    if (repl && acc && acc != repl) {
	TelEngine::destruct(repl);
	TelEngine::destruct(acc);
	Debug(ClientDriver::self(),DebugWarn,
	    "Attempt to replace an existing account with another account");
	return false;
    }
    if (repl) {
	TelEngine::destruct(acc);
	acc = repl;
    }
    String oldDataDir = acc ? acc->dataDir() : String::empty();
    bool changed = false;
    // Update account
    // Postpone old account deletion: let the new account take files
    //  from the old one's data directory
    ClientAccount* old = 0;
    if (acc) {
	if (acc->toString() != account) {
	    // Account id changed:
	    // Disconnect the account, remove it and add a new one
	    old = acc;
	    acc = 0;
	}
	else {
	    // Compare account parameters
	    changed = !(sameParams(acc->params(),account,s_accParams) &&
		sameParams(acc->params(),account,s_accBoolParams) &&
		sameParams(acc->params(),account,s_accProtoParams) &&
		sameParams(acc->params(),account,s_accProtoParamsSel));
	    if (changed)
		acc->m_params.copyParams(account);
	}
    }
    if (!acc) {
	String id;
	// Adjust loaded account id to internally generated id
	if (loaded) {
	    URI uri(account);
	    if (!(uri.getProtocol() && uri.getUser() && uri.getHost())) {
		const String& proto = account[YSTRING("protocol")];
		const String& user = account[YSTRING("username")];
		const char* host = account.getValue(YSTRING("domain"),account.getValue(YSTRING("server")));
		if (proto && user && host)
		    buildAccountId(id,proto,user,host);
		else {
		    updAccDelOld(old,this);
		    Debug(ClientDriver::self(),DebugNote,
			"Ignoring loaded account '%s' proto=%s user=%s host=%s",
			account.c_str(),proto.c_str(),user.c_str(),host);
		    return false;
		}
	    }
	}
	if (!id)
	    acc = new ClientAccount(account);
	else {
	    NamedList p(account);
	    if (id != account) {
		Debug(ClientDriver::self(),DebugInfo,
		    "Renaming loaded account '%s' to '%s'",
		    account.c_str(),id.c_str());
		p.assign(id);
	    }
	    acc = new ClientAccount(p);
	    if (id != account)
		acc->m_params.setParam("old_id",account.c_str());
	}
	if (loaded && !acc->params().getParam(YSTRING("savepassword")))
	    acc->m_params.setParam("savepassword",
		String::boolText(0 != acc->params().getParam(YSTRING("password"))));
	if (!m_accounts->appendAccount(acc)) {
	    updAccDelOld(old,this);
	    Debug(ClientDriver::self(),DebugNote,
		"Failed to append duplicate account '%s'",acc->toString().c_str());
	    TelEngine::destruct(acc);
	    return false;
	}
	changed = true;
    }
    if (!changed) {
	updAccDelOld(old,this);
	TelEngine::destruct(acc);
	return true;
    }
    // Clear pending params
    acc->m_params.clearParam(YSTRING("internal.status"),'.');
    // (Re)set account own contact
    setAccountContact(acc);
    // Update account list
    NamedList p("");
    acc->fillItemParams(p);
    p.addParam("check:enabled",String::boolText(acc->startup()));
    p.addParam("status_image",resStatusImage(acc->resource().m_status),false);
    Client::self()->updateTableRow(s_accountList,acc->toString(),&p);
    // Make sure the account is selected in accounts list
    Client::self()->setSelect(s_accountList,acc->toString());
    // Update telephony account selector(s)
    updateTelAccList(acc->startup(),acc);
    // Reset selection if loaded: it will be set in setAdvancedMode() if appropriate
    if (loaded)
	Client::self()->setSelect(s_account,s_notSelected);
    setAdvancedMode();
    // (Dis)connect account
    if (acc->resource().offline()) {
	if (acc->startup())
	    setAccountStatus(m_accounts,acc);
    }
    else {
	Engine::enqueue(userLogin(acc,false));
	acc->m_params.setParam("internal.reconnect",String::boolText(true));
    }
    // Clear saved rooms
    updateChatRoomsContactList(false,acc);
    acc->clearRooms(true,false);
    acc->m_cfg.assign("");
    acc->m_cfg.clearSection();
    // Update dir data
    acc->m_params.setParam("datadirectory",oldDataDir);
    String error;
    if (acc->setupDataDir(&error)) {
	acc->loadDataDirCfg();
	acc->loadContacts();
    }
    else
	notifyGenericError(error,acc->toString());
    // Save the account
    if (save)
	acc->save(true,acc->params().getBoolValue(YSTRING("savepassword")));
    TelEngine::destruct(acc);
    updAccDelOld(old,this);
    return true;
}

// Add/edit an account
bool DefaultLogic::internalEditAccount(bool newAcc, const String* account, NamedList* params,
    Window* wnd)
{
    if (!Client::valid() || Client::self()->getVisible(s_wndAccount))
	return false;
    NamedList dummy("");
    if (!params)
	params = &dummy;
    // Make sure we reset all controls in window
    params->setParam("select:" + s_accProviders,s_notSelected);
    String proto;
    ClientAccount* a = 0;
    if (newAcc) {
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
	s_protocolsMutex.unlock();
    }
    else {
	if (TelEngine::null(account))
	    a = selectedAccount(*m_accounts,wnd);
	else
	    a = m_accounts->findAccount(*account);
	if (!a)
	    return false;
	proto = a->protocol();
    }
    const String& acc = a ? a->toString() : String::empty();
    // Protocol combo and specific widget (page) data
    bool adv = Client::s_settings.getBoolValue("client","acc_showadvanced",true);
    params->setParam("check:acc_showadvanced",String::boolText(adv));
    selectProtocolSpec(*params,proto,adv,s_accProtocol);
    // Save password ?
    bool save = false;
    if (a)
	save = a->params().getBoolValue(YSTRING("savepassword"));
    params->setParam("check:acc_savepassword",String::boolText(save));
    // Reset all protocol specific data
    updateProtocolList(0,String::empty(),0,params);
    if (a)
	updateProtocolSpec(*params,proto,true,a->params());
    params->setParam("title",newAcc ? "Add account" : ("Edit account: " + acc).c_str());
    params->setParam("context",acc);
    return Client::openPopup(s_wndAccount,params);
}

// Utility used in handleDialogAction() to retrieve the room from context if input
//  is valid
static inline MucRoom* getInput(ClientAccountList* list, const String& id, Window* w,
    String& input, bool emptyOk = false)
{
    if (!(list && id))
	return 0;
    Client::self()->getText(YSTRING("inputdialog_input"),input,false,w);
    return (emptyOk || input) ? list->findRoom(id) : 0;
}

// Handle dialog actions. Return true if handled
bool DefaultLogic::handleDialogAction(const String& name, bool& retVal, Window* wnd)
{
    String n(name);
    if (!n.startSkip("dialog:",false))
	return false;
    int pos = n.find(":");
    if (pos < 0)
	return false;
    String dlg = n.substr(0,pos);
    String ctrl = n.substr(pos + 1);
    DDebug(ClientDriver::self(),DebugAll,
	"DefaultLogic handleDialogAction(%s) dlg=%s action=%s wnd=%s",
	name.c_str(),dlg.c_str(),ctrl.c_str(),wnd ? wnd->id().c_str() : "");
    if (ctrl == "button_hide") {
	retVal = true;
	return true;
    }
    if (ctrl != YSTRING("ok"))
	return false;
    String context;
    if (wnd && Client::valid())
	Client::self()->getProperty(dlg,YSTRING("_yate_context"),context,wnd);
    // Handle OK
    if (dlg == s_mucChgSubject) {
	// Accept MUC room subject change
	String subject;
	MucRoom* room = getInput(m_accounts,context,wnd,subject,true);
	retVal = room && room->canChangeSubject();
	if (retVal) {
	    Message* m = room->buildMucRoom("setsubject");
	    m->addParam("subject",subject);
	    retVal = Engine::enqueue(m);
	}
    }
    else if (dlg == s_mucChgNick) {
	// Accept MUC room nick change
	String nick;
	MucRoom* room = getInput(m_accounts,context,wnd,nick);
	retVal = room && room->resource().online();
	if (retVal && nick != room->resource().m_name) {
	    if (!isGoogleMucDomain(room->uri().getHost())) {
		Message* m = room->buildMucRoom("setnick");
		m->addParam("nick",nick);
		retVal = Engine::enqueue(m);
	    }
	    else {
		// Google MUC: send unavailable first
		Message* m = room->buildJoin(false);
		if (Engine::enqueue(m)) {
		    m = room->buildJoin(true);
		    m->setParam("nick",nick);
		    retVal = Engine::enqueue(m);
		}
	    }
	}
    }
    else if (dlg == s_mucInviteAdd) {
	// Add contact to muc invite
	String contact;
	Client::self()->getText(YSTRING("inputdialog_input"),contact,false,wnd);
	String user, domain;
	splitContact(contact,user,domain);
	retVal = user && domain;
	if (retVal && Client::valid() &&
	    !Client::self()->getTableRow(s_inviteContacts,contact,0,wnd)) {
	    NamedList row("");
	    row.addParam("name",contact);
	    row.addParam("contact",contact);
	    row.addParam("check:name",String::boolText(true));
	    row.addParam("name_image",Client::s_skinPath + "addcontact.png");
	    Client::self()->addTableRow(s_inviteContacts,contact,&row,false,wnd);
	}
    }
    else
	retVal = context && Client::self()->action(wnd,context);
    return true;
}

// Handle chat and contact related actions. Return true if handled
bool DefaultLogic::handleChatContactAction(const String& name, Window* wnd)
{
    ClientContact* c = 0;
    MucRoom* room = 0;
    String id;
    // Send chat action from single chat window, docked chat or MUC room
    bool ok = getPrefixedContact(name,s_chatSend,id,m_accounts,&c,&room);
    if (ok || name == s_chatSend) {
	// Single chat window
	if (!ok && wnd && wnd->context())
	    c = m_accounts->findContact(wnd->context());
	if (c) {
	    DDebug(ClientDriver::self(),DebugAll,
		"DefaultLogic sending chat for contact=%s",c->toString().c_str());
	    String text;
	    c->getChatInput(text);
	    if ((text || Client::self()->getBoolOpt(Client::OptSendEmptyChat)) &&
		c->sendChat(text)) {
		unsigned int time = Time::secNow();
		NamedList* tmp = buildChatParams(text,"me",time);
		c->setChatProperty("history","_yate_tempitemreplace",String(false));
		c->addChatHistory("chat_out",tmp);
		c->setChatProperty("history","_yate_tempitemreplace",String(true));
		c->setChatInput();
		if (text)
		    logChat(c,time,true,false,text);
	    }
	}
	else if (room) {
	    MucRoomMember* m = id ? room->findMemberById(id) : 0;
	    if (!m)
		return false;
	    DDebug(ClientDriver::self(),DebugAll,
		"DefaultLogic sending MUC chat room=%s nick=%s",
		room->uri().c_str(),m->m_name.c_str());
	    String text;
	    room->getChatInput(id,text);
	    bool ok = text || Client::self()->getBoolOpt(Client::OptSendEmptyChat);
	    if (room->ownMember(m))
		ok = ok && room->sendChat(text,String::empty(),"groupchat");
	    else
		ok = ok && room->sendChat(text,m->m_name);
	    if (ok) {
		unsigned int time = Time::secNow();
		NamedList* tmp = buildChatParams(text,"me",time);
		room->setChatProperty(id,"history","_yate_tempitemreplace",String(false));
		room->addChatHistory(id,"chat_out",tmp);
		room->setChatProperty(id,"history","_yate_tempitemreplace",String(true));
		room->setChatInput(id);
		if (text)
		    logChat(room,time,true,false,text,room->ownMember(m),m->m_name);
	    }
	}
	else
	    return false;
	return true;
    }
    // Show contact chat
    if (name == s_chat || name == s_chatContactList) {
	ClientContact* c = selectedChatContact(*m_accounts,wnd);
	if (!c)
	    return false;
	MucRoom* r = c->mucRoom();
	if (!r) {
	    if (!c->hasChat()) {
		c->createChatWindow();
		NamedList p("");
		fillChatContact(p,*c,true,true);
		ClientResource* res = c->status();
		c->updateChatWindow(p,"Chat [" + c->m_name + "]",
		    resStatusImage(res ? res->m_status : ClientResource::Offline));
	    }
	    c->showChat(true,true);
	}
	else if (checkGoogleRoom(r->uri(),wnd))
	    joinRoom(r);
	return true;
    }
    // Call chat contact
    if (name == s_chatCall) {
	ClientContact* c = selectedChatContact(*m_accounts,wnd,false);
	if (!c)
	    return false;
	ClientResource* res = c->findAudioResource();
	if (!res)
	    return false;
	NamedList p("");
	p.addParam("line",c->accountName(),false);
	p.addParam("account",c->accountName(),false);
	p.addParam("target",c->uri());
	p.addParam("instance",res->toString());
	if (c->account())
	    p.addParam("protocol",c->account()->protocol(),false);
	return callStart(p);
    }
    // Show chat contact log
    if (name == s_chatShowLog) {
	ClientContact* c = selectedChatContact(*m_accounts,wnd);
	return logShow(c);
    }
    // Edit chat contact
    if (name == s_chatEdit) {
	ClientContact* c = selectedChatContact(*m_accounts,wnd);
	return c && showContactEdit(*m_accounts,false,c);
    }
    if (getPrefixedContact(name,s_chatEdit,id,m_accounts,&c,&room) && c) {
	bool ok = showContactEdit(*m_accounts,false,c);
	if (ok && wnd) {
	    // Hide contact info window
	    Window* w = getContactInfoEditWnd(false,false,c);
	    if (wnd == w)
		Client::self()->closeWindow(wnd->id());
	}
	return ok;
    }
    // Add chat contact
    if (name == s_chatNew)
	return showContactEdit(*m_accounts,false);
    if (name == s_chatRoomNew) {
	s_mucWizard->start(true);
	return true;
    }
    // Remove chat contact
    if (name == s_chatDel)
	return deleteSelectedItem(s_chatContactList + ":",wnd);
    // Show chat contact info
    if (name == s_chatInfo) {
	ClientContact* c = selectedChatContact(*m_accounts,wnd,false);
	return updateContactInfo(c,true,true);
    }
    // Subscription management
    bool sub = (name == s_chatSub);
    bool unsubd = !sub && (name == s_chatUnsubd);
    if (sub || unsubd || name == s_chatUnsub) {
	ClientContact* c = selectedChatContact(*m_accounts,wnd,false);
	if (!c)
	    return false;
	if (!unsubd)
	    Engine::enqueue(Client::buildSubscribe(true,sub,c->accountName(),c->uri()));
	else
	    Engine::enqueue(Client::buildSubscribe(false,false,c->accountName(),c->uri()));
	return true;
    }
    // Add group in contact edit/add window
    if (name == "contactedit_addgroup") {
	if (!(Client::valid() && wnd))
	    return false;
	String grp;
	Client::self()->getText(YSTRING("editgroup"),grp,false,wnd);
	if (!grp)
	    return false;
	NamedList upd("");
	NamedList* p = new NamedList(grp);
	p->addParam("group",grp);
	p->addParam("check:group",String::boolText(true));
	upd.addParam(new NamedPointer(grp,p,String::boolText(true)));
	if (Client::self()->updateTableRows(YSTRING("groups"),&upd,false,wnd))
	    Client::self()->setText(YSTRING("editgroup"),String::empty(),false,wnd);
	return true;
    }
    // Invite chat contacts, create room
    ok = getPrefixedContact(name,s_mucInvite,id,m_accounts,&c,0);
    if (ok || name == s_mucInvite) {
	if (!ok && wnd && wnd->context())
	    c = m_accounts->findContact(wnd->context());
	if (!c)
	    return false;
	showMucInvite(*c,m_accounts);
	return true;
    }
    // Store contact
    if (getPrefixedContact(name,s_storeContact,id,m_accounts,0,&room)) {
	if (room)
	    updateChatRoomsContactList(room->local() || room->remote(),0,room);
	return storeContact(room);
    }
    return false;
}

// Handle chat contact edit ok button press. Return true if handled
bool DefaultLogic::handleChatContactEditOk(const String& name, Window* wnd)
{
    static const String s_cceokname = "contactedit_ok";
    if (name != s_cceokname)
	return false;
    if (!(Client::valid() && wnd))
	return true;
    String contact;
    ClientAccount* a = 0;
    if (wnd->context()) {
	// Edit
	ClientContact* c = m_accounts->findContact(wnd->context());
	if (c) {
	    a = c->account();
	    contact = c->uri();
	}
	if (!a) {
	    // Try to retrieve from data
	    String account;
	    Client::self()->getText(YSTRING("chatcontact_account"),account,false,wnd);
	    a = m_accounts->findAccount(account);
	    if (!a) {
		showError(wnd,"Account does not exists");
		return true;
	    }
	    Client::self()->getText(YSTRING("chatcontact_uri"),contact,false,wnd);
	}
    }
    else {
	a = selectedAccount(*m_accounts,wnd,s_chatAccount);
	if (!a) {
	    showAccSelect(wnd);
	    return true;
	}
	String user, domain;
	Client::self()->getText(YSTRING("username"),user,false,wnd);
	Client::self()->getText(YSTRING("domain"),domain,false,wnd);
	if (!checkUri(wnd,user,domain,false))
	    return true;
	contact << user << "@" << domain;
	// Check unique
	if (a->findRoomByUri(contact)) {
	    showRoomDupError(wnd);
	    return true;
	}
    }
    if (!a->resource().online()) {
	showError(wnd,"Selected account is offline");
	return true;
    }
    String cname;
    Client::self()->getText(YSTRING("name"),cname,false,wnd);
    bool reqSub = false;
    if (!wnd->context())
	Client::self()->getCheck(YSTRING("request_subscribe"),reqSub,wnd);
    NamedList p("");
    Client::self()->getOptions(YSTRING("groups"),&p,wnd);
    Message* m = Client::buildUserRoster(true,a->toString(),contact);
    m->addParam("name",cname,false);
    unsigned int n = p.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = p.getParam(i);
	if (!(ns && ns->name()))
	    continue;
	NamedList pp("");
	Client::self()->getTableRow(YSTRING("groups"),ns->name(),&pp,wnd);
	if (pp.getBoolValue(YSTRING("check:group")))
	    m->addParam("group",ns->name(),false);
    }
    Engine::enqueue(m);
    if (reqSub)
	Engine::enqueue(Client::buildSubscribe(true,true,a->toString(),contact));
    Client::self()->setVisible(wnd->id(),false);
    return true;
}

// Handle chat room contact edit ok button press. Return true if handled
bool DefaultLogic::handleChatRoomEditOk(const String& name, Window* wnd)
{
    static const String s_creokname = "chatroomedit_ok";
    if (name != s_creokname)
	return false;
    if (!(Client::valid() && wnd))
	return false;
    ClientAccount* a = selectedAccount(*m_accounts,wnd,s_chatAccount);
    if (!a)
	return showAccSelect(wnd);
    // Retrieve user/domain
    String user, domain;
    Client::self()->getText(YSTRING("room_room"),user,false,wnd);
    Client::self()->getText(YSTRING("room_server"),domain,false,wnd);
    if (!checkUri(wnd,user,domain,true))
	return false;
    // Check if changed
    String id;
    String contact(user + "@" + domain);
    ClientContact::buildContactId(id,a->toString(),contact);
    MucRoom* room = a->findRoom(id);
    if (wnd->context()) {
	MucRoom* e = 0;
	if (wnd->context() != id)
	    e = m_accounts->findRoom(wnd->context());
	if (e) {
	    // Reset non temporary old one
	    // Delete it if don't have chat displayed
	    if (e->local() || e->remote()) {
		e->setLocal(false);
		e->setRemote(false);
		updateChatRoomsContactList(false,0,e);
		storeContact(e);
	    }
	    if (!e->hasChat(e->resource().toString()))
		TelEngine::destruct(e);
	}
    }
    room = 0;
    bool dataChanged = false;
    bool changed = getRoom(wnd,a,true,!wnd->context(),room,dataChanged);
    if (!room)
	return false;
    updateChatRoomsContactList(true,0,room);
    if (dataChanged)
	storeContact(room);
    if (room->m_params.getBoolValue(YSTRING("autojoin")))
	joinRoom(room,changed);
    Client::self()->setVisible(wnd->id(),false);
    return true;
}

// Handle actions from MUCS window. Return true if handled
bool DefaultLogic::handleMucsAction(const String& name, Window* wnd, NamedList* params)
{
    XDebug(ClientDriver::self(),DebugAll,"DefaultLogic::handleMucsAction(%s)",
	name.c_str());
    MucRoom* room = 0;
    String id;
    if (getPrefixedContact(name,s_mucMembers,id,m_accounts,0,&room) ||
	getPrefixedContact(name,s_mucPrivChat,id,m_accounts,0,&room)) {
	// Handle item pressed in members list or show private chat
	MucRoomMember* member = room ? selectedRoomMember(*room) : 0;
	if (member && !room->ownMember(member) && room->canChatPrivate())
	    createRoomChat(*room,member,true);
	return member != 0;
    }
    if (getPrefixedContact(name,s_mucChgSubject,id,m_accounts,0,&room)) {
	// Change room subject
	if (room && room->ownMember(id) && room->canChangeSubject()) {
	    String text;
	    text << "Change room '" << room->uri() << "' subject";
	    showInput(wnd,s_mucChgSubject,text,room->toString(),"Change room subject");
	}
	return true;
    }
    if (getPrefixedContact(name,s_mucChgNick,id,m_accounts,0,&room)) {
	// Change room nickname
	if (room && room->ownMember(id)) {
	    String text;
	    text << "Change nickname in room '" << room->uri() << "'";
	    showInput(wnd,s_mucChgNick,text,room->toString(),"Change nickname");
	}
	return true;
    }
    if (getPrefixedContact(name,s_mucInvite,id,m_accounts,0,&room)) {
	// Invite contacts to conference
	if (!room)
	    return false;
	showMucInvite(*room,m_accounts);
	return true;
    }
    if (getPrefixedContact(name,s_mucRoomShowLog,id,m_accounts,0,&room)) {
	// Show MUC room log
	if (!room)
	    return false;
	logShow(room,true);
	return true;
    }
    if (getPrefixedContact(name,s_mucMemberShowLog,id,m_accounts,0,&room)) {
	// Show MUC room member log
	MucRoomMember* member = room ? selectedRoomMember(*room) : 0;
	if (!member)
	    return false;
	logShow(room,room->ownMember(member),member->m_name);
	return true;
    }
    bool kick = getPrefixedContact(name,s_mucKick,id,m_accounts,0,&room);
    if (kick || getPrefixedContact(name,s_mucBan,id,m_accounts,0,&room)) {
	MucRoomMember* member = room ? selectedRoomMember(*room) : 0;
	if (!member || room->ownMember(member))
	    return false;
	if (kick) {
	    if (room->canKick(member)) {
		// TODO: implement reason input from user
		Message* m = room->buildMucRoom("kick");
		m->addParam("nick",member->m_name);
		Engine::enqueue(m);
	    }
	}
	else if (room->canBan(member) && member->m_uri) {
	    Message* m = room->buildMucRoom("ban");
	    m->addParam("contact",member->m_uri);
	    Engine::enqueue(m);
	}
	return true;
    }
    // Save/edit chat room contact
    if (getPrefixedContact(name,s_mucSave,id,m_accounts,0,&room))
	return room && showContactEdit(*m_accounts,true,room);
    // Join MUC room
    if (getPrefixedContact(name,s_mucJoin,id,m_accounts,0,&room)) {
	joinRoom(room,params && params->getBoolValue(YSTRING("force")));
	return room != 0;
    }
    // Add contact to muc invite list
    if (name == s_mucInviteAdd) {
	showInput(wnd,name,"Invite friend to conference",name,"Invite friend");
	return true;
    }
    return false;
}

// Handle 'ok' in MUC invite window
bool DefaultLogic::handleMucInviteOk(Window* w)
{
    if (!(w && Client::valid() ))
	return false;
    String account;
    Client::self()->getText("invite_account",account,false,w);
    ClientAccount* acc = m_accounts->findAccount(account);
    if (!acc) {
	showError(w,"Account not found!");
	return false;
    }
    String room;
    Client::self()->getText("invite_room",room,false,w);
    MucRoom* r = 0;
    if (room) {
	r = acc->findRoomByUri(room);
	if (!r) {
	    showError(w,"MUC room not found!");
	    return false;
	}
    }
    else {
	String guid;
	Client::generateGuid(guid,account);
	String uri = "private-chat-" + guid;
	uri << "@" << (isGmailAccount(acc) ? s_googleMucDomain : "conference.jabber.org");
	String id;
	ClientContact::buildContactId(id,account,uri);
	r = acc->findRoom(id);
	if (!r)
	    r = new MucRoom(acc,id,"",uri);
    }
    String text;
    Client::self()->getText(YSTRING("invite_text"),text,false,w);
    ObjList chosen;
    getSelectedContacts(chosen,s_inviteContacts,w,YSTRING("name"));
    bool inviteNow = (room || r->resource().online());
    unsigned int count = 0;
    r->m_params.clearParam(YSTRING("internal.invite"),'.');
    for (ObjList* o = chosen.skipNull(); o; o = o->skipNext()) {
	NamedList* nl = static_cast<NamedList*>(o->get());
	const String& uri = (*nl)[YSTRING("contact")];
	if (inviteNow)
	    Engine::enqueue(buildMucRoom("invite",account,room,text,uri));
	else {
	    count++;
	    r->m_params.addParam("internal.invite.contact",uri);
	}
    }
    if (!inviteNow) {
	if (count) {
    	    r->m_params.addParam("internal.invite.count",String(count));
    	    r->m_params.addParam("internal.invite.text",text,false);
	}
	joinRoom(r);
    }
    Client::self()->setVisible(w->id(),false);
    return true;
}

// Handle select from MUCS window. Return true if handled
bool DefaultLogic::handleMucsSelect(const String& name, const String& item, Window* wnd,
    const String& text)
{
    MucRoom* room = 0;
    String id;
    if (getPrefixedContact(name,s_mucMembers,id,m_accounts,0,&room)) {
	// Handle selection changes in members list
	MucRoomMember* member = (room && item) ? room->findMemberById(item) : 0;
	if (!room)
	    return false;
	// Enable/disable actions
	NamedList p("");
	enableMucActions(p,*room,member,false);
	room->updateChatWindow(room->resource().toString(),p);
	return true;
    }
    return false;
}

// Handle resource.notify messages from MUC rooms
// The account was already checked
bool DefaultLogic::handleMucResNotify(Message& msg, ClientAccount* acc, const String& contact,
    const String& instance, const String& operation)
{
    if (!acc)
	return false;
    MucRoom* room = acc->findRoomByUri(contact);
    if (!room)
	return false;
    Debug(ClientDriver::self(),DebugAll,
	"Logic(%s) handle MUC notify account=%s contact=%s instance=%s operation=%s",
	name().c_str(),acc->toString().c_str(),contact.c_str(),instance.safe(),
	operation.c_str());
    MucRoomMember* member = 0;
    const String& mucContact = msg[YSTRING("muc.contact")];
    const String& mucInst = msg[YSTRING("muc.contactinstance")];
    String nick;
    if (mucContact && mucInst) {
	member = room->findMember(mucContact,mucInst);
	if (member && room->ownMember(member))
	    nick = instance;
    }
    if (!member)
	member = instance ? room->findMember(instance) : 0;
    if (operation == YSTRING("error")) {
	if (instance && !room->ownMember(member))
	    return false;
	if (room->resource().m_status == ClientResource::Connecting) {
	    // Connection refused
	    String text("Failed to join room");
	    text.append(msg.getValue(YSTRING("reason"),msg.getValue(YSTRING("error"))),": ");
	    addChatNotify(*room,text,msg.msgTime().sec());
	    room->resource().m_status = ClientResource::Offline;
	    updateMucRoomMember(*room,room->resource());
	    room->m_params.clearParam(YSTRING("internal.invite"),'.');
	    room->m_params.clearParam(YSTRING("internal.reconnect"));
	}
	return true;
    }
    if (!instance)
	return false;
    bool online = (operation == YSTRING("online"));
    if (!online && operation != YSTRING("offline"))
	return false;
    // Get user status notifications
    ObjList* list = String(msg.getParam(YSTRING("muc.userstatus"))).split(',');
    bool newRoom = 0 != list->find(YSTRING("newroom"));
    bool ownUser = 0 != list->find(YSTRING("ownuser"));
    bool userKicked = !online && list->find(YSTRING("userkicked"));
    bool userBanned = !online && list->find(YSTRING("userbanned"));
    if (!ownUser && list->find(YSTRING("nickchanged")))
	nick = msg.getParam(YSTRING("muc.nick"));
    TelEngine::destruct(list);
    // Retrieve the member
    if (!member && online) {
	if (ownUser) {
	    member = &(room->resource());
	    nick = instance;
	}
	else
	    member = static_cast<MucRoomMember*>(room->appendResource(instance));
    }
    if (!member)
	return false;
    // Update contact UI. Set some chat history messages
    if (userKicked || userBanned) {
	String tmp(member->m_name + " was ");
	const char* by = 0;
	const char* r = 0;
	if (userKicked) {
	    tmp << "kicked";
	    by = msg.getValue(YSTRING("muc.userkicked.by"));
	    r = msg.getValue(YSTRING("muc.userkicked.reason"));
	}
	else {
	    tmp << "banned";
	    by = msg.getValue(YSTRING("muc.userbanned.by"));
	    r = msg.getValue(YSTRING("muc.userbanned.reason"));
	}
	if (!TelEngine::null(by))
	    tmp << " by " << by;
	if (!TelEngine::null(r))
	    tmp << " (" << r << ")";
	addChatNotify(*room,tmp,msg.msgTime().sec());
    }
    bool changed = false;
    // Update role and affiliation
    const String& roleStr = msg[YSTRING("muc.role")];
    int role = lookup(roleStr,MucRoomMember::s_roleName);
    if (role != MucRoomMember::RoleUnknown && role != member->m_role) {
	Debug(ClientDriver::self(),DebugAll,
	    "Logic(%s) account=%s room=%s nick=%s role set to '%s'",
	    name().c_str(),acc->toString().c_str(),room->uri().c_str(),
	    member->m_name.c_str(),roleStr.c_str());
	member->m_role = role;
	changed = true;
	if (role != MucRoomMember::RoleNone) {
	    // Notify role change
	    String text;
	    if (room->ownMember(member))
		text << "You are now a ";
	    else
		text << member->m_name + " is now a ";
	    addChatNotify(*room,text + roleStr + " in the room",msg.msgTime().sec());
	}
    }
    int aff = msg.getIntValue("muc.affiliation",MucRoomMember::s_affName);
    if (aff != MucRoomMember::AffUnknown && aff != member->m_affiliation) {
	Debug(ClientDriver::self(),DebugAll,
	    "Logic(%s) account=%s room=%s nick=%s affiliation set to '%s'",
	    name().c_str(),acc->toString().c_str(),room->uri().c_str(),
	    member->m_name.c_str(),msg.getValue("muc.affiliation"));
	member->m_affiliation = aff;
	if (member->m_affiliation == MucRoomMember::Outcast) {
	    String text;
	    if (room->ownMember(member))
		text << "You are";
	    else
		text << member->m_name + " is";
	    text << " no longer a room member";
	    addChatNotify(*room,text,msg.msgTime().sec());
	}
	changed = true;
    }
    // Update status
    if (online != member->online()) {
	// Create the room by setting a default config if this a new one
	if (online && room->ownMember(member) && newRoom &&
	    room->resource().m_status == ClientResource::Connecting &&
	    member->m_affiliation == MucRoomMember::Owner)
	    Engine::enqueue(room->buildMucRoom("setconfig"));
	if (member->m_status < ClientResource::Online)
	    member->m_status = ClientResource::Online;
	else
	    member->m_status = ClientResource::Offline;
	if (!room->ownMember(member)) {
	    String text(member->m_name);
	    text << " is " << lookup(member->m_status,ClientResource::s_statusName);
	    addChatNotify(*room,text,msg.msgTime().sec());
	}
	changed = true;
	if (member->m_status == ClientResource::Online && room->ownMember(member)) {
	    // Send invitations
	    unsigned int count = room->m_params.getIntValue(YSTRING("internal.invite.count"));
	    if (count) {
		const String& text = room->m_params[YSTRING("internal.invite.text")];
		NamedIterator iter(room->m_params);
		for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
		    if (ns->name() == YSTRING("internal.invite.contact"))
			Engine::enqueue(buildMucRoom("invite",acc->toString(),
			    room->uri(),text,*ns));
		}
	    }
	    room->m_params.clearParam(YSTRING("internal.invite"),'.');
	}
	// Re-connect?
	if (room->ownMember(member) && !online &&
	    room->m_params.getBoolValue(YSTRING("internal.reconnect"))) {
	    room->m_params.clearParam(YSTRING("internal.reconnect"));
	    joinRoom(room);
	}
    }
    // Update contact/instance
    if (!room->ownMember(member)) {
	if (mucContact)
	    changed = setChangedString(member->m_uri,mucContact) || changed;
	if (mucInst)
	    changed = setChangedString(member->m_instance,mucInst) || changed;
    }
    // Handle nick changes
    if (nick && nick != member->m_name) {
	String text;
	if (room->ownMember(member))
	    text << "You are";
	else {
	    text << member->m_name << " is";
	    // Close old member's chat log
	    logCloseSession(room,false,member->m_name);
	}
	text << " now known as " << nick;
	addChatNotify(*room,text,msg.msgTime().sec());
	member->m_name = nick;
	changed = true;
    }
    // Update
    if (changed) {
	updateMucRoomMember(*room,*member,&msg);
	if (acc->resource().online() && room->ownMember(member) &&
	    (room->local() || room->remote()))
	    updateChatRoomsContactList(true,0,room);
    }
    return true;
}

// Show/hide the notification area (messages)
bool DefaultLogic::showNotificationArea(bool show, Window* wnd, NamedList* upd,
    const char* notif)
{
    if (!Client::self())
	return false;
    if (upd) {
	Client::self()->updateTableRows(YSTRING("messages"),upd,false,wnd);
	addTrayIcon(notif);
    }
    else if (!show)
	removeTrayIcon(notif);
    NamedList p("");
    const char* ok = String::boolText(show);
    p.addParam("check:messages_show",ok);
    p.addParam("show:frame_messages",ok);
    Client::self()->setParams(&p,wnd);
    if (wnd)
	Client::self()->setUrgent(wnd->id(),true,wnd);
    return true;
}

// Show a roster change or failure notification
void DefaultLogic::showUserRosterNotification(ClientAccount* a, const String& oper,
    Message& msg, const String& contactUri, bool newContact)
{
    if (!a)
	return;
    NamedList list("");
    NamedList* upd = 0;
    String text;
    const char* firstButton = 0;
    bool update = (oper == YSTRING("update"));
    const char* notif = "notification";
    ClientContact* c = contactUri ? a->findContactByUri(contactUri) : 0;
    String cName;
    if (c)
	buildContactName(cName,*c);
    else
	cName = contactUri;
    if (update || oper == YSTRING("delete")) {
	if (!c)
	    return;
	notif = "info";
	upd = buildNotifArea(list,"generic",a->toString(),contactUri,
	    "Friends list changed");
	text << (update ? (newContact ? "Added" : "Updated") : "Removed");
	text << " friend " << cName;
    }
    else if (oper == YSTRING("error")) {
	if (!contactUri)
	    return;
	ClientContact* c = a->findContactByUri(contactUri);
	const String& req = msg["requested_operation"];
	const char* what = 0;
	if (req == "update") {
	    upd = buildNotifArea(list,"contactupdatefail",a->toString(),
		contactUri,"Friend update failure");
	    what = (c ? "update" : "add");
	}
	else if (req == YSTRING("delete")) {
	    if (!c)
		return;
	    upd = buildNotifArea(list,"contactremovefail",a->toString(),
		contactUri,"Friend delete failure");
	    what = "remove";
	}
	else
	    return;
	text << "Failed to " << what << " friend " << cName;
	addError(text,msg);
    }
    else if (oper == YSTRING("queryerror")) {
	upd = buildNotifArea(list,"rosterreqfail",a->toString(),String::empty(),
	    "Friends list failure");
	firstButton = "Retry";
	text << "Failed to retrieve the friends list";
	addError(text,msg);
    }
    else {
	if (oper == YSTRING("result"))
	    Debug(ClientDriver::self(),DebugAll,"Contact %s for '%s' account=%s confirmed",
		msg.getValue("requested_operation"),msg.getValue("contact"),
		a->toString().c_str());
	return;
    }
    setGenericNotif(*upd,firstButton);
    Debug(ClientDriver::self(),DebugAll,"Account '%s'. %s",
	a->toString().c_str(),text.c_str());
    text << "\r\nAccount: " << a->toString();
    upd->addParam("text",text);
    showNotificationArea(true,Client::self()->getWindow(s_wndMain),&list,notif);
}

// Handle actions from notification area. Return true if handled
bool DefaultLogic::handleNotificationAreaAction(const String& action, Window* wnd)
{
    String id = action;
    const TokenDict* act = s_notifPrefix;
    for (; act && act->token; act++)
	if (id.startSkip(act->token,false))
	    break;
    if (!(act && act->token))
	return false;
    NamedList p("");
    Client::self()->getTableRow(YSTRING("messages"),id,&p,wnd);
    const String& type = p[YSTRING("item_type")];
    const String& account = p[YSTRING("account")];
    if (!(type && account))
	return false;
    bool handled = true;
    bool remove = true;
    if (type == YSTRING("subscription")) {
	const String& contact = p[YSTRING("contact")];
	if (!contact)
	    return false;
	if (act->value == PrivNotificationOk) {
	    Engine::enqueue(Client::buildSubscribe(false,true,account,contact));
	    Engine::enqueue(Client::buildSubscribe(true,true,account,contact));
	}
	else if (act->value == PrivNotificationReject)
	    Engine::enqueue(Client::buildSubscribe(false,false,account,contact));
	else
	    handled = false;
    }
    else if (type == YSTRING("loginfail")) {
	if (act->value == PrivNotificationLogin) {
	    ClientAccount* acc = m_accounts->findAccount(account);
	    remove = acc && ::loginAccount(this,acc->params(),true);
	}
	else if (act->value == PrivNotificationAccEdit)
	    remove = internalEditAccount(false,&account,0,wnd);
	else if (act->value == PrivNotificationAccounts) {
	    Window* w = Client::self()->getWindow(s_wndAcountList);
	    if (w) {
		Client::self()->setSelect(s_accountList,account,w);
		remove = Client::self()->setVisible(s_wndAcountList,true,true);
	    }
	}
	else
	    handled = false;
    }
    else if (type == YSTRING("mucinvite")) {
	const String& room = p[YSTRING("room")];
	if (!room)
	    return false;
	if (act->value == PrivNotificationOk) {
	    ClientAccount* acc = m_accounts->findAccount(account);
	    if (acc) {
		NamedList params("");
		params.addParam("room_account",acc->toString());
		params.addParam("room_uri",room);
		// Check if we already have a room, use its nickname
		const char* nick = 0;
		MucRoom* r = acc->findRoomByUri(room);
		if (r)
		    nick = r->m_params.getValue(YSTRING("nick"));
		else if (acc->contact())
		    nick = acc->contact()->uri().getUser();
		params.addParam("room_nick",nick);
		params.addParam("room_password",p[YSTRING("password")]);
		params.addParam("check:room_history",String::boolText(true));
		s_tempWizards.append(new JoinMucWizard(m_accounts,&params));
	    }
	    else
		remove = false;
	}
	else if (act->value == PrivNotificationReject) {
	    Message* m = buildMucRoom("decline",account,String::empty());
	    m->copyParams(p,YSTRING("room,contact,contact_instance"));
	    // TODO: implement reason
	    Engine::enqueue(m);
	}
	else
	    handled = false;
    }
    else if (type == YSTRING("incomingfile")) {
	const String& chan = p[YSTRING("targetid")];
	if (chan) {
	    if (act->value == PrivNotificationOk) {
		const String& file = p[YSTRING("file_name")];
		if (file)
		    remove = !chooseFileTransfer(false,s_fileOpenRecvPrefix + id,wnd,file);
	    }
	    else {
		Message* m = Client::buildMessage("call.drop",String::empty());
		m->addParam("id",chan);
		m->addParam("reason","rejected");
		Engine::enqueue(m);
		remove = true;
	    }
	}
    }
    else if (type == YSTRING("rosterreqfail")) {
	if (act->value == PrivNotification1)
	    remove = queryRoster(m_accounts->findAccount(account));
    }
    else
	return false;
    if (handled) {
	if (remove)
	    Client::self()->delTableRow(YSTRING("messages"),id,wnd);
    }
    else
	Debug(ClientDriver::self(),DebugStub,"Unhandled notification area action='%s' type=%s",
	    act->token,type.c_str());
    return handled;
}

// Save a contact
bool DefaultLogic::storeContact(ClientContact* c)
{
    ClientAccount* a = c ? c->account() : 0;
    if (!a)
	return false;
    MucRoom* room = c->mucRoom();
    if (!room)
	return false;
    if (room->local()) {
	String error;
	if (!(a->setupDataDir(&error) && saveContact(a->m_cfg,room))) {
	    String text;
	    text << "Failed to save chat room " << room->uri();
	    text.append(error,"\r\n");
	    notifyGenericError(text,a->toString(),room->uri());
	}
    }
    else
	ClientLogic::clearContact(a->m_cfg,room);
    Engine::enqueue(a->userData(true,"chatrooms"));
    return true;
}

// Handle ok from account password/credentials input window
bool DefaultLogic::handleAccCredInput(Window* wnd, const String& name, bool inputPwd)
{
    ClientAccount* acc = name ? m_accounts->findAccount(name) : 0;
    if (!acc)
	return false;
    String prefix = inputPwd ? "inputpwd_" : "inputacccred_";
    String pwd;
    Client::self()->getText(prefix + "password",pwd,false,wnd);
    if (!pwd)
	return showError(wnd,"Account password is mandatory");
    // Check username changes
    if (!inputPwd) {
	String user;
	Client::self()->getText(prefix + "username",user,false,wnd);
	if (!user)
	    return showError(wnd,"Account username is mandatory");
	if (user != acc->params()[YSTRING("username")]) {
	    String newId;
	    buildAccountId(newId,acc->protocol(),user,
		acc->params().getValue("domain",acc->params().getValue("server")));
	    if (m_accounts->findAccount(newId))
		return showAccDupError(wnd);
	    NamedList account(acc->params());
	    account.assign(newId);
	    account.setParam("username",user);
	    account.setParam("password",pwd);
	    saveCheckParam(account,prefix,YSTRING("savepassword"),wnd);
	    return updateAccount(account,true,name);
	}
    }
    acc->m_params.setParam("password",pwd);
    saveCheckParam(acc->m_params,prefix,YSTRING("savepassword"),wnd);
    acc->save(true,acc->params().getBoolValue(YSTRING("savepassword")));
    if (acc->startup()) {
	setAccountStatus(m_accounts,acc,0,0,false);
	return true;
    }
    return ::loginAccount(this,acc->params(),true,false);
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
