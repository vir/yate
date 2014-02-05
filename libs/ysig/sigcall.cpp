/**
 * sigcall.cpp
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

#include <stdlib.h>
#include <string.h>


using namespace TelEngine;

const TokenDict SignallingCircuit::s_lockNames[] = {
    {"localhw",            LockLocalHWFail},
    {"localmaint",         LockLocalMaint},
    {"lockinghw",          LockingHWFail},
    {"lockingmaint",       LockingMaint},
    {"localhwchanged",     LockLocalHWFailChg},
    {"localmaintchanged",  LockLocalMaintChg},
    {"resetting",          Resetting},
    {"remotehw",           LockRemoteHWFail},
    {"remotemaint",        LockRemoteMaint},
    {"remotehwchanged",    LockRemoteHWFailChg},
    {"remotemaintchanged", LockRemoteMaintChg},
    {0,0},
};

const TokenDict SignallingCallControl::s_mediaRequired[] = {
    { "no", SignallingCallControl::MediaNever },
    { "false", SignallingCallControl::MediaNever },
    { "off", SignallingCallControl::MediaNever },
    { "disable", SignallingCallControl::MediaNever },
    { "answered", SignallingCallControl::MediaAnswered },
    { "connected", SignallingCallControl::MediaAnswered },
    { "ringing", SignallingCallControl::MediaRinging },
    { "progress", SignallingCallControl::MediaRinging },
    { "yes", SignallingCallControl::MediaAlways },
    { "true", SignallingCallControl::MediaAlways },
    { "on", SignallingCallControl::MediaAlways },
    { "enable", SignallingCallControl::MediaAlways },
    { 0, 0 }
};

/**
 * SignallingCallControl
 */
SignallingCallControl::SignallingCallControl(const NamedList& params,
	const char* msgPrefix)
    : Mutex(true,"SignallingCallControl"),
      m_mediaRequired(MediaNever),
      m_verifyEvent(false),
      m_verifyTimer(0),
      m_circuits(0),
      m_strategy(SignallingCircuitGroup::Increment),
      m_exiting(false)
{
    // Controller location
    m_location = params.getValue(YSTRING("location"));
    // Strategy
    const char* strategy = params.getValue(YSTRING("strategy"),"increment");
    m_strategy = SignallingCircuitGroup::str2strategy(strategy);
    String restrict;
    if (m_strategy != SignallingCircuitGroup::Random)
	restrict = params.getValue(YSTRING("strategy-restrict"));
    if (!restrict.null()) {
	if (restrict == "odd")
	    m_strategy |= SignallingCircuitGroup::OnlyOdd;
	else if (restrict == "even")
	    m_strategy |= SignallingCircuitGroup::OnlyEven;
	else if (restrict == "odd-fallback")
	    m_strategy |= SignallingCircuitGroup::OnlyOdd | SignallingCircuitGroup::Fallback;
	else if (restrict == "even-fallback")
	    m_strategy |= SignallingCircuitGroup::OnlyEven | SignallingCircuitGroup::Fallback;
    }

    // Message prefix
    m_msgPrefix = params.getValue(YSTRING("message-prefix"),msgPrefix);

    // Verify event timer
    m_verifyTimer.interval(params,"verifyeventinterval",10,120,true,true);
    m_verifyTimer.start();

    // Media Required
    m_mediaRequired = (MediaRequired)params.getIntValue(YSTRING("needmedia"),
	s_mediaRequired,m_mediaRequired);
}

SignallingCallControl::~SignallingCallControl()
{
    attach((SignallingCircuitGroup*)0);
}

// Attach a signalling circuit group. Set its strategy
SignallingCircuitGroup* SignallingCallControl::attach(SignallingCircuitGroup* circuits)
{
    Lock mylock(this);
    // Don't attach if it's the same object
    if (m_circuits == circuits)
	return 0;
    cleanup(circuits ? "circuit group attach" : "circuit group detach");
    if (m_circuits && circuits)
	Debug(DebugNote,
	    "SignallingCallControl. Replacing circuit group (%p) with (%p) [%p]",
	    m_circuits,circuits,this);
    SignallingCircuitGroup* tmp = m_circuits;
    m_circuits = circuits;
    if (m_circuits) {
	Lock lock(m_circuits);
	m_circuits->setStrategy(m_strategy);
    }
    return tmp;
}

// Reserve a circuit from a given list in attached group
bool SignallingCallControl::reserveCircuit(SignallingCircuit*& cic, const char* range,
	int checkLock, const String* list, bool mandatory, bool reverseRestrict)
{
    DDebug(DebugAll,"SignallingCallControl::reserveCircuit(%p,'%s',%d,'%s',%s,%s) [%p]",
	cic,range,checkLock,TelEngine::c_safe(list),
	String::boolText(mandatory),String::boolText(reverseRestrict),this);
    Lock mylock(this);
    releaseCircuit(cic);
    if (!m_circuits)
	return false;
    if (list) {
	int s = -1;
	if (!mandatory && reverseRestrict) {
	    s = m_circuits->strategy();
	    // Use the opposite strategy restriction
	    if (s & SignallingCircuitGroup::OnlyEven)
		s = (s & ~SignallingCircuitGroup::OnlyEven) | SignallingCircuitGroup::OnlyOdd;
	    else if (s & SignallingCircuitGroup::OnlyOdd)
		s = (s & ~SignallingCircuitGroup::OnlyOdd) | SignallingCircuitGroup::OnlyEven;
	}
	cic = m_circuits->reserve(*list,mandatory,checkLock,s,m_circuits->findRange(range));
    }
    else if (range) {
	const char* nRange = range;
	switch (nRange[0]) {
	    case '!':
		mandatory = true;
		nRange++;
		break;
	    case '?':
		mandatory = false;
		nRange++;
		break;
	}
	int num = String(nRange).toInteger();
	if (num > 0) {
	    // Specific circuit required
	    SignallingCircuit* circuit = m_circuits->find(num);
	    if (circuit && !circuit->locked(checkLock) && circuit->reserve()) {
		if (circuit->ref())
		    cic = circuit;
		else
		    m_circuits->release(circuit);
	    }
	    if (cic || mandatory)
		return (cic != 0);
	    DDebug(DebugInfo,"SignallingCallControl. Fallback, circuit %u not available [%p]",num,this);
	}
	cic = m_circuits->reserve(checkLock,-1,m_circuits->findRange(range));
    }
    else
	cic = m_circuits->reserve(checkLock,-1);
    return (cic != 0);
}

