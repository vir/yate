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

/* Guess if codecs are dynamically loaded or linked in */
#if (OPENH323_NUMVERSION < 11300)
#define OLD_STYLE_CODECS 1
#endif

/* Guess if we have a QOS parameter to the RTP channel creation */
#if (OPENH323_NUMVERSION >= 11304)
#define NEED_RTP_QOS_PARAM 1
#endif

#include <telengine.h>
#include <telephony.h>
#include <yateversn.h>

#include <string.h>

using namespace TelEngine;

static Mutex s_calls;
static Mutex s_route;

static bool s_externalRtp;

static Configuration s_cfg;
static ObjList translate;

static TokenDict dict_str2code[] = {
    { "alpha" , PProcess::AlphaCode },
    { "beta" , PProcess::BetaCode },
    { "release" , PProcess::ReleaseCode },
    { 0 , 0 },
};

const char* h323_formats[] = {
    "G.711-ALaw-64k{sw}", "alaw",
    "G.711-uLaw-64k{sw}", "mulaw",
    "GSM-06.10{sw}", "gsm",
    0
};

static TokenDict dict_h323_dir[] = {
    { "receive", H323Channel::IsReceiver },
    { "send", H323Channel::IsTransmitter },
    { "bidir", H323Channel::IsBidirectional },
    { 0 , 0 },
};

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

class TranslateObj : public GenObject
{
public:
   H225_TransportAddress_ipAddress ip;
   PString alias;
   String e164;
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
    TranslateObj * findAlias(const PString & alias);
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
#ifdef NEED_RTP_QOS_PARAM
    H323Channel *CreateRealTimeLogicalChannel(const H323Capability & capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters *param,RTP_QOS * rtpqos = NULL);
#else
    H323Channel *CreateRealTimeLogicalChannel(const H323Capability & capability,H323Channel::Directions dir,unsigned sessionID,const H245_H2250LogicalChannelParameters *param);
#endif
    BOOL OnStartLogicalChannel(H323Channel & channel);
    BOOL OnCreateLogicalChannel(const H323Capability & capability, H323Channel::Directions dir, unsigned & errorCode ) ;
    BOOL StartExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan);
    void OnStoppedExternal(H323Channel::Directions dir);
    virtual void disconnected();
    inline const String &id() const
	{ return m_id; }
    inline const String &status() const
	{ return m_status; }
    inline static int total()
	{ return s_total; }
private:
    bool m_nativeRtp;
    String m_id;
    String m_status;
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
    StatusHandler() : MessageHandler("status") { }
    virtual bool received(Message &msg);
};

class H323Plugin : public Plugin
{
public:
    H323Plugin();
    virtual ~H323Plugin();
    virtual void initialize();
    void cleanup();
    YateH323Connection *findConnectionLock(const char *id);
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
    Engine::dispatch(m_msg);
    *m_msg = "preroute";
    Engine::dispatch(m_msg);
    *m_msg = "route";
    bool ok = Engine::dispatch(m_msg) && !m_msg->retValue().null();
    YateH323Connection *conn = hplugin.findConnectionLock(m_id);
    if (!conn) {
	Debug(DebugMild,"YateH323Connection '%s' wanished while routing!",m_id.c_str());
	return false;
    }
    if (ok) {
	conn->AnsweringCall(H323Connection::AnswerCallPending);
	*m_msg = "call";
	m_msg->addParam("callto",m_msg->retValue());
	m_msg->retValue() = 0;
	m_msg->userData(static_cast<DataEndpoint *>(conn));
	if (Engine::dispatch(m_msg)) {
	    Debug(DebugInfo,"Routing H.323 call %s [%p] to '%s'",m_id.c_str(),conn,m_msg->getValue("callto"));
	    conn->AnsweringCall(H323Connection::AnswerCallNow);
	    conn->deref();
	}
	else {
	    Debug(DebugInfo,"Rejecting unconnected H.323 call %s [%p]",m_id.c_str(),conn);
	    conn->AnsweringCall(H323Connection::AnswerCallDenied);
	}
    }
    else {
	Debug(DebugInfo,"Rejecting unrouted H.323 call %s [%p]",m_id.c_str(),conn);
	conn->AnsweringCall(H323Connection::AnswerCallDenied);
    }
    conn->Unlock();
    return ok;
}

