/**
 * h323chan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * As a special exception to the GNU General Public License, permission is 
 * granted for additional uses of the text contained in this release of Yate 
 * as noted here.
 * This exception is that permission is hereby granted to link Yate with the
 * OpenH323 and PWLIB runtime libraries to produce an executable image.
 * 
 * H.323 channel
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

#include <ptlib.h>
#include <h323.h>
#include <h323pdu.h>
#include <ptclib/delaychan.h>
#include <gkserver.h>

#ifndef OPENH323_VERSION
#define OPENH323_VERSION "SomethingOld"
#endif

/* Define a easily comparable version, 2 digits for each component */
#define OPENH323_NUMVERSION ((OPENH323_MAJOR)*10000 + (OPENH323_MINOR)*100 + (OPENH323_BUILD))

#if (OPENH323_NUMVERSION < 11202)
#error Open H323 version too old
#endif

/* Guess if we have a QOS parameter to the RTP channel creation */
#if (OPENH323_NUMVERSION >= 11304)
#define NEED_RTP_QOS_PARAM 1
#endif

#if (OPENH323_NUMVERSION >= 11404)
#define USE_CAPABILITY_FACTORY
#endif

#include <telengine.h>
#include <telephony.h>
#include <yateversn.h>

#include <string.h>

using namespace TelEngine;

static Mutex s_calls;
static Mutex s_route;

static bool s_externalRtp;
static bool s_passtrough;
static int s_maxqueue = 5;
static int s_maxconns = 0;

static Configuration s_cfg;

static TokenDict dict_str2code[] = {
    { "alpha" , PProcess::AlphaCode },
    { "beta" , PProcess::BetaCode },
    { "release" , PProcess::ReleaseCode },
    { 0 , 0 },
};

const char* h323_formats[] = {
    "G.711-ALaw-64k", "alaw",
    "G.711-uLaw-64k", "mulaw",
    "GSM-06.10", "gsm",
    "MS-GSM", "msgsm",
    "SpeexNarrow", "speex",
    "LPC-10", "lpc10",
    "iLBC", "ilbc",
    "G.723", "g723",
    "G.726", "g726",
    "G.728", "g728",
    "G.729", "g729",
    "PCM-16", "slin",
#if 0
    "G.729A", "g729a",
    "G.729B", "g729b",
    "G.729A/B", "g729ab",
    "G.723.1", "g723.1",
    "G.723.1(5.3k)", "g723.1-5k3",
    "G.723.1A(5.3k)", "g723.1a-5k3",
    "G.723.1A(6.3k)", "g723.1a-6k3",
    "G.723.1A(6.3k)-Cisco", "g723.1a-6k3-cisco",
    "G.726-16k", "g726-16k",
    "G.726-24k", "g726-24k",
    "G.726-32k", "g726-32k",
    "G.726-40k", "g726-40k",
    "iLBC-15k2", "ilbc-15k2",
    "iLBC-13k3", "ilbc-13k3",
    "SpeexNarrow-18.2k", "speex-18k2",
    "SpeexNarrow-15k", "speex-15k",
    "SpeexNarrow-11k", "speex-11k",
    "SpeexNarrow-8k", "speex-8k",
    "SpeexNarrow-5.95k", "speex-5k95",
#endif
    0
};

static TokenDict dict_h323_dir[] = {
    { "receive", H323Channel::IsReceiver },
    { "send", H323Channel::IsTransmitter },
    { "bidir", H323Channel::IsBidirectional },
    { 0 , 0 },
};

static TokenDict dict_silence[] = {
    { "none", H323AudioCodec::NoSilenceDetection },
    { "fixed", H323AudioCodec::FixedSilenceDetection },
    { "adaptive", H323AudioCodec::AdaptiveSilenceDetection },
    { 0 , 0 },
};

static const char* CallEndReasonText(int reason)
{
#define MAKE_END_REASON(r) case H323Connection::r: return #r
    switch (reason) {
	MAKE_END_REASON(EndedByLocalUser);
	MAKE_END_REASON(EndedByNoAccept);
	MAKE_END_REASON(EndedByAnswerDenied);
	MAKE_END_REASON(EndedByRemoteUser);
	MAKE_END_REASON(EndedByRefusal);
	MAKE_END_REASON(EndedByNoAnswer);
	MAKE_END_REASON(EndedByCallerAbort);
	MAKE_END_REASON(EndedByTransportFail);
	MAKE_END_REASON(EndedByConnectFail);
	MAKE_END_REASON(EndedByGatekeeper);
	MAKE_END_REASON(EndedByNoUser);
	MAKE_END_REASON(EndedByNoBandwidth);
	MAKE_END_REASON(EndedByCapabilityExchange);
	MAKE_END_REASON(EndedByCallForwarded);
	MAKE_END_REASON(EndedBySecurityDenial);
	MAKE_END_REASON(EndedByLocalBusy);
	MAKE_END_REASON(EndedByLocalCongestion);
	MAKE_END_REASON(EndedByRemoteBusy);
	MAKE_END_REASON(EndedByRemoteCongestion);
	MAKE_END_REASON(EndedByUnreachable);
	MAKE_END_REASON(EndedByNoEndPoint);
	MAKE_END_REASON(EndedByHostOffline);
	MAKE_END_REASON(EndedByTemporaryFailure);
	MAKE_END_REASON(EndedByQ931Cause);
	MAKE_END_REASON(EndedByDurationLimit);
	MAKE_END_REASON(EndedByInvalidConferenceID);
	case H323Connection::NumCallEndReasons: return "CallStillActive";
	default: return "UnlistedCallEndReason";
    }
#undef MAKE_END_REASON
}

class H323Process : public PProcess
{
    PCLASSINFO(H323Process, PProcess)
    H323Process()
	: PProcess(
	    s_cfg.getValue("general","vendor","Null Team"),
	    s_cfg.getValue("general","product","YATE"),
	    (unsigned short)s_cfg.getIntValue("general","major",YATE_MAJOR),
	    (unsigned short)s_cfg.getIntValue("general","minor",YATE_MINOR),
	    (PProcess::CodeStatus)s_cfg.getIntValue("general","status",dict_str2code,PProcess::AlphaCode),
	    (unsigned short)s_cfg.getIntValue("general","build",YATE_BUILD)
	    )
	{ Resume(); }
public:
    void Main()
	{ }
};

class YateH323EndPoint;
class YateGatekeeperServer;

class YateGatekeeperCall : public H323GatekeeperCall
{
    PCLASSINFO(YateGatekeeperCall, H323GatekeeperCall);
  public:
    YateGatekeeperCall(
      YateGatekeeperServer & server,
      const OpalGloballyUniqueID & callIdentifier, /// Unique call identifier
      Direction direction
    );

    virtual H323GatekeeperRequest::Response OnAdmission(
      H323GatekeeperARQ & request
    );
};

class YateGatekeeperServer : public H323GatekeeperServer
{
    PCLASSINFO(YateGatekeeperServer, H323GatekeeperServer);
  public:
    YateGatekeeperServer(YateH323EndPoint & ep);
    BOOL Init();
    H323GatekeeperRequest::Response OnRegistration(
		          H323GatekeeperRRQ & request);
    H323GatekeeperRequest::Response OnUnregistration(
		          H323GatekeeperURQ & request );
    H323GatekeeperCall * CreateCall(const OpalGloballyUniqueID & id,H323GatekeeperCall::Direction dir);
    BOOL TranslateAliasAddressToSignalAddress(const H225_AliasAddress & alias,H323TransportAddress & address);
    virtual BOOL GetUsersPassword(const PString & alias,PString & password) const;

  private:
    YateH323EndPoint & endpoint;
};

