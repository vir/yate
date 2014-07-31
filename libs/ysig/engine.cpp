/**
 * engine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#include "yatesig.h"
#include <yateversn.h>

#include <string.h>

// Maximum wait for a non-critical mutex acquisition
#ifndef MAX_LOCK_WAIT
#define MAX_LOCK_WAIT 10000
#endif

#define MIN_TICK_SLEEP 500
#define DEF_TICK_SLEEP 5000
#define MAX_TICK_SLEEP 50000

namespace TelEngine {

class SignallingThreadPrivate : public Thread
{
public:
    inline SignallingThreadPrivate(SignallingEngine* engine, const char* name, Priority prio)
	: Thread(name,prio), m_engine(engine)
	{ }
    virtual ~SignallingThreadPrivate();
    virtual void run();

private:
    SignallingEngine* m_engine;
};

};


using namespace TelEngine;

static ObjList s_factories;
static Mutex s_mutex(true,"SignallingFactory");

// Retrieve a value from a list
// Shift it if upper bits are set and mask is not set
// Mask it with a given mask
static inline unsigned char fixValue(const NamedList& list, const String& param,
    const TokenDict* dict, unsigned char mask, unsigned char upperMask, unsigned char shift)
{
    unsigned char val = (unsigned char)list.getIntValue(param,dict,0);
    if (0 != (val & upperMask) && 0 == (val & mask))
	val >>= shift;
    return val & mask;
}

SignallingFactory::SignallingFactory(bool fallback)
{
    s_mutex.lock();
    if (!s_factories.find(this)) {
	ObjList* l = fallback ? s_factories.append(this) : s_factories.insert(this);
	l->setDelete(false);
    }
    s_mutex.unlock();
}

SignallingFactory::~SignallingFactory()
{
    s_mutex.lock();
    s_factories.remove(this,false);
    s_mutex.unlock();
}

SignallingComponent* SignallingFactory::build(const String& type, NamedList* name)
{
    if (type.null())
	return 0;
    NamedList dummy(type);
    if (!name)
	name = &dummy;
    Lock lock(s_mutex);
    for (ObjList* l = &s_factories; l; l = l->next()) {
	SignallingFactory* f = static_cast<SignallingFactory*>(l->get());
	if (!f)
	    continue;
	DDebug(DebugAll,"Attempting to create a '%s' %s using factory %p",
	    name->c_str(),type.c_str(),f);
	SignallingComponent* obj = f->create(type,*name);
	if (obj)
	    return obj;
    }
    lock.drop();
    DDebug(DebugInfo,"Factory creating default '%s' named '%s'",type.c_str(),name->c_str());
    // now build some objects we know about
    if (type == YSTRING("SS7MTP2"))
	return new SS7MTP2(*name);
    else if (type == YSTRING("SS7M2PA"))
	return new SS7M2PA(*name);
    else if (type == YSTRING("SS7MTP3"))
	return new SS7MTP3(*name);
    else if (type == YSTRING("SS7Router"))
	return new SS7Router(*name);
    else if (type == YSTRING("SS7Management"))
	return new SS7Management(*name);
    else if (type == YSTRING("ISDNQ921"))
	return new ISDNQ921(*name,*name);
    else if (type == YSTRING("ISDNQ931"))
	return new ISDNQ931(*name,*name);
    else if (type == YSTRING("ISDNQ931Monitor"))
	return new ISDNQ931Monitor(*name,*name);
    Debug(DebugMild,"Factory could not create '%s' named '%s'",type.c_str(),name->c_str());
    return 0;
}

void* SignallingFactory::buildInternal(const String& type, NamedList* name)
{
    SignallingComponent* c = build(type,name);
    if (!c)
	return 0;
    void* raw = c->getObject(type);
    if (!raw)
	Debug(DebugFail,"Built component %p could not be casted back to type '%s'",c,type.c_str());
#ifdef DEBUG
    else
	Debug(DebugAll,"Built component %p type '%s' interface at %p",c,type.c_str(),raw);
#endif
    return raw;
}


SignallingComponent::SignallingComponent(const char* name, const NamedList* params, const char* type)
    : m_engine(0), m_compType(type)
{
    if (params) {
	name = params->getValue(YSTRING("debugname"),name);
	m_compType = params->getValue(YSTRING("type"),m_compType);
	debugLevel(params->getIntValue(YSTRING("debuglevel"),-1));
    }
    DDebug(engine(),DebugAll,"Component '%s' created [%p]",name,this);
    setName(name);
}

void SignallingComponent::setName(const char* name)
{
    debugName(0);
    m_name = name;
    debugName(m_name);
}

SignallingComponent::~SignallingComponent()
{
    DDebug(engine(),DebugAll,"Component '%s' deleted [%p]",toString().c_str(),this);
}

void SignallingComponent::destroyed()
{
    detach();
}

const String& SignallingComponent::toString() const
{
    return m_name;
}

bool SignallingComponent::initialize(const NamedList* config)
{
    return true;
}

bool SignallingComponent::resolveConfig(const String& cmpName, NamedList& params, const NamedList* config)
{
    if (!config)
	return false;
    String name = config->getValue(cmpName,params);
    if (!(name && !name.toBoolean(false)))
	return false;
    static_cast<String&>(params) = name;
    NamedString* param = config->getParam(params);
    NamedPointer* ptr = YOBJECT(NamedPointer,param);
    NamedList* ifConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
    if (ifConfig)
	params.copyParams(*ifConfig);
    else {
	if (config->hasSubParams(params + "."))
	    params.copySubParams(*config,params + ".");
	else
	    params.addParam("local-config","true");
    }
    return true;
}

bool SignallingComponent::control(NamedList& params)
{
    return false;
}

NamedList* SignallingComponent::controlCreate(const char* oper)
{
    if (m_name.null())
	return 0;
    NamedList* params = new NamedList("chan.control");
    params->addParam("component",m_name);
    if (!TelEngine::null(oper))
	params->addParam("operation",oper);
    return params;
}

bool SignallingComponent::controlExecute(NamedList* params)
{
    bool ok = false;
    if (params) {
	ok = control(*params);
	TelEngine::destruct(params);
    }
    return ok;
}


void SignallingComponent::engine(SignallingEngine* eng)
{
    if (eng == m_engine)
	return;
    if (eng)
	eng->insert(this);
    else
	detach();
}

void SignallingComponent::insert(SignallingComponent* component)
{
    if (!component)
	return;
    if (m_engine) {
	// we have an engine - force the other component in the same
	m_engine->insert(component);
	return;
    }
    if (component->engine())
	// insert ourselves in the other's engine
	component->engine()->insert(this);
}

void SignallingComponent::detach()
{
    debugChain();
    if (m_engine) {
	m_engine->remove(this);
	m_engine = 0;
    }
}

void SignallingComponent::timerTick(const Time& when)
{
    XDebug(engine(),DebugAll,"Timer ticked for component '%s' [%p]",
	toString().c_str(),this);
}

unsigned long SignallingComponent::tickSleep(unsigned long usec) const
{
    return m_engine ? m_engine->tickSleep(usec) : 0;
}

void SignallingNotifier::notify(NamedList& notifs)
{
    DDebug(DebugInfo,"SignallingNotifier::notify() [%p] stub",this);
}

void SignallingNotifier::cleanup()
{
    DDebug(DebugInfo,"SignallingNotifier::cleanup() [%p] stub",this);
}


static SignallingEngine* s_self = 0;
long SignallingEngine::s_maxLockWait = MAX_LOCK_WAIT;

SignallingEngine::SignallingEngine(const char* name)
    : Mutex(true,"SignallingEngine"),
      m_thread(0),
      m_usecSleep(DEF_TICK_SLEEP), m_tickSleep(0)
{
    debugName(name);
}

SignallingEngine::~SignallingEngine()
{
    if (m_thread) {
	Debug(this,DebugGoOn,
	    "Engine destroyed with worker thread still running [%p]",this);
	stop();
    }
    lock();
    if (s_self == this)
	s_self = 0;
    unsigned int n = m_components.count();
    if (n)
	Debug(this,DebugNote,"Cleaning up %u components [%p]",n,this);
    m_components.clear();
    unlock();
}

SignallingEngine* SignallingEngine::self(bool create)
{
    if (create && !s_self) {
	// if mutex debugging is in force don't limit the lock time
	if (Lockable::wait())
	    s_maxLockWait = -1;
	s_self = new SignallingEngine;
    }
    return s_self;
}

SignallingComponent* SignallingEngine::find(const String& name)
{
    Lock mylock(this);
    return static_cast<SignallingComponent*>(m_components[name]);
}

SignallingComponent* SignallingEngine::find(const String& name, const String& type, const SignallingComponent* start)
{
    XDebug(this,DebugAll,"Engine finding '%s' of type %s from %p [%p]",
	name.c_str(),type.c_str(),start,this);
    Lock mylock(this);
    ObjList* l = m_components.skipNull();
    if (start) {
	l = m_components.find(start);
	if (!l)
	    return 0;
	l = l->skipNext();
    }
    for (; l; l = l->skipNext()) {
	SignallingComponent* c = static_cast<SignallingComponent*>(l->get());
	if ((name.null() || (c->toString() == name)) &&
	    (type.null() || c->getObject(type)))
	    return c;
    }
    return 0;
}

bool SignallingEngine::find(const SignallingComponent* component)
{
    if (!component)
	return false;
    Lock mylock(this);
    DDebug(this,DebugAll,"Engine finding component @%p [%p]",component,this);
    return m_components.find(component) != 0;
}

SignallingComponent* SignallingEngine::build(const String& type, NamedList& params, bool init, bool ref)
{
    XDebug(this,DebugAll,"Engine building '%s' of type %s [%p]",
	params.c_str(),type.c_str(),this);
    Lock mylock(this);
    SignallingComponent* c = find(params,type);
    if (c && (ref ? c->ref() : c->alive())) {
	DDebug(this,DebugAll,"Engine returning existing component '%s' @%p (%d) [%p]",
	    c->toString().c_str(),c,c->refcount(),this);
	return c;
    }
    c = SignallingFactory::build(type,&params);
    DDebug(this,DebugAll,"Created new component '%s' @%p [%p]",
	c->toString().c_str(),c,this);
    insert(c);
    if (init && c)
	c->initialize(&params);
    return c;
}

void SignallingEngine::insert(SignallingComponent* component)
{
    if (!component)
	return;
    Lock mylock(this);
    if (component->engine() == this)
	return;
#ifdef DEBUG
    const char* dupl = m_components.find(component->toString()) ? " (duplicate)" : "";
    Debug(this,DebugAll,"Engine inserting component '%s'%s @%p [%p]",
	component->toString().c_str(),dupl,component,this);
#endif
    component->detach();
    component->m_engine = this;
    component->debugChain(this);
    m_components.append(component);
}

void SignallingEngine::remove(SignallingComponent* component)
{
    if (!component)
	return;
    Lock mylock(this);
    if (component->engine() != this)
	return;
    DDebug(this,DebugAll,"Engine removing component @%p '%s' [%p]",
	component,component->toString().c_str(),this);
    m_components.remove(component,false);
    component->m_engine = 0;
    component->detach();
}

bool SignallingEngine::remove(const String& name)
{
    if (name.null())
	return false;
    Lock mylock(this);
    SignallingComponent* component = find(name);
    if (!component)
	return false;
    DDebug(this,DebugAll,"Engine removing component '%s' @%p [%p]",
	component->toString().c_str(),component,this);
    component->m_engine = 0;
    component->detach();
    m_components.remove(component);
    return true;
}

void SignallingEngine::notify(SignallingComponent* component, NamedList notifs)
{
    if (!(m_notifier && component))
	return;
    Debug(this,DebugAll,"Engine [%p] sending notify from '%s' [%p]",this,component->toString().c_str(),component);
    m_notifier->notify(notifs);
}

bool SignallingEngine::control(NamedList& params)
{
    bool ok = false;
    Lock mylock(this);
    for (ObjList* l = m_components.skipNull(); l; l = l->skipNext())
	ok = static_cast<SignallingComponent*>(l->get())->control(params) || ok;
    // Do not add operation-status here !!
    // The handler should return false because the message wasn't processed
    // by any component
    return ok;
}

bool SignallingEngine::start(const char* name, Thread::Priority prio, unsigned long usec)
{
    Lock mylock(this);
    if (m_thread)
	return m_thread->running();
    // defaults and sanity checks
    if (usec == 0)
	usec = DEF_TICK_SLEEP;
    else if (usec < MIN_TICK_SLEEP)
	usec = MIN_TICK_SLEEP;
    else if (usec > MAX_TICK_SLEEP)
	usec = MAX_TICK_SLEEP;

    SignallingThreadPrivate* tmp = new SignallingThreadPrivate(this,name,prio);
    if (tmp->startup()) {
	m_usecSleep = usec;
	m_thread = tmp;
	DDebug(this,DebugInfo,"Engine started worker thread [%p]",this);
	return true;
    }
    delete tmp;
    Debug(this,DebugGoOn,"Engine failed to start worker thread [%p]",this);
    return false;
}

void SignallingEngine::stop()
{
    // TODO: experimental: remove commented if it's working
    if (!m_thread)
	return;
    m_thread->cancel(false);
    while (m_thread)
	Thread::yield(true);
    Debug(this,DebugAll,"Engine stopped worker thread [%p]",this);
#if 0
    lock();
    SignallingThreadPrivate* tmp = m_thread;
    m_thread = 0;
    if (tmp) {
	delete tmp;
	DDebug(this,DebugInfo,"Engine stopped worker thread [%p]",this);
    }
    unlock();
#endif
}

Thread* SignallingEngine::thread() const
{
    return m_thread;
}

unsigned long SignallingEngine::tickSleep(unsigned long usec)
{
    if (m_tickSleep > usec)
	m_tickSleep = usec;
    return m_tickSleep;
}

unsigned long SignallingEngine::timerTick(const Time& when)
{
    RefPointer<SignallingComponent> c;
    lock();
    m_tickSleep = m_usecSleep;
    ListIterator iter(m_components);
    while ((c = static_cast<SignallingComponent*>(iter.get()))) {
	unlock();
	c->timerTick(when);
	c = 0;
	lock();
    }
    unsigned long rval = m_tickSleep;
    m_tickSleep = m_usecSleep;
    unlock();
    return rval;
}

void SignallingEngine::maxLockWait(long maxWait)
{
    if (maxWait < 0)
	maxWait = -1;
    else if (maxWait < MIN_TICK_SLEEP)
	maxWait = MIN_TICK_SLEEP;
    s_maxLockWait = maxWait;
}

SignallingThreadPrivate::~SignallingThreadPrivate()
{
    if (m_engine)
	m_engine->m_thread = 0;
}

void SignallingThreadPrivate::run()
{
    for (;;) {
	if (m_engine) {
	    Time t;
	    unsigned long sleepTime = m_engine->timerTick(t);
	    if (sleepTime) {
		usleep(sleepTime,true);
		continue;
	    }
	}
	yield(true);
    }
}


/*
 * SignallingTimer
 */
