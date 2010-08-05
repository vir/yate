/**
 * layer4.cpp
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

SS7Layer4::SS7Layer4(unsigned char sio, const NamedList* params)
    : SignallingComponent("SS7Layer4"),
      m_sio(sio), m_l3Mutex(true,"SS7Layer4::layer3"), m_layer3(0)
{
    if (!params)
	return;
    m_sio = (params->getIntValue("service",sio & 0x0f) & 0x0f)
	| (SS7MSU::getPriority(params->getValue("priority"),sio & 0x30) & 0x30)
	| (SS7MSU::getNetIndicator(params->getValue("netindicator"),sio & 0xc0) & 0xc0);
}

bool SS7Layer4::initialize(const NamedList* config)
{
    if (engine() && !network()) {
	NamedList params("ss7router");
	if (config) {
	    String name = config->getValue("router",params);
	    if (name && !name.toBoolean(false))
		static_cast<String&>(params) = name;
	}
	if (params.toBoolean(true))
	    attach(YOBJECT(SS7Router,engine()->build("SS7Router",params,true)));
	else if (config) {
	    String name = config->getValue("network");
	    if (name && name.toBoolean(true)) {
		static_cast<String&>(params) = name;
		attach(YOBJECT(SS7Layer3,engine()->build("SS7Layer3",params,true)));
	    }
	}
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
	    if (tmp->getObject("SS7Router"))
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
