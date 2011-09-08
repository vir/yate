/**
 * jabberclient.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Jabber Client module
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatephone.h>
#include <yatejabber.h>
#include <stdlib.h>

using namespace TelEngine;

namespace { // anonymous

class YStreamReceive;                    // Stream receive thread
class YStreamSetReceive;                 // A list of stream receive threads
class YStreamProcess;                    // Stream process (getEvent()) thread
class YStreamSetProcess;                 // A list of stream process threads
class YJBConnectThread;                  // Stream connect thread
class YJBEntityCapsList;                 // Entity capbilities
class YJBEngine;                         // Jabber engine
class StreamData;                        // Data attached to a stream
class JBMessageHandler;                  // Module message handlers
class JBModule;                          // The module


// Max items in messages dispatched by the module
// This value is used to avoid building large messages
#define JABBERCLIENT_MAXITEMS 50

/*
 * Stream receive thread
 */
class YStreamReceive : public JBStreamSetReceive, public Thread
{
public:
    inline YStreamReceive(JBStreamSetList* owner, Thread::Priority prio = Thread::Normal)
	: JBStreamSetReceive(owner), Thread("JBStreamReceive",prio)
	{}
    virtual bool start()
	{ return Thread::startup(); }
    virtual void stop()
	{ Thread::cancel(); }
protected:
    virtual void run()
	{ JBStreamSetReceive::run(); }
};

/*
 * A list of stream receive threads
 */
class YStreamSetReceive : public JBStreamSetList
{
public:
    inline YStreamSetReceive(JBEngine* engine, unsigned int max, const char* name)
	: JBStreamSetList(engine,max,0,name)
	{}
protected:
    virtual JBStreamSet* build()
	{ return new YStreamReceive(this); }
};

/*
 * Stream process (getEvent()) thread
 */
class YStreamProcess : public JBStreamSetProcessor, public Thread
{
public:
    inline YStreamProcess(JBStreamSetList* owner, Thread::Priority prio = Thread::Normal)
	: JBStreamSetProcessor(owner), Thread("JBStreamProcess",prio)
	{}
    virtual bool start()
	{ return Thread::startup(); }
    virtual void stop()
	{ Thread::cancel(); }
protected:
    virtual void run()
	{ JBStreamSetProcessor::run(); }
};

/*
 * A list of stream process threads
 */
class YStreamSetProcess : public JBStreamSetList
{
public:
    inline YStreamSetProcess(JBEngine* engine, unsigned int max, const char* name)
	: JBStreamSetList(engine,max,0,name)
	{}
protected:
    virtual JBStreamSet* build()
	{ return new YStreamProcess(this); }
};

/*
 * Stream connect thread
 */
class YJBConnectThread : public JBConnect, public Thread
{
public:
    inline YJBConnectThread(const JBStream& stream)
	: JBConnect(stream), Thread("YJBConnectThread")
	{}
    virtual void stopConnect()
	{ cancel(false); }
    virtual void run()
	{ JBConnect::connect(); }
};

/*
 * Entity capability
 */
class YJBEntityCapsList : public JBEntityCapsList
{
public:
    // Load the entity caps file
    void load();
    // Set caps file. Save it if changed
    void setFile(const char* file);
protected:
    inline void getEntityCapsFile(String& file) {
	    Lock mylock(this);
	    file = m_file;
	}
    // Notify changes and save the entity caps file
    virtual void capsAdded(JBEntityCaps* caps);
    // Save the file
    void save();
    String m_file;
};

/*
 * Data attached to a stream
 */
class StreamData : public NamedList
{
public:
    enum ReqType {
	UnknownReq = 0,
	UserRosterUpdate,
	UserRosterRemove,
	UserDataGet,
	UserDataSet,
	DiscoInfo,
	DiscoItems,
    };
    inline StreamData(JBClientStream& m_owner, bool requestRoster)
	: NamedList(m_owner.local().bare()),
	m_requestRoster(requestRoster), m_presence(0),
	m_requests(""), m_reqIndex((unsigned int)Time::msecNow())
	{}
    ~StreamData()
	{ TelEngine::destruct(m_presence); }
    // Retrieve a contact
    inline NamedList* contact(const String& name) {
	    if (name &= *this)
		return this;
	    ObjList* o = find(name);
	    return o ? static_cast<NamedList*>(o->get()) : 0;
	}
    // Append a contact (if not found)
    // This method is thread safe
    inline NamedList* addContact(const String& name) {
	    NamedList* c = contact(name);
	    if (!c) {
		c = new NamedList(name);
		m_contacts.append(c);
	    }
	    return c;
	}
    // Remove a a contact (if not found)
    // This method is thread safe
    inline void removeContact(const String& name) {
	    ObjList* o = find(name);
	    if (o)
		o->remove();
	}
    // Append or update a resource
    inline void setResource(const String& cn, const String& name, const String& capsid) {
	    NamedList* c = name ? contact(cn) : 0;
	    if (c)
		c->setParam(name,capsid);
	}
    // Remove a resource
    // Remove all of them if resource name is empty
    inline void removeResource(const String& cn, const String& name) {
	    NamedList* c = contact(cn);
	    if (!c)
		return;
	    if (name)
		c->clearParam(name);
	    else
		c->clearParams();
	}
    // Set presence params
    void setPresence(const char* prio, const char* show, const char* status);
    // Retrieve a contact
    ObjList* find(const String& name);
    // Add a pending request. Return its id
    void addRequest(ReqType t, const NamedList& params, String& id);
    // Remove a pending request
    bool removeRequest(const String& id);
    // Process a received response. Return true if handled
    bool processResponse(JBEvent* ev, bool ok);
    // Build an online presence element
    static XmlElement* buildPresence(StreamData* d = 0, const char* to = 0);
    // Build a message from a request response
    static Message* message(const char* msg, NamedList& req, bool ok,
	XmlElement* xml);
    // Add a pending request to a stream's data
    static inline void addRequest(JBClientStream* s, ReqType t,
	const NamedList& params, String& id) {
	    Lock lock(s);
	    StreamData* data = s ? static_cast<StreamData*>(s->userData()) : 0;
	    if (data)
		data->addRequest(t,params,id);
	}
    // Remove a pending request from stream's data
    static inline bool removeRequest(JBClientStream* s, const String& id) {
	    Lock lock(s);
	    StreamData* data = s ? static_cast<StreamData*>(s->userData()) : 0;
	    return data && data->removeRequest(id);
	}
    // Process a received response. Return true if handled
    static inline bool processResponse(JBClientStream* s, JBEvent* ev, bool ok) {
	    Lock lock(s);
	    StreamData* data = s ? static_cast<StreamData*>(s->userData()) : 0;
	    return data && data->processResponse(ev,ok);
	}

    // Request roster when connected
    bool m_requestRoster;
    // Presence data
    NamedList* m_presence;
    // Contacts and their resources
    ObjList m_contacts;
    // Pending requests. Each element is a NamedList object
    NamedList m_requests;
    // Request index
    unsigned int m_reqIndex;
};

/*
 * Jabber engine
 */
class YJBEngine : public JBClientEngine
{
public:
    YJBEngine();
    ~YJBEngine();
    // Retrieve features
    const XMPPFeatureList& features() const
	{ return m_features; }
    // Retrieve stream data from a stream
    inline StreamData* streamData(JBClientStream* s)
	{ return s ? static_cast<StreamData*>(s->userData()) : 0; }
    // Retrieve stream data from an event's stream
    inline StreamData* streamData(JBEvent* ev)
	{ return ev ? streamData(ev->clientStream()) : 0; }
    // (Re)initialize the engine
    void initialize(const NamedList* params, bool first = false);
    // Process events
    virtual void processEvent(JBEvent* ev);
    // Start stream TLS
    virtual void encryptStream(JBStream* stream);
    // Connect an outgoing stream
    virtual void connectStream(JBStream* stream);
    // Start stream compression
    virtual void compressStream(JBStream* stream, const String& formats);
    // Process 'user.roster' messages
    bool handleUserRoster(Message& msg, const String& line);
    // Process 'user.update' messages
    bool handleUserUpdate(Message& msg, const String& line);
    // Process 'user.data' messages
    bool handleUserData(Message& msg, const String& line);
    // Process 'contact.info' messages
    bool handleContactInfo(Message& msg, const String& line);
    // Process 'jabber.iq' messages
    bool handleJabberIq(Message& msg, const String& line);
    // Process 'jabber.account' messages
    bool handleJabberAccount(Message& msg, const String& line);
    // Process 'resource.subscribe' messages
    bool handleResSubscribe(Message& msg, const String& line);
    // Process 'resource.notify' messages
    bool handleResNotify(Message& msg, const String& line);
    // Process 'msg.execute' messages
    bool handleMsgExecute(Message& msg, const String& line);
    // Process 'user.login' messages
    bool handleUserLogin(Message& msg, const String& line);
    // Process 'muc.room' messages
    bool handleMucRoom(Message& msg, const String& line);
    // Process 'engine.start' messages
    void handleEngineStart(Message& msg);
    // Handle muc 'message' stanzas (not related to chat)
    // The given event is always valid and carry a valid stream and xml element
    // Return true if the event was handled
    bool processMucMessage(JBEvent* ev);
    // Handle 'presence' stanzas
    // The given event is always valid and carry a valid stream and xml element
    void processPresenceStanza(JBEvent* ev);
    // Handle 'iq' stanzas
    // The given event is always valid and carry a valid stream and xml element
    void processIqStanza(JBEvent* ev);
    // Process stream Running, Destroy, Terminated events
    // The given event is always valid and carry a valid stream
    void processStreamEvent(JBEvent* ev, bool ok);
    // Process stream register result events
    // The given event has a valid element and stream
    void processRegisterEvent(JBEvent* ev, bool ok);
    // Process received roster elements
    void processRoster(JBEvent* ev, XmlElement* service, int tag, int iqType);
    // Process disco info/items responses. Return true if processed
    bool processDiscoRsp(JBEvent* ev, XmlElement* service, int tag, int ns, bool ok);
    // Fill parameters with disco info responses
    void fillDiscoInfo(NamedList& dest, XmlElement* query);
    // Fill parameters with disco items responses
    // Set 'partial'=true and return if JABBERCLIENT_MAXITEMS value was reached
    // Check 'start' on exit: 0 means done
    void fillDiscoItems(NamedList& dest, XmlElement* query, XmlElement*& start);
    // Fill module status
    void statusParams(String& str);
    unsigned int statusDetail(String& str);
    void statusDetail(String& str, const String& name);
    // Complete stream detail
    void streamDetail(String& str, JBStream* stream);
    // Complete stream name starting with partWord
    void completeStreamName(String& str, const String& partWord);
private:
    String m_progName;                   // Program name to be advertised on request
    String m_progVersion;                // Program version to be advertised on request
    XMPPFeatureList m_features;          // Client features
};

/*
 * Module message handlers
 */
class JBMessageHandler : public MessageHandler
{
public:
    // Message handlers
    // Non-negative enum values will be used as handler priority
    enum {
	ResSubscribe   = -1,           // YJBEngine::handleResSubscribe()
	ResNotify      = -2,           // YJBEngine::handleResNotify()
	UserRoster     = -3,           // YJBEngine::handleUserRoster()
	UserUpdate     = -4,           // YJBEngine::handleUserUpdate()
	UserLogin      = -5,           // YJBEngine::handleUserLogin()
	JabberAccount  = -6,           // YJBEngine::handleJabberAccount()
	ContactInfo    = -7,           // YJBEngine::handleContactInfo()
	MucRoom        = -8,           // YJBEngine::handleMucRoom()
	UserData       = -9,           // YJBEngine::handleUserData()
	JabberIq       = 150,          // YJBEngine::handleJabberIq()
    };
    JBMessageHandler(int handler);
protected:
    virtual bool received(Message& msg);
private:
    int m_handler;
};

