/**
 * yjinglechan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Jingle channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 * Author: Marian Podgoreanu
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
#include <yateversn.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <yatejingle.h>

using namespace TelEngine;

namespace { // anonymous

class YJBEngine;                         // Jabber engine. Initiate protocol from Yate run mode
class YJGEngine;                         // Jingle service
class YJBMessage;                        // Message service
class YJBStreamService;                  // Stream start/stop event service
class YJBClientPresence;                 // Presence service for client streams
class YJBPresence;                       // Presence service
class YJBIqService;                      // Handle 'iq' stanzas not processed by other services
class YJGData;                           // Handle the transport and formats for a connection
class YJGConnection;                     // Jingle channel
class ResNotifyHandler;                  // resource.notify handler
class ResSubscribeHandler;               // resource.subscribe handler
class UserLoginHandler;                  // user.login handler
class XmppGenerateHandler;               // xmpp.generate handler
class XmppIqHandler;                     // xmpp.iq handler used to respond to unprocessed set/get stanzas
class YJGDriver;                         // The driver

// TODO:
//  Negotiate DTMF method. Accept remote peer's method;

// Username/Password length for transport
#define JINGLE_AUTHSTRINGLEN         16

// URI
#define BUILD_XMPP_URI(jid) (plugin.name() + ":" + jid)


/**
 * YJBEngine
 */
class YJBEngine : public JBEngine
{
public:
    inline YJBEngine(Protocol proto) : JBEngine(proto)
	{}
    virtual bool exiting() const
	{ return Engine::exiting(); }
    void initialize();
    // Setup the transport layer security for a stream
    virtual bool encryptStream(JBStream* stream);
};

/**
 * YJGEngine
 */
class YJGEngine : public JGEngine
{
public:
    inline YJGEngine(YJBEngine* engine, int prio)
	: JGEngine(engine,0,prio), m_requestSubscribe(true)
	{}
    inline bool requestSubscribe() const
	{ return m_requestSubscribe; }
    void initialize();
    virtual void processEvent(JGEvent* event);
private:
    bool m_requestSubscribe;             // Request subscribe before making a call
};

/**
 * YJBMessage
 */
class YJBMessage : public JBMessage
{
public:
    inline YJBMessage(YJBEngine* engine, int prio)
	: JBMessage(engine,0,prio)
	{}
    void initialize();
    virtual void processMessage(JBEvent* event);
};

/**
 * YJBStreamService
 */
class YJBStreamService : public JBService
{
public:
    YJBStreamService(JBEngine* engine, int prio)
	: JBService(engine,"jabberstreamservice",0,prio)
	{}
    virtual ~YJBStreamService()
	{}
    void initialize();
protected:
    // Process stream termination events
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);
};

/**
 * YJBClientPresence
 */
class YJBClientPresence : public JBService
{
public:
    YJBClientPresence(JBEngine* engine, int prio)
	: JBService(engine,"clientpresence",0,prio)
	{}
    virtual ~YJBClientPresence()
	{}
    void initialize();
protected:
    // Process stream termination events
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);
};

/**
 * YJBPresence
 */
class YJBPresence : public JBPresence
{
    friend class YUserPresence;
public:
    inline YJBPresence(JBEngine* engine, int prio)
	: JBPresence(engine,0,prio)
	{}
    void initialize();
    // Overloaded methods
    virtual bool notifyProbe(JBEvent* event);
    virtual bool notifySubscribe(JBEvent* event, Presence presence);
    virtual void notifySubscribe(XMPPUser* user, Presence presence);
    virtual bool notifyPresence(JBEvent* event, bool available);
    virtual void notifyPresence(XMPPUser* user, JIDResource* resource);
    virtual void notifyNewUser(XMPPUser* user);
    // Create & enqueue a message from received presence parameter.
    // Add status/operation/subscription parameters
    static Message* message(int presence, const char* from, const char* to,
	const char* subscription);
};

/**
 * YJBIqService
 */
class YJBIqService : public JBService
{
public:
    YJBIqService(JBEngine* engine, int prio)
	: JBService(engine,"jabberiqservice",0,prio)
	{}
    void initialize();
protected:
    // Process iq events
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);
};

/**
 * YJGData
 */
class YJGData : public JGTransport, virtual public JGAudioList
{
    friend class YJGConnection;
public:
    // Init data and format list
    YJGData(YJGConnection* conn, Message* msg = 0);
    // Release remote transport info
    virtual ~YJGData();
    // Create media description XML element
    inline XMLElement* mediaXML()
	{ return JGAudioList::toXML(); }
    // Reserve RTP address and port or start the RTP session
    bool rtp(bool start);
    // Update media from received data. Return false if already updated media or failed to negotiate a format
    // Hangup the connection if failed to negotiate audio formats
    bool updateMedia(JGAudioList& media);
    // Check received transports and try to accept one if not already negotiated one
    // Return true if accepted
    bool updateTransport(ObjList& transport);
protected:
    YJGConnection* m_conn;               // Connection owning this object
    bool m_mediaReady;                   // Media ready (updated) flag
    bool m_transportReady;               // Transport ready (both parties) flag
    bool m_started;                      // True if socket.stun already sent
    JGTransport* m_remote;               // The remote transport info
};

/**
 * YJGConnection
 */
class YJGConnection : public Channel
{
    YCLASS(YJGConnection,Channel)
public:
    enum State {
	Pending,
	Active,
	Terminated,
    };
    // Outgoing constructor
    YJGConnection(Message& msg, const char* caller, const char* called, bool available);
    // Incoming contructor
    YJGConnection(JGEvent* event);
    virtual ~YJGConnection();
    inline State state()
	{ return m_state; }
    inline const JabberID& local() const
	{ return m_local; }
    inline const JabberID& remote() const
	{ return m_remote; }
    // Overloaded methods from Channel
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    virtual bool callRouted(Message& msg);
    virtual void disconnected(bool final, const char* reason);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgUpdate(Message& msg);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool msgTone(Message& msg, const char* tone);
    inline bool disconnect(const char* reason) {
	    setReason(reason);
	    return Channel::disconnect(m_reason);
	}
    // Route an incoming call
    bool route();
    // Process Jingle and Terminated events
    void handleEvent(JGEvent* event);
    void hangup(bool reject, const char* reason = 0);
    // Process remote user's presence changes.
    // Make the call if outgoing and in Pending (waiting for presence information) state
    // Hangup if the remote user is unavailbale
    // Return true to disconnect
    bool presenceChanged(bool available);

    inline void updateResource(const String& resource) {
	    if (!m_remote.resource() && resource)
		m_remote.resource(resource);
	}

    inline void getRemoteAddr(String& dest) {
	if (m_session && m_session->stream())
	    dest = m_session->stream()->addr().host();
    }

    inline void setReason(const char* reason) {
	    if (!m_reason)
		m_reason = reason;
	}

private:
    Mutex m_mutex;                       // Lock transport and session
    State m_state;                       // Connection state
    JGSession* m_session;                // Jingle session attached to this connection
    JabberID m_local;                    // Local user's JID
    JabberID m_remote;                   // Remote user's JID
    String m_callerPrompt;               // Text to be sent to called before calling it
    YJGData* m_data;                     // Transport and data format(s)
    // Termination
    bool m_hangup;                       // Hang up flag: True - already hung up
    String m_reason;                     // Hangup reason
    // Timeouts
    u_int64_t m_timeout;                 // Timeout for not answered outgoing connections
};

/**
 * resource.notify message handler
 */
class ResNotifyHandler : public MessageHandler
{
public:
    ResNotifyHandler() : MessageHandler("resource.notify") {}
    virtual bool received(Message& msg);
    static void process(const JabberID& from, const JabberID& to,
	const String& status, bool subFrom, NamedList* params = 0);
    static void sendPresence(JabberID& from, JabberID& to, const String& status,
	NamedList* params = 0);
};

/**
 * resource.subscribe message handler
 */
class ResSubscribeHandler : public MessageHandler
{
public:
    ResSubscribeHandler() : MessageHandler("resource.subscribe") {}
    virtual bool received(Message& msg);
};

/**
 * user.login handler
 */
class UserLoginHandler : public MessageHandler
{
public:
    UserLoginHandler() : MessageHandler("user.login") {}
    virtual bool received(Message& msg);
};

/**
 * xmpp.generate message handler
 */
class XmppGenerateHandler : public MessageHandler
{
public:
    inline XmppGenerateHandler() : MessageHandler("xmpp.generate") {}
    virtual bool received(Message& msg);
};

/**
 * xmpp.iq message handler
 */
class XmppIqHandler : public MessageHandler
{
public:
    inline XmppIqHandler(int prio = 1000) : MessageHandler("xmpp.iq",prio) {}
    virtual bool received(Message& msg);
};

/**
 * YJGDriver
 */
class YJGDriver : public Driver
{
public:
    // Enumerate protocols supported by this module
    enum Protocol {
	Jabber     = 0,
	Xmpp       = 1,
	Jingle     = 2,
	ProtoCount = 3
    };
    // Additional driver status commands
    enum StatusCommands {
	StatusStreams  = 0,              // Show all streams
	StatusCmdCount = 1
    };
    YJGDriver();
    virtual ~YJGDriver();
    // Check if the channels should send single DTMFs
    inline bool singleTone() const
	{ return m_singleTone; }
    // Inherited methods
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    // Message handler: Disconnect channels, destroy streams, clear rosters
    virtual bool received(Message& msg, int id);
    // Try to create a JID from a message.
    // First try to get the 'username' parameter of the message. Then the 'from' parmeter
    // @param checkDomain True to check if jid's domain is valid
    // Return false if node or domain are 0 or domain is invalid
    bool getJidFrom(JabberID& jid, Message& msg, bool checkDomain = false);
    // Assign param value to jid.
    // @param checkDomain True to check if jid's domain is valid
    // Return false if node or domain are 0 or domain is invalid
    bool decodeJid(JabberID& jid, Message& msg, const char* param,
	bool checkDomain = false);
    // Create the presence notification command
    XMLElement* getPresenceCommand(JabberID& from, JabberID& to, bool available);
    // Create a random string of JINGLE_AUTHSTRINGLEN length
    void createAuthRandomString(String& dest);
    // Process presence. Notify connections
    void processPresence(const JabberID& local, const JabberID& remote,
	bool available, bool audio);
    // Create a media string from a list
    void createMediaString(String& dest, ObjList& formats, char sep);
    // Find a connection by local and remote jid, optionally ignore local
    // resource (always ignore if local has no resource)
    YJGConnection* find(const JabberID& local, const JabberID& remote, bool anyResource = false);
    // Build and add XML child elements from a received message
    bool addChildren(NamedList& msg, XMLElement* xml = 0, ObjList* list = 0);
    // Check if this module handles a given protocol
    static bool canHandleProtocol(const String& proto) {
	    for (unsigned int i = 0; i < ProtoCount; i++)
		if (proto == s_protocol[i])
		    return true;
	    return false;
	}
    // Check if this module handles a given protocol
    static const char* defProtoName()
	{ return s_protocol[Jabber].c_str(); }
    // Protocols supported by this module
    static const String s_protocol[ProtoCount];
protected:
    // Handle command complete requests
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    // Additional driver status commands
    static String s_statusCmd[StatusCmdCount];
private:
    // Check and build caller and called for Component run mode
    // Caller: Set user if missing. Get default server identity for Yate Component
    // Try to get an available resource for the called party
    bool setComponentCall(JabberID& caller, JabberID& called, const char* cr,
	const char* cd, bool& available, String& error);

