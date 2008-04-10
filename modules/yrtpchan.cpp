/**
 * yrtpchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * RTP channel - also acts as data helper for other protocols
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


#include <yatephone.h>
#include <yatertp.h>

#include <string.h>
#include <stdlib.h>

#define MIN_PORT 16384
#define MAX_PORT 32768
#define BUF_SIZE 240
#define BUF_PREF 160

using namespace TelEngine;
namespace { // anonymous

/* Payloads for the AV profile */
static TokenDict dict_payloads[] = {
    { "mulaw",         0 },
    { "alaw",          8 },
    { "gsm",           3 },
    { "lpc10",         7 },
    { "slin",         11 },
    { "g726",          2 },
    { "g722",          9 },
    { "g723",          4 },
    { "g728",         15 },
    { "g729",         18 },
    { "ilbc",         98 },
    { "ilbc20",       98 },
    { "ilbc30",       98 },
    { "amr",          96 },
    { "amr/16000",    99 },
    { "speex",       102 },
    { "speex/16000", 103 },
    { "speex/32000", 104 },
    { "h261",         31 },
    { "h263",         34 },
    { "mpv",          32 },
    { 0 ,              0 },
};

static TokenDict dict_yrtp_dir[] = {
    { "receive", RTPSession::RecvOnly },
    { "send", RTPSession::SendOnly },
    { "bidir", RTPSession::SendRecv },
    { 0 , 0 },
};

static TokenDict dict_tos[] = {
    { "lowdelay", Socket::LowDelay },
    { "throughput", Socket::MaxThroughput },
    { "reliability", Socket::MaxReliability },
    { "mincost", Socket::MinCost },
    { 0, 0 }
};

static int s_minport = MIN_PORT;
static int s_maxport = MAX_PORT;
static int s_bufsize = BUF_SIZE;
static String s_tos;
static String s_localip;
static bool s_autoaddr  = true;
static bool s_anyssrc   = false;
static bool s_needmedia = false;
static bool s_rtcp  = true;
static bool s_drill = false;

static Thread::Priority s_priority = Thread::Normal;
static int s_sleep   = 5;
static int s_timeout = 0;
static int s_minjitter = 0;
static int s_maxjitter = 0;

class YRTPSource;
class YRTPConsumer;
class YRTPSession;

class YRTPWrapper : public RefObject
{
    friend class YRTPSource;
    friend class YRTPConsumer;
    friend class YRTPSession;
public:
    YRTPWrapper(const char *localip, CallEndpoint* conn = 0, const char* media = "audio", RTPSession::Direction direction = RTPSession::SendRecv, bool rtcp = true);
    ~YRTPWrapper();
    virtual void* getObject(const String &name) const;
    void setupRTP(const char* localip, bool rtcp);
    bool startRTP(const char* raddr, unsigned int rport, const Message& msg);
    bool setRemote(const char* raddr, unsigned int rport, const Message& msg);
    bool sendDTMF(char dtmf, int duration = 0);
    void gotDTMF(char tone);
    void timeout(bool initial);
    inline YRTPSession* rtp() const
	{ return m_rtp; }
    inline RTPSession::Direction dir() const
	{ return m_dir; }
    inline CallEndpoint* conn() const
	{ return m_conn; }
    inline const String& id() const
	{ return m_id; }
    inline const String& callId() const
	{ return m_master; }
    inline const String& media() const
	{ return m_media; }
    inline unsigned int bufSize() const
	{ return m_bufsize; }
    inline unsigned int port() const
	{ return m_port; }
    inline void setMaster(const char* master)
	{ if (master) m_master = master; }
    inline bool isAudio() const
	{ return m_audio; }
    YRTPSource* getSource();
    YRTPConsumer* getConsumer();
    void addDirection(RTPSession::Direction direction);
    static YRTPWrapper* find(const CallEndpoint* conn, const String& media);
    static YRTPWrapper* find(const String& id);
    static void guessLocal(const char* remoteip, String& localip);
private:
    void setTimeout(const Message& msg, int timeOut);
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
    bool m_audio;
};

