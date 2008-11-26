/**
 * dejitter.cpp
 * Yet Another RTP Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatertp.h>

using namespace TelEngine;

class RTPDelayedData : public DataBlock
{
public:
    inline RTPDelayedData(u_int64_t when, bool mark, unsigned int tstamp, const void* data, int len)
	: DataBlock(const_cast<void*>(data),len), m_scheduled(when), m_marker(mark), m_timestamp(tstamp)
	{ }
    inline u_int64_t scheduled() const
	{ return m_scheduled; }
    inline bool marker() const
	{ return m_marker; }
    inline unsigned int timestamp() const
	{ return m_timestamp; }
    inline void schedule(u_int64_t when)
	{ m_scheduled = when; }
private:
    u_int64_t m_scheduled;
    bool m_marker;
    unsigned int m_timestamp;
};


RTPDejitter::RTPDejitter(RTPReceiver* receiver, unsigned int mindelay, unsigned int maxdelay)
    : m_receiver(receiver), m_mindelay(mindelay), m_maxdelay(maxdelay),
      m_headStamp(0), m_tailStamp(0), m_headTime(0), m_tailTime(0)
{
    if (m_maxdelay > 2000000)
	m_maxdelay = 2000000;
    if (m_maxdelay < 50000)
	m_maxdelay = 50000;
    if (m_mindelay < 5000)
	m_mindelay = 5000;
    if (m_mindelay > m_maxdelay - 20000)
	m_mindelay = m_maxdelay - 20000;
}

RTPDejitter::~RTPDejitter()
{
    DDebug(DebugMild,"Dejitter destroyed with %u packets [%p]",m_packets.count(),this);
}

bool RTPDejitter::rtpRecvData(bool marker, unsigned int timestamp, const void* data, int len)
{
    u_int64_t when = 0;
    bool insert = false;

    if (m_headStamp && (m_tailStamp != m_headStamp)) {
	// at least one packet got out of the queue and another is waiting
	int dTs = timestamp - m_headStamp;
	if (dTs < 0) {
	    DDebug(DebugMild,"Dejitter got TS %u while last delivered was %u [%p]",timestamp,m_headStamp,this);
	    return false;
	}
	u_int64_t bufTime = m_tailTime - m_headTime;
	int bufStamp = m_tailStamp - m_headStamp;
	if (bufStamp <= 0)
	    Debug(DebugWarn,"Oops! %d [%p]",bufStamp,this);
	// interpolate or extrapolate the delivery time for the packet
	// rounding down is ok - the buffer will slowly shrink as expected
	when = dTs * bufTime / bufStamp;
DDebug(DebugMild,"Dejitter when=" FMT64U " dTs=%d bufTime=" FMT64U " bufSTamp=%d [%p]",
    when,dTs,bufTime,bufStamp,this);
	when += m_headTime;
	if (dTs > bufStamp) {
	    bufTime = when - m_headTime;
	    if (bufTime > m_maxdelay) {
		// buffer has lagged behind so we must drop some old packets
		// and also reschedule the others
		DDebug(DebugMild,"Dejitter grew to " FMT64U " [%p]",bufTime,this);
//		when = m_headTime - 
	    }
	}
	else
	    // timestamp falls inside buffer so we must insert the packet
	    // between the already scheduled ones
	    insert = true;
    }
    else {
	if (m_tailStamp) {
	    int dTs = timestamp - m_tailStamp;
	    if (dTs < 0) {
		// until we get some statistics don't attempt to reorder packets
		DDebug(DebugMild,"Dejitter got TS %u while last queued was %u [%p]",timestamp,m_tailStamp,this);
		return false;
	    }
	}
	// we got no packets out yet so use a fixed interval
	when = Time::now() + m_mindelay;
    }

    if (when > m_tailTime) {
	// remember the latest in the queue
	m_tailStamp = timestamp;
	m_tailTime = when;
    }
    RTPDelayedData* packet = new RTPDelayedData(when,marker,timestamp,data,len);
    if (insert) {
	for (ObjList* l = m_packets.skipNull();l;l = l->skipNext()) {
	    RTPDelayedData* pkt = static_cast<RTPDelayedData*>(l->get());
	    if (pkt->scheduled() > when) {
		DDebug(DebugMild,"Dejitter inserting packet %p before %p [%p]",packet,pkt,this);
		l->insert(packet);
		return true;
	    }
	}
    }
    m_packets.append(packet);
    return true;
}

void RTPDejitter::timerTick(const Time& when)
{
    RTPDelayedData* packet = static_cast<RTPDelayedData*>(m_packets.get());
    if (!packet) {
	// queue is empty - reset timestamps
	m_headStamp = m_tailStamp = 0;
	return;
    }
    if (packet->scheduled() <= when) {
	// remember the last delivered
	m_headStamp = packet->timestamp();
	m_headTime = when;
	if (m_receiver)
	    m_receiver->rtpRecvData(packet->marker(),packet->timestamp(),packet->data(),packet->length());
	m_packets.remove(packet);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
