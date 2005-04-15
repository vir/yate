/**
 * wpchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wanpipe PRI cards telephony driver
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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

extern "C" {

#include <libpri.h>

#ifndef _WINDOWS
#define __LINUX__
#include <linux/if_wanpipe.h>
#include <linux/if.h>
#include <linux/wanpipe.h>
#include <linux/sdla_bitstrm.h>
#endif

};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WINDOWS
#include <sys/ioctl.h>
#include <fcntl.h>
#endif


using namespace TelEngine;

/* Layer 1 formats */
static TokenDict dict_str2law[] = {
    { "mulaw", PRI_LAYER_1_ULAW },
    { "alaw", PRI_LAYER_1_ALAW },
    { "g721", PRI_LAYER_1_G721 },
    { 0, -1 }
};

class WpChan;

class WpSpan : public PriSpan, public Thread
{
    friend class WpData;
public:
    virtual ~WpSpan();
    virtual void run();

private:
    WpSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, Configuration* cfg, const String& sect, int fd);
    static struct pri *makePri(int fd, int dchan, int nettype, int swtype, int overlapDial, int nsf);
    int m_fd;
    WpData *m_data;
};

class WpSource : public PriSource
{
public:
    WpSource(WpChan *owner,unsigned int bufsize,const char* format);
    ~WpSource();
    void put(unsigned char val);

private:
    unsigned int m_bufpos;
};

class WpConsumer : public PriConsumer, public Fifo
{
public:
    WpConsumer(WpChan *owner,unsigned int bufsize,const char* format);
    ~WpConsumer();

    virtual void Consume(const DataBlock &data, unsigned long timeDelta);
};

class WpChan : public PriChan
{
    friend class WpSource;
    friend class WpConsumer;
    friend class WpData;
public:
    WpChan(PriSpan *parent, int chan, unsigned int bufsize);
    virtual ~WpChan();
    virtual void disconnected(bool final, const char *reason);
    virtual bool nativeConnect(DataEndpoint *peer);
    bool openData(const char* format);

private:
    WpSource* m_wp_s;
    WpConsumer* m_wp_c;
};

class WpData : public Thread
{
public:
    WpData(WpSpan* span);
    ~WpData();
    virtual void run();
private:
    WpSpan* m_span;
    int m_fd;
    unsigned char* m_buffer;
    WpChan **m_chans;
};

class WpDriver : public PriDriver
{
    friend class PriSpan;
    friend class WpHandler;
public:
    WpDriver();
    virtual ~WpDriver();
    virtual void initialize();
    virtual bool create(PriDriver* driver, int span, int first, int chans, Configuration* cfg, const String& sect);
};

WpDriver wdriver;

#define WP_HEADER 16

static int wp_read(struct pri *pri, void *buf, int buflen)
{
    buflen -= 2;
    int sz = buflen+WP_HEADER;
    char *tmp = (char*)::calloc(sz,1);
    XDebug("wp_read",DebugAll,"pre buf=%p len=%d tmp=%p sz=%d",buf,buflen,tmp,sz);
    int r = ::recv(::pri_fd(pri),tmp,sz,MSG_NOSIGNAL);
    XDebug("wp_read",DebugAll,"post r=%d",r);
    if (r > 0) {
	r -= WP_HEADER;
	if ((r > 0) && (r <= buflen)) {
	    ::memcpy(buf,tmp+WP_HEADER,r);
	    r += 2;
	}
    }
    ::free(tmp);
    return r;
}

static int wp_write(struct pri *pri, void *buf, int buflen)
{
    buflen -= 2;
    int sz = buflen+WP_HEADER;
    char *tmp = (char*)::calloc(sz,1);
    ::memcpy(tmp+WP_HEADER,buf,buflen);
    XDebug("wp_write",DebugAll,"pre buf=%p len=%d tmp=%p sz=%d",buf,buflen,tmp,sz);
    int w = ::send(::pri_fd(pri),tmp,sz,0);
    XDebug("wp_write",DebugAll,"post w=%d",w);
    if (w > 0) {
	w -= WP_HEADER;
	w += 2;
    }
    ::free(tmp);
    return w;
}

