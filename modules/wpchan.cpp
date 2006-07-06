/**
 * wpchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wanpipe PRI cards telephony driver
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

#define INVALID_HANDLE_VALUE (-1)
#define __LINUX__
#include <linux/if_wanpipe.h>
#include <linux/if.h>
#include <linux/wanpipe_defines.h>
#include <linux/wanpipe_cfg.h>
#include <linux/wanpipe.h>
#include <linux/sdla_aft_te1.h>

#ifdef HAVE_WANPIPE_HWEC
#include <wanec_iface.h>
#endif

};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <fcntl.h>

using namespace TelEngine;
namespace { // anonymous

class WpChan;
class WpData;
class WpDriver;

class WpSpan : public PriSpan, public Thread
{
    friend class WpData;
    friend class WpDriver;
public:
    virtual ~WpSpan();
    virtual void run();
    inline int overRead() const
	{ return m_overRead; }
    unsigned long bChanMap() const;

private:
    WpSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect, HANDLE fd);
    HANDLE m_fd;
    WpData *m_data;
    int m_overRead;
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
    WpData(WpSpan* span, const char* card, const char* device, Configuration& cfg, const String& sect);
    ~WpData();
    virtual void run();
private:
    bool decodeEvent(const api_rx_hdr_t* ev);
    WpSpan* m_span;
    HANDLE m_fd;
    unsigned char* m_buffer;
    WpChan **m_chans;
    int m_samples;
    bool m_swap;
    unsigned char m_rdError;
    unsigned char m_wrError;
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
#define MAX_DATA_ERRORS 250

static int wp_recv(HANDLE fd, void *buf, int buflen, int flags = 0)
{
    int r = ::recv(fd,buf,buflen,flags);
    return r;
}

static int wp_send(HANDLE fd, void *buf, int buflen, int flags = 0)
{
    int w = ::send(fd,buf,buflen,flags);
    return w;
}

static int wp_read(struct pri *pri, void *buf, int buflen)
{
    buflen -= 2;
    int sz = buflen+WP_HEADER;
    char *tmp = (char*)::calloc(sz,1);
    XDebug("wp_read",DebugAll,"pre buf=%p len=%d tmp=%p sz=%d",buf,buflen,tmp,sz);
    int r = wp_recv((HANDLE)::pri_fd(pri),tmp,sz,MSG_NOSIGNAL);
    XDebug("wp_read",DebugAll,"post r=%d",r);
    if (r > 0) {
	r -= WP_HEADER;
	if ((r > 0) && (r <= buflen)) {
	    WpSpan* span = (WpSpan*)::pri_get_userdata(pri);
	    if (span)
		r -= span->overRead();
	    DDebug("wp_read",DebugAll,"Transferring %d for %p",r,pri);
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
    int w = wp_send((HANDLE)::pri_fd(pri),tmp,sz,0);
    XDebug("wp_write",DebugAll,"post w=%d",w);
    if (w > 0) {
	w -= WP_HEADER;
	DDebug("wp_write",DebugAll,"Transferred %d for %p",w,pri);
	w += 2;
    }
    ::free(tmp);
    return w;
}

static bool wp_select(HANDLE fd,int samp,bool* errp = 0)
{
    fd_set rdfds;
    fd_set errfds;
    FD_ZERO(&rdfds);
    FD_SET(fd,&rdfds);
    FD_ZERO(&errfds);
    FD_SET(fd,&errfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = samp*125;
    int sel = ::select(fd+1, &rdfds, NULL, errp ? &errfds : NULL, &tv);
    if (sel < 0)
	Debug(DebugWarn,"Wanpipe select failed on %d: error %d: %s",
	    fd,errno,::strerror(errno));
    if (errp)
	*errp = FD_ISSET(fd,&errfds);
    return FD_ISSET(fd,&rdfds);
}

#ifdef HAVE_WANPIPE_HWEC

static bool wp_hwec_ioctl(wan_ec_api_t* ecapi)
{
    if (!ecapi)
	return false;
    int fd = -1;
    for (int i = 0; i < 5; i++) {
	fd = open(WANEC_DEV_DIR WANEC_DEV_NAME, O_RDONLY);
	if (fd >= 0)
	    break;
	Thread::msleep(200);
    }
    if (fd < 0)
	return false;
    ecapi->err = WAN_EC_API_RC_OK;
    if (::ioctl(fd,ecapi->cmd,ecapi)) {
	// preserve errno while we close the handle
	int err = errno;
	::close(fd);
	errno = err;
	return false;
    }
    ::close(fd);
    return true;
}

// Configure the DTMF detection of the DSP
static bool wp_dtmf_config(const char* card, const char* device, bool enable, unsigned long chanmap)
{
    wan_ec_api_t ecapi;
    ::memset(&ecapi,0,sizeof(ecapi));
    ::strncpy((char*)ecapi.devname,card,sizeof(ecapi.devname));
    ::strncpy((char*)ecapi.if_name,device,sizeof(ecapi.if_name));
    ecapi.channel_map = chanmap;
    if (enable) {
	ecapi.cmd = WAN_EC_CMD_DTMF_ENABLE;
	// event on start of tone, before echo canceller
	ecapi.u_dtmf_config.type = WP_API_EVENT_DTMF_PRESENT | WP_API_EVENT_DTMF_SOUT;
    }
    else
	ecapi.cmd = WAN_EC_CMD_DTMF_DISABLE;
    return wp_hwec_ioctl(&ecapi);
}

#else

static bool wp_dtmf_config(const char* card, const char* device, bool enable, unsigned long chanmap)
{
    return false;
}

#endif // HAVE_WANPIPE_HWEC

// Enable/disable DTMF events
static bool wp_dtmfs(HANDLE fd, bool detect)
{
#ifdef HAVE_WANPIPE_HWEC
    api_tx_hdr_t api_tx_hdr;
    ::memset(&api_tx_hdr,0,sizeof(api_tx_hdr_t));
    api_tx_hdr.wp_api_tx_hdr_event_type = WP_API_EVENT_DTMF;
    api_tx_hdr.wp_api_tx_hdr_event_mode = detect ? WP_API_EVENT_ENABLE : WP_API_EVENT_DISABLE;
    return (::ioctl(fd,SIOC_WANPIPE_API,&api_tx_hdr) >= 0);
#else
    // pretend enabling fails, disabling succeeds
    if (!detect)
	return true;
    errno = ENOSYS;
    return false;
#endif
}

void wp_close(HANDLE fd)
{
    if (fd == INVALID_HANDLE_VALUE)
	return;
    ::close(fd);
}

static HANDLE wp_open(const char* card, const char* device)
{
    DDebug(DebugAll,"wp_open('%s','%s')",card,device);
    if (null(card) || null(device))
	return INVALID_HANDLE_VALUE;
    HANDLE fd = ::socket(AF_WANPIPE, SOCK_RAW, 0);
    if (fd == INVALID_HANDLE_VALUE) {
	Debug(DebugGoOn,"Wanpipe failed to create socket: error %d: %s",
	    errno,::strerror(errno));
	return fd;
    }
    // Bind to the card/interface
    struct wan_sockaddr_ll sa;
    memset(&sa,0,sizeof(struct wan_sockaddr_ll));
    ::strncpy((char*)sa.sll_device,device,sizeof(sa.sll_device));
    ::strncpy((char*)sa.sll_card,card,sizeof(sa.sll_card));
    sa.sll_protocol = htons(PVC_PROT);
    sa.sll_family=AF_WANPIPE;
    if (::bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	Debug(DebugGoOn,"Wanpipe failed to bind %d: error %d: %s",
	    fd,errno,::strerror(errno));
	wp_close(fd);
	fd = INVALID_HANDLE_VALUE;
    }
    return fd;
}

static struct pri* wp_create(const char* card, const char* device, int nettype, int swtype)
{
    DDebug(DebugAll,"wp_create('%s','%s',%d,%d)",card,device,nettype,swtype);
    HANDLE fd = wp_open(card,device);
    if (fd == INVALID_HANDLE_VALUE)
	return 0;
    struct pri* p = ::pri_new_cb((int)fd, nettype, swtype, wp_read, wp_write, 0);
    if (!p)
	wp_close(fd);
    return p;
}

WpSpan::WpSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect, HANDLE fd)
    : PriSpan(_pri,driver,span,first,chans,dchan,cfg,sect), Thread("WpSpan"),
      m_fd(fd), m_data(0), m_overRead(0)
{
    Debug(&__plugin,DebugAll,"WpSpan::WpSpan() [%p]",this);
    m_overRead = cfg.getIntValue(sect,"overread",cfg.getIntValue("general","overread",0));
}

WpSpan::~WpSpan()
{
    Debug(&__plugin,DebugAll,"WpSpan::~WpSpan() [%p]",this);
    m_ok = false;
    delete m_data;
    wp_close(m_fd);
    m_fd = INVALID_HANDLE_VALUE;
}

void WpSpan::run()
{
    Debug(&__plugin,DebugAll,"WpSpan::run() [%p]",this);
    for (;;) {
	bool rd = wp_select(m_fd,5); // 5 bytes per smallest q921 frame
	Thread::check();
	runEvent(!rd);
    }
}

unsigned long WpSpan::bChanMap() const
{
    unsigned long res = 0;
    for (int i = 1; i <= 31; i++)
	if (validChan(i))
	    res |= ((unsigned long)1 << i);
    return res;
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


static Thread::Priority cfgPriority(Configuration& cfg, const String& sect)
{
    String tmp(cfg.getValue(sect,"thread"));
    if (tmp.null())
	tmp = cfg.getValue("general","thread");
    return Thread::priority(tmp);
}


WpData::WpData(WpSpan* span, const char* card, const char* device, Configuration& cfg, const String& sect)
    : Thread("WpData",cfgPriority(cfg,sect)), m_span(span), m_fd(INVALID_HANDLE_VALUE),
      m_buffer(0), m_chans(0), m_samples(50), m_swap(true), m_rdError(0), m_wrError(0)
{
    Debug(&__plugin,DebugAll,"WpData::WpData(%p,'%s','%s') [%p]",
	span,card,device,this);
    HANDLE fd = wp_open(card,device);
    if (fd != INVALID_HANDLE_VALUE) {
	m_fd = fd;
	m_span->m_data = this;
	bool detect = m_span->detect();
	if (!(wp_dtmf_config(card,device,detect,m_span->bChanMap()) && wp_dtmfs(fd,detect))) {
	    int err = errno;
	    Debug(&__plugin,detect ? DebugWarn : DebugMild,
		"Failed to %s DTMF detection on span %d: %s (%d)",
		detect ? "enable" : "disable",m_span->span(),strerror(err),err);
	}
    }

    if (m_span->chans() == 24)
	// for T1 we typically have 23 B channels so we adjust the number
	//  of samples to get multiple of 32 bit and also reduce overhead
	m_samples = 64;

    m_samples = cfg.getIntValue("general","samples",m_samples);
    m_samples = cfg.getIntValue(sect,"samples",m_samples);
    m_swap = cfg.getBoolValue("general","bitswap",m_swap);
    m_swap = cfg.getBoolValue(sect,"bitswap",m_swap);
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
    int bchans = m_span->bchans();
    int buflen = m_samples*bchans;
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
    while (m_span && (m_fd >= 0)) {
	Thread::check();
	api_rx_hdr_t* ev = (api_rx_hdr_t*)m_buffer;
	bool oob = false;
	bool rd = wp_select(m_fd,m_samples,&oob);
	if (oob) {
	    XDebug("wpdata_recv_oob",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,buflen,sz);
	    int r = wp_recv(m_fd,m_buffer,sz,MSG_OOB);
	    XDebug("wpdata_recv_oob",DebugAll,"post r=%d",r);
	    if (r < 0) {
#ifdef SIOC_WANPIPE_SOCK_STATE
		r = ::ioctl(m_fd,SIOC_WANPIPE_SOCK_STATE,0);
		Debug(&__plugin,DebugNote,"Socket for span %d is %s",
		    m_span->span(),
		    (r == 0) ? "connected" : ((r == 1) ? "disconnected" : "connecting"));
#else
		Debug(&__plugin,DebugNote,"Failed to read OOB on span %d",
		    m_span->span());
#endif
	    }
	    else if (r >= WP_HEADER)
		decodeEvent(ev);
	}

	if (rd) {
	    ev->error_flag = 0;
	    XDebug("wpdata_recv",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,buflen,sz);
	    int r = wp_recv(m_fd,m_buffer,sz,0/*MSG_NOSIGNAL*/);
	    XDebug("wpdata_recv",DebugAll,"post r=%d",r);
	    r -= WP_HEADER;
	    if (ev->error_flag) {
		if (!m_rdError)
		    Debug(&__plugin,DebugWarn,"Read data error 0x%02X on span %d [%p]",
			ev->error_flag,m_span->span(),this);
		if (m_rdError < MAX_DATA_ERRORS)
		    m_rdError++;
	    }
	    else
		m_rdError = 0;

	    // check if a DTMF was received with the data
	    if (r >= 0)
		decodeEvent(ev);

	    // We should have read N bytes for each B channel
	    if ((r > 0) && ((r % bchans) == 0)) {
		r /= bchans;
		const unsigned char* dat = m_buffer + WP_HEADER;
		m_span->lock();
		for (int n = r; n > 0; n--)
		    for (b = 0; b < bchans; b++) {
			WpSource *s = m_chans[b]->m_wp_s;
			if (s)
			    s->put(m_swap ? PriDriver::bitswap(*dat) : *dat);
			dat++;
		    }
		m_span->unlock();
	    }
	    int wr = m_samples;
	    ::memset(m_buffer,0,WP_HEADER);
	    unsigned char* dat = m_buffer + WP_HEADER;
	    m_span->lock();
	    for (int n = wr; n > 0; n--) {
		for (b = 0; b < bchans; b++) {
		    WpConsumer *c = m_chans[b]->m_wp_c;
		    unsigned char d = c ? c->get() : 0xff;
		    *dat++ = m_swap ? PriDriver::bitswap(d) : d;
		}
	    }
	    m_span->unlock();
	    wr = (wr * bchans) + WP_HEADER;
	    XDebug("wpdata_send",DebugAll,"pre buf=%p len=%d sz=%d",m_buffer,wr,sz);
	    int w = wp_send(m_fd,m_buffer,wr,MSG_DONTWAIT);
	    XDebug("wpdata_send",DebugAll,"post w=%d",w);
	    if (w != wr) {
		if (!m_wrError)
		    Debug(&__plugin,DebugWarn,"Wrote %d data bytes instead of %d on span %d [%p]",
			w,wr,m_span->span(),this);
		if (m_wrError < MAX_DATA_ERRORS)
		    m_wrError++;
	    }
	    else
		m_wrError = 0;
	}
    }
}

