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
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <ptlib.h>
#include <h323.h>
#include <h323neg.h>
#include <h323pdu.h>
#include <h323caps.h>
#include <ptclib/delaychan.h>
#include <gkserver.h>
#include <q931.h>

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

#if (OPENH323_NUMVERSION >= 12000)
typedef PBoolean BOOL;
#endif

// These defines collide with DSCP service classes
#undef CS4
#undef CS5
#undef CS6
#undef CS7

#include <yatephone.h>
#include <yateversn.h>

#include <string.h>

using namespace TelEngine;
namespace { // anonymous

static bool s_externalRtp;
static bool s_fallbackRtp;
static bool s_needMedia = true;
static bool s_passtrough;
static bool s_pwlibThread;
static int s_maxCleaning = 0;

static Mutex s_cfgMutex(false,"H323:cfg");
static Configuration s_cfg;

static Mutex s_mutex(false,"H323Chan");
static int s_connCount = 0;
static int s_chanCount = 0;
static unsigned int s_engineStop = 0;
// Time to wait for an endpoint's client thread to terminate
static u_int64_t s_waitGkClient = 200000;
// Time to wait for the gatekeeper usage flag to be released when creating a connection
static u_int64_t s_waitGkUsageCall = 200000;
// Time to wait for the gatekeeper usage flag to be released when setting/removing
// an endpoint's gatekeeper
static u_int64_t s_waitGkUsageChanging = 300000;

static const TokenDict dict_str2code[] = {
    { "alpha" , PProcess::AlphaCode },
    { "beta" , PProcess::BetaCode },
    { "release" , PProcess::ReleaseCode },
    { 0 , 0 },
};

static const char* h323_formats[] = {
    "G.711-ALaw-64k", "alaw",
    "G.711-uLaw-64k", "mulaw",
    "GSM-06.10", "gsm",
    "MS-GSM", "msgsm",
    "SpeexNarrow", "speex",
    "LPC-10", "lpc10",
    "iLBC-15k2", "ilbc20",
    "iLBC-13k3", "ilbc30",
    "G.723", "g723",
    "G.726", "g726",
    "G.728", "g728",
    "G.729B", "g729b",
    "G.729", "g729",
    "PCM-16", "slin",
#if 0
    "G.729A", "g729a",
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
    "iLBC", "ilbc",
    "SpeexNarrow-18.2k", "speex-18k2",
    "SpeexNarrow-15k", "speex-15k",
    "SpeexNarrow-11k", "speex-11k",
    "SpeexNarrow-8k", "speex-8k",
    "SpeexNarrow-5.95k", "speex-5k95",
#endif
    0
};

static const TokenDict dict_h323_dir[] = {
    { "receive", H323Channel::IsReceiver },
    { "send", H323Channel::IsTransmitter },
    { "bidir", H323Channel::IsBidirectional },
    { 0 , 0 },
};

static const TokenDict dict_silence[] = {
    { "none", H323AudioCodec::NoSilenceDetection },
    { "fixed", H323AudioCodec::FixedSilenceDetection },
    { "adaptive", H323AudioCodec::AdaptiveSilenceDetection },
    { 0 , 0 },
};

// OpenH323 cause code mappings
static const TokenDict dict_errors[] = {
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
    { "noanswer", H323Connection::EndedByNoAnswer },
    { "noanswer", H323Connection::EndedByCallerAbort },
    { 0 , 0 },
};

// Q.931/Q.850 cause code mappings
static const TokenDict q931_errors[] = {
    { "incomplete", Q931::InvalidNumberFormat },
    { "congestion", Q931::NoCircuitChannelAvailable },
    { "congestion", Q931::TemporaryFailure },
    { "congestion", Q931::Congestion },
    { "offline", Q931::SubscriberAbsent },
    { "nocall", Q931::InvalidCallReference },
#if (OPENH323_NUMVERSION >= 11506)
    { "nocall", Q931::IdentifiedChannelNonExistent },
#endif
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

static int cleaningCount()
{
    Lock lock(s_mutex);
    return s_connCount - s_chanCount;
}

static bool cleaningBusy()
{
    int maxc = s_maxCleaning;
    return (maxc > 0) && (cleaningCount() > maxc);
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
	    (PProcess::CodeStatus)s_cfg.getIntValue("general","status",dict_str2code,PProcess::ReleaseCode),
	    (unsigned short)s_cfg.getIntValue("general","build",YATE_BUILD)
	    )
	{ Resume(); }
public:
    void Main()
	{ }
};

class YateGkRegThread;
class YateGatekeeperServer;
class YateH323EndPoint;
class YateH323Chan;

class DtmfMethods
{
public:
    enum Method {
	H323,
	Rfc2833,
	Inband,
	MethodCount
    };
    inline DtmfMethods()
	{ setDefault(); }
    inline void set(int _0 = MethodCount, int _1 = MethodCount, int _2 = MethodCount) {
	    m_methods[0] = _0;
	    m_methods[1] = _1;
	    m_methods[2] = _2;
	}
    inline void setDefault()
	{ set(Rfc2833,H323,Inband); }
    // Replace all methods from comma separated list
    // If no method is set use other or setDefEmpty (reset to default)
    // Return false if methods contain unknown methods
    bool set(const String& methods, const DtmfMethods* other, bool setDefEmpty = true,
	bool intersectOther = false);
    // Intersect with other methods
    void intersect(const DtmfMethods& other);
    // Retrieve a method from deperecated parameters
    // Reset the method if the parameter is false
    // Display a message anyway if warn is not false
    // Return true if the parameter was found
    bool getDeprecatedDtmfMethod(const NamedList& list, const char* param, int method, bool* warn);
    // Reset a method
    void reset(int method);
    // Build a string list from methods
    void buildMethods(String& buf, const char* sep = ",");
    bool hasMethod(int method) const;
    inline void printMethods(DebugEnabler* enabler, int level, const String& str) {
	    String tmp;
	    buildMethods(tmp);
	    Debug(enabler,level,"Built DTMF methods '%s' from '%s'",tmp.safe(),str.safe());
	}
    inline int operator[](unsigned int index) {
	    if (index < MethodCount)
		return m_methods[index];
	    return MethodCount;
	}
    inline DtmfMethods& operator=(const DtmfMethods& other) {
	    for (int i = 0; i < MethodCount; i++)
		m_methods[i] = other.m_methods[i];
	    return *this;
	}
    static const TokenDict s_methodName[];
protected:
    int m_methods[MethodCount];
};

class H323Driver : public Driver
{
public:
    enum Relays {
	Stop = Private,
    };
    H323Driver();
    virtual ~H323Driver();
    virtual void initialize();
    virtual bool hasLine(const String& line) const;
    virtual bool msgRoute(Message& msg);
    virtual bool msgExecute(Message& msg, String& dest);
    virtual void msgTimer(Message& msg);
    virtual bool received(Message &msg, int id);
    virtual void statusParams(String& str);
    void cleanup();
    YateH323EndPoint* findEndpoint(const String& ep);
    // Safely add an endpoint to the list
    void addEndPoint(YateH323EndPoint* ep);
    // Safely remove an endpoint from list
    void removeEndPoint(YateH323EndPoint* ep);
    // Create and initialize an endpoint
    bool initEndpoint(const String& name, const NamedList* params, bool fromConfig);
    bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    void msgStatus(Message& msg);
private:
    bool handleEngineStop(Message& msg);
    Mutex m_epMutex;
    ObjList m_endpoints;
};

H323Process* s_process = 0;
static H323Driver hplugin;

static String s_statusCmd = "status";

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
    YateH323EndPoint& m_endpoint;
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

class YateH323AudioConsumer : public DataConsumer, public Mutex, public PIndirectChannel
{
    PCLASSINFO(YateH323AudioConsumer, PIndirectChannel)
    YCLASS(YateH323AudioConsumer, DataConsumer)
public:
    YateH323AudioConsumer()
	: Mutex(false,"YateH323AudioConsumer"), m_exit(false)
	{ Debug(&hplugin,DebugAll,"YateH323AudioConsumer::YateH323AudioConsumer() [%p]",this); }
    ~YateH323AudioConsumer();
    virtual BOOL Close();
    virtual BOOL IsOpen() const;
    virtual BOOL Read(void *buf, PINDEX len);
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
private:
    PAdaptiveDelay readDelay;
    DataBlock m_buffer;
    bool m_exit;
};

class YateH323_ExternalRTPChannel;

class YateH323EndPoint : public String, public Mutex, public H323EndPoint
{
    PCLASSINFO(YateH323EndPoint, H323EndPoint)
    friend class YateGkRegThread;
    friend class YateH323Connection;     // Change connections counter
public:
    enum GkMode {
	ByAddr,
	ByName,
	Discover,
	Unregister
    };
    YateH323EndPoint(const NamedList* params = 0, const char* name = 0);
    ~YateH323EndPoint();
    // Make an outgoing call
    H323Connection* yateMakeCall(const PString& remoteParty, PString& token, void* userData);
    virtual H323Connection* CreateConnection(unsigned callReference, void* userData,
	H323Transport* transport, H323SignalPDU* setupPDU);
    virtual H235Authenticators CreateAuthenticators();
    bool Init(bool reg, const NamedList* params = 0);
    bool startGkClient(int mode, int retry = 0, const char* name = "");
    void stopGkClient(bool notify = true, const char* reason = 0, bool waitGk = true);
    void checkGkClient();
    void logout(const char* reason = "Unregistered", bool waitGk = true);
protected:
    virtual void OnRegistrationReject();
    bool initInternal(bool reg, const NamedList* params);
    void initTimeout(PTimeInterval& interval, const String& name,
	const NamedList& params, long minVal, long maxVal = 600000);
    void setCodecs();
    bool internalGkClient(YateGkRegThread* thread, int mode, const PString& name);
    void internalGkNotify(bool registered, const char* reason = 0, const char* error = 0);
    void stopListener();
    bool removeGk(bool wait = true);
    bool checkListener(const NamedList* params, bool& changed);
    // Check if a thread is valid
    bool validThread(YateGkRegThread* thread);
    // Start using the gatekeeper. Return false if wait period expired
    bool startUsingGk(bool changing);
    inline void stopUsingGk()
	{ m_gkInUse = false; }

    YateGatekeeperServer* m_gkServer;
    YateGkRegThread* m_thread;
    bool m_retry;                        // Keep calling internalGkClient()
    Mutex m_mutex;                       // Protect listener, client data, authenticators,
                                         // gatekeeper changes and connections counter
    unsigned int m_connCount;            // Number of connections
    ObjList m_authMethods;               // Authenticators to use
    bool m_authUseAll;                   // Use all authenticators created in base class
    // Listener
    bool m_listening;                    // Listener for incoming calls started
    String m_listenAddr;                 // Listener address
    int m_listenPort;                    // Listener port
    // Client
    bool m_client;                       // Client to gatekeeper
    bool m_registered;                   // Registered flag used to notify
    bool m_gkInUse;                      // We are currently setting/removing/using the gatekeeper
    String m_gkAddr;                     // Gatekeeper address
    int m_gkMode;                        // Gatekeeper mode
};

class YateH323Connection :  public H323Connection, public DebugEnabler
{
    PCLASSINFO(YateH323Connection, H323Connection)
    friend class YateH323Chan;
public:
    YateH323Connection(YateH323EndPoint& endpoint, H323Transport* transport, unsigned callReference, void* userdata);
    ~YateH323Connection();
    virtual H323Connection::AnswerCallResponse OnAnswerCall(const PString& caller,
	const H323SignalPDU& signalPDU, H323SignalPDU& connectPDU);
    virtual H323Connection::CallEndReason SendSignalSetup(const PString& alias, const H323TransportAddress& address);
    virtual void OnEstablished();
    virtual void OnCleared();
    virtual BOOL OnAlerting(const H323SignalPDU& alertingPDU, const PString& user);
    virtual BOOL OnReceivedProgress(const H323SignalPDU& pdu);
    virtual BOOL OnReceivedCapabilitySet(const H323Capabilities& remoteCaps,
	const H245_MultiplexCapability* muxCap, H245_TerminalCapabilitySetReject& reject);
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
    virtual BOOL OnCreateLogicalChannel(const H323Capability& capability, H323Channel::Directions dir, unsigned& errorCode);
    virtual BOOL OpenLogicalChannel(const H323Capability& capability, unsigned sessionID, H323Channel::Directions dir);
    virtual void CleanUpOnCallEnd();
    BOOL startExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan);
    void stoppedExternal(H323Channel::Directions dir);
    void setRemoteAddress(const char* remoteIP, WORD remotePort);
    void cleanups(bool closeChans = true, bool dropChan = true);
    void setCallerID(const char* number, const char* name);
    void rtpExecuted(Message& msg);
    void rtpForward(Message& msg, bool init = false);
    void updateFormats(const Message& msg);
    bool adjustCapabilities();
    void answerCall(AnswerCallResponse response, bool autoEarly = false);
    static BOOL decodeCapability(const H323Capability& capability, const char** dataFormat, int* payload = 0, String* capabName = 0);
    unsigned int fixQ931Cause() const;
    inline bool hasRemoteAddress() const
	{ return m_passtrough && (m_remotePort > 0); }
    inline bool nativeRtp() const
	{ return m_nativeRtp; }
    inline void rtpLocal()
	{ m_passtrough = false; }
    inline bool rtpStarted() const
	{ return m_rtpStarted; }
    inline const String& rtpId() const
	{ return m_rtpid; }
    inline int dtmfPayload() const
	{ return m_dtmfPayload; }
private:
    void setEpConn(bool created);
    // Retrieve RTP DTMF payload from local/remote caps. Return negative if not found
    int rtpDtmfPayload(bool local);
    String m_chanId;
    YateH323Chan* m_chan;
    Mutex* m_mutex;
    bool m_externalRtp;
    bool m_nativeRtp;
    bool m_passtrough;
    bool m_lockFormats;
    String m_formats;
    String m_rtpid;
    String m_rtpAddr;
    int m_rtpPort;
    String m_remoteFormats;
    String m_remoteAddr;
    int m_remotePort;
    bool m_needMedia;
    bool m_rtpStarted;
    int m_dtmfPayload;
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
    YateH323Chan(YateH323Connection* conn,Message* msg,const char* addr);
    ~YateH323Chan();
    PChannel* openAudioChannel(BOOL isEncoding);
    bool stopDataLinks();
    void hangup(bool dropChan = true, bool clearCall = true);
    void finish();

