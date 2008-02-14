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
#include <yatejingle.h>

using namespace TelEngine;

TokenDict JBEvent::s_type[] = {
    {"Terminated",       Terminated},
    {"Destroy",          Destroy},
    {"Running",          Running},
    {"WriteFail",        WriteFail},
    {"Presence",         Presence},
    {"Message",          Message},
    {"Iq",               Iq},
    {"IqError",          IqError},
    {"IqResult",         IqResult},
    {"IqDiscoInfoGet",   IqDiscoInfoGet},
    {"IqDiscoInfoSet",   IqDiscoInfoSet},
    {"IqDiscoInfoRes",   IqDiscoInfoRes},
    {"IqDiscoInfoErr",   IqDiscoInfoErr},
    {"IqDiscoItemsGet",  IqDiscoItemsGet},
    {"IqDiscoItemsSet",  IqDiscoItemsSet},
    {"IqDiscoItemsRes",  IqDiscoItemsRes},
    {"IqDiscoItemsErr",  IqDiscoItemsErr},
    {"IqCommandGet",     IqCommandGet},
    {"IqCommandSet",     IqCommandSet},
    {"IqCommandRes",     IqCommandRes},
    {"IqCommandErr",     IqCommandErr},
    {"IqJingleGet",      IqJingleGet},
    {"IqJingleSet",      IqJingleSet},
    {"IqJingleRes",      IqJingleRes},
    {"IqJingleErr",      IqJingleErr},
    {"Unhandled",        Unhandled},
    {"Invalid",          Invalid},
    {0,0}
};

TokenDict XMPPUser::s_subscription[] = {
    {"none", None},
    {"to",   To},
    {"from", From},
    {"both", Both},
    {0,0},
};

TokenDict JBEngine::s_protoName[] = {
    {"component",    Component},
    {"client",       Client},
    {0,0}
};

static TokenDict s_serviceType[] = {
    {"jingle",       JBEngine::ServiceJingle},
    {"iq",           JBEngine::ServiceIq},
    {"message",      JBEngine::ServiceMessage},
    {"presence",     JBEngine::ServicePresence},
    {"command",      JBEngine::ServiceCommand},
    {"disco",        JBEngine::ServiceDisco},
    {"stream",       JBEngine::ServiceStream},
    {"write-fail",   JBEngine::ServiceWriteFail},
    {0,0}
};

static TokenDict s_threadNames[] = {
    {"Jabber stream connect", JBThread::StreamConnect},
    {"Engine receive",        JBThread::EngineReceive},
    {"Engine process",        JBThread::EngineProcess},
    {"Presence",              JBThread::Presence},
    {"Jingle",                JBThread::Jingle},
    {"Message",               JBThread::Message},
    {0,0}
};

TokenDict JIDResource::s_show[] = {
    {"away",   ShowAway},
    {"chat",   ShowChat},
    {"dnd",    ShowDND},
    {"xa",     ShowXA},
    {0,0},
};

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

// Private thread class
class JBPrivateThread : public Thread, public JBThread
{
public:
    inline JBPrivateThread(Type type, JBThreadList* list, void* client,
	int sleep, int prio)
	: Thread(lookup(type,s_threadNames),Thread::priority((Priority)prio)),
	JBThread(type,list,client,sleep)
	{}
    virtual void cancelThread(bool hard = false)
	{ Thread::cancel(hard); }
    virtual void run()
	{ JBThread::runClient(); }
};

/**
 * JBThread
 */
// Constructor. Append itself to the list
JBThread::JBThread(Type type, JBThreadList* list, void* client, int sleep)
    : m_type(type),
    m_owner(list),
    m_client(client),
    m_sleep(sleep)
{
    if (!m_owner)
	return;
    Lock lock(m_owner->m_mutex);
    m_owner->m_threads.append(this)->setDelete(false);
}

// Destructor. Remove itself from the list
JBThread::~JBThread()
{
    Debug("jabber",DebugAll,"'%s' private thread terminated client=(%p) [%p]",
	lookup(m_type,s_threadNames),m_client,this);
    if (!m_owner)
	return;
    Lock lock(m_owner->m_mutex);
    m_owner->m_threads.remove(this,false);
}

// Create and start a private thread
bool JBThread::start(Type type, JBThreadList* list, void* client,
	int sleep, int prio)
{
    Lock lock(list->m_mutex);
    const char* error = 0;
    bool ok = !list->m_cancelling;
    if (ok)
	ok = (new JBPrivateThread(type,list,client,sleep,prio))->startup();
    else
	error = ". Owner's threads are beeing cancelled";
    if (!ok)
	Debug("jabber",DebugNote,"'%s' private thread failed to start client=(%p)%s",
	    lookup(type,s_threadNames),client,error?error:"");
    return ok;
}

// Process the client
void JBThread::runClient()
{
    if (!m_client)
	return;
    Debug("jabber",DebugAll,"'%s' private thread is running client=(%p) [%p]",
	lookup(m_type,s_threadNames),m_client,this);
    switch (m_type) {
	case StreamConnect:
	    ((JBStream*)m_client)->connect();
	    break;
	case EngineProcess:
	    while (true)
		if (!((JBEngine*)m_client)->process(Time::msecNow()))
		    Thread::msleep(m_sleep,true);
		else
		    Thread::check(true);
	    break;
	case EngineReceive:
	    while (true)
		if (!((JBEngine*)m_client)->receive())
		    Thread::msleep(m_sleep,true);
		else
		    Thread::check(true);
	    break;
	case Presence:
	    while (true)
		if (!((JBPresence*)m_client)->process())
		    Thread::msleep(m_sleep,true);
		else
		    Thread::check(true);
	    break;
	case Jingle:
	    while (true) {
		bool ok = false;
		while (true) {
		    if (Thread::check(false))
			break;
		    JGEvent* event = ((JGEngine*)m_client)->getEvent(Time::msecNow());
		    if (!event)
			break;
		    ok = true;
		    ((JGEngine*)m_client)->processEvent(event);
		}
		if (!ok)
		    Thread::msleep(m_sleep,true);
		else
		    Thread::check(true);
	    }
	    break;
	case Message:
	    while (true) {
		JBEvent* event = ((JBMessage*)m_client)->getMessage();
		if (event) {
		    ((JBMessage*)m_client)->processMessage(event);
		    Thread::check(true);
		}
		else
		    Thread::yield(true);
	    }
	    break;
	default:
	    Debug(DebugStub,"JBThread::run() unhandled type %u",m_type);
    }
}


/**
 * JBThreadList
 */
void JBThreadList::cancelThreads(bool wait, bool hard)
{
    // Destroy private threads
    m_mutex.lock();
    for (ObjList* o = m_threads.skipNull(); o; o = o->skipNext()) {
	JBThread* p = static_cast<JBThread*>(o->get());
	p->cancelThread(hard);
    }
    m_cancelling = true;
    m_mutex.unlock();
    // Wait to terminate
    if (!hard && wait)
	while (m_threads.skipNull())
	    Thread::yield();
    m_cancelling = false;
}

// Default values
#define JB_RESTART_COUNT               2 // Stream restart counter default value
#define JB_RESTART_COUNT_MIN           1
#define JB_RESTART_COUNT_MAX          10

#define JB_RESTART_UPDATE          15000 // Stream restart counter update interval
#define JB_RESTART_UPDATE_MIN       5000
#define JB_RESTART_UPDATE_MAX     300000

// Presence values
#define JINGLE_VERSION            "1.0"  // Version capability
#define JINGLE_VOICE         "voice-v1"  // Voice capability for Google Talk

/**
 * JBEngine
 */

JBEngine::JBEngine(Protocol proto)
    : Mutex(true),
    m_protocol(proto),
    m_restartUpdateInterval(JB_RESTART_UPDATE),
    m_restartCount(JB_RESTART_COUNT),
    m_printXml(false),
    m_identity(0),
    m_componentCheckFrom(1),
    m_serverMutex(true),
    m_servicesMutex(true),
    m_initialized(false)
{
    for (int i = 0; i < ServiceCount; i++)
	 m_services[i].setDelete(false);
    debugName("jbengine");
    XDebug(this,DebugAll,"JBEngine [%p]",this);
}

JBEngine::~JBEngine()
{
    cleanup();
    cancelThreads();
    // Remove streams if alive
    if (m_streams.skipNull()) {
	Debug(this,DebugNote,"Engine destroyed while still owning streams [%p]",this);
	ListIterator iter(m_streams);
	for (GenObject* o = 0; 0 != (o = iter.get());)
	    TelEngine::destruct(static_cast<JBStream*>(o));
    }
    TelEngine::destruct((RefObject*)m_identity);
    XDebug(this,DebugAll,"~JBEngine [%p]",this);
}

// Cleanup streams. Stop all threads owned by this engine. Release memory
void JBEngine::destruct()
{
    cleanup();
    cancelThreads();
    GenObject::destruct();
}

// Initialize the engine's parameters
void JBEngine::initialize(const NamedList& params)
{
    debugLevel(params.getIntValue("debug_level",debugLevel()));

    int recv = -1, proc = -1;

    if (!m_initialized) {
	// Build engine Jabber identity and features
	if (m_protocol == Component)
	    m_identity = new JIDIdentity(JIDIdentity::Gateway,JIDIdentity::GatewayGeneric);
	else
	    m_identity = new JIDIdentity(JIDIdentity::Account,JIDIdentity::AccountRegistered);
	m_features.add(XMPPNamespace::Jingle);
	m_features.add(XMPPNamespace::JingleAudio);
	m_features.add(XMPPNamespace::Dtmf);
	m_features.add(XMPPNamespace::DiscoInfo);

	recv = params.getIntValue("private_receive_threads",1);
	for (int i = 0; i < recv; i++)
	    JBThread::start(JBThread::EngineReceive,this,this,2,Thread::Normal);
	proc = params.getIntValue("private_process_threads",1);
	for (int i = 0; i < proc; i++)
	    JBThread::start(JBThread::EngineProcess,this,this,2,Thread::Normal);
    }

    m_serverMutex.lock();
    m_server.clear();
    m_serverMutex.unlock();

    m_printXml = params.getBoolValue("printxml",false);
    // Alternate domain names
    m_alternateDomain.set(0,params.getValue("extra_domain"));
    // Stream restart update interval
    m_restartUpdateInterval =
	params.getIntValue("stream_restartupdateinterval",JB_RESTART_UPDATE);
    if (m_restartUpdateInterval < JB_RESTART_UPDATE_MIN)
	m_restartUpdateInterval = JB_RESTART_UPDATE_MIN;
    else
	if (m_restartUpdateInterval > JB_RESTART_UPDATE_MAX)
	    m_restartUpdateInterval = JB_RESTART_UPDATE_MAX;
    // Stream restart count
    m_restartCount = 
	params.getIntValue("stream_restartcount",JB_RESTART_COUNT);
    if (m_restartCount < JB_RESTART_COUNT_MIN)
	m_restartCount = JB_RESTART_COUNT_MIN;
    else
	if (m_restartCount > JB_RESTART_COUNT_MAX)
	    m_restartCount = JB_RESTART_COUNT_MAX;
    // XML parser max receive buffer
    XMLParser::s_maxDataBuffer =
	params.getIntValue("xmlparser_maxbuffer",XMLPARSER_MAXDATABUFFER);
    // Default resource
    m_defaultResource = params.getValue("default_resource","yate");
    // Check 'from' attribute for component streams
    String chk = params.getValue("component_checkfrom");
    if (chk == "none")
	m_componentCheckFrom = 0;
    else if (chk == "remote")
	m_componentCheckFrom = 2;
    else
	m_componentCheckFrom = 1;

    if (debugAt(DebugInfo)) {
	String s;
	s << " protocol=" << lookup(m_protocol,s_protoName);
	s << " default_resource=" << m_defaultResource;
	s << " component_checkfrom=" << m_componentCheckFrom;
	s << " stream_restartupdateinterval=" << (unsigned int)m_restartUpdateInterval;
	s << " stream_restartcount=" << (unsigned int)m_restartCount;
	s << " xmlparser_maxbuffer=" << (unsigned int)XMLParser::s_maxDataBuffer;
	s << " printxml=" << m_printXml;
	if (recv > -1)
	    s << " private_receive_threads=" << recv;
	if (proc > -1)
	    s << " private_process_threads=" << proc;
	Debug(this,DebugInfo,"Jabber engine initialized:%s [%p]",s.c_str(),this);
    }

    m_initialized = true;
}

// Terminate all streams
void JBEngine::cleanup()
{
    Lock lock(this);
    // Use an iterator: the stream might be destroyed when terminating
    ListIterator iter(m_streams);
    for (GenObject* o = 0; 0 != (o = iter.get());) {
	JBStream* s = static_cast<JBStream*>(o);
	s->terminate(true,0,XMPPError::Shutdown,0,true);
    }
}

// Set the default component server to use
void JBEngine::setComponentServer(const char* domain)
{
    if (m_protocol != Component)
	return;
    Lock lock(m_serverMutex);
    XMPPServerInfo* p = findServerInfo(domain,true);
    // If doesn't exists try to get the first one from the list
    if (!p) {
	ObjList* obj = m_server.skipNull();
	p = obj ? static_cast<XMPPServerInfo*>(obj->get()) : 0;
    }
    if (!p) {
	Debug(this,DebugNote,"No default component server [%p]",this);
	return;
    }
    m_componentDomain.set(0,p->name());
    m_componentAddr = p->address();
    DDebug(this,DebugAll,"Default component server set to '%s' (%s) [%p]",
	m_componentDomain.c_str(),m_componentAddr.c_str(),this);
}

