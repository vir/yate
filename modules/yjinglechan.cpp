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
class YUserPresence;                     // A local user's roster
class YJGTransport;                      // Handle the transport for a connection
class YJGConnection;                     // Jingle channel
class YJGLibThread;                      // Library thread
class ResNotifyHandler;                  // resource.notify handler
class ResSubscribeHandler;               // resource.subscribe handler
class ResUnsubscribeHandler;             // resource.unsubscribe handler
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

#define JINGLE_RESOURCE          "Talk"  // Default resource for local party
#define JINGLE_VOICE         "voice-v1"  // Voice capability for Google Talk
#define JINGLE_VERSION            "1.0"  // Version capability

#define JINGLE_AUTHSTRINGLEN         16  // Username/Password length for transport

#define JINGLE_CONN_TIMEOUT       10000  // Timeout value to override "maxcall" in call.execute

#define JINGLE_ANONYMOUS_CALLER "anonymous"

static const String MODULE_NAME("jingle");
#define MODULE_PROTOCOL          "xmpp"

#define MODULE_MSG_NOTIFY      "resource.notify"
#define MODULE_MSG_SUBSCRIBE   "resource.subscribe"
#define MODULE_MSG_UNSUBSCRIBE "resource.unsubscribe"

/*
resource.notify
	module		The sending module's name
	protocol	MODULE_PROTOCOL
	subscription	none/to/from/both
	presence	unknown/available/unavailable
	from_user       node@domain
	from_resource   resource
	from_uri	node@domain/resource
	to_user		node@domain
	to_resource	resource
	to_uri		node@domain/resource

resource.subscribe
resource.unsubscribe
	module		The sending module's name
	protocol	MODULE_PROTOCOL
	subscription	none/to/from/both
	presence	unknown/available/unavailable  (OPTIONAL)
	from_user       node@domain
	to_user		node@domain
*/

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
    // Start thread members
    // @param read Reading socket thread count.
    void startThreads(u_int16_t read);
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
    inline int autoSubscribe() const
	{ return m_autoSubscribe; }
    inline bool delUnavailable() const
	{ return m_delUnavailable; }
    void initialize(const NamedList& params);
    // Overloaded methods
    virtual void processDisco(JBEvent* event);
    virtual void processError(JBEvent* event);
    virtual void processProbe(JBEvent* event);
    virtual void processSubscribe(JBEvent* event);
    virtual void processSubscribed(JBEvent* event);
    virtual void processUnsubscribe(JBEvent* event);
    virtual void processUnsubscribed(JBEvent* event);
    virtual void processUnavailable(JBEvent* event);
    virtual void processUnknown(JBEvent* event);
    // Start thread members
    // @param process Event processor thread count.
    void startThreads(u_int16_t process);
    // Find a user pair by JIDs (is remote has a resource the ). If none, create one
    // @param newPresence Set to true on exit if a new element was created
    // @param audio True to request an audio resource
    // @return The presence state
    bool get(const JabberID& local, JabberID& remote, bool& newPresence, bool audio = true);
    // Enqueue message in the engine
    void notify(const YUserPresence* yup, const char* error = 0);
    void subscribe(const YUserPresence* yup, JBPresence::Presence type);
    inline void cleanup() {
	    Lock lock(m_mutexUserpair);
	    m_userpair.clear();
	}

protected:
    void processBroadcast(JBEvent* event, bool available);
    void processDirected(JBEvent* event, bool available);
    void processSubscribe(JBEvent* event, JBPresence::Presence type);
    int getSubscription(const JabberID& local, const JabberID& remote);
    // Add/remove a user pair to the list. Used by YUserPresence constructor
    void addPresence(YUserPresence* yup);
    void removePresence(YUserPresence* yup);

private:
    ObjList m_userpair;                  // User pair list
    Mutex m_mutexUserpair;               // m_userpair lock
    int m_autoSubscribe;                 // Auto subscribe state 
    bool m_delUnavailable;               // Delete unavailable user or resource
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
 * YUserPresence
 */
class YUserPresence : public RefObject, public Mutex
{
public:
    enum State {
	Unknown,
	Available,
	Unavailable,
    };
    enum Subscription {
	SubNone = 0,
	SubTo   = 1,
	SubFrom = 2,
	SubBoth = 3,
    };

