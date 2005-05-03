/**
 * session.cpp
 * Yet Another RTP Stack
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

#include <yatertp.h>

using namespace TelEngine;

RTPSession::RTPSession()
    : m_transport(0), m_direction(FullStop)
{
}

RTPSession::~RTPSession()
{
    direction(FullStop);
    if (m_transport) {
	RTPTransport* tmp = m_transport;
	m_transport = 0;
	tmp->setProcessor(0);
	delete tmp;
    }
}

void RTPSession::timerTick(const Time& when)
{
}

void RTPSession::rtpData(const void* data, int len)
{
    switch (m_direction) {
	case FullStop:
	case SendOnly:
	    return;
	default:
	    break;
    }
    if ((len < 12) || !data)
	return;
}

void RTPSession::rtcpData(const void* data, int len)
{
    switch (m_direction) {
	case FullStop:
	case SendOnly:
	    return;
	default:
	    break;
    }
    if ((len < 8) || !data)
	return;
}

void RTPSession::transport(RTPTransport* trans)
{
    if (trans == m_transport)
	return;
    if (m_transport)
	m_transport->setProcessor(0);
    m_transport = trans;
    if (m_transport)
	m_transport->setProcessor(this);
    else
	m_direction = FullStop;
}

bool RTPSession::direction(Direction dir)
{
    if ((dir != FullStop) && !m_transport)
	return false;
    m_direction = dir;
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
