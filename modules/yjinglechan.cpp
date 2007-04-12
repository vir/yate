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

class YJBEngine;                         // Jabber engine
class YJBPresence;                       // Jabber presence engine
class YJGEngine;                         // Jingle engine
class YJGTransport;                      // Handle the transport for a connection
class YJGConnection;                     // Jingle channel
class YJGLibThread;                      // Library thread
class ResNotifyHandler;                  // resource.notify handler
class ResSubscribeHandler;               // resource.subscribe handler
class YJGDriver;                         // The driver

// Yate Payloads
static TokenDict dict_payloads[] = {
    { "mulaw",   0 },
    { "alaw",    8 },
    { "gsm",     3 },
    { "lpc10",   7 },
    { "slin",   11 },
    { "g726",    2 },
    { "g722",    9 },
    { "g723",    4 },
    { "g728",   15 },
    { "g729",   18 },
    { "ilbc",   98 },
    { "ilbc20", 98 },
    { "ilbc30", 98 },
    { "h261",   31 },
    { "h263",   34 },
    { "mpv",    32 },
    {      0,    0 },
};

// Username/Password length for transport
#define JINGLE_AUTHSTRINGLEN         16
// Timeout value to override "maxcall" in call.execute
#define JINGLE_CONN_TIMEOUT       10000
// Default caller if none for outgoing calls
#define JINGLE_ANONYMOUS_CALLER "unk_caller"
// Messages
/* MODULE_MSG_NOTIFY
	protocol	MODULE_NAME
	subscription	true/false: subscription state 'to' --> 'from'
	status		online/offline/subscribed/unsubscribed or any other string
	from		node@domain
	to		node@domain
*/
#define MODULE_MSG_NOTIFY      "resource.notify"
/* MODULE_MSG_SUBSCRIBE
	protocol	MODULE_NAME
	operation	probe/subscribe/unsubscribe
	from		node@domain
	to		node@domain
*/
#define MODULE_MSG_SUBSCRIBE   "resource.subscribe"
// Module name
static const String MODULE_NAME("jingle");

/**
 * YJBEngine
 */
class YJBEngine : public JBEngine
{
public:
    inline YJBEngine() {}
    virtual ~YJBEngine() {}
    // Overloaded methods
    virtual bool connect(JBComponentStream* stream);
    virtual bool exiting()
	{ return Engine::exiting(); }
    // Start thread members
    // @param read Reading socket thread count.
    void startThreads(u_int16_t read);
    // Process a message event.
    // @param event The event to process. Always a valid message event.
    // @return True if the event was processed (kept). False to destroy it.
    virtual bool processMessage(JBEvent* event);
};

/**
 * YJBPresence
 */
class YJBPresence : public JBPresence
{
    friend class YUserPresence;
public:
    YJBPresence(JBEngine* engine, const NamedList& params);
    virtual ~YJBPresence();
    // Overloaded methods
    virtual bool notifyProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote);
    virtual bool notifySubscribe(JBEvent* event, const JabberID& local,
	const JabberID& remote, Presence presence);
    virtual void notifySubscribe(XMPPUser* user, Presence presence);
    virtual bool notifyPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote, bool available);
    virtual void notifyPresence(XMPPUser* user, JIDResource* resource);
    virtual void notifyNewUser(XMPPUser* user);
    // Start thread members
    // @param process Event processor thread count.
    // @param timeout Check user timeout.
    void startThreads(u_int16_t process, u_int16_t timeout);
protected:
    // Create & enqueue a message from received presence parameter.
    // Add status/operation/subscription parameters
    bool message(Presence presence, const char* from, const char* to,
	const char* subscription);
};

/**
 * YJGEngine
 */
class YJGEngine : public JGEngine
{
public:
    inline YJGEngine(YJBEngine* jb, const NamedList& jgParams)
	: JGEngine(jb,jgParams), m_requestSubscribe(true) {}
    virtual ~YJGEngine() {}
    virtual void processEvent(JGEvent* event);
    void requestSubscribe(bool value)
	{ m_requestSubscribe = value; }
    bool requestSubscribe()
	{ return m_requestSubscribe; }
    // Start thread members
    // @param read Reading events from the Jabber engine thread count.
    // @param process Event processor thread count.
    void startThreads(u_int16_t read, u_int16_t process);
private:
    bool m_requestSubscribe;
};

/**
 * YJGTransport
 */
class YJGTransport : public JGTransport, public Mutex
{
public:
    YJGTransport(YJGConnection* connection, Message* msg = 0);
    virtual ~YJGTransport();
    inline const JGTransport* remote() const
	{ return m_remote; }
    inline bool transportReady() const
	{ return m_transportReady; }
    // Init local address/port
    bool initLocal();
    // Update media
    bool updateMedia(ObjList& media);
    // Update transport
    bool updateTransport(ObjList& transport);
    // Start RTP
    bool start();
    // Send transport through the given session
    inline bool send(JGSession* session)
	{ return session->requestTransport(new JGTransport(*this)); }
    // Create a media description element
    XMLElement* createDescription();
    // Create a media string from the list
    void createMediaString(String& dest);
protected:
    bool m_mediaReady;                   // Media ready (updated) flag
    bool m_transportReady;               // Transport ready (both parties) flag
    bool m_started;                      // True if chan.stun already sent
    JGTransport* m_remote;               // The remote transport info
    ObjList m_formats;                   // The media formats
    YJGConnection* m_connection;         // The connection
    RefObject* m_rtpData;
    String m_rtpId;
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
    YJGConnection(YJGEngine* jgEngine, Message& msg, const char* caller,
	const char* called, bool available);
    YJGConnection(YJGEngine* jgEngine, JGEvent* event);
    virtual ~YJGConnection();
    inline State state()
	{ return m_state; }
    inline const JabberID& local() const
	{ return m_local; }
    inline const JabberID& remote() const
	{ return m_remote; }
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    virtual bool callRouted(Message& msg);
    virtual void disconnected(bool final, const char* reason);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgUpdate(Message& msg);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgTone(Message& msg, const char* tone);
    bool route();
    void handleEvent(JGEvent* event);
    void hangup(bool reject, const char* reason = 0);
    // Process presence info
    //@return True to disconnect
    bool processPresence(bool available, const char* error = 0);
    inline void updateResource(const String& resource) {
	    if (!m_remote.resource() && resource)
		m_remote.resource(resource);
	}
    inline void getRemoteAddr(String& dest) {
	if (m_session && m_session->stream())
	    dest = m_session->stream()->remoteAddr().host();
    }
    inline bool disconnect()
	{ return Channel::disconnect(m_reason); }

protected:
    void handleJingle(JGEvent* event);
    void handleError(JGEvent* event);
    void handleTransport(JGEvent* event);
    bool call();
private:
    State m_state;                       // Connection state
    YJGEngine* m_jgEngine;               // Jingle engine
    JGSession* m_session;                // Jingle session for this connection
    JabberID m_local;
    JabberID m_remote;
    String m_callerPrompt;
    YJGTransport* m_transport;           // Transport
    Mutex m_connMutex;                   // Lock transport and session
    // Termination
    bool m_hangup;                       // Hang up flag: True - already hung up
    String m_reason;                     // Hangup reason
    // Timeouts
    u_int32_t m_timeout;                 // Timeout for not answered outgoing connections
};

/**
 * YJGLibThread
 * Thread class for library asynchronous operations
 */
class YJGLibThread : public Thread
{
public:
    // Action to run
    enum Action {
	JBReader,                        // m_jb->runReceive()
	JBConnect,                       // m_jb->connect(m_stream)
	JGReader,                        // m_jg->runReceive()
	JGProcess,                       // m_jg->runProcess()
	JBPresence,                      // m_presence->runProcess()
	JBPresenceTimeout,               // m_presence->runCheckTimeout()
    };
    YJGLibThread(Action action, const char* name = 0,
	Priority prio = Normal);
    YJGLibThread(JBComponentStream* stream, const char* name = 0,
	Priority prio = Normal);
    virtual void run();
protected:
    Action m_action;                     // Action
    JBComponentStream* m_stream;         // The stream if action is JBConnect
};