/*
 * The module
 */
class JBModule : public Module
{
    friend class TcpListener;            // Add/remove to/from list
public:
    // Message relays
    enum {
	EngineStart = Private
    };
    JBModule();
    virtual ~JBModule();
    // Inherited methods
    virtual void initialize();
    // Check if a message was sent by us
    inline bool isModule(const Message& msg) const {
	    String* module = msg.getParam("module");
	    return module && *module == name();
	}
    // Build a Message. Complete module, protocol and line parameters
    inline Message* message(const char* msg, JBClientStream* stream = 0) {
	    Message* m = new Message(msg);
	    complete(*m,stream);
	    return m;
	}
    // Complete module, protocol and line parameters
    inline void complete(Message& m, JBClientStream* stream = 0) {
	    m.addParam("module",name());
	    m.addParam("protocol","jabber");
	    if (stream) {
		m.addParam("account",stream->account());
		m.addParam("line",stream->account());
	    }
	}
    // Retrieve the line (account) from a message
    inline String* getLine(Message& msg) {
	    String* tmp = msg.getParam("line");
	    return tmp ? tmp : msg.getParam("account");
	}
    // Check if this module handles a given protocol
    inline bool canHandleProtocol(const String& proto)
	{ return proto == "jabber"; }
    // List accounts
    void statusAccounts(String& retVal, bool details = true);

protected:
    // Inherited methods
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    virtual bool commandExecute(String& retVal, const String& line);
private:
    bool m_init;
    ObjList m_handlers;                  // Message handlers list
};


/*
 * Local data
 */
INIT_PLUGIN(JBModule);                   // The module
YJBEntityCapsList s_entityCaps;
static YJBEngine* s_jabber = 0;
static String s_priority = "20";         // Default priority for generated presence
static String s_rosterQueryId = "roster-query";
static String s_capsNode = "http://yate.null.ro/yate/client/caps"; // Node for entity capabilities
static String s_yateClientNs = "http://yate.null.ro/yate/client";  // Client namespace
static const String s_reqTypeParam = "jabberclient_requesttype";

// Commands help
static const char* s_cmdStatus = "  status jabberclient stream_name";
static const char* s_cmdDropStream = "  jabberclient drop stream_name|*|all";
static const char* s_cmdDebug = "  jabberclient debug stream_name [debug_level|on|off]";

// Commands handled by this module (format module_name command [params])
static const String s_cmds[] = {
    "drop",
    "debug",
    ""
};

// Message handlers installed by the module
static const TokenDict s_msgHandler[] = {
    {"resource.subscribe",  JBMessageHandler::ResSubscribe},
    {"resource.notify",     JBMessageHandler::ResNotify},
    {"user.roster",         JBMessageHandler::UserRoster},
    {"user.update",         JBMessageHandler::UserUpdate},
    {"user.login",          JBMessageHandler::UserLogin},
    {"jabber.account",      JBMessageHandler::JabberAccount},
    {"contact.info",        JBMessageHandler::ContactInfo},
    {"muc.room",            JBMessageHandler::MucRoom},
    {"user.data",           JBMessageHandler::UserData},
    {"jabber.iq",           JBMessageHandler::JabberIq},
    {0,0}
};

// MUC user status parameter translation table
// XEP0045 Section 15.6.2
static const TokenDict s_mucUserStatus[] = {
    {"nonanonymous",        100},        // The room is non anonymous
    {"ownuser",             110},        // Presence from room on behalf of user itself
    {"publiclog",           170},        // Room chat is logged to a public archive
    {"nopubliclog",         171},        // Room chat is not logged to a public archive
    {"nonanonymous",        172},        // The room is non anonymous
    {"semianonymous",       173},        // The room is semi anonymous
    {"fullanonymous",       174},        // The room is fully anonymous
    {"newroom",             201},        // A new room has been created (initial accept)
    {"nickchanged",         210},        // Nick changed (initial accept)
    {"userbanned",          301},        // User banned
    {"nickchanged",         303},        // User nick changed (unavailable)
    {"userkicked",          307},        // User kicked
    {"userremoved",         321},        // User lost affiliation in a members only room
    {"userremoved",         322},        // Room changed to members only and user is not a member
    {"serviceshutdown",     332},        // The system hosting the service is shutting down
    {0,0}
};

// Find an xml element's child text
static inline const String& getChildText(XmlElement& xml, const String& name,
    XmlElement* start = 0)
{
    XmlElement* child = xml.findNextChild(start,&name);
    return child ? child->getText() : String::empty();
}
 
// Add a child element text to a list of parameters
static inline void addChildText(NamedList& list, XmlElement& parent,
    int tag, int ns, const char* param = 0, bool emptyOk = false)
{
    XmlElement* r = XMPPUtils::findFirstChild(parent,tag,ns);
    if (r)
	list.addParam(param ? param : r->unprefixedTag().c_str(),r->getText(),emptyOk);
}

// Get a space separated word from a buffer
// Return false if empty
static inline bool getWord(String& buf, String& word)
{
    XDebug(&__plugin,DebugAll,"getWord(%s)",buf.c_str());
    int pos = buf.find(" ");
    if (pos >= 0) {
	word = buf.substr(0,pos);
	buf = buf.substr(pos + 1);
    }
    else {
	word = buf;
	buf = "";
    }
    if (!word)
	return false;
    return true;
}

// Decode an error element and set error/reason to a list of params
static void getXmlError(NamedList& list, XmlElement* xml)
{
    if (!xml)
	return;
    String error, reason;
    XMPPUtils::decodeError(xml,reason,error);
    list.addParam("reason",reason,false);
    list.addParam("error",error,false);
}

// Request the roster on a given stream
// Set stream RosterRequested flag
static bool requestRoster(JBStream* stream)
{
    if (!stream || stream->flag(JBStream::RosterRequested))
	return false;
    XmlElement* xml = XMPPUtils::createIq(XMPPUtils::IqGet,0,0,s_rosterQueryId);
    xml->addChild(XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::Roster));
    if (stream->sendStanza(xml)) {
	stream->setRosterRequested(true);
	return true;
    }
    return false;
}

// Request the roster on a given stream
// Set stream RosterRequested flag
static bool sendPresence(JBStream* stream, bool ok, XmlElement* xml)
{
    if (!(stream && xml))
	return false;
    if (stream->sendStanza(xml)) {
	stream->setAvailableResource(ok);
	return true;
    }
    return false;
}

// Process MUC user child. Add list parameters
static void fillMucUser(NamedList& list, XmlElement& xml, XMPPUtils::Presence pres)
{
    list.addParam("muc",String::boolText(true));
    bool kicked = false;
    bool banned = false;
    // Fill user status flags
    String s("status");
    const String& ns = XMPPUtils::s_ns[XMPPNamespace::MucUser];
    String flags;
    for (XmlElement* c = 0; 0 != (c = xml.findNextChild(c,&s,&ns));) {
	String* str = c->getAttribute("code");
	if (TelEngine::null(str))
	    continue;
	int code = str->toInteger();
	if (code < 100 || code > 999)
	    continue;
	if (code == 307)
	    kicked = true;
	if (code == 301)
	    banned = true;
	flags.append(lookup(code,s_mucUserStatus,*str),",");
    }
    list.addParam("muc.userstatus",flags,false);
    // Process the 'item' child
    XmlElement* item = XMPPUtils::findFirstChild(xml,XmlTag::Item,XMPPNamespace::MucUser);
    if (item) {
	list.addParam("muc.affiliation",item->attribute("affiliation"),false);
	list.addParam("muc.role",item->attribute("role"),false);
	list.addParam("muc.nick",item->attribute("nick"),false);
	JabberID jid(item->attribute("jid"));
	if (jid.node()) {
	    list.addParam("muc.contact",jid.bare(),false);
	    list.addParam("muc.contactinstance",jid.resource(),false);
	}
    }
    // Specific type processing
    if (pres != XMPPUtils::Unavailable)
	return;
    String sname;
    // Occupant kicked or banned
    if (item && (kicked || banned)) {
	String pref("muc.");
	pref << lookup(kicked ? 307 : 301,s_mucUserStatus);
	sname = "actor";
	XmlElement* actor = item->findFirstChild(&sname,&ns);
	if (actor) {
	    JabberID jid(actor->attribute("jid"));
	    if (jid) {
		list.addParam(pref + ".by",jid.bare());
		list.addParam(pref + ".byinstance",jid.resource(),false);
	    }
	}
	addChildText(list,*item,XmlTag::Reason,XMPPNamespace::MucUser,pref + ".reason");
    }
    // XEP0045 10.9 room destroyed
    sname = "destroy";
    XmlElement* destroy = xml.findFirstChild(&sname,&ns);
    if (destroy) {
	list.addParam("muc.destroyed",String::boolText(true));
	JabberID jid(destroy->attribute("jid"));
	if (jid)
	    list.addParam("muc.alternateroom",jid.bare());
	addChildText(list,*destroy,XmlTag::Reason,XMPPNamespace::MucUser,"muc.destroyreason");
    }
}

// Build a muc admin set iq element
static XmlElement* buildMucAdmin(const char* room, const char* nick, const char* jid,
    const char* role, const char* aff,
    const char* xmlId, const char* reason = 0)
{
    XmlElement* xml = XMPPUtils::createIq(XMPPUtils::IqSet,0,room,xmlId);
    XmlElement* query = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::MucAdmin);
    xml->addChild(query);
    XmlElement* item = XMPPUtils::createElement(XmlTag::Item);
    query->addChild(item);
    item->setAttributeValid("nick",nick);
    item->setAttributeValid("jid",jid);
    item->setAttributeValid("role",role);
    item->setAttributeValid("affiliation",aff);
    if (!TelEngine::null(reason))
	item->addChild(XMPPUtils::createElement(XmlTag::Reason,reason));
    return xml;
}

// Build a muc owner iq element containing a form
static XmlElement* buildMucOwnerForm(const char* room, bool set, Message& msg, const char* id)
{
    XmlElement* xml = XMPPUtils::createIq(set ? XMPPUtils::IqSet : XMPPUtils::IqGet,0,room,id);
    XmlElement* query = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::MucOwner);
    xml->addChild(query);
    if (set) {
	XmlElement* x = XMPPUtils::createElement(XmlTag::X,XMPPNamespace::XData);
	x->setAttribute("type","submit");
	query->addChild(x);
	// TODO: Check if we can build a form from the message
    }
    return xml;
}

// Build a muc.room message
static Message* buildMucRoom(JBEvent& ev, const char* oper, const JabberID& contact)
{
    Message* m = __plugin.message("muc.room",ev.clientStream());
    m->addParam("operation",oper);
    m->addParam("room",ev.from().bare());
    m->addParam("contact",contact.bare(),false);
    m->addParam("contact_instance",contact.resource(),false);
    return m;
}


/*
 * YJBEntityCapsList
 */
// Load the entity caps file
void YJBEntityCapsList::load()
{
    if (!m_enable)
	return;
    String file;
    getEntityCapsFile(file);
    loadXmlDoc(file,s_jabber);
}

// Set caps file
void YJBEntityCapsList::setFile(const char* file)
{
    Lock mylock(this);
    String old = m_file;
    m_file = file;
    if (!m_file) {
	m_file = Engine::configPath(Engine::clientMode());
	if (!m_file.endsWith(Engine::pathSeparator()))
	    m_file << Engine::pathSeparator();
	m_file << "jabberentitycaps.xml";
    }
    Engine::self()->runParams().replaceParams(m_file);
    bool changed = m_enable && old && m_file && old != m_file;
    mylock.drop();
    if (changed)
	save();
}