// Retrieve a timer interval from a list of parameters
unsigned int SignallingTimer::getInterval(const NamedList& params, const char* param,
    unsigned int minVal, unsigned int defVal, unsigned int maxVal, bool allowDisable)
{
    unsigned int val = (unsigned int)params.getIntValue(param,defVal);
    if (!val)
        return allowDisable ? 0 : minVal;
    if (val < minVal)
	return minVal;
    if (maxVal && val > maxVal)
	return maxVal;
    return val;
}


/**
 * SignallingUtils
 */

// Coding standard as defined in Q.931/Q.850
static const TokenDict s_dict_codingStandard[] = {
	{"CCITT",            0x00},
	{"ISO/IEC",          0x01},
	{"national",         0x02},
	{"network specific", 0x03},
	{0,0}
	};

// Locations as defined in Q.850
static const TokenDict s_dict_location[] = {
	{"U",    0x00},                  // User
	{"LPN",  0x01},                  // Private network serving the local user
	{"LN",   0x02},                  // Public network serving the local user
	{"TN",   0x03},                  // Transit network
	{"RLN",  0x04},                  // Public network serving the remote user
	{"RPN",  0x05},                  // Private network serving the remote user
	{"INTL", 0x07},                  // International network
	{"BI",   0x0a},                  // Network beyond the interworking point
	{0,0}
	};

