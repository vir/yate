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
    { "h261",   31 },
    { "h263",   34 },
    { "mpv",    32 },
    { 0 , 0 },
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
    friend class YRTPSource;
    friend class YRTPConsumer;
    friend class YRTPSession;
public:
    YRTPWrapper(const char *localip, CallEndpoint* conn = 0, const char* media = "audio", RTPSession::Direction direction = RTPSession::SendRecv);
    ~YRTPWrapper();
    void setupRTP(const char* localip);
    bool startRTP(const char* raddr, unsigned int rport, int payload, int evpayload, const char* format);
    bool sendDTMF(char dtmf, int duration = 0);
    void gotDTMF(char tone);
    inline YRTPSession* rtp() const
	{ return m_rtp; }
    inline RTPSession::Direction dir() const
	{ return m_dir; }
    inline CallEndpoint* conn() const
	{ return m_conn; }
    inline const String& id() const
	{ return m_id; }
    inline const String& media() const
	{ return m_media; }
    inline unsigned int bufSize() const
	{ return m_bufsize; }
    inline unsigned int port() const
	{ return m_port; }
    inline void setMaster(const char* master)
	{ if (master) m_master = master; }
    void addDirection(RTPSession::Direction direction);
    static YRTPWrapper* find(const CallEndpoint* conn, const String& media);
    static YRTPWrapper* find(const String& id);
    static void guessLocal(const char* remoteip, String& localip);
private:
    YRTPSession* m_rtp;
    RTPSession::Direction m_dir;
    CallEndpoint* m_conn;
    YRTPSource* m_source;
    YRTPConsumer* m_consumer;
    String m_id;
    String m_media;
    String m_master;
    unsigned int m_bufsize;
    unsigned int m_port;
};

class YRTPSession : public RTPSession
{
public:
    inline YRTPSession(YRTPWrapper* wrap)
	: m_wrap(wrap), m_resync(false)
	{ }
    virtual bool rtpRecvData(bool marker, unsigned int timestamp,
	const void* data, int len);
    virtual bool rtpRecvEvent(int event, char key, int duration,
	int volume, unsigned int timestamp);
    virtual void rtpNewPayload(int payload, unsigned int timestamp);
    virtual void rtpNewSSRC(u_int32_t newSsrc);
    inline void resync()
	{ m_resync = true; }
private:
    YRTPWrapper* m_wrap;
    bool m_resync;
};

class YRTPSource : public DataSource
{
    friend class YRTPWrapper;
public:
    YRTPSource(YRTPWrapper* wrap);
    ~YRTPSource();
private:
    YRTPWrapper* m_wrap;
};

class YRTPConsumer : public DataConsumer
{
    friend class YRTPWrapper;
public:
    YRTPConsumer(YRTPWrapper* wrap);
    ~YRTPConsumer();
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
    DTMFHandler() : MessageHandler("chan.dtmf",150) { }
    virtual bool received(Message &msg);
};

class YRTPPlugin : public Module
{
public:
    YRTPPlugin();
    virtual ~YRTPPlugin();
    virtual void initialize();
    virtual void statusParams(String& str);
private:
    bool m_first;
};

static YRTPPlugin splugin;
static ObjList s_calls;
static Mutex s_mutex;

YRTPWrapper::YRTPWrapper(const char* localip, CallEndpoint* conn, const char* media, RTPSession::Direction direction)
    : m_rtp(0), m_dir(direction), m_conn(conn),
      m_source(0), m_consumer(0), m_media(media),
      m_bufsize(0), m_port(0)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::YRTPWrapper('%s',%p,'%s',%s) [%p]",
	localip,conn,media,lookup(direction,dict_yrtp_dir),this);
    m_id = "yrtp/";
    m_id << (unsigned int)::random();
    s_mutex.lock();
    s_calls.append(this);
    setupRTP(localip);
    s_mutex.unlock();
}

YRTPWrapper::~YRTPWrapper()
{
    Debug(&splugin,DebugAll,"YRTPWrapper::~YRTPWrapper() %s [%p]",
	lookup(m_dir,dict_yrtp_dir),this);
    s_mutex.lock();
    s_calls.remove(this,false);
    if (m_rtp) {
	Debug(DebugAll,"Cleaning up RTP %p",m_rtp);
	YRTPSession* tmp = m_rtp;
	m_rtp = 0;
	delete tmp;
    }
    if (m_source) {
	Debug(&splugin,DebugGoOn,"There is still a RTP source %p [%p]",m_source,this);
	m_source->destruct();
	m_source = 0;
    }
    if (m_consumer) {
	Debug(&splugin,DebugGoOn,"There is still a RTP consumer %p [%p]",m_consumer,this);
	m_consumer->destruct();
	m_consumer = 0;
    }
    s_mutex.unlock();
}

