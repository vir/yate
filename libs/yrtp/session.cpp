/**
 * session.cpp
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

#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

#define INF_TIMEOUT ((u_int64_t)(int64_t)-1)

RTPBaseIO::~RTPBaseIO()
{
    security(0);
}

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

bool RTPBaseIO::silencePayload(int type)
{
    if ((type >= -1) && (type <= 127)) {
	m_silenceType = type;
	return true;
    }
    return false;
}

unsigned int RTPBaseIO::ssrcInit()
{
    if (m_ssrcInit) {
	m_ssrcInit = false;
	do {
	    m_ssrc = ::random();
	} while (0 == m_ssrc);
    }
    return m_ssrc;
}

void RTPBaseIO::security(RTPSecure* secure)
{
    DDebug(DebugInfo,"RTPBaseIO::security(%p) old=%p [%p]",secure,m_secure,this);
    if (secure == m_secure)
	return;
    RTPSecure* tmp = m_secure;
    m_secure = 0;
    if (secure) {
	secure->owner(this);
	m_secure = secure;
    }
    else
	secLength(0,0);
    TelEngine::destruct(tmp);
}


RTPReceiver::~RTPReceiver()
{
    setDejitter(0);
}

void RTPReceiver::setDejitter(RTPDejitter* dejitter)
{
    if (dejitter == m_dejitter)
	return;
    DDebug(DebugInfo,"RTP setting new dejitter %p [%p]",dejitter,this);
    RTPDejitter* tmp = m_dejitter;
    m_dejitter = 0;
    if (tmp) {
	tmp->group(0);
	tmp->destruct();
    }
    // make the dejitter buffer belong to the same group as the session
    if (dejitter && m_session)
	dejitter->group(m_session->group());
    m_dejitter = dejitter;
}

void RTPReceiver::rtpData(const void* data, int len)
{
    // trivial check for basic fields validity
    if ((len < m_secLen + 12) || !data)
	return;
    const unsigned char* pc = (const unsigned char*)data;
    // check protocol version number
    if ((pc[0] & 0xc0) != 0x80)
	return;
    const unsigned char* secPtr = 0;
    if (m_secLen) {
	// security info is placed after data and padding
	len -= m_secLen;
	secPtr = pc + len;
    }
    // check if padding is present and remove it (but remember length)
    unsigned char padding = 0;
    if (pc[0] & 0x20) {
	len -= (padding = pc[len-1]);
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

    // grab some data at the first packet received or resync
    if (m_ssrcInit) {
	m_ssrcInit = false;
	m_ssrc = ss;
	m_ts = ts - m_tsLast;
	m_seq = seq-1;
	m_warn = true;
    }

    if (ss != m_ssrc) {
	rtpNewSSRC(ss,marker);
	// check if the SSRC is still unchanged
	if (ss != m_ssrc) {
	    if (m_warn) {
		m_warn = false;
		Debug(DebugWarn,"RTP Received SSRC %08X but expecting %08X [%p]",
		    ss,m_ssrc,this);
	    }
	    return;
	}
	// SSRC accepted, sync sequence and resync the timestamp offset
	m_seq = seq;
	m_ts = ts - m_tsLast;
	// drop this packet, next packet will come in correctly
	return;
    }

    // substraction with overflow
    int16_t ds = seq - m_seq;
    // received duplicate or delayed packet?
    if (ds <= 0) {
	DDebug(DebugMild,"RTP received SEQ %u while current is %u [%p]",seq,m_seq,this);
	return;
    }

    u_int32_t rollover = m_rollover;
    // this time compare unsigned to detect rollovers
    if (seq < m_seq)
	rollover++;
    u_int64_t seq48 = rollover;
    seq48 = (seq48 << 16) | seq;

    // if some security data is present authenticate the packet now
    if (secPtr && !rtpCheckIntegrity((const unsigned char*)data,len + padding + 12,secPtr + m_mkiLen,ss,seq48))
	return;

    // keep track of the last valid sequence number and timestamp we have seen
    m_seq = seq;
    m_rollover = rollover;
    m_tsLast = ts - m_ts;

    if (!len)
	pc = 0;
    if (rtpDecipher(const_cast<unsigned char*>(pc),len + padding,secPtr,ss,seq48))
	rtpRecv(marker,typ,m_tsLast,pc,len);
}

void RTPReceiver::rtcpData(const void* data, int len)
{
}

bool RTPReceiver::rtpRecv(bool marker, int payload, unsigned int timestamp, const void* data, int len)
{
    if ((payload != dataPayload()) && (payload != eventPayload()) && (payload != silencePayload()))
	rtpNewPayload(payload,timestamp);
    if (payload == eventPayload())
	return decodeEvent(marker,timestamp,data,len);
    if (payload == silencePayload())
	return decodeSilence(marker,timestamp,data,len);
    finishEvent(timestamp);
    if (payload == dataPayload()) {
#if 0
// dejitter is broken - don't use it
	if (m_dejitter)
	    return m_dejitter->rtpRecvData(marker,timestamp,data,len);
	else
#endif
	    return rtpRecvData(marker,timestamp,data,len);
    }
    return false;
}

bool RTPReceiver::rtpRecvData(bool marker, unsigned int timestamp, const void* data, int len)
{
    return m_session && m_session->rtpRecvData(marker,timestamp,data,len);
}

bool RTPReceiver::rtpRecvEvent(int event, char key, int duration, int volume, unsigned int timestamp)
{
    return m_session && m_session->rtpRecvEvent(event,key,duration,volume,timestamp);
}

void RTPReceiver::rtpNewPayload(int payload, unsigned int timestamp)
{
    if (m_session)
	m_session->rtpNewPayload(payload,timestamp);
}

void RTPReceiver::rtpNewSSRC(u_int32_t newSsrc, bool marker)
{
    if (m_session)
	m_session->rtpNewSSRC(newSsrc,marker);
}

bool RTPReceiver::decodeEvent(bool marker, unsigned int timestamp, const void* data, int len)
{
    // we support only basic RFC2833, no RFC2198 redundancy
    if (len < 4)
	return false;
    const unsigned char* pc = (const unsigned char*)data;
    for (; len >= 4; len-=4, pc+=4) {
	int event = pc[0];
	int vol = pc[1] & 0x3f;
	bool end = (pc[1] & 0x80) != 0;
	int duration = ((int)pc[2] << 8) | pc[3];
	if (m_evTs && (m_evNum >= 0)) {
	    if ((m_evNum != event) && (m_evTs <= timestamp))
		pushEvent(m_evNum,timestamp - m_evTs,m_evVol,m_evTs);
	}
	m_evVol = vol;
	if (!end) {
	    m_evTs = timestamp;
	    m_evNum = event;
	    continue;
	}
	if (m_evTs > timestamp)
	    return false;
	// make sure we don't see the same event again
	m_evTs = timestamp+1;
	m_evNum = -1;
	pushEvent(event,duration,vol,timestamp);
    }
    return true;
}

bool RTPReceiver::decodeSilence(bool marker, unsigned int timestamp, const void* data, int len)
{
    return false;
}

void RTPReceiver::finishEvent(unsigned int timestamp)
{
    if ((m_evNum < 0) || !m_evTs)
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

void RTPReceiver::timerTick(const Time& when)
{
}

bool RTPReceiver::rtpDecipher(unsigned char* data, int len, const void* secData, u_int32_t ssrc, u_int64_t seq)
{
    return (m_secure)
	? m_secure->rtpDecipher(data,len,secData,ssrc,seq)
	: true;
}

bool RTPReceiver::rtpCheckIntegrity(const unsigned char* data, int len, const void* authData, u_int32_t ssrc, u_int64_t seq)
{
    return (m_secure)
	? m_secure->rtpCheckIntegrity(data,len,authData,ssrc,seq)
	: true;
}


RTPSender::RTPSender(RTPSession* session, bool randomTs)
    : RTPBaseIO(session), m_evTime(0), m_tsLast(0), m_padding(0)
{
    if (randomTs) {
	m_ts = ::random() & ~1;
	// avoid starting sequence numbers too close to zero
	m_seq = 2500 + (::random() % 60000);
    }
}
		

bool RTPSender::rtpSend(bool marker, int payload, unsigned int timestamp, const void* data, int len)
{
    if (!(m_session && m_session->transport()))
	return false;

    if (!data)
	len = 0;
    payload &= 0x7f;
    if (marker || m_ssrcInit)
	payload |= 0x80;
    m_tsLast = timestamp;
    timestamp += m_ts;
    ssrcInit();
    m_seq++;
    if (m_seq == 0)
	m_rollover++;

    unsigned char padding = 0;
    unsigned char byte1 = 0x80;
    if (m_padding > 1) {
	padding = len % m_padding;
	if (padding) {
	    padding = m_padding - padding;
	    byte1 |= 0x20;
	}
    }

    DataBlock buf(0,len+padding+m_secLen+12);
    unsigned char* pc = (unsigned char*)buf.data();
    if (padding)
	pc[len + padding + 11] = padding;
    *pc++ = byte1;
    *pc++ = payload;
    *pc++ = (unsigned char)(m_seq >> 8);
    *pc++ = (unsigned char)(m_seq & 0xff);
    *pc++ = (unsigned char)(timestamp >> 24);
    *pc++ = (unsigned char)(timestamp >> 16);
    *pc++ = (unsigned char)(timestamp >> 8);
    *pc++ = (unsigned char)(timestamp & 0xff);
    *pc++ = (unsigned char)(m_ssrc >> 24);
    *pc++ = (unsigned char)(m_ssrc >> 16);
    *pc++ = (unsigned char)(m_ssrc >> 8);
    *pc++ = (unsigned char)(m_ssrc & 0xff);
    if (data && len) {
	::memcpy(pc,data,len);
	rtpEncipher(pc,len + padding);
    }
    if (m_secLen)
	rtpAddIntegrity((const unsigned char*)buf.data(),len + padding + 12,pc + (len + padding + m_mkiLen));
    static_cast<RTPProcessor*>(m_session->transport())->rtpData(buf.data(),buf.length());
    return true;
}

bool RTPSender::rtpSendData(bool marker, unsigned int timestamp, const void* data, int len)
{
    if (dataPayload() < 0)
	return false;
    if (sendEventData(timestamp))
	return true;
    return rtpSend(marker,dataPayload(),timestamp,data,len);
}

bool RTPSender::rtpSendEvent(int event, int duration, int volume, unsigned int timestamp)
{
    // send as RFC2833 if we have the payload type set
    if (eventPayload() < 0)
	return false;
    if ((duration <= 50) || (duration > 10000))
	duration = 1600;
    if (!timestamp)
	timestamp = m_tsLast;
    if (m_evTs) {
	Debug(DebugNote,"RFC 2833 overlapped in RTP event %d, session %p, fixing.",
	    event,m_session);
	// the timestamp must always advance to avoid misdetections
	if (timestamp == m_evTs)
	    m_tsLast = timestamp = timestamp + 2;
	// make sure we send an event end packet
	m_evTime = 0;
	sendEventData(timestamp);
    }
    m_evTs = timestamp;
    m_evNum = event;
    m_evVol = volume;
    m_evTime = duration;
    return sendEventData(timestamp);
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

bool RTPSender::sendEventData(unsigned int timestamp)
{
    if (m_evTs) {
	if (eventPayload() < 0) {
	    m_evTs = 0;
	    return false;
	}
	int duration = timestamp - m_evTs;
	char buf[4];
	buf[0] = m_evNum;
	buf[1] = m_evVol & 0x7f;
	buf[2] = duration >> 8;
	buf[3] = duration & 0xff;
	unsigned int tstamp = m_evTs;
	if (duration >= m_evTime) {
	    buf[1] |= 0x80;
	    m_evTs = 0;
	    // repeat the event end packet to increase chances it gets seen
	    if (rtpSend(!duration,eventPayload(),tstamp,buf,sizeof(buf)))
		m_seq--;
	}
	bool ok = rtpSend(!duration,eventPayload(),tstamp,buf,sizeof(buf));
	// have to update last timestamp since we sent the event start stamp
	m_tsLast = timestamp;
	return ok;
    }
    return false;
}

bool RTPSender::padding(int chunk)
{
    if ((chunk < 0) || (chunk > 128))
	return false;
    m_padding = chunk;
    return true;
}

void RTPSender::timerTick(const Time& when)
{
}

void RTPSender::rtpEncipher(unsigned char* data, int len)
{
    if (m_secure)
	m_secure->rtpEncipher(data,len);
}

void RTPSender::rtpAddIntegrity(const unsigned char* data, int len, unsigned char* authData)
{
    if (m_secure)
	m_secure->rtpAddIntegrity(data,len,authData);
}


RTPSession::RTPSession()
    : m_transport(0), m_direction(FullStop),
      m_send(0), m_recv(0), m_secure(0),
      m_timeoutTime(0), m_timeoutInterval(0)
{
    DDebug(DebugInfo,"RTPSession::RTPSession() [%p]",this);
}

RTPSession::~RTPSession()
{
    DDebug(DebugInfo,"RTPSession::~RTPSession() [%p]",this);
    direction(FullStop);
    group(0);
    transport(0);
    TelEngine::destruct(m_secure);
}

void RTPSession::timerTick(const Time& when)
{
    if (m_send)
	static_cast<RTPBaseIO*>(m_send)->timerTick(when);
    if (m_recv)
	static_cast<RTPBaseIO*>(m_recv)->timerTick(when);

    if (m_timeoutInterval) {
	// only check timeout if we have a receiver
	if (m_timeoutTime && m_recv) {
	    if (when >= m_timeoutTime) {
		// rearm timeout next time we get a packet
		m_timeoutTime = INF_TIMEOUT;
		timeout(0 == m_recv->ssrc());
	    }
	}
	else
	    m_timeoutTime = when + m_timeoutInterval;
    }
}

void RTPSession::rtpData(const void* data, int len)
{
    if ((m_direction & RecvOnly) == 0)
	return;
    if (m_recv) {
	m_timeoutTime = 0;
	m_recv->rtpData(data,len);
    }
}

void RTPSession::rtcpData(const void* data, int len)
{
    if ((m_direction & RecvOnly) == 0)
	return;
    if (m_recv) {
	m_timeoutTime = 0;
	m_recv->rtcpData(data,len);
    }
}

bool RTPSession::rtpRecvData(bool marker, unsigned int timestamp, const void* data, int len)
{
    XDebug(DebugAll,"RTPSession::rtpRecv(%s,%u,%p,%d) [%p]",
	String::boolText(marker),timestamp,data,len,this);
    return false;
}

bool RTPSession::rtpRecvEvent(int event, char key, int duration, int volume, unsigned int timestamp)
{
    XDebug(DebugAll,"RTPSession::rtpRecvEvent(%d,%02x,%d,%d,%u) [%p]",
	event,key,duration,volume,timestamp,this);
    return false;
}

void RTPSession::rtpNewPayload(int payload, unsigned int timestamp)
{
    XDebug(DebugAll,"RTPSession::rtpNewPayload(%d,%u) [%p]",
	payload,timestamp,this);
}

void RTPSession::rtpNewSSRC(u_int32_t newSsrc,bool marker)
{
    XDebug(DebugAll,"RTPSession::rtpNewSSRC(%08X,%s) [%p]",
	newSsrc,String::boolText(marker),this);
}

void RTPSession::timeout(bool initial)
{
    DDebug(DebugNote,"RTPSession::timeout(%s) [%p]",String::boolText(initial),this);
}

RTPSender* RTPSession::createSender()
{
    return new RTPSender(this);
}

RTPReceiver* RTPSession::createReceiver()
{
    return new RTPReceiver(this);
}

RTPTransport* RTPSession::createTransport()
{
    RTPTransport* trans = new RTPTransport();
    trans->group(group());
    return trans;
}

Cipher* RTPSession::createCipher(const String& name, Cipher::Direction dir)
{
    return 0;
}

bool RTPSession::checkCipher(const String& name)
{
    return false;
}

bool RTPSession::initGroup(int msec, Thread::Priority prio)
{
    if (m_group)
	return true;
    // try to pick the grop from the transport if it has one
    if (m_transport)
	group(m_transport->group());
    if (!m_group)
	group(new RTPGroup(msec,prio));
    if (!m_group)
	return false;
    if (m_transport)
	m_transport->group(m_group);
    return true;
}

bool RTPSession::initTransport()
{
    if (m_transport)
	return true;
    transport(createTransport());
    return (m_transport != 0);
}

void RTPSession::transport(RTPTransport* trans)
{
    DDebug(DebugInfo,"RTPSession::transport(%p) old=%p [%p]",trans,m_transport,this);
    if (trans == m_transport)
	return;
    RTPTransport* tmp = m_transport;
    m_transport = 0;
    if (tmp) {
	tmp->setProcessor(0);
	tmp->destruct();
    }
    m_transport = trans;
    if (m_transport)
	m_transport->setProcessor(this);
    else
	m_direction = FullStop;
}

void RTPSession::sender(RTPSender* send)
{
    DDebug(DebugInfo,"RTPSession::sender(%p) old=%p [%p]",send,m_send,this);
    if (send == m_send)
	return;
    RTPSender* tmp = m_send;
    m_send = send;
    if (tmp)
	delete tmp;
    if (m_send && m_secure) {
	RTPSecure* sec = m_secure;
	m_secure = 0;
	m_send->security(sec);
    }
}

void RTPSession::receiver(RTPReceiver* recv)
{
    DDebug(DebugInfo,"RTPSession::receiver(%p) old=%p [%p]",recv,m_recv,this);
    if (recv == m_recv)
	return;
    RTPReceiver* tmp = m_recv;
    m_recv = recv;
    if (tmp)
	delete tmp;
}

void RTPSession::security(RTPSecure* secure)
{
    if (m_send)
	m_send->security(secure);
    else if (secure != m_secure) {
	TelEngine::destruct(m_secure);
	m_secure = secure;
    }
}

bool RTPSession::direction(Direction dir)
{
    DDebug(DebugInfo,"RTPSession::direction(%d) old=%d [%p]",dir,m_direction,this);
    if ((dir != FullStop) && !m_transport)
	return false;

    if (dir & RecvOnly) {
	if (!m_recv)
	    receiver(createReceiver());
    }
    else
	receiver(0);

    if (dir & SendOnly) {
	if (!m_send)
	    sender(createSender());
    }
    else
	sender(0);

    m_direction = dir;
    return true;
}

bool RTPSession::dataPayload(int type)
{
    if (m_recv || m_send) {
	DDebug(DebugInfo,"RTPSession::dataPayload(%d) [%p]",type,this);
	bool ok = (!m_recv) || m_recv->dataPayload(type);
	return ((!m_send) || m_send->dataPayload(type)) && ok;
    }
    return false;
}

bool RTPSession::eventPayload(int type)
{
    if (m_recv || m_send) {
	DDebug(DebugInfo,"RTPSession::eventPayload(%d) [%p]",type,this);
	bool ok = (!m_recv) || m_recv->eventPayload(type);
	return ((!m_send) || m_send->eventPayload(type)) && ok;
    }
    return false;
}

bool RTPSession::silencePayload(int type)
{
    if (m_recv || m_send) {
	DDebug(DebugInfo,"RTPSession::silencePayload(%d) [%p]",type,this);
	bool ok = (!m_recv) || m_recv->silencePayload(type);
	return ((!m_send) || m_send->silencePayload(type)) && ok;
    }
    return false;
}

void RTPSession::setTimeout(int interval)
{
    if (interval) {
	if (interval < 0)
	    interval = 0;
	// force sane limits: between 500ms and 60s
	else if (interval < 500)
	    interval = 500;
	else if (interval > 60000)
	    interval = 60000;
    }
    m_timeoutTime = 0;
    m_timeoutInterval = interval * (u_int64_t)1000;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
