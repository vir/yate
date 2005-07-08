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

#include <ptlib.h>
#include <h323.h>
#include <h323pdu.h>
#include <h323caps.h>
#include <ptclib/delaychan.h>
#include <gkserver.h>

/* For some reason the Windows version of OpenH323 #undefs the version.
 * You need to put a openh323version.h file somewhere in your include path,
 *  preferably in the OpenH323 include directory.
 * Make sure you keep that file in sync with your other OpenH323 components.
 * You can find a template for that below:

--- cut here ---
#ifndef OPENH323_MAJOR

#define OPENH323_MAJOR 1
#define OPENH323_MINOR 0
#define OPENH323_BUILD 0

#endif
--- cut here ---

 */

#ifdef _WINDOWS
#include <openh323version.h>
#endif

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

#include <yatephone.h>
#include <yateversn.h>

#include <string.h>

using namespace TelEngine;

static bool s_externalRtp;
static bool s_passtrough;

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
    "G.726-16k", "g726-16",
    "G.726-24k", "g726-24",
    "G.726-32k", "g726-32",
    "G.726-40k", "g726-40",
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

static TokenDict dict_errors[] = {
    { "noroute", H323Connection::EndedByUnreachable },
    { "noroute", H323Connection::EndedByNoUser },
    { "noconn", H323Connection::EndedByNoEndPoint },
    { "nomedia", H323Connection::EndedByCapabilityExchange },
    { "nomedia", H323Connection::EndedByNoBandwidth },
    { "busy", H323Connection::EndedByLocalBusy },
    { "busy", H323Connection::EndedByRemoteBusy },
    { "rejected", H323Connection::EndedByRefusal },
    { "rejected", H323Connection::EndedByNoAccept },
    { "forbidden", H323Connection::EndedBySecurityDenial },
    { "congestion", H323Connection::EndedByLocalCongestion },
    { "congestion", H323Connection::EndedByRemoteCongestion },
    { "offline", H323Connection::EndedByHostOffline },
    { "timeout", H323Connection::EndedByDurationLimit },
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

static bool isE164(const char* str)
{
    if (!(str && *str))
	return false;
    for (;;) {
	switch (*str++) {
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
	    case '*':
	    case '#':
		continue;
	    case 0:
		return true;
	    default:
		return false;
	}
    }
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

class YateGkRegThread;
class YateH323EndPoint;
class YateGatekeeperServer;

class H323Driver : public Driver
{
    friend class YateH323EndPoint;
public:
    H323Driver();
    virtual ~H323Driver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool received(Message &msg, int id);
    void cleanup();
    YateH323EndPoint* findEndpoint(const String& ep) const;
private:
    ObjList m_endpoints;
};

H323Process* s_process = 0;
static H323Driver hplugin;

class YateGatekeeperCall : public H323GatekeeperCall
{
    PCLASSINFO(YateGatekeeperCall, H323GatekeeperCall);
public:
    YateGatekeeperCall(
	YateGatekeeperServer& server,
	const OpalGloballyUniqueID& callIdentifier, /// Unique call identifier
	Direction direction
    );

    virtual H323GatekeeperRequest::Response OnAdmission(
	H323GatekeeperARQ& request
    );
};

class YateGatekeeperServer : public H323GatekeeperServer
{
    PCLASSINFO(YateGatekeeperServer, H323GatekeeperServer);
public:
    YateGatekeeperServer(YateH323EndPoint& ep);
    BOOL Init();
    H323GatekeeperRequest::Response OnRegistration(
	H323GatekeeperRRQ& request);
    H323GatekeeperRequest::Response OnUnregistration(
	H323GatekeeperURQ& request);
    H323GatekeeperCall* CreateCall(const OpalGloballyUniqueID& id,H323GatekeeperCall::Direction dir);
    BOOL TranslateAliasAddressToSignalAddress(const H225_AliasAddress& alias,H323TransportAddress& address);
    virtual BOOL GetUsersPassword(const PString& alias,PString& password) const;

private:
    YateH323EndPoint& endpoint;
};

class YateH323AudioSource : public DataSource, public PIndirectChannel
{
    PCLASSINFO(YateH323AudioSource, PIndirectChannel)
    YCLASS(YateH323AudioSource, DataSource)
public:
    YateH323AudioSource()
	: m_exit(false)
	{ Debug(&hplugin,DebugAll,"YateH323AudioSource::YateH323AudioSource() [%p]",this); }
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
    YCLASS(YateH323AudioConsumer, DataConsumer)
public:
    YateH323AudioConsumer()
	: m_exit(false)
	{ Debug(&hplugin,DebugAll,"YateH323AudioConsumer::YateH323AudioConsumer() [%p]",this); }
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

class YateH323EndPoint : public String, public H323EndPoint
{
    PCLASSINFO(YateH323EndPoint, H323EndPoint)
public:
    enum GkMode {
	ByAddr,
	ByName,
	Discover,
	Unregister
    };
    YateH323EndPoint(const NamedList* params = 0, const char* name = 0);
    ~YateH323EndPoint();
    virtual H323Connection* CreateConnection(unsigned callReference, void* userData,
	H323Transport* transport, H323SignalPDU* setupPDU);
    bool Init(const NamedList* params = 0);
    bool startGkClient(int mode, const char* name = "");
    void asyncGkClient(int mode, const PString& name);
protected:
    YateGatekeeperServer* m_gkServer;
    YateGkRegThread* m_thread;
};

class YateH323Connection :  public H323Connection
{
    PCLASSINFO(YateH323Connection, H323Connection)
    friend class YateH323Chan;
public:
    YateH323Connection(YateH323EndPoint& endpoint, H323Transport* transport, unsigned callReference, void* userdata);
    ~YateH323Connection();
    virtual H323Connection::AnswerCallResponse OnAnswerCall(const PString& caller,
	const H323SignalPDU& signalPDU, H323SignalPDU& connectPDU);
    virtual void OnEstablished();
    virtual void OnCleared();
    virtual BOOL OnAlerting(const H323SignalPDU& alertingPDU, const PString& user);
    virtual void OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp);
    virtual void OnUserInputString(const PString& value);
    virtual BOOL OpenAudioChannel(BOOL isEncoding, unsigned bufferSize,
	H323AudioCodec &codec);
    virtual void OnSetLocalCapabilities();
#ifdef NEED_RTP_QOS_PARAM
    virtual H323Channel* CreateRealTimeLogicalChannel(const H323Capability& capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters* param,RTP_QOS* rtpqos = NULL);
#else
    virtual H323Channel* CreateRealTimeLogicalChannel(const H323Capability& capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters* param);
#endif
    virtual BOOL OnStartLogicalChannel(H323Channel& channel);
    virtual BOOL OnCreateLogicalChannel(const H323Capability& capability, H323Channel::Directions dir, unsigned& errorCode ) ;
    BOOL startExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan);
    void stoppedExternal(H323Channel::Directions dir);
    void setRemoteAddress(const char* remoteIP, WORD remotePort);
    void cleanups();
    bool sendTone(Message& msg, const char* tone);
    void setCallerID(const char* number, const char* name);
    void rtpExecuted(Message& msg);
    void rtpForward(Message& msg, bool init = false);
    void answerCall(AnswerCallResponse response);
    static BOOL decodeCapability(const H323Capability& capability, const char** dataFormat, int* payload = 0, String* capabName = 0);
    inline bool hasRemoteAddress() const
	{ return m_passtrough && (m_remotePort > 0); }
private:
    YateH323Chan* m_chan;
    bool m_externalRtp;
    bool m_nativeRtp;
    bool m_passtrough;
    String m_formats;
    String m_rtpid;
    String m_rtpAddr;
    int m_rtpPort;
    String m_remoteFormats;
    String m_remoteAddr;
    int m_remotePort;
};

// this part has been inspired (more or less) from chan_h323 of project asterisk, credits to Jeremy McNamara for chan_h323 and to Mark Spencer for asterisk.
class YateH323_ExternalRTPChannel : public H323_ExternalRTPChannel
{
    PCLASSINFO(YateH323_ExternalRTPChannel, H323_ExternalRTPChannel);
public:
    /* Create a new channel. */
    YateH323_ExternalRTPChannel(
	YateH323Connection& connection,
	const H323Capability& capability,
	Directions direction,
	unsigned sessionID,
	const PIPSocket::Address& ip,
	WORD dataPort);
    /* Destructor */
    ~YateH323_ExternalRTPChannel();

    virtual BOOL Start();
    virtual BOOL OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters& param);
    virtual BOOL OnSendingPDU(H245_H2250LogicalChannelParameters& param);
    virtual BOOL OnReceivedPDU(const H245_H2250LogicalChannelParameters& param,unsigned& errorCode);
    virtual void OnSendOpenAck(H245_H2250LogicalChannelAckParameters& param);
private:
    YateH323Connection* m_conn;
};