// Notify changes and save the entity caps file
void YJBEntityCapsList::capsAdded(JBEntityCaps* caps)
{
    if (caps)
	save();
}

// Save the file
void YJBEntityCapsList::save()
{
    String file;
    getEntityCapsFile(file);
    saveXmlDoc(file,s_jabber);
}


/*
 * YJBEngine
 */
YJBEngine::YJBEngine()
{
    m_receive = new YStreamSetReceive(this,0,"recv");
    m_process = new YStreamSetProcess(this,0,"process");
    // Features
    m_features.add(XMPPNamespace::DiscoInfo);
    m_features.add(XMPPNamespace::DiscoItems);
    m_features.add(XMPPNamespace::Jingle);
    m_features.add(XMPPNamespace::JingleError);
    m_features.add(XMPPNamespace::JingleAppsRtp);
    m_features.add(XMPPNamespace::JingleAppsRtpAudio);
    m_features.add(XMPPNamespace::JingleAppsRtpInfo);
    m_features.add(XMPPNamespace::JingleAppsRtpError);
    m_features.add(XMPPNamespace::JingleTransportIceUdp);
    m_features.add(XMPPNamespace::JingleTransportRawUdp);
    m_features.add(XMPPNamespace::JingleTransfer);
    m_features.add(XMPPNamespace::JingleDtmf);
    m_features.add(XMPPNamespace::JingleAppsFileTransfer);
    m_features.add(XMPPNamespace::JingleSession);
    m_features.add(XMPPNamespace::JingleAudio);
    m_features.add(XMPPNamespace::JingleTransport);
    m_features.add(XMPPNamespace::DtmfOld);
    m_features.add(XMPPNamespace::Roster);
    m_features.add(XMPPNamespace::IqPrivate);
    m_features.add(XMPPNamespace::VCard);
    m_features.add(XMPPNamespace::IqVersion);
    m_features.add(XMPPNamespace::EntityCaps);
    m_features.m_identities.append(new JIDIdentity("client","im"));
    m_features.updateEntityCaps();
}

YJBEngine::~YJBEngine()
{
}

// (Re)initialize engine
void YJBEngine::initialize(const NamedList* params, bool first)
{
    NamedList dummy("");
    if (!params)
	params = &dummy;

    lock();
    // Program name and version to be advertised on request
    if (!m_progName) {
	m_progName = "Yate";
	m_progVersion.clear();
	m_progVersion << Engine::runParams().getValue("version") << "" <<
	    Engine::runParams().getValue("release");
	// TODO: set program name and version for server identities
    }
    unlock();
    JBEngine::initialize(*params);
}

// Process events
void YJBEngine::processEvent(JBEvent* ev)
{
    if (!(ev && ev->stream())) {
	if (ev && !ev->stream())
	    Debug(this,DebugStub,"Event (%p,'%s') without stream",ev,ev->name());
	TelEngine::destruct(ev);
	return;
    }
    Debug(this,DebugInfo,"Processing event (%p,%s)",ev,ev->name());
    switch (ev->type()) {
	case JBEvent::Message:
	    if (ev->element()) {
		if (processMucMessage(ev))
		    break;
		Message* m = __plugin.message("msg.execute",ev->clientStream());
		const char* tStr = ev->stanzaType();
		m->addParam("type",tStr ? tStr : XMPPUtils::msgText(XMPPUtils::Normal));
		m->addParam("id",ev->id(),false);
		m->addParam("caller",ev->from().bare());
		m->addParam("caller_instance",ev->from().resource(),false);
		XmlElement* xml = ev->releaseXml();
		m->addParam("subject",XMPPUtils::subject(*xml),false);
		m->addParam("body",XMPPUtils::body(*xml),false);
		XmlElement* state = xml->findFirstChild(0,
		    &XMPPUtils::s_ns[XMPPNamespace::ChatStates]);
		if (state)
		    m->addParam("chatstate",state->unprefixedTag());
		String tmp("delay");
		XmlElement* delay = xml->findFirstChild(&tmp,&XMPPUtils::s_ns[XMPPNamespace::Delay]);
		if (!delay) {
		    // Handle old jabber:x:delay
		    tmp = "x";
		    String ns = "jabber:x:delay";
		    delay = xml->findFirstChild(&tmp,&ns);
		}
		if (delay) {
		    unsigned int sec = (unsigned int)-1;
		    String* time = delay->getAttribute("stamp");
		    if (!TelEngine::null(time)) {
			if (tmp == "delay")
			    sec = XMPPUtils::decodeDateTimeSec(*time);
			else
			    sec = XMPPUtils::decodeDateTimeSecXDelay(*time);
		    }
		    if (sec != (unsigned int)-1) {
			m->addParam("delay_time",String(sec));
			m->addParam("delay_by",delay->attribute("from"),false);
			m->addParam("delay_text",delay->getText(),false);
		    }
		}
		m->addParam(new NamedPointer("xml",xml));
		Engine::enqueue(m);
	    }
	    break;
	case JBEvent::Presence:
	    if (ev->element())
		processPresenceStanza(ev);
	    break;
	case JBEvent::Iq:
	    if (ev->element())
		processIqStanza(ev);
	    break;
	case JBEvent::Running:
	case JBEvent::Destroy:
	case JBEvent::Terminated:
	    processStreamEvent(ev,ev->type() == JBEvent::Running);
	    break;
	case JBEvent::RegisterOk:
	case JBEvent::RegisterFailed:
	    if (ev->element())
		processRegisterEvent(ev,ev->type() == JBEvent::RegisterOk);
	    break;
	case JBEvent::Start:
	    if (ev->stream()->outgoing()) {
		if (!checkDupId(ev->stream()))
		    ev->stream()->start();
		else
		    ev->stream()->terminate(-1,true,0,XMPPError::InvalidId,"Duplicate stream id");
		break;
	    }
	default:
	    returnEvent(ev,XMPPError::ServiceUnavailable);
	    return;
    }
    TelEngine::destruct(ev);
}

// Start stream TLS
void YJBEngine::encryptStream(JBStream* stream)
{
    if (!stream)
	return;
    DDebug(this,DebugAll,"encryptStream(%p,'%s')",stream,stream->toString().c_str());
    Message msg("socket.ssl");
    msg.userData(stream);
    msg.addParam("server",String::boolText(stream->incoming()));
    if (stream->incoming())
	msg.addParam("domain",stream->local().domain());
    if (!Engine::dispatch(msg))
	stream->terminate(0,stream->incoming(),0,XMPPError::Internal,"SSL start failure");
}

// Connect an outgoing stream
void YJBEngine::connectStream(JBStream* stream)
{
    if (Engine::exiting() || exiting())
	return;
    if (stream && stream->outgoing())
	(new YJBConnectThread(*stream))->startup();
}

// Start stream compression
void YJBEngine::compressStream(JBStream* stream, const String& formats)
{
    if (!stream)
	return;
    DDebug(this,DebugAll,"compressStream(%p,'%s') formats=%s",
	stream,stream->toString().c_str(),formats.c_str());
    Message msg("engine.compress");
    msg.userData(stream);
    msg.addParam("formats",formats,false);
    msg.addParam("name",stream->toString());
    msg.addParam("data_type","text");
    Engine::dispatch(msg);
}

// Process 'user.roster' messages
bool YJBEngine::handleUserRoster(Message& msg, const String& line)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    bool upd = (*oper == "update");
    if (!upd && *oper != "delete") {
	if (*oper == "query") {
	    JBClientStream* s = findAccount(line);
	    if (!s)
		return false;
	    bool ok = requestRoster(s);
	    TelEngine::destruct(s);
	    return ok;
	}
	DDebug(this,DebugStub,"handleUserRoster() oper=%s not implemented!",oper->c_str());
	return false;
    }
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    JabberID contact(msg.getValue("contact"));
    DDebug(this,DebugAll,"handleUserRoster() line=%s oper=%s contact=%s",
	line.c_str(),oper->c_str(),contact.c_str());

    s->lock();
    bool same = TelEngine::null(contact) || contact.bare() == s->local().bare();
    s->unlock();
    if (same) {
	TelEngine::destruct(s);
	return false;
    }

    String id;
    if (upd)
	StreamData::addRequest(s,StreamData::UserRosterUpdate,msg,id);
    else
	StreamData::addRequest(s,StreamData::UserRosterRemove,msg,id);
    XmlElement* query = XMPPUtils::createIq(XMPPUtils::IqSet,0,0,id);
    XmlElement* x = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::Roster);
    query->addChild(x);
    XmlElement* item = new XmlElement("item");
    item->setAttribute("jid",contact.bare());
    x->addChild(item);
    if (upd) {
	item->setAttributeValid("name",msg.getValue("name"));
	String* grp = msg.getParam("groups");
	if (grp) {
	    ObjList* list = grp->split(',',false);
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext())
		item->addChild(XMPPUtils::createElement(XmlTag::Group,o->get()->toString()));
	    TelEngine::destruct(list);
	}
	else {
	    unsigned int n = msg.length();
	    for (unsigned int i = 0; i < n; i++) {
		NamedString* ns = msg.getParam(i);
		if (ns && ns->name() == "group" && *ns)
		    item->addChild(XMPPUtils::createElement(XmlTag::Group,*ns));
	    }
	}
	// Arbitrary children
	String* tmp = msg.getParam("extra");
	if (tmp) {
	    ObjList* list = tmp->split(',',false);
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
		NamedString* ns = msg.getParam(o->get()->toString());
		if (ns)
		    item->addChild(XMPPUtils::createElement(ns->name(),*ns));
	    }
	    TelEngine::destruct(list);
	}
    }
    else
	item->setAttribute("subscription","remove");
    bool ok = s->sendStanza(query);
    if (!ok && id)
	StreamData::removeRequest(s,id);
    TelEngine::destruct(s);
    return ok;
}

// Process 'user.update' messages
bool YJBEngine::handleUserUpdate(Message& msg, const String& line)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    bool ok = false;
    if (*oper == "update") {
	Debug(this,DebugStub,"YJBEngine::handleUserUpdate(update) not implemented!");
    }
    else if (*oper == "delete") {
	Debug(this,DebugStub,"YJBEngine::handleUserUpdate(delete) not implemented!");
    }
    else if (*oper == "query")
	ok = requestRoster(s);
    TelEngine::destruct(s);
    return ok;
}

// Process 'user.data' messages
bool YJBEngine::handleUserData(Message& msg, const String& line)
{
    const String& oper = msg["operation"];
    if (!oper)
	return false;
    bool upd = (oper == "update");
    if (!upd && oper != "query")
	return false;
    const String& data = msg["data"];
    if (!data)
	return false;
    if (!XmlSaxParser::validTag(data)) {
	Debug(this,DebugNote,"%s with invalid tag data=%s",msg.c_str(),data.c_str());
	return false;
    }
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    XmlElement* xmlPriv = new XmlElement(data);
    xmlPriv->setXmlns(String::empty(),true,s_yateClientNs);
    if (upd) {
	NamedIterator iter(msg);
	const NamedString* ns = 0;
	int n = msg.getIntValue("data.count");
	for (int i = 1; i <= n; i++) {
	    String prefix;
	    prefix << "data." << i;
	    XmlElement* r = XMPPUtils::createElement(XmlTag::Item);
	    xmlPriv->addChild(r);
	    r->setAttributeValid("id",msg[prefix]);
	    prefix << ".";
	    for (iter.reset(); 0 != (ns = iter.get());) {
		if (!ns->name().startsWith(prefix))
		    continue;
		XmlElement* p = XMPPUtils::createElement(XmlTag::Parameter);
		p->setAttribute("name",ns->name().substr(prefix.length()));
		p->setAttribute("value",*ns);
		r->addChild(p);
	    }
	}
    }
    String id;
    if (upd)
	StreamData::addRequest(s,StreamData::UserDataSet,msg,id);
    else
	StreamData::addRequest(s,StreamData::UserDataGet,msg,id);
    XmlElement* xml = XMPPUtils::createIq(upd ? XMPPUtils::IqSet : XMPPUtils::IqGet,0,0,id);
    XmlElement* ch = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::IqPrivate);
    xml->addChild(ch);
    ch->addChild(xmlPriv);
    bool ok = s->sendStanza(xml);
    if (!ok && id)
	StreamData::removeRequest(s,id);
    TelEngine::destruct(s);
    return ok;
}

