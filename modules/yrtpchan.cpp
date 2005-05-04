/**
 * yrtpchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * RTP channel - also acts as data helper for other protocols
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


#include <yatephone.h>
#include <yatertp.h>

#include <string.h>
#include <stdlib.h>


using namespace TelEngine;

/* Payloads for the AV profile */
static TokenDict dict_payloads[] = {
    { "mulaw",   0 },
    { "alaw",    8 },
    { "gsm",     3 },
    { "lpc10",   7 },
    { "slin",   11 },
    { "g726",    2 },
    { "g722",    9 },
    { "g723",    4 },
    { "g728",   15 },
    { "g729",   18 },
};

static TokenDict dict_yrtp_dir[] = {
    { "receive", RTPSession::RecvOnly },
    { "send", RTPSession::SendOnly },
    { "bidir", RTPSession::SendRecv },
    { 0 , 0 },
};

static Configuration s_cfg;

class YRTPWrapper : public RefObject
{
    friend class YRTPAudioSource;
    friend class YRTPAudioConsumer;
public:
    YRTPWrapper(const char *localip, CallEndpoint* conn = 0, RTPSession::Direction direction = RTPSession::SendRecv);
    ~YRTPWrapper();
    void setupRTP(const char* localip);
    bool startRTP(const char* raddr, unsigned int rport, int payload, const char* format);
    bool sendDTMF(char dtmf);
    void gotDTMF(char tone);
    inline CallEndpoint* conn() const
	{ return m_conn; }
    inline const String& id() const
	{ return m_id; }
    inline RTPSession* rtp() const
	{ return m_rtp; }
    inline RTPSession::Direction dir() const
	{ return m_dir; }
    inline unsigned int bufSize() const
	{ return m_bufsize; }
    inline unsigned int port() const
	{ return m_port; }
    inline void setMaster(const char* master)
	{ if (master) m_master = master; }
    static YRTPWrapper* find(const CallEndpoint* conn, RTPSession::Direction direction = RTPSession::SendRecv);
    static YRTPWrapper* find(const String& id);
    static String guessLocal(const char* remoteip);
private:
    RTPSession* m_rtp;
    RTPSession::Direction m_dir;
    CallEndpoint* m_conn;
    YRTPAudioSource* m_source;
    YRTPAudioConsumer* m_consumer;
    String m_id;
    String m_master;
    unsigned int m_bufsize;
    unsigned int m_port;
};

class YRTPAudioSource : public ThreadedSource
{
    friend class YRTPWrapper;
public:
    YRTPAudioSource(YRTPWrapper* wrap);
    ~YRTPAudioSource();
    virtual void run(void);
private:
    YRTPWrapper* m_wrap;
    gchar* m_buffer;
    DataBlock m_data;
};

class YRTPAudioConsumer : public DataConsumer
{
    friend class YRTPWrapper;
public:
    YRTPAudioConsumer(YRTPWrapper* wrap);
    ~YRTPAudioConsumer();
    virtual void Consume(const DataBlock &data, unsigned long timeDelta);
    inline int timestamp() const
	{ return m_timestamp; }
private:
    YRTPWrapper* m_wrap;
    int m_timestamp;
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message &msg);
};

class RtpHandler : public MessageHandler
{
public:
    RtpHandler() : MessageHandler("chan.rtp") { }
    virtual bool received(Message &msg);
};

class DTMFHandler : public MessageHandler
{
public:
    DTMFHandler() : MessageHandler("chan.dtmf",100) { }
    virtual bool received(Message &msg);
};

class YRTPPlugin : public Plugin
{
public:
    YRTPPlugin();
    virtual ~YRTPPlugin();
    virtual void initialize();
private:
    bool m_first;
};

static YRTPPlugin splugin;
static ObjList s_calls;
static Mutex s_mutex;

static void tel_event_cb(RtpSession* session,gint type,gpointer user_data)
{
    DDebug(DebugAll,"tel_event_cb(%p,%d,%p)",session,type,user_data);
    static const char dtmf[] = "0123456789*#ABCDF";
    if (user_data && (type >= 0) && (type <= 16))
	static_cast<YRTPWrapper*>(user_data)->gotDTMF(dtmf[type]);
}

static void tel_packet_cb(RtpSession* session,mblk_t* blk,gpointer user_data)
{
    DDebug(DebugAll,"tel_packet_cb(%p,%p,%p)",session,blk,user_data);
}

