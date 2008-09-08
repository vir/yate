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
    {"Terminated",              Terminated},
    {"Destroy",                 Destroy},
    {"Running",                 Running},
    {"WriteFail",               WriteFail},
    {"Presence",                Presence},
    {"Message",                 Message},
    {"Iq",                      Iq},
    {"IqError",                 IqError},
    {"IqResult",                IqResult},
    {"IqDiscoInfoGet",          IqDiscoInfoGet},
    {"IqDiscoInfoSet",          IqDiscoInfoSet},
    {"IqDiscoInfoRes",          IqDiscoInfoRes},
    {"IqDiscoInfoErr",          IqDiscoInfoErr},
    {"IqDiscoItemsGet",         IqDiscoItemsGet},
    {"IqDiscoItemsSet",         IqDiscoItemsSet},
    {"IqDiscoItemsRes",         IqDiscoItemsRes},
    {"IqDiscoItemsErr",         IqDiscoItemsErr},
    {"IqCommandGet",            IqCommandGet},
    {"IqCommandSet",            IqCommandSet},
    {"IqCommandRes",            IqCommandRes},
    {"IqCommandErr",            IqCommandErr},
    {"IqJingleGet",             IqJingleGet},
    {"IqJingleSet",             IqJingleSet},
    {"IqJingleRes",             IqJingleRes},
    {"IqJingleErr",             IqJingleErr},
    {"IqRosterSet",             IqRosterSet},
    {"IqRosterRes",             IqRosterRes},
    {"IqRosterErr",             IqRosterErr},
    {"IqClientRosterUpdate",    IqClientRosterUpdate},
    {"Unhandled",               Unhandled},
    {"Invalid",                 Invalid},
    {0,0}
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
    {"roster",       JBEngine::ServiceRoster},
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