// Release a given circuit
bool SignallingCallControl::releaseCircuit(SignallingCircuit*& cic, bool sync)
{
    if (!cic)
	return false;
    bool ok = cic->status(SignallingCircuit::Idle,sync);
    DDebug(DebugAll,"SignallingCallControl. Released circuit %u [%p]",cic->code(),this);
    cic->deref();
    cic = 0;
    return ok;
}

bool SignallingCallControl::releaseCircuit(unsigned int code, bool sync)
{
    Lock mylock(this);
    SignallingCircuit* cic = m_circuits ? m_circuits->find(code) : 0;
    if (!cic)
	return false;
    return cic->status(SignallingCircuit::Idle,sync);
}

// Get events from calls
// Raise Disable event when no more calls and exiting
SignallingEvent* SignallingCallControl::getEvent(const Time& when)
{
    lock();
    // Verify ?
    if (m_verifyEvent && m_verifyTimer.timeout(when.msec())) {
	SignallingMessage* msg = new SignallingMessage;
	SignallingEvent* event = new SignallingEvent(SignallingEvent::Verify,msg,this);
	buildVerifyEvent(msg->params());
	TelEngine::destruct(msg);
	setVerify(true,false,&when);
	unlock();
	return event;
    }
    ListIterator iter(m_calls);
    for (;;) {
	SignallingCall* call = static_cast<SignallingCall*>(iter.get());
	// End of iteration?
	if (!call)
	    break;
	RefPointer<SignallingCall> callRef = call;
	// Dead pointer?
	if (!callRef)
	    continue;
	unlock();
	SignallingEvent* event = callRef->getEvent(when);
	// Check if this call controller wants the event
	if (event && !processEvent(event))
	    return event;
	lock();
    }
    unlock();
    // Get events from circuits not reserved
    // TODO: Find a better way to parse circuit list to get events
    Lock lckCtrl(this);
    if (m_circuits) {
	Lock lckCic(m_circuits);
	for (ObjList* o = m_circuits->circuits().skipNull(); o; o = o->skipNext()) {
	    SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
	    if (cic->status() == SignallingCircuit::Reserved)
		continue;
	    SignallingCircuitEvent* ev = cic->getEvent(when);
	    if (!ev)
		continue;
	    SignallingEvent* event = processCircuitEvent(ev);
	    if (event)
		return event;
	}
    }
    // Terminate if exiting and no more calls
    //TODO: Make sure we raise this event one time only
    if (exiting() && !m_calls.skipNull())
	return new SignallingEvent(SignallingEvent::Disable,0,this);
    return 0;
}

// Clear call list
void SignallingCallControl::clearCalls()
{
    lock();
    m_calls.clear();
    unlock();
}

// Remove a call from list
void SignallingCallControl::removeCall(SignallingCall* call, bool del)
{
    if (!call)
	return;
    lock();
    if (m_calls.remove(call,del))
	DDebug(DebugAll,
	    "SignallingCallControl. Call (%p) removed%s from queue [%p]",
	    call,(del ? " and deleted" : ""),this);
    unlock();
}

// Set the verify event flag. Restart/fire verify timer
void SignallingCallControl::setVerify(bool restartTimer, bool fireNow, const Time* time)
{
    m_verifyEvent = true;
    if (!restartTimer)
	return;
    m_verifyTimer.stop();
    if (!fireNow)
	m_verifyTimer.start(time ? time->msec() : Time::msecNow());
    else
	m_verifyTimer.fire();
}


/**
 * SignallingCall
 */
SignallingCall::SignallingCall(SignallingCallControl* controller, bool outgoing, bool signalOnly)
    : Mutex(true,"SignallingCall"),
    m_lastEvent(0),
    m_overlap(false),
    m_controller(controller),
    m_outgoing(outgoing),
    m_signalOnly(signalOnly),
    m_inMsgMutex(true,"SignallingCall::inMsg"),
    m_private(0)
{
}

SignallingCall::~SignallingCall()
{
    lock();
    m_inMsg.clear();
    if (m_controller)
	m_controller->removeCall(this,false);
    unlock();
}

// Event termination notification
void SignallingCall::eventTerminated(SignallingEvent* event)
{
    Lock mylock(this);
    if (!m_lastEvent || !event || m_lastEvent != event)
	return;
    XDebug(DebugAll,"SignallingCall. Event (%p,'%s') terminated [%p]",event,event->name(),this);
    m_lastEvent = 0;
}

void SignallingCall::enqueue(SignallingMessage* msg)
{
    if (!msg)
	return;
    Lock lock(m_inMsgMutex);
    m_inMsg.append(msg);
    XDebug(DebugAll,"SignallingCall. Enqueued message (%p,'%s') [%p]",
	msg,msg->name(),this);
}

// Dequeue a received message
SignallingMessage* SignallingCall::dequeue(bool remove)
{
    Lock lock(m_inMsgMutex);
    ObjList* obj = m_inMsg.skipNull();
    if (!obj)
	return 0;
    SignallingMessage* msg = static_cast<SignallingMessage*>(obj->get());
    if (remove) {
	m_inMsg.remove(msg,false);
	XDebug(DebugAll,"SignallingCall. Dequeued message (%p,'%s') [%p]",
	   msg,msg->name(),this);
    }
    return msg;
}


/**
 * SignallingEvent
 */
const TokenDict SignallingEvent::s_types[] = {
	{"Unknown",  Unknown},
	{"Generic",  Generic},
	{"NewCall",  NewCall},
	{"Accept",   Accept},
	{"Connect",  Connect},
	{"Complete", Complete},
	{"Progress", Progress},
	{"Ringing",  Ringing},
	{"Answer",   Answer},
	{"Transfer", Transfer},
	{"Suspend",  Suspend},
	{"Resume",   Resume},
	{"Release",  Release},
	{"Info",     Info},
	{"Charge",   Charge},
	{"Message",  Message},
	{"Facility", Facility},
	{"Circuit",  Circuit},
	{"Enable",   Enable},
	{"Disable",  Disable},
	{"Reset",    Reset},
	{"Verify",   Verify},
	{0,0}
	};