YRTPWrapper* YRTPWrapper::find(const CallEndpoint* conn, const String& media)
{
    Lock lock(s_mutex);
    ObjList* l = &s_calls;
    for (; l; l=l->next()) {
	const YRTPWrapper *p = static_cast<const YRTPWrapper *>(l->get());
	if (p && (p->conn() == conn) && (p->media() == media))
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
    Debug(&splugin,DebugAll,"YRTPWrapper::setupRTP(\"%s\") [%p]",localip,this);
    m_rtp = new YRTPSession(this);
    m_rtp->initTransport();
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
    SocketAddr addr(AF_INET);
    if (!addr.host(localip)) {
	Debug(&splugin,DebugWarn,"YRTPWrapper [%p] could not parse address '%s'",this,localip);
	return;
    }
    for (; attempt; attempt--) {
	int lport = (minport + (::random() % (maxport - minport))) & 0xfffe;
	addr.port(lport);
	if (m_rtp->localAddr(addr)) {
	    m_port = lport;
	    Debug(&splugin,DebugAll,"YRTPWrapper [%p] RTP %p bound to %s:%u",this,m_rtp,localip,m_port);
	    return;
	}
    }
    Debug(&splugin,DebugWarn,"YRTPWrapper [%p] RTP bind failed in range %d-%d",this,minport,maxport);
}

bool YRTPWrapper::startRTP(const char* raddr, unsigned int rport, int payload, int evpayload, const char* format)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::startRTP(\"%s\",%u,%d) [%p]",raddr,rport,payload,this);
    if (!m_rtp) {
	Debug(&splugin,DebugWarn,"YRTPWrapper [%p] attempted to start RTP before setup!",this);
	return false;
    }

    if (m_bufsize) {
	DDebug(&splugin,DebugAll,"YRTPWrapper [%p] attempted to restart RTP!",this);
	m_rtp->resync();
	return true;
    }

    if (!format)
	format = lookup(payload, dict_payloads);
    if (!format) {
	Debug(&splugin,DebugWarn,"YRTPWrapper [%p] can't find name for payload %d",this,payload);
	return false;
    }

    if (payload == -1)
	payload = lookup(format, dict_payloads, -1);
    if (payload == -1) {
	Debug(&splugin,DebugWarn,"YRTPWrapper [%p] can't find payload for format %s",this,format);
	return false;
    }

    if ((payload < 0) || (payload >= 127)) {
	Debug(&splugin,DebugWarn,"YRTPWrapper [%p] received invalid payload %d",this,payload);
	return false;
    }

    Debug(&splugin,DebugAll,"RTP format '%s' payload %d",format,payload);

    SocketAddr addr(AF_INET);
    if (!(addr.host(raddr) && addr.port(rport) && m_rtp->remoteAddr(addr))) {
	Debug(&splugin,DebugWarn,"RTP failed to set remote address %s:%d [%p]",raddr,rport,this);
	return false;
    }
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
    if (!(m_rtp->initGroup() && m_rtp->direction(m_dir)))
	return false;
    m_rtp->dataPayload(payload);
    m_rtp->eventPayload(evpayload);
    m_bufsize = s_cfg.getIntValue("rtp","buffer",240);
    return true;
}

bool YRTPWrapper::sendDTMF(char dtmf, int duration)
{
    return m_rtp && m_rtp->rtpSendKey(dtmf,duration);
}

