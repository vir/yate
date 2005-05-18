/**
 * wpchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wanpipe PRI cards telephony driver
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

#include <modules/libypri.h>

#ifndef _WINDOWS
#error This module is only for Windows
#else

extern "C" {

#define MSG_NOSIGNAL 0
#define MSG_DONTWAIT 0
#include <winioctl.h>
#define IOCTL_WRITE 1
#define IOCTL_READ 2
#define IOCTL_MGMT 3
#define IoctlWriteCommand \
	CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_WRITE, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define IoctlReadCommand \
	CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_READ, METHOD_IN_DIRECT, FILE_ANY_ACCESS)

};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


using namespace TelEngine;

class WpChan;

class WpSpan : public PriSpan, public Thread
{
    friend class WpData;
    friend class WpReader;
    friend class WpWriter;
    friend class WpDriver;
public:
    virtual ~WpSpan();
    virtual void run();
    int dataRead(void *buf, int buflen);
    int dataWrite(void *buf, int buflen);

private:
    WpSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect);
    WpData* m_data;
    WpReader* m_reader;
    WpWriter* m_writer;
    DataBlock m_rdata;
    DataBlock m_wdata;
};

class WpSource : public PriSource
{
public:
    WpSource(WpChan *owner, const char* format, unsigned int bufsize);
    ~WpSource();
    void put(unsigned char val);

private:
    unsigned int m_bufpos;
};

class WpConsumer : public PriConsumer, public Fifo
{
public:
    WpConsumer(WpChan *owner, const char* format, unsigned int bufsize);
    ~WpConsumer();

    virtual void Consume(const DataBlock &data, unsigned long timeDelta);
};

class WpChan : public PriChan
{
    friend class WpSource;
    friend class WpConsumer;
    friend class WpData;
public:
    WpChan(const PriSpan *parent, int chan, unsigned int bufsize);
    virtual ~WpChan();
    virtual bool openData(const char* format, int echoTaps);

private:
    WpSource* m_wp_s;
    WpConsumer* m_wp_c;
};

class WpData : public Thread
{
public:
    WpData(WpSpan* span, const char* card, const char* device);
    ~WpData();
    virtual void run();
private:
    WpSpan* m_span;
    HANDLE m_fd;
    unsigned char* m_buffer;
    WpChan **m_chans;
};

class WpReader : public Thread
{
public:
    WpReader(WpSpan* span, const char* card, const char* device);
    ~WpReader();
    virtual void run();
private:
    WpSpan* m_span;
    HANDLE m_fd;
};

class WpWriter : public Thread
{
public:
    WpWriter(WpSpan* span, const char* card, const char* device);
    ~WpWriter();
    virtual void run();
private:
    WpSpan* m_span;
    HANDLE m_fd;
};

class WpDriver : public PriDriver
{
    friend class PriSpan;
    friend class WpHandler;
public:
    WpDriver();
    virtual ~WpDriver();
    virtual void initialize();
    virtual PriSpan* createSpan(PriDriver* driver, int span, int first, int chans, Configuration& cfg, const String& sect);
    virtual PriChan* createChan(const PriSpan* span, int chan, unsigned int bufsize);
};

INIT_PLUGIN(WpDriver);

#define WP_HEADER 16

static int wp_recv(HANDLE fd, void *buf, int buflen, int flags = 0)
{
    int r = 0;
    if (!DeviceIoControl(fd,IoctlReadCommand,0,0,buf,buflen,(LPDWORD)&r,0))
	r = 0;
    return r;
}

static int wp_send(HANDLE fd, void *buf, int buflen, int flags = 0)
{
    int w = 0;
    if (!DeviceIoControl(fd,IoctlWriteCommand,buf,buflen,buf,buflen,(LPDWORD)&w,0))
	w = 0;
    return w;
}

static int wp_read(struct pri *pri, void *buf, int buflen)
{
    WpSpan* span = (WpSpan*)::pri_get_userdata(pri);
    return span ? span->dataRead(buf,buflen) : 0;
}

int WpSpan::dataRead(void *buf, int buflen)
{
    Lock mylock(this);
    if (m_rdata.data() && buf && (int)m_rdata.length() <= buflen) {
	buflen = m_rdata.length();
	::memcpy(buf,m_rdata.data(),buflen);
	m_rdata.clear();
	Debug(&__plugin,DebugAll,"WpSpan dequeued %d bytes block [%p]",buflen,this);
	return buflen+2;
    }
    return 0;
}

static int wp_write(struct pri *pri, void *buf, int buflen)
{
    WpSpan* span = (WpSpan*)::pri_get_userdata(pri);
    return span ? span->dataWrite(buf,buflen) : 0;
}

int WpSpan::dataWrite(void *buf, int buflen)
{
    Lock mylock(this);
    if (m_wdata.null() && buf && (buflen > 2)) {
	buflen -= 2;
	m_wdata.assign(0,buflen+WP_HEADER);
	unsigned char *d = (unsigned char*)m_wdata.data();
	const unsigned char* s = (const unsigned char*)buf;
	d += WP_HEADER;
	for (int i=0; i < buflen; i++)
	    *d++ = PriDriver::bitswap(*s++);

	Debug(&__plugin,DebugAll,"WpSpan queued %d bytes block [%p]",m_wdata.length(),this);
	return buflen+2;
    }
    return 0;
}

static bool wp_select(HANDLE fd,int samp,bool* errp = 0)
{
    return (::WaitForSingleObject(fd,samp/8) == WAIT_OBJECT_0);
}

void wp_close(HANDLE fd)
{
    if (fd == INVALID_HANDLE_VALUE)
	return;
    ::CloseHandle(fd);
}

static HANDLE wp_open(const char* card, const char* device)
{
    DDebug(DebugAll,"wp_open('%s','%s')",card,device);
    if (null(card) || null(device))
	return INVALID_HANDLE_VALUE;
    String devname("\\\\.\\");
    devname << card << "_" << device;
    HANDLE fd = ::CreateFile(
	devname,
	GENERIC_READ|GENERIC_WRITE,
	FILE_SHARE_READ|FILE_SHARE_WRITE,
	0,
	OPEN_EXISTING,
	FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH,
	0);
    if (fd == INVALID_HANDLE_VALUE) {
	Debug(DebugGoOn,"Wanpipe failed to open device '%s': error %d: %s",
	    devname.c_str(),errno,::strerror(errno));
	return fd;
    }
    return fd;
}

WpReader::WpReader(WpSpan* span, const char* card, const char* device)
    : Thread("WpReader"), m_span(span), m_fd(INVALID_HANDLE_VALUE)
{
    Debug(&__plugin,DebugAll,"WpReader::WpReader(%p) [%p]",span,this);
    m_fd = wp_open(card,device);
    m_span->m_reader = this;
}

WpReader::~WpReader()
{
    Debug(&__plugin,DebugAll,"WpReader::~WpReader() [%p]",this);
    m_span->m_reader = 0;
    HANDLE tmp = m_fd;
    m_fd = INVALID_HANDLE_VALUE;
    wp_close(tmp);
}

void WpReader::run()
{
    while (m_span && (m_fd != INVALID_HANDLE_VALUE)) {
	Thread::msleep(1,true);
	Lock mylock(m_span);
	if (m_span->m_rdata.data())
	    continue;
	mylock.drop();
	unsigned char buf[1500+WP_HEADER];
	int r = wp_recv(m_fd,buf,sizeof(buf)) - WP_HEADER;
	Debug(&__plugin,DebugAll,"WpReader read returned %d [%p]",r,this);
	if (r <= 0)
	    continue;
	m_span->lock();
	m_span->m_rdata.assign(0,r);
	unsigned char *d = (unsigned char*)m_span->m_rdata.data();
	const unsigned char* s = buf;
	s += WP_HEADER;
	for (int i=0; i < r; i++)
	    *d++ = PriDriver::bitswap(*s++);
	Debug(&__plugin,DebugAll,"WpReader queued %d bytes block [%p]",r,this);
	m_span->unlock();
    }
}

WpWriter::WpWriter(WpSpan* span, const char* card, const char* device)
    : Thread("WpWriter"), m_span(span), m_fd(INVALID_HANDLE_VALUE)
{
    Debug(&__plugin,DebugAll,"WpWriter::WpWriter(%p) [%p]",span,this);
    m_fd = wp_open(card,device);
    m_span->m_writer = this;
}

WpWriter::~WpWriter()
{
    Debug(&__plugin,DebugAll,"WpWriter::~WpWriter() [%p]",this);
    m_span->m_writer = 0;
    HANDLE tmp = m_fd;
    m_fd = INVALID_HANDLE_VALUE;
    wp_close(tmp);
}

void WpWriter::run()
{
    while (m_span && (m_fd != INVALID_HANDLE_VALUE)) {
	Thread::msleep(1,true);
	Lock mylock(m_span);
	if (m_span->m_wdata.null())
	    continue;
	DataBlock block(m_span->m_wdata);
	m_span->m_wdata.clear();
	mylock.drop();
	Debug(&__plugin,DebugAll,"WpWriter writing %d bytes block [%p]",block.length(),this);
	wp_send(m_fd,block.data(),block.length());
    }
}

WpSpan::WpSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect)
    : PriSpan(_pri,driver,span,first,chans,dchan,cfg,sect), Thread("WpSpan"),
      m_data(0), m_reader(0), m_writer(0)
{
    Debug(&__plugin,DebugAll,"WpSpan::WpSpan() [%p]",this);
}

WpSpan::~WpSpan()
{
    Debug(&__plugin,DebugAll,"WpSpan::~WpSpan() [%p]",this);
    m_ok = false;
    delete m_data;
    delete m_reader;
    delete m_writer;
}

void WpSpan::run()
{
    Debug(&__plugin,DebugAll,"WpSpan::run() [%p]",this);
    for (;;) {
	Thread::msleep(1,true);
	lock();
	runEvent(m_rdata.null());
	unlock();
    }
}

WpSource::WpSource(WpChan *owner, const char* format, unsigned int bufsize)
    : PriSource(owner,format,bufsize),
      m_bufpos(0)
{
    Debug(m_owner,DebugAll,"WpSource::WpSource(%p) [%p]",owner,this);
    static_cast<WpChan*>(m_owner)->m_wp_s = this;
}

WpSource::~WpSource()
{
    Debug(m_owner,DebugAll,"WpSource::~WpSource() [%p]",this);
    static_cast<WpChan*>(m_owner)->m_wp_s = 0;
}

void WpSource::put(unsigned char val)
{
    ((char*)m_buffer.data())[m_bufpos] = val;
    if (++m_bufpos >= m_buffer.length()) {
	m_bufpos = 0;
	Forward(m_buffer);
    }
}

WpConsumer::WpConsumer(WpChan *owner, const char* format, unsigned int bufsize)
    : PriConsumer(owner,format,bufsize), Fifo(2*bufsize)
{
    Debug(m_owner,DebugAll,"WpConsumer::WpConsumer(%p) [%p]",owner,this);
    static_cast<WpChan*>(m_owner)->m_wp_c = this;
}

WpConsumer::~WpConsumer()
{
    Debug(m_owner,DebugAll,"WpConsumer::~WpConsumer() [%p]",this);
    static_cast<WpChan*>(m_owner)->m_wp_c = 0;
}

void WpConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    const unsigned char* buf = (const unsigned char*)data.data();
    for (unsigned int i = 0; i < data.length(); i++)
	put(buf[i]);
}

WpData::WpData(WpSpan* span, const char* card, const char* device)
    : Thread("WpData"), m_span(span), m_fd(INVALID_HANDLE_VALUE), m_buffer(0), m_chans(0)
{
    Debug(&__plugin,DebugAll,"WpData::WpData(%p) [%p]",span,this);
    HANDLE fd = wp_open(card,device);
    if (fd != INVALID_HANDLE_VALUE) {
	m_fd = fd;
	m_span->m_data = this;
    }
}

WpData::~WpData()
{
    Debug(&__plugin,DebugAll,"WpData::~WpData() [%p]",this);
    m_span->m_data = 0;
    wp_close(m_fd);
    m_fd = INVALID_HANDLE_VALUE;
    if (m_buffer)
	::free(m_buffer);
    if (m_chans)
	delete[] m_chans;
}

void WpData::run()
{
    Debug(&__plugin,DebugAll,"WpData::run() [%p]",this);
    int samp = 50;
    int bchans = m_span->bchans();
    int buflen = samp*bchans;
    int sz = buflen+WP_HEADER;
    m_buffer = (unsigned char*)::malloc(sz);
    // Build a compacted list of allocated B channels
    m_chans = new WpChan* [bchans];
    int b = 0;
    for (int n = 0; n < bchans; n++) {
	while (!m_span->m_chans[b])
	    b++;
	m_chans[n] = static_cast<WpChan*>(m_span->m_chans[b++]);
	DDebug(&__plugin,DebugInfo,"wpdata ch[%d]=%d (%p)",n,m_chans[n]->chan(),m_chans[n]);
    }
    while (m_span && (m_fd != INVALID_HANDLE_VALUE)) {
	Thread::check();
	bool oob = false;
	bool rd = wp_select(m_fd,samp,&oob);
	if (oob) {
	    DDebug("wpdata_recv_oob",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,buflen,sz);
	    int r = wp_recv(m_fd,m_buffer,sz,MSG_OOB);
	    DDebug("wpdata_recv_oob",DebugAll,"post r=%d",r);
	}

	if (rd) {
	    XDebug("wpdata_recv",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,buflen,sz);
	    int r = wp_recv(m_fd,m_buffer,sz,0/*MSG_NOSIGNAL*/);
	    XDebug("wpdata_recv",DebugAll,"post r=%d",r);
	    r -= WP_HEADER;
	    // We should have read N bytes for each B channel
	    if ((r > 0) && ((r % bchans) == 0)) {
		r /= bchans;
		const unsigned char* dat = m_buffer + WP_HEADER;
		m_span->lock();
		for (int n = r; n > 0; n--)
		    for (b = 0; b < bchans; b++) {
			WpSource *s = m_chans[b]->m_wp_s;
			if (s)
			    s->put(PriDriver::bitswap(*dat));
			dat++;
		    }
		m_span->unlock();
	    }
	    int w = samp;
	    ::memset(m_buffer,0,WP_HEADER);
	    unsigned char* dat = m_buffer + WP_HEADER;
	    m_span->lock();
	    for (int n = w; n > 0; n--) {
		for (b = 0; b < bchans; b++) {
		    WpConsumer *c = m_chans[b]->m_wp_c;
		    *dat++ = PriDriver::bitswap(c ? c->get() : 0xff);
		}
	    }
	    m_span->unlock();
	    w = (w * bchans) + WP_HEADER;
	    XDebug("wpdata_send",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,w,sz);
	    w = wp_send(m_fd,m_buffer,w,MSG_DONTWAIT);
	    XDebug("wpdata_send",DebugAll,"post w=%d",w);
	}
    }
}