class YateH323Chan :  public Channel
{
    friend class YateH323Connection;
public:
    YateH323Chan(YateH323Connection* conn,bool outgoing,const char* addr);
    ~YateH323Chan();
    BOOL OpenAudioChannel(BOOL isEncoding, H323AudioCodec &codec);

    virtual void disconnected(bool final, const char *reason);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callReject(const char* error, const char* reason);
    inline void setTarget(const char* targetid)
	{ m_targetid = targetid; }
private:
    YateH323Connection* m_conn;
};

class YateGkRegThread : public PThread
{
    PCLASSINFO(YateGkRegThread, PThread);
public:
    YateGkRegThread(YateH323EndPoint* endpoint, int mode, const char* name = "")
	: PThread(10000), m_ep(endpoint), m_mode(mode), m_name(name)
	{ }
    void Main()
	{ m_ep->asyncGkClient(m_mode,m_name); }
protected:
    YateH323EndPoint* m_ep;
    int m_mode;
    PString m_name;
};

class UserHandler : public MessageHandler
{
public:
    UserHandler()
	: MessageHandler("user.login",140)
	{ }
    virtual bool received(Message &msg);
};

// start of fake capabilities code

class BaseG7231Capab : public H323AudioCapability
{
    PCLASSINFO(BaseG7231Capab, H323AudioCapability);
public:
    BaseG7231Capab(const char* fname, bool annexA = true)
	: H323AudioCapability(7,4), m_name(fname), m_aa(annexA)
	{ }
    virtual PObject* Clone() const
	// default copy constructor - take care!
	{ return new BaseG7231Capab(*this); }
    virtual unsigned GetSubType() const
	{ return H245_AudioCapability::e_g7231; }
    virtual PString GetFormatName() const
	{ return m_name; }
    virtual H323Codec* CreateCodec(H323Codec::Direction direction) const
	{ return 0; }
    virtual Comparison Compare(const PObject& obj) const
	{
	    Comparison res = H323AudioCapability::Compare(obj);
	    if (res != EqualTo)
		return res;
	    bool aa = static_cast<const BaseG7231Capab&>(obj).m_aa;
	    if (aa && !m_aa)
		return LessThan;
	    if (m_aa && !aa)
		return GreaterThan;
	    return EqualTo;
	}
    virtual BOOL OnSendingPDU(H245_AudioCapability& pdu, unsigned packetSize) const
	{
	    pdu.SetTag(GetSubType());
	    H245_AudioCapability_g7231& g7231 = pdu;
	    g7231.m_maxAl_sduAudioFrames = packetSize;
	    g7231.m_silenceSuppression = m_aa;
	    return TRUE;
	}
    virtual BOOL OnReceivedPDU(const H245_AudioCapability& pdu, unsigned& packetSize)
	{
	    if (pdu.GetTag() != H245_AudioCapability::e_g7231)
		return FALSE;
	    const H245_AudioCapability_g7231& g7231 = pdu;
	    packetSize = g7231.m_maxAl_sduAudioFrames;
	    m_aa = (g7231.m_silenceSuppression != 0);
	    return TRUE;
	}
protected:
    const char* m_name;
    bool m_aa;
};

class BaseG729Capab : public H323AudioCapability
{
    PCLASSINFO(BaseG729Capab, H323AudioCapability);
public:
    BaseG729Capab(const char* fname, unsigned type = H245_AudioCapability::e_g729)
	: H323AudioCapability(24,6), m_name(fname), m_type(type)
	{ }
    virtual PObject* Clone() const
	// default copy constructor - take care!
	{ return new BaseG729Capab(*this); }
    virtual unsigned GetSubType() const
	{ return m_type; }
    virtual PString GetFormatName() const
	{ return m_name; }
    virtual H323Codec* CreateCodec(H323Codec::Direction direction) const
	{ return 0; }
protected:
    const char* m_name;
    unsigned m_type;
};

// shameless adaptation from the G711 capability declaration
#define DEFINE_YATE_CAPAB(cls,base,param,name) \
class cls : public base { \
  public: \
    cls() : base(name,param) { } \
}; \
H323_REGISTER_CAPABILITY(cls,name) \