// Process 'contact.info' messages
bool YJBEngine::handleContactInfo(Message& msg, const String& line)
{
    const String& oper = msg["operation"];
    if (!oper)
	return false;
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    bool ok = false;
    const char* contact = msg.getValue("contact");
    const char* id = msg.getValue("id");
    DDebug(this,DebugAll,"handleContactInfo() line=%s oper=%s contact=%s",
	line.c_str(),oper.c_str(),contact);
    String req;
    bool info = (oper == "queryinfo");
    if (info || oper == "queryitems") {
	if (TelEngine::null(id)) {
	    if (info)
		StreamData::addRequest(s,StreamData::DiscoInfo,msg,req);
	    else
		StreamData::addRequest(s,StreamData::DiscoItems,msg,req);
	}
	XmlElement* xml = XMPPUtils::createIqDisco(info,true,0,contact,
	    req ? req.c_str() : id);
	ok = s->sendStanza(xml);
    }
    else if (oper == "query") {
	XmlElement* xml = XMPPUtils::createVCard(true,0,contact,id);
	ok = s->sendStanza(xml);
    }
    else if (oper == "update") {
	XmlElement* xml = XMPPUtils::createVCard(false,0,contact,id);
	XmlElement* vcard = XMPPUtils::findFirstChild(*xml,XmlTag::VCard);
	if (vcard) {
	    String prefix(msg.getValue("message-prefix"));
	    if (prefix)
		prefix = "." + prefix;
	    // Name
	    const char* first = msg.getValue(prefix + "name.first");
	    const char* middle = msg.getValue(prefix + "name.middle");
	    const char* last = msg.getValue(prefix + "name.last");
	    String firstN, lastN;
	    // Try to build elements if missing
	    if (!(first || last || middle)) {
		String* tmp = msg.getParam(prefix + "name");
		if (tmp) {
		    int pos = tmp->rfind(' ');
		    if (pos > 0) {
			firstN = tmp->substr(0,pos);
			lastN = tmp->substr(pos + 1);
		    }
		    else
			lastN = *tmp;
		}
		first = firstN.c_str();
		last = lastN.c_str();
	    }
	    XmlElement* n = new XmlElement("N");
	    n->addChild(XMPPUtils::createElement("GIVEN",first));
	    n->addChild(XMPPUtils::createElement("MIDDLE",middle));
	    n->addChild(XMPPUtils::createElement("FAMILY",last));
	    vcard->addChild(n);
	    // email
	    const char* email = msg.getValue(prefix + "email");
	    if (email)
		vcard->addChild(XMPPUtils::createElement("EMAIL",email));
	    // photo
	    const char* photo = msg.getValue(prefix + "photo");
	    if (!TelEngine::null(photo)) {
		vcard->addChild(XMPPUtils::createElement("TYPE",msg.getValue(prefix + "photo_format")));
		vcard->addChild(XMPPUtils::createElement("BINVAL",photo));
	    }
	}
	ok = s->sendStanza(xml);
    }
    if (!ok && req)
	StreamData::removeRequest(s,req);
    TelEngine::destruct(s);
    return ok;
}

// Process 'jabber.iq' messages
bool YJBEngine::handleJabberIq(Message& msg, const String& line)
{
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    XmlElement* xml = XMPPUtils::getXml(msg);
    bool ok = xml && s->sendStanza(xml);
    TelEngine::destruct(s);
    return ok;
}

// Process 'jabber.account' messages
bool YJBEngine::handleJabberAccount(Message& msg, const String& line)
{
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    // Use a while to break to the end
    while (msg.getBoolValue("query")) {
	msg.setParam("jid",s->local());
	String* contact = msg.getParam("contact");
	if (TelEngine::null(contact))
	    break;
	Lock lock(s);
	StreamData* data = streamData(s);
	NamedList* c = data ? data->contact(*contact) : 0;
	if (!c)
	    break;
	String* inst = msg.getParam("instance");
	if (!TelEngine::null(inst)) {
	    String* res = c->getParam(*inst);
	    if (res)
		s_entityCaps.addCaps(msg,*res);
	    break;
	}
	// Find an audio resource for the contact
	unsigned int n = c->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* res = c->getParam(i);
	    if (TelEngine::null(res))
		continue;
	    Lock lock(s_entityCaps);
	    JBEntityCaps* caps = s_entityCaps.findCaps(*res);
	    if (caps && caps->hasAudio()) {
		msg.setParam("instance",res->name());
		s_entityCaps.addCaps(msg,*caps);
		break;
	    }
	}
	break;
    }
    SocketAddr a;
    s->localAddr(a);
    if (a.host())
	msg.addParam("localip",a.host());
    TelEngine::destruct(s);
    return true;
}

// Process 'resource.subscribe' messages
bool YJBEngine::handleResSubscribe(Message& msg, const String& line)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    bool sub = (*oper == "subscribe");
    if (!sub && *oper != "unsubscribe")
	return false;
    JabberID to(msg.getValue("to"));
    if (!to.node())
	return false;
    DDebug(this,DebugAll,"handleResSubscribe() line=%s oper=%s to=%s",
	line.c_str(),oper->c_str(),to.c_str());
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    XmlElement* p = XMPPUtils::createPresence(0,to.bare(),
	sub ? XMPPUtils::Subscribe : XMPPUtils::Unsubscribe);
    bool ok = s->sendStanza(p);
    TelEngine::destruct(s);
    return ok;
}

// Process 'resource.notify' messages
bool YJBEngine::handleResNotify(Message& msg, const String& line)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    DDebug(this,DebugAll,"handleResNotify() line=%s oper=%s",line.c_str(),oper->c_str());
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    // Use a while to break to the end
    bool ok = false;
    XmlElement* xml = 0;
    JabberID to(msg.getValue("to"));
    while (true) {
	if (*oper == "online") {
	    s->lock();
	    StreamData* sdata = streamData(s);
	    if (sdata)
		sdata->setPresence(msg.getValue("priority",s_priority),
		   msg.getValue("show"),msg.getValue("status"));
	    xml = StreamData::buildPresence(sdata);
	    s->unlock();
	    // Directed presence
	    if (xml && to.node())
		xml->setAttribute("to",to);
	    break;
	}
	bool sub = (*oper == "subscribed");
	if (sub || *oper == "unsubscribed") {
	    if (to.node())
		xml = XMPPUtils::createPresence(0,to.bare(),
		    sub ? XMPPUtils::Subscribed : XMPPUtils::Unsubscribed);
	    break;
	}
	Debug(this,DebugStub,"handleResNotify() oper=%s not implemented!",
	    oper->c_str());
	break;
    }
    if (xml)
	ok = s->sendStanza(xml);
    TelEngine::destruct(s);
    return ok;
}

// Process 'msg.execute' messages
bool YJBEngine::handleMsgExecute(Message& msg, const String& line)
{
    DDebug(this,DebugAll,"handleMsgExecute() line=%s",line.c_str());
    JBClientStream* s = findAccount(line);
    if (!s)
	return false;
    XmlElement* xml = XMPPUtils::getChatXml(msg);
    bool ok = false;
    if (xml) {
	String* to = xml->getAttribute("to");
	if (!to) {
	    to = msg.getParam("called");
	    if (to) {
		JabberID c(*to);
		if (!c.resource())
		    c.resource(msg.getValue("called_instance"));
		xml->setAttribute("to",c);
	    }
	}
	if (to)
	    ok = s->sendStanza(xml);
	else
	    TelEngine::destruct(xml);
    }
    TelEngine::destruct(s);
    return ok;
}

// Process 'user.login' messages
bool YJBEngine::handleUserLogin(Message& msg, const String& line)
{
    String* proto = msg.getParam("protocol");
    if (proto && !__plugin.canHandleProtocol(*proto))
	return false;

    // Check operation
    NamedString* oper = msg.getParam("operation");
    bool login = !oper || *oper == "login" || *oper == "create";
    if (!login && (!oper || (*oper != "logout" && *oper != "delete")))
	return false;

    Debug(&__plugin,DebugAll,"handleUserLogin(%s) account=%s",
	String::boolText(login),line.c_str());

    JBClientStream* stream = s_jabber->findAccount(line);
    bool ok = false;
    if (login) {
	if (!stream) {
	    stream = s_jabber->create(line,msg,
		String(::lookup(JBStream::c2s,JBStream::s_typeName)) + "/" + line);
	    if (stream) {
		// Build user data and set it
		Lock lock(stream);
		StreamData* d = new StreamData(*stream,
		    msg.getBoolValue("request_roster",true));
		if (msg.getBoolValue("send_presence",true))
		    d->setPresence(msg.getValue("priority",s_priority),
			 msg.getValue("show"),msg.getValue("status"));
		stream->userData(d);
	    }
	}
	else
	    msg.setParam("error","User already logged in");
	ok = (0 != stream) && stream->state() != JBStream::Destroy;
    }
    else if (stream) {
	if (stream->state() == JBStream::Running) {
	    XmlElement* xml = XMPPUtils::createPresence(0,0,XMPPUtils::Unavailable);
	    stream->sendStanza(xml);
	}
	const char* reason = msg.getValue("reason");
	if (!reason)
	    reason = Engine::exiting() ? "" : "Logout";
	XMPPError::Type err = Engine::exiting() ? XMPPError::Shutdown : XMPPError::NoError;
	stream->terminate(-1,true,0,err,reason);
	ok = true;
    }
    TelEngine::destruct(stream);
    return ok;
}

// Utility: add an integer muc history limit attribute
static void addHistory(XmlElement*& h, const char* attr, NamedList& list,
    const char* param, bool time = false)
{
    unsigned int tmp = (unsigned int)list.getIntValue(param,-1);
    if (tmp == (unsigned int)-1 || (time && !tmp))
	return;
    String s;
    if (!time)
	s = tmp;
    else {
	XMPPUtils::encodeDateTimeSec(s,tmp);
	if (!s)
	    return;
    }
    if (!h)
	h = new XmlElement("history");
    h->setAttribute(attr,s);
}

