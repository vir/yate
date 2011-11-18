/**
 * ysipchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Sip Channel
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
#include <yatesip.h>
#include <yatesdp.h>

#include <string.h>


using namespace TelEngine;
namespace { // anonymous

class YateSIPListener;                   // Base class for listeners (need binding)
class YateSIPPartyHolder;                // A SIPParty holder
class YateSIPTransport;                  // SIP transport: keeps a socket, read/send data
class YateSIPUDPTransport;               // UDP transport
class YateSIPTCPTransport;               // TCP/TLS transport
class YateSIPTransportWorker;            // A transport worker
class YateSIPTCPListener;                // A TCP listener
class YateUDPParty;                      // A SIP UDP party
class YateTCPParty;                      // A SIP TCP/TLS party
class YateSIPEngine;                     // The SIP engine
class YateSIPLine;                       // A line
class YateSIPEndPoint;                   // Endpoint processor
class SIPDriver;

#define EXPIRES_MIN 60
#define EXPIRES_DEF 600
#define EXPIRES_MAX 3600

// TCP transport idle values in seconds
// Outgoing: interval to send keep alive
// Incoming: interval allowed to stay with refcounter=1 and no data received/sent
#define TCP_IDLE_MIN 32
#define TCP_IDLE_DEF 120
#define TCP_IDLE_MAX 600

// Maximum allowed value for bind retry interval in milliseconds
// 1 minute
#define BIND_RETRY_MAX 60000

static TokenDict dict_errors[] = {
    { "incomplete", 484 },
    { "noroute", 404 },
    { "noroute", 604 },
    { "noconn", 503 },
    { "noconn", 408 },
    { "noauth", 401 },
    { "nomedia", 415 },
    { "nocall", 481 },
    { "busy", 486 },
    { "busy", 600 },
    { "noanswer", 487 },
    { "rejected", 406 },
    { "rejected", 606 },
    { "forbidden", 403 },
    { "forbidden", 603 },
    { "offline", 404 },
    { "congestion", 480 },
    { "unallocated", 410 },
    { "failure", 500 },
    { "pending", 491 },
    { "looping", 483 },
    { "timeout", 408 },
    { "timeout", 504 },
    { "service-not-implemented", 501 },
    { "unimplemented", 501 },
    { "service-unavailable", 503 },
    { "noresource", 503 },
    { "interworking", 500 },
    { "interworking", 400 },
    { "invalid-message", 400 },
    { "protocol-error", 400 },
    {  0,   0 },
};

static const char s_dtmfs[] = "0123456789*#ABCDF";

static TokenDict info_signals[] = {
    { "*", 10 },
    { "#", 11 },
    { "A", 12 },
    { "B", 13 },
    { "C", 14 },
    { "D", 15 },
    {  0,   0 },
};

// Protocol definition
class ProtocolHolder
{
public:
    enum Protocol {
	Unknown = 0,
	Udp,
	Tcp,
	Tls
    };
    inline ProtocolHolder(int p)
	: m_proto(p)
	{}
    // Retrieve protocol
    inline int protocol() const
	{ return m_proto; }
    inline const char* protoName(bool upperCase = true) const
	{ return lookupProtoName(protocol(),upperCase); }
    static inline const char* lookupProtoName(int proto, bool upperCase = true)
	{ return lookup(proto,upperCase ? s_protoUC : s_protoLC); }
    static inline int lookupProto(const char* name, bool upperCase = true,
	int def = Unknown)
	{ return lookup(name,upperCase ? s_protoUC : s_protoLC,def); }
    static inline int lookupProtoAny(const String& name, int def = Unknown) {
	    String tmp = name;
	    return lookupProto(tmp.toLower(),false,def);
	}
    static const TokenDict s_protoLC[];  // Lower case proto name
    static const TokenDict s_protoUC[];  // Upper case proto name
protected:
    int m_proto;
private:
    ProtocolHolder() {}                  // No default
};

// A SIP party holder
class YateSIPPartyHolder : public ProtocolHolder
{
public:
    inline YateSIPPartyHolder(Mutex* mutex = 0)
	: ProtocolHolder(Udp),
	m_party(0), m_partyMutex(mutex), m_transLocalPort(0), m_transRemotePort(0)
	{}
    virtual ~YateSIPPartyHolder()
	{ setParty(); }
    // Retrieve a referrenced pointer to the held party
    inline SIPParty* party() {
	    Lock lock(m_partyMutex);
	    return (m_party && m_party->ref()) ? m_party : 0;
	}
    // Retrieve the transport from party
    YateSIPTransport* transport(bool ref = false);
    // Check if a transport is used by our party
    inline bool isTransport(YateSIPTransport* trans)
	{ return trans == transport(); }
    // Set the held party. Referrence it before
    void setParty(SIPParty* party = 0);
    // Set the party of a non answer message. Return true on success
    bool setSipParty(SIPMessage* message, const YateSIPLine* line = 0,
	bool useEp = false, const char* host = 0, int port = 0) const;
    // (Re)Build party. Return true on success
    bool buildParty(bool force = true);
    // Change party and its transport if the parameter list contains a transport
    // Set force to true to try building a party anyway
    // Return true on success
    bool setParty(const NamedList& params, bool force,
	const String& prefix = String::empty(),
	const String& defRemoteAddr = String::empty(), int defRemotePort = 0);
    // Transport status changed notification
    virtual void transportChangedStatus(int stat, const String& reason)
	{}
protected:
    // Change a parameter, notify descendents
    bool change(String& dest, const String& src);
    bool change(int& dest, int src);
    // Changing notification for descendents
    virtual void changing();
    // Update transport type. Return true if changed
    bool updateProto(const NamedList& params, const String& prefix = String::empty());
    // Update transport remote addr/port. Return true if changed
    bool updateRemoteAddr(const NamedList& params, const String& prefix = String::empty(),
	const String& defRemoteAddr = String::empty(), int defRemotePort = 0);
    // Update transport local addr/port. Return true if changed
    bool updateLocalAddr(const NamedList& params, const String& prefix = String::empty());
    // Update RTP local address
    void setRtpLocalAddr(String& addr, Message* m = 0);

    SIPParty* m_party;                   // Held party
    Mutex* m_partyMutex;                 // Mutex protecting the party pointer
    // Data used to (re)build the transport
    String m_transId;
    String m_transLocalAddr;
    int m_transLocalPort;
    String m_transRemoteAddr;
    int m_transRemotePort;
};

// Base class for listeners (need binding)
class YateSIPListener
{
public:
    YateSIPListener(const String& addr = String::empty(), int port = 0);
    inline const String& address() const
	{ return m_address; }
    inline int port() const
	{ return m_port; }
    // Check bind now flag
    bool bindNow(Mutex* mutex);
    // Check if address would change
    bool addrWouldChange(Mutex* mutex, bool udp, const String& addr, int port);
    // Set addr/port and bind flag. Return the bind flag
    bool setAddr(const String& addr, int port);
    // Initialize a socket
    // Set m_addr
    Socket* initSocket(int proto, const String& name, SocketAddr& addr, Mutex* mutex,
	int backLogBuffer, bool forceBind, String& reason);
protected:
    unsigned int m_bindInterval;         // Interval to try binding
    u_int64_t m_nextBind;                // Next time to bind
    bool m_bind;                         // Re-bind flag
    String m_address;                    // Address to bind
    int m_port;                          // Port to bind
};

// SIP transport: keeps a socket, read data from it, send data through it
class YateSIPTransport : public Mutex, public RefObject, public ProtocolHolder
{
    YCLASS(YateSIPTransport,RefObject);
    YNOCOPY(YateSIPTransport);
    friend class SIPDriver;
    friend class YateSIPEndPoint;
    friend class YateSIPTransportWorker;
public:
    enum Status {
	Idle = 0,
	Connected,
	Terminating,
	Terminated
    };
    // (Re)Initialize the transport
    bool init(const NamedList& params, const NamedList& defs, bool first,
	Thread::Priority prio = Thread::Normal);
    // Retrieve status
    inline int status() const
	{ return m_status; }
    // Check if valid (connected)
    inline bool valid() const
	{ return status() == Connected; }
    // Retrieve local address for this transport
    // This method is not thread safe for outgoing TCP
    inline const SocketAddr& local() const
	{ return m_local; }
    // Retrieve remote address for this transport
    // This method is not thread safe for outgoing TCP
    inline const SocketAddr& remote() const
	{ return m_remote; }
    // Safely retrieve RTP local address
    inline void rtpAddr(String& buf) {
	    Lock lock(this);
	    buf = m_rtpLocalAddr;
	}
    // Print sent messages to output
    void printSendMsg(const SIPMessage* msg, const SocketAddr* addr = 0);
    // Print received messages to output
    // For TCP transports the function will assume 'buf' is not null terminated
    void printRecvMsg(const char* buf, int len);
    // Add transport data yate message
    void fillMessage(Message& msg, bool addRoute = false);
    // Transport descendents
    virtual YateSIPUDPTransport* udpTransport()
	{ return 0; }
    virtual YateSIPTCPTransport* tcpTransport()
	{ return 0; }
    // Stop the worker. Change status
    void terminate(const char* reason = 0);
    // Process data (read/send).
    // Return 0 to continue processing, positive to sleep (usec),
    //  negative to terminate and destroy
    virtual int process() = 0;
    // Retrieve the transport id
    virtual const String& toString() const;
    // Reset and delete a socket
    static void resetSocket(Socket*& sock, int linger);
    // Status names
    static inline const char* statusName(int stat, const char* defVal = "Unknown")
	{ return lookup(stat,s_statusName,defVal); }
    static const TokenDict s_statusName[];
protected:
    YateSIPTransport(int proto, const String& id, Socket* sock, int stat = Connected);
    virtual void destroyed();
    // Status changed notification for descendents
    virtual void statusChanged()
	{}
    // Change transport status. Notify it
    void changeStatus(int stat);
    // Handle received messages, set party, add to engine
    // Consume the message
    void receiveMsg(SIPMessage*& msg);
    // Print socket read error to output
    void printReadError();
    // Print socket write error to output
    void printWriteError(int res, unsigned int len);
    // Set m_protoAddr from local/remote ip/port or reset it
    void setProtoAddr(bool set);

    String m_id;                         // Transport id
    int m_status;                        // Transport status
    unsigned int m_statusChgTime;        // Last status changed time (seconds)
    String m_reason;                     // Termination reason
    Socket* m_sock;                      // The socket
    unsigned int m_maxpkt;               // Max receive packet length
    DataBlock m_buffer;                  // Read buffer
    SocketAddr m_local;                  // Local ip/port
    SocketAddr m_remote;                 // Remote ip/port
    String m_rtpLocalAddr;               // RTP local address
    YateSIPTransportWorker* m_worker;    // Transport worker
    bool m_initialized;                  // Flag reset when initializing by the module and set in init()
    String m_protoAddr;                  // Proto + addr: used for debug (send/recv msg)
private:
    YateSIPTransport() : ProtocolHolder(Udp) {} // No default constructor
};

// UDP transport
class YateSIPUDPTransport : public YateSIPTransport, public YateSIPListener
{
    YCLASS(YateSIPUDPTransport,YateSIPTransport);
    friend class YateSIPTransport;
public:
    YateSIPUDPTransport(const String& id);
    inline bool isDefault() const
	{ return m_default; }
    virtual YateSIPUDPTransport* udpTransport()
	{ return this; }
    // (Re)Initialize the transport
    bool init(const NamedList& params, const NamedList& defs, bool first,
	Thread::Priority prio = Thread::Normal);
    // Send data
    void send(const void* data, unsigned int len, const SocketAddr& addr);
    // Process data (read)
    virtual int process();
protected:
    bool m_default;
    bool m_forceBind;
    int m_bufferReq;
};

// TCP/TLS transport
class YateSIPTCPTransport : public YateSIPTransport
{
    YCLASS(YateSIPTCPTransport,YateSIPTransport);
    friend class YateTCPParty;
public:
    // Build an outgoing transport
    YateSIPTCPTransport(bool tls, const String& laddr, const String& raddr, int rport);
    // Build an incoming transport
    YateSIPTCPTransport(Socket* sock, bool tls);
    inline bool outgoing() const
	{ return m_outgoing; }
    inline bool tls() const
	{ return protocol() == Tls; }
    inline const String& remoteAddr() const
	{ return m_remoteAddr; }
    inline int remotePort() const
	{ return m_remotePort; }
    // Safely return a reference to party
    YateTCPParty* getParty();
    virtual YateSIPTCPTransport* tcpTransport()
	{ return this; }
    // (Re)Initialize the transport
    bool init(const NamedList& params, bool first, Thread::Priority prio = Thread::Normal);
    // Set flow timer flag and idle interval (in seconds)
    // Reset idle timeout
    void setFlowTimer(bool on, unsigned int interval);
    // Send an event
    void send(SIPEvent* event);
    // Process data (read/send)
    virtual int process();
protected:
    virtual void destroyed();
    // Status changed notification
    virtual void statusChanged();
    // Reset transport's party
    void resetParty(YateTCPParty* party, bool set);
    // Connect an outgoing transport. Terminate the socket before it
    // Return: 1: OK, 0: retry connect, -1: stop the transport
    int connect(u_int64_t connToutUs = 60000000);
    // Send pending messages or keepalive, return false on failure
    bool sendPending(const Time& time, bool& sent);
    // Read data
    bool readData(const Time& time, bool& read);
    // Reset socket and connection related data
    void resetConnection(Socket* sock = 0);
    // Set transport idle timeout
    void setIdleTimeout(u_int64_t time = Time::now());
    // Send keep alive (or response to keep alive)
    bool sendKeepAlive(bool request);
    // Method called on buffer overflow.
    // Reset connection. Return false
    bool overflow(unsigned int msglen);

    bool m_outgoing;                     // Direction
    YateTCPParty* m_party;               // Transport party
    ObjList m_queue;                     // Pending message queue
    int m_sent;                          // Sent bytes from first message in queue
                                         // -1 to dequeue a new message and print it
    unsigned int m_idleInterval;         // Incoming: interval allowed to stay with a reference
                                         //  counter=1 without receiving any data
                                         // Outgoing: keep alive interval
    u_int64_t m_idleTimeout;             // Idle timeout: check state or send keep alive
    bool m_flowTimer;                    // Flow timer flag (RFC5626)
    bool m_keepAlivePending;             // Pending keep alive response
    SIPMessage* m_msg;                   // Partially received SIP message (expecting body)
    String m_sipBuffer;                  // Accumulated read data
    unsigned int m_sipBufOffs;           // Offset in sip buffer for partial sip message
    unsigned int m_contentLen;           // Expected content length for partial sip message
    // Outgoing (re-connect info)
    String m_remoteAddr;                 // Remote party address
    int m_remotePort;                    // Remote port
    String m_localAddr;                  // Optional local addrress to bind to
    unsigned int m_connectRetry;         // Number of re-connect
    u_int64_t m_nextConnect;             // Interval to try ro re-connect
};

// Transport worker
class YateSIPTransportWorker : public Thread
{
    friend class YateSIPTransport;
public:
    YateSIPTransportWorker(YateSIPTransport* trans, Thread::Priority prio);
    ~YateSIPTransportWorker();
    virtual void run();
private:
    void cleanupTransport(bool final, bool terminate = false);
    YateSIPTransport* m_transport;
};

class YateSIPTCPListener : public Thread, public String, public ProtocolHolder, public YateSIPListener
{
    friend class SIPDriver;
    friend class YateSIPEndPoint;
public:
    YateSIPTCPListener(int proto, const String& name, const NamedList& params);
    ~YateSIPTCPListener();
    void init(const NamedList& params, bool first);
    inline bool tls() const
	{ return protocol() == Tls; }
    inline bool listening() const
	{ return m_socket != 0; }
    inline void setReason(const char* reason) {
	    if (!reason)
		return;
	    Lock lck(m_mutex);
	    m_reason = reason;
	}
    virtual void run();
private:
    // Close the socket. Remove from endpoint list
    void cleanup(bool final);
    // Reset socket
    void stopListening(const char* reason = 0, int level = DebugNote);

    Mutex m_mutex;                       // Mutex protecting transport parameters and bind ip/port
    String m_reason;                     // Last error (state change) string
    bool m_sslContextChanged;            // SSL context changed flag
    bool m_transParamsChanged;           // Transport parameters changed flag
    Socket* m_socket;                    // The socket
    unsigned int m_backlog;              // Pending connections queue length
    String m_sslContext;                 // SSL/TLS context
    NamedList m_transParams;             // Parameters for created transports
    bool m_initialized;                  // Flag reset when initializing by the module and set in init()
};

class YateUDPParty : public SIPParty
{
public:
    YateUDPParty(YateSIPUDPTransport* trans, const SocketAddr& addr, int* localPort = 0,
	const char* localAddr = 0);
    ~YateUDPParty();
    inline const SocketAddr& addr() const
	{ return m_addr; }
    virtual void transmit(SIPEvent* event);
    virtual const char* getProtoName() const;
    virtual bool setParty(const URI& uri);
    virtual void* getTransport();
    // Get an object from this one
    virtual void* getObject(const String& name) const;
protected:
    YateSIPUDPTransport* m_transport;
    SocketAddr m_addr;
};

class YateTCPParty : public SIPParty
{
public:
    YateTCPParty(YateSIPTCPTransport* trans);
    ~YateTCPParty();
    virtual void transmit(SIPEvent* event);
    virtual const char* getProtoName() const;
    virtual bool setParty(const URI& uri);
    virtual void* getTransport();
    // Get an object from this one
    virtual void* getObject(const String& name) const;
    // Update party local/remote addr/port from transport
    void updateAddrs();
protected:
    virtual void destroyed();
    YateSIPTCPTransport* m_transport;
};

class SipHandler;

class YateSIPEngine : public SIPEngine
{
public:
    YateSIPEngine(YateSIPEndPoint* ep);
    // Initialize the engine
    void initialize(NamedList* params);
    virtual bool buildParty(SIPMessage* message);
    virtual bool checkUser(const String& username, const String& realm, const String& nonce,
	const String& method, const String& uri, const String& response,
	const SIPMessage* message, GenObject* userData);
    virtual SIPTransaction* forkInvite(SIPMessage* answer, SIPTransaction* trans);
    // Transport status changed notification
    void transportChangedStatus(YateSIPTransport* trans, int stat, const String& reason);
    // Check if the engine has an active transaction using a given transport
    bool hasActiveTransaction(YateSIPTransport* trans);
    // Check if the engine has pending transactions
    bool hasInitialTransaction();
    // Clear transactions
    inline void clearTransactions() {
	    Lock lck(this);
	    m_transList.clear();
	}
    inline bool prack() const
	{ return m_prack; }
    inline bool info() const
	{ return m_info; }
private:
    static bool copyAuthParams(NamedList* dest, const NamedList& src, bool ok = true);
    YateSIPEndPoint* m_ep;
    bool m_prack;
    bool m_info;
    bool m_fork;
};

class YateSIPLine : public String, public Mutex, public YateSIPPartyHolder
{
    YCLASS(YateSIPLine,String)
public:
    YateSIPLine(const String& name);
    virtual ~YateSIPLine();
    void setupAuth(SIPMessage* msg) const;
    SIPMessage* buildRegister(int expires) const;
    void login();
    void logout(bool sendLogout = true, const char* reason = 0);
    bool process(SIPEvent* ev);
    void timer(const Time& when);
    bool update(const Message& msg);
    // Transport status changed notification
    virtual void transportChangedStatus(int stat, const String& reason);
    inline const String& getLocalAddr() const
	{ return m_localAddr; }
    inline const String& getPartyAddr() const
	{ return m_partyAddr ? m_partyAddr : m_transRemoteAddr; }
    inline int getLocalPort() const
	{ return m_localPort; }
    inline int getPartyPort() const
	{ return m_partyPort ? m_partyPort : m_transRemotePort; }
    inline bool localDetect() const
	{ return m_localDetect; }
    inline const String& getFullName() const
	{ return m_display; }
    inline const String& getUserName() const
	{ return m_username; }
    inline const String& getAuthName() const
	{ return m_authname ? m_authname : m_username; }
    inline const String& regDomain() const
	{ return m_registrar ? m_registrar : m_transRemoteAddr; }
    inline const String& domain() const
	{ return m_domain ? m_domain : regDomain(); }
    inline const char* domain(const char* defDomain) const
	{ return m_domain ? m_domain.c_str() :
	    (TelEngine::null(defDomain) ? regDomain().c_str() : defDomain); }
    inline bool valid() const
	{ return m_valid; }
    inline bool marked() const
	{ return m_marked; }
    inline void marked(bool mark)
	{ m_marked = mark; }
private:
    void clearTransaction();
    void detectLocal(const SIPMessage* msg);
    void keepalive();
    void setValid(bool valid, const char* reason = 0, const char* error = 0);
    virtual void changing();

    String m_registrar;
    String m_username;
    String m_authname;
    String m_password;
    String m_domain;
    String m_display;
    u_int64_t m_resend;
    u_int64_t m_keepalive;
    int m_interval;
    int m_alive;
    int m_flags;
    SIPTransaction* m_tr;
    bool m_marked;
    bool m_valid;
    String m_callid;
    String m_localAddr;
    String m_partyAddr;
    int m_localPort;
    int m_partyPort;
    bool m_localDetect;
    bool m_keepTcpOffline;               // Don't reset party when offline
};

class YateSIPEndPoint : public Thread
{
    friend class SIPDriver;
    friend class YateSIPTCPListener;
public:
    YateSIPEndPoint(Thread::Priority prio = Thread::Normal,
	unsigned int partyMutexCount = 5);
    ~YateSIPEndPoint();
    bool Init(void);
    void run(void);
    bool incoming(SIPEvent* e, SIPTransaction* t);
    void invite(SIPEvent* e, SIPTransaction* t);
    void regReq(SIPEvent* e, SIPTransaction* t);
    void regRun(const SIPMessage* message, SIPTransaction* t);
    void options(SIPEvent* e, SIPTransaction* t);
    bool generic(SIPEvent* e, SIPTransaction* t);
    bool buildParty(SIPMessage* message, const char* host = 0, int port = 0, const YateSIPLine* line = 0);
    inline void addTcpTransport(YateSIPTCPTransport* trans) {
	    if (!trans)
		return;
	    Lock lock(m_mutex);
	    m_transports.append(trans)->setDelete(false);
	}
    // Retrieve the default transport. Return a referrenced object
    inline YateSIPUDPTransport* defTransport() {
	    Lock lock(m_mutex);
	    return (m_defTransport && m_defTransport->ref()) ? m_defTransport : 0;
	}
    // (re)set default UDP transport
    void updateDefUdpTransport();
    // Retrieve a transport by name (name can be a prefix).
    // Return a referrenced object
    YateSIPTransport* findTransport(const String& name);
    // Retrieve an UDP transport. Return a referrenced object
    YateSIPUDPTransport* findUdpTransport(const String& name);
    // Retrieve an UDP transport by addr/port. Return a referrenced object
    YateSIPUDPTransport* findUdpTransport(const String& addr, int port);
    // Build or delete an UDP transport (re-init existing). Start the thread
    bool setupUdpTransport(const String& name, bool enabled, const NamedList& params,
	const NamedList& defs = NamedList::empty(), const char* reason = 0);
    // Delete an UDP transport
    bool removeUdpTransport(const String& name, const char* reason);
    // Remove a transport from list without deleting it. Notify termination.
    // Return true if found
    bool removeTransport(YateSIPTransport* trans, bool updDef = true);
    // Clear all transports
    void clearUdpTransports(const char* reason);
    // Transport status changed notification
    void transportChangedStatus(YateSIPTransport* trans, int stat, const String& reason);
    // Build or delete a TCP listener. Start the thread
    bool setupListener(int proto, const String& name, bool enabled, const NamedList& params);
    // Remove a listener from list without deleting it. Return true if found
    bool removeListener(YateSIPTCPListener* listener);
    // Remove a listener from list. Remove all if name is empty. Wait for termination
    void cancelListener(const String& name = String::empty(), const char* reason = 0);
    // This method is called by the driver when start/end initializing
    void initializing(bool start);
    inline YateSIPEngine* engine() const
	{ return m_engine; }
    inline void incFailedAuths() 
	{ m_failedAuths++; }
    inline unsigned int failedAuths()
    {
	unsigned int tmp = m_failedAuths;
	m_failedAuths = 0;
	return tmp;
    }
    inline unsigned int timedOutTrs()
    {
	unsigned int tmp = m_timedOutTrs;
	m_timedOutTrs = 0;
	return tmp;
    }
    inline unsigned int timedOutByes()
    {
	unsigned int tmp = m_timedOutByes;
	m_timedOutByes = 0;
	return tmp;
    }
    MutexPool m_partyMutexPool;          // SIPParty mutex pool
    // Check if data is allowed to be read from socket(s) and processed
    static bool canRead();
    static int s_evCount;
private:
    YateSIPEngine *m_engine;
    Mutex m_mutex;                       // Protect transports and listeners
    ObjList m_transports;                // All transports (non UDP are not owned)
    YateSIPUDPTransport* m_defTransport; // Default transport (pointer to object in m_transports)
    ObjList m_listeners;                 // Listeners list

    unsigned int m_failedAuths;
    unsigned int m_timedOutTrs;
    unsigned int m_timedOutByes;
};

// Handle transfer requests
// Respond to the enclosed transaction
class YateSIPRefer : public Thread
{
public:
    YateSIPRefer(const String& transferorID, const String& transferredID,
	Driver* transferredDrv, Message* msg, SIPMessage* sipNotify,
	SIPTransaction* transaction);
    virtual void run(void);
    virtual void cleanup(void)
	{ release(true); }
private:
    // Respond the transaction and deref() it
    void setTrResponse(int code);
    // Set transaction response. Send the notification message. Notify the
    // connection and release other objects
    void release(bool fromCleanup = false);

    String m_transferorID;           // Transferor channel's id
    String m_transferredID;          // Transferred channel's id
    Driver* m_transferredDrv;        // Transferred driver's pointer
    Message* m_msg;                  // 'call.route' message
    SIPMessage* m_sipNotify;         // NOTIFY message to send the result
    int m_notifyCode;                // The result to send with NOTIFY
    SIPTransaction* m_transaction;   // The transaction to respond to
    int m_rspCode;                   // The transaction response
};

class YateSIPRegister : public Thread
{
public:
    inline YateSIPRegister(YateSIPEndPoint* ep, SIPMessage* message, SIPTransaction* t)
	: Thread("YSIP Register"),
	  m_ep(ep), m_msg(message), m_tr(t)
	{ }
    virtual void run()
	{ m_ep->regRun(m_msg,m_tr); }
private:
    YateSIPEndPoint* m_ep;
    RefPointer<SIPMessage> m_msg;
    RefPointer<SIPTransaction> m_tr;
};

class YateSIPConnection : public Channel, public SDPSession, public YateSIPPartyHolder
{
    friend class SipHandler;
    YCLASS(YateSIPConnection,Channel)
public:
    enum {
	Incoming = 0,
	Outgoing = 1,
	Ringing = 2,
	Established = 3,
	Cleared = 4,
    };
    enum {
	ReinviteNone,
	ReinvitePending,
	ReinviteRequest,
	ReinviteReceived,
    };
    YateSIPConnection(SIPEvent* ev, SIPTransaction* tr);
    YateSIPConnection(Message& msg, const String& uri, const char* target = 0);
    ~YateSIPConnection();
    virtual void destroyed();
    virtual void complete(Message& msg, bool minimal=false) const;
    virtual void disconnected(bool final, const char *reason);
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool msgUpdate(Message& msg);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    void startRouter();
    bool process(SIPEvent* ev);
    bool checkUser(SIPTransaction* t, bool refuse = true);
    void doBye(SIPTransaction* t);
    void doCancel(SIPTransaction* t);
    bool doInfo(SIPTransaction* t);
    void doRefer(SIPTransaction* t);
    void reInvite(SIPTransaction* t);
    void hangup();
    inline const SIPDialog& dialog() const
	{ return m_dialog; }
    inline void setStatus(const char *stat, int state = -1)
	{ status(stat); if (state >= 0) m_state = state; }
    inline void setReason(const char* str = "Request Terminated", int code = 487)
	{ m_reason = str; m_reasonCode = code; }
    inline SIPTransaction* getTransaction() const
	{ return m_tr; }
    inline const String& callid() const
	{ return m_callid; }
    inline const String& user() const
	{ return m_user; }
    inline int getPort() const
	{ return m_port; }
    inline const String& getLine() const
	{ return m_line; }
    inline void referTerminated()
	{ m_referring = false; }
    inline bool isDialog(const String& callid, const String& fromTag,
	const String& toTag) const
	{ return callid == m_dialog &&
	    m_dialog.fromTag(isOutgoing()) == fromTag &&
	    m_dialog.toTag(isOutgoing()) == toTag; }
    // Build and add a callid parameter to a list
    static inline void addCallId(NamedList& nl, const String& dialog,
	const String& fromTag, const String& toTag) {
	    String tmp;
	    tmp << "sip/" << dialog << "/" << fromTag << "/" << toTag;
	    nl.addParam("callid",tmp);
	}

protected:
    virtual Message* buildChanRtp(RefObject* context);
    MimeSdpBody* createProvisionalSDP(Message& msg);
    virtual void mediaChanged(const SDPMedia& media);
    virtual void endDisconnect(const Message& msg, bool handled);

private:
    virtual void statusParams(String& str);
    void clearTransaction();
    void detachTransaction2();
    void startPendingUpdate();
    bool processTransaction2(SIPEvent* ev, const SIPMessage* msg, int code);
    SIPMessage* createDlgMsg(const char* method, const char* uri = 0);
    void emitUpdate();
    bool emitPRACK(const SIPMessage* msg);
    bool startClientReInvite(Message& msg, bool rtpForward);
    // Build the 'call.route' and NOTIFY messages needed by the transfer thread
    bool initTransfer(Message*& msg, SIPMessage*& sipNotify, const SIPMessage* sipRefer,
	const MimeHeaderLine* refHdr, const URI& uri, const MimeHeaderLine* replaces);
    // Decode an application/isup body into 'msg' if configured to do so
    // The message's name and user data are restored before exiting, regardless the result
    // Return true if an ISUP message was succesfully decoded
    bool decodeIsupBody(Message& msg, MimeBody* body);
    // Build the body of a SIP message from an engine message
    // Encode an ISUP message from parameters received in msg if enabled to process them
    // Build a multipart/mixed body if more then one body is going to be sent
    MimeBody* buildSIPBody(Message& msg, MimeSdpBody* sdp = 0);
    // Build the body of a hangup SIP message
    MimeBody* buildSIPBody();

    SIPTransaction* m_tr;
    SIPTransaction* m_tr2;
    // are we already hung up?
    bool m_hungup;
    // should we send a BYE?
    bool m_byebye;
    // should we CANCEL?
    bool m_cancel;
    int m_state;
    String m_reason;
    int m_reasonCode;
    String m_callid;
    // SIP dialog of this call, used for re-INVITE or BYE
    SIPDialog m_dialog;
    // remote URI as we send in dialog messages
    URI m_uri;
    String m_domain;
    String m_user;
    String m_line;
    int m_port;
    Message* m_route;
    ObjList* m_routes;
    bool m_authBye;
    bool m_inband;
    bool m_info;
    // REFER already running
    bool m_referring;
    // reINVITE requested or in progress
    int m_reInviting;
    // sequence number of last transmitted PRACK
    int m_lastRseq;
};

class YateSIPGenerate : public GenObject
{
    YCLASS(YateSIPGenerate,GenObject)
public:
    YateSIPGenerate(SIPMessage* m);
    virtual ~YateSIPGenerate();
    bool process(SIPEvent* ev);
    bool busy() const
	{ return m_tr != 0; }
    int code() const
	{ return m_code; }
private:
    void clearTransaction();
    SIPTransaction* m_tr;
    int m_code;
};

class UserHandler : public MessageHandler
{
public:
    UserHandler()
	: MessageHandler("user.login",150)
	{ }
    virtual bool received(Message &msg);
};

class SipHandler : public MessageHandler
{
public:
    SipHandler()
	: MessageHandler("xsip.generate",110)
	{ }
    virtual bool received(Message &msg);
};

// Proxy class used to transport a data buffer or a socket
// The object doesn't own its data
class RefObjectProxy : public RefObject
{
public:
    inline RefObjectProxy()
	: m_data(0), m_socket(0)
	{}
    inline RefObjectProxy(DataBlock* data)
	: m_data(data), m_socket(0)
	{}
    inline RefObjectProxy(Socket** sock)
	: m_data(0), m_socket(sock)
	{}
    virtual void* getObject(const String& name) const {
	    if (name == YSTRING("DataBlock"))
		return m_data;
	    if (name == YSTRING("Socket*"))
		return (void*)m_socket;
	    return RefObject::getObject(name);
	}
private:
    DataBlock* m_data;
    Socket** m_socket;
};

class SIPDriver : public Driver
{
public:
    enum Relay {
	Stop = Private,
    };
    SIPDriver();
    ~SIPDriver();
    virtual void initialize();
    virtual bool hasLine(const String& line) const;
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool received(Message& msg, int id);
    inline YateSIPEndPoint* ep() const
	{ return m_endpoint; }
    inline SDPParser& parser()
        { return m_parser; }
    YateSIPConnection* findCall(const String& callid, bool incRef = false);
    YateSIPConnection* findDialog(const SIPDialog& dialog, bool incRef = false);
    YateSIPConnection* findDialog(const String& dialog, const String& fromTag,
	const String& toTag, bool incRef = false);
    YateSIPLine* findLine(const String& line) const;
    YateSIPLine* findLine(const String& addr, int port, const String& user = String::empty());
    // Drop channels belonging using a given transport
    // Return the number of disconnected channels
    unsigned int transportTerminated(YateSIPTransport* trans);
    bool validLine(const String& line);
    bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    void msgStatus(Message& msg);
    // Build and dispatch a socket.ssl message
    bool socketSsl(Socket** sock, bool server, const String& context = String::empty());
protected:
    virtual void genUpdate(Message& msg);
private:
    // Add status methods
    void msgStatusAccounts(Message& msg);
    void msgStatusTransports(Message& msg, bool showUdp, bool showTcp, bool showTls);
    void msgStatusListener(Message& msg);
    void msgStatusTransport(Message& msg, const String& id);

    SDPParser m_parser;
    YateSIPEndPoint *m_endpoint;
};

static SIPDriver plugin;
static ObjList s_lines;
static Configuration s_cfg;
static Mutex s_globalMutex(true,"SIPGlobal"); // Protect globals (don't use the plugin to avoid deadlocks)
static unsigned int s_engineStop = 0;    // engine.stop message counter
static bool s_engineHalt = false;        // engine.halt received
static unsigned int s_bindRetryMs = 500; // Listeners bind retry interval
static String s_realm = "Yate";
static int s_floodEvents = 20;
static int s_maxForwards = 20;
static int s_nat_refresh = 25;
static bool s_privacy = false;
static bool s_auto_nat = true;
static bool s_progress = false;
static bool s_inband = false;
static bool s_info = false;
static bool s_start_rtp = false;
static bool s_ack_required = true;
static bool s_1xx_formats = true;
static bool s_rtp_preserve = false;
static bool s_auth_register = true;
static bool s_reg_async = true;
static bool s_multi_ringing = false;
static bool s_refresh_nosdp = true;
static bool s_ignoreVia = true;          // Ignore Via headers and send answer back to the source
static bool s_sipt_isup = false;         // Control the application/isup body processing
static bool s_printMsg = true;           // Print sent/received SIP messages to output
static u_int64_t s_waitActiveUdpTrans = 1000000; // Time to wait for active UDP transactions
                                                 // to complete when a listener is disabled
static unsigned int s_tcpConnectRetry = 3; // Number of TCP connect attempts
static u_int64_t s_tcpConnectInterval = 1000000; // The interval to attempt tcp connect
static unsigned int s_tcpIdle = TCP_IDLE_DEF; // TCP transport idle interval
static unsigned int s_tcpMaxpkt = 1500;  // Maximum packet to accept on TCP connections
static String s_tcpOutRtpip;             // RTP ip for outgoing tcp/tls transports (protected by plugin mutex)
static bool s_lineKeepTcpOffline = true; // Lines: keep TCP transports when offline
static String s_sslCertFile;             // File containing the SSL client certificate to present if requested by the server
static String s_sslKeyFile;              // File containing the key of the SSL client certificate

static int s_expires_min = EXPIRES_MIN;
static int s_expires_def = EXPIRES_DEF;
static int s_expires_max = EXPIRES_MAX;

static String s_statusCmd = "status";

int YateSIPEndPoint::s_evCount = 0;

// Lower case proto name
const TokenDict ProtocolHolder::s_protoLC[] = {
    { "udp",  Udp},
    { "tcp",  Tcp},
    { "tls",  Tls},
    { 0, 0 },
};

// Upper case proto name
const TokenDict ProtocolHolder::s_protoUC[] = {
    { "UDP",  Udp},
    { "TCP",  Tcp},
    { "TLS",  Tls},
    { 0, 0 },
};

// Transport status names
const TokenDict YateSIPTransport::s_statusName[] = {
    { "Idle",        Idle},
    { "Connected",   Connected},
    { "Terminating", Terminating},
    { "Terminated",  Terminated},
    { 0, 0 },
};


// Generate a transport id index when needed
static inline unsigned int getTransIndex()
{
    static unsigned int s_index = 0;
    Lock lck(plugin);
    if (!++s_index)
	++s_index;
    return s_index;
}

// Add a socket error to a buffer
static inline void addSockError(String& buf, Socket& sock, const char* sep = " ")
{
    String tmp;
    Thread::errorString(tmp,sock.error());
    buf << sep << tmp << " (" << sock.error() << ")";
}

// Return default udp/tcp/tls port
static inline int sipPort(bool noTls)
{
    return noTls ? 5060 : 5061;
}

// Return valid tcp idle interval
static unsigned int tcpIdleInterval(int val)
{
    int min = TCP_IDLE_MIN;
    // Adjust minimum value to engine INVITE timeout
    if (plugin.ep())
	min = (int)(plugin.ep()->engine()->getTimer('B',true) / 1000000) * 3 / 2;
    if (val >= min && val <= TCP_IDLE_MAX)
	return val;
    return (val < min) ? min : TCP_IDLE_MAX;
}

// Return a valid maxpkt
static unsigned int getMaxpkt(int val, int defVal)
{
    if (val >= 524 && val <= 65528)
	return val;
    if (val <= 0)
	return defVal;
    if (val > 65528)
	return 65528;
    return 524;
}

// Skip tabs, spaces, CR and LF from buffer start
// Return true if the buffer changed
static bool skipSpaces(String& buf, bool crlf = true)
{
    unsigned int i = 0;
    if (crlf) {
	for (; i < buf.length(); i++)
	    if (buf[i] != '\r' && buf[i] != '\n' && buf[i] != ' ' && buf[i] != '\t')
		break;
    }
    else
	for (; i < buf.length(); i++)
	    if (buf[i] != ' ' && buf[i] != '\t')
		break;
    if (!i)
	return false;
    buf = buf.substr(i);
    return true;
}

// Find an empty line in a buffer
// Return the position past it or buffer length + 1 if not found
// NOTE: returned value may be buffer length
static unsigned int getEmptyLine(const String& buf)
{
    int count = 0;
    unsigned int i = 0;
    for (; count < 2 && i < buf.length(); i++) {
	if (buf[i] == '\r') {
	    i++;
	    if (i < buf.length() && buf[i] == '\n')
		count++;
	    else
		count = 0;
	}
	else if (buf[i] == '\n')
	    count++;
	else
	    count = 0;
    }
    return (count == 2) ? i : buf.length() + 1;
}

// Fill a buffer with message method/code for debug purposes
static inline void getMsgLine(String& buf, const SIPMessage* msg)
{
    if (!msg)
	return;
    if (msg->isAnswer())
	buf << "code " << msg->code;
    else
	buf << "'" << msg->method << " " << msg->uri << "'";
}

// Reset transport timeout from expires
static void resetTransportIdle(const SIPMessage* msg, int interval)
{
    if (!msg || interval <= 0)
	return;
    YateSIPTCPTransport* tcp = YOBJECT(YateSIPTCPTransport,msg->getParty());
    if (!tcp)
	return;
    const MimeHeaderLine* hl = msg->getHeader("Flow-Timer");
    int val = hl ? hl->toInteger() : 0;
    bool on = (val > 0 && val < interval);
    if (on) {
	// Wait extra time for incoming transports
	interval = tcp->outgoing() ? val : val + 20;
    }
    tcp->setFlowTimer(on,interval);
}

// Check if an IPv4 address belongs to one of the non-routable blocks
static bool isPrivateAddr(const String& host)
{
    if (host.startsWith("192.168.") || host.startsWith("169.254.") || host.startsWith("10."))
	return true;
    String s(host);
    if (!s.startSkip("172.",false))
	return false;
    int i = 0;
    s >> i;
    return (i >= 16) && (i <= 31) && s.startsWith(".");
}

// Check if an address is an 'all interfaces' one
// This works for IPv4 addresses only
static inline bool isAllIfacesAddr(const String& addr)
{
    return !addr || addr == YSTRING("0.0.0.0");
}

// Check if there may be a NAT between an address embedded in the protocol
//  and an address obtained from the network layer
static bool isNatBetween(const String& embAddr, const String& netAddr)
{
    return isPrivateAddr(embAddr) && !isPrivateAddr(netAddr);
}

// List of headers we may not want to handle generically
static const char* s_filterHeaders[] = {
    "from",
    "to",
    0
};

// List of critical headers we surely don't want to handle generically
static const char* s_rejectHeaders[] = {
    "via",
    "route",
    "record-route",
    "call-id",
    "cseq",
    "max-forwards",
    "content-length",
    "www-authenticate",
    "proxy-authenticate",
    "authorization",
    "proxy-authorization",
    0
};

// Check if a string matches one member of a static list
static bool matchAny(const String& name, const char** strs)
{
    for (; *strs; strs++)
	if (name == *strs)
	    return true;
    return false;
}

// Copy headers from SIP message to Yate message
static void copySipHeaders(NamedList& msg, const SIPMessage& sip, bool filter = true)
{
    const ObjList* l = sip.header.skipNull();
    for (; l; l = l->skipNext()) {
	const MimeHeaderLine* t = static_cast<const MimeHeaderLine*>(l->get());
	String name(t->name());
	name.toLower();
	if (matchAny(name,s_rejectHeaders))
	    continue;
	if (filter && matchAny(name,s_filterHeaders))
	    continue;
	String tmp(*t);
	const ObjList* p = t->params().skipNull();
	for (; p; p = p->skipNext()) {
	    NamedString* s = static_cast<NamedString*>(p->get());
	    tmp << ";" << s->name();
	    if (!s->null())
		tmp << "=" << *s;
	}
	msg.addParam("sip_"+name,tmp);
    }
}

// Copy headers from Yate message to SIP message
static void copySipHeaders(SIPMessage& sip, const NamedList& msg, const char* prefix = "osip_")
{
    prefix = msg.getValue(YSTRING("osip-prefix"),prefix);
    if (!prefix)
	return;
    unsigned int n = msg.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* str = msg.getParam(i);
	if (!str)
	    continue;
	String name(str->name());
	if (!name.startSkip(prefix,false))
	    continue;
	if (name.trimBlanks().null())
	    continue;
	sip.addHeader(name,*str);
    }
}

// Copy privacy related information from SIP message to Yate message
static void copyPrivacy(Message& msg, const SIPMessage& sip)
{
    bool anonip = (sip.getHeaderValue("Anonymity") &= "ipaddr");
    const MimeHeaderLine* hl = sip.getHeader("Remote-Party-ID");
    const MimeHeaderLine* pr = sip.getHeader("Privacy");
    if (!(anonip || hl || pr))
	return;
    const NamedString* p = hl ? hl->getParam("screen") : 0;
    if (p)
	msg.setParam("screened",*p);
    if (pr && (*pr &= "none")) {
	msg.setParam("privacy",String::boolText(false));
	return;
    }
    bool privname = false;
    bool privuri = false;
    String priv;
    if (anonip)
	priv.append("addr",",");
    p = hl ? hl->getParam("privacy") : 0;
    if (p) {
	if ((*p &= "full") || (*p &= "full-network"))
	    privname = privuri = true;
	else if ((*p &= "name") || (*p &= "name-network"))
	    privname = true;
	else if ((*p &= "uri") || (*p &= "uri-network"))
	    privuri = true;
    }
    if (pr) {
	if ((*pr &= "user") || pr->getParam("user"))
	    privname = true;
	if ((*pr &= "header") || pr->getParam("header"))
	    privuri = true;
    }
    if (privname)
	priv.append("name",",");
    if (privuri)
	priv.append("uri",",");
    if (pr) {
	if ((*pr &= "session") || pr->getParam("session"))
	    priv.append("session",",");
	if ((*pr &= "critical") || pr->getParam("critical"))
	    priv.append("critical",",");
    }
    if (priv)
	msg.setParam("privacy",priv);
    if (hl) {
	URI uri(*hl);
	const char* tmp = uri.getDescription();
	if (tmp)
	    msg.setParam("privacy_callername",tmp);
	tmp = uri.getUser();
	if (tmp)
	    msg.setParam("privacy_caller",tmp);
	tmp = uri.getHost();
	if (tmp)
	    msg.setParam("privacy_domain",tmp);
	const String* str = hl->getParam("party");
	if (!TelEngine::null(str))
	    msg.setParam("remote_party",*str);
	str = hl->getParam("id-type");
	if (!TelEngine::null(str))
	    msg.setParam("remote_id_type",*str);
    }
}

// Copy privacy related information from Yate message to SIP message
static void copyPrivacy(SIPMessage& sip, const Message& msg)
{
    String screened(msg.getValue(YSTRING("screened")));
    String privacy(msg.getValue(YSTRING("privacy")));
    if (screened.null() && privacy.null())
	return;
    bool screen = screened.toBoolean();
    bool anonip = (privacy.find("addr") >= 0);
    bool privname = (privacy.find("name") >= 0);
    bool privuri = (privacy.find("uri") >= 0);
    String rfc3323;
    // allow for a simple "privacy=yes" or similar
    if (privacy.toBoolean(false))
	privname = privuri = true;
    // "privacy=no" is translated to RFC 3323 "none"
    else if (!privacy.toBoolean(true))
	rfc3323 = "none";
    if (anonip)
	sip.setHeader("Anonymity","ipaddr");
    if (screen || privname || privuri) {
	const char* caller = msg.getValue(YSTRING("privacy_caller"),msg.getValue(YSTRING("caller")));
	if (!caller)
	    caller = "anonymous";
	const char* domain = msg.getValue(YSTRING("privacy_domain"),msg.getValue(YSTRING("domain")));
	if (!domain)
	    domain = "domain";
	String tmp = msg.getValue(YSTRING("privacy_callername"),msg.getValue(YSTRING("callername"),caller));
	if (tmp) {
	    MimeHeaderLine::addQuotes(tmp);
	    tmp += " ";
	}
	tmp << "<sip:" << caller << "@" << domain << ">";
	MimeHeaderLine* hl = new MimeHeaderLine("Remote-Party-ID",tmp);
	if (screen)
	    hl->setParam("screen","yes");
	if (privname && privuri)
	    hl->setParam("privacy","full");
	else if (privname)
	    hl->setParam("privacy","name");
	else if (privuri)
	    hl->setParam("privacy","uri");
	else
	    hl->setParam("privacy","none");
	const char* str = msg.getValue(YSTRING("remote_party"));
	if (str)
	    hl->setParam("party",str);
	str = msg.getValue(YSTRING("remote_id_type"));
	if (str)
	    hl->setParam("id-type",str);
	sip.addHeader(hl);
    }
    if (rfc3323.null()) {
	if (privname)
	    rfc3323.append("user",";");
	if (privuri)
	    rfc3323.append("header",";");
	if (privacy.find("session") >= 0)
	    rfc3323.append("session",";");
	if (rfc3323 && (privacy.find("critical") >= 0))
	    rfc3323.append("critical",";");
    }
    if (rfc3323)
	sip.addHeader("Privacy",rfc3323);
}

// Check if the given body have the given type
// Find the given type inside multiparts
static inline MimeBody* getOneBody(MimeBody* body, const char* type)
{
    return body ? body->getFirst(type) : 0;
}

// Check if the given body is a SDP one or find an enclosed SDP body
//  if it is a multipart
static inline MimeSdpBody* getSdpBody(MimeBody* body)
{
    if (!body)
	return 0;
    return static_cast<MimeSdpBody*>(body->isSDP() ? body : body->getFirst("application/sdp"));
}

// Add a mime body parameter to a list of parameters
// Remove quotes, trim blanks and convert to lower case before adding
// Return false if the parameter wasn't added
inline bool addBodyParam(NamedList& nl, const char* param, MimeBody* body, const char* bodyParam)
{
    const NamedString* ns = body ? body->getParam(bodyParam) : 0;
    if (!ns)
	return false;
    String p = *ns;
    MimeHeaderLine::delQuotes(p);
    p.trimBlanks();
    if (p.null())
	return false;
    p.toLower();
    nl.addParam(param,p);
    return true;
}

// Decode an application/isup body into 'msg' if configured to do so
// The message's name and user data are restored before exiting, regardless the result
// Return true if an ISUP message was succesfully decoded
static bool doDecodeIsupBody(const DebugEnabler* debug, Message& msg, MimeBody* body)
{
    if (!s_sipt_isup)
	return false;
    // Get a valid application/isup body
    MimeBinaryBody* isup = static_cast<MimeBinaryBody*>(getOneBody(body,"application/isup"));
    if (!isup)
	return false;
    // Remember the message's name and user data and fill parameters
    String name = msg;
    RefObject* userdata = msg.userData();
    if (userdata)
	userdata->ref();
    msg = "isup.decode";
    msg.addParam("message-prefix","isup.");
    addBodyParam(msg,"isup.protocol-type",isup,"version");
    addBodyParam(msg,"isup.protocol-basetype",isup,"base");
    msg.addParam(new NamedPointer("rawdata",new DataBlock(isup->body())));
    bool ok = Engine::dispatch(msg);
    // Clear added params and restore message
    if (!ok) {
	Debug(debug,DebugMild,"%s failed error='%s'",
	    msg.c_str(),msg.getValue(YSTRING("error")));
	msg.clearParam(YSTRING("error"));
    }
    msg.clearParam(YSTRING("rawdata"));
    msg = name;
    msg.userData(userdata);
    TelEngine::destruct(userdata);
    return ok;
}

// Build the body of a SIP message from an engine message
// Encode an ISUP message from parameters received in msg if enabled to process them
// Build a multipart/mixed body if more then one body is going to be sent
static MimeBody* doBuildSIPBody(const DebugEnabler* debug, Message& msg, MimeSdpBody* sdp)
{
    MimeBinaryBody* isup = 0;

    // Build isup
    while (s_sipt_isup) {
	String prefix = msg.getValue(YSTRING("message-prefix"));
	if (!msg.getParam(prefix + "message-type"))
	    break;

	// Remember the message's name and user data
	String name = msg;
	RefObject* userdata = msg.userData();
	if (userdata)
	    userdata->ref();

	DataBlock* data = 0;
	msg = "isup.encode";
	if (Engine::dispatch(msg)) {
	    NamedString* ns = msg.getParam(YSTRING("rawdata"));
	    if (ns) {
		NamedPointer* np = static_cast<NamedPointer*>(ns->getObject(YSTRING("NamedPointer")));
		if (np)
		    data = static_cast<DataBlock*>(np->userObject(YSTRING("DataBlock")));
	    }
	}
	if (data && data->length()) {
	    isup = new MimeBinaryBody("application/isup",(const char*)data->data(),data->length());
	    isup->setParam("version",msg.getValue(prefix + "protocol-type"));
	    const char* s = msg.getValue(prefix + "protocol-basetype");
	    if (s)
		isup->setParam("base",s);
	    MimeHeaderLine* line = new MimeHeaderLine("Content-Disposition","signal");
	    line->setParam("handling","optional");
	    isup->appendHdr(line);
	}
	else {
	    Debug(debug,DebugMild,"%s failed error='%s'",
		msg.c_str(),msg.getValue(YSTRING("error")));
	    msg.clearParam(YSTRING("error"));
	}

	// Restore message
	msg = name;
	msg.userData(userdata);
	TelEngine::destruct(userdata);
	break;
    }

    if (!isup)
	return sdp;
    if (!sdp)
	return isup;
    // Build multipart
    MimeMultipartBody* body = new MimeMultipartBody;
    body->appendBody(sdp);
    body->appendBody(isup);
    return body;
}

// Find an URI parameter separator. Accept '?' or '&'
static inline int findURIParamSep(const String& str, int start)
{
    if (start < 0)
	return -1;
    for (int i = start; i < (int)str.length(); i++)
	if (str[i] == '?' || str[i] == '&')
	    return i;
    return -1;
}


bool YateSIPPartyHolder::change(String& dest, const String& src)
{
    if (dest == src)
	return false;
    changing();
    dest = src;
    return true;
}

bool YateSIPPartyHolder::change(int& dest, int src)
{
    if (dest == src)
	return false;
    changing();
    dest = src;
    return true;
}

void YateSIPPartyHolder::changing()
{
}

// Check if a transport is used by our party
YateSIPTransport* YateSIPPartyHolder::transport(bool ref)
{
    Lock lock(m_partyMutex);
    YateSIPTransport* trans = 0;
    if (m_party)
	trans = static_cast<YateSIPTransport*>(m_party->getTransport());
    return (trans && (!ref || trans->ref())) ? trans : 0;
}

// Set the held party. Referrence it before
void YateSIPPartyHolder::setParty(SIPParty* party)
{
    Lock lck(m_partyMutex);
    if (party == m_party)
	return;
    if (party && !party->ref())
	party = 0;
    if (party != m_party)
	DDebug(&plugin,DebugAll,"YateSIPPartyHolder set party (%p) [%p]",party,this);
    SIPParty* tmp = m_party;
    m_party = party;
    lck.drop();
    TelEngine::destruct(tmp);
}

// Set the party of a non answer message
bool YateSIPPartyHolder::setSipParty(SIPMessage* message, const YateSIPLine* line,
    bool useEp, const char* host, int port) const
{
    if (!message || message->isAnswer())
	return false;
    Lock lck(m_partyMutex);
    if (!m_party) {
	lck.drop();
	if (useEp && plugin.ep())
	    plugin.ep()->buildParty(message,host,port,line);
	return 0 != message->getParty();
    }
    message->setParty(m_party);
    lck.drop();
    if (line)
	line->setupAuth(message);
    return true;
}

// Change party and its transport. Return true on success
bool YateSIPPartyHolder::buildParty(bool force)
{
    XDebug(&plugin,DebugAll,"YateSIPPartyHolder::buildParty(%s,%s,%d,%s,%d) force=%u [%p]",
	protoName(),m_transLocalAddr.c_str(),m_transLocalPort,
	m_transRemoteAddr.c_str(),m_transRemotePort,force,this);
    if (!force) {
	Lock lock(m_partyMutex);
	if (m_party)
	    return true;
    }
    YateSIPTCPTransport* tcpTrans = 0;
    YateSIPUDPTransport* udpTrans = 0;
    bool initTcp = false;
    bool addrValid = true;
    if (m_transId) {
	YateSIPTransport* trans = 0;
	if (plugin.ep())
	    trans = plugin.ep()->findTransport(m_transId);
	if (trans) {
	    tcpTrans = trans->tcpTransport();
	    if (!tcpTrans) {
		udpTrans = trans->udpTransport();
		if (!udpTrans)
		    TelEngine::destruct(trans);
	    }
	}
    }
    if (!(tcpTrans || udpTrans)) {
	if (protocol() == Udp) {
	    SocketAddr addr(AF_INET);
	    if (plugin.ep()) {
		if (!m_transLocalAddr)
		    udpTrans = plugin.ep()->defTransport();
		else {
		    addr.host(m_transLocalAddr);
		    udpTrans = plugin.ep()->findUdpTransport(addr.host(),m_transLocalPort);
		}
	    }
	}
	else {
	    initTcp = true;
	    bool tls = (protocol() == Tls);
	    if (tls || protocol() == Tcp) {
		addrValid = m_transRemoteAddr && m_transRemotePort > 0;
		if (addrValid)
		    tcpTrans = new YateSIPTCPTransport(tls,m_transLocalAddr,
			m_transRemoteAddr,m_transRemotePort);
	    }
	    else
		Debug(DebugStub,"YateSIPPartyHolder::buildParty() transport %s not implemented",
		    protoName());
	}
    }
    SIPParty* p = 0;
    if (udpTrans) {
	SocketAddr addr(AF_INET);
	addr.host(m_transRemoteAddr);
	addr.port(m_transRemotePort);
	addrValid = addr.host() && addr.port() > 0;
	if (addrValid)
	    p = new YateUDPParty(udpTrans,addr);
    }
    else if (tcpTrans) {
	p = tcpTrans->getParty();
	if (!p)
	    p = new YateTCPParty(tcpTrans);
    }
    setParty(p);
    TelEngine::destruct(p);
    if (!addrValid)
	DDebug(&plugin,DebugNote,
	    "Failed to build %s transport with invalid remote addr=%s:%d",
	    protoName(),m_transRemoteAddr.c_str(),m_transRemotePort);
    if (tcpTrans && initTcp) {
	// TODO: handle other params: maxpkt, thread prio
	tcpTrans->init(NamedList::empty(),true);
    }
    TelEngine::destruct(udpTrans);
    TelEngine::destruct(tcpTrans);
    return m_party != 0;
}

// Change party and its transport. Return true on success
bool YateSIPPartyHolder::setParty(const NamedList& params, bool force, const String& prefix,
    const String& defRemoteAddr, int defRemotePort)
{
    const String& transId = params[prefix + "connection_id"];
    if (!(force || transId || params.getParam(prefix + "ip_transport") ||
	params.getBoolValue(prefix + "ip_transport_tcp"))) {
	setParty();
	return false;
    }
    if (change(m_transId,transId))
	Debug(&plugin,DebugAll,"YateSIPPartyHolder transport id changed to '%s' [%p]",
	    m_transId.c_str(),this);
    updateProto(params,prefix);
    updateRemoteAddr(params,prefix,defRemoteAddr,defRemotePort);
    updateLocalAddr(params,prefix);
    return buildParty();
}

// Update transport type. Return true if changed
bool YateSIPPartyHolder::updateProto(const NamedList& params, const String& prefix)
{
    int proto = lookupProtoAny(params[prefix + "ip_transport"]);
    if (proto == Unknown) {
	if (!params.getBoolValue(prefix + "ip_transport_tcp")) {
	    // Check transport id prefix
	    if (m_transId.startsWith("tcp:",false))
		proto = Tcp;
	    else if (m_transId.startsWith("tls:",false))
		proto = Tls;
	    else
		proto = Udp;
	}
	else if (!params.getBoolValue(prefix + "ip_transport_tls"))
	    proto = Tcp;
	else
	    proto = Tls;
    }
    bool chg = change(m_proto,proto);
    if (chg)
	Debug(&plugin,DebugAll,"YateSIPPartyHolder transport proto changed to '%s' [%p]",
	    protoName(),this);
    return chg;
}

// Update transport remote addr/port. Return true if changed
bool YateSIPPartyHolder::updateRemoteAddr(const NamedList& params, const String& prefix,
    const String& defRemoteAddr, int defRemotePort)
{
    const char* addr = params.getValue(prefix + "ip_transport_remoteip",defRemoteAddr);
    int port = params.getIntValue(prefix + "ip_transport_remoteport",defRemotePort);
    if (port <= 0)
	port = sipPort(protocol() != Tls);
    bool chg = change(m_transRemoteAddr,addr);
    chg = change(m_transRemotePort,port) || chg;
    if (chg)
	Debug(&plugin,DebugAll,"YateSIPPartyHolder remote addr changed to '%s:%d' [%p]",
	    m_transRemoteAddr.c_str(),m_transRemotePort,this);
    return chg;
}

// Update transport local addr/port. Return true if changed
bool YateSIPPartyHolder::updateLocalAddr(const NamedList& params, const String& prefix)
{
    bool chg = change(m_transLocalAddr,params[prefix + "ip_transport_localip"]);
    int port = params.getIntValue(prefix + "ip_transport_localport");
    chg = change(m_transLocalPort,port) || chg;
    if (chg)
	Debug(&plugin,DebugAll,"YateSIPPartyHolder local addr changed to '%s:%d' [%p]",
	    m_transLocalAddr.c_str(),m_transLocalPort,this);
    return chg;
}

// Update RTP local address
void YateSIPPartyHolder::setRtpLocalAddr(String& addr, Message* m)
{
    addr.clear();
    if (m)
	addr = m->getValue(YSTRING("rtp_localip"));
    if (!addr && m_party) {
	Lock lock(m_partyMutex);
	YateSIPTransport* t = YOBJECT(YateSIPTransport,m_party);
	if (t && !t->ref())
	    t = 0;
	lock.drop();
	if (t)
	    t->rtpAddr(addr);
	TelEngine::destruct(t);
    }
    DDebug(&plugin,DebugAll,"YateSIPPartyHolder rtp local addr is '%s' [%p]",
	addr.c_str(),this);
}


YateSIPListener::YateSIPListener(const String& addr, int port)
    : m_bindInterval(0), m_nextBind(0),
    m_bind(true), m_address(addr), m_port(port)
{
}

// Check bind now flag
bool YateSIPListener::bindNow(Mutex* mutex)
{
    if (!m_bind)
	return false;
    Lock lck(mutex);
    bool old = m_bind;
    m_bind = false;
    return old;
}

// Check if address would change
bool YateSIPListener::addrWouldChange(Mutex* mutex, bool udp, const String& addr, int port)
{
    SocketAddr existing(udp ? AF_INET : PF_INET);
    Lock lck(mutex);
    existing.host(m_address);
    existing.port(m_port);
    lck.drop();
    SocketAddr newAddr(udp ? AF_INET : PF_INET);
    newAddr.host(addr);
    newAddr.port(port);
    return existing != newAddr;
}

// Set addr/port and bind flag. Return the bind flag
bool YateSIPListener::setAddr(const String& addr, int port)
{
    if (m_address != addr) {
	m_bind = true;
	m_address = addr;
    }
    if (m_port != port) {
	m_bind = true;
	m_port = port;
    }
    return m_bind;
}

// Initialize a socket
Socket* YateSIPListener::initSocket(int proto, const String& name, SocketAddr& lAddr, Mutex* mutex,
    int backLogBuffer, bool forceBind, String& reason)
{
    reason = "";
    Lock lck(mutex);
    String addr = m_address;
    int port = m_port;
    lck.drop();
    bool udp = (proto == ProtocolHolder::Udp);
    const char* type = ProtocolHolder::lookupProtoName(proto);
    Debug(&plugin,DebugAll,"Listener(%s,'%s') initializing socket addr='%s:%d'",
	type,name.c_str(),addr.c_str(),port);
    Socket* sock = 0;
    if (udp)
	sock = new Socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    else
	sock = new Socket(PF_INET,SOCK_STREAM);
    // Use a while() to break to the end
    while (true) {
	if (!sock->valid()) {
	    reason = "Create socket failed";
	    break;
	}
	if (!udp)
	    sock->setReuse();
#ifdef SO_RCVBUF
	// Set UDP buffer size
	if (udp && backLogBuffer > 0) {
	    int buflen = backLogBuffer;
	    if (buflen < 4096)
		buflen = 4096;
	    if (sock->setOption(SOL_SOCKET,SO_RCVBUF,&buflen,sizeof(buflen))) {
		buflen = 0;
		socklen_t sz = sizeof(buflen);
		if (sock->getOption(SOL_SOCKET,SO_RCVBUF,&buflen,&sz))
		    Debug(&plugin,DebugNote,"Listener(%s,'%s') buffer size is %d (requested %d)",
			type,name.c_str(),buflen,backLogBuffer);
		else
		    Debug(&plugin,DebugWarn,
			"Listener(%s,'%s') could not get UDP buffer size (requested %d)",
			type,name.c_str(),backLogBuffer);
	    }
	    else
		Debug(&plugin,DebugWarn,"Listener(%s,'%s') could not set buffer size %d",
		    type,name.c_str(),buflen);
	}
#endif
	// Bind the socket
	lAddr.host(addr);
	lAddr.port(port);
	bool ok = sock->bind(lAddr);
	if (!ok && forceBind) {
	    String error;
	    Thread::errorString(error,sock->error());
	    Debug(&plugin,DebugWarn,
		"Listener(%s,'%s') unable to bind on '%s:%d' - trying a random port. %d '%s'",
		type,name.c_str(),lAddr.host().c_str(),lAddr.port(),sock->error(),error.c_str());
	    lAddr.port(0);
	    ok = sock->bind(lAddr);
	    if (ok && !sock->getSockName(lAddr)) {
		reason = "Failed to retrieve bind address";
		break;
	    }
	}
	if (!ok) {
	    reason = "Bind failed";
	    break;
	}
	if (!sock->setBlocking(false)) {
	    reason = "Set non blocking mode failed";
	    break;
	}
	if (!udp && !sock->listen(backLogBuffer)) {
	    reason = "Listen failed";
	    break;
	}
	break;
    }
    if (!reason) {
	Debug(&plugin,DebugInfo,"Listener(%s,'%s') started on '%s:%d'",
	    type,name.c_str(),lAddr.host().safe(),lAddr.port());
	m_nextBind = 0;
	m_bindInterval = 0;
	return sock;
    }
    String s;
    Thread::errorString(s,sock->error());
    Debug(&plugin,DebugWarn,"Listener(%s,'%s') %s. %d: '%s'",
	type,name.c_str(),reason.c_str(),sock->error(),s.c_str());
    if (!m_bindInterval)
	m_bindInterval = s_bindRetryMs;
    else if (m_bindInterval < BIND_RETRY_MAX)
	m_bindInterval *= 2;
    m_nextBind = Time::now() + m_bindInterval * 1000;
    YateSIPTransport::resetSocket(sock,0);
    return 0;
}


YateSIPTransport::YateSIPTransport(int proto, const String& id, Socket* sock, int stat)
    : Mutex(true,"YateSIPTransport"),
    ProtocolHolder(proto),
    m_id(id), m_status(stat), m_statusChgTime(Time::secNow()),
    m_sock(sock), m_maxpkt(1500),
    m_local(proto == Udp ? AF_INET : PF_INET),
    m_remote(proto == Udp ? AF_INET : PF_INET),
    m_worker(0), m_initialized(false)
{
    Debug(&plugin,DebugAll,"Transport(%s) created [%p]",m_id.c_str(),this);
}

// Initialize transport
bool YateSIPTransport::init(const NamedList& params, const NamedList& defs,
    bool first, Thread::Priority prio)
{
    static String s_maxPkt = "maxpkt";
    lock();
    m_initialized = true;
    m_rtpLocalAddr.clear();
    if (udpTransport()) {
	int v = params.getIntValue(s_maxPkt,defs.getIntValue(s_maxPkt,m_maxpkt));
	m_maxpkt = getMaxpkt(v,1500);
	// Set RTP addr from bind address if the transport was not built from
	// 'general' section
	if (params != YSTRING("general") && !isAllIfacesAddr(m_local.host()))
	    m_rtpLocalAddr = m_local.host();
    }
    else {
	m_maxpkt = getMaxpkt(params.getIntValue(YSTRING("tcp_maxpkt"),m_maxpkt),m_maxpkt);
	// Override rtp ip for tcp outgoing 
	if (tcpTransport() && tcpTransport()->outgoing()) {
	    Lock lck(s_globalMutex);
	    m_rtpLocalAddr = s_tcpOutRtpip;
	}
    }
    m_rtpLocalAddr = params.getValue(YSTRING("rtp_localip"),m_rtpLocalAddr);
    unlock();
    // Done if not first
    if (!first)
	return true;
    if (m_sock) {
	m_sock->getSockName(m_local);
	m_sock->getPeerName(m_remote);
    }
    m_worker = new YateSIPTransportWorker(this,prio);
    if (m_worker->startup())
	return true;
    Debug(&plugin,DebugWarn,"Transport(%s) failed to start worker thread [%p]",m_id.c_str(),this);
    m_reason = "Failed to start worker";
    m_worker = 0;
    return false;
}

void YateSIPTransport::printSendMsg(const SIPMessage* msg, const SocketAddr* addr)
{
    if (!msg)
	return;
    if (!plugin.debugAt(DebugInfo))
	return;
    String raddr;
    if (addr)
	raddr << addr->host() << ":" << addr->port();
    else
	raddr << m_remote.host() << ":" << m_remote.port();
    if (!plugin.filterDebug(raddr))
	return;
    String tmp;
    getMsgLine(tmp,msg);
    if (addr)
	raddr = " to " + raddr;
    else
	raddr.clear();
    String buf((char*)msg->getBuffer().data(),msg->getBuffer().length());
    Debug(&plugin,DebugInfo,"'%s' sending %s %p%s [%p]\r\n------\r\n%s------",
	m_protoAddr.c_str(),tmp.c_str(),msg,raddr.safe(),this,buf.c_str());
}

// Print received messages to output
void YateSIPTransport::printRecvMsg(const char* buf, int len)
{
    if (!buf)
	return;
    if (!plugin.debugAt(DebugInfo))
	return;
    String raddr;
    raddr << m_remote.host() << ":" << m_remote.port();
    if (!plugin.filterDebug(raddr))
	return;
    String tmp;
    if (udpTransport())
	raddr = " from " + raddr;
    else {
	raddr.clear();
	tmp.assign(buf,len);
	buf = tmp;
    }
    Debug(&plugin,DebugInfo,
	"'%s' received %d bytes SIP message%s [%p]\r\n------\r\n%s------",
	m_protoAddr.c_str(),len,raddr.safe(),this,buf);
}

// Add transport data yate message
void YateSIPTransport::fillMessage(Message& msg, bool addRoute)
{
    msg.setParam("connection_id",toString());
    msg.setParam("connection_reliable",String::boolText(0 != tcpTransport()));
    if (addRoute) {
	msg.setParam("route_params","oconnection_id");
	msg.setParam("oconnection_id",toString());
    }
}

// Stop the worker. Remove from global list
void YateSIPTransport::terminate(const char* reason)
{
    XDebug(&plugin,DebugInfo,"YateSIPTransport::terminate(%s) [%p]",reason,this);
    changeStatus(Terminating);
    if (m_worker) {
	bool wait = false;
	lock();
	if (m_worker) {
	    if (Thread::current() != m_worker)
		wait = true;
	    else
		m_worker->m_transport = 0;
	    m_worker->cancel();
	}
	unlock();
	if (wait) {
	    unsigned int n = 500;
	    while (m_worker && n--)
		Thread::idle();
	    if (m_worker)
		Debug(&plugin,DebugFail,"Transport(%s) terminating with worker running [%p]",
		    m_id.c_str(),this);
	}
    }
    if (!TelEngine::null(reason)) {
	Lock lock(this);
	if (!m_reason)
	    m_reason = reason;
    }
    changeStatus(Terminated);
}

const String& YateSIPTransport::toString() const
{
    return m_id;
}

// Reset and delete a socket
void YateSIPTransport::resetSocket(Socket*& sock, int linger)
{
    if (!sock)
	return;
    sock->setLinger(linger);
    delete sock;
    sock = 0;
}

void YateSIPTransport::destroyed()
{
    terminate("Destroyed");
    resetSocket(m_sock,-1);
    Debug(&plugin,DebugAll,"Transport(%s) destroyed [%p]",m_id.c_str(),this);
    RefObject::destroyed();
}

// Change transport status. Notify it
void YateSIPTransport::changeStatus(int stat)
{
    Lock lock(this);
    if (stat == m_status || m_status == Terminated)
	return;
    unsigned int t = Time::secNow();
    DDebug(&plugin,DebugAll,"Transport(%s) changed status old=%s new=%s statustime=%u [%p]",
	m_id.c_str(),statusName(m_status),statusName(stat),t - m_statusChgTime,this);
    m_statusChgTime = t;
    m_status = stat;
    String reason;
    if (m_status == Terminated) {
	reason = m_reason;
	m_reason.clear();
    }
    lock.drop();
    statusChanged();
    if (plugin.ep())
	plugin.ep()->transportChangedStatus(this,m_status,reason);
}

// Handle received messages, set party, add to engine
void YateSIPTransport::receiveMsg(SIPMessage*& msg)
{
    if (!msg)
	return;
    if (!msg->isAnswer()) {
	SIPParty* party = 0;
	YateSIPUDPTransport* udp = udpTransport();
	YateSIPTCPTransport* tcp = tcpTransport();
	if (udp) {
	    URI uri(msg->uri);
	    YateSIPLine* line = plugin.findLine(m_remote.host(),m_remote.port(),uri.getUser());
	    const char* host = 0;
	    int port = -1;
	    if (line && line->getLocalPort()) {
		host = line->getLocalAddr();
		port = line->getLocalPort();
	    }
	    if (!host)
		host = m_local.host();
	    if (port <= 0)
		port = m_local.port();
	    party = new YateUDPParty(udp,m_remote,&port,host);
	}
	else if (tcp) {
	    party = tcp->getParty();
	    if (!party) {
		party = new YateTCPParty(tcp);
		DDebug(&plugin,DebugAll,
		    "Transport(%s) built tcp party (%p) for received message (%p) [%p]",
		    m_id.c_str(),party,msg,this);
	    }
	}
	if (party) {
	    msg->setParty(party);
	    TelEngine::destruct(party);
	}
    }
    if (plugin.ep() && plugin.ep()->engine())
	plugin.ep()->engine()->addMessage(msg);
    TelEngine::destruct(msg);
}

// Print socket read error to output
void YateSIPTransport::printReadError()
{
    if (m_sock->canRetry())
	return;
    m_reason = "Socket read error:";
    addSockError(m_reason,*m_sock);
    Debug(&plugin,DebugWarn,"Transport(%s) %s [%p]",m_id.c_str(),m_reason.c_str(),this);
}

// Print socket write error to output
void YateSIPTransport::printWriteError(int res, unsigned int len)
{
    if (res == (int)len) {
	XDebug(&plugin,DebugAll,"Transport(%s) sent %u bytes [%p]",
	    m_id.c_str(),len,this);
	return;
    }
    if (res >= 0) {
	Debug(&plugin,DebugAll,"Transport(%s) sent %d/%u [%p]",m_id.c_str(),res,len,this);
	return;
    }
    if (m_sock->canRetry())
        return;
    m_reason = "Socket send error:";
    addSockError(m_reason,*m_sock);
    Debug(&plugin,DebugWarn,"Transport(%s) %s [%p]",m_id.c_str(),m_reason.c_str(),this);
}

// Set m_protoAddr from local/remote ip/port or reset it
void YateSIPTransport::setProtoAddr(bool set)
{
    Lock lck(this);
    if (!set) {
	m_protoAddr = "";
	return;
    }
    m_protoAddr << protoName(false) << ":" << m_local.host() << ":" << m_local.port();
    if (!udpTransport())
	m_protoAddr << "-" << m_remote.host() << ":" << m_remote.port();
}


YateSIPUDPTransport::YateSIPUDPTransport(const String& id)
    : YateSIPTransport(Udp,id,0,Idle),
    m_default(false), m_forceBind(true), m_bufferReq(0)
{
}

// (Re)Initialize the transport
bool YateSIPUDPTransport::init(const NamedList& params, const NamedList& defs, bool first,
    Thread::Priority prio)
{
    bool ok = YateSIPTransport::init(params,defs,first,prio);
    m_default = params.getBoolValue("default",toString() == YSTRING("general"));
    m_forceBind = params.getBoolValue("udp_force_bind",true);
    m_bufferReq = params.getIntValue("buffer",defs.getIntValue("buffer"));
    if (first)
	setAddr(params.getValue("addr","0.0.0.0"),params.getIntValue("port",5060));
    Debug(&plugin,DebugAll,
	"Transport(%s) initialized addr='%s:%d' default=%s maxpkt=%u rtp_localip=%s [%p]",
	m_id.c_str(),m_address.c_str(),m_port,String::boolText(m_default),m_maxpkt,
	m_rtpLocalAddr.c_str(),this);
    return ok;
}

// Send data
void YateSIPUDPTransport::send(const void* data, unsigned int len, const SocketAddr& addr)
{
    if (!m_sock)
	return;
    Lock lck(this);
    if (!m_sock)
	return;
    int sent = m_sock->sendTo(data,len,addr);
    printWriteError(sent,len);
}

// Process data (read/send).
// Return 0 to continue processing, positive to sleep (usec),
//  negative to terminate and destroy
int YateSIPUDPTransport::process()
{
    bool force = bindNow(this);
    if (force || !m_sock) {
	if (m_sock) {
	    changeStatus(Idle);
	    Lock lck(this);
	    YateSIPTransport::resetSocket(m_sock,-1);
	    m_local.host("");
	    m_local.port(0);
	    setProtoAddr(false);
	}
	if (!force && m_nextBind > Time::now())
	    return Thread::idleUsec();
	String reason;
	SocketAddr addr(PF_INET);
	Socket* sock = initSocket(ProtocolHolder::Udp,toString(),addr,this,
	    m_bufferReq,m_forceBind,reason);
	if (sock) {
	    lock();
	    m_sock = sock;
	    m_local = addr;
	    unlock();
	    setProtoAddr(true);
	    changeStatus(Connected);
	}
	else {
	    changeStatus(Idle);
	    Lock lck(this);
	    m_reason = reason;
	}
	if (!m_sock)
	    return Thread::idleUsec();
    }
    int& evc = YateSIPEndPoint::s_evCount;
    // Do nothing if the endpoint is flooded with events or terminating
    if (!(YateSIPEndPoint::canRead() || ((evc & 3) == 0)))
	return Thread::idleUsec();
    int retVal = 0;
    // Check if we can read (select is available)
    // Wait up to the platform idle time if we had no events in last run
    if (m_sock->canSelect()) {
	bool ok = false;
	if (m_sock->select(&ok,0,0,Thread::idleUsec())) {
	    if (!ok)
		return 0;
	}
	else {
	    // Select failed
	    if (m_sock->canRetry())
		return Thread::idleUsec();
	    String tmp;
	    Thread::errorString(tmp,m_sock->error());
	    Debug(&plugin,DebugWarn,"Transport(%s) select failed: %d '%s' [%p]",
		m_id.c_str(),m_sock->error(),tmp.c_str(),this);
	    return Thread::idleUsec();
	}
    }
    else
	retVal = Thread::idleUsec();
    // We can read the data
    m_buffer.resize(m_maxpkt);
    int res = m_sock->recvFrom((void*)m_buffer.data(),m_buffer.length() - 1,m_remote);
    if (res <= 0) {
	printReadError();
	return retVal;
    }
    if (res < 72) {
	DDebug(&plugin,DebugInfo,
	    "Transport(%s) received short SIP message of %d bytes from %s:%d [%p]",
	    m_id.c_str(),res,m_remote.host().c_str(),m_remote.port(),this);
	return 0;
    }
    char* b = (char*)m_buffer.data();
    b[res] = 0;
    if (s_printMsg)
	printRecvMsg(b,res);
    SIPMessage* msg = SIPMessage::fromParsing(0,b,res);
    receiveMsg(msg);
    return 0;
}


// Outgoing
YateSIPTCPTransport::YateSIPTCPTransport(bool tls, const String& laddr, const String& raddr,
    int rport)
    : YateSIPTransport(tls ? Tls : Tcp,String::empty(),0,Idle),
    m_outgoing(true), m_party(0), m_sent(-1),
    m_idleInterval(TCP_IDLE_DEF), m_idleTimeout(0),
    m_flowTimer(false), m_keepAlivePending(false),
    m_msg(0), m_sipBufOffs(0), m_contentLen(0),
    m_remoteAddr(raddr), m_remotePort(rport), m_localAddr(laddr),
    m_connectRetry(s_tcpConnectRetry), m_nextConnect(0)
{
    m_maxpkt = s_tcpMaxpkt;
    if (m_remotePort <= 0)
	m_remotePort = sipPort(protocol() != Tls);
    m_id << (tls ? "tls:" : "tcp:") << getTransIndex() << "-" << raddr << ":" << m_remotePort;
    if (plugin.ep())
	plugin.ep()->addTcpTransport(this);
}

// Incoming
YateSIPTCPTransport::YateSIPTCPTransport(Socket* sock, bool tls)
    : YateSIPTransport(tls ? Tls : Tcp,String::empty(),sock,sock ? Connected : Idle),
    m_outgoing(false), m_party(0), m_sent(-1),
    m_idleInterval(TCP_IDLE_DEF), m_idleTimeout(0),
    m_flowTimer(false), m_keepAlivePending(false),
    m_msg(0), m_sipBufOffs(0), m_contentLen(0),
    m_remotePort(0), m_connectRetry(0), m_nextConnect(0)
{
    m_maxpkt = s_tcpMaxpkt;
    m_id << (tls ? "tls:" : "tcp:");
    if (m_sock) {
    	m_sock->getSockName(m_local);
	m_sock->getPeerName(m_remote);
	m_id << m_local.host() << ":" << m_local.port();
	m_id << "-" << m_remote.host() << ":" << m_remote.port();;
	setProtoAddr(true);
    }
    else
	m_id << getTransIndex();
    if (plugin.ep())
	plugin.ep()->addTcpTransport(this);
}

YateTCPParty* YateSIPTCPTransport::getParty()
{
    Lock lock(this);
    return (m_party && m_party->ref()) ? m_party : 0;
}

// (Re)Initialize the transport
bool YateSIPTCPTransport::init(const NamedList& params, bool first, Thread::Priority prio)
{
    bool ok = YateSIPTransport::init(params,NamedList::empty(),first,prio);
    m_idleInterval = tcpIdleInterval(params.getIntValue(YSTRING("tcp_idle"),s_tcpIdle));
    setIdleTimeout();
    Debug(&plugin,DebugAll,
	"Transport(%s) initialized maxpkt=%u rtp_localip=%s tcp_idle=%u [%p]",
	m_id.c_str(),m_maxpkt,m_rtpLocalAddr.c_str(),m_idleInterval,this);
    return ok;
}

// Set flow timer flag and idle interval (in seconds)
// Reset idle timeout
void YateSIPTCPTransport::setFlowTimer(bool on, unsigned int interval)
{
    Lock lock(this);
    m_flowTimer = on;
    if (m_flowTimer || m_outgoing || (!m_outgoing && m_idleInterval < interval))
	m_idleInterval = interval;
    Debug(&plugin,DebugInfo,"Transport(%s) flow timer is '%s' idle interval is %u seconds [%p]",
	m_id.c_str(),String::boolText(m_flowTimer),m_idleInterval,this);
    setIdleTimeout();
}

// Send data
void YateSIPTCPTransport::send(SIPEvent* event)
{
    SIPMessage* msg = event->getMessage();
    if (!msg || s_engineHalt)
	return;
    Lock lock(this);
    if (m_status == Terminated)
	return;
    if (m_queue.find(msg) || !msg->ref())
	return;
    m_queue.append(msg);
#ifdef XDEBUG
    String tmp;
    getMsgLine(tmp,msg);
    Debug(&plugin,DebugAll,"Transport(%s) enqueued (%p,%s) [%p]",
	m_id.c_str(),msg,tmp.c_str(),this);
#endif
}

// Process data (read/send)
int YateSIPTCPTransport::process()
{
    if (s_engineHalt) {
	// Stop processing
	Lock lck(this);
	// Last chance to send pending data
	ObjList* first = m_queue.skipNull();
	if (first && m_sock && m_sock->valid()) {
	    DataBlock buf;
	    for (ObjList* o = first; o && buf.length() < 4096; o = o->skipNext()) {
		SIPMessage* msg = static_cast<SIPMessage*>(o->get());
		if (s_printMsg && (o != first || m_sent < 0))
		    printSendMsg(msg);
		if (o != first || m_sent <= 0)
		    buf += msg->getBuffer();
		else {
		    int remaining = msg->getBuffer().length() - m_sent;
		    if (remaining > 0)
			buf.assign(((char*)msg->getBuffer().data()) + m_sent,remaining);
		}
	    }
	    if (buf.length()) {
		DDebug(&plugin,DebugAll,"Transport(%s) sending last %u bytes [%p]",
		    m_id.c_str(),buf.length(),this);
		m_sock->writeData(buf.data(),buf.length());
	    }
	}
	m_queue.clear();
	// Terminate now incoming with no reference
	// Remember: the worker is referencing us
	if (!m_outgoing && refcount() == 2)
	    return -1;
	return 2000;
    }
    if (!(m_sock && m_sock->valid())) {
	if (!m_outgoing)
	    return -1;
	if (!m_connectRetry || s_engineStop)
	    return -1;
	int retVal = Thread::idleUsec();
	if (m_nextConnect > Time::now())
	    return retVal;
	int conn = connect();
	if (conn > 0) {
	    m_connectRetry = s_tcpConnectRetry;
	    m_nextConnect = 0;
	    setIdleTimeout();
	}
	else if (!conn) {
	    m_connectRetry--;
	    DDebug(&plugin,DebugAll,"Transport(%s) connect retry is %u [%p]",
		toString().c_str(),m_connectRetry,this);
	    if (m_connectRetry)
		m_nextConnect = Time::now() + s_tcpConnectInterval;
	    else
		retVal = -1;
	}
	else {
	    m_connectRetry = 0;
	    retVal = -1;
	}
	return retVal;
    }
    Time time;
    bool sent = false;
    // Send pending data/keepalive
    if (!sendPending(time,sent)) {
	resetConnection();
	return m_outgoing ? 0 : -1;
    }
    // Read data
    bool read = false;
    if (!readData(time,read)) {
	resetConnection();
	return m_outgoing ? 0 : -1;
    }
    // Idle incoming with refcount=2 (the worker is referencing us): terminate
    if (!m_outgoing && m_idleTimeout < time) {
	if (refcount() == 2) {
	    m_reason = "Connection idle timeout";
	    Debug(&plugin,DebugInfo,"Transport(%s) idle [%p]",m_id.c_str(),this);
	    return -1;
	}
	setIdleTimeout(time);
    }
    return read ? 0 : Thread::idleUsec();
}

void YateSIPTCPTransport::destroyed()
{
    TelEngine::destruct(m_msg);
    YateSIPTransport::destroyed();
}

// Status changed notification for descendents
void YateSIPTCPTransport::statusChanged()
{
    Lock lock(this);
    if (m_status == Terminated) {
	// Remove messages now: they keep a party who is keeping a reference to us
	m_queue.clear();
	m_sent = -1;
    }
}

// Reset transport's party
void YateSIPTCPTransport::resetParty(YateTCPParty* party, bool set)
{
    if (!party)
	return;
    Lock lock(this);
    if (!m_party) {
	if (!set) {
	    Debug(&plugin,DebugNote,
		"Transport(%s) party (%p) trying to reset empty [%p]",
		m_id.c_str(),party,this);
	    return;
	}
    }
    else if (set || m_party != party) {
	int level = DebugNote;
#ifdef DEBUG
	if (set && m_party != party)
	    level = DebugFail;
#endif
	Debug(&plugin,level,"Transport(%s) party (%p) trying to %sset (%p) [%p]",
	    m_id.c_str(),party,set ? "" : "re",m_party,this);
	return;
    }
    m_party = set ? party : 0;
    DDebug(&plugin,DebugAll,"Transport(%s) party changed to (%p) [%p]",
	m_id.c_str(),m_party,this);
}

// Connect an outgoing transport. Terminate the socket before it
int YateSIPTCPTransport::connect(u_int64_t connToutUs)
{
    resetConnection();
    Socket* sock = new Socket(PF_INET,SOCK_STREAM);
    int retVal = -1;
    m_reason.clear();
    // Use a while() to break to the end
    while (true) {
	if (!m_remoteAddr) {
	    m_reason = "Empty remote address";
	    break;
	}
	bool ok = true;
	// Bind to local ip
	SocketAddr lip(PF_INET);
	if (m_localAddr) {
	    lip.host(m_localAddr);
	    if (lip.host()) {
		ok = sock->bind(lip);
		if (!ok) {
		    m_reason << "Failed to bind on '" << lip.host() << "' (" << m_localAddr << "). ";
		    addSockError(m_reason,*sock);
		}
	    }
	    else
		m_reason << "Invalid local address '" << m_localAddr << "'";
	}
	if (!ok)
	    break;
	// Allow connect retry
	retVal = 0;
	ok = false;
	SocketAddr a(PF_INET);
	a.host(m_remoteAddr);
	a.port(m_remotePort);
	if (!a.host()) {
	    m_reason << "Failed to resolve '" << m_remoteAddr << "'";
	    break;
	}
	// Use async connect
	if (connToutUs && !(sock->canSelect() && sock->setBlocking(false))) {
	    connToutUs = 0;
	    if (sock->canSelect()) {
		String tmp;
		addSockError(tmp,*sock);
		Debug(&plugin,DebugInfo,
		    "Transport(%s) using sync connect (async set failed).%s [%p]",
		    m_id.c_str(),tmp.c_str(),this);
	    }
	    else
		Debug(&plugin,DebugInfo,
		    "Transport(%s) using sync connect (select() not available) [%p]",
		    m_id.c_str(),this);
	}
	u_int64_t start = connToutUs ? Time::now() : 0;
	unsigned int intervals = 0;
	if (start) {
	    intervals = (unsigned int)(connToutUs / Thread::idleUsec());
	    // Make sure we wait for at least 1 timeout interval
	    if (!intervals)
		intervals = 1;
	}
	String domain;
	if (a.host() != m_remoteAddr)
	    domain << " (" << m_remoteAddr << ")";
	Debug(&plugin,DebugAll,
	    "Transport(%s) attempt to connect to '%s:%d'%s localip=%s [%p]",
	    m_id.c_str(),a.host().c_str(),a.port(),domain.safe(),
	    lip.host().safe(),this);
	ok = (0 != sock->connect(a));
	bool timeout = false;
	bool stop = false;
	// Async connect in progress
	if (!ok && sock->inProgress()) {
	    bool done = false;
	    bool event = false;
	    while (intervals && !(done || event || stop)) {
		if (!sock->select(0,&done,&event,Thread::idleUsec()))
		    break;
	        intervals--;
		stop = Thread::check(false) || Engine::exiting();
	    }
	    timeout = !intervals && !(done || event);
	    if (!stop && sock && !sock->error() && (done || event) && sock->updateError())
		ok = !sock->error();
	}
	if (ok) {
	    // TLS?
	    if (tls() && !plugin.socketSsl(&sock,false)) {
		m_reason = "SSL not available locally";
		retVal = -1;
		ok = false;
	    }
	    if (ok) {
		if (!Thread::check(false))
		    retVal = 1;
		else {
		    m_reason = "Cancelled";
		    retVal = -1;
		}
	    }
	}
	else if (!stop) {
	    m_reason << "Failed to connect to '" << a.host() << ":" << a.port() << "'";
	    m_reason << domain;
	    if (timeout)
		m_reason << " . Connect timeout";
	    else
		addSockError(m_reason,*sock);
	}
	break;
    }
    if (retVal > 0)
	resetConnection(sock);
    else {
	int level = DebugWarn;
	if (!m_reason) {
	    if (Thread::check(false) || Engine::exiting()) {
		level = DebugInfo;
		m_reason = "Connect cancelled";
	    }
	    else
		m_reason = "Connect failed";
	}
	Debug(&plugin,level,"Transport(%s) %s [%p]",m_id.c_str(),m_reason.c_str(),this);
	resetSocket(sock,0);
    }
    return retVal;
}

// Send pending messages, return false on failure
bool YateSIPTCPTransport::sendPending(const Time& time, bool& sent)
{
    sent = false;
    if (!m_sock)
	return false;
    int attempts = 3;
    while (attempts--) {
	Lock lock(this);
	ObjList* o = m_queue.skipNull();
	SIPMessage* msg = o ? static_cast<SIPMessage*>(o->get()) : 0;
	if (msg && m_sent < 0) {
	    m_sent = 0;
	    XDebug(&plugin,DebugAll,"Transport(%s) dequeued (%p) [%p]",
		m_id.c_str(),msg,this);
	    if (s_printMsg)
		printSendMsg(msg);
	}
	else if (!msg) {
	    m_sent = -1;
	    break;
	}
	const DataBlock& buf = msg->getBuffer();
	sent = true;
	int len = buf.length();
	if (len > m_sent) {
	    char* b = (char*)(buf.data());
	    len -= m_sent;
	    int wr = m_sock->writeData(b + m_sent,len);
	    printWriteError(wr,len);
	    if (wr > 0) {
		m_sent += wr;
		// Outgoing: reset keep alive timer
		if (m_outgoing)
		    setIdleTimeout(time);
	    }
	    else if (wr && !m_sock->canRetry())
		return false;
	}
	if (m_sent >= (int)buf.length()) {
#ifdef DEBUG
	    String tmp;
	    getMsgLine(tmp,msg);
	    Debug(&plugin,DebugAll,"Transport(%s) sent (%p,%s) [%p]",
		m_id.c_str(),msg,tmp.c_str(),this);
#endif
	    o->remove();
	    m_sent = -1;
	    if (m_keepAlivePending) {
		m_keepAlivePending = false;
		if (!sendKeepAlive(false))
		    return false;
	    }
	    continue;
	}
	break;
    }
    // Keep alive?
    if (m_outgoing && !sent && m_idleTimeout <= time) {
	if (sendKeepAlive(true)) {
	    sent = true;
	    setIdleTimeout(time);
	}
	else
	    return false;
    }
    return true;
}

// Read data
bool YateSIPTCPTransport::readData(const Time& time, bool& read)
{
    read = false;
    m_buffer.resize(m_maxpkt);
    int res = m_sock->readData((void*)m_buffer.data(),m_buffer.length() - 1);
    if (res < 0) {
	printReadError();
	return m_sock->canRetry();
    }
    if (!res) {
	m_reason = "Network down";
	Debug(&plugin,DebugNote,"Transport(%s) %s [%p]",m_id.c_str(),m_reason.c_str(),this);
	return false;
    }
    read = true;
    char* b = (char*)m_buffer.data();
    b[res] = 0;
    XDebug(&plugin,DebugAll,"%s current buffer '%s' read %d: %s [%p]",
	m_id.c_str(),m_sipBuffer.c_str(),res,b,this);
    m_sipBuffer << b;
    if (!m_msg) {
	// Always skip blanks before message start
	skipSpaces(m_sipBuffer,false);
	if (!m_outgoing && m_sipBuffer.startSkip("\r\n\r\n")) {
	    // RFC5626: send CR/LF in response now or after the next message
	    lock();
	    m_keepAlivePending = (0 != m_queue.skipNull());
	    unlock();
	    if (!(m_keepAlivePending || sendKeepAlive(false)))
		return false;
	}
    }
    while (m_sipBuffer.length() && m_sipBuffer.length() >= 72) {
	if (!m_msg) {
	    m_sipBufOffs = 0;
	    m_contentLen = 0;
	    // Skip spaces from message start: it might be keep alive
	    if (skipSpaces(m_sipBuffer) && m_sipBuffer.length() < 72)
		break;
	    // Find an empty line
	    m_sipBufOffs = getEmptyLine(m_sipBuffer);
	    if (m_sipBufOffs > m_sipBuffer.length()) {
		m_sipBufOffs = 0;
		if (m_sipBuffer.length() <= m_maxpkt)
		    break;
		return overflow(m_sipBuffer.length());
	    }
	    if (m_sipBufOffs > m_maxpkt)
		return overflow(m_sipBufOffs);
	    // Parse the message headers
	    m_msg = SIPMessage::fromParsing(0,m_sipBuffer,m_sipBufOffs,&m_contentLen);
	    if (!m_msg) {
		m_reason = "Received invalid message";
		String tmp(m_sipBuffer,m_sipBufOffs);
		Debug(&plugin,DebugNote,
		    "'%s' got invalid message [%p]\r\n------\r\n%s------",
		    m_id.c_str(),this,tmp.c_str());
		return false;
	    }
	    // Check now if expected message length exceeds maxpkt
	    unsigned int expected = m_sipBufOffs + m_contentLen;
	    if (expected > m_maxpkt)
		return overflow(expected);
	}
	// Expecting message body ?
	if (m_contentLen) {
	    if (m_sipBufOffs + m_contentLen > m_sipBuffer.length())
		break;
	    m_msg->buildBody(m_sipBuffer + m_sipBufOffs,m_contentLen);
	    m_sipBufOffs += m_contentLen;
	    m_contentLen = 0;
	}
	if (s_printMsg)
	    printRecvMsg(m_sipBuffer,m_sipBufOffs);
	SIPMessage* msg = m_msg;
	m_msg = 0;
	receiveMsg(msg);
	m_sipBuffer = m_sipBuffer.substr(m_sipBufOffs);
	m_sipBufOffs = 0;
    }
    // Got data: reset timeout for incoming and connection check for all
    if (!m_outgoing)
	setIdleTimeout(time);
    return true;
}

// Reset socket and status
void YateSIPTCPTransport::resetConnection(Socket* sock)
{
    Lock lck(this);
    DDebug(&plugin,DebugAll,"Transport(%s) resetting connection sock=%p [%p]",
	m_id.c_str(),sock,this);
    // Reset send/recv data
    TelEngine::destruct(m_msg);
    m_sent = -1;
    m_sipBuffer.clear();
    m_sipBufOffs = 0;
    m_contentLen = 0;
    m_keepAlivePending = false;
    m_flowTimer = false;
    setProtoAddr(false);
    // Reset socket and addresses
    if (m_sock) {
	resetSocket(m_sock,-1);
	m_local.clear();
	m_remote.clear();
    }
    m_sock = sock;
    if (m_sock) {
	m_sock->getSockName(m_local);
	m_sock->getPeerName(m_remote);
	setProtoAddr(true);
	Debug(&plugin,DebugAll,"Transport(%s) connected local=%s:%d remote=%s:%d [%p]",
	    m_id.c_str(),m_local.host().c_str(),m_local.port(),
	    m_remote.host().c_str(),m_remote.port(),this);
    }
    // Update party local/remote ip/port
    if (m_party)
	m_party->updateAddrs();
    lck.drop();
    changeStatus(m_sock ? Connected : Idle);
}

void YateSIPTCPTransport::setIdleTimeout(u_int64_t time)
{
    m_idleTimeout = time + (u_int64_t)m_idleInterval * 1000000;
    XDebug(&plugin,DebugAll,"Transport(%s) set idle timeout to %u [%p]",
	m_id.c_str(),(unsigned int)(m_idleTimeout / 1000000),this);
}

bool YateSIPTCPTransport::sendKeepAlive(bool request)
{
    XDebug(&plugin,DebugAll,"Transport(%s) sending keep alive%s [%p]",
	m_id.c_str(),request ? "" : " response",this);
    unsigned int len = request ? 4 : 2;
    int wr = m_sock->writeData("\r\n\r\n",len);
    printWriteError(wr,len);
    return wr >= 0 || m_sock->canRetry();
}

// Method called on buffer overflow.
// Reset connection. Return 0 for outgoing -1 otherwise
bool YateSIPTCPTransport::overflow(unsigned int msglen)
{
    m_reason = "Buffer overflow (message too long)";
    Debug(&plugin,DebugNote,"'%s' %s len=%u maxpkt=%u [%p]",
	m_id.c_str(),m_reason.c_str(),msglen,m_maxpkt,this);
    resetConnection();
    return false;
}


YateSIPTransportWorker::YateSIPTransportWorker(YateSIPTransport* trans,
    Thread::Priority prio)
    : Thread("YSIP Worker",prio), m_transport(trans)
{
    XDebug(&plugin,DebugAll,"YateSIPTransportWorker(%p,%s) [%p]",
	trans,trans ? trans->toString().c_str() : "",this);
}

YateSIPTransportWorker::~YateSIPTransportWorker()
{
    cleanupTransport(true);
}

void YateSIPTransportWorker::run()
{
    if (!m_transport)
	return;
    DDebug(&plugin,DebugAll,"YateSIPTransportWorker (%p) '%s' started [%p]",
	m_transport,m_transport->toString().c_str(),this);
    while (true) {
	if (Thread::check(false))
	    break;
	// Keep the transport alive while calling its method
	RefPointer<YateSIPTransport> trans = m_transport;
	int n = trans ? trans->process() : -1;
	trans = 0;
	if (n > 0)
	    Thread::usleep(n);
	else if (n < 0)
	    break;
    }
    DDebug(&plugin,DebugAll,"YateSIPTransportWorker terminated [%p]",this);
    cleanupTransport(false,!Thread::check(false));
}

void YateSIPTransportWorker::cleanupTransport(bool final, bool terminate)
{
    // Reset now transport data: the thread might be cancelled from
    // YateSIPTransport::destroyed()
    if (m_transport) {
	Lock lock(m_transport);
	m_transport->m_worker = 0;
    }
    RefPointer<YateSIPTransport> trans = m_transport;
    m_transport = 0;
#ifdef XDEBUG
    Debugger debug(DebugAll,"YateSIPTransportWorker::cleanupTransport()",
	" final=%u terminate=%u transport=%p [%p]",
	final,terminate,(YateSIPTransport*)trans,this);
#endif
    if (!trans)
	return;
    if (final)
	Debug(DebugWarn,"YateSIPTransportWorker abnormally terminated! [%p]",this);
    YateSIPTCPTransport* tcp = trans->tcpTransport();
    if (tcp && tcp->outgoing())
	tcp = 0;
    if (terminate)
	trans->terminate();
    // Deref incoming TCP
    if (tcp)
	tcp->deref();
    trans = 0;
}


YateSIPTCPListener::YateSIPTCPListener(int proto, const String& name, const NamedList& params)
    : Thread("YSIP Listener",Thread::priority(params.getValue("thread"))),
    String(name),
    ProtocolHolder(proto),
    m_mutex(true,"YSIPListener"),
    m_sslContextChanged(true), m_transParamsChanged(true),
    m_socket(0), m_backlog(5), m_transParams(params), m_initialized(false)
{
    init(params,true);
}

YateSIPTCPListener::~YateSIPTCPListener()
{
    cleanup(true);
}

// Init data
void YateSIPTCPListener::init(const NamedList& params, bool first)
{
    m_initialized = true;
    String addr = params.getValue("addr","0.0.0.0");
    int port = params.getIntValue("port");
    port = (port > 0 ? port : sipPort(!tls()));
    String rtpLip;
    SocketAddr lAddr(PF_INET);
    lAddr.host(addr);
    if (!isAllIfacesAddr(lAddr.host()))
	rtpLip = lAddr.host();
    rtpLip = params.getValue("rtp_localip",rtpLip);
    String sslContext;
    m_mutex.lock();
    if (tls()) {
	sslContext = params.getValue("sslcontext");
	m_sslContextChanged = first || m_sslContextChanged || (sslContext != m_sslContext);
	m_sslContext = sslContext;
	if (!m_sslContext)
	    Debug(&plugin,DebugConf,"Listener(%s,'%s') ssl context is empty [%p]",
		protoName(),c_str(),this);
    }
    m_backlog = params.getIntValue("backlog",5,0);
    m_bind = setAddr(addr,port) || first;
    m_transParamsChanged = m_transParamsChanged || first;
    if (rtpLip != m_transParams["rtp_localip"]) {
	m_transParamsChanged = true;
	m_transParams.setParam("rtp_localip",rtpLip);
    }
    m_mutex.unlock();
    Debug(&plugin,DebugAll,
	"Listener(%s,'%s') initialized addr='%s' port=%d sslcontext='%s' rtp_localip='%s' [%p]",
	protoName(),c_str(),addr.c_str(),port,sslContext.safe(),rtpLip.c_str(),this);
}

void YateSIPTCPListener::run()
{
    DDebug(&plugin,DebugAll,"Listener(%s,'%s') start running [%p]",
	protoName(),c_str(),this);
    SocketAddr lAddr(PF_INET);
    NamedList transParams("");
    String sslContext;
    while (true) {
	if (Thread::check(false))
	    break;
	if (m_sslContextChanged || m_transParamsChanged) {
	    Lock lock(m_mutex);
	    if (m_sslContextChanged) {
		sslContext = m_sslContext;
		if (tls() && !sslContext)
		    m_reason = "Empty ssl context";
	    }
	    if (m_transParamsChanged)
		transParams = m_transParams;
	    m_sslContextChanged = false;
	    m_transParamsChanged = false;
	}
	if (tls() && !sslContext) {
	    stopListening();
	    Thread::msleep(3 * Thread::idleMsec());
	    continue;
	}
	bool force = bindNow(&m_mutex);
	if (force || !m_socket) {
	    if (m_socket)
		stopListening("Address changed",DebugInfo);
	    if (!force && m_nextBind > Time::now()) {
		Thread::idle();
		continue;
	    }
	    String reason;
	    Socket* sock = initSocket(protocol(),toString(),lAddr,&m_mutex,m_backlog,false,reason);
	    Lock lck(m_mutex);
	    m_socket = sock;
	    m_reason = reason;
	    if (!m_socket)
		continue;
	}
	SocketAddr addr(PF_INET);
	Socket* sock = m_socket->accept(addr);
	if (!sock) {
	    Thread::idle();
	    continue;
	}
	Debug(&plugin,DebugAll,"Listener(%s,'%s') '%s:%d' got conn from '%s:%d' [%p]",
	    protoName(),c_str(),lAddr.host().safe(),lAddr.port(),
	    addr.host().c_str(),addr.port(),this);
	if (!sock->setBlocking(false)) {
	    String tmp;
	    Thread::errorString(tmp,sock->error());
	    Debug(&plugin,DebugAll,
		"Listener(%s,'%s') '%s:%d' failed to set non-blocking mode for '%s:%d'. %d '%s' [%p]",
		protoName(),c_str(),lAddr.host().safe(),lAddr.port(),
		addr.host().c_str(),addr.port(),sock->error(),tmp.c_str(),this);
	    delete sock;
	    Thread::idle();
	    continue;
	}
	if (!tls() || plugin.socketSsl(&sock,true,sslContext)) {
	    YateSIPTCPTransport* trans = new YateSIPTCPTransport(sock,tls());
	    if (!trans->init(transParams,true))
		TelEngine::destruct(trans);
	}
	else {
	    if (tls())
		Debug(&plugin,DebugWarn,"Listener(%s,'%s') failed to start SSL [%p]",
		    protoName(),c_str(),this);
	    delete sock;
	}
    }
    cleanup(false);
}

// Close the socket. Remove from endpoint list
void YateSIPTCPListener::cleanup(bool final)
{
    if (plugin.ep())
	plugin.ep()->removeListener(this);
    if (final) {
	if (!m_socket)
	    DDebug(&plugin,DebugInfo,"Listener(%s,'%s') terminated [%p]",protoName(),c_str(),this);
	else
	    Debug(&plugin,DebugWarn,"Listener(%s,'%s') abnormally terminated [%p]",protoName(),c_str(),this);
    }
    m_mutex.lock();
    const char* reason = 0;
    if (!m_reason)
	reason = "Terminated";
    m_mutex.unlock();
    stopListening(reason,DebugInfo);
}

// Reset socket
void YateSIPTCPListener::stopListening(const char* reason, int level)
{
    if (!m_socket)
	return;
    Lock lck(m_mutex);
    if (!m_socket)
	return;
    if (!reason)
	reason = m_reason;
    Debug(&plugin,level,"Listener(%s,'%s') stop listening reason='%s' [%p]",
	protoName(),c_str(),reason,this);
    YateSIPTransport::resetSocket(m_socket,0);
}


YateUDPParty::YateUDPParty(YateSIPUDPTransport* trans, const SocketAddr& addr,
    int* localPort, const char* localAddr)
    : m_transport(0), m_addr(addr)
{
    if (plugin.ep())
	m_mutex = plugin.ep()->m_partyMutexPool.mutex(this);
    if (trans && trans->ref())
	m_transport = trans;
    if (!localPort) {
	if (m_transport) {
	    m_localPort = m_transport->local().port();
	    m_local = m_transport->local().host();
	}
    }
    else {
	m_localPort = *localPort;
	m_local = localAddr;
    }
    m_party = m_addr.host();
    m_partyPort = m_addr.port();
    if (isAllIfacesAddr(m_local)) {
	SocketAddr laddr;
	if (laddr.local(addr))
	    m_local = laddr.host();
	else
	    m_local = "localhost";
    }
    DDebug(&plugin,DebugAll,"YateUDPParty local %s:%d party %s:%d transport=%p [%p]",
	m_local.c_str(),m_localPort,m_party.c_str(),m_partyPort,m_transport,this);
}

YateUDPParty::~YateUDPParty()
{
    DDebug(&plugin,DebugAll,"YateUDPParty::~YateUDPParty() transport=%p [%p]",
	m_transport,this);
    TelEngine::destruct(m_transport);
}

void YateUDPParty::transmit(SIPEvent* event)
{
    const SIPMessage* msg = event->getMessage();
    if (!msg)
	return;
    if (m_transport) {
	Lock lck(m_transport);
	if (s_printMsg)
	    m_transport->printSendMsg(msg,&m_addr);
	m_transport->send(msg->getBuffer().data(),msg->getBuffer().length(),m_addr);
	return;
    }
    String tmp;
    getMsgLine(tmp,msg);
    Debug(&plugin,DebugWarn,"No transport to send %s to %s:%d",
	tmp.c_str(),m_addr.host().c_str(),m_addr.port());
}

const char* YateUDPParty::getProtoName() const
{
    return "UDP";
}

bool YateUDPParty::setParty(const URI& uri)
{
    Lock lock(m_mutex);
    if (m_partyPort && m_party && s_ignoreVia)
	return true;
    if (uri.getHost().null())
	return false;
    int port = uri.getPort();
    if (port <= 0)
	port = 5060;
    if (!m_addr.host(uri.getHost())) {
	Debug(&plugin,DebugWarn,"Could not resolve UDP party name '%s' [%p]",
	    uri.getHost().safe(),this);
	return false;
    }
    m_addr.port(port);
    m_party = uri.getHost();
    m_partyPort = port;
    DDebug(&plugin,DebugInfo,"New UDP party is %s:%d (%s:%d) [%p]",
	m_party.c_str(),m_partyPort,
	m_addr.host().c_str(),m_addr.port(),
	this);
    return true;
}

void* YateUDPParty::getTransport()
{
    return m_transport;
}

// Get an object from this one
void* YateUDPParty::getObject(const String& name) const
{
    if (name == YSTRING("YateUDPParty"))
	return (void*)this;
    if (name == YSTRING("YateSIPUDPTransport") || name == YSTRING("YateSIPTransport"))
	return m_transport;
    return SIPParty::getObject(name);
}


YateTCPParty::YateTCPParty(YateSIPTCPTransport* trans)
    : SIPParty(true),
    m_transport(0)
{
    if (plugin.ep())
	m_mutex = plugin.ep()->m_partyMutexPool.mutex(this);
    if (trans && trans->ref()) {
	m_transport = trans;
	trans->resetParty(this,true);
    }
    updateAddrs();
    DDebug(&plugin,DebugAll,"YateTCPParty local %s:%d party %s:%d transport=%p [%p]",
	m_local.c_str(),m_localPort,m_party.c_str(),m_partyPort,m_transport,this);
}

YateTCPParty::~YateTCPParty()
{
    XDebug(&plugin,DebugAll,"YateTCPParty::~YateTCPParty() transport=%p [%p]",
	m_transport,this);
    TelEngine::destruct(m_transport);
}

void YateTCPParty::transmit(SIPEvent* event)
{
    const SIPMessage* msg = event->getMessage();
    if (!msg)
	return;
    if (m_transport) {
	m_transport->send(event);
	return;
    }
    String tmp;
    getMsgLine(tmp,msg);
    Debug(&plugin,DebugWarn,"YateTCPParty no transport to send %s [%p]",
	tmp.c_str(),this);
}

const char* YateTCPParty::getProtoName() const
{
    if (m_transport)
	return m_transport->protoName();
    return "TCP";
}

bool YateTCPParty::setParty(const URI& uri)
{
    Lock lock(m_mutex);
    if (m_partyPort && m_party && s_ignoreVia)
	return true;
    Debug(&plugin,DebugWarn,"YateTCPParty::setParty(%s) not implemented [%p]",
	uri.safe(),this);
    return false;
}

void* YateTCPParty::getTransport()
{
    return m_transport;
}

// Get an object from this one
void* YateTCPParty::getObject(const String& name) const
{
    if (name == YSTRING("YateTCPParty"))
	return (void*)this;
    if (name == YSTRING("YateSIPTCPTransport") || name == YSTRING("YateSIPTransport"))
	return m_transport;
    return SIPParty::getObject(name);
}

void YateTCPParty::updateAddrs()
{
    if (!m_transport)
	return;
    m_transport->lock();
    String laddr = m_transport->local().host();
    int lport = m_transport->local().port();
    String raddr = m_transport->remote().host();
    int rport = m_transport->remote().port();
    SocketAddr remote(PF_INET);
    if (raddr)
	remote = m_transport->remote();
    else {
	remote.host(m_transport->remoteAddr());
	remote.port(m_transport->remotePort());
	raddr = remote.host();
	rport = remote.port();
    }
    m_transport->unlock();
    if (!laddr) {
	SocketAddr addr;
	if (addr.local(remote))
	    laddr = addr.host();
	else
	    laddr = "localhost";
    }
    if (lport <= 0)
	lport = sipPort(!m_transport->tls());
    setAddr(laddr,lport,true);
    setAddr(raddr,rport,false);
}

void YateTCPParty::destroyed()
{
    DDebug(&plugin,DebugAll,"YateTCPParty::destroyed() transport=%p [%p]",
	m_transport,this);
    if (m_transport) {
	m_transport->resetParty(this,false);
	TelEngine::destruct(m_transport);
    }
    SIPParty::destroyed();
}


YateSIPEngine::YateSIPEngine(YateSIPEndPoint* ep)
    : SIPEngine(s_cfg.getValue("general","useragent")),
      m_ep(ep), m_prack(false), m_info(false)
{
    addAllowed("INVITE");
    addAllowed("BYE");
    addAllowed("CANCEL");
    if (s_cfg.getBoolValue("general","registrar",!Engine::clientMode()))
	addAllowed("REGISTER");
    if (s_cfg.getBoolValue("general","transfer",!Engine::clientMode()))
	addAllowed("REFER");
    if (s_cfg.getBoolValue("general","options",true))
	addAllowed("OPTIONS");
    m_prack = s_cfg.getBoolValue("general","prack");
    if (m_prack)
	addAllowed("PRACK");
    m_info = s_cfg.getBoolValue("general","info",true);
    if (m_info)
	addAllowed("INFO");
    lazyTrying(s_cfg.getBoolValue("general","lazy100",false));
    m_fork = s_cfg.getBoolValue("general","fork",true);
    m_flags = s_cfg.getIntValue("general","flags",m_flags);
    NamedList *l = s_cfg.getSection("methods");
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString *n = l->getParam(i);
	    if (!n)
		continue;
	    String meth(n->name());
	    meth.toUpper();
	    addAllowed(meth);
	}
    }
    initialize(s_cfg.getSection("general"));
}

// Initialize the engine
void YateSIPEngine::initialize(NamedList* params)
{
    NamedList dummy("");
    if (!params)
	params = &dummy;
    m_reqTransCount = params->getIntValue("sip_req_trans_count",4,2,10,false);
    m_rspTransCount = params->getIntValue("sip_rsp_trans_count",5,2,10,false);
    DDebug(this,DebugAll,"Initialized sip_req_trans_count=%d sip_rsp_trans_count=%d",
	m_reqTransCount,m_rspTransCount);
}

SIPTransaction* YateSIPEngine::forkInvite(SIPMessage* answer, SIPTransaction* trans)
{
    if (m_fork && trans->isActive() && (answer->code/100) == 2)
    {
	Debug(this,DebugNote,"Changing early dialog tag because of forked 2xx");
	trans->setDialogTag(answer->getParamValue("To","tag"));
	trans->processMessage(answer);
	return trans;
    }
    return SIPEngine::forkInvite(answer,trans);
}

// Transport status changed notification
void YateSIPEngine::transportChangedStatus(YateSIPTransport* trans, int stat, const String& reason)
{
    if (!(trans && stat == YateSIPTransport::Terminated))
	return;
    // Clear transactions
    Lock lock(this);
    for (ObjList* l = m_transList.skipNull(); l; l = l->skipNext()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	if (t->initialMessage() && t->initialMessage()->getParty() &&
	    trans == t->initialMessage()->getParty()->getTransport()) {
	    bool active = t->isActive();
	    Debug(this,active ? DebugInfo : DebugAll,
		"Clearing %stransaction (%p) transport terminated reason=%s",
		active ? "active " : "",t,reason.c_str());
	    t->setCleared();
	}
    }
}

// Check if the engine has an active transaction using a given transport
bool YateSIPEngine::hasActiveTransaction(YateSIPTransport* trans)
{
    if (!trans)
	return false;
    Lock lock(this);
    for (ObjList* l = m_transList.skipNull(); l; l = l->skipNext()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	if (t->isActive() && t->initialMessage() && t->initialMessage()->getParty() &&
	    trans == t->initialMessage()->getParty()->getTransport()	    )
	    return true;
    }
    return false;
}

// Check if the engine has pending transactions
bool YateSIPEngine::hasInitialTransaction()
{
    Lock lock(this);
    for (ObjList* l = m_transList.skipNull(); l; l = l->skipNext()) {
	SIPTransaction* t = static_cast<SIPTransaction*>(l->get());
	if (t->getState() == SIPTransaction::Initial)
	    return true;
    }
    return false;
}

bool YateSIPEngine::buildParty(SIPMessage* message)
{
    return m_ep->buildParty(message);
}

bool YateSIPEngine::copyAuthParams(NamedList* dest, const NamedList& src, bool ok)
{
    // we added those and we want to exclude them from copy
    static TokenDict exclude[] = {
	{ "protocol", 1 },
	// purposely copy the username and realm
	{ "nonce", 1 },
	{ "method", 1 },
	{ "uri", 1 },
	{ "response", 1 },
	{ "ip_host", 1 },
	{ "ip_port", 1 },
	{ "address", 1 },
	{ "billid", 1 },
	{  0,   0 },
    };
    if (!dest)
	return ok;
    unsigned int n = src.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* s = src.getParam(i);
	if (!s)
	    continue;
	String name = s->name();
	if (name.startSkip("authfail_",false) == ok)
	    continue;
	if (name.toInteger(exclude,0))
	    continue;
	dest->setParam(name,*s);
    }
    return ok;
}

bool YateSIPEngine::checkUser(const String& username, const String& realm, const String& nonce,
    const String& method, const String& uri, const String& response,
    const SIPMessage* message, GenObject* userData)
{
    NamedList* params = YOBJECT(NamedList,userData);

    Message m("user.auth");
    m.addParam("protocol","sip");
    if (username) {
	m.addParam("username",username);
	m.addParam("realm",realm);
	m.addParam("nonce",nonce);
	m.addParam("response",response);
    }
    m.addParam("method",method);
    m.addParam("uri",uri);
    if (message) {
	String raddr;
	int rport = 0;
	message->getParty()->getAddr(raddr,rport,false);
	String port(rport);
	m.addParam("ip_host",raddr);
	m.addParam("ip_port",port);
	m.addParam("ip_transport",message->getParty()->getProtoName());
	if (raddr)
	    m.addParam("address",raddr + ":" + port);
	// a dialogless INVITE could create a new call
	m.addParam("newcall",String::boolText((message->method == YSTRING("INVITE")) && !message->getParam("To","tag")));
	const MimeHeaderLine* hl = message->getHeader("From");
	if (hl) {
	    URI from(*hl);
	    m.addParam("domain",from.getHost());
	}
    }

    if (params) {
	m.copyParam(*params,"caller");
	m.copyParam(*params,"called");
	m.copyParam(*params,"billid");
    }

    if (!Engine::dispatch(m))
	return copyAuthParams(params,m,false);

    // empty password returned means authentication succeeded
    if (m.retValue().null())
	return copyAuthParams(params,m);
    // check for refusals
    if (m.retValue() == "-") {
	if (params) {
	    const char* err = m.getValue(YSTRING("error"));
	    if (err)
		params->setParam("error",err);
	    err = m.getValue(YSTRING("reason"));
	    if (err)
		params->setParam("reason",err);
	}
	return copyAuthParams(params,m,false);
    }
    // password works only with username
    if (!username)
	return copyAuthParams(params,m,false);

    String res;
    buildAuth(username,realm,m.retValue(),nonce,method,uri,res);
    if (res == response)
	return copyAuthParams(params,m);
    // if the URI included some parameters retry after stripping them off
    int sc = uri.find(';');
    bool ok = false;
    if (sc >= 0) {
	buildAuth(username,realm,m.retValue(),nonce,method,uri.substr(0,sc),res);
	ok = (res == response) && copyAuthParams(params,m);
    }

    if (!ok && !response.null()) {
	DDebug(&plugin,DebugNote,"Failed authentication for username='%s'",username.c_str());
	m_ep->incFailedAuths();
	plugin.changed();
	Message* fail = new Message(m);
	*fail = "user.authfail";
	fail->retValue().clear();
	Engine::enqueue(fail);
    }
    return ok || copyAuthParams(params,m,false);
}

YateSIPEndPoint::YateSIPEndPoint(Thread::Priority prio, unsigned int partyMutexCount)
    : Thread("YSIP EndPoint",prio),
      m_partyMutexPool(partyMutexCount,true,"SIPParty"),
      m_engine(0), m_mutex(true,"YateSIPEndPoint"), m_defTransport(0),
      m_failedAuths(0),m_timedOutTrs(0), m_timedOutByes(0)
{
    Debug(&plugin,DebugAll,"YateSIPEndPoint::YateSIPEndPoint(%s) [%p]",
	Thread::priority(prio),this);
}

YateSIPEndPoint::~YateSIPEndPoint()
{
    Debug(&plugin,DebugAll,"YateSIPEndPoint::~YateSIPEndPoint() [%p]",this);
    plugin.channels().clear();
    s_lines.clear();
    if (m_engine) {
	// send any pending events
	while (m_engine->process())
	    ;
	delete m_engine;
	m_engine = 0;
    }
    m_defTransport = 0;
}

bool YateSIPEndPoint::buildParty(SIPMessage* message, const char* host, int port, const YateSIPLine* line)
{
    if (message->isAnswer())
	return false;
    DDebug(&plugin,DebugAll,"YateSIPEndPoint::buildParty(%p,'%s',%d,%p)",
	message,host,port,line);
    if (line && line->setSipParty(message,line))
	return true;
    // Find transport
    YateSIPUDPTransport* trans = defTransport();
    if (!trans && line && line->getLocalAddr())
	trans = findUdpTransport(line->getLocalAddr(),line->getLocalPort());
    if (!trans)
	return false;
    // Build an udp party
    URI uri(message->uri);
    if (line) {
	if (!host)
	    host = line->getPartyAddr();
	if (port <= 0)
	    port = line->getPartyPort();
	line->setupAuth(message);
    }
    if (!host) {
	host = uri.getHost().safe();
	if (port <= 0)
	    port = uri.getPort();
    }
    if (port <= 0)
	port = 5060;
    SocketAddr addr(AF_INET);
    if (!addr.host(host)) {
	TelEngine::destruct(trans);
	Debug(&plugin,DebugWarn,"Error resolving name '%s'",host);
	return false;
    }
    addr.port(port);
    DDebug(&plugin,DebugAll,"built addr: %s:%d",addr.host().c_str(),addr.port());
    YateUDPParty* party = new YateUDPParty(trans,addr);
    TelEngine::destruct(trans);
    message->setParty(party);
    TelEngine::destruct(party);
    return true;
}

// (re)set default UDP transport
void YateSIPEndPoint::updateDefUdpTransport()
{
    static const String s_general("general");
    Lock lock(m_mutex);
    m_defTransport = 0;
    // Find a non-general default transport
    for (ObjList* o = m_transports.skipNull(); o; o = o->skipNext()) {
	YateSIPTransport* t = static_cast<YateSIPTransport*>(o->get());
	m_defTransport = t->udpTransport();
	if (m_defTransport && m_defTransport->toString() != s_general &&
	    m_defTransport->isDefault())
	    break;
	m_defTransport = 0;
    }
    if (!m_defTransport) {
	m_defTransport = findUdpTransport(s_general);
	if (m_defTransport)
	    m_defTransport->deref();
    }
    if (m_defTransport)
	Debug(&plugin,DebugInfo,"Default UDP transport is '%s'",
	    m_defTransport->toString().c_str());
    else if (!Engine::exiting())
	Debug(&plugin,DebugNote,"Default UDP transport not set");
}

// Retrieve a transport by name. Return referrenced object
YateSIPTransport* YateSIPEndPoint::findTransport(const String& name)
{
    if (!name)
	return 0;
    Lock lock(m_mutex);
    ObjList* o = m_transports.find(name);
    YateSIPTransport* t = o ? static_cast<YateSIPTransport*>(o->get()) : 0;
    return (t && t->ref()) ? t : 0;
}

// Retrieve an UDP transport by name. Return referrenced object
YateSIPUDPTransport* YateSIPEndPoint::findUdpTransport(const String& name)
{
    YateSIPTransport* t = findTransport(name);
    YateSIPUDPTransport* trans = t ? t->udpTransport() : 0;
    if (!trans)
	TelEngine::destruct(t);
    return trans;
}

// Retrieve an UDP transport by addr/port. Return referrenced object
YateSIPUDPTransport* YateSIPEndPoint::findUdpTransport(const String& addr, int port)
{
    Lock lock(m_mutex);
    for (ObjList* o = m_transports.skipNull(); o; o = o->skipNext()) {
	YateSIPTransport* t = static_cast<YateSIPTransport*>(o->get());
	if (!t->udpTransport())
	    continue;
	Lock lck(t);
	if (t->local().port() == port && t->local().host() == addr)
	    return t->ref() ? static_cast<YateSIPUDPTransport*>(t) : 0;
    }
    return 0;
}

// Build an UDP transport (re-init existing). Start the thread
bool YateSIPEndPoint::setupUdpTransport(const String& name, bool enabled,
    const NamedList& params, const NamedList& defs, const char* reason)
{
    if (!name)
	return false;
    YateSIPUDPTransport* rd = findUdpTransport(name);
    if (rd) {
	const char* addr = params.getValue("addr","0.0.0.0");
	int port = params.getIntValue("port",5060);
	bool stop = enabled && !rd->addrWouldChange(rd,true,addr,port);
	if (stop)
	    rd->init(params,defs,false);
	else {
	    removeUdpTransport(name,enabled ? "Address changed" : (reason ? reason : "Disabled"));
	    rd->deref();
	}
	TelEngine::destruct(rd);
	if (stop)
	    return true;
    }
    if (!enabled)
	return true;
    Lock lock(m_mutex);
    const char* s = params.getValue("thread",defs.getValue("thread"));
    rd = new YateSIPUDPTransport(name);
    rd->init(params,defs,true,Thread::priority(s));
    m_transports.append(rd);
    return true;
}

// Delete an UDP transport
bool YateSIPEndPoint::removeUdpTransport(const String& name, const char* reason)
{
    XDebug(&plugin,DebugAll,"YateSIPEndPoint::removeUdpTransport(%s,%s)",name.c_str(),reason);
    if (!name)
	return false;
    YateSIPUDPTransport* rd = findUdpTransport(name);
    if (!rd)
	return false;
    Debug(&plugin,DebugAll,"Removing udp transport '%s': %s",name.c_str(),reason);
    removeTransport(rd,false);
    // Terminate channels, disconnect lines
    plugin.transportTerminated(rd);
    // Notify lines
    for (ObjList* ol = s_lines.skipNull(); ol; ol = ol->skipNext()) {
	YateSIPLine* line = static_cast<YateSIPLine*>(ol->get());
	if (line->isTransport(rd))
	    line->transportChangedStatus(YateSIPTransport::Terminated,reason);
    }
    // Wait for active transactions to terminate
    if (!Engine::exiting()) {
	unsigned int intervals = (unsigned int)(s_waitActiveUdpTrans / Thread::idleUsec());
	if (!intervals)
	    intervals = 1;
	while (intervals > 0 && !Engine::exiting() &&
	    m_engine->hasActiveTransaction(rd)) {
	    Thread::idle();
	    intervals--;
	}
	if (!intervals)
	    Debug(&plugin,DebugNote,
		"Removing udp transport '%s' with active transactions using it",
		name.c_str());
    }
    rd->terminate(reason);
    TelEngine::destruct(rd);
    return true;
}

// Remove a transport from list
bool YateSIPEndPoint::removeTransport(YateSIPTransport* trans, bool updDef)
{
    if (!trans)
	return false;
    Lock lock(m_mutex);
    if (!m_transports.remove(trans,false))
	return false;
    Debug(&plugin,DebugAll,"Removed transport (%p,'%s')",trans,trans->toString().c_str());
    if (trans == m_defTransport) {
	Debug(&plugin,DebugInfo,"Reset default UDP transport");
	m_defTransport = 0;
	if (updDef)
	    updateDefUdpTransport();
    }
    return true;
}

// Remove all transports
void YateSIPEndPoint::clearUdpTransports(const char* reason)
{
#ifdef DEBUG
    Debugger debug(DebugAll,"YateSIPEndPoint::clearUdpTransports()");
#else
    Debug(&plugin,DebugAll,"Clearing udp transports reason=%s",reason);
#endif
    while (true) {
	Lock lock(m_mutex);
	RefPointer<YateSIPUDPTransport> trans;
	for (ObjList* o = m_transports.skipNull(); o; o = o->skipNext()) {
	    trans = YOBJECT(YateSIPUDPTransport,o->get());
	    if (trans)
		break;
	}
	if (!trans)
	    break;
	lock.drop();
	removeTransport(trans);
	trans->terminate(reason);
	trans->deref();
	trans = 0;
    }
}

// TCP transport status changed notification
void YateSIPEndPoint::transportChangedStatus(YateSIPTransport* trans, int stat, const String& reason)
{
    if (!trans)
	return;
    DDebug(&plugin,DebugAll,"YateSIPEndPoint::transportChangedStatus(%p,%s,%s)",
	trans,YateSIPTransport::statusName(stat),reason.c_str());
    // Remove from list
    if (stat == YateSIPTCPTransport::Terminated)
	removeTransport(trans);
    // Notify lines
    for (ObjList* ol = s_lines.skipNull(); ol; ol = ol->skipNext()) {
	YateSIPLine* line = static_cast<YateSIPLine*>(ol->get());
	if (line->isTransport(trans))
	    line->transportChangedStatus(stat,reason);
    }
    // Notify transactions in engine
    if (m_engine)
	m_engine->transportChangedStatus(trans,stat,reason);
    if (stat != YateSIPTCPTransport::Terminated)
	return;
    // Notify unregister
    if (!Engine::exiting()) {
	Message* m = new Message("user.unregister");
	m->addParam("connection_id",trans->toString());
	Engine::enqueue(m);
    }
    // Notify channels
    plugin.transportTerminated(trans);
}

// Build or delete a TCP listener. Start the thread
bool YateSIPEndPoint::setupListener(int proto, const String& name, bool enabled, const NamedList& params)
{
    if (!name)
	return false;
    Lock lock(m_mutex);
    ObjList* o = m_listeners.find(name);
    if (o) {
	YateSIPTCPListener* l = static_cast<YateSIPTCPListener*>(o->get());
	if (enabled) {
	    if (l->protocol() == proto)
		l->init(params,false);
	    else {
		lock.drop();
		cancelListener(name,"Type changed");
		return setupListener(proto,name,enabled,params);
	    }
	}
	else {
	    lock.drop();
	    cancelListener(name,"Disabled");
	}
	return true;
    }
    if (!enabled)
	return true;
    // Build it
    YateSIPTCPListener* listener = new YateSIPTCPListener(proto,name,params);
    if (listener->startup()) {
	m_listeners.append(listener);
	DDebug(&plugin,DebugAll,"Added listener %p '%s'",listener,listener->toString().c_str());
	return true;
    }
    Debug(&plugin,DebugWarn,"Failed to start listener thread type=%s name='%s'",
	ProtocolHolder::lookupProtoName(proto),name.c_str());
    return false;
}

// Remove a listener from list without deleting it. Return true if found
bool YateSIPEndPoint::removeListener(YateSIPTCPListener* listener)
{
    if (!listener)
	return false;
    Lock lock(m_mutex);
    if (!m_listeners.remove(listener,false))
	return false;
    DDebug(&plugin,DebugAll,"Removed listener (%p,'%s')",listener,listener->toString().c_str());
    return true;
}

// Remove a listener from list. Remove all if name is empty. Wait for termination
void YateSIPEndPoint::cancelListener(const String& name, const char* reason)
{
    m_mutex.lock();
    bool wait = false;
    for (ObjList* o = m_listeners.skipNull(); o; o = o->skipNext()) {
	YateSIPTCPListener* l = static_cast<YateSIPTCPListener*>(o->get());
	if (name && name != l->toString())
	    continue;
	wait = true;
	Debug(&plugin,DebugAll,"Stopping listener (%p,'%s') reason=%s",
	    l,l->toString().c_str(),reason);
	l->setReason(reason);
	l->cancel();
	if (name)
	    break;
    }
    m_mutex.unlock();
    if (!wait)
	return;
    while (true) {
	Thread::idle();
	Lock lck(m_mutex);
	ObjList* o = !name ? m_listeners.skipNull() : m_listeners.find(name);
	if (!o)
	    break;
    }
    if (!name)
    	Debug(&plugin,DebugAll,"Stopped all listeners");
    else
    	Debug(&plugin,DebugAll,"Stopped listener '%s'",name.c_str());
}

// This method is called by the driver when start/end initializing
// start==true: Reset initialized flag for listeners and UDP transports
// start==false: Terminate not initialized listeners and UDP transports
void YateSIPEndPoint::initializing(bool start)
{
    ObjList rmListener;
    ObjList rmUdpTrans;
    m_mutex.lock();
    for (ObjList* o = m_listeners.skipNull(); o; o = o->skipNext()) {
	YateSIPTCPListener* l = static_cast<YateSIPTCPListener*>(o->get());
	if (start)
	    l->m_initialized = false;
	else if (!l->m_initialized)
	    rmListener.append(new String(l->toString()));
    }
    for (ObjList* o = m_transports.skipNull(); o; o = o->skipNext()) {
	YateSIPTCPTransport* t = static_cast<YateSIPTCPTransport*>(o->get());
	if (start)
	    t->m_initialized = false;
	else if (!t->m_initialized && t->udpTransport())
	    rmUdpTrans.append(new String(t->toString()));
    }
    m_mutex.unlock();
    if (start)
	return;
    for (ObjList* o = rmListener.skipNull(); o; o = o->skipNext()) {
	String* name = static_cast<String*>(o->get());
	Debug(&plugin,DebugNote,"Stopping deleted listener '%s'",name->c_str());
	cancelListener(*name,"Deleted");
    }
    for (ObjList* o = rmUdpTrans.skipNull(); o; o = o->skipNext())
	removeUdpTransport(o->get()->toString(),"Deleted");
}

bool YateSIPEndPoint::Init()
{
    m_engine = new YateSIPEngine(this);
    m_engine->debugChain(&plugin);
    return true;
}

// Check if data is allowed to be read from socket(s) and processed
bool YateSIPEndPoint::canRead()
{
    return s_floodEvents <= 1 || (s_evCount < s_floodEvents) || Engine::exiting();
}

void YateSIPEndPoint::run()
{
    for (;;)
    {
	if (!canRead()) {
	    if (s_evCount == s_floodEvents)
	        Debug(&plugin,DebugMild,"Flood detected: %d handled events",s_evCount);
	    else if ((s_evCount % s_floodEvents) == 0)
	        Debug(&plugin,DebugWarn,"Severe flood detected: %d events",s_evCount);
	}
	SIPEvent* e = m_engine->getEvent();
	if (e)
	    s_evCount++;
	else 
	    s_evCount = 0;
	// hack: use a loop so we can use break and continue
	for (; e; m_engine->processEvent(e),e = 0) {
	    SIPTransaction* t = e->getTransaction();
	    if (!t)
		continue;
	    plugin.lock();

	    if (t->isOutgoing() && t->getResponseCode() == 408) {
	    	if (t->getMethod() == YSTRING("BYE")) {
		    DDebug(&plugin,DebugInfo,"BYE for transaction %p has timed out",t);
		    m_timedOutByes++;
		    plugin.changed();
		}
		if (e->getState() == SIPTransaction::Cleared && e->getUserData()) {
		    DDebug(&plugin,DebugInfo,"Transaction %p has timed out",t);
		    m_timedOutTrs++;
		    plugin.changed();
		}
	    }

	    GenObject* obj = static_cast<GenObject*>(t->getUserData());
	    RefPointer<YateSIPConnection> conn = YOBJECT(YateSIPConnection,obj);
	    YateSIPLine* line = YOBJECT(YateSIPLine,obj);
	    YateSIPGenerate* gen = YOBJECT(YateSIPGenerate,obj);
	    plugin.unlock();
	    if (conn) {
		if (conn->process(e)) {
		    delete e;
		    break;
		}
		else
		    continue;
	    }
	    if (line) {
		if (line->process(e)) {
		    delete e;
		    break;
		}
		else
		    continue;
	    }
	    if (gen) {
		if (gen->process(e)) {
		    delete e;
		    break;
		}
		else
		    continue;
	    }
	    if ((e->getState() == SIPTransaction::Trying) &&
		!e->isOutgoing() && incoming(e,e->getTransaction())) {
		delete e;
		break;
	    }
	}
	if (s_evCount || s_engineHalt)
	    Thread::check();
	else
	    Thread::usleep(Thread::idleUsec());
    }
}

bool YateSIPEndPoint::incoming(SIPEvent* e, SIPTransaction* t)
{
    if (t->isInvite())
	invite(e,t);
    else if (t->getMethod() == YSTRING("BYE")) {
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	if (conn) {
	    conn->doBye(t);
	    conn->deref();
	}
	else
	    t->setResponse(481);
    }
    else if (t->getMethod() == YSTRING("CANCEL")) {
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	if (conn) {
	    conn->doCancel(t);
	    conn->deref();
	}
	else
	    t->setResponse(481);
    }
    else if (t->getMethod() == YSTRING("INFO")) {
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	bool done = false;
	if (conn) {
	    done = conn->doInfo(t);
	    conn->deref();
	    if (!done)
		done = generic(e,t);
	}
	else if (t->getDialogTag()) {
	    done = true;
	    t->setResponse(481);
	}
	else
	    done = generic(e,t);
	if (!done)
	    t->setResponse(415);
    }
    else if (t->getMethod() == YSTRING("REGISTER"))
	regReq(e,t);
    else if (t->getMethod() == YSTRING("OPTIONS"))
	options(e,t);
    else if (t->getMethod() == YSTRING("REFER")) {
	YateSIPConnection* conn = plugin.findCall(t->getCallID(),true);
	if (conn) {
	    conn->doRefer(t);
	    conn->deref();
	}
	else
	    t->setResponse(481);
    }
    else
	return generic(e,t);
    return true;
}

void YateSIPEndPoint::invite(SIPEvent* e, SIPTransaction* t)
{
    if (!plugin.canAccept()) {
	Debug(&plugin,DebugWarn,"Refusing new SIP call, full or exiting");
	t->setResponse(480);
	return;
    }

    if (e->getMessage()->getParam("To","tag")) {
	SIPDialog dlg(*e->getMessage());
	YateSIPConnection* conn = plugin.findDialog(dlg,true);
	if (conn) {
	    conn->reInvite(t);
	    conn->deref();
	}
	else {
	    Debug(&plugin,DebugWarn,"Got re-INVITE for missing dialog");
	    t->setResponse(481);
	}
	return;
    }

    YateSIPConnection* conn = new YateSIPConnection(e,t);
    conn->initChan();
    conn->startRouter();

}

void YateSIPEndPoint::regReq(SIPEvent* e, SIPTransaction* t)
{
    if (Engine::exiting()) {
	Debug(&plugin,DebugWarn,"Dropping request, engine is exiting");
	t->setResponse(500, "Server Shutting Down");
	return;
    }
    if (s_reg_async) {
	YateSIPRegister* reg = new YateSIPRegister(this,e->getMessage(),t);
	if (reg->startup())
	    return;
	Debug(&plugin,DebugWarn,"Failed to start register thread");
	delete reg;
    }
    regRun(e->getMessage(),t);
}

void YateSIPEndPoint::regRun(const SIPMessage* message, SIPTransaction* t)
{
    const MimeHeaderLine* hl = message->getHeader("Contact");
    if (!hl) {
	t->setResponse(400);
	return;
    }

    Message msg("user.register");
    msg.addParam("sip_uri",t->getURI());
    msg.addParam("sip_callid",t->getCallID());
    String user;
    int age = t->authUser(user,false,&msg);
    DDebug(&plugin,DebugAll,"User '%s' age %d",user.c_str(),age);
    if (((age < 0) || (age > 10)) && s_auth_register) {
	Lock lck(s_globalMutex);
	t->requestAuth(s_realm,"",age >= 0);
	return;
    }

    // TODO: track registrations, allow deregistering all
    if (*hl == "*") {
	t->setResponse(200);
	return;
    }

    URI addr(*hl);
    if (user.null())
	user = addr.getUser();
    msg.setParam("username",user);
    msg.setParam("number",addr.getUser());
    msg.setParam("driver","sip");
    String data(addr);
    String raddr;
    int rport = 0;
    message->getParty()->getAddr(raddr,rport,false);
    bool nat = isNatBetween(addr.getHost(),raddr);
    if (!nat) {
	int port = addr.getPort();
	if (!port)
	    port = 5060;
	nat = (rport != port) && msg.getBoolValue(YSTRING("nat_port_support"),true);
    }
    bool natChanged = false;
    if (msg.getBoolValue(YSTRING("nat_support"),s_auto_nat && nat)) {
	Debug(&plugin,DebugInfo,"Registration NAT detected: private '%s:%d' public '%s:%d'",
	    addr.getHost().c_str(),addr.getPort(),raddr.c_str(),rport);
	String tmp(addr.getHost());
	if (addr.getPort())
	    tmp << ":" << addr.getPort();
	msg.addParam("reg_nat_addr",tmp);
	int pos = data.find(tmp);
	if (pos >= 0) {
	    int len = tmp.length();
	    tmp.clear();
	    tmp << data.substr(0,pos) << raddr << ":" << rport << data.substr(pos + len);
	    data = tmp;
	    natChanged = true;
	}
    }
    msg.setParam("data","sip/" + data);
    msg.setParam("ip_host",raddr);
    msg.setParam("ip_port",String(rport));
    msg.setParam("ip_transport",message->getParty()->getProtoName());

    bool dereg = false;
    String tmp(message->getHeader("Expires"));
    if (tmp.null())
	tmp = hl->getParam("expires");
    int expires = tmp.toInteger(-1);
    if (expires < 0)
	expires = s_expires_def;
    if (expires > s_expires_max)
	expires = s_expires_max;
    if (expires && (expires < s_expires_min)) {
	tmp = s_expires_min;
	SIPMessage* r = new SIPMessage(t->initialMessage(),423);
	r->addHeader("Min-Expires",tmp);
	t->setResponse(r);
	r->deref();
	return;
    }
    tmp = expires;
    msg.setParam("expires",tmp);
    if (!expires) {
	msg = "user.unregister";
	dereg = true;
    }
    else
	msg.setParam("sip_to",addr);
    hl = message->getHeader("User-Agent");
    if (hl)
	msg.setParam("device",*hl);
    // Add transport if registering
    if (expires && message->getParty()) {
	YateSIPTransport* trans = static_cast<YateSIPTransport*>(message->getParty()->getTransport());
	if (trans)
	    trans->fillMessage(msg,true);
    }
    // Always OK deregistration attempts
    if (Engine::dispatch(msg) || dereg) {
	if (dereg) {
	    t->setResponse(200);
	    Debug(&plugin,DebugNote,"Unregistered user '%s'",user.c_str());
	}
	else {
	    tmp = msg.getValue(YSTRING("expires"),tmp);
	    if (tmp.null())
		tmp = expires;
	    SIPMessage* r = new SIPMessage(t->initialMessage(),200);
	    r->addHeader("Expires",tmp);
	    MimeHeaderLine* contact = new MimeHeaderLine("Contact","<" + addr + ">");
	    contact->setParam("expires",tmp);
	    r->addHeader(contact);
	    if (natChanged) {
		if (s_nat_refresh > 0)
		    r->addHeader("P-NAT-Refresh",String(s_nat_refresh));
		r->addHeader("X-Real-Contact",data);
	    }
	    if (t->initialMessage() && t->initialMessage()->getParty() &&
		t->initialMessage()->getParty()->isReliable()) {
		const String& ftValue = msg[YSTRING("xsip_flow-timer")];
		int flowTimer = ftValue.toInteger();
		if (flowTimer > 10 && flowTimer <= 120)
		    r->addHeader(new MimeHeaderLine("Flow-Timer",ftValue));
	    }
	    // Reset transport timeout
	    resetTransportIdle(r,tmp.toInteger());
	    t->setResponse(r);
	    r->deref();
	    Debug(&plugin,DebugNote,"Registered user '%s' expires in %s s%s",
		user.c_str(),tmp.c_str(),natChanged ? " (NAT)" : "");
	}
    }
    else
	t->setResponse(404);
}

void YateSIPEndPoint::options(SIPEvent* e, SIPTransaction* t)
{
    const MimeHeaderLine* acpt = e->getMessage()->getHeader("Accept");
    if (acpt) {
	if (*acpt != YSTRING("application/sdp")) {
	    t->setResponse(415);
	    return;
	}
    }
    t->setResponse(200);
}

bool YateSIPEndPoint::generic(SIPEvent* e, SIPTransaction* t)
{
    String meth(t->getMethod());
    meth.toLower();
    String user;
    const String* auth = s_cfg.getKey("methods",meth);
    if (!auth)
	return false;

    Message m("sip." + meth);
    const SIPMessage* message = e->getMessage();
    String host;
    int portNum = 0;
    message->getParty()->getAddr(host,portNum,false);
    URI uri(message->uri);
    YateSIPLine* line = plugin.findLine(host,portNum,uri.getUser());
    if (line) {
	// message comes from line we have registered to
	if (user.null())
	    user = line->getUserName();
	m.addParam("domain",line->domain());
	m.addParam("in_line",*line);
    }
    else if (auth->toBoolean(true)) {
	int age = t->authUser(user,false,&m);
	DDebug(&plugin,DebugAll,"User '%s' age %d",user.c_str(),age);
	if ((age < 0) || (age > 10)) {
	    Lock lck(s_globalMutex);
	    t->requestAuth(s_realm,"",age >= 0);
	    return true;
	}
    }
    // Add transport info
    YateSIPTransport* trans = YOBJECT(YateSIPTransport,message->getParty());
    if (trans)
	trans->fillMessage(m);
    if (message->getParam("To","tag")) {
	SIPDialog dlg(*message);
	YateSIPConnection* conn = plugin.findDialog(dlg,true);
	if (conn) {
	    m.userData(conn);
	    conn->complete(m);
	    conn->deref();
	}
    }
    m.addParam("username",user,false);
    m.addParam("called",uri.getUser(),false);
    uri = message->getHeader("From");
    uri.parse();
    m.addParam("caller",uri.getUser(),false);
    m.addParam("callername",uri.getDescription(),false);

    String tmp(message->getHeaderValue("Max-Forwards"));
    int maxf = tmp.toInteger(s_maxForwards);
    if (maxf > s_maxForwards)
	maxf = s_maxForwards;
    tmp = maxf-1;
    m.addParam("antiloop",tmp);

    String port(portNum);
    m.addParam("address",host + ":" + port);
    m.addParam("ip_host",host);
    m.addParam("ip_port",port);
    m.addParam("ip_transport",message->getParty()->getProtoName());
    m.addParam("sip_uri",t->getURI());
    m.addParam("sip_callid",t->getCallID());
    // establish the dialog here so user code will have the dialog tag handy
    t->setDialogTag();
    m.addParam("xsip_dlgtag",t->getDialogTag());
    copySipHeaders(m,*message,false);

    doDecodeIsupBody(&plugin,m,message->body);
    // add the body if it's a string one
    MimeStringBody* strBody = YOBJECT(MimeStringBody,message->body);
    if (strBody) {
	m.addParam("xsip_type",strBody->getType());
	m.addParam("xsip_body",strBody->text());
    }
    else {
	MimeLinesBody* txtBody = YOBJECT(MimeLinesBody,message->body);
	if (txtBody) {
	    String bodyText((const char*)txtBody->getBody().data(),txtBody->getBody().length());
	    m.addParam("xsip_type",txtBody->getType());
	    m.addParam("xsip_body",bodyText);
	}
	else if (message->body) {
	    const DataBlock& binBody = message->body->getBody();
	    String bodyText;
	    Base64 b64(binBody.data(),binBody.length(),false);
	    b64.encode(bodyText);
	    b64.clear(false);
	    m.addParam("xsip_type",message->body->getType());
	    m.addParam("xsip_body_encoding","base64");
	    m.addParam("xsip_body",bodyText);
	}
    }

    int code = 0;
    if (Engine::dispatch(m)) {
	const String* ret = m.getParam(YSTRING("code"));
	if (!ret)
	    ret = &m.retValue();
	code = ret->toInteger(m.getIntValue(YSTRING("reason"),dict_errors,200));
    }
    else {
	code = m.getIntValue(YSTRING("code"),m.getIntValue(YSTRING("reason"),dict_errors,0));
	if (code < 300)
	    code = 0;
    }
    if ((code >= 200) && (code < 700)) {
	SIPMessage* resp = new SIPMessage(message,code);
	copySipHeaders(*resp,m);
	t->setResponse(resp);
	resp->deref();
	return true;
    }
    return false;
}


// Build the transfer thread
// transferorID: Channel id of the sip connection that received the REFER request
// transferredID: Channel id of the transferor's peer
// transferredDrv: Channel driver of the transferor's peer
// msg: already populated 'call.route'
// sipNotify: already populated SIPMessage("NOTIFY")
YateSIPRefer::YateSIPRefer(const String& transferorID, const String& transferredID,
    Driver* transferredDrv, Message* msg, SIPMessage* sipNotify,
    SIPTransaction* transaction)
    : Thread("YSIP Transfer"),
    m_transferorID(transferorID), m_transferredID(transferredID),
    m_transferredDrv(transferredDrv), m_msg(msg), m_sipNotify(sipNotify),
    m_notifyCode(200), m_transaction(0), m_rspCode(500)
{
    if (transaction && transaction->ref())
	m_transaction = transaction;
}

void YateSIPRefer::run()
{
    String* attended = m_msg->getParam(YSTRING("transfer_callid"));
#ifdef DEBUG
    if (attended)
	Debug(&plugin,DebugAll,"%s(%s) running callid=%s fromtag=%s totag=%s [%p]",
	    name(),m_transferorID.c_str(),attended->c_str(),
	    m_msg->getValue(YSTRING("transfer_fromtag")),
	    m_msg->getValue(YSTRING("transfer_totag")),this);
    else
	Debug(&plugin,DebugAll,"%s(%s) running [%p]",name(),m_transferorID.c_str(),this);
#endif

    // Use a while() to break to the end
    while (m_transferredDrv && m_msg) {
	// Attended transfer: check if the requested channel is owned by our plugin
	// NOTE: Remove the whole 'if' when a routing module will be able to route
	//  attended transfer requests
	if (attended) {
	    String* from = m_msg->getParam(YSTRING("transfer_fromtag"));
	    String* to = m_msg->getParam(YSTRING("transfer_totag"));
	    if (null(from) || null(to)) {
		m_rspCode = m_notifyCode = 487;     // Request Terminated
		break;
	    }
	    YateSIPConnection* conn = plugin.findDialog(*attended,*from,*to,true);
	    if (conn) {
		m_transferredDrv->lock();
		RefPointer<Channel> chan = m_transferredDrv->find(m_transferredID);
		m_transferredDrv->unlock();
		if (chan && conn->getPeer() && 
		    chan->connect(conn->getPeer(),m_msg->getValue(YSTRING("reason")))) {
		    m_rspCode = 202;
		    m_notifyCode = 200;
		}
		else
		    m_rspCode = m_notifyCode = 487;     // Request Terminated
		TelEngine::destruct(conn);
		break;
	    }
	    // Not ours
	    m_msg->clearParam("called");
	    YateSIPConnection::addCallId(*m_msg,*attended,*from,*to);
	}

	// Route the call
	bool ok = Engine::dispatch(m_msg);
	m_transferredDrv->lock();
	RefPointer<Channel> chan = m_transferredDrv->find(m_transferredID);
	m_transferredDrv->unlock();
	if (!(ok && chan)) {
#ifdef DEBUG
	    if (ok)
		Debug(&plugin,DebugAll,"%s(%s). Connection vanished while routing! [%p]",
		    name(),m_transferorID.c_str(),this);
	    else
		Debug(&plugin,DebugAll,"%s(%s). 'call.route' failed [%p]",
		    name(),m_transferorID.c_str(),this);
#endif
	    m_rspCode = m_notifyCode = (ok ? 487 : 481);
	    break;
	}
	m_msg->userData(chan);
	if ((m_msg->retValue() == "-") || (m_msg->retValue() == YSTRING("error")))
	    m_rspCode = m_notifyCode = 603; // Decline
	else if (m_msg->getIntValue(YSTRING("antiloop"),1) <= 0)
	    m_rspCode = m_notifyCode = 482; // Loop Detected
	else {
	    DDebug(&plugin,DebugAll,"%s(%s). Call succesfully routed [%p]",
		name(),m_transferorID.c_str(),this);
	    *m_msg = "call.execute";
	    m_msg->setParam("callto",m_msg->retValue());
	    m_msg->clearParam(YSTRING("error"));
	    m_msg->retValue().clear();
	    if (Engine::dispatch(m_msg)) {
		DDebug(&plugin,DebugAll,"%s(%s). 'call.execute' succeeded [%p]",
		    name(),m_transferorID.c_str(),this);
		m_rspCode = 202;
		m_notifyCode = 200;
	    }
	    else {
		DDebug(&plugin,DebugAll,"%s(%s). 'call.execute' failed [%p]",
		    name(),m_transferorID.c_str(),this);
		m_rspCode = m_notifyCode = 603; // Decline
	    }
	}
	break;
    }
    release();
}

// Respond the transaction and deref() it
void YateSIPRefer::setTrResponse(int code)
{
    if (!m_transaction)
	return;
    SIPTransaction* t = m_transaction;
    m_transaction = 0;
    m_rspCode = code;
    t->setResponse(m_rspCode);
    TelEngine::destruct(t);
}

// Set transaction response. Send the notification message. Notify the
// connection and release other objects
void YateSIPRefer::release(bool fromCleanup)
{
    setTrResponse(m_rspCode);
    TelEngine::destruct(m_msg);
    // Set NOTIFY response and send it (only if the transaction was accepted)
    if (m_sipNotify) {
	if (m_rspCode < 300 && plugin.ep() && plugin.ep()->engine()) {
	    String s;
	    s << "SIP/2.0 " << m_notifyCode << " " << lookup(m_notifyCode,SIPResponses) << "\r\n";
	    m_sipNotify->setBody(new MimeStringBody("message/sipfrag;version=2.0",s));
	    plugin.ep()->engine()->addMessage(m_sipNotify);
	    m_sipNotify = 0;
	}
	else
	    TelEngine::destruct(m_sipNotify);
	// If we still have a NOTIFY message in cleanup() the thread
	//  was cancelled in the hard way
	if (fromCleanup)
	    Debug(&plugin,DebugWarn,"YateSIPRefer(%s) thread terminated abnormally [%p]",
		m_transferorID.c_str(),this);
    }
    // Notify transferor on termination
    if (m_transferorID) {
	plugin.lock();
	YateSIPConnection* conn = static_cast<YateSIPConnection*>(plugin.find(m_transferorID));
	if (conn)
	    conn->referTerminated();
	plugin.unlock();
	m_transferorID = "";
    }
}


// Incoming call constructor - just before starting the routing thread
YateSIPConnection::YateSIPConnection(SIPEvent* ev, SIPTransaction* tr)
    : Channel(plugin,0,false),
      SDPSession(&plugin.parser()),
      YateSIPPartyHolder(driver()),
      m_tr(tr), m_tr2(0), m_hungup(false), m_byebye(true), m_cancel(false),
      m_state(Incoming), m_port(0), m_route(0), m_routes(0),
      m_authBye(true), m_inband(s_inband), m_info(s_info),
      m_referring(false), m_reInviting(ReinviteNone), m_lastRseq(0)
{
    Debug(this,DebugAll,"YateSIPConnection::YateSIPConnection(%p,%p) [%p]",ev,tr,this);
    setReason();
    m_tr->ref();
    m_routes = m_tr->initialMessage()->getRoutes();
    m_callid = m_tr->getCallID();
    m_dialog = *m_tr->initialMessage();
    m_tr->initialMessage()->getParty()->getAddr(m_host,m_port,false);
    m_address << m_host << ":" << m_port;
    filterDebug(m_address);
    m_uri = m_tr->initialMessage()->getHeader("From");
    m_uri.parse();
    m_tr->setUserData(this);
    // Set channel SIP party
    setParty(m_tr->initialMessage()->getParty());

    URI uri(m_tr->getURI());
    YateSIPLine* line = plugin.findLine(m_host,m_port,uri.getUser());
    Message *m = message("call.preroute");
    decodeIsupBody(*m,m_tr->initialMessage()->body);
    m->addParam("caller",m_uri.getUser());
    m->addParam("called",uri.getUser());
    if (m_uri.getDescription())
	m->addParam("callername",m_uri.getDescription());
    const MimeHeaderLine* hl = m_tr->initialMessage()->getHeader("Call-Info");
    if (hl) {
	const NamedString* type = hl->getParam("purpose");
	if (!type || *type == YSTRING("info"))
	    m->addParam("caller_info_uri",*hl);
	else if (*type == YSTRING("icon"))
	    m->addParam("caller_icon_uri",*hl);
	else if (*type == YSTRING("card"))
	    m->addParam("caller_card_uri",*hl);
    }

    if (line) {
	// call comes from line we have registered to - trust it...
	m_user = line->getUserName();
	m_externalAddr = line->getLocalAddr();
	m_line = *line;
	m_domain = line->domain();
	m->addParam("username",m_user);
	m->addParam("domain",m_domain);
	m->addParam("in_line",m_line);
    }
    else {
	String user;
	int age = tr->authUser(user,false,m);
	DDebug(this,DebugAll,"User '%s' age %d",user.c_str(),age);
	if (age >= 0) {
	    if (age < 10) {
		m_user = user;
		m->addParam("username",m_user);
	    }
	    else
		m->addParam("expired_user",user);
	    m->addParam("xsip_nonce_age",String(age));
	}
	m_domain = m->getValue(YSTRING("domain"));
    }
    if (s_privacy)
	copyPrivacy(*m,*ev->getMessage());

    String tmp(ev->getMessage()->getHeaderValue("Max-Forwards"));
    int maxf = tmp.toInteger(s_maxForwards);
    if (maxf > s_maxForwards)
	maxf = s_maxForwards;
    tmp = maxf-1;
    m->addParam("antiloop",tmp);
    m->addParam("ip_host",m_host);
    m->addParam("ip_port",String(m_port));
    m->addParam("ip_transport",m_tr->initialMessage()->getParty()->getProtoName());
    m->addParam("sip_uri",uri);
    m->addParam("sip_from",m_uri);
    m->addParam("sip_to",ev->getMessage()->getHeaderValue("To"));
    m->addParam("sip_callid",m_callid);
    m->addParam("device",ev->getMessage()->getHeaderValue("User-Agent"));
    copySipHeaders(*m,*ev->getMessage());
    const char* reason = 0;
    hl = m_tr->initialMessage()->getHeader("Referred-By");
    if (hl)
	reason = "transfer";
    else {
	hl = m_tr->initialMessage()->getHeader("Diversion");
	if (hl) {
	    reason = "divert";
	    const String* par = hl->getParam("reason");
	    if (par) {
		tmp = par->c_str();
		MimeHeaderLine::delQuotes(tmp);
		if (tmp.trimBlanks())
		    m->addParam("divert_reason",tmp);
	    }
	    par = hl->getParam("privacy");
	    if (par) {
		tmp = par->c_str();
		MimeHeaderLine::delQuotes(tmp);
		if (tmp.trimBlanks())
		    m->addParam("divert_privacy",tmp);
	    }
	    par = hl->getParam("screen");
	    if (par) {
		tmp = par->c_str();
		MimeHeaderLine::delQuotes(tmp);
		if (tmp.trimBlanks())
		    m->addParam("divert_screen",tmp);
	    }
	}
    }
    if (hl) {
	URI div(*hl);
	m->addParam("diverter",div.getUser());
	if (div.getDescription())
	    m->addParam("divertername",div.getDescription());
	m->addParam("diverteruri",div);
    }
    setRtpLocalAddr(m_rtpLocalAddr);
    MimeSdpBody* sdp = getSdpBody(ev->getMessage()->body);
    if (sdp) {
	setMedia(plugin.parser().parse(sdp,m_rtpAddr,m_rtpMedia));
	if (m_rtpMedia) {
	    m_rtpForward = true;
	    // guess if the call comes from behind a NAT
	    bool nat = isNatBetween(m_rtpAddr,m_host);
	    if (m->getBoolValue(YSTRING("nat_support"),s_auto_nat && nat)) {
		Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		    m_rtpAddr.c_str(),m_host.c_str());
		m->addParam("rtp_nat_addr",m_rtpAddr);
		m_rtpAddr = m_host;
	    }
	    m->addParam("rtp_addr",m_rtpAddr);
	    putMedia(*m);
	}
	if (plugin.parser().sdpForward()) {
	    const DataBlock& raw = sdp->getBody();
	    String tmp((const char*)raw.data(),raw.length());
	    m->addParam("sdp_raw",tmp);
	    m_rtpForward = true;
	}
	if (m_rtpForward)
	    m->addParam("rtp_forward","possible");
    }
    DDebug(this,DebugAll,"RTP addr '%s' [%p]",m_rtpAddr.c_str(),this);
    if (reason)
	m->addParam("reason",reason);
    m_route = m;
    Message* s = message("chan.startup");
    s->addParam("caller",m_uri.getUser());
    s->addParam("called",uri.getUser());
    if (m_user)
	s->addParam("username",m_user);
    Engine::enqueue(s);
}

// Outgoing call constructor - in call.execute handler
YateSIPConnection::YateSIPConnection(Message& msg, const String& uri, const char* target)
    : Channel(plugin,0,true),
      SDPSession(&plugin.parser()),
      YateSIPPartyHolder(driver()),
      m_tr(0), m_tr2(0), m_hungup(false), m_byebye(true), m_cancel(true),
      m_state(Outgoing), m_port(0), m_route(0), m_routes(0),
      m_authBye(false), m_inband(s_inband), m_info(s_info),
      m_referring(false), m_reInviting(ReinviteNone), m_lastRseq(0)
{
    Debug(this,DebugAll,"YateSIPConnection::YateSIPConnection(%p,'%s') [%p]",
	&msg,uri.c_str(),this);
    m_targetid = target;
    setReason();
    m_inband = msg.getBoolValue(YSTRING("dtmfinband"),s_inband);
    m_info = msg.getBoolValue(YSTRING("dtmfinfo"),s_info);
    m_secure = msg.getBoolValue(YSTRING("secure"),plugin.parser().secure());
    setRfc2833(msg.getParam(YSTRING("rfc2833")));
    m_rtpForward = msg.getBoolValue(YSTRING("rtp_forward"));
    m_user = msg.getValue(YSTRING("user"));
    m_line = msg.getValue(YSTRING("line"));
    String tmp;
    YateSIPLine* line = 0;
    if (m_line) {
	line = plugin.findLine(m_line);
	if (line) {
	    if (uri.find('@') < 0 && !uri.startsWith("tel:")) {
		if (!uri.startsWith("sip:"))
		    tmp = "sip:";
		tmp << uri << "@" << line->domain();
	    }
	    m_externalAddr = line->getLocalAddr();
	}
    }
    if (tmp.null()) {
	if (!(uri.startsWith("tel:") || uri.startsWith("sip:"))) {
	    int sep = uri.find(':');
	    if ((sep < 0) || ((sep > 0) && (uri.substr(sep+1).toInteger(-1) > 0)))
		tmp = "sip:";
	}
	tmp << uri;
    }
    m_uri = tmp;
    m_uri.parse();
    if (!setParty(msg,false,"o",m_uri.getHost(),m_uri.getPort()) && line) {
	SIPParty* party = line->party();
	setParty(party);
	TelEngine::destruct(party);
    }
    SIPMessage* m = new SIPMessage("INVITE",m_uri);
    setSipParty(m,line,true,msg.getValue("host"),msg.getIntValue("port"));
    if (!m->getParty()) {
	Debug(this,DebugWarn,"Could not create party for '%s' [%p]",m_uri.c_str(),this);
	TelEngine::destruct(m);
	tmp = "Invalid address: ";
	tmp << m_uri;
	msg.setParam("reason",tmp);
	setReason(tmp,500);
	return;
    }
    int maxf = msg.getIntValue(YSTRING("antiloop"),s_maxForwards);
    m->addHeader("Max-Forwards",String(maxf));
    copySipHeaders(*m,msg);
    m_domain = msg.getValue(YSTRING("domain"));
    const String* callerId = msg.getParam(YSTRING("caller"));
    String caller;
    if (callerId)
	caller = *callerId;
    else if (line) {
	caller = line->getUserName();
	callerId = &caller;
	m_domain = line->domain(m_domain);
    }
    String display = msg.getValue(YSTRING("callername"),(line ? line->getFullName().c_str() : (const char*)0));
    m->complete(plugin.ep()->engine(),
	callerId ? (callerId->null() ? "anonymous" : callerId->c_str()) : (const char*)0,
	m_domain,
	0,
	msg.getIntValue(YSTRING("xsip_flags"),-1));
    if (display) {
	MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(m->getHeader("From"));
	if (hl) {
	    MimeHeaderLine::addQuotes(display);
	    *hl = display + " " + *hl;
	}
    }
    if (msg.getParam(YSTRING("calledname"))) {
	display = msg.getValue(YSTRING("calledname"));
	MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(m->getHeader("To"));
	if (hl) {
	    MimeHeaderLine::addQuotes(display);
	    *hl = display + " " + *hl;
	}
    }
    if (plugin.ep()->engine()->prack())
	m->addHeader("Supported","100rel");
    m->getParty()->getAddr(m_host,m_port,false);
    m_address << m_host << ":" << m_port;
    filterDebug(m_address);
    m_dialog = *m;
    if (s_privacy)
	copyPrivacy(*m,msg);

    // Check if this is a transferred call
    String* diverter = msg.getParam(YSTRING("diverter"));
    if (!null(diverter)) {
	const MimeHeaderLine* from = m->getHeader("From");
	if (from) {
	    URI fr(*from);
	    URI d(fr.getProtocol(),*diverter,fr.getHost(),fr.getPort(),
		msg.getValue(YSTRING("divertername"),""));
	    String* reason = msg.getParam(YSTRING("divert_reason"));
	    String* privacy = msg.getParam(YSTRING("divert_privacy"));
	    String* screen = msg.getParam(YSTRING("divert_screen"));
	    bool divert = !(TelEngine::null(reason) && TelEngine::null(privacy) && TelEngine::null(screen));
	    divert = msg.getBoolValue(YSTRING("diversion"),divert);
	    MimeHeaderLine* hl = new MimeHeaderLine(divert ? "Diversion" : "Referred-By",d);
	    if (divert) {
		if (!TelEngine::null(reason))
		    hl->setParam("reason",MimeHeaderLine::quote(*reason));
		if (!TelEngine::null(privacy))
		    hl->setParam("privacy",MimeHeaderLine::quote(*privacy));
		if (!TelEngine::null(screen))
		    hl->setParam("screen",MimeHeaderLine::quote(*screen));
	    }
	    m->addHeader(hl);
	}
    }

    // add some Call-Info headers
    const char* info = msg.getValue(YSTRING("caller_info_uri"));
    if (info) {
	MimeHeaderLine* hl = new MimeHeaderLine("Call-Info",info);
	hl->setParam("purpose","info");
	m->addHeader(hl);
    }
    info = msg.getValue(YSTRING("caller_icon_uri"));
    if (info) {
	MimeHeaderLine* hl = new MimeHeaderLine("Call-Info",info);
	hl->setParam("purpose","icon");
	m->addHeader(hl);
    }
    info = msg.getValue(YSTRING("caller_card_uri"));
    if (info) {
	MimeHeaderLine* hl = new MimeHeaderLine("Call-Info",info);
	hl->setParam("purpose","card");
	m->addHeader(hl);
    }

    setRtpLocalAddr(m_rtpLocalAddr,&msg);
    MimeSdpBody* sdp = createPasstroughSDP(msg);
    if (!sdp)
	sdp = createRtpSDP(m_host,msg);
    m->setBody(buildSIPBody(msg,sdp));
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_callid = m_tr->getCallID();
	m_tr->setUserData(this);
    }
    m->deref();
    setMaxcall(msg);
    Message* s = message("chan.startup",msg);
    s->setParam("caller",caller);
    s->copyParams(msg,"callername,called,billid,callto,username");
    s->setParam("calledfull",m_uri.getUser());
    if (m_callid)
	s->setParam("sip_callid",m_callid);
    Engine::enqueue(s);
}

YateSIPConnection::~YateSIPConnection()
{
    Debug(this,DebugAll,"YateSIPConnection::~YateSIPConnection() [%p]",this);
}

void YateSIPConnection::destroyed()
{
    DDebug(this,DebugAll,"YateSIPConnection::destroyed() [%p]",this);
    hangup();
    clearTransaction();
    TelEngine::destruct(m_route);
    TelEngine::destruct(m_routes);
    Channel::destroyed();
}

void YateSIPConnection::startRouter()
{
    Message* m = m_route;
    m_route = 0;
    Channel::startRouter(m);
}

void YateSIPConnection::clearTransaction()
{
    if (!(m_tr || m_tr2))
	return;
    Lock lock(driver());
    if (m_tr) {
	m_tr->setUserData(0);
	if (m_tr->setResponse()) {
	    SIPMessage* m = new SIPMessage(m_tr->initialMessage(),m_reasonCode,
		m_reason.safe("Request Terminated"));
	    paramMutex().lock();
	    copySipHeaders(*m,parameters(),0);
	    paramMutex().unlock();
	    m->setBody(buildSIPBody());
	    m_tr->setResponse(m);
	    TelEngine::destruct(m);
	    m_byebye = false;
	}
	else if (m_hungup && m_tr->isIncoming() && m_dialog.localTag.null())
	    m_dialog.localTag = m_tr->getDialogTag();
	m_tr->deref();
	m_tr = 0;
    }
    // cancel any pending reINVITE
    if (m_tr2) {
	m_tr2->setUserData(0);
	if (m_tr2->isIncoming())
	    m_tr2->setResponse(487);
	m_tr2->deref();
	m_tr2 = 0;
    }
}

void YateSIPConnection::detachTransaction2()
{
    Lock lock(driver());
    if (m_tr2) {
	m_tr2->setUserData(0);
	m_tr2->deref();
	m_tr2 = 0;
	if (m_reInviting != ReinvitePending)
	    m_reInviting = ReinviteNone;
    }
    startPendingUpdate();
}

void YateSIPConnection::hangup()
{
    if (m_hungup)
	return;
    m_hungup = true;
    const char* error = lookup(m_reasonCode,dict_errors);
    Debug(this,DebugAll,"YateSIPConnection::hangup() state=%d trans=%p error='%s' code=%d reason='%s' [%p]",
	m_state,m_tr,error,m_reasonCode,m_reason.c_str(),this);
    setMedia(0);
    Message* m = message("chan.hangup");
    if (m_reason)
	m->setParam("reason",m_reason);
    Engine::enqueue(m);
    switch (m_state) {
	case Cleared:
	    clearTransaction();
	    return;
	case Incoming:
	    if (m_tr) {
		clearTransaction();
		return;
	    }
	    break;
	case Outgoing:
	case Ringing:
	    if (m_cancel && m_tr) {
		SIPMessage* m = new SIPMessage("CANCEL",m_uri);
		setSipParty(m,plugin.findLine(m_line),true,m_host,m_port);
		if (!m->getParty())
		    Debug(this,DebugWarn,"Could not create party for '%s:%d' [%p]",
			m_host.c_str(),m_port,this);
		else {
		    const SIPMessage* i = m_tr->initialMessage();
		    m->copyHeader(i,"Via");
		    m->copyHeader(i,"From");
		    m->copyHeader(i,"To");
		    m->copyHeader(i,"Call-ID");
		    String tmp;
		    tmp << i->getCSeq() << " CANCEL";
		    m->addHeader("CSeq",tmp);
		    if (m_reason == YSTRING("pickup")) {
			MimeHeaderLine* hl = new MimeHeaderLine("Reason","SIP");
			hl->setParam("cause","200");
			hl->setParam("text","\"Call completed elsewhere\"");
			m->addHeader(hl);
		    }
		    m->setBody(buildSIPBody());
		    plugin.ep()->engine()->addMessage(m);
		}
		m->deref();
	    }
	    break;
    }
    clearTransaction();
    m_state = Cleared;

    if (m_byebye && m_dialog.localTag && m_dialog.remoteTag) {
	SIPMessage* m = createDlgMsg("BYE");
	if (m) {
	    if (m_reason) {
		// FIXME: add SIP and Q.850 cause codes, set the proper reason
		MimeHeaderLine* hl = new MimeHeaderLine("Reason","SIP");
		if ((m_reasonCode >= 300) && (m_reasonCode <= 699) && (m_reasonCode != 487))
		    hl->setParam("cause",String(m_reasonCode));
		hl->setParam("text",MimeHeaderLine::quote(m_reason));
		m->addHeader(hl);
	    }
	    paramMutex().lock();
	    const char* stats = parameters().getValue(YSTRING("rtp_stats"));
	    if (stats)
		m->addHeader("P-RTP-Stat",stats);
	    copySipHeaders(*m,parameters(),0);
	    paramMutex().unlock();
	    m->setBody(buildSIPBody());
	    plugin.ep()->engine()->addMessage(m);
	    m->deref();
	}
    }
    m_byebye = false;
    if (!error)
	error = m_reason.c_str();
    disconnect(error,parameters());
}

// Creates a new message in an existing dialog
SIPMessage* YateSIPConnection::createDlgMsg(const char* method, const char* uri)
{
    if (!uri)
	uri = m_uri;
    SIPMessage* m = new SIPMessage(method,uri);
    m->addRoutes(m_routes);
    setSipParty(m,plugin.findLine(m_line),true,m_host,m_port);
    if (!m->getParty()) {
	Debug(this,DebugWarn,"Could not create party for '%s:%d' [%p]",
	    m_host.c_str(),m_port,this);
	m->destruct();
	return 0;
    }
    m->addHeader("Call-ID",m_callid);
    String tmp;
    tmp << "<" << m_dialog.localURI << ">";
    MimeHeaderLine* hl = new MimeHeaderLine("From",tmp);
    tmp = m_dialog.localTag;
    if (tmp.null() && m_tr)
	tmp = m_tr->getDialogTag();
    if (tmp)
	hl->setParam("tag",tmp);
    m->addHeader(hl);
    tmp.clear();
    tmp << "<" << m_dialog.remoteURI << ">";
    hl = new MimeHeaderLine("To",tmp);
    tmp = m_dialog.remoteTag;
    if (tmp.null() && m_tr)
	tmp = m_tr->getDialogTag();
    if (tmp)
	hl->setParam("tag",tmp);
    m->addHeader(hl);
    return m;
}

// Emit a call.update to notify cdrbuild of callid dialog tags change
void YateSIPConnection::emitUpdate()
{
    Message* m = message("call.update");
    m->addParam("operation","cdrbuild");
    Engine::enqueue(m);
}

// Emit a PRovisional ACK if enabled in the engine, return true to handle them
bool YateSIPConnection::emitPRACK(const SIPMessage* msg)
{
    if (!(msg && msg->isAnswer() && (msg->code > 100) && (msg->code < 200)))
	return false;
    if (!plugin.ep()->engine()->prack())
	return true;
    const MimeHeaderLine* rs = msg->getHeader("RSeq");
    const MimeHeaderLine* cs = msg->getHeader("CSeq");
    if (!(rs && cs))
	return true;
    int seq = rs->toInteger(0,10);
    // return false only if we already seen this provisional response
    if (seq == m_lastRseq)
	return false;
    if (seq < m_lastRseq) {
	Debug(this,DebugMild,"Not sending PRACK for RSeq %d < %d [%p]",
	    seq,m_lastRseq,this);
	return false;
    }
    String tmp;
    const MimeHeaderLine* co = msg->getHeader("Contact");
    if (co) {
	tmp = *co;
	static const Regexp r("^[^<]*<\\([^>]*\\)>.*$");
	if (tmp.matches(r))
	    tmp = tmp.matchString(1);
    }
    SIPMessage* m = createDlgMsg("PRACK",tmp);
    if (!m)
	return true;
    m_lastRseq = seq;
    tmp = *rs;
    tmp << " " << *cs;
    m->addHeader("RAck",tmp);
    plugin.ep()->engine()->addMessage(m);
    m->deref();
    return true;
}

// Creates a SDP for provisional (1xx) messages
MimeSdpBody* YateSIPConnection::createProvisionalSDP(Message& msg)
{
    if (!msg.getBoolValue(YSTRING("earlymedia"),true))
	return 0;
    if (m_rtpForward)
	return createPasstroughSDP(msg);
    // check if our peer can source at least audio data
    if (!(getPeer() && getPeer()->getSource()))
	return 0;
    if (m_rtpAddr.null())
	return 0;
    if (s_1xx_formats)
	updateFormats(msg);
    return createRtpSDP(true);
}

// Build and populate a chan.rtp message
Message* YateSIPConnection::buildChanRtp(RefObject* context)
{
    Message* m = new Message("chan.rtp");
    if (context)
	m->userData(context);
    else {
	complete(*m,true);
	m->addParam("call_direction",direction());
	m->addParam("call_address",address());
	m->addParam("call_status",status());
	m->addParam("call_billid",billid());
	m->userData(static_cast<CallEndpoint*>(this));
    }
    return m;
}

// Media changed notification, reimplemented from SDPSession
void YateSIPConnection::mediaChanged(const SDPMedia& media)
{
    SDPSession::mediaChanged(media);
    if (media.id() && media.transport()) {
	Message m("chan.rtp");
	m.addParam("rtpid",media.id());
	m.addParam("media",media);
	m.addParam("transport",media.transport());
	m.addParam("terminate",String::boolText(true));
	m.addParam("call_direction",direction());
	m.addParam("call_address",address());
	m.addParam("call_status",status());
	m.addParam("call_billid",billid());
	Engine::dispatch(m);
	const char* stats = m.getValue(YSTRING("stats"));
	if (stats) {
	    paramMutex().lock();
	    parameters().setParam("rtp_stats"+media.suffix(),stats);
	    paramMutex().unlock();
	}
    }
    // Clear the data endpoint, will be rebuilt later if required
    clearEndpoint(media);
}

// Process SIP events belonging to this connection
bool YateSIPConnection::process(SIPEvent* ev)
{
    const SIPMessage* msg = ev->getMessage();
    int code = ev->getTransaction()->getResponseCode();
    DDebug(this,DebugInfo,"YateSIPConnection::process(%p) %s %s code=%d [%p]",
	ev,ev->isActive() ? "active" : "inactive",
	SIPTransaction::stateName(ev->getState()),code,this);
#ifdef XDEBUG
    if (msg)
	Debug(this,DebugInfo,"Message %p '%s' %s %s code=%d body=%p",
	    msg,msg->method.c_str(),
	    msg->isOutgoing() ? "outgoing" : "incoming",
	    msg->isAnswer() ? "answer" : "request",
	    msg->code,msg->body);
#endif

    Lock mylock(driver());
    if (ev->getTransaction() == m_tr2) {
	mylock.drop();
	return processTransaction2(ev,msg,code);
    }

    bool updateTags = true;
    SIPDialog oldDlg(m_dialog);
    m_dialog = *ev->getTransaction()->recentMessage();
    mylock.drop();

    if (msg && !msg->isOutgoing() && msg->isAnswer() && (code >= 300) && (code <= 699)) {
	updateTags = false;
	m_cancel = false;
	m_byebye = false;
	paramMutex().lock();
	parameters().clearParams();
	parameters().addParam("cause_sip",String(code));
	parameters().addParam("reason_sip",msg->reason);
	setReason(msg->reason,code);
	if (msg->body) {
	    paramMutex().unlock();
	    Message tmp("isup.decode");
	    bool ok = decodeIsupBody(tmp,msg->body);
	    paramMutex().lock();
	    if (ok)
		parameters().copyParams(tmp);
	}
	copySipHeaders(parameters(),*msg);
	if (code < 400) {
	    // this is a redirect, it should provide a Contact and possibly a Diversion
	    const MimeHeaderLine* hl = msg->getHeader("Contact");
	    if (hl) {
		parameters().addParam("redirect",String::boolText(true));
		URI uri(*hl);
		parameters().addParam("called",uri.getUser());
		if (uri.getDescription())
		    parameters().addParam("calledname",uri.getDescription());
		parameters().addParam("calleduri",uri);
		hl = msg->getHeader("Diversion");
		if (hl) {
		    uri = *hl;
		    parameters().addParam("diverter",uri.getUser());
		    if (uri.getDescription())
			parameters().addParam("divertername",uri.getDescription());
		    parameters().addParam("diverteruri",uri);
		    String tmp = hl->getParam("reason");
		    MimeHeaderLine::delQuotes(tmp);
		    if (tmp.trimBlanks())
			parameters().addParam("divert_reason",tmp);
		    tmp = hl->getParam("privacy");
		    MimeHeaderLine::delQuotes(tmp);
		    if (tmp.trimBlanks())
			parameters().addParam("divert_privacy",tmp);
		    tmp = hl->getParam("screen");
		    MimeHeaderLine::delQuotes(tmp);
		    if (tmp.trimBlanks())
			parameters().addParam("divert_screen",tmp);
		}
	    }
	    else
		Debug(this,DebugMild,"Received %d redirect without Contact [%p]",code,this);
	}
	paramMutex().unlock();
	hangup();
    }
    else if (code == 408) {
	// Proxy timeout does not provide an answer message
	updateTags = false;
	if (m_dialog.remoteTag.null())
	    m_byebye = false;
	paramMutex().lock();
	parameters().setParam("cause_sip","408");
	paramMutex().unlock();
	setReason("Request Timeout",code);
	hangup();
    }

    // Only update channels' callid if dialog tags change
    if (updateTags) {
	Lock lock(driver());
	updateTags = (oldDlg |= m_dialog);
    }

    if (!ev->isActive()) {
	Lock lock(driver());
	if (m_tr) {
	    DDebug(this,DebugInfo,"YateSIPConnection clearing transaction %p [%p]",
		m_tr,this);
	    m_tr->setUserData(0);
	    m_tr->deref();
	    m_tr = 0;
	}
	if (m_state != Established)
	    hangup();
	else if (s_ack_required && (code == 408)) {
	    // call was established but we didn't got the ACK
	    setReason("Not received ACK",code);
	    hangup();
	}
	else {
	    if (updateTags)
		emitUpdate();
	    startPendingUpdate();
	}
	return false;
    }
    if (!msg || msg->isOutgoing()) {
	if (updateTags)
	    emitUpdate();
	return false;
    }
    String natAddr;
    MimeSdpBody* sdp = getSdpBody(msg->body);
    if (sdp) {
	DDebug(this,DebugInfo,"YateSIPConnection got SDP [%p]",this);
	setMedia(plugin.parser().parse(sdp,m_rtpAddr,m_rtpMedia));
	// guess if the call comes from behind a NAT
	if (s_auto_nat && isNatBetween(m_rtpAddr,m_host)) {
	    Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		m_rtpAddr.c_str(),m_host.c_str());
	    natAddr = m_rtpAddr;
	    m_rtpAddr = m_host;
	}
	DDebug(this,DebugAll,"RTP addr '%s' [%p]",m_rtpAddr.c_str(),this);
    }
    if ((!m_routes) && msg->isAnswer() && (msg->code > 100) && (msg->code < 300))
	m_routes = msg->getRoutes();

    if (msg->isAnswer() && m_externalAddr.null() && m_line) {
	// see if we should detect our external address
	const YateSIPLine* line = plugin.findLine(m_line);
	if (line && line->localDetect()) {
	    const MimeHeaderLine* hl = msg->getHeader("Via");
	    if (hl) {
		const NamedString* par = hl->getParam("received");
		if (par && *par) {
		    m_externalAddr = *par;
		    Debug(this,DebugInfo,"Detected local address '%s' [%p]",
			m_externalAddr.c_str(),this);
		}
	    }
	}
    }

    if (msg->isAnswer() && ((msg->code / 100) == 2)) {
	updateTags = false;
	m_cancel = false;
	Lock lock(driver());
	const SIPMessage* ack = m_tr ? m_tr->latestMessage() : 0;
	if (ack && ack->isACK()) {
	    // accept any URI change caused by a Contact: header in the 2xx
	    m_uri = ack->uri;
	    m_uri.parse();
	    DDebug(this,DebugInfo,"YateSIPConnection clearing answered transaction %p [%p]",
		m_tr,this);
	    m_tr->setUserData(0);
	    m_tr->deref();
	    m_tr = 0;
	}
	lock.drop();
	setReason("",0);
	setStatus("answered",Established);
	Message *m = message("call.answered");
	copySipHeaders(*m,*msg);
	decodeIsupBody(*m,msg->body);
	addRtpParams(*m,natAddr,msg->body);
	Engine::enqueue(m);
	startPendingUpdate();
    }
    if (emitPRACK(msg)) {
	if (s_multi_ringing || (m_state < Ringing)) {
	    const char* name = "call.progress";
	    const char* reason = 0;
	    switch (msg->code) {
		case 180:
		    updateTags = false;
		    name = "call.ringing";
		    setStatus("ringing",Ringing);
		    break;
		case 181:
		    reason = "forwarded";
		    setStatus("progressing");
		    break;
		case 182:
		    reason = "queued";
		    setStatus("progressing");
		    break;
		case 183:
		    setStatus("progressing");
		    break;
		// for all others emit a call.progress but don't change status
	    }
	    if (name) {
		Message* m = message(name);
		copySipHeaders(*m,*msg);
		decodeIsupBody(*m,msg->body);
		if (reason)
		    m->addParam("reason",reason);
		addRtpParams(*m,natAddr,msg->body);
		if (m_rtpAddr.null())
		    m->addParam("earlymedia","false");
		Engine::enqueue(m);
	    }
	}
    }
    if (updateTags)
	emitUpdate();
    if (msg->isACK()) {
	DDebug(this,DebugInfo,"YateSIPConnection got ACK [%p]",this);
	startRtp();
    }
    return false;
}

// Process secondary transaction (reINVITE)  belonging to this connection
bool YateSIPConnection::processTransaction2(SIPEvent* ev, const SIPMessage* msg, int code)
{
    if (ev->getState() == SIPTransaction::Cleared) {
	bool fatal = (m_reInviting == ReinviteRequest);
	detachTransaction2();
	if (fatal) {
	    setReason("Request Timeout",408);
	    hangup();
	}
	else {
	    Message* m = message("call.update");
	    m->addParam("operation","reject");
	    m->addParam("error","timeout");
	    Engine::enqueue(m);
	}
	return false;
    }
    if (!msg || msg->isOutgoing() || !msg->isAnswer())
	return false;
    if (code < 200)
	return false;

    if (m_reInviting == ReinviteRequest) {
	detachTransaction2();
	// we emitted a client reINVITE, now we are forced to deal with it
	if (code < 300) {
	    MimeSdpBody* sdp = getSdpBody(msg->body);
	    while (sdp) {
		String addr;
		ObjList* lst = plugin.parser().parse(sdp,addr,0,String::empty(),m_rtpForward);
		if (!lst)
		    break;
		if ((addr == m_rtpAddr) || isNatBetween(addr,m_host)) {
		    ObjList* l = m_rtpMedia;
		    for (; l; l = l->next()) {
			SDPMedia* m = static_cast<SDPMedia*>(l->get());
			if (!m)
			    continue;
			SDPMedia* m2 = static_cast<SDPMedia*>((*lst)[*m]);
			if (!m2)
			    continue;
			// both old and new media exist, compare ports
			if (m->remotePort() != m2->remotePort()) {
			    DDebug(this,DebugWarn,"Port for '%s' changed: '%s' -> '%s' [%p]",
				m->c_str(),m->remotePort().c_str(),
				m2->remotePort().c_str(),this);
			    TelEngine::destruct(lst);
			    break;
			}
		    }
		    if (lst) {
			setMedia(lst);
			return false;
		    }
		}
		TelEngine::destruct(lst);
		setReason("Media information changed during reINVITE",415);
		hangup();
		return false;
	    }
	    setReason("Missing media information",415);
	}
	else
	    setReason(msg->reason,code);
	hangup();
	return false;
    }

    Message* m = message("call.update");
    decodeIsupBody(*m,msg->body);
    if (code < 300) {
	m->addParam("operation","notify");
	String natAddr;
	MimeSdpBody* sdp = getSdpBody(msg->body);
	if (sdp) {
	    DDebug(this,DebugInfo,"YateSIPConnection got reINVITE SDP [%p]",this);
	    setMedia(plugin.parser().parse(sdp,m_rtpAddr,m_rtpMedia,String::empty(),m_rtpForward));
	    // guess if the call comes from behind a NAT
	    if (s_auto_nat && isNatBetween(m_rtpAddr,m_host)) {
		Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		    m_rtpAddr.c_str(),m_host.c_str());
		natAddr = m_rtpAddr;
		m_rtpAddr = m_host;
	    }
	    DDebug(this,DebugAll,"RTP addr '%s' [%p]",m_rtpAddr.c_str(),this);
	    if (m_rtpForward) {
		// drop any local RTP we might have before
		m_mediaStatus = m_rtpAddr.null() ? MediaMuted : MediaMissing;
		m_rtpLocalAddr.clear();
		clearEndpoint();
	    }
	}
	if (!addRtpParams(*m,natAddr,sdp))
	    addSdpParams(*m,sdp);
    }
    else {
	m->addParam("operation","reject");
	m->addParam("error",lookup(code,dict_errors,"failure"));
	m->addParam("reason",msg->reason);
    }
    detachTransaction2();
    Engine::enqueue(m);
    return false;
}

void YateSIPConnection::reInvite(SIPTransaction* t)
{
    if (!checkUser(t))
	return;
    DDebug(this,DebugAll,"YateSIPConnection::reInvite(%p) [%p]",t,this);
    Lock mylock(driver());
    int invite = m_reInviting;
    if (m_tr || m_tr2 || (invite == ReinviteRequest) || (invite == ReinviteReceived)) {
	// another request pending - refuse this one
	t->setResponse(491);
	return;
    }
    if (m_hungup) {
	t->setResponse(481);
	return;
    }
    m_reInviting = ReinviteReceived;
    mylock.drop();

    // hack: use a while instead of if so we can return or break out of it
    MimeSdpBody* sdp = getSdpBody(t->initialMessage()->body);
    while (sdp) {
	// for pass-trough RTP we need support from our peer
	if (m_rtpForward) {
	    String addr;
	    String natAddr;
	    ObjList* lst = plugin.parser().parse(sdp,addr,0,String::empty(),true);
	    if (!lst)
		break;
	    // guess if the call comes from behind a NAT
	    if (s_auto_nat && isNatBetween(addr,m_host)) {
		Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		    addr.c_str(),m_host.c_str());
		natAddr = addr;
		addr = m_host;
	    }
	    Debug(this,DebugAll,"reINVITE RTP addr '%s'",addr.c_str());

	    Message msg("call.update");
	    complete(msg);
	    msg.addParam("operation","request");
	    copySipHeaders(msg,*t->initialMessage());
	    msg.addParam("rtp_forward","yes");
	    msg.addParam("rtp_addr",addr);
	    if (natAddr)
		msg.addParam("rtp_nat_addr",natAddr);
	    putMedia(msg,lst);
	    addSdpParams(msg,sdp);
	    bool ok = Engine::dispatch(msg);
	    Lock mylock2(driver());
	    // if peer doesn't support updates fail the reINVITE
	    if (!ok) {
		t->setResponse(msg.getIntValue(YSTRING("error"),dict_errors,488),msg.getValue(YSTRING("reason")));
		m_reInviting = invite;
	    }
	    else if (m_tr2) {
		// ouch! this shouldn't have happened!
		t->setResponse(491);
		// media is uncertain now so drop the call
		setReason("Internal Server Error",500);
		mylock2.drop();
		hangup();
	    }
	    else {
		// we remember the request and leave it pending
		t->ref();
		t->setUserData(this);
		m_tr2 = t;
	    }
	    return;
	}
	// refuse request if we had no media at all before
	if (m_mediaStatus == MediaMissing)
	    break;
	String addr;
	ObjList* lst = plugin.parser().parse(sdp,addr);
	if (!lst)
	    break;
	// guess if the call comes from behind a NAT
	if (s_auto_nat && isNatBetween(addr,m_host)) {
	    Debug(this,DebugInfo,"RTP NAT detected: private '%s' public '%s'",
		addr.c_str(),m_host.c_str());
	    addr = m_host;
	}

	// TODO: check if we should accept the new media
	// many implementation don't handle well failure so we should drop

	if (m_rtpAddr != addr) {
	    m_rtpAddr = addr;
	    Debug(this,DebugAll,"New RTP addr '%s'",m_rtpAddr.c_str());
	    // clear all data endpoints - createRtpSDP will build new ones
	    if (!s_rtp_preserve)
		clearEndpoint();
	}
	bool audioChg = (getMedia(YSTRING("audio")) != 0);
	setMedia(lst);
	audioChg ^= (getMedia(YSTRING("audio")) != 0);

	m_mediaStatus = MediaMissing;
	// let RTP guess again the local interface or use the enforced address
	setRtpLocalAddr(m_rtpLocalAddr);

	SIPMessage* m = new SIPMessage(t->initialMessage(), 200);
	MimeSdpBody* sdpNew = createRtpSDP(true);
	m->setBody(sdpNew);
	t->setResponse(m);
	m->deref();
	Message* msg = message("call.update");
	msg->addParam("operation","notify");
	msg->addParam("mandatory","false");
	msg->addParam("audio_changed",String::boolText(audioChg));
	msg->addParam("mute",String::boolText(MediaStarted != m_mediaStatus));
	putMedia(*msg);
	Engine::enqueue(msg);
	m_reInviting = invite;
	return;
    }
    m_reInviting = invite;
    if (s_refresh_nosdp && !sdp) {
	// be permissive, accept session refresh with no SDP
	SIPMessage* m = new SIPMessage(t->initialMessage(),200);
	// if required provide our own media offer
	if (!m_rtpForward)
	    m->setBody(createSDP());
	t->setResponse(m);
	m->deref();
	return;
    }
    t->setResponse(488);
}

bool YateSIPConnection::checkUser(SIPTransaction* t, bool refuse)
{
    // don't try to authenticate requests from server
    if (m_user.null() || m_line)
	return true;
    NamedList params("");
    params.addParam("billid",billid(),false);
    int age = t->authUser(m_user,false,&params);
    if ((age >= 0) && (age <= 10))
	return true;
    DDebug(this,DebugAll,"YateSIPConnection::checkUser(%p) failed, age %d [%p]",t,age,this);
    if (refuse) {
	Lock lck(s_globalMutex);
	t->requestAuth(s_realm,m_domain,age >= 0);
    }
    return false;
}

void YateSIPConnection::doBye(SIPTransaction* t)
{
    if (m_authBye && !checkUser(t))
	return;
    DDebug(this,DebugAll,"YateSIPConnection::doBye(%p) [%p]",t,this);
    const SIPMessage* msg = t->initialMessage();
    if (msg->body) {
	Message tmp("isup.decode");
	if (decodeIsupBody(tmp,msg->body)) {
	    paramMutex().lock();
	    parameters().copyParams(tmp);
	    paramMutex().unlock();
	}
    }
    SIPMessage* m = new SIPMessage(t->initialMessage(),200);
    paramMutex().lock();
    copySipHeaders(parameters(),*msg);
    const char* stats = parameters().getValue(YSTRING("rtp_stats"));
    if (stats)
	m->addHeader("P-RTP-Stat",stats);
    paramMutex().unlock();
    const MimeHeaderLine* hl = msg->getHeader("Reason");
    if (hl) {
	const NamedString* text = hl->getParam("text");
	if (text)
	    m_reason = MimeHeaderLine::unquote(*text);
	// FIXME: add SIP and Q.850 cause codes
    }
    setMedia(0);
    t->setResponse(m);
    m->deref();
    m_byebye = false;
    hangup();
}

void YateSIPConnection::doCancel(SIPTransaction* t)
{
#ifdef DEBUG
    // CANCEL cannot be challenged but it may (should?) be authenticated with
    //  an old nonce from the transaction that is being cancelled
    if (m_user && (t->authUser(m_user) < 0))
	Debug(&plugin,DebugMild,"User authentication failed for user '%s' but CANCELing anyway [%p]",
	    m_user.c_str(),this);
#endif
    DDebug(this,DebugAll,"YateSIPConnection::doCancel(%p) [%p]",t,this);
    if (m_tr) {
	t->setResponse(200);
	m_byebye = false;
	clearTransaction();
	disconnect("Cancelled");
    }
    else
	t->setResponse(481);
}

bool YateSIPConnection::doInfo(SIPTransaction* t)
{
    if (m_authBye && !checkUser(t))
	return true;
    DDebug(this,DebugAll,"YateSIPConnection::doInfo(%p) [%p]",t,this);
    if (m_hungup) {
	t->setResponse(481);
	return true;
    }
    int sig = -1;
    const MimeLinesBody* lb = YOBJECT(MimeLinesBody,getOneBody(t->initialMessage()->body,"application/dtmf-relay"));
    const MimeStringBody* sb = YOBJECT(MimeStringBody,getOneBody(t->initialMessage()->body,"application/dtmf"));
    if (lb) {
	const ObjList* l = lb->lines().skipNull();
	for (; l; l = l->skipNext()) {
	    String tmp = static_cast<String*>(l->get());
	    tmp.toUpper();
	    if (tmp.startSkip("SIGNAL=",false)) {
		sig = tmp.trimBlanks().toInteger(info_signals,-1);
		break;
	    }
	}
    }
    else if (sb) {
	String tmp = sb->text();
	tmp.trimSpaces();
	sig = tmp.toInteger(info_signals,-1);
    }
    else
	return false;
    t->setResponse(200);
    if ((sig >= 0) && (sig <= 16)) {
	char tmp[2];
	tmp[0] = s_dtmfs[sig];
	tmp[1] = '\0';
	Message* msg = message("chan.dtmf");
	copySipHeaders(*msg,*t->initialMessage());
	msg->addParam("text",tmp);
	msg->addParam("detected","sip-info");
	dtmfEnqueue(msg);
    }
    return true;
}

void YateSIPConnection::doRefer(SIPTransaction* t)
{
    if (m_authBye && !checkUser(t))
	return;
    DDebug(this,DebugAll,"doRefer(%p) [%p]",t,this);
    if (m_hungup) {
	t->setResponse(481);
	return;
    }
    if (m_referring) {
	DDebug(this,DebugAll,"doRefer(%p). Already referring [%p]",t,this);
	t->setResponse(491);           // Request Pending
	return;
    }
    m_referring = true;
    const MimeHeaderLine* refHdr = t->initialMessage()->getHeader("Refer-To");
    if (!(refHdr && refHdr->length())) {
	DDebug(this,DebugAll,"doRefer(%p). Empty or missing 'Refer-To' header [%p]",t,this);
	t->setResponse(400);           // Bad request
	m_referring = false;
	return;
    }

    // Get 'Refer-To' URI and its parameters
    URI uri(*refHdr);
    ObjList params;
    // Find the first parameter separator. Ignore everything before it
    int start = findURIParamSep(uri.getExtra(),0);
    if (start >= 0)
	start++;
    else
	start = uri.getExtra().length();
    while (start < (int)uri.getExtra().length()) {
	int end = findURIParamSep(uri.getExtra(),start);
	// Check if this is the last parameter or an empty one
	if (end < 0)
	    end = uri.getExtra().length();
	else if (end == start) {
	    start++;
	    continue;
	}
	String param;
	param = uri.getExtra().substr(start,end - start);
	start = end + 1;
	if (!param)
	    continue;
	param = param.uriUnescape();
	int eq = param.find("=");
	if (eq < 0) {
	    DDebug(this,DebugInfo,"doRefer(%p). Skipping 'Refer-To' URI param '%s' [%p]",
		t,param.c_str(),this);
	    continue;
	}
	String name = param.substr(0,eq).trimBlanks();
	String value = param.substr(eq + 1);
	DDebug(this,DebugAll,"doRefer(%p). Found 'Refer-To' URI param %s=%s [%p]",
	    t,name.c_str(),value.c_str(),this);
	if (name)
	    params.append(new MimeHeaderLine(name,value));
    }
    // Check attended transfer request parameters
    ObjList* repl = params.find("Replaces");
    const MimeHeaderLine* replaces = repl ? static_cast<MimeHeaderLine*>(repl->get()) : 0;
    if (replaces) {
	const String* fromTag = replaces->getParam("from-tag");
	const String* toTag = replaces->getParam("to-tag");
	if (null(replaces) || null(fromTag) || null(toTag)) {
	    DDebug(this,DebugAll,
		"doRefer(%p). Invalid 'Replaces' '%s' from-tag=%s to-tag=%s [%p]",
		t,replaces->safe(),c_safe(fromTag),c_safe(toTag),this);
	    t->setResponse(501);           // Not implemented
	    m_referring = false;
	    return;
	}
	// Avoid replacing the same connection
	if (isDialog(*replaces,*fromTag,*toTag)) {
	    DDebug(this,DebugAll,
		"doRefer(%p). Attended transfer request for the same dialog [%p]",
		t,this);
	    t->setResponse(400,"Can't replace the same dialog");           // Bad request
	    m_referring = false;
	    return;
	}
    }

    Message* msg = 0;
    SIPMessage* sipNotify = 0;
    Channel* ch = YOBJECT(Channel,getPeer());
    if (ch && ch->driver() &&
	initTransfer(msg,sipNotify,t->initialMessage(),refHdr,uri,replaces)) {
	(new YateSIPRefer(id(),ch->id(),ch->driver(),msg,sipNotify,t))->startup();
	return;
    }
    DDebug(this,DebugAll,"doRefer(%p). No peer or peer has no driver [%p]",t,this);
    t->setResponse(503);       // Service Unavailable
    m_referring = false;
}

void YateSIPConnection::complete(Message& msg, bool minimal) const
{
    Channel::complete(msg,minimal);
    if (minimal)
	return;
    Lock mylock(driver());
    if (m_domain)
	msg.setParam("domain",m_domain);
    addCallId(msg,m_dialog,m_dialog.fromTag(isOutgoing()),m_dialog.toTag(isOutgoing()));
}

void YateSIPConnection::disconnected(bool final, const char *reason)
{
    Debug(this,DebugAll,"YateSIPConnection::disconnected() '%s' [%p]",reason,this);
    if (reason) {
	int code = lookup(reason,dict_errors);
	if (code >= 300 && code <= 699)
	    setReason(lookup(code,SIPResponses,reason),code);
	else
	    setReason(reason);
    }
    Channel::disconnected(final,reason);
}

bool YateSIPConnection::msgProgress(Message& msg)
{
    Channel::msgProgress(msg);
    int code = 183;
    const NamedString* reason = msg.getParam(YSTRING("reason"));
    if (reason) {
	// handle the special progress types that have provisional codes
	if (*reason == YSTRING("forwarded"))
	    code = 181;
	else if (*reason == YSTRING("queued"))
	    code = 182;
    }
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), code);
	copySipHeaders(*m,msg);
	m->setBody(buildSIPBody(msg,createProvisionalSDP(msg)));
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("progressing");
    return true;
}

bool YateSIPConnection::msgRinging(Message& msg)
{
    Channel::msgRinging(msg);
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 180);
	copySipHeaders(*m,msg);
	m->setBody(buildSIPBody(msg,createProvisionalSDP(msg)));
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("ringing");
    return true;
}

bool YateSIPConnection::msgAnswered(Message& msg)
{
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	updateFormats(msg,true);
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 200);
	copySipHeaders(*m,msg);
	MimeSdpBody* sdp = createPasstroughSDP(msg);
	if (!sdp) {
	    m_rtpForward = false;
	    bool startNow = msg.getBoolValue(YSTRING("rtp_start"),s_start_rtp);
	    if (startNow && !m_rtpMedia) {
		// early RTP start but media list yet unknown - build best guess
		String fmts;
		plugin.parser().getAudioFormats(fmts);
		ObjList* lst = new ObjList;
		lst->append(new SDPMedia("audio","RTP/AVP",msg.getValue(YSTRING("formats"),fmts)));
		setMedia(lst);
		m_rtpAddr = m_host;
	    }
	    // normally don't start RTP yet, only when we get the ACK
	    sdp = createRtpSDP(startNow);
	}
	m->setBody(buildSIPBody(msg,sdp));

	const MimeHeaderLine* co = m_tr->initialMessage()->getHeader("Contact");
	if (co) {
	    // INVITE had a Contact: header - time to change remote URI
	    m_uri = *co;
	    m_uri.parse();
	}

	// and finally send the answer, transaction will finish soon afterwards
	m_tr->setResponse(m);
	m->deref();
    }
    setReason("",0);
    setStatus("answered",Established);
    return true;
}

bool YateSIPConnection::msgTone(Message& msg, const char* tone)
{
    if (m_hungup)
	return false;
    bool info = m_info;
    bool inband = m_inband;
    const String* method = msg.getParam(YSTRING("method"));
    if (method) {
	if ((*method == YSTRING("info")) || (*method == YSTRING("sip-info"))) {
	    info = true;
	    inband = false;
	}
	else if (*method == YSTRING("rfc2833")) {
	    info = false;
	    inband = false;
	}
	else if (*method == YSTRING("inband")) {
	    info = false;
	    inband = true;
	}
    }
    // RFC 2833 and inband require that we have an active local RTP stream
    if (m_rtpMedia && (m_mediaStatus == MediaStarted) && !info) {
	ObjList* l = m_rtpMedia->find("audio");
	const SDPMedia* m = static_cast<const SDPMedia*>(l ? l->get() : 0);
	if (m) {
	    if (!(inband || m->rfc2833().toBoolean(true))) {
		Debug(this,DebugNote,"Forcing DTMF '%s' inband, format '%s' [%p]",
		    tone,m->format().c_str(),this);
		inband = true;
	    }
	    if (inband && dtmfInband(tone))
		return true;
	    msg.setParam("targetid",m->id());
	    return false;
	}
    }
    // either INFO was requested or we have no other choice
    for (; tone && *tone; tone++) {
	char c = *tone;
	for (int i = 0; i <= 16; i++) {
	    if (s_dtmfs[i] == c) {
		SIPMessage* m = createDlgMsg("INFO");
		if (m) {
		    copySipHeaders(*m,msg);
		    String tmp;
		    tmp << "Signal=" << i << "\r\n";
		    m->setBody(new MimeStringBody("application/dtmf-relay",tmp));
		    plugin.ep()->engine()->addMessage(m);
		    m->deref();
		}
		break;
	    }
	}
    }
    return true;
}

bool YateSIPConnection::msgText(Message& msg, const char* text)
{
    if (m_hungup || null(text))
	return false;
    SIPMessage* m = createDlgMsg("MESSAGE");
    if (m) {
	copySipHeaders(*m,msg);
	m->setBody(new MimeStringBody("text/plain",text));
	plugin.ep()->engine()->addMessage(m);
	m->deref();
	return true;
    }
    return false;
}

bool YateSIPConnection::msgDrop(Message& msg, const char* reason)
{
    if (!Channel::msgDrop(msg,reason))
	return false;
    int code = lookup(reason,dict_errors);
    if (code >= 300 && code <= 699) {
	m_reasonCode = code;
	m_reason = lookup(code,SIPResponses,reason);
    }
    return true;
}

bool YateSIPConnection::msgUpdate(Message& msg)
{
    String* oper = msg.getParam(YSTRING("operation"));
    if (!oper || oper->null())
	return false;
    Lock lock(driver());
    if (m_hungup)
	return false;
    if (*oper == YSTRING("request")) {
	if (m_tr || m_tr2) {
	    DDebug(this,DebugWarn,"Update request rejected, pending:%s%s [%p]",
		m_tr ? " invite" : "",m_tr2 ? " reinvite" : "",this);
	    msg.setParam("error","pending");
	    msg.setParam("reason","Another INVITE Pending");
	    return false;
	}
	return startClientReInvite(msg,true);
    }
    if (*oper == YSTRING("initiate")) {
	if (m_reInviting != ReinviteNone) {
	    msg.setParam("error","pending");
	    msg.setParam("reason","Another INVITE Pending");
	    return false;
	}
	m_reInviting = ReinvitePending;
	startPendingUpdate();
	return true;
    }
    if (!m_tr2) {
	if (*oper == YSTRING("notify")) {
	    switch (m_reInviting) {
		case ReinviteNone:
		    if (!msg.getBoolValue(YSTRING("audio_changed")))
			break;
		    // if any side is forwarding RTP we shouldn't reach here
		    if (m_rtpForward || msg.getBoolValue(YSTRING("rtp_forward")))
			break;
		    if (msg.getBoolValue(YSTRING("media"),true) ||
			msg.getBoolValue(YSTRING("mute"),false))
			    break;
		    // fall through
		case ReinviteRequest:
		    if (startClientReInvite(msg,(ReinviteRequest == m_reInviting)))
			return true;
		    Debug(this,DebugMild,"Failed to start reINVITE, %s: %s [%p]",
			msg.getValue(YSTRING("error"),"unknown"),
			msg.getValue(YSTRING("reason"),"No reason"),this);
		    return false;
	    }
	}
	msg.setParam("error","nocall");
	return false;
    }
    if (!(m_tr2->isIncoming() && (m_tr2->getState() == SIPTransaction::Process))) {
	msg.setParam("error","failure");
	msg.setParam("reason","Incompatible Transaction State");
	return false;
    }
    if (*oper == YSTRING("notify")) {
	bool rtpSave = m_rtpForward;
	m_rtpForward = msg.getBoolValue(YSTRING("rtp_forward"),m_rtpForward);
	MimeSdpBody* sdp = createPasstroughSDP(msg);
	if (!sdp) {
	    m_rtpForward = rtpSave;
	    m_tr2->setResponse(500,"Server failed to build the SDP");
	    detachTransaction2();
	    return false;
	}
	if (m_rtpForward != rtpSave)
	    Debug(this,DebugInfo,"RTP forwarding changed: %s -> %s",
		String::boolText(rtpSave),String::boolText(m_rtpForward));
	SIPMessage* m = new SIPMessage(m_tr2->initialMessage(), 200);
	m->setBody(sdp);
	m_tr2->setResponse(m);
	detachTransaction2();
	m->deref();
	return true;
    }
    else if (*oper == YSTRING("reject")) {
	m_tr2->setResponse(msg.getIntValue(YSTRING("error"),dict_errors,488),msg.getValue(YSTRING("reason")));
	detachTransaction2();
	return true;
    }
    return false;
}

void YateSIPConnection::endDisconnect(const Message& msg, bool handled)
{
    const String* reason = msg.getParam(YSTRING("reason"));
    if (!TelEngine::null(reason)) {
	int code = reason->toInteger(dict_errors);
	if (code >= 300 && code <= 699)
	    setReason(lookup(code,SIPResponses,*reason),code);
	else
	    setReason(*reason,m_reasonCode);
    }
    const char* sPrefix = msg.getValue(YSTRING("osip-prefix"));
    const char* mPrefix = msg.getValue(YSTRING("message-prefix"));
    if (!(sPrefix || mPrefix))
        return;
    paramMutex().lock();
    parameters().clearParams();
    if (sPrefix) {
	parameters().setParam("osip-prefix",sPrefix);
	parameters().copySubParams(msg,sPrefix,false);
    }
    if (mPrefix) {
	parameters().setParam("message-prefix",mPrefix);
	parameters().copySubParams(msg,mPrefix,false);
    }
    paramMutex().unlock();
}

void YateSIPConnection::statusParams(String& str)
{
    Channel::statusParams(str);
    if (m_line)
	str << ",line=" << m_line;
    if (m_user)
	str << ",user=" << m_user;
    if (m_rtpForward)
	str << ",forward=" << (m_sdpForward ? "sdp" : "rtp");
    str << ",inviting=" << (m_tr != 0);
}

bool YateSIPConnection::callRouted(Message& msg)
{
    // try to disable RTP forwarding earliest possible
    if (m_rtpForward && !msg.getBoolValue(YSTRING("rtp_forward")))
	m_rtpForward = false;
    setRfc2833(msg.getParam(YSTRING("rfc2833")));
    Channel::callRouted(msg);
    Lock lock(driver());
    if (m_hungup || !m_tr)
	return false;
    if (m_tr->getState() == SIPTransaction::Process) {
	String s(msg.retValue());
	if (s.startSkip("sip/",false) && s && msg.getBoolValue(YSTRING("redirect"))) {
	    Debug(this,DebugAll,"YateSIPConnection redirecting to '%s' [%p]",s.c_str(),this);
	    String tmp(msg.getValue(YSTRING("calledname")));
	    if (tmp) {
		MimeHeaderLine::addQuotes(tmp);
		tmp += " ";
	    }
	    s = tmp + "<" + s + ">";
	    int code = msg.getIntValue(YSTRING("reason"),dict_errors,302);
	    if ((code < 300) || (code > 399))
		code = 302;
	    SIPMessage* m = new SIPMessage(m_tr->initialMessage(),code);
	    m->addHeader("Contact",s);
	    tmp = msg.getValue(YSTRING("diversion"));
	    if (tmp.trimBlanks() && tmp.toBoolean(true)) {
		// if diversion is a boolean true use the dialog local URI
		if (tmp.toBoolean(false))
		    tmp = m_dialog.localURI;
		if (!(tmp.startsWith("<") && tmp.endsWith(">")))
		    tmp = "<" + tmp + ">";
		MimeHeaderLine* hl = new MimeHeaderLine("Diversion",tmp);
		tmp = msg.getValue(YSTRING("divert_reason"));
		if (tmp) {
		    MimeHeaderLine::addQuotes(tmp);
		    hl->setParam("reason",tmp);
		}
		tmp = msg.getValue(YSTRING("divert_privacy"));
		if (tmp) {
		    MimeHeaderLine::addQuotes(tmp);
		    hl->setParam("privacy",tmp);
		}
		tmp = msg.getValue(YSTRING("divert_screen"));
		if (tmp) {
		    MimeHeaderLine::addQuotes(tmp);
		    hl->setParam("screen",tmp);
		}
		m->addHeader(hl);
	    }
	    copySipHeaders(*m,msg);
	    m_tr->setResponse(m);
	    m->deref();
	    m_byebye = false;
	    setReason("Redirected",302);
	    setStatus("redirected");
	    return false;
	}

	updateFormats(msg);
	if (msg.getBoolValue(YSTRING("progress"),s_progress))
	    m_tr->setResponse(183);
    }
    return true;
}

void YateSIPConnection::callAccept(Message& msg)
{
    m_user = msg.getValue(YSTRING("username"));
    if (m_authBye)
	m_authBye = msg.getBoolValue(YSTRING("xsip_auth_bye"),true);
    if (m_rtpForward) {
	String tmp(msg.getValue(YSTRING("rtp_forward")));
	if (tmp != YSTRING("accepted"))
	    m_rtpForward = false;
    }
    m_secure = m_secure && msg.getBoolValue(YSTRING("secure"),true);
    Channel::callAccept(msg);

    if ((m_reInviting == ReinviteNone) && !m_rtpForward && !isAnswered() && 
	msg.getBoolValue(YSTRING("autoreinvite"),false)) {
	// remember we want to switch to RTP forwarding when party answers
	m_reInviting = ReinvitePending;
	startPendingUpdate();
    }
}

void YateSIPConnection::callRejected(const char* error, const char* reason, const Message* msg)
{
    Channel::callRejected(error,reason,msg);
    int code = lookup(error,dict_errors,500);
    if (code < 300 || code > 699)
	code = 500;
    Lock lock(driver());
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	if (code == 401) {
	    Lock lck(s_globalMutex);
	    m_tr->requestAuth(s_realm,m_domain,false);
	}
	else
	    m_tr->setResponse(code,reason);
    }
    setReason(reason,code);
}

// Start a client reINVITE transaction
bool YateSIPConnection::startClientReInvite(Message& msg, bool rtpForward)
{
    bool hadRtp = !m_rtpForward;
    if (msg.getBoolValue(YSTRING("rtp_forward"),m_rtpForward) != rtpForward) {
	msg.setParam("error","failure");
	msg.setParam("reason","Mismatched RTP forwarding");
	return false;
    }
    m_rtpForward = rtpForward;
    // this is the point of no return
    if (hadRtp)
	clearEndpoint();
    MimeSdpBody* sdp = 0;
    if (rtpForward)
	sdp = createPasstroughSDP(msg,false);
    else {
	updateSDP(msg);
	sdp = createRtpSDP(true);
    }
    if (!sdp) {
	msg.setParam("error","failure");
	msg.setParam("reason","Could not build the SDP");
	if (hadRtp) {
	    Debug(this,DebugWarn,"Could not build SDP for reINVITE, hanging up [%p]",this);
	    disconnect("nomedia");
	}
	return false;
    }
    Debug(this,DebugNote,"Initiating reINVITE (%s RTP before) [%p]",
	hadRtp ? "had" : "no",this);
    SIPMessage* m = createDlgMsg("INVITE");
    copySipHeaders(*m,msg);
    if (s_privacy)
	copyPrivacy(*m,msg);
    m->setBody(sdp);
    m_tr2 = plugin.ep()->engine()->addMessage(m);
    if (m_tr2) {
	m_tr2->ref();
	m_tr2->setUserData(this);
    }
    m->deref();
    return true;
}

// Emit pending update if possible, method is called with driver mutex hold
void YateSIPConnection::startPendingUpdate()
{
    Lock mylock(driver());
    if (m_hungup || m_tr || m_tr2 || (m_reInviting != ReinvitePending))
	return;
    if (m_rtpAddr.null()) {
	Debug(this,DebugWarn,"Cannot start update, remote RTP address unknown [%p]",this);
	m_reInviting = ReinviteNone;
	return;
    }
    if (!m_rtpMedia) {
	Debug(this,DebugWarn,"Cannot start update, remote media unknown [%p]",this);
	m_reInviting = ReinviteNone;
	return;
    }
    m_reInviting = ReinviteRequest;
    mylock.drop();

    Message msg("call.update");
    complete(msg);
    msg.addParam("operation","request");
    msg.addParam("rtp_forward","yes");
    msg.addParam("rtp_addr",m_rtpAddr);
    putMedia(msg);
    // if peer doesn't support updates fail the reINVITE
    if (!Engine::dispatch(msg)) {
	Debug(this,DebugWarn,"Cannot start update by '%s', %s: %s [%p]",
	    getPeerId().c_str(),
	    msg.getValue(YSTRING("error"),"not supported"),
	    msg.getValue(YSTRING("reason"),"No reason provided"),this);
	m_reInviting = ReinviteNone;
    }
}

// Build the 'call.route' and NOTIFY messages needed by the transfer thread
// msg: 'call.route' message to create & fill
// sipNotify: NOTIFY message to create & fill
// sipRefer: received REFER message, refHdr: 'Refer-To' header
// refHdr: The 'Refer-To' header
// uri: The already parsed 'Refer-To' URI
// replaces: An already checked Replaces parameter from 'Refer-To' or
//  0 for unattended transfer
// If return false, msg and sipNotify are 0
bool YateSIPConnection::initTransfer(Message*& msg, SIPMessage*& sipNotify,
    const SIPMessage* sipRefer, const MimeHeaderLine* refHdr,
    const URI& uri, const MimeHeaderLine* replaces)
{
    // call.route
    msg = new Message("call.route");
    msg->addParam("id",getPeer()->id());
    if (m_billid)
	msg->addParam("billid",m_billid);
    if (m_user)
	msg->addParam("username",m_user);

    const MimeHeaderLine* sh = sipRefer->getHeader("To");                   // caller
    if (sh) {
	URI uriCaller(*sh);
	uriCaller.parse();
	msg->addParam("caller",uriCaller.getUser());
	msg->addParam("callername",uriCaller.getDescription());
    }

    if (replaces) {                                                        // called or replace
	const String* fromTag = replaces->getParam("from-tag");
	const String* toTag = replaces->getParam("to-tag");
	msg->addParam("transfer_callid",*replaces);
	msg->addParam("transfer_fromtag",c_safe(fromTag));
	msg->addParam("transfer_totag",c_safe(toTag));
    }
    else {
	msg->addParam("called",uri.getUser());
	msg->addParam("calledname",uri.getDescription());
    }

    sh = sipRefer->getHeader("Referred-By");                               // diverter
    URI referBy;
    if (sh)
	referBy = *sh;
    else
	referBy = m_dialog.remoteURI;
    msg->addParam("diverter",referBy.getUser());
    msg->addParam("divertername",referBy.getDescription());

    msg->addParam("reason","transfer");                                    // reason
    // NOTIFY
    String tmp;
    const MimeHeaderLine* co = sipRefer->getHeader("Contact");
    // TODO: Handle contact: it might require a different transport
    // If we need another transport and is a connected one, try to delay party creation:
    //  we won't need it if the transfer fails
    // Set notify party from received REFER?
    // Remember: createDlgMsg() sets the party from channel's party
    Debug(this,DebugStub,"initTransfer. Possible incomplete NOTIFY party creation [%p]",this);
    if (co) {
	tmp = *co;
	static const Regexp r("^[^<]*<\\([^>]*\\)>.*$");
	if (tmp.matches(r))
	    tmp = tmp.matchString(1);
    }
    sipNotify = createDlgMsg("NOTIFY",tmp);
    if (!sipNotify->getParty() && plugin.ep())
	plugin.ep()->buildParty(sipNotify);
    if (!sipNotify->getParty()) {
	DDebug(this,DebugAll,"initTransfer. Could not create party to send NOTIFY [%p]",this);
	TelEngine::destruct(sipNotify);
	TelEngine::destruct(msg);
	return false;
    }
    copySipHeaders(*msg,*sipRefer);
    sipNotify->complete(plugin.ep()->engine());
    sipNotify->addHeader("Event","refer");
    sipNotify->addHeader("Subscription-State","terminated;reason=noresource");
    sipNotify->addHeader("Contact",sipRefer->uri);
    return true;
}

// Decode an application/isup body into 'msg' if configured to do so
bool YateSIPConnection::decodeIsupBody(Message& msg, MimeBody* body)
{
    return doDecodeIsupBody(this,msg,body);
}

// Build the body of a SIP message from an engine message
MimeBody* YateSIPConnection::buildSIPBody(Message& msg, MimeSdpBody* sdp)
{
    return doBuildSIPBody(this,msg,sdp);
}

// Build the body of a hangup SIP message from disconnect parameters
MimeBody* YateSIPConnection::buildSIPBody()
{
    if (!s_sipt_isup)
	return 0;
    Message msg("");
    paramMutex().lock();
    msg.copyParams(parameters());
    paramMutex().unlock();
    return doBuildSIPBody(this,msg,0);
}


YateSIPLine::YateSIPLine(const String& name)
    : String(name), Mutex(true,"YateSIPLine"),
      m_resend(0), m_keepalive(0), m_interval(0), m_alive(0),
      m_flags(-1), m_tr(0), m_marked(false), m_valid(false),
      m_localPort(0), m_partyPort(0), m_localDetect(false),
      m_keepTcpOffline(s_lineKeepTcpOffline)
{
    m_partyMutex = this;
    DDebug(&plugin,DebugInfo,"YateSIPLine::YateSIPLine('%s') [%p]",c_str(),this);
    s_lines.append(this);
}

YateSIPLine::~YateSIPLine()
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::~YateSIPLine() '%s' [%p]",c_str(),this);
    s_lines.remove(this,false);
    logout();
}

void YateSIPLine::setupAuth(SIPMessage* msg) const
{
    if (msg)
	msg->setAutoAuth(getAuthName(),m_password);
}

void YateSIPLine::setValid(bool valid, const char* reason, const char* error)
{
    DDebug(&plugin,DebugInfo,"YateSIPLine(%s) setValid(%u,%s) current=%u [%p]",
	c_str(),valid,reason,m_valid,this);
    if ((m_valid == valid) && !reason)
	return;
    m_valid = valid;
    if (m_registrar && m_username) {
	Message* m = new Message("user.notify");
	m->addParam("account",*this);
	m->addParam("protocol","sip");
	m->addParam("username",m_username);
	if (m_domain)
	    m->addParam("domain",m_domain);
	m->addParam("registered",String::boolText(valid));
	m->addParam("reason",reason,false);
	m->addParam("error",error,false);
	Engine::enqueue(m);
    }
}

void YateSIPLine::changing()
{
    // we need to log out before any parameter changes
    logout();
}

SIPMessage* YateSIPLine::buildRegister(int expires) const
{
    String exp(expires);
    String tmp;
    tmp << "sip:" << m_registrar;
    SIPMessage* m = new SIPMessage("REGISTER",tmp);
    setSipParty(m,this);
    if (!m->getParty()) {
	Debug(&plugin,DebugWarn,"Could not create party for '%s' [%p]",
	    m_registrar.c_str(),this);
	m->destruct();
	return 0;
    }
    tmp.clear();
    if (m_display)
	tmp = MimeHeaderLine::quote(m_display) + " ";
    tmp << "<sip:";
    tmp << m_username << "@";
    Lock lckParty(m->getParty()->mutex());
    tmp << m->getParty()->getLocalAddr() << ":";
    tmp << m->getParty()->getLocalPort() << ">";
    lckParty.drop();
    m->addHeader("Contact",tmp);
    m->addHeader("Expires",exp);
    tmp = "<sip:";
    tmp << m_username << "@" << domain() << ">";
    m->addHeader("To",tmp);
    if (m_callid)
	m->addHeader("Call-ID",m_callid);
    m->complete(plugin.ep()->engine(),m_username,domain(),0,m_flags);
    return m;
}

void YateSIPLine::login()
{
    m_keepalive = 0;
    if (m_registrar.null() || m_username.null()) {
	logout();
	setValid(true);
	return;
    }
    DDebug(&plugin,DebugInfo,"YateSIPLine '%s' logging in [%p]",c_str(),this);
    clearTransaction();
    // prepare a sane resend interval, just in case something goes wrong
    int interval = m_interval / 2;
    if (interval) {
	if (interval < 30)
	    interval = 30;
	else if (interval > 600)
	    interval = 600;
	m_resend = interval*(int64_t)1000000 + Time::now();
    }

    buildParty(false);
    // Wait for the transport to become valid
    Lock lckParty(m_partyMutex);
    YateSIPTransport* trans = transport();
    if (!(trans && trans->valid())) {
	DDebug(&plugin,DebugInfo,
	    "YateSIPLine '%s' delaying login (transport not ready) [%p]",c_str(),this);
	return;
    }
    lckParty.drop();
    SIPMessage* m = buildRegister(m_interval);
    if (!m) {
	setValid(false);
	if (!m_keepTcpOffline)
	    setParty();
	return;
    }

    if (m_localDetect) {
	Lock lck(m->getParty()->mutex());
	if (m_localAddr.null())
	    m_localAddr = m->getParty()->getLocalAddr();
	if (!m_localPort)
	    m_localPort = m->getParty()->getLocalPort();
    }

    DDebug(&plugin,DebugInfo,"YateSIPLine '%s' emiting %p [%p]",
	c_str(),m,this);
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_tr->setUserData(this);
	if (m_callid.null())
	    m_callid = m_tr->getCallID();
    }
    m->deref();
}

void YateSIPLine::logout(bool sendLogout, const char* reason)
{
    m_resend = 0;
    m_keepalive = 0;
    if (sendLogout)
	sendLogout = m_valid && m_registrar && m_username;
    clearTransaction();
    setValid(false,reason);
    if (sendLogout) {
	DDebug(&plugin,DebugInfo,"YateSIPLine '%s' logging out [%p]",c_str(),this);
	buildParty(false);
	SIPMessage* m = buildRegister(0);
	m_partyAddr.clear();
	m_partyPort = 0;
	if (!m)
	    return;
	plugin.ep()->engine()->addMessage(m);
	m->deref();
    }
    m_callid.clear();
}

bool YateSIPLine::process(SIPEvent* ev)
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    if (ev->getTransaction() != m_tr)
	return false;
    if (ev->getState() == SIPTransaction::Cleared) {
	clearTransaction();
	setValid(false,"timeout");
	if (!m_keepTcpOffline)
	    setParty();
	m_keepalive = 0;
	Debug(&plugin,DebugWarn,"SIP line '%s' logon timeout",c_str());
	return false;
    }
    const SIPMessage* msg = ev->getMessage();
    if (!(msg && msg->isAnswer()))
	return false;
    if (ev->getState() != SIPTransaction::Process)
	return false;
    clearTransaction();
    DDebug(&plugin,DebugAll,"YateSIPLine '%s' got answer %d [%p]",
	c_str(),msg->code,this);
    switch (msg->code) {
	case 200:
	    {
		int exp = m_interval;
		const MimeHeaderLine* hl = msg->getHeader("Contact");
		if (hl) {
		    const NamedString* e = hl->getParam("expires");
		    if (e)
			exp = e->toInteger(exp);
		    else
			hl = 0;
		}
		if (!hl) {
		    hl = msg->getHeader("Expires");
		    if (hl)
			exp = hl->toInteger(exp);
		}
		if ((exp != m_interval) && (exp >= 60)) {
		    Debug(&plugin,DebugNote,"SIP line '%s' changed expire interval from %d to %d",
			c_str(),m_interval,exp);
		    m_interval = exp;
		}
		// Reset transport timeout from expires or flow timer
		resetTransportIdle(msg,m_alive ? m_alive : m_interval);
	    }
	    // re-register at 3/4 of the expire interval
	    m_resend = m_interval*(int64_t)750000 + Time::now();
	    m_keepalive = m_alive ? m_alive*(int64_t)1000000 + Time::now() : 0;
	    detectLocal(msg);
	    if (msg->getParty())
		msg->getParty()->getAddr(m_partyAddr,m_partyPort,false);
	    setValid(true);
	    Debug(&plugin,DebugCall,"SIP line '%s' logon success to %s:%d",
		c_str(),m_partyAddr.c_str(),m_partyPort);
	    break;
	default:
	    // detect local address even from failed attempts - helps next time
	    detectLocal(msg);
	    setValid(false,msg->reason,lookup(msg->code,dict_errors,String(msg->code)));
	    if (!m_keepTcpOffline)
		setParty();
	    Debug(&plugin,DebugWarn,"SIP line '%s' logon failure %d: %s",
		c_str(),msg->code,msg->reason.safe());
    }
    return false;
}

void YateSIPLine::detectLocal(const SIPMessage* msg)
{
    if (!(m_localDetect && msg->getParty()))
	return;
    String laddr = m_localAddr;
    int lport = m_localPort;
    MimeHeaderLine* hl = const_cast<MimeHeaderLine*>(msg->getHeader("Via"));
    if (hl) {
	const NamedString* par = hl->getParam("received");
	if (par && *par)
	    laddr = *par;
	par = hl->getParam("rport");
	if (par) {
	    int port = par->toInteger(0,10);
	    if (port > 0)
		lport = port;
	}
    }
    Lock lckParty(msg->getParty()->mutex());
    if (laddr.null())
	laddr = msg->getParty()->getLocalAddr();
    if (!lport)
	lport = msg->getParty()->getLocalPort();
    lckParty.drop();
    if ((laddr != m_localAddr) || (lport != m_localPort)) {
	Debug(&plugin,DebugInfo,"Detected local address %s:%d for SIP line '%s'",
	    laddr.c_str(),lport,c_str());
	m_localAddr = laddr;
	m_localPort = lport;
	// since local address changed register again in 2 seconds
	m_resend = 2000000 + Time::now();
	// Update now party local ip/port
	SIPParty* p = party();
	if (p) {
	    p->setAddr(m_localAddr,m_localPort,true);
	    TelEngine::destruct(p);
	}
    }
}

void YateSIPLine::keepalive()
{
    if (!m_party)
        return;
    Lock lock(m_partyMutex);
    if (!m_party || m_party->isReliable())
        return;
    YateUDPParty* udp = static_cast<YateUDPParty*>(m_party);
    YateSIPUDPTransport* t = static_cast<YateSIPUDPTransport*>(m_party->getTransport());
    if (t) {
	Debug(&plugin,DebugAll,"Sending UDP keepalive to %s:%d for '%s'",
	    udp->addr().host().c_str(),udp->addr().port(),c_str());
	t->send("\r\n",2,udp->addr());
    }
    m_keepalive = m_alive ? m_alive*(int64_t)1000000 + Time::now() : 0;
}

void YateSIPLine::timer(const Time& when)
{
    if (!m_resend || (m_resend > when)) {
	if (m_keepalive && (m_keepalive <= when))
	    keepalive();
	return;
    }
    m_resend = 0;
    login();
}

void YateSIPLine::clearTransaction()
{
    if (m_tr) {
	DDebug(&plugin,DebugInfo,"YateSIPLine clearing transaction %p [%p]",
	    m_tr,this);
	m_tr->setUserData(0);
	m_tr->deref();
	m_tr = 0;
    }
}

bool YateSIPLine::update(const Message& msg)
{
    DDebug(&plugin,DebugInfo,"YateSIPLine::update() '%s' [%p]",c_str(),this);
    const String& oper = msg[YSTRING("operation")];
    if (oper == YSTRING("logout")) {
	logout();
	setParty();
	return true;
    }
    bool chg = updateProto(msg);
    bool transChg = chg;
    transChg = updateLocalAddr(msg) || transChg;
    chg = change(m_registrar,msg.getValue(YSTRING("registrar"),msg.getValue(YSTRING("server")))) || chg;
    chg = change(m_username,msg.getValue(YSTRING("username"))) || chg;
    chg = change(m_authname,msg.getValue(YSTRING("authname"))) || chg;
    chg = change(m_password,msg.getValue(YSTRING("password"))) || chg;
    chg = change(m_domain,msg.getValue(YSTRING("domain"))) || chg;
    chg = change(m_flags,msg.getIntValue(YSTRING("xsip_flags"),-1)) || chg;
    m_display = msg.getValue(YSTRING("description"));
    m_interval = msg.getIntValue(YSTRING("interval"),600);
    String tmp(msg.getValue(YSTRING("localaddress"),s_auto_nat ? "auto" : ""));
    // "auto", "yes", "enable" or "true" to autodetect local address
    m_localDetect = (tmp == YSTRING("auto")) || tmp.toBoolean(false);
    if (!m_localDetect) {
	// "no", "disable" or "false" to just disable detection
	if (!tmp.toBoolean(true))
	    tmp.clear();
	int port = 0;
	if (tmp) {
	    int sep = tmp.find(':');
	    if (sep > 0) {
		port = tmp.substr(sep+1).toInteger(5060);
		tmp = tmp.substr(0,sep);
	    }
	    else if (sep < 0)
		port = 5060;
	}
	chg = change(m_localAddr,tmp) || chg;
	chg = change(m_localPort,port) || chg;
    }
    String raddr;
    int rport = 0;
    tmp = msg.getValue(YSTRING("outbound"));
    if (tmp) {
	int sep = tmp.find(':');
	if (sep > 0) {
	    rport = tmp.substr(sep + 1).toInteger(0);
	    raddr = tmp.substr(0,sep);
	}
	else
	    raddr = tmp;
    }
    if (!raddr) {
	int sep = m_registrar.find(':');
	if (sep > 0) {
	    rport = m_registrar.substr(sep + 1).toInteger(0);
	    raddr = m_registrar.substr(0,sep);
	}
	else
	    raddr = m_registrar;
    }
    if (!raddr)
	raddr = m_transRemoteAddr;
    if (rport <= 0)
	rport = sipPort(protocol() != Tls);
    bool rAddrChg = change(m_transRemoteAddr,raddr);
    rAddrChg = change(m_transRemotePort,rport) || rAddrChg;
    if (rAddrChg) {
	transChg = true;
	chg = true;
    }
    m_alive = msg.getIntValue(YSTRING("keepalive"),(m_localDetect ? 25 : 0));
    // (Re)Set party
    if (transChg || !m_party) {
	// Logout if not already done
	if (!chg) {
	    chg = true;
	    logout();
	}
	buildParty();
	if (!m_party)
	    Debug(&plugin,DebugNote,"Line '%s' failed to set party [%p]",c_str(),this);
    }
    // if something changed we logged out so try to climb back
    if (chg || (oper == YSTRING("login")))
	login();
    return chg;
}

// Transport status changed notification
void YateSIPLine::transportChangedStatus(int stat, const String& reason)
{
    Debug(&plugin,DebugAll,"Line '%s' transport status is %s",
	c_str(),YateSIPTransport::statusName(stat));
    YateSIPTransport* trans = transport();
    if (stat == YateSIPTransport::Terminated) {
	u_int64_t old = m_resend;
	logout(trans && trans->udpTransport(),reason);
	setParty();
	// Try to re-login if set to do that
	m_resend = old;
    }
    else if (stat == YateSIPTransport::Connected) {
	if (trans) {
	    Lock lock(trans);
	    m_localAddr = trans->local().host();
	    m_localPort = trans->local().port();
	}
	// Pending login
	if (trans && m_resend)
	    login();
    }
}

YateSIPGenerate::YateSIPGenerate(SIPMessage* m)
    : m_tr(0), m_code(0)
{
    m_tr = plugin.ep()->engine()->addMessage(m);
    if (m_tr) {
	m_tr->ref();
	m_tr->setUserData(this);
    }
    m->deref();
}

YateSIPGenerate::~YateSIPGenerate()
{
    clearTransaction();
}

bool YateSIPGenerate::process(SIPEvent* ev)
{
    DDebug(&plugin,DebugInfo,"YateSIPGenerate::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    if (ev->getTransaction() != m_tr)
	return false;
    if (ev->getState() == SIPTransaction::Cleared) {
	clearTransaction();
	return false;
    }
    const SIPMessage* msg = ev->getMessage();
    if (!(msg && msg->isAnswer()))
	return false;
    if (ev->getState() != SIPTransaction::Process)
	return false;
    clearTransaction();
    Debug(&plugin,DebugAll,"YateSIPGenerate got answer %d [%p]",
	m_code,this);
    return false;
}

void YateSIPGenerate::clearTransaction()
{
    if (m_tr) {
	DDebug(&plugin,DebugInfo,"YateSIPGenerate clearing transaction %p [%p]",
	    m_tr,this);
	m_code = m_tr->getResponseCode();
	m_tr->setUserData(0);
	m_tr->deref();
	m_tr = 0;
    }
}


bool UserHandler::received(Message &msg)
{
    String tmp(msg.getValue(YSTRING("protocol")));
    if (tmp != YSTRING("sip"))
	return false;
    tmp = msg.getValue(YSTRING("account"));
    if (tmp.null())
	return false;
    YateSIPLine* line = plugin.findLine(tmp);
    if (!line)
	line = new YateSIPLine(tmp);
    line->update(msg);
    return true;
}


bool SipHandler::received(Message &msg)
{
    Debug(&plugin,DebugInfo,"SipHandler::received() [%p]",this);
    RefPointer<YateSIPConnection> conn;
    String uri;
    const char* id = msg.getValue(YSTRING("id"));
    if (id) {
	plugin.lock();
	conn = static_cast<YateSIPConnection*>(plugin.find(id));
	plugin.unlock();
	if (!conn) {
	    msg.setParam("error","noconn");
	    return false;
	}
	uri = conn->m_uri;
    }
    const char* method = msg.getValue(YSTRING("method"));
    uri = msg.getValue(YSTRING("uri"),uri);
    static const Regexp r("<\\([^>]\\+\\)>");
    if (uri.matches(r))
	uri = uri.matchString(1);
    if (!(method && uri))
	return false;

    int maxf = msg.getIntValue(YSTRING("antiloop"),s_maxForwards);
    if (maxf <= 0) {
	Debug(&plugin,DebugMild,"Blocking looping request '%s %s' [%p]",
	    method,uri.c_str(),this);
	msg.setParam("error","looping");
	return false;
    }

    SIPMessage* sip = 0;
    YateSIPLine* line = 0;
    const char* domain = msg.getValue(YSTRING("domain"));
    if (conn) {
	line = plugin.findLine(conn->getLine());
	sip = conn->createDlgMsg(method,uri);
	conn = 0;
    }
    else {
	line = plugin.findLine(msg.getValue(YSTRING("line")));
	if (line && !line->valid()) {
	    msg.setParam("error","offline");
	    return false;
	}
	sip = new SIPMessage(method,uri);
	YateSIPPartyHolder holder;
	const char* host = msg.getValue("host");
	int port = msg.getIntValue("port");
	holder.setParty(msg,false,String::empty(),host,port);
	holder.setSipParty(sip,line,true,host,port);
	if (line)
	    domain = line->domain(domain);
    }
    if (!sip->getParty()) {
	Debug(&plugin,DebugWarn,"Could not create party to generate '%s'",
	    sip->method.c_str());
	TelEngine::destruct(sip);
	msg.setParam("error","notransport");
	return false;
    }
    sip->addHeader("Max-Forwards",String(maxf));
    copySipHeaders(*sip,msg,"sip_");
    const String& type = msg[YSTRING("xsip_type")];
    const String& body = msg[YSTRING("xsip_body")];
    if (type && body) {
	const String& bodyEnc = msg[YSTRING("xsip_body_encoding")];
	if (bodyEnc.null())
	    sip->setBody(new MimeStringBody(type,body.c_str(),body.length()));
	else {
	    DataBlock binBody;
	    bool ok = false;
	    if (bodyEnc == YSTRING("base64")) {
		Base64 b64;
		b64 << body;
		ok = b64.decode(binBody);
	    }
	    else if (bodyEnc == YSTRING("hex"))
		ok = binBody.unHexify(body,body.length());
	    else if (bodyEnc == YSTRING("hexs"))
		ok = binBody.unHexify(body,body.length(),' ');

	    if (ok)
		sip->setBody(new MimeBinaryBody(type,(const char*)binBody.data(),binBody.length()));
	    else
		Debug(&plugin,DebugWarn,"Invalid xsip_body_encoding '%s'",bodyEnc.c_str());
	}
    }
    sip->complete(plugin.ep()->engine(),msg.getValue(YSTRING("user")),domain,0,
	msg.getIntValue(YSTRING("xsip_flags"),-1));
    if (!msg.getBoolValue(YSTRING("wait"))) {
	// no answer requested - start transaction and forget
	plugin.ep()->engine()->addMessage(sip);
	sip->deref();
	return true;
    }
    YateSIPGenerate gen(sip);
    while (gen.busy())
	Thread::idle();
    if (gen.code())
	msg.setParam("code",String(gen.code()));
    else
	msg.clearParam("code");
    return true;
}


YateSIPConnection* SIPDriver::findCall(const String& callid, bool incRef)
{
    XDebug(this,DebugAll,"SIPDriver finding call '%s'",callid.c_str());
    Lock mylock(this);
    ObjList* l = channels().skipNull();
    for (; l; l = l->skipNext()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c->callid() == callid)
	    return (incRef ? c->ref() : c->alive()) ? c : 0;
    }
    return 0;
}

YateSIPConnection* SIPDriver::findDialog(const SIPDialog& dialog, bool incRef)
{
    XDebug(this,DebugAll,"SIPDriver finding dialog '%s'",dialog.c_str());
    Lock mylock(this);
    ObjList* l = channels().skipNull();
    for (; l; l = l->skipNext()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c->dialog() &= dialog)
	    return (incRef ? c->ref() : c->alive()) ? c : 0;
    }
    return 0;
}

YateSIPConnection* SIPDriver::findDialog(const String& dialog, const String& fromTag,
    const String& toTag, bool incRef)
{
    XDebug(this,DebugAll,"SIPDriver finding dialog '%s' fromTag='%s' toTag='%s'",
	dialog.c_str(),fromTag.c_str(),toTag.c_str());
    Lock mylock(this);
    for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(o->get());
	if (c->isDialog(dialog,fromTag,toTag))
	    return (incRef ? c->ref() : c->alive()) ? c : 0;
    }
    return 0;
}

// find line by name
YateSIPLine* SIPDriver::findLine(const String& line) const
{
    if (line.null())
	return 0;
    ObjList* l = s_lines.find(line);
    return l ? static_cast<YateSIPLine*>(l->get()) : 0;
}

// find line by party address and port
YateSIPLine* SIPDriver::findLine(const String& addr, int port, const String& user)
{
    if (!(port && addr))
	return 0;
    Lock mylock(this);
    ObjList* l = s_lines.skipNull();
    for (; l; l = l->skipNext()) {
	YateSIPLine* sl = static_cast<YateSIPLine*>(l->get());
	if (sl->getPartyPort() && (sl->getPartyPort() == port) && (sl->getPartyAddr() == addr)) {
	    if (user && (sl->getUserName() != user))
		continue;
	    return sl;
	}
    }
    return 0;
}

// Drop channels belonging using a given transport
// Return the number of disconnected channels
unsigned int SIPDriver::transportTerminated(YateSIPTransport* trans)
{
    unsigned int n = 0;
    lock();
    ListIterator iter(channels());
    while (true) {
	RefPointer<YateSIPConnection> conn = static_cast<YateSIPConnection*>(iter.get());
	unlock();
	if (!conn)
	    break;
	if (conn->isTransport(trans)) {
	    Debug(this,DebugNote,"Disconnecting '%s': transport terminated",
		conn->id().c_str());
	    n++;
	    conn->disconnect("notransport");
	}
	conn = 0;
	lock();
    }
    return n;
}

// check if a line is either empty or valid (logged in or no registrar)
bool SIPDriver::validLine(const String& line)
{
    if (line.null())
	return true;
    YateSIPLine* l = findLine(line);
    return l && l->valid();
}

bool SIPDriver::received(Message& msg, int id)
{
    if (id == Timer) {
	ObjList* l = s_lines.skipNull();
	for (; l; l = l->skipNext())
	    static_cast<YateSIPLine*>(l->get())->timer(msg.msgTime());
    }
    else if (id == Stop) {
	s_engineStop++;
	dropAll(msg);
	m_endpoint->cancelListener();
	// Logout lines on first handle
	// Delay engine.halt until all lines logged out, we have no more channels
	//  and there are no more transactions in initial state
	// This will give some time to TCP transports to send pending data
	bool noHalt = false;
	for (ObjList* o = s_lines.skipNull(); o; o = o->skipNext()) {
	    YateSIPLine* line = static_cast<YateSIPLine*>(o->get());
	    noHalt = noHalt || line->valid();
	    if (s_engineStop == 1)
		line->logout();
	}
	if (!noHalt) {
	    Lock lock(this);
	    noHalt = (0 != channels().skipNull());
	}
	if (!noHalt)
	    noHalt = m_endpoint->engine()->hasInitialTransaction();
	Debug(this,DebugAll,"Returning %s from %s handler",String::boolText(noHalt),msg.c_str());
	return noHalt;
    }
    else if (id == Halt) {
	s_engineHalt = true;
	dropAll(msg);
	channels().clear();
	s_lines.clear();
	// Clear transactions: they keep references to parties and transports
	m_endpoint->engine()->clearTransactions();
	m_endpoint->clearUdpTransports("Exiting");
	// Wait for transports to terminate
	unsigned int n = 100;
	while (--n) {
	    Lock lck(m_endpoint->m_mutex);
	    if (!m_endpoint->m_transports.skipNull())
		break;
	    lck.drop();
	    Thread::idle();
	}
	m_endpoint->m_mutex.lock();
	n = m_endpoint->m_transports.count();
	if (n)
	    Debug(this,DebugGoOn,"Exiting with %u transports in queue",n);
	m_endpoint->m_mutex.unlock();
	m_endpoint->cancel();
    }
    else if (id == Status) {
	String target = msg.getValue(YSTRING("module"));
	if (target && target.startsWith(name(),true) && !target.startsWith(prefix())) {
	    msgStatus(msg);
	    return false;
	}
    }
    return Driver::received(msg,id);
}

bool SIPDriver::hasLine(const String& line) const
{
    return line && findLine(line);
}

bool SIPDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugWarn,"SIP call found but no data channel!");
	return false;
    }
    const String& line = msg["line"];
    if (!validLine(line)) {
	// asked to use a line but it's not registered
	msg.setParam("error","offline");
	return false;
    }
    YateSIPConnection* conn = new YateSIPConnection(msg,dest,msg.getValue(YSTRING("id")));
    conn->initChan();
    if (conn->getTransaction()) {
	CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
	if (ch && conn->connect(ch,msg.getValue(YSTRING("reason")))) {
	    conn->callConnect(msg);
	    msg.setParam("peerid",conn->id());
	    msg.setParam("targetid",conn->id());
	    conn->deref();
	    return true;
	}
    }
    conn->destruct();
    return false;
}

SIPDriver::SIPDriver()
    : Driver("sip","varchans"), 
      m_parser("sip","SIP Call"),
      m_endpoint(0)
{
    Output("Loaded module SIP Channel");
    m_parser.debugChain(this);
}

SIPDriver::~SIPDriver()
{
    Output("Unloading module SIP Channel");
}

void SIPDriver::initialize()
{
    Output("Initializing module SIP Channel");
    s_cfg = Engine::configFile("ysipchan");
    s_cfg.load();
    s_maxForwards = s_cfg.getIntValue("general","maxforwards",20);
    s_floodEvents = s_cfg.getIntValue("general","floodevents",20);
    s_privacy = s_cfg.getBoolValue("general","privacy");
    s_auto_nat = s_cfg.getBoolValue("general","nat",true);
    s_progress = s_cfg.getBoolValue("general","progress",false);
    s_inband = s_cfg.getBoolValue("general","dtmfinband",false);
    s_info = s_cfg.getBoolValue("general","dtmfinfo",false);
    s_start_rtp = s_cfg.getBoolValue("general","rtp_start",false);
    s_multi_ringing = s_cfg.getBoolValue("general","multi_ringing",false);
    s_refresh_nosdp = s_cfg.getBoolValue("general","refresh_nosdp",true);
    s_ignoreVia = s_cfg.getBoolValue("general","ignorevia",true);
    s_printMsg = s_cfg.getBoolValue("general","printmsg",true);
    s_tcpMaxpkt = getMaxpkt(s_cfg.getIntValue("general","tcp_maxpkt",4096),4096);
    s_lineKeepTcpOffline = s_cfg.getBoolValue("general","line_keeptcpoffline",!Engine::clientMode());
    s_sipt_isup = s_cfg.getBoolValue("sip-t","isup",false);
    s_expires_min = s_cfg.getIntValue("registrar","expires_min",EXPIRES_MIN);
    s_expires_def = s_cfg.getIntValue("registrar","expires_def",EXPIRES_DEF);
    s_expires_max = s_cfg.getIntValue("registrar","expires_max",EXPIRES_MAX);
    s_auth_register = s_cfg.getBoolValue("registrar","auth_required",true);
    s_nat_refresh = s_cfg.getIntValue("registrar","nat_refresh",25);
    s_reg_async = s_cfg.getBoolValue("registrar","async_process",true);
    s_ack_required = !s_cfg.getBoolValue("hacks","ignore_missing_ack",false);
    s_1xx_formats = s_cfg.getBoolValue("hacks","1xx_change_formats",true);
    s_rtp_preserve = s_cfg.getBoolValue("hacks","ignore_sdp_addr",false);
    m_parser.initialize(s_cfg.getSection("codecs"),s_cfg.getSection("hacks"),s_cfg.getSection("general"));
    if (!m_endpoint) {
	Thread::Priority prio = Thread::priority(s_cfg.getValue("general","thread"));
	unsigned int partyMutexCount = s_cfg.getIntValue("general","party_mutexcount",47,13,101);
	m_endpoint = new YateSIPEndPoint(prio,partyMutexCount);
	if (!(m_endpoint->Init())) {
	    delete m_endpoint;
	    m_endpoint = 0;
	    return;
	}
	m_endpoint->startup();
	setup();
	installRelay(Halt);
	installRelay(Progress);
	installRelay(Update);
	installRelay(Route);
	installRelay(Status);
	installRelay(Stop,"engine.stop");
	Engine::install(new UserHandler);
	if (s_cfg.getBoolValue("general","generate"))
	    Engine::install(new SipHandler);
    }
    else {
	m_endpoint->engine()->initialize(s_cfg.getSection("general"));
	loadLimits();
    }
    // Unsafe globals
    s_globalMutex.lock();
    s_realm = s_cfg.getValue("general","realm","Yate");
    s_tcpOutRtpip = s_cfg.getValue("general","tcp_out_rtp_localip");
    s_sslCertFile = s_cfg.getValue("general","ssl_certificate_file");
    s_sslKeyFile = s_cfg.getValue("general","ssl_key_file");
    s_globalMutex.unlock();
    // Adjust here the TCP idle interval: it uses the SIP engine
    s_tcpIdle = tcpIdleInterval(s_cfg.getIntValue("general","tcp_idle",TCP_IDLE_DEF));
    // Mark listeners
    m_endpoint->initializing(true);
    // Setup general listener
    NamedList* general = s_cfg.getSection("general");
    NamedList dummy("general");
    NamedList* def = general;
    if (!def)
	def = &dummy;
    NamedList* generalListener = s_cfg.getSection("listener general");
    if (generalListener) {
	bool enabled = generalListener->getBoolValue("enable",true);
	m_endpoint->setupUdpTransport("general",enabled,*generalListener,*def);
    }
    else if (general)
	m_endpoint->setupUdpTransport("general",true,*general,*def);
    else
	m_endpoint->setupUdpTransport("general",true,*def);
    // Setup listeners
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* nl = s_cfg.getSection(i);
	String name = nl ? nl->c_str() : "";
	if (!name.startSkip("listener ",false))
	    continue;
	name.trimBlanks();
	if (!name || name == YSTRING("general"))
	    continue;
	const String& type = (*nl)[YSTRING("type")];
	int proto = ProtocolHolder::lookupProtoAny(type);
	if (proto == ProtocolHolder::Unknown) {
	    proto = ProtocolHolder::Udp;
	    Debug(this,DebugNote,"Invalid listener type '%s' in section '%s': defaults to %s",
		type.c_str(),nl->c_str(),ProtocolHolder::lookupProtoName(proto,false));
	}
	bool enabled = nl->getBoolValue(YSTRING("enable"),true);
	switch (proto) {
	    case ProtocolHolder::Udp:
		m_endpoint->cancelListener(name,"Type changed");
		m_endpoint->setupUdpTransport(name,enabled,*nl,*def);
		break;
	    case ProtocolHolder::Tcp:
	    case ProtocolHolder::Tls:
		m_endpoint->setupUdpTransport(name,false,NamedList::empty(),
		    NamedList::empty(),"Type changed");
		m_endpoint->setupListener(proto,name,enabled,*nl);
		break;
	    default:
		if (enabled)
		    Debug(this,DebugNote,"Unknown listener type '%s' in section '%s'",
			type.c_str(),nl->c_str());
	}
    }
    // Remove deleted listeners
    m_endpoint->initializing(false);
    // Everything set: update default udp transport
    m_endpoint->updateDefUdpTransport();
}

void SIPDriver::genUpdate(Message& msg)
{
    DDebug(this,DebugInfo,"fill module.update message");
    Lock l(this);
    if (m_endpoint) {
	msg.setParam("failed_auths",String(m_endpoint->failedAuths()));
	msg.setParam("transaction_timeouts",String(m_endpoint->timedOutTrs()));
	msg.setParam("bye_timeouts",String(m_endpoint->timedOutByes()));
    }
}

bool SIPDriver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    String cmd = s_statusCmd + " " + name();
    String overviewCmd = s_statusCmd + " overview " + name();
    if (partLine == cmd || partLine == overviewCmd) {
	itemComplete(msg.retValue(),YSTRING("accounts"),partWord);
	itemComplete(msg.retValue(),YSTRING("listeners"),partWord);
	itemComplete(msg.retValue(),YSTRING("transports"),partWord);
    }
    String cmdTrans = cmd + " transports";
    String cmdOverViewTrans = overviewCmd + " transports";
    if (partLine == cmdTrans || partLine == cmdOverViewTrans) {
	itemComplete(msg.retValue(),YSTRING("all"),partWord);
	itemComplete(msg.retValue(),YSTRING("udp"),partWord);
	itemComplete(msg.retValue(),YSTRING("tcp"),partWord);
	itemComplete(msg.retValue(),YSTRING("tls"),partWord);
	if (partLine == cmdTrans) {
	    Lock lock(m_endpoint->m_mutex);
	    for (ObjList* o = m_endpoint->m_transports.skipNull(); o; o = o->skipNext()) {
		YateSIPTransport* t = static_cast<YateSIPTransport*>(o->get());
		itemComplete(msg.retValue(),t->toString(),partWord);
	    }
	}
    }
    else 
    	return Driver::commandComplete(msg,partLine,partWord);
    return false;
}

void SIPDriver::msgStatus(Message& msg)
{
    String str = msg.getValue(YSTRING("module"));
    if (str.null() || str.startSkip(name())) {
	str.trimBlanks();
	if (str.null())
	    Module::msgStatus(msg);
	else if (str.startSkip("accounts"))
	    msgStatusAccounts(msg);
	else if (str.startSkip("transports")) {
	    String tmp = str;
	    tmp.trimBlanks().toLower();
	    if (tmp == YSTRING("udp"))
		msgStatusTransports(msg,true,false,false);
	    else if (tmp == YSTRING("tcp"))
		msgStatusTransports(msg,false,true,false);
	    else if (tmp == YSTRING("tls"))
		msgStatusTransports(msg,false,false,true);
	    else if (!tmp || tmp == YSTRING("all"))
		msgStatusTransports(msg,true,true,true);
	    else if (msg.getBoolValue("details",true))
		msgStatusTransport(msg,str);
	}
	else if (str.startSkip("listeners"))
	    msgStatusListener(msg);
    }
}

// Build and dispatch a socket.ssl message
bool SIPDriver::socketSsl(Socket** sock, bool server, const String& context)
{
    Message m("socket.ssl");
    m.addParam("module",name());
    m.addParam("server",String::boolText(server));
    m.addParam("context",context,false);
    if (!server) {
	Lock lock(s_globalMutex);
	m.addParam("certificate",s_sslCertFile,false);
	m.addParam("key",s_sslKeyFile,false);
    }
    if (sock && *sock) {
	RefObjectProxy* p = new RefObjectProxy(sock);
	m.userData(p);
	TelEngine::destruct(p);
    }
    else
        m.addParam("test",String::boolText(true));
    return Engine::dispatch(m);
}

// Add accounts status
void SIPDriver::msgStatusAccounts(Message& msg)
{
    msg.retValue().clear();
    msg.retValue() << "module=" << name();
    msg.retValue() << ",protocol=SIP";
    msg.retValue() << ",format=Username|Status;";
    msg.retValue() << "accounts=" << s_lines.count();
    if (!msg.getBoolValue("details",true)) {
	msg.retValue() << "\r\n";
	return;
    }
    String accounts = "";
    for (ObjList* o = s_lines.skipNull(); o; o = o->skipNext()) {
	YateSIPLine* line = static_cast<YateSIPLine*>(o->get());
	accounts.append(line->c_str(),",") << "=";
	accounts.append(line->getUserName()) << "|";
	accounts << (line->valid() ? "online" : "offline");
    }
    msg.retValue().append(accounts,";"); 
    msg.retValue() << "\r\n";
}

// Add transports status
void SIPDriver::msgStatusTransports(Message& msg, bool showUdp, bool showTcp, bool showTls)
{
    msg.retValue().clear();
    msg.retValue() << "module=" << name();
    msg.retValue() << ",protocol=SIP";
    YateSIPUDPTransport* def = m_endpoint ? m_endpoint->defTransport() : 0;
    msg.retValue() << ",udp_default=" << (def ? def->toString() : String::empty());
    TelEngine::destruct(def);
    msg.retValue() << ",format=Proto|Status|Local|Remote|Outgoing|Reason;";
    String buf;
    unsigned int n = 0;
    if (m_endpoint) {
	Lock lock(m_endpoint->m_mutex);
	bool details = msg.getBoolValue("details",true);
	for (ObjList* o = m_endpoint->m_transports.skipNull(); o; o = o->skipNext()) {
	    YateSIPTransport* t = static_cast<YateSIPTransport*>(o->get());
	    YateSIPTCPTransport* tcp = t->tcpTransport();
	    if (!tcp) {
		if (!showUdp)
		    continue;
	    }
	    else if (!tcp->tls()) {
		if (!showTcp)
		    continue;
	    }
	    else if (!showTls)
		continue;
	    n++;
	    if (!details)
		continue;
	    Lock lck(t);
	    buf.append(String(n),",") << "=" << t->protoName() << "|";
	    buf << YateSIPTransport::statusName(t->status()) << "|";
	    buf << t->local().host() << ":" << t->local().port() << "|";
	    if (tcp) {
		buf << t->remote().host() << ":" << t->remote().port() << "|";
		buf << String::boolText(tcp->outgoing());
	    }
	    else
		buf << "|";
	    buf << "|" << t->m_reason;
	}
    }
    msg.retValue() << "transports=" << n;
    msg.retValue().append(buf,";"); 
    msg.retValue() << "\r\n";
}

// Add listeners status
void SIPDriver::msgStatusListener(Message& msg)
{
    msg.retValue().clear();
    msg.retValue() << "module=" << name();
    msg.retValue() << ",protocol=SIP";
    msg.retValue() << ",format=Proto|Address|Status|Reason;";
    String buf;
    unsigned int n = 0;
    if (m_endpoint) {
	bool details = msg.getBoolValue("details",true);
	Lock lock(m_endpoint->m_mutex);
	for (ObjList* o = m_endpoint->m_transports.skipNull(); o; o = o->skipNext()) {
	    YateSIPTransport* t = static_cast<YateSIPTransport*>(o->get());
	    YateSIPUDPTransport* udp = t->udpTransport();
	    if (!udp)
		continue;
	    n++;
	    if (!details)
		continue;
	    Lock lck(udp);
	    buf.append(udp->toString(),",") << "=" << udp->protoName() <<"|";
	    buf << udp->local().host() << ":" << udp->local().port() << "|";
	    buf << (udp->status() == YateSIPTransport::Connected ? "Listening|" : "Idle|");
	    buf << udp->m_reason;
	}
	if (details) {
	    for (ObjList* o = m_endpoint->m_listeners.skipNull(); o; o = o->skipNext()) {
		YateSIPTCPListener* l = static_cast<YateSIPTCPListener*>(o->get());
		n++;
		buf.append(l->toString(),",") << "=";
		buf << l->protoName() << "|";
		Lock lck(l->m_mutex);
		buf << l->address() << ":" << l->port() << "|";
		buf << (l->listening() ? "Listening|" : "Idle|");
		buf << l->m_reason;
	    }
	}
	else
	    n += m_endpoint->m_listeners.count();
    }
    msg.retValue() << "listeners=" << n;
    msg.retValue().append(buf,";"); 
    msg.retValue() << "\r\n";
}

// Add transport status
void SIPDriver::msgStatusTransport(Message& msg, const String& id)
{
    msg.retValue().clear();
    msg.retValue() << "module=" << name();
    msg.retValue() << ",protocol=SIP;";
    String tmp = id;
    tmp.trimBlanks();
    YateSIPTransport* t = m_endpoint ? m_endpoint->findTransport(tmp) : 0;
    if (t) {
	YateSIPTCPTransport* tcp = t->tcpTransport();
	t->lock();
	msg.retValue() << "name=" << t->toString();
	msg.retValue() << ",protocol=" << t->protoName();
	msg.retValue() << ",status=" << YateSIPTransport::statusName(t->status());
	msg.retValue() << ",statustime=" << (msg.msgTime().sec() - t->m_statusChgTime);
	msg.retValue() << ",local=" << t->local().host() << ":" << t->local().port();
	if (tcp) {
	    msg.retValue() << ",remote=" << t->remote().host() << ":" << t->remote().port();
	    msg.retValue() << ",outgoing=" << String::boolText(tcp->outgoing());
	}
	String lines;
	for (ObjList* ol = s_lines.skipNull(); ol; ol = ol->skipNext()) {
	    YateSIPLine* line = static_cast<YateSIPLine*>(ol->get());
	    if (line->isTransport(t))
		lines.append(line->toString(),",");
	}
	msg.retValue() << ",lines=" << lines;
	msg.retValue() << ",references=" << (t->refcount() - 1);
	msg.retValue() << ",reason=" << t->m_reason;
	t->unlock();
	TelEngine::destruct(t);
    }
    msg.retValue() << "\r\n";
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
