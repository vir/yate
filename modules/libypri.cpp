/**
 * libypri.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common C++ base classes for PRI cards telephony drivers
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

extern "C" {
extern int q931_setup(struct pri *pri, q931_call *c, struct pri_sr *req);
};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


using namespace TelEngine;

// default buffer length: 20 ms
static int s_buflen = 160;

#ifdef PRI_NEW_SET_API
#define PRI_CB_STR struct pri *pri,
#else
#define PRI_CB_STR
#endif

static void pri_err_cb(PRI_CB_STR char *s)
{
    Debug("PRI",DebugWarn,"%s",s);
}

static void pri_msg_cb(PRI_CB_STR char *s)
{
    Debug("PRI",DebugInfo,"%s",s);
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

static TokenDict dict_str2cause[] = {
    { "noroute", PRI_CAUSE_NO_ROUTE_DESTINATION },
    { "noconn", PRI_CAUSE_REQUESTED_CHAN_UNAVAIL },
    { "busy", PRI_CAUSE_USER_BUSY },
    { "noanswer", PRI_CAUSE_NO_USER_RESPONSE },
    { "rejected", PRI_CAUSE_CALL_REJECTED },
    { "forbidden", PRI_CAUSE_OUTGOING_CALL_BARRED },
    { "forbidden", PRI_CAUSE_INCOMING_CALL_BARRED },
    { "offline", PRI_CAUSE_DESTINATION_OUT_OF_ORDER },
    { "unallocated", PRI_CAUSE_UNALLOCATED },
    { "moved", PRI_CAUSE_NUMBER_CHANGED },
    { "congestion", PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION },
    { "congestion", PRI_CAUSE_SWITCH_CONGESTION },
    { "failure", PRI_CAUSE_DESTINATION_OUT_OF_ORDER },
    { 0, -1 }
};

/* Layer 1 formats */
static TokenDict dict_str2law[] = {
    { "mulaw", PRI_LAYER_1_ULAW },
    { "alaw", PRI_LAYER_1_ALAW },
    { "g721", PRI_LAYER_1_G721 },
    { 0, -1 }
};

/* Echo canceller taps */
static TokenDict dict_numtaps[] = {
    { "on", 1 },
    { "yes", 1 },
    { "true", 1 },
    { "enable", 1 },
    { 0, 0 }
};

class ChanGroup : public String
{
public:
    enum {
	FirstAvail = 0,
	RoundRobin = 1,
	RandomChan = 2
    };
    ChanGroup(const String& name, const NamedList* sect, int last);
    virtual ~ChanGroup()
	{ }
    inline void getRange(int& first, int& last, int& used) const
	{ first = m_first; last = m_last; used = m_used; }
    void setUsed(int used);
private:
    int m_mode;
    int m_first;
    int m_last;
    int m_used;
};

ChanGroup::ChanGroup(const String& name, const NamedList* sect, int last)
    : String(name)
{
    static TokenDict dict_groupmode[] = {
	{ "first", FirstAvail },
	{ "firstavail", FirstAvail },
	{ "rotate", RoundRobin },
	{ "roundrobin", RoundRobin },
	{ "random", RandomChan },
	{ 0, 0 }
    };
    m_mode = sect->getIntValue("mode",dict_groupmode,RoundRobin);
    m_first = sect->getIntValue("first",1);
    m_last = sect->getIntValue("last",last);
    setUsed(m_last);
}

void ChanGroup::setUsed(int used)
{
    switch (m_mode) {
	case FirstAvail:
	    m_used = m_last;
	    break;
	case RandomChan:
	    m_used = m_first + (::random() % (m_last - m_first + 1));
	    break;
	default:
	    m_used = used;
    }
}

