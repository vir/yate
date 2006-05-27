/**
 * zapchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Zapata telephony driver
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

#include <modules/libypri.h>

#ifdef _WINDOWS
#error This module is not for Windows
#else

extern "C" {
#include <linux/zaptel.h>
};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WINDOWS
#include <sys/ioctl.h>
#include <fcntl.h>
#endif

#ifndef ZT_EVENT_DTMFDIGIT
#ifdef ZT_EVENT_DTMFDOWN
#define ZT_EVENT_DTMFDIGIT ZT_EVENT_DTMFDOWN
#else
#define ZT_EVENT_DTMFDIGIT 0
#endif
#endif

#ifndef ZT_EVENT_PULSEDIGIT
#define ZT_EVENT_PULSEDIGIT 0
#endif

using namespace TelEngine;
namespace { // anonymous

/* Zaptel formats */
static TokenDict dict_str2ztlaw[] = {
    { "slin", -1 },
    { "default", ZT_LAW_DEFAULT },
    { "mulaw", ZT_LAW_MULAW },
    { "alaw", ZT_LAW_ALAW },
    { 0, -2 }
};

class ZapChan;

class ZapSpan : public PriSpan, public Thread
{
    friend class ZapDriver;
public:
    virtual ~ZapSpan();
    virtual void run();

private:
    ZapSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect, int fd);
    int m_fd;
};

class ZapSource : public PriSource, public Thread
{
public:
    ZapSource(ZapChan *owner, const char* format, unsigned int bufsize);
    ~ZapSource();
    virtual void run();
private:
    DataBlock m_data;
};

class ZapConsumer : public PriConsumer
{
public:
    ZapConsumer(ZapChan *owner, const char* format, unsigned int bufsize);
    ~ZapConsumer();
    virtual void Consume(const DataBlock &data, unsigned long tStamp);
private:
    unsigned int m_bufsize;
    DataErrors m_overruns;
};

class ZapChan : public PriChan
{
    friend class ZapSource;
    friend class ZapConsumer;
public:
    ZapChan(const PriSpan *parent, int chan, unsigned int bufsize);
    virtual ~ZapChan();
    virtual bool openData(const char* format, int echoTaps);
    virtual void closeData();
    inline int fd() const
	{ return m_fd; }
    inline int law() const
	{ return m_law; }
private:
    int m_fd;
    int m_law;
};

class ZapDriver : public PriDriver
{
    friend class PriSpan;
    friend class ZapHandler;
public:
    ZapDriver();
    virtual ~ZapDriver();
    virtual void initialize();
    virtual PriSpan* createSpan(PriDriver* driver, int span, int first, int chans, Configuration& cfg, const String& sect);
    virtual PriChan* createChan(const PriSpan* span, int chan, unsigned int bufsize);
};

INIT_PLUGIN(ZapDriver);

static int zt_get_event(int fd)
{
    /* Avoid the silly zt_getevent which ignores a bunch of events */
    int j = 0;
    if (::ioctl(fd, ZT_GETEVENT, &j) == -1)
	return -1;
    return j;
}

static int zt_open_dchan(int channo, int bsize = 1024, int nbufs = 16)
{
    DDebug(&__plugin,DebugInfo,"Opening zap d-channel %d with %d x %d buffers",channo,nbufs,bsize);
    int fd = ::open("/dev/zap/channel", O_RDWR, 0600);
    if (fd < 0) {
	Debug("Zaptel",DebugGoOn,"Failed to open device: error %d: %s",errno,::strerror(errno));
	return -1;
    }
    if (::ioctl(fd,ZT_SPECIFY,&channo) == -1) {
	Debug("Zaptel",DebugGoOn,"Failed to specify chan %d: error %d: %s",channo,errno,::strerror(errno));
	::close(fd);
	return -1;
    }
    ZT_PARAMS par;
    if (::ioctl(fd, ZT_GET_PARAMS, &par) == -1) {
	Debug("Zaptel",DebugGoOn,"Failed to get params of chan %d: error %d: %s",channo,errno,::strerror(errno));
	::close(fd);
	return -1;
    }
    if (par.sigtype != ZT_SIG_HDLCFCS) {
	Debug("Zaptel",DebugGoOn,"Channel %d is not in HDLC/FCS mode",channo);
	::close(fd);
	return -1;
    }
    ZT_BUFFERINFO bi;
    bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
    bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
    bi.numbufs = nbufs;
    bi.bufsize = bsize;
    if (::ioctl(fd, ZT_SET_BUFINFO, &bi) == -1)
	Debug("Zaptel",DebugWarn,"Could not set buffering on %d: error %d: %s",channo,errno,::strerror(errno));
    return fd;
}