bool WpDriver::create(PriDriver* driver, int span, int first, int chans, Configuration* cfg, const String& sect)
{
}


virtual PriChan* WpSpan::create(int chan)
{
    return new WpChan(this,chan,m_bufsize)
}

PriSpan *PriSpan::create(int span, int chan1, int nChans, int dChan, int netType,
			 int switchType, int dialPlan, int presentation,
			 int overlapDial, int nsf)
{
    int fd = ::socket(AF_WANPIPE, SOCK_RAW, 0);
    if (fd < 0)
	return 0;
    struct pri *p = makePri(fd,
	(dChan >= 0) ? dChan+chan1-1 : -1,
	netType,
	switchType, overlapDial, nsf);
    if (!p) {
	::close(fd);
	return 0;
    }
    WpSpan *ps = new WpSpan(p,span,chan1,nChans,dChan,fd);
    ps->startup();
    return ps;
}

struct pri *PriSpan::makePri(int fd, int dchan, int nettype, int swtype)
{
    if (dchan >= 0) {
	// Set up the D channel if we have one
	struct wan_sockaddr_ll sa;
	memset(&sa,0,sizeof(struct wan_sockaddr_ll));
	::strcpy( (char*)sa.sll_device, "w1g2");
	::strcpy( (char*)sa.sll_card, "wanpipe1");
	sa.sll_protocol = htons(PVC_PROT);
	sa.sll_family=AF_WANPIPE;
	if (::bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	    Debug("PriSpan",DebugGoOn,"Failed to bind %d: error %d: %s",
		fd,errno,::strerror(errno));
	    return 0;
	}
    }
    return ::pri_new_cb(fd, nettype, swtype, wp_read, wp_write, 0);
}

WpSpan::WpSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, Configuration* cfg, const String& sect, int fd)
    : PriSpan(_pri,driver,span,first,chans,cfg,sect), Thread("WpSpan"),
      m_fd(fd), m_data(0)
{
    Debug(DebugAll,"PriSpan::PriSpan() [%p]",this);
    WpData* dat = new WpData(this);
    dat->startup();
}

WpSpan::~WpSpan()
{
    Debug(DebugAll,"WpSpan::~WpSpan() [%p]",this);
    m_ok = false;
    delete m_data;
    ::close(m_fd);
    m_fd = -1;
}

void WpSpan::run()
{
    Debug(DebugAll,"PriSpan::run() [%p]",this);
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
	pri_event *ev = 0;
	if (!sel)
	    runEvent(true);
	else if (sel > 0)
	    runEvent(false);
	else if (errno != EINTR)
	    Debug("WpSpan",DebugGoOn,"select() error %d: %s",
		errno,::strerror(errno));
    }
}

WpSource::WpSource(WpChan *owner,unsigned int bufsize,const char* format)
    : PriSource(owner,bufsize),
      m_bufpos(0)
{
    Debug(DebugAll,"WpSource::WpSource(%p) [%p]",owner,this);
    static_cast<WpChan*>(m_owner)->m_wp_s = this;
}

WpSource::~WpSource()
{
    Debug(DebugAll,"WpSource::~WpSource() [%p]",this);
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

WpConsumer::WpConsumer(WpChan *owner,unsigned int bufsize,const char* format)
    : PriConsumer(owner,bufsize), Fifo(2*bufsize)
{
    Debug(DebugAll,"WpConsumer::WpConsumer(%p) [%p]",owner,this);
    static_cast<WpChan*>(m_owner)->m_wp_c = this;
}

WpConsumer::~WpConsumer()
{
    Debug(DebugAll,"WpConsumer::~WpConsumer() [%p]",this);
    static_cast<WpChan*>(m_owner)->m_wp_c = 0;
}

void WpConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    const unsigned char* buf = (const unsigned char*)data.data();
    for (unsigned int i = 0; i < data.length(); i++)
	put(buf[i]);
}