DEFINE_YATE_CAPAB(YateG7231_5,BaseG7231Capab,false,OPAL_G7231_5k3"{sw}")
DEFINE_YATE_CAPAB(YateG7231_6,BaseG7231Capab,false,OPAL_G7231_6k3"{sw}")
DEFINE_YATE_CAPAB(YateG7231A5,BaseG7231Capab,true,OPAL_G7231A_5k3"{sw}")
DEFINE_YATE_CAPAB(YateG7231A6,BaseG7231Capab,true,OPAL_G7231A_6k3"{sw}")
DEFINE_YATE_CAPAB(YateG729,BaseG729Capab,H245_AudioCapability::e_g729,OPAL_G729"{sw}")
DEFINE_YATE_CAPAB(YateG729A,BaseG729Capab,H245_AudioCapability::e_g729AnnexA,OPAL_G729A"{sw}")
DEFINE_YATE_CAPAB(YateG729B,BaseG729Capab,H245_AudioCapability::e_g729wAnnexB,OPAL_G729B"{sw}")
DEFINE_YATE_CAPAB(YateG729AB,BaseG729Capab,H245_AudioCapability::e_g729AnnexAwAnnexB,OPAL_G729AB"{sw}")

// end of fake capabilities code

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


YateGatekeeperServer::YateGatekeeperServer(YateH323EndPoint& ep)
    : H323GatekeeperServer(ep), endpoint(ep)
{
    Debug(&hplugin,DebugAll,"YateGatekeeperServer::YateGatekeeperServer() [%p]",this);
}

BOOL YateGatekeeperServer::Init()
{
    SetGatekeeperIdentifier("YATE gatekeeper");
    H323TransportAddressArray interfaces;
    const char* addr = 0;
    int i;
    for (i=1; (addr=s_cfg.getValue("gk",("interface"+String(i)).c_str())); i++){
	if (!AddListener(new H323GatekeeperListener(endpoint, *this,s_cfg.getValue("gk","name","YateGatekeeper"),new H323TransportUDP(endpoint,PIPSocket::Address(addr),s_cfg.getIntValue("gk","port",1719),0))))
	    Debug(DebugGoOn,"Can't start the Gk listener for address: %s",addr);
    }  
    return TRUE;	
}

YateH323EndPoint::YateH323EndPoint(const NamedList* params, const char* name)
    : String(name), m_gkServer(0), m_thread(0)
{
    Debug(&hplugin,DebugAll,"YateH323EndPoint::YateH323EndPoint(%p,\"%s\") [%p]",
	params,name,this);
    if (params && params->getBoolValue("gw",false))
	terminalType = e_GatewayOnly;
    hplugin.m_endpoints.append(this);
}

YateH323EndPoint::~YateH323EndPoint()
{
    Debug(&hplugin,DebugAll,"YateH323EndPoint::~YateH323EndPoint() [%p]",this);
    hplugin.m_endpoints.remove(this,false);
    RemoveListener(0);
    ClearAllCalls(H323Connection::EndedByTemporaryFailure, true);
    if (m_gkServer)
	delete m_gkServer;
    if (m_thread)
	Debug(DebugFail,"Destroying YateH323EndPoint '%s' still having a YateGkRegThread %p [%p]",
	    safe(),m_thread,this);
}

H323Connection* YateH323EndPoint::CreateConnection(unsigned callReference,
    void* userData, H323Transport* transport, H323SignalPDU* setupPDU)
{
    if (!hplugin.canAccept()) {
	Debug(DebugWarn,"Refusing new H.323 call, full or exiting");
	return 0;
    }
    return new YateH323Connection(*this,transport,callReference,userData);
}

bool YateH323EndPoint::Init(const NamedList* params)
{
    if (null()) {
	int dump = s_cfg.getIntValue("general","dumpcodecs");
	if (dump > 0)
#ifdef USE_CAPABILITY_FACTORY
	    ListRegisteredCaps(dump);
#else
	    FakeH323CapabilityRegistration::ListRegistered(dump);
#endif
    }

    String csect("codecs");
    if (!null()) {
	csect << " " << c_str();
	// fall back to global codec definitions if [codec NAME] does not exist
	if (!s_cfg.getSection(csect))
	    csect = "codecs";
    }
    bool defcodecs = s_cfg.getBoolValue(csect,"default",true);
    const char** f = h323_formats;
    for (; *f; f += 2) {
	bool ok = false;
	bool fake = false;
	String tmp(s_cfg.getValue(csect,f[1]));
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
		Debug(&hplugin,DebugAll,"H.323 added %d capabilities '%s'",num,tmp.c_str());
	    else
		// warn if codecs were disabled by default
		Debug(&hplugin,defcodecs ? DebugInfo : DebugWarn,"H323 failed to add capability '%s'",tmp.c_str());
	}
    }

    AddAllUserInputCapabilities(0,1);
    DisableDetectInBandDTMF(!(params && params->getBoolValue("dtmfinband")));
    DisableFastStart(!(params && params->getBoolValue("faststart")));
    DisableH245Tunneling(!(params && params->getBoolValue("h245tunneling")));
    DisableH245inSetup(!(params && params->getBoolValue("h245insetup")));
    SetSilenceDetectionMode(static_cast<H323AudioCodec::SilenceDetectionMode>
	(params ? params->getIntValue("silencedetect",dict_silence,H323AudioCodec::NoSilenceDetection)
	 : H323AudioCodec::NoSilenceDetection));

    PIPSocket::Address addr = INADDR_ANY;
    int port = 1720;
    if (params)
	port = params-> getIntValue("port",port);
    if ((!params) || params->getBoolValue("ep",true)) {
	H323ListenerTCP *listener = new H323ListenerTCP(*this,addr,port);
	if (!(listener && StartListener(listener))) {
	    Debug(DebugGoOn,"Unable to start H323 Listener at port %d",port);
	    if (listener)
		delete listener;
	    return false;
	}
	const char *ali = "yate";
	if (params) {
	    ali = params->getValue("username",ali);
	    ali = params->getValue("alias",ali);
	}
	SetLocalUserName(ali);
	if (params && params->getBoolValue("gkclient")){
	    const char *p = params->getValue("password");
	    if (p) {
		SetGatekeeperPassword(p);
		Debug(&hplugin,DebugInfo,"Enabling H.235 security access to gatekeeper %s",p);
	    }
	    const char* d = params->getValue("gkip");
	    const char* a = params->getValue("gkname");
	    if (d)
		startGkClient(ByAddr,d);
	    else if (a)
		startGkClient(ByName,a);
	    else
		startGkClient(Discover);
	}
    }

    // only the first, nameless endpoint can be a gatekeeper
    if ((!m_gkServer) && null() && s_cfg.getBoolValue("gk","server",false)) {
	m_gkServer = new YateGatekeeperServer(*this);
	m_gkServer->Init();
    }

    return true;
}

// Start a new PThread that performs GK discovery
bool YateH323EndPoint::startGkClient(int mode, const char* name)
{
    int retries = 10;
    hplugin.lock();
    while (m_thread) {
	hplugin.unlock();
	if (!--retries) {
	    Debug(&hplugin,DebugGoOn,"Old Gk client thread in '%s' not finished",safe());
	    return false;
	}
	Thread::msleep(25);
	hplugin.lock();
    }
    m_thread = new YateGkRegThread(this,mode,name);
    hplugin.unlock();
    m_thread->SetAutoDelete();
    m_thread->Resume();
    return true;
}

