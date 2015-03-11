/**
 * ysockschan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SOCKS channel
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
#include <stdlib.h>

using namespace TelEngine;
namespace { // anonymous

class SOCKSEndpointDef;                  // A SOCKS endpoint definition
class SOCKSPacket;                       // A SOCKS protocol packet
class SOCKSConn;                         // SOCKS TCP connection
class SOCKSListener;                     // A socket listener
class SOCKSEngine;                       // The SOCKS engine

class YSocksEngine;
class YSocksWrapper;                     // A link between a data source and/or
                                         //  consumer and a SOCKS connection
class YSocksWrapperWorker;               // Worker thread for an YSocksWrapper
class YSocksSource;                      // A data source
class YSocksConsumer;                    // A data consumer
class YSocksListenerThread;              // A socket listener thread
class YSocksProcessThread;               // A connection processor thread
class YSocksConnectThread;               // A connect thread

class YSocksPlugin;

/*
    SOCKS packet formats:

    AuthMethods - RFC 1928 Section 3
	|VER | NMETHODS | METHODS  |
	| 1  |    1     | 1 to 255 |
    AuthReply - RFC 1928 Section 3
	|VER | METHOD |
        | 1  |    1   |
    UnamePwdRequest - RFC 1929 Section 2
	|VER | ULEN |  UNAME   | PLEN |  PASSWD  |
	| 1  |  1   | 1 to 255 |  1   | 1 to 255 |
    UnamePwdReply - RFC 1929 Section 2
	|VER | STATUS |
	| 1  |  1     |
    Request - RFC 1928 Section 4
        |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
        | 1  |  1  | X'00' |  1   | Variable |    2     |
    Reply - RFC 1928 Section 6
        |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
        | 1  |  1  | X'00' |  1   | Variable |    2     |
    DST.ADDR and BND.ADDR - RFC 1928 Section 5
	For Domain type, the first byte is the field length
*/

/**
 * This class holds data describing a SOCKS endpoint such as
 *  type, address, port, authentication
 * @short A SOCKS endpoint definition
 */
class SOCKSEndpointDef : public RefObject
{
public:
    /**
     * Constructor
     * @param name Endpoint name (id)
     * @param proxy True if this is a proxy, false if it is a server
     * @param address The address used by the endpoint
     * @param port The port used by the endpoint
     * @param external External (public) address of the endpoint
     * @param uname Username used to authenticate
     * @param pwd Username used to authenticate
     */
    SOCKSEndpointDef(const char* name, bool proxy, const char* address, int port,
	const char* external = 0, const char* uname = 0, const char* pwd = 0);

    /**
     * Constructor
     * @param params The list of parameters
     */
    SOCKSEndpointDef(NamedList& params);

    /**
     * Get the endpoint definition name (id)
     * @return The endpoint definition name
     */
    virtual const String& toString() const;

    /**
     * Check if this is a proxy endpoint
     * @return True if this is a proxy endpoint
     */
    inline bool proxy() const
	{ return m_proxy; }

    /**
     * Get the endpoint definition name (id)
     * @return The endpoint definition name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Get the address used by this endpoint
     * @return The address used by this endpoint
     */
    inline const String& address() const
	{ return m_address; }

    /**
     * Get the external (public) address used by this endpoint (when applicable)
     * @return The public address used by this endpoint
     */
    inline const String& externalAddr() const
	{ return m_externalAddr; }

    /**
     * Get the port used by this endpoint
     * @return The port used by this endpoint
     */
    inline int port() const
	{ return m_port; }

    /**
     * Get the port used by this endpoint
     * @return The port used by this endpoint
     */
    inline bool authRequired() const
	{ return m_authRequired; }

    /**
     * Get the username used to authenticate
     * @return The username used to authenticate
     */
    inline const String& username() const
	{ return m_username; }

    /**
     * Get the password used to authenticate
     * @return The password used to authenticate
     */
    inline const String& password() const
	{ return m_password; }

private:
    bool m_proxy;
    String m_name;
    String m_address;
    String m_externalAddr;
    int m_port;
    bool m_authRequired;
    String m_username;
    String m_password;
};

/**
 * This class holds a packet sent or received during SOCKS negotiation
 * @short A SOCKS protocol packet
 */
class SOCKSPacket : public GenObject
{
    friend class SOCKSEngine;
public:
    /**
     * Message type enumeration
     */
    enum Type {
	AuthMethods = 1,
	AuthReply,
	UnamePwdRequest,
	UnamePwdReply,
	Request,
	Reply,
	Unknown
    };

    /**
     * Command type enumeration - RFC 1928, section 4
     */
    enum CmdType {
	Connect      = 0x01,
	Bind         = 0x02,
	UdpAssociate = 0x03,
	CmdUnknown
    };

    /**
     * Address type enumeration - RFC 1928, section 4
     */
    enum AddrType {
	IPv4      = 0x01,
	Domain    = 0x03,
	IPv6      = 0x04,
	AddrUnknown
    };

    /**
     * Authentication methods enumeration - RFC 1928, section 3
     * 0x03 - 0x7f: IANA assigned
     * 0x80 - 0xfe: Private methods
     */
    enum AuthMethod {
	AuthNone  = 0x00,                // Authentication not required
	GSSAPI    = 0x01,                // GSSAPI
	UnamePwd  = 0x02,                // Username/password
	NotAuth   = 0xff                 // Not acceptable
    };

    /**
     * Error enumeration (usually received with Reply) - RFC 1928 Section 6
     */
    enum Error {
	EOk                = 0x00,       // Succeeded
        EFailure           = 0x01,       // General SOCKS server failure
        ENotAllowed        = 0x02,       // Connection not allowed by ruleset
        ENoConn            = 0x03,       // Network unreachable
        EHostGone          = 0x04,       // Host unreachable
        EConnRefused       = 0x05,       // Connection refused
        ETimeout           = 0x06,       // TTL expired
        EUnsuppCmd         = 0x07,       // Command not supported
        EUnsuppAddrType    = 0x08,       // Address type not supported
    };

    /**
     * Parser result enumeration
     */
    enum ParseResult {
	ParseOk,
	ParseError,
	ParseIncomplete
    };

    /**
     * Constructor
     * @param t The message type
     * @param conn The connection sending or receiving this packet
     */
    inline SOCKSPacket(Type t, SOCKSConn* conn)
	: m_type(t), m_conn(conn)
	{}

    /**
     * Get the message type
     * @return The message type
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the message name
     * @return The message name
     */
    inline const char* msgName() const
	{ return token(m_type,s_msgName); }

    /**
     * Get the connection sending or receiving this packet
     * @return The connection sending or receiving this packet
     */
    inline SOCKSConn* conn() const
	{ return m_conn; }

    /**
     * Parse received data
     * @param buf The buffer
     * @param len The buffer length
     * @return The result as enumeration
     */
    ParseResult parse(unsigned char* buf, unsigned int len);

    /**
     * Build a string with the message content for debug purposes
     * @param buf The destination string
     * @param extended True to add names and binary packet representation
     */
    void toString(String& buf, bool extended) const;

    /**
     * Build a SOCKS request/reply message
     * @param conn The connection sending this packet
     * @param request True if this is a request, false if this is a reply
     * @param cmdRsp CMD/RSP value
     * @param addrType Address type as enumeration
     * @param addr The address
     * @param port The port
     * @return Valid SOCKSPacket pointer on success
     */
    static SOCKSPacket* buildSocks(SOCKSConn* conn, bool request,
	unsigned char cmdRsp, unsigned char addrType, const String& addr, int port);

    /**
     * Build an auth methods message
     * @param conn The connection sending this packet
     * @param methods The list of methods
     * @param count The number of methods in the list (must be at least 1)
     * @return Valid SOCKSPacket pointer on success
     */
    static SOCKSPacket* buildAuthMethods(SOCKSConn* conn, const void* methods,
	unsigned char count);

    /**
     * Build an auth reply message
     * @param conn The connection sending this packet
     * @param method The method
     * @return Valid SOCKSPacket pointer on success
     */
    static SOCKSPacket* buildAuthReply(SOCKSConn* conn, unsigned char method);

    /**
     * Build an username/password auth request
     * @param conn The connection sending this packet
     * @param uname The username (length must be between 0 and 255)
     * @param pwd The password (length must be between 0 and 255)
     * @return Valid SOCKSPacket pointer on success
     */
    static SOCKSPacket* buildUnamePwdReq(SOCKSConn* conn, const String& uname, const String& pwd);

    /**
     * Build an username/password auth reply
     * @param conn The connection sending this packet
     * @param ok The result (0 for success)
     * @return Valid SOCKSPacket pointer on success
     */
    static SOCKSPacket* buildUnamePwdReply(SOCKSConn* conn, unsigned char ok = 0);

    /**
     * Get a token from a dictionary
     * @param what The token's id
     * @param dict The dictionary
     * @param def Default value to return if not found
     * @return The message name or the default value if not found
     */
    static inline const char* token(int what, TokenDict* dict, const char* def = "Unknown")
	{ return TelEngine::lookup(what,dict,def); }

    /**
     * Message names
     */
    static TokenDict s_msgName[];

    /**
     * Auth method names
     */
    static TokenDict s_authName[];

    /**
     * Command names
     */
    static TokenDict s_cmdName[];

    /**
     * Address type names
     */
    static TokenDict s_addrTypeName[];

    /**
     * Reply texts
     */
    static TokenDict s_replyText[];

    // Data used when encoding/decoding and when printed to output
    unsigned char m_cmdRsp;
    unsigned char m_addrType;
    String m_addr;
    int m_port;
    DataBlock m_auth;
    String m_username;
    String m_password;

private:
    Type m_type;
    DataBlock m_buffer;                  // The message buffer
    SOCKSConn* m_conn;
};

/**
 * This class holds a TCP connection used to transfer SOCKS packets
 *  and user data
 * @short A SOCKS TCP connection
 */
class SOCKSConn : public RefObject, public Mutex
{
    friend class SOCKSEngine;
public:
    /**
     * Enumerate connection status
     */
    enum Status {
	Idle = 1,                        // Waiting for a message to be sent
	Connecting,                      // Outgoing connection is connecting the socket
	WaitMsg,                         // The connection is waiting for a SOCKS message
	Running,                         // SOCKS negotiation completed: data can be transferred
	Terminated,                      // Terminated: no traffic allowed
    };

    /**
     * Enumerate data direction
     */
    enum Direction {
	None  = 0x00,                    // None
	Send  = 0x01,                    // Send only
	Recv  = 0x02,                    // Receive only
	Both  = 0x03,                    // Send/receive
    };

    /**
     * Constructor. Build an incoming connection
     * @param engine The SOCKS engine used for debug purposes
     * @param sock The connected socket
     * @param epDef Endpoint definion used by this connection
     */
    SOCKSConn(SOCKSEngine* engine, Socket* sock, SOCKSEndpointDef* epDef);

    /**
     * Constructor. Build an outgoing connection
     * @param engine The SOCKS engine used for debug purposes
     * @param epDef Endpoint definion used by this connection
     * @param cmd Command to send in request
     * @param addrType Address type to send in request
     * @param addr The address to send in request
     * @param port The port to send in request
     */
    SOCKSConn(SOCKSEngine* engine, SOCKSEndpointDef* epDef, unsigned char cmd,
	unsigned char addrType, const String& addr, int port);

    /**
     * Destructor. Terminate the socket
     */
    ~SOCKSConn();

    /**
     * Get the connection status
     * @return The connection status
     */
    inline Status status() const
	{ return m_status; }

    /**
     * Get connection direction
     * @return True if the connection is an outgoing one
     */
    inline bool outgoing() const
	{ return m_outgoing; }

    /**
     * Check if this connection can transfer data (SOCKS negotiation terminated)
     * @return True if this connection can transfer data
     */
    inline bool canTransferData() const
	{ return m_status == Running; }

    /**
     * Check if the socket is valid
     */
    inline bool valid() const
	{ return m_socket && m_socket->valid(); }

    /**
     * Get this connection's engine
     * @return This connection's engine
     */
    inline SOCKSEngine* engine() const
	{ return m_engine; }

    /**
     * Get this connection's endpoint definition
     * @return This connection's endpoint definition
     */
    inline const SOCKSEndpointDef* epDef() const
	{ return m_epDef; }

    /**
     * Get the CMD value stored by this connection
     * @return The CMD value stored by this connection
     */
    inline unsigned char reqCmd() const
	{ return m_reqCmd; }

    /**
     * Get the request address type stored by this connection
     * @return The request address type stored by this connection
     */
    inline unsigned char reqAddrType() const
	{ return m_reqAddrType; }

    /**
     * Get the request address stored by this connection
     * @return The request address stored by this connection
     */
    inline const String& reqAddr() const
	{ return m_reqAddr; }

    /**
     * Get the request port stored by this connection
     * @return The request port stored by this connection
     */
    inline int reqPort() const
	{ return m_reqPort; }

    /**
     * Get the RSP value stored by this connection
     * @return The RSP value stored by this connection
     */
    inline unsigned char replyRsp() const
	{ return m_replyRsp; }

    /**
     * Get the reply address type stored by this connection
     * @return The reply address type stored by this connection
     */
    inline unsigned char replyAddrType() const
	{ return m_replyAddrType; }

    /**
     * Get the reply address stored by this connection
     * @return The reply address stored by this connection
     */
    inline const String& replyAddr() const
	{ return m_replyAddr; }

    /**
     * Get the reply port stored by this connection
     * @return The reply port stored by this connection
     */
    inline int replyPort() const
	{ return m_replyPort; }

    /**
     * Get this connection's id
     * @return This connection's id
     */
    virtual const String& toString() const;

    /**
     * Process connection while waiting for a message
     * Read data from socket and process the received message
     * @param now The current time
     * @param error Flag set on exit to indicate failure
     * @param timeout Failure reason: timeout or invalid message
     * @return Valid SOCKSPacket pointer when a valid packet is received.
     *  Check 'error' if 0 is returned
     */
    SOCKSPacket* processSocks(const Time& now, bool& error, bool& timeout);

    /**
     * Build and send a SOCKS reply
     * @param addrType Address type to send in reply
     * @param addr The address to send in reply
     * @param port The port to send in reply
     * @param rsp Reply value (0 for success)
     * @return True on success
     */
    bool sendReply(unsigned char addrType, const String& addr, int port,
	unsigned char rsp = SOCKSPacket::EOk);

    /**
     * Enable data transfer after succesfully negotiating SOCKS
     * @return True on success
     */
    bool enableDataTransfer();

    /**
     * Get connection address
     * @param local True to get local address, false to get the remote one
     * @param addr Destination
     * @return True on success (valid socket)
     */
    inline bool getAddr(bool local, SocketAddr& addr) {
	    if (!m_socket)
		return false;
	    return local ? m_socket->getSockName(addr) : m_socket->getPeerName(addr);
	}

    /**
     * Set connecting state (outgoing only)
     */
    void setConnecting();

    /**
     * Set socket (outgoing only)
     * @param sock The socket. It will be consumed
     * @param sendAuthMeth True to send auth methods after succesfully connected
     * @return True on success
     */
    bool setSocket(Socket* sock, bool sendAuthMeth = true);

    /**
     * Terminate and delete the socket
     */
    void terminate();

