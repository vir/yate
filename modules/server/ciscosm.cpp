/**
 * ciscosm.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7
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

#include <yatephone.h>
#include <yatesig.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BUF_SIZE  48500

using namespace TelEngine;
namespace { // anonymous

class RudpSocket;
class SessionManager;
class SessionUser;
class SLT;
class DataSequence;
class RudpThread;
class SessionManagerModule;
class Modulo256;

// The list of sessions
static ObjList s_sessions;
Mutex s_sessionMutex(false,"CiscoSM");

class Modulo256
{
public:
    // Increment a value. Set to 0 if greater the 255
    static inline void inc(unsigned int& value) {
	    if (value < 255)
		value++;
	    else
		value = 0;
	}
    static inline bool between(int value, int low, int high) {
	    if (low == high)
		return value == low;
	    if (low < high)
		return value >= low && value <= high;
	    // low > high: counter wrapped around
	    return value >= low || value <= high;
	}
};

// Class used to keep the data and the sequence number
class DataSequence : public DataBlock
{
public:
    DataSequence(DataBlock& data, u_int8_t seq);
    ~DataSequence();
    inline void addSeq(unsigned int seq)
	{m_seq = seq;}
    inline u_int8_t sequence()
	{return m_seq;}
    inline void inc()
	{m_retransmitted++;}
    inline u_int8_t retransCounter()
	{ return m_retransmitted; }
    // Method called to change ack number in case the data need to be retransmissed
    bool refreshAck(u_int8_t ack);
private:
    u_int8_t m_seq;                                        // The data sequence number
    u_int8_t m_retransmitted;                              // Counter increased every time when the data is retransmitted
};

class RudpThread : public Thread
{
    friend class RudpSocket;
public:
    RudpThread(RudpSocket* rudp,Priority prio);
    virtual ~RudpThread();
    virtual void run();
private:
    RudpSocket* m_rudp;                                    // The rudp socket that holds this thread
};

class RudpSocket : public GenObject, public Mutex
{
    friend class DataSequence;
public:
    enum RudpState {
	RudpUp,
	RudpDown,
	RudpWait,
	RudpDead,
    };
    RudpSocket(SessionManager* sm);
    ~RudpSocket();
    //Method called by Session Manager to initialize the socket
    bool initialize(const NamedList& params);
    // Socket initialization
    bool initSocket(const NamedList& params);
    // Check if we have any new data in queue
    bool checkData(bool force);
    // Retransmit the data between the retransmission timer sequence number and actual sequence number
    void retransData();
    void checkAck(const DataBlock& data);
    bool checkSeq(const DataBlock& data);
    void checkTimeouts(u_int64_t time);
    void sendAck();
    void buildAck(DataBlock& data);
    void sendNull();
    void sendSyn(bool recvSyn);
    void keepData(DataBlock& data, int seq);
    void sendMSG(const DataBlock& msg);
    bool readData();
    void recvMsg(DataBlock& packet);
    bool sendData(const DataBlock& msg);
    bool handleSyn(const DataBlock& data, bool ack);
    void removeData(u_int8_t ack);
    void handleEack(const DataBlock& data);
    void handleData(DataBlock& data);
    void reset();
    bool checkChecksum(DataBlock& data);
    //thread functions
    bool running();
    void kill();
    void removeOneData(u_int8_t ack);
    bool startThread(Thread::Priority prio = Thread::Normal);
    void stopThread();
    u_int16_t checksum(u_int16_t len,const u_int8_t* buff);
    void appendChecksum(DataBlock& data);
    inline void setThread()
	{ m_thread = 0;}
    inline bool haveSyn(u_int8_t flags)
	{ return 0 != (flags & 0x80); }
    inline bool haveAck(u_int8_t flags)
	{ return 0 != (flags & 0x40); }
    inline bool haveNul(u_int8_t flags)
	{ return 0 != (flags & 0x8); }
    inline bool haveChecksum(u_int8_t flags)
	{ return 0 != (flags & 0x4); }
    inline bool haveEack(u_int8_t flags)
	{ return 0 != (flags & 0x20); }
    inline bool haveReset(u_int8_t flags)
	{ return 0 != (flags & 0x10); }
    inline bool haveTcs(u_int8_t flags)
	{ return 0 != (flags & 0x2); }
    inline unsigned int getAckNum()
	{ return m_ackNum; }
    static inline const char* stateName(RudpState s)
	{ return lookup((int)s,s_RudpStates); }
    void changeState(RudpState newState);
    inline RudpState State()
	{ return m_state; }
private:
    SessionManager* m_sm;                                  // The session manager that owns this socket
    RudpThread* m_thread;                                  // The socket thread
    Socket *m_socket;                                      // The socket
    ObjList m_msgList;                                     // The messages list
    int m_lastError;                                       // Last error we have displayed
    // Sequence numbers
    unsigned int m_sequence;                               // The sequence number
    unsigned int m_ackNum;                                 // The sequence number of the last in sequence message received
    unsigned int m_lastAck;                                // The last acknowledged message
    unsigned int m_lastSend;                               // The last message sequence number that was send
    unsigned int m_retTStartSeq;                           // The sequence number when was started retransmission timer
    unsigned int m_syn;                                    // The sequence number of the syn segment sent by us when
                                                           //   we receive the confirmation for it we inform the session manager
    // Timers
    SignallingTimer m_cumAckTimer;                         // Cumulative Acknowledgment timer
    SignallingTimer m_nullTimer;                           // Null timer
    SignallingTimer m_retransTimer;                        // Retransmission timer
    SignallingTimer m_synTimer;                            // Syn retransmission timer
    // Flags
    int m_version;                                         // RUDP version, negative to autodetect
    bool m_haveChecksum;                                   // Flag that indicate if we have checksum or not
    bool m_sendSyn;                                        // Flag that indicate if we should send syn segment or wait for it
    // Connection
    u_int32_t m_connId;                                    // Connection identifier
    // Counters
    u_int8_t m_retransCounter;                             // The maximum number of retransmissions
    u_int8_t m_maxCumAck;                                  // The maximum number of segments received without being confirmed
    u_int8_t m_queueCount;                                 // The last number of messages from list known by rudp thread
    unsigned int m_wrongChecksum;                          // Counter how keeps the number of packets
                                                           // received with wrong checksum
    static const TokenDict s_RudpStates[];
    RudpState m_state;                                     // Rudp state
};

class SessionManager : public RefObject , public DebugEnabler, public Mutex
{
public:
    enum State {
	Operational,
	Nonoperational,
	StandBy,
    };
    enum Type {
	Start = 0,                                         // Start Message
	Stop = 1,                                          // Stop Message
	Active = 2,                                        // Active Message used with redundant MGC configuration
	Standby = 3,                                       // Standby Message used with redundant MGC configuration
	Q_HOLD_I = 4,                                      // Q_HOLD Invoke Message used with redundant MGC configuration
	Q_HOLD_R = 5,                                      // Q_HOLD Response Message used with redundant MGC configuration
	Q_RESUME_I = 6,                                    // Q_RESUME Invoke Message used with redundant MGC configuration
	Q_RESUME_R = 7,                                    // Q_RESUME Response Message used with redundant MGC configuration
	Q_RESET_I = 8,                                     // Q_RESET Invoke Message used with redundant MGC configuration
	Q_RESET_R = 9,                                     // Q_RESET Response Message used with redundant MGC configuration
	Q_RESTART = 12,
	PDU = 0x8000,                                      // PDU - Non Session Manager message from application
    };
    SessionManager(const String& name,const NamedList& param);
    ~SessionManager();
    // Initialize this layer and rudp also
    bool initialize(const NamedList& params);
    virtual const String& toString() const
	{ return m_name; }
    // Receiving notifications from rudp
    void notify(bool up);
    // Handle the data received from Rudp
    void handleData(DataBlock& data);
    // Send start stop messages
    void initSession();
    bool sendData(DataBlock& data, bool connectR = false);
    // Find a specific user
    bool insert(SessionUser* user);
    void remove(SessionUser* user);
    void userNotice(bool up);
    static SessionManager* get(const String& name, const NamedList* params);
    // Send a PDU message if state is operational else return false
    bool sendPdu(const DataBlock& data);
    void handlePDU(DataBlock& data);
    void handleSmMessage(u_int32_t smMessageType);
    // Inform all users about session manager status change
    void informUser(bool up);
    inline bool operational() const
	{ return Operational == m_state; }
    static inline const char* stateName(State s)
	{ return lookup((int)s,s_smStates); }
    void changeState(State newState);
    static inline const char* typeName(Type type)
	{ return lookup(type,s_types,"Unknown Message Type"); }
    inline RudpSocket* socket()
	{ return m_socket;}
    virtual void destroyed();
private:
    ObjList m_users;                                       // List of users
    RudpSocket* m_socket;                                  // The Rudp Layer
    State m_state;                                         // Session Manager state
    String m_name;                                         // The name of this session
    unsigned int m_upUsers;                                // The number of up users
    SignallingTimer m_standbyTimer;                        // When should send next Standby
    static const TokenDict s_smStates[];
    static const TokenDict s_types[];
};

class SessionUser
{
public:
    SessionUser(u_int16_t protType);
    virtual ~SessionUser();
    virtual void notify(bool up) = 0;
    virtual bool checkMessage(DataBlock& data) = 0;
    inline u_int16_t protocol()
	{return m_protType;}
protected:
    u_int16_t m_protType;                                  // Protocol type
    RefPointer<SessionManager> m_session;
};

class SLT : public SS7Layer2, public SessionUser
{
    YCLASS(SLT,SS7Layer2)
public:
    enum Messages {
	Connect_R = 0x06,                                  // Connect Request message
	Connect_C = 0x07,                                  // Connect confirmation
	Disconnect_R = 0x0a,                               // Disconnect request
	Disconnect_C = 0x0b,                               // Disconnect confirmation
	Disconnect_I = 0x0c,                               // Disconnect indication
	Data_Req = 0x10,                                   // Data request, send message out link
	Data_Ind = 0x11,                                   // Data indication, receive message from link
	Data_Retrieval_R = 0x12,                           // Data retrieval request
	Data_Retrieval_C = 0x13,                           // Data retrieval confirmation
	Data_Retrieval_I = 0x14,                           // Data retrieval indication
	Data_Retrieval_M = 0x15,                           // Data retrieval message
	Link_State_Controller_R = 0x20,                    // Link state controller request
	Link_State_Controller_C = 0x21,                    // Link state controller confirmation
	Link_State_Controller_I = 0x22,                    // Link state controller indication
	// Management messages
	Configuration_R = 0x40,                            // Configuration request
	Configuration_C = 0x41,                            // Configuration confirmation
	Status_R = 0x42,                                   // Status request
	Status_C = 0x43,                                   // Status confirmation
	Statistic_R = 0x44,                                // Statistic request
	Statistic_C = 0x45,                                // Statistic confirmation
	Control_R = 0x46,                                  // Control request
	Control_C = 0x47,                                  // Control confirmation
	Flow_Control_R = 0x50,                             // Flow control response from mtp3
	Flow_Control_I = 0x51,                             // Flow control indication to mtp3
    };
    enum connectM {
	Emergency = 0x03,                                  // Emergency alignment
	Normal = 0x04,                                     // Normal alignment
	Power = 0x05,                                      // Power on mtp2
	Start = 0x06,                                      // Start mtp2
    };
    enum errors {
	Unknown = 0x00,                                    // Unknown error
	T2_expired = 0x14,                                 // T2 expired waiting for sio
	// these are the only error code known at this time
    };
    enum State {
	Configured,                                        // SLT layer has sent the configuration pdu and received the
                                                           //  confirmation
	Waiting,                                           // SLT layer has sent the configuration pdu but hasn't received
                                                           //  the confirmation
	Unconfigured,                                      // SLT didn't send the configuration message
    };
    enum dataRetR {
	Return = 0x01,                                     // Return the BSN
	Retrieve = 0x02,                                   // Retrieve messages from BSN
	Drop = 0x03,                                       // Drop messages
    };
    enum linkStateCR {
	LPDR = 0x00,                                       // Local processor down
	LPUR = 0x01,                                       // Local processor up
	Emergency_ = 0x02,
	Emergency_c = 0x03,                                // Emergency ceases
	FlushB = 0x04,                                     // Flush buffers
	FlushTB = 0x05,                                    // Flush transmit buffers
	FlushRT = 0x06,                                    // Flush retransmit buffers
	FlushRecvB = 0x07,                                 // Flush receive buffers
	Continue = 0x08,
    };
    enum linkStateCI {
	LPD = 0x00,                                        // Local processor down
	LPU = 0x01,                                        // Local processor up
	LEC = 0x02,                                        // Link entered congestion
	PLU = 0x03,                                        // Physical layer up
	PLD = 0x04,                                        // Physical layer down
	PE = 0x06,                                         // Protocol error
	WHAL = 0x07,                                       // We have aligned the link
	WHLA = 0x08,                                       // We have lost alignment
	RTBF = 0x09,                                       // RTB full
	RTBNF = 0x0a,                                      // RTB no longer full
	NA = 0x0b,                                         // Negative acknowledgment
	RECS = 0x0c,                                       // Remote entered a congested state
	RCO = 0x0d,                                        // Remote congestion is over
	REPO = 0x0e,                                       // Remote entered processor outage
	RPOR = 0x0f,                                       // Remote processor outage has recovered
    };

    enum linkCongestion {
	UnknownC = 0x00,
	ManagementI = 0x01,
	CongestionE = 0x03,
    };
    enum protocolError {
	UnknownE = 0x00,
	AbnormalBSN = 0x02,
	AbnormalFIB = 0x03,
	CongestionD = 0x04,
    };


    SLT(const String& name, const NamedList& param);
    virtual ~SLT();
    virtual unsigned int status() const;
    void setStatus(unsigned int status);
    void setRemoteStatus(unsigned int status);
    void setReqStatus(unsigned int status);
    virtual void notify(bool up);
    virtual bool control(Operation oper, NamedList* params = 0);
    virtual bool aligned() const;
    virtual void destroyed();
    virtual void timerTick(const Time& when);
    u_int16_t get16Message(u_int8_t* msg);
    u_int32_t get32Message(u_int8_t* msg);
    virtual bool checkMessage(DataBlock& data);
    void processManagement(u_int16_t msgType, DataBlock& data);
    void processSltMessage(u_int16_t msgType, DataBlock& data);
    void buildHeader(DataBlock& data, bool management = false);
    void sendConnect(unsigned int status);
    void sendAutoConnect();
    void sendControllerR(unsigned int linkState);
    void sendManagement(unsigned int message);
    void sendDisconnect();
    void processCIndication(DataBlock& data);
    virtual bool transmitMSU(const SS7MSU& msu);
    void getStringMessage(String& tmp, DataBlock& data);
    virtual bool operational() const;
    virtual void configure(bool start);
    static inline const char* stateName(State s)
	{ return lookup((int)s,s_states); }
    inline u_int16_t channel()
	{ return m_channelId; }
    void changeState(State newState);
    static inline const char* messageType(Messages m)
	{ return lookup((int)m,s_messages); }
    static inline const char* connectType(connectM m)
	{ return lookup((int)m,s_connectM); }
    static inline const char* slinkStateCI(linkStateCI state)
	{return lookup((int)state, s_linkStateCI);}
    static inline const char* showError(errors er)
	{return lookup((int)er, s_errors, "Not Handled");}
    static inline const char* showState(State s)
	{ return lookup((int)s,s_states); }
    static inline const char* showDataRet(dataRetR d)
	{ return lookup((int)d,s_dataRetR); }
    static inline const char* slinkStateCR(linkStateCR state)
	{return lookup((int)state, s_linkStateCR);}
    static inline const char* slinkCongestion(linkCongestion state)
	{return lookup((int)state, s_linkCongestion);}
    static inline const char* sprotocolError(protocolError state)
	{return lookup((int)state, s_protocolError);}
    static SignallingComponent* create(const String& type, NamedList& params);
protected:
    unsigned int m_status;                                 // Layer status
    unsigned int m_rStatus;                                // Remote layer status
    unsigned int m_reqStatus;                              // We keep the requested status and we change it when
                                                           // receive the confirmation
    u_int16_t m_messageId;                                 // Message ID
    u_int16_t m_channelId;                                 // Channel ID
    u_int16_t m_bearerId;                                  // Bearer ID NOTE it is set to 0
    SignallingTimer m_confReqTimer;                        // The configuration request timer
    bool m_printMsg;                                       // Flag used to see if we print this layer messages
    bool m_autostart;                                      // Automatically align on resume
    static const TokenDict s_messages[];
    static const TokenDict s_connectM[];
    static const TokenDict s_errors[];
    static const TokenDict s_states[];
    static const TokenDict s_dataRetR[];
    static const TokenDict s_linkStateCR[];
    static const TokenDict s_linkStateCI[];
    static const TokenDict s_linkCongestion[];
    static const TokenDict s_protocolError[];

};

class CiscoSMModule : public Module
{
public:
    CiscoSMModule();
    ~CiscoSMModule();
    virtual void initialize();
private:
    bool m_init;
};

static CiscoSMModule plugin;
typedef GenPointer<SessionUser> UserPointer;
YSIGFACTORY2(SLT);

/**
    Class SltThread
*/

