/**
 * jbengine.cpp
 * Yet Another Jabber Component Protocol Stack
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatejabber.h>

using namespace TelEngine;

/**
 * JBEngine
 */

static XMPPNamespace s_ns;

// Default values
#define JB_STREAM_RESTART_COUNT        2 // Stream restart counter default value
#define JB_STREAM_RESTART_COUNT_MIN    1
#define JB_STREAM_RESTART_COUNT_MAX   10

#define JB_STREAM_RESTART_UPDATE   15000 // Stream restart counter update interval
#define JB_STREAM_RESTART_UPDATE_MIN 5000
#define JB_STREAM_RESTART_UPDATE_MAX 300000

// Sleep time for threads
#define SLEEP_READSOCKET               2 // Read socket
#define SLEEP_PROCESSPRESENCE          2 // Process presence events
#define SLEEP_PROCESSTIMEOUT          10 // Process presence timeout 

// Presence values
#define PRESENCE_PROBE_INTERVAL  1800000
#define PRESENCE_EXPIRE_INTERVAL  300000

#define JINGLE_VERSION            "1.0"  // Version capability
#define JINGLE_VOICE         "voice-v1"  // Voice capability for Google Talk

JBEngine::JBEngine()
    : Mutex(true),
      m_clientsMutex(true),
      m_presence(0),
      m_identity(0),
      m_restartUpdateTime(0),
      m_restartUpdateInterval(JB_STREAM_RESTART_UPDATE),
      m_restartCount(JB_STREAM_RESTART_COUNT),
      m_streamID(0),
      m_serverMutex(true)
{
    debugName("jbengine");
    m_identity = new JIDIdentity(JIDIdentity::Gateway,JIDIdentity::GatewayGeneric);
    //m_features.add(XMPPNamespace::DiscoInfo);
    //m_features.add(XMPPNamespace::Command);
    m_features.add(XMPPNamespace::Jingle);
    m_features.add(XMPPNamespace::JingleAudio);
    m_features.add(XMPPNamespace::Dtmf);
    XDebug(this,DebugAll,"JBEngine. [%p]",this);
}

JBEngine::~JBEngine()
{
    cleanup();
    if (m_identity)
	m_identity->deref();
    XDebug(this,DebugAll,"~JBEngine. [%p]",this);
}

void JBEngine::initialize(const NamedList& params)
{
    clearServerList();
    // Alternate domain names
    m_alternateDomain = params.getValue("extra_domain");
    // Stream restart update interval
    m_restartUpdateInterval =
	params.getIntValue("stream_restartupdateinterval",JB_STREAM_RESTART_UPDATE);
    if (m_restartUpdateInterval < JB_STREAM_RESTART_UPDATE_MIN)
	m_restartUpdateInterval = JB_STREAM_RESTART_UPDATE_MIN;
    else
	if (m_restartUpdateInterval > JB_STREAM_RESTART_UPDATE_MAX)
	    m_restartUpdateInterval = JB_STREAM_RESTART_UPDATE_MAX;
    // Stream restart count
    m_restartCount = 
	params.getIntValue("stream_restartcount",JB_STREAM_RESTART_COUNT);
    if (m_restartCount < JB_STREAM_RESTART_COUNT_MIN)
	m_restartCount = JB_STREAM_RESTART_COUNT_MIN;
    else
	if (m_restartCount > JB_STREAM_RESTART_COUNT_MAX)
	    m_restartCount = JB_STREAM_RESTART_COUNT_MAX;
    // XML parser max receive buffer
    XMLParser::s_maxDataBuffer =
	params.getIntValue("xmlparser_maxbuffer",XMLPARSER_MAXDATABUFFER);
    // Default resource
    m_defaultResource = params.getValue("default_resource","yate");
    if (debugAt(DebugAll)) {
	String s;
	s << "\r\ndefault_resource=" << m_defaultResource;
	s << "\r\nstream_restartupdateinterval=" << (unsigned int)m_restartUpdateInterval;
	s << "\r\nstream_restartcount=" << (unsigned int)m_restartCount;
	s << "\r\nxmlparser_maxbuffer=" << (unsigned int)XMLParser::s_maxDataBuffer;
	Debug(this,DebugAll,"Initialized:%s",s.c_str());
    }
}

void JBEngine::cleanup()
{
    Lock lock(this);
    DDebug(this,DebugAll,"Cleanup.");
    ObjList* obj = m_streams.skipNull();
    for (; obj; obj = m_streams.skipNext()) {
	JBComponentStream* s = static_cast<JBComponentStream*>(obj->get());
	s->terminate(true,true,XMPPUtils::createStreamError(XMPPError::Shutdown),true);
    }
}

void JBEngine::setComponentServer(const char* domain)
{
    Lock lock(m_serverMutex);
    JBServerInfo* p = getServer(domain,true);
    // If doesn't exists try to get the first one from the list
    if (!p) {
	ObjList* obj = m_server.skipNull();
	p = obj ? static_cast<JBServerInfo*>(obj->get()) : 0;
    }
    if (!p) {
	Debug(this,DebugNote,"No default component server.");
	return;
    }
    m_componentDomain = p->name();
    m_componentAddr = p->address();
    DDebug(this,DebugAll,"Default component server set to '%s' (%s).",
	m_componentDomain.c_str(),m_componentAddr.c_str());
}

JBComponentStream* JBEngine::getStream(const char* domain, bool create)
{
    Lock lock(this);
    if (!domain)
	domain = m_componentDomain;
    JBComponentStream* stream = findStream(domain);
    XDebug(this,DebugAll,
	"getStream. Remote: '%s'. Stream exists: %s (%p). Create: %s.",
	domain,stream?"YES":"NO",stream,create?"YES":"NO");
    if (!stream && create) {
	SocketAddr addr(PF_INET);
	addr.host(domain);
	addr.port(getPort(addr.host()));
	stream = new JBComponentStream(this,domain,addr);
	m_streams.append(stream);
    }
    return ((stream && stream->ref()) ? stream : 0);
}

bool JBEngine::receive()
{
    bool ok = false;
    lock();
    ListIterator iter(m_streams);
    for (;;) {
	JBComponentStream* stream = static_cast<JBComponentStream*>(iter.get());
        // End of iteration?
	if (!stream)
	    break;
	// Get a reference
	RefPointer<JBComponentStream> sref = stream;
	if (!sref)
	    continue;
	// Read socket
	unlock();
	if (sref->receive())
	    ok = true;
	lock();
    }
    unlock();
    return ok;
}

void JBEngine::runReceive()
{
    while (1) {
	if (!receive())
	    Thread::msleep(SLEEP_READSOCKET,true);
    }
}