TokenDict JBMessage::s_msg[] = {
    {"chat",      Chat},
    {"groupchat", GroupChat},
    {"headline",  HeadLine},
    {"normal",    Normal},
    {"error",     Error},
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
    : m_type(type), m_owner(list), m_client(client), m_sleep(sleep)
{
    if (!m_owner)
	return;
    Lock lock(m_owner->m_mutex);
    m_owner->m_threads.append(this)->setDelete(false);
}

// Destructor. Remove itself from the list
JBThread::~JBThread()
{
    Debug(m_owner?m_owner->owner():0,DebugAll,
	"'%s' private thread terminated client=(%p) [%p]",
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
	Debug(list?list->owner():0,DebugNote,
	    "'%s' private thread failed to start client=(%p)%s",
	    lookup(type,s_threadNames),client,error?error:"");
    return ok;
}

// Process the client
void JBThread::runClient()
{
    if (!m_client)
	return;
    Debug(m_owner?m_owner->owner():0,DebugAll,
	"'%s' private thread is running client=(%p) [%p]",
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
	Debug(owner(),DebugAll,"Cancelling '%s' private thread hard=%s",
	    lookup(p->type(),s_threadNames),String::boolText(hard));
	p->cancelThread(hard);
    }
    m_cancelling = true;
    m_mutex.unlock();
    // Wait to terminate
    if (!hard && wait) {
	DDebug(owner(),DebugAll,"Waiting for private threads to terminate");
	while (m_threads.skipNull())
	    Thread::yield();
	Debug(owner(),DebugAll,"All private threads terminated");
    }
    m_cancelling = false;
}

// Default values
#define JB_RESTART_COUNT               2 // Stream restart counter default value
#define JB_RESTART_COUNT_MIN           1
#define JB_RESTART_COUNT_MAX          10

#define JB_RESTART_UPDATE          15000 // Stream restart counter update interval
#define JB_RESTART_UPDATE_MIN       5000
#define JB_RESTART_UPDATE_MAX     300000
#define JB_SETUP_INTERVAL          60000 // Stream setup timeout
#define JB_IDLE_INTERVAL           60000 // Stream idle timeout

// Presence values
#define JINGLE_VERSION            "1.0"  // Version capability
#define JINGLE_VOICE         "voice-v1"  // Voice capability for Google Talk

/**
 * JBEngine
 */

JBEngine::JBEngine(Protocol proto)
    : Mutex(true), m_protocol(proto),
    m_restartUpdateInterval(JB_RESTART_UPDATE), m_restartCount(JB_RESTART_COUNT),
    m_streamSetupInterval(JB_SETUP_INTERVAL), m_streamIdleInterval(JB_IDLE_INTERVAL),
    m_printXml(0), m_identity(0), m_componentCheckFrom(1), m_serverMutex(true),
    m_servicesMutex(true), m_initialized(false)
{
    JBThreadList::setOwner(this);
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
    TelEngine::destruct(m_identity);
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
    int lvl = params.getIntValue("debug_level",-1);
    if (lvl != -1)
	debugLevel(lvl);

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

    String tmp = params.getValue("printxml");
    m_printXml = tmp.toBoolean() ? -1: ((tmp == "verbose") ? 1 : 0);

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

// Find a stream by its name
JBStream* JBEngine::findStream(const String& name)
{
    Lock lock(this);
    ObjList* tmp = m_streams.find(name);
    JBStream* stream = tmp ? static_cast<JBStream*>(tmp->get()) : 0;
    return stream && stream->ref() ? stream : 0;
}

// Get a stream. Create it not found and requested
// For the component protocol, the jid parameter may contain the domain to find,
//  otherwise, the default component will be used
JBStream* JBEngine::getStream(const JabberID* jid, bool create)
{
    Lock lock(this);
    if (exiting())
	return 0;

    // Client
    JBStream* stream = 0;
    if (m_protocol == Client) {
	if (!(jid && jid->bare()))
	    return 0;
	for (ObjList* o = m_streams.skipNull(); o; o = o->skipNext()) {
	    stream = static_cast<JBStream*>(o->get());
	    if (stream->local().match(*jid))
		break;
	    stream = 0;
	}
	return ((stream && stream->ref()) ? stream : 0);
    }

    const JabberID* remote = jid;
    if (!remote)
	remote = &m_componentDomain;
    for (ObjList* o = m_streams.skipNull(); o; o = o->skipNext()) {
	stream = static_cast<JBStream*>(o->get());
	if (stream->remote() == *remote)
	    break;
	stream = 0;
    }

    if (!stream && create && m_protocol != Client) {
	XMPPServerInfo* info = findServerInfo(remote->domain(),true);
	if (!info) {
	    Debug(this,DebugNote,"No server info to create stream to '%s' [%p]",
		remote->c_str(),this);
	    return 0;
	}
	stream = new JBComponentStream(this,*info,JabberID(0,info->identity(),0),*remote);
	m_streams.append(stream);
    }
    return ((stream && stream->ref()) ? stream : 0);
}

// Try to get a stream if stream parameter is 0
bool JBEngine::getStream(JBStream*& stream, bool& release)
{
    release = false;
    if (stream)
	return true;
    stream = getStream(0,true);
    if (stream) {
	release = true;
	return true;
    }
    return false;
}

// Create a new client stream if no other stream exists for the given account
JBClientStream* JBEngine::createClientStream(NamedList& params)
{
    NamedString* account = params.getParam("account");
    if (!account)
	return 0;

    // Check for existing stream
    JBStream* stream = findStream(*account);
    if (stream) {
	if (stream->type() != Client)
	    TelEngine::destruct(stream);
	return static_cast<JBClientStream*>(stream);
    }

    Lock lock(this);
    const char* domain = params.getValue("domain");
    const char* address = params.getValue("server",params.getValue("address"));
    if (!domain)
	domain = address;
    JabberID jid(params.getValue("username"),domain,params.getValue("resource"));
    // Build server info and create a new stream
    if (!address)
	address = jid.domain();
    if (!(address && jid.node() && jid.domain())) {
	Debug(this,DebugNote,"Can't create client stream: invalid jid=%s or address=%s",
	    jid.bare().c_str(),address);
	params.setParam("error","Invalid id or address");
	return 0;
    }
    int port = params.getIntValue("port",5222);
    int flags = XMPPUtils::decodeFlags(params.getValue("options"),XMPPServerInfo::s_flagName);
    XMPPServerInfo* info = new XMPPServerInfo("",address,port,
	params.getValue("password"),"","",flags);
    stream = new JBClientStream(this,*info,jid,params);
    m_streams.append(stream);
    TelEngine::destruct(info);
    return stream->ref() ? static_cast<JBClientStream*>(stream) : 0;
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
    bool gotEvent = false;
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
	if (!event) {
	    lock();
	    continue;
	}

	gotEvent = true;
	bool recv = false;
	// Send events to the registered services
	switch (event->type()) {
	    case JBEvent::Message:
		recv = received(ServiceMessage,event);
		break;
	    case JBEvent::IqJingleGet:
	    case JBEvent::IqJingleSet:
	    case JBEvent::IqJingleRes:
	    case JBEvent::IqJingleErr:
		recv = received(ServiceJingle,event);
		break;
	    case JBEvent::Iq:
	    case JBEvent::IqError:
	    case JBEvent::IqResult:
		recv = received(ServiceIq,event);
		break;
	    case JBEvent::Presence:
		recv = received(ServicePresence,event);
		break;
	    case JBEvent::IqDiscoInfoGet:
	    case JBEvent::IqDiscoInfoSet:
	    case JBEvent::IqDiscoInfoRes:
	    case JBEvent::IqDiscoInfoErr:
	    case JBEvent::IqDiscoItemsGet:
	    case JBEvent::IqDiscoItemsSet:
	    case JBEvent::IqDiscoItemsRes:
	    case JBEvent::IqDiscoItemsErr:
		recv = received(ServiceDisco,event) || processDisco(event);
		break;
	    case JBEvent::IqCommandGet:
	    case JBEvent::IqCommandSet:
	    case JBEvent::IqCommandRes:
	    case JBEvent::IqCommandErr:
		recv = received(ServiceCommand,event) || processCommand(event);
		break;
	    case JBEvent::IqRosterSet:
	    case JBEvent::IqRosterRes:
	    case JBEvent::IqRosterErr:
	    case JBEvent::IqClientRosterUpdate:
		recv = received(ServiceRoster,event);
		break;
	    case JBEvent::WriteFail:
		recv = received(ServiceWriteFail,event);
		break;
	    case JBEvent::Terminated:
	    case JBEvent::Destroy:
	    case JBEvent::Running:
		recv = received(ServiceStream,event);
		break;
	    default: ;
	}
	if (!recv && event) {
	    Debug(this,DebugAll,"Delete unhandled event (%p,%s) [%p]",
		event,event->name(),this);
	    TelEngine::destruct(event);
	}
	lock();
    }
    unlock();
    return gotEvent;
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
    XDebug(this,DebugAll,"JBEngine::connect(%p) [%p]",stream,this);
    if (stream && stream->state() == JBStream::Idle)
	JBThread::start(JBThread::StreamConnect,this,stream,2,Thread::Normal);
}

// Setup the transport layer security for a stream
bool JBEngine::encryptStream(JBStream* stream)
{
    if (!stream)
	return false;
    Debug(this,DebugStub,
	"Unable to start TLS for stream (%p) local=%s remote=%s [%p]",
	stream,stream->local().c_str(),stream->remote().c_str(),this);
    return false;
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

// Print an XML element to output
void JBEngine::printXml(const XMLElement& xml, const JBStream* stream, bool send) const
{
    if (!(m_printXml && debugAt(DebugInfo)))
	return;
    String s;
    const char* dir = send ? "sending" : "receiving";
    if (m_printXml < 0) {
	bool unclose = xml.type() == XMLElement::StreamStart ||
	    xml.type() == XMLElement::StreamEnd;
	xml.toString(s,unclose);
	Debug(this,DebugInfo,"Stream %s %s [%p]",dir,s.c_str(),stream);
    }
    else {
	XMPPUtils::print(s,(XMLElement&)xml);
	Debug(this,DebugInfo,"Stream %s [%p]%s",dir,stream,s.c_str());
    }
}

// Process disco info events
bool JBEngine::processDisco(JBEvent* event)
{
    JBStream* stream = event->stream();
    XMLElement* child = event->child();
    // Check if we should or can respond to it
    if (!(event->type() == JBEvent::IqDiscoInfoGet && stream && child))
	return false;

    // Create response
    if (m_identity)
	m_identity->setName(stream->local());
    XMLElement* iq = XMPPUtils::createDiscoInfoRes(event->to(),event->from(),event->id(),
	&m_features,m_identity);
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
    stream->sendStanza(event->createError(XMPPError::TypeCancel,XMPPError::SFeatureNotImpl));
    TelEngine::destruct(event);
    return true;
}

// Find a service to process a received event
bool JBEngine::received(Service service, JBEvent* event)
{
    if (!event)
	return false;
    Lock lock(m_servicesMutex);
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
    : Mutex(true), m_initialized(false), m_engine(engine), m_priority(prio)
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
    // Keep a reference to be able to show debug
    event->ref();
    bool ok = true;
    while (true) {
	if (!accept(event,processed,insert)) {
	    ok = false;
	    break;
	}
	if (processed) {
	    // Don't use TelEngine::destruct(): it will set the pointer to 0
	    event->deref();
	    break;
	}
	event->releaseStream();
	if (insert)
	    m_events.insert(event);
	else
	    m_events.append(event);
	break;
    }
    DDebug(this,DebugAll,"Event (%p,%s) %s [%p]",event,event->name(),
	ok?(processed?"processed":(insert?"inserted":"appended")):"not accepted",this);
    TelEngine::destruct(event);
    return ok;
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
    : m_type(type), m_stream(0), m_link(true), m_element(element), m_child(child)
{
    if (!init(stream,element))
	m_type = Invalid;
}

JBEvent::JBEvent(Type type, JBStream* stream, XMLElement* element, const String& senderID)
    : m_type(type), m_stream(0), m_link(true), m_element(element), m_child(0),
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
    releaseXML(true);
    XDebug(DebugAll,"JBEvent::~JBEvent [%p]",this);
}

void JBEvent::releaseStream()
{
    if (m_link && m_stream) {
	m_stream->eventTerminated(this);
	m_link = false;
    }
}

// Create an error response from this event if it contains a known type.
XMLElement* JBEvent::createError(XMPPError::ErrorType type, XMPPError::Type error,
	const char* text)
{
    if (!element())
	return 0;
    XMLElement* xml = 0;
    switch (m_type) {
	case Iq:
	case IqDiscoInfoGet:
	case IqDiscoInfoSet:
	case IqDiscoItemsGet:
	case IqDiscoItemsSet:
	case IqCommandGet:
	case IqCommandSet:
	case IqJingleGet:
	case IqJingleSet:
	    break;
	case Message:
	    if (JBMessage::Error == JBMessage::msgType(element()->getAttribute("type")))
		return 0;
	    break;
	case Presence:
	    if (JBPresence::Error == JBPresence::presenceType(element()->getAttribute("type")))
		return 0;
	    break;
	default:
	    return 0;
    }
    xml = XMPPUtils::createError(releaseXML(),type,error,text);
    return xml;
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

    // Most elements have these parameters:
    m_stanzaType = m_element->getAttribute("type");
    m_from.set(m_element->getAttribute("from"));
    m_to.set(m_element->getAttribute("to"));
    m_id = m_element->getAttribute("id");

    // Decode some data
    switch (m_element->type()) {
	case XMLElement::Message:
	    if (m_stanzaType != "error") {
		XMLElement* body = m_element->findFirstChild("body");
		if (body) {
		    m_text = body->getText();
		    TelEngine::destruct(body);
		}
	    }
	    else
		XMPPUtils::decodeError(m_element,m_text,m_text);
	    break;
	case XMLElement::Iq:
	case XMLElement::Presence:
	    if (m_stanzaType != "error")
		break;
	default:
	    XMPPUtils::decodeError(m_element,m_text,m_text);
    }
    return bRet;
}


/**
 * JBMessage
 */
// Initialize service. Create private threads
void JBMessage::initialize(const NamedList& params)
{
    int lvl = params.getIntValue("debug_level",-1);
    if (lvl != -1)
	debugLevel(lvl);

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
}

// Create a message element
XMLElement* JBMessage::createMessage(MsgType type, const char* from,
	const char* to, const char* id, const char* message)
{
    XMLElement* msg = new XMLElement(XMLElement::Message);
    msg->setAttributeValid("type",lookup(type,s_msg,""));
    msg->setAttribute("from",from);
    msg->setAttribute("to",to);
    if (id)
	msg->setAttributeValid("id",id);
    if (message)
	msg->addChild(new XMLElement(XMLElement::Body,0,message));
    return msg;
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

    m_info.clear();
    bool changed = setPresence(p == JBPresence::None);
    for (XMLElement* c = element->findFirstChild(); c; c = element->findNextChild(c)) {
	if (c->nameIs("show")) {
	    if (!changed && m_show != showType(c->getText()))
		changed = true;
	    m_show = showType(c->getText());
	}
	else if (c->nameIs("status")) {
	    if (!changed && m_status != c->getText())
		changed = true;
	    m_status = c->getText();
	}
	else if (c->nameIs("c")) {
	    NamedList caps("");
	    if (XMPPUtils::split(caps,c->getAttribute("ext"),' ',true)) {
		// Check audio
		bool tmp = (0 != caps.getParam(JINGLE_VOICE));
		if (tmp != hasCap(CapAudio)) {
		    changed = true;
		    if (tmp)
			m_capability |= CapAudio;
		    else
			m_capability &= ~CapAudio;
		}
	    }
	}
	else
	    m_info.append(new XMLElement(*c));
    }
    return changed;
}

// Append this resource's capabilities to a given element
void JIDResource::addTo(XMLElement* element, bool addInfo)
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
    if (addInfo)
	XMPPUtils::addChidren(element,m_info);
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
	XMPPDirVal sub, bool subTo, bool sendProbe)
    : Mutex(true), m_local(0), m_jid(node,domain), m_nextProbe(0), m_expire(0)
{
    if (local && local->ref())
	m_local = local;
    else {
	Debug(DebugFail,"XMPPUser. No local user for remote=%s [%p]",m_jid.c_str(),this);
	return;
    }
    m_local->addUser(this);
    DDebug(m_local->engine(),DebugAll,"User(%s). Added remote=%s [%p]",
	m_local->jid().c_str(),m_jid.c_str(),this);
    // Done if no engine
    if (!m_local->engine()) {
	m_subscription.set((int)sub);
	return;
    }
    // Update subscription
    switch (sub) {
	case XMPPDirVal::None:
	    break;
	case XMPPDirVal::Both:
	    updateSubscription(true,true,0);
	    updateSubscription(false,true,0);
	    break;
	case XMPPDirVal::From:
	    updateSubscription(true,true,0);
	    break;
	case XMPPDirVal::To:
	    updateSubscription(false,true,0);
	    break;
    }
    // Subscribe to remote user if not already subscribed and auto subscribe is true
    if (subTo || (!m_subscription.to() && (m_local->engine()->autoSubscribe().to())))
	sendSubscribe(JBPresence::Subscribe,0);
    // Probe remote user
    if (sendProbe)
	probe(0);
}

XMPPUser::~XMPPUser()
{
    if (!m_local)
	return;
    DDebug(m_local->engine(),DebugAll, "~XMPPUser() local=%s remote=%s [%p]",
	m_local->jid().c_str(),m_jid.c_str(),this);
    // Remove all local resources: this will make us unavailable
    clearLocalRes();
    m_local->removeUser(this);
    TelEngine::destruct(m_local);
}

// Add a resource for the user
bool XMPPUser::addLocalRes(JIDResource* resource, bool send)
{
    if (!resource)
	return false;
    Lock lock(this);
    if (!m_localRes.add(resource))
	return false;
    DDebug(m_local->engine(),DebugAll,
	"User(%s). Added local resource name=%s audio=%s avail=%s [%p]",
	m_local->jid().c_str(),resource->name().c_str(),
	String::boolText(resource->hasCap(JIDResource::CapAudio)),
	String::boolText(resource->available()),this);
    if (send && m_subscription.from())
	sendPresence(resource,0,true);
    return true;
}

// Remove a resource of the user
void XMPPUser::removeLocalRes(JIDResource* resource)
{
    if (!(resource && m_localRes.get(resource->name()))) {
	TelEngine::destruct(resource);
	return;
    }
    Lock lock(this);
    resource->setPresence(false);
    if (m_subscription.from())
	sendPresence(resource,0);
    DDebug(m_local->engine(),DebugAll,
	"User(%s). Removing local resource name=%s [%p]",
	m_local->jid().c_str(),resource->name().c_str(),this);
    m_localRes.remove(resource);
}

// Remove all user's resources
void XMPPUser::clearLocalRes()
{
    Lock lock(this);
    m_localRes.clear();
    if (m_subscription.from())
	sendUnavailable(0);
}

// Add a remote resource to the list
bool XMPPUser::addRemoteRes(JIDResource* resource)
{
    if (!resource)
	return false;
    Lock lock(this);
    if (!m_remoteRes.add(resource))
	return false;
    DDebug(m_local->engine(),DebugAll,
	"User(%s). Added remote resource name=%s audio=%s avail=%s [%p]",
	m_local->jid().c_str(),resource->name().c_str(),
	String::boolText(resource->hasCap(JIDResource::CapAudio)),
	String::boolText(resource->available()),this);
    return true;
}

// Remove a remote resource from the list
void XMPPUser::removeRemoteRes(JIDResource* resource)
{
    if (!(resource && m_remoteRes.get(resource->name()))) {
	TelEngine::destruct(resource);
	return;
    }
    Lock lock(this);
    DDebug(m_local->engine(),DebugAll,
	"User(%s). Removing remote resource name=%s [%p]",
	m_local->jid().c_str(),resource->name().c_str(),this);
    m_remoteRes.remove(resource);
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
bool XMPPUser::processPresence(JBEvent* event, bool available)
{
    updateTimeout(true);
    // No resource ?
    Lock lock(&m_remoteRes);
    if (event->from().resource().null()) {
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
		if (m_local->engine())
		    m_local->engine()->notifyPresence(this,res);
	    }
	    if (!m_local->engine() || m_local->engine()->delUnavailable())
		removeRemoteRes(res);
		
	}
	// Done if no presence service
	if (!m_local->engine())
	    return true;

	if (m_local->engine() && notify)
	    m_local->engine()->notifyPresence(event,false);
	// No more resources ? Remove user
	if (!m_remoteRes.getFirst() && m_local->engine()->delUnavailable())
	    return false;
	// Notify local resources to remote user if not already done
	if (m_subscription.from())
	    notifyResources(false,event->stream(),false);
	return true;
    }

    // 'from' has a resource: check if we already have one
    ObjList* obj = m_remoteRes.m_resources.skipNull();
    JIDResource* res = 0;
    for (; obj; obj = obj->skipNext()) {
	res = static_cast<JIDResource*>(obj->get());
	if (res->name() == event->from().resource())
	    break;
	res = 0;
    }
    // Add a new resource if we don't have one
    if (!res) {
	res = new JIDResource(event->from().resource());
	m_remoteRes.add(res);
	DDebug(m_local->engine(),DebugInfo,
	    "User(%s). remote=%s added resource '%s' [%p]",
	    m_local->jid().c_str(),event->from().bare().c_str(),res->name().c_str(),this);
    }
    // Changed: notify
    if (res->fromXML(event->element())) {
	DDebug(m_local->engine(),DebugInfo,
	    "User(%s). remote=%s resource %s state=%s audio=%s [%p]",
	    m_local->jid().c_str(),event->from().bare().c_str(),res->name().c_str(),
	    res->available()?"available":"unavailable",
	    String::boolText(res->hasCap(JIDResource::CapAudio)),this);
	if (m_local->engine())
	    m_local->engine()->notifyPresence(this,res);
    }
    if (!available && (!m_local->engine() || m_local->engine()->delUnavailable())) {
	removeRemoteRes(res);
	// No more resources ? Remove user
	if (!m_remoteRes.getFirst())
	    return false;
    }
    // Done if no presence service
    if (!m_local->engine())
	return true;
    // Notify local resources to remote user if not already done
    if (m_subscription.from())
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
	    if (m_subscription.from()) {
		sendSubscribe(JBPresence::Subscribed,event->stream());
		return;
	    }
	    // Approve if auto subscribing
	    if (m_local->engine()->autoSubscribe().from())
		sendSubscribe(JBPresence::Subscribed,event->stream());
	    break;
	case JBPresence::Subscribed:
	    // Already subscribed to remote user: do nothing
	    if (m_subscription.to())
		return;
	    updateSubscription(false,true,event->stream());
	    break;
	case JBPresence::Unsubscribe:
	    // Already unsubscribed from us: confirm it
	    if (!m_subscription.from()) {
		sendSubscribe(JBPresence::Unsubscribed,event->stream());
		return;
	    }
	    // Approve if auto subscribing
	    if (m_local->engine()->autoSubscribe().from())
		sendSubscribe(JBPresence::Unsubscribed,event->stream());
	    break;
	case JBPresence::Unsubscribed:
	    // If not subscribed to remote user ignore the unsubscribed confirmation
	    if (!m_subscription.to())
		return;
	    updateSubscription(false,false,event->stream());
	    break;
	default:
	    return;
    }
    // Notify
    if (m_local->engine())
	m_local->engine()->notifySubscribe(this,type);
}