RudpThread::RudpThread(RudpSocket* rudp, Priority prio)
    : Thread("RUDP Runner",prio),
    m_rudp(rudp)
{
}

RudpThread::~RudpThread()
{
    DDebug("RudpThread",DebugAll,"RudpThread::~RudpThread() [%p]",this);
    m_rudp->setThread();
}

void RudpThread::run()
{
    if (!m_rudp)
	return;
    while (true) {
	if (m_rudp->readData())
	    Thread::check(true);
	else
	    Thread::idle(true);
    }
}

/**
    class DataSequence
*/

DataSequence::DataSequence(DataBlock& data, u_int8_t seq)
    : DataBlock(data),
      m_seq(seq), m_retransmitted(0)
{
}

DataSequence::~DataSequence()
{
}

bool DataSequence::refreshAck(u_int8_t acn)
{
    u_int8_t* ack = data(3);
    if (acn == *ack)
	return false;
    *ack = acn;
    return true;
}

/**
    class Rudp
*/



const TokenDict RudpSocket::s_RudpStates[] = {
    { "RudpUp",     RudpUp },
    { "RudpDown",   RudpDown },
    { "RudpWait",   RudpWait },
    { "RudpDead",   RudpDead },
    { 0, 0 }
};

RudpSocket::RudpSocket(SessionManager* sm)
    : Mutex(true,"RudpSocket"),
      m_sm(sm), m_thread(0), m_socket(0), m_lastError(-1),
      m_sequence(0), m_ackNum(0), m_lastAck(0), m_lastSend(0), m_retTStartSeq(0),
      m_syn(1000), m_cumAckTimer(0), m_nullTimer(0), m_retransTimer(0), m_synTimer(0),
      m_version(-1), m_haveChecksum(false), m_sendSyn(false),
      m_connId(0x208000),
      m_retransCounter(0), m_maxCumAck(0), m_queueCount(0), m_wrongChecksum(0),
      m_state(RudpDown)
{
}

