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

extern "C" {
#include <libpri.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define __LINUX__
#include <linux/if_wanpipe.h>
#include <linux/if.h>
#include <linux/wanpipe.h>
#include <linux/sdla_bitstrm.h>

extern int q931_setup(struct pri *pri, q931_call *c, struct pri_sr *req);
};
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
		     
#include <telengine.h>
#include <telephony.h>
#include <stdio.h>


using namespace TelEngine;

static int s_buflen = 480;

static void pri_err_cb(char *s)
{
    Debug("PRI",DebugWarn,"%s",s);
}

static void pri_msg_cb(char *s)
{
    Debug("PRI",DebugInfo,"%s",s);
}

#define bitswap(v) bitswap_table[v]

static unsigned char bitswap_table[256];

static void bitswap_init()
{
    for (unsigned int c = 0; c <= 255; c++) {
	unsigned char v = 0;
	for (int b = 0; b <= 7; b++)
	    if (c & (1 << b))
		v |= (0x80 >> b);
	bitswap_table[c] = v;
    }
}

/* Switch types */
static TokenDict dict_str2switch[] = {
    { "unknown", PRI_SWITCH_UNKNOWN },
    { "ni2", PRI_SWITCH_NI2 },
    { "dms100", PRI_SWITCH_DMS100 },
    { "lucent5e", PRI_SWITCH_LUCENT5E },
    { "at&t4ess", PRI_SWITCH_ATT4ESS },
    { "euroisdn_e1", PRI_SWITCH_EUROISDN_E1 },
    { "euroisdn_t1", PRI_SWITCH_EUROISDN_T1 },
    { "ni1", PRI_SWITCH_NI1 },
    { 0, -1 }
};

static TokenDict dict_str2type[] = {
    { "pri_net", PRI_NETWORK },
    { "pri_cpe", PRI_CPE },
#ifdef BRI_NETWORK_PTMP
    { "bri_net_ptmp", BRI_NETWORK_PTMP },
    { "bri_cpe_ptmp", BRI_CPE_PTMP },
    { "bri_net", BRI_NETWORK },
    { "bri_cpe", BRI_CPE },
#endif
    { 0, -1 }
};

#if 0
/* Numbering plan identifier */
static TokenDict dict_str2nplan[] = {
    { "unknown", PRI_NPI_UNKNOWN },
    { "e164", PRI_NPI_E163_E164 },
    { "x121", PRI_NPI_X121 },
    { "f69", PRI_NPI_F69 },
    { "national", PRI_NPI_NATIONAL },
    { "private", PRI_NPI_PRIVATE },
    { "reserved", PRI_NPI_RESERVED },
    { 0, -1 }
};

/* Type of number */
static TokenDict dict_str2ntype[] = {
    { "unknown", PRI_TON_UNKNOWN },
    { "international", PRI_TON_INTERNATIONAL },
    { "national", PRI_TON_NATIONAL },
    { "net_specific", PRI_TON_NET_SPECIFIC },
    { "subscriber", PRI_TON_SUBSCRIBER },
    { "abbreviated", PRI_TON_ABBREVIATED },
    { "reserved", PRI_TON_RESERVED },
    { 0, -1 }
};
#endif

/* Dialing plan */
static TokenDict dict_str2dplan[] = {
    { "unknown", PRI_UNKNOWN },
    { "international", PRI_INTERNATIONAL_ISDN },
    { "national", PRI_NATIONAL_ISDN },
    { "local", PRI_LOCAL_ISDN },
    { "private", PRI_PRIVATE },
    { 0, -1 }
};

/* Presentation */
static TokenDict dict_str2pres[] = {
    { "allow_user_not_screened", PRES_ALLOWED_USER_NUMBER_NOT_SCREENED },
    { "allow_user_passed", PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN },
    { "allow_user_failed", PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN },
    { "allow_network", PRES_ALLOWED_NETWORK_NUMBER },
    { "prohibit_user_not_screened", PRES_PROHIB_USER_NUMBER_NOT_SCREENED },
    { "prohibit_user_passed", PRES_PROHIB_USER_NUMBER_PASSED_SCREEN },
    { "prohibit_user_failed", PRES_PROHIB_USER_NUMBER_FAILED_SCREEN },
    { "prohibit_network", PRES_PROHIB_NETWORK_NUMBER },
    { "not_available", PRES_NUMBER_NOT_AVAILABLE },
    { 0, -1 }
};

#ifdef PRI_NSF_NONE
#define YATE_NSF_DEFAULT PRI_NSF_NONE
#else
#define YATE_NSF_DEFAULT -1
#endif
/* Network Specific Facilities (AT&T) */
static TokenDict dict_str2nsf[] = {
#ifdef PRI_NSF_NONE
    { "none", PRI_NSF_NONE },
    { "sid_preferred", PRI_NSF_SID_PREFERRED },
    { "ani_preferred", PRI_NSF_ANI_PREFERRED },
    { "sid_only", PRI_NSF_SID_ONLY },
    { "ani_only", PRI_NSF_ANI_ONLY },
    { "call_assoc_tsc", PRI_NSF_CALL_ASSOC_TSC },
    { "notif_catsc_clearing", PRI_NSF_NOTIF_CATSC_CLEARING },
    { "operator", PRI_NSF_OPERATOR },
    { "pcco", PRI_NSF_PCCO },
    { "sdn", PRI_NSF_SDN },
    { "toll_free_megacom", PRI_NSF_TOLL_FREE_MEGACOM },
    { "megacom", PRI_NSF_MEGACOM },
    { "accunet", PRI_NSF_ACCUNET },
    { "long_distance", PRI_NSF_LONG_DISTANCE_SERVICE },
    { "international_toll_free", PRI_NSF_INTERNATIONAL_TOLL_FREE },
    { "at&t_multiquest", PRI_NSF_ATT_MULTIQUEST },
    { "call_redirection", PRI_NSF_CALL_REDIRECTION_SERVICE },
#endif
    { 0, -1 }
};