/**
 * resource.notify message handler
 */
class ResNotifyHandler : public MessageHandler
{
public:
    ResNotifyHandler() : MessageHandler(MODULE_MSG_NOTIFY) {}
    virtual bool received(Message& msg);
    static void process(const JabberID& from, const JabberID& to,
	const String& status, bool subFrom);
    static void sendPresence(JabberID& from, JabberID& to, const String& status);
};

/**
 * resource.subscribe message handler
 */
class ResSubscribeHandler : public MessageHandler
{
public:
    ResSubscribeHandler() : MessageHandler(MODULE_MSG_SUBSCRIBE) {}
    virtual bool received(Message& msg);
    static void process(const JabberID& from, const JabberID& to,
	JBPresence::Presence presence);
};

/**
 * YJGDriver
 */
class YJGDriver : public Driver
{
public:
    YJGDriver();
    virtual ~YJGDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool received(Message& msg, int id);
    inline u_int32_t pendingTimeout()
	{ return m_pendingTimeout; }
    inline const String& anonymousCaller() const
	{ return m_anonymousCaller; }
    // Split 'src' into parts separated by 'sep'.
    // Puts the parts as names in dest if nameFirst is true
    // Puts the parts as values in dest if nameFirst is false
    bool getParts(NamedList& dest, const char* src, const char sep, bool nameFirst);
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
    // Create the presence notification command.
    XMLElement* getPresenceCommand(JabberID& from, JabberID& to, bool available);
    // Create a random string of JINGLE_AUTHSTRINGLEN length
    void createAuthRandomString(String& dest);
    // Process presence. Notify connections
    void processPresence(const JabberID& local, const JabberID& remote,
	bool available, bool audio);
    // Create a media string from a list
    void createMediaString(String& dest, ObjList& formats, char sep);
    // Find a connection by local and remote jid, optionally ignore local resource (always ignore if local has no resource)
    YJGConnection* find(const JabberID& local, const JabberID& remote, bool anyResource = false);

protected:
    void initCodecLists();
    void initJB(const NamedList& sect);
    void initPresence(const NamedList& sect);
    void initJG(const NamedList& sect);
public:
    YJBEngine* m_jb;                     // Jabber component engine
    YJBPresence* m_presence;             // Jabber component presence server
    YJGEngine* m_jg;                     // Jingle engine
    ObjList m_allCodecs;                 // List of all codecs (JGAudio)
    ObjList m_usedCodecs;                // List of used codecs (JGAudio)
    bool m_sendCommandOnNotify;          // resource.notify: Send command if true. Send presence if false
private:
    bool m_init;
    u_int32_t m_pendingTimeout;
    String m_anonymousCaller;
};

/**
 * Local data
 */
static Configuration s_cfg;              // The configuration: yjinglechan.conf
static YJGDriver iplugin;                // The driver
static String s_localAddress;            // The local machine's address

/**
 * YJBEngine
 */
bool YJBEngine::connect(JBComponentStream* stream)
{
    if (!stream)
	return false;
    (new YJGLibThread(stream,"JBConnect thread"))->startup();
    return true;
}

void YJBEngine::startThreads(u_int16_t read)
{
    // Reader(s)
    if (!read)
	Debug(this,DebugWarn,"No reading socket threads(s)!");
    for (; read; read--)
	(new YJGLibThread(YJGLibThread::JBReader,"JBReader thread"))->startup();
}

bool YJBEngine::processMessage(JBEvent* event)
{
    const char* text = 0;
    if (event->element()) {
	XMLElement* body = event->element()->findFirstChild(XMLElement::Body);
	if (body)
	    text = body->getText();
    }
    YJGConnection* conn = iplugin.find(event->to().c_str(),event->from().c_str());
    DDebug(this,DebugInfo,
	"Message '%s' from: '%s' to: '%s' conn=%p",
	text,event->from().c_str(),event->to().c_str(),conn);
    if (conn) {
	Message* m = conn->message("chan.text");
	m->addParam("text",text);
	Engine::enqueue(m);
    }
    return false;
}

/**
 * YJBPresence
 */
YJBPresence::YJBPresence(JBEngine* engine, const NamedList& params)
    : JBPresence(engine,params)
{
}

YJBPresence::~YJBPresence()
{
}

bool YJBPresence::notifyProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,
	"notifyProbe. Local: '%s'. Remote: '%s'.",local.c_str(),remote.c_str());
    // Enqueue message
    return message(JBPresence::Probe,remote.bare(),local.bare(),0);
}

bool YJBPresence::notifySubscribe(JBEvent* event, const JabberID& local,
	const JabberID& remote, Presence presence)
{
    XDebug(this,DebugAll,
	"notifySubscribe(%s). Local: '%s'. Remote: '%s'.",
	presenceText(presence),local.c_str(),remote.c_str());
    // Respond if auto subscribe
    if ((presence == JBPresence::Subscribe || presence == JBPresence::Unsubscribe) &&
	(autoSubscribe() & XMPPUser::From)) {
	if (presence == JBPresence::Subscribe)
	    presence = JBPresence::Subscribed;
	else
	    presence = JBPresence::Unsubscribed;
	XMLElement* xml = createPresence(local.bare(),remote.bare(),presence);
	if (event->stream())
	    event->stream()->sendStanza(xml);
	return true;
    }
    // Enqueue message
    return message(presence,remote.bare(),local.bare(),0);
}

void YJBPresence::notifySubscribe(XMPPUser* user, Presence presence)
{
    if (!user)
	return;
    XDebug(this,DebugAll,
	"notifySubscribe(%s). User: (%p).",presenceText(presence),user);
    // Enqueue message
    message(presence,user->jid().bare(),user->local()->jid().bare(),0);
}

bool YJBPresence::notifyPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote, bool available)
{
    XDebug(this,DebugAll,
	"notifyPresence(%s). Local: '%s'. Remote: '%s'.",
	available?"available":"unavailable",local.c_str(),remote.c_str());
    // Check audio properties and availability for received resource
    bool capAudio = false;
    if (event && event->element()) {
	JIDResource* res = new JIDResource(remote.resource());
	if (res->fromXML(event->element())) {
	    capAudio = res->hasCap(JIDResource::CapAudio);
	    available = res->available();
	}
	res->deref();
    }
    // Notify presence to module
    iplugin.processPresence(local,remote,available,capAudio);
    // Enqueue message
    return message(available ? JBPresence::None : JBPresence::Unavailable,
	remote.bare(),local.bare(),0);
}

void YJBPresence::notifyPresence(XMPPUser* user, JIDResource* resource)
{
    if (!(user && resource))
	return;
    XDebug(this,DebugAll,"notifyPresence. User: (%p). Resource: (%p).",
	user,resource);
    // Notify plugin
    JabberID remote(user->jid().node(),user->jid().domain(),resource->name());
    iplugin.processPresence(user->local()->jid(),remote,resource->available(),
	resource->hasCap(JIDResource::CapAudio));
    // Enqueue message
    message(resource->available() ? JBPresence::None : JBPresence::Unavailable,
	user->jid().bare(),user->local()->jid().bare(),
	String::boolText(user->subscribedTo()));
}

void YJBPresence::notifyNewUser(XMPPUser* user)
{
    if (!user)
	return;
    DDebug(this,DebugInfo,"Added new user '%s' for '%s'.",
	user->jid().c_str(),user->local()->jid().c_str());
    // Add local resource
    user->addLocalRes(new JIDResource(iplugin.m_jb->defaultResource(),JIDResource::Available,
	JIDResource::CapAudio));
}