WpChan::WpChan(const PriSpan *parent, int chan, unsigned int bufsize)
    : PriChan(parent,chan,bufsize), m_wp_s(0), m_wp_c(0)
{
}

WpChan::~WpChan()
{
    closeData();
}

bool WpChan::openData(const char* format, int echoTaps)
{
    if (echoTaps)
	Debug(DebugWarn,"Echo cancellation requested but not available in wanpipe");
    m_span->lock();
    setSource(new WpSource(this,format,m_bufsize));
    getSource()->deref();
    setConsumer(new WpConsumer(this,format,m_bufsize));
    getConsumer()->deref();
    m_span->unlock();
    return true;
}

PriSpan* WpDriver::createSpan(PriDriver* driver, int span, int first, int chans, Configuration& cfg, const String& sect)
{
    Debug(this,DebugAll,"WpDriver::createSpan(%p,%d,%d,%d) [%p]",driver,span,first,chans,this);
    int netType = -1;
    int swType = -1;
    int dchan = -1;
    netParams(cfg,sect,chans,&netType,&swType,&dchan);
    String card;
    card << "wanpipe" << span;
    card = cfg.getValue(sect,"card",card);
    String dev;
    dev = cfg.getValue(sect,"dgroup","if1");
    pri* p = ::pri_new_cb((int)INVALID_HANDLE_VALUE, netType, swType, wp_read, wp_write, 0);
    if (!p)
	return 0;
    WpSpan *ps = new WpSpan(p,driver,span,first,chans,dchan,cfg,sect);
    ps->startup();
    WpWriter* wr = new WpWriter(ps,card,dev);
    wr->startup();
    WpReader* rd = new WpReader(ps,card,dev);
    rd->startup();
    dev = cfg.getValue(sect,"bgroup","if0");
    WpData* dat = new WpData(ps,card,dev);
    dat->startup();
    return ps;
}

PriChan* WpDriver::createChan(const PriSpan* span, int chan, unsigned int bufsize)
{
    Debug(this,DebugAll,"WpDriver::createChan(%p,%d,%u) [%p]",span,chan,bufsize,this);
    return new WpChan(span,chan,bufsize);
}

WpDriver::WpDriver()
    : PriDriver("wp")
{
    Output("Loaded module Wanpipe");
}

WpDriver::~WpDriver()
{
    Output("Unloading module Wanpipe");
}

void WpDriver::initialize()
{
    Output("Initializing module Wanpipe");
    init("wpchan");
}

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