// Process 'muc.room' messages
bool YJBEngine::handleMucRoom(Message& msg, const String& line)
{
    const String& oper = msg["operation"];
    if (!oper)
	return false;
    JBClientStream* s = s_jabber->findAccount(line);
    if (!s)
	return false;
    JabberID room(msg.getValue("room"));
    Debug(&__plugin,DebugAll,"handleMucRoom() account=%s oper=%s room=%s",
	line.c_str(),oper.c_str(),room.c_str());
    bool ok = false;
    const String& id = msg["id"];
    bool login = (oper == "login" || oper == "create");
    if (login || oper == "logout" || oper == "delete") {
	if (room.node() && !room.resource())
	    room.resource(msg["nick"]);
	if (!room.isFull()) {
	    TelEngine::destruct(s);
	    return false;
	}
	XmlElement* xml = XMPPUtils::getPresenceXml(msg);
	xml->setAttribute("to",room);
	xml->setAttributeValid("id",id);
	XmlElement* m = XMPPUtils::createElement(XmlTag::X,XMPPNamespace::Muc);
	xml->addChild(m);
	if (login) {
	    // Password
	    const String& pwd = msg["password"];
	    if (pwd)
		m->addChild(XMPPUtils::createElement(XmlTag::Password,pwd));
	    // Chat history limits
	    XmlElement* h = 0;
	    if (msg.getBoolValue("history",true)) {
		addHistory(h,"maxchars",msg,"history.maxchars");
		addHistory(h,"maxstanzas",msg,"history.maxmsg");
		addHistory(h,"seconds",msg,"history.newer");
		addHistory(h,"since",msg,"history.after",true);
	    }
	    else {
		h = new XmlElement("history");
		h->setAttribute("maxchars","0");
	    }
	    if (h)
		m->addChild(h);
	}
	// Make sure we have the correct type
	if (login)
	    xml->removeAttribute("type");
	else
	    xml->setAttribute("type",XMPPUtils::presenceText(XMPPUtils::Unavailable));
	ok = s->sendStanza(xml);
    }
    else if (oper == "setsubject") {
	String* subject = room ? msg.getParam("subject") : 0;
	if (subject) {
	    XmlElement* xml = XMPPUtils::createMessage(XMPPUtils::GroupChat,0,room.bare(),0,0);
	    xml->addChild(XMPPUtils::createElement(XmlTag::Subject,*subject));
	    ok = s->sendStanza(xml);
	}
    }
    else if (oper == "setnick") {
	room.resource(msg["nick"]);
	if (room.isFull()) {
	    XmlElement* xml = XMPPUtils::getPresenceXml(msg);
	    xml->setAttribute("to",room);
	    xml->removeAttribute("type");
	    xml->addChild(XMPPUtils::createElement(XmlTag::X,XMPPNamespace::Muc));
	    ok = s->sendStanza(xml);
	}
    }
    else if (oper == "querymembers") {
	XmlElement* xml = XMPPUtils::createIqDisco(false,true,0,room,id);
	ok = s->sendStanza(xml);
    }
    else if (oper == "kick") {
	const String& nick = msg["nick"];
	if (nick) {
	    XmlElement* xml = buildMucAdmin(room,nick,0,"none",0,id,
		msg.getValue("reason"));
	    ok = s->sendStanza(xml);
	}
    }
    else if (oper == "ban") {
	const String& contact = msg["contact"];
	if (contact) {
	    XmlElement* xml = buildMucAdmin(room,0,contact,0,"outcast",id,
		msg.getValue("reason"));
	    ok = s->sendStanza(xml);
	}
    }
    else if (oper == "setconfig") {
	XmlElement* xml = buildMucOwnerForm(room,true,msg,id);
	ok = s->sendStanza(xml);
    }
    else if (oper == "decline" || oper == "invite") {
	XmlElement* xml = XMPPUtils::createMessage(XMPPUtils::Normal,0,room.bare(),0,0);
	XmlElement* x = XMPPUtils::createElement(XmlTag::X,XMPPNamespace::MucUser);
	xml->addChild(x);
	XmlElement* element = new XmlElement(oper);
	x->addChild(element);
	JabberID contact(msg.getValue("contact"));
	contact.resource(msg.getValue("contact_instance"));
	element->setAttributeValid("to",contact);
	const String& reason = msg["reason"];
	if (reason)
	    element->addChild(XMPPUtils::createElement(XmlTag::Reason,reason));
	ok = s->sendStanza(xml);
    }
    TelEngine::destruct(s);
    return ok;
}

// Process 'engine.start' messages
void YJBEngine::handleEngineStart(Message& msg)
{
    // Check client TLS
    Message m("socket.ssl");
    m.addParam("test",String::boolText(true));
    m.addParam("server",String::boolText(false));
    m_hasClientTls = Engine::dispatch(m);
    if (!m_hasClientTls)
	Debug(this,DebugNote,"TLS not available for outgoing streams");
}

// Handle muc 'message' stanzas (not related to chat)
// The given event is always valid and carry a valid stream and xml element
// Return true if the event was handled
bool YJBEngine::processMucMessage(JBEvent* ev)
{
    // We handle only 'normal'
    XMPPUtils::MsgType t = XMPPUtils::msgType(ev->stanzaType());
    if (t != XMPPUtils::Normal)
	return false;
    // Handle 'x' elements in MUC user namespace
    XmlElement* c = XMPPUtils::findFirstChild(*ev->element(),XmlTag::X,XMPPNamespace::MucUser);
    if (!c)
	return false;
    DDebug(this,DebugAll,"Processing MUC message type=%s from=%s",
	ev->stanzaType().c_str(),ev->from().c_str());
    const String& ns = XMPPUtils::s_ns[XMPPNamespace::MucUser];
    // XEP 0045 7.5 invite user into conference
    String tmp("invite");
    XmlElement* invite = c->findFirstChild(&tmp,&ns);
    if (invite) {
	JabberID from(invite->getAttribute("from"));
	Message* m = buildMucRoom(*ev,"invite",from);
	addChildText(*m,*invite,XmlTag::Reason,XMPPNamespace::MucUser);
	addChildText(*m,*c,XmlTag::Password,XMPPNamespace::MucUser);
	Engine::enqueue(m);
	return true;
    }
    // XEP 0045 7.5 invitation declined
    tmp = "decline";
    XmlElement* decline = c->findFirstChild(&tmp,&ns);
    if (decline) {
	JabberID from(decline->getAttribute("from"));
	Message* m = buildMucRoom(*ev,"decline",from);
	addChildText(*m,*decline,XmlTag::Reason,XMPPNamespace::MucUser);
	Engine::enqueue(m);
	return true;
    }
    // TODO: handle XEP0249 direct muc invitation
    return false;
}

// Handle 'presence' stanzas
// The given event is always valid and carry a valid stream and xml element
void YJBEngine::processPresenceStanza(JBEvent* ev)
{
    DDebug(this,DebugAll,"Processing presence type=%s from=%s",
	ev->stanzaType().c_str(),ev->from().c_str());
    if (!ev->from())
	return;
    XMPPUtils::Presence pres = XMPPUtils::presenceType(ev->stanzaType());
    // Handle MUC online/offline/error
    XmlElement* xMucUser = 0;
    XmlElement* xMuc = 0;
    if (pres == XMPPUtils::PresenceNone || pres == XMPPUtils::Unavailable ||
	pres == XMPPUtils::PresenceError) {
	// Handle 'x' elements in MUC user namespace(s)
	xMucUser = XMPPUtils::findFirstChild(*ev->element(),XmlTag::X,XMPPNamespace::MucUser);
	xMuc = XMPPUtils::findFirstChild(*ev->element(),XmlTag::X,XMPPNamespace::Muc);
    }
    bool online = pres == XMPPUtils::PresenceNone;
    if (online || pres == XMPPUtils::Unavailable) {
	String capsId;
	if (online && ev->from().resource())
	    s_entityCaps.processCaps(capsId,ev->element(),ev->stream(),0,ev->from());
	// Update contact list resources
	if (!xMucUser) {
	    Lock lock(ev->stream());
	    StreamData* sdata = streamData(ev);
	    if (sdata) {
		if (online)
		    sdata->setResource(ev->from().bare(),ev->from().resource(),capsId);
		else
		    sdata->removeResource(ev->from().bare(),ev->from().resource());
	    }
	}
	// Notify
	Message* m = __plugin.message("resource.notify",ev->clientStream());
	m->addParam("operation",online ? "online" : "offline");
	m->addParam("contact",ev->from().bare());
	if (ev->from().resource())
	    m->addParam("instance",ev->from().resource());
	if (online) {
	    m->addParam("uri",ev->from());
	    unsigned int n = 0;
	    XmlElement* ch = 0;
	    while (0 != (ch = ev->element()->findNextChild(ch))) {
		int tag = XmlTag::Count; 
		int ns = XMPPNamespace::Count; 
		XMPPUtils::getTag(*ch,tag,ns);
		// Known children in stream's namespace
		if (ns == ev->stream()->xmlns() &&
		    (tag == XmlTag::Priority || ch->unprefixedTag() == "show" ||
		    ch->unprefixedTag() == "status")) {
		    m->addParam(ch->unprefixedTag(),ch->getText());
		    continue;
		}
		// Add extra parameters
		if (!n)
		    m->addParam("message-prefix",ev->element()->tag());
		n++;
		String pref;
		pref << ev->element()->tag() << "." << n;
		m->addParam(pref,ch->tag());
		ch->copyAttributes(*m,pref + ".");
	    }
	    if (capsId)
		s_entityCaps.addCaps(*m,capsId);
	}
	if (xMucUser)
	    fillMucUser(*m,*xMucUser,pres);
	Engine::enqueue(m);
	return;
    }
    bool subReq = (pres == XMPPUtils::Subscribe);
    if (subReq || pres == XMPPUtils::Unsubscribe) {
	Message* m = __plugin.message("resource.subscribe",ev->clientStream());
	m->addParam("operation",ev->stanzaType());
	m->addParam("subscriber",ev->from().bare());
	Engine::enqueue(m);
	return;
    }
    if (pres == XMPPUtils::PresenceError) {
	Message* m = __plugin.message("resource.notify",ev->clientStream());
	m->addParam("operation","error");
	m->addParam("contact",ev->from().bare());
	if (ev->from().resource())
	    m->addParam("instance",ev->from().resource());
	getXmlError(*m,ev->element());
	if (xMucUser)
	    fillMucUser(*m,*xMucUser,pres);
	else if (xMuc)
	    m->addParam("muc",String::boolText(true));
	Engine::enqueue(m);
    }
    // Ignore XMPPUtils::Subscribed, XMPPUtils::Unsubscribed, XMPPUtils::Probe,
}