void H323MsgThread::run()
{
    Debug(DebugAll,"Started routing thread for %s [%p]",m_id.c_str(),this);
    s_route.lock();
    s_count++;
    s_route.unlock();
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
  Debug(DebugInfo,"i = %d",i);
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
    return new YateH323Connection(*this,callReference,userData);
}

bool YateH323EndPoint::Init(void)
{
    if (s_cfg.getBoolValue("codecs","g711u",true))
#ifdef OLD_STYLE_CODECS
	SetCapability(0,0,new H323_G711Capability(H323_G711Capability::muLaw));
#else
	AddAllCapabilities(0, 0, "G.711-u*{sw}");
#endif

    if (s_cfg.getBoolValue("codecs","g711a",true))
#ifdef OLD_STYLE_CODECS
	SetCapability(0,0,new H323_G711Capability(H323_G711Capability::ALaw));
#else
	AddAllCapabilities(0, 0, "G.711-A*{sw}");
#endif

    if (s_cfg.getBoolValue("codecs","gsm0610",true)) {
#ifdef OLD_STYLE_CODECS
	H323_GSM0610Capability *gsmCap = new H323_GSM0610Capability;
	SetCapability(0, 0, gsmCap);
	gsmCap->SetTxFramesInPacket(4);
#else
	AddAllCapabilities(0, 0, "GSM*{sw}");
#endif
    }

    if (s_cfg.getBoolValue("codecs","speexnarrow",true)) {
#ifdef OLD_STYLE_CODECS
	SpeexNarrow3AudioCapability *speex3Cap = new SpeexNarrow3AudioCapability();
	SetCapability(0, 0, speex3Cap);
	speex3Cap->SetTxFramesInPacket(5);
#else
	AddAllCapabilities(0, 0, "Speex*{sw}");
#endif
    }

    if (s_cfg.getBoolValue("codecs","lpc10",true))
#ifdef OLD_STYLE_CODECS
	SetCapability(0, 0, new H323_LPC10Capability(*this));
#else
	AddAllCapabilities(0, 0, "LPC*{sw}");
#endif

    AddAllUserInputCapabilities(0,1);

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
/*    	    if (s_cfg.getBoolValue("gk","server",true))
	    {
		gkServer = new YateGatekeeperServer(*this);
	   	gkServer->Init();
	    }
*/	

//    bool useGk = s_cfg.getBoolean("general","use_gatekeeper");
    return true;
}

YateH323Connection::YateH323Connection(YateH323EndPoint &endpoint,
    unsigned callReference, void *userdata)
    : H323Connection(endpoint,callReference), DataEndpoint("h323"), m_nativeRtp(false)
{
    Debug(DebugAll,"YateH323Connection::YateH323Connection(%p,%u,%p) [%p]",
	&endpoint,callReference,userdata,this);
    m_id = "h323/";
    m_id << callReference;
    m_status = "new";
    s_calls.lock();
    hplugin.calls().append(this)->setDelete(false);
    s_calls.unlock();
    DataEndpoint *dd = static_cast<DataEndpoint *>(userdata);
    if (dd && connect(dd))
	deref();
}

YateH323Connection::~YateH323Connection()
{
    Debug(DebugAll,"YateH323Connection::~YateH323Connection() [%p]",this);
    m_status = "destroyed";
    s_calls.lock();
    hplugin.calls().remove(this,false);
    s_calls.unlock();
    CloseAllLogicalChannels(true);
    CloseAllLogicalChannels(false);
}

H323Connection::AnswerCallResponse YateH323Connection::OnAnswerCall(const PString &caller,
    const H323SignalPDU &setupPDU, H323SignalPDU &connectPDU)
{
    Debug(DebugInfo,"YateH323Connection::OnAnswerCall caller='%s' [%p]",(const char *)caller,this);
    m_status = "incoming";

    if (int cnt = H323MsgThread::count() > s_cfg.getIntValue("incoming","maxqueue",5)) {
	Debug(DebugWarn,"Dropping call, there are already %d waiting",cnt);
	return H323Connection::AnswerCallDeferred;
    }

    Message *m = new Message("ring");
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
    s_route.lock();
    H323MsgThread *t = new H323MsgThread(m,id());
    if (t->error()) {
	Debug(DebugWarn,"Error starting routing thread! [%p]",this);
	s_route.unlock();
	delete t;
	return H323Connection::AnswerCallDenied;
    }
    s_route.unlock();
    return H323Connection::AnswerCallDeferred;
}