class YRTPSession : public RTPSession
{
    friend class RTPSession;
public:
    inline YRTPSession(YRTPWrapper* wrap)
	: m_wrap(wrap), m_resync(false), m_anyssrc(false)
	{ }
    virtual ~YRTPSession();
    virtual bool rtpRecvData(bool marker, unsigned int timestamp,
	const void* data, int len);
    virtual bool rtpRecvEvent(int event, char key, int duration,
	int volume, unsigned int timestamp);
    virtual void rtpNewPayload(int payload, unsigned int timestamp);
    virtual void rtpNewSSRC(u_int32_t newSsrc, bool marker);
    inline void resync()
	{ m_resync = true; }
    inline void anySSRC(bool acceptAny = true)
	{ m_anyssrc = acceptAny; }
protected:
    virtual void timeout(bool initial);
private:
    YRTPWrapper* m_wrap;
    bool m_resync;
    bool m_anyssrc;
};

class YRTPSource : public DataSource
{
    friend class YRTPWrapper;
public:
    YRTPSource(YRTPWrapper* wrap);
    ~YRTPSource();
    inline void busy(bool isBusy)
	{ m_busy = isBusy; }
private:
    YRTPWrapper* m_wrap;
    volatile bool m_busy;
};

class YRTPConsumer : public DataConsumer
{
    friend class YRTPWrapper;
public:
    YRTPConsumer(YRTPWrapper* wrap);
    ~YRTPConsumer();
    virtual void Consume(const DataBlock &data, unsigned long tStamp);
    inline void setSplitable()
	{ m_splitable = (m_format == "alaw") || (m_format == "mulaw"); }
private:
    YRTPWrapper* m_wrap;
    bool m_splitable;
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
    virtual void statusDetail(String& str);
private:
    bool m_first;
};

static YRTPPlugin splugin;
static ObjList s_calls;
static Mutex s_mutex;
static Mutex s_srcMutex;


YRTPWrapper::YRTPWrapper(const char* localip, CallEndpoint* conn, const char* media, RTPSession::Direction direction, bool rtcp)
    : m_rtp(0), m_dir(direction), m_conn(conn),
      m_source(0), m_consumer(0), m_media(media),
      m_bufsize(0), m_port(0)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::YRTPWrapper('%s',%p,'%s',%s,%s) [%p]",
	localip,conn,media,lookup(direction,dict_yrtp_dir),String::boolText(rtcp),this);
    m_id = "yrtp/";
    m_id << (unsigned int)::random();
    if (conn)
	m_master = conn->id();
    m_audio = (m_media == "audio");
    s_mutex.lock();
    s_calls.append(this);
    setupRTP(localip,rtcp);
    s_mutex.unlock();
}

YRTPWrapper::~YRTPWrapper()
{
    Debug(&splugin,DebugAll,"YRTPWrapper::~YRTPWrapper() %s '%s' [%p]",
	lookup(m_dir,dict_yrtp_dir),m_media.c_str(),this);
    s_mutex.lock();
    s_calls.remove(this,false);
    if (m_rtp) {
	Debug(DebugAll,"Cleaning up RTP %p",m_rtp);
	YRTPSession* tmp = m_rtp;
	m_rtp = 0;
	tmp->destruct();
    }
    if (m_source) {
	Debug(&splugin,DebugGoOn,"There is still a RTP source %p [%p]",m_source,this);
	TelEngine::destruct(m_source);
    }
    if (m_consumer) {
	Debug(&splugin,DebugGoOn,"There is still a RTP consumer %p [%p]",m_consumer,this);
	TelEngine::destruct(m_consumer);
    }
    s_mutex.unlock();
}

