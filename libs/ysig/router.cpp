/**
 * router.cpp
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
#include <yatephone.h>

using namespace TelEngine;

typedef GenPointer<SS7Layer3> L3Pointer;
typedef GenPointer<SS7Layer4> L4Pointer;

namespace { //anonymous

class L3ViewPtr : public L3Pointer
{
public:
    inline L3ViewPtr(SS7Layer3* l3)
	: L3Pointer(l3)
	{ }
    inline ObjList& view(SS7PointCode::Type type)
	{ return m_views[type-1]; }
private:
    ObjList m_views[YSS7_PCTYPE_COUNT];
};

class HeldMSU : public SS7MSU
{
    friend class TelEngine::SS7Route;
private:
    inline HeldMSU(const SS7Router* router, const SS7MSU& msu,
        const SS7Label& label, int sls, SS7Route::State states, const SS7Layer3* source)
	: SS7MSU(msu),
	  m_router(router), m_label(label), m_sls(sls),
	  m_states(states), m_source(source)
	{ }

    const SS7Router* m_router;
    const SS7Label m_label;
    int m_sls;
    SS7Route::State m_states;
    const SS7Layer3* m_source;
};

}; // anonymous namespace

// Control operations
static const TokenDict s_dict_control[] = {
    { "show", SS7Router::Status },
    { "pause", SS7Router::Pause },
    { "resume", SS7Router::Resume },
    { "restart", SS7Router::Restart },
    { "traffic", SS7Router::Traffic },
    { "advertise", SS7Router::Advertise },
    { "prohibit", SS7MsgSNM::TFP },
    { "restrict", SS7MsgSNM::TFR },
    { "congest", SS7MsgSNM::TFC },
    { "allow", SS7MsgSNM::TFA },
    { "allowed", SS7MsgSNM::TRA },
    { "test-prohibited", SS7MsgSNM::RST },
    { "test-restricted", SS7MsgSNM::RSR },
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
	case SS7MsgSNM::RST:
	    return SS7Route::Prohibited;
	case SS7MsgSNM::TFR:
	case SS7MsgSNM::RSR:
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
// Get the state to name token table
const TokenDict* SS7Route::stateNames()
{
    return s_dict_states;
}

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
    // Remove from list if already there
    detach(network);
    SS7Route* route = network->findRoute(m_type,m_packed);
    if (route) {
	if (m_maxDataLength > route->getMaxDataLength() || m_maxDataLength == 0)
	    m_maxDataLength = route->getMaxDataLength();
    }
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
	    m_networks.remove(p);
	    break;
	}
    }
    m_maxDataLength = 0;
    for (o = m_networks.skipNull(); o; o = o->skipNext()) {
	L3Pointer* p = static_cast<L3Pointer*>(o->get());
	if (!p)
	    continue;
	RefPointer<SS7Layer3> l3 = static_cast<SS7Layer3*>(*p);
	if (!l3)
	    continue;
	SS7Route* route = l3->findRoute(m_type,m_packed);
	if (route) {
	    if (m_maxDataLength > route->getMaxDataLength() ||
		    m_maxDataLength == 0)
		m_maxDataLength = route->getMaxDataLength();
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

// Check and reset congestion status
bool SS7Route::congested()
{
    if (m_congCount >= 8 || m_congBytes >= 256) {
	m_congCount = 0;
	m_congBytes = 0;
	return true;
    }
    return false;
}

// Try to transmit a MSU through one of the attached networks
int SS7Route::transmitMSU(const SS7Router* router, const SS7MSU& msu,
	const SS7Label& label, int sls, State states, const SS7Layer3* source)
{
    lock();
    if (msu.getSIF() > SS7MSU::MTNS && m_buffering) {
	if (m_state & states) {
	    // Store User Part messages in the controlled rerouting buffer
	    DDebug(router,DebugInfo,"Storing %s MSU in reroute buffer of %u",
		msu.getServiceName(),packed());
	    m_reroute.append(new HeldMSU(router,msu,label,sls,states,source));
	    sls = 0;
	}
	else
	    sls = -1;
    }
    else
	sls = transmitInternal(router,msu,label,sls,states,source);
    unlock();
    return sls;
}

// Transmit the MSU, called with the route locked
int SS7Route::transmitInternal(const SS7Router* router, const SS7MSU& msu,
    const SS7Label& label, int sls, State states, const SS7Layer3* source)
{
#ifdef DEBUG
    bool info = true;
#else
    bool info = false;
#endif
    int offs = 0;
    bool userPart = (msu.getSIF() > SS7MSU::MTNS);
    if (userPart)
	offs = sls >> shift();
    ListIterator iter(m_networks,offs);
    while (L3Pointer* p = static_cast<L3Pointer*>(iter.get())) {
	RefPointer<SS7Layer3> l3 = static_cast<SS7Layer3*>(*p);
	if (!l3 || (l3 == source) ||
	    !(l3->getRouteState(label.type(),label.dpc(),userPart) & states))
	    continue;
	unlock();
	XDebug(router,DebugAll,"Attempting transmitMSU %s on L3=%p '%s' [%p]",
	    msu.getServiceName(),(void*)l3,l3->toString().c_str(),router);
	int res = l3->transmitMSU(msu,label,sls);
	lock();
	if (res != -1) {
	    unsigned int cong = l3->congestion(res);
	    if (cong) {
		m_congCount++;
		m_congBytes += msu.length();
	    }
	    if (info) {
		String addr;
		addr << label;
		Debug(router,DebugInfo,"MSU %s size %u sent on %s:%d%s",
		    addr.c_str(),msu.length(),l3->toString().c_str(),res,
		    (cong ? " (congested)" : ""));
	    }
	    return res;
	}
	info = true;
    }
    Debug(router,DebugMild,"Could not send %s MSU size %u on any linkset",
	msu.getServiceName(),msu.length());
    return -1;
}

void SS7Route::rerouteCheck(u_int64_t when)
{
    lock();
    if (m_buffering && m_buffering <= when) {
	if (m_state & Prohibited)
	    rerouteFlush();
	unsigned int c = 0;
	while (HeldMSU* msu = static_cast<HeldMSU*>(m_reroute.remove(false))) {
	    transmitInternal(msu->m_router,*msu,msu->m_label,msu->m_sls,
		msu->m_states,msu->m_source);
	    TelEngine::destruct(msu);
	    c++;
	}
	if (c)
	    Debug(DebugNote,"Released %u MSUs from reroute buffer of %u",c,packed());
	m_buffering = 0;
    }
    unlock();
}

void SS7Route::rerouteFlush()
{
    if (!m_buffering)
	return;
    lock();
    unsigned int c = m_reroute.count();
    if (c)
	Debug(DebugMild,"Flushed %u MSUs from reroute buffer of %u",c,packed());
    m_reroute.clear();
    m_buffering = 0;
    unlock();
}

void SS7Route::reroute()
{
    XDebug(DebugAll,"Initiating controlled rerouting to %u",packed());
    lock();
    m_buffering = Time::now() + 800000;
    unlock();
}


/**
 * SS7Router
 */
SS7Router::SS7Router(const NamedList& params)
    : SignallingComponent(params.safe("SS7Router"),&params,"ss7-router"),
      Mutex(true,"SS7Router"),
      m_changes(0), m_transfer(false), m_phase2(false), m_started(false),
      m_restart(0), m_isolate(0), m_statsMutex(false,"SS7RouterStats"),
      m_trafficOk(0), m_trafficSent(0), m_routeTest(0), m_testRestricted(false),
      m_transferSilent(false), m_checkRoutes(false), m_autoAllowed(false),
      m_sendUnavail(true), m_sendProhibited(true),
      m_rxMsu(0), m_txMsu(0), m_fwdMsu(0), m_failMsu(0), m_congestions(0),
      m_mngmt(0)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7Router::SS7Router(%p) [%p]%s",
	    &params,this,tmp.c_str());
    }