// Q.850 2.2.5. Cause class: Bits 4-6
// Q.850 Table 1. Cause value: Bits 0-6
// Defined for CCITT coding standard
static const TokenDict s_dict_causeCCITT[] = {
	// normal-event class
	{"normal-event",                   0x00},
	{"unallocated",                    0x01}, // Unallocated (unassigned) number
	{"noroute-to-network",             0x02}, // No route to specified transit network
	{"noroute",                        0x03}, // No route to destination
	{"send-info-tone",                 0x04}, // Send special information tone
	{"misdialed-trunk-prefix",         0x05}, // Misdialed trunk prefix
	{"channel-unacceptable",           0x06}, // Channel unacceptable
	{"call-delivered",                 0x07}, // Call awarded and being delivered in an established channel
	{"preemption",                     0x08}, // Preemption
	{"preemption-circuit-reserved",    0x09}, // Preemption circuit reserved for re-use
	{"ported-number",                  0x0e}, // QoR: ported number Q.850 Addendum 1 (06/2000)
	{"excess-digits",                  0x0e}, // Excess digits received, call is proceeding
	{"normal-clearing",                0x10}, // Normal Clearing
	{"busy",                           0x11}, // User busy
	{"noresponse",                     0x12}, // No user responding
	{"noanswer",                       0x13}, // No answer from user (user alerted)
	{"offline",                        0x14}, // Subscriber absent
	{"rejected",                       0x15}, // Call Rejected
	{"moved",                          0x16}, // Number changed
	{"redirection",                    0x17}, // Redirection to new destination Q.850 05/98
	{"rejected-by-feature",            0x18}, // Call rejected due to feature at the destination Q.850 Amendment 1 (07/2001)
	{"looping",                        0x19}, // Exchange routing error (hop counter) Q.850 05/98
	{"answered",                       0x1a}, // Non-selected user clearing (answered elsewhere)
	{"out-of-order",                   0x1b}, // Destination out of order
	{"invalid-number",                 0x1c}, // Invalid number format
	{"facility-rejected",              0x1d}, // Facility rejected
	{"status-enquiry-rsp",             0x1e}, // Response to STATUS ENQUIRY
	{"normal",                         0x1f}, // Normal, unspecified
	// resource-unavailable class
	{"resource-unavailable",           0x20}, // Resource unavailable
	{"congestion",                     0x22}, // No circuit/channel available
	{"channel-congestion",             0x22},
	{"net-out-of-order",               0x26}, // Network out of order
	{"frame-mode-conn-down",           0x27}, // Permanent frame mode connection out of service
	{"frame-mode-conn-up",             0x28}, // Permanent frame mode connection operational
	{"noconn",                         0x29},
	{"temporary-failure",              0x29}, // Temporary failure
	{"congestion",                     0x2a}, // Switching equipment congestion
	{"switch-congestion",              0x2a},
	{"access-info-discarded",          0x2b}, // Access information discarded
	{"channel-unavailable",            0x2c}, // Requested channel not available
	{"preemption-congestion",          0x2e}, // Precedence call blocked
	{"noresource",                     0x2f}, // Resource unavailable, unspecified
	{"service-unavailable",            0x30}, // Service or option not available
	{"qos-unavailable",                0x31}, // Quality of service unavailable
	{"facility-not-subscribed",        0x32}, // Requested facility not subscribed
	{"forbidden-out",                  0x35}, // Outgoing call barred within CUG
	{"forbidden-in",                   0x37}, // Incoming call barred within CUG
	{"bearer-cap-not-auth",            0x39}, // Bearer capability not authorized
	{"bearer-cap-not-available",       0x3a}, // Bearer capability not presently available
	{"nomedia",                        0x3a},
	{"invalid-access-info-out",        0x3e}, // Inconsistency in designated outgoing access information and subscriber class
	{"service-unavailable",            0x3f}, // Service or option not available
	// service-not-implemented class
	{"bearer-cap-not-implemented",     0x41}, // Bearer capability not implemented
	{"channel-type-not-implemented",   0x42}, // Channel type not implemented
	{"facility-not-implemented",       0x45}, // Requested facility not implemented
	{"restrict-bearer-cap-avail",      0x46}, // Only restricted digital information bearer capability is available
	{"service-not-implemented",        0x4f}, // Service or option not implemented, unspecified
	// invalid-message class
	{"invalid-callref",                0x51}, // Invalid call reference value
	{"unknown-channel",                0x52}, // Identified channel does not exist
	{"unknown-callid",                 0x53}, // A suspended call exists, but this call identity does not
	{"duplicate-callid",               0x54}, // Call identity in use
	{"no-call-suspended",              0x55}, // No call suspended
	{"suspended-call-cleared",         0x56}, // Call having the requested call identity has been cleared
	{"not-subscribed",                 0x57}, // User not member of CUG
	{"incompatible-dest",              0x58}, // Incompatible destination
	{"unknown-group",                  0x5a}, // Non-existent CUG
	{"invalid-transit-net",            0x5b}, // Invalid transit network selection
	{"invalid-message",                0x5f}, // Invalid message, unspecified
	// protocol-error class
	{"missing-mandatory-ie",           0x60}, // Mandatory information element is missing
	{"unknown-message",                0x61}, // Message type non-existent or not implemented
	{"wrong-message",                  0x62}, // Message not compatible with call state, non-existent or not implemented
	{"unknown-ie",                     0x63}, // Information element non-existent or not implemented
	{"invalid-ie",                     0x64}, // Invalid information element contents
	{"wrong-state-message",            0x65}, // Message not compatible with call state
	{"timeout",                        0x66}, // Recovery on timer expiry
	{"unknown-param-passed-on",        0x67}, // Parameter non-existent or not implemented, passed on
	{"unknown-param-message-droppped", 0x6e}, // Message with unrecognized parameter, discarded
	{"protocol-error",                 0x6f}, // Protocol error, unspecified
	// interworking class
	{"interworking",                   0x7f}, // Interworking, unspecified
	{0,0}
	};