JBEvent* JBEngine::getEvent(u_int64_t time)
{
    lock();
    ListIterator iter(m_streams);
    for (;;) {
	JBComponentStream* stream = static_cast<JBComponentStream*>(iter.get());
        // End of iteration?
	if (!stream)
	    break;
	// Get a reference
	RefPointer<JBComponentStream> sref = stream;
	if (!sref)
	    continue;
	// Get event
	unlock();
	JBEvent* event = sref->getEvent(time);
	if (event)
	    // Check if the engine should process these events
	    switch (event->type()) {
		case JBEvent::Presence:
		    if (!(m_presence && m_presence->receive(event)))
			return event;
		    break;
		case JBEvent::IqDiscoGet:
		case JBEvent::IqDiscoSet:
		case JBEvent::IqDiscoRes: {
		    JabberID jid(event->to());
		    if (jid.node()) {
			if (!(m_presence && m_presence->receive(event)))
			    return event;
			break;
		    }
		    if (!processDiscoInfo(event))
			return event;
		    break;
		    }
		case JBEvent::IqCommandGet:
		case JBEvent::IqCommandSet:
		case JBEvent::IqCommandRes: {
		    JabberID jid(event->to());
		    if (!processCommand(event))
			return event;
		    break;
		    }
		case JBEvent::Invalid:
		    event->deref();
		    break;
		case JBEvent::Terminated:
		    // Restart stream
		    connect(event->stream());
		    event->deref();
		    break;
		// Unhandled 'iq' element
		case JBEvent::Iq:
		    DDebug(this,DebugInfo,
			"getEvent. Event((%p): %u). Unhandled 'iq' element.",event,event->type());
		    stream->sendIqError(event->releaseXML(),XMPPError::TypeCancel,
			XMPPError::SFeatureNotImpl);
		    event->deref();
		    break;
		default:
		    return event;
	    }
	lock();
    }
    unlock();
    return 0;
}

bool JBEngine::remoteIdExists(const JBComponentStream* stream)
{
    // IDs for incoming streams are generated by this engine
    // Check the stream source and ID
    if (!stream)
	return false;
    Lock lock(this);
    ObjList* obj = m_streams.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JBComponentStream* s = static_cast<JBComponentStream*>(obj->get());
	if (s != stream &&
	    s->remoteName() == stream->remoteName() &&
	    s->id() == stream->id())
	    return true;
    }
    return false;
}

void JBEngine::createSHA1(String& sha, const String& id,
	const String& password)
{
    SHA1 sha1;
    sha1 << id << password;
    sha = sha1.hexDigest();
}

bool JBEngine::checkSHA1(const String& sha, const String& id,
	const String& password)
{
    String tmp;
    createSHA1(tmp,id,password);
    return tmp == sha;
}

void JBEngine::timerTick(u_int64_t time)
{
    if (m_restartUpdateTime > time)
	return;
    // Update server streams counters
    Lock lock(m_serverMutex);
    ObjList* obj = m_server.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JBServerInfo* server = static_cast<JBServerInfo*>(obj->get());
	if (server->restartCount() < m_restartCount) {
	    server->incRestart();
	    DDebug(this,DebugAll,
		"Increased stream restart counter (%u) for '%s'.",
		server->restartCount(),server->name().c_str());
	}
	// Check if stream should be restarted (created)
	if (server->autoRestart()) {
	    JBComponentStream* stream = getStream(server->name(),true);
	    if (stream)
		stream->deref();
	}
    }
    // Update next update time
    m_restartUpdateTime = time + m_restartUpdateInterval;
}

bool JBEngine::connect(JBComponentStream* stream)
{
    if (!stream)
	return false;
    stream->connect();
    return true;
}

void JBEngine::returnEvent(JBEvent* event)
{
    if (!event)
	return;
    if (event->type() == JBEvent::Message && processMessage(event))
	return;
    DDebug(this,DebugAll,
	"returnEvent. Delete event((%p): %u).",event,event->type());
    event->deref();
}

bool JBEngine::acceptOutgoing(const String& remoteAddr,
	String& password)
{
    bool b = getServerPassword(password,remoteAddr,false);
    XDebug(this,DebugAll,
	"acceptOutgoing. To: '%s'. %s.",remoteAddr.c_str(),
	b ? "Accepted" : "Not accepted");
    return b;
}

int JBEngine::getPort(const String& remoteAddr)
{
    int port = 0;
    getServerPort(port,remoteAddr,false);
    XDebug(this,DebugAll,
	"getPort. For: '%s'. Port: %d",remoteAddr.c_str(),port);
    return port;
}

void JBEngine::appendServer(JBServerInfo* server, bool open)
{
    if (!server)
	return;
    // Add if doesn't exists. Delete if already in the list
    JBServerInfo* p = getServer(server->name(),true);
    if (!p) {
	m_serverMutex.lock();
	m_server.append(server);
	m_serverMutex.unlock();
    }
    else
	server->deref();
    // Open stream
    if (open) {
	JBComponentStream* stream = getStream(server->name());
	if (stream)
	    stream->deref();
    }
}

bool JBEngine::getServerIdentity(String& destination,
	const char* token, bool domain)
{
    Lock lock(m_serverMutex);
    JBServerInfo* server = getServer(token,domain);
    if (!server)
	return false;
    destination = server->identity();
    return true;
}

bool JBEngine::getFullServerIdentity(String& destination, const char* token,
	bool domain)
{
    Lock lock(m_serverMutex);
    JBServerInfo* server = getServer(token,domain);
    if (!server)
	return false;
    destination = server->fullIdentity();
    return true;
}

bool JBEngine::getStreamRestart(const char* token, bool domain)
{
    Lock lock(m_serverMutex);
    JBServerInfo* server = getServer(token,domain);
    if (!(server && server->getRestart()))
	return false;
    DDebug(this,DebugAll,
	"Decreased stream restart counter (%u) for '%s'.",
	server->restartCount(),server->name().c_str());
    return true;
}

bool JBEngine::processDiscoInfo(JBEvent* event)
{
    JBComponentStream* stream = event->stream();
    // Check if we have a stream and this engine is the destination
    if (!(stream && event->element()))
	return false;
    //TODO: Check if the engine is the destination.
    //      The destination might be a user
    switch (event->type()) {
	case JBEvent::IqDiscoGet:
	    {
	    DDebug(this,DebugAll,"processDiscoInfo. Get. From '%s' to '%s'.",
		event->from().c_str(),event->to().c_str());
	    // Create response
	    XMLElement* query = XMPPUtils::createElement(XMLElement::Query,
		XMPPNamespace::DiscoInfo);
	    // Set the identity & features
	    if (m_identity) {
		*(String*)(m_identity) = stream->localName();
		query->addChild(m_identity->toXML());
	    }
	    m_features.addTo(query);
	    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqResult,event->to(),
		event->from(),event->id());
	    iq->addChild(query);
	    // Send response
	    stream->sendStanza(iq);
	    }
	    break;
	case JBEvent::IqDiscoRes:
	    DDebug(this,DebugAll,"processDiscoInfo. Result.  From '%s' to '%s'.",
		event->from().c_str(),event->to().c_str());
	    break;
	case JBEvent::IqDiscoSet:
	    DDebug(this,DebugAll,"processDiscoInfo. Set. From '%s' to '%s'.",
		event->from().c_str(),event->to().c_str());
	    break;
	default:
	    Debug(this,DebugNote,"processDiscoInfo. From '%s' to '%s'. Unhandled.",
		event->from().c_str(),event->to().c_str());
    }
    // Release event
    event->deref();
    return true;
}

bool JBEngine::processCommand(JBEvent* event)
{
    JBComponentStream* stream = event->stream();
    if (!(event && event->element() && event->child())) {
	event->deref();
	return true;
    }
    //TODO: Check if the engine is the destination.
    //      The destination might be a user
    switch (event->type()) {
	case JBEvent::IqCommandGet:
	case JBEvent::IqCommandSet:
	    DDebug(this,DebugAll,"processCommand. From '%s' to '%s'.",
		event->from().c_str(),event->to().c_str());
	    stream->sendIqError(event->releaseXML(),XMPPError::TypeCancel,
		    XMPPError::SFeatureNotImpl);
	    break;
	case JBEvent::IqDiscoRes:
	    DDebug(this,DebugAll,"processCommand. Result. From '%s' to '%s'.",
		event->from().c_str(),event->to().c_str());
	    break;
	default:
	    Debug(this,DebugNote,"processCommand. From '%s' to '%s'. Unhandled.",
		event->from().c_str(),event->to().c_str());
    }
    // Release event
    event->deref();
    return true;
}

