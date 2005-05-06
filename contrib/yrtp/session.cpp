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

bool RTPBaseIO::dataPayload(int type)
{
    if ((type >= -1) && (type <= 127)) {
	m_dataType = type;
	return true;
    }
    return false;
}

bool RTPBaseIO::eventPayload(int type)
{
    if ((type >= -1) && (type <= 127)) {
	m_eventType = type;
	return true;
    }
    return false;
}

bool RTPBaseIO::ciscoPayload(int type)
{
    if ((type >= -1) && (type <= 127)) {
	m_ciscoType = type;
	return true;
    }
    return false;
}

void RTPReceiver::rtpData(const void* data, int len)
{
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

RTPSession::RTPSession()
    : m_transport(0), m_direction(FullStop),
      m_sync(true),
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

bool RTPReceiver::rtpRecv(bool marker, int payload, unsigned int timestamp, const void* data, int len)
{
    if ((payload != dataPayload()) && (payload != eventPayload()))
	rtpNewPayload(payload,timestamp);
    if (payload == eventPayload())
	return decodeEvent(marker,timestamp,data,len);
    if (payload == ciscoPayload())
	return decodeCisco(marker,timestamp,data,len);
    finishEvent(timestamp);
    if (payload == dataPayload())
	return rtpRecvData(marker,timestamp,data,len);
    return false;
}

bool RTPReceiver::rtpRecvData(bool marker, unsigned int timestamp, const void* data, int len)
{
    return false;
}

bool RTPReceiver::rtpRecvEvent(int event, char key, int duration, int volume, unsigned int timestamp)
{
    return false;
}

void RTPReceiver::rtpNewPayload(int payload, unsigned int timestamp)
{
}

bool RTPReceiver::decodeEvent(bool marker, unsigned int timestamp, const void* data, int len)
{
    // we support only basic RFC2833, no RFC2198 redundancy
    if (len != 4)
	return false;
    const unsigned char* pc = (const unsigned char*)data;
    int event = pc[0];
    int vol = pc[1] & 0x3f;
    bool end = (pc[1] & 0x80) != 0;
    int duration = ((int)pc[2] << 8) | pc[3];
    if (m_evTs) {
	if ((m_evNum != event) && (m_evTs <= timestamp))
	    pushEvent(m_evNum,timestamp - m_evTs,m_evVol,m_evTs);
    }
    m_evTs = timestamp;
    m_evNum = event;
    m_evVol = vol;
}

bool RTPReceiver::decodeCisco(bool marker, unsigned int timestamp, const void* data, int len)
{
}

void RTPReceiver::finishEvent(unsigned int timestamp)
{
    if (!m_evTs)
	return;
    int duration = timestamp - m_evTs;
    if (duration < 10000)
	return;
    timestamp = m_evTs;
    m_evTs = 0;
    pushEvent(m_evNum,duration,m_evVol,timestamp);
}

bool RTPReceiver::pushEvent(int event, int duration, int volume, unsigned int timestamp)
{
    static const char dtmf[] = "0123456789*#ABCDF";
    char key = (event <= 16) ? dtmf[event] : 0;
    return rtpRecvEvent(event,key,duration,volume,timestamp);
}

bool RTPSender::rtpSend(bool marker, int payload, unsigned int timestamp, const void* data, int len)
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

bool RTPSender::rtpSendData(bool marker, unsigned int timestamp, const void* data, int len)
{
    if (m_dataType < 0)
	return false;
    return rtpSend(marker,m_dataType,timestamp,data,len);
}

bool RTPSender::rtpSendEvent(int event, int duration, int volume, unsigned int timestamp)
{
    // send as RFC2833 if we have the payload type set
    if (eventPayload() >= 0) {
    }
    // else try FRF.11 Annex A (Cisco's way) if it's set up
    else if ((ciscoPayload() >= 0) && (event <= 16)) {
    }
    return false;
}

bool RTPSender::rtpSendKey(char key, int duration, int volume, unsigned int timestamp)
{
    int event = 0;
    if ((key >= '0') && (key <= '9'))
	event = key - '0';
    else if (key == '*')
	event = 10;
    else if (key == '#')
	event = 11;
    else if ((key >= 'A') && (key <= 'D'))
	event = key + 12 - 'A';
    else if ((key >= 'a') && (key <= 'd'))
	event = key + 12 - 'a';
    else if ((key == 'F') || (key == 'f'))
	event = 16;
    else
	return false;
    return rtpSendEvent(event,duration,volume,timestamp);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