// Get a stream. Create it not found and requested
// For the component protocol, the jid parameter may contain the domain to find,
//  otherwise, the default component will be used
// For the client protocol, the jid parameter must contain the full
//  user's jid (including the resource)
JBStream* JBEngine::getStream(const JabberID* jid, bool create, const char* pwd)
{
    Lock lock(this);
    if (exiting())
	return 0;
    const JabberID* remote = jid;
    // Set remote to be a valid pointer
    if (!remote)
	remote = &m_componentDomain;
    // Find the stream
    JBStream* stream = 0;
    for (ObjList* o = m_streams.skipNull(); o; o = o->skipNext()) {
	stream = static_cast<JBStream*>(o->get());
	if (stream->remote() == *remote)
	    break;
	stream = 0;
    }

    if (!stream && create) {
	XMPPServerInfo* info = findServerInfo(remote->domain(),true);
	if (!info) {
	    Debug(this,DebugNote,"No server info to create stream to '%s' [%p]",
		remote->c_str(),this);
	    return 0;
	}
	SocketAddr addr(PF_INET);
	addr.host(info->address());
	addr.port(info->port());
	stream = new JBComponentStream(this,JabberID(0,info->identity(),0),
	    *remote,info->password(),addr,
	    info->autoRestart(),m_restartCount,m_restartUpdateInterval);
	m_streams.append(stream);
    }
    return ((stream && stream->ref()) ? stream : 0);
}

// Try to get a stream if stream parameter is 0
bool JBEngine::getStream(JBStream*& stream, bool& release, const char* pwd)
{
    release = false;
    if (stream)
	return true;
    stream = getStream(0,true,pwd);
    if (stream) {
	release = true;
	return true;
    }
    return false;
}

// Keep calling receive() for each stream until no data is received or the thread is terminated
bool JBEngine::receive()
{
    bool ok = false;
    lock();
    ListIterator iter(m_streams);
    for (;;) {
	JBStream* stream = static_cast<JBStream*>(iter.get());
        // End of iteration?
	if (!stream)
	    break;
	// Get a reference
	RefPointer<JBStream> sref = stream;
	if (!sref)
	    continue;
	// Read socket
	unlock();
	if (Thread::check(false))
	    return false;
	if (sref->receive())
	    ok = true;
	lock();
    }
    unlock();
    return ok;
}

// Get events from the streams owned by this engine
bool JBEngine::process(u_int64_t time)
{
    lock();
    ListIterator iter(m_streams);
    for (;;) {
	if (Thread::check(false))
	    break;
	JBStream* stream = static_cast<JBStream*>(iter.get());
        // End of iteration?
	if (!stream)
	    break;
	// Get a reference
	RefPointer<JBStream> sref = stream;
	if (!sref)
	    continue;
	// Get event
	unlock();
	JBEvent* event = sref->getEvent(time);
	if (!event)
	    return false;

	Lock serviceLock(m_servicesMutex);
	// Send events to the registered services
	switch (event->type()) {
	    case JBEvent::Message:
		if (received(ServiceMessage,event))
		    return true;
		break;
	    case JBEvent::IqJingleGet:
	    case JBEvent::IqJingleSet:
	    case JBEvent::IqJingleRes:
	    case JBEvent::IqJingleErr:
		if (received(ServiceJingle,event))
		    return true;
		break;
	    case JBEvent::Iq:
	    case JBEvent::IqError:
	    case JBEvent::IqResult:
		if (received(ServiceIq,event))
		    return true;
		break;
	    case JBEvent::Presence:
		if (received(ServicePresence,event))
		    return true;
		break;
	    case JBEvent::IqDiscoInfoGet:
	    case JBEvent::IqDiscoInfoSet:
	    case JBEvent::IqDiscoInfoRes:
	    case JBEvent::IqDiscoInfoErr:
	    case JBEvent::IqDiscoItemsGet:
	    case JBEvent::IqDiscoItemsSet:
	    case JBEvent::IqDiscoItemsRes:
	    case JBEvent::IqDiscoItemsErr:
		if (received(ServiceDisco,event))
		    return true;
		if (processDisco(event))
		    event = 0;
		break;
	    case JBEvent::IqCommandGet:
	    case JBEvent::IqCommandSet:
	    case JBEvent::IqCommandRes:
	    case JBEvent::IqCommandErr:
		if (received(ServiceCommand,event))
		    return true;
		if (processCommand(event))
		    event = 0;
		break;
	    case JBEvent::WriteFail:
		if (received(ServiceWriteFail,event))
		    return true;
		break;
	    case JBEvent::Terminated:
	    case JBEvent::Destroy:
	    case JBEvent::Running:
		if (received(ServiceStream,event))
		    return true;
		break;
	    default: ;
	}
	serviceLock.drop();
	if (event) {
	    Debug(this,DebugAll,"Delete unhandled event (%p,%s) [%p]",
		event,event->name(),this);
	    TelEngine::destruct(event);
	}
	lock();
    }
    unlock();
    return false;
}

// Check for duplicate stream id at a remote server
bool JBEngine::checkDupId(const JBStream* stream)
{
    if (!(stream && stream->outgoing()))
	return false;
    Lock lock(this);
    for (ObjList* o = m_streams.skipNull(); o; o = o->skipNext()) {
	JBStream* s = static_cast<JBStream*>(o->get());
	if (s != stream && s->outgoing() &&
	    s->remote() == stream->remote() && s->id() == stream->id())
	    return true;
    }
    return false;
}

// Check the 'from' attribute received by a Component stream at startup
// 0: no check 1: local identity 2: remote identity
bool JBEngine::checkComponentFrom(JBComponentStream* stream, const char* from)
{
    if (!stream)
	return false;
    JabberID tmp(from);
    switch (m_componentCheckFrom) {
	case 1:
	    return stream->local() == tmp;
	case 2:
	    return stream->remote() == tmp;
	case 0:
	    return true;
    }
    return false;
}

// Asynchronously call the connect method of the given stream if the stream is idle
void JBEngine::connect(JBStream* stream)
{
    if (stream && stream->state() == JBStream::Idle)
	JBThread::start(JBThread::StreamConnect,this,stream,2,Thread::Normal);
}

// Append server info the list
void JBEngine::appendServer(XMPPServerInfo* server, bool open)
{
    if (!server)
	return;
    // Add if doesn't exists. Delete if already in the list
    XMPPServerInfo* p = findServerInfo(server->name(),true);
    if (!p) {
	m_serverMutex.lock();
	m_server.append(server);
	Debug(this,DebugAll,"Added server '%s' port=%d [%p]",
	    server->name().c_str(),server->port(),this);
	m_serverMutex.unlock();
    }
    else
	TelEngine::destruct(server);
    // Open stream
    if (open && m_protocol == Component) {
	JabberID jid(0,server->name(),0);
	JBStream* stream = getStream(&jid);
	if (stream)
	    TelEngine::destruct(stream);
    }
}

// Get the identity of the given server
bool JBEngine::getServerIdentity(String& destination, bool full,
	const char* token, bool domain)
{
    Lock lock(m_serverMutex);
    XMPPServerInfo* server = findServerInfo(token,domain);
    if (!server)
	return false;
    destination = full ? server->fullIdentity() : server->identity();
    return true;
}