JBComponentStream* JBEngine::findStream(const String& remoteName)
{
    ObjList* obj = m_streams.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JBComponentStream* stream = static_cast<JBComponentStream*>(obj->get());
	if (stream->remoteName() == remoteName)
	    return stream;
    }
    return 0;
}

void JBEngine::removeStream(JBComponentStream* stream, bool del)
{
    if (!m_streams.remove(stream,del))
	return;
    DDebug(this,DebugAll,
	"removeStream (%p). Deleted: %s.",stream,del?"YES":"NO");
}

void JBEngine::addClient(JBClient* client)
{
    if (!client)
	return;
    // Check existence
    Lock lock(m_clientsMutex);
    if (m_clients.find(client))
	return;
    m_clients.append(client);
}

void JBEngine::removeClient(JBClient* client)
{
    if (!client)
	return;
    Lock lock(m_clientsMutex);
    m_clients.remove(client,false);
}

JBServerInfo* JBEngine::getServer(const char* token, bool domain)
{
    if (!token)
	token = domain ? m_componentDomain : m_componentAddr;
    if (!token)
	return 0;
    ObjList* obj = m_server.skipNull();
    if (domain)
	for (; obj; obj = obj->skipNext()) {
	    JBServerInfo* server = static_cast<JBServerInfo*>(obj->get());
	    if (server->name() == token)
		return server;
	}
    else
	for (; obj; obj = obj->skipNext()) {
	    JBServerInfo* server = static_cast<JBServerInfo*>(obj->get());
	    if (server->address() == token)
		return server;
	}
    return 0;
}

bool JBEngine::processMessage(JBEvent* event)
{
    DDebug(this,DebugInfo,
	"JBEngine::processMessage. From: '%s'. To: '%s'.",
	event->from().c_str(),event->to().c_str());
    return false;
}

bool JBEngine::getServerPassword(String& destination,
	const char* token, bool domain)
{
    Lock lock(m_serverMutex);
    JBServerInfo* server = getServer(token,domain);
    if (!server)
	return false;
    destination = server->password();
    return true;
}

bool JBEngine::getServerPort(int& destination,
	const char* token, bool domain)
{
    Lock lock(m_serverMutex);
    JBServerInfo* server = getServer(token,domain);
    if (!server)
	return false;
    destination = server->port();
    return true;
}

void JBEngine::setPresenceServer(JBPresence* presence)
{
    if (m_presence)
	return;
    Lock lock(this);
    if (presence)
	m_presence = presence;
}

void JBEngine::unsetPresenceServer(JBPresence* presence)
{
    Lock lock(this);
    if (m_presence == presence)
	m_presence = 0;
}

/**
 * JBEvent
 */
JBEvent::JBEvent(Type type, JBComponentStream* stream)
    : m_type(type),
      m_stream(0),
      m_link(true),
      m_element(0),
      m_child(0)
{
    init(stream,0);
}

JBEvent::JBEvent(Type type, JBComponentStream* stream,
	XMLElement* element, XMLElement* child)
    : m_type(type),
      m_stream(0),
      m_link(true),
      m_element(element),
      m_child(child)
{
    if (!init(stream,element))
	m_type = Invalid;
}

JBEvent::JBEvent(Type type, JBComponentStream* stream,
	XMLElement* element, const String& senderID)
    : m_type(type),
      m_stream(0),
      m_link(true),
      m_element(element),
      m_child(0),
      m_id(senderID)
{
    if (!init(stream,element))
	m_type = Invalid;
}

JBEvent::~JBEvent()
{
    if (m_stream) {
	releaseStream();
	m_stream->deref();
    }
    if (m_element)
	delete m_element;
    XDebug(DebugAll,"JBEvent::~JBEvent. [%p]",this);
}

void JBEvent::releaseStream()
{
    if (m_link && m_stream) {
	m_stream->eventTerminated(this);
	m_link = false;
    }
}

bool JBEvent::init(JBComponentStream* stream, XMLElement* element)
{
    bool bRet = true;
    if (stream && stream->ref())
	m_stream = stream;
    else
	bRet = false;
    m_element = element;
    XDebug(DebugAll,"JBEvent::JBEvent. Type: %u. Stream (%p). Element: (%p). [%p]",
	m_type,m_stream,m_element,this);
    return bRet;
}

/**
 * JBClient
 */
JBClient::JBClient(JBEngine* engine)
    : m_engine(0)
{
    if (engine && engine->ref()) {
	m_engine = engine;
	m_engine->addClient(this);
    }
}

JBClient::~JBClient()
{
    if (m_engine) {
	m_engine->removeClient(this);
	m_engine->deref();
    }
}

/**
 * JIDResource
 */
TokenDict JIDResource::s_show[] = {
	{"away",   ShowAway},
	{"chat",   ShowChat},
	{"dnd",    ShowDND},
	{"xa",     ShowXA},
	{0,0},
	};

bool JIDResource::setPresence(bool value)
{
    Presence p = value ? Available : Unavailable;
    if (m_presence == p)
	return false;
    m_presence = p;
    return true;
}

bool JIDResource::fromXML(XMLElement* element)
{
    if (!(element && element->type() == XMLElement::Presence))
	return false;
    JBPresence::Presence p = JBPresence::presenceType(element->getAttribute("type"));
    if (p != JBPresence::None && p != JBPresence::Unavailable)
	return false;
    // Show
    XMLElement* tmp = element->findFirstChild("show");
    m_show = tmp ? showType(tmp->getText()) : ShowNone;
    // Status
    tmp = element->findFirstChild("status");
    m_status = tmp ? tmp->getText() : "";
    // Capability
    bool capsChanged = false;
    tmp = element->findFirstChild("c");
    if (tmp) {
	NamedList caps("");
	if (XMPPUtils::split(caps,tmp->getAttribute("ext"),' ',true)) {
	    // Check audio
	    bool tmp = (0 != caps.getParam(JINGLE_VOICE));
	    if (tmp != hasCap(CapAudio)) {
		capsChanged = true;
		if (tmp)
		    m_capability |= CapAudio;
		else
		    m_capability &= ~CapAudio;
	    }
	}
    }
    // Presence
    bool presenceChanged = setPresence(p == JBPresence::None);
    // Return
    return capsChanged || presenceChanged;
}

void JIDResource::addTo(XMLElement* element)
{
    if (!element)
	return;
    if (m_show != ShowNone)
	element->addChild(new XMLElement("show",0,showText(m_show)));
    element->addChild(new XMLElement("status",0,m_status));
    // Add priority
    XMLElement* priority = new XMLElement("priority",0,"25");
    element->addChild(priority);
    // Add capabilities
    XMLElement* c = new XMLElement("c");
    c->setAttribute("xmlns","http://jabber.org/protocol/caps");
    c->setAttribute("node","http://www.google.com/xmpp/client/caps");
    c->setAttribute("ver",JINGLE_VERSION);
    if (hasCap(CapAudio))
	c->setAttribute("ext",JINGLE_VOICE);
    element->addChild(c);
}