Fifo::Fifo(int buflen)
    : m_buflen(buflen), m_head(0), m_tail(1)
{
    if (!m_buflen)
	m_buflen = s_buflen;
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

PriSpan::PriSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect)
    : Mutex(true),
      m_driver(driver), m_span(span), m_offs(first), m_nchans(chans), m_bchans(0),
      m_pri(_pri), m_restart(0), m_chans(0), m_ok(false)
{
    Debug(m_driver,DebugAll,"PriSpan::PriSpan() [%p]",this);
    int buflength = cfg.getIntValue(sect,"buflen", s_buflen);

    m_inband = cfg.getBoolValue(sect,"dtmfinband",cfg.getBoolValue("general","dtmfinband"));
    m_layer1 = cfg.getIntValue(sect,"format",dict_str2law,(chans == 24) ? PRI_LAYER_1_ULAW : PRI_LAYER_1_ALAW);
    m_dplan = cfg.getIntValue(sect,"dialplan",dict_str2dplan,PRI_UNKNOWN);
    m_pres = cfg.getIntValue(sect,"presentation",dict_str2pres,PRES_ALLOWED_USER_NUMBER_NOT_SCREENED);
    m_restartPeriod = cfg.getIntValue(sect,"restart",cfg.getIntValue("general","restart")) * (u_int64_t)1000000;
    m_dumpEvents = cfg.getBoolValue(sect,"dumpevents",cfg.getBoolValue("general","dumpevents"));
    m_overlapped = cfg.getIntValue(sect,"overlapdial",cfg.getIntValue("general","overlapdial"));
    if (m_overlapped < 0)
	m_overlapped = 0;
#ifdef PRI_SET_OVERLAPDIAL
    ::pri_set_overlapdial(m_pri, (m_overlapped > 0));
#endif
#ifdef PRI_NSF_NONE
    ::pri_set_nsf(m_pri,cfg.getIntValue(sect,"facilities",dict_str2nsf,YATE_NSF_DEFAULT));
#endif
    ::pri_set_debug(m_pri,cfg.getIntValue(sect,"debug"));
    ::pri_set_userdata(m_pri, this);

    PriChan **ch = new PriChan* [chans];
    for (int i = 1; i <= chans; i++) {
	if (i != dchan) {
	    ch[i-1] = m_driver->createChan(this,i,buflength);
	    m_bchans++;
	}
	else
	    ch[i-1] = 0;
    }

    m_chans = ch;
    m_restart = Time::now() + m_restartPeriod;
    m_driver->m_spans.append(this);
}

PriSpan::~PriSpan()
{
    Debug(m_driver,DebugAll,"PriSpan::~PriSpan() [%p]",this);
    m_driver->m_spans.remove(this,false);
    m_ok = false;
    for (int i = 0; i < m_nchans; i++) {
	PriChan *c = m_chans[i];
	m_chans[i] = 0;
	if (c) {
	    c->hangup(PRI_CAUSE_NORMAL_UNSPECIFIED);
	    c->destruct();
	}
    }
    delete[] m_chans;
}

void PriSpan::runEvent(bool idleRun)
{
    pri_event *ev = 0;
    lock();
    if (idleRun) {
	ev = ::pri_schedule_run(m_pri);
	idle();
    }
    else
	ev = ::pri_check_event(m_pri);
    if (ev) {
	if (m_dumpEvents && debugAt(DebugAll))
	    ::pri_dump_event(m_pri, ev);
	handleEvent(*ev);
    }
    unlock();
}

