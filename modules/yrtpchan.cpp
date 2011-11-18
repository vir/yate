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
    { "mpa",          14 },
    { "ilbc",         98 },
    { "ilbc20",       98 },
    { "ilbc30",       98 },
    { "amr",          96 },
    { "amr-o",        96 },
    { "amr/16000",    99 },
    { "amr-o/16000",  99 },
    { "speex",       102 },
    { "speex/16000", 103 },
    { "speex/32000", 104 },
    { "mjpeg",        26 },
    { "h261",         31 },
    { "h263",         34 },
    { "mpv",          32 },
    { "mp2t",         33 },
    { "mp4v",         98 },
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
static int s_padding = 0;
static String s_localip;
static String s_notifyMsg;
static bool s_autoaddr  = true;
static bool s_anyssrc   = false;
static bool s_warnFirst = false;
static bool s_warnLater = false;
static bool s_rtcp  = true;
static bool s_drill = false;

static Thread::Priority s_priority = Thread::Normal;
static int s_tos     = 0;
static int s_sleep   = 5;
static int s_interval= 0;
static int s_timeout = 0;
static int s_udptlTimeout = 0;

static int s_minjitter = 0;
static int s_maxjitter = 0;

class YRTPSource;
class YRTPConsumer;
class YRTPSession;
class YUDPTLSession;
class YRTPReflector;

class YRTPWrapper : public RefObject
{
    friend class YRTPSource;
    friend class YRTPConsumer;
    friend class YRTPSession;
    friend class YUDPTLSession;
public:
    YRTPWrapper(const char *localip, CallEndpoint* conn, const char* media, RTPSession::Direction direction, Message& msg, bool udptl = false);
    ~YRTPWrapper();
    virtual void* getObject(const String& name) const;
    bool setParams(const char* raddr, Message& msg);
    bool setRemote(const char* raddr, unsigned int rport, const Message& msg);
    void setFaxDivert(const Message& msg);
    bool sendDTMF(char dtmf, int duration = 0);
    void gotDTMF(char tone);
    void gotFax();
    void timeout(bool initial);
    inline YRTPSession* rtp() const
	{ return m_rtp; }
    inline YUDPTLSession* udptl() const
	{ return m_udptl; }
    inline UDPSession* session() const
	{ return m_rtp ? (UDPSession*)m_rtp : (UDPSession*)m_udptl; }
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
    inline const String& host() const
	{ return m_host; }
    inline unsigned int bufSize() const
	{ return m_bufsize; }
    inline unsigned int port() const
	{ return m_port; }
    inline void setMaster(const char* master)
	{ if (master) m_master = master; }
    inline bool isAudio() const
	{ return m_audio; }
    inline bool valid() const
	{ return m_valid; }
    DataSource* getSource();
    DataConsumer* getConsumer();
    void addDirection(RTPSession::Direction direction);
    void terminate(Message& msg);
    static YRTPWrapper* find(const CallEndpoint* conn, const String& media);
    static YRTPWrapper* find(const String& id);
    static void guessLocal(const char* remoteip, String& localip);
private:
    void setupRTP(const char* localip, bool rtcp);
    void setupUDPTL(const char* localip, u_int16_t maxLen = 250, u_int8_t maxSec = 2);
    bool bindLocal(const char* localip, bool rtcp);
    bool startRTP(const char* raddr, unsigned int rport, Message& msg);
    bool startUDPTL(const char* raddr, unsigned int rport, Message& msg);
    bool setupSRTP(Message& msg, bool buildMaster);
    bool startSRTP(const String& suite, const String& keyParams, const ObjList* paramList);
    bool setupUDPTL(Message& msg);
    void setTimeout(const Message& msg, int timeOut);
    YRTPSession* m_rtp;
    YUDPTLSession* m_udptl;
    RTPSession::Direction m_dir;
    CallEndpoint* m_conn;
    YRTPSource* m_source;
    YRTPConsumer* m_consumer;
    String m_id;
    String m_media;
    String m_format;
    String m_master;
    String m_faxDivert;
    String m_faxCaller;
    String m_faxCalled;
    String m_host;
    unsigned int m_bufsize;
    unsigned int m_port;
    bool m_audio;
    bool m_valid;

    unsigned int m_noAudio;
    unsigned int m_lostAudio;
};

class YRTPSession : public RTPSession
{
public:
    inline YRTPSession(YRTPWrapper* wrap)
	: m_wrap(wrap), m_resync(false), m_anyssrc(false), m_getFax(true)
	{ }
    virtual ~YRTPSession();
    virtual bool rtpRecvData(bool marker, unsigned int timestamp,
	const void* data, int len);
    virtual bool rtpRecvEvent(int event, char key, int duration,
	int volume, unsigned int timestamp);
    virtual void rtpNewPayload(int payload, unsigned int timestamp);
    virtual void rtpNewSSRC(u_int32_t newSsrc, bool marker);
    virtual Cipher* createCipher(const String& name, Cipher::Direction dir);
    virtual bool checkCipher(const String& name);
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
    bool m_getFax;
};

class YUDPTLSession : public UDPTLSession
{
public:
    inline YUDPTLSession(YRTPWrapper* wrap, u_int16_t maxLen, u_int8_t maxSec)
	: UDPTLSession(maxLen,maxSec),
	  m_wrap(wrap)
	{ }
    virtual ~YUDPTLSession();
protected:
    virtual void udptlRecv(const void* data, int len, u_int16_t seq, bool recovered);
    virtual void timeout(bool initial);
private:
    YRTPWrapper* m_wrap;
};

class YRTPSource : public DataSource
{
    friend class YRTPWrapper;
public:
    YRTPSource(YRTPWrapper* wrap);
    ~YRTPSource();
    virtual bool valid() const
	{ return m_wrap && m_wrap->valid(); }
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
    virtual bool valid() const
	{ return m_wrap && m_wrap->valid(); }
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
    inline void setSplitable()
	{ m_splitable = (m_format == YSTRING("alaw")) || (m_format == YSTRING("mulaw")); }
private:
    YRTPWrapper* m_wrap;
    bool m_splitable;
};