    bool m_init;
    bool m_singleTone;                   // Send single/batch DTMFs
    bool m_installIq;                    // Install the 'iq' service in jabber
                                         //  engine and xmpp. message handlers
    String m_statusCmd;                  //
};


/**
 * Local data
 */
static Configuration s_cfg;                       // The configuration file
static JGAudioList s_knownCodecs;                 // List of all known codecs (JGAudio)
static JGAudioList s_usedCodecs;                  // List of used codecs (JGAudio)
static String s_localAddress;                     // The local machine's address
static unsigned int s_pendingTimeout = 10000;     // Outgoing call pending timeout
static String s_anonymousCaller = "unk_caller";   // Caller name when missing
static YJBEngine* s_jabber = 0;
static YJGEngine* s_jingle = 0;
static YJBMessage* s_message = 0;
static YJBPresence* s_presence = 0;
static YJBClientPresence* s_clientPresence = 0;
static YJBStreamService* s_stream = 0;
static YJBIqService* s_iqService = 0;
const String YJGDriver::s_protocol[YJGDriver::ProtoCount] = {"jabber", "xmpp", "jingle"};
static YJGDriver plugin;                          // The driver


// Get the number of private threads of a given type
// Force to 1 for client run mode
// Force at least 1 otherwise
inline int threadCount(const NamedList& params, const char* param)
{
    if (s_jabber->protocol() == JBEngine::Client)
	return 1;
    int t = params.getIntValue(param);
    return t < 1 ? 1 : t;
}

inline void addValidParam(Message& m, const char* param, const char* value)
{
    if (value)
	m.addParam(param,value);
}


/**
 * YJBEngine
 */
void YJBEngine::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force private processing. Force 1 thread for client run mode
    sect->setParam("private_process_threads",String(threadCount(*sect,"private_process_threads")));
    sect->setParam("private_receive_threads",String(threadCount(*sect,"private_receive_threads")));
    JBEngine::initialize(*sect);

    String defComponent;
    // Set server list if not client
    unsigned int count = (protocol() != Client) ? s_cfg.sections() : 0;
    for (unsigned int i = 0; i < count; i++) {
	const NamedList* comp = s_cfg.getSection(i);
	String name = comp ? comp->c_str() : "";
	if (!name || name == "general" || name == "codecs")
	    continue;

	const char* address = comp->getValue("address");
	String tmp = comp->getValue("port");
	int port = tmp.toInteger();
	if (!(address && port)) {
	    Debug(this,DebugNote,
		"Invalid address=%s or port=%s in configuration for %s",
		address,tmp.c_str(),name.c_str());
	    continue;
	}
	const char* password = comp->getValue("password");
	// Check identity. Construct the full identity
	String identity = comp->getValue("identity");
	if (!identity)
	    identity = name;
	String fullId;
	bool keepRoster = false;
	if (identity == name) {
	    String subdomain = comp->getValue("subdomain",s_cfg.getValue(
		"general","default_resource",defaultResource()));
	    identity = subdomain;
	    identity << '.' << name;
	    fullId = name;
	}
	else {
	    keepRoster = true;
	    fullId << '.' << name;
	    if (identity.endsWith(fullId)) {
		if (identity.length() == fullId.length()) {
		    Debug(this,DebugNote,"Invalid identity=%s in configuration for %s",
			identity.c_str(),name.c_str());
		    continue;
		}
		fullId = identity;
	    }
	    else {
		fullId = identity;
		fullId << '.' << name;
	    }
	    identity = fullId;
	}
	if (!identity)
	    continue;
	int flags = XMPPUtils::decodeFlags(comp->getValue("options"),
	    XMPPServerInfo::s_flagName);
	if (!comp->getBoolValue("auto_restart",true))
	    flags |= XMPPServerInfo::NoAutoRestart;
	if (keepRoster)
	    flags |= XMPPServerInfo::KeepRoster;
	XMPPServerInfo* server = new XMPPServerInfo(name,address,port,
	    password,identity,fullId,flags);
	bool startup = comp->getBoolValue("startup");
#ifdef DEBUG
	String f;
	XMPPUtils::buildFlags(f,flags,XMPPServerInfo::s_flagName);
	DDebug(this,DebugAll,
	    "Add server '%s' %s:%d ident=%s full-ident=%s options=%s",
	    name.c_str(),address,port,identity.c_str(),fullId.c_str(),
	    f.c_str());
#endif
	appendServer(server,startup);
	if (!defComponent || comp->getBoolValue("default"))
	    defComponent = name;
    }
    // Set default component server
    if (protocol() == Component)
	setComponentServer(defComponent);
}

// Setup the transport layer security for a stream
bool YJBEngine::encryptStream(JBStream* stream)
{
    if (!stream)
	return false;
    Message msg("socket.ssl");
    msg.userData(stream);
    msg.addParam("server",String::boolText(!stream->outgoing()));
    return Engine::dispatch(msg);
}


/**
 * YJGEngine
 */
void YJGEngine::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force private processing
    sect->setParam("private_process_threads",String(threadCount(*sect,"private_process_threads")));
    JGEngine::initialize(*sect);
    // Init data
    m_requestSubscribe = sect->getBoolValue("request_subscribe",true);
}

// Process jingle events
void YJGEngine::processEvent(JGEvent* event)
{
    if (!event)
	return;
    JGSession* session = event->session();
    // This should never happen !!!
    if (!session) {
	Debug(this,DebugWarn,"Received event without session");
	delete event;
	return;
    }
    YJGConnection* conn = static_cast<YJGConnection*>(session->userData());
    if (conn) {
	conn->handleEvent(event);
	if (event->final())
	    conn->disconnect(event->reason());
    }
    else {
	if (event->type() == JGEvent::Jingle &&
	    event->action() == JGSession::ActInitiate) {
	    if (event->session()->ref()) {
		conn = new YJGConnection(event);
		// Constructor failed ?
		if (conn->state() == YJGConnection::Pending)
		    TelEngine::destruct(conn);
		else if (!conn->route())
		    event->session()->userData(0);
	    }
	    else {
		Debug(this,DebugWarn,"Session ref failed for new connection");
		event->session()->hangup(false,"failure");
	    }
        }
	else
	    DDebug(this,DebugAll,"Invalid (non initiate) event for new session");
    }
    delete event;
}


/**
 * YJBMessage
 */
void YJBMessage::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force sync (not enqueued) message processing
    sect->setParam("sync_process","true");
    JBMessage::initialize(*sect);
}

// Process a Jabber message
void YJBMessage::processMessage(JBEvent* event)
{
    if (!(event && event->text()))
	return;

    YJGConnection* conn = plugin.find(event->to().c_str(),event->from().c_str());
    DDebug(this,DebugInfo,"Message from=%s to=%s conn=%p '%s' [%p]",
	event->from().c_str(),event->to().c_str(),conn,event->text().c_str(),this);
    if (conn) {
	Message* m = conn->message("chan.text");
	m->addParam("text",event->text());
	Engine::enqueue(m);
    }
}


/**
 * YJBStreamService
 */
void YJBStreamService::initialize()
{
    debugChain(&plugin);
}

// Process stream termination events
bool YJBStreamService::accept(JBEvent* event, bool& processed, bool& insert)
{
    JBStream* stream = event ? event->stream() : 0;
    if (!stream)
	return false;
    if (event->type() != JBEvent::Terminated &&
	event->type() != JBEvent::Running &&
	event->type() != JBEvent::Destroy)
	return false;

    Message* m = new Message("user.notify");
    m->addParam("account",stream->name());
    m->addParam("protocol",plugin.defProtoName());
    m->addParam("username",stream->local().node());
    m->addParam("server",stream->local().domain());
    m->addParam("jid",stream->local());
    m->addParam("registered",String::boolText(event->type() == JBEvent::Running));
    if (event->type() != JBEvent::Running && event->text())
	m->addParam("reason",event->text());
    bool restart = (stream->state() != JBStream::Destroy && stream->flag(JBStream::AutoRestart));
    m->addParam("autorestart",String::boolText(restart));
    Engine::enqueue(m);
    return false;
}

/**
 * YJBClientPresence
 */
void YJBClientPresence::initialize()
{
    debugChain(&plugin);
}