class YateH323AudioSource : public DataSource, public PIndirectChannel
{
    PCLASSINFO(YateH323AudioSource, PIndirectChannel)
public:
    YateH323AudioSource()
	: m_exit(false)
	{ Debug(DebugAll,"h.323 source [%p] created",this); }
    ~YateH323AudioSource();
    virtual BOOL Close(); 
    virtual BOOL IsOpen() const;
    virtual BOOL Write(const void *buf, PINDEX len);
private:
    PAdaptiveDelay writeDelay;
    DataBlock m_data;
    bool m_exit;
};

class YateH323AudioConsumer : public DataConsumer, public PIndirectChannel
{
    PCLASSINFO(YateH323AudioConsumer, PIndirectChannel)
public:
    YateH323AudioConsumer()
	: m_exit(false)
	{ Debug(DebugAll,"h.323 consumer [%p] created",this); }
    ~YateH323AudioConsumer();
    virtual BOOL Close(); 
    virtual BOOL IsOpen() const;
    virtual BOOL Read(void *buf, PINDEX len);
    virtual void Consume(const DataBlock &data, unsigned long timeDelta);
private:
    PAdaptiveDelay readDelay;
    DataBlock m_buffer;
    bool m_exit;
    Mutex m_mutex;
};

class YateH323_ExternalRTPChannel;

class YateH323EndPoint : public H323EndPoint
{
    PCLASSINFO(YateH323EndPoint, H323EndPoint)
public:
    YateH323EndPoint();
    ~YateH323EndPoint();
    virtual H323Connection *CreateConnection(unsigned callReference, void *userData,
	H323Transport *transport, H323SignalPDU *setupPDU);
    bool Init(void);
    YateGatekeeperServer *gkServer;
};

class YateH323Connection :  public H323Connection, public DataEndpoint
{
    PCLASSINFO(YateH323Connection, H323Connection)
public:
    YateH323Connection(YateH323EndPoint &endpoint, unsigned callReference, void *userdata);
    ~YateH323Connection();
    virtual H323Connection::AnswerCallResponse OnAnswerCall(const PString &caller,
	const H323SignalPDU &signalPDU, H323SignalPDU &connectPDU);
    virtual void OnEstablished();
    virtual void OnCleared();
    virtual BOOL OnAlerting(const H323SignalPDU &alertingPDU, const PString &user);
    virtual void OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp);
    virtual void OnUserInputString(const PString &value);
    virtual BOOL OpenAudioChannel(BOOL isEncoding, unsigned bufferSize,
	H323AudioCodec &codec);
    virtual void OnSetLocalCapabilities();
#ifdef NEED_RTP_QOS_PARAM
    virtual H323Channel *CreateRealTimeLogicalChannel(const H323Capability & capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters *param,RTP_QOS * rtpqos = NULL);
#else
    virtual H323Channel *CreateRealTimeLogicalChannel(const H323Capability & capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters *param);
#endif
    virtual BOOL OnStartLogicalChannel(H323Channel & channel);
    virtual BOOL OnCreateLogicalChannel(const H323Capability & capability, H323Channel::Directions dir, unsigned & errorCode ) ;
    BOOL StartExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan);
    void OnStoppedExternal(H323Channel::Directions dir);
    void SetRemoteAddress(const char* remoteIP, WORD remotePort);
    virtual void disconnected(bool final, const char *reason);
    void rtpExecuted(Message& msg);
    void rtpForward(Message& msg, bool init = false);
    static BOOL decodeCapability(const H323Capability & capability, const char** dataFormat, int *payload = 0, String* capabName = 0);
    inline const String &id() const
	{ return m_id; }
    inline const String &status() const
	{ return m_status; }
    inline void setStatus(const char *status)
	{ m_status = status; }
    inline void setTarget(const char *target = 0)
	{ m_targetid = target; }
    inline const String& getTarget() const
	{ return m_targetid; }
    inline static int total()
	{ return s_total; }
    inline bool HasRemoteAddress() const
	{ return s_passtrough && (m_remotePort > 0); }
private:
    bool m_nativeRtp;
    bool m_passtrough;
    String m_id;
    String m_status;
    String m_targetid;
    String m_formats;
    String m_rtpAddr;
    int m_rtpPort;
    String m_remoteFormats;
    String m_remoteAddr;
    int m_remotePort;
    static int s_total;
};

// this part have been inspired (more or less) from chan_h323 of project asterisk, credits to Jeremy McNamara for chan_h323 and to Mark Spencer for asterisk.
class YateH323_ExternalRTPChannel : public H323_ExternalRTPChannel
{
	PCLASSINFO(YateH323_ExternalRTPChannel, H323_ExternalRTPChannel);
public:
	/* Create a new channel. */
	YateH323_ExternalRTPChannel(
      		YateH323Connection & connection,        
      		const H323Capability & capability,  
      		Directions direction,               
      		unsigned sessionID,                 
      		const PIPSocket::Address & ip,      
      		WORD dataPort);
	/* Destructor */
	~YateH323_ExternalRTPChannel();
	BOOL Start();
	
     	BOOL OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters & param);
	BOOL OnSendingPDU( H245_H2250LogicalChannelParameters & param );
	BOOL OnReceivedPDU(const H245_H2250LogicalChannelParameters & param,unsigned & errorCode);
private:
	YateH323Connection *m_conn;
};