    /**
     * Send data through the socket
     * @param buf The buffer to send
     * @param len The number of bytes to send. On succesfully return it
     *  will contain the number of bytes actually sent
     * @return True on success
     */
    bool send(const void* buf, unsigned int& len);

    /**
     * Read data from socket
     * @param buf The destination buffer
     * @param len The number of bytes to read. On succesfully return it
     *  will contain the number of bytes actually read
     * @return True on success
     */
    bool recv(void* buf, unsigned int& len);

    /**
     * Get a status name
     * @param stat The status to find
     * @param def Default value to return if not found
     * @return The status name or the default value if not found
     */
    static inline const char* statusName(int stat, const char* def = "Unknown")
	{ return TelEngine::lookup(stat,s_statusName,def); }

    /**
     * Connect a socket
     * @param engine The engine owning the object requesting connect (used for debug)
     * @param address Address to connect to
     * @param port Port to connect to
     * @param connToutMs Connect timeout in milliseconds
     * @param error Error code on failure
     * @param timeout Connection timeout flag
     * @return Connected Socket pointer, 0 on failure
     */
    static Socket* connect(SOCKSEngine* engine, const String& address, int port,
	unsigned int connToutMs, int& error, bool& timeout);

    /**
     * Status names
     */
    static TokenDict s_statusName[];

protected:
    /**
     * Build and send a SOCKS request
     * @return True on success
     */
    inline bool sendRequest() {
	    return sendProtocolMsg(
		SOCKSPacket::buildSocks(this,true,m_reqCmd,m_reqAddrType,m_reqAddr,m_reqPort),
		false,SOCKSPacket::Reply);
	}

    /**
     * Build and send an auth methods message
     * @return True on success
     */
    bool sendAuthMethods();

    /**
     * Build and send an auth reply message
     * @param method The method to send
     * @return True on success
     */
    bool sendAuthReply(unsigned char method);

    /**
     * Build and send an username/password request
     * @return True on success
     */
    inline bool sendUnamePwd() {
	    if (!m_epDef)
		return false;
	    return sendProtocolMsg(
		SOCKSPacket::buildUnamePwdReq(this,m_epDef->username(),m_epDef->password()),
		false,SOCKSPacket::UnamePwdReply);
	}

    /**
     * Build and send an username/password reply
     * @param ok Reply code (0 for success)
     * @return True on success
     */
    inline bool sendUnamePwdReply(unsigned char ok = 0) {
	    return sendProtocolMsg(SOCKSPacket::buildUnamePwdReply(this,ok),
		0 != ok,SOCKSPacket::Request);
	}

    /**
     * Terminate the socket. Release memory
     */
    virtual void destroyed();

    /**
     * Send protocol messages through the socket. Change connection status on success
     * @param packet The message to send
     * @param terminate True to indicate connection termination
     * @param wait The message to wait for
     * @return True on success
     */
    bool sendProtocolMsg(SOCKSPacket* packet, bool terminate,
	SOCKSPacket::Type wait = SOCKSPacket::Unknown);

    /**
     * Build connection id from socket local and remote data
     */
    void buildId();

    /**
     * Change connection status
     * @param stat The new connection status
     * @return True if connection status changed
     */
    bool changeStatus(Status stat);

    /**
     * Set/reset the timeout when negotiating SOCKS
     * @param now The starting point (0 to reset)
     * @param auth True if authenticating, false if negotiating a request
     */
    void setSocksTimeout(u_int64_t now = Time::msecNow(), bool auth = true);

private:
    // Message processors. Return false to terminate the connection
    bool processAuthMethods(const SOCKSPacket& packet);
    bool processAuthReply(const SOCKSPacket& packet);
    bool processUnamePwdRequest(const SOCKSPacket& packet);
    bool processUnamePwdReply(const SOCKSPacket& packet);
    bool processRequest(const SOCKSPacket& packet);
    bool processReply(const SOCKSPacket& packet);

    String m_id;
    Status m_status;
    bool m_outgoing;
    SOCKSPacket* m_waitMsg;              // Indicates the next packet to receive
    SOCKSEngine* m_engine;
    Socket* m_socket;
    int m_sendError;                     // Avoid repeating non fatal send error messages
    u_int64_t m_socksTimeoutMs;          // The timeout value when negotiating SOCKS
    SOCKSEndpointDef* m_epDef;
    // SOCKS request data
    unsigned char m_reqCmd;
    unsigned char m_reqAddrType;
    String m_reqAddr;
    int m_reqPort;
    // SOCKS reply data
    unsigned char m_replyRsp;
    unsigned char m_replyAddrType;
    String m_replyAddr;
    int m_replyPort;
};


/**
 * Socket listener. Notify the engine when an incoming connection is created
 * @short A socket listener
 */
class SOCKSListener
{
    friend class SOCKSEngine;            // Reset the engine when stopped
public:
    enum Status {
	Created,
	Initializing,
	Bind,
	Listening,
	Accepting,
	Terminated
    };

    /**
     * Constructor
     * @param engine The engine using this listener's services
     * @param epDef The endpoint definition used by the listener
     * @param backlog Maximum length of the queue of pending connections, 0 for system maximum
     */
    SOCKSListener(SOCKSEngine* engine, SOCKSEndpointDef* epDef, unsigned int backlog = 5);

    /**
     * Destructor
     */
    virtual ~SOCKSListener();

    /**
     * Get the endpoint definition used by this listener
     * @return The endpoint definition used by this listener
     */
    inline SOCKSEndpointDef* epDef()
	{ return m_epDef; }

    /**
     * Get the engine using this listener's services
     * @return The engine using this listener's services
     */
    inline SOCKSEngine* engine() const
	{ return m_engine; }

    /**
     * Get socket address
     * @param addr Destination address
     * @return True on success (valid socket)
     */
    inline bool getAddr(SocketAddr& addr)
	{ return m_socket && m_socket->getSockName(addr); }

    /**
     * Get the listener status
     * @return The listener status
     */
    inline int status() const
	{ return m_status; }

    /**
     * Create and bind the socket
     * @return True on success
     */
    bool init();

    /**
     * Start listening
     * @return True on success
     */
    bool startListen();

    /**
     * Check for incoming connections
     * @param addr Address to be filled when a connection was created
     * @return Valid Socket pointer if an incoming connection was created
     */
    Socket* accept(SocketAddr& addr);

    /**
     * Terminate the socket
     */
    void terminate();

    /**
     * Init, start listening and call accept() in a loop.
     * Notify the engine when a connection is created
     */
    virtual void run();

    /**
     * Stop this listener
     * Thread descendants should re-implement this method to cancel the thread
     * @param hard True to cancel a descendant thread in the hard way
     */
    virtual void stop(bool hard);

    /**
     * Listener status name
     */
    static TokenDict s_statusName[];

protected:
    SOCKSEndpointDef* m_epDef;
    String m_id;
    unsigned int m_backlog;
    Socket* m_socket;
    bool m_listenError;
    SOCKSEngine* m_engine;
    Status m_status;
};

/**
 * This class holds the socket listeners, endpoint descriptions, and connection
 *  negotiating the SOCKS protocol
 * @short SOCKS protocol processor
 */
class SOCKSEngine : public DebugEnabler, public Mutex
{
public:
    /**
     * Constructor
     * @param params The engine's parameter list
     */
    SOCKSEngine(NamedList& params);

    /**
     * Destructor
     */
    virtual ~SOCKSEngine()
	{ }

    /**
     * Check if the engine is exiting
     * @return True if the engine is exiting
     */
    inline bool exiting() const
	{ return m_exiting; }

    /**
     * Set the exiting flag
     */
    inline void setExiting()
	{ m_exiting = true; }

    /**
     * Get the timeout interval of a connection waiting for a message,
     *  before the SOCKS request was sent/received
     * @return The timeout interval in miljiseconds
     */
    inline u_int64_t waitMsgAuthInterval() const
	{ return m_waitMsgAuthInterval; }

    /**
     * Get the timeout interval of a connection waiting for a message,
     *  after the SOCKS request was sent/received
     * @return The timeout interval in miljiseconds
     */
    inline u_int64_t waitMsgReplyInterval() const
	{ return m_waitMsgReplyInterval; }

    /**
     * Retrieve the connect timeout interval
     * @return Connect timeout interval in milliseconds
     */
    inline unsigned int connectTimeout() const
	{ return m_connectToutMs; }

    /**
     * Initialize engine's parameters
     * @param params The engine's parameter list
     */
    virtual void initialize(NamedList& params);

    /**
     * Cleanup the engine. Stop listeners
     */
    virtual void cleanup();

    /**
     * Connect a connection, increase its reference counter, add it to the
     *  list and start negotisting SOCKS when connected
     * @param conn The connection
     * @return True on success
     */
    virtual bool addConnection(SOCKSConn* conn);

    /**
     * Incoming connection notification. Build a connection and add
     *  it to the incoming connections list
     * @param listener The notifier
     * @param sock The created socket
     * @param addr The remote address requesting the connection
     * @return True if the connection was accepted
     */
    virtual bool incomingConnection(SOCKSListener* listener, Socket* sock,
	SocketAddr& addr);

    /**
     * Process connections negotiating SOCKS
     * @return True if at least one connection processed a message
     */
    bool process();

    /**
     * Process a connection negotiating the SOCKS protocol.
     * @param conn The connection to process
     * @param now The time of the call
     * @return True if the connection processed a message
     */
    virtual bool processSocksConnection(SOCKSConn* conn, const Time& now);

    /**
     * Send a packet through a connection
     * The packet is consumed (the pointer will be deleted)
     * @param packet The packet to send
     * @return True on success
     */
    virtual bool sendPacket(SOCKSPacket* packet);

    /**
     * Print a debug message when a connections received a packet
     * @param packet The packet to print
     */
    virtual void receivedPacket(const SOCKSPacket& packet);

    /**
     * Add an endpoint definition. Replace an existing one with the same name.
     * The pointer will be owned by the engine (append to list without
     *  increasing its reference counter)
     * @param epDef The endpoint definition to add
     */
    void addEpDef(SOCKSEndpointDef* epDef);

    /**
     * Remove an endpoint definition
     * @param name The endpoint definition name
     */
    void removeEpDef(const String& name);

    /**
     * Find an endpoint definition by its name
     * @param name The endpoint definition name
     * @return Referenced SOCKSEndpointDef pointer or 0 if not found
     */
    SOCKSEndpointDef* findEpDef(const String& name);

    /**
     * Add a socket listener. The engine doesn't own the pointer
     * @param listener The listener to add
     */
    virtual void addListener(SOCKSListener* listener);

    /**
     * Remove a socket listener
     * @param listener The listener to add
     */
    virtual void removeListener(SOCKSListener* listener);

    /**
     * Check if a listener exists
     * @param listener The listener to find
     * @param status Set to listener status on exit, if found
     * @return False if not found
     */
    virtual bool hasListener(SOCKSListener* listener, int& status);

    /**
     * Stop socket listeners
     * @param wait True to wait for all listeners to remove themselves from the list
     * @param hard Parameter to be passed to listener's stop() method
     */
    virtual void stopListeners(bool wait, bool hard);

    /**
     * Remove and delete a connection from SOCKS list
     * @param conn The connection to remove
     * @param reason The reason
     */
    virtual void removeSocksConn(SOCKSConn* conn, const char* reason);

    /**
     * Terminate and delete a socket
     * @param sock The socket to terminate
     */
    static void destroySocket(Socket*& sock);

    /**
     * Retrieve connect timeout from parameters
     * @param params Parameter list
     * @param defVal Default value to return if missing/invalid
     * @return Connect timeout value
     */
    static inline unsigned int getConnectTimeout(const NamedList& params,
	unsigned int defVal) {
	    unsigned int val = params.getIntValue(YSTRING("connect_timeout"),defVal,0,120000);
	    if (!val || val >= 1000)
		return val;
	    return 1000;
	}

protected:
    /**
     * Process a SOCKS request
     * @param packet The received packet
     * @param conn The receiving connection
     * @return SOCKSPacket error (EOk to indicate success)
     */
    virtual SOCKSPacket::Error processSOCKSRequest(const SOCKSPacket& packet, SOCKSConn* conn);

    /**
     * Default SOCKS request. Sends a reply with 'rsp' non 0 (error).
     * This method is called by the engine if request processor returns an error
     * @param conn The receiving connection
     * @param err Error to send
     */
    virtual void defaultRequestHandler(SOCKSConn* conn,
	SOCKSPacket::Error err = SOCKSPacket::EUnsuppCmd);

    /**
     * Process a SOCKS reply
     * @param packet The received packet
     * @param conn The receiving connection
     * @return False to remove the connection from the list
     */
    virtual bool processSOCKSReply(const SOCKSPacket& packet, SOCKSConn* conn);

    /**
     * Notify descentants when an error occured in a processing connection.
     * The connection will be removed from list after notification
     * @param conn The connection
     * @param timeout True if the connection timed out
     */
    virtual void socksConnError(SOCKSConn* conn, bool timeout)
	{}

    bool m_exiting;
    u_int64_t m_waitMsgAuthInterval;
    u_int64_t m_waitMsgReplyInterval;
    unsigned int m_connectToutMs;        // Connect timeout in milliseconds
    bool m_showMsg;                      // Print message on output
    bool m_dumpExtended;                 // Dump names and binary msg if printed
    ObjList m_epDef;                     // The endpoint definition list
    ObjList m_listeners;                 // The list of listeners
    ObjList m_socksConn;                 // The list of connections negotiating the SOCKS protocol
};

// The SOCKS engine
class YSocksEngine : public SOCKSEngine
{
    friend class YSocksPlugin;
public:
    YSocksEngine(NamedList& params);
    virtual void initialize(NamedList& params);
    virtual void cleanup();
    // Find a wrapper with a given DST ADDR/PORT
    // Return a referenced object if found
    YSocksWrapper* findWrapper(bool client, const String& dstAddr, int dstPort);
    // Find a wrapper. Return a referenced object if found
    YSocksWrapper* findWrapper(const String& wID);
    // Find a wrapper with a given connection
    // Return a referenced object if found
    YSocksWrapper* findWrapper(SOCKSConn* conn);
    // Remove a wrapper from list
    void removeWrapper(YSocksWrapper* w, bool delObj);
    // Add a wrapper
    void addWrapper(YSocksWrapper* w);
protected:
    virtual SOCKSPacket::Error processSOCKSRequest(const SOCKSPacket& packet, SOCKSConn* conn);
    virtual bool processSOCKSReply(const SOCKSPacket& packet, SOCKSConn* conn);
    virtual void socksConnError(SOCKSConn* conn, bool timeout);
    inline ObjList& listeners()
	{ return m_listeners; }
private:
    ObjList m_wrappers;
};