// Handle 'iq' stanzas
// The given event is always valid and carry a valid stream and xml element
void YJBEngine::processIqStanza(JBEvent* ev)
{
    XmlElement* service = ev->child();
    XMPPUtils::IqType type = XMPPUtils::iqType(ev->stanzaType());
    bool rsp = type == XMPPUtils::IqResult || type == XMPPUtils::IqError;
    // Don't accept requests without child
    if (!(rsp || service)) {
	ev->sendStanzaError(XMPPError::ServiceUnavailable);
	return;
    }
    bool ok = rsp && type == XMPPUtils::IqResult;
    int t = XmlTag::Count;
    int n = XMPPNamespace::Count;
    if (service)
	XMPPUtils::getTag(*service,t,n);
    if (rsp) {
	// Server entity caps responses
	if (n == XMPPNamespace::DiscoInfo &&
	    s_entityCaps.processRsp(ev->element(),ev->id(),ok))
	    return;
	// Responses to disco info/items requests
	if (rsp && processDiscoRsp(ev,service,t,n,ok))
	    return;
    }

    bool fromServer = !ev->from();
    if (!fromServer) {
	Lock lock(ev->stream());
	fromServer = ev->stream()->local().match(ev->from()) ||
	    ev->from() == ev->stream()->local().domain();
    }
    if (fromServer) {
	switch (n) {
	    case XMPPNamespace::Roster:
		processRoster(ev,service,t,type);
		return;
	}
	// Check responses without child
	if (rsp) {
	    if (ev->id() == s_rosterQueryId) {
		processRoster(ev,service,t,type);
		return;
	    }
	}
    }
    // Disco info requests
    if (n == XMPPNamespace::DiscoInfo && type == XMPPUtils::IqGet) {
	bool rsp = fromServer;
	if (!rsp) {
	    // Respond to users subscribed to our presence
	    Lock lock(ev->stream());
	    StreamData* sdata = streamData(ev);
	    if (sdata) {
		NamedList* c = sdata->contact(ev->from().bare());
		if (c) {
		    const String& sub = (*c)["subscription"];
		    rsp = (sub == "both" || sub == "from");
		}
	    }
	}
	if (rsp) {
	    XmlElement* xml = 0;
	    String* node = service->getAttribute("node");
	    const char* from = !fromServer ? ev->from().c_str() : 0;
	    if (TelEngine::null(node))
		xml = m_features.buildDiscoInfo(0,from,ev->id());
	    else if (*node == s_capsNode)
		xml = m_features.buildDiscoInfo(0,from,ev->id(),s_capsNode);
	    else {
		// Disco info to our node#hash
		int pos = node->find("#");
		if (pos > 0 && node->substr(0,pos) == s_capsNode &&
		    node->substr(pos + 1) == m_features.m_entityCapsHash)
		    xml = m_features.buildDiscoInfo(0,from,ev->id(),*node);
	    }
	    if (xml) {
		ev->stream()->sendStanza(xml);
		return;
	    }
	}
    }
    // Vcard responses
    if (rsp && t == XmlTag::VCard && n == XMPPNamespace::VCard) {
	Message* m = __plugin.message("contact.info",ev->clientStream());
	m->addParam("operation","notify");
	if (!fromServer)
	    m->addParam("contact",ev->from().bare());
	String prefix("contact.");
	// Name
	String ch("N");
	XmlElement* tmp = service->findFirstChild(&ch);
        if (tmp) {
	    String name;
	    const String& given = getChildText(*tmp,"GIVEN");
	    if (given) {
		m->addParam(prefix + "name.first",given);
		name << given;
	    }
	    const String& middle = getChildText(*tmp,"MIDDLE");
	    if (middle) {
		m->addParam(prefix + "name.middle",middle);
		name.append(middle," ");
	    }
	    const String& family = getChildText(*tmp,"FAMILY");
	    if (family) {
		m->addParam(prefix + "name.last",family);
		name.append(family," ");
	    }
	    if (name)
		m->addParam(prefix + "name",name);
	}
        // EMAIL
	m->addParam(prefix + "email",getChildText(*service,"EMAIL"),false);
	// Photo
	ch = "PHOTO";
	tmp = service->findFirstChild(&ch);
	if (tmp) {
	    const String& t = getChildText(*tmp,"TYPE");
	    const String& img = getChildText(*tmp,"BINVAL");
	    if (t && img) {
		m->addParam(prefix + "photo_format",t);
		m->addParam(prefix + "photo",img);
	    }
	}
	Engine::enqueue(m);
	return;
    }
    // Check pending requests
    if (rsp && StreamData::processResponse(ev->clientStream(),ev,ok))
	return;
    // Route the iq
    Message m("jabber.iq");
    __plugin.complete(m,ev->clientStream());
    m.addParam("from",ev->from().bare(),false);
    m.addParam("from_instance",ev->from().resource(),false);
    if (ev->to()) {
	m.addParam("to",ev->to().bare());
	m.addParam("to_instance",ev->to().resource());
    }
    else {
	Lock lock(ev->stream());
	m.addParam("to",ev->stream()->local().bare());
	m.addParam("to_instance",ev->stream()->local().resource());
    }
    m.addParam("id",ev->id(),false);
    m.addParam("type",ev->stanzaType(),false);
    if (n != XMPPNamespace::Count)
	m.addParam("xmlns",XMPPUtils::s_ns[n]);
    m.addParam(new NamedPointer("xml",ev->releaseXml()));
    XmlElement* xmlRsp = 0;
    if (Engine::dispatch(m)) {
	if (!rsp) {
	    xmlRsp = XMPPUtils::getXml(m,"response",0);
	    if (!xmlRsp && m.getBoolValue("respond"))
		xmlRsp = ev->buildIqResult(true);
	}
    }
    else if (!rsp) {
	xmlRsp = XMPPUtils::createIq(XMPPUtils::IqError,ev->to(),ev->from(),ev->id());
	xmlRsp->addChild(XMPPUtils::createError(XMPPError::TypeCancel,XMPPError::ServiceUnavailable));
    }
    if (xmlRsp)
	ev->stream()->sendStanza(xmlRsp);
}

// Process stream Running, Destroy, Terminated events
// The given event is always valid and carry a valid stream
void YJBEngine::processStreamEvent(JBEvent* ev, bool ok)
{
    if (ok) {
	// Connected:
	//  request the roster, send presence
	// TODO: request vcard, private data
	bool reqRoster = true;
	XmlElement* pres = 0;
	ev->stream()->lock();
	StreamData* sdata = streamData(ev);
	if (sdata) {
	    reqRoster = sdata->m_requestRoster;
	    if (sdata->m_presence)
		pres = StreamData::buildPresence(sdata);
	}
	else
	    pres = StreamData::buildPresence();
	ev->stream()->unlock();
	if (reqRoster)
	    requestRoster(ev->stream());
	if (pres)
	    sendPresence(ev->stream(),true,pres);
    }
    else {
	// Reset stream data
	ev->stream()->lock();
	ev->stream()->setRosterRequested(false);
	StreamData* sdata = streamData(ev);
	if (sdata) {
	    sdata->m_contacts.clear();
	    sdata->m_requests.clearParams();
	}
	ev->stream()->unlock();
    }
    Message* m = __plugin.message("user.notify",ev->clientStream());
    Lock lock(ev->stream());
    m->addParam("username",ev->stream()->local().node());
    m->addParam("server",ev->stream()->local().domain());
    m->addParam("jid",ev->stream()->local());
    m->addParam("registered",String::boolText(ok));
    if (ok)
	m->addParam("instance",ev->stream()->local().resource());
    else if (ev->text())
	m->addParam("reason",ev->text());
    bool restart = (ev->stream()->state() != JBStream::Destroy &&
	!ev->stream()->flag(JBStream::NoAutoRestart));
    m->addParam("autorestart",String::boolText(restart));
    lock.drop();
    Engine::enqueue(m);
}

// Process stream register result events
// The given event has a valid element and stream
void YJBEngine::processRegisterEvent(JBEvent* ev, bool ok)
{
    Debug(this,DebugStub,"processRegisterEvent() not implemented!");

    if (ok) {
	return;
    }

    // Check for instructions
    if (ev->stanzaType() == "result") {
	XmlElement* query = XMPPUtils::findFirstChild(*ev->element(),XmlTag::Query,
	    XMPPNamespace::IqRegister);
	const char* url = 0;
	const char* info = 0;
	if (query) {
	    String x("x");
	    XmlElement* tmp = query->findFirstChild(&x,
		&XMPPUtils::s_ns[XMPPNamespace::XOob]);
	    if (tmp) {
		x = "url";
		tmp = tmp->findFirstChild(&x);
		if (tmp)
		    url = tmp->getText();
		x = "instructions";
		tmp = query->findFirstChild(&x);
		if (tmp)
		    info = tmp->getText();
	    }
	}
	if (url || info) {
	    DDebug(this,DebugAll,"Account '%s' got register info '%s' url='%s'",
		ev->stream()->toString().c_str(),info,url);
	}
    }
}

// Add a roster item to a list
static void addRosterItem(NamedList& list, XmlElement& x, const String& id,
    int index, bool del = false)
{
    String pref("contact.");
    pref << index;
    list.addParam(pref,id);
    if (del)
	return;
    pref << ".";
    list.addParam(pref + "name",x.attribute("name"),false);
    list.addParam(pref + "subscription",x.attribute("subscription"),false);
    NamedString* groups = new NamedString(pref + "groups");
    list.addParam(groups);
    // Groups and other children
    const String* ns = &XMPPUtils::s_ns[XMPPNamespace::Roster];
    for (XmlElement* c = x.findFirstChild(0,ns); c; c = x.findNextChild(c,0,ns)) {
	if (XMPPUtils::isUnprefTag(*c,XmlTag::Group)) {
	    const String& grp = c->getText();
	    groups->append(grp,",");
	    list.addParam(pref + "group",grp,false);
	}
	else
	    list.addParam(pref + c->unprefixedTag(),c->getText());
    }
}

// Process received roster elements
void YJBEngine::processRoster(JBEvent* ev, XmlElement* service, int tag, int iqType)
{
    // Server roster push
    if (iqType == XMPPUtils::IqSet) {
	// Accept 'query' on streams that already requested the roster
	if (!service || tag != XmlTag::Query ||
	    !ev->stream()->flag(JBStream::RosterRequested)) {
	    ev->sendStanzaError(XMPPError::ServiceUnavailable);
	    return;
	}
	XmlElement* x = XMPPUtils::findFirstChild(*service,XmlTag::Item,XMPPNamespace::Roster);
	if (!x)
	    return;
	String* jid = x->getAttribute("jid");
	if (TelEngine::null(jid))
	    return;
	Message* m = __plugin.message("user.roster",ev->clientStream());
	String* sub = x->getAttribute("subscription");
	bool upd = !sub || *sub != "remove";
	ev->stream()->lock();
	StreamData* sdata = streamData(ev);
	if (sdata) {
	    if (*jid != ev->stream()->local().bare()) {
		if (upd) {
		    NamedList* c = sdata->addContact(*jid);
		    c->setParam("subscription",TelEngine::c_safe(sub));
		}
		else
		    sdata->removeContact(*jid);
		Debug(this,DebugAll,"Account(%s) %s roster item '%s'",
		    m->getValue("account"),upd ? "updated" : "deleted",jid->c_str());
	    }
	}
	ev->stream()->unlock();
	m->addParam("operation",upd ? "update" : "delete");
	m->addParam("id",ev->id(),false);
	m->addParam("contact.count","1");
	addRosterItem(*m,*x,*jid,1,!upd);
	Engine::enqueue(m);
	return;
    }
    // Process responses
    if (iqType == XMPPUtils::IqResult) {
	if (!service || tag != XmlTag::Query || ev->id() != s_rosterQueryId) {
	    StreamData::processResponse(ev->clientStream(),ev,false);
	    return;
	}
	// Handle 'query' roster responses
	Message* m = __plugin.message("user.roster",ev->clientStream());
	m->addParam("operation","update");
	m->addParam("queryrsp",String::boolText(true));
	NamedString* count = new NamedString("contact.count");
	m->addParam(count);
	int n = 0;
	XmlElement* x = 0;
	ev->stream()->lock();
	StreamData* sdata = streamData(ev);
	while (0 != (x = XMPPUtils::findNextChild(*service,x,XmlTag::Item,XMPPNamespace::Roster))) {
	    String* jid = x->getAttribute("jid");
	    if (!TelEngine::null(jid)) {
		if (sdata && *jid != ev->stream()->local().bare()) {
		    NamedList* c = sdata->addContact(*jid);
		    c->setParam("subscription",x->attribute("subscription"));
		    Debug(this,DebugAll,"Account(%s) updated roster item '%s'",
			m->getValue("account"),jid->c_str());
		}
		addRosterItem(*m,*x,*jid,++n);
	    }
	}
	ev->stream()->unlock();
	*count = String(n);
	Engine::enqueue(m);
	return;
    }
    if (iqType == XMPPUtils::IqError) {
	if (ev->id() == s_rosterQueryId) {
	    Message* m = __plugin.message("user.roster",ev->clientStream());
	    m->addParam("operation","queryerror");
	    // Reset stream roster requested flag to allow subsequent requests
	    ev->stream()->setRosterRequested(false);
	    getXmlError(*m,ev->element());
	    Engine::enqueue(m);
	}
	else
	    StreamData::processResponse(ev->clientStream(),ev,false);
	return;
    }
    ev->sendStanzaError(XMPPError::ServiceUnavailable);
}

