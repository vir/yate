/**
 * router.cpp
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
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

#include "yatess7.h"


using namespace TelEngine;

void SS7Router::attach(SS7Layer3* network)
{
    if (!network)
	return;
    Debug(toString(),DebugStub,"Please implement SS7Router::attach()");
    SignallingComponent::insert(network);
    if (!m_layer3.find(network))
	m_layer3.append(new GenPointer<SS7Layer3>(network));
    network->attach(this);
}

void SS7Router::attach(SS7Layer4* service)
{
    if (!service)
	return;
    Debug(toString(),DebugStub,"Please implement SS7Router::attach()");
    SignallingComponent::insert(service);
    if (!m_layer4.find(service))
	m_layer4.append(new GenPointer<SS7Layer4>(service));
    service->attach(this);
}

int SS7Router::transmitMSU(const SS7MSU& msu, int sls)
{
    Debug(toString(),DebugStub,"Please implement SS7Router::transmitMSU(%p,%d)",&msu,sls);
    return -1;
}

bool SS7Router::receivedMSU(const SS7MSU& msu, SS7Layer3* network, int sls)
{
    Debug(toString(),DebugStub,"Please implement SS7Router::receivedMSU(%p,%p,%d)",&msu,network,sls);
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
