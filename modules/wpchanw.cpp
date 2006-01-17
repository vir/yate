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
    virtual void cleanup();
    int dataRead(void *buf, int buflen);
    int dataWrite(void *buf, int buflen);

private:
    WpSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect);
    WpData* m_data;
    WpReader* m_reader;
    WpWriter* m_writer;
    DataBlock m_rdata;
    ObjList m_wdata;
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

    virtual void Consume(const DataBlock &data, unsigned long tStamp);
private:
    DataErrors m_overruns;
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
    WpData(WpSpan* span, const char* card, const char* device, Thread::Priority prio);
    ~WpData();
    virtual void run();
private:
    WpSpan* m_span;
    HANDLE m_fd;
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
    virtual bool received(Message &msg, int id);
    virtual PriSpan* createSpan(PriDriver* driver, int span, int first, int chans, Configuration& cfg, const String& sect);
    virtual PriChan* createChan(const PriSpan* span, int chan, unsigned int bufsize);
};

INIT_PLUGIN(WpDriver);

#define WP_HEADER 21
#define WP_BUFFER 8188 // maximum length of data = 8K - 4


static void dump_buffer(const void* buf, int len)
{
    String s;
    const unsigned char* p = (const unsigned char*)buf;
    for (int i=0; i < len; i++) {
	char tmp[4];
	sprintf(tmp," %02x",p[i]);
	//Debug(DebugAll,"%d",i);
	s += tmp;
    }
    Output("[%d@%p]%s",len,buf,s.c_str());
}

static int wp_recv(HANDLE fd, void *buf, int buflen, int flags = 0)
{
    int r = 0;
    if (!DeviceIoControl(fd,IoctlReadCommand,0,0,buf,buflen,(LPDWORD)&r,0)) {
	r = 0;
	Output("recv (%d,%p,%d) last err=%x",fd,buf,buflen,GetLastError());
    }
    return r;
}

static int wp_send(HANDLE fd, void *buf, int buflen, int flags = 0)
{
    int w = 0;
    if (!DeviceIoControl(fd,IoctlWriteCommand,buf,buflen,buf,buflen,(LPDWORD)&w,0)) {
	w = 0;
	Output("send (%d,%p,%d) last err=%x",fd,buf,buflen,GetLastError());
    }
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
	DDebug(&__plugin,DebugAll,"WpSpan dequeued %d bytes block [%p]",buflen,this);
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
    if (buf && (buflen > 2) && (m_wdata.length() < 5)) {
	buflen -= 2;
	DataBlock* block = new DataBlock(buf,buflen);
	m_wdata.append(block);
	DDebug(&__plugin,DebugAll,"WpSpan queued %d bytes block, total blocks %d [%p]",block->length(),m_wdata.count(),this);
	return buflen+2;
    }
    return 0;
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
    DDebug(&__plugin,DebugAll,"WpReader::WpReader(%p) [%p]",span,this);
    m_fd = wp_open(card,device);
    m_span->m_reader = this;
}

WpReader::~WpReader()
{
    DDebug(&__plugin,DebugAll,"WpReader::~WpReader() [%p]",this);
    if (m_span)
	m_span->m_reader = 0;
    HANDLE tmp = m_fd;
    m_fd = INVALID_HANDLE_VALUE;
    wp_close(tmp);
}

void WpReader::run()
{
    while (m_span && m_span->m_reader && (m_fd != INVALID_HANDLE_VALUE)) {
	Thread::msleep(1,true);
	Lock mylock(m_span);
	if (m_span->m_rdata.data())
	    continue;
	mylock.drop();
	unsigned char buf[WP_HEADER+WP_BUFFER];
	int r = wp_recv(m_fd,buf,sizeof(buf)) - WP_HEADER;
	XDebug(&__plugin,DebugAll,"WpReader read returned %d [%p]",r,this);
	if (r <= 0)
	    continue;
	Thread::check();
	m_span->lock();
	m_span->m_rdata.assign(buf+WP_HEADER,r);
	DDebug(&__plugin,DebugAll,"WpReader queued %d bytes block [%p]",r,this);
	m_span->unlock();
    }
}