SignallingEvent::SignallingEvent(Type type, SignallingMessage* message, SignallingCall* call)
    : m_type(type), m_message(0), m_call(0), m_controller(0), m_cicEvent(0)
{
    if (call && call->ref()) {
	m_call = call;
	m_controller = call->controller();
    }
    if (message && message->ref())
	m_message = message;
}

SignallingEvent::SignallingEvent(Type type, SignallingMessage* message, SignallingCallControl* controller)
    : m_type(type), m_message(0), m_call(0), m_controller(controller), m_cicEvent(0)
{
    if (message && message->ref())
	m_message = message;
}

// Constructor for a signalling circuit related event
SignallingEvent::SignallingEvent(SignallingCircuitEvent*& event, SignallingCall* call)
    : m_type(Circuit), m_message(0), m_call(0), m_controller(0), m_cicEvent(event)
{
    event = 0;
    if (call && call->ref()) {
	m_call = call;
	m_controller = call->controller();
    }
}

SignallingEvent::~SignallingEvent()
{
    m_controller = 0;
    if (m_message)
	m_message->deref();
    if (m_call) {
	m_call->eventTerminated(this);
	m_call->deref();
    }
    TelEngine::destruct(m_cicEvent);
}

bool SignallingEvent::sendEvent()
{
    if (m_call)
	return m_call->sendEvent(this);
    delete this;
    return false;
}


/**
 * SignallingCircuitEvent
 */
SignallingCircuitEvent::SignallingCircuitEvent(SignallingCircuit* cic, Type type, const char* name)
    : NamedList(name),
    m_circuit(0),
    m_type(type)
{
    XDebug(DebugAll,"SignallingCircuitEvent::SignallingCircuitEvent() [%p]",this);
    if (cic && cic->ref())
	m_circuit = cic;
}

SignallingCircuitEvent::~SignallingCircuitEvent()
{
    if (m_circuit) {
	m_circuit->eventTerminated(this);
	m_circuit->deref();
    }
    XDebug(DebugAll,"SignallingCircuitEvent::~SignallingCircuitEvent() [%p]",this);
}

// Send this event through the circuit. Release (delete) the event on success
bool SignallingCircuitEvent::sendEvent()
{
    bool ok = m_circuit && m_circuit->sendEvent(type(),this);
    delete this;
    return ok;
}


/**
 * SignallingCircuit
 */
static const TokenDict s_cicTypeDict[] = {
    {"TDM",     SignallingCircuit::TDM},
    {"RTP",     SignallingCircuit::RTP},
    {"IAX",     SignallingCircuit::IAX},
    {"Unknown", SignallingCircuit::Unknown},
    {"Local",   SignallingCircuit::Local},
    {0,0}
};

static const TokenDict s_cicStatusDict[] = {
    {"Missing",   SignallingCircuit::Missing},
    {"Disabled",  SignallingCircuit::Disabled},
    {"Idle",      SignallingCircuit::Idle},
    {"Reserved",  SignallingCircuit::Reserved},
    {"Starting",  SignallingCircuit::Starting},
    {"Stopping",  SignallingCircuit::Stopping},
    {"Special",   SignallingCircuit::Special},
    {"Connected", SignallingCircuit::Connected},
    {0,0}
};

SignallingCircuit::SignallingCircuit(Type type, unsigned int code,
	SignallingCircuitGroup* group, SignallingCircuitSpan* span)
    : m_mutex(true,"SignallingCircuit::operations"),
      m_group(group), m_span(span),
      m_code(code), m_type(type),
      m_status(Disabled),
      m_lock(0), m_lastEvent(0), m_noEvents(true)
{
    XDebug(m_group,DebugAll,"SignallingCircuit::SignallingCircuit [%p]",this);
}

SignallingCircuit::SignallingCircuit(Type type, unsigned int code, Status status,
	SignallingCircuitGroup* group, SignallingCircuitSpan* span)
    : m_mutex(true,"SignallingCircuit::operations"),
      m_group(group), m_span(span),
      m_code(code), m_type(type),
      m_status(status),
      m_lock(0), m_lastEvent(0), m_noEvents(true)
{
    XDebug(m_group,DebugAll,"SignallingCircuit::SignallingCircuit [%p]",this);
}

SignallingCircuit::~SignallingCircuit()
{
    clearEvents();
    XDebug(m_group,DebugAll,"SignallingCircuit::~SignallingCircuit [%p]",this);
}

// Set circuit data from a list of parameters
bool SignallingCircuit::setParams(const NamedList& params)
{
    bool ok = true;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = params.getParam(i);
	if (param && !setParam(param->name(),*param))
	    ok = false;
    }
    return ok;
}

// Get first event from queue
SignallingCircuitEvent* SignallingCircuit::getEvent(const Time& when)
{
    if (m_noEvents)
	return 0;
    Lock lock(m_mutex);
    if (m_lastEvent)
	return 0;
    ObjList* obj = m_events.skipNull();
    if (!obj) {
	m_noEvents = true;
	return 0;
    }
    m_lastEvent = static_cast<SignallingCircuitEvent*>(m_events.remove(obj->get(),false));
    return m_lastEvent;
}

bool SignallingCircuit::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    XDebug(m_group,DebugStub,"SignallingCircuit::sendEvent(%u,%p) [%p]",type,params,this);
    return false;
}

// Set/reset circuit flag(s)
inline bool cicFlag(SignallingCircuit* cic, bool set, int flag, int chgFlag, bool setChg)
{
    if (chgFlag) {
	if (setChg)
	    cic->setLock(chgFlag);
	else
	    cic->resetLock(chgFlag);
    }
    if (set == (0 != cic->locked(flag)))
	return false;
    if (set)
	cic->setLock(flag);
    else
	cic->resetLock(flag);
    return true;
}

// Set/reset HW failure lock flag
bool SignallingCircuit::hwLock(bool set, bool remote, bool changed, bool setChanged)
{
    Lock lock(m_mutex);
    int flag = remote ? LockRemoteHWFail : LockLocalHWFail;
    int chgFlag = changed ? (remote ? LockRemoteHWFailChg : LockLocalHWFailChg) : 0;
    return cicFlag(this,set,flag,chgFlag,setChanged);
}

// Set/reset maintenance lock flag
bool SignallingCircuit::maintLock(bool set, bool remote, bool changed, bool setChanged)
{
    Lock lock(m_mutex);
    int flag = remote ? LockRemoteMaint : LockLocalMaint;
    int chgFlag = changed ? (remote ? LockRemoteMaintChg : LockLocalMaintChg) : 0;
    return cicFlag(this,set,flag,chgFlag,setChanged);
}

// Add event to queue
void SignallingCircuit::addEvent(SignallingCircuitEvent* event)
{
    if (!event)
	return;
    Lock lock(m_mutex);
    m_noEvents = false;
    m_events.append(event);
}

// Clear event queue
void SignallingCircuit::clearEvents()
{
    Lock lock(m_mutex);
    m_events.clear();
}

// Event termination notification
void SignallingCircuit::eventTerminated(SignallingCircuitEvent* event)
{
    Lock lock(m_mutex);
    if (event && m_lastEvent == event) {
	XDebug(m_group,DebugAll,"Event (%p) '%s' terminated for cic=%u [%p]",
	    event,event->c_str(),code(),this);
	m_lastEvent = 0;
    }
}

// Get the text associated with a circuit type
const char* SignallingCircuit::lookupType(int type)
{
    return lookup(type,s_cicTypeDict);
}

// Get the text associated with a circuit status
const char* SignallingCircuit::lookupStatus(int status)
{
    return lookup(status,s_cicStatusDict);
}

/**
 * SignallingCircuitRange
 */
SignallingCircuitRange::SignallingCircuitRange(const String& rangeStr,
	const char* name, int strategy)
    : String(name),
      m_count(0), m_last(0),
      m_strategy(strategy),
      m_used(0)
{
    add(rangeStr);
}

// Allocate and return an array containing range circuits
unsigned int* SignallingCircuitRange::copyRange(unsigned int& count) const
{
    if (!m_count)
	return 0;
    count = m_count;
    unsigned int* tmp = new unsigned int[count];
    ::memcpy(tmp,range(),m_range.length());
    return tmp;
}

// Add codes to this range from a string
bool SignallingCircuitRange::add(const String& rangeStr)
{
    unsigned int n = 0;
    unsigned int* p = SignallingUtils::parseUIntArray(rangeStr,0,(unsigned int)-1,n,true);
    if (!p)
	return false;
    add(p,n);
    delete[] p;
    return true;
}

// Add an array of circuit codes to this range
void SignallingCircuitRange::add(unsigned int* codes, unsigned int len)
{
    if (!(codes && len))
	return;
    m_range.append(codes,len*sizeof(unsigned int));
    m_count += len;
    updateLast();
}

// Add a compact range of circuit codes to this range
void SignallingCircuitRange::add(unsigned int first, unsigned int last)
{
    if (first > last)
	return;
    unsigned int count = last - first + 1;
    DataBlock data(0,count*sizeof(unsigned int));
    unsigned int* codes = (unsigned int*)data.data();
    for (unsigned int i = 0; i < count; i++)
	codes[i] = first+i;
    m_range.append(data);
    m_count += count;
    updateLast();
}

// Remove a circuit code from this range
void SignallingCircuitRange::remove(unsigned int code)
{
    unsigned int* d = (unsigned int*)range();
    for (unsigned int i = 0; i < count(); i++)
	if (d[i] == code)
	    d[i] = 0;
    updateLast();
}


// Check if a circuit code is within this range
bool SignallingCircuitRange::find(unsigned int code)
{
    if (!range())
	return false;
    for (unsigned int i = 0; i < count(); i++)
	if (range()[i] == code)
	    return true;
    return false;
}

// Update last circuit code
void SignallingCircuitRange::updateLast()
{
    m_last = 0;
    for (unsigned int i = 0; i < count(); i++)
	if (m_last <= range()[i])
	    m_last = range()[i] + 1;
}


/**
 * SignallingCircuitGroup
 */
const TokenDict SignallingCircuitGroup::s_strategy[] = {
	{"increment", Increment},
	{"decrement", Decrement},
	{"lowest",    Lowest},
	{"highest",   Highest},
	{"random",    Random},
	{0,0}
	};

SignallingCircuitGroup::SignallingCircuitGroup(unsigned int base, int strategy, const char* name)
    : SignallingComponent(name),
      Mutex(true,"SignallingCircuitGroup"),
      m_range(String::empty(),name,strategy),
      m_base(base)
{
    setName(name);
    XDebug(this,DebugAll,"SignallingCircuitGroup::SignallingCircuitGroup() [%p]",this);
}

// Set circuits status to Missing. Clear circuit list
// Clear span list
SignallingCircuitGroup::~SignallingCircuitGroup()
{
    clearAll();
    XDebug(this,DebugAll,"SignallingCircuitGroup::~SignallingCircuitGroup() [%p]",this);
}

// Find a circuit by code
SignallingCircuit* SignallingCircuitGroup::find(unsigned int cic, bool local)
{
    if (!local) {
	if (cic < m_base)
	    return 0;
	cic -= m_base;
    }
    Lock mylock(this);
    if (cic >= m_range.m_last)
	return 0;
    ObjList* l = m_circuits.skipNull();
    for (; l; l = l->skipNext()) {
	SignallingCircuit* c = static_cast<SignallingCircuit*>(l->get());
	if (c->code() == cic)
	    return c;
    }
    return 0;
}

// Find a range of circuits owned by this group
SignallingCircuitRange* SignallingCircuitGroup::findRange(const char* name)
{
    Lock mylock(this);
    ObjList* obj = m_ranges.find(name);
    return obj ? static_cast<SignallingCircuitRange*>(obj->get()) : 0;
}

void SignallingCircuitGroup::getCicList(String& dest)
{
    dest = "";
    Lock mylock(this);
    for (ObjList* l = m_circuits.skipNull(); l; l = l->skipNext()) {
	SignallingCircuit* c = static_cast<SignallingCircuit*>(l->get());
	dest.append(String(c->code()),",");
    }
}

// Insert a circuit if not already in the list
bool SignallingCircuitGroup::insert(SignallingCircuit* circuit)
{
    if (!circuit)
	return false;
    Lock mylock(this);
    if (m_circuits.find(circuit) || find(circuit->code(),true))
	return false;
    circuit->m_group = this;
    m_circuits.append(circuit);
    m_range.add(circuit->code());
    return true;
}

// Remove a circuit from list. Update maximum circuit code
void SignallingCircuitGroup::remove(SignallingCircuit* circuit)
{
    if (!circuit)
	return;
    Lock mylock(this);
    if (!m_circuits.remove(circuit,false))
	return;
    circuit->m_group = 0;
    m_range.remove(circuit->code());
    // TODO: remove from all ranges
}

// Append a span to the list if not already there
bool SignallingCircuitGroup::insertSpan(SignallingCircuitSpan* span)
{
    if (!span)
	return false;
    Lock mylock(this);
    if (!m_spans.find(span))
	m_spans.append(span);
    return true;
}

SignallingCircuitSpan* SignallingCircuitGroup::buildSpan(const String& name, unsigned int start, NamedList* param)
{
    // Local class used to pass the circuit group pointer to the span
    class VoiceParams : public NamedList
    {
    public:
	inline VoiceParams(const char* name, SignallingCircuitGroup* group)
	    : NamedList(name), m_group(group)
	    { }
	virtual void* getObject(const String& name) const
	    { return (name == YSTRING("SignallingCircuitGroup")) ? m_group : NamedList::getObject(name); }
	SignallingCircuitGroup* m_group;
    };

    VoiceParams params(debugName(),this);
    params << "/" << name;
    params.addParam("voice",name);
    if (param)
	params.copyParams(*param);
    if (start)
	params.addParam("start",String(start));
    return YSIGCREATE(SignallingCircuitSpan,&params);
}

// Build and insert a range from circuits belonging to a given span
void SignallingCircuitGroup::insertRange(SignallingCircuitSpan* span, const char* name,
	int strategy)
{
    if (!span)
	return;
    if (!name)
	name = span->id();
    Lock mylock(this);
    String tmp;
    for (ObjList* o = m_circuits.skipNull(); o; o = o->skipNext()) {
	SignallingCircuit* c = static_cast<SignallingCircuit*>(o->get());
	if (span == c->span())
	    tmp.append(String(c->code()),",");
    }
    mylock.drop();
    insertRange(tmp,name,strategy);
}

// Build and insert a range contained in a string
void SignallingCircuitGroup::insertRange(const String& range, const char* name,
	int strategy)
{
    Lock mylock(this);
    if (findRange(name))
	return;
    if (strategy < 0)
	strategy = m_range.m_strategy;
    m_ranges.append(new SignallingCircuitRange(range,name,strategy));
    Debug(this,DebugNote,"Added range %s: %s [%p]",name,range.c_str(),this);
}

// Remove a span from list
void SignallingCircuitGroup::removeSpan(SignallingCircuitSpan* span, bool delCics, bool delSpan)
{
    if (!span)
	return;
    Lock mylock(this);
    if (delCics)
	removeSpanCircuits(span);
    m_spans.remove(span,delSpan);
}

// Remove circuits belonging to a span
void SignallingCircuitGroup::removeSpanCircuits(SignallingCircuitSpan* span)
{
    if (!span)
	return;
    Lock mylock(this);
    ListIterator iter(m_circuits);
    for (GenObject* obj = 0; (obj = iter.get());) {
	SignallingCircuit* c = static_cast<SignallingCircuit*>(obj);
	if (span == c->span()) {
	    remove(c);
	    TelEngine::destruct(c);
	}
    }
}

// Get the status of a circuit given by its code
SignallingCircuit::Status SignallingCircuitGroup::status(unsigned int cic)
{
    Lock mylock(this);
    SignallingCircuit* circuit = find(cic);
    return circuit ? circuit->status() : SignallingCircuit::Missing;
}

// Change the status of a circuit given by its code
bool SignallingCircuitGroup::status(unsigned int cic, SignallingCircuit::Status newStat,
	bool sync)
{
    Lock mylock(this);
    SignallingCircuit* circuit = find(cic);
    return circuit && circuit->status(newStat,sync);
}

inline void adjustParity(unsigned int& n, int strategy, bool up)
{
    if (((strategy & SignallingCircuitGroup::OnlyEven) && (n & 1)) ||
	((strategy & SignallingCircuitGroup::OnlyOdd) && !(n & 1))) {
	if (up)
	    n++;
	else if (n)
	    n--;
	else
	    n = (strategy & SignallingCircuitGroup::OnlyEven) ? 0 : 1;
    }
}

// Choose the next circuit code to check, depending on strategy
unsigned int SignallingCircuitGroup::advance(unsigned int n, int strategy,
	SignallingCircuitRange& range)
{
    // Increment by 2 when even or odd only circuits are requested
    unsigned int delta = (strategy & (OnlyOdd|OnlyEven)) ? 2 : 1;
    switch (strategy & 0xfff) {
	case Increment:
	case Lowest:
	    n += delta;
	    if (n >= range.m_last) {
		n = 0;
		adjustParity(n,strategy,true);
	    }
	    break;
	case Decrement:
	case Highest:
	    if (n >= delta)
		n -= delta;
	    else {
		n = range.m_last;
		adjustParity(n,strategy,false);
	    }
	    break;
	default:
	    n = (n + 1) % range.m_last;
	    break;
    }
    return n;
}