YRTPWrapper::YRTPWrapper(const char* localip, CallEndpoint* conn, RTPSession::Direction direction)
    : m_rtp(0), m_dir(direction), m_conn(conn),
      m_source(0), m_consumer(0), m_bufsize(0), m_port(0)
{
    Debug(DebugAll,"YRTPWrapper::YRTPWrapper(\"%s\",%p,%s) [%p]",
	localip,conn,lookup(direction,dict_yrtp_dir),this);
    m_id = "yrtp/";
    m_id << (unsigned int)::random();
    s_mutex.lock();
    s_calls.append(this);
    setupRTP(localip);
    s_mutex.unlock();
}

YRTPWrapper::~YRTPWrapper()
{
    Debug(DebugAll,"YRTPWrapper::~YRTPWrapper() %s [%p]",
	lookup(m_dir,dict_yrtp_dir),this);
    s_mutex.lock();
    s_calls.remove(this,false);
    if (m_source) {
	Debug(DebugGoOn,"There is still a RTP source %p [%p]",m_source,this);
	m_source->destruct();
	m_source = 0;
    }
    if (m_consumer) {
	Debug(DebugGoOn,"There is still a RTP consumer %p [%p]",m_consumer,this);
	m_consumer->destruct();
	m_consumer = 0;
    }
    if (m_rtp) {
	Debug(DebugAll,"Cleaning up RTP %p",m_rtp);
	RTPSession* tmp = m_rtp;
	m_rtp = 0;
	delete tmp;
    }
    s_mutex.unlock();
}

YRTPWrapper* YRTPWrapper::find(const CallEndpoint* conn, RTPSession::Direction direction)
{
    Lock lock(s_mutex);
    ObjList* l = &s_calls;
    for (; l; l=l->next()) {
	const YRTPWrapper *p = static_cast<const YRTPWrapper *>(l->get());
	if (p && (p->conn() == conn) && (p->dir() == direction))
	    return const_cast<YRTPWrapper *>(p);
    }
    return 0;
}

YRTPWrapper* YRTPWrapper::find(const String& id)
{
    Lock lock(s_mutex);
    ObjList* l = &s_calls;
    for (; l; l=l->next()) {
	const YRTPWrapper *p = static_cast<const YRTPWrapper *>(l->get());
	if (p && (p->id() == id))
	    return const_cast<YRTPWrapper *>(p);
    }
    return 0;
}

void YRTPWrapper::setupRTP(const char* localip)
{
    Debug(DebugAll,"YRTPWrapper::setupRTP(\"%s\") [%p]",localip,this);
    m_rtp = new RTPSession;
    int minport = s_cfg.getIntValue("rtp","minport",16384);
    int maxport = s_cfg.getIntValue("rtp","maxport",32768);
    int attempt = 10;
    if (minport > maxport) {
	int tmp = maxport;
	maxport = minport;
	minport = tmp;
    }
    else if (minport == maxport) {
	maxport++;
	attempt = 1;
    }
    for (; attempt; attempt--) {
	int lport = (minport + (::random() % (maxport - minport))) & 0xfffe;
	if (::rtp_session_set_local_addr(m_rtp, (gchar *)localip, lport) == 0) {
	    m_port = lport;
	    Debug(DebugAll,"YRTPWrapper [%p] RTP %p bound to %s:%u",this,m_rtp,localip,m_port);
	    return;
	}
    }
    Debug(DebugWarn,"YRTPWrapper [%p] RTP bind failed in range %d-%d",this,minport,maxport);
}