    virtual void zeroRefs();
    virtual void destroyed();
    virtual void disconnected(bool final, const char *reason);
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    virtual bool setDebug(Message& msg);
    void setAddress(const char* addr);
    inline void setTarget(const char* targetid)
	{ m_targetid = targetid; }
protected:
    virtual void endDisconnect(const Message& msg, bool handled);
private:
    // Send tone(s) using method
    bool sendTone(Message& msg, const char* tone, int meth, bool& retVal);

    YateH323Connection* m_conn;
    H323Connection::CallEndReason m_reason;
    bool m_hungup;
    DtmfMethods m_dtmfMethods;
    bool m_honorDtmfDetect;
};

class YateGkRegThread : public PThread
{
    PCLASSINFO(YateGkRegThread, PThread);
public:
    YateGkRegThread(YateH323EndPoint* endpoint, int mode, int retry = 0, const char* name = "")
	: PThread(256000),
	  m_orphan(false), m_ep(endpoint), m_mode(mode), m_retry(retry), m_name(name) {
	    Lock lck(s_mutexCount);
	    s_count++;
	}
    ~YateGkRegThread() {
	    Lock lck(s_mutexCount);
	    s_count--;
	}
    void Main();
    inline bool orphan() const
	{ return m_orphan; }
    inline void setOrphan()
	{ m_orphan = true; }
    static unsigned int s_count;
    static Mutex s_mutexCount;
protected:
    bool m_orphan;
    YateH323EndPoint* m_ep;
    int m_mode;
    int m_retry;
    PString m_name;
};

class YateCallThread : public PThread
{
    PCLASSINFO(YateCallThread, PThread);
public:
    YateCallThread(YateH323EndPoint* ep, const char* remoteParty, void* userData, int& status)
	: PThread(256000),
	  m_ep(ep), m_userData(userData), m_remoteParty(remoteParty), m_status(status)
	{ }
    virtual void Main();
    static bool makeCall(YateH323EndPoint* ep, const char* remoteParty, void* userData, bool newThread = false);
protected:
    YateH323EndPoint* m_ep;
    void* m_userData;
    PString m_remoteParty;
    int& m_status;
};