// Reserve a circuit
SignallingCircuit* SignallingCircuitGroup::reserve(int checkLock, int strategy,
	SignallingCircuitRange* range)
{
    DDebug(this,DebugInfo,"SignallingCircuitGroup::reserve(%d,%d,%p) [%p]",
	checkLock,strategy,range,this);
    Lock mylock(this);
    if (!range)
	range = &m_range;
    if (range->m_last < 1)
	return 0;
    if (strategy < 0)
	strategy = range->m_strategy;
    bool up = true;
    unsigned int n = range->m_used;
    // first adjust the last used channel number
    switch (strategy & 0xfff) {
	case Increment:
	    n = (n + 1) % range->m_last;
	    break;
	case Decrement:
	    if (n < 2)
		n = range->m_last;
	    else
		n--;
	    up = false;
	    break;
	case Lowest:
	    n = 0;
	    break;
	case Highest:
	    n = range->m_last;
	    up = false;
	    break;
	default:
	    while ((range->m_last > 1) && (n == range->m_used))
		n = Random::random() % range->m_last;
    }
    // then go to the proper even/odd start circuit
    adjustParity(n,strategy,up);
    // remember where the scan started
    unsigned int start = n;
    // try at most how many channels we have, halve that if we only scan even or odd
    unsigned int i = range->m_last;
    if (strategy & (OnlyOdd|OnlyEven))
	i = (i + 1) / 2;
    while (i--) {
	// Check if the circuit is within range
	if (range->find(n)) {
	    SignallingCircuit* circuit = find(n,true);
	    if (circuit && !circuit->locked(checkLock) && circuit->reserve()) {
		if (circuit->ref()) {
		    range->m_used = n;
		    return circuit;
		}
		release(circuit);
		return 0;
	    }
	}
	n = advance(n,strategy,*range);
	// if wrapped around bail out, don't scan again
	if (n == start)
	    break;
    }
    mylock.drop();
    if (strategy & Fallback) {
	if (strategy & OnlyEven) {
	    Debug(this,DebugNote,"No even circuits available, falling back to odd [%p]",this);
	    return reserve(checkLock,OnlyOdd | (strategy & 0xfff),range);
	}
	if (strategy & OnlyOdd) {
	    Debug(this,DebugNote,"No odd circuits available, falling back to even [%p]",this);
	    return reserve(checkLock,OnlyEven | (strategy & 0xfff),range);
	}
    }
    return 0;
}

// Reserve a circuit from the given list
// Reserve another one if not found and not mandatory
SignallingCircuit* SignallingCircuitGroup::reserve(const String& list, bool mandatory,
	int checkLock, int strategy, SignallingCircuitRange* range)
{
    DDebug(this,DebugInfo,"SignallingCircuitGroup::reserve('%s',%s,%d,%d,%p) [%p]",
	list.c_str(),String::boolText(mandatory),checkLock,strategy,range,this);
    Lock mylock(this);
    if (!range)
	range = &m_range;
    // Check if any of the given circuits are free
    while (true) {
	if (list.null())
	    break;
	ObjList* circuits = list.split(',',false);
	if (!circuits)
	    break;
	SignallingCircuit* circuit = 0;
	for (ObjList* obj = circuits->skipNull(); obj; obj = obj->skipNext()) {
	    int code = (static_cast<String*>(obj->get()))->toInteger(-1);
	    if (code > 0 && range->find(code))
		circuit = find(code,false);
	    if (circuit && !circuit->locked(checkLock) && circuit->reserve()) {
		if (circuit->ref()) {
		    range->m_used = m_base + circuit->code();
		    break;
		}
		release(circuit);
	    }
	    circuit = 0;
	}
	TelEngine::destruct(circuits);
	if (circuit)
	    return circuit;
	break;
    }
    // Don't try to reserve another one if the given list is mandatory
    if (mandatory)
	return 0;
    return reserve(checkLock,strategy,range);
}

// Clear data
void SignallingCircuitGroup::clearAll()
{
    Lock mylock(this);
    // Remove spans and their circuits
    ListIterator iter(m_spans);
    for (GenObject* obj = 0; (obj = iter.get());)
	removeSpan(static_cast<SignallingCircuitSpan*>(obj),true,true);
    // Remove the rest of circuits. Reset circuits' group
    // Some of them may continue to exists after clearing the list
    for (ObjList* l = m_circuits.skipNull(); l; l = l->skipNext()) {
	SignallingCircuit* c = static_cast<SignallingCircuit*>(l->get());
	c->status(SignallingCircuit::Missing,true);
	c->m_group = 0;
    }
    m_circuits.clear();
    m_ranges.clear();
}


/**
 * SignallingCircuitSpan
 */
SignallingCircuitSpan::SignallingCircuitSpan(const char* id, SignallingCircuitGroup* group)
    : SignallingComponent(id),
      m_group(group), m_increment(0), m_id(id)
{
    if (m_group)
	m_group->insertSpan(this);
    XDebug(DebugAll,"SignallingCircuitSpan::SignallingCircuitSpan() '%s' [%p]",id,this);
}

SignallingCircuitSpan::~SignallingCircuitSpan()
{
    if (m_group)
	m_group->removeSpan(this,true,false);
    XDebug(DebugAll,"SignallingCircuitSpan::~SignallingCircuitSpan() '%s' [%p]",m_id.safe(),this);
}


/**
 * AnalogLine
 */
const TokenDict* AnalogLine::typeNames()
{
    static const TokenDict names[] = {
	{"FXO",       FXO},
	{"FXS",       FXS},
	{"recorder",  Recorder},
	{"monitor",   Monitor},
	{0,0}
    };
    return names;
}

const TokenDict* AnalogLine::stateNames()
{
    static const TokenDict names[] = {
	{"OutOfService",   OutOfService},
	{"Idle",           Idle},
	{"Dialing",        Dialing},
	{"DialComplete",   DialComplete},
	{"Ringing",        Ringing},
	{"Answered",       Answered},
	{"CallEnded",      CallEnded},
	{"OutOfOrder",     OutOfOrder},
	{0,0}
	};
    return names;
}

const TokenDict* AnalogLine::csNames() {
    static const TokenDict names[] = {
	{"after",  After},
	{"before", Before},
	{"none",   NoCallSetup},
	{0,0}
	};
    return names;
}

inline u_int64_t getValidInt(const NamedList& params, const char* param, int defVal)
{
    int tmp = params.getIntValue(param,defVal);
    return tmp >= 0 ? tmp : defVal;
}