bool YRTPWrapper::startRTP(const char* raddr, unsigned int rport, int payload, const char* format)
{
    Debug(DebugAll,"YRTPWrapper::startRTP(\"%s\",%u,%d) [%p]",raddr,rport,payload,this);
    if (!m_rtp) {
	Debug(DebugWarn,"YRTPWrapper [%p] attempted to start RTP before setup!",this);
	return false;
    }

    if (m_bufsize) {
	Debug(DebugMild,"YRTPWrapper [%p] attempted to restart RTP!",this);
	return true;
    }

    if (!format)
	format = lookup(payload, dict_payloads);
    if (!format) {
	Debug(DebugWarn,"YRTPWrapper [%p] can't find name for payload %d",this,payload);
	return false;
    }

    if (payload == -1)
	payload = lookup(format, dict_payloads, -1);
    if (payload == -1) {
	Debug(DebugWarn,"YRTPWrapper [%p] can't find payload for format %s",this,format);
	return false;
    }

    if ((payload < 0) || (payload >= 127)) {
	Debug(DebugWarn,"YRTPWrapper [%p] received invalid payload %d",this,payload);
	return false;
    }

    Debug(DebugAll,"RTP format '%s' payload %d",format,payload);

    ::rtp_session_set_scheduling_mode(m_rtp, s_cfg.getBoolValue("rtp","scheduled",true)); /* yes */
    if (::rtp_session_set_remote_addr(m_rtp, (gchar *)raddr, rport)) {
	Debug(DebugWarn,"RTP failed to set remote address %s:%d [%p]",raddr,rport,this);
	return false;
    }
    if (::rtp_session_set_payload_type(m_rtp, payload)) {
	Debug(DebugWarn,"RTP failed to set payload type %d [%p]",payload,this);
	return false;
    }
    ::rtp_session_signal_connect(m_rtp,"telephone-event",(RtpCallback)tel_event_cb,this);
    ::rtp_session_signal_connect(m_rtp,"telephone-event_packet",(RtpCallback)tel_packet_cb,this);
    ::rtp_session_set_jitter_compensation(m_rtp, s_cfg.getIntValue("rtp","jitter",50));
    // Change format of source and/or consumer,
    //  reinstall them to rebuild codec chains
    if (m_source) {
	if (m_conn) {
	    m_source->ref();
	    m_conn->setSource();
	}
	m_source->m_format = format;
	if (m_conn) {
	    m_conn->setSource(m_source);
	    m_source->deref();
	}
	m_source->start("RTP Source");
    }
    if (m_consumer) {
	if (m_conn) {
	    m_consumer->ref();
	    m_conn->setConsumer();
	}
	m_consumer->m_format = format;
	if (m_conn) {
	    m_conn->setConsumer(m_consumer);
	    m_consumer->deref();
	}
    }
    m_bufsize = s_cfg.getIntValue("rtp","buffer",160);
    return true;
}

bool YRTPWrapper::sendDTMF(char dtmf)
{
    if (m_rtp && m_consumer) {
	::rtp_session_send_dtmf(m_rtp, dtmf, m_consumer->timestamp());
	return true;
    }
    return false;
}

void YRTPWrapper::gotDTMF(char tone)
{
    Debug(DebugInfo,"YRTPWrapper::gotDTMF('%c') [%p]",tone,this);
    if (m_master.null())
	return;
    char buf[2];
    buf[0] = tone;
    buf[1] = 0;
    Message *m = new Message("chan.masquerade");
    m->addParam("id",m_master);
    m->addParam("message","chan.dtmf");
    m->addParam("text",buf);
    Engine::enqueue(m);
}

String YRTPWrapper::guessLocal(const char* remoteip)
{
    String ret;
    struct sockaddr_in addr;
    if (remoteip && *remoteip && ::inet_aton(remoteip, &addr.sin_addr)) {
	addr.sin_family = AF_INET;
	addr.sin_port = htons(6666); // whatever
	int s = ::socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if (s != -1) {
	    if (0 == ::connect(s, (const sockaddr *)&addr, sizeof(addr))) {
		socklen_t len = sizeof(addr);
		if (0 == ::getsockname(s, (sockaddr *)&addr, &len))
		    ret = ::inet_ntoa(addr.sin_addr);
	    }
	    ::close(s);
	}
    }
    Debug(DebugInfo,"Guessed local IP '%s' for remote '%s'",ret.c_str(),remoteip);
    return ret;
}

YRTPAudioSource::YRTPAudioSource(YRTPWrapper* wrap)
    : m_wrap(wrap), m_buffer(0)
{
    Debug(DebugAll,"YRTPAudioSource::YRTPAudioSource(%p) [%p]",wrap,this);
    m_format.clear();
    if (m_wrap) {
	m_wrap->ref();
	m_wrap->m_source = this;
    }
}

YRTPAudioSource::~YRTPAudioSource()
{
    Debug(DebugAll,"YRTPAudioSource::~YRTPAudioSource() [%p] wrapper=%p",this,m_wrap);
    m_mutex.lock();
    stop();
    if (m_wrap) {
	YRTPWrapper* tmp = m_wrap;
	m_wrap = 0;
	tmp->m_source = 0;
	tmp->deref();
	Thread::yield();
    }
    m_data.clear(false);
    if (m_buffer) {
	::free(m_buffer);
	m_buffer = 0;
    }
    m_mutex.unlock();
}

