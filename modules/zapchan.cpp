/**
 * zapchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Zapata telephony driver
 */

extern "C" {
#include <libpri.h>
#include <linux/zaptel.h>
};
#include <unistd.h>
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

#define SIG_EM          ZT_SIG_EM
#define SIG_EMWINK      (0x10000 | ZT_SIG_EM)
#define SIG_FEATD       (0x20000 | ZT_SIG_EM)
#define SIG_FEATDMF     (0x40000 | ZT_SIG_EM)
#define SIG_FEATB       (0x80000 | ZT_SIG_EM)
#define SIG_FXSLS       ZT_SIG_FXSLS
#define SIG_FXSGS       ZT_SIG_FXSGS
#define SIG_FXSKS       ZT_SIG_FXSKS
#define SIG_FXOLS       ZT_SIG_FXOLS
#define SIG_FXOGS       ZT_SIG_FXOGS
#define SIG_FXOKS       ZT_SIG_FXOKS
#define SIG_PRI         ZT_SIG_CLEAR
#define SIG_R2          ZT_SIG_CAS
#define SIG_SF          ZT_SIG_SF
#define SIG_SFWINK      (0x10000 | ZT_SIG_SF)
#define SIG_SF_FEATD    (0x20000 | ZT_SIG_SF)
#define SIG_SF_FEATDMF  (0x40000 | ZT_SIG_SF)
#define SIG_SF_FEATB    (0x80000 | ZT_SIG_SF)

static int s_buflen = 480;

static int zt_get_event(int fd)
{
    /* Avoid the silly zt_getevent which ignores a bunch of events */
    int j = 0;
    if (::ioctl(fd, ZT_GETEVENT, &j) == -1)
	return -1;
    return j;
}

#if 0
static int zt_wait_event(int fd)
{
    /* Avoid the silly zt_waitevent which ignores a bunch of events */
    int i = ZT_IOMUX_SIGEVENT, j = 0;
    if (::ioctl(fd, ZT_IOMUX, &i) == -1)
	return -1;
    if (::ioctl(fd, ZT_GETEVENT, &j) == -1)
	return -1;
    return j;
}
#endif