// Process client presence and roster updates
bool YJBClientPresence::accept(JBEvent* event, bool& processed, bool& insert)
{
    if (!event)
	return false;

    processed = true;
    while (true) {
	if (event->type() != JBEvent::Presence &&
	    event->type() != JBEvent::IqClientRosterUpdate) {
	    Debug(this,DebugStub,"Can't accept unexpected event=%s [%p]",
		event->name(),this);
	    processed = false;
	    break;
	}

	// User roster update
	if (event->type() == JBEvent::IqClientRosterUpdate) {
	    if (!event->child())
		break;
	    XMLElement* item = event->child()->findFirstChild(XMLElement::Item);
	    for (; item; item = event->child()->findNextChild(item,XMLElement::Item)) {
		Message* m = YJBPresence::message(-1,0,event->to().bare(),
		    item->getAttribute("subscription"));
		if (event->stream() && event->stream()->name())
		    m->setParam("account",event->stream()->name());
		m->setParam("contact",item->getAttribute("jid"));
		addValidParam(*m,"contactname",item->getAttribute("name"));
		addValidParam(*m,"ask",item->getAttribute("ask"));
		// Get jid group(s)
		XMLElement* group = item->findFirstChild(XMLElement::Group);
		for (; group; group = item->findNextChild(group,XMLElement::Group))
		    addValidParam(*m,"group",group->getText());
		Engine::enqueue(m);
	    }
	    break;
	}

	// Presence
	const char* sub = 0;
	if (event->stream() && event->stream()->type() == JBEngine::Client) {
	    JBClientStream* stream = static_cast<JBClientStream*>(event->stream());
	    Lock lock(stream->roster());
	    XMPPUser* user = stream->getRemote(event->from());
	    if (user) {
		sub = XMPPDirVal::lookup((int)user->subscription());
		TelEngine::destruct(user);
	    }
	}


	Message* m = 0;
	JBPresence::Presence pres = JBPresence::presenceType(event->stanzaType());

	if (pres == JBPresence::None || pres == JBPresence::Unavailable) {
	    bool capAudio = false;
	    bool available = (pres == JBPresence::None);
	    JIDResource* res = 0;
	    if (event->element()) {
		res = new JIDResource(event->from().resource());
		if (res->fromXML(event->element())) {
		    capAudio = res->hasCap(JIDResource::CapAudio);
		    available = res->available();
		}
	    }
	    // Notify presence to module and enqueue message in engine
	    plugin.processPresence(event->to(),event->from(),available,capAudio);
	    m = YJBPresence::message(pres,event->from(),event->to(),sub);
	    if (res) {
		m->addParam("audio",String::boolText(capAudio));
		ObjList* o = res->infoXml()->skipNull();
		if (o || res->status()) {
		    String prefix = "jingle";
		    m->addParam("message-prefix",prefix);
		    prefix << ".";
		    unsigned int n = 1;
		    // Set status: avoid some meaningful values
		    if (res->status())
			if (res->status() != "subscribed" &&
			    res->status() != "unsubscribed" &&
			    res->status() != "offline")
			    m->setParam("status",res->status());
			else {
			    m->addParam(prefix + "1","status");
			    m->addParam(prefix + "1.",res->status());
			    n = 2;
			}
		    for (; o; o = o->skipNext(), n++) {
			XMLElement* e = static_cast<XMLElement*>(o->get());
			e->toList(*m,String(prefix + String(n)));
		    }
		}
		TelEngine::destruct(res);
	    }
	}
	else
	    switch (pres) {
		case JBPresence::Subscribed:
		case JBPresence::Unsubscribed:
		case JBPresence::Subscribe:
		case JBPresence::Unsubscribe:
		case JBPresence::Probe:
		    m = YJBPresence::message(pres,event->from().bare(),
			event->to().bare(),sub);
		    break;
		case JBPresence::Error:
		    if (event->text()) {
			m = YJBPresence::message(pres,event->from().bare(),
			    event->to().bare(),sub);
			m->setParam("error",event->text());
		    }
		    break;
		default:
		    Debug(this,DebugStub,"accept() not implemented for presence=%s [%p]",
			event->stanzaType().c_str(),this);
		    processed = false;
	    }

	if (m) {
	    if (event->stream() && event->stream()->name())
		m->setParam("account",event->stream()->name());
	    Engine::enqueue(m);
	}
	break;
    }

    return processed;
}


/**
 * YJBPresence
 */
void YJBPresence::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force private processing
    sect->setParam("private_process_threads",String(threadCount(*sect,"private_process_threads")));
    JBPresence::initialize(*sect);
}

bool YJBPresence::notifyProbe(JBEvent* event)
{
    XDebug(this,DebugAll,"notifyProbe local=%s remote=%s [%p]",
	event->to().c_str(),event->from().c_str(),this);
    Engine::enqueue(message(JBPresence::Probe,event->from().bare(),event->to().bare(),0));
    return true;
}

bool YJBPresence::notifySubscribe(JBEvent* event, Presence presence)
{
    XDebug(this,DebugAll,"notifySubscribe(%s) local=%s remote=%s [%p]",
	presenceText(presence),event->to().c_str(),event->from().c_str(),this);
    // Respond if auto subscribe
    if (!ignoreNonRoster() && event->stream() && autoSubscribe().from() &&
	(presence == JBPresence::Subscribe || presence == JBPresence::Unsubscribe)) {
	if (presence == JBPresence::Subscribe)
	    presence = JBPresence::Subscribed;
	else
	    presence = JBPresence::Unsubscribed;
	XMLElement* xml = createPresence(event->to().bare(),event->from().bare(),presence);
	event->stream()->sendStanza(xml);
	return true;
    }
    // Enqueue message
    Engine::enqueue(message(presence,event->from().bare(),event->to().bare(),0));
    return true;
}

void YJBPresence::notifySubscribe(XMPPUser* user, Presence presence)
{
    if (!user)
	return;
    XDebug(this,DebugAll,"notifySubscribe(%s) local=%s remote=%s [%p]",
	presenceText(presence),user->local()->jid().bare().c_str(),
	user->jid().bare().c_str(),this);
    Engine::enqueue(message(presence,user->jid().bare(),user->local()->jid().bare(),0));
}

bool YJBPresence::notifyPresence(JBEvent* event, bool available)
{
    // Check audio properties and availability for received resource
    bool capAudio = false;
    if (event && event->element()) {
	JIDResource* res = new JIDResource(event->from().resource());
	if (res->fromXML(event->element())) {
	    capAudio = res->hasCap(JIDResource::CapAudio);
	    available = res->available();
	}
	TelEngine::destruct(res);
    }
    Debug(this,DebugAll,"notifyPresence local=%s remote=%s available=%s [%p]",
	event->to().c_str(),event->from().c_str(),String::boolText(available),this);
    // Notify presence to module and enqueue message in engine
    plugin.processPresence(event->to(),event->from(),available,capAudio);
    Engine::enqueue(message(available ? JBPresence::None : JBPresence::Unavailable,
	event->from().bare(),event->to().bare(),0));
    return true;
}

// Notify plugin and enqueue message in engine
void YJBPresence::notifyPresence(XMPPUser* user, JIDResource* resource)
{
    if (!(user && resource))
	return;
    JabberID remote(user->jid().node(),user->jid().domain(),resource->name());
    Debug(this,DebugAll,"notifyPresence local=%s remote=%s available=%s [%p]",
	user->local()->jid().c_str(),remote.c_str(),
	String::boolText(resource->available()),this);
    plugin.processPresence(user->local()->jid(),remote,resource->available(),
	resource->hasCap(JIDResource::CapAudio));
    Engine::enqueue(message(resource->available() ? JBPresence::None : JBPresence::Unavailable,
	user->jid().bare(),user->local()->jid().bare(),
	String::boolText(user->subscription().to())));
}

void YJBPresence::notifyNewUser(XMPPUser* user)
{
    if (!user)
	return;
    DDebug(this,DebugAll,"notifyNewUser local=%s remote=%s. Adding default resource [%p]",
	user->local()->jid().bare().c_str(),user->jid().bare().c_str(),this);
    // Add local resource
    user->addLocalRes(new JIDResource(s_jabber->defaultResource(),JIDResource::Available,
	JIDResource::CapAudio));
}

Message* YJBPresence::message(int presence, const char* from, const char* to,
	const char* subscription)
{
    Message* m = 0;
    const char* status = 0;
    const char* operation = 0;
    switch (presence) {
	case JBPresence::None:
	    m = new Message("resource.notify");
	    status = "online";
	    break;
	case JBPresence::Unavailable:
	    m = new Message("resource.notify");
	    status = "offline";
	    break;
	case JBPresence::Subscribed:
	    m = new Message("resource.notify");
	    status = "subscribed";
	    break;
	case JBPresence::Unsubscribed:
	    m = new Message("resource.notify");
	    status = "unsubscribed";
	    break;
	case JBPresence::Probe:
	    m = new Message("resource.notify");
	    operation = "probe";
	    break;
	case JBPresence::Subscribe:
	    m = new Message("resource.subscribe");
	    operation = "subscribe";
	    break;
	case JBPresence::Unsubscribe:
	    m = new Message("resource.subscribe");
	    operation = "unsubscribe";
	    break;
	default:
	    m = new Message("resource.notify");
    }
    m->addParam("module",plugin.name());
    m->addParam("protocol",plugin.defProtoName());
    m->addParam("to",to);
    addValidParam(*m,"from",from);
    addValidParam(*m,"operation",operation);
    addValidParam(*m,"subscription",subscription);
    addValidParam(*m,"status",status);
    return m;
}


/**
 * YJBIqService
 */
void YJBIqService::initialize()
{
    debugChain(&plugin);
}

// Process events
bool YJBIqService::accept(JBEvent* event, bool& processed, bool& insert)
{
    if (!(event && event->element()))
	return false;

    processed = (event->element()->type() == XMLElement::Iq);
    if (!processed) {
	// Don't show the debug if it's a WriteFail event: this event may
	//  carry any failed stanza
	if (event->type() != JBEvent::WriteFail)
	    Debug(this,DebugStub,"Can't accept unexpected event=%s [%p]",
		event->name(),this);
	return false;
    }

    bool incoming = (event->type() != JBEvent::WriteFail);
    Message* m = new Message("xmpp.iq");
    m->addParam("module",plugin.name());
    if (event->stream())
	m->addParam("account",event->stream()->name());
    const JabberID* from = &(event->from());
    const JabberID* to = &(event->to());
    // Received stanza: get source/destination JID from stream if missing
    if (incoming) {
	if (to->null() && event->stream())
	    to = &(event->stream()->local());
	if (from->null() && event->stream())
	    from = &(event->stream()->remote());
    }
    addValidParam(*m,"from",*from);
    addValidParam(*m,"to",*to);
    m->addParam("type",event->stanzaType());
    addValidParam(*m,"id",event->id());
    addValidParam(*m,"username",from->node());
    if (!to->null())
	m->addParam("calleduri",BUILD_XMPP_URI(*to));
    if (!incoming)
	m->addParam("failure",String::boolText(true));
    XMLElement* xml = event->releaseXML();
    XMLElement* child = xml->findFirstChild();
    m->addParam(new NamedPointer("xml",xml,child?child->name():0));
    TelEngine::destruct(child);
    Engine::enqueue(m);
    return true;
}


/**
 * YJGData
 */
// Init data and format list
YJGData::YJGData(YJGConnection* conn, Message* msg)
    : m_conn(conn),
    m_mediaReady(false),
    m_transportReady(false),
    m_started(false),
    m_remote(0)
{
    // Set data members
    name = "rtp";
    protocol = "udp";
    type = "local";
    network = "0";
    preference = "1";
    generation = "0";
    plugin.createAuthRandomString(username);
    plugin.createAuthRandomString(password);
    // Get formats from message. Fill with all supported if none
    String f = msg ? msg->getValue("formats") : 0;
    if (!f)
	s_usedCodecs.createList(f,true);
    ObjList* formats = f.split(',');
    // Create the formats list. Validate formats against the used codecs list
    for (ObjList* o = formats->skipNull(); o; o = o->skipNext()) {
	String* format = static_cast<String*>(o->get());
	JGAudio* a = s_usedCodecs.findSynonym(*format);
	if (a)
	    ObjList::append(new JGAudio(*a));
    }
    TelEngine::destruct(formats);
    // Not outgoing: Ready
    if (m_conn->isIncoming())
	return;
    //TODO: Get transport data from message if RTP forward
}