void YJBPresence::startThreads(u_int16_t process, u_int16_t timeout)
{
    // Process the received events
    if (!process)
	Debug(m_engine,DebugWarn,"No threads(s) to process events!");
    for (; process; process--)
	(new YJGLibThread(YJGLibThread::JBPresence,"JBPresence thread"))->startup();
    // Process timeout
    if (!timeout)
	Debug(m_engine,DebugWarn,"No threads(s) to check user timeout!");
    for (; timeout; timeout--)
	(new YJGLibThread(YJGLibThread::JBPresenceTimeout,"JBPresenceTimeout thread"))->startup();
}

bool YJBPresence::message(Presence presence, const char* from, const char* to,
	const char* subscription)
{
    Message* m = 0;
    const char* type = 0;
    const char* status = 0;
    const char* operation = 0;
    switch (presence) {
	case JBPresence::None:
	    type = MODULE_MSG_NOTIFY;
	    status = "online";
	    break;
	case JBPresence::Unavailable:
	    type = MODULE_MSG_NOTIFY;
	    status = "offline";
	    break;
	case JBPresence::Subscribed:
	    type = MODULE_MSG_NOTIFY;
	    status = "subscribed";
	    break;
	case JBPresence::Unsubscribed:
	    type = MODULE_MSG_NOTIFY;
	    status = "unsubscribed";
	    break;
	case JBPresence::Probe:
	    type = MODULE_MSG_SUBSCRIBE;
	    operation = "probe";
	    break;
	case JBPresence::Subscribe:
	    type = MODULE_MSG_SUBSCRIBE;
	    operation = "subscribe";
	    break;
	case JBPresence::Unsubscribe:
	    type = MODULE_MSG_SUBSCRIBE;
	    operation = "unsubscribe";
	    break;
	default:
	    return 0;
    }
    m = new Message(type);
    m->addParam("module",MODULE_NAME);
    if (operation)
	m->addParam("operation",operation);
    if (subscription)
	m->addParam("subscription",subscription);
    if (status)
	m->addParam("status",status);
    m->addParam("from",from);
    m->addParam("to",to);
    return Engine::enqueue(m);
}

/**
 * YJGEngine
 */
void YJGEngine::processEvent(JGEvent* event)
{
    if (!event)
	return;
    JGSession* session = event->session();
    // This should never happen !!!
    if (!session) {
	Debug(this,DebugWarn,"processEvent. Received event without session.");
	delete event;
	return;
    }
    YJGConnection* connection = 0;
    if (session->jingleConn()) {
	connection = static_cast<YJGConnection*>(session->jingleConn());
	connection->handleEvent(event);
	// Disconnect if final
	if (event->final())
	    connection->disconnect();
    }
    else {
	if (event->type() == JGEvent::Jingle &&
	    event->action() == JGSession::ActInitiate) {
	    if (event->session()->ref()) {
		connection = new YJGConnection(this,event);
		// Constructor failed ?
		if (connection->state() == YJGConnection::Pending)
		    connection->deref();
		else if (!connection->route())
		    event->session()->jingleConn(0);
	    }
	    else {
		Debug(this,DebugWarn,
		    "processEvent. Session ref failed for new connection.");
		event->session()->hangup(false,"failure");
	    }
        }
	else
	    DDebug(this,DebugAll,
		"processEvent. Invalid (non initiate) event for new session.");

    }
    delete event;
}

void YJGEngine::startThreads(u_int16_t read, u_int16_t process)
{
    // Read events from Jabber engine
    if (!read)
	Debug(this,DebugWarn,"No threads(s) to get events from JBEngine!");
    for (; read; read--)
	(new YJGLibThread(YJGLibThread::JGReader,"JGReader thread"))->startup();
    // Process the received events
    if (!process)
	Debug(this,DebugWarn,"No threads(s) to process events!");
    for (; process; process--)
	(new YJGLibThread(YJGLibThread::JGProcess,"JGProcess thread"))->startup();
}

/**
 * YJGTransport
 */
YJGTransport::YJGTransport(YJGConnection* connection, Message* msg)
    : Mutex(true),
      m_mediaReady(false),
      m_transportReady(false),
      m_started(false),
      m_remote(0),
      m_connection(connection),
      m_rtpData(0)
{
    Lock lock(this);
    if (!m_connection)
	return;
    // Set data members
    m_name = "rtp";
    m_protocol = "udp";
    m_type = "local";
    m_network = "0";
    m_preference = "1";
    m_generation = "0";
    iplugin.createAuthRandomString(m_username);
    iplugin.createAuthRandomString(m_password);
    // *** MEDIA
    // Get formats from message. Fill with all supported if none received
    NamedList nl("");
    const char* formats = msg ? msg->getValue("formats") : 0;
    if (formats) {
	// 'formats' parameter is empty ? Add 'alaw','mulaw'
	if (!iplugin.getParts(nl,formats,',',true)) {
	    nl.setParam("alaw","1");
	    nl.setParam("mulaw","2");
	}
    }
    else
	for (int i = 0; dict_payloads[i].token; i++)
	    nl.addParam(dict_payloads[i].token,String(i+1));
    // Parse the used codecs list
    // If the current element is in the received list, keep it
    ObjList* obj = iplugin.m_usedCodecs.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(obj->get());
	// Get name for this id
	const char* payload = lookup(a->m_id.toInteger(),dict_payloads);
	// Append if received a format
	if (nl.getValue(payload))
	    m_formats.append(new JGAudio(*a));
    }
    // Not outgoing: Ready
    if (m_connection->isIncoming())
	return;
    // *** TRANSPORT
    //TODO: Transport from message if RTP forward
}

YJGTransport::~YJGTransport()
{
    if (m_remote)
	m_remote->deref();
}

bool YJGTransport::initLocal()
{
    Lock lock(this);
    if (!m_connection)
	return false;
    // Set data
    Message m("chan.rtp");
    m.userData(static_cast<CallEndpoint*>(m_connection));
    m_connection->complete(m);
    m.addParam("direction","bidir");
    m.addParam("media","audio");
    m.addParam("anyssrc","true");
    m.addParam("getsession","true");
    m_address = s_localAddress;
    if (m_address)
	m.setParam("localip",m_address);
    else {
	String s;
	if (m_connection)
	    m_connection->getRemoteAddr(s);
	m.setParam("remoteip",s);
    }
    if (!Engine::dispatch(m)) {
	DDebug(m_connection,DebugAll,"Transport. Init RTP failed. [%p]",
	    m_connection);
	return false;
    }
    m_address = m.getValue("localip",m_address);
    m_port = m.getValue("localport","-1");
    return true;
}

bool YJGTransport::start()
{
    Lock lock(this);
    if (m_started || !(m_connection && m_mediaReady && m_transportReady))
	return false;
    Debug(m_connection,DebugCall,
	"Transport. Start. Local: '%s:%s'. Remote: '%s:%s'. [%p]",
	m_address.c_str(),m_port.c_str(),
	m_remote->m_address.c_str(),m_remote->m_port.c_str(),m_connection);
    // Start RTP
    Message* m = new Message("chan.rtp");
    m->userData(static_cast<CallEndpoint*>(m_connection));
    m_connection->complete(*m);
    m->addParam("direction","bidir");
    m->addParam("media","audio");
#if 0
    String formats;
    createMediaString(formats);
    m->addParam("formats",formats);
#endif
    const char* format = 0;
    ObjList* obj = m_formats.skipNull();
    if (obj) {
	JGAudio* audio = static_cast<JGAudio*>(obj->get());
	format = lookup(audio->m_id.toInteger(),dict_payloads);
    }
    m->addParam("format",format);
    m->addParam("localip",m_address);
    m->addParam("localport",m_port);
    m->addParam("remoteip",m_remote->m_address);
    m->addParam("remoteport",m_remote->m_port);
    //m.addParam("autoaddr","false");
    m->addParam("rtcp","false");
    m->addParam("getsession","true");
    if (!Engine::dispatch(m)) {
	Debug(m_connection,DebugCall,"Transport. Start RTP failed. [%p]",
	    m_connection);
	delete m;
	return false;
    }
    // Start STUN
    Message* msg = new Message("chan.stun");
    msg->userData(m->userData());
    msg->addParam("localusername",m_remote->m_username + m_username);
    msg->addParam("remoteusername",m_username + m_remote->m_username);
    msg->addParam("remoteip",m_remote->m_address);
    msg->addParam("remoteport",m_remote->m_port);
    msg->addParam("userid",m->getValue("rtpid"));
    delete m;
    Engine::enqueue(msg);
    m_started = true;
    return true;
}

