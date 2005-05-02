/**
 * transport.cpp
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

#define BUF_SIZE 1500

using namespace TelEngine;

RTPTransport::RTPTransport()
    : m_processor(0)
{
}

void RTPTransport::timerTick(const Time& when)
{
    if (m_rtpSock.valid()) {
	bool ok = false;
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	if (m_rtpSock.select(&ok,0,0,&tv) && ok) {
	    char buf[BUF_SIZE];
	    SocketAddr addr;
	    int len = m_rtpSock.recvFrom(buf,sizeof(buf),addr);
	    if (m_processor && (len >= 12) && (addr == m_remoteAddr))
		m_processor->rtpData(buf,len);
	}
    }
    if (m_rtcpSock.valid()) {
	bool ok = false;
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	if (m_rtcpSock.select(&ok,0,0,&tv) && ok) {
	    char buf[BUF_SIZE];
	    SocketAddr addr;
	    int len = m_rtcpSock.recvFrom(buf,sizeof(buf),addr);
	    if (m_processor && (len >= 8) && (addr == m_remoteRTCP))
		m_processor->rtcpData(buf,len);
	}
    }
}

void RTPTransport::rtpData(const void* data, int len)
{
    if (m_rtpSock.valid() && m_remoteAddr.valid())
	m_rtpSock.sendTo(data,len,m_remoteAddr);
}

void RTPTransport::rtcpData(const void* data, int len)
{
    if (m_rtcpSock.valid() && m_remoteRTCP.valid())
	m_rtcpSock.sendTo(data,len,m_remoteRTCP);
}

void RTPTransport::setProcessor(RTPProcessor* processor)
{
    m_processor = processor;
}

bool RTPTransport::localAddr(SocketAddr& addr)
{
    int p = addr.port();
    // make sure we don't have a port or it's an even one
    if ((p & 1) == 0) {
	m_localAddr = addr;
	return true;
    }
    return false;
}

bool RTPTransport::remoteAddr(SocketAddr& addr)
{
    int p = addr.port();
    // make sure we have a port and it's an even one
    if (p && ((p & 1) == 0)) {
	m_remoteAddr = addr;
	m_remoteRTCP = addr;
	m_remoteRTCP.port(addr.port()+1);
	return true;
    }
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