// Q.931 4.5.5. Information transfer capability: Bits 0-4
// Defined for CCITT coding standard
static const TokenDict s_dict_transferCapCCITT[] = {
	{"speech",       0x00},          // Speech
	{"udi",          0x08},          // Unrestricted digital information
	{"rdi",          0x09},          // Restricted digital information
	{"3.1khz-audio", 0x10},          // 3.1 khz audio
	{"udi-ta",       0x11},          // Unrestricted digital information with tone/announcements
	{"video",        0x18},          // Video
	{0,0}
	};

// Q.931 4.5.5. Transfer mode: Bits 5,6
// Defined for CCITT coding standard
static const TokenDict s_dict_transferModeCCITT[] = {
	{"circuit",      0x00},          // Circuit switch mode
	{"packet",       0x02},          // Packet mode
	{0,0}
	};

// Q.931 4.5.5. Transfer rate: Bits 0-4
// Defined for CCITT coding standard
static const TokenDict s_dict_transferRateCCITT[] = {
	{"packet",        0x00},         // Packet mode only
	{"64kbit",        0x10},         // 64 kbit/s
	{"2x64kbit",      0x11},         // 2x64 kbit/s
	{"384kbit",       0x13},         // 384 kbit/s
	{"1536kbit",      0x15},         // 1536 kbit/s
	{"1920kbit",      0x17},         // 1920 kbit/s
	{"multirate",     0x18},         // Multirate (64 kbit/s base rate)
	{0,0}
	};