class YRTPMonitor : public RTPProcessor
{
public:
    inline YRTPMonitor(const String* id = 0)
	: m_id(id),
	  m_rtpPackets(0), m_rtcpPackets(0), m_rtpBytes(0),
	  m_payload(-1), m_start(0), m_last(0)
	{ }
    virtual void rtpData(const void* data, int len);
    virtual void rtcpData(const void* data, int len);
    void startup();
    void saveStats(Message& msg) const;
protected:
    void updateTimes(u_int64_t when);
    void timerTick(const Time& when);
    void timeout(bool initial);
    const String* m_id;
    unsigned int m_rtpPackets;
    unsigned int m_rtcpPackets;
    unsigned int m_rtpBytes;
    int m_payload;
    u_int64_t m_start;
    u_int64_t m_last;
};

class YRTPReflector : public GenObject
{
public:
    YRTPReflector(const String& id, bool passiveA, bool passiveB);
    virtual ~YRTPReflector();
    inline const String& idA() const
	{ return m_idA; }
    inline const String& idB() const
	{ return m_idB; }
    inline RTPTransport& rtpA() const
	{ return *m_rtpA; }
    inline RTPTransport& rtpB() const
	{ return *m_rtpB; }
    inline YRTPMonitor& monA() const
	{ return *m_monA; }
    inline YRTPMonitor& monB() const
	{ return *m_monB; }
    inline void setA(const String& id)
	{ m_idA = id; }
    inline void setB(const String& id)
	{ m_idB = id; }
private:
    RTPGroup* m_group;
    RTPTransport* m_rtpA;
    RTPTransport* m_rtpB;
    YRTPMonitor* m_monA;
    YRTPMonitor* m_monB;
    String m_idA;
    String m_idB;
};

class CipherHolder : public RefObject
{
public:
    inline CipherHolder()
	: m_cipher(0)
	{ }
    virtual ~CipherHolder()
	{ TelEngine::destruct(m_cipher); }
    virtual void* getObject(const String& name) const
	{ return (name == YSTRING("Cipher*")) ? (void*)&m_cipher : RefObject::getObject(name); }
    inline Cipher* cipher()
	{ Cipher* tmp = m_cipher; m_cipher = 0; return tmp; }
private:
    Cipher* m_cipher;
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
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);

    void addStats(const NamedList& stats);
    void genUpdate(Message& msg);

private:
    bool reflectSetup(Message& msg, const char* id, RTPTransport& rtp, const char* rHost, const char* leg);
    bool reflectStart(Message& msg, const char* id, RTPTransport& rtp, SocketAddr& rAddr);
    void reflectDrop(YRTPReflector*& refl, Lock& mylock);
    void reflectExecute(Message& msg);
    void reflectAnswer(Message& msg, bool ignore);
    void reflectHangup(Message& msg);
    bool m_first;
    ObjList m_updates;
};

static YRTPPlugin splugin;
static ObjList s_calls;
static ObjList s_mirrors;
static Mutex s_mutex(false,"YRTPChan");
static Mutex s_refMutex(false,"YRTPChan::reflect");
static Mutex s_srcMutex(false,"YRTPChan::source");


YRTPWrapper::YRTPWrapper(const char* localip, CallEndpoint* conn, const char* media, RTPSession::Direction direction, Message& msg, bool udptl)
    : m_rtp(0), m_udptl(0), m_dir(direction), m_conn(conn),
      m_source(0), m_consumer(0), m_media(media),
      m_bufsize(0), m_port(0), m_valid(true), m_noAudio(0), m_lostAudio(0)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::YRTPWrapper('%s',%p,'%s',%s,%p,%s) [%p]",
	localip,conn,media,lookup(direction,dict_yrtp_dir),
	&msg,String::boolText(udptl),this);
    m_id = udptl ? "udptl/" : "yrtp/";
    m_id << (unsigned int)Random::random();
    if (conn)
	m_master = conn->id();
    m_audio = !udptl && (m_media == YSTRING("audio"));
    s_mutex.lock();
    s_calls.append(this);
    if (udptl) {
	int md = 0xffff & msg.getIntValue(YSTRING("sdp_T38FaxMaxDatagram"));
	md = msg.getIntValue(YSTRING("t38maxdatagram"),md);
	if (md < 96)
	    md = 250;
	const String* ec = msg.getParam(YSTRING("sdp_T38FaxUdpEC"));
	int ms = (ec && (*ec == YSTRING("t38UDPRedundancy"))) ? 2 : 0;
	ms = msg.getIntValue(YSTRING("t38redundancy"),ms);
	if (ms < 0)
	    ms = 0;
	else if (ms > 16)
	    ms = 16;
	m_format = "t38";
	setupUDPTL(localip,md,ms);
    }
    else
	setupRTP(localip,msg.getBoolValue(YSTRING("rtcp"),s_rtcp));
    s_mutex.unlock();
}

YRTPWrapper::~YRTPWrapper()
{
    Debug(&splugin,DebugAll,"YRTPWrapper::~YRTPWrapper() %s '%s' [%p]",
	lookup(m_dir,dict_yrtp_dir),m_media.c_str(),this);
    s_mutex.lock();
    s_calls.remove(this,false);
    if (m_rtp) {
	Debug(DebugAll,"Cleaning up RTP %p [%p]",m_rtp,this);
	TelEngine::destruct(m_rtp);
    }
    if (m_udptl) {
	Debug(DebugAll,"Cleaning up UDPTL %p [%p]",m_udptl,this);
	TelEngine::destruct(m_udptl);
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

void* YRTPWrapper::getObject(const String& name) const
{
    if (name == YSTRING("Socket"))
	return m_rtp ? m_rtp->rtpSock() : 0;
    if (name == YSTRING("DataSource"))
	return m_source;
    if (name == YSTRING("DataConsumer"))
	return m_consumer;
    if (name == YSTRING("RTPSession"))
	return m_rtp;
    return RefObject::getObject(name);
}

YRTPWrapper* YRTPWrapper::find(const CallEndpoint* conn, const String& media)
{
    if (!conn)
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_calls;
    for (; l; l=l->next()) {
	YRTPWrapper *p = static_cast<YRTPWrapper *>(l->get());
	if (p && (p->conn() == conn) && (p->media() == media))
	    return p->ref() ? p : 0;
    }
    return 0;
}

YRTPWrapper* YRTPWrapper::find(const String& id)
{
    if (id.null())
	return 0;
    Lock lock(s_mutex);
    ObjList* l = &s_calls;
    for (; l; l=l->next()) {
	YRTPWrapper *p = static_cast<YRTPWrapper *>(l->get());
	if (p && (p->id() == id))
	    return p->ref() ? p : 0;
    }
    return 0;
}

void YRTPWrapper::setupRTP(const char* localip, bool rtcp)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::setupRTP(\"%s\",%s) [%p]",
	localip,String::boolText(rtcp),this);
    m_rtp = new YRTPSession(this);
    m_rtp->initTransport();
    bindLocal(localip,rtcp);
}

void YRTPWrapper::setupUDPTL(const char* localip, u_int16_t maxLen, u_int8_t maxSec)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::setupUDPTL(\"%s\",%u,%u) [%p]",
	localip,maxLen,maxSec,this);
    m_udptl = new YUDPTLSession(this,maxLen,maxSec);
    m_udptl->initTransport();
    bindLocal(localip,false);
}

