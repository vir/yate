/**
 * engine.cpp
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
#include <yateversn.h>


using namespace TelEngine;

SignallingComponent::~SignallingComponent()
{
    detach();
}

void SignallingComponent::detach()
{
    if (m_engine) {
	m_engine->remove(this);
	m_engine = 0;
    }
}

void SignallingComponent::timerTick(const Time& when)
{
}


SignallingEngine::SignallingEngine()
    : Mutex(true)
{
    debugName("signalling");
}

SignallingEngine::~SignallingEngine()
{
    lock();
    m_components.clear();
    unlock();
}

void SignallingEngine::insert(SignallingComponent* component)
{
    if (!component)
	return;
    if (component->engine() == this)
	return;
    Lock lock(this);
    component->detach();
    component->m_engine = this;
    m_components.append(component);
}

void SignallingEngine::remove(SignallingComponent* component)
{
    if (!component)
	return;
    if (component->engine() != this)
	return;
    Lock lock(this);
    component->m_engine = 0;
    component->detach();
    m_components.remove(component,false);
}

void SignallingEngine::timerTick(const Time& when)
{
}

/* vi: set ts=8 sw=4 sts=4 noet: */
