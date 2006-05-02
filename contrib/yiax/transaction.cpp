/**
 * transaction.cpp
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

using namespace TelEngine;

IAXTransaction::IAXTransaction(IAXEngine* engine, void* data)
    : m_engine(engine), m_private(data)
{
}

IAXTransaction::~IAXTransaction()
{
}

bool IAXTransaction::process(const SocketAddr& addr, IAXFrame* frame)
{
    if (!frame)
	return false;
    if (addr != m_addr)
	return false;
    const IAXFullFrame* ff = frame->fullFrame();
    if (!remoteCallNo()) {
	// can match miniframes only after receiving a remote call number
	if (!ff)
	    return false;
	if (ff->destCallNo() != localCallNo())
	    return false;
	// remember the remotely received call number
	m_rCallNo = ff->sourceCallNo();
    }
    if (frame->sourceCallNo() != remoteCallNo())
	return false;
    if (ff->destCallNo() != localCallNo())
	return false;
    // everything matched - it's ours
}

IAXEvent* IAXTransaction::getEvent()
{
}

/*
Allowed to allocate a local call number:
    AST_FRAME_IAX
	AST_COMMAND_ : NEW, REGREQ, POKE, REGREL, FWDOWNL
*/

/* vi: set ts=8 sw=4 sts=4 noet: */