RudpSocket::~RudpSocket()
{
    DDebug(&plugin,DebugAll,"RudpSocket::~RudpSocket() [%p]",this);
    Lock mylock(this);
    m_msgList.clear();
    delete m_socket;
}

void RudpSocket::kill()
{
    stopThread();
    m_socket->terminate();
    m_sm = 0;
}

void RudpSocket::changeState(RudpState newState)
{
    if (m_state == newState)
	return;
    Debug(m_sm,DebugNote,"Socket state changed: %s -> %s",
	stateName(m_state),stateName(newState));
    m_state = newState;
}

// Initialize Parameters ,initialize socket and start thread
bool RudpSocket::initialize(const NamedList& params)
{
    m_sequence = params.getIntValue("rudp_sequence",0xff & Random::random());
    if (!Modulo256::between(m_sequence,0,255)) {
	Debug(m_sm,DebugNote,"Rudp Sequence value out of bounds set to 0");
	m_sequence = 0;
    }
    m_cumAckTimer.interval(params,"rudp_cumulative",100,300,false);
    m_nullTimer.interval(params,"rudp_nulltimer",1500,2000,false);
    m_retransTimer.interval(params,"rudp_retransmission",400,600,false);
    m_synTimer.interval(params,"rudp_syntimer",900,1000,false);
    m_retransCounter = params.getIntValue("rudp_maxretrans",2);
    m_maxCumAck = params.getIntValue("rudp_maxcumulative",3);
    m_version = params.getIntValue("rudp_version",-1);
    m_haveChecksum = params.getBoolValue("rudp_checksum",false);
    m_sendSyn = params.getBoolValue("rudp_sendsyn",false);
    if (!initSocket(params)) {
	DDebug(m_sm,DebugMild,"Failed to initialize the socket");
	return false;
    }
    startThread();
    if (m_sendSyn)
	sendSyn(false);
    return true;
}

// Socket initialization
bool RudpSocket::initSocket(const NamedList& params)
{
    m_socket = new Socket(AF_INET,SOCK_DGRAM);
    String rhost = params.getValue("remote_host");
    unsigned int rport = params.getIntValue("remote_port",8060);
    if (!rhost || !rport) {
	Debug(m_sm,DebugStub, "Unable to initialize socket, remote%s%s%s is missing",
	    rhost ? "" : " host",
	    (rhost || rport) ? "" : " and",
	    rport ? "" : " port");
	return false;
    }
    String host = params.getValue("local_host","0.0.0.0");
    bool randHost = false;
    unsigned int port = params.getIntValue("local_port",rport);
    if (host == "0.0.0.0")
	randHost = true;
    SocketAddr addr(AF_INET);
    addr.host(host);
    addr.port(port);
    if (!m_socket->bind(addr)) {
	Debug(m_sm,DebugNote,"Unable to bind to %s:%u : %s",addr.host().c_str(),addr.port(),strerror(m_socket->error()));
	return false;
    }
    if (randHost && !m_socket->getSockName(addr)) {
	Debug(m_sm,DebugNote,"Error getting address: %s",strerror(m_socket->error()));
	return false;
    }
    Debug(m_sm,DebugAll,"Socket bound to: %s:%u",addr.host().c_str(),addr.port());
    addr.host(rhost);
    addr.port(rport);
    if (!m_socket->connect(addr)) {
	Debug(m_sm,DebugNote,"Unable to connect to %s:%u : %s",
		addr.host().c_str(),addr.port(),strerror(m_socket->error()));
	return false;
    }
    Debug(m_sm,DebugAll,"Socket connected to %s:%u",addr.host().c_str(),addr.port());
    return true;
}

// Call check data and verify if any timer has expired is called from readData()
void RudpSocket::checkTimeouts(u_int64_t time)
{
    if (m_state != RudpUp) {
	if (m_synTimer.timeout(time)) {
	    m_synTimer.stop();
	    sendSyn(false);
	}
	return;
    }
    checkData(false);
    if (m_retransTimer.timeout(time)) {
	m_retransTimer.stop();
	retransData();
    }
    if (m_cumAckTimer.timeout(time)) {
    	if (!checkData(true))
	    sendAck();
    }
    if (m_nullTimer.timeout(time)) {
    	if (!checkData(true))
	    sendNull();
    }
}

// Verify if exist any new data in the queue and transmit all data
// with sequence number > last sent and <= actual sequence number
bool RudpSocket::checkData(bool force)
{
    Lock mylock(this);
    if (!force && (m_queueCount >= m_msgList.count()))
	return false;
    bool sent = false;
    ObjList* obj = m_msgList.skipNull();
    for (; obj; obj = obj->skipNext()) {
	DataSequence* data = static_cast<DataSequence*>(obj->get());
	if (data && data->sequence() != m_lastSend && Modulo256::between(data->sequence(),m_lastSend,m_sequence)) {
	    sent = true;
	    if (data->refreshAck(m_ackNum) && m_haveChecksum)
		appendChecksum(static_cast<DataBlock&>(*data));
	    sendData(static_cast<DataBlock>(*data));
	    m_lastSend = data->sequence();
	}
    }
    m_queueCount = m_msgList.count();
    if (sent) {
	// Stop cumulative acknowledge timer because we send acknowledge segment with data
	m_cumAckTimer.stop();
	// Restart null timer because we had data trafic
	m_nullTimer.stop();
	m_nullTimer.start();
	if (!m_retransTimer.started()) {
	    // start retrasnsmission timer and refresh retransmission sequence number
	    m_retransTimer.start();
	    m_retTStartSeq = m_lastSend;
	}
    }
    return sent;
}

// Method called from checkTimeouts
// Retransmits all data with sequence number between last data confirmed and current sequence number
// increment retransmission counter and if reach the maximum retransmission counter notify the
// session manager and initiate reset
void RudpSocket::retransData()
{
    if (m_state != RudpUp)
	return;
    Lock mylock(this);
    ObjList* obj = m_msgList.skipNull();
    for (; obj; obj = obj->skipNext()) {
	DataSequence* data = static_cast<DataSequence*>(obj->get());
	if (data && Modulo256::between(data->sequence(),m_lastAck,m_sequence)) {
	    if (data->retransCounter() <= m_retransCounter) {
		XDebug(m_sm,DebugInfo,"Retransmission %u of data with seq %u",
		    data->retransCounter(),data->sequence());
		if (data->refreshAck(m_ackNum) && m_haveChecksum)
		    appendChecksum(static_cast<DataBlock&>(*data));
		sendData(static_cast<DataBlock&>(*data));
		data->inc();
		if (!m_retransTimer.started())
		     m_retransTimer.start();
	    } else {
		Debug(m_sm,DebugNote,"RUDP Layer down, retransmission exceeded for seq %u",data->sequence());
#ifdef DEBUG
		String aux;
		aux.hexify(data->data(),data->length(),' ');
		Debug(m_sm,DebugInfo,"Retransmission exceeded for data: %s ",aux.c_str());
#endif
		m_sm->notify(true);
		changeState(RudpDown);
		mylock.drop();
		reset();
	    }
	}
    }
}

void RudpSocket::reset()
{
    m_sequence = m_ackNum = m_lastAck = m_lastSend = m_retTStartSeq = 0;
    m_retransTimer.stop();
    m_nullTimer.stop();
    // remove all data
    removeData(255);
    if (m_sendSyn)
	sendSyn(false);
}