#endif
    const String* tr = params.getParam(YSTRING("transfer"));
    if (!TelEngine::null(tr)) {
	m_transferSilent = (*tr == YSTRING("silent"));
	m_transfer = !m_transferSilent && tr->toBoolean();
    }
    setNI(SS7MSU::getNetIndicator(params.getValue(YSTRING("netindicator")),SS7MSU::National));
    m_autoAllowed = params.getBoolValue(YSTRING("autoallow"),m_autoAllowed);
    m_sendUnavail = params.getBoolValue(YSTRING("sendupu"),m_sendUnavail);
    m_sendProhibited = params.getBoolValue(YSTRING("sendtfp"),m_sendProhibited);
    m_restart.interval(params,"starttime",5000,(m_transfer ? 60000 : 10000),false);
    m_isolate.interval(params,"isolation",500,1000,true);
    m_routeTest.interval(params,"testroutes",10000,50000,true),
    m_trafficOk.interval(m_restart.interval() + 4000);
    m_trafficSent.interval(m_restart.interval() + 8000);
    m_testRestricted = params.getBoolValue(YSTRING("testrestricted"),m_testRestricted);
    loadLocalPC(params);
    const String* param = params.getParam(YSTRING("management"));
    const char* name = "ss7snm";
    if (param) {
	if (*param && !param->toBoolean(false))
	    name = param->c_str();
    }
    else
	param = &params;
    if (param->toBoolean(true)) {
	NamedPointer* ptr = YOBJECT(NamedPointer,param);
	NamedList* mConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
	NamedList mParams(name);
	mParams.addParam("basename",name);
	if (mConfig)
	    mParams.copyParams(*mConfig);
	else {
	    if (params.hasSubParams(mParams + "."))
		mParams.copySubParams(params,mParams + ".");
	    else
		mParams.addParam("local-config","true");
	    mConfig = &mParams;
	}
	attach(m_mngmt = YSIGCREATE(SS7Management,&mParams));
    }
}

SS7Router::~SS7Router()
{
    Debug(this,DebugInfo,"SS7Router destroyed, rx=%lu, tx=%lu, fwd=%lu, fail=%lu, cong=%lu",
	m_rxMsu,m_txMsu,m_fwdMsu,m_failMsu,m_congestions);
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
	debugLevel(config->getIntValue(YSTRING("debuglevel_router"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	const String* tr = config->getParam(YSTRING("transfer"));
	if (!TelEngine::null(tr)) {
	    m_transferSilent = (*tr == YSTRING("silent"));
	    m_transfer = !m_transferSilent && tr->toBoolean(m_transfer);
	}
	m_autoAllowed = config->getBoolValue(YSTRING("autoallow"),m_autoAllowed);
	m_sendUnavail = config->getBoolValue(YSTRING("sendupu"),m_sendUnavail);
	m_sendProhibited = config->getBoolValue(YSTRING("sendtfp"),m_sendProhibited);
    }
    if (m_mngmt)
	SignallingComponent::insert(m_mngmt);
    return m_started || (config && !config->getBoolValue(YSTRING("autostart"))) || restart();
}

void SS7Router::loadLocalPC(const NamedList& params)
{
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++)
	m_local[i] = 0;
    unsigned int n = params.length();
    for (unsigned int i= 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!ns)
	    continue;
	if (ns->name() != "local")
	    continue;
	ObjList* route = ns->split(',',true);
	ObjList* obj = route->skipNull();
	SS7PointCode pc;
	SS7PointCode::Type type = SS7PointCode::Other;
	do {
	    if (!obj)
		break;
	    type = SS7PointCode::lookup(obj->get()->toString());
	    obj = obj->skipNext();
	    if (obj)
		pc.assign(obj->get()->toString(),type);
	} while (false);
	TelEngine::destruct(route);
	unsigned int packed = pc.pack(type);
	if ((unsigned int)type > YSS7_PCTYPE_COUNT || !packed) {
	    Debug(this,DebugNote,"Invalid %s='%s' (invalid point code%s) [%p]",
		ns->name().c_str(),ns->safe(),type == SS7PointCode::Other ? " type" : "",this);
	    continue;
	}
	m_local[type - 1] = packed;
    }
}

unsigned char SS7Router::getNI(SS7PointCode::Type pcType, unsigned char defNI) const
{
    if ((defNI & 0xc0) == 0)
	defNI <<= 6;
    if (SS7Layer3::hasType(pcType))
	return SS7Layer3::getNI(pcType,defNI);
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if ((*p)->hasType(pcType))
	    return (*p)->getNI(pcType,defNI);
    }
    return defNI;
}

unsigned int SS7Router::getDefaultLocal(SS7PointCode::Type type) const
{
    unsigned int local = getLocal(type);
    if (!local) {
	for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	    L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	    unsigned int l = (*p)->getLocal(type);
	    if (l && local && (l != local))
		return 0;
	    local = l;
	}
    }
    return local;
}

bool SS7Router::operational(int sls) const
{
    if (!m_started || m_isolate.started())
	return false;
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if ((*p)->operational(sls))
	    return true;
    }
    return false;
}

bool SS7Router::restart()
{
    Debug(this,DebugNote,"Restart of %s initiated [%p]",
	(m_transfer ? "STP" : "SN"),this);
    lock();
    m_phase2 = false;
    m_started = false;
    m_isolate.stop();
    m_routeTest.stop();
    m_trafficOk.stop();
    m_trafficSent.stop();
    m_restart.stop();
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!(*p)->operational()) {
	    clearView(*p);
	    clearRoutes(*p,false);
	}
    }
    checkRoutes();
    m_checkRoutes = true;
    m_restart.start();
    m_trafficOk.start();
    unlock();
    rerouteFlush();
    return true;
}

void SS7Router::disable()
{
    Debug(this,DebugNote,"MTP operation is disabled [%p]",this);
    lock();
    m_phase2 = false;
    m_started = false;
    m_checkRoutes = false;
    m_isolate.stop();
    m_restart.stop();
    m_routeTest.stop();
    m_trafficOk.stop();
    m_trafficSent.stop();
    unlock();
    rerouteFlush();
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
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (*p == network) {
	    add = false;
	    break;
	}
    }
    if (add) {
	m_changes++;
	m_layer3.append(new L3ViewPtr(network));
	Debug(this,DebugAll,"Attached network (%p,'%s') [%p]",
	    network,network->toString().safe(),this);
    }
    updateRoutes(network);
    buildViews();
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
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (*p != network)
	    continue;
	m_changes++;
	m_layer3.remove(p);
	removeRoutes(network);
	if (engine() && engine()->find(network)) {
	    name = network->toString().safe();
	    lock.drop();
	    network->attach(0);
	}
	Debug(this,DebugAll,"Detached network (%p,'%s') [%p]",network,name,this);
	break;
    }
    buildViews();
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
	m_layer4.remove(p);
	if (service == (SS7Layer4*)m_mngmt)
	    m_mngmt = 0;
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

void SS7Router::buildViews()
{
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!*p)
	    continue;
	for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	    SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	    buildView(type,p->view(type),*p);
	}
    }
}