void YateH323EndPoint::asyncGkClient(int mode, const PString& name)
{
    switch (mode) {
	case ByAddr:
	    if (SetGatekeeper(name,new H323TransportUDP(*this))) {
		Debug(&hplugin,DebugInfo,"Connected '%s' to GK addr '%s'",
		    safe(),(const char*)name);
		m_thread = 0;
		return;
	    }
	    Debug(&hplugin,DebugWarn,"Failed to connect '%s' to GK addr '%s'",
		safe(),(const char*)name);
	    break;
	case ByName:
	    if (LocateGatekeeper(name)) {
		Debug(&hplugin,DebugInfo,"Connected '%s' to GK name '%s'",
		    safe(),(const char*)name);
		m_thread = 0;
		return;
	    }
	    Debug(&hplugin,DebugWarn,"Failed to connect '%s' to GK name '%s'",
		safe(),(const char*)name);
	    break;
	case Discover:
	    if (DiscoverGatekeeper(new H323TransportUDP(*this))) {
		Debug(&hplugin,DebugInfo,"Connected '%s' to discovered GK",safe());
		m_thread = 0;
		return;
	    }
	    Debug(&hplugin,DebugWarn,"Failed to discover a GK in '%s'",safe());
	    break;
	case Unregister:
	    RemoveGatekeeper();
	    m_thread = 0;
	    return;
    }
    RemoveListener(0);
    m_thread = 0;
}

YateH323Connection::YateH323Connection(YateH323EndPoint& endpoint,
    H323Transport* transport, unsigned callReference, void* userdata)
    : H323Connection(endpoint,callReference), m_chan(0),
      m_externalRtp(s_externalRtp), m_nativeRtp(false), m_passtrough(false),
      m_rtpPort(0), m_remotePort(0)
{
    Debug(&hplugin,DebugAll,"YateH323Connection::YateH323Connection(%p,%u,%p) [%p]",
	&endpoint,callReference,userdata,this);

    // outgoing calls get the "call.execute" message as user data
    Message* msg = static_cast<Message*>(userdata);
    m_chan = new YateH323Chan(this,(userdata != 0),
	((transport && !userdata) ? (const char*)transport->GetRemoteAddress() : 0));
    if (!msg) {
	m_passtrough = s_passtrough;
	return;
    }

    setCallerID(msg->getValue("caller"),msg->getValue("callername"));
    rtpForward(*msg,s_passtrough);

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg->userData());
    if (ch && ch->connect(m_chan)) {
	m_chan->setTarget(msg->getValue("id"));
	msg->setParam("peerid",m_chan->id());
	msg->setParam("targetid",m_chan->id());
	m_chan->deref();
    }
}

YateH323Connection::~YateH323Connection()
{
    Debug(&hplugin,DebugAll,"YateH323Connection::~YateH323Connection() [%p]",this);
    YateH323Chan* tmp = m_chan;
    m_chan = 0;
    if (tmp) {
	tmp->m_conn = 0;
	tmp->disconnect();
    }
    cleanups();
}

void YateH323Connection::cleanups()
{
    m_chan = 0;
    CloseAllLogicalChannels(true);
    CloseAllLogicalChannels(false);
}

H323Connection::AnswerCallResponse YateH323Connection::OnAnswerCall(const PString &caller,
    const H323SignalPDU &setupPDU, H323SignalPDU &connectPDU)
{
    Debug(m_chan,DebugInfo,"YateH323Connection::OnAnswerCall caller='%s' chan=%p [%p]",
	(const char *)caller,m_chan,this);
    if (!m_chan)
	return H323Connection::AnswerCallDenied;
    if (!hplugin.canAccept()) {
	Debug(DebugWarn,"Not answering H.323 call, full or exiting");
	return H323Connection::AnswerCallDenied;
    }

    Message *m = m_chan->message("call.route",false,true);
    const char *s = s_cfg.getValue("incoming","context");
    if (s)
	m->setParam("context",s);

    m->setParam("callername",caller);
    s = GetRemotePartyNumber();
    Debug(m_chan,DebugInfo,"GetRemotePartyNumber()='%s'",s);
    m->setParam("caller",s ? s : (const char *)("h323/"+caller));

    const H225_Setup_UUIE &setup = setupPDU.m_h323_uu_pdu.m_h323_message_body;
    const H225_ArrayOf_AliasAddress &adr = setup.m_destinationAddress;
    for (int i = 0; i<adr.GetSize(); i++)
	Debug(m_chan,DebugAll,"adr[%d]='%s'",i,(const char *)H323GetAliasAddressString(adr[i]));
    String called;
    if (adr.GetSize() > 0)
	called = (const char *)H323GetAliasAddressString(adr[0]);
    if (called.null())
	called = s_cfg.getValue("incoming","called");
    if (!called.null()) {
	Debug(m_chan,DebugInfo,"Called number is '%s'",called.c_str());
	m->setParam("called",called);
    }
    else
	Debug(m_chan,DebugMild,"No called number present!");
#if 0
    s = GetRemotePartyAddress();
    Debug(m_chan,DebugInfo,"GetRemotePartyAddress()='%s'",s);
    if (s)
	m->setParam("calledname",s);
#endif
    if (hasRemoteAddress()) {
	m->addParam("rtp_forward","possible");
	m->addParam("rtp_addr",m_remoteAddr);
	m->addParam("rtp_port",String(m_remotePort));
	m->addParam("formats",m_remoteFormats);
    }

    if (m_chan->startRouter(m))
	return H323Connection::AnswerCallDeferred;
    Debug(&hplugin,DebugWarn,"Error starting H.323 routing thread! [%p]",this);
    return H323Connection::AnswerCallDenied;
}

void YateH323Connection::rtpExecuted(Message& msg)
{
    Debug(m_chan,DebugAll,"YateH323Connection::rtpExecuted(%p) [%p]",
	&msg,this);
    if (!m_passtrough)
	return;
    String tmp = msg.getValue("rtp_forward");
    m_passtrough = (tmp == "accepted");
    if (m_passtrough)
	Debug(m_chan,DebugInfo,"H323 Peer accepted RTP forward");
}