bool YRTPWrapper::setupUDPTL(Message& msg)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::setupUDPTL(%p '%s') [%p]",
	&msg,msg.c_str(),this);
    if (!m_udptl)
	return false;
    String tmp(m_udptl->maxLen());
    msg.setParam("t38maxdatagram",tmp);
    msg.setParam("osdp_T38FaxMaxDatagram",tmp);
    msg.setParam("t38redundancy",String(m_udptl->maxSec()));
    if (m_udptl->maxSec())
	msg.setParam("osdp_T38FaxUdpEC","t38UDPRedundancy");
    return true;
}

bool YRTPWrapper::bindLocal(const char* localip, bool rtcp)
{
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
	Debug(&splugin,DebugWarn,"Wrapper '%s' could not parse address '%s' [%p]",
	    m_id.c_str(),localip,this);
	return false;
    }
    for (; attempt; attempt--) {
	int lport = (minport + (Random::random() % (maxport - minport))) & 0xfffe;
	addr.port(lport);
	if (m_rtp ? m_rtp->localAddr(addr,rtcp) : m_udptl->localAddr(addr)) {
	    m_host = addr.host();
	    m_port = lport;
	    Debug(&splugin,DebugInfo,"Session '%s' %p bound to %s:%u%s [%p]",
		m_id.c_str(),session(),localip,m_port,(rtcp ? " +RTCP" : ""),this);
	    return true;
	}
    }
    Debug(&splugin,DebugWarn,"YRTPWrapper '%s' bind failed in range %d-%d [%p]",
	m_id.c_str(),minport,maxport,this);
    return false;
}

bool YRTPWrapper::setParams(const char* rip, Message& msg)
{
    // start or just setup either RTP or UDPTL
    int rport = msg.getIntValue(YSTRING("remoteport"));
    if (rip && (rport > 0))
	return m_udptl ? startUDPTL(rip,rport,msg) : startRTP(rip,rport,msg);
    else
	return m_udptl ? setupUDPTL(msg) : setupSRTP(msg,msg.getBoolValue(YSTRING("secure")));
}

bool YRTPWrapper::setRemote(const char* raddr, unsigned int rport, const Message& msg)
{
    if (!session())
	return false;
    SocketAddr addr(AF_INET);
    if (!(addr.host(raddr) && addr.port(rport) && session()->remoteAddr(addr,msg.getBoolValue(YSTRING("autoaddr"),s_autoaddr)))) {
	Debug(&splugin,DebugWarn,"RTP failed to set remote address %s:%d [%p]",raddr,rport,this);
	return false;
    }
    return true;
}

void YRTPWrapper::setTimeout(const Message& msg, int timeOut)
{
    const String* param = msg.getParam(YSTRING("timeout"));
    if (param) {
	// accept true/false to apply default or disable
	if (param->isBoolean()) {
	    if (param->toBoolean())
		timeOut = rtp() ? s_timeout : s_udptlTimeout;
	    else
		timeOut = 0;
	}
	else
	    timeOut = param->toInteger(timeOut);
    }
    if ((timeOut >= 0) && session())
	session()->setTimeout(timeOut);
}

bool YRTPWrapper::startRTP(const char* raddr, unsigned int rport, Message& msg)
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

    String p(msg.getValue(YSTRING("payload")));
    if (p.null())
	p = msg.getValue(YSTRING("format"));
    int payload = p.toInteger(dict_payloads,-1);
    int evpayload = msg.getIntValue(YSTRING("evpayload"),101);
    const char* format = msg.getValue(YSTRING("format"));
    int tos = msg.getIntValue(YSTRING("tos"),dict_tos,s_tos);
    int msec = msg.getIntValue(YSTRING("msleep"),s_sleep);

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
    int minJitter = msg.getIntValue(YSTRING("minjitter"),s_minjitter);
    int maxJitter = msg.getIntValue(YSTRING("maxjitter"),s_maxjitter);

    if (!setRemote(raddr,rport,msg))
	return false;
    m_rtp->anySSRC(msg.getBoolValue(YSTRING("anyssrc"),s_anyssrc));
    m_format = format;
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
    if (!(m_rtp->initGroup(msec,Thread::priority(msg.getValue(YSTRING("thread")),s_priority)) &&
	 m_rtp->direction(m_dir)))
	return false;

    bool secure = false;
    const String* sec = msg.getParam(YSTRING("crypto_suite"));
    if (sec && *sec) {
	// separate crypto parameters
	const String* key = msg.getParam(YSTRING("crypto_key"));
	if (key && *key) {
	    if (startSRTP(*sec,*key,0))
		secure = true;
	    else
		Debug(&splugin,DebugWarn,"Could not start SRTP for: '%s' '%s' [%p]",
		    sec->c_str(),key->c_str(),this);
	}
	sec = 0;
	msg.clearParam("crypto_suite");
    }
    secure = secure && setupSRTP(msg,true);
    if (!secure)
	m_rtp->security(0);

    m_rtp->dataPayload(payload);
    m_rtp->eventPayload(evpayload);
    m_rtp->setTOS(tos);
    m_rtp->padding(msg.getIntValue(YSTRING("padding"),s_padding));
    if (msg.getBoolValue(YSTRING("drillhole"),s_drill)) {
	bool ok = m_rtp->drillHole();
	Debug(&splugin,(ok ? DebugInfo : DebugWarn),
	    "Wrapper %s a hole in firewall/NAT [%p]",
	    (ok ? "opened" : "failed to open"),this);
    }
    setTimeout(msg,s_timeout);
    m_rtp->setReports(msg.getIntValue(YSTRING("rtcp_interval"),s_interval));