// Q.931 4.5.5. User information Layer 1 protocol: Bits 0-4
// Defined for CCITT coding standard
static const TokenDict s_dict_formatCCITT[] = {
	{"v110",          0x01},         // Recomendation V.110 and X.30
	{"mulaw",         0x02},         // Recomendation G.711 mu-law
	{"alaw",          0x03},         // Recomendation G.711 A-law
	{"g721",          0x04},         // Recomendation G.721 32kbit/s ADPCM and I.460
	{"h221",          0x05},         // Recomendation H.221 and H.242
	{"h223",          0x06},         // Recomendation H.223 and H.245 videoconference
	{"non-CCITT",     0x07},         // Non CCITT standardized rate adaption
	{"v120",          0x08},         // Recomendation V.120
	{"x31",           0x09},         // Recomendation X.31 HDLC flag stuffing
	{0,0}
	};

const TokenDict* SignallingUtils::s_dictCCITT[5] = {
	s_dict_causeCCITT,
	s_dict_formatCCITT,
	s_dict_transferCapCCITT,
	s_dict_transferModeCCITT,
	s_dict_transferRateCCITT
	};

// Check if a comma separated list of flags has a given flag
bool SignallingUtils::hasFlag(const String& flags, const char* flag)
{
    ObjList* obj = flags.split(',',false);
    bool found = (obj->find(flag) != 0);
    TelEngine::destruct(obj);
    return found;
}

// Append a flag to a comma separated list of flags
bool SignallingUtils::appendFlag(String& flags, const char* flag)
{
    if (TelEngine::null(flag) || hasFlag(flags,flag))
	return false;
    flags.append(flag,",");
    return true;
}

// Remove a flag from a comma separated list of flags
bool SignallingUtils::removeFlag(String& flags, const char* flag)
{
    ObjList* obj = flags.split(',',false);
    ObjList* found = obj->find(flag);
    if (found) {
	obj->remove(found,true);
	flags = "";
	for (ObjList* o = obj->skipNull(); o; o = o->skipNext())
	    flags.append(*static_cast<String*>(o->get()),",");
    }
    TelEngine::destruct(obj);
    return (found != 0);
}