void YateH323Connection::rtpForward(Message& msg, bool init)
{
    Debug(m_chan,DebugAll,"YateH323Connection::rtpForward(%p,%d) [%p]",
	&msg,init,this);
    String tmp = msg.getValue("rtp_forward");
    if (!((init || m_passtrough) && tmp))
	return;
    m_passtrough = tmp.toBoolean();
    if (!m_passtrough)
	return;
    int port = msg.getIntValue("rtp_port");
    String addr(msg.getValue("rtp_addr"));
    if (port && addr) {
	m_rtpAddr = addr;
	m_rtpPort = port;
	m_formats = msg.getValue("formats");
	msg.setParam("rtp_forward","accepted");
	Debug(m_chan,DebugInfo,"Accepted RTP forward %s:%d formats '%s'",
	    addr.c_str(),port,m_formats.safe());
    }
    else {
	m_passtrough = false;
	Debug(m_chan,DebugInfo,"Disabling RTP forward [%p]",this);
    }
}

void YateH323Connection::answerCall(AnswerCallResponse response)
{
    bool media = false;
    if (hasRemoteAddress() && m_rtpPort)
	media = true;
    else if (m_chan && m_chan->getConsumer() && m_chan->getConsumer()->getConnSource())
	media = true;
    // modify responses to indicate we have early media (remote ringing)
    if (media) {
	switch (response) {
	    case AnswerCallPending:
		response = AnswerCallAlertWithMedia;
		break;
	    case AnswerCallDeferred:
		response = AnswerCallDeferredWithMedia;
		break;
	    default:
		break;
	}
    }
    AnsweringCall(response);
}

void YateH323Connection::OnEstablished()
{
    Debug(m_chan,DebugInfo,"YateH323Connection::OnEstablished() [%p]",this);
    if (!m_chan)
	return;
    if (HadAnsweredCall()) {
	m_chan->status("connected");
	return;
    }
    m_chan->status("answered");
    Message *m = m_chan->message("call.answered",false,true);
    if (m_passtrough) {
	if (m_remotePort) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_remoteAddr);
	    m->addParam("rtp_port",String(m_remotePort));
	    m->addParam("formats",m_remoteFormats);
	}
	else
	    Debug(m_chan,DebugWarn,"H323 RTP passtrough with no remote address! [%p]",this);
    }
    Engine::enqueue(m);
}

void YateH323Connection::OnCleared()
{
    int reason = GetCallEndReason();
    const char* rtext = CallEndReasonText(reason);
    Debug(m_chan,DebugInfo,"YateH323Connection::OnCleared() reason: %s (%d) [%p]",
	rtext,reason,this);
    if (m_chan)
	m_chan->disconnect(rtext);
}

BOOL YateH323Connection::OnAlerting(const H323SignalPDU &alertingPDU, const PString &user)
{
    Debug(m_chan,DebugInfo,"YateH323Connection::OnAlerting '%s' [%p]",(const char *)user,this);
    if (!m_chan)
	return FALSE;
    m_chan->status("ringing");
    Message *m = m_chan->message("call.ringing",false,true);
    if (hasRemoteAddress()) {
	m->addParam("rtp_forward","yes");
	m->addParam("rtp_addr",m_remoteAddr);
	m->addParam("rtp_port",String(m_remotePort));
	m->addParam("formats",m_remoteFormats);
    }
    Engine::enqueue(m);
    return TRUE;
}

void YateH323Connection::OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
    Debug(m_chan,DebugInfo,"YateH323Connection::OnUserInputTone '%c' duration=%u [%p]",tone,duration,this);
    if (!m_chan)
	return;
    char buf[2];
    buf[0] = tone;
    buf[1] = 0;
    Message *m = m_chan->message("chan.dtmf",false,true);
    m->addParam("text",buf);
    m->addParam("duration",String(duration));
    Engine::enqueue(m);
}

void YateH323Connection::OnUserInputString(const PString &value)
{
    Debug(m_chan,DebugInfo,"YateH323Connection::OnUserInputString '%s' [%p]",(const char *)value,this);
    if (!m_chan)
	return;
    String text((const char *)value);
    const char *type = text.startSkip("MSG") ? "chan.text" : "chan.dtmf";
    Message *m = m_chan->message(type,false,true);
    m->addParam("text",text);
    Engine::enqueue(m);
}

BOOL YateH323Connection::OpenAudioChannel(BOOL isEncoding, unsigned bufferSize,
    H323AudioCodec &codec)
{
    Debug(m_chan,DebugInfo,"YateH323Connection::OpenAudioChannel [%p]",this);
    if (!m_nativeRtp) {
	Debug(DebugGoOn,"YateH323Connection::OpenAudioChannel for non-native RTP in [%p]",this);
	return FALSE;
    }
    return m_chan && m_chan->OpenAudioChannel(isEncoding,codec);
}

