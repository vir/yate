/**
 * park.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Call parking module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

// Parking endpoint
class ParkEndpoint : public CallEndpoint
{
public:
    ParkEndpoint(const String &name);
    ~ParkEndpoint();
    // Call parent's method. Dispatch a chan.hangup message
    virtual void disconnected(bool final, const char *reason);
    // Connect to peer. Create data source. Notify state changes
    bool callExecute(Message& msg);
    // Complete a message with common notification parameters
    Message* complete(const char* message, const char* status = 0);
private:
    bool m_hungup;                       // Allready hungup flag (used to avoid multiple notifications)
    String m_peerId;                     // Peer's channel id
};

// Parking module
class ParkModule : public Module
{
    friend class ParkEndpoint;
public:
    ParkModule();
    virtual ~ParkModule();
    virtual void initialize();
    // Find a parking by id
    ParkEndpoint* findParking(const String& id);
private:
    bool m_initialized;                  // True if already initialized
};

static ParkModule s_module;
static const char* s_prefix = "park/";
static unsigned int s_id = 1;            // Channel id to use
static ObjList s_chans;                  // Channel list
static Mutex s_mutex(true,"Park");       // Global mutex


// call.execute handler. Park a call
class ParkHandler : public MessageHandler
{
public:
    inline ParkHandler(int prio = 100)
	: MessageHandler("call.execute",prio,s_module.name())
	{ }
    virtual bool received(Message& msg);
};

// engine.halt handler. Disconnect all parked calls
class HaltHandler : public MessageHandler
{
public:
    inline HaltHandler(int prio = 100)
	: MessageHandler("engine.halt",prio,s_module.name())
	{ }
    virtual bool received(Message& msg);
};

// chan.locate handler
class LocateHandler : public MessageHandler
{
public:
    inline LocateHandler(int prio = 100)
	: MessageHandler("chan.locate",prio,s_module.name())
	{ }
    virtual bool received(Message& msg);
};


// Find a parking by id
ParkEndpoint* findParking(const String& id)
{
    Lock lock(s_mutex);
    ObjList* obj = s_chans.find(id);
    if (obj)
	return static_cast<ParkEndpoint*>(obj->get());
    return 0;
}


/**
 * ParkEndpoint
 */
ParkEndpoint::ParkEndpoint(const String& id)
    : CallEndpoint(id),
    m_hungup(false)
{
    Lock lock(s_mutex);
    s_chans.append(this);
}

ParkEndpoint::~ParkEndpoint()
{
    disconnected(true,0);
    Lock lock(s_mutex);
    s_chans.remove(this,false);
}

// Call parent's method. Dispatch a chan.hangup message
void ParkEndpoint::disconnected(bool final, const char *reason)
{
    CallEndpoint::disconnected(final,reason);
    if (m_hungup)
	return;
    m_hungup = true;
    setSource();
    Message* m = complete("chan.hangup");
    m->addParam("targetid",m_peerId);
    if (reason && *reason)
	m->addParam("reason",reason);
    Engine::enqueue(m);
}

// Connect to peer. Create data source. Notify state changes
bool ParkEndpoint::callExecute(Message& msg)
{
    CallEndpoint* peer = YOBJECT(CallEndpoint,msg.userData());
    if (!peer) {
	Debug(&s_module,DebugNote,"No channel to park on '%s'",id().c_str());
	msg.setParam("error","failure");
	return false;
    }
    m_peerId = peer->id();
    if (!peer->connect(this,msg.getValue("reason"))) {
	Debug(&s_module,DebugNote,"Failed to park '%s' on '%s'",m_peerId.c_str(),id().c_str());
	return false;
    }
    msg.setParam("peerid",id());
    msg.setParam("targetid",id());

    Message* m = complete("chan.startup","outgoing");
    m->addParam("cdrwrite","false");
    m->addParam("caller",msg.getValue("caller"));
    m->addParam("called",msg.getValue("called"));
    Engine::enqueue(m);

    // Set source
    String source = msg.getValue("source");
    if (source) {
	Message src("chan.attach");
	src.userData(this);
	src.addParam("source",source);
	Engine::dispatch(src);
    }

    m = complete("call.ringing","ringing");
    m->addParam("targetid",m_peerId);
    m->addParam("peerid",m_peerId);
    Engine::enqueue(m);

    DDebug(&s_module,DebugInfo,
	"'%s' parked on '%s' (%ssource: '%s') [%p]",m_peerId.c_str(),
	id().c_str(),getSource()?"":"Failed to set ",source.c_str(),this);
    return true;
}

// Complete a message with common notification parameters
Message* ParkEndpoint::complete(const char* message, const char* status)
{
    Message* m = new Message(message);
    m->addParam("driver",s_module.name());
    m->addParam("id",id());
    if (status)
	m->addParam("status",status);
    return m;
}


/**
 * ParkModule
 */
ParkModule::ParkModule()
    : Module("park","misc"),
    m_initialized(false)
{
    Output("Loaded module Call Parking");
}

ParkModule::~ParkModule()
{
    Output("Unloading module Call Parking");
}

void ParkModule::initialize()
{
    Output("Initializing module Call Parking");
    if (m_initialized)
	return;
    setup();
    m_initialized = true;
    Engine::install(new ParkHandler);
    Engine::install(new HaltHandler);
    Engine::install(new LocateHandler);
}


/**
 * Message handlers
 */

// call.execute. Park a call
bool ParkHandler::received(Message& msg)
{
    if (Engine::exiting())
	return false;

    String callto = msg.getValue("callto");
    if (!(callto.startSkip(s_prefix,false) && callto))
	return false;

    String id;
    if (callto == "any") {
	s_mutex.lock();
	// Try to re-use an old parking
	for (unsigned int i = 1; i < s_id; i++) {
	    id << s_prefix << i;
	    if (!findParking(id))
		break;
	    id = "";
	}
	if (!id)
	    id << s_prefix << s_id++;
	s_mutex.unlock();
    }
    else {
	id << s_prefix << callto;
	if (findParking(id)) {
	    Debug(&s_module,DebugNote,"Park '%s' already taken",id.c_str());
	    msg.setParam("error","failure");
	    return false;
	}
    }

    ParkEndpoint* park = new ParkEndpoint(id);
    bool ok = park->callExecute(msg);
    park->deref();
    return ok;
}

// engine.halt. Disconnect all parked calls
bool HaltHandler::received(Message& msg)
{
    Lock lock (s_mutex);
    ListIterator iter(s_chans);
    for (GenObject* o = 0; (o = iter.get());)
	(static_cast<ParkEndpoint*>(o))->disconnect("shutdown");
    return false;
}

// chan.locate
bool LocateHandler::received(Message& msg)
{
    String chan = msg.getValue("id");
    if (!chan.startsWith(s_prefix))
	return false;
    Lock lock(s_mutex);
    ParkEndpoint* park = findParking(chan);
    if (park)
	msg.userData(park);
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */


