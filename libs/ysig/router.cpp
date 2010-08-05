/**
 * router.cpp
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#include "yatesig.h"
#include <yatephone.h>

using namespace TelEngine;

typedef GenPointer<SS7Layer3> L3Pointer;
typedef GenPointer<SS7Layer4> L4Pointer;

// Control operations
static const TokenDict s_dict_control[] = {
    { "pause", SS7Router::Pause },
    { "resume", SS7Router::Resume },
    { "traffic", SS7Router::Traffic },
    { "advertise", SS7Router::Advertise },
    { "prohibit", SS7MsgSNM::TFP },
    { "restrict", SS7MsgSNM::TFR },
    { "congest", SS7MsgSNM::TFC },
    { "allow", SS7MsgSNM::TFA },
    { "allowed", SS7MsgSNM::TRA },
    { 0, 0 }
};

static const TokenDict s_dict_states[] = {
    { "prohibit", SS7Route::Prohibited },
    { "unknown", SS7Route::Unknown },
    { "restrict", SS7Route::Restricted },
    { "congest", SS7Route::Congestion },
    { "allow", SS7Route::Allowed },
    { 0, 0 }
};

static SS7Route::State routeState(SS7MsgSNM::Type cmd)
{
    switch (cmd) {
	case SS7MsgSNM::TFP:
	    return SS7Route::Prohibited;
	case SS7MsgSNM::TFR:
	    return SS7Route::Restricted;
	case SS7MsgSNM::TFC:
	    return SS7Route::Congestion;
	case SS7MsgSNM::TFA:
	case SS7MsgSNM::TRA:
	    return SS7Route::Allowed;
	default:
	    return SS7Route::Unknown;
    }
}

/**
 * SS7Route
 */
// Attach a network to use for this destination or change its priority
void SS7Route::attach(SS7Layer3* network, SS7PointCode::Type type)
{
    if (!network)
	return;
    unsigned int priority = network->getRoutePriority(type,m_packed);
    // No route to point code ?
    if (priority == (unsigned int)-1)
	return;
    Lock lock(this);
    m_changes++;
    // Remove from list if already there
    detach(network);
    // Insert
    if (priority == 0) {
	m_networks.insert(new L3Pointer(network));
	return;
    }
    for (ObjList* o = m_networks.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (!*p)
	    continue;
	if (priority <= (*p)->getRoutePriority(type,m_packed)) {
	    o->insert(new L3Pointer(network));
	    return;
	}
    }
    m_networks.append(new L3Pointer(network));
}

// Remove a network from the list without deleting it
bool SS7Route::detach(SS7Layer3* network)
{
    Lock lock(this);
    ObjList* o = m_networks.skipNull();
    if (!network)
	return o != 0;
    for (; o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (*p && *p == network) {
	    m_changes++;
	    m_networks.remove(p,false);
	    break;
	}
    }
    return 0 != m_networks.skipNull();
}

// Check if a network is in the list (thread safe)
bool SS7Route::hasNetwork(const SS7Layer3* network)
{
    if (!network)
	return false;
    Lock lock(this);
    for (ObjList* o = m_networks.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (*p && network == *p)
	    return true;
    }
    return false;
}

// Check if a network is in the list (const but unsafe)
bool SS7Route::hasNetwork(const SS7Layer3* network) const
{
    if (!network)
	return false;
    for (ObjList* o = m_networks.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (*p && network == *p)
	    return true;
    }
    return false;
}

// Check if at least one network is operational
bool SS7Route::operational(int sls)
{
    Lock lock(this);
    for (ObjList* o = m_networks.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (*p && (*p)->operational(sls))
	    return true;
    }
    return false;
}

// Try to transmit a MSU through one of the attached networks
int SS7Route::transmitMSU(const SS7Router* router, const SS7MSU& msu,
	const SS7Label& label, int sls)
{
    lock();
    ObjList* o;
    do {
	for (o = m_networks.skipNull(); o; o = o->skipNext()) {
	    L3Pointer* p = static_cast<L3Pointer*>(o->get());
	    RefPointer<SS7Layer3> l3 = static_cast<SS7Layer3*>(*p);
	    if (!l3)
		continue;
	    XDebug(router,DebugAll,"Attempting transmitMSU on L3=%p '%s' [%p]",
		(void*)l3,l3->toString().c_str(),router);
	    int chg = m_changes;
	    unlock();
	    int res = l3->transmitMSU(msu,label,sls);
	    if (res != -1)
		return res;
	    lock();
	    // if list has changed break with o not null so repeat the scan
	    if (chg != m_changes)
		break;
	}
    } while (o);
    unlock();
    return -1;
}