YJGData::~YJGData()
{
    TelEngine::destruct(m_remote);
}

// Reserve RTP address and port or start the RTP session
bool YJGData::rtp(bool start)
{
    if (start) {
	if (m_started || !(m_mediaReady && m_transportReady))
	    return false;
    }
    else if (m_started)
	return false;

    Debug(m_conn,DebugInfo,"%s RTP local='%s:%s' remote='%s:%s' [%p]",
	start ? "Starting" : "Initializing",address.c_str(),port.c_str(),
	m_remote?m_remote->address.c_str():"",m_remote?m_remote->port.c_str():"",
	m_conn);

    Message m("chan.rtp");
    m.userData(static_cast<CallEndpoint*>(m_conn));
    m_conn->complete(m);
    m.addParam("direction","bidir");
    m.addParam("media","audio");
    m.addParam("getsession","true");
    if (start) {
	ObjList* obj = JGAudioList::skipNull();
	if (obj)
	    m.addParam("format",(static_cast<JGAudio*>(obj->get()))->synonym);
	m.addParam("localip",address);
	m.addParam("localport",port);
	m.addParam("remoteip",m_remote->address);
	m.addParam("remoteport",m_remote->port);
	//m.addParam("autoaddr","false");
	m.addParam("rtcp","false");
    }
    else {
	m.addParam("anyssrc","true");
	if (s_localAddress) {
	    address = s_localAddress;
	    m.addParam("localip",address);
	}
	else {
	    String remote;
	    m_conn->getRemoteAddr(remote);
	    if (remote)
		m.addParam("remoteip",remote);
	}
    }

    if (!Engine::dispatch(m)) {
	Debug(m_conn,DebugNote,"Failed to %s RTP [%p]",
	    start?"start":"initialize",m_conn);
	return false;
    }

    if (start) {
	// Start STUN
	Message* msg = new Message("socket.stun");
	msg->userData(m.userData());
	msg->addParam("localusername",m_remote->username + username);
	msg->addParam("remoteusername",username + m_remote->username);
	msg->addParam("remoteip",m_remote->address);
	msg->addParam("remoteport",m_remote->port);
	msg->addParam("userid",m.getValue("rtpid"));
	Engine::enqueue(msg);
	m_started = true;
    }
    else {
	address = m.getValue("localip",address);
	port = m.getValue("localport","-1");
    }
    return true;
}

// Update media from received data. Return false if already updated media or failed to negotiate a format
// Hangup the connection if failed to negotiate audio formats
bool YJGData::updateMedia(JGAudioList& media)
{
    if (m_mediaReady)
	return false;
    // Check if we received any media
    if (!media.skipNull()) {
	Debug(m_conn,DebugNote,"Remote party has no media [%p]",m_conn);
	m_conn->hangup(false,"nomedia");
	return false;
    }

    // Fill a string with our capabilities for debug purposes
    String caps;
    if (m_conn->debugAt(DebugNote))
	JGAudioList::createList(caps,false);

    ListIterator iter(*(JGAudioList*)this);
    for (GenObject* go; (go = iter.get());) {
	JGAudio* local = static_cast<JGAudio*>(go);
	// Check if incoming media contains local media (compare 'id' and 'name')
	ObjList* o = media.skipNull();
	for (; o; o = o->skipNext()) {
	    JGAudio* remote = static_cast<JGAudio*>(o->get());
	    if (local->id == remote->id && local->name == remote->name)
		break;
	}
	// obj is 0. Current element from m_formats is not in received media. Remove it
	if (!o)
	    JGAudioList::remove(local,true);
    }

    // Check if both parties have common media
    if (!skipNull()) {
	if (m_conn->debugAt(DebugNote)) {
	    String recvCaps;
	    media.createList(recvCaps,false);
	    Debug(m_conn,DebugNote,"No common format(s) local=%s remote=%s [%p]",
		caps.c_str(),recvCaps.c_str(),m_conn);
	}
	m_conn->hangup(false,"nomedia");
	return false;
    }
    m_mediaReady = true;
    if (m_conn->debugAt(DebugAll)) {
	createList(caps,true);
	Debug(m_conn,DebugAll,"Media is ready: %s [%p]",caps.c_str(),m_conn);
    }
    return true;
}

// Check received transports and try to accept one if not already negotiated one
bool YJGData::updateTransport(ObjList& transport)
{
    if (m_transportReady)
	return false;
    JGTransport* remote = 0;
    // Find a transport we'd love to use
    for (ObjList* o = transport.skipNull(); o; o = o->skipNext()) {
	remote = static_cast<JGTransport*>(o->get());
	// Check: generation, name, protocol, type, network
	if (generation == remote->generation &&
	    name == remote->name &&
	    protocol == remote->protocol &&
	    type == remote->type)
	    break;
	// We hate it: reset and skip
	DDebug(m_conn,DebugInfo,
	    "Skipping transport name=%s protocol=%s type=%s generation=%s [%p]",
	    remote->name.c_str(),remote->protocol.c_str(),
	    remote->type.c_str(),remote->generation.c_str(),m_conn);
	remote = 0;
    }
    if (!remote)
	return false;
    // Ok: keep it !
    TelEngine::destruct(m_remote);
    transport.remove(remote,false);
    m_remote = remote;
    m_transportReady = true;
    Debug(m_conn,DebugAll,"Transport is ready: local='%s:%s' remote='%s:%s' [%p]",
	address.c_str(),port.c_str(),m_remote->address.c_str(),
	m_remote->port.c_str(),m_conn);
    return true;
}


/**
 * YJGConnection
 */
// Outgoing call
YJGConnection::YJGConnection(Message& msg, const char* caller, const char* called,
	bool available)
    : Channel(&plugin,0,true),
    m_mutex(true),
    m_state(Pending),
    m_session(0),
    m_local(caller),
    m_remote(called),
    m_callerPrompt(msg.getValue("callerprompt")),
    m_data(0),
    m_hangup(false),
    m_timeout(0)
{
    Debug(this,DebugCall,"Outgoing. caller='%s' called='%s' [%p]",caller,called,this);
    // Init transport
    m_data = new YJGData(this,&msg);
    // Set timeout and maxcall
    int tout = msg.getIntValue("timeout",-1);
    if (tout > 0)
	timeout(Time::now() + tout*(u_int64_t)1000);
    else if (tout == 0)
	timeout(0);
    m_timeout = msg.getIntValue("maxcall",0) * (u_int64_t)1000;
    u_int64_t pendingTimeout = s_pendingTimeout * (u_int64_t)1000;
    u_int64_t timenow = Time::now();
    if (m_timeout && pendingTimeout >= m_timeout) {
	maxcall(timenow + m_timeout);
	m_timeout = 1;
    }
    else {
	maxcall(timenow + pendingTimeout);
	if (m_timeout)
	    m_timeout += timenow - pendingTimeout;
    }
    XDebug(this,DebugInfo,"Time: " FMT64 ". Maxcall set to " FMT64 " us. [%p]",
	Time::now(),maxcall(),this);
    // Startup
    Message* m = message("chan.startup",msg);
    m->setParam("direction",status());
    m_targetid = msg.getValue("id");
    m->setParam("caller",msg.getValue("caller"));
    m->setParam("called",msg.getValue("called"));
    m->setParam("billid",msg.getValue("billid"));
    Engine::enqueue(m);
    // Make the call
    if (available)
	presenceChanged(true);
}

// Incoming call
YJGConnection::YJGConnection(JGEvent* event)
    : Channel(&plugin,0,false),
    m_mutex(true),
    m_state(Active),
    m_session(event->session()),
    m_local(event->session()->local()),
    m_remote(event->session()->remote()),
    m_data(0),
    m_hangup(false),
    m_timeout(0)
{
    Debug(this,DebugCall,"Incoming. caller='%s' called='%s' [%p]",
	m_remote.c_str(),m_local.c_str(),this);
    // Set session
    m_session->userData(this);
    // Init transport
    m_data = new YJGData(this);
    if (!m_data->updateMedia(event->audio()))
	m_state = Pending;
    m_data->updateTransport(event->transport());
    // Startup
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    m->setParam("caller",m_remote.bare());
    m->setParam("called",m_local.node());
    Engine::enqueue(m);
}

// Release data
YJGConnection::~YJGConnection()
{
    hangup(false);
    disconnected(true,m_reason);
    TelEngine::destruct((RefObject*)m_data);
    Debug(this,DebugCall,"Destroyed [%p]",this);
}

// Route an incoming call
bool YJGConnection::route()
{
    Message* m = message("call.preroute",false,true);
    m->addParam("username",m_remote.node());
    m->addParam("called",m_local.node());
    m->addParam("calleduri",BUILD_XMPP_URI(m_local));
    m->addParam("caller",m_remote.node());
    m->addParam("callername",m_remote.bare());
    m_mutex.lock();
    if (m_data->m_remote) {
	m->addParam("ip_host",m_data->m_remote->address);
	m->addParam("ip_port",m_data->m_remote->port);
    }
    m_mutex.unlock();
    return startRouter(m);
}

// Call accepted
// Init RTP. Accept session and transport. Send transport
void YJGConnection::callAccept(Message& msg)
{
    Debug(this,DebugCall,"callAccept [%p]",this);
    m_mutex.lock();
    if (m_session) {
	m_data->rtp(false);
	m_session->accept(m_data->JGAudioList::toXML());
	m_session->acceptTransport();
	m_session->sendTransport(new JGTransport(*m_data));
    }
    m_mutex.unlock();
    Channel::callAccept(msg);
}

void YJGConnection::callRejected(const char* error, const char* reason,
	const Message* msg)
{
    Debug(this,DebugCall,"callRejected. error=%s reason=%s [%p]",error,reason,this);
    hangup(false,error?error:reason);
    Channel::callRejected(error,reason,msg);
}

bool YJGConnection::callRouted(Message& msg)
{
    DDebug(this,DebugCall,"callRouted [%p]",this);
    return Channel::callRouted(msg);
}

void YJGConnection::disconnected(bool final, const char* reason)
{
    Debug(this,DebugCall,"disconnected. final=%u reason=%s [%p]",
	final,reason,this);
    setReason(reason);
    Channel::disconnected(final,m_reason);
}

bool YJGConnection::msgAnswered(Message& msg)
{
    Debug(this,DebugCall,"msgAnswered [%p]",this);
    return Channel::msgAnswered(msg);
}

bool YJGConnection::msgUpdate(Message& msg)
{
    DDebug(this,DebugCall,"msgUpdate [%p]",this);
    return Channel::msgUpdate(msg);
}