//    if (maxJitter > 0)
//	m_rtp->setDejitter(minJitter*1000,maxJitter*1000);
    m_bufsize = s_bufsize;
    return true;
}

bool YRTPWrapper::startUDPTL(const char* raddr, unsigned int rport, Message& msg)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::startUDPTL(\"%s\",%u) [%p]",raddr,rport,this);
    if (!m_udptl) {
	Debug(&splugin,DebugWarn,"Wrapper attempted to start UDPTL before setup! [%p]",this);
	return false;
    }

    int tos = msg.getIntValue(YSTRING("tos"),dict_tos,s_tos);
    int msec = msg.getIntValue(YSTRING("msleep"),s_sleep);
    if (!setRemote(raddr,rport,msg))
	return false;
    if (!m_udptl->initGroup(msec,Thread::priority(msg.getValue(YSTRING("thread")),s_priority)))
	return false;

    m_udptl->setTOS(tos);
    if (msg.getBoolValue(YSTRING("drillhole"),s_drill)) {
	bool ok = m_udptl->drillHole();
	Debug(&splugin,(ok ? DebugInfo : DebugWarn),
	    "Wrapper %s a hole in firewall/NAT [%p]",
	    (ok ? "opened" : "failed to open"),this);
    }
    setTimeout(msg,s_udptlTimeout);
    return setupUDPTL(msg);
}

bool YRTPWrapper::setupSRTP(Message& msg, bool buildMaster)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::setupSRTP(%s) [%p]",
	String::boolText(buildMaster),this);
    if (!m_rtp)
	return false;

    RTPSecure* srtp = m_rtp->security();
    if (!srtp) {
	if (!buildMaster)
	    return false;
	if (m_rtp->receiver())
	    srtp = m_rtp->receiver()->security();
	if (srtp)
	    srtp = new RTPSecure(*srtp);
	else
	    srtp = new RTPSecure(msg.getValue(YSTRING("crypto_suite")));
    }
    else
	buildMaster = false;

    String suite;
    String key;
    if (!(srtp->supported(m_rtp) && srtp->create(suite,key,true))) {
	if (buildMaster)
	    TelEngine::destruct(srtp);
	return false;
    }
    m_rtp->security(srtp);

    msg.setParam("ocrypto_suite",suite);
    msg.setParam("ocrypto_key",key);
    return true;
}

bool YRTPWrapper::startSRTP(const String& suite, const String& keyParams, const ObjList* paramList)
{
    Debug(&splugin,DebugAll,"YRTPWrapper::startSRTP('%s','%s',%p) [%p]",
	suite.c_str(),keyParams.c_str(),paramList,this);
    if (!(m_rtp && m_rtp->receiver()))
	return false;
    RTPSecure* srtp = new RTPSecure;
    if (srtp->supported(m_rtp) && srtp->setup(suite,keyParams,paramList)) {
	m_rtp->receiver()->security(srtp);
	Debug(&splugin,DebugNote,"Started SRTP suite '%s' [%p]",suite.c_str(),this);
	return true;
    }
    TelEngine::destruct(srtp);
    return false;
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
    m->addParam("detected","rfc2833");
    Engine::enqueue(m);
}

void YRTPWrapper::gotFax()
{
    Debug(&splugin,DebugInfo,"YRTPWrapper::gotFax() [%p]",this);
    if (m_master.null())
	return;
    Message* m = new Message("chan.masquerade");
    m->addParam("id",m_master);
    if (m_faxDivert) {
	Debug(&splugin,DebugCall,"Diverting call %s to: %s",
	    m_master.c_str(),m_faxDivert.c_str());
	m->addParam("message","call.execute");
	m->addParam("callto",m_faxDivert);
	m->addParam("reason","fax");
    }
    else {
	m->addParam("message","call.fax");
	m->addParam("detected","rfc2833");
    }
    m->addParam("caller",m_faxCaller,false);
    m->addParam("called",m_faxCalled,false);
    Engine::enqueue(m);
}