void PriSpan::idle()
{
    if (!m_chans)
	return;
    if (m_restartPeriod && (Time::now() > m_restart)) {
	m_restart = Time::now() + m_restartPeriod;
	Debug(m_driver,DebugInfo,"Restarting idle channels on span %d",m_span);
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
	    {
		for (int i=0; i<m_nchans; i++)
		    if (m_chans[i])
			m_chans[i]->goneUp();
	    }
	    break;
	case PRI_EVENT_DCHAN_DOWN:
	    Debug(DebugWarn,"D-channel down on span %d",m_span);
	    m_ok = false;
	    {
		for (int i=0; i<m_nchans; i++)
		    if (m_chans[i])
			m_chans[i]->hangup(PRI_CAUSE_NETWORK_OUT_OF_ORDER);
	    }
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
	    Debug(m_driver,DebugInfo,"Ringing our call on channel %d on span %d",ev.ringing.channel,m_span);
	    ringingChan(ev.proceeding.channel);
	    break;
	case PRI_EVENT_HANGUP:
	    Debug(m_driver,DebugInfo,"Hangup detected on channel %d on span %d",ev.hangup.channel,m_span);
	    hangupChan(ev.hangup.channel,ev.hangup);
	    break;
	case PRI_EVENT_ANSWER:
	    Debug(m_driver,DebugInfo,"Answered channel %d on span %d",ev.answer.channel,m_span);
	    answerChan(ev.setup_ack.channel);
	    break;
	case PRI_EVENT_HANGUP_ACK:
	    Debug(m_driver,DebugInfo,"Hangup ACK on channel %d on span %d",ev.hangup.channel,m_span);
	    break;
	case PRI_EVENT_RESTART_ACK:
	    Debug(m_driver,DebugInfo,"Restart ACK on channel %d on span %d",ev.restartack.channel,m_span);
	    break;
	case PRI_EVENT_SETUP_ACK:
	    Debug(m_driver,DebugInfo,"Setup ACK on channel %d on span %d",ev.setup_ack.channel,m_span);
	    ackChan(ev.setup_ack.channel);
	    break;
	case PRI_EVENT_HANGUP_REQ:
	    Debug(m_driver,DebugInfo,"Hangup REQ on channel %d on span %d",ev.hangup.channel,m_span);
	    hangupChan(ev.hangup.channel,ev.hangup);
	    break;
	case PRI_EVENT_PROCEEDING:
	    Debug(m_driver,DebugInfo,"Call proceeding on channel %d on span %d",ev.proceeding.channel,m_span);
	    proceedingChan(ev.proceeding.channel);
	    break;
#ifdef PRI_EVENT_PROGRESS
	case PRI_EVENT_PROGRESS:
	    Debug(m_driver,DebugInfo,"Call progressing on channel %d on span %d",ev.proceeding.channel,m_span);
	    proceedingChan(ev.proceeding.channel);
	    break;
#endif
#ifdef PRI_EVENT_KEYPAD_DIGIT
	case PRI_EVENT_KEYPAD_DIGIT:
	    digitsChan(ev.digit.channel,ev.digit.digits);
	    break;
#endif
	default:
	    Debug(m_driver,DebugInfo,"Unhandled PRI event %d",ev.e);
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

PriChan *PriSpan::getChan(int chan) const
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
	Debug(m_driver,DebugAll,"Restarting B-channel %d on span %d",chan,m_span);
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
    Debug(m_driver,DebugInfo,"Ring on channel %d on span %d",chan,m_span);
    Debug(m_driver,DebugInfo,"caller='%s' callerno='%s' callingplan=%d",
	ev.callingname,ev.callingnum,ev.callingplan);
    Debug(m_driver,DebugInfo,"callednum='%s' redirectnum='%s' calledplan=%d",
	ev.callednum,ev.redirectingnum,ev.calledplan);
    Debug(m_driver,DebugInfo,"type=%d complete=%d format='%s'",
	ev.ctype,ev.complete,lookup(ev.layer1,dict_str2law,"unknown"));
    PriChan* c = getChan(chan);
    if (c)
	c->ring(ev);
}

void PriSpan::infoChan(int chan, pri_event_ring &ev)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Info on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(m_driver,DebugInfo,"info on channel %d on span %d",chan,m_span);
    Debug(m_driver,DebugInfo,"caller='%s' callerno='%s' callingplan=%d",
	ev.callingname,ev.callingnum,ev.callingplan);
    Debug(m_driver,DebugInfo,"callednum='%s' redirectnum='%s' calledplan=%d",
	ev.callednum,ev.redirectingnum,ev.calledplan);
    getChan(chan)->gotDigits(ev.callednum,true);
}

void PriSpan::digitsChan(int chan, const char* digits)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Digits on invalid channel %d on span %d",chan,m_span);
	return;
    }
    getChan(chan)->gotDigits(digits,false);
}

void PriSpan::hangupChan(int chan,pri_event_hangup &ev)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Hangup on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(m_driver,DebugInfo,"Hanging up channel %d on span %d",chan,m_span);
    getChan(chan)->hangup(ev.cause);
}

void PriSpan::ackChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"ACK on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(m_driver,DebugInfo,"ACKnowledging channel %d on span %d",chan,m_span);
    getChan(chan)->setTimeout(0);
}

void PriSpan::answerChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"ANSWER on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(m_driver,DebugInfo,"ANSWERing channel %d on span %d",chan,m_span);
    getChan(chan)->answered();
}

void PriSpan::proceedingChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Proceeding on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(m_driver,DebugInfo,"Extending timeout on channel %d on span %d",chan,m_span);
    getChan(chan)->setTimeout(120000000);
    Engine::enqueue(getChan(chan)->message("call.progress"));
}