void SS7Router::buildView(SS7PointCode::Type type, ObjList& view, SS7Layer3* network)
{
    view.clear();
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!*p || ((*p) == network))
	    continue;
	for (ObjList* r = (*p)->getRoutes(type); r; r = r->next()) {
	    const SS7Route* route = static_cast<const SS7Route*>(r->get());
	    if (!route)
		continue;
	    if (!network->getRoutePriority(type,route->packed()))
		continue;
	    ObjList* v;
	    for (v = view.skipNull(); v; v = v->skipNext()) {
		const SS7Route* r = static_cast<const SS7Route*>(v->get());
		if (r->packed() == route->packed())
		    break;
	    }
	    if (!v) {
		DDebug(this,DebugAll,"Creating route to %u from %s in view of %s",
		    route->packed(),(*p)->toString().c_str(),network->toString().c_str());
		view.append(new SS7Route(route->packed(),type));
	    }
	}
    }
}

void SS7Router::timerTick(const Time& when)
{
    Lock mylock(this,SignallingEngine::maxLockWait());
    if (!mylock.locked())
	return;
    if (m_isolate.timeout(when.msec())) {
	Debug(this,DebugWarn,"Node is isolated and down! [%p]",this);
	m_phase2 = false;
	m_started = false;
	m_isolate.stop();
	m_restart.stop();
	m_trafficOk.stop();
	m_trafficSent.stop();
	mylock.drop();
	rerouteFlush();
	return;
    }
    if (m_started) {
	if (m_routeTest.timeout(when.msec())) {
	    m_routeTest.start(when.msec());
	    mylock.drop();
	    sendRouteTest();
	}
	else if (m_trafficOk.timeout(when.msec())) {
	    m_trafficOk.stop();
	    silentAllow();
	}
	else if (m_trafficSent.timeout(when.msec()))
	    m_trafficSent.stop();
	mylock.drop();
	rerouteCheck(when);
	return;
    }
    // MTP restart actions
    if (m_transfer && !m_phase2) {
	if (m_restart.timeout(when.msec() + 5000))
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
	if (!m_trafficSent.started())
	    m_trafficSent.start();
	if (m_checkRoutes)
	    checkRoutes();
	// advertise all non-Prohibited routes we learned about
	if (m_transfer)
	    notifyRoutes(SS7Route::NotProhibited);
	// iterate and notify all user parts
	ObjList* l = &m_layer4;
	for (; l; l = l->next()) {
	    L4Pointer* p = static_cast<L4Pointer*>(l->get());
	    if (p && *p)
		(*p)->notify(this,-1);
	}
	if (m_routeTest.interval())
	    m_routeTest.start(when.msec());
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
    notifyRoutes(SS7Route::Prohibited);
}

int SS7Router::routeMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls, SS7Route::State states)
{
    XDebug(this,DebugStub,"Possibly incomplete SS7Router::routeMSU(%p,%p,%p,%d) states=0x%X",
	&msu,&label,network,sls,states);
    m_routeMutex.lock();
    RefPointer<SS7Route> route = findRoute(label.type(),label.dpc().pack(label.type()));
    m_routeMutex.unlock();
    int slsTx = route ? route->transmitMSU(this,msu,label,sls,states,network) : -1;
    if (slsTx >= 0) {
	bool cong = route->congested();
	if (cong) {
	    Debug(this,DebugMild,"Route to %u reports congestion",route->packed());
	    while (m_mngmt) {
		unsigned int local = getLocal(label.type());
		if (!local)
		    break;
		NamedList* ctl = m_mngmt->controlCreate("congest");
		if (!ctl)
		    break;
		String addr;
		addr << SS7PointCode::lookup(label.type()) << ",";
		addr << SS7PointCode(label.type(),local) << "," << label.opc();
		String dest;
		dest << SS7PointCode(label.type(),route->packed());
		ctl->addParam("address",addr);
		ctl->addParam("destination",dest);
		ctl->setParam("automatic",String::boolText(true));
		m_mngmt->controlExecute(ctl);
		break;
	    }
	}
	m_statsMutex.lock();
	m_txMsu++;
	if (network)
	    m_fwdMsu++;
	if (cong)
	    m_congestions++;
	m_statsMutex.unlock();
    }
    else {
	m_statsMutex.lock();
	m_failMsu++;
	m_statsMutex.unlock();
	if (!route) {
	    String tmp;
	    tmp << label.dpc();
	    Debug(this,DebugMild,"No route to %s was found for %s MSU size %u",
		tmp.c_str(),msu.getServiceName(),msu.length());
	}
	else
	    Debug(this,DebugAll,"Failed to send %s MSU size %u on %s route %u",
		msu.getServiceName(),msu.length(),route->stateName(),route->packed());
    }
    return slsTx;
}

int SS7Router::transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls)
{
    SS7Route::State states = SS7Route::NotProhibited;
    switch (msu.getSIF()) {
	case SS7MSU::SNM:
	    if ((msu.at(label.length()+1) & 0x0f) == SS7MsgSNM::MIM) {
		int res = routeMSU(msu,label,0,sls,SS7Route::AnyState);
		if (res >= 0)
		    return res;
		// now we are desperate to send a link management packet
		sls = -2;
	    }
	case SS7MSU::MTN:
	case SS7MSU::MTNS:
	    // Management and Maintenance can be sent even on prohibited routes
	    states = SS7Route::AnyState;
	    break;
	default:
	    if (!m_started)
		return -1;
    }
    return routeMSU(msu,label,0,sls,states);
}

HandledMSU SS7Router::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (m_autoAllowed && network && (msu.getSIF() > SS7MSU::MTNS)) {
	unsigned int src = label.opc().pack(label.type());
	Lock mylock(m_routeMutex);
	SS7Route* route = findRoute(label.type(),src);
	if (route && !route->priority() && (route->state() & (SS7Route::Unknown|SS7Route::Prohibited))) {
	    Debug(this,DebugNote,"Auto activating adjacent route %u on '%s' [%p]",
		src,network->toString().c_str(),this);
	    setRouteSpecificState(label.type(),src,src,SS7Route::Allowed,network);
	    if (m_transfer && m_started)
		notifyRoutes(SS7Route::KnownState,src);
	}
    }
    if ((msu.getSIF() > SS7MSU::MTNS) && !m_started)
	return HandledMSU::Failure;
    bool maint = (msu.getSIF() == SS7MSU::MTN) || (msu.getSIF() == SS7MSU::MTNS);
    if (!maint) {
	m_statsMutex.lock();
	m_rxMsu++;
	m_statsMutex.unlock();
    }
    lock();
    ObjList* l;
    HandledMSU ret;
    do {
	for (l = &m_layer4; l; l = l->next()) {
	    L4Pointer* p = static_cast<L4Pointer*>(l->get());
	    if (!p)
		continue;
	    RefPointer<SS7Layer4> l4 = static_cast<SS7Layer4*>(*p);
	    if (!l4)
		continue;
	    XDebug(this,DebugAll,"Attempting receivedMSU %s to L4=%p '%s' [%p]",
		msu.getServiceName(),(void*)l4,l4->toString().c_str(),this);
	    int chg = m_changes;
	    unlock();
	    HandledMSU handled = l4->receivedMSU(msu,label,network,sls);
	    XDebug(this,DebugAll,"L4=%p '%s' returned %u [%p]",
		(void*)l4,l4->toString().c_str(),(unsigned int)handled,this);
	    switch (handled) {
		case HandledMSU::Accepted:
		case HandledMSU::Failure:
		    return handled;
		case HandledMSU::Rejected:
		    break;
		default:
		    ret = handled;
	    }
	    lock();
	    // if list has changed break with l not null so repeat the scan
	    if (chg != m_changes)
		break;
	}
    } while (l); // loop until the list was scanned to end
    unlock();
    switch (ret) {
	// these cases are explicitely set by the user parts
	case HandledMSU::Unequipped:
	case HandledMSU::Inaccessible:
	    if (m_sendUnavail)
		return ret;
	    return HandledMSU::Failure;
	default:
	    break;
    }
    // maintenance must stop here, others may be transferred out
    if (maint)
	return HandledMSU::Rejected;
    unsigned int dpc = label.dpc().pack(label.type());
    // if packet was for this node as set in router don't process any further
    if (getLocal(label.type()) == dpc)
	return m_sendUnavail ? HandledMSU::Unequipped : HandledMSU::Failure;
    bool local = network && (network->getLocal(label.type()) == dpc);
    if (m_transfer || m_transferSilent) {
	if (routeMSU(msu,label,network,label.sls(),SS7Route::NotProhibited) >= 0)
	    return HandledMSU::Accepted;
	// not routed and not local - send TFP or just drop it silently
	if (!local)
	    return m_sendProhibited ? HandledMSU::NoAddress : HandledMSU::Failure;
    }
    if (HandledMSU::NoCircuit == ret)
	return HandledMSU::NoCircuit;
    return (local && m_sendUnavail) ? HandledMSU::Unequipped : HandledMSU::Failure;
}