// Reserve the line's circuit
AnalogLine::AnalogLine(AnalogLineGroup* grp, unsigned int cic, const NamedList& params)
    : Mutex(true,"AnalogLine"),
    m_type(Unknown),
    m_state(Idle),
    m_inband(false),
    m_echocancel(0),
    m_acceptPulseDigit(true),
    m_answerOnPolarity(false),
    m_hangupOnPolarity(false),
    m_polarityControl(false),
    m_callSetup(NoCallSetup),
    m_callSetupTimeout(0),
    m_noRingTimeout(0),
    m_alarmTimeout(0),
    m_group(grp),
    m_circuit(0),
    m_private(0),
    m_peer(0),
    m_getPeerEvent(false)
{
    // Check and set some data
    const char* error = 0;
    while (true) {
#define CHECK_DATA(test,sError) if (test) { error = sError; break; }
	CHECK_DATA(!m_group,"circuit group is missing")
	CHECK_DATA(m_group->findLine(cic),"circuit already allocated")
	SignallingCircuit* circuit = m_group->find(cic);
	if (circuit && circuit->ref())
	    m_circuit = circuit;
	CHECK_DATA(!m_circuit,"circuit is missing")
	break;
#undef CHECK_DATA
    }
    if (error) {
	Debug(m_group,DebugNote,"Can't create analog line (cic=%u): %s",
	    cic,error);
	return;
    }

    m_type = m_group->type();
    if (m_type == Recorder)
	m_type = FXO;
    m_address << m_group->toString() << "/" << m_circuit->code();
    m_inband = params.getBoolValue(YSTRING("dtmfinband"),false);
    String tmp = params.getValue(YSTRING("echocancel"));
    if (tmp.isBoolean())
	m_echocancel = tmp.toBoolean() ? 1 : -1;
    m_answerOnPolarity = params.getBoolValue(YSTRING("answer-on-polarity"),false);
    m_hangupOnPolarity = params.getBoolValue(YSTRING("hangup-on-polarity"),false);
    m_polarityControl = params.getBoolValue(YSTRING("polaritycontrol"),false);

    m_callSetup = (CallSetupInfo)lookup(params.getValue(YSTRING("callsetup")),csNames(),After);

    m_callSetupTimeout = getValidInt(params,"callsetup-timeout",2000);
    m_noRingTimeout = getValidInt(params,"ring-timeout",10000);
    m_alarmTimeout = getValidInt(params,"alarm-timeout",30000);
    m_delayDial = getValidInt(params,"delaydial",2000);

    DDebug(m_group,DebugAll,"AnalogLine() addr=%s type=%s [%p]",
	address(),lookup(m_type,typeNames()),this);

    if (!params.getBoolValue(YSTRING("out-of-service"),false)) {
	resetCircuit();
	if (params.getBoolValue(YSTRING("connect"),true))
	    connect(false);
    }
    else
	enable(false,false);
}

AnalogLine::~AnalogLine()
{
    DDebug(m_group,DebugAll,"~AnalogLine() addr=%s [%p]",address(),this);
}

// Remove old peer's peer. Set this line's peer
void AnalogLine::setPeer(AnalogLine* line, bool sync)
{
    Lock mylock(this);
    if (line == this) {
	Debug(m_group,DebugNote,"%s: Attempt to set peer to itself [%p]",
		address(),this);
	return;
    }
    if (line == m_peer) {
	if (sync && m_peer) {
	    XDebug(m_group,DebugAll,"%s: Syncing with peer (%p) '%s' [%p]",
		address(),m_peer,m_peer->address(),this);
	    m_peer->setPeer(this,false);
	}
	return;
    }
    AnalogLine* tmp = m_peer;
    m_peer = 0;
    if (tmp) {
	DDebug(m_group,DebugAll,"%s: Removed peer (%p) '%s' [%p]",
	    address(),tmp,tmp->address(),this);
	if (sync)
	    tmp->setPeer(0,false);
    }
    m_peer = line;
    if (m_peer) {
	DDebug(m_group,DebugAll,"%s: Peer set to (%p) '%s' [%p]",
	    address(),m_peer,m_peer->address(),this);
	if (sync)
	    m_peer->setPeer(this,false);
    }
}

// Reset the line circuit's echo canceller to line default echo canceller state
void AnalogLine::resetEcho(bool train)
{
    if (!(m_circuit || m_echocancel))
	return;
    bool enable = (m_echocancel > 0);
    m_circuit->setParam("echocancel",String::boolText(enable));
    if (enable && train)
	m_circuit->setParam("echotrain",String(""));
}

// Connect the line's circuit. Reset line echo canceller
bool AnalogLine::connect(bool sync)
{
    Lock mylock(this);
    bool ok = m_circuit && m_circuit->connect();
    resetEcho(true);
    if (sync && ok && m_peer)
	m_peer->connect(false);
    return ok;
}

// Disconnect the line's circuit. Reset line echo canceller
bool AnalogLine::disconnect(bool sync)
{
    Lock mylock(this);
    bool ok = m_circuit && m_circuit->disconnect();
    resetEcho(false);
    if (sync && ok && m_peer)
	m_peer->disconnect(false);
    return ok;
}

// Send an event through this line
bool AnalogLine::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    Lock mylock(this);
    if (state() == OutOfService)
	return false;
    if (m_inband &&
	(type == SignallingCircuitEvent::Dtmf || type == SignallingCircuitEvent::PulseDigit))
	return false;
    return m_circuit && m_circuit->sendEvent(type,params);
}

// Get events from the line's circuit if not out of service
AnalogLineEvent* AnalogLine::getEvent(const Time& when)
{
    Lock mylock(this);
    if (state() == OutOfService) {
	checkTimeouts(when);
	return 0;
    }

    SignallingCircuitEvent* event = m_circuit ? m_circuit->getEvent(when) : 0;
    if (!event) {
	checkTimeouts(when);
	return 0;
    }

    if ((event->type() == SignallingCircuitEvent::PulseDigit ||
	event->type() == SignallingCircuitEvent::PulseStart) &&
	!m_acceptPulseDigit) {
	DDebug(m_group,DebugInfo,"%s: ignoring pulse event '%s' [%p]",
	    address(),event->c_str(),this);
	delete event;
	return 0;
    }

    return new AnalogLineEvent(this,event);
}

// Alternate get events from this line or peer
AnalogLineEvent* AnalogLine::getMonitorEvent(const Time& when)
{
    Lock mylock(this);
    m_getPeerEvent = !m_getPeerEvent;
    AnalogLineEvent* event = 0;
    if (m_getPeerEvent) {
	event = getEvent(when);
	if (!event && m_peer)
	    event = m_peer->getEvent(when);
    }
    else {
	if (m_peer)
	    event = m_peer->getEvent(when);
	if (!event)
	    event = getEvent(when);
    }
    return event;
}