static int zt_open_bchan(int channo, bool subchan, unsigned int blksize)
{
    DDebug(&__plugin,DebugInfo,"Opening zap b-channel %d with block size=%d",channo,blksize);
    int fd = ::open(subchan ? "/dev/zap/pseudo" : "/dev/zap/channel",O_RDWR|O_NONBLOCK);
    if (fd < 0) {
	Debug("Zaptel",DebugGoOn,"Failed to open device: error %d: %s",errno,::strerror(errno));
	return -1;
    }
    if (channo) {
	if (::ioctl(fd, subchan ? ZT_CHANNO : ZT_SPECIFY, &channo)) {
	    Debug("Zaptel",DebugGoOn,"Failed to specify chan %d: error %d: %s",channo,errno,::strerror(errno));
	    ::close(fd);
	    return -1;
	}
    }
    if (blksize) {
	if (::ioctl(fd, ZT_SET_BLOCKSIZE, &blksize) == -1) {
	    Debug("Zaptel",DebugGoOn,"Failed to set block size %d: error %d: %s",blksize,errno,::strerror(errno));
	    ::close(fd);
	    return -1;
	}
    }
    return fd;
}

static bool zt_set_law(int fd, int law)
{
    if (law < 0) {
	int linear = 1;
	if (::ioctl(fd, ZT_SETLINEAR, &linear) != -1)
	    return true;
    }
    else
	if (::ioctl(fd, ZT_SETLAW, &law) != -1)
	    return true;
    DDebug("Zaptel",DebugInfo,"Failed to set law %d: error %d: %s",law,errno,::strerror(errno));
    return false;
}

static bool zt_echo_cancel(int fd, int taps)
{
    if (::ioctl(fd, ZT_ECHOCANCEL, &taps) != -1)
	return true;
    DDebug("Zaptel",DebugInfo,"Failed to set %d echo cancellation taps: error %d: %s",taps,errno,::strerror(errno));
    return false;
}

ZapSpan::ZapSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect, int fd)
    : PriSpan(_pri,driver,span,first,chans,dchan,cfg,sect), Thread("ZapSpan"),
      m_fd(fd)
{
    Debug(m_driver,DebugAll,"ZapSpan::ZapSpan() [%p]",this);
}

ZapSpan::~ZapSpan()
{
    Debug(m_driver,DebugAll,"ZapSpan::~ZapSpan() [%p]",this);
    m_ok = false;
    ::close(m_fd);
    m_fd = -1;
}

void ZapSpan::run()
{
    Debug(m_driver,DebugAll,"ZapSpan::run() [%p]",this);
    fd_set rdfds;
    fd_set errfds;
    for (;;) {
	FD_ZERO(&rdfds);
	FD_SET(m_fd, &rdfds);
	FD_ZERO(&errfds);
	FD_SET(m_fd, &errfds);
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 100;
	int sel = ::select(m_fd+1, &rdfds, NULL, &errfds, &tv);
	Thread::check();
	if (!sel)
	    runEvent(true);
	else if (sel > 0) {
	    if (FD_ISSET(m_fd, &errfds)) {
		int zev = zt_get_event(m_fd);
		if (zev)
		    Debug(DebugInfo,"Zapata event %d on span %d",zev,span());
	    }
	    if (FD_ISSET(m_fd, &rdfds))
		runEvent(false);
	}
	else if (errno != EINTR)
	    Debug("ZapSpan",DebugGoOn,"select() error %d: %s",
		errno,::strerror(errno));
    }
}

ZapSource::ZapSource(ZapChan *owner, const char* format, unsigned int bufsize)
    : PriSource(owner,format,bufsize), Thread("ZapSource")
{
    Debug(m_owner,DebugAll,"ZapSource::ZapSource(%p) [%p]",owner,this);
}

ZapSource::~ZapSource()
{
    Debug(m_owner,DebugAll,"ZapSource::~ZapSource() [%p]",this);
}

void ZapSource::run()
{
    int rd = 0;
    for (;;) {
	Thread::yield(true);
	int fd = static_cast<ZapChan*>(m_owner)->fd();
	if (fd != -1) {
	    rd = ::read(fd,m_buffer.data(),m_buffer.length());
	    XDebug(m_owner,DebugAll,"ZapSource read %d bytes [%p]",rd,this);
	    if (rd > 0)
		Forward(m_buffer);
	    else if (rd < 0) {
		if ((errno != EAGAIN) && (errno != EINTR)) {
		    int zev = zt_get_event(fd);
		    if (zev) {
			Debug(m_owner,DebugInfo,"ZapSource event %d [%p]",zev,this);
			// driver-decoded digit arrived
			if (zev & (ZT_EVENT_DTMFDIGIT | ZT_EVENT_PULSEDIGIT)) {
			    char buf[2];
			    buf[0] = zev & 0xff;
			    buf[1] = '\0';
			    m_owner->gotDigits(buf);
			}
		    }
		    else
			break;
		}
	    }
	}
	else
	    break;
    }
    Debug(m_owner,DebugWarn,"ZapSource at EOF (read %d) [%p]",rd,this);
    // TODO: find a better way of dealing with this abnormal condition
    for (;;)
	Thread::yield(true);
}