// Call the route changed notification for all known routes
void SS7Router::notifyRoutes(SS7Route::State states, unsigned int onlyPC)
{
    if (SS7Route::Unknown == states)
	return;
    DDebug(this,DebugAll,"Notifying routes with states 0x%02X only to %u [%p]",
	states,onlyPC,this);
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	ListIterator iter(m_route[i]);
	while (true) {
	    SS7Route* route = static_cast<SS7Route*>(iter.get());
	    if (!route)
		break;
	    if ((route->state() & states) == 0)
		continue;
	    routeChanged(route,static_cast<SS7PointCode::Type>(i+1),0,0,onlyPC,true);
	}
    }
}

// Call the route changed notification for all known routes on a network
void SS7Router::notifyRoutes(SS7Route::State states, const SS7Layer3* network)
{
    if (SS7Route::Unknown == states || !network)
	return;
    DDebug(this,DebugAll,"Notifying routes with states 0x%02X only to '%s' [%p]",
	states,network->toString().c_str(),this);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	const ObjList* l = network->getRoutes(static_cast<SS7PointCode::Type>(i+1));
	for (; l; l = l->next()) {
	    const SS7Route* r = static_cast<const SS7Route*>(l->get());
	    if (r && !r->priority())
		notifyRoutes(states,r->packed());
	}
    }
}

// Add a network to the routing table. Clear all its routes before appending it to the table
void SS7Router::updateRoutes(SS7Layer3* network)
{
    if (!network)
	return;
    Lock lock(m_routeMutex);
    removeRoutes(network);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = (SS7PointCode::Type)(i + 1);
	for (ObjList* o = network->m_route[i].skipNull(); o; o = o->skipNext()) {
	    SS7Route* src = static_cast<SS7Route*>(o->get());
	    SS7Route* dest = findRoute(type,src->packed());
	    if (dest) {
		if (dest->priority() > src->priority())
		    dest->m_priority = src->priority();
		if (dest->shift() < src->shift())
		    dest->m_shift = src->shift();
	    }
	    else {
		dest = new SS7Route(*src);
		m_route[i].append(dest);
	    }
	    DDebug(this,DebugAll,"Add route type=%s packed=%u for network (%p,'%s') [%p]",
		SS7PointCode::lookup(type),src->m_packed,network,network->toString().safe(),this);
	    dest->attach(network,type);
	}
    }
}

// Remove the given network from all destinations in the routing table.
// Remove the entry in the routing table if empty (no more routes to the point code).
void SS7Router::removeRoutes(SS7Layer3* network)
{
    if (!network)
	return;
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	ListIterator iter(m_route[i]);
	while (true) {
	    SS7Route* route = static_cast<SS7Route*>(iter.get());
	    if (!route)
		break;
	    if (!route->detach(network)) {
		SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
		DDebug(this,DebugAll,"Removing empty route type=%s packed=%u [%p]",
		    SS7PointCode::lookup(type),route->m_packed,this);
		switch (route->state()) {
		    case SS7Route::Unknown:
		    case SS7Route::Prohibited:
			break;
		    default:
			// if an active route is removed broadcast it prohibited
			route->m_state = SS7Route::Prohibited;
			routeChanged(route,type,0,network);
		}
		m_route[i].remove(route,true);
	    }
	}
    }
    DDebug(this,DebugAll,"Removed network (%p,'%s') from routing table [%p]",
	network,network->toString().safe(),this);
}

// Route changed notification, if we are STP advertise routes to concerned neighbours
void SS7Router::routeChanged(const SS7Route* route, SS7PointCode::Type type,
    unsigned int remotePC, const SS7Layer3* network, unsigned int onlyPC, bool forced)
{
    if (!route)
	return;
    const char* pct = SS7PointCode::lookup(type);
    String dest;
    dest << SS7PointCode(type,route->packed());
    if (dest.null())
	return;
    DDebug(this,DebugAll,"Destination %s:%u state: %s set by %u only to %u [%p]",
	pct,route->packed(),route->stateName(),remotePC,onlyPC,this);
    // only forward TRx if we are a STP and not in Restart Phase 1
    if (!(m_transfer && (m_started || m_phase2)))
	return;
    // and during MTP restart only advertise Route Prohibited
    if (route->state() != SS7Route::Prohibited && !m_started)
	return;
    if (m_mngmt && (route->state() != SS7Route::Unknown)) {
	for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	    L3ViewPtr* l3p = static_cast<L3ViewPtr*>(o->get());
	    if (!l3p || ((*l3p) == network))
		continue;
	    if (!((forced && onlyPC) || (*l3p)->operational()))
		continue;
	    for (ObjList* v = l3p->view(type).skipNull(); v; v = v->skipNext()) {
		SS7Route* r = static_cast<SS7Route*>(v->get());
		if (r->packed() != route->packed())
		    continue;
		SS7Route::State state = getRouteView(type,r->packed(),0,*l3p);
		if ((r->state() == state) && !forced)
		    break;
		DDebug(this,DebugAll,"Route %u of view '%s' changed: %s -> %s",
		    r->packed(),(*l3p)->toString().c_str(),
		    SS7Route::stateName(r->state()),SS7Route::stateName(state));
		r->m_state = state;
		unsigned int local = (*l3p)->getLocal(type);
		if (!local)
		    local = getLocal(type);
		if (!local)
		    break;
		// never advertise a local point code from itself
		if (r->packed() == local)
		    break;
		const char* cmd = SS7Route::stateName(state);
		v = (*l3p)->getRoutes(type);
		if (v)
		    v = v->skipNull();
		for (; v; v = v->skipNext()) {
		    r = static_cast<SS7Route*>(v->get());
		    if (r->priority() || (r->state() == SS7Route::Prohibited))
			continue;
		    if (onlyPC && (r->packed() != onlyPC))
			continue;
		    NamedList* ctl = m_mngmt->controlCreate(cmd);
		    if (!ctl)
			break;
		    String addr;
		    addr << pct << "," << SS7PointCode(type,local) << ","
			<< SS7PointCode(type,r->packed());
		    Debug(this,DebugInfo,"Advertising Route %s %s %s [%p]",
			dest.c_str(),cmd,addr.c_str(),this);
		    ctl->addParam("address",addr);
		    ctl->addParam("destination",dest);
		    ctl->setParam("automatic",String::boolText(true));
		    m_mngmt->controlExecute(ctl);
		}
		break;
	    } // route search in view
	} // network iteration
    }
}