/* Layer 1 formats */
static TokenDict dict_str2law[] = {
    { "mulaw", PRI_LAYER_1_ULAW },
    { "alaw", PRI_LAYER_1_ALAW },
    { "g721", PRI_LAYER_1_G721 },
    { 0, -1 }
};

class Fifo
{
public:
    Fifo(int buflen = s_buflen);
    ~Fifo();
    void clear();
    void put(unsigned char value);
    unsigned char get();
private:
    int m_buflen;
    int m_head;
    int m_tail;
    unsigned char* m_buffer;
};

Fifo::Fifo(int buflen)
    : m_buflen(buflen), m_head(0), m_tail(1)
{
    m_buffer = (unsigned char*)::malloc(m_buflen);
}

Fifo::~Fifo()
{
    if (m_buffer)
	::free(m_buffer);
}

// make the fifo empty
void Fifo::clear()
{
    m_head = 0;
    m_tail = 1;
}

// put a byte in fifo, overwrite last byte if full
void Fifo::put(unsigned char value)
{
    m_buffer[m_tail] = value;
    bool full = (m_head == m_tail);
    m_tail++;
    if (m_tail >= m_buflen)
	m_tail = 0;
    if (full)
	m_head = m_tail;
}

// get a byte from fifo, return last read if empty
unsigned char Fifo::get()
{
    unsigned char tmp = m_buffer[m_head];
    int nh = m_head+1;
    if (nh >= m_buflen)
	nh = 0;
    if (nh != m_tail)
	m_head = nh;
    return tmp;
}

class WpChan;

class PriSpan : public GenObject, public Thread
{
    friend class WpData;
public:
    static PriSpan *create(int span, int chan1, int nChans, int dChan, int netType,
			   int switchType, int dialPlan, int presentation,
			   int overlapDial, int nsf = YATE_NSF_DEFAULT);
    virtual ~PriSpan();
    virtual void run();
    inline struct pri *pri()
	{ return m_pri; }
    inline int span() const
	{ return m_span; }
    inline bool belongs(int chan) const
	{ return (chan >= m_offs) && (chan < m_offs+m_nchans); }
    inline int chan1() const
	{ return m_offs; }
    inline int chans() const
	{ return m_nchans; }
    inline int bchans() const
	{ return m_bchans; }
    inline int dplan() const
	{ return m_dplan; }
    inline int pres() const
	{ return m_pres; }
    inline unsigned int overlapped() const
	{ return m_overlapped; }
    inline bool outOfOrder() const
	{ return !m_ok; }
    int findEmptyChan(int first = 0, int last = 65535) const;
    WpChan *getChan(int chan) const;
    void idle();
    static unsigned long long restartPeriod;
    static bool dumpEvents;

private:
    PriSpan(struct pri *_pri, int span, int first, int chans, int dchan, int fd, int dplan, int pres, int overlapDial);
    static struct pri *makePri(int fd, int dchan, int nettype, int swtype, int overlapDial, int nsf);
    void handleEvent(pri_event &ev);
    bool validChan(int chan) const;
    void restartChan(int chan, bool outgoing, bool force = false);
    void ringChan(int chan, pri_event_ring &ev);
    void infoChan(int chan, pri_event_ring &ev);
    void hangupChan(int chan,pri_event_hangup &ev);
    void ackChan(int chan);
    void answerChan(int chan);
    void proceedingChan(int chan);
    int m_span;
    int m_offs;
    int m_nchans;
    int m_bchans;
    int m_fd;
    int m_dplan;
    int m_pres;
    unsigned int m_overlapped;
    struct pri *m_pri;
    unsigned long long m_restart;
    WpChan **m_chans;
    WpData *m_data;
    bool m_ok;
};

class WpSource : public DataSource
{
public:
    WpSource(WpChan *owner,unsigned int bufsize,const char* format);
    ~WpSource();
    void put(unsigned char val);

private:
    WpChan *m_owner;
    unsigned int m_bufpos;
    DataBlock m_buf;
};

class WpConsumer : public DataConsumer, public Fifo
{
public:
    WpConsumer(WpChan *owner,unsigned int bufsize,const char* format);
    ~WpConsumer();

    virtual void Consume(const DataBlock &data, unsigned long timeDelta);

private:
    WpChan *m_owner;
    DataBlock m_buffer;
};