ZapConsumer::ZapConsumer(ZapChan *owner, const char* format, unsigned int bufsize)
    : PriConsumer(owner,format,bufsize), m_bufsize(bufsize)
{
    Debug(m_owner,DebugAll,"ZapConsumer::ZapConsumer(%p) [%p]",owner,this);
}

ZapConsumer::~ZapConsumer()
{
    Debug(m_owner,DebugAll,"ZapConsumer::~ZapConsumer() [%p]",this);
    if (m_overruns.events())
	Debug(m_owner,DebugMild,"Consumer had %u overruns (%lu bytes)",
	    m_overruns.events(),m_overruns.bytes());
}

void ZapConsumer::Consume(const DataBlock &data, unsigned long tStamp)
{
    int fd = static_cast<ZapChan*>(m_owner)->fd();
    XDebug(DebugAll,"ZapConsumer fd=%d datalen=%u",fd,data.length());
    if ((fd != -1) && !data.null()) {
	if (m_buffer.length()+data.length() <= m_bufsize*4)
	    m_buffer += data;
	else {
	    m_overruns.update(data.length());
	    DDebug(m_owner,DebugAll,"ZapConsumer skipped %u bytes, buffer is full",data.length());
	}
	if (m_buffer.null())
	    return;
	if (m_buffer.length() >= m_bufsize) {
	    int wr = ::write(fd,m_buffer.data(),m_bufsize);
	    if (wr < 0) {
		if ((errno != EAGAIN) && (errno != EINTR))
		    Debug(DebugGoOn,"ZapConsumer write error %d: %s",
			errno,::strerror(errno));
	    }
	    else {
		if ((unsigned)wr != m_bufsize)
		    Debug(m_owner,DebugInfo,"ZapConsumer short write, %d of %u bytes",wr,m_bufsize);
		    m_buffer.cut(-wr);
	    }
	}
    }
}

ZapChan::ZapChan(const PriSpan *parent, int chan, unsigned int bufsize)
    : PriChan(parent,chan,bufsize), m_fd(-1), m_law(-1)
{
}

ZapChan::~ZapChan()
{
    closeData();
}

bool ZapChan::openData(const char* format, int echoTaps)
{
    m_fd = zt_open_bchan(m_abschan,false,m_bufsize);
    if (m_fd == -1)
	return false;
    int defLaw = ZT_LAW_ALAW;
    if (m_span->chans() == 24)
	defLaw = ZT_LAW_MULAW;
    defLaw = lookup(format,dict_str2ztlaw,defLaw);
    if (zt_set_law(m_fd,defLaw)) {
	m_law = defLaw;
	format = lookup(m_law,dict_str2ztlaw,"unknown");
	Debug(this,DebugInfo,"Opened Zap channel %d, law is: %s",m_abschan,format);
    }
    zt_echo_cancel(m_fd,echoTaps);
    ZapSource* src = new ZapSource(this,format,m_bufsize);
    setSource(src);
    src->startup();
    src->deref();
    setConsumer(new ZapConsumer(this,format,m_bufsize));
    getConsumer()->deref();
    return true;
}

void ZapChan::closeData()
{
    PriChan::closeData();
    if (m_fd != -1) {
	::close(m_fd);
	m_fd = -1;
    }
}

PriSpan* ZapDriver::createSpan(PriDriver* driver, int span, int first, int chans, Configuration& cfg, const String& sect)
{
    Debug(this,DebugAll,"ZapDriver::createSpan(%p,%d,%d,%d) [%p]",driver,span,first,chans,this);
    int netType = -1;
    int swType = -1;
    int dchan = -1;
    netParams(cfg,sect,chans,&netType,&swType,&dchan);
    if (dchan < 0)
	return 0;
    int fd = zt_open_dchan(dchan+first-1);
    if (fd < 0)
	return 0;
    pri* p = ::pri_new(fd,netType,swType);
    if (!p)
	return 0;
    ZapSpan *zs = new ZapSpan(p,driver,span,first,chans,dchan,cfg,sect,fd);
    zs->startup();
    return zs;
}

PriChan* ZapDriver::createChan(const PriSpan* span, int chan, unsigned int bufsize)
{
    Debug(this,DebugAll,"ZapDriver::createChan(%p,%d,%u) [%p]",span,chan,bufsize,this);
    return new ZapChan(span,chan,bufsize);
}

ZapDriver::ZapDriver()
    : PriDriver("zap")
{
    Output("Loaded module Zapchan");
}

ZapDriver::~ZapDriver()
{
    Output("Unloading module Zapchan");
}

void ZapDriver::initialize()
{
    Output("Initializing module Zapchan");
    init("zapchan");
}

}; // anonymous namespace

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