/**
 * SS7Router
 */
SS7Router::SS7Router(const NamedList& params)
    : SignallingComponent(params.safe("SS7Router"),&params),
      Mutex(true,"SS7Router"),
      m_changes(0), m_transfer(false), m_phase2(false), m_started(false),
      m_restart(0), m_isolate(0),
      m_rxMsu(0), m_txMsu(0), m_fwdMsu(0),
      m_mngmt(0), m_maint(0)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7Router::SS7Router(%p) [%p]%s",
	    &params,this,tmp.c_str());
    }
#endif
    m_transfer = params.getBoolValue("transfer");
    m_restart.interval(params,"starttime",5000,(m_transfer ? 60000 : 10000),false);
    m_isolate.interval(params,"isolation",500,1000,false);
}

SS7Router::~SS7Router()
{
    Debug(this,DebugInfo,"SS7Router destroyed, rx=%lu, tx=%lu, fwd=%lu",
	m_rxMsu,m_txMsu,m_fwdMsu);
}

bool SS7Router::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
        config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7Router::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue("debuglevel_router",
	    config->getIntValue("debuglevel",-1)));
	m_transfer = config->getBoolValue("transfer",m_transfer);
	const String* param = config->getParam("management");
	const char* name = "ss7snm";
	if (param) {
	    if (*param && !param->toBoolean(false))
		name = param->c_str();
	}
	else
	    param = config;
	if (param->toBoolean(true)) {
	    NamedPointer* ptr = YOBJECT(NamedPointer,param);
	    NamedList* mConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
	    NamedList params(name);
	    params.addParam("basename",name);
	    if (mConfig)
		params.copyParams(*mConfig);
	    else {
		params.copySubParams(*config,params + ".");
		mConfig = &params;
	    }
	    attach(m_mngmt = YSIGCREATE(SS7Management,&params));
	}
	param = config->getParam("maintenance");
	name = "ss7mtn";
	if (param) {
	    if (*param && !param->toBoolean(false))
		name = param->c_str();
	}
	else
	    param = config;
	if (param->toBoolean(true)) {
	    NamedPointer* ptr = YOBJECT(NamedPointer,param);
	    NamedList* mConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
	    NamedList params(name);
	    params.addParam("basename",name);
	    if (mConfig)
		params.copyParams(*mConfig);
	    else {
		params.copySubParams(*config,params + ".");
		mConfig = &params;
	    }
	    attach(m_maint = YSIGCREATE(SS7Maintenance,&params));
	}
    }
    return m_started || (config && !config->getBoolValue("autostart")) || restart();
}

bool SS7Router::operational(int sls) const
{
    if (!m_started || m_isolate.started())
	return false;
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if ((*p)->operational(sls))
	    return true;
    }
    return false;
}

bool SS7Router::restart()
{
    Debug(this,DebugNote,"Restart of %s initiated [%p]",
	(m_transfer ? "STP" : "SN"),this);
    Lock mylock(this);
    m_phase2 = false;
    m_started = false;
    m_isolate.stop();
    m_restart.start();
    return true;
}

void SS7Router::disable()
{
    Debug(this,DebugNote,"MTP operation is disabled [%p]",this);
    Lock mylock(this);
    m_phase2 = false;
    m_started = false;
    m_isolate.stop();
    m_restart.stop();
}

// Attach a SS7 Layer 3 (network) to the router
void SS7Router::attach(SS7Layer3* network)
{
    if (!network || network == this)
	return;
    SignallingComponent::insert(network);
    lock();
    bool add = true;
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (*p == network) {
	    add = false;
	    break;
	}
    }
    if (add) {
	m_changes++;
	m_layer3.append(new L3Pointer(network));
	Debug(this,DebugAll,"Attached network (%p,'%s') [%p]",
	    network,network->toString().safe(),this);
    }
    updateRoutes(network);
    unlock();
    network->attach(this);
}

// Detach a SS7 Layer 3 (network) from the router
void SS7Router::detach(SS7Layer3* network)
{
    if (!network)
	return;
    Lock lock(this);
    const char* name = 0;
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (*p != network)
	    continue;
	m_changes++;
	m_layer3.remove(p,false);
	removeRoutes(network);
	if (engine() && engine()->find(network)) {
	    name = network->toString().safe();
	    lock.drop();
	    network->attach(0);
	}
	Debug(this,DebugAll,"Detached network (%p,'%s') [%p]",network,name,this);
	break;
    }
}