// A link between a data source and/or consumer and a SOCKS connection
class YSocksWrapper : public RefObject, public Mutex, public DebugEnabler
{
    friend class YSocksConsumer;
    friend class YSocksSource;
    friend class YSocksWrapperWorker;
public:
    enum State {
	Pending,
	Connecting,
	WaitStart,
	Established,
	Running,
	Terminated
    };
    // Build a wrapper (client if epDef is non 0)
    YSocksWrapper(const char* id, YSocksEngine* engine, CallEndpoint* cp,
	NamedList& params, const char* notify, SOCKSEndpointDef* epDef = 0);
    inline State state() const
	{ return m_state; }
    inline bool client() const
	{ return m_client; }
    inline bool canRecv() const
	{ return 0 != (m_dir & SOCKSConn::Recv); }
    inline bool canSend() const
	{ return m_dir & SOCKSConn::Send; }
    inline const String& media() const
	{ return m_media; }
    inline const String& dstAddr() const
	{ return m_dstAddr; }
    inline const String& notify() const
	{ return m_notify; }
    inline int dstPort() const
	{ return m_dstPort; }
    inline SOCKSConn* conn() const
	{ return m_conn; }
    inline bool autoStart() const
	{ return m_autoStart; }
    inline const String& srvAddr() const
	{ return m_srvAddr; }
    inline int srvPort() const
	{ return m_srvPort; }
    inline unsigned int connectTimeoutInterval() const
	{ return m_connectToutMs; }
    inline YSocksEngine* engine() const
	{ return m_engine; }
    // Connect socket if client
    bool connect();
    void connectTerminated(YSocksConnectThread* th, Socket* sock, int error,
	bool timeout);
    // Client connection got reply
    void connRecvReply();
    // Connection error while negotiating the protocol
    void connError(bool timeout);
    // Set connection with valid request for server wrapper
    bool setConn(SOCKSConn* conn);
    // Build source/consumer
    YSocksSource* getSource();
    YSocksConsumer* getConsumer();
    // Build and start or stop worker thread
    bool startWorker();
    void stopWorker(bool wait);
    // Read data from conn and forward it
    bool recvData();
    // Enable data transfer. Change state, set source/consumer format
    void enableDataTransfer(const char* format = 0);
    // Get the wrapper id
    virtual const String& toString() const;
    // Notify status in chan.notify
    void notify(int stat);
protected:
    // Release memory
    virtual void destroyed();
private:
    State m_state;
    bool m_client;                       // Client or server connection
    int m_dir;                           // Data direction
    bool m_autoStart;                    // Start automatically
    String m_id;                         // Wrapper id
    String m_notify;                     // Channel to notify
    String m_media;                      //
    String m_format;                     //
    CallEndpoint* m_callEp;
    String m_dstAddr;                    // SOCKS request Destination address field
    int m_dstPort;                       // SOCKS request Destination port field
    String m_srvAddr;                    // Server wrapper address
    int m_srvPort;                       // Server wrapper port
    DataBlock m_recvBuffer;
    YSocksEngine* m_engine;
    YSocksSource* m_source;
    YSocksConsumer* m_consumer;
    SOCKSConn* m_conn;
    YSocksWrapperWorker* m_thread;
    // Client connect
    unsigned int m_connectToutMs;        // Connect timeout interval
    YSocksConnectThread* m_connect;      // Connect thread
};

// Worker thread for a wrapper
class YSocksWrapperWorker: public Thread
{
public:
    inline YSocksWrapperWorker(YSocksWrapper* w, Thread::Priority prio = Thread::Normal)
	: Thread("SOCKS Wrapper",prio),
	m_wrapper(w)
	{}
    // Check if the thread should terminate
    inline bool invalid() {
	    return Thread::check(false) || !m_wrapper ||
		m_wrapper->state() == YSocksWrapper::Terminated;
	}
    void run();
private:
    YSocksWrapper* m_wrapper;
};

// Socks data source
class YSocksSource : public DataSource
{
    friend class YSocksWrapper;
public:
    YSocksSource(YSocksWrapper* w);
    inline void busy(bool isBusy)
	{ m_busy = isBusy; }
    inline bool shouldSendEmpty() {
	    if (m_sentEmpty)
		return false;
	    Lock lck(this);
	    m_sentEmpty = (0 != m_consumers.skipNull());
	    return m_sentEmpty;
	}
    inline void resetSendEmpty()
	{ m_sentEmpty = true; }
protected:
    // Remove from wrapper. Release memory
    virtual void destroyed();
private:
    YSocksWrapper* m_wrapper;
    volatile bool m_busy;
    bool m_sentEmpty;
};

// Socks data consumer
class YSocksConsumer : public DataConsumer
{
    friend class YSocksWrapper;
public:
    YSocksConsumer(YSocksWrapper* w);
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
protected:
    // Remove from wrapper. Release memory
    virtual void destroyed();
private:
    YSocksWrapper* m_wrapper;
};

// A socket listener thread
class YSocksListenerThread : public SOCKSListener, public Thread
{
public:
     // @param engine The engine using this listener's services
     // @param address The local address to bind to
     // @param port The local port to bind to
     // @param backlog Maximum length of the queue of pending connections, 0 for system maximum
     // @param prio Thread priority
    inline YSocksListenerThread(SOCKSEngine* engine, SOCKSEndpointDef* proxy,
	unsigned int backlog, Thread::Priority prio = Thread::Normal)
	: SOCKSListener(engine,proxy,backlog),
	  Thread("SOCKSListen",prio)
	{}

    // Add the listener to engine and start it
    inline bool addAndStart() {
	    if (m_engine)
		m_engine->addListener(this);
	    return Thread::startup();
	}

    virtual void run()
	{ SOCKSListener::run(); }

    // Stop this listener
    virtual void stop(bool hard)
	{ Thread::cancel(hard); }
};

// A connection processor thread
class YSocksProcessThread : public Thread
{
public:
    inline YSocksProcessThread(Thread::Priority prio = Thread::Normal)
	: Thread("SOCKSProcess",prio)
	{}
    void run();
};

// A connect thread
class YSocksConnectThread : public Thread
{
public:
    YSocksConnectThread(YSocksWrapper* w, Thread::Priority prio = Thread::Normal);
    virtual void cleanup()
	{ notify(); }
    virtual void run();

protected:
    void notify(Socket* sock = 0, int error = 0, bool timeout = false);
    YSocksEngine* m_engine;
    String m_wrapperId;
    String m_address;
    int m_port;
    unsigned int m_toutIntervalMs;
};

// The plugin
class YSocksPlugin : public Module
{
public:
    enum SocksRelays {
	ChanSocks = Private
    };
    YSocksPlugin();
    virtual ~YSocksPlugin();
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    // 'chan.socks' message handler
    bool handleChanSocks(Message& msg);
    // Uninstall the relays
    bool unload();
    // Build a wrapper id
    inline void buildWrapperId(String& buf) {
	    Lock lock(this);
	    buf << name() << "/" << ++m_wrapperId;
	}
protected:
    // Handle command complete requests
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
private:
    unsigned int m_wrapperId;
    bool m_init;
};


// Local data and functions
INIT_PLUGIN(YSocksPlugin);
static YSocksEngine* s_engine = 0;
static unsigned int s_bufLen = 4096;     // Read buffer length
static int s_minPort = 16384;            // Min port value used to create temporary listeners
static int s_maxPort = 32768;            // Max port value used to create temporary listeners
static Mutex s_srcMutex(true,"YSocksChan::source"); // Protect source/wrapper association

// Data transfer directions
static TokenDict dict_conn_dir[] = {
    { "receive", SOCKSConn::Recv },
    { "send",    SOCKSConn::Send },
    { "bidir",   SOCKSConn::Both },
    { 0, 0 },
};

static String s_statusCmd;
// Status commands handled by this module
static String s_statusCmds[] = {
    "listeners",                         // Show listeners
    ""
};

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}


// The SOCKS protocol version
#define SOCKS_VERSION 0x05

// The USERNAME/PASSWORD authentication version (RFC 1929)
#define UNAMEPWD_VERSION 0x01

// Message names
TokenDict SOCKSPacket::s_msgName[] = {
    { "AuthMethods",       AuthMethods },
    { "AuthReply",         AuthReply },
    { "UnamePwdRequest",   UnamePwdRequest },
    { "UnamePwdReply",     UnamePwdReply },
    { "Request",           Request },
    { "Reply",             Reply },
    { 0, 0 }
};

// Command names
TokenDict SOCKSPacket::s_cmdName[] = {
    { "Connect",       Connect },
    { "Bind",          Bind },
    { "UdpAssociate",  UdpAssociate },
    { 0, 0 }
};

// Address type names
TokenDict SOCKSPacket::s_addrTypeName[] = {
    { "IPv4",    IPv4 },
    { "Domain",  Domain },
    { "IPv6",    IPv6 },
    { 0, 0 }
};

// Auth method names
TokenDict SOCKSPacket::s_authName[] = {
    { "None",               AuthNone },
    { "GSSAPI",             GSSAPI },
    { "Username/Password",  UnamePwd },
    { "NotAuth",            NotAuth },
    { 0, 0 }
};

// Reply texts
TokenDict SOCKSPacket::s_replyText[] = {
    { "Succeeded",                          EOk },
    { "General SOCKS server failure",       EFailure },
    { "Connection not allowed by ruleset",  ENotAllowed },
    { "Network unreachable",                ENoConn },
    { "Host unreachable",                   EHostGone },
    { "Connection refused",                 EConnRefused },
    { "TTL expired",                        ETimeout },
    { "Command not supported",              EUnsuppCmd },
    { "Address type not supported",         EUnsuppAddrType },
    { 0, 0 }
};

// Message names
TokenDict SOCKSConn::s_statusName[] = {
    { "Idle",         Idle },
    { "Connecting",   Connecting },
    { "WaitMsg",      WaitMsg },
    { "Running",      Running },
    { "Terminated",   Terminated },
    { 0, 0 }
};

// Listener status names
TokenDict SOCKSListener::s_statusName[] = {
    { "Created",      Created },
    { "Initializing", Initializing },
    { "Bind",         Bind },
    { "Listening",    Listening },
    { "Accepting",    Accepting },
    { "Terminated",   Terminated },
    { 0, 0 }
};

// Check the version of a SOCKS packet
inline bool validSocksVersion(SOCKSPacket& packet, unsigned char ver)
{
    if (ver == SOCKS_VERSION)
	return true;
    Debug(packet.conn() ? packet.conn()->engine() : 0,DebugNote,
	"SOCKSConn(%s) received message %s with invalid version %u (supported: %u) [%p]",
	packet.conn() ? packet.conn()->toString().c_str() : "",packet.msgName(),
	ver,SOCKS_VERSION,packet.conn());
    return false;
}

// Check the version of a Username/password authentication packet
inline bool validUnamePwdVersion(SOCKSPacket& packet, unsigned char ver)
{
    if (ver == UNAMEPWD_VERSION)
	return true;
    Debug(packet.conn() ? packet.conn()->engine() : 0,DebugNote,
	"SOCKSConn(%s) received message %s with invalid version %u (supported: %u) [%p]",
	packet.conn() ? packet.conn()->toString().c_str() : "",packet.msgName(),
	ver,UNAMEPWD_VERSION,packet.conn());
    return false;
}

// Check the message length
inline bool validSocksMsgLen(SOCKSPacket& packet, unsigned int expected, unsigned char len)
{
    if (expected == len)
	return true;
    Debug(packet.conn() ? packet.conn()->engine() : 0,DebugNote,
	"SOCKSConn(%s) received message %s with invalid length %u (expected: %u) [%p]",
	packet.conn() ? packet.conn()->toString().c_str() : "",packet.msgName(),
	len,expected,packet.conn());
    return false;
}


// Parse received data
SOCKSPacket::ParseResult SOCKSPacket::parse(unsigned char* buf, unsigned int len)
{
    if (!(buf && len))
	return ParseIncomplete;

    m_buffer.append(buf,len);
    unsigned char* d = (unsigned char*)m_buffer.data();
    len = m_buffer.length();
    if (m_type == AuthMethods) {
	if (!validSocksVersion(*this,d[0]))
	    return ParseError;
	if (len < 2 || (len < ((unsigned int)d[1] + 2)))
	    return ParseIncomplete;
	if (!validSocksMsgLen(*this,d[1] + 2,len))
	    return ParseError;
	m_auth.append(d + 2,d[1]);
	return ParseOk;
    }
    if (m_type == AuthReply) {
	if (!validSocksVersion(*this,d[0]))
	    return ParseError;
	if (len < 2)
	    return ParseIncomplete;
	if (!validSocksMsgLen(*this,2,len))
	    return ParseError;
	m_auth.append(d + 1,1);
	return ParseOk;
    }
    if (m_type == UnamePwdRequest) {
	if (!validUnamePwdVersion(*this,d[0]))
	    return ParseError;
	if (len < 4)
	    return ParseIncomplete;
	// Check username
	if (((unsigned int)d[1] + 2) > len)
	    return ParseIncomplete;
	// Check password
	unsigned int pwdLenPos = d[d[1] + 2];
	if (pwdLenPos >= len || d[pwdLenPos] + pwdLenPos > len)
	    return ParseIncomplete;
	if (!validSocksMsgLen(*this,3 + d[1] + d[pwdLenPos],len))
	    return ParseError;
	m_username.assign((char*)d + 2,d[1]);
	m_password.assign((char*)d + pwdLenPos + 1,d[pwdLenPos]);
	return ParseOk;
    }
    if (m_type == UnamePwdReply) {
	if (!validUnamePwdVersion(*this,d[0]))
	    return ParseError;
	if (len < 2)
	    return ParseIncomplete;
	if (!validSocksMsgLen(*this,2,len))
	    return ParseError;
	m_auth.append(d + 1,1);
	return ParseOk;
    }
    if (m_type == Request || m_type == Reply) {
	if (!validSocksVersion(*this,d[0]))
	    return ParseError;
	// Min len: 10
	if (len < 10)
	    return ParseIncomplete;
	m_cmdRsp = d[1];
	// Start check with index 3: address type
	unsigned int domainLen = 0;
	unsigned int expected = 6; // Msg len without address/domain
	m_addrType = d[3];
        d = d + 4;
	if (m_addrType == Domain) {
	    domainLen = *d++;
	    expected += domainLen + 1;
	}
	else if (m_addrType == IPv4)
	    expected += 4;
	else if (m_addrType == IPv6)
	    expected += 16;
	else {
	    Debug(m_conn ? m_conn->engine() : 0,DebugMild,
		"SOCKSConn(%s) received %s with invalid address type %u [%p]",
		m_conn ? m_conn->toString().c_str() : "",msgName(),m_addrType,m_conn);
	    return ParseError;
	}
	// Check len
	if (expected < len)
	    return ParseIncomplete;
	if (!validSocksMsgLen(*this,expected,len))
	    return ParseError;
	// Decode addr
	m_addr = "";
	if (m_addrType == IPv4) {
	    m_addr << d[0] << "." << d[1] << "." << d[2] << "." << d[3];
	    d += 4;
	}
	else if (m_addrType == Domain) {
	    m_addr.assign((const char*)d,domainLen);
	    d = d + domainLen;
	}
	else if (m_addrType == IPv6) {
	    for (int i = 0; i < 8; i++, d += 2) {
		String tmp;
		tmp.hexify(d,2);
		m_addr.append(tmp,":");
	    }
	}
	m_port = (d[0] << 8) | d[1];
	return ParseOk;
    }
    Debug(DebugStub,"Request to parse unhandled message type %u: '%s'",
	m_type,msgName());
    return ParseError;
}

// Utility function used in SOCKSPacket::toString()
inline void addExtended(String& buf, bool extended, unsigned char value,
    TokenDict* dict)
{
    if (!extended)
	return;
    buf << "(" << SOCKSPacket::token(value,dict) << ")";
}