void PriSpan::ringingChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Ringing on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(m_driver,DebugInfo,"Extending timeout on channel %d on span %d",chan,m_span);
    getChan(chan)->setTimeout(120000000);
    Engine::enqueue(getChan(chan)->message("call.ringing"));
}

PriSource::PriSource(PriChan *owner, const char* format, unsigned int bufsize)
    : DataSource(format),
      m_owner(owner), m_buffer(0,bufsize)
{
    Debug(m_owner,DebugAll,"PriSource::PriSource(%p,'%s',%u) [%p]",owner,format,bufsize,this);
}

PriSource::~PriSource()
{
    Debug(m_owner,DebugAll,"PriSource::~PriSource() [%p]",this);
}

PriConsumer::PriConsumer(PriChan *owner, const char* format, unsigned int bufsize)
    : DataConsumer(format),
      m_owner(owner), m_buffer(0,bufsize)
{
    Debug(m_owner,DebugAll,"PriConsumer::PriConsumer(%p,'%s',%u) [%p]",owner,format,bufsize,this);
}

PriConsumer::~PriConsumer()
{
    Debug(m_owner,DebugAll,"PriConsumer::~PriConsumer() [%p]",this);
}

PriChan::PriChan(const PriSpan *parent, int chan, unsigned int bufsize)
    : Channel(parent->driver()),
      m_span(const_cast<PriSpan*>(parent)), m_chan(chan), m_ring(false),
      m_timeout(0), m_call(0), m_bufsize(bufsize)
{
    Debug(this,DebugAll,"PriChan::PriChan(%p,%d,%u) [%p]",parent,chan,bufsize,this);
    // I hate counting from one...
    m_abschan = m_chan+m_span->chan1()-1;
    m_isdn = true;
    m_address << m_span->span() << "/" << m_chan << "/" << m_abschan;
    status(chanStatus());
}

PriChan::~PriChan()
{
    Debug(this,DebugAll,"PriChan::~PriChan() [%p] %d",this,m_chan);
    hangup(PRI_CAUSE_NORMAL_UNSPECIFIED);
}

void PriChan::disconnected(bool final, const char *reason)
{
    Debugger debug("PriChan::disconnected()", " '%s' [%p]",reason,this);
    if (!final) {
	Message* m = message("chan.disconnected");
	m_targetid.clear();
	m->addParam("span",String(m_span->span()));
	m->addParam("channel",String(m_chan));
	m->addParam("reason",reason);
	Engine::enqueue(m);
    }
    m_span->lock();
    hangup(PRI_CAUSE_NORMAL_CLEARING);
    m_span->unlock();
}

bool PriChan::nativeConnect(DataEndpoint *peer)
{
    return false;
}

const char *PriChan::chanStatus() const
{
    if (m_ring)
	return "ringing";
    if (m_call)
	return m_timeout ? "calling" : "connected";
    return m_span->outOfOrder() ? "alarm" : "idle";
}

void PriChan::idle()
{
    if (m_timeout && (Time::now() > m_timeout)) {
	Debug("PriChan",DebugWarn,"Timeout %s channel %s (%d/%d)",
	    chanStatus(),id().c_str(),m_chan,m_span->span());
	m_timeout = 0;
	hangup(PRI_CAUSE_RECOVERY_ON_TIMER_EXPIRE);
    }
}

void PriChan::restart(bool outgoing)
{
    disconnect("restart");
    closeData();
    if (outgoing)
	::pri_reset(m_span->pri(),m_chan);
}

void PriChan::closeData()
{
    m_span->lock();
    setSource();
    setConsumer();
    m_span->unlock();
}

bool PriChan::answer()
{
    if (!m_ring) {
	Debug("PriChan",DebugWarn,"Answer request on %s channel %d on span %d",
	    chanStatus(),m_chan,m_span->span());
	return false;
    }
    m_ring = false;
    m_timeout = 0;
    status(chanStatus());
    Debug(this,DebugInfo,"Answering on %s (%d/%d)",id().c_str(),m_span->span(),m_chan);
    ::pri_answer(m_span->pri(),(q931_call*)m_call,m_chan,!m_isdn);
    return true;
}

void PriChan::goneUp()
{
    status(chanStatus());
}