#ifdef NEED_RTP_QOS_PARAM
H323Channel* YateH323Connection::CreateRealTimeLogicalChannel(const H323Capability& capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters* param,RTP_QOS* rtpqos)
#else
H323Channel* YateH323Connection::CreateRealTimeLogicalChannel(const H323Capability& capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters* param)
#endif
{
    Debug(m_chan,DebugAll,"H323Connection::CreateRealTimeLogicalChannel%s%s [%p]",
	m_externalRtp ? " external" : "",m_passtrough ? " passtrough" : "",this);
    if (m_externalRtp || m_passtrough) {
	const char* sdir = lookup(dir,dict_h323_dir);
	const char *format = 0;
	decodeCapability(capability,&format);
	Debug(m_chan,DebugAll,"Capability '%s' format '%s' session %u %s",
	    (const char *)capability.GetFormatName(),format,sessionID,sdir);

	// disallow codecs not supported by remote receiver
	if (m_passtrough && !(m_formats.null() || (m_formats.find(format) >= 0))) {
	    Debug(m_chan,DebugMild,"Refusing to create '%s' not in remote '%s'",format,m_formats.c_str());
	    return 0;
	}

	if (m_passtrough && (dir == H323Channel::IsReceiver)) {
	    if (format && (m_remoteFormats.find(format) < 0) && s_cfg.getBoolValue("codecs",format,true)) {
		if (m_remoteFormats)
		    m_remoteFormats << ",";
		m_remoteFormats << format;
	    }
	}
	PIPSocket::Address externalIpAddress;
	GetControlChannel().GetLocalAddress().GetIpAddress(externalIpAddress);
	Debug(m_chan,DebugInfo,"address '%s'",(const char *)externalIpAddress.AsString());
	WORD externalPort = 0;
	if (!m_passtrough) {
	    Message m("chan.rtp");
	    m.addParam("localip",externalIpAddress.AsString());
	    m.userData(m_chan);
	    if (sdir)
		m.addParam("direction",sdir);
	    if (Engine::dispatch(m)) {
		m_rtpid = m.getValue("rtpid");
		externalPort = m.getIntValue("localport");
	    }
	}
	if (externalPort || m_passtrough) {
	    m_nativeRtp = false;
	    if (!externalPort) {
		externalPort = m_rtpPort;
		externalIpAddress = PString(m_rtpAddr.safe());
	    }
	    return new YateH323_ExternalRTPChannel(*this, capability, dir, sessionID, externalIpAddress, externalPort);
	}
	Debug(m_chan,DebugWarn,"YateH323Connection falling back to native RTP [%p]",this);
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
    Debug(m_chan,DebugAll,"YateH323Connection::OnSetLocalCapabilities()%s%s [%p]",
	m_externalRtp ? " external" : "",m_passtrough ? " passtrough" : "",this);
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
		Debug(m_chan,DebugAll,"Removing capability '%s' (%s) not in remote '%s'",
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
    Debug(m_chan,DebugInfo,"YateH323Connection::OnStartLogicalChannel(%p) [%p]",&channel,this);
    return m_nativeRtp ? H323Connection::OnStartLogicalChannel(channel) : TRUE;
}

BOOL YateH323Connection::OnCreateLogicalChannel(const H323Capability & capability, H323Channel::Directions dir, unsigned & errorCode ) 
{
    Debug(m_chan,DebugInfo,"YateH323Connection::OnCreateLogicalChannel('%s',%s) [%p]",(const char *)capability.GetFormatName(),lookup(dir,dict_h323_dir),this);
    return H323Connection::OnCreateLogicalChannel(capability,dir,errorCode);
}

BOOL YateH323Connection::decodeCapability(const H323Capability& capability, const char** dataFormat, int* payload, String* capabName)
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
    DDebug(&hplugin,DebugAll,"capability '%s' format '%s' payload %d",fname.c_str(),format,pload);
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

void YateH323Connection::setRemoteAddress(const char* remoteIP, WORD remotePort)
{
    if (!m_remotePort) {
	Debug(m_chan,DebugInfo,"Copying remote RTP address [%p]",this);
	m_remotePort = remotePort;
	m_remoteAddr = remoteIP;
    }
}

BOOL YateH323Connection::startExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan)
{
    const char* sdir = lookup(dir,dict_h323_dir);
    Debug(m_chan,DebugAll,"YateH323Connection::startExternalRTP(\"%s\",%u,%s,%p) [%p]",
	remoteIP,remotePort,sdir,chan,this);
    if (m_passtrough && m_rtpPort) {
	setRemoteAddress(remoteIP,remotePort);

	Debug(m_chan,DebugInfo,"Passing RTP to %s:%d",m_rtpAddr.c_str(),m_rtpPort);
	const PIPSocket::Address ip(m_rtpAddr.safe());
	WORD dataPort = m_rtpPort;
	chan->SetExternalAddress(H323TransportAddress(ip, dataPort), H323TransportAddress(ip, dataPort+1));
	stoppedExternal(dir);
	return TRUE;
    }
    if (!m_externalRtp)
	return FALSE;
    Message m("chan.rtp");
    m.userData(m_chan);
    if (m_rtpid)
	m.setParam("rtpid",m_rtpid);
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
	m_rtpid = m.getValue("rtpid");
	return TRUE;
    }
    return FALSE;
}

void YateH323Connection::stoppedExternal(H323Channel::Directions dir)
{
    Debug(m_chan,DebugInfo,"YateH323Connection::stoppedExternal(%s) [%p]",
	lookup(dir,dict_h323_dir),this);
    if (!m_chan)
	return;
    switch (dir) {
	case H323Channel::IsReceiver:
	    m_chan->setSource();
	    break;
	case H323Channel::IsTransmitter:
	    m_chan->setConsumer();
	    break;
	case H323Channel::IsBidirectional:
	    m_chan->setSource();
	    m_chan->setConsumer();
	default:
	    break;
    }
}

bool YateH323Connection::sendTone(Message& msg, const char* tone)
{
    if (m_rtpid) {
        msg.setParam("targetid",m_rtpid);
	return false;
    }
    while (*tone)
	SendUserInputTone(*tone++);
    return true;
}

YateH323_ExternalRTPChannel::YateH323_ExternalRTPChannel( 
    YateH323Connection& connection,
    const H323Capability& capability,
    Directions direction, 
    unsigned sessionID, 
    const PIPSocket::Address& ip, 
    WORD dataPort)
    : H323_ExternalRTPChannel(connection, capability, direction, sessionID, ip, dataPort),
      m_conn(&connection)
{ 
    Debug(&hplugin,DebugAll,"YateH323_ExternalRTPChannel::YateH323_ExternalRTPChannel %s addr=%s:%u [%p]",
	lookup(GetDirection(),dict_h323_dir), (const char *)ip.AsString(), dataPort,this);
    SetExternalAddress(H323TransportAddress(ip, dataPort), H323TransportAddress(ip, dataPort+1));
}

YateH323_ExternalRTPChannel::~YateH323_ExternalRTPChannel()
{
    Debug(&hplugin,DebugInfo,"YateH323_ExternalRTPChannel::~YateH323_ExternalRTPChannel %s%s [%p]",
	lookup(GetDirection(),dict_h323_dir),(isRunning ? " running" : ""),this);
    if (isRunning) {
	isRunning = FALSE;
	if (m_conn)
	    m_conn->stoppedExternal(GetDirection());
    }
}

BOOL YateH323_ExternalRTPChannel::Start()
{
    Debug(&hplugin,DebugAll,"YateH323_ExternalRTPChannel::Start() [%p]",this);
    if (!m_conn)
	return FALSE;

    PIPSocket::Address remoteIpAddress;
    WORD remotePort;
    GetRemoteAddress(remoteIpAddress,remotePort);
    Debug(&hplugin,DebugInfo,"external rtp ip address %s:%u",(const char *)remoteIpAddress.AsString(),remotePort);

    return isRunning = m_conn->startExternalRTP((const char *)remoteIpAddress.AsString(), remotePort, GetDirection(), this);
}

BOOL YateH323_ExternalRTPChannel::OnReceivedPDU(
				const H245_H2250LogicalChannelParameters& param,
				unsigned& errorCode)
{
    Debug(&hplugin,DebugAll,"YateH323_ExternalRTPChannel::OnReceivedPDU [%p]",this);
    if (!H323_ExternalRTPChannel::OnReceivedPDU(param,errorCode))
	return FALSE;
    if (!m_conn || m_conn->hasRemoteAddress())
	return TRUE;
    PIPSocket::Address remoteIpAddress;
    WORD remotePort;
    GetRemoteAddress(remoteIpAddress,remotePort);
    Debug(&hplugin,DebugInfo,"Remote RTP IP address %s:%u",(const char *)remoteIpAddress.AsString(),remotePort);
    m_conn->setRemoteAddress((const char *)remoteIpAddress.AsString(), remotePort);
    return TRUE;
}