void YRTPAudioSource::run(void)
{
    // wait until the rtp is fully initialized
    while (!m_wrap->bufSize())
	::usleep(20000);
    int bufsize = m_wrap->bufSize();
    int timestamp = 0;
    int have_more;
    m_buffer = (gchar*)::malloc(bufsize);
    ::memset(m_buffer, 0, bufsize);
    Debug(DebugAll,"YRTPAudioSource::run() entering loop [%p]",this);
    for (;;) {
	Lock lock(m_mutex);
	if (!(m_wrap && m_wrap->rtp()))
	    break;
	int rd = ::rtp_session_recv_with_ts(m_wrap->rtp(),
	    m_buffer, bufsize, timestamp, &have_more);
	if (rd > 0) {
	    XDebug(DebugAll,"YRTPAudioSource read %d bytes, ts=%d, more=%d [%p]",
		rd,timestamp,have_more,this);
	    m_data.assign(m_buffer, rd, false);
	    lock.drop();
	    // FIXME: find number of samples from format - if known...
	    Forward(m_data);
	    if (!m_wrap)
		break;
	    m_data.clear(false);
	    timestamp += bufsize;
	}
	lock.drop();
	Thread::yield();
    }
    Debug(DebugAll,"YRTPAudioSource::run() exiting loop [%p]",this);
}

YRTPAudioConsumer::YRTPAudioConsumer(YRTPWrapper *wrap)
    : m_wrap(wrap), m_timestamp(0)
{
    Debug(DebugAll,"YRTPAudioConsumer::YRTPAudioConsumer(%p) [%p]",wrap,this);
    m_format.clear();
    if (m_wrap) {
	m_wrap->ref();
	m_wrap->m_consumer = this;
    }
}

YRTPAudioConsumer::~YRTPAudioConsumer()
{
    Debug(DebugAll,"YRTPAudioConsumer::~YRTPAudioConsumer() [%p] wrapper=%p ts=%d",this,m_wrap,m_timestamp);
    if (m_wrap) {
	YRTPWrapper* tmp = m_wrap;
	m_wrap = 0;
	tmp->m_consumer = 0;
	tmp->deref();
    }
}

void YRTPAudioConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    if (!(m_wrap && m_wrap->bufSize() && m_wrap->rtp()))
	return;
    XDebug(DebugAll,"YRTPAudioConsumer writing %d bytes, ts=%d [%p]",
	data.length(),m_timestamp,this);
    ::rtp_session_send_with_ts(m_wrap->rtp(),(gchar *)data.data(),data.length(),m_timestamp);
    // if timestamp increment is not provided we have to guess...
    m_timestamp += timeDelta ? timeDelta : data.length();
}

bool AttachHandler::received(Message &msg)
{
    int more = 2;
    String src(msg.getValue("source"));
    if (src.null())
	more--;
    else {
	Regexp r("^rtp/\\(.*\\)$");
	if (src.matches(r)) {
	    src = src.matchString(1);
	    more--;
	}
	else
	    src = "";
    }

    String cons(msg.getValue("consumer"));
    if (cons.null())
	more--;
    else {
	Regexp r("^rtp/\\(.*\\)$");
	if (cons.matches(r)) {
	    cons = cons.matchString(2);
	    more--;
	}
	else
	    cons = "";
    }
    if (src.null() && cons.null())
	return false;

    String lip(msg.getValue("localip"));
    String rip(msg.getValue("remoteip"));
    String rport(msg.getValue("remoteport"));
    if (lip.null())
	lip = YRTPWrapper::guessLocal(rip);
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch) {
	if (!src.null())
	    Debug(DebugWarn,"RTP source '%s' attach request with no call channel!",src.c_str());
	if (!cons.null())
	    Debug(DebugWarn,"RTP consumer '%s' attach request with no call channel!",cons.c_str());
	return false;
    }

    YRTPWrapper *w = YRTPWrapper::find(ch);
    if (!w)
	w = YRTPWrapper::find(msg.getValue("rtpid"));
    if (!w) {
	w = new YRTPWrapper(lip,ch);
	w->setMaster(msg.getValue("id"));

	if (!src.null()) {
	    YRTPAudioSource* s = new YRTPAudioSource(w);
	    ch->setSource(s);
	    s->deref();
	}

	if (!cons.null()) {
	    YRTPAudioConsumer* c = new YRTPAudioConsumer(w);
	    ch->setConsumer(c);
	    c->deref();
	}
    }

    if (rip && rport) {
	String p(msg.getValue("payload"));
	if (p.null())
	    p = msg.getValue("format");
	w->startRTP(rip,rport.toInteger(),p.toInteger(dict_payloads,-1),msg.getValue("format"));
    }
    msg.setParam("localip",lip);
    msg.setParam("localport",String(w->port()));
    msg.setParam("rtpid",w->id());

    // Stop dispatching if we handled all requested
    return !more;
}