class WpChan : public DataEndpoint
{
    friend class WpSource;
    friend class WpConsumer;
    friend class WpData;
public:
    WpChan(PriSpan *parent, int chan, unsigned int bufsize);
    virtual ~WpChan();
    virtual void disconnected(bool final, const char *reason);
    virtual bool nativeConnect(DataEndpoint *peer);
    inline PriSpan *span() const
	{ return m_span; }
    inline int chan() const
	{ return m_chan; }
    inline int absChan() const
	{ return m_abschan; }
    inline bool inUse() const
	{ return (m_ring || m_call); }
    void ring(q931_call *call = 0);
    void hangup(int cause = PRI_CAUSE_INVALID_MSG_UNSPECIFIED);
    void sendDigit(char digit);
    void gotDigits(const char *digits);
    bool call(Message &msg, const char *called = 0);
    bool answer();
    void answered();
    void idle();
    void restart(bool outgoing = false);
    bool open(const char* format);
    void close();
    inline void setTimeout(unsigned long long tout)
	{ m_timeout = tout ? Time::now()+tout : 0; }
    const char *status() const;
    const String& id() const
	{ return m_id; }
    bool isISDN() const
	{ return m_isdn; }
    inline void setTarget(const char *target = 0)
	{ m_targetid = target; }
    inline const String& getTarget() const
	{ return m_targetid; }
private:
    PriSpan *m_span;
    int m_chan;
    bool m_ring;
    unsigned long long m_timeout;
    q931_call *m_call;
    unsigned int m_bufsize;
    int m_abschan;
    bool m_isdn;
    String m_id;
    String m_targetid;
    WpSource* m_wp_s;
    WpConsumer* m_wp_c;
};

class WpData : public Thread
{
public:
    WpData(PriSpan* span);
    ~WpData();
    virtual void run();
private:
    PriSpan* m_span;
    int m_fd;
    unsigned char* m_buffer;
    WpChan **m_chans;
};

class WpHandler : public MessageHandler
{
public:
    WpHandler() : MessageHandler("call.execute") { }
    virtual bool received(Message &msg);
};

class WpDropper : public MessageHandler
{
public:
    WpDropper() : MessageHandler("call.drop") { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("engine.status") { }
    virtual bool received(Message &msg);
};

class WpChanHandler : public MessageReceiver
{
public:
    enum {
	Ringing,
	Answered,
	DTMF,
    };
    virtual bool received(Message &msg, int id);
};

class WanpipePlugin : public Plugin
{
    friend class PriSpan;
    friend class WpHandler;
public:
    WanpipePlugin();
    virtual ~WanpipePlugin();
    virtual void initialize();
    virtual bool isBusy() const;
    PriSpan *findSpan(int chan);
    WpChan *findChan(const char *id);
    WpChan *findChan(int first = -1, int last = -1);
    ObjList m_spans;
    Mutex mutex;
};

WanpipePlugin wplugin;
unsigned long long PriSpan::restartPeriod = 0;
bool PriSpan::dumpEvents = false;

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
    PriSpan *ps = new PriSpan(p,span,chan1,nChans,dChan,fd,dialPlan,presentation,overlapDial);
    ps->startup();
    return ps;
}

struct pri *PriSpan::makePri(int fd, int dchan, int nettype, int swtype,
			     int overlapDial, int nsf)
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
    struct pri *ret = ::pri_new_cb(fd, nettype, swtype, wp_read, wp_write, 0);
#ifdef PRI_NSF_NONE
    if (ret)
	::pri_set_nsf(ret, nsf);
#endif
#ifdef PRI_SET_OVERLAPDIAL
    if (ret)
	::pri_set_overlapdial(ret, (overlapDial > 0));
#endif
    return ret;
}

PriSpan::PriSpan(struct pri *_pri, int span, int first, int chans, int dchan, int fd, int dplan, int pres, int overlapDial)
    : Thread("PriSpan"), m_span(span), m_offs(first), m_nchans(chans), m_bchans(0),
      m_fd(fd), m_dplan(dplan), m_pres(pres), m_overlapped(0), m_pri(_pri),
      m_restart(0), m_chans(0), m_data(0), m_ok(false)
{
    Debug(DebugAll,"PriSpan::PriSpan(%p,%d,%d,%d) [%p]",_pri,span,chans,fd,this);
    if (overlapDial > 0)
	m_overlapped = overlapDial;
    WpChan **ch = new WpChan* [chans];
    for (int i = 1; i <= chans; i++) {
	if (i != dchan) {
	    ch[i-1] = new WpChan(this,i,s_buflen);
	    m_bchans++;
	}
	else
	    ch[i-1] = 0;
    }
    m_chans = ch;
    wplugin.m_spans.append(this);
    WpData* dat = new WpData(this);
    dat->startup();
}

PriSpan::~PriSpan()
{
    Debug(DebugAll,"PriSpan::~PriSpan() [%p]",this);
    wplugin.m_spans.remove(this,false);
    m_ok = false;
    delete m_data;
    for (int i = 0; i < m_nchans; i++) {
	WpChan *c = m_chans[i];
	m_chans[i] = 0;
	if (c) {
	    c->hangup(PRI_CAUSE_NORMAL_UNSPECIFIED);
	    c->destruct();
	}
    }
    delete[] m_chans;
    ::close(m_fd);
}

void PriSpan::run()
{
    Debug(DebugAll,"PriSpan::run() [%p]",this);
    m_restart = Time::now() + restartPeriod;
    ::pri_set_userdata(m_pri, this);
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
	Lock lock(wplugin.mutex);
	if (!sel) {
	    ev = ::pri_schedule_run(m_pri);
	    idle();
	}
	else if (sel > 0)
	    ev = ::pri_check_event(m_pri);
	else if (errno != EINTR)
	    Debug("PriSpan",DebugGoOn,"select() error %d: %s",
		errno,::strerror(errno));
	if (ev) {
	    if (dumpEvents && debugAt(DebugAll))
		::pri_dump_event(m_pri, ev);
	    handleEvent(*ev);
	}
    }
}