    YUserPresence(YJBPresence* engine, const char* local, const char* remote,
	Subscription subscription, State state);
    virtual ~YUserPresence();
    inline const JabberID& local() const
	{ return m_local; }
    inline const JabberID& remote() const
	{ return m_remote; }
    inline State localState() const
	{ return m_localState; }
    inline State remoteState() const
	{ return m_remoteState; }
    inline bool available() const
	{ return m_remoteState == Available; }
    inline int subscription() const
	{ return m_subscription; }
    inline bool subscribedTo() const
	{ return (m_subscription & SubTo); }
    inline bool subscribedFrom() const
	{ return (m_subscription & SubFrom); }
    inline bool audio() const
	{ return m_audio; }
    // Send a presence element to the remote peer
    // The caps parameter is used only for type None to send capabilities
    bool send(JBPresence::Presence type = JBPresence::None, bool caps = true,
	JBComponentStream* stream = 0);
    // Request info
    bool sendInfoRequest(bool info = true, JBComponentStream* stream = 0);
    // Send query info/items
    bool sendInfo(const char* id, JBComponentStream* stream = 0);
    bool sendItems(const char* id, JBComponentStream* stream = 0);
    // Process presence
    void processError(JBEvent* event);
    void processProbe(JBEvent* event);
    void processUnavailable(JBEvent* event);
    void processUnknown(JBEvent* event);
    void updateSubscription(bool from, bool value);
    static inline const char* stateText(int value)
	{ return lookup(value,s_state); }
    static inline int stateType(const char* value)
	{ return lookup(value,s_state,Unknown); }
    static inline const char* subscribeText(int value)
	{ return lookup(value,s_subscription); }
    static inline int subscribeType(const char* value)
	{ return lookup(value,s_subscription,SubNone); }
protected:
    void updateResource(XMLElement* element);
    void updateState(bool available);
    bool getStream(JBComponentStream*& stream, bool& release);
    inline bool sendStanza(JBComponentStream* stream, XMLElement* xml) {
	    JBComponentStream::Error res = stream->sendStanza(xml);
	    if (res == JBComponentStream::ErrorContext ||
		res == JBComponentStream::ErrorNoSocket)
		return false;
	    return true;
	}
    static TokenDict s_subscription[];
    static TokenDict s_state[];
private:
    JabberID m_local;                    // Local peer's JID
    JabberID m_remote;                   // Remote peer's JID
    State m_localState;                  // Remote peer's availability
    State m_remoteState;                 // Remote peer's availability
    int m_subscription;                  // Subscription state
    bool m_audio;                        // Resource has audio capabilities
    YJBPresence* m_engine;               // The presence engine
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
    virtual bool received(Message &msg);
};

/**
 * resource.subscribe message handler
 */
class ResSubscribeHandler : public MessageHandler
{
public:
    ResSubscribeHandler() : MessageHandler(MODULE_MSG_SUBSCRIBE) {}
    virtual bool received(Message &msg);
};

/**
 * resource.unsubscribe message handler
 */
class ResUnsubscribeHandler : public MessageHandler
{
public:
    ResUnsubscribeHandler() : MessageHandler(MODULE_MSG_UNSUBSCRIBE) {}
    virtual bool received(Message &msg);
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
    bool getParts(NamedList& dest, const char* src, const char sep, bool nameFirst);
    void createAuthRandomString(String& dest);
    void processPresence(const JabberID& local, const JabberID& remote,
	bool available, bool audio, const char* error);
    // Create a media string from a list
    void createMediaString(String& dest, ObjList& formats, char sep);

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

/**
 * YJBPresence
 */
YJBPresence::YJBPresence(JBEngine* engine, const NamedList& params)
    : JBPresence(engine),
      m_mutexUserpair(true)
{
    initialize(params);
}

YJBPresence::~YJBPresence()
{
    cleanup();
}

void YJBPresence::initialize(const NamedList& params)
{
    m_autoSubscribe = YUserPresence::subscribeType(params.getValue("auto_subscribe"));
    m_delUnavailable = params.getBoolValue("delete_unavailable",true);
    if (debugAt(DebugAll)) {
	String s;
	s << "\r\nauto_subscribe=" << YUserPresence::subscribeText(m_autoSubscribe);
	s << "\r\ndelete_unavailable=" << String::boolText(m_delUnavailable);
	Debug(this,DebugAll,"Initialized:%s",s.c_str());
    }
}

void YJBPresence::processDisco(JBEvent* event)
{
    if (!event)
	return;
    if (event->type() == JBEvent::IqDiscoRes)
	return;
    // Set/Get info or items
    if (!event->child())
	return;
    XMPPNamespace::Type ns = XMPPNamespace::type(event->child()->getAttribute("xmlns"));
    bool info = (ns == XMPPNamespace::DiscoInfo);
    JabberID local(event->to());
    JabberID remote(event->from());
    Lock lock(m_mutexUserpair);
    bool found = false;
    // Try to match the local user. Remote user is searched by full jid
    ObjList* obj = m_userpair.skipNull();
    for(; obj; obj = obj->skipNext()) {
	YUserPresence* yup = static_cast<YUserPresence*>(obj->get());
	if (!(yup->local().match(local) && remote == yup->remote()))
	    continue;
	found = true;
	if (info)
	    yup->sendInfo(event->id(),event->stream());
	else
	    yup->sendItems(event->id(),event->stream());
    }
    if (found)
	return;
    // No local user: add one if it's in our domain
    String identity;
    if (!m_engine->getFullServerIdentity(identity) || identity != local.domain())
	return;
    DDebug(this,DebugInfo,"Adding new user presence on info request.");
    // Add just the bare jids: we know nothing about remote user's capabilities
    YUserPresence* yup = new YUserPresence(this,local.bare(),remote.bare(),
	(YUserPresence::Subscription)getSubscription(local,remote),YUserPresence::Unknown);
    if (info)
	yup->sendInfo(event->id(),event->stream());
    else
	yup->sendItems(event->id(),event->stream());
}

void YJBPresence::processError(JBEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,"processError. Event: (%p).",event);
    JabberID local(event->to());
    JabberID remote(event->from());
    Lock lock(m_mutexUserpair);
    ObjList* obj = m_userpair.skipNull();
    for(; obj; obj = obj->skipNext()) {
	YUserPresence* yup = static_cast<YUserPresence*>(obj->get());
	if (local == yup->local() && remote == yup->remote())
	    yup->processError(event);
    }
}