// Send message to remote peer
bool YJGConnection::msgText(Message& msg, const char* text)
{
    DDebug(this,DebugCall,"msgText. '%s' [%p]",text,this);
    Lock lock(m_mutex);
    if (m_session) {
	m_session->sendMessage(text);
	return true;
    }
    return false;
}

// Hangup
bool YJGConnection::msgDrop(Message& msg, const char* reason)
{
    DDebug(this,DebugCall,"msgDrop('%s') [%p]",reason,this);
    setReason(reason?reason:"dropped");
    if (!Channel::msgDrop(msg,m_reason))
	return false;
    hangup(false);
    return true;
}

// Send tones to remote peer
bool YJGConnection::msgTone(Message& msg, const char* tone)
{
    DDebug(this,DebugCall,"msgTone. '%s' [%p]",tone,this);
    if (!(tone && *tone))
	return true;
    Lock lock(m_mutex);
    if (!m_session)
	return true;
    if (plugin.singleTone()) {
	char s[2] = {0,0};
	while (*tone) {
	    s[0] = *tone++;
	    m_session->sendDtmf(s);
	}
    }
    else
	m_session->sendDtmf(tone);
    return true;
}

// Hangup the call. Send session terminate if not already done
void YJGConnection::hangup(bool reject, const char* reason)
{
    Lock lock(m_mutex);
    if (m_hangup)
	return;
    m_hangup = true;
    m_state = Terminated;
    setReason(reason?reason:(Engine::exiting()?"shutdown":"hangup"));
    Message* m = message("chan.hangup",true);
    m->setParam("status","hangup");
    m->setParam("reason",m_reason);
    Engine::enqueue(m);
    if (m_session) {
	m_session->userData(0);
	m_session->hangup(reject,m_reason);
	TelEngine::destruct(m_session);
    }
    Debug(this,DebugCall,"Hangup. reason=%s [%p]",m_reason.c_str(),this);
}

// Handle Jingle events
void YJGConnection::handleEvent(JGEvent* event)
{
    if (!event)
	return;
    Lock lock(m_mutex);
    if (m_hangup) {
	Debug(this,DebugInfo,"Ignoring event (%p,%u). Already hung up [%p]",
	    event,event->type(),this);
	return;
    }

    if (event->type() == JGEvent::Terminated) {
	Debug(this,DebugInfo,"Remote terminated with reason='%s' [%p]",
	    event->reason().c_str(),this);
	setReason(event->reason());
	return;
    }

    if (event->type() != JGEvent::Jingle) {
	Debug(this,DebugNote,"Received unexpected event (%p,%u) [%p]",
	    event,event->type(),this);
	return;
    }

    // Process jingle events
    switch (event->action()) {
	case JGSession::ActDtmf:
	    Debug(this,DebugInfo,"Received dtmf(%s) '%s' [%p]",
		event->reason().c_str(),event->text().c_str(),this);
	    if (event->reason() == "button-up" && event->text()) {
		Message* m = message("chan.dtmf");
		m->addParam("text",event->text());
		m->addParam("detected","jingle");
		dtmfEnqueue(m);
	    }
	    break;
	case JGSession::ActDtmfMethod:
	    Debug(this,DebugAll,"Received dtmf method='%s' [%p]",
		event->text().c_str(),this);
	    // Method can be 'rtp' or 'xmpp': accept both
	    m_session->confirm(event->element());
	    break;
	case JGSession::ActTransport:
	    if (m_data->m_transportReady) {
		Debug(this,DebugAll,"Received transport while ready [%p]",this);
		m_session->confirm(event->releaseXML(),XMPPError::SNotAcceptable,
		    0,XMPPError::TypeCancel);
		break;
	    }
	    m_data->updateTransport(event->transport());
	    if (m_data->m_transportReady) {
		m_session->confirm(event->element());
		if (isOutgoing())
		    m_session->acceptTransport();
		m_data->rtp(true);
	    }
	    else
		m_session->confirm(event->releaseXML(),XMPPError::SNotAcceptable);
	    break;
	case JGSession::ActTransportAccept:
	    Debug(this,DebugAll,"Remote peer accepted transport [%p]",this);
	    break;
	case JGSession::ActAccept:
	    if (isAnswered())
		break;
	    // Update media
	    Debug(this,DebugCall,"Remote peer answered the call [%p]",this);
	    m_state = Active;
	    m_data->updateMedia(event->audio());
	    m_data->rtp(true);
	    maxcall(0);
	    status("answered");
	    Engine::enqueue(message("call.answered",false,true));
	    break;
	default:
	    Debug(this,DebugNote,
		"Received unexpected Jingle event (%p) with action=%u [%p]",
		event,event->action(),this);
    }
}

// Process remote user's presence notifications
// Make the call if outgoing and in Pending (waiting for presence information) state
// Hangup if the remote user is unavailbale
// Return true to disconnect
bool YJGConnection::presenceChanged(bool available)
{
    Lock lock(m_mutex);
    if (m_state == Terminated)
	return false;
    // Check if unavailable in any other states
    if (!available) {
	if (!m_hangup) {
	    DDebug(this,DebugCall,"Remote user is unavailable [%p]",this);
	    hangup(false,"offline");
	}
	return true;
    }
    // Check if we are in pending state and remote peer is present
    if (!(isOutgoing() && m_state == Pending && available))
	return false;
    // Make the call
    Debug(this,DebugCall,"Calling. caller=%s called=%s [%p]",
	m_local.c_str(),m_remote.c_str(),this);
    m_state = Active;
    m_session = s_jingle->call(m_local,m_remote,m_data->JGAudioList::toXML(),
	JGTransport::createTransport(),m_callerPrompt);
    if (!m_session) {
	hangup(false,"noconn");
	return true;
    }
    m_session->userData(this);
    maxcall(m_timeout);
    Engine::enqueue(message("call.ringing",false,true));
    // Init & send transport
    m_data->rtp(false);
    m_session->sendTransport(new JGTransport(*m_data));
    return false;
}

/**
 * resource.notify message handler
 */
bool ResNotifyHandler::received(Message& msg)
{
    // Avoid loopback message (if the same module: it's a message sent by this module)
    if (plugin.name() == msg.getValue("module"))
	return false;

    // Check status
    NamedString* status = msg.getParam("status");
    if (!status || status->null())
	return false;

    if (s_jabber && s_jabber->protocol() == JBEngine::Client) {
	NamedString* account = msg.getParam("account");
	if (!account || account->null())
	    return false;
	JBClientStream* stream = static_cast<JBClientStream*>(s_jabber->findStream(*account));
	if (!stream)
	    return false;
	const char* to = msg.getValue("to");
	XDebug(&plugin,DebugAll,"%s account=%s to=%s status=%s",
	    account->c_str(),to,status->c_str());
	XMLElement* pres = 0;
	bool ok = (*status == "subscribed");
	if (ok || *status == "unsubscribed")
	    pres = JBPresence::createPresence(stream->local().bare(),to,
		ok?JBPresence::Subscribed:JBPresence::Unsubscribed);
	else {
	    Lock lock(stream->streamMutex());
	    JIDResource* res = stream->getResource();
	    if (res && res->ref()) {
		if (*status == "online")
		    res->setPresence(true);
		else if (*status == "offline")
		    res->setPresence(false);
		else
		    res->status(*status);
		pres = JBPresence::createPresence(stream->local().bare(),to,
		   res->available()?JBPresence::None:JBPresence::Unavailable);
		res->addTo(pres,true);
		TelEngine::destruct(res);
	    }
	}
	ok = false;
	if (pres) {
	    JBStream::Error err = stream->sendStanza(pres);
	    ok = (err == JBStream::ErrorNone) || (err == JBStream::ErrorPending);
	}
	TelEngine::destruct(stream);
	return ok;
    }

    if (!s_presence)
	return false;

    JabberID from,to;
    // *** Check from/to
    if (!plugin.getJidFrom(from,msg,true))
	return false;
    if (!s_presence->autoRoster())
	to = msg.getValue("to");
    else if (!plugin.decodeJid(to,msg,"to"))
	return false;
    // *** Everything is OK. Process the message
    XDebug(&plugin,DebugAll,"Received '%s' from '%s' with status '%s'",
	msg.c_str(),from.c_str(),status->c_str());
    if (s_presence->addOnPresence().to() || s_presence->addOnSubscribe().to())
	process(from,to,*status,msg.getBoolValue("subscription",false),&msg);
    else
	sendPresence(from,to,*status,&msg);
    return true;
}

void ResNotifyHandler::process(const JabberID& from, const JabberID& to,
	const String& status, bool subFrom, NamedList* params)
{
    if (!s_presence)
	return;
    DDebug(&plugin,DebugAll,"ResNotifyHandler::process() from=%s to=%s status=%s",
	from.c_str(),to.c_str(),status.c_str());

    bool pres = (status != "subscribed") && (status != "unsubscribed");
    bool add = pres ? s_presence->addOnPresence().to() : s_presence->addOnSubscribe().to();
    XMPPUserRoster* roster = s_presence->getRoster(from,add,0);
    if (!roster)
	return;
    XMPPUser* user = roster->getUser(to,false,0);

    bool newUser = (0 == user);
    // Add new user
    if (newUser) {
	user = new XMPPUser(roster,to.node(),to.domain(),
	    subFrom ? XMPPDirVal::From : XMPPDirVal::None,false,false);
	if (!user->ref())
	    user = 0;
    }
    TelEngine::destruct(roster);
    if (!user)
	return;
    Lock lock(user);
    // Process
    for (;;) {
	// Subscription response
	if (!pres) {
	    if (status == "subscribed") {
		// Send only if not already subscribed to us
		if (!user->subscription().from())
		    user->sendSubscribe(JBPresence::Subscribed,0);
		break;
	    }
	    if (status == "unsubscribed") {
		// Send only if not already unsubscribed from us
		if (user->subscription().from())
		    user->sendSubscribe(JBPresence::Unsubscribed,0);
		break;
	    }
	    break;
	}

	// Presence
	JIDResource::Presence p = (status != "offline") ?
	    JIDResource::Available : JIDResource::Unavailable;
	const char* name = from.resource();
	if (!name)
	    name = s_jabber->defaultResource();
	JIDResource* res = 0;
	bool changed = false;
	if (name) {
	    changed = user->addLocalRes(new JIDResource(name,p,JIDResource::CapAudio),false);
	    res = user->localRes().get(name);
	}
	else
	    res = user->getAudio(true,true);
	if (!res) {
	    DDebug(&plugin,DebugNote,
		"ResNotifyHandler::process() from=%s to=%s status=%s: no resource named '%s'",
		from.c_str(),to.c_str(),status.c_str(),name);
	    break;
	}
	res->infoXml()->clear();
	plugin.addChildren(*params,0,res->infoXml());
	if (p == JIDResource::Unavailable)
	    changed = res->setPresence(false) || changed;
	else {
	    changed = res->setPresence(true) || changed;
	    if (status == "online") {
		if (!res->status().null()) {
		    res->status("");
		    changed = true;
		}
	    }
	    else {
		if (status != res->status()) {
		    res->status(status);
		    changed = true;
		}
	    }
	}

	if (changed && user->subscription().from())
	    user->sendPresence(res,0,true);
	// Remove if unavailable
	if (!res->available())
	    user->removeLocalRes(res);
	break;
    }
    lock.drop();
    TelEngine::destruct(user);
}