void RudpSocket::buildAck(DataBlock& data)
{
    u_int8_t buf[8];
    for (int i = 0;i < 8;i ++)
	buf[i] = 0;
    buf[0] = 0x40;
    if (m_haveChecksum)
	buf[1] = 8;
    else
	buf[1] = 4;
    buf[2] = m_sequence;
    buf[3] = m_ackNum;
    data.assign((void*)buf,buf[1]);
}

// Build and return or send an ack segment
void RudpSocket::sendAck()
{
    m_cumAckTimer.stop();
    if (m_state != RudpUp)
	return;
    DataBlock data;
    buildAck(data);
    if (m_haveChecksum)
	appendChecksum(data);
    String dat;
    dat.hexify(data.data(),data.length(),' ');
    sendData(data);
}

// Build a null segment and enqueue it
void RudpSocket::sendNull()
{
    DataBlock data;
    buildAck(data);
    u_int8_t* buf = data.data(0,2);
    buf[0] = 0x48;
    if (m_haveChecksum)
	appendChecksum(data);
    keepData(data,m_sequence);
    m_nullTimer.stop();
    m_nullTimer.start();
}

// helper function to store a 16 bit in big endian format
static void store16(u_int8_t* dest, u_int16_t val)
{
    dest[0] = (u_int8_t)(0xff & (val >> 8));
    dest[1] = (u_int8_t)(0xff & val);
}

// helper function to store a 32 bit in big endian format
static void store32(u_int8_t* dest, u_int32_t val)
{
    store16(dest,val >> 16);
    store16(dest+2,val & 0xffff);
}

void RudpSocket::sendSyn(bool recvSyn)
{
    if (m_version < 0)
	return;
    u_int8_t buf[30];
    for (unsigned int i = 0; i < sizeof(buf); i++)
	buf[i] = 0;
    if (!recvSyn) {
	m_synTimer.start();
	buf[0] = 0x80;
	buf[3] = 0;
    } else {
	if (m_synTimer.started())
	    m_synTimer.stop();
	buf[0] = 0xc0;
	buf[3] = m_ackNum;
    }
    buf[2] = m_sequence;
    m_syn = m_sequence;
    switch (m_version) {
	case 0:
	    buf[1] = m_haveChecksum ? 12 : 8;
	    store32(buf+4,m_connId);
	    break;
	case 1:
	    buf[1] = 30;
	    store16(buf+8,0xe447); // ???
	    store16(buf+10,0xce0c); // ???
	    store32(buf+12,m_connId);
	    store16(buf+16,0x0180); // MSS?
	    store16(buf+18,(u_int16_t)m_retransTimer.interval());
	    store16(buf+20,(u_int16_t)m_cumAckTimer.interval());
	    store16(buf+22,(u_int16_t)m_nullTimer.interval());
	    store16(buf+24,2000); // Transf. state timeout?
	    buf[26] = m_retransCounter;
	    buf[27] = m_maxCumAck;
	    buf[28] = 0x03; // Max out of seq?
	    buf[29] = 0x05; // Max auto reset?
	    break;
	default:
	    Debug(m_sm,DebugWarn,"Unhandled RUDP version %d",m_version);
	    m_version = -1;
	    return;
    }
    DataBlock data;
    data.assign((void*)buf,buf[1]);
    if (m_haveChecksum)
	appendChecksum(data);
    if (sendData(data))
	changeState(RudpWait);
}

// Method used to assign a sequence number for data and append it to message list to be send
void RudpSocket::keepData(DataBlock& data, int seq)
{
    DataSequence* seqdata = new DataSequence(data,seq);
    Lock mylock(this);
    m_msgList.append(seqdata);
    Modulo256::inc(m_sequence);
}

bool RudpSocket::readData()
{
    checkTimeouts(Time::msecNow());
    if (!m_socket)
	return false;
    if (!m_socket->valid() && State() == RudpSocket::RudpUp) {
	Debug(m_sm,DebugWarn,"RUDP socket is dead, check the network connection!");
	changeState(RudpDead);
	reset();
	return false;
    }
    bool readOk = false,error = false;
    if (!m_socket->select(&readOk,0,&error,1000))
	return false;
    if (error) {
	m_socket->updateError();
	int err = m_socket->error();
	if (err && (err != m_lastError)) {
	    m_lastError = err;
	    Debug(m_sm,DebugMild,"Selecting error: %s (%d)",strerror(err),err);
	}
    }
    if (!readOk || error)
	return false;
    unsigned char buffer [MAX_BUF_SIZE];
    SocketAddr addr;
    int r = m_socket->recv(buffer,MAX_BUF_SIZE);

    if (r < 0) {
	int err = m_socket->error();
	if (err && (err != m_lastError)) {
	    m_lastError = err;
	    Debug(m_sm,DebugMild,"Reading data error: %s (%d)",strerror(err),err);
	}
    }
    else if (r == 0)
	return false;
    m_lastError = -1;
    DataBlock packet(buffer,r);
#ifdef XDEBUG
    String seen;
    seen.hexify(packet.data(),packet.length(),' ');
    Debug(m_sm,DebugInfo,"Reading data: %s length returned = %d",seen.c_str(), r);
#endif
    // Sanity checks for packet and header length
    if (packet.length() < 4)
	return false;
    if ((unsigned int)packet.at(1) > packet.length())
	return false;
    if (m_state == RudpDown && !haveSyn((u_int8_t)packet.at(0)))
	return false;
    if (m_haveChecksum && !checkChecksum(packet)) {
	m_wrongChecksum++;
	DDebug(m_sm,DebugMild,"Wrong checksums received: %u",m_wrongChecksum);
	return false;
    }
    recvMsg(packet);
    return true;
}

bool RudpSocket::sendData(const DataBlock& msg)
{
    bool sendOk = false, error = false;
    if (!m_socket)
	return false;
    if (m_socket->select(0,&sendOk,&error,1000)) {
	if (error)
	    return false;
	if (!sendOk)
	    return false;
	int msgLen = msg.length();
	int len = m_socket->send(msg.data(),msgLen);
	if (len != msgLen) {
	    Debug(m_sm,DebugAll,"Error sending data, message not sent: %s ",strerror(m_socket->error()));
	    return false;
	}
	else {
#ifdef XDEBUG
	    String seen;
	    seen.hexify(msg.data(),msg.length(),' ');
	    XDebug(m_sm,DebugInfo,"Sending data: %s length returned = %d",seen.c_str(), msg.length());
#endif
	    return true;
	}
    }
    return false;
}

// Enqueue data received from session manager
void RudpSocket::sendMSG(const DataBlock& data)
{
    if (m_state != RudpUp)
	return;
    DataBlock auxdata;
    buildAck(auxdata);
    auxdata += data;
    if (m_haveChecksum)
	appendChecksum(auxdata);
    keepData(auxdata,m_sequence);
}

// Check received packet and send it for processing
void RudpSocket::recvMsg(DataBlock& packet)
{
    u_int8_t flag = packet.at(0);
    // SYN needs to be handled first
    if (haveSyn(flag)) {
	if (haveAck(flag)) {
	    handleSyn(packet,true);
	    m_nullTimer.start();
	    return;
	}
	handleSyn(packet,false);
	return;
    }
    bool hasData = false;
    // Then acknowledge pending packets - ACK and EACK
    if (haveAck(flag)) {
	hasData = true;
	checkAck(packet);
    }
    if (haveEack(flag))
	handleEack(packet);

    // Check for special packets
    if (haveNul(flag)) {
	hasData = false;
	if (!haveAck(flag))
	    Debug(m_sm,DebugWarn,"Received NULL segment without ACK flag set");
	checkSeq(packet);
	m_cumAckTimer.fire();
    }
    if (haveReset(flag)) {
	Debug(m_sm,DebugMild,"Received RESET segment, ignored");
	hasData = false;
	checkSeq(packet);
    }
    if (haveTcs(flag)) {
	Debug(m_sm,DebugMild,"Received TCS segment, ignored");
	hasData = false;
    }
    // If we had ACK or EACK only also check for data after header
    if (hasData)
	handleData(packet);
}

bool RudpSocket::handleSyn(const DataBlock& data, bool ack)
{
    DDebug(m_sm,DebugInfo,"Handling SYN%s with length %u",
	(ack ? "-ACK" : ""),data.length());
    if (m_version < 0) {
	switch (data.length()) {
	    case 12:
		m_version = 0;
		m_haveChecksum = true;
		break;
	    case 8:
		m_version = 0;
		m_haveChecksum = false;
		break;
	    case 30:
		m_version = 1;
		m_haveChecksum = true;
		break;
	    default:
		Debug(m_sm,DebugWarn,"Cannot guess RUDP version from SYN length %u",
		    data.length());
		return false;
	}
	Debug(m_sm,DebugNote,"Guessed RUDP version %d%s from SYN length %u",
	    m_version,(m_haveChecksum ? " (CKSUM)" : ""),data.length());
    }
    m_ackNum = data.at(2);
    if ((m_version == 1) && (data.length() >= 30)) {
	m_connId = (data.at(12) << 24) | (data.at(13) << 16) | (data.at(14) << 8) | data.at(15);
	m_retransTimer.interval((data.at(18) << 8) | data.at(19));
	m_cumAckTimer.interval((data.at(20) << 8) | data.at(21));
	m_nullTimer.interval((data.at(22) << 8) | data.at(23));
	m_retransCounter = data.at(26);
	m_maxCumAck = data.at(27);
    }
    if (ack) {
	checkAck(data);
	sendAck();
    } else
	sendSyn(true);
    return true;
}