class H323Handler : public MessageHandler
{
public:
    H323Handler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class H323Dropper : public MessageHandler
{
public:
    H323Dropper(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class H323ConnHandler : public MessageReceiver
{
public:
    enum {
        Ringing,
        Answered,
        DTMF,
	Text,
    };
    virtual bool received(Message &msg, int id);
};

class H323Stopper : public MessageHandler
{
public:
    H323Stopper(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class H323MsgThread : public Thread
{
public:
    H323MsgThread(Message *msg, const char *id)
	: Thread("H323MsgThread"), m_msg(msg), m_id(id) { }
    virtual void run();
    virtual void cleanup();
    bool route();
    inline static int count()
	{ return s_count; }
    inline static int routed()
	{ return s_routed; }
private:
    Message *m_msg;
    String m_id;
    static int s_count;
    static int s_routed;
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("engine.status") { }
    virtual bool received(Message &msg);
};

class H323Plugin : public Plugin
{
public:
    H323Plugin();
    virtual ~H323Plugin();
    virtual void initialize();
    virtual bool isBusy() const;
    void cleanup();
    YateH323Connection *findConnectionLock(const String& id);
    inline YateH323EndPoint *ep()
	{ return m_endpoint; }
    inline ObjList &calls()
	{ return m_calls; }
private:
    bool m_first;
    ObjList m_calls;
    YateH323EndPoint *m_endpoint;
    static H323Process *m_process;
};

H323Process *H323Plugin::m_process = 0;
static H323Plugin hplugin;

int YateH323Connection::s_total = 0;

int H323MsgThread::s_count = 0;
int H323MsgThread::s_routed = 0;


bool H323MsgThread::route()
{
    Debug(DebugAll,"Routing thread for %s [%p]",m_id.c_str(),this);
    bool ok = Engine::dispatch(m_msg) && !m_msg->retValue().null();
    YateH323Connection *conn = hplugin.findConnectionLock(m_id);
    if (!conn) {
	Debug(DebugMild,"YateH323Connection '%s' vanished while routing!",m_id.c_str());
	return false;
    }
    if (ok) {
	conn->AnsweringCall(H323Connection::AnswerCallPending);
	*m_msg = "call.execute";
	m_msg->addParam("callto",m_msg->retValue());
	m_msg->retValue().clear();
	m_msg->userData(static_cast<DataEndpoint *>(conn));
	if (Engine::dispatch(m_msg)) {
	    Debug(DebugInfo,"Routing H.323 call %s [%p] to '%s'",m_id.c_str(),conn,m_msg->getValue("callto"));
	    conn->rtpExecuted(*m_msg);
	    conn->setStatus("routed");
	    conn->setTarget(m_msg->getValue("targetid"));
	    if (conn->getTarget().null()) {
		Debug(DebugInfo,"Answering now H.323 call %s [%p] because we have no targetid",m_id.c_str(),conn);
		conn->AnsweringCall(H323Connection::AnswerCallNow);
	    }
	    conn->deref();
	}
	else {
	    Debug(DebugInfo,"Rejecting unconnected H.323 call %s [%p]",m_id.c_str(),conn);
	    conn->setStatus("rejected");
	    conn->AnsweringCall(H323Connection::AnswerCallDenied);
	}
    }
    else {
	Debug(DebugInfo,"Rejecting unrouted H.323 call %s [%p]",m_id.c_str(),conn);
	conn->setStatus("rejected");
	conn->AnsweringCall(H323Connection::AnswerCallDenied);
    }
    conn->Unlock();
    return ok;
}

void H323MsgThread::run()
{
    s_route.lock();
    s_count++;
    s_route.unlock();
    Debug(DebugAll,"Started routing thread for %s [%p]",m_id.c_str(),this);
    bool ok = route();
    s_route.lock();
    s_count--;
    if (ok)
	s_routed++;
    s_route.unlock();
}

void H323MsgThread::cleanup()
{
    Debug(DebugAll,"Cleaning up routing thread for %s [%p]",m_id.c_str(),this);
    delete m_msg;
}

YateGatekeeperServer::YateGatekeeperServer(YateH323EndPoint & ep)
  : H323GatekeeperServer(ep),
    endpoint(ep)
{
    Debug(DebugAll,"YateGatekeeperServer::YateGatekeeperServer() [%p]",this);
}

BOOL YateGatekeeperServer::Init ()		    
{

  SetGatekeeperIdentifier("YATE gatekeeper");
  H323TransportAddressArray interfaces;
  const char *addr = 0;
  int i;
  for (i = 1; (addr = s_cfg.getValue("gk",("interface"+String(i)).c_str())); i++){
	if (!AddListener(new H323GatekeeperListener(endpoint, *this,s_cfg.getValue("gk","name","YateGatekeeper"),new H323TransportUDP(endpoint,PIPSocket::Address(addr),s_cfg.getIntValue("gk","port",1719),0))))
	  Debug(DebugGoOn,"I can't start the listener for address: %s",addr);
     }  
  return TRUE;	
}


YateH323EndPoint::YateH323EndPoint()
  : gkServer(0)
{
    Debug(DebugAll,"YateH323EndPoint::YateH323EndPoint() [%p]",this);
}

YateH323EndPoint::~YateH323EndPoint()
{
    Debug(DebugAll,"YateH323EndPoint::~YateH323EndPoint() [%p]",this);
    RemoveListener(0);
    ClearAllCalls(H323Connection::EndedByLocalUser, true);
    if (gkServer)
	delete gkServer;
}

H323Connection *YateH323EndPoint::CreateConnection(unsigned callReference,
    void *userData, H323Transport *transport, H323SignalPDU *setupPDU)
{
    if (Engine::exiting()) {
	Debug(DebugWarn,"Refusing new connection, engine is exiting");
	return 0;
    }
    if (s_maxconns > 0) {
	s_calls.lock();
	int cnt = hplugin.calls().count();
	s_calls.unlock();
	if (cnt >= s_maxconns) {
	    Debug(DebugWarn,"Dropping connection, there are already %d",cnt);
	    return 0;
	}
    }
    return new YateH323Connection(*this,callReference,userData);
}

#ifdef USE_CAPABILITY_FACTORY
static void ListRegisteredCaps(int level)
{
    PFactory<H323Capability>::KeyList_T list = PFactory<H323Capability>::GetKeyList();
    for (std::vector<PString>::const_iterator find = list.begin(); find != list.end(); ++find)
	Debug(level,"Registed capability: '%s'",(const char*)*find);
}
#else
// This class is used just to find out if a capability is registered
class FakeH323CapabilityRegistration : public H323CapabilityRegistration
{
    PCLASSINFO(FakeH323CapabilityRegistration,H323CapabilityRegistration);
public:
    FakeH323CapabilityRegistration()
	: H323CapabilityRegistration("[fake]")
	{ }
    static void ListRegistered(int level);
    static bool IsRegistered(const PString& name);
    virtual H323Capability* Create(H323EndPoint& ep) const
	{ return 0; }
};

void FakeH323CapabilityRegistration::ListRegistered(int level)
{
    PWaitAndSignal mutex(H323CapabilityRegistration::GetMutex());
    H323CapabilityRegistration* find = registeredCapabilitiesListHead;
    for (; find; find = static_cast<FakeH323CapabilityRegistration*>(find)->link)
	Debug(level,"Registed capability: '%s'",(const char*)*find);
}

bool FakeH323CapabilityRegistration::IsRegistered(const PString& name)
{
    PWaitAndSignal mutex(H323CapabilityRegistration::GetMutex());
    H323CapabilityRegistration* find = registeredCapabilitiesListHead;
    for (; find; find = static_cast<FakeH323CapabilityRegistration*>(find)->link)
	if (*find == name)
	    return true;
    return false;
}
#endif

bool YateH323EndPoint::Init(void)
{
    int dump = s_cfg.getIntValue("general","dumpcodecs");
    if (dump > 0)
#ifdef USE_CAPABILITY_FACTORY
	ListRegisteredCaps(dump);
#else
	FakeH323CapabilityRegistration::ListRegistered(dump);
#endif
    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
    const char** f = h323_formats;
    for (; *f; f += 2) {
	bool ok = false;
	bool fake = false;
	String tmp(s_cfg.getValue("codecs",f[1]));
	if ((tmp == "fake") || (tmp == "pretend")) {
	    ok = true;
	    fake = true;
	}
	else
	    ok = tmp.toBoolean(defcodecs);
	if (ok) {
	    tmp = f[0];
	    tmp += "*{sw}";
	    PINDEX init = GetCapabilities().GetSize();
	    AddAllCapabilities(0, 0, tmp.c_str());
	    PINDEX num = GetCapabilities().GetSize() - init;
	    if (fake && !num) {
		// failed to add so pretend we support it in hardware
		tmp = f[0];
		tmp += "*{hw}";
		AddAllCapabilities(0, 0, tmp.c_str());
		num = GetCapabilities().GetSize() - init;
	    }
	    if (num)
		Debug(DebugAll,"H.323 added %d capabilities '%s'",num,tmp.c_str());
	    else
		// warn if codecs were disabled by default
		Debug(defcodecs ? DebugInfo : DebugWarn,"H323 failed to add capability '%s'",tmp.c_str());
	}
    }

    AddAllUserInputCapabilities(0,1);
    DisableDetectInBandDTMF(!s_cfg.getBoolValue("ep","dtmfinband",false));
    DisableFastStart(!s_cfg.getBoolValue("ep","faststart",false));
    DisableH245Tunneling(!s_cfg.getBoolValue("ep","h245tunneling",false));
    SetSilenceDetectionMode(static_cast<H323AudioCodec::SilenceDetectionMode>
	(s_cfg.getIntValue("ep","silencedetect",dict_silence,H323AudioCodec::NoSilenceDetection)));

    PIPSocket::Address addr = INADDR_ANY;
    int port = s_cfg.getIntValue("ep","port",1720);
    if (s_cfg.getBoolValue("ep","ep",true)) {
	H323ListenerTCP *listener = new H323ListenerTCP(*this,addr,port);
	if (!(listener && StartListener(listener))) {
	    Debug(DebugGoOn,"Unable to start H323 Listener at port %d",port);
	    if (listener)
		delete listener;
	    return false;
	}
	const char *ali = s_cfg.getValue("ep","alias","yate");
	SetLocalUserName(ali);
	if (s_cfg.getBoolValue("ep","gkclient",false)){
	    const char *p = s_cfg.getValue("ep","password");
	    if (p) {
		SetGatekeeperPassword(p);
		Debug(DebugInfo,"Enabling H.235 security access to gatekeeper %s",p);
	    }
	    const char *d = s_cfg.getValue("ep","gkip");
	    const char *a = s_cfg.getValue("ep","gkname");
	    if (d) {
		PString gkName = d;
		H323TransportUDP * rasChannel  = new H323TransportUDP(*this);
		if (SetGatekeeper(gkName, rasChannel)) 
		    Debug(DebugInfo,"Connect to gatekeeper ip = %s",d);
		else {
		    Debug(DebugGoOn,"Unable to connect to gatekeeper ip = %s",d);
		    if (listener)
			listener->Close();
		}
	    } else if (a) {
		PString gkIdentifier = a;
		if (LocateGatekeeper(gkIdentifier)) 
		    Debug(DebugInfo,"Connect to gatekeeper name = %s",a);
		else {
		    Debug(DebugGoOn,"Unable to connect to gatekeeper name = %s",a);
		    if (listener)
			listener->Close();
		}
	    } else {
	        if (DiscoverGatekeeper(new H323TransportUDP(*this))) 
		    Debug(DebugInfo,"Find a gatekeeper");
		else {
		    Debug(DebugGoOn,"Unable to connect to any gatekeeper");
		    if (listener)
			listener->Close();
		    return false;
		}
	    }
	}
    }
    	    if (s_cfg.getBoolValue("gk","server",false))
	    {
		gkServer = new YateGatekeeperServer(*this);
	   	gkServer->Init();
	    }

//    bool useGk = s_cfg.getBoolean("general","use_gatekeeper");
    return true;
}

YateH323Connection::YateH323Connection(YateH323EndPoint &endpoint,
    unsigned callReference, void *userdata)
    : H323Connection(endpoint,callReference), DataEndpoint("h323"),
      m_nativeRtp(false), m_passtrough(false), m_rtpPort(0), m_remotePort(0)
{
    Debug(DebugAll,"YateH323Connection::YateH323Connection(%p,%u,%p) [%p]",
	&endpoint,callReference,userdata,this);
    m_id = "h323/";
    m_id << callReference;
    setStatus("new");
    s_calls.lock();
    hplugin.calls().append(this)->setDelete(false);
    s_calls.unlock();
    Message* m = new Message("chan.startup");
    m->addParam("id",m_id);
    m->addParam("direction",userdata ? "outgoing" : "incoming");
    m->addParam("status","new");
    Engine::enqueue(m);
    DataEndpoint *dd = static_cast<DataEndpoint *>(userdata);
    if (dd && connect(dd))
	deref();
}

YateH323Connection::~YateH323Connection()
{
    Debug(DebugAll,"YateH323Connection::~YateH323Connection() %s %s [%p]",
	m_status.c_str(),m_id.c_str(),this);
    setStatus("destroyed");
    s_calls.lock();
    hplugin.calls().remove(this,false);
    s_calls.unlock();
    CloseAllLogicalChannels(true);
    CloseAllLogicalChannels(false);
}

H323Connection::AnswerCallResponse YateH323Connection::OnAnswerCall(const PString &caller,
    const H323SignalPDU &setupPDU, H323SignalPDU &connectPDU)
{
    Debug(DebugInfo,"YateH323Connection::OnAnswerCall caller='%s' in %s [%p]",
	(const char *)caller,m_id.c_str(),this);
    setStatus("incoming");

    if (Engine::exiting()) {
	Debug(DebugWarn,"Dropping call, engine is exiting");
	setStatus("dropped");
	return H323Connection::AnswerCallDenied;
    }
    int cnt = H323MsgThread::count();
    if (cnt > s_maxqueue) {
	Debug(DebugWarn,"Dropping call, there are already %d waiting",cnt);
	setStatus("dropped");
	return H323Connection::AnswerCallDenied;
    }

    Message *m = new Message("call.route");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    const char *s = s_cfg.getValue("incoming","context");
    if (s)
	m->addParam("context",s);

    m->addParam("callername",caller);
    s = GetRemotePartyNumber();
    Debug(DebugInfo,"GetRemotePartyNumber()='%s'",s);
    m->addParam("caller",s ? s : (const char *)("h323/"+caller));

    const H225_Setup_UUIE &setup = setupPDU.m_h323_uu_pdu.m_h323_message_body;
    const H225_ArrayOf_AliasAddress &adr = setup.m_destinationAddress;
    for (int i = 0; i<adr.GetSize(); i++)
	Debug(DebugAll,"adr[%d]='%s'",i,(const char *)H323GetAliasAddressString(adr[i]));
    String called;
    if (adr.GetSize() > 0)
	called = (const char *)H323GetAliasAddressString(adr[0]);
    if (called.null())
	called = s_cfg.getValue("incoming","called");
    if (!called.null()) {
	Debug(DebugInfo,"Called number is '%s'",called.c_str());
	m->addParam("called",called);
    }
    else
	Debug(DebugWarn,"No called number present!");
#if 0
    s = GetRemotePartyAddress();
    Debug(DebugInfo,"GetRemotePartyAddress()='%s'",s);
    if (s)
	m->addParam("calledname",s);
#endif
    if (s_passtrough && m_remotePort) {
	m_passtrough = true;
	m->addParam("rtp_forward","possible");
	m->addParam("rtp_addr",m_remoteAddr);
	m->addParam("rtp_port",String(m_remotePort));
	m->addParam("formats",m_remoteFormats);
    }
    H323MsgThread *t = new H323MsgThread(m,id());
    if (!t->startup()) {
	Debug(DebugWarn,"Error starting routing thread! [%p]",this);
	delete t;
	setStatus("dropped");
	return H323Connection::AnswerCallDenied;
    }
    return H323Connection::AnswerCallDeferred;
}

void YateH323Connection::rtpExecuted(Message& msg)
{
    Debug(DebugAll,"YateH323Connection::rtpExecuted(%p) [%p]",
	&msg,this);
    if (!m_passtrough)
	return;
    String tmp = msg.getValue("rtp_forward");
    m_passtrough = (tmp == "accepted");
    if (m_passtrough)
	Debug(DebugInfo,"H323 Peer accepted RTP forward");
}

void YateH323Connection::rtpForward(Message& msg, bool init)
{
    Debug(DebugAll,"YateH323Connection::rtpForward(%p,%d) [%p]",
	&msg,init,this);
    String tmp = msg.getValue("rtp_forward");
    if (!(init || m_passtrough && tmp))
	return;
    m_passtrough = tmp.toBoolean();
    if (!m_passtrough)
	return;
    tmp = msg.getValue("rtp_port");
    int port = tmp.toInteger();
    String addr(msg.getValue("rtp_addr"));
    if (port && addr) {
	m_rtpAddr = addr;
	m_rtpPort = port;
	m_formats = msg.getValue("formats");
	msg.setParam("rtp_forward","accepted");
	Debug(DebugInfo,"H323 Accepted RTP forward %s:%d formats '%s'",
	    addr.c_str(),port,m_formats.safe());
    }
    else
	m_passtrough = false;
}

void YateH323Connection::OnEstablished()
{
    Debug(DebugInfo,"YateH323Connection::OnEstablished() [%p]",this);
    s_calls.lock();
    s_total++;
    setStatus("connected");
    s_calls.unlock();
    if (HadAnsweredCall())
	return;
    Message *m = new Message("call.answered");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    if (m_targetid)
	m->addParam("targetid",m_targetid);
    m->addParam("status","answered");
    if (m_passtrough) {
	if (m_remotePort) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_remoteAddr);
	    m->addParam("rtp_port",String(m_remotePort));
	    m->addParam("formats",m_remoteFormats);
	}
	else
	    Debug(DebugWarn,"H323 RTP passtrough with no remote address! [%p]",this);
    }
    Engine::enqueue(m);
}

void YateH323Connection::OnCleared()
{
    int reason = GetCallEndReason();
    const char* rtext = CallEndReasonText(reason);
    Debug(DebugInfo,"YateH323Connection::OnCleared() reason: %s (%d) [%p]",
	rtext,reason,this);
    setStatus("cleared");
    Message *m = new Message("chan.hangup");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    if (m_targetid)
	m->addParam("targetid",m_targetid);
    m->addParam("reason",rtext);
    Engine::enqueue(m);
    disconnect(rtext);
}

BOOL YateH323Connection::OnAlerting(const H323SignalPDU &alertingPDU, const PString &user)
{
    Debug(DebugInfo,"YateH323Connection::OnAlerting '%s' [%p]",(const char *)user,this);
    setStatus("ringing");
    Message *m = new Message("call.ringing");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    if (m_targetid)
	m->addParam("targetid",m_targetid);
    m->addParam("status","ringing");
    if (m_passtrough && m_remotePort) {
	m->addParam("rtp_forward","yes");
	m->addParam("rtp_addr",m_remoteAddr);
	m->addParam("rtp_port",String(m_remotePort));
	m->addParam("formats",m_remoteFormats);
    }
    Engine::enqueue(m);
    return true;
}

void YateH323Connection::OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
    Debug(DebugInfo,"YateH323Connection::OnUserInputTone '%c' duration=%u [%p]",tone,duration,this);
    char buf[2];
    buf[0] = tone;
    buf[1] = 0;
    Message *m = new Message("chan.dtmf");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    if (m_targetid)
	m->addParam("targetid",m_targetid);
    m->addParam("text",buf);
    m->addParam("duration",String(duration));
    Engine::enqueue(m);
}

void YateH323Connection::OnUserInputString(const PString &value)
{
    Debug(DebugInfo,"YateH323Connection::OnUserInputString '%s' [%p]",(const char *)value,this);
    String text((const char *)value);
    const char *type = text.startSkip("MSG") ? "chan.text" : "chan.dtmf";
    Message *m = new Message(type);
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    if (m_targetid)
	m->addParam("targetid",m_targetid);
    m->addParam("text",text);
    Engine::enqueue(m);
}

BOOL YateH323Connection::OpenAudioChannel(BOOL isEncoding, unsigned bufferSize,
    H323AudioCodec &codec)
{
    Debug(DebugInfo,"YateH323Connection::OpenAudioChannel [%p]",this);
    if (!m_nativeRtp) {
	Debug(DebugGoOn,"YateH323Connection::OpenAudioChannel for external RTP in [%p]",this);
	return false;
    }

    if (isEncoding) {
	if (!getConsumer())
	{
	    setConsumer(new YateH323AudioConsumer);
	    getConsumer()->deref();
	}
	// data going TO h.323
	if (getConsumer())
	    return codec.AttachChannel(static_cast<YateH323AudioConsumer *>(getConsumer()),false);
    }
    else {
	if(!getSource())
	{
            setSource(new YateH323AudioSource);
	    getSource()->deref();
	}
	// data coming FROM h.323
	if (getSource())
	    return codec.AttachChannel(static_cast<YateH323AudioSource *>(getSource()),false);
    }
    return false;
}

void YateH323Connection::disconnected(bool final, const char *reason)
{
    Debugger debug("YateH323Connection::disconnected()"," '%s' [%p]",reason,this);
    setStatus("disconnected");
    setTarget();
    // we must bypass the normal Yate refcounted destruction as OpenH323 will destroy the object
    ref();
    if (getSource() && m_nativeRtp)
	static_cast<YateH323AudioSource *>(getSource())->Close();
    if (getConsumer() && m_nativeRtp)
	static_cast<YateH323AudioConsumer *>(getConsumer())->Close();
    ClearCall();
}

#ifdef NEED_RTP_QOS_PARAM
H323Channel *YateH323Connection::CreateRealTimeLogicalChannel(const H323Capability & capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters *param,RTP_QOS * rtpqos)
#else
H323Channel *YateH323Connection::CreateRealTimeLogicalChannel(const H323Capability & capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters *param)
#endif
{
    Debug(DebugAll,"H323Connection::CreateRealTimeLogicalChannel [%p]",this);
    if (s_externalRtp || s_passtrough) {
	const char* sdir = lookup(dir,dict_h323_dir);
	const char *format = 0;
	decodeCapability(capability,&format);
	Debug(DebugAll,"capability '%s' format '%s' session %u %s",
	    (const char *)capability.GetFormatName(),format,sessionID,sdir);

	// disallow codecs not supported by remote receiver
	if (s_passtrough && !(m_formats.null() || (m_formats.find(format) >= 0))) {
	    Debug(DebugWarn,"Refusing '%s' not in remote '%s'",format,m_formats.c_str());
	    return 0;
	}

	if (s_passtrough && (dir == H323Channel::IsReceiver)) {
	    if (format && (m_remoteFormats.find(format) < 0) && s_cfg.getBoolValue("codecs",format,true)) {
		if (m_remoteFormats)
		    m_remoteFormats << ",";
		m_remoteFormats << format;
	    }
	}
	PIPSocket::Address externalIpAddress;
	GetControlChannel().GetLocalAddress().GetIpAddress(externalIpAddress);
	Debug(DebugInfo,"address '%s'",(const char *)externalIpAddress.AsString());
	WORD externalPort = 0;
	if (s_externalRtp) {
	    Message m("chan.rtp");
	    m.addParam("localip",externalIpAddress.AsString());
	    m.userData(static_cast<DataEndpoint *>(this));
	    // the cast above is required because of the multiple inheritance
	    if (sdir)
		m.addParam("direction",sdir);
	    if (Engine::dispatch(m)) {
		String p(m.getValue("localport"));
		externalPort = p.toInteger();
	    }
	}
	if (externalPort || s_passtrough) {
	    m_nativeRtp = false;
	    if (!externalPort) {
		externalPort = m_rtpPort;
		externalIpAddress = PString(m_rtpAddr.safe());
	    }
	    return new YateH323_ExternalRTPChannel(*this, capability, dir, sessionID, externalIpAddress, externalPort);
	}
	Debug(DebugWarn,"YateH323Connection failed to create external RTP, using native");
    }

    m_nativeRtp = true;
#ifdef NEED_RTP_QOS_PARAM
    return H323Connection::CreateRealTimeLogicalChannel(capability,dir,sessionID,param,rtpqos);
#else
    return H323Connection::CreateRealTimeLogicalChannel(capability,dir,sessionID,param);
#endif
}

void YateH323Connection::OnSetLocalCapabilities()
{
    Debug(DebugAll,"YateH323Connection::OnSetLocalCapabilities() [%p]",this);
    H323Connection::OnSetLocalCapabilities();
    if (m_formats.null())
	return;
    // remote has a list of supported codecs - remove unsupported capabilities
    bool nocodecs = true;
    for (int i = 0; i < localCapabilities.GetSize(); i++) {
	const char* format = 0;
	String fname;
	decodeCapability(localCapabilities[i],&format,0,&fname);
	if (format) {
	    if (m_formats.find(format) < 0) {
		Debug(DebugAll,"Removing capability '%s' (%s) not in remote '%s'",
		    fname.c_str(),format,m_formats.c_str());
		localCapabilities.Remove(fname.c_str());
		i--;
	    }
	    else
		nocodecs = false;
	}
    }
    if (nocodecs)
	Debug(DebugWarn,"No codecs remaining for H323 connection [%p]",this);
}

BOOL YateH323Connection::OnStartLogicalChannel(H323Channel & channel) 
{
    Debug(DebugInfo,"YateH323Connection::OnStartLogicalChannel(%p) [%p]",&channel,this);
    return m_nativeRtp ? H323Connection::OnStartLogicalChannel(channel) : TRUE;
}

BOOL YateH323Connection::OnCreateLogicalChannel(const H323Capability & capability, H323Channel::Directions dir, unsigned & errorCode ) 
{
    Debug(DebugInfo,"YateH323Connection::OnCreateLogicalChannel('%s',%s) [%p]",(const char *)capability.GetFormatName(),lookup(dir,dict_h323_dir),this);
    return H323Connection::OnCreateLogicalChannel(capability,dir,errorCode);
}

BOOL YateH323Connection::decodeCapability(const H323Capability & capability, const char** dataFormat, int *payload, String* capabName)
{
    String fname((const char *)capability.GetFormatName());
    // turn capability name into format name
    if (fname.endsWith("{sw}",false))
	fname = fname.substr(0,fname.length()-4);
    if (fname.endsWith("{hw}",false))
	fname = fname.substr(0,fname.length()-4);
    OpalMediaFormat oformat(fname, FALSE);
    int pload = oformat.GetPayloadType();
    const char *format = 0;
    const char** f = h323_formats;
    for (; *f; f += 2) {
	if (fname.startsWith(*f,false)) {
	    format = f[1];
	    break;
	}
    }
    DDebug(DebugAll,"capability '%s' format '%s' payload %d",fname.c_str(),format,pload);
    if (format) {
	if (capabName)
	    *capabName = fname;
	if (dataFormat)
	    *dataFormat = format;
	if (payload)
	    *payload = pload;
	return TRUE;
    }
    return FALSE;
}

void YateH323Connection::SetRemoteAddress(const char* remoteIP, WORD remotePort)
{
    if (!m_remotePort) {
	Debug(DebugInfo,"Copying remote RTP address [%p]",this);
	m_remotePort = remotePort;
	m_remoteAddr = remoteIP;
    }
}

BOOL YateH323Connection::StartExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan)
{
    const char* sdir = lookup(dir,dict_h323_dir);
    Debug(DebugAll,"YateH323Connection::StartExternalRTP(\"%s\",%u,%s,%p) [%p]",
	remoteIP,remotePort,sdir,chan,this);
    if (m_passtrough && m_rtpPort) {
	SetRemoteAddress(remoteIP,remotePort);

	Debug(DebugInfo,"Passing RTP to %s:%d",m_rtpAddr.c_str(),m_rtpPort);
	const PIPSocket::Address ip(m_rtpAddr.safe());
	WORD dataPort = m_rtpPort;
	chan->SetExternalAddress(H323TransportAddress(ip, dataPort), H323TransportAddress(ip, dataPort+1));
	OnStoppedExternal(dir);
	return TRUE;
    }
    if (!s_externalRtp)
	return FALSE;
    Message m("chan.rtp");
    m.userData(static_cast<DataEndpoint *>(this));
    // the cast above is required because of the multiple inheritance
//    Debug(DebugAll,"userData=%p this=%p",m.userData(),this);
    if (sdir)
	m.addParam("direction",sdir);
    m.addParam("remoteip",remoteIP);
    m.addParam("remoteport",String(remotePort));
    int payload = 128;
    const char *format = 0;
    decodeCapability(chan->GetCapability(),&format,&payload);
    if (format)
	m.addParam("format",format);
    if ((payload >= 0) && (payload < 127))
	m.addParam("payload",String(payload));
    if (Engine::dispatch(m)) {
	return TRUE;
    }
    return FALSE;
}

void YateH323Connection::OnStoppedExternal(H323Channel::Directions dir)
{
    Debug(DebugInfo,"YateH323Connection::OnStoppedExternal(%s) [%p]",lookup(dir,dict_h323_dir),this);
    switch (dir) {
	case H323Channel::IsReceiver:
	    setSource();
	    break;
	case H323Channel::IsTransmitter:
	    setConsumer();
	    break;
	case H323Channel::IsBidirectional:
	    setSource();
	    setConsumer();
	default:
	    break;
    }
}

YateH323_ExternalRTPChannel::YateH323_ExternalRTPChannel( 
	YateH323Connection & connection,
	const H323Capability & capability,
	Directions direction, 
	unsigned sessionID, 
	const PIPSocket::Address & ip, 
	WORD dataPort)
	: H323_ExternalRTPChannel(connection, capability, direction, sessionID, ip, dataPort),m_conn(&connection)
{ 
    Debug(DebugAll,"YateH323_ExternalRTPChannel::YateH323_ExternalRTPChannel %s addr=%s:%u [%p]",
	lookup(GetDirection(),dict_h323_dir), (const char *)ip.AsString(), dataPort,this);
    SetExternalAddress(H323TransportAddress(ip, dataPort), H323TransportAddress(ip, dataPort+1));
}

YateH323_ExternalRTPChannel::~YateH323_ExternalRTPChannel()
{
    Debug(DebugInfo,"YateH323_ExternalRTPChannel::~YateH323_ExternalRTPChannel [%p]",this);
    if (isRunning) {
	isRunning = FALSE;
	if (m_conn)
	    m_conn->OnStoppedExternal(GetDirection());
    }
}

BOOL YateH323_ExternalRTPChannel::Start()
{
    Debug(DebugAll,"YateH323_ExternalRTPChannel::Start() [%p]",this);
    if (!m_conn)
	return FALSE;

    PIPSocket::Address remoteIpAddress;
    WORD remotePort;
    GetRemoteAddress(remoteIpAddress,remotePort);
    Debug(DebugInfo,"external rtp ip address %s:%u",(const char *)remoteIpAddress.AsString(),remotePort);

    return isRunning = m_conn->StartExternalRTP((const char *)remoteIpAddress.AsString(), remotePort, GetDirection(), this);
}

BOOL YateH323_ExternalRTPChannel::OnReceivedPDU(
				const H245_H2250LogicalChannelParameters & param,
				unsigned & errorCode)
{
    Debug(DebugInfo,"OnReceivedPDU [%p]",this);
    if (!H323_ExternalRTPChannel::OnReceivedPDU(param,errorCode))
	return FALSE;
    if (!m_conn || m_conn->HasRemoteAddress())
	return TRUE;
    PIPSocket::Address remoteIpAddress;
    WORD remotePort;
    GetRemoteAddress(remoteIpAddress,remotePort);
    Debug(DebugInfo,"external rtp ip address %s:%u",(const char *)remoteIpAddress.AsString(),remotePort);
    m_conn->SetRemoteAddress((const char *)remoteIpAddress.AsString(), remotePort);
    return TRUE;
}

BOOL YateH323_ExternalRTPChannel::OnSendingPDU( H245_H2250LogicalChannelParameters & param )
{
    Debug(DebugInfo,"OnSendingPDU [%p]",this);
    return H323_ExternalRTPChannel::OnSendingPDU(param);
}

BOOL YateH323_ExternalRTPChannel::OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters & param)
{

    Debug(DebugInfo,"OnReceivedAckPDU [%p]",this);
    return H323_ExternalRTPChannel::OnReceivedAckPDU(param);
}