class UserHandler : public MessageHandler
{
public:
    UserHandler()
	: MessageHandler("user.login",140,hplugin.name())
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

unsigned int YateGkRegThread::s_count = 0;
Mutex YateGkRegThread::s_mutexCount(false,"H323GkThreads");
static DtmfMethods s_dtmfMethods;
static bool s_honorDtmfDetect = true;
// Deprecated dtmf params warn
static bool s_warnDtmfInbandCfg = true;
static bool s_warnDtmfInbandCallExecute = true;

const TokenDict DtmfMethods::s_methodName[] = {
    { "h323",     H323},
    { "rfc2833",  Rfc2833},
    { "inband",   Inband},
    { 0, 0 },
};

// Replace all methods from comma separated list
// If no method is set use other or setDefEmpty (reset to default)
bool DtmfMethods::set(const String& methods, const DtmfMethods* other, bool setDefEmpty,
    bool intersectOther)
{
    set();
    bool found = false;
    bool ok = true;
    ObjList* m = methods.split(',');
    int i = 0;
    for (ObjList* o = m->skipNull(); o && i < MethodCount; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	int meth = lookup(s->trimBlanks(),s_methodName,MethodCount);
	if (meth != MethodCount) {
	    m_methods[i++] = meth;
	    found = true;
	}
	else if (*s)
	    ok = false;
    }
    TelEngine::destruct(m);
    if (!found) {
	if (other) {
	    *this = *other;
	    intersectOther = false;
	}
	else if (setDefEmpty)
	    setDefault();
    }
    if (intersectOther && other)
	intersect(*other);
    return ok;
}

// Intersect with other methods
void DtmfMethods::intersect(const DtmfMethods& other)
{
    for (int i = 0; i < MethodCount; i++)
	if (m_methods[i] != MethodCount && !other.hasMethod(m_methods[i]))
	    m_methods[i] = MethodCount;
}

// Retrieve a method from deperecated parameters
// Reset the method if the parameter is false
// Display a message anyway if warn is not false
bool DtmfMethods::getDeprecatedDtmfMethod(const NamedList& list, const char* param,
    int method, bool* warn)
{
    String* p = list.getParam(param);
    if (!p)
	return false;
    if (!p->toBoolean())
	reset(method);
    if (warn && *warn) {
	*warn = false;
	Debug(&hplugin,DebugConf,"Deprecated '%s' in '%s'. Use 'dtmfmethods' instead!",param,list.c_str());
    }
    return true;
}

// Reset a method
void DtmfMethods::reset(int method)
{
    for (int i = 0; i < MethodCount; i++)
	if (m_methods[i] == method) {
	    m_methods[i] = MethodCount;
	    break;
	}
}

// Build a string list from methods
void DtmfMethods::buildMethods(String& buf, const char* sep)
{
    for (int i = 0; i < MethodCount; i++)
	buf.append(lookup(m_methods[i],s_methodName),sep);
}

bool DtmfMethods::hasMethod(int method) const
{
    for (int i = 0; i < MethodCount; i++)
	if (m_methods[i] == method)
	    return true;
    return false;
}


// Get a number of thread idle intervals from a time period
static inline unsigned int threadIdleIntervals(u_int64_t periodUs)
{
    u_int64_t us = TelEngine::Thread::idleUsec();
    return (unsigned int)((periodUs + us - 1) / us);
}

// Emit an alarm when refusing a new call but not more often than every 10s
static void congestedWarn(const char* msg)
{
    static uint64_t s_alarmTime = 0;
    if (s_alarmTime > Time::now())
	Debug(&hplugin,DebugWarn,"%s",msg);
    else {
	Alarm(&hplugin,"performance",DebugWarn,"%s",msg);
	s_alarmTime = Time::now() + 10000000;
    }
}

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

#ifndef DISABLE_CAPS_DUMP
#ifdef USE_CAPABILITY_FACTORY
static void ListRegisteredCaps(int level)
{
    PFactory<H323Capability>::KeyList_T list = PFactory<H323Capability>::GetKeyList();
#if (OPENH323_NUMVERSION >= 12000)
    for (PFactory<H323Capability>::KeyList_T::const_iterator find = list.begin(); find != list.end(); ++find)
      Debug(level,"Registed capability: '%s'",find->c_str());
#else
    for (std::vector<PString>::const_iterator find = list.begin(); find != list.end(); ++find)
	Debug(level,"Registed capability: '%s'",(const char*)*find);
#endif
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
#endif // DISABLE_CAPS_DUMP

YateGatekeeperServer::YateGatekeeperServer(YateH323EndPoint& ep)
    : H323GatekeeperServer(ep), m_endpoint(ep)
{
    Debug(&hplugin,DebugAll,"YateGatekeeperServer::YateGatekeeperServer() [%p]",this);
}

BOOL YateGatekeeperServer::Init()
{
    SetGatekeeperIdentifier("YATE gatekeeper");
    H323TransportAddressArray interfaces;
    const char* addr = 0;
    int i;
    // NOTE: no need to protect s_cfg: this is called from driver initialize()
    for (i = 1; (addr = s_cfg.getValue("gk",("interface" + String(i)).c_str())); i++) {
	const char* name = s_cfg.getValue("gk","name","YateGatekeeper");
	int port = s_cfg.getIntValue("gk","port",1719);
	H323TransportUDP* trans = new H323TransportUDP(m_endpoint,PIPSocket::Address(addr),port,0);
	if (AddListener(new H323GatekeeperListener(m_endpoint,*this,name,trans)))
	    Debug(&hplugin,DebugAll,"Started Gk listener on %s:%d",addr,port);
	else
	    Alarm(&hplugin,"config",DebugGoOn,"Can't start the Gk listener for address: %s",addr);
    }
    i = s_cfg.getIntValue("gk","ttl",600);
    if (i > 0) {
	// adjust time to live between 1 minute and 1 day
	if (i < 60)
	    i = 60;
	if (i > 86400)
	    i = 86400;
	SetTimeToLive(i);
    }
    disengageOnHearbeatFail = s_cfg.getBoolValue("gk","heartbeatdrop",true);
    canOnlyAnswerRegisteredEP = canOnlyCallRegisteredEP = s_cfg.getBoolValue("gk","registeredonly",false);
    return TRUE;
}


YateH323EndPoint::YateH323EndPoint(const NamedList* params, const char* name)
    : String(name), Mutex(false,"H323Endpoint"),
      m_gkServer(0), m_thread(0), m_retry(false), m_mutex(true,"H323Ep"),
      m_connCount(0), m_authUseAll(true), m_listening(false), m_listenPort(0),
      m_client(false), m_registered(false), m_gkInUse(false), m_gkMode(Unregister)
{
    Debug(&hplugin,DebugAll,"YateH323EndPoint::YateH323EndPoint(%p,\"%s\") [%p]",
	params,name,this);
    if (params && params->getBoolValue("gw",false))
	terminalType = e_GatewayOnly;
    // TODO: check if the following methods can be called on subsequent init
    // Move them to init if so
    setCodecs();
    AddAllUserInputCapabilities(0,1);
}

YateH323EndPoint::~YateH323EndPoint()
{
    Debug(&hplugin,DebugAll,"YateH323EndPoint::~YateH323EndPoint(\"%s\") [%p]",c_str(),this);
    hplugin.removeEndPoint(this);
    stopListener();
    ClearAllCalls(H323Connection::EndedByTemporaryFailure, true);
    if (m_gkServer)
	delete m_gkServer;
    stopGkClient(true,"Unregistered",false);
    removeGk(false);
    if (m_thread)
	Debug(DebugFail,"Destroying YateH323EndPoint '%s' still having a YateGkRegThread %p [%p]",
	    safe(),m_thread,this);
}

H323Connection* YateH323EndPoint::yateMakeCall(const PString& remoteParty,
    PString& token, void* userData)
{
    // Sync with gatekeeper changing flag
    if (!startUsingGk(false)) {
	congestedWarn("Refusing new outgoing H.323 call, gatekeeper busy");
	return 0;
    }
    token = PString::Empty();
    H323Connection* conn = InternalMakeCall(PString::Empty(),PString::Empty(),
	UINT_MAX,remoteParty,NULL,token,userData);
    if (conn != NULL)
        conn->Unlock();
    stopUsingGk();
    return conn;
}

H323Connection* YateH323EndPoint::CreateConnection(unsigned callReference,
    void* userData, H323Transport* transport, H323SignalPDU* setupPDU)
{
    // check if there aren't too many connections assigned to the cleaner thread
    if (cleaningBusy()) {
	congestedWarn("Refusing new H.323 call, too many cleaning up");
	return 0;
    }
    if (!hplugin.canAccept(userData == 0)) {
	congestedWarn("Refusing new H.323 call, full or exiting");
	return 0;
    }
    // Incoming call, sync with gatekeeper changing flag
    if (!userData && !startUsingGk(false)) {
	congestedWarn("Refusing new incoming H.323 call, gatekeeper busy");
	return 0;
    }
    Lock mylock(this);
    YateH323Connection* conn = new YateH323Connection(*this,transport,callReference,userData);
    mylock.drop();
    if (!userData)
	stopUsingGk();
    return conn;
}

H235Authenticators YateH323EndPoint::CreateAuthenticators()
{
    H235Authenticators e = H323EndPoint::CreateAuthenticators();
    Lock lck(m_mutex);
    ObjList* o = m_authMethods.skipNull();
    if (!o)
	return e;
    // Move authenticators to our list
    H235Authenticators ret;
    e.AllowDeleteObjects(false);
    for (; o; o = o->skipNext())
	for (PINDEX i = 0; i < e.GetSize(); i++)
	    if (o->get()->toString() == e[i].GetName()) {
		ret.Append(e.RemoveAt(i));
		break;
	    }
    while (m_authUseAll && e.GetSize())
	ret.Append(e.RemoveAt(0));
    e.AllowDeleteObjects(true);
#ifdef DEBUG
    String tmp;
    for (PINDEX i = 0; i < ret.GetSize(); i++)
	tmp.append(ret[i].GetName(),",");
    Debug(&hplugin,DebugAll,"Endpoint(%s) created authenticators '%s' [%p]",
	safe(),tmp.safe(),this);
#endif
    return ret;
}

bool YateH323EndPoint::Init(bool reg, const NamedList* params)
{
    bool ok = false;
    bool started = startUsingGk(true);
    if (started) {
	ok = initInternal(reg,params);
	stopUsingGk();
    }
    else if (m_client && reg && !m_registered)
	internalGkNotify(false,"Gatekeeper busy");
    if (!ok)
	Alarm(&hplugin,"config",DebugWarn,"Endpoint(%s) failed to init%s [%p]",safe(),
	    started ? "" : ": gatekeeper busy",this);
    return ok;
}

void YateH323EndPoint::OnRegistrationReject()
{
    Lock lck(m_mutex);
    m_retry = false;
    Debug(&hplugin,DebugNote,"Endpoint(%s) registration rejected [%p]",
	safe(),this);
    internalGkNotify(false,"Registration failed","noauth");
}

void YateH323EndPoint::initTimeout(PTimeInterval& interval, const String& name,
    const NamedList& params, long minVal, long maxVal)
{
    long int msec = params.getIntValue(name);
    if (msec <= 0)
	return;
    if (msec < minVal)
	msec = minVal;
    else if (msec > maxVal)
	msec = maxVal;
    interval = msec;
}

bool YateH323EndPoint::initInternal(bool reg, const NamedList* params)
{
    Lock lck(m_mutex);
    DDebug(&hplugin,DebugAll,"Endpoint(%s)::initInternal(%u,%p) [%p]",
	safe(),reg,params,this);
    DisableDetectInBandDTMF(!(params && params->getBoolValue("dtmfinband")));
    DisableFastStart(params && !params->getBoolValue("faststart",true));
    DisableH245Tunneling(params && !params->getBoolValue("h245tunneling",true));
    DisableH245inSetup(!(params && params->getBoolValue("h245insetup")));
    SetSilenceDetectionMode(static_cast<H323AudioCodec::SilenceDetectionMode>
	(params ? params->getIntValue("silencedetect",dict_silence,H323AudioCodec::NoSilenceDetection)
	: H323AudioCodec::NoSilenceDetection));
    if (params) {
	initTimeout(controlChannelStartTimeout,YSTRING("timeout_control"),*params,10000);
	initTimeout(signallingChannelCallTimeout,YSTRING("timeout_answer"),*params,5000);
	initTimeout(capabilityExchangeTimeout,YSTRING("timeout_capabilities"),*params,1000,120000);
    }
    // Init authenticators
    m_authMethods.clear();
    m_authUseAll = false;
    String meths = params ? (*params)["authmethods"] : String::empty();
    meths.trimBlanks();
    if (meths && meths != "*") {
	ObjList* list = meths.split(',',false);
	ObjList* o = list->skipNull();
	while (o) {
	    String* s = static_cast<String*>(o->get());
	    s->trimBlanks();
	    o = o->skipNext();
	    if (s->null())
		continue;
	    if (*s != "*")
		m_authMethods.append(new String(*s));
	    else
		m_authUseAll = (o == 0);
	}
	TelEngine::destruct(list);
    }
#ifdef DEBUG
    String tmpAuth;
    tmpAuth.append(m_authMethods,",");
    Debug(&hplugin,DebugAll,"Endpoint(%s) authmethods=%s useall=%s [%p]",
	safe(),tmpAuth.safe(),String::boolText(m_authUseAll),this);
#endif
    // Login ?
    if (((!params) || params->getBoolValue("ep",true)) && reg) {
	bool listenChanged = false;
	if (!checkListener(params,listenChanged))
	    return false;
	// Setup endpoint
	PString username("yate");
	if (params) {
	    username = params->getValue("username",(const char*)username);
	    username = params->getValue("alias",(const char*)username);
	}
	const char* server = params ? params->getValue("server") : 0;
	bool client = params && params->getBoolValue("gkclient",server != 0);
	if (m_client != client) {
	    m_client = client;
	    if (!m_client) {
		stopGkClient(true,"Unregistered",false);
		removeGk(false);
	    }
	}
	if (m_client) {
	    // adjust time to live between 1 minute and 1 day
	    int ttl = 300;
	    if (params) {
		ttl = params->getIntValue("interval",ttl,0,86400);
		// "gkttl" is deprecated
		ttl = params->getIntValue("gkttl",ttl,0,86400);
	    }
	    if (ttl > 0)
		registrationTimeToLive.SetInterval(0,ttl >= 60 ? ttl : 60);
	    PString pwd;
	    String d = server;
	    String a;
	    int gkPort = 0;
	    if (params) {
		pwd = params->getValue("password");
		d = params->getValue("gkip",server);
		a = params->getValue("gkname");
		gkPort = params->getIntValue("gkport");
	    }
	    String dummy;
	    String* serverAddr = &dummy;
	    int mode = Discover;
	    if (d) {
		mode = ByAddr;
		serverAddr = &d;
		// Fix port
		int pos = d.find(':');
		if (pos >= 0) {
		    if (d.substr(pos + 1).toInteger() <= 0) {
			d = d.substr(0,pos);
			if (gkPort > 0)
			    d << ":" << gkPort;
		    }
		}
		else if (gkPort > 0)
		    d << ":" << gkPort;
	    }
	    else if (a) {
		mode = ByName;
		serverAddr = &a;
	    }
	    bool doReg = !IsRegisteredWithGatekeeper();
	    bool authChanged = (username != GetLocalUserName()) || (pwd != GetGatekeeperPassword());
	    bool serverChanged = (mode != m_gkMode) || (*serverAddr != m_gkAddr);
	    if (doReg || listenChanged || authChanged || serverChanged) {
		stopGkClient(false,0,false);
		removeGk(false);
		Debug(&hplugin,DebugAll,"Endpoint(%s) registering [%p]",safe(),this);
		SetLocalUserName(username);
		SetGatekeeperPassword(pwd);
		int retry = params ? params->getIntValue("gkretry",60) : 60;
		if ((retry > 0) && (retry < 10))
		    retry = 10;
		startGkClient(mode,retry,serverAddr->safe());
	    }
	    else
		internalGkNotify(true,"Already registered");
	}
	else
	    SetLocalUserName(username);
    }
    else if (m_client && !reg)
	logout("Unregistered",false);

    // only the first, nameless endpoint can be a gatekeeper
    // No need to protect s_cfg: null endpoint is called from module initialize
    if ((!m_gkServer) && null() && s_cfg.getBoolValue("gk","server",false)) {
	m_gkServer = new YateGatekeeperServer(*this);
	m_gkServer->Init();
    }

    return true;
}

void YateH323EndPoint::setCodecs()
{
#ifndef DISABLE_CAPS_DUMP
    if (null()) {
	// NOTE: the method is called for null() from driver initialize()
	// No need to protect s_cfg
	int dump = s_cfg.getIntValue("general","dumpcodecs");
	if (dump > 0)
#ifdef USE_CAPABILITY_FACTORY
	    ListRegisteredCaps(dump);
#else
	    FakeH323CapabilityRegistration::ListRegistered(dump);
#endif
    }
#endif
    // Safely make a copy of codecs section
    s_cfgMutex.lock();
    NamedList* csect = s_cfg.getSection("codecs");
    if (!null()) {
	// fall back to global codec definitions if [codecs NAME] does not exist
	NamedList* tmp = s_cfg.getSection(String("codecs ") + c_str());
	if (tmp)
	    csect = tmp;
    }
    NamedList codecs("");
    if (csect)
	codecs = *csect;
    s_cfgMutex.unlock();
    bool defcodecs = codecs.getBoolValue("default",true);
    const char** f = h323_formats;
    String added;
    String failed;
    for (; *f; f += 2) {
	bool ok = false;
	bool fake = false;
	String tmp(codecs.getValue(f[1]));
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
		added.append(String((int)num)," ") << ": " << tmp;
	    else
		failed.append("'"," ") << tmp << "'";
	}
    }
    if (added)
	Debug(&hplugin,DebugAll,"Endpoint(%s) added capabilities %s [%p]",
	    safe(),added.safe(),this);
    // warn if codecs were disabled by default
    if (failed)
	Debug(&hplugin,defcodecs ? DebugInfo : DebugWarn,
	    "Endpoint(%s) failed to add capabilities %s [%p]",safe(),failed.safe(),this);
}

// Start a new PThread that performs GK discovery
bool YateH323EndPoint::startGkClient(int mode, int retry, const char* name)
{
    XDebug(&hplugin,DebugAll,"YateH323EndPoint::startGkClient(%d,%d,%s) [%p]",mode,retry,name,this);
    int retries = 10;
    hplugin.lock();
    while (m_thread) {
	hplugin.unlock();
	if (!--retries) {
	    Debug(&hplugin,DebugGoOn,
		"Endpoint(%s) old Gk client thread not finished [%p]",safe(),this);
	    return false;
	}
	Thread::msleep(25);
	hplugin.lock();
    }
    m_retry = true;
    m_thread = new YateGkRegThread(this,mode,retry,name);
    hplugin.unlock();
    m_thread->SetThreadName("Yate H323GkClient");
    m_thread->SetAutoDelete();
    m_thread->Resume();
    Lock lck(m_mutex);
    m_gkMode = mode;
    m_gkAddr = name;
    return true;
}

void YateH323EndPoint::stopGkClient(bool notify, const char* reason, bool waitGk)
{
#ifdef XDEBUG
    Debugger debug(DebugAll,"YateH323EndPoint::stopGkClient","(%s) [%p]",safe(),this);
#endif
    hplugin.lock();
    if (m_thread) {
	if (m_retry) {
	    m_retry = false;
	    unsigned int n = threadIdleIntervals(s_waitGkClient);
	    for (unsigned int i = 0; i < n; i++) {
		if (!m_thread)
		    break;
		hplugin.unlock();
		TelEngine::Thread::idle();
		hplugin.lock();
	    }
	}
	if (m_thread) {
	    Debug(&hplugin,DebugGoOn,"Endpoint(%s) abandoning old Gk client thread (%p) [%p]",
		safe(),m_thread,this);
	    m_thread->setOrphan();
	    m_thread = 0;
	    hplugin.unlock();
	    removeGk(waitGk);
	    hplugin.lock();
	}
    }
    hplugin.unlock();
    if (notify)
	internalGkNotify(false,reason);
}

bool YateH323EndPoint::internalGkClient(YateGkRegThread* thread, int mode, const PString& name)
{
    if (!(m_retry && validThread(thread)))
	return true;
    DDebug(&hplugin,DebugAll,"Endpoint(%s)::internalGkClient(%p,%d,%s) [%p]",
	safe(),thread,mode,(const char*)name,this);
    String reason;
    bool reg = (mode != Unregister);
    if (reg && !startUsingGk(true)) {
	if (m_retry && validThread(thread))
	    internalGkNotify(false,"Gatekeeper busy");
	return false;
    }
    bool ret = false;
    bool terminated = false;
    switch (mode) {
	case ByAddr:
	    ret = SetGatekeeper(name,new H323TransportUDP(*this));
	    if (!(m_retry && validThread(thread))) {
		ret = terminated = true;
		break;
	    }
	    if (ret)
		Debug(&hplugin,DebugCall,"Connected '%s' to GK addr '%s'",
		    safe(),(const char*)name);
	    else
		reason << "Failed to connect '" << safe() <<
		    "' to GK addr '" << (const char*)name << "'";
	    break;
	case ByName:
	    ret = LocateGatekeeper(name);
	    if (!(m_retry && validThread(thread))) {
		ret = terminated = true;
		break;
	    }
	    if (ret)
		Debug(&hplugin,DebugCall,"Connected '%s' to GK name '%s'",
		    safe(),(const char*)name);
	    else
		reason << "Failed to connect '" << safe() <<
		    "' to GK name '" << (const char*)name << "'";
	    break;
	case Discover:
	    ret = DiscoverGatekeeper(new H323TransportUDP(*this));
	    if (!(m_retry && validThread(thread))) {
		ret = terminated = true;
		break;
	    }
	    if (ret)
		Debug(&hplugin,DebugCall,"Connected '%s' to discovered GK",safe());
	    else
		reason << "Failed to discover a GK in '" << safe() << "'";
	    break;
	case Unregister:
	    if (validThread(thread)) {
		removeGk();
		stopListener();
	    }
	    if (validThread(thread)) {
		Debug(&hplugin,DebugCall,"Removed the GK in '%s'",safe());
		internalGkNotify(false,"Unregistered");
	    }
	    else
		terminated = true;
	    ret = true;
    }
    if (reg)
	stopUsingGk();
    if (reg && !terminated) {
	if (ret)
	    internalGkNotify(true);
	else {
	    if (reason)
		Debug(&hplugin,DebugWarn,"%s",reason.c_str());
	    if (m_retry)
		internalGkNotify(false,reason);
	    else
		ret = true;
	}
    }
    DDebug(&hplugin,DebugAll,"Endpoint(%s)::internalGkClient(%p,%d,%s) exiting with %u [%p]",
	safe(),thread,mode,(const char*)name,ret,this);
    return ret;
}

void YateH323EndPoint::internalGkNotify(bool registered, const char* reason, const char* error)
{
    if (((m_registered == registered) && !reason) || null())
	return;
    m_registered = registered;
    DDebug(&hplugin,DebugAll,"Endpoint(%s) notifying reg=%u reason=%s error=%s [%p]",
	safe(),m_registered,reason,error,this);
    Message* m = new Message("user.notify");
    m->addParam("account",*this);
    m->addParam("protocol","h323");
    m->addParam("registered",String::boolText(registered));
    m->addParam("error",error,false);
    m->addParam("reason",reason,false);
    Engine::enqueue(m);
}

void YateH323EndPoint::checkGkClient()
{
    if (!m_thread && m_mutex.lock(50000)) {
	if (!m_gkInUse)
	    internalGkNotify(IsRegisteredWithGatekeeper());
	m_mutex.unlock();
    }
}

void YateH323EndPoint::logout(const char* reason, bool waitGk)
{
    if (!m_client)
	return;
    Lock lck(m_mutex);
    stopListener();
    if (m_registered || IsRegisteredWithGatekeeper()) {
	stopGkClient(false,0,waitGk);
	startGkClient(Unregister);
    }
    else
	stopGkClient(true,reason,waitGk);
}

void YateH323EndPoint::stopListener()
{
    Lock lck(m_mutex);
    DDebug(&hplugin,DebugAll,"Endpoint(%s)::stopListener [%p]",safe(),this);
    RemoveListener(0);
    m_listening = false;
}

bool YateH323EndPoint::removeGk(bool wait)
{
    if (!gatekeeper)
	return true;
    DDebug(&hplugin,DebugAll,"Endpoint(%s)::removeGk [%p]",safe(),this);
    if (wait && !startUsingGk(true))
	return false;
    RemoveGatekeeper();
    if (wait)
	stopUsingGk();
    return true;
}

bool YateH323EndPoint::checkListener(const NamedList* params, bool& changed)
{
    changed = false;
    // Setup the listener if we don't have one or bind address changed
    int port = 1720;
    String a;
    if (params) {
	port = params->getIntValue("port",port);
	a = params->getValue("addr");
    }
    if (m_listening && m_listenAddr == a && m_listenPort == port)
	return true;
    // Remove old listener
    stopListener();
    m_listenAddr = a;
    m_listenPort = port;
    unsigned int retries = 5;
    int minPort = 1000;
    int maxPort = 65535;
    if (params) {
	retries = params->getIntValue("bind_retry_count",retries,0,10);
	minPort = params->getIntValue("bind_minport",minPort,1,65535);
	maxPort = params->getIntValue("bind_maxport",maxPort,1,65535);
    }
    DDebug(&hplugin,DebugAll,"Endpoint(%s) starting listener addr=%s port=%d [%p]",
	safe(),m_listenAddr.safe(),m_listenPort,this);
    PIPSocket::Address addr = INADDR_ANY;
    if (m_listenAddr)
	addr = m_listenAddr.c_str();
    port = m_listenPort;
    H323ListenerTCP* listener = new H323ListenerTCP(*this,addr,port);
    m_listening = StartListener(listener);
    if (!m_listening) {
	delete listener;
	if (retries) {
	    if (maxPort > minPort) {
		int diff = maxPort - minPort;
		if (diff < (int)retries)
		    retries = diff;
	    }
	    else {
		maxPort = minPort;
		retries = 1;
	    }
	}
	int level = DebugGoOn;
	String extra;
	if (retries) {
	    level = DebugWarn;
	    extra << "retry=" << retries;
	    extra << " ports [" << minPort << "-" << maxPort << "] ";
	}
	Debug(&hplugin,level,
	    "Endpoint(%s) unable to start H323 Listener on %s:%d %s[%p]",
	    safe(),(const char*)addr.AsString(),m_listenPort,extra.safe(),this);
	for (unsigned int i = 0; !m_listening && i < retries; i++) {
	    port = minPort;
	    if (minPort < maxPort)
		port += Random::random() % (maxPort - minPort);
	    listener = new H323ListenerTCP(*this,addr,port);
	    m_listening = StartListener(listener);
	    if (!m_listening)
		delete listener;
	}
    }
    if (m_listening) {
	changed = true;
	Debug(&hplugin,DebugAll,"Endpoint(%s) listener started on %s:%d [%p]",
	    safe(),(const char*)addr.AsString(),port,this);
	return true;
    }
    if (retries)
	Alarm(&hplugin,"config",DebugGoOn,"Endpoint(%s) unable to start H323 Listener on %s [%p]",
	    safe(),(const char*)addr.AsString(),this);
    String reason = "Cannot listen on ";
    reason << m_listenAddr << ":" << m_listenPort;
    stopGkClient(false);
    internalGkNotify(false,reason);
    return false;
}

// Start using the gatekeeper
// Return false if wait period expired
bool YateH323EndPoint::startUsingGk(bool changing)
{
    // Changing gatekeeper: wait for in use flag to be false and no more connections
    // Not changing (making a call): wait for in use flag to be false
    m_mutex.lock();
    bool ok = false;
    if ((changing && (m_gkInUse || m_connCount)) || (!changing && m_gkInUse)) {
	unsigned int n = threadIdleIntervals(changing ? s_waitGkUsageChanging : s_waitGkUsageCall);
	for (unsigned int i = 0; i < n; i++) {
	    if (!m_gkInUse && (!changing || (changing && !m_connCount))) {
		ok = true;
		break;
	    }
	    m_mutex.unlock();
	    Thread::idle();
	    m_mutex.lock();
	    if (Engine::exiting() || Thread::check(false))
		break;
	}
    }
    else
	ok = true;
    if (ok)
	m_gkInUse = true;
    m_mutex.unlock();
    return ok;
}

// Check if a thread is valid
bool YateH323EndPoint::validThread(YateGkRegThread* thread)
{
    return thread && m_thread == thread && !thread->orphan();
}


void YateGkRegThread::Main()
{
    if (!m_ep)
	return;
    // Avoid sleeping a large period of time: we might be requested to terminate
    unsigned int n = 0;
    unsigned int ms = 0;
    if (m_retry > 0) {
	ms = 50;
	n = (unsigned int)(m_retry * 1000) / ms;
    }
    String epName = m_ep->safe();
    Debug(&hplugin,DebugAll,
	"Endpoint(%s) client thread (%p) starting mode=%d name=%s [%p]",
	epName.safe(),this,m_mode,(const char*)m_name,m_ep);
    while (!orphan() && m_ep->m_retry && !m_ep->internalGkClient(this,m_mode,m_name) && ms) {
	for (unsigned int i = 0; i < n; i++) {
	    if (orphan() || !m_ep->m_retry)
		break;
	    Thread::msleep(ms);
	}
    }
    hplugin.lock();
    bool ok = (!orphan() && m_ep->m_thread == this);
    if (ok) {
	m_ep->m_thread = 0;
	m_ep->m_retry = false;
    }
    hplugin.unlock();
    Debug(&hplugin,ok ? DebugAll : DebugGoOn,
	"Endpoint(%s) client thread (%p) finished [%p]",
	epName.safe(),this,m_ep);
    m_ep = 0;
}


// make a call either normally or in a proxy PWlib thread
bool YateCallThread::makeCall(YateH323EndPoint* ep, const char* remoteParty, void* userData, bool newThread)
{
    // check if there aren't too many connections assigned to the cleaner thread
    if (cleaningBusy()) {
	congestedWarn("Refusing new outgoing H.323 call, too many cleaning up");
	return false;
    }
    if (!newThread) {
	PString token;
	return ep->yateMakeCall(remoteParty,token,userData) != 0;
    }
    int status = 0;
    YateCallThread* call = new YateCallThread(ep,remoteParty,userData,status);
    call->SetThreadName("Yate H323Call");
    call->SetNoAutoDelete();
    call->Resume();
    call->WaitForTermination();
    delete call;
    return status > 0;
}

// the actual method that does the job in the proxy thread
void YateCallThread::Main()
{
    PString token;
    if (m_ep->yateMakeCall(m_remoteParty,token,m_userData))
	m_status = 1;
    else
	m_status = -1;
}


YateH323Connection::YateH323Connection(YateH323EndPoint& endpoint,
    H323Transport* transport, unsigned callReference, void* userdata)
    : H323Connection(endpoint,callReference), m_chan(0), m_mutex(0),
      m_externalRtp(s_externalRtp), m_nativeRtp(false),
      m_passtrough(false), m_lockFormats(false),
      m_rtpPort(0), m_remotePort(0), m_needMedia(true),
      m_rtpStarted(false), m_dtmfPayload(-1)
{
    Debug(&hplugin,DebugAll,"YateH323Connection::YateH323Connection(%p,%u,%p) [%p]",
	&endpoint,callReference,userdata,this);
    s_mutex.lock();
    s_connCount++;
    s_mutex.unlock();
    setEpConn(true);
    m_needMedia = s_needMedia;

    // outgoing calls get the "call.execute" message as user data
    Message* msg = static_cast<Message*>(userdata);
    m_chan = new YateH323Chan(this,msg,
	((transport && !userdata) ? (const char*)transport->GetRemoteAddress() : 0));
    m_chan->initChan();
    m_chanId = m_chan->id();
    m_mutex = m_chan->mutex();
    debugCopy(m_chan);
    debugName(m_chanId);
    if (!msg) {
	m_passtrough = s_passtrough;
	return;
    }

    setCallerID(msg->getValue("caller"),msg->getValue("callername"));
    rtpForward(*msg,s_passtrough);
    updateFormats(*msg);
    m_needMedia = msg->getBoolValue("needmedia",m_needMedia);

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg->userData());
    if (ch && ch->connect(m_chan,msg->getValue("reason"))) {
	m_chan->callConnect(*msg);
	m_chan->setTarget(msg->getValue("id"));
	msg->setParam("peerid",m_chan->id());
	msg->setParam("targetid",m_chan->id());
	m_chan->deref();
    }
}