// Build a string with the message content for debug purposes
void SOCKSPacket::toString(String& buf, bool extended) const
{
    buf << "Type=" << msgName();
    unsigned char* d = (unsigned char*)m_buffer.data();
    unsigned int len = m_buffer.length();
    switch (m_type) {
	case AuthMethods:
	    if (len) {
		buf << " VER=" << d[0];
		buf << " METHODS=" << m_auth.length() << " [";
		unsigned char* a = (unsigned char*)m_auth.data();
		for (unsigned int i = 0; i < m_auth.length(); i++, a++) {
		    buf << (i ? " " : "") << *a;
		    addExtended(buf,extended,*a,s_authName);
		}
		buf << "]";
	    }
	    break;
	case AuthReply:
	    if (len) {
		buf << " VER=" << d[0];
		unsigned char* a = (unsigned char*)m_auth.data();
		if (a) {
		    buf << " METHOD=" << *a;
		    addExtended(buf,extended,*a,s_authName);
		}
	    }
	    break;
	case UnamePwdRequest:
	    if (len) {
		buf << " VER=" << d[0];
		buf << " UNAME=" << m_username;
		buf << " PASSWD=" << m_password;
	    }
	    break;
	case UnamePwdReply:
	    if (len) {
		buf << " VER=" << d[0];
		if (m_auth.length()) {
		    unsigned char stat = *(unsigned char*)m_auth.data();
		    buf << " STATUS=" << stat;
		    if (extended)
			buf << (!stat ? "(OK)" : "(Failure)");
		}
	    }
	    break;
	case Request:
	case Reply:
	    if (len) {
		buf << " VER=" << d[0];
		bool req = (m_type == Request);
		buf << (req ? " CMD=" : " RSP=") << m_cmdRsp;
		addExtended(buf,extended,m_cmdRsp,req ? s_cmdName : s_replyText);
		buf << " ATYP=" << m_addrType;
		addExtended(buf,extended,m_addrType,s_addrTypeName);
		buf << " ADDR=" << m_addr;
		buf << " PORT=" << m_port;
	    }
	    break;
	case Unknown:
	    extended = true;
    }
    if (extended) {
	String tmp;
	tmp.hexify(m_buffer.data(),m_buffer.length(),' ');
	buf << " Hex: " << tmp;
    }
}

// Build a SOCKS request/reply message
SOCKSPacket* SOCKSPacket::buildSocks(SOCKSConn* conn, bool request,
    unsigned char cmdRsp, unsigned char addrType, const String& addr, int port)
{
    SOCKSPacket::Type type = request ? SOCKSPacket::Request : SOCKSPacket::Reply;
    const char* error = 0;
    unsigned char ip[4];
    // Check addr
    if (addrType == Domain) {
	if (!addr)
	    error = "empty address";
	if (addr.length() > 255)
	    error = "address too long";
    }
    else if (addrType == IPv4) {
	if (addr) {
	    ObjList* list = addr.split('.');
	    int i = 0;
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext(), i++) {
		int tmp = o->get()->toString().toInteger(-1);
		if (i > 3 || tmp < 0 || tmp > 255) {
		    error = "invalid address";
		    break;
		}
		ip[i] = (unsigned char)tmp;
	    }
	    TelEngine::destruct(list);
	}
	else
	    error = "empty address";
    }
    else
	error = "unsupported type";
    if (error) {
        Debug(conn ? conn->engine() : 0,DebugMild,
	    "SOCKSConn(%s) can't build %s with address=%s type=%u(%s) '%s' [%p]",
	    conn ? conn->toString().c_str() : "",lookup(type,SOCKSPacket::s_msgName),
	    addr.c_str(),addrType,lookup(addrType,SOCKSPacket::s_addrTypeName),error,conn);
	return 0;
    }

    SOCKSPacket* packet = new SOCKSPacket(type,conn);
    packet->m_cmdRsp = cmdRsp;
    packet->m_addrType = addrType;
    packet->m_addr = addr;
    packet->m_port = port;
    unsigned char buf[4] = {SOCKS_VERSION,cmdRsp,0,addrType};
    packet->m_buffer.append(buf,4);
    if (addrType == Domain) {
	unsigned char l = addr.length();
	packet->m_buffer.append(&l,1);
	packet->m_buffer += addr;
    }
    else if (addrType == IPv4)
	packet->m_buffer.append(ip,4);
    unsigned char p[2] = {(unsigned char)(port >> 8),(unsigned char)port};
    packet->m_buffer.append(p,2);
    return packet;
}

// Build an "auth methods" message
SOCKSPacket* SOCKSPacket::buildAuthMethods(SOCKSConn* conn, const void* methods,
    unsigned char count)
{
    if (!(methods && count))
	return 0;
    unsigned char buf[2] = {SOCKS_VERSION,count};
    SOCKSPacket* packet = new SOCKSPacket(AuthMethods,conn);
    packet->m_buffer.append(buf,2);
    packet->m_auth.append((void*)methods,count);
    packet->m_buffer += packet->m_auth;
    return packet;
}

// Build an "auth reply" message
SOCKSPacket* SOCKSPacket::buildAuthReply(SOCKSConn* conn, unsigned char method)
{
    unsigned char buf[2] = {SOCKS_VERSION,method};
    SOCKSPacket* packet = new SOCKSPacket(AuthReply,conn);
    packet->m_buffer.append(buf,2);
    packet->m_auth.append(&method,1);
    return packet;
}


// Build an username/password auth request
SOCKSPacket* SOCKSPacket::buildUnamePwdReq(SOCKSConn* conn, const String& uname,
    const String& pwd)
{
    if (uname.null() || pwd.null() || uname.length() > 255 || pwd.length() > 255)
	return 0;
    SOCKSPacket* packet = new SOCKSPacket(UnamePwdRequest,conn);
    packet->m_username = uname;
    packet->m_password = pwd;
    unsigned char tmp = UNAMEPWD_VERSION;
    packet->m_buffer.append(&tmp,1);
    tmp = (unsigned char)uname.length();
    packet->m_buffer.append(&tmp,1);
    packet->m_buffer += uname;
    tmp = (unsigned char)pwd.length();
    packet->m_buffer.append(&tmp,1);
    packet->m_buffer += pwd;
    return packet;
}

// Build an username/password auth reply
SOCKSPacket* SOCKSPacket::buildUnamePwdReply(SOCKSConn* conn, unsigned char ok)
{
    SOCKSPacket* packet = new SOCKSPacket(UnamePwdReply,conn);
    packet->m_auth.append(&ok,1);
    unsigned char buf[2] = {UNAMEPWD_VERSION,ok};
    packet->m_buffer.append(buf,2);
    return packet;
}


// Incoming connection constructor
SOCKSConn::SOCKSConn(SOCKSEngine* engine, Socket* sock, SOCKSEndpointDef* epDef)
    : Mutex(true,"SOCKSConn"),
    m_status(Idle), m_outgoing(false),
    m_waitMsg(0), m_engine(engine), m_socket(sock), m_sendError(0),
    m_socksTimeoutMs(0), m_epDef(epDef),
    m_reqCmd(SOCKSPacket::CmdUnknown), m_reqAddrType(SOCKSPacket::AddrUnknown),
    m_reqPort(0),
    m_replyRsp(SOCKSPacket::EOk), m_replyAddrType(SOCKSPacket::AddrUnknown),
    m_replyPort(0)
{
    buildId();
    changeStatus(WaitMsg);
    m_waitMsg = new SOCKSPacket(SOCKSPacket::AuthMethods,this);
    setSocksTimeout(Time::msecNow(),true);
}

// Outgoing connection constructor
SOCKSConn::SOCKSConn(SOCKSEngine* engine, SOCKSEndpointDef* epDef, unsigned char cmd,
    unsigned char addrType, const String& addr, int port)
    : Mutex(true,"SOCKSConn"),
    m_status(Idle), m_outgoing(true),
    m_waitMsg(0), m_engine(engine), m_socket(0), m_sendError(0),
    m_socksTimeoutMs(0), m_epDef(epDef),
    m_reqCmd(cmd), m_reqAddrType(addrType), m_reqAddr(addr), m_reqPort(port),
    m_replyRsp(SOCKSPacket::EOk), m_replyAddrType(SOCKSPacket::AddrUnknown),
    m_replyPort(0)
{
}

SOCKSConn::~SOCKSConn()
{
    TelEngine::destruct(m_epDef);
    terminate();
}

// Terminate the socket. Release memory
void SOCKSConn::destroyed()
{
    DDebug(m_engine,DebugAll,"SOCKSConn(%s) destroyed [%p]",m_id.c_str(),this);
    TelEngine::destruct(m_epDef);
    terminate();
    RefObject::destroyed();
}

// Get this connection's id
const String& SOCKSConn::toString() const
{
    return m_id;
}

// Process connection while waiting for a message
SOCKSPacket* SOCKSConn::processSocks(const Time& now, bool& error, bool& timeout)
{
    error = timeout = false;
    Lock lock(this);
    if (m_status == Terminated) {
	error = true;
	return 0;
    }
    if (m_status != WaitMsg || !m_socket)
	return 0;
    // Sanity check
    if (!m_waitMsg) {
	Debug(m_engine,DebugGoOn,
	    "SOCKSConn(%s) inconsistent status (no msg in %s status) [%p]",
	    m_id.c_str(),statusName(m_status),this);
	error = true;
	return 0;
    }
    // Check received message
    // Max msg: UnamePwdRequest: 513 bytes
    unsigned char buf[528];
    unsigned int read = sizeof(buf);
    if (!recv(buf,read)) {
	changeStatus(Terminated);
	error = true;
	return 0;
    }
    // Use a while to break
    while (read) {
	SOCKSPacket::ParseResult res = m_waitMsg->parse(buf,read);
	if (res == SOCKSPacket::ParseIncomplete)
	    break;
	setSocksTimeout(0);
	if (m_engine)
	    m_engine->receivedPacket(*m_waitMsg);
	if (res == SOCKSPacket::ParseError) {
	    Debug(m_engine,DebugNote,
		"SOCKSConn(%s) received invalid message '%s' [%p]",
		m_id.c_str(),m_waitMsg->msgName(),this);
	    TelEngine::destruct(m_waitMsg);
	    changeStatus(Terminated);
	    error = true;
	    return 0;
	}
	// OK
	SOCKSPacket* ret = m_waitMsg;
	m_waitMsg = 0;
	changeStatus(Idle);
	switch (ret->type()) {
	    case SOCKSPacket::AuthMethods:
		error = !processAuthMethods(*ret);
		break;
	    case SOCKSPacket::AuthReply:
		error = !processAuthReply(*ret);
		break;
	    case SOCKSPacket::UnamePwdRequest:
		error = !processUnamePwdRequest(*ret);
		break;
	    case SOCKSPacket::UnamePwdReply:
		error = !processUnamePwdReply(*ret);
		break;
	    case SOCKSPacket::Request:
		error = !processRequest(*ret);
		if (!error)
		    setSocksTimeout(now.msec(),false);
		break;
	    case SOCKSPacket::Reply:
		error = !processReply(*ret);
		break;
	    default:
		error = true;
		Debug(m_engine,DebugNote,
		    "SOCKSConn(%s) received unhandled message '%s' [%p]",
		    m_id.c_str(),m_waitMsg->msgName(),this);
	}

	if (error) {
	    changeStatus(Terminated);
	    TelEngine::destruct(ret);
	}
	return ret;
    }
    // Check timeout
    if (m_socksTimeoutMs && m_socksTimeoutMs < now.msec()) {
        Debug(m_engine,DebugNote,
	    "SOCKSConn(%s) timed out while waiting for '%s' [%p]",
	    m_id.c_str(),m_waitMsg->msgName(),this);
        TelEngine::destruct(m_waitMsg);
	changeStatus(Terminated);
	error = true;
        timeout = true;
	return 0;
    }
    return 0;
}

// Build and send a SOCKS reply
bool SOCKSConn::sendReply(unsigned char addrType, const String& addr, int port,
    unsigned char rsp)
{
    m_replyRsp = rsp;
    m_replyAddrType = addrType;
    m_replyAddr = addr;
    m_replyPort = port;
    SOCKSPacket* packet = SOCKSPacket::buildSocks(this,false,rsp,addrType,addr,port);
    if (!packet) {
	changeStatus(Terminated);
	return false;
    }
    bool terminate = (rsp != SOCKSPacket::EOk);
    return sendProtocolMsg(packet,terminate,SOCKSPacket::Unknown);
}

// Enable data transfer after succesfully negotiating SOCKS
bool SOCKSConn::enableDataTransfer()
{
    Lock lock(this);
    if (m_status == Terminated)
	return false;
    setSocksTimeout(0);
    changeStatus(Running);
    return true;
}

// Set connecting state (outgoing only)
void SOCKSConn::setConnecting()
{
    if (!outgoing())
	return;
    if (m_socket)
	terminate();
    changeStatus(Connecting);
}

// Set socket (outgoing only)
bool SOCKSConn::setSocket(Socket* sock, bool sendAuthMeth)
{
    Lock lck(this);
    if (!outgoing() || m_status != Connecting) {
	SOCKSEngine::destroySocket(sock);
	return false;
    }
    if (m_socket)
	terminate();
    changeStatus(Idle);
    m_socket = sock;
    buildId();
    Debug(m_engine,DebugAll,"SOCKSConn(%s)::setSocket(%p) [%p]",m_id.c_str(),m_socket,this);
    if (m_socket) {
	m_socket->setBlocking(false);
	if (sendAuthMeth)
	    sendAuthMethods();
	return true;
    }
    terminate();
    return false;
}

// Terminate and delete the socket
void SOCKSConn::terminate()
{
    TelEngine::destruct(m_waitMsg);
    changeStatus(Terminated);
    if (!m_socket)
	return;
    DDebug(m_engine,DebugAll,"SOCKSConn(%s) terminating socket [%p]",m_id.c_str(),this);
    SOCKSEngine::destroySocket(m_socket);
    m_sendError = 0;
}

// Build and send an auth methods message
bool SOCKSConn::sendAuthMethods()
{
    lock();
    unsigned char n = 1;
    unsigned char meth[2] = {SOCKSPacket::AuthNone,0};
    if (m_epDef && m_epDef->authRequired()) {
	meth[1] = SOCKSPacket::UnamePwd;
	n++;
    }
    unlock();
    return sendProtocolMsg(SOCKSPacket::buildAuthMethods(this,meth,n),false,
	SOCKSPacket::AuthReply);
}

// Build and send an auth reply message
bool SOCKSConn::sendAuthReply(unsigned char method)
{
    switch (method) {
	case SOCKSPacket::AuthNone:
	    return sendProtocolMsg(SOCKSPacket::buildAuthReply(this,method),false,
		SOCKSPacket::Request);
	case SOCKSPacket::UnamePwd:
	    return sendProtocolMsg(SOCKSPacket::buildAuthReply(this,method),false,
		SOCKSPacket::UnamePwdRequest);
	case SOCKSPacket::NotAuth:
	    return sendProtocolMsg(SOCKSPacket::buildAuthReply(this,method),true);
    }
    Debug(m_engine,DebugStub,
	"SOCKSConn(%s) request to send auth reply with unhandled method %u [%p]",
	m_id.c_str(),method,this);
    return false;
}