YateH323AudioSource::~YateH323AudioSource()
{
    Debug(DebugAll,"h.323 source [%p] deleted",this);
    m_exit = true;
    // Delay actual destruction until the mutex is released
    m_mutex.lock();
    m_data.clear(false);
    m_mutex.unlock();
}

YateH323AudioConsumer::~YateH323AudioConsumer()
{
    Debug(DebugAll,"h.323 consumer [%p] deleted",this);
    m_exit = true;
    // Delay actual destruction until the mutex is released
    m_mutex.check();
}

BOOL YateH323AudioConsumer::Close()
{
    Debug(DebugAll,"h.323 consumer [%p] closed",this);
    m_exit = true;
    return true;
}

BOOL YateH323AudioConsumer::IsOpen() const
{
    return !m_exit;
}

void YateH323AudioConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    if (m_exit)
	return;
    Lock lock(m_mutex);
    if ((m_buffer.length() + data.length()) <= (480*5))
	m_buffer += data;
#ifdef DEBUG
    else
	Debug("YateH323AudioConsumer",DebugAll,"Skipped %u bytes, buffer is full [%p]",data.length(),this);
#endif
    m_timestamp += timeDelta;
}

BOOL YateH323AudioConsumer::Read(void *buf, PINDEX len)
{
    while (!m_exit) {
	Lock lock(m_mutex);
	if (!getConnSource()) {
	    ::memset(buf,0,len);
	    readDelay.Delay(len/16);
	    break;
	}
	if (len >= (int)m_buffer.length()) {
	    lock.drop();
	    Thread::yield();
	    if (m_exit || Engine::exiting())
		return false;
	    continue;
	}
	if (len > 0) {
	    ::memcpy(buf,m_buffer.data(),len);
	    m_buffer.assign(len+(char *)m_buffer.data(),m_buffer.length()-len);
	    XDebug("YateH323AudioConsumer",DebugAll,"Pulled %d bytes from buffer [%p]",len,this);
	    break;
	}
	else
	    len = 0;
    }
    lastReadCount = len;
    return (len != 0);
}