void* YRTPWrapper::getObject(const String &name) const
{
    if (name == "Socket")
	return m_rtp ? m_rtp->rtpSock() : 0;
    return RefObject::getObject(name);
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

void YRTPWrapper::setupRTP(const char* localip, bool rtcp)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::setupRTP(\"%s\",%s) [%p]",
	localip,String::boolText(rtcp),this);
    m_rtp = new YRTPSession(this);
    m_rtp->initTransport();
    int minport = s_minport;
    int maxport = s_maxport;
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
	Debug(&splugin,DebugWarn,"Wrapper could not parse address '%s' [%p]",localip,this);
	return;
    }
    for (; attempt; attempt--) {
	int lport = (minport + (::random() % (maxport - minport))) & 0xfffe;
	addr.port(lport);
	if (m_rtp->localAddr(addr,rtcp)) {
	    m_port = lport;
	    Debug(&splugin,DebugInfo,"Session %p bound to %s:%u%s [%p]",
		m_rtp,localip,m_port,(rtcp ? " +RTCP" : ""),this);
	    return;
	}
    }
    Debug(&splugin,DebugWarn,"YRTPWrapper [%p] RTP bind failed in range %d-%d",this,minport,maxport);
}

bool YRTPWrapper::setRemote(const char* raddr, unsigned int rport, const Message& msg)
{
    SocketAddr addr(AF_INET);
    if (!(addr.host(raddr) && addr.port(rport) && m_rtp->remoteAddr(addr,msg.getBoolValue("autoaddr",s_autoaddr)))) {
	Debug(&splugin,DebugWarn,"RTP failed to set remote address %s:%d [%p]",raddr,rport,this);
	return false;
    }
    return true;
}

void YRTPWrapper::setTimeout(const Message& msg, int timeOut)
{
    const String* param = msg.getParam("timeout");
    if (param) {
	// accept true/false to apply default or disable
	if (param->isBoolean())
	    timeOut = param->toBoolean() ? s_timeout : 0;
	else
	    timeOut = param->toInteger(timeOut);
    }
    if (timeOut >= 0)
	m_rtp->setTimeout(timeOut);
}

bool YRTPWrapper::startRTP(const char* raddr, unsigned int rport, const Message& msg)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::startRTP(\"%s\",%u) [%p]",raddr,rport,this);
    if (!m_rtp) {
	Debug(&splugin,DebugWarn,"Wrapper attempted to start RTP before setup! [%p]",this);
	return false;
    }

    if (m_bufsize) {
	DDebug(&splugin,DebugAll,"Wrapper attempted to restart RTP! [%p]",this);
	setRemote(raddr,rport,msg);
	m_rtp->resync();
	setTimeout(msg,-1);
	return true;
    }

    String p(msg.getValue("payload"));
    if (p.null())
	p = msg.getValue("format");
    int payload = p.toInteger(dict_payloads,-1);
    int evpayload = msg.getIntValue("evpayload",101);
    const char* format = msg.getValue("format");
    p = msg.getValue("tos",s_tos);
    int tos = p.toInteger(dict_tos,0);
    int msec = msg.getIntValue("msleep",s_sleep);

    if (!format)
	format = lookup(payload, dict_payloads);
    if (!format) {
	if (payload < 0)
	    Debug(&splugin,DebugWarn,"Wrapper neither format nor payload specified [%p]",this);
	else
	    Debug(&splugin,DebugWarn,"Wrapper can't find name for payload %d [%p]",payload,this);
	return false;
    }

    if (payload == -1)
	payload = lookup(format, dict_payloads, -1);
    if (payload == -1) {
	Debug(&splugin,DebugWarn,"Wrapper can't find payload for format %s [%p]",format,this);
	return false;
    }

    if ((payload < 0) || (payload >= 127)) {
	Debug(&splugin,DebugWarn,"Wrapper received invalid payload %d [%p]",payload,this);
	return false;
    }

    Debug(&splugin,DebugInfo,"RTP starting format '%s' payload %d [%p]",format,payload,this);
    int minJitter = msg.getIntValue("minjitter",s_minjitter);
    int maxJitter = msg.getIntValue("maxjitter",s_maxjitter);

    if (!setRemote(raddr,rport,msg))
	return false;
    m_rtp->anySSRC(msg.getBoolValue("anyssrc",s_anyssrc));
    // Change format of source and/or consumer,
    //  reinstall them to rebuild codec chains
    if (m_source) {
	if (m_conn) {
	    m_source->ref();
	    m_conn->setSource(0,m_media);
	}
	m_source->m_format = format;
	if (m_conn) {
	    m_conn->setSource(m_source,m_media);
	    m_source->deref();
	}
    }
    if (m_consumer) {
	if (m_conn) {
	    m_consumer->ref();
	    m_conn->setConsumer(0,m_media);
	}
	m_consumer->m_format = format;
	m_consumer->setSplitable();
	if (m_conn) {
	    m_conn->setConsumer(m_consumer,m_media);
	    m_consumer->deref();
	}
    }
    if (!(m_rtp->initGroup(msec,Thread::priority(msg.getValue("thread"),s_priority)) &&
	 m_rtp->direction(m_dir)))
	return false;
    m_rtp->dataPayload(payload);
    m_rtp->eventPayload(evpayload);
    m_rtp->setTOS(tos);
    if (msg.getBoolValue("drillhole",s_drill))
	m_rtp->drillHole();
    setTimeout(msg,s_timeout);