// Attach a SS7 Layer 4 (service) to the router. Attach itself to the service
void SS7Router::attach(SS7Layer4* service)
{
    if (!service)
	return;
    SignallingComponent::insert(service);
    lock();
    bool add = true;
    for (ObjList* o = m_layer4.skipNull(); o; o = o->skipNext()) {
	L4Pointer* p = static_cast<L4Pointer*>(o->get());
	if (*p == service) {
	    add = false;
	    break;
	}
    }
    if (add) {
	m_changes++;
	m_layer4.append(new L4Pointer(service));
	Debug(this,DebugAll,"Attached service (%p,'%s') [%p]",
	    service,service->toString().safe(),this);
    }
    unlock();
    service->attach(this);
}

// Detach a SS7 Layer 4 (service) from the router. Detach itself from the service
void SS7Router::detach(SS7Layer4* service)
{
    if (!service)
	return;
    Lock lock(this);
    for (ObjList* o = m_layer4.skipNull(); o; o = o->skipNext()) {
	L4Pointer* p = static_cast<L4Pointer*>(o->get());
	if (*p != service)
	    continue;
	m_changes++;
	m_layer4.remove(p,false);
	if (service == (SS7Layer4*)m_mngmt)
	    m_mngmt = 0;
	if (service == (SS7Layer4*)m_maint)
	    m_maint = 0;
	const char* name = 0;
	if (engine() && engine()->find(service)) {
	    name = service->toString().safe();
	    lock.drop();
	    service->attach(0);
	}
	Debug(this,DebugAll,"Detached service (%p,'%s') [%p]",service,name,this);
	break;
    }
}

void SS7Router::timerTick(const Time& when)
{
    Lock mylock(this);
    if (m_isolate.timeout(when.msecNow())) {
	Debug(this,DebugWarn,"Node is isolated and down! [%p]",this);
	m_phase2 = false;
	m_started = false;
	m_isolate.stop();
	m_restart.stop();
	return;
    }
    if (m_started)
	return;
    // MTP restart actions
    if (m_transfer && !m_phase2) {
	if (m_restart.timeout(when.msecNow() + 5000))
	    restart2();
    }
    else if (m_restart.timeout(when.msecNow())) {
	Debug(this,DebugNote,"Restart of %s complete [%p]",
	    (m_transfer ? "STP" : "SN"),this);
	m_restart.stop();
	m_started = true;
	m_phase2 = false;
	// send TRA to all operational adjacent nodes
	sendRestart();
	// advertise all non-Prohibited routes we learned about
	if (m_transfer)
	    notifyRoutes(0,SS7Route::NotProhibited);
	// iterate and notify all user parts
	ObjList* l = &m_layer4;
	for (; l; l = l->next()) {
	    L4Pointer* p = static_cast<L4Pointer*>(l->get());
	    if (p && *p)
		(*p)->notify(this,-1);
	}
    }
}

void SS7Router::restart2()
{
    Lock mylock(this);
    if (m_phase2 || !m_transfer)
	return;
    Debug(this,DebugNote,"Restart of STP entering second phase [%p]",this);
    m_phase2 = true;
    mylock.drop();
    // advertise Prohibited routes we learned until now
    notifyRoutes(0,SS7Route::Prohibited);
}

int SS7Router::routeMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls, SS7Route::State states)
{
    XDebug(this,DebugStub,"Possibly incomplete SS7Router::routeMSU(%p,%p,%p,%d) min=%d",
	&msu,&label,network,sls,minState);
    lock();
    RefPointer<SS7Route> route = findRoute(label.type(),label.dpc().pack(label.type()),states);
    unlock();
    int slsTx = route ? route->transmitMSU(this,msu,label,sls) : -1;
    if (slsTx >= 0) {
	lock();
	m_txMsu++;
	if (network)
	    m_fwdMsu++;
	unlock();
    }
    return slsTx;
}

int SS7Router::transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls)
{
    SS7Route::State states = SS7Route::NotProhibited;
    switch (msu.getSIF()) {
	case SS7MSU::SNM:
	case SS7MSU::MTN:
	case SS7MSU::MTNS:
	    // Management and Maintenance can be sent even on prohibited routes
	    states = SS7Route::AnyState;
    }
    return routeMSU(msu,label,0,sls,states);
}