// Find a server info object
XMPPServerInfo* JBEngine::findServerInfo(const char* token, bool domain)
{
    if (!token)
	token = domain ? m_componentDomain : m_componentAddr;
    if (!token)
	return 0;
    if (domain)
	for (ObjList* o = m_server.skipNull(); o; o = o->skipNext()) {
	    XMPPServerInfo* server = static_cast<XMPPServerInfo*>(o->get());
	    if (server->name() &= token)
		return server;
	}
    else
	for (ObjList* o = m_server.skipNull(); o; o = o->skipNext()) {
	    XMPPServerInfo* server = static_cast<XMPPServerInfo*>(o->get());
	    if (server->address() == token)
		return server;
	}
    return 0;
}

// Attach a service to this engine
void JBEngine::attachService(JBService* service, Service type, int prio)
{
    if (!service)
	return;
    Lock lock(m_servicesMutex);
    if (m_services[type].find(service))
	return;
    if (prio == -1)
	prio = service->priority();
    ObjList* insert = m_services[type].skipNull();
    for (; insert; insert = insert->skipNext()) {
	JBService* tmp = static_cast<JBService*>(insert->get());
	if (prio <= tmp->priority()) {
	    insert->insert(service);
	    break;
	}
    }
    if (!insert)
	m_services[type].append(service);
    Debug(this,DebugInfo,"Attached service (%p) '%s' type=%s priority=%d [%p]",
	service,service->debugName(),lookup(type,s_serviceType),prio,this);
}

// Remove a service from all event handlers of this engine.
void JBEngine::detachService(JBService* service)
{
    if (!service)
	return;
    Lock lock(m_servicesMutex);
    for (int i = 0; i < ServiceCount; i++) {
	GenObject* o = m_services[i].find(service);
	if (!o)
	    continue;
	m_services[i].remove(service,false);
	Debug(this,DebugInfo,"Removed service (%p) '%s' type=%s [%p]",
	    service,service->debugName(),lookup(i,s_serviceType),this);
    }
}

// Process disco info events
bool JBEngine::processDisco(JBEvent* event)
{
    JBStream* stream = event->stream();
    XMLElement* child = event->child();

    // Check if we sould or can respond to it
    if (!(event->type() == JBEvent::IqDiscoInfoGet && stream && child))
	return false;

    // Create response
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqResult,event->to(),event->from(),event->id());
    XMLElement* query = XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::DiscoInfo);
    m_features.addTo(query);
    if (m_identity) {
	*(String*)(m_identity) = stream->local();
	query->addChild(m_identity->toXML());
    }
    iq->addChild(query);
    stream->sendStanza(iq);
    TelEngine::destruct(event);
    return true;
}

// Process commands
bool JBEngine::processCommand(JBEvent* event)
{
    JBStream* stream = event->stream();
    if (!stream ||
	(event->type() != JBEvent::IqCommandGet && event->type() != JBEvent::IqCommandSet))
	return false;

    // Send error
    XMLElement* err = XMPPUtils::createIq(XMPPUtils::IqError,event->to(),event->from(),event->id());
    err->addChild(event->releaseXML());
    err->addChild(XMPPUtils::createError(XMPPError::TypeCancel,XMPPError::SFeatureNotImpl));
    stream->sendStanza(err);
    TelEngine::destruct(event);
    return true;
}

// Find a service to process a received event
bool JBEngine::received(Service service, JBEvent* event)
{
    if (!event)
	return false;
    for (ObjList* o = m_services[service].skipNull(); o; o = o->skipNext()) {
	JBService* service = static_cast<JBService*>(o->get());
	XDebug(this,DebugAll,"Sending event (%p,%s) to service '%s' [%p]",
	    event,event->name(),service->debugName(),this);
	if (service->received(event))
	    return true;
    }
    return false;
}


/**
 * JBService
 */
JBService::JBService(JBEngine* engine, const char* name,
	const NamedList* params, int prio)
    : Mutex(true),
    m_initialized(false),
    m_engine(engine),
    m_priority(prio)
{
    debugName(name);
    XDebug(this,DebugAll,"Jabber service created [%p]",this);
    if (params)
	initialize(*params);
}

JBService::~JBService()
{
    XDebug(this,DebugAll,"JBService::~JBService() [%p]",this);
}

// Remove from engine. Release memory
void JBService::destruct()
{
    if (m_engine)
	m_engine->detachService(this);
    Debug(this,DebugAll,"Jabber service destroyed [%p]",this);
    GenObject::destruct();
}

// Accept an event from the engine
bool JBService::accept(JBEvent* event, bool& processed, bool& insert)
{
    Debug(this,DebugStub,"JBService::accept(%p)",event);
    return false;
}

// Receive an event from engine
bool JBService::received(JBEvent* event)
{
    if (!event)
	return false;
    bool insert = false;
    bool processed = false;
    Lock lock(this);
    XDebug(this,DebugAll,"Receiving (%p,%s) [%p]",event,event->name(),this);
    if (!accept(event,processed,insert)) {
	DDebug(this,DebugAll,"Event (%p,%s) not accepted [%p]",
	    event,event->name(),this);
	return false;
    }
    if (processed) {
	DDebug(this,DebugAll,"Event (%p,%s) processed [%p]",
	    event,event->name(),this);
	return true;
    }
    Debug(this,DebugAll,"%s event (%p,%s) [%p]",
	insert?"Inserting":"Appending",event,event->name(),this);
    event->releaseStream();
    if (insert)
	m_events.insert(event);
    else
	m_events.append(event);
    return true;
}

// Get an event from queue
JBEvent* JBService::deque()
{
    Lock lock(this);
    ObjList* obj = m_events.skipNull();
    if (!obj)
	return 0;
    JBEvent* event = static_cast<JBEvent*>(obj->remove(false));
    DDebug(this,DebugAll,"Dequeued event (%p,%s) [%p]",
	event,event->name(),this);
    return event;
}


/**
 * JBEvent
 */
JBEvent::JBEvent(Type type, JBStream* stream, XMLElement* element, XMLElement* child)
    : m_type(type),
      m_stream(0),
      m_link(true),
      m_element(element),
      m_child(child)
{
    if (!init(stream,element))
	m_type = Invalid;
}

JBEvent::JBEvent(Type type, JBStream* stream, XMLElement* element, const String& senderID)
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
	TelEngine::destruct(m_stream);
    }
    if (m_element)
	delete m_element;
    XDebug(DebugAll,"JBEvent::~JBEvent [%p]",this);
}

void JBEvent::releaseStream()
{
    if (m_link && m_stream) {
	m_stream->eventTerminated(this);
	m_link = false;
    }
}

bool JBEvent::init(JBStream* stream, XMLElement* element)
{
    bool bRet = true;
    if (stream && stream->ref())
	m_stream = stream;
    else
	bRet = false;
    m_element = element;
    XDebug(DebugAll,"JBEvent::init type=%s stream=(%p) xml=(%p) [%p]",
	name(),m_stream,m_element,this);
    if (!m_element)
	return bRet;

    // Decode some data
    if (m_type == Message) {
	XMLElement* body = m_element->findFirstChild("body");
	if (body)
	    m_text = body->getText();
    }
    else
	XMPPUtils::decodeError(m_element,m_text,m_text);
    // Most elements have these parameters:
    m_stanzaType = m_element->getAttribute("type");
    m_from = m_element->getAttribute("from");
    m_to = m_element->getAttribute("to");
    m_id = m_element->getAttribute("id");
    return bRet;
}