void PriChan::hangup(int cause)
{
    if (!cause)
	cause = PRI_CAUSE_INVALID_MSG_UNSPECIFIED;
    const char *reason = pri_cause2str(cause);
    if (inUse())
	Debug(this,DebugInfo,"Hanging up %s in state %s: %s (%d)",
	    id().c_str(),chanStatus(),reason,cause);
    m_timeout = 0;
    m_targetid.clear();
    disconnect(reason);
    closeData();
    m_ring = false;
    if (m_call) {
	::pri_hangup(m_span->pri(),(q931_call*)m_call,cause);
	::pri_destroycall(m_span->pri(),(q931_call*)m_call);
	m_call = 0;
	Message *m = message("chan.hangup");
	m->addParam("span",String(m_span->span()));
	m->addParam("channel",String(m_chan));
	m->addParam("reason",pri_cause2str(cause));
	Engine::enqueue(m);
    }
    status(chanStatus());
}

void PriChan::answered()
{
    if (!m_call) {
	Debug("PriChan",DebugWarn,"Answer detected on %s %s channel %d on span %d",
	    chanStatus(),id().c_str(),m_chan,m_span->span());
	return;
    }
    m_timeout = 0;
    status(chanStatus());
    Debug(this,DebugInfo,"Remote answered on %s (%d/%d)",id().c_str(),m_span->span(),m_chan);
    Message *m = message("call.answered");
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    Engine::enqueue(m);
}

void PriChan::gotDigits(const char *digits, bool overlapped)
{
    if (null(digits)) {
	Debug(this,DebugMild,"Received empty digits string in mode %s channel %s (%d/%d)",
	    (overlapped ? "overlapped" : "keypad"),id().c_str(),m_span->span(),m_chan);
	return;
    }
    Message *m = message("chan.dtmf");
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    m->addParam("text",digits);
    if (overlapped)
	m->addParam("overlapped","yes");
    Engine::enqueue(m);
}

void PriChan::sendDigit(char digit)
{
    if (m_call)
	::pri_information(m_span->pri(),(q931_call*)m_call,digit);
}

bool PriChan::call(Message &msg, const char *called)
{
    if (m_span->outOfOrder()) {
	Debug("PriChan",DebugInfo,"Span %d is out of order, failing call",m_span->span());
	msg.setParam("error","offline");
	return false;
    }
    if (!called)
	called = msg.getValue("called");
    Debug(this,DebugInfo,"Calling '%s' on channel %d span %d",
	called,m_chan,m_span->span());
    int layer1 = msg.getIntValue("format",dict_str2law,m_span->layer1());
    hangup(PRI_CAUSE_PRE_EMPTED);
    setOutgoing(true);
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	openData(lookup(layer1,dict_str2law),msg.getIntValue("cancelecho",dict_numtaps));
	if (connect(ch,msg.getValue("reason")))
	    msg.setParam("peerid",id());
	m_targetid = msg.getValue("id");
	msg.setParam("targetid",id());
    }
    else
	msg.userData(this);
    m_inband = msg.getBoolValue("dtmfinband",m_span->inband());
    Output("Calling '%s' on %s (%d/%d)",called,id().c_str(),m_span->span(),m_chan);
    char *caller = (char *)msg.getValue("caller");
    int callerplan = msg.getIntValue("callerplan",dict_str2dplan,m_span->dplan());
    char *callername = (char *)msg.getValue("callername");
    int callerpres = msg.getIntValue("callerpres",dict_str2pres,m_span->pres());
    int calledplan = msg.getIntValue("calledplan",dict_str2dplan,m_span->dplan());
    Debug(this,DebugAll,"Caller='%s' name='%s' plan=%s pres=%s, Called plan=%s",
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
    ::pri_call(m_span->pri(),(q931_call*)m_call,0/*transmode*/,m_chan,1/*exclusive*/,!m_isdn,
	caller,callerplan,callername,callerpres,(char *)called,calledplan,layer1
    );
#endif
    setTimeout(30000000);
    status(chanStatus());
    Message *m = message("chan.startup");
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    m->addParam("direction","outgoing");
    Engine::enqueue(m);
    return true;
}