void YateH323Connection::OnEstablished()
{
    Debug(DebugInfo,"YateH323Connection::OnEstablished() [%p]",this);
    s_calls.lock();
    s_total++;
    m_status = "connected";
    s_calls.unlock();
    if (!HadAnsweredCall())
	return;
    Message *m = new Message("answer");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    m->addParam("status","answered");
    Engine::enqueue(m);
}

void YateH323Connection::OnCleared()
{
    Debug(DebugInfo,"YateH323Connection::OnCleared() [%p]",this);
    m_status = "cleared";
    bool ans = HadAnsweredCall();
    disconnect();
    if (!ans)
	return;
    Message *m = new Message("hangup");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    Engine::enqueue(m);
}

BOOL YateH323Connection::OnAlerting(const H323SignalPDU &alertingPDU, const PString &user)
{
    Debug(DebugInfo,"YateH323Connection::OnAlerting '%s' [%p]",(const char *)user,this);
    m_status = "ringing";
    Message *m = new Message("ringing");
    m->addParam("driver","h323");
    m->addParam("id",m_id);
    Engine::enqueue(m);
    return true;
}

void YateH323Connection::OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
    Debug(DebugInfo,"YateH323Connection::OnUserInputTone '%c' duration=%u [%p]",tone,duration,this);
}