bool WpData::decodeEvent(const api_rx_hdr_t* ev)
{
#ifdef WP_API_EVENT_DTMF_PRESENT
    switch (ev->event_type) {
	case WP_API_EVENT_NONE:
	    return false;
	case WP_API_EVENT_DTMF:
	    if (ev->hdr_u.wp_api_event.u_event.dtmf.type & WP_API_EVENT_DTMF_PRESENT) {
		String tone((char)ev->hdr_u.wp_api_event.u_event.dtmf.digit);
		tone.toUpper();
		int chan = ev->hdr_u.wp_api_event.channel;
		PriChan* c = m_span->getChan(chan);
		if (c)
		    c->gotDigits(tone);
		else
		    Debug(&__plugin,DebugMild,"Detected DTMF '%s' for invalid channel %d on span %d",
			tone.c_str(),chan,m_span->span());
	    }
	    break;
	default:
	    Debug(&__plugin,DebugMild,"Unhandled event %u on span %d",
		ev->event_type,m_span->span());
	    break;
    }
    return true;
#else
    return false;
#endif
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
    dev << "w" << span << "g1";
    pri* p = wp_create(card,cfg.getValue(sect,"dgroup",dev),netType,swType);
    if (!p)
	return 0;
    WpSpan *ps = new WpSpan(p,driver,span,first,chans,dchan,cfg,sect,(HANDLE)::pri_fd(p));
    ps->startup();
    dev.clear();
    dev << "w" << span << "g2";
    WpData* dat = new WpData(ps,card,cfg.getValue(sect,"bgroup",dev),cfg,sect);
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

}; // anonymous namespace

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