void PriSpan::idle()
{
    if (!m_chans)
	return;
    if (restartPeriod && (Time::now() > m_restart)) {
	m_restart = Time::now() + restartPeriod;
	Debug("PriSpan",DebugInfo,"Restarting idle channels on span %d",m_span);
	for (int i=0; i<m_nchans; i++)
	    if (m_chans[i])
		restartChan(i+1,true);
    }
    for (int i=0; i<m_nchans; i++)
	if (m_chans[i])
	    m_chans[i]->idle();
}

void PriSpan::handleEvent(pri_event &ev)
{
    switch (ev.e) {
	case PRI_EVENT_DCHAN_UP:
	    Debug(DebugInfo,"D-channel up on span %d",m_span);
	    m_ok = true;
	    m_restart = Time::now() + 1000000;
	    break;
	case PRI_EVENT_DCHAN_DOWN:
	    Debug(DebugWarn,"D-channel down on span %d",m_span);
	    m_ok = false;
	    for (int i=0; i<m_nchans; i++)
		if (m_chans[i])
		    m_chans[i]->hangup(PRI_CAUSE_NETWORK_OUT_OF_ORDER);
	    break;
	case PRI_EVENT_RESTART:
	    restartChan(ev.restart.channel,false,true);
	    break;
	case PRI_EVENT_CONFIG_ERR:
	    Debug(DebugWarn,"Error on span %d: %s",m_span,ev.err.err);
	    break;
	case PRI_EVENT_RING:
	    ringChan(ev.ring.channel,ev.ring);
	    break;
	case PRI_EVENT_INFO_RECEIVED:
	    infoChan(ev.ring.channel,ev.ring);
	    break;
	case PRI_EVENT_RINGING:
	    Debug(DebugInfo,"Ringing our call on channel %d on span %d",ev.ringing.channel,m_span);
	    break;
	case PRI_EVENT_HANGUP:
	    Debug(DebugInfo,"Hangup detected on channel %d on span %d",ev.hangup.channel,m_span);
	    hangupChan(ev.hangup.channel,ev.hangup);
	    break;
	case PRI_EVENT_ANSWER:
	    Debug(DebugInfo,"Answered channel %d on span %d",ev.answer.channel,m_span);
	    answerChan(ev.setup_ack.channel);
	    break;
	case PRI_EVENT_HANGUP_ACK:
	    Debug(DebugInfo,"Hangup ACK on channel %d on span %d",ev.hangup.channel,m_span);
	    break;
	case PRI_EVENT_RESTART_ACK:
	    Debug(DebugInfo,"Restart ACK on channel %d on span %d",ev.restartack.channel,m_span);
	    break;
	case PRI_EVENT_SETUP_ACK:
	    Debug(DebugInfo,"Setup ACK on channel %d on span %d",ev.setup_ack.channel,m_span);
	    ackChan(ev.setup_ack.channel);
	    break;
	case PRI_EVENT_HANGUP_REQ:
	    Debug(DebugInfo,"Hangup REQ on channel %d on span %d",ev.hangup.channel,m_span);
	    hangupChan(ev.hangup.channel,ev.hangup);
	    break;
	case PRI_EVENT_PROCEEDING:
	    Debug(DebugInfo,"Call proceeding on channel %d on span %d",ev.proceeding.channel,m_span);
	    proceedingChan(ev.proceeding.channel);
	    break;
#ifdef PRI_EVENT_PROGRESS
	case PRI_EVENT_PROGRESS:
	    Debug(DebugInfo,"Call progressing on channel %d on span %d",ev.proceeding.channel,m_span);
	    proceedingChan(ev.proceeding.channel);
	    break;
#endif
	default:
	    Debug(DebugInfo,"Received PRI event %d",ev.e);
    }
}

bool PriSpan::validChan(int chan) const
{
    return (chan > 0) && (chan <= m_nchans) && m_chans && m_chans[chan-1];
}

int PriSpan::findEmptyChan(int first, int last) const
{
    if (!m_ok)
	return -1;
    first -= m_offs;
    last -= m_offs;
    if (first < 0)
	first = 0;
    if (last > m_nchans-1)
	last = m_nchans-1;
    for (int i=first; i<=last; i++)
	if (m_chans[i] && !m_chans[i]->inUse())
	    return i+1;
    return -1;
}

WpChan *PriSpan::getChan(int chan) const
{
    return validChan(chan) ? m_chans[chan-1] : 0;
}

void PriSpan::restartChan(int chan, bool outgoing, bool force)
{
    if (chan < 0) {
	Debug(DebugInfo,"Restart request on entire span %d",m_span);
	return;
    }
    if (!validChan(chan)) {
	Debug(DebugInfo,"Restart request on invalid channel %d on span %d",chan,m_span);
	return;
    }
    if (force || !getChan(chan)->inUse()) {
	Debug(DebugAll,"Restarting B-channel %d on span %d",chan,m_span);
	getChan(chan)->restart(outgoing);
    }
}

