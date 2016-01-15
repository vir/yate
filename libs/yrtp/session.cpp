/**
 * session.cpp
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

#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

// u_int64_t Infinity
#define INF_TIMEOUT ((u_int64_t)(int64_t)-1)

// How many lost packets mean we lost sequence sync
#define SEQ_DESYNC_COUNT 50
// How many packets in a row will resync sequence
#define SEQ_RESYNC_COUNT 5

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
	    m_ssrc = Random::random();
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
    if (!len)
	pc = 0;

    // grab some data at the first packet received or resync
    if (m_ssrcInit) {
	m_ssrcInit = false;
	m_ssrc = ss;
	m_ts = ts - m_tsLast;
	m_seq = seq-1;
	m_seqCount = 0;
	m_warn = true;
	if (m_dejitter)
	    m_dejitter->clear();
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
	    m_wrongSSRC++;
	    return;
	}
	// SSRC accepted, sync sequence and resync the timestamp offset
	m_seq = seq;
	m_ts = ts - m_tsLast;
	m_seqCount = 0;
	if (m_dejitter)
	    m_dejitter->clear();
	// drop this packet, next packet will come in correctly
	return;
    }

    u_int32_t rollover = m_rollover;
    // compare unsigned to detect rollovers
    if (seq < m_seq)
	rollover++;
    u_int64_t seq48 = rollover;
    seq48 = (seq48 << 16) | seq;

    // if some security data is present authenticate the packet now
    if (secPtr && !rtpCheckIntegrity((const unsigned char*)data,len + padding + 12,secPtr + m_mkiLen,ss,seq48))
	return;

    // substraction with overflow to compute sequence difference
    int16_t ds = seq - m_seq;
    if (ds != 1)
	m_seqLost++;
    if (ds == 0)
	return;

    // check if we received a packet too much out of sequence
    // be much more tolerant when authenticating as we cannot resync
    if ((ds <= -SEQ_DESYNC_COUNT) || ((ds > SEQ_DESYNC_COUNT) && !secPtr)) {
	m_ioLostPkt++;
	if (!secPtr) {
	    // try to resync sequence unless we need to authenticate
	    if (m_seqCount++) {
		if (seq == ++m_seqSync) {
		    // good - packets numbers still in sequence
		    if (m_seqCount >= SEQ_RESYNC_COUNT) {
			Debug(DebugNote,"RTP sequence resync: %u -> %u [%p]",m_seq,seq,this);
			// sync sequence and resync the timestamp offset
			m_seq = seq;
			m_ts = ts - m_tsLast;
			m_seqCount = 0;
			if (m_warnSeq > 0)
			    m_warn = true;
			else
			    m_warnSeq = -1;
			m_syncLost++;
			if (m_dejitter)
			    m_dejitter->clear();
			// drop this packet, next packet will come in correctly
			return;
		    }
		}
		else
		    m_seqCount = 0;
	    }
	    else
		m_seqSync = seq;
	}
	if (m_warnSeq > 0) {
	    if (m_warn) {
		m_warn = false;
		Debug(DebugWarn,"RTP received SEQ %u while current is %u [%p]",seq,m_seq,this);
	    }
	}
	else if (m_warnSeq < 0) {
	    m_warnSeq = 0;
	    Debug(DebugInfo,"RTP received SEQ %u while current is %u [%p]",seq,m_seq,this);
	}
	return;
    }

    if (!rtpDecipher(const_cast<unsigned char*>(pc),len + padding,secPtr,ss,seq48))
	return;

    m_tsLast = ts - m_ts;
    m_seqCount = 0;
    m_ioPackets++;
    m_ioOctets += len;
    // keep track of the last valid sequence number and timestamp we have seen
    m_seq = seq;
    m_rollover = rollover;

    if (m_dejitter) {
	if (!m_dejitter->rtpRecv(marker,typ,m_tsLast,pc,len))
	    m_ioLostPkt++;
	return;
    }
    if (ds > 1)
	m_ioLostPkt += (ds - 1);
    if (ds >= 1)
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
    if (payload == dataPayload())
	return rtpRecvData(marker,timestamp,data,len);
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

void RTPReceiver::stats(NamedList& stat) const
{
    if (m_session)
	stat.setParam("remoteip",m_session->UDPSession::transport()->remoteAddr().host());
    stat.setParam("lostpkts",String(m_ioLostPkt));
    stat.setParam("synclost",String(m_syncLost));
    stat.setParam("wrongssrc",String(m_wrongSSRC));
    stat.setParam("seqslost",String(m_seqLost));
}


RTPSender::RTPSender(RTPSession* session, bool randomTs)
    : RTPBaseIO(session), m_evTime(0), m_padding(0)
{
    if (randomTs) {
	m_ts = Random::random() & ~1;
	// avoid starting sequence numbers too close to zero
	m_seq = (uint16_t)(2500 + (Random::random() % 60000));
    }
}

bool RTPSender::rtpSend(bool marker, int payload, unsigned int timestamp, const void* data, int len)
{
    if (!(m_session && m_session->UDPSession::transport()))
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
    m_ioPackets++;
    m_ioOctets += len;

    unsigned char padding = 0;
    unsigned char byte1 = 0x80;
    if (m_padding > 1) {
	padding = len % m_padding;
	if (padding) {
	    padding = m_padding - padding;
	    byte1 |= 0x20;
	}
    }

    m_buffer.resize(len + padding + m_secLen + 12);
    unsigned char* pc = (unsigned char*)m_buffer.data();
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
	rtpAddIntegrity((const unsigned char*)m_buffer.data(),len + padding + 12,pc + (len + padding + m_mkiLen));
    static_cast<RTPProcessor*>(m_session->UDPSession::transport())->rtpData(m_buffer.data(),m_buffer.length());
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

void RTPSender::stats(NamedList& stat) const
{
}


UDPSession::UDPSession()
    : m_transport(0), m_timeoutTime(0), m_timeoutInterval(0)
{
    DDebug(DebugAll,"UDPSession::UDPSession() [%p]",this);
}

UDPSession::~UDPSession()
{
    DDebug(DebugAll,"UDPSession::~UDPSession() [%p]",this);
    group(0);
    transport(0);
}

void UDPSession::timeout(bool initial)
{
    DDebug(DebugNote,"UDPSession::timeout(%s) [%p]",String::boolText(initial),this);
}

void UDPSession::transport(RTPTransport* trans)
{
    DDebug(DebugInfo,"UDPSession::transport(%p) old=%p [%p]",trans,m_transport,this);
    if (trans == m_transport)
	return;
    TelEngine::destruct(m_transport);
    m_transport = trans;
    if (m_transport)
	m_transport->setProcessor(this);
}

RTPTransport* UDPSession::createTransport()
{
    RTPTransport* trans = new RTPTransport();
    trans->group(group());
    return trans;
}

bool UDPSession::initGroup(int msec, Thread::Priority prio)
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

bool UDPSession::initTransport()
{
    if (m_transport)
	return true;
    transport(createTransport());
    return (m_transport != 0);
}

void UDPSession::setTimeout(int interval)
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


RTPSession::RTPSession()
    : Mutex(true,"RTPSession"),
      m_direction(FullStop),
      m_send(0), m_recv(0), m_secure(0),
      m_reportTime(0), m_reportInterval(0),
      m_warnSeq(1)
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
    if (m_reportInterval) {
	if (when >= m_reportTime) {
	    m_reportTime = when + m_reportInterval;
	    sendRtcpReport(when);
	}
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
	if ((m_timeoutTime != INF_TIMEOUT) || m_recv->ssrc())
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

RTPSender* RTPSession::createSender()
{
    return new RTPSender(this);
}

RTPReceiver* RTPSession::createReceiver()
{
    return new RTPReceiver(this);
}

Cipher* RTPSession::createCipher(const String& name, Cipher::Direction dir)
{
    return 0;
}

bool RTPSession::checkCipher(const String& name)
{
    return false;
}

void RTPSession::transport(RTPTransport* trans)
{
    if (!trans)
	sendRtcpBye();
    UDPSession::transport(trans);
    if (!m_transport)
	m_direction = FullStop;
}

void RTPSession::sender(RTPSender* send)
{
    DDebug(DebugInfo,"RTPSession::sender(%p) old=%p [%p]",send,m_send,this);
    if (send == m_send)
	return;
    sendRtcpBye();
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
    if (m_recv)
	m_recv->m_warnSeq = m_warnSeq;
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

void RTPSession::getStats(String& stats) const
{
    DDebug(DebugInfo,"RTPSession::getStats() tx=%p rx=%p [%p]",m_send,m_recv,this);
    if (m_send) {
	stats.append("PS=",",") << m_send->ioPackets();
	stats << ",OS=" << m_send->ioOctets();
    }
    if (m_recv) {
	stats.append("PR=",",") << m_recv->ioPackets();
	stats << ",OR=" << m_recv->ioOctets();
	stats << ",PL=" << m_recv->ioPacketsLost();
    }
}

void RTPSession::setReports(int interval)
{
    if (interval > 0 && m_transport && m_transport->rtcpSock()->valid()) {
	if (interval < 500)
	    interval = 500;
	else if (interval > 60000)
	    interval = 60000;
	m_reportInterval = interval * (u_int64_t)1000 + (Random::random() % 20000);
    }
    else
	m_reportInterval = 0;
    m_reportTime = 0;
}

void RTPSession::getStats(NamedList& stats) const
{
    if (m_send)
	m_send->stats(stats);
    if (m_recv)
	m_recv->stats(stats);
    stats.setParam("wrongsrc",String(m_wrongSrc));
}

static void store32(unsigned char* buf, unsigned int& len, u_int32_t val)
{
    buf[len++] = (unsigned char)(val >> 24);
    buf[len++] = (unsigned char)(val >> 16);
    buf[len++] = (unsigned char)(val >> 8);
    buf[len++] = (unsigned char)(val & 0xff);
}

void RTPSession::sendRtcpReport(const Time& when)
{
    if (!((m_send || m_recv) && m_transport && m_transport->rtcpSock()->valid()))
	return;
    unsigned char buf[52];
    buf[0] = 0x80; // RC=0
    buf[1] = 0xc9; // RR
    buf[2] = 0;
    unsigned int len = 8;
    if (m_send && m_send->ioPackets()) {
	// Include a sender report
	buf[1] = 0xc8; // SR
	// NTP timestamp
	store32(buf,len,(uint32_t)(2208988800 + (when.usec() / 1000000)));
	store32(buf,len,(uint32_t)(((when.usec() % 1000000) << 32) / 1000000));
	// RTP timestamp
	store32(buf,len,m_send->tsLast());
	// Packet and octet counters
	store32(buf,len,m_send->ioPackets());
	store32(buf,len,m_send->ioOctets());
    }
    if (m_recv && m_recv->ioPackets()) {
	// Add a single receiver report
	buf[0] |= 0x01; // RC=1
	store32(buf,len,m_recv->ssrc());
	u_int32_t lost = m_recv->ioPacketsLost();
	u_int32_t lostf = 0xff & (lost * 255 / (lost + m_recv->ioPackets()));
	store32(buf,len,(lost & 0xffffff) | (lostf << 24));
	store32(buf,len,(uint32_t)m_recv->fullSeq());
	// TODO: Compute and store Jitter, LSR and DLSR
	store32(buf,len,0);
	store32(buf,len,0);
	store32(buf,len,0);
    }
    // Don't send a RR with no receiver report blocks...
    if (len <= 8)
	return;
    DDebug(DebugInfo,"RTPSession sending RTCP Report [%p]",this);
    unsigned int lptr = 4;
    store32(buf,lptr,(m_send ? m_send->ssrcInit() : 0));
    buf[3] = (len - 1) / 4; // same as ((len + 3) / 4) - 1
    static_cast<RTPProcessor*>(m_transport)->rtcpData(buf,len);
}

void RTPSession::sendRtcpBye()
{
    if (!(m_send && m_transport && m_transport->rtcpSock()->valid()))
	return;
    u_int32_t ssrc = m_send->ssrc();
    if (!ssrc)
	return;
    DDebug(DebugInfo,"RTPSession sending RTCP Bye [%p]",this);
    // SSRC was initialized if we sent at least one RTP or RTCP packet
    unsigned char buf[8];
    buf[0] = 0x81;
    buf[1] = 0xcb;
    buf[2] = 0;
    buf[3] = 1; // len = 2 x 32bit
    buf[4] = (unsigned char)(ssrc >> 24);
    buf[5] = (unsigned char)(ssrc >> 16);
    buf[6] = (unsigned char)(ssrc >> 8);
    buf[7] = (unsigned char)(0xff & ssrc);
    static_cast<RTPProcessor*>(m_transport)->rtcpData(buf,8);
}

void RTPSession::incWrongSrc()
{
    XDebug(DebugAll,"RTPSession::incWrongSrc() [%p]",this);
    m_wrongSrc++;
}


UDPTLSession::UDPTLSession(u_int16_t maxLen, u_int8_t maxSec)
    : Mutex(true,"UDPTLSession"),
      m_rxSeq(0xffff), m_txSeq(0xffff),
      m_maxLen(maxLen), m_maxSec(maxSec),
      m_warn(true)
{
    DDebug(DebugInfo,"UDPTLSession::UDPTLSession(%u,%u) [%p]",maxLen,maxSec,this);
    if (m_maxLen < 96)
	m_maxLen = 96;
    else if (m_maxLen > 1492)
	m_maxLen = 1492;
}

UDPTLSession::~UDPTLSession()
{
    DDebug(DebugInfo,"UDPTLSession::~UDPTLSession() [%p]",this);
}

void UDPTLSession::timerTick(const Time& when)
{
    if (m_timeoutInterval) {
	if (m_timeoutTime) {
	    if (when >= m_timeoutTime) {
		// rearm timeout next time we get a packet
		m_timeoutTime = INF_TIMEOUT;
		timeout(0xffff == m_rxSeq);
	    }
	}
	else
	    m_timeoutTime = when + m_timeoutInterval;
    }
}

RTPTransport* UDPTLSession::createTransport()
{
    RTPTransport* trans = new RTPTransport(RTPTransport::UDPTL);
    trans->group(group());
    return trans;
}

void UDPTLSession::rtpData(const void* data, int len)
{
    if ((len < 6) || !data)
	return;
    m_timeoutTime = 0;
    const unsigned char* pd = (const unsigned char*)data;
    int pLen = pd[2];
    if (pLen > (len-5)) {
	// primary IFP does not fit in packet
	if ((m_rxSeq == 0xffff) && ((pd[0] & 0xc0) == 0x80) && m_warn) {
	    m_warn = false;
	    Debug(DebugWarn,"Receiving RTP instead of UDPTL [%p]",this);
	}
	return;
    }
    u_int16_t seq = pd[1] + (((u_int16_t)pd[0]) << 8);
    // substraction with overflow
    int16_t ds = seq - m_rxSeq;
    if ((m_rxSeq == 0xffff) && (seq != 0)) {
	// received sequence does not start at zero
	if ((pd[0] & 0xc0) == 0x80) {
	    if (m_warn) {
		m_warn = false;
		Debug(DebugWarn,"Receiving RTP instead of UDPTL [%p]",this);
	    }
	    return;
	}
	ds = 1;
    }
    if (ds < 0) {
	// received old packet
	if (m_warn) {
	    m_warn = false;
	    Debug(DebugWarn,"UDPTL received SEQ %u while current is %u [%p]",seq,m_rxSeq,this);
	}
	return;
    }
    m_warn = true;
    if (ds > 1) {
	// some packets were lost, try to recover
	if (0 == pd[pLen+3])
	    // recover from secondary IFPs
	    recoverSec(pd+pLen+5,len-pLen-5,seq-1,pd[pLen+4]);
    }
    m_rxSeq = seq;
    udptlRecv(pd+3,pLen,seq,false);
}

void UDPTLSession::recoverSec(const unsigned char* data, int len, u_int16_t seq, int nSec)
{
    if ((nSec <= 0) || (len <= 1))
	return;
    if ((int16_t)(seq - m_rxSeq) <= 0)
	return;
    int sLen = data[0];
    if (sLen >= len)
	return;
    // recursively recover from remaining secondaries
    recoverSec(data+sLen+1,len-sLen-1,seq-1,nSec-1);
    int16_t ds = seq - m_rxSeq;
    switch (ds) {
	case 1:
	    break;
	case 2:
	    Debug(DebugMild,"UDPTL lost IFP with SEQ %u [%p]",m_rxSeq+1,this);
	    break;
	default:
	    Debug(DebugWarn,"UDPTL lost IFPs with SEQ %u-%u [%p]",m_rxSeq+1,seq-1,this);
	    break;
    }
    Debug(DebugInfo,"UDPTL recovered IFP with SEQ %u [%p]",seq,this);
    m_rxSeq = seq;
    udptlRecv(data+1,sLen,seq,true);
}

bool UDPTLSession::udptlSend(const void* data, int len, u_int16_t seq)
{
    if (!(UDPSession::transport() && data && len))
	return false;
    Lock lck(this);
    int pl = len + 5;
    if ((len > 255) || (pl > m_maxLen)) {
	Debug(DebugWarn,"UDPTL could not send IFP with len=%d [%p]",len,this);
	m_txQueue.clear();
	return false;
    }
    // substraction with overflow
    int16_t ds = seq - m_txSeq;
    if (ds != 0) {
	if (ds != 1) {
	    Debug(DebugInfo,"UDPTL sending SEQ %u while current is %u [%p]",seq,m_txSeq,this);
	    m_txQueue.clear();
	}
	if (m_maxSec)
	    m_txQueue.insert(new DataBlock(const_cast<void*>(data),len));
    }
    DataBlock buf(0,m_maxLen);
    unsigned char* pd = buf.data(0,6);
    if (!pd)
	return false;
    pd[0] = (seq >> 8) & 0xff;
    pd[1] = seq & 0xff;
    pd[2] = len & 0xff;
    ::memcpy(pd+3,data,len);
    pd[len+3] = 0; // secondary IFPs
    int nSec = 0;
    for (ObjList* l = m_txQueue.skipNext(); l; l = l->skipNext()) {
	// truncate the TX queue when reaching maximum packet length or IFP count
	if (nSec >= m_maxSec) {
	    l->clear();
	    break;
	}
	DataBlock* d = static_cast<DataBlock*>(l->get());
	if ((pl+d->length()+1) > m_maxLen) {
	    l->clear();
	    break;
	}
	pd[pl] = d->length() & 0xff;
	::memcpy(pd+pl+1,d->data(),d->length());
	pl += d->length()+1;
	nSec++;
    }
    pd[len+4] = nSec;
    m_txSeq = seq;
    static_cast<RTPProcessor*>(UDPSession::transport())->rtpData(pd,pl);
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