// Process disco info/items responses. Return true if processed
bool YJBEngine::processDiscoRsp(JBEvent* ev, XmlElement* service, int tag, int ns, bool ok)
{
    if (StreamData::processResponse(ev->clientStream(),ev,ok))
	return true;
    if (tag != XmlTag::Query)
	return false;
    bool info = (ns == XMPPNamespace::DiscoInfo);
    if (!info && ns != XMPPNamespace::DiscoItems)
	return false;
    if (!ok) {
	Message* m = __plugin.message("contact.info",ev->clientStream());
	m->addParam("operation","error");
	m->addParam("contact",ev->from(),false);
	m->addParam("id",ev->id(),false);
	getXmlError(*m,ev->element());
	Engine::enqueue(m);
	return true;
    }
    // Disco info responses
    if (info) {
	Message* m = __plugin.message("contact.info",ev->clientStream());
	m->addParam("operation","notifyinfo");
	m->addParam("contact",ev->from(),false);
	m->addParam("id",ev->id(),false);
	fillDiscoInfo(*m,service);
	Engine::enqueue(m);
	return true;
    }
    // Disco items
    Message* m = __plugin.message("contact.info",ev->clientStream());
    m->addParam("operation","notifyitems");
    m->addParam("contact",ev->from(),false);
    m->addParam("id",ev->id(),false);
    if (service) {
	XmlElement* c = 0;
	do {
	    fillDiscoItems(*m,service,c);
	    if (c) {
		Engine::enqueue(m);
		m = __plugin.message("contact.info",ev->clientStream());
		m->addParam("operation","notifyitems");
		m->addParam("contact",ev->from(),false);
		m->addParam("id",ev->id(),false);
	    }
	}
	while (c);
    }
    Engine::enqueue(m);
    return true;
}

// Fill parameters with disco info responses
void YJBEngine::fillDiscoInfo(NamedList& dest, XmlElement* query)
{
    if (!query)
	return;
    JBEntityCaps caps(0,' ',0,0);
    caps.m_features.fromDiscoInfo(*query);
    // Add identities
    ObjList* o = caps.m_features.m_identities.skipNull();
    if (o) {
	NamedString* ns = new NamedString("info.count");
	dest.addParam(ns);
	int n = 0;
	for (; o; o = o->skipNext()) {
	    JIDIdentity* ident = static_cast<JIDIdentity*>(o->get());
	    if (!(ident->m_category || ident->m_type || ident->m_name))
		continue;
	    String prefix("info.");
	    prefix << ++n;
	    dest.addParam(prefix + ".category",ident->m_category,false);
	    dest.addParam(prefix + ".type",ident->m_type,false);
	    dest.addParam(prefix + ".name",ident->m_name,false);
	}
	if (n)
	    *ns = String(n);
	else
	    dest.clearParam(ns);
    }
    // Add features
    JBEntityCapsList list;
    list.addCaps(dest,caps);
}

// Fill parameters with disco items responses
void YJBEngine::fillDiscoItems(NamedList& dest, XmlElement* query, XmlElement*& start)
{
    if (!query) {
	start = 0;
	return;
    }
    NamedString* count = new NamedString("item.count","");
    dest.addParam(count);
    String prefix("item.");
    unsigned int n = 0;
    const String& tag = XMPPUtils::s_tag[XmlTag::Item];
    const String& ns = XMPPUtils::s_ns[XMPPNamespace::DiscoItems];
    while (0 != (start = query->findNextChild(start,&tag,&ns))) {
	JabberID jid(start->attribute("jid"));
	if (!jid)
	    continue;
	String pref(prefix + String(++n));
	dest.addParam(pref,jid);
	const char* name = start->attribute("name");
	if (!TelEngine::null(name))
	    dest.addParam(pref + ".name",name);
	if (n == JABBERCLIENT_MAXITEMS)
	    break;
    }
    if (n)
	*count = String(n);
    else
	dest.clearParam(count);
    if (start)
	dest.setParam("partial",String::boolText(true));
}

// Fill module status params
void YJBEngine::statusParams(String& str)
{
    lock();
    unsigned int c2s = m_receive ? m_receive->streamCount() : 0;
    unlock();
    str << "count=" << c2s;
}

// Fill module status detail
unsigned int YJBEngine::statusDetail(String& str)
{
    XDebug(this,DebugAll,"statusDetail('%s')",str.c_str());
    lock();
    RefPointer<JBStreamSetList> list = m_receive;
    unlock();
    str << "format=Direction|Status|Local|Remote";
    if (!list)
	return 0;
    unsigned int n = 0;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    JBStream* stream = static_cast<JBStream*>(s->get());
	    Lock lock(stream);
	    n++;
	    streamDetail(str,stream);
	}
    }
    list->unlock();
    list = 0;
    return n;
}

// Complete stream details
void YJBEngine::statusDetail(String& str, const String& name)
{
    XDebug(this,DebugAll,"statusDetail(%s)",name.c_str());
    JBStream* stream = findStream(name);
    if (!stream)
	return;
    Lock lock(stream);
    str.append("name=",";");
    str << stream->toString();
    str << ",direction=" << (stream->incoming() ? "incoming" : "outgoing");
    str << ",state=" << stream->stateName();
    str << ",local=" << stream->local();
    str << ",remote=" << stream->remote();
    String buf;
    XMPPUtils::buildFlags(buf,stream->flags(),JBStream::s_flagName);
    str << ",options=" << buf;
}

// Complete stream detail
void YJBEngine::streamDetail(String& str, JBStream* stream)
{
    str << ";" << stream->toString() << "=";
    str << (stream->incoming() ? "incoming" : "outgoing");
    str << "|" << stream->stateName();
    str << "|" << stream->local();
    str << "|" << stream->remote();
}

// Complete stream name starting with partWord
void YJBEngine::completeStreamName(String& str, const String& partWord)
{
    lock();
    RefPointer<JBStreamSetList> list = m_receive;
    unlock();
    if (!list)
	return;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    JBStream* stream = static_cast<JBClientStream*>(s->get());
	    Lock lock(stream);
	    if (!partWord || stream->toString().startsWith(partWord))
		Module::itemComplete(str,stream->toString(),partWord);
	}
    }
    list->unlock();
    list = 0;
}


/*
 * StreamData
 */
// Set presence params
void StreamData::setPresence(const char* prio, const char* show, const char* status)
{
    if (!m_presence)
	m_presence = new NamedList("");
    if (!TelEngine::null(prio))
	m_presence->setParam("priority",prio);
    else
	m_presence->clearParam("priority");
    if (!TelEngine::null(show))
	m_presence->setParam("show",show);
    else
	m_presence->clearParam("show");
    m_presence->setParam("status",status);
}

// Retrieve a contact
ObjList* StreamData::find(const String& name)
{
    for (ObjList* o = m_contacts.skipNull(); o; o = o->skipNext()) {
	NamedList* c = static_cast<NamedList*>(o->get());
	if (*c &= name)
	    return o;
    }
    return 0;
}

// Add a pending request
void StreamData::addRequest(ReqType t, const NamedList& params, String& id)
{
    String type(t);
    NamedList* req = new NamedList(params);
    id.clear();
    id = type;
    switch (t) {
	case UserRosterUpdate:
	case UserRosterRemove:
	case DiscoInfo:
	case DiscoItems:
	    id << "_" << params["contact"].hash();
	    break;
	case UserDataGet:
	case UserDataSet:
	    id << "_" << params["data"].hash();
	    break;
	default: ;
    }
    id << "_";
    // Remove pending requests to the same target
    if (t == DiscoInfo || t == DiscoItems) {
	NamedIterator iter(m_requests);
	while (true) {
	    const NamedString* ns = iter.get();
	    if (!ns)
		break;
	    if (ns->name().startsWith(id,false)) {
		m_requests.clearParam((NamedString*)ns);
		iter.reset();
	    }
	}
    }
    id << ++m_reqIndex;
    req->addParam(s_reqTypeParam,type);
    m_requests.addParam(new NamedPointer(id,req));
    Debug(&__plugin,DebugAll,"StreamData(%s) added request %s type=%s",
	c_str(),id.c_str(),type.c_str());
}

// Remove a pending request
bool StreamData::removeRequest(const String& id)
{
    NamedString* ns = id ? m_requests.getParam(id) : 0;
    if (!ns)
	return false;
    Debug(&__plugin,DebugAll,"StreamData(%s) removing request %s",
	c_str(),id.c_str());
    m_requests.clearParam(ns);
    return true;
}

// Process a received response. Return true if handled
bool StreamData::processResponse(JBEvent* ev, bool ok)
{
    NamedString* ns = ev->id() ? m_requests.getParam(ev->id()) : 0;
    if (!ns)
	return false;
    const char* msg = 0;
    NamedList* req = YOBJECT(NamedList,ns);
    int t = UnknownReq;
    if (req) {
	t = req->getIntValue(s_reqTypeParam);
	switch (t) {
	    case UserRosterUpdate:
	    case UserRosterRemove:
		msg = "user.roster";
		break;
	    case UserDataGet:
	    case UserDataSet:
		msg = "user.data";
		break;
	    case DiscoInfo:
	    case DiscoItems:
		msg = "contact.info";
		break;
	    default:
		Debug(&__plugin,DebugStub,
		    "StreamData(%s) unhandled request type %s id=%s",
		    c_str(),req->getValue(s_reqTypeParam),ns->name().c_str());
	}
    }
    else
	Debug(&__plugin,DebugStub,"StreamData(%s) no parameters in request %s",
	    c_str(),ns->name().c_str());
    if (msg) {
	Message* m = message(msg,*req,ok,ev->element());
	if (ok && (t == DiscoInfo || t == DiscoItems)) {
	    // Disco info/items responses contains the data
	    XmlElement* query = 0;
	    if (ev->element())
		query = XMPPUtils::findFirstChild(*ev->element(),XmlTag::Query,
		    (t == DiscoInfo) ? XMPPNamespace::DiscoInfo : XMPPNamespace::DiscoItems);
	    if (t == DiscoInfo) {
		if (query)
		    s_jabber->fillDiscoInfo(*m,query);
	    }
	    else if (query) {
		XmlElement* c = 0;
		do {
		    s_jabber->fillDiscoItems(*m,query,c);
		    if (c) {
			Engine::enqueue(m);
			m = message(msg,*req,ok,ev->element());
		    }
		}
		while (c);
	    }
	}
	else if (ok && t == UserDataGet) {
	    // Private data responses contains the data
	    unsigned int n = 0;
	    XmlElement* data = 0;
	    if (ev->element()) {
		XmlElement* query = XMPPUtils::findFirstChild(*ev->element(),
		    XmlTag::Query,XMPPNamespace::IqPrivate);
		data = query ? query->findFirstChild(0,&s_yateClientNs) : 0;
	    }
	    if (data) {
		XmlElement* x = 0;
		const String& tag = XMPPUtils::s_tag[XmlTag::Item];
		const String& param = XMPPUtils::s_tag[XmlTag::Parameter];
		while (0 != (x = data->findNextChild(x,&tag))) {
		    String prefix;
		    prefix << "data." << ++n;
		    m->addParam(prefix,x->attribute("id"));
		    prefix << ".";
		    XmlElement* p = 0;
		    while (0 != (p = x->findNextChild(p,&param))) {
			const char* name = p->attribute("name");
			if (!TelEngine::null(name))
			    m->addParam(prefix + name,p->attribute("value"));
		    }
		}
	    }
	    m->setParam("data.count",String(n));
	}
	Engine::enqueue(m);
    }
    removeRequest(ev->id());
    return true;
}