// Called by the cleaner thread after CleanUpOnCallEnd() and OnCleared()
YateH323Connection::~YateH323Connection()
{
    Debug(this,DebugAll,"YateH323Connection::~YateH323Connection() [%p]",this);
    s_mutex.lock();
    s_connCount--;
    s_mutex.unlock();
    setEpConn(false);
    YateH323Chan* tmp = m_chan;
    m_chan = 0;
    if (tmp)
	tmp->finish();
    cleanups();
    debugName(0);
}

// Called by the cleaner thread before OnCleared() and the destructor
void YateH323Connection::CleanUpOnCallEnd()
{
    Debug(this,DebugAll,"YateH323Connection::CleanUpOnCallEnd() [%p]",this);
    if (m_chan)
	m_chan->stopDataLinks();
    H323Connection::CleanUpOnCallEnd();
}

void YateH323Connection::cleanups(bool closeChans, bool dropChan)
{
    if (dropChan)
	m_chan = 0;
    if (closeChans && Lock()) {
	CloseAllLogicalChannels(true);
	CloseAllLogicalChannels(false);
	Unlock();
    }
}

H323Connection::AnswerCallResponse YateH323Connection::OnAnswerCall(const PString &caller,
    const H323SignalPDU &setupPDU, H323SignalPDU &connectPDU)
{
    Debug(this,DebugInfo,"YateH323Connection::OnAnswerCall caller='%s' chan=%p [%p]",
	(const char *)caller,m_chan,this);
    TelEngine::Lock lock(m_mutex);
    if (!(m_chan && m_chan->alive()))
	return H323Connection::AnswerCallDenied;
    if (!hplugin.canRoute()) {
	Debug(this,DebugWarn,"Not answering H.323 call, full or exiting");
	YateH323Chan* tmp = m_chan;
	m_chan = 0;
	tmp->hangup(false,false);
	tmp->deref();
	return H323Connection::AnswerCallDenied;
    }

    Message *m = m_chan->message("call.preroute",false,true);
    lock.drop();
    const YateH323EndPoint& ep = static_cast<const YateH323EndPoint&>(GetEndPoint());
    if (ep.c_str())
	m->setParam("in_line",ep.c_str());
    s_cfgMutex.lock();
    const char *s = s_cfg.getValue("incoming","context");
    if (s)
	m->setParam("context",s);
    s_cfgMutex.unlock();

    m->setParam("callername",caller);
    s = GetRemotePartyNumber();
    Debug(this,DebugInfo,"GetRemotePartyNumber()='%s'",s);
    m->setParam("caller",s ? s : (const char *)("h323/"+caller));

    const Q931& q931 = setupPDU.GetQ931();
    const H225_Setup_UUIE& setup = setupPDU.m_h323_uu_pdu.m_h323_message_body;
    const H225_ArrayOf_AliasAddress& adr = setup.m_destinationAddress;
    for (int i = 0; i<adr.GetSize(); i++)
	Debug(this,DebugAll,"adr[%d]='%s'",i,(const char *)H323GetAliasAddressString(adr[i]));
    String called;
    if (adr.GetSize() > 0)
	called = (const char *)H323GetAliasAddressString(adr[0]);
    if (called)
	Debug(this,DebugInfo,"Called number (alias) is '%s'",called.c_str());
    else {
	PString cal;
	if (q931.GetCalledPartyNumber(cal)) {
	    called=(const char *)cal;
	    Debug(this,DebugInfo,"Called-Party-Number (IE) is '%s'",called.c_str());
	}
    }
    if (called.null()) {
	Debug(this,DebugMild,"No called number present!");
	s_cfgMutex.lock();
	called = s_cfg.getValue("incoming","called");
	s_cfgMutex.unlock();
    }
    if (called)
	m->setParam("called",called);

#if 0
    s = GetRemotePartyAddress();
    Debug(this,DebugInfo,"GetRemotePartyAddress()='%s'",s);
    if (s)
	m->setParam("calledname",s);
#endif
    if (hasRemoteAddress()) {
	m->addParam("rtp_forward","possible");
	m->addParam("rtp_addr",m_remoteAddr);
	m->addParam("rtp_port",String(m_remotePort));
    }
    else if (m_passtrough) {
	Debug(this,DebugNote,"Disabling RTP forward because of slow start mode [%p]",this);
	m_passtrough = false;
    }
    if (m_remoteFormats)
	m->addParam("formats",m_remoteFormats);

    if (m_chan->startRouter(m))
	return H323Connection::AnswerCallDeferred;
    Debug(&hplugin,DebugWarn,"Error starting H.323 routing thread! [%p]",this);
    return H323Connection::AnswerCallDenied;
}