//    if (maxJitter > 0)
//	m_rtp->setDejitter(minJitter*1000,maxJitter*1000);
    m_bufsize = s_bufsize;
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

void YRTPWrapper::timeout(bool initial)
{
    Debug(&splugin,DebugWarn,"%s timeout in%s%s wrapper [%p]",
	(initial ? "Initial" : "Later"),
	(m_master ? " channel " : ""),
	m_master.safe(),this);
    if (s_needmedia && m_master) {
	Message* m = new Message("call.drop");
	m->addParam("id",m_master);
	m->addParam("reason","nomedia");
	Engine::enqueue(m);
    }
}

void YRTPWrapper::guessLocal(const char* remoteip, String& localip)
{
    if (s_localip) {
	localip = s_localip;
	return;
    }
    localip.clear();
    SocketAddr r(AF_INET);
    if (!r.host(remoteip)) {
	Debug(&splugin,DebugNote,"Guess - Could not parse remote '%s'",remoteip);
	return;
    }
    SocketAddr l;
    if (!l.local(r)) {
	Debug(&splugin,DebugNote,"Guess - Could not guess local for remote '%s'",remoteip);
	return;
    }
    localip = l.host();
    Debug(&splugin,DebugInfo,"Guessed local IP '%s' for remote '%s'",localip.c_str(),remoteip);
}

YRTPSource* YRTPWrapper::getSource()
{
    if (m_source && m_source->ref())
	return m_source;
    return new YRTPSource(this);
}

YRTPConsumer* YRTPWrapper::getConsumer()
{
    if (m_consumer && m_consumer->ref())
	return m_consumer;
    return new YRTPConsumer(this);
}

void YRTPWrapper::addDirection(RTPSession::Direction direction)
{
    m_dir = (RTPSession::Direction)(m_dir | direction);
    if (m_rtp && m_bufsize)
	m_rtp->direction(m_dir);
}


YRTPSession::~YRTPSession()
{
    // disconnect thread and transport before our virtual methods become invalid
    // this will also lock the group preventing rtpRecvData from being called
    group(0);
    transport(0);
}

bool YRTPSession::rtpRecvData(bool marker, unsigned int timestamp, const void* data, int len)
{
    s_srcMutex.lock();
    YRTPSource* source = m_wrap ? m_wrap->m_source : 0;
    // we MUST NOT reference count here as RTPGroup will crash if we remove
    // any RTPProcessor from its own thread
    if (source) {
	if (source->alive())
	    source->busy(true);
	else
	    source = 0;
    }
    s_srcMutex.unlock();
    if (!source)
	return false;
    // the source will not be destroyed until we reset the busy flag
    DataBlock block;
    block.assign((void*)data, len, false);
    source->Forward(block,timestamp);
    block.clear(false);
    source->busy(false);
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

void YRTPSession::rtpNewSSRC(u_int32_t newSsrc, bool marker)
{
    if ((m_anyssrc || m_resync) && receiver()) {
	m_resync = false;
	Debug(&splugin,DebugInfo,"Changing SSRC from %08X to %08X in wrapper %p",
	    receiver()->ssrc(),newSsrc,m_wrap);
	receiver()->ssrc(newSsrc);
    }
}

void YRTPSession::timeout(bool initial)
{
    if (m_wrap)
	m_wrap->timeout(initial);
}


YRTPSource::YRTPSource(YRTPWrapper* wrap)
    : m_wrap(wrap), m_busy(false)
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
    Debug(&splugin,DebugAll,"YRTPSource::~YRTPSource() [%p] wrapper=%p ts=%lu",
	this,m_wrap,m_timestamp);
    if (m_wrap) {
	s_srcMutex.lock();
	YRTPWrapper* tmp = m_wrap;
	const YRTPSource* s = tmp->m_source;
	m_wrap = 0;
	tmp->m_source = 0;
	s_srcMutex.unlock();
	if (s != this)
	    Debug(&splugin,DebugGoOn,"Wrapper %p held source %p not [%p]",tmp,s,this);
	// we have just to wait for any YRTPSession::rtpRecvData() to finish
	while (m_busy)
	    Thread::yield();
	tmp->deref();
    }
}