/**
 * JIDResourceList
 */
bool JIDResourceList::add(const String& name)
{
    if (get(name))
	return false;
    m_resources.append(new JIDResource(name));
    return true;
}

bool JIDResourceList::add(JIDResource* resource)
{
    if (!resource)
	return false;
    if (get(resource->name())) {
	resource->deref();
	return false;
    }
    m_resources.append(resource);
    return true;
}

JIDResource* JIDResourceList::get(const String& name)
{
    ObjList* obj = m_resources.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JIDResource* res = static_cast<JIDResource*>(obj->get());
	if (res->name() == name)
	    return res;
    }
    return 0;
}

JIDResource* JIDResourceList::getAudio(bool availableOnly)
{
    ObjList* obj = m_resources.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JIDResource* res = static_cast<JIDResource*>(obj->get());
	if (res->hasCap(JIDResource::CapAudio) &&
	    (!availableOnly || (availableOnly && res->available())))
	    return res;
    }
    return 0;
}

/**
 * XMPPUser
 */
TokenDict XMPPUser::s_subscription[] = {
	{"none", None},
	{"to",   To},
	{"from", From},
	{"both", Both},
	{0,0},
	};

XMPPUser::XMPPUser(XMPPUserRoster* local, const char* node, const char* domain,
	Subscription sub, bool subTo, bool sendProbe)
    : Mutex(true),
      m_local(0),
      m_jid(node,domain),
      m_subscription(None),
      m_nextProbe(0),
      m_expire(0)
{
    if (local && local->ref())
	m_local = local;
    if (!m_local)
	Debug(m_local->engine(),DebugFail,
	    "XMPPUser. No local user for '%s'. [%p]",m_jid.c_str(),this);
    else
	DDebug(m_local->engine(),DebugNote,
	    "XMPPUser. Local: (%p): %s. Remote: %s. [%p]",
	    m_local,m_local->jid().c_str(),m_jid.c_str(),this);
    m_local->addUser(this);
    // Update subscription
    switch (sub) {
	case None:
	    break;
	case Both:
	    updateSubscription(true,true,0);
	    updateSubscription(false,true,0);
	    break;
	case From:
	    updateSubscription(true,true,0);
	    break;
	case To:
	    updateSubscription(false,true,0);
	    break;
    }
    // Subscribe to remote user if not already subscribed and auto subscribe is true
    if (subTo || (!subscribedTo() && (m_local->engine()->autoSubscribe() & To)))
	sendSubscribe(JBPresence::Subscribe,0);
    // Probe remote user
    if (sendProbe)
	probe(0);
}

XMPPUser::~XMPPUser()
{
    DDebug(m_local->engine(),DebugNote, "~XMPPUser. Local: %s. Remote: %s. [%p]",
	m_local->jid().c_str(),m_jid.c_str(),this);
    // Remove all local resources: this will make us unavailable
    clearLocalRes();
    m_local->removeUser(this);
    m_local->deref();
}

bool XMPPUser::addLocalRes(JIDResource* resource)
{
    if (!resource)
	return false;
    Lock lock(this);
    if (!m_localRes.add(resource))
	return false;
    DDebug(m_local->engine(),DebugAll,
	"XMPPUser. Added local resource '%s'. Audio: %s. [%p]",
	resource->name().c_str(),
	resource->hasCap(JIDResource::CapAudio)?"YES":"NO",this);
    resource->setPresence(true);
    if (subscribedFrom())
	sendPresence(resource,0,true);
    return true;
}

void XMPPUser::removeLocalRes(JIDResource* resource)
{
    if (!(resource && m_localRes.get(resource->name())))
	return;
    Lock lock(this);
    resource->setPresence(false);
    if (subscribedFrom())
	sendPresence(resource,0);
    m_localRes.remove(resource);
    DDebug(m_local->engine(),DebugAll,
	"XMPPUser. Removed local resource '%s'. [%p]",
	resource->name().c_str(),this);
}

void XMPPUser::clearLocalRes()
{
    Lock lock(this);
    m_localRes.clear();
    if (subscribedFrom())
	sendUnavailable(0);
}

void XMPPUser::processDisco(JBEvent* event)
{
    if (event->type() == JBEvent::IqDiscoRes)
	return;
    const String* id = 0;
    if (event->id())
	id = &(event->id());
    // Check query
    if (!event->child()) {
	m_local->engine()->sendError(XMPPError::SFeatureNotImpl,event->to(),
	    event->from(),event->releaseXML(),event->stream(),id);
	return;
    }
    XMPPNamespace::Type type;
    if (event->child()->hasAttribute("xmlns",s_ns[XMPPNamespace::DiscoInfo]))
	type = XMPPNamespace::DiscoInfo;
    else if (event->child()->hasAttribute("xmlns",s_ns[XMPPNamespace::DiscoItems]))
	type = XMPPNamespace::DiscoItems;
    else {
	m_local->engine()->sendError(XMPPError::SFeatureNotImpl,event->to(),
	    event->from(),event->releaseXML(),event->stream(),id);
	return;
    }
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,event->to(),
	event->from(),event->id());
    iq->addChild(XMPPUtils::createElement(XMLElement::Query,type));
    m_local->engine()->sendStanza(iq,event->stream());
}

void XMPPUser::processError(JBEvent* event)
{
    String code, type, error;
    JBPresence::decodeError(event->element(),code,type,error);
    DDebug(m_local->engine(),DebugAll,"XMPPUser. Error. '%s'. Code: '%s'. [%p]",
	error.c_str(),code.c_str(),this);
}

void XMPPUser::processProbe(JBEvent* event, const String* resName)
{
    updateTimeout(true);
    XDebug(m_local->engine(),DebugAll,"XMPPUser. Process probe. [%p]",this);
    if (resName)
	notifyResource(false,*resName,event->stream(),true);
    else
	notifyResources(false,event->stream(),true);
}

bool XMPPUser::processPresence(JBEvent* event, bool available, const JabberID& from)
{
    updateTimeout(true);
    // No resource ?
    Lock lock(&m_remoteRes);
    if (from.resource().null()) {
	// Available without resource ? No way !!!
	if (available)
	    return true;
	// Check if should notify
	bool notify = true;
	// User is unavailable for all resources
	ListIterator iter(m_remoteRes.m_resources);
	GenObject* obj;
	for (; (obj = iter.get());) {
	    JIDResource* res = static_cast<JIDResource*>(obj);
	    if (res->setPresence(false)) {
		DDebug(m_local->engine(),DebugNote,
		    "XMPPUser. Local user (%p). Resource (%s) changed state: %s. Audio: %s. [%p]",
		    m_local,res->name().c_str(),res->available()?"available":"unavailable",
		    res->hasCap(JIDResource::CapAudio)?"YES":"NO",this);
		notify = false;
		m_local->engine()->notifyPresence(this,res);
	    }
	    if (m_local->engine()->delUnavailable())
		m_remoteRes.m_resources.remove(res,true);
		
	}
	if (notify)
	    m_local->engine()->notifyPresence(event,m_local->jid(),m_jid,false);
	// No more resources ? Remove user
	if (!m_remoteRes.getFirst() && m_local->engine()->delUnavailable())
	    return false;
	// Notify local resources to remote user if not already done
	if (subscribedFrom())
	    notifyResources(false,event->stream(),false);
	return true;
    }
    // 'from' has a resource: check if we already have one
    ObjList* obj = m_remoteRes.m_resources.skipNull();
    JIDResource* res = 0;
    for (; obj; obj = obj->skipNext()) {
	res = static_cast<JIDResource*>(obj->get());
	if (res->name() == from.resource())
	    break;
	res = 0;
    }
    // Add a new resource if we don't have one
    if (!res) {
	res = new JIDResource(from.resource());
	m_remoteRes.add(res);
	DDebug(m_local->engine(),DebugNote,
	    "XMPPUser. Local user (%p). Added new remote resource (%s). [%p]",
	    m_local,res->name().c_str(),this);
    }
    // Changed: notify
    if (res->fromXML(event->element())) {
	DDebug(m_local->engine(),DebugNote,
	    "XMPPUser. Local user (%p). Resource (%s) changed state: %s. Audio: %s. [%p]",
	    m_local,res->name().c_str(),res->available()?"available":"unavailable",
	    res->hasCap(JIDResource::CapAudio)?"YES":"NO",this);
	m_local->engine()->notifyPresence(this,res);
    }
    if (!available && m_local->engine()->delUnavailable()) {
	m_remoteRes.m_resources.remove(res,true);
	// No more resources ? Remove user
	if (!m_remoteRes.getFirst())
	    return false;
    }
    // Notify local resources to remote user if not already done
    if (subscribedFrom())
	notifyResources(false,event->stream(),false);
    return true;
}