// Send protocol messages through the socket
bool SOCKSConn::sendProtocolMsg(SOCKSPacket* packet, bool terminate,
	SOCKSPacket::Type wait)
{
    if (!packet)
	return false;
    Lock lock(this);
    if (!m_engine || m_status != Idle) {
	Debug(m_engine,DebugMild,"SOCKSConn(%s) can't send %s in state %s [%p]",
	    m_id.c_str(),packet->msgName(),statusName(m_status),this);
	TelEngine::destruct(packet);
	return false;
    }
    if (!m_engine->sendPacket(packet)) {
	changeStatus(Terminated);
	return false;
    }
    TelEngine::destruct(m_waitMsg);
    if (terminate)
	changeStatus(Terminated);
    else {
	if (wait != SOCKSPacket::Unknown) {
	    m_waitMsg = new SOCKSPacket(wait,this);
	    changeStatus(WaitMsg);
	    setSocksTimeout(Time::msecNow(),wait != SOCKSPacket::Reply);
	}
	else
	    changeStatus(Idle);
    }
    return true;
}

// Build connection id
void SOCKSConn::buildId()
{
    Lock lock(this);
    if (!m_socket)
	return;
    m_id = "";
    SocketAddr local;
    m_socket->getSockName(local);
    m_id << local.host() << ":" << local.port();
    SocketAddr remote;
    m_socket->getPeerName(remote);
    m_id << "-" << remote.host() << ":" << remote.port();
}

// Send data
bool SOCKSConn::send(const void* buf, unsigned int& len)
{
    if (!(len && valid()))
	return false;

    int c = m_socket->writeData(buf,len);
    if (c != Socket::socketError()) {
#ifdef XDEBUG
	if (len) {
	    String s;
	    s.hexify((void*)buf,len,' ');
	    Debug(m_engine,DebugAll,"SOCKSConn(%s) sent %d/%u bytes '%s' [%p]",
		m_id.c_str(),c,len,s.c_str(),this);
	}
#endif
	len = c;
	m_sendError = 0;
	return true;
    }
    len = 0;
    if (m_socket->canRetry()) {
	if (m_sendError != m_socket->error()) {
	    m_sendError = m_socket->error();
	    String s;
	    Thread::errorString(s,m_socket->error());
	    DDebug(m_engine,DebugMild,
		"SOCKSConn(%s) socket temporary unavailable to send. %d: '%s' [%p]",
		m_id.c_str(),m_socket->error(),s.c_str(),this);
	}
	return true;
    }
    String s;
    Thread::errorString(s,m_socket->error());
    Debug(m_engine,DebugWarn,"SOCKSConn(%s) socket send error. %d: '%s' [%p]",
	m_id.c_str(),m_socket->error(),s.c_str(),this);
    return false;
}

// Read data from socket
bool SOCKSConn::recv(void* buf, unsigned int& len)
{
    if (!valid())
	return false;

    int read = m_socket->readData(buf,len);
    if (read != Socket::socketError()) {
#ifdef XDEBUG
	if (read) {
	    String s;
	    s.hexify(buf,read,' ');
	    Debug(m_engine,DebugAll,"SOCKSConn(%s) recv %d bytes '%s' [%p]",
		m_id.c_str(),read,s.c_str(),this);
	}
#endif
	len = read;
	return true;
    }

    len = 0;
    if (m_socket->canRetry())
	return true;
    String s;
    Thread::errorString(s,m_socket->error());
    Debug(m_engine,DebugWarn,"SOCKSConn(%s) socket read error. %d: '%s' [%p]",
	m_id.c_str(),m_socket->error(),s.c_str(),this);
    return false;
}

// Connect a socket
Socket* SOCKSConn::connect(SOCKSEngine* engine, const String& address, int port,
    unsigned int connToutMs, int& error, bool& timeout)
{
    SocketAddr addr(PF_INET);
    addr.host(address);
    if (!addr.host()) {
	Debug(engine,DebugNote,"Failed to resolve '%s'",address.c_str());
	error = Thread::lastError();
	return 0;
    }
    addr.port(port);
    String sa;
    if (!engine || engine->debugAt(DebugNote)) {
	sa << addr.host().c_str() << ":" << addr.port();
	if (addr.host() != address)
	    sa << " (" << address << ")";
    }
    Debug(engine,DebugAll,"Connecting to '%s'",sa.safe());
    Socket* sock = new Socket;
    bool ok = false;
    error = 0;
    timeout = false;
    if (sock->create(PF_INET,SOCK_STREAM)) {
	if (connToutMs && sock->canSelect() && sock->setBlocking(false))
	    ok = sock->connectAsync(addr,connToutMs * 1000,&timeout);
	else
	    ok = sock->connect(addr);
	if (Thread::check(false)) {
	    SOCKSEngine::destroySocket(sock);
	    XDebug(engine,DebugAll,"Connect to %s cancelled",sa.c_str());
	    return 0;
	}
    }
    if (ok) {
	Debug(engine,DebugAll,"Connected to '%s'",sa.safe());
	return sock;
    }
    if (!timeout)
	error = sock->error();
    SOCKSEngine::destroySocket(sock);
    if (!engine || engine->debugAt(DebugNote)) {
	String s;
	if (timeout)
	    s = "Timeout";
	else {
	    String tmp;
	    Thread::errorString(tmp,error);
	    s << error << " " << tmp;
	}
	Debug(engine,DebugNote,"Failed to connect to %s: %s",sa.c_str(),s.c_str());
    }
    return 0;
}

// Changed connection status
bool SOCKSConn::changeStatus(Status stat)
{
    if (m_status == stat || m_status == Terminated)
	return false;
    Debug(m_engine,DebugInfo,"SOCKSConn(%s) changed status from '%s' to '%s' [%p]",
        m_id.c_str(),statusName(m_status),statusName(stat),this);
    m_status = stat;
    return true;
}

// Set/reset the timeout when negotiating SOCKS
void SOCKSConn::setSocksTimeout(u_int64_t now, bool auth)
{
    if (!now) {
	if (!m_socksTimeoutMs)
	     Debug(m_engine,DebugInfo,"SOCKSConn(%s) stopping timer [%p]",
		m_id.c_str(),this);
	m_socksTimeoutMs = 0;
	return;
    }
    u_int64_t interval = 0;
    if (m_engine) {
	if (auth)
	    interval = m_engine->waitMsgAuthInterval();
	else
	    interval = m_engine->waitMsgReplyInterval();
    }
    m_socksTimeoutMs = now;
    Debug(m_engine,DebugInfo,
	"SOCKSConn(%s) starting timer now=" FMT64U " interval=" FMT64U " [%p]",
	m_id.c_str(),m_socksTimeoutMs,interval,this);
    m_socksTimeoutMs += interval;
}

// Message processor. Return false to terminate the connection
bool SOCKSConn::processAuthMethods(const SOCKSPacket& packet)
{
    if (!packet.m_auth.length()) {
	Debug(m_engine,DebugMild,"SOCKSConn(%s) received '%s' with no methods [%p]",
	    m_id.c_str(),packet.msgName(),this);
	return false;
    }

    unsigned char* d = (unsigned char*)packet.m_auth.data();
    unsigned char auth = SOCKSPacket::NotAuth;
    if (m_epDef && m_epDef->authRequired()) {
	for (unsigned int i = 0; i < packet.m_auth.length(); i++) {
	    if (d[i] == SOCKSPacket::UnamePwd) {
		auth = SOCKSPacket::UnamePwd;
		break;
	    }
	}
    }
    if (auth == SOCKSPacket::NotAuth)
	for (unsigned int i = 0; i < packet.m_auth.length(); i++) {
	    if (d[i] == SOCKSPacket::AuthNone) {
		auth = SOCKSPacket::AuthNone;
		break;
	    }
	}
    if (auth != SOCKSPacket::NotAuth)
	return sendAuthReply(auth);
    Debug(m_engine,DebugMild,
	"SOCKSConn(%s) received '%s' with unsupported methods [%p]",
	m_id.c_str(),packet.msgName(),this);
    sendAuthReply(SOCKSPacket::NotAuth);
    return false;
}

// Message processor. Return false to terminate the connection
bool SOCKSConn::processAuthReply(const SOCKSPacket& packet)
{
    if (!packet.m_auth.length()) {
	Debug(m_engine,DebugMild,"SOCKSConn(%s) received '%s' with no method [%p]",
	    m_id.c_str(),packet.msgName(),this);
	return false;
    }
    unsigned char auth = *(unsigned char*)packet.m_auth.data();
    if (auth == SOCKSPacket::AuthNone)
	return sendRequest();
    if (auth == SOCKSPacket::UnamePwd)
	return sendUnamePwd();
    Debug(m_engine,DebugNote,
	"SOCKSConn(%s) received unsupported authentication method %u [%p]",
	m_id.c_str(),auth,this);
    return false;
}

// Message processor. Return false to terminate the connection
bool SOCKSConn::processUnamePwdRequest(const SOCKSPacket& packet)
{
    if (m_epDef && packet.m_username == m_epDef->username() &&
	packet.m_password == m_epDef->password()) {
	Debug(m_engine,DebugAll,"SOCKSConn(%s) authenticated [%p]",m_id.c_str(),this);
	return sendUnamePwdReply(0);
    }
    Debug(m_engine,DebugNote,"SOCKSConn(%s) remote has incorrect credentials [%p]",
	m_id.c_str(),this);
    return sendUnamePwdReply(0xff);
}

// Message processor. Return false to terminate the connection
bool SOCKSConn::processUnamePwdReply(const SOCKSPacket& packet)
{
    if (!packet.m_auth.length()) {
	Debug(m_engine,DebugMild,"SOCKSConn(%s) received '%s' with no status [%p]",
	    m_id.c_str(),packet.msgName(),this);
	return false;
    }
    unsigned char auth = *(unsigned char*)packet.m_auth.data();
    // 0: authenticated
    if (!auth) {
	Debug(m_engine,DebugAll,"SOCKSConn(%s) authenticated [%p]",m_id.c_str(),this);
	return sendRequest();
    }
    Debug(m_engine,DebugNote,
	"SOCKSConn(%s) remote denyed authentication (code=%u) [%p]",
	m_id.c_str(),auth,this);
    return false;
}

// Message processor. Return false to terminate the connection
bool SOCKSConn::processRequest(const SOCKSPacket& packet)
{
    // Update data
    m_reqCmd = packet.m_cmdRsp;
    m_reqAddrType = packet.m_addrType;
    m_reqAddr = packet.m_addr;
    m_reqPort = packet.m_port;
    return true;
}

// Message processor. Return false to terminate the connection
bool SOCKSConn::processReply(const SOCKSPacket& packet)
{
    // Update data
    m_replyRsp = packet.m_cmdRsp;
    m_replyAddrType = packet.m_addrType;
    m_replyAddr = packet.m_addr;
    m_replyPort = packet.m_port;
    // OK
    if (m_replyRsp == SOCKSPacket::EOk) {
	DDebug(m_engine,DebugAll,"SOCKSConn(%s) processed %s [%p]",
	    m_id.c_str(),packet.msgName(),this);
	return true;
    }
    // Error
    Debug(m_engine,DebugNote,"SOCKSConn(%s) received %s with rsp %u: '%s' [%p]",
	m_id.c_str(),packet.msgName(),m_replyRsp,
	lookup(m_replyRsp,SOCKSPacket::s_replyText),this);
    return false;
}


typedef GenPointer<SOCKSListener> ListenerPointer;

/*
 * SOCKSEndpointDef
 */
SOCKSEndpointDef::SOCKSEndpointDef(const char* name, bool proxy,
    const char* address, int port, const char* external,
    const char* uname, const char* pwd)
    : m_proxy(proxy), m_name(name),
    m_address(address), m_externalAddr(external), m_port(port),
    m_authRequired(false),
    m_username(uname), m_password(pwd)
{
    m_authRequired = !(m_username.null() || m_password.null());
}

SOCKSEndpointDef::SOCKSEndpointDef(NamedList& params)
{
    m_name = params;
    m_proxy = params.getBoolValue("proxy");
    m_address = params.getValue("address");
    m_port = params.getIntValue("port");
    m_externalAddr = params.getValue("external_address");
    m_username = params.getValue("username");
    m_password = params.getValue("password");
    m_authRequired = !(m_username.null() || m_password.null());
}

// Get the endpoint definiton name (id)
const String& SOCKSEndpointDef::toString() const
{
    return name();
}


/*
 * SOCKSListener
 */
SOCKSListener::SOCKSListener(SOCKSEngine* engine, SOCKSEndpointDef* epDef,
    unsigned int backlog)
    : m_epDef(epDef),
    m_backlog(backlog), m_socket(0), m_listenError(false),
    m_engine(engine), m_status(Created)
{
    if (m_epDef)
	m_id << m_epDef->address() << ":" << m_epDef->port();
    DDebug(m_engine,DebugAll,"SOCKSListener(%s) created [%p]",m_id.c_str(),this);
}

SOCKSListener::~SOCKSListener()
{
    terminate();
    if (m_engine)
	m_engine->removeListener(this);
    DDebug(m_engine,DebugAll,"SOCKSListener(%s) destroyed [%p]",m_id.c_str(),this);
}

// Create and bind the socket
bool SOCKSListener::init()
{
    if (m_socket)
	terminate();
    if (!m_epDef)
	return false;

    m_status = Initializing;
    SocketAddr addr(PF_INET);
    addr.host(m_epDef->address());
    addr.port(m_epDef->port());
    m_socket = new Socket;
    bool ok = m_socket->create(PF_INET,SOCK_STREAM);
    const char* op = 0;
    if (ok) {
	m_socket->setReuse();
	ok = m_socket->bind(addr);
	if (ok) {
	    ok = m_socket->setBlocking(false);
	    if (!ok)
		op = "set blocking (false)";
	}
	else
	    op = "bind";
    }
    else
	op = "create";
    if (ok) {
	m_status = Bind;
	Debug(m_engine,DebugAll,"Listener(%s) bind succeeded [%p]",m_id.c_str(),this);
    }
    else {
	if (!m_listenError) {
	    String s;
	    Thread::errorString(s,m_socket->error());
	    Debug(m_engine,DebugWarn,"Listener(%s) failed to %s socket. %d: '%s' [%p]",
		m_id.c_str(),op,m_socket->error(),s.c_str(),this);
	    m_listenError = true;
	}
	terminate();
    }
    return ok;
}

// Start listening
bool SOCKSListener::startListen()
{
    if (!(m_socket && m_socket->valid()))
	return false;
    if (m_socket->listen(m_backlog)) {
	Debug(m_engine,DebugAll,"Listener(%s) started [%p]",m_id.c_str(),this);
	m_listenError = false;
	m_status = Listening;
	return true;
    }
    if (!m_listenError) {
	String s;
	Thread::errorString(s,m_socket->error());
	Debug(m_engine,DebugWarn,"Listener(%s) failed to start. %d: '%s' [%p]",
	    m_id.c_str(),m_socket->error(),s.c_str(),this);
	m_listenError = true;
    }
    return false;
}

// Check for incoming connections
Socket* SOCKSListener::accept(SocketAddr& addr)
{
    if (!(m_socket && m_socket->valid()))
	return 0;
    Socket* sock = m_socket->accept(addr);
    if (sock)
	Debug(m_engine,DebugAll,"Listener(%s) got conn from '%s:%d' [%p]",
	    m_id.c_str(),addr.host().c_str(),addr.port(),this);
    return sock;
}

