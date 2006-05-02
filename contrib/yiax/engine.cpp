/**
 * engine.cpp
 * Yet Another IAX2 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

#include <yateiax.h>
#include <yateversn.h>

using namespace TelEngine;

IAXEngine::IAXEngine(int transCount)
    : Mutex(true), m_transList(0), m_transListCount(0)
{
    if (transCount < 4)
	transCount = 4;
    else if (transCount > 256)
	transCount = 256;
    m_transList = new ObjList* [transCount];
    for (int i = 0; i < transCount; i++)
	m_transList[i] = new ObjList;
    m_transListCount = transCount;
}

IAXEngine::~IAXEngine()
{
    for (int i = 0; i < m_transListCount; i++)
	delete m_transList[i];
    delete[] m_transList;
}

IAXTransaction* IAXEngine::addFrame(const SocketAddr& addr, IAXFrame* frame)
{
    if (!frame)
	return 0;
    for (int i = 0; i < m_transListCount; i++) {
	ObjList* l = m_transList[i];
	for (; l; l = l->next()) {
	    IAXTransaction* tr = static_cast<IAXTransaction*>(l->get());
	    if (tr && tr->process(addr,frame))
		return tr;
	}
    }
}

IAXTransaction* IAXEngine::addFrame(const SocketAddr& addr, const unsigned char* buf, unsigned int len)
{
    IAXFrame* frame = IAXFrame::parse(buf,len,this,&addr);
    if (!frame)
	return 0;
    IAXTransaction* tr = addFrame(addr,frame);
    frame->deref();
    return tr;
}

IAXEvent* IAXEngine::getEvent()
{
    Lock lock(this);
    for (int i = 0; i < m_transListCount; i++) {
	ObjList* l = m_transList[i];
	for (; l; l = l->next()) {
	    IAXTransaction* tr = static_cast<IAXTransaction*>(l->get());
	    if (!tr)
		continue;
	    IAXEvent* ev = tr->getEvent();
	    if (ev)
		return ev;
	}
    }
    return 0;
}

void IAXEngine::processEvent(IAXEvent* event)
{
    delete event;
}

bool IAXEngine::process()
{
    bool ok = false;
    for (;;) {
	IAXEvent* ev = getEvent();
	if (!ev)
	    break;
	processEvent(ev);
	ok = true;
    }
    return ok;
}


IAXEvent::IAXEvent(Type type, bool final, IAXTransaction* transaction)
    : m_type(type), m_final(final), m_transaction(0)
{
    if (transaction && transaction->ref())
	m_transaction = transaction;
}

IAXEvent::~IAXEvent()
{
    if (m_transaction)
	m_transaction->deref();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