// Get the view of a route from a specific outside network
SS7Route::State SS7Router::getRouteView(SS7PointCode::Type type, unsigned int packedPC,
    unsigned int remotePC, const SS7Layer3* network)
{
    if (type == SS7PointCode::Other || (unsigned int)type > YSS7_PCTYPE_COUNT || !packedPC)
	return SS7Route::Unknown;
    if (remotePC && !network) {
	for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	    SS7Layer3* l3 = *static_cast<L3ViewPtr*>(o->get());
	    if (l3 && !l3->getRoutePriority(type,remotePC)) {
		network = l3;
		break;
	    }
	}
    }
    if (network && !network->allowedTo(type,packedPC)) {
	DDebug(this,DebugInfo,"View of %u from %u on %s is Prohibited",
	    packedPC,remotePC,network->toString().c_str());
	return SS7Route::Prohibited;
    }
    SS7Route* route = 0;
    if (network)
	route = const_cast<SS7Layer3*>(network)->findRoute(type,packedPC);
    SS7Route::State routeState = route ? route->state() : SS7Route::Unknown;
    unsigned int routePrio = route ? route->priority() : (unsigned int)-1;
    // combine all matching routes not on current network
    SS7Route::State best = SS7Route::Unknown;
    bool thisIsCurrent = (routeState & (SS7Route::NotProhibited|SS7Route::Unknown)) != 0;
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	SS7Layer3* l3 = *static_cast<L3ViewPtr*>(o->get());
	if (!l3 || (l3 == network))
	    continue;
	SS7Route::State state;
	if (l3->operational()) {
	    SS7Route* r = l3->findRoute(type,packedPC);
	    if (!r)
		continue;
	    if (r->priority() == routePrio) {
		// sharing - neither is allowed to send through us to the route
		DDebug(this,DebugAll,"Operational '%s' is load sharing with '%s'",
		    l3->toString().c_str(),network->toString().c_str());
		best = SS7Route::Prohibited;
		thisIsCurrent = false;
		break;
	    }
	    state = r->state();
	    if ((r->priority() < routePrio || SS7Route::Unknown == routeState) && (state & SS7Route::NotProhibited))
		thisIsCurrent = false;
	    DDebug(this,DebugAll,"Operational '%s' contributed state %s",
		l3->toString().c_str(),SS7Route::stateName(state));
	}
	else {
	    state = SS7Route::Prohibited;
	    DDebug(this,DebugAll,"Non-operational '%s' contributed state %s",
		l3->toString().c_str(),SS7Route::stateName(state));
	}
	if ((state & SS7Route::KnownState) > (best & SS7Route::KnownState))
	    best = state;
    }
    if (thisIsCurrent && (routePrio != (unsigned int)-1)) {
	DDebug(this,DebugAll,"Route is current in an alternative set");
	best = SS7Route::Prohibited;
    }
    DDebug(this,DebugInfo,"Route view of %u from %u%s%s: %s",
	packedPC,remotePC,(network ? " on " : ""),
	(network ? network->toString().c_str() : ""),SS7Route::stateName(best));
    return best;
}

void SS7Router::clearView(const SS7Layer3* network)
{
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!*p || ((*p) != network))
	    continue;
	for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	    SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	    for (ObjList* v = p->view(type).skipNull(); v; v = v->skipNext()) {
		SS7Route* r = static_cast<SS7Route*>(v->get());
		DDebug(this,DebugAll,"Route %u of view '%s' cleared: %s -> Unknown",
		    r->packed(),network->toString().c_str(),
		    SS7Route::stateName(r->state()));
		r->m_state = SS7Route::Unknown;
	    }
	}
	break;
    }
}

// Set the state of a route.
bool SS7Router::setRouteState(SS7PointCode::Type type, unsigned int packedPC, SS7Route::State state,
    unsigned int remotePC, const SS7Layer3* network)
{
    if (type == SS7PointCode::Other || (unsigned int)type > YSS7_PCTYPE_COUNT || !packedPC)
	return false;
    Lock lock(m_routeMutex);
    SS7Route* route = findRoute(type,packedPC);
    if (!route)
	return false;
    if (state != route->m_state) {
	DDebug(this,DebugAll,"Local route %u/%u changed by %u: %s -> %s",
	packedPC,route->priority(),remotePC,
	    SS7Route::stateName(route->state()),SS7Route::stateName(state));
	route->reroute();
	route->m_state = state;
	if (state != SS7Route::Unknown)
	    routeChanged(route,type,remotePC,network);
    }
    return true;
}

// Set the state of a route per source.
bool SS7Router::setRouteSpecificState(SS7PointCode::Type type, unsigned int packedPC,
    unsigned int srcPC, SS7Route::State state, const SS7Layer3* changer)
{
    if (type == SS7PointCode::Other || (unsigned int)type > YSS7_PCTYPE_COUNT || !packedPC)
	return false;
    Lock myLock(m_routeMutex);
    SS7Route* route = findRoute(type,packedPC);
    if (!route) {
	Debug(this,DebugNote,"Route to %u advertised by %u not found",packedPC,srcPC);
	return false;
    }
    SS7Route::State best = state;
    bool ok = false;
    for (ObjList* nl = route->m_networks.skipNull(); nl; nl = nl->skipNext()) {
	SS7Layer3* l3 = *static_cast<L3Pointer*>(nl->get());
	if (!l3)
	    continue;
	SS7Route* r = l3->findRoute(type,packedPC);
	if (!r) {
	    Debug(this,DebugGoOn,"Route to %u not found in network '%s'",packedPC,l3->toString().c_str());
	    continue;
	}
	ok = true;
	if (l3->getRoutePriority(type,srcPC)) {
	    DDebug(this,DebugAll,"Route %u/%u of network '%s' is: %s",
		r->packed(),r->priority(),l3->toString().c_str(),SS7Route::stateName(r->state()));
	    if (((r->state() & SS7Route::KnownState) > (best & SS7Route::KnownState)) &&
		l3->operational())
		best = r->state();
	}
	else {
	    // srcPC is adjacent STP on this network
	    DDebug(this,DebugAll,"Route %u/%u of network '%s' changed: %s -> %s",
		r->packed(),r->priority(),l3->toString().c_str(),
		SS7Route::stateName(r->state()),SS7Route::stateName(state));
	    if (r->m_state != state) {
		// controlled reroute for the entire linkset if node is adjacent
		if (!r->priority())
		    reroute(l3);
		else
		    route->reroute();
		r->m_state = state;
	    }
	}
    }
    if (srcPC && !ok) {
	Debug(this,DebugWarn,"Route to %u advertised by %u not found in any network",packedPC,srcPC);
	return false;
    }
    DDebug(this,DebugAll,"Local best route %u/%u changed by %u: %s -> %s",packedPC,
	route->priority(),srcPC,SS7Route::stateName(route->state()),SS7Route::stateName(best));
    // check if an adjacent node has been seen restarting elsewhere
    bool restartElsewhere = srcPC && (srcPC != packedPC) && !route->priority() &&
	(route->state() == SS7Route::Prohibited) && (best & SS7Route::NotProhibited);
    route->m_state = best;
    routeChanged(route,type,srcPC,changer);
    if (restartElsewhere && m_transfer && m_started) {
	DDebug(this,DebugInfo,"Adjacent node %u seen started by %u, sending TFPs",packedPC,srcPC);
	notifyRoutes(SS7Route::Prohibited,packedPC);
    }
    myLock.drop();
    SS7PointCode pc(type);
    if (!pc.unpack(type,packedPC))
	return true;
    lock();
    ListIterator iter(m_layer4);
    while (L4Pointer* p = static_cast<L4Pointer*>(iter.get())) {
	if (p && *p) {
	    RefPointer<SS7Layer4> l4 = static_cast<SS7Layer4*>(*p);
	    if (!l4)
		continue;
	    unlock();
	    l4->routeStatusChanged(type,pc,state);
	    l4 = 0;
	    lock();
	}
    }
    unlock();
    return true;
}