BOOL YateH323_ExternalRTPChannel::OnSendingPDU(H245_H2250LogicalChannelParameters& param)
{
    Debug(&hplugin,DebugAll,"YateH323_ExternalRTPChannel::OnSendingPDU [%p]",this);
    return H323_ExternalRTPChannel::OnSendingPDU(param);
}

BOOL YateH323_ExternalRTPChannel::OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters& param)
{
    Debug(&hplugin,DebugAll,"YateH323_ExternalRTPChannel::OnReceivedAckPDU [%p]",this);
    return H323_ExternalRTPChannel::OnReceivedAckPDU(param);
}

void YateH323_ExternalRTPChannel::OnSendOpenAck(H245_H2250LogicalChannelAckParameters& param)
{
    Debug(&hplugin,DebugAll,"YateH323_ExternalRTPChannel::OnSendOpenAck [%p]",this);
    H323_ExternalRTPChannel::OnSendOpenAck(param);
}

YateH323AudioSource::~YateH323AudioSource()
{
    Debug(&hplugin,DebugAll,"YateH323AudioSource::~YateH323AudioSource() [%p]",this);
    m_exit = true;
    // Delay actual destruction until the mutex is released
    m_mutex.lock();
    m_data.clear(false);
    m_mutex.unlock();
}

YateH323AudioConsumer::~YateH323AudioConsumer()
{
    Debug(&hplugin,DebugAll,"YateH323AudioConsumer::~YateH323AudioConsumer() [%p]",this);
    m_exit = true;
    // Delay actual destruction until the mutex is released
    m_mutex.check();
}

BOOL YateH323AudioConsumer::Close()
{
    Debug(&hplugin,DebugAll,"YateH323AudioConsumer::Close() [%p]",this);
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
	Debug(&hplugin,DebugAll,"Consumer skipped %u bytes, buffer is full [%p]",data.length(),this);
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
	    XDebug(&hplugin,DebugAll,"Consumer pulled %d bytes from buffer [%p]",len,this);
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
    Debug(&hplugin,DebugAll,"YateH323AudioSource::Close() [%p]",this);
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
    if (m->retValue().null())
	return FALSE;
    password = m->retValue();
    return TRUE;
}

H323GatekeeperCall* YateGatekeeperServer::CreateCall(const OpalGloballyUniqueID& id,
						H323GatekeeperCall::Direction dir)
{
    return new YateGatekeeperCall(*this, id, dir);
}

H323GatekeeperRequest::Response YateGatekeeperServer::OnRegistration(H323GatekeeperRRQ& request)
{
    int i = H323GatekeeperServer::OnRegistration(request);
    if (i == H323GatekeeperRequest::Confirm) {	
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
	    m->addParam("driver","h323");
	    m->addParam("data",ips);
	    if (!Engine::dispatch(m) && !m->retValue().null())
		return H323GatekeeperRequest::Reject;
	}
    }
    else 
	return (H323Transaction::Response)i;
    return H323GatekeeperRequest::Confirm;
}

H323GatekeeperRequest::Response YateGatekeeperServer::OnUnregistration(H323GatekeeperURQ& request)
{
    /* We use just the first alias since is the one we need */
    int i = H323GatekeeperServer::OnUnregistration(request);
    if (i == H323GatekeeperRequest::Confirm) {
	for (int j = 0; j < request.urq.m_endpointAlias.GetSize(); j++) {
	    PString alias = H323GetAliasAddressString(request.urq.m_endpointAlias[j]);
	    if (alias.IsEmpty())
		return H323GatekeeperRequest::Reject;
	    Message *m = new Message("user.unregister");
	    m->addParam("username",alias);
	    Engine::dispatch(m);
	}
    }
    else 
	return (H323Transaction::Response)i;
    return H323GatekeeperRequest::Confirm;
}