void YRTPWrapper::timeout(bool initial)
{
    if (!(initial ? s_warnFirst : s_warnLater))
	return;
    if (initial)
	m_noAudio++;
    else 
	m_lostAudio++;
    Debug(&splugin,DebugWarn,"%s timeout in%s%s wrapper [%p]",
	(initial ? "Initial" : "Later"),
	(m_master ? " channel " : ""),
	m_master.safe(),this);
    if (m_master && s_notifyMsg) {
	Message* m = new Message(s_notifyMsg);
	m->addParam("id",m_master);
	m->addParam("reason","nomedia");
	m->addParam("event","timeout");
	m->addParam("initial",String::boolText(initial));
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

DataSource* YRTPWrapper::getSource()
{
    if (m_source && m_source->ref())
	return m_source;
    return new YRTPSource(this);
}

DataConsumer* YRTPWrapper::getConsumer()
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

void YRTPWrapper::terminate(Message& msg)
{
    Debug(&splugin,DebugInfo,"YRTPWrapper::terminate() [%p]",this);
    String stats;
    if (m_rtp) {
	NamedList nl("");
	m_rtp->getStats(nl);
	nl.setParam("noaudio",String(m_noAudio));
	nl.setParam("lostaudio",String(m_lostAudio));
    	splugin.addStats(nl);
    	splugin.changed();
	m_rtp->getStats(stats);
    }
    if (m_udptl)
	m_udptl->getStats(stats);
    if (stats)
	msg.setParam("stats",stats);
    m_valid = false;
}

void YRTPWrapper::setFaxDivert(const Message& msg)
{
    NamedString* divert = msg.getParam(YSTRING("fax_divert"));
    if (!divert)
	return;
    // if divert is empty or false disable diverting
    if (divert->null() || !divert->toBoolean(true))
	m_faxDivert.clear();
    else {
	m_faxDivert = *divert;
	m_faxCaller = msg.getValue(YSTRING("fax_caller"),msg.getValue(YSTRING("caller"),m_faxCaller));
	m_faxCalled = msg.getValue(YSTRING("fax_called"),msg.getValue(YSTRING("called"),m_faxCalled));
    }
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
    source->Forward(block,timestamp,(marker ? DataNode::DataMark : 0));
    block.clear(false);
    source->busy(false);
    return true;
}

bool YRTPSession::rtpRecvEvent(int event, char key, int duration,
	int volume, unsigned int timestamp)
{
    if (!m_wrap)
	return false;
    if (key) {
	m_wrap->gotDTMF(key);
	return true;
    }
    if (event == 36) {
	// got G3 CNG
	if (m_getFax) {
	    m_getFax = false;
	    m_wrap->gotFax();
	}
	return true;
    }
    return false;
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

Cipher* YRTPSession::createCipher(const String& name, Cipher::Direction dir)
{
    Message msg("engine.cipher");
    msg.addParam("cipher",name);
    msg.addParam("direction",lookup(dir,Cipher::directions(),"unknown"));
    CipherHolder* cHold = new CipherHolder;
    msg.userData(cHold);
    cHold->deref();
    return Engine::dispatch(msg) ? cHold->cipher() : 0;
}

bool YRTPSession::checkCipher(const String& name)
{
    Message msg("engine.cipher");
    msg.addParam("cipher",name);
    return Engine::dispatch(msg);
}


YUDPTLSession::~YUDPTLSession()
{
    // disconnect thread and transport before our virtual methods become invalid
    // this will also lock the group preventing rtpRecvData from being called
    group(0);
    transport(0);
}

void YUDPTLSession::udptlRecv(const void* data, int len, u_int16_t seq, bool recovered)
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
	return;
    // the source will not be destroyed until we reset the busy flag
    DataBlock block;
    block.assign((void*)data, len, false);
    // use the sequence number as timestamp, duplicates are possible
    source->Forward(block,seq);
    block.clear(false);
    source->busy(false);
}

void YUDPTLSession::timeout(bool initial)
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
	m_format = m_wrap->m_format;
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
	m_format = m_wrap->m_format;
	if (m_format)
	    setSplitable();
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

unsigned long YRTPConsumer::Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags)
{
    if (!(m_wrap && m_wrap->valid()))
	return 0;
    const char* ptr = (const char*)data.data();
    unsigned int len = data.length();
    if (len && m_wrap->udptl()) {
	XDebug(&splugin,DebugAll,"YRTPConsumer writing %d UDPTL bytes, ts=%lu [%p]",
	    data.length(),tStamp,this);
	// for T.38 the timestamp is used as sequence number
	m_wrap->udptl()->udptlSend(ptr,len,tStamp);
	return invalidStamp();
    }
    if (!(m_wrap->bufSize() && m_wrap->rtp()))
	return 0;
    XDebug(&splugin,DebugAll,"YRTPConsumer writing %d bytes, ts=%lu [%p]",
	data.length(),tStamp,this);
    unsigned int buf = m_wrap->bufSize();
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
	bool mark = (flags & DataMark) != 0;
	flags &= ~DataMark;
	m_wrap->rtp()->rtpSendData(mark,tStamp,ptr,sz);
	// if timestamp increment is not provided we have to guess...
	tStamp += sz;
	len -= sz;
	ptr += sz;
    }
    return invalidStamp();
}


void YRTPMonitor::updateTimes(u_int64_t when)
{
    if (!m_start)
	m_start = when;
    m_last = when;
}

void YRTPMonitor::rtpData(const void* data, int len)
{
    updateTimes(Time::now());
    m_rtpPackets++;
    m_rtpBytes += len;
    // we already know data is at least 12 bytes (RTP header) long
    m_payload = 0x7f & ((const unsigned char*)data)[1];
}

void YRTPMonitor::rtcpData(const void* data, int len)
{
    updateTimes(Time::now());
    m_rtcpPackets++;
}

void YRTPMonitor::timerTick(const Time& when)
{
    if (!(m_id && m_last))
	return;
    u_int64_t tout = 1000 * s_timeout;
    if (tout && ((m_last + tout) < when.usec()))
	timeout(0 == m_start);
}

void YRTPMonitor::timeout(bool initial)
{
    if (null(m_id))
	return;
    if (!(initial ? s_warnFirst : s_warnLater))
	return;
    Debug(&splugin,DebugWarn,"%s timeout in '%s' reflector [%p]",
	(initial ? "Initial" : "Later"),m_id->c_str(),this);
    if (s_notifyMsg) {
	Message* m = new Message(s_notifyMsg);
	m->addParam("id",m_id->c_str());
	m->addParam("reason","nomedia");
	m->addParam("event","timeout");
	m->addParam("initial",String::boolText(initial));
	Engine::enqueue(m);
    }
    // been there, done that, enough
    m_id = 0;
}

void YRTPMonitor::startup()
{
    if (0 == m_last)
	m_last = Time::now();
}

void YRTPMonitor::saveStats(Message& msg) const
{
    unsigned int d = m_start ? ((m_last - m_start + 500000) / 1000000) : 0;
    msg.addParam("rtp_rx_packets",String(m_rtpPackets));
    msg.addParam("rtcp_rx_packets",String(m_rtcpPackets));
    msg.addParam("rtp_rx_bytes",String(m_rtpBytes));
    msg.addParam("rtp_rx_duration",String(d));
    if (m_payload >= 0)
	msg.addParam("rtp_rx_payload",String(m_payload));
}


YRTPReflector::YRTPReflector(const String& id, bool passiveA, bool passiveB)
    : m_idA(id)
{
    DDebug(&splugin,DebugInfo,"YRTPReflector::YRTPReflector('%s') [%p]",id.c_str(),this);
    m_group = new RTPGroup(s_sleep,s_priority);
    m_rtpA = new RTPTransport;
    m_rtpB = new RTPTransport;
    m_rtpA->setProcessor(m_rtpB);
    m_rtpB->setProcessor(m_rtpA);
    m_monA = new YRTPMonitor(passiveA ? 0 : &m_idA);
    m_rtpA->setMonitor(m_monA);
    m_monB = new YRTPMonitor(passiveB ? 0 : &m_idB);
    m_rtpB->setMonitor(m_monB);
    m_group->join(m_rtpA);
    m_group->join(m_rtpB);
    m_group->join(m_monA);
    m_group->join(m_monB);
}