void PriSpan::ringChan(int chan, pri_event_ring &ev)
{
    if (chan == -1)
	chan = findEmptyChan();
    if (!validChan(chan)) {
	Debug(DebugInfo,"Ring on invalid channel %d on span %d",chan,m_span);
	::pri_hangup(pri(),ev.call,PRI_CAUSE_CHANNEL_UNACCEPTABLE);
	::pri_destroycall(pri(),ev.call);
	return;
    }
    Debug(DebugInfo,"Ring on channel %d on span %d",chan,m_span);
    Debug(DebugInfo,"caller='%s' callerno='%s' callingplan=%d",
	ev.callingname,ev.callingnum,ev.callingplan);
    Debug(DebugInfo,"callednum='%s' redirectnum='%s' calledplan=%d",
	ev.callednum,ev.redirectingnum,ev.calledplan);
    Debug(DebugInfo,"type=%d complete=%d format='%s'",
	ev.ctype,ev.complete,lookup(ev.layer1,dict_str2law,"unknown"));
    WpChan* c = getChan(chan);
    c->ring(ev.call);
    if (m_overlapped && !ev.complete) {
	if (::strlen(ev.callednum) < m_overlapped) {
	    ::pri_need_more_info(pri(),ev.call,chan,!c->isISDN());
	    return;
	}
    }
    Message *m = new Message("call.route");
    m->addParam("driver","wp");
    m->addParam("id",c->id());
    m->addParam("span",String(m_span));
    m->addParam("channel",String(chan));
    if (ev.callingnum[0])
	m->addParam("caller",ev.callingnum);
    if (ev.callednum[0])
	m->addParam("called",ev.callednum);
    if (m_overlapped && !ev.complete)
	m->addParam("overlapped","yes");
    const char* dataLaw = "slin";
    switch (ev.layer1) {
	case PRI_LAYER_1_ALAW:
	    dataLaw = "alaw";
	    break;
	case PRI_LAYER_1_ULAW:
	    dataLaw = "mulaw";
	    break;
    }
    m->addParam("format",dataLaw);
    if (Engine::dispatch(m)) {
	*m = "call.execute";
	m->addParam("callto",m->retValue());
	m->retValue().clear();
	c->open(dataLaw);
	m->userData(getChan(chan));
	if (Engine::dispatch(m)) {
	    c->setTarget(m->getValue("targetid"));
	    if (c->getTarget().null()) {
		Debug(DebugInfo,"Answering now chan %s [%p] because we have no targetid",
		    c->id().c_str(),c);
		c->answer();
	    }
	    else
		getChan(chan)->setTimeout(60000000);
	}
	else
	    getChan(chan)->hangup(PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
    }
    else
	getChan(chan)->hangup(PRI_CAUSE_NO_ROUTE_DESTINATION);
    delete m;
}

void PriSpan::infoChan(int chan, pri_event_ring &ev)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Info on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(DebugInfo,"info on channel %d on span %d",chan,m_span);
    Debug(DebugInfo,"caller='%s' callerno='%s' callingplan=%d",
	ev.callingname,ev.callingnum,ev.callingplan);
    Debug(DebugInfo,"callednum='%s' redirectnum='%s' calledplan=%d",
	ev.callednum,ev.redirectingnum,ev.calledplan);
    getChan(chan)->gotDigits(ev.callednum);
}

void PriSpan::hangupChan(int chan,pri_event_hangup &ev)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Hangup on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(DebugInfo,"Hanging up channel %d on span %d",chan,m_span);
    getChan(chan)->hangup(ev.cause);
}

void PriSpan::ackChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"ACK on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(DebugInfo,"ACKnowledging channel %d on span %d",chan,m_span);
    getChan(chan)->setTimeout(0);
}

void PriSpan::answerChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"ANSWER on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(DebugInfo,"ANSWERing channel %d on span %d",chan,m_span);
    getChan(chan)->answered();
}

void PriSpan::proceedingChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Proceeding on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(DebugInfo,"Extending timeout on channel %d on span %d",chan,m_span);
    getChan(chan)->setTimeout(60000000);
}

WpSource::WpSource(WpChan *owner,unsigned int bufsize,const char* format)
    : DataSource(format),
      m_owner(owner), m_bufpos(0), m_buf(0,bufsize)
{
    Debug(DebugAll,"WpSource::WpSource(%p) [%p]",owner,this);
    m_owner->m_wp_s = this;
}

WpSource::~WpSource()
{
    Debug(DebugAll,"WpSource::~WpSource() [%p]",this);
    m_owner->m_wp_s = 0;
}

void WpSource::put(unsigned char val)
{
    ((char*)m_buf.data())[m_bufpos] = val;
    if (++m_bufpos >= m_buf.length()) {
	m_bufpos = 0;
	Forward(m_buf);
    }
}

WpConsumer::WpConsumer(WpChan *owner,unsigned int bufsize,const char* format)
    : DataConsumer(format), Fifo(2*bufsize),
      m_owner(owner)
{
    Debug(DebugAll,"WpConsumer::WpConsumer(%p) [%p]",owner,this);
    m_owner->m_wp_c = this;
}

WpConsumer::~WpConsumer()
{
    Debug(DebugAll,"WpConsumer::~WpConsumer() [%p]",this);
    m_owner->m_wp_c = 0;
}

void WpConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    const unsigned char* buf = (const unsigned char*)data.data();
    for (unsigned int i = 0; i < data.length(); i++)
	put(buf[i]);
}

WpData::WpData(PriSpan* span)
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
			    s->put(bitswap(*dat));
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
		    *dat++ = bitswap(c ? c->get() : 0xff);
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