void XMPPUser::processSubscribe(JBEvent* event, JBPresence::Presence type)
{
    Lock lock(this);
    switch (type) {
	case JBPresence::Subscribe:
	    // Already subscribed to us: Confirm subscription
	    if (subscribedFrom()) {
		sendSubscribe(JBPresence::Subscribed,event->stream());
		return;
	    }
	    // Approve if auto subscribing
	    if ((m_local->engine()->autoSubscribe() & From))
		sendSubscribe(JBPresence::Subscribed,event->stream());
	    DDebug(m_local->engine(),DebugAll,
		"XMPPUser. processSubscribe. Subscribing. [%p]",this);
	    break;
	case JBPresence::Subscribed:
	    // Already subscribed to remote user: do nothing
	    if (subscribedTo())
		return;
	    updateSubscription(false,true,event->stream());
	    DDebug(m_local->engine(),DebugAll,
		"XMPPUser. processSubscribe. Subscribed. [%p]",this);
	    break;
	case JBPresence::Unsubscribe:
	    // Already unsubscribed from us: confirm it
	    if (!subscribedFrom()) {
		sendSubscribe(JBPresence::Unsubscribed,event->stream());
		return;
	    }
	    // Approve if auto subscribing
	    if ((m_local->engine()->autoSubscribe() & From))
		sendSubscribe(JBPresence::Unsubscribed,event->stream());
	    DDebug(m_local->engine(),DebugAll,
		"XMPPUser. processSubscribe. Unsubscribing. [%p]",this);
	    break;
	case JBPresence::Unsubscribed:
	    // If not subscribed to remote user ignore the unsubscribed confirmation
	    if (!subscribedTo())
		return;
	    updateSubscription(false,false,event->stream());
	    DDebug(m_local->engine(),DebugAll,
		"XMPPUser. processSubscribe. Unsubscribed. [%p]",this);
	    break;
	default:
	    return;
    }
    // Notify
    m_local->engine()->notifySubscribe(this,type);
}

bool XMPPUser::probe(JBComponentStream* stream, u_int64_t time)
{
    Lock lock(this);
    updateTimeout(false,time);
    XDebug(m_local->engine(),DebugAll,"XMPPUser. Sending '%s'. [%p]",
	JBPresence::presenceText(JBPresence::Probe),this);
    XMLElement* xml = JBPresence::createPresence(m_local->jid().bare(),m_jid.bare(),
	JBPresence::Probe);
    return m_local->engine()->sendStanza(xml,stream);
}

bool XMPPUser::sendSubscribe(JBPresence::Presence type, JBComponentStream* stream)
{
    Lock lock(this);
    bool from = false;
    bool value = false;
    switch (type) {
	case JBPresence::Subscribed:
	    from = true;
	    value = true;
	    break;
	case JBPresence::Unsubscribed:
	    from = true;
	    break;
	case JBPresence::Subscribe:
	case JBPresence::Unsubscribe:
	    break;
	default:
	    return false;
    }
    XDebug(m_local->engine(),DebugAll,"XMPPUser. Sending '%s'. [%p]",
	JBPresence::presenceText(type),this);
    XMLElement* xml = JBPresence::createPresence(m_local->jid().bare(),m_jid.bare(),type);
    bool result = m_local->engine()->sendStanza(xml,stream);
    // Set subscribe data. Not for subscribe/unsubscribe
    if (from && result)
	updateSubscription(true,value,stream);
    return result;
}

bool XMPPUser::timeout(u_int64_t time)
{
    Lock lock(this);
    if (!m_expire) {
	if (m_nextProbe < time)
	    probe(0,time);
	return false;
    }
    if (m_expire > time)
	return false;
    // Timeout. Clear resources & Notify
    Debug(m_local->engine(),DebugNote,
	"XMPPUser. Local user (%p). Timeout. [%p]",m_local,this);
    m_remoteRes.clear();
    m_local->engine()->notifyPresence(0,m_jid,m_local->jid(),false);
    return true;
}

bool XMPPUser::sendPresence(JIDResource* resource, JBComponentStream* stream,
	bool force)
{
    Lock lock(this);
    // Send presence for all resources
    JabberID from(m_local->jid().node(),m_local->jid().domain());
    if (!resource) {
	ObjList* obj = m_localRes.m_resources.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    JIDResource* res = static_cast<JIDResource*>(obj->get());
	    from.resource(res->name());
	    XMLElement* xml = JBPresence::createPresence(from,m_jid.bare(),
		res->available() ? JBPresence::None : JBPresence::Unavailable);
	    // Add capabilities
	    if (res->available())
		res->addTo(xml);
	    m_local->engine()->sendStanza(xml,stream);
	}
	return true;
    }
    // Don't send if already done and not force
    if (resource->presence() != JIDResource::Unknown && !force)
	return false;
    from.resource(resource->name());
    XMLElement* xml = JBPresence::createPresence(from,m_jid.bare(),
	resource->available() ? JBPresence::None : JBPresence::Unavailable);
    // Add capabilities
    if (resource->available())
	resource->addTo(xml);
    return m_local->engine()->sendStanza(xml,stream);
}

void XMPPUser::notifyResource(bool remote, const String& name,
	JBComponentStream* stream, bool force)
{
    if (remote) {
	Lock lock(&m_remoteRes);
	JIDResource* res = m_remoteRes.get(name);
	if (res)
	    m_local->engine()->notifyPresence(this,res);
	return;
    }
    Lock lock(&m_localRes);
    JIDResource* res = m_localRes.get(name);
    if (res)
	sendPresence(res,stream,force);
}

void XMPPUser::notifyResources(bool remote, JBComponentStream* stream, bool force)
{
    if (remote) {
	Lock lock(&m_remoteRes);
	ObjList* obj = m_remoteRes.m_resources.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    JIDResource* res = static_cast<JIDResource*>(obj->get());
	    m_local->engine()->notifyPresence(this,res);
	}
	return;
    }
    Lock lock(&m_localRes);
    ObjList* obj = m_localRes.m_resources.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JIDResource* res = static_cast<JIDResource*>(obj->get());
	sendPresence(res,stream,force);
    }
}