void YateH323Connection::rtpExecuted(Message& msg)
{
    Debug(this,DebugAll,"YateH323Connection::rtpExecuted(%p) [%p]",
	&msg,this);
    m_needMedia = msg.getBoolValue("needmedia",m_needMedia);
    if (!m_passtrough)
	return;
    String tmp = msg.getValue("rtp_forward");
    m_passtrough = (tmp == "accepted");
    if (m_passtrough)
	Debug(this,DebugInfo,"H323 Peer accepted RTP forward");
}

void YateH323Connection::rtpForward(Message& msg, bool init)
{
    Debug(this,DebugAll,"YateH323Connection::rtpForward(%p,%d) [%p]",
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
	Debug(this,DebugInfo,"Accepted RTP forward %s:%d formats '%s'",
	    addr.c_str(),port,m_formats.safe());
    }
    else {
	m_passtrough = false;
	Debug(this,DebugInfo,"Disabling RTP forward [%p]",this);
    }
}

// Update the formats when RTP is proxied
void YateH323Connection::updateFormats(const Message& msg)
{
    // when doing RTP forwarding formats are altered in rtpForward()
    if (m_passtrough || m_lockFormats)
	return;
    m_lockFormats = msg.getBoolValue("lock_formats");
    // only audio is currently supported
    const char* formats = msg.getValue("formats");
    if (!formats)
	return;
    if (m_formats != formats) {
	Debug(this,DebugNote,"Formats changed to '%s'",formats);
	m_formats = formats;
	// send changed capability set only if another was already sent
	if (adjustCapabilities() && capabilityExchangeProcedure->HasSentCapabilities())
	    SendCapabilitySet(FALSE);
    }
}

// Adjust local capabilities to not exceed the format list
bool YateH323Connection::adjustCapabilities()
{
    if (m_formats.null())
	return false;
    // remote has a list of supported codecs - remove unsupported capabilities
    bool nocodecs = true;
    bool changed = false;
    if (!Lock())
	return false;
    for (int i = 0; i < localCapabilities.GetSize(); i++) {
	const char* format = 0;
	String fname;
	decodeCapability(localCapabilities[i],&format,0,&fname);
	if (format) {
	    if (m_formats.find(format) < 0) {
		Debug(this,DebugAll,"Removing capability '%s' (%s) not in remote '%s'",
		    fname.c_str(),format,m_formats.c_str());
		changed = true;
		// also remove any matching fast start channels
		for (PINDEX idx = 0; idx < fastStartChannels.GetSize(); idx++) {
		    if (fastStartChannels[idx].GetCapability() == localCapabilities[i]) {
			Debug(this,DebugInfo,"Removing fast start channel %s '%s' (%s)",
			    lookup(fastStartChannels[idx].GetDirection(),dict_h323_dir,"?"),
			    fname.c_str(),format);
			fastStartChannels.RemoveAt(idx--);
		    }
		}
		localCapabilities.Remove(fname.c_str());
		i--;
	    }
	    else
		nocodecs = false;
	}
    }
    Unlock();
    if (nocodecs) {
	Debug(DebugWarn,"No codecs remaining for H323 connection [%p]",this);
	if (m_needMedia) {
	    changed = false;
	    ClearCall(EndedByCapabilityExchange);
	}
    }
    return changed;
}

void YateH323Connection::answerCall(AnswerCallResponse response, bool autoEarly)
{
    bool media = false;
    if (hasRemoteAddress() && m_rtpPort)
	media = true;
    else if (autoEarly) {
	TelEngine::Lock lock(m_mutex);
	if (m_chan && m_chan->alive() && m_chan->getPeer() && m_chan->getPeer()->getSource())
	    media = true;
    }
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

H323Connection::CallEndReason YateH323Connection::SendSignalSetup(const PString& alias, const H323TransportAddress& address)
{
    if (m_chan && m_chan->address().null())
	m_chan->setAddress(address);
    return H323Connection::SendSignalSetup(alias,address);
}

void YateH323Connection::OnEstablished()
{
    TelEngine::Lock lock(m_mutex);
    Debug(this,DebugInfo,"YateH323Connection::OnEstablished() [%p]",this);
    if (!m_chan)
	return;
    if (m_chan->address().null())
	m_chan->setAddress(GetControlChannel().GetRemoteAddress());
    if (HadAnsweredCall()) {
	m_chan->status("connected");
	return;
    }
    m_chan->status("answered");
    Message *m = m_chan->message("call.answered",false,true);
    lock.drop();
    if (m_passtrough) {
	if (m_remotePort) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_remoteAddr);
	    m->addParam("rtp_port",String(m_remotePort));
	    m->addParam("formats",m_remoteFormats);
	}
	else {
	    Debug(this,DebugWarn,"H323 RTP passtrough with no remote address! [%p]",this);
	    if (m_needMedia)
		ClearCall(EndedByCapabilityExchange);
	}
    }
    Engine::enqueue(m);
    if (!capabilityExchangeProcedure->HasReceivedCapabilities())
	capabilityExchangeProcedure->Start(TRUE);
}

// Called by the cleaner thread between CleanUpOnCallEnd() and the destructor
void YateH323Connection::OnCleared()
{
    int reason = GetCallEndReason();
    unsigned int q931 = fixQ931Cause();
    const char* rtext = CallEndReasonText(reason);
    const char* error = lookup(q931,q931_errors);
    if (!error)
	error = lookup(reason,dict_errors);
    Debug(this,DebugInfo,"YateH323Connection::OnCleared() error: '%s' reason: %s (%d) [%p]",
	error,rtext,reason,this);
    TelEngine::Lock lock(m_mutex);
    if (m_chan && m_chan->ref()) {
	YateH323Chan* tmp = m_chan;
	m_chan = 0;
	lock.drop();
	Channel::paramMutex().lock();
	if (q931)
	    tmp->parameters().setParam("cause_q931",String(q931));
	Channel::paramMutex().unlock();
	tmp->disconnect(error ? error : rtext,tmp->parameters());
	tmp->finish();
	tmp->deref();
    }
}

BOOL YateH323Connection::OnAlerting(const H323SignalPDU &alertingPDU, const PString &user)
{
    Debug(this,DebugInfo,"YateH323Connection::OnAlerting '%s' [%p]",(const char *)user,this);
    TelEngine::Lock lock(m_mutex);
    if (!m_chan)
	return FALSE;
    m_chan->status("ringing");
    Message *m = m_chan->message("call.ringing",false,true);
    lock.drop();
    if (hasRemoteAddress()) {
	m->addParam("rtp_forward","yes");
	m->addParam("rtp_addr",m_remoteAddr);
	m->addParam("rtp_port",String(m_remotePort));
	m->addParam("formats",m_remoteFormats);
    }
    Engine::enqueue(m);
    return TRUE;
}

BOOL YateH323Connection::OnReceivedProgress(const H323SignalPDU& pdu)
{
    Debug(this,DebugInfo,"YateH323Connection::OnReceivedProgress [%p]",this);
    if (!H323Connection::OnReceivedProgress(pdu))
	return FALSE;
    TelEngine::Lock lock(m_mutex);
    if (!m_chan)
	return FALSE;
    m_chan->status("progressing");
    Message *m = m_chan->message("call.progress",false,true);
    lock.drop();
    if (hasRemoteAddress()) {
	m->addParam("rtp_forward","yes");
	m->addParam("rtp_addr",m_remoteAddr);
	m->addParam("rtp_port",String(m_remotePort));
	m->addParam("formats",m_remoteFormats);
    }
    Engine::enqueue(m);
    return TRUE;
}

BOOL YateH323Connection::OnReceivedCapabilitySet(const H323Capabilities& remoteCaps,
    const H245_MultiplexCapability* muxCap, H245_TerminalCapabilitySetReject& reject)
{
    DDebug(this,DebugInfo,"YateH323Connection::OnReceivedCapabilitySet [%p]",this);
    bool ok = H323Connection::OnReceivedCapabilitySet(remoteCaps,muxCap,reject);
    int payload = rtpDtmfPayload(false);
    if (m_dtmfPayload != payload) {
	if (m_rtpStarted) {
	    // TODO: Update external rtp event payload when implemented
	    if (payload > 0)
		Debug(this,DebugInfo,"Unable to change event payload, disabling RFC 2833 [%p]",this);
	    m_dtmfPayload = -3;
	}
	else
	    m_dtmfPayload = payload;
    }
    return ok;
}

void YateH323Connection::OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
    Debug(this,DebugInfo,"YateH323Connection::OnUserInputTone '%c' duration=%u [%p]",tone,duration,this);
    TelEngine::Lock lock(m_mutex);
    if (!m_chan)
	return;
    Message *m = m_chan->message("chan.dtmf",false,true);
    lock.drop();
    char buf[2];
    buf[0] = tone;
    buf[1] = 0;
    m->addParam("text",buf);
    m->addParam("duration",String(duration));
    m->addParam("detected","h323");
    m_chan->dtmfEnqueue(m);
}

void YateH323Connection::OnUserInputString(const PString &value)
{
    Debug(this,DebugInfo,"YateH323Connection::OnUserInputString '%s' [%p]",(const char *)value,this);
    TelEngine::Lock lock(m_mutex);
    if (!m_chan)
	return;
    String text((const char *)value);
    const char *type = text.startSkip("MSG",false) ? "chan.text" : "chan.dtmf";
    Message *m = m_chan->message(type,false,true);
    lock.drop();
    m->addParam("text",text);
    Engine::enqueue(m);
}

BOOL YateH323Connection::OpenAudioChannel(BOOL isEncoding, unsigned bufferSize,
    H323AudioCodec &codec)
{
    Debug(this,DebugInfo,"YateH323Connection::OpenAudioChannel chan=%p [%p]",m_chan,this);
    if (!m_nativeRtp) {
	Debug(DebugGoOn,"YateH323Connection::OpenAudioChannel for non-native RTP in [%p]",this);
	if (m_needMedia)
	    ClearCall(EndedByCapabilityExchange);
	return FALSE;
    }
    PChannel* achan = 0;
    TelEngine::Lock lock(m_mutex);
    if (m_chan && m_chan->alive())
	achan = m_chan->openAudioChannel(isEncoding);
    lock.drop();
    return achan && codec.AttachChannel(achan,false);
}

