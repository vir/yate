/**
 * interface.cpp
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

SignallingInterface::~SignallingInterface()
{
    Debug(engine(),DebugStub,"Please implement SignallingInterface::~");
}

void SignallingInterface::attach(SignallingReceiver* receiver)
{
    if (m_receiver == receiver)
	return;
    Debug(engine(),DebugStub,"Please implement SignallingInterface::attach()");
    m_receiver = receiver;
    if (!receiver)
	return;
    insert(receiver);
    receiver->attach(this);
}

bool SignallingInterface::control(Operation oper, NamedList* params)
{
    DDebug(engine(),DebugInfo,"Unhandled SignallingInterface::control(%d,%p) [%p]",
	oper,params,this);
    return false;
}

bool SignallingInterface::receivedPacket(const DataBlock& packet)
{
    return m_receiver && m_receiver->receivedPacket(packet);
}

bool SignallingInterface::notify(Notification event)
{
    return m_receiver && m_receiver->notify(event);
}


SignallingReceiver::~SignallingReceiver()
{
    Debug(engine(),DebugStub,"Please implement SignallingReceiver::~");
}

void SignallingReceiver::attach(SignallingInterface* iface)
{
    if (m_interface == iface)
	return;
    Debug(engine(),DebugStub,"Please implement SignallingReceiver::attach()");
    m_interface = iface;
    if (!iface)
	return;
    insert(iface);
    iface->attach(this);
}

bool SignallingReceiver::notify(SignallingInterface::Notification event)
{
    DDebug(DebugInfo,"Unhandled SignallingReceiver::notify(%d) [%p]",event,this);
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
