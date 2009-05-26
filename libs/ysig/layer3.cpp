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

#include <stdlib.h>


using namespace TelEngine;

typedef GenPointer<SS7Layer2> L2Pointer;

void SS7L3User::notify(SS7Layer3* network, int sls)
{
    Debug(this,DebugStub,"Please implement SS7L3User::notify(%p,%d) [%p]",network,sls,this);
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

// Build the list of destination point codes and set the routing priority
bool SS7Layer3::buildRoutes(const NamedList& params)
{
    Lock lock(m_routeMutex);
    for (unsigned int i = 0; i < YSS7_PCTYPE_COUNT; i++)
	m_route[i].clear();
    unsigned int n = params.length();
    const char* param = "route";
    bool added = false;
    for (unsigned int i= 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!(ns && ns->name() == param))
	    continue;
	// Get & check the route
	ObjList* route = ns->split(',',true);
	ObjList* obj = route->skipNull();
	SS7PointCode* pc = new SS7PointCode(0,0,0);
	SS7PointCode::Type type = SS7PointCode::Other;
	unsigned int prio = 0;
	while (true) {
	    if (!obj)
		break;
	    type = SS7PointCode::lookup(obj->get()->toString());
	    obj = obj->skipNext();
	    if (!(obj && pc->assign(obj->get()->toString())))
		break;
	    obj = obj->skipNext();
	    if (obj)
		prio = obj->get()->toString().toInteger(0);
	    break;
	}
	TelEngine::destruct(route);
	unsigned int packed = pc->pack(type);
	TelEngine::destruct(pc);
	if ((unsigned int)type > YSS7_PCTYPE_COUNT || !packed) {
	    Debug(this,DebugNote,"Invalid %s='%s' (invalid point code%s) [%p]",
		param,ns->safe(),type == SS7PointCode::Other ? " type" : "",this);
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
    if (type == SS7PointCode::Other || (unsigned int)type > YSS7_PCTYPE_COUNT)
	return (unsigned int)-1;
    Lock lock(m_routeMutex);
    SS7Route* route = findRoute(type,packedPC);
    if (route)
	return route->m_priority;
    return (unsigned int)-1;
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
    unsigned char len = s[1] >> 4;
    // get a pointer to the test pattern
    const unsigned char* t = msu.getData(label.length()+3,len);
    if (!t) {
	Debug(this,DebugMild,"Received MTN type %02X length %u with invalid pattern length %u [%p]",
	    s[0],msu.length(),len,this);
	return false;
    }
    switch (s[0]) {
	case SS7MsgMTN::SLTM:
	    {
		Debug(this,DebugAll,"Received SLTM with test pattern length %u",len);
		SS7Label lbl(label,sls,0);
		SS7MSU answer(msu.getSIO(),lbl,0,len+2);
		unsigned char* d = answer.getData(lbl.length()+1,len+2);
		if (!d)
		    return false;
		*d++ = 0x21;
		*d++ = len << 4;
		while (len--)
		    *d++ = *t++;
		return transmitMSU(answer,lbl,sls) >= 0;
	    }
	    return true;
	case SS7MsgMTN::SLTA:
	    Debug(this,DebugAll,"Received SLTA with test pattern length %u",len);
	    return true;
    }
    Debug(this,DebugMild,"Received MTN type %02X, length %u [%p]",
	s[0],msu.length(),this);
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
    SS7Label lbl(label,sls,0);
    SS7MSU answer(SS7MSU::SNM,msu.getSSF(),lbl,0,llen+2);
    unsigned char* d = answer.getData(lbl.length()+1,llen+2);
    if (!d)
	return false;
    d[0] = SS7MsgSNM::UPU;
    label.dpc().store(label.type(),d+1);
    d[llen-1] = msu.getSIF() | ((cause & 0x0f) << 4);
    return transmitMSU(answer,lbl,sls) >= 0;
}

// Find a route having the specified point code type and packed point code
SS7Route* SS7Layer3::findRoute(SS7PointCode::Type type, unsigned int packed)
{
    if ((unsigned int)type == 0)
	return 0;
    unsigned int index = (unsigned int)type - 1;
    if (index >= YSS7_PCTYPE_COUNT)
	return 0;
    Lock lock(m_routeMutex);
    for (ObjList* o = m_route[index].skipNull(); o; o = o->skipNext()) {
	SS7Route* route = static_cast<SS7Route*>(o->get());
	XDebug(this,DebugAll,"findRoute type=%s packed=%u. Check %u [%p]",
	    SS7PointCode::lookup(type),packed,route->m_packed,this);
	if (route->m_packed == packed)
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
	    SS7Route* dest = findRoute(type,src->m_packed);
	    if (!dest) {
		dest = new SS7Route(src->m_packed);
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
void SS7Layer3::removeRoutes(SS7Layer3* network)
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
		DDebug(this,DebugAll,"Removing empty route type=%s packed=%u [%p]",
		    SS7PointCode::lookup((SS7PointCode::Type)(i+1)),route->m_packed,this);
		m_route[i].remove(route,true);
	    }
	}
    }
    DDebug(this,DebugAll,"Removed network (%p,'%s') from routing table [%p]",
	network,network->toString().safe(),this);
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
	for (; o; o = o->skipNext()) {
	    SS7Route* route = static_cast<SS7Route*>(o->get());
	    SS7PointCode pc(type,route->m_packed);
	    tmp << sType << pc;
	    if (!router) {
		tmp << " " << route->m_priority << "\r\n";
		continue;
	    }
	    for (ObjList* oo = route->m_networks.skipNull(); oo; oo = oo->skipNext()) {
		GenPointer<SS7Layer3>* d = static_cast<GenPointer<SS7Layer3>*>(oo->get());
		if (*d)
		    tmp << " '" << (*d)->toString() << "'=" << (*d)->getRoutePriority(type,route->m_packed);
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
      m_total(0), m_active(0)
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
	if ((*p)->operational())
	    active++;
    }
    m_total = total;
    m_active = active;
    return active;
}

bool SS7MTP3::operational(int sls) const
{
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
    // Attach in the first free SLS
    int sls = 0;
    ObjList* before = m_links.skipNull();
    for (; before; before = before->skipNext()) {
	L2Pointer* p = static_cast<L2Pointer*>(before->get());
	if (!*p)
	    continue;
	if (sls < (*p)->sls())
	    break;
	sls++;
    }
    link->sls(sls);
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
	m_links.remove(p,false);
	Debug(this,DebugAll,"Detached link (%p,'%s') with SLS=%d [%p]",
	    link,link->toString().safe(),link->sls(),this);
	link->attach(0);
	countLinks();
	return;
    }
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
	unsigned int n = config->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* param = config->getParam(i);
	    if (!(param && param->name() == "link"))
		continue;
	    NamedPointer* ptr = YOBJECT(NamedPointer,param);
	    NamedList* linkConfig = ptr ? YOBJECT(NamedList,ptr->userData()) : 0;
	    NamedList params(param->c_str());
	    params.addParam("basename",*param);
	    if (linkConfig)
		params.copyParams(*linkConfig);
	    else
		linkConfig = &params;
	    SS7Layer2* link = YSIGCREATE(SS7Layer2,&params);
	    if (!link)
		continue;
	    attach(link);
	    if (!link->initialize(linkConfig)) {
		detach(link);
		TelEngine::destruct(link);
	    }
	}
    }
    if (engine() && !user()) {
	NamedList params("ss7router");
	if (config)
	    static_cast<String&>(params) = config->getValue("router",params);
	if (params.toBoolean(true))
	    SS7Layer3::attach(YOBJECT(SS7Router,engine()->build("SS7Router",params,true)));
    }
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

    // Try to find a link with the given SLS
    ObjList* l = (sls >= 0) ? &m_links : 0;
    for (; l; l = l->next()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!(p && *p))
	    continue;
	SS7Layer2* link = *p;
	if (link->sls() == sls) {
	    DDebug(this,DebugAll,"Found link %p for SLS=%d [%p]",link,sls,this);
	    if (link->operational()) {
		if (link->transmitMSU(msu)) {
		    dump(msu,true,sls);
		    return sls;
		}
		return -1;
	    }
	    // found link but is down - reroute
	    Debug(this,DebugMild,"Rerouting MSU for SLS=%d, link is down",sls);
	    break;
	}
    }

    // Link not found or not operational: choose another one
    for (l = m_links.skipNull(); l; l = l->skipNext()) {
	L2Pointer* p = static_cast<L2Pointer*>(l->get());
	if (!*p)
	    continue;
	SS7Layer2* link = *p;
	if (link->operational() && link->transmitMSU(msu)) {
	    dump(msu,true,link->sls());
	    return link->sls();
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
	Debug(this,DebugAll,"Received MSU. Address: %s",tmp.c_str());
    }
#endif
    // first try to call the user part
    if (SS7Layer3::receivedMSU(msu,label,sls))
	return true;
    // then try to minimally process MTN and SNM MSUs
    if (maintenance(msu,label,sls) || management(msu,label,sls))
	return true;
    // if nothing worked, report the unavailable user part
    return unavailable(msu,label,sls);
}

void SS7MTP3::notify(SS7Layer2* link)
{
    Lock lock(this);
    bool ok = operational();
    countLinks();
#ifdef DEBUG
    String tmp;
    if (link)
	tmp << "Link '" << link->toString() << "' is " << (link->operational()?"":"not ") << "operational. ";
    Debug(this,DebugInfo,"%sLinkset has %u/%u active links [%p]",tmp.null()?"":tmp.c_str(),m_active,m_total,this);
#endif
    // if operational status changed notify upper layer
    if (ok != operational())
	SS7Layer3::notify(link ? link->sls() : -1);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