BOOL YateH323AudioSource::Close()
{
    Debug(DebugAll,"h.323 source [%p] closed",this);
    m_exit = true;
    return true;
}

BOOL YateH323AudioSource::IsOpen() const
{
    return !m_exit;
}

BOOL YateH323AudioSource::Write(const void *buf, PINDEX len)
{
    if (!m_exit) {
	m_data.assign((void *)buf,len,false);
	Forward(m_data,len/2);
	m_data.clear(false);
	writeDelay.Delay(len/16);
    }
    lastWriteCount = len;
    return true;
}


BOOL YateGatekeeperServer::GetUsersPassword(const PString & alias,PString & password) const
{
    Message *m = new Message("user.auth");
    m->addParam("username",alias);
    Engine::dispatch(m);
    if (m->retValue() != NULL)
    {
	password = m->retValue();
	return true;
    } else
    {
	return false;
    }
}

H323GatekeeperCall * YateGatekeeperServer::CreateCall(const OpalGloballyUniqueID & id,
						H323GatekeeperCall::Direction dir)
{
	  return new YateGatekeeperCall(*this, id, dir);
}

H323GatekeeperRequest::Response YateGatekeeperServer::OnRegistration(H323GatekeeperRRQ & request)
{
    int i = H323GatekeeperServer::OnRegistration(request);
    if (i == H323GatekeeperRequest::Confirm)
    {	
	PString alias,r;
	String ips;
        for (int j = 0; j < request.rrq.m_terminalAlias.GetSize(); j++) {
	    alias = H323GetAliasAddressString(request.rrq.m_terminalAlias[j]);
	    r = H323GetAliasAddressE164(request.rrq.m_terminalAlias[j]);
	    H225_TransportAddress_ipAddress ip;
	    if (request.rrq.m_callSignalAddress.GetSize() >0)
		ip=request.rrq.m_callSignalAddress[0];
	    ips = "h323/" + String(ip.m_ip[0]) + "." + String(ip.m_ip[1]) + "." + String(ip.m_ip[2]) + "."  + String(ip.m_ip[3]) + ":" + String((int)ip.m_port);
	    /** 
	    * we deal just with the first callSignalAddress, since openh323 
	    * don't give a shit for multi hosted boxes.
	    */
	    Message *m = new Message("user.register");
	    m->addParam("username",alias);
	    m->addParam("techno","h323gk");
	    m->addParam("data",ips);
	    if (!Engine::dispatch(m) && !m->retValue().null())
		return H323GatekeeperRequest::Reject;
	}
    } else 
	return (H323Transaction::Response)i;
    return H323GatekeeperRequest::Confirm;
}
H323GatekeeperRequest::Response YateGatekeeperServer::OnUnregistration(H323GatekeeperURQ & request )
{
    /* We use just the first alias since is the one we need */
    int i = H323GatekeeperServer::OnUnregistration(request);
    if (i == H323GatekeeperRequest::Confirm)
    {
	for (int j = 0; j < request.urq.m_endpointAlias.GetSize(); j++) {
	    PString alias = H323GetAliasAddressString(request.urq.m_endpointAlias[j]);
	    if (alias.IsEmpty())
		return H323GatekeeperRequest::Reject;
	    Message *m = new Message("user.unregister");
	    m->addParam("username",alias);
	    Engine::dispatch(m);
	}
    } else 
	return (H323Transaction::Response)i;
    return H323GatekeeperRequest::Confirm;
}