BOOL YateGatekeeperServer::TranslateAliasAddressToSignalAddress(const H225_AliasAddress& alias,H323TransportAddress& address)
{
    PString aliasString = H323GetAliasAddressString(alias);
    Message m("call.route");
    m.addParam("called",aliasString);
    Engine::dispatch(m);
    String s = m.retValue();
    if (s) {
	/** 
	 * Here we have 2 cases, first is handle when the call have to be send
	 * to endpoint (if the call is to another yate channel, or is h323 
	 * proxied), or if has to be send to another gatekeeper we find out 
	 * from the driver parameter
	 */
	    if ((m.getParam("driver")) && (*(m.getParam("driver")) == "h323"))
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


YateGatekeeperCall::YateGatekeeperCall(YateGatekeeperServer& gk,
                                   const OpalGloballyUniqueID& id,
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

void YateH323Connection::setCallerID(const char* number, const char* name)
{
    if (!number && isE164(name)) {
	number = name;
	name = 0;
    }

    if (!(name || number))
	return;

    if (isE164(number)) {
	String display;
	if (!name)
	    display << number << " [" << s_cfg.getValue("ep","ident","yate") << "]";
	else if (isE164(name))
	    display << number << " [" << name << "]";
	else
	    display = name;
	Debug(m_chan,DebugInfo,"Setting H.323 caller: number='%s' name='%s'",number,display.c_str());
	SetLocalPartyName(number);
	localAliasNames.AppendString(display.c_str());
    }
    else {
	String display;
	if (number && name)
	    display << number << " [" << name << "]";
	else if (number)
	    display = number;
	else
	    display = name;
	Debug(m_chan,DebugInfo,"Setting H.323 caller: name='%s'",display.c_str());
	SetLocalPartyName(display.c_str());
    }
}

YateH323Chan::YateH323Chan(YateH323Connection* conn,bool outgoing,const char* addr)
    : Channel(hplugin,0,outgoing), m_conn(conn)
{
    Debug(this,DebugAll,"YateH323Chan::YateH323Chan(%p,%s) %s [%p]",
	conn,addr,direction(),this);
    m_address = addr;
    Engine::enqueue(message("chan.startup"));
}

YateH323Chan::~YateH323Chan()
{
    Debug(this,DebugAll,"YateH323Chan::~YateH323Chan() %s %s [%p]",
	m_status.c_str(),m_id.c_str(),this);
    Message *m = message("chan.hangup");
    drop();
    YateH323Connection* tmp = m_conn;
    m_conn = 0;
    if (tmp) {
	const char* err = 0;
	const char* txt = "Normal cleanup";
	int reason = tmp->GetCallEndReason();
	if (reason != H323Connection::NumCallEndReasons) {
	    err = lookup(reason,dict_errors);
	    txt = CallEndReasonText(reason);
	}
	if (err)
	    m->setParam("error",err);
	if (txt)
	    m->setParam("reason",txt);
	Engine::enqueue(m);

	PSyncPoint sync;
	tmp->cleanups();
	tmp->ClearCallSynchronous(&sync);
    }
    else
	Engine::enqueue(m);
}

void YateH323Chan::disconnected(bool final, const char *reason)
{
    Debugger debug("YateH323Chan::disconnected()"," '%s' [%p]",reason,this);
    if (!final) {
	Channel::disconnected(final,reason);
	return;
    }
    YateH323AudioSource* s = YOBJECT(YateH323AudioSource,getSource());
    if (s)
	s->Close();
    YateH323AudioConsumer* c = YOBJECT(YateH323AudioConsumer,getConsumer());
    if (c)
	c->Close();
    if (m_conn)
	m_conn->ClearCall((H323Connection::CallEndReason)lookup(reason,dict_errors,H323Connection::EndedByLocalUser));
}

BOOL YateH323Chan::OpenAudioChannel(BOOL isEncoding, H323AudioCodec &codec)
{
    if (isEncoding) {
	if (!getConsumer()) {
	    setConsumer(new YateH323AudioConsumer);
	    getConsumer()->deref();
	}
	// data going TO h.323
	if (getConsumer())
	    return codec.AttachChannel(static_cast<YateH323AudioConsumer *>(getConsumer()),false);
    }
    else {
	if (!getSource()) {
            setSource(new YateH323AudioSource);
	    getSource()->deref();
	}
	// data coming FROM h.323
	if (getSource())
	    return codec.AttachChannel(static_cast<YateH323AudioSource *>(getSource()),false);
    }
    return FALSE;
}

bool YateH323Chan::callRouted(Message& msg)
{
    Channel::callRouted(msg);
    if (m_conn) {
        String s(msg.retValue());
        if (s.startSkip("h323/",false) && s && msg.getBoolValue("redirect")) {
            Debug(this,DebugAll,"YateH323Chan redirecting to '%s' [%p]",s.c_str(),this);
	    m_conn->TransferCall(s.safe());
	    return false;
	}
	return true;
    }
    return false;
}

void YateH323Chan::callAccept(Message& msg)
{
    Channel::callAccept(msg);
    if (m_conn) {
	m_conn->rtpExecuted(msg);
	m_conn->answerCall(H323Connection::AnswerCallPending);
    }
}

void YateH323Chan::callReject(const char* error, const char* reason)
{
    Channel::callReject(error,reason);
}

bool YateH323Chan::msgRinging(Message& msg)
{
    Channel::msgRinging(msg);
    if (!m_conn)
	return false;
    if (msg.getParam("rtp_forward"))
	m_conn->rtpForward(msg);
    m_conn->answerCall(H323Connection::AnswerCallPending);
    return true;
}

bool YateH323Chan::msgAnswered(Message& msg)
{
    if (!m_conn)
	return false;
    m_conn->rtpForward(msg);
    m_conn->answerCall(H323Connection::AnswerCallNow);
    return true;
}

bool YateH323Chan::msgTone(Message& msg, const char* tone)
{
    return tone && m_conn && m_conn->sendTone(msg,tone);
}

bool YateH323Chan::msgText(Message& msg, const char* text)
{
    if (text && m_conn) {
	Debug(this,DebugInfo,"Text '%s' for %s [%p]",text,id().c_str(),this);
	m_conn->SendUserInputIndicationString(text);
	return true;
    }
    return false;
}

bool UserHandler::received(Message &msg)
{
    String tmp(msg.getValue("protocol"));
    if (tmp != "h323")
	return false;
    tmp = msg.getValue("account");
    tmp.trimBlanks();
    if (tmp.null())
	return false;
    if (!hplugin.findEndpoint(tmp)) {
	YateH323EndPoint* ep = new YateH323EndPoint(&msg,tmp);
	ep->Init(&msg);
    }
    return true;
}

H323Driver::H323Driver()
    : Driver("h323","varchans")
{
    Output("Loaded module H.323 - based on OpenH323-" OPENH323_VERSION);
}

H323Driver::~H323Driver()
{
    cleanup();
    if (s_process) {
	delete s_process;
	s_process = 0;
    }
}

bool H323Driver::received(Message &msg, int id)
{
    bool ok = Driver::received(msg,id);
    if (id == Halt)
        cleanup();
    return ok;
};

void H323Driver::cleanup()
{
    channels().clear();
    m_endpoints.clear();
    if (s_process) {
	PSyncPoint terminationSync;
	terminationSync.Signal();
	Output("Waiting for OpenH323 to die");
	terminationSync.Wait();
    }
}

bool H323Driver::msgExecute(Message& msg, String& dest)
{
    if (dest.null())
        return false;
    if (!msg.userData()) {
	Debug(this,DebugWarn,"H.323 call found but no data channel!");
	return false;
    }
    Debug(this,DebugInfo,"Found call to H.323 target='%s'",
	dest.c_str());
    PString p;
    YateH323EndPoint* ep = hplugin.findEndpoint(msg.getValue("line"));
    YateH323Connection* conn = ep ? static_cast<YateH323Connection*>(
	ep->MakeCallLocked(dest.c_str(),p,&msg)
    ) : 0;
    if (conn) {
	conn->Unlock();
	return true;
    }
    return false;
};
		    
YateH323EndPoint* H323Driver::findEndpoint(const String& ep) const
{
    ObjList* l = m_endpoints.find(ep);
    return static_cast<YateH323EndPoint*>(l ? l->get() : 0);
}

void H323Driver::initialize()
{
    Output("Initializing module H.323");
    s_cfg = Engine::configFile("h323chan");
    s_cfg.load();
    setup();
    s_externalRtp = s_cfg.getBoolValue("general","external_rtp",false);
    s_passtrough = s_cfg.getBoolValue("general","passtrough_rtp",false);
    maxRoute(s_cfg.getIntValue("incoming","maxqueue",5));
    maxChans(s_cfg.getIntValue("ep","maxconns",0));
    if (!s_process) {
	installRelay(Halt);
	s_process = new H323Process;
	Engine::install(new UserHandler);
    }
    int dbg = s_cfg.getIntValue("general","debug");
    if (dbg < 0)
	dbg = 0;
    if (dbg > 10)
	dbg = 10;
    PTrace::Initialise(dbg,0,PTrace::Blocks | PTrace::Timestamp
	| PTrace::Thread | PTrace::FileAndLine);
    if (!m_endpoints.count()) {
	NamedList* sect = s_cfg.getSection("ep");
	YateH323EndPoint* ep = new YateH323EndPoint(sect);
	ep->Init(sect);
	int n = s_cfg.sections();
	for (int i = 0; i < n; i++) {
	    sect = s_cfg.getSection(i);
	    if (!sect)
		continue;
	    String s(*sect);
	    if (s.startSkip("ep ",false) && s) {
		ep = new YateH323EndPoint(sect,s);
		ep->Init(sect);
	    }
	}
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
