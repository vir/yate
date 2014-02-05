/**
 * transport.cpp
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

#define BUF_SIZE 1500

using namespace TelEngine;

static unsigned long s_sleep = 5;

// Set IPv6 sin6_scope_id for remote addresses from local address
// recvFrom() will set the sin6_scope_id of the remote socket address
// This will avoid socket address comparison mismatch (same address, different scope id)
static inline void setScopeId(const SocketAddr& local, SocketAddr& sa1,
    SocketAddr& sa2, SocketAddr* sa3 = 0)
{
    if (local.family() != SocketAddr::IPv6)
	return;
    unsigned int val = local.scopeId();
    sa1.scopeId(val);
    sa2.scopeId(val);
    if (sa3)
	sa3->scopeId(val);
}


RTPGroup::RTPGroup(int msec, Priority prio)
    : Mutex(true,"RTPGroup"),
      Thread("RTP Group",prio), m_listChanged(false)
{
    DDebug(DebugInfo,"RTPGroup::RTPGroup() [%p]",this);
    if (msec < 1)
	msec = 1;
    if (msec > 50)
	msec = 50;
    m_sleep = msec;
}

RTPGroup::~RTPGroup()
{
    DDebug(DebugInfo,"RTPGroup::~RTPGroup() [%p]",this);
}

void RTPGroup::cleanup()
{
    DDebug(DebugInfo,"RTPGroup::cleanup() [%p]",this);
    lock();
    m_listChanged = true;
    ObjList* l = &m_processors;
    while (l) {
	RTPProcessor* p = static_cast<RTPProcessor*>(l->get());
	if (p) {
	    p->group(0);
	    if (p != static_cast<RTPProcessor*>(l->get()))
		continue;
	}
	l = l->next();
    }
    m_processors.clear();
    unlock();
}

void RTPGroup::run()
{
    DDebug(DebugInfo,"RTPGroup::run() [%p]",this);
    bool ok = true;
    while (ok) {
	unsigned long msec = m_sleep;
	if (msec < s_sleep)
	    msec = s_sleep;
	lock();
	Time t;
	ObjList* l = &m_processors;
	m_listChanged = false;
	for (ok = false;l;l = l->next()) {
	    RTPProcessor* p = static_cast<RTPProcessor*>(l->get());
	    if (p) {
		ok = true;
		p->timerTick(t);
		// the list is protected from other threads but can be changed
		//  from this one so if it happened we just break out and try
		//  again later rather than using an expensive ListIterator
		if (m_listChanged)
		    break;
	    }
	}
	unlock();
	Thread::msleep(msec,true);
    }
    DDebug(DebugInfo,"RTPGroup::run() ran out of processors [%p]",this);
}

void RTPGroup::join(RTPProcessor* proc)
{
    DDebug(DebugAll,"RTPGroup::join(%p) [%p]",proc,this);
    lock();
    m_listChanged = true;
    m_processors.append(proc)->setDelete(false);
    startup();
    unlock();
}

void RTPGroup::part(RTPProcessor* proc)
{
    DDebug(DebugAll,"RTPGroup::part(%p) [%p]",proc,this);
    lock();
    m_listChanged = true;
    m_processors.remove(proc,false);
    unlock();
}

void RTPGroup::setMinSleep(int msec)
{
    if (msec < 1)
	msec = 1;
    if (msec > 20)
	msec = 20;
    s_sleep = msec;
}


RTPProcessor::RTPProcessor()
    : m_wrongSrc(0), m_group(0)
{
    DDebug(DebugAll,"RTPProcessor::RTPProcessor() [%p]",this);
}

RTPProcessor::~RTPProcessor()
{
    DDebug(DebugAll,"RTPProcessor::~RTPProcessor() [%p]",this);
    group(0);
}

void RTPProcessor::group(RTPGroup* newgrp)
{
    DDebug(DebugAll,"RTPProcessor::group(%p) old=%p [%p]",newgrp,m_group,this);
    if (newgrp == m_group)
	return;
    if (m_group)
	m_group->part(this);
    m_group = newgrp;
    if (m_group)
	m_group->join(this);
}

void RTPProcessor::rtpData(const void* data, int len)
{
}

void RTPProcessor::rtcpData(const void* data, int len)
{
}

void RTPProcessor::getStats(String& stats) const
{
}


RTPTransport::RTPTransport(RTPTransport::Type type)
    : RTPProcessor(),
      m_type(type), m_processor(0), m_monitor(0), m_autoRemote(false),
      m_warnSendErrorRtp(true), m_warnSendErrorRtcp(true)
{
    DDebug(DebugAll,"RTPTransport::RTPTransport(%d) [%p]",type,this);
}

RTPTransport::~RTPTransport()
{
    DDebug(DebugAll,"RTPTransport::~RTPTransport() [%p]",this);
    RTPGroup* g = group();
    if (g)
	Debug(DebugGoOn,"RTPTransport destroyed while in RTPGroup %p [%p]",g,this);
    group(0);
    setProcessor();
    setMonitor();
}

void RTPTransport::destruct()
{
    group(0);
    setProcessor();
    setMonitor();
    RTPProcessor::destruct();
}

void RTPTransport::timerTick(const Time& when)
{
    XDebug(DebugAll,"RTPTransport::timerTick() group=%p [%p]",group(),this);
    if (m_rtpSock.valid()) {
	char buf[BUF_SIZE];
	int len;
	while ((len = m_rtpSock.recvFrom(buf,sizeof(buf),m_rxAddrRTP)) > 0) {
	    XDebug(DebugAll,"RTP/UDPTL from '%s:%d' length %d [%p]",
		m_rxAddrRTP.host().c_str(),m_rxAddrRTP.port(),len,this);
	    switch (m_type) {
		case RTP:
		    if (len < 12)
			continue;
		    if (((unsigned char)buf[0] & 0xc0) != 0x80)
			continue;
		    break;
		case UDPTL:
		    if (len < 6)
			continue;
		    break;
		default:
		    break;
	    }
	    if (!m_remoteAddr.valid())
		continue;
	    // looks like it's RTP or UDPTL, at least by length and version
	    bool preferred = false;
	    if ((m_autoRemote || (preferred = (m_rxAddrRTP == m_remotePref))) && (m_rxAddrRTP != m_remoteAddr)) {
		Debug(DebugInfo,"Auto changing RTP address from %s:%d to%s %s:%d",
		    m_remoteAddr.host().c_str(),m_remoteAddr.port(),
		    (preferred ? " preferred" : ""),
		    m_rxAddrRTP.host().c_str(),m_rxAddrRTP.port());
		// if we received from the preferred address don't auto change any more
		if (preferred)
		    m_remotePref.clear();
		remoteAddr(m_rxAddrRTP);
	    }
	    m_autoRemote = false;
	    if (m_rxAddrRTP == m_remoteAddr) {
		if (m_processor)
		    m_processor->rtpData(buf,len);
		if (m_monitor)
		    m_monitor->rtpData(buf,len);
	    }
	    else if (m_processor)
		m_processor->incWrongSrc();
	}
	m_rtpSock.timerTick(when);
    }
    if (m_rtcpSock.valid()) {
	char buf[BUF_SIZE];
	int len;
	while (((len = m_rtcpSock.recvFrom(buf,sizeof(buf),m_rxAddrRTCP)) >= 8) && (m_rxAddrRTCP == m_remoteRTCP)) {
	    XDebug(DebugAll,"RTCP from '%s:%d' length %d [%p]",
		m_rxAddrRTCP.host().c_str(),m_rxAddrRTCP.port(),len,this);
	    if (m_processor)
		m_processor->rtcpData(buf,len);
	    if (m_monitor)
		m_monitor->rtcpData(buf,len);
	}
	m_rtcpSock.timerTick(when);
    }
}

// Send data to remote party
// Put a debug message on failure
// Return true if all bytes were sent
static bool sendData(Socket& sock, const SocketAddr& to, const void* data, int len,
    const char* what, bool& flag)
{
    if (!sock.valid())
	return false;
    if (!to.valid()) {
	if (flag) {
	    flag = false;
	    SocketAddr local;
	    sock.getSockName(local);
	    Debug(DebugNote,"%s send failed (local=%s): invalid remote address",
		what,local.addr().c_str());
	}
	return false;
    }
    int wr = sock.sendTo(data,len,to);
    if (wr == Socket::socketError() && flag && !sock.canRetry()) {
	flag = false;
	// Retrieve the error before calling getSockName() to avoid reset
	String s;
	int e = sock.error();
	Thread::errorString(s,e);
	SocketAddr local;
	sock.getSockName(local);
	Debug(DebugNote,"%s send failed (local=%s remote=%s): %d %s",
	    what,local.addr().c_str(),to.addr().c_str(),e,s.c_str());
    }
    return wr == len;
}

void RTPTransport::rtpData(const void* data, int len)
{
    if (!data)
	return;
    switch (m_type) {
	case RTP:
	    if (len < 12)
		return;
	    break;
	case UDPTL:
	    if (len < 6)
		return;
	    break;
	default:
	    break;
    }
    sendData(m_rtpSock,m_remoteAddr,data,len,"RTP",m_warnSendErrorRtp);
}

void RTPTransport::rtcpData(const void* data, int len)
{
    if ((len < 8) || !data)
	return;
    sendData(m_rtcpSock,m_remoteRTCP,data,len,"RTCP",m_warnSendErrorRtcp);
}

void RTPTransport::setProcessor(RTPProcessor* processor)
{
    if (processor) {
	// both should run in the same RTP group
	if (group())
	    processor->group(group());
	else
	    group(processor->group());
    }
    m_processor = processor;
}

void RTPTransport::setMonitor(RTPProcessor* monitor)
{
    m_monitor = monitor;
}

bool RTPTransport::localAddr(SocketAddr& addr, bool rtcp)
{
    // check if sockets are already created and bound
    if (m_rtpSock.valid())
	return false;
    int p = addr.port();
    // for RTCP make sure we don't have a port or it's an even one
    if (rtcp && (p & 1))
	return false;
    m_warnSendErrorRtp = true;
    m_warnSendErrorRtcp = true;
    if (m_rtpSock.create(addr.family(),SOCK_DGRAM) && m_rtpSock.bind(addr)) {
	m_rtpSock.setBlocking(false);
	if (!rtcp) {
	    // RTCP not requested - we are done
	    m_rtpSock.getSockName(addr);
	    m_localAddr = addr;
	    setScopeId(m_localAddr,m_remoteAddr,m_remotePref);
	    return true;
	}
	if (!p) {
	    m_rtpSock.getSockName(addr);
	    p = addr.port();
	    if (p & 1) {
		// allocated odd port - have to swap sockets
		m_rtcpSock.attach(m_rtpSock.detach());
		addr.port(p-1);
		if (m_rtpSock.create(addr.family(),SOCK_DGRAM) && m_rtpSock.bind(addr)) {
		    m_rtpSock.setBlocking(false);
		    m_localAddr = addr;
		    setScopeId(m_localAddr,m_remoteAddr,m_remoteRTCP,&m_remotePref);
		    return true;
		}
		DDebug(DebugMild,"RTP Socket failed with code %d",m_rtpSock.error());
		m_rtpSock.terminate();
		m_rtcpSock.terminate();
		return false;
	    }
	}
	addr.port(p+1);
	if (m_rtcpSock.create(addr.family(),SOCK_DGRAM) && m_rtcpSock.bind(addr)) {
	    m_rtcpSock.setBlocking(false);
	    addr.port(p);
	    m_localAddr = addr;
	    setScopeId(m_localAddr,m_remoteAddr,m_remoteRTCP,&m_remotePref);
	    return true;
	}
#ifdef DEBUG
	else
	    Debug(DebugMild,"RTCP Socket failed with code %d",m_rtcpSock.error());
#endif
    }
#ifdef DEBUG
    else
	Debug(DebugMild,"RTP Socket failed with code %d",m_rtpSock.error());
#endif
    m_rtpSock.terminate();
    m_rtcpSock.terminate();
    return false;
}

bool RTPTransport::remoteAddr(SocketAddr& addr, bool sniff)
{
    Lock lock(group());
    m_autoRemote = sniff;
    int p = addr.port();
    // make sure we have a valid address and a port
    // we do not check that it's even numbered as many NAPTs will break that
    if (p && addr.valid()) {
	m_warnSendErrorRtp = true;
	m_warnSendErrorRtcp = true;
	m_remoteAddr = addr;
	m_remoteRTCP = addr;
	m_remoteRTCP.port(addr.port()+1);
	// if sniffing packets from other sources remember preferred address
	if (sniff)
	    m_remotePref = addr;
	setScopeId(m_localAddr,m_remoteAddr,m_remoteRTCP,sniff ? &m_remotePref : 0);
	return true;
    }
    return false;
}

bool RTPTransport::setBuffer(int bufLen)
{
#ifdef SO_RCVBUF
    if (bufLen < 1024)
	bufLen = 1024;
    else if (bufLen > 65536)
	bufLen = 65536;
    bool ok = m_rtpSock.valid() && m_rtpSock.setOption(SOL_SOCKET,SO_RCVBUF,&bufLen,sizeof(bufLen));
    if (ok && m_rtcpSock.valid())
	ok = m_rtcpSock.setOption(SOL_SOCKET,SO_RCVBUF,&bufLen,sizeof(bufLen));
    return ok;
#else
    return false;
#endif
}

bool RTPTransport::drillHole()
{
    if (m_rtpSock.valid() && m_remoteAddr.valid()) {
	static const char buf[4] = { 0, 0, 0, 0 };
	if (m_rtpSock.sendTo(buf,sizeof(buf),m_remoteAddr) == sizeof(buf)) {
	    if (m_rtcpSock.valid() && m_remoteRTCP.valid())
		m_rtcpSock.sendTo(buf,sizeof(buf),m_remoteRTCP);
	    return true;
	}
    }
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