void ResNotifyHandler::sendPresence(JabberID& from, JabberID& to,
	const String& status, NamedList* params)
{
    if (!s_presence)
	return;
    JBPresence::Presence jbPresence;
    // Get presence type from status
    if (status == "online")
	jbPresence = JBPresence::None;
    else if (status == "offline")
	jbPresence = JBPresence::Unavailable;
    else {
	if (!s_presence->autoRoster()) {
	    XDebug(&plugin,DebugNote,"Can't send command for status='%s'",status.c_str());
	    return;
	}
	if (status == "subscribed")
	    jbPresence = JBPresence::Subscribed;
	else if (status == "unsubscribed") 
	    jbPresence = JBPresence::Unsubscribed;
	else {
	    XDebug(&plugin,DebugNote,"Can't send presence for status='%s'",status.c_str());
	    return;
	}
    }
    // Check if we can get a stream
    JBStream* stream = s_jabber->getStream();
    if (!stream)
	return;
    // Create XML element to be sent
    bool available = (jbPresence == JBPresence::None);
    XMLElement* stanza = 0;
    if (!s_presence->autoRoster()) {
	if (to.domain().null())
	    to.domain(s_jabber->componentServer().c_str());
	DDebug(&plugin,DebugAll,"Sending presence %s from: %s to: %s",
	    String::boolText(available),from.c_str(),to.c_str());
	stanza = plugin.getPresenceCommand(from,to,available);
    }
    else {
	stanza = JBPresence::createPresence(from,to,jbPresence);
	// Create resource info if available
	if (available) {
	    JIDResource* resource = new JIDResource(from.resource(),JIDResource::Available,
		JIDResource::CapAudio);
	    resource->addTo(stanza);
	    TelEngine::destruct(resource);
	}
    }
    if (stanza && params)
	plugin.addChildren(*params,stanza);
    // Send
    stream->sendStanza(stanza);
    TelEngine::destruct(stream);
}

/**
 * resource.subscribe message handler
 */
bool ResSubscribeHandler::received(Message& msg)
{
    // Avoid loopback message (if the same module: it's a message sent by this module)
    if (plugin.name() == msg.getValue("module"))
	return false;

    // Check operation
    NamedString* oper = msg.getParam("operation");
    if (!oper)
	return false;
    JBPresence::Presence presence;
    if (*oper == "subscribe")
	presence = JBPresence::Subscribe;
    else if (*oper == "probe")
	presence = JBPresence::Probe;
    else if (*oper == "unsubscribe")
	presence = JBPresence::Unsubscribe;
    else
	return false;

    XMLElement* pres = 0;
    JBStream* stream = 0;
    bool ok = false;
    while (true) {
	// Client stream
	NamedString* account = msg.getParam("account");
	if (account) {
	    stream = s_jabber->findStream(*account);
	    if (stream) {
		XDebug(&plugin,DebugAll,"%s account=%s to=%s operation=%s",
		    account->c_str(),msg.getValue("to"),oper->c_str());
		pres = JBPresence::createPresence(stream->local(),
		    msg.getValue("to"),presence);
		break;
	    }
	}

	// Component stream
	if (!s_presence || s_jabber->protocol() == JBEngine::Client)
	    break;
	JabberID from,to;
	// Check from/to
	if (!plugin.decodeJid(from,msg,"from",true))
	    break;
	if (!plugin.decodeJid(to,msg,"to"))
	    break;
	XDebug(&plugin,DebugAll,"%s from=%s to=%s operation=%s",
	    from.c_str(),to.c_str(),oper->c_str());
	// Don't automatically add
	if ((presence == JBPresence::Probe && !s_presence->addOnProbe().to()) ||
	    ((presence == JBPresence::Subscribe || presence == JBPresence::Unsubscribe) &&
	    !s_presence->addOnSubscribe().to())) {
	    stream = s_jabber->getStream();
	    if (stream)
		pres = JBPresence::createPresence(from,to,presence);
	    break;
	}
	// Add roster/user
	XMPPUserRoster* roster = s_presence->getRoster(from,true,0);
	XMPPUser* user = roster->getUser(to,false,0);
	// Add new user and local resource
	if (!user) {
	    user = new XMPPUser(roster,to.node(),to.domain(),XMPPDirVal::From,
		false,false);
	    s_presence->notifyNewUser(user);
	    if (!user->ref()) {
		TelEngine::destruct(roster);
		break;
	    }
	}
	TelEngine::destruct(roster);
	// Process
	ok = true;
	user->lock();
	for (;;) {
	    if (presence == JBPresence::Subscribe ||
		presence == JBPresence::Unsubscribe) {
		bool sub = (presence == JBPresence::Subscribe);
		// Already (un)subscribed: notify. NO: send request
		if (sub != user->subscription().to()) {
		    user->sendSubscribe(presence,0);
		    user->probe(0);
		}
		else
		    s_presence->notifySubscribe(user,sub?JBPresence::Subscribed:JBPresence::Unsubscribed);
		break;
	    }
	    // Respond if user has a resource with audio capabilities
	    JIDResource* res = user->getAudio(false,true);
	    if (res) {
		user->notifyResource(true,res->name());
		break;
	    }
	    // No audio resource for remote user: send probe
	    // Send probe fails: Assume remote user unavailable
	    if (!user->probe(0)) {
		XMLElement* xml = JBPresence::createPresence(to,from,JBPresence::Unavailable);
		JBEvent* event = new JBEvent(JBEvent::Presence,0,xml);
		s_presence->notifyPresence(event,false);
		TelEngine::destruct(event);
	    }
	    break;
	}
	user->unlock();
	TelEngine::destruct(user);
	break;
    }

    if (stream && !ok) {
	JBStream::Error err = stream->sendStanza(pres);
	pres = 0;
	ok = (err == JBStream::ErrorNone) || (err == JBStream::ErrorPending);
    }
    TelEngine::destruct(stream);
    TelEngine::destruct(pres);
    return ok;
}

/**
 * UserLoginHandler
 */
bool UserLoginHandler::received(Message& msg)
{
    if (!(s_jabber && s_jabber->protocol() == JBEngine::Client))
	return false;
    if (!plugin.canHandleProtocol(msg.getValue("protocol")))
	return false;
    NamedString* account = msg.getParam("account");
    if (!account || account->null())
	return false;
    // Check operation
    NamedString* oper = msg.getParam("operation");
    bool login = !oper || *oper == "login" || *oper == "create";
    if (!login && (!oper || (*oper != "logout" && *oper != "delete")))
	return false;

    Debug(&plugin,DebugAll,"user.login for account=%s operation=%s",
	account->c_str(),oper?oper->c_str():"");

    JBClientStream* stream = static_cast<JBClientStream*>(s_jabber->findStream(*account));
    bool ok = false;
    if (login) {
	if (!stream)
	    stream = s_jabber->createClientStream(msg);
	else
	    msg.setParam("error","User already logged in");
	ok = (0 != stream);
    }
    else if (stream) {
	if (stream->state() == JBStream::Running) {
	    XMLElement* xml = JBPresence::createPresence(0,0,JBPresence::Unavailable);
	    stream->sendStanza(xml);
	}
	const char* reason = msg.getValue("reason");
	if (!reason)
	    reason = Engine::exiting() ? "" : "Logout";
	XMPPError::Type err = Engine::exiting()?XMPPError::Shutdown:XMPPError::NoError;
	stream->terminate(true,0,err,reason,true);
	ok = true;
    }
    TelEngine::destruct(stream);
    return ok;
}

/**
 * XmppGenerateHandler
 */
bool XmppGenerateHandler::received(Message& msg)
{
    // Process only mesages not enqueued by this module
    if (!s_jabber || plugin.name() == msg.getValue("module"))
	return false;

    // Check protocol only if present
    const char* proto = msg.getValue("protocol");
    if (proto && !plugin.canHandleProtocol(proto))
	return false;

    // Try to get a stream to sent the stanza
    JBStream* stream = 0;
    if (s_jabber->protocol() == JBEngine::Client) {
	NamedString* account = msg.getParam("account");
	if (!account)
	    return false;
	stream = s_jabber->findStream(*account);
    }
    else {
	JabberID f(msg.getValue("from"));
	stream = s_jabber->getStream(f.null()?0:&f);
    }
    if (!stream)
	return false;

    // Get and send stanza
    bool ok = false;
    XMLElement* stanza = XMLElement::getXml(msg,true);
    if (stanza) {
	JBStream::Error res = stream->sendStanza(stanza,msg.getValue("id"));
	ok = (res == JBStream::ErrorNone || res == JBStream::ErrorPending);
    }
    TelEngine::destruct(stream);
    return ok;
}

/**
 * XmppIqHandler
 */
bool XmppIqHandler::received(Message& msg)
{
    // Process only mesages enqueued by this module
    if (plugin.name() != msg.getValue("module"))
	return false;
    // Ignore failed stanzas
    if (msg.getBoolValue("failure"))
	return false;
    // Respond only to type 'set' or 'get'
    NamedString* type = msg.getParam("type");
    if (!type || (*type != "set" && *type != "get"))
	return false;

    NamedString* account = msg.getParam("account");
    const char* from = msg.getValue("from");
    const char* to = msg.getValue("to");
    const char* id = msg.getValue("id");
    Debug(&plugin,DebugAll,"%s: account=%s from=%s to=%s id=%s returned to module",
	msg.c_str(),account?account->c_str():"",from,to,id);

    JBStream* stream = 0;
    if (account)
	stream = s_jabber->findStream(*account);
    else {
	JabberID f(from);
	stream = s_jabber->getStream(f.null()?0:&f);
    }
    if (!stream)
	return false;

    // Don't send error without id or received element:
    //  the sender won't be able to match the response
    XMLElement* recvStanza = XMLElement::getXml(msg,true);
    if (id || recvStanza) {
	XMLElement* stanza = XMPPUtils::createIq(XMPPUtils::IqError,to,from,id);
	stanza->addChild(recvStanza);
	stanza->addChild(XMPPUtils::createError(XMPPError::TypeModify,XMPPError::SFeatureNotImpl));
	stream->sendStanza(stanza);
    }
    else
	TelEngine::destruct(recvStanza);
    TelEngine::destruct(stream);
    // Return true to make sure nobody will respond again!!!
    return true;
}