bool XMPPUser::sendUnavailable(JBComponentStream* stream)
{
    XDebug(m_local->engine(),DebugAll,"XMPPUser. Sending 'unavailable'. [%p]",this);
    XMLElement* xml = JBPresence::createPresence(m_local->jid().bare(),m_jid.bare(),
	JBPresence::Unavailable);
    return m_local->engine()->sendStanza(xml,stream);
}

void XMPPUser::updateSubscription(bool from, bool value, JBComponentStream* stream)
{
    Lock lock(this);
    // Don't update if nothing changed
    if ((from && value == subscribedFrom()) ||
	(!from && value == subscribedTo()))
	return;
    // Update
    int s = (from ? From : To);
    if (value)
	m_subscription |= s;
    else
	m_subscription &= ~s;
    DDebug(m_local->engine(),DebugNote,
	"XMPPUser. Local user (%p). Subscription updated. State '%s'. [%p]",
	m_local,subscribeText(m_subscription),this);
    // Send presence if remote user is subscribed to us
    if (from && subscribedFrom()) {
	sendUnavailable(stream);
	sendPresence(0,stream,true);
    }
}

void XMPPUser::updateTimeout(bool from, u_int64_t time)
{
    Lock lock(this);
    m_nextProbe = time + m_local->engine()->probeInterval();
    if (from)
	m_expire = 0;
    else
	m_expire = time + m_local->engine()->expireInterval();
}

/**
 * XMPPUserRoster
 */
XMPPUserRoster::XMPPUserRoster(JBPresence* engine, const char* node,
	const char* domain)
    : Mutex(true),
      m_jid(node,domain),
      m_engine(engine)
{
    m_engine->addRoster(this);
    DDebug(m_engine,DebugNote, "XMPPUserRoster. %s. [%p]",m_jid.c_str(),this);
}

XMPPUserRoster::~XMPPUserRoster()
{
    m_engine->removeRoster(this);
    lock();
    m_remote.clear();
    unlock();
    DDebug(m_engine,DebugNote, "~XMPPUserRoster. %s. [%p]",m_jid.c_str(),this);
}

XMPPUser* XMPPUserRoster::getUser(const JabberID& jid, bool add, bool* added)
{
    Lock lock(this);
    ObjList* obj = m_remote.skipNull();
    XMPPUser* u = 0;
    for (; obj; obj = obj->skipNext()) {
	u = static_cast<XMPPUser*>(obj->get());
	if (jid.bare() == u->jid().bare())
	    break;
	u = 0;
    }
    if (!u && !add)
	return 0;
    if (!u) {
	u = new XMPPUser(this,jid.node(),jid.domain(),XMPPUser::From);
	if (added)
	    *added = true;
    }
    return u->ref() ? u : 0;
}

bool XMPPUserRoster::removeUser(const JabberID& remote)
{
    Lock lock(this);
    ObjList* obj = m_remote.skipNull();
    for (; obj; obj = obj->skipNext()) {
	XMPPUser* u = static_cast<XMPPUser*>(obj->get());
	if (remote.bare() == u->jid().bare()) {
	    m_remote.remove(u,true);
	    break;
	}
    }
    return (0 != m_remote.skipNull());
}

bool XMPPUserRoster::timeout(u_int64_t time)
{
    Lock lock(this);
    ListIterator iter(m_remote);
    GenObject* obj;
    for (; (obj = iter.get());) {
	XMPPUser* u = static_cast<XMPPUser*>(obj);
	if (u->timeout(time))
	    m_remote.remove(u,true);
    }
    return (0 == m_remote.skipNull());
}

/**
 * JBPresence
 */
TokenDict JBPresence::s_presence[] = {
	{"error",         Error},
	{"probe",         Probe},
	{"subscribe",     Subscribe},
	{"subscribed",    Subscribed},
	{"unavailable",   Unavailable},
	{"unsubscribe",   Unsubscribe},
	{"unsubscribed",  Unsubscribed},
	{0,0}
	};

JBPresence::JBPresence(JBEngine* engine, const NamedList& params)
    : JBClient(engine),
      Mutex(true),
      m_autoSubscribe((int)XMPPUser::None),
      m_delUnavailable(false),
      m_addOnSubscribe(false),
      m_addOnProbe(false),
      m_addOnPresence(false),
      m_autoProbe(true),
      m_probeInterval(0),
      m_expireInterval(0)
{
    debugName("jbpresence");
    XDebug(this,DebugAll,"JBPresence. [%p]",this);
    if (m_engine)
	m_engine->setPresenceServer(this);
    initialize(params);
}

JBPresence::~JBPresence()
{
    cleanup();
    if (m_engine)
	m_engine->unsetPresenceServer(this);
    m_events.clear();
    XDebug(this,DebugAll,"~JBPresence. [%p]",this);
}

void JBPresence::initialize(const NamedList& params)
{
    m_autoSubscribe = XMPPUser::subscribeType(params.getValue("auto_subscribe"));
    m_delUnavailable = params.getBoolValue("delete_unavailable",true);
    m_autoProbe = params.getBoolValue("auto_probe",true);
    if (m_engine) {
	String domain = m_engine->componentServer();
	String id = m_engine->getServerIdentity(domain);
	if (domain == id)
	    m_addOnSubscribe = m_addOnProbe = m_addOnPresence = true;
    }
    m_probeInterval = params.getIntValue("probe_interval",PRESENCE_PROBE_INTERVAL);
    m_expireInterval = params.getIntValue("expire_interval",PRESENCE_EXPIRE_INTERVAL);
    if (debugAt(DebugAll)) {
	String s;
	s << "\r\nauto_subscribe=" << XMPPUser::subscribeText(m_autoSubscribe);
	s << "\r\ndelete_unavailable=" << String::boolText(m_delUnavailable);
	s << "\r\nadd_onsubscribe=" << String::boolText(m_addOnSubscribe);
	s << "\r\nadd_onprobe=" << String::boolText(m_addOnProbe);
	s << "\r\nadd_onpresence=" << String::boolText(m_addOnPresence);
	s << "\r\nauto_probe=" << String::boolText(m_autoProbe);
	s << "\r\nprobe_interval=" << (unsigned int)m_probeInterval;
	s << "\r\nexpire_interval=" << (unsigned int)m_expireInterval;
	Debug(this,DebugAll,"Initialized:%s",s.c_str());
    }
}

bool JBPresence::receive(JBEvent* event)
{
    if (!event)
	return false;
    switch (event->type()) {
	case JBEvent::Presence:
	case JBEvent::IqDiscoGet:
	case JBEvent::IqDiscoSet:
	case JBEvent::IqDiscoRes:
	    break;
	default:
	    return false;
    }
    XDebug(this,DebugAll,"Received event.");
    Lock lock(this);
    event->releaseStream();
    m_events.append(event);
    return true;
}

