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

// Default values
#define JB_STREAM_PARTIALRESTART       2
#define JB_STREAM_TOTALRESTART        -1
#define JB_STREAM_WAITRESTART       5000

// Sleep time for threads
#define SLEEP_READSOCKET               2 // Read socket
#define SLEEP_PROCESSPRESENCE          2 // Process presence events

JBEngine::JBEngine()
    : Mutex(true),
      m_clientsMutex(true),
      m_presence(0),
      m_featuresMutex(true),
      m_partialStreamRestart(JB_STREAM_PARTIALRESTART),
      m_totalStreamRestart(JB_STREAM_TOTALRESTART),
      m_streamID(0),
      m_serverMutex(true)
{
    debugName("jbengine");
    XDebug(this,DebugAll,"JBEngine. [%p]",this);
}

JBEngine::~JBEngine()
{
    cleanup();
    XDebug(this,DebugAll,"~JBEngine. [%p]",this);
}

void JBEngine::initialize(const NamedList& params)
{
    clearServerList();
    // Stream restart attempts
    m_partialStreamRestart =
	params.getIntValue("stream_partialrestart",JB_STREAM_PARTIALRESTART);
    // Sanity check to avoid perpetual retries
    if (m_partialStreamRestart < 1)
	m_partialStreamRestart = 1;
    m_totalStreamRestart =
	params.getIntValue("stream_totalrestart",JB_STREAM_TOTALRESTART);
    m_waitStreamRestart =
	params.getIntValue("stream_waitrestart",JB_STREAM_WAITRESTART);
    // XML parser max receive buffer
    XMLParser::s_maxDataBuffer =
	params.getIntValue("xmlparser_maxbuffer",XMLPARSER_MAXDATABUFFER);
    if (debugAt(DebugAll)) {
	String s;
	s << "\r\nstream_partialrestart=" << m_partialStreamRestart;
	s << "\r\nstream_totalrestart=" << m_totalStreamRestart;
	s << "\r\nstream_waitrestart=" << m_waitStreamRestart;
	s << "\r\nxmlparser_maxbuffer=" << XMLParser::s_maxDataBuffer;
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
	Debug(this,DebugNote,"No default component server is set.");
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
		case JBEvent::Invalid:
		    event->deref();
		    break;
		case JBEvent::Terminated:
		    // Restart stream
		    connect(event->stream());
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
    // Component server
    destination = "";
    destination << server->identity() << "." << server->name();
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
	    // Set the identity
	    query->addChild(XMPPUtils::createIdentity("gateway","Talk",
		    stream->localName()));
	    // Set features
	    XMPPNamespace::Type ns[2] = {XMPPNamespace::Jingle,
					 XMPPNamespace::JingleAudio};
	    JIDFeatures* f = new JIDFeatures();
	    f->create(ns,2);
	    f->addTo(query);
	    f->deref();
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
	    DDebug(this,DebugAll,"processDiscoInfo. From '%s' to '%s'. Unhandled.",
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
    if (debugAt(DebugInfo) && event->element()) {
	XMLElement* body = event->element()->findFirstChild(XMLElement::Body);
	const char* text = body ? body->getText() : "";
	DDebug(this,DebugInfo,
	    "processMessage. Message: '%s'. From: '%s'. To: '%s'.",
	    text,event->from().c_str(),event->to().c_str());
    }
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

JBPresence::JBPresence(JBEngine* engine)
    : JBClient(engine),
      Mutex(true)
{
    debugName("jbpresence");
    XDebug(this,DebugAll,"JBPresence. [%p]",this);
    if (m_engine)
	m_engine->setPresenceServer(this);
}

JBPresence::~JBPresence()
{
    if (m_engine)
	m_engine->unsetPresenceServer(this);
    m_events.clear();
    XDebug(this,DebugAll,"~JBPresence. [%p]",this);
}

bool JBPresence::receive(JBEvent* event)
{
// TODO:
//	Keep a list with servers to accept
//	Check destination's server name
//	Reject not owned

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
    DDebug(this,DebugAll,"Received event.");
    Lock lock(this);
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
    lock.drop();
    switch (event->type()) {
	case JBEvent::IqDiscoGet:
	case JBEvent::IqDiscoSet:
	case JBEvent::IqDiscoRes:
	    processDisco(event);
	    event->deref();
	    return true;
	default: ;
    }
    DDebug(this,DebugAll,"Process presence: '%s'.",event->stanzaType().c_str());
    Presence p = presenceType(event->stanzaType());
    switch (p) {
	case JBPresence::Error:
	    processError(event);
	    break;
	case JBPresence::Probe:
	    processProbe(event);
	    break;
	case JBPresence::Subscribe:
	    processSubscribe(event);
	    break;
	case JBPresence::Subscribed:
	    processSubscribed(event);
	    break;
	case JBPresence::Unsubscribe:
	    processUnsubscribe(event);
	    break;
	case JBPresence::Unsubscribed:
	    processUnsubscribed(event);
	    break;
	case JBPresence::Unavailable:
	    processUnavailable(event);
	    break;
	default:
	    processUnknown(event);
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

// Just print a message
#define show(method,e) \
{ \
    DDebug(this,DebugAll, \
	"JBPresence::%s. Event: (%p). From: '%s' To: '%s'.", \
	method,e,e->from().c_str(),e->to().c_str()); \
}

void JBPresence::processDisco(JBEvent* event)
{
    show("processDisco",event);
}

void JBPresence::processError(JBEvent* event)
{
    show("processError",event);
}

void JBPresence::processProbe(JBEvent* event)
{
    show("processProbe",event);
}

void JBPresence::processSubscribe(JBEvent* event)
{
    show("processSubscribe",event);
}

void JBPresence::processSubscribed(JBEvent* event)
{
    show("processSubscribed",event);
}

void JBPresence::processUnsubscribe(JBEvent* event)
{
    show("processUnsubscribe",event);
}

void JBPresence::processUnsubscribed(JBEvent* event)
{
    show("processUnsubscribed",event);
}

void JBPresence::processUnavailable(JBEvent* event)
{
    show("processUnavailable",event);
}

void JBPresence::processUnknown(JBEvent* event)
{
    show("processUnknown",event);
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

/* vi: set ts=8 sw=4 sts=4 noet: */