void YJBPresence::processProbe(JBEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,
	"processProbe. Event: (%p). From: '%s' To: '%s'.",
	event,event->from().c_str(),event->to().c_str());
    JabberID local(event->to());
    if (!local.node()) {
    	Debug(this,DebugNote,"processProbe. Received probe without user.");
	return;
    }
    JabberID remote(event->from());
    Lock lock(m_mutexUserpair);
    bool found = false;
    // Try to match the local user. Check the remote user's full jid
    ObjList* obj = m_userpair.skipNull();
    for(; obj; obj = obj->skipNext()) {
	YUserPresence* yup = static_cast<YUserPresence*>(obj->get());
	if (!(yup->local().match(local) && yup->remote().match(remote)))
	    continue;
	found = true;
	XDebug(this,DebugAll,"processProbe. Sending presence from existing %p.",yup);
	yup->send();
    }
    if (found)
	return;
    // Remote has a resource: check if we have a pair whose remote user has no resource
    if (remote.resource()) {
	obj = m_userpair.skipNull();
	for(; obj; obj = obj->skipNext()) {
	    YUserPresence* yup = static_cast<YUserPresence*>(obj->get());
	    if (!(!yup->remote().resource() &&
		yup->local().match(local) &&
		remote.bare() == yup->remote().bare()))
		continue;
	    found = true;
	    XDebug(this,DebugAll,"processProbe. Sending presence from existing %p.",yup);
	    yup->send();
	    break;
	}
    }
    if (found)
	return;
    // No local user: add one if it's in our domain
    String identity;
    if (!m_engine->getFullServerIdentity(identity) || identity != local.domain()) {
    	Debug(this,DebugMild,
	    "processProbe. Received probe for non-local domain: %s",local.c_str());
	return;
    }
    DDebug(this,DebugInfo,"Adding new local user on probe request.");
    // Add just the bare jids: we know nothing about remote user's capabilities
    new YUserPresence(this,local.bare(),remote.bare(),
	(YUserPresence::Subscription)getSubscription(local,remote),YUserPresence::Unknown);
}

void YJBPresence::processSubscribe(JBEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,
	"processSubscribe. Event: (%p). From: '%s' To: '%s'.",
	event,event->from().c_str(),event->to().c_str());
    processSubscribe(event,JBPresence::Subscribe);
}

void YJBPresence::processSubscribed(JBEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,
	"processSubscribed. Event: (%p). From: '%s' To: '%s'.",
	event,event->from().c_str(),event->to().c_str());
    processSubscribe(event,JBPresence::Subscribed);
}

void YJBPresence::processUnsubscribe(JBEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,
	"processUnsubscribe. Event: (%p). From: '%s' To: '%s'.",
	event,event->from().c_str(),event->to().c_str());
    processSubscribe(event,JBPresence::Unsubscribe);
}

void YJBPresence::processUnsubscribed(JBEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,
	"processUnsubscribed. Event: (%p). From: '%s' To: '%s'.",
	event,event->from().c_str(),event->to().c_str());
    processSubscribe(event,JBPresence::Unsubscribed);
}

void YJBPresence::processUnavailable(JBEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,
	"processUnavailable. Event: (%p). From: '%s' To: '%s'.",
	event,event->from().c_str(),event->to().c_str());
    if (!event->to())
	processBroadcast(event,false);
    else
	processDirected(event,false);
}

void YJBPresence::processUnknown(JBEvent* event)
{
    if (!(event && event->element()))
	return;
    XDebug(this,DebugAll,
	"processUnknown. Event: (%p). From: '%s' To: '%s'.",
	event,event->from().c_str(),event->to().c_str());
    // The type attribute should not be present
    const char* type = event->element()->getAttribute("type");
    if (type) {
	DDebug(this,DebugInfo,
	    "processUnknown [%p]. Event: (%p). Unknown type: '%s'.",
	    this,event,type);
	return;
    }
    if (!event->to())
	processBroadcast(event,true);
    else
	processDirected(event,true);
}

void YJBPresence::processBroadcast(JBEvent* event, bool available)
{
    JabberID remote(event->from());
    if (available && !remote.resource()) {
    	Debug(this,DebugNote,"processBroadcast. Received presence without resource.");
	return;
    }
    Lock lock(m_mutexUserpair);
    ListIterator iter(m_userpair);
    for(;;) {
	YUserPresence* yup = static_cast<YUserPresence*>(iter.get());
	if (!yup)
	    break;
	if (!yup->remote().match(remote))
	    continue;
	if (available)
	    yup->processUnknown(event);
	else {
	    yup->processUnavailable(event);
	    if (delUnavailable())
		yup->deref();
	}
    }
}

void YJBPresence::processDirected(JBEvent* event, bool available)
{
    JabberID local(event->to());
    if (!local.node()) {
    	Debug(this,DebugNote,"processDirected. Received presence without destination user.");
	return;
    }
    JabberID remote(event->from());
    if (available && !remote.resource()) {
    	Debug(this,DebugNote,"processDirected. Received presence without resource.");
	return;
    }
    XDebug(this,DebugAll,
	"processDirected. Local: '%s' Remote: '%s'. Available: %s",
	local.c_str(),remote.c_str(),available?"YES":"NO");
    Lock lock(m_mutexUserpair);
    bool found = false;
    // Try to match local and remote user
    ListIterator iter(m_userpair);
    for(;;) {
	YUserPresence* yup = static_cast<YUserPresence*>(iter.get());
	if (!yup)
	    break;
	if (!(yup->local().match(local) && yup->remote().match(remote)))
	    continue;
	found = true;
	if (available)
	    yup->processUnknown(event);
	else {
	    yup->processUnavailable(event);
	    if (delUnavailable())
		yup->deref();
	}
    }
    if (found)
	return;
    // Check if remote has a resource, is available and we have
    //   some pair without remote resource
    if (remote.resource() && available) {
	iter.reset();
	for(;;) {
	    YUserPresence* yup = static_cast<YUserPresence*>(iter.get());
	    if (!yup)
		break;
	    if (!(!yup->remote().resource() &&
		yup->local().match(local) &&
		remote.bare() == yup->remote().bare()))
		continue;
	    found = true;
	    yup->processUnknown(event);
	    break;
	}
    }
    // Don't add if found or unavailable and we have to delete the unavailable users
    if (found || (!available && delUnavailable()))
	return;
    // No local user: add one if it's in our domain
    String identity;
    if (!m_engine->getFullServerIdentity(identity) || identity != local.domain())
	return;
    DDebug(this,DebugInfo,"Adding new local user on presence message.");
    new YUserPresence(this,local,remote,
	(YUserPresence::Subscription)getSubscription(local,remote),
	YUserPresence::Unknown);
}

