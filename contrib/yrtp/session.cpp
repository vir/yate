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
    : m_transport(0)
{
}

RTPSession::~RTPSession()
{
    if (m_transport) {
	m_transport->setProcessor(0);
	m_transport = 0;
    }
}

void RTPSession::timerTick(const Time& when)
{
    if (m_transport)
	m_transport->timerTick(when);
}

void RTPSession::rtpData(const void* data, int len)
{
}

void RTPSession::rtcpData(const void* data, int len)
{
}

/* vi: set ts=8 sw=4 sts=4 noet: */