// Probe a remote user
bool XMPPUser::probe(JBStream* stream, u_int64_t time)
{
    if (!m_local->engine())
	return false;
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
    bool result = false;
    if (m_local->engine()) {
	XMLElement* xml = JBPresence::createPresence(m_local->jid().bare(),m_jid.bare(),type);
	result = m_local->engine()->sendStanza(xml,stream);
    }
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
    // Fake an unavailable presence event
    XMLElement* xml = JBPresence::createPresence(m_jid,m_local->jid(),JBPresence::Unavailable);
    JBEvent* event = new JBEvent(JBEvent::Presence,0,xml);
    m_local->engine()->notifyPresence(event,false);
    TelEngine::destruct(event);
    return true;
}

// Send presence notifications for all resources
bool XMPPUser::sendPresence(JIDResource* resource, JBStream* stream,
	bool force)
{
    Lock lock(this);
    if (!m_local->engine())
	return false;
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
    return m_local->engine() && m_local->engine()->sendStanza(xml,stream);
}

// Update the subscription state for a remote user
void XMPPUser::updateSubscription(bool from, bool value, JBStream* stream)
{
    Lock lock(this);
    int sub = (from ? XMPPDirVal::From : XMPPDirVal::To);
    // Don't update if nothing changed
    if (value == (0 != m_subscription.flag(sub)))
	return;
    if (value)
	m_subscription.set(sub);
    else
	m_subscription.reset(sub);
    DDebug(m_local->engine(),DebugInfo,
	"User(%s). Updated subscription (%s) for remote=%s [%p]",
	m_local->jid().c_str(),XMPPDirVal::lookup((int)m_subscription),
	m_jid.bare().c_str(),this);
    // Send presence if remote user is subscribed to us
    if (from && m_subscription.from()) {
	sendUnavailable(stream);
	sendPresence(0,stream,true);
    }
}

