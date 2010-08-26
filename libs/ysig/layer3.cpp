/**
 * layer3.cpp
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
#include <stdlib.h>


using namespace TelEngine;

static const TokenDict s_dict_control[] = {
    { "pause",   SS7MTP3::Pause },
    { "resume",  SS7MTP3::Resume },
    { "restart", SS7MTP3::Restart },
    { 0, 0 }
};

typedef GenPointer<SS7Layer2> L2Pointer;

void SS7L3User::notify(SS7Layer3* network, int sls)
{
    Debug(this,DebugStub,"Please implement SS7L3User::notify(%p,%d) [%p]",network,sls,this);
}

ObjList* SS7L3User::getNetRoutes(SS7Layer3* network, SS7PointCode::Type type)
{
    return network ? network->getRoutes(type) : (ObjList*)0;
}

const ObjList* SS7L3User::getNetRoutes(const SS7Layer3* network, SS7PointCode::Type type)
{
    return network ? network->getRoutes(type) : (const ObjList*)0;
}


// Constructor
SS7Layer3::SS7Layer3(SS7PointCode::Type type)
    : SignallingComponent("SS7Layer3"),
      m_l3userMutex(true,"SS7Layer3::l3user"),
      m_l3user(0),
      m_routeMutex(true,"SS7Layer3::route")
{
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++)
	m_local[i] = 0;
    setType(type);
}

// Initialize the Layer 3 component
bool SS7Layer3::initialize(const NamedList* config)
{
    if (engine() && !user()) {
	NamedList params("ss7router");
	if (config)
	    static_cast<String&>(params) = config->getValue("router",params);
	if (params.toBoolean(true))
	    SS7Layer3::attach(YOBJECT(SS7Router,engine()->build("SS7Router",params,true)));
    }
    return true;
}

// Attach a Layer 3 user component to this network
void SS7Layer3::attach(SS7L3User* l3user)
{
    Lock lock(m_l3userMutex);
    if (m_l3user == l3user)
	return;
    SS7L3User* tmp = m_l3user;
    m_l3user = l3user;
    lock.drop();
    if (tmp) {
	const char* name = 0;
	if (engine() && engine()->find(tmp)) {
	    name = tmp->toString().safe();
	    if (tmp->getObject("SS7Router"))
		(static_cast<SS7Router*>(tmp))->detach(this);
	    else
		tmp->attach(0);
	}
	Debug(this,DebugAll,"Detached L3 user (%p,'%s') [%p]",tmp,name,this);
    }
    if (!l3user)
	return;
    Debug(this,DebugAll,"Attached L3 user (%p,'%s') [%p]",l3user,l3user->toString().safe(),this);
    insert(l3user);
    if (l3user->getObject("SS7Router"))
	(static_cast<SS7Router*>(l3user))->attach(this);
    else
	l3user->attach(this);
}

SS7PointCode::Type SS7Layer3::type(unsigned char netType) const
{
    if (netType & 0xc0)
	netType >>= 6;
    return m_cpType[netType & 0x03];
}

void SS7Layer3::setType(SS7PointCode::Type type, unsigned char netType)
{
    if (netType & 0xc0)
	netType >>= 6;
    m_cpType[netType & 0x03] = type;
}

void SS7Layer3::setType(SS7PointCode::Type type)
{
    m_cpType[3] = m_cpType[2] = m_cpType[1] = m_cpType[0] = type;
}

unsigned char SS7Layer3::getNI(SS7PointCode::Type pcType, unsigned char defNI) const
{
    if ((defNI & 0xc0) == 0)
	defNI <<= 6;
    if (SS7PointCode::Other == pcType || type(defNI) == pcType)
	return defNI;
    if (pcType == m_cpType[2])
	return SS7MSU::National;
    if (pcType == m_cpType[3])
	return SS7MSU::ReservedNational;
    if (pcType == m_cpType[0])
	return SS7MSU::International;
    if (pcType == m_cpType[1])
	return SS7MSU::SpareInternational;
    return defNI;
}

bool SS7Layer3::hasType(SS7PointCode::Type pcType) const
{
    if (SS7PointCode::Other == pcType)
	return false;
    for (int i = 0; i < 4; i++)
	if (pcType == m_cpType[i])
	    return true;
    return false;
}

// Build the list of destination point codes and set the routing priority
bool SS7Layer3::buildRoutes(const NamedList& params)
{
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	m_route[i].clear();
	m_local[i] = 0;
    }
    unsigned int n = params.length();
    bool added = false;
    for (unsigned int i= 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!ns)
	    continue;
	unsigned int prio = 0;
	bool local = false;
	if (ns->name() == "local")
	    local = true;
	else if (ns->name() == "route")
	    prio = 100;
	else if (ns->name() != "adjacent")
	    continue;
	// Get & check the route
	ObjList* route = ns->split(',',true);
	ObjList* obj = route->skipNull();
	SS7PointCode pc;
	SS7PointCode::Type type = SS7PointCode::Other;
	while (true) {
	    if (!obj)
		break;
	    type = SS7PointCode::lookup(obj->get()->toString());
	    obj = obj->skipNext();
	    if (!(obj && pc.assign(obj->get()->toString(),type)))
		break;
	    obj = obj->skipNext();
	    if (obj && prio)
		prio = obj->get()->toString().toInteger(prio);
	    break;
	}
	TelEngine::destruct(route);
	unsigned int packed = pc.pack(type);
	if ((unsigned int)type > YSS7_PCTYPE_COUNT || !packed) {
	    Debug(this,DebugNote,"Invalid %s='%s' (invalid point code%s) [%p]",
		ns->name().c_str(),ns->safe(),type == SS7PointCode::Other ? " type" : "",this);
	    continue;
	}
	if (local) {
	    m_local[type - 1] = packed;
	    continue;
	}
	if (findRoute(type,packed))
	    continue;
	added = true;
	m_route[(unsigned int)type - 1].append(new SS7Route(packed,prio));
	DDebug(this,DebugAll,"Added route '%s'",ns->c_str());
    }
    if (!added)
	Debug(this,DebugMild,"No outgoing routes [%p]",this);
    else
	printRoutes();
    return added;
}

// Get the priority of a route.
unsigned int SS7Layer3::getRoutePriority(SS7PointCode::Type type, unsigned int packedPC)
{
    if (type == SS7PointCode::Other || (unsigned int)type > YSS7_PCTYPE_COUNT || !packedPC)
	return (unsigned int)-1;
    Lock lock(m_routeMutex);
    SS7Route* route = findRoute(type,packedPC);
    if (route)
	return route->m_priority;
    return (unsigned int)-1;
}

// Get the state of a route.
SS7Route::State SS7Layer3::getRouteState(SS7PointCode::Type type, unsigned int packedPC)
{
    if (type == SS7PointCode::Other || (unsigned int)type > YSS7_PCTYPE_COUNT || !packedPC)
	return SS7Route::Unknown;
    Lock lock(m_routeMutex);
    SS7Route* route = findRoute(type,packedPC);
    if (route)
	return route->state();
    return SS7Route::Unknown;
}

// Set the state of a route.
bool SS7Layer3::setRouteState(SS7PointCode::Type type, unsigned int packedPC, SS7Route::State state, GenObject* context)
{
    if (type == SS7PointCode::Other || (unsigned int)type > YSS7_PCTYPE_COUNT || !packedPC)
	return false;
    Lock lock(m_routeMutex);
    SS7Route* route = findRoute(type,packedPC);
    if (!route)
	return false;
    if (state != route->m_state) {
	route->m_state = state;
	if (state != SS7Route::Unknown)
	    routeChanged(route,type,context);
    }
    return true;
}

bool SS7Layer3::maintenance(const SS7MSU& msu, const SS7Label& label, int sls)
{
    if (msu.getSIF() != SS7MSU::MTN)
	return false;
    XDebug(this,DebugStub,"Possibly incomplete SS7Layer3::maintenance(%p,%p,%d) [%p]",
	&msu,&label,sls,this);
    // Q.707 says test pattern length should be 1-15 but we accept 0 as well
    const unsigned char* s = msu.getData(label.length()+1,2);
    if (!s)
	return false;
    String addr;
    addr << SS7PointCode::lookup(label.type()) << "," << label;
    if (debugAt(DebugAll))
	addr << " (" << label.opc().pack(label.type()) << ":" << label.dpc().pack(label.type()) << ":" << label.sls() << ")";
    int level = DebugInfo;
    if (label.sls() != sls) {
	addr << " on " << sls;
	level = DebugMild;
    }
    unsigned char len = s[1] >> 4;
    // get a pointer to the test pattern
    const unsigned char* t = msu.getData(label.length()+3,len);
    if (!t) {
	Debug(this,DebugMild,"Received MTN %s type %02X length %u with invalid pattern length %u [%p]",
	    addr.c_str(),s[0],msu.length(),len,this);
	return false;
    }
    switch (s[0]) {
	case SS7MsgMTN::SLTM:
	    {
		Debug(this,level,"Received SLTM %s with %u bytes",addr.c_str(),len);
		SS7Label lbl(label,label.sls(),0);
		SS7MSU answer(msu.getSIO(),lbl,0,len+2);
		unsigned char* d = answer.getData(lbl.length()+1,len+2);
		if (!d)
		    return false;
		Debug(this,DebugInfo,"Sending SLTA %s with %u bytes",addr.c_str(),len);
		*d++ = SS7MsgMTN::SLTA;
		*d++ = len << 4;
		while (len--)
		    *d++ = *t++;
		return transmitMSU(answer,lbl,sls) >= 0;
	    }
	    return true;
	case SS7MsgMTN::SLTA:
	    Debug(this,level,"Received SLTM %s with %u bytes",addr.c_str(),len);
	    return true;
    }
    Debug(this,DebugMild,"Received MTN %s type %02X, length %u [%p]",
	addr.c_str(),s[0],msu.length(),this);
    return false;
}

bool SS7Layer3::management(const SS7MSU& msu, const SS7Label& label, int sls)
{
    if (msu.getSIF() != SS7MSU::SNM)
	return false;
    Debug(this,DebugStub,"Please implement SS7Layer3::management(%p,%p,%d) [%p]",
	&msu,&label,sls,this);
    // according to Q.704 there should be at least the heading codes (8 bit)
    const unsigned char* s = msu.getData(label.length()+1,1);
    if (!s)
	return false;
    // TODO: implement
    return false;
}

bool SS7Layer3::unavailable(const SS7MSU& msu, const SS7Label& label, int sls, unsigned char cause)
{
    DDebug(this,DebugInfo,"SS7Layer3::unavailable(%p,%p,%d,%d) [%p]",
	&msu,&label,sls,cause,this);
#ifdef DEBUG
    String s;
    s.hexify(msu.data(),msu.length(),' ');
    Debug(this,DebugMild,"Unhandled MSU len=%u Serv: %s, Prio: %s, Net: %s, Data: %s",
	msu.length(),msu.getServiceName(),msu.getPriorityName(),
	msu.getIndicatorName(),s.c_str());
#endif
    if (msu.getSIF() == SS7MSU::SNM)
	return false;
    // send a SNM UPU (User Part Unavailable, Q.704 15.17.2)
    unsigned char llen = SS7PointCode::length(label.type());
    SS7Label lbl(label,label.sls(),0);
    SS7MSU answer(SS7MSU::SNM,msu.getSSF(),lbl,0,llen+2);
    unsigned char* d = answer.getData(lbl.length()+1,llen+2);
    if (!d)
	return false;
    d[0] = SS7MsgSNM::UPU;
    label.dpc().store(label.type(),d+1);
    d[llen+1] = msu.getSIF() | ((cause & 0x0f) << 4);
    return transmitMSU(answer,lbl,sls) >= 0;
}

// Find a route having the specified point code type and packed point code
SS7Route* SS7Layer3::findRoute(SS7PointCode::Type type, unsigned int packed, SS7Route::State states)
{
    if ((unsigned int)type == 0 || !packed)
	return 0;
    unsigned int index = (unsigned int)type - 1;
    if (index >= YSS7_PCTYPE_COUNT)
	return 0;
    Lock lock(m_routeMutex);
    for (ObjList* o = m_route[index].skipNull(); o; o = o->skipNext()) {
	SS7Route* route = static_cast<SS7Route*>(o->get());
	XDebug(this,DebugAll,"findRoute type=%s packed=%u. Check %u [%p]",
	    SS7PointCode::lookup(type),packed,route->m_packed,this);
	if (route->m_packed == packed && ((route->state() | states) != 0))
	    return route;
    }
    return 0;
}

// Add a network to the routing table. Clear all its routes before appending it to the table
void SS7Layer3::updateRoutes(SS7Layer3* network)
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
void SS7Layer3::removeRoutes(SS7Layer3* network, GenObject* context)
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
			route->m_state = SS7Route::Prohibited;
			routeChanged(route,type,context);
		}
		m_route[i].remove(route,true);
	    }
	}
    }
    DDebug(this,DebugAll,"Removed network (%p,'%s') from routing table [%p]",
	network,network->toString().safe(),this);
}

// Call the route changed notification for all known routes that match
void SS7Layer3::notifyRoutes(const SS7Layer3* network, SS7Route::State states, GenObject* context)
{
    if (SS7Route::Unknown == states)
	return;
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	ListIterator iter(m_route[i]);
	while (true) {
	    SS7Route* route = static_cast<SS7Route*>(iter.get());
	    if (!route)
		break;
	    if ((route->state() & states) == 0)
		continue;
	    if (network && !route->hasNetwork(network))
		continue;
	    routeChanged(route,static_cast<SS7PointCode::Type>(i+1),context);
	}
    }
}

void SS7Layer3::routeChanged(const SS7Route* route, SS7PointCode::Type type, GenObject* context)
{
}

void SS7Layer3::printRoutes()
{
    String s;
    bool router = getObject("SS7Router") != 0;
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
	ObjList* o = m_route[i].skipNull();
	if (!o)
	    continue;
	SS7PointCode::Type type = (SS7PointCode::Type)(i + 1);
	String tmp;
	String sType = SS7PointCode::lookup(type);
	sType << String(' ',(unsigned int)(8 - sType.length()));
	if (m_local[i])
	    sType << SS7PointCode(type,m_local[i]) << " > ";
	for (; o; o = o->skipNext()) {
	    SS7Route* route = static_cast<SS7Route*>(o->get());
	    tmp << sType << SS7PointCode(type,route->m_packed);
	    if (!router) {
		tmp << " " << route->m_priority << " (" << route->stateName() << ")\r\n";
		continue;
	    }
	    tmp << " (" << route->stateName() << ")";
	    for (ObjList* oo = route->m_networks.skipNull(); oo; oo = oo->skipNext()) {
		GenPointer<SS7Layer3>* d = static_cast<GenPointer<SS7Layer3>*>(oo->get());
		if (*d)
		    tmp << " " << (*d)->toString() << "," << (*d)->getRoutePriority(type,route->m_packed);
	    }
	    tmp << "\r\n"; 
	}
	s << tmp;
    }
    if (s) {
	s = s.substr(0,s.length() - 2);
	Debug(this,DebugAll,"%s: [%p]\r\n%s",router?"Routing table":"Destinations",this,s.c_str());
    }
    else 
	Debug(this,DebugAll,"No %s [%p]",router?"routes":"destinations",this);
}


SS7MTP3::SS7MTP3(const NamedList& params)
    : SignallingComponent(params.safe("SS7MTP3"),&params),
      SignallingDumpable(SignallingDumper::Mtp3),
      Mutex(true,"SS7MTP3"),
      m_total(0), m_active(0), m_inhibit(false), m_checklinks(true), m_check(0)
{
#ifdef DEBUG
    if (debugAt(DebugAll)) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7MTP3::SS7MTP3(%p) [%p]%s",
	    &params,this,tmp.c_str());
    }
#endif
    // Set point code type for each network indicator
    static const unsigned char ni[4] = { SS7MSU::International,
	SS7MSU::SpareInternational, SS7MSU::National, SS7MSU::ReservedNational };
    String stype = params.getValue("netind2pctype");
    int level = DebugAll;
    if (stype.find(',') >= 0) {
	ObjList* obj = stype.split(',',false);
	ObjList* o = obj->skipNull();
	for (unsigned int i = 0; i < 4; i++) {
	    String* s = 0;
	    if (o) {
		s = static_cast<String*>(o->get());
		o = o->skipNext();
	    }
	    SS7PointCode::Type type = SS7PointCode::lookup(s?s->c_str():0);
	    if (type == SS7PointCode::Other)
		level = DebugNote;
	    setType(type,ni[i]);
	}
	TelEngine::destruct(obj);
    }
    else {
	SS7PointCode::Type type = SS7PointCode::lookup(stype.c_str());
	if (type == SS7PointCode::Other)
	    level = DebugNote;
	for (unsigned int i = 0; i < 4; i++)
	    setType(type,ni[i]);
    }
    Debug(this,level,"Point code types are '%s' [%p]",stype.safe(),this);

    m_inhibit = !params.getBoolValue("autostart",true);
    m_checklinks = params.getBoolValue("checklinks",true);
    int check = params.getIntValue("maintenance",60000);
    if (check > 0) {
	if (check < 5000)
	    check = 5000;
	else if (check > 600000)
	    check = 600000;
	m_check = 1000 * check;
    }
    buildRoutes(params);
    setDumper(params.getValue("layer3dump"));
}

SS7MTP3::~SS7MTP3()
{
    setDumper();
}

unsigned int SS7MTP3::countLinks()
{
    unsigned int total = 0;
    unsigned int active = 0;
    ObjList* l = &m_links;
    for (; l; l = l->next()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!(p && *p))
	    continue;
	total++;
	if ((*p)->operational() && !(*p)->m_unchecked)
	    active++;
    }
    m_total = total;
    m_active = active;
    return active;
}

bool SS7MTP3::operational(int sls) const
{
    if (m_inhibit)
	return false;
    if (sls < 0)
	return (m_active != 0);
    const ObjList* l = &m_links;
    for (; l; l = l->next()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!(p && *p))
	    continue;
	if ((*p)->sls() == sls)
	    return (*p)->operational();
    }
    return false;
}

bool SS7MTP3::inhibited(int sls) const
{
    if (sls < 0)
	return m_inhibit;
    const ObjList* l = &m_links;
    for (; l; l = l->next()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!(p && *p))
	    continue;
	if ((*p)->sls() == sls)
	    return (*p)->inhibited();
    }
    return true;
}

int SS7MTP3::getSequence(int sls) const
{
    if (sls < 0)
	return -1;
    const ObjList* l = &m_links;
    for (; l; l = l->next()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!(p && *p))
	    continue;
	if ((*p)->sls() == sls)
	    return (*p)->getSequence();
    }
    return false;
}

// Attach a link in the first free SLS
void SS7MTP3::attach(SS7Layer2* link)
{
    if (!link)
	return;
    SignallingComponent::insert(link);
    Lock lock(this);
    // Check if already attached
    for (ObjList* o = m_links.skipNull(); o; o = o->skipNext()) {
	L2Pointer* p = static_cast<L2Pointer*>(o->get());
	if (*p == link) {
	    link->attach(this);
	    return;
	}
    }
    ObjList* before = 0;
    int sls = link->sls();
    if (sls >= 0) {
	before = m_links.skipNull();
	for (; before; before = before->skipNext()) {
	    L2Pointer* p = static_cast<L2Pointer*>(before->get());
	    if (!*p)
		continue;
	    if (sls < (*p)->sls())
		break;
	    if (sls == (*p)->sls()) {
		sls = -1;
		break;
	    }
	}
    }
    if (sls < 0) {
	// Attach in the first free SLS
	sls = 0;
	before = m_links.skipNull();
	for (; before; before = before->skipNext()) {
	    L2Pointer* p = static_cast<L2Pointer*>(before->get());
	    if (!*p)
		continue;
	    if (sls < (*p)->sls())
		break;
	    sls++;
	}
	link->sls(sls);
    }
    link->ref();
    if (!before)
	m_links.append(new L2Pointer(link));
    else
	before->insert(new L2Pointer(link));
    Debug(this,DebugAll,"Attached link (%p,'%s') with SLS=%d [%p]",
	link,link->toString().safe(),link->sls(),this);
    countLinks();
    link->attach(this);
}

// Detach a link. Remove its L2 user
void SS7MTP3::detach(SS7Layer2* link)
{
    if (!link)
	return;
    Lock lock(this);
    for (ObjList* o = m_links.skipNull(); o; o = o->skipNext()) {
	L2Pointer* p = static_cast<L2Pointer*>(o->get());
	if (*p != link)
	    continue;
	m_links.remove(p);
	Debug(this,DebugAll,"Detached link (%p,'%s') with SLS=%d [%p]",
	    link,link->toString().safe(),link->sls(),this);
	link->attach(0);
	TelEngine::destruct(link);
	countLinks();
	return;
    }
}

bool SS7MTP3::control(Operation oper, NamedList* params)
{
    bool ok = operational();
    switch (oper) {
	case Pause:
	    if (!m_inhibit) {
		m_inhibit = true;
		if (ok)
		    SS7Layer3::notify(-1);
	    }
	    return true;
	case Restart:
	    if (ok) {
		ok = false;
		m_inhibit = true;
		SS7Layer3::notify(-1);
	    }
	    // fall through
	case Resume:
	    if (m_inhibit) {
		m_inhibit = false;
		if (ok != operational())
		    SS7Layer3::notify(-1);
	    }
	    if (params && params->getBoolValue("emergency")) {
		unsigned int cnt = 0;
		const ObjList* l = &m_links;
		for (; l; l = l->next()) {
		    L2Pointer* p = static_cast<L2Pointer*>(l->get());
		    if (!(p && *p))
			continue;
		    cnt++;
		    (*p)->control(SS7Layer2::Resume,params);
		}
		Debug(this,DebugNote,"Emergency resume attempt on %u links [%p]",cnt,this);
	    }
	    return true;
	case Status:
	    return ok;
    }
    return false;
}

bool SS7MTP3::control(NamedList& params)
{
    String* ret = params.getParam("completion");
    const String* oper = params.getParam("operation");
    const char* cmp = params.getValue("component");
    int cmd = oper ? oper->toInteger(s_dict_control,-1) : -1;
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
    if (cmd >= 0)
	return control((Operation)cmd,&params);
    return SignallingDumpable::control(params,this);
}

// Configure and initialize MTP3 and its links
bool SS7MTP3::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SS7MTP3::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config)
	debugLevel(config->getIntValue("debuglevel_mtp3",
	    config->getIntValue("debuglevel",-1)));
    countLinks();
    if (config && (0 == m_total)) {
	m_checklinks = config->getBoolValue("checklinks",m_checklinks);
	unsigned int n = config->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* param = config->getParam(i);
	    if (!(param && param->name() == "link"))
		continue;
	    NamedPointer* ptr = YOBJECT(NamedPointer,param);
	    NamedList* linkConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
	    String linkName(*param);
	    int linkSls = -1;
	    int sep = linkName.find(',');
	    if (sep >= 0) {
		linkSls = linkName.substr(sep + 1).toInteger(-1);
		linkName = linkName.substr(0,sep);
	    }
	    NamedList params(linkName);
	    params.addParam("basename",linkName);
	    if (linkConfig)
		params.copyParams(*linkConfig);
	    else {
		params.copySubParams(*config,params + ".");
		linkConfig = &params;
	    }
	    SS7Layer2* link = YSIGCREATE(SS7Layer2,&params);
	    if (!link)
		continue;
	    if (linkSls >= 0)
		link->sls(linkSls);
	    link->m_unchecked = m_checklinks;
	    attach(link);
	    if (!link->initialize(linkConfig))
		detach(link);
	    TelEngine::destruct(link);
	}
	m_inhibit = !config->getBoolValue("autostart",true);
    }
    SS7Layer3::initialize(config);
    return 0 != m_total;
}

// Detach all links and user. Destroys the object, disposes the memory
void SS7MTP3::destroyed()
{
    lock();
    ListIterator iter(m_links);
    for (GenObject* o = 0; 0 != (o = iter.get());) {
	L2Pointer* p = static_cast<L2Pointer*>(o);
	detach(*p);
    }
    SS7Layer3::attach(0);
    unlock();
    SS7Layer3::destroyed();
}

int SS7MTP3::transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls)
{
    Lock lock(this);
    if (!m_active) {
	Debug(this,DebugMild,"Could not transmit MSU, %s [%p]",
	    m_total ? "all links are down" : "no data links attached",this);
	return -1;
    }

    bool maint = (msu.getSIF() == SS7MSU::MTN) || (msu.getSIF() == SS7MSU::MTNS);
    // Try to find a link with the given SLS
    ObjList* l = (sls >= 0) ? &m_links : 0;
    for (; l; l = l->next()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!(p && *p))
	    continue;
	SS7Layer2* link = *p;
	if (link->sls() == sls) {
	    XDebug(this,DebugAll,"Found link %p for SLS=%d [%p]",link,sls,this);
	    if (link->operational() && (maint || !link->inhibited())) {
		if (link->transmitMSU(msu)) {
		    DDebug(this,DebugAll,"Sent MSU over link '%s' %p with SLS=%d%s [%p]",
			link->toString().c_str(),link,sls,
			(m_inhibit ? " while inhibited" : ""),this);
		    dump(msu,true,sls);
		    return sls;
		}
		return -1;
	    }
	    if (maint) {
		Debug(this,DebugNote,"Dropping maintenance MSU for SLS=%d, link is down",sls);
		return -1;
	    }
	    // found link but is down - reroute
	    Debug(this,DebugMild,"Rerouting MSU for SLS=%d, link is down",sls);
	    break;
	}
    }

    bool mgmt = (msu.getSIF() == SS7MSU::SNM);
    // Link not found or not operational: choose another one
    for (l = m_links.skipNull(); l; l = l->skipNext()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!*p)
	    continue;
	SS7Layer2* link = *p;
	if (link->operational() && (mgmt || !link->inhibited()) && link->transmitMSU(msu)) {
	    sls = link->sls();
	    DDebug(this,DebugAll,"Sent MSU over link '%s' %p with SLS=%d%s [%p]",
		link->toString().c_str(),link,sls,
		(m_inhibit ? " while inhibited" : ""),this);
	    dump(msu,true,sls);
	    return sls;
	}
    }

    Debug(this,DebugWarn,"Could not find any link to send MSU [%p]",this);
    return -1;
}

bool SS7MTP3::receivedMSU(const SS7MSU& msu, SS7Layer2* link, int sls)
{
    dump(msu,false,sls);
    int netType = msu.getNI();
    SS7PointCode::Type cpType = type(netType);
    unsigned int llen = SS7Label::length(cpType);
    if (!llen) {
	Debug(toString(),DebugWarn,"Received MSU but point code type is unconfigured [%p]",this);
	return false;
    }
    // check MSU length against SIO + label length
    if (msu.length() <= llen) {
	Debug(this,DebugMild,"Received short MSU of length %u [%p]",
	    msu.length(),this);
	return false;
    }
    SS7Label label(cpType,msu);
#ifdef DEBUG
    if (debugAt(DebugInfo)) {
	String tmp;
	tmp << label << " (" << label.opc().pack(cpType) << ":" << label.dpc().pack(cpType) << ":" << label.sls() << ")";
	Debug(this,DebugAll,"Received MSU from link '%s' %p with SLS=%d. Address: %s",
	    link->toString().c_str(),link,sls,tmp.c_str());
    }
#endif
    bool maint = (msu.getSIF() == SS7MSU::MTN) || (msu.getSIF() == SS7MSU::MTNS);
    if (link) {
	if (link->m_unchecked) {
	    if (!maint)
		return false;
	    if (label.sls() == sls) {
		Debug(this,DebugNote,"Placing link '%s' %d in service [%p]",
		    link->toString().c_str(),sls,this);
		link->m_unchecked = false;
		notify(link);
	    }
	}
	if (!maint && (msu.getSIF() != SS7MSU::SNM) && link->inhibited())
	    return false;
    }
    // first try to call the user part
    if (SS7Layer3::receivedMSU(msu,label,sls))
	return true;
    // then try to minimally process MTN and SNM MSUs
    if (maintenance(msu,label,sls) || management(msu,label,sls))
	return true;
    // if nothing worked, report the unavailable regular user part
    return (msu.getSIF() > SS7MSU::MTNS) && unavailable(msu,label,sls);
}

void SS7MTP3::notify(SS7Layer2* link)
{
    Lock lock(this);
    unsigned int act = m_active;
    countLinks();
#ifdef DEBUG
    String tmp;
    if (link)
	tmp << "Link '" << link->toString() << "' is " << (link->operational()?"":"not ") << "operational. ";
    Debug(this,DebugInfo,"%sLinkset has %u/%u active links [%p]",tmp.null()?"":tmp.c_str(),m_active,m_total,this);
#endif
    if (link) {
	if (link->operational()) {
	    if (link->m_unchecked) {
		u_int64_t t = Time::now() + 50000 + (::random() % 100000);
		if ((t < link->m_check) || (t - 4000000 > link->m_check))
		    link->m_check = t;
	    }
	}
	else
	    link->m_unchecked = m_checklinks;
    }
    // if operational status of a link changed notify upper layer
    if (act != m_active) {
	Debug(this,DebugNote,"Linkset is%s operational [%p]",
	    (operational() ? "" : " not"),this);
	// if we became inaccessible try to resume all other links
	unsigned int cnt = 0;
	for (const ObjList* l = &m_links; l && !(m_active || m_inhibit); l = l->next()) {
	    L2Pointer* p = static_cast<L2Pointer*>(l->get());
	    if (!p)
		continue;
	    SS7Layer2* l2 = *p;
	    if ((l2 == link) || !l2)
		continue;
	    cnt++;
	    l2->control(SS7Layer2::Resume);
	}
	if (cnt)
	    Debug(this,DebugNote,"Attempted to resume %u links [%p]",cnt,this);
	SS7Layer3::notify(link ? link->sls() : -1);
    }
}

void SS7MTP3::timerTick(const Time& when)
{
    Lock lock(this);
    for (ObjList* o = m_links.skipNull(); o; o = o->skipNext()) {
	L2Pointer* p = static_cast<L2Pointer*>(o->get());
	if (!p)
	    continue;
	SS7Layer2* l2 = *p;
	if (l2 && l2->m_check && (l2->m_check < when) && l2->operational()) {
	    l2->m_check = m_check ? when + m_check : 0;
	    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++) {
		SS7PointCode::Type type = (SS7PointCode::Type)(i + 1);
		unsigned int local = getLocal(type);
		if (!local)
		    continue;
		ObjList* o = getRoutes(type);
		if (!o)
		    continue;
		unsigned char sio = getNI(type) | SS7MSU::MTN;
		for (o = o->skipNull(); o; o = o->skipNext()) {
		    const SS7Route* r = static_cast<const SS7Route*>(o->get());
		    if (r->priority())
			continue;
		    // build and send a SLTM to the adjacent node
		    unsigned int len = 4;
		    int sls = l2->sls();
		    SS7Label lbl(type,r->packed(),local,sls);
		    SS7MSU sltm(sio,lbl,0,len+2);
		    unsigned char* d = sltm.getData(lbl.length()+1,len+2);
		    if (!d)
			continue;

		    String addr;
		    addr << SS7PointCode::lookup(type) << "," << lbl;
		    if (debugAt(DebugAll))
			addr << " (" << lbl.opc().pack(type) << ":" << lbl.dpc().pack(type) << ":" << sls << ")";
		    Debug(this,DebugInfo,"Sending SLTM %s with %u bytes",addr.c_str(),len);

		    *d++ = SS7MsgMTN::SLTM;
		    *d++ = len << 4;
		    unsigned char patt = sls;
		    patt = (patt << 4) | (patt & 0x0f);
		    while (len--)
			*d++ = patt++;
		    l2->transmitMSU(sltm);
		}
	    }
	}
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