// Send TRA to all or just one network
void SS7Router::sendRestart(const SS7Layer3* network)
{
    if (!m_mngmt)
	return;
    DDebug(this,DebugAll,"sendRestart(%p) [%p]",network,this);
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
	    unsigned int adjacent = r->packed();
	    unsigned int local = getLocal(type);
	    for (ObjList* nl = r->m_networks.skipNull(); nl; nl = nl->skipNext()) {
		SS7Layer3* l3 = *static_cast<L3Pointer*>(nl->get());
		if (network && (network != l3))
		    continue;
		if (l3->getRoutePriority(type,adjacent))
		    continue;
		if (!l3->operational())
		    continue;
		unsigned int netLocal = l3->getLocal(type);
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
		    "," << SS7PointCode(type,adjacent);
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

// Send TRA by point code
void SS7Router::sendRestart(SS7PointCode::Type type, unsigned int packedPC)
{
    if (!packedPC)
	return;
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	SS7Layer3* l3 = *static_cast<L3ViewPtr*>(o->get());
	if (!l3)
	    continue;
	if (!l3->getRoutePriority(type,packedPC)) {
	    sendRestart(l3);
	    return;
	}
    }
}

// Mark Allowed routes from which we didn't receive even a TRA
void SS7Router::silentAllow(const SS7Layer3* network)
{
    DDebug(this,DebugInfo,"Trying to silently allow %s%s%s [%p]",
	(network ? "'" : "all linksets"),
	(network ? network->toString().c_str() : ""),(network ? "'" : ""),this);
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	SS7Layer3* l3 = *static_cast<L3ViewPtr*>(o->get());
	if (!l3)
	    continue;
	if (network && (network != l3))
	    continue;
	if (!l3->operational())
	    continue;
	SS7MTP3* mtp3 = YOBJECT(SS7MTP3,l3);
	if (mtp3 && !mtp3->linksChecked())
	    continue;
	bool noisy = true;
	for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	    SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	    unsigned int adjacent = 0;
	    for (ObjList* l = l3->getRoutes(type); l; l = l->next()) {
		SS7Route* r = static_cast<SS7Route*>(l->get());
		if (!r)
		    continue;
		if (!r->priority())
		    adjacent = r->packed();
		if (r->state() != SS7Route::Unknown)
		    continue;
		if (noisy) {
		    Debug(this,DebugNote,"Allowing unknown state routes of '%s' from %u [%p]",
			l3->toString().c_str(),adjacent,this);
		    noisy = false;
		}
		setRouteSpecificState(type,r->packed(),adjacent,SS7Route::Allowed,l3);
		if (!r->priority()) {
		    notifyRoutes(SS7Route::NotProhibited,r->packed());
		    sendRestart(l3);
		}
	    }
	}
    }
}

// Mark Allowed routes by point code
void SS7Router::silentAllow(SS7PointCode::Type type, unsigned int packedPC)
{
    if (!packedPC)
	return;
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	SS7Layer3* l3 = *static_cast<L3ViewPtr*>(o->get());
	if (!l3)
	    continue;
	if (!l3->getRoutePriority(type,packedPC)) {
	    silentAllow(l3);
	    return;
	}
    }
}

// Send RST and/or RSR to probe for routes left prohibited/restricted
void SS7Router::sendRouteTest()
{
    if (!m_mngmt)
	return;
    int cnt = 0;
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext()) {
	    const SS7Route* r = static_cast<const SS7Route*>(l->get());
	    // adjacent routes are not tested this way
	    if (!r->priority())
		continue;
	    const char* oper = 0;
	    switch (r->state()) {
		case SS7Route::Unknown:
		case SS7Route::Prohibited:
		    oper = "test-prohibited";
		    break;
		case SS7Route::Restricted:
		    if (!m_testRestricted)
			continue;
		    oper = "test-restricted";
		    break;
		default:
		    continue;
	    }
	    unsigned int local = getLocal(type);
	    for (ObjList* nl = r->m_networks.skipNull(); nl; nl = nl->skipNext()) {
		L3Pointer* n = static_cast<L3Pointer*>(nl->get());
		if (!(*n)->operational())
		    continue;
		if ((*n)->getRoutePriority(type,r->packed()) == (unsigned int)-1)
		    continue;
		unsigned int netLocal = (*n)->getLocal(type);
		if (!netLocal)
		    netLocal = local;
		if (!netLocal)
		    continue;
		unsigned int remote = 0;
		for (ObjList* l2 = (*n)->getRoutes(type); l2; l2 = l2->next()) {
		    const SS7Route* r2 = static_cast<const SS7Route*>(l2->get());
		    if (!r2)
			continue;
		    if (r2->priority() || (r2->state() != SS7Route::Allowed))
			continue;
		    remote = r2->packed();
		    break;
		}
		if (!remote)
		    continue;
		// use the router's local address at most once
		if (local == netLocal)
		    local = 0;
		NamedList* ctl = m_mngmt->controlCreate(oper);
		if (!ctl)
		    break;
		String addr;
		addr << SS7PointCode::lookup(type) <<
		    "," << SS7PointCode(type,netLocal) <<
		    "," << SS7PointCode(type,remote);
		String dest;
		dest << SS7PointCode(type,r->packed());
		DDebug(this,DebugAll,"Sending %s %s %s [%p]",
		    oper,dest.c_str(),addr.c_str(),this);
		ctl->addParam("address",addr);
		ctl->addParam("destination",dest);
		ctl->setParam("automatic",String::boolText(true));
		if (m_mngmt->controlExecute(ctl))
		    cnt++;
	    }
	}
    }
    if (cnt)
	Debug(this,DebugInfo,"Sent %d Route Test messages [%p]",cnt,this);
}

// Check if at least one adjacent route is available, start isolation if not
void SS7Router::checkRoutes(const SS7Layer3* noResume)
{
    if (m_isolate.started() || !m_isolate.interval())
	return;
    bool isolated = true;
    Lock lock(m_routeMutex);
    m_checkRoutes = false;
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext()) {
	    SS7Route* r = static_cast<SS7Route*>(l->get());
	    SS7Route::State state = getRouteView(type,r->packed());
	    if ((state & (SS7Route::NotProhibited|SS7Route::Unknown)) && !r->priority())
		isolated = false;
	    if (r->state() != state) {
		DDebug(this,DebugAll,"Local route %u/%u changed during check: %s -> %s",
		    r->packed(),r->priority(),
		    SS7Route::stateName(r->state()),SS7Route::stateName(state));
		r->m_state = state;
		routeChanged(r,type,0);
	    }
	}
    }
    if (isolated && noResume && (m_started || m_restart.started())) {
	Debug(this,DebugMild,"Node has become isolated! [%p]",this);
	m_isolate.start();
	m_trafficSent.stop();
	// we are in an emergency - uninhibit any possible link
	for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	    L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	    SS7Layer3* l3 = *p;
	    if ((l3 == noResume) || !l3)
		continue;
	    NamedList* ctl = l3->controlCreate("resume");
	    if (ctl) {
		ctl->setParam("automatic",String::boolText(true));
		ctl->setParam("emergency",String::boolText(true));
		l3->controlExecute(ctl);
	    }
	    if (!m_isolate.started())
		break;
	}
    }
}