bool SS7Router::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    XDebug(this,DebugStub,"Possibly incomplete SS7Router::receivedMSU(%p,%p,%p,%d)",
	&msu,&label,network,sls);
    lock();
    m_rxMsu++;
    ObjList* l;
    do {
	for (l = &m_layer4; l; l = l->next()) {
	    L4Pointer* p = static_cast<L4Pointer*>(l->get());
	    if (!p)
		continue;
	    RefPointer<SS7Layer4> l4 = static_cast<SS7Layer4*>(*p);
	    if (!l4)
		continue;
	    XDebug(this,DebugAll,"Attempting receivedMSU to L4=%p '%s' [%p]",
		(void*)l4,l4->toString().c_str(),this);
	    int chg = m_changes;
	    unlock();
	    if (l4->receivedMSU(msu,label,network,sls))
		return true;
	    lock();
	    // if list has changed break with l not null so repeat the scan
	    if (chg != m_changes)
		break;
	}
    } while (l); // loop until the list was scanned to end
    unlock();
    return m_transfer && (routeMSU(msu,label,network,sls,SS7Route::NotProhibited) >= 0);
}

void SS7Router::routeChanged(const SS7Route* route, SS7PointCode::Type type)
{
    if (!route)
	return;
    const char* pct = SS7PointCode::lookup(type);
    String dest;
    dest << SS7PointCode(type,route->packed());
    if (dest.null())
	return;
    const char* state = lookup(route->state(),s_dict_states);
    Debug(this,DebugAll,"Destination %s:%s state changed to %s [%p]",
	pct,dest.c_str(),state,this);
    // only forward TRx if we are a STP and not in Restart Phase 1
    if (!(m_transfer && (m_started || m_phase2)))
	return;
    // and during MTP restart only advertise Route Prohibited
    if (route->state() != SS7Route::Prohibited && !m_started)
	return;
    if (m_mngmt && (route->state() != SS7Route::Unknown)) {
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext()) {
	    const SS7Route* r = static_cast<const SS7Route*>(l->get());
	    // send only to different adjacent nodes
	    if (r == route || r->priority())
		continue;
	    unsigned int local = getLocal(type);
	    for (ObjList* nl = r->m_networks.skipNull(); nl; nl = nl->skipNext()) {
		GenPointer<SS7Layer3>* n = static_cast<GenPointer<SS7Layer3>*>(nl->get());
		if (!(*n)->operational())
		    continue;
		if (route->hasNetwork(*n))
		    continue;
		unsigned int netLocal = (*n)->getLocal(type);
		if (!netLocal)
		    netLocal = local;
		if (!netLocal)
		    continue;
		// use the router's local address at most once
		if (local == netLocal)
		    local = 0;
		NamedList* ctl = m_mngmt->controlCreate(state);
		if (!ctl)
		    break;
		String addr;
		addr << pct << "," << SS7PointCode(type,netLocal) <<
		    "," << SS7PointCode(type,r->packed());
		DDebug(this,DebugAll,"Sending Route %s %s %s [%p]",
		    dest.c_str(),state,addr.c_str(),this);
		ctl->addParam("address",addr);
		ctl->addParam("destination",dest);
		ctl->setParam("automatic",String::boolText(true));
		m_mngmt->controlExecute(ctl);
	    }
	}
    }
}

void SS7Router::sendRestart(const SS7Layer3* network)
{
    if (!m_mngmt)
	return;
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext()) {
	    const SS7Route* r = static_cast<const SS7Route*>(l->get());
	    // send only to adjacent nodes
	    if (r->priority())
		continue;
	    unsigned int local = getLocal(type);
	    for (ObjList* nl = r->m_networks.skipNull(); nl; nl = nl->skipNext()) {
		GenPointer<SS7Layer3>* n = static_cast<GenPointer<SS7Layer3>*>(nl->get());
		if (network && (network != *n))
		    continue;
		if (!(*n)->operational())
		    continue;
		unsigned int netLocal = (*n)->getLocal(type);
		if (!netLocal)
		    netLocal = local;
		if (!netLocal)
		    continue;
		// use the router's local address at most once
		if (local == netLocal)
		    local = 0;
		NamedList* ctl = m_mngmt->controlCreate("restart");
		if (!ctl)
		    break;
		String addr;
		addr << SS7PointCode::lookup(type) <<
		    "," << SS7PointCode(type,netLocal) <<
		    "," << SS7PointCode(type,r->packed());
		DDebug(this,DebugAll,"Sending Restart Allowed %s [%p]",addr.c_str(),this);
		ctl->addParam("address",addr);
		ctl->setParam("automatic",String::boolText(true));
		m_mngmt->controlExecute(ctl);
		if (network)
		    break;
	    }
	}
    }
}