// Build an online presence element
XmlElement* StreamData::buildPresence(StreamData* d, const char* to)
{
    XmlElement* xml = XMPPUtils::createPresence(0,to);
    if (d) {
	if (!d->m_presence) {
	    TelEngine::destruct(xml);
	    return 0;
	}
	unsigned int n = d->m_presence->count();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* p = d->m_presence->getParam(i);
	    if (p && p->name())
		xml->addChild(XMPPUtils::createElement(p->name(),*p));
	}
	// TODO: Build data or module default caps
    }
    else {
	if (s_priority)
	    XMPPUtils::setPriority(*xml,s_priority);
	// TODO: Build module default caps
    }
    xml->addChild(XMPPUtils::createEntityCapsGTalkV1(s_capsNode,true));
    xml->addChild(XMPPUtils::createEntityCaps(s_jabber->features().m_entityCapsHash,
	s_capsNode));
    return xml;
}

// Build a message from a request
Message* StreamData::message(const char* msg, NamedList& req, bool ok, XmlElement* xml)
{
    Message* m = new Message(msg);
    m->copyParams(req);
    m->setParam("module",__plugin.name());
    m->addParam("requested_operation",m->getValue("operation"),false);
    m->setParam("operation",ok ? "result" : "error");
    if (!ok)
	getXmlError(*m,xml);
    m->clearParam(s_reqTypeParam);
    return m;
}


/*
 * JBMessageHandler
 */
JBMessageHandler::JBMessageHandler(int handler)
    : MessageHandler(lookup(handler,s_msgHandler),handler < 0 ? 100 : handler),
    m_handler(handler)
{
}

bool JBMessageHandler::received(Message& msg)
{
    if (__plugin.isModule(msg))
	return false;
    String* line = __plugin.getLine(msg);
    if (TelEngine::null(line))
	return false;
    XDebug(&__plugin,DebugAll,"%s line=%s",msg.c_str(),line->c_str());
    switch (m_handler) {
	case JabberIq:
	    return s_jabber->handleJabberIq(msg,*line);
	case ResNotify:
	    return s_jabber->handleResNotify(msg,*line);
	case ResSubscribe:
	    return s_jabber->handleResSubscribe(msg,*line);
	case UserRoster:
	    return s_jabber->handleUserRoster(msg,*line);
	case UserLogin:
	    return s_jabber->handleUserLogin(msg,*line);
	case UserUpdate:
	    return s_jabber->handleUserUpdate(msg,*line);
	case JabberAccount:
	    return s_jabber->handleJabberAccount(msg,*line);
	case ContactInfo:
	    return s_jabber->handleContactInfo(msg,*line);
	case MucRoom:
	    return s_jabber->handleMucRoom(msg,*line);
	case UserData:
	    return s_jabber->handleUserData(msg,*line);
	default:
	    Debug(&__plugin,DebugStub,"JBMessageHandler(%s) not handled!",msg.c_str());
    }
    return false;
}


/*
 * JBModule
 */
// Early load, late unload: we own the jabber engine
JBModule::JBModule()
    : Module("jabberclient","misc",true),
    m_init(false)
{
    Output("Loaded module Jabber Client");
}

JBModule::~JBModule()
{
    Output("Unloading module Jabber Client");
    TelEngine::destruct(s_jabber);
}

void JBModule::initialize()
{
    Output("Initializing module Jabber Client");
    Configuration cfg(Engine::configFile("jabberclient"));

    s_entityCaps.setFile(cfg.getValue("general","entitycaps_file"));
    if (!m_init) {
	m_init = true;
	setup();
	installRelay(Halt);
	installRelay(Help);
	installRelay(ImExecute);
	installRelay(EngineStart,"engine.start");
	s_jabber = new YJBEngine;
	s_jabber->debugChain(this);
	// Install handlers
	for (const TokenDict* d = s_msgHandler; d->token; d++) {
	    JBMessageHandler* h = new JBMessageHandler(d->value);
	    Engine::install(h);
	    m_handlers.append(h);
	}
	// Load entity caps file
	s_entityCaps.m_enable = cfg.getBoolValue("general","entitycaps",true);
	if (s_entityCaps.m_enable)
	    s_entityCaps.load();
	else
	    Debug(this,DebugAll,"Entity capability is disabled");
    }
    // Init the engine
    s_jabber->initialize(cfg.getSection("general"),!m_init);
}

// Message handler
bool JBModule::received(Message& msg, int id)
{
    if (id == ImExecute) {
	if (isModule(msg))
	    return false;
	String* line = getLine(msg);
	return !TelEngine::null(line) && s_jabber->handleMsgExecute(msg,*line);
    }
    if (id == Status) {
	String target = msg.getValue("module");
	// Target is the module
	if (!target || target == name())
	    return Module::received(msg,id);
	// Check additional commands
	if (!target.startSkip(name(),true))
	    return false;
	target.trimBlanks();
	if (!target)
	    return Module::received(msg,id);
	// Handle: status jabberclient stream_name
	if (target == "accounts")
	    statusAccounts(msg.retValue(),msg.getBoolValue("details",true));
	else {
	    statusModule(msg.retValue());
	    s_jabber->statusDetail(msg.retValue(),target);
	}
	msg.retValue() << "\r\n";
	return true;
    }
    if (id == Help) {
	String line = msg.getValue("line");
	if (line.null()) {
	    msg.retValue() << s_cmdStatus << "\r\n";
	    msg.retValue() << s_cmdDropStream << "\r\n";
	    msg.retValue() << s_cmdDebug << "\r\n";
	    return false;
	}
	if (line != name())
	    return false;
	msg.retValue() << s_cmdStatus << "\r\n";
	msg.retValue() << "Show stream status\r\n";
	msg.retValue() << s_cmdDropStream << "\r\n";
	msg.retValue() << "Terminate a stream or all of them\r\n";
	msg.retValue() << s_cmdDebug << "\r\n";
	msg.retValue() << "Show or set the debug level for a stream.\r\n";
	return true;
    }
    if (id == Halt) {
	s_jabber->setExiting();
	// Uninstall message handlers
	for (ObjList* o = m_handlers.skipNull(); o; o = o->skipNext()) {
	    JBMessageHandler* h = static_cast<JBMessageHandler*>(o->get());
	    Engine::uninstall(h);
	}
	s_jabber->cleanup();
	DDebug(this,DebugAll,"Halted");
	return Module::received(msg,id);
    }
    if (id == Timer)
	s_entityCaps.expire(msg.msgTime().msec());
    else if (id == EngineStart)
	s_jabber->handleEngineStart(msg);
    return Module::received(msg,id);
}

// Fill module status params
void JBModule::statusParams(String& str)
{
    s_jabber->statusParams(str);
}

// Fill module status detail
void JBModule::statusDetail(String& str)
{
    s_jabber->statusDetail(str);
}

// Handle command complete requests
bool JBModule::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    XDebug(this,DebugAll,"commandComplete() partLine='%s' partWord=%s",
	partLine.c_str(),partWord.c_str());

    // No line or 'help': complete module name
    if (partLine.null() || partLine == "help")
	return Module::itemComplete(msg.retValue(),name(),partWord);
    // Line is module name: complete module commands
    if (partLine == name()) {
	for (const String* list = s_cmds; list->length(); list++)
	    Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }

    String line = partLine;
    String word;
    getWord(line,word);

    if (word == name()) {
	// Line is module name: complete module commands and parameters
	getWord(line,word);
	// Check for a known command
	for (const String* list = s_cmds; list->length(); list++) {
	    if (*list != word)
		continue;
	    if (*list == "drop") {
		// Handle: jabberclient drop stream_name|*|all
		if (line)
		    return true;
		Module::itemComplete(msg.retValue(),"*",partWord);
		Module::itemComplete(msg.retValue(),"all",partWord);
		s_jabber->completeStreamName(msg.retValue(),partWord);
	    }
	    else if (*list == "debug") {
		// Handle: jabberclient debug stream_name [debug_level]
		if (line)
		    return true;
		s_jabber->completeStreamName(msg.retValue(),partWord);
	    }
	    return true;
	}
	// Complete module commands
	for (const String* list = s_cmds; list->length(); list++)
	    Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }
    if (word == "status") {
	// Handle: status jabberclient stream_name
	getWord(line,word);
	if (word != name()) {
	    if (word == "overview") {
		getWord(line,word);
		if (word == name() && line.null())
		    itemComplete(msg.retValue(),"accounts",partWord);
	    }
	    return Module::commandComplete(msg,partLine,partWord);
	}
	getWord(line,word);
	if (word == "accounts")
	    return false;
	if (word) {
	    if (line)
		return false;
	    s_jabber->completeStreamName(msg.retValue(),partWord);
	}
	else {
	    itemComplete(msg.retValue(),"accounts",partWord);
	    s_jabber->completeStreamName(msg.retValue(),partWord);
	}
	return true;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

// Handle command request
bool JBModule::commandExecute(String& retVal, const String& line)
{
    String l = line;
    String word;
    getWord(l,word);
    if (word != name())
	return false;
    getWord(l,word);
    DDebug(this,DebugAll,"Executing command '%s' params '%s'",word.c_str(),l.c_str());
    if (word == "drop") {
	Debug(this,DebugAll,"Executing '%s' command line=%s",word.c_str(),line.c_str());
	if (l == "all" || l == "*")
	    retVal << "Dropped " << s_jabber->dropAll() << " stream(s)";
	else {
	    // Handle: jabberclient drop stream_name
	    JBStream* stream = s_jabber->findStream(l);
	    if (stream) {
		stream->terminate(-1,true,0,XMPPError::NoError);
		TelEngine::destruct(stream);
		retVal << "Dropped stream '" << l << "'";
	    }
	    else
		retVal << "Stream '" << l << "' not found";
	}
    }
    else if (word == "debug") {
	Debug(this,DebugAll,"Executing '%s' command line=%s",word.c_str(),line.c_str());
	getWord(l,word);
	JBStream* stream = s_jabber->findStream(word);
	if (stream) {
	    retVal << "Stream '" << word << "' debug";
	    if (l) {
		int level = l.toInteger(-1);
		if (level >= 0) {
		    stream->debugLevel(level);
		    retVal << " at level " << stream->debugLevel();
		}
		else if (l.isBoolean()) {
		    stream->debugEnabled(l.toBoolean());
		    retVal << " is " << (stream->debugEnabled() ? "on" : "off");
		}
	    }
	    else
		 retVal << " at level " << stream->debugLevel();
	    TelEngine::destruct(stream);
	}
	else
	    retVal << "Stream '" << word << "' not found";
    }
    else
	return false;
    retVal << "\r\n";
    return true;
}

void JBModule::statusAccounts(String& retVal, bool details)
{
    DDebug(this,DebugAll,"List the status of all accounts");
    RefPointer<JBStreamSetList> list;
    s_jabber->getStreamList(list,JBStream::c2s);

    retVal.clear();
    retVal << "module=" << name();
    retVal << ",protocol=Jabber";
    retVal << ",format=Username|Status;";
    retVal << "accounts=";
    if (!list)
	retVal << 0;
    else
	retVal << list->sets().count();
    if (!details)
	return;

    String str = "";
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    JBClientStream* stream = static_cast<JBClientStream*>(s->get());
	    stream->lock();
	    str.append(stream->account(),",") << "=";
	    str.append(stream->local().bare()) << "|";
	    str << stream->stateName();
	    stream->unlock();
	}
    }
    list->unlock();
    list = 0;
    retVal.append(str,";");
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