// Clear the routes of a linkset that's not in service
void SS7Router::clearRoutes(SS7Layer3* network, bool ok)
{
    if (!network)
	return;
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = network->getRoutes(type);
	if (l)
	    l = l->skipNull();
	unsigned int adjacent = 0;
	for (; l; l = l->skipNext()) {
	    SS7Route* r = static_cast<SS7Route*>(l->get());
	    if (!r->priority())
		adjacent = r->packed();
	    if (ok && (r->state() != SS7Route::Prohibited))
		continue;
	    // if an adjacent node is operational but not in service we may have a chance
	    SS7Route::State state = (ok || !r->priority()) ? SS7Route::Unknown : SS7Route::Prohibited;
	    DDebug(DebugInfo,"Clearing route %u/%u of %s by %u to %s",
		r->packed(),r->priority(),network->toString().c_str(),
		adjacent,SS7Route::stateName(state));
	    setRouteSpecificState(type,r->packed(),adjacent,state,network);
	}
    }
}

// Initiate controlled rerouting on all routes including a linkset
void SS7Router::reroute(const SS7Layer3* network)
{
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext()) {
	    SS7Route* r = static_cast<SS7Route*>(l->get());
	    if (r->hasNetwork(network))
		r->reroute();
	}
    }
}

// Check if routes have finished controlled rerouting
void SS7Router::rerouteCheck(const Time& when)
{
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext())
	    static_cast<SS7Route*>(l->get())->rerouteCheck(when);
    }
}

// Flush the controlled rerouting buffer of all routes
void SS7Router::rerouteFlush()
{
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	const ObjList* l = getRoutes(type);
	if (l)
	    l = l->skipNull();
	for (; l; l = l->skipNext())
	    static_cast<SS7Route*>(l->get())->rerouteFlush();
    }
}

bool SS7Router::uninhibit(SS7Layer3* network, int sls, bool remote)
{
    if (!(network && m_mngmt))
	return false;
    bool ok = false;
    const char* cmd = remote ? "link-force-uninhibit" : "link-uninhibit";
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	unsigned int local = network->getLocal(type);
	if (!local)
	    local = getLocal(type);
	if (!local)
	    continue;
	for (const ObjList* o = network->getRoutes(type); o; o = o->next()) {
	    const SS7Route* r = static_cast<const SS7Route*>(o->get());
	    if (!r || r->priority())
		continue;
	    NamedList* ctl = m_mngmt->controlCreate(cmd);
	    if (!ctl)
		return false;
	    String addr;
	    addr << SS7PointCode::lookup(type) <<
		"," << SS7PointCode(type,local) <<
		"," << SS7PointCode(type,r->packed()) <<
		"," << sls;
	    DDebug(this,DebugInfo,"Requesting %s %s [%p]",cmd,addr.c_str(),this);
	    ctl->addParam("address",addr);
	    ctl->setParam("automatic",String::boolText(true));
	    m_mngmt->controlExecute(ctl);
	    ok = true;
	}
    }
    return ok;
}

bool SS7Router::inhibit(const SS7Label& link, int setFlags, int clrFlags, bool notLast)
{
    int remote = link.dpc().pack(link.type());
    if (!remote)
	return false;
    Lock mylock(this);
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!*p || (*p)->getRoutePriority(link.type(),remote))
	    continue;
	RefPointer<SS7Layer3> net = static_cast<SS7Layer3*>(*p);
	mylock.drop();
	if (notLast && setFlags) {
	    const SS7MTP3* mtp3 = YOBJECT(SS7MTP3,net);
	    if (mtp3 && (mtp3->linksActive() == 1) && !mtp3->inhibited(link.sls()))
		return false;
	}
	return net->inhibit(link.sls(),setFlags,clrFlags);
    }
    return false;
}

bool SS7Router::inhibited(const SS7Label& link, int flags)
{
    int remote = link.dpc().pack(link.type());
    if (!remote)
	return false;
    Lock mylock(this);
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!*p || (*p)->getRoutePriority(link.type(),remote))
	    continue;
	RefPointer<SS7Layer3> net = static_cast<SS7Layer3*>(*p);
	mylock.drop();
	return net->inhibited(link.sls(),flags);
    }
    return false;
}

int SS7Router::getSequence(const SS7Label& link)
{
    int remote = link.dpc().pack(link.type());
    if (!remote)
	return false;
    Lock mylock(this);
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!*p || (*p)->getRoutePriority(link.type(),remote))
	    continue;
	RefPointer<SS7Layer3> net = static_cast<SS7Layer3*>(*p);
	mylock.drop();
	return net->getSequence(link.sls());
    }
    return -1;
}

void SS7Router::recoverMSU(const SS7Label& link, int sequence)
{
    int remote = link.dpc().pack(link.type());
    if (!remote)
	return;
    Lock mylock(this);
    for (ObjList* o = m_layer3.skipNull(); o; o = o->skipNext()) {
	L3ViewPtr* p = static_cast<L3ViewPtr*>(o->get());
	if (!*p || (*p)->getRoutePriority(link.type(),remote))
	    continue;
	RefPointer<SS7Layer3> net = static_cast<SS7Layer3*>(*p);
	mylock.drop();
	net->recoverMSU(link.sls(),sequence);
	break;
    }
}

void SS7Router::receivedUPU(SS7PointCode::Type type, const SS7PointCode node,
    SS7MSU::Services part, unsigned char cause, const SS7Label& label, int sls)
{
    // Iterate and notify all User Parts
    lock();
    ListIterator iter(m_layer4);
    while (L4Pointer* p = static_cast<L4Pointer*>(iter.get())) {
	if (p && *p) {
	    RefPointer<SS7Layer4> l4 = static_cast<SS7Layer4*>(*p);
	    if (!l4)
		continue;
	    unlock();
	    l4->receivedUPU(type,node,part,cause,label,sls);
	    l4 = 0;
	    lock();
	}
    }
    unlock();
}


void SS7Router::notify(SS7Layer3* network, int sls)
{
    DDebug(this,DebugInfo,"Notified %s on %p sls %d [%p]",
	(network ? (network->operational() ? "net-up" : "net-down") : "no-net"),
	network,sls,this);
    bool useMe = false;
    Lock lock(this);
    if (network) {
	if (network->inService(sls)) {
	    if (m_isolate.started()) {
		Debug(this,DebugNote,"Isolation ended before shutting down [%p]",this);
		m_isolate.stop();
	    }
	    bool tra = true;
	    // send TRA only if a link become operational
	    if (sls >= 0)
		tra = network->operational(sls);
	    if (m_started) {
		if (tra) {
		    // send TRA only for the first activated link
		    const SS7MTP3* mtp3 = YOBJECT(SS7MTP3,network);
		    if (!mtp3 || (mtp3->linksActive() <= 1)) {
			// adjacent point restart
			clearRoutes(network,true);
			if (m_transfer)
			    notifyRoutes(SS7Route::Prohibited,network);
			sendRestart(network);
			m_trafficOk.start();
		    }
		}
	    }
	    else {
		if (!m_restart.started())
		    restart();
		else if (tra)
		    clearRoutes(network,true);
		useMe = true;
	    }
	}
	else {
	    clearView(network);
	    bool oper = network->operational(sls);
	    if (sls >= 0)
		oper = oper || network->operational();
	    clearRoutes(network,oper);
	    checkRoutes(network);
	}
	reroute(network);
    }
    // iterate and notify all user parts
    ObjList* l = &m_layer4;
    for (; l; l = l->next()) {
	L4Pointer* p = static_cast<L4Pointer*>(l->get());
	if (p && *p) {
	    SS7Layer4* l4 = *p;
	    if (useMe && (l4 != m_mngmt))
		l4->notify(this,-1);
	    else
		l4->notify(network,sls);
	}
    }
}

