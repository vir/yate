/**
 * interface.cpp
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2006 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yatess7.h"


using namespace TelEngine;

SignallingInterface::~SignallingInterface()
{
    Debug("STUB",DebugWarn,"Please implement SignallingInterface::~");
}

void SignallingInterface::attach(SignallingReceiver* receiver)
{
    if (m_receiver == receiver)
	return;
    Debug("STUB",DebugWarn,"Please implement SignallingReceiver::attach()");
}

bool SignallingInterface::control(Operation oper, NamedList* params)
{
    return false;
}

bool SignallingInterface::receivedPacket()
{
    return m_receiver && m_receiver->receivedPacket();
}


SignallingReceiver::~SignallingReceiver()
{
    Debug("STUB",DebugWarn,"Please implement SignallingReceiver::~");
}

void SignallingReceiver::attach(SignallingInterface* iface)
{
    if (m_interface == iface)
	return;
    Debug("STUB",DebugWarn,"Please implement SignallingReceiver::attach()");
}

/* vi: set ts=8 sw=4 sts=4 noet: */