YRTPConsumer::YRTPConsumer(YRTPWrapper *wrap)
    : m_wrap(wrap), m_splitable(false)
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
    Debug(&splugin,DebugAll,"YRTPConsumer::~YRTPConsumer() [%p] wrapper=%p ts=%lu",
	this,m_wrap,m_timestamp);
    if (m_wrap) {
	YRTPWrapper* tmp = m_wrap;
	const YRTPConsumer* c = tmp->m_consumer;
	m_wrap = 0;
	tmp->m_consumer = 0;
	tmp->deref();
	if (c != this)
	    Debug(&splugin,DebugGoOn,"Wrapper %p held consumer %p not [%p]",tmp,c,this);
    }
}

void YRTPConsumer::Consume(const DataBlock &data, unsigned long tStamp)
{
    if (!(m_wrap && m_wrap->bufSize() && m_wrap->rtp()))
	return;
    XDebug(&splugin,DebugAll,"YRTPConsumer writing %d bytes, ts=%lu [%p]",
	data.length(),tStamp,this);
    unsigned int buf = m_wrap->bufSize();
    const char* ptr = (const char*)data.data();
    unsigned int len = data.length();
    while (len && m_wrap && m_wrap->rtp()) {
	unsigned int sz = len;
	if (m_splitable && m_wrap->isAudio() && (sz > buf)) {
	    // divide evenly a buffer that is multiple of preferred size
	    if ((buf > BUF_PREF) && ((len % BUF_PREF) == 0))
		sz = BUF_PREF;
	    else
		sz = buf;
	    DDebug(&splugin,DebugAll,"Creating %u bytes fragment of %u bytes buffer",sz,len);
	}
	m_wrap->rtp()->rtpSendData(false,tStamp,ptr,sz);
	// if timestamp increment is not provided we have to guess...
	tStamp += sz;
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
	if (src.startSkip("rtp/",false))
	    more--;
	else
	    src = "";
    }

    String cons(msg.getValue("consumer"));
    if (cons.null())
	more--;
    else {
	if (cons.startSkip("rtp/",false))
	    more--;
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

    YRTPWrapper *w = YRTPWrapper::find(ch,media);
    if (!w)
	w = YRTPWrapper::find(msg.getValue("rtpid"));
    if (!w) {
	w = new YRTPWrapper(lip,ch,media,RTPSession::SendRecv,msg.getBoolValue("rtcp",s_rtcp));
	w->setMaster(msg.getValue("id"));

	if (!src.null()) {
	    YRTPSource* s = w->getSource();
	    ch->setSource(s,media);
	    s->deref();
	}

	if (!cons.null()) {
	    YRTPConsumer* c = w->getConsumer();
	    ch->setConsumer(c,media);
	    c->deref();
	}

	if (w->deref())
	    return false;
    }

    if (rip && rport)
	w->startRTP(rip,rport.toInteger(),msg);
    msg.setParam("localip",lip);
    msg.setParam("localport",String(w->port()));
    msg.setParam("rtpid",w->id());

    // Stop dispatching if we handled all requested
    return !more;
}


bool RtpHandler::received(Message &msg)
{
    String trans = msg.getValue("transport");
    if (trans && !trans.startsWith("RTP/"))
	return false;
    Debug(&splugin,DebugAll,"%s message received",(trans ? trans.c_str() : "No-transport"));
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

    YRTPWrapper* w = 0;
    const char* media = msg.getValue("media","audio");
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	w = YRTPWrapper::find(ch,media);
	if (w)
	    Debug(&splugin,DebugAll,"Wrapper %p found by CallEndpoint %p",w,ch);
    }
    if (!w) {
	const char* rid = msg.getValue("rtpid");
	w = YRTPWrapper::find(rid);
	if (w)
	    Debug(&splugin,DebugAll,"Wrapper %p found by ID '%s'",w,rid);
    }
    if (!(ch || w)) {
	Debug(&splugin,DebugWarn,"Neither call channel nor RTP wrapper found!");
	return false;
    }

    String rip(msg.getValue("remoteip"));

    if (!w) {
	// it would be pointless to create an unreferenced wrapper
	if (!(d_recv || d_send))
	    return false;
	String lip(msg.getValue("localip"));
	if (lip.null())
	    YRTPWrapper::guessLocal(rip,lip);
	if (lip.null()) {
	    Debug(&splugin,DebugWarn,"RTP request with no local address!");
	    return false;
	}
	msg.setParam("localip",lip);

	w = new YRTPWrapper(lip,ch,media,direction,msg.getBoolValue("rtcp",s_rtcp));
	w->setMaster(msg.getValue("id"));
    }
    else {
	w->ref();
	w->addDirection(direction);
    }

    if (d_recv && ch && !ch->getSource(media)) {
	YRTPSource* s = w->getSource();
	ch->setSource(s,media);
	s->deref();
    }

    if (d_send && ch && !ch->getConsumer(media)) {
	YRTPConsumer* c = w->getConsumer();
	ch->setConsumer(c,media);
	c->deref();
    }

    if (w->deref())
	return false;

    String rport(msg.getValue("remoteport"));
    if (rip && rport)
	w->startRTP(rip,rport.toInteger(),msg);
    msg.setParam("localport",String(w->port()));
    msg.setParam("rtpid",w->id());

    if (msg.getBoolValue("getsession",!msg.userData()))
	msg.userData(w);
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
    s_mutex.lock();
    str.append("chans=",",") << s_calls.count();
    s_mutex.unlock();
}