bool YJGTransport::updateMedia(ObjList& media)
{
    Lock lock(this);
    if (m_mediaReady || !m_connection)
	return false;
    // Check if we received any media
    if (0 == media.skipNull()) {
	Debug(m_connection,DebugWarn,
	    "Transport. The remote party has no media. [%p]",m_connection);
	m_connection->hangup(false,"nomedia");
	return false;
    }
    ListIterator iter_local(m_formats);
    for (GenObject* go; (go = iter_local.get());) {
	JGAudio* local = static_cast<JGAudio*>(go);
	// Check if incoming media contains local media (compare 'id' and 'name')
	ObjList* obj = media.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    JGAudio* remote = static_cast<JGAudio*>(obj->get());
	    if (local->m_id == remote->m_id && local->m_name == remote->m_name)
		break;
	}
	// obj is 0. Current element from m_formats is not in received media. Remove it
	if (!obj)
	    m_formats.remove(local,true);
    }
    // Check if both parties have common media
    if (0 == m_formats.skipNull()) {
	String recvFormat;
	iplugin.createMediaString(recvFormat,media,',');
	Debug(m_connection,DebugWarn,
	    "Transport. Unable to negotiate media (no common formats). Received: '%s'. [%p]",
	    recvFormat.c_str(),m_connection);
	m_connection->hangup(false,"nomedia");
	return false;
    }
    m_mediaReady = true;
    if (iplugin.debugAt(DebugCall)) {
	String s;
	createMediaString(s);
	Debug(m_connection,DebugCall,"Transport. Media is ready ('%s'). [%p]",
	    s.c_str(),m_connection);
    }
    return true;
}

bool YJGTransport::updateTransport(ObjList& transport)
{
    Lock lock(this);
    if (m_transportReady || !m_connection)
	return false;
    JGTransport* remote = 0;
    // Find a transport we'd love to use
    ObjList* obj = transport.skipNull();
    for (; obj; obj = obj->skipNext()) {
	remote = static_cast<JGTransport*>(obj->get());
	// Check: generation, name, protocol, type, network
	if (m_generation == remote->m_generation &&
	    m_name == remote->m_name &&
	    m_protocol == remote->m_protocol &&
	    m_type == remote->m_type)
	    break;
	// We hate it: reset and skip
	DDebug(m_connection,DebugInfo,
	    "Transport. Unacceptable transport. Name: '%s'. Protocol: '%s. Type: '%s'. Generation: '%s'. [%p]",
	    m_remote->m_name.c_str(),m_remote->m_protocol.c_str(),
	    m_remote->m_type.c_str(),m_remote->m_generation.c_str(),
	    m_connection);
	remote = 0;
    }
    if (!remote)
	return false;
    // Ok: keep it !
    if (m_remote)
	m_remote->deref();
    m_remote = new JGTransport(*remote);
    m_transportReady = true;
    Debug(m_connection,DebugCall,
	"Transport. Transport is ready. Local: '%s:%s'. Remote: '%s:%s'. [%p]",
	m_address.c_str(),m_port.c_str(),
	m_remote->m_address.c_str(),m_remote->m_port.c_str(),m_connection);
    return true;
}

XMLElement* YJGTransport::createDescription()
{
    Lock lock(this);
    XMLElement* descr = JGAudio::createDescription();
    ObjList* obj = m_formats.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(obj->get());
	a->addTo(descr);
    }
    JGAudio* te = new JGAudio("106","telephone-event","8000","");
    te->addTo(descr);
    te->deref();
    return descr;
}

void YJGTransport::createMediaString(String& dest)
{
    Lock lock(this);
    iplugin.createMediaString(dest,m_formats,',');
}

/**
 * YJGConnection
 */