// Check acknowledged sequence number
void RudpSocket::checkAck(const DataBlock& data)
{
    u_int8_t ack = data.at(3);
    if (ack == m_syn) {
	m_nullTimer.stop();
	m_nullTimer.start();
	changeState(RudpUp);
	m_sm->notify(false);
	m_syn = 1000;
    }
    removeData(ack);
}

// Check incoming sequence number, increment if matched
bool RudpSocket::checkSeq(const DataBlock& data)
{
    u_int8_t seq = data.at(2);
    unsigned int exp = m_ackNum;
    Modulo256::inc(exp);
    if (seq == exp) {
	m_ackNum = seq;
	if (!m_cumAckTimer.started())
	    m_cumAckTimer.start();
	return true;
    }
    // Received packet is not next in sequence - ignore it
    if (seq != m_ackNum)
	Debug(m_sm,DebugMild,"Packet out of sequence, expecting %u or %u but got %u",
	    m_ackNum,exp,seq);
    else
	Debug(m_sm,DebugNote,"Received duplicate packet %u",seq);
    return false;
}

// Remove ack data
void RudpSocket::removeData(u_int8_t ack)
{
    if (Modulo256::between(m_retTStartSeq,m_lastAck,m_sequence))
	m_retransTimer.stop();
    Lock mylock(this);
    XDebug(m_sm,DebugInfo,"Removing packets in range %u - %u",m_lastAck,ack);
    ListIterator iter(m_msgList);
     while (DataSequence* data = static_cast<DataSequence*>(iter.get())) {
	if (Modulo256::between(data->sequence(),m_lastAck,ack)) {
	    XDebug(m_sm,DebugAll,"Removed packet with seq %u",data->sequence());
	    m_msgList.remove(data,true);
	    if (m_queueCount > 0)
		m_queueCount -- ;
	    else
		m_queueCount = 0;
	}
    }
    m_lastAck = ack;
    // If queue count is bigger than 0 means that we still have packets in queue waiting to be confirmed
    if (m_queueCount > 0) {
	// set sequence timer to next in queue to ack
	m_retTStartSeq = ack;
	Modulo256::inc(m_retTStartSeq);
	// Restart Retransmission Timer
	m_retransTimer.start();
    }
}

void RudpSocket::handleEack(const DataBlock& data)
{
    u_int8_t pack = data.at(1) - (m_haveChecksum ? 8 : 4);
    DDebug(m_sm,DebugNote,"Received EACK for %u packets, last Ack %u",pack,m_lastAck);
    for (int i = 4; i < pack + 4; i++)
	removeOneData(data.at(i));
    if (!m_cumAckTimer.started())
	m_cumAckTimer.start();
}

void RudpSocket::handleData(DataBlock& data)
{
    int hdr = data.at(1);
    if (data.length() <= (unsigned int)hdr)
	return;
    if (!checkSeq(data))
	return;
    data.cut(-hdr);
    m_sm->handleData(data);
}

void RudpSocket::removeOneData(u_int8_t ack)
{
    Lock mylock(this);
    ObjList* obj = m_msgList.skipNull();
    for (; obj; obj = obj->skipNext()) {
	DataSequence* data = static_cast<DataSequence*>(obj->get());
	if (data && data->sequence() == ack) {
	    XDebug(m_sm,DebugAll,"Removed one packet with seq %u",ack);
	    m_msgList.remove(data,true);
	    if (m_queueCount > 0)
		m_queueCount --;
	    else
		m_queueCount = 0;
	    return;
	}
    }
    DDebug(m_sm,DebugInfo,"Not found packet with seq %u",ack);
}

bool RudpSocket::running()
{
    return m_thread && m_thread->running();
}

bool RudpSocket::startThread(Thread::Priority prio)
{
    if (!m_thread) {
	m_thread = new RudpThread(this,prio);
	Debug(m_sm,DebugAll,"Creating %s",m_thread->name());
    }
    if (m_thread->running()) {
	Debug(m_sm,DebugAll,"%s is already running",m_thread->name());
	return true;
    }
    if (m_thread->startup()) {
	Debug(m_sm,DebugAll,"Starting up %s",m_thread->name());
	return true;
    }
    Debug(m_sm,DebugWarn,"%s failed to start",m_thread->name());
    m_thread->cancel(true);
    m_thread = 0;
    return false;
}

void RudpSocket::stopThread()
{
    if (!m_thread)
	return;
    m_thread->cancel();
    while (m_thread)
	Thread::yield();
}