WpData::WpData(WpSpan* span)
    : Thread("WpData"), m_span(span), m_fd(-1), m_buffer(0), m_chans(0)
{
    Debug(DebugAll,"WpData::WpData(%p) [%p]",span,this);
    int fd = ::socket(AF_WANPIPE, SOCK_RAW, 0);
    if (fd >= 0) {
	// Set up the B channel group
	struct wan_sockaddr_ll sa;
	memset(&sa,0,sizeof(struct wan_sockaddr_ll));
	::strcpy( (char*)sa.sll_device, "w1g1");
	::strcpy( (char*)sa.sll_card, "wanpipe1");
	sa.sll_protocol = htons(PVC_PROT);
	sa.sll_family=AF_WANPIPE;
	if (::bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	    Debug("PriSpan",DebugGoOn,"Failed to bind %d: error %d: %s",
		fd,errno,::strerror(errno));
	    ::close(fd);
	}
	else {
	    m_fd = fd;
	    m_span->m_data = this;
	}
    }
}

WpData::~WpData()
{
    Debug(DebugAll,"WpData::~WpData() [%p]",this);
    m_span->m_data = 0;
    if (m_fd >= 0)
	::close(m_fd);
    if (m_buffer)
	::free(m_buffer);
    if (m_chans)
	delete[] m_chans;
}

void WpData::run()
{
    Debug(DebugAll,"WpData::run() [%p]",this);
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
	m_chans[n] = m_span->m_chans[b++];
	DDebug("wpdata_chans",DebugInfo,"ch[%d]=%d (%p)",n,m_chans[n]->chan(),m_chans[n]);
    }
    fd_set rdfds,oobfds;
    while (m_span && (m_fd >= 0)) {
	FD_ZERO(&rdfds);
	FD_ZERO(&oobfds);
	FD_SET(m_fd, &rdfds);
	FD_SET(m_fd, &oobfds);
	if (::select(m_fd+1, &rdfds, NULL, &oobfds, NULL) <= 0)
	    continue;

	if (FD_ISSET(m_fd, &oobfds)) {
	    DDebug("wpdata_recv_oob",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,buflen,sz);
	    int r = ::recv(m_fd,m_buffer,sz,MSG_OOB);
	    DDebug("wpdata_recv_oob",DebugAll,"post r=%d",r);
	}

	if (FD_ISSET(m_fd, &rdfds)) {
	    XDebug("wpdata_recv",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,buflen,sz);
	    int r = ::recv(m_fd,m_buffer,sz,0/*MSG_NOSIGNAL*/);
	    XDebug("wpdata_recv",DebugAll,"post r=%d",r);
	    r -= WP_HEADER;
	    // We should have read N bytes for each B channel
	    if ((r > 0) && ((r % bchans) == 0)) {
		r /= bchans;
		const unsigned char* dat = m_buffer + WP_HEADER;
		wplugin.mutex.lock();
		for (int n = r; n > 0; n--)
		    for (b = 0; b < bchans; b++) {
			WpSource *s = m_chans[b]->m_wp_s;
			if (s)
			    s->put(PriDriver::bitswap(*dat));
			dat++;
		    }
		wplugin.mutex.unlock();
	    }
	    int w = samp;
	    ::memset(m_buffer,0,WP_HEADER);
	    unsigned char* dat = m_buffer + WP_HEADER;
	    wplugin.mutex.lock();
	    for (int n = w; n > 0; n--) {
		for (b = 0; b < bchans; b++) {
		    WpConsumer *c = m_chans[b]->m_wp_c;
		    *dat++ = PriDriver::bitswap(c ? c->get() : 0xff);
		}
	    }
	    wplugin.mutex.unlock();
	    w = (w * bchans) + WP_HEADER;
	    XDebug("wpdata_send",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,w,sz);
	    w = ::send(m_fd,m_buffer,w,MSG_DONTWAIT);
	    XDebug("wpdata_send",DebugAll,"post w=%d",w);
	}
    }
}

bool WpChan::openData(const char* format)
{
    setSource(new WpSource(this,m_bufsize,format));
    getSource()->deref();
    setConsumer(new WpConsumer(this,m_bufsize,format));
    getConsumer()->deref();
    return true;
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

/* vi: set ts=8 sw=4 sts=4 noet: */