// Update some timeout values
void XMPPUser::updateTimeout(bool from, u_int64_t time)
{
    if (!m_local->engine())
	return;
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
	const char* domain, JBEngine::Protocol proto)
    : Mutex(true),
    m_jid(node,domain),
    m_identity(0),
    m_engine(engine)
{
    if (m_engine)
	m_engine->addRoster(this);

    if (proto == JBEngine::Component)
	m_identity = new JIDIdentity(JIDIdentity::Client,JIDIdentity::ComponentGeneric);
    else if (proto == JBEngine::Client)
	m_identity = new JIDIdentity(JIDIdentity::Client,JIDIdentity::AccountRegistered);
    else
	m_identity = new JIDIdentity(JIDIdentity::CategoryUnknown,JIDIdentity::TypeUnknown);
    m_features.add(XMPPNamespace::CapVoiceV1);

    Debug(m_engine,DebugAll, "XMPPUserRoster %s [%p]",m_jid.c_str(),this);
}

XMPPUserRoster::~XMPPUserRoster()
{
    if (m_engine)
	m_engine->removeRoster(this);
    TelEngine::destruct(m_identity);
    cleanup();
    Debug(m_engine,DebugAll, "~XMPPUserRoster %s [%p]",m_jid.c_str(),this);
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
	u = new XMPPUser(this,jid.node(),jid.domain(),XMPPDirVal::From);
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
    m_delUnavailable(false), m_autoRoster(false), m_ignoreNonRoster(false),
    m_autoProbe(true), m_probeInterval(1800000), m_expireInterval(300000),
    m_defIdentity(0)
{
    JBThreadList::setOwner(this);
    m_defIdentity = new JIDIdentity(JIDIdentity::Client,JIDIdentity::ComponentGeneric);
    m_defFeatures.add(XMPPNamespace::CapVoiceV1);
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
    TelEngine::destruct(m_defIdentity);
}

// Initialize the service
void JBPresence::initialize(const NamedList& params)
{
    int lvl = params.getIntValue("debug_level",-1);
    if (lvl != -1)
	debugLevel(lvl);

    m_autoSubscribe.replace(params.getValue("auto_subscribe"));
    m_delUnavailable = params.getBoolValue("delete_unavailable",true);
    m_ignoreNonRoster = params.getBoolValue("ignorenonroster",false);
    m_autoProbe = params.getBoolValue("auto_probe",true);
    NamedString* addSubParam = params.getParam("add_onsubscribe");
    if (addSubParam)
	m_addOnSubscribe.replace(addSubParam->c_str());
    NamedString* addPresParam = params.getParam("add_onpresence");
    if (addPresParam)
	m_addOnPresence.replace(addPresParam->c_str());
    NamedString* addProbeParam = params.getParam("add_onprobe");
    if (addProbeParam)
	m_addOnProbe.replace(addProbeParam->c_str());

    // Override missing add_ params if should keep the roster
    // Automatically process (un)subscribe and probe requests if no roster
    if (engine()) {
	XMPPServerInfo* info = engine()->findServerInfo(engine()->componentServer(),true);
	if (info)
	    if (info->flag(XMPPServerInfo::KeepRoster)) {
		if (!addSubParam)
		    m_addOnSubscribe.set(XMPPDirVal::Both);
		if (!addPresParam)
		    m_addOnPresence.set(XMPPDirVal::Both);
		if (!addProbeParam)
		    m_addOnProbe.set(XMPPDirVal::Both);
	    }
	    else {
		m_autoProbe = true;
		m_autoSubscribe.replace(XMPPDirVal::From);
	    }
    }

    m_probeInterval = 1000 * params.getIntValue("probe_interval",m_probeInterval/1000);
    m_expireInterval = 1000 * params.getIntValue("expire_interval",m_expireInterval/1000);

    m_autoRoster = m_addOnSubscribe.flag(-1) || m_addOnProbe.flag(-1) ||
	m_addOnPresence.flag(-1);

    if (m_ignoreNonRoster)
	m_autoProbe = false;

    if (debugAt(DebugInfo)) {
	String s;
	s << " auto_subscribe=" << XMPPDirVal::lookup((int)m_autoSubscribe);
	s << " delete_unavailable=" << String::boolText(m_delUnavailable);
	s << " ignorenonroster=" << String::boolText(m_ignoreNonRoster);
	s << " add_onsubscribe=" << XMPPDirVal::lookup((int)m_addOnSubscribe);
	s << " add_onprobe=" << XMPPDirVal::lookup((int)m_addOnProbe);
	s << " add_onpresence=" << XMPPDirVal::lookup((int)m_addOnPresence);
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
	case JBEvent::IqRosterRes:
	case JBEvent::IqRosterErr:
	    insert = true;
	    return true;
	default:
	    return false;
    }

    JabberID jid(event->to());
    // Check destination. Don't do that for client streams: already done
    // Don't accept disco stanzas without node (reroute them to the engine)
    // Presence stanzas might be a brodcast (no 'to' attribute)
    if (disco) {
	if (!jid.node())
	    return false;
	if (validDomain(jid.domain()))
	    return true;
    }
    else if (event->stream() && event->stream()->type() == JBEngine::Client)
	return true;
    else if (!event->to() || validDomain(jid.domain()))
	return true;

    if (m_ignoreNonRoster)
	DDebug(this,DebugNote,"Received element with invalid domain '%s' [%p]",
	     jid.domain().c_str(),this);
    else {
	Debug(this,DebugNote,"Received element with invalid domain '%s' [%p]",
	     jid.domain().c_str(),this);
	// Respond only if stanza is not a response
	if (event->stanzaType() != "error" && event->stanzaType() != "result")
	    sendStanza(event->createError(XMPPError::TypeModify,XMPPError::SNoRemote),event->stream());
    }
    processed = true;
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
    switch (event->type()) {
	case JBEvent::IqDiscoInfoGet:
	case JBEvent::IqDiscoInfoSet:
	case JBEvent::IqDiscoInfoRes:
	case JBEvent::IqDiscoInfoErr:
	case JBEvent::IqDiscoItemsGet:
	case JBEvent::IqDiscoItemsSet:
	case JBEvent::IqDiscoItemsRes:
	case JBEvent::IqDiscoItemsErr:
	    processDisco(event);
	    TelEngine::destruct(event);
	    return true;
	default: ;
    }
    DDebug(this,DebugAll,"Process presence: '%s' [%p]",event->stanzaType().c_str(),this);
    Presence p = presenceType(event->stanzaType());
    switch (p) {
	case JBPresence::Error:
	    processError(event);
	    break;
	case JBPresence::Probe:
	    processProbe(event);
	    break;
	case JBPresence::Subscribe:
	case JBPresence::Subscribed:
	case JBPresence::Unsubscribe:
	case JBPresence::Unsubscribed:
	    processSubscribe(event,p);
	    break;
	case JBPresence::Unavailable:
	    // Check destination only if we have one. No destination: broadcast
	    processUnavailable(event);
	    break;
	default:
	    // Simple presence shouldn't have a type
	    if (event->element()->getAttribute("type")) {
		if (m_ignoreNonRoster)
		    break;
		Debug(this,DebugNote,
		    "Received unexpected presence type=%s from=%s to=%s [%p]",
		    event->element()->getAttribute("type"),event->from().c_str(),
		    event->to().c_str(),this);
		sendStanza(event->createError(XMPPError::TypeModify,XMPPError::SFeatureNotImpl),
		    event->stream());
		break;
	    }
	    processPresence(event);
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
void JBPresence::processDisco(JBEvent* event)
{
    XDebug(this,DebugAll,"processDisco event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),event->to().c_str(),event->from().c_str(),this);
    if (event->type() != JBEvent::IqDiscoInfoGet || !event->stream())
	return;

    XMLElement* rsp = 0;
    JabberID from(event->to());
    XMPPUserRoster* roster = getRoster(event->to(),false,0);
    if (roster) {
	XMPPUser* user = roster->getUser(event->from());
	bool ok = false;
	if (user) {
	    Lock lock(user);
	    if (from.resource())
		ok = (0 != user->m_localRes.get(from.resource()));
	    else {
		JIDResource* res = user->m_localRes.getFirst();
		if (res) {
		    ok = true;
		    from.resource(res->name());
		}
	    }
	}
	if (ok)
	    rsp = roster->createDiscoInfoResult(from,event->from(),event->id());
	TelEngine::destruct(user);
	TelEngine::destruct(roster);
    }

    if (!rsp && !m_ignoreNonRoster) {
	if (from.resource().null() && engine())
	    from.resource(engine()->defaultResource());
	rsp = XMPPUtils::createDiscoInfoRes(from,event->from(),event->id(),
	    &m_defFeatures,m_defIdentity);
    }

    if (rsp)
	sendStanza(rsp,event->stream());
}

void JBPresence::processError(JBEvent* event)
{
    XDebug(this,DebugAll,"processError event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),event->to().c_str(),event->from().c_str(),this);
    XMPPUser* user = recvGetRemoteUser("error",event->to(),event->from());
    if (user)
	user->processError(event);
    TelEngine::destruct(user);
}

void JBPresence::processProbe(JBEvent* event)
{
    XDebug(this,DebugAll,"processProbe event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),event->to().c_str(),event->from().c_str(),this);
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser("probe",event->to(),event->from(),
	m_addOnProbe.from(),0,m_addOnProbe.from(),&newUser);
    if (!user) {
	if (m_autoProbe) {
	    XMLElement* stanza = createPresence(event->to().bare(),event->from());
	    JIDResource* resource = new JIDResource(engine()->defaultResource(),
		JIDResource::Available,JIDResource::CapAudio);
	    resource->addTo(stanza);
	    TelEngine::destruct(resource);
	    if (event->stream())
		event->stream()->sendStanza(stanza);
	    else
		TelEngine::destruct(stanza);
	}
	else if (!notifyProbe(event) && !m_ignoreNonRoster)
	    sendStanza(event->createError(XMPPError::TypeModify,XMPPError::SItemNotFound),
		    event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    String resName = event->to().resource();
    if (resName.null())
	user->processProbe(event);
    else
	user->processProbe(event,&resName);
    TelEngine::destruct(user);
}

void JBPresence::processSubscribe(JBEvent* event, Presence presence)
{
    XDebug(this,DebugAll,
	"processSubscribe '%s' event=(%p,%s) local=%s remote=%s [%p]",
	presenceText(presence),event,event->name(),
	event->to().c_str(),event->from().c_str(),this);
    bool addLocal = (presence == Subscribe) ? m_addOnSubscribe.from() : false;
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser(presenceText(presence),event->to(),event->from(),
	addLocal,0,addLocal,&newUser);
    if (!user) {
	if (!notifySubscribe(event,presence) &&
	    (presence != Subscribed && presence != Unsubscribed) &&
	    !m_ignoreNonRoster)
	    sendStanza(event->createError(XMPPError::TypeModify,XMPPError::SItemNotFound),
		    event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    user->processSubscribe(event,presence);
    TelEngine::destruct(user);
}

void JBPresence::processUnavailable(JBEvent* event)
{
    XDebug(this,DebugAll,"processUnavailable event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),event->to().c_str(),event->from().c_str(),this);
    // Don't add if delUnavailable is true
    bool addLocal = m_addOnPresence.from() && !m_delUnavailable;
    // Check if broadcast
    if (event->to().null()) {
	Lock lock(this);
	ObjList* obj = m_rosters.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    XMPPUserRoster* roster = static_cast<XMPPUserRoster*>(obj->get());
	    bool newUser = false;
	    XMPPUser* user = getRemoteUser(roster->jid(),event->from(),addLocal,0,
		addLocal,&newUser);
	    if (!user)
		continue;
	    if (newUser)
		notifyNewUser(user);
	    if (!user->processPresence(event,false))
		removeRemoteUser(event->to(),event->from());
	    TelEngine::destruct(user);
	}
	return;
    }
    // Not broadcast: find user
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser("unavailable",event->to(),event->from(),
	addLocal,0,addLocal,&newUser);
    if (!user) {
	if (!notifyPresence(event,false) && !m_ignoreNonRoster)
	    sendStanza(event->createError(XMPPError::TypeModify,XMPPError::SItemNotFound),
		    event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    if (!user->processPresence(event,false))
	removeRemoteUser(event->to(),event->from());
    TelEngine::destruct(user);
}

void JBPresence::processPresence(JBEvent* event)
{
    XDebug(this,DebugAll,"processPresence event=(%p,%s) local=%s remote=%s [%p]",
	event,event->name(),event->to().c_str(),event->from().c_str(),this);
    // Check if broadcast
    if (event->to().null()) {
	Lock lock(this);
	ObjList* obj = m_rosters.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    XMPPUserRoster* roster = static_cast<XMPPUserRoster*>(obj->get());
	    bool newUser = false;
	    XMPPUser* user = getRemoteUser(roster->jid(),event->from(),
		m_addOnPresence.from(),0,m_addOnPresence.from(),&newUser);
	    if (!user)
		continue;
	    if (newUser)
		notifyNewUser(user);
	    user->processPresence(event,true);
	    TelEngine::destruct(user);
	}
	return;
    }
    // Not broadcast: find user
    bool newUser = false;
    XMPPUser* user = recvGetRemoteUser("",event->to(),event->from(),
	m_addOnPresence.from(),0,m_addOnPresence.from(),&newUser);
    if (!user) {
	if (!notifyPresence(event,true) && !m_ignoreNonRoster)
	    sendStanza(event->createError(XMPPError::TypeModify,XMPPError::SItemNotFound),
		    event->stream());
	return;
    }
    if (newUser)
	notifyNewUser(user);
    user->processPresence(event,true);
    TelEngine::destruct(user);
}

bool JBPresence::notifyProbe(JBEvent* event)
{
    DDebug(this,DebugStub,"notifyProbe local=%s remote=%s [%p]",
	event->to().c_str(),event->from().c_str(),this);
    return false;
}

bool JBPresence::notifySubscribe(JBEvent* event, Presence presence)
{
    DDebug(this,DebugStub,"notifySubscribe local=%s remote=%s [%p]",
	event->to().c_str(),event->from().c_str(),this);
    return false;
}

void JBPresence::notifySubscribe(XMPPUser* user, Presence presence)
{
    DDebug(this,DebugStub,"notifySubscribe user=%p [%p]",user,this);
}

bool JBPresence::notifyPresence(JBEvent* event, bool available)
{
    DDebug(this,DebugStub,"notifyPresence local=%s remote=%s [%p]",
	event->to().c_str(),event->from().c_str(),this);
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
	TelEngine::destruct(element);
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
    XMLElement* tmp = child->findFirstChild();
    TelEngine::destruct(child);
    if (tmp) {
	error = tmp->name();
	TelEngine::destruct(tmp);
    }
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