YRTPReflector::~YRTPReflector()
{
    DDebug(&splugin,DebugInfo,"YRTPReflector::~YRTPReflector() [%p]",this);
    m_rtpA->setProcessor();
    m_rtpA->setMonitor();
    m_rtpB->setProcessor();
    m_rtpB->setMonitor();
    m_group->part(m_rtpA);
    m_group->part(m_monA);
    m_group->part(m_rtpB);
    m_group->part(m_monB);
    m_group = 0;
    TelEngine::destruct(m_rtpA);
    TelEngine::destruct(m_rtpB);
    TelEngine::destruct(m_monA);
    TelEngine::destruct(m_monB);
}


bool AttachHandler::received(Message &msg)
{
    int more = 2;
    String src(msg.getValue(YSTRING("source")));
    if (src.null())
	more--;
    else {
	if (src.startSkip("rtp/",false))
	    more--;
	else
	    src = "";
    }

    String cons(msg.getValue(YSTRING("consumer")));
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

    const char* media = msg.getValue(YSTRING("media"),"audio");
    String rip(msg.getValue(YSTRING("remoteip")));
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (!ch) {
	if (!src.null())
	    Debug(&splugin,DebugWarn,"RTP source '%s' attach request with no call channel!",src.c_str());
	if (!cons.null())
	    Debug(&splugin,DebugWarn,"RTP consumer '%s' attach request with no call channel!",cons.c_str());
	return false;
    }

    RefPointer<YRTPWrapper> w = YRTPWrapper::find(ch,media);
    if (!w)
	w = YRTPWrapper::find(msg.getValue(YSTRING("rtpid")));
    if (!w) {
	String lip(msg.getValue(YSTRING("localip")));
	if (lip.null())
	    YRTPWrapper::guessLocal(rip,lip);
	w = new YRTPWrapper(lip,ch,media,RTPSession::SendRecv,msg);
	w->setMaster(msg.getValue(YSTRING("id")));

	if (!src.null()) {
	    DataSource* s = w->getSource();
	    ch->setSource(s,media);
	    s->deref();
	}

	if (!cons.null()) {
	    DataConsumer* c = w->getConsumer();
	    ch->setConsumer(c,media);
	    c->deref();
	}
    }

    w->deref();
    if (w->refcount() <= 1)
	return false;

    w->setParams(rip,msg);
    w->setFaxDivert(msg);
    msg.setParam("localip",w->host());
    msg.setParam("localport",String(w->port()));
    msg.setParam("rtpid",w->id());

    // Stop dispatching if we handled all requested
    return !more;
}


bool RtpHandler::received(Message &msg)
{
    bool udptl = false;
    String trans = msg.getValue(YSTRING("transport"));
    if (trans && !trans.startsWith("RTP/")) {
	if (trans &= "udptl")
	    udptl = true;
	else
	    return false;
    }
    Debug(&splugin,DebugAll,"%s message received",(trans ? trans.c_str() : "No-transport"));
    bool terminate = msg.getBoolValue(YSTRING("terminate"),false);
    String dir(msg.getValue(YSTRING("direction")));
    RTPSession::Direction direction = terminate ? RTPSession::FullStop : RTPSession::SendRecv;
    bool d_recv = false;
    bool d_send = false;
    if (dir == YSTRING("bidir")) {
	d_recv = true;
	d_send = true;
    }
    else if (dir == YSTRING("receive")) {
	d_recv = true;
	direction = RTPSession::RecvOnly;
    }
    else if (dir == YSTRING("send")) {
	d_send = true;
	direction = RTPSession::SendOnly;
    }

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    DataEndpoint* de = YOBJECT(DataEndpoint,msg.userData());
    const char* media = udptl ? "image" : "audio";
    media = msg.getValue(YSTRING("media"),(de ? de->name().c_str() : media));
    RefPointer<YRTPWrapper> w = YRTPWrapper::find(ch,media);
    if (w)
	Debug(&splugin,DebugAll,"Wrapper %p found by CallEndpoint %p",(YRTPWrapper*)w,ch);
    else {
	const char* rid = msg.getValue(YSTRING("rtpid"));
	w = YRTPWrapper::find(rid);
	if (w)
	    Debug(&splugin,DebugAll,"Wrapper %p found by ID '%s'",(YRTPWrapper*)w,rid);
    }
    if (w)
	w->deref();
    if (terminate) {
	if (w) {
	    w->terminate(msg);
	    return true;
	}
	return false;
    }
    if (!(ch || de || w)) {
	Debug(&splugin,DebugWarn,"Neither call channel nor RTP wrapper found!");
	return false;
    }

    String rip(msg.getValue(YSTRING("remoteip")));

    if (!w) {
	// it would be pointless to create an unreferenced wrapper
	if (!(d_recv || d_send))
	    return false;
	String lip(msg.getValue(YSTRING("localip")));
	if (lip.null())
	    YRTPWrapper::guessLocal(rip,lip);
	if (lip.null()) {
	    Debug(&splugin,DebugWarn,"RTP request with no local address!");
	    return false;
	}

	w = new YRTPWrapper(lip,ch,media,direction,msg,udptl);
	w->setMaster(msg.getValue(YSTRING("id")));

	w->deref();
    }
    else if (w->valid())
	w->addDirection(direction);
    else
	return false;

    if (d_recv) {
	if (ch && !ch->getSource(media)) {
	    DataSource* s = w->getSource();
	    ch->setSource(s,media);
	    s->deref();
	}
	else if (de && !de->getSource()) {
	    DataSource* s = w->getSource();
	    de->setSource(s);
	    s->deref();
	}
    }

    if (d_send) {
	if (ch && !ch->getConsumer(media)) {
	    DataConsumer* c = w->getConsumer();
	    ch->setConsumer(c,media);
	    c->deref();
	}
	else if (de && !de->getConsumer()) {
	    DataConsumer* c = w->getConsumer();
	    de->setConsumer(c);
	    c->deref();
	}
    }

    if (w->refcount() <= 1)
	return false;

    w->setParams(rip,msg);
    w->setFaxDivert(msg);
    msg.setParam("localip",w->host());
    msg.setParam("localport",String(w->port()));
    msg.setParam("rtpid",w->id());

    if (msg.getBoolValue(YSTRING("getsession"),!msg.userData()))
	msg.userData(w);
    return true;
}