// Outgoing call
YJGConnection::YJGConnection(YJGEngine* jgEngine, Message& msg, const char* caller,
	const char* called, bool available)
    : Channel(&iplugin,0,true),
      m_state(Pending),
      m_jgEngine(jgEngine),
      m_session(0),
      m_local(caller),
      m_remote(called),
      m_transport(0),
      m_connMutex(true),
      m_hangup(false),
      m_timeout(0)
{
    Debug(this,DebugCall,"Outgoing. Caller: '%s'. Called: '%s'. [%p]",
	caller,called,this);
    m_callerPrompt = msg.getValue("callerprompt");
    // Init transport
    m_transport = new YJGTransport(this,&msg);
    // Set timeout
    m_timeout = msg.getIntValue("maxcall",0) * (u_int64_t)1000;
    u_int64_t pendingTimeout = iplugin.pendingTimeout() * (u_int64_t)1000;
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
    XDebug(this,DebugInfo,
	"YJGConnection. Time: " FMT64 ". Maxcall set to " FMT64 " us. [%p]",
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
	processPresence(true);
}

// Incoming call
YJGConnection::YJGConnection(YJGEngine* jgEngine, JGEvent* event)
    : Channel(&iplugin,0,false),
      m_state(Active),
      m_jgEngine(jgEngine),
      m_session(event->session()),
      m_local(event->session()->local()),
      m_remote(event->session()->remote()),
      m_transport(0),
      m_connMutex(true),
      m_hangup(false),
      m_timeout(0)
{
    Debug(this,DebugCall,"Incoming. Caller: '%s'. Called: '%s'. [%p]",
	m_remote.c_str(),m_local.c_str(),this);
    // Set session
    m_session->jingleConn(this);
    // Init transport
    m_transport = new YJGTransport(this);
    if (!m_transport->updateMedia(event->audio()))
	m_state = Pending;
    m_transport->updateTransport(event->transport());
    // Startup
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    m->setParam("caller",m_remote.bare());
    m->setParam("called",m_local.node());
    Engine::enqueue(m);
}

YJGConnection::~YJGConnection()
{
    hangup(false);
    disconnected(true,m_reason);
    Lock lock(m_connMutex);
    if (m_session) {
	m_session->deref();
	m_session = 0;
    }
    if (m_transport) {
	m_transport->deref();
	m_transport = 0;
    }
    Debug(this,DebugCall,"Terminated. [%p]",this);
}

bool YJGConnection::route()
{
    Message* m = message("call.preroute",false,true);
    m->addParam("username",m_remote.node());
    m->addParam("called",m_local.node());
    m->addParam("caller",m_remote.node());
    m->addParam("callername",m_remote.bare());
    m_connMutex.lock();
    if (m_transport && m_transport->remote()) {
	m->addParam("ip_host",m_transport->remote()->m_address);
	m->addParam("ip_port",m_transport->remote()->m_port);
    }
    m_connMutex.unlock();
    return startRouter(m);
}

void YJGConnection::callAccept(Message& msg)
{
    // Init local transport
    // Accept session and transport
    // Request transport
    // Try to start transport
    Debug(this,DebugCall,"callAccept. [%p]",this);
    m_connMutex.lock();
    if (m_transport && m_session) {
	m_transport->initLocal();
	m_session->accept(m_transport->createDescription());
	m_session->acceptTransport(0);
	m_transport->send(m_session);
    }
    m_connMutex.unlock();
    Channel::callAccept(msg);
}

void YJGConnection::callRejected(const char* error, const char* reason,
	const Message* msg)
{
    Channel::callRejected(error,reason,msg);
    if (error)
	m_reason = error;
    else
	m_reason = reason;
    Debug(this,DebugCall,"callRejected. Reason: '%s'. [%p]",m_reason.c_str(),this);
    hangup(false);
}

bool YJGConnection::callRouted(Message& msg)
{
    DDebug(this,DebugCall,"callRouted. [%p]",this);
    return true;
}

void YJGConnection::disconnected(bool final, const char* reason)
{
    Debug(this,DebugCall,"disconnected. Final: %u. Reason: '%s'. [%p]",
	final,reason,this);
    Lock lock(m_connMutex);
    if (m_reason.null() && reason)
	m_reason = reason;
    Channel::disconnected(final,m_reason);
}

bool YJGConnection::msgAnswered(Message& msg)
{
    DDebug(this,DebugCall,"msgAnswered. [%p]",this);
    return true;
}

bool YJGConnection::msgUpdate(Message& msg)
{
    DDebug(this,DebugCall,"msgUpdate. [%p]",this);
    return true;
}

bool YJGConnection::msgText(Message& msg, const char* text)
{
    DDebug(this,DebugCall,"msgText. '%s'. [%p]",text,this);
    Lock lock(m_connMutex);
    if (m_session) {
	m_session->sendMessage(text);
	return true;
    }
    return false;
}

bool YJGConnection::msgTone(Message& msg, const char* tone)
{
    DDebug(this,DebugCall,"msgTone. '%s'. [%p]",tone,this);
    Lock lock(m_connMutex);
    if (m_session)
	while (tone && *tone)
	    m_session->sendDtmf(*tone++);
    return true;
}

void YJGConnection::hangup(bool reject, const char* reason)
{
    Lock lock(m_connMutex);
    if (m_hangup)     // Already hung up
	return;
    m_hangup = true;
    m_state = Terminated;
    if (!m_reason) {
	if (reason)
	    m_reason = reason;
	else
	    m_reason = Engine::exiting() ? "Server shutdown" : "Hangup";
    }
    Message* m = message("chan.hangup",true);
    m->setParam("status","hangup");
    m->setParam("reason",m_reason);
    Engine::enqueue(m);
    if (m_session) {
	m_session->jingleConn(0);
	m_session->hangup(reject,m_reason);
    }
    Debug(this,DebugCall,"hangup. Reason: '%s'. [%p]",m_reason.c_str(),this);
}

void YJGConnection::handleEvent(JGEvent* event)
{
    if (!event)
	return;
    Lock lock(m_connMutex);
    switch (event->type()) {
	case JGEvent::Jingle:
	    handleJingle(event);
	    break;
	case JGEvent::Terminated:
	    m_reason = event->reason();
	    Debug(this,DebugCall,"handleEvent((%p): %u). Terminated. Reason: '%s'. [%p]",
		event,event->type(),m_reason.c_str(),this);
	    break;
	case JGEvent::Error:
	    handleError(event);
	    break;
	case JGEvent::Message: {
	    DDebug(this,DebugCall,
		"handleEvent((%p): %u). Message: '%s'. [%p]",
		event,event->type(),event->text().c_str(),this);
	    Message* m = message("chan.text");
	    m->addParam("text",event->text());
            Engine::enqueue(m);
	    }
	    break;
	default:
	    Debug(this,DebugNote,"handleEvent((%p): %u). Unexpected. [%p]",
		event,event->type(),event);
    }
}

void YJGConnection::handleJingle(JGEvent* event)
{
    switch (event->action()) {
	case JGSession::ActDtmf:
	    Debug(this,DebugInfo,"handleJingle. Dtmf(%s): '%s'. [%p]",
		event->reason().c_str(),event->text().c_str(),this);
	    if (event->reason() == "button-up") {
		Message* m = message("chan.dtmf");
		m->addParam("text",event->text());
		Engine::enqueue(m);
	    }
	    break;
	case JGSession::ActDtmfMethod:
	    Debug(this,DebugInfo,"handleJingle. Dtmf method: '%s'. [%p]",
		event->text().c_str(),this);
	    // TODO: method can be 'rtp' or 'xmpp'
	    m_session->sendResult(event->id());
	    break;
	case JGSession::ActTransport:
	    {
		bool accept = !m_transport->transportReady() &&
		    m_transport->updateTransport(event->transport());
		DDebug(this,DebugInfo,"handleJingle. Transport-info. %s. [%p]",
		    accept?"Accepted":"Not accepted",this);
		if (!accept) {
		    XMPPError::ErrorType errType = XMPPError::TypeCancel;
		    if (!m_transport->transportReady())
			errType = XMPPError::TypeModify;
		    m_session->sendError(event->releaseXML(),
			XMPPError::SNotAcceptable,errType);
		}
		else {
		    m_session->sendResult(event->id());
		    if (isOutgoing())
			m_session->acceptTransport(0);
		}
	    }
	    m_transport->start();
	    break;
	case JGSession::ActTransportAccept:
	    Debug(this,DebugInfo,"handleJingle. Transport-accept. [%p]",this);
	    break;
	case JGSession::ActAccept:
	    if (isAnswered())
		break;
	    // Update media
	    Debug(this,DebugCall,"handleJingle. Accept. [%p]",this);
	    m_transport->updateMedia(event->audio());
	    m_transport->start();
	    // Notify engine
	    maxcall(0);
	    status("answered");
	    Engine::enqueue(message("call.answered",false,true));
	    break;
	case JGSession::ActModify:
	    Debug(this,DebugWarn,"handleJingle. Modify: not implemented. [%p]",this);
	    break;
	case JGSession::ActRedirect:
	    Debug(this,DebugWarn,"handleJingle. Redirect: not implemented. [%p]",this);
	    break;
	default: ;
	    DDebug(this,DebugWarn,
		"handleJingle. Event (%p). Action: %u. Unexpected. [%p]",
		event,event->action(),this);
    }
}

void YJGConnection::handleError(JGEvent* event)
{
    DDebug(this,DebugCall,
	"handleError. Error. Id: '%s'. Reason: '%s'. Text: '%s'. [%p]",
	event->id().c_str(),event->reason().c_str(),
	event->text().c_str(),this);
}

bool YJGConnection::processPresence(bool available, const char* error)
{
    Lock lock(m_connMutex);
    if (m_state == Terminated) {
	DDebug(this,DebugInfo,
	    "processPresence. Received presence in Terminated state. [%p]",this);
	return false;
    }
    // Check if error or unavailable in any other state
    if (!(error || available))
	error = "offline";
    if (error) {
	Debug(this,DebugCall,"processPresence. Hangup (%s). [%p]",error,this);
	hangup(false,error);
	return true;
    }
    // Check if we are in pending state and remote peer is present
    if (!(m_state == Pending && available))
	return false;
    // Make the call
    m_state = Active;
    Debug(this,DebugCall,"call. Caller: '%s'. Called: '%s'. [%p]",
	m_local.c_str(),m_remote.c_str(),this);
    // Make the call
    m_session = iplugin.m_jg->call(m_local,m_remote,
	m_transport->createDescription(),
	JGTransport::createTransport(),m_callerPrompt);
    if (!m_session) {
	hangup(false,"create session failed");
	return true;
    }
    // Adjust timeout
    maxcall(m_timeout);
    XDebug(this,DebugInfo,"processPresence. Maxcall set to " FMT64 " us. [%p]",maxcall(),this);
    // Send prompt
    Engine::enqueue(message("call.ringing",false,true));
    m_session->jingleConn(this);
    // Send transport
    m_transport->initLocal();
    m_transport->send(m_session);
    return false;
}

/**
 * YJGLibThread
 */
YJGLibThread::YJGLibThread(Action action, const char* name, Priority prio)
    : Thread(name,prio), m_action(action), m_stream(0)
{
}

YJGLibThread::YJGLibThread(JBComponentStream* stream, const char* name,
	Priority prio)
    : Thread(name,prio), m_action(JBConnect), m_stream(0)
{
    if (stream && stream->ref())
	m_stream = stream;
}

void YJGLibThread::run()
{
    switch (m_action) {
	case JBReader:
	    DDebug(iplugin.m_jb,DebugAll,"%s started.",name());
	    iplugin.m_jb->runReceive();
	    break;
	case JBConnect:
	    if (m_stream) {
		DDebug(iplugin.m_jb,DebugAll,
		    "%s started. Stream (%p). Remote: '%s'.",
		    name(),m_stream,m_stream->remoteName().c_str());
		m_stream->connect();
		m_stream->deref();
		m_stream = 0;
	    }
	    break;
	case JGReader:
	    DDebug(iplugin.m_jg,DebugAll,"%s started.",name());
	    iplugin.m_jg->runReceive();
	    break;
	case JGProcess:
	    DDebug(iplugin.m_jg,DebugAll,"%s started.",name());
	    iplugin.m_jg->runProcess();
	    break;
	case JBPresence:
	    DDebug(iplugin.m_jb,DebugAll,"%s started.",name());
	    iplugin.m_presence->runProcess();
	    break;
	case JBPresenceTimeout:
	    DDebug(iplugin.m_jb,DebugAll,"%s started.",name());
	    iplugin.m_presence->runCheckTimeout();
	    break;
    }
    DDebug(iplugin.m_jb,DebugAll,"%s end of run.",name());
}

/**
 * resource.notify message handler
 */
bool ResNotifyHandler::received(Message& msg)
{
    // Avoid loopback message (if the same module: it's a message sent by this module)
    if (MODULE_NAME == msg.getValue("module"))
	return false;
    JabberID from,to;
    // *** Check from/to
    if (!iplugin.getJidFrom(from,msg,true))
	return true;
    if (iplugin.m_sendCommandOnNotify)
	to = msg.getValue("to");
    else if (!iplugin.decodeJid(to,msg,"to"))
	return true;
    // *** Check status
    String status = msg.getValue("status");
    if (status.null()) {
	Debug(&iplugin,DebugNote,
	    "Received '%s' from '%s' with missing 'status' parameter.",
	    MODULE_MSG_NOTIFY,from.c_str());
	return true;
    }
    // *** Everything is OK. Process the message
    XDebug(&iplugin,DebugAll,"Received '%s' from '%s' with status '%s'.",
	MODULE_MSG_NOTIFY,from.c_str(),status.c_str());
    if (!iplugin.m_sendCommandOnNotify && iplugin.m_presence->addOnPresence())
	process(from,to,status,msg.getBoolValue("subscription",false));
    else
	sendPresence(from,to,status);
    return true;
}

void ResNotifyHandler::process(const JabberID& from, const JabberID& to,
	const String& status, bool subFrom)
{
    XMPPUserRoster* roster = iplugin.m_presence->getRoster(from,true,0);
    XMPPUser* user = roster->getUser(to,false,0);
    // Add new user and local resource
    if (!user) {
	user = new XMPPUser(roster,to.node(),to.domain(),
	    subFrom ? XMPPUser::From : XMPPUser::None,false,false);
	iplugin.m_presence->notifyNewUser(user);
	if (!user->ref()) {
	    roster->deref();
	    return;
	}
    }
    roster->deref();
    user->lock();
    // Process
    for (;;) {
	if (status == "subscribed") {
	    // Send only if not already subscribed to us
	    if (!user->subscribedFrom())
		user->sendSubscribe(JBPresence::Subscribed,0);
	    break;
	}
	if (status == "unsubscribed") {
	    // Send only if not already unsubscribed from us
	    if (user->subscribedFrom())
		user->sendSubscribe(JBPresence::Unsubscribed,0);
	    break;
	}
	// Presence
	JIDResource* res = user->getAudio(true,true);
	if (!res)
	    break;
	bool changed = false;
	if (status == "offline")
	    changed = res->setPresence(false);
	else {
	    changed = res->setPresence(true);
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
	if (changed)
	    user->sendPresence(res,0,true);
	break;
    }
    user->unlock();
    user->deref();
}

void ResNotifyHandler::sendPresence(JabberID& from, JabberID& to,
	const String& status)
{
    JBPresence::Presence jbPresence;
    // Get presence type from status
    if (status == "online")
	jbPresence = JBPresence::None;
    else if (status == "offline")
	jbPresence = JBPresence::Unavailable;
    else {
	if (iplugin.m_sendCommandOnNotify) {
	    XDebug(&iplugin,DebugNote,"Can't send command for status='%s'.",status.c_str());
	    return;
	}
	if (status == "subscribed")
	    jbPresence = JBPresence::Subscribed;
	else if (status == "unsubscribed") 
	    jbPresence = JBPresence::Unsubscribed;
	else {
	    XDebug(&iplugin,DebugNote,"Can't send presence for status='%s'.",status.c_str());
	    return;
	}
    }
    // Check if we can get a stream
    JBComponentStream* stream = iplugin.m_jb->getStream();
    if (!stream)
	return;
    // Create XML element to be sent
    bool available = (jbPresence == JBPresence::None);
    XMLElement* stanza = 0;
    if (iplugin.m_sendCommandOnNotify) {
	if (to.domain().null())
	    to.domain(iplugin.m_jb->componentServer().c_str());
	DDebug(&iplugin,DebugNote,"Sending presence %s from: %s to: %s",
	    String::boolText(available),from.c_str(),to.c_str());
	stanza = iplugin.getPresenceCommand(from,to,available);
    }
    else {
	stanza = JBPresence::createPresence(from,to,jbPresence);
	// Create resource info if available
	if (available) {
	    JIDResource* resource = new JIDResource(from.resource(),JIDResource::Available,
		JIDResource::CapAudio);
	    resource->addTo(stanza);
	    resource->deref();
	}
    }
    // Send
    stream->sendStanza(stanza);
    stream->deref();
}

/**
 * resource.subscribe message handler
 */
bool ResSubscribeHandler::received(Message& msg)
{
    // Avoid loopback message (if the same module: it's a message sent by this module)
    if (MODULE_NAME == msg.getValue("module"))
	return false;
    JabberID from,to;
    // *** Check from/to
    if (!iplugin.decodeJid(from,msg,"from",true))
	return true;
    if (!iplugin.decodeJid(to,msg,"to"))
	return true;
    // *** Check operation
    String tmpParam = msg.getValue("operation");
    JBPresence::Presence presence;
    if (tmpParam == "subscribe")
	presence = JBPresence::Subscribe;
    else if (tmpParam == "probe")
	presence = JBPresence::Probe;
    else if (tmpParam == "unsubscribe")
	presence = JBPresence::Unsubscribe;
    else {
	Debug(&iplugin,DebugNote,
	    "Received '%s' with missing or unknown parameter: operation=%s.",
	    MODULE_MSG_SUBSCRIBE,msg.getValue("operation"));
	return true;
    }
    // *** Everything is OK. Process the message
    XDebug(&iplugin,DebugAll,"Accepted '%s'.",MODULE_MSG_SUBSCRIBE);
    process(from,to,presence);
    return true;
}

void ResSubscribeHandler::process(const JabberID& from, const JabberID& to,
	JBPresence::Presence presence)
{
    // Don't automatically add
    if ((presence == JBPresence::Probe && !iplugin.m_presence->addOnProbe()) ||
	((presence == JBPresence::Subscribe || presence == JBPresence::Unsubscribe) &&
	!iplugin.m_presence->addOnSubscribe())) {
	JBComponentStream* stream = iplugin.m_jb->getStream();
	if (!stream)
	    return;
	stream->sendStanza(JBPresence::createPresence(from,to,presence));
	stream->deref();
	return;
    }
    // Add roster/user
    XMPPUserRoster* roster = iplugin.m_presence->getRoster(from,true,0);
    XMPPUser* user = roster->getUser(to,false,0);
    // Add new user and local resource
    if (!user) {
	user = new XMPPUser(roster,to.node(),to.domain(),XMPPUser::From,
	    false,false);
	iplugin.m_presence->notifyNewUser(user);
	if (!user->ref()) {
	    roster->deref();
	    return;
	}
    }
    roster->deref();
    // Process
    user->lock();
    for (;;) {
	if (presence == JBPresence::Subscribe) {
	    // Already subscribed: notify. NO: send request
	    if (user->subscribedTo())
		iplugin.m_presence->notifySubscribe(user,JBPresence::Subscribed);
	    else {
		user->sendSubscribe(JBPresence::Subscribe,0);
		user->probe(0);
	    }
	    break;
	}
	if (presence == JBPresence::Unsubscribe) {
	    // Already unsubscribed: notify. NO: send request
	    if (!user->subscribedTo())
		iplugin.m_presence->notifySubscribe(user,JBPresence::Unsubscribed);
	    else {
		user->sendSubscribe(JBPresence::Unsubscribe,0);
		user->probe(0);
	    }
	    break;
	}
	// Respond if user has a resource with audio capabilities
	JIDResource* res = user->getAudio(false,true);
	if (res) {
	    user->notifyResource(true,res);
	    break;
	}
	// No audio resource for remote user: send probe
	// Send probe fails: Assume remote user unavailable
	if (!user->probe(0))
	    iplugin.m_presence->notifyPresence(0,to,from,false);
	break;
    }
    user->unlock();
    user->deref();
}

/**
 * YJGDriver
 */
YJGDriver::YJGDriver()
    : Driver(MODULE_NAME,"varchans"),
      m_jb(0), m_presence(0), m_jg(0), m_sendCommandOnNotify(true), m_init(false), m_pendingTimeout(0)
{
    Output("Loaded module YJingle");
}

YJGDriver::~YJGDriver()
{
    Output("Unloading module YJingle");
    if (m_presence)
	m_presence->deref();
    if (m_jg)
	m_jg->deref();
    if (m_jb)
	m_jb->deref();
}

void YJGDriver::initialize()
{
    Output("Initializing module YJingle");
    s_cfg = Engine::configFile("yjinglechan");
    s_cfg.load();
    if (m_init) {
	if (m_jb)
	    m_jb->printXml(s_cfg.getBoolValue("general","printxml",false));
	return;
    }
    NamedList* sect = s_cfg.getSection("general");
    if (!sect) {
	Debug(this,DebugNote,"Section [general] missing - no initialization.");
	return;
    }
    m_init = true;
    s_localAddress = sect->getValue("localip");
    m_anonymousCaller = sect->getValue("anonymous_caller",JINGLE_ANONYMOUS_CALLER);
    m_pendingTimeout = sect->getIntValue("pending_timeout",JINGLE_CONN_TIMEOUT);
    lock();
    initCodecLists();                 // Init codec list
    if (debugAt(DebugAll)) {
	String s;
	s << "\r\nlocalip=" << s_localAddress;
	s << "\r\npending_timeout=" << (unsigned int)m_pendingTimeout;
	s << "\r\nanonymous_caller=" << m_anonymousCaller;
	String media;
	createMediaString(media,m_usedCodecs,' ');
	s << "\r\ncodecs=" << media;
	Debug(this,DebugAll,"Initialized:%s",s.c_str());
    }
    if (s_localAddress.null())
	Debug(this,DebugNote,"No local address set.");
    // Initialize
    initJB(*sect);                    // Init Jabber Component engine
    initPresence(*sect);              // Init Jabber Component presence
    initJG(*sect);                    // Init Jingle engine
    unlock();
    // Driver setup
    installRelay(Halt);
    Engine::install(new ResNotifyHandler);
    Engine::install(new ResSubscribeHandler);
    setup();
}

bool YJGDriver::getParts(NamedList& dest, const char* src, const char sep,
	bool nameFirst)
{
    if (!src)
	return false;
    u_int32_t index = 1;
    for (u_int32_t i = 0; src[i];) {
	// Skip separator(s)
	for (; src[i] && src[i] == sep; i++) ;
	// Find first separator
	u_int32_t start = i;
	for (; src[i] && src[i] != sep; i++) ;
	// Get part
	if (start != i) {
	    String tmp(src + start,i - start);
	    if (nameFirst)
		dest.setParam(tmp,String((int)index++));
	    else
		dest.setParam(String((int)index++),tmp);
	}
    }
    return true;
}

void YJGDriver::initCodecLists()
{
    // Init all supported codecs if not already done
    if (!m_allCodecs.skipNull()) {
	m_allCodecs.append(new JGAudio("0",  "PCMU",    "8000",  ""));
	m_allCodecs.append(new JGAudio("2",  "G726-32", "8000",  ""));
	m_allCodecs.append(new JGAudio("3",  "GSM",     "8000",  ""));
	m_allCodecs.append(new JGAudio("4",  "G723",    "8000",  ""));
	m_allCodecs.append(new JGAudio("7",  "LPC",     "8000",  ""));
	m_allCodecs.append(new JGAudio("8",  "PCMA",    "8000",  ""));
	m_allCodecs.append(new JGAudio("9",  "G722",    "8000",  ""));
	m_allCodecs.append(new JGAudio("11", "L16",     "8000",  ""));
	m_allCodecs.append(new JGAudio("15", "G728",    "8000",  ""));
	m_allCodecs.append(new JGAudio("18", "G729",    "8000",  ""));
	m_allCodecs.append(new JGAudio("31", "H261",    "90000", ""));
	m_allCodecs.append(new JGAudio("32", "MPV",     "90000", ""));
	m_allCodecs.append(new JGAudio("34", "H263",    "90000", ""));
	m_allCodecs.append(new JGAudio("98", "iLBC",    "8000",  ""));
    }
    // Init codecs in use
    m_usedCodecs.clear();
    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
    for (int i = 0; dict_payloads[i].token; i++) {
	// Skip if duplicate id
	// TODO: Enforce checking: Equal IDs may not be neighbours
	if (dict_payloads[i].value == dict_payloads[i+1].value)
	    continue;
	const char* payload = dict_payloads[i].token;
	bool enable = defcodecs && DataTranslator::canConvert(payload);
	// If enabled, add the codec to the used codecs list
	if (s_cfg.getBoolValue("codecs",payload,enable)) {
	    // Use codec if exists in m_allCodecs
	    ObjList* obj = m_allCodecs.skipNull();
	    for (; obj; obj = obj->skipNext()) {
		JGAudio* a = static_cast<JGAudio*>(obj->get());
		if (a->m_id == dict_payloads[i].value) {
		    XDebug(this,DebugAll,"Add '%s' to used codecs",payload);
		    m_usedCodecs.append(new JGAudio(*a));
		    break;
		}
	    }
	}
    }
    if (!m_usedCodecs.skipNull())
	Debug(this,DebugWarn,"No audio format(s) available.");
}

void YJGDriver::initJB(const NamedList& sect)
{
    if (m_jb)
	return;
    // Create the engine
    m_jb = new YJBEngine();
    m_jb->debugChain(this);
    // Initialize
    m_jb->initialize(sect);
    String defComponent;
    // Set server list
    unsigned int count = s_cfg.sections();
    for (unsigned int i = 0; i < count; i++) {
	const NamedList* comp = s_cfg.getSection(i);
	if (!comp)
	    continue;
	String name = *comp;
	if (name.null() || (name == "general") || (name == "codecs"))
	    continue;
	const char* address = comp->getValue("address");
	int port = comp->getIntValue("port",0);
	if (!(address && port)) {
	    Debug(this,DebugNote,"Missing address or port in configuration for %s",
		name.c_str());
	    continue;
	}
	const char* password = comp->getValue("password");
	// Check identity
	String identity = comp->getValue("identity");
	if (identity.null())
	    identity = name;
	String fullId;
	bool keepRoster = false;
	if (identity == name) {
	    String subdomain = comp->getValue("subdomain",s_cfg.getValue(
		"general","default_resource",m_jb->defaultResource()));
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
	bool startup = comp->getBoolValue("startup");
	u_int32_t restartCount = m_jb->restartCount();
	if (!(address && port && !identity.null()))
	    continue;
	if (defComponent.null() || comp->getBoolValue("default"))
	    defComponent = name;
	JBServerInfo* server = new JBServerInfo(name,address,port,
	    password,identity,fullId,keepRoster,startup,restartCount);
	DDebug(this,DebugAll,
	    "Add server '%s' addr=%s port=%d pass=%s ident=%s full-ident=%s roster=%s startup=%s restartcount=%u.",
	    name.c_str(),address,port,password,identity.c_str(),fullId.c_str(),
	    String::boolText(keepRoster),String::boolText(startup),restartCount);
	m_jb->appendServer(server,startup);
    }
    // Set default server
    m_jb->setComponentServer(defComponent);
    // Init threads
    int read = 1;
    m_jb->startThreads(read);
}

void YJGDriver::initPresence(const NamedList& sect)
{
    // Already initialized ?
    if (m_presence)
	m_presence->initialize(sect);
    else {
	m_presence = new YJBPresence(m_jb,sect);
	m_presence->debugChain(this);
	// Init threads
	int process = 1;
	int timeout = 1;
	m_presence->startThreads(process,timeout);
	m_sendCommandOnNotify = !(m_presence->addOnSubscribe() &&
	    m_presence->addOnProbe() && m_presence->addOnPresence());
    }
}

void YJGDriver::initJG(const NamedList& sect)
{
    if (m_jg)
	m_jg->initialize(sect);
    else {
	m_jg = new YJGEngine(m_jb,sect);
	m_jg->debugChain(this);
	// Init threads
	int read = 1;
	int process = 1;
	m_jg->startThreads(read,process);
    }
    m_jg->requestSubscribe(sect.getBoolValue("request_subscribe",true));
    if (debugAt(DebugAll)) {
	String s;
	s << "\r\nrequest_subscribe=" << String::boolText(m_jg->requestSubscribe());
	Debug(m_jg,DebugAll,"Initialized:%s",s.c_str());
    }
}

bool YJGDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugNote,"Jingle call failed. No data channel.");
	msg.setParam("error","failure");
	return false;
    }
    // Assume Jabber Component !!!
    // Get identity for default server
    String identity;
    if (!iplugin.m_jb->getFullServerIdentity(identity)) {
	Debug(this,DebugNote,"Jingle call failed. No default server.");
	msg.setParam("error","failure");
	return false;
    }
    String c = msg.getValue("caller");
    if (!c)
	c = iplugin.anonymousCaller();
    if (!(c && JabberID::valid(c))) {
	Debug(this,DebugNote,"Jingle call failed. Missing or invalid caller name.");
	msg.setParam("error","failure");
	return false;
    }
    JabberID caller(c,identity);
    JabberID called(dest);
    // Get an available resource for the remote user if we keep the roster
    bool available = true;
    if (iplugin.m_presence->addOnSubscribe() ||
	iplugin.m_presence->addOnProbe() ||
	iplugin.m_presence->addOnPresence()) {
	// Get remote user
	bool newPresence = false;
	XMPPUser* remote = iplugin.m_presence->getRemoteUser(caller,called,true,0,
	    true,&newPresence);
	// Get a resource for the caller
	JIDResource* res = remote->getAudio(true,true);
	if (!res) {
	    iplugin.m_presence->notifyNewUser(remote);
	    res = remote->getAudio(true,true);
	    // This should never happen !!!
	    if (!res) {
		remote->deref();
		Debug(this,DebugNote,
		    "Jingle call failed. Unable to get a resource for the caller.");
		msg.setParam("error","failure");
		return false;
	    }
	}
	caller.resource(res->name());
	// Get a resource for the called
	res = remote->getAudio(false,true);
	available = (res != 0);
	if (!(newPresence || available)) {
	    if (!iplugin.m_jg->requestSubscribe()) {
		remote->deref();
		Debug(this,DebugNote,"Jingle call failed. Remote peer is unavailable.");
		msg.setParam("error","offline");
		return false;
	    }
	    remote->sendSubscribe(JBPresence::Subscribe,0);
	}
	if (available)
	    called.resource(res->name());
	else
	    if (!newPresence)
		remote->probe(0);
	remote->deref();
    }
    else {
	available = false;
	// Get stream for default component
	JBComponentStream* stream = m_jb->getStream();
	if (!stream) {
	    Debug(this,DebugNote,"Jingle call failed. No stream for called=%s.",called.c_str());
	    msg.setParam("error","failure");
	    return false;
	}
	// Send subscribe request and probe
	XMLElement* xml = 0;
	if (iplugin.m_jg->requestSubscribe()) {
	    xml = JBPresence::createPresence(caller.bare(),called.bare(),JBPresence::Subscribe);
	    stream->sendStanza(xml);
	}
	xml = JBPresence::createPresence(caller.bare(),called.bare(),JBPresence::Probe);
	stream->sendStanza(xml);
	stream->deref();
    }
    // Set a resource for caller
    if (caller.resource().null())
	caller.resource(iplugin.m_jb->defaultResource());
    // Parameters OK. Create connection and init channel
    DDebug(this,DebugAll,"msgExecute. Caller: '%s'. Called: '%s'.",
	caller.c_str(),called.c_str());
    YJGConnection* conn = new YJGConnection(m_jg,msg,caller,called,available);
    Channel* ch = static_cast<Channel*>(msg.userData());
    if (ch && conn->connect(ch,msg.getValue("reason"))) {
	msg.setParam("peerid",conn->id());
	msg.setParam("targetid",conn->id());
    }
    conn->deref();
    return true;
}

