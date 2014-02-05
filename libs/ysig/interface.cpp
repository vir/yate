/**
 * interface.cpp
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


using namespace TelEngine;

// SignallingInterface notification texts used to print debug
const TokenDict SignallingInterface::s_notifName[] = {
	{"LinkUp",     LinkUp},
	{"LinkDown",   LinkDown},
	{"HWError",    HardwareError},
	{"TxClock",    TxClockError},
	{"RxClock",    RxClockError},
	{"Align",      AlignError},
	{"CRC",        CksumError},
	{"TxOversize", TxOversize},
	{"RxOversize", RxOversize},
	{"TxOverflow", TxOverflow},
	{"RxOverflow", RxOverflow},
	{"TxUnder",    TxUnderrun},
	{"RxUnder",    RxUnderrun},
	{0,0}
	};

SignallingInterface::~SignallingInterface()
{
    if (m_receiver)
	Debug(this,DebugGoOn,"Destroyed with receiver (%p) attached",m_receiver);
}

void SignallingInterface::attach(SignallingReceiver* receiver)
{
    Lock lock(m_recvMutex);
    if (m_receiver == receiver)
	return;
    SignallingReceiver* tmp = m_receiver;
    m_receiver = receiver;
    lock.drop();
    if (tmp) {
	const char* name = 0;
	if (engine() && engine()->find(tmp)) {
	    name = tmp->toString().safe();
	    tmp->attach(0);
	}
	Debug(this,DebugAll,"Detached receiver (%p,'%s') [%p]",tmp,name,this);
    }
    if (!receiver)
	return;
    Debug(this,DebugAll,"Attached receiver (%p,'%s') [%p]",
	receiver,receiver->toString().safe(),this);
    insert(receiver);
    receiver->attach(this);
}

bool SignallingInterface::control(Operation oper, NamedList* params)
{
    DDebug(this,DebugInfo,"Unhandled SignallingInterface::control(%d,%p) [%p]",
	oper,params,this);
    return TelEngine::controlReturn(params,false);
}

bool SignallingInterface::receivedPacket(const DataBlock& packet)
{
    m_recvMutex.lock();
    RefPointer<SignallingReceiver> tmp = m_receiver;
    m_recvMutex.unlock();
    return tmp && tmp->receivedPacket(packet);
}

bool SignallingInterface::notify(Notification event)
{
    m_recvMutex.lock();
    RefPointer<SignallingReceiver> tmp = m_receiver;
    m_recvMutex.unlock();
    return tmp && tmp->notify(event);
}


SignallingReceiver::SignallingReceiver(const char* name)
    : SignallingComponent(name),
      m_ifaceMutex(true,"SignallingReceiver::interface"), m_interface(0)
{
}

SignallingReceiver::~SignallingReceiver()
{
    if (m_interface)
	Debug(this,DebugGoOn,"Destroyed with interface (%p) attached",m_interface);
    TelEngine::destruct(attach(0));
}

SignallingInterface* SignallingReceiver::attach(SignallingInterface* iface)
{
    Lock lock(m_ifaceMutex);
    if (m_interface == iface)
	return 0;
    SignallingInterface* tmp = m_interface;
    m_interface = iface;
    lock.drop();
    if (tmp) {
	if (tmp->receiver() == this) {
	    Debug(this,DebugAll,"Detaching interface (%p,'%s') [%p]",
		tmp,tmp->toString().safe(),this);
	    tmp->attach(0);
	}
	else {
	    Debug(this,DebugNote,"Interface (%p,'%s') was not attached to us [%p]",
		tmp,tmp->toString().safe(),this);
	    tmp = 0;
	}
    }
    if (!iface)
	return tmp;
    Debug(this,DebugAll,"Attached interface (%p,'%s') [%p]",
	iface,iface->toString().safe(),this);
    insert(iface);
    iface->attach(this);
    return tmp;
}

bool SignallingReceiver::notify(SignallingInterface::Notification event)
{
    DDebug(this,DebugInfo,"Unhandled SignallingReceiver::notify(%d) [%p]",event,this);
    return false;
}

bool SignallingReceiver::control(SignallingInterface::Operation oper, NamedList* params)
{
    m_ifaceMutex.lock();
    RefPointer<SignallingInterface> tmp = m_interface;
    m_ifaceMutex.unlock();
    return TelEngine::controlReturn(params,tmp && tmp->control(oper,params));
}

bool SignallingReceiver::transmitPacket(const DataBlock& packet, bool repeat,
    SignallingInterface::PacketType type)
{
    m_ifaceMutex.lock();
    RefPointer<SignallingInterface> tmp = m_interface;
    m_ifaceMutex.unlock();
    return tmp && tmp->transmitPacket(packet,repeat,type);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