bool JBPresence::process()
{
    Lock lock(this);
    ObjList* obj = m_events.skipNull();
    if (!obj)
	return false;
    JBEvent* event = static_cast<JBEvent*>(obj->get());
    m_events.remove(event,false);
    JabberID local(event->to());
    JabberID remote(event->from());
    switch (event->type()) {
	case JBEvent::IqDiscoGet:
	case JBEvent::IqDiscoSet:
	case JBEvent::IqDiscoRes:
	    if (checkDestination(event,local))
		processDisco(event,local,remote);
	    event->deref();
	    return true;
	default: ;
    }
    XDebug(this,DebugAll,"Process presence: '%s'.",event->stanzaType().c_str());
    Presence p = presenceType(event->stanzaType());
    switch (p) {
	case JBPresence::Error:
	    if (checkDestination(event,local))
		processError(event,local,remote);
	    break;
	case JBPresence::Probe:
	    if (checkDestination(event,local))
		processProbe(event,local,remote);
	    break;
	case JBPresence::Subscribe:
	case JBPresence::Subscribed:
	case JBPresence::Unsubscribe:
	case JBPresence::Unsubscribed:
	    if (checkDestination(event,local))
		processSubscribe(event,p,local,remote);
	    break;
	case JBPresence::Unavailable:
	    // Check destination only if we have one. No destination: broadcast
	    if (local && !checkDestination(event,local))
		break;
	    processUnavailable(event,local,remote);
	    break;
	default:
	    // Check destination only if we have one. No destination: broadcast
	    if (local && !checkDestination(event,local))
		break;
	    if (!event->element())
		break;
	    // Simple presence shouldn't have a type
	    if (event->element()->getAttribute("type")) {
		Debug(this,DebugNote,
		    "Received unexpected presence type '%s' from '%s' to '%s'.",
		    event->element()->getAttribute("type"),remote.c_str(),local.c_str());
		sendError(XMPPError::SFeatureNotImpl,local,remote,
		    event->releaseXML(),event->stream());
		break;
	    }
	    processPresence(event,local,remote);
    }
    event->deref();
    return true;
}

void JBPresence::runProcess()
{
    while(1) {
	if (!process())
	    Thread::msleep(SLEEP_PROCESSPRESENCE,true);
    }
}

void JBPresence::processDisco(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,
	"processDisco. Event: ((%p): %u). Local: '%s'. Remote: '%s'.",
	event,event->type(),local.c_str(),remote.c_str());
    XMPPUser* user = getRemoteUser(local,remote,false,0,false,0);
    if (!user) {
	DDebug(this,DebugNote,
	    "Received 'disco' for non user. Local: '%s'. Remote: '%s'.",
	    local.c_str(),remote.c_str());
	// Don't send error if it is a response
	if (event->type() != JBEvent::IqDiscoRes)
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    user->processDisco(event);
    user->deref();
}

void JBPresence::processError(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,
	"processError. Event: ((%p): %u). Local: '%s'. Remote: '%s'.",
	event,event->type(),local.c_str(),remote.c_str());
    XMPPUser* user = getRemoteUser(local,remote,false,0,false,0);
    if (!user) {
	DDebug(this,DebugNote,
	    "Received 'error' for non user. Local: '%s'. Remote: '%s'.",
	    local.c_str(),remote.c_str());
	return;
    }
    user->processError(event);
    user->deref();
}

void JBPresence::processProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,
	"processProbe. Event: ((%p): %u). Local: '%s'. Remote: '%s'.",
	event,event->type(),local.c_str(),remote.c_str());
    bool newUser = false;
    XMPPUser* user = getRemoteUser(local,remote,m_addOnProbe,0,m_addOnProbe,&newUser);
    if (!user) {
	DDebug(this,DebugNote,
	    "Received 'probe' for non user. Local: '%s'. Remote: '%s'.",
	    local.c_str(),remote.c_str());
	if (m_autoProbe) {
	    XMLElement* stanza = createPresence(local.bare(),remote);
	    JIDResource* resource = new JIDResource(m_engine->defaultResource(),JIDResource::Available,
		JIDResource::CapAudio);
	    resource->addTo(stanza);
	    resource->deref();
	    if (event->stream())
		event->stream()->sendStanza(stanza);
	    else
		delete stanza;
	}
	else if (!notifyProbe(event,local,remote))
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    String resName = local.resource();
    if (resName.null())
	user->processProbe(event);
    else
	user->processProbe(event,&resName);
    user->deref();
}

void JBPresence::processSubscribe(JBEvent* event, Presence presence,
	const JabberID& local, const JabberID& remote)
{
    XDebug(this,DebugAll,
	"Process '%s'. Event: ((%p): %u). Local: '%s'. Remote: '%s'.",
	presenceText(presence),event,event->type(),local.c_str(),remote.c_str());
    bool addLocal = (presence == Subscribe) ? m_addOnSubscribe : false;
    bool newUser = false;
    XMPPUser* user = getRemoteUser(local,remote,addLocal,0,addLocal,&newUser);
    if (!user) {
	DDebug(this,DebugNote,
	    "Received '%s' for non user. Local: '%s'. Remote: '%s'.",
	    presenceText(presence),local.c_str(),remote.c_str());
	if (!notifySubscribe(event,local,remote,presence) &&
	    (presence != Subscribed && presence != Unsubscribed))
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    user->processSubscribe(event,presence);
    user->deref();
}

void JBPresence::processUnavailable(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,
	"processUnavailable. Event: ((%p): %u). Local: '%s'. Remote: '%s'.",
	event,event->type(),local.c_str(),remote.c_str());
    // Don't add if delUnavailable is true
    bool addLocal = m_addOnPresence && !m_delUnavailable;
    // Check if broadcast
    if (local.null()) {
	Lock lock(this);
	ObjList* obj = m_rosters.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    XMPPUserRoster* roster = static_cast<XMPPUserRoster*>(obj->get());
	    bool newUser = false;
	    XMPPUser* user = getRemoteUser(roster->jid(),remote,addLocal,0,
		addLocal,&newUser);
	    if (!user)
		continue;
	    if (newUser)
		notifyNewUser(user);
	    if (!user->processPresence(event,false,remote))
		removeRemoteUser(local,remote);
	    user->deref();
	}
	return;
    }
    // Not broadcast: find user
    bool newUser = false;
    XMPPUser* user = getRemoteUser(local,remote,addLocal,0,addLocal,&newUser);
    if (!user) {
	DDebug(this,DebugNote,
	    "Received 'unavailable' for non user. Local: '%s'. Remote: '%s'.",
	    local.c_str(),remote.c_str());
	if (!notifyPresence(event,local,remote,false))
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    if (!user->processPresence(event,false,remote))
	removeRemoteUser(local,remote);
    user->deref();
}

void JBPresence::processPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,
	"processPresence. Event: ((%p): %u). Local: '%s'. Remote: '%s'.",
	event,event->type(),local.c_str(),remote.c_str());
    // Check if broadcast
    if (local.null()) {
	Lock lock(this);
	ObjList* obj = m_rosters.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    XMPPUserRoster* roster = static_cast<XMPPUserRoster*>(obj->get());
	    bool newUser = false;
	    XMPPUser* user = getRemoteUser(roster->jid(),remote,
		m_addOnPresence,0,m_addOnPresence,&newUser);
	    if (!user)
		continue;
	    if (newUser)
		notifyNewUser(user);
	    user->processPresence(event,true,remote);
	    user->deref();
	}
	return;
    }
    // Not broadcast: find user
    bool newUser = false;
    XMPPUser* user = getRemoteUser(local,remote,m_addOnPresence,0,m_addOnPresence,
	&newUser);
    if (!user) {
	DDebug(this,DebugNote,
	    "Received presence for non user. Local: '%s'. Remote: '%s'.",
	    local.c_str(),remote.c_str());
	if (!notifyPresence(event,local,remote,true))
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    user->processPresence(event,true,remote);
    user->deref();
}

bool JBPresence::notifyProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,
	"JBPresence::notifyProbe. Local: '%s'. Remote: '%s'.",
	local.c_str(),remote.c_str());
    return false;
}