// Change the line state if neither current or new state are OutOfService
bool AnalogLine::changeState(State newState, bool sync)
{
    Lock mylock(this);
    bool ok = false;
    while (true) {
	if (m_state == newState || m_state == OutOfService || newState == OutOfService)
	    break;
	if (newState != Idle && newState < m_state)
	    break;
	DDebug(m_group,DebugInfo,"%s: changed state from %s to %s [%p]",
	    address(),lookup(m_state,stateNames()),
	    lookup(newState,stateNames()),this);
	m_state = newState;
	ok = true;
	break;
    }
    if (sync && ok && m_peer)
	m_peer->changeState(newState,false);
    return true;
}

// Enable/disable line. Change circuit's state to Disabled/Reserved when
//  entering/exiting the OutOfService state
bool AnalogLine::enable(bool ok, bool sync, bool connectNow)
{
    Lock mylock(this);
    while (true) {
	if (ok) {
	    if (m_state != OutOfService)
		break;
	    Debug(m_group,DebugInfo,"%s: back in service [%p]",address(),this);
	    m_state = Idle;
	    if (m_circuit) {
		m_circuit->status(SignallingCircuit::Reserved);
		if (connectNow)
		    connect(false);
	    }
	    break;
	}
	// Disable
	if (m_state == OutOfService)
	    break;
	Debug(m_group,DebugNote,"%s: out of service [%p]",address(),this);
	m_state = OutOfService;
	disconnect(false);
	if (m_circuit)
	    m_circuit->status(SignallingCircuit::Disabled);
	break;
    }
    if (sync && m_peer)
	m_peer->enable(ok,false,connectNow);
    return true;
}

// Deref the circuit
void AnalogLine::destroyed()
{
    lock();
    disconnect(false);
    if (m_circuit)
	m_circuit->status(SignallingCircuit::Idle);
    setPeer(0,true);
    if (m_group)
	m_group->removeLine(this);
    TelEngine::destruct(m_circuit);
    unlock();
    RefObject::destroyed();
}


/**
 * AnalogLineGroup
 */

// Construct an analog line group owning single lines
AnalogLineGroup::AnalogLineGroup(AnalogLine::Type type, const char* name, bool slave)
    : SignallingCircuitGroup(0,SignallingCircuitGroup::Increment,name),
    m_type(type),
    m_fxo(0),
    m_slave(false)
{
    setName(name);
    if (m_type == AnalogLine::FXO)
	m_slave = slave;
    XDebug(this,DebugAll,"AnalogLineGroup() [%p]",this);
}

// Constructs an FXS analog line monitor
AnalogLineGroup::AnalogLineGroup(const char* name, AnalogLineGroup* fxo)
    : SignallingCircuitGroup(0,SignallingCircuitGroup::Increment,name),
    m_type(AnalogLine::FXS),
    m_fxo(fxo)
{
    setName(name);
    if (m_fxo)
	m_fxo->debugChain(this);
    else
	Debug(this,DebugWarn,"Request to create monitor without fxo group [%p]",this);
    XDebug(this,DebugAll,"AnalogLineGroup() monitor fxo=%p [%p]",m_fxo,this);
}

AnalogLineGroup::~AnalogLineGroup()
{
    XDebug(this,DebugAll,"~AnalogLineGroup() [%p]",this);
}

// Append it to the list
bool AnalogLineGroup::appendLine(AnalogLine* line, bool destructOnFail)
{
    AnalogLine::Type type = m_type;
    if (type == AnalogLine::Recorder)
	type = AnalogLine::FXO;
    if (!(line && line->type() == type && line->group() == this)) {
	if (destructOnFail)
	    TelEngine::destruct(line);
	return false;
    }
    Lock mylock(this);
    m_lines.append(line);
    DDebug(this,DebugAll,"Added line (%p) %s [%p]",line,line->address(),this);
    return true;
}

// Remove a line from the list and destruct it
void AnalogLineGroup::removeLine(unsigned int cic)
{
    Lock mylock(this);
    AnalogLine* line = findLine(cic);
    if (!line)
	return;
    removeLine(line);
    TelEngine::destruct(line);
}

// Remove a line from the list without destroying it
void AnalogLineGroup::removeLine(AnalogLine* line)
{
    if (!line)
	return;
    Lock mylock(this);
    if (m_lines.remove(line,false))
	DDebug(this,DebugAll,"Removed line %p %s [%p]",line,line->address(),this);
}

// Find a line by its circuit
AnalogLine* AnalogLineGroup::findLine(unsigned int cic)
{
    Lock mylock(this);
    for (ObjList* o = m_lines.skipNull(); o; o = o->skipNext()) {
	AnalogLine* line = static_cast<AnalogLine*>(o->get());
	if (line->circuit() && line->circuit()->code() == cic)
	    return line;
    }
    return 0;
}

// Find a line by its address
AnalogLine* AnalogLineGroup::findLine(const String& address)
{
    Lock mylock(this);
    ObjList* tmp = m_lines.find(address);
    return tmp ? static_cast<AnalogLine*>(tmp->get()) : 0;
}

// Iterate through the line list to get an event
AnalogLineEvent* AnalogLineGroup::getEvent(const Time& when)
{
    lock();
    ListIterator iter(m_lines);
    for (;;) {
	AnalogLine* line = static_cast<AnalogLine*>(iter.get());
	// End of iteration?
	if (!line)
	    break;
	RefPointer<AnalogLine> lineRef = line;
	// Dead pointer?
	if (!lineRef)
	    continue;
	unlock();
	AnalogLineEvent* event = !fxo() ? lineRef->getEvent(when) : lineRef->getMonitorEvent(when);
	if (event)
	    return event;
	lock();
    }
    unlock();
    return 0;
}

// Remove all spans and circuits. Release object
void AnalogLineGroup::destroyed()
{
    lock();
    for (ObjList* o = m_lines.skipNull(); o; o = o->skipNext()) {
	AnalogLine* line = static_cast<AnalogLine*>(o->get());
	Lock lock(line);
	line->m_group = 0;
    }
    m_lines.clear();
    TelEngine::destruct(m_fxo);
    unlock();
    SignallingCircuitGroup::destroyed();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