void YRTPWrapper::gotDTMF(char tone)
{
    Debug(&splugin,DebugInfo,"YRTPWrapper::gotDTMF('%c') [%p]",tone,this);
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

void YRTPWrapper::guessLocal(const char* remoteip, String& localip)
{
    localip.clear();
    SocketAddr r(AF_INET);
    if (!r.host(remoteip)) {
	Debug(&splugin,DebugInfo,"Guess - Could not parse remote '%s'",remoteip);
	return;
    }
    SocketAddr l;
    if (!l.local(r)) {
	Debug(&splugin,DebugInfo,"Guess - Could not guess local for remote '%s'",remoteip);
	return;
    }
    localip = l.host();
    Debug(&splugin,DebugInfo,"Guessed local IP '%s' for remote '%s'",localip.c_str(),remoteip);
}

void YRTPWrapper::addDirection(RTPSession::Direction direction)
{
    m_dir = (RTPSession::Direction)(m_dir | direction);
    if (m_rtp && m_bufsize)
	m_rtp->direction(m_dir);
}

bool YRTPSession::rtpRecvData(bool marker, unsigned int timestamp, const void* data, int len)
{
    YRTPSource* source = m_wrap ? m_wrap->m_source : 0;
    if (!source)
	return false;
    DataBlock block;
    block.assign((void*)data, len, false);
    source->Forward(block);
    block.clear(false);
    return true;
}

bool YRTPSession::rtpRecvEvent(int event, char key, int duration,
	int volume, unsigned int timestamp)
{
    if (!(m_wrap && key))
	return false;
    m_wrap->gotDTMF(key);
    return true;
}

void YRTPSession::rtpNewPayload(int payload, unsigned int timestamp)
{
    if (payload == 13) {
	Debug(&splugin,DebugInfo,"Activating RTP silence payload %d in wrapper %p",payload,m_wrap);
	silencePayload(payload);
    }
}

void YRTPSession::rtpNewSSRC(u_int32_t newSsrc)
{
    if (m_resync && receiver()) {
	m_resync = false;
	Debug(&splugin,DebugInfo,"Changing SSRC from %08X to %08X in wrapper %p",
	    receiver()->ssrc(),newSsrc,m_wrap);
	receiver()->ssrc(newSsrc);
    }
}

YRTPSource::YRTPSource(YRTPWrapper* wrap)
    : m_wrap(wrap)
{
    Debug(&splugin,DebugAll,"YRTPSource::YRTPSource(%p) [%p]",wrap,this);
    m_format.clear();
    if (m_wrap) {
	m_wrap->ref();
	m_wrap->m_source = this;
    }
}

YRTPSource::~YRTPSource()
{
    Debug(&splugin,DebugAll,"YRTPSource::~YRTPSource() [%p] wrapper=%p",this,m_wrap);
    m_mutex.lock();
    if (m_wrap) {
	YRTPWrapper* tmp = m_wrap;
	m_wrap = 0;
	tmp->m_source = 0;
	tmp->deref();
	Thread::yield();
    }
    m_mutex.unlock();
}

YRTPConsumer::YRTPConsumer(YRTPWrapper *wrap)
    : m_wrap(wrap), m_timestamp(0)
{
    Debug(&splugin,DebugAll,"YRTPConsumer::YRTPConsumer(%p) [%p]",wrap,this);
    m_format.clear();
    if (m_wrap) {
	m_wrap->ref();
	m_wrap->m_consumer = this;
    }
}

YRTPConsumer::~YRTPConsumer()
{
    Debug(&splugin,DebugAll,"YRTPConsumer::~YRTPConsumer() [%p] wrapper=%p ts=%d",this,m_wrap,m_timestamp);
    if (m_wrap) {
	YRTPWrapper* tmp = m_wrap;
	m_wrap = 0;
	tmp->m_consumer = 0;
	tmp->deref();
    }
}

void YRTPConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    if (!(m_wrap && m_wrap->bufSize() && m_wrap->rtp()))
	return;
    XDebug(&splugin,DebugAll,"YRTPConsumer writing %d bytes, delta=%lu ts=%d [%p]",
	data.length(),timeDelta,m_timestamp,this);
    unsigned int buf = m_wrap->bufSize();
    const char* ptr = (const char*)data.data();
    unsigned int len = data.length();
    // make it safe to break a long octet buffer
    if (len == timeDelta)
	timeDelta = 0;
    while (len && m_wrap && m_wrap->rtp()) {
	unsigned int sz = len;
	if ((sz > buf) && !timeDelta) {
	    DDebug(&splugin,DebugAll,"Creating %u bytes fragment of %u bytes buffer",buf,len);
	    sz = buf;
	}
	m_wrap->rtp()->rtpSendData(false,m_timestamp,ptr,sz);
	// if timestamp increment is not provided we have to guess...
	m_timestamp += timeDelta ? timeDelta : sz;
	len -= sz;
	ptr += sz;
    }
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

    const char* media = msg.getValue("media","audio");
    String lip(msg.getValue("localip"));
    String rip(msg.getValue("remoteip"));
    String rport(msg.getValue("remoteport"));
    if (lip.null())
	YRTPWrapper::guessLocal(rip,lip);
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch) {
	if (!src.null())
	    Debug(&splugin,DebugWarn,"RTP source '%s' attach request with no call channel!",src.c_str());
	if (!cons.null())
	    Debug(&splugin,DebugWarn,"RTP consumer '%s' attach request with no call channel!",cons.c_str());
	return false;
    }

    YRTPWrapper *w = YRTPWrapper::find(ch);
    if (!w)
	w = YRTPWrapper::find(msg.getValue("rtpid"));
    if (!w) {
	w = new YRTPWrapper(lip,ch,media);
	w->setMaster(msg.getValue("id"));

	if (!src.null()) {
	    YRTPSource* s = new YRTPSource(w);
	    ch->setSource(s,media);
	    s->deref();
	}

	if (!cons.null()) {
	    YRTPConsumer* c = new YRTPConsumer(w);
	    ch->setConsumer(c,media);
	    c->deref();
	}
    }

    if (rip && rport) {
	String p(msg.getValue("payload"));
	if (p.null())
	    p = msg.getValue("format");
	w->startRTP(rip,rport.toInteger(),p.toInteger(dict_payloads,-1),msg.getIntValue("evpayload",101),msg.getValue("format"));
    }
    msg.setParam("localip",lip);
    msg.setParam("localport",String(w->port()));
    msg.setParam("rtpid",w->id());

    // Stop dispatching if we handled all requested
    return !more;
}