BOOL YateGatekeeperServer::TranslateAliasAddressToSignalAddress(const H225_AliasAddress & alias,H323TransportAddress & address)
{
    PString aliasString = H323GetAliasAddressString(alias);
    Message m("call.route");
    m.addParam("called",aliasString);
    Engine::dispatch(m);
    String s = m.retValue();
    if (!s.null()){
	/** 
	 * Here we have 2 cases, first is handle when the call have to be send
	 * to endpoint (if the call is to another yate channel, or is h323 
	 * proxied), or if has to be send to another gatekeeper we find out 
	 * from the techno parameter
	 */
	    if ((m.getParam("techno")) && (*(m.getParam("techno")) == "h323gk"))
		address = s.c_str();
	    else {
		s.clear();
		s << "ip$" << s_cfg.getValue("gk","interface1") << ":" << s_cfg.getIntValue("ep","port",1720);
		address = s.c_str();
	    }
        return TRUE;
    }
    return FALSE;
}


YateGatekeeperCall::YateGatekeeperCall(YateGatekeeperServer & gk,
                                   const OpalGloballyUniqueID & id,
                                   Direction dir)
  : H323GatekeeperCall(gk, id, dir)
{
}

H323GatekeeperRequest::Response YateGatekeeperCall::OnAdmission(H323GatekeeperARQ & info)
{
/*    for (int i = 0; i < info.arq.m_srcInfo.GetSize(); i++) {
	PString alias = H323GetAliasAddressString(info.arq.m_srcInfo[i]);
	PString d = H323GetAliasAddressString(info.arq.m_destinationInfo[0]);
        Debug(DebugInfo,"aliasul in m_srcInfo %s si m_destinationInfo %s",(const char *)alias,(const char *)d);
	
    }

    return H323GatekeeperCall::OnAdmission(info);*/
	
#ifdef TEST_TOKEN
  info.acf.IncludeOptionalField(H225_AdmissionConfirm::e_tokens);
  info.acf.m_tokens.SetSize(1);
  info.acf.m_tokens[0].m_tokenOID = "1.2.36.76840296.1";
  info.acf.m_tokens[0].IncludeOptionalField(H235_ClearToken::e_nonStandard);
  info.acf.m_tokens[0].m_nonStandard.m_nonStandardIdentifier = "1.2.36.76840296.1.1";
  info.acf.m_tokens[0].m_nonStandard.m_data = "SnfYt0jUuZ4lVQv8umRYaH2JltXDRW6IuYcnASVU";
#endif

#ifdef TEST_SLOW_ARQ
  if (info.IsFastResponseRequired()) {
    if (YateH323GatekeeperCall::OnAdmission(info) == H323GatekeeperRequest::Reject)
      return H323GatekeeperRequest::Reject;

    return H323GatekeeperRequest::InProgress(5000); // 5 seconds maximum
  }

  PTimeInterval delay = 500+PRandom::Number()%3500; // Take from 0.5 to 4 seconds
  PTRACE(3, "RAS\tTest ARQ delay " << delay);
  PThread::Sleep(delay);
  return H323GatekeeperRequest::Confirm;
#else
  return H323GatekeeperCall::OnAdmission(info);
#endif
}