WpChan::WpChan(PriSpan *parent, int chan, unsigned int bufsize)
    : DataEndpoint("wanpipe"), m_span(parent), m_chan(chan), m_ring(false),
      m_timeout(0), m_call(0), m_bufsize(bufsize), m_wp_s(0), m_wp_c(0)
{
    Debug(DebugAll,"WpChan::WpChan(%p,%d) [%p]",parent,chan,this);
    // I hate counting from one...
    m_abschan = m_chan+m_span->chan1()-1;
    m_isdn = true;
    m_id << "wp/" << m_abschan;
}

WpChan::~WpChan()
{
    Debug(DebugAll,"WpChan::~WpChan() [%p] %d",this,m_chan);
    hangup(PRI_CAUSE_NORMAL_UNSPECIFIED);
}

void WpChan::disconnected(bool final, const char *reason)
{
    Debugger debug("WpChan::disconnected()", " '%s' [%p]",reason,this);
    if (!final) {
	Message m("chan.disconnected");
	m.addParam("driver","wp");
	m.addParam("id",id());
	m.addParam("span",String(m_span->span()));
	m.addParam("channel",String(m_chan));
	if (m_targetid) {
	    m.addParam("targetid",m_targetid);
	    setTarget();
	}
	m.addParam("reason",reason);
	Engine::enqueue(m);
    }
    wplugin.mutex.lock();
    hangup(PRI_CAUSE_NORMAL_CLEARING);
    wplugin.mutex.unlock();
}

bool WpChan::nativeConnect(DataEndpoint *peer)
{
    return false;
}

const char *WpChan::status() const
{
    if (m_ring)
	return "ringing";
    if (m_call)
	return m_timeout ? "calling" : "connected";
    return "idle";
}

void WpChan::idle()
{
    if (m_timeout && (Time::now() > m_timeout)) {
	Debug("WpChan",DebugWarn,"Timeout %s channel %d on span %d",
	    status(),m_chan,m_span->span());
	m_timeout = 0;
	hangup(PRI_CAUSE_RECOVERY_ON_TIMER_EXPIRE);
    }
}

void WpChan::restart(bool outgoing)
{
    disconnect("restart");
    close();
    if (outgoing)
	::pri_reset(m_span->pri(),m_chan);
}

void WpChan::close()
{
    wplugin.mutex.lock();
    setSource();
    setConsumer();
    wplugin.mutex.unlock();
}

bool WpChan::open(const char* format)
{
    setSource(new WpSource(this,m_bufsize,format));
    getSource()->deref();
    setConsumer(new WpConsumer(this,m_bufsize,format));
    getConsumer()->deref();
    return true;
}

bool WpChan::answer()
{
    if (!m_ring) {
	Debug("WpChan",DebugWarn,"Answer request on %s channel %d on span %d",
	    status(),m_chan,m_span->span());
	return false;
    }
    m_ring = false;
    m_timeout = 0;
    Output("Answering on wp/%d (%d/%d)",m_abschan,m_span->span(),m_chan);
    ::pri_answer(m_span->pri(),m_call,m_chan,!m_isdn);
    return true;
}

void WpChan::hangup(int cause)
{
    const char *reason = pri_cause2str(cause);
    if (inUse())
	Debug(DebugInfo,"Hanging up wp/%d in state %s: %s (%d)",
	    m_abschan,status(),reason,cause);
    m_timeout = 0;
    setTarget();
    disconnect(reason);
    close();
    m_ring = false;
    if (m_call) {
	::pri_hangup(m_span->pri(),m_call,cause);
	::pri_destroycall(m_span->pri(),m_call);
	m_call = 0;
	Message *m = new Message("chan.hangup");
	m->addParam("driver","wp");
	m->addParam("id",id());
	m->addParam("span",String(m_span->span()));
	m->addParam("channel",String(m_chan));
	m->addParam("reason",pri_cause2str(cause));
	Engine::enqueue(m);
    }
}

void WpChan::answered()
{
    if (!m_call) {
	Debug("WpChan",DebugWarn,"Answer detected on %s channel %d on span %d",
	    status(),m_chan,m_span->span());
	return;
    }
    m_timeout = 0;
    Output("Remote answered on wp/%d (%d/%d)",m_abschan,m_span->span(),m_chan);
    Message *m = new Message("call.answered");
    m->addParam("driver","wp");
    m->addParam("id",id());
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    if (m_targetid)
	m->addParam("targetid",m_targetid);
    m->addParam("status","answered");
    Engine::enqueue(m);
}

void WpChan::gotDigits(const char *digits)
{
    Message *m = new Message("chan.dtmf");
    m->addParam("driver","wp");
    m->addParam("id",id());
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    if (m_targetid)
	m->addParam("targetid",m_targetid);
    m->addParam("text",digits);
    Engine::enqueue(m);
}

void WpChan::sendDigit(char digit)
{
    if (m_call)
	::pri_information(m_span->pri(),m_call,digit);
}