bool JBPresence::notifySubscribe(JBEvent* event, const JabberID& local,
	const JabberID& remote, Presence presence)
{
    XDebug(this,DebugAll,
	"JBPresence::notifySubscribe(%s). Local: '%s'. Remote: '%s'.",
	presenceText(presence),local.c_str(),remote.c_str());
    return false;
}

void JBPresence::notifySubscribe(XMPPUser* user, Presence presence)
{
    XDebug(this,DebugAll,
	"JBPresence::notifySubscribe(%s). User: (%p).",
	presenceText(presence),user);
}

bool JBPresence::notifyPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote, bool available)
{
    XDebug(this,DebugAll,
	"JBPresence::notifyPresence(%s). Local: '%s'. Remote: '%s'.",
	available?"available":"unavailable",local.c_str(),remote.c_str());
    return false;
}

void JBPresence::notifyPresence(XMPPUser* user, JIDResource* resource)
{
    XDebug(this,DebugAll,
	"JBPresence::notifyPresence. User: (%p). Resource: (%p).",
	user,resource);
}

void JBPresence::notifyNewUser(XMPPUser* user)
{
    XDebug(this,DebugAll,"JBPresence::notifyNewUser. User: (%p).",user);
}

XMPPUserRoster* JBPresence::getRoster(const JabberID& jid, bool add, bool* added)
{
    if (jid.node().null() || jid.domain().null())
	return 0;
    Lock lock(this);
    ObjList* obj = m_rosters.skipNull();
    for (; obj; obj = obj->skipNext()) {
	XMPPUserRoster* ur = static_cast<XMPPUserRoster*>(obj->get());
	if (jid.bare() == ur->jid().bare())
	    return ur->ref() ? ur : 0;
    }
    if (!add)
	return 0;
    if (added)
	*added = true;
    XMPPUserRoster* ur = new XMPPUserRoster(this,jid.node(),jid.domain());
    return ur->ref() ? ur : 0;
}

XMPPUser* JBPresence::getRemoteUser(const JabberID& local, const JabberID& remote,
	bool addLocal, bool* addedLocal, bool addRemote, bool* addedRemote)
{
    XMPPUserRoster* ur = getRoster(local,addLocal,addedLocal);
    if (!ur)
	return 0;
    XMPPUser* user = ur->getUser(remote,addRemote,addedRemote);
    ur->deref();
    return user;
}

void JBPresence::removeRemoteUser(const JabberID& local, const JabberID& remote)
{
    Lock lock(this);
    ObjList* obj = m_rosters.skipNull();
    XMPPUserRoster* ur = 0;
    for (; obj; obj = obj->skipNext()) {
	ur = static_cast<XMPPUserRoster*>(obj->get());
	if (local.bare() == ur->jid().bare()) {
	    if (ur->removeUser(remote))
		ur = 0;
	    break;
	}
	ur = 0;
    }
    if (ur)
	m_rosters.remove(ur,true);
}

bool JBPresence::validDomain(const String& domain)
{
    if (m_engine->getAlternateDomain() && (domain == m_engine->getAlternateDomain()))
	return true;
    String identity;
    if (!m_engine->getFullServerIdentity(identity) || identity != domain)
	return false;
    return true;
}

bool JBPresence::getStream(JBComponentStream*& stream, bool& release)
{
    release = false;
    if (stream)
	return true;
    stream = m_engine->getStream(0,!m_engine->exiting());
    if (stream) {
	release = true;
	return true;
    }
    if (!m_engine->exiting())
	DDebug(m_engine,DebugGoOn,"No stream to send data.");
    return false;
}

bool JBPresence::sendStanza(XMLElement* element, JBComponentStream* stream)
{
    if (!element)
	return true;
    bool release = false;
    if (!getStream(stream,release)) {
	delete element;
	return false;
    }
    JBComponentStream::Error res = stream->sendStanza(element);
    if (release)
	stream->deref();
    if (res == JBComponentStream::ErrorContext ||
	res == JBComponentStream::ErrorNoSocket)
	return false;
    return true;
}

bool JBPresence::sendError(XMPPError::Type type,
	const String& from, const String& to,
	XMLElement* element, JBComponentStream* stream, const String* id)
{
    XMLElement* xml = 0;
    if (!id)
	xml = createPresence(from,to,Error);
    else
	xml = XMPPUtils::createIq(XMPPUtils::IqError,from,to,*id);
    xml->addChild(element);
    xml->addChild(XMPPUtils::createError(XMPPError::TypeModify,type));
    return sendStanza(xml,stream);
}

void JBPresence::checkTimeout(u_int64_t time)
{
    lock();
    ListIterator iter(m_rosters);
    for (;;) {
	XMPPUserRoster* ur = static_cast<XMPPUserRoster*>(iter.get());
        // End of iteration?
	if (!ur)
	    break;
	// Get a reference
	RefPointer<XMPPUserRoster> sref = ur;
	if (!sref)
	    continue;
	// Check timeout
	unlock();
	if (sref->timeout(time)) {
	    lock();
	    m_rosters.remove(ur,true);
	    unlock();
	}
	lock();
    }
    unlock();
}

void JBPresence::runCheckTimeout()
{
    while (1) {
	checkTimeout(Time::msecNow());
	Thread::msleep(SLEEP_PROCESSTIMEOUT,true);
    }
}

XMLElement* JBPresence::createPresence(const char* from,
	const char* to, Presence type)
{
    XMLElement* presence = new XMLElement(XMLElement::Presence);
    presence->setAttributeValid("type",presenceText(type));
    presence->setAttribute("from",from);
    presence->setAttribute("to",to);
    return presence;
}

bool JBPresence::decodeError(const XMLElement* element,
	String& code, String& type, String& error)
{
    if (!(element && element->type() == XMLElement::Presence))
	return false;
    code = "";
    type = "";
    error = "";
    XMLElement* child = ((XMLElement*)element)->findFirstChild("error");
    if (!child)
	return true;
    child->getAttribute("code",code);
    child->getAttribute("type",type);
    child = child->findFirstChild();
    if (child)
	error = child->name();
    return true;
}

bool JBPresence::checkDestination(JBEvent* event, const JabberID& jid)
{
    if (!event || validDomain(jid.domain()))
	return true;
    Debug(this,DebugNote,"Received element with invalid domain '%s'.",
	jid.domain().c_str());
    const String* id = 0;
    switch (event->type()) {
	case JBEvent::IqDiscoGet:
	case JBEvent::IqDiscoSet:
	case JBEvent::IqDiscoRes:
	    id = &(event->id());
	    break;
	default: ;
    }
    sendError(XMPPError::SNoRemote,event->to(),event->from(),
	event->releaseXML(),event->stream(),id);
    return false;
}

void JBPresence::cleanup()
{
    Lock lock(this);
    DDebug(this,DebugAll,"Cleanup.");
    ListIterator iter(m_rosters);
    GenObject* obj;
    for (; (obj = iter.get());) {
	XMPPUserRoster* ur = static_cast<XMPPUserRoster*>(obj);
	ur->cleanup();
	m_rosters.remove(ur,true);
    }
}

void JBPresence::addRoster(XMPPUserRoster* ur)
{
    Lock lock(this);
    m_rosters.append(ur);
}

void JBPresence::removeRoster(XMPPUserRoster* ur)
{
    Lock lock(this);
    m_rosters.remove(ur,false);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