void YRTPPlugin::statusDetail(String& str)
{
    s_mutex.lock();
    ObjList* l = s_calls.skipNull();
    for (; l; l=l->skipNext()) {
	YRTPWrapper* w = static_cast<YRTPWrapper*>(l->get());
        str.append(w->id(),",") << "=" << w->callId();
    }
    s_mutex.unlock();
}

void YRTPPlugin::initialize()
{
    Output("Initializing module YRTP");
    Configuration cfg(Engine::configFile("yrtpchan"));
    s_minport = cfg.getIntValue("general","minport",MIN_PORT);
    s_maxport = cfg.getIntValue("general","maxport",MAX_PORT);
    s_bufsize = cfg.getIntValue("general","buffer",BUF_SIZE);
    s_minjitter = cfg.getIntValue("general","minjitter");
    s_maxjitter = cfg.getIntValue("general","maxjitter");
    s_tos = cfg.getValue("general","tos");
    s_localip = cfg.getValue("general","localip");
    s_autoaddr = cfg.getBoolValue("general","autoaddr",true);
    s_anyssrc = cfg.getBoolValue("general","anyssrc",false);
    s_rtcp = cfg.getBoolValue("general","rtcp",true);
    s_drill = cfg.getBoolValue("general","drillhole",Engine::clientMode());
    s_timeout = cfg.getIntValue("general","timeout",3000);
    s_needmedia = cfg.getBoolValue("general","needmedia",false);
    s_sleep = cfg.getIntValue("general","defsleep",5);
    RTPGroup::setMinSleep(cfg.getIntValue("general","minsleep"));
    s_priority = Thread::priority(cfg.getValue("general","thread"));
    setup();
    if (m_first) {
	m_first = false;
	Engine::install(new AttachHandler);
	Engine::install(new RtpHandler);
	Engine::install(new DTMFHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