bool YJGDriver::received(Message& msg, int id)
{
    if (id == Timer)
	m_jb->timerTick(msg.msgTime().msec());
    else
	if (id == Halt) {
	    dropAll(msg);
	    lock();
	    channels().clear();
	    unlock();
	    m_presence->cleanup();
	    m_jb->cleanup();
	}
    return Driver::received(msg,id);
}

bool YJGDriver::getJidFrom(JabberID& jid, Message& msg, bool checkDomain)
{
    String username = msg.getValue("username");
    if (username.null())
	return decodeJid(jid,msg,"from",checkDomain);
    String domain;
    iplugin.m_jb->getFullServerIdentity(domain);
    jid.set(username,domain,iplugin.m_jb->defaultResource());
    return true;
}

bool YJGDriver::decodeJid(JabberID& jid, Message& msg, const char* param,
	bool checkDomain)
{
    jid.set(msg.getValue(param));
    if (jid.node().null() || jid.domain().null()) {
	Debug(this,DebugNote,"'%s'. Parameter '%s'='%s' is an invalid JID.",
	    msg.c_str(),param,jid.c_str());
	return false;
    }
    if (checkDomain && !m_presence->validDomain(jid.domain())) {
	Debug(this,DebugNote,"'%s'. Parameter '%s'='%s' has invalid (unknown) domain.",
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
    if (iplugin.m_jb->getServerIdentity(domain))
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
		(!broadcast && local.bare() != conn->local().bare()) ||
		remote.bare() != conn->remote().bare())
		continue;
	    conn->updateResource(remote.resource());
	    if (conn->processPresence(true))
		conn->disconnect();
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
	if (conn->processPresence(false))
	    conn->disconnect();
    }
    unlock();
}

void YJGDriver::createMediaString(String& dest, ObjList& formats, char sep)
{
    dest = "";
    String s = sep;
    ObjList* obj = formats.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(obj->get());
	const char* payload = lookup(a->m_id.toInteger(),dict_payloads);
	if (!payload)
	    continue;
	dest.append(payload,s);
    }
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
	    if (bareJID == conn->local().bare())
		return conn;
	}
	else if (conn->local().match(local))
	    return conn;
    }
    return 0;
}


}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