bool WpChan::call(Message &msg, const char *called)
{
    if (m_span->outOfOrder()) {
	Debug("WpChan",DebugInfo,"Span %d is out of order, failing call",m_span->span());
	return false;
    }
    if (!called)
	called = msg.getValue("called");
    Debug("WpChan",DebugInfo,"Calling '%s' on channel %d span %d",
	called, m_chan,m_span->span());
    int layer1 = lookup(msg.getValue("format"),dict_str2law,-1);
    hangup(PRI_CAUSE_PRE_EMPTED);
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd) {
	open(lookup(layer1,dict_str2law));
	connect(dd);
	setTarget(msg.getValue("id"));
	msg.addParam("targetid",id());
    }
    else
	msg.userData(this);
    Output("Calling '%s' on wp/%d (%d/%d)",called,m_abschan,m_span->span(),m_chan);
    char *caller = (char *)msg.getValue("caller");
    int callerplan = lookup(msg.getValue("callerplan"),dict_str2dplan,m_span->dplan());
    char *callername = (char *)msg.getValue("callername");
    int callerpres = lookup(msg.getValue("callerpres"),dict_str2pres,m_span->pres());
    int calledplan = lookup(msg.getValue("calledplan"),dict_str2dplan,m_span->dplan());
    Debug(DebugAll,"Caller='%s' name='%s' plan=%s pres=%s, Called plan=%s",
	caller,callername,lookup(callerplan,dict_str2dplan),
	lookup(callerpres,dict_str2pres),lookup(calledplan,dict_str2dplan));
    m_call =::pri_new_call(span()->pri());
#ifdef PRI_DUMP_INFO
    struct pri_sr *req = ::pri_sr_new();
    ::pri_sr_set_bearer(req,0/*transmode*/,layer1);
    ::pri_sr_set_channel(req,m_chan,1/*exclusive*/,!m_isdn);
    ::pri_sr_set_caller(req,caller,callername,callerplan,callerpres);
    ::pri_sr_set_called(req,(char *)called,calledplan,1/*complete*/);
    ::q931_setup(span()->pri(),m_call,req);
#else
    ::pri_call(m_span->pri(),m_call,0/*transmode*/,m_chan,1/*exclusive*/,!m_isdn,
	caller,callerplan,callername,callerpres,(char *)called,calledplan,layer1
    );
#endif
    setTimeout(10000000);
    Message *m = new Message("chan.startup");
    m->addParam("driver","wp");
    m->addParam("id",id());
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    m->addParam("direction","outgoing");
    Engine::enqueue(m);
    return true;
}

void WpChan::ring(q931_call *call)
{
    if (call) {
	setTimeout(10000000);
	m_call = call;
	m_ring = true;
	::pri_acknowledge(m_span->pri(),m_call,m_chan,0);
	Message *m = new Message("chan.startup");
	m->addParam("driver","wp");
	m->addParam("id",id());
	m->addParam("span",String(m_span->span()));
	m->addParam("channel",String(m_chan));
	m->addParam("direction","incoming");
	Engine::enqueue(m);
    }
    else
	hangup(PRI_CAUSE_WRONG_CALL_STATE);
}

bool WpHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^wp/\\([^/]*\\)/\\?\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (!msg.userData()) {
	Debug(DebugWarn,"Wanpipe call found but no data channel!");
	return false;
    }
    String chan = dest.matchString(1);
    String num = dest.matchString(2);
    DDebug(DebugInfo,"Found call to Wanpipe chan='%s' name='%s'",
	chan.c_str(),num.c_str());
    WpChan *c = 0;

    r = "^\\([0-9]\\+\\)-\\([0-9]*\\)$";
    Lock lock(wplugin.mutex);
    if (chan.matches(r))
	c = wplugin.findChan(chan.matchString(1).toInteger(),
	    chan.matchString(2).toInteger(65535));
    else
	c = wplugin.findChan(chan.toInteger(-1));

    if (c) {
	Debug(DebugInfo,"Will call '%s' on chan wp/%d (%d/%d)",
	    num.c_str(),c->absChan(),c->span()->span(),c->chan());
	return c->call(msg,num);
    }
    else
	Debug(DebugWarn,"No free Wanpipe channel '%s'",chan.c_str());
    return false;
}

bool WpDropper::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (id.null()) {
	Debug("WpDropper",DebugInfo,"Dropping all calls");
	wplugin.mutex.lock();
	const ObjList *l = &wplugin.m_spans;
	for (; l; l=l->next()) {
	    PriSpan *s = static_cast<PriSpan *>(l->get());
	    if (s) {
		for (int n=1; n<=s->chans(); n++) {
		    WpChan *c = s->getChan(n);
		    if (c)
			c->hangup(PRI_CAUSE_INTERWORKING);
		}
	    }
	}
	wplugin.mutex.unlock();
	return false;
    }
    if (!id.startsWith("wp/"))
	return false;
    WpChan *c = 0;
    id >> "wp/";
    int n = id.toInteger();
    if ((n > 0) && (c = wplugin.findChan(n))) {
	Debug("WpDropper",DebugInfo,"Dropping wp/%d (%d/%d)",
	    n,c->span()->span(),c->chan());
	wplugin.mutex.lock();
	c->hangup(PRI_CAUSE_INTERWORKING);
	wplugin.mutex.unlock();
	return true;
    }
    Debug("WpDropper",DebugInfo,"Could not find wp/%s",id.c_str());
    return false;
}