/**
 * JBMessage
 */
// Initialize service. Create private threads
void JBMessage::initialize(const NamedList& params)
{
    debugLevel(params.getIntValue("debug_level",debugLevel()));

    if (m_initialized)
	return;
    m_initialized = true;
    m_syncProcess = params.getBoolValue("sync_process",m_syncProcess);
    if (debugAt(DebugInfo)) {
	String s;
	s << " synchronous_process=" << m_syncProcess;
	Debug(this,DebugInfo,"Jabber Message service initialized:%s [%p]",
	    s.c_str(),this);
    }
    if (!m_syncProcess) {
	int c = params.getIntValue("private_process_threads",1);
	for (int i = 0; i < c; i++)
	    JBThread::start(JBThread::Message,this,this,2,Thread::Normal);
    }
}

// Message processor
void JBMessage::processMessage(JBEvent* event)
{
    Debug(this,DebugStub,"Default message processing. Deleting (%p)",event);
    TelEngine::destruct(event);
}

// Accept an event from the engine and process it if configured to do that
bool JBMessage::accept(JBEvent* event, bool& processed, bool& insert)
{
    if (event->type() != JBEvent::Message)
	return false;
    if (m_syncProcess) {
	processed = true;
	processMessage(event);
    }
    return true;
}


/**
 * JIDResource
 */
// Set the presence for this resource
bool JIDResource::setPresence(bool value)
{
    Presence p = value ? Available : Unavailable;
    if (m_presence == p)
	return false;
    m_presence = p;
    return true;
}

// Change this resource from received element
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

// Append this resource's capabilities to a given element
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
// Add a resource to the list
bool JIDResourceList::add(const String& name)
{
    Lock lock(this);
    if (get(name))
	return false;
    m_resources.append(new JIDResource(name));
    return true;
}

// Add a resource to the list
bool JIDResourceList::add(JIDResource* resource)
{
    if (!resource)
	return false;
    Lock lock(this);
    if (get(resource->name())) {
	TelEngine::destruct(resource);
	return false;
    }
    m_resources.append(resource);
    return true;
}

// Get a resource from list
JIDResource* JIDResourceList::get(const String& name)
{
    Lock lock(this);
    ObjList* obj = m_resources.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JIDResource* res = static_cast<JIDResource*>(obj->get());
	if (res->name() == name)
	    return res;
    }
    return 0;
}

// Get the first resource with audio capabilities
JIDResource* JIDResourceList::getAudio(bool availableOnly)
{
    Lock lock(this);
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
// Constructor
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
    else {
	Debug(DebugFail,"XMPPUser. No local user for remote=%s [%p]",m_jid.c_str(),this);
	return;
    }
    m_local->addUser(this);
    DDebug(m_local->engine(),DebugInfo,"User(%s). Added remote=%s [%p]",
	m_local->jid().c_str(),m_jid.c_str(),this);
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
    if (!m_local)
	return;
    DDebug(m_local->engine(),DebugInfo, "~XMPPUser() local=%s remote=%s [%p]",
	m_local->jid().c_str(),m_jid.c_str(),this);
    // Remove all local resources: this will make us unavailable
    clearLocalRes();
    m_local->removeUser(this);
    TelEngine::destruct(m_local);
}

// Add a resource for the user
bool XMPPUser::addLocalRes(JIDResource* resource)
{
    if (!resource)
	return false;
    Lock lock(this);
    if (!m_localRes.add(resource))
	return false;
    DDebug(m_local->engine(),DebugAll,
	"User(%s). Added resource name=%s audio=%s [%p]",
	m_local->jid().c_str(),resource->name().c_str(),
	String::boolText(resource->hasCap(JIDResource::CapAudio)),this);
    resource->setPresence(true);
    if (subscribedFrom())
	sendPresence(resource,0,true);
    return true;
}

// Remove a resource of the user
void XMPPUser::removeLocalRes(JIDResource* resource)
{
    if (!(resource && m_localRes.get(resource->name())))
	return;
    Lock lock(this);
    resource->setPresence(false);
    if (subscribedFrom())
	sendPresence(resource,0);
    m_localRes.remove(resource);
    DDebug(m_local->engine(),DebugAll,"User(%s). Removed resource name=%s [%p]",
	m_local->jid().c_str(),resource->name().c_str(),this);
}

// Remove all user's resources
void XMPPUser::clearLocalRes()
{
    Lock lock(this);
    m_localRes.clear();
    if (subscribedFrom())
	sendUnavailable(0);
}

// Process an error stanza
void XMPPUser::processError(JBEvent* event)
{
    String code, type, error;
    JBPresence::decodeError(event->element(),code,type,error);
    DDebug(m_local->engine(),DebugAll,"User(%s). Received error=%s code=%s [%p]",
	m_local->jid().c_str(),error.c_str(),code.c_str(),this);
}

// Process presence probe
void XMPPUser::processProbe(JBEvent* event, const String* resName)
{
    updateTimeout(true);
    XDebug(m_local->engine(),DebugAll,"User(%s). Received probe [%p]",
	m_local->jid().c_str(),this);
    if (resName)
	notifyResource(false,*resName,event->stream(),true);
    else
	notifyResources(false,event->stream(),true);
}

// Process presence stanzas
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
		DDebug(m_local->engine(),DebugInfo,
		    "User(%s). Resource %s state=%s audio=%s [%p]",
		    m_local->jid().c_str(),res->name().c_str(),
		    res->available()?"available":"unavailable",
		    String::boolText(res->hasCap(JIDResource::CapAudio)),this);
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
	DDebug(m_local->engine(),DebugInfo,
	    "User(%s). remote=%s added resource '%s' [%p]",
	    m_local->jid().c_str(),from.bare().c_str(),res->name().c_str(),this);
    }
    // Changed: notify
    if (res->fromXML(event->element())) {
	DDebug(m_local->engine(),DebugInfo,
	    "User(%s). remote=%s resource %s state=%s audio=%s [%p]",
	    m_local->jid().c_str(),from.bare().c_str(),res->name().c_str(),
	    res->available()?"available":"unavailable",
	    String::boolText(res->hasCap(JIDResource::CapAudio)),this);
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

// Process subscribe requests
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
	    break;
	case JBPresence::Subscribed:
	    // Already subscribed to remote user: do nothing
	    if (subscribedTo())
		return;
	    updateSubscription(false,true,event->stream());
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
	    break;
	case JBPresence::Unsubscribed:
	    // If not subscribed to remote user ignore the unsubscribed confirmation
	    if (!subscribedTo())
		return;
	    updateSubscription(false,false,event->stream());
	    break;
	default:
	    return;
    }
    // Notify
    m_local->engine()->notifySubscribe(this,type);
}

