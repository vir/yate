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
protected:
    inline void getEntityCapsFile(String& file) {
	    file = Engine::configPath(Engine::clientMode());
	    if (!file.endsWith(Engine::pathSeparator()))
		file << Engine::pathSeparator();
	    file << "jabberentitycaps.xml";
	}
    // Notify changes and save the entity caps file
    virtual void capsAdded(JBEntityCaps* caps);
};

/*
 * Data attached to a stream
 */
class StreamData : public NamedList
{
public:
    inline StreamData(JBClientStream& m_owner, bool requestRoster)
	: NamedList(m_owner.local().bare()),
	m_requestRoster(requestRoster), m_presence(0)
	{}
    ~StreamData()
	{ TelEngine::destruct(m_presence); }
    // Retrieve a contact
    inline NamedList* contact(const String& name) {
	    if (name == *this)
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
    // Build an online presence element
    static XmlElement* buildPresence(StreamData* d = 0, const char* to = 0);

    // Request roster when connected
    bool m_requestRoster;
    // Presence data
    NamedList* m_presence;
    // Contacts and their resources
    ObjList m_contacts;
};

/*
 * Jabber engine
 */
class YJBEngine : public JBClientEngine
{
public:
    YJBEngine();
    ~YJBEngine();
    // Find a c2s stream by account
    inline JBClientStream* find(const String& name) {
	    JBStream* s = findStream(name);
	    return s ? s->clientStream() : 0;
	}
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
    // Process 'user.roster' messages
    bool handleUserRoster(Message& msg, const String& line);
    // Process 'user.update' messages
    bool handleUserUpdate(Message& msg, const String& line);
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
    inline Message* message(const char* msg, JBStream* stream = 0) {
	    Message* m = new Message(msg);
	    complete(*m,stream);
	    return m;
	}
    // Complete module, protocol and line parameters
    inline void complete(Message& m, JBStream* stream = 0) {
	    m.addParam("module",name());
	    m.addParam("protocol","jabber");
	    if (stream) {
		m.addParam("account",stream->name());
		m.addParam("line",stream->name());
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
    {"jabber.iq",           JBMessageHandler::JabberIq},
    {0,0}
};

// Add xml data parameter to a message
static inline void addValidParam(NamedList& list, const char* param, const char* value)
{
    if (!TelEngine::null(value))
	list.addParam(param,value);
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

// Notify changes and save the entity caps file
void YJBEntityCapsList::capsAdded(JBEntityCaps* caps)
{
    if (!caps)
	return;
    // Save the file
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
    m_features.add(XMPPNamespace::Session);
    m_features.add(XMPPNamespace::Register);
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
		Message* m = __plugin.message("msg.execute",ev->stream());
		m->addParam("type",ev->stanzaType());
		m->addParam("caller",ev->from().bare());
		addValidParam(*m,"caller_instance",ev->from().resource());
		XmlElement* xml = ev->releaseXml();
		addValidParam(*m,"subject",XMPPUtils::subject(*xml));
		addValidParam(*m,"body",XMPPUtils::body(*xml));
		String tmp("delay");
		XmlElement* delay = xml->findFirstChild(&tmp,&XMPPUtils::s_ns[XMPPNamespace::Delay]);
		if (delay) {
		    String* time = delay->getAttribute("stamp");
		    unsigned int sec = (unsigned int)-1;
		    if (!TelEngine::null(time))
			sec = XMPPUtils::decodeDateTimeSec(*time);
		    if (sec != (unsigned int)-1) {
			m->addParam("delay_time",String(sec));
			addValidParam(*m,"delay_text",delay->getText());
			JabberID from(delay->attribute("from"));
			if (from)
			    m->addParam("delay_by",from);
			else
			    m->addParam("delay_by",ev->stream()->remote());
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

// Process 'user.roster' messages
bool YJBEngine::handleUserRoster(Message& msg, const String& line)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    bool upd = (*oper == "update");
    if (!upd && *oper != "delete") {
	DDebug(this,DebugStub,"handleUserRoster() oper=%s not implemented!",oper->c_str());
	return false;
    }
    JBClientStream* s = find(line);
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

    XmlElement* query = XMPPUtils::createIq(XMPPUtils::IqSet,0,0);
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
    TelEngine::destruct(s);
    return ok;
}

// Process 'user.update' messages
bool YJBEngine::handleUserUpdate(Message& msg, const String& line)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    JBClientStream* s = find(line);
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

// Process 'jabber.iq' messages
bool YJBEngine::handleJabberIq(Message& msg, const String& line)
{
    JBClientStream* s = find(line);
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
    JBClientStream* s = find(line);
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
	unsigned int n = c->count();
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
    JBClientStream* s = find(line);
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
    JBClientStream* s = find(line);
    if (!s)
	return false;
    // Use a while to break to the end
    bool ok = false;
    while (true) {
	bool sub = (*oper == "subscribed");
	if (sub || *oper == "unsubscribed") {
	    JabberID to(msg.getValue("to"));
	    if (to.node()) {
		XmlElement* p = XMPPUtils::createPresence(0,to.bare(),
		    sub ? XMPPUtils::Subscribed : XMPPUtils::Unsubscribed);
		ok = s->sendStanza(p);
	    }
	    break;
	}
	Debug(this,DebugStub,"handleResNotify() oper=%s not implemented!",
	    oper->c_str());
	break;
    }
    TelEngine::destruct(s);
    return ok;
}

// Process 'msg.execute' messages
bool YJBEngine::handleMsgExecute(Message& msg, const String& line)
{
    DDebug(this,DebugAll,"handleMsgExecute() line=%s",line.c_str());
    JBClientStream* s = find(line);
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
    return true;
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

    JBClientStream* stream = s_jabber->find(line);
    bool ok = false;
    if (login) {
	if (!stream) {
	    stream = s_jabber->create(line,msg);
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

// Handle 'presence' stanzas
// The given event is always valid and carry a valid stream and xml element
void YJBEngine::processPresenceStanza(JBEvent* ev)
{
    DDebug(this,DebugAll,"Processing presence type=%s from=%s",
	ev->stanzaType().c_str(),ev->from().c_str());
    if (!ev->from())
	return;
    XMPPUtils::Presence pres = XMPPUtils::presenceType(ev->stanzaType());
    bool online = pres == XMPPUtils::PresenceNone;
    if (online || pres == XMPPUtils::Unavailable) {
	String capsId;
	if (online) {
	    if (!ev->from().resource())
		return;
	    s_entityCaps.processCaps(capsId,ev->element(),ev->stream(),ev->to(),ev->from());
	}
	// Update contact list resources
	Lock lock(ev->stream());
	StreamData* sdata = streamData(ev);
	if (!sdata)
	    return;
	if (online)
	    sdata->setResource(ev->from().bare(),ev->from().resource(),capsId);
	else
	    sdata->removeResource(ev->from().bare(),ev->from().resource());
	lock.drop();
	// Notify
	Message* m = __plugin.message("resource.notify",ev->stream());
	m->addParam("operation",online ? "online" : "offline");
	m->addParam("contact",ev->from().bare());
	if (ev->from().resource())
	    m->addParam("instance",ev->from().resource());
	if (online) {
	    m->addParam("uri",ev->from());
	    m->addParam("priority",String(XMPPUtils::priority(*ev->element())));
	    const String& ns = XMPPUtils::s_ns[ev->stream()->xmlns()];
	    String s("show");
	    XmlElement* tmp = ev->element()->findFirstChild(&s,&ns);
	    if (tmp)
		addValidParam(*m,"show",tmp->getText());
	    s = "status";
	    tmp = ev->element()->findFirstChild(&s,&ns);
	    if (tmp)
		addValidParam(*m,"status",tmp->getText());
	    if (capsId)
		s_entityCaps.addCaps(*m,capsId);
	    // TODO: add arbitrary children texts
	}
	Engine::enqueue(m);
	return;
    }
    bool subReq = (pres == XMPPUtils::Subscribe);
    if (subReq || pres == XMPPUtils::Unsubscribe) {
	Message* m = __plugin.message("resource.subscribe",ev->stream());
	m->addParam("operation",ev->stanzaType());
	m->addParam("subscriber",ev->from().bare());
	Engine::enqueue(m);
	return;
    }
    // Ignore XMPPUtils::Subscribed, XMPPUtils::Unsubscribed, XMPPUtils::Probe,
    //  XMPPUtils::PresenceError
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
    int t = XmlTag::Count;
    int n = XMPPNamespace::Count;
    if (service)
	XMPPUtils::getTag(*service,t,n);
    // Server entity caps responses
    if (rsp && n == XMPPNamespace::DiscoInfo &&
	s_entityCaps.processRsp(ev->element(),ev->id(),type == XMPPUtils::IqResult))
	return;
    bool fromServer = (!ev->from() || ev->from() == ev->stream()->local().bare());
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
    // Iq from known contact or user itself
    ev->stream()->lock();
    StreamData* data = streamData(ev);
    bool allow = data && data->contact(ev->from().bare());
    ev->stream()->unlock();
    if (!allow)
	allow = (ev->from().bare() == ev->to().bare());
    if (!allow) {
	if (!rsp)
	    ev->sendStanzaError(XMPPError::ServiceUnavailable);
	return;
    }
    // Route the iq
    Message m("jabber.iq");
    __plugin.complete(m,ev->stream());
    m.addParam("from",ev->from().bare());
    m.addParam("from_instance",ev->from().resource());
    m.addParam("to",ev->to().bare());
    m.addParam("to_instance",ev->to().resource());
    addValidParam(m,"id",ev->id());
    addValidParam(m,"type",ev->stanzaType());
    if (!rsp && n != XMPPNamespace::Count)
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

    Message* m = __plugin.message("user.notify",ev->stream());
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
    addValidParam(list,pref + "name",x.attribute("name"));
    addValidParam(list,pref + "subscription",x.attribute("subscription"));
    NamedString* groups = new NamedString(pref + "groups");
    list.addParam(groups);
    // Groups and other children
    const String* ns = &XMPPUtils::s_ns[XMPPNamespace::Roster];
    for (XmlElement* c = x.findFirstChild(0,ns); c; c = x.findNextChild(c,0,ns)) {
	if (XMPPUtils::isUnprefTag(*c,XmlTag::Group))
	    groups->append(c->getText(),",");
	else
	    list.append(pref + c->unprefixedTag(),c->getText());
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
	Message* m = __plugin.message("user.roster",ev->stream());
	String* sub = x->getAttribute("subscription");
	bool upd = !sub || *sub != "remove";
	ev->stream()->lock();
	StreamData* sdata = streamData(ev);
	if (sdata) {
	    if (*jid != ev->stream()->local().bare()) {
		if (upd)
		    sdata->addContact(*jid);
		else
		    sdata->removeContact(*jid);
		Debug(this,DebugAll,"Account(%s) %s roster item '%s'",
		    m->getValue("account"),upd ? "updated" : "deleted",jid->c_str());
	    }
	}
	ev->stream()->unlock();
	m->addParam("operation",upd ? "update" : "delete");
	m->addParam("contact.count","1");
	addRosterItem(*m,*x,*jid,1,!upd);
	Engine::enqueue(m);
	return;
    }
    // Ignore responses for now (except for roster query)
    // The client shouldn't expect the result (the server will push changes)
    if (iqType == XMPPUtils::IqResult) {
	// Handle 'query' responses
	if (!service || tag != XmlTag::Query || ev->id() != s_rosterQueryId)
	    return;
	Message* m = __plugin.message("user.roster",ev->stream());
	m->addParam("operation","update");
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
		    sdata->addContact(*jid);
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
    if (iqType == XMPPUtils::IqError)
	return;
    ev->sendStanzaError(XMPPError::ServiceUnavailable);
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
	if (*c == name)
	    return o;
    }
    return 0;
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
    xml->addChild(XMPPUtils::createEntityCapsGTalkV1());
    return xml;
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

    if (!m_init) {
	m_init = true;
	setup();
	installRelay(Halt);
	installRelay(Help);
	installRelay(ImExecute);
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
	if (!target.startSkip(name(),false))
	    return false;
	target.trimBlanks();
	if (!target)
	    return Module::received(msg,id);
	// Handle: status jabberclient stream_name
	statusModule(msg.retValue());
	s_jabber->statusDetail(msg.retValue(),target);
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
	if (word != name())
	    return Module::commandComplete(msg,partLine,partWord);
	getWord(line,word);
	if (word) {
	    if (line)
		return false;
	    s_jabber->completeStreamName(msg.retValue(),partWord);
	}
	else
	    s_jabber->completeStreamName(msg.retValue(),partWord);
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

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