bool WpChanHandler::received(Message &msg, int id)
{
    String tid(msg.getValue("targetid"));
    if (!tid.startSkip("wp/",false))
	return false;
    int n = tid.toInteger();
    WpChan* c = 0;
    if ((n > 0) && (c = wplugin.findChan(n))) {
	Lock lock(wplugin.mutex);
	switch (id) {
	    case Answered:
		c->answer();
		break;
	    case Ringing:
		Debug("Wp",DebugInfo,"Not implemented ringing!");
		break;
	    case DTMF:
		for (const char* t = msg.getValue("text"); t && *t; ++t)
		    c->sendDigit(*t);
		break;
	}
    }
    Debug("WpChanHandler",DebugInfo,"Could not find wp/%s",tid.c_str());
    return false;
}

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"wp") && ::strcmp(sel,"fixchans"))
	return false;
    String st("name=wp,type=fixchans,format=Status|Span/Chan");
    wplugin.mutex.lock();
    const ObjList *l = &wplugin.m_spans;
    st << ",spans=" << l->count() << ",spanlen=";
    bool first = true;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s) {
	    if (first)
		first = false;
	    else
		st << "|";
	    st << s->chans();
	}
    }
    st << ";buflen=" << s_buflen << ";";
    l = &wplugin.m_spans;
    first = true;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s) {
	    for (int n=1; n<=s->chans(); n++) {
		WpChan *c = s->getChan(n);
		if (c) {
		    if (first)
			first = false;
		    else
			st << ",";
		    st << c->id() << "=";
		    st << c->status() << "|" << s->span() << "/" << n;
		}
	    }
	}
    }
    wplugin.mutex.unlock();
    msg.retValue() << st << "\n";
    return false;
}

WanpipePlugin::WanpipePlugin()
    : mutex(true)
{
    Output("Loaded module Wanpipe");
    bitswap_init();
    ::pri_set_error(pri_err_cb);
    ::pri_set_message(pri_msg_cb);
}

WanpipePlugin::~WanpipePlugin()
{
    Output("Unloading module Wanpipe");
}

PriSpan *WanpipePlugin::findSpan(int chan)
{
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s && s->belongs(chan))
	    return s;
    }
    return 0;
}

WpChan *WanpipePlugin::findChan(const char *id)
{
    String s(id);
    if (!s.startsWith("wp/"))
	return 0;
    s >> "wp/";
    int n = s.toInteger();
    return (n > 0) ? findChan(n) : 0;
}

WpChan *WanpipePlugin::findChan(int first, int last)
{
    DDebug(DebugAll,"WanpipePlugin::findChan(%d,%d)",first,last);
    // see first if we have an exact request
    if (first > 0 && last < 0) {
	PriSpan *s = findSpan(first);
	return s ? s->getChan(first - s->chan1() + 1) : 0;
    }
    if (last < 0)
	last = 65535;
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s) {
	    Debug(DebugAll,"Searching for free chan in span %d [%p]",
		s->span(),s);
	    int c = s->findEmptyChan(first,last);
	    if (c > 0)
		return s->getChan(c);
	    if (s->belongs(last))
		break;
	}
    }
    return 0;
}

bool WanpipePlugin::isBusy() const
{
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s) {
	    for (int n=1; n<=s->chans(); n++) {
		WpChan *c = s->getChan(n);
		if (c && c->inUse())
		    return true;
	    }
	}
    }
    return false;
}

void WanpipePlugin::initialize()
{
    Output("Initializing module Wanpipe");
    Configuration cfg(Engine::configFile("chan_wanpipe"));
    PriSpan::restartPeriod = cfg.getIntValue("general","restart") * 1000000ULL;
    PriSpan::dumpEvents = cfg.getBoolValue("general","dumpevents");
    if (!m_spans.count()) {
	s_buflen = cfg.getIntValue("general","buflen",480);
	int chan1 = 1;
	for (int span = 1;;span++) {
	    String sect("span ");
	    sect += String(span);
	    int num = cfg.getIntValue(sect,"chans",-1);
	    if (num < 0)
		break;
	    if (num) {
		int dchan = -1;
		// guess where we may have a D channel
		switch (num) {
		    case 3:
			// BRI ISDN
			dchan = 3;
			break;
		    case 24:
			// T1 with CCS
			dchan = 24;
			break;
		    case 31:
			// EuroISDN
			dchan = 16;
			break;
		}
		chan1 = cfg.getIntValue(sect,"first",chan1);
		PriSpan::create(span,chan1,num,
		    cfg.getIntValue(sect,"dchan", dchan),
		    cfg.getIntValue(sect,"type",dict_str2type,PRI_NETWORK),
		    cfg.getIntValue(sect,"swtype",dict_str2switch,
			PRI_SWITCH_UNKNOWN),
		    cfg.getIntValue(sect,"dialplan",dict_str2dplan,
			PRI_UNKNOWN),
		    cfg.getIntValue(sect,"presentation",dict_str2pres,
			PRES_ALLOWED_USER_NUMBER_NOT_SCREENED),
		    cfg.getIntValue(sect,"overlapdial"),
		    cfg.getIntValue(sect,"facilities",dict_str2nsf,
			YATE_NSF_DEFAULT)
		);
		chan1 += num;
	    }
	}
	if (m_spans.count()) {
	    Output("Created %d spans",m_spans.count());
	    Engine::install(new WpHandler);
	    Engine::install(new WpDropper);
	    Engine::install(new StatusHandler);
	    WpChanHandler* ch = new WpChanHandler;
	    Engine::install(new MessageRelay("call.ringing",ch,WpChanHandler::Ringing));
	    Engine::install(new MessageRelay("call.answered",ch,WpChanHandler::Answered));
	    Engine::install(new MessageRelay("chan.dtmf",ch,WpChanHandler::DTMF));
	}
	else
	    Output("No spans created, module not activated");
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