WpWriter::WpWriter(WpSpan* span, const char* card, const char* device)
    : Thread("WpWriter"), m_span(span), m_fd(INVALID_HANDLE_VALUE)
{
    DDebug(&__plugin,DebugAll,"WpWriter::WpWriter(%p) [%p]",span,this);
    m_fd = wp_open(card,device);
    m_span->m_writer = this;
}

WpWriter::~WpWriter()
{
    DDebug(&__plugin,DebugAll,"WpWriter::~WpWriter() [%p]",this);
    if (m_span)
	m_span->m_writer = 0;
    HANDLE tmp = m_fd;
    m_fd = INVALID_HANDLE_VALUE;
    wp_close(tmp);
}

void WpWriter::run()
{
    while (m_span && m_span->m_writer && (m_fd != INVALID_HANDLE_VALUE)) {
	Thread::msleep(1,true);
	m_span->lock();
	DataBlock *block = static_cast<DataBlock*>(m_span->m_wdata.remove(false));
	m_span->unlock();
	if (!block)
	    continue;
	DDebug(&__plugin,DebugAll,"WpWriter dequeued %d bytes block [%p]",block->length(),this);
	// this is really stupid - have to send a huge buffer, or else
	// Error : Tx system buffer length not equal sizeof(TX_DATA_STRUCT)!
	unsigned char buf[WP_HEADER+WP_BUFFER];
	int len = block->length();
	::memcpy(buf+WP_HEADER,block->data(),len);
	block->destruct();
	buf[0] = 11;
	buf[1] = len & 0xff;
	buf[2] = len >> 8;
	wp_send(m_fd,buf,sizeof(buf));
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
}

void WpSpan::cleanup()
{
    Debug(&__plugin,DebugAll,"WpSpan::cleanup() [%p]",this);
    m_ok = false;
    if (m_data)
	m_data->cancel();
    if (m_reader)
	m_reader->cancel();
    if (m_writer)
	m_writer->cancel();
    Debug(&__plugin,DebugAll,"WpSpan waiting for cleanups [%p]",this);
    Thread::msleep(20);
    while (m_data || m_reader || m_writer)
	Thread::msleep(1);
    Debug(&__plugin,DebugAll,"WpSpan cleanups complete [%p]",this);
}

void WpSpan::run()
{
    Debug(&__plugin,DebugAll,"WpSpan::run() [%p]",this);
    while (m_data && m_reader && m_writer) {
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
    if (m_overruns.events())
	Debug(m_owner,DebugMild,"Consumer had %u overruns (%lu bytes)",
	    m_overruns.events(),m_overruns.bytes());
}

void WpConsumer::Consume(const DataBlock &data, unsigned long tStamp)
{
    unsigned int err = put((const unsigned char*)data.data(),data.length());
    if (err)
	m_overruns.update(err);
}



WpData::WpData(WpSpan* span, const char* card, const char* device, Thread::Priority prio)
    : Thread("WpData",prio), m_span(span), m_fd(INVALID_HANDLE_VALUE), m_chans(0)
{
    DDebug(&__plugin,DebugAll,"WpData::WpData(%p) [%p]",span,this);
    HANDLE fd = wp_open(card,device);
    if (fd != INVALID_HANDLE_VALUE) {
	m_fd = fd;
	m_span->m_data = this;
    }
}

WpData::~WpData()
{
    DDebug(&__plugin,DebugAll,"WpData::~WpData() [%p]",this);
    if (m_span)
	m_span->m_data = 0;
    wp_close(m_fd);
    m_fd = INVALID_HANDLE_VALUE;
    if (m_chans)
	delete[] m_chans;
}

void WpData::run()
{
    DDebug(&__plugin,DebugAll,"WpData::run() [%p]",this);
    unsigned char buffer[WP_HEADER+WP_BUFFER];
    int bchans = m_span->bchans();
    // Build a compacted list of allocated B channels
    m_chans = new WpChan* [bchans];
    int b = 0;
    for (int n = 0; n < bchans; n++) {
	while (!m_span->m_chans[b])
	    b++;
	m_chans[n] = static_cast<WpChan*>(m_span->m_chans[b++]);
	DDebug(&__plugin,DebugInfo,"wpdata ch[%d]=%d (%p)",n,m_chans[n]->chan(),m_chans[n]);
    }
    int rok = 0, rerr = 0;
    int wok = 0, werr = 0;
    while (m_span && m_span->m_data && (m_fd != INVALID_HANDLE_VALUE)) {
	Thread::check();
	int samp = 0;
	int r = wp_recv(m_fd,buffer,sizeof(buffer),0/*MSG_NOSIGNAL*/);
	XDebug(&__plugin,DebugAll,"WpData recv r=%d",r);
	r -= WP_HEADER;
	// We should have read N bytes for each B channel
	if (r > 0) {
	    samp = r / bchans;
	    if ((r % bchans) == 0) {
		const unsigned char* dat = buffer + WP_HEADER;
		m_span->lock();
		int p1 = -1;
		int p2 = -1;
		for (int n = samp; n > 0; n--) {
		    for (b = 0; b < bchans; b++) {
			WpSource *s = m_chans[b]->m_wp_s;
			if (s)
			    s->put(PriDriver::bitswap(*dat));
			dat++;
		    }
		}
		m_span->unlock();
		++rok;
	    }
	    else
		Debug(DebugWarn,"WpData read %d (ok/bad %d/%d)",r,rok,++rerr);
	}
	if (samp) {
	    ::memset(buffer,0,WP_HEADER);
	    unsigned char* dat = buffer + WP_HEADER;
	    m_span->lock();
	    for (int n = samp; n > 0; n--) {
		for (b = 0; b < bchans; b++) {
		    WpConsumer *c = m_chans[b]->m_wp_c;
		    *dat++ = PriDriver::bitswap(c ? c->get() : 0xff);
		}
	    }
	    m_span->unlock();
	    int w = samp * bchans;
	    dat = buffer;
	    buffer[0] = 11;
	    buffer[1] = w & 0xff;
	    buffer[2] = w >> 8;
	    w = wp_send(m_fd,buffer,sizeof(buffer),MSG_DONTWAIT);
	    if (w != sizeof(buffer))
		Debug(DebugWarn,"WpData wrote %d (ok/bad %d/%d)",w,wok,++werr);
	    else
		++wok;
	    XDebug(&__plugin,DebugAll,"WpData send w=%d",w);
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
    Debug(this,DebugAll,"WpChan::openData(%s,%d) [%p]",format,echoTaps,this);
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

static Thread::Priority cfgPriority(Configuration& cfg, const String& sect)
{
    String tmp(cfg.getValue(sect,"thread"));
    if (tmp.null())
	tmp = cfg.getValue("general","thread");
    return Thread::priority(tmp);
}
		    

PriSpan* WpDriver::createSpan(PriDriver* driver, int span, int first, int chans, Configuration& cfg, const String& sect)
{
    Debug(this,DebugAll,"WpDriver::createSpan(%p,%d,%d,%d) [%p]",driver,span,first,chans,this);
    int netType = -1;
    int swType = -1;
    int dchan = -1;
    netParams(cfg,sect,chans,&netType,&swType,&dchan);
    String card;
    card << "WANPIPE" << span;
    card = cfg.getValue(sect,"card",card);
    String dev;
    dev = cfg.getValue(sect,"dgroup","IF0");
    pri* p = ::pri_new_cb((int)INVALID_HANDLE_VALUE, netType, swType, wp_read, wp_write, 0);
    if (!p)
	return 0;
    WpSpan *ps = new WpSpan(p,driver,span,first,chans,dchan,cfg,sect);
    WpWriter* wr = new WpWriter(ps,card,dev);
    WpReader* rd = new WpReader(ps,card,dev);
    dev = cfg.getValue(sect,"bgroup","IF1");
    WpData* dat = new WpData(ps,card,dev,cfgPriority(cfg,sect));
    wr->startup();
    rd->startup();
    dat->startup();
    ps->startup();
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
    installRelay(Halt,110);
}

bool WpDriver::received(Message &msg, int id)
{
    bool ok = PriDriver::received(msg,id);
    if (id == Halt) {
	Debug(this,DebugAll,"WpDriver clearing all spans [%p]",this);
	lock();
	const ObjList *l = &m_spans;
	for (; l; l=l->next()) {
	    WpSpan *s = static_cast<WpSpan*>(l->get());
	    if (s)
		s->cancel();
	}
	unlock();
	Debug(this,DebugAll,"WpDriver waiting for spans to exit [%p]",this);
	while (m_spans.get())
	    Thread::msleep(10);
    }
    return ok;
}


#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