/**
 * YJGDriver
 */
String YJGDriver::s_statusCmd[StatusCmdCount] = {"streams"};

YJGDriver::YJGDriver()
    : Driver("jingle","varchans"), m_init(false), m_singleTone(true), m_installIq(true)
{
    Output("Loaded module YJingle");
    m_statusCmd << "status " << name();
}

YJGDriver::~YJGDriver()
{
    Output("Unloading module YJingle");
    TelEngine::destruct(s_jingle);
    TelEngine::destruct(s_message);
    TelEngine::destruct(s_presence);
    TelEngine::destruct(s_clientPresence);
    TelEngine::destruct(s_stream);
    TelEngine::destruct(s_iqService);
    TelEngine::destruct(s_jabber);
}

void YJGDriver::initialize()
{
    Output("Initializing module YJingle");
    s_cfg = Engine::configFile("yjinglechan");
    s_cfg.load();
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;

    if (!m_init) {
	m_init = true;

	// Init all known codecs
	s_knownCodecs.add("0",  "PCMU",    "8000",  "", "mulaw");
	s_knownCodecs.add("2",  "G726-32", "8000",  "", "g726");
	s_knownCodecs.add("3",  "GSM",     "8000",  "", "gsm");
	s_knownCodecs.add("4",  "G723",    "8000",  "", "g723");
	s_knownCodecs.add("7",  "LPC",     "8000",  "", "lpc10");
	s_knownCodecs.add("8",  "PCMA",    "8000",  "", "alaw");
	s_knownCodecs.add("9",  "G722",    "8000",  "", "g722");
	s_knownCodecs.add("11", "L16",     "8000",  "", "slin");
	s_knownCodecs.add("15", "G728",    "8000",  "", "g728");
	s_knownCodecs.add("18", "G729",    "8000",  "", "g729");
	s_knownCodecs.add("31", "H261",    "90000", "", "h261");
	s_knownCodecs.add("32", "MPV",     "90000", "", "mpv");
	s_knownCodecs.add("34", "H263",    "90000", "", "h263");
	s_knownCodecs.add("98", "iLBC",    "8000",  "", "ilbc");
	s_knownCodecs.add("98", "iLBC",    "8000",  "", "ilbc20");
	s_knownCodecs.add("98", "iLBC",    "8000",  "", "ilbc30");

	// Jabber protocol to use
	JBEngine::Protocol proto = (Engine::mode() == Engine::Client) ?
	    JBEngine::Client : JBEngine::Component;
	NamedString* p = sect->getParam("protocol");
	if (p)
	    proto = (JBEngine::Protocol)JBEngine::lookupProto(*p,proto);

	if (proto == JBEngine::Client)
	    m_installIq = true;
	else
	    m_installIq = sect->getBoolValue("installiq",true);

	// Create Jabber engine and services
	s_jabber = new YJBEngine(proto);
	s_jingle = new YJGEngine(s_jabber,0);
	s_message = new YJBMessage(s_jabber,1);
	// Create protocol dependent services
	// Don't create presence service for client protocol: presence is kept by client streams
	// Instantiate event handler for messages related to presence when running in client mode
	if (s_jabber->protocol() != JBEngine::Client)
	    s_presence = new YJBPresence(s_jabber,0);
	else {
	    s_clientPresence = new YJBClientPresence(s_jabber,0);
	    s_stream = new YJBStreamService(s_jabber,0);
	}
	if (m_installIq)
	    s_iqService = new YJBIqService(s_jabber,100);

	// Attach services to the engine
	s_jabber->attachService(s_jingle,JBEngine::ServiceJingle);
	s_jabber->attachService(s_jingle,JBEngine::ServiceWriteFail);
	s_jabber->attachService(s_jingle,JBEngine::ServiceIq);
	s_jabber->attachService(s_jingle,JBEngine::ServiceStream);
	s_jabber->attachService(s_message,JBEngine::ServiceMessage);
	if (s_presence) {
	    s_jabber->attachService(s_presence,JBEngine::ServicePresence);
	    s_jabber->attachService(s_presence,JBEngine::ServiceDisco);
	}
	else if (s_clientPresence) {
	    s_jabber->attachService(s_clientPresence,JBEngine::ServicePresence);
	    s_jabber->attachService(s_clientPresence,JBEngine::ServiceRoster);
	}
	if (s_stream)
	    s_jabber->attachService(s_stream,JBEngine::ServiceStream);
	if (s_iqService) {
	    s_jabber->attachService(s_iqService,JBEngine::ServiceIq);
	    s_jabber->attachService(s_iqService,JBEngine::ServiceCommand);
	    s_jabber->attachService(s_iqService,JBEngine::ServiceDisco);
	    s_jabber->attachService(s_iqService,JBEngine::ServiceWriteFail);
	}

	// Driver setup
	installRelay(Halt);
	Engine::install(new ResNotifyHandler);
	Engine::install(new ResSubscribeHandler);
	Engine::install(new XmppGenerateHandler);
	if (s_jabber->protocol() == JBEngine::Client)
	    Engine::install(new UserLoginHandler);
	if (m_installIq)
	    Engine::install(new XmppIqHandler);
	setup();
    }

    lock();

    // Initialize Jabber engine and services
    s_jabber->initialize();
    s_jingle->initialize();
    s_message->initialize();
    if (s_presence)
	s_presence->initialize();
    if (s_stream)
	s_stream->initialize();

    m_singleTone = sect->getBoolValue("singletone",true);
    s_localAddress = sect->getValue("localip");
    s_anonymousCaller = sect->getValue("anonymous_caller","unk_caller");
    s_pendingTimeout = sect->getIntValue("pending_timeout",10000);
    // Init codecs in use. Check each codec in known codecs list against the configuration
    s_usedCodecs.clear();
    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
    for (ObjList* o = s_knownCodecs.skipNull(); o; o = o->skipNext()) {
	JGAudio* crt = static_cast<JGAudio*>(o->get());
	bool enable = defcodecs && DataTranslator::canConvert(crt->synonym);
	if (s_cfg.getBoolValue("codecs",crt->synonym,enable))
	    s_usedCodecs.append(new JGAudio(*crt));
    }

    int dbg = DebugInfo;
    if (!s_localAddress)
	dbg = DebugNote;
    if (!s_usedCodecs.count())
	dbg = DebugWarn;

    if (debugAt(dbg)) {
	String s;
	s << " localip=" << s_localAddress ? s_localAddress.c_str() : "MISSING";
	s << " singletone=" << String::boolText(m_singleTone);
	s << " pending_timeout=" << s_pendingTimeout;
	s << " anonymous_caller=" << s_anonymousCaller;
	String media;
	if (!s_usedCodecs.createList(media,true))
	    media = "MISSING";
	s << " codecs=" << media;
	Debug(this,dbg,"Module initialized:%s",s.c_str());
    }

    unlock();
}

// Make an outgoing calls
// Build peers' JIDs and check if the destination is available
bool YJGDriver::msgExecute(Message& msg, String& dest)
{
    // Construct JIDs
    JabberID caller;
    JabberID called;
    bool available = true;
    String error;
    const char* errStr = "failure";
    while (true) {
	if (!msg.userData()) {
	    error = "No data channel";
	    break;
	}
	// Component: prepare caller/called. check availability
	// Client: just check if caller/called are full JIDs
	if (s_jabber->protocol() == JBEngine::Component) {
	    setComponentCall(caller,called,msg.getValue("caller"),dest,available,error);
	    break;
	}
	// Check if a stream exists. Try to get a resource for caller and/or called
	JBStream* stream = 0;
	NamedString* account = msg.getParam("account");
	if (account)
	    stream = s_jabber->findStream(*account);
	if (stream)
	    caller.set(stream->local().node(),stream->local().domain(),
		stream->local().resource());
	else {
	    caller.set(msg.getValue("caller"));
	    stream = s_jabber->getStream(&caller,false);
	}
	if (!(stream && stream->type() == JBEngine::Client)) {
	    error = "No stream";
	    errStr = "noconn";
	    TelEngine::destruct(stream);
	    break;
	}
	if (!caller.resource()) {
	    Debug(this,DebugAll,"Set resource '%s' for caller '%s'",
		stream->local().resource().c_str(),caller.c_str());
	    caller.resource(stream->local().resource());
	}
	called.set(dest);
	if (!called.resource()) {
	    JBClientStream* client = static_cast<JBClientStream*>(stream);
	    XMPPUser* user = client->getRemote(called);
	    if (user) {
		user->lock();
		JIDResource* res = user->getAudio(false);
		if (res)
		    called.resource(res->name());
		user->unlock();
		TelEngine::destruct(user);
	    }
	}
	if (!(caller.isFull() && called.isFull()))
	    error << "Incomplete caller=" << caller << " or called=" << called;
	TelEngine::destruct(stream);
	break;
    }
    if (error) {
	Debug(this,DebugNote,"Jingle call failed. %s",error.c_str());
	msg.setParam("error",errStr);
	return false;
    }
    // Parameters OK. Create connection and init channel
    Debug(this,DebugAll,"msgExecute. caller='%s' called='%s' available=%u",
	caller.c_str(),called.c_str(),available);
    YJGConnection* conn = new YJGConnection(msg,caller,called,available);
    Channel* ch = static_cast<Channel*>(msg.userData());
    if (ch && conn->connect(ch,msg.getValue("reason"))) {
	conn->callConnect(msg);
	msg.setParam("peerid",conn->id());
	msg.setParam("targetid",conn->id());
    }
    TelEngine::destruct(conn);
    return true;
}

// Handle command complete requests
bool YJGDriver::commandComplete(Message& msg, const String& partLine,
	const String& partWord)
{
    bool status = partLine.startsWith("status");
    bool drop = !status && partLine.startsWith("drop");
    if (!(status || drop))
	return Driver::commandComplete(msg,partLine,partWord);

    // 'status' command
    Lock lock(this);
    // line='status jingle': add additional commands
    if (partLine == m_statusCmd) {
	for (unsigned int i = 0; i < StatusCmdCount; i++)
	    if (!partWord || s_statusCmd[i].startsWith(partWord))
		msg.retValue().append(s_statusCmd[i],"\t");
	return true;
    }

    if (partLine != "status" && partLine != "drop")
	return false;

    // Empty partial word or name start with it: add name and prefix
    if (!partWord || name().startsWith(partWord)) {
	msg.retValue().append(name(),"\t");
	if (channels().skipNull())
	    msg.retValue().append(prefix(),"\t");
	return false;
    }

    // Partial word starts with module prefix: add channels
    if (partWord.startsWith(prefix())) {
	for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	    CallEndpoint* c = static_cast<CallEndpoint*>(o->get());
	    if (c->id().startsWith(partWord))
		msg.retValue().append(c->id(),"\t");
	}
	return true;
    }
    return false;
}