void YJBPresence::processSubscribe(JBEvent* event, JBPresence::Presence type)
{
    JabberID local(event->to());
    JabberID remote(event->from());
    XDebug(m_engine,DebugAll,"processSubscribe. Local '%s'. Remote '%s'.",
	local.c_str(),remote.c_str());
    Lock lock(m_mutexUserpair);
    ObjList* obj = m_userpair.skipNull();
    YUserPresence* yup = 0;
    // Check if we already have an user
    for(; obj; obj = obj->skipNext()) {
	yup = static_cast<YUserPresence*>(obj->get());
	if (local.bare() == yup->local().bare() &&
	    remote.bare() == yup->remote().bare())
	    break;
	yup = 0;
    }
    // Not found: add one if it's a subscribe request for an user in our domain
    if (!yup && type == JBPresence::Subscribe) {
	String identity;
	if (!m_engine->getFullServerIdentity(identity) ||
	    identity != local.domain())
	    return;
	DDebug(this,DebugAll,"Adding new local user on subscription request.");
	yup = new YUserPresence(this,local.bare(),remote.bare(),
	    YUserPresence::SubNone,YUserPresence::Unknown);
    }
    if (!yup)
	return;
    // Confirm
    bool from,value;
    switch (type) {
	case JBPresence::Subscribe:
	    // Already subscribed to us: Confirm subscription
	    if (yup->subscribedFrom()) {
		yup->send(JBPresence::Subscribed);
		return;
	    }
	    // Approve if auto subscribing
	    if ((autoSubscribe() & YUserPresence::SubFrom) &&
		!yup->send(JBPresence::Subscribed))
		return;
	    from = true;
	    value = true;
	    DDebug(m_engine,DebugAll,"processSubscribe. Subscribing.");
	    break;
	case JBPresence::Subscribed:
	    // Already subscribed to remote user: do nothing
	    if (yup->subscribedTo())
		return;
	    from = false;
	    value = true;
	    break;
	case JBPresence::Unsubscribe:
	    // Already unsubscribed from us: confirm it
	    if (!yup->subscribedFrom()) {
		yup->send(JBPresence::Unsubscribed);
		return;
	    }
	    // Approve if auto subscribing
	    if ((autoSubscribe() & YUserPresence::SubFrom) &&
		!yup->send(JBPresence::Unsubscribed))
		return;
	    from = true;
	    value = false;
	    DDebug(m_engine,DebugAll,"processSubscribe. Unsubscribing.");
	    break;
	case JBPresence::Unsubscribed:
	    // If not subscribed to remote user ignore the unsubscribed confirmation
	    if (!yup->subscribedTo())
		return;
	    from = false;
	    value = false;
	    break;
	default:
	    return;
    }
    // Update subscription
    for (obj = m_userpair.skipNull(); obj; obj = obj->skipNext()) {
	YUserPresence* tmp = static_cast<YUserPresence*>(obj->get());
	if (local.bare() == tmp->local().bare() &&
	    remote.bare() == tmp->remote().bare())
	    tmp->updateSubscription(from,value);
    }
    // Notify engine
    subscribe(yup,type); 
}

int YJBPresence::getSubscription(const JabberID& local, const JabberID& remote)
{
    Lock lock(m_mutexUserpair);
    ObjList* obj = m_userpair.skipNull();
    for(; obj; obj = obj->skipNext()) {
	YUserPresence* yup = static_cast<YUserPresence*>(obj->get());
	if (local.bare() == yup->local().bare() &&
	    remote.bare() == yup->remote().bare())
	    return yup->subscription();
    }
    return (int)(YUserPresence::SubFrom);
}

void YJBPresence::startThreads(u_int16_t process)
{
    // Process the received events
    if (!process)
	Debug(m_engine,DebugWarn,"No threads(s) to process events!");
    for (; process; process--)
	(new YJGLibThread(YJGLibThread::JBPresence,"JBPresence thread"))->startup();
}

bool YJBPresence::get(const JabberID& local, JabberID& remote, bool& newPresence, bool audio)
{
    Lock lock(m_mutexUserpair);
    ObjList* obj = m_userpair.skipNull();
    YUserPresence* yup = 0;
    for(; obj; obj = obj->skipNext()) {
	yup = static_cast<YUserPresence*>(obj->get());
	if (audio && !yup->audio()) {
	    yup = 0;
	    continue;
	}
	// Local user is searched by bare jid
	// Remote user is searched by full or bare jid
	if (local.bare() == yup->local().bare() &&
	    yup->remote().match(remote)) {
	    // We found a remote user for the local one. Set the resource
	    remote.resource(yup->remote().resource());
	    if (iplugin.m_jg->requestSubscribe()) {
		bool avail = yup->available();
		yup->send(JBPresence::Subscribe);
		// Simulate a new presence while we get an answer
		newPresence = !avail;
		return avail;
	    }
	    break;
	}
	yup = 0;
    }
    newPresence = (yup == 0);
    if (newPresence)
	yup = new YUserPresence(this,local,remote,YUserPresence::SubFrom,
	    YUserPresence::Unknown);
    return yup->available();
}

