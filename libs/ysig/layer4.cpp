/**
 * layer4.cpp
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

using namespace TelEngine;

SS7Layer4::SS7Layer4(unsigned char sio, const NamedList* params)
    : SignallingComponent("SS7Layer4",params),
      m_sio(sio), m_l3Mutex(true,"SS7Layer4::layer3"), m_layer3(0)
{
    if (!params)
	return;
    m_sio = getSIO(*params,sio);
}

void SS7Layer4::destroyed()
{
    attach(0);
    SignallingComponent::destroyed();
}

unsigned char SS7Layer4::getSIO(const NamedList& params, unsigned char sif, unsigned char prio, unsigned char ni)
{
    if ((prio & 0x30) == 0)
	prio <<= 4;
    if ((ni & 0xc0) == 0)
	ni <<= 6;
    sif = params.getIntValue(YSTRING("service"),sif & 0x0f);
    prio = SS7MSU::getPriority(params.getValue(YSTRING("priority")),prio & 0x30);
    if ((prio & 0x30) == 0)
	prio <<= 4;
    ni = SS7MSU::getNetIndicator(params.getValue(YSTRING("netindicator")),ni & 0xc0);
    if ((ni & 0xc0) == 0)
	ni <<= 6;
    return (sif & 0x0f) | (prio & 0x30) | (ni & 0xc0);
}

bool SS7Layer4::initialize(const NamedList* config)
{
    if (engine() && !network()) {
	NamedList params("ss7router");
	if (resolveConfig(YSTRING("router"),params,config) && params.toBoolean(true))
	    attach(YOBJECT(SS7Router,engine()->build("SS7Router",params,true,false)));
	else if (resolveConfig(YSTRING("network"),params,config) && params.toBoolean(true))
	    attach(YOBJECT(SS7Layer3,engine()->build("SS7Layer3",params,true)));
    }
    return m_layer3 != 0;
}

void SS7Layer4::attach(SS7Layer3* network)
{
    Lock lock(m_l3Mutex);
    if (m_layer3 == network)
	return;
    SS7Layer3* tmp = m_layer3;
    m_layer3 = network;
    lock.drop();
    if (tmp) {
	const char* name = 0;
	if (!engine() || engine()->find(tmp)) {
	    name = tmp->toString().safe();
	    if (tmp->getObject(YSTRING("SS7Router")))
		(static_cast<SS7Router*>(tmp))->detach(this);
	    else
		tmp->attach(0);
	}
	Debug(this,DebugAll,"Detached network/router (%p,'%s') [%p]",tmp,name,this);
    }
    if (!network)
	return;
    Debug(this,DebugAll,"Attached network/router (%p,'%s') [%p]",
	network,network->toString().safe(),this);
    insert(network);
    SS7Router* router = YOBJECT(SS7Router,network);
    if (router)
	router->attach(this);
    else
	network->attach(this);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