#ifdef NEED_RTP_QOS_PARAM
H323Channel* YateH323Connection::CreateRealTimeLogicalChannel(const H323Capability& capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters* param,RTP_QOS* rtpqos)
#else
H323Channel* YateH323Connection::CreateRealTimeLogicalChannel(const H323Capability& capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters* param)
#endif
{
    Debug(this,DebugAll,"H323Connection::CreateRealTimeLogicalChannel%s%s [%p]",
	m_externalRtp ? " external" : "",m_passtrough ? " passtrough" : "",this);
    if (m_externalRtp || m_passtrough) {
	const char* sdir = lookup(dir,dict_h323_dir);
	const char *format = 0;
	decodeCapability(capability,&format);
	Debug(this,DebugAll,"Capability '%s' format '%s' session %u %s",
	    (const char *)capability.GetFormatName(),format,sessionID,sdir);

	// disallow codecs not supported by remote receiver
	if (m_passtrough && !(m_formats.null() || (m_formats.find(format) >= 0))) {
	    Debug(this,DebugMild,"Refusing to create '%s' not in remote '%s'",format,m_formats.c_str());
	    return 0;
	}

	if (dir == H323Channel::IsReceiver) {
	    if (format && (m_remoteFormats.find(format) < 0)) {
		TelEngine::Lock lck(s_cfgMutex);
		if (s_cfg.getBoolValue("codecs",format,true))
		    m_remoteFormats.append(format,",");
	    }
	}
	PIPSocket::Address externalIpAddress;
	GetControlChannel().GetLocalAddress().GetIpAddress(externalIpAddress);
	Debug(this,DebugAll,"Logical control channel address '%s'",
	    (const char *)externalIpAddress.AsString());
	WORD externalPort = 0;
	if (!m_passtrough) {
	    TelEngine::Lock lock(m_mutex);
	    if (m_chan && m_chan->alive()) {
		Message m("chan.rtp");
		m.userData(m_chan);
		lock.drop();
		m.addParam("localip",externalIpAddress.AsString());
		if (sdir)
		    m.addParam("direction",sdir);
		if (Engine::dispatch(m)) {
		    m_rtpid = m.getValue("rtpid");
		    externalPort = m.getIntValue("localport");
		}
	    }
	    else {
		Debug(this,DebugNote,"Not creating logical channel for a dead channel [%p]",this);
		return 0;
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
	if (s_fallbackRtp)
	    Debug(this,DebugWarn,"YateH323Connection falling back to native RTP [%p]",this);
	else {
	    Debug(this,DebugWarn,"YateH323Connection RTP failed but not falling back! [%p]",this);
	    return 0;
	}
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
    Debug(this,DebugAll,"YateH323Connection::OnSetLocalCapabilities()%s%s [%p]",
	m_externalRtp ? " external" : "",m_passtrough ? " passtrough" : "",this);
    H323Connection::OnSetLocalCapabilities();
    adjustCapabilities();
}

BOOL YateH323Connection::OnStartLogicalChannel(H323Channel & channel)
{
    DDebug(this,DebugInfo,"YateH323Connection::OnStartLogicalChannel(%p) [%p]",&channel,this);
    if (!(m_chan && m_chan->alive()))
	return FALSE;
    return m_nativeRtp ? H323Connection::OnStartLogicalChannel(channel) : TRUE;
}

BOOL YateH323Connection::OnCreateLogicalChannel(const H323Capability& capability, H323Channel::Directions dir, unsigned& errorCode)
{
    DDebug(this,DebugInfo,"YateH323Connection::OnCreateLogicalChannel('%s',%s) [%p]",
	(const char *)capability.GetFormatName(),lookup(dir,dict_h323_dir),this);
    return H323Connection::OnCreateLogicalChannel(capability,dir,errorCode);
}

BOOL YateH323Connection::OpenLogicalChannel(const H323Capability& capability, unsigned sessionID, H323Channel::Directions dir)
{
    DDebug(this,DebugInfo,"YateH323Connection::OpenLogicalChannel('%s',%u,%s) [%p]",
	(const char *)capability.GetFormatName(),sessionID,lookup(dir,dict_h323_dir),this);
    if (!(m_chan && m_chan->alive()))
	return FALSE;
    return H323Connection::OpenLogicalChannel(capability,sessionID,dir);
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
	Debug(this,DebugInfo,"Got remote RTP address %s:%u [%p]",
	    remoteIP,remotePort,this);
	m_remotePort = remotePort;
	m_remoteAddr = remoteIP;
    }
}

BOOL YateH323Connection::startExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan)
{
    const char* sdir = lookup(dir,dict_h323_dir);
    Debug(this,DebugAll,"YateH323Connection::startExternalRTP(\"%s\",%u,%s,%p) [%p]",
	remoteIP,remotePort,sdir,chan,this);
    int payload = 128;
    const char *format = 0;
    decodeCapability(chan->GetCapability(),&format,&payload);
    if (format && m_formats && (m_formats.find(format) < 0)) {
	Debug(this,DebugNote,"Refusing RTP '%s' payload %d, not in '%s'",
	    format,payload,m_formats.c_str());
	return FALSE;
    }
    if (m_passtrough && m_rtpPort) {
	setRemoteAddress(remoteIP,remotePort);

	Debug(this,DebugInfo,"Passing RTP to %s:%d",m_rtpAddr.c_str(),m_rtpPort);
	const PIPSocket::Address ip(m_rtpAddr.safe());
	WORD dataPort = m_rtpPort;
	chan->SetExternalAddress(H323TransportAddress(ip, dataPort), H323TransportAddress(ip, dataPort+1));
	stoppedExternal(dir);
	return TRUE;
    }
    if (!m_externalRtp)
	return FALSE;
    if (m_dtmfPayload < 0)
	m_dtmfPayload = rtpDtmfPayload(true);
    Message m("chan.rtp");
    if (m_rtpid)
	m.setParam("rtpid",m_rtpid);
    if (sdir)
	m.addParam("direction",sdir);
    m.addParam("remoteip",remoteIP);
    m.addParam("remoteport",String(remotePort));
    if (format)
	m.addParam("format",format);
    if ((payload >= 0) && (payload < 127))
	m.addParam("payload",String(payload));
    if (m_dtmfPayload > 0)
	m.addParam("evpayload",String(m_dtmfPayload));

    TelEngine::Lock lock(m_mutex);
    if (!(m_chan && m_chan->alive() && m_chan->driver()))
	return FALSE;
    m.userData(m_chan);
    lock.drop();
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	m_rtpStarted = true;
	return TRUE;
    }
    return FALSE;
}

void YateH323Connection::stoppedExternal(H323Channel::Directions dir)
{
    Debug(this,DebugInfo,"YateH323Connection::stoppedExternal(%s) chan=%p [%p]",
	lookup(dir,dict_h323_dir),m_chan,this);
    TelEngine::Lock lock(m_mutex);
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

void YateH323Connection::setEpConn(bool created)
{
    YateH323EndPoint* ep = static_cast<YateH323EndPoint*>(&endpoint);
    TelEngine::Lock lck(ep->m_mutex);
    if (created)
	ep->m_connCount++;
    else
	ep->m_connCount--;
}

// Retrieve RTP DTMF payload from local/remote caps
int YateH323Connection::rtpDtmfPayload(bool local)
{
    int payload = -1;
    const H323Capabilities& caps = local ? GetLocalCapabilities() : GetRemoteCapabilities();
    // NOTE: RFC2833 capability subtype is not set to H323_UserInputCapability::SignalToneRFC2833 in the library
    //       It is set to 10000
    H323Capability* cap = caps.FindCapability(H323Capability::e_UserInput,10000);
    if (cap) {
	payload = cap->GetPayloadType();
	if (payload < 96 || payload > 127)
	    payload = -2;
    }
    XDebug(this,DebugNote,"rtpDtmfPayload(%u) %d [%p]",local,payload,this);
    return payload;
}

// Return a proper Q.931 / Q.850 cause code, zero if unknown / unsupported
unsigned int YateH323Connection::fixQ931Cause() const
{
    unsigned int q931 = GetQ931Cause();
    if (1 <= q931 && q931 <= 127)
	return q931;
    // let's guess...
    switch (GetCallEndReason()) {
	case EndedByNoAnswer:
	    return Q931::NoAnswer;
	default:
	    return 0;
    }
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
    DDebug(m_conn,DebugAll,"YateH323_ExternalRTPChannel::YateH323_ExternalRTPChannel %s addr=%s:%u [%p]",
	lookup(GetDirection(),dict_h323_dir), (const char *)ip.AsString(), dataPort,this);
    SetExternalAddress(H323TransportAddress(ip, dataPort), H323TransportAddress(ip, dataPort+1));
}

YateH323_ExternalRTPChannel::~YateH323_ExternalRTPChannel()
{
    DDebug(m_conn,DebugInfo,"YateH323_ExternalRTPChannel::~YateH323_ExternalRTPChannel %s%s [%p]",
	lookup(GetDirection(),dict_h323_dir),(isRunning ? " running" : ""),this);
    if (isRunning) {
	isRunning = FALSE;
	if (m_conn)
	    m_conn->stoppedExternal(GetDirection());
    }
}

BOOL YateH323_ExternalRTPChannel::Start()
{
    DDebug(m_conn,DebugAll,"YateH323_ExternalRTPChannel::Start() [%p]",this);
    if (!(m_conn && H323_ExternalRTPChannel::Start()))
	return FALSE;

    PIPSocket::Address remoteIpAddress;
    WORD remotePort;
    GetRemoteAddress(remoteIpAddress,remotePort);
    Debug(&hplugin,DebugInfo,"External RTP address %s:%u",(const char *)remoteIpAddress.AsString(),remotePort);

    return isRunning = m_conn->startExternalRTP((const char *)remoteIpAddress.AsString(), remotePort, GetDirection(), this);
}

BOOL YateH323_ExternalRTPChannel::OnReceivedPDU(
				const H245_H2250LogicalChannelParameters& param,
				unsigned& errorCode)
{
    Debug(m_conn,DebugAll,"YateH323_ExternalRTPChannel::OnReceivedPDU [%p]",this);
    if (!H323_ExternalRTPChannel::OnReceivedPDU(param,errorCode))
	return FALSE;
    if (!m_conn || m_conn->hasRemoteAddress())
	return TRUE;
    PIPSocket::Address remoteIpAddress;
    WORD remotePort;
    GetRemoteAddress(remoteIpAddress,remotePort);
    Debug(&hplugin,DebugAll,"Remote RTP address %s:%u",(const char *)remoteIpAddress.AsString(),remotePort);
    m_conn->setRemoteAddress((const char *)remoteIpAddress.AsString(), remotePort);
    return TRUE;
}

BOOL YateH323_ExternalRTPChannel::OnSendingPDU(H245_H2250LogicalChannelParameters& param)
{
    Debug(m_conn,DebugAll,"YateH323_ExternalRTPChannel::OnSendingPDU [%p]",this);
    return H323_ExternalRTPChannel::OnSendingPDU(param);
}

BOOL YateH323_ExternalRTPChannel::OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters& param)
{
    Debug(m_conn,DebugAll,"YateH323_ExternalRTPChannel::OnReceivedAckPDU [%p]",this);
    return H323_ExternalRTPChannel::OnReceivedAckPDU(param);
}

void YateH323_ExternalRTPChannel::OnSendOpenAck(H245_H2250LogicalChannelAckParameters& param)
{
    Debug(m_conn,DebugAll,"YateH323_ExternalRTPChannel::OnSendOpenAck [%p]",this);
    H323_ExternalRTPChannel::OnSendOpenAck(param);
}

YateH323AudioSource::~YateH323AudioSource()
{
    DDebug(&hplugin,DebugAll,"YateH323AudioSource::~YateH323AudioSource() [%p]",this);
    m_exit = true;
    // Delay actual destruction until the mutex is released
    lock();
    m_data.clear(false);
    unlock();
}

YateH323AudioConsumer::~YateH323AudioConsumer()
{
    DDebug(&hplugin,DebugAll,"YateH323AudioConsumer::~YateH323AudioConsumer() [%p]",this);
    m_exit = true;
    // Delay actual destruction until the mutex is released
    check();
}

BOOL YateH323AudioConsumer::Close()
{
    DDebug(&hplugin,DebugAll,"YateH323AudioConsumer::Close() [%p]",this);
    m_exit = true;
    return true;
}

BOOL YateH323AudioConsumer::IsOpen() const
{
    return !m_exit;
}