// Probe a remote user
bool XMPPUser::probe(JBStream* stream, u_int64_t time)
{
    Lock lock(this);
    updateTimeout(false,time);
    XDebug(m_local->engine(),DebugAll,"User(%s). Sending probe [%p]",
	m_local->jid().c_str(),this);
    XMLElement* xml = JBPresence::createPresence(m_local->jid().bare(),m_jid.bare(),
	JBPresence::Probe);
    return m_local->engine()->sendStanza(xml,stream);
}

// Send a subscribe request
bool XMPPUser::sendSubscribe(JBPresence::Presence type, JBStream* stream)
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
    XDebug(m_local->engine(),DebugAll,"User(%s). Sending '%s' to %s [%p]",
	m_local->jid().c_str(),JBPresence::presenceText(type),
	m_jid.bare().c_str(),this);
    XMLElement* xml = JBPresence::createPresence(m_local->jid().bare(),m_jid.bare(),type);
    bool result = m_local->engine()->sendStanza(xml,stream);
    // Set subscribe data. Not for subscribe/unsubscribe
    if (from && result)
	updateSubscription(true,value,stream);
    return result;
}

// Check timeouts
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
	"User(%s). Remote %s expired. Set unavailable [%p]",
	m_local->jid().c_str(),m_jid.bare().c_str(),this);
    m_remoteRes.clear();
    m_local->engine()->notifyPresence(0,m_jid,m_local->jid(),false);
    return true;
}

// Send presence notifications for all resources
bool XMPPUser::sendPresence(JIDResource* resource, JBStream* stream,
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

// Notify the presence of a given resource
void XMPPUser::notifyResource(bool remote, const String& name,
	JBStream* stream, bool force)
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

// Notify the presence for all resources
void XMPPUser::notifyResources(bool remote, JBStream* stream, bool force)
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

// Send unavailable
bool XMPPUser::sendUnavailable(JBStream* stream)
{
    XDebug(m_local->engine(),DebugAll,"User(%s). Sending 'unavailable' to %s [%p]",
	m_local->jid().c_str(),m_jid.bare().c_str(),this);
    XMLElement* xml = JBPresence::createPresence(m_local->jid().bare(),m_jid.bare(),
	JBPresence::Unavailable);
    return m_local->engine()->sendStanza(xml,stream);
}

// Update the subscription state for a remote user
void XMPPUser::updateSubscription(bool from, bool value, JBStream* stream)
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
    DDebug(m_local->engine(),DebugInfo,"User(%s). remote=%s subscription=%s [%p]",
	m_local->jid().c_str(),m_jid.bare().c_str(),
	subscribeText(m_subscription),this);
    // Send presence if remote user is subscribed to us
    if (from && subscribedFrom()) {
	sendUnavailable(stream);
	sendPresence(0,stream,true);
    }
}

// Update some timeout values
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
    DDebug(m_engine,DebugInfo, "XMPPUserRoster. %s [%p]",m_jid.c_str(),this);
}

XMPPUserRoster::~XMPPUserRoster()
{
    m_engine->removeRoster(this);
    lock();
    m_remote.clear();
    unlock();
    DDebug(m_engine,DebugInfo, "~XMPPUserRoster. %s [%p]",m_jid.c_str(),this);
}

// Get a remote user from roster
// Add a new one if requested
XMPPUser* XMPPUserRoster::getUser(const JabberID& jid, bool add, bool* added)
{
    Lock lock(this);
    ObjList* obj = m_remote.skipNull();
    XMPPUser* u = 0;
    for (; obj; obj = obj->skipNext()) {
	u = static_cast<XMPPUser*>(obj->get());
	if (jid.bare() &= u->jid().bare())
	    break;
	u = 0;
    }
    if (!u && !add)
	return 0;
    if (!u) {
	u = new XMPPUser(this,jid.node(),jid.domain(),XMPPUser::From);
	if (added)
	    *added = true;
	Debug(m_engine,DebugAll,"User(%s) added remote=%s [%p]",
	    m_jid.c_str(),u->jid().bare().c_str(),this);
    }
    return u->ref() ? u : 0;
}

// Remove an user from roster
bool XMPPUserRoster::removeUser(const JabberID& remote)
{
    Lock lock(this);
    ObjList* obj = m_remote.skipNull();
    for (; obj; obj = obj->skipNext()) {
	XMPPUser* u = static_cast<XMPPUser*>(obj->get());
	if (remote.bare() &= u->jid().bare()) {
	    Debug(m_engine,DebugAll,"User(%s) removed remote=%s [%p]",
		m_jid.c_str(),u->jid().bare().c_str(),this);
	    m_remote.remove(u,true);
	    break;
	}
    }
    return (0 != m_remote.skipNull());
}

// Check the presence timeout for all remote users
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

// Build the service
JBPresence::JBPresence(JBEngine* engine, const NamedList* params, int prio)
    : JBService(engine,"jbpresence",params,prio),
    m_autoSubscribe((int)XMPPUser::None),
    m_delUnavailable(false),
    m_autoRoster(false),
    m_addOnSubscribe(false),
    m_addOnProbe(false),
    m_addOnPresence(false),
    m_autoProbe(true),
    m_probeInterval(1800000),
    m_expireInterval(300000)
{
}

JBPresence::~JBPresence()
{
    cancelThreads();
    Lock lock(this);
    ListIterator iter(m_rosters);
    GenObject* obj;
    for (; (obj = iter.get());) {
	XMPPUserRoster* ur = static_cast<XMPPUserRoster*>(obj);
	ur->cleanup();
	m_rosters.remove(ur,true);
    }
}

// Initialize the service
void JBPresence::initialize(const NamedList& params)
{
    debugLevel(params.getIntValue("debug_level",debugLevel()));

    m_autoSubscribe = XMPPUser::subscribeType(params.getValue("auto_subscribe"));
    m_delUnavailable = params.getBoolValue("delete_unavailable",true);
    m_autoProbe = params.getBoolValue("auto_probe",true);
    if (engine()) {
	XMPPServerInfo* info = engine()->findServerInfo(engine()->componentServer(),true);
	if (info) {
	    m_addOnSubscribe = m_addOnProbe = m_addOnPresence = info->roster();
	    // Automatically process (un)subscribe and probe requests if no roster
	    if (!info->roster()) {
		m_autoProbe = true;
		m_autoSubscribe = XMPPUser::From;
	    }
	}
    }

    m_probeInterval = 1000 * params.getIntValue("probe_interval",m_probeInterval/1000);
    m_expireInterval = 1000 * params.getIntValue("expire_interval",m_expireInterval/1000);

    m_autoRoster = m_addOnSubscribe || m_addOnProbe || m_addOnPresence;

    if (debugAt(DebugInfo)) {
	String s;
	s << " auto_subscribe=" << XMPPUser::subscribeText(m_autoSubscribe);
	s << " delete_unavailable=" << String::boolText(m_delUnavailable);
	s << " add_onsubscribe=" << String::boolText(m_addOnSubscribe);
	s << " add_onprobe=" << String::boolText(m_addOnProbe);
	s << " add_onpresence=" << String::boolText(m_addOnPresence);
	s << " auto_probe=" << String::boolText(m_autoProbe);
	s << " probe_interval=" << (unsigned int)m_probeInterval;
	s << " expire_interval=" << (unsigned int)m_expireInterval;
	Debug(this,DebugInfo,"Jabber Presence service initialized:%s [%p]",
	    s.c_str(),this);
    }

    if (!m_initialized) {
	m_initialized = true;
	int c = params.getIntValue("private_process_threads",1);
	for (int i = 0; i < c; i++)
	     JBThread::start(JBThread::Presence,this,this,2,Thread::Normal);
    }
}