void SS7Router::checkRoutes()
{
    if (m_isolate.started())
	return;
    bool isolated = true;
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext()) {
	    SS7Route* r = static_cast<SS7Route*>(l->get());
	    if (r->operational())
		isolated = false;
	    else {
		if (r->state() != SS7Route::Prohibited) {
		    r->m_state = SS7Route::Prohibited;
		    routeChanged(r,type);
		}
	    }
	}
    }
    if (isolated) {
	Debug(this,DebugMild,"Node has become isolated! [%p]",this);
	m_isolate.start();
    }
}

void SS7Router::notify(SS7Layer3* network, int sls)
{
    DDebug(this,DebugInfo,"Notified %s on %p sls %d [%p]",
	(network ? (network->operational() ? "net-up" : "net-down") : "no-net"),
	network,sls,this);
    Lock lock(this);
    if (network) {
	if (network->operational()) {
	    if (m_isolate.started()) {
		Debug(this,DebugAll,"Isolation ended before restart [%p]",this);
		m_isolate.stop();
	    }
	    if (m_started)
		sendRestart(network);
	    else {
		if (!m_restart.started())
		    restart();
		network = this;
		sls = -1;
	    }
	}
	else
	    checkRoutes();
    }
    // iterate and notify all user parts
    ObjList* l = &m_layer4;
    for (; l; l = l->next()) {
	L4Pointer* p = static_cast<L4Pointer*>(l->get());
	if (p && *p)
	    (*p)->notify(network,sls);
    }
}

bool SS7Router::control(NamedList& params)
{
    String* ret = params.getParam("completion");
    const String* oper = params.getParam("operation");
    const char* cmp = params.getValue("component");
    int cmd = -1;
    if (!TelEngine::null(oper))
	cmd = oper->toInteger(s_dict_control,cmd);

    if (ret) {
	if (oper && (cmd < 0))
	    return false;
	String part = params.getValue("partword");
	if (cmp) {
	    if (toString() != cmp)
		return false;
	    for (const TokenDict* d = s_dict_control; d->token; d++)
		Module::itemComplete(*ret,d->token,part);
	    return true;
	}
	return Module::itemComplete(*ret,toString(),part);
    }

    if (!(cmp && toString() == cmp))
	return false;

    String err;
    switch (cmd) {
	case SS7Router::Pause:
	    disable();
	    return true;
	case SS7Router::Resume:
	    if (m_started || m_restart.started())
		return true;
	    // fall through
	case SS7Router::Restart:
	    return restart();
	case SS7Router::Traffic:
	    sendRestart();
	    // fall through
	case SS7Router::Status:
	    return operational();
	case SS7Router::Advertise:
	    if (!(m_transfer && (m_started || m_phase2)))
		return false;
	    notifyRoutes();
	    return true;
	case SS7MsgSNM::TRA:
	case SS7MsgSNM::TFP:
	case SS7MsgSNM::TFR:
	case SS7MsgSNM::TFA:
	    {
		SS7PointCode::Type type = SS7PointCode::lookup(params.getValue("pointcodetype"));
		if (SS7PointCode::length(type) == 0) {
		    err << "missing 'pointcodetype'";
		    break;
		}
		const String* dest = params.getParam("destination");
		if (TelEngine::null(dest)) {
		    err << "missing 'destination'";
		    break;
		}
		SS7PointCode pc;
		if (!pc.assign(*dest,type)) {
		    err << "invalid destination: " << *dest ;
		    break;
		}
		if (setRouteState(type,pc,routeState(static_cast<SS7MsgSNM::Type>(cmd))))
		    return true;
		err << "no such route: " << *dest;
	    }
	    break;
	case -1:
	    break;
	default:
	    Debug(this,DebugStub,"Unimplemented control '%s' (%0x02X) [%p]",
		oper->c_str(),cmd,this);
    }
    if (err)
	Debug(this,DebugWarn,"Control error: %s [%p]",err.c_str(),this);
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