void YJBPresence::notify(const YUserPresence* yup, const char* error)
{
    if (!yup)
	return;
    iplugin.processPresence(yup->local(),yup->remote(),yup->available(),yup->audio(),error);
    if (error)
	return;
    // Enqueue message
    XDebug(this,DebugInfo,"Enqueue '%s' for (%p).",MODULE_MSG_NOTIFY,yup);
    Message* m = new Message(MODULE_MSG_NOTIFY);
    m->addParam("module",MODULE_NAME);
    m->addParam("protocol",MODULE_PROTOCOL);
    m->addParam("subscription",YUserPresence::subscribeText(yup->subscription()));
    m->addParam("presence",YUserPresence::stateText(yup->remoteState()));
    m->addParam("from_user",yup->remote().bare());
    m->addParam("from_resource",yup->remote().resource());
    m->addParam("from_uri",yup->remote());
    m->addParam("to_user",yup->local().node());
    m->addParam("to_resource",yup->local().resource());
    m->addParam("to_uri",yup->local());
    Engine::enqueue(m);
}

void YJBPresence::subscribe(const YUserPresence* yup, JBPresence::Presence type)
{
    if (!yup)
	return;
    // Check type
    const char* message = 0;
    bool request = true;
    switch (type) {
	case JBPresence::Subscribed:
	    request = false;
	case JBPresence::Subscribe:
	    message = MODULE_MSG_SUBSCRIBE;
	    break;
	case JBPresence::Unsubscribed:
	    request = false;
	case JBPresence::Unsubscribe:
	    message = MODULE_MSG_UNSUBSCRIBE;
	    break;
	default:
	    return;
    }
    XDebug(this,DebugInfo,"Enqueue '%s' for (%p).",message,yup);
    Message* m = new Message(message);
    m->addParam("module",MODULE_NAME);
    m->addParam("protocol",MODULE_PROTOCOL);
    m->addParam("subscription",YUserPresence::subscribeText(yup->subscription()));
    m->addParam("from_user",yup->remote().bare());
    m->addParam("to_user",yup->local().node());
    Engine::enqueue(m);
}

void YJBPresence::addPresence(YUserPresence* yup)
{
    if (!yup)
	return;
    Lock lock(m_mutexUserpair);
    m_userpair.append(yup);
}

void YJBPresence::removePresence(YUserPresence* yup)
{
    if (!yup)
	return;
    Lock lock(m_mutexUserpair);
    m_userpair.remove(yup,false);
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
		if (!connection->route())
		    event->session()->jingleConn(0);
	    }
	    else
		DDebug(this,DebugWarn,
		    "processEvent. Session ref failed for new connection.");
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
 * YUserPresence
 */
TokenDict YUserPresence::s_subscription[] = {
	{"none", SubNone},
	{"to",   SubTo},
	{"from", SubFrom},
	{"both", SubBoth},
	{0,0},
	};

TokenDict YUserPresence::s_state[] = {
	{"unknown", Unknown},
	{"available",   Available},
	{"unavailable", Unavailable},
	{0,0},
	};

YUserPresence::YUserPresence(YJBPresence* engine, const char* local,
	const char* remote, Subscription subscription, State state)
    : Mutex(true),
      m_local(local),
      m_remote(remote),
      m_localState(Unknown),
      m_remoteState(Unknown),
      m_subscription(SubNone),
      m_audio(false),
      m_engine(engine)
{
    if (!m_local.resource())
	m_local.resource(JINGLE_RESOURCE);
    DDebug(m_engine,DebugNote, "YUserPresence. Local: %s. Remote: %s. [%p]",
	m_local.c_str(),m_remote.c_str(),this);
    m_engine->addPresence(this);
    // Update state
    if (state == Available || state == Unavailable) {
	m_remoteState = state;
	updateState((state == Available));
    }
    // Update subscription
    switch (subscription) {
	case SubNone:
	    break;
	case SubBoth:
	    updateSubscription(true,true);
	    updateSubscription(false,true);
	    break;
	case SubFrom:
	    updateSubscription(true,true);
	    break;
	case SubTo:
	    updateSubscription(false,true);
	    break;
    }
    // Subscribe to remote user if not already subscribed and auto subscribe is true
    if (!subscribedTo() && (m_engine->autoSubscribe() & SubTo))
	send(JBPresence::Subscribe);
    // Request remote's presence
    if (remoteState() == Unknown)
	send(JBPresence::Probe);
}

YUserPresence::~YUserPresence()
{
    // Make us unavailable to remote peer
    if (subscribedFrom() && available() && m_localState != Unavailable)
	send(JBPresence::Unavailable);
    m_engine->removePresence(this);
    DDebug(m_engine,DebugNote, "~YUserPresence. Local: %s. Remote: %s. [%p]",
	m_local.c_str(),m_remote.c_str(),this);
}

