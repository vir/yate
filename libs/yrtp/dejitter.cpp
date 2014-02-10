/**
 * dejitter.cpp
 * Yet Another RTP Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <yatertp.h>

using namespace TelEngine;

namespace { // anonymous

class RTPDelayedData : public DataBlock
{
public:
    inline RTPDelayedData(u_int64_t when, bool mark, int payload,
	unsigned int tstamp, const void* data, int len)
	: DataBlock(const_cast<void*>(data),len), m_scheduled(when),
	  m_marker(mark), m_payload(payload), m_timestamp(tstamp)
	{ }
    inline u_int64_t scheduled() const
	{ return m_scheduled; }
    inline bool marker() const
	{ return m_marker; }
    int payload() const
	{ return m_payload; }
    inline unsigned int timestamp() const
	{ return m_timestamp; }
private:
    u_int64_t m_scheduled;
    bool m_marker;
    int m_payload;
    unsigned int m_timestamp;
};

}; // anonymous namespace


RTPDejitter::RTPDejitter(RTPReceiver* receiver, unsigned int mindelay, unsigned int maxdelay)
    : m_receiver(receiver), m_minDelay(mindelay), m_maxDelay(maxdelay),
      m_headStamp(0), m_tailStamp(0), m_headTime(0), m_sampRate(125000), m_fastRate(10)
{
    if (m_maxDelay > 1000000)
	m_maxDelay = 1000000;
    if (m_maxDelay < 50000)
	m_maxDelay = 50000;
    if (m_minDelay < 5000)
	m_minDelay = 5000;
    if (m_minDelay > m_maxDelay - 30000)
	m_minDelay = m_maxDelay - 30000;
}

RTPDejitter::~RTPDejitter()
{
    DDebug(DebugInfo,"Dejitter destroyed with %u packets [%p]",m_packets.count(),this);
}

void RTPDejitter::clear()
{
    m_packets.clear();
    m_headStamp = m_tailStamp = 0;
}

bool RTPDejitter::rtpRecv(bool marker, int payload, unsigned int timestamp, const void* data, int len)
{
    u_int64_t when = 0;
    bool insert = false;

    if (m_headStamp) {
	// at least one packet got out of the queue
	int dTs = timestamp - m_headStamp;
	if (dTs == 0)
	    return true;
	else if (dTs < 0) {
	    DDebug(DebugNote,"Dejitter dropping TS %u, last delivered was %u [%p]",
		timestamp,m_headStamp,this);
	    return false;
	}
	u_int64_t now = Time::now();
	int64_t rate = 1000 * (now - m_headTime) / dTs;
	if (rate > 0) {
	    if (m_sampRate) {
		if (m_fastRate) {
		    m_fastRate--;
		    rate = (7 * m_sampRate + rate) >> 3;
		}
		else
		    rate = (31 * m_sampRate + rate) >> 5;
	    }
	    if (rate > 150000)
		rate = 150000; // 6.67 kHz
	    else if (rate < 20000)
		rate = 20000; // 50 kHz
	    m_sampRate = rate;
	    XDebug(DebugAll,"Time per sample " FMT64, rate);
	}
	else
	    rate = m_sampRate;
	if (rate > 0)
	    when = m_headTime + (dTs * rate / 1000) + m_minDelay;
	else
	    when = now + m_minDelay;
	if (m_tailStamp) {
	    if (timestamp == m_tailStamp)
		return true;
	    if (((int)(timestamp - m_tailStamp)) < 0)
		insert = true;
	    else if (when > now + m_maxDelay) {
		DDebug(DebugNote,"Packet with TS %u falls after max buffer [%p]",timestamp,this);
		return false;
	    }
	}
    }
    else {
	if (m_tailStamp && ((int)(timestamp - m_tailStamp)) < 0) {
	    // until we get some statistics don't attempt to reorder packets
	    DDebug(DebugNote,"Dejitter got TS %u while last queued was %u [%p]",timestamp,m_tailStamp,this);
	    return false;
	}
	// we got no packets out yet so use a fixed interval
	when = Time::now() + m_minDelay;
    }

    if (insert) {
	for (ObjList* l = m_packets.skipNull(); l; l = l->skipNext()) {
	    RTPDelayedData* pkt = static_cast<RTPDelayedData*>(l->get());
	    if (pkt->timestamp() == timestamp)
		return true;
	    if (pkt->timestamp() > timestamp && pkt->scheduled() > when) {
		l->insert(new RTPDelayedData(when,marker,payload,timestamp,data,len));
		return true;
	    }
	}
    }
    m_tailStamp = timestamp;
    m_packets.append(new RTPDelayedData(when,marker,payload,timestamp,data,len));
    return true;
}

void RTPDejitter::timerTick(const Time& when)
{
    RTPDelayedData* packet = static_cast<RTPDelayedData*>(m_packets.get());
    if (!packet) {
	m_tailStamp = 0;
	if (m_headStamp && (m_headTime + m_maxDelay < when))
	    m_headStamp = 0;
	return;
    }
    if (packet->scheduled() > when)
	return;
    m_packets.remove(packet,false);
    // remember the last delivered
    m_headStamp = packet->timestamp();
    m_headTime = packet->scheduled();
    if (m_receiver)
	m_receiver->rtpRecv(packet->marker(),packet->payload(),
	    packet->timestamp(),packet->data(),packet->length());
    TelEngine::destruct(packet);
    unsigned int count = 0;
    while ((packet = static_cast<RTPDelayedData*>(m_packets.get()))) {
	long int delayed = (long int)(when - packet->scheduled());
	if (delayed <= 0 || delayed <= (long)m_minDelay)
	    break;
	// we are too delayed - probably rtpRecv() took too long to complete...
	m_packets.remove(packet,true);
	count++;
    }
    if (count)
	Debug((count > 1) ? DebugMild : DebugNote,
	    "Dropped %u delayed packet%s from buffer [%p]",count,((count > 1) ? "s" : ""),this);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