// Check if a list's parameter (comma separated list of flags) has a given flag
bool SignallingUtils::hasFlag(const NamedList& list, const char* param, const char* flag)
{
    const String* s = list.getParam(param);
    return s && hasFlag(*s,flag);
}

// Append a flag to a list parameter, create parameter if missing
bool SignallingUtils::appendFlag(NamedList& list, const char* param, const char* flag)
{
    String* s = list.getParam(param);
    if (s)
	return appendFlag(*s,flag);
    list.addParam(param,flag);
    return true;
}

// Add string (keyword) if found or integer parameter to a named list
void SignallingUtils::addKeyword(NamedList& list, const char* param, const TokenDict* tokens, unsigned int val)
{
    const char* value = lookup(val,tokens);
    if (value)
	list.addParam(param,value);
    else
	list.addParam(param,String(val));
}

// Dump a buffer to a list of parameters
void SignallingUtils::dumpData(const SignallingComponent* comp, NamedList& list, const char* param,
	const unsigned char* buf, unsigned int len, char sep)
{
    String raw;
    raw.hexify((void*)buf,len,sep);
    list.addParam(param,raw);
    DDebug(comp,DebugAll,"Utils::dumpData dumped %s='%s'",param,raw.safe());
}

// Dump data from a buffer to a list of parameters. The buffer is parsed until (and including)
//  the first byte with the extension bit (the most significant one) set
unsigned int SignallingUtils::dumpDataExt(const SignallingComponent* comp, NamedList& list, const char* param,
	const unsigned char* buf, unsigned int len, char sep)
{
    if (!(buf && len))
	return 0;
    unsigned int count = 0;
    for (; count < len && !(buf[count] & 0x80); count++) ;
    if (count == len) {
	Debug(comp,DebugMild,"Utils::dumpDataExt invalid ext bits for %s (len=%u)",param,len);
	return 0;
    }
    count++;
    dumpData(comp,list,param,buf,count,sep);
    return count;
}

// Decode a received buffer to a comma separated list of flags
bool SignallingUtils::decodeFlags(const SignallingComponent* comp, NamedList& list, const char* param,
	const SignallingFlags* flags, const unsigned char* buf, unsigned int len)
{
    if (!(flags && buf && len <= sizeof(unsigned int)))
	return false;
    unsigned int val = 0;
    int shift = 0;
    while (len--) {
	val |= ((unsigned int)*buf++) << shift;
	shift += 8;
    }
    String tmp;
    for (; flags->mask; flags++)
	if ((val & flags->mask) == flags->value)
	    tmp.append(flags->name,",");
    DDebug(comp,DebugAll,"Utils::decodeFlags. Decoded %s='%s' from %u",param,tmp.safe(),val);
    list.addParam(param,tmp);
    return true;
}

const TokenDict* SignallingUtils::codings()
{
    return s_dict_codingStandard;
}

const TokenDict* SignallingUtils::locations()
{
    return s_dict_location;
}

#define Q850_MAX_CAUSE 32

// Q.850 2.1
bool SignallingUtils::decodeCause(const SignallingComponent* comp, NamedList& list,
	const unsigned char* buf, unsigned int len, const char* prefix, bool isup)
{
    if (!buf)
	return false;
    if (len < 2) {
	Debug(comp,DebugNote,"Utils::decodeCause. Invalid length %u",len);
	return false;
    }
    String causeName = prefix;
    // Byte 0: Coding standard (bit 5,6), location (bit 0-3)
    unsigned char coding = (buf[0] & 0x60) >> 5;
    addKeyword(list,causeName + ".coding",codings(),coding);
    addKeyword(list,causeName + ".location",locations(),buf[0] & 0x0f);
    unsigned int crt = 1;
    // If bit 7 is 0, the next byte should contain the recomendation
    unsigned char rec = 0;
    if (!(buf[0] & 0x80)) {
	rec = buf[1] & 0x7f;
	// For ISUP there shouldn't be a recomendation byte
	if (isup)
	    Debug(comp,DebugMild,"Utils::decodeCause. Found recomendation %u for ISUP cause",rec);
	crt = 2;
    }
    if (rec)
	list.addParam(causeName + ".rec",String(rec));
    if (crt >= len) {
	Debug(comp,DebugMild,"Utils::decodeCause. Invalid length %u. Cause value is missing",len);
	list.addParam(causeName,"");
	return false;
    }
    // Current byte: bits 0..6: cause, bits 5,6: cause class
    addKeyword(list,causeName,dict(0,coding),buf[crt] & 0x7f);
    // Rest of data: diagnostic
    crt++;
    if (crt < len)
	dumpData(comp,list,causeName + ".diagnostic",buf + crt,len - crt);
    return true;
}