bool YUserPresence::send(JBPresence::Presence type, bool caps,
	JBComponentStream* stream)
{
    bool localStream;
    XDebug(m_engine,DebugAll,"YUserPresence. Sending presence '%s'. [%p]",
	JBPresence::presenceText(type),this);
    if (!getStream(stream,localStream))
	return false;
    // Create the element to send
    XMLElement* xml = 0;
    Lock lock(this);
    switch (type) {
	// Types that need only the bare jid
	case JBPresence::Probe:
	    xml = JBPresence::createPresence(m_local,m_remote.bare(),type);
	    break;
	case JBPresence::Unavailable:
	    xml = JBPresence::createPresence(m_local,m_remote,type);
	    break;
	case JBPresence::Subscribe:
	case JBPresence::Subscribed:
	case JBPresence::Unsubscribe:
	case JBPresence::Unsubscribed:
	    xml = JBPresence::createPresence(m_local.bare(),m_remote.bare(),type);
	    break;
	// Types that need the full jid
	case JBPresence::None:
	    xml = JBPresence::createPresence(m_local,m_remote.bare());
	    if (caps) {
		XMLElement* c = new XMLElement("c");
		c->setAttribute("xmlns","http://jabber.org/protocol/caps");
		c->setAttribute("node","http://www.google.com/xmpp/client/caps");
		c->setAttribute("ver",JINGLE_VERSION);
		c->setAttribute("ext",JINGLE_VOICE);
		xml->addChild(c);
	    }
	    break;
	case JBPresence::Error:
	    return false;
    }
    lock.drop();
    bool result = sendStanza(stream,xml);
    if (localStream)
	stream->deref();
    // Update local state
    if (result &&
	(type == JBPresence::None || type == JBPresence::Unavailable)) {
	m_localState = (type == JBPresence::None ? Available : Unavailable);
	return true;
    }
    // Set subscribe data. Not for subscribe/unsubscribe
    if (type == JBPresence::Subscribed || type == JBPresence::Unsubscribed)
	updateSubscription(true,type == JBPresence::Subscribed);
    return result;
}

bool YUserPresence::sendInfoRequest(bool info, JBComponentStream* stream)
{
    bool localStream;
    if (!getStream(stream,localStream))
	return false;
    XMLElement* xml = XMPPUtils::createIqDisco(m_local,m_remote.bare(),
	String((int)random()),info);
    bool result = sendStanza(stream,xml);
    if (localStream)
	stream->deref();
    return result;
}

bool YUserPresence::sendInfo(const char* id, JBComponentStream* stream)
{
    bool localStream;
    if (!getStream(stream,localStream))
	return false;
    // Create response
    XMLElement* query = XMPPUtils::createElement(XMLElement::Query,
	XMPPNamespace::DiscoInfo);
    // Set features
    XMPPNamespace::Type ns[2] = {XMPPNamespace::Jingle,
				 XMPPNamespace::JingleAudio};
    JIDFeatures* f = new JIDFeatures();
    f->create(ns,2);
    f->addTo(query);
    f->deref();
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqResult,m_local,m_remote,id);
    iq->addChild(query);
    // Send
    bool result = sendStanza(stream,iq);
    if (localStream)
	stream->deref();
    return result;
}

bool YUserPresence::sendItems(const char* id, JBComponentStream* stream)
{
    bool localStream;
    if (!getStream(stream,localStream))
	return false;
    // Create response
    XMLElement* query = XMPPUtils::createElement(XMLElement::Query,
	XMPPNamespace::DiscoItems);
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqResult,m_local,m_remote,id);
    iq->addChild(query);
    // Send
    bool result = sendStanza(stream,iq);
    if (localStream)
	stream->deref();
    return result;
}

void YUserPresence::processError(JBEvent* event)
{
    String code, type, error;
    JBPresence::decodeError(event->element(),code,type,error);
    DDebug(m_engine,DebugAll,"YUserPresence. Error. '%s'. Code: '%s'. [%p]",
	error.c_str(),code.c_str(),this);
    m_engine->notify(this,error);
}

void YUserPresence::processUnavailable(JBEvent* event)
{
    Lock lock(this);
    // Return if we already know that remote user is unavailable
    if (m_remoteState == Unavailable)
	return;
    JabberID jid(event->from());
    // Remote has no resource: broadcast unavailable
    if (jid.resource() && m_remote.resource() && m_remote.resource() != jid.resource())
	return;
    XDebug(m_engine,DebugAll,"YUserPresence::processUnavailable. [%p]",this);
    updateState(false);
}

void YUserPresence::processUnknown(JBEvent* event)
{
    Lock lock(this);
    // Get resource from presence
    JabberID jid(event->from());
    // We already have a resource
    if (m_remote.resource() && m_remote.resource() != jid.resource())
	return;
    XDebug(m_engine,DebugAll,
	"YUserPresence::processPresence. From '%s' to '%s'. [%p]",
	event->from().c_str(),event->to().c_str(),this);
    m_remote.resource(jid.resource());
    updateResource(event->element());
    // Success: Send our presence and capabilities if not already done
    if (m_localState != Available)
	send(JBPresence::None,true,event->stream());
    if (remoteState() != Available)
	updateState(true);
}

void YUserPresence::updateResource(XMLElement* element)
{
    if (!element)
	return;
    NamedList caps("");
    XMLElement* c = element->findFirstChild("c");
    if (c)
	iplugin.getParts(caps,c->getAttribute("ext"),' ',true);
    if (caps.getParam(JINGLE_VOICE))
	m_audio = true;
    else
	m_audio = false;
}

void YUserPresence::updateSubscription(bool from, bool value)
{
    // Don't update if nothing changed
    if ((from && value == subscribedFrom()) ||
	(!from && value == subscribedTo()))
	return;
    // Update
    int s = (from ? SubFrom : SubTo);
    if (value)
	m_subscription |= s;
    else
	m_subscription &= ~s;
    DDebug(m_engine,DebugNote,
	"YUserPresence. Subscription updated. State '%s'. [%p]",
	subscribeText(m_subscription),this);
    // Send presence if remote user is subscribed to us
    if (from && subscribedFrom()) {
	send(JBPresence::Unavailable);
	send();
    }

}

void YUserPresence::updateState(bool available)
{
    // Update
    m_remoteState = (available ? Available : Unavailable);
    DDebug(m_engine,DebugNote,
	"YUserPresence. Remote presence updated. State: '%s'. Audio: %s. [%p]",
	stateText(m_remoteState),m_audio?"YES":"NO",this);
    // Notify on user presence
    m_engine->notify(this);
}

bool YUserPresence::getStream(JBComponentStream*& stream, bool& release)
{
    release = false;
    if (stream)
	return true;
    if (!m_engine->engine())
	return false;
    stream = m_engine->engine()->getStream();
    if (stream) {
	release = true;
	return true;
    }
    Debug(m_engine,DebugGoOn,"YUserPresence. No stream to send data. [%p]",this);
    return false;
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
    //TODO: What if no format available ?
    // *** TRANSPORT
    //TODO: Transport from message if forward
}

YJGTransport::~YJGTransport()
{
    if (m_remote)
	m_remote->deref();
}

bool YJGTransport::initLocal()
{
    if (!m_connection)
	return false;
    Lock lock(this);
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
    Debug(m_connection,DebugCall,"Transport. Start. Local: '%s:%s'. Remote: '%s:%s'. [%p]",
	m_address.c_str(),m_port.c_str(),
	m_remote->m_address.c_str(),m_remote->m_port.c_str(),m_connection);
    Message* m = new Message("chan.rtp");
    m->userData(static_cast<CallEndpoint*>(m_connection));
    m_connection->complete(*m);
    m->addParam("direction","bidir");
    m->addParam("media","audio");
    //TODO: Add real media
#if 0
    String formats;
    createMediaString(formats);
    m->addParam("formats",formats);
    //TODO: Choose one format
#endif
    m->addParam("format","alaw");
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
    // chan.stun
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
	DDebug(m_connection,DebugWarn,
	    "Transport. The remote party has no media. [%p]",m_connection);
	m_connection->hangup(false,"nomedia");
	return false;
    }
    ListIterator iter_local(m_formats);
    for (GenObject* go; (go = iter_local.get());) {
	JGAudio* local = static_cast<JGAudio*>(go);
	// Check if incoming media contains local media (compare 'id' and 'name')
	bool exists = false;
	ObjList* obj = media.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    JGAudio* remote = static_cast<JGAudio*>(obj->get());
	    if (local->m_id == remote->m_id && local->m_name == remote->m_name) {
		exists = true;
		break;
	    }
	}
	// Remove from local if not exists
	if (!exists)
	    m_formats.remove(local,true);
	// Remove from remote
	if (obj)
	    media.remove(obj,true);
    }
    // Check if both parties have common media
    if (0 == m_formats.skipNull()) {
	DDebug(m_connection,DebugWarn,
	    "Transport. Unable to negotiate media (no common formats). [%p]",
	    m_connection);
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
	    m_type == remote->m_type &&
	    m_network == remote->m_network)
	    break;
	// We hate it: reset and skip
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
    bool first = true;
    ObjList* obj = m_formats.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(obj->get());
	const char* payload = lookup(a->m_id.toInteger(),dict_payloads);
	if (!payload)
	    continue;
	if (first) {
	    dest = payload;
	    first = false;
	}
	else
	    dest << "," << payload;
    }
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
      m_hangup(false),
      m_timeout(0)
{
    XDebug(this,DebugInfo,"YJGConnection. Outgoing. [%p]",this);
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
    XDebug(this,DebugInfo,"YJGConnection. Time: " FMT64 ". Maxcall set to " FMT64 " us. [%p]",Time::now(),maxcall(),this);
    // Startup
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    if (msg) {
	m_targetid = msg.getValue("id");
	m->setParam("caller",msg.getValue("caller"));
	m->setParam("called",msg.getValue("called"));
	m->setParam("billid",msg.getValue("billid"));
    }
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
      m_hangup(false),
      m_timeout(0)
{
    XDebug(this,DebugInfo,"YJGConnection. Incoming. [%p]",this);
    // Set session
    m_session->jingleConn(this);
    // Init transport
    m_transport = new YJGTransport(this);
    m_transport->updateMedia(event->audio());
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
    if (m_session)
	m_session->deref();
    if (m_transport)
	m_transport->deref();
    XDebug(this,DebugInfo,"~YJGConnection [%p].",this);
}

bool YJGConnection::route()
{
    Message* m = message("call.preroute",false,true);
    m->addParam("username",m_remote.node());
    m->addParam("called",m_local.node());
    m->addParam("caller",m_remote.node());
    m->addParam("callername",m_remote.bare());
    if (m_transport->remote()) {
	m->addParam("ip_host",m_transport->remote()->m_address);
	m->addParam("ip_port",m_transport->remote()->m_port);
    }
    return startRouter(m);
}

void YJGConnection::callAccept(Message& msg)
{
    // Init local transport
    // Accept session and transport
    // Request transport
    // Try to start transport
    Debug(this,DebugCall,"callAccept. [%p]",this);
    m_transport->initLocal();
    m_session->accept(m_transport->createDescription());
    m_session->acceptTransport(0);
    m_transport->send(m_session);
    m_transport->start();
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
    hangup(true);
}

bool YJGConnection::callRouted(Message& msg)
{
    DDebug(this,DebugCall,"callRouted. [%p]",this);
    return true;
}

void YJGConnection::disconnected(bool final, const char* reason)
{
    DDebug(this,DebugCall,"disconnected. Final: %u. Reason: '%s'. [%p]",
	final,reason,this);
    if (!m_reason && reason)
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
    DDebug(this,DebugCall,"msgText. [%p]",this);
    if (m_session) {
	m_session->sendMessage(text);
	return true;
    }
    return false;
}

void YJGConnection::hangup(bool reject, const char* reason)
{
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
	    DDebug(this,DebugCall,
		"handleEvent((%p): %u). Error. Id: '%s'. Reason: '%s'. Text: '%s'. [%p]",
		event,event->type(),event->id().c_str(),event->reason().c_str(),
		event->text().c_str(),this);
	    break;
	case JGEvent::Message: {
	    DDebug(this,DebugCall,
		"handleEvent((%p): %u). Message text: '%s'. [%p]",
		event,event->type(),event->text().c_str(),this);
	    Message* m = message("chan.text");
	    m->addParam("text",event->text());
            Engine::enqueue(m);
	    }
	    break;
	default:
	    DDebug(this,DebugCall,"handleEvent((%p): %u). [%p]",
		event,event->type(),event);
    }
}