u_int16_t RudpSocket::checksum(u_int16_t len, const u_int8_t* buff)
{
    u_int32_t sum = 0;
    for (u_int16_t i = 0; i < len; i+=2)
	sum += (((u_int16_t)buff[i]) << 8) + ((i+1 < len) ? buff[i+1] : 0);
    while (sum >> 16)
	sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

bool RudpSocket::checkChecksum(DataBlock& data)
{
    u_int8_t* buf = data.data(0,data.length());
    if (!buf)
	return false;
    if (!haveEack(buf[0]) && buf[1] == 4)
	return true;
    return checksum((haveChecksum(buf[0]) ? data.length() : buf[1]),buf) == 0;
}

void RudpSocket::appendChecksum(DataBlock& data)
{
    int dataLen = data.length();
    int rudpLen = 0;
    u_int8_t* buf = data.data(0,dataLen);
    if (!buf)
	return;
    rudpLen = buf[1];
    u_int8_t* cks = buf + (rudpLen - 4);
    if (haveSyn(buf[0]) && (m_version == 1))
	cks = buf+4;
    cks[0] = cks[1] = 0;
    u_int16_t sum = checksum((haveChecksum(buf[0]) ? dataLen : rudpLen),buf);
    cks[0] = (u_int8_t)(sum >> 8);
    cks[1] = (u_int8_t)(sum & 0xff);
}

/**
    class SessionManager
*/

// Find a session by name and reference it, if it doesn't exist create a new one
SessionManager* SessionManager::get(const String& name, const NamedList* params)
{
    if (name.null())
	return 0;
    Lock lock(s_sessionMutex);
    ObjList* l = s_sessions.find(name);
    SessionManager* session = l ? static_cast<SessionManager*>(l->get()) : 0;
    lock.drop();
    if (session && !session->ref())
	session = 0;
    if (params && !session) {
	session = new SessionManager(name,*params);
	session->debugChain(&plugin);
    }
    return session;
}

const TokenDict SessionManager::s_smStates[] = {
    { "Operational",    Operational },
    { "Nonoperational", Nonoperational },
    { "Standby",        StandBy },
    { 0, 0 }
};

// Session Manager messages types NOTE we only handle PDU's
const TokenDict SessionManager::s_types[] = {
    { "Start", Start },
    { "Stop", Stop },
    { "Active", Active },
    { "Standby", Standby },
    { "Q_HOLD_I", Q_HOLD_I },
    { "Q_HOLD_R", Q_HOLD_R },
    { "Q_RESUME_I", Q_RESUME_I },
    { "Q_RESUME_R", Q_RESUME_R },
    { "Q_RESET_I", Q_RESET_I },
    { "Q_RESET_R", Q_RESET_R },
    { "Q_RESTART", Q_RESTART },
    { "PDU", PDU },
    { 0, 0 }
};

SessionManager::SessionManager(const String& name, const NamedList& param)
    : Mutex(true,"SessionManager"),
      m_socket(0), m_state(Nonoperational), m_name(name),
      m_upUsers(0), m_standbyTimer(0)
{
    debugName(0);
    debugName(m_name.c_str());
    DDebug(this,DebugNote,"Creating new session");
    Lock mylock(s_sessionMutex);
    s_sessions.append(this);
    initialize(param);
}

void SessionManager::destroyed()
{
    m_socket->kill();
    TelEngine::destruct(m_socket);
    Lock mylock(s_sessionMutex);
    s_sessions.remove(this,false);
}

SessionManager::~SessionManager()
{
    DDebug(this,DebugAll,"SessionManager::~SessionManager() [%p]",this);
    Lock mylock(s_sessionMutex);
    s_sessions.remove(this);
}

bool SessionManager::initialize(const NamedList& params)
{
    m_standbyTimer.interval(params,"send_standby",100,2500,true);
    m_socket = new RudpSocket(this);
    return m_socket->initialize(params);
}

void SessionManager::notify(bool down)
{
    if (down) {
	m_standbyTimer.stop();
	changeState(Nonoperational);
	informUser(false);
    } else {
	changeState(Operational);
	initSession();
	informUser(true);
    }
}

void SessionManager::handleData(DataBlock& data)
{
    if (data.length() < 4)
	return;
    u_int8_t* buf = data.data(0,4);
    u_int32_t smMessageType;
    smMessageType = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
    if (smMessageType == PDU)
	handlePDU(data);
    else {
#ifdef DEBUG
	String aux;
	aux.hexify(data.data(),data.length(),' ');
	Debug(this,DebugInfo,"Session data dump: %s",aux.c_str());
#endif
	handleSmMessage(smMessageType);
    }
}

// Session Initialization send standby and active messages
void SessionManager::initSession()
{
    if (!m_socket)
	return;
    u_int8_t buf[4];
    buf[0] = buf[1] = buf[2] = 0;
    DataBlock data((void*)buf,4,false);
    // Standby messages should not be sent too often
    if (m_standbyTimer.interval() &&
	(m_standbyTimer.timeout() || !m_standbyTimer.started())) {
	m_standbyTimer.start();
	// send standby message
	buf[3] = Standby;
	DDebug(this,DebugInfo,"Session manager sending: Standby");
	m_socket->sendMSG(data);
    }
    // send active message
    buf[3] = Active;
    DDebug(this,DebugInfo,"Session manager sending: Active");
    m_socket->sendMSG(data);
    data.clear(false);
}

bool SessionManager::insert(SessionUser* user)
{
    if (!user)
	return false;
    Lock mylock(this);
    m_users.append(new UserPointer(user));
    return true;
}

void SessionManager::remove(SessionUser* user)
{
    if (!user)
	return;
    Lock mylock(this);
    for (ObjList* obj = m_users.skipNull(); obj; obj = obj->skipNext()) {
	UserPointer* u = static_cast<UserPointer*>(obj->get());
	if (static_cast<SessionUser*>(*u) == user) {
	    obj->remove();
	    return;
	}
    }
}

// Send a PDU message if state is operational else return false
// We reinitialize the session if the message is a connect request and we don't have any user up
bool SessionManager::sendData(DataBlock& data, bool connectR)
{
    Lock mylock(this);
    if (!m_socket || m_state != Operational)
	return false;
    // Send Standby and Active messages if we have an connect request message and all users are down
    if (connectR && m_upUsers == 0) {
	initSession();
	DDebug(this,DebugAll,"Sending init delayed PDU data: %u bytes",data.length());
    }
    m_socket->sendMSG(data);
    return true;
}

// Inform all users about session status change
void SessionManager::informUser(bool up)
{
    Lock mylock(this);
    ObjList* obj = m_users.skipNull();
    for (; obj; obj = obj->skipNext()) {
	UserPointer* user = static_cast<UserPointer*>(obj->get());
	if (user)
	    (*user)->notify(up);
    }
}

// Method that handle the user up counter to know how many do we have up
void SessionManager::userNotice(bool up)
{
    if (up)
	m_upUsers++;
    else if (m_upUsers >= 1)
	m_upUsers--;
    else
        m_upUsers = 0;
}

// Method that looks for an user to process the message
// When the user is found we stop looking
void SessionManager::handlePDU(DataBlock& data)
{
    u_int8_t* buf = data.data(4,2);
    u_int16_t protType = 0;
    protType = (buf[0] << 8) + buf[1];
    lock();
    ListIterator iter(m_users);
    while (UserPointer* user = static_cast<UserPointer*>(iter.get())) {
	if ((*user)->protocol() != protType)
	    continue;
	unlock();
	if ((*user)->checkMessage(data))
	    return;
	lock();
    }
    unlock();
}

void SessionManager::changeState(State newState)
{
    if (m_state == newState)
	return;
    Debug(this,DebugNote,"Session state changed: %s -> %s",
	stateName(m_state),stateName(newState));
    m_state = newState;
}

// As far as we know these messages should be sent by us
void SessionManager::handleSmMessage(u_int32_t smMessageType)
{
    switch (smMessageType) {
	case Start:
	case Stop:
	case Active:
	case Standby:
	case Q_HOLD_I:
	case Q_HOLD_R:
	case Q_RESUME_I:
	case Q_RESUME_R:
	case Q_RESET_I:
	case Q_RESET_R:
	    Debug(this,DebugMild,"Received unexpected SM message %s",typeName((Type)smMessageType));
	    break;
	case Q_RESTART:
	    Debug(this,DebugAll,"Received SM message %s",typeName((Type)smMessageType));
	    break;
	default:
	    Debug(this,DebugNote,"Unknown message type = 0x%08X",smMessageType);
    }
}

/**
    class SessionUser
*/

SessionUser::SessionUser(u_int16_t protType)
    : m_protType(protType)
{
}

SessionUser::~SessionUser()
{
    DDebug(&plugin,DebugAll,"SessionUser::~SessionUser() [%p]",this);
}


/**
    class SLT
*/

const TokenDict SLT::s_messages[] = {
    { "Connect Request",                    Connect_R },
    { "Connect Confirmation",               Connect_C },
    { "Disconnect Request",                 Disconnect_R },
    { "Disconnect confirmation",            Disconnect_C },
    { "Disconnect indication",              Disconnect_I },
    { "Data Request",                       Data_Req },
    { "Data Indication",                    Data_Ind },
    { "Data retrieval request",             Data_Retrieval_R },
    { "Data retrieval confirmation",        Data_Retrieval_C },
    { "Data retrieval indication",          Data_Retrieval_I },
    { "Data retrieval message",             Data_Retrieval_M },
    { "Link state controller request",      Link_State_Controller_R },
    { "Link state controller confirmation", Link_State_Controller_C },
    { "Link state controller indication",   Link_State_Controller_I },
    { "Configuration request",              Configuration_R },
    { "Configuration confirmation",         Configuration_C },
    { "Status request",                     Status_R },
    { "Status confirmation",                Status_C },
    { "Statistic request",                  Statistic_R },
    { "Statistic confirmation",             Statistic_C },
    { "Control request",                    Control_R },
    { "Control confirmation",               Control_C },
    { "Flow control response",              Flow_Control_R },
    { "Flow control indication",            Flow_Control_I },
    { 0, 0 }
};

const TokenDict SLT::s_connectM[] = {
    { "Emergency alignment", Emergency },
    { "Normal alignment",    Normal },
    { "Power on mtp2",       Power },
    { "Start mtp2",          Start },
    { 0, 0 }
};

const TokenDict SLT::s_errors[] = {
    { "No error",     Unknown },
    { "T2 expired",   T2_expired },
    // this are the only error code known at this time
    { 0, 0 }
};

const TokenDict SLT::s_states[] = {
    { "Configured",   Configured },
    { "Waiting",      Waiting },
    { "Unconfigured", Unconfigured },
    { 0, 0 }
};

const TokenDict SLT::s_dataRetR[] = {
    { "Return the BSN",             Return },
    { "Retrieve messages from BSN", Retrieve },
    { "Drop messages",              Drop },
    { 0, 0 }
};

const TokenDict SLT::s_linkStateCR[] = {
    { "Local processor down",     LPD },
    { "Local processor up",       LPU },
    { "Emergency",                Emergency },
    { "Emergency ceases",         Emergency_c },
    { "Flush buffers",            FlushB },
    { "Flush transmit buffers",   FlushTB },
    { "Flush retransmit buffers", FlushRT },
    { "Flush receive buffers",    FlushRecvB },
    { "Continue",                 Continue },
    { 0, 0 }
};

const TokenDict SLT::s_linkStateCI[] = {
    { "Local processor down",             LPD },
    { "Local processor up",               LPU },
    { "Link entered congestion",          LEC },
    { "Physical layer up",                PLU },
    { "Physical layer down",              PLD },
    { "Protocol error",                   PE },
    { "We have aligned the link",         WHAL },
    { "We have lost alignment",           WHLA },
    { "RTB full",                         RTBF },
    { "RTB no longer full",               RTBNF },
    { "Negative acknowledgment",          NA },
    { "Remote entered a congested state", RECS },
    { "Remote congestion is over",        RCO },
    { "Remote entered processor outage",  REPO },
    { "Remote recovered from outage",     RPOR },
    { 0, 0 }
};

const TokenDict SLT::s_linkCongestion[] = {
    { "Unknown",                UnknownC },
    { "Management initiated",   ManagementI },
    { "Congestion ended",       CongestionE },
    { 0, 0 }
};

const TokenDict SLT::s_protocolError[] = {
    { "Unknown",                UnknownE },
    { "Abnormal BSN received",  AbnormalBSN },
    { "Abnormal FIB received",  AbnormalFIB },
    { "Congestion discard",     CongestionD },
    { 0, 0 }
};

SLT::SLT(const String& name, const NamedList& param)
    : SignallingComponent(param.safe("CiscoSLT"),&param,"cisco-slt"),
      SessionUser(1),
      m_status(Unconfigured), m_rStatus(OutOfService), m_reqStatus(OutOfService),
      m_messageId(1), m_channelId(0), m_bearerId(0),
      m_confReqTimer(0), m_printMsg(false), m_autostart(false)
{
#ifdef DEBUG
    String tmp;
    if (debugAt(DebugAll))
        param.dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"SLT::SLT('%s',%p) [%p]%s",
	name.c_str(),&param,this,tmp.c_str());
#endif
    m_channelId = param.getIntValue("channel",0);
    String sessionName = param.getValue("session","session");
    setName(name);
    m_session = SessionManager::get(sessionName,&param);
    if (m_session) {
	m_session->insert(this);
	m_session->deref();
    }
    m_confReqTimer.interval(param,"configuration",250,5000,true);
    m_printMsg = param.getBoolValue("printslt",false);
    m_autoEmergency = param.getBoolValue("autoemergency",true);
    m_autostart = param.getBoolValue("autostart",true);
    if (m_autostart) {
	m_reqStatus = NormalAlignment;
	if (m_session && m_session->operational())
	    configure(true);
    }
}