unsigned long YateH323AudioConsumer::Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags)
{
    if (m_exit)
	return 0;
    Lock lock(this);
    if ((m_buffer.length() + data.length()) <= (480*5)) {
	m_buffer += data;
	return invalidStamp();
    }
#ifdef DEBUG
    else
	Debug(&hplugin,DebugAll,"Consumer skipped %u bytes, buffer is full [%p]",data.length(),this);
#endif
    return 0;
}

BOOL YateH323AudioConsumer::Read(void *buf, PINDEX len)
{
    readDelay.Delay(len/16);
    while (!m_exit) {
	Lock lock(this);
	if (!getConnSource()) {
	    ::memset(buf,0,len);
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
	    m_buffer.cut(-len);
	    XDebug(&hplugin,DebugAll,"Consumer pulled %d bytes from buffer [%p]",len,this);
	    break;
	}
	else {
	    len = 0;
	    Thread::yield();
	}
    }
    lastReadCount = len;
    return (len != 0);
}

BOOL YateH323AudioSource::Close()
{
    DDebug(&hplugin,DebugAll,"YateH323AudioSource::Close() [%p]",this);
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
	Forward(m_data);
	m_data.clear(false);
	writeDelay.Delay(len/16);
    }
    lastWriteCount = len;
    return true;
}


BOOL YateGatekeeperServer::GetUsersPassword(const PString& alias, PString& password) const
{
    Message m("user.auth");
    m.addParam("protocol","h323");
    m.addParam("username",alias);
    m.addParam("endpoint",m_endpoint);
    m.addParam("gatekeeper",GetGatekeeperIdentifier());
    if (!Engine::dispatch(m))
	return FALSE;
    // as usual empty password means authenticated
    password = m.retValue().c_str();
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
	    for (int k = 0; k < request.rrq.m_callSignalAddress.GetSize(); k++) {
		ip = request.rrq.m_callSignalAddress[k];
		// search for the first address that is not localhost (127.*)
		if (ip.m_ip[0] != 127)
		    break;
	    }
	    ips = "h323/";
	    if (!alias.IsEmpty())
		ips << (const char*)alias << "@";
	    ips << ip.m_ip[0] << "." << ip.m_ip[1] << "." << ip.m_ip[2] << "." << ip.m_ip[3] << ":" << (int)ip.m_port;

	    Message m("user.register");
	    m.addParam("username",alias);
	    m.addParam("driver","h323");
	    m.addParam("data",ips);
	    ips = GetTimeToLive();
	    m.addParam("expires",ips);
	    if (Engine::dispatch(m))
		return H323GatekeeperRequest::Confirm;
	}
	return H323GatekeeperRequest::Reject;
    }
    return (H323Transaction::Response)i;
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
	    Message m("user.unregister");
	    m.addParam("username",alias);
	    if (Engine::dispatch(m))
		return H323GatekeeperRequest::Confirm;
	}
    }
    return (H323Transaction::Response)i;
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
	    if ((m.getParam("driver")) && (*(m.getParam("driver")) == "h323")) {
		s >> "/";
		address = s.c_str();
	    }
	    else {
		s.clear();
		s_cfgMutex.lock();
		s << "ip$" << s_cfg.getValue("gk","interface1") << ":" << s_cfg.getIntValue("ep","port",1720);
		s_cfgMutex.unlock();
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
	if (!name) {
	    TelEngine::Lock lck(s_cfgMutex);
	    display << number << " [" << s_cfg.getValue("ep","ident","yate") << "]";
	}
	else if (isE164(name))
	    display << number << " [" << name << "]";
	else
	    display = name;
	Debug(this,DebugInfo,"Setting H.323 caller: number='%s' name='%s'",number,display.c_str());
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
	Debug(this,DebugInfo,"Setting H.323 caller: name='%s'",display.c_str());
	SetLocalPartyName(display.c_str());
    }
}

YateH323Chan::YateH323Chan(YateH323Connection* conn,Message* msg,const char* addr)
    : Channel(hplugin,0,(msg != 0)),
      m_conn(conn), m_reason(H323Connection::EndedByLocalUser),
      m_hungup(false),
      m_honorDtmfDetect(s_honorDtmfDetect)
{
    s_mutex.lock();
    s_chanCount++;
    s_mutex.unlock();
    setAddress(addr);
    Debug(this,DebugAll,"YateH323Chan::YateH323Chan(%p,%s) %s [%p]",
	conn,addr,direction(),this);
    setMaxcall(msg);
    if (msg)
	setMaxPDD(*msg);
    Message* s = message("chan.startup",msg);
    s_cfgMutex.lock();
    m_dtmfMethods = s_dtmfMethods;
    s_cfgMutex.unlock();
    if (msg) {
	String* meths = msg->getParam("odtmfmethods");
	if (meths) {
	    DtmfMethods old = m_dtmfMethods;
	    m_dtmfMethods.set(*meths,&old);
	}
	else
	    m_dtmfMethods.getDeprecatedDtmfMethod(*msg,"dtmfinband",DtmfMethods::Inband,&s_warnDtmfInbandCallExecute);
	s->copyParams(*msg,"caller,callername,called,billid,callto,username");
	m_honorDtmfDetect = msg->getBoolValue(YSTRING("ohonor_dtmf_detect"),m_honorDtmfDetect);
    }
    Engine::enqueue(s);
}

YateH323Chan::~YateH323Chan()
{
    Debug(this,DebugAll,"YateH323Chan::~YateH323Chan() %s %s [%p]",
	m_status.c_str(),id().c_str(),this);
    s_mutex.lock();
    s_chanCount--;
    s_mutex.unlock();
    if (m_conn)
	Debug(this,DebugFail,"Still having a connection %p [%p]",m_conn,this);
}

void YateH323Chan::zeroRefs()
{
    DDebug(this,DebugAll,"YateH323Chan::zeroRefs() conn=%p [%p]",m_conn,this);
    if (m_conn && m_conn->nativeRtp() && stopDataLinks()) {
	DDebug(this,DebugInfo,"YateH323Chan postpones destruction (native RTP) [%p]",this);
	// let the OpenH323 cleaner thread to do the cleanups so we don't have
	//  to block until the native data threads terminate
	dropChan();
	hangup(false);
	cleanup();
	return;
    }
    Channel::zeroRefs();
}

void YateH323Chan::destroyed()
{
    DDebug(this,DebugAll,"YateH323Chan::destroyed() [%p]",this);
    dropChan();
    stopDataLinks();
    if (m_conn)
	m_conn->cleanups();
    hangup();
    Channel::destroyed();
}

void YateH323Chan::finish()
{
    DDebug(this,DebugAll,"YateH323Chan::finish() [%p]",this);
    m_conn = 0;
    if (m_hungup)
	Channel::zeroRefs();
    else {
	hangup();
	disconnect();
    }
}

void YateH323Chan::hangup(bool dropChan, bool clearCall)
{
    DDebug(this,DebugAll,"YateH323Chan::hangup() [%p]",this);
    if (m_hungup)
	return;
    m_hungup = true;
    Message *m = message("chan.hangup");
    YateH323Connection* tmp = m_conn;
    m_conn = 0;
    if (clearCall && tmp) {
	const char* err = 0;
	H323Connection::CallEndReason reason = tmp->GetCallEndReason();
	if (reason == H323Connection::NumCallEndReasons)
	    reason = m_reason;
	else
	    err = lookup(tmp->fixQ931Cause(),q931_errors);
	if (!err)
	    err = lookup(reason,dict_errors);
	const char* txt = CallEndReasonText(reason);
	if (err)
	    m->setParam("error",err);
	if (txt)
	    m->setParam("reason",txt);
	tmp->cleanups(false,dropChan);
	tmp->ClearCall(reason);
    }
    Engine::enqueue(m);
}

void YateH323Chan::disconnected(bool final, const char *reason)
{
    Debugger debug("YateH323Chan::disconnected()"," '%s' [%p]",reason,this);
    m_reason = (H323Connection::CallEndReason)lookup(reason,dict_errors,H323Connection::EndedByLocalUser);
    Channel::disconnected(final,reason);
    if (!final)
	return;
    stopDataLinks();
    if (m_conn)
	m_conn->ClearCall(m_reason);
}

void YateH323Chan::endDisconnect(const Message& msg, bool handled)
{
    const String* p = msg.getParam(YSTRING("reason"));
    if (!TelEngine::null(p))
	m_reason = (H323Connection::CallEndReason)p->toInteger(dict_errors,m_reason);
#if (OPENH323_NUMVERSION >= 11704)
    p = msg.getParam(YSTRING("cause_q931"));
    if (m_conn && !TelEngine::null(p)) {
	int i = p->toInteger(q931_errors);
	if (i > 0)
	    m_conn->SetQ931Cause(i);
    }
#endif
}

// Send tone(s) using method
bool YateH323Chan::sendTone(Message& msg, const char* tone, int meth, bool& retVal)
{
    if (!(m_conn && tone))
	return false;
    bool ok = false;
    if (meth == DtmfMethods::H323) {
	const char* t = tone;
	while (*t)
	    m_conn->SendUserInputTone(*t++);
	retVal = true;
	ok = true;
    }
    else if (meth == DtmfMethods::Rfc2833) {
	ok = m_conn->rtpStarted() && m_conn->rtpId() && m_conn->dtmfPayload() > 0;
	if (ok)
	    msg.setParam("targetid",m_conn->rtpId());
    }
    else if (meth == DtmfMethods::Inband) {
	ok = dtmfInband(tone);
	retVal = ok;
    }
    XDebug(this,ok ? DebugAll : DebugNote,"sendTone(%s) meth=%s (%d) ok=%u [%p]",
	tone,lookup(meth,DtmfMethods::s_methodName),meth,ok,this);
    return ok;
}

// Set the signalling address
void YateH323Chan::setAddress(const char* addr)
{
    m_address = addr;
    m_address.startSkip("ip$",false);
    filterDebug(m_address);
}

// Shut down the data transfers so OpenH323 can stop its related threads
bool YateH323Chan::stopDataLinks()
{
    DDebug(this,DebugAll,"YateH323Chan::stopDataLinks() [%p]",this);
    Lock lock(m_mutex);
    bool pending = false;
    YateH323AudioSource* s = YOBJECT(YateH323AudioSource,getSource());
    if (s) {
	s->Close();
	pending = true;
    }
    YateH323AudioConsumer* c = YOBJECT(YateH323AudioConsumer,getConsumer());
    if (c) {
	c->Close();
	pending = true;
    }
    DDebug(this,DebugAll,"YateH323Chan::stopDataLinks() returning %s [%p]",
	String::boolText(pending),this);
    return pending;
}

PChannel* YateH323Chan::openAudioChannel(BOOL isEncoding)
{
    if (isEncoding) {
	// data going TO h.323
	YateH323AudioConsumer* cons = static_cast<YateH323AudioConsumer*>(getConsumer());
	if (!cons) {
	    setConsumer(cons = new YateH323AudioConsumer);
	    cons->deref();
	}
	return cons;
    }
    else {
	// data coming FROM h.323
	YateH323AudioSource* src = static_cast<YateH323AudioSource*>(getSource());
	if (!src) {
            setSource(src = new YateH323AudioSource);
	    src->deref();
	}
	return src;
    }
    return 0;
}

bool YateH323Chan::callRouted(Message& msg)
{
    Channel::callRouted(msg);
    if (m_conn) {
	// try to disable RTP forwarding earliest possible
	if (!msg.getBoolValue("rtp_forward"))
	    m_conn->rtpLocal();
        String s(msg.retValue());
        if (s.startSkip("h323/",false) && s && msg.getBoolValue("redirect") && m_conn->Lock()) {
            Debug(this,DebugAll,"YateH323Chan redirecting to '%s' [%p]",s.c_str(),this);
	    m_conn->TransferCall(s.safe());
	    m_conn->Unlock();
	    return false;
	}
	m_conn->updateFormats(msg);
	return true;
    }
    return false;
}

void YateH323Chan::callAccept(Message& msg)
{
    String* meths = msg.getParam(YSTRING("idtmfmethods"));
    if (meths) {
	DtmfMethods old = m_dtmfMethods;
	m_dtmfMethods.set(*meths,&old);
    }
    m_honorDtmfDetect = msg.getBoolValue(YSTRING("ihonor_dtmf_detect"),m_honorDtmfDetect);
    Channel::callAccept(msg);
    if (m_conn) {
	m_conn->rtpExecuted(msg);
	m_conn->updateFormats(msg);
	m_conn->answerCall(H323Connection::AnswerCallDeferred);
    }
}

void YateH323Chan::callRejected(const char* error, const char* reason, const Message* msg)
{
    Channel::callRejected(error,reason,msg);
    stopDataLinks();
    if (m_conn)
	m_conn->ClearCall((H323Connection::CallEndReason)lookup(error,dict_errors,H323Connection::EndedByLocalUser));
}