void YateH323Connection::OnUserInputString(const PString &value)
{
    Debug(DebugInfo,"YateH323Connection::OnUserInputString '%s' [%p]",(const char *)value,this);
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

void YateH323Connection::disconnected()
{
    Debugger debug("YateH323Connection::disconnected()"," [%p]",this);
    m_status = "disconnected";
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
    if (s_externalRtp) {
	const char* sdir = lookup(dir,dict_h323_dir);
	Debug(DebugInfo,"capability '%s' session %u %s",(const char *)capability.GetFormatName(),sessionID,sdir);
	PIPSocket::Address externalIpAddress;
//	GetControlChannel().GetLocalAddress().GetIpAndPort(externalIpAddress, port);
	GetControlChannel().GetLocalAddress().GetIpAddress(externalIpAddress);
	Debug(DebugInfo,"address '%s'",(const char *)externalIpAddress.AsString());
	Message m("rtp");
	m.addParam("localip",externalIpAddress.AsString());
	m.userData(static_cast<DataEndpoint *>(this));
Debug(DebugAll,"userData=%p this=%p",m.userData(),this);
	if (sdir)
	    m.addParam("direction",sdir);
	if (Engine::dispatch(m)) {
	    String p(m.getValue("localport"));
	    WORD externalPort = p.toInteger();
	    if (externalPort) {
		m_nativeRtp = false;
		return new YateH323_ExternalRTPChannel(*this, capability, dir, sessionID, externalIpAddress, externalPort);
	    }
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

BOOL YateH323Connection::StartExternalRTP(const char* remoteIP, WORD remotePort, H323Channel::Directions dir, YateH323_ExternalRTPChannel* chan)
{
    const char* sdir = lookup(dir,dict_h323_dir);
    Debug(DebugAll,"YateH323Connection::StartExternalRTP(\"%s\",%u,%s,%p) [%p]",
	remoteIP,remotePort,sdir,chan,this);
    Message m("rtp");
    m.userData(static_cast<DataEndpoint *>(this));
Debug(DebugAll,"userData=%p this=%p",m.userData(),this);
    if (sdir)
	m.addParam("direction",sdir);
    m.addParam("remoteip",remoteIP);
    m.addParam("remoteport",String(remotePort));
    String capability((const char *)chan->GetCapability().GetFormatName());
//    int payload = chan->GetCapability().GetPayloadType();
    OpalMediaFormat oformat(capability, FALSE);
    int payload = oformat.GetPayloadType();
    const char *format = 0;
    const char** f = h323_formats;
    for (; *f; f += 2) {
	if (capability == *f) {
	    format = f[1];
	    break;
	}
    }
    Debug(DebugInfo,"capability '%s' format '%s' payload %d",capability.c_str(),format,payload);
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
    return H323_ExternalRTPChannel::OnReceivedPDU(param,errorCode);
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
    m_mutex.lock();
    m_mutex.unlock();
}

BOOL YateH323AudioConsumer::Close()
{
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
	if (len >= (int)m_buffer.length()) {
	    Thread::yield();
	    if (m_exit || Engine::exiting())
		return false;
	    continue;
	}
	if (len > 0) {
	    ::memcpy(buf,m_buffer.data(),len);
	    m_buffer.assign(len+(char *)m_buffer.data(),m_buffer.length()-len);
#ifdef DEBUG
	    Debug("YateH323AudioConsumer",DebugAll,"Pulled %d bytes from buffer [%p]",len,this);
#endif
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

TranslateObj * YateGatekeeperServer::findAlias(const PString &alias)
{ 
    ObjList *p = &translate; 
    for (; p; p=p->next()) { 
	    TranslateObj *t =
            static_cast<TranslateObj *>(p->get()); 
	    if (t && t->alias == alias)
		    return t; 
    } 
    return 0; 
}

BOOL YateGatekeeperServer::GetUsersPassword(const PString & alias,PString & password) const
{
    Message *m = new Message("auth");
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
//    PString request_s = request.GetGatekeeperIdentifier();
//    request.rrq.HasOptionalField(H225_RegistrationRequest::e_endpointIdentifier);
    PString alias;
    PString ips;
    PString r ;
    for (int j = 0; j < request.rrq.m_terminalAlias.GetSize(); j++) {
		alias = H323GetAliasAddressString(request.rrq.m_terminalAlias[j]);
		r = H323GetAliasAddressE164(request.rrq.m_terminalAlias[j]);
//		PString c = request.GetEndpointIdentifier();
		Debug(DebugInfo,"marimea matrici este %d : ",request.rrq.m_callSignalAddress.GetSize());
//		H225_TransportAddress_ipAddress ip=request.rrq.m_callSignalAddress[0];
		for (int k=0; k<request.rrq.m_callSignalAddress.GetSize();k++)
		{
        	        H225_TransportAddress_ipAddress ip=request.rrq.m_callSignalAddress[k];
        		ips = String(ip.m_ip[0]) + "." + String(ip.m_ip[1]) + "." + String(ip.m_ip[2]) + "."  + String(ip.m_ip[3]) + ":" + String((int)ip.m_port) ;
    			Debug(DebugInfo,"Stringul initial este %s",(const char *)ips);
		}	
//		Debug(DebugInfo,"ip %d.%d.%d.%d:%u",ip.m_ip[0], ip.m_ip[1], ip.m_ip[2], ip.m_ip[3],ip.m_port.GetValue());

//        	Debug(DebugInfo,"end point user id %s  and e164 %s",(const char *)s,(const char *)r); //(const char *)request.m_endpointIdentifier); //request_s.GetLength());
        	H225_TransportAddress_ipAddress ip=request.rrq.m_callSignalAddress[0];
		Message *m = new Message("regist");
		m->addParam("username",alias);
		m->addParam("techno","h323");
		m->addParam("data",ips);
		Engine::dispatch(m);
		Debug(DebugInfo,"prefix boo registering %s",m->retValue().c_str());
		
		TranslateObj *t = new TranslateObj;
		t->ip = ip;
		t->alias = alias;
		t->e164 = m->retValue();
		translate.append(t);
    }
/*    for (int i = 0; i < request.rrq.m_callSignalAddress.GetSize(); i++) {
        Debug(DebugInfo,"end point identifier %d",request.rrq.m_callSignalAddress[i]); //(const char *)request.m_endpointIdentifier); //request_s.GetLength());
    }*/
    
    return H323GatekeeperServer::OnRegistration(request);
}
H323GatekeeperRequest::Response YateGatekeeperServer::OnUnregistration(H323GatekeeperURQ & request )
{
/*   for (int j = 0; j < request.urq.m_terminalAlias.GetSize(); j++) {
       PString s = H323GetAliasAddressString(request.urq.m_terminalAlias[j]);
   }*/
//   PString s = H323GetAliasAddressString(request.urq.m_callSignalAddress[0]);
    PString s = H323GetAliasAddressString(request.urq.m_endpointAlias[0]);
    TranslateObj * c = findAlias(s);
    
    Message *m = new Message("unregist");
    m->addParam("prefix",c->e164);
    Debug(DebugInfo,"prefixh323 %s",c->e164.c_str());
    Engine::dispatch(m);

   //Debug(DebugInfo,"aliasul descarcat este %s",(const char *)c->alias);
    translate.remove(c);
   
   return H323GatekeeperServer::OnUnregistration(request);
}

BOOL YateGatekeeperServer::TranslateAliasAddressToSignalAddress(const H225_AliasAddress & alias,H323TransportAddress & address)
{
    PString aliasString = H323GetAliasAddressString(alias);
   // TranslateObj *f = new TranslateObj;
    //f->alias = aliasString;
    //translate.find()
    TranslateObj * c = findAlias(aliasString);
//    c->alias = aliasString;
    //Debug(DebugInfo,"ip-ul corespondent este %d.%d.%d.%d:%u",c->ip.m_ip[0],c->ip.m_ip[1],c->ip.m_ip[2],c->ip.m_ip[3],c->ip.m_port.GetValue());
    if (c)
    {
    	Debug(DebugInfo,"alias-ul este %s si cel gasit este %s",(const char *)aliasString,(const char *)c->alias);
//       Debug(DebugInfo,"ip-ul corespondent este %d.%d.%d.%d:%u",c->ip.m_ip[0],c->ip.m_ip[1],c->ip.m_ip[2],c->ip.m_ip[3],c->ip.m_port.GetValue());
        String s = "ip$" + String(c->ip.m_ip[0]) + "." + String(c->ip.m_ip[1]) + "." + String(c->ip.m_ip[2]) + "."  + String(c->ip.m_ip[3]) + ":" + String((int)c->ip.m_port) ;
    	Debug(DebugInfo,"Stringul este %s",(const char *)s);
/*	H323TransportAddress aliasAsTransport = c->aliasaddres;
	PIPSocket::Address ip;
	WORD port = H323EndPoint::DefaultTcpPort;
	if (!aliasAsTransport.GetIpAndPort(ip, port)) {
    		Debug(DebugInfo,"RAS\tCould not translate %s as host name.",(const char *)aliasString);
		return FALSE;
	}*/
//	H225_TransportAddress ceva = c->aliasaddres;
//	address = H323TransportAddress(ceva);
    	//Debug(DebugInfo,"RAS\tTranslating alias %s to %s, host name",(const char *)aliasString,(const char *)address);
//	return TRUE;
	address = s.c_str();
	return TRUE;
	
	//if (H323GatekeeperServer::TranslateAliasAddressToSignalAddress(alias, address))
   }
//    if (H323GatekeeperServer::TranslateAliasAddressToSignalAddress(alias, address))
//	    return TRUE;

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
    H323Connection *conn = hplugin.ep()->MakeCallLocked(dest.matchString(1).c_str(),p,msg.userData());
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

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"h323chan") && ::strcmp(sel,"varchans"))
	return false;
    String st("h323chan,type=varchans");
    Lock lock(s_calls);
    st << ",routed=" << H323MsgThread::routed() << ",routers=" << H323MsgThread::count();
    st << ",total=" << YateH323Connection::total() << ",chans=" << hplugin.calls().count() << ",[LIST]";
    ObjList *l = &hplugin.calls();
    for (; l; l=l->next()) {
	YateH323Connection *c = static_cast<YateH323Connection *>(l->get());
	if (c) {
	    // HACK: we assume transport$address/callref format
	    String s((const char *)c->GetCallToken());
	    st << "," << c->id() << "=" << s.substr(0,s.rfind('/')) << "/" << c->status();
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

YateH323Connection *H323Plugin::findConnectionLock(const char *id)
{
    s_calls.lock();
    ObjList *l = &m_calls;
    for (; l; l=l->next()) {
	YateH323Connection *c = static_cast<YateH323Connection *>(l->get());
	if (c && (c->id() == id)) {
	    if (c->TryLock() > 0) {
		s_calls.unlock();
		return c;
	    }
	    else {
		// Yield and try scanning the list again
		l = &m_calls;
		s_calls.unlock();
		Thread::yield();
		s_calls.lock();
	    }
	}
    }
    s_calls.unlock();
    return 0;
}

void H323Plugin::initialize()
{
    Output("Initializing module H.323 - based on OpenH323-" OPENH323_VERSION);
    s_cfg = Engine::configFile("h323chan");
    s_cfg.load();
    if (!m_process)
	m_process = new H323Process;
    int dbg=s_cfg.getIntValue("general","debug");
    if (dbg)
	PTrace::Initialise(dbg,0,PTrace::Blocks | PTrace::Timestamp
	    | PTrace::Thread | PTrace::FileAndLine);
    if (!m_endpoint) {
	m_endpoint = new YateH323EndPoint;
	m_endpoint->Init();
    }
    s_externalRtp = s_cfg.getBoolValue("general","external_rtp",false);
    if (m_first) {
	m_first = false;
	Engine::install(new H323Handler("call"));
	Engine::install(new H323Dropper("drop"));
	Engine::install(new H323Stopper("engine.halt"));
	Engine::install(new StatusHandler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