void PriChan::ring(pri_event_ring &ev)
{
    q931_call *call = ev.call;
    if (!call) {
	hangup(PRI_CAUSE_WRONG_CALL_STATE);
	return;
    }

    setTimeout(180000000);
    setOutgoing(false);
    m_call = call;
    m_ring = true;
    status(chanStatus());
    ::pri_acknowledge(m_span->pri(),m_call,m_chan,0);
    Message *m = message("chan.startup");
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    m->addParam("direction","incoming");
    Engine::enqueue(m);

    m_inband = m_span->inband();
    openData(lookup(ev.layer1,dict_str2law),0);

    m = message("call.route");
    if (m_span->overlapped() && !ev.complete && (::strlen(ev.callednum) < m_span->overlapped())) {
	::pri_need_more_info(m_span->pri(),m_call,m_chan,!isISDN());
	m->addParam("overlapped","yes");
    }
    if (ev.callingnum[0])
	m->addParam("caller",ev.callingnum);
    if (ev.callednum[0])
	m->addParam("called",ev.callednum);
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
    if (!startRouter(m))
	hangup(PRI_CAUSE_SWITCH_CONGESTION);
}

void PriChan::callAccept(Message& msg)
{
    Debug(this,DebugAll,"PriChan::callAccept() [%p]",this);
    setTimeout(180000000);
    Channel::callAccept(msg);
}

void PriChan::callRejected(const char* error, const char* reason, const Message* msg)
{
    int cause = lookup(error,dict_str2cause,PRI_CAUSE_NETWORK_OUT_OF_ORDER);
    Channel::callRejected(error,reason,msg);
    hangup(cause);
}

bool PriChan::msgRinging(Message& msg)
{
    status("ringing");
    return true;
}

bool PriChan::msgAnswered(Message& msg)
{
    answer();
    return true;
}

bool PriChan::msgTone(Message& msg, const char* tone)
{
    if (null(tone))
	return false;
    if (m_inband) {
	Message m("chan.attach");
	complete(m,true);
	m.userData(this);
	String tmp("tone/dtmfstr/");
	tmp += tone;
	m.setParam("override",tmp);
	m.setParam("single","yes");
	if (Engine::dispatch(m))
	    return true;
	// if we failed try to send as signalling anyway
    }
    while (*tone)
	sendDigit(*tone++);
    return true;
}

bool PriChan::msgText(Message& msg, const char* text)
{
    return false;
}

bool PriChan::msgDrop(Message& msg, const char* reason)
{
    if (inUse()) {
	hangup(PRI_CAUSE_INTERWORKING);
	return true;
    }
    return false;
}


bool PriDriver::msgExecute(Message& msg, String& dest)
{
    Regexp r("^\\([^/]*\\)/\\?\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (!msg.userData()) {
	Debug(DebugWarn,"Pri call found but no data channel!");
	return false;
    }
    String chan = dest.matchString(1);
    String num = dest.matchString(2);
    DDebug(this,DebugInfo,"Found call to pri chan='%s' name='%s'",
	chan.c_str(),num.c_str());
    PriChan *c = 0;

    r = "^\\([0-9]\\+\\)-\\([0-9]*\\)$";
    Lock lock(this);
    if (chan.matches(r))
	c = findFree(chan.matchString(1).toInteger(),
	    chan.matchString(2).toInteger(65535));
    else if ((chan[0] < '0') || (chan[0] > '9'))
	c = findFree(chan);
    else
	c = findFree(chan.toInteger(-1));

    if (c) {
	Debug(this,DebugInfo,"Will call '%s' on chan %s (%d) (%d/%d)",
	    num.c_str(),c->id().c_str(),c->absChan(),
	    c->span()->span(),c->chan());
	return c->call(msg,num);
    }
    else {
	Debug(this,DebugMild,"Found no free channel '%s'",chan.c_str());
	msg.setParam("error","congestion");
    }
    return false;
}

void PriDriver::dropAll()
{
    Debug(this,DebugInfo,"Dropping all %s calls",name().c_str());
    lock();
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s) {
	    for (int n=1; n<=s->chans(); n++) {
		PriChan *c = s->getChan(n);
		if (c)
		    c->hangup(PRI_CAUSE_INTERWORKING);
	    }
	}
    }
    unlock();
}

u_int8_t PriDriver::s_bitswap[256];

bool PriDriver::s_init = true;


PriDriver::PriDriver(const char* name)
    : Driver(name,"fixchans")
{
    varchan(false);
    if (s_init) {
	s_init = false;
	for (unsigned int c = 0; c <= 255; c++) {
	    u_int8_t v = 0;
	    for (int b = 0; b <= 7; b++)
		if (c & (1 << b))
		    v |= (0x80 >> b);
	    s_bitswap[c] = v;
	}
	::pri_set_error(pri_err_cb);
	::pri_set_message(pri_msg_cb);
    }
}