bool H323Handler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^h323/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (!msg.userData()) {
	Debug(DebugWarn,"H.323 call found but no data channel!");
	return false;
    }
    Debug(DebugInfo,"Found call to H.323 target='%s'",
	dest.matchString(1).c_str());
    PString p;
    YateH323Connection* conn = static_cast<YateH323Connection*>(
	hplugin.ep()->MakeCallLocked(dest.matchString(1).c_str(),p,msg.userData())
    );
    if (conn) {
	String caller(msg.getValue("caller"));
	if (caller.null())
	    caller = msg.getValue("callername");
	else
	    caller << " [" << s_cfg.getValue("ep","ident","yate") << "]";
	if (!caller.null()) {
	    Debug(DebugInfo,"Setting H.323 caller name to '%s'",caller.c_str());
	    conn->SetLocalPartyName(caller.c_str());
	}
	conn->rtpForward(msg,s_passtrough);
	conn->setTarget(msg.getValue("id"));
	msg.addParam("targetid",conn->id());
	conn->Unlock();
	return true;
    }
    return false;
};
		    
bool H323Dropper::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (id.null()) {
	Debug("H323Dropper",DebugInfo,"Dropping all calls");
	Lock lock(s_calls);
	ObjList *l = &hplugin.calls();
	for (; l; l=l->next()) {
	    YateH323Connection *c = static_cast<YateH323Connection *>(l->get());
	    if (c)
		c->ClearCall(H323Connection::EndedByGatekeeper);
	}
	return false;
    }
    if (!id.startsWith("h323"))
	return false;
    YateH323Connection *conn = hplugin.findConnectionLock(id);
    if (conn) {
	Debug("H323Dropper",DebugInfo,"Dropping call '%s' [%p]",conn->id().c_str(),conn);
	conn->ClearCall(H323Connection::EndedByGatekeeper);
	conn->Unlock();
	return true;
    }
    Debug("H323Dropper",DebugInfo,"Could not find call '%s'",id.c_str());
    return false;
};

