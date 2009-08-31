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


using namespace TelEngine;

typedef GenPointer<SS7Layer3> L3Pointer;
typedef GenPointer<SS7Layer4> L4Pointer;

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
	    DDebug(router,DebugAll,"Attempting transmitMSU on L3=%p '%s' [%p]",
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
      Mutex(true,"SS7Router"), m_changes(0)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7Router::SS7Router(%p) [%p]%s",
	    &params,this,tmp.c_str());
    }
#endif
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
	    attach(YSIGCREATE(SS7Management,&params));
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
	    attach(YSIGCREATE(SS7Maintenance,&params));
	}
    }
    return true;
}

bool SS7Router::operational(int sls) const
{
    // TODO: implement status check
    return true;
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

int SS7Router::transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls)
{
    XDebug(this,DebugStub,"Possibly incomplete SS7Router::transmitMSU(%p,%p,%d)",
	&msu,&label,sls);
    lock();
    RefPointer<SS7Route> route = findRoute(label.type(),label.dpc().pack(label.type()));
    unlock();
    return route ? route->transmitMSU(this,msu,label,sls) : -1;
}

bool SS7Router::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    XDebug(this,DebugStub,"Possibly incomplete SS7Router::receivedMSU(%p,%p,%p,%d)",
	&msu,&label,network,sls);
    lock();
    ObjList* l;
    do {
	for (l = &m_layer4; l; l = l->next()) {
	    L4Pointer* p = static_cast<L4Pointer*>(l->get());
	    if (!p)
		continue;
	    RefPointer<SS7Layer4> l4 = static_cast<SS7Layer4*>(*p);
	    if (!l4)
		continue;
	    DDebug(this,DebugAll,"Attempting receivedMSU to L4=%p '%s' [%p]",
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
    return false;
}

void SS7Router::notify(SS7Layer3* network, int sls)
{
    Lock lock(this);
    // iterate and notify all user parts
    ObjList* l = &m_layer4;
    for (; l; l = l->next()) {
	L4Pointer* p = static_cast<L4Pointer*>(l->get());
	if (p && *p)
	    (*p)->notify(network,sls);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