bool SS7Router::control(NamedList& params)
{
    String* ret = params.getParam(YSTRING("completion"));
    const String* oper = params.getParam(YSTRING("operation"));
    const char* cmp = params.getValue(YSTRING("component"));
    int cmd = -1;
    if (!TelEngine::null(oper))
	cmd = oper->toInteger(s_dict_control,cmd);

    if (ret) {
	if (oper && (cmd < 0))
	    return false;
	String part = params.getValue(YSTRING("partword"));
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

    m_autoAllowed = params.getBoolValue(YSTRING("autoallow"),m_autoAllowed);
    m_sendUnavail = params.getBoolValue(YSTRING("sendupu"),m_sendUnavail);
    m_sendProhibited = params.getBoolValue(YSTRING("sendtfp"),m_sendProhibited);
    if (!m_transfer)
	m_transferSilent = params.getBoolValue(YSTRING("transfersilent"),m_transferSilent);
    String err;
    switch (cmd) {
	case SS7Router::Pause:
	    disable();
	    return TelEngine::controlReturn(&params,true);
	case SS7Router::Resume:
	    if (m_started || m_restart.started())
		return TelEngine::controlReturn(&params,true);
	    // fall through
	case SS7Router::Restart:
	    return TelEngine::controlReturn(&params,restart());
	case SS7Router::Traffic:
	    if (!m_trafficSent.started())
		m_trafficSent.start();
	    sendRestart();
	    // fall through
	case SS7Router::Status:
	    printRoutes();
	    printStats();
	    return TelEngine::controlReturn(&params,operational());
	case SS7Router::Advertise:
	    if (!(m_transfer && (m_started || m_phase2)))
		return TelEngine::controlReturn(&params,false);
	    notifyRoutes();
	    return TelEngine::controlReturn(&params,true);
	case SS7MsgSNM::RST:
	case SS7MsgSNM::RSR:
	    if (!m_started)
		return TelEngine::controlReturn(&params,false);
	    // fall through
	case SS7MsgSNM::TRA:
	case SS7MsgSNM::TFP:
	case SS7MsgSNM::TFR:
	case SS7MsgSNM::TFA:
	    {
		SS7PointCode::Type type = SS7PointCode::lookup(params.getValue(YSTRING("pointcodetype")));
		if (SS7PointCode::length(type) == 0) {
		    err << "missing 'pointcodetype'";
		    break;
		}
		const String* dest = params.getParam(YSTRING("destination"));
		if (TelEngine::null(dest)) {
		    err << "missing 'destination'";
		    break;
		}
		SS7PointCode pc;
		if (!pc.assign(*dest,type)) {
		    err << "invalid destination: " << *dest ;
		    break;
		}
		if (SS7MsgSNM::RST == cmd || SS7MsgSNM::RSR == cmd) {
		    const String* addr = params.getParam(YSTRING("back-address"));
		    if (TelEngine::null(addr))
			addr = params.getParam(YSTRING("address"));
		    if (TelEngine::null(addr)) {
			err = "missing 'address'";
			break;
		    }
		    SS7PointCode opc;
		    ObjList* l = addr->split(',');
		    if (l->at(2))
			opc.assign(l->at(2)->toString(),type);
		    TelEngine::destruct(l);
		    SS7Route::State state = getRouteView(type,pc.pack(type),opc.pack(type));
		    if (SS7Route::Unknown == state)
			return TelEngine::controlReturn(&params,false);
		    if (routeState(static_cast<SS7MsgSNM::Type>(cmd)) == state)
			return TelEngine::controlReturn(&params,true);
		    // a route state changed, advertise to the adjacent node
		    if (!(m_transfer && m_started && m_mngmt))
			return TelEngine::controlReturn(&params,false);
		    const char* oper = lookup(state,s_dict_states);
		    if (!oper)
			return TelEngine::controlReturn(&params,false);
		    NamedList* ctl = m_mngmt->controlCreate(oper);
		    if (!ctl)
			return TelEngine::controlReturn(&params,false);
		    Debug(this,DebugInfo,"Requesting %s %s to %s [%p]",
			dest->c_str(),oper,addr->c_str(),this);
		    ctl->addParam("address",addr->c_str());
		    ctl->addParam("destination",*dest);
		    ctl->setParam("automatic",String::boolText(true));
		    m_mngmt->controlExecute(ctl);
		    return TelEngine::controlReturn(&params,true);
		}
		String src = params.getParam(YSTRING("source"));
		if (src.null()) {
		    const String* addr = params.getParam(YSTRING("address"));
		    if (addr) {
			ObjList* l = addr->split(',');
			if (l && l->at(1))
			    src = l->at(1)->toString();
			TelEngine::destruct(l);
		    }
		}
		if (src) {
		    SS7PointCode opc;
		    if (!opc.assign(src,type)) {
			if (!params.getBoolValue(YSTRING("automatic")))
			    err << "invalid source: " << src ;
			break;
		    }
		    if (!setRouteSpecificState(type,pc,opc,routeState(static_cast<SS7MsgSNM::Type>(cmd)))) {
			if (!params.getBoolValue(YSTRING("automatic")))
			    err << "no such route: " << *dest << " from: " << src;
			break;
		    }
		}
		else if (!setRouteState(type,pc,routeState(static_cast<SS7MsgSNM::Type>(cmd)))) {
		    if (!params.getBoolValue(YSTRING("automatic")))
			err << "no such route: " << *dest;
		    break;
		}
		if (m_started && (SS7MsgSNM::TRA == cmd)) {
		    // allow all routes for which TFx was not received before TRA
		    silentAllow(type,pc.pack(type));
		    // advertise routes and availability to just restarted node
		    if (!m_trafficSent.started()) {
			m_trafficSent.start();
			if (m_transfer)
			    notifyRoutes(SS7Route::KnownState,pc.pack(type));
			sendRestart(type,pc.pack(type));
		    }
		}
		return TelEngine::controlReturn(&params,true);
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
   return TelEngine::controlReturn(&params,false);
}

void SS7Router::printStats()
{
    String tmp;
    m_statsMutex.lock();
    tmp << "Rx=" << (unsigned int)m_rxMsu << ", Tx=" << (unsigned int)m_txMsu;
    tmp << ", Fwd=" << (unsigned int)m_fwdMsu << ", Fail=" << (unsigned int)m_failMsu;
    tmp << ", Cong=" << (unsigned int)m_congestions;
    m_statsMutex.unlock();
    Output("Statistics for '%s': %s",debugName(),tmp.c_str());
}

// Detach management. Call SignallingComponent::detach()
void SS7Router::destroyed()
{
    if (m_mngmt)
	detach(m_mngmt);
    SS7Layer3::destroyed();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