// Terminate the socket
void SOCKSListener::terminate()
{
    if (!m_socket)
	return;
    m_status = Terminated;
    DDebug(m_engine,DebugAll,"Listener(%s) terminating socket [%p]",
	m_id.c_str(),this);
    SOCKSEngine::destroySocket(m_socket);
}

// Init, start listening and call accept() in a loop
void SOCKSListener::run()
{
    Debug(m_engine,DebugAll,"Listener(%s) start running [%p]",m_id.c_str(),this);
    if (init() && startListen())
	while (true) {
	    if (Thread::check(false) || !m_engine || m_engine->exiting())
		break;
	    SocketAddr addr(PF_INET);
	    Socket* sock = accept(addr);
	    bool processed = false;
	    if (sock) {
		if (sock->setBlocking(false)) {
		    m_status = Accepting;
		    if (m_engine)
			processed = m_engine->incomingConnection(this,sock,addr);
		    else
			delete sock;
		}
		else {
		    String tmp;
		    Thread::errorString(tmp,sock->error());
		    Debug(m_engine,DebugNote,
			"Listener(%s) failed to reset blocking for incoming conn from '%s:%d'. %d: %s [%p]",
			m_id.c_str(),addr.host().c_str(),addr.port(),sock->error(),tmp.c_str(),this);
		    delete sock;
		}
	    }
	    m_status = Listening;
	    if (processed)
		Thread::yield(false);
	    else
		Thread::idle(false);
	}
    terminate();
    if (m_engine)
	m_engine->removeListener(this);
    Debug(m_engine,DebugAll,"Listener(%s) stopped [%p]",m_id.c_str(),this);
}

// Stop this listener. Terminate the socket
void SOCKSListener::stop(bool hard)
{
    Debug(m_engine,DebugStub,"SOCKSListener(%s) stop() [%p]",m_id.c_str(),this);
}


/*
 * SOCKSEngine
 */
SOCKSEngine::SOCKSEngine(NamedList& params)
    : Mutex(true,"SOCKSEngine"),
    m_exiting(false),
    m_waitMsgAuthInterval(10000), m_waitMsgReplyInterval(15000),
    m_connectToutMs(0),
    m_showMsg(false), m_dumpExtended(false)
{
    debugName(params.getValue("debugname","socks"));
    DDebug(this,DebugAll,"SocksEngine created [%p]",this);
}

// Initialize engine's parameters
void SOCKSEngine::initialize(NamedList& params)
{
    m_showMsg = params.getBoolValue(YSTRING("print-msg"),false);
    m_dumpExtended = params.getBoolValue(YSTRING("print-extended"),false);
    m_waitMsgAuthInterval = params.getIntValue(YSTRING("auth-timeout"),10000,3000,30000);
    m_waitMsgReplyInterval = params.getIntValue(YSTRING("reply-timeout"),30000,5000,120000);
    m_connectToutMs = getConnectTimeout(params,10000);
}

// Cleanup the engine. Stop listeners
void SOCKSEngine::cleanup()
{
    stopListeners(true,false);
    m_socksConn.clear();
    XDebug(this,DebugAll,"SOCKSEngine::cleanup()");
}

// Connect a connection, increase its reference counter, add it to the
//  list and start negotisting SOCKS
bool SOCKSEngine::addConnection(SOCKSConn* conn)
{
    if (!conn)
	return false;
    if (!conn->ref()) {
	conn->terminate();
	return false;
    }
    Lock lck(this);
    m_socksConn.append(conn);
    Debug(this,DebugAll,"Added outgoing connection (%p,'%s')",
	conn,conn->toString().c_str());
    return true;
}

// Incoming connection notification
bool SOCKSEngine::incomingConnection(SOCKSListener* listener, Socket* sock,
    SocketAddr& addr)
{
    if (!(listener && sock)) {
	destroySocket(sock);
	return false;
    }

    SOCKSConn* conn = 0;
    if (listener->epDef() && listener->epDef()->ref()) {
	if (!listener->epDef()->proxy())
	    conn = new SOCKSConn(this,sock,listener->epDef());
	else {
	    // TODO: implement
	    Debug(this,DebugStub,"Please implement incomingConnection() for proxy");
	}
    }
    if (!conn) {
	destroySocket(sock);
	return false;
    }
    Lock lock(this);
    m_socksConn.append(conn);
    Debug(this,DebugAll,"Added incoming connection (%p,'%s')",
	conn,conn->toString().c_str());
    return true;
}

// Process connections negotiating SOCKS
bool SOCKSEngine::process()
{
    bool processed = false;
    lock();
    ListIterator iter(m_socksConn);
    Time now;
    for (;;) {
	if (Thread::check(false) || exiting())
	    break;
	SOCKSConn* conn = static_cast<SOCKSConn*>(iter.get());
	// End of iteration?
	if (!conn)
	    break;
	RefPointer<SOCKSConn> connRef = conn;
	// Dead pointer?
	if (!connRef)
	    continue;
	unlock();
	processed = processSocksConnection(connRef,now) || processed;
	lock();
	// Destroy now the reference
	connRef = 0;
    }
    unlock();
    return processed;
}

// Process a connection negotiating the SOCKS protocol
bool SOCKSEngine::processSocksConnection(SOCKSConn* conn, const Time& now)
{
    if (!conn)
	return false;
    Lock lock(conn);
    if (conn->status() == SOCKSConn::Terminated) {
	lock.drop();
	removeSocksConn(conn,"terminated");
	return false;
    }

    bool error = false;
    bool timeout = false;
    SOCKSPacket* packet = conn->processSocks(now,error,timeout);
    if (packet) {
	if (packet->type() == SOCKSPacket::Request) {
	    SOCKSPacket::Error err = processSOCKSRequest(*packet,conn);
	    if (err != SOCKSPacket::EOk) {
		error = true;
		defaultRequestHandler(conn,err);
	    }
	}
	else if (packet->type() == SOCKSPacket::Reply)
	    error = !processSOCKSReply(*packet,conn);
	TelEngine::destruct(packet);
    }
    else if (!error)
	return false;
    if (error) {
	lock.drop();
	socksConnError(conn,timeout);
	removeSocksConn(conn,timeout ? "timeout" : "received invalid packet");
    }
    return true;
}

// Send a packet through a connection
bool SOCKSEngine::sendPacket(SOCKSPacket* packet)
{
    if (!(packet && packet->conn())) {
	TelEngine::destruct(packet);
	return false;
    }

    if (m_showMsg && debugAt(DebugInfo)) {
	String tmp;
	packet->toString(tmp,m_dumpExtended);
	Debug(this,DebugInfo,"SOCKSConn(%s) sending message %s [%p]",
	    packet->conn()->toString().c_str(),tmp.c_str(),packet->conn());
    }

    unsigned int sent = packet->m_buffer.length();
    bool ok = packet->conn()->send(packet->m_buffer.data(),sent) &&
	sent == packet->m_buffer.length();
    if (!ok)
	Debug(this,DebugNote,"SOCKSConn(%s) failed to send message '%s' [%p]",
	    packet->conn()->toString().c_str(),packet->msgName(),packet->conn());
    TelEngine::destruct(packet);
    return ok;
}

// Print a debug message when a connections received a packet
void SOCKSEngine::receivedPacket(const SOCKSPacket& packet)
{
    if (!(m_showMsg && debugAt(DebugInfo)))
	return;
    String tmp;
    packet.toString(tmp,m_dumpExtended);
    Debug(this,DebugInfo,"SOCKSConn(%s) receiving message %s [%p]",
	packet.conn() ? packet.conn()->toString().c_str() : "",
	tmp.c_str(),packet.conn());
}

// Add an endpoint definition. Replace an existing one with the same name
void SOCKSEngine::addEpDef(SOCKSEndpointDef* epDef)
{
    if (!epDef)
	return;
    Lock lock(this);
    if (m_epDef.find(epDef))
	return;
    ObjList* o = m_epDef.find(epDef->toString());
    if (!o)
	m_epDef.append(epDef);
    else
	o->set(epDef);
    return;
}

// Remove an endpoint definition
void SOCKSEngine::removeEpDef(const String& name)
{
    Lock lock(this);
    ObjList* o = m_epDef.find(name);
    if (o) {
	// Remove listener
	SOCKSEndpointDef* ep = static_cast<SOCKSEndpointDef*>(o->get());
	for (ObjList* l = m_listeners.skipNull(); l; l = l->skipNext()) {
	    ListenerPointer* p = static_cast<ListenerPointer*>(l->get());
	    if ((*p)->epDef() == ep) {
		(*p)->stop(false);
		break;
	    }
	}
	// Remove object
	o->remove();
    }
}

// Find an endpoint definition by its name
SOCKSEndpointDef* SOCKSEngine::findEpDef(const String& name)
{
    Lock lock(this);
    ObjList* o = m_epDef.find(name);
    if (!o)
	return 0;
    SOCKSEndpointDef* tmp = static_cast<SOCKSEndpointDef*>(o->get());
    return tmp->ref() ? tmp : 0;
}

// Add a socket listener
void SOCKSEngine::addListener(SOCKSListener* listener)
{
    if (!listener)
	return;
    Lock lock(this);
    m_listeners.append(new ListenerPointer(listener))->setDelete(false);
    Debug(this,DebugAll,"Added listener (%p)",listener);
}

// Remove a socket listener
void SOCKSEngine::removeListener(SOCKSListener* listener)
{
    if (!listener)
	return;
    Lock lock(this);
    for (ObjList* o = m_listeners.skipNull(); o; o = o->skipNext()) {
	ListenerPointer* p = static_cast<ListenerPointer*>(o->get());
	if (*p != listener)
	    continue;
	o->remove(false);
	Debug(this,DebugAll,"Removed listener (%p)",listener);
	return;
    }
}

// Check if a listener exists
bool SOCKSEngine::hasListener(SOCKSListener* listener, int& status)
{
    if (!listener)
	return false;
    Lock lock(this);
    for (ObjList* o = m_listeners.skipNull(); o; o = o->skipNext()) {
	ListenerPointer* p = static_cast<ListenerPointer*>(o->get());
	if (*p != listener)
	    continue;
	status = listener->status();
	return true;
    }
    return false;
}

// Stop socket listeners
void SOCKSEngine::stopListeners(bool wait, bool hard)
{
    Lock lock(this);
    ObjList* o = m_listeners.skipNull();
    if (!o)
	return;
    Debug(this,DebugAll,"Stopping socket listeners wait=%s hard=%s",
	String::boolText(wait),String::boolText(hard));
    for (; o; o = o->skipNext()) {
	ListenerPointer* p = static_cast<ListenerPointer*>(o->get());
	(*p)->stop(hard);
    }
    if (!wait) {
	m_listeners.clear();
	return;
    }
    lock.drop();
    while (m_listeners.skipNull())
	Thread::yield(true);
    Debug(this,DebugAll,"Stopped all socket listeners");
}

// Remove and delete a connection from SOCKS list
void SOCKSEngine::removeSocksConn(SOCKSConn* conn, const char* reason)
{
    if (!conn)
	return;
    Lock lock(this);
    ObjList* o = m_socksConn.find(conn);
    if (!o)
	return;
    Debug(this,DebugAll,"Removing connection (%p,'%s') reason=%s",
	conn,conn->toString().c_str(),reason);
    o->remove();
}

// Terminate and delete a socket
void SOCKSEngine::destroySocket(Socket*& sock)
{
    if (!sock)
	return;
    Socket* tmp = sock;
    sock = 0;
    tmp->setLinger(-1);
    tmp->terminate();
    delete tmp;
}

// Process a SOCKS request
SOCKSPacket::Error SOCKSEngine::processSOCKSRequest(const SOCKSPacket& packet,
    SOCKSConn* conn)
{
    Debug(this,DebugStub,"processSOCKSRequest() conn (%p,%s)",
	conn,conn ? conn->toString().c_str() : "");
    return SOCKSPacket::EUnsuppCmd;
}

// Default SOCKS request. Sends a reply with 'rsp' non 0 (error).
// This method is called by the engine if request processor returns an error
void SOCKSEngine::defaultRequestHandler(SOCKSConn* conn, SOCKSPacket::Error err)
{
    DDebug(this,DebugAll,"defaultRequestHandler(%u) conn (%p,%s)",
	err,conn,conn ? conn->toString().c_str() : "");
    if (!conn || err == SOCKSPacket::EOk)
	return;
    conn->sendReply(conn->reqAddrType(),conn->reqAddr(),conn->reqPort(),err);
}

// Process a SOCKS reply
bool SOCKSEngine::processSOCKSReply(const SOCKSPacket& packet, SOCKSConn* conn)
{
    Debug(this,DebugStub,"processSOCKSReply() conn (%p,%s)",
	conn,conn ? conn->toString().c_str() : "");
    return false;
}


/*
 * YSocksEngine
 */
YSocksEngine::YSocksEngine(NamedList& params)
    : SOCKSEngine(params)
{
    debugChain(&__plugin);
}

void YSocksEngine::initialize(NamedList& params)
{
    SOCKSEngine::initialize(params);
    if (debugAt(DebugInfo)) {
	String tmp;
	tmp << "auth-timeout=" << (unsigned int)m_waitMsgAuthInterval << "ms";
	tmp << " reply-timeout=" << (unsigned int)m_waitMsgReplyInterval << "ms";
	tmp << " print-msg=" << String::boolText(m_showMsg);
	tmp << " print-extended=" << String::boolText(m_dumpExtended);
	Debug(this,DebugInfo,"Initialized %s",tmp.c_str());
    }
}

void YSocksEngine::cleanup()
{
    SOCKSEngine::cleanup();
    ListIterator iter(m_wrappers);
    for (GenObject* o = 0; 0 != (o = iter.get());)
	removeWrapper(static_cast<YSocksWrapper*>(o),false);
    XDebug(this,DebugAll,"YSocksEngine::cleanup()");
}

// Find a wrapper with a given DST ADDR/PORT
// Return a referenced object if found
YSocksWrapper* YSocksEngine::findWrapper(bool client, const String& dstAddr, int dstPort)
{
    Lock lock(this);
    for (ObjList* o = m_wrappers.skipNull(); o; o = o->skipNext()) {
	YSocksWrapper* w = static_cast<YSocksWrapper*>(o->get());
	if (w->client() == client && w->dstPort() == dstPort && w->dstAddr() == dstAddr)
	    return w->ref() ? w : 0;
    }
    return 0;
}

// Find a wrapper
YSocksWrapper* YSocksEngine::findWrapper(const String& wID)
{
    if (!wID)
	return 0;
    Lock lock(this);
    ObjList* o = m_wrappers.find(wID);
    if (!o)
	return 0;
    YSocksWrapper* w = static_cast<YSocksWrapper*>(o->get());
    return w->ref() ? w : 0;
}

// Find a wrapper with a given connection
YSocksWrapper* YSocksEngine::findWrapper(SOCKSConn* conn)
{
    Lock lock(this);
    for (ObjList* o = m_wrappers.skipNull(); o; o = o->skipNext()) {
	YSocksWrapper* w = static_cast<YSocksWrapper*>(o->get());
	if (w->conn() == conn)
	    return w->ref() ? w : 0;
    }
    return 0;
}