bool RtpHandler::received(Message &msg)
{
    Debug(DebugAll,"RTP message received");
    String dir(msg.getValue("direction"));
    RTPSession::Direction direction = RTPSession::SendRecv;
    bool d_recv = false;
    bool d_send = false;
    if (dir == "bidir") {
	d_recv = true;
	d_send = true;
    }
    else if (dir == "receive") {
	d_recv = true;
	direction = RTPSession::RecvOnly;
    }
    else if (dir == "send") {
	d_send = true;
	direction = RTPSession::SendOnly;
    }

    if (!(d_recv || d_send))
	return false;

    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch) {
	if (d_recv)
	    Debug(DebugWarn,"RTP recv request with no call channel!");
	if (d_send)
	    Debug(DebugWarn,"RTP send request with no call channel!");
	return false;
    }

    String rip(msg.getValue("remoteip"));
    String rport(msg.getValue("remoteport"));

    YRTPWrapper *w = YRTPWrapper::find(ch,direction);
    if (w)
	Debug(DebugAll,"YRTPWrapper %p found by CallEndpoint",w);
    if (!w) {
	w = YRTPWrapper::find(msg.getValue("rtpid"));
	if (w)
	    Debug(DebugAll,"YRTPWrapper %p found by ID",w);
    }
    if (!w) {
	String lip(msg.getValue("localip"));
	if (lip.null())
	    lip = YRTPWrapper::guessLocal(rip);
	if (lip.null()) {
	    Debug(DebugWarn,"RTP request with no local address!");
	    return false;
	}
	msg.setParam("localip",lip);

	w = new YRTPWrapper(lip,ch,direction);
	w->setMaster(msg.getValue("id"));

	if (d_recv) {
	    YRTPAudioSource* s = new YRTPAudioSource(w);
	    ch->setSource(s);
	    s->deref();
	}

	if (d_send) {
	    YRTPAudioConsumer* c = new YRTPAudioConsumer(w);
	    ch->setConsumer(c);
	    c->deref();
	}

	if (w->deref())
	    return false;
    }

    if (rip && rport) {
	String p(msg.getValue("payload"));
	if (p.null())
	    p = msg.getValue("format");
	w->startRTP(rip,rport.toInteger(),p.toInteger(dict_payloads,-1),msg.getValue("format"));
    }
    msg.setParam("localport",String(w->port()));
    msg.setParam("rtpid",w->id());

    return true;
}

bool DTMFHandler::received(Message &msg)
{
    String targetid(msg.getValue("targetid"));
    if (targetid.null())
	return false;
    String text(msg.getValue("text"));
    if (text.null())
	return false;
    YRTPWrapper* wrap = YRTPWrapper::find(targetid);
    if (wrap && wrap->rtp() && (::rtp_session_telephone_events_supported(wrap->rtp()) > 0)) {
	Debug(DebugInfo,"RTP DTMF '%s' targetid '%s'",text.c_str(),targetid.c_str());
	for (unsigned int i=0;i<text.length();i++)
	    wrap->sendDTMF(text[i]);
	return true;
    }
    return false;
}

YRTPPlugin::YRTPPlugin()
    : m_first(true)
{
    Output("Loaded module YRTP");
}

YRTPPlugin::~YRTPPlugin()
{
    Output("Unloading module YRTP");
    s_calls.clear();
}

void YRTPPlugin::initialize()
{
    Output("Initializing module YRTP");
    s_cfg = Engine::configFile("yrtpchan");
    s_cfg.load();
    if (m_first) {
	m_first = false;
//	rtp_profile_set_payload(&av_profile,96,&telephone_event);
	rtp_profile_set_payload(&av_profile,101,&telephone_event);
	Engine::install(new AttachHandler);
	Engine::install(new RtpHandler);
	Engine::install(new DTMFHandler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