// Decode bearer capabilities as defined in Q.931 (Bearer Capabilities) and Q.763 (User Service Information)
// Q.931 - 4.5.5 / Q.763 - 3.57
// The given sections in comments are from Q.931
bool SignallingUtils::decodeCaps(const SignallingComponent* comp, NamedList& list, const unsigned char* buf,
	unsigned int len, const char* prefix, bool isup)
{
    if (!buf)
	return false;
    if (len < 2) {
	Debug(comp,DebugMild,"Utils::decodeCaps. Invalid length %u",len);
	return false;
    }
    String capsName = prefix;
    // Byte 0: Coding standard (bit 5,6), Information transfer capability (bit 0-4)
    // Byte 1: Transfer mode (bit 5,6), Transfer rate (bit 0-4)
    unsigned char coding = (buf[0] & 0x60) >> 5;
    addKeyword(list,capsName + ".coding",codings(),coding);
    addKeyword(list,capsName + ".transfercap",dict(2,coding),buf[0] & 0x1f);
    addKeyword(list,capsName + ".transfermode",dict(3,coding),(buf[1] & 0x60) >> 5);
    u_int8_t rate = buf[1] & 0x1f;
    addKeyword(list,capsName + ".transferrate",dict(4,coding),rate);
    // Figure 4.11 Note 1: Next byte is the rate multiplier if the transfer rate is 'multirate' (0x18)
    u_int8_t crt = 2;
    if (rate == 0x18) {
	if (len < 3) {
	    Debug(comp,DebugMild,"Utils::decodeCaps. Invalid length %u. No rate multiplier",len);
	    return false;
	}
	addKeyword(list,capsName + ".multiplier",0,buf[2] & 0x7f);
	crt = 3;
    }
    // Get optional extra information
    // Layer 1 data
    if (len <= crt)
	return true;
    u_int8_t ident = (buf[crt] & 0x60) >> 5;
    if (ident != 1) {
	Debug(comp,DebugNote,"Utils::decodeCaps. Invalid layer 1 ident %u",ident);
	return true;
    }
    addKeyword(list,capsName,dict(1,coding),buf[crt] & 0x1f);
    //TODO: Decode the rest of Layer 1, Layer 2 and Layer 3 data
    return true;
}

// Encode a comma separated list of flags. Flags can be prefixed with the '-'
//  character to be reset if previously set
void SignallingUtils::encodeFlags(const SignallingComponent* comp,
	int& dest, const String& flags, const TokenDict* dict)
{
    if (flags.null() || !dict)
	return;
    ObjList* list = flags.split(',',false);
    DDebug(comp,DebugAll,"Utils::encodeFlags '%s' dest=0x%x",flags.c_str(),dest);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	bool set = !s->startSkip("-",false);
	const TokenDict* p = dict;
	for (; p->token && *s != p->token; p++) ;
	if (!p->token) {
	    DDebug(comp,DebugAll,"Utils::encodeFlags '%s' not found",s->c_str());
	    continue;
	}
	DDebug(comp,DebugAll,"Utils::encodeFlags %sset %s=0x%x",
	    set?"":"re",p->token,p->value);
	if (set)
	    dest |= p->value;
	else
	    dest &= ~p->value;
    }
    TelEngine::destruct(list);
}

// Encode a comma separated list of signalling flags
unsigned int SignallingUtils::encodeFlags(const SignallingComponent* comp, const String& flags,
    const SignallingFlags* dict, const char* paramName)
{
    if (!dict)
	return 0;
    unsigned int v = 0;
    ObjList* l = flags.split(',',false);
    for (ObjList* o = l->skipNull(); o; o = o->skipNext()) {
	const String* s = static_cast<const String*>(o->get());
	for (const SignallingFlags* d = dict; d->mask; d++) {
	    if (*s == d->name) {
		if (v & d->mask) {
		    Debug(comp,DebugMild,"Flag %s. %s overwriting bits 0x%x",
			paramName,d->name,v & d->mask);
		    v &= d->mask;
		}
		v |= d->value;
	    }
	}
    }
    TelEngine::destruct(l);
    return v;
}

// Q.850 2.1
bool SignallingUtils::encodeCause(const SignallingComponent* comp, DataBlock& buf,
	const NamedList& params, const char* prefix, bool isup, bool fail)
{
    u_int8_t data[4] = {2,0x80,0x80,0x80};
    String causeName = prefix;
    // Coding standard (0: CCITT) + location. If no location, set it to 0x0a: "BI"
    unsigned char coding = fixValue(params,causeName + ".coding",codings(),0x03,0x60,5);
    unsigned char loc = (unsigned char)params.getIntValue(causeName + ".location",locations(),0x0a);
    data[1] |= (coding << 5) | (loc & 0x0f);
    // Recommendation (only for Q.931)
    if (!isup) {
	unsigned char rec = (unsigned char)params.getIntValue(causeName + ".rec",0,0);
	// Add recommendation. Clear bit 7 of the first byte
	data[1] &= 0x7f;
	data[2] |= (rec & 0x7f);
	data[0] = 3;
    }
    // Value. Set to normal-clearing if missing for CCITT encoding or
    //  to 0 for other encoding standards
    unsigned char val = (unsigned char)params.getIntValue(causeName,dict(0,coding),!coding ? 0x10 : 0);
    data[data[0]] |= (val & 0x7f);
    // Diagnostic
    DataBlock diagnostic;
    const char* tmp = params.getValue(causeName + ".diagnostic");
    if (tmp)
	diagnostic.unHexify(tmp,strlen(tmp),' ');
    // Set data
    if (!isup && diagnostic.length() + data[0] + 1 > 32) {
	Debug(comp,fail?DebugNote:DebugMild,
	    "Utils::encodeCause. Cause length %u > 32. %s",
	    diagnostic.length() + data[0] + 1,fail?"Fail":"Skipping diagnostic");
	if (fail)
	    return false;
	diagnostic.clear();
    }
    u_int8_t len = data[0] + 1;
    data[0] += diagnostic.length();
    buf.assign(data,len);
    buf += diagnostic;
    return true;
}