bool DTMFHandler::received(Message &msg)
{
    String targetid(msg.getValue(YSTRING("targetid")));
    if (targetid.null())
	return false;
    String text(msg.getValue(YSTRING("text")));
    if (text.null())
	return false;
    RefPointer<YRTPWrapper> wrap = YRTPWrapper::find(targetid);
    if (!wrap)
	return false;
    wrap->deref();
    if (wrap->rtp()) {
	Debug(&splugin,DebugInfo,"RTP DTMF '%s' targetid '%s'",text.c_str(),targetid.c_str());
	int duration = msg.getIntValue(YSTRING("duration"));
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
    s_mirrors.clear();
}

void YRTPPlugin::addStats(const NamedList& stats)
{
    DDebug(this,DebugAll,"Add updates");
    s_mutex.lock();
    m_updates.append(new NamedList(stats));
    s_mutex.unlock();
}

void YRTPPlugin::genUpdate(Message& msg)
{
    DDebug(this,DebugAll,"::genUpdate() [%p] updates=%d",this,m_updates.count());
    Lock l(s_mutex);
    if (m_updates.count() == 0)
	return;
    NamedList* updateParams = static_cast<NamedList*>(m_updates.remove(false));
    msg.copyParams(*updateParams);
    TelEngine::destruct(updateParams);
}

void YRTPPlugin::statusParams(String& str)
{
    s_mutex.lock();
    str.append("chans=",",") << s_calls.count();
    s_mutex.unlock();
    s_refMutex.lock();
    str.append("mirrors=",",") << s_mirrors.count();
    s_refMutex.unlock();
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
    s_refMutex.lock();
    for (l = s_mirrors.skipNull(); l; l=l->skipNext()) {
	YRTPReflector* r = static_cast<YRTPReflector*>(l->get());
        str.append(r->idA(),",") << "=" << r->idB().safe("?");
    }
    s_refMutex.unlock();
}

static Regexp s_reflectMatch(
    "^\\(.*o=[^[:cntrl:]]\\+ IN IP4 \\)"
    "\\([0-9]\\+\\.[0-9]\\+\\.[0-9]\\+\\.[0-9]\\+\\)"
    "\\([[:cntrl:]].*c=IN IP4 \\)"
    "\\([0-9]\\+\\.[0-9]\\+\\.[0-9]\\+\\.[0-9]\\+\\)"
    "\\([[:cntrl:]].*m=audio \\)"
    "\\([0-9]\\+\\)"
    "\\( RTP/.*\\)$"
);

bool YRTPPlugin::reflectSetup(Message& msg, const char* id, RTPTransport& rtp,
    const char* rHost, const char* leg)
{
    String lip(msg.getValue(YSTRING("rtp_localip")));
    if (lip.null())
	YRTPWrapper::guessLocal(rHost,lip);
    SocketAddr addr(AF_INET);
    if (!addr.host(lip)) {
	Debug(this,DebugWarn,"Bad local RTP address '%s' for %s '%s'",
	    lip.c_str(),leg,id);
	return false;
    }

    int minport = msg.getIntValue(YSTRING("rtp_minport"),s_minport);
    int maxport = msg.getIntValue(YSTRING("rtp_maxport"),s_maxport);
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
    bool rtcp = msg.getBoolValue(YSTRING("rtp_rtcp"),s_rtcp);
    for (;;) {
	int lport = (minport + (Random::random() % (maxport - minport))) & 0xfffe;
	addr.port(lport);
	if (rtp.localAddr(addr,rtcp)) {
	    Debug(this,DebugInfo,"Reflector %s for '%s' bound to %s:%u%s",
		leg,id,lip.c_str(),lport,(rtcp ? " +RTCP" : ""));
	    break;
	}
	if (--attempt <= 0) {
	    Debug(this,DebugWarn,"Could not bind reflector %s for '%s' in range %d - %d",
		leg,id,minport,maxport);
	    return false;
	}
    }
    return true;
}

bool YRTPPlugin::reflectStart(Message& msg, const char* id, RTPTransport& rtp,
    SocketAddr& rAddr)
{
    if (!rtp.remoteAddr(rAddr,msg.getBoolValue(YSTRING("rtp_autoaddr"),s_autoaddr))) {
	Debug(this,DebugWarn,"Could not set remote RTP address for '%s'",id);
	return false;
    }
    if (msg.getBoolValue(YSTRING("rtp_drillhole"),s_drill)) {
	bool ok = rtp.drillHole();
	Debug(this,(ok ? DebugInfo : DebugWarn),
	    "Reflector for '%s' %s a hole in firewall/NAT",
	    id,(ok ? "opened" : "failed to open"));
    }
    return true;
}

void YRTPPlugin::reflectDrop(YRTPReflector*& refl, Lock& mylock)
{
    s_mirrors.remove(refl,false);
    mylock.drop();
    Message* m = new Message("call.drop");
    m->addParam("id",refl->idA());
    m->addParam("reason","nomedia");
    TelEngine::destruct(refl);
    Engine::enqueue(m);
}

void YRTPPlugin::reflectExecute(Message& msg)
{
    const String* id = msg.getParam(YSTRING("id"));
    if (null(id))
	return;
    String* sdp = msg.getParam(YSTRING("sdp_raw"));
    if (null(sdp))
	return;
    if (!(msg.getBoolValue(YSTRING("rtp_forward"),false) && msg.getBoolValue(YSTRING("rtp_reflect"),false)))
	return;
    DDebug(this,DebugAll,"YRTPPlugin::reflectExecute() A='%s'",id->c_str());
    // we have a candidate
    if (!sdp->matches(s_reflectMatch)) {
	Debug(this,DebugWarn,"Unable to match SDP to reflect RTP for '%s'",id->c_str());
	return;
    }
    SocketAddr ra(AF_INET);
    if (!(ra.host(sdp->matchString(4)) && ra.port(sdp->matchString(6).toInteger(-1)) && ra.valid())) {
	Debug(this,DebugWarn,"Invalid RTP transport address for '%s'",id->c_str());
	return;
    }
    const char* aHost = msg.getValue(YSTRING("rtp_addr"),ra.host().c_str());
    const char* bHost = msg.getValue(YSTRING("rtp_remoteip"),aHost);
    YRTPReflector* r = new YRTPReflector(*id,
	(sdp->find("a=recvonly") >= 0),(sdp->find("a=sendonly") >= 0));
    if (!(reflectSetup(msg,id->c_str(),r->rtpA(),aHost,"A") &&
	reflectStart(msg,id->c_str(),r->rtpA(),ra) &&
	reflectSetup(msg,id->c_str(),r->rtpB(),bHost,"B"))) {
	TelEngine::destruct(r);
	return;
    }
    String templ;
    templ << "\\1" << r->rtpB().localAddr().host();
    templ << "\\3" << r->rtpB().localAddr().host();
    templ << "\\5" << r->rtpB().localAddr().port() << "\\7";
    *sdp = sdp->replaceMatches(templ);
    s_refMutex.lock();
    s_mirrors.append(r);
    s_refMutex.unlock();
}

void YRTPPlugin::reflectAnswer(Message& msg, bool ignore)
{
    const String* peerid = msg.getParam(YSTRING("peerid"));
    if (null(peerid))
	return;
    YRTPReflector* r = 0;
    Lock mylock(s_refMutex);
    ObjList* l = s_mirrors.skipNull();
    for (; l; l=l->skipNext()) {
	r = static_cast<YRTPReflector*>(l->get());
	if (r->idA() == *peerid)
	    break;
	r = 0;
    }
    if (!r)
	return;
    DDebug(this,DebugAll,"YRTPPlugin::reflectAnswer() A='%s'",peerid->c_str());
    const String* id = msg.getParam(YSTRING("id"));
    if (null(id)) {
	if (ignore)
	    return;
	Debug(this,DebugWarn,"Peer of RTP reflection '%s' answered without ID",peerid->c_str());
	reflectDrop(r,mylock);
	return;
    }
    if (r->idB() && (r->idB() != *id)) {
	Debug(this,DebugWarn,"Reflect target of '%s' changed from '%s' to '%s'",
	    peerid->c_str(),r->idB().c_str(),id->c_str());
	reflectDrop(r,mylock);
	return;
    }
    String* sdp = msg.getParam(YSTRING("sdp_raw"));
    if (null(sdp) || !msg.getBoolValue(YSTRING("rtp_forward"),false)) {
	if (ignore)
	    return;
	Debug(this,DebugWarn,"Unable to complete RTP reflection for '%s'",peerid->c_str());
	reflectDrop(r,mylock);
	return;
    }
    if (!sdp->matches(s_reflectMatch)) {
	if (ignore)
	    return;
	Debug(this,DebugWarn,"Unable to match SDP to reflect RTP for '%s'",id->c_str());
	reflectDrop(r,mylock);
	return;
    }
    if (r->idB().null())
	r->setB(*id);
    SocketAddr ra(AF_INET);
    if (!(ra.host(sdp->matchString(4)) && ra.port(sdp->matchString(6).toInteger(-1)) && ra.valid())) {
	Debug(this,DebugWarn,"Invalid RTP transport address for '%s'",id->c_str());
	reflectDrop(r,mylock);
	return;
    }
    if (!reflectStart(msg,id->c_str(),r->rtpB(),ra)) {
	reflectDrop(r,mylock);
	return;
    }
    r->monA().startup();
    r->monB().startup();
    String templ;
    templ << "\\1" << r->rtpA().localAddr().host();
    templ << "\\3" << r->rtpA().localAddr().host();
    templ << "\\5" << r->rtpA().localAddr().port() << "\\7";
    *sdp = sdp->replaceMatches(templ);
}

void YRTPPlugin::reflectHangup(Message& msg)
{
    const String* id = msg.getParam(YSTRING("id"));
    if (null(id))
	return;
    Lock mylock(s_refMutex);
    ObjList* l = s_mirrors.skipNull();
    for (; l; l=l->skipNext()) {
	YRTPReflector* r = static_cast<YRTPReflector*>(l->get());
	if (r->idA() == *id) {
	    DDebug(this,DebugAll,"YRTPPlugin::reflectHangup() A='%s' B='%s'",
		id->c_str(),r->idB().c_str());
	    r->setA(String::empty());
	    r->monA().saveStats(msg);
	    if (r->idB())
		return;
	}
	else if (r->idB() == *id) {
	    DDebug(this,DebugAll,"YRTPPlugin::reflectHangup() B='%s' A='%s'",
		id->c_str(),r->idA().c_str());
	    r->setB(String::empty());
	    r->monB().saveStats(msg);
	    if (r->idA())
		return;
	}
	else
	    continue;
	s_mirrors.remove(r,false);
	mylock.drop();
	TelEngine::destruct(r);
	break;
    }
}

bool YRTPPlugin::received(Message& msg, int id)
{
    switch (id) {
	case Execute:
	    reflectExecute(msg);
	    return false;
	case Ringing:
	case Progress:
	    reflectAnswer(msg,true);
	    return false;
	case Answered:
	    reflectAnswer(msg,false);
	    return false;
	case Private:
	    reflectHangup(msg);
	    return false;
	default:
	    return Module::received(msg,id);
    }
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
    s_tos = cfg.getIntValue("general","tos",dict_tos);
    s_localip = cfg.getValue("general","localip");
    s_autoaddr = cfg.getBoolValue("general","autoaddr",true);
    s_anyssrc = cfg.getBoolValue("general","anyssrc",false);
    s_padding = cfg.getIntValue("general","padding",0);
    s_rtcp = cfg.getBoolValue("general","rtcp",true);
    s_interval = cfg.getIntValue("general","rtcp_interval",4500);
    s_drill = cfg.getBoolValue("general","drillhole",Engine::clientMode());
    s_sleep = cfg.getIntValue("general","defsleep",5);
    RTPGroup::setMinSleep(cfg.getIntValue("general","minsleep"));
    s_priority = Thread::priority(cfg.getValue("general","thread"));
    s_timeout = cfg.getIntValue("timeouts","timeout",3000);
    s_udptlTimeout = cfg.getIntValue("timeouts","udptl_timeout",25000);
    s_notifyMsg = cfg.getValue("timeouts","notifymsg");
    s_warnFirst = cfg.getBoolValue("timeouts","warnfirst",true);
    s_warnLater = cfg.getBoolValue("timeouts","warnlater",false);
    setup();
    if (m_first) {
	m_first = false;
	installRelay(Execute,50);
	installRelay(Ringing,50);
	installRelay(Progress,50);
	installRelay(Answered,50);
	installRelay(Private,"chan.hangup",50);
	Engine::install(new AttachHandler);
	Engine::install(new RtpHandler);
	Engine::install(new DTMFHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