bool H323ConnHandler::received(Message &msg, int id)
{
    String callid(msg.getValue("targetid"));
    if (!callid.startsWith("h323/",false))
	return false;
    YateH323Connection *conn = hplugin.findConnectionLock(callid);
    if (!conn) {
	Debug(DebugInfo,"Target '%s' was not found in list",callid.c_str());
	return false;
    }
    String text(msg.getValue("text"));
    switch (id) {
        case Answered:
	    conn->rtpForward(msg);
	    conn->AnsweringCall(H323Connection::AnswerCallNow);
	    break;
        case Ringing:
	    conn->rtpForward(msg);
	    conn->AnsweringCall(H323Connection::AnswerCallAlertWithMedia);
	    break;
	case DTMF:
	    Debug("H323",DebugInfo,"DTMF '%s' for %s [%p]",text.c_str(),conn->id().c_str(),conn);
	    for (unsigned int i = 0; i < text.length(); i++)
		conn->SendUserInputTone(text[i]);
	    break;
	case Text:
	    Debug("H323",DebugInfo,"Text '%s' for %s [%p]",text.c_str(),conn->id().c_str(),conn);
	    conn->SendUserInputIndicationString(text.safe());
	    break;
    }
    conn->Unlock();
    return true;
}

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"h323chan") && ::strcmp(sel,"varchans"))
	return false;
    String st("name=h323chan,type=varchans,format=Status|Address");
    Lock lock(s_calls);
    st << ";routed=" << H323MsgThread::routed() << ",routers=" << H323MsgThread::count();
    st << ",total=" << YateH323Connection::total() << ",chans=" << hplugin.calls().count() << ";";
    ObjList *l = &hplugin.calls();
    bool first = true;
    for (; l; l=l->next()) {
	YateH323Connection *c = static_cast<YateH323Connection *>(l->get());
	if (c) {
	    if (first)
		first = false;
	    else
		st << ",";
	    // HACK: we assume transport$address/callref format
	    String s((const char *)c->GetCallToken());
	    st << c->id() << "=" << c->status() << "|" << s.substr(0,s.rfind('/'));
	}
    }
    msg.retValue() << st << "\n";
    return false;
}

bool H323Stopper::received(Message &msg)
{
    hplugin.cleanup();
    return false;
};

H323Plugin::H323Plugin()
    : m_first(true), m_endpoint(0)
{
    Output("Loaded module H.323");
}

void H323Plugin::cleanup()
{
    if (m_endpoint) {
	delete m_endpoint;
	m_endpoint = 0;
	PSyncPoint terminationSync;
	terminationSync.Signal();
	Output("Waiting for OpenH323 to die");
	terminationSync.Wait();
    }
    m_calls.clear();
}

H323Plugin::~H323Plugin()
{
    cleanup();
    if (m_process) {
	delete m_process;
	m_process = 0;
    }
}

YateH323Connection *H323Plugin::findConnectionLock(const String& id)
{
    s_calls.lock();
    ObjList *l = &m_calls;
    while (l) {
	YateH323Connection *c = static_cast<YateH323Connection *>(l->get());
	l=l->next();
	if (c && (c->id() == id)) {
	    int res = c->TryLock();
	    if (res > 0) {
		s_calls.unlock();
		return c;
	    }
	    else if (res < 0) {
		// Connection locked - yield and try scanning the list again
		s_calls.unlock();
		Thread::yield();
		s_calls.lock();
		l = &m_calls;
	    }
	    else {
		// Connection shutting down - we can't lock it anymore
		s_calls.unlock();
		return 0;
	    }
	}
    }
    s_calls.unlock();
    return 0;
}

bool H323Plugin::isBusy() const
{
    return (m_calls.count() != 0);
}

void H323Plugin::initialize()
{
    Output("Initializing module H.323 - based on OpenH323-" OPENH323_VERSION);
    s_cfg = Engine::configFile("h323chan");
    s_cfg.load();
    if (!m_process){
	m_process = new H323Process;
    }
    s_maxqueue = s_cfg.getIntValue("incoming","maxqueue",5);
    s_maxconns = s_cfg.getIntValue("ep","maxconns",0);
    int dbg=s_cfg.getIntValue("general","debug");
    if (dbg)
	PTrace::Initialise(dbg,0,PTrace::Blocks | PTrace::Timestamp
	    | PTrace::Thread | PTrace::FileAndLine);
    if (!m_endpoint) {
	m_endpoint = new YateH323EndPoint;
	m_endpoint->Init();
    }
    s_externalRtp = s_cfg.getBoolValue("general","external_rtp",false);
    s_passtrough = s_cfg.getBoolValue("general","passtrough_rtp",false);
    if (m_first) {
	m_first = false;
	H323ConnHandler* ch = new H323ConnHandler;
	Engine::install(new MessageRelay("call.ringing",ch,H323ConnHandler::Ringing));
	Engine::install(new MessageRelay("call.answered",ch,H323ConnHandler::Answered));
	Engine::install(new MessageRelay("chan.dtmf",ch,H323ConnHandler::DTMF));
	Engine::install(new MessageRelay("chan.text",ch,H323ConnHandler::Text));
	Engine::install(new H323Handler("call.execute"));
	Engine::install(new H323Dropper("call.drop"));
	Engine::install(new H323Stopper("engine.halt"));
	Engine::install(new StatusHandler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