bool SignallingUtils::encodeCaps(const SignallingComponent* comp, DataBlock& buf, const NamedList& params,
	const char* prefix, bool isup)
{
    u_int8_t data[5] = {2,0x80,0x80,0x80,0x80};
    String capsName = prefix;
    // Byte 1: Coding standard (bit 5,6), Information transfer capability (bit 0-4)
    // Byte 2: Transfer mode (bit 5,6), Transfer rate (bit 0-4)
    unsigned char coding = fixValue(params,capsName + ".coding",codings(),0x03,0x60,5);
    unsigned char cap = (unsigned char)params.getIntValue(capsName + ".transfercap",dict(2,coding),0);
    unsigned char mode = fixValue(params,capsName + ".transfermode",dict(3,coding),0x03,0x60,5);
    unsigned char rate = (unsigned char)params.getIntValue(capsName + ".transferrate",dict(4,coding),0x10);
    data[1] |= (coding << 5) | (cap & 0x1f);
    data[2] |= (mode << 5) | (rate & 0x1f);
    if (rate == 0x18) {
	data[0] = 3;
	rate = (unsigned char)params.getIntValue(capsName + ".multiplier",0,0);
	data[3] |= rate & 0x7f;
    }
    // User information layer data
    // Bit 7 = 1, Bits 5,6 = layer (1), Bits 0-4: the value
    int format = params.getIntValue(capsName,dict(1,coding),-1);
    if (format != -1) {
	data[data[0] + 1] |= 0x20 | (((unsigned char)format) & 0x1f);
	data[0]++;
    }
    buf.assign(data,data[0] + 1);
    return true;
}

// Parse a list of integers or integer intervals. Source elements must be separated by a
//   '.' or ',' character. Integer intervals must be separated by a '-' character.
// Empty elements are silently discarded
unsigned int* SignallingUtils::parseUIntArray(const String& source,
	unsigned int min, unsigned int max,
	unsigned int& count, bool discardDup)
{
    count = 0;
    ObjList* list = source.split(((-1!=source.find(','))?',':'.'),false);
    if (!list->count()) {
	TelEngine::destruct(list);
	return 0;
    }

    unsigned int maxArray = 0;
    unsigned int* array = 0;
    bool ok = true;
    int first, last;

    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	// Get the interval (may be a single value)
	int sep = s->find('-');
	if (sep == -1)
	    first = last = s->toInteger(-1);
	else {
	    first = s->substr(0,sep).toInteger(-1);
	    last = s->substr(sep + 1).toInteger(-2);
	}
	if (first < 0 || last < 0 || last < first) {
	    ok = false;
	    break;
	}
	// Resize and copy array if not enough room
	unsigned int len = (unsigned int)(last - first + 1);
	if (count + len > maxArray) {
	    maxArray = count + len;
	    unsigned int* tmp = new unsigned int[maxArray];
	    if (array) {
		::memcpy(tmp,array,sizeof(unsigned int) * count);
		delete[] array;
	    }
	    array = tmp;
	}
	// Add to array code list
	for (; first <= last; first++) {
	    // Check interval
	    if ((unsigned int)first < min || max < (unsigned int)first) {
		ok = false;
		break;
	    }
	    // Check duplicates
	    if (discardDup) {
		bool dup = false;
		for (unsigned int i = 0; i < count; i++)
		    if (array[i] == (unsigned int)first) {
			dup = true;
			break;
		    }
		if (dup)
		    continue;
	    }
	    array[count++] = first;
	}
	if (!ok)
	    break;
    }
    TelEngine::destruct(list);

    if (ok && count)
	return array;
    count = 0;
    if (array)
	delete[] array;
    return 0;
}


/*
 * SignallingMessageTimerList
 */
// Add a pending operation to the list. Start its timer
SignallingMessageTimer* SignallingMessageTimerList::add(SignallingMessageTimer* m,
    const Time& when)
{
    if (!m)
	return 0;
    m->stop();
    m->start(when.msec());
    if (m->global().interval() && !m->global().started())
	m->global().start(when.msec());
    ObjList* ins = skipNull();
    for (; ins; ins = ins->skipNext()) {
	SignallingMessageTimer* crt = static_cast<SignallingMessageTimer*>(ins->get());
	if (m->fireTime() < crt->fireTime())
	    break;
    }
    if (!ins)
	append(m);
    else
	ins->insert(m);
    return m;
}

// Check if the first operation timed out
SignallingMessageTimer* SignallingMessageTimerList::timeout(const Time& when)
{
    ObjList* o = skipNull();
    if (!o)
	return 0;
    SignallingMessageTimer* m = static_cast<SignallingMessageTimer*>(o->get());
    if (!(m->timeout(when.msec()) || m->global().timeout(when.msec())))
	return 0;
    o->remove(false);
    return m;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