// Accept an event from the engine
bool JBPresence::accept(JBEvent* event, bool& processed, bool& insert)
{
    if (!event)
	return false;
    bool disco = false;
    switch (event->type()) {
	case JBEvent::IqDiscoInfoGet:
	case JBEvent::IqDiscoInfoSet:
	case JBEvent::IqDiscoInfoRes:
	case JBEvent::IqDiscoInfoErr:
	case JBEvent::IqDiscoItemsGet:
	case JBEvent::IqDiscoItemsSet:
	case JBEvent::IqDiscoItemsRes:
	case JBEvent::IqDiscoItemsErr:
	    disco = true;
	case JBEvent::Presence:
	    insert = false;
	    break;
	default:
	    return false;
    }

    JabberID jid(event->to());
    // Check destination.
    // Don't accept disco stanzas without node (reroute them to the engine)
    // Presence stanzas might be a brodcast (no 'to' attribute)
    if (disco) {
	if (!jid.node())
	    return false;
	if (validDomain(jid.domain()))
	    return true;
    }
    else if (!event->to() || validDomain(jid.domain()))
	return true;

    Debug(this,DebugNote,"Received element with invalid domain '%s' [%p]",
	jid.domain().c_str(),this);
    // Respond only if stanza is not a response
    if (event->stanzaType() != "error" && event->stanzaType() != "result") {
	const String* id = event->id().null() ? 0 : &(event->id());
	sendError(XMPPError::SNoRemote,event->to(),event->from(),
	    event->releaseXML(),event->stream(),id);
    }
    processed = true;
    TelEngine::destruct(event);
    return true;
}

// Process received events
bool JBPresence::process()
{
    if (Thread::check(false))
	return false;
    Lock lock(this);
    JBEvent* event = deque();
    if (!event)
	return false;
    JabberID local(event->to());
    JabberID remote(event->from());
    switch (event->type()) {
	case JBEvent::IqDiscoInfoGet:
	case JBEvent::IqDiscoInfoSet:
	case JBEvent::IqDiscoInfoRes:
	case JBEvent::IqDiscoInfoErr:
	case JBEvent::IqDiscoItemsGet:
	case JBEvent::IqDiscoItemsSet:
	case JBEvent::IqDiscoItemsRes:
	case JBEvent::IqDiscoItemsErr:
	    processDisco(event,local,remote);
	    TelEngine::destruct(event);
	    return true;
	default: ;
    }
    DDebug(this,DebugAll,"Process presence: '%s' [%p]",event->stanzaType().c_str(),this);
    Presence p = presenceType(event->stanzaType());
    switch (p) {
	case JBPresence::Error:
	    processError(event,local,remote);
	    break;
	case JBPresence::Probe:
	    processProbe(event,local,remote);
	    break;
	case JBPresence::Subscribe:
	case JBPresence::Subscribed:
	case JBPresence::Unsubscribe:
	case JBPresence::Unsubscribed:
	    processSubscribe(event,p,local,remote);
	    break;
	case JBPresence::Unavailable:
	    // Check destination only if we have one. No destination: broadcast
//TODO: Fix it
#if 0
	    if (local && !checkDestination(event,local))
		break;
#endif
	    processUnavailable(event,local,remote);
	    break;
	default:
	    // Check destination only if we have one. No destination: broadcast
//TODO: Fix it
#if 0
	    if (local && !checkDestination(event,local))
		break;
	    if (!event->element())
		break;
#endif
	    // Simple presence shouldn't have a type
	    if (event->element()->getAttribute("type")) {
		Debug(this,DebugNote,
		    "Received unexpected presence type=%s from=%s to=%s [%p]",
		    event->element()->getAttribute("type"),remote.c_str(),local.c_str(),this);
		sendError(XMPPError::SFeatureNotImpl,local,remote,
		    event->releaseXML(),event->stream());
		break;
	    }
	    processPresence(event,local,remote);
    }
    TelEngine::destruct(event);
    return true;
}

// Check timeouts for all users' roster
void JBPresence::checkTimeout(u_int64_t time)
{
    lock();
    ListIterator iter(m_rosters);
    for (;;) {
	if (Thread::check(false))
	    break;
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

// Process received disco 
void JBPresence::processDisco(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,"processDisco event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),local.c_str(),remote.c_str(),this);
    if (event->type() != JBEvent::IqDiscoInfoGet || !event->stream())
	return;
    JabberID from(event->to());
    if (from.resource().null() && engine())
	from.resource(engine()->defaultResource());
    // Create response: add identity and features
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqResult,from,event->from(),event->id());
    XMLElement* query = XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::DiscoInfo);
    JIDIdentity* identity = new JIDIdentity(JIDIdentity::Client,JIDIdentity::ComponentGeneric);
    query->addChild(identity->toXML());
    identity->deref();
    JIDFeatureList fl;
    fl.add(XMPPNamespace::CapVoiceV1);
    fl.addTo(query);
    iq->addChild(query);
    sendStanza(iq,event->stream());
}

void JBPresence::processError(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,"processError event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),local.c_str(),remote.c_str(),this);
    XMPPUser* user = recvGetRemoteUser("error",local,remote);
    if (user)
	user->processError(event);
    TelEngine::destruct(user);
}

void JBPresence::processProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,"processProbe event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),local.c_str(),remote.c_str(),this);
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser("probe",local,remote,m_addOnProbe,0,
	m_addOnProbe,&newUser);
    if (!user) {
	if (m_autoProbe) {
	    XMLElement* stanza = createPresence(local.bare(),remote);
	    JIDResource* resource = new JIDResource(engine()->defaultResource(),
		JIDResource::Available,JIDResource::CapAudio);
	    resource->addTo(stanza);
	    TelEngine::destruct(resource);
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
    TelEngine::destruct(user);
}

void JBPresence::processSubscribe(JBEvent* event, Presence presence,
	const JabberID& local, const JabberID& remote)
{
    XDebug(this,DebugAll,
	"processSubscribe '%s' event=(%p,%s) local=%s remote=%s [%p]",
	presenceText(presence),event,event->name(),local.c_str(),remote.c_str(),this);
    bool addLocal = (presence == Subscribe) ? m_addOnSubscribe : false;
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser(presenceText(presence),local,remote,
	addLocal,0,addLocal,&newUser);
    if (!user) {
	if (!notifySubscribe(event,local,remote,presence) &&
	    (presence != Subscribed && presence != Unsubscribed))
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    user->processSubscribe(event,presence);
    TelEngine::destruct(user);
}

void JBPresence::processUnavailable(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,"processUnavailable event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),local.c_str(),remote.c_str(),this);
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
	    TelEngine::destruct(user);
	}
	return;
    }
    // Not broadcast: find user
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser("unavailable",local,remote,
	addLocal,0,addLocal,&newUser);
    if (!user) {
	if (!notifyPresence(event,local,remote,false))
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    if (!user->processPresence(event,false,remote))
	removeRemoteUser(local,remote);
    TelEngine::destruct(user);
}

void JBPresence::processPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    XDebug(this,DebugAll,"processPresence event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),local.c_str(),remote.c_str(),this);
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
	    TelEngine::destruct(user);
	}
	return;
    }
    // Not broadcast: find user
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser("",local,remote,
	m_addOnPresence,0,m_addOnPresence,&newUser);
    if (!user) {
	if (!notifyPresence(event,local,remote,true))
	    sendError(XMPPError::SItemNotFound,local,remote,event->releaseXML(),
		event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    user->processPresence(event,true,remote);
    TelEngine::destruct(user);
}

bool JBPresence::notifyProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote)
{
    DDebug(this,DebugStub,"notifyProbe local=%s remote=%s [%p]",
	local.c_str(),remote.c_str(),this);
    return false;
}

bool JBPresence::notifySubscribe(JBEvent* event, const JabberID& local,
	const JabberID& remote, Presence presence)
{
    DDebug(this,DebugStub,"notifySubscribe local=%s remote=%s [%p]",
	local.c_str(),remote.c_str(),this);
    return false;
}

void JBPresence::notifySubscribe(XMPPUser* user, Presence presence)
{
    DDebug(this,DebugStub,"notifySubscribe user=%p [%p]",user,this);
}

bool JBPresence::notifyPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote, bool available)
{
    DDebug(this,DebugStub,"notifyPresence local=%s remote=%s [%p]",
	local.c_str(),remote.c_str(),this);
    return false;
}

void JBPresence::notifyPresence(XMPPUser* user, JIDResource* resource)
{
    DDebug(this,DebugStub,"notifyPresence user=%p [%p]",user,this);
}

void JBPresence::notifyNewUser(XMPPUser* user)
{
    DDebug(this,DebugStub,"notifyNewUser user=%p [%p]",user,this);
}

// Get a user's roster. Add a new one if requested
XMPPUserRoster* JBPresence::getRoster(const JabberID& jid, bool add, bool* added)
{
    if (jid.node().null() || jid.domain().null())
	return 0;
    Lock lock(this);
    ObjList* obj = m_rosters.skipNull();
    for (; obj; obj = obj->skipNext()) {
	XMPPUserRoster* ur = static_cast<XMPPUserRoster*>(obj->get());
	if (jid.bare() &= ur->jid().bare())
	    return ur->ref() ? ur : 0;
    }
    if (!add)
	return 0;
    if (added)
	*added = true;
    XMPPUserRoster* ur = new XMPPUserRoster(this,jid.node(),jid.domain());
    DDebug(this,DebugAll,"Added roster for %s [%p]",jid.bare().c_str(),this);
    return ur->ref() ? ur : 0;
}

// Get a remote user's roster
XMPPUser* JBPresence::getRemoteUser(const JabberID& local, const JabberID& remote,
	bool addLocal, bool* addedLocal, bool addRemote, bool* addedRemote)
{
    DDebug(this,DebugAll,"getRemoteUser local=%s add=%s remote=%s add=%s [%p]",
	local.bare().c_str(),String::boolText(addLocal),
	remote.bare().c_str(),String::boolText(addRemote),this);
    XMPPUserRoster* ur = getRoster(local,addLocal,addedLocal);
    if (!ur)
	return 0;
    XMPPUser* user = ur->getUser(remote,addRemote,addedRemote);
    TelEngine::destruct(ur);
    return user;
}

void JBPresence::removeRemoteUser(const JabberID& local, const JabberID& remote)
{
    Lock lock(this);
    ObjList* obj = m_rosters.skipNull();
    XMPPUserRoster* ur = 0;
    for (; obj; obj = obj->skipNext()) {
	ur = static_cast<XMPPUserRoster*>(obj->get());
	if (local.bare() &= ur->jid().bare()) {
	    if (ur->removeUser(remote))
		ur = 0;
	    break;
	}
	ur = 0;
    }
    if (ur)
	m_rosters.remove(ur,true);
}

// Check if a ddestination domain is a valid one
bool JBPresence::validDomain(const String& domain)
{
    if (engine()->getAlternateDomain() &&
	(engine()->getAlternateDomain().domain() &= domain))
	return true;
    XMPPServerInfo* server = engine()->findServerInfo(engine()->componentServer(),true);
    if (!server)
	return false;
    bool ok = ((domain &= server->identity()) || (domain &= server->fullIdentity()));
    return ok;
}

// Send a stanza
bool JBPresence::sendStanza(XMLElement* element, JBStream* stream)
{
    if (!element)
	return true;
    bool release = false;
    if (!engine()->getStream(stream,release)) {
	delete element;
	return false;
    }
    JBStream::Error res = stream->sendStanza(element);
    if (release)
	TelEngine::destruct(stream);
    if (res == JBStream::ErrorContext ||
	res == JBStream::ErrorNoSocket)
	return false;
    return true;
}

// Send an error stanza
bool JBPresence::sendError(XMPPError::Type type,
	const String& from, const String& to,
	XMLElement* element, JBStream* stream, const String* id)
{
    XMLElement* xml = 0;
    XMLElement::Type t = element ? element->type() : XMLElement::Invalid;
    if (t == XMLElement::Iq)
	xml = XMPPUtils::createIq(XMPPUtils::IqError,from,to,
	    id ? id->c_str() : element->getAttribute("id"));
    else
	xml = createPresence(from,to,Error);
    xml->addChild(element);
    xml->addChild(XMPPUtils::createError(XMPPError::TypeModify,type));
    return sendStanza(xml,stream);
}

// Create a presence stanza
XMLElement* JBPresence::createPresence(const char* from,
	const char* to, Presence type)
{
    XMLElement* presence = new XMLElement(XMLElement::Presence);
    presence->setAttributeValid("type",presenceText(type));
    presence->setAttribute("from",from);
    presence->setAttribute("to",to);
    return presence;
}

// Decode a presence error stanza
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

void JBPresence::cleanup()
{
    Lock lock(this);
    DDebug(this,DebugAll,"Cleanup [%p]",this);
    ListIterator iter(m_rosters);
    GenObject* obj;
    for (; (obj = iter.get());) {
	XMPPUserRoster* ur = static_cast<XMPPUserRoster*>(obj);
	ur->cleanup();
	m_rosters.remove(ur,true);
    }
}

inline XMPPUser* JBPresence::recvGetRemoteUser(const char* type,
    const JabberID& local, const JabberID& remote,
    bool addLocal, bool* addedLocal, bool addRemote, bool* addedRemote)
{
    XMPPUser* user = getRemoteUser(local,remote,addLocal,addedLocal,addRemote,addedRemote);
    if (!user)
	Debug(this,DebugAll,
	    "No destination for received presence type=%s local=%s remote=%s [%p]",
	    type,local.c_str(),remote.c_str(),this);
    return user;
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