PriDriver::~PriDriver()
{
}

PriSpan* PriDriver::findSpan(int chan)
{
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s && s->belongs(chan))
	    return s;
    }
    return 0;
}

PriChan* PriDriver::findFree(int first, int last)
{
    DDebug(this,DebugAll,"PriDriver::findFree(%d,%d)",first,last);
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
	    Debug(this,DebugAll,"Searching for free chan in span %d [%p]",
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

PriChan* PriDriver::findFree(const String& group)
{
    ObjList* lst = m_groups.find(group);
    if (!lst)
	return 0;
    ChanGroup* grp = static_cast<ChanGroup*>(lst->get());
    if (!grp)
	return 0;
    int first = 0, last = 0, used = 0;
    grp->getRange(first,last,used);
    PriChan* c = (used < last) ? findFree(used+1,last) : 0;
    if (!c)
	c = (first <= used) ? findFree(first,used) : 0;
    if (!c)
	return 0;
    grp->setUsed(c->absChan());
    return c;
}

bool PriDriver::isBusy() const
{
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s) {
	    for (int n=1; n<=s->chans(); n++) {
		PriChan *c = s->getChan(n);
		if (c && c->inUse())
		    return true;
	    }
	}
    }
    return false;
}

void PriDriver::statusModule(String& str)
{
    Driver::statusModule(str);
    String sp;
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s)
	    sp.append(String(s->chans()),"|");
    }
    str.append("spans=",",") << m_spans.count();
    if (sp)
	str.append("spanlen=",",") << sp;
    str.append("groups=",",") << m_groups.count();
}

void PriDriver::statusParams(String& str)
{
    Driver::statusParams(str);
    int i = 0;
    int u = 0;
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s && !s->outOfOrder()) {
	    for (int n=1; n<=s->chans(); n++) {
		PriChan *c = s->getChan(n);
		if (c) {
		    if (c->inUse())
			u++;
		    else
			i++;
		}
	    }
	}
    }
    str.append("idle=",",") << i;
    str.append("used=",",") << u;
}

void PriDriver::netParams(Configuration& cfg, const String& sect, int chans, int* netType, int* swType, int* dChan)
{
    if (netType)
	*netType = cfg.getIntValue(sect,"type",dict_str2type,PRI_NETWORK);
    if (swType)
	*swType = cfg.getIntValue(sect,"swtype",dict_str2switch,PRI_SWITCH_UNKNOWN);
    if (dChan) {
	int dchan = -1;
	// guess where we may have a D channel
	switch (chans) {
	    case 3:  // BRI ISDN
		dchan = 3;
		break;
	    case 24: // T1 with CCS
		dchan = 24;
		break;
	    case 31: // EuroISDN
		dchan = 16;
		break;
	}
	*dChan = cfg.getIntValue(sect,"dchan", dchan);
    }
}

void PriDriver::init(const char* configName)
{
    Configuration cfg(Engine::configFile(configName));
    s_buflen = cfg.getIntValue("general","buflen",160);
    if (!m_spans.count()) {
	int chan1 = 1;
	for (int span = 1;;span++) {
	    String sect("span ");
	    sect << span;
	    int num = cfg.getIntValue(sect,"chans",-1);
	    if (num < 0)
		break;
	    if (num) {
		chan1 = cfg.getIntValue(sect,"first",chan1);
		if (cfg.getBoolValue(sect,"enabled",true))
		    createSpan(this,span,chan1,num,cfg,sect);
		chan1 += num;
	    }
	}
	if (m_spans.count()) {
	    Output("Created %d spans",m_spans.count());
	    unsigned int n = cfg.sections();
	    for (unsigned int i = 0; i < n; i++) {
		const NamedList* sect = cfg.getSection(i);
		if (!sect)
		    continue;
		String s(*sect);
		if (s.startSkip("group") && sect->getBoolValue("enabled",true))
		    m_groups.append(new ChanGroup(s,sect,chan1-1));
	    }
	    if (m_groups.count())
		Output("Created %d groups",m_groups.count());
	    setup();
	}
	else
	    Output("No spans created, module not activated");
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