SLT::~SLT()
{
    DDebug(&plugin,DebugAll,"SLT::~SLT() [%p]",this);
}

void SLT::destroyed()
{
    DDebug(&plugin,DebugAll,"SLT::destroyed() [%p]",this);
    m_session->remove(this);
    RefPointer<SessionManager> tmp = m_session;
    m_session = 0;
    SignallingComponent::destroyed();
}

unsigned int SLT::status() const
{
    if (Configured != m_status || OutOfService == m_reqStatus)
	return OutOfService;
    return m_rStatus;
}

void SLT::setStatus(unsigned int status)
{
    if (status == m_status)
	return;
    DDebug(this,DebugNote,"SLT status change: %s -> %s [%p]",
	showState((State)m_status),showState((State)status),this);
    m_status = status;
}

void SLT::setRemoteStatus(unsigned int status)
{
    if (status == m_rStatus)
	return;
    DDebug(this,DebugNote,"Remote status change: %s -> %s [%p]",
	statusName(m_rStatus,true),statusName(status,true),this);
    bool old = aligned();
    m_rStatus = status;
    if (aligned() != old)
	SS7Layer2::notify();
}

void SLT::setReqStatus(unsigned int status)
{
    if (status == m_reqStatus)
	return;
    DDebug(this,DebugNote,"Request status change: %s -> %s [%p]",
	statusName(m_reqStatus,true),statusName(status,true),this);
    bool old = aligned();
    m_reqStatus = status;
    if (aligned() != old)
	SS7Layer2::notify();
}

// Process notification received from session manager
void SLT::notify(bool up)
{
    if (!up)
	setStatus(Unconfigured);
    else
	configure(true);
}

bool SLT::control(Operation oper, NamedList* params)
{
    if (params) {
	m_autoEmergency = params->getBoolValue("autoemergency",m_autoEmergency);
	m_autostart = params->getBoolValue("autostart",m_autostart);
	m_printMsg = params->getBoolValue("printslt",m_printMsg);
    }
    switch (oper) {
	case Pause:
	    setReqStatus(OutOfService);
	    sendManagement(Disconnect_R);
	    return TelEngine::controlReturn(params,true);
	case Resume:
	    if (aligned() || !m_autostart)
		return TelEngine::controlReturn(params,true);
	    // fall through
	case Align:
	    {
		bool emg = getEmergency(params);
		setReqStatus(emg ? EmergencyAlignment : NormalAlignment);
		switch (m_status) {
		    case Configured:
			sendConnect(emg ? Emergency : Normal);
			break;
		    case Waiting:
			break;
		    default:
			configure(true);
		}
	    }
	    return TelEngine::controlReturn(params,true);
	case Status:
	    return TelEngine::controlReturn(params,aligned() && m_status == Configured);
	default:
	    return false;//SignallingReceiver::control((SignallingInterface::Operation)oper,params);
    }
}

bool SLT::aligned() const
{
    return ((m_reqStatus == NormalAlignment) || (m_reqStatus == EmergencyAlignment)) &&
	((m_rStatus == NormalAlignment) || (m_rStatus == EmergencyAlignment));
}

// We have to wait for an configuration confirmation message
// if we don't receive it we resend the configuration request
void SLT::timerTick(const Time& when)
{
    SS7Layer2::timerTick(when);
    if (m_confReqTimer.timeout()) {
	sendManagement(Configuration_R);
	m_confReqTimer.stop();
	m_confReqTimer.start();
    }
}

void SLT::getStringMessage(String& tmp, DataBlock& data)
{
    String aux;
    const char* tab = "    ";
    tmp << "PDU message: " << messageType((SLT::Messages)get16Message(data.data(8,2))) << "\r\n";
    aux.hexify(data.data(),data.length(),' ');
    tmp << tab << "Data dump: " << aux << "\r\n";
    tmp << tab << "Protocol Type: " << get16Message(data.data(4,2)) << "\r\n";
    tmp << tab << "Message ID: " << get16Message(data.data(6,2)) << "\r\n";
    tmp << tab << "Channel ID: " << m_channelId << "\r\n";
    switch ((SLT::Messages)get16Message(data.data(8,2))) {
	case Connect_R:
	case Connect_C:
	    if (data.length() >= 20)
		tmp << tab << "Message Description: " << connectType((connectM)get32Message(data.data(16,4))) << "\r\n";
	    break;
	case Link_State_Controller_R:
	    if (data.length() >= 20)
		tmp << tab << "Message Description: " << slinkStateCR((linkStateCR)get32Message(data.data(16,4))) << "\r\n";
	    break;
	case Link_State_Controller_I:
	    if (data.length() >= 20)
		tmp << tab << "Message Description: " << slinkStateCI((linkStateCI)get32Message(data.data(16,4))) << "\r\n";
	    if (data.length() >= 24)
		switch (get32Message(data.data(16,4))) {
		    case LEC:
		    case PLD:
			tmp << tab << "Details: " << slinkCongestion((linkCongestion)get32Message(data.data(20,4)));
			break;
		    case PE:
			tmp << tab << "Details: " << sprotocolError((protocolError)get32Message(data.data(20,4)));
			break;
		    default:
			tmp << tab << "Error: " << showError((errors)get32Message(data.data(20,4)));
		}
	    break;
	case Disconnect_C:
	case Disconnect_I:
	    if (data.length() >= 20)
		tmp << tab << "Error: " << showError((errors)get32Message(data.data(16,4)));
	default:
	    break;
    }
    tmp << "\r\n" << " ";
}

u_int16_t SLT::get16Message(u_int8_t* msj)
{
    if (!msj)
	return 0;
    return ((msj[0] << 8) + msj[1]);
}

u_int32_t SLT::get32Message(u_int8_t* msj)
{
    if (!msj)
	return 0;
    return ((msj[0] << 24) + (msj[1] << 16) + (msj[2] << 8) + msj[3]);
}

// Check if this message is for this SLT and if is it is send for processing
bool SLT::checkMessage(DataBlock& data)
{
    if (m_status == Unconfigured)
	return false;
    u_int16_t channelId = get16Message(data.data(10,2));
    if (m_channelId != channelId)
	return false;
    if (m_printMsg) {
	String tmp;
	getStringMessage(tmp,data);
	Debug(this,DebugInfo,"Received %s",tmp.c_str());
    }
    u_int16_t msgType = get16Message(data.data(8,2));
    if (msgType == Data_Req || msgType == Data_Ind) {
	if (get16Message(data.data(14,2)) < 1) {
	    DDebug(this,DebugWarn,"Received data message with no data");
	    return true;
	}
	if (aligned()) {
	    data.cut(-16);
	    SS7MSU msu(data);
	    return receivedMSU(msu);
	} else
	    DDebug(this,DebugWarn,"Received data message while not aligned, local status = %s, remote status = %s",
		statusName(m_reqStatus,false),statusName(m_rStatus,false));
    } else if (msgType & 0x40) {
	data.cut(-16); // Management message
	processManagement(msgType,data);
    } else {
	data.cut(-16);
	processSltMessage(msgType, data);
    }
    return true;
}

// Process an management message
// NOTE we only handle configuration confirmation
void SLT::processManagement(u_int16_t msgType, DataBlock& data)
{
    switch ((Messages)msgType) {
	case Configuration_C:
	    configure(false);
	    break;
	case Status_C:
	case Statistic_C:
	case Control_C:
	case Flow_Control_R:
	case Flow_Control_I:
	    DDebug(this,DebugInfo,"Unhandled management message: %s",messageType((SLT::Messages)msgType));
	    break;
	default:
	    DDebug(this,DebugInfo,"Unknown management message 0x%04X",msgType);
    }
}