// Remove a wrapper from list
void YSocksEngine::removeWrapper(YSocksWrapper* w, bool delObj)
{
    if (!w)
	return;
    Lock lock(this);
    GenObject* gen = m_wrappers.remove(w,false);
    if (!(gen && gen->alive()))
	return;
    Debug(this,DebugAll,"Removed wrapper (%p,'%s') delObj=%s",
	w,w->toString().c_str(),String::boolText(delObj));
    if (delObj)
	TelEngine::destruct(gen);
}

// Add a wrapper
void YSocksEngine::addWrapper(YSocksWrapper* w)
{
    if (!w)
	return;
    Lock lock(this);
    m_wrappers.append(w)->setDelete(false);
    Debug(this,DebugAll,"Added wrapper (%p,'%s')",w,w->toString().c_str());
}

SOCKSPacket::Error YSocksEngine::processSOCKSRequest(const SOCKSPacket& packet,
    SOCKSConn* conn)
{
    if (!conn)
	return SOCKSPacket::EFailure;

    if (conn->reqCmd() != SOCKSPacket::Connect) {
	Debug(this,DebugNote,
	    "SOCKSConn(%s) %s with unsupported cmd %u (%s) [%p]",
	    conn->toString().c_str(),packet.msgName(),conn->reqCmd(),
	    SOCKSPacket::token(conn->reqCmd(),SOCKSPacket::s_cmdName),conn);
	return SOCKSPacket::EUnsuppCmd;
    }

    if (conn->reqAddrType() != SOCKSPacket::Domain) {
	Debug(this,DebugNote,
	    "SOCKSConn(%s) %s with unsupported address type %u (%s) [%p]",
	    conn->toString().c_str(),packet.msgName(),conn->reqAddrType(),
	    SOCKSPacket::token(conn->reqAddrType(),SOCKSPacket::s_addrTypeName),conn);
	return SOCKSPacket::EUnsuppAddrType;
    }

    // Find a wrapper for the connection
    YSocksWrapper* w = findWrapper(false,conn->reqAddr(),conn->reqPort());
    if (w) {
	// Set wrapper connection and remove it from engine on success
	// Return error to deny the request and remove connection from engine
	SOCKSPacket::Error result = SOCKSPacket::EOk;
	if (w->setConn(conn))
	    removeSocksConn(conn,"accepted");
	else
	    result = SOCKSPacket::EFailure;
	TelEngine::destruct(w);
	return result;
    }

    Debug(this,DebugNote,"SOCKSConn(%s) %s for unknown connection [%p]",
	conn->toString().c_str(),packet.msgName(),conn);
    return SOCKSPacket::EHostGone;
}

// Process SOCKS reply
// Always return false to remove the connection from engine's list
bool YSocksEngine::processSOCKSReply(const SOCKSPacket& packet, SOCKSConn* conn)
{
    if (!conn)
	return false;
    YSocksWrapper* w = findWrapper(conn);
    bool ok = (w && w->client());
    if (ok) {
	w->connRecvReply();
	removeSocksConn(conn,"accepted");
    }
    TelEngine::destruct(w);
    return ok;
}

// Connection error
void YSocksEngine::socksConnError(SOCKSConn* conn, bool timeout)
{
    YSocksWrapper* w = findWrapper(conn);
    if (!w)
	return;
    w->connError(timeout);
    TelEngine::destruct(w);
}


/*
 * YSocksWrapper
 */
// Build a wrapper (client if epDef is non 0)
YSocksWrapper::YSocksWrapper(const char* id, YSocksEngine* engine, CallEndpoint* cp,
	NamedList& params, const char* notify, SOCKSEndpointDef* epDef)
    : Mutex(true,"YSocksWrapper"),
    m_state(Pending), m_client(epDef != 0), m_dir(0), m_autoStart(true),
    m_id(id), m_notify(notify), m_callEp(cp), m_dstPort(0), m_srvPort(-1),
    m_engine(engine), m_source(0), m_consumer(0), m_conn(0),
    m_thread(0),
    m_connectToutMs(0), m_connect(0)
{
    debugName(m_id);
    debugChain(&__plugin);
    m_media = params.getValue("media","data");
    m_dstAddr = params.getValue("dst_addr_domain");
    m_dstPort = params.getIntValue("dst_port",0);
    m_dir = lookup(params.getValue("direction"),dict_conn_dir,SOCKSConn::Both);
    m_autoStart = params.getBoolValue("autostart",false);
    if (m_client) {
	m_connectToutMs = SOCKSEngine::getConnectTimeout(params,engine->connectTimeout());
	m_conn = new SOCKSConn(engine,epDef,SOCKSPacket::Connect,
	    SOCKSPacket::Domain,m_dstAddr,m_dstPort);
    }
    else if (m_engine) {
	SOCKSEndpointDef* srv = m_engine->findEpDef("server");
	if (!srv) {
	    const char* lip = params.getValue("localip");
	    int attempts = lip ? 10 : 0;
	    // Try to build our own listener
	    for (int i = 0; i < attempts; i++) {
		int port = (s_minPort + (Random::random() % (s_maxPort - s_minPort))) & 0xfffe;
		srv = new SOCKSEndpointDef(m_id,false,lip,port,0,
		    params.getValue("username"),params.getValue("password"));
		YSocksListenerThread* th = new YSocksListenerThread(m_engine,srv,1);
		th->addAndStart();
		// Wait for the thread to init and start or terminate
		bool ok = false;
		int status = SOCKSListener::Created;
		while (m_engine->hasListener(th,status)) {
		    if (status < SOCKSListener::Listening) {
			Thread::yield();
			continue;
		    }
		    ok = status < SOCKSListener::Terminated;
		    break;
		}
		if (ok) {
		    srv->ref();
		    m_engine->addEpDef(srv);
		    break;
		}
		TelEngine::destruct(srv);
	    }
	}
	if (srv) {
	    m_srvAddr = srv->externalAddr() ? srv->externalAddr() : srv->address();
	    m_srvPort = srv->port();
	    TelEngine::destruct(srv);
	}
    }
    if (canRecv())
	m_recvBuffer.assign(0,s_bufLen);
    Debug(this,DebugAll,"Created client=%s dst=%s:%d dir=%s autostart=%s [%p]",
	String::boolText(m_client),m_dstAddr.c_str(),m_dstPort,
	lookup(m_dir,dict_conn_dir),String::boolText(m_autoStart),this);
}

// Connect socket if client
bool YSocksWrapper::connect()
{
    Lock lck(this);
    if (!(m_engine && m_client && m_conn))
	return false;
    if (m_connect)
	m_connect->cancel();
    m_connect = new YSocksConnectThread(this);
    if (!m_connect->startup()) {
	Debug(this,DebugWarn,"Failed to start connect thread [%p]",this);
	return false;
    }
    XDebug(this,DebugAll,"Started connect thread (%p) [%p]",m_connect,this);
    m_conn->setConnecting();
    u_int64_t tout = 0;
    if (m_connectToutMs)
	tout = Time::now() + m_connectToutMs * 1000 + 500000;
    lck.drop();
    // Wait for connect to complete
    bool timeout = false;
    while (m_connect && !timeout) {
	Thread::idle();
	if (Thread::check(false))
	    break;
	if (tout)
	    timeout = tout < Time::now();
    }
    lck.acquire(this);
    if (m_connect) {
	m_connect->cancel();
	m_connect = 0;
	if (!m_conn)
	    return false;
	m_conn->setSocket(0);
	if (timeout)
	    Debug(this,DebugNote,"Connect timed out [%p]",this);
	else
	    XDebug(this,DebugAll,"Worker cancelled while connecting [%p]",this);
	return false;
    }
    if (m_conn && m_conn->valid() && !Thread::check(false))
	return m_engine->addConnection(m_conn);
    return false;
}

void YSocksWrapper::connectTerminated(YSocksConnectThread* th, Socket* sock, int error,
    bool timeout)
{
    XDebug(this,DebugAll,"connectTerminated(%p,%p,%d,%d) [%p]",th,sock,error,timeout,this);
    if (!(th && m_connect)) {
	if (sock)
	    SOCKSEngine::destroySocket(sock);
	return;
    }
    Lock lck(this);
    if (m_connect != th || !m_conn) {
	if (sock)
	    SOCKSEngine::destroySocket(sock);
	return;
    }
    m_connect = 0;
    m_conn->setSocket(sock);
    if (sock)
	return;
    if (!debugAt(DebugMild))
	return;
    String err;
    if (timeout)
	Debug(this,DebugMild,"Connect to '%s:%d' timeout [%p]",
	    m_conn->epDef()->address().c_str(),m_conn->epDef()->port(),this);
    else {
	String s;
	if (error) {
	    String tmp;
	    Thread::errorString(tmp,error);
	    s << ": " << error << " " << tmp;
	}
	String addr;
	if (m_conn->epDef())
	    addr << m_conn->epDef()->address() << ":" << m_conn->epDef()->port();
	Debug(this,DebugMild,"Failed to connect to '%s'%s [%p]",
	    addr.c_str(),s.safe(),this);
    }
}

// Client connection got reply
void YSocksWrapper::connRecvReply()
{
    if (!m_conn)
	return;
    if (m_state != Pending) {
	Debug(this,DebugNote,"Got reply in non-Pending state [%p]",this);
	return;
    }

    if (m_conn->replyRsp() != SOCKSPacket::EOk) {
	Lock lock(this);
	Debug(this,DebugNote,"Got reply error %u '%s' [%p]",m_conn->replyRsp(),
	    SOCKSPacket::token(m_conn->replyRsp(),SOCKSPacket::s_replyText),this);
	m_state = Terminated;
	m_conn->terminate();
	return;
    }

    DDebug(this,DebugInfo,"Got reply (connection accepted) [%p]",this);
    m_state = WaitStart;
    if (m_autoStart)
	enableDataTransfer();
}

// Connection error while negotiating the protocol
void YSocksWrapper::connError(bool timeout)
{
    Debug(this,DebugNote,"Connection got error while negotiating timeout=%s [%p]",
	String::boolText(timeout),this);
    notify(Terminated);
    stopWorker(false);
    Lock lock(this);
    m_state = Terminated;
    m_conn->terminate();
}

// Set connection with valid request for server wrapper
bool YSocksWrapper::setConn(SOCKSConn* conn)
{
    if (!conn || m_client)
	return false;

    Lock lock(this);
    if (m_conn) {
	Debug(this,DebugNote,"Received duplicate request conn=%s [%p]",
	    conn->toString().c_str(),this);
	return false;
    }
    if (!conn->ref())
	return false;
    Debug(this,DebugAll,"Received valid request conn=%s [%p]",
	conn->toString().c_str(),this);
    m_conn = conn;
    m_state = WaitStart;
    m_conn->sendReply(m_conn->reqAddrType(),m_dstAddr,m_dstPort);
    if (m_autoStart)
	enableDataTransfer();
    lock.drop();
    // Stop listener
    if (m_engine)
	m_engine->removeEpDef(m_id);
    return true;
}

// Read data from conn and forward it
bool YSocksWrapper::recvData()
{
    if (m_state != Running || !m_conn)
	return false;
    // Get source. Set its busy flag
    s_srcMutex.lock();
    YSocksSource* source = m_source;
    if (source && source->alive())
	source->busy(true);
    else
	source = 0;
    s_srcMutex.unlock();
    if (!source)
	return false;
    // The source will not be destroyed until we reset the busy flag
    unsigned int len = m_recvBuffer.length();
    m_conn->recv(m_recvBuffer.data(),len);
    if (!len) {
	if (source->shouldSendEmpty()) {
	    DataBlock block;
	    XDebug(this,DebugAll,"Forwarding empty block [%p]",this);
	    source->Forward(DataBlock::empty());
	}
	source->busy(false);
	return false;
    }
    source->resetSendEmpty();
    DataBlock block;
    block.assign(m_recvBuffer.data(),len,false);
    XDebug(this,DebugAll,"Forwarding %u bytes [%p]",len,this);
    source->Forward(block);
    block.clear(false);
    source->busy(false);
    return true;
}

// Enable data transfer. Change state, set source/consumer format
void YSocksWrapper::enableDataTransfer(const char* format)
{
    Lock lock(this);
    m_format = format;
    if (m_state != WaitStart) {
	m_autoStart = true;
	return;
    }
    Debug(this,DebugInfo,"Enabling data transfer src=%p cons=%p format=%s [%p]",
	m_source,m_consumer,m_format.c_str(),this);
    if (m_conn)
	m_conn->enableDataTransfer();
    // Change format of source and/or consumer,
    //  reinstall them to rebuild codec chains
    if (m_source) {
	if (m_callEp) {
	    m_source->ref();
	    m_callEp->setSource(0,m_media);
	}
	m_source->m_format =  m_format;
	if (m_callEp) {
	    m_callEp->setSource(m_source,m_media);
	    m_source->deref();
	}
    }
    if (m_consumer) {
	if (m_callEp) {
	    m_consumer->ref();
	    m_callEp->setConsumer(0,m_media);
	}
	m_consumer->m_format = m_format;
	if (m_callEp) {
	    m_callEp->setConsumer(m_consumer,m_media);
	    m_consumer->deref();
	}
    }
    m_state = Running;
}

// Build data source
YSocksSource* YSocksWrapper::getSource()
{
    if (!canRecv())
	return 0;
    if (m_source && m_source->ref())
	return m_source;
    return new YSocksSource(this);
}

// Build data consumer
YSocksConsumer* YSocksWrapper::getConsumer()
{
    if (!canSend())
	return 0;
    if (m_consumer && m_consumer->ref())
	return m_consumer;
    return new YSocksConsumer(this);
}

// Build and start worker thread
bool YSocksWrapper::startWorker()
{
    Lock lock(this);
    if (m_thread)
	return true;
    lock.drop();
    m_thread = new YSocksWrapperWorker(this);
    if (m_thread->startup())
	return true;
    m_thread = 0;
    Debug(this,DebugGoOn,"Failed to start worker thread [%p]",this);
    return false;
}

// Build and start worker thread
void YSocksWrapper::stopWorker(bool wait)
{
    Lock lock(this);
    if (!m_thread)
	return;
    if (m_connect) {
	m_connect->cancel();
	m_connect = 0;
	if (m_conn)
	    m_conn->setSocket(0);
    }
    bool hard = (m_conn && m_conn->status() == SOCKSConn::Connecting);
    DDebug(this,DebugAll,"Stopping worker thread hard=%s wait=%s [%p]",
	String::boolText(hard),String::boolText(wait),this);
    m_thread->cancel(hard);
    if (hard) {
	m_thread = 0;
	return;
    }
    if (!wait)
	return;
    lock.drop();
#ifdef XDEBUG
    Debugger debug("YSocksWrapper::stopWorker"," %p crt=%p,'%s' [%p]",
	m_thread,Thread::current(),Thread::currentName(),this);
#endif
    while (m_thread)
	Thread::idle(true);
}

// Get the wrapper id
const String& YSocksWrapper::toString() const
{
    return m_id;
}