bool RtpHandler::received(Message &msg)
{
    Debug(&splugin,DebugAll,"RTP message received");
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

    const char* media = msg.getValue("media","audio");
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch) {
	if (d_recv)
	    Debug(&splugin,DebugWarn,"RTP recv request with no call channel!");
	if (d_send)
	    Debug(&splugin,DebugWarn,"RTP send request with no call channel!");
	return false;
    }

    String rip(msg.getValue("remoteip"));
    String rport(msg.getValue("remoteport"));

    YRTPWrapper *w = YRTPWrapper::find(ch);
    if (w)
	Debug(&splugin,DebugAll,"YRTPWrapper %p found by CallEndpoint",w);
    if (!w) {
	w = YRTPWrapper::find(msg.getValue("rtpid"));
	if (w)
	    Debug(&splugin,DebugAll,"YRTPWrapper %p found by ID",w);
    }
    if (!w) {
	String lip(msg.getValue("localip"));
	if (lip.null())
	    YRTPWrapper::guessLocal(rip,lip);
	if (lip.null()) {
	    Debug(&splugin,DebugWarn,"RTP request with no local address!");
	    return false;
	}
	msg.setParam("localip",lip);

	w = new YRTPWrapper(lip,ch,media,direction);
	w->setMaster(msg.getValue("id"));
    }
    else {
	w->ref();
	w->addDirection(direction);
    }

    if (d_recv && !ch->getSource()) {
	YRTPSource* s = new YRTPSource(w);
	ch->setSource(s,media);
	s->deref();
    }

    if (d_send && !ch->getConsumer()) {
	YRTPConsumer* c = new YRTPConsumer(w);
	ch->setConsumer(c,media);
	c->deref();
    }

    if (w->deref())
	return false;

    if (rip && rport) {
	String p(msg.getValue("payload"));
	if (p.null())
	    p = msg.getValue("format");
	w->startRTP(rip,rport.toInteger(),p.toInteger(dict_payloads,-1),msg.getIntValue("evpayload",101),msg.getValue("format"));
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
    if (wrap && wrap->rtp()) {
	Debug(&splugin,DebugInfo,"RTP DTMF '%s' targetid '%s'",text.c_str(),targetid.c_str());
	int duration = msg.getIntValue("duration");
	for (unsigned int i=0;i<text.length();i++)
	    wrap->sendDTMF(text.at(i),duration);
	return true;
    }
    return false;
}

YRTPPlugin::YRTPPlugin()
    : Module("yrtp","misc"), m_first(true)
{
    Output("Loaded module YRTP");
}

YRTPPlugin::~YRTPPlugin()
{
    Output("Unloading module YRTP");
    s_calls.clear();
}

void YRTPPlugin::statusParams(String& str)
{
    str.append("chans=",",") << s_calls.count();
}

void YRTPPlugin::initialize()
{
    Output("Initializing module YRTP");
    s_cfg = Engine::configFile("yrtpchan");
    s_cfg.load();
    setup();
    if (m_first) {
	m_first = false;
	Engine::install(new AttachHandler);
	Engine::install(new RtpHandler);
	Engine::install(new DTMFHandler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