void SLT::processSltMessage(u_int16_t msgType, DataBlock& data)
{
    u_int32_t mes = get32Message(data.data(0,4));
    switch ((Messages)msgType) {
	case Connect_C:
	    if (m_reqStatus == NormalAlignment && mes == Emergency)
		sendConnect(Normal);
	    else if (m_reqStatus == EmergencyAlignment && mes == Normal)
		sendConnect(Emergency);
	    else if (m_reqStatus != EmergencyAlignment && m_reqStatus != NormalAlignment)
		sendDisconnect();
	    else {
		setRemoteStatus(mes == Normal ? NormalAlignment : EmergencyAlignment);
		if (aligned())
		    m_session->userNotice(true);
	    }
	    break;
	case Disconnect_C:
	case Disconnect_I:
	    setRemoteStatus(OutOfService);
	    sendAutoConnect();
	    break;
	case Link_State_Controller_C:
	    setRemoteStatus(m_reqStatus);
	    if (aligned())
		m_session->userNotice(false);
	    break;
	case Link_State_Controller_I:
	    processCIndication(data);
	    break;
	default:
	    const char* mes = messageType((Messages)msgType);
	    if (mes)
		DDebug(this,DebugWarn,"Received unhandled SLT message: %s",mes);
	    else
		DDebug(this,DebugWarn,"Received unknown SLT message: 0x%04X",msgType);
    }
}

// Method called to build an PDU message
void SLT::buildHeader(DataBlock& data,bool management)
{
    u_int8_t head[16];
    for (int i = 0;i < 16;i ++)
	head[i] = 0;
    head[2] = 0x80;
    head[4] = (u_int8_t)(protocol() >> 8);
    head[5] = (u_int8_t)(protocol() & 0xff);
    if (!management)
	head[7] = 1; // Message ID
    head[10] = (u_int8_t)(m_channelId >> 8);
    head[11] = (u_int8_t)(m_channelId & 0xff);
    data.append((void*)head,16);
}

void SLT::sendConnect(unsigned int status)
{
    if (m_status != Configured)
	return;
    DataBlock data;
    buildHeader(data);
    u_int8_t* header = data.data(0,16);
    header[9] = Connect_R;
    header[15] = 4;
    u_int8_t det[4];
    det[0] = det[1] = det[2] = 0;
    det[3] = status;
    data.append((void*)det,4);
    if (m_printMsg) {
	String tmp;
	getStringMessage(tmp,data);
	Debug(this,DebugInfo,"Sending %s",tmp.c_str());
    }
    m_session->sendData(data,true);
}

void SLT::sendAutoConnect()
{
    if (!m_autostart)
	return;
    if (m_reqStatus != EmergencyAlignment && m_reqStatus != NormalAlignment)
	return;
    if (m_autoEmergency)
	setReqStatus(getEmergency() ? EmergencyAlignment : NormalAlignment);
    sendConnect(m_reqStatus == EmergencyAlignment ? Emergency : Normal);
}

void SLT::sendControllerR(unsigned int linkState)
{
    DataBlock data;
    buildHeader(data);
    u_int8_t* header = data.data(0,16);
    header[9] = Link_State_Controller_R;
    header[15] = 4;
    u_int8_t det[4];
    for (int i = 0;i < 4;i ++)
	det[i] = 0;
    det[3] |= linkState;
    data.append((void*)det,4);
    if (m_printMsg) {
	String tmp;
	getStringMessage(tmp,data);
	Debug(this,DebugInfo,"Sending %s",tmp.c_str());
    }
    m_session->sendData(data);
}

void SLT::sendManagement(unsigned int message)
{
    DataBlock data;
    buildHeader(data,true);
    u_int8_t* header = data.data(0,16);
    header[9] |= message;
    if (m_printMsg) {
	String tmp;
	getStringMessage(tmp,data);
	Debug(this,DebugInfo,"Sending %s",tmp.c_str());
    }
    m_session->sendData(data,(Configuration_R == message));
}

void SLT::sendDisconnect()
{
    DataBlock data;
    buildHeader(data);
    u_int8_t* header = data.data(0,16);
    header[9] = Disconnect_R;
    if (m_printMsg) {
	String tmp;
	getStringMessage(tmp,data);
	Debug(this,DebugInfo,"Sending %s",tmp.c_str());
    }
    m_session->sendData(data);
}

void SLT::processCIndication(DataBlock& data)
{
    u_int32_t message = get32Message(data.data(0,4));
    switch ((linkStateCI)message) {
	case LPU:
	case PLU:
	case WHAL:
	case RCO:
	case RPOR:
	case RTBNF:
	    break;
	case LEC:
	case RECS:
	case RTBF:
	    if (aligned())
		m_session->userNotice(false);
	    setRemoteStatus(Busy);
	    break;
	case REPO:
	    if (aligned())
		m_session->userNotice(false);
	    setRemoteStatus(ProcessorOutage);
	    break;
	case WHLA:
	    if (aligned())
		m_session->userNotice(false);
	    setRemoteStatus(OutOfAlignment);
	    break;
	case LPD:
	case PLD:
	case PE:
	case NA:
	    if (aligned())
		m_session->userNotice(false);
	    setRemoteStatus(OutOfService);
	    break;
    }
}

bool SLT::transmitMSU(const SS7MSU& msu)
{
    if (!m_session)
	return false;
    if (!aligned()) {
	Debug(this,DebugNote,"Requested to send data while not operational");
	return false;
    }
    DataBlock data;
    buildHeader(data);
    u_int8_t* header = data.data(0,16);
    header[9] = Data_Req;
    header[15] = msu.length();
    data += msu;
    if (m_printMsg) {
	String tmp;
	getStringMessage(tmp,data);
	Debug(this,DebugInfo,"Sending %s",tmp.c_str());
    }
    return m_session->sendData(data);
}

bool SLT::operational() const
{
    return aligned();
}

void SLT::configure(bool start)
{
    if (start && m_confReqTimer.interval()) {
	sendManagement(Configuration_R);
	m_confReqTimer.start();
	setStatus(Waiting);
	return;
    }
    m_confReqTimer.stop();
    setStatus(Configured);
    SS7Layer2::notify();
    DDebug(this,DebugInfo,"requested status = %s",statusName(m_reqStatus,false));
    sendAutoConnect();
}

SignallingComponent* SLT::create(const String& type, NamedList& name)
{
    if (type != "SS7Layer2")
	return 0;
    const String* module = name.getParam("module");
    if (module && *module != "ciscosm")
	return 0;
    TempObjectCounter cnt(plugin.objectsCounter());
    Configuration cfg(Engine::configFile("ciscosm"));
    const char* sectName = name.getValue("link",name);
    NamedList* layer = cfg.getSection(sectName);
    if (!name.getBoolValue(YSTRING("local-config"),false)) {
	const String* ty = name.getParam(YSTRING("type"));
	if (ty) {
	    if (*ty == YSTRING("cisco-slt"))
		layer = &name;
	    else
		return 0;
	} else	if (module)
	    layer = &name;
	else {
	    Debug("CiscoSM",DebugConf,
		  "Ambiguous request! Requested to create a layer2 with external config, but no module param is present!");
	    return 0;
	}
    } else if (!layer) {
	DDebug("CiscoSM",DebugConf,"No section %s in configuration file!", sectName);
	return 0;
    } else
	name.copyParams(*layer);

    NamedList* session = 0;
    NamedList params("");
    if (resolveConfig(YSTRING("session"),params,&name)) {
	if (!params.getBoolValue(YSTRING("local-config"),false))
	    session = &params;
    }
    if (!session) {
	String ses = name.getValue(YSTRING("session"),"session");
	session = cfg.getSection(ses);
    }
    if (!session) {
	Debug("CiscoSLT",DebugConf,"Session config could not be resolved!");
	return 0;
    }
    layer->copyParams(*session);
    return new SLT(name,*layer);
}

/**
    class CiscoSMModule
*/

CiscoSMModule::CiscoSMModule()
    : Module("ciscosm","misc",true),
      m_init(false)
{
    Output("Loaded module Cisco SM");
}

CiscoSMModule::~CiscoSMModule()
{
    Output("Unloading module Cisco SM");
    s_sessions.clear();
}

void CiscoSMModule::initialize()
{
    Output("Initializing module Cisco SM");

    Configuration cfg(Engine::configFile("ciscosm"));
    if (!m_init) {
	m_init = true;
	setup();
    }
    Lock lock(s_sessionMutex);
    ObjList* obj = s_sessions.skipNull();
    for (; obj; obj =  obj->skipNext()) {
	SessionManager* ses = static_cast<SessionManager*>(obj->get());
	if (ses->socket()->State() == RudpSocket::RudpDead)
	    ses->socket()->initSocket(*(cfg.getSection(ses->toString())));
    }
}

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow) {
	if (!s_sessionMutex.lock(500000))
	    return false;
	bool ok = (s_sessions.count() == 0);
	s_sessionMutex.unlock();
	return ok;
    }
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