static int zt_open(int channo, bool subchan,unsigned int blksize)
{
    Debug("ZapChan",DebugInfo,"Open zap channel=%d with block size=%d",channo,blksize);
    int fd = ::open(subchan ? "/dev/zap/pseudo" : "/dev/zap/channel",O_RDWR|O_NONBLOCK);
    if (fd < 0) {
	Debug("ZapChan",DebugFail,"Failed to open zap device: error %d: %s",errno,::strerror(errno));
	return -1;
    }
    if (channo) {
	if (::ioctl(fd, subchan ? ZT_CHANNO : ZT_SPECIFY, &channo)) {
	    Debug("ZapChan",DebugFail,"Failed to specify chan %d: error %d: %s",channo,errno,::strerror(errno));
	    ::close(fd);
	    return -1;
	}
    }
    if (blksize) {
	if (::ioctl(fd, ZT_SET_BLOCKSIZE, &blksize) == -1) {
	    Debug("ZapChan",DebugFail,"Failed to set block size %d: error %d: %s",blksize,errno,::strerror(errno));
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
    if (::ioctl(fd, ZT_SETLAW, &law) != -1)
	return true;

    Debug("ZapChan",DebugInfo,"Failed to set law %d: error %d: %s",law,errno,::strerror(errno));
    return false;
}

static void zt_close(int fd)
{
    if (fd != -1)
	::close(fd);
}

#if 0

static const char *pri_alarm(int alarm)
{
    if (!alarm)
	return "No alarm";
    if (alarm & ZT_ALARM_RED)
	return "Red Alarm";
    if (alarm & ZT_ALARM_YELLOW)
	return "Yellow Alarm";
    if (alarm & ZT_ALARM_BLUE)
	return "Blue Alarm";
    if (alarm & ZT_ALARM_RECOVER)
	return "Recovering";
    if (alarm & ZT_ALARM_LOOPBACK)
	return "Loopback";
    if (alarm & ZT_ALARM_NOTOPEN)
	return "Not Open";
    return "Unknown status";
}

static const char *sig_names(int sig)
{
    switch (sig) {
	case SIG_EM:
	    return "E & M Immediate";
	case SIG_EMWINK:
	    return "E & M Wink";
	case SIG_FEATD:
	    return "Feature Group D (DTMF)";
	case SIG_FEATDMF:
	    return "Feature Group D (MF)";
	case SIG_FEATB:
	    return "Feature Group B (MF)";
	case SIG_FXSLS:
	    return "FXS Loopstart";
	case SIG_FXSGS:
	    return "FXS Groundstart";
	case SIG_FXSKS:
	    return "FXS Kewlstart";
	case SIG_FXOLS:
	    return "FXO Loopstart";
	case SIG_FXOGS:
	    return "FXO Groundstart";
	case SIG_FXOKS:
	    return "FXO Kewlstart";
	case SIG_PRI:
	    return "PRI Signalling";
	case SIG_R2:
	    return "R2 Signalling";
	case SIG_SF:
	    return "SF (Tone) Signalling Immediate";
	case SIG_SFWINK:
	    return "SF (Tone) Signalling Wink";
	case SIG_SF_FEATD:
	    return "SF (Tone) Signalling with Feature Group D (DTMF)";
	case SIG_SF_FEATDMF:
	    return "SF (Tone) Signallong with Feature Group D (MF)";
	case SIG_SF_FEATB:
	    return "SF (Tone) Signalling with Feature Group B (MF)";
	case 0:
	    return "Pseudo Signalling";
	default:
	    static char buf[64];
	    ::sprintf(buf,"Unknown signalling %d",sig);
	    return buf;
    }
}

#endif

static void pri_err_cb(char *s)
{
    Debug("PRI",DebugWarn,"%s",s);
}

static void pri_msg_cb(char *s)
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

/* Network Specific Facilities (AT&T) */
static TokenDict dict_str2nsf[] = {
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
    { 0, -1 }
};

/* Layer 1 formats */
static TokenDict dict_str2law[] = {
    { "mulaw", PRI_LAYER_1_ULAW },
    { "alaw", PRI_LAYER_1_ALAW },
    { "g721", PRI_LAYER_1_G721 },
    { 0, -1 }
};

/* Zaptel formats */
static TokenDict dict_str2ztlaw[] = {
    { "slin", -1 },
    { "default", ZT_LAW_DEFAULT },
    { "mulaw", ZT_LAW_MULAW },
    { "alaw", ZT_LAW_ALAW },
    { 0, -2 }
};

class ZapChan;

class PriSpan : public GenObject, public Thread
{
public:
    static PriSpan *create(int span, int chan1, int nChans, int dChan, bool isNet,
			   int switchType, int dialPlan, int presentation, int nsf = PRI_NSF_NONE);
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
    inline int dplan() const
	{ return m_dplan; }
    inline int pres() const
	{ return m_pres; }
    inline bool outOfOrder() const
	{ return !m_ok; }
    int findEmptyChan(int first = 0, int last = 65535) const;
    ZapChan *getChan(int chan) const;
    void idle();
    static unsigned long long restartPeriod;
    static bool dumpEvents;

private:
    PriSpan(struct pri *_pri, int span, int first, int chans, int dchan, int fd, int dplan, int pres);
    static struct pri *makePri(int fd, int dchan, int nettype, int swtype, int nsf);
    void handleEvent(pri_event &ev);
    bool validChan(int chan) const;
    void restartChan(int chan, bool force = false);
    void ringChan(int chan, pri_event_ring &ev);
    void infoChan(int chan, pri_event_ring &ev);
    void hangupChan(int chan,pri_event_hangup &ev);
    void ackChan(int chan);
    void proceedingChan(int chan);
    int m_span;
    int m_offs;
    int m_nchans;
    int m_fd;
    int m_dplan;
    int m_pres;
    struct pri *m_pri;
    unsigned long long m_restart;
    ZapChan **m_chans;
    bool m_ok;
};

class ZapChan : public DataEndpoint
{
public:
    ZapChan(PriSpan *parent, int chan, unsigned int bufsize);
    virtual ~ZapChan();
    virtual void disconnected();
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
    bool call(Message &msg, const char *called = 0);
    bool answer();
    void idle();
    void restart();
    bool open(int defLaw = -1);
    inline void setTimeout(unsigned long long tout)
	{ m_timeout = tout ? Time::now()+tout : 0; }
    const char *status() const;
    inline int fd() const
	{ return m_fd; }
    inline int law() const
	{ return m_law; }
private:
    PriSpan *m_span;
    int m_chan;
    bool m_ring;
    unsigned long long m_timeout;
    q931_call *m_call;
    unsigned int m_bufsize;
    int m_abschan;
    int m_fd;
    int m_law;
    bool m_isdn;
};

class ZapSource : public ThreadedSource
{
public:
    ZapSource(ZapChan *owner,unsigned int bufsize)
	: m_owner(owner), m_bufsize(bufsize)
	{
	    Debug(DebugAll,"ZapSource::ZapSource(%p) [%p]",owner,this);
	    start("ZapSource");
	}

    ~ZapSource()
	{ Debug(DebugAll,"ZapSource::~ZapSource() [%p]",this); }

    virtual void run();

private:
    ZapChan *m_owner;
    unsigned int m_bufsize;
};

class ZapConsumer : public DataConsumer
{
public:
    ZapConsumer(ZapChan *owner,unsigned int bufsize)
	: m_owner(owner), m_bufsize(bufsize)
	{ Debug(DebugAll,"ZapConsumer::ZapConsumer(%p) [%p]",owner,this); }

    ~ZapConsumer()
	{ Debug(DebugAll,"ZapConsumer::~ZapConsumer() [%p]",this); }

    virtual void Consume(const DataBlock &data, unsigned long timeDelta);

private:
    ZapChan *m_owner;
    unsigned int m_bufsize;
    DataBlock m_buffer;
};

class ZapHandler : public MessageHandler
{
public:
    ZapHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class ZapDropper : public MessageHandler
{
public:
    ZapDropper(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("status") { }
    virtual bool received(Message &msg);
};

class ZaptelPlugin : public Plugin
{
    friend class PriSpan;
    friend class ZapHandler;
public:
    ZaptelPlugin();
    virtual ~ZaptelPlugin();
    virtual void initialize();
    PriSpan *findSpan(int chan);
    ZapChan *findChan(const char *id);
    ZapChan *findChan(int first = -1, int last = -1);
    ObjList m_spans;
};

ZaptelPlugin zplugin;
unsigned long long PriSpan::restartPeriod = 0;
bool PriSpan::dumpEvents = false;

PriSpan *PriSpan::create(int span, int chan1, int nChans, int dChan, bool isNet,
			 int switchType, int dialPlan, int presentation, int nsf)
{
    int fd = ::open("/dev/zap/channel", O_RDWR, 0600);
    if (fd < 0)
	return 0;
    struct pri *p = makePri(fd,
	(dChan >= 0) ? dChan+chan1-1 : -1,
	(isNet ? PRI_NETWORK : PRI_CPE),
	switchType, nsf);
    if (!p) {
	::close(fd);
	return 0;
    }
    return new PriSpan(p,span,chan1,nChans,dChan,fd,dialPlan,presentation);
}

struct pri *PriSpan::makePri(int fd, int dchan, int nettype, int swtype, int nsf)
{
    if (dchan >= 0) {
	// Set up the D channel if we have one
	if (::ioctl(fd,ZT_SPECIFY,&dchan) == -1) {
	    Debug("PriSpan",DebugFail,"Failed to open D-channel %d: error %d: %s",
		dchan,errno,::strerror(errno));
	    return 0;
	}
	ZT_PARAMS par;
	if (::ioctl(fd, ZT_GET_PARAMS, &par) == -1) {
	    Debug("PriSpan",DebugFail,"Failed to get parameters of D-channel %d: error %d: %s",
		dchan,errno,::strerror(errno));
	    return 0;
	}
	if (par.sigtype != ZT_SIG_HDLCFCS) {
	    Debug("PriSpan",DebugFail,"D-channel %d is not in HDLC/FCS mode",dchan);
	    return 0;
	}
	ZT_BUFFERINFO bi;
	bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.numbufs = 16;
	bi.bufsize = 1024;
	if (::ioctl(fd, ZT_SET_BUFINFO, &bi) == -1) {
	    Debug("PriSpan",DebugFail,"Could not set buffering on D-channel %d",dchan);
	    return 0;
	}
    }
    struct pri *ret = ::pri_new(fd, nettype, swtype);
    if (ret)
	::pri_set_nsf(ret, nsf);
    return ret;
}

PriSpan::PriSpan(struct pri *_pri, int span, int first, int chans, int dchan, int fd, int dplan, int pres)
    : Thread("PriSpan"), m_span(span), m_offs(first), m_nchans(chans),
      m_fd(fd), m_dplan(dplan), m_pres(pres), m_pri(_pri),
      m_restart(0), m_chans(0), m_ok(false)
{
    Debug(DebugAll,"PriSpan::PriSpan(%p,%d,%d,%d) [%p]",_pri,span,chans,fd,this);
    ZapChan **ch = new (ZapChan *)[chans];
    for (int i = 1; i <= chans; i++)
	ch[i-1] = (i == dchan) ? 0 : new ZapChan(this,i,s_buflen);
    m_chans = ch;
    zplugin.m_spans.append(this);
}

PriSpan::~PriSpan()
{
    Debug(DebugAll,"PriSpan::~PriSpan() [%p]",this);
    zplugin.m_spans.remove(this,false);
    m_ok = false;
    for (int i = 0; i <m_nchans; i++) {
	ZapChan *c = m_chans[i];
	m_chans[i] = 0;
	if (c) {
	    c->hangup(PRI_CAUSE_NORMAL_UNSPECIFIED);
	    c->destruct();
	}
    }
    delete m_chans;
    ::close(m_fd);
}

void PriSpan::run()
{
    Debug(DebugAll,"PriSpan::run() [%p]",this);
    m_restart = Time::now() + restartPeriod;
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
	if (!sel) {
	    ev = ::pri_schedule_run(m_pri);
	    idle();
	}
	else if (sel > 0)
	    ev = ::pri_check_event(m_pri);
	else if (errno != EINTR)
	    Debug("PriSpan",DebugFail,"select() error %d: %s",
		errno,::strerror(errno));
	if (ev) {
	    if (dumpEvents && debugAt(DebugAll))
		::pri_dump_event(m_pri, ev);
	    handleEvent(*ev);
	}
	else {
	    int zev = zt_get_event(m_fd);
	    if (zev)
		Debug("PriSpan",DebugInfo,"Zapata event %d",zev);
	}
    }
}

void PriSpan::idle()
{
    if (restartPeriod && (Time::now() > m_restart)) {
	m_restart = Time::now() + restartPeriod;
	Debug("PriSpan",DebugInfo,"Restarting idle channels on span %d",m_span);
	for (int i=1; i<m_nchans; i++)
	    restartChan(i);
    }
    if (!m_chans)
	return;
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
	    break;
	case PRI_EVENT_DCHAN_DOWN:
	    Debug(DebugWarn,"D-channel down on span %d",m_span);
	    m_ok = false;
	    for (int i=1; i<m_nchans; i++)
		if (m_chans[i])
		    m_chans[i]->hangup(PRI_CAUSE_NETWORK_OUT_OF_ORDER);
	    break;
	case PRI_EVENT_RESTART:
	    restartChan(ev.restart.channel,true);
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

ZapChan *PriSpan::getChan(int chan) const
{
    return validChan(chan) ? m_chans[chan-1] : 0;
}

void PriSpan::restartChan(int chan, bool force)
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
	Debug(DebugInfo,"Restarting B-channel %d on span %d",chan,m_span);
	getChan(chan)->restart();
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
    getChan(chan)->ring(ev.call);
    Debug(DebugInfo,"caller='%s' callerno='%s' callingplan=%d",
	ev.callingname,ev.callingnum,ev.callingplan);
    Debug(DebugInfo,"callednum='%s' redirectnum='%s' calledplan=%d",
	ev.callednum,ev.redirectingnum,ev.calledplan);
    Debug(DebugInfo,"type=%d complete=%d format='%s'",
	ev.ctype,ev.complete,lookup(ev.layer1,dict_str2law,"unknown"));
    Message *m = new Message("ring");
    m->addParam("driver","zap");
    m->addParam("span",String(m_span));
    m->addParam("channel",String(chan));
    if (ev.callingnum[0])
	m->addParam("caller",ev.callingnum);
    if (ev.callednum[0])
	m->addParam("called",ev.callednum);
    Engine::dispatch(m);
    *m = "preroute";
    Engine::dispatch(m);
    *m = "route";
    if (Engine::dispatch(m)) {
	*m = "call";
	m->addParam("callto",m->retValue());
	m->retValue() = 0;
	int dataLaw = -1;
	switch (ev.layer1) {
	    case PRI_LAYER_1_ALAW:
		dataLaw = ZT_LAW_ALAW;
		break;
	    case PRI_LAYER_1_ULAW:
		dataLaw = ZT_LAW_MULAW;
		break;
	}
	getChan(chan)->open(dataLaw);
	m->userData(getChan(chan));
	if (Engine::dispatch(m))
	    getChan(chan)->answer();
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

void PriSpan::proceedingChan(int chan)
{
    if (!validChan(chan)) {
	Debug(DebugInfo,"Proceeding on invalid channel %d on span %d",chan,m_span);
	return;
    }
    Debug(DebugInfo,"Extending timeout on channel %d on span %d",chan,m_span);
    getChan(chan)->setTimeout(60000000);
}

void ZapSource::run()
{
    DataBlock buf(0,m_bufsize);
    DataBlock data;
    for (;;) {
	Thread::yield();
	int fd = m_owner->fd();
	if (fd != -1) {
	    int rd = ::read(fd,buf.data(),buf.length());
#ifdef DEBUG
	    Debug(DebugAll,"ZapSource read %d bytes",rd);
#endif
	    if (rd > 0) {
		switch (m_owner->law()) {
		    case -1:
			data.assign(buf.data(),rd);
			Forward(data,rd/2);
			break;
		    case ZT_LAW_MULAW:
			data.convert(buf,"mulaw","slin",rd);
			Forward(data,rd);
			break;
		    case ZT_LAW_ALAW:
			data.convert(buf,"alaw","slin",rd);
			Forward(data,rd);
			break;
		}
	    }
	    else if (rd < 0) {
		if ((errno != EAGAIN) && (errno != EINTR))
		    break;
	    }
	    else
		break;
	}
    }
}

void ZapConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    int fd = m_owner->fd();
#ifdef DEBUG
    Debug(DebugAll,"ZapConsumer fd=%d datalen=%u",fd,data.length());
#endif
    if ((fd != -1) && !data.null()) {
	DataBlock blk;
	switch (m_owner->law()) {
	    case -1:
		blk = data;
		break;
	    case ZT_LAW_MULAW:
		blk.convert(data,"slin","mulaw");
		break;
	    case ZT_LAW_ALAW:
		blk.convert(data,"slin","alaw");
		break;
	    default:
		return;
	}
	if (m_buffer.length()+blk.length() <= m_bufsize*4)
	    m_buffer += blk;
	else
	    Debug("ZapConsumer",DebugAll,"Skipped %u bytes, buffer is full",blk.length());
	if (m_buffer.null())
	    return;
	if (m_buffer.length() >= m_bufsize) {
	    int wr = ::write(fd,m_buffer.data(),m_bufsize);
	    if (wr < 0) {
		if ((errno != EAGAIN) && (errno != EINTR))
		Debug(DebugFail,"ZapConsumer write error %d: %s",
		    errno,::strerror(errno));
	    }
	    else {
		if ((unsigned)wr != m_bufsize)
		    Debug("ZapConsumer",DebugInfo,"Short write, %d of %u bytes",wr,m_bufsize);
		m_buffer.cut(-wr);
	    }
	}
    }
}

ZapChan::ZapChan(PriSpan *parent, int chan, unsigned int bufsize)
    : DataEndpoint("zaptel"), m_span(parent), m_chan(chan), m_ring(false),
      m_timeout(0), m_call(0), m_bufsize(bufsize), m_fd(-1), m_law(-1)
{
    Debug(DebugAll,"ZapChan::ZapChan(%p,%d) [%p]",parent,chan,this);
    // I hate counting from one...
    m_abschan = m_chan+m_span->chan1()-1;
    m_isdn = true;
}

ZapChan::~ZapChan()
{
    Debug(DebugAll,"ZapChan::~ZapChan() [%p] %d",this,m_chan);
    hangup(PRI_CAUSE_NORMAL_UNSPECIFIED);
}

void ZapChan::disconnected()
{
    Debugger debug("ZapChan::disconnected()");
    hangup(PRI_CAUSE_NORMAL_CLEARING);
}

bool ZapChan::nativeConnect(DataEndpoint *peer)
{
#if 0
    ZapChan *zap = static_cast<ZapChan *>(peer);
    if ((m_fd < 0) || !zap || (zap->fd() < 0))
	return false;
    ZT_CONFINFO conf;
    conf.confmode = ZT_CONF_DIGITALMON;
    conf.confno = zap->absChan();
    if (ioctl(m_fd, ZT_SETCONF, &conf))
	return false;
    conf.confno = zap->absChan();
    if (ioctl(m_fd, ZT_SETCONF, &conf))
        return false;
#endif
    return false;
}

const char *ZapChan::status() const
{
    if (m_ring)
	return "ringing";
    if (m_call)
	return m_timeout ? "calling" : "connected";
    return "idle";
}

void ZapChan::idle()
{
    if (m_timeout && (Time::now() > m_timeout)) {
	Debug("ZapChan",DebugWarn,"Timeout %s channel %d on span %d",
	    status(),m_chan,m_span->span());
	m_timeout = 0;
	hangup(PRI_CAUSE_RECOVERY_ON_TIMER_EXPIRE);
    }
}

void ZapChan::restart()
{
    disconnect();
    setSource();
    setConsumer();
    ::pri_reset(m_span->pri(),m_chan);
}

bool ZapChan::open(int defLaw)
{
    m_fd = zt_open(m_abschan,false,m_bufsize);
    if (m_fd == -1)
	return false;
    setSource(new ZapSource(this,m_bufsize));
    getSource()->deref();
    setConsumer(new ZapConsumer(this,m_bufsize));
    getConsumer()->deref();
    if (zt_set_law(m_fd,defLaw)) {
	m_law = defLaw;
	Debug(DebugInfo,"Opened Zap channel %d, law is: %s (desired)",m_abschan,lookup(m_law,dict_str2ztlaw,"unknown"));
	return true;
    }
    int laws[3];
    laws[0] = -1;
    if (m_span->chans() > 24) {
	laws[1] = ZT_LAW_ALAW;
	laws[2] = ZT_LAW_MULAW;
    }
    else {
	laws[1] = ZT_LAW_MULAW;
	laws[2] = ZT_LAW_ALAW;
    }
    for (int l=0; l<3;l++)
	if ((laws[l] != defLaw) && zt_set_law(m_fd,laws[l])) {
	    m_law = laws[l];
	    Debug(DebugInfo,"Opened Zap channel %d, law is: %s (fallback)",m_abschan,lookup(m_law,dict_str2ztlaw,"unknown"));
	    return true;
	}
    setSource();
    setConsumer();
    zt_close(m_fd);
    m_fd = -1;
    Debug(DebugFail,"Unable to set zap to any known format");
    return false;
}

bool ZapChan::answer()
{
    if (!m_ring) {
	Debug("ZapChan",DebugWarn,"Answer request on %s channel %d on span %d",
	    status(),m_chan,m_span->span());
	return false;
    }
    m_ring = false;
    m_timeout = 0;
    Output("Answering on zap/%d (%d/%d)",m_abschan,m_span->span(),m_chan);
    ::pri_answer(m_span->pri(),m_call,m_chan,!m_isdn);
    Message *m = new Message("answer");
    m->addParam("driver","zap");
    m->addParam("span",String(m_span->span()));
    m->addParam("channel",String(m_chan));
    Engine::enqueue(m);
    return true;
}

void ZapChan::hangup(int cause)
{
    m_ring = false;
    m_timeout = 0;
    if (m_call) {
	::pri_hangup(m_span->pri(),m_call,cause);
	::pri_destroycall(m_span->pri(),m_call);
	m_call = 0;
	Message *m = new Message("hangup");
	m->addParam("driver","zap");
	m->addParam("span",String(m_span->span()));
	m->addParam("channel",String(m_chan));
	Engine::enqueue(m);
    }
    disconnect();
    setSource();
    setConsumer();
    zt_close(m_fd);
    m_fd = -1;
}

void ZapChan::sendDigit(char digit)
{
    if (m_call)
	::pri_information(m_span->pri(),m_call,digit);
}

bool ZapChan::call(Message &msg, const char *called)
{
    if (m_span->outOfOrder()) {
	Debug("ZapChan",DebugInfo,"Span %d is out of order, failing call",m_span->span());
	return false;
    }
    if (!called)
	called = msg.getValue("called");
    Debug("ZapChan",DebugInfo,"Calling '%s' on channel %d span %d",
	called, m_chan,m_span->span());
    int layer1 = lookup(msg.getValue("dataformat"),dict_str2law,0);
    hangup(PRI_CAUSE_PRE_EMPTED);
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd) {
	int dataLaw = -1;
	switch (layer1) {
	    case PRI_LAYER_1_ALAW:
		dataLaw = ZT_LAW_ALAW;
		break;
	    case PRI_LAYER_1_ULAW:
		dataLaw = ZT_LAW_MULAW;
		break;
	}
	open(dataLaw);
	connect(dd);
    }
    else
	msg.userData(this);
    Output("Calling '%s' on zap/%d (%d/%d)",called,m_abschan,m_span->span(),m_chan);
    m_call =::pri_new_call(span()->pri());
    ::pri_call(m_span->pri(),m_call,0/*transmode*/,m_chan,1/*exclusive*/,!m_isdn,
	(char *)msg.getValue("caller"),
	lookup(msg.getValue("callerplan"),dict_str2dplan,m_span->dplan()),
	(char *)msg.getValue("callername"),
	lookup(msg.getValue("callerpres"),dict_str2pres,m_span->pres()),
	(char *)called,
	lookup(msg.getValue("calledplan"),dict_str2dplan,m_span->dplan()),
	layer1);
    setTimeout(10000000);
    return true;
}

void ZapChan::ring(q931_call *call)
{
    m_call = call;
    if (call) {
	setTimeout(10000000);
	m_ring = true;
	::pri_acknowledge(m_span->pri(),m_call,m_chan,0);
    }
    else
	hangup(PRI_CAUSE_WRONG_CALL_STATE);
}

bool ZapHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^zap/\\([^/]*\\)/\\?\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (!msg.userData()) {
	Debug(DebugFail,"Zaptel call found but no data channel!");
	return false;
    }
    String chan = dest.matchString(1);
    String num = dest.matchString(2);
#ifdef DEBUG
    Debug(DebugInfo,"Found call to Zaptel chan='%s' name='%s'",
	chan.c_str(),num.c_str());
#endif
    ZapChan *c = 0;

    r = "^\\([0-9]\\+\\)-\\([0-9]*\\)$";
    if (chan.matches(r))
	c = zplugin.findChan(chan.matchString(1).toInteger(),
	    chan.matchString(2).toInteger(65535));
    else
	c = zplugin.findChan(chan.toInteger(-1));

    if (c) {
	Debug(DebugInfo,"Will call '%s' on chan zap/%d (%d/%d)",
	    num.c_str(),c->absChan(),c->span()->span(),c->chan());
	return c->call(msg,num);
    }
    else
	Debug(DebugWarn,"Invalid Zaptel channel '%s'",chan.c_str());
    return false;
}

bool ZapDropper::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (id.null()) {
        Debug("ZapDropper",DebugInfo,"Dropping all calls");
	const ObjList *l = &zplugin.m_spans;
	for (; l; l=l->next()) {
	    PriSpan *s = static_cast<PriSpan *>(l->get());
	    if (s) {
		for (int n=1; n<=s->chans(); n++) {
		    ZapChan *c = s->getChan(n);
		    if (c)
			c->hangup(PRI_CAUSE_INTERWORKING);
		}
	    }
	}
        return false;
    }
    if (!id.startsWith("zap/"))
        return false;
    ZapChan *c = 0;
    id >> "zap/";
    int n = id.toInteger();
    if ((n > 0) && (c = zplugin.findChan(n))) {
        Debug("ZapDropper",DebugInfo,"Dropping zap/%d (%d/%d)",
	    n,c->span()->span(),c->chan());
	c->hangup(PRI_CAUSE_INTERWORKING);
	return true;
    }
    Debug("ZapDropper",DebugInfo,"Could not find zap/%s",id.c_str());
    return false;
}

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"zapchan") && ::strcmp(sel,"fixchans"))
	return false;
    String st("zapchan,type=fixchans,spans=");
    st << zplugin.m_spans.count() << ",[LIST]";
    const ObjList *l = &zplugin.m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s) {
	    for (int n=1; n<=s->chans(); n++) {
		ZapChan *c = s->getChan(n);
		if (c) {
		    st << ",zap/" << c->absChan() << "=";
		    st << s->span() << "/" << n << "/" << c->status();
		}
	    }
	}
    }
    msg.retValue() << st << "\n";
    return false;
}