bool YateH323Chan::msgProgress(Message& msg)
{
    Channel::msgProgress(msg);
    if (!m_conn)
	return false;
    if (msg.getParam("rtp_forward"))
	m_conn->rtpForward(msg);
    m_conn->updateFormats(msg);
    m_conn->answerCall(H323Connection::AnswerCallDeferred,msg.getBoolValue("earlymedia",true));
    return true;
}

bool YateH323Chan::msgRinging(Message& msg)
{
    Channel::msgRinging(msg);
    if (!m_conn)
	return false;
    if (msg.getParam("rtp_forward"))
	m_conn->rtpForward(msg);
    m_conn->updateFormats(msg);
    m_conn->answerCall(H323Connection::AnswerCallPending,msg.getBoolValue("earlymedia",true));
    return true;
}

bool YateH323Chan::msgAnswered(Message& msg)
{
    Channel::msgAnswered(msg);
    if (!m_conn)
	return false;
    m_conn->rtpForward(msg);
    m_conn->updateFormats(msg);
    m_conn->answerCall(H323Connection::AnswerCallNow);
    return true;
}

bool YateH323Chan::msgTone(Message& msg, const char* tone)
{
    if (!(tone && m_conn))
	return false;
    DtmfMethods methods = m_dtmfMethods;
    const String* param = msg.getParam(YSTRING("methods"));
    if (param) {
	bool intersect = !msg.getBoolValue(YSTRING("methods_override"));
	methods.set(*param,&m_dtmfMethods,true,intersect);
    }
    bool retVal = false;
    bool ok = false;
    if (msg.getBoolValue(YSTRING("honor_dtmf_detect"),m_honorDtmfDetect)) {
	const String& detected = msg[YSTRING("detected")];
	int meth = lookup(detected,DtmfMethods::s_methodName,DtmfMethods::MethodCount);
	if (meth != DtmfMethods::MethodCount && methods.hasMethod(meth)) {
	    ok = sendTone(msg,tone,meth,retVal);
	    methods.reset(meth);
	}
    }
    for (int i = 0; !ok && i < DtmfMethods::MethodCount; i++) {
	int meth = methods[i];
	if (meth != DtmfMethods::MethodCount)
	    ok = sendTone(msg,tone,meth,retVal);
    }
    if (!ok && debugAt(DebugNote)) {
	String tmp;
	methods.buildMethods(tmp);
	Debug(this,DebugNote,"Failed to send tones '%s' methods=%s [%p]",tone,tmp.c_str(),this);
    }
    return retVal;
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

bool YateH323Chan::setDebug(Message& msg)
{
    if (!Channel::setDebug(msg))
	return false;
    Lock lock(m_mutex);
    if (m_conn)
	m_conn->debugCopy(this);
    return true;
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
    return hplugin.initEndpoint(tmp,&msg,false);
}


H323Driver::H323Driver()
    : Driver("h323","varchans"),
    m_epMutex(true,"H323:ep")
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
    if (id == Status) {
	const String* target = msg.getParam("module");
	if (target && target->startsWith(name(),true) && !target->startsWith(prefix())) {
	    msgStatus(msg);
	    return true;
	}
    }
    if (id == Stop)
	return handleEngineStop(msg);
    bool ok = Driver::received(msg,id);
    if (id == Halt) {
	// Wait a while for client threads to terminate
	int n = threadIdleIntervals(200000);
	while (YateGkRegThread::s_count && (--n) >= 0)
	    Thread::idle();
	if (YateGkRegThread::s_count)
	    Debug(this,DebugFail,"Exiting with %u client threads still running",
		YateGkRegThread::s_count);
        cleanup();
    }
    return ok;
};

void H323Driver::cleanup()
{
    m_epMutex.lock();
    m_endpoints.clear();
    m_epMutex.unlock();
    if (channels().count()) {
	Debug(this,DebugFail,"Still having channels after clearing up all!");
	channels().clear();
    }
    if (s_process) {
	PSyncPoint terminationSync;
	terminationSync.Signal();
	Output("Waiting for OpenH323 to die");
	terminationSync.Wait();
    }
}

void H323Driver::statusParams(String& str)
{
    Driver::statusParams(str);
    str.append("cleaning=",",") << cleaningCount();
}

bool H323Driver::hasLine(const String& line) const
{
    return line && hplugin.findEndpoint(line);
}

bool H323Driver::msgRoute(Message& msg)
{
    if (!s_process)
	return false;
    String* called = msg.getParam("called");
    if (!called || (called->find('@') >= 0))
	return false;
    return Driver::msgRoute(msg);
}

bool H323Driver::msgExecute(Message& msg, String& dest)
{
    if (!s_process)
	return false;
    if (dest.null())
        return false;
    if (!msg.userData()) {
	Debug(this,DebugWarn,"H.323 call found but no data channel!");
	return false;
    }
    Debug(this,DebugInfo,"Found call to H.323 target='%s'",
	dest.c_str());
    YateH323EndPoint* ep = hplugin.findEndpoint(msg.getValue("line"));
    if (ep) {
	if (YateCallThread::makeCall(ep,dest.c_str(),&msg,msg.getBoolValue("pwlibthread",s_pwlibThread)))
	    return true;
	// the only reason a YateH323Connection is not created is congestion
	msg.setParam("error","congestion");
	return false;
    }
    // endpoint unknown or not connected to gatekeeper
    msg.setParam("error","offline");
    return false;
};

void H323Driver::msgTimer(Message& msg)
{
    Driver::msgTimer(msg);
    if (!s_engineStop) {
	Lock lck(m_epMutex);
	for (ObjList* l = m_endpoints.skipNull(); l; l = l->skipNext())
	    static_cast<YateH323EndPoint*>(l->get())->checkGkClient();
    }
}

YateH323EndPoint* H323Driver::findEndpoint(const String& ep)
{
    Lock lck(m_epMutex);
    ObjList* l = m_endpoints.find(ep);
    return static_cast<YateH323EndPoint*>(l ? l->get() : 0);
}

// Safely add an endpoint to the list
void H323Driver::addEndPoint(YateH323EndPoint* ep)
{
    if (!ep)
	return;
    Lock lck(m_epMutex);
    if (m_endpoints.find(ep))
	return;
    m_endpoints.append(ep);
    Debug(this,DebugAll,"Added ep '%s' %p",ep->c_str(),ep);
}

// Safely remove an endpoint from list
void H323Driver::removeEndPoint(YateH323EndPoint* ep)
{
    if (!ep)
	return;
    Lock lck(m_epMutex);
    if (m_endpoints.remove(ep,false))
	Debug(this,DebugAll,"Removed ep '%s' %p",ep->c_str(),ep);
}

void H323Driver::initialize()
{
    Output("Initializing module H.323");
    s_cfgMutex.lock();
    s_cfg = Engine::configFile("h323chan");
    s_cfg.load();
    NamedList* general = s_cfg.getSection("general");
    if (general) {
	String* dtmfMethods = general->getParam("dtmfmethods");
	if (dtmfMethods) {
	    if (!s_dtmfMethods.set(*dtmfMethods,0))
		s_dtmfMethods.printMethods(this,DebugConf,*dtmfMethods);
	}
	else {
	    s_dtmfMethods.setDefault();
	    s_dtmfMethods.getDeprecatedDtmfMethod(*general,"dtmfinband",DtmfMethods::Inband,&s_warnDtmfInbandCfg);
	}
    }
    else
	s_dtmfMethods.setDefault();
    s_cfgMutex.unlock();
    s_honorDtmfDetect = s_cfg.getBoolValue("general","honor_dtmf_detect",true);
    s_externalRtp = s_cfg.getBoolValue("general","external_rtp",true);
    s_passtrough = s_cfg.getBoolValue("general","forward_rtp",false);
    s_fallbackRtp = s_cfg.getBoolValue("general","fallback_rtp",true);
    s_needMedia = s_cfg.getBoolValue("general","needmedia",true);
    // mantain compatibility with old config files
    s_passtrough = s_cfg.getBoolValue("general","passtrough_rtp",s_passtrough);
    s_maxCleaning = s_cfg.getIntValue("general","maxcleaning",100);
    s_pwlibThread = s_cfg.getBoolValue("general","pwlibthread");
    maxRoute(s_cfg.getIntValue("incoming","maxqueue",5));
    maxChans(s_cfg.getIntValue("ep","maxconns",maxChans()));
    if (!s_process) {
	setup();
	installRelay(Halt);
	s_process = new H323Process;
	installRelay(Progress);
	installRelay(Route);
	installRelay(Status);
	installRelay(Stop,"engine.stop");
	Engine::install(new UserHandler);
    }
    int dbg = s_cfg.getIntValue("general","debug");
    if (dbg < 0)
	dbg = 0;
    if (dbg > 10)
	dbg = 10;
    PTrace::Initialise(dbg,0,PTrace::Blocks | PTrace::Timestamp
	| PTrace::Thread | PTrace::FileAndLine);
    initEndpoint(String::empty(),s_cfg.getSection("ep"),true);
    int n = s_cfg.sections();
    for (int i = 0; i < n; i++) {
	NamedList* sect = s_cfg.getSection(i);
	if (!sect)
	    continue;
	String s(*sect);
	if (s.startSkip("ep ",false) && s.trimBlanks())
	    initEndpoint(s,sect,true);
    }
}

// Create and initialize an endpoint
bool H323Driver::initEndpoint(const String& name, const NamedList* params, bool fromConfig)
{
    bool reg = true;
    if (!fromConfig) {
	if (!params)
	    return false;
	const String& oper = (*params)[YSTRING("operation")];
	reg = (oper == YSTRING("login")) || (oper == YSTRING("create"));
	if (!reg && oper != YSTRING("logout") && oper != YSTRING("delete"))
	    return false;
	// Don't accept login if exiting or logout after first engine.stop
	if ((reg && Engine::exiting()) || (!reg && s_engineStop > 1))
	    return false;
    }
    Lock lck(m_epMutex);
    YateH323EndPoint* ep = findEndpoint(name);
    DDebug(this,DebugAll,"initEndpoint(%s,%p,%u) reg=%u found=%p",name.c_str(),params,fromConfig,reg,ep);
    if (!ep && (reg || fromConfig)) {
	ep = new YateH323EndPoint(params,name);
	addEndPoint(ep);
    }
    lck.drop();
    return ep && ep->Init(reg,params);
}

bool H323Driver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    String cmd = s_statusCmd + " " + name();
    String overviewCmd = s_statusCmd + " overview " + name();
    if (partLine == cmd || partLine == overviewCmd)
	itemComplete(msg.retValue(),"accounts",partWord);
    else
    	return Driver::commandComplete(msg,partLine,partWord);
    return false;
}

void H323Driver::msgStatus(Message& msg)
{
    String str = msg.getValue("module");
    while (str.startSkip(name())) {
	str.trimBlanks();
	if (str.null())
	    break;
	if (str.startSkip("accounts")) {
	    msg.retValue().clear();
	    msg.retValue() << "module=" << name();
	    msg.retValue() << ",protocol=H323";
	    msg.retValue() << ",format=Username|Status;";
	    Lock lck(m_epMutex);
	    msg.retValue() << "accounts=" << m_endpoints.count();
	    if (!msg.getBoolValue("details",true)) {
		msg.retValue() << "\r\n";
		return;
	    }
	    for (ObjList* o = m_endpoints.skipNull(); o; o = o->skipNext()) {
		YateH323EndPoint* ep = static_cast<YateH323EndPoint*>(o->get());
		str.append(ep->c_str(),",") << "=";
		str << (const char*)ep->GetLocalUserName() << "|";
		str << (ep->IsRegisteredWithGatekeeper() ? "registered" : "not-registered");
	    }
	    msg.retValue().append(str,";");
    	    msg.retValue() << "\r\n";
	    return;
	}
    }
    Driver::msgStatus(msg);
}

bool H323Driver::handleEngineStop(Message& msg)
{
    s_engineStop++;
    dropAll(msg);
    bool noHalt = false;
    m_epMutex.lock();
    ListIterator iter(m_endpoints);
    for (GenObject* gen = 0; (0 != (gen = iter.get()));) {
	m_epMutex.unlock();
	YateH323EndPoint* ep = static_cast<YateH323EndPoint*>(gen);
	ep->logout();
	if (ep->IsRegisteredWithGatekeeper())
	    noHalt = true;
	m_epMutex.lock();
    }
    m_epMutex.unlock();
    // Don't stop if still have channels
    if (!noHalt) {
	Lock mylock(this);
	noHalt = (0 != channels().skipNull());
    }
    // Don't stop if we still have threads
    noHalt = noHalt || (0 != YateGkRegThread::s_count);
    Debug(this,DebugAll,"Returning %s from %s handler",String::boolText(noHalt),msg.c_str());
    return noHalt;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