// Check and build caller and called for Component run mode
// Caller: Set user if missing. Get default server identity for Yate Component
// Try to get an available resource for the called party
bool YJGDriver::setComponentCall(JabberID& caller, JabberID& called,
	const char* cr, const char* cd, bool& available, String& error)
{
    // Get identity for default server
    String domain;
    if (!s_jabber->getServerIdentity(domain,true)) {
	error = "No default server";
	return false;
    }
    if (!cr)
	cr = s_anonymousCaller;
    // Validate caller's JID
    if (!(cr && JabberID::valid(cr))) {
	error << "Invalid caller=" << cr;
	return false;
    }
    caller.set(cr,domain);
    called.set(cd);

    // Get an available resource for the remote user if we keep the roster
    // Send subscribe and probe if not
    if (s_presence->autoRoster()) {
	// Get remote user
	bool newPresence = false;
	XMPPUser* remote = s_presence->getRemoteUser(caller,called,true,0,
	    true,&newPresence);
	if (!remote) {
	    error = "Remote user is unavailable";
	    return false;
	}
	// Get a resource for the caller
	JIDResource* res = remote->getAudio(true,true);
	if (!res) {
	    s_presence->notifyNewUser(remote);
	    res = remote->getAudio(true,true);
	    // This should never happen !!!
	    if (!res) {
		TelEngine::destruct(remote);
		error = "Unable to get a resource for the caller";
		return false;
	    }
	}
	caller.resource(res->name());
	// Get a resource for the called
	res = remote->getAudio(false,true);
	available = (res != 0);
	if (!(newPresence || available)) {
	    if (!s_jingle->requestSubscribe()) {
		TelEngine::destruct(remote);
		error = "Remote peer is unavailable";
		return false;
	    }
	    remote->sendSubscribe(JBPresence::Subscribe,0);
	}
	if (available)
	    called.resource(res->name());
	else
	    if (!newPresence)
		remote->probe(0);
	TelEngine::destruct(remote);
    }
    else {
	available = false;
	// Get stream for default component
	JBStream* stream = s_jabber->getStream();
	if (!stream) {
	    error << "No stream for called=" << called;
	    return false;
	}
	// Send subscribe request and probe
	XMLElement* xml = 0;
	if (s_jingle->requestSubscribe()) {
	    xml = JBPresence::createPresence(caller.bare(),called.bare(),JBPresence::Subscribe);
	    stream->sendStanza(xml);
	}
	xml = JBPresence::createPresence(caller.bare(),called.bare(),JBPresence::Probe);
	stream->sendStanza(xml);
	TelEngine::destruct(stream);
    }
    return true;
}

// Message handler: Disconnect channels, destroy streams, clear rosters
bool YJGDriver::received(Message& msg, int id)
{
    // Execute: accept 
    if (id == Execute) {
	while (true) {
	    NamedString* callto = msg.getParam("callto");
	    if (!callto)
		break;
	    int pos = callto->find('/');
	    if (pos < 1)
		break;
	    String dest = callto->substr(0,pos);
	    if (!canHandleProtocol(dest))
		break;
	    dest = callto->substr(pos + 1);
	    return msgExecute(msg,dest);
	}
	return Driver::received(msg,Execute);
    }

    if (id == Status) {
	String target = msg.getValue("module");
	// Target is the driver or channel
	if (!target || target == name() || target.startsWith(prefix()))
	    return Driver::received(msg,id);

	// Check additional commands
	if (!target.startSkip(name(),false))
	    return false;
	target.trimBlanks();
	int cmd = 0;
	for (; cmd < StatusCmdCount; cmd++)
	    if (s_statusCmd[cmd] == target)
		break;

	// Show streams
	if (cmd == StatusStreams && s_jabber) {
	    msg.retValue().clear();
	    msg.retValue() << "name=" << name();
	    msg.retValue() << ",type=" << type();
	    msg.retValue() << ",format=Account|State|Local|Remote";
	    s_jabber->lock();
	    msg.retValue() << ";count=" << s_jabber->streams().count();
	    for (ObjList* o = s_jabber->streams().skipNull(); o; o = o->skipNext()) {
		JBStream* stream = static_cast<JBStream*>(o->get());
		msg.retValue() << ";" << JBEngine::lookupProto(stream->type());
		msg.retValue() << "=" << stream->name();
		msg.retValue() << "|" << JBStream::lookupState(stream->state());
		msg.retValue() << "|" << stream->local();
		msg.retValue() << "|" << stream->remote();
	    }
	    s_jabber->unlock();
	    msg.retValue() << "\r\n";
	    return true;
	}
    }
    else if (id == Halt) {
	dropAll(msg);
	if (s_presence)
	    s_presence->cleanup();
	s_jabber->cleanup();
	s_jabber->cancelThreads();
	s_jingle->cancelThreads();
	if (s_presence)
	    s_presence->cancelThreads();
	s_jabber->detachService(s_presence);
	s_jabber->detachService(s_jingle);
	s_jabber->detachService(s_message);
	s_jabber->detachService(s_stream);
	s_jabber->detachService(s_clientPresence);
	s_jabber->detachService(s_iqService);
    }
    return Driver::received(msg,id);
}

bool YJGDriver::getJidFrom(JabberID& jid, Message& msg, bool checkDomain)
{
    String username = msg.getValue("username");
    if (username.null())
	return decodeJid(jid,msg,"from",checkDomain);
    String domain;
    s_jabber->getServerIdentity(domain,true);
    const char* res = msg.getValue("resource",s_jabber->defaultResource());
    jid.set(username,domain,res);
    return true;
}

bool YJGDriver::decodeJid(JabberID& jid, Message& msg, const char* param,
	bool checkDomain)
{
    jid.set(msg.getValue(param));
    if (jid.node().null() || jid.domain().null()) {
	Debug(this,DebugNote,"'%s'. Parameter '%s'='%s' is an invalid JID",
	    msg.c_str(),param,jid.c_str());
	return false;
    }
    if (checkDomain && !(s_presence && s_presence->validDomain(jid.domain()))) {
	Debug(this,DebugNote,"'%s'. Parameter '%s'='%s' has invalid (unknown) domain",
	    msg.c_str(),param,jid.c_str());
	return false;
    }
    return true;
}

XMLElement* YJGDriver::getPresenceCommand(JabberID& from, JabberID& to,
	bool available)
{
    // Used only for debug purposes
    static int idCrt = 1;
    // Create 'x' child
    XMLElement* x = new XMLElement("x");
    x->setAttribute("xmlns","jabber:x:data");
    x->setAttribute("type","submit");
    // Field children of 'x' element
    XMLElement* field = new XMLElement("field");
    field->setAttribute("var","jid");
    XMLElement* value = new XMLElement("value",0,from);
    field->addChild(value);
    x->addChild(field);
    field = new XMLElement("field");
    field->setAttribute("var","available");
    value = new XMLElement("value",0,available ? "true" : "false");
    field->addChild(value);
    x->addChild(field);
    // 'command' stanza
    XMLElement* command = XMPPUtils::createElement(XMLElement::Command,XMPPNamespace::Command);
    command->setAttribute("node","USER_STATUS");
    command->addChild(x);
    // 'iq' stanza
    String id = idCrt++;
    String domain;
    if (s_jabber->getServerIdentity(domain,false))
	from.domain(domain);
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,from,to,id);
    iq->addChild(command);
    return iq;
}

void YJGDriver::createAuthRandomString(String& dest)
{
    dest = "";
    for (; dest.length() < JINGLE_AUTHSTRINGLEN;)
 	dest << (int)random();
    dest = dest.substr(0,JINGLE_AUTHSTRINGLEN);
}

void YJGDriver::processPresence(const JabberID& local, const JabberID& remote,
	bool available, bool audio)
{
    // Check if it is a brodcast and remote user has a resource
    bool broadcast = local.null();
    bool remoteRes = !remote.resource().null();
    DDebug(this,DebugAll,"Presence (%s). Local: '%s'. Remote: '%s'.",
	available?"available":"unavailable",local.c_str(),remote.c_str());
    // If a remote user became available notify only pending connections
    //   that match local bare jid and remote bare jid
    // No need to notify if remote user has no resource or no audio capability
    if (available) {
	if (!remoteRes || !audio)
	    return;
	lock();
	ObjList* obj = channels().skipNull();
	for (; obj; obj = obj->skipNext()) {
	    YJGConnection* conn = static_cast<YJGConnection*>(obj->get());
	    if (conn->state() != YJGConnection::Pending ||
		(!broadcast && (local.bare() |= conn->local().bare())) ||
		(remote.bare() |= conn->remote().bare()))
		continue;
	    conn->updateResource(remote.resource());
	    if (conn->presenceChanged(true))
		conn->disconnect(0);
	}
	unlock();
	return;
    }
    // Remote user is unavailable: notify all connections
    // Remote has no resource: match connections by bare jid
    lock();
    ObjList* obj = channels().skipNull();
    for (; obj; obj = obj->skipNext()) {
	YJGConnection* conn = static_cast<YJGConnection*>(obj->get());
	if ((!broadcast && local.bare() != conn->local().bare()) ||
	    !conn->remote().match(remote))
	    continue;
	if (conn->presenceChanged(false))
	    conn->disconnect(0);
    }
    unlock();
}

YJGConnection* YJGDriver::find(const JabberID& local, const JabberID& remote, bool anyResource)
{
    String bareJID = local.bare();
    if (bareJID == local)
	anyResource = true;
    Lock lock(this);
    ObjList* obj = channels().skipNull();
    for (; obj; obj = obj->skipNext()) {
	YJGConnection* conn = static_cast<YJGConnection*>(obj->get());
	if (!conn->remote().match(remote))
	    continue;
	if (anyResource) {
	    if (bareJID &= conn->local().bare())
		return conn;
	}
	else if (conn->local().match(local))
	    return conn;
    }
    return 0;
}

// Build an XML element from a received message
bool YJGDriver::addChildren(NamedList& msg, XMLElement* xml, ObjList* list)
{
    String prefix = msg.getValue("message-prefix");
    if (!(prefix && (xml || list)))
	return false;

    prefix << ".";
    bool added = false;
    for (unsigned int i = 1; i < 0xffffffff; i++) {
	String childPrefix(prefix + String(i));
	if (!msg.getValue(childPrefix))
	    break;
	XMLElement* child = new XMLElement(msg,childPrefix);
	if (xml)
	    xml->addChild(child);
	else
	    list->append(child);
	added = true;
    }
    return added;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