// Notify status in chan.notify
void YSocksWrapper::notify(int stat)
{
    Lock lck(this);
    if (m_state == Terminated)
	return;
    if (m_notify.null())
	return;
    const char* what = 0;
    switch (stat) {
	case Established:
	    what = "established";
	    break;
	case Running:
	    what = "running";
	    break;
	case Terminated:
	    what = "terminated";
	    break;
	default:
	    return;
    }
    XDebug(this,DebugAll,"Notifying %s notifier=%s [%p]",what,m_notify.c_str(),this);
    Message* m = new Message("chan.notify");
    m->addParam("module",__plugin.name());
    m->addParam("id",m_id);
    m->addParam("notify",m_notify);
    m->addParam("status",what);
    SocketAddr remote;
    if (!client() && m_conn && m_conn->getAddr(false,remote)) {
	m->addParam("remoteip",remote.host());
	m->addParam("remoteport",String(remote.port()));
    }
    lck.drop();
    Engine::enqueue(m);
}

// Release memory
void YSocksWrapper::destroyed()
{
    if (m_engine) {
	m_engine->removeWrapper(this,false);
	// Stop listener
	if (!m_client)
	    m_engine->removeEpDef(m_id);
    }
    stopWorker(true);
    lock();
    if (m_source && m_source->alive())
	TelEngine::destruct(m_source);
    if (m_consumer && m_consumer->alive())
	TelEngine::destruct(m_consumer);
    SOCKSConn* tmp = m_conn;
    TelEngine::destruct(m_conn);
    if (m_connect) {
	m_connect->cancel();
	m_connect = 0;
    }
    unlock();
    if (m_engine && tmp)
	m_engine->removeSocksConn(tmp,"terminated");
    Debug(this,DebugAll,"Destroyed [%p]",this);
    RefObject::destroyed();
}


/*
 * YSocksWrapperWorker
 */
void YSocksWrapperWorker::run()
{
    if (!m_wrapper)
	return;
    Debug(&__plugin,DebugAll,"Worker started for (%p) '%s' [%p]",
	m_wrapper,m_wrapper->toString().c_str(),this);
    // Use while() to go to method exit point
    while (true) {
	// Connect client wrappers
	if (m_wrapper->client() && !m_wrapper->connect())
	    break;
	if (invalid())
	    break;
	// Wait to transfer data
	// NOTE: The SOCKS protocol is negotiated by the engine
	bool waitStart = !m_wrapper->autoStart();
	while (!invalid() && m_wrapper->state() != YSocksWrapper::Running) {
	    Thread::idle();
	    if (waitStart && m_wrapper->state() == YSocksWrapper::WaitStart) {
		waitStart = false;
		m_wrapper->notify(YSocksWrapper::Established);
	    }
	}
	if (invalid())
	    break;
	m_wrapper->notify(YSocksWrapper::Running);
	// Read data
	while (!invalid()) {
	    if (!m_wrapper->canRecv()) {
		Thread::idle();
		continue;
	    }
	    m_wrapper->recvData();
	    Thread::idle();
	}
	break;
    }
    m_wrapper->notify(YSocksWrapper::Terminated);
    Debug(&__plugin,DebugAll,"Worker terminated for (%p) '%s' [%p]",
	m_wrapper,m_wrapper->toString().c_str(),this);
    m_wrapper->m_thread = 0;
}


/*
 * YSocksSource
 */
YSocksSource::YSocksSource(YSocksWrapper* w)
    : m_wrapper(0), m_busy(false), m_sentEmpty(false)
{
    m_format = "";
    if (w && w->ref()) {
	m_wrapper = w;
	m_format = m_wrapper->m_format;
	m_wrapper->m_source = this;
    }
    Debug(m_wrapper,DebugAll,"YSocksSource(%s) [%p]",
	m_wrapper ? m_wrapper->toString().c_str() : "",this);
}

// Remove from endpoint. Release memory
void YSocksSource::destroyed()
{
    Debug(m_wrapper,DebugAll,"YSocksSource(%s) destroyed [%p]",
	m_wrapper ? m_wrapper->toString().c_str() : "",this);
    if (m_wrapper) {
	s_srcMutex.lock();
	YSocksWrapper* tmp = m_wrapper;
	m_wrapper = 0;
	tmp->m_source = 0;
	s_srcMutex.unlock();
	// Wait for any YSocksWrapper::readData() to finish
	while (m_busy)
	    Thread::yield();
	TelEngine::destruct(tmp);
    }
    DataSource::destroyed();
}


/*
 * YSocksConsumer
 */
YSocksConsumer::YSocksConsumer(YSocksWrapper* w)
    : m_wrapper(0)
{
    m_format = "";
    if (w && w->ref()) {
	m_wrapper = w;
	m_format = m_wrapper->m_format;
	m_wrapper->m_consumer = this;
    }
    Debug(m_wrapper,DebugAll,"YSocksConsumer(%s) [%p]",
	m_wrapper ? m_wrapper->toString().c_str() : "",this);
}

unsigned long YSocksConsumer::Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags)
{
    XDebug(m_wrapper,DebugAll,"Sending %u bytes [%p]",data.length(),m_wrapper);
    unsigned int sent = data.length();
    if (m_wrapper && m_wrapper->state() == YSocksWrapper::Running &&
	m_wrapper->m_conn && m_wrapper->m_conn->send(data.data(),sent))
	return sent;
    return 0;
}

// Remove from endpoint. Release memory
void YSocksConsumer::destroyed()
{
    Debug(m_wrapper,DebugAll,"YSocksConsumer(%s) destroyed [%p]",
	m_wrapper ? m_wrapper->toString().c_str() : "",this);
    if (m_wrapper) {
	YSocksWrapper* tmp = m_wrapper;
	m_wrapper = 0;
	tmp->m_consumer = 0;
	TelEngine::destruct(tmp);
    }
    DataConsumer::destroyed();
}


/*
 * YSocksProcessThread
 */
void YSocksProcessThread::run()
{
    while (s_engine && !s_engine->exiting() && !Engine::exiting()) {
	if (Thread::check(false))
	    break;
	if (s_engine->process())
	    Thread::yield(false);
	else
	    Thread::idle(false);
    }
}


/*
 * YSocksConnectThread
 */
YSocksConnectThread::YSocksConnectThread(YSocksWrapper* w, Thread::Priority prio)
    : Thread("SOCKSConnect",prio),
    m_engine(0), m_port(0), m_toutIntervalMs(0)
{
    if (!(w && w->engine()))
	return;
    m_engine = w->engine();
    m_wrapperId = w->toString();
    m_toutIntervalMs = w->connectTimeoutInterval();
    if (w->conn() && w->conn()->epDef()) {
	m_address = w->conn()->epDef()->address();
	m_port = w->conn()->epDef()->port();
    }
}

void YSocksConnectThread::run()
{
    Socket* sock = 0;
    int error = 0;
    bool tout = false;
    if (m_address)
	sock = SOCKSConn::connect(static_cast<SOCKSEngine*>(m_engine),m_address,m_port,
	    m_toutIntervalMs,error,tout);
    notify(sock,error,tout);
}

void YSocksConnectThread::notify(Socket* sock, int error, bool timeout)
{
    YSocksWrapper* w = m_engine ? m_engine->findWrapper(m_wrapperId) : 0;
    m_engine = 0;
    if (w) {
	w->connectTerminated(this,sock,error,timeout);
	TelEngine::destruct(w);
    }
    else if (sock)
	SOCKSEngine::destroySocket(sock);
}


/*
 * YSocksPlugin
 */
YSocksPlugin::YSocksPlugin()
    : Module("socks","misc",true), m_wrapperId(0), m_init(false)
{
    Output("Loaded module YSOCKS");
}

YSocksPlugin::~YSocksPlugin()
{
    Output("Unloading module YSOCKS");
    if (s_engine)
	delete s_engine;
}

// 'chan.socks' message handler
bool YSocksPlugin::handleChanSocks(Message& msg)
{
    NamedString* module = msg.getParam("module");
    if (module && *module == name())
	return false;
    if (!s_engine)
	return false;

    RefObject* userdata = msg.userData();
    CallEndpoint* cp = static_cast<CallEndpoint*>(userdata ? userdata->getObject(YATOM("CallEndpoint")) : 0);
    if (!cp) {
	Debug(&__plugin,DebugMild,"%s without data endpoint",msg.c_str());
	return 0;
    }

    String* addr = msg.getParam("dst_addr_domain");
    if (null(addr)) {
	Debug(this,DebugNote,"%s with empty dst_addr_domain",msg.c_str());
	return false;
    }

    bool client = msg.getBoolValue("client",true);
    int port = msg.getIntValue("dst_port",0);
    YSocksWrapper* w = s_engine->findWrapper(client,*addr,port);
    XDebug(this,DebugAll,"Processing %s client=%u auth=%s port=%d found %p",
	msg.c_str(),client,addr->c_str(),port,w);
    if (!w) {
	// Get and check required parameters
	// Build client or server wrapper
	SOCKSEndpointDef* epDef = 0;
	if (client) {
	    epDef = new SOCKSEndpointDef("",true,msg.getValue("remoteip"),
		msg.getIntValue("remoteport"),
		msg.getValue("username"),msg.getValue("password"));
	    if (!epDef->address() || epDef->port() <= 0) {
		Debug(&__plugin,DebugMild,"%s with invalid remote addr=%s:%s",
		    msg.c_str(),msg.getValue("remoteip"),msg.getValue("remoteport"));
		TelEngine::destruct(epDef);
		return false;
	    }
	}
	else {
	    epDef = s_engine->findEpDef("server");
	    if (!(epDef || msg.getValue("localip"))) {
		Debug(&__plugin,DebugNote,"%s: No server defined",msg.c_str());
		return false;
	    }
	    TelEngine::destruct(epDef);
	}

	String id;
	buildWrapperId(id);
	w = new YSocksWrapper(id,s_engine,cp,msg,cp->id(),epDef);
	if (!w->startWorker()) {
	    TelEngine::destruct(w);
	    return false;
	}
	s_engine->addWrapper(w);

	if (w->media()) {
	    YSocksSource* s = w->getSource();
	    YSocksConsumer* c = w->getConsumer();
	    cp->setSource(s,w->media());
	    cp->setConsumer(c,w->media());
	    TelEngine::destruct(s);
	    TelEngine::destruct(c);
	}
    }

    // Add server and client params
    if (!w->client()) {
	msg.setParam("address",w->srvAddr());
	msg.setParam("port",String(w->srvPort()));
	// Add remote ip
	SocketAddr remote;
	if (w->conn() && w->conn()->getAddr(false,remote)) {
	    msg.addParam("remoteip",remote.host());
	    msg.addParam("remoteport",String(remote.port()));
	}
    }
    msg.setParam("notifier",w->toString());
    // Start ?
    const char* format = msg.getValue("format");
    if (!null(format))
	w->enableDataTransfer(format);
    return !w->deref();
}

void YSocksPlugin::initialize()
{
    Output("Initializing module YSOCKS");
    Configuration cfg(Engine::configFile("ysockschan"));

    NamedList dummy("");
    NamedList* general = cfg.getSection("general");
    if (!general)
	general = &dummy;

    if (!m_init) {
	s_statusCmd = "status " + name();
	setup();
	installRelay(Halt);
	installRelay(ChanSocks,"chan.socks",50);
	s_engine = new YSocksEngine(*general);
	(new YSocksProcessThread)->startup();
    }
    m_init = true;

    s_engine->initialize(*general);

    int tmp = general->getIntValue("buflen",4096);
    s_bufLen = (tmp >= 1024 ? tmp : 1024);

    // Update proxy list
    unsigned int n = cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = cfg.getSection(i);
	if (!sect || *sect == "general")
	    continue;

	bool enabled = sect->getBoolValue("enable",false);
	if (!enabled) {
	    s_engine->removeEpDef(*sect);
	    continue;
	}

	SOCKSEndpointDef* epDef = new SOCKSEndpointDef(*sect);
	if (epDef->address().null() || epDef->port() < 0) {
	    Debug(this,DebugNote,"Invalid endpoint def '%s' in config (addr=%s port=%s)",
		sect->c_str(),epDef->address().c_str(),sect->getValue("port"));
	    TelEngine::destruct(epDef);
	    continue;
	}
	// Check changes
	SOCKSEndpointDef* p = s_engine->findEpDef(*sect);
	if (p) {
	    if (p->address() == epDef->address() && p->port() == epDef->port() &&
		p->username() == epDef->username() && p->password() == epDef->password())
		TelEngine::destruct(epDef);
	    else
		s_engine->removeEpDef(*sect);
	    TelEngine::destruct(p);
	}
	if (epDef) {
	    if (*sect == "server" || sect->getBoolValue("incoming",true))
		(new YSocksListenerThread(s_engine,epDef,5))->addAndStart();
	    s_engine->addEpDef(epDef);
	}
    }
}

// Common message relay handler
bool YSocksPlugin::received(Message& msg, int id)
{
    if (id == ChanSocks)
	return handleChanSocks(msg);
    if (id == Status) {
	String target = msg.getValue("module");
	// Target is the driver or channel
	if (!target || target == name())
	    return Module::received(msg,id);
	// Check additional commands
	if (!target.startSkip(name(),false))
	    return false;
	target.trimBlanks();
	if (target == "listeners") {
	    lock();
	    msg.retValue() << "name=" << name() << ",type=" << type();
	    unlock();
	    if (!(s_engine && s_engine->lock(1000000)))
		return true;
	    msg.retValue() << ";count=" << s_engine->listeners().count();
	    msg.retValue() << ";format=Status";
	    for (ObjList* o = s_engine->listeners().skipNull(); o; o = o->skipNext()) {
		ListenerPointer* p = static_cast<ListenerPointer*>(o->get());
		SocketAddr addr;
		(*p)->getAddr(addr);
		msg.retValue() << ";" << addr.host() << ":" << addr.port();
		msg.retValue() << "=" << lookup((*p)->status(),SOCKSListener::s_statusName);
	    }
	    s_engine->unlock();
	    msg.retValue() << "\r\n";
	}
	return false;
    }
    if (id == Halt)
	unload();
    return Module::received(msg,id);
}

void YSocksPlugin::statusParams(String& str)
{
    if (!s_engine)
	return;
    Lock lock(s_engine);
    str.append("wrappers=",",") << s_engine->m_wrappers.count();
}

void YSocksPlugin::statusDetail(String& str)
{
    if (!s_engine)
	return;
    Lock lock(s_engine);
    str.append("format=Notify|ConnStatus");
    for (ObjList* o = s_engine->m_wrappers.skipNull(); o; o = o->skipNext()) {
	YSocksWrapper* w = static_cast<YSocksWrapper*>(o->get());
	Lock lockW(w);
        str.append(w->toString(),";") << "=" << w->notify();
	str << "|" << SOCKSConn::statusName(w->conn() ? w->conn()->status() : SOCKSConn::Terminated);
    }
}

// Unload the module: uninstall the relays
bool YSocksPlugin::unload()
{
    DDebug(this,DebugAll,"Cleanup");
    if (s_engine) {
	s_engine->setExiting();
	s_engine->cleanup();
    }
    if (!lock(500000))
	return false;
    uninstallRelays();
    unlock();
    return true;
}

// Handle command complete requests
bool YSocksPlugin::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;

    bool status = partLine.startsWith("status");
    if (!status)
	return Module::commandComplete(msg,partLine,partWord);

    // Add additional commands
    if (partLine == s_statusCmd) {
	for (String* list = s_statusCmds; !null(list); list++)
	    if (!partWord || list->startsWith(partWord))
		Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