void YJGConnection::handleJingle(JGEvent* event)
{
    switch (event->action()) {
	case JGSession::ActTransportInfo:
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

bool YJGConnection::processPresence(bool available, const char* error)
{
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
		u_int32_t wait = m_stream->waitBeforeConnect() ?
		    iplugin.m_jb->waitStreamRestart() : 0;
		DDebug(iplugin.m_jb,DebugAll,
		    "%s started. Stream (%p). Remote: '%s'. Waiting: %u ms.",
		    name(),m_stream,m_stream->remoteName().c_str(),wait);
		if (wait)
		    Thread::msleep(wait,true);
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
    }
    DDebug(iplugin.m_jb,DebugAll,"%s end of run.",name());
}


/**
 * resource.notify message handler
 */
bool ResNotifyHandler::received(Message &msg)
{
    XDebug(&iplugin,DebugAll,"Received '%s'.",MODULE_MSG_NOTIFY);
    if (MODULE_NAME == msg.getValue("module")) {
	XDebug(&iplugin,DebugAll,"'%s' loopback. Ignoring.",MODULE_MSG_NOTIFY);
	return false;
    }
    return false;
}

/**
 * resource.subscribe message handler
 */
bool ResSubscribeHandler::received(Message &msg)
{
    XDebug(&iplugin,DebugAll,"Received '%s'.",MODULE_MSG_SUBSCRIBE);
    if (MODULE_NAME == msg.getValue("module")) {
	XDebug(&iplugin,DebugAll,"'%s' loopback. Ignoring.",MODULE_MSG_SUBSCRIBE);
	return false;
    }
    return false;
}

/**
 * resource.unsubscribe message handler
 */
bool ResUnsubscribeHandler::received(Message &msg)
{
    XDebug(&iplugin,DebugAll,"Received '%s'.",MODULE_MSG_UNSUBSCRIBE);
    if (MODULE_NAME == msg.getValue("module")) {
	XDebug(&iplugin,DebugAll,"'%s' loopback. Ignoring.",MODULE_MSG_UNSUBSCRIBE);
	return false;
    }
    return false;
}

/**
 * YJGDriver
 */
YJGDriver::YJGDriver()
    : Driver(MODULE_NAME,"varchans"),
      m_jb(0), m_presence(0), m_jg(0), m_init(false), m_pendingTimeout(0)
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
    if (m_init)
	return;
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
    Engine::install(new ResUnsubscribeHandler);
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
	const char* password = comp->getValue("password");
	const char* identity = comp->getValue("identity","yate");
	bool startup = comp->getBoolValue("startup");
	if (!(address && port && identity))
	    continue;
	if (defComponent.null() || comp->getBoolValue("default"))
	    defComponent = name;
	JBServerInfo* server = new JBServerInfo(name,address,port,
	    password,identity);
	XDebug(this,DebugAll,"Add server '%s' addr=%s port=%d pass=%s ident=%s startup=%s.",
	    name.c_str(),address,port,password,identity,
	    String::boolText(startup));
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
	m_presence->startThreads(process);
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
    const char* c = msg.getValue("caller");
    if (!c)
	c = iplugin.anonymousCaller();
    JabberID caller(c,identity,JINGLE_RESOURCE);
    JabberID called(dest);
    DDebug(this,DebugAll,"msgExecute. Caller: '%s'. Called: '%s'.",
	caller.c_str(),called.c_str());
    bool newPresence = true;
    bool available = iplugin.m_presence->get(caller,called,newPresence);
    if (!(newPresence || available)) {
	Debug(this,DebugNote,"Jingle call failed. Remote peer is unavailable.");
	msg.setParam("error","offline");
	return false;
    }
    // Parameters OK. Create connection and init channel
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

void YJGDriver::createAuthRandomString(String& dest)
{
    dest = "";
    for (; dest.length() < JINGLE_AUTHSTRINGLEN;)
 	dest << (int)random();
    dest = dest.substr(0,JINGLE_AUTHSTRINGLEN);
}

void YJGDriver::processPresence(const JabberID& local, const JabberID& remote,
	bool available, bool audio, const char* error)
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
	if (conn->processPresence(false,error))
	    conn->disconnect();
    }
    unlock();
}

void YJGDriver::createMediaString(String& dest, ObjList& formats, char sep)
{
    dest = "";
    bool first = true;
    ObjList* obj = formats.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JGAudio* a = static_cast<JGAudio*>(obj->get());
	const char* payload = lookup(a->m_id.toInteger(),dict_payloads);
	if (!payload)
	    continue;
	if (first) {
	    dest = payload;
	    first = false;
	}
	else
	    dest << sep << payload;
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