ZaptelPlugin::ZaptelPlugin()
{
    Output("Loaded module Zaptel");
    ::pri_set_error(pri_err_cb);
    ::pri_set_message(pri_msg_cb);
}

ZaptelPlugin::~ZaptelPlugin()
{
    Output("Unloading module Zaptel");
}

PriSpan *ZaptelPlugin::findSpan(int chan)
{
    const ObjList *l = &m_spans;
    for (; l; l=l->next()) {
	PriSpan *s = static_cast<PriSpan *>(l->get());
	if (s && s->belongs(chan))
	    return s;
    }
    return 0;
}

ZapChan *ZaptelPlugin::findChan(const char *id)
{
    String s(id);
    if (!s.startsWith("zap/"))
        return 0;
    s >> "zap/";
    int n = s.toInteger();
    return (n > 0) ? findChan(n) : 0;
}

ZapChan *ZaptelPlugin::findChan(int first, int last)
{
#ifdef DEBUG
    Debug(DebugAll,"ZaptelPlugin::findChan(%d,%d)",first,last);
#endif
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

void ZaptelPlugin::initialize()
{
    Output("Initializing module Zaptel");
    Configuration cfg(Engine::configFile("zapchan"));
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
		chan1 = cfg.getIntValue(sect,"first",chan1);
		PriSpan::create(span,chan1,num,
		    cfg.getIntValue(sect,"dchan", num > 24 ? 16 : -1),
		    cfg.getBoolValue(sect,"isnet",true),
		    cfg.getIntValue(sect,"swtype",dict_str2switch,
			PRI_SWITCH_UNKNOWN),
		    cfg.getIntValue(sect,"dialplan",dict_str2dplan,
			PRI_UNKNOWN),
		    cfg.getIntValue(sect,"presentation",dict_str2pres,
			PRES_ALLOWED_USER_NUMBER_NOT_SCREENED),
		    cfg.getIntValue(sect,"facilities",dict_str2nsf,
			PRI_NSF_NONE)
		);
		chan1 += num;
	    }
	}
	if (m_spans.count()) {
	    Output("Created %d spans",m_spans.count());
	    Engine::install(new ZapHandler("call"));
	    Engine::install(new ZapDropper("drop"));
	    Engine::install(new StatusHandler);
	}
	else
	    Output("No spans created, module not activated");
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
