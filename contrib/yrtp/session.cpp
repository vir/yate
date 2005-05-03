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

#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

RTPSession::RTPSession()
    : m_transport(0), m_direction(FullStop), m_sync(true),
      m_rxSsrc(0), m_rxTs(0), m_rxSeq(0),
      m_txSsrc(0), m_txTs(0), m_txSeq(0)
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
    // trivial check for basic fields validity
    if ((len < 12) || !data)
	return;
    const unsigned char* pc = (const unsigned char*)data;
    // check protocol version number
    if ((pc[0] & 0xc0) != 0x80)
	return;
    // check if padding is present and remove it
    if (pc[0] & 0x20) {
	len -= pc[len-1];
	if (len < 12)
	    return;
    }
    bool ext = (pc[0] & 0x10) != 0;
    int cc = pc[0] & 0x0f;
    bool marker = (pc[1] & 0x80) != 0;
    int typ = pc[1] & 0x7f;
    u_int16_t seq = ((u_int16_t)pc[2] << 8) | pc[3];
    u_int32_t ts = ((u_int32_t)pc[4] << 24) | ((u_int32_t)pc[5] << 16) |
	((u_int32_t)pc[6] << 8) | pc[7];
    u_int32_t ss = ((u_int32_t)pc[8] << 24) | ((u_int32_t)pc[9] << 16) |
	((u_int32_t)pc[10] << 8) | pc[11];

    // grab some data at the first packet received or resync
    if (m_sync) {
	m_sync = false;
	m_rxSsrc = ss;
	m_rxTs = ts;
	m_rxSeq = seq;
    }

    // check if the SSRC is unchanged
    if (ss != m_rxSsrc)
	return;

    // skip over header and any CSRC
    pc += 12+(4*cc);
    len -= 12+(4*cc);
    // check if extension is present and skip it
    if (ext) {
	if (len < 4)
	    return;
	int xl = ((int)pc[2] << 8) | pc[3];
	pc += xl+4;
	len -= xl+4;
    }
    if (len < 0)
	return;
    if (!len)
	pc = 0;
    rtpRecv(marker,typ,ts-m_rxTs,pc,len);
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

bool RTPSession::rtpRecv(bool marker, int payload, unsigned int timestamp, const void* data, int len)
{
    return false;
}

bool RTPSession::rtpSend(bool marker, int payload, unsigned int timestamp, const void* data, int len)
{
    switch (m_direction) {
	case FullStop:
	case RecvOnly:
	    return false;
	default:
	    break;
    }
    if (!m_transport)
	return false;

    if (!data)
	len = 0;
    payload &= 0x7f;
    if (marker)
	payload |= 0x80;
    timestamp += m_txTs;
    if (!m_txSsrc)
	m_txSsrc = ::random();
    m_txSeq++;

    DataBlock buf(0,len+12);
    unsigned char* pc = (unsigned char*)buf.data();
    *pc++ = 0x80;
    *pc++ = payload;
    *pc++ = m_txSeq >> 8;
    *pc++ = m_txSeq & 0xff;
    *pc++ = timestamp >> 24;
    *pc++ = timestamp >> 16;
    *pc++ = timestamp >> 8;
    *pc++ = timestamp & 0xff;
    *pc++ = m_txSsrc >> 24;
    *pc++ = m_txSsrc >> 16;
    *pc++ = m_txSsrc >> 8;
    *pc++ = m_txSsrc & 0xff;
    if (data && len)
	::memcpy(pc,data,len);
    static_cast<RTPProcessor*>(m_transport)->rtpData(buf.data(),buf.length());
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
